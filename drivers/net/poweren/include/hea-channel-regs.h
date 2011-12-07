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

#ifndef _NET_POWEREN_HEA_CHANNEL_REGS_H_
#define _NET_POWEREN_HEA_CHANNEL_REGS_H_

/*
 * Struct containing all logical port counter registers
 *
 * @param	pl_txfd		Logical Port Tx Filter Drop
 * @param	pl_txo		Logical Port y Tx Octets  (UC/MC/BC)
 * @param	pl_txbcp;	Logical Port y Tx BC Packets
 * @param	pl_txucp;	Logical Port y Tx UC Packets
 * @param	pl_txmcp;	Logical Port y Tx MC Packets
 *
 * @param	pl_rxerr	Logical Port y Rx error
 * @param	pl_rxfd		Logical Port y Rx filter drop
 * @param	pl_rxftl	Logical Port y Rx frame too long
 * @param	pl_rxo		Logical Port y Rx Octets  (UC/MC/BC)
 * @param	pl_rxbcp	Logical Port y Rx BC
 * @param	pl_rxmcp	Logical Port y Rx MC
 * @param	pl_rxucp	Logical Port y Rx UC
 * @param	pl_rxwdd	Logical Port Rx WQE Depletion Counter
 *
 */
struct hea_channel_lport_counters {

	/* Tx */
	u64 pl_txfd;
	u64 pl_txo;
	u64 pl_txbcp;
	u64 pl_txmcp;
	u64 pl_txucp;

	/* Rx */
	u64 pl_rxerr;
	u64 pl_rxfd;
	u64 pl_rxftl;
	u64 pl_rxo;
	u64 pl_rxbcp;
	u64 pl_rxmcp;
	u64 pl_rxucp;
	u64 pl_rxwdd;
};

/*
 * Struct containing port error registers of the physical port
 *
 * @param	p_txbfcs	Port Tx Bad FCS
 * @param	p_txrf		Port Remote fault
 * @param	p_txime		Port Tx Internal MAC error
 * @param	p_txbfcs	Port Tx Bad FCS
 *
 * @param	p_rxse		Port Rx Symbol error
 * @param	p_rxce		Port Rx with Code error
 * @param	p_rxjab		Port Rx Jabbers
 * @param	p_rxfrag	Port Rx Fragments
 * @param	p_rxbfcs	Port Rx Bad FCS
 * @param	p_rxrle		Port Rx range length error
 * @param	p_rxorle	Port Rx Out of range length error
 * @param	p_rxrf		Port Rx Runt frame
 * @param	p_rxoerr	Port Rx Other Error
 * @param	p_rxuoc		Port Rx unsupported opcode
 * @param	p_rxime		Port Rx Internal MAC error
 * @param	p_rxfd		Port Rx filter drop
 * @param	p_rxaln		Port Rx Alignment Error
 * @param	p_rxftl		Port Rx Frame too long
 */
struct hea_channel_pport_error_counters {

	/* Tx */
	u64 p_txbfcs;
	u64 p_txrf;
	u64 p_txime;

	/* Rx */
	u64 p_rxse;
	u64 p_rxce;
	u64 p_rxjab;
	u64 p_rxfrag;
	u64 p_rxbfcs;
	u64 p_rxrle;
	u64 p_rxorle;
	u64 p_rxrf;
	u64 p_rxoerr;
	u64 p_rxuoc;
	u64 p_rxime;
	u64 p_rxfd;
	u64 p_rxaln;
	u64 p_rxftl;
};

/*
 * This struct contains the count of received and transmitted pause frames
 *
 * @param	p_txcpf		Port Tx control pause frame
 * @param	p_rxcpf		Port Rx control pause frame
 */
struct hea_channel_pport_pause_frames_counters {

	/* Tx */
	u64 p_txcpf;

	/* Rx */
	u64 p_rxcpf;
};

/*
 * Struct containing all physical port UC/MC/BC counters
 *
 * @param	p_txo		Port Tx Octets (UC/MC/BC)
 * @param	p_txbcp		Port Tx BC Packets
 * @param	p_txmcp		Port Tx MC Packets
 *
 * @param	p_rxo		Port Rx Octets  (UC/MC/BC)
 * @param	p_rxbcp		Port Rx BC Packets
 * @param	p_rxmcp		Port Tx MC Packets
 */
struct hea_channel_pport_uc_mc_bc_counters {

	/* Tx */
	u64 p_txo;
	u64 p_txbcp;
	u64 p_txmcp;

	/* Rx */
	u64 p_rxo;
	u64 p_rxbcp;
	u64 p_rxmcp;
};

/*
 * Struct which contains the counters which
 *
 * @param	p_tx64		Count of Tx packets of size 64 bytes
 * @param	p_tx65		Count of Tx packets of size between 65 and 127 bytes
 * @param	p_tx128		Count of Tx packets of size between 128 and 255 bytes
 * @param	p_tx256		Count of Tx packets of size between 256 and 511 bytes
 * @param	p_tx512		Count of Tx packets of size between 512 and 1023 bytes
 * @param	p_tx1024	Count of Tx packets of size from 1024 bytes
 *
 * @param	p_rx64		Count of Rx packets of size 64 byte
 * @param	p_rx65		Count of Rx packets of size between 65 and 127 bytes
 * @param	p_rx128		Count of Rx packets of size between 128 and 255 bytes
 * @param	p_rx256		Count of Rx packets of size between 256 and 511 bytes
 * @param	p_rx512		Count of Rx packets of size between 512 and 1023 bytes
 * @param	p_rx1024	Count of Rx packets of size from 1024 bytes
 */
struct hea_channel_pport_histogram_counters {

	/* Tx */
	u64 p_tx64;
	u64 p_tx65;
	u64 p_tx128;
	u64 p_tx256;
	u64 p_tx512;
	u64 p_tx1024;

	/* Rx */
	u64 p_rx64;
	u64 p_rx65;
	u64 p_rx128;
	u64 p_rx256;
	u64 p_rx512;
	u64 p_rx1024;
};

enum hea_channel_counter_type {
	HEA_LPORT_COUNTERS,
	HEA_PPORT_ERROR_COUNTERS,
	HEA_PPORT_PAUSE_FRAMES_COUNTERS,
	HEA_PPORT_UC_MC_BC_COUNTERS,
	HEA_PPORT_HISTOGRAM_COUNTERS,
};

struct hea_channel_counters {
	enum hea_channel_counter_type type;
	union {
		struct hea_channel_lport_counters lport_counter;
		struct hea_channel_pport_error_counters pport_err;
		struct hea_channel_pport_pause_frames_counters
			pport_pause_frames;
		struct hea_channel_pport_uc_mc_bc_counters pport_uc_mc_bc;
		struct hea_channel_pport_histogram_counters pport_histogram;
	};
};

#endif /* _NET_POWEREN_HEA_CHANNEL_REGS_H_ */
