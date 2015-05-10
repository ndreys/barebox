#include <common.h>
#include <init.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/ctype.h>

void __noreturn __hang(void)
{
	while (1);
}

static hang_handler_t hang_handler = __hang;

void __noreturn hang(void)
{
	hang_handler_t *__hand_handler_p = get_true_address(&hang_handler);
	hang_handler_t __hang_handler    = get_true_address(*__hand_handler_p);

	__hang_handler();
}

void set_hang_handler(hang_handler_t handler)
{
	hang_handler_t *__hang_handler_p = get_true_address(&hang_handler);

	*__hang_handler_p = handler;
}

void __noreturn panic(const char *fmt, ...)
{
	while(1);
}
