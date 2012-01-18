
#include <linux/module.h>
#include <linux/log2.h>

#include <asm/byteorder.h>

#include "poweren_veth_sma.h"
#include "poweren_veth_vnet.h"

/* Internal definitions */
#define PKT_SIZE	2000
#define PKT_PAYLOAD	(PKT_SIZE - sizeof(struct sma_pkthdr))

struct sma_pkthdr {
	u32 size;
	u16 idx;
};

struct sma_pkt {
	struct sma_pkthdr hdr;
	char data[PKT_PAYLOAD] __aligned(8);
};

struct sma_fifo {
	u32 magic;
	u32 pkts_posted;
	u32 pkts_processed;
	u32 returned_send_credits[2];
	struct sma_pkt first_pkt __aligned(128);
};

struct sma_queue {
	struct sma_fifo *fifo;
	struct sma_pkt *base;
	int idx;
	u32 cache;
	u32 slots;
	u32 size;
};

struct sma_qp {
	struct sma_queue txq;
	struct sma_queue rxq;
	u32 send_credits;
	u32 credits_to_return;
	u8 credit_idx;
};

#define TO_BIGEND32(s)		cpu_to_be32(s)
#define TO_BIGEND64(s)		cpu_to_be64(s)
#define TO_BIGEND16(s)		cpu_to_be16(s)
#define TO_LOCALEND32(s)	be32_to_cpu(s)
#define TO_LOCALEND64(s)	be64_to_cpu(s)
#define TO_LOCALEND16(s)	be16_to_cpu(s)

#define FIFO_MAGIC			0xC0C0C0C0u
#define FIFO_SET_MAGIC(f)	((f)->magic = FIFO_MAGIC)
#define FIFO_GET_MAGIC(f)	((f)->magic)

#define NEXT_PKT(q) \
	((q)->idx = (((q)->idx + 1) & ((q)->slots - 1)))

static inline void init_queue(struct sma_queue *q, void *buf,
		u32 size, u32 slots)
{
	q->cache = 0;
	q->idx  = 0;
	q->fifo = (struct sma_fifo *)(buf);
	q->base = &q->fifo->first_pkt;
	q->size = size;
	q->slots = slots;
}

static inline void init_fifo(struct sma_queue *q)
{
	q->fifo->pkts_processed = 0;
	q->fifo->pkts_posted = 0;
	q->fifo->returned_send_credits[0] = 0;
	q->fifo->returned_send_credits[1] = 0;
}

static inline void reset_queues(struct sma_qp *qp)
{
	qp->rxq.cache = 0;
	qp->rxq.idx = 0;
	qp->txq.cache = 0;
	qp->txq.idx = 0;
}

static inline void update_queue(struct sma_queue *q, u32 *ctr)
{
	NEXT_PKT(q);
	q->cache++;
	*ctr = TO_BIGEND32(q->cache);
}

#define NOTIFY_POSTED(q) \
	update_queue((q), &(q)->fifo->pkts_posted)

#define NOTIFY_PROCESSED(q) \
	update_queue((q), &(q)->fifo->pkts_processed)

#define PACKETS_POSTED(q) \
	TO_LOCALEND32((q)->rxq.fifo->pkts_posted)

#define PACKETS_PROCESSED(q)	((q)->rxq.cache)

#define GET_PKT(q)				((q)->base + (q)->idx)
#define GET_PKT_SIZE(p)			TO_LOCALEND32((p)->hdr.size)
#define SET_PKT_SIZE(p, s)		(p)->hdr.size = TO_BIGEND32((s))

#define HAVE_RETURNED_CREDITS(q, i) \
	((q)->rxq.fifo->returned_send_credits[i] != 0)

#define GET_RETURNED_CREDITS(q, i) \
	TO_LOCALEND32((q)->rxq.fifo->returned_send_credits[i])

#define RESET_RETURNED_CREDITS(q, i) \
	(q)->rxq.fifo->returned_send_credits[i] = 0;

static inline void return_credits(struct sma_qp *qp)
{
	qp->txq.fifo->returned_send_credits[qp->credit_idx] =
		TO_BIGEND32(qp->credits_to_return);
	qp->credit_idx = (qp->credit_idx == 0);
	qp->credits_to_return = 0;
}

