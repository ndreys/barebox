#include <linux/bitops.h>

/*
 * FIXME: We can't use common.h because it'll bring clock.h in before
 * <mach/imx8-clock.h>, which is not what we want.
 *
 * There are plans afoot upstream to move IOMEM to <io.h>, once that
 * happens the line below can be removed.
 */
#define IOMEM(addr)	((void __force __iomem *)(addr))

#include <io.h>
#include <mach/imx8-clock.h>
#include <linux/iopoll.h>
#include <mach/imx8-ddrc.h>
#include <mach/imx8-ccm-regs.h>
#include <mach/imx8-src.h>
#include <mach/imx8-gpc.h>
#include <debug_ll.h>

void ddrc_phy_load_firmware(void __iomem *phy,
			    enum ddrc_phy_firmware_offset offset,
			    const u16 *blob, size_t size)
{
	while (size) {
		writew(*blob++, phy + DDRC_PHY_REG(offset));
		offset++;
		size -= sizeof(*blob);
	}
}

enum pmc_constants {
	PMC_MESSAGE_ID,
	PMC_MESSAGE_STREAM,

	PMC_TRAIN_SUCCESS	= 0x07,
	PMC_TRAIN_STREAM_START	= 0x08,
	PMC_TRAIN_FAIL		= 0xff,
};

static u32 ddrc_phy_get_message(void __iomem *phy, int type)
{
	u32 r, message;
	int error;

	/*
	 * When BIT0 set to 0, the PMU has a message for the user
	 * 10ms seems not enough for poll message, so use 1s here.
	 */
	error = readl_poll_timeout(phy + DDRC_PHY_REG(0xd0004),
				   r, !(r & BIT(0)), SECOND);
	BUG_ON(error);
	
	switch (type) {
	case PMC_MESSAGE_ID:
		/*
		 * Get the major message ID
		 */
		message = readl(phy + DDRC_PHY_REG(0xd0032));
		break;
	case PMC_MESSAGE_STREAM:
		message = readl(phy + DDRC_PHY_REG(0xd0034));
		message <<= 16;
		message |= readl(phy + DDRC_PHY_REG(0xd0032));
		break;
	}

	/*
	 * By setting this register to 0, the user acknowledges the
	 * receipt of the message.
	 */
	writel(0x00000000, phy + DDRC_PHY_REG(0xd0031));
	/* 
	 * When BIT0 set to 0, the PMU has a message for the user
	 */
	error = readl_poll_timeout(phy + DDRC_PHY_REG(0xd0004),
				   r, r & BIT(0), MSECOND);
	BUG_ON(error);

	writel(0x00000001, phy + DDRC_PHY_REG(0xd0031));	

	return message;
}

static void ddrc_phy_fetch_streaming_message(void __iomem *phy)
{
	const u16 index = ddrc_phy_get_message(phy, PMC_MESSAGE_STREAM);
	u16 i;

	putc_ll('|');
	puthex_ll(index);

	for (i = 0; i < index; i++) {
		const u32 arg = ddrc_phy_get_message(phy, PMC_MESSAGE_STREAM);

		putc_ll('|');
		puthex_ll(arg);
	}
}

static void ddrc_phy_wait_training_complete(void __iomem *phy)
{
	for (;;) {
		const u32 m = ddrc_phy_get_message(phy, PMC_MESSAGE_ID);

		puthex_ll(m);
		
		switch (m) {
		case PMC_TRAIN_STREAM_START:
			ddrc_phy_fetch_streaming_message(phy);
			break;
		case PMC_TRAIN_SUCCESS:
			putc_ll('P');
			putc_ll('\r');
			putc_ll('\n');
			return;
		case PMC_TRAIN_FAIL:
			putc_ll('F');
			hang();
		}

		putc_ll('\r');
		putc_ll('\n');
	}
}

void ddrc_phy_execute_training_firmware(void __iomem *phy)
{
	writel(0x00000001, phy + DDRC_PHY_REG(0xd0000));
	writel(0x00000001, phy + DDRC_PHY_REG(0xd0000));
	writel(0x00000009, phy + DDRC_PHY_REG(0xd0099));
	writel(0x00000001, phy + DDRC_PHY_REG(0xd0099));
	writel(0x00000000, phy + DDRC_PHY_REG(0xd0099));

	ddrc_phy_wait_training_complete(phy);
}

void ddrc_phy_configure_clock_for_pstate(enum ddrc_phy_pstate state)
{
	void __iomem *ccm = IOMEM(MX8MQ_CCM_BASE_ADDR);
	void __iomem *gpc = IOMEM(MX8MQ_GPC_BASE_ADDR);

	switch (state) {
	case DDRC_PHY_PSTATE_PS2:
		/*
		 * change the clock source of dram_alt_clk_root to
		 * source 2: 100MHZ
		 */
		writel(CCM_TARGET_ROOTn_ENABLE |
		       DRAM_ALT_CLK_ROOT__SYSTEM_PLL1_DIV8 |
		       CCM_TARGET_ROOTn_PRE_DIV(1),
		       ccm + CCM_TARGET_ROOTn(DRAM_ALT_CLK_ROOT));

		/*
		 * change the clock source of dram_apb_clk_root to
		 * source 2: 40MHZ
		 */
		writel(CCM_TARGET_ROOTn_ENABLE |
		       DRAM_APB_CLK_ROOT__SYSTEM_PLL1_DIV20 |
		       CCM_TARGET_ROOTn_PRE_DIV(2),
		       ccm + CCM_TARGET_ROOTn(DRAM_APB_CLK_ROOT));

		setbits_le32(gpc + GPC_PGC_CPU_0_1_MAPPING, DDR1_A53_DOMAIN);
		setbits_le32(gpc + GPC_PU_PGC_SW_PUP_REQ, DDR1_SW_PUP_REQ);
		break;
	case DDRC_PHY_PSTATE_PS1:
		/*
		 * change the clock source of dram_alt_clk_root to
		 * source 2: 400MHZ
		 */
		writel(CCM_TARGET_ROOTn_ENABLE |
		       DRAM_ALT_CLK_ROOT__SYSTEM_PLL1_DIV2 |
		       CCM_TARGET_ROOTn_PRE_DIV(1),
		       ccm + CCM_TARGET_ROOTn(DRAM_ALT_CLK_ROOT));
		/*
		 * change the clock source of dram_apb_clk_root to
		 * source 2: 40MHZ
		 */
		writel(CCM_TARGET_ROOTn_ENABLE |
		       DRAM_APB_CLK_ROOT__SYSTEM_PLL1_DIV20 |
		       CCM_TARGET_ROOTn_PRE_DIV(2),
		       ccm + CCM_TARGET_ROOTn(DRAM_APB_CLK_ROOT));
		/*
		 * FIXME: Are those two statements needed?
		 */
		setbits_le32(gpc + GPC_PGC_CPU_0_1_MAPPING, DDR1_A53_DOMAIN);
		setbits_le32(gpc + GPC_PU_PGC_SW_PUP_REQ, DDR1_SW_PUP_REQ);
		break;
	default:
		writel(CCM_TARGET_ROOTn_ENABLE |
		       DRAM_APB_CLK_ROOT__SYSTEM_PLL1_CLK |
		       CCM_TARGET_ROOTn_PRE_DIV(4),
		       ccm + CCM_TARGET_ROOTn(DRAM_APB_CLK_ROOT));
	}

	/*
	 * FIXME: There no documentation for that clock root so it is
	 * unclear what this line accomplishes
	 */
	writel(BIT(24), ccm + CCM_TARGET_ROOTn_SET(DRAM_SEL_CLK_ROOT));
}

static void lpddr4_magic_config_1(void __iomem *phy,
				  u32 reg_54002,
				  u32 reg_54003,
				  u32 reg_54008,
				  u32 reg_54009,
				  u32 reg_54019,
				  u32 reg_5401f,
				  u32 reg_54032,
				  u32 reg_54033,
				  u32 reg_54038,
				  u32 reg_54039)
{
	writel(0x00000000, phy + DDRC_PHY_REG(0x54000));
	writel(0x00000000, phy + DDRC_PHY_REG(0x54001));
	writel(reg_54002,  phy + DDRC_PHY_REG(0x54002));
	writel(reg_54003,  phy + DDRC_PHY_REG(0x54003));
	writel(0x00000002, phy + DDRC_PHY_REG(0x54004));
	writel(0x00002828, phy + DDRC_PHY_REG(0x54005));
	writel(0x00000014, phy + DDRC_PHY_REG(0x54006));
	writel(0x00000000, phy + DDRC_PHY_REG(0x54007));
	writel(reg_54008,  phy + DDRC_PHY_REG(0x54008));
	writel(reg_54009,  phy + DDRC_PHY_REG(0x54009));
	writel(0x00000000, phy + DDRC_PHY_REG(0x5400a));
	writel(0x00000002, phy + DDRC_PHY_REG(0x5400b));
	writel(0x00000000, phy + DDRC_PHY_REG(0x5400c));
	writel(0x00000000, phy + DDRC_PHY_REG(0x5400d));
	writel(0x00000000, phy + DDRC_PHY_REG(0x5400e));
	writel(0x00000000, phy + DDRC_PHY_REG(0x5400f));
	writel(0x00000000, phy + DDRC_PHY_REG(0x54010));
	writel(0x00000000, phy + DDRC_PHY_REG(0x54011));
	writel(0x00000310, phy + DDRC_PHY_REG(0x54012));
	writel(0x00000000, phy + DDRC_PHY_REG(0x54013));
	writel(0x00000000, phy + DDRC_PHY_REG(0x54014));
	writel(0x00000000, phy + DDRC_PHY_REG(0x54015));
	writel(0x00000000, phy + DDRC_PHY_REG(0x54016));
	writel(0x00000000, phy + DDRC_PHY_REG(0x54017));
	writel(0x00000000, phy + DDRC_PHY_REG(0x54018));
	writel(reg_54019,  phy + DDRC_PHY_REG(0x54019));
	writel(0x00000031, phy + DDRC_PHY_REG(0x5401a));
	writel(0x00004d46, phy + DDRC_PHY_REG(0x5401b));
	writel(0x00004d08, phy + DDRC_PHY_REG(0x5401c));
	writel(0x00000000, phy + DDRC_PHY_REG(0x5401d));
	writel(0x00000005, phy + DDRC_PHY_REG(0x5401e));
	writel(reg_5401f,  phy + DDRC_PHY_REG(0x5401f));
	writel(0x00000031, phy + DDRC_PHY_REG(0x54020));
	writel(0x00004d46, phy + DDRC_PHY_REG(0x54021));
	writel(0x00004d08, phy + DDRC_PHY_REG(0x54022));
	writel(0x00000000, phy + DDRC_PHY_REG(0x54023));
	writel(0x00000005, phy + DDRC_PHY_REG(0x54024));
	writel(0x00000000, phy + DDRC_PHY_REG(0x54025));
	writel(0x00000000, phy + DDRC_PHY_REG(0x54026));
	writel(0x00000000, phy + DDRC_PHY_REG(0x54027));
	writel(0x00000000, phy + DDRC_PHY_REG(0x54028));
	writel(0x00000000, phy + DDRC_PHY_REG(0x54029));
	writel(0x00000000, phy + DDRC_PHY_REG(0x5402a));
	writel(0x00001000, phy + DDRC_PHY_REG(0x5402b));
	writel(0x00000003, phy + DDRC_PHY_REG(0x5402c));
	writel(0x00000000, phy + DDRC_PHY_REG(0x5402d));
	writel(0x00000000, phy + DDRC_PHY_REG(0x5402e));
	writel(0x00000000, phy + DDRC_PHY_REG(0x5402f));
	writel(0x00000000, phy + DDRC_PHY_REG(0x54030));
	writel(0x00000000, phy + DDRC_PHY_REG(0x54031));
	writel(reg_54032,  phy + DDRC_PHY_REG(0x54032));
	writel(reg_54033,  phy + DDRC_PHY_REG(0x54033));
	writel(0x00004600, phy + DDRC_PHY_REG(0x54034));
	writel(0x0000084d, phy + DDRC_PHY_REG(0x54035));
	writel(0x0000004d, phy + DDRC_PHY_REG(0x54036));
	writel(0x00000500, phy + DDRC_PHY_REG(0x54037));
	writel(reg_54038,  phy + DDRC_PHY_REG(0x54038));
	writel(reg_54039,  phy + DDRC_PHY_REG(0x54039));
	writel(0x00004600, phy + DDRC_PHY_REG(0x5403a));
	writel(0x0000084d, phy + DDRC_PHY_REG(0x5403b));
	writel(0x0000004d, phy + DDRC_PHY_REG(0x5403c));
	writel(0x00000500, phy + DDRC_PHY_REG(0x5403d));
}


