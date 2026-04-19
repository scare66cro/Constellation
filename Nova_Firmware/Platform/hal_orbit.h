/*
 * hal_orbit.h — Orbit I/O board abstraction for Constellation Nova
 *
 * Maps between the legacy BOARD_INFO/IoBoard[] data structures (used by
 * Controls.c, SerialShift.c, and the rest of Application/) and the
 * Modbus TCP register/coil layout of the Orbit I/O boards.
 *
 * The Application layer calls CheckInputs(), OutputOn(), OutputOff(),
 * and ReadAnalogBoards() — those functions read/write IoBoard[].InputState
 * and IoBoard[].OutputState. This module keeps those bitfields synced
 * with the Orbit boards via Modbus TCP.
 *
 * Orbit Modbus TCP register map (see orbit-simulator/src/orbitSimulator.ts):
 *   Coils      0-9  : DO1-DO10
 *                      GDC: paired per actuator (DO1/2=Act1, DO3/4=Act2, ...)
 *                      DO_odd=Open, DO_even=Close
 *   Coils     10-13 : 24V DC outputs 5-8
 *   Disc Inp   0-9  : DI1-DI10
 *                      GDC: paired per actuator (DI1/2=Act1, DI3/4=Act2, ...)
 *                      DI_odd=Open Limit, DI_even=Close Limit
 *   Disc Inp  10    : E-Stop
 *   Hold Regs  0-1  : AO1-AO2 (0-10V: 0-10000 mV)
 *   Hold Regs  2-3  : AO mode (0=voltage, 1=current)
 *   Hold Regs 100+  : VFD RS485 passthrough
 *   Hold Regs 200+  : Sensor RS485 passthrough
 *   Hold Reg  40000 : Board ID
 *   Hold Reg  40001 : E-Stop status
 *   Hold Reg  40002 : Comm lost
 *   Hold Reg  40003 : Safe mode
 *   Hold Reg  40004 : CPU temp × 10
 *   Hold Reg  40005 : Uptime low word
 *   Hold Reg  40006 : Uptime high word
 *
 *   ── GDC (Door Controller) Registers ──
 *   Hold Reg  300   : Door command (freshAirDoorPct × 10, 0-1000)
 *   Hold Reg  301-305: Actuator 1-5 position × 10 (0-1000, read-only)
 *   Hold Reg  306   : Active stage bitfield (read-only)
 *   Hold Reg  307   : Default travel time (seconds, R/W)
 *   Hold Reg  310-314: Stage 1-5 config: door bitfield (bit 0=door 1, etc.)
 *   Hold Reg  315   : Number of active stages (1-5)
 *   Hold Reg  316   : Total system capacity in seconds (read-only)
 *   Hold Reg  318   : Calibrate command (write 1 to start)
 *   Hold Reg  319   : Calibration status (0=idle, 1=opening, 2=closing)
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */

#ifndef HAL_ORBIT_H
#define HAL_ORBIT_H

#include <stdint.h>
#include "hal_modbus_tcp.h"
#include "SerialShift.h"         /* BOARD_INFO, IoBoard[], SYSTEM_BOARDS */
#include "Analog_Input.h"        /* SENSOR_ARRAY, ANALOG_BOARDS_PER_SYSTEM */

/* ─── Configuration ───────────────────────────────────────────────── */

/*
 * NOVA_MAX_ORBITS — maximum number of Orbit boards on the Modbus TCP
 * network.  This is intentionally decoupled from SerialShift.h's
 * BOARD_COUNT (which stays at 3 for legacy Application/ compatibility).
 *
 * Design doc ceiling: 63 (6-bit DIP switch).  Practical limit for a
 * single Nova controller: ~16 boards (typical large site: 2 rooms ×
 * 1 storage + 1 door + 4 refrigeration = 12 Orbits).
 */
#define NOVA_MAX_ORBITS           16

/* Legacy alias — existing code still references ORBIT_MAX_BOARDS.
 * Clamp to BOARD_COUNT when used as an IoBoard[] index guard. */
#define ORBIT_MAX_BOARDS          BOARD_COUNT   /* 3 — for IoBoard[] bounds */

/* Default Orbit base IP and port.
 * Production: 192.168.0.{DIP+1} per design doc.
 * QEMU dev:   10.47.27.{DIP} (matches orbit-simulator defaults).
 */
#ifdef QEMU_BUILD
#define ORBIT_IP_A                10
#define ORBIT_IP_B                47
#define ORBIT_IP_C                27
#define ORBIT_MBTCP_PORT          5502
#else
#define ORBIT_IP_A                192
#define ORBIT_IP_B                168
#define ORBIT_IP_C                0
#define ORBIT_MBTCP_PORT          502
#endif
/* IP_D = dipswitch ID (1-63 design doc, 2-17 typical) */

