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
import type { OrbitClient } from './orbitClient.js';
import type { NovaFwUpdateManager } from './novaFwUpdateManager.js';

/**
 * Minimal command surface the API layer needs from whatever speaks to the
 * controller.  In Constellation this is always the Nova `legacyShim` defined
 * in `index.ts`, which translates these legacy ASCII-style calls into
 * COBS+protobuf frames.  Kept as a structural interface so tests/mocks can
 * supply a stub without depending on the bridge implementation.
 */
export interface CommandBridge {
  sendPost(body: string): Promise<string> | string;
  sendCommand(tag: string, value: string): void;
}
import { getProfile } from './vfdRegisterMaps.js';
import { WARNING_KEYS, DEFAULT_WARNING_TEXT } from './warningTranslator.js';
import { saveConfig, loadConfig } from './simConfig.js';
import { getNovaId } from './dataCache.js';
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
import { computeDiff } from './auditDiff.js';

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

// ─── Orbit I/O config sync ────────────────────────────────────────────────

const ORBIT_API_PORT = parseInt(process.env.ORBIT_API_PORT ?? '9010', 10);

/**
 * Maximum number of Orbit boards the bridge supports.
 * Matches NOVA_MAX_ORBITS in firmware hal_orbit.h.
 */
const NOVA_MAX_ORBITS = 16;

/**
 * After an ioconfig save, resolve equipment names for each orbit board
 * and push them via POST /api/ioconfig so the orbit panel shows labels
 * like "Fan 1" instead of raw "DO1".
 *
 * Data model:
 *   outputConfig[pid] = equipmentId   (pid = board*12 + pin, pin 0-based)
 *   inputConfig[pid]  = equipmentId
 *   ioNames[equipmentId] = "Name:Mode:IO:Renamable:EquipID"
 *
 * Orbit boards are mapped by discovery slot:
 *   Slot 0 (MAIN)   â†’ orbit ID 2 â†’ localhost:ORBIT_API_PORT
 *   Slot 1 (EXP_1)  â†’ orbit ID 3 â†’ localhost:ORBIT_API_PORT+1
 *   ...
 *   Slot N           â†’ orbit ID N+2 â†’ localhost:ORBIT_API_PORT+N
 *
 * The board_count parameter (from IoConfig.board_count or fallback 3)
 * determines how many boards to push labels to.
 */
export async function syncIoConfigToOrbit(body: any, dataCache: DataCache): Promise<void> {
  const outputConfig: string[] = Array.isArray(body?.outputConfig) ? body.outputConfig : [];
  const inputConfig: string[] = Array.isArray(body?.inputConfig) ? body.inputConfig : [];

  // Resolve ioNames from cache (equipment ID â†’ name)
  const ioNamesRaw = dataCache.getByVarName('IoNamesData')?.value ?? '';
  const ioNames = ioNamesRaw ? ioNamesRaw.split(',') : [];

  // Determine board count: prefer explicit value from body or IoConfig,
  // fall back to legacy 3-board default.
  const boardCount = Math.min(
    parseInt(body?.boardCount, 10) || 3,
    NOVA_MAX_ORBITS
  );

  function getEquipName(equipId: string): string {
    const id = parseInt(equipId, 10);
    if (id < 0 || id >= ioNames.length) return '';
    const parts = ioNames[id]?.split(':');
    return parts?.[0] ?? '';
  }

  // Push labels for each board (10 outputs/inputs per orbit)
  for (let board = 0; board < boardCount; board++) {
    const outputLabels: string[] = [];
    const inputLabels: string[] = [];

    // PID range for this board: board*12+1 .. board*12+10 (10 I/O points)
    // Firmware PID is 1-based per board (PID 0 is unused), so DO1=PID 1, DO2=PID 2, etc.
    for (let pin = 0; pin < 10; pin++) {
      const pid = board * 12 + pin + 1;
      const outEqId = outputConfig[pid] ?? '-1';
      const inEqId = inputConfig[pid] ?? '-1';
      outputLabels.push(outEqId !== '-1' ? getEquipName(outEqId) : '');
      inputLabels.push(inEqId !== '-1' ? getEquipName(inEqId) : '');
    }

    const port = ORBIT_API_PORT + board;
    try {
      const resp = await fetch(`http://localhost:${port}/api/ioconfig`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ outputLabels, inputLabels }),
      });
      if (resp.ok) {
        const assigned = outputLabels.filter(Boolean).length + inputLabels.filter(Boolean).length;
        console.log(`[API] Orbit board ${board} (port ${port}): pushed ${assigned} I/O labels`);
      }
    } catch {
      // Orbit simulator may not be running for this board â€” that's fine
    }
  }
}

