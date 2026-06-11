/**
 * Orbit Simulator — Modbus TCP Server + REST API + Web Panel
 * ──────────────────────────────────────────────────────────
 * Simulates an Orbit I/O board (AM2432) on the Constellation Modbus TCP
 * network.  Nova polls each Orbit as a Modbus TCP client on 10.47.27.x.
 *
 * Each Orbit instance exposes:
 *   • 10 Digital Inputs  (coils 0-9, read via FC02)
 *   • 10 Digital Outputs (coils 0-9, read via FC01, write via FC05/FC15)
 *   • 2 Analog Outputs   (holding regs 0-1, FC03/FC06/FC16)
 *   • 4 × 24V DC outputs (coils 10-13 in output space)
 *   • E-Stop input       (coil 10 in input space)
 *   • VFD RS-485 passthrough register block (holding regs 100+)
 *   • Sensor RS-485 passthrough register block (holding regs 200+)
 *   • Status/config registers (holding regs 40000+)
 *
 * The simulator serves:
 *   • Modbus TCP on ORBIT_TCP_PORT (default 5502)
 *   • REST API + web panel on ORBIT_API_PORT (default 9010)
 *
 * Env vars:
 *   ORBIT_ID       — dipswitch address 2-33 (default: 2)
 *   ORBIT_TCP_PORT — Modbus TCP listen port (default: 5502)
 *   ORBIT_API_PORT — REST API listen port (default: 9010)
 */

import * as net from 'net';
import * as http from 'http';
import * as fs from 'fs';
import * as path from 'path';
import { fileURLToPath } from 'url';
import { EventEmitter } from 'events';
import { saveConfig, loadConfig } from './simConfig.js';
import { SensorBusClient, type SensorBoardData } from './sensorBusClient.js';
import { VFDBusClient } from './vfdBusClient.js';
import { SENSOR_VAL_UNDEF as ADC_UNDEF } from './adcConversion.js';
import type { VFDSimulator } from './vfdSimulator.js';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

// ─── Modbus TCP Constants ──────────────────────────────────────────────────
const MBAP_HEADER_LEN  = 7;
const MODBUS_PROTOCOL  = 0;

const FC_READ_COILS            = 0x01;
const FC_READ_DISCRETE_INPUTS  = 0x02;
const FC_READ_HOLDING_REGS     = 0x03;
const FC_READ_INPUT_REGS       = 0x04;
const FC_WRITE_SINGLE_COIL     = 0x05;
const FC_WRITE_SINGLE_REG      = 0x06;
const FC_WRITE_MULTIPLE_COILS  = 0x0F;
const FC_WRITE_MULTIPLE_REGS   = 0x10;

const EX_ILLEGAL_FUNCTION      = 0x01;
const EX_ILLEGAL_DATA_ADDRESS  = 0x02;
const EX_ILLEGAL_DATA_VALUE    = 0x03;

// ─── Sensor Type Constants (match legacy Analog_Input.h) ──────────────────
const SENSOR_TYPE_IR_TEMP  = 0;
const SENSOR_TYPE_HUMID    = 1;
const SENSOR_TYPE_CO2      = 2;
const SENSOR_TYPE_TEMP     = 3;
// 0-500 PSI 4-20 mA pressure transducer (Triton refrigerant pressures).
const SENSOR_TYPE_PRESSURE = 4;
// Generic 4-20 mA analog input (used for the Triton "demand" channel).
const SENSOR_TYPE_MA       = 5;
// NEW Gellert 4-20 mA static-pressure transducer (0–2.5 "wc). Nibble 11
// matches AS2 (ANALOG_SENSOR_TYPE_STATIC_PRESS=11), Nova firmware, and the UI.
// Wire encoding is ×100 ("wc one-hundredths) — NOT the ×10 temp/humid form.
const SENSOR_TYPE_STATIC_PRESSURE = 11;
const SENSOR_TYPE_NONE     = 0xF;
const SENSOR_VAL_UNDEF    = 0x7FFF;

export interface AnalogBoardSim {
  address: number;        // RS-485 address 0-31
  type: number;           // 0=IR, 1=Humid, 2=CO2, 3=Temp
  present: boolean;
  label: string;
  sensorTypes: number[];  // 4 sensor type nibbles
  sensorValues: number[]; // 4 values: temp/humid ×10, CO2 raw ppm
  sensorLabels: string[];
}

// ─── Orbit Role (firmware variant) ─────────────────────────────────────────

export enum OrbitRole {
  UNASSIGNED = 0,
  STORAGE = 1,      // Standard 10 DI / 10 DO I/O board for storage control
  GDC = 2,          // Gellert Door Controller: 5 actuators (open/close)
  TRITON = 3,       // Triton: refrigeration/cooling control module
  PULSAR = 4,       // Pulsar: advanced I/O with expanded capabilities
}

// ─── GDC Actuator State ────────────────────────────────────────────────────

export interface GDCActuator {
  /** Current position 0-100% (0=fully closed, 100=fully open) */
  position: number;
  /** Target position 0-100% */
  target: number;
  /** True if actuator is currently moving */
  moving: boolean;
  /** Open limit switch triggered */
  openSwitch: boolean;
  /** Close limit switch triggered */
  closeSwitch: boolean;
  /** Label for this actuator */
  label: string;
  /** Which stage this door is assigned to (1-based, 0 = unassigned) */
  stageAssignment: number;
  /** Calibrated open travel time in seconds (0 = uncalibrated, uses global default) */
  openTravelTime: number;
  /** Calibrated close travel time in seconds (0 = uncalibrated, uses global default) */
  closeTravelTime: number;
  /** True if this actuator has been calibrated */
  calibrated: boolean;
}

/**
 * GDC Stage Configuration - defines door open priority order.
 * Doors fill proportionally in stage order: stage 1 doors open first,
 * then stage 2, etc. Multiple doors on the same stage open together.
 */
export interface GDCStage {
  /** Stage number (1-5) — also the priority order */
  stageNum: number;
  /** Label for this stage */
  label: string;
}

// ─── Triton (Refrigeration) State ──────────────────────────────────────────

/** A single live sensor reading on a Triton refrigeration unit. */
export interface TritonSensor {
  /** Current value in engineering units (PSI for pressure, °F for temp) */
  value: number;
  /** True if reading is valid (transducer connected, not faulted) */
  valid: boolean;
  /** Low-alarm threshold (engineering units); NaN = disabled */
  lowAlarm: number;
  /** High-alarm threshold (engineering units); NaN = disabled */
  highAlarm: number;
}

/** A latched alarm on a Triton unit. */
export interface TritonAlarm {
  /** Stable alarm code: 'SUC_LOW_P' | 'DIS_HIGH_P' | 'OIL_LOW_P' | … */
  code: string;
  /** Human-readable label */
  label: string;
  /** True if alarm condition is currently true */
  active: boolean;
  /** True if user has acknowledged this alarm */
  acked: boolean;
  /** ms timestamp first asserted */
  timestamp: number;
}

/** Triton refrigeration-unit setpoints (writable).
 *
 *  Modeled on the Gellert Refrigeration Controller (GRC) settings struct
 *  (`COMPRESSOR_CTRL` + `EXPANSION_VALVE` + `DEFROST_CTRL` + `PID_CTRL` in
 *  GRC/ARM_Refrigeration/Application/SettingsTypes.h).  A single Triton
 *  drives ONE compressor — racks are built by adding more Tritons (Nova
 *  handles lead/lag rotation between them).
 */
export interface TritonSetpoints {
  // ── Capacity / start-stop control ──
  /** Compressor cut-in suction pressure (PSI) — start when suction climbs above this */
  cutInP: number;
  /** Compressor cut-out suction pressure (PSI) — stop when suction falls below this */
  cutOutP: number;
  /** Anti-short-cycle minimum off time (seconds) */
  minOffTime: number;
  /** Minimum runtime once started (seconds) */
  minRuntime: number;
  /** Crankcase warm-up time (seconds) before first start */
  warmUpSec: number;
  /** Compressor prove time (seconds) — must see oil/disch pressure within this */
  proveSec: number;
  /** Power-fail timeout (minutes) — if power was off > this, force crankcase prove */
  powerFailMinutes: number;
  /** Crankcase run hours before allowing compressor to start */
  crankcaseRunHours: number;
  /** Low-ambient temperature cutout (°F, signed) — won't run colder than this */
  lowAmbientCutoutF: number;
  /** Pump-down mode: 0=NONE, 1=SWITCH, 2=REMOTE, 3=CONTINUOUS */
  pumpDownMode: number;
  /** Variable-start minimum % (for soft-start drives) */
  variableStartPct: number;
  /** Refrigerant type enum (0=R22, 1=R134A, 2=R404A, 3=R407A, 4=R407C, 5=R410A,
   *                         6=R448A, 7=R449A, 8=R450A, 9=R454A, 10=R513A,
   *                         11=R600A, 12=R744 (CO2)) */
  refrigerantType: number;
  /** Lead/lag — group ID and operator priority (1..6) within the group */
  groupId: number;
  rotationOrder: number;
  /** Hours between forced lead-compressor rotations (Nova-side rotation engine
   *  reads this; we just store it on the Triton so the value travels with the
   *  unit's NVRAM). */
  rotateHours: number;

  // ── Superheat (compressor protection) ──
  /** Target superheat for EXV control (°F) */
  superheatTarget: number;
  /** Low superheat alarm threshold (°F) */
  superheatLowF: number;
  /** Superheat window high (°F) — flooding band */
  superheatWindowHighF: number;
  /** Superheat window low (°F) */
  superheatWindowLowF: number;

  // ── Forced-unload thresholds (pressure-based capacity reduction) ──
  /** Discharge-pressure unload threshold (PSI) */
  discHighUnloadP: number;
  /** Suction-pressure low-unload threshold (PSI) */
  sucLowUnloadP: number;

  // ── Condenser fans ──
  /** Configured number of condenser fans (1..6) */
  condFanCount: number;
  /** VFD mode for cond fans: 0=STAGED (relays), 1=VFD (analog out) */
  condFanVfdMode: number;
  /** VFD min/max % when in VFD mode */
  condFanVfdMinPct: number;
  condFanVfdMaxPct: number;
  /** Head-pressure setpoint for cond-fan VFD (PSI) */
  condFanVfdSetpointP: number;
  /** Per-stage condenser-fan staging discharge-pressure on/off (PSI). 6 stages.
   *  Used directly when `condenserMode == 0` (FIXED).  Otherwise the
   *  staging is done with `fanDiffOnP/fanDiffOffP` relative to the
   *  computed floating-head target. */
  fanStageOnP: number[];
  fanStageOffP: number[];

  // ── Floating-head condenser control (mirrors GRC TargetDischarge) ──
  /** Condenser-fan target mode:
   *    0 = FIXED      → target = `condFanVfdSetpointP`
   *    1 = FLOATING   → target = clamp(P_sat(OAT + condApproachF), min, max)
   *    2 = BALANCED   → same target math; reserved for true VFD-PID control
   *  Defaults to FLOATING.  Falls back to FIXED when the OAT sensor is
   *  invalid (matches GRC behaviour). */
  condenserMode: number;
  /** Condenser approach (°F) added to OAT before P_sat lookup.  GRC used
   *  0 °F (which makes the target physically unreachable); 10 °F is the
   *  industry rule of thumb for finned air-cooled condensers and lets
   *  fans actually cycle off when ambient drops. */
  condApproachF: number;
  /** Floating-head floor (PSI).  Mirrors GRC `MinimumSetpoint` — keeps
   *  the head high enough for adequate liquid-line pressure / EXV feed. */
  condMinHeadP: number;
  /** Floating-head ceiling (PSI).  Mirrors GRC `P_sat(105°F)` upper clamp. */
  condMaxHeadP: number;
  /** Per-stage cond-fan ON differentials (PSI above target).  6 stages.
   *  GRC default: `(i+1) * FAN_DIFFERENTIAL` (e.g. 10/20/30/40/50/60). */
  fanDiffOnP: number[];
  /** Per-stage cond-fan OFF differentials (PSI above target).  6 stages.
   *  GRC default: 5 PSI for every stage. */
  fanDiffOffP: number[];

  // ── EXV / Expansion valve ──
  /** EXV PID gains (×10 storage in regs) */
  exvKp: number;
  exvKi: number;
  exvKd: number;
  /** EXV travel limits (%) */
  exvMinPct: number;
  exvMaxPct: number;
  /** Manual EXV opening (%) — used when manualMode != auto */
  exvManualPct: number;
  /** EXV cold-start opening (%) used during the warm-up window after the
   *  compressor cuts in.  Mirrors GRC `Settings.Expansion.StartPercent`
   *  (default 30 %).  The PID's integrator is seeded so its initial
   *  output matches this percentage exactly, avoiding a step on hand-off. */
  exvStartPct: number;
  /** EXV warm-up duration (minutes) following compressor cut-in.  During
   *  this window the EXV is held at `exvStartPct` and PID output is
   *  ignored.  Mirrors GRC `Settings.Expansion.Warmup` (default 1 min). */
  exvWarmupMin: number;
  /** EXV PID sample period (seconds).  Mirrors GRC `PID_PARAMS.U`
   *  (default 3 s).  PID is recomputed at most once per `exvSamplePeriod`
   *  seconds; between samples the slew-rate-limited output is held. */
  exvSamplePeriod: number;
  /** Subcooling target (°F) */
  subcoolingTarget: number;

  // ── Defrost ──
  /** Defrost mode: 0=NONE, 1=TIMED, 2=DEMAND, 3=HOT_GAS, 4=ELEC.
   *  This controls *how* defrost runs (fan/output behaviour).  The
   *  *trigger* is independent — `defrostIntervalHours` always drives
   *  the override-timer initiation.  `MANUAL` is always available via
   *  the REST API regardless of mode (mirrors GRC). */
  defrostMode: number;
  /** Defrost stages (1..2) — GRC supports up to 2 hot-gas stages */
  defrostStages: number;
  /** Defrost interval (hours) — once this many hours pass since the last
   *  defrost end, a TIMED defrost initiates automatically.  Mirrors GRC
   *  `Defrost.OverrideTimer`. */
  defrostIntervalHours: number;
  /** Defrost max duration (minutes) — TIME-termination ceiling (also a
   *  hard upper bound for TEMPERATURE/PRESSURE termination).  Mirrors
   *  GRC `Defrost.TerminationTime`. */
  defrostMaxMinutes: number;
  /** Defrost termination temperature (°F) — when termType==TEMPERATURE,
   *  defrost ends once `dischargeT >= defrostTermT`.  Mirrors GRC
   *  `Defrost.TerminationTemperature`. */
  defrostTermT: number;
  /** Defrost termination type:
   *    0 = TIME        — terminate after `defrostMaxMinutes`
   *    1 = TEMPERATURE — terminate when `dischargeT >= defrostTermT`
   *                       (capped by `defrostMaxMinutes`)
   *    2 = PRESSURE    — terminate when `dischargeP >= defrostTermP`
   *                       (capped by `defrostMaxMinutes`)
   *  Mirrors GRC `Defrost.TerminationType`. */
  defrostTermType: number;
  /** Defrost termination pressure (PSI) — when termType==PRESSURE,
   *  defrost ends once `dischargeP >= defrostTermP`.  Mirrors GRC
   *  `Defrost.TerminationPressure`. */
  defrostTermP: number;
  /** Drip / pump-out time after defrost (seconds).  Compressor stays
   *  off and condenser fans stay off for this many seconds after the
   *  defrost output drops, so meltwater drains before the system runs
   *  again.  Mirrors GRC `Defrost.DripTime` (called "drip" in HMI). */
  dripTimeSec: number;
  /** Pump down before starting defrost (0/1).  When 1, a defrost
   *  initiation while the compressor is running first commands a
   *  pumpdown (suction → cutOutP) before energizing EQ_DEFROST.  When
   *  0, the compressor is dropped immediately and defrost begins on
   *  the next tick.  Mirrors GRC `Defrost.PumpdownBefore`. */
  pumpDownBeforeDefrost: number;

  // ── Trend-based (DEMAND) defrost initiation ── (Phase 5.5)
  // Mirrors GRC `DefrostCtrl.c §1` `Trend[].trend[128]` ring + trigger.
  // **Disabled by default** (`defrostTrendVarTimerMin == 0`) — set to a
  // positive value to enable trend-based initiation.  When enabled, the
  // SM samples suction PSI once per minute while the compressor is on,
  // and once `defrostTrendVarTimerMin` worth of samples is collected,
  // computes the rolling mean.  When `currentSuction <= mean -
  // defrostTrendDiffP` AND coils-freezing predicate AND condition holds
  // for `defrostTrendInitiateMin` minutes, fires a TREND defrost.
  /** Warm-up gate (minutes of compressor-on samples) before the trend
   *  reading is trusted.  GRC `Defrost.VariableTimer`.  Default 0
   *  (= TREND initiation disabled). */
  defrostTrendVarTimerMin: number;
  /** Sustain time (minutes) the suction must remain `≤ trend - diff`
   *  before the SM commits to a TREND defrost.  GRC
   *  `Defrost.InitiateTimer`. */
  defrostTrendInitiateMin: number;
  /** Suction-pressure differential (PSI) below the rolling mean that
   *  qualifies as "coils icing up".  GRC `Defrost.SuctionDifferential`.
   *  Default 5 PSI. */
  defrostTrendDiffP: number;

  // ── Capacity stage / unloaders ── (Triton supports up to 2 unloaders)
  // Mirrors GRC `Settings.Compressor.Stage[]` (load on demand crossing)
  // + `Settings.Compressor.HighHeadPressure[]` (HP override) +
  // `Settings.Compressor.LowSuctionOverride[]` (LP override) +
  // `UnloaderNormal` polarity flag.  Triton uses the operator-mapped
  // DO_ROLE.UNLOADER1 / UNLOADER2 slots in `ioConfig.doRole`.
  /** Demand % at/above which unloader[i] loads (energizes — adds
   *  capacity).  GRC `Stage.On`.  Defaults: U1=30, U2=40. */
  unloaderOnPct: number[];
  /** Demand % at/below which unloader[i] unloads (de-energizes — sheds
   *  capacity).  GRC `Stage.Off`.  Must be < `unloaderOnPct[i]` for
   *  hysteresis.  Defaults: U1=20, U2=30. */
  unloaderOffPct: number[];
  /** Discharge PSI at/above which unloader[i] is forced OFF (HP
   *  override).  GRC `HighHeadPressure[i].unload`.  Default 350 PSI. */
  unloaderHpUnloadPsi: number[];
  /** Discharge PSI at/below which the HP override clears.  GRC
   *  `HighHeadPressure[i].load`.  Default 320 PSI. */
  unloaderHpLoadPsi: number[];
  /** Suction PSI at/below which unloader[i] is forced OFF (LP
   *  override — protects compressor from low-side starvation).  GRC
   *  `LowSuctionOverride[i].unload`.  Default 5 PSI. */
  unloaderLpUnloadPsi: number[];
  /** Suction PSI at/above which the LP override clears.  GRC
   *  `LowSuctionOverride[i].load`.  Default 15 PSI. */
  unloaderLpLoadPsi: number[];
  /** Unloader contactor polarity: 0 = NORMALLY_OPENED (DO energized →
   *  loaded), 1 = NORMALLY_CLOSED (DO energized → unloaded).  Mirrors
   *  GRC `Compressor.UnloaderNormal`.  Default 0 (NO). */
  unloaderNormal: number;

  // ── PID (capacity + cond-fan + EXV) ──
  /** Capacity PID gains ×10 */
  capP: number; capI: number; capD: number; capU: number;
  /** Condenser-fan PID gains ×10 */
  condP: number; condI: number; condD: number; condU: number;

  // ── Safe-mode policy (deferral #4) ──
  // Operator-configurable per-equipment behaviour while `safeOffMask != 0`
  // OR `safeties.lockoutMask != 0`.  Compressor + unloaders are NEVER in
  // policy: refrigeration safety pins them OFF.  Defaults match the
  // original “all-zero” behaviour for backward compatibility EXCEPT
  // condenser fans, which default to ALL_ON (industry practice: keep the
  // condenser dumping residual head pressure even after a fault).
  /** Cond-fan policy: 0=ALL_OFF, 1=HOLD_LAST, 2=ALL_ON  (default 2). */
  safePolicyCondFans:   number;
  /** When `condFanVfdMode==1`, the VFD % to drive while in safe mode.
   *  Default 100 (full speed) so residual head pressure is dumped fast. */
  safePolicyCondVfdPct: number;
  /** EXV % to drive while in safe mode.  Default 0 (close) to prevent
   *  liquid flood-back into the off compressor on restart. */
  safePolicyExvPct:     number;
  /** Oil-pump policy: 0=OFF, 1=HOLD_LAST  (default 0). */
  safePolicyOilPump:    number;

  // ── Leak / low-charge detection (superheat-trend based) ──
  /** 0 = disabled, 1 = enabled.  When enabled, the simulator monitors the
   *  exponentially-weighted average superheat and EXV opening while the
   *  compressor is in a stable run; sustained high-SH + EXV-near-max is the
   *  classic field signature of an undercharge / refrigerant leak. */
  leakDetectEnabled: number;
  /** Superheat margin (°F) above `superheatTarget` that is considered
   *  abnormal once the EWMA has settled.  Default 8 °F. */
  leakSuperheatMarginF: number;
  /** EXV open-percent threshold — only flag a leak when the filtered EXV
   *  position is at or above this value (refrigerant starvation forces the
   *  valve wide open).  Default 85 %. */
  leakExvOpenPct: number;
  /** Sustain time (minutes) both conditions must hold before the alarm
   *  latches.  Default 15 min — long enough to ride through normal pull-down
   *  and defrost-recovery transients. */
  leakSustainMinutes: number;
}

/** Per-channel failure handling — modeled on GRC `FAILURE_MODE`. */
export interface TritonFailureMode {
  /** 0 = ALARM_ONLY, 1 = SAFE_OFF (compressor stops), 2 = RUN_THROUGH */
  mode: number;
  /** Delay seconds before the failure latches */
  delaySec: number;
}

/** All sensor-failure handling modes for a Triton. */
export interface TritonFailureModes {
  suctionP: TritonFailureMode;
  dischargeP: TritonFailureMode;
  oilP: TritonFailureMode;
  suctionT: TritonFailureMode;
  dischargeT: TritonFailureMode;
  llsT: TritonFailureMode;
  ambientT: TritonFailureMode;
}

/** Per-channel runtime state for the GRC failure-timer machinery.
 *
 *  Each entry tracks how long the configured threshold has been
 *  continuously exceeded, plus a latch flag set when the timer expires.
 *  The latch clears via the ack-all command (Modbus reg 334).
 */
export interface TritonFailureState {
  /** Seconds the threshold has been continuously violated this run */
  overSec: number;
  /** True once `overSec >= delaySec`; latched until ack-all */
  tripped: boolean;
  /**
   * Phase 6: GRC `CompressorPumpdown(); SetCompressorFailure();` parity.
   * Set the first tick the latch trips so we arm a one-shot pumpdown
   * countdown rather than slamming the compressor off via safeOffMask.
   * Cleared when `tripped` falls back to false (ack-all).
   */
  _pumpdownArmed?: boolean;
}

export interface TritonFailureStates {
  suctionP:   TritonFailureState;
  dischargeP: TritonFailureState;
  oilP:       TritonFailureState;
  suctionT:   TritonFailureState;   // doubles as superheat-high accumulator
  superheatLow: TritonFailureState; // separate accumulator for SH-low
  dischargeT: TritonFailureState;
  llsT:       TritonFailureState;
  ambientT:   TritonFailureState;
}

/** Stable bit positions for every Triton alarm code.  These map directly
 *  to the bits packed into Modbus regs 530-535 (active) and 536-541
 *  (acked) so Nova firmware can decode them without any string parsing.
 *
 *  IMPORTANT: this table is APPEND-ONLY.  Never re-number a code; only
 *  add new ones at the next free bit.  Nova's `hal_orbit.h` mirrors the
 *  same numbers as `ORBIT_TRITON_ALARM_*` constants.
 */
export const TRITON_ALARM_BITS: { [code: string]: number } = {
  // 0..5 — instantaneous sensor / detector alarms (legacy set)
  SUC_LOW_P:           0,
  SUC_HIGH_P:          1,
  DIS_HIGH_P:          2,
  DIS_HIGH_T:          3,
  OIL_LOW_P:           4,
  LEAK_SUSP:           5,
  // 6..12 — safety-input interlocks
  SAF_PHASE:           6,
  SAF_HP:              7,
  SAF_LP:              8,
  SAF_COMP_OL:         9,
  SAF_COND_OL:        10,
  SAF_PERMIT:         11,
  SAF_PROVE:          12,
  // 13..19 — GRC FAILURES (timer-driven, mode-respecting)
  FAIL_SUPERHEAT:     13,
  FAIL_SUPERHEATLOW:  14,
  FAIL_DISCHARGE:     15,
  FAIL_SUCTION:       16,
  FAIL_OIL:           17,
  FAIL_OUTSIDE_AIR:   18,
  FAIL_POWER:         19,
  // 20..26 — sensor-validity (transducer disconnected / out-of-range)
  SENS_FAULT_SUC_P:   20,
  SENS_FAULT_DIS_P:   21,
  SENS_FAULT_OIL_P:   22,
  SENS_FAULT_SUC_T:   23,
  SENS_FAULT_DIS_T:   24,
  SENS_FAULT_LLS_T:   25,
  SENS_FAULT_AMB_T:   26,
  // 27..31 — board / runtime / commissioning faults
  BOARD_MISSING_PRESS: 27,
  BOARD_MISSING_TEMP:  28,
  NO_DISCHARGE:        29,
  BAD_OIL_SENSOR:      30,
  COMM_ERR:            31,
};

/** Number of 16-bit Modbus registers per alarm bitmap (active or acked).
 *  6 regs × 16 bits = 96 codes of headroom; current taxonomy uses 32. */
