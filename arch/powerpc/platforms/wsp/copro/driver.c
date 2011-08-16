/*
 * Copyright 2009-2011 Michael Ellerman, IBM Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/anon_inodes.h>
#include <linux/of.h>
#include <linux/cdev.h>
#include <linux/compat.h>
#include <linux/cpumask.h>
#include <linux/poll.h>
#include <linux/stringify.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include <asm/copro-driver.h>

#include <mm/icswx.h>

#include "cop.h"
#include "pbic.h"
#include "imq.h"

#define COP_CLASS_NAME	"wsp_copro_type"
#define COP_DEV_PREFIX	"cop_type!"
#define COP_MAX_DEVS	64

static int cop_major;

#define COP_NAME_SIZE	64

struct cop_device {
	struct cdev cdev;
	struct device *device;
	struct copro_type *copro_type;
	dev_t dev;
	char name[COP_NAME_SIZE];
};

struct cop_imq_ctx {
	struct copro_imq_entry *buffer;
	int total, avail, get;
	unsigned int enabled:1;
	unsigned int overflow:1;
};

/* List of ctx's which have IMQ state */
static LIST_HEAD(cop_imq_ctx_list);
/* Prevent concurrent add/delete from the imq ctx list */
static DEFINE_SPINLOCK(cop_imq_ctx_list_lock);

struct cop_device_ctx {
	struct mutex lock;
	struct copro_type *copro_type;
	struct task_struct *owner;
	struct copro_instance *bound_copro;
	/* Used for the get instances ioctl if the list is too large */
	struct copro_instance *next_instance;
	/* Used to store & track IMQEs buffered for the ctx */
	struct list_head imq_list;
	struct cop_imq_ctx *imq;
	wait_queue_head_t wait;
};