static void lpddr4_800M_cfg_phy(void __iomem *phy,
				const u16 *imem_1d, size_t imem_1d_size,
				const u16 *dmem_1d, size_t dmem_1d_size,
				const u16 *imem_2d, size_t imem_2d_size,
				const u16 *dmem_2d, size_t dmem_2d_size)
{
	int error;
	u32 r;

	writel(0x00000002, phy + DDRC_PHY_REG(0x20110));
	writel(0x00000003, phy + DDRC_PHY_REG(0x20111));
	writel(0x00000004, phy + DDRC_PHY_REG(0x20112));
	writel(0x00000005, phy + DDRC_PHY_REG(0x20113));
	writel(0x00000000, phy + DDRC_PHY_REG(0x20114));
	writel(0x00000001, phy + DDRC_PHY_REG(0x20115));
	writel(0x000001ff, phy + DDRC_PHY_REG(0x1005f));
	writel(0x000001ff, phy + DDRC_PHY_REG(0x1015f));
	writel(0x000001ff, phy + DDRC_PHY_REG(0x1105f));
	writel(0x000001ff, phy + DDRC_PHY_REG(0x1115f));
	writel(0x000001ff, phy + DDRC_PHY_REG(0x1205f));
	writel(0x000001ff, phy + DDRC_PHY_REG(0x1215f));
	writel(0x000001ff, phy + DDRC_PHY_REG(0x1305f));
	writel(0x000001ff, phy + DDRC_PHY_REG(0x1315f));
	writel(0x000001ff, phy + DDRC_PHY_REG(0x11005f));
	writel(0x000001ff, phy + DDRC_PHY_REG(0x11015f));
	writel(0x000001ff, phy + DDRC_PHY_REG(0x11105f));
	writel(0x000001ff, phy + DDRC_PHY_REG(0x11115f));
	writel(0x000001ff, phy + DDRC_PHY_REG(0x11205f));
	writel(0x000001ff, phy + DDRC_PHY_REG(0x11215f));
	writel(0x000001ff, phy + DDRC_PHY_REG(0x11305f));
	writel(0x000001ff, phy + DDRC_PHY_REG(0x11315f));
	writel(0x000001ff, phy + DDRC_PHY_REG(0x21005f));
	writel(0x000001ff, phy + DDRC_PHY_REG(0x21015f));
	writel(0x000001ff, phy + DDRC_PHY_REG(0x21105f));
	writel(0x000001ff, phy + DDRC_PHY_REG(0x21115f));
	writel(0x000001ff, phy + DDRC_PHY_REG(0x21205f));
	writel(0x000001ff, phy + DDRC_PHY_REG(0x21215f));
	writel(0x000001ff, phy + DDRC_PHY_REG(0x21305f));
	writel(0x000001ff, phy + DDRC_PHY_REG(0x21315f));
	writel(0x000001ff, phy + DDRC_PHY_REG(0x55));
	writel(0x000001ff, phy + DDRC_PHY_REG(0x1055));
	writel(0x000001ff, phy + DDRC_PHY_REG(0x2055));
	writel(0x000001ff, phy + DDRC_PHY_REG(0x3055));
	writel(0x000001ff, phy + DDRC_PHY_REG(0x4055));
	writel(0x000001ff, phy + DDRC_PHY_REG(0x5055));
	writel(0x000001ff, phy + DDRC_PHY_REG(0x6055));
	writel(0x000001ff, phy + DDRC_PHY_REG(0x7055));
	writel(0x000001ff, phy + DDRC_PHY_REG(0x8055));
	writel(0x000001ff, phy + DDRC_PHY_REG(0x9055));
	writel(0x00000019, phy + DDRC_PHY_REG(0x200c5));
	writel(0x00000007, phy + DDRC_PHY_REG(0x1200c5));
	writel(0x00000007, phy + DDRC_PHY_REG(0x2200c5));
	writel(0x00000002, phy + DDRC_PHY_REG(0x2002e));
	writel(0x00000002, phy + DDRC_PHY_REG(0x12002e));
	writel(0x00000002, phy + DDRC_PHY_REG(0x22002e));
	writel(0x00000000, phy + DDRC_PHY_REG(0x90204));
	writel(0x00000000, phy + DDRC_PHY_REG(0x190204));
	writel(0x00000000, phy + DDRC_PHY_REG(0x290204));
	writel(0x000000ab, phy + DDRC_PHY_REG(0x20024));
	writel(0x00000000, phy + DDRC_PHY_REG(0x2003a));
	writel(0x000000ab, phy + DDRC_PHY_REG(0x120024));
	writel(0x00000000, phy + DDRC_PHY_REG(0x2003a));
	writel(0x000000ab, phy + DDRC_PHY_REG(0x220024));
	writel(0x00000000, phy + DDRC_PHY_REG(0x2003a));
	writel(0x00000003, phy + DDRC_PHY_REG(0x20056));
	writel(0x0000000a, phy + DDRC_PHY_REG(0x120056));
	writel(0x0000000a, phy + DDRC_PHY_REG(0x220056));
	writel(0x00000e00, phy + DDRC_PHY_REG(0x1004d));
	writel(0x00000e00, phy + DDRC_PHY_REG(0x1014d));
	writel(0x00000e00, phy + DDRC_PHY_REG(0x1104d));
	writel(0x00000e00, phy + DDRC_PHY_REG(0x1114d));
	writel(0x00000e00, phy + DDRC_PHY_REG(0x1204d));
	writel(0x00000e00, phy + DDRC_PHY_REG(0x1214d));
	writel(0x00000e00, phy + DDRC_PHY_REG(0x1304d));
	writel(0x00000e00, phy + DDRC_PHY_REG(0x1314d));
	writel(0x00000e00, phy + DDRC_PHY_REG(0x11004d));
	writel(0x00000e00, phy + DDRC_PHY_REG(0x11014d));
	writel(0x00000e00, phy + DDRC_PHY_REG(0x11104d));
	writel(0x00000e00, phy + DDRC_PHY_REG(0x11114d));
	writel(0x00000e00, phy + DDRC_PHY_REG(0x11204d));
	writel(0x00000e00, phy + DDRC_PHY_REG(0x11214d));
	writel(0x00000e00, phy + DDRC_PHY_REG(0x11304d));
	writel(0x00000e00, phy + DDRC_PHY_REG(0x11314d));
	writel(0x00000e00, phy + DDRC_PHY_REG(0x21004d));
	writel(0x00000e00, phy + DDRC_PHY_REG(0x21014d));
	writel(0x00000e00, phy + DDRC_PHY_REG(0x21104d));
	writel(0x00000e00, phy + DDRC_PHY_REG(0x21114d));
	writel(0x00000e00, phy + DDRC_PHY_REG(0x21204d));
	writel(0x00000e00, phy + DDRC_PHY_REG(0x21214d));
	writel(0x00000e00, phy + DDRC_PHY_REG(0x21304d));
	writel(0x00000e00, phy + DDRC_PHY_REG(0x21314d));
	writel(0x00000e38, phy + DDRC_PHY_REG(0x10049));
	writel(0x00000e38, phy + DDRC_PHY_REG(0x10149));
	writel(0x00000e38, phy + DDRC_PHY_REG(0x11049));
	writel(0x00000e38, phy + DDRC_PHY_REG(0x11149));
	writel(0x00000e38, phy + DDRC_PHY_REG(0x12049));
	writel(0x00000e38, phy + DDRC_PHY_REG(0x12149));
	writel(0x00000e38, phy + DDRC_PHY_REG(0x13049));
	writel(0x00000e38, phy + DDRC_PHY_REG(0x13149));
	writel(0x00000e38, phy + DDRC_PHY_REG(0x110049));
	writel(0x00000e38, phy + DDRC_PHY_REG(0x110149));
	writel(0x00000e38, phy + DDRC_PHY_REG(0x111049));
	writel(0x00000e38, phy + DDRC_PHY_REG(0x111149));
	writel(0x00000e38, phy + DDRC_PHY_REG(0x112049));
	writel(0x00000e38, phy + DDRC_PHY_REG(0x112149));
	writel(0x00000e38, phy + DDRC_PHY_REG(0x113049));
	writel(0x00000e38, phy + DDRC_PHY_REG(0x113149));
	writel(0x00000e38, phy + DDRC_PHY_REG(0x210049));
	writel(0x00000e38, phy + DDRC_PHY_REG(0x210149));
	writel(0x00000e38, phy + DDRC_PHY_REG(0x211049));
	writel(0x00000e38, phy + DDRC_PHY_REG(0x211149));
	writel(0x00000e38, phy + DDRC_PHY_REG(0x212049));
	writel(0x00000e38, phy + DDRC_PHY_REG(0x212149));
	writel(0x00000e38, phy + DDRC_PHY_REG(0x213049));
	writel(0x00000e38, phy + DDRC_PHY_REG(0x213149));
	writel(0x00000021, phy + DDRC_PHY_REG(0x43));
	writel(0x00000021, phy + DDRC_PHY_REG(0x1043));
	writel(0x00000021, phy + DDRC_PHY_REG(0x2043));
	writel(0x00000021, phy + DDRC_PHY_REG(0x3043));
	writel(0x00000021, phy + DDRC_PHY_REG(0x4043));
	writel(0x00000021, phy + DDRC_PHY_REG(0x5043));
	writel(0x00000021, phy + DDRC_PHY_REG(0x6043));
	writel(0x00000021, phy + DDRC_PHY_REG(0x7043));
	writel(0x00000021, phy + DDRC_PHY_REG(0x8043));
	writel(0x00000021, phy + DDRC_PHY_REG(0x9043));
	writel(0x00000003, phy + DDRC_PHY_REG(0x20018));
	writel(0x00000004, phy + DDRC_PHY_REG(0x20075));
	writel(0x00000000, phy + DDRC_PHY_REG(0x20050));
	writel(0x00000320, phy + DDRC_PHY_REG(0x20008));
	writel(0x00000064, phy + DDRC_PHY_REG(0x120008));
	writel(0x00000019, phy + DDRC_PHY_REG(0x220008));
	writel(0x00000009, phy + DDRC_PHY_REG(0x20088));
	writel(0x0000019c, phy + DDRC_PHY_REG(0x200b2));
	writel(0x000005a1, phy + DDRC_PHY_REG(0x10043));
	writel(0x000005a1, phy + DDRC_PHY_REG(0x10143));
	writel(0x000005a1, phy + DDRC_PHY_REG(0x11043));
	writel(0x000005a1, phy + DDRC_PHY_REG(0x11143));
	writel(0x000005a1, phy + DDRC_PHY_REG(0x12043));
	writel(0x000005a1, phy + DDRC_PHY_REG(0x12143));
	writel(0x000005a1, phy + DDRC_PHY_REG(0x13043));
	writel(0x000005a1, phy + DDRC_PHY_REG(0x13143));
	writel(0x0000019c, phy + DDRC_PHY_REG(0x1200b2));
	writel(0x000005a1, phy + DDRC_PHY_REG(0x110043));
	writel(0x000005a1, phy + DDRC_PHY_REG(0x110143));
	writel(0x000005a1, phy + DDRC_PHY_REG(0x111043));
	writel(0x000005a1, phy + DDRC_PHY_REG(0x111143));
	writel(0x000005a1, phy + DDRC_PHY_REG(0x112043));
	writel(0x000005a1, phy + DDRC_PHY_REG(0x112143));
	writel(0x000005a1, phy + DDRC_PHY_REG(0x113043));
	writel(0x000005a1, phy + DDRC_PHY_REG(0x113143));
	writel(0x0000019c, phy + DDRC_PHY_REG(0x2200b2));
	writel(0x000005a1, phy + DDRC_PHY_REG(0x210043));
	writel(0x000005a1, phy + DDRC_PHY_REG(0x210143));
	writel(0x000005a1, phy + DDRC_PHY_REG(0x211043));
	writel(0x000005a1, phy + DDRC_PHY_REG(0x211143));
	writel(0x000005a1, phy + DDRC_PHY_REG(0x212043));
	writel(0x000005a1, phy + DDRC_PHY_REG(0x212143));
	writel(0x000005a1, phy + DDRC_PHY_REG(0x213043));
	writel(0x000005a1, phy + DDRC_PHY_REG(0x213143));
	writel(0x00000001, phy + DDRC_PHY_REG(0x200fa));
	writel(0x00000001, phy + DDRC_PHY_REG(0x1200fa));
	writel(0x00000001, phy + DDRC_PHY_REG(0x2200fa));
	writel(0x00000001, phy + DDRC_PHY_REG(0x20019));
	writel(0x00000001, phy + DDRC_PHY_REG(0x120019));
	writel(0x00000001, phy + DDRC_PHY_REG(0x220019));
	writel(0x00000660, phy + DDRC_PHY_REG(0x200f0));
	writel(0x00000000, phy + DDRC_PHY_REG(0x200f1));
	writel(0x00004444, phy + DDRC_PHY_REG(0x200f2));
	writel(0x00008888, phy + DDRC_PHY_REG(0x200f3));
	writel(0x00005555, phy + DDRC_PHY_REG(0x200f4));
	writel(0x00000000, phy + DDRC_PHY_REG(0x200f5));
	writel(0x00000000, phy + DDRC_PHY_REG(0x200f6));
	writel(0x0000f000, phy + DDRC_PHY_REG(0x200f7));
	writel(0x00000065, phy + DDRC_PHY_REG(0x2000b));
	writel(0x000000c9, phy + DDRC_PHY_REG(0x2000c));
	writel(0x000007d1, phy + DDRC_PHY_REG(0x2000d));
	writel(0x0000002c, phy + DDRC_PHY_REG(0x2000e));
	writel(0x0000000d, phy + DDRC_PHY_REG(0x12000b));
	writel(0x0000001a, phy + DDRC_PHY_REG(0x12000c));
	writel(0x000000fb, phy + DDRC_PHY_REG(0x12000d));
	writel(0x00000010, phy + DDRC_PHY_REG(0x12000e));
	writel(0x00000004, phy + DDRC_PHY_REG(0x22000b));
	writel(0x00000007, phy + DDRC_PHY_REG(0x22000c));
	writel(0x0000003f, phy + DDRC_PHY_REG(0x22000d));
	writel(0x00000010, phy + DDRC_PHY_REG(0x22000e));
	writel(0x00000000, phy + DDRC_PHY_REG(0x20025));
	writel(0x00000000, phy + DDRC_PHY_REG(0x2002d));
	writel(0x00000000, phy + DDRC_PHY_REG(0x12002d));
	writel(0x00000000, phy + DDRC_PHY_REG(0x22002d));
	writel(0x00000002, phy + DDRC_PHY_REG(0x20060));
	writel(0x00000000, phy + DDRC_PHY_REG(0xd0000));

	/* load the 1D training image */
	ddrc_phy_load_firmware(phy, DDRC_PHY_IMEM, imem_1d, imem_1d_size);
	ddrc_phy_load_firmware(phy, DDRC_PHY_DMEM, dmem_1d, dmem_1d_size);

	writel(0x0, phy + DDRC_PHY_REG(0xd0000));
	writel(0x1, phy + DDRC_PHY_REG(0xd0000));

	ddrc_phy_configure_clock_for_pstate(DDRC_PHY_PSTATE_PS2);

	writel(0x0, phy + DDRC_PHY_REG(0xd0000));

	lpddr4_magic_config_1(phy,
			      /* 0x54002: */ 0x00000102,
			      /* 0x54003: */ 0x00000064,
			      /* 0x54008: */ 0x0000121f,
			      /* 0x54009: */ 0x000000c8,
			      /* 0x54019: */ 0x00000004,
			      /* 0x5401f: */ 0x00000004,
			      /* 0x54032: */ 0x00000400,
			      /* 0x54033: */ 0x00003100,
			      /* 0x54038: */ 0x00000400,
			      /* 0x54039: */ 0x00003100);

	ddrc_phy_execute_training_firmware(phy);

	writel(0x1, phy + DDRC_PHY_REG(0xd0099));
	writel(0x0, phy + DDRC_PHY_REG(0xd0000));
	writel(0x1, phy + DDRC_PHY_REG(0xd0000));

	ddrc_phy_configure_clock_for_pstate(DDRC_PHY_PSTATE_PS1);

	ddrc_phy_execute_training_firmware(phy);

	writel(0x1, phy + DDRC_PHY_REG(0xd0099));
	writel(0x0, phy + DDRC_PHY_REG(0xd0000));
	writel(0x1, phy + DDRC_PHY_REG(0xd0000));

	ddrc_phy_configure_clock_for_pstate(DDRC_PHY_PSTATE_PS0);

	writel(0x0, phy + DDRC_PHY_REG(0xd0000));

	lpddr4_magic_config_1(phy,
			      /* 0x54002: */ 0x00000101,
			      /* 0x54003: */ 0x00000190,
			      /* 0x54008: */ 0x0000121f,
			      /* 0x54009: */ 0x000000c8,
			      /* 0x54019: */ 0x00000004,
			      /* 0x5401f: */ 0x00000004,
			      /* 0x54032: */ 0x00000400,
			      /* 0x54033: */ 0x00003100,
			      /* 0x54038: */ 0x00000400,
			      /* 0x54039: */ 0x00003100);
	ddrc_phy_execute_training_firmware(phy);

	writel(0x1, phy + DDRC_PHY_REG(0xd0099));
	writel(0x0, phy + DDRC_PHY_REG(0xd0000));
	writel(0x1, phy + DDRC_PHY_REG(0xd0000));
	writel(0x0, phy + DDRC_PHY_REG(0xd0000));

	/* load the 2D training image */
	ddrc_phy_load_firmware(phy, DDRC_PHY_IMEM, imem_2d, imem_2d_size);
	ddrc_phy_load_firmware(phy, DDRC_PHY_DMEM, dmem_2d, dmem_2d_size);

	writel(0x0, phy + DDRC_PHY_REG(0xd0000));
	writel(0x1, phy + DDRC_PHY_REG(0xd0000));
	writel(0x0, phy + DDRC_PHY_REG(0xd0000));
	writel(0x0, phy + DDRC_PHY_REG(0xd0000));

	lpddr4_magic_config_1(phy,
			      /* 0x54002: */ 0x00000000,
			      /* 0x54003: */ 0x00000c80,
			      /* 0x54008: */ 0x0000131f,
			      /* 0x54009: */ 0x00000005,
			      /* 0x54019: */ 0x00002dd4,
			      /* 0x5401f: */ 0x00002dd4,
			      /* 0x54032: */ 0x0000d400,
			      /* 0x54033: */ 0x0000312d,
			      /* 0x54038: */ 0x0000d400,
			      /* 0x54039: */ 0x0000312d);
	ddrc_phy_execute_training_firmware(phy);

	writel(0x00000001, phy + DDRC_PHY_REG(0xd0099));
	writel(0x00000000, phy + DDRC_PHY_REG(0xd0000));
	writel(0x00000001, phy + DDRC_PHY_REG(0xd0000));

	/* (I) Load PHY Init Engine Image */
	writel(0x00000000, phy + DDRC_PHY_REG(0xd0000));
	writel(0x00000010, phy + DDRC_PHY_REG(0x90000));
	writel(0x00000400, phy + DDRC_PHY_REG(0x90001));
	writel(0x0000010e, phy + DDRC_PHY_REG(0x90002));
	writel(0x00000000, phy + DDRC_PHY_REG(0x90003));
	writel(0x00000000, phy + DDRC_PHY_REG(0x90004));
	writel(0x00000008, phy + DDRC_PHY_REG(0x90005));
	writel(0x0000000b, phy + DDRC_PHY_REG(0x90029));
	writel(0x00000480, phy + DDRC_PHY_REG(0x9002a));
	writel(0x00000109, phy + DDRC_PHY_REG(0x9002b));
	writel(0x00000008, phy + DDRC_PHY_REG(0x9002c));
	writel(0x00000448, phy + DDRC_PHY_REG(0x9002d));
	writel(0x00000139, phy + DDRC_PHY_REG(0x9002e));
	writel(0x00000008, phy + DDRC_PHY_REG(0x9002f));
	writel(0x00000478, phy + DDRC_PHY_REG(0x90030));
	writel(0x00000109, phy + DDRC_PHY_REG(0x90031));
	writel(0x00000000, phy + DDRC_PHY_REG(0x90032));
	writel(0x000000e8, phy + DDRC_PHY_REG(0x90033));
	writel(0x00000109, phy + DDRC_PHY_REG(0x90034));
	writel(0x00000002, phy + DDRC_PHY_REG(0x90035));
	writel(0x00000010, phy + DDRC_PHY_REG(0x90036));
	writel(0x00000139, phy + DDRC_PHY_REG(0x90037));
	writel(0x0000000f, phy + DDRC_PHY_REG(0x90038));
	writel(0x000007c0, phy + DDRC_PHY_REG(0x90039));
	writel(0x00000139, phy + DDRC_PHY_REG(0x9003a));
	writel(0x00000044, phy + DDRC_PHY_REG(0x9003b));
	writel(0x00000630, phy + DDRC_PHY_REG(0x9003c));
	writel(0x00000159, phy + DDRC_PHY_REG(0x9003d));
	writel(0x0000014f, phy + DDRC_PHY_REG(0x9003e));
	writel(0x00000630, phy + DDRC_PHY_REG(0x9003f));
	writel(0x00000159, phy + DDRC_PHY_REG(0x90040));
	writel(0x00000047, phy + DDRC_PHY_REG(0x90041));
	writel(0x00000630, phy + DDRC_PHY_REG(0x90042));
	writel(0x00000149, phy + DDRC_PHY_REG(0x90043));
	writel(0x0000004f, phy + DDRC_PHY_REG(0x90044));
	writel(0x00000630, phy + DDRC_PHY_REG(0x90045));
	writel(0x00000179, phy + DDRC_PHY_REG(0x90046));
	writel(0x00000008, phy + DDRC_PHY_REG(0x90047));
	writel(0x000000e0, phy + DDRC_PHY_REG(0x90048));
	writel(0x00000109, phy + DDRC_PHY_REG(0x90049));
	writel(0x00000000, phy + DDRC_PHY_REG(0x9004a));
	writel(0x000007c8, phy + DDRC_PHY_REG(0x9004b));
	writel(0x00000109, phy + DDRC_PHY_REG(0x9004c));
	writel(0x00000000, phy + DDRC_PHY_REG(0x9004d));
	writel(0x00000001, phy + DDRC_PHY_REG(0x9004e));
	writel(0x00000008, phy + DDRC_PHY_REG(0x9004f));
	writel(0x00000000, phy + DDRC_PHY_REG(0x90050));
	writel(0x0000045a, phy + DDRC_PHY_REG(0x90051));
	writel(0x00000009, phy + DDRC_PHY_REG(0x90052));
	writel(0x00000000, phy + DDRC_PHY_REG(0x90053));
	writel(0x00000448, phy + DDRC_PHY_REG(0x90054));
	writel(0x00000109, phy + DDRC_PHY_REG(0x90055));
	writel(0x00000040, phy + DDRC_PHY_REG(0x90056));
	writel(0x00000630, phy + DDRC_PHY_REG(0x90057));
	writel(0x00000179, phy + DDRC_PHY_REG(0x90058));
	writel(0x00000001, phy + DDRC_PHY_REG(0x90059));
	writel(0x00000618, phy + DDRC_PHY_REG(0x9005a));
	writel(0x00000109, phy + DDRC_PHY_REG(0x9005b));
	writel(0x000040c0, phy + DDRC_PHY_REG(0x9005c));
	writel(0x00000630, phy + DDRC_PHY_REG(0x9005d));
	writel(0x00000149, phy + DDRC_PHY_REG(0x9005e));
	writel(0x00000008, phy + DDRC_PHY_REG(0x9005f));
	writel(0x00000004, phy + DDRC_PHY_REG(0x90060));
	writel(0x00000048, phy + DDRC_PHY_REG(0x90061));
	writel(0x00004040, phy + DDRC_PHY_REG(0x90062));
	writel(0x00000630, phy + DDRC_PHY_REG(0x90063));
	writel(0x00000149, phy + DDRC_PHY_REG(0x90064));
	writel(0x00000000, phy + DDRC_PHY_REG(0x90065));
	writel(0x00000004, phy + DDRC_PHY_REG(0x90066));
	writel(0x00000048, phy + DDRC_PHY_REG(0x90067));
	writel(0x00000040, phy + DDRC_PHY_REG(0x90068));
	writel(0x00000630, phy + DDRC_PHY_REG(0x90069));
	writel(0x00000149, phy + DDRC_PHY_REG(0x9006a));
	writel(0x00000010, phy + DDRC_PHY_REG(0x9006b));
	writel(0x00000004, phy + DDRC_PHY_REG(0x9006c));
	writel(0x00000018, phy + DDRC_PHY_REG(0x9006d));
	writel(0x00000000, phy + DDRC_PHY_REG(0x9006e));
	writel(0x00000004, phy + DDRC_PHY_REG(0x9006f));
	writel(0x00000078, phy + DDRC_PHY_REG(0x90070));
	writel(0x00000549, phy + DDRC_PHY_REG(0x90071));
	writel(0x00000630, phy + DDRC_PHY_REG(0x90072));
	writel(0x00000159, phy + DDRC_PHY_REG(0x90073));
	writel(0x00000d49, phy + DDRC_PHY_REG(0x90074));
	writel(0x00000630, phy + DDRC_PHY_REG(0x90075));
	writel(0x00000159, phy + DDRC_PHY_REG(0x90076));
	writel(0x0000094a, phy + DDRC_PHY_REG(0x90077));
	writel(0x00000630, phy + DDRC_PHY_REG(0x90078));
	writel(0x00000159, phy + DDRC_PHY_REG(0x90079));
	writel(0x00000441, phy + DDRC_PHY_REG(0x9007a));
	writel(0x00000630, phy + DDRC_PHY_REG(0x9007b));
	writel(0x00000149, phy + DDRC_PHY_REG(0x9007c));
	writel(0x00000042, phy + DDRC_PHY_REG(0x9007d));
	writel(0x00000630, phy + DDRC_PHY_REG(0x9007e));
	writel(0x00000149, phy + DDRC_PHY_REG(0x9007f));
	writel(0x00000001, phy + DDRC_PHY_REG(0x90080));
	writel(0x00000630, phy + DDRC_PHY_REG(0x90081));
	writel(0x00000149, phy + DDRC_PHY_REG(0x90082));
	writel(0x00000000, phy + DDRC_PHY_REG(0x90083));
	writel(0x000000e0, phy + DDRC_PHY_REG(0x90084));
	writel(0x00000109, phy + DDRC_PHY_REG(0x90085));
	writel(0x0000000a, phy + DDRC_PHY_REG(0x90086));
	writel(0x00000010, phy + DDRC_PHY_REG(0x90087));
	writel(0x00000109, phy + DDRC_PHY_REG(0x90088));
	writel(0x00000009, phy + DDRC_PHY_REG(0x90089));
	writel(0x000003c0, phy + DDRC_PHY_REG(0x9008a));
	writel(0x00000149, phy + DDRC_PHY_REG(0x9008b));
	writel(0x00000009, phy + DDRC_PHY_REG(0x9008c));
	writel(0x000003c0, phy + DDRC_PHY_REG(0x9008d));
	writel(0x00000159, phy + DDRC_PHY_REG(0x9008e));
	writel(0x00000018, phy + DDRC_PHY_REG(0x9008f));
	writel(0x00000010, phy + DDRC_PHY_REG(0x90090));
	writel(0x00000109, phy + DDRC_PHY_REG(0x90091));
	writel(0x00000000, phy + DDRC_PHY_REG(0x90092));
	writel(0x000003c0, phy + DDRC_PHY_REG(0x90093));
	writel(0x00000109, phy + DDRC_PHY_REG(0x90094));
	writel(0x00000018, phy + DDRC_PHY_REG(0x90095));
	writel(0x00000004, phy + DDRC_PHY_REG(0x90096));
	writel(0x00000048, phy + DDRC_PHY_REG(0x90097));
	writel(0x00000018, phy + DDRC_PHY_REG(0x90098));
	writel(0x00000004, phy + DDRC_PHY_REG(0x90099));
	writel(0x00000058, phy + DDRC_PHY_REG(0x9009a));
	writel(0x0000000a, phy + DDRC_PHY_REG(0x9009b));
	writel(0x00000010, phy + DDRC_PHY_REG(0x9009c));
	writel(0x00000109, phy + DDRC_PHY_REG(0x9009d));
	writel(0x00000002, phy + DDRC_PHY_REG(0x9009e));
	writel(0x00000010, phy + DDRC_PHY_REG(0x9009f));
	writel(0x00000109, phy + DDRC_PHY_REG(0x900a0));
	writel(0x00000005, phy + DDRC_PHY_REG(0x900a1));
	writel(0x000007c0, phy + DDRC_PHY_REG(0x900a2));
	writel(0x00000109, phy + DDRC_PHY_REG(0x900a3));
	writel(0x00000010, phy + DDRC_PHY_REG(0x900a4));
	writel(0x00000010, phy + DDRC_PHY_REG(0x900a5));
	writel(0x00000109, phy + DDRC_PHY_REG(0x900a6));
	writel(0x00000811, phy + DDRC_PHY_REG(0x40000));
	writel(0x00000880, phy + DDRC_PHY_REG(0x40020));
	writel(0x00000000, phy + DDRC_PHY_REG(0x40040));
	writel(0x00000000, phy + DDRC_PHY_REG(0x40060));
	writel(0x00004016, phy + DDRC_PHY_REG(0x40001));
	writel(0x00000083, phy + DDRC_PHY_REG(0x40021));
	writel(0x0000004f, phy + DDRC_PHY_REG(0x40041));
	writel(0x00000000, phy + DDRC_PHY_REG(0x40061));
	writel(0x00004040, phy + DDRC_PHY_REG(0x40002));
	writel(0x00000083, phy + DDRC_PHY_REG(0x40022));
	writel(0x00000051, phy + DDRC_PHY_REG(0x40042));
	writel(0x00000000, phy + DDRC_PHY_REG(0x40062));
	writel(0x00000811, phy + DDRC_PHY_REG(0x40003));
	writel(0x00000880, phy + DDRC_PHY_REG(0x40023));
	writel(0x00000000, phy + DDRC_PHY_REG(0x40043));
	writel(0x00000000, phy + DDRC_PHY_REG(0x40063));
	writel(0x00000720, phy + DDRC_PHY_REG(0x40004));
	writel(0x0000000f, phy + DDRC_PHY_REG(0x40024));
	writel(0x00001740, phy + DDRC_PHY_REG(0x40044));
	writel(0x00000000, phy + DDRC_PHY_REG(0x40064));
	writel(0x00000016, phy + DDRC_PHY_REG(0x40005));
	writel(0x00000083, phy + DDRC_PHY_REG(0x40025));
	writel(0x0000004b, phy + DDRC_PHY_REG(0x40045));
	writel(0x00000000, phy + DDRC_PHY_REG(0x40065));
	writel(0x00000716, phy + DDRC_PHY_REG(0x40006));
	writel(0x0000000f, phy + DDRC_PHY_REG(0x40026));
	writel(0x00002001, phy + DDRC_PHY_REG(0x40046));
	writel(0x00000000, phy + DDRC_PHY_REG(0x40066));
	writel(0x00000716, phy + DDRC_PHY_REG(0x40007));
	writel(0x0000000f, phy + DDRC_PHY_REG(0x40027));
	writel(0x00002800, phy + DDRC_PHY_REG(0x40047));
	writel(0x00000000, phy + DDRC_PHY_REG(0x40067));
	writel(0x00000716, phy + DDRC_PHY_REG(0x40008));
	writel(0x0000000f, phy + DDRC_PHY_REG(0x40028));
	writel(0x00000f00, phy + DDRC_PHY_REG(0x40048));
	writel(0x00000000, phy + DDRC_PHY_REG(0x40068));
	writel(0x00000720, phy + DDRC_PHY_REG(0x40009));
	writel(0x0000000f, phy + DDRC_PHY_REG(0x40029));
	writel(0x00001400, phy + DDRC_PHY_REG(0x40049));
	writel(0x00000000, phy + DDRC_PHY_REG(0x40069));
	writel(0x00000e08, phy + DDRC_PHY_REG(0x4000a));
	writel(0x00000c15, phy + DDRC_PHY_REG(0x4002a));
	writel(0x00000000, phy + DDRC_PHY_REG(0x4004a));
	writel(0x00000000, phy + DDRC_PHY_REG(0x4006a));
	writel(0x00000623, phy + DDRC_PHY_REG(0x4000b));
	writel(0x00000015, phy + DDRC_PHY_REG(0x4002b));
	writel(0x00000000, phy + DDRC_PHY_REG(0x4004b));
	writel(0x00000000, phy + DDRC_PHY_REG(0x4006b));
	writel(0x00004004, phy + DDRC_PHY_REG(0x4000c));
	writel(0x00000080, phy + DDRC_PHY_REG(0x4002c));
	writel(0x00000000, phy + DDRC_PHY_REG(0x4004c));
	writel(0x00000000, phy + DDRC_PHY_REG(0x4006c));
	writel(0x00000e08, phy + DDRC_PHY_REG(0x4000d));
	writel(0x00000c1a, phy + DDRC_PHY_REG(0x4002d));
	writel(0x00000000, phy + DDRC_PHY_REG(0x4004d));
	writel(0x00000000, phy + DDRC_PHY_REG(0x4006d));
	writel(0x00000623, phy + DDRC_PHY_REG(0x4000e));
	writel(0x0000001a, phy + DDRC_PHY_REG(0x4002e));
	writel(0x00000000, phy + DDRC_PHY_REG(0x4004e));
	writel(0x00000000, phy + DDRC_PHY_REG(0x4006e));
	writel(0x00004040, phy + DDRC_PHY_REG(0x4000f));
	writel(0x00000080, phy + DDRC_PHY_REG(0x4002f));
	writel(0x00000000, phy + DDRC_PHY_REG(0x4004f));
	writel(0x00000000, phy + DDRC_PHY_REG(0x4006f));
	writel(0x00002604, phy + DDRC_PHY_REG(0x40010));
	writel(0x00000015, phy + DDRC_PHY_REG(0x40030));
	writel(0x00000000, phy + DDRC_PHY_REG(0x40050));
	writel(0x00000000, phy + DDRC_PHY_REG(0x40070));
	writel(0x00000708, phy + DDRC_PHY_REG(0x40011));
	writel(0x00000005, phy + DDRC_PHY_REG(0x40031));
	writel(0x00000000, phy + DDRC_PHY_REG(0x40051));
	writel(0x00002002, phy + DDRC_PHY_REG(0x40071));
	writel(0x00000008, phy + DDRC_PHY_REG(0x40012));
	writel(0x00000080, phy + DDRC_PHY_REG(0x40032));
	writel(0x00000000, phy + DDRC_PHY_REG(0x40052));
	writel(0x00000000, phy + DDRC_PHY_REG(0x40072));
	writel(0x00002604, phy + DDRC_PHY_REG(0x40013));
	writel(0x0000001a, phy + DDRC_PHY_REG(0x40033));
	writel(0x00000000, phy + DDRC_PHY_REG(0x40053));
	writel(0x00000000, phy + DDRC_PHY_REG(0x40073));
	writel(0x00000708, phy + DDRC_PHY_REG(0x40014));
	writel(0x0000000a, phy + DDRC_PHY_REG(0x40034));
	writel(0x00000000, phy + DDRC_PHY_REG(0x40054));
	writel(0x00002002, phy + DDRC_PHY_REG(0x40074));
	writel(0x00004040, phy + DDRC_PHY_REG(0x40015));
	writel(0x00000080, phy + DDRC_PHY_REG(0x40035));
	writel(0x00000000, phy + DDRC_PHY_REG(0x40055));
	writel(0x00000000, phy + DDRC_PHY_REG(0x40075));
	writel(0x0000060a, phy + DDRC_PHY_REG(0x40016));
	writel(0x00000015, phy + DDRC_PHY_REG(0x40036));
	writel(0x00001200, phy + DDRC_PHY_REG(0x40056));
	writel(0x00000000, phy + DDRC_PHY_REG(0x40076));
	writel(0x0000061a, phy + DDRC_PHY_REG(0x40017));
	writel(0x00000015, phy + DDRC_PHY_REG(0x40037));
	writel(0x00001300, phy + DDRC_PHY_REG(0x40057));
	writel(0x00000000, phy + DDRC_PHY_REG(0x40077));
	writel(0x0000060a, phy + DDRC_PHY_REG(0x40018));
	writel(0x0000001a, phy + DDRC_PHY_REG(0x40038));
	writel(0x00001200, phy + DDRC_PHY_REG(0x40058));
	writel(0x00000000, phy + DDRC_PHY_REG(0x40078));
	writel(0x00000642, phy + DDRC_PHY_REG(0x40019));
	writel(0x0000001a, phy + DDRC_PHY_REG(0x40039));
	writel(0x00001300, phy + DDRC_PHY_REG(0x40059));
	writel(0x00000000, phy + DDRC_PHY_REG(0x40079));
	writel(0x00004808, phy + DDRC_PHY_REG(0x4001a));
	writel(0x00000880, phy + DDRC_PHY_REG(0x4003a));
	writel(0x00000000, phy + DDRC_PHY_REG(0x4005a));
	writel(0x00000000, phy + DDRC_PHY_REG(0x4007a));
	writel(0x00000000, phy + DDRC_PHY_REG(0x900a7));
	writel(0x00000790, phy + DDRC_PHY_REG(0x900a8));
	writel(0x0000011a, phy + DDRC_PHY_REG(0x900a9));
	writel(0x00000008, phy + DDRC_PHY_REG(0x900aa));
	writel(0x000007aa, phy + DDRC_PHY_REG(0x900ab));
	writel(0x0000002a, phy + DDRC_PHY_REG(0x900ac));
	writel(0x00000010, phy + DDRC_PHY_REG(0x900ad));
	writel(0x000007b2, phy + DDRC_PHY_REG(0x900ae));
	writel(0x0000002a, phy + DDRC_PHY_REG(0x900af));
	writel(0x00000000, phy + DDRC_PHY_REG(0x900b0));
	writel(0x000007c8, phy + DDRC_PHY_REG(0x900b1));
	writel(0x00000109, phy + DDRC_PHY_REG(0x900b2));
	writel(0x00000010, phy + DDRC_PHY_REG(0x900b3));
	writel(0x000002a8, phy + DDRC_PHY_REG(0x900b4));
	writel(0x00000129, phy + DDRC_PHY_REG(0x900b5));
	writel(0x00000008, phy + DDRC_PHY_REG(0x900b6));
	writel(0x00000370, phy + DDRC_PHY_REG(0x900b7));
	writel(0x00000129, phy + DDRC_PHY_REG(0x900b8));
	writel(0x0000000a, phy + DDRC_PHY_REG(0x900b9));
	writel(0x000003c8, phy + DDRC_PHY_REG(0x900ba));
	writel(0x000001a9, phy + DDRC_PHY_REG(0x900bb));
	writel(0x0000000c, phy + DDRC_PHY_REG(0x900bc));
	writel(0x00000408, phy + DDRC_PHY_REG(0x900bd));
	writel(0x00000199, phy + DDRC_PHY_REG(0x900be));
	writel(0x00000014, phy + DDRC_PHY_REG(0x900bf));
	writel(0x00000790, phy + DDRC_PHY_REG(0x900c0));
	writel(0x0000011a, phy + DDRC_PHY_REG(0x900c1));
	writel(0x00000008, phy + DDRC_PHY_REG(0x900c2));
	writel(0x00000004, phy + DDRC_PHY_REG(0x900c3));
	writel(0x00000018, phy + DDRC_PHY_REG(0x900c4));
	writel(0x0000000c, phy + DDRC_PHY_REG(0x900c5));
	writel(0x00000408, phy + DDRC_PHY_REG(0x900c6));
	writel(0x00000199, phy + DDRC_PHY_REG(0x900c7));
	writel(0x00000008, phy + DDRC_PHY_REG(0x900c8));
	writel(0x00008568, phy + DDRC_PHY_REG(0x900c9));
	writel(0x00000108, phy + DDRC_PHY_REG(0x900ca));
	writel(0x00000018, phy + DDRC_PHY_REG(0x900cb));
	writel(0x00000790, phy + DDRC_PHY_REG(0x900cc));
	writel(0x0000016a, phy + DDRC_PHY_REG(0x900cd));
	writel(0x00000008, phy + DDRC_PHY_REG(0x900ce));
	writel(0x000001d8, phy + DDRC_PHY_REG(0x900cf));
	writel(0x00000169, phy + DDRC_PHY_REG(0x900d0));
	writel(0x00000010, phy + DDRC_PHY_REG(0x900d1));
	writel(0x00008558, phy + DDRC_PHY_REG(0x900d2));
	writel(0x00000168, phy + DDRC_PHY_REG(0x900d3));
	writel(0x00000070, phy + DDRC_PHY_REG(0x900d4));
	writel(0x00000788, phy + DDRC_PHY_REG(0x900d5));
	writel(0x0000016a, phy + DDRC_PHY_REG(0x900d6));
	writel(0x00001ff8, phy + DDRC_PHY_REG(0x900d7));
	writel(0x000085a8, phy + DDRC_PHY_REG(0x900d8));
	writel(0x000001e8, phy + DDRC_PHY_REG(0x900d9));
	writel(0x00000050, phy + DDRC_PHY_REG(0x900da));
	writel(0x00000798, phy + DDRC_PHY_REG(0x900db));
	writel(0x0000016a, phy + DDRC_PHY_REG(0x900dc));
	writel(0x00000060, phy + DDRC_PHY_REG(0x900dd));
	writel(0x000007a0, phy + DDRC_PHY_REG(0x900de));
	writel(0x0000016a, phy + DDRC_PHY_REG(0x900df));
	writel(0x00000008, phy + DDRC_PHY_REG(0x900e0));
	writel(0x00008310, phy + DDRC_PHY_REG(0x900e1));
	writel(0x00000168, phy + DDRC_PHY_REG(0x900e2));
	writel(0x00000008, phy + DDRC_PHY_REG(0x900e3));
	writel(0x0000a310, phy + DDRC_PHY_REG(0x900e4));
	writel(0x00000168, phy + DDRC_PHY_REG(0x900e5));
	writel(0x0000000a, phy + DDRC_PHY_REG(0x900e6));
	writel(0x00000408, phy + DDRC_PHY_REG(0x900e7));
	writel(0x00000169, phy + DDRC_PHY_REG(0x900e8));
	writel(0x0000006e, phy + DDRC_PHY_REG(0x900e9));
	writel(0x00000000, phy + DDRC_PHY_REG(0x900ea));
	writel(0x00000068, phy + DDRC_PHY_REG(0x900eb));
	writel(0x00000000, phy + DDRC_PHY_REG(0x900ec));
	writel(0x00000408, phy + DDRC_PHY_REG(0x900ed));
	writel(0x00000169, phy + DDRC_PHY_REG(0x900ee));
	writel(0x00000000, phy + DDRC_PHY_REG(0x900ef));
	writel(0x00008310, phy + DDRC_PHY_REG(0x900f0));
	writel(0x00000168, phy + DDRC_PHY_REG(0x900f1));
	writel(0x00000000, phy + DDRC_PHY_REG(0x900f2));
	writel(0x0000a310, phy + DDRC_PHY_REG(0x900f3));
	writel(0x00000168, phy + DDRC_PHY_REG(0x900f4));
	writel(0x00001ff8, phy + DDRC_PHY_REG(0x900f5));
	writel(0x000085a8, phy + DDRC_PHY_REG(0x900f6));
	writel(0x000001e8, phy + DDRC_PHY_REG(0x900f7));
	writel(0x00000068, phy + DDRC_PHY_REG(0x900f8));
	writel(0x00000798, phy + DDRC_PHY_REG(0x900f9));
	writel(0x0000016a, phy + DDRC_PHY_REG(0x900fa));
	writel(0x00000078, phy + DDRC_PHY_REG(0x900fb));
	writel(0x000007a0, phy + DDRC_PHY_REG(0x900fc));
	writel(0x0000016a, phy + DDRC_PHY_REG(0x900fd));
	writel(0x00000068, phy + DDRC_PHY_REG(0x900fe));
	writel(0x00000790, phy + DDRC_PHY_REG(0x900ff));
	writel(0x0000016a, phy + DDRC_PHY_REG(0x90100));
	writel(0x00000008, phy + DDRC_PHY_REG(0x90101));
	writel(0x00008b10, phy + DDRC_PHY_REG(0x90102));
	writel(0x00000168, phy + DDRC_PHY_REG(0x90103));
	writel(0x00000008, phy + DDRC_PHY_REG(0x90104));
	writel(0x0000ab10, phy + DDRC_PHY_REG(0x90105));
	writel(0x00000168, phy + DDRC_PHY_REG(0x90106));
	writel(0x0000000a, phy + DDRC_PHY_REG(0x90107));
	writel(0x00000408, phy + DDRC_PHY_REG(0x90108));
	writel(0x00000169, phy + DDRC_PHY_REG(0x90109));
	writel(0x00000058, phy + DDRC_PHY_REG(0x9010a));
	writel(0x00000000, phy + DDRC_PHY_REG(0x9010b));
	writel(0x00000068, phy + DDRC_PHY_REG(0x9010c));
	writel(0x00000000, phy + DDRC_PHY_REG(0x9010d));
	writel(0x00000408, phy + DDRC_PHY_REG(0x9010e));
	writel(0x00000169, phy + DDRC_PHY_REG(0x9010f));
	writel(0x00000000, phy + DDRC_PHY_REG(0x90110));
	writel(0x00008b10, phy + DDRC_PHY_REG(0x90111));
	writel(0x00000168, phy + DDRC_PHY_REG(0x90112));
	writel(0x00000000, phy + DDRC_PHY_REG(0x90113));
	writel(0x0000ab10, phy + DDRC_PHY_REG(0x90114));
	writel(0x00000168, phy + DDRC_PHY_REG(0x90115));
	writel(0x00000000, phy + DDRC_PHY_REG(0x90116));
	writel(0x000001d8, phy + DDRC_PHY_REG(0x90117));
	writel(0x00000169, phy + DDRC_PHY_REG(0x90118));
	writel(0x00000080, phy + DDRC_PHY_REG(0x90119));
	writel(0x00000790, phy + DDRC_PHY_REG(0x9011a));
	writel(0x0000016a, phy + DDRC_PHY_REG(0x9011b));
	writel(0x00000018, phy + DDRC_PHY_REG(0x9011c));
	writel(0x000007aa, phy + DDRC_PHY_REG(0x9011d));
	writel(0x0000006a, phy + DDRC_PHY_REG(0x9011e));
	writel(0x0000000a, phy + DDRC_PHY_REG(0x9011f));
	writel(0x00000000, phy + DDRC_PHY_REG(0x90120));
	writel(0x000001e9, phy + DDRC_PHY_REG(0x90121));
	writel(0x00000008, phy + DDRC_PHY_REG(0x90122));
	writel(0x00008080, phy + DDRC_PHY_REG(0x90123));
	writel(0x00000108, phy + DDRC_PHY_REG(0x90124));
	writel(0x0000000f, phy + DDRC_PHY_REG(0x90125));
	writel(0x00000408, phy + DDRC_PHY_REG(0x90126));
	writel(0x00000169, phy + DDRC_PHY_REG(0x90127));
	writel(0x0000000c, phy + DDRC_PHY_REG(0x90128));
	writel(0x00000000, phy + DDRC_PHY_REG(0x90129));
	writel(0x00000068, phy + DDRC_PHY_REG(0x9012a));
	writel(0x00000009, phy + DDRC_PHY_REG(0x9012b));
	writel(0x00000000, phy + DDRC_PHY_REG(0x9012c));
	writel(0x000001a9, phy + DDRC_PHY_REG(0x9012d));
	writel(0x00000000, phy + DDRC_PHY_REG(0x9012e));
	writel(0x00000408, phy + DDRC_PHY_REG(0x9012f));
	writel(0x00000169, phy + DDRC_PHY_REG(0x90130));
	writel(0x00000000, phy + DDRC_PHY_REG(0x90131));
	writel(0x00008080, phy + DDRC_PHY_REG(0x90132));
	writel(0x00000108, phy + DDRC_PHY_REG(0x90133));
	writel(0x00000008, phy + DDRC_PHY_REG(0x90134));
	writel(0x000007aa, phy + DDRC_PHY_REG(0x90135));
	writel(0x0000006a, phy + DDRC_PHY_REG(0x90136));
	writel(0x00000000, phy + DDRC_PHY_REG(0x90137));
	writel(0x00008568, phy + DDRC_PHY_REG(0x90138));
	writel(0x00000108, phy + DDRC_PHY_REG(0x90139));
	writel(0x000000b7, phy + DDRC_PHY_REG(0x9013a));
	writel(0x00000790, phy + DDRC_PHY_REG(0x9013b));
	writel(0x0000016a, phy + DDRC_PHY_REG(0x9013c));
	writel(0x0000001d, phy + DDRC_PHY_REG(0x9013d));
	writel(0x00000000, phy + DDRC_PHY_REG(0x9013e));
	writel(0x00000068, phy + DDRC_PHY_REG(0x9013f));
	writel(0x00000008, phy + DDRC_PHY_REG(0x90140));
	writel(0x00008558, phy + DDRC_PHY_REG(0x90141));
	writel(0x00000168, phy + DDRC_PHY_REG(0x90142));
	writel(0x0000000f, phy + DDRC_PHY_REG(0x90143));
	writel(0x00000408, phy + DDRC_PHY_REG(0x90144));
	writel(0x00000169, phy + DDRC_PHY_REG(0x90145));
	writel(0x0000000c, phy + DDRC_PHY_REG(0x90146));
	writel(0x00000000, phy + DDRC_PHY_REG(0x90147));
	writel(0x00000068, phy + DDRC_PHY_REG(0x90148));
	writel(0x00000000, phy + DDRC_PHY_REG(0x90149));
	writel(0x00000408, phy + DDRC_PHY_REG(0x9014a));
	writel(0x00000169, phy + DDRC_PHY_REG(0x9014b));
	writel(0x00000000, phy + DDRC_PHY_REG(0x9014c));
	writel(0x00008558, phy + DDRC_PHY_REG(0x9014d));
	writel(0x00000168, phy + DDRC_PHY_REG(0x9014e));
	writel(0x00000008, phy + DDRC_PHY_REG(0x9014f));
	writel(0x000003c8, phy + DDRC_PHY_REG(0x90150));
	writel(0x000001a9, phy + DDRC_PHY_REG(0x90151));
	writel(0x00000003, phy + DDRC_PHY_REG(0x90152));
	writel(0x00000370, phy + DDRC_PHY_REG(0x90153));
	writel(0x00000129, phy + DDRC_PHY_REG(0x90154));
	writel(0x00000020, phy + DDRC_PHY_REG(0x90155));
	writel(0x000002aa, phy + DDRC_PHY_REG(0x90156));
	writel(0x00000009, phy + DDRC_PHY_REG(0x90157));
	writel(0x00000000, phy + DDRC_PHY_REG(0x90158));
	writel(0x00000400, phy + DDRC_PHY_REG(0x90159));
	writel(0x0000010e, phy + DDRC_PHY_REG(0x9015a));
	writel(0x00000008, phy + DDRC_PHY_REG(0x9015b));
	writel(0x000000e8, phy + DDRC_PHY_REG(0x9015c));
	writel(0x00000109, phy + DDRC_PHY_REG(0x9015d));
	writel(0x00000000, phy + DDRC_PHY_REG(0x9015e));
	writel(0x00008140, phy + DDRC_PHY_REG(0x9015f));
	writel(0x0000010c, phy + DDRC_PHY_REG(0x90160));
	writel(0x00000010, phy + DDRC_PHY_REG(0x90161));
	writel(0x00008138, phy + DDRC_PHY_REG(0x90162));
	writel(0x0000010c, phy + DDRC_PHY_REG(0x90163));
	writel(0x00000008, phy + DDRC_PHY_REG(0x90164));
	writel(0x000007c8, phy + DDRC_PHY_REG(0x90165));
	writel(0x00000101, phy + DDRC_PHY_REG(0x90166));
	writel(0x00000008, phy + DDRC_PHY_REG(0x90167));
	writel(0x00000000, phy + DDRC_PHY_REG(0x90168));
	writel(0x00000008, phy + DDRC_PHY_REG(0x90169));
	writel(0x00000008, phy + DDRC_PHY_REG(0x9016a));
	writel(0x00000448, phy + DDRC_PHY_REG(0x9016b));
	writel(0x00000109, phy + DDRC_PHY_REG(0x9016c));
	writel(0x0000000f, phy + DDRC_PHY_REG(0x9016d));
	writel(0x000007c0, phy + DDRC_PHY_REG(0x9016e));
	writel(0x00000109, phy + DDRC_PHY_REG(0x9016f));
	writel(0x00000000, phy + DDRC_PHY_REG(0x90170));
	writel(0x000000e8, phy + DDRC_PHY_REG(0x90171));
	writel(0x00000109, phy + DDRC_PHY_REG(0x90172));
	writel(0x00000047, phy + DDRC_PHY_REG(0x90173));
	writel(0x00000630, phy + DDRC_PHY_REG(0x90174));
	writel(0x00000109, phy + DDRC_PHY_REG(0x90175));
	writel(0x00000008, phy + DDRC_PHY_REG(0x90176));
	writel(0x00000618, phy + DDRC_PHY_REG(0x90177));
	writel(0x00000109, phy + DDRC_PHY_REG(0x90178));
	writel(0x00000008, phy + DDRC_PHY_REG(0x90179));
	writel(0x000000e0, phy + DDRC_PHY_REG(0x9017a));
	writel(0x00000109, phy + DDRC_PHY_REG(0x9017b));
	writel(0x00000000, phy + DDRC_PHY_REG(0x9017c));
	writel(0x000007c8, phy + DDRC_PHY_REG(0x9017d));
	writel(0x00000109, phy + DDRC_PHY_REG(0x9017e));
	writel(0x00000008, phy + DDRC_PHY_REG(0x9017f));
	writel(0x00008140, phy + DDRC_PHY_REG(0x90180));
	writel(0x0000010c, phy + DDRC_PHY_REG(0x90181));
	writel(0x00000000, phy + DDRC_PHY_REG(0x90182));
	writel(0x00000001, phy + DDRC_PHY_REG(0x90183));
	writel(0x00000008, phy + DDRC_PHY_REG(0x90184));
	writel(0x00000008, phy + DDRC_PHY_REG(0x90185));
	writel(0x00000004, phy + DDRC_PHY_REG(0x90186));
	writel(0x00000008, phy + DDRC_PHY_REG(0x90187));
	writel(0x00000008, phy + DDRC_PHY_REG(0x90188));
	writel(0x000007c8, phy + DDRC_PHY_REG(0x90189));
	writel(0x00000101, phy + DDRC_PHY_REG(0x9018a));
	writel(0x00000000, phy + DDRC_PHY_REG(0x90006));
	writel(0x00000000, phy + DDRC_PHY_REG(0x90007));
	writel(0x00000008, phy + DDRC_PHY_REG(0x90008));
	writel(0x00000000, phy + DDRC_PHY_REG(0x90009));
	writel(0x00000000, phy + DDRC_PHY_REG(0x9000a));
	writel(0x00000000, phy + DDRC_PHY_REG(0x9000b));
	writel(0x00000400, phy + DDRC_PHY_REG(0xd00e7));
	writel(0x00000000, phy + DDRC_PHY_REG(0x90017));
	writel(0x0000002a, phy + DDRC_PHY_REG(0x9001f));
	writel(0x0000006a, phy + DDRC_PHY_REG(0x90026));
	writel(0x00000000, phy + DDRC_PHY_REG(0x400d0));
	writel(0x00000101, phy + DDRC_PHY_REG(0x400d1));
	writel(0x00000105, phy + DDRC_PHY_REG(0x400d2));
	writel(0x00000107, phy + DDRC_PHY_REG(0x400d3));
	writel(0x0000010f, phy + DDRC_PHY_REG(0x400d4));
	writel(0x00000202, phy + DDRC_PHY_REG(0x400d5));
	writel(0x0000020a, phy + DDRC_PHY_REG(0x400d6));
	writel(0x0000020b, phy + DDRC_PHY_REG(0x400d7));
	writel(0x00000002, phy + DDRC_PHY_REG(0x2003a));
	writel(0x00000000, phy + DDRC_PHY_REG(0x9000c));
	writel(0x00000173, phy + DDRC_PHY_REG(0x9000d));
	writel(0x00000060, phy + DDRC_PHY_REG(0x9000e));
	writel(0x00006110, phy + DDRC_PHY_REG(0x9000f));
	writel(0x00002152, phy + DDRC_PHY_REG(0x90010));
	writel(0x0000dfbd, phy + DDRC_PHY_REG(0x90011));
	writel(0x00000060, phy + DDRC_PHY_REG(0x90012));
	writel(0x00006152, phy + DDRC_PHY_REG(0x90013));
	writel(0x0000005a, phy + DDRC_PHY_REG(0x20010));
	writel(0x00000003, phy + DDRC_PHY_REG(0x20011));
	writel(0x000000e0, phy + DDRC_PHY_REG(0x40080));
	writel(0x00000012, phy + DDRC_PHY_REG(0x40081));
	writel(0x000000e0, phy + DDRC_PHY_REG(0x40082));
	writel(0x00000012, phy + DDRC_PHY_REG(0x40083));
	writel(0x000000e0, phy + DDRC_PHY_REG(0x40084));
	writel(0x00000012, phy + DDRC_PHY_REG(0x40085));
	writel(0x0000000f, phy + DDRC_PHY_REG(0x400fd));
	writel(0x00000001, phy + DDRC_PHY_REG(0x10011));
	writel(0x00000001, phy + DDRC_PHY_REG(0x10012));
	writel(0x00000180, phy + DDRC_PHY_REG(0x10013));
	writel(0x00000001, phy + DDRC_PHY_REG(0x10018));
	writel(0x00006209, phy + DDRC_PHY_REG(0x10002));
	writel(0x00000001, phy + DDRC_PHY_REG(0x100b2));
	writel(0x00000001, phy + DDRC_PHY_REG(0x101b4));
	writel(0x00000001, phy + DDRC_PHY_REG(0x102b4));
	writel(0x00000001, phy + DDRC_PHY_REG(0x103b4));
	writel(0x00000001, phy + DDRC_PHY_REG(0x104b4));
	writel(0x00000001, phy + DDRC_PHY_REG(0x105b4));
	writel(0x00000001, phy + DDRC_PHY_REG(0x106b4));
	writel(0x00000001, phy + DDRC_PHY_REG(0x107b4));
	writel(0x00000001, phy + DDRC_PHY_REG(0x108b4));
	writel(0x00000001, phy + DDRC_PHY_REG(0x11011));
	writel(0x00000001, phy + DDRC_PHY_REG(0x11012));
	writel(0x00000180, phy + DDRC_PHY_REG(0x11013));
	writel(0x00000001, phy + DDRC_PHY_REG(0x11018));
	writel(0x00006209, phy + DDRC_PHY_REG(0x11002));
	writel(0x00000001, phy + DDRC_PHY_REG(0x110b2));
	writel(0x00000001, phy + DDRC_PHY_REG(0x111b4));
	writel(0x00000001, phy + DDRC_PHY_REG(0x112b4));
	writel(0x00000001, phy + DDRC_PHY_REG(0x113b4));
	writel(0x00000001, phy + DDRC_PHY_REG(0x114b4));
	writel(0x00000001, phy + DDRC_PHY_REG(0x115b4));
	writel(0x00000001, phy + DDRC_PHY_REG(0x116b4));
	writel(0x00000001, phy + DDRC_PHY_REG(0x117b4));
	writel(0x00000001, phy + DDRC_PHY_REG(0x118b4));
	writel(0x00000001, phy + DDRC_PHY_REG(0x12011));
	writel(0x00000001, phy + DDRC_PHY_REG(0x12012));
	writel(0x00000180, phy + DDRC_PHY_REG(0x12013));
	writel(0x00000001, phy + DDRC_PHY_REG(0x12018));
	writel(0x00006209, phy + DDRC_PHY_REG(0x12002));
	writel(0x00000001, phy + DDRC_PHY_REG(0x120b2));
	writel(0x00000001, phy + DDRC_PHY_REG(0x121b4));
	writel(0x00000001, phy + DDRC_PHY_REG(0x122b4));
	writel(0x00000001, phy + DDRC_PHY_REG(0x123b4));
	writel(0x00000001, phy + DDRC_PHY_REG(0x124b4));
	writel(0x00000001, phy + DDRC_PHY_REG(0x125b4));
	writel(0x00000001, phy + DDRC_PHY_REG(0x126b4));
	writel(0x00000001, phy + DDRC_PHY_REG(0x127b4));
	writel(0x00000001, phy + DDRC_PHY_REG(0x128b4));
	writel(0x00000001, phy + DDRC_PHY_REG(0x13011));
	writel(0x00000001, phy + DDRC_PHY_REG(0x13012));
	writel(0x00000180, phy + DDRC_PHY_REG(0x13013));
	writel(0x00000001, phy + DDRC_PHY_REG(0x13018));
	writel(0x00006209, phy + DDRC_PHY_REG(0x13002));
	writel(0x00000001, phy + DDRC_PHY_REG(0x130b2));
	writel(0x00000001, phy + DDRC_PHY_REG(0x131b4));
	writel(0x00000001, phy + DDRC_PHY_REG(0x132b4));
	writel(0x00000001, phy + DDRC_PHY_REG(0x133b4));
	writel(0x00000001, phy + DDRC_PHY_REG(0x134b4));
	writel(0x00000001, phy + DDRC_PHY_REG(0x135b4));
	writel(0x00000001, phy + DDRC_PHY_REG(0x136b4));
	writel(0x00000001, phy + DDRC_PHY_REG(0x137b4));
	writel(0x00000001, phy + DDRC_PHY_REG(0x138b4));
	writel(0x00000002, phy + DDRC_PHY_REG(0x2003a));
	writel(0x00000002, phy + DDRC_PHY_REG(0xc0080));
	writel(0x00000001, phy + DDRC_PHY_REG(0xd0000));
	writel(0x00000000, phy + DDRC_PHY_REG(0xd0000));
	readl(phy + DDRC_PHY_REG(0x00020010));
	writel(0x0000006a, phy + DDRC_PHY_REG(0x20010));
	readl(phy + DDRC_PHY_REG(0x0002001d));
	writel(0x00000001, phy + DDRC_PHY_REG(0x2001d));
	/*
	 * CalBusy.0 =1, indicates the calibrator is actively calibrating.
	 * Wait Calibrating done.
	 */
	error = readl_poll_timeout(phy + DDRC_PHY_REG(0x20097),
				   r, !(r & BIT(1)), MSECOND);
	BUG_ON(error);

	writel(0x00000000, phy + DDRC_PHY_REG(0xd0000));
	writel(0x00000000, phy + DDRC_PHY_REG(0x2006e));
}

