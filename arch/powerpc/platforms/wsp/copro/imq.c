/*
 * Copyright 2009-2011 Michael Ellerman, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/debugfs.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/uaccess.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/io.h>
#include <linux/mmu_context.h>
#include <linux/ratelimit.h>
#include <asm/copro-driver.h>

#include <mm/icswx.h>

#include "cop.h"
#include "imq.h"
#include "../ics.h"

#define COPRO_IMQ_COMPAT	"ibm,coprocessor-unit-imq"
#define COPRO_HVIMQ_COMPAT	"ibm,coprocessor-unit-hvimq"

/* Some devices have different register layouts */
struct imq_reg_layout {
	u8 isn;
	u8 queue_base;
	u8 queue_size;
	u8 pace_ctrl;
	u8 read_ptr;
	u8 int_ctrl;
	u8 int_status;
};

static struct imq_reg_layout imq_reg_layouts[] = {
#define IMQ_LAYOUT_STANDARD	0
	{ 0x0, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30 },
#define IMQ_LAYOUT_DMA		1
	{ 0x0, 0x08, 0x10, 0x20, 0x18, 0x30, 0x28 },
#define IMQ_LAYOUT_XML		2
	{ 0x0, 0x28, 0x50, 0xa0, 0x78, 0xC8, 0xF0 },
};

#define IMQ_REG_ISN(imq)	imq_reg_layouts[imq->layout].isn
#define IMQ_REG_QUEUE_BASE(imq)	imq_reg_layouts[imq->layout].queue_base
#define IMQ_REG_QUEUE_SIZE(imq)	imq_reg_layouts[imq->layout].queue_size
#define IMQ_REG_PACE_CTRL(imq)	imq_reg_layouts[imq->layout].pace_ctrl
#define IMQ_REG_READ_PTR(imq)	imq_reg_layouts[imq->layout].read_ptr
#define IMQ_REG_INT_CTRL(imq)	imq_reg_layouts[imq->layout].int_ctrl
#define IMQ_REG_INT_STATUS(imq)	imq_reg_layouts[imq->layout].int_status

#define IMQ_TRIGGER_COMPLETION	0
#define IMQ_TRIGGER_CSB_ERROR	1
#define IMQ_TRIGGER_CCB_ERROR	2
#define IMQ_TRIGGER_CRB_ERROR	3

#define IMQ_STATE_OK		0
#define IMQ_STATE_ALMOST_FULL	1
#define IMQ_STATE_FULL		3

/* FIXME If we can do sync with interrupt completions then this can
 * go away, we'll just wait for the last sync completion and be done. */
#define IMQ_INVALID_AS	1

