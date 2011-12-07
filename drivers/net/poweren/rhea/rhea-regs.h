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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http:/www.gnu.org/licenses/>.
 */

#ifndef _RHEA_REGS_H_
#define _RHEA_REGS_H_

#include "hea-bits.h"

/* general register table */
/* General Base Address Register */
#define RHEA_REG_G_GBA	      0x000000
/* Node ID Register (includes Ntype, Device ID, Rev ID, #Ports) */
#define RHEA_REG_G_NID	      0x000008
/* Vendor ID Register */
#define RHEA_REG_G_VID	      0x000010
/* HEA Capability Register */
#define RHEA_REG_G_HEACAP     0x000018
/* Burned-In MAC Register */
#define RHEA_REG_G_BIMAC      0x000020
/* Queue Pair User Space Base Address Register */
#define RHEA_REG_G_QPUBA      0x000048
/* Queue Pair Privileged Space Base Address Register */
#define RHEA_REG_G_QPPBA      0x000050
/* Queue Pair Super Privileged Space Base Address Register */
#define RHEA_REG_G_QPSBA      0x000060
/* Queue Pair Table Size Register (QPCap, QPSize) */
#define RHEA_REG_G_QPTSZ      0x0000A8
/* Queue Pair Context Internal Array Capability Register */
#define RHEA_REG_G_QPCIAC     0x0000B0
/* Completion Queue User Space Base Address Register */
#define RHEA_REG_G_CQUBA      0x0000B8
/* Completion Queue Privileged Space Base Address Register */
#define RHEA_REG_G_CQPBA      0x0000C0
/* Completion Queue Super Privileged Space Base Address Register */
#define RHEA_REG_G_CQSBA      0x0000D0
/* Completion Queue Table Size Register */
#define RHEA_REG_G_CQTSZ      0x0000F8
/* Event Queue Privileged Space Base Address Register */
#define RHEA_REG_G_EQPBA      0x000100
/* Event Queue Super Privileged Space Base Address Register */
#define RHEA_REG_G_EQSBA      0x000110
/* Event Queue Table Size Register */
#define RHEA_REG_G_EQTSZ      0x000138
/* HEA Interrupt Base Register */
#define RHEA_REG_G_BUIDBASE   0x000140
/* HEA Control Register */
#define RHEA_REG_G_HEAC	      0x000148
/* Unaffiliated Asynchronous Event Log Register */
#define RHEA_REG_G_UAELOG     0x000150
/* HEA First UnAffiliated Error Capture Register 0 */
#define RHEA_REG_G_FUEC0      0x000158
/* HEA First UnAffiliated Error Capture Registers 1 */
#define RHEA_REG_G_FUEC1      0x000160
/* HEA First UnAffiliated Error Capture Registers 2 */
#define RHEA_REG_G_FUEC2      0x000168
/* HEA First UnAffiliated Error Capture Registers 3 */
#define RHEA_REG_G_FUEC3      0x000170
/* HEA First UnAffiliated Error Capture Registers 4 */
#define RHEA_REG_G_FUEC4      0x000178
/* HEA First UnAffiliated Error Capture Registers 5 */
#define RHEA_REG_G_FUEC5      0x000180
/* HEA First UnAffiliated Error Capture Registers 6 */
#define RHEA_REG_G_FUEC6      0x000188
/* HEA First UnAffiliated Error Capture Registers 7 */
#define RHEA_REG_G_FUEC7      0x000190
/* HEA First UnAffiliated Error Capture Registers 8 */
#define RHEA_REG_G_FUEC8      0x000198
/* HEA First UnAffiliated Error Capture Registers 9 */
#define RHEA_REG_G_FUEC9      0x0001A0
/* HEA First UnAffiliated Error Capture Registers 10 */
#define RHEA_REG_G_FUEC10     0x0001A8
/* HEA First UnAffiliated Error Capture Registers 11 */
#define RHEA_REG_G_FUEC11     0x0001B0
/* HEA First UnAffiliated Error Capture Registers 12 */
#define RHEA_REG_G_FUEC12     0x0001B8
/* HEA First UnAffiliated Error Capture Registers 13 */
#define RHEA_REG_G_FUEC13     0x0001C0
/* HEA First UnAffiliated Error Capture Registers 14 */
#define RHEA_REG_G_FUEC14     0x0001C8
/* HEA First UnAffiliated Error Capture Registers 15 */
#define RHEA_REG_G_FUEC15     0x0001D0
/* HEA First Affiliated Error Capture Register 0 */
#define RHEA_REG_G_FAEC0      0x0001D8
/* HEA First Affiliated Error Capture Register 1 */
#define RHEA_REG_G_FAEC1      0x0001E0
/* HEA First Affiliated Error Capture Register 2 */
#define RHEA_REG_G_FAEC2      0x0001E8
/* HEA First Affiliated Error Capture Register 3 */
#define RHEA_REG_G_FAEC3      0x0001F0
/* HEA First Affiliated Error Capture Register 4 */
#define RHEA_REG_G_FAEC4      0x0001F8
/* HEA First Affiliated Error Capture Register 5 */
#define RHEA_REG_G_FAEC5      0x000200
/* HEA First Affiliated Error Capture Register 6 */
#define RHEA_REG_G_FAEC6      0x000208
/* HEA First Affiliated Error Capture Register 7 */
#define RHEA_REG_G_FAEC7      0x000210
/* HEA First Affiliated Error Capture Register 8 */
#define RHEA_REG_G_FAEC8      0x000218
/* HEA First Affiliated Error Capture Register 9 */
#define RHEA_REG_G_FAEC9      0x000220
/* HEA First Affiliated Error Capture Register 10 */
#define RHEA_REG_G_FAEC10     0x000228
/* HEA First Affiliated Error Capture Register 11 */
#define RHEA_REG_G_FAEC11     0x000230
/* HEA First Affiliated Error Capture Register 12 */
#define RHEA_REG_G_FAEC12     0x000238
/* HEA First Affiliated Error Capture Register 13 */
#define RHEA_REG_G_FAEC13     0x000240
/* HEA First Affiliated Error Capture Register 14 */
#define RHEA_REG_G_FAEC14     0x000248
/* HEA First Affiliated Error Capture Register 15 */
#define RHEA_REG_G_FAEC15     0x000250
/* HEA Thread Access Base Address Register */
#define RHEA_REG_G_THABA      0x000258
/* Network Node Configuration Register */
#define RHEA_REG_G_NNC	      0x000260
/* Completion Unit Base Address */
#define RHEA_REG_G_CUBA	      0x000268
/* HEA Receive Buffer Allocation Capability Register */
#define RHEA_REG_G_RBAC	      0x000270
/* HEA Timestamp Counter Register */
#define RHEA_REG_G_HEATIME    0x000278
/* Memory Management Cache Capability Register */
#define RHEA_REG_G_MMCCAP     0x000280
/* Early Discard Probability Register */
#define RHEA_REG_G_EDP	      0x000288
/* Counter Capabilities Register */
#define RHEA_REG_G_CNTCAP     0x0002B8
/* Time Increment Register */
#define RHEA_REG_G_TIMEINC    0x0002C0

