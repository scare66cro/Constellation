

#ifndef SYSTEM_H
#define SYSTEM_H

#include "debug.h"

#include <stdint.h>



extern unsigned int system_clock_speed;
extern unsigned char system_mac[6];

extern unsigned int uptime_sec;
extern unsigned int uptime_ms;
extern unsigned int reset_cause;
extern unsigned int net_up;

#endif
