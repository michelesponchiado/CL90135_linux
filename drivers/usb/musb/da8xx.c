/*
 * Texas Instruments DA8xx "glue layer"
 *
 * Copyright (c) 2008-2009 MontaVista Software, Inc. <source@mvista.com>
 *
 * Based on the DaVinci "glue layer" code.
 * Copyright (C) 2005-2006 by Texas Instruments
 *
 * This file is part of the Inventra Controller Driver for Linux.
 *
 * The Inventra Controller Driver for Linux is free software; you
 * can redistribute it and/or modify it under the terms of the GNU
 * General Public License version 2 as published by the Free Software
 * Foundation.
 *
 * The Inventra Controller Driver for Linux is distributed in
 * the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
 * License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with The Inventra Controller Driver for Linux ; if not,
 * write to the Free Software Foundation, Inc., 59 Temple Place,
 * Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/init.h>
#include <linux/clk.h>
#include <linux/io.h>

#include <asm/arch/hardware.h>
#include <asm/arch/usb.h>

#include "musb_core.h"
#include "cppi41_dma.h"

/*
 * DA8xx specific definitions
 */

/* CPPI 4.1 queue manager registers */
#define QMGR_PEND0_REG		0x4090
#define QMGR_PEND1_REG		0x4094

#define DA8XX_TX_EP_MASK	0x1f		/* EP0 + 4 Tx EPs */
#define DA8XX_RX_EP_MASK	0x1e		/* 4 Rx EPs */

#define DA8XX_TX_INTR_MASK	(DA8XX_TX_EP_MASK << USB_INTR_TX_SHIFT)
#define DA8XX_RX_INTR_MASK	(DA8XX_RX_EP_MASK << USB_INTR_RX_SHIFT)

#define A_WAIT_BCON_TIMEOUT	1100		/* in ms */

#ifdef CONFIG_USB_TI_CPPI41_DMA

/*
 * CPPI 4.1 resources used for USB OTG controller module:
 *
 * USB   DMA  DMA  QMgr  Tx     Src
 *       Tx   Rx         QNum   Port
 * ---------------------------------
 * EP0   0    0    0     16,17  1
 * ---------------------------------
 * EP1   1    1    0     18,19  2
 * ---------------------------------
 * EP2   2    2    0     20,21  3
 * ---------------------------------
 * EP3   3    3    0     22,23  4
 * ---------------------------------
 */

static const u16 tx_comp_q[] = { 24, 25 };
static const u16 rx_comp_q[] = { 26, 27 };

const struct usb_cppi41_info usb_cppi41_info = {
	.dma_block	= 0,
	.ep_dma_ch	= { 0, 1, 2, 3 },
	.q_mgr		= 0,
	.num_tx_comp_q	= 2,
	.num_rx_comp_q	= 2,
	.tx_comp_q	= tx_comp_q,
	.rx_comp_q	= rx_comp_q
};

#endif /* CONFIG_USB_TI_CPPI41_DMA */

/*
 * REVISIT (PM): we should be able to keep the PHY in low power mode most
 * of the time (24 MHz oscillator and PLL off, etc.) by setting POWER.D0
 * and, when in host mode, autosuspending idle root ports... PHY_PLLON
 * (overriding SUSPENDM?) then likely needs to stay off.
 */

static inline void phy_on(void)
{
	u32 cfgchip2;

	/*
	 * Start the on-chip PHY and its PLL.
	 */
	cfgchip2 = davinci_cfg_readl(DA8XX_CFGCHIP(2));
	cfgchip2 &= ~(CFGCHIP2_RESET | CFGCHIP2_PHYPWRDN | CFGCHIP2_OTGPWRDN);
	cfgchip2 |= CFGCHIP2_PHY_PLLON;
	davinci_cfg_writel(cfgchip2, DA8XX_CFGCHIP(2));

	pr_info("Waiting for USB PHY clock good...\n");
	while (!(davinci_cfg_readl(DA8XX_CFGCHIP(2)) & CFGCHIP2_PHYCLKGD))
		cpu_relax();
}

