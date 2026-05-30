/*
 * orbit_triton.h — TRITON (refrigeration controller) role: register map
 *                  + control task.
 *
 * Mirrors the orbit-simulator TS Triton model (orbitSimulator.ts +
 * tritonControl.ts) at the architectural level. This first cut implements
 * the **production-critical core** only and explicitly defers the
 * higher-order control loops (EXV PID, condenser-fan staging, defrost
 * SM, leak detector, crankcase prove timer, phase monitor) to follow-up
 * work — see "Deferred / stubbed" below. The skeleton here is enough
 * for `OrbitRole_Get() == TRITON` to produce a working LP image that
 * answers Modbus polls, runs compressor cut-in/cut-out, drives unloaders
 * with HP/LP overrides, latches per-sensor failure-mode trips, and
 * enforces a deterministic safe-mode on comm-loss.
 *
 * --- HR map (FC03/FC06/FC16, addresses 400..499) ---
 *
 * SETPOINTS (R/W) — 400..426
 *   400  enabled                       0/1
 *   401  refrigerantType               0=R22 1=R134A 2=R404A 3=R407A
 *                                       4=R407C 5=R410A (others fall back)
 *   402  cutInP_x10                    PSI×10 (signed) — comp start gate
 *   403  cutOutP_x10                   PSI×10 (signed) — comp stop gate
 *   404  minOffTimeSec                 anti-short-cycle off-time
 *   405  minRuntimeSec
 *   406  warmUpSec
 *   407  powerFailMin
 *   408  lowAmbientCutoutF_x10         °F×10 (signed)
 *   409  pumpDownMode                  0=NONE 1=SWITCH 2=REMOTE 3=CONT
 *   410  discHighUnloadP_x10           PSI×10
 *   411  sucLowUnloadP_x10             PSI×10
 *   412  superheatTargetF_x10          °F×10
 *   413  superheatLowF_x10             °F×10 (alarm threshold)
 *   414  unloaderOnPct[0]              0..100 — demand% to LOAD U1
 *   415  unloaderOnPct[1]              0..100 — demand% to LOAD U2
 *   416  unloaderOffPct[0]             0..100 — demand% to UNLOAD U1
 *   417  unloaderOffPct[1]             0..100 — demand% to UNLOAD U2
 *   418  unloaderHpUnloadPsi_x10[0]    PSI×10 — discP ≥ this forces U1 off
 *   419  unloaderHpUnloadPsi_x10[1]
 *   420  unloaderHpLoadPsi_x10[0]      PSI×10 — discP ≤ this clears HP latch
 *   421  unloaderHpLoadPsi_x10[1]
 *   422  unloaderLpUnloadPsi_x10[0]    PSI×10 — sucP ≤ this forces U1 off
 *   423  unloaderLpUnloadPsi_x10[1]
 *   424  unloaderLpLoadPsi_x10[0]      PSI×10 — sucP ≥ this clears LP latch
 *   425  unloaderLpLoadPsi_x10[1]
 *   426  unloaderNormal                0=NO (DO=loaded), 1=NC (DO=unloaded)
 *   427..439  reserved (read 0, write accepted)
 *
 * FAILURE MODES (R/W) — 440..453, two regs per sensor
 *   Pair {mode, delaySec}. mode: 0=ALARM_ONLY, 1=SAFE_OFF, 2=RUN_THROUGH.
 *   Sensor index → addresses:
 *     [0] suctionP    : 440 mode, 441 delaySec
 *     [1] dischargeP  : 442 mode, 443 delaySec
 *     [2] oilP        : 444 mode, 445 delaySec
 *     [3] suctionT    : 446 mode, 447 delaySec
 *     [4] dischargeT  : 448 mode, 449 delaySec
 *     [5] llsT        : 450 mode, 451 delaySec
 *     [6] ambientT    : 452 mode, 453 delaySec
 *
 * LIVE SENSOR VALUES (R) — 460..473, two regs per sensor (mirror order)
 *   Pair {value_x10 (signed), valid 0/1}. Same sensor index as above.
 *   Values are sourced from sensor_block[0..7] each tick; this region is
 *   a read-only convenience view (saves a master from translating
 *   sensor_block layout).
 *
 * LIVE EQUIPMENT STATE (R) — 480..489
 *   480  compressorOn                  0/1
 *   481  compressorStatus              0..14 (0 STANDBY, 1 RUN, 4 PROVE,
 *                                       9 SYSTEM_OFF, 10 ERROR, 12 PUMPDOWN)
 *   482  capacity_stage                0..ORBIT_TRITON_NUM_UNLOADERS
 *   483  unloader_bits                 bit0=U1 on, bit1=U2 on
 *   484  hp_forced_bits                bit0=U1 HP-locked off, bit1=U2
 *   485  lp_forced_bits                bit0=U1 LP-locked off, bit1=U2
 *   486  compressorRuntimeSec_lo
 *   487  compressorRuntimeSec_hi
 *   488  pumpdownSecRemaining
 *   489  demand_pct                    0..100 (mirror of pressure-board ch4)
 *   490  suction_sat_t_F_x10           °F×10 (signed) — derived from sucP
 *   491  superheat_F_x10               °F×10 (signed) — sucT − Tsat(sucP)
 *   492  safe_off_mask_lo              bit i = FAIL_* code i tripped + SAFE_OFF
 *   493  safe_off_mask_hi
 *   494..499  reserved
 *
 * ALARM BITMAPS (R) — split into two contiguous halves below
 *   These collide with the live-equipment block above; we layer them by
 *   keeping the bit-map outside the 400-block range entirely. See the
 *   alarm regs at 530..541 (active+acked, 6 regs each) below.
 *
 * ALARM BITMAPS (R) — 530..541
 *   530..535   active alarm bits   bit numbering matches TRITON_ALARM_BITS
 *   536..541   acked  alarm bits   (ack-all clears the active set when
 *                                    inactive, OR latches all-acked)
 *   542        ack_command         W=1 acks every active bit; reads 0
 *
 * --- DO/DI channel assignments (FIXED at compile time) ---
 *   DO[0]  COMPRESSOR             energized → comp run
 *   DO[1]  UNLOADER1              polarity per `unloaderNormal`
 *   DO[2]  UNLOADER2              polarity per `unloaderNormal`
 *   DO[3]  OIL_PUMP               energized while comp running
 *   DO[4]  EVAP_FAN               energized while comp on (or override)
 *   DO[5..9]  reserved (left at 0)
 *
 * Operator-configurable DO-role mapping (TS `TritonIoConfig.doRole[]`)
 * is intentionally OUT OF SCOPE for this first cut. When sites want
 * different wiring, the right move is to expose a doRole[] HR table
 * here rather than rewire each install. Documented in
 * /memories/repo/lp-triton-port.md.
 *
 * --- Cold-boot semantics (no hardcoded sensor / setpoint values) ---
 *   - All setpoints = 0 (refrigerantType=0 → R22 falls back gracefully
 *     in the conversion table; bridge MUST set the operator value
 *     before the unit is enabled).
 *   - enabled = false. Compressor + unloaders + oil-pump + evap-fan
 *     all OFF. capacity_stage = 0.
 *   - All sensor failure modes = SAFE_OFF (1), delay = 0 — fail-safe:
 *     an unconfigured sensor latches an immediate SAFE_OFF on the
 *     first invalid sample.
 *   - All sensor values undef + invalid until the sensor RTU master
 *     publishes.
 *   - All alarms cleared.
 *
 * --- Safe-mode policy (comm-loss > ORBIT_COMM_LOSS_MS, default 5 s) ---
 *   capacity_stage   := 0
 *   unloader_on[*]   := false
 *   compressor_on    := false
 *   compressor_status:= 9 (SYSTEM_OFF)
 *   DO[0..4]         := false (matches polarity inverted by unloaderNormal
 *                      so the unloader contactors land in their de-energized
 *                      "field-safe" state regardless of NO/NC config)
 *   pumpdown timer   := 0 (no graceful pumpdown — comm loss is treated as
 *                      a hard stop; refrigerant safety > slug protection)
 *   alarms / setpoints / failure-mode latches: PRESERVED (so the master
 *     can read them back on reconnect for SCADA forensics).
 *
 * --- Sensor source convention ---
 *   The sensor RTU master publishes board-0 (HR 200..203) and board-1
 *   (HR 204..207) according to the TRITON-specific layout (mirrors the
 *   TS sim's writeTempBoard / writePressBoard channels):
 *     Board 0 (temp,  °F×10): [0] suctionT  [1] dischargeT
 *                              [2] llsT      [3] ambientT
 *     Board 1 (press, PSI×10): [0] suctionP  [1] dischargeP
 *                              [2] oilP      [3] demand × 10
 *   NOTE: the storage role uses °C×10 throughout; TRITON uses °F×10
 *   per refrigeration industry convention. Documented divergence; do
 *   NOT convert in the bridge.
 *
 * --- Concurrency ---
 *   OrbitTriton_Tick() acquires OrbitState_Lock for the duration of one
 *   tick (~tens of µs of math). HR handlers acquire it for the duration
 *   of a single read or single-register write.
 *
 * --- Deferred / stubbed (NOT in this port) ---
 *   - EXV PID + slew (TS §4)
 *   - Condenser-fan staging / VFD PID (TS §5)
 *   - Defrost state machine (TS §0c, 5.5)
 *   - Leak / low-charge detector (TS §4b)
 *   - Crankcase-prove timer + phase-monitor power-fail tier (TS §0)
 *   - Lead/lag rotation between Tritons (Nova-side concern, never was
 *     here; wires through `groupId`/`rotationOrder` setpoints if added)
 *   These can be ported in follow-up patches without breaking the HR map
 *   above (each new feature gets its own range; the 494..529 gap is
 *   reserved for live-equipment additions, 460+540+ is open for new
 *   setpoints).
 */