#define ctx_debug(_ctx, fmt, ...)			\
	cop_debug("ctx:%p " fmt, _ctx, ##__VA_ARGS__)


static struct copro_instance *find_ctx_copro(struct cop_device_ctx *ctx)
{
	struct copro_instance *copro;

	/*
	 * Check the bound copro. We could take the lock here, but it's
	 * safe not to - we don't provide any guarantee that things will
	 * make sense if ops are racing vs an unbind.
	 */
	copro = ctx->bound_copro;

	/* Find specified default if any */
	if (!copro && ctx->copro_type->default_map)
		copro = ctx->copro_type->default_map[smp_processor_id()];

	/* Just use any - works for single instance */
	if (!copro)
		copro = list_first_entry(&ctx->copro_type->instance_list,
					 struct copro_instance, type_list);

	return copro;
}

static struct pbic *find_pbic(struct cop_device_ctx *ctx)
{
	return find_ctx_copro(ctx)->unit->pbic;
}

static long cop_map_args_ioctl(struct cop_device_ctx *ctx, unsigned int cmd,
			       void __user *uptr)
{
	return pbic_map_args_ioctl(find_pbic(ctx), cmd, uptr);
}

static long cop_ioctl_get_instances(struct cop_device_ctx *ctx,
				    struct copro_instance_list __user *list)
{
	struct copro_instance *copro;
	struct list_head *head;
	int i;

	head = &ctx->copro_type->instance_list;

	if (ctx->next_instance)
		copro = ctx->next_instance;
	else
		copro = list_first_entry(head, struct copro_instance,
					 type_list);

	i = 0;
	list_for_each_entry_from(copro, head, type_list) {
		if (i == COPRO_INSTANCE_LIST_SIZE)
			break;

		if (put_user((u64)copro->instance, &list->instances[i++]))
			return -EFAULT;
	}

	if (i == COPRO_INSTANCE_LIST_SIZE) {
		/* We've filled up the list, save our progress */
		ctx->next_instance = copro;
	} else {
		/* We're finished */
		if (put_user((u64)0, &list->instances[i]))
			return -EFAULT;

		ctx->next_instance = NULL;
	}

	return 0;
}

static struct copro_instance *
find_copro_instance(struct copro_type *copro_type, uint64_t instance)
{
	struct copro_instance *copro;

	list_for_each_entry(copro, &copro_type->instance_list, type_list)
		if (copro->instance == instance)
			return copro;

	return NULL;
}

static long cop_ioctl_bind(struct cop_device_ctx *ctx, u64 __user *uptr)
{
	struct copro_instance *copro;
	u64 instance;

	if (get_user(instance, uptr))
		return -EFAULT;

	copro = find_copro_instance(ctx->copro_type, instance);
	if (!copro)
		return -EINVAL;

	mutex_lock(&ctx->lock);
	ctx->bound_copro = copro;
	mutex_unlock(&ctx->lock);

	return 0;
}

static inline int imq_buffer_uses_vmalloc(int nr_imqe)
{
	return (nr_imqe * sizeof(struct copro_imq_entry)) > PAGE_SIZE;
}

static long cop_ioctl_alloc_imq(struct cop_device_ctx *ctx, u64 __user *uptr)
{
	struct cop_imq_ctx *imq;
	int qsize, rc;
	u64 nr_imqe;

	if (get_user(nr_imqe, uptr))
		return -EFAULT;

	/* Max size is 64K entries, just like the HW */
	if (nr_imqe > 0x10000 || nr_imqe == 0)
		return -EINVAL;

	mutex_lock(&ctx->lock);

	/* If we're bound, then that copro's unit must have IMQs */
	if (ctx->bound_copro && ctx->bound_copro->unit->nr_imqs == 0) {
		rc = -ENOENT;
		goto out;
	}

	if (ctx->imq) {
		rc = -EEXIST;
		goto out;
	}

	rc = -ENOMEM;
	imq = kzalloc(sizeof(*imq), GFP_KERNEL);
	if (!imq)
		goto out;

	qsize = sizeof(struct copro_imq_entry) * nr_imqe;

	if (imq_buffer_uses_vmalloc(nr_imqe))
		imq->buffer = vmalloc(qsize);
	else
		imq->buffer = kzalloc(qsize, GFP_KERNEL);

	if (!imq->buffer) {
		kfree(imq);
		goto out;
	}

	ctx_debug(ctx, "alloc imq %p buf %p nr %llu\n", imq,
		  imq->buffer, nr_imqe);

	imq->total = nr_imqe;
	imq->get = 0;
	imq->avail = 0;
	imq->enabled = 1;

	ctx->imq = imq;

	spin_lock(&cop_imq_ctx_list_lock);
	list_add_tail_rcu(&ctx->imq_list, &cop_imq_ctx_list);
	spin_unlock(&cop_imq_ctx_list_lock);

	rc = 0;
out:
	mutex_unlock(&ctx->lock);

	return rc;
}

static long cop_ioctl_toggle_imq(struct cop_device_ctx *ctx, int enable)
{
	int rc;

	mutex_lock(&ctx->lock);

	if (ctx->imq) {
		ctx->imq->enabled = enable;
		rc = 0;
	} else
		rc = -ENOENT;

	mutex_unlock(&ctx->lock);

	return rc;
}

static void cop_free_imq(struct cop_device_ctx *ctx)
{
	struct cop_imq_ctx *imq;

	mutex_lock(&ctx->lock);

	if (!ctx->imq) {
		mutex_unlock(&ctx->lock);
		return;
	}

	ctx_debug(ctx, "free imq %p buf %p\n", ctx->imq, ctx->imq->buffer);

	spin_lock(&cop_imq_ctx_list_lock);
	list_del_rcu(&ctx->imq_list);
	spin_unlock(&cop_imq_ctx_list_lock);

	imq = ctx->imq;
	ctx->imq = NULL;

	/* After we drop the mutex a list traverser might still find our ctx
	 * on the cop_imq_ctx_list, because we haven't waited for an RCU
	 * grace period, but they will see ctx->imq = NULL and continue.
	 */
	mutex_unlock(&ctx->lock);

	synchronize_rcu();

	if (imq_buffer_uses_vmalloc(imq->total))
		vfree(imq->buffer);
	else
		kfree(imq->buffer);

	kfree(imq);
}

static void cop_ioctl_free_imq(struct cop_device_ctx *ctx)
{
	cop_free_imq(ctx);

	/* Wake up anyone in read/poll, they will return seeing imq = NULL */
	wake_up_interruptible(&ctx->wait);
}

static long cop_ioctl_open_unit(struct cop_device_ctx *ctx)
{
	int rc;

	mutex_lock(&ctx->lock);

	/* Must be bound before opening the unit */
	if (!ctx->bound_copro) {
		rc = -EINVAL;
		goto out;
	}

	rc = anon_inode_getfd("copro-unit", ctx->bound_copro->unit->fops,
			      ctx->bound_copro->unit, O_RDWR);

out:
	mutex_unlock(&ctx->lock);

	return rc;
}

static long cop_ioctl_get_compatible(struct cop_device_ctx *ctx,
				     struct copro_compat_info __user *info)
{
	struct copro_instance *copro;
	const char *src;
	int plen, len;

	copro = list_first_entry(&ctx->copro_type->instance_list,
				 struct copro_instance, type_list);

	src = of_get_property(copro->dn, "compatible", &plen);
	BUG_ON(!src);

	len = plen;
	if (src[plen - 1] != '\0')
		len++;

	if (len > COPRO_COMPAT_BUF_SIZE) {
		pr_err("%s: compatible value too long!\n", __func__);
		return -ENOSPC;
	}

	if (copy_to_user(info->buf, src, plen))
		return -EFAULT;

	if (len > plen && put_user('\0', info->buf + len - 1))
		return -EFAULT;

	if (put_user((u32)len, &info->len))
		return -EFAULT;

	return 0;
}

static long cop_ioctl_enable_mtrace(struct cop_device_ctx *ctx,
				    u64 __user *uptr)
{
	u64 flags;
	int rc;

	if (get_user(flags, uptr))
		return -EFAULT;

	if (flags & ~COPRO_MTRACE_MASK)
		return -EINVAL;

	mutex_lock(&ctx->lock);

	if (!ctx->bound_copro) {
		rc = -EINVAL;
		goto out;
	}

	pbic_configure_marker_trace(ctx->bound_copro->unit->pbic,
				    ctx->bound_copro, 1, flags);
	rc = 0;
out:
	mutex_unlock(&ctx->lock);

	return rc;
}

static long cop_ioctl_disable_mtrace(struct cop_device_ctx *ctx)
{
	int rc;

	mutex_lock(&ctx->lock);

	if (!ctx->bound_copro) {
		rc = -EINVAL;
		goto out;
	}

	pbic_configure_marker_trace(ctx->bound_copro->unit->pbic,
				    ctx->bound_copro, 0, 0);
	rc = 0;
out:
	mutex_unlock(&ctx->lock);

	return rc;
}

static long cop_ioctl_get_affinity(struct cop_device_ctx *ctx,
				   struct copro_affinity_args __user *uargs,
				   bool is_compat)
{
	struct copro_affinity_args args;
	struct copro_instance *copro;
	int nid, rc, min_length;
	struct cpumask *mask;
	void __user *dst;

	if (copy_from_user(&args, uargs, sizeof(args))) {
		ctx_debug(ctx, "error copying in args\n");
		return -EFAULT;
	}

	min_length = cpumask_size();

	/*
	 * If we can fit the whole mask in a compat long, then use that
	 * even though it's smaller than the kernel size. Intended to
	 * match the code in compat_sys_sched_getaffinity().
	 */
	if (is_compat && nr_cpu_ids <= BITS_PER_COMPAT_LONG)
		min_length = sizeof(compat_ulong_t);

	if (args.len < min_length) {
		ctx_debug(ctx, "user cpumask size (%d) < kernel (%d)\n",
			  args.len, min_length);
		return -EINVAL;
	}

	if (args.instance == COPRO_INSTANCE_INVALID)
		copro = find_ctx_copro(ctx);
	else
		copro = find_copro_instance(ctx->copro_type, args.instance);

	if (!copro) {
		ctx_debug(ctx, "no copro found\n");
		return -EINVAL;
	}

	nid = of_node_to_nid(copro->dn);
	if (nid == -1) {
		ctx_debug(ctx, "no nid found\n");
		return -ENODATA;
	}

	ctx_debug(ctx, "on node %d\n", nid);

	mask = (struct cpumask *)cpumask_of_node(nid);
	dst = (void __user *)args.addr;

	if (is_compat)
		rc = compat_put_bitmap(dst, cpumask_bits(mask), min_length * 8);
	else
		rc = copy_to_user(dst, mask, min_length);

	if (rc) {
		ctx_debug(ctx, "error copying out mask\n");
		return -EFAULT;
	}

	return min_length;
}

#ifdef CONFIG_WORKAROUND_PBIC_DUP_ENTRIES
static long cop_ioctl_invalidate(struct cop_device_ctx *ctx, u64 __user *uptr)
{
	u64 addr;

	if (get_user(addr, uptr))
		return -EFAULT;

	return pbic_user_flush(find_pbic(ctx), addr);
}
#endif

static long cop_ioctl_common(struct file *f, unsigned int cmd,
			     unsigned long arg, bool is_compat)
{
	struct cop_device_ctx *ctx = f->private_data;
	void __user *uptr = (void __user *)arg;

	/* Make sure we are in the same thread group */
	if (!same_thread_group(ctx->owner, current)) {
		ctx_debug(ctx, "in thread group %u is not in same group "
			  "as current %u\n",
			  task_tgid_nr(ctx->owner), task_tgid_nr(current));
		return -EPERM;
	}

	switch (cmd) {
	case COPRO_IOCTL_GET_API_VERSION:
	case COPRO_IOCTL_UNBIND:
	case COPRO_IOCTL_ENABLE_IMQ:
	case COPRO_IOCTL_DISABLE_IMQ:
	case COPRO_IOCTL_FREE_IMQ:
	case COPRO_IOCTL_DISABLE_MTRACE:
		if (arg)
			return -EINVAL;
		break;
	}

	switch (cmd) {
	case COPRO_IOCTL_GET_API_VERSION:
		return COPRO_API_VERSION;

	case COPRO_IOCTL_MAP:
	case COPRO_IOCTL_UNMAP:
		return cop_map_args_ioctl(ctx, cmd, uptr);

	case COPRO_IOCTL_GET_TYPE: {
		u64 type = ctx->copro_type->type;
		if (put_user(type, (u64 __user *)uptr))
			return -EFAULT;
		return 0;
	}

	case COPRO_IOCTL_GET_INSTANCES:
		return cop_ioctl_get_instances(ctx, uptr);

	case COPRO_IOCTL_BIND:
		return cop_ioctl_bind(ctx, uptr);

	case COPRO_IOCTL_UNBIND:
		mutex_lock(&ctx->lock);
		ctx->bound_copro = NULL;
		mutex_unlock(&ctx->lock);
		return 0;

	case COPRO_IOCTL_GET_COMPATIBLE:
		return cop_ioctl_get_compatible(ctx, uptr);

	case COPRO_IOCTL_ALLOC_IMQ:
		return cop_ioctl_alloc_imq(ctx, uptr);

	case COPRO_IOCTL_ENABLE_IMQ:
		return cop_ioctl_toggle_imq(ctx, 1);

	case COPRO_IOCTL_DISABLE_IMQ:
		return cop_ioctl_toggle_imq(ctx, 0);

	case COPRO_IOCTL_FREE_IMQ:
		cop_ioctl_free_imq(ctx);
		return 0;

	case COPRO_IOCTL_ENABLE_MTRACE:
		return cop_ioctl_enable_mtrace(ctx, uptr);

	case COPRO_IOCTL_DISABLE_MTRACE:
		return cop_ioctl_disable_mtrace(ctx);

	case COPRO_IOCTL_GET_AFFINITY:
		return cop_ioctl_get_affinity(ctx, uptr, is_compat);

	case COPRO_IOCTL_GET_PBIC: {
		u64 instance = find_pbic(ctx)->instance;
		if (put_user(instance, (u64 __user *)uptr))
			return -EFAULT;
		return 0;
	}
#ifdef CONFIG_WORKAROUND_PBIC_DUP_ENTRIES
	case COPRO_IOCTL_INVALIDATE:
		return cop_ioctl_invalidate(ctx, uptr);
#endif
	case COPRO_IOCTL_OPEN_UNIT:
		return cop_ioctl_open_unit(ctx);
	}

	ctx_debug(ctx, "unknown ioctl %#x\n", cmd);

	return -EINVAL;
}

static long cop_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
	return cop_ioctl_common(f, cmd, arg, false);
}

