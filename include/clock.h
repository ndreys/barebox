#ifndef CLOCK_H
#define CLOCK_H

#include <types.h>
#include <linux/time.h>

#define CLOCKSOURCE_MASK(bits) (uint64_t)((bits) < 64 ? ((1ULL<<(bits))-1) : -1)

struct clocksource {
	uint32_t	shift;
	uint32_t	mult;
	uint64_t	(*read)(void);
	uint64_t	cycle_last;
	uint64_t	mask;
	int		priority;
	int		(*init)(struct clocksource*);
};

static inline uint32_t cyc2ns(struct clocksource *cs, uint64_t cycles)
{
        uint64_t ret = cycles;
        ret = (ret * cs->mult) >> cs->shift;
        return ret;
}

int init_clock(struct clocksource *);

#ifndef get_time_ns
#define get_time_ns get_time_ns

uint64_t get_time_ns(void);
#endif

void clocks_calc_mult_shift(uint32_t *mult, uint32_t *shift, uint32_t from, uint32_t to, uint32_t maxsec);

uint32_t clocksource_hz2mult(uint32_t hz, uint32_t shift_constant);

#define SECOND ((uint64_t)(1000 * 1000 * 1000))
#define MSECOND ((uint64_t)(1000 * 1000))
#define USECOND ((uint64_t)(1000))

extern uint64_t time_beginning;

static inline int
is_timeout_non_interruptible(uint64_t start_ns, uint64_t time_offset_ns)
{
	if ((int64_t)(start_ns + time_offset_ns - get_time_ns()) < 0)
		return 1;
	else
		return 0;
}

#if defined(__PBL__)
/*
 * Poller infrastructure is not available in PBL, so we just define
 * is_timeout to be a synonym for is_timeout_non_interruptible
 */
static inline int is_timeout(uint64_t start_ns, uint64_t time_offset_ns)
	__alias(is_timeout_non_interruptible);
#else
#include <poller.h>

static inline int is_timeout(uint64_t start_ns, uint64_t time_offset_ns)
{

	if (time_offset_ns >= 100 * USECOND)
		poller_call();

	return is_timeout_non_interruptible(start_ns, time_offset_ns);
}
#endif

static inline void ndelay(unsigned long nsecs)
{
	uint64_t start = get_time_ns();

	while(!is_timeout_non_interruptible(start, nsecs));
}

static inline void udelay(unsigned long usecs)
{
	uint64_t start = get_time_ns();

	while(!is_timeout(start, usecs * USECOND));
}

static inline void mdelay(unsigned long msecs)
{
	uint64_t start = get_time_ns();

	while(!is_timeout(start, msecs * MSECOND));
}

static inline void mdelay_non_interruptible(unsigned long msecs)
{
	uint64_t start = get_time_ns();

	while (!is_timeout_non_interruptible(start, msecs * MSECOND))
		;
}

/*
 * Convenience wrapper to implement a typical polling loop with
 * timeout. returns 0 if the condition became true within the
 * timeout or -ETIMEDOUT otherwise
 */
#define wait_on_timeout(timeout, condition) \
({								\
	int __ret = 0;						\
	uint64_t __to_start = get_time_ns();			\
								\
	while (!(condition)) {					\
		if (is_timeout(__to_start, (timeout))) {	\
			__ret = -ETIMEDOUT;			\
			break;					\
		}						\
	}							\
	__ret;							\
})

#endif /* CLOCK_H */
