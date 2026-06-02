/**
 * REST API routes for the bridge server.
 *
 * These replace the C-based GellertGet/GellertPost FastCGI handlers.
 * The UI still uses REST for:
 *   - Fetching page data on navigation (GET /iot/<pageName>)
 *   - Sending button presses (POST /iot/button)
 *   - Saving settings (POST /iot/PostSave.jsp)
 *   - File uploads (POST /iot/upgrade/upload)
 *   - Various one-off GETs (displays, settings/files, etc.)
 *
 * The WebSocket channels handle the high-frequency polling data.
 * REST handles low-frequency request-response operations.
 */

import { Router, type Request, type Response } from 'express';
import crypto from 'node:crypto';
import * as fs from 'node:fs';
import * as path from 'node:path';
import * as os from 'node:os';
import Busboy from 'busboy';
import type { DataCache } from './dataCache.js';
import type { UpgradeManager } from './upgradeManager.js';
import type { NovaLogDataStore } from './novaLogDataStore.js';
import type { VFDClient } from './vfdClient.js';
import type { NovaFwUpdateManager } from './novaFwUpdateManager.js';
import type { NovaDataStore } from './novaDataStore.js';
import type { MasterSlaveSync, SlavePushBody } from './masterSlaveSync.js';
import type { RemoteSystemsSync } from './remoteSystemsSync.js';
import nodemailer from 'nodemailer';

/**
 * Minimal command surface the API layer needs from whatever speaks to the
 * controller. In Constellation this is the Nova `commandDispatcher`
 * defined in `index.ts`, which routes imperative button/system/log-clear
 * actions through the COBS+protobuf bridge. Settings updates do NOT go
 * through here — the UI POSTs encoded protobuf to `/proto/write/<field>`
 * directly. Kept as a structural interface so tests/mocks can supply a
 * stub without depending on the bridge implementation.
 */
export interface CommandBridge {
  sendPost(body: string): Promise<string> | string;
  /** Optional: assign an Orbit slot's zone role and persist to firmware OSPI.
   *  Provided by the Nova shim; not present on test stubs. */
  assignOrbitRole?: (slot: number, role: number, opts?: { zoneId?: number; legacySlot?: number; refrigStage?: number }) => Promise<void>;
  /** Optional: assign which equipment a given orbit AO drives. Persists
   *  to firmware OSPI Settings.AoEquip[slot][channel]. */
  assignAoEquip?: (slot: number, channel: number, equip: number) => Promise<void>;
  /** Optional: forward a single Modbus HR write to a Triton orbit via
   *  the LP firmware's Modbus TCP client (envelope tag 125). */
  tritonRegWrite?: (slot: number, addr: number, value: number) => Promise<void>;
  /** Optional: forward a role-agnostic Modbus HR write (FC06 for one
   *  value or FC16 for a block) to ANY orbit via the LP firmware's
   *  Modbus TCP client (envelope tag 126). Phase 4b Sub-phase 2. */
  orbitRegWrite?: (slot: number, addr: number, values: number[]) => Promise<void>;
  /** Optional: trigger a controller-SoC warm reset via DMSC
   *  (CMD_REBOOT_SOC = 50). Used by the OTA Activate primitive and
   *  the operator "Reboot Controller" action. Resolves once the
   *  firmware Acks the request — the actual reset happens ~50 ms
   *  later (UART drain). */
  rebootSoc?: () => Promise<void>;
  /** Optional: replay a settings export back into firmware. See
   *  `importSettings` in index.ts protoForwards for the per-entry
   *  semantics; called by POST /iot/settings/import. */
  importSettings?: (entries: { settingsField: number; b64: string }[]) => Promise<{ ok: number; total: number; failed: { settingsField: number; error: string }[] }>;
}
import { getProfile } from './vfdRegisterMaps.js';
// (Apr 27 2026) WARNING_KEYS / DEFAULT_WARNING_TEXT import removed —
// the alerts page derives those tables client-side now
// (constellation-ui/src/lib/business/warningKeys.ts +
// warningText.ts). Bridge `warningTranslator.ts` was deleted in the
// same commit — it had no remaining consumers.
import { saveConfig, loadConfig } from './simConfig.js';
import { getNovaId } from './dataCache.js';
import {
  // Phase 4b 2026-06-01: read + write primitives are no longer imported.
  // Sub-phase 1 swapped reads to `novaStore.getOrbitBank(slot, hr_base)`;
  // Sub-phase 2 swapped writes to `serialBridge.orbitRegWrite()` /
  // `serialBridge.tritonRegWrite()`. The only surviving import is
  // `resolveOrbitHost`, used by `/iot/orbit/ota/version` to probe the
  // remote LP's lp_ota_task at TCP :5503 (which is itself targeted for
  // Phase 4 / Phase 4b future-work but not in this sub-phase).
  resolveOrbitHost,
  asS16,
} from './orbitMbtcp.js';
import { fetchBankInfo } from './orbitOtaClient.js';
import {
  installBundle, getCurrentInstall, isInstalling, InstallError,
} from './firmwareInstaller.js';
import { BundleError, CFU_EXTENSION } from './firmwareBundle.js';
import * as fsp from 'fs/promises';
import * as fssync from 'fs';
import {
  loadAccountMeta,
  saveAccountMeta,
  resolveSlotByUsername,
  recordLogin,
  recordLoginFailure,
  recordLogout,
  recordSave,
  readAudit,
  upsertCloudLink,
  removeCloudLink,
  findCloudLinkByUserId,
  findCloudLinkByToken,
  findCloudLinkByUsername,
  touchCloudLinkLogin,
  setCloudLinkPassword,
  verifyCloudLinkPassword,
  type SlotRole,
  type CloudLink,
} from './accountStore.js';

// â”€â”€â”€ Auth helpers (ECDH + CryptoJS-compatible AES) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

/**
 * CryptoJS-compatible EVP_BytesToKey derivation.
 * When CryptoJS.AES.encrypt(data, passphrase) is given a *string* passphrase,
 * it uses this MD5-based key derivation with a random 8-byte salt.
 */
function evpBytesToKey(passphrase: Buffer, salt: Buffer, keyLen = 32, ivLen = 16): { key: Buffer; iv: Buffer } {
  const totalLen = keyLen + ivLen;
  const blocks: Buffer[] = [];
  let prev = Buffer.alloc(0);
  while (Buffer.concat(blocks).length < totalLen) {
    prev = crypto.createHash('md5').update(Buffer.concat([prev, passphrase, salt])).digest();
    blocks.push(prev);
  }
  const derived = Buffer.concat(blocks);
  return { key: derived.subarray(0, keyLen), iv: derived.subarray(keyLen, keyLen + ivLen) };
}

/**
 * Decrypt a CryptoJS.AES.encrypt(plaintext, passphraseString) ciphertext.
 * Output is OpenSSL format: base64("Salted__" + 8-byte-salt + ciphertext).
 */
function decryptCryptoJS(encryptedBase64: string, passphrase: string): string {
  const buf = Buffer.from(encryptedBase64, 'base64');
  if (buf.subarray(0, 8).toString('utf8') !== 'Salted__') {
    throw new Error('Missing Salted__ prefix');
  }
  const salt = buf.subarray(8, 16);
  const ciphertext = buf.subarray(16);
  const { key, iv } = evpBytesToKey(Buffer.from(passphrase, 'utf8'), salt);
  const decipher = crypto.createDecipheriv('aes-256-cbc', key, iv);
  return Buffer.concat([decipher.update(ciphertext), decipher.final()]).toString('utf8');
}

/**
 * Decide the access level given clear-text type (username) and password.
 * Mirrors the ARM firmware's PasswordAuth() in StorePostData.c.
 *
 *   passwordField === 'clear'     â†’ 0  (logout)
 *   passwordField === 'leveldown' â†’ 1  (drop from L2 â†’ L1)
 *   typeField     === 'DEFAULT'   â†’ 1  (no password configured)
 *   passwordField matches factory â†’ 2  (installer / factory)
 *   any non-empty user+password   â†’ 1  (operator)
 *   else                          â†’ -2 (unauthorized)
 */
const FACTORY_PASSWORD = '4GR1*';                // factory/installer password for level 2

/** RFC-4180-ish CSV escape: quote only when the cell contains a separator,
 *  quote, or newline; doubles internal quotes. */
