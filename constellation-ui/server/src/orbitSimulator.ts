/**
 * Orbit Simulator — Modbus TCP Server
 * ────────────────────────────────────
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
 * The simulator also serves a REST API for the Orbit visual panel to
 * read/write I/O state from the browser.
 *
 * Usage:
 *   npx tsx src/orbitSimulator.ts
 *
 * Env vars:
 *   ORBIT_ID       — dipswitch address 2-33 (default: 2)
 *   ORBIT_TCP_PORT — Modbus TCP listen port (default: 5502)
 *   ORBIT_API_PORT — REST API listen port (default: 9010)
 */

import * as net from 'net';
import http from 'http';
import { EventEmitter } from 'events';
import { saveConfig, loadConfig } from './simConfig.js';

// ─── Modbus TCP Constants ──────────────────────────────────────────────────
const MBAP_HEADER_LEN  = 7;    // Transaction(2) + Protocol(2) + Length(2) + UnitId(1)
const MODBUS_PROTOCOL  = 0;

// Function codes
const FC_READ_COILS            = 0x01;
const FC_READ_DISCRETE_INPUTS  = 0x02;
const FC_READ_HOLDING_REGS     = 0x03;
const FC_READ_INPUT_REGS       = 0x04;
const FC_WRITE_SINGLE_COIL     = 0x05;
const FC_WRITE_SINGLE_REG      = 0x06;
const FC_WRITE_MULTIPLE_COILS  = 0x0F;
const FC_WRITE_MULTIPLE_REGS   = 0x10;

// Exception codes
const EX_ILLEGAL_FUNCTION      = 0x01;
const EX_ILLEGAL_DATA_ADDRESS  = 0x02;
const EX_ILLEGAL_DATA_VALUE    = 0x03;

// ─── Orbit I/O State ───────────────────────────────────────────────────────

export interface OrbitState {
  id: number;               // dipswitch address (2-33)
  ipAddress: string;        // 10.47.27.{id}

  // Digital Inputs (10 channels + E-Stop)
  digitalInputs: boolean[];  // [0..9] = DI1-DI10
  eStop: boolean;            // dedicated E-Stop input

  // Digital Outputs (10 channels + 4 × 24V DC)
  digitalOutputs: boolean[]; // [0..9] = DO1-DO10
  dc24vOutputs: boolean[];   // [0..3] = 24V DC outputs 5-8

  // Analog Outputs (2 channels, 0-10V or 4-20mA)
  analogOutputs: number[];   // [0,1] = value in mV (0-10000) or µA (4000-20000)
  aoModes: ('voltage' | 'current')[]; // jumper-selectable per channel

  // RS-485 Port 1 (VFD RTU)
  vfdRegisters: Map<number, number>;
  vfdActivity: boolean;

  // RS-485 Port 2 (Sensor/Contactor RTU)
  sensorRegisters: Map<number, number>;
  sensorActivity: boolean;

  // Board status
  firmwareVersion: string;
  cpuTemp: number;           // °C
  uptime: number;            // seconds
  commLost: boolean;         // true if no Modbus poll for >5s
  safeMode: boolean;         // fallback mode active
}

const CONFIG_KEY = 'orbit-state';