#ifndef ORBIT_TRITON_H
#define ORBIT_TRITON_H

#include <stdbool.h>
#include <stdint.h>
#include "orbit_state.h"

#define ORBIT_TRITON_TICK_MS         100U   /* matches sim 100 ms cadence */

/* HR address ranges. */
#define HR_TRITON_SP_BASE            400U
#define HR_TRITON_SP_END             440U   /* exclusive */
#define HR_TRITON_FAIL_BASE          440U
#define HR_TRITON_FAIL_END           454U   /* exclusive */
#define HR_TRITON_LIVE_SENS_BASE     460U
#define HR_TRITON_LIVE_SENS_END      474U   /* exclusive */
#define HR_TRITON_LIVE_EQ_BASE       480U
#define HR_TRITON_LIVE_EQ_END        494U   /* exclusive */
#define HR_TRITON_ALARM_ACTIVE_BASE  530U   /* 6 regs */
#define HR_TRITON_ALARM_ACKED_BASE   536U   /* 6 regs */
#define HR_TRITON_ACK_CMD            542U
#define HR_TRITON_END                543U   /* exclusive */

/* DO channel assignments (see header block). */
#define TRITON_DO_COMPRESSOR         0U
#define TRITON_DO_UNLOADER1          1U
#define TRITON_DO_UNLOADER2          2U
#define TRITON_DO_OIL_PUMP           3U
#define TRITON_DO_EVAP_FAN           4U

