
#include <stdint.h>
#include <stdbool.h>
#include <string.h>


#include "inc/hw_types.h"
#include "inc/hw_memmap.h"

#include "driverlib/uart.h"
#include "utils/cmdline.h"

#include "debug.h"

#define CMD_BUF_SIZE 100

static char backspace[]={0x08, 0x20, 0x08, 0x00};
static char command_buffer[CMD_BUF_SIZE];

void debug_process_line(char *message)
{
	switch(CmdLineProcess(message))
	{
		case CMDLINE_BAD_CMD:
			debug_printf("Bad Command!\r\n");
			break;

		case CMDLINE_TOO_MANY_ARGS:
        	debug_printf("Too many arguments for command processor!\n\r");
        	break;
    }
}

void debug_periodic(void)
{
  	static unsigned int index=0;

	if (UARTCharsAvail(UART0_BASE))
	{
		command_buffer[index]=UARTCharGet(UART0_BASE);

		if ((command_buffer[index]==0x0A) || (command_buffer[index]==0x0D)) // end of line
		{
			command_buffer[index]='\0';

			if (strlen(command_buffer)>0)
			{
				debug_printf("\r\n");
				debug_process_line(command_buffer);
			}
			index=0;
			debug_printf("\r\n>> ");
			return;
		}

		if (command_buffer[index]==0x08) // backspace
		{
			if (index>0)
			{
				debug_printf(backspace);
				index--;
			}
		}
		else
		{
			debug_printf("%c", command_buffer[index]);
			index++;
			if (index>=CMD_BUF_SIZE)
			{
				debug_printf("Command buffer exceeded\r\n");
				index=0;
			}
		}
	}
}


/***   End Of File   ***/