static inline void phy_off(void)
{
	u32 cfgchip2;

	/*
	 * Power down the on-chip PHY.
	 */
	cfgchip2 = davinci_cfg_readl(DA8XX_CFGCHIP(2));
	cfgchip2 |= CFGCHIP2_PHYPWRDN | CFGCHIP2_OTGPWRDN;
	davinci_cfg_writel(cfgchip2, DA8XX_CFGCHIP(2));
}

/*
 * Because we don't set CTRL.UINT, it's "important" to:
 *	- not read/write INTRUSB/INTRUSBE (except during
 *	  initial setup, as a workaround);
 *	- use INTSET/INTCLR instead.
 */

/**
 * musb_platform_enable - enable interrupts
 */
void musb_platform_enable(struct musb *musb)
{
	void __iomem *reg_base = musb->ctrl_base;
	u32 mask;

	/* Workaround: setup IRQs through both register sets. */
	mask = ((musb->epmask & DA8XX_TX_EP_MASK) << USB_INTR_TX_SHIFT) |
	       ((musb->epmask & DA8XX_RX_EP_MASK) << USB_INTR_RX_SHIFT) |
	       USB_INTR_USB_MASK;
	musb_writel(reg_base, USB_INTR_MASK_SET_REG, mask);

	/* Force the DRVVBUS IRQ so we can start polling for ID change. */
	if (is_otg_enabled(musb))
		musb_writel(reg_base, USB_INTR_SRC_SET_REG,
			    USB_INTR_DRVVBUS << USB_INTR_USB_SHIFT);
}

/**
 * musb_platform_disable - disable HDRC and flush interrupts
 */
void musb_platform_disable(struct musb *musb)
{
	void __iomem *reg_base = musb->ctrl_base;

	musb_writel(reg_base, USB_INTR_MASK_CLEAR_REG, USB_INTR_USB_MASK |
		    DA8XX_TX_INTR_MASK | DA8XX_RX_INTR_MASK);
	musb_writeb(musb->mregs, MUSB_DEVCTL, 0);
	musb_writel(reg_base, USB_END_OF_INTR_REG, 0);
}

#ifdef CONFIG_USB_MUSB_HDRC_HCD
#define portstate(stmt) 	stmt
#else
#define portstate(stmt)
#endif

static void da8xx_set_vbus(struct musb *musb, int is_on)
{
	WARN_ON(is_on && is_peripheral_active(musb));
}

#define	POLL_SECONDS	2

static struct timer_list otg_workaround;

static void otg_timer(unsigned long _musb)
{
	struct musb		*musb = (void *)_musb;
	void __iomem		*mregs = musb->mregs;
	u8			devctl;
	unsigned long		flags;

	/*
	 * We poll because DaVinci's won't expose several OTG-critical
	 * status change events (from the transceiver) otherwise.
	 */
	devctl = musb_readb(mregs, MUSB_DEVCTL);
	DBG(7, "Poll devctl %02x (%s)\n", devctl, otg_state_string(musb));

	spin_lock_irqsave(&musb->lock, flags);
	switch (musb->xceiv.state) {
	case OTG_STATE_A_WAIT_BCON:
		devctl &= ~MUSB_DEVCTL_SESSION;
		musb_writeb(musb->mregs, MUSB_DEVCTL, devctl);

		devctl = musb_readb(musb->mregs, MUSB_DEVCTL);
		if (devctl & MUSB_DEVCTL_BDEVICE) {
			musb->xceiv.state = OTG_STATE_B_IDLE;
			MUSB_DEV_MODE(musb);
		} else {
			musb->xceiv.state = OTG_STATE_A_IDLE;
			MUSB_HST_MODE(musb);
		}
		break;
	case OTG_STATE_A_WAIT_VFALL:
		/*
		 * Wait till VBUS falls below SessionEnd (~0.2 V); the 1.3
		 * RTL seems to mis-handle session "start" otherwise (or in
		 * our case "recover"), in routine "VBUS was valid by the time
		 * VBUSERR got reported during enumeration" cases.
		 */
		if (devctl & MUSB_DEVCTL_VBUS) {
			mod_timer(&otg_workaround, jiffies + POLL_SECONDS * HZ);
			break;
		}
		musb->xceiv.state = OTG_STATE_A_WAIT_VRISE;
		musb_writel(musb->ctrl_base, USB_INTR_SRC_SET_REG,
			    MUSB_INTR_VBUSERROR << USB_INTR_USB_SHIFT);
		break;
	case OTG_STATE_B_IDLE:
		if (!is_peripheral_enabled(musb))
			break;

		/*
		 * There's no ID-changed IRQ, so we have no good way to tell
		 * when to switch to the A-Default state machine (by setting
		 * the DEVCTL.Session bit).
		 *
		 * Workaround:  whenever we're in B_IDLE, try setting the
		 * session flag every few seconds.  If it works, ID was
		 * grounded and we're now in the A-Default state machine.
		 *
		 * NOTE: setting the session flag is _supposed_ to trigger
		 * SRP but clearly it doesn't.
		 */
		musb_writeb(mregs, MUSB_DEVCTL, devctl | MUSB_DEVCTL_SESSION);
		devctl = musb_readb(mregs, MUSB_DEVCTL);
		if (devctl & MUSB_DEVCTL_BDEVICE)
			mod_timer(&otg_workaround, jiffies + POLL_SECONDS * HZ);
		else
			musb->xceiv.state = OTG_STATE_A_IDLE;
		break;
	default:
		break;
	}
	spin_unlock_irqrestore(&musb->lock, flags);
}

