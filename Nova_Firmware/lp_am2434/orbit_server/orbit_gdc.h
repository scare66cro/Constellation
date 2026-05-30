/*
 * orbit_gdc.h — GDC (Gellert Door Controller) role: register map + task
 *
 * Mirrors orbit-simulator/src/orbitSimulator.ts (GDCActuator/GDCStage,
 * updateGDCActuators, updateCalibration, computeDoorTargets,
 * startCalibration). The TS implementation expressed positions/percentages
 * as float; firmware uses fixed-point (×10, range 0..1000 = 0..100.0%)
 * to keep all wire values in 16-bit Modbus HRs without scaling tricks.
 *
 * --- HR map (FC03/FC06/FC16) ---
 *   HR  300        door_pct_x10        0..1000 (commanded fresh-air %)  R/W
 *   HR  301        default_travel_ms   ms (used for uncalibrated doors)  R/W
 *   HR  302        active_stage_count  current count of opened stages   R
 *   HR  303        calibrating         0/1                              R
 *   HR  304        cal_phase           0=idle,1=opening,2=closing       R
 *   HR  305        cal_request         W=1 starts calibration           W
 *   HR  306..309   reserved                                             —
 *   HR  310..314   actuator[0..4].pos_x10           0..1000             R
 *   HR  315..319   actuator[0..4].target_x10        0..1000             R
 *   HR  320..324   actuator[0..4].status_bits       (see below)         R
 *                    bit0 = moving
 *                    bit1 = open_switch
 *                    bit2 = close_switch
 *                    bit3 = calibrated
 *   HR  325..329   actuator[0..4].stage             0..5 (0=unassigned) R/W
 *   HR  330..334   actuator[0..4].open_travel_ms    0=uncalibrated      R/W
 *   HR  335..339   actuator[0..4].close_travel_ms   0=uncalibrated      R/W
 *
 * Anything outside [300..339] is delegated to the storage handlers so
 * the GDC orbit board still exposes its sensor block (HR 200..263),
 * AO regs (HR 0..3), VFD pass-through (HR 100..147), and shared status
 * (HR 40000..40006). Coil and discrete-input maps come straight from
 * orbit_storage (DI/DO 0..9 + DC24V 11..14) — the GDC tick writes
 * s->do_[2i] / s->do_[2i+1] (open/close pulses) and s->di[2i] /
 * s->di[2i+1] (open/close limit switches) so the master can observe
 * actuator drive state via FC01/FC02 alongside positions via FC03.
 *
 * --- Cold-boot semantics (no hardcoded calibration) ---
 *   - All actuators: pos=0, target=0, stage=0 (unassigned),
 *     open_travel_ms=0, close_travel_ms=0, calibrated=false,
 *     close_switch=true (assume parked at 0%).
 *   - default_travel_ms = 0 → uncalibrated doors with no programmed
 *     default do not move (the bridge programs both before commanding
 *     a door %).
 *   - door_pct_x10 = 0.
 *
 * --- Safe-mode policy (comm-loss > ORBIT_COMM_LOSS_MS) ---
 *   For each of the 5 actuators:
 *     - target := current position (freeze in place, worm gear holds)
 *     - moving := false
 *     - DO[2i] (open)  := false   ← both pulse outputs cleared
 *     - DO[2i+1] (close) := false
 *   door_pct_x10 is NOT zeroed; the GDC tick is gated on s->safe_mode
 *   so it won't try to chase the old command. On comm-restore the
 *   master either re-sends pct or freezes — current position is
 *   preserved either way.
 *   Calibration in flight is aborted (calibrating cleared, phase=idle).
 *
 * --- Concurrency ---
 *   OrbitGdc_Tick() acquires OrbitState_Lock for the duration of one
 *   tick (≈10 µs of math, no blocking I/O). HR handlers acquire it
 *   for the duration of a single read or single-register write.
 */
#ifndef ORBIT_GDC_H
#define ORBIT_GDC_H

#include <stdbool.h>
#include <stdint.h>

#define ORBIT_GDC_ACTUATORS         5U
#define ORBIT_GDC_TICK_MS           100U   /* matches sim setInterval(100) */

/* HR address constants — see header block above. */
#define HR_GDC_BASE                 300U
#define HR_GDC_DOOR_PCT             300U
#define HR_GDC_DEFAULT_TRAVEL_MS    301U
#define HR_GDC_ACTIVE_STAGE_COUNT   302U
#define HR_GDC_CALIBRATING          303U
#define HR_GDC_CAL_PHASE            304U
#define HR_GDC_CAL_REQUEST          305U
#define HR_GDC_POS_BASE             310U
#define HR_GDC_TARGET_BASE          315U
#define HR_GDC_STATUS_BASE          320U
#define HR_GDC_STAGE_BASE           325U
#define HR_GDC_OPEN_MS_BASE         330U
#define HR_GDC_CLOSE_MS_BASE        335U
#define HR_GDC_END                  340U   /* exclusive */

/* Status-bit masks for HR 320..324. */
#define GDC_STATUS_MOVING           0x0001U
#define GDC_STATUS_OPEN_SWITCH      0x0002U
#define GDC_STATUS_CLOSE_SWITCH     0x0004U
#define GDC_STATUS_CALIBRATED       0x0008U

/* Calibration phases. */
#define GDC_CAL_IDLE                0U
#define GDC_CAL_OPENING             1U
#define GDC_CAL_CLOSING             2U

void OrbitGdc_Init(void);                        /* defaults already in OrbitState_Init */
void orbit_gdc_task(void *args);                 /* 10 Hz state-machine task */

/* Modbus dispatch: handle GDC-owned HR addresses; delegates to
 * storage_* for anything outside [300..339]. */
uint8_t OrbitGdc_ReadHrBlock(uint16_t start, uint16_t count, uint16_t *out);
uint8_t OrbitGdc_WriteHrSingle(uint16_t addr, uint16_t value);
uint8_t OrbitGdc_WriteHrBlock(uint16_t start, uint16_t count, const uint16_t *vals);

/* Safe-mode enforcement, called by orbit_safemode while holding the lock. */
void OrbitGdc_EnforceSafeMode(void);

#endif /* ORBIT_GDC_H */
