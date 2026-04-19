// tools.c

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "inc/hw_types.h"
#include "inc/hw_memmap.h"
#include "inc/hw_gpio.h"
#include "driverlib/gpio.h"
#include "driverlib/sysctl.h"
#include "lwip/pbuf.h"
#include "tools.h"
#include "debug.h"

const char * base64_lookup = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void _base64_encode_triple(unsigned char * triple, char * result)
{
	int tripleValue, i;

	tripleValue = triple[0];
	tripleValue *= 256;
	tripleValue += triple[1];
	tripleValue *= 256;
	tripleValue += triple[2];

	for (i=0; i<4; i++)
	{
		result[3-i] = base64_lookup[tripleValue%64];
		tripleValue /= 64;
    }
}

int base64_encode(unsigned char * source, size_t sourcelen, char * target, size_t targetlen)
{
	/* check if the result will fit in the target buffer */
	if ((sourcelen+2)/3*4 > targetlen-1)
		return 0;

	/* encode all full triples */
	while (sourcelen >= 3)
	{
		_base64_encode_triple(source, target);
		sourcelen -= 3;
		source += 3;
		target += 4;
	}

	/* encode the last one or two characters */
	if (sourcelen > 0)
	{
		unsigned char temp[3];
		memset(temp, 0, sizeof(temp));
		memcpy(temp, source, sourcelen);
		_base64_encode_triple(temp, target);
		target[3] = '=';

		if (sourcelen == 1)
			target[2] = '=';

		target += 4;
	}

	/* terminate the string */
	target[0] = 0;

	return 1;
}

//void configure_output_pin(unsigned long periph, unsigned long port, unsigned long pin, unsigned int initial_state)
//{
//	SysCtlPeripheralEnable(periph);
//	GPIOPadConfigSet(port, pin, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD);
//	GPIOPinWrite(port, pin, initial_state?pin:0);
//	GPIODirModeSet(port, pin, GPIO_DIR_MODE_OUT);
//}

int process_csv_line(unsigned char *csv_line, unsigned char **out, unsigned int *out_lengths, unsigned int num_columns)
{
	char *p1;
	int i;
	
	for(i=0, p1=strtok((char*)csv_line, ","); (i<num_columns)&&(p1!=NULL); i++, p1=strtok(NULL, ","))
	{
		strncpy((char*)out[i], p1, out_lengths[i]);
	}
	
	return i;
}

int str_to_ipstr(struct ip_addr *ip, char *str)
{
	char a;
	char b;
	char c;
	char d;
	
	a=atoi(strtok(str,"."));
	b=atoi(strtok(NULL,"."));
	c=atoi(strtok(NULL,"."));
	d=atoi(strtok(NULL,"."));
	
	IP4_ADDR(ip, a,b,c,d);
	return 1;
}

void pbuf_set(struct pbuf *p, unsigned char val, unsigned int length)
{
    struct pbuf *q;
    
    for(q=p; q!=NULL; q=q->next)
    {
        if (q->len<=length) memset(q->payload, val, q->len);
        else                memset(q->payload, val, length);
        length-=q->len;
        if (length==0) return;
    }
}


int pbuf_gets(unsigned char *data, struct pbuf *p, unsigned int max_len)
{
    struct pbuf *q;
    int i;
    int got_d;
    int index=0;
    
    for(q=p; q!=NULL; q=q->next)
    {
        // we're looking for a 0x0a 0x0d pair
        got_d=0;
        for(i=0; i<q->len; i++)
        {
            // got it!
            if ((((char*)q->payload)[i]==0x0a)&&(got_d==1))
            {
                i++;
                if ((index+i)>=max_len) memcpy(data+index, q->payload, max_len-index);
                else                    memcpy(data+index, q->payload, i);
                index+=i;
                q->payload = (char*)q->payload +i;
                q->len-=i;
                q->tot_len-=i;
                if (q!=p) p->tot_len-=i;
                index-=2; // take care of \r\n
                if (index>=max_len) data[max_len-1]='\0';
                else                data[index]='\0';
                return index;
            }
            else if (((char*)q->payload)[i]==0x0d)
            {
                got_d=1;
            }
            else
            {
                got_d=0;
            }
        }
        
        memcpy(data+index, q->payload, q->len);
        index+=q->len;
        q->tot_len-=q->len;
        if (q!=p) p->tot_len-=q->len;        
        q->len=0;
    }
    return -1;
}


void pbuf_to_array(unsigned char *data, struct pbuf *p)
{
    struct pbuf *q;
    for(q=p; q!=NULL; q=q->next)
    {
        memcpy(data, q->payload, q->len);
        data+=q->len;
    }
}

void array_to_pbuf(struct pbuf *p, unsigned char *data, int length)
{
    struct pbuf *q;
    int index=0;
    
    for(q=p; q!=NULL; q=q->next)
    {
        if (q->len<=(length-index))
        {
            memcpy(q->payload, data+index, q->len);
            index+=q->len;
        }
        else
        {
            memcpy(q->payload, data+index, (length-index));
            index=length;
        }
        if (index==length) return;
    }
}


