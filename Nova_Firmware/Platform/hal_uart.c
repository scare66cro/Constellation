/*
 * hal_uart.c — UART HAL implementation for AM2434 Cortex-R5F
 *
 * Implements the UART portion of hal.h using memory-mapped 16550
 * registers.  Works with both real AM2434 hardware and QEMU's
 * serial_mm (16550) model in the constellation-nova machine.
 *
 * AM243x UARTs are 16550-compatible at the register level, so we
 * can drive them directly without the full MCU+ SDK UART driver.
 * This keeps the firmware simple and QEMU-friendly.
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */

#include "hal.h"
#include <stddef.h>

/* ---- 16550 Register offsets (byte-addressed, shift = 2 for 32-bit access) ---- */
#define UART_REG_SHIFT  2   /* registers are 4 bytes apart on AM243x */

#define UART_RBR   (0x00 << UART_REG_SHIFT)  /* Receive Buffer (read) */
#define UART_THR   (0x00 << UART_REG_SHIFT)  /* Transmit Holding (write) */
#define UART_IER   (0x01 << UART_REG_SHIFT)  /* Interrupt Enable */
#define UART_IIR   (0x02 << UART_REG_SHIFT)  /* Interrupt Ident (read) */
#define UART_FCR   (0x02 << UART_REG_SHIFT)  /* FIFO Control (write) */
#define UART_LCR   (0x03 << UART_REG_SHIFT)  /* Line Control */
#define UART_MCR   (0x04 << UART_REG_SHIFT)  /* Modem Control */
#define UART_LSR   (0x05 << UART_REG_SHIFT)  /* Line Status */
#define UART_DLL   (0x00 << UART_REG_SHIFT)  /* Divisor Latch Low (DLAB=1) */
#define UART_DLH   (0x01 << UART_REG_SHIFT)  /* Divisor Latch High (DLAB=1) */

/* LSR bits */
#define LSR_DR     (1 << 0)   /* Data Ready */
#define LSR_THRE   (1 << 5)   /* Transmit Holding Register Empty */

/* IER bits */
#define IER_ERBFI  (1 << 0)   /* Enable Received Data Available Interrupt */

/* AM2434 UART base addresses */
static const uint32_t uart_base[HAL_UART_COUNT] = {
    0x02800000,  /* UART0 — debug */
    0x02810000,  /* UART1 — RPi5 bridge */
    0x02820000,  /* UART2 — RS-485 VFD bus */
    0x02830000,  /* UART3 — RS-485 spare */
};

/* AM2434 input clock to UARTs: 48 MHz (from HFOSC / PLL) */
#define UART_INPUT_CLK  48000000UL

/* ---- Register access macros ---- */
#define REG32(base, off)  (*(volatile uint32_t *)((base) + (off)))

/* ======================== Implementation ======================== */

void hal_uart_init(hal_uart_port_t port, uint32_t baud)
{
    if (port >= HAL_UART_COUNT) return;

    uint32_t base = uart_base[port];
    uint16_t divisor = (uint16_t)(UART_INPUT_CLK / (16 * baud));

    /* Enable FIFO, reset TX/RX FIFOs */
    REG32(base, UART_FCR) = 0x07;

    /* Set DLAB to access divisor latches */
    REG32(base, UART_LCR) = 0x83;   /* 8N1 + DLAB */
    REG32(base, UART_DLL) = divisor & 0xFF;
    REG32(base, UART_DLH) = (divisor >> 8) & 0xFF;

    /* Clear DLAB, set 8N1 */
    REG32(base, UART_LCR) = 0x03;   /* 8 data, no parity, 1 stop */

    /* Enable RX interrupt */
    REG32(base, UART_IER) = IER_ERBFI;
}

