#ifndef SNTP_H
#define SNTP_H

#include "lwip/ip_addr.h"

#define NTP_SUCCESSFUL	0
#define NTP_FAILED_DNS	1
#define NTP_ERROR		2


typedef void(*ntp_receive_time_fn)(unsigned int status, unsigned int timestamp);

void ntp_start_request(ntp_receive_time_fn cb);

#endif
/***   End Of File   ***/
