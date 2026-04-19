/*
 * debug.h — Debug output for Constellation Nova
 *
 * Routes debug_printf to UART0 (debug console).
 *
 * Copyright (c) 2026 Agristar
 */

#ifndef DEBUG_H
#define DEBUG_H

#include <stdio.h>
#include <stdarg.h>
#include "hal.h"

/*
 * debug_printf is implemented in debug.c — sends formatted
 * output to UART0 (HAL_UART_DEBUG).
 */
void debug_printf(const char *fmt, ...);

#endif