void to_lower(char *in)
{
    while(*in!='\0')
    {
        if ((*in>='A')&&(*in<='Z')) *in+=32;
        in++;
    }
}

unsigned char quick_hex(char in)
{
    if      ((in>='0')&&(in<='9'))  return in-48;
    else if ((in>='A')&&(in<='Z'))  return in-55;
    else                            return in-87;
}

unsigned int ahtoi(char *in)
{
    unsigned int ret=0;
    while(*in!='\0')
    {
        ret<<=4;
        ret|=(quick_hex(*in));
        in++;
    }
    return ret;
}

int dayInYear(int dd, int mm)
{
	switch(mm)
   	{
    	case 12:dd += 30;
    	case 11:dd += 31;
    	case 10:dd += 30;
    	case 9:dd += 31;
    	case 8:dd += 31;
    	case 7:dd += 30;
    	case 6:dd += 31;
    	case 5:dd += 30;
    	case 4:dd += 31;
    	case 3:dd += 28;
    	case 2:dd += 31;
   	}
   	return dd;
}

static int calcDay_Dec31(int yyyy)
{
	int dayCode = 0;
	dayCode = ((yyyy-1)*365 + (yyyy-1)/4 - (yyyy-1)/100 + (yyyy-1)/400) % 7;
	return dayCode;
	
} // end calcDay_Dec31(...)

// returns day of the week
unsigned char dotw(int month, int day, int year)
{
	unsigned int days;
	
    days = calcDay_Dec31(year);

    days = (dayInYear(day, month) + days) % 7;

    if ((!(year % 4) && (year % 100) || !(year % 400)) && month > 2)
    {
        days++;
    }
    
    days %= 7;

    return days;
    	
} // end CalculateDayOfTheWeek(...)

int day_in_leap_year(int month, int day, int year)
{
	int days =dayInYear(day, month);

    if ((!(year % 4) && (year % 100) || !(year % 400)) && month > 2)
    {
        days++;
    }
	return days;
}

