/**
 * VFD Modbus TCP Client — polls ACS310 drives and caches their state.
 *
 * This runs inside the bridge server and talks Modbus TCP to either:
 *   - The ACS310 simulator (rs485Responder VFD on port 5020), or
 *   - A real ACS310 with FENA-01 Modbus TCP adapter
 *
 * Auto-discovers drives by scanning unit IDs 1–maxScanId on startup
 * and periodically.  No hardcoded unit list needed.
 *
 * It exposes a simple API consumed by apiRoutes and wsManager:
 *   - getDrives()          → snapshot of all drive states
 *   - writeDrive(unit, cw, ref) → send control word + speed ref
 *   - writeParam(unit, addr, value) → write any single register
 *
 * Register addresses match a real ACS310/FENA-01:
 *   Process data:  0-3      (CW, SpeedRef, StatusWord, ActualSpeed)
 *   Group 01:      100-108  (Freq, FreqRef, RPM, Current, Torque, Power, DCBus, OutputV, Temp)
 *   Group 04:      400      (Active fault code)
 *   Group 20:      2001-2002 (Min/Max frequency)
 *   Group 22:      2202-2203 (Accel/Decel ramp time)
 *   Group 99:      9901-9905 (Motor nameplate)
 */

import ModbusRTU from 'modbus-serial';
import { getProfile, type VFDManufacturer } from './vfdRegisterMaps.js';

export type { VFDManufacturer } from './vfdRegisterMaps.js';

// ── Types ──

export interface VFDDriveSnapshot {
  unitId: number;
  online: boolean;
  label: string;
  manufacturer: VFDManufacturer;
  // Process data
  controlWord: number;
  speedRefPercent: number;   // 0–10000 (0.00–100.00%)
  statusWord: number;
  actualSpeedPercent: number;// 0–10000
  // Group 01 actual signals
  outputFreqHz: number;      // 0.1 Hz
  freqRefHz: number;         // 0.1 Hz
  motorSpeedRpm: number;     // rpm
  motorCurrentA: number;     // 0.1 A
  motorTorquePercent: number;// 0.1 %
  motorPowerkW: number;      // 0.1 kW
  dcBusVoltage: number;      // V
  outputVoltage: number;     // 0.1 V
  driveTemp: number;         // °C
  // Group 04
  faultCode: number;
  // Group 20: Frequency limits
  minFreqHz: number;         // 0.1 Hz
  maxFreqHz: number;         // 0.1 Hz
  // Group 22: Ramp times
  rampUpTime: number;        // 0.1 s
  rampDownTime: number;      // 0.1 s
  // Group 99: Motor nameplate
  ratedCurrentA: number;     // 0.1 A
  ratedVoltage: number;      // V
  ratedFreqHz: number;       // 0.1 Hz
  ratedSpeedRpm: number;     // rpm
  ratedPowerkW: number;      // 0.1 kW
  // Derived helpers
  running: boolean;
  faulted: boolean;
  atReference: boolean;
  direction: number;         // 0=FWD, 1=REV
}

function emptySnapshot(unitId: number): VFDDriveSnapshot {
  return {
    unitId, online: false, label: `VFD Unit ${unitId}`, manufacturer: 'generic',
    controlWord: 0, speedRefPercent: 0, statusWord: 0, actualSpeedPercent: 0,
    outputFreqHz: 0, freqRefHz: 0, motorSpeedRpm: 0, motorCurrentA: 0,
    motorTorquePercent: 0, motorPowerkW: 0, dcBusVoltage: 0, outputVoltage: 0,
    driveTemp: 0, faultCode: 0,
    minFreqHz: 0, maxFreqHz: 0, rampUpTime: 0, rampDownTime: 0,
    ratedCurrentA: 0, ratedVoltage: 0, ratedFreqHz: 0, ratedSpeedRpm: 0, ratedPowerkW: 0,
    running: false, faulted: false, atReference: false, direction: 0,
  };
}

// ── VFD Client ──

export interface VFDClientOptions {
  host: string;
  port: number;
  /** Max unit ID to scan for drives (default 8) */
  maxScanId?: number;
  pollIntervalMs?: number;
}

