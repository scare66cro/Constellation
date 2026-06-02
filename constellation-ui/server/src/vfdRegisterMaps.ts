/**
 * vfdRegisterMaps.ts — Vendor-agnostic VFD / Modbus-device profile registry.
 *
 * Phase 4b Sub-3 architecture pivot (2026-06-02): vendor knowledge lives
 * here (on the bridge, in TypeScript — free to grow without firmware
 * reflash). The STORAGE orbit is a configurable Modbus-RTU poll scheduler
 * + write router with NO vendor awareness. At bridge startup, vfdClient
 * walks operator-configured (unit_id ↔ manufacturer) pairs, builds a
 * `VfdPollConfig` from the matching profile's `pollEntries`, and ships it
 * via the new envelope (tag 127). The orbit then:
 *
 *   1. Polls each entry at the requested rate via the right Modbus FC
 *   2. Stashes results in its 48-reg HR cache window (HR 100..147)
 *   3. Routes bridge HR writes (OrbitRegWrite) back out over RS485 per
 *      each entry's `nativeAddr` + `fc`
 *
 * Adding a new VFD vendor or a Modbus-driven contactor = add a profile
 * entry below. The AM2434 orbit binary footprint stays constant.
 *
 * See docs/orbit-vfd-poll-scheduler.md for the wire contract.
 */

import type { VfdPollEntryWire } from './novaSerialBridge.js';

export type VFDManufacturer =
  | 'abb-acs310'
  | 'abb-acs380'
  | 'phase-tech-lhap'      // LH AquaPhase series — pump VFD, PID-pressure target
  | 'phase-tech-lhv'       // LH-V series — same protocol, more baud rates + N≤30 block-read cap
  | 'phase-tech-dxl'       // Legacy retained for back-compat with prior config files
  | 'modbus-contactor'     // Bare Modbus relay (FC1/FC5 coil)
  | 'generic';

// ── Modbus FC encoding on the wire ───────────────────────────────────
// Low byte = primary FC. High byte = sub-FC for vendor-specific custom
// function codes (so the orbit's RTU master can dispatch 0x4201 → FC 66
// sub-fn 1 → Phase Tech "Set HOA Mode", etc.).
export const FC = {
  READ_COIL:        0x01,
  READ_DI:          0x02,
  READ_HR:          0x03,
  READ_IR:          0x04,
  WRITE_COIL:       0x05,
  WRITE_SINGLE_HR:  0x06,
  WRITE_MULTI_COILS:0x0F,
  WRITE_MULTI_HRS:  0x10,

  // Phase Tech custom FC 66 (0x42) sub-functions.
  PHASE_SET_HOA:    0x4201,  // 0x00 Off / 0x0F Manual / 0xF0 Auto
  PHASE_SET_RUN:    0x4202,  // 0x00 Stop / 0x0F Run (Manual-mode only)
  PHASE_RESET:      0x4203,  // Data byte must be 0xA5
} as const;

/** Per-cache-slot decode rule. The orbit cache holds raw u16 per slot;
 *  this tells vfdClient.ts how to surface that into the UI snapshot. */
export interface VfdSlotDef {
  /** Canonical snapshot field name (matches keys of VFDDriveSnapshot). */
  field: string;
  /** Linear scale applied to the raw u16. e.g. 0.1 for "×10" wire encoding,
   *  0.01 for "×100". Default 1. */
  scale?: number;
  /** Optional custom decoder for non-linear encodings — e.g. Phase Tech
   *  HOA-mode byte (0x00 / 0x0F / 0xF0) → enum (0 Off / 1 Manual / 2 Auto). */
  decode?: (raw: number) => number;
}

/** Schedule template — the per-drive `unitId` is filled in at config-build
 *  time. Everything else is profile-static. */
export interface VfdPollTemplate {
  cacheSlot: number;     // 0..47 (orbit's HR 100..147 cache index)
  nativeAddr: number;    // Modbus-standard register address (1-based per vendor doc)
  fc: number;            // see FC constants above
  pollRateMs: number;    // 0 = on-demand only (write target, no periodic read)
  writable: boolean;     // bridge HR writes to this cache_slot fan out to RS485
}

/** One step of an operator action's translation to cache_slot writes.
 *  Multi-step actions (e.g. Phase Tech start = HOA-Manual then Run) issue
 *  these in order, optionally delaying between. */
export interface VfdActionStep {
  cacheSlot: number;
  values: number[];           // 1 entry → FC06; ≥2 → FC16; or vendor-custom byte for FC 66
  delayAfterMs?: number;      // pause before the next step (e.g. 500 ms for ABB reset)
}

