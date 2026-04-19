/**
 * Modbus RTU over TCP — Client
 * ────────────────────────────
 * Connects to a sensor board simulator (or real RS-485/TCP gateway) and
 * polls sensor boards for raw ADC data using standard Modbus RTU over TCP.
 *
 * Supports FC03 (Read Holding Registers).
 * Handles CRC-16/Modbus, connection management, and timeouts.
 */

import * as net from 'net';
import { EventEmitter } from 'events';

// ─── CRC-16/Modbus ─────────────────────────────────────────────────────────

const CRC_TABLE = new Uint16Array(256);
(function initCrcTable() {
  for (let i = 0; i < 256; i++) {
    let crc = i;
    for (let j = 0; j < 8; j++) {
      crc = (crc & 1) ? ((crc >> 1) ^ 0xA001) : (crc >> 1);
    }
    CRC_TABLE[i] = crc;
  }
})();

function crc16(buf: Buffer | Uint8Array): number {
  let crc = 0xFFFF;
  for (let i = 0; i < buf.length; i++) {
    crc = (crc >> 8) ^ CRC_TABLE[(crc ^ buf[i]) & 0xFF];
  }
  return crc;
}

// ─── Types ─────────────────────────────────────────────────────────────────

export interface RtuClientOptions {
  host: string;
  port: number;
  /** Response timeout in ms (default: 2000) */
  timeout?: number;
  /** Auto-reconnect on disconnect (default: true) */
  reconnect?: boolean;
  /** Reconnect interval in ms (default: 3000) */
  reconnectInterval?: number;
}

// ─── Client ────────────────────────────────────────────────────────────────

export class ModbusRtuClient extends EventEmitter {
  private socket: net.Socket | null = null;
  private connected = false;
  private options: Required<RtuClientOptions>;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private pendingResolve: ((data: Buffer) => void) | null = null;
  private pendingReject: ((err: Error) => void) | null = null;
  private pendingTimeout: ReturnType<typeof setTimeout> | null = null;
  private rxBuffer = Buffer.alloc(0);

  constructor(options: RtuClientOptions) {
    super();
    this.options = {
      timeout: 2000,
      reconnect: true,
      reconnectInterval: 3000,
      ...options,
    };
  }

  get isConnected(): boolean {
    return this.connected;
  }

  /** Connect to the RTU server. */
  connect(): Promise<void> {
    return new Promise((resolve, reject) => {
      if (this.connected) { resolve(); return; }

      this.socket = new net.Socket();
      this.socket.setNoDelay(true);

      this.socket.on('connect', () => {
        this.connected = true;
        this.emit('connected');
        resolve();
      });

      this.socket.on('data', (data: Buffer) => {
        this.rxBuffer = Buffer.concat([this.rxBuffer, data]);
        this.checkResponse();
      });

      this.socket.on('close', () => {
        const wasConnected = this.connected;
        this.connected = false;
        this.rejectPending(new Error('Connection closed'));
        if (wasConnected) this.emit('disconnected');
        if (this.options.reconnect) this.scheduleReconnect();
      });

      this.socket.on('error', (err: Error) => {
        this.emit('error', err);
        if (!this.connected) reject(err);
      });

      this.socket.connect(this.options.port, this.options.host);
    });
  }

  /** Disconnect and stop reconnecting. */
  disconnect(): void {
    this.options.reconnect = false;
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
    this.rejectPending(new Error('Disconnected'));
    this.socket?.destroy();
    this.socket = null;
    this.connected = false;
  }

  /**
   * Read holding registers from a slave (FC03).
   * @returns Array of register values (uint16).
   */
  async readHoldingRegisters(unitId: number, startAddr: number, quantity: number): Promise<number[]> {
    if (!this.connected || !this.socket) {
      throw new Error('Not connected');
    }

    // Build request: UnitID(1) + FC(1) + StartAddr(2) + Quantity(2) + CRC(2)
    const req = Buffer.alloc(8);
    req[0] = unitId;
    req[1] = 0x03;
    req.writeUInt16BE(startAddr, 2);
    req.writeUInt16BE(quantity, 4);
    const crc = crc16(req.subarray(0, 6));
    req.writeUInt16LE(crc, 6);

    return this.transact(req, unitId, 0x03, quantity);
  }

