#ifndef MC_RTC_H_
#define MC_RTC_H_

#ifdef __cplusplus
extern "C"
{
#endif

void rtc_init(void);
unsigned int rtc_get_timestamp(void);
void rtc_set_timestamp(unsigned int timestamp);
void rtc_set_timestamp_str(unsigned char * val);
void rtc_set_timestamp_composite(short year, char month, char day, char hour, char minute, char second);

#ifdef __cplusplus
}
#endif

#endif /*MC_RTC_H_*/
