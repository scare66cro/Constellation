/**
 * orbitOtaPush.ts — Bridge → orbit firmware update push state machine.
 *
 * **PARTIAL SCAFFOLD (2026-05-01)**
 *
 * Drives the `Begin → DataChunk* → Finalize → Activate` state machine
 * defined in `proto/agristar/firmware.proto`, talking to
 * `lp_ota_task` on the LP at TCP `:5503` using the same length-prefixed
 * COBS-free framing as `orbitOtaClient.ts`:
 *
 *     [u32 BE total_len][u8 tag][...proto3 wire body...]
 *
 * **What's implemented:**
 *   - Image read + CRC-32 (Ethernet/zlib polynomial — matches firmware).
 *   - Chunking into `chunkSize`-byte slices.
 *   - Hand-rolled proto3 encoders for `FwBeginUpdate`, `FwDataChunk`,
 *     `FwFinalizeUpdate`, `FwActivateBank` (mirror of the firmware-side
 *     encoders in `Nova_Firmware/lp_am2434/lp_ota_task.c`).
 *   - Sequenced send loop with status-frame await + retry budget.
 *   - Progress callback for UI driving.
 *
 * **What's stubbed pending Phase 1B firmware-side handlers:**
 *   - The LP today replies `FwUpdateStatus { state=FW_ERROR,
 *     error_code=LP_OTA_ERR_NOT_IMPL=1 }` to every Begin/Chunk/etc
 *     frame.  This module's `pushImage()` will surface that as
 *     `OtaPushError.notImplemented` and abort cleanly.  Once the
 *     firmware-side flash path lands (Phase 1B), nothing in this file
 *     needs to change — the state machine just succeeds instead of
 *     hitting NOT_IMPL.
 *
 * **Why scaffold this now:** the state machine + CRC + chunk loop is
 * 100% reusable when Phase 3 lands.  The work doesn't depend on having
 * working firmware-side flash writes — we can dry-run against the
 * Phase 1A NOT_IMPL listener to validate framing, retries, and timing
 * end-to-end.
 *
 * **Out of scope here:**
 *   - HTTP route exposing this to the UI (add later in `apiRoutes.ts`).
 *   - Per-host queueing (one push at a time per orbit; reject overlap).
 *   - Bridge-side persistence of staged image (we just stream the file).
 */

import * as net from 'net';
import * as fs from 'fs/promises';
import {
  LP_OTA_PORT,
  LpOtaTag,
  FwUpdateState,
  type FwUpdateStatus,
  type FwBankInfo,
  _internal as ota_internal,
} from './orbitOtaClient.js';

// ─── Version parsing — for downgrade rejection (Gap 2 bridge-side) ─────
// Project firmware version format is `0.A.<n>+<sha>[-dirty]`. Compare
// the integer N for downgrade detection. Returns null if string doesn't
// parse — caller treats null as "unknown, skip comparison".
// See docs/firmware-version-current.md and docs/lp-am2434-ota-hardening-plan.md.
function parseAlphaN(version: string): number | null {
  if (!version) return null;
  const m = version.match(/^0\.A\.(\d+)(?:[+\-]|$)/);
  return m ? parseInt(m[1], 10) : null;
}

// ─── CRC-32 (Ethernet/zlib polynomial, must match firmware) ────────────

const CRC32_POLY = 0xedb88320;
const CRC32_TABLE: Uint32Array = (() => {
  const t = new Uint32Array(256);
  for (let i = 0; i < 256; i++) {
    let c = i;
    for (let k = 0; k < 8; k++) {
      c = (c & 1) ? (CRC32_POLY ^ (c >>> 1)) : (c >>> 1);
    }
    t[i] = c >>> 0;
  }
  return t;
})();

export function crc32(data: Buffer | Uint8Array): number {
  let c = 0xffffffff;
  for (let i = 0; i < data.length; i++) {
    c = CRC32_TABLE[(c ^ data[i]) & 0xff] ^ (c >>> 8);
  }
  return (c ^ 0xffffffff) >>> 0;
}

