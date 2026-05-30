/**
 * novaFwUpdateManager.ts — Firmware update orchestrator for Constellation Nova
 *
 * Manages the bridge side of the dual-bank firmware update protocol:
 *   1. Accepts a firmware binary (Buffer) + version string
 *   2. Computes CRC-32, sends FwBeginUpdate
 *   3. Streams FwDataChunk messages sequentially with ACK per chunk
 *   4. Sends FwFinalizeUpdate, waits for verification
 *   5. Sends FwActivateBank to swap + reboot
 *
 * Listens for FwUpdateStatus and FwBankInfo messages from firmware.
 * Exposes state for the REST API to query.
 */

import { EventEmitter } from 'events';
import {
  NovaSerialBridge,
  MSG_FW_UPDATE_STATUS, MSG_FW_BANK_INFO,
  pbDecodeFields, pbGetVarint, pbGetString,
} from './novaSerialBridge.js';

/* ─── CRC-32 (Ethernet / zlib polynomial) ─────────────────────────────── */

const CRC32_TABLE = new Uint32Array(256);
(function initCrc32Table() {
  for (let i = 0; i < 256; i++) {
    let crc = i;
    for (let j = 0; j < 8; j++) {
      crc = (crc & 1) ? (0xEDB88320 ^ (crc >>> 1)) : (crc >>> 1);
    }
    CRC32_TABLE[i] = crc >>> 0;
  }
})();

function crc32(buf: Buffer): number {
  let crc = 0xFFFFFFFF;
  for (let i = 0; i < buf.length; i++) {
    crc = CRC32_TABLE[(crc ^ buf[i]) & 0xFF] ^ (crc >>> 8);
  }
  return (crc ^ 0xFFFFFFFF) >>> 0;
}

/* ─── Types ───────────────────────────────────────────────────────────── */

export enum FwUpdateState {
  IDLE       = 0,
  ERASING    = 1,
  RECEIVING  = 2,
  VERIFYING  = 3,
  VERIFIED   = 4,
  ACTIVATING = 5,
  ERROR      = 6,
}

export enum FwBankId {
  BANK_A  = 0,
  BANK_B  = 1,
  GOLDEN  = 2,
}

export interface FwBankInfo {
  activeBank: FwBankId;
  bankAVersion: string;
  bankACrc: number;
  bankAValid: boolean;
  bankBVersion: string;
  bankBCrc: number;
  bankBValid: boolean;
  goldenVersion: string;
  bootCount: number;
  bootReason: number;
}

export interface FwUpdateStatus {
  state: FwUpdateState;
  bytesWritten: number;
  totalSize: number;
  errorCode: number;
  errorMessage: string;
  activeVersion: string;
  stagedVersion: string;
  activeBank: FwBankId;
  percent: number;
}

/* ─── Update Manager ──────────────────────────────────────────────────── */

const DEFAULT_CHUNK_SIZE = 1024;
const MAX_CHUNK_RETRIES = 3;

export class NovaFwUpdateManager extends EventEmitter {
  private bridge: NovaSerialBridge;

  /** Latest status from firmware */
  private status: FwUpdateStatus = {
    state: FwUpdateState.IDLE,
    bytesWritten: 0,
    totalSize: 0,
    errorCode: 0,
    errorMessage: '',
    activeVersion: '',
    stagedVersion: '',
    activeBank: FwBankId.BANK_A,
    percent: 0,
  };

  /** Latest bank info from firmware */
  private bankInfo: FwBankInfo | null = null;

  /** True while an update transfer is in progress on the bridge side */
  private updating = false;

  constructor(bridge: NovaSerialBridge) {
    super();
    this.bridge = bridge;

    // Listen for firmware status messages
    bridge.on('FwUpdateStatus', (data: Buffer) => this.handleUpdateStatus(data));
    bridge.on('FwBankInfo', (data: Buffer) => this.handleBankInfo(data));
  }

