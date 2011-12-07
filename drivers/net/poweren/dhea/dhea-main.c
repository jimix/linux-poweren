#include <asm/poweren_hea_common_types.h>
#include <asm/poweren_hea_dhea_interface.h>

#include "../include/rhea-interface.h"
#include "../include/hea-queue.h"
#include "dhea.h"


static bool a_parameter;
module_param(a_parameter, bool, S_IRUGO);

irqreturn_t dhea_irq_handler(int irq, void *eqr)
{
	dhea_info("IRQ Handler started for IRQ: %i", irq);
	dhea_scan_event_queue((struct eq_res *)eqr);
	return IRQ_HANDLED;
}

static int dhea_pport_link_state_callback(__u32 port_nr, __u32 link_state,
					 void *args)
{

	struct channel_res *channel_info = (struct channel_res *) args;
	unsigned current_state = link_state;

	if (current_state != channel_info->last_reported_state) {
		channel_info->last_reported_state = current_state;
		if (channel_info->owner) {
			struct task_struct *owning_task =
			get_pid_task(channel_info->owner, PIDTYPE_PID);
			if (!owning_task) {
				dhea_warning("Warning: dhea does not know "
				"which process owns this channel 1.\n");
			} else {
				dhea_info("dhea is sending a signal "
				"to process %u.\n",
				owning_task->tgid);
				send_sig_info(SIGUSR1, SEND_SIG_FORCED,
				owning_task);
				put_task_struct(owning_task);
			}
		} else {
			dhea_warning("Warning: dhea does not know which "
			"process owns this channel 2.\n");
		}
	} else {
		dhea_info("Link state did not change so no warning sent.\n");
	}

	/* tell kernel whether link is up or down */
	if (link_state)
		dhea_info("pport %u link is up", port_nr + 1);
	else
		dhea_info("pport %u link is down", port_nr + 1);

	return 0;
}

static int dhea_open(struct inode *inode, struct file *file)
{
	struct dhea_user *duser;

	dhea_debug("dhea: Entering dhea_open.\n");

	duser = kzalloc(sizeof(*duser), GFP_KERNEL);
	if (!duser) {
		dhea_debug("dhea: problem allocating memory.\n");
		dhea_debug("dhea: Leaving dhea_open.\n");
		return -ENOMEM;
	}

	/* Init adapter list for process */
	INIT_LIST_HEAD(&(duser->adapter_list_head));

	file->private_data = duser;

	dhea_debug("dhea: Leaving dhea_open.\n");
	return 0;
}

/* close function - called when the "file" is closed */
static int dhea_release(struct inode *inode, struct file *file)
{
	dhea_debug("dhea: Entering dhea_release.\n");

	if (file->private_data) {
		struct adapter_res *curr;
		struct dhea_user *duser = (struct dhea_user *)
		file->private_data;


		/* Close all open adapter sessions */
		struct list_head *position = NULL;
		list_for_each(position, &(duser->adapter_list_head))
		{
			curr = list_entry(position, struct adapter_res,
			my_list);
			dhea_info("Releasing resoureces associated with ID "
			"= %u.\n", curr->id);
			rhea_session_fini(curr->id);
		}

		kfree(file->private_data);
	}

	dhea_debug("dhea: Leaving dhea_release.\n");
	return 0;
}

/* read function called when the "file" is read */
static ssize_t dhea_read(struct file *file, char *buf, size_t count,
	loff_t *ppos)
{
	dhea_debug("");
	return 0;
}


static ssize_t dhea_write(struct file *file, const char *buf,
	size_t count, loff_t *ppos)
{
	dhea_debug("");
	return 0;
}


static long dhea_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct dhea_user *duser;
	long retval = -EINVAL;

	dhea_debug("");

	duser = (struct dhea_user *)file->private_data;
	if (!duser) {
		dhea_error("duser is NULL");
		return retval;
	}

	switch (cmd) {
	case IOCTL_DHEA_ADAPTER_COUNT:
		retval = dhea_adapter_count(duser, arg);
		break;

	case IOCTL_DHEA_ADAPTER_INIT:
		retval = dhea_adapter_init(duser, arg);
		break;

	case IOCTL_DHEA_ADAPTER_FINI:
		retval = dhea_adapter_fini(duser, arg);
		break;

	case IOCTL_DHEA_GET_VERSION:
		retval = dhea_get_version(duser, arg);
		break;

	case IOCTL_DHEA_PPORT_COUNT:
		retval = dhea_pport_count(duser, arg);
		break;

	case IOCTL_DHEA_CHANNEL_ALLOC:
		retval = dhea_channel_alloc(duser, arg);
		break;

	case IOCTL_DHEA_CHANNEL_FREE:
		retval = dhea_channel_free(duser, arg);
		break;

	case IOCTL_DHEA_EQ_ALLOC:
		retval = dhea_eq_alloc(duser, arg);
		break;

	case IOCTL_DHEA_EQ_FREE:
		retval = dhea_eq_free(duser, arg);
		break;

	case IOCTL_DHEA_CQ_ALLOC:
		retval = dhea_cq_alloc(duser, arg);
		break;

	case IOCTL_DHEA_CQ_FREE:
		retval = dhea_cq_free(duser, arg);
		break;

	case IOCTL_DHEA_QP_ALLOC:
		retval = dhea_qp_alloc(duser, arg);
		break;

	case IOCTL_DHEA_QP_FREE:
		retval = dhea_qp_free(duser, arg);
		break;

	case IOCTL_DHEA_QPN_ARRAY_ALLOC:
		retval = dhea_qpn_array_alloc(duser, arg);
		break;

	case IOCTL_DHEA_QPN_ARRAY_FREE:
		retval = dhea_qpn_array_free(duser, arg);
		break;

	case IOCTL_DHEA_WIRE_QPN_TO_QP:
		retval = dhea_wire_qpn_to_qp(duser, arg);
		break;

	case IOCTL_DHEA_GET_DEFAULT_MAC:
		retval = dhea_get_default_mac(duser, arg);
		break;

	case IOCTL_DHEA_QP_UP:
		retval = dhea_qp_up(duser, arg);
		break;

	case IOCTL_DHEA_QP_DOWN:
		retval = dhea_qp_down(duser, arg);
		break;

	case IOCTL_DHEA_CHANNEL_UP:
		retval = dhea_channel_up(duser, arg);
		break;

	case IOCTL_DHEA_CHANNEL_DOWN:
		retval = dhea_channel_down(duser, arg);
		break;

	case IOCTL_DHEA_TCAM_SLOT_ALLOC:
		retval = dhea_tcam_slot_alloc(duser, arg);
		break;

	case IOCTL_DHEA_TCAM_SLOT_FREE:
		retval = dhea_tcam_slot_free(duser, arg);
		break;

	case IOCTL_DHEA_TCAM_SET:
		retval = dhea_tcam_set(duser, arg);
		break;

	case IOCTL_DHEA_TCAM_GET:
		retval = dhea_tcam_get(duser, arg);
		break;

	case IOCTL_DHEA_TCAM_ENABLE:
		retval = dhea_tcam_enable(duser, arg);
		break;

	case IOCTL_DHEA_TCAM_DISABLE:
		retval = dhea_tcam_disable(duser, arg);
		break;

	case IOCTL_DHEA_MAC_LOOPBACK_ENABLE:
		retval = dhea_mac_loopback_enable(duser, arg);
		break;

	case IOCTL_DHEA_MAC_LOOPBACK_DISABLE:
		retval = dhea_mac_loopback_disable(duser, arg);
		break;

	case IOCTL_DHEA_DEFAULT_PACKETS_REGISTER:
		retval = dhea_register_default_packets(duser, arg);
		break;

	case IOCTL_DHEA_DEFAULT_PACKETS_DEREGISTER:
		retval = dhea_deregister_default_packets(duser, arg);
		break;

	case IOCTL_DHEA_CHANNEL_FEATURE_GET:
		retval = dhea_channel_feature_get(duser, arg);
		break;

	case IOCTL_DHEA_CHANNEL_FEATURE_SET:
		retval = dhea_channel_feature_set(duser, arg);
		break;

	default:
		dhea_error("Unrecogized IOCTL");
		break;
	}
	return retval;
}

static int dhea_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct dhea_user *duser = NULL;
	int retval = -EINVAL;
	unsigned long pfn = 0;
	long length = 0;
	struct object_to_mmap *obj_to_map = NULL;

	dhea_debug("Entering dhea_mmap");

	duser = (struct dhea_user *)file->private_data;
	if (!duser) {
		dhea_error("duser is NULL.\n");
		return retval;
	}
	obj_to_map = &(duser->ob_to_mmap);

	length = vma->vm_end - vma->vm_start;

	if ((obj_to_map->index == 1) && !(obj_to_map->error_status)) {
		if (obj_to_map->cq) {
			vma->vm_page_prot =
			pgprot_noncached_wc(vma->vm_page_prot);
			pfn = (unsigned long)
			(obj_to_map->cq->user_cq_registers) >> PAGE_SHIFT;
			dhea_debug("Mapping CQ regs.\n");
		} else if (obj_to_map->qp) {
			vma->vm_page_prot =
			pgprot_noncached_wc(vma->vm_page_prot);
			pfn = (unsigned long)
			(obj_to_map->qp->user_qp_registers) >> PAGE_SHIFT;
			dhea_debug("Mapping QP regs.\n");
		}

		/* Mapping HEA registers directly */
		if (io_remap_pfn_range(vma, vma->vm_start, pfn, length,
		vma->vm_page_prot)) {
			dhea_error("io_remap_pfn_range failed");
			dhea_debug("Leaving dhea_mmap");
			return -EAGAIN;
	}
	} else {
		if (obj_to_map->cq) {
			pfn = virt_to_phys((void *)
			((obj_to_map->cq)->begin_cqe)) >> PAGE_SHIFT;
			dhea_debug("Mapping CQ CQEs.\n");
		} else if (obj_to_map->qp) {
			switch (obj_to_map->index) {
			case 2:
				pfn = virt_to_phys((void *)(
				(obj_to_map->qp)->sq_begin))
				>> PAGE_SHIFT;
				dhea_debug("Mapping SQ WQEs.\n");
				break;

			case 3:
				pfn = virt_to_phys((void *)(
				(obj_to_map->qp)->rq_begin[0]))
				>> PAGE_SHIFT;
				dhea_debug("Mapping RQ 1 WQEs.\n");
				break;

			case 4:
				pfn = virt_to_phys((void *)(
				(obj_to_map->qp)->rq_begin[1]))
				>> PAGE_SHIFT;
				dhea_debug("Mapping RQ 2 WQEs.\n");
				break;

			case 5:
				pfn =
	virt_to_phys((void *)((obj_to_map->qp)->rq_begin[2])) >> PAGE_SHIFT;
				dhea_debug("Mapping RQ 3 WQEs.\n");
				break;

			default:
				dhea_error("Don't know what you "
				"want to mmap.\n");
			}
		} else if (obj_to_map->error_status) {
			switch (obj_to_map->index) {
			case 1:
				pfn = virt_to_phys((void *)(
		(obj_to_map->error_status)->qp_rx_error_status_page_aligned))
				>> PAGE_SHIFT;
				dhea_info("Mapping QP RX Error Status.\n");
				break;

			case 2:
				pfn = virt_to_phys((void *)(
		(obj_to_map->error_status)->qp_tx_error_status_page_aligned))
				>> PAGE_SHIFT;
				dhea_info("Mapping QP TX Error Status.\n");
				break;

			case 3:
				pfn = virt_to_phys((void *)(
		(obj_to_map->error_status)->cq_error_status_page_aligned))
				>> PAGE_SHIFT;
				dhea_info("Mapping CQ Error Status.\n");
				break;

			default:
				dhea_error("Don't know what you "
				"want to mmap.\n");
			}
		}

		/* Mapping RAM */
		if (remap_pfn_range(vma, vma->vm_start, pfn, length,
		vma->vm_page_prot)) {
			dhea_error("remap_pfn_range failed");
			dhea_debug("Leaving dhea_mmap");
			return -EAGAIN;
		}
	}

	(obj_to_map->index)++;

	dhea_debug("Leaving dhea_mmap");

	return 0;
}


