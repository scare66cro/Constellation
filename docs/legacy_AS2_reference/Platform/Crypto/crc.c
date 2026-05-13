// crc.c

#include "crc.h"

void crc_init(_crc_struct *crc_struct, unsigned short poly, unsigned short init_val)
{
	crc_struct->poly=poly;
	crc_struct->crc=init_val;
}

void crc_calc(_crc_struct *crc_struct, unsigned char *buffer, unsigned int length)
{
	int i;
	int j;
	
	for(i = 0; i<length; i++)  // Bytes in a buffer.
    {
        crc_struct->crc ^= buffer[i] << 8;  //There is a compiler bug that doesnt allow the data fetch to occur in the same instruction with the ^.
        
        for (j = 0; j < 8; j++)
        {
            if ((crc_struct->crc & 0x8000) == 0x8000)
            {
            	crc_struct->crc<<=1;
                crc_struct->crc ^= crc_struct->poly;
            }
            else
            {
            	crc_struct->crc<<=1;
            }
        }
	}
}

/***   End Of File   ***/
