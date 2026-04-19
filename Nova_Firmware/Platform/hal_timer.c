/*
 * hal_timer.c — Timer HAL for AM2434 Cortex-R5F
 *
 * Provides millisecond tick and delay using the R5F PMU
 * (Performance Monitoring Unit) cycle counter, which runs at
 * the core clock speed (800 MHz).
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */

#include "hal.h"

#ifdef QEMU_BUILD
#include "FreeRTOS.h"
#include "task.h"
extern void vPortYield(void);
#endif

/* R5F PMU cycle counter access */
static inline uint32_t read_pmccntr(void)
{
    uint32_t val;
    __asm volatile("mrc p15, 0, %0, c9, c13, 0" : "=r"(val));
    return val;
}

static inline void enable_pmccntr(void)
{
    uint32_t val;
    /* Enable all counters */
    __asm volatile("mrc p15, 0, %0, c9, c12, 0" : "=r"(val));
    val |= 1;  /* Enable */
    __asm volatile("mcr p15, 0, %0, c9, c12, 0" :: "r"(val));
    /* Enable cycle counter */
    __asm volatile("mcr p15, 0, %0, c9, c12, 1" :: "r"(0x80000000));
}

#define CPU_CLOCK_HZ    800000000UL

#define CYCLES_PER_MS   (CPU_CLOCK_HZ / 1000)   /* real hardware value */
#define CYCLES_PER_MS_EFF  CYCLES_PER_MS
#define CYCLES_PER_US   (CPU_CLOCK_HZ / 1000000)

static volatile uint32_t tick_ms = 0;

void hal_timer_init(void)
{
    enable_pmccntr();
    tick_ms = 0;
}

void hal_timer_delay_ms(uint32_t ms)
{
    uint32_t start = read_pmccntr();
    uint32_t target = ms * CYCLES_PER_MS;
    while ((read_pmccntr() - start) < target) {}
}


uint32_t hal_timer_get_ms(void)
{
    /* tick_ms is advanced solely by the QEMU 1kHz timer IRQ via
     * hal_timer_clear_tick().  With -icount shift=auto,sleep=on the
     * virtual clock matches wall-clock, so no PMU polling needed. */
    return tick_ms;
}

uint32_t hal_timer_get_us(void)
{
    return read_pmccntr() / CYCLES_PER_US;
}

/*
 * Called from FreeRTOS tick hook or timer ISR to
 * increment the millisecond counter.
 */
void hal_timer_tick(void)
{
    tick_ms++;

    /* Update system globals (Application/ code reads these) */
    uptime_ms = tick_ms;
    uptime_sec = tick_ms / 1000;
}

/*
 * Clear the QEMU tick timer interrupt.
 * The QEMU constellation-nova machine maps a minimal tick timer
 * at TIMER0_BASE.  Reading offset 0x00 returns the number of
 * accumulated ticks and clears the IRQ.  We process all of them
 * so that a burst of missed ticks (common when the timer fires
 * faster than the emulated CPU) is caught up in one IRQ call.
 *
 * Called from vPortTickHandler (FreeRTOS ARM_CR5 port).
 */
#define AM243X_TIMER0_BASE_ADDR  0x02400000
void hal_timer_clear_tick(void)
{
    volatile uint32_t *tick_reg = (volatile uint32_t *)AM243X_TIMER0_BASE_ADDR;
    uint32_t pending = *tick_reg;  /* Read clears interrupt + returns count */

    /* Advance our software ms counter by the number of missed ticks */
    for (uint32_t i = 0; i < pending && i < 200; i++) {
        hal_timer_tick();
    }

    /* vPortTickHandler calls xTaskIncrementTick once after us.
     * For accumulated ticks (pending > 1), call it here for the extras.
     * This ensures FreeRTOS xTickCount catches up in one IRQ. */
#ifdef QEMU_BUILD
    if (pending > 1) {
        extern portBASE_TYPE xTaskIncrementTick(void);
        for (uint32_t i = 1; i < pending && i < 200; i++) {
            xTaskIncrementTick();
        }
    }
#endif
}
