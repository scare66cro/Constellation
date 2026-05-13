// dhcpd.c

// Standard Includes
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// File Includes
#include "system.h"
#include "lwip/udp.h"
#include "utils/lwiplib.h"
#include "params.h"
#include "lwip/dhcp.h"
#include "dhcpd.h"
#include "tools.h"
#include "netif/etharp.h"
#include "at45_flash.h"

extern unsigned int utc_get_sec(void);

_dhcpd_params *dhcpd_params;
_dhcpd_leases dhcpd_leases[MAX_DHCPD_LEASES];


#define DHCPD_MAX_SIZE 800
unsigned char dhcpd_buffer[DHCPD_MAX_SIZE];

static void dhcpd_save_leases(void);

unsigned char *get_option_offset(unsigned char *in_data, int max_len, int option)
{
    int i;
    for(i=0; i<max_len; i++)
    {
        if (in_data[i]==option) return in_data+i;
        i+=in_data[i+1]+1;
    }
    return NULL;
}

char dhcpd_temp[100];

int get_lease_index(unsigned char *hwaddr)
{
    int i;
    for(i=0; i<MAX_DHCPD_LEASES; i++)
    {
        if (memcmp(hwaddr, dhcpd_leases[i].hwaddr, 6)==0) return i;
    }
    return -1;
}

int dhcpd_next_addr(struct ip_addr *addr)
{
    int in_use=0;
    int i;
    
    int test_addr;
    int start_addr;
    int end_addr;
    
    // IP address are stored in big-endian/network order, we need to conver to litle for this math
    start_addr=htonl(*(unsigned int*)dhcpd_params->start_addr);
    end_addr=htonl(*(unsigned int*)dhcpd_params->end_addr);
    
    // find the next address to offer and place it in addr
    for(test_addr=start_addr; test_addr<=end_addr; test_addr++)
    {
        // check if test is available
        in_use=0;
        for(i=0; i<MAX_DHCPD_LEASES; i++)
        {
            if (((dhcpd_leases[i].state==LEASE_OFFERED)||(dhcpd_leases[i].state==LEASE_BOUND)) &&
                (test_addr==htonl(dhcpd_leases[i].addr.addr)))
            {
                in_use=1;
                break;
            }
        }
        
        if (in_use==0)
        {
            addr->addr=htonl(test_addr);
            return 0;
        }
    }
    
    return -1;
}

unsigned char * add_option(unsigned char *out, char option, void *data, char len)
{
    out[0]=option;
    out[1]=len;
    memcpy(out+2, data, len);
    return out+2+len;
}