static void lpddr4_800mhz_cfg_umctl2(void __iomem *ddr)
{
	writel(0x00000001, ddr + DDRC_DBG1);
	writel(0x00000001, ddr + DDRC_PWRCTL);
	writel(0x83080020, ddr + DDRC_MSTR);
	writel(0x006180e0, ddr + DDRC_RFSHTMG);
	writel(0xc003061b, ddr + DDRC_INIT0);
	writel(0x009d0000, ddr + DDRC_INIT1);
	writel(0x0000fe05, ddr + DDRC_INIT2);
	writel(0x00d4002d, ddr + DDRC_INIT3);
	writel(0x00310008, ddr + DDRC_INIT4);
	writel(0x00040009, ddr + DDRC_INIT5);
	writel(0x0046004d, ddr + DDRC_INIT6);
	writel(0x0005004d, ddr + DDRC_INIT7);
	writel(0x00000979, ddr + DDRC_RANKCTL);
	writel(0x1a203522, ddr + DDRC_DRAMTMG0);
	writel(0x00060630, ddr + DDRC_DRAMTMG1);
	writel(0x070e1214, ddr + DDRC_DRAMTMG2);
	writel(0x00b0c006, ddr + DDRC_DRAMTMG3);
	writel(0x0f04080f, ddr + DDRC_DRAMTMG4);
	writel(0x0d0d0c0c, ddr + DDRC_DRAMTMG5);
	writel(0x01010007, ddr + DDRC_DRAMTMG6);
	writel(0x0000060a, ddr + DDRC_DRAMTMG7);
	writel(0x01010101, ddr + DDRC_DRAMTMG8);
	writel(0x40000008, ddr + DDRC_DRAMTMG9);
	writel(0x00050d01, ddr + DDRC_DRAMTMG10);
	writel(0x01010008, ddr + DDRC_DRAMTMG11);
	writel(0x00020000, ddr + DDRC_DRAMTMG12);
	writel(0x18100002, ddr + DDRC_DRAMTMG13);
	writel(0x00000dc2, ddr + DDRC_DRAMTMG14);
	writel(0x80000000, ddr + DDRC_DRAMTMG15);
	writel(0x00a00050, ddr + DDRC_DRAMTMG17);
	writel(0x53200018, ddr + DDRC_ZQCTL0);
	writel(0x02800070, ddr + DDRC_ZQCTL1);
	writel(0x00000000, ddr + DDRC_ZQCTL2);
	writel(0x0397820a, ddr + DDRC_DFITMG0);
	writel(0x0397820a, ddr + DDRC_FREQ1_DFITMG0);
	writel(0x0397820a, ddr + DDRC_FREQ2_DFITMG0);
	writel(0x00020103, ddr + DDRC_DFITMG1);
	writel(0xe0400018, ddr + DDRC_DFIUPD0);
	writel(0x00df00e4, ddr + DDRC_DFIUPD1);
	writel(0x00000000, ddr + DDRC_DFIUPD2);
	writel(0x00000011, ddr + DDRC_DFIMISC);
	writel(0x0000170a, ddr + DDRC_DFITMG2);
	writel(0x00000001, ddr + DDRC_DBICTL);
	writel(0x00000000, ddr + DDRC_DFIPHYMSTR);
	/* Address map is from MSB 29: r15, r14, cs, r13-r0, b2-b0, c9-c0 */
	writel(0x00000015, ddr + DDRC_ADDRMAP0);
	writel(0x00001f1f, ddr + DDRC_ADDRMAP4);
	/* bank interleave */
	writel(0x00080808, ddr + DDRC_ADDRMAP1);
	writel(0x07070707, ddr + DDRC_ADDRMAP5);
	writel(0x08080707, ddr + DDRC_ADDRMAP6);
	writel(0x020f0c54, ddr + DDRC_ODTCFG);
	writel(0x00000000, ddr + DDRC_ODTMAP);
	writel(0x00000001, ddr + DDRC_PCFG0_PCTRL);

	/* performance setting */
	writel(0x0b060908, ddr + DDRC_ODTCFG);
	writel(0x00000000, ddr + DDRC_ODTMAP);
	writel(0x29511505, ddr + DDRC_SCHED);
	writel(0x0000002c, ddr + DDRC_SCHED1);
	writel(0x5900575b, ddr + DDRC_PERFHPR1);
	writel(0x900093e7, ddr + DDRC_PERFLPR1);
	writel(0x02005574, ddr + DDRC_PERFWR1);
	writel(0x00000016, ddr + DDRC_DBG0);
	writel(0x00000000, ddr + DDRC_DBG1);
	writel(0x00000000, ddr + DDRC_DBGCMD);
	writel(0x00000001, ddr + DDRC_SWCTL);
	writel(0x00000011, ddr + DDRC_POISONCFG);
	writel(0x00000111, ddr + DDRC_PCCFG);
	writel(0x000010f3, ddr + DDRC_PCFG0_PCFGR);
	writel(0x000072ff, ddr + DDRC_PCFG0_PCFGW);
	writel(0x00000001, ddr + DDRC_PCFG0_PCTRL);
	writel(0x01110d00, ddr + DDRC_PCFG0_PCFGQOS0);
	writel(0x00620790, ddr + DDRC_PCFG0_PCFGQOS1);
	writel(0x00100001, ddr + DDRC_PCFG0_PCFGWQOS0);
	writel(0x0000041f, ddr + DDRC_PCFG0_PCFGWQOS1);
	writel(0x00000202, ddr + DDRC_FREQ1_DERATEEN);
	writel(0xec78f4b5, ddr + DDRC_FREQ1_DERATEINT);
	writel(0x00618040, ddr + DDRC_FREQ1_RFSHCTL0);
	writel(0x00610090, ddr + DDRC_FREQ1_RFSHTMG);
}

