/*
 * orbit_sensor_rtu.h — Sensor-board Modbus RTU master
 *
 * Polls each configured sensor board (unit IDs 1..ORBIT_SENSOR_BOARDS)
 * once per ORBIT_SENSOR_POLL_MS, reads 7 holding registers, converts
 * raw ADC values via adc_convert.c, and stuffs the resulting int16
 * engineering values into OrbitState.sensor_block[].
 *
 * Wire format (per sensor-board firmware, mirrors sensorBusClient.ts):
 *   HR 0  : board_type<<8 | fw_major
 *   HR 1  : fw_minor<<8   | disabled_flag (bit 0)
 *   HR 2  : packed sensor types — 4 nibbles (high → low = ch0..ch3)
 *   HR 3..6 : raw ADC × 16 per channel
 *
 * On RTU timeout / CRC error, the four channel slots for that board
 * are set to ORBIT_SENSOR_VAL_UNDEF so the controller LP sees a clean
 * "no data" state instead of stale values.
 *
 * Transport binding: hardware-specific UART4 + RS-485 DE/RE driver
 * registers itself via `orbit_sensor_rtu_bind_transport()` BEFORE the
 * scheduler starts. If no transport is bound when the task wakes, it
 * logs a one-shot warning and parks — the framing code is exercised
 * the moment a transport plugs in.
 */
#ifndef ORBIT_SENSOR_RTU_H
#define ORBIT_SENSOR_RTU_H

#include <stdint.h>
#include <stdbool.h>
#include "mb_rtu.h"

#define ORBIT_SENSOR_POLL_MS  1000U

/* Spawn the RTU master task. Spawned by main.c when role != CONTROLLER. */
void orbit_sensor_rtu_task(void *args);

/* Register the UART4/RS-485 transport. Stored as a pointer; the
 * provided struct must outlive the task (typically static const). */
void orbit_sensor_rtu_bind_transport(const MbRtuTransport *t);

/* Test/dev helper: stuff a single sensor slot. Useful while the real
 * transport isn't bound so the storage HR map can be exercised
 * end-to-end from a Modbus TCP client. Holds the OrbitState lock. */
void orbit_sensor_rtu_set_test_value(uint8_t board_idx, uint8_t channel,
                                     int16_t value_x10);

#endif /* ORBIT_SENSOR_RTU_H */
