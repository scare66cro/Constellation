// khttpd.c

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "HTTP_fs/http_fs.h"
#include "lwip/tcp.h"
#include "tools.h"
#include "system.h"
#include "khttpd.h"

#define MAX_FILE_NAME_LEN 255
#define MAX_VAL_LEN 255
#define MAX_KHTTPDS 20
#define GET_FLAG    0x01
#define POST_FLAG   0x02

#define KHTTPD_BUF_SIZE 2920
#define TIME_STAMP_SIZE 50

unsigned char khttpd_buf[KHTTPD_BUF_SIZE];
char khttpd_name[MAX_FILE_NAME_LEN];
char khttpd_val[MAX_VAL_LEN];

#define KHTTPD_MAX_HEADER 1000
unsigned char khttpd_timeStamp[TIME_STAMP_SIZE];
unsigned char khttpd_headercp[KHTTPD_MAX_HEADER]; //Max header length for now
char khttpd_name_cpy[MAX_FILE_NAME_LEN]; //Max header length for now
unsigned char khttpd_header_buf[KHTTPD_MAX_HEADER];


//Header Dispositions
#define BROWSER_CACHE_LONGTERM 0
#define USE_ETAG_CACHE_SHORTTERM 1
#define NODYNAMIC_HDR_FILE 255


typedef struct
{
    char in_use;
    char index;
    unsigned int content_length;
    char flags;
    _fs_file *fd;
    int retries;

}_khttpd;

_khttpd khttpds[MAX_KHTTPDS];

static void khttpd_send_data(_khttpd *khttpd, struct tcp_pcb *socket);
static void khttpd_close(_khttpd *khttpd, struct tcp_pcb *socket);

void diag_khttpd_status(void)
{
	int i;
	debug_printf("   'In Use'    'Flags'    'fd'\r\n");
	for(i=0; i<MAX_KHTTPDS; i++)
	{
		debug_printf("%2d     %d        0x%02X        %d\r\n", i, khttpds[i].in_use, khttpds[i].flags, khttpds[i].fd);
	}
}

_khttpd *khttpd_malloc(void)
{
    int i;

    for(i=0; i<MAX_KHTTPDS; i++)
    {
        if (khttpds[i].in_use==0)
        {
            khttpds[i].in_use=1;
            khttpds[i].content_length=0;
            khttpds[i].flags=0;
            khttpds[i].fd=NULL;
            khttpds[i].retries=0;
            return &khttpds[i];
        }
    }
    return NULL;
}

void khttpd_free(_khttpd* khttpd)
{
    int i;

	//debug_printf("Closing KHTTPD\n");

    for(i=0; i<MAX_KHTTPDS; i++)
    {
        if (&khttpds[i]==khttpd)
        {
            khttpds[i].in_use=0;
            khttpds[i].flags=0;
            if (khttpds[i].fd!=NULL) fs_close(khttpds[i].fd, NULL);
            khttpds[i].fd=NULL;
            return;
        }
    }
}

static void khttpd_close(_khttpd *khttpd, struct tcp_pcb *socket)
{	
	if (socket != NULL)
    {
    	tcp_arg(socket, NULL);
	    tcp_sent(socket, NULL);
	    tcp_recv(socket, NULL);
	    tcp_poll(socket, NULL, 0);
	    tcp_err(socket, NULL);
	    tcp_close(socket);
    }

    khttpd_free(khttpd);
}

static int extract_str(struct pbuf *p)
{
    struct pbuf *q;
    int i;
    int got_d = 0;
    int buf_index=0;
    
    for(q=p; q!=NULL; q=q->next)
    {
        // we're looking for a 0x0a 0x0d pair
        for(i=0; i<q->len; i++)
        {
            // got it!
            if ((((char*)q->payload)[i]==0x0a)&&(got_d==1))
            {
                i++;
                memcpy(khttpd_buf+buf_index, q->payload, i);
                buf_index+=i;
                q->payload = (char*)q->payload +i;
                q->len-=i;
                q->tot_len-=i;
                if (q!=p) p->tot_len-=i;
                buf_index-=2; // take care of \r\n
                khttpd_buf[buf_index]='\0';
                return buf_index;
            }
            else if (((char*)q->payload)[i]==0x0d)
            {
                got_d=1;
            }
            else
            {
                got_d=0;
            }
        }
        
        memcpy(khttpd_buf+buf_index, q->payload, q->len);
        buf_index+=q->len;
        q->tot_len-=q->len;
        if (q!=p) p->tot_len-=q->len;        
        q->len=0;
    }
    return -1;
}