export function createApiRoutes(dataCache: DataCache, serialBridge: CommandBridge, upgradeManager?: UpgradeManager, logDataStore?: NovaLogDataStore, vfdClient?: VFDClient, fwUpdateManager?: NovaFwUpdateManager, orbitClient?: OrbitClient): Router {
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

  // Temperature conversion helper â€” firmware sends sensor values in Â°C via serial.
  // Convert to display unit for all REST responses.
  // Use board type (from AnalogAllData) to determine if a sensor is a temperature.
  // Board type 3 = Temperature board, Board type 1 = Humidity board.
  const getUseFahrenheit = (): boolean => {
    const entry = dataCache.getByVarName('P2BasicSetupData');
    if (!entry?.value) return true;
    return entry.value.split(',')[1] !== '1';
  };
  /** Given a sensor SID, determine if it's on a temperature board. */
  const isTempSensorBySid = (sid: number): boolean => {
    const analogRaw = dataCache.getByVarName('AnalogAllData')?.value;
    if (!analogRaw) return false;
    const parts = analogRaw.split(',');
    const boardIdx = Math.floor(sid / 4);
    // AnalogAllData stride 5: Addr, BoardType, Label, Version, Disabled
    for (let i = 0; i + 4 < parts.length; i += 5) {
      const addr0 = parseInt(parts[i], 10) - 1;
      if (addr0 === boardIdx) {
        const boardType = parseInt(parts[i + 1], 10);
        return boardType === 3; // boardType 3 = Temperature
      }
    }
    return false;
  };
  const convertSensorTemp = (val: string, sid: number): string => {
    if (!isTempSensorBySid(sid)) return val;
    if (!val || val === '--' || val === 'dis') return val;
    const n = parseFloat(val);
    if (isNaN(n)) return val;
    return getUseFahrenheit() ? (n * 9 / 5 + 32).toFixed(1) : val;
  };

  const WARN_NEWBOARD = WARNING_KEYS.indexOf('WARN_NEWBOARD');

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

  function buildLegacyAnalogBoardPayload(boardAddress1 = 1): string[] {
    const analogRaw = dataCache.getByVarName('AnalogAllData')?.value ?? '';
    const sensorRaw = dataCache.getByVarName('SensorListData')?.value ?? '';
    if (!analogRaw || !sensorRaw) return ['0'];

    const boards = analogRaw.split(',');
    const boardStride = 5;
    let boardBase = -1;

    for (let i = 0; i + 4 < boards.length; i += boardStride) {
      if (Number(boards[i]) === boardAddress1) {
        boardBase = i;
        break;
      }
    }

    if (boardBase < 0) return ['0'];

    const payload = boards.slice(boardBase, boardBase + boardStride);
    const boardIdx0 = boardAddress1 - 1;

    const sensorParts = sensorRaw.split(',');
    for (let sensorIndex = 0; sensorIndex < 4; sensorIndex++) {
      const sid = boardIdx0 * 4 + sensorIndex;
      let type = '255';
      let label = '';
      let offset = '0.0';
      let disabled = '0';
      let value = '--';

      for (let i = 0; i + 5 < sensorParts.length; i += 6) {
        if (Number(sensorParts[i + 1]) === sid) {
          label = sensorParts[i] ?? '';
          type = sensorParts[i + 2] ?? '255';
          value = sensorParts[i + 3] ?? '--';
          offset = sensorParts[i + 4] ?? '0.0';
          disabled = sensorParts[i + 5] === '1' ? '1' : '0';
          break;
        }
      }

      if (disabled === '1') value = 'dis';
      payload.push(type, label, offset, disabled, value);
    }

    return payload;
  }

  // RS485 control-plane sync helper.
  // Keep this for simulator control flows (e.g. analog save/find-boards),
  // but do NOT call it from analog GET endpoints so runtime display data
  // remains ARM-sourced like production.
  async function syncAnalogCacheFromRs485(): Promise<boolean> {
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

  const AS2_EXCLUDES = new Set([
    'WARN_REFRIG_AS1',
    'WARN_HUMID1_AS1',
    'WARN_HUMID2_AS1',
    'WARN_AUX_AS1',
    'WARN_AUX1_AS1',
    'WARN_AUX2_AS1',
    'WARN_LIGHTS1_AS1',
    'WARN_LIGHTS2_AS1',
  ]);

  const AGRISTAR_EXCLUDES = new Set([
    'WARN_REFRIG_DEFROST',
    'WARN_REFRIG_PWM',
    'WARN_REFRIG_STAGE',
    'WARN_HUMIDIFIER',
    'WARN_AUX',
    'WARN_SYSCONFIG_EQ',
    'WARN_NO_OUTPUT',
    'WARN_EXPANSIONBOARD',
    'WARN_LIGHTS',
  ]);

  const NON_ALERT_KEYS = new Set(['WARN_ALARMS_FILE', 'WARN_EQUIPDESC_FILE']);

  const getBoardType = (): 'AS2' | 'AGRISTAR' => {
    const raw = (dataCache.getByVarName('BoardType')?.value ?? 'AS2').toUpperCase();
    return raw.includes('AS2') ? 'AS2' : 'AGRISTAR';
  };

  const getIncludedAlertIndices = (): number[] => {
    const board = getBoardType();
    const excludes = board === 'AS2' ? AS2_EXCLUDES : AGRISTAR_EXCLUDES;
    const indices: number[] = [];

    for (let i = 0; i < WARNING_KEYS.length; i++) {
      const key = WARNING_KEYS[i];
      if (NON_ALERT_KEYS.has(key)) continue;
      if (excludes.has(key)) continue;
      indices.push(i);
    }
    return indices;
  };

  const getFullAlertBits = (): string[] => {
    const raw = dataCache.getByVarName('AlertSetupData')?.value ?? '';
    const bitChars = raw.replace(/[^01]/g, '').split('');
    const minLen = WARNING_KEYS.length;
    while (bitChars.length < minLen) bitChars.push('0');
    return bitChars;
  };

  const getFilteredAlertBits = (): string[] => {
    const full = getFullAlertBits();
    const included = getIncludedAlertIndices();
    return included.map(i => full[i] ?? '0');
  };

  const getFilteredAlertLabels = (): string[] => {
    const labels: string[] = [];
    const included = getIncludedAlertIndices();
    for (const idx of included) {
      if (idx === WARN_NEWBOARD) labels.push('group2');
      const key = WARNING_KEYS[idx] ?? `WARN_UNKNOWN_${idx}`;
      labels.push(DEFAULT_WARNING_TEXT[key] ?? key);
    }
    return labels;
  };

  // â”€â”€â”€ Polling endpoints (kept for backward compatibility) â”€â”€â”€
  // These endpoints are hit by the PollingClient every 3s.
  // Once the WebSocket client is deployed, these become fallback-only.

  router.get('/ws/frontmatter-data', (_req: Request, res: Response) => {
    res.json(dataCache.buildFrontmatterData());
  });

  router.get('/ws/header-data', (_req: Request, res: Response) => {
    res.json(dataCache.buildHeaderData());
  });

  router.get('/ws/tcpip-data', (_req: Request, res: Response) => {
    res.json(dataCache.buildTcpIpData());
  });

  // â”€â”€ Identity endpoint â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  // Returns this Nova's persistent UUID and panel name.
  // Other Novas query this during discovery so groups survive DHCP IP changes.
  router.get('/identity', (_req: Request, res: Response) => {
    const panelName = dataCache.getByVarName('PanelName')?.value || 'Agristar Panel';
    res.json({ novaId: getNovaId(), panelName });
  });

  router.get('/ws/upgrade-data', (_req: Request, res: Response) => {
    if (upgradeManager) {
      res.json(upgradeManager.getStatus());
    } else {
      res.json({ UpgradeStatus: '', UpgradingSoftware: false, isEmpty: true });
    }
  });

  router.get('/ws/network-data', (_req: Request, res: Response) => {
    res.json(dataCache.buildPageData('network') ?? {});
  });

  router.get('/ws/download-data', (_req: Request, res: Response) => {
    res.json({ current: undefined, total: undefined });
  });

  router.get('/ws/equipment-data', (_req: Request, res: Response) => {
    const eqStatus = dataCache.getEquipStatus();
    const pwmRaw = dataCache.getByVarName('PWMData')?.value ?? '';
    const ioNamesRaw = dataCache.getByVarName('IoNamesData')?.value ?? '';
    const auxRaw = dataCache.getByVarName('EquipAuxData')?.value ?? '';
    const miscRaw = dataCache.getByVarName('MiscData')?.value ?? '';
    const basicRaw = dataCache.getByVarName('P2BasicSetupData')?.value ?? '';
    const basicArr = basicRaw ? basicRaw.split(',') : [];
    res.json({
      eqStatus,
      pwmConfig: pwmRaw ? pwmRaw.split(',') : ['0'],
      outputConfig: dataCache.getEqIndexedOutputConfig(),
      ioNames: ioNamesRaw ? ioNamesRaw.split(',') : [],
      auxSwitches: auxRaw ? auxRaw.split(',') : [],
      miscData: miscRaw ? miscRaw.split(',') : ['0'],
      systemMode: basicArr[4] ?? '0',
    });
  });

  // â”€â”€â”€ Specific page endpoints (must come BEFORE the generic loop) â”€â”€â”€

  // Burner data â€” used by home page to check burner mode (returns array directly)
  router.get('/burner', (_req: Request, res: Response) => {
    const burnerRaw = dataCache.getByVarName('P2BurnerData')?.value
                   || dataCache.getByVarName('BurnerData')?.value
                   || '0,0,0,0,0,0,0';
    res.json(burnerRaw.split(','));
  });

  // Basic home data â€” used on first load to determine home page routing
  router.get('/basic/home', (_req: Request, res: Response) => {
    const basicRaw = dataCache.getByVarName('P2BasicSetupData')?.value ?? '';
    const parts = basicRaw.split(',');
    // parts[3] is the home page setting (e.g. 'mnMainData.htm')
    res.json({ data: parts[3] || 'mnMainData.htm' });
  });

  // Alert labels â€” used by alerts page alongside alerts data
  router.get('/alert/labels', (_req: Request, res: Response) => {
    res.json(getFilteredAlertLabels());
  });

  // Alerts data â€” normalize AlertSetup bitstring to one flag per visible alert
  router.get('/alerts', (_req: Request, res: Response) => {
    res.json(getFilteredAlertBits());
  });

  // â”€â”€â”€ Page data endpoints (GET /iot/<pageName>) â”€â”€â”€
  // Used by loadIotData() in +page.ts files during navigation

  // Legacy analog endpoint compatibility: return a full 25-field board payload
  // (Addr,Type,Label,Version,Disabled + 4x [Type,Label,Offset,Disabled,Value]).
  // Some runtime paths still request /iot/analog and expect this shape.
  router.get('/analog', async (_req: Request, res: Response) => {
    const analogAllRaw = dataCache.getByVarName('AnalogAllData')?.value ?? '';
    const firstBoardAddr = analogAllRaw ? Number(analogAllRaw.split(',')[0]) || 1 : 1;
    const payload = buildLegacyAnalogBoardPayload(firstBoardAddr);
    console.log(`[API] GET /iot/analog â†’ board ${firstBoardAddr}, payload length=${payload.length}`);
    res.set('Cache-Control', 'no-store, no-cache, must-revalidate');
    res.json(payload);
  });

  const pageNames = [
    'basic', 'pid', 'pwm', 'fanspeed', 'fanboost',
    'outside', 'humidifier', 'co2', 'ramp', 'misc', 'door', 'plensetup',
    'climacell', 'climacelltimes', 'lights', 'alerts', 'refrigeration',
    'master', 'ioconfig', 'service', 'failures1', 'failures2', 'log',
    'version', 'email', 'accounts', 'sensors', 'runtimes', 'date', 'network',
  ];

  for (const page of pageNames) {
    router.get(`/${page}`, (_req: Request, res: Response) => {
      const data = dataCache.buildPageData(page);
      if (data) {
        res.json(data);

        // When ioconfig is fetched, sync labels to orbit simulators
        if (page === 'ioconfig' && data.config) {
          syncIoConfigToOrbit(data.config, dataCache).catch(() => {});
        }
      } else {
        res.status(404).json({ error: `Unknown page: ${page}` });
      }
    });
  }

  // â”€â”€â”€ PID / sensor data endpoints â”€â”€â”€

  router.get('/pids', (_req: Request, res: Response) => {
    const pidData = dataCache.getByVarName('PidData');
    const sensorData = dataCache.getByVarName('SensorListData');
    res.json({
      PidData: pidData?.value ?? '',
      SensorListData: sensorData?.value ?? '',
    });
  });

  router.get('/sensors/all', (_req: Request, res: Response) => {
    const sensorData = dataCache.getByVarName('SensorListData');
    // Split sensor list into key-value pairs for the UI (stride 6: Label, SID, Type, Value, Offset, Disabled)
    const raw = sensorData?.value ?? '';
    const parts = raw.split(',');
    const result: Record<string, string> = {};
    for (let i = 0; i < parts.length; i++) {
      result[String(i)] = parts[i] ?? '';
    }
    res.json(result);
  });

  router.get('/sensors/unified', async (_req: Request, res: Response) => {
    // Parse SensorListData (stride 6: Label,SID,Type,Value,Offset,Disabled) into
    // the structured format the analog page's mergeSensorData() expects:
    //   SensorLabels:   stride 2 â†’ [SID, Label, ...]
    //   SensorValues:   stride 2 â†’ [SID, Value, ...]
    //   SensorSettings: stride 4 â†’ [SID, Type, Offset, Disabled, ...]
    const sensorData = dataCache.getByVarName('SensorListData');
    const raw = sensorData?.value ?? '';
    const parts = raw ? raw.split(',') : [];

    const SensorLabels: string[] = [];
    const SensorValues: string[] = [];
    const SensorSettings: string[] = [];

    for (let i = 0; i + 5 < parts.length; i += 6) {
      const label    = parts[i];
      const sid      = parts[i + 1];
      const type     = parts[i + 2];
      const value    = convertSensorTemp(parts[i + 3], parseInt(sid, 10));
      const offset   = parts[i + 4];
      const disabled = parts[i + 5];
      SensorLabels.push(sid, label);
      SensorValues.push(sid, value);
      SensorSettings.push(sid, type, offset, disabled);
    }

    const sensorCount = SensorLabels.length / 2;
    console.log(`[API] GET /iot/sensors/unified â†’ ${sensorCount} sensors`);
    res.set('Cache-Control', 'no-store, no-cache, must-revalidate');
    res.json({ SensorLabels, SensorValues, SensorSettings });
  });

  router.get('/analog/all', async (_req: Request, res: Response) => {
    // Return full 25-element arrays per board:
    //   [Addr, Type, Label, Version, Disabled,
    //    SenType1, SenLabel1, SenOffset1, SenDisabled1, SenValue1,
    //    ... Ã—4 sensors]
    // The deployed SvelteKit UI (uisvelte.service) expects this merged format
    // directly â€” it does NOT call /iot/sensors/unified separately.
    const analogData = dataCache.getByVarName('AnalogAllData');
    const analogRaw = analogData?.value ?? '';
    if (!analogRaw) {
      console.log('[API] GET /iot/analog/all â†’ empty (no AnalogAllData)');
      res.set('Cache-Control', 'no-store, no-cache, must-revalidate');
      res.json([]);
      return;
    }

    const sensorRaw = dataCache.getByVarName('SensorListData')?.value ?? '';
    const sensorParts = sensorRaw ? sensorRaw.split(',') : [];
    const analogParts = analogRaw.split(',');
    const boards: string[][] = [];

    for (let i = 0; i + 4 < analogParts.length; i += 5) {
      const boardAddr = Number(analogParts[i]) || 0;
      const boardIdx0 = boardAddr - 1;
      const board = analogParts.slice(i, i + 5);

      // Merge sensor data for this board's 4 sensors
      for (let s = 0; s < 4; s++) {
        const sid = boardIdx0 * 4 + s;
        let type = '255', label = '', offset = '0.0', disabled = '0', value = '--';

        // SensorListData stride 6: Label, SID, Type, Value, Offset, Disabled
        for (let j = 0; j + 5 < sensorParts.length; j += 6) {
          if (Number(sensorParts[j + 1]) === sid) {
            label    = sensorParts[j]     ?? '';
            type     = sensorParts[j + 2] ?? '255';
            value    = sensorParts[j + 3] ?? '--';
            offset   = sensorParts[j + 4] ?? '0.0';
            disabled = sensorParts[j + 5] === '1' ? '1' : '0';
            break;
          }
        }
        if (disabled === '1') value = 'dis';
        board.push(type, label, offset, disabled, convertSensorTemp(value, sid));
      }

      boards.push(board);
    }

    console.log(`[API] GET /iot/analog/all â†’ ${boards.length} boards (25-element merged)`);
    res.set('Cache-Control', 'no-store, no-cache, must-revalidate');
    res.json(boards);
  });

  router.get('/aux/all', (_req: Request, res: Response) => {
    // â”€â”€ Build the Auxiliary page response â”€â”€
    // The Svelte UI expects: { InputConfig, OutputConfig, IoNames, systemMode, allAux }
    // where allAux[] contains { auxProg: string[], rules: Rule[] } for each defined aux output.
    const getArr = (vn: string): string[] => {
      const v = dataCache.getByVarName(vn)?.value;
      return v ? v.split(',') : [];
    };
    const p2Basic = getArr('P2BasicSetupData');
    const systemMode = p2Basic[4] ?? '0';
    const InputConfig = dataCache.getEqIndexedInputConfig();
    const OutputConfig = dataCache.getEqIndexedOutputConfig();
    const IoNames = getArr('IoNamesData');

    // Collect all accumulated aux program entries
    const auxPrograms = dataCache.getAllAuxPrograms();
    const allAux: Array<{ auxProg: string[]; rules: any[] }> = [];
    let missingCount = 0;

    // EQ_AUX1=25 .. EQ_AUX8=32 â€” include entries for defined aux outputs
    for (let auxIdx = 0; auxIdx < 8; auxIdx++) {
      const eqId = 25 + auxIdx;
      const eqStr = String(eqId);
      // Only include if this aux output is assigned in OutputConfig
      if (eqId < OutputConfig.length && OutputConfig[eqId] !== '-1') {
        const raw = auxPrograms.get(eqStr);
        if (raw && !raw.includes('undefined')) {
          allAux.push(parseAuxProgram(raw));
        } else {
          // Defined in IO config but no program data cached â€” provide defaults
          allAux.push({ auxProg: [eqStr, '100', '1', '1'], rules: buildDefaultAuxRules() });
          missingCount++;
        }
      }
    }

    // If we have cached data for some but not all defined aux, trigger async
    // discovery by sending NextAux commands to the ARM to collect the rest.
    // The Svelte UI retries after 3.5s if allAux is empty, so on the second
    // call the accumulated data will be available.
    if (missingCount > 0 && auxPrograms.size > 0) {
      const lastKnownEq = [...auxPrograms.keys()].pop() ?? '24';
      discoverAuxPrograms(serialBridge, lastKnownEq, missingCount);
    }

    console.log(`[API] GET /iot/aux/all â†’ ${allAux.length} aux programs (${missingCount} uncached)`);
    res.json({ InputConfig, OutputConfig, IoNames, systemMode, allAux });
  });

  // â”€â”€â”€ Frontmatter (single GET) â”€â”€â”€

  router.get('/frontmatter', (_req: Request, res: Response) => {
    res.json(dataCache.buildFrontmatterData());
  });

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

  // Public-facing port for the UI. GellertHeader + getPort() consume { data }.
  router.get('/port', (_req: Request, res: Response) => {
    res.json({ data: process.env.BRIDGE_PUBLIC_PORT ?? '81' });
  });

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

  router.get('/log/sources', (_req: Request, res: Response) => {
    if (!logDataStore) { res.json([]); return; }
    res.json(logDataStore.getLogSources());
  });

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

  router.get('/user/download', async (_req: Request, res: Response) => {
    // Download user log data to file â€” requires GellertFileSystem (runs in RPi5 QEMU)
    // The bridge doesn't own file I/O; acknowledge receipt and let GFS handle it.
    res.status(200).send('OK');
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

  router.get('/activity/download', async (_req: Request, res: Response) => {
    // Download activity log data â€” same approach as user download
    res.status(200).send('OK');
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

  router.get('/avail/remote', async (_req: Request, res: Response) => {
    if (!logDataStore) {
      res.json({ availLabels: ['Remote'], availEquip: [1] });
      return;
    }
    try {
      // Fetch a small sample of activity data to get column structure
      const labels = await logDataStore.getActivityRemoteLabels('1', '20');
      res.json(labels);
    } catch (err: any) {
      console.error('[API] /avail/remote error:', err.message);
      res.json({ availLabels: ['Remote'], availEquip: [1] });
    }
  });

  // PID log data

  router.get('/pids', async (req: Request, res: Response) => {
    if (!logDataStore) { res.json({}); return; }
    // PID log retrieval would send PIDLogViewStart to ARM
    // Placeholder â€” PID logging is a level-2 diagnostic feature
    res.json({});
  });

  router.get('/logs', (_req: Request, res: Response) => {
    res.type('text').send('No logs available');
  });

  router.get('/settings/files', (_req: Request, res: Response) => {
    res.json({ files: [] });
  });

  router.get('/nodesetup', (_req: Request, res: Response) => {
    res.json({ nodes: [] });
  });

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

  router.get('/datainfo', (_req: Request, res: Response) => {
    if (!logDataStore) {
      res.json({
        database: { activityCount: 0, historyCount: 0, percentUsed: 0, startDate: new Date().toISOString() },
        sdcard: ['0','0','0','0','0','0','0','0','0','0'],
      });
      return;
    }
    res.json(logDataStore.getDataInfo());
  });

  router.get('/avail/equipment', async (_req: Request, res: Response) => {
    if (!logDataStore) {
      res.json({ availLabels: ['Equipment'], availEquip: [0] });
      return;
    }
    try {
      // Fetch a small sample of activity data to get column structure
      const labels = await logDataStore.getActivityEquipmentLabels('1', '20');
      res.json(labels);
    } catch (err: any) {
      console.error('[API] /avail/equipment error:', err.message);
      res.json({ availLabels: ['Equipment'], availEquip: [0] });
    }
  });

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

  // â”€â”€â”€ Page-save POST endpoints â”€â”€â”€
  // The SaveButton component POSTs JSON to /iot/<route>.
  // We translate the JSON body to the ARM firmware's expected POST format:
  //   Named-field tags:  ^tag=val&field2=val2&field3=val3$CRC!
  //   Positional tags:   ^tag=val0&_1=val1&_2=val2$CRC!
  // The firmware's ParsePost() splits on '&' to build PostTag[]/PostValue[] arrays,
  // then the registered StoreXxx() handler reads fields by name or by index.

  /** Extract array from body (handles flat array or object.key) */
  const arr = (b: any, key?: string): string[] => {
    if (key && b?.[key] && Array.isArray(b[key])) return b[key];
    if (Array.isArray(b)) return b;
    return [];
  };

  /** Build positional POST value: val0&_1=val1&_2=val2... */
  const pos = (...vals: (string | number | undefined)[]): string => {
    const filtered = vals.filter(v => v !== undefined) as (string | number)[];
    if (filtered.length === 0) return '';
    return filtered.map((v, i) => i === 0 ? String(v) : `_${i}=${v}`).join('&');
  };

  const pageSaveMap: Record<string, { tags: Array<{ armTag: string; extract: (body: any) => string }> }> = {

    // â”€â”€ Plenum setup: 3 ARM tags from 1 UI save â”€â”€
    // UI sends string[11]: [0]=tempSP, [1]=humidSP, [2]=humidRef, [3]=burnerTempSP,
    //   [4]=burnerThreshold, [5]=alarmTempLow, [6]=alarmMinLow, [7]=alarmTempHigh,
    //   [8]=alarmMinHigh, [9]=cureTempLow, [10]=cureTempHigh
    plensetup: { tags: [
      { armTag: 'p1Plenum', extract: b => {
        const a = arr(b);
        if (a.length === 0) return '';
        return [
          'AS2',
          `PlenumTempSet=${a[0]}`,
          `PlenumHumidSet=${a[1]}`,
          `selHumSetpointRef=${a[2] ?? '0'}`,
          `BurnerTempSet=${a[3] ?? '0'}`,
          `BurnerThreshold=${a[4] ?? '0'}`,
        ].join('&');
      }},
      { armTag: 'AlarmTempLow', extract: b => {
        const a = arr(b);
        return a.length > 8 ? pos(a[5], a[6], a[7], a[8]) : '';
      }},
      { armTag: 'CureTempLowLimit', extract: b => {
        const a = arr(b);
        // Only send if cure fields are present (onion mode)
        if (a.length <= 10 || !a[9] || !a[10]) return '';
        return pos(a[9], a[6], a[10], a[8]);
      }},
    ]},

    // â”€â”€ Outside air: ctrlMode + CureStartTemp (onion) â”€â”€
    // UI sends { outside: string[5], cure: string[4], dev: string }
    outside: { tags: [
      { armTag: 'ctrlMode', extract: b => {
        const o = arr(b, 'outside');
        if (o.length === 0) return '';
        return [
          o[4] ?? '0',
          `OutsideAirSet=${o[0]}`,
          `selAboveBelow=${o[1]}`,
          `selTempRef=${o[2]}`,
          `calcHumid=${o[3] ?? '0'}`,
        ].join('&');
      }},
      { armTag: 'CureStartTemp', extract: b => {
        const c = arr(b, 'cure');
        if (c.length < 4) return '';
        return pos(c[0], c[1], c[2], c[3]);
      }},
    ]},

    // â”€â”€ Failures 1: FanMode named fields â”€â”€
    // UI sends string[22] mapping 1:1 to firmware field order
    failures1: { tags: [{ armTag: 'FanMode', extract: b => {
      const a = arr(b);
      if (a.length < 20) return '';
      return [
        a[0],
        `FanTimer=${a[1]}`,
        `ClimacellMode=${a[2]}`,
        `ClimacellTimer=${a[3]}`,
        `RefridgeMode=${a[4]}`,
        `RefridgeTimer=${a[5]}`,
        `RefridgeRun=${a[6]}`,
        `RefrStagesMode=${a[7]}`,
        `RefrStagesTimer=${a[8]}`,
        `HumidifiersMode=${a[9]}`,
        `HumidifiersTimer=${a[10]}`,
        `AuxMode=${a[11]}`,
        `AuxTimer=${a[12]}`,
        `HeatMode=${a[13]}`,
        `HeatTimer=${a[14]}`,
        `CavityHeatMode=${a[15]}`,
        `CavityHeatTimer=${a[16]}`,
        `BurnerMode=${a[17]}`,
        `BurnerTimer=${a[18]}`,
        `LightsMode=${a[19]}`,
        `LightsTimer=${a[20]}`,
        `LightsUnits=${a[21] ?? '0'}`,
      ].join('&');
    }}]},

    // â”€â”€ Failures 2: OutAirMode named fields â”€â”€
    // UI sends string[13]
    failures2: { tags: [{ armTag: 'OutAirMode', extract: b => {
      const a = arr(b);
      if (a.length < 12) return '';
      return [
        a[0],
        `OutAirTimer=${a[1]}`,
        `OutHumidMode=${a[2]}`,
        `OutHumidTimer=${a[3]}`,
        `HighCo2Mode=${a[4]}`,
        `HighCo2Timer=${a[5]}`,
        `Co2Setpt=${a[6]}`,
        `LowHumidMode=${a[7]}`,
        `LowHumidTimer=${a[8]}`,
        `LowHumidSet=${a[9]}`,
        `PlenSenMode=${a[10]}`,
        `PlenSenTimer=${a[11]}`,
        `PlenSenDiff=${a[12] ?? '5.0'}`,
      ].join('&');
    }}]},

    // â”€â”€ Misc: p1Misc named fields with AS2 prefix â”€â”€
    // UI sends string[] (miscData): [0]=refrMode, [1]=defrostInterval, [2]=defrostTime,
    //   [3]=tempThresh, [4]=cavityCtrl, [5]=legacy, [6]=cavityDiff, [7]=dutyCycle/sensor,
    //   [8]=kbPref, [9]=locale, [10]=cavityTarget, [11]=reserved, [12]=enthalpyOff
    misc: { tags: [{ armTag: 'p1Misc', extract: b => {
      const a = arr(b, 'miscData');
      if (a.length === 0) return '';
      return [
        'AS2',
        `selRefrMode=${a[0]}`,
        `defrostInterval=${a[1]}`,
        `defrostTime=${a[2]}`,
        `tempThresh=${a[3]}`,
        `selCtrlMode=${a[10] ?? '0'}`,
        `selCavityCtrl=${a[4]}`,
        `cavityDiff=${a[6] ?? '0'}`,
        `cavityDutyCycle=${a[7] ?? '0'}`,
        `selCavityCtrlSensor=${a[7] ?? '0'}`,
        `kbPref=${a[8] ?? '0'}`,
        `cavStandbyOn=${a[13] ?? '0'}`,
      ].join('&');
    }}]},

    // â”€â”€ Basic setup: StorageName named fields â”€â”€
    // UI sends string[11]: [0]=name, [1]=tempType, [3]=homePage, [4]=mode,
    //   [7]=multiView, [8]=password(encrypted), [9]=loginSecure, [10]=animations
    basic: { tags: [{ armTag: 'StorageName', extract: b => {
      const a = arr(b);
      if (a.length === 0) return '';
      return [
        a[0],
        `TempType=${a[1] ?? '0'}`,
        `HomePage=${a[3] ?? ''}`,
        `SystemMode=${a[4] ?? '0'}`,
        `MultiView=${a[7] ?? '0'}`,
        `dlr0=${a[8] ?? ''}`,
        `loginSecure=${a[9] ?? '0'}`,
        `Animations=${a[10] ?? '0'}`,
      ].join('&');
    }}]},

    // â”€â”€ Refrigeration: p2Refrigeration named fields with AS2 prefix â”€â”€
    // UI sends string[23]: stage pairs + PIDU + purge mode/threshold + stages 7-8
    refrigeration: { tags: [{ armTag: 'p2Refrigeration', extract: b => {
      const a = arr(b);
      if (a.length === 0) return '';
      const parts = [
        'AS2',
        `Stage1On=${a[0]}`, `Stage1Off=${a[1]}`,
        `Stage2On=${a[2]}`, `Stage2Off=${a[3]}`,
        `Stage3On=${a[4]}`, `Stage3Off=${a[5]}`,
        `Stage4On=${a[6]}`, `Stage4Off=${a[7]}`,
        `Stage5On=${a[8]}`, `Stage5Off=${a[9]}`,
        `Stage6On=${a[10]}`, `Stage6Off=${a[11]}`,
        `PRefrValue=${a[12]}`, `IRefrValue=${a[13]}`,
        `DRefrValue=${a[14]}`, `URefrValue=${a[15]}`,
        `RefrigerationPurge=${a[16] ?? '0'}`,
        `PurgeThreshold=${a[17] ?? '0'}`,
      ];
      if (a[19] !== undefined) { parts.push(`Stage7On=${a[19]}`, `Stage7Off=${a[20]}`); }
      if (a[21] !== undefined) { parts.push(`Stage8On=${a[21]}`, `Stage8Off=${a[22]}`); }
      return parts.join('&');
    }}]},

    // â”€â”€ Ramp rate: updTemp named fields â”€â”€
    // UI sends string[] (rate): [0]=rate, [1]=updateHours, [2]=tempDiff, [3]=tempRef, [4]=targetTemp
    ramp: { tags: [{ armTag: 'updTemp', extract: b => {
      const a = arr(b, 'rate');
      if (a.length === 0) return '';
      return [
        a[0],
        `rampUpdateHours=${a[1]}`,
        `rampTempDiff=${a[2] ?? '0'}`,
        `selTemp=${a[3] ?? '0'}`,
        `targetTemp=${a[4] ?? '0'}`,
      ].join('&');
    }}]},

    // â”€â”€ Door: PAirValue positional â”€â”€
    // UI sends string[6]: P, I, D, U, actuatorTime, coolAirCycle
    door: { tags: [{ armTag: 'PAirValue', extract: b => {
      const a = arr(b);
      return a.length >= 6 ? pos(a[0], a[1], a[2], a[3], a[4], a[5]) : '';
    }}]},

    // â”€â”€ Service: dealerName positional â”€â”€
    // UI sends string[4]: dealerName, dealerPhone, techName, techPhone
    service: { tags: [{ armTag: 'dealerName', extract: b => {
      const a = arr(b);
      return a.length >= 4 ? pos(a[0], a[1], a[2], a[3]) : '';
    }}]},

    // â”€â”€ Burner: selBurnerMode positional, mode-dependent â”€â”€
    // UI sends string[10]: [0]=on%, [1]=low%, [2]=P, [3]=I, [4]=D, [5]=U,
    //   [6]=mode, [7]=manualOut, [8]=altitude, [9]=altUnit
    burner: { tags: [{ armTag: 'selBurnerMode', extract: b => {
      const a = arr(b);
      if (a.length < 10) return '';
      const mode = parseInt(a[6]) || 0;
      if (mode === 0) return pos(a[6], a[8], a[9]);
      if (mode === 1) return pos(a[6], a[7], a[8], a[9]);
      // Economy (2) or Max cure (3): mode, on, low, P, I, D, U, altitude, altUnit
      return pos(a[6], a[0], a[1], a[2], a[3], a[4], a[5], a[8], a[9]);
    }}]},

    // â”€â”€ Fan boost: selBoostMode positional, mode-dependent â”€â”€
    // UI sends string[5]: [0]=mode, [1]=speed, [2]=hours, [3]=duration, [4]=outsideTemp
    fanboost: { tags: [{ armTag: 'selBoostMode', extract: b => {
      const a = arr(b);
      if (a.length === 0) return '';
      const mode = parseInt(a[0]) || 0;
      if (mode === 0) return pos(a[0]);
      if (mode === 1) return pos(a[0], a[1], a[4], a[2], a[3]); // temp: mode,speed,temp,interval,duration
      return pos(a[0], a[1], a[2], a[3]); // runtime: mode,speed,interval,duration
    }}]},

    // â”€â”€ Fan speed: maxFanSpeed positional â”€â”€
    // UI sends string[] (speed): [0]=max, [1]=min, [2]=refrig, [3]=recirc,
    //   [4]=updatePeriod, [5]=tempDiff, [6]=tempRef1, [7]=tempRef2
    fanspeed: { tags: [{ armTag: 'maxFanSpeed', extract: b => {
      const a = arr(b, 'speed');
      if (a.length < 5) return '';
      return pos(a[0], a[1], a[2], a[3], a[4], a[5] ?? '0', a[6] ?? '0', a[7] ?? '255');
    }}]},

    // â”€â”€ CO2 purge: selPurgeMode positional, mode-dependent â”€â”€
    // UI sends string[8]: [0]=mode, [1]=minTemp, [2]=maxTemp, [3]=duration,
    //   [4]=hoursSince, [5]=fanOut, [6]=doorOut, [7]=co2Setpoint
    co2: { tags: [{ armTag: 'selPurgeMode', extract: b => {
      const a = arr(b);
      if (a.length === 0) return '';
      const mode = parseInt(a[0]) || 0;
      if (mode === 0) return pos(a[0]);
      if (mode === 1) {
        // Manual: mode, cycleTime(hoursSince), minTemp, maxTemp, duration, fanOut, doorOut
        return pos(a[0], a[4], a[1], a[2], a[3], a[5], a[6]);
      }
      // Auto: mode, co2Set, minTemp, maxTemp, duration, fanOut, doorOut
      return pos(a[0], a[7], a[1], a[2], a[3], a[5], a[6]);
    }}]},

    // â”€â”€ Humidifier: selHumidType positional â”€â”€
    // UI sends string[8]: [0]=index, [1]=mode, [2-7]=duty cycles
    humidifier: { tags: [{ armTag: 'selHumidType', extract: b => {
      const a = arr(b, 'control');
      if (a.length === 0) return '';
      return pos(...a);
    }}]},

    // â”€â”€ Climacell: ClimacellEff positional â”€â”€
    // UI sends string[7]: efficiency, altitude, altUnit, P, I, D, U
    climacell: { tags: [{ armTag: 'ClimacellEff', extract: b => {
      const a = arr(b);
      return a.length >= 7 ? pos(a[0], a[1], a[2], a[3], a[4], a[5], a[6]) : '';
    }}]},

    // â”€â”€ ClimaCell times: comma-delimited, sendPost converts to '+' for transport â”€â”€
    climacelltimes: { tags: [{ armTag: 'climacellTimes', extract: b => {
      if (typeof b === 'object' && !Array.isArray(b)) {
        return Array.from({ length: 48 }, (_, i) => b[String(i)] ?? '1').join(',');
      }
      return Array.isArray(b) ? b.slice(0, 48).join(',') : '';
    }}]},

    // â”€â”€ Runtimes: comma-delimited, sendPost converts to '+' for transport â”€â”€
    runtimes: { tags: [{ armTag: 'runTimes', extract: b => {
      if (typeof b === 'object' && !Array.isArray(b)) {
        return Array.from({ length: 48 }, (_, i) => b[String(i)] ?? '1').join(',');
      }
      return Array.isArray(b) ? b.slice(0, 48).join(',') : '';
    }}]},

    // â”€â”€ Email: selEmailAlert mixed named/positional â”€â”€
    // UI sends string[9]: [0]=enable, [1]=server, [2]=authType, [3]=port,
    //   [4]=account, [5]=password, [6]=displayIP, [7]=toAddr, [8]=fromAddr
    // Firmware field order: selEmailAlert, to, from, server, selEmailAuthType, emailPort, acct, pw, display
    email: { tags: [{ armTag: 'selEmailAlert', extract: b => {
      const a = arr(b);
      if (a.length < 9) return '';
      return [
        a[0],
        `_1=${a[7]}`,
        `_2=${a[8]}`,
        `_3=${a[1]}`,
        `selEmailAuthType=${a[2]}`,
        `emailPort=${a[3]}`,
        `_6=${a[4]}`,
        `_7=${a[5]}`,
        `_8=${a[6]}`,
      ].join('&');
    }}]},

    // â”€â”€ Date: Date positional â”€â”€
    // UI sends { Date: string, Time: string, TimeType: string }
    date: { tags: [{ armTag: 'Date', extract: b => {
      if (b?.Date && b?.Time) return pos(b.Date, b.Time, b.TimeType ?? '0');
      const a = arr(b);
      return a.length >= 3 ? pos(a[0], a[1], a[2]) : '';
    }}]},

    // â”€â”€ Master/Slave: selMasterSlaveMode positional â”€â”€
    // UI sends string[2]: [0]=mode, [1]=masterIP
    master: { tags: [{ armTag: 'selMasterSlaveMode', extract: b => {
      const a = arr(b);
      return a.length >= 2 ? pos(a[0], a[1]) : (a.length === 1 ? pos(a[0]) : '');
    }}]},

    // â”€â”€ Alerts: AlertSetup â€” single string of 0/1 flags â”€â”€
    // UI sends boolean[]
    alerts: { tags: [{ armTag: 'AlertSetup', extract: b => {
      if (!Array.isArray(b)) return '';

      const incomingBits = b.map((v: any) => (v ? '1' : '0'));
      const included = getIncludedAlertIndices();
      const full = getFullAlertBits();

      // Legacy behavior: excluded warnings are forced OFF.
      for (let i = 0; i < WARNING_KEYS.length; i++) {
        full[i] = '0';
      }

      // Expand filtered UI bits back into full warning bitmap.
      for (let i = 0; i < included.length; i++) {
        const fullIdx = included[i];
        full[fullIdx] = incomingBits[i] ?? '0';
      }

      return full.join('');
    }}]},

    // â”€â”€ IO Config: p2IoConfig with AS2 prefix + dynamic o/i keys â”€â”€
    // UI sends { outputConfig: string[], inputConfig: string[] }
    // NOTE: Skip '-1' (unassigned) ports to keep body under the ARM's
    // 600-byte MSG_RX_BUFFER_SIZE.  StoreIoConfig() clears all IO
    // assignments first, so omitted ports default to IO_UNDEFINED.
    ioconfig: { tags: [{ armTag: 'p2IoConfig', extract: b => {
      const outputs = arr(b, 'outputConfig');
      const inputs = arr(b, 'inputConfig');
      if (outputs.length === 0 && inputs.length === 0) return '';
      const parts = ['AS2'];
      outputs.forEach((val, i) => { if (val !== undefined && val !== '' && val !== '-1') parts.push(`o${i}=${val}`); });
      inputs.forEach((val, i) => { if (val !== undefined && val !== '' && val !== '-1') parts.push(`i${i}=${val}`); });
      return parts.join('&');
    }}]},

    // â”€â”€ Log: recInterval positional â”€â”€
    // UI sends string[2]: [0]=interval, [1]=wrap
    log: { tags: [{ armTag: 'recInterval', extract: b => {
      const a = arr(b);
      return a.length >= 2 ? pos(a[0], a[1]) : '';
    }}]},

    // â”€â”€ Accounts: AcctId0 positional (encrypted, forward as-is) â”€â”€
    // UI sends { users: string, passwords: string } â€” AES encrypted
    // NOTE: Cannot decrypt without DH shared secret. Forward raw data.
    accounts: { tags: [{ armTag: 'AcctId0', extract: b => {
      if (b?.users !== undefined) {
        return `${b.users}&_1=${b.passwords ?? ''}`;
      }
      const a = arr(b);
      return a.length > 0 ? pos(...a) : '';
    }}]},

    // â”€â”€ PID: pidWrap positional (single value) â”€â”€
    // UI sends { pidWrap: string, logDoors: string, logRefrig: string }
    pid: { tags: [{ armTag: 'pidWrap', extract: b => {
      if (b?.pidWrap !== undefined) return String(b.pidWrap);
      const a = arr(b);
      return a.length > 0 ? a[0] : '';
    }}]},

    // â”€â”€ PWM: p2PwmOutputs with AS2 prefix + dynamic channel keys â”€â”€
    // UI sends string[]: each index is a PWM channel, value is equipment PID
    pwm: { tags: [{ armTag: 'p2PwmOutputs', extract: b => {
      const a = arr(b);
      if (a.length === 0) return '';
      const parts = ['AS2'];
      a.forEach((val, i) => { if (val !== undefined && val !== '-1') parts.push(`${i}=${val}`); });
      return parts.join('&');
    }}]},

    // â”€â”€ Analog board: BAdd named fields â”€â”€
    // UI sends string[25]: [0]=addr, [2]=label, [4]=disabled, then per-sensor groups of 5
    analog: { tags: [{ armTag: 'BAdd', extract: b => {
      const a = arr(b);
      if (a.length < 25) return '';
      return [
        a[0],
        `BdLbl=${a[2] ?? ''}`,
        `BDis=${a[4] ?? '0'}`,
        `Sen1Lbl=${a[6] ?? ''}`,
        `Sen1Off=${a[7] ?? '0'}`,
        `Sen1Dis=${a[8] ?? '0'}`,
        `Sen2Lbl=${a[11] ?? ''}`,
        `Sen2Off=${a[12] ?? '0'}`,
        `Sen2Dis=${a[13] ?? '0'}`,
        `Sen3Lbl=${a[16] ?? ''}`,
        `Sen3Off=${a[17] ?? '0'}`,
        `Sen3Dis=${a[18] ?? '0'}`,
        `Sen4Lbl=${a[21] ?? ''}`,
        `Sen4Off=${a[22] ?? '0'}`,
        `Sen4Dis=${a[23] ?? '0'}`,
      ].join('&');
    }}]},

    // â”€â”€ Lights: no firmware POST tag â€” labels are read-only in firmware â”€â”€
    lights: { tags: [] },

    // â”€â”€ Aux program: AuxProgram named fields with dynamic rules â”€â”€
    // UI sends { auxProg: string[], rules: Rule[], ... }
    aux: { tags: [{ armTag: 'AuxProgram', extract: b => {
      if (!b?.auxProg) return '';
      const ap = b.auxProg as string[];
      const rules = (b.rules ?? []) as Array<Record<string, any>>;
      const parts = [ap[0]]; // AuxProgram = equipment index
      for (let i = 0; i < rules.length; i++) {
        const r = rules[i];
        const n = i + 1;
        if (i > 0 && r.andOr !== '256') parts.push(`andOr${n}=${r.andOr}`);
        parts.push(`type${n}=${r.type}`);
        parts.push(`io${n}=${r.io}`);
        parts.push(`st${n}=${r.st}`);
        parts.push(`op${n}=${r.op}`);
        parts.push(`ref${n}=${r.ref}`);
        parts.push(`sen${n}=${r.sen}`);
        parts.push(`diff${n}=${r.diff ?? '0'}`);
      }
      parts.push(`dutyCycle=${ap[1] ?? '0'}`);
      parts.push(`period=${ap[2] ?? '0'}`);
      parts.push(`units=${ap[3] ?? '0'}`);
      return parts.join('&');
    }}]},
  };

  for (const [route, mapping] of Object.entries(pageSaveMap)) {
    router.post(`/${route}`, async (req: Request, res: Response) => {
      const body = req.body;
      console.log(`[API] Save ${route}:`, JSON.stringify(body).slice(0, 200));

      // Snapshot BEFORE state for audit diff. Safe & cheap â€” reads cache.
      let beforeSnapshot: any = undefined;
      try { beforeSnapshot = dataCache.buildPageData(route); }
      catch { /* snapshot is best-effort; keep undefined on failure */ }

      try {
        for (const { armTag, extract } of mapping.tags) {
          const value = extract(body);
          if (value !== '') {
            // Send to ARM via RTS/ACK handshake â€” ARM echoes back to update cache
            await serialBridge.sendPost(`${armTag}=${value}`);
          }
        }

        if (route === 'analog') {
          await persistAnalogSaveToRs485(body);
          await syncAnalogCacheFromRs485();
        }

        if (route === 'ioconfig') {
          // Push equipment labels to orbit simulator(s) so the panel
          // can display meaningful names instead of raw "DO1"/"DI1".
          syncIoConfigToOrbit(body, dataCache).catch(err =>
            console.warn('[API] Orbit ioconfig sync error:', err.message));
        }

        // Audit trail â€” record who saved what, with field-level before/after.
        try {
          const diff = beforeSnapshot !== undefined ? computeDiff(beforeSnapshot, body) : [];
          const detail = diff.length > 0
            ? `${diff.length} field${diff.length === 1 ? '' : 's'} changed`
            : JSON.stringify(body).slice(0, 120);
          recordSave(currentActor, currentSlot, currentLevel, route, detail, req.ip, diff);
        } catch { /* audit must never break a save */ }

        res.json({ ok: true });
      } catch (err: any) {
        console.error(`[API] Save ${route} error:`, err.message);
        // Return ok:true anyway so the UI doesn't show error â€” cache is updated locally
        // from the ARM echo in the normal data flow
        res.status(503).json({ ok: false, error: err.message });
      }
    });
  }

  // â”€â”€â”€ Other POST endpoints â”€â”€â”€

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
        dataCache.updateFromArm('VfdAlarmData', '');
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
      } else if (body?.tag && body?.value !== undefined) {
        await serialBridge.sendPost(`${body.tag}=${body.value}`);
      } else if (body?.ButtonId !== undefined) {
        await serialBridge.sendPost(`Button=${body.ButtonId}`);
      } else if (body?.tag) {
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

  /** Save settings â€” sends the complete POST body to the ARM via RTS/ACK handshake */
  router.post('/PostSave.jsp', async (req: Request, res: Response) => {
    const body = req.body;
    console.log('[API] PostSave:', JSON.stringify(body));

    try {
      // Build URL-encoded body: "field1=val1&field2=val2&SessionID=0"
      // The ARM expects the entire POST as a single framed message.
      const parts: string[] = [];
      if (body && typeof body === 'object') {
        for (const [key, value] of Object.entries(body)) {
          parts.push(`${key}=${value}`);
        }
      }
      // Add SessionID if not present (ARM requires it for completion tracking)
      if (!parts.some(p => p.startsWith('SessionID='))) {
        parts.push('SessionID=0');
      }
      const postBody = parts.join('&');

      const reply = await serialBridge.sendPost(postBody);
      // Legacy ARM returns the literal strings "true"/"false".  The Nova
      // shim resolves with `undefined` (no throw) when the command is
      // handled successfully — treat that as success too.
      const ok = reply === undefined || reply === 'true';
      res.json({ ok, reply: reply ?? 'true' });
    } catch (err: any) {
      console.error('[API] PostSave error:', err.message);
      res.status(503).json({ ok: false, error: err.message });
    }
  });

  /** TCP/IP settings save */
  router.post('/tcpip', (req: Request, res: Response) => {
    console.log('[API] TCP/IP settings:', JSON.stringify(req.body));
    res.json({ ok: true });
  });

  /** Network configuration save */
  router.post('/network', (_req: Request, res: Response) => {
    res.json({ ok: true });
  });

  /** Node configuration */
  router.post('/node', (req: Request, res: Response) => {
    console.log('[API] Node config:', JSON.stringify(req.body));
    res.json({ ok: true });
  });

  router.get('/node/:ip', (req: Request, res: Response) => {
    res.json({ ip: req.params.ip, status: 'unknown' });
  });

  /**
   * IO config rename â€” sends ioRename POST to ARM via RTS/ACK handshake.
   * Firmware StoreIoName() stores the name for renamable equipment.
   */
  router.post('/ioconfig/:idx/:name', async (req: Request, res: Response) => {
    const idx = req.params.idx;
    const name = decodeURIComponent(req.params.name);
    console.log(`[API] IO rename: idx=${idx} name=${name}`);

    try {
      await serialBridge.sendPost(`ioRename=${idx}&name=${name}`);

      console.log(`[API] IO rename complete: ioRename=${idx} â†’ "${name}"`);
      res.json({ ok: true });
    } catch (err: any) {
      console.error('[API] IO rename error:', err.message);
      res.status(503).json({ ok: false, error: err.message });
    }
  });

  /** Fan speed set */
  router.post('/setfanspeed', (req: Request, res: Response) => {
    console.log('[API] Set fan speed:', JSON.stringify(req.body));
    if (req.body?.setFanSpeed !== undefined) {
      serialBridge.sendCommand('maxFanSpeed', String(req.body.setFanSpeed));
    }
    res.json({ ok: true });
  });

  /**
   * Settings download â€” returns ALL cached settings as a text file.
   * Replicates GellertServerD's settings-to-USB download.
   * The Svelte UI collects these for backup/restore.
   */
  router.post('/settings/download', (req: Request, res: Response) => {
    console.log('[API] Settings download requested');
    try {
      const allData = dataCache.getAll();
      // Build a text representation: one key=value per line
      const lines: string[] = [];
      for (const [key, value] of Object.entries(allData)) {
        lines.push(`${key}=${value}`);
      }
      const content = lines.join('\n');
      console.log(`[API] Settings download: ${lines.length} entries`);
      res.json({ ok: true, content, count: lines.length });
    } catch (err: any) {
      console.error('[API] Settings download error:', err.message);
      res.status(503).json({ ok: false, error: err.message });
    }
  });

  /**
   * Settings restore â€” relay saved settings back to the ARM.
   * Replicates GellertServerD CallBacks.c PostSettingsRestore handler:
   *   1. Send FanRemoteOff=2 (REMOTE_OFF â€” prevents fan control conflicts)
   *   2. For each settings line: send RestoreSettings=1&<line>
   *   3. Send FanRemoteOff=0 (REMOTE_ON)
   *
   * The ARM CGI tag for RestoreSettings is "RestoreSettings";
   * VarString "1" tells the ARM a settings line follows after the '&'.
   */
  router.post('/settings/restore', async (req: Request, res: Response) => {
    const body = req.body;
    console.log('[API] Settings restore:', JSON.stringify(body).slice(0, 200));

    try {
      const content: string = body?.content || '';
      if (!content) {
        res.json({ ok: false, error: 'No settings content provided' });
        return;
      }

      const lines = content.split('\n').filter((l: string) => l.trim().length > 0);
      console.log(`[API] Settings restore: ${lines.length} lines to relay`);

      // Step 1: Disable fan remote control to avoid conflicts
      await serialBridge.sendPost('FanRemoteOff=2');
      await new Promise(r => setTimeout(r, 300));

      // Step 2: Send each settings line (matches RelaySystemSettingsMessage)
      let sent = 0;
      for (const line of lines) {
        await serialBridge.sendPost(`RestoreSettings=1&${line.trim()}`);
        sent++;
        // Small delay between lines (ARM clears VarString in ~300ms)
        await new Promise(r => setTimeout(r, 100));
      }

      // Step 3: Re-enable fan remote control
      await serialBridge.sendPost('FanRemoteOff=0');

      console.log(`[API] Settings restore complete: ${sent}/${lines.length} lines sent`);
      res.json({ ok: true, sent, total: lines.length });
    } catch (err: any) {
      console.error('[API] Settings restore error:', err.message);
      res.status(503).json({ ok: false, error: err.message });
    }
  });

  /** Email test */
  router.post('/email/test', (req: Request, res: Response) => {
    console.log('[API] Email test:', JSON.stringify(req.body));
    res.json({ ok: true, message: 'Test email sent' });
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

  // â”€â”€â”€ User log download â”€â”€â”€

  router.get('/user/download', (req: Request, res: Response) => {
    res.json({ data: [] });
  });

  // â”€â”€â”€ Debug / health â”€â”€â”€

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

  // â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
  // AuxProgram parsing helpers
  // â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

  /**
   * Parse an AuxProgram wire-format string into the UI's allAux entry format.
   *
   * Wire format: "eqIndex:dutyCycle:period:units,type:io:st:op:sen:andOr:ref,...Ã—6,,"
   * UI format:   { auxProg: [eq,duty,period,units], rules: Rule[6] }
   *
   * Rule visibility: rules after an END marker (andOr=255) get andOr='256' (hidden).
   */
  function parseAuxProgram(raw: string): { auxProg: string[]; rules: any[] } {
    const parts = raw.split(',').filter(p => p.trim() !== '');

    // Header: eqIndex:dutyCycle:period:units
    const hp = (parts[0] || '').split(':');
    const auxProg = [hp[0] || '0', hp[1] || '100', hp[2] || '1', hp[3] || '1'];

    // Parse 6 rules
    const rules: any[] = [];
    let showNext = true; // Rule 0 is always visible

    for (let i = 0; i < 6; i++) {
      const ruleStr = parts[i + 1] || '';
      const rp = ruleStr ? ruleStr.split(':') : [];

      const type       = rp[0] || '255';
      const io         = rp[1] || '0';
      const st         = rp[2] || '0';
      const op         = rp[3] || '0';
      const sensorVal  = rp[4] || '0';
      const andOrWire  = rp[5] || '255';
      const ref        = rp[6] || '0';

      // For sensor rules (type=4), split sensorValue into sen/diff based on state.
      // st='0' = absolute mode â†’ sen=sensorValue, diff='0'
      // st='1' = reference mode â†’ diff=sensorValue, sen='255'
      let sen  = sensorVal;
      let diff = '0';
      if (type === '4' && st === '1') {
        diff = sensorVal;
        sen  = '255';
      }

      const andOr: string = showNext ? andOrWire : '256';

      rules.push({
        type, io, st, op, sen, diff, andOr, ref,
        first: i === 0,
        sensorOption: '',
      });

      // Only show next rule if current one chains (AND=0 or OR=1)
      showNext = (andOr === '0' || andOr === '1');
    }

    return { auxProg, rules };
  }

  /** Build default rules for a defined-but-uncached aux program (RT_MANUAL init). */
  function buildDefaultAuxRules(): any[] {
    const rules: any[] = [];
    for (let i = 0; i < 6; i++) {
      rules.push({
        type: i === 0 ? '0' : '255',   // RT_MANUAL for first, RT_UNDEFINED for rest
        io: '0',
        st: '0',
        op: '0',
        sen: '0',
        diff: '0',
        andOr: i === 0 ? '255' : '256', // END for first, hidden for rest
        ref: '0',
        first: i === 0,
        sensorOption: '',
      });
    }
    return rules;
  }

  /**
   * Trigger async NextAux commands to the ARM to collect missing aux programs.
   * The ARM responds to each NextAux with the next defined AuxProgram, which
   * the bridge accumulates in the dataCache. Subsequent /aux/all requests
   * will include the newly discovered data.
   */
  let auxDiscoveryRunning = false;
  function discoverAuxPrograms(bridge: CommandBridge, lastKnownEq: string, count: number): void {
    if (auxDiscoveryRunning) return;
    auxDiscoveryRunning = true;
    console.log(`[API] Starting aux discovery: ${count} missing, sending NextAux from eq ${lastKnownEq}`);

    (async () => {
      try {
        let currentEq = lastKnownEq;
        // Send NextAux up to count+1 times to cycle through all defined programs
        for (let i = 0; i < Math.min(count + 1, 8); i++) {
          await bridge.sendPost(`NextAux=${currentEq}`);
          // Small delay to let the ARM's AuxProgram response arrive and get cached
          await new Promise(r => setTimeout(r, 500));
          // Update currentEq from latest accumulated data
          const latest = [...dataCache.getAllAuxPrograms().keys()].pop();
          if (latest) currentEq = latest;
        }
      } catch (err: any) {
        console.error('[API] Aux discovery error:', err.message);
      } finally {
        auxDiscoveryRunning = false;
      }
    })();
  }

  // â”€â”€â”€ VFD / Fans page endpoints â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

  router.get('/fans', (_req: Request, res: Response) => {
    if (!vfdClient) {
      res.json({ drives: [] });
      return;
    }
    res.json({ drives: vfdClient.getDrives() });
  });

  // VFD alarms â€” returns active fault alarms from all drives in KEY=Text format
  router.get('/alarms', (_req: Request, res: Response) => {
    if (!vfdClient) {
      res.json({ alarms: [] });
      return;
    }
    const alarms: string[] = [];
    for (const drv of vfdClient.getDrives()) {
      if (drv.faulted && drv.faultCode) {
        const profile = getProfile(drv.manufacturer);
        const faultName = profile.faultNames[drv.faultCode]
          ?? `Code 0x${drv.faultCode.toString(16).padStart(4, '0')}`;
        alarms.push(`WARN_VFD=${drv.label}: ${faultName}`);
      }
    }
    res.json({ alarms });
  });

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

    // Proxy to orbit simulator which owns the VFDSimulator
    const orbitPort = parseInt(process.env.ORBIT_API_PORT || '9010', 10);
    try {
      const resp = await fetch(`http://127.0.0.1:${orbitPort}/api/vfd/fault`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ unitId, faultCode }),
      });
      const data = await resp.json() as { ok: boolean };
      res.json({ ok: data.ok, faultCode, faultName });
    } catch (err) {
      res.status(502).json({ ok: false, error: 'Orbit simulator unreachable' });
    }
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

  // â”€â”€â”€ GDC (Door Controller) API â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  // Proxy to the GDC orbit board's REST API.  Only responds if a GDC board
  // is discovered; returns { present: false } otherwise.
  //
  // Port resolution: orbit manager assigns API ports as (9008 + orbit_id).
  // Each orbit's DIP switch ID is (slot + 2), so API port = 9008 + (slot + 2) = ORBIT_API_PORT + slot.
  // But if the sensor sim occupies the same port (9011), we scan multiple ports.

  async function findGDCPort(): Promise<number | null> {
    const gdc = dataCache.getOrbitBoards().find(b => b.role === 2 && b.connected);
    if (!gdc) return null;
    const port = ORBIT_API_PORT + gdc.slot;
    try {
      const resp = await fetch(`http://127.0.0.1:${port}/api/gdc`, {
        signal: AbortSignal.timeout(500),
      });
      if (resp.ok) return port;
    } catch {}
    return null;
  }

  router.get('/gdc', async (_req: Request, res: Response) => {
    const port = await findGDCPort();
    if (!port) {
      res.json({ present: false });
      return;
    }
    try {
      const resp = await fetch(`http://127.0.0.1:${port}/api/gdc`, {
        signal: AbortSignal.timeout(1000),
      });
      if (resp.ok) {
        const data = await resp.json() as Record<string, unknown>;
        res.json({ present: true, ...data });
      } else {
        res.json({ present: false });
      }
    } catch {
      res.json({ present: false });
    }
  });

  router.post('/gdc/stages', async (req: Request, res: Response) => {
    const port = await findGDCPort();
    if (!port) {
      res.status(404).json({ ok: false, error: 'No GDC board found' });
      return;
    }
    try {
      const resp = await fetch(`http://127.0.0.1:${port}/api/gdc/stages`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(req.body),
        signal: AbortSignal.timeout(2000),
      });
      const data = await resp.json();
      res.status(resp.status).json(data);
    } catch (err: any) {
      res.status(502).json({ ok: false, error: err.message ?? 'GDC unreachable' });
    }
  });

  router.post('/gdc/calibrate', async (_req: Request, res: Response) => {
    const port = await findGDCPort();
    if (!port) {
      res.status(404).json({ ok: false, error: 'No GDC board found' });
      return;
    }
    try {
      const resp = await fetch(`http://127.0.0.1:${port}/api/gdc/calibrate`, {
        method: 'POST',
        signal: AbortSignal.timeout(2000),
      });
      const data = await resp.json();
      res.status(resp.status).json(data);
    } catch (err: any) {
      res.status(502).json({ ok: false, error: err.message ?? 'GDC unreachable' });
    }
  });

  // ─── Triton (Refrigeration) API ──────────────────────────────────────────
  // Proxy to each Triton orbit board.  Routes are slot-scoped so the Level 2
  // refrigeration page can paginate across multiple Tritons.

  /** Resolve API port for a given Triton slot, returning null if unreachable. */
  async function findTritonPort(slot: number): Promise<number | null> {
    const triton = dataCache.getOrbitBoards().find(
      b => b.slot === slot && b.role === 3 && b.connected,
    );
    if (!triton) return null;
    const port = ORBIT_API_PORT + triton.slot;
    try {
      const resp = await fetch(`http://127.0.0.1:${port}/api/triton`, {
        signal: AbortSignal.timeout(500),
      });
      if (resp.ok) return port;
    } catch {}
    return null;
  }

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

  /** GET /iot/triton/:slot — full state snapshot for one Triton unit. */
  router.get('/triton/:slot', async (req: Request, res: Response) => {
    const slot = parseInt(req.params.slot, 10);
    if (isNaN(slot)) { res.status(400).json({ ok: false, error: 'invalid slot' }); return; }
    const port = await findTritonPort(slot);
    if (!port) { res.json({ ok: true, present: false }); return; }
    try {
      const resp = await fetch(`http://127.0.0.1:${port}/api/triton`, {
        signal: AbortSignal.timeout(1000),
      });
      const data = await resp.json();
      res.status(resp.status).json(data);
    } catch (err: any) {
      res.status(502).json({ ok: false, error: err.message ?? 'Triton unreachable' });
    }
  });

  /**
   * Helper: write a Modbus holding register to a Triton orbit.  Returns
   * { ok: true } on success, or an HTTP-friendly error envelope.  All Triton
   * control writes go through Modbus TCP — same path as the real Nova firmware.
   */
  async function writeTritonReg(slot: number, addr: number, value: number): Promise<{ ok: boolean; error?: string }> {
    if (!orbitClient) return { ok: false, error: 'orbitClient not available' };
    const triton = dataCache.getOrbitBoards().find(b => b.slot === slot && b.role === 3 && b.connected);
    if (!triton) return { ok: false, error: 'No Triton at slot' };
    // slot → orbit-id (sim convention: orbit-id = slot + 2; see index.ts discovery).
    const orbitId = slot + 2;
    const ok = await orbitClient.writeHoldingRegister(orbitId, addr, value & 0xFFFF);
    return ok ? { ok: true } : { ok: false, error: 'Modbus write failed' };
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
      for (let i = 0; i < Math.min(4, aoMode.length); i++) {
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

  /** POST /iot/triton/:slot/safety/di  body: { index: number, value: boolean }
   *  Toggles DI 1-10 on the orbit (used by the safety section of the popup
   *  to simulate phase-loss / pressure-switch / overload contacts opening). */
  router.post('/triton/:slot/safety/di', async (req: Request, res: Response) => {
    const slot = parseInt(req.params.slot, 10);
    const port = await findTritonPort(slot);
    if (!port) { res.status(404).json({ ok: false, error: 'Triton orbit not reachable' }); return; }
    const { index, value } = req.body ?? {};
    if (!Number.isInteger(index) || index < 0 || index > 9 || typeof value !== 'boolean') {
      res.status(400).json({ ok: false, error: 'index (0-9) and boolean value required' });
      return;
    }
    try {
      const r = await fetch(`http://127.0.0.1:${port}/api/di`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ index, value }),
        signal: AbortSignal.timeout(1000),
      });
      const data = await r.json();
      res.status(r.ok ? 200 : 502).json(data);
    } catch (e: any) {
      res.status(502).json({ ok: false, error: e?.message ?? 'proxy failed' });
    }
  });

  /** POST /iot/triton/:slot/safety/reset  body: { mask?: number }
   *  Clears latched safety lockout bits on the orbit (HP / Comp OL / Run-Prove). */
  router.post('/triton/:slot/safety/reset', async (req: Request, res: Response) => {
    const slot = parseInt(req.params.slot, 10);
    const port = await findTritonPort(slot);
    if (!port) { res.status(404).json({ ok: false, error: 'Triton orbit not reachable' }); return; }
    try {
      const r = await fetch(`http://127.0.0.1:${port}/api/triton/safety/reset`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(req.body ?? {}),
        signal: AbortSignal.timeout(1000),
      });
      const data = await r.json();
      res.status(r.ok ? 200 : 502).json(data);
    } catch (e: any) {
      res.status(502).json({ ok: false, error: e?.message ?? 'proxy failed' });
    }
  });

  // â”€â”€â”€ Orbit module status â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
  // GET /orbit/status â†’ fetch status from each orbit simulator instance.
  // Returns an array of per-board status objects for the UI orbit page.
  router.get('/orbit/status', async (_req: Request, res: Response) => {
    const boards: any[] = [];

    for (let slot = 0; slot < NOVA_MAX_ORBITS; slot++) {
      const port = ORBIT_API_PORT + slot;
      try {
        const resp = await fetch(`http://localhost:${port}/api/status`, {
          signal: AbortSignal.timeout(500),
        });
        if (resp.ok) {
          const data = await resp.json() as Record<string, unknown>;
          boards.push({
            slot,
            port,
            dipswitch_id: slot + 2,
            connected: true,
            ...data,
          });
        }
      } catch {
        // Board not reachable â€” skip
      }
    }

    res.json({
      max_slots: NOVA_MAX_ORBITS,
      boards_found: boards.length,
      boards,
    });
  });

  // GET /orbit/boards â†’ lightweight list of which boards responded
  router.get('/orbit/boards', async (_req: Request, res: Response) => {
    const boards: { slot: number; dipswitch_id: number; connected: boolean; port: number }[] = [];

    for (let slot = 0; slot < NOVA_MAX_ORBITS; slot++) {
      const port = ORBIT_API_PORT + slot;
      try {
        const resp = await fetch(`http://localhost:${port}/api/status`, {
          signal: AbortSignal.timeout(300),
        });
        boards.push({
          slot,
          dipswitch_id: slot + 2,
          connected: resp.ok,
          port,
        });
      } catch {
        // Not reachable â€” don't include
      }
    }

    res.json({ boards });
  });

  return router;
}