void musb_platform_try_idle(struct musb *musb, unsigned long timeout)
{
	static unsigned long last_timer;

	if (!is_otg_enabled(musb))
		return;

	if (timeout == 0)
		timeout = jiffies + msecs_to_jiffies(3);

	/* Never idle if active, or when VBUS timeout is not set as host */
	if (musb->is_active || (musb->a_wait_bcon == 0 &&
				musb->xceiv.state == OTG_STATE_A_WAIT_BCON)) {
		DBG(4, "%s active, deleting timer\n", otg_state_string(musb));
		del_timer(&otg_workaround);
		last_timer = jiffies;
		return;
	}

	if (time_after(last_timer, timeout) && timer_pending(&otg_workaround)) {
		DBG(4, "Longer idle timer already pending, ignoring...\n");
		return;
	}
	last_timer = timeout;

	DBG(4, "%s inactive, starting idle timer for %u ms\n",
	    otg_state_string(musb), jiffies_to_msecs(timeout - jiffies));
	mod_timer(&otg_workaround, timeout);
}

static irqreturn_t da8xx_interrupt(int irq, void *hci, struct pt_regs *regs)
{
	struct musb  *musb = hci;
	void __iomem *reg_base = musb->ctrl_base;
	unsigned long flags;
	irqreturn_t ret = IRQ_NONE;
	u32 status, pend0 = 0;

	spin_lock_irqsave(&musb->lock, flags);

	/*
	 * NOTE: DA8xx shadows the Mentor IRQs.  Don't manage them through
	 * the Mentor registers (except for setup), use the TI ones and EOI.
	 */

	/*
	 * CPPI 4.1 interrupts share the same IRQ and the EOI register but
	 * don't get reflected in the interrupt source/mask registers.
	 */
	if (is_cppi41_enabled()) {
		/*
		 * Check for the interrupts from Tx/Rx completion queues; they
		 * are level-triggered and will stay asserted until the queues
		 * are emptied.  We're using the queue pending register 0 as a
		 * substitute for the interrupt status register and reading it
		 * directly for speed.
		 */
		pend0 = musb_readl(reg_base, QMGR_PEND0_REG);
		if (pend0 & (0xf << 24)) {		/* queues 24 to 27 */
			u32 tx = (pend0 >> 24) & 0x3;
			u32 rx = (pend0 >> 26) & 0x3;

			DBG(4, "CPPI 4.1 IRQ: Tx %x, Rx %x\n", tx, rx);
			cppi41_completion(musb, rx, tx);
			ret = IRQ_HANDLED;
		}
	}

	/* Acknowledge and handle non-CPPI interrupts */
	status = musb_readl(reg_base, USB_INTR_SRC_MASKED_REG);
	if (!status)
		goto eoi;

	musb_writel(reg_base, USB_INTR_SRC_CLEAR_REG, status);
	DBG(4, "USB IRQ %08x\n", status);

	musb->int_rx = (status & DA8XX_RX_INTR_MASK) >> USB_INTR_RX_SHIFT;
	musb->int_tx = (status & DA8XX_TX_INTR_MASK) >> USB_INTR_TX_SHIFT;
	musb->int_usb = (status & USB_INTR_USB_MASK) >> USB_INTR_USB_SHIFT;
	musb->int_regs = regs;

	/*
	 * DRVVBUS IRQs are the only proxy we have (a very poor one!) for
	 * DA8xx's missing ID change IRQ.  We need an ID change IRQ to
	 * switch appropriately between halves of the OTG state machine.
	 * Managing DEVCTL.Session per Mentor docs requires that we know its
	 * value but DEVCTL.BDevice is invalid without DEVCTL.Session set.
	 * Also, DRVVBUS pulses for SRP (but not at 5 V)...
	 */
	if (status & (USB_INTR_DRVVBUS << USB_INTR_USB_SHIFT)) {
		int drvvbus = musb_readl(reg_base, USB_STAT_REG);
		void __iomem *mregs = musb->mregs;
		u8 devctl = musb_readb(mregs, MUSB_DEVCTL);
		int err;

		err = is_host_enabled(musb) && (musb->int_usb &
						MUSB_INTR_VBUSERROR);
		if (err) {
			/*
			 * The Mentor core doesn't debounce VBUS as needed
			 * to cope with device connect current spikes. This
			 * means it's not uncommon for bus-powered devices
			 * to get VBUS errors during enumeration.
			 *
			 * This is a workaround, but newer RTL from Mentor
			 * seems to allow a better one: "re"-starting sessions
			 * without waiting for VBUS to stop registering in
			 * devctl.
			 */
			musb->int_usb &= ~MUSB_INTR_VBUSERROR;
			musb->xceiv.state = OTG_STATE_A_WAIT_VFALL;
			mod_timer(&otg_workaround, jiffies + POLL_SECONDS * HZ);
			WARN("VBUS error workaround (delay coming)\n");
		} else if (is_host_enabled(musb) && drvvbus) {
			musb->is_active = 1;
			MUSB_HST_MODE(musb);
			musb->xceiv.default_a = 1;
			musb->xceiv.state = OTG_STATE_A_WAIT_VRISE;
			portstate(musb->port1_status |= USB_PORT_STAT_POWER);
			del_timer(&otg_workaround);
		} else {
			musb->is_active = 0;
			MUSB_DEV_MODE(musb);
			musb->xceiv.default_a = 0;
			musb->xceiv.state = OTG_STATE_B_IDLE;
			portstate(musb->port1_status &= ~USB_PORT_STAT_POWER);
		}

		DBG(2, "VBUS %s (%s)%s, devctl %02x\n",
				drvvbus ? "on" : "off",
				otg_state_string(musb),
				err ? " ERROR" : "",
				devctl);
		ret = IRQ_HANDLED;
	}

	if (musb->int_tx || musb->int_rx || musb->int_usb)
		ret |= musb_interrupt(musb);

	musb->int_regs = NULL;

 eoi:
	/* EOI needs to be written for the IRQ to be re-asserted. */
	if (ret == IRQ_HANDLED || status)
		musb_writel(reg_base, USB_END_OF_INTR_REG, 0);

	/* Poll for ID change */
	if (is_otg_enabled(musb) && musb->xceiv.state == OTG_STATE_B_IDLE)
		mod_timer(&otg_workaround, jiffies + POLL_SECONDS * HZ);

	spin_unlock_irqrestore(&musb->lock, flags);
#if 0
// Michele Sponchiado 
//	if (ret != IRQ_HANDLED) {
	if ((ret & IRQ_HANDLED)==0) {
		if (status)
			/*
			 * We sometimes get unhandled IRQs in the peripheral
			 * mode from EP0 and SOF...
			 */
			ERR("Unhandled USB IRQ %08x\n", status);
		else if (printk_ratelimit())
			/*
			 * We've seen series of spurious interrupts in the
			 * peripheral mode after USB reset and then after some
			 * time a real interrupt storm starting...
			 */
			ERR("Spurious IRQ, CPPI 4.1 status %08x\n" , pend0);
	}
#endif	
	return ret;
}

