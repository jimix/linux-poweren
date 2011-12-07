/*
 * Copyright (C) 2011, 2012 IBM Corporation.
 * Author:  Davide Pasetto <pasetto_davide@ie.ibm.com>
 *      Karol Lynch <karol_lynch@ie.ibm.com>
 *      Kay Muller <kay.muller@ie.ibm.com>
 *      John Sheehan <john.d.sheehan@ie.ibm.com>
 *      Jimi Xenidis <jimix@watson.ibm.com>
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


/*
 * khea-netdev.c --  HEA kernel network interface
 *
 */

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/module.h>
#include <linux/virtio.h>
#include <linux/virtio_net.h>
#include <linux/scatterlist.h>
#include <linux/if_vlan.h>
#include <linux/types.h>
#include <linux/if_ether.h>
#include <linux/inet_lro.h>

#include <net/ip.h>
#include <net/udp.h>
#include <net/tcp.h>

#include <khea.h>

enum khea_sysfs_attr {
	kattr_rhea_adapter,
	kattr_rhea_pport,
	kattr_rhea_lport,
	kattr_send_q_len,
	kattr_send_q_reclaim,
	kattr_send_wqe_type,
	kattr_recv_q_num,
	kattr_recv_rq1_size,
	kattr_recv_rq2_size,
	kattr_recv_rq1_len,
	kattr_recv_rq2_len,
	kattr_recv_rq1_low,
	kattr_recv_rq2_low,
	kattr_send_ll_limit
};

struct kattr {
	struct attribute attr;
	enum khea_sysfs_attr type;
};

#define to_khea_attr(_attr) container_of(_attr, struct kattr, attr)

#define KHEA_ATTR(_name, _mode, _type) \
	struct kattr khea_attr_##_name = {		\
		.attr.name  = __stringify(_name),	\
		.attr.mode  = _mode,			\
		.type       = _type			\
	}

int khea_fixparams(struct khea_private *kp)
{
	/* return 1 if we must reconfigure interface */
	return 0;
}

static void kobject_release(struct kobject *kobj)
{
	khea_error("debug kobject_release");
	kfree(kobj);
}

static ssize_t khea_attr_show(struct kobject *kobj, struct attribute *attr,
			      char *buf)
{
	struct kattr *kattr = to_khea_attr(attr);
	struct khea_kobj *kko = container_of(kobj, struct khea_kobj, kobj);
	struct khea_private *kp = kko->kp;

	int q;
	ssize_t sum;

	khea_debug(" ");

	if (!kp || !capable(CAP_NET_ADMIN))
		return -EPERM;

	switch (kattr->type) {
	case kattr_rhea_adapter:
		return sprintf(buf, "%d\n", kp->rhea_adapter);
	case kattr_rhea_pport:
		return sprintf(buf, "%d\n", kp->rhea_pport);
	case kattr_rhea_lport:
		return sprintf(buf, "%d\n", kp->rhea_lport);
	case kattr_send_q_len:
		sum = 0;
		for (q = 0; q < kp->num_qp; ++q)
			sum += sprintf(buf, "%d\n",
				kp->qp_vec[q].send_q_len);
		return sum;
	case kattr_send_q_reclaim:
		sum = 0;
		for (q = 0; q < kp->num_qp; ++q)
			sum += sprintf(buf, "%d\n",
				kp->qp_vec[q].send_q_reclaim);
		return sum;
	case kattr_send_wqe_type:
		sum = 0;
		for (q = 0; q < kp->num_qp; ++q)
			sum += sprintf(buf, "%d\n",
				kp->qp_vec[q].send_wqe_type);
		return sum;
	case kattr_recv_q_num:
		return sprintf(buf, "%d\n", kp->recv_q_num);
	case kattr_recv_rq1_size:
		sum = 0;
		for (q = 0; q < kp->num_qp; ++q)
			sum += sprintf(buf, "%d\n",
				kp->qp_vec[q].recv_rq_size[0]);
		return sum;
	case kattr_recv_rq2_size:
		sum = 0;
		for (q = 0; q < kp->num_qp; ++q)
			sum += sprintf(buf, "%d\n",
				kp->qp_vec[q].recv_rq_size[1]);
	case kattr_recv_rq1_len:
		sum = 0;
		for (q = 0; q < kp->num_qp; ++q)
			sum += sprintf(buf, "%d\n",
			 kp->qp_vec[q].recv_rq_len[0]);
		return sum;
	case kattr_recv_rq2_len:
		sum = 0;
		for (q = 0; q < kp->num_qp; ++q)
			sum += sprintf(buf, "%d\n",
				kp->qp_vec[q].recv_rq_len[1]);
		return sum;
	case kattr_recv_rq1_low:
		sum = 0;
		for (q = 0; q < kp->num_qp; ++q)
			sum += sprintf(buf, "%d\n",
				kp->qp_vec[q].recv_rq_low[0]);
		return sum;
	case kattr_recv_rq2_low:
		sum = 0;
		for (q = 0; q < kp->num_qp; ++q)
			sum += sprintf(buf, "%d\n",
				kp->qp_vec[q].recv_rq_low[1]);
		return sum;
	case kattr_send_ll_limit:
		return sprintf(buf, "%d\n", kp->send_ll_limit);
	}

	return -EEXIST;
}

