/**
 * orbitMbtcp.ts — Multi-host Modbus TCP client pool for the bridge's
 * orbit data plane.
 *
 * Replaces the legacy `fetch(orbitUrl(...))` calls into the
 * orbit-simulator REST API. Talks Modbus TCP directly to each LP AM2434
 * orbit board (production) or its `sensor-injector`-style sim (dev).
 * Mirrors the framing logic in `sensor-injector/src/mbtcp.ts` but adds:
 *
 *   • Multi-host pool, keyed by `host:port`.
 *   • FIFO queue per socket so concurrent HTTP handlers don't trip on
 *     "transaction in progress" (the sensor-injector's single-publisher
 *     case can reject; the bridge serves arbitrary parallel requests).
 *   • Lazy-connect: a request triggers the connect; we don't preconnect
 *     to slots that may not exist.
 *   • Auto-reconnect: on socket close, the next request reconnects.
 *
 * Per-slot host resolution is exported as `resolveOrbitHost(slot)`. A
 * future settings field will hold per-slot IPs; for now it reads
 * `ORBIT_IP_<slot>` env vars and falls back to `ORBIT_IP_BASE`+slot
 * (default base `10.1.2.200`). This matches the sensor-injector
 * convention. See /memories/repo/bridge-mbtcp-migration.md.
 *
 * Errors are surfaced honestly: callers get the raw mbtcp error and
 * are expected to translate to HTTP 503/502 — never silently faked
 * as a connected board with empty fields.
 */

import * as net from 'net';

export interface OrbitMbtcpOptions {
  port?: number;          // default 5502 (orbit LP)
  unitId?: number;        // default 1
  timeoutMs?: number;     // per-transaction default 1500
  reconnectMs?: number;   // default 2000
}

interface QueueItem {
  pdu: Buffer;
  resolve: (resp: Buffer) => void;
  reject: (err: Error) => void;
}

interface Pending {
  resolve: (resp: Buffer) => void;
  reject: (err: Error) => void;
  timer: NodeJS.Timeout;
  txnId: number;
  expectedFc: number;
}

class OrbitConn {
  private sock: net.Socket | null = null;
  private rxBuf = Buffer.alloc(0);
  private pending: Pending | null = null;
  private queue: QueueItem[] = [];
  private nextTxn = 1;
  private connecting = false;
  private connected = false;
  private reconnectTimer: NodeJS.Timeout | null = null;
  private host: string;
  private opts: Required<OrbitMbtcpOptions>;

  constructor(host: string, opts: OrbitMbtcpOptions) {
    this.host = host;
    this.opts = {
      port: 5502,
      unitId: 1,
      timeoutMs: 1500,
      reconnectMs: 2000,
      ...opts,
    };
  }

  target(): string { return `${this.host}:${this.opts.port}`; }

  /** Issue a Modbus PDU; returns the response PDU (sans MBAP). */
  transact(pdu: Buffer): Promise<Buffer> {
    return new Promise<Buffer>((resolve, reject) => {
      this.queue.push({ pdu, resolve, reject });
      this.kick();
    });
  }

  private kick(): void {
    if (this.pending || this.queue.length === 0) return;
    if (!this.connected) {
      this.ensureConnect();
      return;
    }
    const item = this.queue.shift()!;
    this.dispatch(item);
  }

  private ensureConnect(): void {
    if (this.sock || this.connecting) return;
    this.connecting = true;

    const s = new net.Socket();
    s.setNoDelay(true);
    this.sock = s;

    s.once('connect', () => {
      this.connecting = false;
      this.connected = true;
      // Drain whatever queued up while we were dialing.
      this.kick();
    });
    s.on('data', (d) => {
      this.rxBuf = Buffer.concat([this.rxBuf, d]);
      this.drain();
    });
    s.on('close', () => {
      this.connecting = false;
      this.connected = false;
      this.failPending(new Error(`socket closed (${this.target()})`));
      this.sock = null;
      // If anyone is still waiting, fail them rather than reconnect-storm;
      // the next caller will trigger a fresh connect attempt.
      this.failQueue(new Error(`socket closed (${this.target()})`));
      this.scheduleReconnect();
    });
    s.on('error', (err) => {
      // Suppress noisy logs — caller will see the rejection.
      void err;
    });

    s.connect(this.opts.port, this.host);
  }