export class VFDClient {
  private host: string;
  private port: number;
  private maxScanId: number;
  private pollMs: number;
  private client: ModbusRTU;
  private drives: Map<number, VFDDriveSnapshot> = new Map();
  private knownIds: Set<number> = new Set();
  private pollTimer: ReturnType<typeof setInterval> | null = null;
  private scanCounter = 0;
  private polling = false;
  private connected = false;
  private connecting = false;
  private onChange: (() => void) | null = null;
  /** Per-drive commanded state: CW + optional speed ref to re-send each cycle */
  private commandedState: Map<number, { cw: number; ref?: number }> = new Map();
  /** Drives that went offline while running — need fault code read on reconnect */
  private needsFaultRead: Set<number> = new Set();

  constructor(options: VFDClientOptions) {
    this.host = options.host;
    this.port = options.port;
    this.maxScanId = options.maxScanId ?? 8;
    this.pollMs = options.pollIntervalMs ?? 1000;
    this.client = new ModbusRTU();
  }

  /** Register a callback for when drive data changes (for WS broadcast) */
  onUpdate(cb: () => void): void {
    this.onChange = cb;
  }

  async start(): Promise<void> {
    await this.connect();
    // Initial scan to discover all drives
    await this.scanForDrives();
    this.pollTimer = setInterval(() => this.pollAll(), this.pollMs);
    console.log(`[VFDClient] Scanning units 1-${this.maxScanId} at ${this.host}:${this.port}, poll every ${this.pollMs}ms`);
  }

  stop(): void {
    if (this.pollTimer) {
      clearInterval(this.pollTimer);
      this.pollTimer = null;
    }
    try { this.client.close(() => {}); } catch {}
    this.connected = false;
    console.log('[VFDClient] Stopped');
  }

  getDrives(): VFDDriveSnapshot[] {
    return Array.from(this.drives.values());
  }

  getDrive(unitId: number): VFDDriveSnapshot | undefined {
    return this.drives.get(unitId);
  }

  /** Set metadata (label, manufacturer) that isn't available via Modbus */
  setDriveMeta(unitId: number, meta: { label?: string; manufacturer?: VFDManufacturer }): void {
    const snap = this.drives.get(unitId);
    if (!snap) return;
    if (meta.label !== undefined) snap.label = meta.label;
    if (meta.manufacturer !== undefined) snap.manufacturer = meta.manufacturer;
  }

  /** Write control word and optionally speed reference to a drive */
  async writeDrive(unitId: number, controlWord: number, speedRefPercent?: number): Promise<boolean> {
    if (!this.connected) return false;
    try {
      this.client.setID(unitId);
      if (speedRefPercent !== undefined) {
        // FC16: write both CW and SpeedRef in one transaction
        await this.client.writeRegisters(0, [controlWord & 0xFFFF, Math.max(0, Math.min(10000, speedRefPercent))]);
      } else {
        // FC06: write just the control word
        await this.client.writeRegister(0, controlWord & 0xFFFF);
      }
      return true;
    } catch (err: any) {
      console.error(`[VFDClient] Write to unit ${unitId} failed: ${err.message}`);
      return false;
    }
  }

  /** Write a single holding register (for parameters like ramp times, freq limits, nameplate) */
  async writeParam(unitId: number, addr: number, value: number): Promise<boolean> {
    if (!this.connected) return false;
    try {
      this.client.setID(unitId);
      await this.client.writeRegister(addr, value & 0xFFFF);
      return true;
    } catch (err: any) {
      console.error(`[VFDClient] writeParam unit ${unitId} addr ${addr} failed: ${err.message}`);
      return false;
    }
  }