static ssize_t khea_attr_store(struct kobject *kobj, struct attribute *attr,
			       const char *buf, size_t count)
{
	struct kattr *kattr = to_khea_attr(attr);
	struct khea_kobj *kko = container_of(kobj, struct khea_kobj, kobj);
	struct khea_private *kp = kko->kp;
	unsigned int v;
	int do_int_reconfig = 0, o;

	khea_debug(" ");

	if (!kp || !capable(CAP_NET_ADMIN))
		return -EPERM;

	if (kstrtouint(buf, 0, &v))
		return -EINVAL;

	switch (kattr->type) {
	case kattr_rhea_adapter:
		return -EPERM;
	case kattr_rhea_pport:
		return -EPERM;
	case kattr_rhea_lport:
		return -EPERM;
	case kattr_send_q_len:
		o = kp->qp_vec[0].send_q_len;
		kp->qp_vec[0].send_q_len = v;
		if (khea_fixparams(kp) || o != kp->qp_vec[0].send_q_len)
			do_int_reconfig = 1;
		break;
	case kattr_send_q_reclaim:
		kp->qp_vec[0].send_q_reclaim = v;
		if (khea_fixparams(kp))
			do_int_reconfig = 1;
		break;
	case kattr_send_wqe_type:
		o = kp->qp_vec[0].send_wqe_type;
		kp->qp_vec[0].send_wqe_type = v;
		if (khea_fixparams(kp) || o != kp->qp_vec[0].send_wqe_type)
			do_int_reconfig = 1;
		break;
	case kattr_recv_q_num:
		o = kp->recv_q_num;
		kp->recv_q_num = v;
		if (khea_fixparams(kp) || o != kp->recv_q_num)
			do_int_reconfig = 1;
		break;
	case kattr_recv_rq1_size:
		o = kp->qp_vec[0].recv_rq_size[0];
		kp->qp_vec[0].recv_rq_size[0] = v;
		if (khea_fixparams(kp) || o != kp->qp_vec[0].recv_rq_size[0])
			do_int_reconfig = 1;
		break;
	case kattr_recv_rq2_size:
		o = kp->qp_vec[0].recv_rq_size[1];
		kp->qp_vec[0].recv_rq_size[1] = v;
		if (khea_fixparams(kp) || o != kp->qp_vec[0].recv_rq_size[1])
			do_int_reconfig = 1;
		break;
	case kattr_recv_rq1_len:
		o = kp->qp_vec[0].recv_rq_len[0];
		kp->qp_vec[0].recv_rq_len[0] = v;
		if (khea_fixparams(kp) || o != kp->qp_vec[0].recv_rq_len[0])
			do_int_reconfig = 1;
		break;
	case kattr_recv_rq2_len:
		o = kp->qp_vec[0].recv_rq_len[1];
		kp->qp_vec[0].recv_rq_len[1] = v;
		if (khea_fixparams(kp) || o != kp->qp_vec[0].recv_rq_len[1])
			do_int_reconfig = 1;
		break;
	case kattr_recv_rq1_low:
		kp->qp_vec[0].recv_rq_low[0] = v;
		if (khea_fixparams(kp))
			do_int_reconfig = 1;
		break;
	case kattr_recv_rq2_low:
		kp->qp_vec[0].recv_rq_low[1] = v;
		if (khea_fixparams(kp))
			do_int_reconfig = 1;
		break;
	case kattr_send_ll_limit:
		kp->send_ll_limit = v;
		if (khea_fixparams(kp))
			do_int_reconfig = 1;
		break;
	}
	return 0;
}

