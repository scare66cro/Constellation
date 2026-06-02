/**
 * VFD client — subscribes to OrbitSensorBank pushes from Nova, slices
 * per-drive snapshots, and routes operator actions through OrbitRegWrite.
 *
 * Phase 4b Sub-3 (2026-06-02) rewrite. The previous implementation
 * opened a Modbus TCP socket directly to a VFD adapter (FENA-01 / ABB
 * ACS310 sim) on the Pi5's network. That breaks the production
 * airgap: in production the Pi5 has no IP route to the equipment LAN.
 *
 * New transport path:
 *   bridge → UART → Nova → Modbus TCP → STORAGE orbit → RS485 → VFD
 *
 * Nova polls each STORAGE orbit's HR 100..147 region (the "VFD
 * passthrough" window per orbit_storage.h: 3 drives × 16 regs) as a
 * `OrbitSensorBank` envelope at `hr_base=100`. This module subscribes,
 * decodes per-unit slices via the canonical 16-reg layout below, and
 * exposes the same `getDrives()` / `sendAction()` / `writeDrive()`
 * surface as the old client so `apiRoutes.ts` doesn't need to change.
 *
 * Canonical 16-reg layout per drive (orbit-side RS485 RTU maps the
 * manufacturer's native register addresses into these slots):
 *
 *   Offset  Field                       Source on ABB ACS310
 *   ──────  ─────────                   ──────────────────────
 *   0       controlWord            R/W  reg 0 (process data)
 *   1       speedRefPercent×100    R/W  reg 1 (process data)  0..10000
 *   2       statusWord              R   reg 2 (process data)
 *   3       actualSpeedPercent      R   reg 3 (process data)  0..10000
 *   4       outputFreqHz×10         R   reg 100 (01.01)
 *   5       motorCurrentA×100       R   reg 103 (01.04)
 *   6       motorPowerKw×100        R   reg 105 (01.06)
 *   7       dcBusVoltage            R   reg 106 (01.07)
 *   8       driveTemp×10            R   reg 108 (01.09)
 *   9       faultCode               R   reg 400 (04.01)
 *   10      maxFreqHz×10            R   reg 2002 (20.02)
 *   11      rampUpTime×10           R   reg 2202 (22.02)
 *   12      rampDownTime×10         R   reg 2203 (22.03)
 *   13      ratedCurrentA×100       R   reg 9906 (99.06)
 *   14      ratedPowerKw×100        R   reg 9909 (99.09)
 *   15      ratedFreqHz×10          R   reg 9907 (99.07)
 *
 * Writes flow as raw register addresses inside the orbit's 100..147
 * window (drive_offset = 100 + (unitId - 1) × 16). The orbit's RTU
 * master then translates back to the manufacturer's native address
 * for the RS485 push.
 *
 * Layout is not finalized — the orbit firmware author may pick a
 * different shape based on what's efficient to scan via RS485 and
 * what other VFD families (Phase Tech DXL) need. When that decision
 * is made, the constants below + the `applySlice()` decoder update;
 * the wire stays as 16 raw u16 per drive either way.
 */

import { EventEmitter } from 'events';
import type { NovaDataStore, OrbitSensorBank } from './novaDataStore.js';
import type { CommandBridge } from './apiRoutes.js';
import type { DataCache } from './dataCache.js';

export type VFDManufacturer = 'abb-acs310' | 'abb-acs380' | 'phase-tech-dxl' | 'generic';

// ── Wire constants matching firmware (orbit_client.h) ──
const STORAGE_HR_VFD_BASE   = 100;     // ORBIT_ROLE_HR_STORAGE_BASE
const STORAGE_HR_VFD_COUNT  = 48;      // ORBIT_ROLE_HR_STORAGE_COUNT
const REGS_PER_DRIVE        = 16;
const DRIVES_PER_ORBIT      = STORAGE_HR_VFD_COUNT / REGS_PER_DRIVE;  // 3

// ── Canonical 16-reg layout offsets (see file header) ──
const F_CONTROL_WORD       = 0;
const F_SPEED_REF_X100     = 1;
const F_STATUS_WORD        = 2;
const F_ACTUAL_SPEED_X100  = 3;
const F_OUTPUT_FREQ_X10    = 4;
const F_CURRENT_A_X100     = 5;
const F_POWER_KW_X100      = 6;
const F_DC_BUS_V           = 7;
const F_TEMP_X10           = 8;
const F_FAULT_CODE         = 9;
const F_MAX_FREQ_X10       = 10;
const F_RAMP_UP_X10        = 11;
const F_RAMP_DOWN_X10      = 12;
const F_RATED_CUR_X100     = 13;
const F_RATED_POWER_X100   = 14;
const F_RATED_FREQ_X10     = 15;