void musb_platform_set_mode(struct musb *musb, u8 musb_mode)
{
	u32 cfgchip2 = davinci_cfg_readl(DA8XX_CFGCHIP(2));

	cfgchip2 &= ~CFGCHIP2_OTGMODE;
	switch (musb_mode) {
#ifdef	CONFIG_USB_MUSB_HDRC_HCD
	case MUSB_HOST:		/* Force VBUS valid, ID = 0 */
		cfgchip2 |= CFGCHIP2_FORCE_HOST;
		break;
#endif
#ifdef	CONFIG_USB_GADGET_MUSB_HDRC
	case MUSB_PERIPHERAL:	/* Force VBUS valid, ID = 1 */
		cfgchip2 |= CFGCHIP2_FORCE_DEVICE;
		break;
#endif
#ifdef	CONFIG_USB_MUSB_OTG
	case MUSB_OTG:		/* Don't override the VBUS/ID comparators */
		cfgchip2 |= CFGCHIP2_NO_OVERRIDE;
		break;
#endif
	default:
		DBG(2, "Trying to set unsupported mode %u\n", musb_mode);
	}

	davinci_cfg_writel(cfgchip2, DA8XX_CFGCHIP(2));
}

int __init musb_platform_init(struct musb *musb)
{
	void __iomem *reg_base = musb->ctrl_base;
	u32 rev;

	musb->mregs += USB_MENTOR_CORE_OFFSET;

	musb->clock = clk_get(NULL, "USB20CLK");
	if (IS_ERR(musb->clock))
		return PTR_ERR(musb->clock);

	if (clk_enable(musb->clock) < 0)
		return -ENODEV;

	/* Returns zero if e.g. not clocked */
	rev = musb_readl(reg_base, USB_REVISION_REG);
	if (!rev)
		return -ENODEV;

	if (is_host_enabled(musb))
		setup_timer(&otg_workaround, otg_timer, (unsigned long) musb);

	musb->board_set_vbus = da8xx_set_vbus;

	/* Reset the controller */
	musb_writel(reg_base, USB_CTRL_REG, USB_SOFT_RESET_MASK);

	/* Start the on-chip PHY and its PLL. */
	phy_on();

	msleep(5);

	/* NOTE: IRQs are in mixed mode, not bypass to pure MUSB */
	pr_debug("DA8xx OTG revision %08x, PHY %03x, control %02x\n",
		 rev, davinci_cfg_readl(DA8XX_CFGCHIP(2)),
		 musb_readb(reg_base, USB_CTRL_REG));

	musb->a_wait_bcon = A_WAIT_BCON_TIMEOUT;
	musb->isr = da8xx_interrupt;
	return 0;
}

int musb_platform_exit(struct musb *musb)
{
	if (is_host_enabled(musb))
		del_timer_sync(&otg_workaround);

	/* Delay to avoid problems with module reload... */
	if (is_host_enabled(musb) && musb->xceiv.default_a) {
		u8 devctl, warn = 0;
		int delay;

		/*
		 * If there's no peripheral connected, VBUS can take a
		 * long time to fall...
		 */
		for (delay = 30; delay > 0; delay--) {
			devctl = musb_readb(musb->mregs, MUSB_DEVCTL);
			if (!(devctl & MUSB_DEVCTL_VBUS))
				goto done;
			if ((devctl & MUSB_DEVCTL_VBUS) != warn) {
				warn = devctl & MUSB_DEVCTL_VBUS;
				DBG(1, "VBUS %d\n",
					warn >> MUSB_DEVCTL_VBUS_SHIFT);
			}
			msleep(1000);
		}

		/* In OTG mode, another host might be connected... */
		DBG(1, "VBUS off timeout (devctl %02x)\n", devctl);
	}
done:
	phy_off();
	return 0;
}
