/*
 * IBM Power Edge of Network (PowerEN)
 *
 * Copyright 2010-2011 Massimiliano Meneghin, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/of.h>
#include <linux/of_platform.h>

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>	/* printk() */
#include <linux/slab.h>		/* kmalloc() */
#include <linux/fs.h>		/* everything... */
#include <linux/errno.h>	/* error codes */
#include <linux/types.h>	/* size_t */
#include <linux/proc_fs.h>
#include <linux/fcntl.h>	/* O_ACCMODE */
#include <linux/mm.h>
#include <linux/highmem.h>	/* kmap */
#include <linux/delay.h>
#include <asm/system.h>		/* cli(), *_flags */
#include <linux/uaccess.h>	/* copy_from/to_user */
#include <linux/io.h>		/* copy_from/to_user */
#include <linux/mman.h>
#include <linux/mmu_notifier.h>
#include <asm/copro-driver.h>
#include <asm/pixad-xml.h>
#include <linux/bootmem.h>
#include <platforms/wsp/copro/cop.h>
#include <platforms/wsp/copro/pbic.h>
#include <platforms/wsp/copro/unit_xmlx.h>

#include "pixad.h"
#include "mem_utils.h"
#include "proc_utils.h"
#include "xml_registers.h"
#include "xml_icws_structures.h"
#include "HW172320.h"

#define XML_UNIT_COMPATIBLE     "ibm,wsp-coprocessor-xmlx"

/* Global data */
static struct pixad_copro *copro_info;
static unsigned int copro_num;
static DEFINE_SPINLOCK(open_lock);
#ifdef DEBUG
u32 copro_debug;
#endif

static int in_be64_wait_bits_set(u64 *addr, u64 bits_mask)
{
	int i;
	const int RETRY_COUNT = 1000;

	for (i = 0; i < RETRY_COUNT; ++i) {
		u64 value = in_be64(addr);

		value &= bits_mask;
		if (value)
			return 0;

		usleep_range(100, 500);
	}

	return -1;
}

static int in_be64_wait_bits_clear(u64 *addr, u64 bits_mask)
{
	int i;
	const int RETRY_COUNT = 1000;

	for (i = 0; i < RETRY_COUNT; ++i) {
		u64 value = in_be64(addr);

		value &= bits_mask;
		if (!value)
			return 0;

		usleep_range(100, 500);
	}

	return -1;
}

static inline int out_be64_and_wait_bits_clear(u64 *addr, u64 out_val,
								u64 bits_mask)
{
	out_be64(addr, out_val);
	return in_be64_wait_bits_clear(addr, bits_mask);
}

static int set_takedown_vf_bit_to_recover_resouces(
						struct pixad_user_proc *target)
{
	u64 value;
	u64 *mmio_addr;
	int ret = 0;

	mmio_addr = (u64 *)
		((u64)copro_info[target->unit_id].copro_unit->mmio_addr +
							XML_MMIO_CTRL_REG);

	value = in_be64(mmio_addr);
	value |= XMLTRL_TDVF(target->vf_id);

	ret = out_be64_and_wait_bits_clear(mmio_addr, value,
						XMLTRL_TDVF(target->vf_id));

	if (ret != 0)
		pixad_err("failed to recover resources for VF%d\n",
							target->vf_id);
	return ret;
}

static int invalidate_cache(struct pixad_user_proc *target)
{
	u64 *mmio_addr;
	u64 value;
	int ret;

	mmio_addr = (u64 *)
		((u64)copro_info[target->unit_id].copro_unit->mmio_addr +
			XML_MMIO_INVALIDATE_CACHES(target->vf_id));

	value =	in_be64(mmio_addr);
	value |= XMLINVC_REQ;

	ret = out_be64_and_wait_bits_clear(mmio_addr, value, XMLINVC_REQ);

	if (ret != 0)
		pixad_err("failed to invalidate caches for VF%d\n",
								target->vf_id);
	return ret;
}