export const TRITON_ALARM_REG_COUNT = 6;
/** First holding register of the active-alarm bitmap. */
export const TRITON_ALARM_REG_ACTIVE_BASE = 530;
/** First holding register of the acked-alarm bitmap. */
export const TRITON_ALARM_REG_ACKED_BASE  = 536;

/** IO-channel role config — replaces hard-wired channel meanings.
 *
 *  A Triton's analog outputs can each be assigned to one of:
 *    UNUSED | EEV | COMP_VFD | COND_VFD | EVAP_VFD
 *  Digital outputs can be assigned to:
 *    UNUSED | COMP | COND_FAN | EVAP_FAN | DEFROST | LIQ_SOL
 *    | UNLOADER1..4 | OIL_PUMP | COMP_ALARM
 *
 *  The condenser-fan stage count = (number of DOs role'd as COND_FAN).
 *
 *  Phase 8: Triton hardware ships with **2 AOs** (per spec —
 *  docs/triton-grc-port-plan.md §1).  The `aoMode` array is length 2.
 *  Snapshots from earlier (length-4) are migrated by truncating to 2.
 */
export interface TritonIoConfig {
  /** 2 analog outputs (matches OrbitState.analogOutputs length) */
  aoMode: number[];
  /** 10 digital outputs (matches OrbitState.digitalOutputs length) */
  doRole: number[];
}

/** Live state of a Triton refrigeration unit. */
export interface TritonState {
  /** True when the unit is enabled and allowed to run */
  enabled: boolean;
  /** Operator label for this unit */
  label: string;
  // ── Live equipment state ──
  /** Compressor running */
  compressorOn: boolean;
  /** Compressor lifetime runtime (seconds) */
  compressorRuntimeSec: number;
  /** Compressor draw (Amps), simulated */
  compressorAmps: number;
  /** Number of condenser fans currently energized (0..4) */
  condenserFanStage: number;
  /** Configured number of condenser fans (typ. 2-4) */
  condenserFanCount: number;
  /** Per-fan live state.  Length = `condenserFanCount`.  `runtimeSec` is the
   *  lifetime accumulator for the *lead* slot (not the physical fan) — when
   *  rotation fires we zero the lead-slot's runtime and pick the next fan
   *  in lead-order.  `on` reflects the current commanded relay state and
   *  is what gets driven onto the DO board.  Optional so pre-Phase-3.5
   *  persisted snapshots migrate cleanly. */
  condenserFans?: Array<{ on: boolean; runtimeSec: number }>;
  /** Index (into `condenserFans`) of the current lead fan.  Stage j means
   *  "fans at indices [leadIndex, leadIndex+1, …, leadIndex+j-1] (mod
   *  count) are commanded on".  Mirrors GRC `CondenserRotation.LeadFan`. */
  condenserLeadIndex?: number;
  /** When `condFanVfdMode==1`, this is the VFD speed reference (0..100 %)
   *  produced by `CondenserPID()`.  Slewed at PWM_INC=10 counts/cycle of
   *  a 222-count range every 0.4 s ≈ 11.25 %/s.  Always 0 in STAGED mode. */
  condenserVfdPct?: number;
  /** Live floating-head discharge-pressure target (PSI) — what the fan
   *  staging logic is actually chasing this tick.  Driven by
   *  `condenserMode` + ambient.  Exposed for SCADA. */
  dischargeTargetP: number;
  /** Evaporator fan running */
  evapFanOn: boolean;
  /** Electronic expansion-valve opening (0..100 %) */
  exvOpenPct: number;
  /** True during a defrost cycle */
  defrostActive: boolean;
  /** ms timestamp of last defrost end */
  lastDefrostEnd: number;
  /** Defrost stage machine (Phase 5).  Optional so pre-Phase-5 persisted
   *  snapshots migrate cleanly.
   *    0 = NONE     — no defrost activity (normal cooling / off)
   *    1 = PUMPDOWN — defrost requested, waiting for compressor pumpdown
   *                    (only entered when `pumpDownBeforeDefrost==1` and
   *                     compressor was running at request time)
   *    2 = ACTIVE   — `EQ_DEFROST` energized; compressor forced OFF;
   *                    HOT_GAS mode also forces all condenser fans ON
   *                    and bypasses the floating-head target.
   *    3 = DRIP     — defrost output dropped; compressor + fans stay off
   *                    for `dripTimeSec` so meltwater drains.
   *  `defrostActive` is true whenever `defrostStage >= 1`. */
  defrostStage?: number;
  /** Why this defrost was initiated (Phase 5).
   *    0 NONE, 1 TIMED (override), 2 MANUAL (REST), 3 TREND (future) */
  defrostType?: number;
  /** Seconds elapsed in the current `defrostStage` (resets on each
   *  stage transition).  Drives the termination timers + drip timer. */
  defrostStageSec?: number;
  /** Seconds since the last defrost end (TurnDefrostOff completion).
   *  Once this exceeds `defrostIntervalHours * 3600`, the override
   *  timer fires and a TIMED defrost initiates. */
  defrostSinceLastSec?: number;
  /** Set true by `POST /api/triton/defrost {action:'start'}` to request
   *  a manual defrost.  Cleared by the SM as soon as the request is
   *  consumed (PUMPDOWN or ACTIVE entered). */
  defrostManualPending?: boolean;
  /** Trend-based DEMAND defrost ring + trigger state (Phase 5.5).
   *  Mirrors GRC `Trend[].trend[128]` + `IT_DEFROST_INITIATE` timer.
   *  Optional so pre-Phase-5.5 snapshots migrate cleanly. */
  defrostTrend?: TritonDefrostTrend;
  /** Per-unloader latched on/off state (Phase capacity-stage).  Index 0
   *  = UNLOADER1, index 1 = UNLOADER2.  Optional so pre-capacity-stage
   *  snapshots migrate cleanly.  Length matches `setpoints.unloaderOnPct`. */
  unloaderOn?: boolean[];
  /** Per-unloader HP-override latch — true when discharge-P forced the
   *  unloader off; cleared once discharge drops back below `…HpLoadPsi`. */
  unloaderHpForced?: boolean[];
  /** Per-unloader LP-override latch — true when suction-P forced the
   *  unloader off; cleared once suction recovers above `…LpLoadPsi`. */
  unloaderLpForced?: boolean[];
  /** True whenever the oil-pump (DO role 10) should be energized.
   *  Default policy: pump runs whenever the compressor is on (mirrors
   *  GRC `EQ_OILPUMP` slaving).  Optional so pre-capacity-stage
   *  snapshots migrate cleanly. */
  oilPumpOn?: boolean;
  /** Manual override request from UI: 'auto' (default), 'force-on', 'force-off' */
  manualMode: 'auto' | 'force-on' | 'force-off';
  /**
   * External demand (0-100 %) — wired in real hardware as a 4-20 mA input
   * on channel 4 of the pressure sensor board.  In the simulator we just
   * keep a placeholder; the live value is expected to come straight from
   * the Nova control panel.
   */
  demand: number;
  // ── Live sensor readings ──
  sensors: {
    suctionP: TritonSensor;     // PSI
    suctionT: TritonSensor;     // °F
    dischargeP: TritonSensor;   // PSI
    dischargeT: TritonSensor;   // °F
    llsT: TritonSensor;         // °F (liquid-line solenoid / liquid-line sensor)
    oilP: TritonSensor;         // PSI
    ambientT: TritonSensor;     // °F
  };
  // ── Setpoints & alarms ──
  setpoints: TritonSetpoints;
  failures: TritonFailureModes;
  ioConfig: TritonIoConfig;
  /** Compressor state machine (mirrors GRC `COMPRESSOR_STATUS`):
   *  0 AUTO_STANDBY, 1 AUTO_RUN, 2 DEFROST, 3 DEFROST_OVERRIDE, 4 PROVE,
   *  5 SWITCH_PUMPDOWN, 6 REMOTE_PUMPDOWN, 7 SWITCH_OFF, 8 REMOTE_OFF,
   *  9 SYSTEM_OFF, 10 ERROR, 11 STARTING, 12 PUMPDOWN, 13 UNLOADING,
   *  14 DEFROST_MANUAL */
  compressorStatus: number;
  /** Total lifetime runtime (hours) — separate from compressorRuntimeSec which
   *  is the live tick counter; this is the persisted accumulator. */
  totalRuntimeHours: number;
  dailyRuntimeHours: number;
  alarms: TritonAlarm[];
  /** Hard-wired safety inputs / interlocks.  Each field shadows the matching
   *  digital-input port (DI 1-8 on the Triton orbit) and is updated every
   *  physics tick from `state.digitalInputs`.  See TRITON_DI_NAMES. */
  safeties: TritonSafeties;
  /** Live state of the leak / low-charge detector. */
  leakDetect: TritonLeakDetect;
  /** Live state of the GRC-faithful EXV PID (Phase 4).  Optional so
   *  pre-Phase-4 persisted snapshots migrate cleanly. */
  exvPid?: TritonExvPid;
  /** Live state of the GRC-faithful condenser-fan PID (deferral #3).
   *  Optional so pre-deferral-#3 persisted snapshots migrate cleanly.
   *  Only meaningful when `condFanVfdMode==1`. */
  condPid?: TritonCondPid;
  /** Per-channel timer state for the GRC failure machinery (FAIL_*).
   *  Optional so old persisted snapshots migrate cleanly. */
  failureStates?: TritonFailureStates;
  /** Bitmask of FAIL_* codes (TRITON_ALARM_BITS values) whose mode is
   *  SAFE_OFF and whose timer has expired.  Any non-zero value forces
   *  the compressor off until ack-all clears the latch. */
  safeOffMask?: number;
  /** ms timestamp of the last `updateTriton()` tick — used to detect
   *  power-fail / process-suspend gaps that exceed `powerFailMinutes`. */
  lastTickMs?: number;
  /** Crankcase-prove countdown (seconds).  Set to `crankcaseRunHours*3600`
   *  on cold boot, on power-fail-gap exceeding `powerFailMinutes`, or when
   *  DI_1 (crankcase current) drops out while the compressor is off.
   *  Decrements every tick; while non-zero the compressor cannot start
   *  (`COMP_PROVE`).  Mirrors GRC `IntervalTimer[IT_PROVE_n]`. */
  crankcaseProveSecRemaining: number;
  /** Pumpdown countdown (seconds).  When >0 the compressor stays running
   *  with the LLS commanded off; suction bleeds down.  Set when entering
   *  pumpdown (auto cut-out with valid suction OR DI_8 manual command);
   *  cleared when suction <= cutOutP again or the timer expires. */
  pumpdownSecRemaining: number;
  /** Seconds the phase monitor (DI_LT1) has been continuously open.
   *  Drives the GRC tiered power-fail response: ≥20 s forces SYSTEM_OFF,
   *  ≥3 min raises WARN_POWER, ≥`powerFailMinutes` latches FAIL_POWER and
   *  forces a fresh crankcase prove.  Reset to 0 the moment the phase
   *  monitor goes closed. */
  phaseLossSec: number;
  /** Phase 7 — diagnostic ring buffers.  Optional so old persisted
   *  snapshots migrate cleanly.  Logs live in RAM only; mirrors GRC
   *  behaviour where the SD card holds the last N records and the
   *  buffer resets on cold boot.  See `TritonLogs`. */
  logs?: TritonLogs;
}

/** Phase 7 — three-channel diagnostic log.  Each ring buffer is fixed-size
 *  to bound memory on the AM2432 target.  When full, oldest entries are
 *  evicted (FIFO).  All buffers reset on firmware boot — no persistence.
 *
 *  Sizes chosen to fit comfortably in R5F SRAM:
 *    pid : 256  × ~32 B = ~8 KB    — every PID compute (3-15 s cadence)
 *    user: 256  × ~96 B = ~24 KB   — 1-min snapshot of operator-visible state
 *    sys : 256  × ~64 B = ~16 KB   — event-driven (alarm/mode/defrost edges)
 */
export interface TritonLogs {
  pid:  TritonPidLogEntry[];
  user: TritonUserLogEntry[];
  sys:  TritonSysLogEntry[];
  /** ms timestamp of the last UserLog write — used to gate the 1-min cadence. */
  lastUserLogMs?: number;
  /** Snapshot of `manualMode` at the end of the previous tick.  REST mode
   *  changes happen between ticks, so a within-tick pre/post diff would
   *  never see them — we compare against this persisted snapshot instead. */
  lastManualMode?: 'auto' | 'force-on' | 'force-off';
}

/** PIDLog entry — mirrors GRC `PIDLOG_RECORD` (PIDLogs.h:36).  Type 0=EXV,
 *  1=CONDENSER (Triton has no head-pressure / refrig PIDs yet). */
export interface TritonPidLogEntry {
  ts: number;       // ms timestamp
  type: number;     // 0=EXV, 1=COND
  P: number;        // proportional contribution
  I: number;        // integral contribution
  D: number;        // derivative contribution
  output: number;   // post-clamp PID output (engineering units)
  error: number;    // CurError (setpoint - measurement)
}

/** UserLog entry — minute-cadence snapshot for the SCADA history charts.
 *  Mirrors GRC `USERLOG_RECORD` (UserLogs.h:31): main-page items + the
 *  raw analog board values.  We don't carry every sensor — just the ones
 *  the operator looks at on the Refrigeration Status page. */
export interface TritonUserLogEntry {
  ts: number;         // ms timestamp
  mode: number;       // compressorStatus (operator-visible state)
  suctionP: number;   // PSI
  suctionT: number;   // °F
  dischargeP: number; // PSI
  dischargeT: number; // °F
  oilP: number;       // PSI
  ambientT: number;   // °F
  superheat: number;  // °F (derived)
  exvPct: number;     // 0-100
  fanStage: number;   // 0..N
  vfdPct: number;     // 0-100
  demand: number;     // 0-100
}

/** SysLog (Activity log) entry — event-driven, written when the SM
 *  observes a state edge worth recording.  Mirrors GRC `SystemLog`
 *  activity-table semantics.  `kind` codes:
 *    0 ALARM_RAISED, 1 ALARM_CLEARED, 2 ALARM_ACKED,
 *    3 COMP_START,   4 COMP_STOP,     5 MANUAL_MODE,
 *    6 DEFROST_BEGIN,7 DEFROST_END,   8 BOOT
 */
export interface TritonSysLogEntry {
  ts: number;
  kind: number;
  /** Free-form code describing the event subject (e.g. alarm code,
   *  manual mode value, defrost stage).  Kept short for memory. */
  code: string;
  /** Optional human-readable label (omit on firmware to save flash). */
  label?: string;
}

/** Live state of the superheat-trend leak detector.  Updated every tick
 *  while the compressor is in a stable run; held otherwise. */
export interface TritonLeakDetect {
  /** Exponentially-weighted moving average of superheat (°F). */
  shAvgF: number;
  /** EWMA of the EXV open percent. */
  exvAvgPct: number;
  /** Seconds the alarm conditions have been continuously satisfied. */
  sustainedSec: number;
  /** True once `sustainedSec >= leakSustainMinutes*60`.  Cleared when the
   *  operator acknowledges the LEAK_SUSP alarm. */
  leakAlarmActive: boolean;
  /** Run-time (sec) the compressor needs before SH/EXV samples are trusted.
   *  Reset to 0 every time the compressor cycles off; only filtered values
   *  collected after this counter passes the warm-up gate are considered. */
  warmupSec: number;
}

/** Live state of the GRC-faithful EXV PID (`PIDCtrl.c PIDController`).
 *
 *  All values persist across snapshots so the integrator + slew limit
 *  survive simulator restarts the same way GRC keeps them in NVRAM-
 *  adjacent statics across power blips.  See section 4 of
 *  `tritonControl.ts` for the algorithm and `docs/triton-grc-port-plan.md`
 *  §5 for the GRC reference. */
export interface TritonExvPid {
  /** One-sided positive integrator (°F·s).  Clamped to [0, WindupLimit]
   *  where `WindupLimit = (exvMaxPct - exvMinPct) / (0.022 * exvKi)`. */
  intError: number;
  /** Last sample's superheat error (°F) — feeds the D term. */
  prevErr: number;
  /** ms timestamp of the last PID sample.  Used to gate computation to
   *  one update per `exvSamplePeriod` seconds. */
  lastPidMs: number;
  /** Seconds elapsed in the post-cut-in warm-up window.  While
   *  `warmupSec < exvWarmupMin*60` the PID is bypassed and the EXV is
   *  held at `exvStartPct`.  Reset on every compressor stop. */
  warmupSec: number;
  /** Latest PID-computed target opening (%) before slew limiting.  The
   *  live `exvOpenPct` slews toward this between PID samples. */
  targetPct: number;
}

/** Live state of the GRC-faithful condenser-fan PID (deferral #3 — mirrors
 *  GRC `CondenserPID()` + `PIDController()` in CondenserFans.c / PIDCtrl.c).
 *
 *  Engages only when `condFanVfdMode==1`.  PID output is in PWM units
 *  [PWM_MIN..PWM_MAX] = [55..277] (range=222) just like GRC; the live
 *  `condenserVfdPct` is converted to % and clamped to
 *  [`condFanVfdMinPct`, `condFanVfdMaxPct`] before being driven onto the AO. */
export interface TritonCondPid {
  /** One-sided positive integrator (PSI·sample).  Clamped to
   *  [0, range / (0.022 * condI)] each cycle, matching GRC WindupLimit. */
  intError: number;
  /** Last sample's discharge-pressure error (PSI) — feeds the D term. */
  prevErr: number;
  /** ms timestamp of the last PID sample.  Gates computation to one update
   *  per `condU` seconds (mirrors GRC `PID_PARAMS.U` / `pid->Timer`). */
  lastPidMs: number;
  /** Latest PID-computed target VFD percent (post conversion + clamp).  The
   *  live `condenserVfdPct` slews toward this between PID samples. */
  targetPct: number;
}

/** Trend-based DEMAND defrost state (Phase 5.5).  Mirrors GRC
 *  `Trend[].trend[128]` ring + `IT_DEFROST_INITIATE` sustain timer.
 *  See `tritonControl.ts §0c-trend` and `docs/triton-grc-port-plan.md §6`. */
export interface TritonDefrostTrend {
  /** Ring buffer of suction-PSI samples (head writes newest).  Length is
   *  fixed at 128 to match GRC; older slots are NaN until filled. */
  ring: number[];
  /** Index of the next slot to write in `ring`. */
  head: number;
  /** Number of valid samples currently in the ring (saturates at 128). */
  count: number;
  /** Seconds since the last sample was added — accumulator that triggers
   *  a new sample once it crosses 60. */
  sampleAccumSec: number;
  /** Compressor-on seconds elapsed since the trend was last reset (used
   *  as the warm-up gate against `defrostTrendVarTimerMin*60`). */
  warmupSec: number;
  /** Last computed rolling mean (PSI), or NaN when warmup not yet
   *  satisfied.  Surfaced in `apiGetTriton().defrost.trendPsi`. */
  trendPsi: number;
  /** Seconds the suction has held `≤ trendPsi - defrostTrendDiffP`
   *  AND coils-freezing predicate.  Once it crosses
   *  `defrostTrendInitiateMin*60`, a TREND defrost initiates. */
  initiateSec: number;
}

/** Digital-input port names for orbits in TRITON role.  Index = DI number-1.
 *  Authoritative GRC main-board pinout (Demar Lott 2019 wiring drawing,
 *  see docs/triton-grc-port-plan.md §1).  The 10-channel Triton orbit
 *  maps DI_LT1 + DI_AF1 + DI_1..DI_8 onto its 10 inputs in order. */
export const TRITON_DI_NAMES: readonly string[] = [
  'Phase Monitor',          // idx 0 = DI_LT1 — closed = OK
  '(reserved)',             // idx 1 = DI_AF1 — was airflow, jumpered out in field
  'Crankcase Heater',       // idx 2 = DI_1   — current sense, drives prove timer
  'Cond Fan Overload',      // idx 3 = DI_2   — closed = OK (alarm only)
  'Compressor Overload',    // idx 4 = DI_3   — closed = OK (manual reset)
  'Oil Failure Switch',     // idx 5 = DI_4   — closed = OK (manual reset)
  'HP Switch',              // idx 6 = DI_5   — closed = OK (manual reset)
  'LP Switch',              // idx 7 = DI_6   — closed = OK (auto reset)
  'Auto-Run Permissive',    // idx 8 = DI_7   — closed = enabled
  'Pumpdown Switch',        // idx 9 = DI_8   — open = command pumpdown
];

/** Display names for `TritonIoConfig.aoMode` values (index = mode value). */
export const TRITON_AO_NAMES: readonly string[] = [
  '',          // 0 UNUSED
  'EEV',       // 1
  'Comp VFD',  // 2
  'Cond VFD',  // 3
  'Evap VFD',  // 4
];

/** Display names for `TritonIoConfig.doRole` values (index = role value).
 *  Phase 8: added `Comp Alarm` (11) so the per-circuit
 *  `EQ_COMPRESSOR_ERROR` GRC slot has a Triton equivalent.  Slots 6-9 are
 *  the four GRC unloaders (`EQ_UNLOADER1..4`); slot 10 is reserved for
 *  oil-pump applications. */
export const TRITON_DO_NAMES: readonly string[] = [
  '',                  // 0 UNUSED
  'Compressor',        // 1
  'Cond Fan',          // 2
  'Evap Fan',          // 3
  'Defrost',           // 4
  'Liquid Solenoid',   // 5
  'Unloader 1',        // 6
  'Unloader 2',        // 7
  'Unloader 3',        // 8
  'Unloader 4',        // 9
  'Oil Pump',          // 10
  'Comp Alarm',        // 11
];

/** Phase 8 — DO-role enum constants used by `applyTritonIoMapping()`.  Keep
 *  in lockstep with TRITON_DO_NAMES.  Names mirror the GRC `COMPRESSOR_EQ`
 *  slots from `SerialShift.h:54-72` minus the per-circuit suffixes
 *  (Triton is single-circuit per orbit). */
export const TRITON_DO_ROLE = {
  UNUSED:    0,
  COMP:      1,
  COND_FAN:  2,
  EVAP_FAN:  3,
  DEFROST:   4,
  LIQ_SOL:   5,
  UNLOADER1: 6,
  UNLOADER2: 7,
  UNLOADER3: 8,
  UNLOADER4: 9,
  OIL_PUMP:  10,
  COMP_ALARM: 11,
} as const;

/** Phase 8 — AO-mode enum constants used by `applyTritonIoMapping()`. */
export const TRITON_AO_MODE = {
  UNUSED:    0,
  EEV:       1,
  COMP_VFD:  2,
  COND_VFD:  3,
  EVAP_VFD:  4,
} as const;

/** Live state of each Triton safety interlock.  Mirrors the authoritative
 *  GRC main-board pinout (see docs/triton-grc-port-plan.md §1):
 *
 *    DI index | Field              | GRC terminal | Healthy
 *    ---------|--------------------|--------------|--------
 *    0        | phaseMonitor       | DI_LT1       | closed
 *    1        | (reserved)         | DI_AF1       | —      (was airflow)
 *    2        | crankcaseCurrent   | DI_1         | closed when comp OFF
 *    3        | condFanOverload    | DI_2         | closed
 *    4        | compOverload       | DI_3         | closed
 *    5        | oilFailSwitch      | DI_4         | closed
 *    6        | hpSwitch           | DI_5         | closed
 *    7        | lpSwitch           | DI_6         | closed
 *    8        | autoRunPermissive  | DI_7         | closed
 *    9        | pumpdownSwitch     | DI_8         | closed (open=pumpdown)
 *
 *  IMPORTANT: there is NO dedicated run-prove DI.  "Run-prove" in GRC is
 *  the inverted DI_1 reading: while the compressor is OFF, DI_1 must read
 *  TRUE (heater drawing current); if it falls open while OFF, the
 *  `crankcaseProveSecRemaining` window restarts. */
export interface TritonSafeties {
  /** Current input state — true = closed/OK, false = open/tripped */
  phaseMonitor: boolean;
  /** Crankcase-heater current sense (DI_1).  When the compressor is OFF,
   *  this MUST be true (heater drawing current) — otherwise the prove
   *  timer resets. */
  crankcaseCurrent: boolean;
  condFanOverload: boolean;
  compOverload: boolean;
  /** Discrete oil-failure switch (DI_4) — independent of analog oil-P. */
  oilFailSwitch: boolean;
  hpSwitch: boolean;
  lpSwitch: boolean;
  autoRunPermissive: boolean;
  pumpdownSwitch: boolean;   // open (false) = pumpdown commanded
  /** Latched lockout — must be ack'd before compressor can restart.
   *  Bit positions: 0=hpSwitch, 1=compOverload, 2=oilFailSwitch,
   *  3=crankcaseProveUnmet (timer not yet expired since last loss).
   *  Note: condFanOverload is alarm-only (auto-reset, GRC §3 hard-trip
   *  list excludes it for compressor lockout). */
  lockoutMask: number;
}

// ─── Orbit I/O State ───────────────────────────────────────────────────────

export interface OrbitState {
  id: number;
  ipAddress: string;
  /** Firmware role: STORAGE or GDC */
  role: OrbitRole;
  digitalInputs: boolean[];
  eStop: boolean;
  digitalOutputs: boolean[];
  dc24vOutputs: boolean[];
  analogOutputs: number[];
  aoModes: ('voltage' | 'current')[];
  vfdRegisters: Map<number, number>;
  vfdActivity: boolean;
  sensorRegisters: Map<number, number>;
  sensorActivity: boolean;
  firmwareVersion: string;
  cpuTemp: number;
  uptime: number;
  commLost: boolean;
  safeMode: boolean;
  /** Equipment labels from UI I/O config (set via POST /api/ioconfig) */
  outputLabels: string[];
  inputLabels: string[];
  /** Simulated analog sensor boards on this orbit's RS-485 bus */
  analogBoards: AnalogBoardSim[];
  // ─── GDC Mode ───────────────────────────────────────────────────────────
  /** GDC actuators (5 total, each with open/close outputs and inputs) */
  gdcActuators: GDCActuator[];
  /** GDC stage configuration - defines thresholds and which doors open */
  gdcStages: GDCStage[];
  /** Fresh air door percentage command (0-100, mapped to stages) */
  freshAirDoorPct: number;
  /** Current active stage count (how many stages are triggered) */
  activeStageCount: number;
  /** Default actuator travel time in seconds (used for uncalibrated doors) */
  actuatorTravelTime: number;
  // ─── GDC Calibration ────────────────────────────────────────────────────
  /** True if calibration is in progress */
  calibrating: boolean;
  /** Current calibration phase: 'idle' | 'opening' | 'closing' */
  calibrationPhase: 'idle' | 'opening' | 'closing';
  /** Per-door calibration start timestamps (ms) */
  calibrationStartTimes: number[];
  // ─── Triton Mode ────────────────────────────────────────────────────────
  /** Refrigeration-unit state when role===TRITON */
  triton: TritonState;
}

