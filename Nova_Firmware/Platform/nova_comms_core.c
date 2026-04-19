/*
 * nova_comms_core.c — R5F-1 Communications Bridge Firmware
 *
 * Handles all RPi5 UART communication, isolating the control engine
 * (R5F-0) from any stalls or faults on the bridge link.
 *
 * Responsibilities:
 *   - UART1 RX/TX (RPi5 serial bridge)
 *   - COBS + CRC-16 framing (nova_protocol)
 *   - Protobuf encoding/decoding
 *   - OTA firmware update reception (write to OSPI flash)
 *   - Forward commands to R5F-0 via IPC ring
 *   - Read shared data snapshot from R5F-0 for status responses
 *
 * Current state: SKELETON — initializes IPC, updates heartbeat,
 * polls for messages from R5F-0. Actual UART handling will be
 * migrated from R5F-0's nova_protocol.c + nova_messages.c + nova_dataexc.c
 * in a later phase.
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */

#include <stdint.h>
#include "nova_ipc.h"

/* ─── Version ─────────────────────────────────────────────────────────── */
#define COMMS_VERSION "1.0.0-comms"

/* ─── Simple busy-wait delay ──────────────────────────────────────────── */
static void delay_ms(uint32_t ms)
{
    volatile uint32_t count = ms * 800;
    while (count > 0) {
        count--;
        __asm volatile("" ::: "memory");
    }
}

/* ─── UART1 stubs (will be replaced with real HAL calls) ──────────────── */

/* On real hardware, UART1 at 0x02810000 is the RPi5 bridge port.
 * In the multi-core architecture, R5F-1 owns UART1 exclusively.
 * R5F-0 no longer touches UART1 — all RPi5 traffic goes through
 * R5F-1 via the IPC shared memory.
 *
 * For now, these are stubs. The actual UART driver will be ported
 * from hal_uart.c with the UART1 base address hardcoded here. */

#define UART1_BASE  0x02810000

static void uart1_init(void)
{
    /* Stub — UART1 initialization will go here */
    (void)UART1_BASE;
}

static int uart1_rx_available(void)
{
    /* Stub — check UART1 LSR for data ready */
    return 0;
}

/* ─── IPC initialization ─────────────────────────────────────────────── */

static void ipc_comms_init(void)
{
    IPC->heartbeat[CORE_ID_COMMS].state = CORE_STATE_BOOTING;
    IPC->heartbeat[CORE_ID_COMMS].counter = 0;
    IPC->heartbeat[CORE_ID_COMMS].uptime_ms = 0;
    IPC->heartbeat[CORE_ID_COMMS].error_code = 0;

    const char *ver = COMMS_VERSION;
    for (int i = 0; i < 31 && ver[i]; i++) {
        IPC->heartbeat[CORE_ID_COMMS].version[i] = ver[i];
    }
}

/* ─── Process messages from R5F-0 ─────────────────────────────────────── */

static void process_ctrl_messages(void)
{
    IpcMessage msg;
    while (ipc_ring_pop(&IPC->ring_ctrl_to_comms, &msg) == 0) {
        switch (msg.type) {
        case IPC_MSG_UART_TX:
            /* R5F-0 wants us to send a packet to RPi5 via UART1.
             * msg.payload contains the COBS-framed packet.
             * TODO: write to UART1 TX FIFO */
            break;

        case IPC_MSG_STATUS_UPDATE:
            /* R5F-0 updated the shared data snapshot.
             * We can now read IPC->shared_data for the latest
             * equipment status, sensor values, etc. */
            break;

        default:
            break;
        }
    }
}

/* ─── Process UART1 RX (from RPi5) ───────────────────────────────────── */

static void process_uart_rx(void)
{
    /* TODO: Read UART1 RX FIFO, run through COBS decoder,
     * extract protobuf command, forward to R5F-0 via IPC ring:
     *
     *   IpcMessage msg;
     *   msg.type = IPC_MSG_UART_RX;
     *   msg.length = packet_len;
     *   memcpy(msg.payload, packet, packet_len);
     *   ipc_ring_push(&IPC->ring_comms_to_ctrl, &msg);
     */
    (void)uart1_rx_available;
}

/* ─── Main comms loop ─────────────────────────────────────────────────── */

int core_main(void)
{
    ipc_comms_init();
    uart1_init();

    /* Wait for R5F-0 to initialize IPC */
    while (IPC->magic != IPC_MAGIC) {
        delay_ms(10);
    }

    IPC->heartbeat[CORE_ID_COMMS].state = CORE_STATE_RUNNING;

    uint32_t uptime_ms = 0;
    const uint32_t poll_interval_ms = 1;  /* 1 kHz polling for UART responsiveness */

    while (1) {
        delay_ms(poll_interval_ms);
        uptime_ms += poll_interval_ms;

        /* Update heartbeat so watchdog knows we're alive */
        IPC->heartbeat[CORE_ID_COMMS].uptime_ms = uptime_ms;
        if ((uptime_ms % 100) == 0) {
            IPC->heartbeat[CORE_ID_COMMS].counter++;
        }

        /* Check for messages from R5F-0 */
        process_ctrl_messages();

        /* Check for data from RPi5 */
        process_uart_rx();
    }

    return 0;  /* unreachable */
}
