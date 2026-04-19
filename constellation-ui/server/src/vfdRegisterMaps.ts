/**
 * VFD Register Maps — manufacturer-specific Modbus register layouts.
 *
 * Each real VFD manufacturer uses different Modbus register addresses and
 * bit encodings for the same logical data.  This module abstracts those
 * differences so that VFDClient (production polling) and VFDSimulator
 * (demo emulation) can both work with any supported manufacturer.
 *
 * Supported:
 *   ABB ACS310       (with FENA-01 Modbus TCP adapter)
 *   Phase Tech DXL   (digital phase converter + VFD)
 *   Generic           (mirrors ABB layout — for demo / testing)
 */

// ═══════════════════════════════════════════════════
// Shared types
// ═══════════════════════════════════════════════════

export type VFDManufacturer = 'abb-acs310' | 'abb-acs380' | 'phase-tech-dxl' | 'generic';

/** Contiguous Modbus read block */
export interface ReadSpec {
  addr: number;
  count: number;
  /** Map raw register data into snapshot fields */
  apply(snap: SnapFields, data: number[]): void;
}

/** Writable snapshot fields populated from register reads */
export interface SnapFields {
  controlWord: number;
  speedRefPercent: number;
  statusWord: number;
  actualSpeedPercent: number;
  outputFreqHz: number;
  freqRefHz: number;
  motorSpeedRpm: number;
  motorCurrentA: number;
  motorTorquePercent: number;
  motorPowerkW: number;
  dcBusVoltage: number;
  outputVoltage: number;
  driveTemp: number;
  faultCode: number;
  minFreqHz: number;
  maxFreqHz: number;
  rampUpTime: number;
  rampDownTime: number;
  ratedCurrentA: number;
  ratedVoltage: number;
  ratedFreqHz: number;
  ratedSpeedRpm: number;
  ratedPowerkW: number;
  running: boolean;
  faulted: boolean;
  atReference: boolean;
  direction: number;
}

/** Internal simulator drive state accessed by profile functions */
export interface SimFields {
  controlWord: number;
  speedRefPercent: number;
  actualSpeedPercent: number;
  outputFreqHz: number;
  motorSpeedRpm: number;
  motorCurrentA: number;
  motorTorquePercent: number;
  motorPowerkW: number;
  dcBusVoltage: number;
  outputVoltage: number;
  driveTemp: number;
  lastFaultCode: number;
  running: boolean;
  faulted: boolean;
  warning: boolean;
  direction: 0 | 1;
  ratedCurrentA: number;
  ratedVoltage: number;
  ratedFreqHz: number;
  ratedSpeedRpm: number;
  ratedPowerkW: number;
  rampUpTimeSec: number;
  rampDownTimeSec: number;
  minFreqHz: number;
  maxFreqHz: number;
}

/** Full profile for one VFD manufacturer / model */
export interface VFDProfile {
  label: string;

  // ── Client polling ──
  reads: ReadSpec[];
  /** Derive boolean flags from status word (writes to snap) */
  deriveFlags(snap: SnapFields): void;

  // ── Client commands ──
  cwAddr: number;
  refAddr: number;
  commands: {
    start: number;
    stop: number;
    reset: number;
    postReset: number;
  };
  directionBit: number;
  /** Standby control word written every poll cycle to maintain comms.
   *  Must assert OFF2+OFF3+enable bits without commanding RUN.
   *  Set to 0 to disable heartbeat writes (read-only mode). */
  heartbeatCw: number;
  /** Convert 0–10 000 speed-percent to native ref register value */
  percentToRef(pct: number, maxFreqHz: number): number;
  /** Convert native ref register value back to 0–10 000 */
  refToPercent(ref: number, maxFreqHz: number): number;

  // ── Named parameter addresses ──
  paramAddrs: {
    rampUp: number;
    rampDown: number;
    minFreq: number;
    maxFreq: number;
  };

  // ── Fault lookup ──
  /** Modbus address of the active/last fault code register */
  faultAddr: number;
  faultNames: Record<number, string>;

