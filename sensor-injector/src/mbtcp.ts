/**
 * mbtcp.ts — Tiny Modbus-TCP master client.
 *
 * Dependency-free, single-socket, in-flight-one transaction. Just enough
 * to push sensor values via FC16 (and read status via FC03) at the orbit
 * LP firmware's :5502.
 *
 * MBAP framing per orbit_modbus_tcp.c:
 *   [0..1]  txn id (we increment per call)
 *   [2..3]  proto id (always 0)
 *   [4..5]  length (count of bytes from byte 6 onward)
 *   [6]     unit id
 *   [7]     function code
 *   [8..]   PDU body
 */

import * as net from 'net';

export interface MbtcpOptions {
  host: string;
  port?: number;          // default 5502 (orbit LP)
  unitId?: number;        // default 1
  timeoutMs?: number;     // per-transaction default 2000
  reconnectMs?: number;   // default 2000
}

interface Pending {
  resolve: (resp: Buffer) => void;
  reject: (err: Error) => void;
  timer: NodeJS.Timeout;
  txnId: number;
  expectedFc: number;
}

export class ModbusTcpClient {
  private sock: net.Socket | null = null;
  private rxBuf = Buffer.alloc(0);
  private pending: Pending | null = null;
  private nextTxn = 1;
  private connected = false;
  private reconnectTimer: NodeJS.Timeout | null = null;
  private opts: Required<MbtcpOptions>;

  constructor(opts: MbtcpOptions) {
    this.opts = {
      port: 5502,
      unitId: 1,
      timeoutMs: 2000,
      reconnectMs: 2000,
      ...opts,
    };
  }

  isConnected() { return this.connected; }
  target() { return `${this.opts.host}:${this.opts.port}`; }

  connect(): void {
    if (this.sock) return;
    const s = new net.Socket();
    s.setNoDelay(true);
    this.sock = s;

    s.on('connect', () => {
      this.connected = true;
      console.log(`[mbtcp] connected ${this.target()}`);
    });
    s.on('data', (d) => {
      this.rxBuf = Buffer.concat([this.rxBuf, d]);
      this.drain();
    });
    s.on('close', () => {
      this.connected = false;
      this.failPending(new Error('socket closed'));
      this.sock = null;
      this.scheduleReconnect();
    });
    s.on('error', (err) => {
      // Suppress noisy ECONNREFUSED storms — print once per drop.
      if (this.connected) console.log(`[mbtcp] ${err.message}`);
    });

    s.connect(this.opts.port, this.opts.host);
  }

