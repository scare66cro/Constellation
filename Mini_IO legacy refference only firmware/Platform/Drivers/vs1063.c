// Standard Includes
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

#include "vs1063.h"
#include "vs1063_encpatch.h"
#include "system.h"

// File Includes
#include "inc/hw_types.h"
#include "inc/hw_memmap.h"
#include "driverlib/gpio.h"
#include "driverlib/ssi.h"
#include "utils/uartstdio.h"
#include "tools.h"

#define SCIR_MODE        0
#define SCIR_STATUS      1
#define SCIR_BASS        2
#define SCIR_CLOCKF      3
#define SCIR_DECODE_TIME 4
#define SCIR_AUDATA      5
#define SCIR_WRAM        6
#define SCIR_WRAMADDR    7
#define SCIR_HDAT0       8
#define SCIR_HDAT1       9
#define SCIR_AIADDR     10
#define SCIR_VOL        11
#define SCIR_AICTRL0    12
#define SCIR_AICTRL1    13
#define SCIR_AICTRL2    14
#define SCIR_AICTRL3    15

#define SMF_MPEGII			(1<<1)
#define SMF_RESET			(1<<2)
#define SMF_CANCEL			(1<<3)
#define SMF_TESTS			(1<<5)
#define SMF_SDINEW			(1<<11)
#define SMF_ENCODE			(1<<12)
#define SMF_LINE1			(1<<14)
// reserved in vs1063 #define SMF_STREAM			(1<<6)  // Only for MP3/WAV playback.
#define VS1063_INT_ENABLE 	(0xC01A)
#define INT_EN_SCI			(1<<1)
#define INT_EN_SDI			(1<<2)

#define min(a,b) (((a)<(b))?(a):(b))

void				Write1063Sci(int regNo, unsigned long value);
unsigned long		Read1063Sci(int regNo);
unsigned char		WaitFor1063Dreq(int timeOutTicks);
void				vs1063_uart_print_settings(void);
void				start_recording(void);
void				vs1063_load_patch(void);

unsigned int init_complete = 0;
unsigned char recording_started = 0;
unsigned char sine_test_started = 0;

char codec_found = 0;

char vs1063_codec_found(void)
{
	return codec_found != 0;
}

static unsigned int total_bytes=0;

int vs1063_read_data(unsigned char* buf, unsigned int buf_avail_bytes)
{
	unsigned int words_to_get;
	unsigned int i;
	unsigned int data;

	//if (total_bytes>=10000) return -1;

	words_to_get = Read1063Sci(SCIR_HDAT1);
	//debug_printf("words_to_get = %d : %d\r\n", words_to_get, buf_avail_bytes/2);

	if (words_to_get==3712) debug_printf("\r\n!!!!!\r\n!!!!! Max words to get, possible overrun\r\n!!!!!\r\n");

	if ((words_to_get*2)>buf_avail_bytes)words_to_get = buf_avail_bytes/2;

	for(i=0; i<words_to_get; i++)
	{
		data = Read1063Sci(SCIR_HDAT0);
		buf[i*2] = data >> 8;
		buf[(i*2)+1] = data & 0xFF;
	}

	//debug_printf("Read %d bytes\r\n", words_to_get*2);
	total_bytes+=(words_to_get*2);
	return words_to_get*2;


	/*static unsigned int words_remaining = 0;
	unsigned int words_count_timeout;
	unsigned int words_to_get;
	unsigned int words_temp;
	unsigned int byte_count = 0;
	unsigned long data;
	
	words_count_timeout = 0xFFFFFF;
	

	if(words_remaining > 0)
	{
		words_to_get = min(words_remaining, (buf_avail_bytes / 2));
		
		words_remaining -= words_to_get;
	}
	else
	{
		do
		{
			words_temp = Read1063Sci(SCIR_HDAT1);
			debug_printf("wt: %d\r\n", words_temp);
		}
		while(words_temp == 0 && --words_count_timeout > 0);
		
		if (words_count_timeout == 0)
		{			
			return 0;
		}
		
		words_to_get = min(words_temp, (buf_avail_bytes / 2));
		
		words_temp -= words_to_get;
		
		if(words_temp > 0)
			words_remaining = words_temp;
	}
	
	while(words_to_get--)
	{
		data = Read1063Sci(SCIR_HDAT0);
		//debug_printf("%X ", data);
		buf[byte_count++] = data >> 8;
		buf[byte_count++] = data & 0xFF;
	}
	
	debug_printf("read %d bytes\r\n", byte_count);

	return byte_count;*/
}


static unsigned char _SSIDataXfer(unsigned char value)
{
	unsigned int ret;
	SSIDataPut(CODEC_BASE, value);
	SSIDataGet(CODEC_BASE, &ret);
	return ret;
}

