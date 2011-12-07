/*
 * Copyright (C) 2011, 2012 IBM Corporation.
 * Author:	Davide Pasetto <pasetto_davide@ie.ibm.com>
 *			Karol Lynch <karol_lynch@ie.ibm.com>
 *			Kay Muller <kay.muller@ie.ibm.com>
 *			Jimi Xenidis <jimix@watson.ibm.com>
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


#ifndef _NET_POWEREN_INTERFACE_H_
#define _NET_POWEREN_INTERFACE_H_

#include <linux/types.h>

#include <asm/poweren_hea_common_types.h>
#include <asm/poweren_hea_cq.h>
#include <asm/poweren_hea_eq.h>
#include <asm/poweren_hea_qp.h>

#include <hea-channel-regs.h>

#define RHEA_MAJOR_VERSION 3
#define RHEA_MINOR_VERSION 0
#define RHEA_RELEASE_VERSION 1

/**********************************/
/****** Module Load and Unload ****/
/**********************************/

/*
 * This function initialises the HEA and allocates resources
 * (for QP, EQ, CQ, ...) which are required to run the HEA.
 * Furthermore it is loading the HEA ruleset.
 *
 * This function is called once at the startup of the HEA
 * They will initialise all adapters which are available in the system.
 *
 * @return 0 on success, negative error-code on failure
 */
extern int rhea_adapter_init(struct hea_adapter *ap);

/*
 * This function is called once unloading of the r_hea module and it is
 * putting the HEA back into its initial state.
 * It is deallocating all resources.
 *
 * @return 0 on success, negative error-code on failure
 */
extern int rhea_adapter_fini(struct hea_adapter *ap);

/*
 * This function returns the RHEA version.
 * @return 0 on success, negative error-code on failure
 */
extern int rhea_get_version(unsigned int *major, unsigned int *minor,
			    unsigned int *release);

/**********************************/
/****** HEA Adapter Handling ******/
/**********************************/

/*
 * @return Returns number of HEA adapters in the system
 * (important for multi-chip environment
 */
extern unsigned rhea_adapter_count(void);

/*
 * Functions which returns adapter configuration information for the
 * adapter with the adapter_number.
 *
 * @param[in]	adapter_number Parameter which specifies the
 *		adapter/node number
 * @param[out] ap Pointer to instance of adapter struct
 */
extern int rhea_adapter_get(unsigned adapter_number,
			    struct hea_adapter *ap);

/*
 * This function is creating a rhea instance, which can be used
 * by a process to access. It is important to specify, whether
 * this instance is going to use NN or EP!
 *
 * @param[out]  rhea_id          Returns the HEA identifier
 * @param[in]   adapter_number  Specifies the HEA adapter which
 *                              should be targeted
 * @return 0 on success, negative error-code on failure
 */
extern int rhea_session_init(unsigned *rhea_id,
			     unsigned adapter_number);

/*
 * This function is making sure that all resources which are
 * allocated for this instances have been destroyed correctly!
 *
 * Furthermore it has to check whether all instances
 * (e.g. CQ, EQ, ...) have been destroyed
 *
 * @param[in] rhea_id    HEA identifier
 * @return 0 on success, negative error-code on failure
 */
extern int rhea_session_fini(unsigned rhea_id);

extern int rhea_gen_dumps(unsigned rhea_id);

/******************************/
/****** Channel Handling ******/
/******************************/

/*
 * This function is the number of available physical ports
 *
 * @param[in]   rhea_id          HEA identifier
 * @return Number of available ports per instance
 */
extern unsigned rhea_pport_count(unsigned rhea_id);

/*
 * This function is reserving a specific channel inside a physical
 * port
 *
 * @param[in]   rhea_id          HEA identifier
 * @param[out]  channel_id      Channel ID
 * @param[in]   pport_id        Physical port ID
 * @param[in]   context_channel   Describes which channel to reserve
 * @return 0 on success, negative error-code on failure
 */
extern int rhea_channel_alloc(unsigned rhea_id,
			      unsigned *channel_id,
			      struct hea_channel_context *context_channel);

/*
 * This function frees up the logical port
 *
 * @param[in]   rhea_id          HEA identifier
 * @param[in]   pport_id        Physical port ID
 * @param[in]   channel_id      Channel ID
 * @return 0 on success, negative error-code on failure
 */