static long compat_cop_ioctl(struct file *f,
			     unsigned int cmd, unsigned long arg)
{
	return cop_ioctl_common(f, cmd, arg, true);
}

static inline int imqe_ctr_add(struct cop_imq_ctx *imq, int ctr, int inc)
{
	return (ctr + inc) & (imq->total - 1);
}

static int copy_imqes_to_user(struct cop_device_ctx *ctx,
			      char __user *buf, size_t count)
{
	int rc, nr_imqes, imqe_avail, imqe_wanted, size;
	char __user *p = buf;

	imqe_wanted = count / sizeof(struct copro_imq_entry);

	while (ctx->imq->avail && imqe_wanted) {
		if (ctx->imq->get + ctx->imq->avail > ctx->imq->total) {
			/* Wrapped case, copy to end of buffer first */
			imqe_avail = ctx->imq->total - ctx->imq->get;
			ctx_debug(ctx, "only %d imqes to buf end\n",
				  imqe_avail);
		} else
			imqe_avail = ctx->imq->avail;

		nr_imqes = min(imqe_avail, imqe_wanted);
		size = nr_imqes * sizeof(struct copro_imq_entry);

		ctx_debug(ctx, "wanted %d avail %d nr %d size %d count %ld\n",
			  imqe_wanted, imqe_avail, nr_imqes, size, count);

		ctx_debug(ctx, "dest %p src %p size %d\n", p,
			  ctx->imq->buffer + ctx->imq->get, size);

		rc = 0;
		if (copy_to_user(p, ctx->imq->buffer + ctx->imq->get, size)) {
			rc = -EFAULT;
			goto out;
		}

		ctx->imq->get = imqe_ctr_add(ctx->imq, ctx->imq->get, nr_imqes);
		ctx->imq->avail -= nr_imqes;
		count -= size;
		imqe_wanted = count / sizeof(struct copro_imq_entry);
		p += size;

		ctx_debug(ctx, "wanted %d get %d avail %d\n", imqe_wanted,
			  ctx->imq->get, ctx->imq->avail);
	}

	rc = p - buf;
out:
	return rc;
}