  private scheduleReconnect() {
    if (this.reconnectTimer) return;
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      this.connect();
    }, this.opts.reconnectMs);
  }

  private failPending(err: Error) {
    if (this.pending) {
      clearTimeout(this.pending.timer);
      this.pending.reject(err);
      this.pending = null;
    }
  }

  /** FC16 — Write Multiple Holding Registers. */
  writeRegisters(startAddr: number, values: number[]): Promise<void> {
    if (values.length === 0 || values.length > 123) {
      return Promise.reject(new Error('FC16 qty out of range'));
    }
    const qty = values.length;
    const pdu = Buffer.alloc(6 + qty * 2);
    pdu[0] = 0x10;
    pdu.writeUInt16BE(startAddr, 1);
    pdu.writeUInt16BE(qty, 3);
    pdu[5] = qty * 2;
    for (let i = 0; i < qty; i++) pdu.writeUInt16BE(values[i] & 0xFFFF, 6 + i * 2);
    return this.transact(pdu).then(() => undefined);
  }

  /** FC06 — Write Single Register. */
  writeRegister(addr: number, value: number): Promise<void> {
    const pdu = Buffer.alloc(5);
    pdu[0] = 0x06;
    pdu.writeUInt16BE(addr, 1);
    pdu.writeUInt16BE(value & 0xFFFF, 3);
    return this.transact(pdu).then(() => undefined);
  }

  /** FC03 — Read Holding Registers. */
  readRegisters(startAddr: number, qty: number): Promise<number[]> {
    if (qty === 0 || qty > 125) return Promise.reject(new Error('FC03 qty out of range'));
    const pdu = Buffer.alloc(5);
    pdu[0] = 0x03;
    pdu.writeUInt16BE(startAddr, 1);
    pdu.writeUInt16BE(qty, 3);
    return this.transact(pdu).then((respPdu) => {
      // respPdu = [fc, byteCount, data...]
      const byteCount = respPdu[1];
      if (byteCount !== qty * 2) throw new Error(`FC03 byte-count mismatch: ${byteCount}`);
      const out: number[] = [];
      for (let i = 0; i < qty; i++) out.push(respPdu.readUInt16BE(2 + i * 2));
      return out;
    });
  }

  /** FC01 — Read Coils. Returns one boolean per coil. */
  readCoils(startAddr: number, qty: number): Promise<boolean[]> {
    return this.readBits(0x01, startAddr, qty);
  }

  /** FC02 — Read Discrete Inputs. Returns one boolean per input. */
  readDiscreteInputs(startAddr: number, qty: number): Promise<boolean[]> {
    return this.readBits(0x02, startAddr, qty);
  }

  private readBits(fc: number, startAddr: number, qty: number): Promise<boolean[]> {
    if (qty === 0 || qty > 2000) return Promise.reject(new Error('bit-read qty out of range'));
    const pdu = Buffer.alloc(5);
    pdu[0] = fc;
    pdu.writeUInt16BE(startAddr, 1);
    pdu.writeUInt16BE(qty, 3);
    return this.transact(pdu).then((respPdu) => {
      // respPdu = [fc, byteCount, packed bytes (LSB first)]
      const byteCount = respPdu[1];
      const expected  = Math.ceil(qty / 8);
      if (byteCount !== expected) throw new Error(`FC${fc} byte-count mismatch: ${byteCount} vs ${expected}`);
      const out: boolean[] = [];
      for (let i = 0; i < qty; i++) {
        const b = respPdu[2 + (i >> 3)];
        out.push(((b >> (i & 7)) & 1) !== 0);
      }
      return out;
    });
  }

  private transact(pdu: Buffer): Promise<Buffer> {
    return new Promise((resolve, reject) => {
      if (!this.connected || !this.sock) return reject(new Error('not connected'));
      if (this.pending) return reject(new Error('transaction in progress'));

      const txnId = this.nextTxn++ & 0xFFFF;
      const frame = Buffer.alloc(7 + pdu.length);
      frame.writeUInt16BE(txnId, 0);
      frame.writeUInt16BE(0, 2);                  // proto
      frame.writeUInt16BE(pdu.length + 1, 4);     // length = unit + pdu
      frame[6] = this.opts.unitId;
      pdu.copy(frame, 7);

      const timer = setTimeout(() => {
        this.failPending(new Error(`timeout after ${this.opts.timeoutMs}ms`));
      }, this.opts.timeoutMs);

      this.pending = { resolve, reject, timer, txnId, expectedFc: pdu[0] };
      this.sock.write(frame);
    });
  }

  private drain() {
    while (this.rxBuf.length >= 7) {
      const length = this.rxBuf.readUInt16BE(4);
      const total = 6 + length;
      if (this.rxBuf.length < total) return;

      const txnId = this.rxBuf.readUInt16BE(0);
      const fc    = this.rxBuf[7];
      const pdu   = this.rxBuf.subarray(7, total);
      this.rxBuf  = this.rxBuf.subarray(total);

      const p = this.pending;
      if (!p) continue;
      if (p.txnId !== txnId) {
        this.failPending(new Error(`txn id mismatch ${txnId} != ${p.txnId}`));
        continue;
      }
      clearTimeout(p.timer);
      this.pending = null;

      if (fc === (p.expectedFc | 0x80)) {
        p.reject(new Error(`Modbus exception code ${pdu[1]} for FC ${p.expectedFc.toString(16)}`));
      } else if (fc !== p.expectedFc) {
        p.reject(new Error(`FC mismatch: got ${fc}, expected ${p.expectedFc}`));
      } else {
        p.resolve(pdu);
      }
    }
  }
}