#define RHEA_REG_PxBASE(x)    ((x) << 18)
#define RHEA_REG_LyBASE(y)    (((y) & 0x03) << 3)

/* BPFC owned registers */

/* Port Rx Control */
#define RHEA_REG_Pxs_RC(x) (RHEA_REG_PxBASE(x) | 0x0)
/* Logical Port y MAC Address (y=0..3) */
#define RHEA_REG_PxLy_MAC(x, y) \
	(RHEA_REG_PxBASE(x) | RHEA_REG_LyBASE(y) | 0x0200)
/* Port Rx Octets */
#define RHEA_REG_Pxs_RXO(x) (RHEA_REG_PxBASE(x) | 0x0800)
/* Port BC Packets */
#define RHEA_REG_Pxs_RXBCP(x) (RHEA_REG_PxBASE(x) | 0x0408)
/* Port MC Packets */
#define RHEA_REG_Pxs_RXMCP(x) (RHEA_REG_PxBASE(x) | 0x0410)
/* BPFC Trace Control */
#define RHEA_REG_Px_TRCPFC(x) (RHEA_REG_PxBASE(x) | 0x0608)
/* BFFC Error Inject */
#define RHEA_REG_Px_ERRINJ(x) (RHEA_REG_PxBASE(x) | 0x0610)
/* BPFC Spare  (n=3..7) */
#define RHEA_REG_Px_SPARE3(x) (RHEA_REG_PxBASE(x) | 0x0618)
/* BPFC Spare  (n=3..7) */
#define RHEA_REG_Px_SPARE4(x) (RHEA_REG_PxBASE(x) | 0x0620)
/* BPFC Spare  (n=3..7) */
#define RHEA_REG_Px_SPARE5(x) (RHEA_REG_PxBASE(x) | 0x0628)
/* BPFC Spare  (n=3..7) */
#define RHEA_REG_Px_SPARE6(x) (RHEA_REG_PxBASE(x) | 0x0630)
/* BPFC Spare  (n=3..7) */
#define RHEA_REG_Px_SPARE7(x) (RHEA_REG_PxBASE(x) | 0x0628)
/* Logical Port Rx Control (y=0..3) */
#define RHEA_REG_PxLy_RC(x, y) \
	(RHEA_REG_PxBASE(x) | RHEA_REG_LyBASE(y) | 0x0800)
