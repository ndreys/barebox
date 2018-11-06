#include <common.h>
#include <linux/ctype.h>

#include "kstrtox.h"

unsigned long long simple_strtoull(const char *cp, char **endp,
				   unsigned int base)
{
	unsigned long long result;
	unsigned int r;
	const char *s;

	s = _parse_integer_fixup_radix(cp, &base);
	r = _parse_integer(s, base, &result);
	if (r & KSTRTOX_OVERFLOW)
		return ULLONG_MAX;

	if (endp)
		*endp = (char *)s + r;

	return result;
}
EXPORT_SYMBOL(simple_strtoull);

unsigned long simple_strtoul(const char *cp, char **endp, unsigned int base)
{
	const unsigned long long result = simple_strtoull(cp, endp, base);

	return clamp_t(unsigned long long, result, 0, ULONG_MAX);
}
EXPORT_SYMBOL(simple_strtoul);

long long simple_strtoll(const char *cp, char **endp, unsigned int base)
{
	unsigned long long upper_boundary = LONG_MAX;
	unsigned long long result;
	bool negative = *cp == '-';

	if (negative) {
		upper_boundary++;
		cp++;
	}

	result = clamp_t(unsigned long long,
			 simple_strtoull(cp, endp, base),
			 0, upper_boundary);
	if (negative)
		return -result;

	return result;
}
EXPORT_SYMBOL(simple_strtoll);

long simple_strtol(const char *cp, char **endp, unsigned int base)
{
	long long result = simple_strtoll(cp, endp, base);

	return clamp_t(long long, result, LONG_MIN, LONG_MAX);
}
EXPORT_SYMBOL(simple_strtol);