/* define which file operations are supported */
const struct file_operations dhea_fops = {
	.owner = THIS_MODULE,
	.read = dhea_read,
	.write = dhea_write,
	.unlocked_ioctl = dhea_ioctl,
	.compat_ioctl = dhea_ioctl,
	.mmap = dhea_mmap,
	.open = dhea_open,
	.release = dhea_release,
};

/* initialize module */
static int __init dhea_init_module(void)
{
	int i;

	pr_info("dhea: loading dhea module:");

	/* Registering Device */
	i = register_chrdev(DHEA_MAJOR, DHEA_NAME, &dhea_fops);
	if (i != 0) {
		pr_info(" failure.\n");
		return -EIO;
	}

	pr_info(" success.\n");
	return 0;
}

/* close and cleanup module */
static void __exit dhea_cleanup_module(void)
{
	pr_info("dhea: unloading dhea module.\n");

	unregister_chrdev(DHEA_MAJOR, DHEA_NAME);
}

/*********** IOCTL handing functions ************/
int dhea_adapter_count(struct dhea_user *duser, unsigned long arg)
{
	unsigned int adapter_count = 0;
	struct dhea_adapter_count_parms *uparms =
	(struct dhea_adapter_count_parms *)arg;
	dhea_debug("Entering: IOCTL_DHEA_ADAPTER_COUNT");

	adapter_count = rhea_adapter_count();
	copy_to_user(&(uparms->adapter_count), &adapter_count, 4);

	dhea_debug("Leaving: IOCTL_DHEA_ADAPTER_COUNT");
	return 0;
}

int dhea_adapter_init(struct dhea_user *duser, unsigned long arg)
{
	int rtn_val = 0;
	unsigned int rhea_major = 0, rhea_minor = 0, rhea_release = 0;
	struct dhea_adapter_init_parms *uparms, kparms;
	unsigned rhea_id = 0;
	struct adapter_res *new_adapter = NULL;

	dhea_debug("Entering: IOCTL_DHEA_ADAPTER_INIT");

	if ((rhea_get_version(&rhea_major, &rhea_minor, &rhea_release)) < 0) {
		dhea_error("rhea_get_version returned with an error.\n");
		dhea_debug("Leaving: IOCTL_DHEA_ADAPTER_INIT.");
		return -1;
	}

	if (rhea_major != DHEA_MAJOR_VERSION) {
		dhea_error("Error: rhea and dhea are incompatible.\n");
		dhea_debug("Leaving: IOCTL_DHEA_ADAPTER_INIT.");
		return -2;
	}

	pr_info("dhea: RHEA Versions Major %u Minor %u Release %u.\n",
	rhea_major, rhea_minor, rhea_release);

	uparms = (struct dhea_adapter_init_parms *)arg;
	copy_from_user((void *)&kparms, (void *)uparms, sizeof(kparms));

	rtn_val = rhea_session_init(&rhea_id, kparms.adapter_number);

	if (rtn_val) {
		kparms.error_number = rtn_val;
		copy_to_user(uparms, &kparms, sizeof(kparms));
		dhea_error("rhea_session_init returned with error code %d.\n",
		rtn_val);
		dhea_debug("Leaving: IOCTL_DHEA_ADAPTER_INIT.");
		return -1;
	}

	new_adapter = kzalloc(sizeof(struct adapter_res), GFP_KERNEL);
	new_adapter->id = rhea_id;

	new_adapter->qp_rx_error_status = kzalloc(2*PAGE_SIZE, GFP_KERNEL);
	new_adapter->qp_tx_error_status = kzalloc(2*PAGE_SIZE, GFP_KERNEL);
	new_adapter->cq_error_status = kzalloc(2*PAGE_SIZE, GFP_KERNEL);
	if (!(new_adapter->qp_rx_error_status) ||
	!(new_adapter->qp_tx_error_status) ||
	!(new_adapter->cq_error_status)) {
		kparms.error_number = -1;
		copy_to_user(uparms, &kparms, sizeof(kparms));
		dhea_error("kmalloc returned NULL.\n");
		dhea_debug("Leaving: IOCTL_DHEA_ADAPTER_INIT.");
		return -1;
	}

	new_adapter->qp_rx_error_status_page_aligned = (unsigned long long *)
	((((unsigned long)(new_adapter->qp_rx_error_status))
	+ (PAGE_SIZE - 1)) & PAGE_MASK);

	new_adapter->qp_tx_error_status_page_aligned = (unsigned long long *)
	((((unsigned long)(new_adapter->qp_tx_error_status))
	+ (PAGE_SIZE - 1)) & PAGE_MASK);
	new_adapter->cq_error_status_page_aligned = (unsigned long long *)
	((((unsigned long)(new_adapter->cq_error_status))
	+ (PAGE_SIZE - 1)) & PAGE_MASK);

	SetPageReserved(virt_to_page((unsigned long)
	new_adapter->qp_rx_error_status_page_aligned));
	SetPageReserved(virt_to_page((unsigned long)
	new_adapter->qp_tx_error_status_page_aligned));
	SetPageReserved(virt_to_page((unsigned long)
	new_adapter->cq_error_status_page_aligned));

	INIT_LIST_HEAD(&(new_adapter->eq_list_head));
	new_adapter->head_cqs = kzalloc(sizeof(struct cq_res), GFP_KERNEL);
	new_adapter->head_channels = kzalloc(sizeof(struct channel_res),
	GFP_KERNEL);

	list_add(&(new_adapter->my_list), &(duser->adapter_list_head));

	kparms.qp_rx_error_status_size = PAGE_SIZE;
	kparms.qp_tx_error_status_size = PAGE_SIZE;
	kparms.cq_error_status_size = PAGE_SIZE;
	kparms.adapter_id = rhea_id;
	copy_to_user(uparms, &kparms, sizeof(kparms));

	/* Do set up for next mmap. */
	duser->ob_to_mmap.cq = NULL;
	duser->ob_to_mmap.qp = NULL;
	duser->ob_to_mmap.error_status = new_adapter;
	duser->ob_to_mmap.index = 1;

	dhea_debug("Leaving: IOCTL_DHEA_ADAPTER_INIT");
	return 0;
}

int dhea_adapter_fini(struct dhea_user *duser, unsigned long arg)
{
	int rtn_val = 0;
	struct dhea_adapter_fini_parms *uparms, kparms;
	unsigned rhea_id = 0;
	struct list_head *position = NULL;
	struct list_head *temp_list = NULL;
	struct adapter_res *curr = NULL;

	dhea_debug("Entering: IOCTL_DHEA_ADAPTER_FINI");

	uparms = (struct dhea_adapter_fini_parms *)arg;
	copy_from_user((void *)&kparms, (void *)uparms, sizeof(kparms));

	rhea_id = kparms.adapter_id;
	dhea_info("Releasing resoureces associated with ID = %u.\n", rhea_id);
	rtn_val = rhea_session_fini(rhea_id);
	kparms.error_number = rtn_val;
	copy_to_user(uparms, &kparms, sizeof(kparms));

	if (rtn_val) {
		dhea_error("Error in IOCTL_DHEA_ADAPTER_FINI.\n");
		dhea_debug("Leaving: IOCTL_DHEA_ADAPTER_FINI.\n");
		return -1;
	}

	list_for_each_safe(position, temp_list, &(duser->adapter_list_head)) {
		curr = list_entry(position, struct adapter_res, my_list);
		if (curr->id == rhea_id) {
			list_del(position);
			kfree(curr->qp_rx_error_status);
			kfree(curr->qp_tx_error_status);
			kfree(curr->cq_error_status);
			kfree(curr);
		}
	}

	dhea_debug("Leaving: IOCTL_DHEA_ADAPTER_FINI");

	return 0;
}

int dhea_get_version(struct dhea_user *duser, unsigned long arg)
{
	struct dhea_get_version_parms *uparms, kparms;

	dhea_debug("Entering: IOCTL_DHEA_GET_VERSION");

	uparms = (struct dhea_get_version_parms *)arg;
	copy_from_user(&kparms, uparms, sizeof(kparms));
	kparms.major = DHEA_MAJOR_VERSION;
	kparms.minor = DHEA_MINOR_VERSION;
	kparms.release = DHEA_RELEASE_VERSION;
	kparms.error_number = 0;
	copy_to_user(uparms, &kparms, sizeof(kparms));

	dhea_debug("Leaving: IOCTL_DHEA_GET_VERSION");
	return 0;
}