  // ── Simulator helpers ──
  simRead(s: SimFields, addr: number): number;
  simWrite(s: SimFields, addr: number, value: number): void;
  simBuildStatus(s: SimFields): number;
  /** Parse control-word bits → update running/faulted/direction */
  simApplyControl(s: SimFields): void;
}

// ═══════════════════════════════════════════════════
// ABB ACS310
// ═══════════════════════════════════════════════════
//
// Register layout via FENA-01 Modbus TCP adapter.
//
//   Process data addr 0-3:   CW, SpeedRef(%), StatusWord, ActualSpeed(%)
//   Group 01    addr 100-108: Output signals (freq, rpm, current …)
//   Group 04    addr 400:     Active fault code
//   Group 20    addr 2001-02: Frequency limits        (0.1 Hz)
//   Group 22    addr 2202-03: Accel / decel ramp time  (0.1 s)
//   Group 99    addr 9901-05: Motor nameplate
//
// Control-word bits:
//   0: RUN  1: OFF2  2: OFF3  3: RunEnable  7: FaultReset  11: Direction
//
// Status-word bits:
//   0: Ready  1: ReadyToOp  2: Running  3: Fault  4: OFF2  5: OFF3
//   6: SwitchOnInhibit  7: Warning  8: AtRef  9: Remote  10: AboveLimit  11: Dir

// Comprehensive ABB fault code table (ACS310 / ACS380 / ACS580).
// Codes are decimal integers as returned in fault registers 400/500.
const ABB_FAULTS: Record<number, string> = {
  1:  'Overcurrent',
  2:  'DC Overvoltage',
  3:  'Device Overtemperature',
  4:  'Short Circuit',
  5:  'Analog Input Loss',
  6:  'Overacceleration',
  7:  'Motor Overtemperature',
  8:  'Motor Stall',
  9:  'Motor Underload',
  10: 'Output Phase Loss',
  11: 'Input Phase Loss',
  12: 'Earth Fault',
  13: 'Supply Phase Loss',
  14: 'Charge Circuit',
  15: 'Output Phase Fault',
  16: 'Safe Torque Off (STO)',
  17: 'PTC Thermistor',
  18: 'MCB Overtemp',
  20: 'Brake Chopper Short',
  21: 'Brake Chopper Overload',
  22: 'Power Unit',
  23: 'Surge / Thunderbolt',
  25: 'Motor Controller Fault',
  26: 'IGBT Overtemp',
  28: 'Supply Overvoltage',
  29: 'Ext Thermistor Fault',
  30: 'SMPS Fault',
  31: 'EEPROM Fault',
  32: 'Thyristor / SCR Fault',
  33: 'Measurement Fault',
  34: 'Internal Bus Comm',
  35: 'Application Fault',
  36: 'Panel Comm Loss',
  37: 'Fieldbus Comm Loss',
  38: 'Motor Phase Order',
  39: 'Ext Device Comm Loss',
  40: 'Device Identification',
  41: 'Internal Power Fault',
  42: 'Position Error',
  43: 'Encoder Fault',
  44: 'Resolver Fault',
  45: 'Overspeed Fault',
  46: 'Sync Error',
  48: 'Power Limit',
  50: 'External Fault 1',
  51: 'External Fault 2',
  52: 'External Fault 3',
  53: 'Modbus Comm Loss',
  55: 'Feedback Loss',
  56: 'Fieldbus Comm Loss B',
  57: 'Timer Fault',
  58: 'STO Input Wiring',
  59: 'Safe Speed',
  60: 'AO Supervision',
  61: 'Speed Error',
  62: 'Torque Open Loop',
  63: 'Brake Control Fault',
  64: 'Overcurrent (HW)',
  65: 'DC Overvoltage (HW)',
  66: 'DC Undervoltage (HW)',
  80: 'Drive Overtemp 2',
  81: 'Fan Fault',
  90: 'ID Run Incomplete',
  91: 'ID Run Warning',
  92: 'Motor Model Fault',
  95: 'FW Update Error',
};

