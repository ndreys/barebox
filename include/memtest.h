#ifndef __MEMTEST_H
#define __MEMTEST_H

#include <linux/ioport.h>

struct mem_test_resource {
	struct resource *r;
	struct list_head list;
};

typedef int (*mem_test_handler_t)(resource_size_t _start, resource_size_t _end);

int mem_test(resource_size_t _start,
		resource_size_t _end, int bus_only);

#ifdef CONFIG_MEMORY_VALIDATION
void validate_memory_range(const char *message,
			   resource_size_t start,
			   resource_size_t end);
void set_memory_validation_handler(mem_test_handler_t handler);
#else
static inline void validate_memory_range(const char *message,
					 resource_size_t start,
					 resource_size_t end)
{
}

static inline void set_memory_validation_handler(mem_test_handler_t handler)
{
}
#endif

#endif /* __MEMTEST_H */