/* Orbit I/O counts (fixed per Orbit hardware) */
#define ORBIT_NUM_DI              10
#define ORBIT_NUM_DO              10
#define ORBIT_NUM_DC24V           4
#define ORBIT_NUM_AO              2
#define ORBIT_NUM_SENSOR_REGS     64

/* ─── Zone Roles ──────────────────────────────────────────────────── */
/*
 * Each Orbit board is assigned a zone role at configuration time.
 * This determines how Nova uses the board's I/O:
 *   STORAGE  — fans, heaters, humidifiers, dampers, analog boards
 *   DOOR     — door actuators, position feedback, interlocks
 *   REFRIG   — compressor DO + VFD/EEV on RS485 Port B
 *   UNASSIGNED — discovered but not yet configured
 */
typedef enum {
    ORBIT_ROLE_UNASSIGNED = 0,
    ORBIT_ROLE_STORAGE    = 1,
    ORBIT_ROLE_DOOR       = 2,
    ORBIT_ROLE_REFRIG     = 3,
    ORBIT_ROLE_COUNT
} orbit_role_t;

/* ─── Per-Orbit Runtime State ─────────────────────────────────────── */

typedef struct {
    mbtcp_conn_t    conn;
    uint8_t         dipswitch_id;     /* 1-63 (DIP switch value) */
    int             connected;        /* 1 = last poll succeeded */
    int             comm_errors;      /* consecutive error count */
    int             estop_active;     /* Orbit reports E-Stop */
    int             safe_mode;        /* Orbit in safe mode */
    uint16_t        cpu_temp_x10;     /* Orbit CPU temp × 10 */
    uint32_t        orbit_uptime;     /* Orbit uptime in seconds */

    /* Sensor passthrough data (populated from holding regs 200+) */
    uint16_t        sensor_regs[ORBIT_NUM_SENSOR_REGS];
    int             sensor_regs_valid;

    /* ── Multi-orbit extensions ── */
    orbit_role_t    role;             /* Zone role assignment */
    uint8_t         zone_id;          /* Zone index (0=Room A, 1=Room B, ...) */
    int8_t          legacy_slot;      /* IoBoard[] index (0-2), or -1 if not mapped */
    uint8_t         refrig_stage;     /* For REFRIG role: compressor stage 1-8 */
    uint16_t        firmware_ver;     /* Orbit firmware version from status regs */
} orbit_board_t;

/* Master orbit array — indexed by discovery slot 0..NOVA_MAX_ORBITS-1.
 * Slots 0..2 also map to IoBoard[0..2] for legacy Controls.c compat. */
extern orbit_board_t orbit_boards[NOVA_MAX_ORBITS];
extern int           orbit_board_count;   /* Number of discovered boards */

/* ─── API ─────────────────────────────────────────────────────────── */

/**
 * Initialize the Orbit subsystem.
 * Sets up Modbus TCP connections for each configured board slot.
 * Board IDs come from the dipswitch settings stored in Settings.
 */
void orbit_init(void);

/**
 * Poll one Orbit board: read DI into IoBoard[board].InputState,
 * write IoBoard[board].OutputState to DO coils.
 * Called from the polling thread (replaces ThreadSerialShift's bit-bang loop).
 * Returns 0 on success, <0 on error.
 * NOTE: 'board' is a LEGACY slot index (0-2) for IoBoard[] compat.
 */
int orbit_poll_io(SYSTEM_BOARDS board);

/**
 * Poll an extended orbit slot (index 0..NOVA_MAX_ORBITS-1).
 * For slots 0-2 this delegates to orbit_poll_io().
 * For slots 3+ this reads DI/DO into orbit_boards[slot] directly
 * (no IoBoard[] mapping — used for refrigeration/door boards).
 * Returns 0 on success, <0 on error.
 */
int orbit_poll_extended(int slot);

/**
 * Read sensor data from Orbit's RS485 passthrough registers.
 * Works on any orbit slot (0..orbit_board_count-1).
 * Returns number of valid sensor readings, or <0 on error.
 */
int orbit_read_sensors(SYSTEM_BOARDS board);
int orbit_read_sensors_slot(int slot);

/**
 * Write a single digital output via Modbus TCP FC05.
 * This is the "immediate" path used when OutputOn()/OutputOff() need
 * to take effect before the next poll cycle.
 * Returns 0 on success.
 */
int orbit_write_do(SYSTEM_BOARDS board, uint8_t output_index, int value);
int orbit_write_do_slot(int slot, uint8_t output_index, int value);