static err_t khttpd_poll(void *arg, struct tcp_pcb *socket)
{
    _khttpd *khttpd = arg;

    if ((arg == NULL) && (socket->state == ESTABLISHED)) 
    {
        debug_printf("poll abort\n");
        tcp_abort(socket);
        return ERR_ABRT;
    } 
    else 
    {
    	//debug_printf("KHTTPD: %d - POLL\r\n", khttpd->index);
    	
        if (khttpd->retries++>=20)
        {
        	//debug_printf("KHTTPD: %d - Retries>4, closing\r\n", khttpd->index);
            khttpd_close(khttpd, socket);
            return ERR_OK;
        }

        if (khttpd!=NULL)
        {
            if (khttpd->fd!=NULL)
            {
                if (khttpd->fd->write==0)
                {
                    khttpd_send_data(khttpd, socket);
                }
            }
        }
    }

    return ERR_OK;
}

static unsigned char *scrub_http_str(unsigned char *in)
{
	unsigned char *p1=in;
	unsigned char *p2=in;
	while(*p1!='\0')
	{
		if (*p1=='+')
		{
			*p2++=' ';
			p1++;
		}
		else if (*p1=='%')
		{
			*p2++=(quick_hex(p1[1])<<4)|(quick_hex(p1[2]));
			p1+=3;
		}
		else
		{
			*p2++=*p1++;
		}
	}
	*p2='\0';
	return in;
}

static err_t khttpd_process_header(_khttpd *khttpd, struct pbuf *p)
{
    int len;
    char *p1;
    char *p2;
    char *p3;
    char *p4;
    
    do
    {
        len=extract_str(p);
        if (len>0)
        {
            p1=strtok((char*)khttpd_buf, " ");
            
            if(strlen(p1) == len) // if there is no header value, just header title...
            {
            	continue;
            }
            
            p2=strtok(NULL, "\0");
            to_lower(p1);
            
            // do the if/else with strcmps
            if (strcmp(p1, "get")==0)
            {
                khttpd->flags |= GET_FLAG;
              
                if (p2[0]=='/') p2++;
                p3=strstr(p2, "?");
                if (p3==NULL)
                {
                    p3=strstr(p2, " ");
                    if (p3==NULL) return ERR_VAL;
                    *p3='\0';
                }
                else
                {
                    *p3='\0';
                    p3++;
                    p4=strstr(p3, " ");
                    if (p4==NULL) return ERR_VAL;
                    *p4='\0';
                }
                p3=(char*)scrub_http_str((unsigned char*)p3);
                strncpy((char*)khttpd_name, (const char*)p2, MAX_FILE_NAME_LEN);
                strncpy((char*)khttpd_val, (const char*)p3, MAX_VAL_LEN);
                return ERR_OK;
            }
            else if (strcmp(p1, "post")==0)
            {
                khttpd->flags |= POST_FLAG;
                khttpd->content_length = 0;
                
                if (p2[0]=='/') p2++;
                p3=strstr(p2, "?");
                if (p3==NULL)
                {
                    p3=strstr(p2, " ");
                    if (p3==NULL) return ERR_VAL;
                    *p3='\0';
                }
                else
                {
                    *p3='\0';
                    p3++;
                    p4=strstr(p3, " ");
                    if (p4==NULL) return ERR_VAL;
                    *p4='\0';
                }
                p3=(char*)scrub_http_str((unsigned char*)p3);
                strncpy((char*)khttpd_name, (const char*)p2, MAX_FILE_NAME_LEN);
                strncpy((char*)khttpd_val, (const char*)p3, MAX_VAL_LEN);
            }
            else if (strcmp(p1, "content-length:")==0)
            {
                khttpd->content_length=atoi(p2);
            }

            		}
        else if (len<0)
        {
        	debug_printf("extract didn't find valid \\r\\n\n");
            return ERR_VAL;
        }
    }while(len > 0);

    return ERR_OK;
}

static err_t khttpd_sent(void *arg, struct tcp_pcb *socket, u16_t len)
{
    _khttpd *khttpd;

    khttpd=(_khttpd*)arg;
  
    if (khttpd==NULL) return ERR_OK;
    khttpd_send_data(khttpd, socket);

    return ERR_OK;
}

static void khttpd_send_data(_khttpd *khttpd, struct tcp_pcb *socket)
{
    int length;
    int bytes_read;
    
    if (khttpd->fd==NULL) return; 
    length = tcp_sndbuf(socket);

    if (length>KHTTPD_BUF_SIZE) length=KHTTPD_BUF_SIZE;

	bytes_read=fs_read(khttpd->fd, khttpd_buf, length);
	if (bytes_read>0)
	{
        khttpd->retries=0;
        if (tcp_write(socket, khttpd_buf, bytes_read, TCP_WRITE_FLAG_COPY)!=ERR_OK)
        {
            debug_printf("Unreading %d bytes\n", bytes_read);
            fs_unread(khttpd->fd, bytes_read);
        }
        tcp_output(socket);
	}
	else if (bytes_read<0)
	{
	    khttpd_close(khttpd, socket);
	}
}

