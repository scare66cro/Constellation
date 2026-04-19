/*
 * hal_adc.c — ADC HAL stub for AM2434 Cortex-R5F
 *
 * Nova does not use local ADC channels — all analog sensor data comes
 * from Orbit boards' RS-485 sensor passthrough registers via Modbus TCP.
 *
 * This stub provides link-time resolution for any Application layer code
 * that references hal_adc_init/hal_adc_read (e.g., Analog_Input.c legacy
 * paths that read local ADC on the TM4C).  Returns 0 (no signal) so the
 * firmware treats all local ADC channels as "not present."
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */

#include "hal.h"

void hal_adc_init(void)
{
    /* No local ADC on Nova — all analog via Orbit sensor passthrough */
}

uint16_t hal_adc_read(uint8_t channel)
{
    (void)channel;
    return 0;
}
