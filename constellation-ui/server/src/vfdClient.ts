/**
 * VFD client — vendor-agnostic via the profile registry in
 * `vfdRegisterMaps.ts`.
 *
 * Phase 4b Sub-3 architecture (2026-06-02): at bridge startup the
 * client reads the operator's drive list (unit_id ↔ manufacturer
 * pairs, persisted via `simConfig` under key `vfd-drives`) and:
 *
 *   1. Looks up each drive's profile, builds a `VfdPollConfig` from
 *      `profile.pollEntries` × `unitId`, and ships it to Nova via the
 *      new envelope (tag 127). Nova forwards to the STORAGE orbit.
 *   2. Subscribes to `OrbitSensorBank` pushes at `(STORAGE_slot,
 *      hr_base=100)` — the same wire the firmware already publishes
 *      (Phase 4b Sub-1) — and decodes each drive's 16-slot slice
 *      through its profile's `slots` map.
 *   3. For operator actions (start/stop/reset), composes the
 *      action's `VfdActionStep[]` sequence into `OrbitRegWrite` calls
 *      at the right cache slots — the orbit forwards over RS485 per
 *      each slot's `nativeAddr` + `fc`.
 *
 * Vendor knowledge stays bridge-side (TypeScript files, free to grow).
 * The STORAGE orbit's binary footprint stays constant regardless of
 * how many vendors / drive models we support.
 *
 * Same external API (`getDrives`, `getDrive`, `sendAction`, `writeDrive`,
 * `setDriveMeta`) as the previous client so `apiRoutes.ts` doesn't
 * change. `writeParam` remains stubbed — needs orbit-side support to
 * forward arbitrary native-address writes (separate envelope path or
 * extended cache region; future work).
 */

import { EventEmitter } from 'events';
import type { NovaDataStore, OrbitSensorBank } from './novaDataStore.js';
import type { CommandBridge } from './apiRoutes.js';
import type { DataCache } from './dataCache.js';
import type { NovaSerialBridge } from './novaSerialBridge.js';
import {
  type VFDManufacturer,
  type VfdProfile,
  getProfile,
  buildPollEntries,
} from './vfdRegisterMaps.js';

export type { VFDManufacturer } from './vfdRegisterMaps.js';

// ── Wire constants matching firmware (orbit_client.h) ──
const STORAGE_HR_VFD_BASE   = 100;     // ORBIT_ROLE_HR_STORAGE_BASE
const REGS_PER_DRIVE        = 16;
const DRIVES_PER_ORBIT      = 3;       // STORAGE_HR_VFD_COUNT (48) / 16

// ── Snapshot shape (kept API-compatible with the previous client) ──
export interface VFDDriveSnapshot {
  unitId: number;
  online: boolean;
  label: string;
  manufacturer: VFDManufacturer;
  // Process data
  controlWord: number;
  speedRefPercent: number;
  statusWord: number;
  actualSpeedPercent: number;
  // Live signals (units documented per-field; scale applied by decoder)
  outputFreqHz: number;
  freqRefHz: number;
  motorSpeedRpm: number;
  motorCurrentA: number;
  motorTorquePercent: number;
  motorPowerkW: number;
  dcBusVoltage: number;
  inputVoltage: number;
  outputVoltage: number;
  driveTemp: number;
  // Diagnostics
  faultCode: number;
  // Limits + ramps
  minFreqHz: number;
  maxFreqHz: number;
  rampUpTime: number;
  rampDownTime: number;
  // Nameplate
  ratedCurrentA: number;
  ratedVoltage: number;
  ratedFreqHz: number;
  ratedSpeedRpm: number;
  ratedPowerkW: number;
  // Vendor-specific
  pressureSetpointPsi: number;
  opMode: number;    // 0 = Off, 1 = Manual, 2 = Auto (Phase Tech HOA)
  // Derived
  running: boolean;
  faulted: boolean;
  atReference: boolean;
  direction: number;
}

function emptySnapshot(unitId: number): VFDDriveSnapshot {
  return {
    unitId, online: false, label: `VFD Unit ${unitId}`, manufacturer: 'generic',
    controlWord: 0, speedRefPercent: 0, statusWord: 0, actualSpeedPercent: 0,
    outputFreqHz: 0, freqRefHz: 0, motorSpeedRpm: 0, motorCurrentA: 0,
    motorTorquePercent: 0, motorPowerkW: 0, dcBusVoltage: 0, inputVoltage: 0,
    outputVoltage: 0, driveTemp: 0, faultCode: 0,
    minFreqHz: 0, maxFreqHz: 0, rampUpTime: 0, rampDownTime: 0,
    ratedCurrentA: 0, ratedVoltage: 0, ratedFreqHz: 0, ratedSpeedRpm: 0, ratedPowerkW: 0,
    pressureSetpointPsi: 0, opMode: 0,
    running: false, faulted: false, atReference: false, direction: 0,
  };
}

