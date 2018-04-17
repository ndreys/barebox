#ifndef __ARM_MMU_H
#define __ARM_MMU_H

#ifdef CONFIG_MMU
void __mmu_cache_on(void);
void __mmu_cache_off(void);
void __mmu_cache_flush(void);
#else
static inline void __mmu_cache_on(void) {}
static inline void __mmu_cache_off(void) {}
static inline void __mmu_cache_flush(void) {}
#endif

static inline void set_ttbr(void *ttb)
{
	asm volatile ("mcr  p15,0,%0,c2,c0,0" : : "r"(ttb) /*:*/);
}

#define DOMAIN_MANAGER	3

static inline void set_domain(unsigned val)
{
	/* Set the Domain Access Control Register */
	asm volatile ("mcr  p15,0,%0,c3,c0,0" : : "r"(val) /*:*/);
}

static inline void
create_sections(uint32_t *ttb, unsigned long addr,
		unsigned long long size, unsigned int flags)
{
	unsigned long ttb_start = add >> 20;
	unsigned long ttb_end   = ttb_start + size >> 20;
	unsigned int i;

	for (i = ttb_start; i < ttb_end; i++, addr += SZ_1M)
		ttb[i] = addr | flags;
}


#endif /* __ARM_MMU_H */
