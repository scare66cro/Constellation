/*
 * orbit_storage.h — STORAGE-role Modbus register map
 *
 * Mirrors orbit-simulator/src/orbitSimulator.ts ~lines 2658-2690.
 *
 * Register layout (all big-endian on the wire, FC03/06/16):
 *   HR    0..  1   AO percent (0..100)              R/W
 *   HR    2..  3   AO mode (0=voltage, 1=current)   R/W
 *   HR  100..147   VFD pass-through (3 drives × 16) R/W (writes Phase 2)
 *   HR  200..263   Sensor block (16 boards × 4)     R/W *
 *   HR 40000        Board ID                        R only
 *   HR 40001        E-stop (0/1)                    R only
 *   HR 40002        Comm-loss (0/1)                 R only
 *   HR 40003        Safe-mode (0/1)                 R only
 *   HR 40004        CPU temperature × 10 °C         R/W (test injection)
 *   HR 40005        Uptime low 16 bits              R only
 *   HR 40006        Uptime high 16 bits             R only
 *   HR 41000..41014 Bench DI inject (DI/E-stop/DC24V) R/W
 *                                                  (live HW sweep wins)
 *
 * * Sensor block writability — until real RTU sensor boards arrive,
 *   a bench-side TCP client (the trimmed orbit-simulator's web panel,
 *   or any Modbus master) injects engineering-format int16 values
 *   into HR 200..263 via FC06/FC16. Once `orbit_sensor_rtu` binds a
 *   real UART transport, its 1 Hz sweep overwrites the injected
 *   values with live RTU data — i.e. live wins, no special mode
 *   switch needed.
 *
 * Coil layout (FC01/05/15):
 *   Coil 0..9      Digital outputs (10 channels)    R/W
 *
 * Discrete-input layout (FC02):
 *   DI 0..9        Digital inputs (10 channels)     R only
 *   DI 10          E-stop                           R only
 *   DI 11..14      DC24V monitor channels           R only
 *
 * Out-of-range addresses MUST return MB_EX_ILLEGAL_DATA_ADDRESS so
 * upstream clients (orbit-bus poller) can probe ranges safely.
 *
 * This module never sleeps and never holds the OrbitState lock across
 * a yield; each helper takes the lock only for the duration of the
 * read/write. The TCP server is responsible for batching multi-register
 * accesses inside a single lock by calling the *_block helpers.
 */
#ifndef ORBIT_STORAGE_H
#define ORBIT_STORAGE_H

#include <stdbool.h>
#include <stdint.h>

/* Modbus exception codes (subset). */
#define MB_EX_NONE                  0x00
#define MB_EX_ILLEGAL_FUNCTION      0x01
#define MB_EX_ILLEGAL_DATA_ADDRESS  0x02
#define MB_EX_ILLEGAL_DATA_VALUE    0x03
#define MB_EX_SLAVE_DEVICE_FAILURE  0x04

/* Read N holding registers starting at `start` into `out`. Returns 0
 * on success or a Modbus exception code on error. The handler takes
 * the OrbitState lock once for the whole block. */
uint8_t storage_read_hr_block(uint16_t start, uint16_t count, uint16_t *out);

/* Write a single holding register. */
uint8_t storage_write_hr_single(uint16_t addr, uint16_t value);

/* Write N holding registers (FC16). */
uint8_t storage_write_hr_block(uint16_t start, uint16_t count, const uint16_t *vals);

/* Read N coils, packed LSB-first into `out_bytes`. */
uint8_t storage_read_coil_block(uint16_t start, uint16_t count, uint8_t *out_bytes);

/* Write a single coil (FC05). value: 0xFF00 = ON, 0x0000 = OFF. */
uint8_t storage_write_coil_single(uint16_t addr, uint16_t value);

/* Write multiple coils (FC15). */
uint8_t storage_write_coil_block(uint16_t start, uint16_t count, const uint8_t *bits);

/* Read N discrete inputs (FC02), packed LSB-first into `out_bytes`. */
uint8_t storage_read_discrete_block(uint16_t start, uint16_t count, uint8_t *out_bytes);

#endif /* ORBIT_STORAGE_H */
