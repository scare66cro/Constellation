#ifndef SNMP_H
#define SNMP_H

#include "asn.1.h"
#include "lwip/ip_addr.h"

#define SNMP_PORT 161

typedef struct
{
	_oid          oid;
	unsigned int  data_length;
	void         *data;
	int           type;
}_trap_varbinding;

typedef  int(*snmp_cb)(_oid *oid, int type, void *val, int val_len);

// ONLY Call snmp_init OR snmp_setup since snmp_init calls snmp_setup
// snmp_init  -- initializes UDP handler
// snmp_setup -- sets up internal system

void snmp_init(void);
void snmp_setup(void);
void snmp_init_keys(void);

int snmp_build_trap(unsigned char *buffer, int version, void *auth, _trap_varbinding *var_bindings, unsigned int num_bindings);
int snmp_send_trap(int version, struct ip_addr *ipaddr, u16_t port, void *auth, _trap_varbinding *var_bindings, unsigned int num_bindings);
int snmp_process(unsigned char *packet_in, unsigned int length_in, unsigned char *packet_out);
// for version 1 and 2c, void *auth points to the community name
// for version 2, void *auth points to an integer representing the user_index
int snmp_start_request(struct ip_addr *ipaddr, u16_t port, _oid *oid, int version, snmp_cb cb, void *auth);
#endif
/***   End Of File   ***/
