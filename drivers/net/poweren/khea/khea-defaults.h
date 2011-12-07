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


/* Defaults, you may be able to change this with Kconfig */
#ifndef CONFIG_KHEA_ENABLE
#define CONFIG_KHEA_ENABLE 4369
#endif

#ifndef CONFIG_KHEA_BASENAME
#define CONFIG_KHEA_BASENAME "hea"
#endif

#ifndef CONFIG_KHEA_NAPI_WEIGHT
#define CONFIG_KHEA_NAPI_WEIGHT 256
#endif

#ifndef CONFIG_KHEA_NUM_QP
#define CONFIG_KHEA_NUM_QP 1
#endif

#ifndef CONFIG_KHEA_SEND_LL_LIMIT
#define CONFIG_KHEA_SEND_LL_LIMIT 80
#endif

#ifndef CONFIG_KHEA_SEND_WQE_TYPE
#define CONFIG_KHEA_SEND_WQE_TYPE 2
#endif

#ifndef CONFIG_KHEA_SENDQ_LEN
#define CONFIG_KHEA_SENDQ_LEN 1024
#endif

#ifndef CONFIG_KHEA_SENDQ_RECLAIM
#define CONFIG_KHEA_SENDQ_RECLAIM 32
#endif

#ifndef CONFIG_KHEA_RECVQ_NUM
#define CONFIG_KHEA_RECVQ_NUM 2
#endif

#ifndef CONFIG_KHEA_RECVQ_REFILL
#define CONFIG_KHEA_RECVQ_REFILL 512
#endif

#ifndef CONFIG_KHEA_RECVQ1_LEN
#define CONFIG_KHEA_RECVQ1_LEN 1024
#endif

#ifndef CONFIG_KHEA_RECVQ2_LEN
#define CONFIG_KHEA_RECVQ2_LEN 1024
#endif

#ifndef CONFIG_KHEA_RECVQ1_LOW
#define CONFIG_KHEA_RECVQ1_LOW 512
#endif

#ifndef CONFIG_KHEA_RECVQ2_LOW
#define CONFIG_KHEA_RECVQ2_LOW 512
#endif

#ifndef CONFIG_KHEA_RECVQ1_SIZE
#define CONFIG_KHEA_RECVQ1_SIZE 128
#endif

#ifndef CONFIG_KHEA_RECVQ2_SIZE
#define CONFIG_KHEA_RECVQ2_SIZE 2048
#endif

#ifndef CONFIG_KHEA_MAX_LRO
#define CONFIG_KHEA_MAX_LRO 0
#endif

#ifndef CONFIG_KHEA_DO_GSO
#define CONFIG_KHEA_DO_GSO 1
#endif

#ifndef CONFIG_KHEA_FRAG_ALLOC_ORDER
#define CONFIG_KHEA_FRAG_ALLOC_ORDER 4
#endif

#ifndef CONFIG_KHEA_USE_LL_RQ1
#define CONFIG_KHEA_USE_LL_RQ1 1
#endif