// ─── Minimal proto3 encoder (mirror of firmware-side hand-encoders) ────

function pbVarint(out: number[], v: number): void {
  // Caller passes uint32 values. JS bit-ops force 32-bit signed; use
  // unsigned shifts and BigInt only where necessary. For >32-bit values
  // (currently unused) this would need BigInt.
  let u = v >>> 0;
  while (u >= 0x80) {
    out.push((u & 0x7f) | 0x80);
    u = u >>> 7;
  }
  out.push(u & 0x7f);
}
function pbKey(out: number[], field: number, wt: number): void {
  pbVarint(out, ((field << 3) | wt) >>> 0);
}
function pbUint32(out: number[], field: number, v: number): void {
  if (v === 0) return;            // proto3 default suppression
  pbKey(out, field, 0);
  pbVarint(out, v);
}
function pbBool(out: number[], field: number, v: boolean): void {
  if (!v) return;
  pbKey(out, field, 0);
  pbVarint(out, 1);
}
function pbString(out: number[], field: number, s: string): void {
  if (!s) return;
  const bytes = Buffer.from(s, 'utf8');
  pbKey(out, field, 2);
  pbVarint(out, bytes.length);
  for (const b of bytes) out.push(b);
}
function pbBytes(out: number[], field: number, data: Buffer): void {
  if (data.length === 0) return;
  pbKey(out, field, 2);
  pbVarint(out, data.length);
  for (const b of data) out.push(b);
}

// ─── Message encoders (must match proto/agristar/firmware.proto) ───────

function encodeBegin(totalSize: number, crc: number,
                     version: string, chunkSize: number): Buffer {
  const out: number[] = [];
  pbUint32(out, 1, totalSize);
  pbUint32(out, 2, crc);
  pbString(out, 3, version);
  pbUint32(out, 4, chunkSize);
  return Buffer.from(out);
}
function encodeChunk(offset: number, data: Buffer, chunkCrc: number): Buffer {
  const out: number[] = [];
  pbUint32(out, 1, offset);
  pbBytes (out, 2, data);
  pbUint32(out, 3, chunkCrc);
  return Buffer.from(out);
}
function encodeFinalize(crc: number): Buffer {
  const out: number[] = [];
  pbUint32(out, 1, crc);
  return Buffer.from(out);
}
function encodeActivate(reboot: boolean): Buffer {
  const out: number[] = [];
  pbBool(out, 1, reboot);
  return Buffer.from(out);
}

// ─── Frame helper ──────────────────────────────────────────────────────

function frame(tag: number, body: Buffer): Buffer {
  const total = 1 + body.length;
  const hdr = Buffer.alloc(5);
  hdr.writeUInt32BE(total, 0);
  hdr.writeUInt8(tag, 4);
  return Buffer.concat([hdr, body]);
}

// ─── Public API ────────────────────────────────────────────────────────

export interface PushImageOptions {
  /** Override the OTA listener port (default 5503). */
  port?: number;
  /** Bytes per FwDataChunk (default 1024; firmware MAX_FRAME=2048). */
  chunkSize?: number;
  /** Per-frame ack/nak timeout in ms (default 4000). */
  ackTimeoutMs?: number;
  /** Total push wallclock cap in ms (default 10 minutes). */
  totalTimeoutMs?: number;
  /** Image version string (default "unspecified"). */
  version?: string;
  /** Reboot the LP after activation (default true). */
  rebootAfterActivate?: boolean;
  /** Progress callback — fires on every status frame from firmware. */
  onProgress?: (status: FwUpdateStatus) => void;
  // ─── Hardening (Gap 2 + Gap 3 — see docs/lp-am2434-ota-hardening-plan.md) ───
  /** How long to wait for the LP's auto-pushed FwBankInfo frame after
   *  TCP connect (default 3000 ms). If the LP doesn't send BankInfo
   *  within this window, the push proceeds anyway (logs a warning) —
   *  this is a soft gate, not a hard requirement, so legacy firmware
   *  without BankInfo support stays compatible. */
  bankInfoTimeoutMs?: number;
  /** If true (default), refuse to push when the LP's currently-running
   *  Alpha-N is *higher* than this push's Alpha-N (per `version` opt).
   *  This is the bridge-side downgrade-rejection net; the LP-side
   *  enforcement (Gap 2-LP) is a separate fix. Set false to allow
   *  intentional downgrades (e.g. rolling back a broken release). */
  rejectDowngrade?: boolean;
  /** Called once when the LP's auto-pushed FwBankInfo is received
   *  (or once with null on timeout). Useful for the installer to
   *  build an audit log of "what did this board look like before
   *  we touched it". */
  onBankInfo?: (info: FwBankInfo | null) => void;
}