extern int rhea_channel_free(unsigned rhea_id, unsigned channel_id);

/*
 * Set MAC address for physical port channel.
 *
 * @param[in]   rhea_id          HEA identifier
 * @param[in]   channel_id      Channel ID
 * @param[in]   mac_address     Value for the MAC address
 * @return 0 on success, negative error-code on failure
 */
extern int rhea_channel_macaddr_set(unsigned rhea_id,
				    unsigned channel_id,
				    union hea_mac_addr mac_address);

/*
 * Get MAC address for physical port channel.
 *
 * @param[in]   rhea_id          HEA identifier
 * @param[in]   channel_id      Channel ID
 * @return MAC address for physical port channel
 */
extern int rhea_channel_macaddr_get(unsigned rhea_id,
				    unsigned channel_id,
				    union hea_mac_addr *mac_address);

/*
 * Turns the physical port channel on
 *
 * @param[in]   rhea_id          HEA identifier
 * @param[in]   channel_id      Channel ID
 * @return 0 on success, negative error-code on failure
 */
extern int rhea_channel_enable(unsigned rhea_id,
			       unsigned channel_id);

/*
 * Turns the physical port channel down
 *
 * @param[in]   rhea_id          HEA identifier
 * @param[in]   channel_id      Channel ID
 * @return 0 on success, negative error-code on failure
 */
extern int rhea_channel_disable(unsigned rhea_id,
				unsigned channel_id);

/*
 * Returns register values of port registers
 *
 * @param[in]   rhea_id          HEA identifier
 * @param[in]   channel_id      Channel ID
 * @param[in]	counter_type	Specifies the type of counters
 *				(error, lport, histogramm, etc.)
 * @param[in]	counter		Pointer to structure which will contain
 *				the values
 * @return 0 on success, negative error-code on failure
 */
extern int rhea_channel_counters_get(unsigned rhea_id,
				     unsigned channel_id,
				     enum hea_channel_counter_type
				     counter_type,
				     struct hea_channel_counters *counter);

/****************************/
/****** Queue Handling ******/
/****************************/

/*
 * This function allocates EQ resources for eq_id
 *
 * @param[in]   rhea_id          HEA identifier
 * @param[out]  eq_id           EQ ID
 * @param[in]   context_eq      List of EQ features
 * @return 0 on success, negative error-code on failure
 */
extern int rhea_eq_alloc(unsigned rhea_id,
			 unsigned *eq_id,
			 struct hea_eq_context *context_eq);

extern int rhea_eq_dumps(unsigned rhea_id, unsigned eq_id);

/*
 * Frees all EQ resources for eq_id
 *
 * @param[in]   rhea_id          HEA identifier
 * @param[out]  eq_id           EQ ID
 * @return 0 on success, negative error-code on failure
 */
extern int rhea_eq_free(unsigned rhea_id, unsigned eq_id);

extern int rhea_eq_table(unsigned rhea_id,
			 unsigned eq_id,
			 struct hea_eqe **eqes,
			 unsigned *eqe_size, unsigned *eqe_count);

extern int rhea_eq_mapinfo(unsigned rhea_id,
			   unsigned eq_id,
			   enum hea_priv_mode priv,
			   void **pointer, unsigned *size, unsigned use_va);

/*
 * This function enables or disables certain features for each event queue
 *
 * @param[in]   rhea_id          HEA identifier
 * @param[in]   eq_id           EQ ID
 * @param[in]   feature         Specifies the feature which this function
 *				wants to set
 * @param[in]   value           Specifies the value the feature should be set to
 * @return 0 on success, negative error-code on failure
 */
extern int rhea_eq_set(unsigned rhea_id,
		       unsigned eq_id,
		       enum hea_eq_feature_set feature,
		       u64 value);

/*
 * This function returns the value of a certain feature of the event queue
 *
 * @param[in]   rhea_id          HEA identifier
 * @param[in]   eq_id           EQ ID
 * @param[in]   feature         Specifies the feature which this function
 *				wants to set
 * @param[in]   value           Holds the written back value
 * @return 0 on success, negative error-code on failure
 */
extern int rhea_eq_get(unsigned rhea_id,
		       unsigned eq_id,
		       enum hea_eq_feature_get feature,
		       u64 *value);