static void dhcpd_discover(struct udp_pcb *pcb, unsigned char *data, int length)
{
    int lease_index=-1;
    struct dhcp_msg * dhcp_msg;
    struct pbuf *p = NULL;
    unsigned int xid;
    unsigned long sip;
    unsigned long snm;
    unsigned long sgw;
    char temp_char;
    int temp_int;
    unsigned char *p1;
    
    debug_printf("dhcpd_discover\n");
    
    // check if leased IP address before
    for(lease_index=0; lease_index<MAX_DHCPD_LEASES; lease_index++)
    {
        if (memcmp(dhcpd_leases[lease_index].hwaddr, data+DHCP_CHADDR_OFS, 6)==0)
        {
            // found a MAC match
            debug_printf("dhcpd: found matching MAC match\r\n");
            break;
        }
    }
    
    // if no previous lease, find the next available spot
    if (lease_index>=MAX_DHCPD_LEASES)
    {
        for(lease_index=0; lease_index<MAX_DHCPD_LEASES; lease_index++)
        {
            if (dhcpd_leases[lease_index].state==LEASE_FREE)
            {
            	debug_printf("dhcpd: found empty lease spot at %d\r\n", lease_index);
                memset(&dhcpd_leases[lease_index], 0, sizeof(_dhcpd_leases));
                break;
            }
        }
        
    }
    if (lease_index>=MAX_DHCPD_LEASES)
    {
        // if no free leases, find the next one to reuse
        for(lease_index=0; lease_index<MAX_DHCPD_LEASES; lease_index++)
        {
            if ((dhcpd_leases[lease_index].state==LEASE_BOUND) &&
                (dhcpd_leases[lease_index].expires<utc_get_sec()))
            {
            	debug_printf("dhcpd: found old lease spot to reuse at %d\r\n", lease_index);
                memset(&dhcpd_leases[lease_index], 0, sizeof(_dhcpd_leases));
                break;
            }
        }
    }
    
    if (lease_index>=MAX_DHCPD_LEASES)
    {
    	debug_printf("dhcpd: No free lease spots, cannot reply to request\r\n");
    	return; // no available lease states
    }
    
    if (dhcpd_leases[lease_index].addr.addr==0)
    {
        if (dhcpd_next_addr(&dhcpd_leases[lease_index].addr)<0)
        {
            // no available addresses
            debug_printf("dhcpd: no available addresses left\r\n");
            return;
        }
    }
    
    dhcpd_leases[lease_index].state=LEASE_OFFERED;
    memcpy(dhcpd_leases[lease_index].hwaddr, data+DHCP_CHADDR_OFS, 6);

    memcpy(&xid, data+DHCP_XID_OFS, 4); // save tx_id
    
    // at this point we have an IP address to offer
    dhcp_msg=(struct dhcp_msg *)dhcpd_buffer;
    dhcp_msg->op=DHCP_BOOTREPLY;
    dhcp_msg->htype=DHCP_HTYPE_ETH;
    dhcp_msg->hlen=DHCP_HLEN_ETH;
    dhcp_msg->hops=0;
    dhcp_msg->xid = xid; // set tx_id
    dhcp_msg->secs=0;
    dhcp_msg->flags=0; // flags to 0
    dhcp_msg->ciaddr.addr=0; // set cip to 0
    dhcp_msg->yiaddr.addr=dhcpd_leases[lease_index].addr.addr; // set yip to offered IP address
    dhcp_msg->siaddr.addr=lwIPLocalIPAddrGet();// set sip to our IP address
    dhcp_msg->giaddr.addr=0; // set gip to 0
    memcpy(dhcp_msg->chaddr, dhcpd_leases[lease_index].hwaddr, 6); // set client mac
    memset(dhcp_msg->sname, 0, DHCP_SNAME_LEN);
    memset(dhcp_msg->file, 0, DHCP_FILE_LEN);
    dhcp_msg->cookie=htonl(0x63825363UL);
    //
    // options
    sip = lwIPLocalIPAddrGet();
    snm = lwIPLocalNetMaskGet();
    sgw = lwIPLocalGWAddrGet();
    p1=dhcp_msg->options;
    temp_char=DHCP_OFFER;
    p1=add_option(p1, DHCP_OPTION_MESSAGE_TYPE, &temp_char, 1);            // set dhcp discovery
    p1=add_option(p1, DHCP_OPTION_SERVER_ID, &sip, 4);                     // set dhcp server id - our IP
    temp_int=htonl(dhcpd_params->lease_time);
    p1=add_option(p1, DHCP_OPTION_LEASE_TIME, &temp_int, 4);               // set lease time
    p1=add_option(p1, DHCP_OPTION_SUBNET_MASK, &snm, 4);                   // set subnet mask
    p1=add_option(p1, DHCP_OPTION_ROUTER, &sgw, 4);                        // set router

    // we have to add the ARP entry for this item since we need to send
    // the packet to the new IP address with the client MAC but the
    // client won't respond to an ARP request
    if (update_arp_entry(netif_find("en0"), (ip_addr_t*)&dhcp_msg->yiaddr, (struct eth_addr*)dhcp_msg->chaddr, ETHARP_FLAG_TRY_HARD)!=ERR_OK)
    {
        debug_printf("update_arp_entry not returning ERR_OK!\n");
    }
    
    // send reply packet
    p=pbuf_alloc(PBUF_RAW, p1-dhcpd_buffer, PBUF_POOL);
    if (p!=NULL)
    {
    	debug_printf("dhcpd: offering %d.%d.%d.%d\r\n", ip4_addr1(&dhcp_msg->yiaddr),ip4_addr2(&dhcp_msg->yiaddr),ip4_addr3(&dhcp_msg->yiaddr),ip4_addr4(&dhcp_msg->yiaddr));
	    array_to_pbuf(p, dhcpd_buffer, p1-dhcpd_buffer);
	    udp_sendto(pcb,p,(ip_addr_t*)&dhcp_msg->yiaddr, DHCP_CLIENT_PORT);
	    pbuf_free(p);
    }
    else
    {
    	debug_printf("dhcpd: error allocating pbuf\r\n");
    }
}

