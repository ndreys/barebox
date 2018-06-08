#ifndef __IMX_ATF_H__
#define __IMX_ATF_H__

#include <asm/system.h>

#define MX8MQ_ATF_BL31_SIZE_LIMIT	SZ_64K
#define MX8MQ_ATF_BL31_BASE_ADDR	0x00910000
#define MX8MQ_ATF_BL33_BASE_ADDR	0x40200000

/**
 * imx8mq_atf_load_bl31 - Load ATF BL31 blob and transfer contol to it
 *
 * @name:	Name of the BL31 blob
 *
 * This macro:

 *     1. Copies built-in BL31 blob to an address i.MX8M's BL31
 *        expects to be placed
 *
 *     2. Sets up temporary stack pointer for EL2, which is execution
 *        level that BL31 will drop us off at after it completes its
 *        initialization routine
 *
 *     3. Transfers control to BL31
 *
 * NOTE: This has to be a macro in order delay the expansion of
 * get_builtin_firmware(), otherwise it'll incorrectly interpret
 * "name" literally
 *
 * NOTE: This function will do nothing if executed at any other EL
 * than EL3. This is done intentionally to support single entry point
 * initialization (see i.MX8M EVK for an example)
 *
 * NOTE: This function expects NXP's implementation of ATF that can be
 * found at:
 *     https://source.codeaurora.org/external/imx/imx-atf
 *
 * any other implementation may or may not work
 *
 */
#define imx8mq_atf_load_bl31(name)					\
	do {								\
		void __noreturn (*bl31)(void) =				\
			(void *)MX8MQ_ATF_BL31_BASE_ADDR;		\
		size_t bl31_size;					\
		const u8 *fw;						\
									\
		if (current_el() != 3)					\
			break;						\
									\
		get_builtin_firmware(name, &fw, &bl31_size);		\
		if (WARN_ON(bl31_size > MX8MQ_ATF_BL31_SIZE_LIMIT))	\
			break;						\
									\
		memcpy(bl31, fw, bl31_size);				\
		asm volatile("msr sp_el2, %0" : :			\
			     "r" (MX8MQ_ATF_BL33_BASE_ADDR - 16) :	\
			     "cc");					\
									\
		bl31();							\
	} while (0)

#endif