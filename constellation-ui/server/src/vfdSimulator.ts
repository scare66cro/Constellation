/**
 * ABB ACS310 Variable Frequency Drive — Modbus TCP Simulator
 * ────────────────────────────────────────────────────────────
 * Faithful Modbus register emulation of an ACS310 with FENA-01
 * Modbus TCP adapter. A client written against this simulator will
 * work unchanged against a real ACS310 + FENA-01, and vice-versa.
 *
 * Register Map (0-based Modbus address / 1-based register number):
 *
 *   ── Process Data (fieldbus fast-path) ──
 *   addr 0  (reg 1)   Control Word     (R/W)
 *   addr 1  (reg 2)   Speed Reference  (R/W)  0–10000 = 0–100.00%
 *   addr 2  (reg 3)   Status Word      (R)
 *   addr 3  (reg 4)   Actual Speed     (R)    0–10000 = 0–100.00%
 *
 *   ── Group 01: Actual Signals (read-only) ──
 *   addr 100 (01.01)  Output Frequency   0.1 Hz
 *   addr 101 (01.02)  Frequency Ref      0.1 Hz
 *   addr 102 (01.03)  Motor Speed        1 rpm
 *   addr 103 (01.04)  Motor Current      0.1 A
 *   addr 104 (01.05)  Motor Torque       0.1 %
 *   addr 105 (01.06)  Motor Power        0.1 kW
 *   addr 106 (01.07)  DC Bus Voltage     1 V
 *   addr 107 (01.08)  Output Voltage     0.1 V
 *   addr 108 (01.09)  Drive Temperature  1 °C
 *
 *   ── Group 04: Fault History (read-only) ──
 *   addr 400 (04.01)  Active Fault Code
 *   addr 401 (04.02)  Previous Fault 1
 *   addr 402 (04.03)  Previous Fault 2
 *
 *   ── Group 99: Motor Nameplate (R/W) ──
 *   addr 9901 (99.02) Nominal Current    0.1 A
 *   addr 9902 (99.03) Nominal Voltage    1 V
 *   addr 9903 (99.04) Nominal Frequency  0.1 Hz
 *   addr 9904 (99.05) Nominal Speed      1 rpm
 *   addr 9905 (99.06) Nominal Power      0.1 kW
 *
 * Control Word bits:
 *   0: RUN/OFF1  1: OFF2  2: OFF3  3: Run enable
 *   4: Ramp out  5: Ramp hold  6: Fieldbus ref  7: Fault reset
 *   10: Supervision  11: Direction (0=FWD 1=REV)
 *
 * Status Word bits:
 *   0: Ready  1: Ready-to-op  2: Running  3: Fault
 *   4: OFF2   5: OFF3  6: Switch-on inhibit  7: Warning
 *   8: At reference  9: Remote  10: Above limit  11: Direction
 *
 * Port: 5020 (default, configurable via VFD_PORT env var)
 */

import { ServerTCP, type IServiceVector } from 'modbus-serial';
import { saveConfig, loadConfig } from './simConfig.js';
import { getProfile, type VFDManufacturer } from './vfdRegisterMaps.js';

export type { VFDManufacturer } from './vfdRegisterMaps.js';

export interface VFDState {
  /** Modbus unit ID (1–247) */
  unitId: number;
  /** Human label */
  label: string;
  /** VFD manufacturer/model for register interpretation */
  manufacturer: VFDManufacturer;

  // ── Process data (addresses 0-3) ──
  controlWord: number;       // addr 0: control word
  speedRefPercent: number;   // addr 1: 0–10000 (0.00–100.00%)
  actualSpeedPercent: number;// addr 3: 0–10000

  // ── Group 01: Actual signals (addresses 100-108) ──
  outputFreqHz: number;      // addr 100 (01.01): 0.1 Hz  (600 = 60.0 Hz)
  motorSpeedRpm: number;     // addr 102 (01.03): 1 rpm
  motorCurrentA: number;     // addr 103 (01.04): 0.1 A   (66 = 6.6 A)
  motorTorquePercent: number;// addr 104 (01.05): 0.1 %
  motorPowerkW: number;      // addr 105 (01.06): 0.1 kW  (22 = 2.2 kW)
  dcBusVoltage: number;      // addr 106 (01.07): 1 V
  outputVoltage: number;     // addr 107 (01.08): 0.1 V   (4600 = 460.0 V)
  driveTemp: number;         // addr 108 (01.09): 1 °C

  // ── Group 04: Fault history (address 400) ──
  lastFaultCode: number;     // addr 400 (04.01): active fault

  // ── Internal simulation state ──
  running: boolean;
  faulted: boolean;
  warning: boolean;
  direction: 0 | 1;          // 0=FWD, 1=REV

