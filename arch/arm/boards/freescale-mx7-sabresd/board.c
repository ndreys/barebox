/*
 * Copyright (C) 2017 Sascha Hauer, Pengutronix
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
 */

#include <common.h>
#include <init.h>
#include <io.h>
#include <environment.h>
#include <mach/bbu.h>
#include <asm/armlinux.h>
#include <generated/mach-types.h>
#include <partition.h>
#include <mach/generic.h>
#include <mach/imx7-regs.h>
#include <linux/sizes.h>
#include <mfd/imx7-iomuxc-gpr.h>
#include <envfs.h>


/* static int ar8031_phy_fixup(struct phy_device *dev) */
/* { */
/* 	phy_write(dev, 0x1e, 0x21); */
/* 	phy_write(dev, 0x1f, 0x7ea8); */
/* 	phy_write(dev, 0x1e, 0x2f); */
/* 	phy_write(dev, 0x1f, 0x71b7); */

/* 	return 0; */
/* } */

	/* /\* Use 125M anatop REF_CLK1 for ENET1, clear gpr1[13], gpr1[17]*\/ */
	/* clrsetbits_le32(&iomuxc_gpr_regs->gpr[1], */
	/* 	(IOMUXC_GPR_GPR1_GPR_ENET1_TX_CLK_SEL_MASK | */
	/* 	 IOMUXC_GPR_GPR1_GPR_ENET1_CLK_DIR_MASK), 0); */

	/* return set_clk_enet(ENET_125MHz); */

static int mx7_sabresd_init_defaultenv(void)
{
	defaultenv_append_directory(env);

	return 0;
}
coredevice_initcall(mx7_sabresd_init_defaultenv);

static int mx7_sabresd_init_fec(void)
{
	void __iomem *gpr = IOMEM(MX7_IOMUXC_GPR_BASE_ADDR);
	uint32_t gpr1;

	gpr1 = readl(gpr + IOMUXC_GPR1);
	gpr1 &= ~(IMX7D_GPR1_ENET1_TX_CLK_SEL_MASK |
		  IMX7D_GPR1_ENET1_CLK_DIR_MASK);
	writel(gpr1, gpr + IOMUXC_GPR1);

	return 0;
}
coredevice_initcall(mx7_sabresd_init_fec);

static int mx7_sabresd_coredevices_init(void)
{
	if (!of_machine_is_compatible("fsl,imx7d-sdb"))
		return 0;

	/* phy_register_fixup_for_uid(PHY_ID_AR8031, AR_PHY_ID_MASK, */
	/* 			   ar8031_phy_fixup); */
	

	/* imx6_bbu_internal_mmc_register_handler("mmc", "/dev/mmc2.boot0.barebox", */
	/* 				       BBU_HANDLER_FLAG_DEFAULT); */

	return 0;
}
coredevice_initcall(mx7_sabresd_coredevices_init);