static ssize_t cop_read(struct file *f, char __user *buf,
			size_t count, loff_t *ppos)
{
	struct cop_device_ctx *ctx = f->private_data;
	DEFINE_WAIT(wait);
	int rc = 0;

	ctx_debug(ctx, "buf %p count %ld\n", buf, count);

	if (count < sizeof(struct copro_imq_entry))
		return -EINVAL;

	mutex_lock(&ctx->lock);

	prepare_to_wait(&ctx->wait, &wait, TASK_INTERRUPTIBLE);

	while (ctx->imq) {
		if (ctx->imq->avail) {
			rc = copy_imqes_to_user(ctx, buf, count);
			break;
		}

		if (f->f_flags & O_NONBLOCK) {
			rc = -EAGAIN;
			break;
		}

		if (signal_pending(current)) {
			rc = -ERESTARTSYS;
			break;
		}

		mutex_unlock(&ctx->lock);
		schedule();
		mutex_lock(&ctx->lock);
	}

	finish_wait(&ctx->wait, &wait);

	mutex_unlock(&ctx->lock);

	ctx_debug(ctx, "returning %d\n", rc);

	return rc;
}

static unsigned int cop_poll(struct file *f, poll_table *wait)
{
	struct cop_device_ctx *ctx = f->private_data;
	unsigned int mask = 0;

	poll_wait(f, &ctx->wait, wait);

	mutex_lock(&ctx->lock);

	if (ctx->imq) {
		if (ctx->imq->avail)
			mask |= POLLIN;

		if (ctx->imq->overflow) {
			mask |= POLLERR;
			ctx->imq->overflow = 0;
		}
	} else
		mask |= POLLHUP;

	mutex_unlock(&ctx->lock);

	return mask;
}

