#ifndef CRC_H_
#define CRC_H_

typedef struct
{
	unsigned short poly;
	unsigned short crc;
}_crc_struct;

void crc_init(_crc_struct *crc_struct, unsigned short poly, unsigned short init_val);
void crc_calc(_crc_struct *crc_struct, unsigned char *buffer, unsigned int length);

#endif /*CRC_H_*/