/**
 * Write analog output value via Modbus TCP FC06.
 * channel: 0 or 1 (AO1 or AO2)
 * value: 0-10000 (mV for 0-10V mode)
 */
int orbit_write_ao(SYSTEM_BOARDS board, uint8_t channel, uint16_t value);
int orbit_write_ao_slot(int slot, uint8_t channel, uint16_t value);

/**
 * Read Orbit board status registers (ID, E-Stop, comm, temp, uptime).
 * Works on any orbit slot.
 */
int orbit_read_status(SYSTEM_BOARDS board);
int orbit_read_status_slot(int slot);

/**
 * Discover which Orbit boards are present on the network.
 * Scans IPs from ORBIT_IP_x.2 through ORBIT_IP_x.{2+NOVA_MAX_ORBITS-1}
 * and populates orbit_boards[].  Legacy slots 0-2 also update IoBoard[].
 * Returns number of boards found.
 */
int orbit_discover(void);

/**
 * Get the orbit slot index for a given DIP switch ID.
 * Returns slot index (0..orbit_board_count-1), or -1 if not found.
 */
int orbit_slot_by_id(uint8_t dipswitch_id);

/**
 * Assign a zone role to an orbit slot.
 * If role is STORAGE and legacy_slot is 0-2, the orbit is also mapped
 * into IoBoard[legacy_slot] for Controls.c compatibility.
 */
void orbit_set_role(int slot, orbit_role_t role, uint8_t zone_id, int8_t legacy_slot);

/* ─── AO Mode Sync ────────────────────────────────────────────────── */

/**
 * Write analog output mode for one channel on an Orbit board.
 * mode: 0 = voltage (0-10V), 1 = current (4-20mA)
 * Writes to Orbit holding registers 2-3.
 */
int orbit_write_ao_mode(SYSTEM_BOARDS board, uint8_t channel, uint16_t mode);
int orbit_write_ao_mode_slot(int slot, uint8_t channel, uint16_t mode);

/**
 * Sync AO modes from Settings to all connected Orbit boards.
 * Called during orbit_discover() and when AO configuration changes.
 */
void orbit_sync_ao_modes(void);

/* ─── VFD Passthrough ─────────────────────────────────────────────── */

/**
 * Write a single VFD register via Orbit's Modbus passthrough.
 * The Orbit relays FC06 to holding reg (100 + vfd_reg) onto its
 * RS-485 VFD bus as a Modbus RTU write to the VFD drive.
 */
int orbit_write_vfd_reg(int slot, uint16_t vfd_reg, uint16_t value);

/**
 * Read VFD registers via Orbit's Modbus passthrough.
 * Reads from holding regs 100+ which the Orbit populates from RS-485.
 */
int orbit_read_vfd_regs(int slot, uint16_t start_reg, uint16_t count,
                        uint16_t *regs_out);

/* ─── Orbit Warning / Alarm Detection ─────────────────────────────── */

/**
 * Warning codes for Orbit-specific faults.
 * These are appended to the standard firmware warnings in SendWarnings().
 * Codes start at 0x100 to avoid collision with APPLICATION WARNING_ITEMS.
 */
#define ORBIT_WARN_BASE           0x100
#define ORBIT_WARN_ESTOP          (ORBIT_WARN_BASE + 0)   /* Orbit reports E-Stop */
#define ORBIT_WARN_SAFE_MODE      (ORBIT_WARN_BASE + 1)   /* Orbit watchdog safe mode */
#define ORBIT_WARN_COMM_LOST      (ORBIT_WARN_BASE + 2)   /* Nova lost contact with Orbit */
#define ORBIT_WARN_SENSOR_FAULT   (ORBIT_WARN_BASE + 3)   /* Sensor returned 0x7FFF */
#define ORBIT_WARN_CPU_OVERTEMP   (ORBIT_WARN_BASE + 4)   /* Orbit CPU > 85°C */
#define ORBIT_WARN_MAX            (ORBIT_WARN_BASE + 5)

#define ORBIT_WARN_SEVERITY_ALARM   2   /* Alarm level (matches proto AlarmSeverity) */
#define ORBIT_WARN_SEVERITY_WARN    1   /* Warning level */

/* ─── Triton-Specific Alarm Codes (from REFRIG-role orbits) ─────────
 *
 * Triton orbits expose a packed alarm bitmap on holding regs 530..535
 * (active) and 536..541 (acked).  Each bit position is stable and matches
 * the TRITON_ALARM_BITS table in orbit-simulator/src/orbitSimulator.ts —
 * never re-number.  Codes start at ORBIT_TRITON_BASE (0x200) so they
 * cannot collide with ORBIT_WARN_* (0x100..0x1FF) or APPLICATION
 * WARNING_ITEMS (0..0xFF).
 */