const CONFIG_KEY = 'orbit-state';

function createDefaultActuator(index: number, stageAssignment: number): GDCActuator {
  return {
    position: 0,
    target: 0,
    moving: false,
    openSwitch: false,
    closeSwitch: true,
    label: `Door ${index + 1}`,
    stageAssignment,
    openTravelTime: 0,
    closeTravelTime: 0,
    calibrated: false,
  };
}

/**
 * Create default GDC stages — each door on its own stage, sequential:
 *   Stage 1: Door 1 (control door — opens first)
 *   Stage 2: Door 2
 *   Stage 3: Door 3
 *   Stage 4: Door 4
 *   Stage 5: Door 5
 */
function createDefaultStages(): GDCStage[] {
  return [
    { stageNum: 1, label: 'Control Door' },
    { stageNum: 2, label: 'Volume Group 1' },
    { stageNum: 3, label: 'Volume Group 2' },
    { stageNum: 4, label: 'Volume Group 3' },
    { stageNum: 5, label: 'Volume Group 4' },
  ];
}

function makeSensor(value: number, low: number, high: number): TritonSensor {
  return { value, valid: true, lowAlarm: low, highAlarm: high };
}

// ─── GRC-derived refrigerant defaults ─────────────────────────────────────
//
// Lifted verbatim from the legacy GRC firmware
// (GRC/ARM_Refrigeration/Application/Refrigerant.c).  Each refrigerant has a
// 16-element pressure default vector (PSI) with the GRC index meanings:
//   0  COMPRESSOR_OFF             (cut-out / low-suction-off)
//   1  COMPRESSOR_ON              (cut-in)
//   2  DISCHARGE_ALARM            (high-discharge trip / unload limit)
//   3  HHP_UNLOAD_1               (high-head unload stage 1)
//   4  HHP_UNLOAD_2               (high-head unload stage 2)
//   5  HHP_UNLOAD_3               (high-head unload stage 3)
//   6  HHP_UNLOAD_4               (high-head unload stage 4)
//   7  LOW_SUCTION_ALARM          (low-suction cutout)
//   8  MINIMUM_SETPOINT           (min discharge / floating-head floor)
//   9  FIXED_CONTROL              (fixed-head control point)
//  10  FAN_DIFFERENTIAL           (per-stage cond-fan offset, PSI)
//  11  UNLOADER_LOW_OVERRIDE_1    (low-suction unloader 1 cutout)
//  12  UNLOADER_LOW_OVERRIDE_2
//  13  UNLOADER_LOW_OVERRIDE_3
//  14  UNLOADER_LOW_OVERRIDE_4
//  15  DEFROST_OFF                (defrost-termination discharge pressure)
//
// Indexed by the GRC refrigerant enum.  We map our Triton enum (R22=0,
// R134A=1, R404A=2, R407A=3, R407C=4, R410A=5, ...) onto these values via
// `tritonToGrcRefrigerant()` (re-exported from `tritonControl.ts`).
//
// PHASE 1 EXTRACT NOTE
//   The refrigerant tables, `psatF()`, `tritonToGrcRefrigerant()`,
//   `getGrcPressureDefaults()`, `applyRefrigerantDefaults()`, and the entire
//   `updateTriton()` body now live in `tritonControl.ts` as the future home
//   of the GRC-faithful state machines.  The names below are re-imports so
//   the rest of this file (state initialisation, refrigerant change handler)
//   keeps compiling unchanged.  See docs/triton-grc-port-plan.md.

import {
  tickTritonControl,
  applyRefrigerantDefaults,
  psatF,
  tsatF,
  type TritonControlCtx,
} from './tritonControl';

/**
 * Default Triton state — modeled on a small commercial refrigeration unit
 * (R-404A, single-circuit, 4 condenser fans, EXV).  The simulation loop
 * drives the live sensors based on whether the compressor is running.
 *
 * Numeric defaults are lifted from the legacy Gellert Refrigeration
 * Controller (GRC) factory-default settings — see
 * GRC/ARM_Refrigeration/Application/Settings.c `GetFactoryDefault()`.
 */
function createDefaultTriton(): TritonState {
  const triton: TritonState = {
    enabled: true,
    label: 'Triton Refrigeration Unit',
    compressorOn: false,
    compressorRuntimeSec: 0,
    compressorAmps: 0,
    condenserFanStage: 0,
    condenserFanCount: 4,
    condenserFans: [
      { on: false, runtimeSec: 0 },
      { on: false, runtimeSec: 0 },
      { on: false, runtimeSec: 0 },
      { on: false, runtimeSec: 0 },
    ],
    condenserLeadIndex: 0,
    condenserVfdPct: 0,
    dischargeTargetP: 241,         // R-404A FIXED_CONTROL default
    evapFanOn: true,
    exvOpenPct: 0,
    defrostActive: false,
    lastDefrostEnd: 0,
    defrostStage: 0,
    defrostType: 0,
    defrostStageSec: 0,
    defrostSinceLastSec: 0,
    defrostManualPending: false,
    defrostTrend: {
      ring: new Array(128).fill(NaN),
      head: 0,
      count: 0,
      sampleAccumSec: 0,
      warmupSec: 0,
      trendPsi: NaN,
      initiateSec: 0,
    },
    unloaderOn:        [false, false],
    unloaderHpForced:  [false, false],
    unloaderLpForced:  [false, false],
    oilPumpOn:         false,
    manualMode: 'auto',
    demand: 50,
    sensors: {
      // Default values approximate a unit at idle in 70°F ambient
      suctionP:   makeSensor( 70.0,   5.0, 120.0),
      suctionT:   makeSensor( 30.0, -40.0,  90.0),
      dischargeP: makeSensor(150.0,  10.0, 425.0),
      dischargeT: makeSensor( 95.0,  10.0, 250.0),
      llsT:       makeSensor( 80.0, -40.0, 200.0),
      oilP:       makeSensor( 55.0,   8.0, 100.0),
      ambientT:   makeSensor( 70.0, -40.0, 130.0),
    },
    setpoints: {
      // Capacity / start-stop  (GRC SetRefrigerationSettings + Compressor defaults)
      // cutInP / cutOutP / discHighUnloadP / sucLowUnloadP / fan staging are
      // overwritten below by applyRefrigerantDefaults() — these literal seeds
      // just satisfy the type before that call runs.
      cutInP:               55.0,   // GRC R-404A COMPRESSOR_ON
      cutOutP:              25.0,   // GRC R-404A COMPRESSOR_OFF
      minOffTime:           300,    // GRC ShortCycle (sec)
      minRuntime:           120,    // 2-min minimum runtime
      warmUpSec:             60,    // GRC Pulse.WarmUp
      proveSec:              60,
      powerFailMinutes:      30,    // GRC Compressor.PowerFailTimeout
      crankcaseRunHours:      8,    // GRC Compressor.CrankcaseRunTimer
      lowAmbientCutoutF:     40,    // GRC Compressor.LowAmbientTemperature
      pumpDownMode:           1,    // SWITCH (GRC ContinuousPumpdown=0 → switch-driven)
      variableStartPct:      50,    // GRC Compressor.VariableStart
      refrigerantType:        2,    // R-404A (Triton enum)
      groupId:                0,
      rotationOrder:          1,
      rotateHours:           24,    // GRC Compressor.RotateCompressor
      // Superheat (GRC Compressor.SuperHeat*)
      superheatTarget:      15.0,   // SuperHeatSetPoint
      superheatLowF:         5.0,   // SuperHeatLow
      superheatWindowHighF: 30.0,   // SuperHeatWindowHigh
      superheatWindowLowF:  10.0,   // SuperHeatWindowLow
      // Forced-unload (overwritten by applyRefrigerantDefaults)
      discHighUnloadP:      400,    // GRC R-404A DISCHARGE_ALARM
      sucLowUnloadP:         20,    // GRC R-404A LOW_SUCTION_ALARM
      // Cond fans
      condFanCount:           4,
      condFanVfdMode:         0,
      condFanVfdMinPct:      30,
      condFanVfdMaxPct:     100,
      condFanVfdSetpointP:  241,    // GRC R-404A FIXED_CONTROL
      // Per-stage absolute PSI (applyRefrigerantDefaults overwrites these).
      // GRC stages: on = setpoint + n*diff, off = setpoint + n*diff - 2*diff.
      fanStageOnP:  [251, 261, 271, 281, 291, 301],
      fanStageOffP: [231, 241, 251, 261, 271, 281],
      // Floating-head condenser control — mirrors GRC `TargetDischarge()`
      // with an added approach-temperature bias (10 °F default) so fans
      // can actually cycle off at low ambient.  applyRefrigerantDefaults
      // re-seeds min/max + diffs when the refrigerant changes.
      condenserMode:  1,            // FLOATING (GRC default)
      condApproachF: 10,            // efficient bias above OAT
      condMinHeadP: 123,            // GRC R-404A MinimumSetpoint
      condMaxHeadP: 359,            // P_sat(105 °F) for R-404A ≈ 359 PSI
      fanDiffOnP:  [10, 20, 30, 40, 50, 60],
      fanDiffOffP: [ 5,  5,  5,  5,  5,  5],
      // EXV / Expansion valve  (GRC Settings.Expansion.PID — gains are ×10)
      exvKp:    5.0,    // GRC PID.P
      exvKi:    1.5,    // GRC PID.I (15 ×0.1 — GRC stores as ×10)
      exvKd:    0.2,    // GRC PID.D
      exvMinPct: 5,
      exvMaxPct: 100,
      exvManualPct: 50,
      exvStartPct: 30,        // GRC Expansion.StartPercent
      exvWarmupMin: 1,        // GRC Expansion.Warmup (minutes)
      exvSamplePeriod: 3,     // GRC PID_PARAMS.U (seconds)
      subcoolingTarget: 8.0,
      // Defrost (GRC Settings.Defrost)
      defrostMode:            1,    // TIMED  (GRC TerminationType=TIME)
      defrostStages:          1,
      defrostIntervalHours:   6,    // GRC Defrost.OverrideTimer
      defrostMaxMinutes:     10,    // GRC Defrost.TerminationTime
      defrostTermT:          55.0,
      defrostTermType:        0,    // 0=TIME (default; matches GRC default)
      defrostTermP:         250,    // GRC R-404A DOF (defrost-off pressure)
      dripTimeSec:          120,
      pumpDownBeforeDefrost:  1,
      // Trend-based DEMAND defrost (Phase 5.5).  Disabled by default —
      // operator must set `defrostTrendVarTimerMin > 0` to enable.
      defrostTrendVarTimerMin: 0,    // GRC Defrost.VariableTimer
      defrostTrendInitiateMin: 5,    // GRC Defrost.InitiateTimer
      defrostTrendDiffP:       5,    // GRC Defrost.SuctionDifferential
      // Capacity stage / unloaders (GRC defaults from Settings.c §236).
      unloaderOnPct:        [30, 40],
      unloaderOffPct:       [20, 30],
      unloaderHpUnloadPsi:  [350, 350],
      unloaderHpLoadPsi:    [320, 320],
      unloaderLpUnloadPsi:  [5, 5],
      unloaderLpLoadPsi:    [15, 15],
      unloaderNormal:        0,
      // PID (capacity + cond-fan)  — GRC CondenserFan.PID was 5/15/2/3
      capP: 5.0, capI: 1.5, capD: 0.2, capU: 3.0,
      condP: 5.0, condI: 1.5, condD: 0.2, condU: 3.0,
      // Safe-mode policy defaults (deferral #4).
      safePolicyCondFans:   2,    // ALL_ON
      safePolicyCondVfdPct: 100,
      safePolicyExvPct:     0,    // close
      safePolicyOilPump:    0,    // OFF
      // Leak / low-charge detection (no GRC equivalent — Triton extension)
      leakDetectEnabled:    1,
      leakSuperheatMarginF: 8.0,
      leakExvOpenPct:       85,
      leakSustainMinutes:   15,
    },
    failures: {
      // GRC Settings.Failure[*] defaults from GetFactoryDefault().  Mode
      // mapping: GRC FM_ALARM → 0 (ALARM_ONLY), GRC FM_FAIL → 1 (SAFE_OFF).
      suctionP:   { mode: 1, delaySec: 60 },   // FAIL_SUCTION:    FM_FAIL,  Timer=1 min
      dischargeP: { mode: 0, delaySec:  0 },   // FAIL_DISCHARGE:  FM_ALARM, Timer=0
      oilP:       { mode: 0, delaySec: 60 },   // FAIL_OIL:        FM_ALARM, Timer=1 min
      suctionT:   { mode: 1, delaySec: 60 },   // FAIL_SUPERHEAT*: derived; SAFE_OFF safer
      dischargeT: { mode: 1, delaySec: 60 },
      llsT:       { mode: 0, delaySec: 60 },
      ambientT:   { mode: 0, delaySec: 600 },  // FAIL_OUTSIDE_AIR: FM_ALARM, Timer=10 min
    },
    ioConfig: {
      // Phase 8 — Triton orbit hardware spec: 2 AO, 10 DO
      // (docs/triton-grc-port-plan.md §1 — "AM2432 Triton orbit").
      // Default AO mapping: ch0=EEV, ch1=Cond VFD (recommended for an
      // air-cooled scroll/recip Triton with EEV + variable-speed fans).
      aoMode: [TRITON_AO_MODE.EEV, TRITON_AO_MODE.COND_VFD],
      // Default DO mapping (recommended single-circuit Triton, see
      // docs/triton-grc-port-plan.md §1 table):
      //   DO_1 Compressor, DO_2 Liquid Solenoid,
      //   DO_3..6 Condenser Fans 1-4,
      //   DO_7..8 Unloader 1-2,
      //   DO_9 Defrost, DO_10 Compressor Alarm.
      doRole: [
        TRITON_DO_ROLE.COMP, TRITON_DO_ROLE.LIQ_SOL,
        TRITON_DO_ROLE.COND_FAN, TRITON_DO_ROLE.COND_FAN,
        TRITON_DO_ROLE.COND_FAN, TRITON_DO_ROLE.COND_FAN,
        TRITON_DO_ROLE.UNLOADER1, TRITON_DO_ROLE.UNLOADER2,
        TRITON_DO_ROLE.DEFROST, TRITON_DO_ROLE.COMP_ALARM,
      ],
    },
    compressorStatus: 0,        // AUTO_STANDBY
    totalRuntimeHours: 0,
    dailyRuntimeHours: 0,
    alarms: [],
    safeties: {
      phaseMonitor: true,
      crankcaseCurrent: true,      // heater energized when comp off
      condFanOverload: true,
      compOverload: true,
      oilFailSwitch: true,
      hpSwitch: true,
      lpSwitch: true,
      autoRunPermissive: true,
      pumpdownSwitch: true,        // closed = no pumpdown command
      lockoutMask: 0,
    },
    crankcaseProveSecRemaining: 0, // 0 = proven, ready to start (warm boot)
    pumpdownSecRemaining: 0,
    phaseLossSec: 0,
    leakDetect: {
      shAvgF: 0,
      exvAvgPct: 0,
      sustainedSec: 0,
      leakAlarmActive: false,
      warmupSec: 0,
    },
    exvPid: {
      intError:  0,
      prevErr:   0,
      lastPidMs: 0,
      warmupSec: 0,
      targetPct: 0,
    },
    condPid: {
      intError:  0,
      prevErr:   0,
      lastPidMs: 0,
      targetPct: 0,
    },
    failureStates: {
      suctionP:     { overSec: 0, tripped: false },
      dischargeP:   { overSec: 0, tripped: false },
      oilP:         { overSec: 0, tripped: false },
      suctionT:     { overSec: 0, tripped: false },
      superheatLow: { overSec: 0, tripped: false },
      dischargeT:   { overSec: 0, tripped: false },
      llsT:         { overSec: 0, tripped: false },
      ambientT:     { overSec: 0, tripped: false },
    },
    safeOffMask: 0,
    lastTickMs: 0,
    logs: { pid: [], user: [], sys: [], lastUserLogMs: 0 },
  };
  // Re-seed pressure-derived setpoints from the GRC PressureDefaults table
  // so the literal seeds above are kept consistent with the central source.
  applyRefrigerantDefaults(triton.setpoints, triton.setpoints.refrigerantType);
  return triton;
}

/**
 * Default analog (sensor-bus) board layout per orbit role.
 *
 *   STORAGE → 2 boards: temperature (4ch) + humidity/CO2 (4ch)
 *   GDC     → no sensor boards (GDC orbits never have a sensor bus)
 *   TRITON  → 2 boards:
 *               • board 0 — temperature: suction T, discharge T, LLS T, ambient T
 *               • board 1 — pressure   : suction P, discharge P, oil P, demand
 *                 The 4th channel of the pressure board is wired to a 4-20 mA
 *                 demand input (in the field).  In the simulator we publish
 *                 the same scaled value as `triton.demand`; callers may
 *                 prefer to take demand straight from Nova instead.
 *   PULSAR  → empty for now (no sim model yet)
 *
 * sensorValues are the raw int representation the sensor bus emits — the
 * UI applies the per-type scaling (typically /10 for everything except CO2
 * and demand-mA which we keep as-is for clarity).
 */
function defaultAnalogBoardsForRole(role: OrbitRole): AnalogBoardSim[] {
  if (role === OrbitRole.GDC) return [];
  if (role === OrbitRole.TRITON) {
    return [
      {
        address: 0, type: SENSOR_TYPE_TEMP, present: true, label: 'Temperature',
        sensorTypes: [SENSOR_TYPE_TEMP, SENSOR_TYPE_TEMP, SENSOR_TYPE_TEMP, SENSOR_TYPE_TEMP],
        // 30.0, 95.0, 80.0, 70.0 °F (matches createDefaultTriton sensor seeds)
        sensorValues: [300, 950, 800, 700],
        sensorLabels: ['Suction T', 'Discharge T', 'LLS T', 'Ambient T'],
      },
      {
        address: 1, type: SENSOR_TYPE_PRESSURE, present: true, label: 'Pressure',
        sensorTypes: [SENSOR_TYPE_PRESSURE, SENSOR_TYPE_PRESSURE, SENSOR_TYPE_PRESSURE, SENSOR_TYPE_MA],
        // 70.0, 150.0, 55.0 PSI; ch4 = 50% demand (placeholder)
        sensorValues: [700, 1500, 550, 50],
        sensorLabels: ['Suction P', 'Discharge P', 'Oil P', 'Demand'],
      },
    ];
  }
  // STORAGE (and any unknown role) — original factory-default layout
  return [
    {
      address: 0, type: SENSOR_TYPE_TEMP, present: true, label: 'Temperature',
      sensorTypes: [SENSOR_TYPE_TEMP, SENSOR_TYPE_TEMP, SENSOR_TYPE_TEMP, SENSOR_TYPE_TEMP],
      sensorValues: [210, 215, 150, 190],
      sensorLabels: ['Plenum 1', 'Plenum 2', 'Outside', 'Return'],
    },
    {
      address: 1, type: SENSOR_TYPE_HUMID, present: true, label: 'Humidity',
      sensorTypes: [SENSOR_TYPE_HUMID, SENSOR_TYPE_HUMID, SENSOR_TYPE_HUMID, SENSOR_TYPE_CO2],
      sensorValues: [450, 550, 400, 800],
      sensorLabels: ['Outside RH', 'Plenum RH', 'Return RH', 'CO2'],
    },
    {
      address: 2, type: SENSOR_TYPE_TEMP, present: true, label: 'Pile Temp',
      sensorTypes: [SENSOR_TYPE_TEMP, SENSOR_TYPE_TEMP, SENSOR_TYPE_TEMP, SENSOR_TYPE_TEMP],
      sensorValues: [72, 74, 71, 73],
      sensorLabels: ['Bd 3 - S 1', 'Bd 3 - S 2', 'Bd 3 - S 3', 'Bd 3 - S 4'],
    },
  ];
}

function createDefaultState(id: number, role: OrbitRole = OrbitRole.STORAGE): OrbitState {
  // For TRITON, DI 1-8 are wired to safety contacts that are normally CLOSED
  // (true) when healthy.  All others default open (false).
  const di = new Array(10).fill(false);
  if (role === OrbitRole.TRITON) {
    for (let i = 0; i < 8; i++) di[i] = true;
  }
  return {
    id,
    ipAddress: `10.47.27.${id}`,
    role,
    digitalInputs: di,
    eStop: false,
    digitalOutputs: new Array(10).fill(false),
    dc24vOutputs: new Array(4).fill(false),
    analogOutputs: [0, 0],
    aoModes: ['voltage', 'voltage'],
    vfdRegisters: new Map(),
    vfdActivity: false,
    sensorRegisters: new Map(),
    sensorActivity: false,
    firmwareVersion: role === OrbitRole.GDC ? '1.0.0-GDC' : '1.0.0',
    cpuTemp: 42,
    uptime: 0,
    commLost: true,
    safeMode: false,
    // Default equipment labels matching ARM factory IO config (board 0 / MAIN orbit).
    // These can be overwritten via POST /api/ioconfig when the bridge syncs.
    outputLabels: role === OrbitRole.STORAGE ? [
      'Red Light',           // DO1  — EQ_REDLIGHT
      'Yellow Light',        // DO2  — EQ_YELLOWLIGHT
      'Fan',                 // DO3  — EQ_FAN
      'Climacell',           // DO4  — EQ_CLIMACELL
      'Humidifier 1 Head',   // DO5  — EQ_HUMID_HEAD1
      'Humidifier 1 Pump',   // DO6  — EQ_HUMID_PUMP1
      '',                    // DO7  — (unassigned)
      '',                    // DO8  — (unassigned)
      'Door Close',          // DO9  — EQ_PULSEDOOR_CLOSE
      'Door Open',           // DO10 — EQ_PULSEDOOR_OPEN
    ] : role === OrbitRole.TRITON ? [
      // Mirrors createDefaultTriton().ioConfig.doRole defaults.
      'Compressor',          // DO1 — COMP
      'Evap Fan',            // DO2 — EVAP_FAN
      'Cond Fan 1',          // DO3 — COND_FAN
      'Cond Fan 2',          // DO4 — COND_FAN
      'Cond Fan 3',          // DO5 — COND_FAN
      'Cond Fan 4',          // DO6 — COND_FAN
      'Defrost',             // DO7 — DEFROST
      'Liquid Solenoid',     // DO8 — LIQ_SOL
      '',                    // DO9 — UNUSED
      '',                    // DO10 — UNUSED
    ] : new Array(10).fill(''),
    inputLabels: role === OrbitRole.STORAGE ? [
      'Power',               // DI1  — EQ_POWER
      'Remote Standby',      // DI2  — EQ_REMOTE_STANDBY
      'Fan',                 // DI3  — EQ_FAN (proof)
      'Climacell',           // DI4  — EQ_CLIMACELL
      'Humidifier 1 Head',   // DI5  — EQ_HUMID_HEAD1
      '',                    // DI6  — (unassigned)
      '',                    // DI7  — (unassigned)
      '',                    // DI8  — (unassigned)
      'Air Flow',            // DI9  — EQ_AIR_FLOW
      'Low Temp',            // DI10 — EQ_LOW_TEMP
    ] : role === OrbitRole.TRITON ? TRITON_DI_NAMES.slice() : new Array(10).fill(''),
    analogBoards: defaultAnalogBoardsForRole(role),
    // GDC-specific state: each door on its own stage by default (sequential fill)
    gdcActuators: [
      createDefaultActuator(0, 1),  // Door 1 → Stage 1 (Control Door)
      createDefaultActuator(1, 2),  // Door 2 → Stage 2
      createDefaultActuator(2, 3),  // Door 3 → Stage 3
      createDefaultActuator(3, 4),  // Door 4 → Stage 4
      createDefaultActuator(4, 5),  // Door 5 → Stage 5
    ],
    gdcStages: createDefaultStages(),
    freshAirDoorPct: 0,
    activeStageCount: 0,
    actuatorTravelTime: 90, // 90 seconds full travel (default for 36" actuator)
    calibrating: false,
    calibrationPhase: 'idle',
    calibrationStartTimes: [0, 0, 0, 0, 0],
    triton: createDefaultTriton(),
  };
}

// ─── Orbit Simulator Class ─────────────────────────────────────────────────

export class OrbitSimulator extends EventEmitter {
  state: OrbitState;
  private modbusServer: net.Server | null = null;
  private apiServer: http.Server | null = null;
  private lastPollTime = 0;
  private uptimeInterval: ReturnType<typeof setInterval> | null = null;
  private watchdogInterval: ReturnType<typeof setInterval> | null = null;
  private gdcInterval: ReturnType<typeof setInterval> | null = null;
  private tritonInterval: ReturnType<typeof setInterval> | null = null;
  private panelHtml: string | null = null;
  private sensorBus: SensorBusClient | null = null;
  private vfdBus: VFDBusClient | null = null;
  private vfdFaultHandler: ((unitId: number, faultCode: number) => boolean) | null = null;
  private vfdSim: VFDSimulator | null = null;