  /**
   * Action-based control — manufacturer-agnostic.
   * The profile translates actions into the correct control-word bits
   * and speed-reference encoding for the target drive.
   */
  async sendAction(
    unitId: number,
    action: 'start' | 'stop' | 'reset' | 'toggle-direction',
    speedRefPercent?: number,
  ): Promise<boolean> {
    const snap = this.drives.get(unitId);
    if (!snap || !this.connected) return false;
    const profile = getProfile(snap.manufacturer);
    try {
      this.client.setID(unitId);
      switch (action) {
        case 'start': {
          const ref = profile.percentToRef(speedRefPercent ?? 0, snap.maxFreqHz);
          if (profile.refAddr === profile.cwAddr + 1) {
            await this.client.writeRegisters(profile.cwAddr,
              [profile.commands.start, ref]);
          } else {
            await this.client.writeRegister(profile.cwAddr, profile.commands.start);
            await this.client.writeRegister(profile.refAddr, ref);
          }
          // Remember commanded state so heartbeat keeps re-sending RUN + ref
          this.commandedState.set(unitId, { cw: profile.commands.start, ref });
          break;
        }
        case 'stop':
          await this.client.writeRegister(profile.cwAddr, profile.commands.stop);
          // Clear commanded state — heartbeat reverts to standby
          this.commandedState.delete(unitId);
          break;
        case 'reset':
          await this.client.writeRegister(profile.cwAddr, profile.commands.reset);
          await new Promise(r => setTimeout(r, 500));
          await this.client.writeRegister(profile.cwAddr, profile.commands.postReset);
          snap.faulted = false;
          snap.faultCode = 0;
          this.needsFaultRead.delete(unitId);
          this.commandedState.delete(unitId);
          console.log(`[VFDClient] Unit ${unitId} fault reset sent`);
          break;
        case 'toggle-direction': {
          const newCw = snap.controlWord ^ (1 << profile.directionBit);
          if (snap.running && speedRefPercent !== undefined) {
            const ref = profile.percentToRef(speedRefPercent, snap.maxFreqHz);
            if (profile.refAddr === profile.cwAddr + 1) {
              await this.client.writeRegisters(profile.cwAddr, [newCw, ref]);
            } else {
              await this.client.writeRegister(profile.cwAddr, newCw);
              await this.client.writeRegister(profile.refAddr, ref);
            }
            this.commandedState.set(unitId, { cw: newCw, ref });
          } else {
            await this.client.writeRegister(profile.cwAddr, newCw);
            this.commandedState.set(unitId, { cw: newCw });
          }
          break;
        }
      }
      return true;
    } catch (err: any) {
      console.error(`[VFDClient] sendAction ${action} unit ${unitId} failed: ${err.message}`);
      return false;
    }
  }

  // ── Private ──

  /** Scan unit IDs 1–maxScanId to discover drives */
  private async scanForDrives(): Promise<void> {
    if (!this.connected) {
      await this.connect();
      if (!this.connected) return;
    }
    for (let uid = 1; uid <= this.maxScanId; uid++) {
      if (this.knownIds.has(uid)) continue; // already tracked
      try {
        this.client.setID(uid);
        await this.client.readHoldingRegisters(0, 1); // just probe for response
        this.knownIds.add(uid);
        if (!this.drives.has(uid)) {
          const snap = emptySnapshot(uid);
          snap.manufacturer = await this.detectManufacturer(uid);
          this.drives.set(uid, snap);
        }
        const mfg = this.drives.get(uid)!.manufacturer;
        console.log(`[VFDClient] Discovered drive at unit ${uid} → ${mfg}`);
      } catch {
        // No response — not a drive at this address
      }
    }
  }

  /**
   * Auto-detect manufacturer by probing register addresses that are
   * valid on one profile but invalid on another.
   *
   *   ABB ACS310:  registers 100-108 (Group 01 actual signals) exist
   *   Phase Tech DXL: register 4 (output current) exists, 100+ invalid
   *
   * Real drives return Modbus exception 02 (Illegal Data Address) for
   * addresses outside their register map.  The simulator mirrors this.
   */
  private async detectManufacturer(unitId: number): Promise<VFDManufacturer> {
    // First check: does register 100 (ABB Group 01.01) exist?
    let hasAbbGroup01 = false;
    let isFmbtStandard = false;
    try {
      this.client.setID(unitId);
      await this.client.readHoldingRegisters(100, 1);
      hasAbbGroup01 = true;
    } catch (err: any) {
      // FMBT-21 in Standard profile returns exception 4 (Slave device failure)
      // for registers it can't forward to the drive.  Exception 2 means
      // the adapter itself doesn't know the address (not FMBT).
      if (err.modbusCode === 4 || err.message?.includes('exception 4')) {
        isFmbtStandard = true;
      }
    }
    if (hasAbbGroup01) {
      // Distinguish ACS310 from ACS380: ACS310 has reg 104 (01.05 torque),
      // ACS380 does not.
      try {
        this.client.setID(unitId);
        await this.client.readHoldingRegisters(104, 1);
        return 'abb-acs310';
      } catch {
        return 'abb-acs380';
      }
    }
    // FMBT-21 Standard profile: only regs 0-3 work, 100+ gives exception 4
    if (isFmbtStandard) {
      return 'abb-acs380';
    }
    try {
      this.client.setID(unitId);
      // Probe DXL operating data — register 4 (output current)
      await this.client.readHoldingRegisters(4, 1);
      // Success → DXL-style register map
      return 'phase-tech-dxl';
    } catch {
      // Register 4 also invalid — unknown layout
    }
    return 'generic';
  }

