/*
 * Synopsys Designware PCIe host controller driver
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Jingoo Han <jg1.han@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */


#include <common.h>
#include <clock.h>
#include <malloc.h>
#include <io.h>
#include <init.h>
#include <asm/mmu.h>

#include <linux/clk.h>
#include <linux/kernel.h>
#include <of_address.h>
#include <of_pci.h>
#include <linux/pci.h>
#include <linux/phy/phy.h>
#include <linux/reset.h>
#include <linux/sizes.h>

#include "pcie-designware.h"

static unsigned long global_io_offset;

int dw_pcie_read(void __iomem *addr, int size, u32 *val)
{
	if ((uintptr_t)addr & (size - 1)) {
		*val = 0;
		return PCIBIOS_BAD_REGISTER_NUMBER;
	}

	if (size == 4)
		*val = readl(addr);
	else if (size == 2)
		*val = readw(addr);
	else if (size == 1)
		*val = readb(addr);
	else {
		*val = 0;
		return PCIBIOS_BAD_REGISTER_NUMBER;
	}

	return PCIBIOS_SUCCESSFUL;
}

int dw_pcie_write(void __iomem *addr, int size, u32 val)
{
	if ((uintptr_t)addr & (size - 1))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	if (size == 4)
		writel(val, addr);
	else if (size == 2)
		writew(val, addr);
	else if (size == 1)
		writeb(val, addr);
	else
		return PCIBIOS_BAD_REGISTER_NUMBER;

	return PCIBIOS_SUCCESSFUL;
}

u32 dw_pcie_readl_dbi(struct dw_pcie *pci, u32 reg)
{
	if (pci->ops->readl_dbi)
		return pci->ops->readl_dbi(pci, reg);

	return readl(pci->dbi_base + reg);
}

void dw_pcie_writel_dbi(struct dw_pcie *pci, u32 reg, u32 val)
{
	if (pci->ops->writel_dbi)
		pci->ops->writel_dbi(pci, reg, val);
	else
		writel(val, pci->dbi_base + reg);
}

#include <abort.h>

static int dw_pcie_rd_own_conf(struct pcie_port *pp, int where, int size,
			       u32 *val)
{
	struct dw_pcie *pci;

	if (pp->ops->rd_own_conf)
		return pp->ops->rd_own_conf(pp, where, size, val);

	pci = to_dw_pcie_from_pp(pp);
	return dw_pcie_read(pci->dbi_base + where, size, val);
}

static int dw_pcie_wr_own_conf(struct pcie_port *pp, int where, int size,
			       u32 val)
{
	struct dw_pcie *pci;

	if (pp->ops->wr_own_conf)
		return pp->ops->wr_own_conf(pp, where, size, val);

	pci = to_dw_pcie_from_pp(pp);
	return dw_pcie_write(pci->dbi_base + where, size, val);
}

static void dw_pcie_prog_outbound_atu(struct dw_pcie *pci, int index,
				      int type, u64 cpu_addr, u64 pci_addr,
				      u32 size)
{
	u32 retries, val;

	dw_pcie_writel_dbi(pci, PCIE_ATU_VIEWPORT,
			   PCIE_ATU_REGION_OUTBOUND | index);
	dw_pcie_writel_dbi(pci, PCIE_ATU_LOWER_BASE,
			   lower_32_bits(cpu_addr));
	dw_pcie_writel_dbi(pci, PCIE_ATU_UPPER_BASE,
			   upper_32_bits(cpu_addr));
	dw_pcie_writel_dbi(pci, PCIE_ATU_LIMIT,
			   lower_32_bits(cpu_addr + size - 1));
	dw_pcie_writel_dbi(pci, PCIE_ATU_LOWER_TARGET,
			   lower_32_bits(pci_addr));
	dw_pcie_writel_dbi(pci, PCIE_ATU_UPPER_TARGET,
			   upper_32_bits(pci_addr));
	dw_pcie_writel_dbi(pci, PCIE_ATU_CR1, type);
	dw_pcie_writel_dbi(pci, PCIE_ATU_CR2, PCIE_ATU_ENABLE);

	/*
	 * Make sure ATU enable takes effect before any subsequent config
	 * and I/O accesses.
	 */
	for (retries = 0; retries < LINK_WAIT_MAX_IATU_RETRIES; retries++) {
		val = dw_pcie_readl_dbi(pci, PCIE_ATU_CR2);
		if (val == PCIE_ATU_ENABLE)
			return;

		udelay(LINK_WAIT_IATU_MAX);
	}
	dev_err(pci->dev, "iATU is not being enabled\n");
}