  // ─── Fault Injection (Chaos Mode) ─────────────────────────────────────
  /** Probability 0.0-1.0 that a Modbus response is dropped entirely (timeout) */
  faultDropRate = 0;
  /** Probability 0.0-1.0 that a Modbus response has corrupted data (bad bytes) */
  faultCorruptRate = 0;
  /** Probability 0.0-1.0 that a Modbus response is a Modbus exception (illegal function) */
  faultExceptionRate = 0;
  /** Artificial latency added to every Modbus response (ms) */
  faultLatencyMs = 0;
  /** If true, fault injection is active */
  get chaosEnabled(): boolean {
    return this.faultDropRate > 0 || this.faultCorruptRate > 0
      || this.faultExceptionRate > 0 || this.faultLatencyMs > 0;
  }

  constructor(id: number = 2, role: OrbitRole = OrbitRole.STORAGE) {
    super();
    const saved = loadConfig<any>(`${CONFIG_KEY}-${id}`);
    if (saved) {
      this.state = {
        ...createDefaultState(id, saved.role ?? role),
        ...saved,
        vfdRegisters: new Map(Object.entries(saved.vfdRegisters ?? {}).map(([k, v]) => [Number(k), Number(v)])),
        sensorRegisters: new Map(Object.entries(saved.sensorRegisters ?? {}).map(([k, v]) => [Number(k), Number(v)])),
      };
    } else {
      this.state = createDefaultState(id, role);
    }
    // Migration: legacy saved state for TRITON orbits used the storage
    // (temp + humidity) sensor-board layout.  Reseed to temp + pressure if
    // we detect the old layout so the panel and sensor bus reflect the
    // actual Triton hardware (0-500 PSI transducers, demand mA).
    let migratedAnalogBoards = false;

    // Migration: STORAGE orbits saved before the pile-temp board was added
    // only have 2 analog boards.  Append the factory-default pile board if
    // none with address >= 2 exists yet.
    if ((this.state.role ?? role) === OrbitRole.STORAGE) {
      const boards = this.state.analogBoards ?? [];
      const hasPile = boards.some((b: AnalogBoardSim) => b.address >= 2);
      if (!hasPile) {
        const factory = defaultAnalogBoardsForRole(OrbitRole.STORAGE);
        const pile = factory.find(b => b.address === 2);
        if (pile) {
          this.state.analogBoards = [...boards, pile];
          migratedAnalogBoards = true;
          console.log(`[Orbit ${this.state.id}] Migrated STORAGE: added pile-temp board (addr=2)`);
        }
      }
    }

    if (this.state.role === OrbitRole.TRITON) {
      const board1 = this.state.analogBoards?.[1];
      if (!board1 || board1.type !== SENSOR_TYPE_PRESSURE) {
        this.state.analogBoards = defaultAnalogBoardsForRole(OrbitRole.TRITON);
        migratedAnalogBoards = true;
      }
      // Migration: backfill DI/DO labels for legacy Triton states that
      // were saved before the labels were defined per role.  Only overwrite
      // when the existing labels look unset (all empty strings).
      const inEmpty = !this.state.inputLabels?.some((s: string) => s && s.length > 0);
      const outEmpty = !this.state.outputLabels?.some((s: string) => s && s.length > 0);
      if (inEmpty || outEmpty) {
        const fresh = createDefaultState(this.state.id, OrbitRole.TRITON);
        if (inEmpty)  this.state.inputLabels  = fresh.inputLabels.slice();
        if (outEmpty) this.state.outputLabels = fresh.outputLabels.slice();
        migratedAnalogBoards = true;       // piggyback the persist
      }
      // Migration: ensure DI 1-8 default to closed (true) for legacy saved
      // state where they were all false (which would trip every safety).
      const allDiOpen = this.state.digitalInputs.slice(0, 8).every((b: boolean) => !b);
      if (allDiOpen) {
        for (let i = 0; i < 8; i++) this.state.digitalInputs[i] = true;
        migratedAnalogBoards = true;
      }
    }

    // Migration: older Triton snapshots were written before ioConfig/
    // failures/setpoints existed (or before fields were added to them).
    // Without this, any Modbus write to regs 450-519 throws inside
    // writeTritonRegister and the popup save buttons silently fail.  Deep-
    // merge the saved triton object onto the freshly defaulted one so
    // brand-new sub-fields appear without clobbering anything the user has
    // edited.
    let migratedTritonShape = false;
    if (this.state.role === OrbitRole.TRITON) {
      const defaults = createDefaultTriton();
      const t = this.state.triton ?? {} as TritonState;
      const need = (k: keyof TritonState) =>
        (t as any)[k] == null || typeof (t as any)[k] !== 'object';
      const before = JSON.stringify({
        ioCfg: !!t.ioConfig, fail: !!t.failures, sp: !!t.setpoints,
      });
      this.state.triton = {
        ...defaults,
        ...t,
        setpoints: { ...defaults.setpoints, ...(t.setpoints ?? {}) },
        sensors:   { ...defaults.sensors,   ...(t.sensors   ?? {}) },
        failures:  need('failures')
          ? defaults.failures
          : { ...defaults.failures, ...t.failures },
        ioConfig:  need('ioConfig')
          ? defaults.ioConfig
          : {
              // Phase 8: hardware spec is 2 AOs.  Old length-4 snapshots
              // are truncated to the first 2 channels (which on a real
              // Triton orbit are the only physical AOs anyway).
              aoMode: Array.isArray(t.ioConfig?.aoMode) && t.ioConfig.aoMode.length >= 2
                ? t.ioConfig.aoMode.slice(0, 2)
                : defaults.ioConfig.aoMode,
              doRole: Array.isArray(t.ioConfig?.doRole) && t.ioConfig.doRole.length === 10
                ? t.ioConfig.doRole : defaults.ioConfig.doRole,
            },
        // Phase 6: drop saved alarms — they're transient runtime state on
        // GRC and re-evaluate from sensor reads on boot.  This avoids
        // ghost-latched FAIL_* alarms surviving across firmware restarts.
        alarms: [],
        safeties: need('safeties')
          ? defaults.safeties
          : { ...defaults.safeties, ...t.safeties },
        leakDetect: (t as any).leakDetect && typeof (t as any).leakDetect === 'object'
          ? { ...defaults.leakDetect, ...(t as any).leakDetect }
          : defaults.leakDetect,
        exvPid: (t as any).exvPid && typeof (t as any).exvPid === 'object'
          ? { ...defaults.exvPid!, ...(t as any).exvPid }
          : defaults.exvPid,
        condPid: (t as any).condPid && typeof (t as any).condPid === 'object'
          ? { ...defaults.condPid!, ...(t as any).condPid }
          : defaults.condPid,
        condenserFans: Array.isArray((t as any).condenserFans)
          ? (t as any).condenserFans
          : defaults.condenserFans,
        condenserLeadIndex: typeof (t as any).condenserLeadIndex === 'number'
          ? (t as any).condenserLeadIndex
          : 0,
        condenserVfdPct: typeof (t as any).condenserVfdPct === 'number'
          ? (t as any).condenserVfdPct
          : 0,
        defrostStage:        typeof (t as any).defrostStage        === 'number' ? (t as any).defrostStage        : 0,
        defrostType:         typeof (t as any).defrostType         === 'number' ? (t as any).defrostType         : 0,
        defrostStageSec:     typeof (t as any).defrostStageSec     === 'number' ? (t as any).defrostStageSec     : 0,
        defrostSinceLastSec: typeof (t as any).defrostSinceLastSec === 'number' ? (t as any).defrostSinceLastSec : 0,
        defrostManualPending: !!(t as any).defrostManualPending,
        defrostTrend: (t as any).defrostTrend && Array.isArray((t as any).defrostTrend.ring)
          ? {
              ring:           ((t as any).defrostTrend.ring as number[]).slice(0, 128),
              head:           typeof (t as any).defrostTrend.head           === 'number' ? (t as any).defrostTrend.head           : 0,
              count:          typeof (t as any).defrostTrend.count          === 'number' ? (t as any).defrostTrend.count          : 0,
              sampleAccumSec: typeof (t as any).defrostTrend.sampleAccumSec === 'number' ? (t as any).defrostTrend.sampleAccumSec : 0,
              warmupSec:      typeof (t as any).defrostTrend.warmupSec      === 'number' ? (t as any).defrostTrend.warmupSec      : 0,
              trendPsi:       typeof (t as any).defrostTrend.trendPsi       === 'number' ? (t as any).defrostTrend.trendPsi       : NaN,
              initiateSec:    typeof (t as any).defrostTrend.initiateSec    === 'number' ? (t as any).defrostTrend.initiateSec    : 0,
            }
          : defaults.defrostTrend,
        unloaderOn:       Array.isArray((t as any).unloaderOn)       ? (t as any).unloaderOn.map((v: any) => !!v).slice(0, 2) : defaults.unloaderOn,
        unloaderHpForced: Array.isArray((t as any).unloaderHpForced) ? (t as any).unloaderHpForced.map((v: any) => !!v).slice(0, 2) : defaults.unloaderHpForced,
        unloaderLpForced: Array.isArray((t as any).unloaderLpForced) ? (t as any).unloaderLpForced.map((v: any) => !!v).slice(0, 2) : defaults.unloaderLpForced,
        oilPumpOn:        typeof (t as any).oilPumpOn === 'boolean' ? (t as any).oilPumpOn : false,
        // Phase 6: failure latches are TRANSIENT runtime state — they
        // must NOT survive a firmware restart (GRC `IntervalTimer[]`
        // resets to 0 on boot, alarm-and-error tables aren't persisted).
        // Always start fresh from `defaults.failureStates`.  Acked
        // alarms in `t.alarms` are also dropped by the
        // `a.active && !a.acked` filter that runs against the inbound
        // snapshot below; live `active` flags get re-evaluated on the
        // first tick.
        failureStates: defaults.failureStates,
        safeOffMask: 0,
        lastTickMs:  0,
        // Phase 7 — log buffers are transient runtime state.  GRC's SD
        // card persists across boots, but we treat the AM2432 RAM ring
        // buffers as boot-fresh so a firmware restart clears the log
        // (matches how the cold-boot path zeroes them above).
        logs: { pid: [], user: [], sys: [], lastUserLogMs: 0 },
      };
      const after = JSON.stringify({
        ioCfg: !!this.state.triton.ioConfig,
        fail:  !!this.state.triton.failures,
        sp:    !!this.state.triton.setpoints,
        saf:   !!this.state.triton.safeties,
      });
      if (before !== after) migratedTritonShape = true;
    }
    // Always reset transient runtime state — these should not survive restarts.
    // commLost starts false so the watchdog can detect the false→true edge on
    // the first tick (lastPollTime=0 means elapsed is huge → commLost=true).
    this.state.commLost = false;
    this.state.safeMode = false;

    // Pre-load the web panel HTML
    try {
      const panelPath = path.resolve(__dirname, '..', 'panel', 'index.html');
      this.panelHtml = fs.readFileSync(panelPath, 'utf-8');
    } catch {
      console.warn('[Orbit] Web panel HTML not found — /panel endpoint disabled');
    }

    // Persist any one-shot migration we did above.
    if (migratedAnalogBoards) {
      console.log(`[Orbit ${this.state.id}] Migrated TRITON sensor boards: humid → pressure`);
      this.save();
    }
    if (migratedTritonShape) {
      console.log(`[Orbit ${this.state.id}] Backfilled TRITON ioConfig/failures defaults`);
      this.save();
    }

    // Always re-derive Triton DI/DO labels from the live ioConfig so the
    // orbit-sim panel and the bridge see the same mapping as the firmware.
    this.refreshTritonIoLabels();
  }

  /** Register a handler for VFD fault injection (called from HTTP API) */
  setVFDFaultHandler(handler: (unitId: number, faultCode: number) => boolean): void {
    this.vfdFaultHandler = handler;
  }

  /** Attach the VFD simulator for drive management API */
  setVFDSimulator(sim: VFDSimulator): void {
    this.vfdSim = sim;
  }

  start(modbusPort: number, apiPort: number): void {
    this.startModbusTcp(modbusPort);
    this.startApiServer(apiPort);

    this.uptimeInterval = setInterval(() => { this.state.uptime++; }, 1000);

    this.watchdogInterval = setInterval(() => {
      const elapsed = Date.now() - this.lastPollTime;
      const wasLost = this.state.commLost;
      this.state.commLost = elapsed > 5000;

      if (this.state.commLost && !wasLost) {
        console.log(`[Orbit ${this.state.id}] Communication lost — entering safe mode`);
        this.state.safeMode = true;
        this.enforceSafeMode();
        this.emit('comm-lost');
      } else if (this.state.commLost && wasLost) {
        // Still in safe mode — keep enforcing safe outputs every tick
        this.enforceSafeMode();
      } else if (!this.state.commLost && wasLost) {
        console.log(`[Orbit ${this.state.id}] Communication restored`);
        this.state.safeMode = false;
        this.emit('comm-restored');
      }
    }, 1000);

    console.log(`[Orbit ${this.state.id}] Simulator started (${OrbitRole[this.state.role]})`);
    console.log(`  Modbus TCP : :${modbusPort}`);
    console.log(`  REST API   : http://localhost:${apiPort}/api/status`);
    console.log(`  Web Panel  : http://localhost:${apiPort}/`);
    console.log(`  IP Address : ${this.state.ipAddress}`);

    // ── GDC Actuator Simulation ──
    if (this.state.role === OrbitRole.GDC) {
      this.gdcInterval = setInterval(() => this.updateGDCActuators(), 100);
      console.log(`  GDC Mode   : 5 actuators, ${this.state.actuatorTravelTime}s travel time`);
    }

    // ── Triton Refrigeration Simulation ──
    if (this.state.role === OrbitRole.TRITON) {
      this.tritonInterval = setInterval(() => this.updateTriton(), 500);
      console.log(`  Triton Mode: refrigeration unit, ${this.state.triton.condenserFanCount} cond fans`);
    }

    // ── Sensor Bus: connect to sensor-board-sim if configured ──
    const sensorRtuHost = process.env.SENSOR_RTU_HOST ?? 'localhost';
    const sensorRtuPort = parseInt(process.env.SENSOR_RTU_PORT ?? '5510', 10);
    if (sensorRtuPort > 0) {
      const boardAddresses = this.state.analogBoards.map(b => b.address);
      this.sensorBus = new SensorBusClient({
        host: sensorRtuHost,
        port: sensorRtuPort,
        boardAddresses,
        pollInterval: 1000,
        timeout: 2000,
      });

      this.sensorBus.on('poll-complete', () => {
        this.updateBoardsFromSensorBus();
      });

      this.sensorBus.on('error', (err: Error) => {
        // Silently ignore connection errors — they'll retry
      });

      this.sensorBus.start().catch(() => {
        console.log(`[Orbit ${this.state.id}] Sensor bus not available — using static board data`);
      });
      console.log(`  Sensor RTU : ${sensorRtuHost}:${sensorRtuPort}`);
    }

    // ── VFD Bus: connect to VFD drives via RTU on STORAGE-role orbits ──
    //
    // STORAGE orbits front the supply-fan VFD bus (RS-485 Port B in
    // production: orbit ↔ ABB ACS310/ACS380 drives). We default the RTU
    // port to 5520 (the VFDSimulator's RTU server) so a typical dev
    // setup auto-wires without env-var ceremony. Other roles (GDC,
    // TRITON, PULSAR) don't host VFDs in this hardware generation.
    //
    // Set VFD_RTU_PORT=0 explicitly to disable.
    const vfdRtuHost = process.env.VFD_RTU_HOST ?? 'localhost';
    const vfdRtuPort = parseInt(
      process.env.VFD_RTU_PORT ?? (this.state.role === OrbitRole.STORAGE ? '5520' : '0'),
      10,
    );
    if (vfdRtuPort > 0 && this.state.role === OrbitRole.STORAGE) {
      const driveIds = (process.env.VFD_DRIVE_IDS ?? '1,2,3')
        .split(',').map(s => parseInt(s.trim(), 10)).filter(n => n > 0);

      this.vfdBus = new VFDBusClient({
        host: vfdRtuHost,
        port: vfdRtuPort,
        driveUnitIds: driveIds,
        pollInterval: 500,
        timeout: 2000,
      });

      this.vfdBus.on('poll-complete', () => {
        this.updateVfdFromBus();
      });

      this.vfdBus.on('error', () => {});

      this.vfdBus.start().catch(() => {
        console.log(`[Orbit ${this.state.id}] VFD bus not available — VFD registers will be static`);
      });
      console.log(`  VFD RTU    : ${vfdRtuHost}:${vfdRtuPort} drives=[${driveIds}]`);
    }
  }

  /** Update analogBoards from live sensor bus data. */
  private updateBoardsFromSensorBus(): void {
    if (!this.sensorBus) return;

    for (const data of this.sensorBus.getBoards()) {
      let board = this.state.analogBoards.find(b => b.address === data.address);
      if (!board) {
        // New board discovered — add it
        board = {
          address: data.address,
          type: data.boardType,
          present: data.present,
          label: `Board ${data.address}`,
          sensorTypes: [...data.sensorTypes],
          sensorValues: [...data.engineeringValues],
          sensorLabels: ['', '', '', ''],
        };
        this.state.analogBoards.push(board);
      } else {
        board.type = data.boardType;
        board.present = data.present;
        board.sensorTypes = [...data.sensorTypes];
        board.sensorValues = [...data.engineeringValues];
      }
    }

    this.state.sensorActivity = true;
  }

  /** Update vfdRegisters from live VFD bus poll data. */
  private updateVfdFromBus(): void {
    if (!this.vfdBus) return;

    const regMap = this.vfdBus.getRegisterMap();
    for (const [addr, value] of regMap) {
      this.state.vfdRegisters.set(addr, value);
    }

    this.state.vfdActivity = true;
  }

  save(): void {
    const serializable = {
      ...this.state,
      vfdRegisters: Object.fromEntries(this.state.vfdRegisters),
      sensorRegisters: Object.fromEntries(this.state.sensorRegisters),
    };
    saveConfig(`${CONFIG_KEY}-${this.state.id}`, serializable);
  }

  /** Coalesce many quick writes (e.g. an FC10 burst that touches dozens of
   *  setpoint registers) into a single disk write ~500 ms after the last
   *  one.  Use this from Modbus write handlers so user-edited setpoints,
   *  IO config, and failure modes survive a sim restart. */
  private saveDebounceTimer: ReturnType<typeof setTimeout> | null = null;
  private scheduleSave(delayMs = 500): void {
    if (this.saveDebounceTimer) clearTimeout(this.saveDebounceTimer);
    this.saveDebounceTimer = setTimeout(() => {
      this.saveDebounceTimer = null;
      this.save();
    }, delayMs);
  }

  /** Refresh DI/DO/AO labels for a Triton orbit from `triton.ioConfig`.
   *  Called whenever an ioConfig register is written so the orbit-sim panel
   *  (which reads these labels via /api/status) shows the live mapping. */
  private refreshTritonIoLabels(): void {
    if (this.state.role !== OrbitRole.TRITON) return;
    const t = this.state.triton;
    // Inputs are wired to safety contacts and never change with ioConfig.
    this.state.inputLabels = TRITON_DI_NAMES.slice();
    // Outputs: name = TRITON_DO_NAMES[role], with a per-instance suffix when
    // there are multiple of the same role (e.g. "Cond Fan 1", "Cond Fan 2").
    const roleCounts: Record<number, number> = {};
    for (const r of t.ioConfig.doRole) roleCounts[r] = (roleCounts[r] ?? 0) + 1;
    const seen: Record<number, number> = {};
    this.state.outputLabels = t.ioConfig.doRole.map((r) => {
      const base = TRITON_DO_NAMES[r] ?? '';
      if (!base) return '';
      if ((roleCounts[r] ?? 0) <= 1) return base;
      seen[r] = (seen[r] ?? 0) + 1;
      return `${base} ${seen[r]}`;
    });
  }

  stop(): void {
    this.save();
    if (this.uptimeInterval) clearInterval(this.uptimeInterval);
    if (this.watchdogInterval) clearInterval(this.watchdogInterval);
    if (this.gdcInterval) clearInterval(this.gdcInterval);
    if (this.tritonInterval) clearInterval(this.tritonInterval);
    this.sensorBus?.stop();
    this.vfdBus?.stop();
    this.modbusServer?.close();
    this.apiServer?.close();
    console.log(`[Orbit ${this.state.id}] Simulator stopped`);
  }

  // ─── Communication-Loss Safe Mode ──────────────────────────────────────

  /**
   * Enforce role-specific safe outputs when communication is lost.
   * Called from the watchdog timer and after every Modbus response.
   *
   * Storage orbit:
   *   - Humidifier OFF (DO4 head, DO5 pump)
   *   - Climacell OFF (DO3)
   *   - Door outputs OFF (DO8 close, DO9 open) — holds current position
   *   - Fan stays running (DO2 left alone) — ventilation safety
   *   - Yellow light ON (DO1) — visual indicator of safe mode
   *
   * GDC orbit:
   *   - All doors close (freshAirDoorPct → 0)
   *   - The GDC actuator loop drives close outputs from the 0% target
   */
  private enforceSafeMode(): void {
    if (!this.state.safeMode) return;

    if (this.state.role === OrbitRole.STORAGE) {
      // Kill humidifier
      this.state.digitalOutputs[4] = false;   // Humidifier 1 Head
      this.state.digitalOutputs[5] = false;   // Humidifier 1 Pump
      // Kill climacell
      this.state.digitalOutputs[3] = false;   // Climacell
      // Stop door movement — hold current position (worm gear holds)
      this.state.digitalOutputs[8] = false;   // Door Close
      this.state.digitalOutputs[9] = false;   // Door Open
      // DC24V and analog outputs off
      this.state.dc24vOutputs.fill(false);
      this.state.analogOutputs = [0, 0];
      // Yellow light on — visual safe-mode indicator
      this.state.digitalOutputs[1] = true;
      // Fan (DO2) intentionally left alone — keeps running for ventilation
    } else if (this.state.role === OrbitRole.GDC) {
      // Close all doors: set target to 0%, actuator loop handles movement
      this.state.freshAirDoorPct = 0;
    }
  }

  // ─── GDC Actuator Simulation ───────────────────────────────────────────

  /**
   * Get the ordered list of door indices by stage priority.
   * Doors in the same stage open simultaneously; lower stage numbers first.
   * Returns array of { doorIndex, stageNum } sorted by stage.
   */
  private getDoorOrder(): { doorIndex: number; stageNum: number }[] {
    return this.state.gdcActuators
      .map((act, i) => ({ doorIndex: i, stageNum: act.stageAssignment }))
      .filter(d => d.stageNum > 0)
      .sort((a, b) => a.stageNum - b.stageNum);
  }

  /**
   * Calculate per-door target positions from freshAirDoorPct.
   *
   * Proportional fill model:
   *   Total system capacity = sum of all assigned door travel times.
   *   freshAirDoorPct maps to a fraction of that total.
   *   Doors fill in stage order — stage 1 doors proportionally first,
   *   then stage 2, etc. Doors in the same stage fill simultaneously.
   *
   * Example (5 doors × 90s = 450s total):
   *   10% → 45s budget → Door 1 at 50% (45/90), rest at 0%
   *   20% → 90s budget → Door 1 at 100%, rest at 0%
   *   30% → 135s budget → Door 1 at 100%, Door 2 at 50%, rest at 0%
   *   100% → 450s → all at 100%
   */
  private computeDoorTargets(): number[] {
    const targets = new Array(5).fill(0);
    const pct = this.state.freshAirDoorPct;
    if (pct <= 0) return targets;

    const doorOrder = this.getDoorOrder();
    if (doorOrder.length === 0) return targets;

    // Calculate total system capacity (seconds)
    let totalCapacity = 0;
    for (const d of doorOrder) {
      const act = this.state.gdcActuators[d.doorIndex];
      const travel = act.calibrated && act.openTravelTime > 0
        ? act.openTravelTime
        : this.state.actuatorTravelTime;
      totalCapacity += travel;
    }

    // Budget in seconds from cooling %
    let budgetSeconds = (pct / 100) * totalCapacity;

    // Group doors by stage (same stage = simultaneous)
    const stageGroups = new Map<number, { doorIndex: number; travelTime: number }[]>();
    for (const d of doorOrder) {
      const act = this.state.gdcActuators[d.doorIndex];
      const travel = act.calibrated && act.openTravelTime > 0
        ? act.openTravelTime
        : this.state.actuatorTravelTime;
      if (!stageGroups.has(d.stageNum)) stageGroups.set(d.stageNum, []);
      stageGroups.get(d.stageNum)!.push({ doorIndex: d.doorIndex, travelTime: travel });
    }

    // Sort stages by number
    const sortedStages = [...stageGroups.entries()].sort((a, b) => a[0] - b[0]);

    // Distribute budget across stages in order
    for (const [_stageNum, doors] of sortedStages) {
      if (budgetSeconds <= 0) break;

      // For simultaneous doors, the stage consumes the MAX travel time
      // (all doors in the stage move together, but each has its own travel time)
      const maxTravel = Math.max(...doors.map(d => d.travelTime));

      // How much of this stage's capacity can we use?
      const stageTimeUsed = Math.min(budgetSeconds, maxTravel);

      for (const d of doors) {
        // Each door's position = how much of its own travel time is covered
        targets[d.doorIndex] = Math.min(100, (stageTimeUsed / d.travelTime) * 100);
      }

      // Deduct the time consumed by this stage (the slowest door determines the stage time)
      budgetSeconds -= stageTimeUsed;
    }

    // Count active stages (any door moving or open)
    const activeStages = new Set<number>();
    for (const d of doorOrder) {
      if (targets[d.doorIndex] > 0) activeStages.add(d.stageNum);
    }
    this.state.activeStageCount = activeStages.size;

    return targets;
  }