char *dotw_string[]={"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
char *month_string[]={"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static char ctime_str[100];
char *ctime2(const time_t *timep)
{
	short year;
	char month;
	char day;
	char hour;
	char minute;
	char second;
	
	format_1970_time(*timep, &year, &month, &day, &hour, &minute, &second);
	
	sprintf(ctime_str, "%s %s %2d %02d:%02d:%02d %d\n", dotw_string[dotw(month,day,year)],
	                                                    month_string[month-1],
	                                                    day, hour, minute, second, year);
	return ctime_str;
}

time_t mktime(struct tm *tm)
{
	return make_timestamp(tm->tm_year+1900, tm->tm_mon, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
}

static short days_in_month[ 12 ] = {  0,  31,  59,  90, 120, 151, 181, 212, 243, 273, 304, 334};

unsigned int make_timestamp(short year, char month, char day, char hour, char minute, char second)
{
    int days;
    int i;
    int leaps;
    unsigned int ret=second;
    
    ret += minute * 60;
    ret += hour * 3600;
    
    // Now we calculate what day we are on in the year
    days=days_in_month[month-1]+day-1;
    
    // Check if we are past Feb and if it is a leap year
    if (month>2)
    {
        // leap years are years that are divisiable by 4 and not divisiable by 100 unless it's divisiable by 400
        if (((year%4)==0) && ( ((year%100)>0) || ((year%400)==0)))
        {
            // it's a leap year and we're past Feb, add one day
            days++;
        }
    }

    ret += days * 86400;
    
    // Now calculate full years taking into account leap years, we're only going back to 2000 since we have a nice offset for that year
    leaps=0;
    for(i=2000; i<year; i++)
    {
        if (((i%4)==0) && ( ((i%100)>0) || ((i%400)==0)))
        {
            leaps++;
        }
    }
        
        
    //sprintf(PS,"leaps = %d\r\n", leaps);
    //debug_printf(PS);
    ret += leaps * 86400; // add in all previous leap days
    ret += (year-2000) * 31536000;
    
    return ret + 946684800; // offset for 2000 CE
}

unsigned int ntp_to_1970(unsigned int time)
{
    // if most significant bit is a '1' the NTP time is in a range from
    // 1968-2036, starting at 1/1/1900
    //
    // if the most significant bit is a '0' then the NTP time is in the range
    // from 2036-2104 starting at 6:28:16 on 2/7/2036
    
    if (time & 0x80000000) { return time-2208988800L; }
    else                   { return time+2085978496L; }
}



char *format_date_time(unsigned int timestamp, char *out, unsigned char time_format)
{
	short year;
	char month;
	char day;
	char hour;
	char minute;
	char second;
	char am=1;
	
	format_1970_time(timestamp, &year, &month, &day, &hour, &minute, &second);
	
	if (time_format==0)
	{	
//		sprintf(out, "%d/%d/%d %s%d:%s%d:%s%d", month, day, year,
//			hour<10?"0":"", hour,
//			minute<10?"0":"", minute,
//			second<10?"0":"", second);
    sprintf(out, "%02d/%02d/%04d %02d:%02d:%02d", month, day, year, hour, minute, second);
	}
	else
	{
		if (hour>=12) am=0;
		if (hour>12)  hour-=12;
		if (hour==0) hour=12;

		sprintf(out, "%02d/%02d/%04d %02d:%02d:%02d %s", month, day, year, hour, minute, second, am?"AM":"PM");
	}
			
	return out;
}

void format_1970_time(unsigned int time_stamp, short *year, char *month, char *day,char *hour, char *minute, char *second)
{
    int i; // generic temp values for intermediate calculations
    unsigned short j;
    unsigned short k;
    unsigned short leap;
    
    // time_stamp is seconds since since 1/1/2000
    time_stamp -= 946684800L;
    
    // Seconds
    *second=time_stamp%60;
    time_stamp /=60;

    // Minutes 
    *minute = time_stamp%60;
    time_stamp/=60;

    // Hours 
    *hour=time_stamp%24;
    time_stamp/=24;

    // Since time_stamp is now days since 1/1/2000 
    *year = (time_stamp*100)/36525;
    leap = ( *year >> 2 ); // calc leap years so far (2000 is) 
    
    // time_stamp is now day of current year (leaps included) 
    time_stamp -= *year * 365 + leap; 
    
    *year += 2000; // fix up year, based on earlier subtraction 
    
    j = (short)time_stamp; // j - short version of time_stamp 

    leap = *year % 4;
    
    if( leap == 0 ) // leap year? check century 
    { 
        leap = *year % 100;

        if( leap == 0 ) // maybe not, check 4th century 
        { 
            leap = *year % 400;
                
            if( leap == 0 ) // I guess it is 
            {
                leap++;
            }
        }
    }
    else
    {
        leap = 0;
    }


    if( leap != 0 )
    {
        leap = 1;
    }
    else
    {
        leap = 0;
    }

    // We adjusted for all leap years above, including this one.
    // We need to increment the day, if it's still before Feb 29 
    
    if ( ( leap != 0 ) )//&& ( j <= days_in_month[ 2 ] ) )
    {
        j++;
    }

    *day = j - ( days_in_month[ 11 ] + leap ); // Set to day in December 
    *month = 12; // Set to month of December 
    i = 11;
    
    while( i >= 0 ) // Work backward 
    { 
        if( i > 1 )
        {
            k = days_in_month[ i ] + leap;
        }
        else
        {
            k = days_in_month[ i ];
        }

        if( j > k ) 
        {
            j -= k;
            *day = j;
            *month = i + 1;
            break;
        }
       i--;
    }
}


void set_str(char *to, char *from, unsigned int max_len)
{
	if (from==NULL)
	{
		to[0]='\0';
		return;
	}
	strncpy((char*)to, (char*)from, max_len);
	to[max_len-1]='\0';
}

void set_eid(unsigned char *to, unsigned char *from, unsigned int max)
{
	char *p1;
	int i=0;
	
	for(p1=strtok((char*)from, ":"); p1!=NULL; p1=strtok(NULL, ":"))
	{
		to[i++]=ahtoi(p1);
		
		debug_printf("%s : %02X\r\n", p1, to[i-1]);
		
		if (i>=max) return;
	}
	
	debug_printf("Done with EID: ");
	
	for(i=0; i<max; i++)
	{
		debug_printf("%02X:", to[i]);
	}
	
}

unsigned int set_bit(unsigned int to, unsigned int from, unsigned int bit)
{
	if (from) to|=(bit);
	else      to&=~(bit);
	return to;
}

void set_ip(unsigned char *to, char *from)
{
	char *p1;
	
	p1=strtok(from, ".");
	to[0]=atoi(p1);
	p1=strtok(NULL, ".");
	to[1]=atoi(p1);
	p1=strtok(NULL, ".");
	to[2]=atoi(p1);
	p1=strtok(NULL, "\0");
	to[3]=atoi(p1);
}

unsigned char is_true(char *value)
{
	if ((value[0]=='f')||(value[0]=='F')||(value[0]=='0')) return 0;
	return 1;
}

int extract_float(char *data_in)
{
	char *p1;
	int ret=0;
	int part=0;

	// Check that all characters are '.' or 0-9
	for(p1=data_in; *p1!='\0'; p1++)
	{
		if ( (*p1!='-') && (*p1!='.')&& ( (*p1<'0') || (*p1>'9')))
		{
			return ret;
		}
	}

	// get first part
	if ((p1=strtok(data_in, "."))==NULL)
	{
		return ret;
	}
	ret=(atoi(p1)*100);

	if ((p1=strtok(NULL, "."))==NULL)
	{
		return ret;
	}
	p1[2]='\0';
	part=(atoi(p1));
	if ((strlen(p1)<2)&&(part<10))part*=10;

	if (ret>=0) ret+=part;
	else        ret-=part;

	return ret;
}


/***   End Of File   ***/