static void lpddr4_100mhz_cfg_umctl2(void __iomem *ddr)
{
	writel(0x0d0b010c, ddr + DDRC_FREQ1_DRAMTMG0);
	writel(0x00030410, ddr + DDRC_FREQ1_DRAMTMG1);
	writel(0x0305090c, ddr + DDRC_FREQ1_DRAMTMG2);
	writel(0x00505006, ddr + DDRC_FREQ1_DRAMTMG3);
	writel(0x05040305, ddr + DDRC_FREQ1_DRAMTMG4);
	writel(0x0d0e0504, ddr + DDRC_FREQ1_DRAMTMG5);
	writel(0x0a060004, ddr + DDRC_FREQ1_DRAMTMG6);
	writel(0x0000090e, ddr + DDRC_FREQ1_DRAMTMG7);
	writel(0x00000032, ddr + DDRC_FREQ1_DRAMTMG14);
	writel(0x00000000, ddr + DDRC_FREQ1_DRAMTMG15);
	writel(0x0036001b, ddr + DDRC_FREQ1_DRAMTMG17);
	writel(0x7e9fbeb1, ddr + DDRC_FREQ1_DERATEINT);
	writel(0x0020d040, ddr + DDRC_FREQ1_RFSHCTL0);
	writel(0x03818200, ddr + DDRC_FREQ1_DFITMG0);
	writel(0x0a1a096c, ddr + DDRC_FREQ1_ODTCFG);
	writel(0x00000000, ddr + DDRC_FREQ1_DFITMG2);
	writel(0x00038014, ddr + DDRC_FREQ1_RFSHTMG);
	writel(0x00840000, ddr + DDRC_FREQ1_INIT3);
	writel(0x0000004d, ddr + DDRC_FREQ1_INIT6);
	writel(0x0000004d, ddr + DDRC_FREQ1_INIT7);
	writel(0x00310000, ddr + DDRC_FREQ1_INIT4);
}

