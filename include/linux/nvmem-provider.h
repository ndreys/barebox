/*
 * nvmem framework provider.
 *
 * Copyright (C) 2015 Srinivas Kandagatla <srinivas.kandagatla@linaro.org>
 * Copyright (C) 2013 Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#ifndef _LINUX_NVMEM_PROVIDER_H
#define _LINUX_NVMEM_PROVIDER_H

struct device_d;
struct nvmem_device;
struct nvmem_cell_info;

struct nvmem_config {
	struct device_d		*dev;
	const char		*name;
	int			id;
	const struct nvmem_cell_info	*cells;
	int			ncells;
	bool			read_only;
};

#if IS_ENABLED(CONFIG_NVMEM)

struct nvmem_device *nvmem_register(const struct nvmem_config *cfg,
				    struct regmap *map);
int nvmem_unregister(struct nvmem_device *nvmem);

#else

static inline struct nvmem_device *nvmem_register(const struct nvmem_config *c,
						  struct regmap *map)
{
	return ERR_PTR(-ENOSYS);
}

static inline int nvmem_unregister(struct nvmem_device *nvmem)
{
	return -ENOSYS;
}

#endif /* CONFIG_NVMEM */

#endif  /* ifndef _LINUX_NVMEM_PROVIDER_H */