/* Sensor index assignments (HR map "FAILURE MODES" + "LIVE SENSOR" + the
 * sensor_block[0..7] mapping). Keep stable — wire-compatible bit numbers
 * in the alarm bitmap depend on this order. */
#define TRITON_SENS_SUC_P            0U
#define TRITON_SENS_DIS_P            1U
#define TRITON_SENS_OIL_P            2U
#define TRITON_SENS_SUC_T            3U
#define TRITON_SENS_DIS_T            4U
#define TRITON_SENS_LLS_T            5U
#define TRITON_SENS_AMB_T            6U

/* Alarm bit positions — append-only. Mirror TS TRITON_ALARM_BITS so the
 * Nova-side decoder can use one shared table. Kept here in firmware so a
 * bridge that loses sync can still decode by name. */
#define TRITON_ALARM_SUC_LOW_P            0U
#define TRITON_ALARM_SUC_HIGH_P           1U
#define TRITON_ALARM_DIS_HIGH_P           2U
#define TRITON_ALARM_DIS_HIGH_T           3U
#define TRITON_ALARM_OIL_LOW_P            4U
/* 5 LEAK_SUSP — leak detector deferred */
#define TRITON_ALARM_SAF_PHASE            6U
#define TRITON_ALARM_SAF_HP               7U
#define TRITON_ALARM_SAF_LP               8U
#define TRITON_ALARM_SAF_COMP_OL          9U
#define TRITON_ALARM_SAF_COND_OL         10U
#define TRITON_ALARM_SAF_PERMIT          11U
/* 12 SAF_PROVE — crankcase-prove deferred */
/* 13 FAIL_SUPERHEAT — superheat-high failure timer */
#define TRITON_ALARM_FAIL_SUPERHEAT      13U
#define TRITON_ALARM_FAIL_SUPERHEAT_LOW  14U
#define TRITON_ALARM_FAIL_DISCHARGE      15U
#define TRITON_ALARM_FAIL_SUCTION        16U
#define TRITON_ALARM_FAIL_OIL            17U
#define TRITON_ALARM_FAIL_OUTSIDE_AIR    18U
/* 19 FAIL_POWER — power-fail tier deferred */
#define TRITON_ALARM_SENS_FAULT_SUC_P    20U
#define TRITON_ALARM_SENS_FAULT_DIS_P    21U
#define TRITON_ALARM_SENS_FAULT_OIL_P    22U
#define TRITON_ALARM_SENS_FAULT_SUC_T    23U
#define TRITON_ALARM_SENS_FAULT_DIS_T    24U
#define TRITON_ALARM_SENS_FAULT_LLS_T    25U
#define TRITON_ALARM_SENS_FAULT_AMB_T    26U