/** Per-drive operator-configured metadata stored in `simConfig::vfd-drives`. */
interface DriveAssignment {
  unitId: number;
  manufacturer: VFDManufacturer;
  label?: string;
}

export interface VFDClientOptions {
  // Legacy fields — retained for type compatibility with index.ts.
  host: string;
  port: number;
  maxScanId?: number;
  pollIntervalMs?: number;
}

export interface VFDBridgeContext {
  novaStore: NovaDataStore;
  serialBridge: CommandBridge;
  dataCache: DataCache;
  /** Direct reference to the NovaSerialBridge so we can call
   *  `sendVfdPollConfig` — that send isn't part of the structural
   *  CommandBridge interface (which is per-route surface). */
  novaBridge: NovaSerialBridge;
}

export class VFDClient extends EventEmitter {
  private drives: Map<number, VFDDriveSnapshot> = new Map();
  /** Unit → profile lookup. Built from operator's drive-assignment
   *  config on first call to `applyAssignments()`. */
  private assignments: Map<number, DriveAssignment> = new Map();

  private ctx: VFDBridgeContext | null = null;
  private onBankFn: ((bank: OrbitSensorBank) => void) | null = null;
  private onChange: (() => void) | null = null;

  /** Last slot that we've sent a VfdPollConfig for. Used to re-replay
   *  on STORAGE-slot change (operator re-assigning role) without
   *  spamming the orbit on every poll. */
  private lastConfigSentForSlot = -1;
  /** Lock to avoid concurrent maybeSendConfig() invocations stacking
   *  multiple in-flight sendVfdPollConfig calls (each awaits an
   *  Ack and overlapping ones would time out). */
  private sendInFlight = false;

  constructor(_opts: VFDClientOptions) {
    super();
    for (let i = 1; i <= DRIVES_PER_ORBIT; i++) {
      this.drives.set(i, emptySnapshot(i));
    }
  }

  setBridgeContext(ctx: VFDBridgeContext): void {
    this.ctx = ctx;
  }

  onUpdate(cb: () => void): void {
    this.onChange = cb;
  }

  /** Wire up the operator's drive assignments. Called by index.ts after
   *  the `vfd-drives` config file is loaded. Each entry: { unitId,
   *  manufacturer, label? }. Drives not present here default to the
   *  `generic` profile (empty schedule). */
  applyAssignments(rows: DriveAssignment[]): void {
    this.assignments.clear();
    for (const r of rows) {
      this.assignments.set(r.unitId, r);
      const snap = this.drives.get(r.unitId);
      if (snap) {
        snap.manufacturer = r.manufacturer;
        if (r.label) snap.label = r.label;
      }
    }
    // Re-send VfdPollConfig now that assignments may have changed.
    this.lastConfigSentForSlot = -1;
    void this.maybeSendConfig();
  }

  async start(): Promise<void> {
    if (!this.ctx) {
      console.warn('[VFDClient] start() called before setBridgeContext — drives will be empty until context wires up');
      return;
    }
    if (this.onBankFn) return;

    this.onBankFn = (bank: OrbitSensorBank) => {
      // Opportunistic config retry — the first bank arrival of any kind
      // is also when orbit topology is live in dataCache. maybeSendConfig
      // is idempotent (skips when lastConfigSentForSlot already matches),
      // so calling it on every bank is cheap.
      void this.maybeSendConfig();

      if (bank.hrBase !== STORAGE_HR_VFD_BASE) return;
      const board = this.ctx!.dataCache.getOrbitBoards()
        .find(b => b.slot === bank.slot);
      if (!board || board.role !== 1 /* STORAGE */) return;
      this.applyBank(bank);
    };
    this.ctx.novaStore.on('orbitSensorBank', this.onBankFn);
    console.log(`[VFDClient] subscribed to OrbitSensorBank @ hr_base=${STORAGE_HR_VFD_BASE} for STORAGE orbits`);

    // Try sending VfdPollConfig now in case the STORAGE slot is
    // already discovered. If not, the first bank arrival will retry
    // via applyBank() → maybeSendConfig().
    await this.maybeSendConfig();
  }

  stop(): void {
    if (this.ctx && this.onBankFn) {
      this.ctx.novaStore.off('orbitSensorBank', this.onBankFn);
      this.onBankFn = null;
    }
    console.log('[VFDClient] Stopped');
  }

  getDrives(): VFDDriveSnapshot[] {
    return Array.from(this.drives.values());
  }

