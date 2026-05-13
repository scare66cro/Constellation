/*
 * PORT_byte_swap.c
 *
 *  Created on: Mar 24, 2015
 *      Author: kfarr
 */

#include <stdint.h>
#include "PORT_byte_swap.h"

#define htonl(a)                    \
        ((((a) >> 24) & 0x000000ff) |   \
         (((a) >>  8) & 0x0000ff00) |   \
         (((a) <<  8) & 0x00ff0000) |   \
         (((a) << 24) & 0xff000000))

uint64_t be64toh(uint64_t value)
{
    const uint32_t high_part = htonl((uint32_t)(value >> 32));
    const uint32_t low_part = htonl((uint32_t)(value & 0xFFFFFFFFLL));
    return ((uint64_t)(low_part) << 32) | high_part;
}
