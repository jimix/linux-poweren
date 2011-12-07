/*
 * Copyright (C) 2011, 2012 IBM Corporation.
 * Author:	Davide Pasetto <pasetto_davide@ie.ibm.com>
 *		Karol Lynch <karol_lynch@ie.ibm.com>
 *		Kay Muller <kay.muller@ie.ibm.com>
 *		Jimi Xenidis <jimix@watson.ibm.com>
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

#include <linux/etherdevice.h>
#include <linux/list.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/memory.h>
#include <linux/kexec.h>
#include <linux/mutex.h>
#include <linux/utsname.h>

#include <linux/of_device.h>
#include <linux/of_platform.h>

#include <linux/firmware.h>

#include <platforms/wsp/copro/cop.h>
#include <platforms/wsp/copro/pbic.h>

#include <asm/poweren_hea_common_types.h>

#include <rhea-interface.h>
#include <rhea.h>
#include <rhea-funcs.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Davide Pasetto <pasetto_davide@ie.ibm.com>");
MODULE_AUTHOR("Kay Muller <kay.muller@ie.ibm.com>");
MODULE_DESCRIPTION("IBM rHEA Driver");

static struct of_device_id rhea_device_table[] = {
	{
	 .compatible = RHEA_OF_ADAPTER_COMPAT,
	 },
	{},
};

MODULE_DEVICE_TABLE(of, rhea_device_table);

spinlock_t rhea_adapters_lock;

static int __devexit rhea_remove(struct platform_device *dev)
{
	struct hea_adapter *ap = dev_get_drvdata(&dev->dev);

	int instance = ap->instance;

	rhea_debug("rhea_remove() - begin (%d)", instance);

	spin_lock(&rhea_adapters_lock);

	rhea_adapter_fini(ap);

	rhea_free(ap, sizeof(*ap));

	spin_unlock(&rhea_adapters_lock);

	rhea_debug("rhea_remove() - end");
	return 0;
}

static int __devinit rhea_probe_adapter(struct platform_device *dev)
{
	int err = -ENODEV;
	struct hea_adapter *ap = NULL;

	if (!dev || !dev->dev.of_node) {
		rhea_error("Invalid of_hea device probed");
		err = -EINVAL;
		goto out;
	}

	spin_lock(&rhea_adapters_lock);

	ap = rhea_alloc(sizeof(*ap), 0);
	if (NULL == ap) {
		rhea_error("Not able to allocate adapter memory");
		err = -ENOMEM;
		goto out;
	}

	err = rhea_discover_adapter(&dev->dev, ap);
	if (err)
		goto out;


	err = rhea_adapter_init(ap);
	if (err) {
		rhea_error("rhea_init failed");
		goto out;
	}

	dev_set_drvdata(&dev->dev, ap);

	spin_unlock(&rhea_adapters_lock);

	return err;

out:
	if (ap)
		rhea_free(ap, sizeof(*ap));

	spin_unlock(&rhea_adapters_lock);

	return err;
}

static struct platform_driver rhea_driver = {
	.driver = {
		   .name = DRV_NAME,
		   .owner = THIS_MODULE,
		   .of_match_table = rhea_device_table,
		   },
	.probe = rhea_probe_adapter,
	.remove = rhea_remove,
};

static int rhea_reboot_notifier(struct notifier_block *nb,
				unsigned long action, void *unused)
{
	if (action == SYS_RESTART)
		platform_driver_unregister(&rhea_driver);
	return NOTIFY_DONE;
}

static struct notifier_block rhea_reboot_nb = {
	.notifier_call = rhea_reboot_notifier,
};


static int __init rhea_module_init(void)
{
	struct new_utsname *uts = utsname();
	struct device_node *dn;
	int err;

	rhea_info("IBM rhea platform device driver (%s %s)", uts->release,
		uts->version);

	spin_lock_init(&rhea_adapters_lock);

	dn = of_find_compatible_node(NULL, NULL, RHEA_OF_ADAPTER_COMPAT);
	if (NULL == dn) {
		rhea_error("%s: no compatible adapters found!", __func__);
		return -ENODEV;
	}

	err = register_reboot_notifier(&rhea_reboot_nb);
	if (err) {
		rhea_error("failed registering reboot notifier");
		return err;
	}

	err = platform_driver_probe(&rhea_driver, rhea_probe_adapter);
	if (err) {
		rhea_error("failed probing rhea: %i", err);
		goto out;
	}

	return 0;
out:

	unregister_reboot_notifier(&rhea_reboot_nb);
	return err;
}


static void __exit rhea_exit(void)
{
	flush_scheduled_work();
	platform_driver_unregister(&rhea_driver);
	unregister_reboot_notifier(&rhea_reboot_nb);
}

#ifndef MODULE
/* if we are not a module, then we need to be part of the subsystem */
subsys_initcall(rhea_module_init);
#else
module_init(rhea_module_init);
#endif

module_exit(rhea_exit);
