#include <common.h>
#include <debug_ll.h>

int printf(const char *fmt, ...)
{
	va_list args;
	uint i;
	char printbuffer[CFG_PBSIZE];

	va_start(args, fmt);
	i = vsprintf(printbuffer, fmt, args);
	va_end(args);

	puts_ll(printbuffer);

	return i;
}

int pr_print(int level, const char *fmt, ...)
{
	va_list args;
	uint i;
	char printbuffer[CFG_PBSIZE];

	va_start(args, fmt);
	i = vsprintf(printbuffer, fmt, args);
	va_end(args);

	puts_ll(printbuffer);

	return i;
}
#ifdef CONFIG_DEBUG_LL_INPUT
int ctrlc(void)
{
	if (tstc_ll() && getc_ll() == 3)
		return 1;
	return 0;
}
#else
int ctrlc(void)
{
	return 0;
}
#endif
void console_putc(unsigned int ch, char c)
{
	putc_ll(c);
}
