/*=============================================================================
Copyright © 2007 Infinetix Corp.

This file is the property of Infinetix Corp and shall not be
reproduced, copied, or used as the basis for the manufacture or sale
of equipment without the express written permission of Infinetix Corp.

FILE NAME: flash.c

PURPOSE:
    Provides a library for writing for the flash pages of the AT25DF041A

NOTES:


CHANGE HISTORY:

    ***********************************************
    Revision: 2.0
    User: Ken Farr     Date: 08/08/08  Time: 11:13PM
    Reformatted the entire system to use an SPI flash device

    Revision: Initial 1.0
    User: Ken Farr     Date: 09/14/06  Time: 11:13PM
    cleaned up code, reformatted to our coding standards and ported to
    Infinetix's IPStack

=============================================================================*/

/*=============================================================================
                              Includes
=============================================================================*/

// Standard Includes
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// File Includes
//#include "flash.h"
#include "debug.h"
#include "pinout.h"
#include "driverlib/ssi.h"


#define CS_HIGH()   set_output(FLASH_CS, 1)
#define CS_LOW()    set_output(FLASH_CS, 0)

#define SPI_WRITE(a) _SSIDataXfer(SSI0_BASE, a)
#define SPI_READ()   _SSIDataXfer(SSI0_BASE, 0xFF)
#define SPI_FLUSH() _SSIFlush(SSI0_BASE)


/*=============================================================================
                              Defines
=============================================================================*/

#define RESET_ENABLE				0x66
#define RESET_MEMORY				0x99

#define READ_ID1					0x9E
#define READ_ID2					0x9F
#define MULTIPLE_IO_READ_ID			0xAF
#define READ_SERIAL_FLASH			0x5A

#define READ						0x03
#define FAST_READ					0x08

#define WRITE_ENABLE				0x06
#define WRITE_DISABLE				0x04

#define READ_STATUS_REGISTER		0x05
#define WRITE_STATUS_REGISTER		0x01
#define READ_FLAG_STATUS_REGISTER	0x70
#define CLEAR_FLAG_STATUS_REGISTER	0x50

#define SUBSECTOR_ERASE				0x20
#define	SECTOR_ERASE				0xD8

#define PAGE_PROGRAM				0x02

/*=============================================================================
                                Enums
=============================================================================*/

/*=============================================================================
                            Type Definitions
=============================================================================*/

/*=============================================================================
                              Structures
=============================================================================*/

/*=============================================================================
                          External/Public Constants
=============================================================================*/

/*=============================================================================
                          External/Public Variables
=============================================================================*/

/*=============================================================================
                            File Variables
=============================================================================*/

/*=============================================================================
                       Local Function Protoypes
=============================================================================*/

/*=============================================================================
                           Function Definitions
=============================================================================*/

unsigned char _SSIDataXfer(unsigned long base, unsigned char value)
{
	unsigned int ret;
	SSIDataPut(base, value);
	SSIDataGet(base, &ret);
	return ret;
}

void _SSIFlush(unsigned long base)
{
	unsigned int value;
	while(SSIDataGetNonBlocking(base, &value)) { }
}

void _EnableWrite(void)
{
	CS_LOW();
	SPI_WRITE(WRITE_ENABLE);
	CS_HIGH();
}

static void _DisableWrite(void)
{
	CS_LOW();
	SPI_WRITE(WRITE_DISABLE);
	CS_HIGH();
}

static unsigned char _ReadStatus(void)
{
	unsigned char ret;

	CS_LOW();
	SPI_WRITE(READ_STATUS_REGISTER);
	ret = SPI_READ();
	CS_HIGH();
	return ret;
}

static unsigned char _ReadFlagStatus(void)
{
	unsigned char ret;

	CS_LOW();
	SPI_WRITE(READ_FLAG_STATUS_REGISTER);
	ret = SPI_READ();
	CS_HIGH();
	return ret;
}

static int _ReadID(void)
{
	int i;
	unsigned int id;

	CS_LOW();

	SPI_WRITE(READ_ID2);

	for(i=0; i<4; i++)
	{
		((unsigned char*)&id)[i]=SPI_READ();
	}

	CS_HIGH();

	if (id!=0x1018BA20)
	{
		debug_printf("Flash ID %08X is not Micron.\r\n", id);
		return -1;
	}

  debug_printf("Flash ID %08X is Micron.\r\n", id);
	return 0;
}

int micron_FlashInit(void)
{
	return _ReadID();
}

static void _WritePage(unsigned int addr, unsigned char *data, unsigned int length)
{
	unsigned int i;
	unsigned char status;

	_EnableWrite();

	CS_LOW();

	SPI_WRITE(PAGE_PROGRAM);
	SPI_WRITE((addr&0x00FF0000)>>16);
	SPI_WRITE((addr&0x0000FF00)>>8);
	SPI_WRITE((addr&0x000000FF)>>0);

	for(i=0; i<length; i++)
	{
		SPI_WRITE(data[i]);
	}

	CS_HIGH();


	for(i=0; i<20000; i++)
	{
		if (((status=_ReadFlagStatus())&0x80)) break;
	}

	if ((status&0x80)==0) debug_printf("Timed out waiting for WritePage to finish\r\n");


}

void micron_FlashWrite(unsigned int addr, unsigned char *data, unsigned int length)
{
    unsigned int this_write;

    if (length==0) return;

    while(length>0)
    {
    	this_write = 256-(addr%256);
    	if (this_write>length) this_write=length;

    	_WritePage(addr, data, this_write);
    	addr+=this_write;
    	data+=this_write;
    	length-=this_write;
    }

}


unsigned int micron_FlashRead(unsigned char *to, unsigned int from, unsigned int num_bytes)
{
	int i;

	CS_LOW();

	SPI_WRITE(READ);
	SPI_WRITE((from&0x00FF0000)>>16);
	SPI_WRITE((from&0x0000FF00)>>8);
	SPI_WRITE((from&0x000000FF)>>0);

	for(i=0; i<num_bytes; i++)
	{
		to[i]=SPI_READ();
	}

	CS_HIGH();

    return i;
}


static void _EraseSubsector(unsigned int addr)
{
	unsigned int i;
	unsigned char status;

	_EnableWrite();

	CS_LOW();
	SPI_WRITE(SUBSECTOR_ERASE);
	SPI_WRITE((addr&0x00FF0000)>>16);
	SPI_WRITE((addr&0x0000FF00)>>8);
	SPI_WRITE((addr&0x000000FF)>>0);

	CS_HIGH();

	for(i=0; i<32000; i++)
	{
		if (((status=_ReadFlagStatus())&0x80)) break;
	}

	if ((status&0x80)==0) debug_printf("Timed out waiting for EraseSubsector to finish\r\n");
}

void micron_FlashEraseArea(unsigned int addr, unsigned int length)
{
	unsigned int start_page = addr/4096;
  unsigned int end_page = ((addr + length) / 4096) + (((addr + length) % 4096) ? 1 : 0);

  for (; start_page < end_page; start_page++)
	{
		_EraseSubsector(addr);
    addr += 4096;
	}
}



/***   End Of File   ***/
