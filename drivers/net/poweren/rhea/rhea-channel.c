/*
 * Copyright (C) 2011, 2012 IBM Corporation.
 * Author:	Davide Pasetto <pasetto_davide@ie.ibm.com>
 *			Karol Lynch <karol_lynch@ie.ibm.com>
 *			Kay Muller <kay.muller@ie.ibm.com>
 *			Jimi Xenidis <jimix@watson.ibm.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http:/www.gnu.org/licenses/>.
 */

#include "rhea-channel.h"
#include "rhea-mdio.h"

#include <hea-channel-regs.h>
#include "rhea-channel-resource.h"

#include <linux/etherdevice.h>

#define RHEA_PPORT_Q_MAX 64

struct rhea_ports {
	struct rhea_pport *pports[HEA_MAX_PPORT_COUNT];
	spinlock_t lock;
};

#include "rhea-base.h"

static struct rhea_ports s_pports;

struct rhea_pport *_rhea_pport_get(unsigned pport_nr)
{
	struct rhea_pport *pport;

	if (!is_hea_pport(pport_nr)) {
		rhea_error("Invalid physical port number : %u", pport_nr + 1);
		return NULL;
	}

	pport = s_pports.pports[pport_nr];

	if (NULL == pport)
		return NULL;

	return s_pports.pports[pport_nr];
}

static inline int _rhea_pport_err_reset(struct rhea_pport *pport)
{
	int rc = 0;
	u64 uaelog;
	enum hea_speed link_speed;
	struct rhea_pport_em *em;

	if (NULL == pport || RHEA_CHANNEL_NO_STATE == pport->state)
		return -EINVAL;

	link_speed = (HEA_SPEED_NONE != pport->port_cfg.speed_hw &&
		      pport->port_cfg.speed_dt != pport->port_cfg.speed_hw) ?
		      pport->port_cfg.speed_hw : pport->port_cfg.speed_dt;

	em = &pport->pport_regs->em;

	uaelog = in_be64(&em->pg_uaelog);

	/* check if link is up */
	if (HEA_SPEED_10G == link_speed) {
		rhea_debug("Check 10G: 0x%016llx", uaelog);

		/* see if link state change event occurred and deal with it */
		if (hea_get_u64_bits(uaelog, 32, 32)) {
			u16 mdio_data;
			u64 pst;

			/* Clear interrupt register */
			mdio_data = hea_mdio_iread(pport, 1, 0x9005);
			rhea_debug("MDIO Got IRQ from: %i",
				hea_get_mdio_bit(mdio_data, 0));

			pst = in_be64(&pport->pport_regs->mmc.p_pst);

			rhea_debug("Local Link State: %llu",
				hea_get_u64_bits(pst, 60, 60));

			rhea_debug("Remote Link State: %llu",
				hea_get_u64_bits(pst, 61, 61));

			/* 1 when having sync-lock --> link is up */
			mdio_data = hea_mdio_iread(pport, 3, 0x20);
			rhea_debug("MDIO Got 0x020 (sync-lock) from: %i",
				hea_get_mdio_bit(mdio_data, 0));

			/* 1=XGXS lane align. */
			mdio_data = hea_mdio_iread(pport, 4, 0x18);
			rhea_debug("MDIO Got 0x18 (XGXS lane align) from: %i",
				   hea_get_mdio_bit(mdio_data, 12));

			/* 1=PMD signal OK */
			mdio_data = hea_mdio_iread(pport, 1, 0xA);
			rhea_debug("MDIO Got 0xA (signal ok (1=PMD)) from: %i",
				   hea_get_mdio_bit(mdio_data, 0));

			/* tell HEA that we have detected the change */
			uaelog = hea_set_u64_bits(uaelog, 1, 0, 0);

			rhea_info("10G Port Link state change: %u",
				   pport->port_cfg.pport_nr  + 1);
		}
	} else {
		rhea_debug("Check 1G: 0x%016llx", uaelog);

		/* clear PCS intr */
		pcs_mdio_write(pport, 17, (uint16_t)~0);

		/* see if link state change event occurred and deal with it */
		if (hea_get_u64_bits(uaelog, 33, 33)) {
			/* tell HEA we detected the change */
			uaelog = hea_set_u64_bits(uaelog, 1, 1, 1);

			rhea_info("1G Port A PHY Event: %u state change",
				   pport->port_cfg.pport_nr + 1);
		}

		if (hea_get_u64_bits(uaelog, 35, 35)) {
			/* tell HEA we detected the change */
			uaelog = hea_set_u64_bits(uaelog, 1, 3, 3);

			rhea_info("1G Port A GPCS Interrupt: %u state change",
				  pport->port_cfg.pport_nr + 1);
		}
	}

	/* reset state to be cleared */
	out_be64(&em->pg_uaelog, uaelog);

	return rc;
}

int rhea_pport_err_reset(unsigned int pport_nr)
{
	int rc = 0;
	struct rhea_pport *pport;

	pport = _rhea_pport_get(pport_nr);
	if (NULL == pport)
		return 0;

	/* check if the port is actually enabled */
	if (RHEA_CHANNEL_ENABLED != pport->state)
		return 0;

	spin_lock(&pport->lock);

	rc = _rhea_pport_err_reset(pport);
	if (0 > rc) {
		spin_unlock(&pport->lock);
		rhea_error("Error when resetting pport[%u] error state",
			   pport_nr + 1);
		return rc;
	}

	spin_unlock(&pport->lock);

	return rc;
}


static int _rhea_pport_link_state_get(struct rhea_pport *pport)
{
	int link_state_up;
	enum hea_speed link_speed;

	if (NULL == pport)
		return -EINVAL;

	link_state_up = pport->link_state_up;

	link_speed = (HEA_SPEED_NONE != pport->port_cfg.speed_hw &&
		      pport->port_cfg.speed_dt != pport->port_cfg.speed_hw) ?
		pport->port_cfg.speed_hw : pport->port_cfg.speed_dt;

	/* check if link is up */
	if (HEA_SPEED_10G == link_speed) {
		u16 mdio_data;
		int link_status;

		link_status = 0;

		/* PMD Receive Signal Detect */
		mdio_data = hea_mdio_iread(pport, 1, 0x000A);
		if (mdio_data == 0xffff) {
			rhea_error("MDIO communication ERROR");
			return -EINVAL;
		}
		link_status += (hea_get_mdio_bit(mdio_data, 0) == 1) ? 1 : 0 ;

		/* 10GBASE-R PCS */
		mdio_data = hea_mdio_iread(pport, 3, 0x0020);
		if (mdio_data == 0xffff) {
			rhea_error("MDIO communication ERROR");
			return -EINVAL;
		}

		link_status += (hea_get_mdio_bit(mdio_data, 0) == 1) ? 1 : 0 ;

		/* 1=XGXS lane align --> connection between HEA and PHY is up */
		mdio_data = hea_mdio_iread(pport, 4, 0x18);
		if (mdio_data == 0xffff) {
			rhea_error("MDIO communication ERROR");
			return -EINVAL;
		}

		link_status += (hea_get_mdio_bit(mdio_data, 12) == 1) ? 1 : 0 ;

		link_state_up = ((3 == link_status) ? 1 : 0);

		rhea_debug("10G Link %s of pport[%u]",
			  ((link_state_up) ? "UP" : "DOWN"),
			  pport->port_cfg.pport_nr + 1);
	} else {
		u16 mdio_data;

		rhea_debug("Check 1G");

		mdio_data = hea_mdio_read(pport, 0x11);
		if (mdio_data == 0xffff) {
			rhea_error("MDIO communication ERROR");
			return -EINVAL;
		}

		/* get state of the 1G port */
		link_state_up = hea_get_mdio_bit(mdio_data, 8) == 1 ? 1 : 0;

		rhea_debug("1G Link %s of pport[%u]",
			  (link_state_up) ? "UP" : "DOWN",
			  pport->port_cfg.pport_nr + 1);
	}

	/* write back result */
	pport->link_state_up = link_state_up;

	return link_state_up;
}

int rhea_pport_link_state_get(unsigned pport_nr)
{
	int rc = 0;
	struct rhea_pport *pport;

	pport = _rhea_pport_get(pport_nr);
	if (NULL == pport)
		return 0;

	/* check if the port is actually enabled */
	if (RHEA_CHANNEL_ENABLED != pport->state)
		return 0;

	spin_lock(&pport->lock);

	rc = _rhea_pport_link_state_get(pport);
	if (0 > rc) {
		spin_unlock(&pport->lock);
		rhea_error("Error when getting link state of pport[%u]",
			   pport_nr + 1);
		return rc;
	}

	spin_unlock(&pport->lock);

	return rc;
}

static int _rhea_pport_start(struct rhea_pport *pport)
{
	u16 mdio_data;
	int rc = 0;
	int speed_hw;
	u64 reg;
	struct rhea_pport_mmc *mmc;

	if (NULL == pport) {
		rhea_error("Invalid parameter passed in");
		return -EINVAL;
	}

	mmc = &pport->pport_regs->mmc;

	reg = in_be64(&mmc->pg_ec);

	/* try to detect mode of the HEA port */
	if (1 == hea_get_u64_bits(reg, 1, 1))
		pport->port_cfg.speed_hw = HEA_SPEED_10G;
	else if (1 == hea_get_u64_bits(reg, 2, 2))
		pport->port_cfg.speed_hw = HEA_SPEED_1G;
	else {
		rhea_error("Port %u is not enabled",
			   pport->port_cfg.pport_nr + 1);
		return -EINVAL;
	}

	if (pport->port_cfg.speed_dt != pport->port_cfg.speed_hw)
		rhea_warning("port %d speed mismatch, overriding with %d.",
			     pport->port_cfg.pport_nr + 1,
			     pport->port_cfg.speed_hw);

	rhea_info("Initialise port %u with speed %uMbit/s.",
		  pport->port_cfg.pport_nr + 1, pport->port_cfg.speed_hw);

	speed_hw = pport->port_cfg.speed_hw;

	pport->port_cfg.ext_phy_addr = -1;	/* Unknown yet */
	pport->port_cfg.int_pcs_addr = 16;

	rc = _rhea_pport_counters_clear(pport);
	if (rc) {
		rhea_error("Could not reset port counters");
		return -EINVAL;
	}

	if (HEA_SPEED_1G == speed_hw) {
		reg = in_be64(&mmc->pg_ec);

		/* set; 1G Enet Mode */
		reg = hea_set_u64_bits(reg, 1, 3, 3);
		out_be64(&mmc->pg_ec, reg);
	}

	/* take external PHY out of reset after 1ms */
	usleep_range(1000, 1100);

	/* Turn off the PHY Reset by writing Pxs_PC */
	reg = in_be64(&mmc->p_pc);
	reg = hea_set_u64_bits(reg, 0, 54, 54);
	out_be64(&mmc->p_pc, reg);

	/* Take MAC out of reset */
	if (HEA_SPEED_1G == speed_hw) {

		reg = in_be64(&mmc->p_pc);

		/* Make sure that we're in 1G by default */
		reg = hea_set_u64_bits(reg, 1, 57, 59);

		/* works for 100Mbit as well */
		reg = hea_set_u64_bits(reg, 0, 60, 60);
		out_be64(&mmc->p_pc, reg);

		/* Soft reset the PCS */
		out_be64(&mmc->p_mma, 0x0000080110008000ULL);
	} else {
		reg = in_be64(&mmc->p_pc);

		/* Turn off the XGXSPCS Reset */
		reg = hea_set_u64_bits(reg, 0, 56, 56);
		out_be64(&mmc->p_pc, reg);

		/* Turn off the MAC Reset */
		reg = hea_set_u64_bits(reg, 0, 60, 60);
		out_be64(&mmc->p_pc, reg);

		/* get 6 microseconds delay after reset */
		udelay(6);
	}

	reg = in_be64(&mmc->p_pc);

	/* Enable the MAC Tx */
	reg = hea_set_u64_bits(reg, 1, 63, 63);
	out_be64(&mmc->p_pc, reg);

	/* set speed (not required for 10G) */
	if (HEA_SPEED_100M == speed_hw) {
		reg = in_be64(&mmc->p_pc);
		reg = hea_set_u64_bits(reg, 2, 58, 59);
		out_be64(&mmc->p_pc, reg);

	}

	if (HEA_SPEED_1G == speed_hw) {
		/* Enable SGMII Auto-Negotiation here in 1G mode, */
		hea_mdio_write(pport, 0x10, 0x1140);
	}

	if (HEA_SPEED_100M == speed_hw || HEA_SPEED_1G == speed_hw) {
		int speed = ((HEA_SPEED_1G == speed_hw) ? 0x1 : 0x2);

		reg = in_be64(&mmc->p_pc);
		/* set 1G or 100MB speed */
		reg = hea_set_u64_bits(reg, speed, 58, 59);
		/* Turn on the Reset MAC */
		reg = hea_set_u64_bits(reg, 1, 60, 60);
		out_be64(&mmc->p_pc, reg);

		/* Turn off the Reset MAC */
		reg = hea_set_u64_bits(reg, 0, 60, 60);
		out_be64(&mmc->p_pc, reg);

		/* Soft reset the PCS */
		out_be64(&mmc->p_mma, 0x0000080110008000ULL);
	}

	if (HEA_SPEED_10G == speed_hw) {
		/*
		 * This code enables the interrupt support from the PHY
		 *
		 * LASI_1,2  active low, open drain, extern pull-up required
		 *
		 * active when fault condition in 1.9003,4,5
		 * is set and the respective enable bits in the control
		 * registers 1.9000,1,2 are set
		 *
		 * the LASI is connected to HEA_INT on the Prism chip.
		 * HEA_INT value can be read from Px_UAELOG.32
		 */

		/* Enable RX faults */
		mdio_data = 0; /* disable ALL */
		hea_mdio_iwrite(pport, 1, 0x9000, mdio_data);

		/* Enable TX faults. Default is ALL ON */
		mdio_data = 0; /* disable ALL */
		hea_mdio_iwrite(pport, 1, 0x9001, mdio_data);

		/* Enable Alarms for LASI */
		mdio_data = hea_mdio_iread(pport, 1, 0x9002);
		mdio_data |= hea_set_mdio_bit(0); /* LS */
		hea_mdio_iwrite(pport, 1, 0x9002, mdio_data);
	}

	if (HEA_SPEED_100M == speed_hw) {
		reg = in_be64(&mmc->p_macc);

		/* Disable jumbo frames support for 100 Mbit */
		reg = hea_set_u64_bits(reg, 0, 47, 47);
		out_be64(&mmc->p_macc, reg);
	}

	if (HEA_SPEED_10G == speed_hw && NULL != pport->channel[HEA_UC_PORT] &&
	    pport->channel[HEA_UC_PORT]->channel_cfg.uc.test) {
		reg = in_be64(&mmc->p_macc);

		/* set physical port into internal
		 * transmit loopback mode
		 * */
		reg = hea_set_u64_bits(reg, 1, 46, 46);

		out_be64(&mmc->p_macc, reg);

		rhea_info("Enable internal loopback mode for pport: %u",
			  pport->port_cfg.pport_nr + 1);

		pport->mac_loopback = 1;
	}

	/* enable notification of port state change */
	if (HEA_SPEED_10G == speed_hw) {
		/* ignore 1G */
		reg = hea_set_u64_bits(0x0ULL, 1, 1, 1);
		reg = hea_set_u64_bits(reg, 1, 3, 3);
		out_be64(&pport->pport_regs->em.pg_uaelogm, reg);

	} else {
		/* ignore 10G */
		reg = hea_set_u64_bits(0x0ULL, 1, 0, 0);
		out_be64(&pport->pport_regs->em.pg_uaelogm, reg);

		/* enable PCS intr */
		pcs_mdio_write(pport, 18, 0x0007);

		/* clear PCS intr */
		pcs_mdio_write(pport, 17, (uint16_t)~0);
		{
		    u16 pcs;
		    /* enable PCS intr */
		    pcs = pcs_mdio_read(pport, 18);
		    rhea_debug("PCS INTR (18): %04x", pcs);


		    pcs = pcs_mdio_read(pport, 17);
		    rhea_debug("PCS INTR (17): %04x", pcs);
		}
	}

	reg = in_be64(&mmc->p_pc);

	/* Enable the Rx */
	reg = hea_set_u64_bits(reg, 1, 62, 62);

	/* Enable the Tx */
	reg = hea_set_u64_bits(reg, 1, 63, 63);
	out_be64(&mmc->p_pc, reg);

	/* Wait for GMAC autonegotiation to complete... if we don't do this,
	 * the first packet(s) that we transmit might get lost
	 * (we've seen this behaviour on real hardware) */
	if (HEA_SPEED_1G == speed_hw) {
		int i;
		for (i = 1000000; i > 0; i--) {
			reg = in_be64(&pport->pport_regs->mmc.p_spcsst);
			if (1 == hea_get_u64_bits(reg, 46, 46))
				break;

			udelay(1);
		}

		if (0 == i)
			rhea_info("GMAC auto-negotiation failed!");
	}

	return rc;
}