static KHEA_ATTR(rhea_adapter, S_IRUGO | S_IWUSR, kattr_rhea_adapter);
static KHEA_ATTR(rhea_pport, S_IRUGO | S_IWUSR, kattr_rhea_pport);
static KHEA_ATTR(rhea_lport, S_IRUGO | S_IWUSR, kattr_rhea_lport);
static KHEA_ATTR(send_q_len, S_IRUGO | S_IWUSR, kattr_send_q_len);
static KHEA_ATTR(send_q_reclaim, S_IRUGO | S_IWUSR, kattr_send_q_reclaim);
static KHEA_ATTR(send_wqe_type, S_IRUGO | S_IWUSR, kattr_send_wqe_type);
static KHEA_ATTR(recv_q_num, S_IRUGO | S_IWUSR, kattr_recv_q_num);
static KHEA_ATTR(recv_rq1_size, S_IRUGO | S_IWUSR, kattr_recv_rq1_size);
static KHEA_ATTR(recv_rq2_size, S_IRUGO | S_IWUSR, kattr_recv_rq2_size);
static KHEA_ATTR(recv_rq1_len, S_IRUGO | S_IWUSR, kattr_recv_rq1_len);
static KHEA_ATTR(recv_rq2_len, S_IRUGO | S_IWUSR, kattr_recv_rq2_len);
static KHEA_ATTR(recv_rq1_low, S_IRUGO | S_IWUSR, kattr_recv_rq1_low);
static KHEA_ATTR(recv_rq2_low, S_IRUGO | S_IWUSR, kattr_recv_rq2_low);
static KHEA_ATTR(send_ll_limit, S_IRUGO | S_IWUSR, kattr_send_ll_limit);

static struct attribute *khea_params_attrs[] = {
	&khea_attr_rhea_adapter.attr,
	&khea_attr_rhea_pport.attr,
	&khea_attr_rhea_lport.attr,
	&khea_attr_send_q_len.attr,
	&khea_attr_send_q_reclaim.attr,
	&khea_attr_send_wqe_type.attr,
	&khea_attr_recv_q_num.attr,
	&khea_attr_recv_rq1_size.attr,
	&khea_attr_recv_rq2_size.attr,
	&khea_attr_recv_rq1_len.attr,
	&khea_attr_recv_rq2_len.attr,
	&khea_attr_recv_rq1_low.attr,
	&khea_attr_recv_rq2_low.attr,
	&khea_attr_send_ll_limit.attr,
	NULL,
	NULL
};

static struct attribute *khea_stats_attrs[] = {
	NULL,
	NULL
};

static const struct sysfs_ops khea_ops = {
	.show = khea_attr_show,
	.store = khea_attr_store
};

void khea_sysfs_init_params(struct kobj_type *ktype)
{
	khea_debug(" ");
	ktype->sysfs_ops = &khea_ops;
	ktype->default_attrs = khea_params_attrs;
	ktype->release = kobject_release;
}

void khea_sysfs_init_stats(struct kobj_type *ktype)
{
	khea_debug(" ");
	ktype->sysfs_ops = &khea_ops;
	ktype->default_attrs = khea_stats_attrs;
	ktype->release = kobject_release;
}