const ABB_ACS310: VFDProfile = {
  label: 'ABB ACS310',

  reads: [
    { addr: 0, count: 4, apply(s, d) {
      s.controlWord = d[0]; s.speedRefPercent = d[1];
      s.statusWord = d[2];  s.actualSpeedPercent = d[3];
    }},
    { addr: 100, count: 9, apply(s, d) {
      s.outputFreqHz = d[0]; s.freqRefHz = d[1]; s.motorSpeedRpm = d[2];
      s.motorCurrentA = d[3] * 10;        // 0.1A → 0.01A standard
      s.motorTorquePercent = d[4] * 10;   // 0.1% → 0.01% standard
      s.motorPowerkW = d[5] * 10;         // 0.1kW → 0.01kW standard
      s.dcBusVoltage = d[6]; s.outputVoltage = d[7];
      s.driveTemp = d[8] * 10;            // 1°C → 0.1°C standard
    }},
    { addr: 400, count: 1, apply(s, d) { s.faultCode = d[0]; }},
    { addr: 2001, count: 2, apply(s, d) { s.minFreqHz = d[0]; s.maxFreqHz = d[1]; }},
    { addr: 2202, count: 2, apply(s, d) { s.rampUpTime = d[0]; s.rampDownTime = d[1]; }},
    { addr: 9901, count: 5, apply(s, d) {
      s.ratedCurrentA = d[0]; s.ratedVoltage = d[1]; s.ratedFreqHz = d[2];
      s.ratedSpeedRpm = d[3]; s.ratedPowerkW = d[4];
    }},
  ],

  deriveFlags(s) {
    s.running     = !!(s.statusWord & 0x0004);
    s.faulted     = !!(s.statusWord & 0x0008);
    s.atReference = !!(s.statusWord & 0x0100);
    s.direction   = (s.statusWord & 0x0800) ? 1 : 0;
  },

  cwAddr: 0,
  refAddr: 1,
  commands: { start: 0x047F, stop: 0x040E, reset: 0x04FE, postReset: 0x040E },
  directionBit: 11,
  heartbeatCw: 0x040E,  // OFF2+OFF3+Enable, no RUN

  percentToRef(p) { return Math.max(0, Math.min(10000, p)); },
  refToPercent(r) { return r; },

  paramAddrs: { rampUp: 2202, rampDown: 2203, minFreq: 2001, maxFreq: 2002 },
  faultAddr: 400,
  faultNames: ABB_FAULTS,

  // ── Simulator ──

  simBuildStatus(s) {
    let w = 0;
    if (!s.faulted) w |= 0x0003;
    if (s.running && s.actualSpeedPercent > 0) w |= 0x0004;
    if (s.faulted) w |= 0x0048;
    w |= 0x0030;
    if (s.warning) w |= 0x0080;
    if (Math.abs(s.actualSpeedPercent - s.speedRefPercent) < 50) w |= 0x0100;
    w |= 0x0200;
    if (s.actualSpeedPercent > 9500) w |= 0x0400;
    if (s.direction) w |= 0x0800;
    return w;
  },

  simApplyControl(s) {
    const cw = s.controlWord;
    if ((cw & 0x0080) && s.faulted) { s.faulted = false; s.lastFaultCode = 0; }
    if (!(cw & 0x0002)) s.running = false;
    if (!(cw & 0x0004)) s.running = false;
    if (!s.faulted) {
      const run = !!(cw & 0x0001);
      const en  = !!(cw & 0x0008);
      if (run && en) s.running = true;
      else if (!run) s.running = false;
    }
    s.direction = (cw & 0x0800) ? 1 : 0;
  },

  simRead(s, addr) {
    switch (addr) {
      case 0:    return s.controlWord;
      case 1:    return Math.round(s.speedRefPercent);
      case 2:    return ABB_ACS310.simBuildStatus(s);
      case 3:    return Math.round(s.actualSpeedPercent);
      case 100:  return s.outputFreqHz;
      case 101:  return s.outputFreqHz;   // freq ref mirrors output in sim
      case 102:  return s.motorSpeedRpm;
      case 103:  return s.motorCurrentA;
      case 104:  return s.motorTorquePercent;
      case 105:  return s.motorPowerkW;
      case 106:  return s.dcBusVoltage;
      case 107:  return s.outputVoltage;
      case 108:  return s.driveTemp;
      case 400:  return s.lastFaultCode;
      case 401:  return 0;
      case 402:  return 0;
      case 2001: return s.minFreqHz;
      case 2002: return s.maxFreqHz;
      case 2202: return Math.round(s.rampUpTimeSec * 10);
      case 2203: return Math.round(s.rampDownTimeSec * 10);
      case 9901: return s.ratedCurrentA;
      case 9902: return s.ratedVoltage;
      case 9903: return s.ratedFreqHz;
      case 9904: return s.ratedSpeedRpm;
      case 9905: return s.ratedPowerkW;
      default:   throw new Error(`Illegal data address ${addr}`);
    }
  },

  simWrite(s, addr, v) {
    switch (addr) {
      case 0:    s.controlWord = v & 0xFFFF; break;
      case 1:    s.speedRefPercent = Math.max(0, Math.min(10000, v)); break;
      case 2001: s.minFreqHz = Math.max(0, v); break;
      case 2002: s.maxFreqHz = Math.max(0, v); break;
      case 2202: s.rampUpTimeSec = Math.max(0.1, v / 10); break;
      case 2203: s.rampDownTimeSec = Math.max(0.1, v / 10); break;
      case 9901: s.ratedCurrentA = v; break;
      case 9902: s.ratedVoltage = v; break;
      case 9903: s.ratedFreqHz = v; break;
      case 9904: s.ratedSpeedRpm = v; break;
      case 9905: s.ratedPowerkW = v; break;
    }
  },
};