int dw_pcie_wait_for_link(struct dw_pcie *pci)
{
	int retries;

	/* Check if the link is up or not */
	for (retries = 0; retries < LINK_WAIT_MAX_RETRIES; retries++) {
		if (dw_pcie_link_up(pci)) {
			dev_info(pci->dev, "Link up\n");
			return 0;
		}
		udelay(LINK_WAIT_USLEEP_MAX);
	}

	dev_err(pci->dev, "Phy link never came up\n");

	return -ETIMEDOUT;
}

int dw_pcie_link_up(struct dw_pcie *pci)
{
	u32 val;

	if (pci->ops->link_up)
		return pci->ops->link_up(pci);

	val = readl(pci->dbi_base + PCIE_PHY_DEBUG_R1);
	return ((val & PCIE_PHY_DEBUG_R1_LINK_UP) &&
		!(val & PCIE_PHY_DEBUG_R1_LINK_IN_TRAINING));
}

static inline struct pcie_port *host_to_pcie(struct pci_controller *host)
{
	return container_of(host, struct pcie_port, pci);
}

static void dw_pcie_set_local_bus_nr(struct pci_controller *host, int busno)
{
	struct pcie_port *pp = host_to_pcie(host);

	pp->root_bus_nr = busno;
}

static struct pci_ops dw_pcie_ops;