static int cop_open(struct inode *inode, struct file *f)
{
	struct cdev *cdev = inode->i_cdev;
	struct cop_device_ctx *ctx;
	struct cop_device *cop_dev;
	ulong acop;
	int rc;

	cop_dev = container_of(cdev, struct cop_device, cdev);

	acop = acop_copro_type_bit(cop_dev->copro_type->type);
	rc = use_cop(acop, current->mm);
	if (rc) {
		cop_debug("couldn't enable copro access mm %p\n", current->mm);
		return rc;
	}

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mutex_init(&ctx->lock);
	init_waitqueue_head(&ctx->wait);
	ctx->copro_type = cop_dev->copro_type;

	get_task_struct(current->group_leader);
	ctx->owner = current->group_leader;

	f->private_data = ctx;

	ctx_debug(ctx, "owned by task %p (mm %p)\n", ctx->owner,
		  ctx->owner->mm);

	return 0;
}

static int cop_release(struct inode *inode, struct file *f)
{
	struct cop_device_ctx *ctx = f->private_data;

	ctx_debug(ctx, "freed by task %p mm (%p)\n", ctx->owner,
		  ctx->owner->mm);

	cop_free_imq(ctx);

	put_task_struct(ctx->owner);
	kfree(ctx);

	return 0;
}