  // ── Motor nameplate — Group 99 (addresses 9901-9905) ──
  ratedCurrentA: number;     // addr 9901 (99.02): 0.1 A  (66 = 6.6 A)
  ratedVoltage: number;      // addr 9902 (99.03): 1 V    (460)
  ratedFreqHz: number;       // addr 9903 (99.04): 0.1 Hz (600 = 60.0 Hz)
  ratedSpeedRpm: number;     // addr 9904 (99.05): 1 rpm  (1750)
  ratedPowerkW: number;      // addr 9905 (99.06): 0.1 kW (22 = 2.2 kW)
  rampUpTimeSec: number;     // accel ramp (seconds 0→100%)
  rampDownTimeSec: number;   // decel ramp

  // ── Group 22: Speed ramp settings (addresses 2202-2203) ──
  // These mirror ABB par 22.02/22.03 (acceleration/deceleration time in 0.1 s)
  // rampUpTimeSec and rampDownTimeSec are the actual sim values in seconds;
  // the register values are 0.1 s (100 = 10.0 s)

  // ── Group 20: Frequency limits (addresses 2001-2002) ──
  minFreqHz: number;         // addr 2001 (20.01): 0.1 Hz  (0 = 0 Hz)
  maxFreqHz: number;         // addr 2002 (20.02): 0.1 Hz  (600 = 60.0 Hz)
}

function defaultDrive(unitId: number, label: string, manufacturer: VFDManufacturer = 'abb-acs310'): VFDState {
  return {
    unitId,
    label,
    manufacturer,
    // Process data
    controlWord: 0,
    speedRefPercent: 0,
    actualSpeedPercent: 0,
    // Group 01 actual signals
    outputFreqHz: 0,       // 0.1 Hz
    motorSpeedRpm: 0,
    motorCurrentA: 0,      // 0.1 A
    motorTorquePercent: 0, // 0.1 %
    motorPowerkW: 0,       // 0.1 kW
    dcBusVoltage: 650,     // 1 V  (typical DC bus at 460V supply)
    outputVoltage: 0,      // 0.1 V
    driveTemp: 35,         // 1 °C  (ambient at power-up)
    // Group 04 faults
    lastFaultCode: 0,
    // Sim state
    running: false,
    faulted: false,
    warning: false,
    direction: 0,
    // Group 99 motor nameplate  (20 HP / 14.9 kW, 460V, 60 Hz, 4-pole)
    ratedCurrentA: 270,    // 27.0 A × 10
    ratedVoltage: 460,     // 460 V
    ratedFreqHz: 600,      // 60.0 Hz × 10
    ratedSpeedRpm: 1760,   // rpm
    ratedPowerkW: 149,     // 14.9 kW × 10
    rampUpTimeSec: 10,
    rampDownTimeSec: 10,
    // Group 20 frequency limits
    minFreqHz: 0,        // 0 Hz
    maxFreqHz: 600,      // 60.0 Hz
  };
}

// ────────────────────────────────────────────────
// Drive simulation tick
// ────────────────────────────────────────────────

const TICK_MS = 100;  // 10 Hz simulation

function tickDrive(d: VFDState): void {
  // Process control word with manufacturer-specific bit layout
  const profile = getProfile(d.manufacturer);
  profile.simApplyControl(d);

  // ── Speed ramp ──
  const target = d.running ? d.speedRefPercent : 0;
  const dt = TICK_MS / 1000;
  if (d.actualSpeedPercent < target) {
    const step = (10000 / d.rampUpTimeSec) * dt;
    d.actualSpeedPercent = Math.min(target, d.actualSpeedPercent + step);
  } else if (d.actualSpeedPercent > target) {
    const step = (10000 / d.rampDownTimeSec) * dt;
    d.actualSpeedPercent = Math.max(target, d.actualSpeedPercent - step);
  }

  // ── Derived outputs (register values use real ACS310 scaling) ──
  const speedFrac = d.actualSpeedPercent / 10000; // 0.0–1.0

  // 01.01 Output frequency = ratedFreq × speed fraction  (0.1 Hz)
  let outFreq = Math.round((d.ratedFreqHz) * speedFrac);
  // Clamp to Group 20 min/max freq limits
  if (outFreq > 0 && outFreq < d.minFreqHz) outFreq = d.minFreqHz;
  if (outFreq > d.maxFreqHz) outFreq = d.maxFreqHz;
  d.outputFreqHz = outFreq;

  // 01.03 Motor speed in RPM
  d.motorSpeedRpm = Math.round(d.ratedSpeedRpm * speedFrac);

  // 01.08 Output voltage: V/f linear up to rated  (0.1 V)
  d.outputVoltage = Math.round(d.ratedVoltage * speedFrac * 10);

  // Motor current: cubic affinity law — power ∝ speed³, so current ∝ speed²
  // Plus ~15% no-load magnetizing current at any non-zero speed
  const loadFrac = speedFrac > 0.01 ? 0.15 + 0.85 * speedFrac * speedFrac : 0;
  d.motorCurrentA = Math.round(d.ratedCurrentA * loadFrac);

  // 01.05 Torque  (0.1 %)  — torque ∝ speed² for centrifugal loads
  d.motorTorquePercent = Math.round(speedFrac * speedFrac * 1000);

  // 01.06 Power — Fan/pump affinity law: P ∝ speed³  (0.1 kW)
  d.motorPowerkW = Math.round(d.ratedPowerkW * speedFrac * speedFrac * speedFrac);

  // 01.07 DC bus jitter around 650 V
  d.dcBusVoltage = 650 + Math.round((Math.random() - 0.5) * 4);

  // Warning: >90% speed
  d.warning = speedFrac > 0.90;

  // Drive temperature: ambient 35 + heat from cubic load
  const heatFrac = speedFrac > 0.01 ? 0.15 + 0.85 * speedFrac * speedFrac : 0;
  const heatRise = Math.round(heatFrac * 25);
  d.driveTemp = 35 + heatRise;
}