#define ORBIT_TRITON_BASE                  0x200
#define ORBIT_TRITON_REG_ACTIVE_BASE       530
#define ORBIT_TRITON_REG_ACKED_BASE        536
#define ORBIT_TRITON_REG_COUNT             6      /* 6 × 16 = 96 codes headroom */

#define ORBIT_TRITON_SUC_LOW_P            (ORBIT_TRITON_BASE +  0)
#define ORBIT_TRITON_SUC_HIGH_P           (ORBIT_TRITON_BASE +  1)
#define ORBIT_TRITON_DIS_HIGH_P           (ORBIT_TRITON_BASE +  2)
#define ORBIT_TRITON_DIS_HIGH_T           (ORBIT_TRITON_BASE +  3)
#define ORBIT_TRITON_OIL_LOW_P            (ORBIT_TRITON_BASE +  4)
#define ORBIT_TRITON_LEAK_SUSP            (ORBIT_TRITON_BASE +  5)
#define ORBIT_TRITON_SAF_PHASE            (ORBIT_TRITON_BASE +  6)
#define ORBIT_TRITON_SAF_HP               (ORBIT_TRITON_BASE +  7)
#define ORBIT_TRITON_SAF_LP               (ORBIT_TRITON_BASE +  8)
#define ORBIT_TRITON_SAF_COMP_OL          (ORBIT_TRITON_BASE +  9)
#define ORBIT_TRITON_SAF_COND_OL          (ORBIT_TRITON_BASE + 10)
#define ORBIT_TRITON_SAF_PERMIT           (ORBIT_TRITON_BASE + 11)
#define ORBIT_TRITON_SAF_PROVE            (ORBIT_TRITON_BASE + 12)
#define ORBIT_TRITON_FAIL_SUPERHEAT       (ORBIT_TRITON_BASE + 13)
#define ORBIT_TRITON_FAIL_SUPERHEATLOW    (ORBIT_TRITON_BASE + 14)
#define ORBIT_TRITON_FAIL_DISCHARGE       (ORBIT_TRITON_BASE + 15)
#define ORBIT_TRITON_FAIL_SUCTION         (ORBIT_TRITON_BASE + 16)
#define ORBIT_TRITON_FAIL_OIL             (ORBIT_TRITON_BASE + 17)
#define ORBIT_TRITON_FAIL_OUTSIDE_AIR     (ORBIT_TRITON_BASE + 18)
#define ORBIT_TRITON_FAIL_POWER           (ORBIT_TRITON_BASE + 19)
#define ORBIT_TRITON_SENS_FAULT_SUC_P     (ORBIT_TRITON_BASE + 20)
#define ORBIT_TRITON_SENS_FAULT_DIS_P     (ORBIT_TRITON_BASE + 21)
#define ORBIT_TRITON_SENS_FAULT_OIL_P     (ORBIT_TRITON_BASE + 22)
#define ORBIT_TRITON_SENS_FAULT_SUC_T     (ORBIT_TRITON_BASE + 23)
#define ORBIT_TRITON_SENS_FAULT_DIS_T     (ORBIT_TRITON_BASE + 24)
#define ORBIT_TRITON_SENS_FAULT_LLS_T     (ORBIT_TRITON_BASE + 25)
#define ORBIT_TRITON_SENS_FAULT_AMB_T     (ORBIT_TRITON_BASE + 26)
#define ORBIT_TRITON_BOARD_MISSING_PRESS  (ORBIT_TRITON_BASE + 27)
#define ORBIT_TRITON_BOARD_MISSING_TEMP   (ORBIT_TRITON_BASE + 28)
#define ORBIT_TRITON_NO_DISCHARGE         (ORBIT_TRITON_BASE + 29)
#define ORBIT_TRITON_BAD_OIL_SENSOR       (ORBIT_TRITON_BASE + 30)
#define ORBIT_TRITON_COMM_ERR             (ORBIT_TRITON_BASE + 31)
#define ORBIT_TRITON_MAX                  (ORBIT_TRITON_BASE + 32)

/** Per-slot active-alarm bitfield (bit N = ORBIT_TRITON_BASE + N). */
extern uint32_t triton_alarm_active[NOVA_MAX_ORBITS];
/** Per-slot acked-alarm bitfield (bit N = ORBIT_TRITON_BASE + N). */
extern uint32_t triton_alarm_acked[NOVA_MAX_ORBITS];

