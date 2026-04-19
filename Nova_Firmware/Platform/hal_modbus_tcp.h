/*
 * hal_modbus_tcp.h — Lightweight Modbus TCP client for AM2434 Cortex-R5F
 *
 * Provides blocking Modbus TCP master operations over lwIP raw sockets.
 * Used by the Nova firmware to communicate with Orbit I/O boards which
 * act as Modbus TCP servers on the local 10.47.27.x network.
 *
 * Orbit boards convert Modbus TCP to Modbus RTU on their RS-485 ports
 * for downstream VFDs and sensors.
 *
 * Supported function codes:
 *   FC01 — Read Coils (Digital Outputs readback)
 *   FC02 — Read Discrete Inputs (Digital Inputs + E-Stop)
 *   FC03 — Read Holding Registers (Analog Outputs, VFD/Sensor passthrough)
 *   FC04 — Read Input Registers (status)
 *   FC05 — Write Single Coil (set one DO)
 *   FC06 — Write Single Register (set one AO)
 *   FC15 — Write Multiple Coils (set multiple DOs)
 *   FC16 — Write Multiple Registers (set multiple AOs)
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */

#ifndef HAL_MODBUS_TCP_H
#define HAL_MODBUS_TCP_H

#include <stdint.h>

/* ─── Error codes ─────────────────────────────────────────────────── */

#define MBTCP_OK                   0
#define MBTCP_ERR_CONNECT         -1
#define MBTCP_ERR_TIMEOUT         -2
#define MBTCP_ERR_EXCEPTION       -3
#define MBTCP_ERR_SHORT_RESP      -4
#define MBTCP_ERR_BAD_FC          -5
#define MBTCP_ERR_SOCKET          -6

/* ─── Modbus TCP MBAP header ──────────────────────────────────────── */

#define MBTCP_PORT                502
#define MBTCP_MBAP_LEN            7     /* TxnID(2) + Proto(2) + Len(2) + Unit(1) */
#define MBTCP_MAX_PDU             253
#define MBTCP_MAX_FRAME           (MBTCP_MBAP_LEN + 1 + MBTCP_MAX_PDU)
#define MBTCP_TIMEOUT_MS          8000

/* ─── Connection handle ───────────────────────────────────────────── */

typedef struct {
    int     socket_fd;           /* -1 = not connected */
    uint8_t unit_id;             /* Modbus unit ID (default 1) */
    uint32_t ip_addr;            /* Target IP in network byte order */
    uint16_t port;               /* TCP port (default 502 or 5502 for sim) */
    uint16_t transaction_id;     /* Auto-incrementing per request */
    uint32_t timeout_ms;         /* Response timeout */
    uint32_t last_error;         /* Last error code for diagnostics */
    uint32_t tx_count;           /* Total requests sent */
    uint32_t rx_count;           /* Total valid responses */
    uint32_t err_count;          /* Total errors */
} mbtcp_conn_t;

/* ─── API ─────────────────────────────────────────────────────────── */

/**
 * Initialize a connection handle (does not connect yet).
 * ip_a.ip_b.ip_c.ip_d — target Orbit board IP address
 * port — TCP port (502 for real hardware, 5502 for simulator)
 */
void mbtcp_init(mbtcp_conn_t *conn,
                uint8_t ip_a, uint8_t ip_b, uint8_t ip_c, uint8_t ip_d,
                uint16_t port, uint8_t unit_id);

/**
 * Open TCP connection. Returns MBTCP_OK or error code.
 * Call from a FreeRTOS task context (blocks up to timeout_ms).
 */
int mbtcp_connect(mbtcp_conn_t *conn);

/**
 * Close connection and reset socket.
 */
void mbtcp_disconnect(mbtcp_conn_t *conn);

/**
 * Check if connection is established.
 */
int mbtcp_is_connected(mbtcp_conn_t *conn);

/**
 * FC01: Read Coils (digital output readback from Orbit)
 * @param start_addr  Starting coil address (0-based)
 * @param quantity    Number of coils to read (1-2000)
 * @param coils_out   Buffer for coil states (1 bit per coil, LSB first)
 * @return MBTCP_OK or error code
 */
int mbtcp_read_coils(mbtcp_conn_t *conn,
                     uint16_t start_addr, uint16_t quantity,
                     uint8_t *coils_out);

/**
 * FC02: Read Discrete Inputs (digital inputs + E-Stop from Orbit)
 */
int mbtcp_read_discrete_inputs(mbtcp_conn_t *conn,
                               uint16_t start_addr, uint16_t quantity,
                               uint8_t *inputs_out);

/**
 * FC03: Read Holding Registers (AO values, VFD/sensor passthrough, status)
 */
int mbtcp_read_holding_regs(mbtcp_conn_t *conn,
                            uint16_t start_addr, uint16_t quantity,
                            uint16_t *regs_out);

/**
 * FC04: Read Input Registers
 */
int mbtcp_read_input_regs(mbtcp_conn_t *conn,
                          uint16_t start_addr, uint16_t quantity,
                          uint16_t *regs_out);

/**
 * FC05: Write Single Coil (turn one digital output on/off)
 * @param value  Non-zero = ON (0xFF00), zero = OFF (0x0000)
 */
int mbtcp_write_single_coil(mbtcp_conn_t *conn,
                            uint16_t addr, int value);

/**
 * FC06: Write Single Register (set one analog output)
 */
int mbtcp_write_single_reg(mbtcp_conn_t *conn,
                           uint16_t addr, uint16_t value);

/**
 * FC15: Write Multiple Coils
 * @param coils_in  Bit-packed coil values (LSB first)
 */
int mbtcp_write_multiple_coils(mbtcp_conn_t *conn,
                               uint16_t start_addr, uint16_t quantity,
                               const uint8_t *coils_in);

/**
 * FC16: Write Multiple Registers
 */
int mbtcp_write_multiple_regs(mbtcp_conn_t *conn,
                              uint16_t start_addr, uint16_t quantity,
                              const uint16_t *regs_in);

#endif /* HAL_MODBUS_TCP_H */
