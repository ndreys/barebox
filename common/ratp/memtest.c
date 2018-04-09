/*
 * Copyright (c) 2018 Sascha Hauer <s.hauer@pengutronix.de>, Pengutronix
 *
 * See file CREDITS for list of people who contributed to this
 * project.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

/*
 * memtest
 */

#include <common.h>
#include <memory.h>
#include <errno.h>
#include <memtest.h>
#include <mmu.h>
#include <ratp.h>
#include <ratp_bb.h>
#include <malloc.h>

struct ratp_bb_memtest_request {
	struct ratp_bb header;
	uint16_t buffer_offset;
	uint8_t  cached;
	uint8_t  uncached;
	uint8_t  bus_only;
	uint8_t  thorough;
	uint8_t  buffer[];
} __attribute__((packed));

struct ratp_bb_memtest_progress {
	struct ratp_bb header;
	uint16_t buffer_offset;
	uint16_t step_description_size;
	uint16_t step_description_offset;
	uint64_t progress_offset;
	uint64_t progress_max;
	uint8_t  buffer[];
} __attribute__((packed));

struct ratp_bb_memtest_response {
	struct ratp_bb header;
	uint16_t buffer_offset;
	uint32_t errno;
	uint16_t error_description_size;
	uint16_t error_description_offset;
	uint64_t expected_value;
	uint64_t actual_value;
	uint64_t address;
	uint8_t  buffer[];
} __attribute__((packed));

struct memtest_ctx {
	struct ratp *ratp;
	struct ratp_bb_memtest_response *failure_msg;
	int failure_msg_len;
};

static struct ratp_bb_memtest_response *build_response (
	int *response_len,
	int errno,
	const char *failure_description,
	resource_size_t expected_value,
	resource_size_t actual_value,
	volatile resource_size_t *address)
{
	struct ratp_bb_memtest_response *msg;
	int description_len;

	description_len = failure_description ? strlen (failure_description) : 0;

	msg = xzalloc(sizeof(*msg) + description_len);
	msg->header.type = cpu_to_be16(BB_RATP_TYPE_MEMTEST_RETURN);
	msg->buffer_offset = cpu_to_be16(sizeof(*msg));
	msg->errno = cpu_to_be32(errno);
	msg->error_description_size = cpu_to_be16(description_len);
	msg->error_description_offset = 0;
	msg->expected_value = cpu_to_be64((uint64_t)expected_value);
	msg->actual_value = cpu_to_be64((uint64_t)actual_value);
	msg->address = cpu_to_be64((uint64_t)((unsigned int)address));
	if (description_len)
		memcpy (msg->buffer, failure_description, description_len);

	*response_len = sizeof(*msg) + description_len;
	return msg;
}

static void report_failure(const char *failure_description,
			   resource_size_t expected_value,
			   resource_size_t actual_value,
			   volatile resource_size_t *address,
			   void *user_data)
{
	struct memtest_ctx *ctx = (struct memtest_ctx *) user_data;

	/* We don't report this message right away, as this is the response of
	 * the command. Instead, just store it to be returned once the test
	 * operation exits. */

	if (ctx->failure_msg)
		return;

	ctx->failure_msg = build_response(&ctx->failure_msg_len,
					  EIO, failure_description,
					  expected_value, actual_value,
					  address);
}

static int report_progress (const char *description,
			    resource_size_t offset,
			    resource_size_t max_progress,
			    void *user_data)
{
	struct memtest_ctx *ctx = (struct memtest_ctx *) user_data;
	struct ratp_bb_memtest_progress *msg;
	int description_len;

	description_len = description ? strlen (description) : 0;

	msg = xzalloc(sizeof(*msg) + description_len);
	msg->header.type = cpu_to_be16(BB_RATP_TYPE_MEMTEST_PROGRESS);
	msg->buffer_offset = cpu_to_be16(sizeof(*msg));

	msg->step_description_size = cpu_to_be16(description_len);
	msg->step_description_offset = 0;
	msg->progress_offset = cpu_to_be64((uint64_t)offset);
	msg->progress_max = cpu_to_be64((uint64_t)max_progress);

	if (description_len)
		memcpy (msg->buffer, description, description_len);

	ratp_send(ctx->ratp, msg, sizeof(*msg) + description_len);
	ratp_poll_sync(ctx->ratp, 100);

	return 0;
}