static inline void sma_return_credits(struct sma_qp *qp)
{
	qp->credits_to_return++;

	if (qp->credits_to_return >= (qp->rxq.slots >> 1))
		return_credits(qp);
}

#define DEC_SEND_CREDIT(q) \
	((q)->send_credits = ((q)->send_credits - 1))

#define INC_SEND_CREDIT(q, c) \
	((q)->send_credits = ((q)->send_credits + (c)))

static inline void sma_reclaim_credits(struct sma_qp *qp, int idx)
{
	if (HAVE_RETURNED_CREDITS(qp, idx)) {
		INC_SEND_CREDIT(qp, GET_RETURNED_CREDITS(qp, idx));
		RESET_RETURNED_CREDITS(qp, idx);
	}
}


struct sma_context {
	char *txsma;
	char *rxsma;
	u64 rxsma_size;
	u64 txsma_size;
	struct sma_qp pkts;
	struct sma_qp cmds;
};

static struct sma_context g_ctx;
/* End internal definitions */


#define SMA_PAD 4608 /* Reserved: (34 * 128) + 256 */

inline int poweren_veth_sma_init(sma_mapfunc_t map_sma)
{
	g_ctx.rxsma = map_sma(SMA_MAP_LOCAL, &g_ctx.rxsma_size);

	if (!g_ctx.rxsma || g_ctx.rxsma_size == 0) {
		poweren_veth_info("map_sma local failed");
		return -ENOMEM;
	}

	g_ctx.txsma = map_sma(SMA_MAP_REMOTE, &g_ctx.txsma_size);

	if (!g_ctx.txsma || g_ctx.txsma_size == 0) {
		poweren_veth_info("map_sma remote failed");
		return -ENOMEM;
	}

	return 0;
}

int poweren_veth_sma_open(void **hndl)
{
	struct sma_context *ctx = &g_ctx;
	u32 win_size = ctx->rxsma_size - SMA_PAD;
	struct sma_qp *pkts = &ctx->pkts;
	struct sma_qp *cmds = &ctx->cmds;
	u32 cmd_size = 10 * sizeof(struct sma_pkt);
	u32 cmd_slots = rounddown_pow_of_two(cmd_size / sizeof(struct sma_pkt));
	u32 q_size = win_size - cmd_size;
	u32 slots = rounddown_pow_of_two(q_size / sizeof(struct sma_pkt));

	memset(pkts, 0, sizeof(*pkts));
	memset(cmds, 0, sizeof(*cmds));

	init_queue(&pkts->txq, ctx->txsma, q_size, slots);
	init_fifo(&pkts->txq);
	poweren_veth_info("TXQ init: sizes %u/%u/%u", win_size, q_size, slots);

	mb(); /* ensure queue config is complete before setting magic byte */

	FIFO_SET_MAGIC(pkts->txq.fifo);

	/* Now the command q's */
	init_queue(&cmds->txq, ctx->txsma+q_size, cmd_size, cmd_slots);
	init_fifo(&cmds->txq);
	poweren_veth_info("CMD TXQ init: sizes %u/%u", cmd_size, cmd_slots);

	mb(); /* ensure queue config is complete before setting magic byte */

	FIFO_SET_MAGIC(cmds->txq.fifo);

	init_queue(&pkts->rxq, ctx->rxsma, q_size, slots);
	init_queue(&cmds->rxq, ctx->rxsma+q_size, cmd_size, cmd_slots);

	pkts->send_credits = 0;
	pkts->credits_to_return = slots;
	return_credits(pkts);

	cmds->send_credits = cmd_slots;

	*hndl = ctx;
	return 0;
}

inline int poweren_veth_sma_newpkts(void *hndl)
{
	struct sma_context *ctx = hndl;
	struct sma_qp *qp = &ctx->pkts;
	int numpkts;

	if (FIFO_GET_MAGIC(qp->rxq.fifo) != FIFO_MAGIC)
		return 0;

	numpkts = PACKETS_POSTED(qp) - PACKETS_PROCESSED(qp);

	if (numpkts < 0) {
		poweren_veth_info("newpkts: Error reading %d newpkts", numpkts);
		reset_queues(qp);
		return 0;
	}

	return numpkts;
}

