/*
 * Copyright (C) 2017 Zodiac Inflight Innovation
 * Author: Andrey Smirnov <andrew.smirnov@gmail.com>
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
#include <gpio.h>
#include <mach/imx7-regs.h>
#include <mfd/imx7-iomuxc-gpr.h>
#include <environment.h>
#include <envfs.h>

static void zii_imx7d_rpu2_init_fec(void)
{
	void __iomem *gpr = IOMEM(MX7_IOMUXC_GPR_BASE_ADDR);
	uint32_t gpr1;

	/*
	 * Make sure we do not drive ENETn_TX_CLK signal
	 */
	gpr1 = readl(gpr + IOMUXC_GPR1);
	gpr1 &= ~(IMX7D_GPR1_ENET1_TX_CLK_SEL_MASK |
		  IMX7D_GPR1_ENET1_CLK_DIR_MASK |
		  IMX7D_GPR1_ENET2_TX_CLK_SEL_MASK |
		  IMX7D_GPR1_ENET2_CLK_DIR_MASK);
	writel(gpr1, gpr + IOMUXC_GPR1);
}

static int zii_imx7d_rpu2_coredevices_init(void)
{
	if (!of_machine_is_compatible("zii,imx7d-zii-rpu2"))
		return 0;

	zii_imx7d_rpu2_init_fec();

	return 0;
}
coredevice_initcall(zii_imx7d_rpu2_coredevices_init);