extern int rhea_interrupt_setup(unsigned rhea_id,
				unsigned eq_id,
				hea_irq_handler_t irq_handler,
				void *irq_handler_args);

extern void rhea_interrupt_free(unsigned rhea_id, unsigned eq_id);

/*
 * This function allocates CQ resources for cq_id
 *
 * @param[in]   rhea_id          HEA identifier
 * @param[out]  cq_id           CQ ID
 * @param[in]   context_cq      List of CQ features
 * @return 0 on success, negative error-code on failure
 */
extern int rhea_cq_alloc(unsigned rhea_id,
			 unsigned *cq_id,
			 struct hea_cq_context *context_cq);

extern int rhea_cq_dumps(unsigned rhea_id, unsigned cq_id);

/*
 * This function frees CQ resources for cq_id
 *
 * @param[in]   rhea_id          HEA identifier
 * @param[in]   cq_id           CQ ID
 * @return 0 on success, negative error-code on failure
 */
extern int rhea_cq_free(unsigned rhea_id, unsigned cq_id);

/*
 * This function enables or disables certain features for each completion queue
 *
 * @param[in]   rhea_id          HEA identifier
 * @param[in]   cq_id           CQ ID
 * @param[in]   feature         Specifies the feature which this function
 *				wants to set
 * @param[in]   value           Specifies the value the feature should be set to
 * @return 0 on success, negative error-code on failure
 */
int rhea_cq_set(unsigned rhea_id,
		unsigned cq_id,
		enum hea_cq_feature_set feature, u64 value);

/*
 * This function returns the value of a certain feature of the completion queue
 *
 * @param[in]   rhea_id          HEA identifier
 * @param[in]   cq_id           CQ ID
 * @param[in]   feature         Specifies the feature which this function
 *				wants to set
 * @param[in]   value           Holds the written back value
 * @return 0 on success, negative error-code on failure
 */
extern int rhea_cq_get(unsigned rhea_id,
		       unsigned cq_id,
		       enum hea_cq_feature_get feature,
		       u64 *value);

/*
 * This function returns the pointer to the CQ table
 */
extern int rhea_cq_table(unsigned rhea_id,
			 unsigned cq_id,
			 struct hea_cqe **cqes,
			 unsigned *cqe_size, unsigned *cqe_count);

/*
 * Gives access to CQ registers
 */
extern int rhea_cq_mapinfo(unsigned rhea_id,
			   unsigned cq_id,
			   enum hea_priv_mode priv,
			   void **pointer, unsigned *size, unsigned use_va);


/*
 * Allocates a new QP and its resources
 *
 * @param[in]   rhea_id          HEA identifier
 * @param[out]  qp_id           QP ID
 * @param[in]   context_qp      List of QP features
 * @return 0 on success, negative error-code on failure
 */
extern int rhea_qp_alloc(unsigned rhea_id,
			 unsigned *qp_id,
			 struct hea_qp_context *context_qp);

extern int rhea_qp_dumps(unsigned rhea_id, unsigned qp_id);

union snd_wqe;
extern int rhea_sq_table(unsigned rhea_id,
			 unsigned qp_id,
			 union snd_wqe **wqes,
			 unsigned *wqe_size, unsigned *wqe_count);

extern int rhea_qp_mapinfo(unsigned rhea_id,
			   unsigned qp_id,
			   enum hea_priv_mode priv,
			   void **pointer, unsigned *size, unsigned use_va);

union rcv_wqe;
extern int rhea_rq_table(unsigned rhea_id,
			 unsigned qp_id,
			 unsigned rq_nr,
			 union rcv_wqe **wqes,
			 unsigned *wqe_size, unsigned *wqe_count);

/*
 * Frees a QP and its resources
 *
 * @param[in]   rhea_id          HEA identifier
 * @param[out]  qp_id           QP ID
 * @return 0 on success, negative error-code on failure
 */
extern int rhea_qp_free(unsigned rhea_id, unsigned qp_id);

/*
 * This function enables or disables certain features for each queue pair
 *
 * @param[in]   rhea_id          HEA identifier
 * @param[in]   qp_id           QP ID
 * @param[in]   feature         Specifies the feature which this
 *				function wants to set
 * @param[in]   value           Specifies the value the feature should be set to
 * @return 0 on success, negative error-code on failure
 */
