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
  /** Subcooling target (°F) */
  subcoolingTarget: number;

  // ── Defrost ──
  /** Defrost mode: 0=NONE, 1=TIMED, 2=DEMAND, 3=HOT_GAS, 4=ELEC */
  defrostMode: number;
  /** Defrost stages (1..2) — GRC supports up to 2 hot-gas stages */
  defrostStages: number;
  /** Defrost interval (hours) */
  defrostIntervalHours: number;
  /** Defrost max duration (minutes) */
  defrostMaxMinutes: number;
  /** Defrost termination temperature (°F evap-out) */
  defrostTermT: number;
  /** Drip / pump-out time after defrost (seconds) */
  dripTimeSec: number;
  /** Pump down before starting defrost (0/1) */
  pumpDownBeforeDefrost: number;

  // ── PID (capacity + cond-fan + EXV) ──
  /** Capacity PID gains ×10 */
  capP: number; capI: number; capD: number; capU: number;
  /** Condenser-fan PID gains ×10 */
  condP: number; condI: number; condD: number; condU: number;

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
 *    | UNLOADER1..4 | OIL_PUMP
 *
 *  The condenser-fan stage count = (number of DOs role'd as COND_FAN).
 */
export interface TritonIoConfig {
  /** 4 analog outputs */
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

/** Digital-input port names for orbits in TRITON role.  Index = DI number-1.
 *  Order matches GRC small-comm-refrig wiring conventions. */
export const TRITON_DI_NAMES: readonly string[] = [
  'Phase Monitor',          // DI1 — closed = OK   (auto-reset, blocks start)
  'HP Switch',              // DI2 — closed = OK   (manual reset, latching)
  'LP Switch',              // DI3 — closed = OK   (auto reset, ignored in pumpdown)
  'Compressor Overload',    // DI4 — closed = OK   (manual reset, latching)
  'Cond Fan Overload',      // DI5 — closed = OK   (auto reset, alarm only)
  'Run Prove',              // DI6 — closed = proven (checked after proveSec)
  'Pumpdown Switch',        // DI7 — open  = command pumpdown
  'Auto-Run Permissive',    // DI8 — closed = enabled
  '',                       // DI9 — spare
  '',                       // DI10 — spare
];

/** Display names for `TritonIoConfig.aoMode` values (index = mode value). */
export const TRITON_AO_NAMES: readonly string[] = [
  '',          // 0 UNUSED
  'EEV',       // 1
  'Comp VFD',  // 2
  'Cond VFD',  // 3
  'Evap VFD',  // 4
];

/** Display names for `TritonIoConfig.doRole` values (index = role value). */
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
];

/** Live state of each Triton safety interlock.  Keys mirror TRITON_DI_NAMES. */
export interface TritonSafeties {
  /** Current input state — true = closed/OK, false = open/tripped */
  phaseMonitor: boolean;
  hpSwitch: boolean;
  lpSwitch: boolean;
  compOverload: boolean;
  condFanOverload: boolean;
  runProve: boolean;
  pumpdownSwitch: boolean;   // open (false) = pumpdown commanded
  autoRunPermissive: boolean;
  /** Latched lockout — must be ack'd before compressor can restart.
   *  Bit positions: 0=hpSwitch, 1=compOverload, 2=runProve. */
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
// `tritonToGrcRefrigerant()`.
type GrcPressureVec = readonly [
  number, number, number, number, number, number, number, number,
  number, number, number, number, number, number, number, number,
];

const GRC_PRESSURE_DEFAULTS: Record<number, GrcPressureVec> = {
  // GRC enum:           OFF, ON, DISC,HHP1,HHP2,HHP3,HHP4, LSA, MIN, FIX, FAN, U1,  U2, U3, U4, DOF
  /* R_22    */ 0:  [25, 55, 325, 290, 300, 310, 320, 20, 100, 200, 10,  60,  55, 50, 45, 250],
  /* R_410A  */ 1:  [50, 95, 519, 482, 492, 502, 512, 20, 169, 326, 14, 105, 100, 95, 90, 401],
  /* R_407C  */ 2:  [25, 55, 336, 300, 310, 320, 330, 20,  96, 202, 10,  54,  49, 44, 39, 254],
  /* R_134A  */ 3:  [22, 30, 225, 192, 202, 212, 222, 20,  57, 128,  7,  29,  24, 19, 14, 164],
  /* R_404A  */ 4:  [25, 55, 400, 358, 368, 378, 388, 20, 123, 241, 10,  74,  69, 64, 59, 298],
  /* R_507   */ 5:  [25, 55, 400, 360, 370, 380, 390, 20, 130, 251, 10,  80,  75, 70, 65, 310],
  /* R_407A  */ 6:  [25, 55, 360, 327, 337, 347, 357, 20, 100, 205, 10,  50,  45, 40, 35, 274],
};

/** Temperature → saturated-vapor pressure cubic coefficients [a,b,c,d]
 *  for `P(t) = a*t^3 + b*t^2 + c*t + d`, with `t` in °F and `P` in PSI.
 *  Lifted verbatim from GRC `Refrigerant.c` `TempToPressureCoefficients`.
 *  Indexed by GRC refrigerant enum (R22=0, R410A=1, R407C=2, R134A=3,
 *  R404A=4, R507=5, R407A=6). */
const GRC_TEMP_TO_P_COEFF: Record<number, readonly [number, number, number, number]> = {
  /* R_22    */ 0: [2.87064e-5, 6.104444e-3, 0.821491477, 24.04855744],
  /* R_410A  */ 1: [4.54707e-5, 9.520674e-3, 1.278976335, 48.66722633],
  /* R_407C  */ 2: [3.49531e-5, 6.717706e-3, 0.940156102, 29.99215385],
  /* R_134A  */ 3: [2.56407e-5, 4.204094e-3, 0.500317987,  6.628019008],
  /* R_404A  */ 4: [3.39324e-5, 7.142816e-3, 0.968937731, 32.11551756],
  /* R_507   */ 5: [4.35828e-5, 5.933340e-3, 1.053624172, 35.44995267],
  /* R_407A  */ 6: [3.57635e-5, 6.973933e-3, 0.997833893, 32.30318463],
};

/** Saturation pressure (PSI) for a given Triton refrigerant index at
 *  `tempF` °F.  Mirrors GRC `Refrigerant.c CalculatePressure()`.  Falls
 *  back to R-404A when the Triton refrigerant has no GRC analog. */
function psatF(tritonType: number, tempF: number): number {
  const grc = tritonToGrcRefrigerant(tritonType) ?? 4;
  const c = GRC_TEMP_TO_P_COEFF[grc] ?? GRC_TEMP_TO_P_COEFF[4];
  const t  = tempF;
  const t2 = t * t;
  return c[0] * t2 * t + c[1] * t2 + c[2] * t + c[3];
}

/** Triton refrigerantType enum value → GRC refrigerant index.
 *  Returns null when the Triton refrigerant has no GRC entry (we then fall
 *  back to R-404A as the closest HFC analog). */
function tritonToGrcRefrigerant(triton: number): number | null {
  switch (triton) {
    case 0:  return 0; // R22
    case 1:  return 3; // R134A
    case 2:  return 4; // R404A
    case 3:  return 6; // R407A
    case 4:  return 2; // R407C
    case 5:  return 1; // R410A
    // 6=R448A, 7=R449A, 8=R450A, 9=R454A, 10=R513A, 11=R600A, 12=R744,
    // 13=R32, 14=R454B, 15=R454C, 16=R455A, 17=R1234yf, 18=R1234ze(E),
    // 19=R466A, 20=R515B — no GRC data; use closest analog via fallback.
    default: return null;
  }
}

/** Returns the GRC pressure-default vector for a Triton refrigerantType,
 *  falling back to R-404A for refrigerants the GRC firmware never shipped. */
function getGrcPressureDefaults(tritonType: number): GrcPressureVec {
  const grc = tritonToGrcRefrigerant(tritonType);
  if (grc != null && GRC_PRESSURE_DEFAULTS[grc]) return GRC_PRESSURE_DEFAULTS[grc];
  return GRC_PRESSURE_DEFAULTS[4]; // R-404A fallback
}

/**
 * Re-seed all pressure-derived setpoints from the GRC PressureDefaults table
 * for the given refrigerant.  Mirrors GRC `SetRefrigerationSettings()` in
 * Settings.c.  Called whenever the refrigerant type changes and once at
 * Triton creation.  Returns the same setpoints reference for chaining.
 */
function applyRefrigerantDefaults(sp: TritonSetpoints, tritonType: number): TritonSetpoints {
  const v = getGrcPressureDefaults(tritonType);
  sp.refrigerantType   = tritonType;
  sp.cutOutP           = v[0];
  sp.cutInP            = v[1];
  sp.discHighUnloadP   = v[2];
  sp.sucLowUnloadP     = v[7];      // LOW_SUCTION_ALARM
  // Floating-head cond-fan setpoint defaults to GRC FIXED_CONTROL value
  sp.condFanVfdSetpointP = v[9];
  // Floating-head clamps — `MinimumSetpoint` floor and P_sat(105 °F) ceiling,
  // exactly matching GRC `TargetDischarge()`.
  sp.condMinHeadP = v[8];
  sp.condMaxHeadP = Math.round(psatF(tritonType, 105));
  // Per-stage cond-fan staging: GRC uses (n+1)*FAN_DIFFERENTIAL above setpoint,
  // off = on - 2*FAN_DIFFERENTIAL.  We pre-compute six absolute thresholds
  // (PSI) anchored at FIXED_CONTROL so the staged-relay logic stays simple.
  const fixed = v[9];
  const diff  = v[10];
  for (let s = 0; s < 6; s++) {
    sp.fanStageOnP[s]  = fixed + (s + 1) * diff;
    sp.fanStageOffP[s] = fixed + (s + 1) * diff - 2 * diff;
    // Differential-mode equivalents (offsets above the floating target).
    // ON  = (s+1) * FAN_DIFFERENTIAL  (e.g. 10/20/30/40/50/60 for R-404A)
    // OFF = 5 PSI for every stage     (matches GRC default `fanXoff`=5)
    sp.fanDiffOnP[s]  = (s + 1) * diff;
    sp.fanDiffOffP[s] = 5;
  }
  return sp;
}

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
    dischargeTargetP: 241,         // R-404A FIXED_CONTROL default
    evapFanOn: true,
    exvOpenPct: 0,
    defrostActive: false,
    lastDefrostEnd: 0,
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
      subcoolingTarget: 8.0,
      // Defrost (GRC Settings.Defrost)
      defrostMode:            1,    // TIMED  (GRC TerminationType=TIME)
      defrostStages:          1,
      defrostIntervalHours:   6,    // GRC Defrost.OverrideTimer
      defrostMaxMinutes:     10,    // GRC Defrost.TerminationTime
      defrostTermT:          55.0,
      dripTimeSec:          120,
      pumpDownBeforeDefrost:  1,
      // PID (capacity + cond-fan)  — GRC CondenserFan.PID was 5/15/2/3
      capP: 5.0, capI: 1.5, capD: 0.2, capU: 3.0,
      condP: 5.0, condI: 1.5, condD: 0.2, condU: 3.0,
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
      // Default AO mapping: ch0=EEV, ch1=COMP_VFD, ch2/3=UNUSED
      aoMode: [1, 2, 0, 0],
      // Default DO mapping: 0=COMP, 1=EVAP_FAN, 2..5=COND_FAN×4,
      //                     6=DEFROST, 7=LIQ_SOL, 8/9=UNUSED
      doRole: [1, 3, 2, 2, 2, 2, 4, 5, 0, 0],
    },
    compressorStatus: 0,        // AUTO_STANDBY
    totalRuntimeHours: 0,
    dailyRuntimeHours: 0,
    alarms: [],
    safeties: {
      phaseMonitor: true,
      hpSwitch: true,
      lpSwitch: true,
      compOverload: true,
      condFanOverload: true,
      runProve: true,
      pumpdownSwitch: true,        // closed = no pumpdown command
      autoRunPermissive: true,
      lockoutMask: 0,
    },
    leakDetect: {
      shAvgF: 0,
      exvAvgPct: 0,
      sustainedSec: 0,
      leakAlarmActive: false,
      warmupSec: 0,
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
              aoMode: Array.isArray(t.ioConfig?.aoMode) && t.ioConfig.aoMode.length === 4
                ? t.ioConfig.aoMode : defaults.ioConfig.aoMode,
              doRole: Array.isArray(t.ioConfig?.doRole) && t.ioConfig.doRole.length === 10
                ? t.ioConfig.doRole : defaults.ioConfig.doRole,
            },
        alarms: Array.isArray(t.alarms) ? t.alarms : [],
        safeties: need('safeties')
          ? defaults.safeties
          : { ...defaults.safeties, ...t.safeties },
        leakDetect: (t as any).leakDetect && typeof (t as any).leakDetect === 'object'
          ? { ...defaults.leakDetect, ...(t as any).leakDetect }
          : defaults.leakDetect,
        failureStates: (t as any).failureStates && typeof (t as any).failureStates === 'object'
          ? { ...defaults.failureStates!, ...(t as any).failureStates }
          : defaults.failureStates,
        safeOffMask: typeof (t as any).safeOffMask === 'number' ? (t as any).safeOffMask : 0,
        lastTickMs:  typeof (t as any).lastTickMs  === 'number' ? (t as any).lastTickMs  : 0,
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

    // ── VFD Bus: connect to VFD drives via RTU if configured ──
    const vfdRtuHost = process.env.VFD_RTU_HOST ?? 'localhost';
    const vfdRtuPort = parseInt(process.env.VFD_RTU_PORT ?? '0', 10);
    if (vfdRtuPort > 0) {
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
    if (addr >= 450 && addr <= 453) return u16(t.ioConfig.aoMode[addr - 450] ?? 0);
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
    if (addr >= 450 && addr <= 453) {
      t.ioConfig.aoMode[addr - 450] = clamp(value, 0, 4);
      this.refreshTritonIoLabels();
      return;
    }
    if (addr >= 454 && addr <= 463) {
      t.ioConfig.doRole[addr - 454] = clamp(value, 0, 10);
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
        // Orbit registers: temp/humid = ×10, CO2 = raw ppm
        // Sensor board sim: engineering values (°C, %RH, ppm)
        const engValue = (st === SENSOR_TYPE_CO2) ? Number(value) : Number(value) / 10;
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
    const dt = 0.5; // seconds per tick — matches setInterval(updateTriton, 500)

    // 0. Read safety inputs from DI 1-8 and evaluate interlocks.
    //    Sets t.safeties.lockoutMask for latching faults; returns true if
    //    the compressor is permitted to run this tick.
    const di = this.state.digitalInputs;
    const sf = t.safeties;
    sf.phaseMonitor      = !!di[0];
    sf.hpSwitch          = !!di[1];
    sf.lpSwitch          = !!di[2];
    sf.compOverload      = !!di[3];
    sf.condFanOverload   = !!di[4];
    sf.runProve          = !!di[5];
    sf.pumpdownSwitch    = !!di[6];
    sf.autoRunPermissive = !!di[7];

    // Latching faults (manual reset): set bit when the input goes open
    // and the compressor is/was running OR the fault is present at all.
    if (!sf.hpSwitch)     sf.lockoutMask |= 0x01;
    if (!sf.compOverload) sf.lockoutMask |= 0x02;
    // Run-prove latch is set in the runtime check below (only meaningful
    // after proveSec into a run).

    // Pumpdown is in progress when the switch is open AND we have a
    // pump-down before stop configured.  Used to bypass LP switch.
    const pumpdownActive = !sf.pumpdownSwitch && t.compressorOn;

    // Hard interlocks — any of these tripped → compressor not permitted.
    const hardTrip =
         !sf.phaseMonitor
      || !sf.autoRunPermissive
      || (!sf.lpSwitch && !pumpdownActive)
      || (sf.lockoutMask !== 0)
      || ((t.safeOffMask ?? 0) !== 0);

    // Latch alarms for each safety state.  These use the existing
    // alarm machinery so they show up alongside sensor alarms.
    const latchAlarm = (code: string, label: string, cond: boolean) => {
      const ex = t.alarms.find(a => a.code === code);
      if (cond) {
        if (ex) ex.active = true;
        else t.alarms.push({ code, label, active: true, acked: false, timestamp: Date.now() });
      } else if (ex) {
        ex.active = false;
        if (ex.acked) t.alarms = t.alarms.filter(a => a !== ex);
      }
    };
    latchAlarm('SAF_PHASE',     'Phase Monitor Trip',          !sf.phaseMonitor);
    latchAlarm('SAF_HP',        'High-Pressure Switch Open',   !sf.hpSwitch || (sf.lockoutMask & 0x01) !== 0);
    latchAlarm('SAF_LP',        'Low-Pressure Switch Open',    !sf.lpSwitch && !pumpdownActive);
    latchAlarm('SAF_COMP_OL',   'Compressor Overload',         !sf.compOverload || (sf.lockoutMask & 0x02) !== 0);
    latchAlarm('SAF_COND_OL',   'Cond Fan Overload',           !sf.condFanOverload);
    latchAlarm('SAF_PERMIT',    'Auto-Run Permissive Open',    !sf.autoRunPermissive);

    // 1. Decide whether the compressor should be running this tick.
    let shouldRun = t.compressorOn;
    if (hardTrip) {
      shouldRun = false;
      if (t.compressorOn) t.compressorStatus = 10; // ERROR
    } else if (t.manualMode === 'force-on') {
      shouldRun = true;
    } else if (t.manualMode === 'force-off' || !t.enabled) {
      shouldRun = false;
    } else {
      // Auto: simple bang-bang on suction PSI
      if (t.compressorOn && t.sensors.suctionP.value <= t.setpoints.cutOutP) {
        shouldRun = false;
      } else if (!t.compressorOn && t.sensors.suctionP.value >= t.setpoints.cutInP) {
        // Honor minimum off time before restart
        const offDur = (Date.now() - t.lastDefrostEnd) / 1000;
        if (offDur >= t.setpoints.minOffTime) shouldRun = true;
      }
    }
    if (shouldRun !== t.compressorOn) {
      t.compressorOn = shouldRun;
      if (!shouldRun) t.lastDefrostEnd = Date.now();
    }

    // 2. Advance runtime / amps.
    if (t.compressorOn) {
      t.compressorRuntimeSec += dt;
      // Realistic-ish RLA: 12-18 amps under typical load
      t.compressorAmps = 14.0 + (t.sensors.dischargeP.value - 150) * 0.02;
      // Run-prove: after proveSec, the proof contact (DI6) MUST be closed.
      // If it opens after the prove window, latch lockout bit 2.
      if (t.compressorRuntimeSec >= t.setpoints.proveSec && !sf.runProve) {
        sf.lockoutMask |= 0x04;
        latchAlarm('SAF_PROVE', 'Run Prove Failure', true);
      }
    } else {
      t.compressorAmps = Math.max(0, t.compressorAmps - dt * 5);
      // Reset the runtime counter when stopped so prove window restarts cleanly.
      t.compressorRuntimeSec = 0;
    }
    // Always evaluate the run-prove alarm so it clears once the lockout is reset.
    if ((sf.lockoutMask & 0x04) === 0) latchAlarm('SAF_PROVE', 'Run Prove Failure', false);

    // 3. Drive the sensors toward equilibrium values that depend on state.
    //    When idle, suction PSI rises with the heat load on the box — model
    //    that by drifting toward (cutInP + 5) so the bang-bang cycle
    //    naturally restarts the compressor without operator intervention.
    const ambient = t.sensors.ambientT.value;
    const offSuctionTarget = Math.max(t.setpoints.cutInP + 5, 25);
    const target = {
      suctionP:   t.compressorOn ? Math.max(t.setpoints.cutOutP - 5, 25) : offSuctionTarget,
      suctionT:   t.compressorOn ? 25 : ambient - 5,
      dischargeP: t.compressorOn ? Math.min(140 + (ambient - 70) * 2.5, 350) : ambient + 30,
      dischargeT: t.compressorOn ? 130 + (ambient - 70) : ambient + 5,
      llsT:       t.compressorOn ? Math.min(95 + (ambient - 70) * 0.3, 130) : ambient,
      oilP:       t.compressorOn ? 55 + (t.sensors.dischargeP.value - 150) * 0.05 : 0,
      ambientT:   ambient, // free-running, set by user / chaos
    };
    const tau = 0.05; // exponential approach rate (per tick)
    const lerp = (a: number, b: number) => a + (b - a) * tau;
    t.sensors.suctionP.value   = lerp(t.sensors.suctionP.value,   target.suctionP);
    t.sensors.suctionT.value   = lerp(t.sensors.suctionT.value,   target.suctionT);
    t.sensors.dischargeP.value = lerp(t.sensors.dischargeP.value, target.dischargeP);
    t.sensors.dischargeT.value = lerp(t.sensors.dischargeT.value, target.dischargeT);
    t.sensors.llsT.value       = lerp(t.sensors.llsT.value,       target.llsT);
    t.sensors.oilP.value       = lerp(t.sensors.oilP.value,       target.oilP);

    // 3b. Mirror live values onto the sensor-bus analog boards so polls of
    //     /api/sensors reflect the same physics as /api/triton.  Board 0 is
    //     temperature (suction T, discharge T, LLS T, ambient T) and board 1
    //     is pressure (suction P, discharge P, oil P, demand).  All values
    //     are stored as int *10 to match the existing storage convention,
    //     except channel 4 of the pressure board which carries the raw 0-100
    //     demand percentage.
    const tempBoard = this.state.analogBoards[0];
    if (tempBoard && tempBoard.type === SENSOR_TYPE_TEMP) {
      tempBoard.sensorValues[0] = Math.round(t.sensors.suctionT.value   * 10);
      tempBoard.sensorValues[1] = Math.round(t.sensors.dischargeT.value * 10);
      tempBoard.sensorValues[2] = Math.round(t.sensors.llsT.value       * 10);
      tempBoard.sensorValues[3] = Math.round(t.sensors.ambientT.value   * 10);
    }
    const pressBoard = this.state.analogBoards[1];
    if (pressBoard && pressBoard.type === SENSOR_TYPE_PRESSURE) {
      pressBoard.sensorValues[0] = Math.round(t.sensors.suctionP.value   * 10);
      pressBoard.sensorValues[1] = Math.round(t.sensors.dischargeP.value * 10);
      pressBoard.sensorValues[2] = Math.round(t.sensors.oilP.value       * 10);
      pressBoard.sensorValues[3] = Math.round(t.demand);
    }

    // 4. EXV modulation — drive open-pct so suctionT - saturationT(suctionP)
    //    approaches superheatTarget.  We approximate the saturation temp as
    //    a linear function of pressure for the simulator (real R-404A would
    //    use a P-T table).  Open more EXV → more refrigerant → less SH.
    const satT = 0.55 * t.sensors.suctionP.value - 35;     // crude °F
    const superheat = t.sensors.suctionT.value - satT;
    if (t.compressorOn) {
      const err = superheat - t.setpoints.superheatTarget;
      t.exvOpenPct = Math.max(5, Math.min(95, t.exvOpenPct + err * 0.5));
    } else {
      t.exvOpenPct = Math.max(0, t.exvOpenPct - 1);
    }

    // 4b. Leak / low-charge detection.
    //     A refrigerant leak shows up as the EXV opening further and further
    //     while superheat refuses to come down — there is simply not enough
    //     liquid to feed the evaporator.  We sample the EWMA of both signals
    //     once the compressor has been running for a stabilisation window
    //     (2 × warmUpSec, default 120 s); when the filtered superheat is
    //     above target+margin AND the filtered EXV is at/above the open
    //     threshold for `leakSustainMinutes` continuously, we latch a
    //     LEAK_SUSP alarm.  Conditions reset on compressor stop or defrost.
    const ld = t.leakDetect;
    if (!t.compressorOn || t.defrostActive) {
      ld.warmupSec    = 0;
      ld.sustainedSec = 0;
    } else {
      ld.warmupSec += dt;
    }
    if (t.setpoints.leakDetectEnabled && t.compressorOn && !t.defrostActive) {
      // EWMA with τ ≈ 60 s (α = dt/τ = 0.5/60 ≈ 0.0083).
      const alpha = dt / 60;
      // Seed the filters on the first sample of a new run.
      if (ld.warmupSec <= dt) {
        ld.shAvgF    = superheat;
        ld.exvAvgPct = t.exvOpenPct;
      } else {
        ld.shAvgF    += alpha * (superheat        - ld.shAvgF);
        ld.exvAvgPct += alpha * (t.exvOpenPct     - ld.exvAvgPct);
      }
      // Only credit sustain time once past the stabilisation gate.
      const stableAfter = Math.max(60, t.setpoints.warmUpSec * 2);
      if (ld.warmupSec >= stableAfter) {
        const shHigh  = ld.shAvgF    >= t.setpoints.superheatTarget + t.setpoints.leakSuperheatMarginF;
        const exvHigh = ld.exvAvgPct >= t.setpoints.leakExvOpenPct;
        if (shHigh && exvHigh) ld.sustainedSec += dt;
        else                   ld.sustainedSec  = 0;
        if (ld.sustainedSec >= t.setpoints.leakSustainMinutes * 60) {
          ld.leakAlarmActive = true;
        }
      }
    } else {
      // Detector disabled — keep the live filters frozen but never alarm.
      ld.sustainedSec    = 0;
      ld.leakAlarmActive = false;
    }

    // 5. Condenser fan staging.  Mirrors GRC `StageCondenserFans()` in
    //    CondenserFans.c — first compute the head-pressure target via
    //    `TargetDischarge()` (mode-aware: FIXED uses operator setpoint;
    //    FLOATING/BALANCED uses `P_sat(OAT + approach)` clamped to
    //    [condMinHeadP, condMaxHeadP]), then stage each fan with
    //    on = target + diffOn[i], off = target + diffOff[i].  When the
    //    operator picks FIXED — or the OAT sensor is invalid — we fall
    //    back to the absolute thresholds in `fanStageOnP/OffP`.
    const condSp = t.setpoints;
    let headTarget: number;
    let useDiffs = false;
    const oatValid = t.sensors.ambientT.valid;
    if (condSp.condenserMode === 0 || !oatValid) {
      headTarget = condSp.condFanVfdSetpointP;
    } else {
      const psat = psatF(condSp.refrigerantType, t.sensors.ambientT.value + condSp.condApproachF);
      let p = psat;
      if (condSp.condMinHeadP > 0 && p < condSp.condMinHeadP) p = condSp.condMinHeadP;
      if (condSp.condMaxHeadP > 0 && p > condSp.condMaxHeadP) p = condSp.condMaxHeadP;
      headTarget = p;
      useDiffs = true;
    }
    t.dischargeTargetP = Math.round(headTarget * 10) / 10;
    let stage = t.condenserFanStage;
    for (let i = 0; i < t.condenserFanCount; i++) {
      const onP  = useDiffs
        ? headTarget + (condSp.fanDiffOnP[i]  ?? 999)
        : (condSp.fanStageOnP[i]  ?? 999);
      const offP = useDiffs
        ? headTarget + (condSp.fanDiffOffP[i] ?? 0)
        : (condSp.fanStageOffP[i] ?? 0);
      if (stage <= i && t.sensors.dischargeP.value >= onP) stage = i + 1;
      else if (stage > i && t.sensors.dischargeP.value <= offP) stage = i;
    }
    t.condenserFanStage = Math.min(stage, t.condenserFanCount);

    // 6. Latch alarms when a sensor crosses its threshold.  Acked alarms
    //    stay in the list until they go inactive (operator acknowledges
    //    before clearing); inactive+acked alarms are pruned.
    const maybeAlarm = (code: string, label: string, condition: boolean) => {
      const existing = t.alarms.find(a => a.code === code);
      if (condition) {
        if (existing) {
          existing.active = true;
        } else {
          t.alarms.push({ code, label, active: true, acked: false, timestamp: Date.now() });
        }
      } else if (existing) {
        existing.active = false;
        if (existing.acked) {
          t.alarms = t.alarms.filter(a => a !== existing);
        }
      }
    };
    maybeAlarm('SUC_LOW_P',  'Suction Pressure Low',  t.sensors.suctionP.value   < t.sensors.suctionP.lowAlarm);
    maybeAlarm('SUC_HIGH_P', 'Suction Pressure High', t.sensors.suctionP.value   > t.sensors.suctionP.highAlarm);
    maybeAlarm('DIS_HIGH_P', 'Discharge Pressure High', t.sensors.dischargeP.value > t.sensors.dischargeP.highAlarm);
    maybeAlarm('DIS_HIGH_T', 'Discharge Temp High',     t.sensors.dischargeT.value > t.sensors.dischargeT.highAlarm);
    maybeAlarm('OIL_LOW_P',  'Oil Pressure Low',        t.compressorOn && t.sensors.oilP.value < t.sensors.oilP.lowAlarm);
    // Leak / low-charge — driven by the EWMA detector above.  When the
    // operator acks the alarm and we want to give the detector a fresh
    // chance, we also clear the latch so a renewed leak signature can
    // re-trigger after the same sustain window.
    maybeAlarm('LEAK_SUSP',  'Refrigerant Leak Suspected (high SH + EXV wide open)',
               t.leakDetect.leakAlarmActive);
    {
      const ackedLeak = t.alarms.find(a => a.code === 'LEAK_SUSP' && a.acked);
      if (ackedLeak && !ackedLeak.active) {
        t.leakDetect.leakAlarmActive = false;
        t.leakDetect.sustainedSec    = 0;
      }
    }

    // 7. GRC FAILURE machinery — timed thresholds with mode-respecting
    //    actions.  Each `t.failures.*` channel has a `delaySec` and a
    //    `mode` (0=ALARM_ONLY, 1=SAFE_OFF, 2=RUN_THROUGH).  We integrate
    //    seconds-above-threshold per channel; when the timer expires we
    //    latch the corresponding FAIL_* alarm and, for SAFE_OFF mode,
    //    set a bit in `safeOffMask` that participates in `hardTrip` next
    //    tick.  Latches and timers all clear via the ack-all command.
    if (!t.failureStates) {
      t.failureStates = {
        suctionP:     { overSec: 0, tripped: false },
        dischargeP:   { overSec: 0, tripped: false },
        oilP:         { overSec: 0, tripped: false },
        suctionT:     { overSec: 0, tripped: false },
        superheatLow: { overSec: 0, tripped: false },
        dischargeT:   { overSec: 0, tripped: false },
        llsT:         { overSec: 0, tripped: false },
        ambientT:     { overSec: 0, tripped: false },
      };
    }
    if (typeof t.safeOffMask !== 'number') t.safeOffMask = 0;

    const fs = t.failureStates;
    const evalFail = (
      code: string, label: string,
      cfg: TritonFailureMode, st: TritonFailureState,
      cond: boolean,
    ) => {
      // RUN_THROUGH (2) means: alarm only, never set safeOff bit.  ALARM_ONLY
      // (0) — same as RUN_THROUGH for the simulator's purposes.  SAFE_OFF
      // (1) — also engages the safeOffMask once tripped.
      if (cond) st.overSec += dt;
      else      st.overSec  = 0;

      if (!st.tripped && st.overSec >= Math.max(0, cfg.delaySec)) {
        st.tripped = true;
      }
      const bit = TRITON_ALARM_BITS[code] ?? -1;
      if (st.tripped && cfg.mode === 1 && bit >= 0) {
        t.safeOffMask = (t.safeOffMask ?? 0) | (1 << bit);
      }
      maybeAlarm(code, label, st.tripped);
    };

    // FAIL_SUPERHEAT — superheat above target+window for delaySec while running
    evalFail('FAIL_SUPERHEAT', 'Superheat High (timed)',
      t.failures.suctionT, fs.suctionT,
      t.compressorOn && !t.defrostActive
        && superheat > t.setpoints.superheatTarget + t.setpoints.superheatWindowHighF);
    // FAIL_SUPERHEATLOW — superheat below the floodback floor
    evalFail('FAIL_SUPERHEATLOW', 'Superheat Low (floodback risk)',
      t.failures.suctionT, fs.superheatLow,
      t.compressorOn && !t.defrostActive
        && superheat < t.setpoints.superheatLowF);
    // FAIL_DISCHARGE — discharge pressure over high alarm for delaySec
    evalFail('FAIL_DISCHARGE', 'Discharge Pressure (sustained over)',
      t.failures.dischargeP, fs.dischargeP,
      t.sensors.dischargeP.value > t.sensors.dischargeP.highAlarm);
    // FAIL_SUCTION — suction pressure out of range for delaySec
    evalFail('FAIL_SUCTION', 'Suction Pressure (sustained out of range)',
      t.failures.suctionP, fs.suctionP,
      t.sensors.suctionP.value < t.sensors.suctionP.lowAlarm
        || t.sensors.suctionP.value > t.sensors.suctionP.highAlarm);
    // FAIL_OIL — oil low for delaySec while compressor running
    evalFail('FAIL_OIL', 'Oil Pressure (sustained low)',
      t.failures.oilP, fs.oilP,
      t.compressorOn && t.sensors.oilP.value < t.sensors.oilP.lowAlarm);
    // FAIL_OUTSIDE_AIR — ambient below configured cutout for delaySec
    evalFail('FAIL_OUTSIDE_AIR', 'Outside Air Below Cutout',
      t.failures.ambientT, fs.ambientT,
      t.sensors.ambientT.valid
        && t.sensors.ambientT.value < t.setpoints.lowAmbientCutoutF);

    // 7b. Power-fail detector — gap between updateTriton ticks larger than
    //     `powerFailMinutes` indicates the box was off long enough to need
    //     the crankcase prove.  Latches FAIL_POWER until ack-all.
    const nowMs = Date.now();
    const lastMs = t.lastTickMs ?? 0;
    if (lastMs > 0) {
      const gapMin = (nowMs - lastMs) / 60000;
      if (gapMin > Math.max(1, t.setpoints.powerFailMinutes)) {
        const bit = TRITON_ALARM_BITS.FAIL_POWER;
        // Power-fail is treated as ALARM_ONLY in the simulator (no
        // safeOff) — Nova decides whether to enforce the prove.
        const ex = t.alarms.find(a => a.code === 'FAIL_POWER');
        if (ex) ex.active = true;
        else t.alarms.push({
          code: 'FAIL_POWER', label: 'Power-Fail (gap exceeded)',
          active: true, acked: false, timestamp: nowMs,
        });
        // Mark on safeOffMask only if Nova / operator wants to enforce it
        // by setting the failureModes mode for ambientT to SAFE_OFF; here
        // we keep it as a plain alarm.
        void bit;
      }
    }
    t.lastTickMs = nowMs;

    // 7c. Sensor-validity faults (transducer disconnected / out of range).
    //     Each maps 1:1 to a SENS_FAULT_* code.  These don't go through
    //     the timer — a single-tick invalid reading raises the alarm.
    maybeAlarm('SENS_FAULT_SUC_P', 'Suction-P Sensor Fault',  !t.sensors.suctionP.valid);
    maybeAlarm('SENS_FAULT_DIS_P', 'Discharge-P Sensor Fault', !t.sensors.dischargeP.valid);
    maybeAlarm('SENS_FAULT_OIL_P', 'Oil-P Sensor Fault',       !t.sensors.oilP.valid);
    maybeAlarm('SENS_FAULT_SUC_T', 'Suction-T Sensor Fault',   !t.sensors.suctionT.valid);
    maybeAlarm('SENS_FAULT_DIS_T', 'Discharge-T Sensor Fault', !t.sensors.dischargeT.valid);
    maybeAlarm('SENS_FAULT_LLS_T', 'LLS-T Sensor Fault',       !t.sensors.llsT.valid);
    maybeAlarm('SENS_FAULT_AMB_T', 'Ambient-T Sensor Fault',   !t.sensors.ambientT.valid);

    // 7d. Sensor-board missing — analog board[0] is temp, board[1] is press.
    const tempBd  = this.state.analogBoards[0];
    const pressBd = this.state.analogBoards[1];
    maybeAlarm('BOARD_MISSING_TEMP',  'Temperature Board Missing',
               !tempBd  || !tempBd.present);
    maybeAlarm('BOARD_MISSING_PRESS', 'Pressure Board Missing',
               !pressBd || !pressBd.present);

    // 7e. NO_DISCHARGE — compressor has been running past the prove window
    //     but discharge pressure is essentially the same as suction (no
    //     compression happening).  Simple delta check.
    maybeAlarm('NO_DISCHARGE', 'No Discharge Pressure Rise',
      t.compressorOn
        && t.compressorRuntimeSec > t.setpoints.proveSec
        && t.sensors.dischargeP.value < t.sensors.suctionP.value + 20);

    // 7f. BAD_OIL_SENSOR — value pinned out-of-range while running.
    maybeAlarm('BAD_OIL_SENSOR', 'Oil Sensor Out of Range',
      t.compressorOn
        && (t.sensors.oilP.value < -5 || t.sensors.oilP.value > 500));
  }

  /** GET /api/triton — full Triton state snapshot (for SCADA rendering) */
  private apiGetTriton(res: http.ServerResponse): void {
    if (this.state.role !== OrbitRole.TRITON) {
      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ ok: true, present: false }));
      return;
    }
    const t = this.state.triton;
    // Derived helpers the UI commonly wants
    const satT = 0.55 * t.sensors.suctionP.value - 35;
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
      },
      evapFanOn: t.evapFanOn,
      exvOpenPct: Number(t.exvOpenPct.toFixed(1)),
      defrostActive: t.defrostActive,
      lastDefrostEnd: t.lastDefrostEnd,
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
      alarms: t.alarms,
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
        const { mode } = JSON.parse(body);
        if (!['auto', 'force-on', 'force-off'].includes(mode)) {
          res.writeHead(400, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ ok: false, error: 'invalid mode' }));
          return;
        }
        this.state.triton.manualMode = mode;
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: true, mode }));
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