static void send_nak(struct udp_pcb *pcb, int lease_index, int xid)
{
    unsigned char *p1;
    struct dhcp_msg *dhcp_msg=(struct dhcp_msg*)dhcpd_buffer;
    unsigned long sip;
    unsigned long snm;
    unsigned long sgw;
    char temp_char;
    struct pbuf *p = NULL;
    int temp_int;
    
    debug_printf("dhcpd: sending nak\n");
  
    dhcp_msg->op=DHCP_BOOTREPLY;
    dhcp_msg->htype=DHCP_HTYPE_ETH;
    dhcp_msg->hlen=DHCP_HLEN_ETH;
    dhcp_msg->hops=0;
    dhcp_msg->xid = xid; // set tx_id
    dhcp_msg->secs=0;
    dhcp_msg->flags=0; // flags to 0
    dhcp_msg->ciaddr.addr=0; // set cip to 0
    dhcp_msg->yiaddr.addr=dhcpd_leases[lease_index].addr.addr; // set yip to offered IP address
    dhcp_msg->siaddr.addr=lwIPLocalIPAddrGet();// set sip to our IP address
    dhcp_msg->giaddr.addr=0; // set gip to 0
    memcpy(dhcp_msg->chaddr, dhcpd_leases[lease_index].hwaddr, 6); // set client mac
    memset(dhcp_msg->sname, 0, DHCP_SNAME_LEN);
    memset(dhcp_msg->file, 0, DHCP_FILE_LEN);
    dhcp_msg->cookie=htonl(0x63825363UL);
    //
    // options
    sip = lwIPLocalIPAddrGet();
    snm = lwIPLocalNetMaskGet();
    sgw = lwIPLocalGWAddrGet();
    p1=dhcp_msg->options;
    temp_char=DHCP_NAK;
    p1=add_option(p1, DHCP_OPTION_MESSAGE_TYPE, &temp_char, 1);            // set dhcp discovery
    p1=add_option(p1, DHCP_OPTION_SERVER_ID, &sip, 4);                     // set dhcp server id - our IP
    temp_int=htonl(dhcpd_params->lease_time);
    p1=add_option(p1, DHCP_OPTION_LEASE_TIME, &temp_int, 4);               // set lease time
    p1=add_option(p1, DHCP_OPTION_SUBNET_MASK, &snm, 4);                   // set subnet mask
    p1=add_option(p1, DHCP_OPTION_ROUTER, &sgw, 4);                        // set router
    
    // we have to add the ARP entry for this item since we need to send
    // the packet to the new IP address with the client MAC but the
    // client won't respond to an ARP request
    if (update_arp_entry(netif_find("en0"), (ip_addr_t*)&dhcp_msg->yiaddr, (struct eth_addr*)dhcp_msg->chaddr, ETHARP_FLAG_TRY_HARD)!=ERR_OK)
    {
        debug_printf("update_arp_entry not returning ERR_OK!\n");
    }
    
    // send reply packet
    p=pbuf_alloc(PBUF_RAW, p1-dhcpd_buffer, PBUF_POOL);
    if (p!=NULL)
    {
	    array_to_pbuf(p, dhcpd_buffer, p1-dhcpd_buffer);
	    udp_sendto(pcb,p,(ip_addr_t*)&dhcp_msg->yiaddr, DHCP_CLIENT_PORT);
	    pbuf_free(p);
    }
    else
    {
    	debug_printf("dhcpd: error allocating pbuf\r\n");
    }
}

