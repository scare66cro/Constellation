/* nova_vfd.h
 *
 * Nova-native VFD (variable frequency drive) state cache.
 *
 * Polls Orbit's VFD passthrough register block (HR 100-163) on every
 * discovered ORBIT_ROLE_STORAGE board and caches per-drive status. Up
 * to 4 drives per orbit × multiple storage orbits → NOVA_MAX_VFD_DRIVES
 * total. The orbit's RS-485 Port B forwards Modbus RTU traffic to real
 * ABB ACS310 / ACS380 drives; the orbit mirrors each drive's state into
 * a 16-register slot in HR 100+ which we read here.
 *
 * Per-drive 16-register slot layout (matches
 * `orbit-simulator/src/vfdBusClient.ts` lines 24-44):
 *   0  = Control Word (R/W)
 *   1  = Speed Reference 0–10000 (R/W)
 *   2  = Status Word (R) — 0 means drive not present / not responding
 *   3  = Actual Speed 0–10000 (R)
 *   4  = Output Frequency (0.1 Hz) (R)
 *   5  = Freq Reference (0.1 Hz) (R)
 *   6  = Motor Speed RPM (R)
 *   7  = Motor Current (0.1 A) (R)
 *   8  = Motor Torque (0.1 %) (R)
 *   9  = Motor Power (0.1 kW) (R)
 *  10  = DC Bus Voltage (V) (R)
 *  11  = Output Voltage (0.1 V) (R)
 *  12  = Drive Temperature (°C) (R)
 *  13  = Active Fault Code (R)
 *  14  = reserved
 *  15  = reserved
 *
 * ABB status-word bit layout: bit 3 = Fault, bit 2 = Running.
 *
 * This module is the SAFETY SOURCE OF TRUTH for "is a VFD-controlled
 * fan faulted right now?" — FanFailChk() in nova_failures.c calls
 * nova_vfd_any_faulted() to OR-in to its existing condition, reusing
 * the legacy Settings.Failure[FAIL_FAN].Timer escalation. The bridge
 * (constellation-ui/server/src/vfdClient.ts) still polls drives
 * directly over Modbus TCP for the /level2/fans display page, but it
 * no longer makes alarm decisions — that logic moved here.
 */
#ifndef NOVA_VFD_H
#define NOVA_VFD_H

#include <stdbool.h>
#include <stdint.h>

#define NOVA_MAX_VFD_DRIVES        12
#define NOVA_VFD_REGS_PER_DRIVE    16
#define NOVA_VFD_DRIVES_PER_ORBIT  4   /* HR 100-163 = 4 × 16 */

/* ABB status-word bits (also Phase Tech and most Modbus-profile drives). */
#define VFD_SW_FAULT     (1u << 3)
#define VFD_SW_RUNNING   (1u << 2)

typedef struct {
    bool      present;          /* status_word != 0 (drive responded recently) */
    bool      faulted;          /* SW bit 3 set */
    bool      running;          /* SW bit 2 set */
    uint16_t  status_word;
    uint16_t  fault_code;       /* offset 13; vendor-specific code */
    uint16_t  actual_speed_pct; /* 0-10000 (= 0.00-100.00 %) */
    uint16_t  output_freq_x10;  /* 0.1 Hz */
    uint16_t  motor_current_x10;/* 0.1 A */
    uint16_t  drive_temp_c;     /* °C */
    uint8_t   orbit_slot;       /* which orbit_boards[] slot it lives on */
    uint8_t   slot_idx;         /* 0..3: which drive within that orbit's HR block */
    uint8_t   drive_idx;        /* 1..NOVA_MAX_VFD_DRIVES — flat global index */
} nova_vfd_state_t;

/** Initialize the cache. Call once at startup. */
void nova_vfd_init(void);

/** Poll all storage orbits' VFD register blocks and refresh the cache.
 *  Safe to call from the orbit polling thread; uses the existing
 *  per-orbit Modbus TCP connection (no separate socket). Returns the
 *  number of present drives. */
int  nova_vfd_tick(void);

/** Number of drives currently `present` in the cache. */
int  nova_vfd_count(void);

/** Get a pointer to a drive's cached state by flat index 0..count-1.
 *  Returns NULL if idx is out of range. Pointer is stable for the
 *  lifetime of the cache (statically allocated). */
const nova_vfd_state_t *nova_vfd_get(int idx);

/** Returns true if any present drive currently shows the fault bit.
 *  When true, *out_drive_idx (1..NOVA_MAX_VFD_DRIVES) and *out_fault_code
 *  are populated with the FIRST faulted drive found. Either pointer may
 *  be NULL. */
bool nova_vfd_any_faulted(uint8_t *out_drive_idx, uint16_t *out_fault_code);

#endif /* NOVA_VFD_H */