// ────────────────────────────────────────────────
// Modbus register access
// ────────────────────────────────────────────────

function readRegister(drives: Map<number, VFDState>, addr: number, unitId: number): number {
  const d = drives.get(unitId);
  if (!d) throw new Error(`Unknown unit ${unitId}`);
  const profile = getProfile(d.manufacturer);
  return profile.simRead(d, addr);
}

function writeRegister(drives: Map<number, VFDState>, addr: number, value: number, unitId: number): void {
  const d = drives.get(unitId);
  if (!d) throw new Error(`Unknown unit ${unitId}`);
  const profile = getProfile(d.manufacturer);
  profile.simWrite(d, addr, value);
}

// ────────────────────────────────────────────────
// Public API
// ────────────────────────────────────────────────

export interface VFDSimulatorOptions {
  host?: string;
  port?: number;
}

export class VFDSimulator {
  private drives: Map<number, VFDState> = new Map();
  private server: ServerTCP | null = null;
  private tickTimer: ReturnType<typeof setInterval> | null = null;
  private syncTimer: ReturnType<typeof setInterval> | null = null;
  private host: string;
  private port: number;

  constructor(options?: VFDSimulatorOptions) {
    this.host = options?.host ?? '0.0.0.0';
    this.port = options?.port ?? 5020;

    // Load persisted drives or create default
    const saved = loadConfig<VFDState[]>('vfd-drives');
    if (saved && Array.isArray(saved) && saved.length > 0) {
      for (const d of saved) {
        this.drives.set(d.unitId, { ...defaultDrive(d.unitId, d.label), ...d });
      }
      console.log(`[VFD] Loaded ${saved.length} drive(s) from disk`);
    } else {
      // Default: single ACS310 as unit 1 for a supply fan
      const fan = defaultDrive(1, 'Supply Fan VFD', 'abb-acs310');
      this.drives.set(1, fan);
      console.log(`[VFD] Default config: 1 drive (unit 1: Supply Fan VFD)`);
    }
  }

  private persist(): void {
    saveConfig('vfd-drives', Array.from(this.drives.values()));
  }

  /** Start the Modbus TCP server and simulation tick */
  start(): void {
    const drives = this.drives;

    const vector: IServiceVector = {
      getHoldingRegister: (addr: number, unitID: number): number => {
        return readRegister(drives, addr, unitID);
      },

      getMultipleHoldingRegisters: (addr: number, length: number, unitID: number): number[] => {
        const result: number[] = [];
        for (let i = 0; i < length; i++) {
          result.push(readRegister(drives, addr + i, unitID));
        }
        return result;
      },

      getInputRegister: (addr: number, unitID: number): number => {
        return readRegister(drives, addr, unitID);
      },

      setRegister: (addr: number, value: number, unitID: number): void => {
        writeRegister(drives, addr, value, unitID);
      },
    };

    this.server = new ServerTCP(vector, {
      host: this.host,
      port: this.port,
      debug: false,
    });

    this.server.on('initialized', () => {
      console.log(`[VFD] Modbus TCP server listening on ${this.host}:${this.port}`);
      console.log(`[VFD] Drives: ${Array.from(drives.values()).map(d => `unit ${d.unitId} (${d.label})`).join(', ')}`);
    });

    (this.server as any).on('error', (err: Error) => {
      console.error(`[VFD] Modbus TCP error: ${err.message}`);
    });

    // Start simulation tick
    this.tickTimer = setInterval(() => {
      for (const d of drives.values()) {
        tickDrive(d);
      }
    }, TICK_MS);

    // Periodically sync metadata (manufacturer, label) from on-disk config
    // so the simulator reflects changes made via the bridge's /fans/meta API.
    this.syncTimer = setInterval(() => {
      const saved = loadConfig<any[]>('vfd-drives');
      if (!saved || !Array.isArray(saved)) return;
      for (const cfg of saved) {
        const d = this.drives.get(cfg.unitId);
        if (!d) continue;
        if (cfg.manufacturer && cfg.manufacturer !== d.manufacturer) {
          d.manufacturer = cfg.manufacturer;
        }
        if (cfg.label && cfg.label !== d.label) {
          d.label = cfg.label;
        }
      }
    }, 5000);
  }

