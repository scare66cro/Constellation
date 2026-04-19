/*=============================================================================
Copyright © 2009 Infinetix Corp.

This file is the property of Infinetix Corp and shall not be
reproduced, copied, or used as the basis for the manufacture or sale
of equipment without the express written permission of Infinetix Corp.

FILE NAME: discovery.c

PURPOSE:
    Replies to device discovery commands and IP setting commands

NOTES:

   

=============================================================================*/

/*=============================================================================
                              Includes
=============================================================================*/

// Standard Includes
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// OS Includes
//#include "FreeRTOS.h"
//#include "semphr.h"
//#include "task.h"

// File Includes
#include "flash.h"
#include "discovery.h"
#include "system.h"
#include "lwip/udp.h"
#include "utils/lwiplib.h"
#include "lwip/tcp.h"
#include "HTTP_fs/http_fs.h"
#include "net_params.h"
#include "version.h"

/*=============================================================================
                              Defines
=============================================================================*/

#define DISCOVERY_IN_PORT  873
#define DISCOVERY_OUT_PORT 547

#define DISCOVERY_QUERY         0x03FD12C7
#define DISCOVERY_QUERY_REPLY   0x5D6612B8
#define DISCOVERY_SET           0x45789235

/*=============================================================================
                                Enums
=============================================================================*/

/*=============================================================================
                            Type Definitions
=============================================================================*/

/*=============================================================================
                              Structures
=============================================================================*/

typedef struct
{
    int   command;
    char  ToIP[4];
    char  IP[4];
    char  NM[4];
    char  GW[4];
    char  MAC[6];
    char  Version[30];
}_discovery_command;

/*=============================================================================
                          External/Public Constants
=============================================================================*/

/*=============================================================================
                          External/Public Variables
=============================================================================*/

/*=============================================================================
                            File Variables
=============================================================================*/

struct udp_pcb *udp_socket;
struct tcp_pcb *tcp_socket;

/*=============================================================================
                       Local Function Protoypes
=============================================================================*/

void DataReceived(void *arg, struct udp_pcb *socket, struct pbuf *p, struct ip_addr *addr, u16_t port);

/*=============================================================================
                           Function Definitions
=============================================================================*/

/******************************************************************************

FUNCTION NAME: DiscoveryInit

PURPOSE:


INPUTS:
    NONE

OUTPUTS:
    NONE

NOTES:
    NOTES TO CLARIFY FUNCTION

******************************************************************************/

static int tcp_bytes_to_write=0;
static int tcp_write_offset=-1;
static int tcp_write_index=0;
static int tcp_dump_all=0;

#define DISC_BUFFER_SIZE 512
static unsigned char disc_buffer[DISC_BUFFER_SIZE];
static unsigned int disc_index=0;

static void close_conn(struct tcp_pcb *socket)
{
    tcp_arg(socket, NULL);
    tcp_sent(socket, NULL);
    tcp_recv(socket, NULL);
    tcp_close(socket);
}

static err_t disc_received(void *arg, struct tcp_pcb *socket, struct pbuf *p, err_t err)
{
    int len;
    char *pc;
    struct pbuf *q;
    unsigned int bytes_to_copy=0;
    unsigned int bytes_copied;
    
    if (err == ERR_OK && p != NULL) 
    {
        // Inform TCP that we have taken the data.
        tcp_recved(socket, p->tot_len);  

        //pointer to the pay load
        pc=(char *)p->payload; 

        //size of the pay load
        len =p->tot_len; 

        // check state information
        if (tcp_write_offset==-1)
        {
            // read out tcp_write_offset
            tcp_write_offset  = (*pc++<< 0);
            tcp_write_offset |= (*pc++<< 8);
            tcp_write_offset |= (*pc++<<16);
            tcp_write_offset |= (*pc++<<24);
            
            tcp_bytes_to_write  = (*pc++<< 0);
            tcp_bytes_to_write |= (*pc++<< 8);
            tcp_bytes_to_write |= (*pc++<<16);
            tcp_bytes_to_write |= (*pc++<<24);
            
            len-=8;
            p->len-=8;
            
            tcp_write_index=0;
            
            debug_printf("Discovery, ready to receive %d byte of web pages\n", tcp_bytes_to_write);
            
            if (tcp_bytes_to_write>FLASH_WEB_LENGTH)
            {
            	debug_printf("ERROR: web page flash overrun of %d bytes\n", tcp_bytes_to_write-FLASH_WEB_LENGTH);
            	tcp_dump_all=1;
            }
            else
            {            
	            // erase flash
	            debug_printf("Erasing Flash...");
	            FlashEraseArea(FLASH_WEB, tcp_bytes_to_write);
	            debug_printf("Done\n");

	            disc_index=0;
            }
        }
        
        if (tcp_dump_all==0)
        {        
	        // write bytes to flash
	        debug_printf("\rWriting: %d%%", (tcp_write_index*100)/tcp_bytes_to_write);

	        for(q=p; q!=NULL; q=q->next)
	        {
	        	bytes_copied = 0;

	        	while (bytes_copied < q->len)
	        	{
	        		bytes_to_copy = DISC_BUFFER_SIZE-disc_index;
	        		if (bytes_to_copy > (q->len-bytes_copied)) bytes_to_copy = q->len-bytes_copied;

	        		memcpy(disc_buffer+disc_index, ((unsigned char*)q->payload)+bytes_copied, bytes_to_copy);
	        		disc_index+=bytes_to_copy;
	        		bytes_copied+=bytes_to_copy;

	        		if (disc_index>=DISC_BUFFER_SIZE)
	        		{
	        			FlashWrite(FLASH_WEB+tcp_write_offset+tcp_write_index, disc_buffer, disc_index);
	        			tcp_write_index+=disc_index;
	        			disc_index=0;
	        		}
	        	}
	        }
	        
	        if ((tcp_write_index+disc_index)>=tcp_bytes_to_write)
	        {
	        	if (disc_index>0)
	        	{
	        		FlashWrite(FLASH_WEB+tcp_write_offset+tcp_write_index, disc_buffer, disc_index);
					disc_index=0;
					tcp_write_index+=disc_index;
	        	}

	        	debug_printf("\nDone\n");
	            tcp_write(socket, (char*)&tcp_bytes_to_write, 4, 0);	// send notification that we're done
	            fs_init(); // reinit filesystem
	        }
        }

        tcp_sent(socket, NULL);
    }

    if (p!=NULL)
    {
        pbuf_free(p);
    }

    if (err == ERR_OK && p == NULL)
    {
        close_conn(socket);
    }

    return ERR_OK;
}