  /**
   * Write a single holding register (FC06).
   * @returns The value echoed back by the slave.
   */
  async writeSingleRegister(unitId: number, addr: number, value: number): Promise<number> {
    if (!this.connected || !this.socket) {
      throw new Error('Not connected');
    }

    // Build request: UnitID(1) + FC(1) + Addr(2) + Value(2) + CRC(2)
    const req = Buffer.alloc(8);
    req[0] = unitId;
    req[1] = 0x06;
    req.writeUInt16BE(addr, 2);
    req.writeUInt16BE(value & 0xFFFF, 4);
    const crc = crc16(req.subarray(0, 6));
    req.writeUInt16LE(crc, 6);

    const resp = await this.transactRaw(req, unitId, 0x06, 8);
    return resp.readUInt16BE(4);
  }

  /**
   * Write multiple holding registers (FC16).
   */
  async writeMultipleRegisters(unitId: number, startAddr: number, values: number[]): Promise<void> {
    if (!this.connected || !this.socket) {
      throw new Error('Not connected');
    }

    const qty = values.length;
    // Build: UnitID(1) + FC(1) + StartAddr(2) + Qty(2) + ByteCount(1) + Data(N*2) + CRC(2)
    const req = Buffer.alloc(7 + qty * 2 + 2);
    req[0] = unitId;
    req[1] = 0x10;
    req.writeUInt16BE(startAddr, 2);
    req.writeUInt16BE(qty, 4);
    req[6] = qty * 2;
    for (let i = 0; i < qty; i++) {
      req.writeUInt16BE(values[i] & 0xFFFF, 7 + i * 2);
    }
    const crc = crc16(req.subarray(0, 7 + qty * 2));
    req.writeUInt16LE(crc, 7 + qty * 2);

    await this.transactRaw(req, unitId, 0x10, 8);
  }

  // ─── Internal ──────────────────────────────────────────────────────────

  /** Expected total response length for current pending transaction (0 = variable/FC03-style) */
  private pendingFixedLen = 0;

  private transact(frame: Buffer, expectedUnit: number, expectedFc: number, expectedQty: number): Promise<number[]> {
    return new Promise((resolve, reject) => {
      // Only one transaction at a time
      if (this.pendingResolve) {
        reject(new Error('Transaction already in progress'));
        return;
      }

      this.rxBuffer = Buffer.alloc(0);
      this.pendingFixedLen = 0; // variable-length FC03 style

      this.pendingResolve = (resp: Buffer) => {
        // Parse response
        const unitId = resp[0];
        const fc = resp[1];

        if (unitId !== expectedUnit) {
          reject(new Error(`Unit ID mismatch: expected ${expectedUnit}, got ${unitId}`));
          return;
        }

        // Exception response
        if (fc === (expectedFc | 0x80)) {
          reject(new Error(`Modbus exception: code ${resp[2]}`));
          return;
        }

        if (fc !== expectedFc) {
          reject(new Error(`FC mismatch: expected ${expectedFc}, got ${fc}`));
          return;
        }

        const byteCount = resp[2];
        if (byteCount !== expectedQty * 2) {
          reject(new Error(`Byte count mismatch: expected ${expectedQty * 2}, got ${byteCount}`));
          return;
        }

        const values: number[] = [];
        for (let i = 0; i < expectedQty; i++) {
          values.push(resp.readUInt16BE(3 + i * 2));
        }
        resolve(values);
      };

      this.pendingReject = reject;

      this.pendingTimeout = setTimeout(() => {
        this.rejectPending(new Error(`Timeout (${this.options.timeout}ms)`));
      }, this.options.timeout);

      this.socket!.write(frame);
    });
  }