inline int poweren_veth_sma_space(void *hndl)
{
	struct sma_context *ctx = hndl;
	struct sma_qp *qp = &ctx->pkts;

	sma_reclaim_credits(qp, 0);
	sma_reclaim_credits(qp, 1);

	return qp->send_credits;
}

#ifndef CONFIG_POWEREN_EP_DEVICE

inline int poweren_veth_sma_close(void *hndl)
{
	return 0;
}

inline int poweren_veth_sma_post(void *hndl, struct mem_addr *sg,
			u16 count, sma_pinfunc_t pin)
{
	struct sma_context *ctx = (struct sma_context *)hndl;
	struct sma_qp *qp = &ctx->cmds;
	struct sma_pkt *pktp = GET_PKT(&qp->txq);
	struct mem_addr *sma_addrs = (struct mem_addr *)pktp->data;
	dma_addr_t dma_addr;
	int i;

	BUG_ON(!pin);

	if (sizeof(*sma_addrs) * count > PKT_PAYLOAD)
		return 0;

	sma_reclaim_credits(qp, 0);
	sma_reclaim_credits(qp, 1);

	if (qp->send_credits <= 0)
		return 0;

	poweren_veth_info("Posting Buffers: %u", count);

	for (i = 0; i < count; i++) {
		dma_addr = pin(sg[i].addr, sg[i].len);

		sma_addrs[i].addr = TO_BIGEND64(dma_addr);
		sma_addrs[i].len = TO_BIGEND32(sg[i].len);
		sma_addrs[i].idx = TO_BIGEND16(sg[i].idx);
	}

	SET_PKT_SIZE(pktp, count);
	DEC_SEND_CREDIT(qp);

	mb(); /* ensure credits & size updated before notifying remote side */

	NOTIFY_POSTED(&qp->txq);

	return 1;
}

inline int poweren_veth_sma_writepkt(void *hndl, const struct mem_addr *src,
		sma_writefunc_t send)
{
	struct sma_context *ctx = (struct sma_context *)hndl;
	struct sma_qp *qp = &ctx->pkts;
	struct sma_pkt *pktp = GET_PKT(&qp->txq);

	if (src->len > PKT_PAYLOAD)
		return 0;

	if (qp->send_credits <= 0)
		return 0;

	send(pktp->data, (void *)src->addr, src->len);

	SET_PKT_SIZE(pktp, src->len);
	DEC_SEND_CREDIT(qp);

	mb(); /* ensure credits & size updated before notifying remote side */

	NOTIFY_POSTED(&qp->txq);

	return 1;
}

inline int poweren_veth_sma_readpkt(void *hndl, struct mem_addr *buf)
{
	struct sma_context *ctx = (struct sma_context *)hndl;
	struct sma_qp *qp = &ctx->pkts;
	struct sma_pkt *pktp = GET_PKT(&qp->rxq);

	/* DMA'ed packet - this is kind of a completion */
	buf->len = GET_PKT_SIZE(pktp);
	buf->idx = TO_LOCALEND16(pktp->hdr.idx);

	sma_return_credits(qp);

	NOTIFY_PROCESSED(&qp->rxq);

	return 1;
}

#else
/**
 * skb_addrs: together with buffers_push and buffers_pop, implements
 * a queue for sequentially storing DMA buffers posted to the device
 * by the host.
 */
struct skb_addrs {
	struct mem_addr *addrs;
	u64 head;
	u64 tail;
	u16 count;
};

static struct skb_addrs host_addrs;

static inline void buffers_push(struct skb_addrs *rb, dma_addr_t addr,
		u32 len, u16 idx)
{
	int i = (rb->head+1) & (rb->count-1);
	rb->addrs[i].addr = addr;
	rb->addrs[i].len = len;
	rb->addrs[i].idx = idx;
	rb->head++;
}

static inline bool buffers_can_pop(struct skb_addrs *rb)
{
	return rb->head != rb->tail;
}

