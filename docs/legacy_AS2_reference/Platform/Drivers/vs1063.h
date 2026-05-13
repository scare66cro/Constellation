#ifndef __VS1063_H__
#define __VS1063_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include "pinout.h"

typedef enum
{
	ENCODER_160K_48K_STEREO	= 1,
	ENCODER_128K_32k_STEREO = 2,
	ENCODER_160K_48K_MONO	= 3,
	ENCODER_64K_32K_MONO	= 4,
} encoder_quality_t;

typedef enum
{
	ENCODER_GAIN_TYPE_AUTO		= 1,
	ENCODER_GAIN_TYPE_MANUAL	= 2,
} encoder_gain_type_t;

typedef enum
{
	ENCODER_SAMPLERATE_48K	= 48000U,
	ENCODER_SAMPLERATE_32K	= 32000U,
} encoder_samplerate_t;

typedef enum
{
	ENCODER_BITRATE_MODE_QUALITY	= 0x0000,
	ENCODER_BITRATE_MODE_VBR		= 0x4000,
	ENCODER_BITRATE_MODE_ABR		= 0x8000,
	ENCODER_BITRATE_MODE_CBR		= 0xC000,
} encoder_bitrate_mode_t;

typedef enum
{
	ENCODER_BITRATE_MULTIPLIER_10X		= 0x0000,
	ENCODER_BITRATE_MULTIPLIER_100X		= 0x1000,
	ENCODER_BITRATE_MULTIPLIER_1000X	= 0x2000,
	ENCODER_BITRATE_MULTIPLIER_10000X	= 0x3000,
} encoder_bitrate_multiplier_t;

typedef enum
{
	BITRATE_160K			= (ENCODER_BITRATE_MODE_CBR | ENCODER_BITRATE_MULTIPLIER_10000X | 16),
	BITRATE_128K			= (ENCODER_BITRATE_MODE_CBR | ENCODER_BITRATE_MULTIPLIER_1000X | 128),
	BITRATE_64K				= (ENCODER_BITRATE_MODE_CBR | ENCODER_BITRATE_MULTIPLIER_1000X | 64), 
} encoder_bitrates_t;

typedef enum
{
	ENCODER_FORMAT_MP3		= (0x6 << 4)
} encoder_format_t;

typedef enum
{
	ENCODER_CHANNELS_STEREO	= (0x0 << 0),
	ENCODER_CHANNELS_MONO	= (0x4 << 0),
} encoder_channels_t;

char vs1063_hw_init(void);
void vs1063_sine_test_start(void);
void vs1063_sine_test_stop(void);
void vs1063_profile_init(encoder_quality_t encoder_quality, encoder_gain_type_t encoder_type, unsigned int gain);
void vs1063_start_recording(void);
void vs1063_stop_recording(void);
int vs1063_read_data(unsigned char* databuf, unsigned int bufsize);
void vs1063_uart_print_settings(void);
char vs1063_codec_found(void);

unsigned char WaitFor1063Dreq(int timeOutTicks);
unsigned long Read1063Sci(int regNo);

#define CODEC_CS(a) 	set_output(XCS, a)
#define CODEC_XDCS(a)  	set_output(XDCS, a)
#define CODEC_RESET(a)  set_output(VLSI_RST, a)
#define CODEC_DREQ()  	read_input(DREQ)
#define CODEC_BASE		SSI1_BASE

#ifdef __cplusplus
}
#endif

#endif /*VS1063_H_*/
