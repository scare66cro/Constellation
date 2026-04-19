/**
 * VFD Drive Simulator — Native Modbus TCP server
 * ────────────────────────────────────────────────
 * Simulates ABB ACS310 variable frequency drives on a Modbus TCP port
 * (default 5020).  The VFDClient in the bridge server connects here
 * and auto-discovers drives by scanning unit IDs.
 *
 * Uses native TCP (no modbus-serial dependency) — same pattern as
 * the orbit Modbus TCP server in orbitSimulator.ts.
 *
 * Default drives (simulated):
 *   Unit 1: Supply Fan 1 VFD  (20 HP, 460V, 60Hz)
 *   Unit 2: Supply Fan 2 VFD  (15 HP, 460V, 60Hz)
 *   Unit 3: Supply Fan 3 VFD  (10 HP, 460V, 60Hz)
 *
 * Register map (ABB ACS310 + FENA-01 layout):
 *   0:      Control Word (R/W)
 *   1:      Speed Reference 0–10000 (R/W)
 *   2:      Status Word (R)
 *   3:      Actual Speed 0–10000 (R)
 *   100-108: Group 01 actual signals (freq, rpm, current, etc.)
 *   400:    Active fault code
 *   2001-02: Min/max frequency limits
 *   2202-03: Accel/decel ramp times
 *   9901-05: Motor nameplate
 */

import * as net from 'net';
import * as fs from 'fs';
import * as path from 'path';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const CONFIG_DIR = path.resolve(__dirname, '..', '.sim-config');

function loadConfig<T>(key: string): T | undefined {
  const file = path.join(CONFIG_DIR, `${key}.json`);
  try { return JSON.parse(fs.readFileSync(file, 'utf8')); } catch { return undefined; }
}

function saveConfig<T>(key: string, value: T): void {
  if (!fs.existsSync(CONFIG_DIR)) fs.mkdirSync(CONFIG_DIR, { recursive: true });
  fs.writeFileSync(path.join(CONFIG_DIR, `${key}.json`), JSON.stringify(value, null, 2));
}

// ─── CRC-16 (Modbus RTU) ──────────────────────────────────────────────────
function vfdCrc16(buf: Buffer): number {
  let crc = 0xFFFF;
  for (let i = 0; i < buf.length; i++) {
    crc ^= buf[i];
    for (let j = 0; j < 8; j++) {
      if (crc & 1) { crc = (crc >> 1) ^ 0xA001; }
      else { crc >>= 1; }
    }
  }
  return crc & 0xFFFF;
}

// ─── Modbus TCP Constants ──────────────────────────────────────────────────
const MBAP_HEADER_LEN  = 7;
const FC_READ_HOLDING_REGS    = 0x03;
const FC_READ_INPUT_REGS      = 0x04;
const FC_WRITE_SINGLE_REG     = 0x06;
const FC_WRITE_MULTIPLE_REGS  = 0x10;

// ─── Drive State ───────────────────────────────────────────────────────────

interface VFDDrive {
  unitId: number;
  label: string;
  controlWord: number;
  speedRefPercent: number;     // 0–10000 (0.00–100.00%)
  actualSpeedPercent: number;  // 0–10000
  outputFreqHz: number;        // 0.1 Hz
  motorSpeedRpm: number;
  motorCurrentA: number;       // 0.1 A
  motorTorquePercent: number;  // 0.1 %
  motorPowerkW: number;        // 0.1 kW
  dcBusVoltage: number;        // V
  outputVoltage: number;       // 0.1 V
  driveTemp: number;           // °C
  lastFaultCode: number;
  running: boolean;
  faulted: boolean;
  warning: boolean;
  direction: 0 | 1;
  // Nameplate
  ratedCurrentA: number;       // 0.1 A
  ratedVoltage: number;        // V
  ratedFreqHz: number;         // 0.1 Hz
  ratedSpeedRpm: number;
  ratedPowerkW: number;        // 0.1 kW
  rampUpTimeSec: number;
  rampDownTimeSec: number;
  minFreqHz: number;           // 0.1 Hz
  maxFreqHz: number;           // 0.1 Hz
}