function createDefaultState(id: number): OrbitState {
  return {
    id,
    ipAddress: `10.47.27.${id}`,
    digitalInputs: new Array(10).fill(false),
    eStop: false,
    digitalOutputs: new Array(10).fill(false),
    dc24vOutputs: new Array(4).fill(false),
    analogOutputs: [0, 0],
    aoModes: ['voltage', 'voltage'],
    vfdRegisters: new Map(),
    vfdActivity: false,
    sensorRegisters: new Map(),
    sensorActivity: false,
    firmwareVersion: '1.0.0',
    cpuTemp: 42,
    uptime: 0,
    commLost: true,
    safeMode: false,
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

  constructor(id: number = 2) {
    super();
    // Load persisted state or create defaults
    const saved = loadConfig<any>(`${CONFIG_KEY}-${id}`);
    if (saved) {
      this.state = {
        ...createDefaultState(id),
        ...saved,
        vfdRegisters: new Map(Object.entries(saved.vfdRegisters ?? {}).map(([k, v]) => [Number(k), Number(v)])),
        sensorRegisters: new Map(Object.entries(saved.sensorRegisters ?? {}).map(([k, v]) => [Number(k), Number(v)])),
      };
    } else {
      this.state = createDefaultState(id);
    }
  }

  /** Start Modbus TCP server + REST API */
  start(modbusPort: number, apiPort: number): void {
    this.startModbusTcp(modbusPort);
    this.startApiServer(apiPort);

    // Uptime counter
    this.uptimeInterval = setInterval(() => {
      this.state.uptime++;
    }, 1000);

    // Watchdog — detect comm loss
    this.watchdogInterval = setInterval(() => {
      const elapsed = Date.now() - this.lastPollTime;
      const wasLost = this.state.commLost;
      this.state.commLost = elapsed > 5000;

      if (this.state.commLost && !wasLost) {
        console.log(`[Orbit ${this.state.id}] Communication lost — entering safe mode`);
        this.state.safeMode = true;
        this.emit('comm-lost');
      } else if (!this.state.commLost && wasLost) {
        console.log(`[Orbit ${this.state.id}] Communication restored`);
        this.state.safeMode = false;
        this.emit('comm-restored');
      }
    }, 1000);

    console.log(`[Orbit ${this.state.id}] Simulator started — Modbus TCP :${modbusPort}, API :${apiPort}`);
    console.log(`[Orbit ${this.state.id}] IP address: ${this.state.ipAddress}`);
  }

  /** Persist state to disk */
  save(): void {
    const serializable = {
      ...this.state,
      vfdRegisters: Object.fromEntries(this.state.vfdRegisters),
      sensorRegisters: Object.fromEntries(this.state.sensorRegisters),
    };
    saveConfig(`${CONFIG_KEY}-${this.state.id}`, serializable);
  }

  /** Shutdown */
  stop(): void {
    this.save();
    if (this.uptimeInterval) clearInterval(this.uptimeInterval);
    if (this.watchdogInterval) clearInterval(this.watchdogInterval);
    this.modbusServer?.close();
    this.apiServer?.close();
    console.log(`[Orbit ${this.state.id}] Simulator stopped`);
  }

  // ─── Modbus TCP Server ─────────────────────────────────────────────────

  private startModbusTcp(port: number): void {
    this.modbusServer = net.createServer((socket) => {
      let buffer = Buffer.alloc(0);

      socket.on('data', (chunk) => {
        buffer = Buffer.concat([buffer, chunk]);
        while (buffer.length >= MBAP_HEADER_LEN) {
          const pduLength = buffer.readUInt16BE(4);
          const frameLen = 6 + pduLength; // MBAP header (6 bytes before length) + PDU
          if (buffer.length < frameLen) break;

          const frame = buffer.subarray(0, frameLen);
          buffer = buffer.subarray(frameLen);

          this.handleModbusFrame(frame, socket);
        }
      });

      socket.on('error', () => {}); // swallow connection resets
    });

    this.modbusServer.listen(port, '0.0.0.0');
  }

  private handleModbusFrame(frame: Buffer, socket: net.Socket): void {
    this.lastPollTime = Date.now();
    this.state.commLost = false;

    const transactionId = frame.readUInt16BE(0);
    const unitId = frame[6];
    const functionCode = frame[7];

    let response: Buffer;

    try {
      switch (functionCode) {
        case FC_READ_COILS:
          response = this.handleReadCoils(frame);
          break;
        case FC_READ_DISCRETE_INPUTS:
          response = this.handleReadDiscreteInputs(frame);
          break;
        case FC_READ_HOLDING_REGS:
          response = this.handleReadHoldingRegs(frame);
          break;
        case FC_READ_INPUT_REGS:
          response = this.handleReadInputRegs(frame);
          break;
        case FC_WRITE_SINGLE_COIL:
          response = this.handleWriteSingleCoil(frame);
          break;
        case FC_WRITE_SINGLE_REG:
          response = this.handleWriteSingleReg(frame);
          break;
        case FC_WRITE_MULTIPLE_COILS:
          response = this.handleWriteMultipleCoils(frame);
          break;
        case FC_WRITE_MULTIPLE_REGS:
          response = this.handleWriteMultipleRegs(frame);
          break;
        default:
          response = this.makeException(frame, EX_ILLEGAL_FUNCTION);
      }
    } catch {
      response = this.makeException(frame, EX_ILLEGAL_DATA_ADDRESS);
    }

    // Apply E-Stop: if E-Stop active, force all outputs off
    if (this.state.eStop) {
      this.state.digitalOutputs.fill(false);
      this.state.dc24vOutputs.fill(false);
      this.state.analogOutputs = [0, 0];
    }

    socket.write(response);
    this.emit('poll', { functionCode, unitId });
  }

  // ─── FC01: Read Coils (Digital Outputs) ────────────────────────────────

  private handleReadCoils(frame: Buffer): Buffer {
    const startAddr = frame.readUInt16BE(8);
    const quantity = frame.readUInt16BE(10);

    // Coils 0-9 = DO1-DO10, 10-13 = 24V DC outputs
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

    // Inputs 0-9 = DI1-DI10, 10 = E-Stop
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
    // 0-1: Analog outputs (value in tenths: 0-10000 for 0-10V, 4000-20000 for 4-20mA)
    if (addr <= 1) return this.state.analogOutputs[addr];

    // 2-3: AO mode (0=voltage, 1=current)
    if (addr <= 3) return this.state.aoModes[addr - 2] === 'current' ? 1 : 0;

    // 100-163: VFD RS-485 passthrough registers
    if (addr >= 100 && addr < 164) return this.state.vfdRegisters.get(addr) ?? 0;

    // 200-263: Sensor RS-485 passthrough registers
    if (addr >= 200 && addr < 264) return this.state.sensorRegisters.get(addr) ?? 0;

    // 40000+: Status registers
    if (addr === 40000) return this.state.id;
    if (addr === 40001) return this.state.eStop ? 1 : 0;
    if (addr === 40002) return this.state.commLost ? 1 : 0;
    if (addr === 40003) return this.state.safeMode ? 1 : 0;
    if (addr === 40004) return Math.round(this.state.cpuTemp * 10);
    if (addr === 40005) return this.state.uptime & 0xFFFF;
    if (addr === 40006) return (this.state.uptime >> 16) & 0xFFFF;

    return 0;
  }

  // ─── FC04: Read Input Registers ────────────────────────────────────────

  private handleReadInputRegs(frame: Buffer): Buffer {
    const startAddr = frame.readUInt16BE(8);
    const quantity = frame.readUInt16BE(10);
    const data = Buffer.alloc(quantity * 2);

    for (let i = 0; i < quantity; i++) {
      const addr = startAddr + i;
      // Input registers mirror holding registers for now
      const val = this.getHoldingRegister(addr);
      data.writeUInt16BE(val & 0xFFFF, i * 2);
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
    return frame; // echo request as response for FC05
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
    } else if (addr >= 200 && addr < 264) {
      this.state.sensorRegisters.set(addr, value);
      this.state.sensorActivity = true;
    } else {
      return this.makeException(frame, EX_ILLEGAL_DATA_ADDRESS);
    }

    this.emit('output-change', { type: 'register', addr, value });
    return frame; // echo request as response for FC06
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

    // Response: echo first 12 bytes (MBAP header + FC + start + quantity)
    const resp = Buffer.alloc(12);
    frame.copy(resp, 0, 0, 12);
    resp.writeUInt16BE(6, 4); // length = unit(1) + fc(1) + start(2) + qty(2)
    return resp;
  }

  // ─── FC10: Write Multiple Registers ───────────────────────────────────

  private handleWriteMultipleRegs(frame: Buffer): Buffer {
    const startAddr = frame.readUInt16BE(8);
    const quantity = frame.readUInt16BE(10);

    for (let i = 0; i < quantity; i++) {
      const addr = startAddr + i;
      const value = frame.readUInt16BE(13 + i * 2);

      if (addr <= 1) {
        this.state.analogOutputs[addr] = value;
      } else if (addr <= 3) {
        this.state.aoModes[addr - 2] = value === 1 ? 'current' : 'voltage';
      } else if (addr >= 100 && addr < 164) {
        this.state.vfdRegisters.set(addr, value);
      } else if (addr >= 200 && addr < 264) {
        this.state.sensorRegisters.set(addr, value);
      }
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
    // Copy transaction ID, protocol ID, unit ID from request
    request.copy(resp, 0, 0, 2); // transaction ID
    resp.writeUInt16BE(MODBUS_PROTOCOL, 2);
    resp.writeUInt16BE(1 + 1 + pdu.length, 4); // length = unitId + FC + pdu
    resp[6] = request[6]; // unit ID
    resp[7] = request[7]; // function code
    pdu.copy(resp, 8);
    return resp;
  }

  private makeException(request: Buffer, exCode: number): Buffer {
    const resp = Buffer.alloc(MBAP_HEADER_LEN + 2);
    request.copy(resp, 0, 0, 2);
    resp.writeUInt16BE(MODBUS_PROTOCOL, 2);
    resp.writeUInt16BE(3, 4); // length = unitId + FC + exCode
    resp[6] = request[6];
    resp[7] = request[7] | 0x80; // exception flag
    resp[8] = exCode;
    return resp;
  }

  // ─── REST API Server ──────────────────────────────────────────────────

  private startApiServer(port: number): void {
    this.apiServer = http.createServer((req, res) => {
      // CORS
      res.setHeader('Access-Control-Allow-Origin', '*');
      res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
      res.setHeader('Access-Control-Allow-Headers', 'Content-Type');

      if (req.method === 'OPTIONS') {
        res.writeHead(204);
        res.end();
        return;
      }

      const url = new URL(req.url ?? '/', `http://localhost:${port}`);

      if (req.method === 'GET' && url.pathname === '/api/status') {
        this.apiGetStatus(res);
      } else if (req.method === 'POST' && url.pathname === '/api/di') {
        this.apiSetDigitalInput(req, res);
      } else if (req.method === 'POST' && url.pathname === '/api/estop') {
        this.apiSetEStop(req, res);
      } else if (req.method === 'POST' && url.pathname === '/api/reset') {
        this.apiReset(res);
      } else {
        res.writeHead(404, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ error: 'Not found' }));
      }
    });

    this.apiServer.listen(port, '0.0.0.0');
  }

  private apiGetStatus(res: http.ServerResponse): void {
    const state = {
      id: this.state.id,
      ipAddress: this.state.ipAddress,
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
    };
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

  private apiSetEStop(req: http.IncomingMessage, res: http.ServerResponse): void {
    let body = '';
    req.on('data', (chunk: Buffer) => { body += chunk.toString(); });
    req.on('end', () => {
      try {
        const { active } = JSON.parse(body);
        this.state.eStop = !!active;
        if (this.state.eStop) {
          // E-Stop kills all outputs immediately
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

  private apiReset(res: http.ServerResponse): void {
    const id = this.state.id;
    this.state = createDefaultState(id);
    this.save();
    console.log(`[Orbit ${this.state.id}] Reset to defaults`);
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ ok: true }));
  }
}

// ─── Standalone Entry Point ─────────────────────────────────────────────────

if (process.argv[1]?.endsWith('orbitSimulator.ts') || process.argv[1]?.endsWith('orbitSimulator.js')) {
  const ORBIT_ID       = parseInt(process.env.ORBIT_ID ?? '2', 10);
  const ORBIT_TCP_PORT = parseInt(process.env.ORBIT_TCP_PORT ?? '5502', 10);
  const ORBIT_API_PORT = parseInt(process.env.ORBIT_API_PORT ?? '9010', 10);

  const orbit = new OrbitSimulator(ORBIT_ID);
  orbit.start(ORBIT_TCP_PORT, ORBIT_API_PORT);

  // Activity blinker reset
  setInterval(() => {
    orbit.state.vfdActivity = false;
    orbit.state.sensorActivity = false;
  }, 2000);

  // Periodic save
  setInterval(() => orbit.save(), 30000);

  // Graceful shutdown
  process.on('SIGINT', () => { orbit.stop(); process.exit(0); });
  process.on('SIGTERM', () => { orbit.stop(); process.exit(0); });
}
