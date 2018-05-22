#ifndef __IMX8_CLOCK__
#define __IMX8_CLOCK__

#include <asm/system.h>
#include <asm-generic/div64.h>
#include <asm/syscounter.h>
#include <mach/imx8mq-regs.h>

#if defined(__PBL__)

static inline uint64_t imx8_get_time_ns(void)
{
	void __iomem *syscnt = IOMEM(MX8MQ_SYSCNT_CTRL_BASE_ADDR);
	const uint32_t cntrfrq = syscnt_get_cntfrq(syscnt);
	uint64_t cntpct = get_cntpct();

	syscnt_enable(syscnt);

	cntpct *= 1000 * 1000 * 1000;
	do_div(cntpct, cntrfrq);

	return cntpct;
}

#define get_time_ns	imx8_get_time_ns

#endif

#include <clock.h>

#endif