  /**
   * Phase 8 — translate the pure-control TritonState into the orbit's
   * physical I/O surface (10 DO + 2 AO) using the operator-configured
   * `ioConfig.doRole[]` / `ioConfig.aoMode[]` mapping.
   *
   * This is the ONLY place logical control signals (compressorOn, EXV %,
   * fan stage, …) become physical Modbus-visible outputs.  On real
   * hardware the same translation lives in the AM2432 HAL layer.
   *
   * Called from `updateTriton()` every tick AFTER `tickTritonControl`
   * has computed the new logical state.  Safe-mode override happens
   * upstream in `enforceSafeMode()` (which fires after every Modbus
   * write); when commLost we just skip the mapping so the safed
   * outputs stay zeroed.
   *
   * Mapping rules:
   *   COMP        → t.compressorOn
   *   LIQ_SOL     → t.compressorOn && pumpdownSecRemaining===0
   *                 (LLS closes immediately on pumpdown command).
   *   COND_FAN    → t.condenserFans[k].on, where k = 0,1,2,…
   *                 in the order the role appears in doRole[].
   *                 Falls back to t.condenserFanStage > k for old
   *                 snapshots that don't carry per-fan state.
   *   EVAP_FAN    → t.evapFanOn
   *   DEFROST     → defrostStage===2 (ACTIVE).  DRIP keeps it off.
   *   UNLOADER1-4 → reserved for future capacity-stage logic;
   *                 currently always OFF.
   *   OIL_PUMP    → reserved; currently always OFF.
   *   COMP_ALARM  → any active alarm in t.alarms.
   *
   * AOs (0-100 % values):
   *   EEV       → t.exvOpenPct
   *   COMP_VFD  → 0 (no compressor-VFD logic yet — Triton runs on/off)
   *   COND_VFD  → t.condenserVfdPct (only meaningful when
   *               setpoints.condFanVfdMode===1, else stays 0)
   *   EVAP_VFD  → 100 when evapFanOn else 0
   */
  private applyTritonIoMapping(): void {
    if (this.state.role !== OrbitRole.TRITON) return;
    if (this.state.commLost || this.state.safeMode) return;
    const t = this.state.triton;
    const cfg = t.ioConfig;

    const lssOn = t.compressorOn && (t.pumpdownSecRemaining ?? 0) === 0;
    const defrostActive = (t.defrostStage ?? 0) === 2;
    const anyAlarm = t.alarms.some(a => a.active);

    // Walk DOs and assign according to role.  Track per-role index for
    // multi-instance roles (CondFan stages, Unloader index).
    let condFanIdx = 0;
    let unloaderIdx = 0;
    const unloaderInverted = (t.setpoints as any).unloaderNormal === 1;
    for (let i = 0; i < 10 && i < cfg.doRole.length; i++) {
      const role = cfg.doRole[i];
      let on = false;
      switch (role) {
        case TRITON_DO_ROLE.COMP:        on = t.compressorOn;            break;
        case TRITON_DO_ROLE.LIQ_SOL:     on = lssOn;                     break;
        case TRITON_DO_ROLE.COND_FAN: {
          const fan = t.condenserFans?.[condFanIdx];
          on = fan ? !!fan.on : (t.condenserFanStage > condFanIdx);
          condFanIdx++;
          break;
        }
        case TRITON_DO_ROLE.EVAP_FAN:    on = t.evapFanOn;               break;
        case TRITON_DO_ROLE.DEFROST:     on = defrostActive;             break;
        case TRITON_DO_ROLE.COMP_ALARM:  on = anyAlarm;                  break;
        // Unloaders 1..2: drive from latched `unloaderOn[]` (set by
        // tritonControl §7g).  `unloaderNormal === 1` (NORMALLY_CLOSED
        // contactor) inverts the physical output so a "loaded"
        // unloader corresponds to the DO being de-energized.  Slots 8
        // and 9 (UNLOADER3/4) currently fall through to OFF — Triton
        // doesn't ship with them, but the DO_ROLE enum keeps the slot
        // for parity with GRC.
        case TRITON_DO_ROLE.UNLOADER1:
        case TRITON_DO_ROLE.UNLOADER2: {
          const loaded = !!t.unloaderOn?.[unloaderIdx];
          on = unloaderInverted ? !loaded : loaded;
          unloaderIdx++;
          break;
        }
        case TRITON_DO_ROLE.UNLOADER3:
        case TRITON_DO_ROLE.UNLOADER4:   on = false;                     break;
        case TRITON_DO_ROLE.OIL_PUMP:    on = !!t.oilPumpOn;             break;
        case TRITON_DO_ROLE.UNUSED:
        default:                         on = false;                     break;
      }
      this.state.digitalOutputs[i] = on;
    }

    // Walk AOs.
    for (let i = 0; i < 2 && i < cfg.aoMode.length; i++) {
      const mode = cfg.aoMode[i];
      let pct = 0;
      switch (mode) {
        case TRITON_AO_MODE.EEV:       pct = t.exvOpenPct;            break;
        case TRITON_AO_MODE.COND_VFD:  pct = t.condenserVfdPct ?? 0;  break;
        case TRITON_AO_MODE.EVAP_VFD:  pct = t.evapFanOn ? 100 : 0;   break;
        case TRITON_AO_MODE.COMP_VFD:
        case TRITON_AO_MODE.UNUSED:
        default:                       pct = 0;                       break;
      }
      // Clamp 0-100 and store as percent.  The Modbus AO HRs read it
      // back unscaled; the orbit board's analog driver does the
      // PCT→counts conversion (4-20 mA at the terminal).
      this.state.analogOutputs[i] = Math.max(0, Math.min(100, pct));
    }
  }

  /**
   * Update GDC actuator simulation.
   * Called every 100ms in GDC mode.
   *
   * Proportional fill: freshAirDoorPct maps to total system pulse time,
   * distributed across doors in stage order. Each door gets a 0-100%
   * target position based on the budget it receives.
   * 
   * Output mapping (10 outputs, paired per actuator):
   *   DO1/DO2:  Actuator 1 Open / Close
   *   DO3/DO4:  Actuator 2 Open / Close
   *   DO5/DO6:  Actuator 3 Open / Close
   *   DO7/DO8:  Actuator 4 Open / Close
   *   DO9/DO10: Actuator 5 Open / Close
   * 
   * Input mapping (10 inputs, paired per actuator):
   *   DI1/DI2:  Actuator 1 Open Limit / Close Limit
   *   DI3/DI4:  Actuator 2 Open Limit / Close Limit
   *   DI5/DI6:  Actuator 3 Open Limit / Close Limit
   *   DI7/DI8:  Actuator 4 Open Limit / Close Limit
   *   DI9/DI10: Actuator 5 Open Limit / Close Limit
   */
  private updateGDCActuators(): void {
    const now = Date.now();
    
    // During calibration, handle timing measurements
    if (this.state.calibrating) {
      this.updateCalibration(now);
      return;
    }

    // Compute proportional targets from cooling output
    const doorTargets = this.computeDoorTargets();

    for (let i = 0; i < 5; i++) {
      const act = this.state.gdcActuators[i];
      act.target = doorTargets[i];

      // Calculate delta per tick using calibrated or default travel time
      let travelTime: number;
      if (act.position < act.target) {
        travelTime = act.calibrated && act.openTravelTime > 0 
          ? act.openTravelTime 
          : this.state.actuatorTravelTime;
      } else {
        travelTime = act.calibrated && act.closeTravelTime > 0 
          ? act.closeTravelTime 
          : this.state.actuatorTravelTime;
      }
      const deltaPerTick = 100 / (travelTime * 10);

      const doOpen = i * 2;
      const doClose = i * 2 + 1;
      const diOpenLimit = i * 2;
      const diCloseLimit = i * 2 + 1;

      // ── SAFETY INTERLOCKS ──
      // 1. NEVER allow open + close outputs on simultaneously
      // 2. If opening and open limit switch closes → stop immediately
      // 3. If closing and close limit switch closes → stop immediately

      // Update limit switches FIRST (before output decisions)
      act.openSwitch = act.position >= 99;
      act.closeSwitch = act.position <= 1;
      this.state.digitalInputs[diOpenLimit] = act.openSwitch;
      this.state.digitalInputs[diCloseLimit] = act.closeSwitch;

      if (act.position < act.target - 0.5 && !act.openSwitch) {
        // Opening — but only if open limit is NOT active
        act.moving = true;
        act.position = Math.min(act.position + deltaPerTick, act.target);
        this.state.digitalOutputs[doOpen] = true;
        this.state.digitalOutputs[doClose] = false;  // NEVER both on
      } else if (act.position > act.target + 0.5 && !act.closeSwitch) {
        // Closing — but only if close limit is NOT active
        act.moving = true;
        act.position = Math.max(act.position - deltaPerTick, act.target);
        this.state.digitalOutputs[doOpen] = false;  // NEVER both on
        this.state.digitalOutputs[doClose] = true;
      } else {
        // At target, at limit, or within deadband — all outputs OFF
        if (act.openSwitch) act.position = 100;
        if (act.closeSwitch) act.position = 0;
        act.moving = false;
        this.state.digitalOutputs[doOpen] = false;
        this.state.digitalOutputs[doClose] = false;
      }

      // Final safety assertion: if somehow both are on, force both off
      if (this.state.digitalOutputs[doOpen] && this.state.digitalOutputs[doClose]) {
        this.state.digitalOutputs[doOpen] = false;
        this.state.digitalOutputs[doClose] = false;
        act.moving = false;
        console.error(`[Orbit ${this.state.id}] SAFETY: Door ${i+1} open+close both on — forced OFF`);
      }
    }
  }

  /**
   * Update calibration - called each tick during calibration.
   * Phase 1 (opening): First close all doors, then open all and time until open limit.
   * Phase 2 (closing): Close all doors and time until close limit.
   */
  private updateCalibration(now: number): void {
    const deltaPerTick = 100 / (this.state.actuatorTravelTime * 10);

    for (let i = 0; i < 5; i++) {
      const act = this.state.gdcActuators[i];
      const startTime = this.state.calibrationStartTimes[i];

      const doOpen = i * 2;
      const doClose = i * 2 + 1;

      if (this.state.calibrationPhase === 'opening') {
        // Opening phase: move door toward 100%
        if (!act.openSwitch) {
          act.moving = true;
          act.position = Math.min(act.position + deltaPerTick, 100);
          this.state.digitalOutputs[doOpen] = true;
          this.state.digitalOutputs[doClose] = false;  // NEVER both on

          // Check if just hit open limit
          if (act.position >= 99) {
            act.openSwitch = true;
            act.closeSwitch = false;
            act.moving = false;
            this.state.digitalOutputs[doOpen] = false;  // Limit hit → stop
            // Calculate and store open travel time
            if (startTime > 0) {
              act.openTravelTime = (now - startTime) / 1000;
              console.log(`[Orbit ${this.state.id}] Door ${i + 1} open travel: ${act.openTravelTime.toFixed(1)}s`);
            }
          }
        } else {
          // Already at open limit, stop
          act.moving = false;
          this.state.digitalOutputs[doOpen] = false;
          this.state.digitalOutputs[doClose] = false;
        }
      } else if (this.state.calibrationPhase === 'closing') {
        // Closing phase: move door toward 0%
        if (!act.closeSwitch) {
          act.moving = true;
          act.position = Math.max(act.position - deltaPerTick, 0);
          this.state.digitalOutputs[doOpen] = false;  // NEVER both on
          this.state.digitalOutputs[doClose] = true;

          // Check if just hit close limit
          if (act.position <= 1) {
            act.closeSwitch = true;
            act.openSwitch = false;
            act.moving = false;
            this.state.digitalOutputs[doClose] = false;  // Limit hit → stop
            // Calculate and store close travel time
            if (startTime > 0) {
              act.closeTravelTime = (now - startTime) / 1000;
              console.log(`[Orbit ${this.state.id}] Door ${i + 1} close travel: ${act.closeTravelTime.toFixed(1)}s`);
            }
          }
        } else {
          // Already at close limit, stop
          act.moving = false;
          this.state.digitalOutputs[doOpen] = false;
          this.state.digitalOutputs[doClose] = false;
        }
      }

      // Safety assertion: open + close must never be on together
      if (this.state.digitalOutputs[doOpen] && this.state.digitalOutputs[doClose]) {
        this.state.digitalOutputs[doOpen] = false;
        this.state.digitalOutputs[doClose] = false;
        act.moving = false;
        console.error(`[Orbit ${this.state.id}] SAFETY: Door ${i+1} open+close both on during cal — forced OFF`);
      }

      // Reflect to digital inputs (paired: DI1/DI2 = actuator 1, etc.)
      this.state.digitalInputs[i * 2] = act.openSwitch;
      this.state.digitalInputs[i * 2 + 1] = act.closeSwitch;
    }

    // Check if phase is complete (all doors hit their limits)
    const allAtLimit = this.state.calibrationPhase === 'opening'
      ? this.state.gdcActuators.every(a => a.openSwitch)
      : this.state.gdcActuators.every(a => a.closeSwitch);

    if (allAtLimit) {
      if (this.state.calibrationPhase === 'opening') {
        // Transition to closing phase
        this.state.calibrationPhase = 'closing';
        const now = Date.now();
        this.state.calibrationStartTimes = this.state.gdcActuators.map(() => now);
        console.log(`[Orbit ${this.state.id}] Calibration: all doors open, starting close phase`);
      } else {
        // Calibration complete
        this.state.calibrating = false;
        this.state.calibrationPhase = 'idle';
        this.state.calibrationStartTimes = [0, 0, 0, 0, 0];
        for (const act of this.state.gdcActuators) {
          act.calibrated = true;
        }
        console.log(`[Orbit ${this.state.id}] Calibration complete!`);
        this.emit('calibration-complete', {
          actuators: this.state.gdcActuators.map((a, i) => ({
            door: i + 1,
            openTime: a.openTravelTime,
            closeTime: a.closeTravelTime,
          })),
        });
      }
    }
  }

  /**
   * Start GDC actuator calibration.
   * All doors will open (timing how long each takes to hit open limit),
   * then close (timing how long each takes to hit close limit).
   * Results are stored per-actuator and used for accurate position timing.
   */
  startCalibration(): { ok: boolean; error?: string } {
    if (this.state.role !== OrbitRole.GDC) {
      return { ok: false, error: 'Orbit is not in GDC mode' };
    }
    if (this.state.calibrating) {
      return { ok: false, error: 'Calibration already in progress' };
    }

    console.log(`[Orbit ${this.state.id}] Starting GDC calibration...`);
    
    // First ensure all doors are fully closed
    const allClosed = this.state.gdcActuators.every(a => a.closeSwitch);
    
    if (!allClosed) {
      // Need to close all doors first before starting calibration
      this.state.calibrating = true;
      this.state.calibrationPhase = 'closing';
      const now = Date.now();
      // Set all targets to close and start timing
      for (let i = 0; i < 5; i++) {
        this.state.gdcActuators[i].target = 0;
        this.state.calibrationStartTimes[i] = this.state.gdcActuators[i].closeSwitch ? 0 : now;
      }
      console.log(`[Orbit ${this.state.id}] Closing all doors first...`);
    } else {
      // All closed, start opening phase
      this.state.calibrating = true;
      this.state.calibrationPhase = 'opening';
      const now = Date.now();
      this.state.calibrationStartTimes = this.state.gdcActuators.map(() => now);
    }

    return { ok: true };
  }

  /**
   * Get calibration status
   */
  getCalibrationStatus(): {
    calibrating: boolean;
    phase: string;
    progress: { door: number; position: number; openTime: number; closeTime: number; calibrated: boolean }[];
  } {
    return {
      calibrating: this.state.calibrating,
      phase: this.state.calibrationPhase,
      progress: this.state.gdcActuators.map((a, i) => ({
        door: i + 1,
        position: Math.round(a.position),
        openTime: a.openTravelTime,
        closeTime: a.closeTravelTime,
        calibrated: a.calibrated,
      })),
    };
  }

  /**
   * Set fresh air door percentage (called via REST API or Modbus).
   * @param pct 0-100 percentage
   */
  setFreshAirDoorPct(pct: number): void {
    this.state.freshAirDoorPct = Math.max(0, Math.min(100, pct));
  }

  // ─── Modbus TCP Server ─────────────────────────────────────────────────

  private startModbusTcp(port: number): void {
    this.modbusServer = net.createServer((socket) => {
      console.log(`[Modbus] Client connected from ${socket.remoteAddress}:${socket.remotePort}`);
      let buffer = Buffer.alloc(0);

      socket.on('data', (chunk) => {
        buffer = Buffer.concat([buffer, chunk]);
        while (buffer.length >= MBAP_HEADER_LEN) {
          // Validate protocol ID at bytes [2-3] must be 0x0000 for Modbus TCP.
          // If not, the stream is misaligned — skip one byte and retry.
          const protoId = buffer.readUInt16BE(2);
          if (protoId !== 0) {
            buffer = buffer.subarray(1);
            continue;
          }

          const pduLength = buffer.readUInt16BE(4);
          // Modbus TCP max PDU length is 253 + 1 (unit ID) = 254.
          // Reject obviously invalid lengths to prevent parser stall.
          if (pduLength < 2 || pduLength > 260) {
            buffer = buffer.subarray(1);
            continue;
          }

          const frameLen = 6 + pduLength;
          if (buffer.length < frameLen) break;

          const frame = buffer.subarray(0, frameLen);
          buffer = buffer.subarray(frameLen);
          this.handleModbusFrame(frame, socket);
        }
        // Prevent unbounded buffer growth from garbage data
        if (buffer.length > 2048) {
          buffer = buffer.subarray(buffer.length - 512);
        }
      });

      socket.on('error', () => {});
    });

    this.modbusServer.listen(port, '0.0.0.0');
  }

  private handleModbusFrame(frame: Buffer, socket: net.Socket): void {
    this.lastPollTime = Date.now();
    // Don't set commLost here — let the watchdog interval manage comm state
    // transitions exclusively so the edge-triggered safeMode logic works.

    const functionCode = frame[7];
    let response: Buffer;

    // ── Fault injection: drop (simulate timeout) ──
    if (this.faultDropRate > 0 && Math.random() < this.faultDropRate) {
      this.emit('fault', { type: 'drop', functionCode });
      return; // No response — client sees a timeout
    }

    // ── Fault injection: forced exception ──
    if (this.faultExceptionRate > 0 && Math.random() < this.faultExceptionRate) {
      response = this.makeException(frame, EX_ILLEGAL_FUNCTION);
      this.emit('fault', { type: 'exception', functionCode });
      this.sendWithLatency(socket, response);
      return;
    }

    try {
      switch (functionCode) {
        case FC_READ_COILS:            response = this.handleReadCoils(frame); break;
        case FC_READ_DISCRETE_INPUTS:  response = this.handleReadDiscreteInputs(frame); break;
        case FC_READ_HOLDING_REGS:     response = this.handleReadHoldingRegs(frame); break;
        case FC_READ_INPUT_REGS:       response = this.handleReadInputRegs(frame); break;
        case FC_WRITE_SINGLE_COIL:     response = this.handleWriteSingleCoil(frame); break;
        case FC_WRITE_SINGLE_REG:      response = this.handleWriteSingleReg(frame); break;
        case FC_WRITE_MULTIPLE_COILS:  response = this.handleWriteMultipleCoils(frame); break;
        case FC_WRITE_MULTIPLE_REGS:   response = this.handleWriteMultipleRegs(frame); break;
        default: response = this.makeException(frame, EX_ILLEGAL_FUNCTION);
      }
    } catch {
      response = this.makeException(frame, EX_ILLEGAL_DATA_ADDRESS);
    }

    if (this.state.eStop) {
      this.state.digitalOutputs.fill(false);
      this.state.dc24vOutputs.fill(false);
      this.state.analogOutputs = [0, 0];
    }

    // Safe mode: enforce role-specific output restrictions after every write
    this.enforceSafeMode();

    // ── Fault injection: corrupt response data ──
    if (this.faultCorruptRate > 0 && Math.random() < this.faultCorruptRate) {
      // Flip random bytes in the PDU portion (after MBAP header)
      const corrupted = Buffer.from(response);
      const numFlips = 1 + Math.floor(Math.random() * 3);
      for (let i = 0; i < numFlips; i++) {
        const idx = MBAP_HEADER_LEN + Math.floor(Math.random() * (corrupted.length - MBAP_HEADER_LEN));
        if (idx < corrupted.length) {
          corrupted[idx] ^= (1 << Math.floor(Math.random() * 8));
        }
      }
      this.emit('fault', { type: 'corrupt', functionCode, flips: numFlips });
      this.sendWithLatency(socket, corrupted);
    } else {
      this.sendWithLatency(socket, response);
    }

    this.emit('poll', { functionCode });
  }

  /** Send response with optional artificial latency */
  private sendWithLatency(socket: net.Socket, data: Buffer): void {
    if (this.faultLatencyMs > 0) {
      setTimeout(() => { if (!socket.destroyed) socket.write(data); }, this.faultLatencyMs);
    } else {
      socket.write(data);
    }
  }

  // ─── FC01: Read Coils (Digital Outputs) ────────────────────────────────

  private handleReadCoils(frame: Buffer): Buffer {
    const startAddr = frame.readUInt16BE(8);
    const quantity = frame.readUInt16BE(10);
    const allOutputs = [...this.state.digitalOutputs, ...this.state.dc24vOutputs];
    const byteCount = Math.ceil(quantity / 8);
    const data = Buffer.alloc(byteCount);

    for (let i = 0; i < quantity; i++) {
      const addr = startAddr + i;
      if (addr < allOutputs.length && allOutputs[addr]) {
        data[Math.floor(i / 8)] |= (1 << (i % 8));
      }
    }

    return this.makeResponse(frame, Buffer.concat([Buffer.from([byteCount]), data]));
  }

  // ─── FC02: Read Discrete Inputs ────────────────────────────────────────

  private handleReadDiscreteInputs(frame: Buffer): Buffer {
    const startAddr = frame.readUInt16BE(8);
    const quantity = frame.readUInt16BE(10);
    const allInputs = [...this.state.digitalInputs, this.state.eStop];
    const byteCount = Math.ceil(quantity / 8);
    const data = Buffer.alloc(byteCount);

    for (let i = 0; i < quantity; i++) {
      const addr = startAddr + i;
      if (addr < allInputs.length && allInputs[addr]) {
        data[Math.floor(i / 8)] |= (1 << (i % 8));
      }
    }

    return this.makeResponse(frame, Buffer.concat([Buffer.from([byteCount]), data]));
  }

  // ─── FC03: Read Holding Registers ──────────────────────────────────────

  private handleReadHoldingRegs(frame: Buffer): Buffer {
    const startAddr = frame.readUInt16BE(8);
    const quantity = frame.readUInt16BE(10);
    const data = Buffer.alloc(quantity * 2);

    for (let i = 0; i < quantity; i++) {
      const addr = startAddr + i;
      const val = this.getHoldingRegister(addr);
      data.writeUInt16BE(val & 0xFFFF, i * 2);
    }

    return this.makeResponse(frame, Buffer.concat([Buffer.from([quantity * 2]), data]));
  }

  private getHoldingRegister(addr: number): number {
    if (addr <= 1) return this.state.analogOutputs[addr];
    if (addr <= 3) return this.state.aoModes[addr - 2] === 'current' ? 1 : 0;
    if (addr >= 100 && addr < 164) return this.state.vfdRegisters.get(addr) ?? 0;
    if (addr >= 200 && addr < 264) { this.state.sensorActivity = true; return this.getSensorRegister(addr); }
    // ── GDC Registers 300-319 ──
    if (addr >= 300 && addr < 320 && this.state.role === OrbitRole.GDC) return this.getGDCRegister(addr);
    // ── Triton Registers 320-339 (master/slave broadcast + control from Nova) ──
    if (addr >= 320 && addr < 542 && this.state.role === OrbitRole.TRITON) return this.getTritonRegister(addr);
    if (addr === 40000) return this.state.id;
    if (addr === 40001) return this.state.eStop ? 1 : 0;
    if (addr === 40002) return this.state.commLost ? 1 : 0;
    if (addr === 40003) return this.state.safeMode ? 1 : 0;
    if (addr === 40004) return Math.round(this.state.cpuTemp * 10);
    if (addr === 40005) return this.state.uptime & 0xFFFF;
    if (addr === 40006) return (this.state.uptime >> 16) & 0xFFFF;
    return 0;
  }

  /**
   * GDC Register Map (300-319):
   *   300   W   Door command: freshAirDoorPct × 10 (0-1000)
   *   301-305 R Actuator 1-5 position × 10 (0-1000)
   *   306   R   Active stage bitfield (bit 0=stage1, bit 1=stage2, ...)
   *   307   R/W Default travel time in seconds
   *   310-314 W Stage 1-5 door assignment bitfield (which doors are in this stage)
   *   315   W   Number of active stages (1-5)
   *   316   R   Total system capacity in seconds
   *   318   W   Calibrate command (1=start)
   *   319   R   Calibration status (0=idle, 1=opening, 2=closing)
   */
  private getGDCRegister(addr: number): number {
    switch (addr) {
      case 300: return Math.round(this.state.freshAirDoorPct * 10);
      case 301: case 302: case 303: case 304: case 305: {
        const act = this.state.gdcActuators[addr - 301];
        return act ? Math.round(act.position * 10) : 0;
      }
      case 306: {
        // Active stage bitfield — stages with any door target > 0
        let bitfield = 0;
        for (let i = 0; i < 5; i++) {
          const act = this.state.gdcActuators[i];
          if (act.target > 0 && act.stageAssignment > 0) {
            bitfield |= (1 << (act.stageAssignment - 1));
          }
        }
        return bitfield;
      }
      case 307: return this.state.actuatorTravelTime;
      case 310: case 311: case 312: case 313: case 314: {
        // Stage door bitfield (which doors assigned to this stage)
        const stageNum = addr - 310 + 1;
        let doorBits = 0;
        for (let i = 0; i < 5; i++) {
          if (this.state.gdcActuators[i]?.stageAssignment === stageNum) {
            doorBits |= (1 << i);
          }
        }
        return doorBits;
      }
      case 315: return this.state.gdcStages.length;
      case 316: {
        // Total system capacity in seconds
        let total = 0;
        for (const act of this.state.gdcActuators) {
          if (act.stageAssignment > 0) {
            total += act.calibrated && act.openTravelTime > 0
              ? act.openTravelTime
              : this.state.actuatorTravelTime;
          }
        }
        return Math.round(total);
      }
      case 318: return 0; // write-only command
      case 319: {
        if (!this.state.calibrating) return 0;
        if (this.state.calibrationPhase === 'opening') return 1;
        if (this.state.calibrationPhase === 'closing') return 2;
        return 0;
      }
      default: return 0;
    }
  }

