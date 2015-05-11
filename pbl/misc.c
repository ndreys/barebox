#include <common.h>
#include <init.h>
#include <memtest.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <debug_ll.h>

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

#ifdef CONFIG_MEMORY_VALIDATION

static mem_test_handler_t mem_test_handler = NULL;

void validate_memory_range(const char *message,
			   resource_size_t start,
			   resource_size_t end)
{
	int ret;
	mem_test_handler_t *__mem_test_handler_p = get_true_address(&mem_test_handler);
	mem_test_handler_t __mem_test_handler    = get_true_address(*__mem_test_handler_p);

	if (__mem_test_handler) {
		message = get_true_address(message);

		puts_ll(message);
		putc_ll(' ');
		putc_ll('[');
		puthex_ll(start);
		putc_ll(' ');
		putc_ll('-');
		putc_ll(' ');
		puthex_ll(end);
		putc_ll(']');
		putc_ll('\r');
		putc_ll('\n');

		ret = __mem_test_handler(start, end);

		if (ret < 0)
			hang();
	}
}

void set_memory_validation_handler(mem_test_handler_t handler)
{
	mem_test_handler_t *__mem_test_handler_p = get_true_address(&mem_test_handler);

	*__mem_test_handler_p = handler;
}
#endif

void __noreturn panic(const char *fmt, ...)
{
	while(1);
}
