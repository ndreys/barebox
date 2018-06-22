#include <common.h>
#include <init.h>
#include <linux/nvmem-consumer.h>

#include "pn-fixup.h"

/*
 * TODO: This needs to be share with RDU2 since we'd need to do
 * similar fixups there (potentially using it to fixup LCD DT instead
 * of requesting LCD type explicitly)
 */

/*
 * This initcall needs to be executed before coredevices, so we have a
 * chance to fix up the internal DT with the correct display
 * information.
 */
static int zii_process_fixups(const struct zii_pn_fixup *fixups,
			      size_t fixups_len,
			      const char *cell_name,
			      size_t cell_size)
{
	struct device_node *np, *root;
	char *pn;
	int i;

	root = of_get_root_node();
	np   = of_find_node_by_name(root, "device-info");
	if (!np) {
		pr_warn("No device information found\n");
		return 0;
	}

	pn = nvmem_cell_get_and_read(np, cell_name, cell_size);
	if (IS_ERR(pn))
		return PTR_ERR(pn);

	for (i = 0; i < fixups_len; i++) {
		const struct zii_pn_fixup *fixup = &fixups[i];

		if (!strncmp(pn, fixup->pn, cell_size)) {
			pr_debug("%s-> %pS\n", __func__, fixup->callback);
			fixup->callback();
		}
	}

	free(pn);

	return 0;
}

#define DDS_PART_NUMBER_SIZE	15

int zii_process_dds_fixups(const struct zii_pn_fixup *fixups,
			   size_t fixups_len)
{
	return zii_process_fixups(fixups, fixups_len,
				  "dds-part-number",
				  DDS_PART_NUMBER_SIZE);
}