export interface VfdProfile {
  manufacturer: VFDManufacturer;
  label: string;

  /** Schedule entries — bridge composes a `VfdPollConfig` by injecting
   *  the per-drive `unitId` into each. */
  pollEntries: VfdPollTemplate[];

  /** Per-cache-slot decoder. Sparse — slots without an entry are
   *  ignored by the UI but still polled (useful for sanity-check reads
   *  the operator may want to expose later without a profile change). */
  slots: Record<number, VfdSlotDef>;

  /** Operator-action translations. Bridge issues each step as an
   *  `OrbitRegWrite(STORAGE_slot, 100 + driveOffset + cacheSlot, values)`
   *  call; the orbit's RTU master forwards according to the slot's
   *  `nativeAddr` + `fc`. */
  actions: {
    start: (speedRefPercent?: number) => VfdActionStep[];
    stop:  () => VfdActionStep[];
    reset: () => VfdActionStep[];
  };

  // ── UI display affordances ──────────────────────────────────────
  primarySetpointLabel: string;       // 'Speed', 'Pressure', 'Frequency', ...
  primarySetpointUnit:  string;       // '%', 'psi', 'Hz', ...
  /** Cache slot that holds the primary user-facing setpoint (for the
   *  fans page's main number input). Undefined if no single setpoint
   *  makes sense (e.g. PID-pressure drives with multiple PSI targets). */
  primarySetpointCacheSlot?: number;

  // ── Back-compat surface for apiRoutes.ts (/iot/fans/param, /inject-fault) ─
  paramAddrs: Record<string, number>;
  faultNames: Record<number, string>;
}

// ── Helper: project profile + unitId into wire-shape poll entries. ──
export function buildPollEntries(profile: VfdProfile, unitId: number): VfdPollEntryWire[] {
  return profile.pollEntries.map(t => ({
    cacheSlot:  t.cacheSlot,
    unitId,
    nativeAddr: t.nativeAddr,
    fc:         t.fc,
    pollRateMs: t.pollRateMs,
    writable:   t.writable,
  }));
}

// ═══════════════════════════════════════════════════════════════════════
// ABB ACS310 — classic process-data + group registers (FENA-01 layout).
// ═══════════════════════════════════════════════════════════════════════
// Control-word bits: 0=RUN, 1=OFF2, 2=OFF3, 3=RunEnable, 7=FaultReset, 11=Direction.
// Status-word bits:  0=Ready, 1=ReadyToOp, 2=Running, 3=Fault, 8=AtRef, 11=Dir.
// All registers are native HR (FC 3) — RS485 sees the same address space as
// the FENA-01 TCP adapter exposes.

const ABB_FAULTS: Record<number, string> = {
  1: 'Overcurrent', 2: 'DC Overvoltage', 3: 'Device Overtemperature',
  4: 'Short Circuit', 6: 'Earth Fault', 7: 'Input Phase Loss',
  8: 'Motor Overload', 9: 'Motor Overtemperature', 11: 'Output Phase Loss',
  12: 'DC Undervoltage', 16: 'Stall', 17: 'Underload',
};