#define imq_debug(_imq, fmt, ...)			\
	cop_debug("%s " fmt, _imq->name, ##__VA_ARGS__)


static int imqe_valid(struct copro_imq_entry *imqe, struct copro_imq *imq)
{
	return copro_imqe_valid(imqe) == imq->valid;
}

static struct task_struct *find_task_for_mm(struct mm_struct *mm)
{
	struct task_struct *tsk;

	rcu_read_lock();

	/* This sucks rocks but should be uncommon */
	for_each_process(tsk) {
		if (tsk->mm == mm) {
			get_task_struct(tsk);
			goto out;
		}
	}

	tsk = NULL;
out:
	rcu_read_unlock();

	return tsk;
}

/* Yes, this is horrid. */
static void imqe_two_stage_copy(struct copro_imq *imq,
				struct copro_imq_entry *imqe, void *src,
				unsigned long dst, int size,
				void *src2, unsigned long dst2, int size2)
{
	struct task_struct *tsk;
	struct mm_struct *mm;
	int rc;

	mm = mm_lookup_by_id(copro_imqe_pid(imqe));

	if (!mm) {
		pr_warning("%s: No MM for CSB/CCB write back\n", imq->name);
		return;
	}

	tsk = cop_driver_handle_imqe(imq, imqe, mm);

	if (size == 0)
		goto out;

	if (!tsk) {
		tsk = find_task_for_mm(mm);

		if (!tsk) {
			pr_err("%s: No process found for CSB/CCB write back\n",
			       imq->name);
			goto out;
		}
	}

	rc = access_process_vm(tsk, dst, src, size, 1);

	if (rc && src2 && size2) {
		wmb();
		rc = access_process_vm(tsk, dst2, src2, size2, 1);
	}

	if (rc == 0)
		pr_warn_ratelimited("%s: Error copying CSB/CCB to 0x%lx\n",
				    imq->name, dst);

out:
	if (tsk)
		put_task_struct(tsk);

	mmput(mm);
}

static void imqe_copy(struct copro_imq *imq, struct copro_imq_entry *imqe,
		      void *src, unsigned long dst, int size)
{
	return imqe_two_stage_copy(imq, imqe, src, dst, size, NULL, 0, 0);
}

struct imqe_csb {
	u16 valid;
	u8 cc;
	u8 ce;
	u32 pbc;
	u64 addr;
};

static void imqe_copy_csb(struct copro_imq *imq, struct copro_imq_entry *imqe)
{
	struct imqe_csb *csb;
	unsigned long dst;
	void *src;

	dst = copro_imqe_csb_ptr(imqe);
	src = (void *)&imqe->csb;

	csb = (struct imqe_csb *)&imqe->csb;

	/* Set the CE bit to tell userspace about the CSB failure */
	csb->ce |= COPRO_CSB_CE_CSB_ERROR;

	imq_debug(imq, "v:%d cc:%d addr:0x%llx dest:0x%lx\n",
		  (csb->valid) ? 1 : 0, csb->cc, csb->addr, dst);

	/* Copy the body of the CSB, and then the word containing valid */
	imqe_two_stage_copy(imq, imqe, src + 8, dst + 8, 64 - 8,
			    src, dst, 8);
}

#define CCB_CM_CHAIN_MASK	0x4
#define CCB_CM_MEANING_MASK	0x3
#define CCB_METHOD_STORE	0
#define CCB_METHOD_INTERRUPT	1
#define CCB_METHOD_JOE		2

#define CCB_CV_MASK		0x3FFFFFFFFFFFFFFFull
#define CCB_CA_MASK		0xFFFFFFFFFFFFFFFCull
#define CCB_CM_MASK		0x0000000000000003ull

static void imqe_process_ccb(struct copro_imq *imq,
			     struct copro_imq_entry *imqe)
{
	ulong dst;
	u64 src;
	u64 cv;
	u64 ca;
	int cm;

	cv = imqe->ccb[0] & CCB_CV_MASK;
	ca = imqe->ccb[1] & CCB_CA_MASK;
	cm = imqe->ccb[1] & CCB_CM_MASK;

	/* FIXME We don't know if this is last in chain or not, so always do
	 * the completion regardless of the high CM bit. */
	cm &= CCB_CM_MEANING_MASK;

	switch (cm) {
	case CCB_METHOD_STORE:
		dst = ca;
		src = cv;
		imq_debug(imq, "CCB store completion to 0x%lx\n", dst);
		imqe_copy(imq, imqe, &src, dst, sizeof(src));
		break;
	case CCB_METHOD_INTERRUPT:
		/* FIXME */
		imq_debug(imq, "Not-yet-implemented interrupt completion\n");
		break;
	case CCB_METHOD_JOE:
	default:
		pr_warn_ratelimited("%s: Unsupported CCB method\n", imq->name);
	}
}

static void imqe_process(struct copro_imq *imq, struct copro_imq_entry *imqe)
{
	imq_debug(imq, "IMQE@%p v:%d trigger:0x%x st:%d ovfl:%d src:%d "
		  "pid:0x%x csb:0x%llx\n", imqe, copro_imqe_valid(imqe),
		  copro_imqe_trigger(imqe), copro_imqe_state(imqe),
		  copro_imqe_overflow(imqe), copro_imqe_src_vf(imqe),
		  copro_imqe_pid(imqe), copro_imqe_csb_ptr(imqe));

	if (imq->is_hv && copro_imqe_state(imqe) != IMQ_STATE_OK) {
		imq_debug(imq, "IMQ %d is %sfull\n", copro_imqe_src_vf(imqe),
			  copro_imqe_state(imqe) == IMQ_STATE_FULL ?
			  "" : "almost ");
	} else {
		WARN_ON(copro_imqe_src_vf(imqe) != imq->number);
		WARN_ON(copro_imqe_overflow(imqe));
	}

	if (copro_imqe_as(imqe) == IMQ_INVALID_AS) {
		imq_debug(imq, "skipping discarded imqe %p\n", imqe);
		return;
	}

	if (!imq->is_hv || copro_imqe_overflow(imqe)) {
		switch (copro_imqe_trigger(imqe)) {
		case IMQ_TRIGGER_CSB_ERROR:
			imqe_copy_csb(imq, imqe);
			break;
		case IMQ_TRIGGER_CCB_ERROR:
			imqe_process_ccb(imq, imqe);
			break;
		case IMQ_TRIGGER_COMPLETION:
			imqe_copy(imq, imqe, NULL, 0, 0);
			break;
		default:
			imq_debug(imq, "unhandled trigger type %d\n",
				  copro_imqe_trigger(imqe));
		}
	}
}

static void imq_write_read_pointer(struct copro_imq *imq)
{
	int offset;
	u64 reg;

	mutex_lock(&imq->lock);

	offset = imq->next - imq->queue;

	reg = offset * sizeof(*imq->next);

	out_be64(imq->mmio_addr + IMQ_REG_READ_PTR(imq), reg);

	mutex_unlock(&imq->lock);

	imq_debug(imq, "wrote read ptr 0x%llx\n", reg);
}

static struct copro_imq_entry *imq_dequeue(struct copro_imq *imq)
{
	struct copro_imq_entry *imqe;

	mutex_lock(&imq->lock);

	if (imq->next == imq->queue + imq->nr_entries) {
		imq->valid = !imq->valid & 0x1;
		imq->next = imq->queue;
		imq_debug(imq, "queue wrapped, valid = %d\n", imq->valid);
	}

	if (imqe_valid(imq->next, imq))
		imqe = imq->next++;
	else
		imqe = NULL;

	mutex_unlock(&imq->lock);

	return imqe;
}

static void imq_work_handler(struct work_struct *work)
{
	struct copro_imq_entry *imqe;
	struct copro_imq *imq;

	imq = container_of(work, struct copro_imq, work);

	imq_debug(imq, "running work\n");

#ifdef DEBUG
	if (imq->paused) {
		imq_debug(imq, "paused\n");
		return;
	}
#endif

	imqe = imq_dequeue(imq);
	if (!imqe) {
		imq_debug(imq, "no imqe found\n");
		return;
	}

	while (imqe) {
		while (imqe) {
			imqe_process(imq, imqe);
			imqe = imq_dequeue(imq);
		}

		imq_write_read_pointer(imq);
		imqe = imq_dequeue(imq);
	}
}

static irqreturn_t imq_handler(int irq, void *data)
{
	struct copro_imq *imq = data;

	imq_debug(imq, "scheduling work\n");
	schedule_work(&imq->work);

	return IRQ_HANDLED;
}

static void imq_discard_imqes(struct copro_imq *imq, unsigned int pid)
{
	struct copro_imq_entry *imqe, *next;
	int valid;

	mutex_lock(&imq->lock);

	valid = imq->valid;
	next = imq->next;
	do {
		if (next == imq->queue + imq->nr_entries) {
			valid = !valid & 0x1;
			next = imq->queue;
			imq_debug(imq, "discard wrapped, valid = %d\n", valid);
		}

		imqe = next++;
		if (copro_imqe_valid(imqe) != valid)
			break;

		if (copro_imqe_pid(imqe) != pid)
			continue;

		copro_imqe_set_as(imqe, IMQ_INVALID_AS);
		imq_debug(imq, "discard IMQE %p for pid %d\n", imqe, pid);
	} while (1);

	mutex_unlock(&imq->lock);
}

void copro_imq_exit_mm_context(struct mm_struct *mm)
{
	struct copro_instance *copro;
	struct copro_type *type;
	struct copro_imq *imq;
	int i;

	list_for_each_entry(type, &copro_type_list, list) {
		if (!mm_used_copro_type(mm, type->type))
			continue;

		list_for_each_entry(copro, &type->instance_list, type_list) {
			for (i = 0; i < MAX_NUMBER_OF_IMQS; i++) {
				imq = copro->unit->imq[i];
				if (imq)
					imq_discard_imqes(imq, mm->context.id);
			}
		}
	}
}

#ifdef DEBUG
static int imq_pause_set(void *data, u64 val)
{
	struct copro_imq *imq = data;

	val = val & 0x1;

	if (imq->paused == val) {
		imq_debug(imq, "already %s\n",
			  imq->paused ? "paused" : "active");
		return 0;
	}

	imq->paused = val;

	imq_debug(imq, "%spaused\n", imq->paused ? "" : "un");

	if (!imq->paused) {
		/* We might have ignored an interrupt, rehandle now */
		imq_debug(imq, "scheduling work\n");
		schedule_work(&imq->work);
	}

	return 0;
}

static int imq_pause_get(void *data, u64 *val)
{
	struct copro_imq *imq = data;
	*val = imq->paused;
	return 0;
}

DEFINE_SIMPLE_ATTRIBUTE(imq_pause_fops, imq_pause_get, imq_pause_set, "%llu\n");
#endif /* DEBUG */

#ifdef CONFIG_PPC_WSP_COPRO_DEBUGFS
static int imq_regs_show(struct seq_file *m, void *private)
{
	struct copro_imq *imq = m->private;
	u64 val;

	seq_printf(m, "IMQ %s queue @ %#llx virq %d nr_entries %d\n",
		   imq->name, (u64)__pa(imq->queue),
		   imq->virq, imq->nr_entries);

	val = in_be64(imq->mmio_addr + IMQ_REG_ISN(imq));
	seq_printf(m, "irq number:   0x%016llx\n", val);
	val = in_be64(imq->mmio_addr + IMQ_REG_QUEUE_BASE(imq));
	seq_printf(m, "queue base:   0x%016llx\n", val);
	val = in_be64(imq->mmio_addr + IMQ_REG_QUEUE_SIZE(imq));
	seq_printf(m, "queue size:   0x%016llx\n", val);
	val = in_be64(imq->mmio_addr + IMQ_REG_PACE_CTRL(imq));
	seq_printf(m, "pace control: 0x%016llx\n", val);
	val = in_be64(imq->mmio_addr + IMQ_REG_READ_PTR(imq));
	seq_printf(m, "read pointer: 0x%016llx\n", val);
	val = in_be64(imq->mmio_addr + IMQ_REG_INT_CTRL(imq));
	seq_printf(m, "int control:  0x%016llx\n", val);
	val = in_be64(imq->mmio_addr + IMQ_REG_INT_STATUS(imq));
	seq_printf(m, "int status:   0x%016llx\n", val);

	return 0;
}

static int imq_regs_open(struct inode *inode, struct file *file)
{
	return single_open(file, imq_regs_show, inode->i_private);
}

static const struct file_operations imq_regs_fops = {
	.open = imq_regs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int imq_generic_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;
	return 0;
}

static ssize_t imq_queue_read(struct file *file, char __user *buf,
			      size_t len, loff_t *ppos)
{
	struct copro_imq *imq = file->private_data;
	int avail = imq->nr_entries * sizeof(struct copro_imq_entry);

	return simple_read_from_buffer(buf, len, ppos, imq->queue, avail);
}

static const struct file_operations imq_queue_fops = {
	.open = imq_generic_open,
	.read = imq_queue_read,
};

static ssize_t imq_inject_write(struct file *file, const char __user *buf,
				size_t len, loff_t *ppos)
{
	struct copro_imq *imq = file->private_data;
	struct copro_imq_entry imqe;
	int avail = len;

	while (avail >= sizeof(imqe)) {
		if (copy_from_user(&imqe, buf, sizeof(imqe)))
			return -EFAULT;

		imqe_process(imq, &imqe);

		buf += sizeof(imqe);
		avail -= sizeof(imqe);
	}

	return len - avail;
}

static const struct file_operations imq_inject_fops = {
	.open = imq_generic_open,
	.write = imq_inject_write,
};

static int imq_next_get(void *data, u64 *val)
{
	struct copro_imq *imq = data;
	*val = imq->next - imq->queue;
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(imq_next_fops, imq_next_get, NULL, "%llu\n");

static inline void copro_imq_debug_init(struct copro_imq *imq)
{
	struct dentry *d;
	char name[16];

	snprintf(name, sizeof(name), "imq%d", imq->number);
	d = debugfs_create_dir(name, imq->unit->dentry);

	if (!d)
		return;

	imq->paused = 0;
	debugfs_create_u32("nr_entries", 0400, d, &imq->nr_entries);
	debugfs_create_u8("valid", 0600, d, &imq->valid);
	debugfs_create_file("queue", 0400, d, imq, &imq_queue_fops);
	debugfs_create_file("next", 0400, d, imq, &imq_next_fops);
	debugfs_create_file("regs", 0400, d, imq, &imq_regs_fops);
#ifdef DEBUG
	debugfs_create_file("pause", 0600, d, imq, &imq_pause_fops);
	debugfs_create_file("inject", 0200, d, imq, &imq_inject_fops);
#endif
}
#else /* !CONFIG_PPC_WSP_COPRO_DEBUGFS */
static inline void copro_imq_debug_init(struct copro_imq *imq) { }
#endif

static uint nr_entries = 1024;
static int __init setup_imq_entries(char *str)
{
	ulong ents;

	ents = memparse(str, NULL);

	ents = roundup_pow_of_two(ents);
	if (ents < 4)
		ents = 4;
	if (ents > (64 << 10))
		ents = 64 << 10;

	return 1;
}
__setup("imqes=", setup_imq_entries);

static inline int param_set_imq_size(const char *val, struct kernel_param *kp)
{
	struct kernel_param tmp_kp;
	unsigned int value;
	int order, rc;

	if (!val)
		return -EINVAL;

	tmp_kp.arg = &value;
	rc = param_set_uint(val, &tmp_kp);
	if (rc)
		return rc;

	/* Must be a power of 2 */
	order = get_count_order(value);
	value = 1 << order;

	/* Must be between 4 and 64K */
	if (value < 4)
		value = 4;
	else if (value > 0x10000)
		value = 0x10000;

	*(unsigned int *)kp->arg = value;

	return 0;
}

static void imq_dd1_fixup(struct copro_imq *imq, struct device_node *dn)
{
#ifdef CONFIG_WORKAROUND_DD1_IMQ
	if (of_device_is_compatible(dn->parent, "ibm,wsp-coprocessor-cdmx")) {
		/*
		 * Fixups for erratum 208. The first IMQE is written to
		 * position 1 in the queue. We need to update our next pointer
		 * to expect this, and also mark entry 0 as valid for when we
		 * wrap. This also affects the read pointer logic, we need to
		 * write our read pointer as behind by one - ie. the entry
		 * we just read, not the next we'll read.
		 */
		imq->ptr_offset = 1;
		copro_imqe_set_valid(imq->next, 1);
		imq->next += imq->ptr_offset;
		return;
	}
#endif
}

static int imq_setup_name(struct copro_imq *imq)
{
	int rc, size;

	size = 0;
reprint:
	rc = snprintf(imq->name, size, "%s-%simq%d", imq->unit->name,
		      imq->is_hv ? "hv" : "", imq->number);

	if (rc >= size) {
		size = rc + 1; /* Need space for NULL */
		imq->name = kmalloc(size, GFP_KERNEL);
		if (!imq->name)
			return -ENOMEM;
		goto reprint;
	}

	return 0;
}

static int copro_imq_probe(struct device_node *dn,
			   struct platform_device *unit_pdev)
{
	void __iomem *mmio_addr;
	struct copro_unit *unit;
	struct copro_imq *imq;
	int qsize, rc, hwirq;
	unsigned long addr;
	unsigned int virq;
	const u32 *vfnum;
	u64 reg;

	mmio_addr = of_iomap(dn, 0);
	if (mmio_addr == NULL) {
		pr_err("Error iomapping reg %s\n", dn->full_name);
		return -EINVAL;
	}

	vfnum = of_get_property(dn, "ibm,vf-number", NULL);
	if (!vfnum)	/* FIXME support for old device tree */
		vfnum = of_get_property(dn, "vf-number", NULL);

	if (!vfnum) {
		pr_err("No ibm,vf-number property found %s\n", dn->full_name);
		return -EINVAL;
	}

	if (*vfnum >= MAX_NUMBER_OF_IMQS) {
		pr_err("ibm,vf-number %d out of range %s\n", *vfnum,
		       dn->full_name);
		return -EINVAL;
	}

	unit = dev_get_drvdata(&unit_pdev->dev);
	if (unit->imq[*vfnum] != NULL) {
		pr_err("imq %d already exists! %s\n", *vfnum, dn->full_name);
		return -EEXIST;
	}

	hwirq = wsp_ics_alloc_irq(dn, 1);
	if (hwirq < 0) {
		pr_err("Failed to allocate hwirq for %s\n", dn->full_name);
		return hwirq;
	}

	virq = irq_create_mapping(NULL, hwirq);
	if (virq == NO_IRQ) {
		pr_err("Failed mapping irq %d %s\n", *vfnum, dn->full_name);
		return -ENOMEM;
	}

	imq = kzalloc(sizeof(*imq), GFP_KERNEL);
	if (!imq) {
		rc = -ENOMEM;
		goto out_dispose_mapping;
	}

	if (of_device_is_compatible(dn, COPRO_HVIMQ_COMPAT))
		imq->is_hv = 1;

	if (of_device_is_compatible(dn->parent, "ibm,wsp-coprocessor-pcieep"))
		imq->layout = IMQ_LAYOUT_DMA;

	if (of_device_is_compatible(dn->parent, "ibm,wsp-coprocessor-xmlx"))
		imq->layout = IMQ_LAYOUT_XML;

	imq->unit = unit;
	imq->number = *vfnum;
	imq->nr_entries = nr_entries;

	rc = imq_setup_name(imq);
	if (rc)
		goto out_free_imq;

	rc = request_irq(virq, imq_handler, 0, imq->name, imq);
	if (rc) {
		pr_err("Failed requesting irq %d, %s\n", virq, dn->full_name);
		goto out_free_name;
	}

	qsize = imq->nr_entries * sizeof(struct copro_imq_entry);
	addr = __get_free_pages(GFP_KERNEL | __GFP_ZERO, get_order((qsize)));
	if (!addr) {
		rc = -ENOMEM;
		pr_err("Failed allocating queue for %s\n", dn->full_name);
		goto out_free_irq;
	}
	imq->queue = (struct copro_imq_entry *)addr;

	of_node_get(dn);
	get_device(&unit_pdev->dev);
	imq->mmio_addr = mmio_addr;

	mutex_init(&imq->lock);
	INIT_WORK(&imq->work, imq_work_handler);
	imq->next = imq->queue;
	imq->virq = virq;
	imq->valid = 1;

	imq_dd1_fixup(imq, dn);

	reg = ~((imq->nr_entries * sizeof(struct copro_imq_entry)) - 1);
	out_be64(mmio_addr + IMQ_REG_QUEUE_SIZE(imq), reg & 0x3fff00);
	out_be64(mmio_addr + IMQ_REG_ISN(imq), hwirq);
	out_be64(mmio_addr + IMQ_REG_QUEUE_BASE(imq), __pa(imq->queue));
#ifdef CONFIG_WORKAROUND_DD1_IMQ
	reg = 0;
#else
	/* Enable AF events */
	reg = 1;
#endif
	out_be64(mmio_addr + IMQ_REG_PACE_CTRL(imq), reg);
	out_be64(mmio_addr + IMQ_REG_INT_CTRL(imq), 0x1);

	copro_imq_debug_init(imq);

	pr_debug("%s queue @ %#llx irq %d/%d nr_entries %d\n",
		 imq->name, (u64)__pa(imq->queue), virq,
		 hwirq, imq->nr_entries);

	/* Install ourself */
	unit->imq[imq->number] = imq;

	return 0;

out_free_irq:
	free_irq(virq, imq);
out_free_name:
	kfree(imq->name);
out_free_imq:
	kfree(imq);
out_dispose_mapping:
	irq_dispose_mapping(virq);

	return -ENOMEM;
}

int copro_unit_probe_imqs(struct platform_device *unit_pdev)
{
	struct device_node *parent, *child;
	int found = 0;

	parent = of_node_get(unit_pdev->dev.of_node);

	for_each_child_of_node(parent, child) {
		if (!of_device_is_compatible(child, COPRO_IMQ_COMPAT))
			continue;

		if (copro_imq_probe(child, unit_pdev) == 0)
			found++;
	}

	return found;
}
