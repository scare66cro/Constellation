/*
 * nova_watchdog_core.c — R5F-2 Safety Watchdog Firmware
 *
 * This is the complete firmware for R5F-2. It is intentionally tiny
 * (~2 KB compiled) and does exactly one thing: monitor R5F-0's heartbeat
 * counter in shared IPC memory and force safe-state if it stops.
 *
 * Design principles:
 *   - No FreeRTOS, no malloc, no complex data structures
 *   - Bare-metal polling loop: read heartbeat, compare, delay, repeat
 *   - If R5F-0 heartbeat stalls for WDG_TIMEOUT_MS → trip
 *   - After WDG_MAX_STRIKES consecutive trips → hold safe-state
 *   - Safe-state: set IPC flag so R5F-0 (if it recovers) knows
 *   - Future: direct GPIO output to force critical equipment off
 *
 * Memory: runs entirely in its 128 KB partition + IPC shared region.
 * No UART, no interrupts, no peripherals — pure polling.
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include "nova_ipc.h"

/* ─── Version ─────────────────────────────────────────────────────────── */
#define WDG_VERSION "1.0.0-wdg"

/* ─── Simple busy-wait delay ──────────────────────────────────────────── */
/* At ~800 MHz, 800 iterations ≈ 1 µs. This is approximate — good enough
 * for a watchdog that checks every 50ms. */
static void delay_ms(uint32_t ms)
{
    volatile uint32_t count = ms * 800;
    while (count > 0) {
        count--;
        /* Prevent the compiler from optimizing this away */
        __asm volatile("" ::: "memory");
    }
}

/* ─── IPC initialization ─────────────────────────────────────────────── */

static void ipc_watchdog_init(void)
{
    /* If IPC region isn't initialized yet (we might boot before R5F-0),
     * zero our section and wait for R5F-0 to write the magic. */
    IPC->heartbeat[CORE_ID_WATCHDOG].state = CORE_STATE_BOOTING;
    IPC->heartbeat[CORE_ID_WATCHDOG].counter = 0;
    IPC->heartbeat[CORE_ID_WATCHDOG].uptime_ms = 0;
    IPC->heartbeat[CORE_ID_WATCHDOG].error_code = 0;

    /* Copy version string */
    const char *ver = WDG_VERSION;
    for (int i = 0; i < 31 && ver[i]; i++) {
        IPC->heartbeat[CORE_ID_WATCHDOG].version[i] = ver[i];
    }

    /* Initialize watchdog control block */
    IPC->watchdog.safe_state_active = 0;
    IPC->watchdog.trip_count = 0;
    IPC->watchdog.last_trip_time_ms = 0;
    IPC->watchdog.strike_count = 0;
    IPC->watchdog.monitored_cores = (1 << CORE_ID_CONTROL);  /* Monitor R5F-0 */
}

/* ─── Safe-state enforcement ──────────────────────────────────────────── */

static void force_safe_state(void)
{
    IPC->watchdog.safe_state_active = 1;
    __asm volatile("DMB" ::: "memory");

    /*
     * === LAYER 1: Hardware GPIO backstop ===
     * Drive safety GPIO outputs directly via hal_watchdog.
     * The watchdog core has MPU-protected access to specific GPIO
     * pins that can force-off critical equipment:
     *   - Compressor contactor → OFF
     *   - Burner relay → OFF
     *   - Cavity/electric heat → OFF
     *   - Auxiliary power bus → OFF
     *   - Fan keeps running (fail-safe for ventilation)
     *
     * On QEMU, hal_watchdog_force_safe_gpio() is a no-op — we rely
     * on the IPC flag.
     */
    extern void hal_watchdog_force_safe_gpio(void);
    hal_watchdog_force_safe_gpio();

    /*
     * === LAYER 2: IPC trip notification to R5F-0 ===
     * If R5F-0 is partially responsive (e.g. stuck in a slow loop
     * but still processing IPC), it should command all orbits into
     * their role-specific safe states:
     *
     * Storage orbits:
     *   - Humidifier OFF (DO5 head, DO6 pump)
     *   - Climacell OFF (DO4)
     *   - Door outputs OFF (hold current position)
     *   - Fan stays running at recirculation speed
     *   - Yellow light ON (safe-mode indicator)
     *
     * GDC (door) orbits:
     *   - All fresh-air doors close (freshAirDoorPct → 0)
     *   - Door actuators drive to fully closed position
     *
     * === LAYER 3: Orbit-local watchdog ===
     * Each orbit board has its own 5-second communication watchdog.
     * If Nova stops polling (because R5F-0 is dead), the orbit
     * enters safe mode autonomously with the same behaviour above.
     * This is the last line of defence — no IPC or GPIO required.
     */
    IpcMessage trip_msg;
    trip_msg.type = IPC_MSG_WDG_TRIP;
    trip_msg.length = 8;
    /* Payload: uptime (4 bytes) + R5F-0 state (4 bytes) */
    {
        uint32_t up = IPC->heartbeat[CORE_ID_WATCHDOG].uptime_ms;
        uint32_t st = IPC->heartbeat[CORE_ID_CONTROL].state;
        trip_msg.payload[0] = (uint8_t)(up);
        trip_msg.payload[1] = (uint8_t)(up >> 8);
        trip_msg.payload[2] = (uint8_t)(up >> 16);
        trip_msg.payload[3] = (uint8_t)(up >> 24);
        trip_msg.payload[4] = (uint8_t)(st);
        trip_msg.payload[5] = (uint8_t)(st >> 8);
        trip_msg.payload[6] = (uint8_t)(st >> 16);
        trip_msg.payload[7] = (uint8_t)(st >> 24);
    }
    /* Best-effort push — if R5F-0 is completely dead the ring may be full */
    ipc_ring_push(&IPC->ring_wdg_to_ctrl, &trip_msg);
}

