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
#include <mach/bbu.h>
#include <libfile.h>
#include <mach/imx5.h>

#include <envfs.h>

#include "pn-fixup.h"

static int zii_rdu1_init(void)
{
	const char *hostname;

	if (!of_machine_is_compatible("zii,imx51-rdu1") &&
	    !of_machine_is_compatible("zii,imx51-scu2-mezz"))
		return 0;

	hostname = of_get_machine_compatible() + strlen("imx51-");

	imx51_babbage_power_init();

	barebox_set_hostname(hostname);

	imx51_bbu_internal_mmc_register_handler("mmc", "/dev/mmc0", 0);
	imx51_bbu_internal_spi_i2c_register_handler("spi",
		"/dev/dataflash0.barebox",
		BBU_HANDLER_FLAG_DEFAULT |
		IMX_BBU_FLAG_PARTITION_STARTS_AT_HEADER);

	defaultenv_append_directory(defaultenv_zii_imx51_rdu1);

	return 0;
}
coredevice_initcall(zii_rdu1_init);

static void zii_rdu1_sp_switch_eeprom(void)
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

static int zii_rdu1_process_fixups(void)
{
	int ret;

	if (!of_machine_is_compatible("zii,imx51-rdu1"))
		return 0;

	ret = zii_process_dds_fixups(ARRAY_AND_SIZE(zii_rdu1_dds_fixups));
	/*
	 * TODO: Add RDU p/n fixups
	 */

	return ret;
}
postmmu_initcall(zii_rdu1_process_fixups);
