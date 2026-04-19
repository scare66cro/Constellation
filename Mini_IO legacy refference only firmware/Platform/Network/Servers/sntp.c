// sntp.c

#include <string.h>

#include "lwip/udp.h"
#include "lwip/dns.h"

#include "tools.h"
#include "sntp.h"
#include "net_params.h"
#include "system.h"

#define NT_O_LEN   48
#define NTP_OFFSET 40

static struct ip_addr remoteaddr;
static ntp_receive_time_fn ntp_cb=NULL;
static struct udp_pcb *udp_socket;

static void ntp_received(void *arg, struct udp_pcb *socket, struct pbuf *p, struct ip_addr *addr, u16_t port)
{
	short year;
    char day;
    char month;
    char hour;
    char minute;
    char second;
    unsigned int ntp_timestamp;
    unsigned int timestamp;
    
    char *data;
    
    //debug_printf("sntp received!\n");
    
    if (p!=NULL)
    {
        data=p->payload;

		if (memcmp(&remoteaddr, addr, sizeof(struct ip_addr))==0)
		{
	        ntp_timestamp =(data[NTP_OFFSET+3]<<0);
	        ntp_timestamp|=(data[NTP_OFFSET+2]<<8);
	        ntp_timestamp|=(data[NTP_OFFSET+1]<<16);
	        ntp_timestamp|=(data[NTP_OFFSET+0]<<24);
	        timestamp=ntp_to_1970(ntp_timestamp);
	
	    	format_1970_time(timestamp, &year, &month, &day, &hour, &minute, &second);            
	        //debug_printf("ntp: Received %d bytes from %d.%d.%d.%d :: %s%d/%s%d/%d %s%d:%s%d:%s%d\n", p->tot_len, ip4_addr1(addr),ip4_addr2(addr),ip4_addr3(addr),ip4_addr4(addr),
	        	//month<10?"0":"", month, day<10?"0":"", day, year, 
	        	//hour<10?"0":"", hour, minute<10?"0":"", minute, second<10?"0":"", second);
			
			if (ntp_cb!=NULL) ntp_cb(NTP_SUCCESSFUL, timestamp);
			udp_remove(socket);
			udp_socket=NULL;
		}
		else
		{
			//debug_printf("packet not for us...\n");
		}
		
		pbuf_free(p);
    }
    else
    {
        //debug_printf("ntp: Received a NULL p!\n");
        if (ntp_cb!=NULL) ntp_cb(NTP_ERROR, 0);
    }	
}



// starts the NTP process
static void ntp_request(void)
{
    struct pbuf *p = NULL;
	
	
    if (net_up==0)
    {
    	if (ntp_cb!=NULL) ntp_cb(NTP_ERROR, 0);
    	return;
    }

	if (udp_socket==NULL)
	{
	    // setup UDP server for Discovery Requests
	    udp_socket = udp_new();
	    if (udp_socket==NULL)
	    {
			if (ntp_cb!=NULL) ntp_cb(NTP_ERROR, 0);
			return;
	    } 
	    udp_bind(udp_socket, IP_ADDR_ANY, net_params.sntp.port);
	    udp_recv(udp_socket, ntp_received, NULL);
	}

    p=pbuf_alloc(PBUF_RAW, NT_O_LEN, PBUF_POOL);
    if (p!=NULL)
    {
	    pbuf_set(p, 0, NT_O_LEN);
	    ((char*)p->payload)[0]=0x1B;
	    udp_sendto(udp_socket,p,&remoteaddr, net_params.sntp.port);
	    pbuf_free(p);
    }
    else
    {
    	if (ntp_cb!=NULL) ntp_cb(NTP_ERROR, 0);
    	udp_remove(udp_socket);
    }
}

static void ntp_dns_found_cb(const char *name, struct ip_addr *ipaddr, void *callback_arg)
{
	if ((ipaddr) && (ipaddr->addr))
  	{
  		memcpy(&remoteaddr, ipaddr, sizeof(struct ip_addr));
		//debug_printf("ntp: dns_found: '%s' -> %d.%d.%d.%d\n\n", name, ip4_addr1(ipaddr),ip4_addr2(ipaddr),ip4_addr3(ipaddr),ip4_addr4(ipaddr));
		ntp_request();
  	}
  	else
  	{
  		//debug_printf("ntp: dns_found returns no IP addr\n\n");
  		if (ntp_cb!=NULL) ntp_cb(NTP_FAILED_DNS, 0);
  	}
}

void ntp_start_request(ntp_receive_time_fn cb)
{
	u32_t ipaddr;
	
	ntp_cb=cb;
	memset(&remoteaddr, 0, sizeof(struct ip_addr));
	
	ipaddr = ipaddr_addr(net_params.sntp.server);
  	if (ipaddr == IPADDR_NONE) 
  	{
		dns_gethostbyname(net_params.sntp.server, &remoteaddr, ntp_dns_found_cb, NULL, 1);
  	}
  	else 
  	{
  		ip4_addr_set_u32(&remoteaddr, ipaddr);
  		//debug_printf("ntp: in IP address form already, making request\n");
  		ntp_request();	
  	}
}

/***   End Of File   ***/
