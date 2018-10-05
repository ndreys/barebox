// SPDX-License-Identifier: GPL-2.0+

/*
 * Copyright (C) 2017 Zodiac Inflight Innovation
 *
 * Author: Andrey Smirnov <andrew.smirnov@gmail.com>
 *
 * based on previous iterations of this code
 *
 *     Copyright (C) 2015 Nikita Yushchenko, CogentEmbedded, Inc
 *     Copyright (C) 2015 Andrey Gusakov, CogentEmbedded, Inc
 *
 * based on similar i.MX51 EVK (Babbage) board support code
 *
 *     Copyright (C) 2007 Sascha Hauer, Pengutronix
 */

#include <common.h>
#include <fcntl.h>
#include <fs.h>
#include <init.h>
#include <environment.h>
#include <libfile.h>
#include <mach/bbu.h>
#include <mach/imx5.h>
#include <net.h>
#include <linux/crc8.h>
#include <linux/sizes.h>
#include <linux/nvmem-consumer.h>
#include "../zii-common/pn-fixup.h"

#include <envfs.h>

static int zii_rdu1_init(void)
{
	const char *hostname;

	if (!of_machine_is_compatible("zii,imx51-rdu1") &&
	    !of_machine_is_compatible("zii,imx51-scu2-mezz") &&
	    !of_machine_is_compatible("zii,imx51-scu3-esb"))
		return 0;

	hostname = of_get_machine_compatible() + strlen("imx51-");

	imx51_babbage_power_init();

	barebox_set_hostname(hostname);

	imx51_bbu_internal_mmcboot_register_handler("eMMC", "/dev/mmc0", 0);
	imx51_bbu_internal_spi_i2c_register_handler("SPI",
		"/dev/dataflash0.barebox",
		BBU_HANDLER_FLAG_DEFAULT |
		IMX_BBU_FLAG_PARTITION_STARTS_AT_HEADER);

	return 0;
}
coredevice_initcall(zii_rdu1_init);

#define KEY		0
#define VALUE		1
#define STRINGS_NUM	2

static int zii_rdu1_load_config(void)
{
	struct device_node *np, *root;
	size_t len, remaining_space;
	const uint8_t crc8_polynomial = 0x8c;
	DECLARE_CRC8_TABLE(crc8_table);
	const char *cursor, *end;
	const char *file = "/dev/dataflash0.config";
	uint8_t *config;
	int ret = 0;
	enum {
		BLOB_SPINOR,
		BLOB_RAVE_SP_EEPROM,
		BLOB_MICROWIRE,
	} blob;

	if (!of_machine_is_compatible("zii,imx51-rdu1"))
		return 0;

	crc8_populate_lsb(crc8_table, crc8_polynomial);

	for (blob = BLOB_SPINOR; blob <= BLOB_MICROWIRE; blob++) {
		switch (blob) {
		case BLOB_MICROWIRE:
			file = "/dev/microwire-eeprom";
			/* FALLTHROUGH */
		case BLOB_SPINOR:
			config = read_file(file, &remaining_space);
			if (!config) {
				pr_err("Failed to read %s\n", file);
				return -EIO;
			}
			break;
		case BLOB_RAVE_SP_EEPROM:
			/* Needed for error logging below */
			file = "shadow copy in RAVE SP EEPROM";

			root = of_get_root_node();
			np   = of_find_node_by_name(root, "eeprom@a4");
			if (!np)
				return -ENODEV;

			pr_info("Loading %s, this may take a while\n", file);

			remaining_space = SZ_1K;
			config = nvmem_cell_get_and_read(np, "shadow-config",
							 remaining_space);
			if (IS_ERR(config))
				return PTR_ERR(config);

			break;
		}

		/*
		 * The environment blob has its CRC8 stored as the
		 * last byte of the blob, so calculating CRC8 over the
		 * whole things should return 0
		 */
		if (crc8(crc8_table, config, remaining_space, 0)) {
			pr_err("CRC mismatch for %s\n", file);
			free(config);
			config = NULL;
		} else {
			/*
			 * We are done if there's a blob with a valid
			 * CRC8
			 */
			break;
		}
	}

	if (!config) {
		pr_err("No valid config blobs were found\n");
		ret = -EINVAL;
		goto free_config;
	}

	/*
	 * Last byte is CRC8, so it is of no use for our parsing
	 * algorithm
	 */
	remaining_space--;

	cursor = config;
	end = cursor + remaining_space;

	/*
	 * The environemnt is stored a a bunch of zero-terminated
	 * ASCII strings in "key":"value" pairs
	 */
	while (cursor < end) {
		const char *strings[STRINGS_NUM] = { NULL, NULL };
		char *key;
		unsigned int i;

		for (i = 0; i < ARRAY_SIZE(strings); i++) {
			if (!*cursor) {
				/* We assume that last key:value pair
				 * will be terminated by an extra '\0'
				 * at the end */
				goto free_config;
			}

			len = strnlen(cursor, remaining_space);
			if (len >= remaining_space) {
				ret = -EOVERFLOW;
				goto free_config;
			}

			strings[i] = cursor;

			len++;	/* Account for '\0' at the end of the string */
			cursor += len;
			remaining_space -= len;

			if (cursor > end) {
				ret = -EOVERFLOW;
				goto free_config;
			}
		}

		key = basprintf("config_%s", strings[KEY]);
		ret = setenv(key, strings[VALUE]);
		free(key);

		if (ret)
			goto free_config;
	}

free_config:
	free(config);
	return ret;
}
late_initcall(zii_rdu1_load_config);

