#ifndef _POWEREN_COP_H_
#define _POWEREN_COP_H_

/* CRB structs for DMA */

#define CRB_FLAG_PCIE_FN	8		/* 0x00001F00 */
#define CRB_FLAG_HOST_IRQ	13		/* 0x00002000 */
#define CSBP_ADDR_TRANS		1		/* 0x0000000000000002 */
#define CSB_CTRL_VALID		31		/* 0x80000000 */
#define CSB_CTRL_CC			8		/* 0x0000FF00 */

/* field, bit index, value */
#define CXB_SET_BITS(f, b, v) \
	((f) |= ((typeof((f)))(v) << (b)))

/* field, bit index, value (mask) to check against */
#define CXB_CHECK_BITS(f, b, v) \
	(((f) >> (b)) & (typeof((f)))(v))

#define CSB_VALID(csb) \
	CXB_CHECK_BITS((csb)->control, CSB_CTRL_VALID, 0x01)

struct dde {
	u16 p;
	u8 count;
	u8 pool_idx;
	u32 byte_count;
	u64 addr;
};

struct cop_ccb_common {
	u64 cv;
	u64 ca_cm;
};

struct cop_crb_dmax {
	u32 ccw;
	u32 flags;
	u64 csbp;
	struct dde source;
	struct dde target;
	struct cop_ccb_common ccb;
};

struct cop_csb_common {
	u32 control;
	u32 pbc;
	u64 addr;
};

#endif /*_POWEREN_COP_H_ */