void vs1063_sine_test_start(void)
{
	if(recording_started || sine_test_started)
	{
		return;
	}
	
	sine_test_started = 1;
	
	vs1063_hw_init();
	
	debug_printf("Beginning sine test\r\n");
	Write1063Sci(SCIR_AUDATA, 0xBB81);		// 48KHz samle rate, stereo
	Write1063Sci(SCIR_AICTRL0, 0x0AA8);		// Frequency calc'd for 2KHz, Left channel
	Write1063Sci(SCIR_AICTRL1, 0x0AA8);		// ... Right channel
	Write1063Sci(SCIR_AIADDR, 0x4020);
}

void vs1063_sine_test_stop(void)
{
	if(sine_test_started != 1)
	{
		return;
	}
	
	debug_printf("Ending sine test\r\n");
	
	vs1063_hw_init();
	
	sine_test_started = 0;
}



void vs1063_profile_init(encoder_quality_t encoder_quality, encoder_gain_type_t encoder_type, unsigned int gain)
{
	static unsigned int aictrl0;
	static unsigned int aictrl1;
	static unsigned int aictrl2;
	static unsigned int aictrl3;
	static unsigned int wramaddr;

	debug_printf("VS1063: Profile Init...");

	vTaskDelay(100/portTICK_RATE_MS);
	CODEC_RESET(1);
	SysCtlDelay(542535);
	    
	if(!(WaitFor1063Dreq(10)))
	{
		debug_printf("DREQ Timeout - Codec reset assertion in profile init\r\n");
		return;
	}
	
	vs1063_load_patch();
	
	// Set 1063 clock multiplier, 0xC000 = 4.5 x 12.288MHz = 55.296 MHz 
	Write1063Sci(SCIR_CLOCKF, 0xC000);
	if(!WaitFor1063Dreq(100))
	{
		debug_printf("DREQ timeout, set clock\r\n");
		return;
	}
	
	switch (encoder_quality)
	{
		case ENCODER_160K_48K_STEREO:
			aictrl0 = ENCODER_SAMPLERATE_48K;
			aictrl3 = ENCODER_FORMAT_MP3 | ENCODER_CHANNELS_STEREO;
			wramaddr = BITRATE_160K; 
			break;
			
		case ENCODER_128K_32k_STEREO:
			aictrl0 = ENCODER_SAMPLERATE_32K;
			aictrl3 = ENCODER_FORMAT_MP3 | ENCODER_CHANNELS_STEREO;
			wramaddr = BITRATE_128K;
			break;
		
		case ENCODER_160K_48K_MONO:
			aictrl0 = ENCODER_SAMPLERATE_48K;
			aictrl3 = ENCODER_FORMAT_MP3 | ENCODER_CHANNELS_MONO;
			wramaddr = BITRATE_160K;
			break;
			
		case ENCODER_64K_32K_MONO:
			aictrl0 = ENCODER_SAMPLERATE_32K;
			aictrl3 = ENCODER_FORMAT_MP3 | ENCODER_CHANNELS_MONO;
			wramaddr = BITRATE_64K;
			break;
			
		default:
			debug_printf("Invalid encoder quality...\r\n");
	}
	
	switch (encoder_type)
	{
		case ENCODER_GAIN_TYPE_AUTO:
			aictrl1 = 0;
			aictrl2 = gain;
			break;
			
		case ENCODER_GAIN_TYPE_MANUAL:
			aictrl1 = gain;
			aictrl2 = 0;
			break;
			
		default:
			debug_printf("Invalid gain type\r\n");
	}
	
	Write1063Sci(SCIR_AICTRL0, aictrl0);
	Write1063Sci(SCIR_AICTRL1, aictrl1);
	Write1063Sci(SCIR_AICTRL2, aictrl2);
	Write1063Sci(SCIR_AICTRL3, aictrl3);
	Write1063Sci(SCIR_WRAMADDR, wramaddr);


	//Write1063Sci(SCIR_AICTRL0, 48000U);		// 48000hz samplerate
	//Write1063Sci(SCIR_AICTRL1, 1024U);		// 1x Gain
	//Write1063Sci(SCIR_AICTRL3, 0x0060);		// MP3 format, stereo
	//Write1063Sci(SCIR_WRAMADDR, 0xE0C0);	// 128 KBit/s
	
	init_complete = 1;

	debug_printf("done\r\n");
}

void vs1063_start_recording(void)
{
	debug_printf("VS1063: Start Recording...");
	
	if(!init_complete)
	{
		debug_printf("VS1063 not ready for recording...\r\n");
		return;
	}
	
	if(!(WaitFor1063Dreq(100)))
	{
		debug_printf("DREQ timeout, before start recording\r\n");
		return;
	}
	
	if(recording_started || sine_test_started)
	{
		debug_printf("recording already started...\r\n");
		
		return;
	}
	
	recording_started = 1;
	
	debug_printf("VS1063: Sending start commands\r\n");
	
	Write1063Sci(SCIR_MODE, Read1063Sci(SCIR_MODE) | SMF_ENCODE);
	Write1063Sci(SCIR_AIADDR, 0x50);
	
	WaitFor1063Dreq(100);

	debug_printf("done\r\n");

	total_bytes=0;

}