  private scheduleReconnect(): void {
    if (this.reconnectTimer) return;
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      // Only reconnect if there's pending work; otherwise stay idle.
      if (this.queue.length > 0) this.ensureConnect();
    }, this.opts.reconnectMs);
  }

  private failPending(err: Error): void {
    if (this.pending) {
      clearTimeout(this.pending.timer);
      this.pending.reject(err);
      this.pending = null;
    }
  }

  private failQueue(err: Error): void {
    const q = this.queue;
    this.queue = [];
    for (const item of q) item.reject(err);
  }

  private dispatch(item: QueueItem): void {
    if (!this.sock || !this.connected) {
      item.reject(new Error(`not connected (${this.target()})`));
      return;
    }
    const txnId = this.nextTxn++ & 0xFFFF;
    const frame = Buffer.alloc(7 + item.pdu.length);
    frame.writeUInt16BE(txnId, 0);
    frame.writeUInt16BE(0, 2);                          // protocol id
    frame.writeUInt16BE(item.pdu.length + 1, 4);        // length = unit + pdu
    frame[6] = this.opts.unitId;
    item.pdu.copy(frame, 7);

    const timer = setTimeout(() => {
      this.failPending(new Error(`timeout after ${this.opts.timeoutMs}ms (${this.target()})`));
      // Move to next request; socket may still be alive.
      this.kick();
    }, this.opts.timeoutMs);

    this.pending = {
      resolve: item.resolve,
      reject:  item.reject,
      timer,
      txnId,
      expectedFc: item.pdu[0],
    };
    try {
      this.sock.write(frame);
    } catch (err: any) {
      this.failPending(err instanceof Error ? err : new Error(String(err)));
      this.kick();
    }
  }

  private drain(): void {
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
        this.failPending(new Error(`txn id mismatch ${txnId} != ${p.txnId} (${this.target()})`));
        this.kick();
        continue;
      }
      clearTimeout(p.timer);
      this.pending = null;

      if (fc === (p.expectedFc | 0x80)) {
        p.reject(new Error(`Modbus exception 0x${pdu[1].toString(16)} for FC 0x${p.expectedFc.toString(16)} (${this.target()})`));
      } else if (fc !== p.expectedFc) {
        p.reject(new Error(`FC mismatch ${fc} != ${p.expectedFc} (${this.target()})`));
      } else {
        p.resolve(pdu);
      }
      this.kick();
    }
  }
}

// ─── Pool ───────────────────────────────────────────────────────────────

const pool = new Map<string, OrbitConn>();

function keyOf(host: string, port: number): string {
  return `${host}:${port}`;
}

function getConn(host: string, port = 5502): OrbitConn {
  const k = keyOf(host, port);
  let c = pool.get(k);
  if (!c) {
    c = new OrbitConn(host, { port });
    pool.set(k, c);
  }
  return c;
}

// ─── Per-slot IP resolution ────────────────────────────────────────────
//
// Resolution order:
//   1. ORBIT_IP_<slot>       (e.g. ORBIT_IP_0=10.1.2.230)
//   2. ORBIT_IP_BASE + slot  (default base 10.1.2.200, → 10.1.2.200,
//                              10.1.2.201, ...)
// The dipswitch/firmware convention is base.B.C.(slot+2); the 200+slot
// convention here matches the sensor-injector default which is the dev
// rig's STORAGE LP at 10.1.2.200. Document any per-site override in
// /memories/repo/bridge-mbtcp-migration.md.

const ORBIT_IP_BASE = process.env.ORBIT_IP_BASE ?? '10.1.2.200';

export function resolveOrbitHost(slot: number): string {
  const override = process.env[`ORBIT_IP_${slot}`];
  if (override && override.trim().length > 0) return override.trim();
  // Increment final octet from the base. If the base doesn't parse as
  // an IPv4, fall through to the literal base (caller will see a DNS
  // failure honestly).
  const m = ORBIT_IP_BASE.match(/^(\d+)\.(\d+)\.(\d+)\.(\d+)$/);
  if (!m) return ORBIT_IP_BASE;
  const a = +m[1], b = +m[2], c = +m[3], d = +m[4] + slot;
  if (d > 255) return ORBIT_IP_BASE;   // out of range, fail loudly
  return `${a}.${b}.${c}.${d}`;
}

