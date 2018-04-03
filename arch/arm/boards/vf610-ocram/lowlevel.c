#include <common.h>
#include <linux/sizes.h>
#include <mach/generic.h>
#include <asm/barebox-arm-head.h>
#include <asm/barebox-arm.h>
#include <mach/vf610-regs.h>
#include <mach/clock-vf610.h>
#include <mach/iomux-vf610.h>
#include <debug_ll.h>

static inline void setup_uart(void)
{
	void __iomem *iomux = IOMEM(VF610_IOMUXC_BASE_ADDR);

	vf610_ungate_all_peripherals();
	vf610_setup_pad(iomux, VF610_PAD_PTB10__UART0_TX);
	vf610_uart_setup_ll();

	putc_ll('>');
}

void __naked barebox_arm_reset_vector(void)
{

	vf610_cpu_lowlevel_init();

	if (IS_ENABLED(CONFIG_DEBUG_LL))
		setup_uart();

	barebox_arm_entry(VF610_IRAM_BASE_ADDR, VF610_IRAM_SIZE, NULL);
}
