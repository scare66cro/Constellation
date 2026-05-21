/**
 * orbitOtaClient.ts — Bridge-side client for the Phase 1A LP OTA listener.
 *
 * Talks to the firmware-side `lp_ota_task` over plain TCP on port 5503.
 * Wire framing matches `Nova_Firmware/lp_am2434/lp_ota_task.h` exactly:
 *
 *     [u32 BE total_len][u8 tag][...proto3 wire body...]
 *
 *   total_len  Bytes from `tag` onward (i.e. body_len + 1).
 *   tag        See LpOtaTag below — matches the LP_OTA_TAG_* defines.
 *   body       proto3 wire-format body for the message named.
 *
 * Phase 1A scope:
 *   • One-shot "fetch FwBankInfo" entry point per orbit IP.
 *   • Decoder for FwBankInfo + FwUpdateStatus (only the fields the LP
 *     actually emits today — others stay at proto3 defaults).
 *   • No flash-write path yet — calling fetchBankInfo() against an LP
 *     that's running Phase 1A returns the running version string and
 *     not much else.
 *
 * Phase 1B will replace the decoders with ts-proto-generated types from
 * `proto/agristar/firmware.proto` and add `pushImage(orbitIp, file)`
 * that drives FwBeginUpdate / FwDataChunk / FwFinalizeUpdate /
 * FwActivateBank.
 */

import * as net from 'net';

// ─── Wire constants (must match lp_ota_task.h) ─────────────────────────

export const LP_OTA_PORT = 5503;

export enum LpOtaTag {
  Begin       = 0x10,
  Chunk       = 0x11,
  Finalize    = 0x12,
  Activate    = 0x13,
  Status      = 0x20,
  BankInfo    = 0x21,
}

const MAX_FRAME = 2048;

// ─── Message shapes (match proto/agristar/firmware.proto) ──────────────

export enum FwBankId {
  BankA  = 0,
  BankB  = 1,
  Golden = 2,
}

export enum FwUpdateState {
  Idle       = 0,
  Erasing    = 1,
  Receiving  = 2,
  Verifying  = 3,
  Verified   = 4,
  Activating = 5,
  Error      = 6,
}

export interface FwBankInfo {
  activeBank:    FwBankId;
  bankAVersion:  string;
  bankACrc:      number;
  bankAValid:    boolean;
  bankBVersion:  string;
  bankBCrc:      number;
  bankBValid:    boolean;
  goldenVersion: string;
  bootCount:     number;
  bootReason:    number;
  /** OrbitRole this LP is provisioned as (from lp_device_config).
   *  Undefined when the LP firmware is older than 2026-05-20 and doesn't
   *  emit field 11. Treated as "unknown" by the bridge — falls through to
   *  relying on the LP-side FwBeginUpdate.expected_role gate. */
  currentRole?: number;
}

export interface FwUpdateStatus {
  state:         FwUpdateState;
  bytesWritten:  number;
  totalSize:     number;
  errorCode:     number;
  errorMessage:  string;
  activeVersion: string;
  stagedVersion: string;
  activeBank:    FwBankId;
}

// ─── proto3 minimal decoder (varint / length-delim only) ───────────────

class PbReader {
  constructor(private readonly buf: Buffer, private p = 0) {}
  eof(): boolean { return this.p >= this.buf.length; }
  varint(): number {
    let v = 0n; let shift = 0n;
    for (;;) {
      if (this.p >= this.buf.length) throw new Error('varint truncated');
      const b = this.buf[this.p++];
      v |= BigInt(b & 0x7f) << shift;
      if ((b & 0x80) === 0) return Number(v & 0xffffffffn);
      shift += 7n;
      if (shift > 63n) throw new Error('varint overflow');
    }
  }
  bytes(n: number): Buffer {
    if (this.p + n > this.buf.length) throw new Error('bytes truncated');
    const out = this.buf.subarray(this.p, this.p + n);
    this.p += n;
    return out;
  }
  /** Skip a field whose key has the given wire type. */
  skip(wt: number): void {
    if (wt === 0) { this.varint(); return; }
    if (wt === 2) { const n = this.varint(); this.bytes(n); return; }
    if (wt === 1) { this.bytes(8); return; }
    if (wt === 5) { this.bytes(4); return; }
    throw new Error(`unsupported wire type ${wt}`);
  }
  /** Iterate (field, wire_type) pairs; caller reads the value. */
  *fields(): Iterable<{ field: number; wt: number }> {
    while (!this.eof()) {
      const key = this.varint();
      yield { field: key >>> 3, wt: key & 0x7 };
    }
  }
}

