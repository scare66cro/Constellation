// Standard Includes
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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
#include "driverlib/spi.h"
#include "http_fs.h"

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
tBoolean			WaitFor1063Dreq(int timeOutTicks);
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

int vs1063_read_data(unsigned char* buf, unsigned int buf_avail_bytes)
{
	static unsigned int words_remaining = 0;
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
		buf[byte_count++] = data >> 8;
		buf[byte_count++] = data & 0xFF;
	}
	
	return byte_count;
}

void vs1063_sine_test_start(void)
{
	if(recording_started || sine_test_started)
	{
		return;
	}
	
	sine_test_started = 1;
	
	vs1063_hw_init();
	
	dprintf("Beginning sine test\r\n");
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
	
	dprintf("Ending sine test\r\n");
	
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
	
	delay_ms(100);
	CODEC_RESET(1);
	SysCtlDelay(542535);
	    
	if(!(WaitFor1063Dreq(10)))
	{
		dprintf("DREQ Timeout - Codec reset assertion in profile init\r\n");
		return;
	}
	
	vs1063_load_patch();
	
	// Set 1063 clock multiplier, 0xC000 = 4.5 x 12.288MHz = 55.296 MHz 
	Write1063Sci(SCIR_CLOCKF, 0xC000);
	if(!WaitFor1063Dreq(100))
	{
		dprintf("DREQ timeout, set clock\r\n");
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
			dprintf("Invalid encoder quality...\r\n");
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
			dprintf("Invalid gain type\r\n");
	}
	
	Write1063Sci(SCIR_AICTRL0, aictrl0);
	Write1063Sci(SCIR_AICTRL1, aictrl1);
	Write1063Sci(SCIR_AICTRL2, aictrl2);
	Write1063Sci(SCIR_AICTRL3, aictrl3);
	Write1063Sci(SCIR_WRAMADDR, wramaddr);

	init_complete = 1;
}

void vs1063_start_recording(void)
{
	dprintf("start recording\r\n");
	
	if(!init_complete)
	{
		dprintf("VS1063 not ready for recording...\r\n");
		return;
	}
	
	if(!(WaitFor1063Dreq(100)))
	{
		dprintf("DREQ timeout, before start recording\r\n");
		return;
	}
	
	if(recording_started || sine_test_started)
	{
		dprintf("recording already started...\r\n");
		
		return;
	}
	
	recording_started = 1;
	
	dprintf("Sending start commands to vs1063\r\n");
	
	Write1063Sci(SCIR_MODE, Read1063Sci(SCIR_MODE) | SMF_ENCODE);
	Write1063Sci(SCIR_AIADDR, 0x50);
	
	dprintf("done\r\n");
	
	WaitFor1063Dreq(100);
}