static err_t disc_accept(void *arg, struct tcp_pcb *socket, err_t err)
{
    // clear state 
    tcp_write_offset=-1;
	tcp_dump_all=0;
	
    tcp_setprio(socket, TCP_PRIO_MIN);
    tcp_recv(socket, disc_received);
    tcp_err(socket, NULL);
    tcp_poll(socket, NULL, 4);
    return ERR_OK;
}

void discovery_init(void)
{
    // setup UDP server for Discovery Requests
    udp_socket = udp_new();
    udp_bind(udp_socket, IP_ADDR_ANY, DISCOVERY_IN_PORT);
    udp_recv(udp_socket, DataReceived, NULL);
    
    // setup TCP server for Programming Requests
    tcp_socket = tcp_new();
    tcp_bind(tcp_socket, IP_ADDR_ANY, 24);
    tcp_socket = tcp_listen(tcp_socket);
    tcp_accept(tcp_socket, disc_accept); 
    
}

/******************************************************************************

FUNCTION NAME: DataReceived

PURPOSE: Receives UDP data from command or query

INPUTS:
    NONE

OUTPUTS:
    NONE

NOTES:
    NOTES TO CLARIFY FUNCTION

******************************************************************************/
//void DataReceived( UDPStackStructure * Connection )
void DataReceived(void *arg, struct udp_pcb *socket, struct pbuf *p, struct ip_addr *addr, u16_t port)
{
    _discovery_command * DiscoveryPacket;
    struct netif *network_interface;
//    struct pbuf *p_out;
//    unsigned int Length;
//    int i;
//    char TempIP[4];

    unsigned long TempIP;
    
    if (p==NULL)
    {
        debug_printf("Discovery: DataReceived called with NULL pbuf!\r\n");
        return;
    }

    debug_printf("Received Discovery Packet from %d.%d.%d.%d\r\n", ip4_addr1(addr),ip4_addr2(addr),ip4_addr3(addr),ip4_addr4(addr));
    
    if (p->len != sizeof(_discovery_command))
    {
        debug_printf("Error, incorrect number of bytes expected %d got %d\r\n", sizeof(_discovery_command), p->len);
        return;
    }
    
    DiscoveryPacket=p->payload;
    
    
    switch( DiscoveryPacket->command )
    {
        case DISCOVERY_QUERY:
        {
            debug_printf("QUERY\r\n");
            network_interface = netif_find("en0");
            DiscoveryPacket->command = DISCOVERY_QUERY_REPLY;
            memcpy(DiscoveryPacket->IP, &network_interface->ip_addr, 4);
            memcpy(DiscoveryPacket->NM, &network_interface->netmask, 4);
            memcpy(DiscoveryPacket->GW, &network_interface->gw, 4);
            memcpy(DiscoveryPacket->MAC, network_interface->hwaddr, 6);
            strcpy(DiscoveryPacket->Version, DEVICE_NAME);
            
            udp_sendto(socket,p,IP_ADDR_BROADCAST, DISCOVERY_OUT_PORT);
            
            break;
        }
        
        case DISCOVERY_SET:
        {
            debug_printf("Command = %d\r\n", DiscoveryPacket->command);
            debug_printf("IP: %d.%d.%d.%d\r\n", DiscoveryPacket->IP[0], DiscoveryPacket->IP[1], DiscoveryPacket->IP[2],DiscoveryPacket->IP[3]);
            debug_printf("NM: %d.%d.%d.%d\r\n", DiscoveryPacket->NM[0], DiscoveryPacket->NM[1], DiscoveryPacket->NM[2],DiscoveryPacket->NM[3]);
            debug_printf("GW: %d.%d.%d.%d\r\n", DiscoveryPacket->GW[0], DiscoveryPacket->GW[1], DiscoveryPacket->GW[2],DiscoveryPacket->GW[3]);
            debug_printf("MAC: %2x:%2x:%2x:%2x:%2x:%2x\r\n", DiscoveryPacket->MAC[0], DiscoveryPacket->MAC[1], DiscoveryPacket->MAC[2],
                                                        DiscoveryPacket->MAC[3], DiscoveryPacket->MAC[4], DiscoveryPacket->MAC[5]);
    
          
            TempIP=lwIPLocalIPAddrGet();
            if (memcmp(&TempIP,DiscoveryPacket->ToIP,4)==0)
            {
                debug_printf("Successful SET\r\n");            
                memcpy(net_params.net.ip, DiscoveryPacket->IP, 4);
                memcpy(net_params.net.nm, DiscoveryPacket->NM, 4);
                memcpy(net_params.net.gw, DiscoveryPacket->GW, 4);
                save_net_params();
            }
            else
            {
                debug_printf("SET not for us\r\n");
            }
            break;
        }
    }
    
    pbuf_free(p);
}

/***   End Of File   ***/
