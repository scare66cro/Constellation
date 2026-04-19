/*
 * hal_watchdog.c — Watchdog HAL for AM2434 Cortex-R5F
 *
 * AM243x has RTI/WWDT (Windowed Watchdog Timer) modules.
 * RTI0 is dedicated to R5F-2 watchdog core.
 *
 * On real hardware:
 *   - RTI0 WWDT is configured with a ~600ms timeout window
 *   - The watchdog core must feed within the open window to prevent reset
 *   - Safety GPIO pins can force-off critical equipment independently
 *
 * On QEMU:
 *   - RTI registers are not modelled, so writes are harmless no-ops
 *   - The build uses QEMU_BUILD to guard hardware-specific paths
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */

#include "hal.h"
#include "nova_ipc.h"
#include <stdint.h>

/* ─── AM2434 RTI (Real-Time Interrupt) base addresses ─────────────────── */
/* See TI AM243x TRM §12.7: RTI Module */
#ifndef QEMU_BUILD

#define RTI0_BASE               0x02F7A000U

/* RTI WWDT registers (offsets from RTI0_BASE) */
#define RTI_WWDCTRL             (*(volatile uint32_t *)(RTI0_BASE + 0x90U))
#define RTI_WWDPRLD             (*(volatile uint32_t *)(RTI0_BASE + 0x94U))
#define RTI_WWDSTATUS           (*(volatile uint32_t *)(RTI0_BASE + 0x98U))
#define RTI_WWDKEY              (*(volatile uint32_t *)(RTI0_BASE + 0x9CU))
#define RTI_WWDCNTR             (*(volatile uint32_t *)(RTI0_BASE + 0xA0U))
#define RTI_WWDSIZECTRL         (*(volatile uint32_t *)(RTI0_BASE + 0xA4U))

/* WWDT key sequence: write 0xE51A first, then 0xA35C to feed */
#define RTI_WWDKEY_FIRST        0x0000E51AU
#define RTI_WWDKEY_SECOND       0x0000A35CU

/* ─── Safety GPIO: direct equipment force-off pins ────────────────────── */
/* These GPIO outputs are MPU-protected for R5F-2 only.
 * Active-LOW: writing 0 de-energises the contactor/relay. */
#define GPIO_SAFETY_BASE        0x00600000U   /* GPIO0 base on AM243x */
#define GPIO_OUT_DATA_CLR       (*(volatile uint32_t *)(GPIO_SAFETY_BASE + 0x190U))
#define GPIO_DIR                (*(volatile uint32_t *)(GPIO_SAFETY_BASE + 0x130U))

/* Pin assignments (bit masks) — matched to Nova PCB schematic */
#define SAFETY_PIN_COMPRESSOR   (1U << 16)   /* GPIO0_16: compressor contactor */
#define SAFETY_PIN_BURNER       (1U << 17)   /* GPIO0_17: burner relay */
#define SAFETY_PIN_HEAT         (1U << 18)   /* GPIO0_18: cavity/electric heat */
#define SAFETY_PIN_AUX_POWER    (1U << 19)   /* GPIO0_19: auxiliary power bus */
#define SAFETY_PINS_ALL         (SAFETY_PIN_COMPRESSOR | SAFETY_PIN_BURNER | \
                                 SAFETY_PIN_HEAT | SAFETY_PIN_AUX_POWER)

/* Fan is intentionally NOT in this mask — ventilation stays on for safety */

#endif /* !QEMU_BUILD */

void hal_watchdog_init(void)
{
#ifndef QEMU_BUILD
    /* ── Configure safety GPIO pins as outputs (driven HIGH = equipment enabled) ── */
    GPIO_DIR &= ~SAFETY_PINS_ALL;  /* Clear bits = output direction */

    /* ── Configure RTI0 WWDT ── */
    /* Preload value sets the timeout window.
     * At RTI clock = 200 MHz with /16 prescaler = 12.5 MHz:
     *   Preload 0x0000_1D4C (~7500) → ~600 ms window.
     * The watchdog core polls every 50ms so it has many chances to feed. */
    RTI_WWDPRLD = 0x00001D4CU;
    RTI_WWDSIZECTRL = 0x50U;  /* 50% window (open window is 50-100% of the counter) */
    RTI_WWDCTRL = 0xA98559DAU;  /* Enable WWDT (magic enable key) */
#endif
    /* On QEMU: no-op — RTI is not modelled */
}

void hal_watchdog_feed(void)
{
#ifndef QEMU_BUILD
    /* RTI WWDT feed: two-step key write within the open window */
    RTI_WWDKEY = RTI_WWDKEY_FIRST;
    RTI_WWDKEY = RTI_WWDKEY_SECOND;
#endif
}

void hal_watchdog_force_safe_gpio(void)
{
#ifndef QEMU_BUILD
    /* De-energise all safety-critical relays by clearing GPIO pins.
     * This is the hardware-level backstop: even if R5F-0 is hung,
     * R5F-2 can directly kill contactors via these GPIO lines.
     * Fan stays running (not in SAFETY_PINS_ALL). */
    GPIO_OUT_DATA_CLR = SAFETY_PINS_ALL;
#endif
}