function defaultDrive(unitId: number, label: string, hp: number): VFDDrive {
  // Convert HP to kW: 1 HP ≈ 0.746 kW, stored as 0.1kW
  const kw10 = Math.round(hp * 7.46);
  // Typical current for 460V at the given HP
  const amps10 = Math.round((hp * 746) / (460 * 0.85 * Math.sqrt(3)) * 10);
  return {
    unitId, label,
    controlWord: 0, speedRefPercent: 0, actualSpeedPercent: 0,
    outputFreqHz: 0, motorSpeedRpm: 0, motorCurrentA: 0,
    motorTorquePercent: 0, motorPowerkW: 0,
    dcBusVoltage: 650, outputVoltage: 0, driveTemp: 35,
    lastFaultCode: 0,
    running: false, faulted: false, warning: false, direction: 0,
    ratedCurrentA: amps10,
    ratedVoltage: 460,
    ratedFreqHz: 600,      // 60.0 Hz
    ratedSpeedRpm: 1760,
    ratedPowerkW: kw10,
    rampUpTimeSec: 10,
    rampDownTimeSec: 10,
    minFreqHz: 0,
    maxFreqHz: 600,        // 60.0 Hz
  };
}

// ─── Simulation Tick ───────────────────────────────────────────────────────

const TICK_MS = 100;

function tickDrive(d: VFDDrive): void {
  // Apply control word (ABB ACS310 bit layout)
  const cw = d.controlWord;
  if ((cw & 0x0080) && d.faulted) { d.faulted = false; d.lastFaultCode = 0; }
  if (!(cw & 0x0002)) d.running = false;  // OFF2
  if (!(cw & 0x0004)) d.running = false;  // OFF3
  if (!d.faulted) {
    const run = !!(cw & 0x0001);
    const en  = !!(cw & 0x0008);
    if (run && en) d.running = true;
    else if (!run) d.running = false;
  }
  d.direction = (cw & 0x0800) ? 1 : 0;

  // Speed ramp
  const target = d.running ? d.speedRefPercent : 0;
  const dt = TICK_MS / 1000;
  if (d.actualSpeedPercent < target) {
    const step = (10000 / d.rampUpTimeSec) * dt;
    d.actualSpeedPercent = Math.min(target, d.actualSpeedPercent + step);
  } else if (d.actualSpeedPercent > target) {
    const step = (10000 / d.rampDownTimeSec) * dt;
    d.actualSpeedPercent = Math.max(target, d.actualSpeedPercent - step);
  }

  // Derived outputs
  const speedFrac = d.actualSpeedPercent / 10000;
  let outFreq = Math.round(d.ratedFreqHz * speedFrac);
  if (outFreq > 0 && outFreq < d.minFreqHz) outFreq = d.minFreqHz;
  if (outFreq > d.maxFreqHz) outFreq = d.maxFreqHz;
  d.outputFreqHz = outFreq;
  d.motorSpeedRpm = Math.round(d.ratedSpeedRpm * speedFrac);
  d.outputVoltage = Math.round(d.ratedVoltage * speedFrac * 10);
  const loadFrac = speedFrac > 0.01 ? 0.15 + 0.85 * speedFrac * speedFrac : 0;
  d.motorCurrentA = Math.round(d.ratedCurrentA * loadFrac);
  d.motorTorquePercent = Math.round(speedFrac * speedFrac * 1000);
  d.motorPowerkW = Math.round(d.ratedPowerkW * speedFrac * speedFrac * speedFrac);
  d.dcBusVoltage = 650 + Math.round((Math.random() - 0.5) * 4);
  d.warning = speedFrac > 0.90;
  d.driveTemp = 35 + Math.round((speedFrac > 0.01 ? 0.15 + 0.85 * speedFrac * speedFrac : 0) * 25);
}

// ─── Modbus Register Read/Write ────────────────────────────────────────────

function buildStatusWord(d: VFDDrive): number {
  let w = 0;
  if (!d.faulted) w |= 0x0003;              // Ready + ReadyToOp
  if (d.running && d.actualSpeedPercent > 0) w |= 0x0004;  // Running
  if (d.faulted) w |= 0x0048;               // Fault + SwitchOnInhibit
  w |= 0x0030;                              // OFF2 + OFF3 always set
  if (d.warning) w |= 0x0080;               // Warning
  if (Math.abs(d.actualSpeedPercent - d.speedRefPercent) < 50) w |= 0x0100; // AtRef
  w |= 0x0200;                              // Remote
  if (d.actualSpeedPercent > 9500) w |= 0x0400; // AboveLimit
  if (d.direction) w |= 0x0800;             // Direction
  return w;
}