  /**
   * Write a GDC register (300-319).
   * Called from FC06 (single write) and FC10 (multi write).
   */
  private writeGDCRegister(addr: number, value: number): void {
    switch (addr) {
      case 300:
        // Door command: value is freshAirDoorPct × 10 (0-1000)
        this.setFreshAirDoorPct(Math.min(Math.max(value / 10, 0), 100));
        break;
      case 307:
        // Default travel time (seconds)
        if (value >= 10 && value <= 600) {
          this.state.actuatorTravelTime = value;
        }
        break;
      case 310: case 311: case 312: case 313: case 314: {
        // Stage door assignment: value = door bitfield (bit 0 = door 1, etc.)
        const stageNum = addr - 310 + 1;
        const doorBits = value & 0x1F; // 5 bits for 5 doors
        // Ensure we have enough stages
        while (this.state.gdcStages.length < stageNum) {
          this.state.gdcStages.push({
            stageNum: this.state.gdcStages.length + 1,
            label: `Stage ${this.state.gdcStages.length + 1}`,
          });
        }
        // Update actuator stage assignments from the bitfield
        for (let i = 0; i < 5; i++) {
          if (doorBits & (1 << i)) {
            this.state.gdcActuators[i].stageAssignment = stageNum;
          } else if (this.state.gdcActuators[i].stageAssignment === stageNum) {
            this.state.gdcActuators[i].stageAssignment = 0;
          }
        }
        break;
      }
      case 315:
        // Number of active stages
        if (value >= 1 && value <= 5) {
          this.state.gdcStages = this.state.gdcStages.slice(0, value);
        }
        break;
      case 318:
        // Calibrate command
        if (value === 1 && !this.state.calibrating) {
          this.startCalibration();
        }
        break;
      default:
        break;
    }
  }

  /**
   * Triton Register Map (320-519) — Nova talks Modbus TCP to every Triton.
   *
   * Block layout (200 regs total):
   *   320-339  Live + basic control (telemetry + manual + main setpoints)
   *   340-359  Compressor advanced settings + lead/lag + lifetime counters
   *   360-389  Condenser fans (count, VFD config, 6-stage pressure tables)
   *   390-409  EXV / superheat
   *   410-429  Defrost
   *   430-449  PID gains (capacity + cond fan)
   *   450-479  IO config (analog out modes, digital out roles)
   *   480-519  Per-sensor failure modes
   *
   * All scaled values are stored as int×10 unless noted.  Signed values use
   * standard 2's complement on the 16-bit wire.  Sentinel values (≤ -32760
   * for signed broadcasts, < 0 for unsigned demand) mean "no broadcast".
   */
  private getTritonRegister(addr: number): number {
    const t = this.state.triton;
    const sp = t.setpoints;
    const u16 = (v: number) => v & 0xFFFF;
    const s16x10 = (v: number) => Math.round(v * 10) & 0xFFFF;

    // ── 320-339: live + basic control ──
    switch (addr) {
      case 320: return s16x10(t.sensors.ambientT.value);
      case 321: return s16x10(t.demand);
      case 322: return t.compressorOn ? 1 : 0;
      case 323: return t.condenserFanStage;
      case 324: return t.defrostActive ? 1 : 0;
      case 325: return t.manualMode === 'force-on' ? 1 : t.manualMode === 'force-off' ? 2 : 0;
      case 326: return s16x10(sp.cutInP);
      case 327: return s16x10(sp.cutOutP);
      case 328: return s16x10(sp.superheatTarget);
      case 329: return u16(sp.minOffTime);
      case 330: return u16(sp.minRuntime);
      case 331: return u16(sp.defrostIntervalHours);
      case 332: return u16(sp.defrostMaxMinutes);
      case 333: return s16x10(sp.defrostTermT);
      case 334: return 0; // write-only command
      case 335: return u16(t.compressorStatus);
      case 336: return s16x10(t.compressorAmps);
      case 337: return s16x10(t.exvOpenPct);
      case 338: return s16x10(t.sensors.suctionT.value); // (also exposed via analog board) — kept for direct telemetry
      case 339: return s16x10(sp.subcoolingTarget); // subcoolingTarget echo (full in 396)
    }

    // ── 340-359: compressor advanced ──
    switch (addr) {
      case 340: return u16(sp.warmUpSec);
      case 341: return u16(sp.proveSec);
      case 342: return u16(sp.powerFailMinutes);
      case 343: return u16(sp.crankcaseRunHours);
      case 344: return s16x10(sp.lowAmbientCutoutF);
      case 345: return u16(sp.pumpDownMode);
      case 346: return u16(sp.variableStartPct);
      case 347: return s16x10(sp.superheatLowF);
      case 348: return s16x10(sp.superheatWindowHighF);
      case 349: return s16x10(sp.superheatWindowLowF);
      case 350: return u16(sp.discHighUnloadP);
      case 351: return u16(sp.sucLowUnloadP);
      case 352: return u16(sp.rotateHours);
      case 353: return u16(sp.groupId);
      case 354: return u16(sp.rotationOrder);
      case 355: return u16(sp.rotationOrder); // lead echo (Nova writes; we just store)
      case 356: return u16(t.totalRuntimeHours);
      case 357: return u16(t.totalRuntimeHours); // alias for symmetry
      case 358: return u16(t.dailyRuntimeHours);
      case 359: return u16(sp.refrigerantType);
    }

    // ── 360-389: condenser fans ──
    if (addr === 360) return u16(sp.condFanCount);
    if (addr === 361) return u16(sp.condFanVfdMode);
    if (addr === 362) return u16(sp.condFanVfdMinPct);
    if (addr === 363) return u16(sp.condFanVfdMaxPct);
    if (addr === 364) return u16(sp.condFanVfdSetpointP);
    if (addr >= 365 && addr <= 370) return u16(sp.fanStageOnP[addr - 365] ?? 0);
    if (addr >= 371 && addr <= 376) return u16(sp.fanStageOffP[addr - 371] ?? 0);
    // ── Floating-head condenser control (377-389) ──
    if (addr === 377) return u16(sp.condenserMode);
    if (addr === 378) return s16x10(sp.condApproachF);
    if (addr === 379) return u16(sp.condMinHeadP);
    if (addr === 380) return u16(sp.condMaxHeadP);
    if (addr === 381) return s16x10(t.dischargeTargetP);   // live readback
    if (addr >= 382 && addr <= 387) return u16(sp.fanDiffOnP[addr  - 382] ?? 0);
    if (addr >= 397 && addr <= 402) return u16(sp.fanDiffOffP[addr - 397] ?? 0);

    // ── 390-409: EXV ──
    switch (addr) {
      case 390: return s16x10(sp.exvKp);
      case 391: return s16x10(sp.exvKi);
      case 392: return s16x10(sp.exvKd);
      case 393: return u16(sp.exvMinPct);
      case 394: return u16(sp.exvMaxPct);
      case 395: return u16(sp.exvManualPct);
      case 396: return s16x10(sp.subcoolingTarget);
    }

    // ── 410-429: defrost ──
    switch (addr) {
      case 410: return u16(sp.defrostMode);
      case 411: return u16(sp.defrostStages);
      case 412: return u16(sp.dripTimeSec);
      case 413: return u16(sp.pumpDownBeforeDefrost);
      case 414: return u16(sp.defrostTrendVarTimerMin);
      case 415: return u16(sp.defrostTrendInitiateMin);
      case 416: return s16x10(sp.defrostTrendDiffP);
    }

    // ── 430-449: PID ──
    switch (addr) {
      case 430: return s16x10(sp.capP);
      case 431: return s16x10(sp.capI);
      case 432: return s16x10(sp.capD);
      case 433: return s16x10(sp.capU);
      case 434: return s16x10(sp.condP);
      case 435: return s16x10(sp.condI);
      case 436: return s16x10(sp.condD);
      case 437: return s16x10(sp.condU);
      // ── 440-443: leak / low-charge detection ──
      case 440: return u16(sp.leakDetectEnabled);
      case 441: return s16x10(sp.leakSuperheatMarginF);
      case 442: return u16(sp.leakExvOpenPct);
      case 443: return u16(sp.leakSustainMinutes);
    }

    // ── 450-479: IO config ──
    if (addr >= 450 && addr <= 451) return u16(t.ioConfig.aoMode[addr - 450] ?? 0);
    if (addr >= 454 && addr <= 463) return u16(t.ioConfig.doRole[addr - 454] ?? 0);

    // ── 480-519: failure modes ──
    // Layout: 2 regs per channel × 7 channels = 14 regs (480-493).
    // Channel order: suctionP, dischargeP, oilP, suctionT, dischargeT, llsT, ambientT.
    if (addr >= 480 && addr <= 493) {
      const channels: TritonFailureMode[] = [
        t.failures.suctionP, t.failures.dischargeP, t.failures.oilP,
        t.failures.suctionT, t.failures.dischargeT, t.failures.llsT, t.failures.ambientT,
      ];
      const ch = channels[Math.floor((addr - 480) / 2)];
      const isMode = ((addr - 480) % 2) === 0;
      return ch ? u16(isMode ? ch.mode : ch.delaySec) : 0;
    }

    // ── 530-541: alarm bitmaps (Triton → Nova) ──
    //   530..535 — active-alarm bits, 16 codes per reg (96 codes of headroom)
    //   536..541 — acked-alarm bits, same packing
    // Stable bit positions are defined by TRITON_ALARM_BITS.  Nova decodes
    // these into its own warning enum without needing string parsing.
    if (addr >= TRITON_ALARM_REG_ACTIVE_BASE
        && addr <  TRITON_ALARM_REG_ACTIVE_BASE + TRITON_ALARM_REG_COUNT) {
      const wordIdx = addr - TRITON_ALARM_REG_ACTIVE_BASE;
      let word = 0;
      for (const a of t.alarms) {
        if (!a.active) continue;
        const bit = TRITON_ALARM_BITS[a.code];
        if (bit === undefined) continue;
        if ((bit >>> 4) === wordIdx) word |= (1 << (bit & 0x0F));
      }
      return u16(word);
    }
    if (addr >= TRITON_ALARM_REG_ACKED_BASE
        && addr <  TRITON_ALARM_REG_ACKED_BASE + TRITON_ALARM_REG_COUNT) {
      const wordIdx = addr - TRITON_ALARM_REG_ACKED_BASE;
      let word = 0;
      for (const a of t.alarms) {
        if (!a.acked) continue;
        const bit = TRITON_ALARM_BITS[a.code];
        if (bit === undefined) continue;
        if ((bit >>> 4) === wordIdx) word |= (1 << (bit & 0x0F));
      }
      return u16(word);
    }

    return 0;
  }

  /**
   * Write a Triton register.  Called from FC06 / FC10 by Nova.  Values are
   * clamped so a stuck/garbage write can't put the unit into a dangerous state.
   */
  private writeTritonRegister(addr: number, value: number): void {
    const t = this.state.triton;
    const sp = t.setpoints;
    const signed = value > 0x7FFF ? value - 0x10000 : value;
    const clamp = (v: number, lo: number, hi: number) => Math.max(lo, Math.min(hi, v));

    // ── 320-339: live + basic control ──
    switch (addr) {
      case 320: {
        if (signed <= -32760) return; // sentinel = no broadcast
        const degF = signed / 10;
        if (degF >= -100 && degF <= 200) {
          t.sensors.ambientT.value = degF;
          t.sensors.ambientT.valid = true;
        }
        return;
      }
      case 321: {
        if (signed < 0) return;
        t.demand = clamp(signed / 10, 0, 100);
        return;
      }
      case 325:
        t.manualMode = value === 1 ? 'force-on' : value === 2 ? 'force-off' : 'auto';
        return;
      case 326: sp.cutInP          = clamp(signed / 10,   0, 500); return;
      case 327: sp.cutOutP         = clamp(signed / 10,   0, 500); return;
      case 328: sp.superheatTarget = clamp(signed / 10,   0,  60); return;
      case 329: sp.minOffTime      = clamp(value,  0, 3600); return;
      case 330: sp.minRuntime      = clamp(value,  0, 3600); return;
      case 331: sp.defrostIntervalHours = clamp(value, 1, 48);  return;
      case 332: sp.defrostMaxMinutes    = clamp(value, 1, 120); return;
      case 333: sp.defrostTermT    = clamp(signed / 10, 0, 200); return;
      case 334:
        if (value === 1) {
          for (const a of t.alarms) a.acked = true;
          // Ack-all also clears the GRC failure latches and safe-off mask
          // so the compressor is allowed to restart once conditions have
          // returned to normal.  The next tick re-evaluates from scratch.
          if (t.failureStates) {
            for (const k of Object.keys(t.failureStates) as Array<keyof TritonFailureStates>) {
              t.failureStates[k].overSec = 0;
              t.failureStates[k].tripped = false;
              t.failureStates[k]._pumpdownArmed = false;
            }
          }
          t.safeOffMask = 0;
        }
        return;
    }

    // ── 340-359: compressor advanced ──
    switch (addr) {
      case 340: sp.warmUpSec        = clamp(value, 0, 3600); return;
      case 341: sp.proveSec         = clamp(value, 0, 3600); return;
      case 342: sp.powerFailMinutes = clamp(value, 0, 1440); return;
      case 343: sp.crankcaseRunHours = clamp(value, 0, 24);  return;
      case 344: sp.lowAmbientCutoutF = clamp(signed / 10, -100, 200); return;
      case 345: sp.pumpDownMode      = clamp(value, 0, 3);   return;
      case 346: sp.variableStartPct  = clamp(value, 0, 100); return;
      case 347: sp.superheatLowF        = clamp(signed / 10, -50, 100); return;
      case 348: sp.superheatWindowHighF = clamp(signed / 10, 0, 100);   return;
      case 349: sp.superheatWindowLowF  = clamp(signed / 10, 0, 100);   return;
      case 350: sp.discHighUnloadP  = clamp(value, 0, 600);  return;
      case 351: sp.sucLowUnloadP    = clamp(value, 0, 200);  return;
      case 352: sp.rotateHours      = clamp(value, 0, 8760); return;
      case 353: sp.groupId          = clamp(value, 0, 31);   return;
      case 354: sp.rotationOrder    = clamp(value, 1, 6);    return;
      case 359: applyRefrigerantDefaults(sp, clamp(value, 0, 20)); return;
    }

    // ── 360-389: condenser fans ──
    if (addr === 360) {
      sp.condFanCount = clamp(value, 1, 6);
      // Mirror to live equipment state so the simulation loop honors it.
      t.condenserFanCount = sp.condFanCount;
      return;
    }
    if (addr === 361) { sp.condFanVfdMode     = clamp(value, 0, 1);   return; }
    if (addr === 362) { sp.condFanVfdMinPct   = clamp(value, 0, 100); return; }
    if (addr === 363) { sp.condFanVfdMaxPct   = clamp(value, 0, 100); return; }
    if (addr === 364) { sp.condFanVfdSetpointP = clamp(value, 0, 600); return; }
    if (addr >= 365 && addr <= 370) { sp.fanStageOnP[addr - 365]  = clamp(value, 0, 600); return; }
    if (addr >= 371 && addr <= 376) { sp.fanStageOffP[addr - 371] = clamp(value, 0, 600); return; }
    if (addr === 377) { sp.condenserMode = clamp(value, 0, 2);     return; }
    if (addr === 378) { sp.condApproachF = clamp(signed / 10, 0, 50); return; }
    if (addr === 379) { sp.condMinHeadP  = clamp(value, 0, 600);   return; }
    if (addr === 380) { sp.condMaxHeadP  = clamp(value, 0, 800);   return; }
    if (addr >= 382 && addr <= 387) { sp.fanDiffOnP[addr  - 382] = clamp(value, 0, 200); return; }
    if (addr >= 397 && addr <= 402) { sp.fanDiffOffP[addr - 397] = clamp(value, 0, 200); return; }

    // ── 390-409: EXV ──
    switch (addr) {
      case 390: sp.exvKp = clamp(signed / 10, -100, 100); return;
      case 391: sp.exvKi = clamp(signed / 10, -100, 100); return;
      case 392: sp.exvKd = clamp(signed / 10, -100, 100); return;
      case 393: sp.exvMinPct = clamp(value, 0, 100); return;
      case 394: sp.exvMaxPct = clamp(value, 0, 100); return;
      case 395: sp.exvManualPct = clamp(value, 0, 100); return;
      case 396: sp.subcoolingTarget = clamp(signed / 10, 0, 60); return;
    }

    // ── 410-429: defrost ──
    switch (addr) {
      case 410: sp.defrostMode        = clamp(value, 0, 4);    return;
      case 411: sp.defrostStages      = clamp(value, 1, 2);    return;
      case 412: sp.dripTimeSec        = clamp(value, 0, 600);  return;
      case 413: sp.pumpDownBeforeDefrost = clamp(value, 0, 1); return;
      // Phase 5.5 trend defrost
      case 414: sp.defrostTrendVarTimerMin = clamp(value, 0, 1440); return;
      case 415: sp.defrostTrendInitiateMin = clamp(value, 0, 240);  return;
      case 416: sp.defrostTrendDiffP       = clamp(signed / 10, 0, 100); return;
    }

    // ── 430-449: PID ──
    switch (addr) {
      case 430: sp.capP = clamp(signed / 10, -100, 100); return;
      case 431: sp.capI = clamp(signed / 10, -100, 100); return;
      case 432: sp.capD = clamp(signed / 10, -100, 100); return;
      case 433: sp.capU = clamp(signed / 10, -100, 100); return;
      case 434: sp.condP = clamp(signed / 10, -100, 100); return;
      case 435: sp.condI = clamp(signed / 10, -100, 100); return;
      case 436: sp.condD = clamp(signed / 10, -100, 100); return;
      case 437: sp.condU = clamp(signed / 10, -100, 100); return;
      // ── 440-443: leak / low-charge detection ──
      case 440: sp.leakDetectEnabled    = clamp(value, 0, 1);             return;
      case 441: sp.leakSuperheatMarginF = clamp(signed / 10, 0, 100);     return;
      case 442: sp.leakExvOpenPct       = clamp(value, 0, 100);           return;
      case 443: sp.leakSustainMinutes   = clamp(value, 1, 1440);          return;
    }

    // ── 450-479: IO config ──
    // Phase 8: Triton hardware ships 2 AO channels (450..451) and 10 DO
    // channels (454..463).  doRole accepts 0..11 (COMP_ALARM was added).
    if (addr >= 450 && addr <= 451) {
      t.ioConfig.aoMode[addr - 450] = clamp(value, 0, 4);
      this.refreshTritonIoLabels();
      return;
    }
    if (addr >= 454 && addr <= 463) {
      t.ioConfig.doRole[addr - 454] = clamp(value, 0, 11);
      this.refreshTritonIoLabels();
      return;
    }

    // ── 480-519: failure modes ──
    if (addr >= 480 && addr <= 493) {
      const channels: TritonFailureMode[] = [
        t.failures.suctionP, t.failures.dischargeP, t.failures.oilP,
        t.failures.suctionT, t.failures.dischargeT, t.failures.llsT, t.failures.ambientT,
      ];
      const ch = channels[Math.floor((addr - 480) / 2)];
      if (!ch) return;
      const isMode = ((addr - 480) % 2) === 0;
      if (isMode) ch.mode = clamp(value, 0, 2);
      else ch.delaySec = clamp(value, 0, 3600);
      return;
    }
  }

  /** Compute sensor register from analogBoards state (regs 200-263).
   *  Layout: reg 200 = board_count, then 7 regs per board. */
  private getSensorRegister(addr: number): number {
    const offset = addr - 200;
    const boards = this.state.analogBoards;
    if (offset === 0) return boards.length;
    const boardIdx = Math.floor((offset - 1) / 7);
    const reg = (offset - 1) % 7;
    if (boardIdx >= boards.length) return 0;
    const b = boards[boardIdx];
    switch (reg) {
      case 0: return ((b.type & 0xFF) << 8) | (b.address & 0x3F) | (b.present ? 0x80 : 0);
      case 1: return ((b.sensorTypes[0] & 0xF) << 12) |
                     ((b.sensorTypes[1] & 0xF) << 8) |
                     ((b.sensorTypes[2] & 0xF) << 4) |
                      (b.sensorTypes[3] & 0xF);
      case 2: case 3: case 4: case 5:
        return (b.sensorValues[reg - 2] ?? SENSOR_VAL_UNDEF) & 0xFFFF;
      case 6: return 0x0203;
      default: return 0;
    }
  }

  // ─── FC04: Read Input Registers ────────────────────────────────────────

  private handleReadInputRegs(frame: Buffer): Buffer {
    const startAddr = frame.readUInt16BE(8);
    const quantity = frame.readUInt16BE(10);
    const data = Buffer.alloc(quantity * 2);

    for (let i = 0; i < quantity; i++) {
      const addr = startAddr + i;
      data.writeUInt16BE(this.getHoldingRegister(addr) & 0xFFFF, i * 2);
    }

    return this.makeResponse(frame, Buffer.concat([Buffer.from([quantity * 2]), data]));
  }

  // ─── FC05: Write Single Coil ──────────────────────────────────────────

  private handleWriteSingleCoil(frame: Buffer): Buffer {
    const addr = frame.readUInt16BE(8);
    const value = frame.readUInt16BE(10) === 0xFF00;

    if (addr < 10) {
      this.state.digitalOutputs[addr] = value;
    } else if (addr < 14) {
      this.state.dc24vOutputs[addr - 10] = value;
    } else {
      return this.makeException(frame, EX_ILLEGAL_DATA_ADDRESS);
    }

    this.emit('output-change', { type: 'coil', addr, value });
    return frame;
  }

  // ─── FC06: Write Single Register ──────────────────────────────────────

  private handleWriteSingleReg(frame: Buffer): Buffer {
    const addr = frame.readUInt16BE(8);
    const value = frame.readUInt16BE(10);

    if (addr <= 1) {
      this.state.analogOutputs[addr] = value;
    } else if (addr <= 3) {
      this.state.aoModes[addr - 2] = value === 1 ? 'current' : 'voltage';
    } else if (addr >= 100 && addr < 164) {
      this.state.vfdRegisters.set(addr, value);
      this.state.vfdActivity = true;
      // Forward write to VFD drive via RTU bus
      this.vfdBus?.writeFromOrbit(addr, value).catch(() => {});
    } else if (addr >= 200 && addr < 264) {
      this.state.sensorRegisters.set(addr, value);
      this.state.sensorActivity = true;
    } else if (addr >= 300 && addr < 320 && this.state.role === OrbitRole.GDC) {
      this.writeGDCRegister(addr, value);
    } else if (addr >= 320 && addr < 520 && this.state.role === OrbitRole.TRITON) {
      this.writeTritonRegister(addr, value);
      this.scheduleSave();
    } else {
      return this.makeException(frame, EX_ILLEGAL_DATA_ADDRESS);
    }

    this.emit('output-change', { type: 'register', addr, value });
    return frame;
  }

  // ─── FC0F: Write Multiple Coils ───────────────────────────────────────

  private handleWriteMultipleCoils(frame: Buffer): Buffer {
    const startAddr = frame.readUInt16BE(8);
    const quantity = frame.readUInt16BE(10);
    const dataBytes = frame.subarray(13);

    for (let i = 0; i < quantity; i++) {
      const addr = startAddr + i;
      const value = (dataBytes[Math.floor(i / 8)] & (1 << (i % 8))) !== 0;
      if (addr < 10) {
        this.state.digitalOutputs[addr] = value;
      } else if (addr < 14) {
        this.state.dc24vOutputs[addr - 10] = value;
      }
    }

    this.emit('output-change', { type: 'multi-coil', startAddr, quantity });
    const resp = Buffer.alloc(12);
    frame.copy(resp, 0, 0, 12);
    resp.writeUInt16BE(6, 4);
    return resp;
  }

  // ─── FC10: Write Multiple Registers ───────────────────────────────────

  private handleWriteMultipleRegs(frame: Buffer): Buffer {
    const startAddr = frame.readUInt16BE(8);
    const quantity = frame.readUInt16BE(10);

    // Collect VFD writes for batch forwarding to RTU bus
    let vfdBatchStart = -1;
    const vfdBatchValues: number[] = [];
    let touchedTriton = false;

    for (let i = 0; i < quantity; i++) {
      const addr = startAddr + i;
      const value = frame.readUInt16BE(13 + i * 2);

      if (addr <= 1) {
        this.state.analogOutputs[addr] = value;
      } else if (addr <= 3) {
        this.state.aoModes[addr - 2] = value === 1 ? 'current' : 'voltage';
      } else if (addr >= 100 && addr < 164) {
        this.state.vfdRegisters.set(addr, value);
        if (vfdBatchStart < 0) vfdBatchStart = addr;
        vfdBatchValues.push(value);
      } else if (addr >= 200 && addr < 264) {
        this.state.sensorRegisters.set(addr, value);
      } else if (addr >= 300 && addr < 320 && this.state.role === OrbitRole.GDC) {
        this.writeGDCRegister(addr, value);
      } else if (addr >= 320 && addr < 520 && this.state.role === OrbitRole.TRITON) {
        this.writeTritonRegister(addr, value);
        touchedTriton = true;
      }
    }

    if (touchedTriton) this.scheduleSave();

    // Forward VFD batch write to RTU bus
    if (vfdBatchStart >= 0 && vfdBatchValues.length > 0 && this.vfdBus) {
      this.vfdBus.writeMultipleFromOrbit(vfdBatchStart, vfdBatchValues).catch(() => {});
    }

    this.emit('output-change', { type: 'multi-reg', startAddr, quantity });
    const resp = Buffer.alloc(12);
    frame.copy(resp, 0, 0, 12);
    resp.writeUInt16BE(6, 4);
    return resp;
  }