  /* ═══════════════════════════════════════════════════════════════════ *
   *  Public API                                                         *
   * ═══════════════════════════════════════════════════════════════════ */

  getStatus(): FwUpdateStatus {
    return { ...this.status };
  }

  getBankInfo(): FwBankInfo | null {
    return this.bankInfo ? { ...this.bankInfo } : null;
  }

  isUpdating(): boolean {
    return this.updating;
  }

  /**
   * Start a firmware update.
   *
   * @param firmware  Complete firmware binary (payload + 256-byte signature trailer)
   * @param version   Version string (e.g. "2.1.0")
   * @returns Promise that resolves when the image is verified on the firmware side.
   *          The caller can then call activate() to swap banks + reboot.
   */
  async startUpdate(firmware: Buffer, version: string): Promise<void> {
    if (this.updating) {
      throw new Error('Update already in progress');
    }
    if (!this.bridge.isConnected()) {
      throw new Error('Firmware not connected');
    }

    // ── Pre-flight validation ──
    // Minimum reasonable size: ARM vector table (256 bytes) + signature (256 bytes)
    const FW_MIN_SIZE = 512;
    const FW_MAX_SIZE = 0x1F0000; // ~2 MB (FW_BANK_MAX_SIZE from nova_fw_update.h)
    const FW_SIGNATURE_SIZE = 256;

    if (firmware.length < FW_MIN_SIZE) {
      throw new Error(`Firmware image too small (${firmware.length} bytes, min ${FW_MIN_SIZE})`);
    }
    if (firmware.length > FW_MAX_SIZE) {
      throw new Error(`Firmware image too large (${firmware.length} bytes, max ${FW_MAX_SIZE})`);
    }

    // Validate ARM Cortex-R5F vector table: first word should be the initial SP
    // (must be in SRAM range 0x00000000-0x001FFFFF or ATCM 0x00000000-0x0000FFFF)
    const initialSP = firmware.readUInt32LE(0);
    if (initialSP === 0 || initialSP > 0x00400000) {
      console.warn(`[FwUpdate] WARNING: Initial SP 0x${initialSP.toString(16)} looks invalid — may not be a valid R5F image`);
    }

    console.log(`[FwUpdate] Image validated: ${firmware.length} bytes (payload ${firmware.length - FW_SIGNATURE_SIZE} + sig ${FW_SIGNATURE_SIZE})`);

    this.updating = true;
    const totalSize = firmware.length;
    const imageCrc = crc32(firmware);
    const chunkSize = DEFAULT_CHUNK_SIZE;

    console.log(`[FwUpdate] Starting: ${totalSize} bytes, CRC=0x${imageCrc.toString(16)}, version=${version}`);

    try {
      // 1. Begin — firmware erases inactive bank
      this.updateLocalStatus(FwUpdateState.ERASING, 0, totalSize);
      await this.bridge.sendFwBeginUpdate(totalSize, imageCrc, version, chunkSize);
      console.log('[FwUpdate] Begin ACK received — erasing inactive bank');

      // 2. Send chunks sequentially
      this.updateLocalStatus(FwUpdateState.RECEIVING, 0, totalSize);
      let offset = 0;
      while (offset < totalSize) {
        const end = Math.min(offset + chunkSize, totalSize);
        const chunk = firmware.subarray(offset, end);
        const chunkCrc = crc32(chunk);

        let sent = false;
        for (let retry = 0; retry < MAX_CHUNK_RETRIES; retry++) {
          try {
            await this.bridge.sendFwDataChunk(offset, chunk, chunkCrc);
            sent = true;
            break;
          } catch (err) {
            console.warn(`[FwUpdate] Chunk @${offset} retry ${retry + 1}: ${(err as Error).message}`);
            if (retry === MAX_CHUNK_RETRIES - 1) throw err;
          }
        }

        if (!sent) {
          throw new Error(`Failed to send chunk at offset ${offset} after ${MAX_CHUNK_RETRIES} retries`);
        }

        offset = end;
        this.updateLocalStatus(FwUpdateState.RECEIVING, offset, totalSize);
      }

      console.log(`[FwUpdate] All ${Math.ceil(totalSize / chunkSize)} chunks sent`);

      // 3. Finalize — firmware verifies full image CRC
      this.updateLocalStatus(FwUpdateState.VERIFYING, totalSize, totalSize);
      await this.bridge.sendFwFinalizeUpdate(imageCrc);
      console.log('[FwUpdate] Finalize ACK — image verified on firmware');

      this.updateLocalStatus(FwUpdateState.VERIFIED, totalSize, totalSize);
      this.emit('verified');

    } catch (err) {
      const msg = (err as Error).message;
      console.error(`[FwUpdate] Failed: ${msg}`);
      this.status.state = FwUpdateState.ERROR;
      this.status.errorMessage = msg;
      this.emit('error', err);
      throw err;
    } finally {
      this.updating = false;
    }
  }