function readRegister(d: VFDDrive, addr: number): number {
  switch (addr) {
    case 0:    return d.controlWord;
    case 1:    return Math.round(d.speedRefPercent);
    case 2:    return buildStatusWord(d);
    case 3:    return Math.round(d.actualSpeedPercent);
    case 100:  return d.outputFreqHz;
    case 101:  return d.outputFreqHz;   // freq ref mirrors output
    case 102:  return d.motorSpeedRpm;
    case 103:  return d.motorCurrentA;
    case 104:  return d.motorTorquePercent;
    case 105:  return d.motorPowerkW;
    case 106:  return d.dcBusVoltage;
    case 107:  return d.outputVoltage;
    case 108:  return d.driveTemp;
    case 400:  return d.lastFaultCode;
    case 401:  return 0;
    case 402:  return 0;
    case 2001: return d.minFreqHz;
    case 2002: return d.maxFreqHz;
    case 2202: return Math.round(d.rampUpTimeSec * 10);
    case 2203: return Math.round(d.rampDownTimeSec * 10);
    case 9901: return d.ratedCurrentA;
    case 9902: return d.ratedVoltage;
    case 9903: return d.ratedFreqHz;
    case 9904: return d.ratedSpeedRpm;
    case 9905: return d.ratedPowerkW;
    default:   return 0xFFFF;  // illegal address
  }
}

function writeRegister(d: VFDDrive, addr: number, value: number): boolean {
  switch (addr) {
    case 0:    d.controlWord = value & 0xFFFF; return true;
    case 1:    d.speedRefPercent = Math.max(0, Math.min(10000, value)); return true;
    case 2001: d.minFreqHz = Math.max(0, value); return true;
    case 2002: d.maxFreqHz = Math.max(0, value); return true;
    case 2202: d.rampUpTimeSec = Math.max(0.1, value / 10); return true;
    case 2203: d.rampDownTimeSec = Math.max(0.1, value / 10); return true;
    case 9901: d.ratedCurrentA = value; return true;
    case 9902: d.ratedVoltage = value; return true;
    case 9903: d.ratedFreqHz = value; return true;
    case 9904: d.ratedSpeedRpm = value; return true;
    case 9905: d.ratedPowerkW = value; return true;
    default:   return false;
  }
}

// Known valid register ranges for address validation
function isValidReadAddr(addr: number): boolean {
  if (addr >= 0 && addr <= 3) return true;
  if (addr >= 100 && addr <= 108) return true;
  if (addr >= 400 && addr <= 402) return true;
  if (addr >= 2001 && addr <= 2002) return true;
  if (addr >= 2202 && addr <= 2203) return true;
  if (addr >= 9901 && addr <= 9905) return true;
  return false;
}

// ─── VFD Simulator Class ──────────────────────────────────────────────────

export class VFDSimulator {
  private drives: Map<number, VFDDrive> = new Map();
  private server: net.Server | null = null;
  private rtuServer: net.Server | null = null;
  private tickTimer: ReturnType<typeof setInterval> | null = null;
  private port: number;

  constructor(port = 5020) {
    this.port = port;

    // Load persisted drives or create defaults
    const saved = loadConfig<VFDDrive[]>('vfd-drives');
    if (saved && Array.isArray(saved) && saved.length > 0) {
      for (const d of saved) {
        this.drives.set(d.unitId, { ...defaultDrive(d.unitId, d.label, 20), ...d });
      }
      console.log(`[VFD] Loaded ${saved.length} drive(s) from config`);
    } else {
      // Default: 3 supply fan drives (exhaust is gravity louvers)
      this.drives.set(1, defaultDrive(1, 'Supply Fan 1 VFD', 20));
      this.drives.set(2, defaultDrive(2, 'Supply Fan 2 VFD', 15));
      this.drives.set(3, defaultDrive(3, 'Supply Fan 3 VFD', 10));
      this.persist();
      console.log('[VFD] Default config: 3 supply fan drives (20HP, 15HP, 10HP)');
    }
  }