static void send_ack(struct udp_pcb *pcb, int lease_index, int xid, int cip)
{
    unsigned char *p1;
    struct dhcp_msg *dhcp_msg=(struct dhcp_msg*)dhcpd_buffer;
    unsigned long sip;
    unsigned long snm;
    unsigned long sgw;
    char temp_char;
    struct pbuf *p = NULL;
    int temp_int;
    
    debug_printf("dhcpd: sending ack\n");
  
    dhcpd_leases[lease_index].state=LEASE_BOUND;
    dhcpd_leases[lease_index].expires=utc_get_sec()+dhcpd_params->lease_time;
    dhcpd_leases[lease_index].magic=DHCPD_MAGIC;
      
    dhcp_msg->op=DHCP_BOOTREPLY;
    dhcp_msg->htype=DHCP_HTYPE_ETH;
    dhcp_msg->hlen=DHCP_HLEN_ETH;
    dhcp_msg->hops=0;
    dhcp_msg->xid = xid; // set tx_id
    dhcp_msg->secs=0;
    dhcp_msg->flags=0; // flags to 0
    dhcp_msg->ciaddr.addr=cip;
    dhcp_msg->yiaddr.addr=dhcpd_leases[lease_index].addr.addr; // set yip to offered IP address
    dhcp_msg->siaddr.addr=lwIPLocalIPAddrGet();// set sip to our IP address
    dhcp_msg->giaddr.addr=0; // set gip to 0
    memcpy(dhcp_msg->chaddr, dhcpd_leases[lease_index].hwaddr, 6); // set client mac
    memset(dhcp_msg->sname, 0, DHCP_SNAME_LEN);
    memset(dhcp_msg->file, 0, DHCP_FILE_LEN);
    dhcp_msg->cookie=htonl(0x63825363UL);
    //
    // options
    sip = lwIPLocalIPAddrGet();
    snm = lwIPLocalNetMaskGet();
    sgw = lwIPLocalGWAddrGet();
    p1=dhcp_msg->options;
    temp_char=DHCP_ACK;
    p1=add_option(p1, DHCP_OPTION_MESSAGE_TYPE, &temp_char, 1);            // set dhcp discovery
    p1=add_option(p1, DHCP_OPTION_SERVER_ID, &sip, 4);                     // set dhcp server id - our IP
    temp_int=htonl(dhcpd_params->lease_time);
    p1=add_option(p1, DHCP_OPTION_LEASE_TIME, &temp_int, 4);               // set lease time
    p1=add_option(p1, DHCP_OPTION_SUBNET_MASK, &snm, 4);                   // set subnet mask
    p1=add_option(p1, DHCP_OPTION_ROUTER, &sgw, 4);                        // set router
    
    // we have to add the ARP entry for this item since we need to send
    // the packet to the new IP address with the client MAC but the
    // client won't respond to an ARP request
    if (update_arp_entry(netif_find("en0"), (ip_addr_t*)&dhcp_msg->yiaddr, (struct eth_addr*)dhcp_msg->chaddr, ETHARP_FLAG_TRY_HARD)!=ERR_OK)
    {
        debug_printf("update_arp_entry not returning ERR_OK!\n");
    }
    
    // send reply packet
    p=pbuf_alloc(PBUF_RAW, p1-dhcpd_buffer, PBUF_POOL);
    if (p!=NULL)
    {
	    array_to_pbuf(p, dhcpd_buffer, p1-dhcpd_buffer);
	    udp_sendto(pcb,p,(ip_addr_t*)&dhcp_msg->yiaddr, DHCP_CLIENT_PORT);
	    pbuf_free(p);
	    dhcpd_save_leases();
    }
    else
    {
    	debug_printf("dhcpd: error allocating pbuf\r\n");
    }
}