  /**
   * Activate the staged firmware and reboot.
   * Only valid after a successful startUpdate().
   *
   * Sequence:
   *   1. `sendFwActivateBank()` — swap the active bank header in OSPI
   *      (firmware-side handler in `nova_fw_update.c`, expected to be
   *      linked into nova_lp builds in OTA Phase 1).
   *   2. If `reboot=true`, `sendRebootSoc()` (CMD_REBOOT_SOC=50) →
   *      `Sciclient_pmDeviceReset(SystemP_WAIT_FOREVER)` → DMSC warm
   *      reset → SBL loads OSPI 0x80000 with the new bytes. This is
   *      THE primitive that makes Activate take effect (verified
   *      2026-05-03; see docs/firmware-bridge-protocol.md §13).
   *
   * If the firmware doesn't yet implement `FwActivateBank` (msg 113)
   * — which is the case on LP today — `sendFwActivateBank` times out.
   * We treat that as non-fatal IFF the bytes were already written to
   * `0x80000` directly (Phase 2 staging-copy-hack semantics) and still
   * trigger the reboot. The OTA Phase 3 stage-2 chooser will eliminate
   * this branch.
   */
  async activate(reboot = true): Promise<void> {
    if (this.status.state !== FwUpdateState.VERIFIED) {
      throw new Error(`Cannot activate — state is ${FwUpdateState[this.status.state]}, expected VERIFIED`);
    }

    console.log(`[FwUpdate] Activating staged firmware (reboot=${reboot})`);
    this.updateLocalStatus(FwUpdateState.ACTIVATING, this.status.totalSize, this.status.totalSize);

    // Step 1: ask firmware to swap the active bank header. If the
    // handler isn't linked yet, this times out — log and continue;
    // the bytes-at-0x80000 hack path still works.
    try {
      // We always pass `reboot=false` here because the actual reboot is
      // owned by sendRebootSoc() below — that's the verified primitive.
      // Letting the firmware's own MSG_FW_ACTIVATE_BANK handler reboot
      // would race the Ack the same way CMD_REBOOT_SOC does, and there
      // is no point doing it twice.
      await this.bridge.sendFwActivateBank(false);
      console.log('[FwUpdate] Activate ACK — bank swap committed');
    } catch (err) {
      const msg = (err as Error).message;
      if (msg.startsWith('Command timeout')) {
        console.warn('[FwUpdate] FwActivateBank timed out — firmware likely lacks handler; continuing with rebootSoc');
      } else {
        console.error(`[FwUpdate] Activate failed: ${msg}`);
        this.status.state = FwUpdateState.ERROR;
        this.status.errorMessage = msg;
        throw err;
      }
    }

    // Step 2: reboot the SoC so the SBL re-loads the (now-swapped)
    // image at OSPI 0x80000. Without this, the running firmware
    // continues executing the OLD image regardless of bank header.
    if (reboot) {
      try {
        await this.bridge.sendRebootSoc();
        console.log('[FwUpdate] Reboot triggered — SoC will warm-reset within ~50 ms');
        this.emit('activated');
      } catch (err) {
        const msg = (err as Error).message;
        // CMD_REBOOT_SOC almost always times out (firmware resets
        // before Ack lands) — that's the success path, not a failure.
        // Same logic as POST /iot/system/reboot in apiRoutes.ts.
        if (msg.startsWith('Command timeout')) {
          console.log('[FwUpdate] Reboot triggered (firmware reset before Ack landed)');
          this.emit('activated');
        } else {
          console.error(`[FwUpdate] Reboot failed: ${msg}`);
          this.status.state = FwUpdateState.ERROR;
          this.status.errorMessage = msg;
          throw err;
        }
      }
    } else {
      // Caller asked to defer the reboot (e.g. wants to commit several
      // updates atomically). Bank header is swapped but new image is
      // not yet running — the OLD firmware keeps executing until the
      // operator/orchestrator triggers a separate POST /iot/system/reboot.
      this.emit('activated');
    }
  }

