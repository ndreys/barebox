/*
 * Copyright (C) 2018 Sascha Hauer, Pengutronix
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation.
 *
 */

#include <common.h>
#include <init.h>
#include <asm/memory.h>
#include <linux/sizes.h>
#include <linux/phy.h>
#include <linux/micrel_phy.h>
#include <mach/bbu.h>

#include <envfs.h>

static int ksz8091_phy_fixup(struct phy_device *dev)
{

#define KSZ8081_MODE_OVERRIDE			0x16

#define KSZ8081_MODE_OVERRIDE_RMII		BIT(1)
#define KSZ8081_MODE_OVERRIDE_B_CAST_OFF	BIT(9)

#define KSZ8081_PHY_CTL2			0x1F	
#define KSZ8081_PHY_CTL2_HP_MDX			BIT(15)
#define KSZ8081_PHY_CTL2_ENABLE_JABBER		BIT(8)
#define KSZ8081_PHY_CTL2_RMII_50MHZ		BIT(7)
#define KSZ8081_PHY_CTL2_LED_MODE_ACTIVITY_LINK	BIT(4)
	
	phy_write(dev, KSZ8081_PHY_CTL2,
		  KSZ8081_PHY_CTL2_HP_MDX |
		  KSZ8081_PHY_CTL2_ENABLE_JABBER |
		  KSZ8081_PHY_CTL2_LED_MODE_ACTIVITY_LINK);

	phy_write(dev, KSZ8081_MODE_OVERRIDE,
		  KSZ8081_MODE_OVERRIDE_RMII |
		  KSZ8081_MODE_OVERRIDE_B_CAST_OFF);

	return 0;
}

static int zii_imx8mq_rdu3_init(void)
{
	if (!of_machine_is_compatible("zii,imx8mq-rdu3"))
		return 0;

	barebox_set_hostname("imx8mq-zii-rdu3");

	defaultenv_append_directory(defaultenv_zii_rdu3);

	phy_register_fixup_for_uid(PHY_ID_KSZ8081, MICREL_PHY_ID_MASK,
				   ksz8091_phy_fixup);

	imx6_bbu_internal_spi_i2c_register_handler("SPI", "/dev/m25p0.barebox",
						   BBU_HANDLER_FLAG_DEFAULT);

	imx6_bbu_internal_mmc_register_handler("eMMC", "/dev/mmc0", 0);

	return 0;
}
device_initcall(zii_imx8mq_rdu3_init);
