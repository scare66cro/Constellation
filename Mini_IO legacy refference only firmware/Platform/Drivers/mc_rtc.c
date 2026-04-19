#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "mc_rtc.h"

#include "system.h"

#include "driverlib/i2c.h"

#include "pinout.h"
//#include "app_params.h"

#include "tools.h"



#define RTC_I2C_ADDRESS						(0x6F)

#define RTC_REG_SECOND						(0x00)
#define RTC_REG_BEGIN_DATETIME				(0x00)
#define RTC_REG_MINUTE						(0x01)
#define RTC_REG_HOUR						(0x02)
#define RTC_REG_DAY							(0x03)
#define RTC_REG_DATE						(0x04)
#define RTC_REG_MONTH						(0x05)
#define RTC_REG_YEAR						(0x06)
#define RTC_REG_RTCC            (0x07)

unsigned int _rtc_read_reg(unsigned int reg);
void _rtc_read_sequential(unsigned char * dest, unsigned int reg, unsigned int n);
void _rtc_write_reg(unsigned int reg, unsigned int val);
void _rtc_write_sequential(unsigned int reg, unsigned char * data, unsigned int n);

void rtc_init(void)
{
  unsigned char temp;

  temp = 0x43; // 0x40 - enable square wave clock output on pin 7 -MFP, 0x03 - freq = 32.768kHz
  _rtc_write_reg(RTC_REG_RTCC, temp);
  temp = _rtc_read_reg(RTC_REG_RTCC);

  // Check if OSC is running, start if not.
  temp = _rtc_read_reg(RTC_REG_SECOND);
  if(!(temp & 0x80))
  {
    debug_printf("RTC oscillator not running. Starting now.\r\n");
    _rtc_write_reg(RTC_REG_SECOND, (temp | 0x80));
  }
  temp = _rtc_read_reg(RTC_REG_SECOND);

  temp = _rtc_read_reg(RTC_REG_DAY);
  while ((temp & 0x20) != 0x20) // oscillator status bit - 1 = enabled and running
  {
    temp = _rtc_read_reg(RTC_REG_DAY);
  }

  temp = _rtc_read_reg(RTC_REG_DAY);
  if(!(temp & 0x08))  // external battery
  {
    debug_printf("RTC battery not enabled. Enabling now.\r\n");
    _rtc_write_reg(RTC_REG_DAY, (temp | 0x08));
  }
}

unsigned char _rtc_data[7];
unsigned int rtc_get_timestamp(void)
{
	char second;
	char minute;
	char hour;
	char day;
	char month;
	short year;
	
	_rtc_read_sequential(_rtc_data, RTC_REG_BEGIN_DATETIME, 7);
	
	second = ((((_rtc_data[0] >> 4) & 0x07) * 10) + (_rtc_data[0] & 0x0F));	// Seconds
	minute = ((((_rtc_data[1] >> 4) & 0x07) * 10) + (_rtc_data[1] & 0x0F));	// Minutes
	
	// Hours parsing (24 vs AM/PM)
	hour = (_rtc_data[2] & 0x0F);			// Hour
	
	if(_rtc_data[2] & 0x40)		// AM/PM format
	{	
		if(_rtc_data[2] & 0x20)	// if PM
		{
			if(hour < 12)		// and not noon
			{
				hour += 12;		// make 24 hour
			}  
		}
		else					// else AM
		{
			if(hour == 12)		// if midnight
			{
				hour = 0;		// zero hour
			}
		}
	}
	else						// 24 hour format
	{
		hour += ((_rtc_data[2] >> 4) & 0x03) * 10;
	}
	
	day = ((((_rtc_data[4] >> 4) & 0x03) * 10) + (_rtc_data[4] & 0x0F));		// Day of month
	month = (((_rtc_data[5] >> 4) & 0x01) * 10) + (_rtc_data[5] & 0x0F);
	year = ((_rtc_data[6] >> 4) * 10) + (_rtc_data[6] & 0x0F) + 2000;
	
//	debug_printf("rtc_get_timestamp - y/m/d h:m:s %d/%d/%d %d:%d:%d\r\n", year, month, day, hour, minute, second);
	
	return make_timestamp(year, month, day, hour, minute, second);
}