// ── Snapshot shape (kept compatible with the previous client) ──
export interface VFDDriveSnapshot {
  unitId: number;
  online: boolean;
  label: string;
  manufacturer: VFDManufacturer;
  // Process data
  controlWord: number;
  speedRefPercent: number;   // 0..10000 (0..100.00 %)
  statusWord: number;
  actualSpeedPercent: number;
  // Group 01 actual signals
  outputFreqHz: number;      // 0.1 Hz
  freqRefHz: number;         // 0.1 Hz — not in the canonical layout; kept for API compat (always 0)
  motorSpeedRpm: number;     // rpm — not in canonical layout; kept for API compat (always 0)
  motorCurrentA: number;     // 0.1 A
  motorTorquePercent: number;// not in canonical layout; kept for API compat (always 0)
  motorPowerkW: number;      // 0.1 kW (canonical is ×100 — exposed as 0.1 for API compat by dividing)
  dcBusVoltage: number;      // V
  outputVoltage: number;     // not in canonical layout; kept for API compat (always 0)
  driveTemp: number;         // °C
  // Group 04
  faultCode: number;
  // Group 20
  minFreqHz: number;         // not in canonical layout; kept for API compat (always 0)
  maxFreqHz: number;         // 0.1 Hz
  // Group 22
  rampUpTime: number;        // 0.1 s
  rampDownTime: number;      // 0.1 s
  // Group 99
  ratedCurrentA: number;     // 0.1 A
  ratedVoltage: number;      // not in canonical layout; kept for API compat (always 0)
  ratedFreqHz: number;       // 0.1 Hz
  ratedSpeedRpm: number;     // not in canonical layout; kept for API compat (always 0)
  ratedPowerkW: number;      // 0.1 kW
  // Derived
  running: boolean;
  faulted: boolean;
  atReference: boolean;
  direction: number;         // 0 = FWD, 1 = REV
}

function emptySnapshot(unitId: number): VFDDriveSnapshot {
  return {
    unitId, online: false, label: `VFD Unit ${unitId}`, manufacturer: 'generic',
    controlWord: 0, speedRefPercent: 0, statusWord: 0, actualSpeedPercent: 0,
    outputFreqHz: 0, freqRefHz: 0, motorSpeedRpm: 0, motorCurrentA: 0,
    motorTorquePercent: 0, motorPowerkW: 0, dcBusVoltage: 0, outputVoltage: 0,
    driveTemp: 0, faultCode: 0,
    minFreqHz: 0, maxFreqHz: 0, rampUpTime: 0, rampDownTime: 0,
    ratedCurrentA: 0, ratedVoltage: 0, ratedFreqHz: 0, ratedSpeedRpm: 0, ratedPowerkW: 0,
    running: false, faulted: false, atReference: false, direction: 0,
  };
}

/** Per-drive metadata that lives only on the bridge (labels, manufacturer
 *  hint for UI display). Survives bridge restarts via `simConfig`. */
interface DriveMeta {
  label?: string;
  manufacturer?: VFDManufacturer;
}

// ── Status-word bit semantics — ABB ACS310/380 convention ──
// `statusWord` bits used to derive `running` / `faulted` / `atReference`
// / `direction`. These match the canonical 16-reg layout's reg-2 source
// (ABB process-data StatusWord, which is what the orbit's RTU master
// will copy in for ABB drives). For other families the orbit-side
// translation puts them in the same bits so this stays universal.
const SW_READY_RUN  = 1 << 1;   // 0x0002
const SW_AT_REF     = 1 << 8;   // 0x0100
const SW_FAULTED    = 1 << 3;   // 0x0008
const SW_DIRECTION  = 1 << 11;  // 0x0800 — set = REV

// ── ABB ACS310 CW values for start/stop/reset.  These are what the
// bridge writes to the orbit's HR 100+drive_offset+0 slot; the orbit
// forwards verbatim to the VFD over RS485. If the orbit decides to
// abstract these into role-agnostic command words later, this map
// moves bridge-side and the orbit-side handler decodes here. ──
const CW_START      = 0x047F;
const CW_STOP       = 0x047E;
const CW_RESET      = 0x0080;
const CW_POST_RESET = 0x047E;

