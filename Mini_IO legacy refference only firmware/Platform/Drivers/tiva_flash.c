
#include <stdbool.h>
#include <stdint.h>

#include "flash.h"
#include "driverlib/flash.h"
#include "debug.h"

int tiva_FlashInit(void)
{
	return 0;
}

void tiva_FlashEraseArea(unsigned int addr, unsigned int length)
{
	int i;
	for(i=addr; i<(addr+length); i+=16384)
	{
		FlashErase(i);
	}
}

int tiva_FlashWrite(unsigned int to, unsigned char *from, unsigned int length)
{
	if (length%4)
	{
		debug_printf("\r\n!!FIXING LENGTH!!\r\n");
		length=((length/4)+1)*4;
	}
	//debug_printf("Writing %08X %08X %d\r\n", from, to, length);
	if (length==0) return 0;
	return FlashProgram((unsigned int*)from, to, length);
}

unsigned int tiva_FlashRead(unsigned char *to, unsigned int from, unsigned int length)
{
	//debug_printf("Reading: %d from %08X\r\n", length, from);
	memcpy(to, (unsigned char*)from, length);
	return length;
}

/***   End Of File   ***/