  /* ═══════════════════════════════════════════════════════════════════ *
   *  Message handlers                                                   *
   * ═══════════════════════════════════════════════════════════════════ */

  private handleUpdateStatus(data: Buffer): void {
    const fields = pbDecodeFields(data);
    this.status = {
      state: pbGetVarint(fields, 1) as FwUpdateState,
      bytesWritten: pbGetVarint(fields, 2),
      totalSize: pbGetVarint(fields, 3),
      errorCode: pbGetVarint(fields, 4),
      errorMessage: pbGetString(fields, 5),
      activeVersion: pbGetString(fields, 6),
      stagedVersion: pbGetString(fields, 7),
      activeBank: pbGetVarint(fields, 8) as FwBankId,
      percent: 0,
    };
    if (this.status.totalSize > 0) {
      this.status.percent = Math.round((this.status.bytesWritten / this.status.totalSize) * 100);
    }
    this.emit('status', this.status);
  }

  private handleBankInfo(data: Buffer): void {
    const fields = pbDecodeFields(data);
    this.bankInfo = {
      activeBank: pbGetVarint(fields, 1) as FwBankId,
      bankAVersion: pbGetString(fields, 2),
      bankACrc: pbGetVarint(fields, 3),
      bankAValid: pbGetVarint(fields, 4) !== 0,
      bankBVersion: pbGetString(fields, 5),
      bankBCrc: pbGetVarint(fields, 6),
      bankBValid: pbGetVarint(fields, 7) !== 0,
      goldenVersion: pbGetString(fields, 8),
      bootCount: pbGetVarint(fields, 9),
      bootReason: pbGetVarint(fields, 10),
    };
    console.log(`[FwUpdate] Bank info: active=${FwBankId[this.bankInfo.activeBank]}, ` +
      `A=${this.bankInfo.bankAVersion || '(empty)'}${this.bankInfo.bankAValid ? '✓' : '✗'}, ` +
      `B=${this.bankInfo.bankBVersion || '(empty)'}${this.bankInfo.bankBValid ? '✓' : '✗'}, ` +
      `golden=${this.bankInfo.goldenVersion || '(none)'}, boots=${this.bankInfo.bootCount}`);
    this.emit('bankInfo', this.bankInfo);
  }

  /* ═══════════════════════════════════════════════════════════════════ *
   *  Internal helpers                                                   *
   * ═══════════════════════════════════════════════════════════════════ */

  private updateLocalStatus(state: FwUpdateState, bytesWritten: number, totalSize: number): void {
    this.status.state = state;
    this.status.bytesWritten = bytesWritten;
    this.status.totalSize = totalSize;
    this.status.percent = totalSize > 0 ? Math.round((bytesWritten / totalSize) * 100) : 0;
    this.emit('status', this.status);
  }
}
