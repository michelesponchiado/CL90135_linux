/*
 * MPC8536 DS Board Setup
 *
 * Copyright 2008 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/delay.h>
#include <linux/seq_file.h>
#include <linux/interrupt.h>

#include <asm/of_platform.h>
#include <asm/system.h>
#include <asm/time.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>
#include <mm/mmu_decl.h>
#include <asm/prom.h>
#include <asm/udbg.h>
#include <asm/mpic.h>

#include <sysdev/fsl_soc.h>
#include <sysdev/fsl_pci.h>

void __init mpc8536_ds_pic_init(void)
{
	struct mpic *mpic;
	struct resource r;
	struct device_node *np;

	np = of_find_node_by_type(NULL, "open-pic");
	if (np == NULL) {
		printk(KERN_ERR "Could not find open-pic node\n");
		return;
	}

	if (of_address_to_resource(np, 0, &r)) {
		printk(KERN_ERR "Failed to map mpic register space\n");
		of_node_put(np);
		return;
	}

	mpic = mpic_alloc(np, r.start,
			  MPIC_PRIMARY | MPIC_WANTS_RESET |
			  MPIC_BIG_ENDIAN,
			0, 256, " OpenPIC  ");
	BUG_ON(mpic == NULL);
	of_node_put(np);

	mpic_init(mpic);
}

#ifdef CONFIG_PCI
#define MPC8536_LAW_REGS_OFFSET	0xc00
#define MPC8536_LAW_REGS_SIZE	0x200
#define MPC8536_LAWAR9		0x130
#define MPC8536_LAWAR10		0x150
#define MPC8536_LAWAR_TGT_PCIE3	0x00300000

static void __init mpc8536_ds_fixup_laws(void)
{
	void __iomem *local_access;

	/* Use Local Access Windows 9 and 10 for PCI Express */
	local_access = ioremap(get_immrbase() + MPC8536_LAW_REGS_OFFSET,
			       MPC8536_LAW_REGS_SIZE);
	out_be32(local_access + MPC8536_LAWAR9,
		 in_be32(local_access + MPC8536_LAWAR9) |
		 	 MPC8536_LAWAR_TGT_PCIE3);
	out_be32(local_access + MPC8536_LAWAR10,
		 in_be32(local_access + MPC8536_LAWAR10) |
		 	 MPC8536_LAWAR_TGT_PCIE3);
}
#endif

/*
 * Setup the architecture
 */
static void __init mpc8536_ds_setup_arch(void)
{
#ifdef CONFIG_PCI
	struct device_node *np;
#endif

	if (ppc_md.progress)
		ppc_md.progress("mpc8536_ds_setup_arch()", 0);

#ifdef CONFIG_PCI
	mpc8536_ds_fixup_laws();

	for_each_node_by_type(np, "pci") {
		if (of_device_is_compatible(np, "fsl,mpc8540-pci") ||
		    of_device_is_compatible(np, "fsl,mpc8548-pcie")) {
			struct resource rsrc;
			of_address_to_resource(np, 0, &rsrc);
			if ((rsrc.start & 0xfffff) == 0x8000)
				fsl_add_bridge(np, 1);
			else
				fsl_add_bridge(np, 0);
		}
	}

#endif

	printk("MPC8536 DS board from Freescale Semiconductor\n");
}

static struct of_device_id __initdata mpc8536_ds_ids[] = {
	{ .type = "soc", },
	{ .compatible = "soc", },
	{ .compatible = "simple-bus", },
	{},
};

static int __init mpc8536_ds_publish_devices(void)
{
	return of_platform_bus_probe(NULL, mpc8536_ds_ids, NULL);
}
machine_device_initcall(mpc8536_ds, mpc8536_ds_publish_devices);

/*
 * Called very early, device-tree isn't unflattened
 */
static int __init mpc8536_ds_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	return of_flat_dt_is_compatible(root, "fsl,mpc8536ds");
}

define_machine(mpc8536_ds) {
	.name			= "MPC8536 DS",
	.probe			= mpc8536_ds_probe,
	.setup_arch		= mpc8536_ds_setup_arch,
	.init_IRQ		= mpc8536_ds_pic_init,
#ifdef CONFIG_PCI
	.pcibios_fixup_bus	= fsl_pcibios_fixup_bus,
#endif
	.get_irq		= mpic_get_irq,
	.restart		= fsl_rstcr_restart,
	.calibrate_decr		= generic_calibrate_decr,
	.progress		= udbg_progress,
};
