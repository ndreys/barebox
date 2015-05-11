#include <debug_ll.h>
#include <mach/clock-imx51_53.h>
#include <mach/iomux-mx51.h>
#include <common.h>
#include <mach/esdctl.h>
#include <mach/generic.h>
#include <asm/cache.h>
#include <asm/barebox-arm-head.h>
#include <asm/barebox-arm.h>

#ifdef CONFIG_DEBUG_LL
static inline void setup_uart(void)
{
	void __iomem *iomuxbase = IOMEM(MX51_IOMUXC_BASE_ADDR);
	void __iomem *ccmbase = IOMEM(MX51_CCM_BASE_ADDR);

	/*
	 * Restore CCM values that might be changed by the Mask ROM
	 * code.
	 *
	 * Source: RealView debug scripts provided by Freescale
	 */
	writel(MX5_CCM_CBCDR_RESET_VALUE,  ccmbase + MX5_CCM_CBCDR);
	writel(MX5_CCM_CSCMR1_RESET_VALUE, ccmbase + MX5_CCM_CSCMR1);
	writel(MX5_CCM_CSCDR1_RESET_VALUE, ccmbase + MX5_CCM_CSCDR1);

	/*
	 * The code below should be more or less a "moral equivalent"
	 * of:
	 *	 MX51_PAD_UART1_TXD__UART1_TXD 0x1c5
	 *       MX51_PAD_UART1_TXD__UART1_RXD 0x1c5
	 * in device tree
	 */
	writel(0x00000000, iomuxbase + 0x022c);
	writel(MX51_UART_PAD_CTRL, iomuxbase + 0x061c);
	writel(0, iomuxbase + 0x0228);
	writel(0, iomuxbase + 0x09e4);
	writel(MX51_UART_PAD_CTRL, iomuxbase + 0x0618);

	imx51_uart_setup_ll();

	putc_ll('>');
}
#else
static inline void setup_uart(void)
{
}
#endif	/* CONFIG_DEBUG_LL */

#define IMX21_WDOG_WCR	0x00 /* Watchdog Control Register */
#define IMX21_WDOG_WSR	0x02 /* Watchdog Service Register */
#define IMX21_WDOG_WSTR	0x04 /* Watchdog Status Register  */
#define IMX21_WDOG_WMCR	0x08 /* Misc Register */
#define IMX21_WDOG_WCR_WDE	(1 << 2)
#define IMX21_WDOG_WCR_SRS	(1 << 4)
#define IMX21_WDOG_WCR_WDA	(1 << 5)

void __noreturn reset_cpu(unsigned long addr)
{
	void __iomem *wdogbase = IOMEM(MX51_WDOG_BASE_ADDR);

	writew(0, wdogbase + IMX21_WDOG_WCR);
	writew(IMX21_WDOG_WCR_WDE, wdogbase + IMX21_WDOG_WCR);

	/* Write Service Sequence */
	writew(0x5555, wdogbase + IMX21_WDOG_WSR);
	writew(0xaaaa, wdogbase + IMX21_WDOG_WSR);

	__hang();
}

static void __noreturn imx51_babbage_hang(void)
{
	for (;;) {
		int c;

		if (tstc_ll()) {
			c = getc_ll();
			if (c < 0)
				break;

			switch (c) {
			case 'r':
				reset_cpu(0);
				break;
			case 'p':
				putc_ll(c);
				break;
			}
		}

		putc_ll('h');
	}

	__hang();
}

extern char __dtb_imx51_babbage_start[];

ENTRY_FUNCTION(start_imx51_babbage, r0, r1, r2)
{
	void *fdt;

	imx5_cpu_lowlevel_init();

	if (IS_ENABLED(CONFIG_DEBUG_LL))
		setup_uart();

	arm_setup_stack(0x20000000 - 16);

	fdt = __dtb_imx51_babbage_start - get_runtime_offset();

	imx51_barebox_entry(fdt);
}

static noinline void babbage_entry(void)
{
	arm_early_mmu_cache_invalidate();

	relocate_to_current_adr();
	setup_c();

	puts_ll("lowlevel init done\n");

	imx51_barebox_entry(NULL);
}

ENTRY_FUNCTION(start_imx51_babbage_xload, r0, r1, r2)
{
	imx5_cpu_lowlevel_init();

	if (IS_ENABLED(CONFIG_DEBUG_LL))
		setup_uart();

	arm_setup_stack(0x20000000 - 16);
	set_hang_handler(imx51_babbage_hang);

	babbage_entry();
}