// ═══════════════════════════════════════════════════
// ABB ACS380
// ═══════════════════════════════════════════════════
//
// ACS380 micro drive with FENA-21 Modbus TCP adapter (standard ABB Drives profile).
// Same CW / SW bit assignments as ACS310.
// FENA uses standard profile (regs 0-3 for process data, NOT Enhanced
// profile which would use regs 50+).
//
// Group 01 parameter layout (verified by live register probe):
//   01.01 (100): Motor speed        01.06 (105): Motor torque
//   01.02 (101): Speed reference     01.07 (106): Motor power
//   01.03 (102): Output frequency    01.08 (107): Motor voltage
//   01.04 (103): Motor current       01.09 (108): (motor thermal, returns 0)
//   01.05 (104): MISSING             01.10 (109): Drive thermal state
//   01.11 (110): DC bus voltage (confirmed live: ~2960 = 296.0V)
//
// Nameplate shifted to 99.06-99.10 (regs 9905-9909)
// Group 22 ramp times NOT accessible via standard Modbus registers.

const ABB_ACS380: VFDProfile = {
  label: 'ABB ACS380 (FMBT-21)',

  reads: [
    // ── Block 1: Process data (regs 0-5) ──
    // Fixed by FMBT-21 Standard profile:
    //   Reg 0: Control Word IN
    //   Reg 1: Speed Reference IN (0-20000 = 0-100%)
    //   Reg 2: Always 0 (FMBT-21 does not populate this in standard profile)
    //   Reg 3: Status Word OUT (verified live: 0x1238 = faulted+OFF2+OFF3+Remote+DriveOK)
    //   Reg 4: Actual Speed (0-20000 = 0-100%) — extended OUT (same scale as reg 101)
    //   Reg 5: Duplicate of Reg 4
    { addr: 0, count: 6, apply(s, d) {
      s.controlWord = d[0];
      s.speedRefPercent = Math.round(d[1] / 2);  // 0-20000 → 0-10000
      s.statusWord = d[3];                         // Reg 3 is the real status word (Reg 2 always 0)
      s.actualSpeedPercent = Math.round(d[4] / 2);  // 0-20000 → 0-10000
      s.direction = (d[3] & 0x8000) ? 1 : 0;
    }},
    // ── Block 2: Group 01 actual signals via Address Mode 0 ──
    // Register = 100 × Group + Index (confirmed via FMBT-21 web config)
    // Note: reg 104 (1.04) returns exception — must be skipped.
    // Read regs 101-103 (1.01-1.03)
    { addr: 101, count: 3, apply(s, d) {
      // 1.01 Motor Speed: 0-32767 scale (signed 16-bit, confirmed by probe:
      //   100% → 32767, 50% → 19990).  Convert to 0-10000 internal.
      s.actualSpeedPercent = Math.round((d[0] / 32767) * 10000);
      // 1.02 Speed Reference: 0.1% units (e.g. 998 = 99.8%). Not used directly —
      //   speedRefPercent already set from Block 1 reg 1.
      // 1.03 Output Frequency: always returns 0 on ACS380 via FMBT-21.
      //   Derived in deriveFlags from speed × rated freq instead.
    }},
    // Read regs 105-110 (1.05-1.10), skipping reg 104
    { addr: 105, count: 6, apply(s, d) {
      // 1.05 Motor Torque: 0-32767 scale (same as speed).  Convert to 0-10000 (0.01% standard)
      s.motorTorquePercent = Math.round((d[0] / 32767) * 10000);
      // 1.06 Motor Current (raw 0.01 A) — keep as 0.01A standard
      s.motorCurrentA = d[1];                          // e.g. 82 = 0.82 A
      // 1.07 Motor Power (raw 0.01 kW) — keep as 0.01kW standard
      s.motorPowerkW = d[2];                           // e.g. 32 = 0.32 kW
      // 1.08 Motor Thermal Level (%) — NOT output voltage (confirmed raw=2 at cold motor)
      // Output voltage is derived in deriveFlags from speed × ratedVoltage.
      // 1.09 DC Bus Voltage (1 V)
      s.dcBusVoltage = d[4];                            // e.g. 332 = 332 V
      // 1.10 Drive Temperature (raw 0.01°C) → 0.1°C standard
      s.driveTemp = Math.round(d[5] / 10);             // e.g. 2977 → 298 = 29.8°C
    }},
    // ── Block 3: Fault code ──
    // Reg 400 returns FMBT-21 mapping data (0x3381), not a fault code.
    // Reg 401 always 0.  Reg 500 = param 5.01 = last fault code (verified live: 0x000D).
    { addr: 500, count: 1, apply(s, d) {
      s.faultCode = d[0];
    }},
    // ── Block 4: Nameplate data ──
    // 99.05 = reg 9905: Rated Current (0.1 A)
    { addr: 9905, count: 1, apply(s, d) {
      s.ratedCurrentA = d[0];                          // e.g. 260 = 2.60 A (0.01A units)
    }},
    // 99.06-99.09 = regs 9906-9909: Voltage, Freq, Speed, Power
    // (9901/9904 throw exceptions so we read 9906+ as a contiguous block)
    { addr: 9906, count: 4, apply(s, d) {
      s.ratedVoltage = Math.round(d[0] / 10);         // 2300 → 230 V (raw is 0.1V)
      s.ratedFreqHz = d[1];                            // 600 = 60.0 Hz (0.1 Hz)
      s.ratedSpeedRpm = d[2];                          // 3450 RPM
      s.ratedPowerkW = d[3];                           // 75 = 0.75 HP (0.01 HP)
    }},
  ],

  // Derive running/faulted flags from status word (Reg 3).
  // Reg 3 is the real status word — same bit layout as ACS310.
  deriveFlags(s) {
    s.running     = !!(s.statusWord & 0x0004);
    s.faulted     = !!(s.statusWord & 0x0008);
    s.atReference = !!(s.statusWord & 0x0100);
    // direction already set from bit 15 of reg 3 in the read block
    // Derive RPM from speed % and nameplate rated speed
    if (s.ratedSpeedRpm > 0) {
      s.motorSpeedRpm = Math.round((s.actualSpeedPercent / 10000) * s.ratedSpeedRpm);
    }
    // Derive output frequency from speed % and nameplate rated frequency
    // (reg 103 / param 1.03 always returns 0 on ACS380 via FMBT-21)
    if (s.ratedFreqHz > 0 && s.actualSpeedPercent > 0) {
      s.outputFreqHz = Math.round((s.actualSpeedPercent / 10000) * s.ratedFreqHz);
    }
    // Derive output voltage (V/Hz: proportional to speed × rated voltage)
    // Store in 0.1V units to match ACS310 standard
    if (s.ratedVoltage > 0 && s.actualSpeedPercent > 0) {
      s.outputVoltage = Math.round((s.actualSpeedPercent / 10000) * s.ratedVoltage * 10);
    }
  },

  cwAddr: 0,
  refAddr: 1,
  commands: { start: 0x047F, stop: 0x040E, reset: 0x04FE, postReset: 0x040E },
  directionBit: 11,
  heartbeatCw: 0x040E,  // OFF2+OFF3+Enable, no RUN — clears FMBT-21 e-stop warning

  // FMBT-21 uses 0-20000 for 0-100%.  Our internal/UI is 0-10000.
  percentToRef(p) { return Math.max(0, Math.min(20000, Math.round(p * 2))); },
  refToPercent(r) { return Math.round(r / 2); },

  paramAddrs: { rampUp: 2202, rampDown: 2203, minFreq: 2001, maxFreq: 2002 },
  faultAddr: 500,
  faultNames: ABB_FAULTS,

  // Simulator re-uses ACS310 sim logic (same CW/SW bits)
  simBuildStatus: ABB_ACS310.simBuildStatus,
  simApplyControl: ABB_ACS310.simApplyControl,

  simRead(s, addr) {
    switch (addr) {
      case 0:    return s.controlWord;
      case 1:    return Math.round(s.speedRefPercent);
      case 2:    return ABB_ACS380.simBuildStatus(s);
      case 3:    return Math.round(s.actualSpeedPercent);
      case 100:  return s.motorSpeedRpm;    // 01.01
      case 101:  return s.outputFreqHz;     // 01.02 (speed/freq ref)
      case 102:  return s.outputFreqHz;     // 01.03 output freq
      case 103:  return s.motorCurrentA;    // 01.04
      // 104 intentionally missing
      case 105:  return s.motorTorquePercent; // 01.06
      case 106:  return s.motorPowerkW;     // 01.07
      case 107:  return s.outputVoltage;    // 01.08
      case 109:  return s.driveTemp;        // 01.10
      case 110:  return s.dcBusVoltage;     // 01.11
      case 400:  return s.lastFaultCode;
      case 2001: return s.minFreqHz;
      case 2002: return s.maxFreqHz;
      case 9905: return s.ratedCurrentA;
      case 9906: return s.ratedVoltage;
      case 9907: return s.ratedFreqHz;
      case 9908: return s.ratedSpeedRpm;
      case 9909: return s.ratedPowerkW;
      default:   throw new Error(`Illegal data address ${addr}`);
    }
  },

  simWrite(s, addr, v) {
    switch (addr) {
      case 0:    s.controlWord = v & 0xFFFF; break;
      case 1:    s.speedRefPercent = Math.max(0, Math.min(10000, v)); break;
      case 2001: s.minFreqHz = Math.max(0, v); break;
      case 2002: s.maxFreqHz = Math.max(0, v); break;
      case 9905: s.ratedCurrentA = v; break;
      case 9906: s.ratedVoltage = v; break;
      case 9907: s.ratedFreqHz = v; break;
      case 9908: s.ratedSpeedRpm = v; break;
      case 9909: s.ratedPowerkW = v; break;
    }
  },
};

