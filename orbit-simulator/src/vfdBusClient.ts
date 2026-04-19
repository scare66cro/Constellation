/**
 * VFD Bus Client — Orbit's RS-485 Bus B Abstraction
 * ──────────────────────────────────────────────────
 * Manages a Modbus RTU connection to VFD drives on the orbit's
 * second RS-485 port (bus B).  Polls each discovered drive for
 * status / actuals and updates the orbit's internal VFD register
 * block (HR 100-163).
 *
 * Also forwards write commands from the orbit's HR 100+ region
 * down to the actual VFD drives via RTU.
 *
 * Mirrors production hardware: orbit ↔ RS-485 bus B ↔ ACS310 drives.
 */

import { ModbusRtuClient } from './modbusRtuClient.js';
import { EventEmitter } from 'events';

// ─── Constants ─────────────────────────────────────────────────────────────

/** Registers per drive in the orbit's VFD register block */
const REGS_PER_DRIVE = 16;

/** Orbit HR base address for VFD data */
const VFD_REG_BASE = 100;

/** Drive register layout (offset within each 16-register slot):
 *  0  = Control Word (R/W)
 *  1  = Speed Reference 0–10000 (R/W)
 *  2  = Status Word (R)
 *  3  = Actual Speed 0–10000 (R)
 *  4  = Output Frequency (0.1 Hz) (R)
 *  5  = Freq Reference (0.1 Hz) (R)
 *  6  = Motor Speed RPM (R)
 *  7  = Motor Current (0.1 A) (R)
 *  8  = Motor Torque (0.1 %) (R)
 *  9  = Motor Power (0.1 kW) (R)
 * 10  = DC Bus Voltage (V) (R)
 * 11  = Output Voltage (0.1 V) (R)
 * 12  = Drive Temperature (°C) (R)
 * 13  = Active Fault Code (R)
 * 14  = reserved
 * 15  = reserved
 */

/** VFD drive register addresses on the actual ACS310 */
const DRIVE_PROCESS_REGS = { start: 0, count: 4 };   // CW, SpeedRef, StatusWord, ActualSpeed
const DRIVE_ACTUALS_REGS = { start: 100, count: 9 };  // Group 01: Freq..Temp
const DRIVE_FAULT_REG    = 400;

// ─── Types ─────────────────────────────────────────────────────────────────

export interface VFDBusConfig {
  host: string;
  port: number;
  /** Unit IDs of drives to poll */
  driveUnitIds: number[];
  /** Poll interval in ms (default: 500) */
  pollInterval?: number;
  /** RTU response timeout in ms (default: 2000) */
  timeout?: number;
}

export interface VFDDriveData {
  unitId: number;
  online: boolean;
  /** 16-register slot for the orbit register block */
  registers: number[];
}

// ─── VFD Bus Client ────────────────────────────────────────────────────────

export class VFDBusClient extends EventEmitter {
  private client: ModbusRtuClient;
  private config: Required<VFDBusConfig>;
  private drives = new Map<number, VFDDriveData>();
  private pollTimer: ReturnType<typeof setInterval> | null = null;
  private polling = false;
  private connected = false;

  constructor(config: VFDBusConfig) {
    super();
    this.config = {
      pollInterval: 500,
      timeout: 2000,
      ...config,
    };

    this.client = new ModbusRtuClient({
      host: config.host,
      port: config.port,
      timeout: this.config.timeout,
      reconnect: true,
      reconnectInterval: 3000,
    });

    this.client.on('connected', () => {
      this.connected = true;
      console.log(`[VFDBus] Connected to RTU server ${config.host}:${config.port}`);
      this.emit('connected');
    });

    this.client.on('disconnected', () => {
      this.connected = false;
      console.log('[VFDBus] Disconnected from RTU server');
      this.emit('disconnected');
    });

    this.client.on('error', (err: Error) => {
      this.emit('error', err);
    });

    // Initialize drive slots
    for (const uid of config.driveUnitIds) {
      this.drives.set(uid, {
        unitId: uid,
        online: false,
        registers: new Array(REGS_PER_DRIVE).fill(0),
      });
    }
  }

  get isConnected(): boolean { return this.connected; }

  /** Get all drives. */
  getDrives(): VFDDriveData[] {
    return Array.from(this.drives.values());
  }

  /** Get orbit register map for all drives (keyed by orbit HR address). */
  getRegisterMap(): Map<number, number> {
    const map = new Map<number, number>();
    const sortedIds = [...this.config.driveUnitIds].sort((a, b) => a - b);
    for (let idx = 0; idx < sortedIds.length; idx++) {
      const drive = this.drives.get(sortedIds[idx]);
      if (!drive) continue;
      const base = VFD_REG_BASE + idx * REGS_PER_DRIVE;
      for (let r = 0; r < REGS_PER_DRIVE; r++) {
        map.set(base + r, drive.registers[r]);
      }
    }
    return map;
  }

  /** Start connecting and polling. */
  async start(): Promise<void> {
    try {
      await this.client.connect();
    } catch {
      console.log('[VFDBus] Initial connect failed — will retry');
    }
    this.pollTimer = setInterval(() => this.pollAll(), this.config.pollInterval);
  }

  /** Stop polling and disconnect. */
  stop(): void {
    if (this.pollTimer) {
      clearInterval(this.pollTimer);
      this.pollTimer = null;
    }
    this.client.disconnect();
  }