  private persist(): void {
    saveConfig('vfd-drives', Array.from(this.drives.values()));
  }

  start(): void {
    // Start Modbus TCP server
    this.server = net.createServer((socket) => {
      let buffer = Buffer.alloc(0);
      socket.on('data', (chunk) => {
        buffer = Buffer.concat([buffer, chunk]);
        while (buffer.length >= MBAP_HEADER_LEN) {
          const pduLen = buffer.readUInt16BE(4);
          const frameLen = 6 + pduLen;
          if (buffer.length < frameLen) break;
          const frame = buffer.subarray(0, frameLen);
          buffer = buffer.subarray(frameLen);
          const resp = this.handleFrame(frame);
          if (resp) {
            try { socket.write(resp); } catch {}
          }
        }
      });
      socket.on('error', () => {});
    });

    this.server.listen(this.port, '0.0.0.0', () => {
      console.log(`[VFD] Modbus TCP server listening on :${this.port}`);
      const labels = Array.from(this.drives.values()).map(d => `unit ${d.unitId} (${d.label})`);
      console.log(`[VFD] Drives: ${labels.join(', ')}`);
    });

    this.server.on('error', (err: NodeJS.ErrnoException) => {
      if (err.code === 'EADDRINUSE') {
        console.error(`[VFD] Port ${this.port} in use — VFD simulator disabled`);
      } else {
        console.error(`[VFD] Server error: ${err.message}`);
      }
    });

    // Start simulation tick
    this.tickTimer = setInterval(() => {
      for (const d of this.drives.values()) {
        tickDrive(d);
      }
    }, TICK_MS);
  }

  stop(): void {
    if (this.tickTimer) { clearInterval(this.tickTimer); this.tickTimer = null; }
    if (this.server) { this.server.close(); this.server = null; }
    if (this.rtuServer) { this.rtuServer.close(); this.rtuServer = null; }
    this.persist();
  }

  /**
   * Start a Modbus RTU over TCP server — the orbit connects here as an
   * RTU client, just like it connects to the sensor board sim.
   * This mirrors production: VFDs sit on the orbit's RS-485 port B.
   */
  startRtu(rtuPort: number): void {
    this.rtuServer = net.createServer((socket) => {
      let buf = Buffer.alloc(0);
      socket.on('data', (chunk) => {
        buf = Buffer.concat([buf, chunk]);
        // RTU frames: minimum 8 bytes for FC03/FC06 requests
        while (buf.length >= 8) {
          const resp = this.handleRtuFrame(buf);
          if (resp) {
            buf = buf.subarray(resp.consumed);
            try { socket.write(resp.frame); } catch {}
          } else {
            break; // incomplete frame
          }
        }
      });
      socket.on('error', () => {});
    });

    this.rtuServer.listen(rtuPort, '0.0.0.0', () => {
      console.log(`[VFD] Modbus RTU/TCP server listening on :${rtuPort}`);
    });

    this.rtuServer.on('error', (err: NodeJS.ErrnoException) => {
      if (err.code === 'EADDRINUSE') {
        console.error(`[VFD] RTU port ${rtuPort} in use — VFD RTU disabled`);
      } else {
        console.error(`[VFD] RTU server error: ${err.message}`);
      }
    });
  }

