#include <common.h>
#include <asm-generic/div64.h>
#include <poller.h>

int is_timeout_non_interruptible(uint64_t start_ns, uint64_t time_offset_ns)
{
	if ((int64_t)(start_ns + time_offset_ns - get_time_ns()) < 0)
		return 1;
	else
		return 0;
}
EXPORT_SYMBOL(is_timeout_non_interruptible);

#if defined(__PBL__)
/*
 * Poller infrastructure is not available in PBL, so we just define
 * is_timeout to be a synonym for is_timeout_non_interruptible
 */
int is_timeout(uint64_t start_ns, uint64_t time_offset_ns)
	__alias(is_timeout_non_interruptible);
#else
#include <poller.h>

int is_timeout(uint64_t start_ns, uint64_t time_offset_ns)
{

	if (time_offset_ns >= 100 * USECOND)
		poller_call();

	return is_timeout_non_interruptible(start_ns, time_offset_ns);
}
#endif
EXPORT_SYMBOL(is_timeout);

void ndelay(unsigned long nsecs)
{
	uint64_t start = get_time_ns();

	while(!is_timeout_non_interruptible(start, nsecs));
}
EXPORT_SYMBOL(ndelay);

void udelay(unsigned long usecs)
{
	uint64_t start = get_time_ns();

	while(!is_timeout(start, usecs * USECOND));
}
EXPORT_SYMBOL(udelay);

void mdelay(unsigned long msecs)
{
	uint64_t start = get_time_ns();

	while(!is_timeout(start, msecs * MSECOND));
}
EXPORT_SYMBOL(mdelay);

void mdelay_non_interruptible(unsigned long msecs)
{
	uint64_t start = get_time_ns();

	while (!is_timeout_non_interruptible(start, msecs * MSECOND))
		;
}
EXPORT_SYMBOL(mdelay_non_interruptible);

__weak uint64_t get_time_ns(void)
{
	return 0;
}
EXPORT_SYMBOL(get_time_ns);