int	dhea_pport_count(struct dhea_user *duser, unsigned long arg)
{
	unsigned rhea_id;
	struct dhea_pport_count_parms *uparms, kparms;
	dhea_debug("Entering: IOCTL_DHEA_ADAPTER_COUNT");

	uparms = (struct dhea_pport_count_parms *)arg;
	copy_from_user(&kparms, uparms, sizeof(kparms));

	rhea_id = kparms.adapter_id;
	kparms.pport_count = rhea_pport_count(rhea_id);

	copy_to_user(&(uparms->pport_count), &(kparms.pport_count),
	sizeof(unsigned int));

	dhea_debug("Leaving: IOCTL_DHEA_ADAPTER_COUNT");

	return 0;
}


int	dhea_channel_alloc(struct dhea_user *duser, unsigned long arg)
{
	int rtn_val = 0;
	unsigned rhea_id;
	unsigned channel_id = 0;
	struct channel_res *new_channel = NULL;
	struct adapter_res *adapter = NULL;

	struct dhea_channel_alloc_parms *uparms, kparms;
	dhea_debug("Entering: IOCTL_DHEA_CHANNEL_ALLOC");

	uparms = (struct dhea_channel_alloc_parms *)arg;
	copy_from_user(&kparms, uparms, sizeof(kparms));

	rhea_id = kparms.adapter_id;
	if (kparms.channel_context.cfg.type == HEA_UC_PORT ||
		kparms.channel_context.cfg.type == HEA_MC_PORT ||
		kparms.channel_context.cfg.type == HEA_BC_PORT) {
			kparms.channel_context.cfg.dc.test =
		duser->enable_mac_loopback;
		}

	new_channel = kzalloc(sizeof(*new_channel), GFP_KERNEL);

	/* specify callback for link state change */
	kparms.channel_context.pport_event.fkt_ptr =
	&dhea_pport_link_state_callback;
	kparms.channel_context.pport_event.args = new_channel;

	rtn_val = rhea_channel_alloc(rhea_id, &channel_id,
	(struct hea_channel_context *)&(kparms.channel_context));

	kparms.channel_id = channel_id;
	kparms.error_number = rtn_val;

	copy_to_user(uparms, &kparms, sizeof(kparms));

	if (rtn_val) {
		kfree(new_channel);
		dhea_error("Error: IOCTL_DHEA_CHANNEL_ALLOC.");
		dhea_debug("Leaving: IOCTL_DHEA_CHANNEL_ALLOC.");
		return -1;
	}

	adapter = find_adapter_element_new(duser, rhea_id);
	if (adapter == NULL) {
		kfree(new_channel);
		dhea_error("Error: IOCTL_DHEA_CHANNEL_ALLOC: "
		"find_adapter_element_new failed to find adapter with ID %u.",
		 rhea_id);
		dhea_debug("Leaving: IOCTL_DHEA_CHANNEL_ALLOC.");
		return -1;
	}

	new_channel->adapter_id = rhea_id;
	new_channel->channel_id = channel_id;
	new_channel->head_qps = kzalloc(sizeof(struct qp_res), GFP_KERNEL);
	new_channel->owner = get_task_pid(current, PIDTYPE_PID);
	new_channel->last_reported_state = 1;

	add_channel_to_head(adapter->head_channels, new_channel);
	dhea_debug("Leaving: IOCTL_DHEA_CHANNEL_ALLOC.");

	return 0;
}

int	dhea_channel_free(struct dhea_user *duser, unsigned long arg)
{
	int rtn_val;
	unsigned rhea_id;
	unsigned channel_id;
	struct dhea_channel_free_parms *uparms, kparms;
	struct channel_res *channel_to_free;
	struct adapter_res *adapter;

	dhea_debug("Entering: IOCTL_DHEA_CHANNEL_FREE");
	uparms = (struct dhea_channel_free_parms *)arg;
	copy_from_user(&kparms, uparms, sizeof(kparms));

	rhea_id = kparms.adapter_id;
	channel_id = kparms.channel_id;
	rtn_val = rhea_channel_free(rhea_id, channel_id);

	copy_to_user(&(uparms->error_number), &(rtn_val), sizeof(int));

	if (rtn_val) {
		dhea_error("Error: IOCTL_DHEA_CHANNEL_FREE.");
		dhea_debug("Leaving: IOCTL_DHEA_CHANNEL_FREE.");
		return -1;
	}

	adapter = find_adapter_element_new(duser, rhea_id);
	if (adapter == NULL) {
		dhea_error("Error: IOCTL_DHEA_CHANNEL_FREE: "
		"find_adapter_element failed to find adapter with ID %u.",
		rhea_id);
		dhea_debug("Leaving: IOCTL_DHEA_CHANNEL_FREE.");
		return -1;
	}

	channel_to_free = delete_channel_element(adapter->head_channels,
	channel_id);
	kfree(channel_to_free);

	dhea_debug("Leaving: IOCTL_DHEA_CHANNEL_FREE.");
	return 0;
}

int	dhea_register_default_packets(struct dhea_user *duser,
		unsigned long arg)
{
	int rtn_val = 0;
	unsigned rhea_id;
	unsigned def_channel_id = 0;
	struct hea_channel_context channel_ctx;
	struct dhea_register_default_packets_parms *uparms, kparms;

	dhea_debug("Entering: IOCTL_DHEA_DEFAULT_PACKETS_REGISTER");

	memset(&channel_ctx, 0, sizeof(channel_ctx));
	uparms = (struct dhea_register_default_packets_parms *)arg;
	copy_from_user(&kparms, uparms, sizeof(kparms));

	rhea_id = kparms.adapter_id;

	channel_ctx.cfg.type = kparms.type;
	channel_ctx.cfg.pport_nr = kparms.pport_number;
	channel_ctx.cfg.max_frame_size = 0;
	channel_ctx.cfg.dc.lport_channel_id = kparms.channel_id;
	channel_ctx.cfg.dc.channel_usuage = HEA_DEFAULT_CHANNEL_SHARE;
	channel_ctx.cfg.dc.test = 0;

	/* specify callback in case of a link state change */
	channel_ctx.pport_event.args = NULL;
	channel_ctx.pport_event.fkt_ptr = NULL;

	rtn_val = rhea_channel_alloc(rhea_id, &def_channel_id, &(channel_ctx));

	kparms.def_channel_id = def_channel_id;
	kparms.error_number = rtn_val;

	copy_to_user(uparms, &kparms, sizeof(kparms));

	if (rtn_val) {
		dhea_error("Error: IOCTL_DHEA_DEFAULT_PACKETS_REGISTER.");
		dhea_error("Error: rhea_channel_alloc's return value "
		"indicated an error.");
		dhea_debug("Leaving: IOCTL_DHEA_DEFAULT_PACKETS_REGISTER.");
		return -1;
	}

	rtn_val = rhea_channel_enable(rhea_id, def_channel_id);
	kparms.error_number = rtn_val;

	copy_to_user(uparms, &kparms, sizeof(kparms));

	if (rtn_val) {
		dhea_error("Error: IOCTL_DHEA_DEFAULT_PACKETS_REGISTER.");
		dhea_error("Error: rhea_channel_enable's return "
		"value indicated an error.");
		dhea_debug("Leaving: IOCTL_DHEA_DEFAULT_PACKETS_REGISTER.");
		if (rhea_channel_free(rhea_id, def_channel_id) < 0)
			dhea_error("Error failed to free default channel.");

		return -1;
	}

	dhea_debug("Leaving: IOCTL_DHEA_DEFAULT_PACKETS_REGISTER.");

	return 0;
}

int	dhea_deregister_default_packets(struct dhea_user *duser,
		unsigned long arg)
{
	int rtn_val = 0;
	unsigned rhea_id;
	unsigned channel_id;
	struct dhea_deregister_default_packets_parms *uparms, kparms;

	dhea_debug("Entering: IOCTL_DHEA_DEFAULT_PACKETS_DEREGISTER");
	uparms = (struct dhea_deregister_default_packets_parms *)arg;
	copy_from_user(&kparms, uparms, sizeof(kparms));

	rhea_id = kparms.adapter_id;
	channel_id = kparms.channel_id;

	rtn_val = rhea_channel_disable(rhea_id, channel_id);

	if (rtn_val) {
		dhea_warning("Warning: rhea_channel_disable's return value "
		"indicated an error %d.", rtn_val);
	}

	rtn_val = rhea_channel_free(rhea_id, channel_id);
	kparms.error_number = rtn_val;
	copy_to_user(&(uparms->error_number), &(rtn_val), sizeof(int));
	if (rtn_val) {
		dhea_error("Error: IOCTL_DEREGISTER_DEFAULT_PACKETS.");
		dhea_debug("Leaving: IOCTL_DEREGISTER_DEFAULT_PACKETS.");
		return -1;
	}

	dhea_debug("Leaving: IOCTL_DEREGISTER_DEFAULT_PACKETS.");
	return 0;
}