function csvCell(v: unknown): string {
  const s = String(v ?? '');
  return /[",\r\n]/.test(s) ? `"${s.replace(/"/g, '""')}"` : s;
}

function authenticateLocal(typeField: string, passwordField: string): number {
  if (passwordField === 'clear') return 0;
  if (passwordField === 'leveldown') return 1;
  if (passwordField === FACTORY_PASSWORD || typeField === FACTORY_PASSWORD) return 2;
  if (typeField === 'DEFAULT') return 1;
  if ((typeField || passwordField) && passwordField !== '') return 1;
  return -2;
}

/** Label the actor for audit purposes. */
function resolveActor(typeField: string, level: number, userAccounts: string[]): { actor: string; slot: number | null } {
  if (level <= 0) return { actor: 'anonymous', slot: null };
  if (typeField === FACTORY_PASSWORD || !typeField) return { actor: 'factory', slot: null };
  if (typeField === 'DEFAULT') return { actor: 'default', slot: null };
  const slot = resolveSlotByUsername(typeField, userAccounts);
  return { actor: typeField, slot };
}

let serverECDH: crypto.ECDH | null = null;
let sharedSecret: string | null = null;
let currentLevel = 0;
let currentActor = 'anonymous';
let currentSlot: number | null = null;

/**
 * Read-only accessors for the current authenticated session, exposed so
 * other bridge modules (e.g. /proto/write/:field in index.ts) can attach
 * actor/slot/level to audit entries.
 */
export function getCurrentAuth(): { actor: string; slot: number | null; level: number } {
  return { actor: currentActor, slot: currentSlot, level: currentLevel };
}

// ─── Orbit topology constants ─────────────────────────────────────────
//
// The bridge no longer talks to the orbit-simulator REST API. All
// orbit-side data flows over Modbus TCP via `orbitMbtcp.ts` (see
// /memories/repo/bridge-mbtcp-migration.md). `ORBIT_API_PORT`,
// `ORBIT_HTTP_HOST`, `orbitUrl()`, and `syncIoConfigToOrbit()` were
// retired in this migration:
//   • IO label sync: LP firmware owns labels via Settings.IoConfig.
//   • All read/write ops: Modbus TCP FC03/06/16 to the orbit's HR map.
//   • Per-slot host: `resolveOrbitHost(slot)` in orbitMbtcp.ts.

/**
 * Maximum number of Orbit boards the bridge supports.
 * Matches NOVA_MAX_ORBITS in firmware hal_orbit.h.
 */
const NOVA_MAX_ORBITS = 16;

// ─── Late-bound hooks ───────────────────────────────────────────────────
// Modules constructed AFTER createApiRoutes() (e.g. ProtoStream, which
// needs httpServer + novaStore + vfdClient assembled first) register
// callbacks here so route handlers can notify them without circular
// imports or constructor reordering.
let onTcpipSavedHook: (() => void) | null = null;
export function setTcpipSavedHook(cb: () => void): void {
  onTcpipSavedHook = cb;
}

// firmware-progress WS broadcast — set by index.ts after WsManager is
// constructed (createApiRoutes runs first; see ordering in index.ts).
// firmwareInstaller.ts calls this on every progress tick.
let firmwareProgressBroadcast: ((data: unknown) => void) | null = null;
export function setFirmwareProgressBroadcast(cb: (data: unknown) => void): void {
  firmwareProgressBroadcast = cb;
}

export function createApiRoutes(dataCache: DataCache, serialBridge: CommandBridge, upgradeManager?: UpgradeManager, logDataStore?: NovaLogDataStore, vfdClient?: VFDClient, fwUpdateManager?: NovaFwUpdateManager, _orbitClientLegacy?: unknown, novaStore?: NovaDataStore, masterSlaveSync?: MasterSlaveSync, remoteSystemsSync?: RemoteSystemsSync): Router {
  const router = Router();
  const RS485_API = `http://localhost:${process.env.RS485_PANEL_PORT ?? '9001'}`;

  // Hydrate DJANGO_TOKEN from persisted iotConfig on startup so /iot/cloud/*
  // endpoints work after a bridge restart without re-saving the token.
  if (!process.env.DJANGO_TOKEN) {
    const savedIot = loadConfig<{ AccessToken?: string }>('iotConfig');
    if (savedIot?.AccessToken) {
      process.env.DJANGO_TOKEN = savedIot.AccessToken;
      console.log('[API] Loaded DJANGO_TOKEN from iotConfig');
    }
  }

  // Bridge is a TRANSPARENT PASSTHROUGH (see CLAUDE.md invariant 6 +
  // memories/repo/data-path-rules.md): no temperature unit conversion, no scaling.
  // Conversion to user units happens exactly once, in firmware
  // Nova_Firmware/Platform/nova_thread_overrides.c::ReadAnalogBoards()
  // (uses Settings.TempType to deliver °F or °C). Doing it again here
  // produced 118 °F readings on the analog page (~47.8 °C × 1.8 + 32).
  // (Apr 2026: convertSensorTemp helper removed alongside the
  // /iot/sensors/unified + /iot/analog/all routes — last consumer.)


  // (Apr 27 2026) AS2_EXCLUDES / AGRISTAR_EXCLUDES / NON_ALERT_KEYS /
  // getBoardType / getIncludedAlertIndices / getFilteredAlertLabels and
  // their /alert/{labels,indices} routes were deleted: the alerts page
  // now derives both arrays client-side from `warningKeys.ts` +
  // `warningText.ts` + a hardcoded AGRISTAR exclude set
  // (see constellation-ui/src/lib/business/alertMetadata.ts).
  // Constellation hardware is always Agri-Star; the legacy AS2 branch
  // disappears with this cleanup. WARN_NEWBOARD is no longer referenced
  // by anything in this file.

  type Rs485Board = {
    address: number;
    boardType: number;
    label: string;
    firmwareVersion: [number, number];
    disabled: boolean;
    sensors: Array<{
      uiType: number;
      label: string;
      offset: number;
      disabled: boolean;
      value: number;
    }>;
  };

  const TEMP_LIKE_TYPES = new Set([0, 3, 4, 5, 9, 11]);

  // RS485 control-plane sync helper.
  // DEV-only: production firmware owns the RS485 link directly and there
  // is no rs485Panel listener on RS485_PANEL_PORT. Skip silently in prod.
  async function syncAnalogCacheFromRs485(): Promise<boolean> {
    if (process.env.DEV_ORBIT_PROBE === '0') return false;
    try {
      const response = await fetch(`${RS485_API}/api/boards`);
      if (!response.ok) return false;

      const payload = await response.json() as { ok?: boolean; boards?: Rs485Board[] };
      if (!payload.ok || !Array.isArray(payload.boards) || payload.boards.length === 0) return false;

      const analogParts: string[] = [];
      const sensorParts: string[] = [];
      const labelParts: string[] = [];

      for (const board of payload.boards) {
        const addr1 = board.address + 1;
        const ver = board.firmwareVersion && board.firmwareVersion.length >= 2
          ? `${board.firmwareVersion[0]}.${board.firmwareVersion[1]}`
          : '1.0';

        analogParts.push(
          String(addr1),
          String(board.boardType ?? 3),
          String(board.label ?? ''),
          ver,
          board.disabled ? '1' : '0',
        );

        for (let sensorIndex = 0; sensorIndex < 4; sensorIndex++) {
          const sensor = board.sensors?.[sensorIndex] ?? {
            uiType: 255,
            label: '',
            offset: 0,
            disabled: false,
            value: 0,
          };
          const sid = board.address * 4 + sensorIndex;
          const uiType = Number(sensor.uiType ?? 255);
          const value = Number(sensor.value ?? 0);
          const valueStr = TEMP_LIKE_TYPES.has(uiType)
            ? value.toFixed(1)
            : value.toFixed(0);

          sensorParts.push(
            String(sensor.label ?? ''),
            String(sid),
            String(uiType),
            valueStr,
            Number(sensor.offset ?? 0).toFixed(1),
            sensor.disabled ? '1' : '0',
          );

          labelParts.push(String(sensor.label ?? ''), String(sid), String(uiType));
        }
      }

      dataCache.updateFromArm('AnalogAll', analogParts.join(','));
      dataCache.updateFromArm('SensorList', sensorParts.join(','));
      dataCache.updateFromArm('SensorLabelData', labelParts.join(','));
      return true;
    } catch {
      return false;
    }
  }

  async function persistAnalogSaveToRs485(body: unknown): Promise<void> {
    // DEV-only: see syncAnalogCacheFromRs485 above.
    if (process.env.DEV_ORBIT_PROBE === '0') return;
    if (!Array.isArray(body) || body.length < 25) return;

    const address = Number(body[0]) - 1;
    if (!Number.isInteger(address) || address < 0) return;

    const sensors = Array.from({ length: 4 }, (_, sensorIndex) => {
      const base = 5 + sensorIndex * 5;
      return {
        uiType: Number(body[base] ?? 255),
        label: String(body[base + 1] ?? ''),
        offset: Number(body[base + 2] ?? 0),
        disabled: String(body[base + 3] ?? '0') === '1',
      };
    });

    const patch = {
      address,
      label: String(body[2] ?? ''),
      disabled: String(body[4] ?? '0') === '1',
      sensors,
    };

    await fetch(`${RS485_API}/api/boards/update`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(patch),
    });
  }

  // ── Identity endpoint ───────────────────────────────────────────────
  // Returns this Nova's persistent UUID and panel name.
  // Other Novas query this during discovery so groups survive DHCP IP changes.
  router.get('/identity', (_req: Request, res: Response) => {
    const panelName = dataCache.getByVarName('PanelName')?.value || 'Agristar Panel';
    res.json({ novaId: getNovaId(), panelName });
  });

  // ── Master/Slave outside-air sync (replaces legacy AS2 UDP broadcast) ──
  // POST /iot/master-slave/push   — peer master pushes its current
  //   outside_temp / outside_humid (tenths of °C / %RH) to this
  //   panel. Validated against MasterSlaveSettings.allowedMasters +
  //   masterIp before being forwarded to the controller LP via
  //   SettingsUpdate.remoteOutside (settings.proto field 41).
  // GET  /iot/master-slave/status — operator-facing snapshot of the
  //   sync state for the Level-2 master/slave page.
  // Detailed protocol notes live in masterSlaveSync.ts.
  router.post('/master-slave/push', async (req: Request, res: Response) => {
    if (!masterSlaveSync) { res.status(503).json({ error: 'sync_not_wired' }); return; }
    const body = req.body as SlavePushBody;
    /* req.ip can be ::ffff:10.x.y.z behind dual-stack; strip the
     * IPv4-mapped prefix so the allowlist (which stores plain v4
     * strings) compares cleanly. */
    const sourceIp = (req.ip ?? '').replace(/^::ffff:/, '');
    const err = await masterSlaveSync.receivePush(sourceIp, body);
    if (err) { res.status(403).json({ error: err }); return; }
    res.json({ ok: true });
  });
  router.get('/master-slave/status', (_req: Request, res: Response) => {
    if (!masterSlaveSync) { res.status(503).json({ error: 'sync_not_wired' }); return; }
    res.json(masterSlaveSync.getStatus());
  });
  router.get('/master-slave/discover', async (_req: Request, res: Response) => {
    if (!masterSlaveSync) { res.status(503).json({ error: 'sync_not_wired' }); return; }
    try {
      const peers = await masterSlaveSync.discoverSlaves();
      res.json({ peers });
    } catch (err: any) {
      res.status(500).json({ error: err?.message ?? String(err) });
    }
  });

  // ── Remote Systems list (UUID-keyed, DHCP-resilient) ───────────────
  // The bridge keeps a persistent list of peer panels the operator
  // wants to monitor, polled every 30 s via /iot/identity, with the
  // same auto-heal pattern as master/slave (matches by novaId so
  // panel renames AND DHCP changes both heal cleanly). Detailed
  // notes in remoteSystemsSync.ts.
  router.get('/remote-systems', (_req: Request, res: Response) => {
    if (!remoteSystemsSync) { res.status(503).json({ error: 'sync_not_wired' }); return; }
    res.json({ systems: remoteSystemsSync.list() });
  });
  router.post('/remote-systems', async (req: Request, res: Response) => {
    if (!remoteSystemsSync) { res.status(503).json({ error: 'sync_not_wired' }); return; }
    const body = req.body as { name?: string; host?: string; port?: number };
    if (!body || typeof body.host !== 'string' || !body.host.trim()) {
      res.status(400).json({ error: 'host required' });
      return;
    }
    try {
      const entry = await remoteSystemsSync.add({
        name: body.name,
        host: body.host,
        port: typeof body.port === 'number' ? body.port : undefined,
      });
      res.json({ ok: true, system: entry });
    } catch (err: any) {
      res.status(400).json({ error: err?.message ?? String(err) });
    }
  });
  router.delete('/remote-systems/:id', (req: Request, res: Response) => {
    if (!remoteSystemsSync) { res.status(503).json({ error: 'sync_not_wired' }); return; }
    const ok = remoteSystemsSync.remove(req.params.id);
    res.status(ok ? 200 : 404).json({ ok });
  });
  router.patch('/remote-systems/:id', (req: Request, res: Response) => {
    if (!remoteSystemsSync) { res.status(503).json({ error: 'sync_not_wired' }); return; }
    const body = req.body as { name?: string };
    if (typeof body?.name !== 'string') {
      res.status(400).json({ error: 'name required' });
      return;
    }
    const ok = remoteSystemsSync.rename(req.params.id, body.name);
    res.status(ok ? 200 : 404).json({ ok });
  });
  router.get('/remote-systems/discover', async (_req: Request, res: Response) => {
    if (!remoteSystemsSync) { res.status(503).json({ error: 'sync_not_wired' }); return; }
    try {
      const candidates = await remoteSystemsSync.discover();
      res.json({ candidates });
    } catch (err: any) {
      res.status(500).json({ error: err?.message ?? String(err) });
    }
  });

  // ─── Orbit OTA — Phase 1A version reporting ───────────────────────────
  // GET /iot/orbit/ota/version?slot=N
  //   Probes the LP at resolveOrbitHost(slot) on TCP :5503, waits for
  //   the auto-pushed FwBankInfo frame from `lp_ota_task`, returns
  //   { reachable: true, host, slot, bankInfo } on success or
  //   { reachable: false, host, slot, error } on failure.  Honest
  //   passthrough — no caching, no fabricated values.  See
  //   docs/LP-AM2434-OTA-Update-Plan.md for the larger plan; Phase 1B
  //   adds POST /iot/orbit/ota/push for the actual flash write path.
  router.get('/orbit/ota/version', async (req: Request, res: Response) => {
    const slotRaw = req.query.slot;
    const slot = Number(slotRaw);
    if (!Number.isFinite(slot) || slot < 0 || slot > 7) {
      res.status(400).json({ error: 'slot must be an integer 0..7' });
      return;
    }
    const host = resolveOrbitHost(slot);
    const out = await fetchBankInfo(host);
    if (out.reachable) {
      res.json({ slot, host, reachable: true, bankInfo: out.bankInfo });
    } else {
      res.status(503).json({ slot, host, reachable: false, error: out.error });
    }
  });

  // ─── Constellation Firmware Update (.cfu) — Phase 1B + 3 ─────────────
  //
  // Replacement for the legacy AS2 `.rpi` upgrade flow.  Two endpoints:
  //   POST /iot/firmware/upload    multipart upload of a `.cfu` bundle
  //   POST /iot/firmware/install   trigger install of a previously-uploaded staging file
  //   GET  /iot/firmware/status    snapshot of current install state (or null if idle)
  //
  // Progress is broadcast on the `firmware-progress` WS channel — a
  // full `InstallProgress` snapshot per tick.  See
  // `firmwareInstaller.ts` for the state machine and
  // `firmwareBundle.ts` for the bundle format.

  /** Staging directory for uploaded `.cfu` files.  Per-Pi5 production
   *  override via env var FIRMWARE_STAGING_DIR.  Created on demand. */
  const FIRMWARE_STAGING_DIR =
    process.env.FIRMWARE_STAGING_DIR
    ?? path.join(os.tmpdir(), 'constellation-firmware-staging');

  function ensureStagingDir(): void {
    try { fssync.mkdirSync(FIRMWARE_STAGING_DIR, { recursive: true }); } catch {}
  }

  /** POST /iot/firmware/upload — multipart-form upload of a `.cfu` bundle.
   *
   *  On success returns `{ stagingId, filename, sizeBytes }`.  The
   *  `stagingId` is opaque and must be passed to /iot/firmware/install
   *  to trigger the install.
   *
   *  Validates that the uploaded filename ends with `.cfu` — same
   *  filename-extension discipline AS2 used with `.rpi`.  The actual
   *  manifest validation happens at install time inside `loadBundle()`.
   */
  router.post('/firmware/upload', (req: Request, res: Response) => {
    ensureStagingDir();
    const stagingId = `${Date.now()}-${Math.random().toString(36).slice(2, 8)}`;
    let savedPath: string | null = null;
    let savedFilename = '';
    let savedSize = 0;
    let uploadError = '';

    try {
      const busboy = Busboy({
        headers: req.headers,
        limits: { fileSize: 200 * 1024 * 1024 },  // 200 MB cap — same as AS2 path
      });

      busboy.on('file', (_fieldname, file, info) => {
        const filename = info.filename || '';
        if (path.extname(filename).toLowerCase() !== CFU_EXTENSION) {
          uploadError = `File must have ${CFU_EXTENSION} extension; got "${filename}"`;
          (file as any).resume();
          return;
        }
        savedFilename = path.basename(filename);
        const stagedName = `${stagingId}_${savedFilename}`;
        savedPath = path.join(FIRMWARE_STAGING_DIR, stagedName);
        const ws = fssync.createWriteStream(savedPath);
        file.on('data', (chunk: Buffer) => { savedSize += chunk.length; });
        file.pipe(ws);
        ws.on('error', (err) => { uploadError = `Write failed: ${err.message}`; });
      });

      busboy.on('finish', () => {
        if (uploadError) {
          if (savedPath) { try { fssync.unlinkSync(savedPath); } catch {} }
          if (!res.headersSent) res.status(400).json({ error: uploadError });
          return;
        }
        if (!savedPath) {
          if (!res.headersSent) res.status(400).json({ error: 'No file provided' });
          return;
        }
        console.log(`[API] /firmware/upload OK stagingId=${stagingId} file=${savedFilename} bytes=${savedSize}`);
        if (!res.headersSent) {
          res.json({ stagingId, filename: savedFilename, sizeBytes: savedSize });
        }
      });

      busboy.on('error', (err) => {
        console.error('[API] /firmware/upload busboy err:', err);
        if (!res.headersSent) res.status(500).json({ error: 'Upload processing failed' });
      });

      req.pipe(busboy);
    } catch (err: any) {
      console.error('[API] /firmware/upload setup err:', err);
      res.status(500).json({ error: err?.message ?? 'Upload failed' });
    }
  });

  /** POST /iot/firmware/install — kick off the install of a previously
   *  uploaded staging file.
   *
   *  Body: `{ stagingId: string, skipReboot?: boolean, allowDowngrade?: boolean }`.
   *  Returns 202 immediately with the initial progress snapshot; the
   *  long-running install runs asynchronously and progress is
   *  broadcast via the `firmware-progress` WS channel.
   *
   *  Returns 409 if an install is already in progress.
   */
  router.post('/firmware/install', async (req: Request, res: Response) => {
    if (isInstalling()) {
      res.status(409).json({ error: 'Firmware install already in progress' });
      return;
    }

    const { stagingId, skipReboot, allowDowngrade } = req.body ?? {};
    if (!stagingId || typeof stagingId !== 'string') {
      res.status(400).json({ error: 'stagingId is required' });
      return;
    }

    // Resolve staging file (prefix match — the staged filename has
    // `<stagingId>_<originalName>` so we look up by directory listing).
    let stagedPath: string | null = null;
    try {
      const entries = await fsp.readdir(FIRMWARE_STAGING_DIR);
      const match = entries.find(n => n.startsWith(`${stagingId}_`));
      if (match) stagedPath = path.join(FIRMWARE_STAGING_DIR, match);
    } catch {}

    if (!stagedPath) {
      res.status(404).json({ error: `Staging file for ${stagingId} not found` });
      return;
    }

    // Kick off the install async, respond immediately.
    console.log(`[API] /firmware/install starting from ${stagedPath} skipReboot=${!!skipReboot} allowDowngrade=${!!allowDowngrade}`);
    installBundle(
      stagedPath,
      firmwareProgressBroadcast,
      { skipReboot: !!skipReboot, allowDowngrade: !!allowDowngrade },
    )
      .then(() => { console.log('[API] /firmware/install completed'); })
      .catch((err) => {
        if (err instanceof InstallError || err instanceof BundleError) {
          console.error(`[API] /firmware/install failed: [${err.code}] ${err.message}`);
        } else {
          console.error('[API] /firmware/install failed:', err);
        }
      })
      .finally(() => {
        // Best-effort: remove the staged .cfu after the install (success
        // or failure).  Keeping it would just clutter /tmp and the
        // extracted bundle in extractDir is already cleaned up inside
        // installBundle's finally block.
        if (stagedPath) {
          fsp.unlink(stagedPath).catch(() => {});
        }
      });

    res.status(202).json({
      ok: true,
      stagingId,
      message: 'Install started; subscribe to firmware-progress WS for updates',
      progress: getCurrentInstall(),
    });
  });

  /** GET /iot/firmware/status — snapshot of current install state, or
   *  `{ progress: null, isInstalling: false }` if idle. */
  router.get('/firmware/status', (_req: Request, res: Response) => {
    res.json({
      progress: getCurrentInstall(),
      isInstalling: isInstalling(),
    });
  });

  // (S9g: GET /ws/network-data and POST /network removed — network page
  // hydrates from $networkComposite reactively; refresh button is now a
  // pure UX flash since the composite already updates on every push.)
  // (S9k cleanup 2026-04-21: GET /ws/upgrade-data and /ws/download-data
  // removed — both were HTTP polling fallbacks for the retired
  // PollingClient.  The upgrade and download pages subscribe to the
  // 'upgrade-data' / 'download-data' WS push channels instead.)

  // â”€â”€â”€ Specific page endpoints (must come BEFORE the generic loop) â”€â”€â”€

  // (S9k cleanup: /burner GET removed â€” home & level2 burner pages now read $burnerSettings proto store directly.)

  /* (Apr 2026 proto-direct migration: removed `/iot/basic/home` GET. The
   * root home page now reads `basicSetup.homePage` from the typed
   * `basicSetup` proto store (TAG.BasicSetup, field 4) directly. */

  // (Apr 27 2026) /alert/labels and /alert/indices removed — the level1
  // alerts page derives both arrays client-side via
  // src/lib/business/alertMetadata.ts. See the helper-deletion comment
  // higher in this file for context.

  // (Apr 29 2026 mbtcp migration) /sync/ioconfig REMOVED. The legacy
  // route POSTed equipment labels to each orbit-simulator's REST
  // /api/ioconfig endpoint so the sim panel could show "Fan 1" instead
  // of "DO1". The orbit-simulator REST API is gone; LP firmware owns
  // labels via Settings.IoConfig (proto field 36) which the UI already
  // writes to firmware via /proto/write/36 immediately before this
  // call. UI side: the post-save fetch will 404 — a real work item to
  // remove from the IO Config page.

  // Analog RS485 sync â€” side-effect endpoint paired with /proto/write/42.
  // The Level 2 Analog Board page writes the AnalogBoard settings
  // (label/offset/disabled per sensor) direct to firmware, then POSTs the
  // same 25-element body here so the simulator's RS485 board cache
  // (boards/sensor-list/sensor-labels) stays in sync. Production firmware
  // owns the RS485 link directly â€” this endpoint exists only for the
  // sim path. Best-effort.
  router.post('/sync/analog', async (req: Request, res: Response) => {
    try {
      await persistAnalogSaveToRs485(req.body);
      await syncAnalogCacheFromRs485();
      res.json({ ok: true });
    } catch (err) {
      console.warn('[API] /sync/analog failed:', err);
      res.json({ ok: false, error: String(err) });
    }
  });

  // â”€â”€â”€ PID / sensor data endpoints â”€â”€â”€

  router.get('/pids', (_req: Request, res: Response) => {
    const pidData = dataCache.getByVarName('PidData');
    const sensorData = dataCache.getByVarName('SensorListData');
    res.json({
      PidData: pidData?.value ?? '',
      SensorListData: sensorData?.value ?? '',
    });
  });

  /* (Apr 2026 proto-direct migration: removed `/iot/sensors/all` GET.
   * The history/activitylog and history/userlog pages now read
   * `sensorList` from protoStores.ts, which derives from the
   * `analogBoards` aggregator (typed AnalogBoard frames). The
   * `buildSensorList()` helper accepts `SensorInfo[]` directly so no
   * CSV-parse step is needed.) */


  /* (Apr 2026 proto-direct migration: removed `/iot/sensors/unified` and
   * `/iot/analog/all` GET routes. The level2/analog page now subscribes
   * to `analogBoardArrays` (derived from the AnalogBoard proto stream) in
   * protoStores.ts and assembles the 25-element row layout client-side.
   * The `AnalogAllData` / `SensorListData` cache entries and
   * `rebuildAnalogAggregates()` are still populated for other legacy
   * routes — `/pids`, `/sensors/all`, `/iot/sensors/...` — until those
   * pages are migrated as well.) */


  // (Apr 2026 proto-direct migration: removed `/iot/aux/all` GET. The
  // auxiliary page now reads `auxiliaryComposite` in protoStores.ts,
  // which subscribes to AuxProgramBundle (tag 72) directly. The
  // companion helpers parseAuxProgram, buildDefaultAuxRules,
  // discoverAuxPrograms, and the dataCache.getAllAuxPrograms accumulator
  // were retired in the same change.)

  // ─── Display / config endpoints ───

  router.get('/displays', (_req: Request, res: Response) => {
    // Email page consumes `displays.data.DisplayList` (groups of 5: name, _, ip, _, _)
    // and `displays.data.LocalIpAdd[0]` as "ip:port". In dev there is no multi-panel
    // discovery, so return just this panel's own info so the form can populate.
    const ifaces = os.networkInterfaces();
    const ip = Object.values(ifaces)
      .flat()
      .find(i => i && i.family === 'IPv4' && !i.internal)?.address ?? '127.0.0.1';
    const port = process.env.BRIDGE_PUBLIC_PORT ?? '81';
    const name = dataCache.getByVarName('StorageName')?.value?.split(',')[0] ?? 'Constellation';
    res.json({
      data: {
        DisplayList: [name, '', ip, '', ''],
        LocalIpAdd: [`${ip}:${port}`],
      },
    });
  });

  // (S9k cleanup 2026-04-21: GET /port removed — `getPort()` in
  // src/lib/business/network.ts now reads NetworkConfig.httpPort from
  // the typed proto store directly.)

  // ─── Background pictures (custom login/home backgrounds) ───
  // Stored on disk under server/data/backgrounds/{id}_{safeName}. A small
  // index.json tracks displayName + original filename per id.
  const BG_DIR = path.resolve(
    typeof import.meta !== 'undefined' && import.meta.url
      ? path.dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Z]:)/, '$1'))
      : process.cwd(),
    '..', 'data', 'backgrounds',
  );
  const BG_INDEX = path.join(BG_DIR, 'index.json');
  interface BgEntry { id: string; filename: string; displayName: string; }
  function readBgIndex(): BgEntry[] {
    try {
      if (!fs.existsSync(BG_INDEX)) return [];
      return JSON.parse(fs.readFileSync(BG_INDEX, 'utf8')) as BgEntry[];
    } catch { return []; }
  }
  function writeBgIndex(list: BgEntry[]): void {
    fs.mkdirSync(BG_DIR, { recursive: true });
    fs.writeFileSync(BG_INDEX, JSON.stringify(list, null, 2));
  }

  router.get('/background-pictures', (_req: Request, res: Response) => {
    res.json(readBgIndex());
  });

  router.get('/background-pictures/file/:filename', (req: Request, res: Response) => {
    const safe = path.basename(req.params.filename);
    const full = path.join(BG_DIR, safe);
    if (!full.startsWith(BG_DIR) || !fs.existsSync(full)) {
      res.status(404).json({ error: 'not found' }); return;
    }
    res.sendFile(full);
  });

  router.post('/background-pictures', (req: Request, res: Response) => {
    try {
      fs.mkdirSync(BG_DIR, { recursive: true });
      const busboy = Busboy({ headers: req.headers, limits: { fileSize: 20 * 1024 * 1024 } });
      let displayName = '';
      let savedFilename = '';
      const id = `bg_${Date.now()}_${Math.random().toString(36).slice(2, 10)}`;
      busboy.on('field', (name: string, val: string) => {
        if (name === 'displayName') displayName = val;
      });
      busboy.on('file', (_fieldname: string, stream: NodeJS.ReadableStream, info: { filename: string }) => {
        const safeOrig = (info.filename || 'upload').replace(/[^a-zA-Z0-9.-]/g, '_');
        savedFilename = `${id}_${safeOrig}`;
        const dest = path.join(BG_DIR, savedFilename);
        stream.pipe(fs.createWriteStream(dest));
      });
      busboy.on('finish', () => {
        if (!savedFilename) { res.status(400).json({ error: 'no file' }); return; }
        const entry: BgEntry = { id, filename: savedFilename, displayName: displayName || savedFilename };
        const list = readBgIndex();
        list.push(entry);
        writeBgIndex(list);
        res.json(entry);
      });
      busboy.on('error', (err: Error) => res.status(500).json({ error: err.message }));
      req.pipe(busboy);
    } catch (err: any) {
      res.status(500).json({ error: err.message });
    }
  });

  router.put('/background-pictures/:id', (req: Request, res: Response) => {
    const list = readBgIndex();
    const idx = list.findIndex(e => e.id === req.params.id);
    if (idx < 0) { res.status(404).json({ error: 'not found' }); return; }
    const { displayName } = req.body ?? {};
    if (typeof displayName === 'string' && displayName.trim()) {
      list[idx].displayName = displayName.trim();
      writeBgIndex(list);
    }
    res.json(list[idx]);
  });

  router.delete('/background-pictures/:id', (req: Request, res: Response) => {
    const list = readBgIndex();
    const idx = list.findIndex(e => e.id === req.params.id);
    if (idx < 0) { res.status(404).json({ error: 'not found' }); return; }
    const [entry] = list.splice(idx, 1);
    try { fs.unlinkSync(path.join(BG_DIR, entry.filename)); } catch { /* ignore */ }
    writeBgIndex(list);
    res.json({ ok: true });
  });

  // â”€â”€â”€ IoT Client Configuration â”€â”€â”€
  // Stores access token for Django cloud sync.
  // When token is saved, registers the device with Django.

  interface IotConfig {
    AccessToken: string;
    Timeout: string;
    Protocol: string;
    Version: string;
  }

  router.get('/config', (_req: Request, res: Response) => {
    // Load saved IoT config or return defaults
    const saved = loadConfig<IotConfig>('iotConfig');
    res.json(saved ?? {
      AccessToken: '',
      Timeout: '30',
      Protocol: 'http',
      Version: '1.0.0-bridge',
    });
  });

  router.post('/config', async (req: Request, res: Response) => {
    const body = req.body as Partial<IotConfig>;
    console.log('[API] IoT config save:', JSON.stringify(body));

    // Load existing config
    const existing = loadConfig<IotConfig>('iotConfig') ?? {
      AccessToken: '',
      Timeout: '30',
      Protocol: 'http',
      Version: '1.0.0-bridge',
    };

    // Merge new values
    const newConfig: IotConfig = {
      AccessToken: body.AccessToken ?? existing.AccessToken,
      Timeout: body.Timeout ?? existing.Timeout,
      Protocol: body.Protocol ?? existing.Protocol,
      Version: existing.Version, // Version is read-only
    };

    // Save to disk
    saveConfig('iotConfig', newConfig);

    // If token changed and Django sync is enabled, update the sync module's token
    if (body.AccessToken && body.AccessToken !== existing.AccessToken) {
      console.log('[API] New access token configured, updating Django sync...');
      
      // Try to verify token with Django
      const djangoUrl = process.env.DJANGO_URL || 'http://localhost:8000';
      try {
        const verifyResp = await fetch(`${djangoUrl}/api/bridge/sync/`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({
            token: body.AccessToken,
            timestamp: new Date().toISOString(),
            payload: { MainData: ['0'] }, // Minimal payload to verify token
          }),
        });

        if (verifyResp.ok) {
          const result = await verifyResp.json();
          console.log(`[API] Token verified with Django: deviceId=${result.deviceId}`);
          
          // Update environment for djangoSync module
          process.env.DJANGO_TOKEN = body.AccessToken;
          process.env.DJANGO_SYNC_ENABLED = 'true';
        } else {
          console.warn(`[API] Token verification failed: ${verifyResp.status}`);
        }
      } catch (err: any) {
        console.warn('[API] Django not reachable for token verification:', err.message);
      }
    }

    res.json({ ok: true, config: newConfig });
  });

  // â”€â”€â”€ Log / History endpoints â”€â”€â”€
  // User log graph and table data.  Static paths first, then :sensorId param.
  // All log endpoints accept an optional `source` query param to filter by device
  // (e.g. ?source=nova-1, ?source=orbit-2). Defaults to "nova-1".

  // (S9k cleanup 2026-04-21: GET /log/sources removed — no UI consumer.
  // The history page derives its source list from the typed proto stream.)

  router.get('/user/dates', async (req: Request, res: Response) => {
    if (!logDataStore) { res.json([]); return; }
    const start = (req.query.start as string) || '';
    const end = (req.query.end as string) || '';
    const source = (req.query.source as string) || undefined;
    try {
      const dates = await logDataStore.getUserDates(start, end, source);
      res.json(dates);
    } catch (err: any) {
      console.error('[API] /user/dates error:', err.message);
      res.json([]);
    }
  });

  router.get('/user/download', async (req: Request, res: Response) => {
    if (!logDataStore) { res.status(503).type('text/csv').send(''); return; }
    const db = logDataStore.db;
    const start = String(req.query.start ?? '01/01/2000');
    const end = String(req.query.end ?? '12/31/2099');
    const fileName = String(req.query.fileName ?? 'userlog.csv').replace(/[^A-Za-z0-9_.-]/g, '_');
    try {
      // db.* methods are async (Postgres-backed); must await before
      // touching .map / for-of, or TS flags Promise iteration errors
      // and the runtime gets a Promise stringified into the CSV.
      const rows = await db.getUserLogByDateRange(start, end);
      // Pivot the sensor_log children: build columns for every distinct
      // (type, sensor_id) pair seen in this slice so spreadsheets get a
      // wide row per snapshot instead of having to JOIN across two files.
      const sensorRows = await db.getSensorLogByLogIds(rows.map(r => r.id));
      const sensorCols = new Map<string, Map<number, number>>(); // colKey → (logId → value)
      for (const sr of sensorRows) {
        const key = `${sr.sensor_type}_${sr.sensor_id}`;
        if (!sensorCols.has(key)) sensorCols.set(key, new Map());
        sensorCols.get(key)!.set(sr.log_id, sr.value);
      }
      const sensorKeys = Array.from(sensorCols.keys()).sort();
      const header = [
        'Date', 'Time', 'PlenumTempSp', 'PlenumTemp', 'CoolAvlTemp',
        'PlenumHumidSp', 'Mode', 'FanSpeed', 'CoolOutput', 'RefrigOutput',
        'BurnerOutput', 'CalcHumid', 'FanRuntimeMin', 'Co2Level',
        ...sensorKeys,
      ];
      const lines: string[] = [header.join(',')];
      for (const r of rows) {
        const cols: (string | number)[] = [
          csvCell(r.date), csvCell(r.time),
          r.plenum_temp_sp, r.plenum_temp, r.cool_avl_temp, r.plenum_humid_sp,
          r.mode, r.fan_speed, r.cool_output, r.refrig_output,
          r.burner_output, r.calc_humid, r.fan_runtime_min, r.co2_level,
        ];
        for (const k of sensorKeys) cols.push(sensorCols.get(k)!.get(r.id) ?? '');
        lines.push(cols.join(','));
      }
      res.type('text/csv')
         .header('Content-Disposition', `attachment; filename="${fileName}"`)
         .send(lines.join('\n') + '\n');
    } catch (err: any) {
      console.error('[API] /user/download error:', err.message);
      res.status(500).type('text/csv').send('');
    }
  });

  router.get('/user/:sensorId', async (req: Request, res: Response) => {
    if (!logDataStore) { res.json([]); return; }
    const { sensorId } = req.params;
    const start = (req.query.start as string) || '';
    const end = (req.query.end as string) || '';
    const source = (req.query.source as string) || undefined;
    try {
      const data = await logDataStore.getUserSensorData(sensorId, start, end, source);
      res.json(data);
    } catch (err: any) {
      console.error(`[API] /user/${sensorId} error:`, err.message);
      res.json([]);
    }
  });

  // Activity log data

  router.get('/activity/dates', async (req: Request, res: Response) => {
    if (!logDataStore) { res.json([]); return; }
    const rawStart = ((req.query.start as string) || '').trim();
    const rawEnd = ((req.query.end as string) || '').trim();
    const start = rawStart.replace(/[^0-9-]/g, '');
    const end = rawEnd.replace(/[^0-9-]/g, '');
    const source = (req.query.source as string) || undefined;
    try {
      const dates = await logDataStore.getActivityDates(start, end, source);
      res.json(dates);
    } catch (err: any) {
      console.error('[API] /activity/dates error:', err.message);
      res.json([]);
    }
  });

  router.get('/activity/download', async (req: Request, res: Response) => {
    if (!logDataStore) { res.status(503).type('text/csv').send(''); return; }
    const db = logDataStore.db;
    // Activity log uses RECORD-NUMBER range (newest=1) to match the
    // existing /activity/dates + /activity/:sensorId contract.  If a
    // caller passes a date range instead (MM/DD/YYYY), fall back to the
    // date-range query.
    const startRaw = String(req.query.start ?? '1');
    const endRaw = String(req.query.end ?? '1000');
    const fileName = String(req.query.fileName ?? 'activitylog.csv').replace(/[^A-Za-z0-9_.-]/g, '_');
    try {
      // db.* methods are async; awaiting before for-of avoids
      // TS2488 "Promise must have [Symbol.iterator]".
      const rows = await (startRaw.includes('/')
        ? db.getActivityLogByDateRange(startRaw, endRaw)
        : db.getActivityLogByRange(parseInt(startRaw, 10) || 1, parseInt(endRaw, 10) || 1000));
      const lines: string[] = [
        'Date,Time,EventType,EqIndex,Description,NewState,Mode',
      ];
      for (const r of rows) {
        lines.push([
          csvCell(r.date), csvCell(r.time),
          r.event_type, r.eq_index, csvCell(r.description),
          r.new_state, r.mode,
        ].join(','));
      }
      res.type('text/csv')
         .header('Content-Disposition', `attachment; filename="${fileName}"`)
         .send(lines.join('\n') + '\n');
    } catch (err: any) {
      console.error('[API] /activity/download error:', err.message);
      res.status(500).type('text/csv').send('');
    }
  });

  router.get('/activity/:sensorId', async (req: Request, res: Response) => {
    if (!logDataStore) { res.json([]); return; }
    const { sensorId } = req.params;
    const rawStart = ((req.query.start as string) || '').trim();
    const rawEnd = ((req.query.end as string) || '').trim();
    const start = rawStart.replace(/[^0-9-]/g, '');
    const end = rawEnd.replace(/[^0-9-]/g, '');
    const source = (req.query.source as string) || undefined;
    try {
      const data = await logDataStore.getActivitySensorData(sensorId, start, end, source);
      res.json(data);
    } catch (err: any) {
      console.error(`[API] /activity/${sensorId} error:`, err.message);
      res.json([]);
    }
  });

  // Graph favorites

  router.get('/graph/favorites', (_req: Request, res: Response) => {
    if (!logDataStore) { res.json({ favoriteSelections: '' }); return; }
    const favorites = logDataStore.getGraphFavorites();
    res.json({ favoriteSelections: favorites });
  });

  router.post('/graph/favorites', async (req: Request, res: Response) => {
    if (!logDataStore) { res.status(503).json({ error: 'Log data store not available' }); return; }
    const { GraphFavorites } = req.body ?? {};
    if (typeof GraphFavorites !== 'string') {
      res.status(400).json({ error: 'GraphFavorites must be a string' });
      return;
    }
    try {
      await logDataStore.saveGraphFavorites(GraphFavorites);
      res.json({ ok: true });
    } catch (err: any) {
      console.error('[API] /graph/favorites POST error:', err.message);
      res.status(500).json({ error: err.message });
    }
  });

  // Available remote equipment labels (for activity log column mapping)
  /* (Apr 2026 proto-direct migration: removed `/iot/avail/remote` GET.
   * The route was a stub returning hardcoded `['Remote']`. The history/
   * activitylog page now seeds the same default in `remoteListStore` at
   * store-creation time. Re-introduce a typed proto store if/when real
   * per-source remote-equipment lists become available.) */

  // PID log history — backed by SQLite pid_log table (replaces legacy SD card).
  // Distinct from GET /pids which returns live PidData/SensorListData.
  router.get('/pidlog', async (req: Request, res: Response) => {
    if (!logDataStore) { res.json({ records: [], count: 0 }); return; }
    const start = String(req.query.start ?? '01/01/2000');
    const end = String(req.query.end ?? '12/31/2099');
    const loopParam = req.query.loop;
    const loopIndex = loopParam !== undefined ? parseInt(String(loopParam), 10) : undefined;
    try {
      const records = await logDataStore.getPidLogRecords(start, end,
        Number.isFinite(loopIndex as number) ? loopIndex : undefined);
      res.json({
        records,
        count: records.length,
        total: await logDataStore.getPidLogCount(
          Number.isFinite(loopIndex as number) ? loopIndex : undefined),
      });
    } catch (err: any) {
      console.error('[API] /pidlog error:', err.message);
      res.status(500).json({ error: err.message });
    }
  });

  // LoadLog history — per-bay commodity loading temperatures (replaces SD card).
  // Records emitted ~every 5 min by firmware while a bay is LL_ACQUIRING.
  router.get('/loadlog', async (req: Request, res: Response) => {
    if (!logDataStore) { res.json({ records: [], count: 0 }); return; }
    const start = String(req.query.start ?? '01/01/2000');
    const end = String(req.query.end ?? '12/31/2099');
    try {
      const records = await logDataStore.getLoadLogRecords(start, end);
      res.json({
        records,
        count: records.length,
        total: await logDataStore.getLoadLogCount(),
      });
    } catch (err: any) {
      console.error('[API] /loadlog error:', err.message);
      res.status(500).json({ error: err.message });
    }
  });

  // CSV exports for PIDLog / LoadLog.  Distinct from /user/download +
  // /activity/download which are GellertFileSystem stubs — these stream
  // CSV directly from the bridge SQLite store since the new log paths
  // don't go through GFS at all.
  router.get('/pidlog/download', async (req: Request, res: Response) => {
    if (!logDataStore) { res.status(503).type('text/csv').send(''); return; }
    const start = String(req.query.start ?? '01/01/2000');
    const end = String(req.query.end ?? '12/31/2099');
    const loopParam = req.query.loop;
    const loopIndex = loopParam !== undefined ? parseInt(String(loopParam), 10) : undefined;
    try {
      const records = await logDataStore.getPidLogRecords(start, end,
        Number.isFinite(loopIndex as number) ? loopIndex : undefined);
      // CSV columns mirror the legacy AS2 PIDLog SD-card schema for
      // operator familiarity.  P/I/D/Error are floats; Output is signed
      // int32 (-100..100); Sequence is monotonic per loop_index.
      const lines: string[] = [
        'Date,Time,LoopIndex,P,I,D,Error,Output,Sequence',
      ];
      for (const r of records) {
        lines.push([
          csvCell(r.date), csvCell(r.time), r.loop_index,
          r.p_term, r.i_term, r.d_term, r.error, r.output, r.sequence,
        ].join(','));
      }
      res.type('text/csv')
         .header('Content-Disposition', 'attachment; filename="pidlog.csv"')
         .send(lines.join('\n') + '\n');
    } catch (err: any) {
      console.error('[API] /pidlog/download error:', err.message);
      res.status(500).type('text/csv').send('');
    }
  });

  router.get('/loadlog/download', async (req: Request, res: Response) => {
    if (!logDataStore) { res.status(503).type('text/csv').send(''); return; }
    const start = String(req.query.start ?? '01/01/2000');
    const end = String(req.query.end ?? '12/31/2099');
    try {
      const records = await logDataStore.getLoadLogRecords(start, end);
      // Flatten parent + bays into one row per bay so the file is
      // spreadsheet-friendly.  SensorTempF is sensor_x10/10 (legacy AS2
      // wire convention).  SENSOR_VAL_UNDEFINED (32767) is preserved
      // verbatim — operators expect the sentinel.
      const lines: string[] = [
        'Date,Time,RecordNum,Sequence,BayIndex,Pipe,SensorTempX10,Status',
      ];
      for (const r of records) {
        for (let i = 0; i < r.bays.length; i++) {
          const b = r.bays[i];
          lines.push([
            csvCell(r.date), csvCell(r.time),
            r.record_num, r.sequence,
            i, b.pipe, b.sensor_x10, b.status,
          ].join(','));
        }
      }
      res.type('text/csv')
         .header('Content-Disposition', 'attachment; filename="loadlog.csv"')
         .send(lines.join('\n') + '\n');
    } catch (err: any) {
      console.error('[API] /loadlog/download error:', err.message);
      res.status(500).type('text/csv').send('');
    }
  });

  router.get('/logs', (_req: Request, res: Response) => {
    res.type('text').send('No logs available');
  });

  // (May 2026 cleanup: GET /settings/files removed — the multi-display
  // file selector UI was replaced by direct browser file pickers. Use
  // GET /iot/settings/export for download and POST /iot/settings/import
  // for restore.)

  // (S9k cleanup 2026-04-21: GET /nodesetup removed — no UI consumer.)

  router.get('/upgrade', (_req: Request, res: Response) => {
    if (upgradeManager) {
      res.json(upgradeManager.getUpgradeInfo());
    } else {
      res.json({ ControllerList: [], DisplayList: [] });
    }
  });

  /** POST /iot/upgrade â€” Initiate an upgrade */
  router.post('/upgrade', async (req: Request, res: Response) => {
    if (!upgradeManager) {
      res.status(503).json({ error: 'Upgrade manager not available' });
      return;
    }
    if (upgradeManager.isUpgrading) {
      res.status(409).json({ error: 'Upgrade already in progress' });
      return;
    }

    const { selDisplay, selController, uploadedFile } = req.body ?? {};
    const upgradeFile = uploadedFile || selDisplay || '';
    const controllerIp = selController || '127.0.0.1';

    if (!upgradeFile) {
      res.status(400).json({ error: 'No upgrade file specified' });
      return;
    }

    console.log(`[API] Upgrade requested: file=${upgradeFile} controller=${controllerIp}`);

    try {
      // Start asynchronously â€” progress reported via WebSocket
      await upgradeManager.startUpgrade(upgradeFile, controllerIp);
      res.json({ ok: true, message: 'Upgrade started' });
    } catch (err: any) {
      console.error('[API] Upgrade start failed:', err.message);
      res.status(500).json({ error: err.message });
    }
  });

  router.get('/datainfo', async (_req: Request, res: Response) => {
    if (!logDataStore) {
      res.json({
        database: { activityCount: 0, historyCount: 0, percentUsed: 0, startDate: new Date().toISOString() },
        storage:  { mount: '', totalBytes: 0, usedBytes: 0, freeBytes: 0, percentUsed: 0 },
      });
      return;
    }
    res.json(await logDataStore.getDataInfo());
  });

  /* (Apr 2026 proto-direct migration: removed `/iot/avail/equipment`
   * GET — see `/iot/avail/remote` comment above. Equipment defaults
   * baked into `equipListStore`.) */

  // â”€â”€â”€ Alarm history â”€â”€â”€

  router.get('/alarms/:range', async (req: Request, res: Response) => {
    if (!logDataStore) { res.json([]); return; }
    const count = parseInt(req.params.range, 10);
    if (isNaN(count) || count <= 0) { res.json([]); return; }
    try {
      const alarms = await logDataStore.getAlarmHistory(count);
      res.json(alarms);
    } catch (err: any) {
      console.error('[API] /alarms error:', err.message);
      res.json([]);
    }
  });

  // ─── Page-save POST endpoints ────────────────────────────────────────
  // (S9k cleanup, Phase 5.1 finale: every page in `pageSaveMap` was
  // migrated to client-side `writeProto(...)` over /proto/write/<tag>.
  // The map ended up entirely empty, the iterator registered zero
  // routes, and the `arr` / `pos` body-decode helpers became dead. The
  // whole block has been removed. New page saves should write protobuf
  // directly from the Svelte page — see e.g. `level2/burner/+page.svelte
  // ::saveBurner` for the canonical pattern.)

  // ─── Other POST endpoints ────────────────────────────────────────────

  /** Button press â€” sends a virtual button command to the ARM */
  router.post('/button', async (req: Request, res: Response) => {
    const body = req.body;
    console.log('[API] Button press:', JSON.stringify(body));

    // Forward to ARM via serial bridge
    // The C code maps button presses to specific tags
    try {
      // ClearAlarm â€” Svelte sends { tag: 'button2', ClearAlarm: 'ClearAlarm' }
      // Replicate GellertServerD behavior: clear the server-side warning cache
      // immediately (LtxWarningsClear), then forward to the ARM firmware.
      if (body?.ClearAlarm === 'ClearAlarm') {
        console.log('[API] ClearAlarm â€” clearing cached warnings');
        dataCache.updateFromArm('AlarmData', '');
        dataCache.updateFromArm('WarningData', '');
        await serialBridge.sendPost('ClearAlarm=ClearAlarm');

        // Reset any faulted VFD drives â€” works for both RTU and simulated VFDs
        if (vfdClient) {
          const faultedDrives = vfdClient.getDrives().filter(d => d.faulted);
          for (const drv of faultedDrives) {
            console.log(`[API] ClearAlarm â€” resetting VFD fault on unit ${drv.unitId}`);
            try {
              await vfdClient.sendAction(drv.unitId, 'reset');
            } catch (err: any) {
              console.warn(`[API] VFD reset unit ${drv.unitId} failed: ${err.message}`);
            }
          }
        }
      } else if (body?.tag) {
        // (S9k cleanup 2026-04-21: removed `body.value !== undefined` and
        // `body.ButtonId !== undefined` branches \u2014 no UI POST sends those
        // shapes.  The legacy `Button=N` and `tag=value` paths are gone.)
        // Generic tag with extra fields (e.g. { tag: 'button2', SomeKey: 'val' })
        // Build URL-encoded body from non-tag fields
        const parts = Object.entries(body)
          .filter(([k]) => k !== 'tag')
          .map(([k, v]) => `${k}=${v}`);
        if (parts.length > 0) {
          await serialBridge.sendPost(parts.join('&'));
        }
      }
    } catch (err: any) {
      // Button press failed â€” log but don't block UI
      console.warn('[API] Button sendPost error:', err.message);
    }

    res.json({ ok: true });
  });

  // (S9k cleanup 2026-04-21: POST /PostSave.jsp removed — the legacy ARM2
  // bulk-save endpoint is fully replaced by per-tag proto writes via
  // /proto/write. NB: the original cleanup commit claimed "zero UI
  // consumers", but missed the iotclient page — it was POSTing here
  // and silently absorbing the 404 every save. Caller cleaned up
  // 2026-05-09 in level2/iotclient/+page.svelte::update().)

  /** TCP/IP settings save. Persists user intent to `tcpip.json` (consumed
   * by `protoStream.encodeNetworkConfig` for the read side) and pushes a
   * fresh NetworkConfig (envelope tag 19) to all proto-stream subscribers
   * so the page can update without a reload. The firmware does not yet
   * own this state in the simulator — see protoStream.ts for the
   * production transition note. */
  router.post('/tcpip', (req: Request, res: Response) => {
    const body = req.body ?? {};
    // Page sends shape: { HttpPort:[port,publicIp], LocalIpAdd:[ip],
    // LocalIpMask:[mask], LocalIpGateway:[gw], LocalIpMode:[mode],
    // LocalDns:[dns1?, dns2?] }.
    const port      = parseInt(body?.HttpPort?.[0] ?? '', 10);
    const publicIp  = String(body?.HttpPort?.[1] ?? '');
    const ipAddr    = String(body?.LocalIpAdd?.[0] ?? '');     // Bug fix 2026-06-02:
    const ipMask    = String(body?.LocalIpMask?.[0] ?? '');    //   previously LocalIpAdd
    const ipGateway = String(body?.LocalIpGateway?.[0] ?? ''); //   was ignored — IP
    const ipMode    = parseInt(body?.LocalIpMode?.[0] ?? '', 10); //   change was discarded
    const dnsArr    = Array.isArray(body?.LocalDns)            //   silently.
      ? (body.LocalDns as unknown[]).map(String).filter((s) => s && s !== '...')
      : [];

    // ── Server-side validation ────────────────────────────────────
    const validIpv4 = (s: string): boolean =>
      /^(\d{1,3}\.){3}\d{1,3}$/.test(s) &&
      s.split('.').every(o => { const n = Number(o); return n >= 0 && n <= 255; });
    const isStatic = ipMode === 0;
    if (isStatic) {
      if (!validIpv4(ipAddr)) {
        res.status(400).json({ ok: false, error: `invalid LocalIpAdd ${ipAddr}` });
        return;
      }
      if (!validIpv4(ipMask)) {
        res.status(400).json({ ok: false, error: `invalid LocalIpMask ${ipMask}` });
        return;
      }
      // Gateway may legitimately be empty / absent on isolated subnets.
      if (ipGateway && !validIpv4(ipGateway)) {
        res.status(400).json({ ok: false, error: `invalid LocalIpGateway ${ipGateway}` });
        return;
      }
      for (const d of dnsArr) {
        if (!validIpv4(d)) {
          res.status(400).json({ ok: false, error: `invalid DNS entry ${d}` });
          return;
        }
      }
    } else if (ipMode !== 1) {
      res.status(400).json({ ok: false, error: `invalid LocalIpMode ${body?.LocalIpMode?.[0]}` });
      return;
    }

    saveConfig('tcpip', {
      httpPort:  Number.isFinite(port) ? port : undefined,
      publicIp:  publicIp || undefined,
      ipAddr:    ipAddr    || undefined,
      ipMask:    ipMask    || undefined,
      ipGateway: ipGateway || undefined,
      ipMode:    Number.isFinite(ipMode) ? ipMode : undefined,
      dns:       dnsArr,
    });

    onTcpipSavedHook?.();
    console.log(`[API] TCP/IP settings persisted (mode=${ipMode}, ip=${ipAddr}, port=${port}, dns=${dnsArr.length})`);

    // ── Loopback short-circuit ────────────────────────────────────
    // Restored from the legacy iotclient handler (2026-06-02). When the
    // operator sets the static IP to 127.x or "localhost", they're
    // putting the panel into kiosk-only mode: panel browser running on
    // the Pi5 itself, talking to the bridge over loopback, NO LAN. This
    // is the escape hatch for shipping the panel to a site without
    // internet — without it, the only way out of a working LAN config
    // is through Level 2 (chicken/egg if you can't reach the page).
    //
    // Persist the intent so the page reflects it, then return — do NOT
    // call nmcli (127.x on eth0 would brick the box: NM would tear down
    // the current LAN config, leave eth0 with a loopback address,
    // routing would be broken, and there'd be no way back without a
    // serial console).
    const isLoopbackTarget = isStatic && (
      ipAddr === 'localhost' || ipAddr.startsWith('127.')
    );
    if (isLoopbackTarget) {
      console.log(`[API] /iot/tcpip loopback mode requested (ip=${ipAddr}) — persisted, skipping nmcli + reboot`);
      res.json({ ok: true, loopback: true });
      return;
    }

    // ── Apply to the OS via the privileged helper ─────────────────
    // Bug fix 2026-06-02: previously the handler returned ok:true here
    // after persisting tcpip.json but never touched the OS network
    // stack — eth0 stayed on its boot-time DHCP IP and the page's
    // "redirect to new IP after 8 s" UX hit a Pi5 that hadn't moved.
    // Now we invoke /usr/local/sbin/apply-network-config (installed
    // via deploy/apply-network-config + agristar-network-sudoers) to
    // nmcli-modify the connection and reboot. Helper validates every
    // arg before invoking nmcli; sudoers grants the bridge user only
    // this one script.
    //
    // dotted-quad netmask → CIDR prefix (e.g. 255.255.255.0 → 24).
    const maskToCidr = (m: string): number => {
      const octs = m.split('.').map(Number);
      if (octs.length !== 4 || octs.some(o => !Number.isFinite(o) || o < 0 || o > 255)) return 24;
      let bits = 0;
      for (const o of octs) {
        for (let i = 7; i >= 0; i--) {
          if ((o >> i) & 1) bits++;
          else return bits;
        }
      }
      return bits;
    };
    const args = isStatic
      ? ['static', ipAddr, String(maskToCidr(ipMask)), ipGateway || '', dnsArr.join(',')]
      : ['dhcp'];
    // Respond BEFORE invoking the helper — once nmcli reconfigures eth0
    // the TCP connection serving this response will drop. The page's
    // /redirect path already accounts for the bridge appearing to time
    // out mid-response.
    res.json({ ok: true });
    setImmediate(() => {
      const cmd = `sudo /usr/local/sbin/apply-network-config ${args.map(a => `'${a.replace(/'/g, "'\\''")}'`).join(' ')}`;
      console.log(`[API] /iot/tcpip applying via helper: ${cmd}`);
      void import('child_process').then(({ exec }) => {
        exec(cmd, (err, stdout, stderr) => {
          if (err) console.error(`[API] apply-network-config failed:`, err.message, stderr);
          else    console.log(`[API] apply-network-config: ${stdout.trim()}`);
        });
      });
    });
  });

  // (S9g: POST /network removed — page no longer fires it; composite is
  // reactive so no manual refresh round-trip is needed.)

  /** UI locale persistence. Pure UI state — bridge has no business
   * decoding it; we just persist so it survives restarts and is
   * available to any future server-rendered surface. */
  router.post('/locale', (req: Request, res: Response) => {
    const loc = String(req.body?.locale ?? '').trim();
    if (!loc) {
      res.status(400).json({ ok: false, error: 'locale required' });
      return;
    }
    saveConfig('locale', loc);
    res.json({ ok: true, locale: loc });
  });
  router.get('/locale', (_req: Request, res: Response) => {
    res.json({ locale: loadConfig<string>('locale') ?? 'en' });
  });

  /** Node configuration */
  /* (May 2026 cleanup: removed POST /iot/node and GET /iot/node/:ip.
     Both were accept-and-discard stubs (POST logged the body and
     returned ok; GET returned `{ip, status:'unknown'}`). No UI page
     consumes them. The level2/master "Find Nodes" button currently
     fires `tag:'findNodes'` at /iot/button which collapses to a no-op
     in commandDispatcher.sendPost — multi-panel master/slave
     discovery needs a real CMD_FIND_BOARD wire, see
     /memories/repo/iot-button-audit.md. */

  /* IO rename: removed POST /iot/ioconfig/:idx/:name.
     The level2/ioconfig page now writes IoNameUpdate directly via
     /proto/write/40 (TAG.IoNameUpdate). Nova firmware
     LpSettings_ApplyIoName validates the slot's renamable flag,
     persists the rename to OSPI, and re-broadcasts IoDefinition
     (envelope tag 25). The legacy `serialBridge.sendPost('ioRename=…')`
     ASCII RTS/ACK call was a dead drop on LP-AM2434 — see
     /memories/repo/legacy-migration-plan.md. */

  /* S9k cont 10: removed POST /iot/setfanspeed.
     level1/fanspeed `Set` button now writes FanSpeedSettings.maxSpeed
     directly via /proto/write/41. The serialBridge.sendCommand stub it
     called was a no-op since S9k cont 9. */

  /* ─── Settings export / import (level2/settings page) ──────────────────
   *
   * Round-tripped pairs of (envelope tag, SettingsUpdate field). Read
   * side: the bridge already has the latest raw nanopb payload per
   * envelope tag in NovaDataStore (the same cache /proto/stream replays
   * on subscribe). Write side: each SettingsUpdate field maps to a
   * firmware decoder (LpSettings_Apply* in lp_settings.c). The two
   * field numbers are NOT the same: envelope tags 40–67 carry
   * settings broadcast frames, while the SettingsUpdate oneof uses 1–40
   * (mirrors `SETTINGS_FIELD` in src/lib/business/protoWrite.ts).
   *
   * Single source of truth in the UI is `SETTINGS_FIELD` — keep the two
   * lists in sync when adding new settings. */
  const SETTINGS_EXPORT_TABLE: { name: string; envelopeTag: number; settingsField: number }[] = [
    { name: 'PlenumSettings',      envelopeTag: 40, settingsField: 1  },
    { name: 'FanSpeedSettings',    envelopeTag: 41, settingsField: 2  },
    { name: 'FanBoostSettings',    envelopeTag: 42, settingsField: 3  },
    { name: 'RampRateSettings',    envelopeTag: 43, settingsField: 4  },
    { name: 'RefrigSettings',      envelopeTag: 44, settingsField: 5  },
    { name: 'BurnerSettings',      envelopeTag: 45, settingsField: 6  },
    { name: 'Co2Settings',         envelopeTag: 46, settingsField: 7  },
    { name: 'CureSettings',        envelopeTag: 47, settingsField: 8  },
    { name: 'ClimacellSettings',   envelopeTag: 48, settingsField: 9  },
    { name: 'ClimacellTimes',      envelopeTag: 49, settingsField: 10 },
    { name: 'HumidCtrlSettings',   envelopeTag: 50, settingsField: 11 },
    { name: 'OutsideAirSettings',  envelopeTag: 51, settingsField: 12 },
    { name: 'MiscSettings',        envelopeTag: 52, settingsField: 13 },
    { name: 'FailureSettings',     envelopeTag: 53, settingsField: 14 },
    { name: 'FailureSettings2',    envelopeTag: 54, settingsField: 15 },
    { name: 'TempAlarmSettings',   envelopeTag: 55, settingsField: 16 },
    { name: 'DoorSettings',        envelopeTag: 56, settingsField: 19 },
    { name: 'LoadMonitorSettings', envelopeTag: 57, settingsField: 20 },
    { name: 'UserLogSettings',     envelopeTag: 59, settingsField: 22 },
    { name: 'EmailSettings',       envelopeTag: 62, settingsField: 27 },
    { name: 'AlertSettings',       envelopeTag: 63, settingsField: 28 },
    { name: 'PwmChannelSettings',  envelopeTag: 64, settingsField: 30 },
    { name: 'MasterSlaveSettings', envelopeTag: 66, settingsField: 25 },
    { name: 'PidLogSettings',      envelopeTag: 67, settingsField: 37 },
    { name: 'AnalogBoard',         envelopeTag: 26, settingsField: 29 },
    { name: 'IoConfig',            envelopeTag: 24, settingsField: 39 },
    { name: 'BasicSetup',          envelopeTag: 20, settingsField: 32 },
    { name: 'AccountSettings',     envelopeTag: 29, settingsField: 38 },
    { name: 'ServiceInfo',         envelopeTag: 23, settingsField: 31 },
    /* DateTime / NetworkConfig intentionally NOT round-tripped:
     *   - DateTime: importing a stale wall clock is a footgun.
     *   - NetworkConfig: importing IP/mask/gw on a different panel
     *     would knock the source panel off the LAN.
     * Runtimes / AuxProgramSettings / IoNameUpdate are also excluded:
     *   - Runtimes: hour counters are panel-history, not config.
     *   - AuxProgramSettings: write-side is per-channel; export comes
     *     from AuxProgramBundle (envelope tag 72) which doesn't
     *     decode 1:1 into a single SettingsUpdate field.
     *   - IoNameUpdate: write-only sentinel, no broadcast frame. */
  ];

  /** GET /iot/settings/export — returns the current settings as a
   *  downloadable JSON blob the UI restore endpoint can replay. */
  router.get('/settings/export', (_req: Request, res: Response) => {
    if (!novaStore) {
      res.status(503).json({ ok: false, error: 'nova_store_not_ready' });
      return;
    }
    const fields: { name: string; settingsField: number; envelopeTag: number; b64: string }[] = [];
    const missing: string[] = [];
    for (const e of SETTINGS_EXPORT_TABLE) {
      const buf = novaStore.getRawMessage(e.envelopeTag);
      if (!buf || buf.length === 0) {
        missing.push(e.name);
        continue;
      }
      fields.push({
        name: e.name,
        settingsField: e.settingsField,
        envelopeTag: e.envelopeTag,
        b64: buf.toString('base64'),
      });
    }
    const blob = {
      version:   1,
      generated: new Date().toISOString(),
      panel:     dataCache.getByVarName('StorageName')?.value?.split(',')[0] ?? 'Constellation',
      fields,
      missing,
    };
    console.log(`[API] Settings export: ${fields.length}/${SETTINGS_EXPORT_TABLE.length} groups (${missing.length} not yet broadcast)`);
    res.json(blob);
  });

  /** POST /iot/settings/import — replays a previously-exported blob.
   *  Body must match the GET /export shape. */
  router.post('/settings/import', async (req: Request, res: Response) => {
    if (!serialBridge.importSettings) {
      res.status(503).json({ ok: false, error: 'import_not_supported' });
      return;
    }
    const body = req.body as { version?: number; fields?: { settingsField: number; b64: string }[] } | undefined;
    if (!body || !Array.isArray(body.fields) || body.fields.length === 0) {
      res.status(400).json({ ok: false, error: 'no_fields_in_body' });
      return;
    }
    if (body.version !== 1) {
      res.status(400).json({ ok: false, error: `unsupported_version: ${body.version}` });
      return;
    }
    /* Whitelist: only allow settingsField values that appear in our
     * round-trip table. Prevents a tampered file from poking a field
     * we explicitly excluded (DateTime, NetworkConfig, ...). */
    const allowed = new Set(SETTINGS_EXPORT_TABLE.map((e) => e.settingsField));
    const accepted = body.fields.filter((f) => allowed.has(f.settingsField | 0));
    const rejected = body.fields.length - accepted.length;
    console.log(`[API] Settings import: ${accepted.length} groups accepted, ${rejected} rejected (not on round-trip whitelist)`);
    try {
      const result = await serialBridge.importSettings(accepted);
      res.json({ ok: true, applied: result.ok, total: result.total, failed: result.failed, rejected });
    } catch (err: any) {
      console.error('[API] Settings import error:', err.message);
      res.status(503).json({ ok: false, error: err.message });
    }
  });

  /**
   * Email test — actually sends a test email using the stored EmailSettings
   * (server, port, username, password, fromAddr, toAddr, authType).
   *
   * authType maps from the UI Select:
   *   0 = StartTLS  → secure:false, opportunistic STARTTLS upgrade (port 587 typical)
   *   1 = TLS-SSL   → secure:true,  implicit TLS                   (port 465 typical)
   *   2 = None      → secure:false, no STARTTLS                    (port 25 typical)
   * The UI lets the operator pick port + authType independently;
   * we honor whatever they saved.
   */
  router.post('/email/test', async (_req: Request, res: Response) => {
    if (!novaStore?.emailSettings) {
      res.status(503).json({ ok: false, error: 'Email settings not loaded yet' });
      return;
    }
    const e = novaStore.emailSettings;
    if (e.enabled !== 0) {
      // enabled: 0 = enable (legacy inverted bool), 1 = disable
      res.status(400).json({ ok: false, error: 'Email alerts are disabled' });
      return;
    }
    if (!e.server || !e.port || !e.fromAddr || !e.toAddr) {
      res.status(400).json({ ok: false, error: 'Email settings incomplete (need server, port, from, to)' });
      return;
    }
    const secure = e.authType === 1; // TLS-SSL
    const requireTLS = e.authType === 0; // StartTLS
    const auth = e.username ? { user: e.username, pass: e.password } : undefined;
    const transport = nodemailer.createTransport({
      host: e.server,
      port: e.port,
      secure,
      requireTLS,
      auth,
      // Modest timeouts so the UI button doesn't hang forever on a typo.
      connectionTimeout: 15000,
      greetingTimeout: 10000,
      socketTimeout: 20000,
    });
    try {
      const info = await transport.sendMail({
        from: e.fromAddr,
        to: e.toAddr,
        subject: 'Constellation test email',
        text: 'This is a test email from the Constellation panel. If you received this, alerts are configured correctly.',
      });
      console.log(`[API] Email test sent: messageId=${info.messageId} to=${e.toAddr}`);
      res.json({ ok: true, messageId: info.messageId });
    } catch (err: any) {
      console.error('[API] Email test error:', err.message);
      res.status(502).json({ ok: false, error: err.message });
    } finally {
      transport.close();
    }
  });

  /** Upgrade upload â€” handle multipart/form-data .rpi file upload */
  router.post('/upgrade/upload', (req: Request, res: Response) => {
    if (!upgradeManager) {
      res.status(503).json({ error: 'Upgrade manager not available' });
      return;
    }

    console.log('[API] Upgrade upload started');

    try {
      const busboy = Busboy({ headers: req.headers, limits: { fileSize: 200 * 1024 * 1024 } });
      let fileProcessed = false;
      let uploadError = '';

      busboy.on('file', (fieldname: string, file: NodeJS.ReadableStream, info: { filename: string; encoding: string; mimeType: string }) => {
        const filename = info.filename;
        console.log(`[API] Upload file: field=${fieldname} name=${filename}`);

        if (!filename.match(/^AS2.*\.rpi$/i)) {
          uploadError = 'Invalid file format. File must be AS2_*.rpi';
          (file as any).resume(); // drain the stream
          return;
        }

        fileProcessed = true;
        upgradeManager!.handleUpload(filename, file)
          .then((savedName) => {
            console.log(`[API] Upload complete: ${savedName}`);
            if (!res.headersSent) res.json({ ok: true, filename: savedName });
          })
          .catch((err) => {
            console.error('[API] Upload handler error:', err.message);
            if (!res.headersSent) res.status(500).json({ error: err.message });
          });
      });

      busboy.on('finish', () => {
        if (uploadError) {
          if (!res.headersSent) res.status(400).json({ error: uploadError });
          return;
        }
        if (!fileProcessed) {
          if (!res.headersSent) res.status(400).json({ error: 'No file provided' });
        }
      });

      busboy.on('error', (err: Error) => {
        console.error('[API] Busboy error:', err.message);
        if (!res.headersSent) res.status(500).json({ error: 'Upload processing failed' });
      });

      req.pipe(busboy);
    } catch (err: any) {
      console.error('[API] Upload setup error:', err.message);
      res.status(500).json({ error: 'Upload failed' });
    }
  });

  /** Locale preference */
  router.post('/locale', (req: Request, res: Response) => {
    console.log('[API] Locale set:', JSON.stringify(req.body));
    res.json({ ok: true });
  });

  // â”€â”€â”€ Auth endpoints (ECDH key exchange + AES-encrypted password) â”€â”€â”€

  /**
   * POST /iot/dhkey â€” Diffie-Hellman key exchange.
   * Client sends its P-521 public key (hex).
   * Server generates a keypair, computes the shared secret, and returns
   * its public key so the client can derive the same shared secret.
   */
  router.post('/dhkey', (req: Request, res: Response) => {
    try {
      const clientPublicHex: string = req.body?.publicKey;
      if (!clientPublicHex) {
        res.status(400).json({ error: 'Missing publicKey' });
        return;
      }

      serverECDH = crypto.createECDH('secp521r1');
      serverECDH.generateKeys();

      // Compute shared secret from the client's public key
      sharedSecret = serverECDH.computeSecret(Buffer.from(clientPublicHex, 'hex')).toString('hex');

      const serverPublicHex = serverECDH.getPublicKey('hex');
      console.log(`[Auth] DH key exchange OK â€” secret ${sharedSecret.length} hex chars`);
      res.json({ key: serverPublicHex });
    } catch (err: any) {
      console.error('[Auth] DH key exchange failed:', err.message);
      res.status(500).json({ error: 'Key exchange failed' });
    }
  });

  /** GET /iot/dhkey â€” legacy path (some code does GET instead of POST) */
  router.get('/dhkey', (_req: Request, res: Response) => {
    res.json({ key: serverECDH ? serverECDH.getPublicKey('hex') : '' });
  });

  /**
   * GET /iot/checkkey â€” check if a DH session is established.
   * Returns { data: 'true' } when a shared secret exists, else { data: 'false' }.
   */
  router.get('/checkkey', (_req: Request, res: Response) => {
    res.json({ data: sharedSecret ? 'true' : 'false' });
  });

  /**
   * POST /iot/dlr â€” Device Login Request.
   * The UI AES-encrypts the credentials with the ECDH shared secret.
   *   Body: { dlr: encrypted_type, dlr1: encrypted_password, dlr2: nonce, dlr3: nonce }
   * Returns: { data: '<level_number>' }
   */
  router.post('/dlr', (req: Request, res: Response) => {
    try {
      if (!sharedSecret) {
        console.warn('[Auth] /dlr called without DH session â€” returning -2');
        res.json({ data: '-2' });
        return;
      }

      const { dlr, dlr1 } = req.body ?? {};
      if (!dlr && !dlr1) {
        res.json({ data: '-2' });
        return;
      }

      // Decrypt the type (username) and password fields
      let typeField = '';
      let passwordField = '';
      try { typeField = decryptCryptoJS(dlr, sharedSecret); } catch { /* empty string OK */ }
      try { passwordField = decryptCryptoJS(dlr1, sharedSecret); } catch { /* empty string OK */ }

      console.log(`[Auth] Login: type="${typeField}" password="${passwordField ? '***' : '(empty)'}"`);

      const level = authenticateLocal(typeField, passwordField);
      currentLevel = Math.max(level, 0);

      // Resolve the human-readable actor for audit. Use the current
      // UserAccounts list from cache to map username → slot index.
      const users = (dataCache.getByVarName('UserAccounts')?.value ?? '').split(',');
      const resolved = resolveActor(typeField, level, users);
      currentActor = resolved.actor;
      currentSlot = resolved.slot;

      if (level > 0) {
        recordLogin(currentActor, currentSlot, level, req.ip);
      } else if (passwordField !== 'clear' && passwordField !== 'leveldown' && level < 0) {
        recordLoginFailure(typeField || 'unknown', req.ip);
      }

      console.log(`[Auth] → accessLevel = ${level} (actor=${currentActor}, slot=${currentSlot})`);

      res.json({ data: String(level) });
    } catch (err: any) {
      console.error('[Auth] /dlr error:', err.message);
      res.json({ data: '-2' });
    }
  });

  /**
   * POST /iot/logout â€” clear the auth session.
   */
  router.post('/logout', (req: Request, res: Response) => {
    recordLogout(currentActor, currentSlot, req.ip);
    currentLevel = 0;
    currentActor = 'anonymous';
    currentSlot = null;
    sharedSecret = null;
    serverECDH = null;
    console.log('[Auth] Session cleared (logout)');
    res.json({ ok: true });
  });

  // ─── Account metadata + audit log ─────────────────────────────────────
  /**
   * GET /iot/accounts-meta — returns per-slot role, last-login, cloud link
   * stubs, and factory last-login. The passwords themselves still come
   * from the ARM via /iot/accounts (encrypted channel) and are NEVER
   * stored in this metadata file.
   */
  router.get('/accounts-meta', (_req: Request, res: Response) => {
    try {
      const meta = loadAccountMeta();
      const users = (dataCache.getByVarName('UserAccounts')?.value ?? '').split(',');
      // Pair username + metadata for the UI (username is the ARM-owned field)
      const slots = meta.slots.map((s, i) => ({
        index: i,
        username: users[i] ?? '',
        role: s.role,
        lastLogin: s.lastLogin,
        lastLoginIp: s.lastLoginIp,
        loginCount: s.loginCount,
      }));
      res.json({
        factory: {
          lastLogin: meta.factoryLastLogin,
          loginCount: meta.factoryLoginCount,
        },
        slots,
        cloudLinks: meta.cloudLinks,
        currentSession: { actor: currentActor, slot: currentSlot, level: currentLevel },
      });
    } catch (err: any) {
      res.status(500).json({ error: err.message });
    }
  });

  /**
   * POST /iot/accounts-meta — update a single slot's role.
   *   Body: { slot: number, role: 'disabled'|'operator'|'admin' }
   * Requires Level 2 (admin). Usernames/passwords are edited via /iot/accounts
   * and remain the source of truth for the ARM firmware.
   */
  router.post('/accounts-meta', (req: Request, res: Response) => {
    try {
      if (currentLevel < 2) { res.status(403).json({ error: 'admin required' }); return; }
      const { slot, role } = req.body ?? {};
      if (typeof slot !== 'number' || slot < 0 || slot >= 10) {
        res.status(400).json({ error: 'bad slot' }); return;
      }
      const allowed: SlotRole[] = ['disabled', 'operator', 'admin'];
      if (!allowed.includes(role)) { res.status(400).json({ error: 'bad role' }); return; }
      const meta = loadAccountMeta();
      const before = meta.slots[slot].role;
      meta.slots[slot].role = role;
      saveAccountMeta(meta);
      recordSave(currentActor, currentSlot, currentLevel, 'accounts-meta',
        `slot ${slot}: ${before} → ${role}`, req.ip);
      res.json({ ok: true });
    } catch (err: any) {
      res.status(500).json({ error: err.message });
    }
  });

  /**
   * GET /iot/audit?limit=100 — most recent audit entries, newest first.
   * Requires Level 2. Never contains passwords; save payloads are
   * truncated to 120 chars in `detail`.
   */
  router.get('/audit', (req: Request, res: Response) => {
    try {
      if (currentLevel < 2) { res.status(403).json({ error: 'admin required' }); return; }
      const limit = parseInt(String(req.query.limit ?? '100'), 10) || 100;
      res.json({ entries: readAudit(limit) });
    } catch (err: any) {
      res.status(500).json({ error: err.message });
    }
  });

  // ─── Cloud identity link (pass 2) ─────────────────────────────────────
  // These endpoints forward to the Django `api/bridge/device-link/*` views.
  // The bridge identifies itself with its DJANGO_TOKEN (the IoTClient
  // provisioning token); cloud users never see that token. Level 2 is
  // required on the bridge side for create/unlink so only admins can
  // attach or remove cloud identities.

  const djangoBase = (): string => process.env.DJANGO_URL || 'http://localhost:8000';
  const deviceToken = (): string | null => process.env.DJANGO_TOKEN || null;

  /**
   * GET /iot/cloud/links — local cached view of CloudLinks (no Django call).
   * Anyone with an active UI session can see the list; linkTokens are
   * stripped so they can't be replayed from a screenshot.
   */
  router.get('/cloud/links', (_req: Request, res: Response) => {
    try {
      const meta = loadAccountMeta();
      const safe = meta.cloudLinks.map(l => ({
        cloudUserId:     l.cloudUserId,
        username:        l.username,
        displayName:     l.displayName,
        role:            l.role,
        slot:            l.slot,
        linkedAt:        l.linkedAt,
        lastRemoteLogin: l.lastRemoteLogin,
      }));
      res.json({ links: safe });
    } catch (err: any) {
      res.status(500).json({ error: err.message });
    }
  });

  /**
   * POST /iot/cloud/link — create/refresh a DeviceLink in Django.
   *   Body: { username, password, slot?, role? }
   * Admin-only. Sends device token + user creds to Django, receives a
   * CloudLink record (including opaque linkToken), caches it locally,
   * and audits the action.
   */
  router.post('/cloud/link', async (req: Request, res: Response) => {
    try {
      if (currentLevel < 2) { res.status(403).json({ error: 'admin required' }); return; }
      const tok = deviceToken();
      if (!tok) { res.status(501).json({ error: 'DJANGO_TOKEN not configured' }); return; }

      const { username, password, slot, role } = req.body ?? {};
      if (!username || !password) {
        res.status(400).json({ error: 'username and password required' }); return;
      }
      const slotNum = typeof slot === 'number' && slot >= 0 && slot <= 9 ? slot : null;
      const roleStr: SlotRole = role === 'operator' ? 'operator' : 'admin';

      const resp = await fetch(`${djangoBase()}/api/bridge/device-link/create/`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          deviceToken: tok, username, password,
          localSlot: slotNum, role: roleStr,
        }),
      });
      const text = await resp.text();
      if (!resp.ok) {
        let msg = text;
        try { msg = JSON.parse(text).error ?? text; } catch { /* ignore */ }
        res.status(resp.status).json({ error: msg });
        return;
      }
      const j = JSON.parse(text);
      const link: CloudLink = {
        cloudUserId:     j.cloudUserId,
        username:        j.username,
        displayName:     j.displayName || j.username,
        role:            j.role === 'operator' ? 'operator' : 'admin',
        linkToken:       j.linkToken,
        linkedAt:        j.linkedAt,
        slot:            typeof j.localSlot === 'number' ? j.localSlot : null,
        lastRemoteLogin: j.lastRemoteLogin ?? null,
      };
      upsertCloudLink(link);
      recordSave(currentActor, currentSlot, currentLevel, 'cloud/link',
        `linked ${link.username}${link.slot !== null ? ` → slot ${link.slot + 1}` : ''}`, req.ip);

      // Echo the safe subset (no linkToken).
      res.json({
        ok: true,
        link: {
          cloudUserId: link.cloudUserId, username: link.username,
          displayName: link.displayName, role: link.role, slot: link.slot,
          linkedAt: link.linkedAt, lastRemoteLogin: link.lastRemoteLogin,
        },
      });
    } catch (err: any) {
      console.error('[Cloud] /cloud/link error:', err.message);
      res.status(500).json({ error: err.message });
    }
  });

  /**
   * POST /iot/cloud/unlink — revoke a DeviceLink in Django and drop locally.
   *   Body: { cloudUserId }
   */
  router.post('/cloud/unlink', async (req: Request, res: Response) => {
    try {
      if (currentLevel < 2) { res.status(403).json({ error: 'admin required' }); return; }
      const tok = deviceToken();
      if (!tok) { res.status(501).json({ error: 'DJANGO_TOKEN not configured' }); return; }

      const cloudUserId = req.body?.cloudUserId;
      if (!cloudUserId) { res.status(400).json({ error: 'cloudUserId required' }); return; }

      const existing = findCloudLinkByUserId(cloudUserId);
      const resp = await fetch(`${djangoBase()}/api/bridge/device-link/delete/`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ token: tok, cloudUserId }),
      });
      // 404 from Django is fine — we still drop our local cache.
      if (!resp.ok && resp.status !== 404) {
        const txt = await resp.text();
        res.status(resp.status).json({ error: txt });
        return;
      }
      removeCloudLink(cloudUserId);
      recordSave(currentActor, currentSlot, currentLevel, 'cloud/unlink',
        `unlinked ${existing?.username ?? cloudUserId}`, req.ip);
      res.json({ ok: true });
    } catch (err: any) {
      console.error('[Cloud] /cloud/unlink error:', err.message);
      res.status(500).json({ error: err.message });
    }
  });

  /**
   * POST /iot/cloud/remote-login — sign in with a stored linkToken.
   *   Body: { linkToken }
   * On success: sets session actor to cloud:<username>, level based on
   * the link's role (operator → 1, admin → 2). Falls back to the local
   * cache only when Django is unreachable; a revoked/invalid token
   * always fails.
   */
  router.post('/cloud/remote-login', async (req: Request, res: Response) => {
    try {
      const tok = deviceToken();
      const linkToken = req.body?.linkToken;
      if (!linkToken) { res.status(400).json({ error: 'linkToken required' }); return; }

      let link: CloudLink | null = null;

      if (tok) {
        try {
          const resp = await fetch(`${djangoBase()}/api/bridge/device-link/remote-login/`, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ deviceToken: tok, linkToken }),
          });
          if (resp.ok) {
            const j = await resp.json();
            if (j.valid) {
              link = {
                cloudUserId:     j.cloudUserId,
                username:        j.username,
                displayName:     j.displayName || j.username,
                role:            j.role === 'operator' ? 'operator' : 'admin',
                linkToken:       j.linkToken,
                linkedAt:        j.linkedAt,
                slot:            typeof j.localSlot === 'number' ? j.localSlot : null,
                lastRemoteLogin: j.lastRemoteLogin ?? null,
              };
              upsertCloudLink(link);
            }
          } else if (resp.status === 404 || resp.status === 403) {
            // Revoked or unknown — do NOT fall back to the cache.
            removeCloudLink(findCloudLinkByToken(linkToken)?.cloudUserId ?? '');
            res.status(401).json({ error: 'link revoked' });
            return;
          }
        } catch (e: any) {
          console.warn('[Cloud] Django unreachable, attempting offline cache:', e.message);
        }
      }

      // Offline fallback: match on locally cached linkToken.
      if (!link) link = findCloudLinkByToken(linkToken);
      if (!link) { res.status(401).json({ error: 'invalid linkToken' }); return; }

      const lvl = link.role === 'admin' ? 2 : 1;
      currentLevel = lvl;
      currentActor = `cloud:${link.username}`;
      currentSlot = link.slot;
      touchCloudLinkLogin(link.cloudUserId);
      recordLogin(currentActor, currentSlot, lvl, req.ip);

      res.json({
        ok: true,
        level: lvl,
        username: link.username,
        displayName: link.displayName,
        role: link.role,
        slot: link.slot,
      });
    } catch (err: any) {
      console.error('[Cloud] /cloud/remote-login error:', err.message);
      res.status(500).json({ error: err.message });
    }
  });

  /**
   * POST /iot/cloud/password-login — sign in with Django username + password.
   *   Body: { username, password }
   * Requires Django reachable AND an existing DeviceLink for this user
   * (admins establish links via /cloud/link). Sets session level based
   * on the link's role. Returns the opaque linkToken so the UI can
   * cache it for frictionless re-login.
   */
  router.post('/cloud/password-login', async (req: Request, res: Response) => {
    try {
      const tok = deviceToken();
      if (!tok) { res.status(501).json({ error: 'DJANGO_TOKEN not configured' }); return; }
      const { username, password } = req.body ?? {};
      if (!username || !password) {
        res.status(400).json({ error: 'username and password required' }); return;
      }

      let resp: Response | undefined;
      try {
        resp = await fetch(`${djangoBase()}/api/bridge/device-link/password-login/`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ deviceToken: tok, username, password }),
        }) as unknown as Response;
      } catch (e: any) {
        // Offline path: verify against the cached hash on the link (if any).
        const cached = findCloudLinkByUsername(username);
        if (cached && verifyCloudLinkPassword(cached, password)) {
          const lvl = cached.role === 'admin' ? 2 : 1;
          currentLevel = lvl;
          currentActor = `cloud:${cached.username}`;
          currentSlot = cached.slot;
          touchCloudLinkLogin(cached.cloudUserId);
          recordLogin(currentActor, currentSlot, lvl, req.ip);
          res.json({
            ok: true,
            offline: true,
            level: lvl,
            username: cached.username,
            displayName: cached.displayName,
            role: cached.role,
            slot: cached.slot,
            linkToken: cached.linkToken,
          });
          return;
        }
        recordLoginFailure(`cloud:${username}`, req.ip);
        res.status(503).json({ error: 'cloud unreachable' });
        return;
      }

      const text = await (resp as any).text();
      if (!(resp as any).ok) {
        let msg = text;
        try { msg = JSON.parse(text).error ?? text; } catch { /* ignore */ }
        recordLoginFailure(`cloud:${username}`, req.ip);
        res.status((resp as any).status).json({ error: msg });
        return;
      }
      const j = JSON.parse(text);
      const link: CloudLink = {
        cloudUserId:     j.cloudUserId,
        username:        j.username,
        displayName:     j.displayName || j.username,
        role:            j.role === 'operator' ? 'operator' : 'admin',
        linkToken:       j.linkToken,
        linkedAt:        j.linkedAt,
        slot:            typeof j.localSlot === 'number' ? j.localSlot : null,
        lastRemoteLogin: j.lastRemoteLogin ?? null,
      };
      upsertCloudLink(link);
      // Cache a fresh scrypt hash so this user can log in when Django is offline.
      setCloudLinkPassword(link.cloudUserId, password);

      const lvl = link.role === 'admin' ? 2 : 1;
      currentLevel = lvl;
      currentActor = `cloud:${link.username}`;
      currentSlot = link.slot;
      touchCloudLinkLogin(link.cloudUserId);
      recordLogin(currentActor, currentSlot, lvl, req.ip);

      res.json({
        ok: true,
        level: lvl,
        username: link.username,
        displayName: link.displayName,
        role: link.role,
        slot: link.slot,
        linkToken: link.linkToken,
      });
    } catch (err: any) {
      console.error('[Cloud] /cloud/password-login error:', err.message);
      res.status(500).json({ error: err.message });
    }
  });

  // ─── Version check (HEAD) ─────────────────────────────────────────────

  router.head('/version', (_req: Request, res: Response) => {
    res.status(200).end();
  });

  // (S9k cleanup 2026-04-21: duplicate GET /user/download handler removed
  // — Express only ever invoked the first one at line ~1033.)

  // ─── Debug / health ───

  router.get('/health', (_req: Request, res: Response) => {
    res.json({
      status: 'ok',
      uptime: process.uptime(),
      cgiVars: dataCache.length,
      cached: Object.keys(dataCache.getAll()).length,
    });
  });

  router.get('/debug/cache', (_req: Request, res: Response) => {
    res.json(dataCache.getAll());
  });

  // (Apr 2026 proto-direct migration: parseAuxProgram, buildDefaultAuxRules,
  // and discoverAuxPrograms removed alongside /iot/aux/all. The auxiliary
  // page now consumes auxiliaryComposite (protoStores.ts) directly.)


  // â”€â”€â”€ VFD / Fans page endpoints â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

  router.get('/fans', (_req: Request, res: Response) => {
    if (!vfdClient) {
      res.json({ drives: [] });
      return;
    }
    res.json({ drives: vfdClient.getDrives() });
  });

  // NOTE: GET /vfd/alarms (the WARN_VFD synthesizer) was removed when
  // fan-failure detection moved into Nova firmware
  // (nova_failures.c::FanFailChk + nova_vfd.c). Drive faults now flow
  // through the standard WARN_FAN path with the faulted drive index in
  // the eqIo slot, so the UI alarm modal renders them via the same
  // AlarmData stream as every other firmware warning.

  router.post('/fans/control', async (req: Request, res: Response) => {
    if (!vfdClient) {
      res.status(503).json({ ok: false, error: 'VFD client not available' });
      return;
    }
    const { unitId, action, controlWord, speedRefPercent } = req.body;
    if (typeof unitId !== 'number') {
      res.status(400).json({ ok: false, error: 'unitId required' });
      return;
    }
    const validActions = ['start', 'stop', 'reset', 'toggle-direction'] as const;
    type VFDAction = typeof validActions[number];
    let ok = false;
    if (typeof action === 'string' && validActions.includes(action as VFDAction)) {
      ok = await vfdClient.sendAction(unitId, action as VFDAction, speedRefPercent);
    } else if (typeof controlWord === 'number') {
      // Raw control word (legacy / testing)
      ok = await vfdClient.writeDrive(unitId, controlWord, speedRefPercent);
    } else {
      res.status(400).json({ ok: false, error: 'action or controlWord required' });
      return;
    }
    res.json({ ok });
  });

  // Inject a random realistic fault into a drive (simulation only).
  // Picks a random fault code from the drive's manufacturer fault table,
  // then proxies to the RS485 panel's /api/vfd/fault endpoint (which owns
  // the VFDSimulator instance).
  router.post('/fans/inject-fault', async (req: Request, res: Response) => {
    if (!vfdClient) {
      res.status(503).json({ ok: false, error: 'VFD client not available' });
      return;
    }
    const { unitId } = req.body;
    if (typeof unitId !== 'number') {
      res.status(400).json({ ok: false, error: 'unitId required' });
      return;
    }
    // Pick a random fault from the manufacturer's table
    const snap = vfdClient.getDrive(unitId);
    const profile = getProfile(snap?.manufacturer);
    const faultCodes = Object.keys(profile.faultNames).map(Number);
    if (faultCodes.length === 0) {
      res.status(500).json({ ok: false, error: 'No fault codes for manufacturer' });
      return;
    }
    const faultCode = faultCodes[Math.floor(Math.random() * faultCodes.length)];
    const faultName = profile.faultNames[faultCode];

    // (Apr 29 2026 mbtcp migration) The orbit-simulator was the only
    // VFDSimulator host; with it retired, fault injection has no place
    // to land. Real hardware doesn't take injected faults — they happen
    // for real. Surface honestly so the UI fault-injection UX (sim-only
    // affordance) can be retired or rewired to talk straight to a
    // standalone VFD sim.
    void unitId; void faultCode; void faultName;
    res.status(503).json({
      ok: false,
      error: 'VFD fault injection retired with orbit-simulator REST API',
    });
  });

  // Fan control mode configuration.
  // GET  /fans/config â†’ { mode: 'legacy' | 'vfd' }
  // POST /fans/config â†’ { mode: 'legacy' | 'vfd' }
  // Default is 'legacy' which preserves existing digital I/O fan control.
  // 'vfd' mode forwards firmware fan speed to VFD drives via Modbus TCP
  // and routes VFD drive faults into the system alarm display.
  router.get('/fans/config', async (_req: Request, res: Response) => {
    const { loadConfig } = await import('./simConfig.js');
    const mode = loadConfig<string>('fan-control-mode') ?? 'legacy';
    res.json({ mode });
  });

  router.post('/fans/config', async (req: Request, res: Response) => {
    const { mode } = req.body;
    if (mode !== 'legacy' && mode !== 'vfd') {
      res.status(400).json({ ok: false, error: 'mode must be "legacy" or "vfd"' });
      return;
    }
    const { saveConfig } = await import('./simConfig.js');
    saveConfig('fan-control-mode', mode);
    console.log(`[API] Fan control mode set to: ${mode}`);
    res.json({ ok: true, mode });
  });

  // Write a single VFD parameter register (ramp times, freq limits, nameplate, etc.)
  // Accepts either a raw { addr } or a named { param } (resolved via register map).
  router.post('/fans/param', async (req: Request, res: Response) => {
    if (!vfdClient) {
      res.status(503).json({ ok: false, error: 'VFD client not available' });
      return;
    }
    const { unitId, addr, param, value } = req.body;
    if (typeof unitId !== 'number' || typeof value !== 'number') {
      res.status(400).json({ ok: false, error: 'unitId and value required (numbers)' });
      return;
    }
    let resolvedAddr = addr;
    if (typeof param === 'string') {
      const snap = vfdClient.getDrive(unitId);
      const profile = getProfile(snap?.manufacturer);
      resolvedAddr = (profile.paramAddrs as Record<string, number>)[param];
    }
    if (typeof resolvedAddr !== 'number') {
      res.status(400).json({ ok: false, error: 'addr or valid param name required' });
      return;
    }
    const ok = await vfdClient.writeParam(unitId, resolvedAddr, value);
    res.json({ ok });
  });

  // Set drive metadata (label, manufacturer) â€” stored in sim config, not Modbus
  router.post('/fans/meta', async (req: Request, res: Response) => {
    if (!vfdClient) {
      res.status(503).json({ ok: false, error: 'VFD client not available' });
      return;
    }
    const { unitId, label, manufacturer } = req.body;
    if (typeof unitId !== 'number') {
      res.status(400).json({ ok: false, error: 'unitId required' });
      return;
    }
    // Update the in-memory snapshot
    vfdClient.setDriveMeta(unitId, { label, manufacturer });
    // Also persist to the simulator's config file
    const { loadConfig, saveConfig } = await import('./simConfig.js');
    const drives = loadConfig<any[]>('vfd-drives') ?? [];
    const idx = drives.findIndex((d: any) => d.unitId === unitId);
    if (idx >= 0) {
      if (label !== undefined) drives[idx].label = label;
      if (manufacturer !== undefined) drives[idx].manufacturer = manufacturer;
      saveConfig('vfd-drives', drives);
    }
    res.json({ ok: true });
  });

  // Apply settings to all drives at once (speed, ramp, freq limits)
  // Supports action-based control { action, speedRefPercent } and named params.
  router.post('/fans/set-all', async (req: Request, res: Response) => {
    if (!vfdClient) {
      res.status(503).json({ ok: false, error: 'VFD client not available' });
      return;
    }
    const { action, controlWord, speedRefPercent, params } = req.body;
    const validActions = ['start', 'stop', 'reset', 'toggle-direction'] as const;
    type VFDAction = typeof validActions[number];
    const allDrives = vfdClient.getDrives();
    const results: boolean[] = [];
    for (const drv of allDrives) {
      if (!drv.online) continue;
      if (typeof action === 'string' && validActions.includes(action as VFDAction)) {
        results.push(await vfdClient.sendAction(drv.unitId, action as VFDAction, speedRefPercent));
      } else if (typeof controlWord === 'number') {
        results.push(await vfdClient.writeDrive(drv.unitId, controlWord, speedRefPercent));
      }
      if (Array.isArray(params)) {
        const profile = getProfile(drv.manufacturer);
        for (const p of params) {
          const addr = (typeof p.param === 'string')
            ? (profile.paramAddrs as Record<string, number>)[p.param]
            : p.addr;
          if (typeof addr === 'number' && typeof p.value === 'number') {
            results.push(await vfdClient.writeParam(drv.unitId, addr, p.value));
          }
        }
      }
    }
    res.json({ ok: results.every(Boolean), count: allDrives.filter(d => d.online).length });
  });

  // â”€â”€ System Groups persistence (server-side) â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

  router.get('/groups', (_req: Request, res: Response) => {
    const groups = loadConfig('systemGroups') ?? { groups: [], selectedGroupId: null };
    res.json(groups);
  });

  router.post('/groups', (req: Request, res: Response) => {
    const body = req.body;
    if (!body || !Array.isArray(body.groups)) {
      res.status(400).json({ ok: false, error: 'Invalid groups payload' });
      return;
    }
    saveConfig('systemGroups', body);
    res.json({ ok: true });
  });

  /* â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• *
   *  Nova Firmware Update endpoints                                      *
   * â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â• */

  /** GET /iot/firmware/status â€” current update state + bank info */
  router.get('/firmware/status', (_req: Request, res: Response) => {
    if (!fwUpdateManager) {
      res.status(503).json({ error: 'Firmware update manager not available' });
      return;
    }
    res.json({
      status: fwUpdateManager.getStatus(),
      bankInfo: fwUpdateManager.getBankInfo(),
    });
  });

  /** POST /iot/firmware/upload â€” upload a .bin firmware binary */
  router.post('/firmware/upload', (req: Request, res: Response) => {
    if (!fwUpdateManager) {
      res.status(503).json({ error: 'Firmware update manager not available' });
      return;
    }
    if (fwUpdateManager.isUpdating()) {
      res.status(409).json({ error: 'Update already in progress' });
      return;
    }

    const busboy = Busboy({
      headers: req.headers,
      limits: { fileSize: 2 * 1024 * 1024 },  // 2 MB max (bank size)
    });
    let fileProcessed = false;
    let version = '';

    busboy.on('field', (name: string, val: string) => {
      if (name === 'version') version = val;
    });

    busboy.on('file', (_fieldname: string, stream: NodeJS.ReadableStream, info: { filename: string }) => {
      const filename = info.filename;
      if (!filename.endsWith('.bin')) {
        (stream as any).resume();
        if (!res.headersSent) res.status(400).json({ error: 'File must be a .bin firmware image' });
        return;
      }

      fileProcessed = true;
      const chunks: Buffer[] = [];

      stream.on('data', (chunk: Buffer) => chunks.push(chunk));
      stream.on('end', () => {
        const firmware = Buffer.concat(chunks);
        if (firmware.length === 0) {
          if (!res.headersSent) res.status(400).json({ error: 'Empty firmware file' });
          return;
        }

        const ver = version || filename.replace(/\.bin$/i, '');
        console.log(`[API] Firmware upload: ${filename} (${firmware.length} bytes), version=${ver}`);

        // Start update asynchronously â€” progress via /firmware/status or WebSocket
        fwUpdateManager!.startUpdate(firmware, ver)
          .then(() => {
            console.log(`[API] Firmware update verified â€” ready to activate`);
          })
          .catch((err) => {
            console.error(`[API] Firmware update failed: ${err.message}`);
          });

        if (!res.headersSent) {
          res.json({ ok: true, size: firmware.length, version: ver });
        }
      });
    });

    busboy.on('finish', () => {
      if (!fileProcessed && !res.headersSent) {
        res.status(400).json({ error: 'No firmware file provided' });
      }
    });

    busboy.on('error', (err: Error) => {
      console.error('[API] Firmware upload busboy error:', err.message);
      if (!res.headersSent) res.status(500).json({ error: err.message });
    });

    req.pipe(busboy);
  });

  /** POST /iot/firmware/activate â€” swap banks and reboot */
  router.post('/firmware/activate', async (req: Request, res: Response) => {
    if (!fwUpdateManager) {
      res.status(503).json({ error: 'Firmware update manager not available' });
      return;
    }

    const reboot = req.body?.reboot !== false;  // default true

    try {
      await fwUpdateManager.activate(reboot);
      res.json({ ok: true, message: reboot ? 'Activating and rebooting' : 'Activated (no reboot)' });
    } catch (err: any) {
      res.status(409).json({ error: err.message });
    }
  });

  // ─── GDC (Door Controller) API ───────────────────────────────────────
  // Reads/writes the GDC orbit board's HR map directly via Modbus TCP
  // (orbitMbtcp.ts). Per-slot host comes from `resolveOrbitHost(slot)`.
  // Errors are surfaced honestly: 503 with the underlying mbtcp error
  // when the board is unreachable; only the "no GDC role assigned" case
  // returns the soft `{ present: false }` reply the UI expects.
  //
  // HR map (orbit_gdc.h):
  //   300       door_pct_x10            R/W   0..1000
  //   301       default_travel_ms       R/W   ms
  //   302       active_stage_count      R     uint
  //   303       calibrating             R     0/1
  //   304       cal_phase               R     0=idle 1=opening 2=closing
  //   305       cal_request             W     1 = start calibration
  //   310..314  actuator[i].pos_x10     R     0..1000
  //   315..319  actuator[i].target_x10  R     0..1000
  //   320..324  actuator[i].status_bits R     bit0 moving / bit1 open_sw
  //                                            bit2 close_sw / bit3 calibrated
  //   325..329  actuator[i].stage       R/W   0..5  (0 = unassigned)
  //   330..334  actuator[i].open_ms     R/W   uint
  //   335..339  actuator[i].close_ms    R/W   uint

  function findGdcSlot(): number | null {
    // role 2 = GDC. dataCache is populated from firmware OrbitStatus.
    const gdc = dataCache.getOrbitBoards().find(b => b.role === 2 && b.connected);
    return gdc ? gdc.slot : null;
  }


  /** GET /iot/gdc — synthesised from HR 300..339, sourced from the
   *  Phase 4b OrbitSensorBank role window (envelope tag 124) cached
   *  in novaStore. Bridge no longer opens Modbus TCP to the GDC
   *  orbit — Nova polls it; bridge decodes the cached snapshot. */
  router.get('/gdc', async (_req: Request, res: Response) => {
    const slot = findGdcSlot();
    if (slot === null) { res.json({ present: false }); return; }
    const bank = novaStore?.getOrbitBank(slot, 300);
    if (!bank) { res.json({ present: false, slot, awaiting: true }); return; }

    // Indexing helper relative to base 300. Bank values are raw uint16.
    const r = (a: number) => bank.values[a - 300] ?? 0;

    const actuators: Array<{
      index: number;
      label: string;
      position: number;
      target: number;
      stageAssignment: number;
      openTravelTime: number;
      closeTravelTime: number;
      moving: boolean;
      openSwitch: boolean;
      closeSwitch: boolean;
      calibrated: boolean;
    }> = [];
    const ACT_COUNT = 5;
    for (let i = 0; i < ACT_COUNT; i++) {
      const status = r(320 + i);
      actuators.push({
        index:        i + 1,
        // Default label "Door 1".."Door 5" since the LP HR map carries
        // no per-actuator labels.
        // LP-MISSING: actuator labels — needs HR string region in firmware
        label:        `Door ${i + 1}`,
        // Phase 4b contract fix: was `pos` in the legacy mbtcp path; UI
        // always read `actuator.position`, so the field rename lands here.
        position:        r(310 + i) / 10,           // 0..100.0%
        target:          r(315 + i) / 10,
        stageAssignment: r(325 + i),
        openTravelTime:  r(330 + i),
        closeTravelTime: r(335 + i),
        moving:       (status & 0x0001) !== 0,
        openSwitch:   (status & 0x0002) !== 0,
        closeSwitch:  (status & 0x0004) !== 0,
        calibrated:   (status & 0x0008) !== 0,
      });
    }

    // Synthesise stages[] from actuator.stageAssignment. Stage labels
    // default to "Stage N" — the LP HR map has no label region.
    // LP-MISSING: stage labels — needs HR string region in firmware
    const stageNums = Array.from(
      new Set(actuators.map(a => a.stageAssignment).filter(s => s > 0)),
    ).sort((a, b) => a - b);
    const stages = stageNums.map(stageNum => ({
      stageNum,
      label: `Stage ${stageNum}`,
      doors: actuators
        .filter(a => a.stageAssignment === stageNum)
        .map(a => ({ index: a.index, label: a.label })),
    }));

    // Total system capacity = sum of open_travel_ms over assigned
    // actuators (uncalibrated → default_travel_ms fallback).
    const defaultTravel = r(301);
    let totalCapacity = 0;
    for (const a of actuators) {
      if (a.stageAssignment > 0) {
        totalCapacity += (a.calibrated && a.openTravelTime > 0)
          ? a.openTravelTime
          : defaultTravel;
      }
    }

    res.json({
      present: true,
      slot,
      freshAirDoorPct:    r(300) / 10,
      actuatorTravelTime: defaultTravel,
      activeStageCount:   r(302),
      calibrating:        r(303) !== 0,
      calibrationPhase:   r(304),
      totalCapacity,
      stages,
      actuators,
      ageMs: Date.now() - bank.ts,
      // LP-MISSING: per-stage runtime totals — needs HR counters in firmware
      // LP-MISSING: door labels — needs HR string region in firmware
    });
  });

  /** POST /iot/gdc/stages — reassign actuators to stages.
   *  Body: { stages: [{ stageNum, doors: [1,2,...] }] }
   *  Writes HR_GDC_STAGE_BASE+i (325..329) per actuator. Doors not
   *  named in any stage are cleared to stage 0 (unassigned). Stage
   *  labels in the request body are ignored — LP has no label region.
   */
  router.post('/gdc/stages', async (req: Request, res: Response) => {
    const slot = findGdcSlot();
    if (slot === null) {
      res.status(404).json({ ok: false, error: 'No GDC board found' });
      return;
    }
    const stages = req.body?.stages;
    if (!Array.isArray(stages)) {
      res.status(400).json({ ok: false, error: 'stages must be an array' });
      return;
    }
    const ACT_COUNT = 5;
    const stageOf = new Array<number>(ACT_COUNT).fill(0);
    for (const s of stages) {
      if (typeof s?.stageNum !== 'number') {
        res.status(400).json({ ok: false, error: 'each stage needs stageNum' });
        return;
      }
      if (Array.isArray(s.doors)) {
        for (const d of s.doors) {
          const idx = (d | 0) - 1;
          if (idx >= 0 && idx < ACT_COUNT) stageOf[idx] = s.stageNum & 0xFFFF;
        }
      }
    }
    if (!serialBridge.orbitRegWrite) {
      res.status(503).json({ ok: false, error: 'orbitRegWrite_not_supported' });
      return;
    }
    try {
      // Phase 4b Sub-phase 2: Nova picks FC16 for the 5-reg block write
      // (325..329) and issues it via its existing Modbus TCP client.
      await serialBridge.orbitRegWrite(slot, 325, stageOf);
      res.json({ ok: true });
    } catch (err: any) {
      res.status(503).json({ ok: false, error: String(err?.message ?? err) });
    }
  });

  /** POST /iot/gdc/calibrate — kick HR_GDC_CAL_REQUEST=1. */
  router.post('/gdc/calibrate', async (_req: Request, res: Response) => {
    const slot = findGdcSlot();
    if (slot === null) {
      res.status(404).json({ ok: false, error: 'No GDC board found' });
      return;
    }
    if (!serialBridge.orbitRegWrite) {
      res.status(503).json({ ok: false, error: 'orbitRegWrite_not_supported' });
      return;
    }
    try {
      // Phase 4b Sub-phase 2: Nova picks FC06 (single value) and writes
      // HR_GDC_CAL_REQUEST=305 ← 1.
      await serialBridge.orbitRegWrite(slot, 305, [1]);
      res.json({ ok: true });
    } catch (err: any) {
      res.status(503).json({ ok: false, error: String(err?.message ?? err) });
    }
  });

  // ─── Triton (Refrigeration) API ──────────────────────────────────────
  // Reads happen via direct Modbus TCP to the Triton orbit (mbtcp pool,
  // see orbitMbtcp.ts). Writes still go through the LP firmware
  // (`serialBridge.tritonRegWrite`, envelope tag 125) since the bridge
  // doesn't own the Modbus master role on the orbit bus in production —
  // that was a sim-mode artefact. Per-slot host: `resolveOrbitHost(slot)`.

  /**
   * GET /iot/triton/list — summary of all Triton boards (used to build the
   * tab strip on the refrigeration page).  Always returns 200 with an array;
   * empty array means "no Tritons present, hide the SCADA section".
   */
  router.get('/triton/list', async (_req: Request, res: Response) => {
    const tritons = dataCache.getOrbitBoards().filter(b => b.role === 3);
    res.json({
      tritons: tritons.map(t => ({
        slot: t.slot,
        connected: t.connected,
        // Label is best-effort; full label resolution happens on /triton/:slot.
        label: `Triton ${t.slot + 1}`,
      })),
    });
  });

  /**
   * GET /iot/triton/:slot — full state snapshot synthesised from the
   * Triton HR map (orbit_triton.h). Fields the LP doesn't currently
   * expose are absent from the response — the UI must use optional
   * chaining or accept visible breakage as a real follow-up item.
   *
   * Reads (4 separate FC03 transactions, executed serially through the
   * mbtcp pool's per-host queue):
   *   400..439  setpoints           (40 regs)
   *   440..453  failure modes       (14 regs, 2-per-sensor)
   *   460..473  live sensor values  (14 regs, value+valid pair)
   *   480..493  live equipment      (14 regs)
   *   530..542  alarm bitmaps + ack (13 regs)
   *
   * LP-MISSING vs the retired sim REST shape:
   *   - manualMode / label / orbitId / role / enabled (orbitId+role from
   *     dataCache; the rest absent — needs HR or proto string region)
   *   - compressor.{amps, totalRuntimeHours, dailyRuntimeHours} —
   *     needs HR runtime/amps regs in firmware
   *   - condenserFans.* — needs HR cond-fan stage/pid/per-fan-runtime regs
   *   - exvOpenPct / evapFanOn — needs HR live regs
   *   - oilPumpOn — derivable from compressorOn for now (omitted)
   *   - defrost.* — needs HR defrost stage/timer regs
   *   - leakDetect — needs HR leak detector regs
   *   - safeties.{lockoutMask, dismissed, ...} — needs HR safety regs
   *   - logs.{pid,user,sys} — needs proto log channel
   *   - physical.{digitalOutputs, analogOutputs, outputLabels, doRole, aoMode}
   *     — DO/AO are HR-resident on the orbit but not yet in the LP map;
   *     `ioConfig` block (aoMode/doRole) needs HR regs
   *   - safePolicy — needs HR safe-mode policy regs
   *   - failureStates / safeOffMask — `safeOffMask` is in HR 492..493,
   *     `failureStates` per-channel timer state is not in HR
   */
  router.get('/triton/:slot', async (req: Request, res: Response) => {
    const slot = parseInt(req.params.slot, 10);
    if (isNaN(slot)) { res.status(400).json({ ok: false, error: 'invalid slot' }); return; }
    const board = dataCache.getOrbitBoards().find(
      b => b.slot === slot && b.role === 3 && b.connected,
    );
    if (!board) { res.json({ ok: true, present: false }); return; }

    // Phase 4b: the entire TRITON HR window (400..655) rides in a
    // single OrbitSensorBank push at hr_base=400. Bridge no longer
    // opens Modbus TCP to the orbit — Nova polls, bridge decodes the
    // cached snapshot.
    const bank = novaStore?.getOrbitBank(slot, 400);
    if (!bank) { res.json({ ok: true, present: false, awaiting: true }); return; }

    // Indexing helper relative to base 400 (single block read). Each
    // block from orbit_triton.h has its own offset within the window:
    //   400..439 setpoints           → values[0..39]
    //   440..453 failure modes       → values[40..53]
    //   460..473 live sensor values  → values[60..73]
    //   480..493 live equipment      → values[80..93]
    //   530..542 alarms + ack cmd    → values[130..142]
    const r = (a: number) => bank.values[a - 400] ?? 0;
    const sp    = (off: number) => r(400 + off);
    const fail  = (off: number) => r(440 + off);
    const sens  = (off: number) => r(460 + off);
    const equip = (off: number) => r(480 + off);
    const alarmReg = (off: number) => r(530 + off);

    // Setpoints decode.
    const minOffTimeSec = sp(4);
    const setpoints: Record<string, number> = {
      enabled:                   sp(0),
      refrigerantType:           sp(1),
      cutInP:                    asS16(sp(2))  / 10,
      cutOutP:                   asS16(sp(3))  / 10,
      minOffTimeSec,
      // Phase 4b contract fix: TritonHotspotPopup binds `minOffTime`
      // (no `Sec` suffix); preserve both so neither path drifts.
      minOffTime:                minOffTimeSec,
      minRuntime:                sp(5),
      warmUpSec:                 sp(6),
      powerFailMinutes:          sp(7),
      lowAmbientCutoutF:         asS16(sp(8))  / 10,
      pumpDownMode:              sp(9),
      discHighUnloadP:           asS16(sp(10)) / 10,
      sucLowUnloadP:             asS16(sp(11)) / 10,
      superheatTarget:           asS16(sp(12)) / 10,
      superheatLowF:             asS16(sp(13)) / 10,
      unloaderOnPct0:            sp(14),
      unloaderOnPct1:            sp(15),
      unloaderOffPct0:           sp(16),
      unloaderOffPct1:           sp(17),
      unloaderHpUnloadPsi0:      asS16(sp(18)) / 10,
      unloaderHpUnloadPsi1:      asS16(sp(19)) / 10,
      unloaderHpLoadPsi0:        asS16(sp(20)) / 10,
      unloaderHpLoadPsi1:        asS16(sp(21)) / 10,
      unloaderLpUnloadPsi0:      asS16(sp(22)) / 10,
      unloaderLpUnloadPsi1:      asS16(sp(23)) / 10,
      unloaderLpLoadPsi0:        asS16(sp(24)) / 10,
      unloaderLpLoadPsi1:        asS16(sp(25)) / 10,
      unloaderNormal:            sp(26),
    };

    // Failure modes — pair {mode, delaySec} per sensor. Order MUST
    // match the sensor index in orbit_triton.h.
    const sensorKeys = ['suctionP','dischargeP','oilP','suctionT','dischargeT','llsT','ambientT'] as const;
    const failures: Record<string, { mode: number; delaySec: number }> = {};
    for (let i = 0; i < sensorKeys.length; i++) {
      failures[sensorKeys[i]] = {
        mode:     fail(i * 2),
        delaySec: fail(i * 2 + 1),
      };
    }

    // Live sensor values — pair {value_x10 (signed), valid 0/1}.
    // Pressure channels (suctionP..oilP) are PSI×10; temperature
    // channels are °F×10 (Triton convention — do NOT convert to °C
    // bridge-side; the UI consumes the raw °F).
    const sensors: Record<string, { value: number; valid: boolean }> = {};
    for (let i = 0; i < sensorKeys.length; i++) {
      sensors[sensorKeys[i]] = {
        value: asS16(sens(i * 2)) / 10,
        valid: sens(i * 2 + 1) !== 0,
      };
    }

    // Live equipment (HR 480..493 → offsets 0..13 within the block).
    const compressorRuntimeSec = (equip(7) << 16) | equip(6);   // _hi << 16 | _lo
    const safeOffMask = (equip(13) << 16) | equip(12);          // hi<<16 | lo
    const compressor = {
      on:          equip(0) !== 0,
      status:      equip(1),
      runtimeSec:  compressorRuntimeSec,
    };
    const unloaders = {
      on: [
        (equip(3) & 0x01) !== 0,
        (equip(3) & 0x02) !== 0,
      ],
      hpForced: [
        (equip(4) & 0x01) !== 0,
        (equip(4) & 0x02) !== 0,
      ],
      lpForced: [
        (equip(5) & 0x01) !== 0,
        (equip(5) & 0x02) !== 0,
      ],
      normal: setpoints.unloaderNormal,
    };

    // Alarms — 6 regs active + 6 regs acked + 1 ack-cmd at HR 530..542.
    const activeBits: number[] = [];
    const ackedBits:  number[] = [];
    for (let i = 0; i < 6; i++) {
      activeBits.push(alarmReg(i));
      ackedBits.push(alarmReg(6 + i));
    }

    // Phase 4b contract fix: the UI's TritonScadaSection/Hotspot popup
    // consumes `state.alarms` as an array of {label, active, acked}
    // objects (see TritonScadaSection.svelte:59 `.filter(a => a.active)`
    // and HotspotPopup line 951 `each state.alarms as a`). The legacy
    // mbtcp path returned `{activeBits, ackedBits}` bitmaps, leaving
    // those consumers permanently broken. Decode the bits here into
    // the array shape the UI already expects. The bitmap is still
    // exposed under `alarms.bitmaps` for the debug-view consumers.
    const TRITON_ALARM_LABELS: Record<number, string> = {
      0: 'Suction low pressure',
      1: 'Suction high pressure',
      2: 'Discharge high pressure',
      3: 'Discharge high temp',
      4: 'Oil low pressure',
      6: 'Phase fault',
      7: 'HP safety',
      8: 'LP safety',
      9: 'Compressor overload',
      10: 'Condenser overload',
      11: 'Permit fault',
      13: 'Superheat fault',
      14: 'Superheat low',
      15: 'Discharge sensor fault',
      16: 'Suction sensor fault',
      17: 'Oil sensor fault',
      18: 'Outside air sensor fault',
      20: 'Suction-P sensor fault',
      21: 'Discharge-P sensor fault',
      22: 'Oil-P sensor fault',
      23: 'Suction-T sensor fault',
      24: 'Discharge-T sensor fault',
      25: 'LLS-T sensor fault',
      26: 'Ambient-T sensor fault',
    };
    const alarmObjects: Array<{ code: number; label: string; active: boolean; acked: boolean }> = [];
    const totalBits = 6 * 16;
    for (let code = 0; code < totalBits; code++) {
      const wordIdx = code >> 4;
      const bit = 1 << (code & 0x0F);
      const active = (activeBits[wordIdx] & bit) !== 0;
      const acked  = (ackedBits[wordIdx]  & bit) !== 0;
      if (!active && !acked) continue;
      alarmObjects.push({
        code,
        label: TRITON_ALARM_LABELS[code] ?? `Alarm ${code}`,
        active,
        acked,
      });
    }

    const superheatF = asS16(equip(11)) / 10;
    res.json({
      ok: true,
      present: true,
      orbitId: board.slot + 2,            // dipswitch id from slot
      // LP-MISSING: orbit role string — synthesise from dataCache role
      role: 'TRITON',
      // LP-MISSING: enabled / label / manualMode — needs HR string + flag regs
      enabled: setpoints.enabled !== 0,
      // Default label until LP exposes a string region — UI's
      // TritonScadaSection header reads `state.label`.
      label: `Triton ${board.slot + 1}`,
      compressor,
      // LP-MISSING: condenserFans.{stage,count,targetP,leadIndex,vfdPct,pid,fans} — needs HR regs
      capacity: {
        stage: equip(2),
        unloaderBits: equip(3),
        hpForcedBits: equip(4),
        lpForcedBits: equip(5),
      },
      // LP-MISSING: evapFanOn / oilPumpOn / exvOpenPct — needs HR live regs
      unloaders,
      // LP-MISSING: defrost.* — needs HR defrost SM regs
      demand: equip(9),                   // 0..100
      sensors,
      derived: {
        suctionSatTempF: asS16(equip(10)) / 10,
        superheatF,
        // Phase 4b contract fix: TritonMimic reads `state.derived.superheat`;
        // legacy path only exposed `superheatF` so the gauge never updated.
        superheat:       superheatF,
        // LP-MISSING: leakDetect — needs HR leak-detector regs
      },
      setpoints,
      failures,
      // LP-MISSING: ioConfig.{aoMode,doRole} — needs HR I/O config regs
      // LP-MISSING: safeties.{lockoutMask,...} — needs HR safety regs
      safeOffMask,
      // LP-MISSING: failureStates per-channel timer — not in HR map
      // LP-MISSING: safePolicy — needs HR safe-policy regs
      // Phase 4b contract: alarms is now array-of-objects (UI shape),
      // with raw bitmaps preserved under `.bitmaps` for debug consumers.
      alarms: alarmObjects,
      alarmBitmaps: { activeBits, ackedBits },
      ageMs: Date.now() - bank.ts,
      // LP-MISSING: logs.{pid,user,sys} — needs proto log channel
      // LP-MISSING: physical.{digitalOutputs,analogOutputs,outputLabels} — needs HR I/O state regs
    });
  });

  /**
   * Helper: write a Modbus holding register to a Triton orbit.
   *
   * Forwards through the LP AM2434 firmware via the TritonRegWrite proto
   * envelope (tag 125). The LP runs the actual Modbus TCP client; the
   * bridge is a transparent transport gateway. Returns an HTTP-friendly
   * envelope; the proto send is fire-and-(short)-forget — firmware does
   * not currently echo a per-write ack, so success here means "frame
   * queued onto the UART without protocol error".
   */
  async function writeTritonReg(slot: number, addr: number, value: number): Promise<{ ok: boolean; error?: string }> {
    if (!serialBridge.tritonRegWrite) {
      return { ok: false, error: 'tritonRegWrite_not_supported' };
    }
    const triton = dataCache.getOrbitBoards().find(b => b.slot === slot && b.role === 3 && b.connected);
    if (!triton) return { ok: false, error: 'No Triton at slot' };
    try {
      await serialBridge.tritonRegWrite(slot, addr, value & 0xFFFF);
      return { ok: true };
    } catch (err: any) {
      return { ok: false, error: err?.message ?? 'send_failed' };
    }
  }

  /** Convert a signed/clamped float to the unsigned 16-bit wire value (×10). */
  function encodeS16x10(v: number): number {
    const i = Math.round(v * 10);
    return (i < 0 ? i + 0x10000 : i) & 0xFFFF;
  }

  /** POST /iot/triton/:slot/manual  body: { mode: 'auto'|'force-on'|'force-off' } */
  router.post('/triton/:slot/manual', async (req: Request, res: Response) => {
    const slot = parseInt(req.params.slot, 10);
    const mode = req.body?.mode;
    const code = mode === 'force-on' ? 1 : mode === 'force-off' ? 2 : mode === 'auto' ? 0 : -1;
    if (code < 0) { res.status(400).json({ ok: false, error: 'mode must be auto|force-on|force-off' }); return; }
    const result = await writeTritonReg(slot, 325, code);
    res.status(result.ok ? 200 : 502).json(result);
  });

  /** POST /iot/triton/:slot/setpoints  body: partial TritonSetpoints
   *
   *  Accepts ANY of the GRC-derived setpoint keys.  Each is mapped to its
   *  Modbus register and encoded with the right scaling.  Unknown keys are
   *  silently dropped, missing keys are skipped, so the popup modals can
   *  send only the fields they own.  Response includes how many writes
   *  succeeded so the caller can detect partial failures.
   */
  router.post('/triton/:slot/setpoints', async (req: Request, res: Response) => {
    const slot = parseInt(req.params.slot, 10);
    const sp = req.body ?? {};
    const writes: Array<[number, number]> = [];
    const num = (v: unknown): number | null => {
      const n = typeof v === 'number' ? v : parseFloat(v as string);
      return isFinite(n) ? n : null;
    };
    const addScaled = (key: string, reg: number) => {
      const n = num(sp[key]); if (n !== null) writes.push([reg, encodeS16x10(n)]);
    };
    const addInt = (key: string, reg: number) => {
      const n = num(sp[key]); if (n !== null) writes.push([reg, Math.round(n) & 0xFFFF]);
    };

    // 320-339: live + basic (writable)
    addScaled('cutInP',          326);
    addScaled('cutOutP',         327);
    addScaled('superheatTarget', 328);
    addInt   ('minOffTime',      329);
    addInt   ('minRuntime',      330);
    addInt   ('defrostIntervalHours', 331);
    addInt   ('defrostMaxMinutes',    332);
    addScaled('defrostTermT',    333);

    // 340-359: compressor advanced + lead/lag
    addInt   ('warmUpSec',         340);
    addInt   ('proveSec',          341);
    addInt   ('powerFailMinutes',  342);
    addInt   ('crankcaseRunHours', 343);
    addScaled('lowAmbientCutoutF', 344);
    addInt   ('pumpDownMode',      345);
    addInt   ('variableStartPct',  346);
    addScaled('superheatLowF',        347);
    addScaled('superheatWindowHighF', 348);
    addScaled('superheatWindowLowF',  349);
    addInt   ('discHighUnloadP',   350);
    addInt   ('sucLowUnloadP',     351);
    addInt   ('rotateHours',       352);
    addInt   ('groupId',           353);
    addInt   ('rotationOrder',     354);
    addInt   ('refrigerantType',   359);

    // 360-389: condenser fans
    addInt   ('condFanCount',         360);
    addInt   ('condFanVfdMode',       361);
    addInt   ('condFanVfdMinPct',     362);
    addInt   ('condFanVfdMaxPct',     363);
    addInt   ('condFanVfdSetpointP',  364);
    if (Array.isArray(sp.fanStageOnP)) {
      for (let i = 0; i < Math.min(6, sp.fanStageOnP.length); i++) {
        const n = num(sp.fanStageOnP[i]);
        if (n !== null) writes.push([365 + i, Math.round(n) & 0xFFFF]);
      }
    }
    if (Array.isArray(sp.fanStageOffP)) {
      for (let i = 0; i < Math.min(6, sp.fanStageOffP.length); i++) {
        const n = num(sp.fanStageOffP[i]);
        if (n !== null) writes.push([371 + i, Math.round(n) & 0xFFFF]);
      }
    }
    // Floating-head condenser control (mirrors GRC TargetDischarge).
    addInt   ('condenserMode',  377);
    addScaled('condApproachF',  378);
    addInt   ('condMinHeadP',   379);
    addInt   ('condMaxHeadP',   380);
    if (Array.isArray(sp.fanDiffOnP)) {
      for (let i = 0; i < Math.min(6, sp.fanDiffOnP.length); i++) {
        const n = num(sp.fanDiffOnP[i]);
        if (n !== null) writes.push([382 + i, Math.round(n) & 0xFFFF]);
      }
    }
    if (Array.isArray(sp.fanDiffOffP)) {
      for (let i = 0; i < Math.min(6, sp.fanDiffOffP.length); i++) {
        const n = num(sp.fanDiffOffP[i]);
        if (n !== null) writes.push([397 + i, Math.round(n) & 0xFFFF]);
      }
    }

    // 390-409: EXV
    addScaled('exvKp', 390);
    addScaled('exvKi', 391);
    addScaled('exvKd', 392);
    addInt   ('exvMinPct',    393);
    addInt   ('exvMaxPct',    394);
    addInt   ('exvManualPct', 395);
    addScaled('subcoolingTarget', 396);

    // 410-429: defrost
    addInt   ('defrostMode',           410);
    addInt   ('defrostStages',         411);
    addInt   ('dripTimeSec',           412);
    addInt   ('pumpDownBeforeDefrost', 413);

    // 430-449: PID
    addScaled('capP',  430); addScaled('capI',  431);
    addScaled('capD',  432); addScaled('capU',  433);
    addScaled('condP', 434); addScaled('condI', 435);
    addScaled('condD', 436); addScaled('condU', 437);

    // 440-443: leak / low-charge detection
    addInt   ('leakDetectEnabled',    440);
    addScaled('leakSuperheatMarginF', 441);
    addInt   ('leakExvOpenPct',       442);
    addInt   ('leakSustainMinutes',   443);

    if (writes.length === 0) { res.json({ ok: true, written: 0 }); return; }
    let okCount = 0;
    for (const [reg, val] of writes) {
      const r = await writeTritonReg(slot, reg, val);
      if (r.ok) okCount++;
    }
    res.json({ ok: okCount === writes.length, written: okCount, total: writes.length });
  });

  /** POST /iot/triton/:slot/ioconfig  body: { aoMode?: number[], doRole?: number[] } */
  router.post('/triton/:slot/ioconfig', async (req: Request, res: Response) => {
    const slot = parseInt(req.params.slot, 10);
    const { aoMode, doRole } = req.body ?? {};
    const writes: Array<[number, number]> = [];
    if (Array.isArray(aoMode)) {
      // Phase 8 \u2014 Triton hardware ships 2 AO channels; cap writes to 450..451.
      for (let i = 0; i < Math.min(2, aoMode.length); i++) {
        const v = parseInt(aoMode[i], 10);
        if (Number.isFinite(v)) writes.push([450 + i, v & 0xFFFF]);
      }
    }
    if (Array.isArray(doRole)) {
      for (let i = 0; i < Math.min(10, doRole.length); i++) {
        const v = parseInt(doRole[i], 10);
        if (Number.isFinite(v)) writes.push([454 + i, v & 0xFFFF]);
      }
    }
    if (writes.length === 0) { res.json({ ok: true, written: 0 }); return; }
    let okCount = 0;
    for (const [reg, val] of writes) {
      const r = await writeTritonReg(slot, reg, val);
      if (r.ok) okCount++;
    }
    res.json({ ok: okCount === writes.length, written: okCount, total: writes.length });
  });

  /** POST /iot/triton/:slot/failures  body: { suctionP?: {mode,delaySec}, ... } */
  router.post('/triton/:slot/failures', async (req: Request, res: Response) => {
    const slot = parseInt(req.params.slot, 10);
    const f = req.body ?? {};
    // Channel order MUST match getTritonRegister: suctionP, dischargeP, oilP,
    //   suctionT, dischargeT, llsT, ambientT.
    const channels = ['suctionP','dischargeP','oilP','suctionT','dischargeT','llsT','ambientT'];
    const writes: Array<[number, number]> = [];
    channels.forEach((key, idx) => {
      const ch = f[key];
      if (!ch || typeof ch !== 'object') return;
      const base = 480 + idx * 2;
      if (Number.isFinite(+ch.mode))     writes.push([base,     (+ch.mode) & 0xFFFF]);
      if (Number.isFinite(+ch.delaySec)) writes.push([base + 1, (+ch.delaySec) & 0xFFFF]);
    });
    if (writes.length === 0) { res.json({ ok: true, written: 0 }); return; }
    let okCount = 0;
    for (const [reg, val] of writes) {
      const r = await writeTritonReg(slot, reg, val);
      if (r.ok) okCount++;
    }
    res.json({ ok: okCount === writes.length, written: okCount, total: writes.length });
  });

  /** POST /iot/triton/:slot/ack  body: { code: string } | { all: true } */
  router.post('/triton/:slot/ack', async (req: Request, res: Response) => {
    const slot = parseInt(req.params.slot, 10);
    // Per-alarm ack isn't representable in a single register — and the real
    // hardware UX is "ack everything that's currently latched", so we always
    // map ack to the ack-all command (reg 334 = 1).  The body shape is kept
    // for backward compat with the popup which can send { all: true } or
    // { code: 'XYZ' }.
    const result = await writeTritonReg(slot, 334, 1);
    res.status(result.ok ? 200 : 502).json(result);
  });

  // (Apr 29 2026 mbtcp migration) /triton/:slot/safety/di REMOVED.
  // The route forwarded a DI-toggle into the orbit-simulator's /api/di
  // endpoint to fake phase-loss / pressure-switch / overload contacts.
  // Real hardware doesn't take injected DI — those contacts wire to
  // physical inputs. The TritonHotspotPopup's safety-section inject
  // buttons should be retired or moved into the sensor-injector tool.

  /** POST /iot/triton/:slot/safety/reset — clear latched safety lockout
   *  bits (HP / Comp OL / Run-Prove) by writing the TRITON ack_command
   *  register (HR 542 = 1) per orbit_triton.h. The LP firmware clears
   *  active alarms, the safe_off_mask, and per-sensor trip latches on
   *  this pulse — re-fire requires a fresh trip condition. */
  router.post('/triton/:slot/safety/reset', async (req: Request, res: Response) => {
    const slot = parseInt(req.params.slot, 10);
    if (!Number.isFinite(slot) || slot < 0) {
      return res.status(400).json({ ok: false, error: 'bad slot' });
    }
    // Phase 4b Sub-phase 2: route through the existing TritonRegWrite
    // envelope (tag 125) instead of opening Modbus TCP from the Pi5.
    const result = await writeTritonReg(slot, 542, 1);
    res.status(result.ok ? 200 : 503).json(result);
  });

  // ─── Orbit module status ─────────────────────────────────────────────
  // Both endpoints below derive from `dataCache.getOrbitBoards()` which
  // is populated by firmware OrbitStatus / OrbitDiscovery pushes
  // (envelope tags 120 / 121 via novaAdapter). The bridge no longer
  // probes orbit-simulator REST `/api/status`.

  // (Phase 4b 2026-06-01) GET /iot/orbit/status REMOVED. The route had
  // ZERO consumers in `constellation-ui/src/` (audited 2026-06-01) and
  // its FC01/FC02/FC03 probes were the last reads needing the bridge
  // to open Modbus TCP from the Pi5. DI/DO/AO live state is carried
  // by `OrbitBoardStatus` fields 16-23 today — the LP emits them
  // ([Nova_Firmware/lp_am2434/main.c:4328-4353]); future consumers
  // should subscribe to the `OrbitStatus` proto stream instead.

  /** GET /iot/orbit/boards — lightweight list. Includes `connected`
   *  and (when known) firmware health hints (`commErrors`, `estopActive`,
   *  `safeMode`) so the orbit topology page can flag degraded boards
   *  without fetching the full /status payload. */
  router.get('/orbit/boards', (_req: Request, res: Response) => {
    const boards = dataCache.getOrbitBoards().map(b => {
      const row: Record<string, unknown> = {
        slot: b.slot,
        dipswitch_id: b.dipswitchId ?? (b.slot + 2),
        connected: b.connected,
      };
      if (b.commErrors  !== undefined) row.commErrors  = b.commErrors;
      if (b.estopActive !== undefined) row.estopActive = b.estopActive;
      if (b.safeMode    !== undefined) row.safeMode    = b.safeMode;
      return row;
    });
    res.json({ boards });
  });

  // ─── Orbit topology / role assignment ────────────────────────────────
  // GET /iot/orbits → list of installed orbits with their assigned role.
  //
  // dataCache.orbitBoards is populated from firmware OrbitStatus pushes
  // (envelope tag 120) via novaAdapter. The bridge no longer probes the
  // orbit simulator REST API — the LP AM2434 firmware is the sole
  // Modbus TCP client to the orbit boards. Role is sourced from
  // firmware Settings.OrbitRole[] and overridden by POST /iot/orbits/role.
  router.get('/orbits', (_req: Request, res: Response) => {
    const boards = dataCache.getOrbitBoards();
    res.json({
      orbits: boards.map(b => {
        // Phase 4b Sub-1 audit follow-up (2026-06-02): include the
        // OrbitBoardStatus fields 16-23 that the LP has emitted since
        // the April 2026 LP-I/O extension but the bridge was dropping
        // on the floor. Each field is independently optional — when
        // the LP omits one on the wire (proto3 zero-suppression), it
        // stays `undefined` here and is filtered out below so the
        // response stays compact for slots that haven't reported.
        const row: Record<string, unknown> = {
          slot: b.slot,
          dipswitchId: b.dipswitchId ?? (b.slot + 2),  // matches firmware ORBIT_IP scheme
          connected: b.connected,
          role: b.role,                       // 0=UNASSIGNED 1=STORAGE 2=DOOR 3=REFRIG
          aoEquip: b.aoEquip ?? [0, 0],
        };
        if (b.digitalInputs      !== undefined) row.digitalInputs      = b.digitalInputs;
        if (b.digitalOutputs     !== undefined) row.digitalOutputs     = b.digitalOutputs;
        if (b.dc24vOutputs       !== undefined) row.dc24vOutputs       = b.dc24vOutputs;
        if (b.analogOutputsX10   !== undefined) row.analogOutputsX10   = b.analogOutputsX10;
        if (b.vfdActivitySecs    !== undefined) row.vfdActivitySecs    = b.vfdActivitySecs;
        if (b.sensorActivitySecs !== undefined) row.sensorActivitySecs = b.sensorActivitySecs;
        if (b.outputLabels       !== undefined) row.outputLabels       = b.outputLabels;
        if (b.inputLabels        !== undefined) row.inputLabels        = b.inputLabels;
        return row;
      }),
    });
  });

  // GET /iot/orbits/sensor-banks → raw HR[hr_base..hr_base+N-1] per orbit.
  // Mirrors firmware OrbitSensorBank pushes (envelope tag 124); no scaling
  // or unit conversion. Bridge is a transparent gateway here — UI applies
  // any sensor-channel decoding via IoDefinition metadata.
  //
  // Phase 4b 2026-06-01: one orbit may now publish multiple banks
  // (sensor block at hr_base=200 + role window at 300/400). The
  // response flattens to one entry per (slot, hrBase) tuple.
  router.get('/orbits/sensor-banks', (_req: Request, res: Response) => {
    if (!novaStore) { res.json({ banks: [] }); return; }
    const all = novaStore.getAllOrbitBanks();
    if (all.length === 0) { res.json({ banks: [] }); return; }
    const now = Date.now();
    res.json({
      banks: all.map(b => ({
        slot:   b.slot,
        hrBase: b.hrBase,
        values: b.values,
        seq:    b.seq,
        ageMs:  now - b.ts,
      })),
    });
  });

  // POST /iot/orbits/role  body: { slot:number, role:number, zoneId?:number, refrigStage?:number }
  // Forwards an OrbitRoleAssign message to firmware. Firmware persists the
  // pick to OSPI Settings.OrbitRole[] and re-emits OrbitStatus with the
  // updated role on the next poll.
  router.post('/orbits/role', async (req: Request, res: Response) => {
    const slot = Number(req.body?.slot);
    const role = Number(req.body?.role);
    if (!Number.isInteger(slot) || slot < 0 || slot >= NOVA_MAX_ORBITS) {
      return res.status(400).json({ error: 'slot must be 0..15' });
    }
    if (!Number.isInteger(role) || role < 0 || role > 3) {
      return res.status(400).json({ error: 'role must be 0..3 (0=UNASSIGNED 1=STORAGE 2=DOOR 3=REFRIG)' });
    }
    if (!serialBridge.assignOrbitRole) {
      return res.status(503).json({ error: 'firmware bridge does not support orbit role assignment' });
    }
    try {
      await serialBridge.assignOrbitRole(slot, role, {
        zoneId: req.body?.zoneId,
        refrigStage: req.body?.refrigStage,
      });
      // No optimistic cache write — firmware OrbitStatus push (envelope
      // tag 120, OrbitBoardStatus.role = field 10) is the only source of
      // truth.  CONTROLLER LP persists to OSPI in `bridge_rx_callback`
      // (Nova_Firmware/lp_am2434/main.c) via LpSettings_SetOrbitRole +
      // LpSettings_Save, then re-emits OrbitStatus on the next ~5 s
      // cadence.  If the role doesn't appear in /iot/orbits within ~10 s,
      // the firmware save failed — chase it on UART, do not paper over
      // here. (See agristar-principles.md: bridge never fills in values.)
      res.json({ ok: true, slot, role });
    } catch (e: any) {
      res.status(502).json({ error: e?.message ?? 'role assign failed' });
    }
  });

  // POST /iot/orbits/aoequip  body: { slot:number, channel:number, equip:number }
  // Programs which equipment a single orbit AO drives. Persists to
  // firmware OSPI Settings.AoEquip[slot][channel]. Used by the Level 2
  // PWM 4-20 mA Output Setup page. equip values match ao_equip_t in
  // Nova_Firmware/Platform/nova_fan_output.h:
  //   0 = AO_EQUIP_UNUSED   (default — AO is left at 0 V)
  //   1 = AO_EQUIP_FAN_SPEED (mirror PwmChannel[PWM_FAN].Output as 0..10000)
  router.post('/orbits/aoequip', async (req: Request, res: Response) => {
    const slot    = Number(req.body?.slot);
    const channel = Number(req.body?.channel);
    const equip   = Number(req.body?.equip);
    if (!Number.isInteger(slot) || slot < 0 || slot >= NOVA_MAX_ORBITS) {
      return res.status(400).json({ error: `slot must be 0..${NOVA_MAX_ORBITS - 1}` });
    }
    if (!Number.isInteger(channel) || channel < 0 || channel > 1) {
      return res.status(400).json({ error: 'channel must be 0..1' });
    }
    if (!Number.isInteger(equip) || equip < 0 || equip > 255) {
      return res.status(400).json({ error: 'equip must be 0..255 (ao_equip_t)' });
    }
    if (!serialBridge.assignAoEquip) {
      return res.status(503).json({ error: 'firmware bridge does not support AO equipment assignment' });
    }
    try {
      await serialBridge.assignAoEquip(slot, channel, equip);
      // No optimistic cache write — firmware re-emits OrbitStatus with
      // the updated aoEquip[] within ~5 s.  Same rule as the role
      // handler above: bridge never fills in values, firmware polling
      // is the single source of truth.
      res.json({ ok: true, slot, channel, equip });
    } catch (e: any) {
      res.status(502).json({ error: e?.message ?? 'AO equip assign failed' });
    }
  });

  // ─── System lifecycle ────────────────────────────────────────────────
  // POST /iot/system/reboot → SystemCmd { cmd_type=CMD_REBOOT_SOC=50 }.
  //
  // Firmware acks the request, then calls
  // Sciclient_pmDeviceReset(SystemP_WAIT_FOREVER) after a brief UART
  // FIFO drain. DMSC tears the SoC down, ROM re-runs, SBL loads OSPI
  // 0x80000 — same path the JTAG auto-flasher uses (proven 2026-05-02).
  // The bridge's NovaSerialBridge auto-reconnect picks the firmware
  // back up once it re-handshakes (~30 s end-to-end).
  //
  // Used by:
  //   - Operator "Reboot Controller" action (when wired into the UI).
  //   - The OTA Activate primitive — orbitOtaPush.ts will call this
  //     after writing a new image to OSPI Bank B so the new bytes
  //     take effect without needing JTAG/operator presence. This is
  //     the single primitive that unlocks unattended Azure-pushed
  //     firmware updates.
  router.post('/system/reboot', async (_req: Request, res: Response) => {
    if (!serialBridge.rebootSoc) {
      return res.status(503).json({ error: 'firmware bridge does not support remote reboot' });
    }
    try {
      await serialBridge.rebootSoc();
      res.json({ ok: true, note: 'firmware acked CMD_REBOOT_SOC; SoC will warm-reset within ~50 ms' });
    } catch (e: any) {
      // Expected outcome: firmware resets so quickly that the Ack
      // either races the disconnect or is dropped — `sendCommand`
      // surfaces this as `Command timeout (seq=N, msg=82)`. We treat
      // that as success and let the bridge's reconnect logic confirm
      // the firmware came back.  Genuine transport errors (e.g. UART
      // not open, COBS encode failure) bubble through as 502.
      const msg = String(e?.message ?? '');
      if (msg.startsWith('Command timeout')) {
        return res.json({
          ok: true,
          note: 'reboot triggered (firmware reset before Ack landed); reconnect in progress',
        });
      }
      res.status(502).json({ error: msg || 'reboot request failed' });
    }
  });

  return router;
}
