// Telnet.c

// Standard Includes
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// File Includes
#include "debug.h"
#include "telnet.h"
#include "system.h"


static char command_buffer[MAX_COMMAND_LENGTH];
static unsigned int command_buffer_index = 0;

static err_t telnet_received(void *arg, struct tcp_pcb *socket, struct pbuf *p, err_t err);
static err_t telnet_accept(void *arg, struct tcp_pcb *socket, err_t err);
static err_t telnet_poll(void *arg, struct tcp_pcb *socket);
static unsigned int extract_command(char *buffer, _telnet_command *command);

static telnet_command_fn callback_fn=NULL;
static unsigned int timeout_sec=0;
static unsigned int last_communication_sec=0;

void telnet_init(telnet_command_fn cb, unsigned int telnet_timeout_sec)
{
	struct tcp_pcb *tcp_socket;
	callback_fn=cb;
	timeout_sec=telnet_timeout_sec;

    tcp_socket = tcp_new();
    tcp_bind(tcp_socket, IP_ADDR_ANY, 23);
    tcp_socket = tcp_listen(tcp_socket);
    tcp_accept(tcp_socket, telnet_accept); 
}


static err_t telnet_accept(void *arg, struct tcp_pcb *socket, err_t err)
{
    last_communication_sec=uptime_sec;
    command_buffer_index=0;
	
    debug_printf("Telnet connected!\r\n");
    
    tcp_setprio(socket, TCP_PRIO_MIN);
    tcp_recv(socket, telnet_received);
    tcp_err(socket, NULL);
    tcp_poll(socket, telnet_poll, 4);
    return ERR_OK;
}

static void close_conn(struct tcp_pcb *socket)
{
    tcp_arg(socket, NULL);
    tcp_sent(socket, NULL);
    tcp_recv(socket, NULL);
    tcp_close(socket);
}

static err_t telnet_poll(void *arg, struct tcp_pcb *socket)
{
	if ((uptime_sec-last_communication_sec)>=timeout_sec)
    {
        debug_printf("Telnet timed out, closing\r\n");
        close_conn(socket);
    }
    
    return ERR_OK;
}

static err_t telnet_received(void *arg, struct tcp_pcb *socket, struct pbuf *p, err_t err)
{
    struct pbuf *q;
    _telnet_command command;
    
    last_communication_sec=uptime_sec;
    
    if (err == ERR_OK && p != NULL) 
    {
        // Inform TCP that we have taken the data.
        tcp_recved(socket, p->tot_len);  
        
        if ((p->tot_len+command_buffer_index)>=MAX_COMMAND_LENGTH)
        {
        	debug_printf("Telnet buffer overflow!\r\n");
            command_buffer_index=0;
        }
        else
        {
            for(q=p; q!=NULL; q=q->next)
            {
                // Echo back
                if (q->len>0) tcp_write(socket, q->payload, q->len, TCP_WRITE_FLAG_COPY);
                memcpy(command_buffer+command_buffer_index, q->payload, q->len);
                command_buffer_index+=q->len;
            }
            
            command_buffer[command_buffer_index]='\0';
            
            if (extract_command(command_buffer, &command))
            {
            	if (callback_fn!=NULL) callback_fn(socket, &command);
                command_buffer_index=0;
            }
        }
        
        pbuf_free(p);
        
        tcp_sent(socket, NULL);
    }
    else
    {
        pbuf_free(p);
    }

    if (err == ERR_OK && p == NULL)
    {
        close_conn(socket);
    }

    return ERR_OK;
}

static unsigned int extract_command(char *buffer, _telnet_command *command)
{
    int length;
    int i = 0;

    length = strlen((char*)buffer);
    command->num_args = 0;


    if ((buffer[length-1]==0x0A) || (buffer[length-1]==0x0D))
    {
    	length--;
        buffer[length] = '\0';
        i = 1; // Mark that we have a complete command
    }

    if ((buffer[length-1]==0x0A) || (buffer[length-1]==0x0D))
    {
    	length--;
        buffer[length] = '\0';
        i = 1; // Mark that we have a complete command
    }

    // If we didn't receive an ending character
    if (i==0) return 0;

    // Get the command
    command->command[0] = buffer;
    for(i = 0; i < length; i++)
    {
        if (buffer[i]==' ')
        {
        	buffer[i] = '\0';
        	command->num_args++;

            if (command->num_args >= MAX_ARGUMENT_LIST) break;
            if ((i+1)<length) command->command[command->num_args] = &buffer[i+1];
        }
    }

    command->num_args++;

    return 1;
}


/***   End Of File   ***/
