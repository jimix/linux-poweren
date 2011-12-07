/**************************************************************************
 * IBM CONFIDENTIAL -- (IBM Confidential Restricted when
 * combined with the aggregated modules for this product)
 * Licensed Internal Code Source Materials
 *
 * (C) COPYRIGHT International Business Machines Corp. 2010
 * All Rights Reserved
 *
 * US Government Users Restricted Rights - Use, duplication or
 * disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
 **************************************************************************
 *
 * Description: MDIO read/write routines
 *************************************************************************/

#ifndef _RHEA_MDIO_H_
#define _RHEA_MDIO_H_

#include <rhea-linux.h>
#include <rhea-channel.h>
#include <asm/poweren_hea_bitops.h>

/*
 * convert IEEE MDIO register request to IBM numbering access
 *
 *
 *                        1     1
 *            0123 4567 8901 2345   IBM offsets
 *
 *            1     1
 *            5432 1098 7654 3210   MDIO Reg offset
 *
 *
 *  e.g.
 *  ieee [3:0]  --> ibm  [12:15]
 *  ieee [15:12]  --> ibm  [0:3]
 *
 */

#define hea_get_mdio_bits(val, e, s) \
	hea_get_u16_bits(val, (15 - e), (15 - s))

#define hea_get_mdio_bit(val, b) \
	hea_get_u16_bits(val, (15 - b), (15 - b))

#define hea_hea_set_mdio_bits(val, e, s) \
	hea_set_u16_bits(0x0ULL, val, (15 - e), (15 - s))

#define hea_set_mdio_bit(b) \
	hea_set_u16_bit(0x1, (15 - b))


/* MDIO address by port
 *
 * valid pports: 0 to 3
 *
 * See schematics for MDIO addresses
 */
static int s_phy_mdio_addr[] = {0x11, 0x12, 0x00, 0x01};

static inline int hea_mdio_addr_get(struct rhea_pport *pport)
{
	int pport_nr;
	int mdio_addr;

	if (NULL == pport)
		return -EINVAL;

	pport_nr = pport->port_cfg.pport_nr;

	if (!is_hea_pport(pport_nr))
		return -EINVAL;

	/* get PHY mdio address */
	/* consider moving getting information from device tree */
	mdio_addr = s_phy_mdio_addr[pport_nr];

	return mdio_addr;

}


/*
 * Read/Write an MDIO register with IEEE 802.3 clause 22
 * direct/indirect addressing
 *
 * @see pHPG-10-p122
 *
 */
static inline u16
hea_mdio_rw_reg(unsigned char indirect, struct rhea_pport *pport,
		int mdio_addr, int rw_op, int reg_no, u16 data)
{
	u64 mma;
	int i;

	struct rhea_pport_mmc *mmc;

	if (NULL == pport) {
		rhea_error("Wrong parameter");
		return 0xFFFF;
	}

	mmc = &pport->pport_regs->mmc;

	mma = hea_set_u64_bits(0x0ULL, 1, 20, 20); /* GO */
	mma = hea_set_u64_bits(mma, indirect, 23, 23); /* 0=direct 1=indirect */
	mma = hea_set_u64_bits(mma, rw_op, 30, 31); /* 1=write 2=read */
	mma = hea_set_u64_bits(mma, mdio_addr, 35, 39); /* 5bit MDIO address */
	mma = hea_set_u64_bits(mma, reg_no, 43, 47); /* 5bit reg address */
	mma = hea_set_u64_bits(mma, data, 48, 63); /* 16bit reg data */

	/* We're only allowed to initiate a new read/write operation through
	 * the management interface when we detected that PHY sense (bit 21)
	 * has been asserted. */
	for (i = 0; i < 10; i++) {
		u64 mma_reg = in_be64(&mmc->p_mma);
		if (0 != hea_test_u64_bit(mma_reg, 21))
			break;

		usleep_range(1000, 1100);
	}

	/* No PHY sensed? --> There must be something wrong! */
	if (i == 10) {
		rhea_error("MDIO ERROR: Did not sense a PHY on this port!");
		return (u16) ~0;
	}

	/* Do the operation itself! */
	out_be64(&mmc->p_mma, mma);

	/* Wait for the operation to complete */
	for (i = 0; i < 100; i++) {
		u64 mma_reg = in_be64(&mmc->p_mma);

		if (0 == hea_test_u64_bit(mma_reg, 20))
			break;

		usleep_range(1000, 1100);
	}

	mma = in_be64(&mmc->p_mma);

	/* Operation still ongoing? --> There must be something wrong! */
	if (hea_test_u64_bit(mma, 20)) {
		rhea_error("MDIO ERROR: operation did not complete in time!");
		return (u16) ~0;
	}

	if (hea_test_u64_bit(mma, 21) == 0) {
		rhea_error("hea_mdio_rw_reg(): PHY Sense=%d",
			   (int) hea_test_u64_bit(mma, 21));
		return (u16) ~0;
	}
	if (hea_test_u64_bit(mma, 22) == 1) {
		rhea_error("hea_mdio_rw_reg(): PHY ERROR=%d",
			   (int) hea_test_u64_bit(mma, 22));
		return (u16) ~0;
	}

	return (u16) mma;
}



static inline u16
hea_mdio_read_shadow(struct rhea_pport *pport, int reg_no, int shadow)
{
	u16 reg_val;
	int mdio_addr;

	reg_val = hea_set_u16_bits(0x0U, shadow, 1, 5);

	mdio_addr = hea_mdio_addr_get(pport);

	(void) hea_mdio_rw_reg(0, pport, mdio_addr, 1, reg_no, reg_val);
	return hea_mdio_rw_reg(0, pport, mdio_addr, 2, reg_no, reg_val);
}




