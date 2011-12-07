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

#ifndef _ASM_POWEREN_HEA_EQ_H_
#define _ASM_POWEREN_HEA_EQ_H_

#include "poweren_hea_bitops.h"
#include "poweren_hea_cq.h"

/*
 * Specifies features for EQ
 * which can be set at runtime
 */
enum hea_eq_feature_get {
	HEA_EQ_AER_GET,
	HEA_EQ_ENABLED_GET,
	HEA_EQ_IRQ_NR_GET,
};

enum hea_eq_feature_set {
	HEA_EQ_AER_SET,
};

enum hea_eq_coalesing2_delay {
	HEA_EQ_COALESING_DELAY_0 = 0,
	HEA_EQ_COALESING_DELAY_1 = 1,
	HEA_EQ_COALESING_DELAY_2 = 2,
	HEA_EQ_COALESING_DELAY_3 = 3,
	HEA_EQ_COALESING_DELAY_4 = 4,
	HEA_EQ_COALESING_DELAY_5 = 5,
	HEA_EQ_COALESING_DELAY_6 = 6,
	HEA_EQ_COALESING_DELAY_7 = 7,
	HEA_EQ_COALESING_DELAY_8 = 8,
};

enum hea_eq_gen_comp_event {
	HEA_EQ_GEN_COM_EVENT_DISABLE,
	HEA_EQ_GEN_COM_EVENT_ENABLE,
};

/*
 * Struct describes EQ configuration features
 *
 * eqe_count	Minumum number of EQEs
 * irq_type	supported interrupt type
 */
struct hea_eq_cfg {
	enum hea_q_size eqe_count;
	enum hea_interrupts irq_type;
	enum hea_eq_coalesing2_delay coalesing2_delay;
	enum hea_eq_gen_comp_event generate_completion_events;
};

/*
 * Structure which represents EQE
 */
struct hea_eqe {
	__u64 eqe;
};

static inline int hea_eqe_is_valid(struct hea_eqe *e)
{
	return hea_test_u64_bit(e->eqe, 0);
}
static inline int hea_eqe_is_completion(struct hea_eqe *e)
{
	return hea_test_u64_bit(e->eqe, 1);
}


/* Bits 6:7 = 0b01 = EQ was Empty of Completion Events (prior to this
 * event) */
/* Bits 6:7 = 0b10 = EQ was NOT Empty of Completion Events & Current
 * Time > Next Interrupt Time Stamp. */
/* Bits 6:7 = 0b11 = EQ was NOT Empty of Completion Events & Current
 * Time <= Next Interrupt Time Stamp. */
static inline int hea_eqe_comp_was_empty(struct hea_eqe *e)
{
	return hea_get_u64_bits(e->eqe, 6, 7) == 1;
}
static inline int hea_eqe_comp_gt_nits(struct hea_eqe *e)
{
	return hea_get_u64_bits(e->eqe, 6, 7) == 2;
}
static inline int hea_eqe_comp_le_nits(struct hea_eqe *e)
{
	return hea_get_u64_bits(e->eqe, 6, 7) == 3;
}

static inline int hea_eqe_event_type(struct hea_eqe *e)
{
	return hea_get_u64_bits(e->eqe, 2, 7);
}

#define HEA_EQE_ET_QP_WARNING                 0x03	/* QP Warning */
#define HEA_EQE_ET_CP_WARNING                 0x04	/* CQ Warning */
#define HEA_EQE_ET_QP_ERROR                   0x05	/* QP Error */
#define HEA_EQE_ET_QP_ERROR_EQ0               0x06	/* QP Error */
#define HEA_EQE_ET_CQ_ERROR                   0x07	/* CQ Error */
#define HEA_EQE_ET_CQ_ERROR_EQ0               0x08	/* CQ Error */
#define HEA_EQE_ET_PORT_EVENT                 0x0A	/* Port Event* */
#define HEA_EQE_ET_EQ_ERROR                   0x0C	/* EQ Error */
/* Unaffiliated Access Error */
#define HEA_EQE_ET_UA_ERROR                   0x11
/* First Error Capture Info */
#define HEA_EQE_ET_FIRST_ERROR_CAPTURE_INFO   0x14
/* COP CQ Access Error */
#define HEA_EQE_ET_COP_CQ_ACCESS_ERROR        0x16
/* COP QP Access Error */
#define HEA_EQE_ET_COP_QP_ACCESS_ERROR        0x17
/* COP Ticket Access Error */
#define HEA_EQE_ET_COP_TICKET_ACCESS_ERROR    0x18
#define HEA_EQE_ET_COP_TICKET_ERROR           0x19	/* COP Ticket Error */
#define HEA_EQE_ET_COP_DATA_ERROR             0x1A	/* COP Data Error */