const ABB_ACS310: VfdProfile = {
  manufacturer: 'abb-acs310',
  label: 'ABB ACS310',
  pollEntries: [
    { cacheSlot: 0,  nativeAddr: 0,    fc: FC.READ_HR, pollRateMs: 200,   writable: true  },  // ControlWord
    { cacheSlot: 1,  nativeAddr: 1,    fc: FC.READ_HR, pollRateMs: 200,   writable: true  },  // SpeedRef ×100
    { cacheSlot: 2,  nativeAddr: 2,    fc: FC.READ_HR, pollRateMs: 200,   writable: false },  // StatusWord
    { cacheSlot: 3,  nativeAddr: 3,    fc: FC.READ_HR, pollRateMs: 200,   writable: false },  // ActualSpeed ×100
    { cacheSlot: 4,  nativeAddr: 100,  fc: FC.READ_HR, pollRateMs: 500,   writable: false },  // 01.01 Output Frequency ×10
    { cacheSlot: 5,  nativeAddr: 103,  fc: FC.READ_HR, pollRateMs: 500,   writable: false },  // 01.04 Motor Current ×100
    { cacheSlot: 6,  nativeAddr: 105,  fc: FC.READ_HR, pollRateMs: 500,   writable: false },  // 01.06 Motor Power ×100
    { cacheSlot: 7,  nativeAddr: 106,  fc: FC.READ_HR, pollRateMs: 1000,  writable: false },  // 01.07 DC bus voltage
    { cacheSlot: 8,  nativeAddr: 108,  fc: FC.READ_HR, pollRateMs: 5000,  writable: false },  // 01.09 Drive Temp ×10
    { cacheSlot: 9,  nativeAddr: 400,  fc: FC.READ_HR, pollRateMs: 1000,  writable: false },  // 04.01 Active Fault Code
    { cacheSlot: 10, nativeAddr: 2001, fc: FC.READ_HR, pollRateMs: 10000, writable: true  },  // 20.01 Min Frequency ×10
    { cacheSlot: 11, nativeAddr: 2002, fc: FC.READ_HR, pollRateMs: 10000, writable: true  },  // 20.02 Max Frequency ×10
    { cacheSlot: 12, nativeAddr: 2202, fc: FC.READ_HR, pollRateMs: 10000, writable: true  },  // 22.02 Ramp Up ×10
    { cacheSlot: 13, nativeAddr: 2203, fc: FC.READ_HR, pollRateMs: 10000, writable: true  },  // 22.03 Ramp Down ×10
    { cacheSlot: 14, nativeAddr: 9906, fc: FC.READ_HR, pollRateMs: 60000, writable: false },  // 99.06 Rated Current ×100
    { cacheSlot: 15, nativeAddr: 9907, fc: FC.READ_HR, pollRateMs: 60000, writable: false },  // 99.07 Rated Frequency ×10
  ],
  slots: {
    0:  { field: 'controlWord' },
    1:  { field: 'speedRefPercent',  scale: 0.01 },
    2:  { field: 'statusWord' },
    3:  { field: 'actualSpeedPercent', scale: 0.01 },
    4:  { field: 'outputFreqHz',     scale: 0.1 },
    5:  { field: 'motorCurrentA',    scale: 0.01 },
    6:  { field: 'motorPowerkW',     scale: 0.01 },
    7:  { field: 'dcBusVoltage' },
    8:  { field: 'driveTemp',        scale: 0.1 },
    9:  { field: 'faultCode' },
    10: { field: 'minFreqHz',        scale: 0.1 },
    11: { field: 'maxFreqHz',        scale: 0.1 },
    12: { field: 'rampUpTime',       scale: 0.1 },
    13: { field: 'rampDownTime',     scale: 0.1 },
    14: { field: 'ratedCurrentA',    scale: 0.01 },
    15: { field: 'ratedFreqHz',      scale: 0.1 },
  },
  actions: {
    start: (pct = 0) => [{
      cacheSlot: 0,
      values: [0x047F, Math.max(0, Math.min(10000, Math.round(pct * 100)))],
    }],
    stop:  () => [{ cacheSlot: 0, values: [0x047E] }],
    reset: () => [
      { cacheSlot: 0, values: [0x0080], delayAfterMs: 500 },
      { cacheSlot: 0, values: [0x047E] },
    ],
  },
  primarySetpointLabel: 'Speed',
  primarySetpointUnit:  '%',
  primarySetpointCacheSlot: 1,
  paramAddrs: { rampUp: 2202, rampDown: 2203, minFreq: 2001, maxFreq: 2002 },
  faultNames: ABB_FAULTS,
};

// ─── ABB ACS380 ── thin variant sharing the ACS310 process-data + most
// of Group 01. Differences (e.g. missing 01.05 torque, FMBT-21 standard
// profile quirks) handled by leaving the affected cache slots empty.
const ABB_ACS380: VfdProfile = {
  ...ABB_ACS310,
  manufacturer: 'abb-acs380',
  label: 'ABB ACS380',
};

// ═══════════════════════════════════════════════════════════════════════
// Phase Tech LH AquaPhase — pump VFD; PID-targets a pressure setpoint.
// ═══════════════════════════════════════════════════════════════════════
// Run/stop ride a vendor-custom FC (FC 66 / 0x42) — encoded in the wire
// as fc=0x4201 (Set HOA Mode), 0x4202 (Set Run State), 0x4203 (Reset).
// The orbit's RTU master decodes (fc>>8) as the FC 66 sub-function and
// uses the values[] payload byte verbatim as the FC 66 data byte.
//
// HOA Run-Stop status is exposed via Discrete Input 10001 (FC 2).
// Live data is in Input Registers (FC 4); setpoints in Holding (FC 3+6).
//
// Phase Tech doc caveat: HR read block N ≤ 6 (LHAP) or 30 (LH-V). Orbit
// MUST issue per-entry reads (not block-fragment merging) for now.