void vs1063_stop_recording(void)
{
	unsigned int words_waiting;
	
	if(recording_started == 0)
	{
		return;
	}
	
	debug_printf("VS1063: Stopping Stream\r\n");
	
	debug_printf("total_bytes = %d\r\n", total_bytes);

	Write1063Sci(SCIR_MODE, Read1063Sci(SCIR_MODE) | SMF_CANCEL);
	
	vTaskDelay(200/portTICK_RATE_MS);
	
	do
	{
		words_waiting = Read1063Sci(SCIR_HDAT1);
		
		if(words_waiting == 0)
		{
			recording_started = 0;
			
			Write1063Sci(SCIR_WRAMADDR, 0x1E06);
			
			return;
		}
		
		while(words_waiting--)
		{
			Read1063Sci(SCIR_HDAT0);
		}
	}while(1);
}

char vs1063_hw_init(void)
{
	unsigned int ulCharRx;
	debug_printf("VS1063: Init - ");
	
	CODEC_RESET(0);

	SysCtlDelay(542535);

	init_complete = 0;
	recording_started = 0;

    while(SSIDataGetNonBlocking(CODEC_BASE, &ulCharRx)){}

    // Release reset of the VS1063
	CODEC_RESET(1);

	// Delay 100 milliseconds
	vTaskDelay(100/portTICK_RATE_MS);
	    
	if(!(WaitFor1063Dreq(10)))
	{
		debug_printf("DREQ Timeout - Codec reset assertion\r\n");
		return 0;
	}
	
	while(SSIDataGetNonBlocking(CODEC_BASE, &ulCharRx))
    {
    }
	ulCharRx = Read1063Sci(SCIR_STATUS);
	
	debug_printf("status (0x%08X) - ", ulCharRx);
	
	if (((ulCharRx >> 4) & 0xF) != 6)
	{
		debug_printf("Unable to detect codec.\r\n");
		codec_found = 0;	
	}
	else
	{
		debug_printf("codec found.\r\n");
		codec_found = 1;
	}
	
	vs1063_uart_print_settings();

	return codec_found;
}

void vs1063_uart_print_settings(void)
{
	unsigned long temp;
	
	temp = Read1063Sci(SCIR_MODE);
	debug_printf("Mode: 0x%x\r\n", temp);
	
	temp = Read1063Sci(SCIR_STATUS);
	debug_printf("VLSI Chip Version (6 = 1063): %d\r\n", (temp & 0xF0) >> 4);
	debug_printf("Status: 0x%x\r\n", temp);
	
	temp = Read1063Sci(SCIR_CLOCKF);
	debug_printf("Clock: 0x%x\r\n", temp);
	
	temp = Read1063Sci(SCIR_AICTRL0);
	debug_printf("AICTRL0: 0x%x\r\n", temp);
	
	temp = Read1063Sci(SCIR_AICTRL1);
	debug_printf("AICTRL1: 0x%x\r\n", temp);
	
	temp = Read1063Sci(SCIR_AICTRL2);
	debug_printf("AICTRL2: 0x%x\r\n", temp);
	
	temp = Read1063Sci(SCIR_AICTRL3);
	debug_printf("AICTRL3: 0x%x\r\n", temp);
	
	temp = Read1063Sci(SCIR_WRAMADDR);
	debug_printf("SCIR_WRAMADDR: 0x%x\r\n", temp);
}

void Write1063Sci(int regNo, unsigned long value)
{
    CODEC_CS(0);
    _SSIDataXfer(0x02);
    _SSIDataXfer(0xFF & regNo);
    _SSIDataXfer(0xFF & (value >> 8));
    _SSIDataXfer(0xFF & value);
    CODEC_CS(1);
}

unsigned long Read1063Sci(int regNo)
{
  unsigned long tempvalue = 0;

    CODEC_CS(0);
    _SSIDataXfer(0x03);
    _SSIDataXfer(0xFF & regNo);
    tempvalue = 0xFF & _SSIDataXfer(0xFF);
    tempvalue <<= 8;
    tempvalue |= 0xFF & _SSIDataXfer(0xFF);
    CODEC_CS(1);
    
    return(tempvalue);
}

unsigned char WaitFor1063Dreq(int timeOutTicks)
{
  int i;

    for(i = 0; i < timeOutTicks; i++)
    {
        if ( 1 == CODEC_DREQ() )
            return(true);
        SysCtlDelay(SysCtlClockGet() / 10000);
    }

    return(false);
}

void vs1063_load_patch(void)
{
	int i = 0;
	unsigned short addr;
	unsigned short n;
	unsigned short val;

	while (i < sizeof(vs1063_patch_data) / sizeof(vs1063_patch_data[0]))
	{
		addr = vs1063_patch_data[i++];
		
		n = vs1063_patch_data[i++];
		if (n & 0x8000U) /* RLE run, replicate n samples */
		{ 
			n &= 0x7FFF;
			val = vs1063_patch_data[i++];
			while (n--)
			{
    			Write1063Sci(addr, val);
			}
		}
		else
		{           /* Copy run, copy n samples */
			while (n--)
			{
				val = vs1063_patch_data[i++];
				Write1063Sci(addr, val);
			}
		}
 	}
}
