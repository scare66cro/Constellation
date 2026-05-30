/*
 * pinout.h — AM2434 Nova pin definitions
 *
 * Replaces Mini_IO/Platform/Drivers/pinout.h.
 * Uses the same _pin_str structure so Application code compiles unchanged.
 *
 * Pin assignments are provisional — will be finalized during PCB layout.
 * GPIO0 base: 0x00600000, GPIO1 base: 0x00601000
 *
 * Copyright (c) 2026 Agristar
 */

#ifndef __DRIVERS_PINOUT_H__
#define __DRIVERS_PINOUT_H__

#include <stdbool.h>
#include <stdint.h>
#include "hal.h"

/* Include TivaWare-compatible shim headers that the original
   Drivers/pinout.h provided transitively. */
#include "driverlib/gpio.h"
#include "driverlib/sysctl.h"
#include "driverlib/timer.h"
#include "inc/hw_memmap.h"
#include "inc/hw_ints.h"

/* AM2434 GPIO module base addresses */
#define AM2434_GPIO0_BASE   0x00600000
#define AM2434_GPIO1_BASE   0x00601000

/*
 * Pin definitions — same _pin_str structure as TM4C.
 * .port    = GPIO module base address
 * .pin     = bit mask (1 << gpio_number_within_module)
 * .periph  = unused on AM2434 (peripherals enabled via SysConfig)
 * .strength = unused
 * .type    = unused
 */

/* ---- UART pins (directly mapped by pin mux, no GPIO config needed) ---- */
/* UART0: debug console — PA0/PA1 equivalent */
/* UART1: RPi5 bridge — 230400 baud */
/* UART2: RS-485 Port A (VFDs) — via SN65HVD78 transceiver */
/* UART3: RS-485 Port B (spare) */
/* No _pin_str needed — UARTs are configured by MCU+ SDK pin mux */

/* ---- RS-485 direction control ---- */
/* RS-485 transceivers have a DE (Driver Enable) pin */
static const _pin_str AUX_DIR = {
    .pin = (1 << 0), .port = AM2434_GPIO0_BASE,
    .periph = 0, .strength = 0, .type = 0, .initial_state = 0
};

static const _pin_str RS485B_DIR = {
    .pin = (1 << 1), .port = AM2434_GPIO0_BASE,
    .periph = 0, .strength = 0, .type = 0, .initial_state = 0
};

/* ---- Status LEDs ---- */
static const _pin_str LED_STATUS = {
    .pin = (1 << 3), .port = AM2434_GPIO0_BASE,
    .periph = 0, .strength = 0, .type = 0, .initial_state = 0
};

static const _pin_str LED_COMMS = {
    .pin = (1 << 4), .port = AM2434_GPIO0_BASE,
    .periph = 0, .strength = 0, .type = 0, .initial_state = 0
};

/* ---- SPI chip selects ---- */
static const _pin_str FLASH_CS = { .pin = (1 << 16), .port = AM2434_GPIO0_BASE, .periph = 0, .strength = 0, .type = 0, .initial_state = 1 };
static const _pin_str SD_CS    = { .pin = (1 << 17), .port = AM2434_GPIO0_BASE, .periph = 0, .strength = 0, .type = 0, .initial_state = 1 };

/* ---- Analog inputs (ADC channels, not GPIO — defined here for completeness) ---- */
static const _pin_str RT_AD = { .pin = 0, .port = 0, .periph = 0, .strength = 0, .type = 0, .initial_state = 0 };
static const _pin_str LT_AD = { .pin = 0, .port = 0, .periph = 0, .strength = 0, .type = 0, .initial_state = 0 };

/* ---- Default button ---- */
static const _pin_str DEFAULT  = { .pin = (1 << 20), .port = AM2434_GPIO0_BASE, .periph = 0, .strength = 0, .type = 0, .initial_state = 0 };

/* ---- TM4C compatibility aliases ---- */
static const _pin_str LTX_TX = { .pin = 0, .port = 0, .periph = 0, .strength = 0, .type = 0, .initial_state = 0 };
static const _pin_str LTX_RX = { .pin = 0, .port = 0, .periph = 0, .strength = 0, .type = 0, .initial_state = 0 };
static const _pin_str RX     = { .pin = 0, .port = 0, .periph = 0, .strength = 0, .type = 0, .initial_state = 0 };
static const _pin_str TX     = { .pin = 0, .port = 0, .periph = 0, .strength = 0, .type = 0, .initial_state = 0 };
static const _pin_str SCL    = { .pin = 0, .port = 0, .periph = 0, .strength = 0, .type = 0, .initial_state = 0 };
static const _pin_str SDA    = { .pin = 0, .port = 0, .periph = 0, .strength = 0, .type = 0, .initial_state = 0 };
static const _pin_str SPICLK = { .pin = 0, .port = 0, .periph = 0, .strength = 0, .type = 0, .initial_state = 0 };
static const _pin_str MOSI   = { .pin = 0, .port = 0, .periph = 0, .strength = 0, .type = 0, .initial_state = 0 };
static const _pin_str MISO   = { .pin = 0, .port = 0, .periph = 0, .strength = 0, .type = 0, .initial_state = 0 };

#define SD_TX   MOSI

/* ---- Net LEDs (directly driven by DP83826 PHY on AM2434) ---- */
static const _pin_str NET_LINK     = { .pin = 0, .port = 0, .periph = 0, .strength = 0, .type = 0, .initial_state = 0 };
static const _pin_str NET_ACTIVITY = { .pin = 0, .port = 0, .periph = 0, .strength = 0, .type = 0, .initial_state = 0 };

/* ---- AES (not used on Nova — legacy) ---- */
static const _pin_str AES_MUX   = { .pin = 0, .port = 0, .periph = 0, .strength = 0, .type = 0, .initial_state = 0 };
static const _pin_str AES_ERROR = { .pin = 0, .port = 0, .periph = 0, .strength = 0, .type = 0, .initial_state = 0 };

/* SPI2 shift register compat (TM4C bit-bang, remapped for AM2434) */
static const _pin_str SDO    = { .pin = 0, .port = 0, .periph = 0, .strength = 0, .type = 0, .initial_state = 0 };
static const _pin_str SDI    = { .pin = 0, .port = 0, .periph = 0, .strength = 0, .type = 0, .initial_state = 0 };
static const _pin_str SCK    = { .pin = 0, .port = 0, .periph = 0, .strength = 0, .type = 0, .initial_state = 0 };
static const _pin_str CS_595 = { .pin = 0, .port = 0, .periph = 0, .strength = 0, .type = 0, .initial_state = 0 };
static const _pin_str CS_597 = { .pin = 0, .port = 0, .periph = 0, .strength = 0, .type = 0, .initial_state = 0 };

/* SD card base (SPI — not used on Nova, QSPI instead) */
#define SD_MAX_SPEED    25000000

/* I2C base (for RTC driver compatibility) */
#define RTC_I2C_BASE    0x20000000  /* AM243x I2C0 */

/* Function declarations */
void PinoutSet(void);
void CreateSPILocks(void);

#endif /* __DRIVERS_PINOUT_H__ */