// ═══════════════════════════════════════════════════
// Phase Technologies DXL
// ═══════════════════════════════════════════════════
//
// Digital phase converter + VFD.  Compact contiguous register layout.
// Speed / frequency references are in 0.1 Hz (not percent).
//
//   Process data 0-3:
//     0: Command Register  (R/W)  bit0=Run, bit1=Dir, bit2=FaultReset
//     1: Frequency Setpoint(R/W)  0.1 Hz
//     2: Status Register   (R)    bit0=Ready, bit1=Running, bit2=Fault,
//                                 bit3=AtSpeed, bit4=Dir, bit5=Warning
//     3: Output Frequency  (R)    0.1 Hz
//
//   Operating data 4-11:
//     4:  Output Current    0.1 A
//     5:  Output Voltage    0.1 V
//     6:  DC Bus Voltage    1 V
//     7:  Drive Temperature 1 °C
//     8:  Motor Speed       1 RPM
//     9:  Motor Power       0.1 kW
//     10: Motor Torque      0.1 %
//     11: Active Fault Code
//
//   Configuration 20-28:
//     20: Accel Time        0.1 s
//     21: Decel Time        0.1 s
//     22: Min Frequency     0.1 Hz
//     23: Max Frequency     0.1 Hz
//     24: Rated Current     0.1 A
//     25: Rated Voltage     1 V
//     26: Rated Frequency   0.1 Hz
//     27: Rated Speed       1 RPM
//     28: Rated Power       0.1 kW

