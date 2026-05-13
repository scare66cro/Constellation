// smtp.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lwip/tcp.h"
#include "utils/lwiplib.h"

#include "smtp.h"
#include "system.h"

static int smtp_state=SMTP_IDLE;

// email information
static char *smtp_subject=NULL;
static char *smtp_body = NULL;
static int smtp_body_index=0;
static int smtp_body_length=0;
static char *smtp_to_addr = NULL;
static char *smtp_from_addr = NULL;


int smtp_status(void)
{
    return smtp_state;
}

static void smtp_close(struct tcp_pcb *socket)
{
    tcp_arg(socket, NULL);
    tcp_sent(socket, NULL);
    tcp_recv(socket, NULL);
    tcp_poll(socket, NULL, 0);
    tcp_close(socket);
}

static int smtp_get_code(struct pbuf *p)
{
    char *p1;
    if (p==NULL) return -1;
    if (p->len<4) return -1;
    p1=p->payload;
    p1[3]='\0';
    return atoi(p1);
}

static char smtp_buffer[1000];

err_t smtp_sent(void *arg, struct tcp_pcb *socket, u16_t space)
{
    if (smtp_state==SMTP_DATA_HEAD)
    {
        smtp_state=SMTP_DATA_BODY;
        smtp_body_index=0;
        smtp_body_length=strlen(smtp_body);
    }
    
    if (smtp_state==SMTP_DATA_BODY)
    {
        if (smtp_body_index<smtp_body_length)
        {
            if (space>smtp_body_length)
            {
                tcp_write(socket, smtp_body+smtp_body_index, smtp_body_length-smtp_body_index, 0);
                debug_printf("SENDING: %s", smtp_body+smtp_body_index);
                smtp_body_index=smtp_body_length;
            }
            else
            {
                tcp_write(socket, smtp_body+smtp_body_index, space, 0);
                debug_printf("SENDING: %s", smtp_body+smtp_body_index);
                smtp_body_index+=space;
            }
        }
        else
        {
            // we're done sending data, send the QUIT
            if (space>=5)
            {
                tcp_write(socket, "\r\n.\r\n", 5, 0);
                debug_printf("SENDING: \r\n.\r\n");
                smtp_state=SMTP_DONE;
                tcp_sent(socket, NULL);
            }
        }
    }
    else
    {
        smtp_state=SMTP_ERROR;
        smtp_close(socket);
    }
    
    return ERR_OK;
}

static err_t smtp_received(void *arg, struct tcp_pcb *socket, struct pbuf *p, err_t err)
{
    int code;
    int ip_addr;
    
    tcp_sent(socket, NULL);
    
    if (err == ERR_OK && p != NULL) 
    {
        // Inform TCP that we have taken the data.
        tcp_recved(socket, p->tot_len);  
        code=smtp_get_code(p);    
        pbuf_free(p); 
        
        switch(code)
        {
            case 220:
            {
                if (smtp_state==SMTP_CONNECTING)
                {
                    smtp_state=SMTP_GREETING;
                    ip_addr=lwIPLocalIPAddrGet();
                    sprintf(smtp_buffer, "HELO [%d.%d.%d.%d]\r\n", (ip_addr>> 0)&0xff,
                                                                   (ip_addr>> 8)&0xff,
                                                                   (ip_addr>>16)&0xff,
                                                                   (ip_addr>>24)&0xff);
                    tcp_write(socket, smtp_buffer, strlen(smtp_buffer), 0);
                    debug_printf("SENDING: %s", smtp_buffer);
                }
                else
                {
                    smtp_state=SMTP_ERROR;
                    smtp_close(socket);
                }
                break;
            }
            case 221:
            {
                if (smtp_state==SMTP_QUIT)
                {
                    smtp_state=SMTP_IDLE;
                }
                smtp_close(socket);
                break;
            }    
            case 250:
            {
                switch(smtp_state)
                {
                    case SMTP_GREETING:
                        smtp_state=SMTP_MAIL_FROM;
                        sprintf(smtp_buffer, "MAIL FROM: <%s>\r\n", smtp_from_addr);
                        tcp_write(socket, smtp_buffer, strlen(smtp_buffer), 0);
                        debug_printf("SENDING: %s", smtp_buffer);
                        break;
                        
                    case SMTP_MAIL_FROM:
                        smtp_state=SMTP_MAIL_TO;
                        sprintf(smtp_buffer, "RCPT TO: <%s>\r\n", smtp_to_addr);
                        tcp_write(socket, smtp_buffer, strlen(smtp_buffer), 0);
                        debug_printf("SENDING: %s", smtp_buffer);
                        break;
                        
                    case SMTP_MAIL_TO:
                        smtp_state=SMTP_DATA;
                        tcp_write(socket, "DATA\r\n", 6, 0);
                        debug_printf("SENDING: DATA\r\n");
                        break;
                
                    case SMTP_DONE:
                        smtp_state=SMTP_QUIT;
                        tcp_write(socket, "QUIT\r\n", 6, 0);
                        debug_printf("SENDING: QUIT\r\n");
                        break;
                    default:
                        smtp_state=SMTP_ERROR;
                        smtp_close(socket);
                        break;
                }
                break;
            }
            case 354:
            {
                if (smtp_state==SMTP_DATA)
                {
                    smtp_state=SMTP_DATA_HEAD;
                    sprintf(smtp_buffer, "From: %s\r\nTo: %s\r\nSubject: %s\r\n", smtp_from_addr, smtp_to_addr, smtp_subject);
                    tcp_write(socket, smtp_buffer, strlen(smtp_buffer), 0);
                    debug_printf("SENDING: %s", smtp_buffer);
                    
                    tcp_sent(socket, smtp_sent);
                }
                else
                {
                    smtp_state=SMTP_ERROR;
                    smtp_close(socket);
                }
                break;
            }
        }

    }
    else
    {
        pbuf_free(p);
    }

    if (err == ERR_OK && p == NULL)
    {
        smtp_close(socket);
    }

    return ERR_OK;
}

static err_t smtp_connected(void *arg, struct tcp_pcb *socket, err_t err)
{
    if (err!=ERR_OK)
    {
        debug_printf("smtp_connected called with err = %x\r\n", err);
        smtp_state=SMTP_IDLE;
    }
    else
    {
        tcp_recv(socket, smtp_received);
        tcp_sent(socket, NULL);
        tcp_err(socket, NULL);
    }
    return err;
}


int smtp_send(char *subject, char *body, char *to_addr, char *from_addr, struct ip_addr server, short server_port)
{
    struct tcp_pcb * smtp_pcb = tcp_new();

    smtp_subject=subject;
    smtp_body=body;
    smtp_to_addr=to_addr;
    smtp_from_addr=from_addr;
    smtp_body_index=0;
    
    tcp_bind( smtp_pcb, IP_ADDR_ANY, 0);
    tcp_arg(smtp_pcb, NULL);
    if (tcp_connect(smtp_pcb, &server, server_port, smtp_connected)!=ERR_OK)
    {
        return -1;
    }
  
    smtp_state=SMTP_CONNECTING;
    debug_printf("smtp_state = SMTP_CONNECTING\n");
    return 0;
}





/***   End Of File   ***/
