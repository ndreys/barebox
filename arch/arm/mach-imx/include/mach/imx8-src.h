#ifndef __IMX8_SRC_H__
#define __IMX8_SRC_H__

#define SRC_DDRC_RCR		0x1000
#define DDRC1_PHY_RESET		BIT(2)
#define DDRC1_CORE_RST		BIT(1)
#define DDRC1_PRST		BIT(0)

#define SRC_DDRC2_RCR		0x1004
#define SRC_RCR_ENABLE  	BIT(31) | BIT(27) | BIT(26) | BIT(25) | BIT(24)

#endif