static int do_test_one_area(struct memtest_ctx *ctx,
			    struct mem_test_resource *r,
			    int bus_only,
			    unsigned cache_flag)
{
	int ret;

	remap_range((void *)r->r->start, r->r->end -
			r->r->start + 1, cache_flag);

	ret = mem_test_bus_integrity(r->r->start, r->r->end,
				     report_progress, report_failure,
				     ctx);
	if (ret < 0)
		return ret;

	if (bus_only)
		return 0;

	return mem_test_moving_inversions(r->r->start, r->r->end,
					  report_progress, report_failure,
					  ctx);
}

static int do_memtest_thorough(struct memtest_ctx *ctx,
			       struct list_head *memtest_regions,
			       int bus_only, unsigned cache_flag)
{
	struct mem_test_resource *r;
	int ret;

	list_for_each_entry(r, memtest_regions, list) {
		ret = do_test_one_area(ctx, r, bus_only, cache_flag);
		if (ret)
			return ret;
	}

	return 0;
}

static int do_memtest_biggest(struct memtest_ctx *ctx,
			      struct list_head *memtest_regions,
			      int bus_only, unsigned cache_flag)
{
	struct mem_test_resource *r;

	r = mem_test_biggest_region(memtest_regions);
	if (!r)
		return -EINVAL;

	return do_test_one_area(ctx, r, bus_only, cache_flag);
}

static int ratp_cmd_memtest(struct ratp *ratp,
			    const struct ratp_bb *req, int req_len,
			    struct ratp_bb **rsp, int *rsp_len)
{
	struct ratp_bb_memtest_request *memtest_req = (struct ratp_bb_memtest_request *)req;
	int (*memtest)(struct memtest_ctx *, struct list_head *, int, unsigned);
	struct list_head memtest_used_regions;
	struct memtest_ctx memtest_ctx = {
		.ratp = ratp,
		.failure_msg = NULL,
	};

	memtest = memtest_req->thorough ? do_memtest_thorough : do_memtest_biggest;

	if (!arch_can_remap() && (memtest_req->cached || memtest_req->uncached)) {
		*rsp = (struct ratp_bb *)build_response(rsp_len, EINVAL, "Cannot map cached or uncached", 0, 0, 0);
		return 0;
	}

	INIT_LIST_HEAD(&memtest_used_regions);
	if (mem_test_request_regions(&memtest_used_regions) < 0) {
		*rsp = (struct ratp_bb *)build_response(rsp_len, EINVAL, "Cannot request memory regions", 0, 0, 0);
		return 0;
	}

	if (memtest_req->cached &&
	    (memtest(&memtest_ctx, &memtest_used_regions, memtest_req->bus_only, MAP_CACHED) < 0))
		goto out;

	if (memtest_req->uncached &&
	    (memtest(&memtest_ctx, &memtest_used_regions, memtest_req->bus_only, MAP_UNCACHED) < 0))
		goto out;

	if (!memtest_req->cached &&
	    !memtest_req->uncached &&
	    (memtest(&memtest_ctx, &memtest_used_regions, memtest_req->bus_only, MAP_DEFAULT) < 0))
		goto out;

out:
	mem_test_release_regions(&memtest_used_regions);

	if (memtest_ctx.failure_msg) {
		*rsp = (struct ratp_bb *) memtest_ctx.failure_msg;
		*rsp_len = memtest_ctx.failure_msg_len;
		return 0;
	}

	*rsp = (struct ratp_bb *)build_response(rsp_len, 0, NULL, 0, 0, 0);
	return 0;
}

BAREBOX_RATP_CMD_START(MEMTEST)
	.request_id = BB_RATP_TYPE_MEMTEST,
	.response_id = BB_RATP_TYPE_MEMTEST_RETURN,
	.cmd = ratp_cmd_memtest
BAREBOX_RATP_CMD_END