void dhea_scan_event_queue(struct eq_res *eq)
{
	struct hea_eqe *eqe_current;
	unsigned q_nr;

	eqe_current = eq->eq.qe_current;

	while (hea_eqe_is_valid(eqe_current)) {
		if (unlikely(hea_eqe_is_completion(eqe_current)))
			dhea_error(" event 0x%016llx",
			*((unsigned long long *)eqe_current));
		else {
			dhea_info(" event 0x%016llx", eqe_current->eqe);

			switch (hea_eqe_event_type(eqe_current)) {
			case HEA_EQE_ET_QP_WARNING:
				q_nr = hea_eqe_qp_number(eqe_current);

				dhea_info("Warning for QP: %u.\n", q_nr);
				break;

			case HEA_EQE_ET_CP_WARNING:
				q_nr = hea_eqe_cq_number(eqe_current);


				dhea_info("Warning for CQ: %u.\n", q_nr);
				break;

			case HEA_EQE_ET_QP_ERROR_EQ0:
			case HEA_EQE_ET_QP_ERROR:

				q_nr = hea_eqe_qp_number(eqe_current);

				dhea_info("Error for QP: %u.\n", q_nr);

				break;

			case HEA_EQE_ET_CQ_ERROR_EQ0:
			case HEA_EQE_ET_CQ_ERROR:
				q_nr = hea_eqe_cq_number(eqe_current);

				dhea_info("Error for CQ: %u.\n", q_nr);
				break;

			case HEA_EQE_ET_PORT_EVENT:
				dhea_info("HEA_EQE_ET_PORT_EVENT\n");
				break;

			case HEA_EQE_ET_EQ_ERROR:
				dhea_info("HEA_EQE_ET_EQ_ERROR\n");
				break;

			case HEA_EQE_ET_UA_ERROR:
				dhea_info("HEA_EQE_ET_UA_ERROR\n");
				break;

			case HEA_EQE_ET_FIRST_ERROR_CAPTURE_INFO:
				dhea_info("First Error Capture info\n");
				break;

			case HEA_EQE_ET_COP_CQ_ACCESS_ERROR:
				dhea_info("HEA_EQE_ET_COP_CQ_ACCESS_ERROR\n");
				break;

			case HEA_EQE_ET_COP_QP_ACCESS_ERROR:
				dhea_info("HEA_EQE_ET_COP_QP_ACCESS_ERROR\n");
				break;

			case HEA_EQE_ET_COP_TICKET_ACCESS_ERROR:
			case HEA_EQE_ET_COP_TICKET_ERROR:
			case HEA_EQE_ET_COP_DATA_ERROR:
			default:
				;
			}
		}

		/* clear this entry and move to next one */
		eqe_current->eqe = 0;
		heaq_set_next_qe(&eq->eq);
		eqe_current = eq->eq.qe_current;
	}
}


int	dhea_eq_alloc(struct dhea_user *duser, unsigned long arg)
{
	int rtn_val = 0;
	unsigned rhea_id;
	unsigned eq_id;
	struct dhea_eq_alloc_parms *uparms, kparms;
	struct eq_res *neq_eq = kzalloc(sizeof(*neq_eq), GFP_KERNEL);
	struct hea_eq_context eq_ctx;
	struct adapter_res *adapter;

	dhea_debug("Entering: IOCTL_DHEA_EQ_ALLOC");
	uparms = (struct dhea_eq_alloc_parms *)arg;
	copy_from_user(&kparms, uparms, sizeof(kparms));

	rhea_id = kparms.adapter_id;

	memset(&eq_ctx, 0, sizeof(struct hea_eq_context));
	eq_ctx.process.lpar = 0; /* FIXME: LPAR ID */
	eq_ctx.process.pid = current->tgid;
	eq_ctx.process.uid = current_uid();
	eq_ctx.process.user_process = (void *) current;

	eq_ctx.cfg.eqe_count = kparms.eqe_count;
	eq_ctx.cfg.irq_type = HEA_IRQ_COALESING_2;
	eq_ctx.cfg.coalesing2_delay = HEA_EQ_COALESING_DELAY_3;
	eq_ctx.cfg.generate_completion_events = HEA_EQ_GEN_COM_EVENT_DISABLE;

	rtn_val = rhea_eq_alloc(rhea_id, &eq_id, &eq_ctx);

	if (rtn_val) {
		kparms.error_number = rtn_val;
		copy_to_user(uparms, &kparms, sizeof(kparms));
		dhea_error("Error in rhea_eq_alloc: IOCTL_DHEA_EQ_ALLOC.");
		dhea_debug("Leaving: IOCTL_DHEA_EQ_ALLOC.");
		return -1;
	}

	kparms.eq_id = eq_id;
	neq_eq->adapter_id = rhea_id;
	neq_eq->eq_id = eq_id;
	neq_eq->next = NULL;


	/* setup access to EQ */
	rtn_val = rhea_eq_table(rhea_id, eq_id, (struct hea_eqe **)
	(&neq_eq->eq.q_begin), &neq_eq->eq.qe_size, &neq_eq->eq.qe_count);
	if (rtn_val) {
		kparms.error_number = rtn_val;
		copy_to_user(uparms, &kparms, sizeof(kparms));
		dhea_error("Error in rhea_eq_table: IOCTL_DHEA_EQ_ALLOC.");
		dhea_debug("Leaving: IOCTL_DHEA_EQ_ALLOC.");
		return -1;
	}

	heaq_init(&neq_eq->eq);

	adapter = find_adapter_element_new(duser, rhea_id);
	if (adapter == NULL) {
		dhea_error("Error: IOCTL_DHEA_EQ_ALLOC: find_adapter_element "
		"failed to find adapter with ID %u.", rhea_id);
		dhea_debug("Leaving: IOCTL_DHEA_EQ_ALLOC.");
		return -1;
	}

	neq_eq->adapter = adapter;


	list_add(&(neq_eq->my_list), &(adapter->eq_list_head));

	/* setup interrupts */
	rtn_val = rhea_interrupt_setup(rhea_id, eq_id, dhea_irq_handler,
	(void *)neq_eq);
	if (rtn_val) {
		kparms.error_number = rtn_val;
		copy_to_user(uparms, &kparms, sizeof(kparms));
		dhea_error("Error in rhea_interrupt_setup: "
		"Could not allocate new IRQ");
		dhea_debug("Leaving: IOCTL_DHEA_EQ_ALLOC.");
		return -1;
	}

	kparms.error_number = 0;
	copy_to_user(uparms, &kparms, sizeof(kparms));

	dhea_debug("Leaving: IOCTL_DHEA_EQ_ALLOC.");
	return 0;
}

int	dhea_eq_free(struct dhea_user *duser, unsigned long arg)
{
	int rtn_val;
	unsigned rhea_id;
	unsigned eq_id;
	struct dhea_eq_free_parms *uparms, kparms;
	struct adapter_res *adapter;
	struct list_head *position = NULL;
	struct list_head *temp_list = NULL;
	struct eq_res *curr = NULL;

	dhea_debug("Entering: IOCTL_DHEA_EQ_FREE");
	uparms = (struct dhea_eq_free_parms *)arg;
	copy_from_user(&kparms, uparms, sizeof(kparms));

	rhea_id = kparms.adapter_id;
	eq_id = kparms.eq_id;

	rtn_val = rhea_eq_free(rhea_id, eq_id);

	copy_to_user(&(uparms->error_number), &(rtn_val), sizeof(int));

	if (rtn_val) {
		dhea_error("Error: IOCTL_DHEA_EQ_FREE.");
		dhea_debug("Leaving: IOCTL_DHEA_EQ_FREE.");
		return -1;
	}

	adapter = find_adapter_element_new(duser, rhea_id);
	if (adapter == NULL) {
		dhea_error("Error: IOCTL_DHEA_EQ_FREE: find_adapter_element "
		"failed to find adapter with ID %u.", rhea_id);
		dhea_debug("Leaving: IOCTL_DHEA_EQ_FREE.");
		return -1;
	}

	list_for_each_safe(position, temp_list, &(adapter->eq_list_head)) {
		curr = list_entry(position, struct eq_res, my_list);
		if (curr->eq_id == eq_id) {
			list_del(position);
			kfree(curr);
		}
	}

	dhea_debug("Leaving: IOCTL_DHEA_EQ_FREE.");
	return 0;
}

int	dhea_cq_alloc(struct dhea_user *duser, unsigned long arg)
{
	int rtn_val = 0;
	unsigned rhea_id;
	unsigned cq_id;
	struct dhea_cq_alloc_parms *uparms, kparms;
	struct cq_res *ncq_cq = kzalloc(sizeof(*ncq_cq), GFP_KERNEL);
	struct hea_cq_context cq_ctx;
	struct hea_cqe *begin_cqe;
	struct adapter_res *adapter = NULL;
	unsigned int cqe_size, cqe_count;
	unsigned int cq_map_size;
	unsigned long long *kregs, *uregs;

	dhea_debug("Entering: IOCTL_DHEA_CQ_ALLOC");
	uparms = (struct dhea_cq_alloc_parms *)arg;
	copy_from_user(&kparms, uparms, sizeof(kparms));
	memset(&cq_ctx, 0, sizeof(cq_ctx));

	rhea_id = kparms.adapter_id;

	cq_ctx.process.lpar = 0; /* FIXME: LPAR ID */
	cq_ctx.process.pid = current->tgid;
	cq_ctx.process.uid = current_user()->uid;
	cq_ctx.process.user_process = (void *) current;

	cq_ctx.ceq = kparms.ceq;
	cq_ctx.aeq = kparms.aeq;

	cq_ctx.cfg.cqe_count = kparms.cqe_count;
	cq_ctx.cfg.hw_managed = kparms.hw_managed;
	cq_ctx.cfg.cqe_auto_toggle = 1;
	cq_ctx.cfg.cache_inject = kparms.cache_inject;
	cq_ctx.cfg.target_cache = kparms.target_cache;

	/* cq_ctx.cfg.token = DHEA_CQ_TOKEN; */

	rtn_val = rhea_cq_alloc(rhea_id, &cq_id, &cq_ctx);

	if (rtn_val) {
		kparms.error_number = rtn_val;
		copy_to_user(uparms, &kparms, sizeof(kparms));
		dhea_error("Error in rhea_cq_alloc: IOCTL_DHEA_CQ_ALLOC.");
		dhea_debug("Leaving: IOCTL_DHEA_CQ_ALLOC.");
		return -1;
	}

	kparms.cq_id = cq_id;

	rtn_val = rhea_cq_table(rhea_id, cq_id, &begin_cqe, &cqe_size,
		 &cqe_count);

	dhea_debug("rhea_cq_table returned %u CQEs with size %u.\n",
	cqe_count, cqe_size);

	if (rtn_val) {
		kparms.error_number = rtn_val;
		copy_to_user(uparms, &kparms, sizeof(kparms));
		dhea_error("Error in rhea_cq_table: IOCTL_DHEA_CQ_ALLOC.");
		dhea_debug("Leaving: IOCTL_DHEA_CQ_ALLOC.");
		return -1;
	}

	rtn_val = rhea_cq_mapinfo(rhea_id, cq_id, HEA_PRIV_PRIV,
	(void **)&kregs, &cq_map_size, 1);
	if (rtn_val) {
		kparms.error_number = rtn_val;
		copy_to_user(uparms, &kparms, sizeof(kparms));
		dhea_error("Error in rhea_cq_mapinfo priv: "
		"IOCTL_DHEA_CQ_ALLOC.");
		dhea_debug("Leaving: IOCTL_DHEA_CQ_ALLOC.");
		return -1;
	}

	rtn_val = rhea_cq_mapinfo(rhea_id, cq_id, HEA_PRIV_USER,
	(void **)&uregs, &cq_map_size, 0);
	if (rtn_val) {
		kparms.error_number = rtn_val;
		copy_to_user(uparms, &kparms, sizeof(kparms));
		dhea_error("Error in rhea_cq_mapinfo user: "
		"IOCTL_DHEA_CQ_ALLOC.");
		dhea_debug("Leaving: IOCTL_DHEA_CQ_ALLOC.");
		return -1;
	}

	ncq_cq->cq_id = cq_id;
	ncq_cq->cq_map_size = cq_map_size;
	ncq_cq->cqe_count = cqe_count;
	ncq_cq->priv_cq_registers = kregs;
	ncq_cq->user_cq_registers = uregs;
	ncq_cq->begin_cqe = begin_cqe;
	ncq_cq->end_cqe = begin_cqe + cqe_count;
	ncq_cq->next = NULL;

	adapter = find_adapter_element_new(duser, rhea_id);
	if (adapter == NULL) {
		dhea_error("Error: IOCTL_DHEA_CQ_ALLOC: find_adapter_element "
		"failed to find adapter with ID %u.", rhea_id);
		dhea_debug("Leaving: IOCTL_DHEA_CQ_ALLOC.");
		return -1;
	}

	add_cq_to_head(adapter->head_cqs, ncq_cq);

	kparms.error_number = 0;
	kparms.cq_map_size = cq_map_size;
	kparms.cq_size_bytes = cqe_size*cqe_count;

	/* Do set up for next mmap. */
	duser->ob_to_mmap.cq = ncq_cq;
	duser->ob_to_mmap.qp = NULL;
	duser->ob_to_mmap.error_status = NULL;
	duser->ob_to_mmap.index = 1;

	copy_to_user(uparms, &kparms, sizeof(kparms));
	dhea_debug("Leaving: IOCTL_DHEA_CQ_ALLOC.");
	return 0;
}