static void dhcpd_request(struct udp_pcb *pcb, unsigned char *data, int length)
{
    unsigned char *p1;
    int lease_index;
    struct dhcp_msg * dhcp_msg=(struct dhcp_msg*)data;
    int opt_len = length-DHCP_OPTIONS_OFS;
    unsigned long sip = lwIPLocalIPAddrGet();
    int xid;
    
    xid=dhcp_msg->xid;
    
    debug_printf("dhcpd: request\n");
    
    // check if it's a DHCP Request packet
    p1=get_option_offset(dhcp_msg->options, opt_len, DHCP_OPTION_MESSAGE_TYPE);
    if (p1==NULL) {debug_printf("p1==null\r\n"); return; }
    if ((p1[1]!=1)||(p1[2]!=DHCP_REQUEST)) { debug_printf("not==DHCP_REQUEST\r\n"); return;}
    
    // check if we offered an ip address
    lease_index=get_lease_index(dhcp_msg->chaddr);
    if (lease_index==-1) { debug_printf("No lease offered\r\n"); return; }
    
    // if offered, check if we're listed as the DHCP Server Identification
    if (dhcpd_leases[lease_index].state==LEASE_OFFERED)
    {
        p1=get_option_offset(dhcp_msg->options, opt_len, DHCP_OPTION_SERVER_ID);
        if (p1==NULL) return;
        if (p1[1]!=4) return;
        if (memcmp(&sip, p1+2, 4)!=0)
        {
            memset(&dhcpd_leases[lease_index], 0, sizeof(_dhcpd_leases));
            return;
        }
    
        // verify IP address and if correct send an ack
        p1=get_option_offset(dhcp_msg->options, opt_len, DHCP_OPTION_REQUESTED_IP);
        if (p1==NULL) { send_nak(pcb, lease_index, xid); return; }
        if (p1[1]!=4) { send_nak(pcb, lease_index, xid); return; }
        if (memcmp(&dhcpd_leases[lease_index].addr.addr, p1+2, 4)!=0) { send_nak(pcb, lease_index, xid); return; }
        
        // IP address matched and we were the server identified, send an ack
        send_ack(pcb, lease_index, xid, 0);
    }
    else
    {
        // probably renewing the lease
        // check that cip is set correctly
        if (dhcp_msg->ciaddr.addr!=dhcpd_leases[lease_index].addr.addr) {send_nak(pcb, lease_index, xid); return; }
        send_ack(pcb, lease_index, xid, dhcpd_leases[lease_index].addr.addr);
      
    }
}

static void dhcpd_release(struct udp_pcb *pcb, unsigned char *data, int length)
{
    int lease_index;
    struct dhcp_msg * dhcp_msg=(struct dhcp_msg*)data;
    
    debug_printf("dhcpd_release\n");
    
    // check if we offered an ip address
    lease_index=get_lease_index(dhcp_msg->chaddr);
    if (lease_index==-1) return;
    
    memset(&dhcpd_leases[lease_index], 0, sizeof(_dhcpd_leases));
}

static void dhcpd_inform(struct udp_pcb *pcb, unsigned char *data, int length)
{
    debug_printf("dhcpd_inform\n");
}

