// ssh.c

/***********   INCLUDES *******************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "inc/hw_types.h"
#include "driverlib/ethernet.h"

#include "lwip/tcp.h"

#include "debug.h"
#include "circular_queue.h"
#include "tools.h"
#include "random.h"

/***********   DEFINES *******************/

const unsigned char *kex_algo[]= {"diffie-hellman-group1-sha1","diffie-hellman-group14-sha1"};
const unsigned char *host_key_algo[]={"ssh-rsa","ssh-dss"};
const unsigned char *encryption_algo[]={"aes128-cbc","3des-cbc","aes192-cbc","aes256-cbc"};
const unsigned char *mac_algo[]={"hmac-md5","hmac-sha1","hmac-sha1-96","hmac-md5-96"};
const unsigned char *compression_algo[]={"none"};
const unsigned char *languages[]={""};

#define NO_MAC -1

#define SSH_IDENT "SSH-2.0-bnSSH_1.0 BNET 1.0\r\n"

#define SSH_BUFFER_SIZE 1000

// messages codes
//  1-19  Transport layer

//  20-29  Algorithm Negotiation
#define KEX_INIT  20

//  30-49  Key Exchange specifics
//  50-59  User authentication generic
//  60-79  User authentication method specific
//  80-89  Connection protocol generic
//  90-127 Channel related messages
// 128-191 Reserved
/***********   STRUCTURES *******************/
#define WAIT_IDENT      51
#define WAIT_KEX_INIT   52

typedef struct
{
    unsigned int  *packet_length;
    unsigned char *padding_length;
    unsigned char *payload;
    unsigned char *padding;
    unsigned char *mac;
    unsigned int   length;
    unsigned char  data[SSH_BUFFER_SIZE];
}_ssh_data;

typedef struct
{
    unsigned char state;
    _ssh_data msg;
}_ssh;

// SSH packet
//unsigned int  packet_length;  // length of entire packet, not including packet_length or mac
//unsigned char padding length;
//unsigned char message_code;
//unsigned char *data;
//unsigned char *padding; // random, 4-255 bytes, makes entire packet size multiple of cipher block or 8, whicherver is larger
//unsigned char *mac    
    

/***********   Local Function Prototypes *******************/
// TCP level stuffs
static err_t ssh_received(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err);
static err_t ssh_accept(void *arg, struct tcp_pcb *pcb, err_t err);
static err_t ssh_sent(void *arg, struct tcp_pcb *pcb, u16_t len);
static err_t ssh_poll(void *arg, struct tcp_pcb *pcb);
static err_t ssh_send_bytes(struct tcp_pcb *pcb);
static void ssh_close(_ssh *ssh, struct tcp_pcb *pcb);

// SSH handshake stuffs
static err_t ssh_send_ident(_ssh *ssh, struct tcp_pcb *pcb);
static err_t ssh_send_kex_exchange(_ssh *ssh, struct tcp_pcb *pcb);


/***********   LOCAL VARIABLES *******************/



/***********   FUNCTION DEFINITIONS *******************/
void ssh_init(void)
{
    struct tcp_pcb * pcb = tcp_new();
    
    pcb = tcp_new();
    tcp_bind(pcb, IP_ADDR_ANY, 22);
    pcb = tcp_listen(pcb);
    tcp_accept(pcb, ssh_accept); 
}


// TCP Stuffs
static void ssh_close(_ssh *ssh, struct tcp_pcb *pcb)
{
    tcp_arg(pcb, NULL);
    tcp_sent(pcb, NULL);
    tcp_close(pcb);
    if (ssh!=NULL) free(ssh);
}

static err_t ssh_received(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    _ssh *ssh = (_ssh*)arg;
    int close_session=0;;

    if ((err==ERR_OK) && (p!=NULL) && (ssh!=NULL))
    {
        // Inform TCP that we have taken the data.
        tcp_recved(pcb, p->tot_len);  

        switch(ssh->state)
        {
            case WAIT_IDENT:
                // ident should be 'SSH-2.0-' and no more than 255 bytes with no \0
                // there could be multiple lines before the SSH- line
                while(pbuf_gets(ssh->msg.data,p,SSH_BUFFER_SIZE)>0)
                {
                    // assume that 2.x is backwards compatiable
                    if (strncmp((const char*)ssh->msg.data, "SSH-2.",6)==0)
                    {
                        debug_printf("Got SSH Ident '%s'\n", ssh->msg.data);
                        // go to next state
                        ssh_send_kex_exchange(ssh,pcb);
                        ssh->state=WAIT_KEX_INIT;
                    }
                }
                break;

            case WAIT_KEX_INIT:
              debug_printf(" got a kex init packet, I think\n");
              close_session=1;
              break;
            
            default:
                close_session=1;
                break;
        }
          
       
    }

    if ((close_session)||((err==ERR_OK) && (p==NULL)))
    {
        ssh_close(ssh,pcb);
    }

    pbuf_free(p);
    return ERR_OK;
}

static err_t ssh_sent(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    return ssh_send_bytes(pcb);
}

static err_t ssh_poll(void *arg, struct tcp_pcb *pcb)
{
    return ssh_send_bytes(pcb);
}

static err_t ssh_send_bytes(struct tcp_pcb *pcb)
{
    unsigned int byte_to_send=0;
    
    if (byte_to_send>0)
    {
        tcp_sent(pcb, ssh_sent);
    }
    else
    {
        tcp_sent(pcb, NULL);
    }
    return ERR_OK;
}