int	dhea_cq_free(struct dhea_user *duser, unsigned long arg)
{
	int rtn_val;
	unsigned rhea_id;
	unsigned cq_id;
	struct dhea_cq_free_parms *uparms, kparms;
	struct adapter_res *adapter;
	struct cq_res *cq_to_free;

	dhea_debug("Entering: IOCTL_DHEA_CQ_FREE");
	uparms = (struct dhea_cq_free_parms *)arg;
	copy_from_user(&kparms, uparms, sizeof(kparms));

	rhea_id = kparms.adapter_id;
	cq_id = kparms.cq_id;
	rtn_val = rhea_cq_free(rhea_id, cq_id);

	copy_to_user(&(uparms->error_number), &(rtn_val), sizeof(int));

	if (rtn_val) {
		dhea_error("Error: IOCTL_DHEA_CQ_FREE.");
		dhea_debug("Leaving: IOCTL_DHEA_CQ_FREE.");
		return -1;
	}

	adapter = find_adapter_element_new(duser, rhea_id);
	if (adapter == NULL) {
		dhea_error("Error: IOCTL_DHEA_CQ_FREE: find_adapter_element "
		"failed to find adapter with ID %u.", rhea_id);
		dhea_debug("Leaving: IOCTL_DHEA_CQ_FREE.");
		return -1;
	}

	cq_to_free = delete_cq_element(adapter->head_cqs, cq_id);
	kfree(cq_to_free);

	dhea_debug("Leaving: IOCTL_DHEA_CQ_FREE.");
	return 0;
}

int	dhea_qp_alloc(struct dhea_user *duser, unsigned long arg)
{
	int rtn_val = 0;
	unsigned rhea_id = 0;
	unsigned channel_id = 0;
	unsigned qp_id = 0;
	struct dhea_qp_alloc_parms *uparms, kparms;
	struct qp_res *nqp_qp = kzalloc(sizeof(*nqp_qp), GFP_KERNEL);
	struct hea_qp_context qp_ctx;
	unsigned long long *kregs, *uregs;
	unsigned int qp_map_size, sq_wqe_size, sq_count;
	unsigned int rq_wqe_size[3], rq_count[3];
	unsigned char *begin_sq;
	unsigned char *begin_rq[3];
	struct adapter_res *adapter;
	struct channel_res *channel;
	int i;
	struct hea_pd_cfg pd_cfg;
	memset(&qp_ctx, 0, sizeof(struct hea_qp_context));

	/* Protection Domain Stuff (needed for translations and security) */
	memset(&pd_cfg, 0, sizeof(pd_cfg));
	pd_cfg.as_bit = 0;
	pd_cfg.gs_bit = 0;
	pd_cfg.pr_bit = 1;
	pd_cfg.enable_pid_validation = 1;
	pd_cfg.pid = current->mm->context.id & ((1<<14) - 1);
	qp_ctx.pd_cfg = pd_cfg;

	dhea_debug("Entering: IOCTL_DHEA_QP_ALLOC and pid equals %lu", pid);
	uparms = (struct dhea_qp_alloc_parms *)arg;
	copy_from_user(&kparms, uparms, sizeof(kparms));

	rhea_id = kparms.adapter_id;
	channel_id = kparms.channel_id;

	dhea_debug("RQ1 has %u WQEs, RQ2 has %u WQEs, RQ3 has %u WQEs .\n",
	kparms.rq1.wqe_count, kparms.rq2.wqe_count, kparms.rq3.wqe_count);

	/* Set up QP context */
	qp_ctx.cfg.sq = kparms.sq;
	qp_ctx.cfg.rq1 = kparms.rq1;
	qp_ctx.cfg.rq2 = kparms.rq2;
	qp_ctx.cfg.rq3_ep = kparms.rq3;

	qp_ctx.process.lpar = 0; /* FIXME: LPAR ID */
	qp_ctx.process.pid = current->tgid;
	qp_ctx.process.uid = current_user()->uid;
	qp_ctx.process.user_process = (void *) current;

	/* Come back to this when we support NN mode.
	Remember nn and ep in a union!
	qp_ctx.cfg.nn.nn_ticket_set_id = kparms.nn_ticket_set_id; */

	qp_ctx.cfg.ep.ll_cache_inject = kparms.ll_cache_inject;
	qp_ctx.cfg.ep.header_sep = kparms.header_sep;

	qp_ctx.cfg.cache_inject = kparms.cache_inject;
	qp_ctx.cfg.target_cache = kparms.target_cache;

	qp_ctx.eq = kparms.eq_id;
	qp_ctx.r_cq = kparms.rcq_id;
	qp_ctx.s_cq = kparms.scq_id;
	qp_ctx.channel = channel_id;

	qp_ctx.cfg.r_cq_use = kparms.r_cq_use;
	qp_ctx.cfg.s_cq_use = kparms.s_cq_use;
	qp_ctx.cfg.hw_managed = kparms.hw_managed;

	qp_ctx.cfg.sq.priority = 0;
	qp_ctx.cfg.sq.tenure = 25;

	rtn_val = rhea_qp_alloc(rhea_id, &qp_id, &qp_ctx);

	if (rtn_val) {
		kparms.error_number = rtn_val;
		copy_to_user(uparms, &kparms, sizeof(kparms));
		dhea_error("Error in rhea_qp_alloc: IOCTL_DHEA_QP_ALLOC.");
		dhea_debug("Leaving: IOCTL_DHEA_QP_ALLOC.");
		return -1;
	}

	kparms.qp_id = qp_id;
	rtn_val = rhea_qp_mapinfo(rhea_id, qp_id, HEA_PRIV_PRIV,
	(void **)&kregs, &qp_map_size, 1);
	if (rtn_val) {
		kparms.error_number = rtn_val;
		copy_to_user(uparms, &kparms, sizeof(kparms));
		dhea_error("Error in rhea_qp_mapinfo priv: IOCTL_DHEA_ALLOC.");
		dhea_debug("Leaving: IOCTL_DHEA_QP_ALLOC.");
		return -1;
	}

	rtn_val = rhea_qp_mapinfo(rhea_id, qp_id, HEA_PRIV_USER,
	(void **)&uregs, &qp_map_size, 0);
	if (rtn_val) {
		kparms.error_number = rtn_val;
		copy_to_user(uparms, &kparms, sizeof(kparms));
		dhea_error("Error in rhea_qp_mapinfo user: "
	" IOCTL_DHEA_QP_ALLOC.");
		dhea_debug("Leaving: IOCTL_DHEA_QP_ALLOC.");
		return -1;
	}

	rtn_val = rhea_sq_table(rhea_id, qp_id, (union snd_wqe **)&begin_sq,
	&sq_wqe_size, &sq_count);
	if (rtn_val) {
		kparms.error_number = rtn_val;
		copy_to_user(uparms, &kparms, sizeof(kparms));
		dhea_error("Error in rhea_sq_table: IOCTL_DHEA_QP_ALLOC.");
		dhea_debug("Leaving: IOCTL_DHEA_QP_ALLOC.");
		return -1;
	}

	dhea_debug("The SQ WQE size equals %u.\n", sq_wqe_size);

	for (i = 0; i < 3; i++) {
		if (i == 0 || (i == 1 && kparms.rq2.wqe_count > 0) ||
		(i == 2 && kparms.rq3.wqe_count > 0)) {
			rtn_val = rhea_rq_table(rhea_id, qp_id, i+1,
			(union rcv_wqe **)&(begin_rq[i]), &(rq_wqe_size[i]),
			&(rq_count[i]));
			if (rtn_val) {
				kparms.error_number = rtn_val;
				copy_to_user(uparms, &kparms, sizeof(kparms));
				dhea_error("Error in rhea_rq_table"
			" %d: IOCTL_DHEA_QP_ALLOC.", i);
				dhea_debug("Leaving: IOCTL_DHEA_QP_ALLOC.");
				return -1;
			}
		}
	}

	nqp_qp->qp_id = qp_id;
	nqp_qp->priv_qp_registers = kregs;
	nqp_qp->user_qp_registers = uregs;
	nqp_qp->sq_begin = begin_sq;
	nqp_qp->sq_end = begin_sq + (sq_wqe_size*sq_count);

	for (i = 0; i < 3; i++) {
		if (i == 0 || (i == 1 && kparms.rq2.wqe_count > 0) ||
		(i == 2 && kparms.rq3.wqe_count > 0)) {
			nqp_qp->rq_begin[i] = begin_rq[i];
			nqp_qp->rq_end[i] =
			begin_rq[i] + (rq_wqe_size[i]*rq_count[i]);
		}
	}
	nqp_qp->next = NULL;

	adapter = find_adapter_element_new(duser, rhea_id);
	if (adapter == NULL) {
		dhea_error("Error: IOCTL_DHEA_QP_ALLOC: find_adapter_element "
	"failed to find adapter with ID %u.", rhea_id);
		dhea_debug("Leaving: IOCTL_DHEA_QP_ALLOC.");
		return -1;
	}

	channel = find_channel_element(adapter->head_channels, channel_id);
	if (adapter == NULL) {
		dhea_error("Error: IOCTL_DHEA_CQ_ALLOC.");
		dhea_debug("Leaving: IOCTL_DHEA_CQ_ALLOC.");
		return -1;
	}

	add_qp_to_head(channel->head_qps, nqp_qp);

	/* Do set up for next mmap. */
	duser->ob_to_mmap.cq = NULL;
	duser->ob_to_mmap.error_status = NULL;
	duser->ob_to_mmap.qp = nqp_qp;
	duser->ob_to_mmap.index = 1;

	kparms.error_number = 0;
	kparms.qp_map_size = qp_map_size;
	kparms.sq_size_bytes = sq_wqe_size*sq_count;

	for (i = 0; i < 3; i++) {
		if (i == 0 || (i == 1 && kparms.rq2.wqe_count > 0) ||
		(i == 2 && kparms.rq3.wqe_count > 0)) {
			kparms.rq_size_bytes[i] = rq_wqe_size[i]*rq_count[i];
		}
	}

	copy_to_user(uparms, &kparms, sizeof(kparms));
	dhea_debug("Leaving: IOCTL_DHEA_CQ_ALLOC.");

	return 0;
}