  getDrive(unitId: number): VFDDriveSnapshot | undefined {
    return this.drives.get(unitId);
  }

  setDriveMeta(unitId: number, m: { label?: string; manufacturer?: VFDManufacturer }): void {
    const a = this.assignments.get(unitId) ?? { unitId, manufacturer: 'generic' as VFDManufacturer };
    if (m.label !== undefined)        a.label = m.label;
    if (m.manufacturer !== undefined) a.manufacturer = m.manufacturer;
    this.assignments.set(unitId, a);
    const snap = this.drives.get(unitId);
    if (snap) {
      if (m.label        !== undefined) snap.label        = m.label;
      if (m.manufacturer !== undefined) snap.manufacturer = m.manufacturer;
    }
    // Manufacturer changed → re-send config so the orbit re-polls
    // against the new profile.
    if (m.manufacturer !== undefined) {
      this.lastConfigSentForSlot = -1;
      void this.maybeSendConfig();
    }
  }

  /** Write CW + optional speed ref to one drive (legacy API). The new
   *  preferred path is `sendAction(start, ref)` which translates per
   *  vendor profile; `writeDrive` issues raw cache_slot writes that
   *  only make sense for ABB-style protocols. */
  async writeDrive(unitId: number, controlWord: number, speedRefPercent?: number): Promise<boolean> {
    const storageSlot = this.findStorageSlot();
    if (storageSlot === null || !this.ctx?.serialBridge.orbitRegWrite) return false;
    const driveBase = STORAGE_HR_VFD_BASE + (unitId - 1) * REGS_PER_DRIVE;
    try {
      if (speedRefPercent !== undefined) {
        await this.ctx.serialBridge.orbitRegWrite(storageSlot, driveBase + 0,
          [controlWord & 0xFFFF, Math.max(0, Math.min(10000, speedRefPercent))]);
      } else {
        await this.ctx.serialBridge.orbitRegWrite(storageSlot, driveBase + 0,
          [controlWord & 0xFFFF]);
      }
      return true;
    } catch (err: any) {
      console.error(`[VFDClient] writeDrive unit ${unitId}: ${err.message}`);
      return false;
    }
  }

  /** Write a vendor-native parameter. Currently NOT plumbed — needs
   *  an orbit-side native-address write path (e.g. a separate
   *  envelope, or extended cache region pre-configured with the
   *  param's slot mapping). */
  async writeParam(unitId: number, addr: number, _value: number): Promise<boolean> {
    console.warn(`[VFDClient] writeParam unit ${unitId} addr ${addr} not supported by Sub-3 vendor-agnostic transport — needs an orbit-side native-address write path`);
    return false;
  }

  async sendAction(
    unitId: number,
    action: 'start' | 'stop' | 'reset' | 'toggle-direction',
    speedRefPercent?: number,
  ): Promise<boolean> {
    const snap = this.drives.get(unitId);
    if (!snap) return false;
    const storageSlot = this.findStorageSlot();
    if (storageSlot === null || !this.ctx?.serialBridge.orbitRegWrite) return false;

    const profile = getProfile(snap.manufacturer);
    let steps;
    switch (action) {
      case 'start': steps = profile.actions.start(speedRefPercent); break;
      case 'stop':  steps = profile.actions.stop();  break;
      case 'reset': steps = profile.actions.reset(); break;
      case 'toggle-direction':
        // No vendor-agnostic way to express this; ABB-only legacy.
        // Profile authors who need it should add a profile.actions.toggleDirection.
        console.warn(`[VFDClient] toggle-direction unit ${unitId}: not supported by vendor-agnostic profile (${snap.manufacturer})`);
        return false;
    }
    if (steps.length === 0) {
      console.warn(`[VFDClient] action ${action} unit ${unitId}: profile ${snap.manufacturer} has no steps`);
      return false;
    }

    const driveBase = STORAGE_HR_VFD_BASE + (unitId - 1) * REGS_PER_DRIVE;
    try {
      for (let i = 0; i < steps.length; i++) {
        const step = steps[i];
        await this.ctx.serialBridge.orbitRegWrite(
          storageSlot,
          driveBase + step.cacheSlot,
          step.values.map(v => v & 0xFFFF),
        );
        if (step.delayAfterMs && i < steps.length - 1) {
          await new Promise(r => setTimeout(r, step.delayAfterMs));
        }
      }
      // Optimistic local state update for actions whose effect is
      // observable in the snapshot before the next bank push.
      if (action === 'reset') {
        snap.faulted = false;
        snap.faultCode = 0;
      }
      return true;
    } catch (err: any) {
      console.error(`[VFDClient] sendAction ${action} unit ${unitId} failed: ${err.message}`);
      return false;
    }
  }