static int flush_engine_channels(struct pixad_user_proc *target)
{
	int engine;
	int ret;
	int session_id;
	const char flush_engine_text[] = "<?>";

	for (engine = 0; engine < 4; ++engine) {
		session_id = (engine + 1) & XMLSESSION_ID_MASK;
		ret = send_sync_crb(target->vf_id, flush_engine_text,
			(ARRAY_SIZE(flush_engine_text) - 1), session_id);
		if (ret != 0) {
			pixad_err("send_sync_crb: %d\n", ret);
			return ret;
		}

		ret = wait_session_closed_imqe(copro_info, target->unit_id,
					       target->vf_id, session_id);
		if (ret != 0) {
			pixad_err("wait no session close msg.ret = %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static int wait_on_pbic_idle(struct pixad_user_proc *target)
{
	int ret;
	u64 *mmio_addr;

	const int pbic_idle_reg_mmio_offset = 0x0f8;
	const int pbic_cop_queue_b4_bit = 23;
	const u64 pbic_idle_bitmask_vf =
		0x1ULL << (63 - (pbic_cop_queue_b4_bit + target->vf_id));

	mmio_addr = (u64 *)((u64)copro_info[target->unit_id].copro_unit->
		pbic->mmio_addr + pbic_idle_reg_mmio_offset);

	ret = in_be64_wait_bits_set(mmio_addr, pbic_idle_bitmask_vf);

	if (ret != 0)
		pixad_err("VF %d pbic_NOT idle\n", target->vf_id);

	return ret;
}

static int get_next_qpid(int curr_qpid)
{
	const int PIXAD_XML_QPOOL_NUM = 4;
	return ((curr_qpid + 1) < PIXAD_XML_QPOOL_NUM) ? (curr_qpid + 1) : 1;
}

static int flush_qcode(struct pixad_user_proc *target)
{
	const char flush_qname[] = "XML_VF_TAKE_DOWN";
	int ret = 0;
	struct pixad_xml_vf *vf = NULL;
	int qpid = 0;

	if (!target)
		return -1;

	vf = &copro_info[target->unit_id].vf_info[target->vf_id];
	qpid = get_next_qpid(vf->curr_qpid);

	ret = request_qcode(&flush_qname[0], sizeof(flush_qname) - 1,
			    local_qcode, qpid, CCW_CI_ALL_INSTANCE, target);
	if (ret != 0) {
		pixad_err("request_qcode error\n");
		return ret;
	}
	/* process fromxml msg to ensure SW Qcode request completes. */
	ret = wait_imqe(copro_info, target->unit_id,
				     target->vf_id, MSG_QCODE_BUFFER_READY);
	if (ret != 0) {
		pixad_err("wait_imqe error\n");
		return ret;
	}

	return 0;
}

static void unlink_target_proc(struct pixad_user_proc *target)
{
	struct list_head *pos, *q;
	struct pixad_user_proc *tmp;

	spin_lock(&open_lock);

	list_for_each_safe(pos, q, &(copro_info[target->unit_id].
			   vf_info[target->vf_id].proc_list.list)) {
		tmp = list_entry(pos, struct pixad_user_proc, list);
		if (tmp == target) {
			list_del(pos);

			copro_info[target->unit_id].
					vf_info[target->vf_id].num_users = 0;
			INIT_LIST_HEAD(&(copro_info[target->unit_id].
					vf_info[target->vf_id].proc_list.list));
			break;
		}
	}

	spin_unlock(&open_lock);
}

static void close_pending_sessions(struct pixad_user_proc *target)
{
	int sess_id;
	int ret;
	u8 *session_start;
	struct state_session_entry *cur;
	struct pixad_xml_vf *vf;

	vf = &copro_info[target->unit_id].vf_info[target->vf_id];
	session_start = (u8 *)vf->virt_session;

	cur = (struct state_session_entry *)session_start;

	/*
	 * skip the first 5 sessions (id 0~4) which is reserved for
	 * flushing engine channels.
	 */
	cur += 5;
	sess_id = 5;
	while ((u8 *)cur < (session_start + vf->size_session)) {
		if (cur->status == 0x2) {
			++cur;
			++sess_id;
			continue;
		}

		ret = send_close_msg(target->vf_id, sess_id);
		if (ret != 0) {
			pixad_err("error sending close crb for session %d\n",
							sess_id);
			break;
		}

		ret = wait_session_closed_imqe(copro_info, target->unit_id,
							target->vf_id, sess_id);

		if (ret != 0)
			pixad_err("HW does not respond close session %d\n",
							sess_id);
		else
			cur->status = 0x2;

		++cur;
		++sess_id;
	}
}

/*
 * This causes the PBIC not to drive any further replenish
 * messages into the XML for the taken down virtual function
 */
static void config_xml_pbic_spill_fill_queue_pause(
	int unit_id, int vf_id, int pause)
{
	u64 *pbic_spill_queue_start_addr_reg = NULL;
	u64 val = 0;
	u64 *pbic_mmio_base =
		copro_info[unit_id].copro_unit->pbic->mmio_addr;

	pbic_spill_queue_start_addr_reg = (u64 *)((u64)pbic_mmio_base +
		PBIC_MMIO_SPILL_QUEUE_START_ADDR(vf_id));

	val = in_be64(pbic_spill_queue_start_addr_reg);
	if (pause)
		val |= (PAUSE_ADD_MASK | PAUSE_DRAIN_MASK);
	else
		val &= ~(PAUSE_ADD_MASK | PAUSE_DRAIN_MASK);

	out_be64(pbic_spill_queue_start_addr_reg, val);
}

static void quiesce_replenish_channel(struct pixad_user_proc *target)
{
	u64 *pbic_queue_ctrl_reg = NULL;
	u64 *pbic_mmio_base =
		copro_info[target->unit_id].copro_unit->pbic->mmio_addr;

	/* set xml pbic spill fill queue VF buffer pause */
	config_xml_pbic_spill_fill_queue_pause(target->unit_id,
						target->vf_id, 1);

	/* The empty PBIC replenishes the channel spill queue */
	pbic_queue_ctrl_reg = (u64 *)((u64)pbic_mmio_base +
		PBIC_MMIO_QUEUE_CTRL(target->vf_id));
	out_be64(pbic_queue_ctrl_reg, 0);
}

static void workaround_HW172320(struct pixad_user_proc *target)
{
	retire_all_qpools(target, copro_info);
	align_qpool_tables_packet_offset(target, copro_info);
}

static int pixad_takedown_vf(struct pixad_user_proc *target)
{
	if (NULL == target)
		return -EINVAL;

	/* go through all steps regardless of any failed step */
	/* 1. Flush engine channels */
	flush_engine_channels(target);

	/* 2. Flush qcode channels */
	flush_qcode(target);

	workaround_HW172320(target);

	/* 3. Confirm engine flushes completed */
	/* 4. Cleanup application session */
	close_pending_sessions(target);

	/* 5. Quiesce XML replenish channel (DD3 only) */
	quiesce_replenish_channel(target);

	/* 6. Send "recover resource" indication (DD2 note: replenish queue
	 * must be full.) */
	set_takedown_vf_bit_to_recover_resouces(target);

	/* 7. Send "Cache invalidate" indication */
	invalidate_cache(target);

	/* 8. Wait on PBIC idle register to be set (no need on DD2 chips) */
	wait_on_pbic_idle(target);

	/* 9. Repeat step 6. */
	set_takedown_vf_bit_to_recover_resouces(target);

	/* free target proc memory */
	unlink_target_proc(target);
	__free_page(target->mempool.pg);
	kfree(target);
	target = NULL;

	pixad_debug("VF takedown process finished\n");
	return 0;
}

void pixad_mmu_release(struct mmu_notifier *mn, struct mm_struct *mm)
{
	struct pixad_user_proc *target;

	target = container_of(mn, struct pixad_user_proc, mmu_notifier);
	pixad_takedown_vf(target);
}

static int prepare_mem_for_VF_takedown(struct pixad_user_proc *tmp,
								int target_unit)
{
	int i;
	int ret;
	struct vm_area_struct *vma;
	unsigned long pfn;
	struct copro_map_args map_arg;
	struct page *pg = NULL;
	unsigned long mapping_addr = 0;

	pixad_debug("prepare mem for VF takedown\n");

	if (NULL == current->mm) {
		pixad_err("current->mm is NULL\n");
		return -ENOMEM;
	}

	pg = alloc_page(GFP_KERNEL);
	if (NULL == pg) {
		pixad_err("alloc_page\n");
		return -ENOMEM;
	}
	tmp->mempool.kv_start = (unsigned long)page_address(pg);
	tmp->mempool.size = PAGE_SIZE;

	for (i = 0; i < PAGE_SIZE; i++)
		((uint8_t *)tmp->mempool.kv_start)[i] = 0xAB;

	/* map this pool to userspace */
	down_write(&current->mm->mmap_sem);
	mapping_addr =
		do_mmap(NULL, 0, PAGE_SIZE, PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS | MAP_NORESERVE, 0);

	up_write(&current->mm->mmap_sem);

	if (IS_ERR((void *)mapping_addr)) {
		pixad_err("do_mmap: %ld\n", mapping_addr);
		ret = -ENOMEM;
		goto error_out;
	}
	vma = find_vma(current->mm, mapping_addr);
	if (NULL == vma) {
		pixad_err("can't find_vma\n");
		ret = -EINVAL;
		goto error_out;
	}

	vma->vm_flags |= (VM_IO | VM_LOCKED | VM_DONTCOPY);
	pfn =  tmp->mempool.kv_start >> PAGE_SHIFT;
	pixad_debug("kv_start=%p, pfn=0x%lx\n",
					(void *)tmp->mempool.kv_start, pfn);
	pixad_debug("vma_start=%p, size=%lu\n",
			(void *)vma->vm_start, vma->vm_end - vma->vm_start);
	if (remap_pfn_range(vma, vma->vm_start, pfn, PAGE_SIZE,
							vma->vm_page_prot)) {
		pixad_err("remap_pfn_range failed\n");
		ret = -EINVAL;
		goto error_out;
	}

	tmp->mempool.uv_start = mapping_addr;
	tmp->mempool.pg = pg;

	map_arg.count = 1;
	map_arg.flags = COPRO_MAP_BOLT;
	map_arg.entries[0].addr = tmp->mempool.uv_start;
	map_arg.entries[0].len = tmp->mempool.size;

	if (pbic_map(copro_info[target_unit].copro_unit->pbic, &map_arg) != 0) {
		pixad_err("pbic_map failed\n");
		ret = -EINVAL;
		goto error_out;
	}

	return 0;

error_out:
	if (!IS_ERR_OR_NULL((void *)mapping_addr)) {
		down_write(&current->mm->mmap_sem);
		do_munmap(current->mm, mapping_addr, PAGE_SIZE);
		up_write(&current->mm->mmap_sem);
	}

	if (pg)
		__free_page(pg);

	return ret;
}

static struct mmu_notifier_ops pixad_mmu_notifier_ops = {
		.release = pixad_mmu_release
};

/*
 * The open registers the process, retrieve bootmem allocation, allocate the
 * IMQ space Init step VF2~VF7 is side here is inside here
 */
static int pixad_open(struct inode *inode, struct file *file)
{
	int target_unit;
	int target_vf;
	struct pixad_user_proc *tmp = NULL;
	int ret;

	spin_lock(&open_lock);

	ret = find_free_VF(copro_num, copro_info, &target_unit, &target_vf);
	if (ret != 0) {
		pixad_err("no XML virtual function available\n");
		spin_unlock(&open_lock);
		return -EBUSY;
	}

	pixad_debug("allocating VF %d on XML unit %d to user\n",
							target_vf, target_unit);
	pixad_debug("qpool: %p\n",
		(void *)copro_info[target_unit].vf_info[target_vf].virt_qpool);
	pixad_debug("session: %p\n", (void *)
		copro_info[target_unit].vf_info[target_vf].virt_session);
	pixad_debug("imq: %p\n",
		(void *)copro_info[target_unit].vf_info[target_vf].virt_imq);

	/* Init steps VF2~VF5 inside */
	pixad_init_vf(&copro_info[target_unit], target_vf);

	tmp = kzalloc(sizeof(struct pixad_user_proc), GFP_KERNEL);
	if (!tmp) {
		copro_info[target_unit].vf_info[target_vf].num_users = 0;
		spin_unlock(&open_lock);
		pixad_err("kmalloc failed\n");
		return -ENOMEM;
	}

	/* clear xm pbic spill fill queue VF buffer pause */
	config_xml_pbic_spill_fill_queue_pause(target_unit, target_vf, 0);

	spin_unlock(&open_lock);

	pixad_debug("proc = %p\n", tmp);
	tmp->vf_id = target_vf;
	tmp->unit_id = target_unit;
	tmp->descriptor = file;

	/* lock is not needed because a VF can be owned by one user */
	list_add(&(tmp->list),
		&(copro_info[target_unit].vf_info[target_vf].proc_list.list));

	/* Reserver mem for later taking down VF */
	ret = prepare_mem_for_VF_takedown(tmp, target_unit);
	if (ret != 0) {
		unlink_target_proc(tmp);
		kfree(tmp);
		return ret;
	}

	/* Init step VF6/VF7 inside */
	run_vf(&(copro_info[target_unit]), target_vf);

	tmp->mmu_notifier.ops = &pixad_mmu_notifier_ops;
	ret = mmu_notifier_register(&tmp->mmu_notifier, current->mm);
	if (ret != 0)
		pr_warn("Unable to register mmu_notifier. "
				"XML VF takedown might fail. ret=%d\n", ret);

	return 0;
}

static inline void reset_session_state(struct pixad_xml_vf *vf, u32 session_id)
{
	struct state_session_entry *sessions =
				(struct state_session_entry *)vf->virt_session;

	sessions[session_id].status = 0x2;
}

static long pixad_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	static int is_frist_valid_bit_check_after_init = 1;
	int retval = 0;
	u32 intval;
	int vf_id;
	int unit_id;
	u32 read_ptr;
	u64 copro_mmio;
	struct pixad_xml_vf *vf = NULL;

	struct pixad_map_info info;
	struct pixad_user_proc *target;
	struct pixad_vf_info user_vf_info;

	target = find_proc(copro_num, copro_info, file);
	if (target == NULL) {
		pixad_err("ERROR process not registered before\n");
		return -EINVAL;
	}
	/* TODO: restore comment up here */
	/*
	pixad_debug("ioctr target = %p\n", target);
	pixad_debug("UNIT ID = %d\n", target->unit_id);
	pixad_debug("VF ID = %d\n", target->vf_id);
	*/

	vf_id = target->vf_id;
	unit_id = target->unit_id;
	copro_mmio = (u64)copro_info[unit_id].copro_unit->mmio_addr;

	switch (cmd) {

	case PIXAD_IOCTL_SET_ACTIVE_QPOOL_ID:
		if (copy_from_user(&intval, (u32 *)arg, sizeof(u32))) {
			pixad_err("PIXAD_IOCTL_SET_ACTIVE_QPOOL_ID\n");
			return -EFAULT;
		}
		pixad_debug("PIXAD_IOCTL_SET_ACTIVE_QPOOL_ID: %d\n", intval);
		vf = &copro_info[unit_id].vf_info[vf_id];
		vf->curr_qpid = intval;
	break;

	case PIXAD_IOCTL_GET_VF_INFO:
		pixad_debug("PIXAD_IOCTL_GET_VF_INFO\n");
		/* init user_vf_info */
		user_vf_info.vf_id = vf_id;
		user_vf_info.type = copro_info[unit_id].copro_unit->types;

		/* the user pace dispatcher retrive memory */

		if (copy_to_user((struct pixad_vf_info *)arg, &user_vf_info,
						sizeof(struct pixad_vf_info))) {
			pixad_err("PIXAD_IOCTL_GET_VF_INFO\n");
			return -EFAULT;
		}
		break;

	case PIXAD_IOCTL_MMAP_IMQ:
		pixad_debug("PIXAD_IOCTL_MMAP_IMQ\n");
		info.size = copro_info[unit_id].vf_info[vf_id].size_imq;
		target->map_addr =
		    (unsigned long)copro_info[unit_id].vf_info[vf_id].virt_imq;

		if (copy_to_user((struct pixad_map_info *)arg, &info,
					sizeof(struct pixad_map_info))) {
			pixad_err("PIXAD_IOCTL_MMAP_IMQ\n");
			return -EFAULT;
		}

		target->map_target = MAP_TARGET_IMQ;
		break;

	case PIXAD_IOCTL_MMAP_QPOOL:
		pixad_debug("PIXAD_IOCTL_MMAP_QPOOL\n");
		info.size = copro_info[unit_id].vf_info[vf_id].size_qpool;
		target->map_addr =
		    (unsigned long)copro_info[unit_id].vf_info[vf_id].
		    virt_qpool;

		if (copy_to_user((struct pixad_map_info *)arg, &info,
					sizeof(struct pixad_map_info))) {
			pixad_err("PIXAD_IOCTL_MMAP_QPOOL failed\n");
			return -EFAULT;
		}

		target->map_target = MAP_TARGET_QPOOL;
		break;

	case PIXAD_IOCTL_MMAP_MMIO:
		pixad_debug("PIXAD_IOCTL_MMAP_MMIO\n");
		target->map_target = MAP_TARGET_MMIO;
		target->map_addr =
		    (unsigned long)copro_info[unit_id].copro_unit->mmio_addr;
		break;

	case PIXAD_IOCTL_GET_IMQ_MAX_INDEX: /* Return the imq buffer size*/
		pixad_debug("PIXAD_IOCTL_GET_IMQ_MAX_INDEX\n");
		intval = copro_info[unit_id].vf_info[vf_id].max_index_imq;
		if (copy_to_user((u32 *)arg, &intval, sizeof(u32))) {
			pixad_err("PIXAD_IOCTL_GET_IMQ_MAX_INDEX failed\n");
			return -EFAULT;
		}
		break;

	case PIXAD_IOCTL_GET_IMQ_VALID_BIT:
		pixad_debug("PIXAD_IOCTL_GET_IMQ_VALID_BIT\n");
		/*
		 * First read for valid bit will return 0 when it should be 1.
		 * Guess the valid bit will be change to 1 when 1st IMQE is set
		 * We work around that by add this check. For 2nd round read,
		 * it will check register
		 */
		if (is_frist_valid_bit_check_after_init) {
			intval = 1;
			is_frist_valid_bit_check_after_init = 0;
		} else {
			intval = xmlreg_get_imq_valid_bit(copro_mmio, vf_id);
		}

		if (copy_to_user((u32 *)arg, &intval, sizeof(u32))) {
			pixad_err("IO_GET_IMQ_VALID_BIT failed\n");
			return -EFAULT;
		}

		break;

	case PIXAD_IOCTL_GET_IMQ_READ_INDEX:
		pixad_debug("PIXAD_IOCTL_GET_IMQ_READ_INDEX\n");
		read_ptr = xmlreg_get_imq_read_ptr(copro_mmio, vf_id);

		if (copy_to_user((u32 *)arg, &read_ptr, sizeof(u32))) {
			pixad_err("PIXAD_IOCTL_GET_IMQ_READ_IDX failed\n");
			return -EFAULT;
		}
		break;

	case PIXAD_IOCTL_SET_IMQ_READ_INDEX:
		/* pixad_debug("PIXAD_IOCTL_SET_IMQ_READ_INDEX\n"); */
		if (copy_from_user(&read_ptr, (u32 *)arg, sizeof(u32))) {
			pixad_err("PIXAD_IOCTL_SET_IMQ_READ_INDEX failed\n");
			return -EFAULT;
		}

		xmlreg_set_imq_read_ptr(copro_mmio, vf_id, read_ptr);
		break;

	case PIXAD_IOCTL_MMAP_TAKEDOWN_MPOOL:
		pixad_debug("PIXAD_IOCTL_MMAP_TAKEDOWN_MPOOL\n");
		target->map_target = MAP_TARGET_TAKEDOWN_MPOOL;
		info.size = target->mempool.size;
		info.offset = target->mempool.uv_start;
		if (copy_to_user((struct pixad_map_info *)arg, &info,
					sizeof(struct pixad_map_info))) {
			pixad_err("PIXAD_IOCTL_MMAP_TAKEDOWN_MPOOL failed\n");
			return -EFAULT;
		}
		break;

	case PIXAD_IOCTL_RESET_SESSION_ID:
		if (copy_from_user(&intval, (u32 *)arg, sizeof(u32))) {
			pixad_err("PIXAD_IOCTL_RESET_SESSION_ID failed\n");
			return -EFAULT;
		}
		pixad_debug("PIXAD_IOCTL_RESET_SESSION_ID: %d\n", intval);
		reset_session_state(&copro_info[unit_id].vf_info[vf_id],
									intval);
		break;
	default:
		retval = -EINVAL;
	}

	return retval;
}

static int pixad_mmap(struct file *filp, struct vm_area_struct *vma)
{
	long length;
	int vf_id;
	struct pixad_user_proc *target;
	unsigned long pfn;

	target = find_proc(copro_num, copro_info, filp);
	if (target == NULL) {
		pixad_err("ERROR proc not registered before...\n");
		return -EIO;
	}
	pixad_debug("target: %p\n", target);

	pixad_debug("UNIT ID %d\n", target->unit_id);
	pixad_debug("VF ID %d\n", target->vf_id);
	vf_id = target->vf_id;

	pixad_debug("map_trget %d", target->map_target);
	if (target->is_mapped[target->map_target] == true) {
		pixad_err("already mmaped\n");
		return -EIO;
	}

	/* Remapping */
	if (target->map_target == MAP_TARGET_MMIO) {

		length = vma->vm_end - vma->vm_start;

		vma->vm_flags |= (VM_IO | VM_RESERVED | VM_DONTCOPY);
		pfn = ((unsigned long)target->map_addr) >> PAGE_SHIFT;

		if (remap_pfn_range
		    (vma, vma->vm_start, pfn, length,
		     vma->vm_page_prot)) {
			pixad_err("remap_pfn_range failed\n");
			return -EAGAIN;
		}

	} else {

		unsigned long pfn;
		long length = vma->vm_end - vma->vm_start;
		pixad_debug("length %d\n", (int)length);
		pfn = virt_to_phys((void *)target->map_addr) >> PAGE_SHIFT;
		if (remap_pfn_range
		    (vma, vma->vm_start, pfn, length,
		     vma->vm_page_prot)) {
			pixad_err("ERROR remap_pfn_range\n");
			return -EAGAIN;
		}
	}
	target->is_mapped[target->map_target] = true;

	return 0;
}


static int pixad_release_new(struct inode *inode, struct file *file)
{
	/*
	 * Actual cleanup code is in the mmu_notifier release callback,
	 * pixad_takedown_vf.
	 */
	return 0;
}

/* Allocate memory from struct proc_t's mpool. */
int mpool_alloc(struct pixad_mem_pool *pool, unsigned int size,
			unsigned int align, void **uvaddr, void **kvaddr)
{
	unsigned long curr_uvaddr = pool->uv_start + pool->cur_ptr;
	unsigned long aligned_uvaddr = ALIGN(curr_uvaddr, align);

	/* check new uvaddr is in valid range */
	unsigned long aligned_uvaddr_end = aligned_uvaddr + size - 1;
	unsigned long mpool_uvaddr_end = pool->uv_start + pool->size - 1;

	if (aligned_uvaddr_end > mpool_uvaddr_end)
		return -ENOMEM;

	/* get algined pool->cur_ptr */
	*uvaddr = (void *)aligned_uvaddr;
	pool->cur_ptr = aligned_uvaddr - pool->uv_start;

	/* set kvaddr = pool->kv_start + pool->cur_ptr */
	*kvaddr = (void *)(pool->kv_start + pool->cur_ptr);

	/* update pool->cur_ptr with allocated 'size' */
	pool->cur_ptr += size;

	return 0;
}

/* define which file operations are supported */
static const struct file_operations pixad_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = pixad_ioctl,
	.mmap = pixad_mmap,
	.open = pixad_open,
	.release = pixad_release_new
};

/*
 * Called when the device is initilize in kernel
 * Init step G1/G2/VF1 is inside here
 */
int __init pixad_init(void)
{
	int i;
	int ret;

	copro_num = wsp_xml_get_num_device();

	if (copro_num == 0) {
		pr_info("%s: XML unit does not exist or has been turned off\n",
			__func__);
		return -ENODEV;
	}

	pixad_debug("copro_num = %d\n", copro_num);

	copro_info = kmalloc(sizeof(struct pixad_copro) * copro_num,
								GFP_KERNEL);
	if (NULL == copro_info) {
		pixad_err("kmalloc error\n");
		return -ENOMEM;
	}

	/*
	 * Init PiXAD data structures pointers of memory buffers for the XML
	 * Refer to XML workbook 2.7.3 Device Driver/Hypervisor init sequence
	 * for initialization steps
	 * Gereral setting Steps 1-2 (G1/G2) and VF Step 1 (VF1) are inside the
	 * function
	 */
	init_xml_data_structures(copro_num, copro_info);

	/*
	 * Init all the transient memory
	 * ? G2 was done again, why?
	 * Also register the address to MMIO
	 */
	for (i = 0; i < copro_num; i++) {
		ret = pixad_init_xml_unit(&copro_info[i]);
		if (ret != 0)
			goto error_out;
	}


	/* Init PiXAD internal processes data structures */
	init_pixad_proc_set(copro_num, copro_info);

	/* Registering Char dev */
	ret = register_chrdev(PIXAD_MAJOR, PIXAD_NAME, &pixad_fops);
	if (ret != 0) {
		pixad_err("Driver Registration Error\n");
		goto error_out;
	}

	return 0;

error_out:
	/* kfree(NULL) is safe */
	kfree(copro_info);
	copro_info = NULL;

	return ret;
}

static void __exit pixad_cleanup(void)
{
	pixad_debug("cleaning up pixad module\n");
	kfree(copro_info);
	copro_info = NULL;

	unregister_chrdev(PIXAD_MAJOR, PIXAD_NAME);
}



module_init(pixad_init);
module_exit(pixad_cleanup);

MODULE_AUTHOR("Massimiliano Meneghin");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PiXAD device for PowerEN accelerator");
