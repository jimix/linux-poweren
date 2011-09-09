#include "HW172320.h"
#include "mem_utils.h"
#include "xml_icws_structures.h"

static int retire_single_qpool(struct pixad_user_proc *target,
			struct pixad_copro *copro_info, int qpid)
{
	if (!target)
		return -1;

	if (0 != retire_qpool(qpid)) {
		pixad_err("retire qpool %d failed\n", qpid);
		return -1;
	}

	/* process fromxml msg to ensure SW Qcode request completes. */
	if (0 != wait_imqe(copro_info, target->unit_id, target->vf_id,
					      MSG_RETIRE_QPOOL_DONE)) {
		pixad_err("err wait_imqe type 0x%02x for qpid=%d\n",
					MSG_RETIRE_QPOOL_DONE, qpid);
		return -1;
	}

	return 0;
}

int retire_all_qpools(struct pixad_user_proc *target,
			struct pixad_copro *copro_info)
{
	/* retire qpool 0 ~ 3 */
	int qpid = 0;

	for (qpid = 0; qpid < 4; qpid++) {
		if (0 != retire_single_qpool(target, copro_info, qpid)) {
			pixad_err("retire qpool %d failed\n", qpid);
			return -1;
		}
	}

	return 0;
}

static inline int zero_qcode_free_pool(struct xml_qm_t *qm,
					enum xml_qcode_type type)
{
	size_t pool_size = 0;

	if (!qm)
		return -1;

	pool_size = sizeof(qm->free_pool[type][0]) *
					ARRAY_SIZE(qm->free_pool[type]);
	memset(&qm->free_pool[type][0], 0, pool_size);

	return 0;
}

int get_packet_offset_align(struct xml_qm_t *qm, enum xml_qcode_type type,
					struct qpool_packet_offset_info *po)
{
	int qcode_num = 0;
	int delay_count = 100;
	int i = 0;

	if (!qm || !po)
		return -1;
	po->align_val = -1;
	po->qcode_entry = -1;

	/* Scan free pool area to determine where the just freed entry was
	 * placed (should be only one 16-bit non 0 value).  If entry was
	 * placed at address with last 3 bits set to 6 free pool area pack
	 * offset is aligned.
	 */
	while (delay_count-- && (0 == qcode_num)) {
		for (i = 0; i < ARRAY_SIZE(qm->free_pool[type]); i++) {
			if (0 != qm->free_pool[type][i]) {
				++qcode_num;
				po->qcode_entry = i;
				/* get the last 3 bits of the address */
				po->align_val = (int)(((u64)
					&qm->free_pool[type][i]) & 0x7);
			}
		}
	}

	if (1 == qcode_num)
		return 0;
	else {
		po->align_val = -1;
		po->qcode_entry = -1;
		return -1;
	}
}

static inline void write_qcode_free_pool(struct xml_qm_t *qm,
	enum xml_qcode_type type, struct qpool_packet_offset_info *po)
{
		int i = 0;
		/* If entry was placed at any other address write 0x0001,
		 * 0x0002, 0x0003 in free pool area at next location past
		 * just placed entry.
		 */
		for (i = 1; i < 4; i++)
			qm->free_pool[type][i + po->qcode_entry] = i;
}

static int update_qcodes(struct pixad_user_proc *target, int qpid,
	enum xml_qcode_type type, struct qpool_packet_offset_info *po)
{
	const char *qname[3] = {"LESS_THAN_24_CHARS_1",
		"LESS_THAN_24_CHARS_2", "LESS_THAN_24_CHARS_3"};
	int update_num = 0;
	int i = 0;

	/* 3.6 Issue number of software Qcode updates with Qname <= 24, Qpool
	 * ID 0.  Number of added Qcodes depends on alignment from previous
	 * step.  Number could be 1, 2, 3.  If alignment of just freed entry
	 * is 0 issues 3 Qcode updates.  If alignment of just freed entry is 1
	 * issue 2 Qcode updates. If alignment of just freed entry is 2 issue
	 * 1 Qcode update.
	 */
	if (0 == po->align_val)
		update_num = 3;
	else if (0x2 == po->align_val)
		update_num = 2;
	else if (0x4 == po->align_val)
		update_num = 1;
	else
		return -1;

	for (i = 0; i < update_num; i++) {
		if (0 != request_qcode(qname[i], strlen(qname[i]), type,
				      qpid, CCW_CI_ALL_INSTANCE, target)) {
			pixad_err("request qcode of %s failed\n", qname[i]);
			return -1;
		}
	}

	return 0;
}

static int align_prefix_local_uri_freepool(struct pixad_user_proc *target,
		struct pixad_copro *copro_info, enum xml_qcode_type type)
{
	const int qpool_0 = 0;
	const char qname[] = "LESS_THAN_24_CHARS";
	struct qpool_packet_offset_info po;
	struct xml_qm_t *qm = NULL;