/* Port Rx Default Control Broadcast */
#define RHEA_REG_Pxs_RCB(x) (RHEA_REG_PxBASE(x) | 0x0a00)
/* Port Rx Default Control Multicast */
#define RHEA_REG_Pxs_RCM(x) (RHEA_REG_PxBASE(x) | 0x0a08)
/* Port Rx Default Control Unicast */
#define RHEA_REG_Pxs_RCU(x) (RHEA_REG_PxBASE(x) | 0x0a10)
/* Logical Port y Rx Octets (y=0..3) */
#define RHEA_REG_PxLy_RXO(x, y)	\
	(RHEA_REG_PxBASE(x) | RHEA_REG_LyBASE(y) | 0x0c00)
/* Port Rx 64 */
#define RHEA_REG_Pxs_RX64(x) (RHEA_REG_PxBASE(x) | 0x0e00)
/* Port Rx 65-127 */
#define RHEA_REG_Pxs_RX65(x) (RHEA_REG_PxBASE(x) | 0x0e08)
/* Port Rx 128-255 */
#define RHEA_REG_Pxs_RX128(x) (RHEA_REG_PxBASE(x) | 0x0e10)
/* Port Rx 256-511 */
#define RHEA_REG_Pxs_RX256(x) (RHEA_REG_PxBASE(x) | 0x0e18)
/* Port Rx 512-1023 */
#define RHEA_REG_Pxs_RX512(x) (RHEA_REG_PxBASE(x) | 0x0e20)
/* Port Rx 1024-max */
#define RHEA_REG_Pxs_RX1024(x) (RHEA_REG_PxBASE(x) | 0x0e28)
/* Port Rx Frame too long */
#define RHEA_REG_Pxs_RXFTL(x) (RHEA_REG_PxBASE(x) | 0x0e30)
/* Logical Port y Rx frame too long (y=0..3) */
#define RHEA_REG_PxLy_RXFTL(x, y)    \
	(RHEA_REG_PxBASE(x) | RHEA_REG_LyBASE(y) | 0x1000)
/* Logical Port y Rx error (y=0..3) */
#define RHEA_REG_PxLy_RXERR(x, y)    \
	(RHEA_REG_PxBASE(x) | RHEA_REG_LyBASE(y) | 0x1200)
/* Logical Port y Rx filter drop (y=0..3) */
#define RHEA_REG_PxLy_RXFD(x, y) \
	(RHEA_REG_PxBASE(x) | RHEA_REG_LyBASE(y) | 0x1400)