  /** Stop the server */
  stop(): void {
    if (this.tickTimer) {
      clearInterval(this.tickTimer);
      this.tickTimer = null;
    }
    if (this.syncTimer) {
      clearInterval(this.syncTimer);
      this.syncTimer = null;
    }
    if (this.server) {
      this.server.close(() => {
        console.log('[VFD] Modbus TCP server closed');
      });
      this.server = null;
    }
  }

  // ── Drive management API (for panel) ──

  getDrives(): VFDState[] {
    return Array.from(this.drives.values()).map(d => ({ ...d }));
  }

  getDrive(unitId: number): VFDState | undefined {
    const d = this.drives.get(unitId);
    return d ? { ...d } : undefined;
  }

  addDrive(unitId: number, label: string, nameplate?: Partial<VFDState>): boolean {
    if (this.drives.has(unitId) || unitId < 1 || unitId > 247) return false;
    const mfg = nameplate?.manufacturer ?? 'abb-acs310';
    const d = { ...defaultDrive(unitId, label, mfg), ...nameplate };
    this.drives.set(unitId, d);
    this.persist();
    console.log(`[VFD] Added drive: unit ${unitId} (${label})`);
    return true;
  }

  removeDrive(unitId: number): boolean {
    if (!this.drives.has(unitId)) return false;
    this.drives.delete(unitId);
    this.persist();
    console.log(`[VFD] Removed drive: unit ${unitId}`);
    return true;
  }

  updateDrive(unitId: number, updates: Partial<VFDState>): boolean {
    const d = this.drives.get(unitId);
    if (!d) return false;
    // Only allow updating safe fields
    if (updates.label !== undefined) d.label = updates.label;
    if (updates.manufacturer !== undefined) d.manufacturer = updates.manufacturer;
    if (updates.ratedFreqHz !== undefined) d.ratedFreqHz = updates.ratedFreqHz;
    if (updates.ratedCurrentA !== undefined) d.ratedCurrentA = updates.ratedCurrentA;
    if (updates.ratedPowerkW !== undefined) d.ratedPowerkW = updates.ratedPowerkW;
    if (updates.ratedVoltage !== undefined) d.ratedVoltage = updates.ratedVoltage;
    if (updates.rampUpTimeSec !== undefined) d.rampUpTimeSec = updates.rampUpTimeSec;
    if (updates.rampDownTimeSec !== undefined) d.rampDownTimeSec = updates.rampDownTimeSec;
    if (updates.minFreqHz !== undefined) d.minFreqHz = updates.minFreqHz;
    if (updates.maxFreqHz !== undefined) d.maxFreqHz = updates.maxFreqHz;
    this.persist();
    return true;
  }

  /** Inject a fault into a drive (for testing) */
  injectFault(unitId: number, faultCode: number): boolean {
    const d = this.drives.get(unitId);
    if (!d) return false;
    d.faulted = true;
    d.lastFaultCode = faultCode;
    d.running = false;
    console.log(`[VFD] Fault injected: unit ${unitId} code 0x${faultCode.toString(16)}`);
    return true;
  }

  /** Write control word + speed ref directly (for panel testing) */
  setControl(unitId: number, controlWord: number, speedRefPercent?: number): boolean {
    const d = this.drives.get(unitId);
    if (!d) return false;
    d.controlWord = controlWord & 0xFFFF;
    if (speedRefPercent !== undefined) {
      d.speedRefPercent = Math.max(0, Math.min(10000, speedRefPercent));
    }
    return true;
  }
}

// ── ABB ACS310 Fault Code Table (common ones) ──
export const ACS310_FAULTS: Record<number, string> = {
  0x0001: 'Overcurrent',
  0x0002: 'DC overvoltage',
  0x0003: 'Device overtemperature',
  0x0004: 'Short circuit',
  0x0005: 'Analog input loss',
  0x0007: 'Motor overtemperature',
  0x0008: 'Motor stall',
  0x0009: 'Underload',
  0x000A: 'Output phase loss',
  0x000B: 'Input phase loss',
  0x000C: 'Earth fault',
  0x0032: 'External fault',
  0x0037: 'Fieldbus comm loss',
  0x0038: 'Motor phase order',
  0x005A: 'ID run incomplete',
};