  // ── Private ──

  private findStorageSlot(): number | null {
    if (!this.ctx) return null;
    const board = this.ctx.dataCache.getOrbitBoards()
      .filter(b => b.role === 1 && b.connected)
      .sort((a, b) => a.slot - b.slot)[0];
    return board ? board.slot : null;
  }

  /** Compose + send the VfdPollConfig for whichever STORAGE slot is
   *  currently visible. Idempotent — skips when the slot hasn't
   *  changed since last send.  Operator config changes / drive
   *  re-assignment force a re-send by zeroing `lastConfigSentForSlot`. */
  private async maybeSendConfig(): Promise<void> {
    if (!this.ctx) return;
    if (this.sendInFlight) return;
    const storageSlot = this.findStorageSlot();
    if (storageSlot === null) return;
    if (storageSlot === this.lastConfigSentForSlot) return;

    const entries = [];
    for (const a of this.assignments.values()) {
      if (a.unitId < 1 || a.unitId > DRIVES_PER_ORBIT) {
        console.warn(`[VFDClient] assignment unit_id=${a.unitId} outside the orbit's 3-drive window — skipping`);
        continue;
      }
      const profile = getProfile(a.manufacturer);
      if (profile.pollEntries.length === 0) continue;
      // Translate this drive's profile-relative cache slots into the
      // orbit-window-relative cache slots: orbit cache index =
      // (unitId-1) * REGS_PER_DRIVE + profileSlot.
      const driveOffset = (a.unitId - 1) * REGS_PER_DRIVE;
      const drivePollEntries = buildPollEntries(profile, a.unitId).map(e => ({
        ...e,
        cacheSlot: e.cacheSlot + driveOffset,
      }));
      entries.push(...drivePollEntries);
    }

    this.sendInFlight = true;
    try {
      await this.ctx.novaBridge.sendVfdPollConfig(storageSlot, entries);
      this.lastConfigSentForSlot = storageSlot;
      console.log(`[VFDClient] sent VfdPollConfig for STORAGE slot ${storageSlot}: ${entries.length} entries across ${this.assignments.size} drive(s)`);
    } catch (err: any) {
      console.error(`[VFDClient] sendVfdPollConfig failed: ${err.message}`);
    } finally {
      this.sendInFlight = false;
    }
  }

  /** Decode one OrbitSensorBank push into the per-drive snapshots. */
  private applyBank(bank: OrbitSensorBank): void {
    // The bank covers all 48 cache slots; each drive owns 16. Drive N
    // (unitId N) lives at offsets (N-1)*16 .. N*16 - 1.
    let changed = false;
    for (let unitIdx = 0; unitIdx < DRIVES_PER_ORBIT; unitIdx++) {
      const unitId = unitIdx + 1;
      const driveOffset = unitIdx * REGS_PER_DRIVE;
      if (driveOffset + REGS_PER_DRIVE > bank.values.length) break;

      const snap = this.drives.get(unitId) ?? emptySnapshot(unitId);
      const profile = getProfile(snap.manufacturer);

      // Walk the profile's slot definitions; for each defined slot,
      // pull the raw u16, apply scale + custom decoder, write the
      // canonical snapshot field.
      let anyNonZero = false;
      for (let k = 0; k < REGS_PER_DRIVE; k++) {
        const raw = bank.values[driveOffset + k] & 0xFFFF;
        if (raw !== 0) anyNonZero = true;
        const def = profile.slots[k];
        if (!def) continue;
        const scaled = def.decode ? def.decode(raw) : (raw * (def.scale ?? 1));
        // Cast snap to any so the dynamic field name is allowed.
        (snap as any)[def.field] = scaled;
      }

      // For profiles that surface `statusWord` (ABB-style), derive
      // the running / faulted / atReference / direction flags. Profiles
      // that DON'T (Phase Tech etc.) already set `running` directly
      // from a discrete-input slot, and leave statusWord at 0.
      if (profile.slots[2]?.field === 'statusWord') {
        snap.running     = (snap.statusWord & (1 << 1))  !== 0;
        snap.faulted     = (snap.statusWord & (1 << 3))  !== 0;
        snap.atReference = (snap.statusWord & (1 << 8))  !== 0;
        snap.direction   = (snap.statusWord & (1 << 11)) ? 1 : 0;
      } else {
        // Phase-Tech-style: faulted iff faultCode > 0; running came
        // straight from cache_slot 0 already.
        snap.faulted = (snap.faultCode ?? 0) > 0;
      }

      snap.online = anyNonZero;
      this.drives.set(unitId, snap);
      changed = true;
    }
    if (changed && this.onChange) this.onChange();
  }
}