extern int rhea_qp_set(unsigned rhea_id,
		       unsigned qp_id,
		       enum hea_qp_feature_set feature,
		       u64 value);

/*
 * This function returns the value of a certain feature
 *
 * @param[in]   rhea_id          HEA identifier
 * @param[in]   qp_id           QP ID
 * @param[in]   feature         Specifies the feature which this
 *				function wants to set
 * @param[in]   value           Holds the written back value
 * @return 0 on success, negative error-code on failure
 */
extern int rhea_qp_get(unsigned rhea_id,
		       unsigned qp_id,
		       enum hea_qp_feature_get feature,
		       u64 *value);

/*
 * Turns the QP on
 *
 * @param[in]   rhea_id          HEA identifier
 * @param[in]   qp_id           QP ID
 * @return 0 on success, negative error-code on failure
 */
extern int rhea_qp_up(unsigned rhea_id, unsigned qp_id);

/*
 * Turns the QP down
 *
 * @param[in]   rhea_id          HEA identifier
 * @param[in]   qp_id           QP ID
 * @return 0 on success, negative error-code on failure
 */
extern int rhea_qp_down(unsigned rhea_id, unsigned qp_id);

/******************************/
/****** QP Determination ******/
/******************************/

extern int rhea_channel_feature_set(unsigned rhea_id,
				    unsigned channel_id,
				    enum hea_channel_feature_set feature,
				    u64 value);

extern int rhea_channel_feature_get(unsigned rhea_id,
				    unsigned channel_id,
				    enum hea_channel_feature_get feature,
				    u64 *value);

/* Note that 2^num_tcam_or_hash_bits <= num_slots */

/*
 * Allocates a number of QPN slots
 *
 * @param[in]   rhea_id          HEA identifier
 * @param[in]   pport_id        Physical Port ID
 * @param[out]  slot_base       Slot base identifier
 * @param[in]   number          Number of QPN slots
 *                              (has to be either: 1,2,4,8,16,32)
 * @return 0 on success, negative error-code on failure
 */
extern int rhea_channel_qpn_alloc(unsigned rhea_id,
				  unsigned channel_id,
				  struct hea_qpn_context *qpn_context);

/*
 * Deallocates a number of QPN slots
 *
 * @param[in]   rhea_id          HEA identifier
 * @param[in]   pport_id        Physical Port ID
 * @param[in]   slot_base       Slot base identifier
 * @return 0 on success, negative error-code on failure
 */
extern int rhea_channel_qpn_free(unsigned rhea_id,
				 unsigned channel_id);

extern int rhea_channel_qpn_share(unsigned rhea_id,
				  unsigned target_channel_id,
				  unsigned source_channel_id);

/*
 * Queries the port to find largest contiguous QPN slot
 *
 * @param[in]   rhea_id         HEA identifier
 * @param[in]   pport_id        Physical Port ID
 * @param[out]  num_free        Size of the largest free QPN block
 * @return 0 on success, negative error-code on failure
 */
extern int rhea_channel_qpn_query(unsigned rhea_id,
				  unsigned channel_id, int *num_free);

/*
 * Wires QPN to QP
 *
 * @param[in]	  rhea_id	HEA identifier
 * @param[in]	 qp_id		Specifies the QP which is
 *				getting associated with the QPN
 * @param[in]	 qpn_offset	user has to compute the offset
 *				(number of available slots
 *				- offset (offset >= 0))
 * @return 0 on success, negative error-code on failure
 */
extern int rhea_channel_wire_qpn_to_qp(unsigned rhea_id,
				       unsigned channel_id,
				       unsigned qp_id,
				       unsigned qpn_offset);

/*
 * Allocates the TCAM slot of the physical port (channel_id)
 *
 * This function only works if the hasher is not used by another
 * rhea session (rhea_id).
 *
 * @param[in]   rhea_id         HEA identifier
 * @param[out]   tcam_id        TCAM Slot ID
 * @param[in]	slot_count	Number of slots which the caller would
 *				like to get
 * @param[in]   channel_id      Channel ID
 * @return 0 on success, negative error-code on failure
 */