  /** Handle one Modbus RTU frame. Returns response frame + bytes consumed, or null if incomplete. */
  private handleRtuFrame(buf: Buffer): { frame: Buffer; consumed: number } | null {
    if (buf.length < 8) return null;

    const unitId = buf[0];
    const fc = buf[1];
    const drive = this.drives.get(unitId);

    let reqLen: number;
    switch (fc) {
      case 0x03: case 0x04: reqLen = 8; break;  // FC03/04: unit+fc+addr(2)+qty(2)+crc(2)
      case 0x06: reqLen = 8; break;              // FC06: unit+fc+addr(2)+val(2)+crc(2)
      case 0x10: {                               // FC16: unit+fc+addr(2)+qty(2)+bytes(1)+data(N)+crc(2)
        if (buf.length < 7) return null;
        const byteCount = buf[6];
        reqLen = 7 + byteCount + 2;
        break;
      }
      default:
        // Unknown FC — consume 8 bytes and return exception
        return { frame: this.buildRtuException(unitId, fc, 0x01), consumed: 8 };
    }

    if (buf.length < reqLen) return null;

    // CRC check
    const payloadLen = reqLen - 2;
    const receivedCrc = buf.readUInt16LE(payloadLen);
    const computedCrc = vfdCrc16(buf.subarray(0, payloadLen));
    if (receivedCrc !== computedCrc) {
      return null; // bad CRC — discard and wait for more data
    }

    if (!drive) {
      return { frame: this.buildRtuException(unitId, fc, 0x0B), consumed: reqLen };
    }

    let resp: Buffer;
    switch (fc) {
      case 0x03: case 0x04: {
        const startAddr = buf.readUInt16BE(2);
        const quantity = buf.readUInt16BE(4);
        const data = Buffer.alloc(quantity * 2);
        for (let i = 0; i < quantity; i++) {
          if (!isValidReadAddr(startAddr + i)) {
            return { frame: this.buildRtuException(unitId, fc, 0x02), consumed: reqLen };
          }
          data.writeUInt16BE(readRegister(drive, startAddr + i) & 0xFFFF, i * 2);
        }
        // Response: unit+fc+byteCount+data+crc
        const r = Buffer.alloc(3 + quantity * 2 + 2);
        r[0] = unitId; r[1] = fc; r[2] = quantity * 2;
        data.copy(r, 3);
        r.writeUInt16LE(vfdCrc16(r.subarray(0, 3 + quantity * 2)), 3 + quantity * 2);
        resp = r;
        break;
      }
      case 0x06: {
        const addr = buf.readUInt16BE(2);
        const value = buf.readUInt16BE(4);
        if (!writeRegister(drive, addr, value)) {
          return { frame: this.buildRtuException(unitId, fc, 0x02), consumed: reqLen };
        }
        // Echo request back (without old CRC, recalculate)
        const r = Buffer.alloc(8);
        buf.copy(r, 0, 0, 6);
        r.writeUInt16LE(vfdCrc16(r.subarray(0, 6)), 6);
        resp = r;
        break;
      }
      case 0x10: {
        const startAddr = buf.readUInt16BE(2);
        const quantity = buf.readUInt16BE(4);
        for (let i = 0; i < quantity; i++) {
          const val = buf.readUInt16BE(7 + i * 2);
          if (!writeRegister(drive, startAddr + i, val)) {
            return { frame: this.buildRtuException(unitId, fc, 0x02), consumed: reqLen };
          }
        }
        // Response: unit+fc+startAddr+qty+crc
        const r = Buffer.alloc(8);
        r[0] = unitId; r[1] = fc;
        r.writeUInt16BE(startAddr, 2);
        r.writeUInt16BE(quantity, 4);
        r.writeUInt16LE(vfdCrc16(r.subarray(0, 6)), 6);
        resp = r;
        break;
      }
      default:
        return { frame: this.buildRtuException(unitId, fc, 0x01), consumed: reqLen };
    }

    return { frame: resp, consumed: reqLen };
  }

  private buildRtuException(unitId: number, fc: number, exCode: number): Buffer {
    const r = Buffer.alloc(5);
    r[0] = unitId;
    r[1] = fc | 0x80;
    r[2] = exCode;
    r.writeUInt16LE(vfdCrc16(r.subarray(0, 3)), 3);
    return r;
  }

  getDrives(): VFDDrive[] { return Array.from(this.drives.values()); }

  /** Add or update a drive. Returns the drive state. */
  setDrive(unitId: number, label: string, hp: number): VFDDrive {
    let d = this.drives.get(unitId);
    if (d) {
      d.label = label;
      // Update nameplate if HP changed
      const kw10 = Math.round(hp * 7.46);
      const amps10 = Math.round((hp * 746) / (460 * 0.85 * Math.sqrt(3)) * 10);
      d.ratedPowerkW = kw10;
      d.ratedCurrentA = amps10;
    } else {
      d = defaultDrive(unitId, label, hp);
      this.drives.set(unitId, d);
    }
    this.persist();
    console.log(`[VFD] Drive set: unit ${unitId} "${label}" ${hp}HP`);
    return d;
  }