static inline struct mem_addr *buffers_pop(struct skb_addrs *rb)
{
	struct mem_addr *buf = NULL;
	int i = (rb->tail + 1) & (rb->count - 1);

	if (rb->head != rb->tail) {
		buf = &rb->addrs[i];
		rb->tail++;
	}

	return buf;
}

inline int poweren_veth_sma_close(void *hndl)
{
	kfree(host_addrs.addrs);
	return 0;
}

inline int poweren_veth_sma_readcmd(void *hndl)
{
	struct sma_context *ctx = (struct sma_context *)hndl;
	struct sma_qp *qp = &ctx->cmds;
	struct sma_pkt *pktp = GET_PKT(&qp->rxq);
	struct mem_addr *sma_addrs;
	int i, len;

	len = GET_PKT_SIZE(pktp); /* Must be ^2 */
	poweren_veth_info("Received Buffers: %d", len);

	if (!host_addrs.addrs) {
		host_addrs.addrs = kzalloc(len * sizeof(*(host_addrs.addrs)),
				GFP_KERNEL);
		host_addrs.count = len;

		/* ensures that the 1st buffer is used initially */
		host_addrs.head = host_addrs.count - 1;
		host_addrs.tail = host_addrs.count - 1;
	}

	sma_addrs = (struct mem_addr *)pktp->data;

	for (i = 0; i < len; i++) {
		buffers_push(&host_addrs,
				TO_LOCALEND64(sma_addrs[i].addr),
				TO_LOCALEND32(sma_addrs[i].len),
				TO_LOCALEND16(sma_addrs[i].idx));
	}

	sma_return_credits(qp);

	NOTIFY_PROCESSED(&qp->rxq);

	return 0;
}

inline int poweren_veth_sma_cmd_newpkts(void *hndl)
{
	struct sma_context *ctx = (struct sma_context *)hndl;
	struct sma_qp *qp = &ctx->cmds;
	int numpkts;

	if (FIFO_GET_MAGIC(qp->rxq.fifo) != FIFO_MAGIC)
		return 0;

	numpkts = PACKETS_POSTED(qp) - PACKETS_PROCESSED(qp);

	if (numpkts > 0)
		poweren_veth_sma_readcmd(hndl);

	return poweren_veth_sma_newpkts(hndl);
}

inline int poweren_veth_sma_writepkt(void *hndl, const struct mem_addr *src,
		sma_writefunc_t send)
{
	struct sma_context *ctx = (struct sma_context *)hndl;
	struct sma_qp *qp = &ctx->pkts;
	struct sma_pkt *pktp = GET_PKT(&qp->txq);
	struct mem_addr *dest;

	if (src->len > PKT_PAYLOAD)
		return 0;

	if (qp->send_credits <= 0)
		return 0;

	dest = buffers_pop(&host_addrs);

	if (!dest) {
		poweren_veth_info("NO BUFFER: %u", qp->send_credits);
		return 0;
	}

	pktp->hdr.idx = TO_BIGEND16(dest->idx);

	send((void *)dest->addr, (void *)src->addr, src->len);

	SET_PKT_SIZE(pktp, src->len);
	DEC_SEND_CREDIT(qp);

	mb(); /* ensure credits & size updated before notifying remote side */

	NOTIFY_POSTED(&qp->txq);

	return 1;
}

inline int poweren_veth_sma_readpkt(void *hndl, struct mem_addr *buf)
{
	struct sma_context *ctx = (struct sma_context *)hndl;
	struct sma_qp *qp = &ctx->pkts;
	struct sma_pkt *pktp = GET_PKT(&qp->rxq);

	/* Standard inline packet */
	memcpy((void *)buf->addr, pktp->data, GET_PKT_SIZE(pktp));
	buf->len = GET_PKT_SIZE(pktp);

	sma_return_credits(qp);

	NOTIFY_PROCESSED(&qp->rxq);

	return 1;
}

inline int poweren_veth_sma_buffers_space(void *hndl)
{
	return buffers_can_pop(&host_addrs) && poweren_veth_sma_space(hndl);
}
#endif