static err_t khttpd_post_request(_khttpd *khttpd, struct tcp_pcb *socket, struct pbuf *p)
{
    struct pbuf *q;

    if (khttpd->fd==NULL) khttpd->fd=fs_open(khttpd_name, 1, khttpd_val, khttpd->content_length); // open for writing
    if (khttpd->fd==NULL)
    {
        debug_printf("Error opening CGI file for writing!\n");
        return ERR_ABRT;
    }

	for(q=p; q!=NULL; q=q->next)
	{
		fs_write(khttpd->fd, q->payload, q->len, khttpd->content_length);
		khttpd->content_length-=q->len;
	}

	if (p->tot_len>0) khttpd->retries=0;
    
    if (khttpd->content_length==0)
    {
        fs_close(khttpd->fd, khttpd_val);
        khttpd->fd=fs_open(khttpd_name, 0, khttpd_val, 0);
        if (khttpd->fd==NULL)
        {
        	debug_printf("Error opening CGI file for reading!\n");
        	return ERR_OK;
        }

        khttpd_send_data(khttpd, socket);
        tcp_sent(socket, khttpd_sent);
    }
    
    return ERR_OK;
}

static err_t khttpd_get_request(_khttpd *khttpd, struct tcp_pcb *socket)
{
    if (khttpd_name[0]=='\0') khttpd->fd=fs_open("index.html", 0, NULL,       0);
    else                      khttpd->fd=fs_open(khttpd_name,  0, khttpd_val, 0);
    if (khttpd->fd==NULL)
    {
    	khttpd->fd=fs_open("404.html", 0, NULL, 0);
    }

    khttpd_send_data(khttpd, socket);
    tcp_sent(socket, khttpd_sent);
    return ERR_OK;
}

static err_t khttpd_received(void *arg, struct tcp_pcb *socket, struct pbuf *p, err_t err)
{
    _khttpd *khttpd=(_khttpd*)arg;
    err_t ret;
    if ((err == ERR_OK) && (p != NULL) && (khttpd!=NULL)) 
    {
        // Inform TCP that we have taken the data.
        tcp_recved(socket, p->tot_len);  

        if (khttpd->fd==NULL) // new connection
        {
        	khttpd->flags=0;
        	
            if (khttpd_process_header(khttpd, p)!=ERR_OK)
            {
                pbuf_free(p);
                khttpd_close(khttpd, socket);
                debug_printf("process header failed, closing socket\n");
                return ERR_OK;
            }
            
            switch(khttpd->flags)
            {
                case GET_FLAG:
                    ret= khttpd_get_request(khttpd,socket);
                    break;
            
                case POST_FLAG:
                    ret= khttpd_post_request(khttpd,socket,p);
                    break;

                default:
                    debug_printf("KHTTPD: unknown flag 0x%x\n", khttpd->flags);
                    ret = ERR_OK;
                    break;
            }
            
            pbuf_free(p);
            return ret;
        } // end new connection
        else
        {
            switch(khttpd->flags)
            {
                case POST_FLAG:
                    ret= khttpd_post_request(khttpd,socket,p);
                    break;
                    
                default:
                    err = ERR_OK;
                    break;              
            }
            
            pbuf_free(p);
            return ret;
        }

    } // end new data

    if (p!=NULL) pbuf_free(p);
    
    if (err == ERR_OK && p == NULL)
    {
        khttpd_close(khttpd, socket);
    }
    
    return ERR_OK;
}

void khttpd_err(void *arg, err_t err)
{
	_khttpd *khttpd = (_khttpd*)arg;
	debug_printf("khttpd_err: %d\r\n", khttpd->index);
	khttpd_close(khttpd, NULL);
}

static err_t khttpd_accept(void *arg, struct tcp_pcb *socket, err_t err)
{
    _khttpd *khttpd = khttpd_malloc();
    
    if (socket == NULL)
    {
    	debug_printf("accept socket null\r\n");	
    }

    if (khttpd==NULL)
    {
        debug_printf("khttpd: cannot accept new connection: out of memory\n");
        diag_khttpd_status();
        return ERR_MEM;
    }
    tcp_setprio(socket, TCP_PRIO_MIN);
    tcp_sent(socket, NULL);
    tcp_arg(socket, khttpd);
    tcp_err(socket, khttpd_err);
    tcp_poll(socket, khttpd_poll, 0);
    tcp_recv(socket, khttpd_received);
    return ERR_OK;
}

void khttpd_init(void)
{
    int i;
    struct tcp_pcb *pcb;

    for(i=0; i<MAX_KHTTPDS; i++)
    {
    	khttpds[i].index=i;
        khttpds[i].in_use=0;
        khttpds[i].flags=0;
        khttpds[i].fd=NULL;
    }
    
    pcb = tcp_new();
    tcp_bind(pcb, IP_ADDR_ANY, 80);
    pcb = tcp_listen(pcb);
    tcp_accept(pcb, khttpd_accept);
}


/***   End Of File   ***/