void rtc_set_timestamp_composite(short year, char month, char day, char hour, char minute, char second)
{
	unsigned char temp_second;
//	unsigned char skipped_second = 0;
	
	debug_printf("rtc_set_timestamp_composite - y/m/d h:m:s %d/%d/%d %d:%d:%d\r\n", year, month, day, hour, minute, second);
	
	// Check for minute rollover. If near a rollover, wait a second and correct time
	// Sloppy but prevents time corruption on the RTC
	temp_second = _rtc_read_reg(RTC_REG_SECOND);
	temp_second = ((((temp_second >> 4) & 0x07) * 10) + (temp_second & 0x0F));
	if(temp_second == 59)
	{
//		skipped_second = 1;
		SysCtlDelay(1000);
		second += 1;
	}
	
	_rtc_data[0] = 0x80 | ((second / 10) << 4) | (second % 10);		// Clearing 8th bit stops oscillator
	_rtc_data[1] = ((minute / 10) << 4) | (minute % 10);
	_rtc_data[2] = ((hour / 10) << 4) | (hour % 10);			// Set to 24 hour time format
	_rtc_data[4] = ((day / 10) << 4) | (day % 10);
	_rtc_data[5] = (((month / 10) << 4) * 0x01) | (month % 10);
	
	year -= 2000;
	
	_rtc_data[6] = ((year / 10) << 4) | (year % 10);
	
	_rtc_write_reg(RTC_REG_SECOND, _rtc_data[0]);
	_rtc_write_reg(RTC_REG_MINUTE, _rtc_data[1]);
	_rtc_write_reg(RTC_REG_HOUR, _rtc_data[2]);
	_rtc_write_reg(RTC_REG_DATE, _rtc_data[4]);
	_rtc_write_reg(RTC_REG_MONTH, _rtc_data[5]);
	_rtc_write_reg(RTC_REG_YEAR, _rtc_data[6]);
	
	debug_printf("RTC time set.\r\n");
	
	//if (skipped_second)
		//debug_printf("second was skipped\r\n");
}

void rtc_set_timestamp(unsigned int timestamp)
{
	short year;
	char month;
	char day;
	char hour;
	char minute;
	char second;
	
	format_1970_time(timestamp, &year, &month, &day, &hour, &minute, &second);
	
	rtc_set_timestamp_composite(year, month, day, hour, minute, second);
}

void rtc_set_timestamp_str(unsigned char * val)
{
	unsigned int temp_stamp; 
	unsigned char * token;
	short year;
	char month;
	char day;
	char hour;
	char minute;
	char second;
	
	token = (unsigned char*)strtok((char *)val, ":");
	year = atoi((char *)token);
	token = (unsigned char*)strtok(NULL, ":");
	month = atoi((char *)token);
	token = (unsigned char*)strtok(NULL, ":");
	day = atoi((char *)token);
	token = (unsigned char*)strtok(NULL, ":");
	hour = atoi((char *)token);
	token = (unsigned char*)strtok(NULL, ":");
	minute = atoi((char *)token);
	token = (unsigned char*)strtok(NULL, ":");
	second = atoi((char *)token);
	
	temp_stamp = make_timestamp(year, month, day, hour, minute, second);
	
//	temp_stamp -= app_params.timezone_offset_min* 60;
	
	rtc_set_timestamp(temp_stamp);
}

unsigned int _rtc_read_reg(unsigned int reg)
{
	// "Write" address to read to slave
	I2CMasterSlaveAddrSet(RTC_I2C_BASE, RTC_I2C_ADDRESS, 0);
  while(I2CMasterBusy(RTC_I2C_BASE)){ }
	
	I2CMasterDataPut(RTC_I2C_BASE, reg);
	I2CMasterControl(RTC_I2C_BASE, I2C_MASTER_CMD_SINGLE_SEND);
	while(I2CMasterBusy(RTC_I2C_BASE)){ }
	
	// Actually read the address now, using the *****TADA!!!!*****
	// ...Read register! What a concept.
	I2CMasterSlaveAddrSet(RTC_I2C_BASE, RTC_I2C_ADDRESS, 1);
  while(I2CMasterBusy(RTC_I2C_BASE)){ }
	
	I2CMasterDataPut(RTC_I2C_BASE, reg);
	I2CMasterControl(RTC_I2C_BASE, I2C_MASTER_CMD_SINGLE_RECEIVE);
	while(I2CMasterBusy(RTC_I2C_BASE)){ }
	
	return I2CMasterDataGet(RTC_I2C_BASE);
}

