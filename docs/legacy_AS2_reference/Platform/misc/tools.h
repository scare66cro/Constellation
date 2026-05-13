#ifndef TOOLS_H
#define TOOLS_H

#include "lwip/pbuf.h"
#include "lwip/ip_addr.h"
#include <time.h>

int base64_encode(unsigned char * source, size_t sourcelen, char * target, size_t targetlen);

//void configure_output_pin(unsigned long periph, unsigned long port, unsigned long pin, unsigned int initial_state);

int str_to_ipstr(struct ip_addr *ip, char *str);

void array_to_pbuf(struct pbuf *p, unsigned char *data, int length);
void pbuf_to_array(unsigned char *data, struct pbuf *p);
void pbuf_set(struct pbuf *p, unsigned char val, unsigned int length);
int pbuf_gets(unsigned char *data, struct pbuf *p, unsigned int max_len);

void to_lower(char *in);

int process_csv_line(unsigned char *csv_line, unsigned char **out, unsigned int *out_lengths, unsigned int num_columns);

unsigned char quick_hex(char in);
unsigned int ahtoi(char *in);

char *ctime2(const time_t *timep);

unsigned int ntp_to_1970(unsigned int time);
void format_1970_time(unsigned int time_stamp, short *year, char *month, char *day, char *hour, char *minute, char *second);
unsigned int make_timestamp(short year, char month, char day, char hour, char minute, char second);
char *format_date_time(unsigned int timestamp, char *out, unsigned char time_fomrat);
unsigned char dotw(int month, int day, int year);


void set_str(char *to, char *from, unsigned int max_len);
void set_eid(unsigned char *to, unsigned char *from, unsigned int max);
unsigned int set_bit(unsigned int to, unsigned int from, unsigned int shift);
void set_ip(unsigned char *to, char *from);
unsigned char is_true(char *value);
int extract_float(char *data_in);

#endif
/***   End Of File   ***/
