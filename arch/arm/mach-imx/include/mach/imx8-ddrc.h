#ifndef __IMX8_DDRC_H__
#define __IMX8_DDRC_H__

#include <linux/compiler.h>

enum ddrc_phy_firmware_offset {
	DDRC_PHY_IMEM = 0x00050000U,
	DDRC_PHY_DMEM = 0x00054000U,
};

void ddrc_phy_load_firmware(void __iomem *,
			    enum ddrc_phy_firmware_offset,
			    const u16 *, size_t);

#define __DDRC_PHY_FIRMWARE_PAIR(f) \
	__ddr_phy_fw_##f##_start, __ddr_phy_fw_##f##_end

#define ___DDRC_PHY_FIRMWARE(s, e) \
	s, ((size_t)((ptrdiff_t)e - (ptrdiff_t)s))
#define __DDRC_PHY_FIRMWARE(f)	___DDRC_PHY_FIRMWARE(f)
#define DDRC_PHY_FIRMWARE(f) \
	__DDRC_PHY_FIRMWARE(__DDRC_PHY_FIRMWARE_PAIR(f))

#define ___DDRC_PHY_FIRMWARE_DECLARE(s, e)	\
	extern const u16 s[];			\
	extern const u16 e[]
#define __DDRC_PHY_FIRMWARE_DECLARE(f) ___DDRC_PHY_FIRMWARE_DECLARE(f)
#define DDRC_PHY_FIRMWARE_DECLARE(f) \
	__DDRC_PHY_FIRMWARE_DECLARE(__DDRC_PHY_FIRMWARE_PAIR(f))

#define DDRC_PHY_REG(x)	((x) * 4)

void ddrc_phy_wait_training_complete(void __iomem *phy);

#endif