static void lpddr4_25mhz_cfg_umctl2(void __iomem *ddr)
{
	writel(0x0d0b010c, ddr + DDRC_FREQ2_DRAMTMG0);
	writel(0x00030410, ddr + DDRC_FREQ2_DRAMTMG1);
	writel(0x0305090c, ddr + DDRC_FREQ2_DRAMTMG2);
	writel(0x00505006, ddr + DDRC_FREQ2_DRAMTMG3);
	writel(0x05040305, ddr + DDRC_FREQ2_DRAMTMG4);
	writel(0x0d0e0504, ddr + DDRC_FREQ2_DRAMTMG5);
	writel(0x0a060004, ddr + DDRC_FREQ2_DRAMTMG6);
	writel(0x0000090e, ddr + DDRC_FREQ2_DRAMTMG7);
	writel(0x00000032, ddr + DDRC_FREQ2_DRAMTMG14);
	writel(0x00000000, ddr + DDRC_FREQ2_DRAMTMG15);
	writel(0x0036001b, ddr + DDRC_FREQ2_DRAMTMG17);
	writel(0x7e9fbeb1, ddr + DDRC_FREQ2_DERATEINT);
	writel(0x0020d040, ddr + DDRC_FREQ2_RFSHCTL0);
	writel(0x03818200, ddr + DDRC_FREQ2_DFITMG0);
	writel(0x0a1a096c, ddr + DDRC_FREQ2_ODTCFG);
	writel(0x00000000, ddr + DDRC_FREQ2_DFITMG2);
	writel(0x0003800c, ddr + DDRC_FREQ2_RFSHTMG);
	writel(0x00840000, ddr + DDRC_FREQ2_INIT3);
	writel(0x0000004d, ddr + DDRC_FREQ2_INIT6);
	writel(0x0000004d, ddr + DDRC_FREQ2_INIT7);
	writel(0x00310000, ddr + DDRC_FREQ2_INIT4);
}