int	dhea_qp_free(struct dhea_user *duser, unsigned long arg)
{
	int rtn_val;
	unsigned rhea_id;
	unsigned channel_id;
	unsigned qp_id;

	struct qp_res *qp_to_free;
	struct adapter_res *adapter;
	struct channel_res *channel;
	struct dhea_qp_free_parms *uparms, kparms;

	dhea_debug("Entering: IOCTL_DHEA_QP_FREE");
	uparms = (struct dhea_qp_free_parms *)arg;
	copy_from_user(&kparms, uparms, sizeof(kparms));

	rhea_id = kparms.adapter_id;
	qp_id = kparms.qp_id;
	channel_id = kparms.channel_id;

	rtn_val = rhea_qp_free(rhea_id, qp_id);

	copy_to_user(&(uparms->error_number), &(rtn_val), sizeof(int));

	if (rtn_val) {
		dhea_error("Error: IOCTL_DHEA_QP_FREE.");
		dhea_debug("Leaving: IOCTL_DHEA_QP_FREE.");
		return -1;
	}

	adapter = find_adapter_element_new(duser, rhea_id);
	if (adapter == NULL) {
		dhea_error("Error: IOCTL_DHEA_QP_FREE: find_adapter_element"
	"failed to find adapter with ID %u.", rhea_id);
		dhea_debug("Leaving: IOCTL_DHEA_QP_FREE.");
		return -1;
	}

	channel = find_channel_element(adapter->head_channels, channel_id);
	qp_to_free = delete_qp_element(channel->head_qps, qp_id);

	kfree(qp_to_free);

	dhea_debug("Leaving: IOCTL_DHEA_QP_FREE.");
	return 0;
}


int dhea_qpn_array_alloc(struct dhea_user *duser, unsigned long arg)
{
	int rtn_val = 0;
	unsigned rhea_id;
	unsigned channel_id;
	struct dhea_qpn_array_alloc_parms *uparms, kparms;
	struct hea_qpn_context qpn_context;
	unsigned base_slot = 0;

	dhea_debug("Entering: IOCTL_DHEA_QPN_ARRAY_ALLOC");

	uparms = (struct dhea_qpn_array_alloc_parms *)arg;
	copy_from_user(&kparms, uparms, sizeof(kparms));

	rhea_id = kparms.adapter_id;
	channel_id = kparms.channel_id;

	memset(&qpn_context, 0, sizeof(qpn_context));
	qpn_context.qpn_cfg.slot_count = kparms.num_slots;

	rtn_val = rhea_channel_qpn_alloc(rhea_id, channel_id, &qpn_context);

	kparms.base_slot = base_slot;
	kparms.error_number = rtn_val;
	copy_to_user(uparms, &kparms, sizeof(kparms));

	if (rtn_val) {
		dhea_error("Error in IOCTL_DHEA_QPN_ARRAY_ALLOC %d.\n",
		rtn_val);
		dhea_debug("Leaving: IOCTL_DHEA_QPN_ARRAY_ALLOC.");
		return -1;
	}

	dhea_debug("Leaving: IOCTL_DHEA_QPN_ARRAY_ALLOC.");
	return 0;
}

int dhea_qpn_array_free(struct dhea_user *duser, unsigned long arg)
{
	int rtn_val = 0;
	unsigned rhea_id;
	unsigned channel_id;
	struct dhea_qpn_array_free_parms *uparms, kparms;

	dhea_debug("Entering: IOCTL_DHEA_QPN_ARRAY_FREE");

	uparms = (struct dhea_qpn_array_free_parms *)arg;
	copy_from_user(&kparms, uparms, sizeof(kparms));

	rhea_id = kparms.adapter_id;
	channel_id = kparms.channel_id;


	rtn_val = rhea_channel_qpn_free(rhea_id, channel_id);

	kparms.error_number = rtn_val;
	copy_to_user(uparms, &kparms, sizeof(kparms));

	if (rtn_val) {
		dhea_error("Error in IOCTL_DHEA_QPN_FREE_ALLOC %d.\n",
		rtn_val);
		dhea_debug("Leaving: IOCTL_DHEA_QPN_FREE_ALLOC.");
		return -1;
	}

	dhea_debug("Leaving: IOCTL_DHEA_QPN_ARRAY_ALLOC.");
	return 0;
}

int	dhea_get_default_mac(struct dhea_user *duser, unsigned long arg)
{

	int rtn_val = 0;
	unsigned rhea_id;
	unsigned channel_id;
	struct dhea_get_default_mac_parms *uparms, kparms;
	union hea_mac_addr mac_address;

	dhea_debug("Entering: IOCTL_DHEA_GET_DEFAULT_MAC");

	uparms = (struct dhea_get_default_mac_parms *)arg;
	copy_from_user(&kparms, uparms, sizeof(kparms));

	rhea_id = kparms.adapter_id;
	channel_id = kparms.channel_id;

	rtn_val = rhea_channel_macaddr_get(rhea_id, channel_id, &mac_address);

	kparms.mac_address = mac_address;
	kparms.error_number = rtn_val;

	copy_to_user(uparms, &kparms, sizeof(kparms));

	if (rtn_val) {
		dhea_error("Error in IOCTL_DHEA_GET_DEFAULT_MAC.");
		dhea_debug("Leaving: IOCTL_DHEA_GET_DEFAULT_MAC.");
		return -1;
	}

	dhea_debug("Leaving: IOCTL_DHEA_GET_DEFAULT_MAC.");

	return 0;
}

int	dhea_qp_up(struct dhea_user *duser, unsigned long arg)
{
	int rtn_val = 0;
	unsigned rhea_id;
	unsigned qp_id;
	struct dhea_qp_up_parms *uparms, kparms;

	dhea_debug("Entering: IOCTL_DHEA_QP_UP");
	uparms = (struct dhea_qp_up_parms *)arg;
	copy_from_user(&kparms, uparms, sizeof(kparms));
	rhea_id = kparms.adapter_id;
	qp_id = kparms.qp_id;

	rtn_val = rhea_qp_up(rhea_id, qp_id);

	dhea_debug("IOCTL_DHEA_QP_UP return value equals %d.", rtn_val);

	kparms.error_number = rtn_val;

	copy_to_user(uparms, &kparms, sizeof(kparms));

	if (rtn_val) {
		dhea_error("Error in IOCTL_DHEA_QP_UP.");
		dhea_debug("Leaving: IOCTL_DHEA_QP_UP.");
		return -1;
	}

	dhea_debug("Leaving: IOCTL_DHEA_QP_UP.");
	return 0;
}

int	dhea_qp_down(struct dhea_user *duser, unsigned long arg)
{
	int rtn_val = 0;
	unsigned rhea_id;
	unsigned qp_id;
	struct dhea_qp_down_parms *uparms, kparms;

	dhea_debug("Entering: IOCTL_DHEA_QP_DOWN");
	uparms = (struct dhea_qp_down_parms *)arg;
	copy_from_user(&kparms, uparms, sizeof(kparms));
	rhea_id = kparms.adapter_id;
	qp_id = kparms.qp_id;

	rtn_val = rhea_qp_down(rhea_id, qp_id);

	dhea_debug("IOCTL_DHEA_QP_DOWN return value equals %d.", rtn_val);

	kparms.error_number = rtn_val;

	copy_to_user(uparms, &kparms, sizeof(kparms));

	if (rtn_val) {
		dhea_error("Error in IOCTL_DHEA_QP_DOWN.");
		dhea_debug("Leaving: IOCTL_DHEA_QP_DOWN.");
		return -1;
	}

	dhea_debug("Leaving: IOCTL_DHEA_QP_DOWN.");
	return 0;
}

int	dhea_channel_up(struct dhea_user *duser, unsigned long arg)
{
	int rtn_val = 0;
	unsigned rhea_id;
	unsigned channel_id;
	struct dhea_channel_up_parms *uparms, kparms;

	dhea_debug("Entering: IOCTL_DHEA_CHANNEL_UP");
	uparms = (struct dhea_channel_up_parms *)arg;
	copy_from_user(&kparms, uparms, sizeof(kparms));
	rhea_id = kparms.adapter_id;
	channel_id = kparms.channel_id;

	rtn_val = rhea_channel_enable(rhea_id, channel_id);

	kparms.error_number = rtn_val;

	copy_to_user(uparms, &kparms, sizeof(kparms));

	if (rtn_val) {
		dhea_error("Error in IOCTL_DHEA_CHANNEL_UP.");
		dhea_debug("Leaving: IOCTL_DHEA_CHANNEL_UP.\n");
		return -1;
	}

	dhea_debug("Leaving: IOCTL_DHEA_CHANNEL_UP.\n");
	return 0;
}