/**
 * Poll alarm bitmap regs from every connected REFRIG-role orbit and
 * update triton_alarm_active[]/triton_alarm_acked[].  Should be called
 * at the same cadence as orbit_check_warnings() (after orbit_read_status).
 * Returns 0 on success, or the count of polling errors.
 */
int orbit_check_triton_alarms(void);

/**
 * Per-slot orbit warning state (bitfield of active orbit warnings).
 */
extern uint8_t orbit_warnings[NOVA_MAX_ORBITS];

/**
 * Check all connected Orbit boards for fault conditions and
 * update orbit_warnings[].  Called after orbit_read_status().
 */
void orbit_check_warnings(void);

/**
 * Get the number of active orbit-specific warnings (for SendWarnings).
 * Fills entries[] with (code, severity) pairs.
 * Returns the number of entries written (up to max_entries).
 */
typedef struct {
    uint16_t code;
    uint8_t  severity;
    uint8_t  slot;        /* which Orbit raised this warning */
} orbit_warn_entry_t;

int orbit_get_active_warnings(orbit_warn_entry_t *entries, int max_entries);

/* ─── Sensor Fault Detection ──────────────────────────────────────── */

#define SENSOR_FAULT_VALUE  0x7FFF

/**
 * Check sensor registers for 0x7FFF fault values and replace with 0.
 * Sets ORBIT_WARN_SENSOR_FAULT in orbit_warnings[slot] if any found.
 * Called after orbit_read_sensors_slot().
 */
void orbit_check_sensor_faults(int slot);

/* ─── GDC (Door Controller) Registers ─────────────────────────────── */

#define ORBIT_GDC_REG_DOOR_CMD       300   /* W:  freshAirDoorPct × 10 */
#define ORBIT_GDC_REG_POS_BASE       301   /* R:  actuator 1-5 position × 10 */
#define ORBIT_GDC_REG_ACTIVE_STAGES  306   /* R:  active stage bitfield */
#define ORBIT_GDC_REG_TRAVEL_TIME    307   /* R/W: default travel time (s) */
#define ORBIT_GDC_REG_STAGE_BASE     310   /* W:  stage 1-5 config regs */
#define ORBIT_GDC_REG_NUM_STAGES     315   /* W:  number of active stages */
#define ORBIT_GDC_REG_TOTAL_CAPACITY 316   /* R:  total system capacity (secs) */
#define ORBIT_GDC_REG_CALIBRATE      318   /* W:  calibrate command */
#define ORBIT_GDC_REG_CAL_STATUS     319   /* R:  calibration status */

#define ORBIT_GDC_MAX_ACTUATORS      5
#define ORBIT_GDC_MAX_STAGES         5

/**
 * Write the door command (freshAirDoorPct × 10) to a DOOR-role Orbit.
 * value: 0-1000 (0.0% - 100.0%)
 */
int orbit_write_gdc_door(int slot, uint16_t value);

/**
 * Read actuator positions from a DOOR-role Orbit.
 * Reads regs 301-305 into positions[0..4] (each 0-1000 = 0-100.0%).
 * Returns 0 on success, <0 on error.
 */
int orbit_read_gdc_positions(int slot, uint16_t positions[ORBIT_GDC_MAX_ACTUATORS]);

/**
 * Read active stage bitfield from a DOOR-role Orbit (reg 306).
 */
int orbit_read_gdc_active_stages(int slot, uint16_t *bitfield);

/**
 * Write stage configuration to a DOOR-role Orbit.
 * stage_idx: 0-4 (stage 1-5)
 * door_bitfield: bit 0=door 1, bit 1=door 2, etc.
 * Stages define door priority order for proportional fill.
 */
int orbit_write_gdc_stage(int slot, uint8_t stage_idx,
                          uint8_t door_bitfield);

/**
 * Write the number of active stages to a DOOR-role Orbit (reg 315).
 */
int orbit_write_gdc_num_stages(int slot, uint8_t count);

/**
 * Write the default travel time to a DOOR-role Orbit (reg 307).
 */
int orbit_write_gdc_travel_time(int slot, uint16_t seconds);

/**
 * GDC Safety Interlock Check.
 * Reads DI limit switches and enforces:
 *   - Open + close outputs are NEVER on simultaneously
 *   - Open limit active  → open output forced OFF
 *   - Close limit active → close output forced OFF
 * Called automatically from orbit_poll_extended() for DOOR-role boards.
 * Can also be called independently from a safety/watchdog task.
 * Returns 0 on success, <0 on comm error.
 */
int orbit_gdc_safety_check(int slot);

#endif /* HAL_ORBIT_H */
