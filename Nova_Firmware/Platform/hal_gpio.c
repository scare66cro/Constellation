/*
 * hal_gpio.c — GPIO HAL implementation for AM2434 Cortex-R5F
 *
 * Provides set_output(), read_input(), configure_output_pin()
 * that Application/ code calls.  Maps to AM243x GPIO registers.
 *
 * AM243x GPIO is different from TM4C:
 *   - Two GPIO modules (GPIO0 @ 0x00600000, GPIO1 @ 0x00601000)
 *   - Each module has up to 92 pins
 *   - Register layout: DIR, OUT_DATA, SET_DATA, CLR_DATA, IN_DATA
 *
 * For QEMU emulation, these write to the constellation-nova machine's
 * unimplemented device regions (which log but don't crash).
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */

#include "hal.h"

/* AM243x GPIO register offsets */
#define GPIO_DIR_OFFSET         0x00
#define GPIO_OUT_DATA_OFFSET    0x04
#define GPIO_SET_DATA_OFFSET    0x08
#define GPIO_CLR_DATA_OFFSET    0x0C
#define GPIO_IN_DATA_OFFSET     0x10

#define REG32(addr)  (*(volatile uint32_t *)(addr))

void set_output(_pin_str pin, unsigned int state)
{
    if (state) {
        REG32(pin.port + GPIO_SET_DATA_OFFSET) = pin.pin;
    } else {
        REG32(pin.port + GPIO_CLR_DATA_OFFSET) = pin.pin;
    }
}

unsigned int read_input(_pin_str pin)
{
    return (REG32(pin.port + GPIO_IN_DATA_OFFSET) & pin.pin) ? 1 : 0;
}

void configure_output_pin(_pin_str pin)
{
    /* Set pin as output (DIR bit = 0 for output on AM243x) */
    uint32_t dir = REG32(pin.port + GPIO_DIR_OFFSET);
    dir &= ~pin.pin;  /* Clear bit = output */
    REG32(pin.port + GPIO_DIR_OFFSET) = dir;

    /* Set initial state */
    set_output(pin, pin.initial_state);
}

void configure_input_pin(_pin_str pin)
{
    /* Set pin as input (DIR bit = 1 for input on AM243x) */
    uint32_t dir = REG32(pin.port + GPIO_DIR_OFFSET);
    dir |= pin.pin;   /* Set bit = input */
    REG32(pin.port + GPIO_DIR_OFFSET) = dir;
}