static err_t ssh_accept(void *arg, struct tcp_pcb *pcb, err_t err)
{
    _ssh *ssh;
    
    ssh=malloc(sizeof(_ssh));
    if (ssh==NULL) return ERR_MEM;

    tcp_arg(pcb, ssh);    
    tcp_setprio(pcb, TCP_PRIO_MAX);
    tcp_recv(pcb, NULL);
    tcp_err(pcb, NULL);
    tcp_sent(pcb, NULL);
    tcp_poll(pcb, NULL, 1);
    
    // send ident
    return ssh_send_ident(ssh,pcb);
}


// SSH Stuffs
static err_t ssh_send_ident(_ssh *ssh, struct tcp_pcb *pcb)
{
    err_t ret;
    ret = tcp_write(pcb, SSH_IDENT, strlen(SSH_IDENT), 0);
    if (ret==ERR_OK)
    {
        ssh->state=WAIT_IDENT;
        tcp_recv(pcb, ssh_received);
    }
    
    return ret;
}

static unsigned int add_string(unsigned char *data, const unsigned char **string_array, unsigned int num_elements)
{
    int i;
    unsigned int len;
    unsigned char *p1;
    
    p1=data+4; // skip 4 bytes for length
    for(i=0; i<num_elements; i++)
    {
        sprintf((char*)p1,"%s,",string_array[i]);
        p1+=strlen((char*)p1);
    }

    len=strlen((char*)data+4)-1;
    *((unsigned int *)data)=htonl(len); // -1 for last comma
    return len+4;
}


static unsigned int ssh_transport(_ssh *ssh, struct tcp_pcb *pcb, unsigned int length, unsigned char cipher_size, int mac_algo)
{
    int i;
    unsigned int random;
    // calculate padding first, must be 4-255 bytes
    // and make the entire packet an even multiple of cipher_size
    // packet length = 
    //   length - passed in, payload length
    //   4      - packet_length
    //   1      - padding_length
    //   4      - minimum padding length
    // NOTE: since MAC isn't encrypted it doesnt' need to be in the padding calculation
    *ssh->msg.padding_length = (cipher_size-(cipher_size%(length+9)));
    if (*ssh->msg.padding_length==0) *ssh->msg.padding_length=cipher_size;
    
    // padding goes right after the payload
    ssh->msg.padding=ssh->msg.data+5+length;
    for(i=0; i<(*ssh->msg.padding_length/4); i++)
    {
        random=rand();
        memcpy(ssh->msg.padding+(i+4), &random, 4);
    }
     
    random=rand();
    memcpy(ssh->msg.padding+(i+4), &random, *ssh->msg.padding_length%4);
    
    // calculate packet length
    ssh->msg.length=5+length+*ssh->msg.padding_length;
    *ssh->msg.packet_length=htonl(ssh->msg.length-4);
    
    // add mac_algo
    if (mac_algo!=NO_MAC)
    {
        // not implemented
    }
    
    tcp_write(pcb, ssh->msg.data, ssh->msg.length, TCP_WRITE_FLAG_COPY);
    
    return 0;
}

static err_t ssh_send_kex_exchange(_ssh *ssh, struct tcp_pcb *pcb)
{
    int i;
    unsigned char *p1;
    
    //  4 bytes payload size, not including these 4 bytes
    //  1 byte num padding
    ssh->msg.packet_length=(unsigned int*)ssh->msg.data;
    ssh->msg.padding_length=ssh->msg.data+4;
    ssh->msg.payload=ssh->msg.data+5;
    p1=ssh->msg.payload;
    
    //  1 byte 0x14 (20d) Key Exchange Init code
    *p1=KEX_INIT;
    p1++;
    
    // 16 byte random cookie
    for(i=0; i<4; i++)
    {
        ((unsigned int *)p1)[i]=rand();
    }
    p1+=16;
    // next packets all are 4 bytes for length and
    // ASCII algorithms listing, not NULL terminated and
    // length doesn't include the 4 bytes for the length
  
    //  key_algorithms
    p1+=add_string(p1, kex_algo, sizeof(kex_algo)/sizeof(char*));
    //  server_host_key_algorithms
    p1+=add_string(p1,host_key_algo, sizeof(host_key_algo)/sizeof(char*));
    //  encryption_algorithms_client_to_server
    //  encryption_algorithms_server_to_client
    p1+=add_string(p1,encryption_algo,sizeof(encryption_algo)/sizeof(char*));
    p1+=add_string(p1,encryption_algo,sizeof(encryption_algo)/sizeof(char*));
    //  mac_algorithms_client_to_server
    //  mac_algorithms_server_to_client
    p1+=add_string(p1,mac_algo,sizeof(mac_algo)/sizeof(char*));
    p1+=add_string(p1,mac_algo,sizeof(mac_algo)/sizeof(char*));
    //  compression_algorithms_client_to_server
    //  compression_algorithms_server_to_client
    p1+=add_string(p1,compression_algo,sizeof(compression_algo)/sizeof(char*));
    p1+=add_string(p1,compression_algo,sizeof(compression_algo)/sizeof(char*));
    //  languages_client_to_server
    //  languages_server_to_client
    p1+=add_string(p1,languages,sizeof(languages)/sizeof(char*));
    p1+=add_string(p1,languages,sizeof(languages)/sizeof(char*));
    
    //   1 byte, 0x00, KEX First Packet Follows
    *p1=0x0;
    p1++;
    //   4 bytes, 0x00, reserved
    memset(p1,0,4);
    p1+=4;
    
    //  xx bytes, padding
    // padding bytes need to be 4-255, random bytes
    // such that the length of the entire packet is
    // a multiple of 8 or the cipher block size
    // since we don't encrypt this packet it needs to be 8
    
    // ssh_transport will fill out:
    // packet_length,
    // padding info
    // mac info
    // and then send the packet
    return ssh_transport(ssh, pcb, p1-ssh->msg.payload, 8, NO_MAC);
}


/***   End Of File   ***/
