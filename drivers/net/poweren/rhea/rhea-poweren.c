/*
 * Copyright (C) 2011 IBM Corporation.
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

#include <rhea-poweren.h>

#include <asm/poweren_hea_channel.h>

#include <linux/firmware.h>
#include <asm/wsp.h>

#include <linux/etherdevice.h>
#include "rhea-funcs.h"

static const u8 *get_dn_mac_addr(struct device_node *dn)
{
	const u8 *p;

	p = of_get_property(dn, "local-mac-address", NULL);
	if (!p || !is_valid_ether_addr(p)) {
		p = of_get_property(dn, "mac-address", NULL);

		if (!p || !is_valid_ether_addr(p))
			return NULL;
	}

	return p;
}

int rhea_discover_adapter(struct device *dev, struct hea_adapter *ap)
{
	const u32 *prop32;
	int len, instance = 0;
	int err;
	int pport_nr;
	struct resource gba;
	const u8 *p;
	union hea_mac_addr the_mac_address_prev = {0};
	int is_mac_diff_4 = 1;
	union hea_mac_addr the_mac_address = { 0 };
	struct device_node *pport_node;
	enum hea_speed dt_speed;
	struct hea_pport_cfg *pport;
	struct device_node *dn;

	rhea_debug("rhea_core_setup() - begin");

	/* get device node */
	dn = dev->of_node;

	err = 0;

	if (of_address_to_resource(dn, 0, &gba)) {
		pr_err("%s: error parsing adapter address", __func__);
		return -ENXIO;
	}

	rhea_debug("rhea_core_setup() - initializing adapter %d", instance);

	ap->gba = gba.start;
	ap->max_region_size = resource_size(&gba);
	snprintf(ap->name, sizeof(ap->name), DRV_NAME "%d", instance);

#ifdef CONFIG_NUMA
	ap->instance = dev->numa_node;
#else
	ap->instance = 0;
#endif /* CONFIG_NUMA */

	/* get IRQ range */
	prop32 = of_get_property(dn, "interrupt-ranges", &len);
	if (!prop32 || len < (2 * sizeof(u32))) {
		rhea_error("No/bad interrupt-ranges found on %s",
			   dn->full_name);
		return -ENOENT;
	}

	if (len > (2 * sizeof(u32))) {
		rhea_error("Multiple ranges not supported.");
		return -EINVAL;
	}

	ap->hwirq_base = of_read_number(prop32, 1);
	ap->hwirq_count = of_read_number(prop32 + 1, 1);
	ap->dn = dn;

	/* get parent pbic */
	ap->mmu.parent_pbic = pbic_get_parent_pbic(dn);

	/* probe physical ports */
	p = get_dn_mac_addr(dn);
	if (p)
		memcpy(the_mac_address.sa.addr, p, ETH_ALEN);
	else {
		rhea_debug("no mac-address found %s", dn->full_name);

		the_mac_address._be64 = 0x0ULL;
	}

	if (ap->pport_count) {
		rhea_error("Ports are already configured");
		return 0;
	}

	for_each_child_of_node(dn, pport_node) {

		if (!on_mambo() &&
		    !of_device_is_compatible(pport_node, RHEA_OF_PORT_COMPAT))
			continue;

		pport_nr = 0;
		prop32 = of_get_property(pport_node, "reg", NULL);
		if (prop32)
			pport_nr = *prop32 - 1;

		if (!is_hea_pport(pport_nr)) {
			rhea_error("bad pport number %d", pport_nr + 1);
			return -ENXIO;
		}

		rhea_debug("detected port %d", pport_nr + 1);

		/* count number of physical ports */
		ap->pport_count++;

		/* get port instance */
		pport = &ap->pports[pport_nr];

		p = get_dn_mac_addr(pport_node);
		if (p) {
			memcpy(pport->mac_address.sa.addr, p, ETH_ALEN);

			/* find difference between two physical ports */
			is_mac_diff_4 = (is_mac_diff_4 &&
					4 <= pport->mac_address._be64 -
					the_mac_address_prev._be64) ?
						1 : 0;

			/* Save current MAC address */
			the_mac_address_prev._be64 = pport->mac_address._be64;

			rhea_debug("Found MAC for pport %u "
				  "%02x:%02x:%02x:%02x:%02x:%02x",
				  pport_nr + 1,
				  pport->mac_address.sa.addr[0],
				  pport->mac_address.sa.addr[1],
				  pport->mac_address.sa.addr[2],
				  pport->mac_address.sa.addr[3],
				  pport->mac_address.sa.addr[4],
				  pport->mac_address.sa.addr[5]);
		} else {
			rhea_warning("! no mac-address found, faking %s",
				    pport_node->full_name);

			WARN_ON(!is_valid_ether_addr(the_mac_address.sa.addr));

			is_mac_diff_4 = 0;

			pport->mac_address._be64 = the_mac_address._be64;
			pport->mac_address._be64 += pport_nr *
						    16 * ap->instance;
			pport->mac_address._be64 += pport_nr * 4;
		}

		pport->ap = ap;
		pport->pport_nr = pport_nr;

		/* Current firmware sets "max-speed", not "port-speed" */
		prop32 = of_get_property(pport_node, "port-speed", NULL);
		if (!prop32)
			prop32 = of_get_property(pport_node, "max-speed",
						 NULL);

		if (!prop32) {
			rhea_error("property \"port-speed\" "
				   "assuming 1G ports");
			dt_speed = HEA_SPEED_1G;
		} else
			dt_speed = *prop32;

		/* saves detected speed */
		pport->speed_dt = dt_speed;

		p = of_get_property(pport_node, "phy-connection-type", NULL);

		if (p && !strcasecmp(p, "xaui"))
			pport->conn_type = HEA_PHY_XAUI;
		else
			pport->conn_type = HEA_PHY_SGMII;
	}

	if (is_mac_diff_4) {
		int i;
		for (i = 0; i < HEA_MAX_PPORT_COUNT; ++i)
			ap->pports[i].mac_lport_count_max = 4;
	} else {
		int i;
		for (i = 0; i < HEA_MAX_PPORT_COUNT; ++i)
			ap->pports[i].mac_lport_count_max = 1;

		rhea_warning("WARNING: This system was not equipped with enough"
			     " MAC addresses to use more than 1 logical port!");
		rhea_warning("WARNING: All logical ports apart from "
			     "logical port 0 are DISABLED");
		rhea_warning("!! Please contact your system administrator !!");
	}

	return 0;
}