static const struct file_operations cop_fops = {
	.owner = THIS_MODULE,
	.open = cop_open,
	.read = cop_read,
	.poll = cop_poll,
	.release = cop_release,
	.unlocked_ioctl = cop_ioctl,
	.compat_ioctl = compat_cop_ioctl,
};

static void ctx_add_imqe(struct copro_imq *imq, struct copro_imq_entry *imqe,
			 struct cop_device_ctx *ctx)
{
	int put;

	mutex_lock(&ctx->lock);

	/* We might see a NULL imq due to RCU */
	if (!ctx->imq || !ctx->imq->enabled) {
		ctx_debug(ctx, "no IMQ or disabled\n");
		goto out;
	}

	if (ctx->bound_copro) {
		/* We must be bound to a copro below the IMQ unit */
		if (ctx->bound_copro->unit != imq->unit) {
			ctx_debug(ctx, "unit mismatch\n");
			goto out;
		}
	} else {
		/* Otherwise, the IMQ unit must have child of the right type */
		if (!(ctx->copro_type->type & imq->unit->types)) {
			ctx_debug(ctx, "wrong type\n");
			goto out;
		}
	}

	if (ctx->imq->avail == ctx->imq->total) {
		/* We've overflowed, set overflow and overwrite */
		ctx_debug(ctx, "avail = total, overwriting\n");
		ctx->imq->overflow = 1;
		put = ctx->imq->get;
		ctx->imq->get = imqe_ctr_add(ctx->imq, put, 1);
	} else {
		put = imqe_ctr_add(ctx->imq, ctx->imq->get, ctx->imq->avail);
		ctx->imq->avail++;
	}

	memcpy(ctx->imq->buffer + put, imqe, sizeof(*imqe));

	ctx_debug(ctx, "copied imqe to %p, avail %d\n",
		  ctx->imq->buffer + put, ctx->imq->avail);

	wake_up_interruptible(&ctx->wait);
out:
	mutex_unlock(&ctx->lock);
}

struct task_struct *
cop_driver_handle_imqe(struct copro_imq *imq, struct copro_imq_entry *imqe,
		       struct mm_struct *mm)
{
	struct task_struct *tsk = NULL;
	struct cop_device_ctx *ctx;

	/*
	 * We don't want to traverse the list twice for each IMQE, so this
	 * function does two things: 1) finds a ctx that matches the mm and
	 * returns the task_struct so the copy code can avoid a linear
	 * search of the process list. 2) finds all ctx's that actually want
	 * the IMQE in their buffer.
	 */

	rcu_read_lock();

