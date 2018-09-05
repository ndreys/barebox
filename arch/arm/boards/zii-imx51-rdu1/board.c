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
#include <init.h>
#include <environment.h>
#include <mach/bbu.h>
#include <libfile.h>
#include <mach/imx5.h>
#include <net.h>
#include <linux/crc8.h>
#include <linux/sizes.h>
#include <linux/nvmem-consumer.h>
#include <mach/zii/pn-fixup.h>

#include <envfs.h>

struct zii_rdu1_lru_fixup {
	struct zii_pn_fixup fixup;
	uint8_t touchscreen_address;
	const char *display;
	const char *mode_name;
};

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

static int zii_rdu1_fixup_disable_pic(struct device_node *root, void *context)
{
	struct device_node *rave_sp;

	rave_sp = of_find_node_by_name(root, "rave-sp");
	if (WARN_ON(!rave_sp))
		return -ENOENT;

	of_delete_node(rave_sp);

	return 0;
}

static int zii_rdu1_disable_sp_node(void)
{
	struct device_node *np;
	u8 *disable_pic;

	if (!of_machine_is_compatible("zii,imx51-rdu1"))
		return 0;

	np = of_find_node_by_name(NULL, "eeprom@a4");
	if (WARN_ON(!np))
		return -ENOENT;

	disable_pic = nvmem_cell_get_and_read(np, "disable-pic", 1);
	if (IS_ERR(disable_pic))
		return PTR_ERR(disable_pic);

	if (*disable_pic) {
		pr_info("PIC is disabled, run "
			"\"memset -b -d /dev/main-eeprom 0xa2 0 1\""
			" to enable\n");
		of_register_fixup(zii_rdu1_fixup_disable_pic, NULL);
	} else {
		pr_info("PIC is enabled, run "
			"\"memset -b -d /dev/main-eeprom 0xa2 1 1\""
			" to disable\n");
	}

	free(disable_pic);

	return 0;
}
late_initcall(zii_rdu1_disable_sp_node);

static void zii_rdu1_sp_switch_eeprom(const struct zii_pn_fixup *fixup)
{
	struct device_node *np, *root;
	struct cdev *cdev;

	root = of_get_root_node();
	np   = of_find_node_by_name(root, "eeprom@ae");
	if (WARN_ON(!np))
		return;

	cdev = cdev_by_device_node(np);
	if (!cdev) {
		pr_err("Couldn't find switch eeprom\n");
		return;
	}

	WARN_ON(devfs_create_link(cdev, "switch-eeprom"));
}

static const struct zii_pn_fixup zii_rdu1_dds_fixups[] = {
	{ "05-0004-02-E3", zii_rdu1_sp_switch_eeprom }
};

static int zii_rdu1_fixup_touchscreen(struct device_node *root, void *context)
{
	const struct zii_rdu1_lru_fixup *fixup = context;
	char touchscreen[sizeof("touchscreen@NN") + 1];
	struct device_node *i2c_node, *node;

	i2c_node = of_find_node_by_alias(root, "i2c1");
	if (!i2c_node)
		return -ENODEV;

	snprintf(touchscreen, sizeof(touchscreen), "touchscreen@%02x",
		 fixup->touchscreen_address);

	node = of_find_node_by_name(i2c_node, touchscreen);
	if (!node)
		return -ENODEV;

	pr_info("Enabling touchscreen device \"%s\".\n", touchscreen);

	of_device_enable(node);
	return 0;
}

static int zii_rdu1_fixup_display(struct device_node *root, void *context)
{
	const struct zii_rdu1_lru_fixup *fixup = context;
	struct device_node *node;

	pr_info("Enabling panel with compatible \"%s\".\n", fixup->display);

	node = of_find_node_by_name(root, "panel");
	if (!node)
		return -ENODEV;

	of_device_enable(node);
	of_property_write_string(node, "compatible", fixup->display);

	return 0;
}

static void zii_rdu1_lru_fixup(const struct zii_pn_fixup *context)
{
	const struct zii_rdu1_lru_fixup *fixup =
		container_of(context, struct zii_rdu1_lru_fixup, fixup);
	struct device_d *fb0;

	pr_info("%s: %s %x", fixup->fixup.pn, fixup->display,
		fixup->touchscreen_address);

	if (fixup->touchscreen_address)
		of_register_fixup(zii_rdu1_fixup_touchscreen, (void *)fixup);

	if (!fixup->display)
		return;

	of_register_fixup(zii_rdu1_fixup_display, (void *)fixup);

	fb0 = get_device_by_name("fb0");
	if (!fb0) {
		pr_warn("fb0 device not found!\n");
		return;
	}

	if (dev_set_param(fb0, "mode_name", fixup->mode_name)) {
		pr_warn("failed to set fb0.mode_name to \'%s\'\n",
			fixup->mode_name);
		return;
	}
}

#define RDU1_LRU_FIXUP(__pn, __touchscreen_address, __panel)	\
	{							\
		{ __pn, zii_rdu1_lru_fixup },			\
		__touchscreen_address,				\
	       __panel						\
	}

#define RDU1_PANEL_UNKNOWN	NULL, NULL
#define RDU1_PANEL_8P9		"toshiba,lt089ac29000", "toshiba89"
#define RDU1_PANEL_12P1		RDU1_PANEL_UNKNOWN
#define RDU1_PANEL_15P3		RDU1_PANEL_UNKNOWN
#define RDU1_PANEL_15P4		"innolux,g154i1-le1", "chimei15"
#define RDU1_PANEL_17P5		RDU1_PANEL_UNKNOWN

static const struct zii_rdu1_lru_fixup zii_rdu1_lru_fixups[] = {
	RDU1_LRU_FIXUP("00-5103-01", 0x4c, RDU1_PANEL_12P1),
	RDU1_LRU_FIXUP("00-5103-30", 0x20, RDU1_PANEL_12P1),
	RDU1_LRU_FIXUP("00-5103-31", 0x20, RDU1_PANEL_12P1),
	RDU1_LRU_FIXUP("00-5104-03", 0x00, RDU1_PANEL_15P3),
	RDU1_LRU_FIXUP("00-5105-01", 0x4b, RDU1_PANEL_8P9),
	RDU1_LRU_FIXUP("00-5105-20", 0x4b, RDU1_PANEL_8P9),
	RDU1_LRU_FIXUP("00-5105-30", 0x20, RDU1_PANEL_8P9),
	RDU1_LRU_FIXUP("00-5108-01", 0x4c, RDU1_PANEL_15P3),
	RDU1_LRU_FIXUP("00-5109-01", 0x00, RDU1_PANEL_15P3),
	RDU1_LRU_FIXUP("00-5118-30", 0x20, RDU1_PANEL_15P4),
	RDU1_LRU_FIXUP("00-5506-01", 0x4c, RDU1_PANEL_15P4),
	RDU1_LRU_FIXUP("00-5507-01", 0x4c, RDU1_PANEL_17P5),
	RDU1_LRU_FIXUP("00-5511-01", 0x4c, RDU1_PANEL_15P4),
	RDU1_LRU_FIXUP("00-5512-01", 0x4c, RDU1_PANEL_17P5),
};

static int zii_rdu1_process_fixups(void)
{
	if (!of_machine_is_compatible("zii,imx51-rdu1"))
		return 0;

	zii_process_dds_fixups(zii_rdu1_dds_fixups);
	zii_process_lru_fixups(zii_rdu1_lru_fixups);

	return 0;
}
postmmu_initcall(zii_rdu1_process_fixups);