const PHASE_TECH_FAULTS: Record<number, string> = {
  // System Status codes (30030): 0 = OK, positive = Fault.
  // Phase Tech's "common fault" tables are model-specific; populate as
  // we learn them from field testing. Operator-facing UI falls back to
  // "Fault {code}" until then.
  1: 'Overcurrent',
  2: 'Overvoltage',
  3: 'Undervoltage',
  4: 'Overtemp',
  5: 'Phase Loss',
  6: 'Ground Fault',
  7: 'Dry Well',
  8: 'Motor Overload',
};

const PHASE_TECH_LHAP: VfdProfile = {
  manufacturer: 'phase-tech-lhap',
  label: 'Phase Tech LH AquaPhase',
  pollEntries: [
    // ── READ-ONLY: state + live data ───────────────────────────────
    { cacheSlot: 0,  nativeAddr: 10001, fc: FC.READ_DI,        pollRateMs: 500,  writable: false }, // HOA Run-Stop bit
    { cacheSlot: 2,  nativeAddr: 30001, fc: FC.READ_IR,        pollRateMs: 1000, writable: false }, // HOA Auto-Manual current
    { cacheSlot: 5,  nativeAddr: 30030, fc: FC.READ_IR,        pollRateMs: 1000, writable: false }, // System Status (fault code)
    { cacheSlot: 6,  nativeAddr: 30013, fc: FC.READ_IR,        pollRateMs: 200,  writable: false }, // Output Frequency ×10
    { cacheSlot: 7,  nativeAddr: 30002, fc: FC.READ_IR,        pollRateMs: 500,  writable: false }, // I_u ×100
    { cacheSlot: 8,  nativeAddr: 30006, fc: FC.READ_IR,        pollRateMs: 500,  writable: false }, // Output KW ×10
    { cacheSlot: 9,  nativeAddr: 30009, fc: FC.READ_IR,        pollRateMs: 1000, writable: false }, // Bus Cap Voltage
    { cacheSlot: 10, nativeAddr: 30010, fc: FC.READ_IR,        pollRateMs: 1000, writable: false }, // Input Voltage
    { cacheSlot: 11, nativeAddr: 30017, fc: FC.READ_IR,        pollRateMs: 5000, writable: false }, // IGBT Case Temp ×10

    // ── READ + WRITE: setpoints + ramps ────────────────────────────
    { cacheSlot: 12, nativeAddr: 40002, fc: FC.READ_HR,        pollRateMs: 10000, writable: true }, // Min Frequency ×10
    { cacheSlot: 13, nativeAddr: 40003, fc: FC.READ_HR,        pollRateMs: 10000, writable: true }, // Max Frequency ×10
    { cacheSlot: 14, nativeAddr: 40004, fc: FC.READ_HR,        pollRateMs: 10000, writable: true }, // Start-Up Ramp ×10
    { cacheSlot: 15, nativeAddr: 40074, fc: FC.READ_HR,        pollRateMs:  5000, writable: true }, // PSI Setpoint ×10

    // ── WRITE-ONLY: vendor-custom commands (no periodic read) ──────
    { cacheSlot: 1,  nativeAddr: 0,     fc: FC.PHASE_SET_HOA,  pollRateMs: 0, writable: true },     // FC 66/1 Set HOA Mode
    { cacheSlot: 3,  nativeAddr: 0,     fc: FC.PHASE_SET_RUN,  pollRateMs: 0, writable: true },     // FC 66/2 Set Run State
    { cacheSlot: 4,  nativeAddr: 0,     fc: FC.PHASE_RESET,    pollRateMs: 0, writable: true },     // FC 66/3 Reset
  ],
  slots: {
    0:  { field: 'running' },
    // 1: write-only HOA-set command; no decoder
    2:  { field: 'opMode', decode: (raw) => raw === 0xF0 ? 2 : raw === 0x0F ? 1 : 0 },
    // 3: write-only run-state command; no decoder
    // 4: write-only reset command; no decoder
    5:  { field: 'faultCode' },
    6:  { field: 'outputFreqHz',  scale: 0.1 },
    7:  { field: 'motorCurrentA', scale: 0.01 },
    8:  { field: 'motorPowerkW',  scale: 0.1 },
    9:  { field: 'dcBusVoltage' },
    10: { field: 'inputVoltage' },
    11: { field: 'driveTemp',     scale: 0.1 },
    12: { field: 'minFreqHz',     scale: 0.1 },
    13: { field: 'maxFreqHz',     scale: 0.1 },
    14: { field: 'rampUpTime',    scale: 0.1 },
    15: { field: 'pressureSetpointPsi', scale: 0.1 },
  },
  actions: {
    // Start sequence: set HOA = Manual (0x0F), then set Run State = Run (0x0F).
    // Speed reference for pump VFDs lands as min=max frequency (operator
    // can override via cache_slot 12/13 if they really want manual freq).
    start: () => [
      { cacheSlot: 1, values: [0x0F], delayAfterMs: 50 },  // HOA → Manual
      { cacheSlot: 3, values: [0x0F] },                    // Run
    ],
    // Stop: clear Run State. Leave HOA mode alone — operator may want
    // to return to Auto separately.
    stop: () => [
      { cacheSlot: 3, values: [0x00] },
    ],
    // Reset: FC 66/3 with magic byte 0xA5.
    reset: () => [
      { cacheSlot: 4, values: [0xA5] },
    ],
  },
  primarySetpointLabel: 'Pressure',
  primarySetpointUnit:  'psi',
  primarySetpointCacheSlot: 15,
  paramAddrs: {
    minFreq: 40002,
    maxFreq: 40003,
    rampUp:  40004,
    shutdownRamp: 40014,
    overcurrentLimit: 40005,
    psiSetpoint: 40074,
    proportionalGain: 40065,
    integralGain: 40066,
    derivativeGain: 40067,
  },
  faultNames: PHASE_TECH_FAULTS,
};