	/* 3.1 Issue software Qcode update with Qname <= 24, Qpool ID 0 */
	if (0 != request_qcode(qname, ARRAY_SIZE(qname) - 1, type,
			      qpool_0, CCW_CI_ALL_INSTANCE, target)) {
		pixad_err("request qcode of %s failed\n", &qname[0]);
		return -1;
	}

	/* 3.2 Zero out free pool area in question */
	qm = (struct xml_qm_t *)copro_info->vf_info[target->vf_id].virt_qpool;
	if (0 != zero_qcode_free_pool(qm, type)) {
		pixad_err("zero_qcode_free_pool type %u failed\n", type);
		return -1;
	}

	/* 3.3 Issue Qpool delete for Qpool ID 0 */
	if (0 != retire_qpool(qpool_0)) {
		pixad_err("retire qpool 0 failed\n");
		return -1;
	}

	/* 3.4 check packet offset alignment */
	if (0 != get_packet_offset_align(qm, type, &po)) {
		pixad_err("get packet offset_align, type %u failed\n", type);
		return -1;
	}

	if (0x6 == po.align_val)
		return 0;
	else {
		/* 3.5 */
		write_qcode_free_pool(qm, type, &po);

		/* 3.6 update software qcodes with qname <= 24 to Qpool 0 */
		if (0 != update_qcodes(target, qpool_0, type, &po)) {
			pixad_err("update_qcodes for type %u failed\n", type);
			return -1;
		}

		/* 3.7 Issue Qpool delete for Qpool ID 0 */
		if (0 != retire_qpool(qpool_0)) {
			pixad_err("retire qpool 0 failed\n");
			return -1;
		}
	}

	return 0;
}

static int align_expansion_freepool(struct pixad_user_proc *target,
					struct pixad_copro *copro_info)
{
	const char qname1[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
	const char qname2[] = "ZYXWVUTSRQPONMLKJIHGFEDCBA";
	struct xml_qm_t *qm = NULL;
	const int qpool_0 = 0;
	size_t pool_size = 0;
	struct qpool_packet_offset_info po;
	int i = 0;
	int qcode_num = 0;
	int retry = 100;

	/* step 1. request qcode */
	if (0 != request_qcode(qname1, sizeof(qname1) - 1, local_qcode,
			qpool_0, CCW_CI_ALL_INSTANCE, target)) {
		pixad_err("request qcode of %s failed\n", qname1);
		return -1;
	}

	/* step 2. Zero out free pool area in question */
	qm = (struct xml_qm_t *)copro_info->vf_info[target->vf_id].virt_qpool;
	pool_size = sizeof(qm->expansion_free_pool[0]) *
					ARRAY_SIZE(qm->expansion_free_pool);
	memset(&qm->expansion_free_pool[0], 0, pool_size);

	/* 3.3 Issue Qpool delete for Qpool ID 0 */
	if (0 != retire_single_qpool(target, copro_info, qpool_0)) {
		pixad_err("retire qpool 0 failed\n");
		return -1;
	}

	/* 3.4 check packet offset alignment */
	po.align_val = -1;
	po.qcode_entry = -1;

	while (retry-- && (qcode_num == 0)) {
		for (i = 0; i < ARRAY_SIZE(qm->expansion_free_pool); i++) {
			if (0 != qm->expansion_free_pool[i]) {
				++qcode_num;
				po.qcode_entry = i;
			}
		}
	}

	if (qcode_num != 1) {
		pixad_err("err non-0 entrie count = %d, should be 1\n",
								qcode_num);
		return -1;
	}

	if (0x1 == (po.qcode_entry & 0x1))
		return 0;

	/* 3.5 */
	qm->expansion_free_pool[po.qcode_entry + 1] = 0x00018000;

	/* 3.6 update software qcodes with qname > 24 to Qpool 0 */
	if (0 != request_qcode(qname2, sizeof(qname2) - 1, local_qcode,
			qpool_0, CCW_CI_ALL_INSTANCE, target)) {
		pixad_err("request qcode of %s failed\n", qname2);
		return -1;
	}

	/* 3.7 */
	if (0 != retire_single_qpool(target, copro_info, qpool_0)) {
		pixad_err("retire qpool 0 failed\n");
		return -1;
	}

	return 0;
}

int align_qpool_tables_packet_offset(struct pixad_user_proc *target,
					struct pixad_copro *copro_info)
{
	align_expansion_freepool(target, copro_info);
	align_prefix_local_uri_freepool(target, copro_info, prefix_qcode);
	align_prefix_local_uri_freepool(target, copro_info, local_qcode);
	align_prefix_local_uri_freepool(target, copro_info, uri_qcode);

	return 0;
}