static void clear_safe_state(void)
{
    IPC->watchdog.safe_state_active = 0;
    IPC->watchdog.strike_count = 0;
    __asm volatile("DMB" ::: "memory");
}

/* ─── Main watchdog loop ──────────────────────────────────────────────── */

int core_main(void)
{
    ipc_watchdog_init();

    /* Wait for R5F-0 to initialize IPC */
    while (IPC->magic != IPC_MAGIC) {
        delay_ms(10);
    }

    IPC->heartbeat[CORE_ID_WATCHDOG].state = CORE_STATE_RUNNING;

    uint32_t last_r5f0_counter = IPC->heartbeat[CORE_ID_CONTROL].counter;
    uint32_t uptime_ms = 0;
    uint32_t stall_ms = 0;

    /* Polling loop: check every 50 ms */
    const uint32_t poll_interval_ms = 50;

    while (1) {
        delay_ms(poll_interval_ms);
        uptime_ms += poll_interval_ms;
        IPC->heartbeat[CORE_ID_WATCHDOG].uptime_ms = uptime_ms;
        IPC->heartbeat[CORE_ID_WATCHDOG].counter++;

        /* Read R5F-0 heartbeat */
        __asm volatile("DMB" ::: "memory");
        uint32_t r5f0_counter = IPC->heartbeat[CORE_ID_CONTROL].counter;
        uint32_t r5f0_state = IPC->heartbeat[CORE_ID_CONTROL].state;

        /* Is R5F-0 alive? */
        if (r5f0_state == CORE_STATE_RUNNING && r5f0_counter != last_r5f0_counter) {
            /* Alive — reset stall counter */
            last_r5f0_counter = r5f0_counter;
            stall_ms = 0;

            /* If we were in safe-state and R5F-0 recovered, clear it
             * (but only if strikes haven't exceeded max) */
            if (IPC->watchdog.safe_state_active &&
                IPC->watchdog.strike_count < WDG_MAX_STRIKES) {
                clear_safe_state();
            }
        } else if (r5f0_state >= CORE_STATE_RUNNING) {
            /* Not making progress — accumulate stall time */
            stall_ms += poll_interval_ms;

            if (stall_ms >= WDG_TIMEOUT_MS) {
                /* TRIP — R5F-0 is unresponsive */
                IPC->watchdog.trip_count++;
                IPC->watchdog.strike_count++;
                IPC->watchdog.last_trip_time_ms = uptime_ms;
                IPC->heartbeat[CORE_ID_WATCHDOG].error_code = r5f0_state;

                force_safe_state();

                /* Reset stall counter — we'll check again next cycle.
                 * If R5F-0 recovers, strike_count resets.
                 * If it doesn't, strike_count accumulates and after
                 * WDG_MAX_STRIKES we hold safe-state permanently. */
                stall_ms = 0;

                /* Update our counter to show we're still alive */
                last_r5f0_counter = r5f0_counter;
            }
        }
        /* else: R5F-0 hasn't booted yet — don't trip */
    }

    return 0;  /* unreachable */
}