int __init dw_pcie_host_init(struct pcie_port *pp)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);
	struct device_d *dev = pci->dev;
	struct device_node *np = dev->device_node;
	struct of_pci_range range;
	struct of_pci_range_parser parser;
	struct resource *cfg_res;
	u32 na, ns;
	const __be32 *addrp;
	int index, ret;

	/* Find the address cell size and the number of cells in order to get
	 * the untranslated address.
	 */
	of_property_read_u32(np, "#address-cells", &na);
	ns = of_n_size_cells(np);

	cfg_res = dev_get_resource_by_name(dev, IORESOURCE_MEM, "config");
	if (cfg_res) {
		pp->cfg0_size = resource_size(cfg_res)/2;
		pp->cfg1_size = resource_size(cfg_res)/2;
		pp->cfg0_base = cfg_res->start;
		pp->cfg1_base = cfg_res->start + pp->cfg0_size;

		/* Find the untranslated configuration space address */
		index = of_property_match_string(np, "reg-names", "config");
		addrp = of_get_address(np, index, NULL, NULL);
		pp->cfg0_mod_base = of_read_number(addrp, ns);
		pp->cfg1_mod_base = pp->cfg0_mod_base + pp->cfg0_size;
	} else {
		dev_err(dev, "missing *config* reg space\n");
	}

	if (of_pci_range_parser_init(&parser, np)) {
		dev_err(dev, "missing ranges property\n");
		return -EINVAL;
	}

	/* Get the I/O and memory ranges from DT */
	for_each_of_pci_range(&parser, &range) {
		unsigned long restype = range.flags & IORESOURCE_TYPE_BITS;

		if (restype == IORESOURCE_IO) {
			of_pci_range_to_resource(&range, np, &pp->io);
			pp->io.name = "I/O";
			pp->io.start = range.pci_addr + global_io_offset;
			pp->io.end =  range.pci_addr + range.size + global_io_offset - 1;
			pp->io_size = resource_size(&pp->io);
			pp->io_bus_addr = range.pci_addr;
			pp->io_base = range.cpu_addr;

			/* Find the untranslated IO space address */
			pp->io_mod_base = of_read_number(parser.range -
							 parser.np + na, ns);
		}
		if (restype == IORESOURCE_MEM) {
			of_pci_range_to_resource(&range, np, &pp->mem);
			pp->mem.name = "MEM";
			pp->mem_size = resource_size(&pp->mem);
			pp->mem_bus_addr = range.pci_addr;

			/* Find the untranslated MEM space address */
			pp->mem_mod_base = of_read_number(parser.range -
							  parser.np + na, ns);
		}
		if (restype == 0) {
			of_pci_range_to_resource(&range, np, &pp->cfg);
			pp->cfg0_size = resource_size(&pp->cfg)/2;
			pp->cfg1_size = resource_size(&pp->cfg)/2;
			pp->cfg0_base = pp->cfg.start;
			pp->cfg1_base = pp->cfg.start + pp->cfg0_size;

			/* Find the untranslated configuration space address */
			pp->cfg0_mod_base = of_read_number(parser.range -
							   parser.np + na, ns);
			pp->cfg1_mod_base = pp->cfg0_mod_base +
					    pp->cfg0_size;
		}
	}

	if (!pci->dbi_base)
		pci->dbi_base = (void __force *)pp->cfg.start;

	pp->mem_base = pp->mem.start;

	if (!pp->va_cfg0_base)
		pp->va_cfg0_base = (void __force *)(u32)pp->cfg0_base;

	if (!pp->va_cfg1_base)
		pp->va_cfg1_base = (void __force *)(u32)pp->cfg1_base;

	ret = of_property_read_u32(np, "num-lanes", &pci->lanes);
	if (ret)
		pci->lanes = 0;

	ret = of_property_read_u32(np, "num-viewport", &pci->num_viewport);
	if (ret)
		pci->num_viewport = 2;

	if (pp->ops->host_init)
		pp->ops->host_init(pp);

	pp->pci.parent = dev;
	pp->pci.pci_ops = &dw_pcie_ops;
	pp->pci.set_busno = dw_pcie_set_local_bus_nr;
	pp->pci.mem_resource = &pp->mem;
	pp->pci.io_resource = &pp->io;

	register_pci_controller(&pp->pci);

	return 0;
}

static int dw_pcie_rd_other_conf(struct pcie_port *pp, struct pci_bus *bus,
		u32 devfn, int where, int size, u32 *val)
{
	int ret, type;
	u32 address, busdev, cfg_size;
	u64 cpu_addr;
	void __iomem *va_cfg_base;
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);

	if (pp->ops->rd_other_conf)
		return pp->ops->rd_other_conf(pp, bus, devfn,
					      where, size, val);

	busdev = PCIE_ATU_BUS(bus->number) | PCIE_ATU_DEV(PCI_SLOT(devfn)) |
		PCIE_ATU_FUNC(PCI_FUNC(devfn));
	address = where & ~0x3;

	if (bus->primary == pp->root_bus_nr) {
		type = PCIE_ATU_TYPE_CFG0;
		cpu_addr = pp->cfg0_mod_base;
		cfg_size = pp->cfg0_size;
		va_cfg_base = pp->va_cfg0_base;
	} else {
		type = PCIE_ATU_TYPE_CFG1;
		cpu_addr = pp->cfg1_mod_base;
		cfg_size = pp->cfg1_size;
		va_cfg_base = pp->va_cfg1_base;
	}

	dw_pcie_prog_outbound_atu(pci, PCIE_ATU_REGION_INDEX1,
				  type, cpu_addr,
				  busdev, cfg_size);
	ret = dw_pcie_read(va_cfg_base + where, size, val);
	if (pci->num_viewport <= 2)
		dw_pcie_prog_outbound_atu(pci, PCIE_ATU_REGION_INDEX1,
					  PCIE_ATU_TYPE_IO, pp->io_mod_base,
					  pp->io_bus_addr, pp->io_size);

	return ret;
}