  /**
   * Forward a write command to a VFD drive.
   * Called by the orbit when it receives a write to HR 100+.
   * @param orbitAddr — The orbit HR address (100-163)
   * @param value — The register value to write
   */
  async writeFromOrbit(orbitAddr: number, value: number): Promise<boolean> {
    if (!this.connected) return false;

    const sortedIds = [...this.config.driveUnitIds].sort((a, b) => a - b);
    const offset = orbitAddr - VFD_REG_BASE;
    const driveIdx = Math.floor(offset / REGS_PER_DRIVE);
    const regOffset = offset % REGS_PER_DRIVE;

    if (driveIdx < 0 || driveIdx >= sortedIds.length) return false;

    const unitId = sortedIds[driveIdx];
    const driveAddr = this.orbitOffsetToDriveAddr(regOffset);
    if (driveAddr < 0) return false;

    try {
      await this.client.writeSingleRegister(unitId, driveAddr, value);
      return true;
    } catch (err) {
      console.error(`[VFDBus] Write to unit ${unitId} addr ${driveAddr} failed: ${(err as Error).message}`);
      return false;
    }
  }

  /**
   * Forward a multi-register write to a VFD drive.
   * @param orbitStartAddr — The orbit HR start address
   * @param values — Array of register values
   */
  async writeMultipleFromOrbit(orbitStartAddr: number, values: number[]): Promise<boolean> {
    if (!this.connected) return false;

    const sortedIds = [...this.config.driveUnitIds].sort((a, b) => a - b);
    const offset = orbitStartAddr - VFD_REG_BASE;
    const driveIdx = Math.floor(offset / REGS_PER_DRIVE);
    const regOffset = offset % REGS_PER_DRIVE;

    if (driveIdx < 0 || driveIdx >= sortedIds.length) return false;

    const unitId = sortedIds[driveIdx];
    const driveAddr = this.orbitOffsetToDriveAddr(regOffset);
    if (driveAddr < 0) return false;

    // For multi-reg writes, the addresses must be contiguous on the drive
    // CW+SpeedRef (offset 0,1) → drive addr 0,1 — this is the common case
    const driveAddrs: number[] = [];
    for (let i = 0; i < values.length; i++) {
      const a = this.orbitOffsetToDriveAddr(regOffset + i);
      if (a < 0) return false;
      driveAddrs.push(a);
    }
    // Check contiguity
    for (let i = 1; i < driveAddrs.length; i++) {
      if (driveAddrs[i] !== driveAddrs[0] + i) {
        // Non-contiguous — fall back to individual writes
        for (let j = 0; j < values.length; j++) {
          await this.client.writeSingleRegister(unitId, driveAddrs[j], values[j]);
        }
        return true;
      }
    }

    try {
      await this.client.writeMultipleRegisters(unitId, driveAddrs[0], values);
      return true;
    } catch (err) {
      console.error(`[VFDBus] Multi-write to unit ${unitId} failed: ${(err as Error).message}`);
      return false;
    }
  }

  // ─── Polling ──────────────────────────────────────────────────────────

  private async pollAll(): Promise<void> {
    if (this.polling || !this.connected) return;
    this.polling = true;

    for (const unitId of this.config.driveUnitIds) {
      try {
        await this.pollDrive(unitId);
      } catch {
        const drive = this.drives.get(unitId);
        if (drive) drive.online = false;
      }
    }

    this.polling = false;
    this.emit('poll-complete');
  }

  private async pollDrive(unitId: number): Promise<void> {
    const drive = this.drives.get(unitId);
    if (!drive) return;

    // Read process data: CW, SpeedRef, StatusWord, ActualSpeed (HR 0-3)
    const process = await this.client.readHoldingRegisters(
      unitId, DRIVE_PROCESS_REGS.start, DRIVE_PROCESS_REGS.count,
    );

    // Read actuals: Group 01 (HR 100-108)
    const actuals = await this.client.readHoldingRegisters(
      unitId, DRIVE_ACTUALS_REGS.start, DRIVE_ACTUALS_REGS.count,
    );

    // Read fault code (HR 400)
    const fault = await this.client.readHoldingRegisters(unitId, DRIVE_FAULT_REG, 1);

    // Map into the 16-register slot
    drive.registers[0]  = process[0];  // CW
    drive.registers[1]  = process[1];  // SpeedRef
    drive.registers[2]  = process[2];  // StatusWord
    drive.registers[3]  = process[3];  // ActualSpeed
    drive.registers[4]  = actuals[0];  // OutputFreq
    drive.registers[5]  = actuals[1];  // FreqRef
    drive.registers[6]  = actuals[2];  // MotorRPM
    drive.registers[7]  = actuals[3];  // MotorCurrent
    drive.registers[8]  = actuals[4];  // MotorTorque
    drive.registers[9]  = actuals[5];  // MotorPower
    drive.registers[10] = actuals[6];  // DCBusVoltage
    drive.registers[11] = actuals[7];  // OutputVoltage
    drive.registers[12] = actuals[8];  // DriveTemp
    drive.registers[13] = fault[0];    // FaultCode

    drive.online = true;
  }

  /** Map orbit slot offset (0-15) to actual ACS310 register address. */
  private orbitOffsetToDriveAddr(offset: number): number {
    if (offset >= 0 && offset <= 3) return offset;           // CW, SpeedRef, StatusWord, ActualSpeed
    if (offset >= 4 && offset <= 12) return 96 + offset;     // 4→100, 5→101, ... 12→108
    if (offset === 13) return DRIVE_FAULT_REG;                // Fault code
    return -1; // reserved or invalid
  }
}
