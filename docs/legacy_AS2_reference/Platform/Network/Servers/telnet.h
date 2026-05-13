// telnet.h

#ifndef TELNET_H
#define TELNET_H

#include "lwip/tcp.h"

#define MAX_COMMAND_LENGTH 50
#define MAX_ARGUMENT_LIST 10

typedef struct
{
    char *command[MAX_ARGUMENT_LIST];
    unsigned int  num_args;
}_telnet_command;

typedef void(*telnet_command_fn)(struct tcp_pcb *socket, _telnet_command *command);

void telnet_init(telnet_command_fn cb, unsigned int telnet_timeout_sec);


#endif
/***   End Of File   ***/