static int dw_pcie_wr_other_conf(struct pcie_port *pp, struct pci_bus *bus,
		u32 devfn, int where, int size, u32 val)
{
	int ret, type;
	u32 busdev, cfg_size;
	u64 cpu_addr;
	void __iomem *va_cfg_base;
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);

	if (pp->ops->wr_other_conf)
		return pp->ops->wr_other_conf(pp, bus, devfn,
					      where, size, val);

	busdev = PCIE_ATU_BUS(bus->number) | PCIE_ATU_DEV(PCI_SLOT(devfn)) |
		 PCIE_ATU_FUNC(PCI_FUNC(devfn));

	if (bus->primary == pp->root_bus_nr) {
		type = PCIE_ATU_TYPE_CFG0;
		cpu_addr = pp->cfg0_mod_base;
		cfg_size = pp->cfg0_size;
		va_cfg_base = pp->va_cfg0_base;
	} else {
		type = PCIE_ATU_TYPE_CFG1;
		cpu_addr = pp->cfg1_mod_base;
		cfg_size = pp->cfg1_size;
		va_cfg_base = pp->va_cfg1_base;
	}

	dw_pcie_prog_outbound_atu(pci, PCIE_ATU_REGION_INDEX1,
				  type, cpu_addr,
				  busdev, cfg_size);
	ret = dw_pcie_write(va_cfg_base + where, size, val);
	if (pci->num_viewport <= 2)
		dw_pcie_prog_outbound_atu(pci, PCIE_ATU_REGION_INDEX1,
					  PCIE_ATU_TYPE_IO, pp->io_mod_base,
					  pp->io_bus_addr, pp->io_size);

	return ret;
}

static int dw_pcie_valid_device(struct pcie_port *pp, struct pci_bus *bus,
				int dev)
{
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);

	/* If there is no link, then there is no device */
	if (bus->number != pp->root_bus_nr) {
		if (!dw_pcie_link_up(pci))
			return 0;
	}

	/* access only one slot on each root port */
	if (bus->number == pp->root_bus_nr && dev > 0)
		return 0;

	/*
	 * do not read more than one device on the bus directly attached
	 * to RC's (Virtual Bridge's) DS side.
	 */
	if (bus->primary == pp->root_bus_nr && dev > 0)
		return 0;

	return 1;
}

static int dw_pcie_rd_conf(struct pci_bus *bus, u32 devfn, int where,
			int size, u32 *val)
{
	struct pcie_port *pp = host_to_pcie(bus->host);
	int ret;

	*val = 0xffffffff;

	if (!dw_pcie_valid_device(pp, bus, PCI_SLOT(devfn)))
		return PCIBIOS_DEVICE_NOT_FOUND;

	data_abort_mask();

	if (bus->number == pp->root_bus_nr)
		ret = dw_pcie_rd_own_conf(pp, where, size, val);
	else
		ret = dw_pcie_rd_other_conf(pp, bus, devfn,
					    where, size, val);

	if (data_abort_unmask())
		return PCIBIOS_DEVICE_NOT_FOUND;

	return ret;
}

static int dw_pcie_wr_conf(struct pci_bus *bus, u32 devfn,
			int where, int size, u32 val)
{
	struct pcie_port *pp = host_to_pcie(bus->host);
	int ret;

	if (!dw_pcie_valid_device(pp, bus, PCI_SLOT(devfn)))
		return PCIBIOS_DEVICE_NOT_FOUND;

	data_abort_mask();

	if (bus->number == pp->root_bus_nr)
		ret = dw_pcie_wr_own_conf(pp, where, size, val);
	else
		ret = dw_pcie_wr_other_conf(pp, bus, devfn,
					    where, size, val);

	if (data_abort_unmask())
		return PCIBIOS_DEVICE_NOT_FOUND;

	return ret;
}

static int dw_pcie_res_start(struct pci_bus *bus, resource_size_t res_addr)
{
	return res_addr;
}