  private async connect(): Promise<void> {
    if (this.connected || this.connecting) return;
    this.connecting = true;
    try {
      await this.client.connectTCP(this.host, { port: this.port });
      this.connected = true;
      console.log(`[VFDClient] Connected to ${this.host}:${this.port}`);
    } catch (err: any) {
      console.error(`[VFDClient] Connection failed: ${err.message}`);
      this.connected = false;
    } finally {
      this.connecting = false;
    }
  }

  private async pollAll(): Promise<void> {
    if (this.polling) return;  // prevent overlapping poll cycles
    this.polling = true;
    try {
      await this.pollAllInner();
    } finally {
      this.polling = false;
    }
  }

  private async pollAllInner(): Promise<void> {
    if (!this.connected) {
      await this.connect();
      if (!this.connected) return;
    }

    // Rescan for new drives every ~30 poll cycles
    this.scanCounter++;
    if (this.scanCounter >= 30) {
      this.scanCounter = 0;
      await this.scanForDrives();
    }

    if (this.knownIds.size === 0) return;

    let changed = false;
    for (const uid of this.knownIds) {
      try {
        this.client.setID(uid);
        const snap = this.drives.get(uid) ?? emptySnapshot(uid);
        const profile = getProfile(snap.manufacturer);

        // If this drive went offline while running, read fault code FIRST
        // before the heartbeat clears the fault state.
        if (this.needsFaultRead.has(uid)) {
          try {
            const faultResult = await this.client.readHoldingRegisters(profile.faultAddr, 1);
            if (faultResult.data[0]) {
              snap.faultCode = faultResult.data[0];
              snap.faulted = true;
              const faultName = profile.faultNames[faultResult.data[0]]
                ?? `Code 0x${faultResult.data[0].toString(16).padStart(4, '0')}`;
              console.log(`[VFDClient] Unit ${uid} fault code recovered: ${faultName}`);
            }
          } catch { /* drive may still be unreachable */ }
          this.needsFaultRead.delete(uid);
        }

        // Write heartbeat FIRST — ensures the drive gets CW+ref before
        // we spend time on reads.  Keeps the FMBT-21 comm timeout happy.
        const commanded = this.commandedState.get(uid);
        if (commanded) {
          try {
            if (commanded.ref !== undefined && profile.refAddr === profile.cwAddr + 1) {
              await this.client.writeRegisters(profile.cwAddr, [commanded.cw, commanded.ref]);
            } else {
              await this.client.writeRegister(profile.cwAddr, commanded.cw);
            }
          } catch { /* retry next cycle */ }
        } else if (profile.heartbeatCw) {
          try {
            await this.client.writeRegister(profile.cwAddr, profile.heartbeatCw);
          } catch { /* retry next cycle */ }
        }

        // Read each register group defined by the manufacturer profile.
        // Individual group failures are tolerated (e.g. mismatched manufacturer
        // during initial discovery before metadata is loaded).
        for (const spec of profile.reads) {
          try {
            const result = await this.client.readHoldingRegisters(spec.addr, spec.count);
            spec.apply(snap, result.data);
          } catch {
            // Register group not available — leave fields at their previous values
          }
        }

        // Derive convenience flags using manufacturer-specific status-word layout
        profile.deriveFlags(snap);
        snap.online = true;

        this.drives.set(uid, snap);
        changed = true;
      } catch (err: any) {
        const snap = this.drives.get(uid);
        if (snap && snap.online) {
          // Drive was running when it dropped — treat as fault
          if (snap.running) {
            snap.faulted = true;
            this.needsFaultRead.add(uid);
            console.log(`[VFDClient] Unit ${uid} lost comms while running — marked faulted`);
          }
          snap.online = false;
          snap.running = false;
          changed = true;
        }
        // Reconnect on socket errors
        if (err.message?.includes('Port Not Open') || err.code === 'ECONNRESET') {
          this.connected = false;
        }
      }
    }

    if (changed && this.onChange) {
      this.onChange();
    }
  }
}