// ── Options + dependency injection ──

export interface VFDClientOptions {
  /** Legacy field — retained for index.ts compatibility. The old
   *  client used this to open a Modbus TCP socket; the new client
   *  takes the VFD path through Nova so the host is ignored. */
  host: string;
  /** Legacy — see `host`. */
  port: number;
  /** Legacy — kept for type compatibility with index.ts. */
  maxScanId?: number;
  /** Legacy — kept for type compatibility with index.ts. */
  pollIntervalMs?: number;
}

/** Bridge-side dependencies needed for the new transport. Injected via
 *  `setBridgeContext()` after openSerial() resolves the Nova UART and
 *  the bridge's data layer is up. */
export interface VFDBridgeContext {
  novaStore: NovaDataStore;
  serialBridge: CommandBridge;
  dataCache: DataCache;
}

// ── Client ──

export class VFDClient extends EventEmitter {
  private drives: Map<number, VFDDriveSnapshot> = new Map();
  private meta:   Map<number, DriveMeta> = new Map();
  /** Last-known commanded state per drive — re-sent on `start()` reconnect
   *  to support stateless brownout recovery. (Today the orbit handles
   *  the heartbeat; this is bookkeeping for UI continuity only.) */
  private commandedState: Map<number, { cw: number; ref?: number }> = new Map();

  private ctx: VFDBridgeContext | null = null;
  private onBankFn: ((bank: OrbitSensorBank) => void) | null = null;
  private onChange: (() => void) | null = null;

  constructor(_opts: VFDClientOptions) {
    super();
    // Pre-populate empty snapshots so the API surface never returns
    // an empty list before the first bank push lands.
    for (let i = 1; i <= DRIVES_PER_ORBIT; i++) {
      this.drives.set(i, emptySnapshot(i));
    }
  }

  /** Wire up the bridge dependencies. Called once index.ts has finished
   *  `openSerial()` and the Nova bridge is ready. Until called, the
   *  client returns the pre-populated empty drives. */
  setBridgeContext(ctx: VFDBridgeContext): void {
    this.ctx = ctx;
  }

  onUpdate(cb: () => void): void {
    this.onChange = cb;
  }

  async start(): Promise<void> {
    // Subscribe to OrbitSensorBank pushes. Filter for STORAGE-role
    // orbits' VFD passthrough window (hrBase=100) and slice into
    // per-unit snapshots.
    if (!this.ctx) {
      console.warn('[VFDClient] start() called before setBridgeContext — drives will be empty until context wires up');
      return;
    }
    if (this.onBankFn) return;  // already started

    this.onBankFn = (bank: OrbitSensorBank) => {
      if (bank.hrBase !== STORAGE_HR_VFD_BASE) return;
      // Only consume from boards that the operator has assigned to
      // STORAGE role — bridge would never put VFDs on a GDC/TRITON
      // board, but we belt-and-suspender here to avoid mis-decoding
      // role-window banks from other slots that happen to land here.
      const board = this.ctx!.dataCache.getOrbitBoards()
        .find(b => b.slot === bank.slot);
      if (!board || board.role !== 1 /* STORAGE */) return;
      this.applyBank(bank);
    };
    this.ctx.novaStore.on('orbitSensorBank', this.onBankFn);
    console.log(`[VFDClient] subscribed to OrbitSensorBank @ hr_base=${STORAGE_HR_VFD_BASE} for STORAGE orbits`);
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
    const existing = this.meta.get(unitId) ?? {};
    if (m.label !== undefined)        existing.label        = m.label;
    if (m.manufacturer !== undefined) existing.manufacturer = m.manufacturer;
    this.meta.set(unitId, existing);
    // Apply to the live snapshot so getDrives() reflects immediately.
    const snap = this.drives.get(unitId);
    if (snap) {
      if (m.label        !== undefined) snap.label        = m.label;
      if (m.manufacturer !== undefined) snap.manufacturer = m.manufacturer;
    }
  }