/*QP/CQ Number */
/*(8:31) QP Number */
/*(8:31) CQ Number */
/*(18:31) Ticket Number */
static inline __u32 hea_eqe_qp_number(struct hea_eqe *e)
{
	return hea_get_u64_bits(e->eqe, 8, 31);
}
static inline __u32 hea_eqe_cq_number(struct hea_eqe *e)
{
	return hea_get_u64_bits(e->eqe, 8, 31);
}
static inline __u32 hea_eqe_ticket_number(struct hea_eqe *e)
{
	return hea_get_u64_bits(e->eqe, 18, 31);
}

/* Resource Identifier */
/*(32:63) QP Token (4 bytes) */
/*(40:63) QP Number (3 bytes) */
/*(32:63) CQ Token (4 bytes) */
/*(40:63) CQ Number (3 bytes) */
/*(56:63) Port Number (1 byte) */
/*(48:63) EQ Number (2 bytes) */
/*(39:47) LPAR, (50:63) PID */
static inline __u32 hea_eqe_qp_token(struct hea_eqe *e)
{
	return hea_get_u64_bits(e->eqe, 32, 63);
}
static inline __u32 hea_eqe_qp_number2(struct hea_eqe *e)
{
	return hea_get_u64_bits(e->eqe, 40, 63);
}
static inline __u32 hea_eqe_cq_token(struct hea_eqe *e)
{
	return hea_get_u64_bits(e->eqe, 32, 63);
}
static inline __u32 hea_eqe_cq_number2(struct hea_eqe *e)
{
	return hea_get_u64_bits(e->eqe, 40, 63);
}
static inline __u32 hea_eqe_pport_number(struct hea_eqe *e)
{
	return hea_get_u64_bits(e->eqe, 56, 63);
}
static inline __u32 hea_eqe_eq_number(struct hea_eqe *e)
{
	return hea_get_u64_bits(e->eqe, 48, 63);
}
static inline __u32 hea_eqe_lpar(struct hea_eqe *e)
{
	return hea_get_u64_bits(e->eqe, 39, 47);
}

#ifdef __KERNEL__
enum irqreturn;
typedef enum irqreturn (*hea_irq_handler_t) (int irq, void *eqp);
#endif

/*********************** AER ******************************************/

/*
 * Defines inline functions to obtain AER CQ Error Status bits
 */
static inline int hea_eq_aer_eq_invalid_link_err_check(const __u64 *aer)
{
	return hea_test_u64_bit(*aer, 1);
}
static inline int hea_eq_aer_data_bus_err_check(const __u64 *aer)
{
	return hea_test_u64_bit(*aer, 32);
}
static inline int hea_eq_aer_mem_access_err_check(const __u64 *aer)
{
	return hea_test_u64_bit(*aer, 33);
}
static inline int hea_eq_aer_intern_err_check(const __u64 *aer)
{
	return hea_test_u64_bit(*aer, 34);
}
static inline int hea_eq_aer_length_align_err_check(const __u64 *aer)
{
	return hea_test_u64_bit(*aer, 35);
}
static inline int hea_eq_aer_reserv_facility_access_err_check(const __u64 *aer)
{
	return hea_test_u64_bit(*aer, 36);
}
static inline int hea_eq_aer_inv_class_access_err_check(const __u64 *aer)
{
	return hea_test_u64_bit(*aer, 37);
}

/*
 * Defines inline functions to reset AER CQ Error Status bits. This
 * function does not reset the actual register.  It sets the
 * appropriate bits in a variable which has to be passed to the
 * register using a different function.
 */
static inline void hea_eq_aer_eq_invalid_link_err_reset(__u64 *aer)
{
	*aer = hea_set_u64_bits(*aer, 0, 1, 1);
}
static inline void hea_eq_aer_data_bus_err_reset(__u64 *aer)
{
	*aer = hea_set_u64_bits(*aer, 0, 10, 32);
}
static inline void hea_eq_aer_mem_access_err_reset(__u64 *aer)
{
	*aer = hea_set_u64_bits(*aer, 0, 10, 33);
}
static inline void hea_eq_aer_intern_err_reset(__u64 *aer)
{
	*aer = hea_set_u64_bits(*aer, 0, 10, 34);
}
static inline void hea_eq_aer_length_align_err_reset(__u64 *aer)
{
	*aer = hea_set_u64_bits(*aer, 0, 10, 35);
}
static inline void
hea_eq_aer_reserv_facility_access_err_reset(__u64 *aer)
{
	*aer = hea_set_u64_bits(*aer, 0, 10, 36);
}
static inline void hea_eq_aer_inv_class_access_err_reset(__u64 *aer)
{
	*aer = hea_set_u64_bits(*aer, 0, 10, 37);
}

#endif /* _ASM_POWEREN_HEA_EQ_H_ */