/* Compressor status enum (mirrors TS TritonState.compressorStatus). */
#define TRITON_STATUS_AUTO_STANDBY    0U
#define TRITON_STATUS_AUTO_RUN        1U
#define TRITON_STATUS_PUMPDOWN       12U
#define TRITON_STATUS_SWITCH_OFF      7U
#define TRITON_STATUS_SYSTEM_OFF      9U
#define TRITON_STATUS_ERROR          10U

void OrbitTriton_Init(void);
void orbit_triton_task(void *args);

uint8_t OrbitTriton_ReadHrBlock (uint16_t start, uint16_t count, uint16_t *out);
uint8_t OrbitTriton_WriteHrSingle(uint16_t addr, uint16_t value);
uint8_t OrbitTriton_WriteHrBlock (uint16_t start, uint16_t count, const uint16_t *vals);

/* Safe-mode enforcement — caller (orbit_safemode_task) holds OrbitState_Lock. */
void OrbitTriton_EnforceSafeMode(void);

/* Saturation helpers — exposed for the rare bridge-side cross-check.
 * `tempF` in °F; returns PSI. `pressurePSI` in PSI; returns °F. The
 * underlying coefficients are lifted verbatim from GRC Refrigerant.c
 * via orbit-simulator/src/tritonControl.ts. */
float OrbitTriton_PsatF(uint8_t tritonRefrigerantType, float tempF);
float OrbitTriton_TsatF(uint8_t tritonRefrigerantType, float pressurePSI);

#endif /* ORBIT_TRITON_H */