void _rtc_write_reg(unsigned int reg, unsigned int val)
{
	I2CMasterSlaveAddrSet(RTC_I2C_BASE, RTC_I2C_ADDRESS, 0);
  while(I2CMasterBusy(RTC_I2C_BASE)){ }
	
	I2CMasterDataPut(RTC_I2C_BASE, reg);
	I2CMasterControl(RTC_I2C_BASE, I2C_MASTER_CMD_BURST_SEND_START);
	while(I2CMasterBusy(RTC_I2C_BASE)) { }
	
	I2CMasterDataPut(RTC_I2C_BASE, val);
	I2CMasterControl(RTC_I2C_BASE, I2C_MASTER_CMD_BURST_SEND_FINISH);
	while(I2CMasterBusy(RTC_I2C_BASE)) { }
}

void _rtc_write_sequential(unsigned int reg, unsigned char * data, unsigned int n)
{
	unsigned int i;
	
	if(n < 2)
	{
		_rtc_write_reg(reg, *(data));
		_rtc_write_reg((reg + 1), *(data + 1));
		return;
	}
	
	I2CMasterSlaveAddrSet(RTC_I2C_BASE, RTC_I2C_ADDRESS, 0);
	
	I2CMasterDataPut(RTC_I2C_BASE, reg);
	I2CMasterControl(RTC_I2C_BASE, I2C_MASTER_CMD_BURST_SEND_START);
	while(I2CMasterBusy(RTC_I2C_BASE)) { }
	
	for(i = 1; i < n; i++)
	{
		if((i + 1) == n)
		{
			I2CMasterDataPut(RTC_I2C_BASE, *(data + i));
			I2CMasterControl(RTC_I2C_BASE, I2C_MASTER_CMD_BURST_SEND_FINISH);
		}
		else
		{
			I2CMasterDataPut(RTC_I2C_BASE, *(data + i));
			I2CMasterControl(RTC_I2C_BASE, I2C_MASTER_CMD_BURST_SEND_CONT);
		}
		
		while(I2CMasterBusy(RTC_I2C_BASE)) { }
	}
} 

void _rtc_read_sequential(unsigned char * dest, unsigned int reg, unsigned int n)
{
	unsigned int i;
	
	if (n < 2) return;
			
	// "Write" address to read to slave
	I2CMasterSlaveAddrSet(RTC_I2C_BASE, RTC_I2C_ADDRESS, 0);
	
	I2CMasterDataPut(RTC_I2C_BASE, reg);
	I2CMasterControl(RTC_I2C_BASE, I2C_MASTER_CMD_SINGLE_SEND);
	while(I2CMasterBusy(RTC_I2C_BASE)){ }
	
	// Actually read the address now, using the *****TADA!!!!*****
	// ...Read register! What a concept.
	I2CMasterSlaveAddrSet(RTC_I2C_BASE, RTC_I2C_ADDRESS, 1);
	
	I2CMasterDataPut(RTC_I2C_BASE, reg);
	I2CMasterControl(RTC_I2C_BASE, I2C_MASTER_CMD_BURST_RECEIVE_START);
	while(I2CMasterBusy(RTC_I2C_BASE)){ }
	
	*(dest) = (unsigned char)I2CMasterDataGet(RTC_I2C_BASE);
	
	for(i = 1; i < n; i++)
	{
		while(I2CMasterBusy(RTC_I2C_BASE)){ }
		
		if((i + 1) == n)
		{
			I2CMasterControl(RTC_I2C_BASE, I2C_MASTER_CMD_BURST_RECEIVE_FINISH);
		}
		else
		{
			I2CMasterControl(RTC_I2C_BASE, I2C_MASTER_CMD_BURST_RECEIVE_CONT);
		}
		
		while(I2CMasterBusy(RTC_I2C_BASE)){ }
		*(dest + i) = (unsigned char)I2CMasterDataGet(RTC_I2C_BASE);
	}
} 