	list_for_each_entry_rcu(ctx, &cop_imq_ctx_list, imq_list) {
		struct task_struct *owner;

		/* Owner never changes, so safe to check without locking */

		/*
		 * I'm not sure if the group leader will ever be
		 * "anonymous" and have a NULL mm.  If this is
		 * possible then we have to search for an active
		 * thread with an mm so we can match against it.
		 */
		owner = ctx->owner;
		tsk = owner;
		do {
			if (tsk->mm)
				break;
		} while_each_thread(owner, tsk);

		if (tsk->mm == NULL)
			continue;

		if (tsk->mm != mm)
			continue;

		/* we always get owner, because we always put owner */
		get_task_struct(owner);

		ctx_add_imqe(imq, imqe, ctx);
	}

	rcu_read_unlock();

	return tsk;
}

static struct class *cop_class;

static ssize_t show_type(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct cop_device *c = dev_get_drvdata(dev);
	return snprintf(buf, PAGE_SIZE, "0x%llx\n", (u64)c->copro_type->type);
}

static ssize_t show_instances(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct cop_device *c = dev_get_drvdata(dev);
	struct copro_instance *copro;
	size_t total, size, rc;

	total = PAGE_SIZE;
	size = 0;

	list_for_each_entry(copro, &c->copro_type->instance_list, type_list) {
		rc = snprintf(buf, total, "0x%llx\n", (u64)copro->instance);
		if (rc > total)
			break;

		total -= rc; size += rc; buf += rc;
	}

	return size;
}

static struct device_attribute cop_dev_attrs[] = {
	__ATTR(type, S_IRUGO, show_type, NULL),
	__ATTR(instances, S_IRUGO, show_instances, NULL),
	__ATTR_NULL
};

static int cop_create_device(struct copro_type *copro_type,
			     int major, int minor)
{
	struct cop_device *c;
	int rc;

	c = kzalloc(sizeof(struct cop_device), GFP_KERNEL);
	if (!c) {
		pr_err("%s: kzalloc failed\n", __func__);
		return -ENOMEM;
	}

	c->dev = MKDEV(major, minor);
	c->copro_type = copro_type;
	rc = snprintf(c->name, COP_NAME_SIZE, "%s%s", COP_DEV_PREFIX,
		      copro_type->name);
	if (rc >= COP_NAME_SIZE) {
		pr_err("%s: ran out of space for name for '%s'\n", __func__,
		       copro_type->name);
		goto out_free;
	}

	cdev_init(&c->cdev, &cop_fops);

	rc = cdev_add(&c->cdev, c->dev, 1);
	if (rc) {
		pr_err("%s: cdev_add() failed\n", __func__);
		goto out_free;
	}

	c->device = device_create(cop_class, NULL, c->dev, c, c->name);
	if (!c->device) {
		pr_err("%s: device_create failed for %s\n", __func__, c->name);
		rc = -ENODEV;
		goto out_del;
	}

	pr_debug("copro: created device for type 0x%x, name %s\n",
		 copro_type->type, c->name);

	return 0;

out_del:
	cdev_del(&c->cdev);
out_free:
	kfree(c);
	return rc;
}

int __init copro_driver_init(void)
{
	struct copro_type *copro_type;
	int minor, rc, status;
	dev_t cop_dev;

	rc = alloc_chrdev_region(&cop_dev, 0, COP_MAX_DEVS, COP_CLASS_NAME);
	if (rc < 0) {
		pr_err("%s: alloc_chrdev_region() failed!\n", __func__);
		return rc;
	}

	cop_major = MAJOR(cop_dev);

	cop_class = class_create(THIS_MODULE, COP_CLASS_NAME);
	if (IS_ERR(cop_class)) {
		pr_err("%s: class_create failed!\n", __func__);
		return -ENOMEM;
	}
	cop_class->dev_attrs = cop_dev_attrs;

	status = 0;
	rc = 0;
	minor = 0;
	list_for_each_entry(copro_type, &copro_type_list, list) {
		rc = cop_create_device(copro_type, cop_major, minor++);
		if (rc)
			status = rc;
	}

	return status;
}
