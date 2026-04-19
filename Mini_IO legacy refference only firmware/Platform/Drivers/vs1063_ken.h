#ifndef __VS1063_H__
#define __VS1063_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include "inc/hw_types.h"

typedef enum
{
	ENCODER_160K_48K_STEREO	= 1,
	ENCODER_128K_32k_STEREO = 2,
	ENCODER_160K_48K_MONO	= 3,
	ENCODER_64K_32K_MONO	= 4,
}
encoder_quality_t;

typedef enum
{
	ENCODER_GAIN_TYPE_AUTO		= 1,
	ENCODER_GAIN_TYPE_MANUAL	= 2,
}
encoder_gain_type_t;

typedef enum
{
	ENCODER_SAMPLERATE_48K	= 48000U,
	ENCODER_SAMPLERATE_32K	= 32000U,
}
encoder_samplerate_t;

typedef enum
{
	ENCODER_BITRATE_MODE_QUALITY	= 0x0000,
	ENCODER_BITRATE_MODE_VBR		= 0x4000,
	ENCODER_BITRATE_MODE_ABR		= 0x8000,
	ENCODER_BITRATE_MODE_CBR		= 0xC000,
}
encoder_bitrate_mode_t;

typedef enum
{
	ENCODER_BITRATE_MULTIPLIER_10X		= 0x0000,
	ENCODER_BITRATE_MULTIPLIER_100X		= 0x1000,
	ENCODER_BITRATE_MULTIPLIER_1000X	= 0x2000,
	ENCODER_BITRATE_MULTIPLIER_10000X	= 0x3000,
}
encoder_bitrate_multiplier_t;

typedef enum
{
	BITRATE_160K			= (ENCODER_BITRATE_MODE_CBR | ENCODER_BITRATE_MULTIPLIER_10000X | 16),
	BITRATE_128K			= (ENCODER_BITRATE_MODE_CBR | ENCODER_BITRATE_MULTIPLIER_1000X | 128),
	BITRATE_64K				= (ENCODER_BITRATE_MODE_CBR | ENCODER_BITRATE_MULTIPLIER_1000X | 64), 
}
encoder_bitrates_t;

typedef enum
{
	ENCODER_FORMAT_MP3		= (0x6 << 4)
}
encoder_format_t;

typedef enum
{
	ENCODER_CHANNELS_STEREO	= (0x0 << 0),
	ENCODER_CHANNELS_MONO	= (0x4 << 0),
}
encoder_channels_t;

char vs1063_hw_init(void);
void vs1063_sine_test_start(void);
void vs1063_sine_test_stop(void);
void vs1063_profile_init(encoder_quality_t encoder_quality, encoder_gain_type_t encoder_type, unsigned int gain);
void vs1063_start_recording(void);
void vs1063_stop_recording(void);
int vs1063_read_data(unsigned char* databuf, unsigned int bufsize);
void vs1063_uart_print_settings(void);
char vs1063_codec_found(void);

tBoolean WaitFor1063Dreq(int timeOutTicks);
unsigned long Read1063Sci(int regNo);

// CODEC_CS
#define CODEC_CS_PIN    	(1<<0)
#define CODEC_CS_PORT		GPIO_PORTH_BASE
#define CODEC_CS_PERIPH		SYSCTL_PERIPH_GPIOH

#define CODEC_CS(a)  GPIOPinWrite(CODEC_CS_PORT, CODEC_CS_PIN, (((a)==0)?0:CODEC_CS_PIN));

// CODEC_XDCS
#define CODEC_XDCS_PIN		(1<<1)
#define CODEC_XDCS_PORT		GPIO_PORTH_BASE
#define CODEC_XDCS_PERIPH	SYSCTL_PERIPH_GPIOH

#define CODEC_XDCS(a)  GPIOPinWrite(CODEC_XDCS_PORT, CODEC_XDCS_PIN, (((a)==0)?0:CODEC_XDCS_PIN));

// CODEC_FLSI_RST
#define CODEC_VLSK_RST_PIN		(1<<2)
#define CODEC_VLSK_RST_PORT 	GPIO_PORTH_BASE
#define CODEC_VLSK_RST_PERIPH	SYSCTL_PERIPH_GPIOH

#define CODEC_RESET(a)  GPIOPinWrite(CODEC_VLSK_RST_PORT, CODEC_VLSK_RST_PIN, (((a)==0)?0:CODEC_VLSK_RST_PIN));

// CODEC_DREQ
#define CODEC_DREQ_PIN			(1<<3)
#define CODEC_DREQ_PORT			GPIO_PORTH_BASE
#define CODEC_DREQ_PERIPH		SYSCTL_PERIPH_GPIOH

#define CODEC_DREQ()  ((GPIOPinRead(CODEC_DREQ_PORT, CODEC_DREQ_PIN)&CODEC_DREQ_PIN)?1:0)

#ifdef __cplusplus
}
#endif

#endif /*VS1063_H_*/