extern int rhea_channel_tcam_alloc(unsigned rhea_id,
				   unsigned channel_id,
				   unsigned *tcam_id,
				   struct hea_tcam_context *tcam_context);

/*
 * Allocates the hasher
 *
 * @param[in]   rhea_id         HEA identifier
 * @param[in]   channel_id      Channel ID
 * @return 0 on success, negative error-code on failure
 */
extern int rhea_channel_hasher_alloc(unsigned rhea_id, unsigned channel_id);

/*
 * Frees hasher from channel
 *
 * @param[in]   rhea_id         HEA identifier
 * @param[in]   channel_id      Channel ID
 * @return 0 on success, negative error-code on failure
 */
extern int rhea_channel_hasher_free(unsigned rhea_id, unsigned channel_id);


/*
 * Deallocates a TCAM slot.
 *
 * @param[in]   rhea_id          HEA identifier
 * @param[in]   tcam_id         TCAM Slot ID
 * @param[in]   pport_id        Physical Port ID
 * @return 0 on success, negative error-code on failure
 */
extern int rhea_channel_tcam_free(unsigned rhea_id,
				  unsigned channel_id,
				  unsigned tcam_id);

/*
 * Sets the value of a a specific TCAM slot.
 *
 * @param[in]   rhea_id          HEA identifier
 * @param[in]   pport_id        Physical Port ID
 * @param[in]   ch_id           Channel ID
 * @param[in]   tcam_id         TCAM Slot ID
 * @param[in]   tcam_ctxt       TCAM Parameters
 * @return 0 on success, negative error-code on failure
 */
extern int rhea_channel_tcam_set(unsigned rhea_id,
				 unsigned channel_id,
				 unsigned tcam_id,
				 struct hea_tcam_setting *tcam_setting);

/*
 * Gets the value of a a specific TCAM slot.
 *
 * @param[in]   rhea_id          HEA identifier
 * @param[in]   pport_id        Physical Port ID
 * @param[in]   ch_id           Channel ID
 * @param[in]   tcam_id         TCAM Slot ID
 * @param[out]  tcam_ctxt       Store tcam_ctxt here.
 * @return 0 on success, negative error-code on failure
 */
extern int rhea_channel_tcam_get(unsigned rhea_id,
				 unsigned channel_id,
				 unsigned tcam_id,
				 struct hea_tcam_setting *tcam_setting);

 /*
  * Enable a specific TCAM slot for participation in QP Determination.
  *
  * @param[in]   rhea_id          HEA identifier
  * @param[in]   channel_id          Physical Port ID
  * @param[in]   ch_id            Channel ID
  * @param[in]   tcam_id          TCAM Slot ID
  * @return 0 on success, negative error-code on failure
  */
extern int rhea_channel_tcam_enable(unsigned rhea_id,
				    unsigned channel_id,
				    unsigned tcam_id,
				    unsigned tcam_offset);
/*
 * Disable a specific TCAM slot from  participation in QP Determination.
 *
 * @param[in]    rhea_id         HEA identifier
 * @param[in]    pport_id    Physical Port ID
 * @param[in]   ch_id        Channel ID
 * @param[in]    tcam_id        TCAM Slot ID
 * @return 0 on success, negative error-code on failure
 */
extern int rhea_channel_tcam_disable(unsigned rhea_id,
				     unsigned channel_id,
				     unsigned tcam_id,
				     unsigned tcam_offset);

/*
 * This function is used for setting the hasher parameters of a physical port
 *
 * @param[in]     rhea_id             HEA identifier
 * @param[in]     hasher_id         Hasher identifier
 * @param[in]    context_hasher    Hasher configuration
 * @return 0 on success, negative error-code on failure
 */
extern int rhea_channel_hasher_set(unsigned rhea_id,
				   unsigned channel_id,
				   struct hea_hasher_setting *hasher_setting);

/*
 * This function is used for getting the hasher parameters of a physical port
 *
 * @param[in]     rhea_id             HEA identifier
 * @param[in]     hasher_id         Hasher identifier
 * @param[in]    context_hasher    Hasher configuration
 * @return 0 on success, negative error-code on failure
 */
extern int rhea_channel_hasher_get(unsigned rhea_id,
				   unsigned channel_id,
				   struct hea_hasher_setting *hasher_setting);

#endif /* _NET_POWEREN_INTERFACE_H_ */