// ─── Public API: Modbus operations ─────────────────────────────────────

/** FC03 — Read N holding registers from an orbit. */
export async function readHoldingRegs(
  host: string,
  startAddr: number,
  qty: number,
): Promise<number[]> {
  if (qty <= 0 || qty > 125) throw new Error(`FC03 qty out of range: ${qty}`);
  const pdu = Buffer.alloc(5);
  pdu[0] = 0x03;
  pdu.writeUInt16BE(startAddr, 1);
  pdu.writeUInt16BE(qty, 3);
  const resp = await getConn(host).transact(pdu);
  const byteCount = resp[1];
  if (byteCount !== qty * 2) {
    throw new Error(`FC03 byte-count mismatch: ${byteCount} != ${qty * 2}`);
  }
  const out: number[] = new Array(qty);
  for (let i = 0; i < qty; i++) out[i] = resp.readUInt16BE(2 + i * 2);
  return out;
}

/** FC01 — Read N coils (DO state). Returns one boolean per coil. */
export async function readCoils(
  host: string,
  startAddr: number,
  qty: number,
): Promise<boolean[]> {
  return readBits(host, startAddr, qty, 0x01);
}

/** FC02 — Read N discrete inputs (DI state). Returns one boolean per input. */
export async function readDiscreteInputs(
  host: string,
  startAddr: number,
  qty: number,
): Promise<boolean[]> {
  return readBits(host, startAddr, qty, 0x02);
}

async function readBits(
  host: string,
  startAddr: number,
  qty: number,
  fc: 0x01 | 0x02,
): Promise<boolean[]> {
  if (qty <= 0 || qty > 2000) throw new Error(`FC${fc.toString(16).padStart(2,'0')} qty out of range: ${qty}`);
  const pdu = Buffer.alloc(5);
  pdu[0] = fc;
  pdu.writeUInt16BE(startAddr, 1);
  pdu.writeUInt16BE(qty, 3);
  const resp = await getConn(host).transact(pdu);
  const byteCount = resp[1];
  const expected = Math.ceil(qty / 8);
  if (byteCount !== expected) {
    throw new Error(`FC${fc.toString(16).padStart(2,'0')} byte-count mismatch: ${byteCount} != ${expected}`);
  }
  const out: boolean[] = new Array(qty);
  for (let i = 0; i < qty; i++) {
    const byte = resp[2 + (i >> 3)];
    out[i] = ((byte >> (i & 7)) & 1) === 1;
  }
  return out;
}

/** FC06 — Write a single holding register. */
export async function writeHoldingReg(
  host: string,
  addr: number,
  value: number,
): Promise<void> {
  const pdu = Buffer.alloc(5);
  pdu[0] = 0x06;
  pdu.writeUInt16BE(addr, 1);
  pdu.writeUInt16BE(value & 0xFFFF, 3);
  await getConn(host).transact(pdu);
}

/** FC16 — Write multiple consecutive holding registers. */
export async function writeHoldingRegs(
  host: string,
  startAddr: number,
  values: number[],
): Promise<void> {
  if (values.length === 0 || values.length > 123) {
    throw new Error(`FC16 qty out of range: ${values.length}`);
  }
  const qty = values.length;
  const pdu = Buffer.alloc(6 + qty * 2);
  pdu[0] = 0x10;
  pdu.writeUInt16BE(startAddr, 1);
  pdu.writeUInt16BE(qty, 3);
  pdu[5] = qty * 2;
  for (let i = 0; i < qty; i++) pdu.writeUInt16BE(values[i] & 0xFFFF, 6 + i * 2);
  await getConn(host).transact(pdu);
}

// ─── Convenience: signed/unsigned word helpers ─────────────────────────

/** Reinterpret an unsigned 16-bit register as a signed int16. */
export function asS16(u: number): number {
  return (u & 0x8000) ? u - 0x10000 : u;
}
