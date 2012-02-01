/*
 *  Copyright 2011 Michael Ellerman, IBM Corp.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */
#ifndef __ASM_POWERPC_WSP_H
#define __ASM_POWERPC_WSP_H

extern int wsp_get_chip_id(struct device_node *dn);

#define MBX_MAX_SERVICE 6
#define MBX_SERVICE_CMD 2
#define MBX_SERVICE_CONSOLE 3
#define MBX_DEFAULT_VFN  0

#define POWEREN_EVAL  0x100
#define POWEREN_RESET 0x500
#define POWEREN_HALT  0x505
#define POWEREN_PING  0x595

struct mbx_service_context {
	void (*service_func) (uint16_t flags, char *data,
			      uint32_t len);
	void *data;
	uint32_t max_len;
};

union mbx_packet_header {
	struct {
		uint16_t service;
		uint16_t flags;
		uint32_t len;
	};
	uint64_t val;
};

union poweren_cmd_packet_header {
	struct  {
		uint16_t cmd;
		uint16_t nargs;
		uint32_t data_size;
		uint64_t args[126];
	} ;
	uint64_t val[127];
};

int mbx_write_packet(uint16_t service, uint16_t flags,
		     char *packet, int32_t len);

int mbx_install_service(uint16_t service,
			void (*service_func) (uint16_t flags, char *data,
					      uint32_t len),
			void *data,
			uint32_t max_len, struct mbx_service_context *ctx);



#endif /* __ASM_POWERPC_WSP_H */