  /**
   * Raw transact — sends a frame and returns the complete response buffer.
   * Used for FC06/FC10 write responses which have fixed-length echo format.
   */
  private transactRaw(frame: Buffer, expectedUnit: number, expectedFc: number, expectedLen: number): Promise<Buffer> {
    return new Promise((resolve, reject) => {
      if (this.pendingResolve) {
        reject(new Error('Transaction already in progress'));
        return;
      }

      this.rxBuffer = Buffer.alloc(0);
      this.pendingFixedLen = expectedLen;

      this.pendingResolve = (resp: Buffer) => {
        const unitId = resp[0];
        const fc = resp[1];
        if (unitId !== expectedUnit) {
          reject(new Error(`Unit ID mismatch: expected ${expectedUnit}, got ${unitId}`));
          return;
        }
        if (fc === (expectedFc | 0x80)) {
          reject(new Error(`Modbus exception: code ${resp[2]}`));
          return;
        }
        if (fc !== expectedFc) {
          reject(new Error(`FC mismatch: expected ${expectedFc}, got ${fc}`));
          return;
        }
        resolve(resp);
      };

      this.pendingReject = reject;
      this.pendingTimeout = setTimeout(() => {
        this.rejectPending(new Error(`Timeout (${this.options.timeout}ms)`));
      }, this.options.timeout);
      this.socket!.write(frame);
    });
  }

  private checkResponse(): void {
    if (!this.pendingResolve) return;

    // Need at least 3 bytes: UnitID + FC + ByteCount
    if (this.rxBuffer.length < 3) return;

    const fc = this.rxBuffer[1];

    // Exception response: 5 bytes total
    if (fc & 0x80) {
      if (this.rxBuffer.length >= 5) {
        const frame = this.rxBuffer.subarray(0, 5);
        this.rxBuffer = this.rxBuffer.subarray(5);
        this.completePending(frame);
      }
      return;
    }

    // Fixed-length response (FC06/FC10 write echo): known total length
    if (this.pendingFixedLen > 0) {
      if (this.rxBuffer.length < this.pendingFixedLen) return;
      const frame = this.rxBuffer.subarray(0, this.pendingFixedLen);
      this.rxBuffer = this.rxBuffer.subarray(this.pendingFixedLen);
      const payloadLen = this.pendingFixedLen - 2;
      const receivedCrc = frame.readUInt16LE(payloadLen);
      const computedCrc = crc16(frame.subarray(0, payloadLen));
      if (receivedCrc !== computedCrc) {
        this.rejectPending(new Error('CRC mismatch'));
        return;
      }
      this.completePending(frame);
      return;
    }

    // Variable-length response (FC03): UnitID(1) + FC(1) + ByteCount(1) + Data(N) + CRC(2)
    const byteCount = this.rxBuffer[2];
    const totalLen = 3 + byteCount + 2;
    if (this.rxBuffer.length < totalLen) return;

    const frame = this.rxBuffer.subarray(0, totalLen);
    this.rxBuffer = this.rxBuffer.subarray(totalLen);

    // Verify CRC
    const payloadLen = totalLen - 2;
    const receivedCrc = frame.readUInt16LE(payloadLen);
    const computedCrc = crc16(frame.subarray(0, payloadLen));
    if (receivedCrc !== computedCrc) {
      this.rejectPending(new Error('CRC mismatch'));
      return;
    }

    this.completePending(frame);
  }

  private completePending(data: Buffer): void {
    if (this.pendingTimeout) {
      clearTimeout(this.pendingTimeout);
      this.pendingTimeout = null;
    }
    const resolve = this.pendingResolve;
    this.pendingResolve = null;
    this.pendingReject = null;
    resolve?.(data);
  }

  private rejectPending(err: Error): void {
    if (this.pendingTimeout) {
      clearTimeout(this.pendingTimeout);
      this.pendingTimeout = null;
    }
    const reject = this.pendingReject;
    this.pendingResolve = null;
    this.pendingReject = null;
    reject?.(err);
  }

  private scheduleReconnect(): void {
    if (this.reconnectTimer) return;
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      this.connect().catch(() => {
        // Will retry via close handler
      });
    }, this.options.reconnectInterval);
  }
}