static int zii_rdu1_ethernet_init(void)
{
	const char *mac_string;
	struct device_node *np, *root;
	uint8_t mac[ETH_ALEN];
	int ret;

	if (!of_machine_is_compatible("zii,imx51-rdu1"))
		return 0;

	root = of_get_root_node();

	np = of_find_node_by_alias(root, "ethernet0");
	if (!np) {
		pr_warn("Failed to find ethernet0\n");
		return -ENOENT;
	}

	mac_string = getenv("config_mac");
	if (!mac_string)
		return -ENOENT;

	ret = string_to_ethaddr(mac_string, mac);
	if (ret < 0)
		return ret;

	of_eth_register_ethaddr(np, mac);
	return 0;
}
/* This needs to happen only after zii_rdu1_load_config was
 * executed */
environment_initcall(zii_rdu1_ethernet_init);

static void zii_rdu1_sp_switch_eeprom(const struct zii_pn_fixup *fixup)
{
	struct device_node *np, *root, *sp_node;
	struct device_d *sp_dev;

	root = of_get_root_node();
	np   = of_find_node_by_name(root, "eeprom@ae");
	if (WARN_ON(!np))
		return;

	sp_node = of_find_node_by_name(root, "rave-sp");
	if (WARN_ON(!sp_node))
		return;

	sp_dev = of_find_device_by_node(sp_node);
	if (WARN_ON(!sp_dev))
		return;

	of_device_enable(np);

	WARN_ON(!of_platform_device_create(np, sp_dev));
}

static const struct zii_pn_fixup zii_rdu1_dds_fixups[] = {
	{ "05-0004-02-E3", zii_rdu1_sp_switch_eeprom }
};

static int zii_rdu1_process_fixups(void)
{
	if (!of_machine_is_compatible("zii,imx51-rdu1"))
		return 0;

	zii_process_dds_fixups(zii_rdu1_dds_fixups);

	return 0;
}
postmmu_initcall(zii_rdu1_process_fixups);

static struct rdu1_display_type {
	const char *substr;	/* how it is described in spinor */
	const char *mode_name;	/* mode_name for barebox DT */
	const char *compatible;	/* compatible for kernel DT */
} display_types[] = {
	{ "Toshiba89", "toshiba89", "toshiba,lt089ac29000" },
	{ "CHIMEI15", "chimei15", "innolux,g154i1-le1" },
	{ "NEC12", "nec12", "nec,nl12880bc20-05" },
}, *current_dt;

