/*
 * mb_rtu.h — Modbus RTU master core (transport-agnostic)
 *
 * Pure framing + CRC-16/Modbus + FC03 request/response handling.
 * Knows nothing about UART hardware — the caller provides a tiny
 * `MbRtuTransport` vtable wrapping the actual byte-level send/recv.
 *
 * The transport is expected to be HALF-DUPLEX RS-485:
 *   - `tx_set_drive(true)` asserts the DE/!RE line, drives the bus.
 *   - `tx_set_drive(false)` releases (after TX drains) so the slave
 *     can reply.
 *   - `tx_bytes()` is a blocking write that returns once the bytes
 *     have been *queued* (not necessarily flushed). The transport is
 *     responsible for waiting for THR-empty before tx_set_drive(false).
 *   - `rx_byte_blocking(ms)` returns 0=byte, -1=timeout, -2=error.
 *   - `rx_flush()` discards anything in the RX FIFO (called before TX
 *     to drop our own echo on a wired RS-485 loop).
 *
 * Inter-frame timing: the caller is expected to wait the standard
 * 3.5-character idle gap between frames. At ≥ 19200 baud the JS sim's
 * 50-ms post-frame delay is overkill; we use 5 ms between polls which
 * is still ≥ 17 char-times at 9600 baud.
 *
 * Ports the wire format from orbit-simulator/src/modbusRtuClient.ts.
 */
#ifndef MB_RTU_H
#define MB_RTU_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define MB_RTU_OK                 0
#define MB_RTU_ERR_TIMEOUT       -1
#define MB_RTU_ERR_CRC           -2
#define MB_RTU_ERR_UNIT_MISMATCH -3
#define MB_RTU_ERR_FC_MISMATCH   -4
#define MB_RTU_ERR_LENGTH        -5
#define MB_RTU_ERR_EXCEPTION     -6
#define MB_RTU_ERR_TX            -7
#define MB_RTU_ERR_PARAM         -8

typedef struct MbRtuTransport {
    /* Toggle the RS-485 driver-enable line. */
    void (*tx_set_drive)(bool enable);
    /* Blocking-write `len` bytes; transport must wait for THR-empty
     * before returning if needed for half-duplex turnaround. Returns
     * 0 on success, negative on hardware error. */
    int  (*tx_bytes)(const uint8_t *buf, size_t len);
    /* Discard anything currently in the RX FIFO/ring. */
    void (*rx_flush)(void);
    /* Read a single byte, waiting up to `timeout_ms`. Returns
     * MB_RTU_OK with byte in *out on success, MB_RTU_ERR_TIMEOUT on
     * silence, MB_RTU_ERR_TX on hardware error. */
    int  (*rx_byte_blocking)(uint8_t *out, uint32_t timeout_ms);
} MbRtuTransport;

/* Standard CRC-16/Modbus (poly 0xA001, init 0xFFFF, no reflect-out). */
uint16_t mb_rtu_crc16(const uint8_t *buf, size_t len);

/* FC03 — Read Holding Registers.
 *   unit_id, start_addr, qty   request parameters
 *   regs_out[]                 caller buffer, size >= qty
 *   timeout_ms                 per-byte read timeout
 * Returns MB_RTU_OK or one of MB_RTU_ERR_*. On MB_RTU_ERR_EXCEPTION
 * the slave's exception code is written to *exc_out (if non-NULL).
 *
 * Caller MUST hold any lock guarding the transport — this function
 * is not reentrant. */
int mb_rtu_read_holding(const MbRtuTransport *t,
                        uint8_t unit_id, uint16_t start_addr, uint16_t qty,
                        uint16_t *regs_out, uint32_t timeout_ms,
                        uint8_t *exc_out);

#endif /* MB_RTU_H */
