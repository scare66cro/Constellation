// flash.c

#include "flash.h"

extern int micron_FlashInit(void);
extern void micron_FlashEraseArea(unsigned int addr, unsigned int length);
extern int micron_FlashWrite(unsigned int to, unsigned char *from, unsigned int length);
extern unsigned int micron_FlashRead(unsigned char *to, unsigned int from, unsigned int length);

extern int issi_FlashInit(void);
extern void issi_FlashEraseArea(unsigned int addr, unsigned int length);
extern int issi_FlashWrite(unsigned int to, unsigned char *from, unsigned int length);
extern unsigned int issi_FlashRead(unsigned char *to, unsigned int from, unsigned int length);

extern int tiva_FlashInit(void);
extern void tiva_FlashEraseArea(unsigned int addr, unsigned int length);
extern int tiva_FlashWrite(unsigned int to, unsigned char *from, unsigned int length);
extern unsigned int tiva_FlashRead(unsigned char *to, unsigned int from, unsigned int length);


#include "debug.h"
#include "FreeRTOS.h"
#include "task.h"

#include "issi_flash.h"

static unsigned int doing_micron=0;
static unsigned int doing_issi=0;

int FlashInit(void)
{
	doing_micron=0;
	doing_issi=0;

	if (micron_FlashInit()==0) doing_micron=1;
//  else if (issi_FlashInit()==0) doing_issi=1;
	else if (issi_flash_init()==0) doing_issi=1;
	return tiva_FlashInit();
}

void FlashEraseArea(unsigned int addr, unsigned int length)
{
    taskENTER_CRITICAL();
	if (IS_TIVA(addr))
	{
		tiva_FlashEraseArea(CLEAR_TIVA(addr), length);
	}
	else
	{
		if (doing_micron) micron_FlashEraseArea(addr, length);
//    else if (doing_issi) issi_FlashEraseArea(addr, length);
		else if (doing_issi) issi_flash_erase(addr, length);
	}
	taskEXIT_CRITICAL();
}

int FlashWrite(unsigned int to, unsigned char *from, unsigned int length)
{
    int ret=0;
    taskENTER_CRITICAL();
	if (IS_TIVA(to))
	{
		ret = tiva_FlashWrite(CLEAR_TIVA(to), from, length);
	}
	else
	{
		if (doing_micron) ret = micron_FlashWrite(to, from, length);
//		else if (doing_micron) ret = issi_FlashWrite(to, from, length);
		else if (doing_issi) ret = issi_flash_write(from, to, length);
	}
	taskEXIT_CRITICAL();
	return ret;
}


unsigned int FlashRead(unsigned char *to, unsigned int from, unsigned int length)
{
    unsigned int ret=0;
    taskENTER_CRITICAL();
	if (IS_TIVA(from))
	{
		ret = tiva_FlashRead(to, CLEAR_TIVA(from), length);
	}
	else
	{
		if (doing_micron) ret = micron_FlashRead(to, from, length);
//    else if (doing_issi) ret = issi_FlashRead(to, from, length);
		else if (doing_issi) ret = issi_flash_read(to, from, length);
	}
	taskEXIT_CRITICAL();
	return ret;
}


/***   End Of File   ***/
