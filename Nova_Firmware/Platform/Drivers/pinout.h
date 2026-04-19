/*
 * Drivers/pinout.h — Shim that redirects to Nova Platform headers.
 *
 * Application code includes "Drivers/pinout.h" expecting the TM4C
 * pin definitions.  On AM2434 we redirect to our own headers which
 * provide the same _pin_str structure and LTX_UART / AUX_UART defines
 * but mapped to the HAL port identifiers.
 */
#ifndef DRIVERS_PINOUT_H_SHIM
#define DRIVERS_PINOUT_H_SHIM

#include "hal.h"
#include "pinout.h"

#endif /* DRIVERS_PINOUT_H_SHIM */