  // ─── Modbus Helpers ───────────────────────────────────────────────────

  private makeResponse(request: Buffer, pdu: Buffer): Buffer {
    const resp = Buffer.alloc(MBAP_HEADER_LEN + 1 + pdu.length);
    request.copy(resp, 0, 0, 2);
    resp.writeUInt16BE(MODBUS_PROTOCOL, 2);
    resp.writeUInt16BE(1 + 1 + pdu.length, 4);
    resp[6] = request[6];
    resp[7] = request[7];
    pdu.copy(resp, 8);
    return resp;
  }

  private makeException(request: Buffer, exCode: number): Buffer {
    const resp = Buffer.alloc(MBAP_HEADER_LEN + 2);
    request.copy(resp, 0, 0, 2);
    resp.writeUInt16BE(MODBUS_PROTOCOL, 2);
    resp.writeUInt16BE(3, 4);
    resp[6] = request[6];
    resp[7] = request[7] | 0x80;
    resp[8] = exCode;
    return resp;
  }

  // ─── REST API + Web Panel Server ──────────────────────────────────────

  private startApiServer(port: number): void {
    this.apiServer = http.createServer((req, res) => {
      res.setHeader('Access-Control-Allow-Origin', '*');
      res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
      res.setHeader('Access-Control-Allow-Headers', 'Content-Type');

      if (req.method === 'OPTIONS') {
        res.writeHead(204);
        res.end();
        return;
      }

      const url = new URL(req.url ?? '/', `http://localhost:${port}`);

      // Web panel
      if (req.method === 'GET' && (url.pathname === '/' || url.pathname === '/index.html')) {
        if (this.panelHtml) {
          res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
          res.end(this.panelHtml);
        } else {
          res.writeHead(200, { 'Content-Type': 'text/html' });
          res.end('<h1>Orbit Simulator</h1><p>Web panel not found. API available at /api/status</p>');
        }
        return;
      }

      // API routes
      if (req.method === 'GET' && url.pathname === '/api/status') {
        this.apiGetStatus(res);
      } else if (req.method === 'GET' && url.pathname === '/api/ioconfig') {
        this.apiGetIoConfig(res);
      } else if (req.method === 'POST' && url.pathname === '/api/ioconfig') {
        this.apiSetIoConfig(req, res);
      } else if (req.method === 'GET' && url.pathname === '/api/sensors') {
        this.apiGetSensors(res);
      } else if (req.method === 'POST' && url.pathname === '/api/sensors') {
        this.apiSetSensors(req, res);
      } else if (req.method === 'POST' && url.pathname === '/api/sensors/value') {
        this.apiSetSensorValue(req, res);
      } else if (req.method === 'POST' && url.pathname === '/api/di') {
        this.apiSetDigitalInput(req, res);
      } else if (req.method === 'POST' && url.pathname === '/api/do') {
        this.apiSetDigitalOutput(req, res);
      } else if (req.method === 'POST' && url.pathname === '/api/estop') {
        this.apiSetEStop(req, res);
      } else if (req.method === 'POST' && url.pathname === '/api/reset') {
        this.apiReset(res);
      } else if (req.method === 'POST' && url.pathname === '/api/vfd/fault') {
        this.apiVfdFault(req, res);
      } else if (req.method === 'POST' && url.pathname === '/api/vfd/reset-fault') {
        this.apiVfdResetFault(req, res);
      } else if (req.method === 'GET' && url.pathname === '/api/vfd/drives') {
        this.apiGetVfdDrives(res);
      } else if (req.method === 'POST' && url.pathname === '/api/vfd/drives') {
        this.apiSetVfdDrive(req, res);
      } else if (req.method === 'DELETE' && url.pathname.startsWith('/api/vfd/drives/')) {
        this.apiDeleteVfdDrive(url.pathname, res);
      } else if (req.method === 'GET' && url.pathname === '/api/gdc') {
        this.apiGetGDC(res);
      } else if (req.method === 'POST' && url.pathname === '/api/gdc/door') {
        this.apiSetGDCDoor(req, res);
      } else if (req.method === 'POST' && url.pathname === '/api/gdc/stages') {
        this.apiSetGDCStages(req, res);
      } else if (req.method === 'POST' && url.pathname === '/api/gdc/calibrate') {
        this.apiStartCalibration(res);
      } else if (req.method === 'GET' && url.pathname === '/api/gdc/calibration') {
        this.apiGetCalibration(res);
      } else if (req.method === 'GET' && url.pathname === '/api/triton') {
        this.apiGetTriton(res);
      } else if (req.method === 'POST' && url.pathname === '/api/triton/manual') {
        this.apiSetTritonManual(req, res);
      } else if (req.method === 'POST' && url.pathname === '/api/triton/setpoints') {
        this.apiSetTritonSetpoints(req, res);
      } else if (req.method === 'POST' && url.pathname === '/api/triton/ack') {
        this.apiAckTritonAlarm(req, res);
      } else if (req.method === 'POST' && url.pathname === '/api/triton/safety/reset') {
        this.apiResetTritonLockout(req, res);
      } else if (req.method === 'POST' && url.pathname === '/api/triton/defrost') {
        this.apiTritonDefrost(req, res);
      } else if (req.method === 'GET' && url.pathname === '/api/triton/logs') {
        this.apiGetTritonLogs(req, res, url);
      } else if (req.method === 'POST' && url.pathname === '/api/triton/logs/reset') {
        this.apiResetTritonLogs(req, res);
      } else if (req.method === 'POST' && url.pathname === '/api/triton/sensor') {
        this.apiSetTritonSensor(req, res);
      } else if (req.method === 'POST' && url.pathname === '/api/role') {
        this.apiSetRole(req, res);
      } else if (req.method === 'GET' && url.pathname === '/api/chaos') {
        this.apiGetChaos(res);
      } else if (req.method === 'POST' && url.pathname === '/api/chaos') {
        this.apiSetChaos(req, res);
      } else {
        res.writeHead(404, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: 'Not found' }));
      }
    });

    this.apiServer.listen(port, '0.0.0.0');
  }

  private apiGetStatus(res: http.ServerResponse): void {
    const state: any = {
      id: this.state.id,
      ipAddress: this.state.ipAddress,
      role: this.state.role,
      roleName: OrbitRole[this.state.role],
      firmwareVersion: this.state.firmwareVersion,
      uptime: this.state.uptime,
      cpuTemp: this.state.cpuTemp,
      commLost: this.state.commLost,
      safeMode: this.state.safeMode,
      eStop: this.state.eStop,
      digitalInputs: this.state.digitalInputs,
      digitalOutputs: this.state.digitalOutputs,
      dc24vOutputs: this.state.dc24vOutputs,
      analogOutputs: this.state.analogOutputs,
      aoModes: this.state.aoModes,
      vfdActivity: this.state.vfdActivity,
      sensorActivity: this.state.sensorActivity,
      outputLabels: this.state.outputLabels,
      inputLabels: this.state.inputLabels,
      analogBoards: this.state.analogBoards,
    };
    // Add GDC-specific info
    if (this.state.role === OrbitRole.GDC) {
      state.gdc = {
        freshAirDoorPct: this.state.freshAirDoorPct,
        activeStageCount: this.state.activeStageCount,
        actuatorTravelTime: this.state.actuatorTravelTime,
        calibrating: this.state.calibrating,
        calibrationPhase: this.state.calibrationPhase,
        stages: this.state.gdcStages,
        actuators: this.state.gdcActuators,
      };
    }
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ ok: true, ...state }));
  }

  private apiSetDigitalInput(req: http.IncomingMessage, res: http.ServerResponse): void {
    let body = '';
    req.on('data', (chunk: Buffer) => { body += chunk.toString(); });
    req.on('end', () => {
      try {
        const { index, value } = JSON.parse(body);
        if (typeof index !== 'number' || index < 0 || index > 9) {
          res.writeHead(400, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ error: 'index must be 0-9' }));
          return;
        }
        this.state.digitalInputs[index] = !!value;
        this.emit('input-change', { type: 'di', index, value: !!value });
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: true }));
      } catch {
        res.writeHead(400, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: 'Invalid JSON' }));
      }
    });
  }

  /** POST /api/do — Set digital outputs from bridge/external controller */
  private apiSetDigitalOutput(req: http.IncomingMessage, res: http.ServerResponse): void {
    let body = '';
    req.on('data', (chunk: Buffer) => { body += chunk.toString(); });
    req.on('end', () => {
      try {
        const data = JSON.parse(body);
        // Accept { index, value } for single output or { outputs: boolean[] } for all
        if (Array.isArray(data.outputs)) {
          for (let i = 0; i < Math.min(data.outputs.length, 10); i++) {
            this.state.digitalOutputs[i] = !!data.outputs[i];
          }
          this.state.commLost = false; // Communication restored
        } else if (typeof data.index === 'number' && data.index >= 0 && data.index < 10) {
          this.state.digitalOutputs[data.index] = !!data.value;
          this.state.commLost = false;
        } else {
          res.writeHead(400, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ error: 'Provide { outputs: boolean[] } or { index, value }' }));
          return;
        }
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: true, outputs: this.state.digitalOutputs }));
      } catch {
        res.writeHead(400, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: 'Invalid JSON' }));
      }
    });
  }

  private apiSetEStop(req: http.IncomingMessage, res: http.ServerResponse): void {
    let body = '';
    req.on('data', (chunk: Buffer) => { body += chunk.toString(); });
    req.on('end', () => {
      try {
        const { active } = JSON.parse(body);
        this.state.eStop = !!active;
        if (this.state.eStop) {
          this.state.digitalOutputs.fill(false);
          this.state.dc24vOutputs.fill(false);
          this.state.analogOutputs = [0, 0];
          console.log(`[Orbit ${this.state.id}] E-STOP ACTIVATED — all outputs killed`);
        } else {
          console.log(`[Orbit ${this.state.id}] E-Stop released`);
        }
        this.emit('estop', { active: !!active });
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: true }));
      } catch {
        res.writeHead(400, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: 'Invalid JSON' }));
      }
    });
  }

  private apiGetIoConfig(res: http.ServerResponse): void {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({
      ok: true,
      outputLabels: this.state.outputLabels,
      inputLabels: this.state.inputLabels,
    }));
  }

  private apiSetIoConfig(req: http.IncomingMessage, res: http.ServerResponse): void {
    let body = '';
    req.on('data', (chunk: Buffer) => { body += chunk.toString(); });
    req.on('end', () => {
      try {
        const data = JSON.parse(body);
        if (Array.isArray(data.outputLabels)) {
          for (let i = 0; i < 10 && i < data.outputLabels.length; i++) {
            this.state.outputLabels[i] = String(data.outputLabels[i] ?? '');
          }
        }
        if (Array.isArray(data.inputLabels)) {
          for (let i = 0; i < 10 && i < data.inputLabels.length; i++) {
            this.state.inputLabels[i] = String(data.inputLabels[i] ?? '');
          }
        }
        this.save();
        console.log(`[Orbit ${this.state.id}] I/O config updated — outputs: [${this.state.outputLabels.filter(Boolean).join(', ')}]`);
        this.emit('ioconfig-change');
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: true }));
      } catch {
        res.writeHead(400, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: 'Invalid JSON' }));
      }
    });
  }

  private apiGetSensors(res: http.ServerResponse): void {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ ok: true, boards: this.state.analogBoards }));
  }

  private apiSetSensors(req: http.IncomingMessage, res: http.ServerResponse): void {
    let body = '';
    req.on('data', (chunk: Buffer) => { body += chunk.toString(); });
    req.on('end', () => {
      try {
        const data = JSON.parse(body);
        if (Array.isArray(data.boards)) {
          this.state.analogBoards = data.boards.slice(0, 8).map((b: any) => ({
            address: Number(b.address ?? 0),
            type: Number(b.type ?? SENSOR_TYPE_TEMP),
            present: b.present !== false,
            label: String(b.label ?? ''),
            sensorTypes: Array.isArray(b.sensorTypes)
              ? b.sensorTypes.slice(0, 4).map(Number)
              : [SENSOR_TYPE_NONE, SENSOR_TYPE_NONE, SENSOR_TYPE_NONE, SENSOR_TYPE_NONE],
            sensorValues: Array.isArray(b.sensorValues)
              ? b.sensorValues.slice(0, 4).map(Number)
              : [SENSOR_VAL_UNDEF, SENSOR_VAL_UNDEF, SENSOR_VAL_UNDEF, SENSOR_VAL_UNDEF],
            sensorLabels: Array.isArray(b.sensorLabels)
              ? b.sensorLabels.slice(0, 4).map(String)
              : ['', '', '', ''],
          }));
          this.save();
          console.log(`[Orbit ${this.state.id}] Sensor config: ${this.state.analogBoards.length} boards`);
        }
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: true }));
      } catch {
        res.writeHead(400, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: 'Invalid JSON' }));
      }
    });
  }

  private apiSetSensorValue(req: http.IncomingMessage, res: http.ServerResponse): void {
    let body = '';
    req.on('data', (chunk: Buffer) => { body += chunk.toString(); });
    req.on('end', () => {
      try {
        const { boardIndex, sensorIndex, value } = JSON.parse(body);
        const b = this.state.analogBoards[boardIndex];
        if (!b || sensorIndex < 0 || sensorIndex > 3) {
          res.writeHead(400, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ error: 'Invalid board/sensor index' }));
          return;
        }
        b.sensorValues[sensorIndex] = Number(value);
        this.save();

        // Forward to sensor board sim so the Modbus-polled values stay in sync
        const sensorApiPort = parseInt(process.env.SENSOR_API_PORT ?? '9020', 10);
        const sensorApiHost = process.env.SENSOR_API_HOST ?? 'localhost';
        const st = b.sensorTypes[sensorIndex];
        // Orbit HR int16 encoding → engineering for the RTU sensor-board sim:
        //   CO2           = raw ppm   (no scale)
        //   static press  = ÷100      ("wc, ×100 wire — NOT the ×10 convention)
        //   temp/humid    = ÷10
        const engValue =
          (st === SENSOR_TYPE_CO2)              ? Number(value)
          : (st === SENSOR_TYPE_STATIC_PRESSURE) ? Number(value) / 100
          :                                        Number(value) / 10;
        const payload = JSON.stringify({ address: b.address, sensor: sensorIndex, value: engValue });
        const fwdReq = http.request({
          hostname: sensorApiHost,
          port: sensorApiPort,
          path: '/api/sensor/value',
          method: 'POST',
          headers: { 'Content-Type': 'application/json', 'Content-Length': Buffer.byteLength(payload) },
        }, () => {});
        fwdReq.on('error', () => {}); // ignore — sensor sim may not be running
        fwdReq.end(payload);

        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: true }));
      } catch {
        res.writeHead(400, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: 'Invalid JSON' }));
      }
    });
  }

  private apiReset(res: http.ServerResponse): void {
    const id = this.state.id;
    const role = this.state.role; // Preserve role on reset
    this.state = createDefaultState(id, role);
    this.save();
    console.log(`[Orbit ${this.state.id}] Reset to defaults (role: ${OrbitRole[role]})`);
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ ok: true }));
  }

  private apiVfdFault(req: http.IncomingMessage, res: http.ServerResponse): void {
    let body = '';
    req.on('data', (c) => { body += c; });
    req.on('end', () => {
      try {
        const { unitId, faultCode } = JSON.parse(body);
        if (typeof unitId !== 'number' || typeof faultCode !== 'number') {
          res.writeHead(400, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ ok: false, error: 'unitId and faultCode required' }));
          return;
        }
        if (!this.vfdFaultHandler) {
          res.writeHead(503, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ ok: false, error: 'VFD simulator not available' }));
          return;
        }
        const ok = this.vfdFaultHandler(unitId, faultCode);
        res.writeHead(ok ? 200 : 404, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok }));
      } catch {
        res.writeHead(400, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: false, error: 'Invalid JSON' }));
      }
    });
  }

  private apiVfdResetFault(req: http.IncomingMessage, res: http.ServerResponse): void {
    let body = '';
    req.on('data', (c) => { body += c; });
    req.on('end', () => {
      try {
        const { unitId } = JSON.parse(body);
        if (typeof unitId !== 'number') {
          res.writeHead(400, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ ok: false, error: 'unitId required' }));
          return;
        }
        if (!this.vfdSim) {
          res.writeHead(503, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ ok: false, error: 'VFD simulator not available' }));
          return;
        }
        const ok = this.vfdSim.clearFault(unitId);
        res.writeHead(ok ? 200 : 404, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok }));
      } catch {
        res.writeHead(400, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: false, error: 'Invalid JSON' }));
      }
    });
  }

  private apiGetVfdDrives(res: http.ServerResponse): void {
    if (!this.vfdSim) {
      res.writeHead(503, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ ok: false, error: 'VFD simulator not available' }));
      return;
    }
    const drives = this.vfdSim.getDrives().map(d => ({
      unitId: d.unitId,
      label: d.label,
      hp: Math.round(d.ratedPowerkW / 7.46),
      running: d.running,
      faulted: d.faulted,
      speedPercent: (d.actualSpeedPercent / 100).toFixed(1),
      freqHz: (d.outputFreqHz / 10).toFixed(1),
    }));
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ ok: true, drives }));
  }

  private apiSetVfdDrive(req: http.IncomingMessage, res: http.ServerResponse): void {
    let body = '';
    req.on('data', (c) => { body += c; });
    req.on('end', () => {
      try {
        const { unitId, label, hp } = JSON.parse(body);
        if (typeof unitId !== 'number' || unitId < 1 || unitId > 247) {
          res.writeHead(400, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ ok: false, error: 'unitId must be 1-247' }));
          return;
        }
        if (!this.vfdSim) {
          res.writeHead(503, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ ok: false, error: 'VFD simulator not available' }));
          return;
        }
        this.vfdSim.setDrive(unitId, label || `VFD Unit ${unitId}`, typeof hp === 'number' ? hp : 20);
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: true }));
      } catch {
        res.writeHead(400, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: false, error: 'Invalid JSON' }));
      }
    });
  }

  private apiDeleteVfdDrive(pathname: string, res: http.ServerResponse): void {
    const parts = pathname.split('/');
    const unitId = parseInt(parts[parts.length - 1], 10);
    if (isNaN(unitId)) {
      res.writeHead(400, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ ok: false, error: 'Invalid unit ID' }));
      return;
    }
    if (!this.vfdSim) {
      res.writeHead(503, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ ok: false, error: 'VFD simulator not available' }));
      return;
    }
    const ok = this.vfdSim.removeDrive(unitId);
    res.writeHead(ok ? 200 : 404, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ ok }));
  }

  // ─── GDC API Endpoints ─────────────────────────────────────────────────

  /** GET /api/gdc — Get GDC door controller status */
  private apiGetGDC(res: http.ServerResponse): void {
    if (this.state.role !== OrbitRole.GDC) {
      res.writeHead(400, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ ok: false, error: 'Orbit is not in GDC mode' }));
      return;
    }
    // Build door-to-stage summary for each stage
    const stagesWithDoors = this.state.gdcStages.map(stage => ({
      ...stage,
      doors: this.state.gdcActuators
        .filter(a => a.stageAssignment === stage.stageNum)
        .map(a => ({ index: this.state.gdcActuators.indexOf(a) + 1, label: a.label })),
    }));
    // Calculate total system capacity (sum of all door travel times)
    let totalCapacity = 0;
    for (const act of this.state.gdcActuators) {
      if (act.stageAssignment > 0) {
        totalCapacity += (act.calibrated && act.openTravelTime > 0)
          ? act.openTravelTime
          : this.state.actuatorTravelTime;
      }
    }
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({
      ok: true,
      freshAirDoorPct: this.state.freshAirDoorPct,
      activeStageCount: this.state.activeStageCount,
      actuatorTravelTime: this.state.actuatorTravelTime,
      totalCapacity,
      calibrating: this.state.calibrating,
      calibrationPhase: this.state.calibrationPhase,
      stages: stagesWithDoors,
      actuators: this.state.gdcActuators,
    }));
  }

  /** POST /api/gdc/door — Set fresh air door percentage { pct: 0-100 } */
  private apiSetGDCDoor(req: http.IncomingMessage, res: http.ServerResponse): void {
    if (this.state.role !== OrbitRole.GDC) {
      res.writeHead(400, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ ok: false, error: 'Orbit is not in GDC mode' }));
      return;
    }
    let body = '';
    req.on('data', (chunk: Buffer) => { body += chunk.toString(); });
    req.on('end', () => {
      try {
        const { pct, travelTime } = JSON.parse(body);
        if (typeof pct === 'number') {
          this.setFreshAirDoorPct(pct);
          console.log(`[Orbit ${this.state.id}] Fresh air door: ${this.state.freshAirDoorPct}%`);
        }
        if (typeof travelTime === 'number' && travelTime > 0) {
          this.state.actuatorTravelTime = travelTime;
        }
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({
          ok: true,
          freshAirDoorPct: this.state.freshAirDoorPct,
        }));
      } catch {
        res.writeHead(400, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: 'Invalid JSON' }));
      }
    });
  }

  /** POST /api/gdc/stages — Configure GDC stages and door assignments
   * Body: { stages: [{ stageNum, label?, doors: [1,2,...] }] }
   * Stages define door priority ORDER for proportional fill.
   * Stage 1 doors open first, then stage 2, etc.
   * Example: { stages: [
   *   { stageNum: 1, doors: [1] },
   *   { stageNum: 2, doors: [2, 3] },
   *   { stageNum: 3, doors: [4, 5] }
   * ]}
   */
  private apiSetGDCStages(req: http.IncomingMessage, res: http.ServerResponse): void {
    if (this.state.role !== OrbitRole.GDC) {
      res.writeHead(400, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ ok: false, error: 'Orbit is not in GDC mode' }));
      return;
    }
    let body = '';
    req.on('data', (chunk: Buffer) => { body += chunk.toString(); });
    req.on('end', () => {
      try {
        const { stages } = JSON.parse(body);
        if (!Array.isArray(stages) || stages.length === 0) {
          res.writeHead(400, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ error: 'stages must be a non-empty array' }));
          return;
        }
        // Validate and apply stage configuration
        const newStages: GDCStage[] = [];
        for (const s of stages) {
          if (typeof s.stageNum !== 'number') {
            res.writeHead(400, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ error: 'Each stage must have stageNum' }));
            return;
          }
          newStages.push({
            stageNum: s.stageNum,
            label: s.label || `Stage ${s.stageNum}`,
          });
          // Assign doors to this stage
          if (Array.isArray(s.doors)) {
            for (const doorNum of s.doors) {
              const idx = doorNum - 1;
              if (idx >= 0 && idx < this.state.gdcActuators.length) {
                this.state.gdcActuators[idx].stageAssignment = s.stageNum;
              }
            }
          }
        }
        // Sort by stage number (priority order)
        newStages.sort((a, b) => a.stageNum - b.stageNum);
        this.state.gdcStages = newStages;
        // Re-evaluate which doors should be open
        this.updateGDCActuators();
        console.log(`[Orbit ${this.state.id}] GDC stages reconfigured: ${newStages.length} stages`);
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({
          ok: true,
          stages: this.state.gdcStages,
          actuators: this.state.gdcActuators,
        }));
      } catch {
        res.writeHead(400, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: 'Invalid JSON' }));
      }
    });
  }

  /** POST /api/gdc/calibrate — Start automatic GDC door calibration */
  private apiStartCalibration(res: http.ServerResponse): void {
    const result = this.startCalibration();
    if (!result.ok) {
      res.writeHead(400, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify(result));
      return;
    }
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({
      ok: true,
      message: 'Calibration started. All doors will open then close to measure travel times.',
      status: this.getCalibrationStatus(),
    }));
  }

  /** GET /api/gdc/calibration — Get calibration status and results */
  private apiGetCalibration(res: http.ServerResponse): void {
    if (this.state.role !== OrbitRole.GDC) {
      res.writeHead(400, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ ok: false, error: 'Orbit is not in GDC mode' }));
      return;
    }
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({
      ok: true,
      ...this.getCalibrationStatus(),
    }));
  }

  // ─── Triton Refrigeration Simulation ───────────────────────────────────

  /**
   * Step the refrigeration simulation forward by one tick (~500 ms).
   *
   * Behavior:
   *  - In 'auto' mode the compressor cycles based on suction PSI vs
   *    cutInP/cutOutP.  Anti-short-cycle is enforced via minOffTime.
   *  - 'force-on' / 'force-off' override the auto cycling for manual control
   *    from the SCADA popups.
   *  - When running, suction PSI is pulled toward (cutOutP - 5) and
   *    discharge PSI rises toward an ambient-influenced target.  The EXV
   *    modulates to hold superheat near setpoints.superheatTarget.
   *  - When idle, all pressures and temps drift toward ambient.
   *  - Condenser fans stage on/off based on dischargeP vs fanStageOnP /
   *    fanStageOffP (with hysteresis).
   *  - Alarms latch (and stay until acked) for low/high sensor excursions.
   */
  private updateTriton(): void {
    if (this.state.role !== OrbitRole.TRITON) return;
    const t = this.state.triton;

    // Build the per-tick context the pure control core consumes.  The temp
    // board lives at index 0, the pressure board at index 1; if either is
    // absent or the wrong type we omit the writer so the core latches the
    // matching BOARD_MISSING_* alarm.
    const tempBoard  = this.state.analogBoards[0];
    const pressBoard = this.state.analogBoards[1];
    const tempOk  = !!tempBoard  && tempBoard.present  && tempBoard.type  === SENSOR_TYPE_TEMP;
    const pressOk = !!pressBoard && pressBoard.present && pressBoard.type === SENSOR_TYPE_PRESSURE;

    const ctx: TritonControlCtx = {
      dt:    0.5, // matches setInterval(updateTriton, 500)
      nowMs: Date.now(),
      digitalInputs: this.state.digitalInputs,
      writeTempBoard: tempOk
        ? (v) => {
            tempBoard.sensorValues[0] = v[0];
            tempBoard.sensorValues[1] = v[1];
            tempBoard.sensorValues[2] = v[2];
            tempBoard.sensorValues[3] = v[3];
          }
        : undefined,
      writePressBoard: pressOk
        ? (v) => {
            pressBoard.sensorValues[0] = v[0];
            pressBoard.sensorValues[1] = v[1];
            pressBoard.sensorValues[2] = v[2];
            pressBoard.sensorValues[3] = v[3];
          }
        : undefined,
    };
    tickTritonControl(t, ctx);
    // Phase 8 — translate logical control state to physical orbit I/O
    // (10 DO + 2 AO) per the operator-configured ioConfig mapping.
    this.applyTritonIoMapping();
  }

  /** GET /api/triton — full Triton state snapshot (for SCADA rendering) */
  private apiGetTriton(res: http.ServerResponse): void {
    if (this.state.role !== OrbitRole.TRITON) {
      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ ok: true, present: false }));
      return;
    }
    const t = this.state.triton;
    // Derived helpers the UI commonly wants.  Use the GRC P-T table so the
    // displayed superheat matches what the EXV PID is actually controlling on.
    const satT = tsatF(t.setpoints.refrigerantType, t.sensors.suctionP.value);
    const superheat = t.sensors.suctionT.value - satT;
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({
      ok: true,
      present: true,
      orbitId: this.state.id,
      role: OrbitRole[this.state.role],
      enabled: t.enabled,
      label: t.label,
      manualMode: t.manualMode,
      compressor: {
        on: t.compressorOn,
        runtimeSec: Math.round(t.compressorRuntimeSec),
        amps: Number(t.compressorAmps.toFixed(1)),
        status: t.compressorStatus,
        totalRuntimeHours: t.totalRuntimeHours,
        dailyRuntimeHours: t.dailyRuntimeHours,
      },
      condenserFans: {
        stage: t.condenserFanStage,
        count: t.condenserFanCount,
        targetP: Number(t.dischargeTargetP.toFixed(1)),
        leadIndex: t.condenserLeadIndex ?? 0,
        vfdPct: Number((t.condenserVfdPct ?? 0).toFixed(1)),
        // Live cond-fan PID state (deferral #3) — only meaningful when
        // `condFanVfdMode==1`.  Exposed for SCADA / smoke probes.
        pid: t.condPid ? {
          intError:  Number(t.condPid.intError.toFixed(2)),
          prevErr:   Number(t.condPid.prevErr.toFixed(2)),
          targetPct: Number(t.condPid.targetPct.toFixed(2)),
          lastSampleMs: t.condPid.lastPidMs,
        } : null,
        fans: (t.condenserFans ?? []).map(f => ({
          on: f.on,
          runtimeHours: Number((f.runtimeSec / 3600).toFixed(2)),
        })),
      },
      evapFanOn: t.evapFanOn,
      exvOpenPct: Number(t.exvOpenPct.toFixed(1)),
      // Capacity-stage / unloader telemetry.
      unloaders: {
        on:       (t.unloaderOn       ?? []).slice(0, 2),
        hpForced: (t.unloaderHpForced ?? []).slice(0, 2),
        lpForced: (t.unloaderLpForced ?? []).slice(0, 2),
        normal:   (t.setpoints as any).unloaderNormal ?? 0,
      },
      oilPumpOn: !!t.oilPumpOn,
      defrostActive: t.defrostActive,
      lastDefrostEnd: t.lastDefrostEnd,
      defrost: {
        // Phase 5 GRC defrost SM telemetry.  `stage`: 0 NONE / 1 PUMPDOWN /
        // 2 ACTIVE / 3 DRIP.  `type`: 0 NONE / 1 TIMED / 2 MANUAL / 3 TREND.
        stage:           t.defrostStage ?? 0,
        type:            t.defrostType  ?? 0,
        stageSec:        Math.round(t.defrostStageSec     ?? 0),
        sinceLastSec:    Math.round(t.defrostSinceLastSec ?? 0),
        manualPending:   !!t.defrostManualPending,
        mode:            t.setpoints.defrostMode,
        termType:        t.setpoints.defrostTermType,
        termTemperature: t.setpoints.defrostTermT,
        termPressure:    t.setpoints.defrostTermP,
        intervalHours:   t.setpoints.defrostIntervalHours,
        maxMinutes:      t.setpoints.defrostMaxMinutes,
        dripTimeSec:     t.setpoints.dripTimeSec,
        // Phase 5.5 trend-defrost telemetry.
        trend: t.defrostTrend ? {
          enabled:       (t.setpoints as any).defrostTrendVarTimerMin > 0,
          warmupSec:     Math.round(t.defrostTrend.warmupSec),
          warmupTargetSec: Math.round(((t.setpoints as any).defrostTrendVarTimerMin ?? 0) * 60),
          samples:       t.defrostTrend.count,
          trendPsi:      isFinite(t.defrostTrend.trendPsi) ? Number(t.defrostTrend.trendPsi.toFixed(2)) : null,
          diffP:         (t.setpoints as any).defrostTrendDiffP ?? 5,
          initiateSec:   Math.round(t.defrostTrend.initiateSec),
          initiateTargetSec: Math.round(((t.setpoints as any).defrostTrendInitiateMin ?? 0) * 60),
        } : null,
      },
      demand: t.demand,
      sensors: t.sensors,
      derived: {
        superheat: Number(superheat.toFixed(1)),
        leakDetect: {
          shAvgF:          Number(t.leakDetect.shAvgF.toFixed(1)),
          exvAvgPct:       Number(t.leakDetect.exvAvgPct.toFixed(1)),
          sustainedSec:    Math.round(t.leakDetect.sustainedSec),
          leakAlarmActive: t.leakDetect.leakAlarmActive,
          warmupSec:       Math.round(t.leakDetect.warmupSec),
        },
      },
      setpoints: t.setpoints,
      failures: t.failures,
      ioConfig: t.ioConfig,
      safeties: t.safeties,
      // Phase 2 GRC compressor SM exposed timers (seconds).  UI / tests
      // poll these to verify the prove window, pumpdown bleed, and phase-
      // monitor escalation cadences match GRC behaviour.
      crankcaseProveSecRemaining: Math.round(t.crankcaseProveSecRemaining ?? 0),
      pumpdownSecRemaining:       Math.round(t.pumpdownSecRemaining ?? 0),
      phaseLossSec:               Math.round((t.phaseLossSec ?? 0) * 10) / 10,
      // Phase 6 — per-channel failure-timer state.  `overSec` is how
      // long the threshold has been continuously violated; `tripped`
      // latches when overSec reaches the channel's `delaySec`.  Cleared
      // by the ack-all command.  `_pumpdownArmed` is the one-shot flag
      // that gates the GRC-style 60 s pumpdown bleed on first trip.
      failureStates: t.failureStates,
      safeOffMask: t.safeOffMask ?? 0,
      // Safe-mode policy snapshot (deferral #4).  `active` is true when
      // any hard-trip / safeOff / lockout is engaged.
      safePolicy: {
        active: ((t.safeOffMask ?? 0) !== 0)
                || ((t.safeties?.lockoutMask ?? 0) !== 0),
        condFans:    (t.setpoints as any).safePolicyCondFans   ?? 0,
        condVfdPct:  (t.setpoints as any).safePolicyCondVfdPct ?? 0,
        exvPct:      (t.setpoints as any).safePolicyExvPct     ?? 0,
        oilPump:     (t.setpoints as any).safePolicyOilPump    ?? 0,
      },
      alarms: t.alarms,
      // Phase 7 — log buffer summary (counts + most-recent ts).  Full
      // entries are fetched via GET /api/triton/logs?type=…
      logs: {
        pid:  { count: t.logs?.pid.length  ?? 0,
                latestMs: t.logs?.pid.length  ? t.logs!.pid[t.logs!.pid.length-1].ts   : 0 },
        user: { count: t.logs?.user.length ?? 0,
                latestMs: t.logs?.user.length ? t.logs!.user[t.logs!.user.length-1].ts : 0 },
        sys:  { count: t.logs?.sys.length  ?? 0,
                latestMs: t.logs?.sys.length  ? t.logs!.sys[t.logs!.sys.length-1].ts   : 0 },
      },
      // Phase 8 — physical orbit I/O surface as resolved by
      // applyTritonIoMapping().  These are the same values the
      // Modbus master sees in the DO/AO HRs (and the same values a
      // real AM2432 HAL would drive onto the terminal block).
      physical: {
        digitalOutputs: this.state.digitalOutputs.slice(),
        analogOutputs:  this.state.analogOutputs.slice(),
        outputLabels:   this.state.outputLabels.slice(),
        aoMode:         t.ioConfig.aoMode.slice(),
        doRole:         t.ioConfig.doRole.slice(),
      },
    }));
  }

  /** POST /api/triton/manual — body: { mode: 'auto'|'force-on'|'force-off' } */
  private apiSetTritonManual(req: http.IncomingMessage, res: http.ServerResponse): void {
    if (this.state.role !== OrbitRole.TRITON) {
      res.writeHead(400, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ ok: false, error: 'Orbit is not in Triton mode' }));
      return;
    }
    let body = '';
    req.on('data', (chunk: Buffer) => { body += chunk.toString(); });
    req.on('end', () => {
      try {
        const { mode, demand } = JSON.parse(body);
        if (!['auto', 'force-on', 'force-off'].includes(mode)) {
          res.writeHead(400, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ ok: false, error: 'invalid mode' }));
          return;
        }
        this.state.triton.manualMode = mode;
        if (typeof demand === 'number' && isFinite(demand)) {
          this.state.triton.demand = Math.max(0, Math.min(100, demand));
        }
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: true, mode, demand: this.state.triton.demand }));
      } catch {
        res.writeHead(400, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: false, error: 'Invalid JSON' }));
      }
    });
  }

  /**
   * POST /api/triton/defrost — body: { action: 'start' | 'stop' }.
   * `start`: arms `defrostManualPending`; the next tick of the SM picks it
   * up and transitions NONE → PUMPDOWN/ACTIVE with `type=MANUAL`.
   * `stop`:  hard-aborts any active defrost (jumps straight back to NONE,
   * resets stage timer, sets `lastDefrostEnd`, restarts the override timer).
   */
  private apiTritonDefrost(req: http.IncomingMessage, res: http.ServerResponse): void {
    if (this.state.role !== OrbitRole.TRITON) {
      res.writeHead(400, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ ok: false, error: 'Orbit is not in Triton mode' }));
      return;
    }
    let body = '';
    req.on('data', (chunk: Buffer) => { body += chunk.toString(); });
    req.on('end', () => {
      try {
        const { action } = JSON.parse(body || '{}');
        const t = this.state.triton;
        if (action === 'start') {
          t.defrostManualPending = true;
        } else if (action === 'stop') {
          t.defrostStage         = 0;
          t.defrostType          = 0;
          t.defrostStageSec      = 0;
          t.defrostSinceLastSec  = 0;
          t.defrostManualPending = false;
          t.defrostActive        = false;
          t.lastDefrostEnd       = Date.now();
          if (t.pumpdownSecRemaining > 0) t.pumpdownSecRemaining = 0;
        } else {
          res.writeHead(400, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ ok: false, error: 'invalid action (start|stop)' }));
          return;
        }
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({
          ok: true, action,
          stage: t.defrostStage, type: t.defrostType,
          manualPending: t.defrostManualPending,
        }));
      } catch {
        res.writeHead(400, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: false, error: 'Invalid JSON' }));
      }
    });
  }

  /**
   * GET /api/triton/logs?type=pid|user|sys&limit=N&from=tsMs&to=tsMs
   * Returns a slice of the requested ring buffer.  Defaults: type=sys,
   * limit=100 (newest-first slice).  `from`/`to` are inclusive ms epoch
   * filters applied before the limit.  Phase 7 — see `TritonLogs`.
   */
  private apiGetTritonLogs(
    req: http.IncomingMessage, res: http.ServerResponse, url: URL,
  ): void {
    if (this.state.role !== OrbitRole.TRITON) {
      res.writeHead(400, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ ok: false, error: 'Orbit is not in Triton mode' }));
      return;
    }
    const type   = (url.searchParams.get('type') || 'sys').toLowerCase();
    const limit  = Math.max(1, Math.min(1024,
      Number(url.searchParams.get('limit') || 100)));
    const from   = url.searchParams.has('from')
      ? Number(url.searchParams.get('from')) : null;
    const to     = url.searchParams.has('to')
      ? Number(url.searchParams.get('to'))   : null;
    const logs   = this.state.triton.logs ?? { pid: [], user: [], sys: [] };
    let src: { ts: number }[] = [];
    if      (type === 'pid')  src = logs.pid;
    else if (type === 'user') src = logs.user;
    else if (type === 'sys')  src = logs.sys;
    else {
      res.writeHead(400, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ ok: false, error: 'invalid type (pid|user|sys)' }));
      return;
    }
    let filtered = src;
    if (from !== null) filtered = filtered.filter(e => e.ts >= from);
    if (to   !== null) filtered = filtered.filter(e => e.ts <= to);
    // Newest-first slice (last `limit` entries reversed).
    const slice = filtered.slice(-limit).reverse();
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({
      ok: true, type,
      total: src.length, returned: slice.length,
      entries: slice,
    }));
  }

  /**
   * POST /api/triton/logs/reset — body: { kind: 'pid'|'user'|'sys'|'all' }.
   * Clears the chosen ring buffer(s).  `all` also rezeroes
   * `lastUserLogMs` so the next tick re-emits a BOOT entry.
   */
  private apiResetTritonLogs(req: http.IncomingMessage, res: http.ServerResponse): void {
    if (this.state.role !== OrbitRole.TRITON) {
      res.writeHead(400, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ ok: false, error: 'Orbit is not in Triton mode' }));
      return;
    }
    let body = '';
    req.on('data', (c: Buffer) => { body += c.toString(); });
    req.on('end', () => {
      try {
        const { kind } = JSON.parse(body || '{}');
        const t = this.state.triton;
        if (!t.logs) t.logs = { pid: [], user: [], sys: [], lastUserLogMs: 0 };
        if (kind === 'pid' || kind === 'all') t.logs.pid  = [];
        if (kind === 'user'|| kind === 'all') t.logs.user = [];
        if (kind === 'sys' || kind === 'all') t.logs.sys  = [];
        if (kind === 'all') t.logs.lastUserLogMs = 0;
        if (!['pid','user','sys','all'].includes(kind)) {
          res.writeHead(400, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ ok: false, error: 'invalid kind (pid|user|sys|all)' }));
          return;
        }
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({
          ok: true, kind,
          counts: {
            pid: t.logs.pid.length, user: t.logs.user.length, sys: t.logs.sys.length,
          },
        }));
      } catch {
        res.writeHead(400, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: false, error: 'Invalid JSON' }));
      }
    });
  }

  /**
   * POST /api/triton/sensor — sim-only sensor injection.
   * Body: `{ name: keyof TritonSensors, value: number, valid?: boolean }`.
   * Writes directly into `t.sensors[name]` so smoke probes can prime
   * physics inputs (ambient, dischargeP, suctionP, etc.) without going
   * through the Modbus path.  Production firmware has no equivalent —
   * sensor reads come from the analog board.  See
   * `/memories/repo/triton-orbit-spec.md` "sim-only REST" notes.
   */
  private apiSetTritonSensor(req: http.IncomingMessage, res: http.ServerResponse): void {
    if (this.state.role !== OrbitRole.TRITON) {
      res.writeHead(400, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ ok: false, error: 'Orbit is not in Triton mode' }));
      return;
    }
    let body = '';
    req.on('data', (c: Buffer) => { body += c.toString(); });
    req.on('end', () => {
      try {
        const { name, value, valid } = JSON.parse(body || '{}');
        const s = (this.state.triton.sensors as any)[name];
        if (!s || typeof s !== 'object') {
          res.writeHead(400, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ ok: false, error: `unknown sensor '${name}'` }));
          return;
        }
        if (typeof value === 'number' && isFinite(value)) s.value = value;
        if (typeof valid === 'boolean')                   s.valid = valid;
        else                                              s.valid = true;
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: true, name, value: s.value, valid: s.valid }));
      } catch {
        res.writeHead(400, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: false, error: 'Invalid JSON' }));
      }
    });
  }

  /**
   * POST /api/triton/setpoints — body: partial TritonSetpoints.
   * Only provided keys are updated; everything else is left alone.
   */
  private apiSetTritonSetpoints(req: http.IncomingMessage, res: http.ServerResponse): void {
    if (this.state.role !== OrbitRole.TRITON) {
      res.writeHead(400, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ ok: false, error: 'Orbit is not in Triton mode' }));
      return;
    }
    let body = '';
    req.on('data', (chunk: Buffer) => { body += chunk.toString(); });
    req.on('end', () => {
      try {
        const patch = JSON.parse(body) as Partial<TritonSetpoints> & {
          enabled?: boolean; label?: string;
        };
        if (typeof patch.enabled === 'boolean') this.state.triton.enabled = patch.enabled;
        if (typeof patch.label === 'string') this.state.triton.label = patch.label;
        Object.assign(this.state.triton.setpoints, patch);
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: true, setpoints: this.state.triton.setpoints }));
      } catch {
        res.writeHead(400, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: false, error: 'Invalid JSON' }));
      }
    });
  }

  /** POST /api/triton/ack — body: { code: string } | { all: true } */
  private apiAckTritonAlarm(req: http.IncomingMessage, res: http.ServerResponse): void {
    if (this.state.role !== OrbitRole.TRITON) {
      res.writeHead(400, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ ok: false, error: 'Orbit is not in Triton mode' }));
      return;
    }
    let body = '';
    req.on('data', (chunk: Buffer) => { body += chunk.toString(); });
    req.on('end', () => {
      try {
        const { code, all } = JSON.parse(body);
        const t = this.state.triton;
        if (all) {
          t.alarms.forEach(a => { a.acked = true; });
          t.alarms = t.alarms.filter(a => a.active);
          // Match the ack-all Modbus path — clear timer state & SAFE_OFF.
          if (t.failureStates) {
            for (const k of Object.keys(t.failureStates) as Array<keyof TritonFailureStates>) {
              t.failureStates[k].overSec = 0;
              t.failureStates[k].tripped = false;
              t.failureStates[k]._pumpdownArmed = false;
            }
          }
          t.safeOffMask = 0;
        } else {
          const a = t.alarms.find(x => x.code === code);
          if (a) {
            a.acked = true;
            if (!a.active) t.alarms = t.alarms.filter(x => x !== a);
          }
        }
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: true, alarms: t.alarms }));
      } catch {
        res.writeHead(400, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: false, error: 'Invalid JSON' }));
      }
    });
  }

  /** POST /api/triton/safety/reset — clear latched lockout bits.
   *  Body: { mask?: number }  (default 0xFF — clear all).
   *  Also acks the matching SAF_* alarms. */
  private apiResetTritonLockout(req: http.IncomingMessage, res: http.ServerResponse): void {
    if (this.state.role !== OrbitRole.TRITON) {
      res.writeHead(400, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ ok: false, error: 'Orbit is not in Triton mode' }));
      return;
    }
    let body = '';
    req.on('data', (chunk: Buffer) => { body += chunk.toString(); });
    req.on('end', () => {
      try {
        const parsed = body ? JSON.parse(body) : {};
        const mask = typeof parsed.mask === 'number' ? parsed.mask : 0xFF;
        const t = this.state.triton;
        t.safeties.lockoutMask &= ~mask;
        // Bit 3 = crankcaseProveUnmet.  When the operator clears that bit
        // they're declaring "compressor is allowed to run NOW" — also zero
        // the prove-window timer so the next tick doesn't immediately
        // re-latch SAF_PROVE.  The natural countdown will resume only if
        // crankcaseRunHours > 0 AND DI_1 reads open while comp is OFF.
        if ((mask & 0x08) !== 0) t.crankcaseProveSecRemaining = 0;
        // Auto-ack the matching latched alarms so they clear once the
        // input has restored.
        const codes = ['SAF_HP', 'SAF_COMP_OL', 'SAF_PROVE'];
        for (const c of codes) {
          const a = t.alarms.find(x => x.code === c);
          if (a) {
            a.acked = true;
            if (!a.active) t.alarms = t.alarms.filter(x => x !== a);
          }
        }
        this.scheduleSave();
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: true, lockoutMask: t.safeties.lockoutMask }));
      } catch {
        res.writeHead(400, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: false, error: 'Invalid JSON' }));
      }
    });
  }

  /** POST /api/role — Change orbit role { role: 0|1|2|3 or "STORAGE"|"GDC"|... } */
  private apiSetRole(req: http.IncomingMessage, res: http.ServerResponse): void {
    let body = '';
    req.on('data', (chunk: Buffer) => { body += chunk.toString(); });
    req.on('end', () => {
      try {
        const { role } = JSON.parse(body);
        let newRole: OrbitRole;
        if (typeof role === 'string') {
          newRole = OrbitRole[role.toUpperCase() as keyof typeof OrbitRole];
          if (newRole === undefined) {
            res.writeHead(400, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ ok: false, error: 'Invalid role name' }));
            return;
          }
        } else if (typeof role === 'number' && role >= 0 && role <= 3) {
          newRole = role;
        } else {
          res.writeHead(400, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ ok: false, error: 'role must be 0-3 or role name' }));
          return;
        }
        const oldRole = this.state.role;
        this.state.role = newRole;
        this.state.firmwareVersion = newRole === OrbitRole.GDC ? '1.0.0-GDC' : '1.0.0';

        // Reseed sensor-bus analog boards to match the new role's hardware.
        // GDC orbits never have a sensor bus; Triton orbits expose temp +
        // pressure boards; storage uses the original temp + humidity layout.
        if (newRole !== oldRole) {
          this.state.analogBoards = defaultAnalogBoardsForRole(newRole);
        }
        
        // Start/stop GDC interval as needed
        if (newRole === OrbitRole.GDC && oldRole !== OrbitRole.GDC) {
          if (!this.gdcInterval) {
            this.gdcInterval = setInterval(() => this.updateGDCActuators(), 100);
          }
          console.log(`[Orbit ${this.state.id}] Switched to GDC mode`);
        } else if (newRole !== OrbitRole.GDC && oldRole === OrbitRole.GDC) {
          if (this.gdcInterval) {
            clearInterval(this.gdcInterval);
            this.gdcInterval = null;
          }
          console.log(`[Orbit ${this.state.id}] Switched to ${OrbitRole[newRole]} mode`);
        }

        // Start/stop Triton interval as needed (mirror of GDC handling above)
        if (newRole === OrbitRole.TRITON && oldRole !== OrbitRole.TRITON) {
          if (!this.tritonInterval) {
            this.tritonInterval = setInterval(() => this.updateTriton(), 500);
          }
          // Phase 8 — refresh DI/DO labels so the orbit panel and the
          // bridge see Triton names (Compressor / Cond Fan / EEV / …)
          // instead of stale storage / unassigned labels.
          this.refreshTritonIoLabels();
          console.log(`[Orbit ${this.state.id}] Switched to Triton mode`);
        } else if (newRole !== OrbitRole.TRITON && oldRole === OrbitRole.TRITON) {
          if (this.tritonInterval) {
            clearInterval(this.tritonInterval);
            this.tritonInterval = null;
          }
        }
        
        this.save();
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: true, role: newRole, roleName: OrbitRole[newRole] }));
      } catch {
        res.writeHead(400, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: 'Invalid JSON' }));
      }
    });
  }

  // ─── Chaos Mode (Fault Injection) API ──────────────────────────────────

  /**
   * GET /api/chaos — return current fault injection settings.
   */
  private apiGetChaos(res: http.ServerResponse): void {
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({
      enabled: this.chaosEnabled,
      dropRate: this.faultDropRate,
      corruptRate: this.faultCorruptRate,
      exceptionRate: this.faultExceptionRate,
      latencyMs: this.faultLatencyMs,
    }));
  }

  /**
   * POST /api/chaos — configure fault injection.
   * Body: { dropRate?: 0-1, corruptRate?: 0-1, exceptionRate?: 0-1, latencyMs?: number }
   * Set all to 0 to disable chaos mode.
   */
  private apiSetChaos(req: http.IncomingMessage, res: http.ServerResponse): void {
    let body = '';
    req.on('data', (chunk: Buffer) => { body += chunk.toString(); });
    req.on('end', () => {
      try {
        const cfg = JSON.parse(body);
        if (cfg.dropRate !== undefined) this.faultDropRate = Math.max(0, Math.min(1, Number(cfg.dropRate)));
        if (cfg.corruptRate !== undefined) this.faultCorruptRate = Math.max(0, Math.min(1, Number(cfg.corruptRate)));
        if (cfg.exceptionRate !== undefined) this.faultExceptionRate = Math.max(0, Math.min(1, Number(cfg.exceptionRate)));
        if (cfg.latencyMs !== undefined) this.faultLatencyMs = Math.max(0, Number(cfg.latencyMs));

        const status = this.chaosEnabled ? 'ACTIVE' : 'disabled';
        console.log(`[Orbit ${this.state.id}] Chaos mode ${status}: drop=${this.faultDropRate} corrupt=${this.faultCorruptRate} exception=${this.faultExceptionRate} latency=${this.faultLatencyMs}ms`);

        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({
          ok: true,
          enabled: this.chaosEnabled,
          dropRate: this.faultDropRate,
          corruptRate: this.faultCorruptRate,
          exceptionRate: this.faultExceptionRate,
          latencyMs: this.faultLatencyMs,
        }));
      } catch {
        res.writeHead(400, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: 'Invalid JSON' }));
      }
    });
  }
}
