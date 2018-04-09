#ifndef __MEMTEST_H
#define __MEMTEST_H

#include <linux/ioport.h>

struct mem_test_resource {
	struct resource *r;
	struct list_head list;
};

int mem_test_request_regions(struct list_head *list);
void mem_test_release_regions(struct list_head *list);
struct mem_test_resource *mem_test_biggest_region(struct list_head *list);

typedef void (* mem_test_report_failure_func) (const char *failure_description,
					       resource_size_t expected_value,
					       resource_size_t actual_value,
					       volatile resource_size_t *address);
int mem_test_bus_integrity(resource_size_t _start,
			   resource_size_t _end,
			   mem_test_report_failure_func report_failure);
int mem_test_moving_inversions(resource_size_t _start,
			       resource_size_t _end,
			       mem_test_report_failure_func report_failure);

#endif /* __MEMTEST_H */