/* Port Rx Symbol error */
#define RHEA_REG_Pxs_RXSE(x) (RHEA_REG_PxBASE(x) | 0x1600)
/* Port Rx with Code error */
#define RHEA_REG_Pxs_RXCE(x) (RHEA_REG_PxBASE(x) | 0x1608)
/* Port Rx Jabbers */
#define RHEA_REG_Pxs_RXJAB(x) (RHEA_REG_PxBASE(x) | 0x1610)
/* Port Rx Fragments */
#define RHEA_REG_Pxs_RXFRAG(x) (RHEA_REG_PxBASE(x) | 0x1618)
/* Port Rx Bad FCS */
#define RHEA_REG_Pxs_RXBFCS(x) (RHEA_REG_PxBASE(x) | 0x1620)
/* Port Rx range length error */
#define RHEA_REG_Pxs_RXRLE(x) (RHEA_REG_PxBASE(x) | 0x1628)
/* Port Rx Out of range length error */
#define RHEA_REG_Pxs_RXORLE(x) (RHEA_REG_PxBASE(x) | 0x1630)
/* Port Rx Runt frame */
#define RHEA_REG_Pxs_RXRF(x) (RHEA_REG_PxBASE(x) | 0x1638)
/* Port Rx Other Error */
#define RHEA_REG_Pxs_RXOERR(x) (RHEA_REG_PxBASE(x) | 0x1800)
/* Port Rx unsupported opcode */
#define RHEA_REG_Pxs_RXUOC(x) (RHEA_REG_PxBASE(x) | 0x1808)
/* Port Rx control pause frame */
#define RHEA_REG_Pxs_RXCPF(x) (RHEA_REG_PxBASE(x) | 0x1810)
/* Port Rx Internal MAC error */
#define RHEA_REG_Pxs_RXIME(x) (RHEA_REG_PxBASE(x) | 0x1818)
/* Port Rx filter drop */
#define RHEA_REG_Pxs_RXFD(x) (RHEA_REG_PxBASE(x) | 0x1820)
/* Port Rx Alignment Error */
#define RHEA_REG_Pxs_RXALN(x) (RHEA_REG_PxBASE(x) | 0x1828)
/* Logical Port QOS Mask (y=0..3) */
#define RHEA_REG_PxLy_QOSM(x, y) \
	(RHEA_REG_PxBASE(x) | RHEA_REG_LyBASE(y) | 0x4000)
/* Port QOS Mask Broadcast */
#define RHEA_REG_Pxs_QOSMB(x) (RHEA_REG_PxBASE(x) | 0x4200)
/* Port QOS Mask Multicast */
#define RHEA_REG_Pxs_QOSMM(x) (RHEA_REG_PxBASE(x) | 0x4208)
/* Port QOS Mask Unicast */
#define RHEA_REG_Pxs_QOSMU(x) ((RHEA_REG_PxBASE(x) | 0x4210)	\
	 | 0x4400)