static int rdu1_fixup_display(struct device_node *root, void *context)
{
	struct device_node *node;

	pr_info("Enabling panel with compatible \"%s\".\n",
		current_dt->compatible);

	node = of_find_node_by_name(root, "panel");
	if (!node)
		return -ENODEV;

	of_device_enable(node);
	of_set_property(node, "compatible", current_dt->compatible,
			strlen(current_dt->compatible) + 1, 1);

	return 0;
}

static void rdu1_display_setup(void)
{
	struct device_d *fb0;
	size_t size;
	void *buf;
	int ret, i, pos = 0;

	ret = read_file_2("/dev/dataflash0.config", &size, &buf, 512);
	if (ret && ret != -EFBIG) {
		pr_err("Failed to read settings from SPI flash!\n");
		return;
	}

	while (pos < size) {
		char *cur = buf + pos;

		pos += strlen(cur) + 1;

		if (strstr(cur, "video"))
			break;
	}

	/* Sanity check before we try any further detection */
	if (pos >= size) {
		pr_err("Config flash doesn't contain video setting!\n");
		free(buf);
		return;
	}

	for (i = 0; i < ARRAY_SIZE(display_types); i++) {
		if (strstr(buf + pos, display_types[i].substr)) {
			current_dt = &display_types[i];
			break;
		}
	}

	free(buf);

	if (!current_dt) {
		pr_warn("No known display configuration found!\n");
		return;
	}

	fb0 = get_device_by_name("fb0");
	if (!fb0) {
		pr_warn("fb0 device not found!\n");
		return;
	}

	ret = dev_set_param(fb0, "mode_name", current_dt->mode_name);
	if (ret) {
		pr_warn("failed to set fb0.mode_name to \'%s\'\n",
			current_dt->mode_name);
		return;
	}

	of_register_fixup(rdu1_fixup_display, NULL);
}

static char *part_number;

static void fetch_part_number(void)
{
	int fd, ret;
	uint8_t buf[0x21];

	fd = open_and_lseek("/dev/main-eeprom", O_RDONLY, 0x20);
	if (fd < 0)
		return;

	ret = read(fd, buf, 32);
	if (ret < 32)
		return;

	if (buf[0] < 1 || buf[0] > 31)
		return;

	buf[buf[0]+1] = 0;
	part_number = strdup(&buf[1]);
	if (part_number)
		pr_info("RDU1 part number: %s\n", part_number);
}

static int rdu1_fixup_touchscreen(struct device_node *root, void *context)
{
	struct device_node *i2c2_node, *node;
	int i;

	static struct {
		const char *part_number_prefix;
		const char *node_name;
	} table[] = {
		{ "00-5103-01", "touchscreen@4c" },
		{ "00-5103-30", "touchscreen@20" },
		{ "00-5103-31", "touchscreen@20" },
		{ "00-5105-01", "touchscreen@4b" },
		{ "00-5105-20", "touchscreen@4b" },
		{ "00-5105-30", "touchscreen@20" },
		{ "00-5107-01", "touchscreen@4b" },
		{ "00-5108-01", "touchscreen@4c" },
		{ "00-5118-30", "touchscreen@20" },
	};

	if (!part_number)
		return 0;

	for (i = 0; i < ARRAY_SIZE(table); i++) {
		const char *prefix = table[i].part_number_prefix;
		if (!strncmp(part_number, prefix, strlen(prefix)))
			break;
	}

	if (i == ARRAY_SIZE(table))
		return 0;

	pr_info("Enabling \"%s\".\n", table[i].node_name);

	i2c2_node = of_find_node_by_alias(root, "i2c1");   /* i2c1 = &i2c2 */
	if (!i2c2_node)
		return 0;

	node = of_find_node_by_name(i2c2_node, table[i].node_name);
	if (!node)
		return 0;

	of_device_enable(node);
	return 0;
}

static int rdu1_late_init(void)
{
	if (!of_machine_is_compatible("zii,imx51-rdu1"))
		return 0;

	rdu1_display_setup();

	fetch_part_number();
	of_register_fixup(rdu1_fixup_touchscreen, NULL);

	return 0;
}
late_initcall(rdu1_late_init);