void vs1063_stop_recording(void)
{
	unsigned int words_waiting;
	
	if(recording_started == 0)
	{
		return;
	}
	
	dprintf("Stopping stream\r\n");
	
	Write1063Sci(SCIR_MODE, Read1063Sci(SCIR_MODE) | SMF_CANCEL);
	
	delay_ms(200);
	
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
	unsigned long ulCharRx;
	dprintf("Init vs1063 - ");
	SysCtlDelay(542535);
	
	init_complete = 0;
	recording_started = 0;

	/** Set up GPIO pints **/ 
     //
     // Enable the GPIO bus used to interface to the SSI controller and CS pins.
     //
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOH);
    
    // Setup reset pin
    configure_out(CODEC_VLSK_RST_PIN, CODEC_VLSK_RST_PORT, CODEC_VLSK_RST_PERIPH, 0);
    // Hold the VS1063 in reset
    CODEC_RESET(0);
    
    // Configure the Config interface chip select pin
    configure_out(CODEC_CS_PIN, CODEC_CS_PORT,CODEC_CS_PERIPH, 1);
    // Drive it inactive high.
    CODEC_CS(1);

	// Configure the Data interface chip select pin
    configure_out(CODEC_XDCS_PIN,    CODEC_XDCS_PORT, CODEC_XDCS_PERIPH,1);
    // Drive it inactive high.
    CODEC_XDCS(1);

	// Configure DREQ in
    configure_in(CODEC_DREQ_PIN, CODEC_DREQ_PORT, CODEC_DREQ_PERIPH);
    
     //
     // The SSI1 peripheral must be enabled for use.
     //
    SysCtlPeripheralEnable(SYSCTL_PERIPH_SSI1);
	/** END Set up GPIO pints **/ 

	/** Set up SPI port pints **/
	
    //
    // Configure pins for SPI/SSI clock, tx, and rx
    //
    GPIOPinConfigure(GPIO_PH4_SSI1CLK);
    GPIOPinConfigure(GPIO_PH7_SSI1TX);
    GPIOPinConfigure(GPIO_PH6_SSI1RX);

	//
	// Set the GPIO pin type as SPI/SSI
	//
    GPIOPinTypeSSI(GPIO_PORTH_BASE, GPIO_PIN_4 | GPIO_PIN_6 | GPIO_PIN_7);

    //
    // Configure and enable the SSI port for SPI master mode.  Use SSI0,
    // system clock supply, idle clock level low and active low clock in
    // freescale SPI mode, master mode, 1MHz SSI frequency, and 8-bit data.
    // For SPI mode, you can set the polarity of the SSI clock when the SSI
    // unit is idle.  You can also configure what clock edge you want to
    // capture data on.  Please reference the datasheet for more information on
    // the different SPI modes. MOTO_MODE_1 for late data from chip.
    //
    SSIConfigSetExpClk(SSI1_BASE, SysCtlClockGet(), SSI_FRF_MOTO_MODE_0,  SSI_MODE_MASTER, (3 * 1000 * 1000) /*2000000*/, 8);

    //
    // Enable the SSI1 module.
    //
    SSIEnable(SSI1_BASE);
	
	// Release reset of the VS1063
	CODEC_RESET(1);
	
	// Delay 100 milliseconds
	delay_ms(100);
	    
	if(!(WaitFor1063Dreq(10)))
	{
		dprintf("DREQ Timeout - Codec reset assertion\r\n");
		return 0;
	}

    //
    // Read any residual data from the SSI port.  This makes sure the receive
    // FIFOs are empty, so we don't read any unwanted junk.  This is done here
    // because the SPI SSI mode is full-duplex, which allows you to send and
    // receive at the same time.  The SSIDataGet function returns
    // "true" when data was returned, and "false" when no data was returned.
    // The "non-blocking" function checks if there is any data in the receive
    // FIFO and does not "hang" if there isn't.
    //
    while(SSIDataGetNonBlocking(SSI1_BASE, &ulCharRx))
    {
    }
	
	ulCharRx = Read1063Sci(SCIR_STATUS);
	
	dprintf("vs1063 status: 0x%08X\r\n", ulCharRx);
	
	if (((ulCharRx >> 4) & 0xF) != 6)
	{		
		dprintf("Unable to detect VS1063 codec.\r\n");
		codec_found = 0;	
	}
	else
	{
		dprintf("VS1063 codec found.\r\n");
		codec_found = 1;
	}
	
	return codec_found;
}

void vs1063_uart_print_settings(void)
{
	unsigned long temp;
	
	temp = Read1063Sci(SCIR_MODE);
	dprintf("Mode: 0x%x\r\n", temp);
	
	temp = Read1063Sci(SCIR_STATUS);
	dprintf("VLSI Chip Version (6 = 1063): %d\r\n", (temp & 0xF0) >> 4);
	dprintf("Status: 0x%x\r\n", temp);
	
	temp = Read1063Sci(SCIR_CLOCKF);
	dprintf("Clock: 0x%x\r\n", temp);
	
	temp = Read1063Sci(SCIR_AICTRL0);
	dprintf("AICTRL0: 0x%x\r\n", temp);
	
	temp = Read1063Sci(SCIR_AICTRL1);
	dprintf("AICTRL1: 0x%x\r\n", temp);
	
	temp = Read1063Sci(SCIR_AICTRL2);
	dprintf("AICTRL2: 0x%x\r\n", temp);
	
	temp = Read1063Sci(SCIR_AICTRL3);
	dprintf("AICTRL3: 0x%x\r\n", temp);
	
	temp = Read1063Sci(SCIR_WRAMADDR);
	dprintf("SCIR_WRAMADDR: 0x%x\r\n", temp);
}

void Write1063Sci(int regNo, unsigned long value)
{
    CODEC_CS(0);
    SSI1_xfer(0x02);
    SSI1_xfer(0xFF & regNo);
    SSI1_xfer(0xFF & (value >> 8));
    SSI1_xfer(0xFF & value);
    CODEC_CS(1);
}

unsigned long Read1063Sci(int regNo)
{
  unsigned long tempvalue = 0;

    CODEC_CS(0);
    SSI1_xfer(0x03);
    SSI1_xfer(0xFF & regNo);
    tempvalue = 0xFF & SSI1_xfer(0xFF);
    tempvalue <<= 8;
    tempvalue |= 0xFF & SSI1_xfer(0xFF);
    CODEC_CS(1);
    
    return(tempvalue);
}

tBoolean WaitFor1063Dreq(int timeOutTicks)
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