int	dhea_channel_down(struct dhea_user *duser, unsigned long arg)
{
	int rtn_val = 0;
	unsigned rhea_id;
	unsigned channel_id;
	struct dhea_channel_down_parms *uparms, kparms;

	dhea_debug("Entering: IOCTL_DHEA_CHANNEL_DOWN");
	uparms = (struct dhea_channel_down_parms *)arg;
	copy_from_user(&kparms, uparms, sizeof(kparms));
	rhea_id = kparms.adapter_id;
	channel_id = kparms.channel_id;

	rtn_val = rhea_channel_disable(rhea_id, channel_id);

	kparms.error_number = rtn_val;

	copy_to_user(uparms, &kparms, sizeof(kparms));

	if (rtn_val) {
		dhea_error("Error in IOCTL_DHEA_CHANNEL_DOWN.");
		dhea_debug("Leaving: IOCTL_DHEA_CHANNEL_DOWN.\n");
		return -1;
	}

	dhea_debug("Leaving: IOCTL_DHEA_CHANNEL_DOWN.\n");
	return 0;
}

int	dhea_wire_qpn_to_qp(struct dhea_user *duser, unsigned long arg)
{
	int rtn_val = 0;
	unsigned rhea_id;
	unsigned channel_id;
	unsigned qp_id;
	unsigned slot_base;
	unsigned int offset;

	struct dhea_wire_qpn_to_qp_parms *uparms, kparms;
	dhea_debug("Entering: IOCTL_WIRE_QPN_TO_QP");

	uparms = (struct dhea_wire_qpn_to_qp_parms *)arg;
	copy_from_user(&kparms, uparms, sizeof(kparms));
	rhea_id = kparms.adapter_id;
	channel_id = kparms.channel_id;
	qp_id = kparms.qp_id;
	slot_base = kparms.base_slot;
	offset = kparms.offset;

	rtn_val = rhea_channel_wire_qpn_to_qp(rhea_id, channel_id, qp_id,
	offset);

	kparms.error_number = rtn_val;
	copy_to_user(uparms, &kparms, sizeof(kparms));
	if (rtn_val) {
		dhea_error("Error in IOCTL_WIRE_QPN_TO_QP.");
		dhea_debug("Leaving: IOCTL_WIRE_QPN_TO_QP.\n");
		return -1;
	}

	dhea_debug("Leaving: IOCTL_WIRE_QPN_TO_QP.\n");
	return 0;
}

int dhea_tcam_slot_alloc(struct dhea_user *duser, unsigned long arg)
{
	int rtn_val = 0;
	unsigned rhea_id;
	unsigned channel_id;
	unsigned tcam_id;
	struct hea_tcam_context tcam_context;
	struct dhea_tcam_slot_alloc_parms *uparms, kparms;
	dhea_debug("Entering: IOCTL_DHEA_TCAM_SLOT_ALLOC");
	uparms = (struct dhea_tcam_slot_alloc_parms *)arg;
	copy_from_user(&kparms, uparms, sizeof(kparms));
	rhea_id = kparms.adapter_id;
	channel_id = kparms.channel_id;

	memcpy(&tcam_context, &(kparms.ctx) , sizeof(tcam_context));
	rtn_val = rhea_channel_tcam_alloc(rhea_id, channel_id, &tcam_id,
	&tcam_context);
	kparms.error_number = rtn_val;

	if (rtn_val) {
		dhea_error("Error in IOCTL_DHEA_TCAM_SLOT_ALLOC.");
		dhea_debug("Leaving: IOCTL_DHEA_TCAM_SLOT_ALLOC.\n");
		copy_to_user(uparms, &kparms, sizeof(kparms));
		return -1;
	}

	kparms.tcam_id = tcam_id;
	copy_to_user(uparms, &kparms, sizeof(kparms));

	dhea_debug("Leaving: IOCTL_DHEA_TCAM_SLOT_ALLOC");

	return 0;
}

int dhea_tcam_set(struct dhea_user *duser, unsigned long arg)
{
	int rtn_val = 0;
	unsigned rhea_id;
	unsigned channel_id;
	unsigned tcam_id;
	struct hea_tcam_setting tcam_setting;
	struct dhea_tcam_set_parms *uparms, kparms;
	dhea_debug("Entering: IOCTL_DHEA_TCAM_SET");
	uparms = (struct dhea_tcam_set_parms *)arg;
	copy_from_user(&kparms, uparms, sizeof(kparms));

	rhea_id = kparms.adapter_id;
	channel_id = kparms.channel_id;
	tcam_id = kparms.tcam_id;

	memcpy(&tcam_setting, &(kparms.tcam_setting), sizeof(tcam_setting));

	rtn_val = rhea_channel_tcam_set(rhea_id, channel_id, tcam_id,
	&tcam_setting);
	kparms.error_number = rtn_val;
	copy_to_user(uparms, &kparms, sizeof(kparms));

	if (rtn_val < 0) {
		dhea_error("Error in IOCTL_DHEA_TCAM_SET.");
		dhea_debug("Leaving: IOCTL_DHEA_TCAM_SET.\n");
		return -1;
	}

	dhea_debug("Leaving: IOCTL_DHEA_TCAM_SET");
	return 0;
}

int dhea_tcam_get(struct dhea_user *duser, unsigned long arg)
{
	int rtn_val = 0;
	unsigned rhea_id;
	unsigned channel_id;
	unsigned tcam_id;
	struct hea_tcam_setting tcam_setting;
	struct dhea_tcam_get_parms *uparms, kparms;
	dhea_debug("Entering: IOCTL_DHEA_TCAM_GET");
	uparms = (struct dhea_tcam_get_parms *)arg;
	copy_from_user(&kparms, uparms, sizeof(kparms));

	rhea_id = kparms.adapter_id;
	channel_id = kparms.channel_id;
	tcam_id = kparms.tcam_id;

	memcpy(&tcam_setting, &(kparms.tcam_setting), sizeof(tcam_setting));

	rtn_val = rhea_channel_tcam_get(rhea_id, channel_id, tcam_id,
	&tcam_setting);

	kparms.error_number = rtn_val;
	memcpy(&(kparms.tcam_setting), &tcam_setting, sizeof(tcam_setting));
	copy_to_user(uparms, &kparms, sizeof(kparms));

	if (rtn_val < 0) {
		dhea_error("Error in IOCTL_DHEA_TCAM_GET.");
		dhea_debug("Leaving: IOCTL_DHEA_TCAM_GET.\n");
		return -1;
	}

	dhea_debug("Leaving: IOCTL_DHEA_TCAM_GET");
	return 0;
}

int dhea_tcam_enable(struct dhea_user *duser, unsigned long arg)
{
	int rtn_val = 0;
	unsigned rhea_id;
	unsigned channel_id;
	unsigned tcam_id;
	unsigned int tcam_offset;
	struct dhea_tcam_enable_parms *uparms, kparms;
	dhea_debug("Entering: IOCTL_DHEA_TCAM_ENABLE");
	uparms = (struct dhea_tcam_enable_parms *)arg;
	copy_from_user(&kparms, uparms, sizeof(kparms));

	rhea_id = kparms.adapter_id;
	channel_id = kparms.channel_id;
	tcam_id = kparms.tcam_id;
	tcam_offset = kparms.tcam_offset;

	rtn_val = rhea_channel_tcam_enable(rhea_id, channel_id,
	tcam_id, tcam_offset);

	kparms.error_number = rtn_val;
	copy_to_user(uparms, &kparms, sizeof(kparms));

	if (rtn_val < 0) {
		dhea_error("Error in IOCTL_DHEA_TCAM_ENABLE.");
		dhea_debug("Leaving: IOCTL_DHEA_TCAM_ENABLE.\n");
		return -1;
	}

	dhea_debug("Leaving: IOCTL_DHEA_TCAM_ENABLE");
	return 0;
}

int dhea_tcam_disable(struct dhea_user *duser, unsigned long arg)
{
	int rtn_val = 0;
	unsigned rhea_id;
	unsigned channel_id;
	unsigned tcam_id;
	unsigned int tcam_offset;
	struct dhea_tcam_disable_parms *uparms, kparms;
	dhea_debug("Entering: IOCTL_DHEA_TCAM_DISABLE");
	uparms = (struct dhea_tcam_disable_parms *)arg;
	copy_from_user(&kparms, uparms, sizeof(kparms));

	rhea_id = kparms.adapter_id;
	channel_id = kparms.channel_id;
	tcam_id = kparms.tcam_id;
	tcam_offset = kparms.tcam_offset;

	rtn_val = rhea_channel_tcam_disable(rhea_id, channel_id,
	tcam_id, tcam_offset);

	kparms.error_number = rtn_val;
	copy_to_user(uparms, &kparms, sizeof(kparms));

	if (rtn_val < 0) {
		dhea_error("Error in IOCTL_DHEA_TCAM_DISABLE.");
		dhea_debug("Leaving: IOCTL_DHEA_TCAM_DISABLE.\n");
		return -1;
	}

	dhea_debug("Leaving: IOCTL_DHEA_TCAM_DISABLE");
	return 0;
}

int dhea_tcam_slot_free(struct dhea_user *duser, unsigned long arg)
{
	int rtn_val = 0;
	unsigned rhea_id;
	unsigned channel_id;
	unsigned tcam_id;

	struct dhea_tcam_enable_parms *uparms, kparms;
	dhea_debug("Entering: IOCTL_DHEA_TCAM_ENABLE");
	uparms = (struct dhea_tcam_enable_parms *)arg;
	copy_from_user(&kparms, uparms, sizeof(kparms));

	rhea_id = kparms.adapter_id;
	channel_id = kparms.channel_id;
	tcam_id = kparms.tcam_id;

	rtn_val = rhea_channel_tcam_free(rhea_id, channel_id, tcam_id);

	kparms.error_number = rtn_val;
	copy_to_user(uparms, &kparms, sizeof(kparms));

	if (rtn_val < 0) {
		dhea_error("Error in IOCTL_DHEA_TCAM_ENABLE.");
		dhea_debug("Leaving: IOCTL_DHEA_TCAM_ENABLE.\n");
		return -1;
	}

	dhea_debug("Leaving: IOCTL_DHEA_TCAM_ENABLE");
	return 0;
}