static int _rhea_channel_avail(unsigned pport_nr,
			       enum hea_channel_type type,
			       unsigned shared)
{
	int rc;
	struct rhea_pport *pport;

	pport = _rhea_pport_get(pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	switch (type) {
	case HEA_UC_PORT:
	case HEA_MC_PORT:
	case HEA_BC_PORT:
		rc = ((NULL == pport->channel[type]) ? 1 : 0);
		if (0 == rc && shared &&
		    HEA_DEFAULT_CHANNEL_MANAGER ==
		    pport->channel[type]->channel_cfg.dc.channel_usuage) {
			rc = 1;
		}

		break;
	default:

		rc = ((NULL == pport->channel[type]) ? 1 : 0);
		break;
	}

	return rc;
}

/*
 * Checks whether the physical port is available
 */
static unsigned _rhea_pport_free_channel_count(unsigned pport_nr)
{
	int i;
	unsigned channel_count = 0;
	struct rhea_pport *pport;

	pport = _rhea_pport_get(pport_nr);
	if (NULL == pport)
		return 0;

	for (i = 0; i < HEA_MAX_PPORT_CHANNEL_COUNT; ++i) {
		if (NULL == pport->channel[i])
			++channel_count;
	}

	return channel_count;
}

static unsigned _rhea_pport_free_lport_count(unsigned pport_nr)
{
	int i = 0;
	unsigned channel_count = 0;
	struct rhea_pport *pport;

	pport = _rhea_pport_get(pport_nr);
	if (NULL == pport)
		return 0;

	for (i = HEA_LPORT_0; i <= HEA_LPORT_3; ++i) {
		if (NULL == pport->channel[i])
			++channel_count;
	}

	return channel_count;
}

/*
 * Checks how many channels are disable
 */
static inline unsigned _rhea_pport_disabled_channel_count(unsigned
							  pport_nr)
{
	int i;
	unsigned channel_count = 0;
	struct rhea_pport *pport;

	pport = _rhea_pport_get(pport_nr);
	if (NULL == pport)
		return 0;

	for (i = 0; i < HEA_MAX_PPORT_CHANNEL_COUNT; ++i) {
		if (NULL == pport->channel[i] ||
		    RHEA_CHANNEL_DISABLED == pport->channel[i]->state ||
		    RHEA_CHANNEL_INIT == pport->channel[i]->state)
			++channel_count;

	}

	return channel_count;
}

static int _rhea_pport_avail(unsigned pport_nr)
{
	int rc = 0;
	int channel_count = 0;
	struct rhea_pport *pport;

	pport = _rhea_pport_get(pport_nr);
	if (NULL == pport)
		return 0;

	channel_count = _rhea_pport_free_channel_count(pport_nr);

	/* computes whether there are pport resources available */
	rc = ((HEA_MAX_PPORT_CHANNEL_COUNT < channel_count ||
	       0 == channel_count) ? 0 : 1);

	return rc;
}

static int _rhea_pport_disable(struct rhea_pport *pport)
{
	u64 reg;
	struct rhea_pport_mmc *mmc;

	if (NULL == pport)
		return -EINVAL;

	mmc = &pport->pport_regs->mmc;

	reg = in_be64(&mmc->p_pc);

	/* disable RX */
	reg = hea_set_u64_bits(reg, 0, 62, 62);

	/* disable TX */
	reg = hea_set_u64_bits(reg, 0, 63, 63);

	out_be64(&mmc->p_pc, reg);

	/* wait until RX is disabled */
	while ((in_be64(&mmc->p_pst) &
		hea_set_u64_bits(0x0ULL, 1, 62, 62)) == 0)
		continue;

	/* wait until TX is disabled */
	while ((in_be64(&mmc->p_pst) &
		hea_set_u64_bits(0x0ULL, 1, 63, 63)) == 0)
		continue;

	return 0;
}

static int _rhea_pport_enable(struct rhea_pport *pport)
{
	u64 reg;
	struct rhea_pport_mmc *mmc;

	if (NULL == pport)
		return -EINVAL;

	mmc = &pport->pport_regs->mmc;

	/* check if mac loopback needs to be enabled */
	if (HEA_SPEED_10G == pport->port_cfg.speed_hw &&
	    NULL != pport->channel[HEA_UC_PORT] &&
	    pport->channel[HEA_UC_PORT]->channel_cfg.uc.test) {

		reg = in_be64(&mmc->p_macc);

		/* set physical port into internal
		 * transmit loopback mode
		 * */
		reg = hea_set_u64_bits(reg, 1, 46, 46);

		out_be64(&mmc->p_macc, reg);

		rhea_info("Enable internal loopback mode for pport: %u",
			  pport->port_cfg.pport_nr + 1);

		pport->mac_loopback = 1;
	}

	reg = in_be64(&mmc->p_pc);

	/* enable RX */
	reg = hea_set_u64_bits(reg, 1, 62, 62);

	/* enable TX */
	reg = hea_set_u64_bits(reg, 1, 63, 63);

	out_be64(&mmc->p_pc, reg);

	return 0;
}

static int _rhea_lport_enable(struct rhea_pport *pport,
			      unsigned lport_nr)
{
	u64 reg;
	struct rhea_pport_bpfc *bpfc;

	if (NULL == pport)
		return -EINVAL;

	if (!is_hea_lport(HEA_LPORT_0 + lport_nr)) {
		rhea_error("Logical port number is too high");
		return -EINVAL;
	}

	bpfc = &pport->pport_regs->bpfc;

	reg = in_be64(&bpfc->pl_rc[lport_nr]);

	/* logical port valid */
	reg = hea_set_u64_bits(reg, 1, 49, 49);

	out_be64(&bpfc->pl_rc[lport_nr], reg);

	/* if MAC is set, enable it */
	if (pport->channel[HEA_LPORT_0 + lport_nr]->mac_address._be64) {
		reg = in_be64(&bpfc->pl_mac[lport_nr]);

		/* enable MAC */
		reg = hea_set_u64_bits(reg, 1, 0, 0);
		out_be64(&bpfc->pl_mac[lport_nr], reg);
	}

	return 0;
}

static int _rhea_lport_disable(struct rhea_pport *pport,
			       unsigned lport_nr)
{
	u64 reg;
	struct rhea_pport_bpfc *bpfc;

	if (NULL == pport)
		return -EINVAL;

	if (!is_hea_lport(HEA_LPORT_0 + lport_nr)) {
		rhea_error("Logical port number is too high");
		return -EINVAL;
	}

	bpfc = &pport->pport_regs->bpfc;

	/* if MAC is set, disable it */
	if (pport->channel[HEA_LPORT_0 + lport_nr]->mac_address._be64) {
		reg = in_be64(&bpfc->pl_mac[lport_nr]);

		/* disable MAC */
		reg = hea_set_u64_bits(reg, 0, 0, 0);
		out_be64(&bpfc->pl_mac[lport_nr], reg);
	}

	reg = in_be64(&bpfc->pl_rc[lport_nr]);

	/* logical port disabled */
	reg = hea_set_u64_bits(reg, 0, 49, 49);

	out_be64(&bpfc->pl_rc[lport_nr], reg);

	return 0;
}

static inline void _enet_iton(u64 val, union hea_mac_addr *mac)
{
	int i;

	if (NULL == mac) {
		rhea_error("Invalid parameter passed in");
		return;
	}

	for (i = 0; i < ETH_ALEN; ++i)
		mac->sa.addr[i] = hea_get_u64_bits(val, 16 + i * 8, 23 + i * 8);
}

int _rhea_channel_mac_set(struct rhea_channel *channel,
			  union hea_mac_addr *mac_address)
{
	int rc = 0;
	struct rhea_pport *pport;

	if (NULL == channel || NULL == mac_address) {
		rhea_error("Invalid parameters: %p", channel);
		return -EINVAL;
	}

	if (!is_hea_lport(channel->type)) {
		rhea_error("Is not an lport");
		return -EINVAL;
	}

	if (!is_valid_ether_addr(mac_address->sa.addr)) {
		rhea_info("Ethernet address is not valid");
		return -EINVAL;
	}

	rhea_debug("Set MAC for pport: %u and logical port: %u",
		   channel->pport_nr + 1, hea_lport_index_get(channel->type));

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	if (is_hea_lport(channel->type)) {
		u64 reg = 0x0ULL;

		/* MAC address is not valid yet */
		reg = hea_set_u64_bits(reg, 0, 0, 0);

		/* set MAC */
		reg = hea_set_u64_bits(reg, mac_address->_be64, 16, 63);

		/* write mac to register */
		out_be64(&pport->pport_regs->bpfc.
			 pl_mac[hea_lport_index_get(channel->type)], reg);

		channel->channel_cfg.lport.mac_address._be64 =
			mac_address->_be64;

		channel->mac_address._be64 = mac_address->_be64;
	} else {
		rhea_warning("It is only possible to set MAC address "
			     "for logical ports");
	}

	return rc;
}

int rhea_channel_mac_set(struct rhea_channel *channel,
			 union hea_mac_addr *mac_address)
{
	int rc = 0;

	struct rhea_pport *pport;

	if (NULL == channel || NULL == mac_address)
		return -EINVAL;

	rhea_debug("Set channel_mac: %u", channel->pport_nr + 1);

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	spin_lock(&pport->lock);

	/* save MAC address */
	rc = _rhea_channel_mac_set(channel, mac_address);
	if (rc) {
		spin_unlock(&pport->lock);
		rhea_error("Was not able to set mac address for pport: "
			  "%u and channel type: %u",
			 channel->pport_nr + 1, channel->type);
		return rc;
	}

	spin_unlock(&pport->lock);

	return rc;
}

int _rhea_channel_mac_get(struct rhea_channel *channel,
			  union hea_mac_addr *mac_address)
{
	int rc = 0;

	union hea_mac_addr mac;
	struct rhea_pport *pport;

	if (NULL == channel || NULL == mac_address)
		return -EINVAL;

	rhea_debug("Get channel_mac: %u", channel->pport_nr + 1);

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	/* check if separate MAC for logical port has been configured */
	if (0 == channel->mac_address._be64) {
		/* get MAC address */
		mac = pport->port_cfg.mac_address;

		if (0 == mac._be64) {
			rhea_error("MAC address not set for physical port: %u",
				   pport->port_cfg.pport_nr + 1);
			return -EINVAL;
		}

		/* save MAC address */
		mac_address->_be64 = mac._be64;

		rhea_debug("Mac channel type: %i", channel->type);

		/* adjust depending on port */
		switch (channel->type) {
		case HEA_LPORT_0:
			break;
		case HEA_LPORT_1:
			mac_address->_be64 += 1;
			break;
		case HEA_LPORT_2:
			mac_address->_be64 += 2;
			break;
		case HEA_LPORT_3:
			mac_address->_be64 += 3;
			break;
		default:
			rhea_error("Invalid type for MAC address");
			rc = -EINVAL;
			break;
		}
	} else {
		/* save MAC address */
		mac_address->_be64 = channel->mac_address._be64;
	}

	return rc;
}

int rhea_channel_mac_get(struct rhea_channel *channel,
			 union hea_mac_addr *mac_address)
{
	int rc = 0;

	struct rhea_pport *pport;

	if (NULL == channel || NULL == mac_address)
		return -EINVAL;

	if (!is_hea_lport(channel->type)) {
		rhea_error("Only lports can get MAC address: %u",
			   channel->type);
		return -EINVAL;
	}

	rhea_debug("Get channel_mac: %u", channel->pport_nr + 1);

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	spin_lock(&pport->lock);

	rc = _rhea_channel_mac_get(channel, mac_address);
	if (rc) {
		spin_unlock(&pport->lock);
		rhea_error("Was not able to get mac address");
		return rc;
	}

	spin_unlock(&pport->lock);

	return rc;
}

static int _rhea_pport_eth_mac_set(struct rhea_gen_base *base,
				   struct rhea_pport *pport)
{
	u64 v;

	if (NULL == base || NULL == pport) {
		rhea_error("Invalid parameter passed in");
		return -EINVAL;
	}

	if (is_valid_ether_addr(pport->port_cfg.mac_address.sa.addr)) {
		rhea_info("MAC address of physical port %u: "
			  "%02x:%02x:%02x:%02x:%02x:%02x",
			  pport->port_cfg.pport_nr + 1,
			  pport->port_cfg.mac_address.sa.addr[0],
			  pport->port_cfg.mac_address.sa.addr[1],
			  pport->port_cfg.mac_address.sa.addr[2],
			  pport->port_cfg.mac_address.sa.addr[3],
			  pport->port_cfg.mac_address.sa.addr[4],
			  pport->port_cfg.mac_address.sa.addr[5]);

		goto set_mac;
	}

	v = in_be64(&base->g_bimac);
	if (v & hea_set_u64_bits(0x0U, ~0x0U, 0, 0)) {
		u64 l;
		l = in_be64(&pport->pport_regs->bpfc.pl_mac[0]);
		if (l & hea_set_u64_bits(0x0U, ~0x0U, 0, 0)) {
			_enet_iton(v, &pport->port_cfg.mac_address);

			rhea_debug("Calculating MAC for port[%d] "
				  "from pl_mac: %llx",
				  pport->port_cfg.pport_nr + 1,
				  pport->port_cfg.mac_address._be64);

			goto set_mac;
		}

		/* The HEA has a MAC address we can calculate */
		v += (pport->port_cfg.pport_nr) * 16 *
			pport->port_cfg.ap->instance;
		v += (pport->port_cfg.pport_nr) * 4;

		_enet_iton(v, &pport->port_cfg.mac_address);

		rhea_debug("Calculating MAC for port[%d] from g_bimac: %llx",
			  pport->port_cfg.pport_nr + 1,
			  pport->port_cfg.mac_address._be64);

		goto set_mac;
	}

	v = in_be64(&pport->pport_regs->mmc.p_uaa);
	if (v != 0) {
		_enet_iton(v, &pport->port_cfg.mac_address);

		rhea_debug("Calculating MAC for port[%d] from pxs_uaa: %llx",
			  pport->port_cfg.pport_nr + 1,
			  pport->port_cfg.mac_address._be64);
		goto set_mac;
	}

	rhea_error("Should not have managed to get here!");

	return -EINVAL;

set_mac:

	return 0;
}

static int _rhea_pport_disable_check(struct rhea_pport *pport)
{
	int rc = 0;

	if (NULL == pport)
		return -EINVAL;

	if (RHEA_CHANNEL_DISABLED == pport->state ||
	    RHEA_CHANNEL_INIT == pport->state)
		return 0;

	if (HEA_MAX_PPORT_LPORT_COUNT ==
	    _rhea_pport_free_lport_count(pport->port_cfg.pport_nr) &&
	    ((NULL == pport->channel[HEA_UC_PORT] ||
	      HEA_DEFAULT_CHANNEL_ALONE !=
	      pport->channel[HEA_UC_PORT]->channel_cfg.dc.channel_usuage) &&
	     (NULL == pport->channel[HEA_MC_PORT] ||
	      HEA_DEFAULT_CHANNEL_ALONE !=
	      pport->channel[HEA_MC_PORT]->channel_cfg.dc.channel_usuage) &&
	     (NULL == pport->channel[HEA_BC_PORT] ||
	      HEA_DEFAULT_CHANNEL_ALONE !=
	      pport->channel[HEA_BC_PORT]->channel_cfg.dc.channel_usuage))) {
		rhea_info("Stop PPORT[%u]", pport->port_cfg.pport_nr + 1);

		rc = _rhea_pport_disable(pport);
		if (rc) {
			rhea_error("Was not able to stop pport: %u",
				   pport->port_cfg.pport_nr + 1);
			return rc;
		}

		if (pport->mac_loopback) {
			u64 reg;

			reg = in_be64(&pport->pport_regs->mmc.p_macc);

			/* set physical port into normal transmit mode */
			reg = hea_set_u64_bits(reg, 0, 46, 46);

			out_be64(&pport->pport_regs->mmc.p_macc, reg);

			rhea_info("Disable internal loopback mode "
				  "for pport: %u",
				   pport->port_cfg.pport_nr + 1);

			pport->mac_loopback = 0;
		}

		/* set new state */
		pport->state = RHEA_CHANNEL_DISABLED;
	}

	return rc;
}

static int _rhea_pport_enable_check(struct rhea_pport *pport)
{
	int rc = 0;

	if (NULL == pport)
		return -EINVAL;

	if (HEA_MAX_PPORT_CHANNEL_COUNT !=
	    _rhea_pport_free_channel_count(pport->port_cfg.pport_nr)) {

		switch (pport->state) {
		case RHEA_CHANNEL_NO_STATE:
			rhea_error("Inialise pport: %u first",
				   pport->port_cfg.pport_nr + 1);
			return -EINVAL;

		case RHEA_CHANNEL_INIT:

			rhea_info("Start pport: %u",
				  pport->port_cfg.pport_nr + 1);

			rc = _rhea_pport_start(pport);
			if (rc) {
				rhea_error("Was not able to start pport: "
					   "%u first",
					   pport->port_cfg.pport_nr + 1);
				return rc;
			}

			/* set new state */
			pport->state = RHEA_CHANNEL_ENABLED;

			break;

		case RHEA_CHANNEL_DISABLED:

			rhea_info("Enable pport: %u",
				  pport->port_cfg.pport_nr + 1);

			rc = _rhea_pport_enable(pport);
			if (rc) {
				rhea_error("Was not able to start pport: "
					   "%u first",
					   pport->port_cfg.pport_nr + 1);
				return rc;
			}

			/* set new state */
			pport->state = RHEA_CHANNEL_ENABLED;

			break;

		default:

			rhea_debug("Don't enable pport: %u",
				   pport->port_cfg.pport_nr + 1);

			break;
		}

	} else {
		rhea_debug("Don't enable PPORT[%u] since channel count is: %u",
			   pport->port_cfg.pport_nr + 1,
			   _rhea_pport_free_channel_count(pport->port_cfg.
							  pport_nr));
	}

	return rc;
}

int rhea_channel_avail(unsigned pport_nr, enum hea_channel_type type)
{
	int rc;
	struct rhea_pport *pport;

	pport = _rhea_pport_get(pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	spin_lock(&pport->lock);

	rc = _rhea_channel_avail(pport_nr, type, 0);

	spin_unlock(&pport->lock);

	return rc;
}

int rhea_channel_start(struct rhea_channel *channel)
{
	int rc = 0;
	struct rhea_pport *pport;

	if (NULL == channel)
		return -EINVAL;

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	spin_lock(&pport->lock);

	if (NULL == pport->channel[HEA_UC_PORT] ||
	    NULL == pport->channel[HEA_BC_PORT] ||
	    NULL == pport->channel[HEA_MC_PORT]) {
		spin_unlock(&pport->lock);
		rhea_error("UC/MC/BC channels have to be allocated "
			   "before enabling a channel");
		return -EINVAL;
	}

	rc = _rhea_pport_enable_check(pport);
	if (rc) {
		spin_unlock(&pport->lock);
		rhea_warning("Was not able to start physical port");
		rc = 0;
	}

	if (is_hea_lport(channel->type)) {
		int port_index = hea_lport_index_get(channel->type);

		rhea_info("Enable LPORT[%u]",
			  hea_lport_index_get(channel->type));

		rc = _rhea_lport_enable(pport, port_index);
		if (rc) {
			spin_unlock(&pport->lock);
			rhea_error("Was not able to enable logical port");
			return rc;
		}
		channel->state = RHEA_CHANNEL_ENABLED;
	} else {
		rhea_debug("It is not possible to start BC/MC/UC channel");
	}

	spin_unlock(&pport->lock);

	return rc;
}

static int _rhea_channel_stop(struct rhea_channel *channel)
{
	int rc = 0;
	struct rhea_pport *pport;

	if (NULL == channel)
		return -EINVAL;

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	if (RHEA_CHANNEL_DISABLED == channel->state ||
	    RHEA_CHANNEL_INIT == channel->state)
		return 0;

	if (is_hea_lport(channel->type)) {
		int port_index = hea_lport_index_get(channel->type);
		rhea_info("Disable LPORT[%u] on PPORT[%u]",
			  hea_lport_index_get(channel->type),
			  channel->pport_nr + 1);

		rc = _rhea_lport_disable(pport, port_index);
		if (rc) {
			rhea_error("Was not able to disable logical port");
			return rc;
		}
		channel->state = RHEA_CHANNEL_DISABLED;
	} else {
		rhea_debug("It is not possible to stop BC/MC/UC channel");
	}

	rc = _rhea_pport_disable_check(pport);
	if (rc) {
		rhea_warning("Problems when trying to turn off physical port");
		rc = 0;
	}

	return rc;
}

int rhea_channel_stop(struct rhea_channel *channel)
{
	int rc = 0;
	struct rhea_pport *pport;

	if (NULL == channel)
		return -EINVAL;

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	spin_lock(&pport->lock);

	rc = _rhea_channel_stop(channel);
	if (rc) {
		spin_unlock(&pport->lock);
		rhea_error("Error when stopping channel type: %u on port: %u",
			   channel->type, channel->pport_nr + 1);
		return rc;
	}

	spin_unlock(&pport->lock);

	return rc;
}

int rhea_pport_avail(unsigned pport_nr)
{
	int rc = 0;
	struct rhea_pport *pport;

	pport = _rhea_pport_get(pport_nr);
	if (NULL == pport)
		return 0;

	spin_lock(&pport->lock);

	rc = _rhea_pport_avail(pport_nr);

	spin_unlock(&pport->lock);

	return rc;
}

int _rhea_channel_vlan_set(struct rhea_channel *channel,
			   struct hea_channel_vlan_cfg *vlan_cfg)
{
	u64 reg;
	int tmp;
	int rc = 0;
	struct rhea_pport *pport;

	if (NULL == channel || NULL == vlan_cfg)
		return -EINVAL;

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	switch (channel->type) {
	case HEA_UC_PORT:
		reg = in_be64(&pport->pport_regs->bpfc.p_rcu);

		tmp = vlan_cfg->vlan_extract ? 1 : 0;
		reg = hea_set_u64_bits(reg, tmp, 50, 50);

		tmp = vlan_cfg->discard_untagged ? 1 : 0;
		reg = hea_set_u64_bits(reg, tmp, 61, 61);

		tmp = vlan_cfg->tag_filtering_mode;
		reg = hea_set_u64_bits(reg, tmp, 62, 63);

		out_be64(&pport->pport_regs->bpfc.p_rcu, reg);
		break;

	case HEA_MC_PORT:

		reg = in_be64(&pport->pport_regs->bpfc.p_rcm);

		tmp = vlan_cfg->vlan_extract ? 1 : 0;
		reg = hea_set_u64_bits(reg, tmp, 50, 50);

		tmp = vlan_cfg->discard_untagged ? 1 : 0;
		reg = hea_set_u64_bits(reg, tmp, 61, 61);

		tmp = vlan_cfg->tag_filtering_mode;
		reg = hea_set_u64_bits(reg, tmp, 62, 63);

		out_be64(&pport->pport_regs->bpfc.p_rcm, reg);

		break;

	case HEA_BC_PORT:

		reg = in_be64(&pport->pport_regs->bpfc.p_rcb);

		tmp = vlan_cfg->vlan_extract ? 1 : 0;
		reg = hea_set_u64_bits(reg, tmp, 50, 50);

		tmp = vlan_cfg->discard_untagged ? 1 : 0;
		reg = hea_set_u64_bits(reg, tmp, 61, 61);

		tmp = vlan_cfg->tag_filtering_mode;
		reg = hea_set_u64_bits(reg, tmp, 62, 63);

		out_be64(&pport->pport_regs->bpfc.p_rcb, reg);

		break;

	default:

		if (is_hea_lport(channel->type)) {
			int lport_index;
			struct rhea_pport_bpfc *bpfc;

			bpfc = &pport->pport_regs->bpfc;

			lport_index = hea_lport_index_get
					(channel->type);

			reg = in_be64(&bpfc->pl_rc[lport_index]);
			reg = hea_set_u64_bits(reg,
				      vlan_cfg->vlan_extract ? 1 : 0,
				      50, 50);
			reg = hea_set_u64_bits(reg,
				      vlan_cfg->
				      discard_untagged ? 1 : 0, 61,
				      61);
			reg = hea_set_u64_bits(reg,
				      vlan_cfg->tag_filtering_mode, 62,
				      63);

			if (vlan_cfg->default_tag.a.vid) {
				/* enable PVID support */
				reg = hea_set_u64_bits(reg,
					      vlan_cfg->default_tag.b,
					      32, 47);
				reg = hea_set_u64_bits(reg, 1, 59, 59);
			}

			out_be64(&bpfc->pl_rc[lport_index], reg);
		}
		break;
	}

	/* make sure that we save all vlan settings */
	channel->channel_cfg.vlan = *vlan_cfg;

	return rc;
}

int rhea_channel_vlan_set(struct rhea_channel *channel,
			  struct hea_channel_vlan_cfg *vlan_cfg)
{
	int rc = 0;
	struct rhea_pport *pport;

	if (NULL == channel || NULL == vlan_cfg)
		return -EINVAL;

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	spin_lock(&pport->lock);

	rc = _rhea_channel_vlan_set(channel, vlan_cfg);
	if (rc) {
		spin_unlock(&pport->lock);
		rhea_error("Was not able to set vlan");
		return rc;
	}

	spin_unlock(&pport->lock);

	return rc;
}

int _rhea_channel_vlan_filter_set(struct rhea_channel *channel,
				  u64 vlan_filter)
{
	int rc = 0;
	struct rhea_pport *pport;
	u64 val;
	u64 *addr_vlan_filter;
	unsigned int lport_nr;
	u16 vlan_index;
	u16 vlan_id;

	if (NULL == channel)
		return -EINVAL;

	if (!is_hea_lport(channel->type)) {
		rhea_error("VLAN filter only work for logical ports");
		return -EINVAL;
	}

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	/* get vlan filter */
	lport_nr = hea_lport_index_get(channel->type);
	addr_vlan_filter = (u64 *)
		&pport->pport_regs->pfc.pl_vlanf[lport_nr][0];

	/* compute vlan array index and get the correct vlan-id */
	vlan_index = (vlan_filter & 0xfff) / 64;
	vlan_id = vlan_filter % 64;
	vlan_id = 63 - vlan_id;

	/* read old value and update */
	val = in_be64(&addr_vlan_filter[vlan_index]);
	val |= (1ULL << vlan_id);

	/* write value to register */
	out_be64(&addr_vlan_filter[vlan_index], val);

	rhea_info("Set VLAN filter: 0x%llx (0x%llx) into index: %u",
		  vlan_filter, val, vlan_index);

	return rc;
}

int rhea_channel_vlan_filter_set(struct rhea_channel *channel,
				 u64 vlan_filter)
{
	int rc = 0;
	struct rhea_pport *pport;

	if (NULL == channel)
		return -EINVAL;

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	spin_lock(&pport->lock);

	rc = _rhea_channel_vlan_filter_set(channel, vlan_filter);
	if (rc) {
		spin_unlock(&pport->lock);
		rhea_error("Was not able to set vlan filter: 0x%llx",
			   vlan_filter);
		return rc;
	}

	spin_unlock(&pport->lock);

	return rc;
}

int _rhea_channel_vlan_filter_clear(struct rhea_channel *channel,
				    u64 vlan_filter)
{
	int rc = 0;
	struct rhea_pport *pport;
	u64 val;
	u64 *addr_vlan_filter;
	unsigned int lport_nr;
	u16 vlan_index;
	u16 vlan_id;

	if (NULL == channel)
		return -EINVAL;

	if (!is_hea_lport(channel->type)) {
		rhea_error("VLAN filter only work for logical ports");
		return -EINVAL;
	}

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	/* get vlan filter */
	lport_nr = hea_lport_index_get(channel->type);
	addr_vlan_filter = (u64 *)
		&pport->pport_regs->pfc.pl_vlanf[lport_nr][0];

	/* compute vlan array index and get the correct vlan-id */
	vlan_index = (vlan_filter & 0xfff) / 64;
	vlan_id = vlan_filter % 64;
	vlan_id = 63 - vlan_id;

	/* read old value and update */
	val = in_be64(&addr_vlan_filter[vlan_index]);
	val &= ~(1ULL << vlan_id);

	/* write value to register */
	out_be64(&addr_vlan_filter[vlan_index], val);

	rhea_info("Clear VLAN filter: 0x%llx (0x%llx) into index: %u",
		  vlan_filter, val, vlan_index);

	return rc;
}

int rhea_channel_vlan_filter_clear(struct rhea_channel *channel,
				   u64 vlan_filter)
{
	int rc = 0;
	struct rhea_pport *pport;

	if (NULL == channel)
		return -EINVAL;

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	spin_lock(&pport->lock);

	rc = _rhea_channel_vlan_filter_clear(channel, vlan_filter);
	if (rc) {
		spin_unlock(&pport->lock);
		rhea_error("Was not able to clear vlan filter: 0x%llx",
			   vlan_filter);
		return rc;
	}

	spin_unlock(&pport->lock);

	return rc;
}

int _rhea_channel_vlan_filter_get(struct rhea_channel *channel,
				  u64 *value)
{
	int rc = 0;
	struct rhea_pport *pport;
	u64 val;
	u64 is_set;
	u64 *addr_vlan_filter;
	u64 vlan_filter;
	unsigned int lport_nr;
	u16 vlan_index;
	u16 vlan_id;

	if (NULL == channel)
		return -EINVAL;

	if (!is_hea_lport(channel->type)) {
		rhea_error("VLAN filter only work for logical ports");
		return -EINVAL;
	}

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	/* get vlan filter */
	lport_nr = hea_lport_index_get(channel->type);
	addr_vlan_filter = (u64 *)
		&pport->pport_regs->pfc.pl_vlanf[lport_nr][0];

	/* get vlan-id */
	vlan_filter = *value;

	/* compute vlan array index and get the correct vlan-id */
	vlan_index = (vlan_filter & 0xfff) / 64;
	vlan_id = vlan_filter % 64;
	vlan_id = 63 - vlan_id;

	/* read current value and compare */
	val = in_be64(&addr_vlan_filter[vlan_index]);
	is_set = (1ULL << vlan_id) & val;

	/* save whether the vlan-id is set or not */
	*value = is_set;

	rhea_debug("Got VLAN filter: 0x%llx status 0x%llx from index: %u",
		   vlan_filter, is_set, vlan_index);

	return rc;
}

int rhea_channel_vlan_filter_get(struct rhea_channel *channel,
				 u64 *vlan_filter)
{
	int rc = 0;
	struct rhea_pport *pport;

	if (NULL == channel)
		return -EINVAL;

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	spin_lock(&pport->lock);

	rc = _rhea_channel_vlan_filter_get(channel, vlan_filter);
	if (rc) {
		spin_unlock(&pport->lock);
		rhea_error("Was not able to get vlan filter: 0x%llx",
			   *vlan_filter);
		return rc;
	}

	spin_unlock(&pport->lock);

	return rc;
}

int _rhea_channel_all_vlan_filters_clear(struct rhea_channel *channel)
{
	struct rhea_pport *pport;
	u64 val;
	u64 *addr_vlan_filter;
	unsigned int lport_nr;
	u16 vlan_index;

	if (NULL == channel)
		return -EINVAL;

	if (!is_hea_lport(channel->type)) {
		rhea_error("VLAN filter only work for logical ports");
		return -EINVAL;
	}

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	/* get vlan filter */
	lport_nr = hea_lport_index_get(channel->type);
	addr_vlan_filter = (u64 *)
		&pport->pport_regs->pfc.pl_vlanf[lport_nr][0];

	val = 0x0ULL;
	for (vlan_index = 0; vlan_index < 64; ++vlan_index) {

		/* write value to register */
		out_be64(&addr_vlan_filter[vlan_index], val);
	}

	return 0;
}

int rhea_channel_all_vlan_filters_clear(struct rhea_channel *channel)
{
	int rc = 0;
	struct rhea_pport *pport;

	if (NULL == channel)
		return -EINVAL;

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	spin_lock(&pport->lock);

	rc = _rhea_channel_all_vlan_filters_clear(channel);
	if (rc) {
		spin_unlock(&pport->lock);
		rhea_error("Was not able to clear all vlan filters");
		return rc;
	}

	spin_unlock(&pport->lock);

	return rc;
}

static inline enum
hea_channel_type _rhea_channel_get_type_from_id(unsigned pport_nr,
						unsigned int channel_id)
{
	enum hea_channel_type type;
	u32 channel_type_id;

	/* take away port specific information */
	channel_type_id = channel_id %
		((u32) MAX_PPORT_CHANNEL_COUNT * pport_nr);

	/* take away sharing information */
	type = (enum hea_channel_type)channel_type_id / MAX_HEA_TYPE_COUNT;

	/* check if it is a logical port */
	if (HEA_LPORT_0 == type) {
		rhea_debug("Found LPORT!!");
		/* get logical port information */
		type = (enum hea_channel_type)
			(channel_type_id % MAX_HEA_TYPE_COUNT + HEA_LPORT_0);
	}

	rhea_debug("Found type: %u", type);

	return type;
}

static inline u32 _rhea_channel_create_id(unsigned int pport_nr,
						struct hea_channel_cfg
						*channel_cfg)
{
	u32 channel_id = 0;

	if (!is_hea_lport(channel_cfg->type) &&
	    HEA_DEFAULT_CHANNEL_SHARE == channel_cfg->dc.channel_usuage) {
		unsigned int index;
		enum hea_channel_type type =
			_rhea_channel_get_type_from_id(pport_nr,
						       channel_cfg->dc.
						       lport_channel_id);

		if (!is_hea_lport(type)) {
			rhea_error("Did not find lport id");
			return -EINVAL;
		}

		index = hea_lport_index_get(type);

		/* deal with shared channel */
		channel_id = MAX_PPORT_CHANNEL_COUNT * pport_nr +
			channel_cfg->type * MAX_HEA_TYPE_COUNT + index + 1;

		rhea_debug("Generated id: %u for lport: %u on pport: %u",
			   channel_id, type - HEA_LPORT_0, pport_nr + 1);
	} else if (is_hea_lport(channel_cfg->type)) {
		/* LPORTS */
		channel_id = MAX_PPORT_CHANNEL_COUNT * pport_nr +
			MAX_HEA_DEFAULT_TYPE_COUNT * MAX_HEA_TYPE_COUNT +
			hea_lport_index_get(channel_cfg->type);

		rhea_debug("Generated lport id: %u", channel_id);
	} else {
		/* BC/UC/MC channel */
		channel_id = MAX_PPORT_CHANNEL_COUNT * pport_nr +
			channel_cfg->type * MAX_HEA_TYPE_COUNT;

		rhea_debug("Generated MC/UC/BC id: %u", channel_id);
	}

	return channel_id;

}

static inline int _rhea_channel_max_frame_size_set(struct rhea_channel
						   *channel,
						   unsigned int
						   max_frame_size)
{
	int rc = 0;
	u64 reg;
	struct rhea_pport *pport;

	if (NULL == channel)
		return -EINVAL;

	if (HEA_MAX_FRAME_SIZE < max_frame_size) {
		rhea_error("Maximum frame size for too high");
		return -EINVAL;
	}

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	/* sets maximum framesize for this lport */
	reg = hea_set_u64_bits(0x0ULL, max_frame_size ?
		      max_frame_size : HEA_MAX_FRAME_SIZE, 50, 63);

	switch (channel->type) {
	case HEA_UC_PORT:
		out_be64(&pport->pport_regs->bpfc.p_mfsizeu, reg);
		break;

	case HEA_MC_PORT:
		out_be64(&pport->pport_regs->bpfc.p_mfsizem, reg);
		break;

	case HEA_BC_PORT:
		out_be64(&pport->pport_regs->bpfc.p_mfsizeb, reg);
		break;

	case HEA_LPORT_0:
		out_be64(&pport->pport_regs->bpfc.pl_mfsize[0], reg);
		break;

	case HEA_LPORT_1:
		out_be64(&pport->pport_regs->bpfc.pl_mfsize[1], reg);
		break;

	case HEA_LPORT_2:
		out_be64(&pport->pport_regs->bpfc.pl_mfsize[2], reg);
		break;

	case HEA_LPORT_3:
		out_be64(&pport->pport_regs->bpfc.pl_mfsize[3], reg);
		break;

	default:
		rhea_error("Should not have been able to get here");
		rc = -EINVAL;
	}

	/* save framesize */
	channel->channel_cfg.max_frame_size = max_frame_size ?
		max_frame_size : HEA_MAX_FRAME_SIZE;

	return rc;
}

int rhea_channel_max_frame_size_set(struct rhea_channel *channel,
				    unsigned int max_frame_size)
{
	int rc = 0;
	struct rhea_pport *pport;

	if (NULL == channel)
		return -EINVAL;

	if (HEA_MAX_FRAME_SIZE < max_frame_size) {
		rhea_error("Maximum frame size for too high");
		return -EINVAL;
	}

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	spin_lock(&pport->lock);

	rc = _rhea_channel_max_frame_size_set(channel, max_frame_size);
	if (rc) {
		rhea_error("Was not able to set frame size for channel");
		spin_unlock(&pport->lock);
		return rc;
	}

	spin_unlock(&pport->lock);

	return rc;
}

static int _channel_lport_counters_get(struct rhea_channel *channel,
				       struct hea_channel_lport_counters
				       *lport_counters)
{
	int rc = 0;
	unsigned int lport_index;
	struct rhea_pport *pport;

	if (NULL == channel || NULL == lport_counters)
		return -EINVAL;

	if (!is_hea_lport(channel->type)) {
		rhea_error("Only possible to execute this function for lports");
		return -EINVAL;
	}

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	lport_index = hea_lport_index_get(channel->type);

	/* get rx lport registers */
	lport_counters->pl_rxerr =
		in_be64(&pport->pport_regs->bpfc.pl_rxerr[lport_index]);
	lport_counters->pl_rxfd =
		in_be64(&pport->pport_regs->bpfc.pl_rxfd[lport_index]);
	lport_counters->pl_rxftl =
		in_be64(&pport->pport_regs->bpfc.pl_rxftl[lport_index]);
	lport_counters->pl_rxo =
		in_be64(&pport->pport_regs->bpfc.pl_rxo[lport_index]);
	lport_counters->pl_rxbcp =
		in_be64(&pport->pport_regs->em.pl_rxbcp[lport_index]);
	lport_counters->pl_rxmcp =
		in_be64(&pport->pport_regs->em.pl_rxmcp[lport_index]);
	lport_counters->pl_rxucp =
		in_be64(&pport->pport_regs->em.pl_rxucp[lport_index]);
	lport_counters->pl_rxwdd =
		in_be64(&pport->pport_regs->em.pl_rxwdd[lport_index]);

	/* get tx lport registers */
	lport_counters->pl_txfd =
		in_be64(&pport->pport_regs->txm.pl_txfd[lport_index]);
	lport_counters->pl_txbcp =
		in_be64(&pport->pport_regs->txm.pl_txbcp[lport_index]);
	lport_counters->pl_txmcp =
		in_be64(&pport->pport_regs->txm.pl_txmcp[lport_index]);
	lport_counters->pl_txucp =
		in_be64(&pport->pport_regs->txm.pl_txucp[lport_index]);
	lport_counters->pl_txo =
		in_be64(&pport->pport_regs->txm.pl_txo[lport_index]);

	return rc;
}

static int _channel_pport_err_counters_get(struct rhea_channel *channel,
					   struct
					   hea_channel_pport_error_counters
					   *err_counters)
{
	int rc = 0;
	struct rhea_pport *pport;

	if (NULL == channel || NULL == err_counters)
		return -EINVAL;

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	/* Rx */
	err_counters->p_rxaln = in_be64(&pport->pport_regs->bpfc.p_rxaln);
	err_counters->p_rxbfcs = in_be64(&pport->pport_regs->bpfc.p_rxbfcs);
	err_counters->p_rxce = in_be64(&pport->pport_regs->bpfc.p_rxce);
	err_counters->p_rxfd = in_be64(&pport->pport_regs->bpfc.p_rxfd);
	err_counters->p_rxfrag = in_be64(&pport->pport_regs->bpfc.p_rxfrag);
	err_counters->p_rxftl = in_be64(&pport->pport_regs->bpfc.p_rxftl);
	err_counters->p_rxime = in_be64(&pport->pport_regs->bpfc.p_rxime);
	err_counters->p_rxjab = in_be64(&pport->pport_regs->bpfc.p_rxjab);
	err_counters->p_rxoerr = in_be64(&pport->pport_regs->bpfc.p_rxoerr);
	err_counters->p_rxorle = in_be64(&pport->pport_regs->bpfc.p_rxorle);
	err_counters->p_rxrf = in_be64(&pport->pport_regs->bpfc.p_rxrf);
	err_counters->p_rxrle = in_be64(&pport->pport_regs->bpfc.p_rxrle);
	err_counters->p_rxse = in_be64(&pport->pport_regs->bpfc.p_rxse);
	err_counters->p_rxuoc = in_be64(&pport->pport_regs->bpfc.p_rxuoc);

	/* Tx */
	err_counters->p_txbfcs = in_be64(&pport->pport_regs->txm.p_txbfcs);
	err_counters->p_txime = in_be64(&pport->pport_regs->txm.p_txime);
	err_counters->p_txrf = in_be64(&pport->pport_regs->txm.p_txrf);

	return rc;
}

static int _channel_pport_pause_frames_counters_get(
	struct rhea_channel *channel,
	struct hea_channel_pport_pause_frames_counters *pause_frame_counters)
{
	int rc = 0;
	struct rhea_pport *pport;

	if (NULL == channel || NULL == pause_frame_counters)
		return -EINVAL;

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	/* Rx */
	pause_frame_counters->p_rxcpf =
		in_be64(&pport->pport_regs->bpfc.p_rxcpf);

	/* Tx */
	pause_frame_counters->p_txcpf =
		in_be64(&pport->pport_regs->txm.p_txcpf);

	return rc;
}

static int _channel_pport_uc_mc_bc_counters_get(
	struct rhea_channel *channel,
	struct hea_channel_pport_uc_mc_bc_counters *uc_mc_bc_counters)
{
	int rc = 0;
	struct rhea_pport *pport;

	if (NULL == channel || NULL == uc_mc_bc_counters)
		return -EINVAL;

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	/* Rx */
	uc_mc_bc_counters->p_rxbcp = in_be64(&pport->pport_regs->bpfc.p_rxbcp);
	uc_mc_bc_counters->p_rxmcp = in_be64(&pport->pport_regs->bpfc.p_rxmcp);
	uc_mc_bc_counters->p_rxo = in_be64(&pport->pport_regs->bpfc.p_rxo);

	/* Tx */
	uc_mc_bc_counters->p_txbcp = in_be64(&pport->pport_regs->txm.p_txbcp);
	uc_mc_bc_counters->p_txmcp = in_be64(&pport->pport_regs->txm.p_txmcp);
	uc_mc_bc_counters->p_txo = in_be64(&pport->pport_regs->txm.p_txo);

	return rc;
}

static int _channel_pport_histogram_counters_get(
	struct rhea_channel *channel,
	struct hea_channel_pport_histogram_counters *histogram_counters)
{
	int rc = 0;
	struct rhea_pport *pport;

	if (NULL == channel || NULL == histogram_counters)
		return -EINVAL;

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	/* Rx */
	histogram_counters->p_rx64 = in_be64(&pport->pport_regs->bpfc.p_rx64);
	histogram_counters->p_rx65 = in_be64(&pport->pport_regs->bpfc.p_rx65);
	histogram_counters->p_rx128 =
		in_be64(&pport->pport_regs->bpfc.p_rx128);
	histogram_counters->p_rx256 =
		in_be64(&pport->pport_regs->bpfc.p_rx256);
	histogram_counters->p_rx512 =
		in_be64(&pport->pport_regs->bpfc.p_rx512);
	histogram_counters->p_rx1024 =
		in_be64(&pport->pport_regs->bpfc.p_rx1024);

	/* Tx */
	histogram_counters->p_tx64 = in_be64(&pport->pport_regs->txm.p_tx64);
	histogram_counters->p_tx65 = in_be64(&pport->pport_regs->txm.p_tx65);
	histogram_counters->p_tx128 = in_be64(&pport->pport_regs->txm.p_tx128);
	histogram_counters->p_tx256 = in_be64(&pport->pport_regs->txm.p_tx256);
	histogram_counters->p_tx512 = in_be64(&pport->pport_regs->txm.p_tx512);
	histogram_counters->p_tx1024 =
		in_be64(&pport->pport_regs->txm.p_tx1024);

	return rc;
}

int channel_counters_get(struct rhea_channel *channel,
			 enum hea_channel_counter_type counter_type,
			 struct hea_channel_counters *counter)
{
	int rc = 0;
	struct rhea_pport *pport;

	if (NULL == channel || NULL == counter)
		return -EINVAL;

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	spin_lock(&pport->lock);

	switch (counter_type) {
	case HEA_LPORT_COUNTERS:
		rc = _channel_lport_counters_get(channel,
						 &counter->lport_counter);
		if (rc)
			rhea_error("Was not able to get logical port "
				   "counter values");
		else
			counter->type = HEA_LPORT_COUNTERS;

		break;

	case HEA_PPORT_ERROR_COUNTERS:
		rc = _channel_pport_err_counters_get(channel,
						     &counter->pport_err);
		if (rc)
			rhea_error("Was not able to get pport error counter "
				   "values");
		else
			counter->type = HEA_PPORT_ERROR_COUNTERS;

		break;

	case HEA_PPORT_PAUSE_FRAMES_COUNTERS:
		rc = _channel_pport_pause_frames_counters_get(
			channel, &counter->pport_pause_frames);
		if (rc)
			rhea_error("Was not able to get pport pause frames "
				   "counter values");
		else
			counter->type = HEA_PPORT_PAUSE_FRAMES_COUNTERS;

		break;

	case HEA_PPORT_UC_MC_BC_COUNTERS:
		rc = _channel_pport_uc_mc_bc_counters_get(channel,
							  &counter->
							  pport_uc_mc_bc);
		if (rc)
			rhea_error("Was not able to get pport UC/MC/BC "
				   "counter values");
		else
			counter->type = HEA_PPORT_UC_MC_BC_COUNTERS;

		break;

	case HEA_PPORT_HISTOGRAM_COUNTERS:
		rc = _channel_pport_histogram_counters_get(channel,
							   &counter->
							   pport_histogram);
		if (rc)
			rhea_error("Was not able to get pport histogram "
				   "counter values");
		else
			counter->type = HEA_PPORT_HISTOGRAM_COUNTERS;

		break;

	default:
		rhea_error("This counter type is not supported");
		rc = -EINVAL;
	}

	spin_unlock(&pport->lock);

	return rc;
}

static int _channel_lport_register_reset(struct rhea_channel *channel)
{
	int rc = 0;
	int i;
	u64 reg;
	unsigned lport_index;
	struct rhea_pport *pport;

	if (NULL == channel)
		return -EINVAL;

	if (!is_hea_lport(channel->type)) {
		rhea_error("Only possible to execute this function for lports");
		return -EINVAL;
	}

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	/* get index of lport */
	lport_index = hea_lport_index_get(channel->type);

	/* reset lport mac */
	reg = 0x0ULL;
	out_be64(&pport->pport_regs->bpfc.pl_mac[lport_index], reg);

	/* set maximum packet size */
	reg = hea_set_u64_bits(0x0ULL, HEA_MAX_FRAME_SIZE, 50, 63);
	out_be64(&pport->pport_regs->bpfc.pl_mfsize[lport_index], reg);

	/* reset rx counter registers */
	reg = 0x0ULL;
	out_be64(&pport->pport_regs->bpfc.pl_rc[lport_index], reg);
	out_be64(&pport->pport_regs->bpfc.pl_rxerr[lport_index], reg);
	out_be64(&pport->pport_regs->bpfc.pl_rxfd[lport_index], reg);
	out_be64(&pport->pport_regs->bpfc.pl_rxftl[lport_index], reg);
	out_be64(&pport->pport_regs->bpfc.pl_rxo[lport_index], reg);
	out_be64(&pport->pport_regs->em.pl_rxbcp[lport_index], reg);
	out_be64(&pport->pport_regs->em.pl_rxmcp[lport_index], reg);
	out_be64(&pport->pport_regs->em.pl_rxucp[lport_index], reg);
	out_be64(&pport->pport_regs->em.pl_rxwdd[lport_index], reg);

	/* reset tx counter registers */
	out_be64(&pport->pport_regs->txm.pl_txfd[lport_index], reg);
	out_be64(&pport->pport_regs->txm.pl_txbcp[lport_index], reg);
	out_be64(&pport->pport_regs->txm.pl_txmcp[lport_index], reg);
	out_be64(&pport->pport_regs->txm.pl_txucp[lport_index], reg);
	out_be64(&pport->pport_regs->txm.pl_txo[lport_index], reg);

	/* reset vlan */

	/* reset vlan filter */
	reg = 0x0ULL;
	for (i = 0; i < HEA_LPORT_VLAN_FILTER_COUNT; ++i) {
		out_be64(&pport->pport_regs->pfc.pl_vlanf[lport_index][i],
			 reg);
	}

	return rc;
}

static int _channel_uc_mc_bc_register_reset(struct rhea_channel *channel)
{
	int rc = 0;
	u64 reg;
	struct rhea_pport *pport;

	if (NULL == channel)
		return -EINVAL;

	if (is_hea_lport(channel->type)) {
		rhea_error("Only possible to execute this function for lports");
		return -EINVAL;
	}

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	switch (channel->type) {
	case HEA_UC_PORT:
		reg = 0x0ULL;
		out_be64(&pport->pport_regs->bpfc.p_rcu, reg);

		reg = hea_set_u64_bits(0x0ULL, HEA_MAX_FRAME_SIZE, 50, 63);
		out_be64(&pport->pport_regs->bpfc.p_mfsizeu, reg);
		break;

	case HEA_MC_PORT:
		reg = 0x0ULL;
		out_be64(&pport->pport_regs->bpfc.p_rcm, reg);

		reg = hea_set_u64_bits(0x0ULL, HEA_MAX_FRAME_SIZE, 50, 63);
		out_be64(&pport->pport_regs->bpfc.p_mfsizem, reg);
		break;

	case HEA_BC_PORT:
		reg = 0x0ULL;
		out_be64(&pport->pport_regs->bpfc.p_rcb, reg);

		reg = hea_set_u64_bits(0x0ULL, HEA_MAX_FRAME_SIZE, 50, 63);
		out_be64(&pport->pport_regs->bpfc.p_mfsizeb, reg);
		break;

	default:
		rhea_error("Should not be able to get here!");
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int _channel_register_reset(struct rhea_channel *channel)
{
	int rc = 0;

	if (NULL == channel)
		return -EINVAL;

	if (is_hea_lport(channel->type)) {
		rc = _channel_lport_register_reset(channel);
		if (rc)
			rhea_error("Was not able to reset lport registers");
	} else {
		rc = _channel_uc_mc_bc_register_reset(channel);
		if (rc)
			rhea_error("Was not able to reset default channel "
				   "registers");
	}

	return rc;
}

static inline int _rhea_channel_alloc(struct rhea_channel **pp_channel,
				      const struct hea_channel_cfg
				      *channel_cfg)
{
	int rc = 0;
	struct rhea_channel *channel = NULL;

	channel = rhea_align_alloc(sizeof(*channel), 8, GFP_KERNEL);
	if (NULL == channel) {
		rhea_error("Not able to allocate memory for channel: "
			   "%u and physical port: %u!",
			   (int) channel_cfg->type, channel_cfg->pport_nr + 1);
		return -ENOMEM;
	}

	memset(channel, 0, sizeof(*channel));

	/* config channel */
	channel->channel_cfg = *channel_cfg;
	channel->type = channel_cfg->type;
	channel->pport_nr = channel_cfg->pport_nr;

	/* everything went fine, now we can pass the poitner back */
	*pp_channel = channel;

	return rc;
}

static inline void _rhea_channel_free(struct rhea_channel *channel)
{
	if (NULL == channel)
		return;

	rhea_align_free(channel, sizeof(*channel));
}

int _rhea_channel_destroy(struct rhea_channel *channel)
{
	int rc = 0;

	struct rhea_pport *pport;

	if (NULL == channel)
		return -EINVAL;

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	if (RHEA_CHANNEL_ENABLED == channel->state &&
	    is_hea_lport(channel->type)) {
		/* make sure that nobody is able to talk to this
		 * channel anymore */
		rc = _rhea_channel_stop(channel);
		if (rc) {
			rhea_error("Was not able to stop the channel: "
				   "%u on port: %u",
				   channel->type, channel->pport_nr + 1);
		}
	}

	rhea_info("Destroy channel %u on PPORT[%u] with id: %u",
		  channel->type, pport->port_cfg.pport_nr + 1, channel->id);

	/* shared channels should not get near registers and real channels */
	if (is_hea_lport(channel->type) ||
	    HEA_DEFAULT_CHANNEL_SHARE !=
	    channel->channel_cfg.dc.channel_usuage) {
		/* make sure that TCAM resources are freed */
		if (NULL != channel->tcam && channel->tcam->alloced) {
			rc = _rhea_tcam_free_all(channel);
			if (rc)
				rhea_error("Was not able to free TCAM");
		}

		/* make sure that QPN resources are freed */
		if (NULL != channel->qpn && channel->qpn->alloced) {
			rc = _rhea_qpn_free(channel);
			if (rc)
				rhea_error("Was not able to free QPN");
		}

		/* delete hasher */
		if (channel->hasher_used) {
			rc = _rhea_hasher_free(channel);
			if (rc)
				rhea_error("Was not able to free hasher");
		}

		/* make sure that all channel registers are reset */
		rc = _channel_register_reset(channel);
		if (rc)
			rhea_error("Was not able to reset channel "
				   "registers to default values!");

		pport->channel[channel->type] = NULL;
	}

	_rhea_channel_free(channel);

	/* checks if the port should be disabled */
	_rhea_pport_disable_check(pport);

	return rc;
}

int rhea_channel_destroy(struct rhea_channel *channel)
{
	int rc = 0;

	struct rhea_pport *pport;

	if (NULL == channel)
		return -EINVAL;

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	spin_lock(&pport->lock);

	rc = _rhea_channel_destroy(channel);
	if (rc) {
		spin_unlock(&pport->lock);
		rhea_error("Was not able to free channel: %u on port: %u",
			   channel->type, channel->pport_nr + 1);
		return rc;
	}

	spin_unlock(&pport->lock);

	return rc;
}

static struct rhea_channel *_rhea_channel_base_create(
	struct hea_channel_cfg *channel_cfg,
	enum hea_default_channel_usuage channel_usage)
{
	int rc = 0;
	struct rhea_pport *pport;
	struct rhea_channel *channel = NULL;

	if (NULL == channel_cfg)
		goto out;

	pport = _rhea_pport_get(channel_cfg->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		goto out;
	}

	if (0 == _rhea_channel_avail(channel_cfg->pport_nr, channel_cfg->type,
				     HEA_DEFAULT_CHANNEL_SHARE ==
				     channel_usage ? 1 : 0)) {
		rhea_error("Channel is not available");
		goto out;
	}

	rc = _rhea_channel_alloc(&channel, channel_cfg);
	if (rc) {
		rhea_error("Was not able to allocate channel: %u", rc);
		goto out;
	}

	channel->id =
		_rhea_channel_create_id(channel_cfg->pport_nr, channel_cfg);

	if (HEA_DEFAULT_CHANNEL_SHARE != channel_usage) {
		rc = _rhea_channel_map_init(&channel->qpn);
		if (rc) {
			rhea_error("Error when initializing QPN element");
			goto out;
		}

		rc = _rhea_channel_map_init(&channel->tcam);
		if (rc) {
			rhea_error("Error when initializing TCAM element");
			goto out;
		}

		/* set maximum packet size */
		rc = _rhea_channel_max_frame_size_set(channel,
						      channel->channel_cfg.
						      max_frame_size);
		if (rc) {
			rhea_error
				("Was not able to set the maximum frame size");
			goto out;
		}

		/* set vlan settings */
		rc = _rhea_channel_vlan_set(channel, &channel_cfg->vlan);
		if (rc) {
			rhea_error("Was not able to enable vlan support!");
			goto out;
		}

		/* save channel in local memory */
		pport->channel[channel_cfg->type] = channel;

		/* all of them are used by the default port config */
	}

	return channel;

out:
	if (channel)
		_rhea_channel_destroy(channel);

	return NULL;
}

static struct rhea_channel *_rhea_channel_create_lport(struct hea_channel_cfg
						       *channel_cfg)
{
	int rc = 0;
	struct rhea_pport *pport;
	union hea_mac_addr mac_lport;
	struct rhea_channel *channel = NULL;
	struct hea_channel_cfg channel_cfg_cp;

	if (NULL == channel_cfg || !is_hea_lport(channel_cfg->type))
		goto out;

	pport = _rhea_pport_get(channel_cfg->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		goto out;
	}

	/* make copy of channel config */
	channel_cfg_cp = *channel_cfg;

	if (HEA_LPORT == channel_cfg_cp.type) {
		int type = 0;

		for (type = HEA_LPORT_0; type <= HEA_LPORT_3; ++type) {
			if (_rhea_channel_avail
			    (channel_cfg_cp.pport_nr, type, 0))
				break;
		}

		if (!is_hea_lport(type)) {
			rhea_error("Was not able to find free logical port");
			goto out;
		}

		rhea_debug("looks for lport: %u", type);

		/* even if it did not work out, the next instance will
		 * check for us */
		channel_cfg_cp.type = type;
	}

	channel = _rhea_channel_base_create(&channel_cfg_cp,
					  HEA_DEFAULT_CHANNEL_ALONE);
	if (NULL == channel) {
		rhea_error("Was not able to allocate channel");
		goto out;
	}

	if (1 == pport->port_cfg.mac_lport_count_max &&
	    HEA_LPORT_0 != channel->type) {

		rhea_warning("WARNING: This system was not equipped with enough"
			     " MAC addresses to use more than 1 logical port!");
		rhea_warning("WARNING: All logical ports apart from "
			     "logical port 0 are DISABLED");
		rhea_warning("!! Please contact your system administrator !!");

		rhea_error("Was not able to obtain logical port: %u "
			   "on physical port: %u",
			   HEA_LPORT_0 - channel->type, channel->pport_nr + 1);

		goto out;
	}

	/* check if mac address is specified */
	if (channel->channel_cfg.lport.mac_address._be64) {
		mac_lport._be64 = channel->channel_cfg.lport.mac_address._be64;
	} else {
		/* reset */
		mac_lport._be64 = 0;

		/* get MAC address which is computed in case it is not set */
		rc = _rhea_channel_mac_get(channel, &mac_lport);
		if (rc) {
			rhea_error("Was not able to get MAC address");
			goto out;
		}
	}

	/* set MAC for logical port */
	rc = _rhea_channel_mac_set(channel, &mac_lport);
	if (rc) {
		rhea_error("Was not able to set the MAC address");
		goto out;
	}

	/* compute lport number */
	channel->channel_cfg.lport.lport_nr =
		hea_lport_index_get(channel->type);

	return channel;

out:
	if (channel)
		_rhea_channel_destroy(channel);

	return NULL;
}

struct rhea_channel *rhea_channel_uc_mc_bc_create(struct hea_channel_cfg
						  *channel_cfg)
{
	struct rhea_channel *channel = NULL;

	if (NULL == channel_cfg)
		return NULL;

	channel =
		_rhea_channel_base_create(channel_cfg,
					  channel_cfg->dc.channel_usuage);
	if (NULL == channel)
		rhea_error("Could not create BC/MC/UC channel");

	return channel;
}

struct rhea_channel *rhea_channel_create(struct hea_channel_cfg *channel_cfg)
{
	struct rhea_pport *pport;
	struct rhea_channel *channel = NULL;

	if (NULL == channel_cfg)
		return NULL;

	pport = _rhea_pport_get(channel_cfg->pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return NULL;
	}

	rhea_info("Allocate channel %u on PPORT[%u]",
		  channel_cfg->type, channel_cfg->pport_nr + 1);

	spin_lock(&pport->lock);

	switch (channel_cfg->type) {
	case HEA_UC_PORT:
	case HEA_MC_PORT:
	case HEA_BC_PORT:
		channel = rhea_channel_uc_mc_bc_create(channel_cfg);
		break;

	case HEA_LPORT:
	case HEA_LPORT_0:
	case HEA_LPORT_1:
	case HEA_LPORT_2:
	case HEA_LPORT_3:
		channel = _rhea_channel_create_lport(channel_cfg);
		break;

	default:
		rhea_error("Channel type is not supported");
	}

	spin_unlock(&pport->lock);

	return channel;

}

enum rhea_channel_state rhea_pport_state(unsigned int pport_nr)
{
	enum rhea_channel_state state;
	struct rhea_pport *pport;

	pport = _rhea_pport_get(pport_nr);
	if (NULL == pport) {
		rhea_error("Invalid pport number");
		return -EINVAL;
	}

	spin_lock(&pport->lock);

	state = pport->state;

	spin_unlock(&pport->lock);

	/* return */
	return state;
}


static int _rhea_pport_alloc_init(struct rhea_pport **pport,
				  unsigned pport_nr)
{
	int rc = 0;

	if (!(0 <= pport_nr && HEA_MAX_PPORT_COUNT > pport_nr)) {
		rhea_error("Invalid physical port number");
		return -EINVAL;
	}

	*pport = rhea_align_alloc(sizeof(**pport), 8, GFP_KERNEL);
	if (NULL == pport) {
		rhea_error("Was not able to allocate a pport resource");
		return -EINVAL;
	}

	memset(*pport, 0, sizeof(**pport));

	spin_lock_init(&(*pport)->lock);

	return rc;
}

static int _rhea_pport_fini(struct rhea_pport *pport)
{
	int i;
	int rc = 0;

	if (NULL == pport)
		return -EINVAL;

	rc = _rhea_pport_disable(pport);
	if (rc)
		rhea_error("Was not able to disable the PPORT[0]");

	for (i = 0; i < ARRAY_SIZE(pport->channel); ++i) {
		if (NULL != pport->channel[i]) {
			rc = _rhea_channel_destroy(pport->channel[i]);
			if (rc) {
				rhea_error("Error when cleaning channel "
					   "resources!");
			}
		}
	}

	/* do these things after the channels are destroyed */

	rc = _rhea_tcam_fini(pport);
	if (rc)
		rhea_error("Was not able to free all TCAM resources!");

	rc = _rhea_qpn_fini(pport);
	if (rc)
		rhea_error("Was not able to free all QPN resources!");

	/* make sure that all registers are set to default values */
	_rhea_port_register_reset(pport);

	/* make sure that this object does not exist anymore! */
	rhea_align_free(pport, sizeof(*pport));

	return rc;
}

int rhea_pport_fini(unsigned pport_nr)
{
	int rc = 0;
	struct rhea_pport *pport;

	pport = _rhea_pport_get(pport_nr);
	if (NULL == pport) {
		rhea_error("Could not get pport");
		return -EINVAL;
	}

	/* make sure nobody else tries to work on the port */
	spin_lock(&pport->lock);

	rc = _rhea_pport_fini(pport);
	if (rc) {
		rhea_error("Was not able to free PPORT[%u]", pport_nr + 1);
		return rc;
	}

	return rc;
}

int rhea_pport_init(struct rhea_gen *rhea_gen, struct hea_pport_cfg *pport)
{
	int rc = 0;
	unsigned pport_nr;

	if (NULL == rhea_gen || NULL == pport) {
		rhea_error("Invalid parameters passed in");
		return -EINVAL;
	}

	rhea_debug("pport_init()");

	if (pport->pport_nr < 0 || pport->pport_nr >= HEA_MAX_PPORT_COUNT) {
		rhea_error("bad pport number %d", pport->pport_nr);
		return -ENXIO;
	}

	/* get port number */
	pport_nr = pport->pport_nr;

	if (NULL == s_pports.pports[pport_nr]) {
		rc = _rhea_pport_alloc_init(&s_pports.pports[pport_nr],
					    pport_nr);
		if (rc) {
			rhea_error("Was not able to allocate new pport[%u] "
				   "resources", pport_nr + 1);
			return rc;
		}

		/* save physical port configuration */
		s_pports.pports[pport_nr]->port_cfg = *pport;

		/* save pport mmio address */
		s_pports.pports[pport_nr]->pport_regs =
			&rhea_gen->pport[pport_nr];

		spin_lock_init(&s_pports.pports[pport_nr]->lock);

		spin_lock(&s_pports.pports[pport_nr]->lock);

		/* make sure that all registers are set to default values */
		_rhea_port_register_reset(s_pports.pports[pport_nr]);

		/* configure MAC addresses for physical port */
		rc = _rhea_pport_eth_mac_set(&rhea_gen->base,
					     s_pports.pports[pport_nr]);
		if (rc) {
			spin_unlock(&s_pports.pports[pport_nr]->lock);
			rhea_error("Could not set pport mac address");
			return rc;
		}

		rc = _rhea_qpn_init(s_pports.pports[pport_nr]);
		if (rc) {
			spin_unlock(&s_pports.pports[pport_nr]->lock);
			rhea_error
				("Was not able to initialise the QPN array!");
			return rc;
		}

		rc = _rhea_tcam_init(s_pports.pports[pport_nr]);
		if (rc) {
			spin_unlock(&s_pports.pports[pport_nr]->lock);
			rhea_error
				("Was not able to initialise the TCAM array!");
			return rc;
		}

		rc = _rhea_hasher_init(s_pports.pports[pport_nr]);
		if (rc) {
			spin_unlock(&s_pports.pports[pport_nr]->lock);
			rhea_error("Was not able to initialise the HASHER!");
			return rc;
		}

		s_pports.pports[pport_nr]->state = RHEA_CHANNEL_INIT;

		spin_unlock(&s_pports.pports[pport_nr]->lock);
	}

	return rc;
}

static void _rhea_pport_tx_error_dump(struct rhea_pport *pport)
{
	struct rhea_pport_txm *t;
	unsigned p;

	if (NULL == pport)
		return;

	t = &pport->pport_regs->txm;
	p = pport->port_cfg.pport_nr;

	rhea_reg_print(t, p_txbfcs, "P[%d]_TXBPFCS", p);
	rhea_reg_print(t, p_txlf, "P[%d]_TXLF", p);
	rhea_reg_print(t, p_txrf, "P[%d]_TXRF", p);
	rhea_reg_print(t, p_txime, "P[%d]_TXIME", p);
	rhea_reg_print(t, p_txcpf, "P[%d]_TXCPF", p);
}

static void _rhea_pport_tx_error_reset(struct rhea_pport *pport)
{
	struct rhea_pport_txm *t;

	if (NULL == pport)
		return;

	t = &pport->pport_regs->txm;

	out_be64(&t->p_txbfcs, 0x0ULL);
	out_be64(&t->p_txlf, 0x0ULL);
	out_be64(&t->p_txrf, 0x0ULL);
	out_be64(&t->p_txime, 0x0ULL);
	out_be64(&t->p_txcpf, 0x0ULL);
}

static void _rhea_pport_rx_error_dump(struct rhea_pport *pport)
{
	struct rhea_pport_bpfc *b;
	unsigned l;
	unsigned p;

	if (NULL == pport)
		return;

	b = &pport->pport_regs->bpfc;
	p = pport->port_cfg.pport_nr;

	for (l = 0; l < ARRAY_SIZE(b->pl_rxerr); l++)
		rhea_reg_print(b, pl_rxerr[l], "P[%d]L[%d]_RXERR", p, l);

	for (l = 0; l < ARRAY_SIZE(b->pl_rxfd); l++)
		rhea_reg_print(b, pl_rxfd[l], "P[%d]L[%d]_RXFD", p, l);

	rhea_reg_print(b, p_rxse, "P[%d]_RXSE", p);
	rhea_reg_print(b, p_rxce, "P[%d]_RXCE", p);
	rhea_reg_print(b, p_rxjab, "P[%d]_RXJAB", p);
	rhea_reg_print(b, p_rxfrag, "P[%d]_RXFRAG", p);
	rhea_reg_print(b, p_rxbfcs, "P[%d]_RXBFCS", p);
	rhea_reg_print(b, p_rxrle, "P[%d]_RXRLE", p);
	rhea_reg_print(b, p_rxorle, "P[%d]_RXORLE", p);
	rhea_reg_print(b, p_rxrf, "P[%d]_RXRF", p);
	rhea_reg_print(b, p_rxoerr, "P[%d]_RXOERR", p);
	rhea_reg_print(b, p_rxuoc, "P[%d]_RXUOC", p);
	rhea_reg_print(b, p_rxcpf, "P[%d]_RXCPF", p);
	rhea_reg_print(b, p_rxime, "P[%d]_RXIME", p);
	rhea_reg_print(b, p_rxfd, "P[%d]_RXFD", p);
	rhea_reg_print(b, p_rxaln, "P[%d]_RXALN", p);

}

static void _rhea_pport_rx_error_reset(struct rhea_pport *pport)
{
	struct rhea_pport_bpfc *b;
	unsigned l;

	if (NULL == pport)
		return;

	b = &pport->pport_regs->bpfc;

	for (l = 0; l < ARRAY_SIZE(b->pl_rxerr); l++)
		out_be64(&b->pl_rxerr[l], 0x0ULL);

	for (l = 0; l < ARRAY_SIZE(b->pl_rxfd); l++)
		out_be64(&b->pl_rxfd[l], 0x0ULL);

	out_be64(&b->p_rxse, 0x0ULL);
	out_be64(&b->p_rxce, 0x0ULL);
	out_be64(&b->p_rxjab, 0x0ULL);
	out_be64(&b->p_rxfrag, 0x0ULL);
	out_be64(&b->p_rxbfcs, 0x0ULL);
	out_be64(&b->p_rxrle, 0x0ULL);
	out_be64(&b->p_rxorle, 0x0ULL);
	out_be64(&b->p_rxrf, 0x0ULL);
	out_be64(&b->p_rxoerr, 0x0ULL);
	out_be64(&b->p_rxuoc, 0x0ULL);
	out_be64(&b->p_rxcpf, 0x0ULL);
	out_be64(&b->p_rxime, 0x0ULL);
	out_be64(&b->p_rxfd, 0x0ULL);
	out_be64(&b->p_rxaln, 0x0ULL);
}

static void _rhea_pport_tx_counters_dump(struct rhea_pport *pport)
{
	struct rhea_pport_txm *t;
	unsigned p;

	if (NULL == pport)
		return;

	t = &pport->pport_regs->txm;
	p = pport->port_cfg.pport_nr;

	rhea_reg_print(t, p_txo, "P[%d]_TXO", p);
	rhea_reg_print(t, p_txbcp, "P[%d]_TXBCP", p);
	rhea_reg_print(t, p_txmcp, "P[%d]_TXMCP", p);
}

static void _rhea_pport_tx_counters_reset(struct rhea_pport *pport)
{
	struct rhea_pport_txm *t;

	if (NULL == pport)
		return;

	t = &pport->pport_regs->txm;

	out_be64(&t->p_txo, 0x0ULL);
	out_be64(&t->p_txbcp, 0x0ULL);
	out_be64(&t->p_txmcp, 0x0ULL);
}

static void _rhea_pport_rx_counters_dump(struct rhea_pport *pport)
{
	struct rhea_pport_bpfc *b;
	unsigned p;

	if (NULL == pport)
		return;

	b = &pport->pport_regs->bpfc;
	p = pport->port_cfg.pport_nr;

	rhea_reg_print(b, p_rxo, "P[%d]_RXO", p);
	rhea_reg_print(b, p_rxbcp, "P[%d]_RXBCP", p);
	rhea_reg_print(b, p_rxmcp, "P[%d]_RXMCP", p);
}

static void _rhea_pport_rx_counters_reset(struct rhea_pport *pport)
{
	struct rhea_pport_bpfc *b;

	if (NULL == pport)
		return;

	b = &pport->pport_regs->bpfc;

	out_be64(&b->p_rxo, 0x0ULL);
	out_be64(&b->p_rxbcp, 0x0ULL);
	out_be64(&b->p_rxmcp, 0x0ULL);
}

static void _rhea_pport_tx_histogram_dump(struct rhea_pport *pport)
{
	struct rhea_pport_txm *t;
	unsigned p;

	if (NULL == pport)
		return;

	t = &pport->pport_regs->txm;
	p = pport->port_cfg.pport_nr;

	rhea_reg_print(t, p_tx64, "P[%d]_TX64", p);
	rhea_reg_print(t, p_tx65, "P[%d]_TX65", p);
	rhea_reg_print(t, p_tx128, "P[%d]_TX128", p);
	rhea_reg_print(t, p_tx256, "P[%d]_TX246", p);
	rhea_reg_print(t, p_tx512, "P[%d]_TX512", p);
	rhea_reg_print(t, p_tx1024, "P[%d]_TX1024", p);
}

static void _rhea_pport_tx_histogram_reset(struct rhea_pport *pport)
{
	struct rhea_pport_txm *t;

	if (NULL == pport)
		return;

	t = &pport->pport_regs->txm;

	out_be64(&t->p_tx64, 0x0ULL);
	out_be64(&t->p_tx65, 0x0ULL);
	out_be64(&t->p_tx128, 0x0ULL);
	out_be64(&t->p_tx256, 0x0ULL);
	out_be64(&t->p_tx512, 0x0ULL);
	out_be64(&t->p_tx1024, 0x0ULL);
}

static void _rhea_pport_rx_histogram_dump(struct rhea_pport *pport)
{
	struct rhea_pport_bpfc *b;
	unsigned p;

	if (NULL == pport)
		return;

	b = &pport->pport_regs->bpfc;
	p = pport->port_cfg.pport_nr;

	rhea_reg_print(b, p_rx64, "P[%d]_RX64", p);
	rhea_reg_print(b, p_rx65, "P[%d]_RX65", p);
	rhea_reg_print(b, p_rx128, "P[%d]_RX128", p);
	rhea_reg_print(b, p_rx256, "P[%d]_RX246", p);
	rhea_reg_print(b, p_rx512, "P[%d]_RX512", p);
	rhea_reg_print(b, p_rx1024, "P[%d]_RX1024", p);
	rhea_reg_print(b, p_rxftl, "P[%d]_RXFTL", p);
}

static void _rhea_pport_rx_histogram_reset(struct rhea_pport *pport)
{
	struct rhea_pport_bpfc *b;

	if (NULL == pport)
		return;

	b = &pport->pport_regs->bpfc;

	out_be64(&b->p_rx64, 0x0ULL);
	out_be64(&b->p_rx65, 0x0ULL);
	out_be64(&b->p_rx128, 0x0ULL);
	out_be64(&b->p_rx256, 0x0ULL);
	out_be64(&b->p_rx512, 0x0ULL);
	out_be64(&b->p_rx1024, 0x0ULL);
	out_be64(&b->p_rxftl, 0x0ULL);
}

static void _rhea_pport_tx_logical_counters_dump(struct rhea_pport *pport)
{
	struct rhea_pport_txm *t;
	unsigned l;
	unsigned p;

	if (NULL == pport)
		return;

	t = &pport->pport_regs->txm;
	p = pport->port_cfg.pport_nr;

	for (l = 0; l < ARRAY_SIZE(t->pl_txo); l++)
		rhea_reg_print(t, pl_txo[l], "P[%d]L[%d]_TXO", p, l);

	for (l = 0; l < ARRAY_SIZE(t->pl_txucp); l++)
		rhea_reg_print(t, pl_txucp[l], "P[%d]L[%d]_TXUCP", p, l);

	for (l = 0; l < ARRAY_SIZE(t->pl_txmcp); l++)
		rhea_reg_print(t, pl_txmcp[l], "P[%d]L[%d]_TXMCP", p, l);

	for (l = 0; l < ARRAY_SIZE(t->pl_txbcp); l++)
		rhea_reg_print(t, pl_txbcp[l], "P[%d]L[%d]_TXBCP", p, l);
}

static void _rhea_pport_tx_logical_counters_reset(struct rhea_pport *pport)
{
	struct rhea_pport_txm *t;
	unsigned l;

	if (NULL == pport)
		return;

	t = &pport->pport_regs->txm;

	for (l = 0; l < ARRAY_SIZE(t->pl_txo); l++)
		out_be64(&t->pl_txo[l], 0x0ULL);

	for (l = 0; l < ARRAY_SIZE(t->pl_txucp); l++)
		out_be64(&t->pl_txucp[l], 0x0ULL);

	for (l = 0; l < ARRAY_SIZE(t->pl_txmcp); l++)
		out_be64(&t->pl_txmcp[l], 0x0ULL);

	for (l = 0; l < ARRAY_SIZE(t->pl_txbcp); l++)
		out_be64(&t->pl_txbcp[l], 0x0ULL);
}

static void _rhea_pport_rx_logical_counters_dump(struct rhea_pport *pport)
{
	struct rhea_pport_em *m;
	struct rhea_pport_bpfc *b;
	unsigned l;
	unsigned p;

	if (NULL == pport)
		return;

	m = &pport->pport_regs->em;
	b = &pport->pport_regs->bpfc;
	p = pport->port_cfg.pport_nr;

	for (l = 0; l < ARRAY_SIZE(b->pl_rxo); l++)
		rhea_reg_print(b, pl_rxo[l], "P[%d]L[%d]_RXO", p, l);

	for (l = 0; l < ARRAY_SIZE(b->pl_rxftl); l++)
		rhea_reg_print(b, pl_rxftl[l], "P[%d]L[%d]_RXFTL", p, l);

	for (l = 0; l < ARRAY_SIZE(m->pl_rxucp); l++)
		rhea_reg_print(m, pl_rxucp[l], "P[%d]L[%d]_RXUCP", p, l);

	for (l = 0; l < ARRAY_SIZE(m->pl_rxmcp); l++)
		rhea_reg_print(m, pl_rxmcp[l], "P[%d]L[%d]_RXMCP", p, l);

	for (l = 0; l < ARRAY_SIZE(m->pl_rxbcp); l++)
		rhea_reg_print(m, pl_rxbcp[l], "P[%d]L[%d]_RXBCP", p, l);
}

static void _rhea_pport_rx_logical_counters_reset(struct rhea_pport *pport)
{

	struct rhea_pport_em *m;
	struct rhea_pport_bpfc *b;
	unsigned l;

	if (NULL == pport)
		return;

	m = &pport->pport_regs->em;
	b = &pport->pport_regs->bpfc;

	for (l = 0; l < ARRAY_SIZE(b->pl_rxo); l++)
		out_be64(&b->pl_rxo[l], 0x0ULL);

	for (l = 0; l < ARRAY_SIZE(b->pl_rxftl); l++)
		out_be64(&b->pl_rxftl[l], 0x0ULL);

	for (l = 0; l < ARRAY_SIZE(m->pl_rxucp); l++)
		out_be64(&m->pl_rxucp[l], 0x0ULL);

	for (l = 0; l < ARRAY_SIZE(m->pl_rxmcp); l++)
		out_be64(&m->pl_rxmcp[l], 0x0ULL);

	for (l = 0; l < ARRAY_SIZE(m->pl_rxbcp); l++)
		out_be64(&m->pl_rxbcp[l], 0x0ULL);
}

static void _rhea_pport_bpfc_dump(struct rhea_pport *pport)
{
	struct rhea_pport_bpfc *b;
	unsigned l;
	unsigned p;

	if (NULL == pport)
		return;

	b = &pport->pport_regs->bpfc;
	p = pport->port_cfg.pport_nr;

	rhea_reg_print(b, p_rc, "P[%d]_RC", p);

	for (l = 0; l < ARRAY_SIZE(b->pl_mac); l++)
		rhea_reg_print(b, pl_mac[l], "P[%d]L[%d]_MAC", p, l);

	rhea_reg_print(b, pg_trcpfc, "P[%d][0]_TRCPFC", p);

	for (l = 0; l < ARRAY_SIZE(b->pl_rc); l++)
		rhea_reg_print(b, pl_rc[l], "P[%d]L[%d]_RC", p, l);

	rhea_reg_print(b, p_rcb, "P[%d]_RCB", p);
	rhea_reg_print(b, p_rcm, "P[%d]_RCM", p);
	rhea_reg_print(b, p_rcu, "P[%d]_RCU", p);

	for (l = 0; l < ARRAY_SIZE(b->pl_qosm); l++)
		rhea_reg_print(b, pl_qosm[l], "P[%d]L[%d]_QOSM", p, l);

	rhea_reg_print(b, p_qosm, "P[%d]_QOSM", p);

	for (l = 0; l < ARRAY_SIZE(b->pg_qpn); l++)
		rhea_reg_print(b, pg_qpn[l], "P[%d][0]_QPN[%d]", p, l);

	for (l = 0; l < ARRAY_SIZE(b->pg_hashm); l++)
		rhea_reg_print(b, pg_hashm[l], "P[%d][0]_HASHM[%d]", p, l);

	for (l = 0; l < ARRAY_SIZE(b->pg_tcampr); l++)
		rhea_reg_print(b, pg_tcampr[l], "P[%d][0]_TCAMPR[%d]", p, l);

	for (l = 0; l < ARRAY_SIZE(b->pg_tcamm); l++)
		rhea_reg_print(b, pg_tcamm[l], "P[%d][0]_TCAMM[%d]", p, l);

	for (l = 0; l < ARRAY_SIZE(b->pl_mfsize); l++)
		rhea_reg_print(b, pl_mfsize[l], "P[%d]L[%d]_MFSIZE", p, l);

	rhea_reg_print(b, p_mfsizeb, "P[%d]_MFSIZE", p);
	rhea_reg_print(b, p_mfsizem, "P[%d]_MFSIZE", p);
	rhea_reg_print(b, p_mfsizeu, "P[%d]_MFSIZE", p);

	for (l = 0; l < ARRAY_SIZE(b->pg_rrange); l++)
		rhea_reg_print(b, pg_rrange[l], "P[%d][0]_RRANGE[%d]", p, l);
}

static void _rhea_pport_bpfc_reset(struct rhea_pport *pport)
{
	u64 reg;
	unsigned l;
	struct rhea_pport_bpfc *b;

	if (NULL == pport)
		return;

	b = &pport->pport_regs->bpfc;

	out_be64(&b->p_rc, 0x0ULL);

	for (l = 0; l < ARRAY_SIZE(b->pl_mac); l++)
		out_be64(&b->pl_mac[l], 0x0ULL);

	out_be64(&b->pg_trcpfc, 0x0ULL);

	for (l = 0; l < ARRAY_SIZE(b->pl_rc); l++)
		out_be64(&b->pl_rc[l], 0x0ULL);

	out_be64(&b->p_rcb, 0x0ULL);
	out_be64(&b->p_rcm, 0x0ULL);
	out_be64(&b->p_rcu, 0x0ULL);

	for (l = 0; l < ARRAY_SIZE(b->pl_qosm); l++)
		out_be64(&b->pl_qosm[l], 0x0ULL);

	out_be64(&b->p_qosm, 0x0ULL);

	for (l = 0; l < ARRAY_SIZE(b->pg_qpn); l++)
		out_be64(&b->pg_qpn[l], 0x0ULL);

	for (l = 0; l < ARRAY_SIZE(b->pg_hashm); l++)
		out_be64(&b->pg_hashm[l], ~(0x0ULL));

	for (l = 0; l < ARRAY_SIZE(b->pg_tcampr); l++)
		out_be64(&b->pg_tcampr[l], 0x0ULL);

	for (l = 0; l < ARRAY_SIZE(b->pg_tcamm); l++)
		out_be64(&b->pg_tcamm[l], 0x0ULL);

	reg = 0x0ULL;
	reg = hea_set_u64_bits(reg, HEA_MAX_FRAME_SIZE, 50, 63);
	for (l = 0; l < ARRAY_SIZE(b->pl_mfsize); l++)
		out_be64(&b->pl_mfsize[l], reg);

	out_be64(&b->p_mfsizeb, reg);
	out_be64(&b->p_mfsizem, reg);
	out_be64(&b->p_mfsizeu, reg);

	for (l = 0; l < ARRAY_SIZE(b->pg_rrange); l++)
		out_be64(&b->pg_rrange[l], 0x0ULL);
}

static void _rhea_pport_mmc_dump(struct rhea_pport *pport)
{
	unsigned p;
	struct rhea_pport_mmc *m;

	if (NULL == pport)
		return;

	m = &pport->pport_regs->mmc;
	p = pport->port_cfg.pport_nr;

	rhea_reg_print(m, p_uaa, "P[%d]_UAA", p);
	rhea_reg_print(m, p_macvc, "P[%d]_MACVC", p);

	rhea_reg_print(m, p_macc, "P[%d]_MACC", p);
	rhea_reg_print(m, p_pst, "P[%d]_PST", p);
	rhea_reg_print(m, p_pc, "P[%d]_PC", p);
	rhea_reg_print(m, p_mma, "P[%d]_MMA", p);
	rhea_reg_print(m, p_xpcsc, "P[%d]_XPCSC", p);
	rhea_reg_print(m, p_xpcsp, "P[%d]_XPCSP", p);
	rhea_reg_print(m, p_xpcsst, "P[%d]_XPCSST", p);
	rhea_reg_print(m, p_pcsid, "P[%d]_PCSID", p);
	rhea_reg_print(m, p_spcsc, "P[%d]_SPCSC", p);
	rhea_reg_print(m, p_spcsst, "P[%d]_SPCSST", p);
	rhea_reg_print(m, pg_ec, "P[%d][0]_EC", p);
	rhea_reg_print(m, pg_est, "P[%d][0]_EST", p);
}

static void _rhea_pport_mmc_reset(struct rhea_pport *pport)
{
	u64 reg;
	struct rhea_pport_mmc *m;

	if (NULL == pport)
		return;

	m = &pport->pport_regs->mmc;

	out_be64(&m->p_uaa, 0x0ULL);

	reg = 0x0ULL;
	reg = hea_set_u64_bits(reg, 1, 31, 31);
	reg = hea_set_u64_bits(reg, 0x8100, 48, 63);
	out_be64(&m->p_macvc, reg);

	/* reset for 10G */
	if (HEA_SPEED_10G == pport->port_cfg.speed_dt) {
		reg = 0x0ULL;
		reg = hea_set_u64_bits(reg, 1, 26, 26);
		reg = hea_set_u64_bits(reg, 1, 30, 30);
		reg = hea_set_u64_bits(reg, 0xd, 32, 37);
		reg = hea_set_u64_bits(reg, 1, 47, 47);
		reg = hea_set_u64_bits(reg, 0xc, 48, 55);
		reg = hea_set_u64_bits(reg, 1, 56, 56);
		reg = hea_set_u64_bits(reg, 0x1B, 58, 63);
	} else if (HEA_SPEED_1G == pport->port_cfg.speed_dt) {
		reg = 0x0ULL;
		reg = hea_set_u64_bits(reg, 1, 42, 42);
		reg = hea_set_u64_bits(reg, 0x2, 43, 44);
		reg = hea_set_u64_bits(reg, 1, 45, 45);
		reg = hea_set_u64_bits(reg, 1, 47, 47);
		reg = hea_set_u64_bits(reg, 0x4, 48, 55);
		reg = hea_set_u64_bits(reg, 1, 56, 56);
		reg = hea_set_u64_bits(reg, 0x1B, 58, 63);
	}

	out_be64(&m->p_macc, reg);

	out_be64(&m->p_pst, 0x0ULL);

	out_be64(&m->p_js, 9018ULL);
	out_be64(&m->p_xpcsp, 0x0ULL);
	out_be64(&m->p_xpcsst, 0x0ULL);

	out_be64(&m->pg_est, 0x0ULL);
}

static void _rhea_pport_em_dump(struct rhea_pport *pport)
{
	struct rhea_pport_em *m;
	unsigned p;

	if (NULL == pport)
		return;

	m = &pport->pport_regs->em;
	p = pport->port_cfg.pport_nr;

	rhea_reg_print(m, pg_uaelog, "P[%d][0]_UAELOG", p);
	rhea_reg_print(m, pg_uaelogm, "P[%d][0]_UAELOGM", p);
	rhea_reg_print(m, p_trcxcs, "P[%d]_TRCXCS", p);
	rhea_reg_print(m, p_trcxbb, "P[%d]_TRCXBB", p);
	rhea_reg_print(m, pg_trc, "P[%d][0]_TRC", p);
	rhea_reg_print(m, pg_hwem, "P[%d][0]_HWEM", p);
}

static void _rhea_pport_em_reset(struct rhea_pport *pport)
{
	u64 reg;
	u64 uaelog;
	struct rhea_pport_em *m;

	if (NULL == pport)
		return;

	m = &pport->pport_regs->em;

	/* get current state of register */
	uaelog = in_be64(&m->pg_uaelog);

	reg = 0x0ULL;

	/* 10G link state state reset */
	reg = hea_set_u64_bits(reg, 1, 0, 0);

	/* 1G state reset */
	reg = hea_set_u64_bits(reg, 1, 1, 1);

	/* 1G GPCS link state reset */
	reg = hea_set_u64_bits(reg, 1, 3, 3);

	out_be64(&m->pg_uaelog, reg);

	out_be64(&m->pg_uaelogm, 0x0ULL);
	out_be64(&m->p_trcxcs, 0x0ULL);
	out_be64(&m->p_trcxbb, 0x0ULL);
	out_be64(&m->pg_trc, 0x0ULL);

	reg = 0x0ULL;
	reg = hea_set_u64_bits(reg, 1, 59, 59);
	reg = hea_set_u64_bits(reg, 1, 60, 60);
	reg = hea_set_u64_bits(reg, 1, 61, 61);
	reg = hea_set_u64_bits(reg, 1, 62, 62);
	reg = hea_set_u64_bits(reg, 1, 63, 63);
	out_be64(&m->pg_hwem, reg);
}

static void _rhea_pport_rbb_dump(struct rhea_pport *pport)
{
	struct rhea_pport_rbb *r;
	unsigned p;

	if (NULL == pport)
		return;

	r = &pport->pport_regs->rbb;
	p = pport->port_cfg.pport_nr;

	rhea_reg_print(r, p_rxbor, "P[%d]_RXBOR", p);
	rhea_reg_print(r, pg_pthlb, "P[%d][0]_PTHLB", p);
	rhea_reg_print(r, p_pthrb, "P[%d]_PTHRB", p);
	rhea_reg_print(r, p_pqu, "P[%d]_PQU", p);
	rhea_reg_print(r, p_pqd, "P[%d]_PQD", p);
	rhea_reg_print(r, p_wsth, "P[%d]_WSTH", p);
	rhea_reg_print(r, p_prt, "P[%d]_PRT", p);
	rhea_reg_print(r, p_lbc, "P[%d]_LBC", p);
	rhea_reg_print(r, pg_trcrbb, "P[%d][0]_TRCRBB", p);
}

static void _rhea_pport_rbb_reset(struct rhea_pport *pport)
{
	u64 reg;
	struct rhea_pport_rbb *r;

	if (NULL == pport)
		return;

	r = &pport->pport_regs->rbb;

	out_be64(&r->p_rxbor, 0x0ULL);
	out_be64(&r->pg_pthlb, 0x0ULL);
	out_be64(&r->p_pthrb, 0x0ULL);

	/* leave this one out --> too many settings */
	/* out_be64(&r->p_pqu, 0x0ULL); */

	out_be64(&r->p_pqd, 0x0ULL);

	reg = 0x0ULL;
	reg = hea_set_u64_bits(reg, 0x80, 54, 63);
	out_be64(&r->p_wsth, reg);

	out_be64(&r->p_prt, 0x0ULL);

	reg = 0x0ULL;
	reg = hea_set_u64_bits(reg, 1, 1, 1);
	out_be64(&r->p_lbc, reg);

	out_be64(&r->pg_trcrbb, 0x0ULL);
}

static inline void _rhea_pport_pfc_rulem_dump(struct rhea_pport *pport)
{
	struct rhea_pport_pfc *f;
	unsigned m;
	unsigned p;

	if (NULL == pport)
		return;

	f = &pport->pport_regs->pfc;
	p = pport->port_cfg.pport_nr;

	for (m = 0; m < ARRAY_SIZE(f->p_rulem); m++)
		rhea_reg_print(f, p_rulem[m], "P[%d]_RULEM[0x%03x]", p, m);
}

static inline void _rhea_pport_pfc_rulem_reset(struct rhea_pport *pport)
{
	struct rhea_pport_pfc *f;
	unsigned m;

	if (NULL == pport)
		return;

	f = &pport->pport_regs->pfc;

	for (m = 0; m < ARRAY_SIZE(f->p_rulem); m++)
		out_be64(&f->p_rulem[m], 0x0ULL);
}

static void _rhea_pport_pfc_dump(struct rhea_pport *pport)
{
	struct rhea_pport_pfc *f;
	unsigned l;
	unsigned m;
	unsigned p;

	if (NULL == pport)
		return;

	f = &pport->pport_regs->pfc;
	p = pport->port_cfg.pport_nr;

	for (l = 0; l < ARRAY_SIZE(f->pl_vlanf); l++) {
		for (m = 0; m < ARRAY_SIZE(f->pl_vlanf[0]); m++) {
			rhea_reg_print(f, pl_vlanf[l][m],
				       "P[%d]L[%d]_VLANF[0x%02x]", p, l, m);
		}
	}

	for (m = 0; m < ARRAY_SIZE(f->p_mhash); m++)
		rhea_reg_print(f, p_mhash[m], "P[%d]_MHASH[0x%02x]", p, m);

	for (m = 0; m < ARRAY_SIZE(f->p_qosa); m++)
		rhea_reg_print(f, p_qosa[m], "P[%d]_QOSA[0x%02x]", p, m);
}

static void _rhea_pport_pfc_reset(struct rhea_pport *pport)
{
	struct rhea_pport_pfc *f;
	unsigned l;
	unsigned m;

	if (NULL == pport)
		return;

	f = &pport->pport_regs->pfc;

	for (l = 0; l < ARRAY_SIZE(f->pl_vlanf); l++) {
		for (m = 0; m < ARRAY_SIZE(f->pl_vlanf[0]); m++)
			out_be64(&f->pl_vlanf[l][m], 0x0ULL);
	}

	for (m = 0; m < ARRAY_SIZE(f->p_mhash); m++)
		out_be64(&f->p_mhash[m], 0x0ULL);

	for (m = 0; m < ARRAY_SIZE(f->p_qosa); m++)
		out_be64(&f->p_qosa[m], 0x0ULL);
}

static int _rhea_max_pport_counters_dump(struct rhea_pport *pport)
{
	if (NULL == pport)
		return -EINVAL;

	_rhea_pport_tx_error_dump(pport);
	_rhea_pport_rx_error_dump(pport);
	_rhea_pport_tx_counters_dump(pport);
	_rhea_pport_rx_counters_dump(pport);
	_rhea_pport_tx_histogram_dump(pport);
	_rhea_pport_rx_histogram_dump(pport);
	_rhea_pport_tx_logical_counters_dump(pport);
	_rhea_pport_rx_logical_counters_dump(pport);

	return 0;
}

void rhea_pport_counters_dump(struct rhea_channel *channel)
{
	struct rhea_pport *pport;

	if (NULL == channel)
		return;

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport)
		return;

	spin_lock(&pport->lock);

	_rhea_max_pport_counters_dump(pport);

	spin_unlock(&pport->lock);
}

void rhea_port_dump(struct rhea_channel *channel)
{
	struct rhea_pport *pport;

	if (NULL == channel)
		return;

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport)
		return;

	spin_lock(&pport->lock);

	_rhea_max_pport_counters_dump(pport);

	_rhea_pport_bpfc_dump(pport);
	_rhea_pport_mmc_dump(pport);
	_rhea_pport_em_dump(pport);
	_rhea_pport_rbb_dump(pport);
	_rhea_pport_pfc_dump(pport);

	spin_unlock(&pport->lock);
}

int _rhea_pport_counters_clear(struct rhea_pport *pport)
{
	if (NULL == pport)
		return -EINVAL;

	_rhea_pport_tx_error_reset(pport);
	_rhea_pport_rx_error_reset(pport);
	_rhea_pport_tx_counters_reset(pport);
	_rhea_pport_rx_counters_reset(pport);
	_rhea_pport_tx_histogram_reset(pport);
	_rhea_pport_rx_histogram_reset(pport);
	_rhea_pport_tx_logical_counters_reset(pport);
	_rhea_pport_rx_logical_counters_reset(pport);

	return 0;
}

void _rhea_port_register_reset(struct rhea_pport *pport)
{
	if (NULL == pport)
		return;

	_rhea_pport_counters_clear(pport);

	_rhea_pport_bpfc_reset(pport);
	_rhea_pport_mmc_reset(pport);
	_rhea_pport_em_reset(pport);
	_rhea_pport_rbb_reset(pport);
	_rhea_pport_pfc_reset(pport);
}



extern int rhea_channel_info_get(struct rhea_channel *channel,
				    enum hea_channel_feature_get feature,
				    u64 *value)
{
	int rc = 0;
	struct rhea_pport *pport;
	struct hea_channel_cfg channel_cfg;
	if (NULL == channel)
		return -EINVAL;

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport)
		return -EINVAL;

	/* save old config */
	channel_cfg = channel->channel_cfg;

	spin_lock(&pport->lock);

	switch (feature) {

	/* general channel features */
	case HEA_CHANNEL_GET_LINK_STATE:

		if (RHEA_CHANNEL_ENABLED != pport->state)
			*value = 0x0ULL;
		else
			rc = _rhea_pport_link_state_get(pport);

			/* get state */
			*value = 0 >= rc ? 0 : 1;

			/* we got a good status */
			if (0 < rc)
				rc = 0;
		break;

	case HEA_CHANNEL_GET_MAC_ADDRESS:
	{
		rc = _rhea_channel_mac_get(channel,
					   &channel_cfg.lport.
					   mac_address);
		if (rc)
			rhea_error("Was not able to set MAC address");

		/* save mac address */
		*value = channel_cfg.lport.mac_address._be64;
	}
	break;

	case HEA_CHANNEL_GET_MAX_FRAME_SIZE:
		*value = channel->channel_cfg.max_frame_size;
		break;

	/**************** VLAN **********************/

	case HEA_CHANNEL_GET_VLAN_EXTRACT:
		*value = channel->channel_cfg.vlan.vlan_extract;
		break;

	case HEA_CHANNEL_GET_DISCARD_UNTAGGED:
		*value = channel->channel_cfg.vlan.discard_untagged;
		break;

	case HEA_CHANNEL_GET_TAG_FILTER_MODE:
		*value = channel->channel_cfg.vlan.tag_filtering_mode;
		break;

	case HEA_CHANNEL_GET_VLAN_FILTER:
		rc = _rhea_channel_vlan_filter_get(channel, value);
		if (rc)
			rhea_error("Was not able to obtain vlan filter");
		break;

	/*************** QPN *******************/

	case HEA_CHANNEL_GET_BASE_SLOT:
		*value = channel->qpn_base;
		break;

	case HEA_CHANNEL_GET_NUM_SLOTS:
		if (channel->qpn)
			*value = channel->qpn->alloced;
		else
			rc = -EINVAL;
		break;

	/*************** TCAM *******************/

	case HEA_CHANNEL_GET_NUM_TCAM_HASH_BITS:
		if (channel->tcam)
			*value = channel->tcam->bits;
		else
			rc = -EINVAL;
		break;

	default:
		rhea_error("Feature is not supported");
		rc = -EINVAL;
		break;
	}

	spin_unlock(&pport->lock);

	return rc;

}


int rhea_channel_info_set(struct rhea_channel *channel,
				    enum hea_channel_feature_set feature,
				    u64 value)
{
	int rc = 0;
	struct rhea_pport *pport;
	struct hea_channel_cfg channel_cfg;

	if (NULL == channel)
		return -EINVAL;

	pport = _rhea_pport_get(channel->pport_nr);
	if (NULL == pport)
		return -EINVAL;

	/* save old config */
	channel_cfg = channel->channel_cfg;

	spin_lock(&pport->lock);

	switch (feature) {
	case HEA_CHANNEL_SET_VLAN_EXTRACT:
		channel_cfg.vlan.vlan_extract = value;
		break;

	case HEA_CHANNEL_SET_DISCARD_UNTAGGED:
		channel_cfg.vlan.discard_untagged = value;
		break;

	case HEA_CHANNEL_SET_TAG_FILTER_MODE:
		channel_cfg.vlan.tag_filtering_mode = value;
		break;

	default:
		break;
	}

	switch (feature) {

	case HEA_CHANNEL_SET_MAC_ADDRESS:
	{
		union hea_mac_addr mac_address;

		mac_address._be64 = value;

		rc = _rhea_channel_mac_set(channel, &mac_address);
		if (rc)
			rhea_error("Was not able to set MAC address");
	}
		break;

	case HEA_CHANNEL_SET_MAX_FRAME_SIZE:
		rc = _rhea_channel_max_frame_size_set(channel, value);
		if (rc) {
			rhea_error("Was not able to set "
				   "the maximum framesize");
		}
		break;

	case HEA_CHANNEL_RESET_UAELOG:
		rc = _rhea_pport_err_reset(pport);
		if (rc)
			rhea_error("Was not able to reset uaelog");
		break;

	    /**************** VLAN **********************/
	case HEA_CHANNEL_SET_VLAN_EXTRACT:
	case HEA_CHANNEL_SET_DISCARD_UNTAGGED:
	case HEA_CHANNEL_SET_TAG_FILTER_MODE:

		rc = _rhea_channel_vlan_set(channel, &channel_cfg.vlan);
		if (rc)
			rhea_error("Was not able to set vlan "
				   "settings for channel: %u and "
				   "physical port: %u", channel->type,
				   channel->pport_nr + 1);
	    break;

	case HEA_CHANNEL_SET_VLAN_FILTER:
		rc = _rhea_channel_vlan_filter_set(channel, value);
		if (rc)
			rhea_error("Was not able to set vlan filter for "
				"channel: %u and physical port: %u",
				channel->type, channel->pport_nr + 1);

		break;

	case HEA_CHANNEL_CLEAR_VLAN_FILTER:

		rc = _rhea_channel_vlan_filter_clear(channel, value);
		if (rc)
			rhea_error("Was not able to clear vlan filter for "
				"channel: %u and physical port: %u",
				channel->type, channel->pport_nr + 1);
		break;

	case HEA_CHANNEL_CLEAR_ALL_VLAN_FILTERS:

		rc = _rhea_channel_all_vlan_filters_clear(channel);
		if (rc)
			rhea_error("Was not able to clear all vlan filter "
				"for channel: %u and physical port: %u",
				channel->type, channel->pport_nr + 1);
		break;

	default:
		break;
	}

	spin_unlock(&pport->lock);

	return rc;
}
