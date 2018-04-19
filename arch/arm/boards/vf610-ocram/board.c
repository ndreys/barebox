/*
 * Copyright (C) 2016 Zodiac Inflight Innovation
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
#include <mmu.h>
#include <linux/sizes.h>
#include <mach/vf610-regs.h>

/*
 * Once reset detection code for Vybrids lands upstream this needs to
 * go to vf610.c and be a generic initcall. Simlar to what is being
 * done for i.MX6
 */
static int vf610_ocram_enable_l2x0(void)
{
	void __iomem *l2x0 = IOMEM(VF610_CA5_L2C_BASE_ADDR);

	if (!of_machine_is_compatible("fsl,vf610-ocram"))
		return 0;

	l2x0_init(l2x0, 0, ~0);

	return 0;
}
postmmu_initcall(vf610_ocram_enable_l2x0);

static int vf610_ocram_enable_cache(void)
{
	if (!of_machine_is_compatible("fsl,vf610-ocram"))
		return 0;

	remap_range((void *)VF610_IRAM_BASE_ADDR, SZ_1M, MAP_CACHED);
	remap_range((void *)0x3f400000, SZ_1M, MAP_CACHED);

	return 0;
}
postmmu_initcall(vf610_ocram_enable_cache);