  /** Write CW + optional speed ref to one drive. FC16 when both are
   *  provided so the drive sees them atomically; FC06 for CW-only. */
  async writeDrive(unitId: number, controlWord: number, speedRefPercent?: number): Promise<boolean> {
    const storageSlot = this.findStorageSlot();
    if (storageSlot === null || !this.ctx?.serialBridge.orbitRegWrite) return false;
    const baseAddr = STORAGE_HR_VFD_BASE + (unitId - 1) * REGS_PER_DRIVE;
    try {
      if (speedRefPercent !== undefined) {
        await this.ctx.serialBridge.orbitRegWrite(storageSlot, baseAddr + F_CONTROL_WORD,
          [controlWord & 0xFFFF, Math.max(0, Math.min(10000, speedRefPercent))]);
      } else {
        await this.ctx.serialBridge.orbitRegWrite(storageSlot, baseAddr + F_CONTROL_WORD,
          [controlWord & 0xFFFF]);
      }
      return true;
    } catch (err: any) {
      console.error(`[VFDClient] writeDrive unit ${unitId}: ${err.message}`);
      return false;
    }
  }

  /** Write a single register at a manufacturer-native address.
   *
   *  Currently NOT plumbed through to the orbit — the orbit's HR
   *  100..147 window is a fixed canonical-layout cache, not arbitrary
   *  manufacturer address space. When the orbit-side RTU master gains
   *  a "write to native address" command path (separate orbit-side
   *  envelope or extended HR region), this lights up. For now we
   *  short-circuit so the /iot/fans/param endpoint returns a clear
   *  not-supported error instead of silently writing to the wrong
   *  slot. */
  async writeParam(unitId: number, addr: number, _value: number): Promise<boolean> {
    console.warn(`[VFDClient] writeParam unit ${unitId} addr ${addr} not supported by Phase 4b Sub-3 transport — needs an orbit-side native-address write envelope`);
    return false;
  }

  /** Action-based control. Bridge composes the manufacturer-specific
   *  control word; orbit forwards it verbatim to the VFD over RS485. */
  async sendAction(
    unitId: number,
    action: 'start' | 'stop' | 'reset' | 'toggle-direction',
    speedRefPercent?: number,
  ): Promise<boolean> {
    const snap = this.drives.get(unitId);
    if (!snap) return false;
    const storageSlot = this.findStorageSlot();
    if (storageSlot === null || !this.ctx?.serialBridge.orbitRegWrite) return false;
    const baseAddr = STORAGE_HR_VFD_BASE + (unitId - 1) * REGS_PER_DRIVE;
    try {
      switch (action) {
        case 'start': {
          const ref = Math.max(0, Math.min(10000, Math.round((speedRefPercent ?? 0) * 100)));
          await this.ctx.serialBridge.orbitRegWrite(storageSlot, baseAddr + F_CONTROL_WORD,
            [CW_START, ref]);
          this.commandedState.set(unitId, { cw: CW_START, ref });
          break;
        }
        case 'stop':
          await this.ctx.serialBridge.orbitRegWrite(storageSlot, baseAddr + F_CONTROL_WORD,
            [CW_STOP]);
          this.commandedState.delete(unitId);
          break;
        case 'reset':
          await this.ctx.serialBridge.orbitRegWrite(storageSlot, baseAddr + F_CONTROL_WORD,
            [CW_RESET]);
          await new Promise(r => setTimeout(r, 500));
          await this.ctx.serialBridge.orbitRegWrite(storageSlot, baseAddr + F_CONTROL_WORD,
            [CW_POST_RESET]);
          snap.faulted = false;
          snap.faultCode = 0;
          this.commandedState.delete(unitId);
          console.log(`[VFDClient] Unit ${unitId} fault reset sent`);
          break;
        case 'toggle-direction': {
          // Flip the direction bit on the current CW. If the drive is
          // running, also re-send the speed ref so we don't accidentally
          // change setpoint while flipping direction.
          const newCw = snap.controlWord ^ SW_DIRECTION;
          if (snap.running && speedRefPercent !== undefined) {
            const ref = Math.max(0, Math.min(10000, Math.round(speedRefPercent * 100)));
            await this.ctx.serialBridge.orbitRegWrite(storageSlot, baseAddr + F_CONTROL_WORD,
              [newCw, ref]);
            this.commandedState.set(unitId, { cw: newCw, ref });
          } else {
            await this.ctx.serialBridge.orbitRegWrite(storageSlot, baseAddr + F_CONTROL_WORD,
              [newCw]);
            this.commandedState.set(unitId, { cw: newCw });
          }
          break;
        }
      }
      return true;
    } catch (err: any) {
      console.error(`[VFDClient] sendAction ${action} unit ${unitId} failed: ${err.message}`);
      return false;
    }
  }

  // ── Private ──