export class OtaPushError extends Error {
  constructor(public readonly kind: string, msg: string) {
    super(msg);
    this.name = 'OtaPushError';
  }
}

export interface PushImageResult {
  bytesSent: number;
  totalSize: number;
  imageCrc: number;
  /** Last status frame received from firmware. */
  finalStatus: FwUpdateStatus | null;
  /** The pre-push FwBankInfo we captured from the LP on connect (Gap 3).
   *  Null if the LP didn't send one within `bankInfoTimeoutMs`. */
  preBankInfo: FwBankInfo | null;
}

/**
 * Stream `imagePath` to the LP at `host:port`, drive the
 * Begin/Chunk/Finalize/Activate sequence, and resolve when the
 * firmware acknowledges activation (or rejects with an error).
 *
 * **Phase 1A behavior (today):** firmware will reply
 * `state=FW_ERROR, error_code=1 (NOT_IMPL)` to the very first Begin
 * frame.  This function will surface that as
 * `OtaPushError("notImplemented", ...)` so the bridge can report a
 * clean "firmware does not yet support flash writes" upstream.
 */
export async function pushImage(
  host: string,
  imagePath: string,
  opts: PushImageOptions = {},
): Promise<PushImageResult> {
  const port      = opts.port      ?? LP_OTA_PORT;
  const chunkSize = opts.chunkSize ?? 1024;
  const ackTimeout    = opts.ackTimeoutMs    ?? 4000;
  const totalTimeout  = opts.totalTimeoutMs  ?? 10 * 60_000;
  const version       = opts.version         ?? 'unspecified';
  const rebootAfterActivate = opts.rebootAfterActivate ?? true;
  const bankInfoTimeout = opts.bankInfoTimeoutMs ?? 3000;
  const rejectDowngrade = opts.rejectDowngrade ?? true;

  const image = await fs.readFile(imagePath);
  if (image.length === 0) {
    throw new OtaPushError('emptyImage', `image file is empty: ${imagePath}`);
  }
  const imageCrc = crc32(image);

  // ── connection & RX framing ────────────────────────────────────────
  const sock = await new Promise<net.Socket>((resolve, reject) => {
    const s = new net.Socket();
    const onErr = (e: Error) => { reject(new OtaPushError('connect', e.message)); };
    s.once('error', onErr);
    s.connect(port, host, () => {
      s.removeListener('error', onErr);
      resolve(s);
    });
  });

  let rx = Buffer.alloc(0);
  /** queue of awaiters wanting the next status frame */
  const statusWaiters: Array<{
    resolve: (s: FwUpdateStatus) => void;
    reject:  (e: Error) => void;
    timer:   NodeJS.Timeout;
  }> = [];

  const settleAllWaiters = (err: Error) => {
    while (statusWaiters.length > 0) {
      const w = statusWaiters.shift()!;
      clearTimeout(w.timer);
      w.reject(err);
    }
  };

  // ── Pre-push FwBankInfo capture (Gap 3) ────────────────────────────
  // LP auto-pushes one FwBankInfo frame on TCP connect. We capture it,
  // expose it to the installer for audit logging, and (optionally) gate
  // the push on it for downgrade detection (Gap 2 bridge-side).
  let preBankInfo: FwBankInfo | null = null;
  let bankInfoResolver: ((info: FwBankInfo | null) => void) | null = null;
  const bankInfoPromise = new Promise<FwBankInfo | null>((resolve) => {
    bankInfoResolver = resolve;
  });

  sock.on('data', (chunk: Buffer) => {
    rx = Buffer.concat([rx, chunk]);
    while (rx.length >= 4) {
      const total = rx.readUInt32BE(0);
      if (total === 0 || total > 4096) {
        settleAllWaiters(new OtaPushError('badFrame', `bad len ${total}`));
        sock.destroy();
        return;
      }
      if (rx.length < 4 + total) return;
      const tag = rx[4];
      const body = rx.subarray(5, 4 + total);
      rx = rx.subarray(4 + total);
      if (tag === LpOtaTag.Status) {
        try {
          const s = ota_internal.decodeStatus(body);
          opts.onProgress?.(s);
          const w = statusWaiters.shift();
          if (w) {
            clearTimeout(w.timer);
            w.resolve(s);
          }
        } catch (e) {
          settleAllWaiters(new OtaPushError('decode',
            `status decode: ${(e as Error).message}`));
          sock.destroy();
          return;
        }
      } else if (tag === LpOtaTag.BankInfo) {
        try {
          preBankInfo = ota_internal.decodeBankInfo(body);
          if (bankInfoResolver) { bankInfoResolver(preBankInfo); bankInfoResolver = null; }
        } catch (e) {
          // Bad BankInfo shouldn't kill the push (legacy compat) — just log.
          console.warn(`[ota-push ${host}] BankInfo decode failed: ${(e as Error).message}`);
        }
      }
      // Unknown tags silently skipped (forward compatibility).
    }
  });

  sock.on('close', () => {
    settleAllWaiters(new OtaPushError('disconnect',
      'firmware closed socket mid-push'));
  });
  sock.on('error', (e) => {
    settleAllWaiters(new OtaPushError('socket', e.message));
  });

  const wallTimer = setTimeout(() => {
    settleAllWaiters(new OtaPushError('timeout',
      `push exceeded ${totalTimeout}ms`));
    sock.destroy();
  }, totalTimeout);

  function awaitStatus(): Promise<FwUpdateStatus> {
    return new Promise<FwUpdateStatus>((resolve, reject) => {
      const timer = setTimeout(() => {
        const idx = statusWaiters.findIndex(w => w.timer === timer);
        if (idx >= 0) statusWaiters.splice(idx, 1);
        reject(new OtaPushError('ackTimeout',
          `no FwUpdateStatus within ${ackTimeout}ms`));
      }, ackTimeout);
      statusWaiters.push({ resolve, reject, timer });
    });
  }

  function send(tag: number, body: Buffer): void {
    sock.write(frame(tag, body));
  }

  function checkStatus(s: FwUpdateStatus, where: string): void {
    if (s.state === FwUpdateState.Error) {
      const kind = s.errorCode === 1 ? 'notImplemented' : 'firmwareError';
      throw new OtaPushError(kind,
        `firmware ${where} error code=${s.errorCode} msg="${s.errorMessage}"`);
    }
  }

  let bytesSent = 0;
  let finalStatus: FwUpdateStatus | null = null;

  // 0.A.182: image-level retry on LP_OTA_ERR_BANK_B_REDO (code 26). DAC
  // writes have stochastic byte loss; LP-side or per-chunk retries wedge
  // the chip. The clean recovery is: BEGIN re-erases Bank B (fresh chip
  // slate), then re-stream all chunks. Each re-erase resets whatever
  // post-partial-PP state was wedging the chip. Cap at `bankRedoLimit`
  // full attempts.
  const LP_OTA_ERR_BANK_B_REDO = 26;
  const bankRedoLimit = 5;
  let bankAttempt = 0;
  let bankClean = false;

  try {
    // ── Gate: wait for LP's auto-pushed FwBankInfo (Gap 3) ─────────────
    // Soft gate — if the LP doesn't speak BankInfo (legacy fw), proceed
    // anyway with a warning. The optional rejectDowngrade check below
    // only fires when we DID receive BankInfo.
    const bankInfoTimer = new Promise<null>(r => setTimeout(() => r(null), bankInfoTimeout));
    preBankInfo = await Promise.race([bankInfoPromise, bankInfoTimer]);
    if (preBankInfo) {
      const active = preBankInfo.activeBank === 1 /* BankA */ ? preBankInfo.bankAVersion
                   : preBankInfo.activeBank === 2 /* Golden */ ? preBankInfo.goldenVersion
                   : preBankInfo.bankBVersion;
      console.log(`[ota-push ${host}] pre-push state: activeBank=${preBankInfo.activeBank} ` +
                  `version="${active}" bootCount=${preBankInfo.bootCount} ` +
                  `bootReason=${preBankInfo.bootReason} A.valid=${preBankInfo.bankAValid} ` +
                  `B.valid=${preBankInfo.bankBValid}`);
      if (rejectDowngrade) {
        const currentN = parseAlphaN(active);
        const incomingN = parseAlphaN(version);
        if (currentN !== null && incomingN !== null && incomingN < currentN) {
          throw new OtaPushError('downgradeBlocked',
            `${host} is running 0.A.${currentN} ("${active}"); refusing to push 0.A.${incomingN} ("${version}"). ` +
            `Pass rejectDowngrade=false to override.`);
        }
      }
    } else {
      console.warn(`[ota-push ${host}] no FwBankInfo received within ${bankInfoTimeout}ms ` +
                   `— proceeding without pre-flight bank check (legacy fw?)`);
    }
    opts.onBankInfo?.(preBankInfo);

    while (bankAttempt < bankRedoLimit && !bankClean) {
      bankAttempt++;
      bytesSent = 0;

      // ── Begin ────────────────────────────────────────────────────────
      send(LpOtaTag.Begin,
           encodeBegin(image.length, imageCrc, version, chunkSize));
      finalStatus = await awaitStatus();
      checkStatus(finalStatus, 'Begin');

      // ── Chunks (single-shot per chunk, no per-chunk retry) ──────────
      for (let off = 0; off < image.length; off += chunkSize) {
        const slice = image.subarray(off, Math.min(off + chunkSize, image.length));
        send(LpOtaTag.Chunk, encodeChunk(off, slice, crc32(slice)));
        finalStatus = await awaitStatus();
        checkStatus(finalStatus, `Chunk@${off}`);
        bytesSent = off + slice.length;
      }

      // ── Finalize — LP runs full Bank-B CRC here ──────────────────────
      send(LpOtaTag.Finalize, encodeFinalize(imageCrc));
      finalStatus = await awaitStatus();
      if (finalStatus.state === FwUpdateState.Error &&
          finalStatus.errorCode === LP_OTA_ERR_BANK_B_REDO) {
        // Bank B CRC mismatch — DAC byte loss. Restart full BEGIN-through-
        // FINALIZE flow. BEGIN re-erases Bank B = fresh chip slate.
        opts.onProgress?.({
          ...finalStatus,
          errorMessage:
            `Bank B redo (attempt ${bankAttempt}/${bankRedoLimit}): ${finalStatus.errorMessage}`,
        });
        continue;
      }
      checkStatus(finalStatus, 'Finalize');
      bankClean = true;
    }
    if (!bankClean) {
      throw new OtaPushError('bankBRedoExhausted',
        `Bank B CRC mismatched on all ${bankRedoLimit} attempts`);
    }

    // ── Activate ─────────────────────────────────────────────────────
    send(LpOtaTag.Activate, encodeActivate(rebootAfterActivate));
    // The Activate response may or may not arrive — firmware may reset
    // before we get the ack back.  Wait briefly; either outcome is OK.
    try {
      finalStatus = await awaitStatus();
      checkStatus(finalStatus, 'Activate');
    } catch (e) {
      if (e instanceof OtaPushError &&
          (e.kind === 'ackTimeout' || e.kind === 'disconnect')) {
        // Expected when firmware reboots immediately.
      } else {
        throw e;
      }
    }
  } finally {
    clearTimeout(wallTimer);
    sock.destroy();
  }

  return { bytesSent, totalSize: image.length, imageCrc, finalStatus, preBankInfo };
}