function decodeBankInfo(body: Buffer): FwBankInfo {
  const r = new PbReader(body);
  const out: FwBankInfo = {
    activeBank: FwBankId.BankA,
    bankAVersion: '', bankACrc: 0, bankAValid: false,
    bankBVersion: '', bankBCrc: 0, bankBValid: false,
    goldenVersion: '', bootCount: 0, bootReason: 0,
  };
  for (const { field, wt } of r.fields()) {
    switch (field) {
      case  1: out.activeBank    = r.varint() as FwBankId;     break;
      case  2: out.bankAVersion  = r.bytes(r.varint()).toString('utf8'); break;
      case  3: out.bankACrc      = r.varint();                 break;
      case  4: out.bankAValid    = r.varint() !== 0;           break;
      case  5: out.bankBVersion  = r.bytes(r.varint()).toString('utf8'); break;
      case  6: out.bankBCrc      = r.varint();                 break;
      case  7: out.bankBValid    = r.varint() !== 0;           break;
      case  8: out.goldenVersion = r.bytes(r.varint()).toString('utf8'); break;
      case  9: out.bootCount     = r.varint();                 break;
      case 10: out.bootReason    = r.varint();                 break;
      case 11: out.currentRole   = r.varint();                 break;
      default: r.skip(wt);
    }
  }
  return out;
}

function decodeStatus(body: Buffer): FwUpdateStatus {
  const r = new PbReader(body);
  const out: FwUpdateStatus = {
    state: FwUpdateState.Idle, bytesWritten: 0, totalSize: 0,
    errorCode: 0, errorMessage: '',
    activeVersion: '', stagedVersion: '', activeBank: FwBankId.BankA,
  };
  for (const { field, wt } of r.fields()) {
    switch (field) {
      case 1: out.state         = r.varint() as FwUpdateState; break;
      case 2: out.bytesWritten  = r.varint();                  break;
      case 3: out.totalSize     = r.varint();                  break;
      case 4: out.errorCode     = r.varint();                  break;
      case 5: out.errorMessage  = r.bytes(r.varint()).toString('utf8'); break;
      case 6: out.activeVersion = r.bytes(r.varint()).toString('utf8'); break;
      case 7: out.stagedVersion = r.bytes(r.varint()).toString('utf8'); break;
      case 8: out.activeBank    = r.varint() as FwBankId;      break;
      default: r.skip(wt);
    }
  }
  return out;
}

// ─── Fetch helper ───────────────────────────────────────────────────────

export interface FetchBankInfoOptions {
  port?: number;        // default LP_OTA_PORT
  timeoutMs?: number;   // default 2000
}

export interface FetchBankInfoResult {
  host: string;
  reachable: true;
  bankInfo: FwBankInfo;
}

export interface FetchBankInfoFailure {
  host: string;
  reachable: false;
  error: string;
}

export type FetchBankInfoOutcome = FetchBankInfoResult | FetchBankInfoFailure;

/**
 * Connect to an LP, wait for the auto-pushed FwBankInfo frame, and
 * return it.  Closes the socket as soon as we have the frame.
 *
 * Returns a result-object (never throws) so callers can build per-orbit
 * status maps without try/catch sprawl.
 */
export function fetchBankInfo(
  host: string,
  opts: FetchBankInfoOptions = {},
): Promise<FetchBankInfoOutcome> {
  const port      = opts.port      ?? LP_OTA_PORT;
  const timeoutMs = opts.timeoutMs ?? 2000;

  return new Promise<FetchBankInfoOutcome>((resolve) => {
    const sock = new net.Socket();
    let rx = Buffer.alloc(0);
    let settled = false;

    const finish = (outcome: FetchBankInfoOutcome) => {
      if (settled) return;
      settled = true;
      try { sock.destroy(); } catch { /* ignore */ }
      resolve(outcome);
    };

    const timer = setTimeout(() => {
      finish({ host, reachable: false, error: `timeout after ${timeoutMs}ms` });
    }, timeoutMs);

    sock.once('error', (err) => {
      clearTimeout(timer);
      finish({ host, reachable: false, error: err.message });
    });
    sock.once('close', () => {
      clearTimeout(timer);
      if (!settled) {
        finish({ host, reachable: false, error: 'connection closed before FwBankInfo' });
      }
    });

    sock.on('data', (chunk: Buffer) => {
      rx = Buffer.concat([rx, chunk]);
      while (rx.length >= 4) {
        const total = rx.readUInt32BE(0);
        if (total === 0 || total > MAX_FRAME) {
          clearTimeout(timer);
          finish({ host, reachable: false, error: `bad frame length ${total}` });
          return;
        }
        if (rx.length < 4 + total) return;       // wait for full frame
        const tag  = rx[4];
        const body = rx.subarray(5, 4 + total);
        rx = rx.subarray(4 + total);
        if (tag === LpOtaTag.BankInfo) {
          try {
            const bankInfo = decodeBankInfo(body);
            clearTimeout(timer);
            finish({ host, reachable: true, bankInfo });
          } catch (e) {
            clearTimeout(timer);
            finish({ host, reachable: false,
                     error: `decode FwBankInfo: ${(e as Error).message}` });
          }
          return;
        }
        // Non-BankInfo frames are skipped while waiting for the
        // auto-push; Phase 1B will surface FwUpdateStatus too.
      }
    });

    sock.connect(port, host);
  });
}

/** Re-export the status decoder so Phase 1B push code can share it. */
export const _internal = { decodeBankInfo, decodeStatus };