const DXL_FAULTS: Record<number, string> = {
  1:  'Overcurrent',       2:  'Overvoltage',
  3:  'Undervoltage',      4:  'Overtemperature',
  5:  'Input Phase Loss',  6:  'Voltage Imbalance',
  7:  'Ground Fault',      8:  'Motor Overload',
  9:  'Communication Loss',10: 'External Trip',
};

const PHASE_TECH_DXL: VFDProfile = {
  label: 'Phase Technologies DXL',

  reads: [
    // Process data (addr 0-3)
    { addr: 0, count: 4, apply(s, d) {
      s.controlWord = d[0];
      s.freqRefHz = d[1];          // native: 0.1 Hz
      s.statusWord = d[2];
      s.outputFreqHz = d[3];       // native: 0.1 Hz
    }},
    // Operating data (addr 4-11)
    { addr: 4, count: 8, apply(s, d) {
      s.motorCurrentA = d[0] * 10;       // 0.1A → 0.01A standard
      s.outputVoltage = d[1];             // 0.1 V
      s.dcBusVoltage = d[2];              // 1 V
      s.driveTemp = d[3] * 10;            // 1°C → 0.1°C standard
      s.motorSpeedRpm = d[4];             // 1 RPM
      s.motorPowerkW = d[5] * 10;         // 0.1kW → 0.01kW standard
      s.motorTorquePercent = d[6] * 10;   // 0.1% → 0.01% standard
      s.faultCode = d[7];
    }},
    // Configuration (addr 20-28)
    { addr: 20, count: 9, apply(s, d) {
      s.rampUpTime = d[0];         // 0.1 s
      s.rampDownTime = d[1];
      s.minFreqHz = d[2];          // 0.1 Hz
      s.maxFreqHz = d[3];
      s.ratedCurrentA = d[4];      // 0.1 A
      s.ratedVoltage = d[5];       // 1 V
      s.ratedFreqHz = d[6];        // 0.1 Hz
      s.ratedSpeedRpm = d[7];      // 1 RPM
      s.ratedPowerkW = d[8];       // 0.1 kW
    }},
  ],

  deriveFlags(s) {
    s.running     = !!(s.statusWord & 0x0002);
    s.faulted     = !!(s.statusWord & 0x0004);
    s.atReference = !!(s.statusWord & 0x0008);
    s.direction   = (s.statusWord & 0x0010) ? 1 : 0;
    // Convert frequency-based refs to canonical 0–10 000 percent
    const maxHz = s.maxFreqHz || 600;
    s.speedRefPercent    = Math.round((s.freqRefHz / maxHz) * 10000);
    s.actualSpeedPercent = Math.round((s.outputFreqHz / maxHz) * 10000);
  },

  cwAddr: 0,
  refAddr: 1,
  commands: { start: 0x0001, stop: 0x0000, reset: 0x0004, postReset: 0x0000 },
  directionBit: 1,
  heartbeatCw: 0,  // DXL doesn't need heartbeat writes

  percentToRef(pct, maxHz) {
    return Math.round((Math.max(0, Math.min(10000, pct)) / 10000) * (maxHz || 600));
  },
  refToPercent(ref, maxHz) {
    return Math.round((ref / (maxHz || 600)) * 10000);
  },

  paramAddrs: { rampUp: 20, rampDown: 21, minFreq: 22, maxFreq: 23 },
  faultAddr: 11,
  faultNames: DXL_FAULTS,

  // ── Simulator ──

  simBuildStatus(s) {
    let w = 0;
    if (!s.faulted) w |= 0x0001;                                       // Ready
    if (s.running && s.actualSpeedPercent > 0) w |= 0x0002;           // Running
    if (s.faulted) w |= 0x0004;                                        // Fault
    if (Math.abs(s.actualSpeedPercent - s.speedRefPercent) < 50) w |= 0x0008; // At speed
    if (s.direction) w |= 0x0010;                                       // Direction
    if (s.warning) w |= 0x0020;                                         // Warning
    return w;
  },

  simApplyControl(s) {
    const cw = s.controlWord;
    if ((cw & 0x0004) && s.faulted) { s.faulted = false; s.lastFaultCode = 0; }
    if (!s.faulted) {
      if (cw & 0x0001) s.running = true;
      else s.running = false;
    }
    s.direction = (cw & 0x0002) ? 1 : 0;
  },

  simRead(s, addr) {
    switch (addr) {
      case 0:  return s.controlWord;
      case 1:  return Math.round((s.speedRefPercent / 10000) * (s.maxFreqHz || 600));
      case 2:  return PHASE_TECH_DXL.simBuildStatus(s);
      case 3:  return s.outputFreqHz;
      case 4:  return s.motorCurrentA;
      case 5:  return s.outputVoltage;
      case 6:  return s.dcBusVoltage;
      case 7:  return s.driveTemp;
      case 8:  return s.motorSpeedRpm;
      case 9:  return s.motorPowerkW;
      case 10: return s.motorTorquePercent;
      case 11: return s.lastFaultCode;
      case 20: return Math.round(s.rampUpTimeSec * 10);
      case 21: return Math.round(s.rampDownTimeSec * 10);
      case 22: return s.minFreqHz;
      case 23: return s.maxFreqHz;
      case 24: return s.ratedCurrentA;
      case 25: return s.ratedVoltage;
      case 26: return s.ratedFreqHz;
      case 27: return s.ratedSpeedRpm;
      case 28: return s.ratedPowerkW;
      default: throw new Error(`Illegal data address ${addr}`);
    }
  },

  simWrite(s, addr, v) {
    switch (addr) {
      case 0: s.controlWord = v & 0xFFFF; break;
      case 1: {
        const maxHz = s.maxFreqHz || 600;
        s.speedRefPercent = Math.max(0, Math.min(10000, Math.round((v / maxHz) * 10000)));
        break;
      }
      case 20: s.rampUpTimeSec = Math.max(0.1, v / 10); break;
      case 21: s.rampDownTimeSec = Math.max(0.1, v / 10); break;
      case 22: s.minFreqHz = Math.max(0, v); break;
      case 23: s.maxFreqHz = Math.max(0, v); break;
      case 24: s.ratedCurrentA = v; break;
      case 25: s.ratedVoltage = v; break;
      case 26: s.ratedFreqHz = v; break;
      case 27: s.ratedSpeedRpm = v; break;
      case 28: s.ratedPowerkW = v; break;
    }
  },
};

// ═══════════════════════════════════════════════════
// Profile lookup
// ═══════════════════════════════════════════════════

const PROFILES: Record<VFDManufacturer, VFDProfile> = {
  'abb-acs310':    ABB_ACS310,
  'abb-acs380':    ABB_ACS380,
  'phase-tech-dxl': PHASE_TECH_DXL,
  'generic':       ABB_ACS310,   // generic re-uses ABB layout
};

export function getProfile(mfg: VFDManufacturer | undefined): VFDProfile {
  return PROFILES[mfg ?? 'generic'] ?? ABB_ACS310;
}