void hal_uart_send_char(hal_uart_port_t port, uint8_t ch)
{
    if (port >= HAL_UART_COUNT) return;

    uint32_t base = uart_base[port];

    /* Wait for THR empty with timeout to prevent stalling if
     * the receiver is not connected (QEMU tcp chardev with no client,
     * or real hardware with bridge offline).
     *
     * In QEMU, the 16550 FIFO transmit completion timer uses
     * QEMU_CLOCK_VIRTUAL_RT (patched from CLOCK_VIRTUAL in serial.c).
     * WFI yields the CPU; the next tick wakes it, and by then the
     * serial timer has advanced the FIFO.
     *
     * Keep retry count low (10): if the TCP chardev has no client,
     * serial_xmit retries burn CPU. Low count ensures the firmware
     * drops bytes quickly and stays responsive. When a client is
     * connected, 10 retries (~10ms) is plenty for 230400 baud. */
#ifdef QEMU_BUILD
    /* In QEMU the chardev may have no TCP client yet, so on the *debug*
     * UART we drop bytes after a single THRE check (debug output is
     * non-essential and we'd rather stay responsive than spin forever).
     *
     * On the protocol UARTs (HAL_UART_UI bridge, HAL_UART_RS485A/B Modbus)
     * every byte matters — dropping a single byte mid-frame corrupts the
     * COBS+CRC framing on the bridge side, causing the bridge to discard
     * the entire frame as a CRC error.  This was the root cause of the
     * settings-update timeout: a refrigeration ACK followed by the
     * "all settings" burst sent enough back-to-back bytes that QEMU's
     * 16550 model temporarily failed THRE, the byte was dropped, and
     * the bridge's framer never saw a valid ACK.
     *
     * Spin-wait with a generous timeout on protocol UARTs; the patched
     * QEMU 16550 clears THRE in tens of microseconds, so even thousands
     * of retries is a sub-millisecond pause in the worst case. */
    if (port == HAL_UART_DEBUG) {
        if (!(REG32(base, UART_LSR) & LSR_THRE)) {
            return;  /* drop debug character — chardev busy or no client */
        }
    } else {
        int qtries = 10000;
        while (!(REG32(base, UART_LSR) & LSR_THRE)) {
            if (--qtries <= 0) return;  /* give up after ~10ms equivalent */
        }
    }
#else
    int tries = 500;
    while (!(REG32(base, UART_LSR) & LSR_THRE)) {
        if (--tries <= 0) return;  /* drop character */
    }
#endif

    REG32(base, UART_THR) = ch;

#ifdef QEMU_BUILD
    /* Pump the cooperative tick after each character so that vTaskDelay
     * timeouts expire even during long UART bursts (40+ queued messages
     * × 230 bytes each = thousands of send_char calls). */
    hal_timer_get_ms();
#endif
}

void hal_uart_send(hal_uart_port_t port, const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        hal_uart_send_char(port, data[i]);
    }
}

int hal_uart_rx_available(hal_uart_port_t port)
{
    if (port >= HAL_UART_COUNT) return 0;
    return (REG32(uart_base[port], UART_LSR) & LSR_DR) ? 1 : 0;
}

uint8_t hal_uart_get_char(hal_uart_port_t port)
{
    if (port >= HAL_UART_COUNT) return 0;
    return (uint8_t)(REG32(uart_base[port], UART_RBR) & 0xFF);
}

void hal_uart_int_enable(hal_uart_port_t port)
{
    if (port >= HAL_UART_COUNT) return;
    REG32(uart_base[port], UART_IER) |= IER_ERBFI;
}

void hal_uart_int_disable(hal_uart_port_t port)
{
    if (port >= HAL_UART_COUNT) return;
    REG32(uart_base[port], UART_IER) &= ~IER_ERBFI;
}

void hal_uart_flush_rx(hal_uart_port_t port)
{
    if (port >= HAL_UART_COUNT) return;
    uint32_t base = uart_base[port];
    /* Drain FIFO */
    while (REG32(base, UART_LSR) & LSR_DR) {
        (void)REG32(base, UART_RBR);
    }
}