  /** Remove a drive by unit ID. */
  removeDrive(unitId: number): boolean {
    if (!this.drives.has(unitId)) return false;
    this.drives.delete(unitId);
    this.persist();
    console.log(`[VFD] Drive removed: unit ${unitId}`);
    return true;
  }

  injectFault(unitId: number, faultCode: number): boolean {
    const d = this.drives.get(unitId);
    if (!d) return false;
    d.faulted = true;
    d.lastFaultCode = faultCode;
    d.running = false;
    console.log(`[VFD] Fault injected: unit ${unitId} code ${faultCode}`);
    return true;
  }

  clearFault(unitId: number): boolean {
    const d = this.drives.get(unitId);
    if (!d) return false;
    if (!d.faulted) return true; // already clear
    d.faulted = false;
    d.lastFaultCode = 0;
    console.log(`[VFD] Fault cleared: unit ${unitId}`);
    return true;
  }

  // ─── Modbus TCP Frame Handler ──────────────────────────────────────────

  private handleFrame(frame: Buffer): Buffer | null {
    if (frame.length < MBAP_HEADER_LEN + 1) return null;
    const unitId = frame[6];
    const fc = frame[7];
    const drive = this.drives.get(unitId);
    if (!drive) return this.makeException(frame, fc, 0x0B); // Gateway target device failed

    switch (fc) {
      case FC_READ_HOLDING_REGS:
      case FC_READ_INPUT_REGS:
        return this.handleReadRegs(frame, drive);
      case FC_WRITE_SINGLE_REG:
        return this.handleWriteSingleReg(frame, drive);
      case FC_WRITE_MULTIPLE_REGS:
        return this.handleWriteMultipleRegs(frame, drive);
      default:
        return this.makeException(frame, fc, 0x01); // Illegal function
    }
  }

  private handleReadRegs(frame: Buffer, drive: VFDDrive): Buffer {
    const startAddr = frame.readUInt16BE(8);
    const quantity = frame.readUInt16BE(10);
    const fc = frame[7];

    // Validate address range
    for (let i = 0; i < quantity; i++) {
      if (!isValidReadAddr(startAddr + i)) {
        return this.makeException(frame, fc, 0x02); // Illegal data address
      }
    }

    const data = Buffer.alloc(quantity * 2);
    for (let i = 0; i < quantity; i++) {
      const val = readRegister(drive, startAddr + i);
      data.writeUInt16BE(val & 0xFFFF, i * 2);
    }

    // MBAP header + unitId + fc + byteCount + data
    const resp = Buffer.alloc(MBAP_HEADER_LEN + 2 + quantity * 2);
    frame.copy(resp, 0, 0, 4);                     // transaction + protocol
    resp.writeUInt16BE(2 + 1 + quantity * 2, 4);    // length
    resp[6] = frame[6];                              // unit ID
    resp[7] = fc;                                    // function code
    resp[8] = quantity * 2;                          // byte count
    data.copy(resp, 9);
    return resp;
  }

  private handleWriteSingleReg(frame: Buffer, drive: VFDDrive): Buffer {
    const addr = frame.readUInt16BE(8);
    const value = frame.readUInt16BE(10);



    if (!writeRegister(drive, addr, value)) {
      return this.makeException(frame, FC_WRITE_SINGLE_REG, 0x02);
    }

    // Echo request as response (per Modbus spec)
    return Buffer.from(frame);
  }

  private handleWriteMultipleRegs(frame: Buffer, drive: VFDDrive): Buffer {
    const startAddr = frame.readUInt16BE(8);
    const quantity = frame.readUInt16BE(10);



    for (let i = 0; i < quantity; i++) {
      const addr = startAddr + i;
      const value = frame.readUInt16BE(13 + i * 2);
      writeRegister(drive, addr, value);
    }

    // Response: echo first 12 bytes
    const resp = Buffer.alloc(12);
    frame.copy(resp, 0, 0, 12);
    resp.writeUInt16BE(6, 4); // length = 6
    return resp;
  }

  private makeException(frame: Buffer, fc: number, exCode: number): Buffer {
    const resp = Buffer.alloc(9);
    frame.copy(resp, 0, 0, 4);
    resp.writeUInt16BE(3, 4);
    resp[6] = frame[6];
    resp[7] = fc | 0x80;
    resp[8] = exCode;
    return resp;
  }
}