// LH-V series shares the protocol — only baud rate + block-read cap
// differ (the orbit's RTU master picks both from RTU config, not from
// the profile). Reuse the same schedule.
const PHASE_TECH_LHV: VfdProfile = {
  ...PHASE_TECH_LHAP,
  manufacturer: 'phase-tech-lhv',
  label: 'Phase Tech LH-V',
};

// Legacy DXL profile kept as a sparse stub so prior `vfd-drives` config
// files referencing it don't fail at lookup. Operator should migrate to
// 'phase-tech-lhap' or 'phase-tech-lhv' when they re-flash a drive.
const PHASE_TECH_DXL: VfdProfile = {
  ...PHASE_TECH_LHAP,
  manufacturer: 'phase-tech-dxl',
  label: 'Phase Tech DXL (legacy)',
};

// ═══════════════════════════════════════════════════════════════════════
// Modbus-driven contactor — the simplest case. One coil at address 1
// for on/off; status mirrors the same coil via FC 1.
// ═══════════════════════════════════════════════════════════════════════
const MODBUS_CONTACTOR: VfdProfile = {
  manufacturer: 'modbus-contactor',
  label: 'Modbus Contactor',
  pollEntries: [
    { cacheSlot: 0, nativeAddr: 1, fc: FC.READ_COIL,  pollRateMs: 500, writable: false }, // current state
    { cacheSlot: 1, nativeAddr: 1, fc: FC.WRITE_COIL, pollRateMs: 0,   writable: true  }, // command
  ],
  slots: {
    0: { field: 'running' },
    // 1 is write-only; no decoder
  },
  actions: {
    start: () => [{ cacheSlot: 1, values: [0xFF00] }], // Modbus coil ON
    stop:  () => [{ cacheSlot: 1, values: [0x0000] }], // Modbus coil OFF
    reset: () => [{ cacheSlot: 1, values: [0x0000] }], // contactors don't fault — just deenergize
  },
  primarySetpointLabel: '',
  primarySetpointUnit:  '',
  paramAddrs: {},
  faultNames: {},
};

// ═══════════════════════════════════════════════════════════════════════
// Generic fallback — empty schedule. Used when the operator hasn't
// assigned a profile yet; vfdClient hands the orbit no entries for this
// drive and the snapshot stays offline.
// ═══════════════════════════════════════════════════════════════════════
const GENERIC: VfdProfile = {
  manufacturer: 'generic',
  label: 'Unconfigured',
  pollEntries: [],
  slots: {},
  actions: {
    start: () => [],
    stop:  () => [],
    reset: () => [],
  },
  primarySetpointLabel: '',
  primarySetpointUnit:  '',
  paramAddrs: {},
  faultNames: {},
};

const PROFILES: Record<VFDManufacturer, VfdProfile> = {
  'abb-acs310':       ABB_ACS310,
  'abb-acs380':       ABB_ACS380,
  'phase-tech-lhap':  PHASE_TECH_LHAP,
  'phase-tech-lhv':   PHASE_TECH_LHV,
  'phase-tech-dxl':   PHASE_TECH_DXL,
  'modbus-contactor': MODBUS_CONTACTOR,
  'generic':          GENERIC,
};

export function getProfile(mfg: VFDManufacturer | undefined): VfdProfile {
  return mfg ? (PROFILES[mfg] ?? GENERIC) : GENERIC;
}

export function listProfiles(): Array<{ manufacturer: VFDManufacturer; label: string }> {
  return Object.values(PROFILES).map(p => ({ manufacturer: p.manufacturer, label: p.label }));
}