int dhea_mac_loopback_enable(struct dhea_user *duser, unsigned long arg)
{
	int rtn_val = 0;
	struct dhea_mac_loopback_parms *uparms, kparms;
	dhea_debug("Leaving: IOCTL_DHEA_MAC_LOOPBACK_ENABLE");

	uparms = (struct dhea_mac_loopback_parms *)arg;
	copy_from_user(&kparms, uparms, sizeof(kparms));
	duser->enable_mac_loopback = 1;

	kparms.error_number = rtn_val;
	copy_to_user(uparms, &kparms, sizeof(kparms));

	dhea_debug("Leaving: IOCTL_DHEA_MAC_LOOPBACK_ENABLE");
	return 0;
}

int dhea_mac_loopback_disable(struct dhea_user *duser, unsigned long arg)
{
	int rtn_val = 0;
	struct dhea_mac_loopback_parms *uparms, kparms;
	dhea_debug("Leaving: IOCTL_DHEA_MAC_LOOPBACK_DISABLE");

	uparms = (struct dhea_mac_loopback_parms *)arg;
	copy_from_user(&kparms, uparms, sizeof(kparms));
	duser->enable_mac_loopback = 0;

	kparms.error_number = rtn_val;
	copy_to_user(uparms, &kparms, sizeof(kparms));

	dhea_debug("Leaving: IOCTL_DHEA_MAC_LOOPBACK_DISABLE");
	return 0;
}

int dhea_channel_feature_set(struct dhea_user *duser, unsigned long arg)
{
	int rtn_val = 0;
	struct dhea_channel_feature_set_parms *uparms, kparms;
	uparms = (struct dhea_channel_feature_set_parms *)arg;
	copy_from_user(&kparms, uparms, sizeof(kparms));

	rtn_val = rhea_channel_feature_set(kparms.adapter_id,
	kparms.channel_id, kparms.feature, kparms.value);

	kparms.error_number = rtn_val;
	copy_to_user(uparms, &kparms, sizeof(kparms));

	if (rtn_val < 0) {
		dhea_error("Error in IOCTL_DHEA_CHANNEL_FEATURE_SET.\n");
		dhea_debug("Leaving: IOCTL_DHEA_CHANNEL_FEATURE_SET.\n");
		return -1;
	}

	dhea_debug("Leaving: IOCTL_DHEA_CHANNEL_FEATURE_SET");
	return 0;
}

int dhea_channel_feature_get(struct dhea_user *duser, unsigned long arg)
{
	int rtn_val = 0;
	struct dhea_channel_feature_get_parms *uparms, kparms;
	unsigned long long index = 0LLU;

	uparms = (struct dhea_channel_feature_get_parms *)arg;
	copy_from_user(&kparms, uparms, sizeof(kparms));
	index = kparms.index;

	rtn_val = rhea_channel_feature_get(kparms.adapter_id, kparms.channel_id,
	kparms.feature, &(index));

	kparms.value = index;
	kparms.error_number = rtn_val;
	copy_to_user(uparms, &kparms, sizeof(kparms));

	if (rtn_val < 0) {
		dhea_error("Error in IOCTL_DHEA_CHANNEL_FEATURE_GET.\n");
		dhea_debug("Leaving: IOCTL_DHEA_CHANNEL_FEATURE_GET.\n");
		return -1;
	}

	dhea_debug("Leaving: IOCTL_DHEA_CHANNEL_FEATURE_GET");
	return 0;
}

void add_adapter_to_head(struct adapter_res *head,
	struct adapter_res *new_adapter)
{
	dhea_debug("Adding an adapter with ID %u to head.\n", new_adapter->id);
	new_adapter->next = head->next;
	head->next = new_adapter;
}

struct adapter_res *delete_adapter_element(struct adapter_res *head,
	unsigned adapter_id)
{
	struct adapter_res *prev, *curr;

	for (prev = head, curr = head->next; curr != NULL; prev = curr,
		curr = curr->next) {
		if (curr->id == adapter_id) {
			prev->next = curr->next;
			dhea_debug("Removed adapter session with ID %d.\n",
			curr->id);
			return curr;
		}
	}

	return NULL;
}

struct adapter_res *find_adapter_element(struct adapter_res *head,
	unsigned adapter_id)
{
	struct adapter_res *prev, *curr;

	for (prev = head, curr = head->next; curr != NULL; prev = curr,
		curr = curr->next) {
		if (curr->id == adapter_id) {
			dhea_debug("Found adapter with id %u.\n", curr->id);
			return curr;
		}
	}

	dhea_debug("Adapter list contains no element with id %u.\n",
	adapter_id);

	return NULL;
}

struct adapter_res *find_adapter_element_new(struct dhea_user *duser,
unsigned adapter_id)
{
	struct list_head *position = NULL;
	struct adapter_res *curr;

	list_for_each(position, &(duser->adapter_list_head)) {
		curr = list_entry(position, struct adapter_res, my_list);
		if (curr->id == adapter_id) {
			dhea_debug("Found adapter with id %u.\n", curr->id);
			return curr;
		}
	}

	dhea_debug("Adapter list contains no element with id %u.\n",
	adapter_id);
	return NULL;
}

void add_channel_to_head(struct channel_res *head,
	struct channel_res *new_channel)
{
	dhea_debug("Adding a channel with ID %u to head.\n",
	new_channel->channel_id);
	new_channel->next = head->next;
	head->next = new_channel;
}

struct channel_res *delete_channel_element(struct channel_res *head,
	unsigned channel_id)
{
	struct channel_res *prev, *curr;

	for (prev = head, curr = head->next; curr != NULL; prev = curr,
		curr = curr->next) {
		if (curr->channel_id == channel_id) {
			prev->next = curr->next;
			dhea_debug("Removed channel with ID %d.\n",
			curr->channel_id);
			return curr;
		}
	}

	return NULL;
}

struct channel_res *find_channel_element(struct channel_res *head,
	unsigned channel_id)
{
	struct channel_res *prev, *curr;

	for (prev = head, curr = head->next; curr != NULL; prev = curr,
		curr = curr->next) {
		if (curr->channel_id == channel_id) {
			dhea_debug("Found channel with ID %u.\n",
			curr->channel_id);
			return curr;
		}
	}

	dhea_debug("Channel list contains no element with ID %u.\n",
	channel_id);
	return NULL;
}

void add_eq_to_head(struct eq_res *head, struct eq_res *new_eq)
{
	dhea_debug("Adding an EQ with ID %u to head.\n", new_eq->eq_id);
	new_eq->next = head->next;
	head->next = new_eq;
}

struct eq_res *delete_eq_element(struct eq_res *head, unsigned eq_id)
{
	struct eq_res *prev, *curr;

	for (prev = head, curr = head->next; curr != NULL; prev = curr,
		curr = curr->next) {
		if (curr->eq_id == eq_id) {
			prev->next = curr->next;
			dhea_debug("Removed EQ with ID %d.\n", eq_id);
			return curr;
		}
	}

	return NULL;
}

struct eq_res *find_eq_element(struct eq_res *head, unsigned eq_id)
{
	struct eq_res *prev, *curr;

	for (prev = head, curr = head->next; curr != NULL; prev = curr,
		curr = curr->next) {
		if (curr->eq_id == eq_id) {
			dhea_debug("Found EQ with ID %u.\n", curr->eq_id);
			return curr;
		}
	}

	dhea_debug("EQ list contains no element with ID %u.\n", eq_id);
	return NULL;
}

void add_cq_to_head(struct cq_res *head, struct cq_res *new_cq)
{
	dhea_debug("Adding a CQ with ID %u to head.\n", new_cq->cq_id);
	new_cq->next = head->next;
	head->next = new_cq;
}

struct cq_res *delete_cq_element(struct cq_res *head, unsigned cq_id)
{
	struct cq_res *prev, *curr;

	for (prev = head, curr = head->next; curr != NULL; prev = curr,
		curr = curr->next) {
		if (curr->cq_id == cq_id) {
			prev->next = curr->next;
			dhea_debug("Removed CQ with ID %d.\n", cq_id);
			return curr;
		}
	}

	return NULL;
}

struct cq_res *find_cq_element(struct cq_res *head, unsigned cq_id)
{
	struct cq_res *prev, *curr;

	for (prev = head, curr = head->next; curr != NULL; prev = curr,
		curr = curr->next) {
		if (curr->cq_id == cq_id) {
			dhea_debug("Found CQ with ID %u.\n", curr->cq_id);
			return curr;
		}
	}

	dhea_debug("CQ list contains no element with ID %u.\n", cq_id);
	return NULL;
}

void add_qp_to_head(struct qp_res *head, struct qp_res *new_qp)
{
	dhea_debug("Adding a QP with ID %u to head.\n", new_qp->qp_id);
	new_qp->next = head->next;
	head->next = new_qp;
}

struct qp_res *delete_qp_element(struct qp_res *head, unsigned qp_id)
{
	struct qp_res *prev, *curr;

	for (prev = head, curr = head->next; curr != NULL; prev = curr,
		curr = curr->next) {
		if (curr->qp_id == qp_id) {
			prev->next = curr->next;
			dhea_debug("Removed QP with ID %d.\n", qp_id);
			return curr;
		}
	}

	return NULL;
}

struct qp_res *find_qp_element(struct qp_res *head, unsigned qp_id)
{
	struct qp_res *prev, *curr;

	for (prev = head, curr = head->next; curr != NULL; prev = curr,
		curr = curr->next) {
		if (curr->qp_id == qp_id) {
			dhea_debug("Found QP with ID %u.\n", curr->qp_id);
			return curr;
		}
	}

	dhea_debug("QP list contains no element with ID %u.\n", qp_id);
	return NULL;
}

module_init(dhea_init_module);
module_exit(dhea_cleanup_module);

MODULE_AUTHOR("Davide Pasetto <pasetto_davide@ie.ibm.com>");
MODULE_AUTHOR("Karol Lynch <karol_lynch@ie.ibm.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("User-space interface to the HEA.");