  /** Resolve the first STORAGE-role orbit slot. Production today has
   *  one STORAGE orbit per panel; if a panel ever ships with multiple
   *  STORAGE orbits the drives belong to whichever was discovered
   *  first (deterministic — sorted by slot index).  Multi-STORAGE-VFD
   *  is a follow-up if it becomes a customer ask. */
  private findStorageSlot(): number | null {
    if (!this.ctx) return null;
    const board = this.ctx.dataCache.getOrbitBoards()
      .filter(b => b.role === 1 && b.connected)
      .sort((a, b) => a.slot - b.slot)[0];
    return board ? board.slot : null;
  }

  /** Slice a single OrbitSensorBank into per-unit snapshots and merge
   *  into the live `drives` map. Notifies via onUpdate when anything
   *  changed. */
  private applyBank(bank: OrbitSensorBank): void {
    let changed = false;
    for (let unitIdx = 0; unitIdx < DRIVES_PER_ORBIT; unitIdx++) {
      const unitId = unitIdx + 1;
      const off = unitIdx * REGS_PER_DRIVE;
      const v = bank.values;
      // Defensive: a partial bank (under-populated values[]) should not
      // throw — leave unread fields at their previous value.
      if (off + REGS_PER_DRIVE > v.length) break;

      const snap = this.drives.get(unitId) ?? emptySnapshot(unitId);
      const meta = this.meta.get(unitId);

      // Pull canonical layout fields verbatim — UI-side formatting
      // handles the ×100 / ×10 scaling display.
      snap.controlWord        = v[off + F_CONTROL_WORD]      & 0xFFFF;
      snap.speedRefPercent    = v[off + F_SPEED_REF_X100]    & 0xFFFF;
      snap.statusWord         = v[off + F_STATUS_WORD]       & 0xFFFF;
      snap.actualSpeedPercent = v[off + F_ACTUAL_SPEED_X100] & 0xFFFF;
      snap.outputFreqHz       = (v[off + F_OUTPUT_FREQ_X10]  & 0xFFFF) / 10;
      snap.motorCurrentA      = (v[off + F_CURRENT_A_X100]   & 0xFFFF) / 100;
      snap.motorPowerkW       = (v[off + F_POWER_KW_X100]    & 0xFFFF) / 100;
      snap.dcBusVoltage       =  v[off + F_DC_BUS_V]         & 0xFFFF;
      snap.driveTemp          = (v[off + F_TEMP_X10]         & 0xFFFF) / 10;
      snap.faultCode          =  v[off + F_FAULT_CODE]       & 0xFFFF;
      snap.maxFreqHz          = (v[off + F_MAX_FREQ_X10]     & 0xFFFF) / 10;
      snap.rampUpTime         = (v[off + F_RAMP_UP_X10]      & 0xFFFF) / 10;
      snap.rampDownTime       = (v[off + F_RAMP_DOWN_X10]    & 0xFFFF) / 10;
      snap.ratedCurrentA      = (v[off + F_RATED_CUR_X100]   & 0xFFFF) / 100;
      snap.ratedPowerkW       = (v[off + F_RATED_POWER_X100] & 0xFFFF) / 100;
      snap.ratedFreqHz        = (v[off + F_RATED_FREQ_X10]   & 0xFFFF) / 10;

      // Apply operator metadata (label, manufacturer) if known.
      if (meta?.label)        snap.label        = meta.label;
      if (meta?.manufacturer) snap.manufacturer = meta.manufacturer;

      // Derive convenience flags from the canonical status word.
      snap.running     = (snap.statusWord & SW_READY_RUN) !== 0;
      snap.faulted     = (snap.statusWord & SW_FAULTED)   !== 0;
      snap.atReference = (snap.statusWord & SW_AT_REF)    !== 0;
      snap.direction   = (snap.statusWord & SW_DIRECTION) ? 1 : 0;

      // "Online" heuristic: any non-zero reg in the slice means the
      // orbit's RTU master has populated this drive at least once.
      // Pure-zero slice means either the RTU hasn't run yet (today,
      // pre-orbit-RTU-impl) or the drive is genuinely silent.
      let anyNonZero = false;
      for (let k = 0; k < REGS_PER_DRIVE; k++) {
        if ((v[off + k] & 0xFFFF) !== 0) { anyNonZero = true; break; }
      }
      snap.online = anyNonZero;

      this.drives.set(unitId, snap);
      changed = true;
    }
    if (changed && this.onChange) this.onChange();
  }
}