void nxp_imx8mq_evk_init_ddr(const u16 *imem_1d, size_t imem_1d_size,
			     const u16 *dmem_1d, size_t dmem_1d_size,
			     const u16 *imem_2d, size_t imem_2d_size,
			     const u16 *dmem_2d, size_t dmem_2d_size)

{
	void __iomem *gpc = IOMEM(MX8MQ_GPC_BASE_ADDR);
	void __iomem *ccm = IOMEM(MX8MQ_CCM_BASE_ADDR);
	void __iomem *src = IOMEM(MX8MQ_SRC_BASE_ADDR);
	void __iomem *ddr = IOMEM(MX8MQ_DDRC_CTL_BASE_ADDR);
	void __iomem *phy = IOMEM(MX8MQ_DDRC_PHY_BASE_ADDR);
	void __iomem *anatop = IOMEM(MX8MQ_ANATOP_BASE_ADDR);
	int error;
	u32 r;

	writel(CCM_TARGET_ROOTn_ENABLE |
	       DRAM_APB_CLK_ROOT__SYSTEM_PLL1_CLK |
	       CCM_TARGET_ROOTn_PRE_PODF(3),
	       ccm + CCM_TARGET_ROOTn(DRAM_APB_CLK_ROOT));

	/* 
	 * dram_pll_init();
	 */
	setbits_le32(gpc + GPC_PGC_CPU_0_1_MAPPING, DDR1_A53_DOMAIN);
	setbits_le32(gpc + GPC_PU_PGC_SW_PUP_REQ, DDR1_SW_PUP_REQ);

	/* Enable DDR1 and DDR2 domain */
	writel(SRC_RCR_ENABLE, src + SRC_DDRC_RCR);
	/* FIXME: Do we really need DDRC2? */
	writel(SRC_RCR_ENABLE, src + SRC_DDRC2_RCR);

	/* Clear power down bit */
	clrbits_le32(anatop + CCM_ANALOG_DRAM_PLL_CFG0, CCM_ANALOG_PLL_PD);
	/* Eanble ARM_PLL/SYS_PLL  */
	setbits_le32(anatop + CCM_ANALOG_DRAM_PLL_CFG0, CCM_ANALOG_PLL_CLKE);
	/* Clear bypass */
	clrbits_le32(anatop + CCM_ANALOG_DRAM_PLL_CFG0,
		     CCM_ANALOG_PLL_BYPASS1);
	/*
	 * TODO: This is present in NXP U-Boot code, but no
	 * justification for it is given. Is this delay needed?
	 */
	udelay(100);
	clrbits_le32(anatop + CCM_ANALOG_DRAM_PLL_CFG0,
		     CCM_ANALOG_PLL_BYPASS2);
	error = readl_poll_timeout(anatop + CCM_ANALOG_DRAM_PLL_CFG0,
				   r, r & CCM_ANALOG_PLL_LOCK, MSECOND);
	BUG_ON(error);

	writel(SRC_RCR_ENABLE | DDRC1_PHY_RESET | DDRC1_CORE_RST,
	       src + SRC_DDRC_RCR);

	/* Configure uMCTL2's registers */
	lpddr4_800mhz_cfg_umctl2(ddr);

	writel(SRC_RCR_ENABLE | DDRC1_PHY_RESET, src + SRC_DDRC_RCR);
	writel(SRC_RCR_ENABLE, src + SRC_DDRC_RCR);

	writel(0x00000000, ddr + DDRC_DBG1);
	writel(0x000000a8, ddr + DDRC_PWRCTL);
	/* writel(0x0000018a, DDRC_PWRCTL(0), )*/
	writel(0x00000000, ddr + DDRC_SWCTL);
	writel(0x00000001, MX8MQ_DDRC_DDR_SS_GPR0); 
	writel(0x00000010, ddr + DDRC_DFIMISC);

	/* Configure LPDDR4 PHY's registers */
	lpddr4_800M_cfg_phy(phy,
			    imem_1d, imem_1d_size,
			    dmem_1d, dmem_1d_size,
			    imem_2d, imem_2d_size,
			    dmem_2d, dmem_2d_size);

	writel(0x00000000, ddr + DDRC_RFSHCTL3);
	writel(0x00000000, ddr + DDRC_SWCTL);
	/*
	 * ------------------- 9 -------------------
	 * Set DFIMISC.dfi_init_start to 1
	 *  -----------------------------------------
	 */
	writel(0x00000030, ddr + DDRC_DFIMISC);
	writel(0x00000001, ddr + DDRC_SWCTL);

	/* wait DFISTAT.dfi_init_complete to 1 */
	error = readl_poll_timeout(ddr + DDRC_DFISTAT,
				   r, r & BIT(0), MSECOND);
	BUG_ON(error);

	writel(0x0000, ddr + DDRC_SWCTL);
	/* clear DFIMISC.dfi_init_complete_en */
	writel(0x00000010, ddr + DDRC_DFIMISC);
	writel(0x00000011, ddr + DDRC_DFIMISC);
	writel(0x00000088, ddr + DDRC_PWRCTL);
	/*
	 * set SWCTL.sw_done to enable quasi-dynamic register
	 * programming outside reset.
	 */
	writel(0x00000001, ddr + DDRC_SWCTL);

	/* wait SWSTAT.sw_done_ack to 1 */
	error = readl_poll_timeout(ddr + DDRC_SWSTAT,
				   r, r & BIT(0), MSECOND);
	BUG_ON(error);
	/* wait STAT.operating_mode([1:0] for ddr3) to normal state */
	error = readl_poll_timeout(ddr + DDRC_STAT,
				   r, (r & 0x3) == 1, MSECOND);
	BUG_ON(error);

	writel(0x00000088, ddr + DDRC_PWRCTL);

	/* enable port 0 */
	writel(0x00000001, ddr + DDRC_PCTRL_0);
	writel(0x00000000, ddr + DDRC_RFSHCTL3);

	writel(0x00000000, ddr + DDRC_SWCTL);

	lpddr4_100mhz_cfg_umctl2(ddr);
	lpddr4_25mhz_cfg_umctl2(ddr);

	writel(0x00000001, ddr + DDRC_SWCTL);

	/* wait SWSTAT.sw_done_ack to 1 */
	error = readl_poll_timeout(ddr + DDRC_SWSTAT,
				   r, r & BIT(0), MSECOND);
	BUG_ON(error);
	writel(0x00000000, ddr + DDRC_SWCTL);
}