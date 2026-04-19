#ifndef DHCD_H
#define DHCD_H

#define DHCPD_PORT 67

typedef struct
{
	unsigned char enable;
    unsigned char start_addr[4]; // address range dhcp server can hand out
    unsigned char end_addr[4];
    unsigned int  lease_time;
}_dhcpd_params;

#define LEASE_FREE    0
#define LEASE_OFFERED 1
#define LEASE_BOUND   2

#define DHCPD_MAGIC 0x456AB2CD

typedef struct
{
	int magic;
    struct ip_addr addr;
    unsigned char hwaddr[6];
    unsigned int state;
    unsigned int expires; // UTC time that the lease expires
}_dhcpd_leases;

#define MAX_DHCPD_LEASES 10

extern _dhcpd_leases dhcpd_leases[MAX_DHCPD_LEASES];

void dhcpd_init(_dhcpd_params *in);

#endif
/***   End Of File   ***/