/* Port Group QPN Array (n=0..31) */
#define RHEA_REG_Px_OPNn(x, n) ((RHEA_REG_PxBASE(x) | (((n) & 0x1f)<<3))
/* Port Group Hash Mask 0 */
#define RHEA_REG_Px_HASHM0(x) (RHEA_REG_PxBASE(x) | 0x4800)
/* Port Group Hash Mask 1 */
#define RHEA_REG_Px_HASHM1(x) (RHEA_REG_PxBASE(x) | 0x4808)
/* Port Group Hash Symmetry Control */
#define RHEA_REG_Px_HASHSC(x) (RHEA_REG_PxBASE(x) | 0x4810)
/* Port Group TCAM Pattern and Result (n=0..15) */
#define RHEA_REG_Px_TCAMPRn(x, n)    \
	((RHEA_REG_PxBASE(x) | (((n) & 0x0f)<<3) | 0x4c00)
/* Port Group TCAM Mask (n=0..15) */
#define RHEA_REG_Px_TCAMMn(x, n) (RHEA_REG_PxBASE(x) | (((n) & 0x0f)<<3))
/* Logical Port Max Frame Size (y=0..3) */
#define RHEA_REG_PxLy_MFSIZE(x, y)   \
	(RHEA_REG_PxBASE(x) | RHEA_REG_LyBASE(y) | 0x4e00)
/* Port Max Frame Size Broadcast */
#define RHEA_REG_Pxs_MFSIZEB(x) (RHEA_REG_PxBASE(x) | 0x5000)
/* Port Max Frame Size Multicast */
#define RHEA_REG_Pxs_MFSIZEM(x) (RHEA_REG_PxBASE(x) | 0x5008)
/* Port Max Frame Size Unicast */
#define RHEA_REG_Pxs_MFSIZEU(x) (RHEA_REG_PxBASE(x) | 0x5010)
/* Port Group Rule Range  */
#define RHEA_REG_Px_RRANGE0(x) (RHEA_REG_PxBASE(x) | 0x5200)
/* Port Group Rule Range 1 */
#define RHEA_REG_Px_RRANGE1(x) (RHEA_REG_PxBASE(x) | 0x5208)
/* Port Group Rule Range 2 */
#define RHEA_REG_Px_RRANGE2(x) (RHEA_REG_PxBASE(x) | 0x5210)
/* Port Group Rule Range 3 */
#define RHEA_REG_Px_RRANGE3(x) (RHEA_REG_PxBASE(x) | 0x5218)
/* Port Group Rule Range 4 */
#define RHEA_REG_Px_RRANGE4(x) (RHEA_REG_PxBASE(x) | 0x5220)
/* Port Group Rule Range 5 */
#define RHEA_REG_Px_RRANGE5(x) (RHEA_REG_PxBASE(x) | 0x5228)
/* Port Group Rule Range 6 */
#define RHEA_REG_Px_RRANGE6(x) (RHEA_REG_PxBASE(x) | 0x5230)
/* Port Group Rule Range 7 */
#define RHEA_REG_Px_RRANGE7(x) (RHEA_REG_PxBASE(x) | 0x5238)
/* Port Group SRM Address Bit Selection 0 */
#define RHEA_REG_Px_SABSEL0(x) (RHEA_REG_PxBASE(x) | 0x5400)
/* Port Group SRM Address Bit Selection 1 */
#define RHEA_REG_Px_SABSEL1(x) (RHEA_REG_PxBASE(x) | 0x5408)
/* Port Group SRM Address Bit Selection 2 */
#define RHEA_REG_Px_SABSEL2(x) (RHEA_REG_PxBASE(x) | 0x5410)
/* Port Group SRM Address Bit Selection 3 */
#define RHEA_REG_Px_SABSEL3(x) (RHEA_REG_PxBASE(x) | 0x5418)
/* Port Group Ext. Coprocessor Mailbox Command */
#define RHEA_REG_Px_XCPMBC(x) (RHEA_REG_PxBASE(x) | 0x5c00)
/* Port Group Ext. Coprocessor Mailbox Data */
#define RHEA_REG_Px_XCPMBD(x) (RHEA_REG_PxBASE(x) | 0x5c00)
/* Port Group Ext. Coprocessor Mailbox Flag */
#define RHEA_REG_Px_XCPMBF(x) (RHEA_REG_PxBASE(x) | 0x5c00)
/* Port Group Ext. Coprocessor Mailbox Result */
#define RHEA_REG_Px_XCPMBR(x) (RHEA_REG_PxBASE(x) | 0x5c00)

/* MMC owned */

/* Port MAC Universally Administered Address */
#define RHEA_REG_Pxs_UAA(x)	    \
	(RHEA_REG_PxBASE(x) | 0xc000)
/* Port MAC VLAN Configuration */
#define RHEA_REG_Pxs_MACVC(x)	    \
	(RHEA_REG_PxBASE(x) | 0xc008)
/* Port MAC Control */
#define RHEA_REG_Pxs_MACC(x)	    \
	(RHEA_REG_PxBASE(x) | 0xc010)
/* Port Status */
#define RHEA_REG_Pxs_PST(x)	    \
	(RHEA_REG_PxBASE(x) | 0xc018)
/* Port Control */
#define RHEA_REG_Pxs_PC(x)	    \
	(RHEA_REG_PxBASE(x) | 0xc020)
/* Port MAC Jumbo Size */
#define RHEA_REG_Pxs_JS(x)	    \
	(RHEA_REG_PxBASE(x) | 0xc028)
/* Port MMA Request / Response */
#define RHEA_REG_Pxs_MMA(x)	    \
	(RHEA_REG_PxBASE(x) | 0xc208)
/* Port 10G PCS Config */
#define RHEA_REG_Pxs_XPCSC(x)	    \
	(RHEA_REG_PxBASE(x) | 0xc600)
/* Port 10G PCS Package */
#define RHEA_REG_Pxs_XPCSP(x)	    \
	(RHEA_REG_PxBASE(x) | 0xc610)
/* Port 10G PCS Status */
#define RHEA_REG_Pxs_XPCSST(x)	    \
	(RHEA_REG_PxBASE(x) | 0xc618)
/* Port PCS ID */
#define RHEA_REG_Pxs_PCSID(x)	    \
	(RHEA_REG_PxBASE(x) | 0xc800)
/* Port 1G SGMII Config */
#define RHEA_REG_Pxs_SPCSC(x)	    \
	(RHEA_REG_PxBASE(x) | 0xc808)
/* Port 1G SGMII Status */
#define RHEA_REG_Pxs_SPCSST(x)	    \
	(RHEA_REG_PxBASE(x) | 0xc810)
/* Port Enet Control */
#define RHEA_REG_Px_EC(x)	    \
	(RHEA_REG_PxBASE(x) | 0xca00)
/* Port Enet Status */
#define RHEA_REG_Px_EST(x)	    \
	(RHEA_REG_PxBASE(x) | 0xca08)
/* Port Transmit Control */
#define RHEA_REG_Pxs_TC(x)	    \
	(RHEA_REG_PxBASE(x) | 0xca10)

/* TXM Owned Registers */

/* Logical Port y Tx Octets (y=0..3) */
#define RHEA_REG_PxLy_TXO(x, y)	    \
	(RHEA_REG_PxBASE(x) | (1<<16) | RHEA_REG_LyBASE(y) | \
	 0x0000)
/* Port Tx 64 */
#define RHEA_REG_Pxs_TX64(x)	    \
	(RHEA_REG_PxBASE(x) | (1<<16) | 0x0200)
/* Port Tx 65-127 */
#define RHEA_REG_Pxs_TX65(x)	    \
	(RHEA_REG_PxBASE(x) | (1<<16) | 0x0208)
/* Port Tx 128-255 */
#define RHEA_REG_Pxs_TX128(x)	    \
	(RHEA_REG_PxBASE(x) | (1<<16) | 0x0210)
/* Port Tx 256-511 */
#define RHEA_REG_Pxs_TX256(x)	    \
	(RHEA_REG_PxBASE(x) | (1<<16) | 0x0218)
/* Port Tx 512-1023 */
#define RHEA_REG_Pxs_TX512(x)	    \
	(RHEA_REG_PxBASE(x) | (1<<16) | 0x0220)
/* Port Tx 1024-max */
#define RHEA_REG_Pxs_TX1024(x)	    \
	(RHEA_REG_PxBASE(x) | (1<<16) | 0x0228)
/* Logical Port y Tx UC Packets (y=0..3) */
#define RHEA_REG_PxLy_TXUCP(x, y)    \
	((RHEA_REG_PxBASE(x) | (1<<16) | RHEA_REG_LyBASE(y) | \
	  0x0400)
/* Logical Port y Tx MC Packets (y=0..3) */
#define RHEA_REG_PxLy_TXMCP(x, y)    \
	((RHEA_REG_PxBASE(x) | (1<<16) | RHEA_REG_LyBASE(y) | \
	  0x0600)
/* Logical Port y Tx BC Packets (y=0..3) */
#define RHEA_REG_PxLy_TXBCP(x, y)    \
	(RHEA_REG_PxBASE(x) | (1<<16) | RHEA_REG_LyBASE(y) | \
	 0x0800)
/* Port Tx Bad FCS */
#define RHEA_REG_Pxs_TXBFCS(x)	    \
	(RHEA_REG_PxBASE(x) | (1<<16) | 0x0a00)
/* Port Local fault */
#define RHEA_REG_Pxs_TXLF(x)	    \
	(RHEA_REG_PxBASE(x) | (1<<16) | 0x0a08)
/* Port Remote fault */
#define RHEA_REG_Pxs_TXRF(x)	    \
	(RHEA_REG_PxBASE(x) | (1<<16) | 0x0a10)
/* Port Tx Internal MAC error */
#define RHEA_REG_Pxs_TXIME(x)	    \
	(RHEA_REG_PxBASE(x) | (1<<16) | 0x0a18)
/* Port Tx control pause frame */
#define RHEA_REG_Pxs_TXCPF(x)	    \
	(RHEA_REG_PxBASE(x) | (1<<16) | 0x0a20)
/* Logical Port Tx Filter Drop (y=0..3) */
#define RHEA_REG_PxLy_TXFD(x, y)	    \
	(RHEA_REG_PxBASE(x) | (1<<16) | RHEA_REG_LyBASE(y) | \
	 0x0c00)
/* Port Tx Octets */
#define RHEA_REG_Pxs_TXO(x)	    \
	(RHEA_REG_PxBASE(x) | (1<<16) | 0x0e00)
/* Port Tx BC Packets */
#define RHEA_REG_Pxs_TXBCP(x)	    \
	(RHEA_REG_PxBASE(x) | (1<<16) | 0x0e08)
/* Port Tx MC Packets */
#define RHEA_REG_Pxs_TXMCP(x)	    \
	(RHEA_REG_PxBASE(x) | (1<<16) | 0x0e10)

/* EM Owned Registers */

/* Logical Port y Rx UC Packets (y=0..3) */
#define RHEA_REG_PxLy_RXUCP(x, y)    \
	(RHEA_REG_PxBASE(x) | RHEA_REG_LyBASE(y) | (1<<16) | \
	 0x4000)
/* Logical Port y Rx MC Packets (y=0..3) */
#define RHEA_REG_PxLy_RXMCP(x, y)    \
	(RHEA_REG_PxBASE(x) | RHEA_REG_LyBASE(y) | (1<<16) | \
	 0x4200)
/* Logical Port y Rx BC Packets (y=0..3) */
#define RHEA_REG_PxLy_RXBCP(x, y)    \
	(RHEA_REG_PxBASE(x) | RHEA_REG_LyBASE(y) | (1<<16) | \
	 0x4400)
/* Port Group Unaffiliated Asynchronous Event Log */
#define RHEA_REG_Px_UAELOG(x)	    \
	(RHEA_REG_PxBASE(x) | (1<<16) | 0x4608)
/* Port Group Unaffiliated Asynchronous Event Log Mask */
#define RHEA_REG_Px_UAELOGM(x)	    \
	(RHEA_REG_PxBASE(x) | (1<<16) | 0x4610)
/* Port XCS Trace Control */
#define RHEA_REG_Pxs_TRCXCS(x)	    \
	(RHEA_REG_PxBASE(x) | (1<<16) | 0x4800)
/* Port XBB Trace Control */
#define RHEA_REG_Pxs_TRCXBB(x)	    \
	(RHEA_REG_PxBASE(x) | (1<<16) | 0x4808)
/* Port Group Trace Control */
#define RHEA_REG_Px_TRC(x)	    \
	(RHEA_REG_PxBASE(x) | (1<<16) | 0x4a00)
/* Port Group HW Error Mask */
#define RHEA_REG_Px_HWEM(x)	    \
	(RHEA_REG_PxBASE(x) | (1<<16) | 0x4a20)
/* Logical Port Rx WQE Depletion Counter */
#define RHEA_REG_PxLy_RXWDD(x, y)    \
	(RHEA_REG_PxBASE(x) | RHEA_REG_LyBASE(y) | (1<<16) | \
	 0x4c00)

/* RBB Owned Registers */

/* Port Group Rx Buffer Overrun Count */
#define RHEA_REG_Px_RXBOR(x)	    \
	(RHEA_REG_PxBASE(x) | (1<<16) | 0x8008)
/* Port Pause Link Buffer Threshold */
#define RHEA_REG_Pxs_PTHLB(x)	    \
	(RHEA_REG_PxBASE(x) | (1<<16) | 0x8200)
/* Port Pause Receive Buffer Threshold */
#define RHEA_REG_Pxs_PTHRB(x)	    \
	(RHEA_REG_PxBASE(x) | (1<<16) | 0x8208)
/* Port Pause Quanta Up */
#define RHEA_REG_Pxs_PQU(x)	    \
	(RHEA_REG_PxBASE(x) | (1<<16) | 0x8210)
/* Port Pause Quanta Down */
#define RHEA_REG_Pxs_PQD(x)	    \
	(RHEA_REG_PxBASE(x) | (1<<16) | 0x8218)
/* Port Wrap Scheduling Threshold */
#define RHEA_REG_Pxs_WSTH(x)	    \
	(RHEA_REG_PxBASE(x) | (1<<16) | 0x8220)
/* Port Pause Retry Time */
#define RHEA_REG_Pxs_PRT(x)	    \
	(RHEA_REG_PxBASE(x) | (1<<16) | 0x8228)
/* Port Link Buffer Control */
#define RHEA_REG_Pxs_LBC(x)	    \
	(RHEA_REG_PxBASE(x) | (1<<16) | 0x8230)
/* Port Pause Receive Buffer Packet Threshold */
#define RHEA_REG_Pxs_PTHRBPK(x)	    \
	(RHEA_REG_PxBASE(x) | (1<<16) | 0x8238)
/* Port Group RBB Trace Control */
#define RHEA_REG_Px_TRCRBB(x)	    \
	(RHEA_REG_PxBASE(x) | (1<<16) | 0x8400)

/* PFC owned registers */

/* Logical Port y VLAN Filter Array entry n (y=0..3, n=0..63) */
#define RHEA_REG_PxLy_VLANFn(x, y, n)	\
	(RHEA_REG_PxBASE(x) | (((y) & 0x03)<<9) | (((n) & 0x3f)<<3) | \
	 (0x10<<16) | 0x0000)
/* Port MAC Hash Array entry n (n=0..63) */
#define RHEA_REG_Pxs_MHASHn(x, n)	\
	(RHEA_REG_PxBASE(x) | (((n) & 0x3f)<<3) | (0x10<<16) | \
	 0xb000)
/* Port QOS Array for B/M/U (n=0..3) */
#define RHEA_REG_Pxs_QOSAn(x, n)	\
	(RHEA_REG_PxBASE(x) | (((n) & 0x03)<<3) | (0x10<<16) | \
	 0xc000)
/* Logical Port QOS Array (y=0..3, n=0..3) */
#define RHEA_REG_PxLy_QOSAn(x, y, n)	\
	(RHEA_REG_PxBASE(x) | (((y) & 0x03)<<9) | (((n) & 0x03)<<3) | \
	 (0x11<<16) | 0x0000)
/* Port Group Rule Memory (r=0..63, c=0..9) */
#define RHEA_REG_Px_RULEMrrc(x, r, c)	\
	(RHEA_REG_PxBASE(x) | (((r) & 0x3f)<<3) | (((x) & 0x07)<<9) | \
	 (0x11<<16) | 0x8000)

#endif /*_RHEA_REGS_H_*/