static void dhcpd_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p, struct ip_addr *addr, u16_t port)
{
    struct dhcp_msg * dhcp_msg;
    unsigned char *opt_ptr;
    int  total_length;
    int  opt_len;
    unsigned char *p1;
    
    if (p==NULL)
    {
        debug_printf("dhcpd_recv with no data\n");
        return;
    }

    if (p->tot_len>DHCPD_MAX_SIZE)
    {
        debug_printf("DHCPD message size too great!\n");
        pbuf_free(p);
        return;
    }
    
    pbuf_to_array(dhcpd_buffer, p);
    opt_ptr=dhcpd_buffer+DHCP_OPTIONS_OFS;
    opt_len = p->tot_len-DHCP_OPTIONS_OFS;
    total_length = p->tot_len;
    pbuf_free(p);
    dhcp_msg = (struct dhcp_msg*)dhcpd_buffer;

    if ((dhcp_msg->op    != DHCP_BOOTREQUEST) ||
        (dhcp_msg->htype != DHCP_HTYPE_ETH) ||
        (dhcp_msg->hlen  != DHCP_HLEN_ETH))
    {
        debug_printf("DHCPD: op    =%d\n", dhcp_msg->op);
        debug_printf("DHCPD: htype =%d\n", dhcp_msg->htype);
        debug_printf("DHCPD: hlen  =%d\n", dhcp_msg->hlen);
        return;
    }
    
    
    // check some options
    p1=get_option_offset(opt_ptr, opt_len, DHCP_OPTION_MESSAGE_TYPE);
    if (p1!=NULL)
    {
        if (p1[1]!=1) return;
        switch(p1[2])
        {
            case DHCP_DISCOVER: dhcpd_discover(pcb, dhcpd_buffer, total_length); break;
            case DHCP_REQUEST:  dhcpd_request(pcb, dhcpd_buffer, total_length);  break;
            case DHCP_RELEASE:  dhcpd_release(pcb, dhcpd_buffer, total_length);  break;
            case DHCP_INFORM:   dhcpd_inform(pcb, dhcpd_buffer, total_length);   break;
        }
    }
}

static void dhcpd_save_leases(void)
{
	debug_printf("Saving leases\r\n");
	FlashEraseArea(FLASH_DHCPD_LEASES, sizeof(_dhcpd_leases)*MAX_DHCPD_LEASES);
	FlashWrite(FLASH_DHCPD_LEASES, (unsigned char*)dhcpd_leases, sizeof(_dhcpd_leases)*MAX_DHCPD_LEASES);
}

void dhcpd_init(_dhcpd_params *in)
{
    struct udp_pcb *pcb;
	int i;
	
	dhcpd_params=in;

	FlashRead((unsigned char*)dhcpd_leases, FLASH_DHCPD_LEASES, sizeof(_dhcpd_leases)*MAX_DHCPD_LEASES); 

	for(i=0; i<MAX_DHCPD_LEASES; i++)
	{
		if (dhcpd_leases[i].magic!=DHCPD_MAGIC)
		{
			debug_printf("Lease %d failed magic\r\n", i);
			memset(&dhcpd_leases[i], 0, sizeof(_dhcpd_leases));
		}
		else
		{
			debug_printf("Lease %d passes magic with state %d\r\n", i, dhcpd_leases[i].state);
		}
	}


	if (dhcpd_params->enable==0) return;

	debug_printf("DHCPD Starting: %d.%d.%d.%d - %d.%d.%d.%d\r\n",
		ip4_addr1(&dhcpd_params->start_addr),ip4_addr2(&dhcpd_params->start_addr),ip4_addr3(&dhcpd_params->start_addr),ip4_addr4(&dhcpd_params->start_addr),
		ip4_addr1(&dhcpd_params->end_addr),ip4_addr2(&dhcpd_params->end_addr),ip4_addr3(&dhcpd_params->end_addr),ip4_addr4(&dhcpd_params->end_addr) );

    pcb = udp_new();
    udp_bind(pcb, IP_ADDR_ANY, DHCP_SERVER_PORT);
    udp_recv(pcb, dhcpd_recv, NULL);
}


/***   End Of File   ***/