static inline u16
hea_mdio_write_shadow(struct rhea_pport *pport, int reg_no,
		      int shadow, u16 data)
{
	int mdio_addr;

	data &= ~hea_set_u16_bits(0x0U, 0x1f, 1, 5);
	data = hea_set_u16_bits(data, shadow, 1, 5);

	/* write enable */
	data |= hea_set_mdio_bit(15);

	mdio_addr = hea_mdio_addr_get(pport);

	return hea_mdio_rw_reg(0, pport, mdio_addr, 1, reg_no, data);
}

/*
 * MDIO read
 */
static inline u16
hea_mdio_read(struct rhea_pport *pport, int reg)
{
	int mdio_addr = hea_mdio_addr_get(pport);
	return hea_mdio_rw_reg(0, pport, mdio_addr, 2, reg, 0);
}

static inline u16
hea_mdio_write(struct rhea_pport *pport, int reg, u16 data)
{
	int mdio_addr = hea_mdio_addr_get(pport);
	return hea_mdio_rw_reg(0, pport, mdio_addr, 1, reg, data);
}


#define HEA_MDIO_1G_PCS_ADDR 0x10

static inline u16
pcs_mdio_read(struct rhea_pport *pport, int reg)
{
	return hea_mdio_rw_reg(0, pport, HEA_MDIO_1G_PCS_ADDR, 2, reg, 0);
}

static inline u16
pcs_mdio_write(struct rhea_pport *pport, int reg, u16 data)
{
	return hea_mdio_rw_reg(0, pport, HEA_MDIO_1G_PCS_ADDR, 1, reg, data);
}


/*
 * Read/Write an MDIO register with IEEE 802.3 clause 22 indirect addressing
 *
 * @see pHPG-10-p122
 *
 */
static inline u16
hea_mdio_indirect_rw_reg(struct rhea_pport *pport,
		    int phy_addr, int rw_op, int dev_addr, u16 data)
{
	u64 mma;
	int i;
	struct rhea_pport_mmc *mmc;

	if (NULL == pport) {
		rhea_error("Wrong parameter");
		return 0xFFFF;
	}

	mmc = &pport->pport_regs->mmc;

	mma = hea_set_u64_bits(0x0ULL, 1, 20, 20); /* GO */
	mma = hea_set_u64_bits(mma, 1, 23, 23); /* 1=indirect */

	mma = hea_set_u64_bits(mma, rw_op, 30, 31); /* 1=write 2=read */
	mma = hea_set_u64_bits(mma, phy_addr, 35, 39); /* 5bit MDIO address */
	mma = hea_set_u64_bits(mma, dev_addr, 43, 47); /* 5bit reg address */
	mma = hea_set_u64_bits(mma, data, 48, 63); /* 16bit reg data */

	rhea_debug("\tMMA write = 0x%016llx", mma);
	out_be64(&mmc->p_mma, mma);

	/* Wait for the operation to complete */
	for (i = 0; i < 100; i++) {
		mma = in_be64(&mmc->p_mma);

		if (0 == hea_test_u64_bit(mma, 20))
			break;

		usleep_range(1000, 1100);
	}

	/* get current register value */
	mma = in_be64(&mmc->p_mma);

	/* Operation still ongoing? --> There must be something wrong! */
	if (1 == hea_test_u64_bit(mma, 20)) {
		rhea_error("MDIO ERROR: operation did not complete in time!");
		return (u16) ~0;
	}

	if (0 == hea_test_u64_bit(mma, 21)) {
		rhea_error("hea_mdio_indirect_rw_reg(): PHY Sense=%d",
			(int) hea_test_u64_bit(mma, 21));

		return (u16) ~0;
	}

	if (1 == hea_test_u64_bit(mma, 22)) {
		rhea_error("hea_mdio_indirect_rw_reg(): PHY ERROR=%d",
			   (int) hea_test_u64_bit(mma, 22));

		return (u16) ~0;
	}

	return (u16) mma;
}


/*
 * MDIO indirect read
 */
static inline u16
hea_mdio_iread(struct rhea_pport *pport, int dev_addr, int reg_addr)
{
	u16 rc;
	int mdio_phy_addr;

	if (NULL == pport)
		return ~0x0;

	mdio_phy_addr = hea_mdio_addr_get(pport);

	rc = hea_mdio_indirect_rw_reg(pport, mdio_phy_addr, 0 /* addr setup */,
				 dev_addr, reg_addr);

	if (rc == 0xffff)
		return rc;

	rc = hea_mdio_indirect_rw_reg(pport, mdio_phy_addr,
				      3 /* indirect read */,
				      dev_addr, 0);
	return rc;
}



/*
 * MDIO indirect write
 */
static inline u16
hea_mdio_iwrite(struct rhea_pport *pport, int dev_addr,
		int reg_addr, u16 reg_data)
{
	u16 rc;
	int mdio_phy_addr;

	if (NULL == pport)
		return ~0x0;

	mdio_phy_addr = hea_mdio_addr_get(pport);

	rc = hea_mdio_indirect_rw_reg(pport, mdio_phy_addr,
				      0 /* addr setup */,
				      dev_addr, reg_addr);

	rc = hea_mdio_indirect_rw_reg(pport, mdio_phy_addr,
				      1 /* indirect write */,
				      dev_addr, reg_data);

	return rc;
}

#endif /* _RHEA_MDIO_H_ */