static struct pci_ops dw_pcie_ops = {
	.read = dw_pcie_rd_conf,
	.write = dw_pcie_wr_conf,
	.res_start = dw_pcie_res_start,
};

void dw_pcie_setup_rc(struct pcie_port *pp)
{
	u32 val;
	struct dw_pcie *pci = to_dw_pcie_from_pp(pp);

	/* set the number of lanes */
	val = dw_pcie_readl_dbi(pci, PCIE_PORT_LINK_CONTROL);
	val &= ~PORT_LINK_MODE_MASK;
	switch (pci->lanes) {
	case 1:
		val |= PORT_LINK_MODE_1_LANES;
		break;
	case 2:
		val |= PORT_LINK_MODE_2_LANES;
		break;
	case 4:
		val |= PORT_LINK_MODE_4_LANES;
		break;
       default:
               dev_err(pci->dev, "num-lanes %u: invalid value\n", pci->lanes);
               return;
	}
	dw_pcie_writel_dbi(pci, PCIE_PORT_LINK_CONTROL, val);

	/* set link width speed control register */
	val = dw_pcie_readl_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL);
	val &= ~PORT_LOGIC_LINK_WIDTH_MASK;
	switch (pci->lanes) {
	case 1:
		val |= PORT_LOGIC_LINK_WIDTH_1_LANES;
		break;
	case 2:
		val |= PORT_LOGIC_LINK_WIDTH_2_LANES;
		break;
	case 4:
		val |= PORT_LOGIC_LINK_WIDTH_4_LANES;
		break;
	}
	dw_pcie_writel_dbi(pci, PCIE_LINK_WIDTH_SPEED_CONTROL, val);

	/* setup RC BARs */
	dw_pcie_writel_dbi(pci, PCI_BASE_ADDRESS_0, 0x00000004);
	dw_pcie_writel_dbi(pci, PCI_BASE_ADDRESS_1, 0x00000000);

	/* setup bus numbers */
	val = dw_pcie_readl_dbi(pci, PCI_PRIMARY_BUS);
	val &= 0xff000000;
	val |= 0x00010100;
	dw_pcie_writel_dbi(pci, PCI_PRIMARY_BUS, val);

	/* setup command register */
	val = dw_pcie_readl_dbi(pci, PCI_COMMAND);
	val &= 0xffff0000;
	val |= PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
		PCI_COMMAND_MASTER | PCI_COMMAND_SERR;
	dw_pcie_writel_dbi(pci, PCI_COMMAND, val);

       /*
        * If the platform provides ->rd_other_conf, it means the platform
        * uses its own address translation component rather than ATU, so
        * we should not program the ATU here.
        */
	if (!pp->ops->rd_other_conf) {
		dw_pcie_prog_outbound_atu(pci, PCIE_ATU_REGION_INDEX0,
					  PCIE_ATU_TYPE_MEM, pp->mem_mod_base,
					  pp->mem_bus_addr, pp->mem_size);
		if (pci->num_viewport <= 2)
			dw_pcie_prog_outbound_atu(pci, PCIE_ATU_REGION_INDEX2,
						  PCIE_ATU_TYPE_IO, pp->io_base,
						  pp->io_bus_addr, pp->io_size);
	}

	dw_pcie_wr_own_conf(pp, PCI_BASE_ADDRESS_0, 4, 0);

	/* program correct class for RC */
	dw_pcie_wr_own_conf(pp, PCI_CLASS_DEVICE, 2, PCI_CLASS_BRIDGE_PCI);

	dw_pcie_rd_own_conf(pp, PCIE_LINK_WIDTH_SPEED_CONTROL, 4, &val);
	val |= PORT_LOGIC_SPEED_CHANGE;
	dw_pcie_wr_own_conf(pp, PCIE_LINK_WIDTH_SPEED_CONTROL, 4, val);
}

MODULE_AUTHOR("Jingoo Han <jg1.han@samsung.com>");
MODULE_DESCRIPTION("Designware PCIe host controller driver");
MODULE_LICENSE("GPL v2");
