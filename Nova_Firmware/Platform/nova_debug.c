/*
 * debug.c — Debug printf for AM2434 Nova
 *
 * Sends formatted output to UART0 (debug console).
 * Uses a critical section for thread safety instead of a stack buffer
 * (FreeRTOS task stacks are too small for 128+ byte local buffers).
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */

#include "debug.h"
#include "hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <stdarg.h>

static char debug_buf[256];

void debug_printf(const char *fmt, ...)
{
    taskENTER_CRITICAL();
    {
        va_list args;
        va_start(args, fmt);
        int len = vsnprintf(debug_buf, sizeof(debug_buf), fmt, args);
        va_end(args);

        if (len > 0) {
            if (len > (int)sizeof(debug_buf) - 1) {
                len = sizeof(debug_buf) - 1;
            }
            hal_uart_send(HAL_UART_DEBUG, (const uint8_t *)debug_buf, (uint32_t)len);
        }
    }
    taskEXIT_CRITICAL();
}
