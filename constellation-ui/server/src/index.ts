/**
 * Constellation Bridge Server � main entry point.
 *
 * Extends the Agristar Bridge Server for the Constellation platform:
 *   1. Opens serial UART to the Nova controller (TCP or physical serial)
 *   2. Implements CRC-32 validated message framing
 *   3. Maintains an in-memory cache of all CGI variables
 *   4. Exposes WebSocket at /iot/ws for real-time push to the UI
 *   5. Exposes REST endpoints at /iot/* for page navigation and POST operations
 *   6. Optionally serves the SvelteKit build as static files
 *
 * Environment variables:
 *   PORT           � HTTP listen port (default: 3001)
 *   SERIAL_PORT    � Serial device path or tcp://host:port (default: 'tcp://localhost:9000')
 *   SERIAL_BAUD    � Baud rate (default: 230400)
 *   SVELTE_BUILD   � Path to SvelteKit build output (optional, for serving static files)
 */

import { createServer } from 'http';
import express from 'express';
import cors from 'cors';
import { DataCache, getNovaId } from './dataCache.js';
import { WsManager } from './wsManager.js';
import { createApiRoutes, getCurrentAuth } from './apiRoutes.js';
import { recordSave } from './accountStore.js';
import { UpgradeManager } from './upgradeManager.js';
import { loadConfig } from './simConfig.js';
import { VFDClient } from './vfdClient.js';
import { getProfile } from './vfdRegisterMaps.js';
import {
  NovaSerialBridge,
  pbDecodeFields, pbGetVarint, pbGetString, pbGetAllSubmsg,
  MSG_FW_INSTALL_PROGRESS, MSG_FW_INSTALL_RESULT,
} from './novaSerialBridge.js';
import { NovaDataStore } from './novaDataStore.js';
import { MasterSlaveSync } from './masterSlaveSync.js';
import { RemoteSystemsSync } from './remoteSystemsSync.js';
import { ProtoStream } from './protoStream.js';
import { createNovaAdapter } from './novaAdapter.js';
import { NovaLogDataStore } from './novaLogDataStore.js';
import { NovaFwUpdateManager } from './novaFwUpdateManager.js';
import { createDjangoSync } from './djangoSync.js';
import { startAuditForwarder } from './auditForwarder.js';
import { createLogger, installConsoleBridge } from './logger.js';
import {
  BUTTON_TO_RO,
  RO,
  REMOTE_AUTO,
  REMOTE_OFF,
  decodeRemoteOffValue,
  lightsButtonNextState,
} from './equipmentIds.js';

// Reroute global `console.*` through the structured logger so every
// existing call site emits JSON for journald. Must run before any
// non-trivial work that logs.
installConsoleBridge();

const log = createLogger('Server');

/* -- Temperature unit helpers -- */
const TEMP_UI_TYPES = new Set([0, 3, 4, 5, 9, 11]);
function cToF(c: number): number { return c * 9 / 5 + 32; }
function isTempType(uiType: number | string): boolean {
  return TEMP_UI_TYPES.has(Number(uiType));
}
function isUnitFahrenheit(cache: DataCache): boolean {
  const entry = cache.getByVarName('P2BasicSetupData');
  if (!entry?.value) return true;
  const fields = entry.value.split(',');
  return fields[1] !== '1';
}
function maybeConvertTemp(c: number, cache: DataCache): number {
  return isUnitFahrenheit(cache) ? cToF(c) : c;
}

// --- Default Equipment Names ---
// In production, gellertserverd reads /var/www/languageEquipDesc.txt at init and
// sends translated names to the ARM firmware via equipDesc serial messages.
// Since the bridge replaces gellertserverd, we overlay them here when the
// firmware sends IO definitions with empty name fields (factory default state).
// Source: UI-International/languageEnglishEquipDesc.txt
const DEFAULT_EQUIP_NAMES: Record<number, string> = {
  0: 'Fan/Green Light', 1: 'Door', 2: 'Refrigeration', 3: 'ClimaCell',
  4: 'Heat', 5: 'Cavity Heat', 6: 'Burner',
  7: 'Humidifier 1 - Head', 8: 'Humidifier 1 - Pump',
  9: 'Humidifier 2 - Head', 10: 'Humidifier 2 - Pump',
  11: 'Humidifier 3 - Head', 12: 'Humidifier 3 - Pump',
  13: 'Refrigeration Stage 1', 14: 'Refrigeration Stage 2',
  15: 'Refrigeration Stage 3', 16: 'Refrigeration Stage 4',
  17: 'Refrigeration Stage 5', 18: 'Refrigeration Stage 6',
  19: 'Refrigeration Stage 7', 20: 'Refrigeration Stage 8',
  21: 'Defrost 1', 22: 'Defrost 2',
  23: 'Bay Lights 1', 24: 'Bay Lights 2',
  25: 'Auxiliary 1', 26: 'Auxiliary 2', 27: 'Auxiliary 3', 28: 'Auxiliary 4',
  29: 'Auxiliary 5', 30: 'Auxiliary 6', 31: 'Auxiliary 7', 32: 'Auxiliary 8',
  33: 'Power Failure', 34: 'Remote Standby', 35: 'Refrigeration Standby',
  36: 'Air Flow', 37: 'Low Temperature Limit',
  38: 'Red Light', 39: 'Yellow Light',
  40: 'Pulse Door - Power', 41: 'Pulse Door - Open', 42: 'Pulse Door - Close',
  43: 'Start/Stop', 44: 'Fan - Auto', 45: 'Fan - Manual',
  46: 'Door - Auto', 47: 'Door - Manual',
  48: 'ClimaCell - Auto', 49: 'ClimaCell - Manual',
  50: 'Humidifier - Auto', 51: 'Humidifier - Manual',
  52: 'Refrigeration - Auto', 53: 'Cure - Auto', 54: 'Burner - Auto',
  55: 'Auxiliary 1 - Auto', 56: 'Auxiliary 1 - Manual',
  57: 'Auxiliary 2 - Auto', 58: 'Auxiliary 2 - Manual',
};

// --- Configuration ---

const PORT        = parseInt(process.env.PORT ?? '9001', 10);
const SERIAL_PORT = process.env.SERIAL_PORT ?? 'tcp://localhost:9000';
const SERIAL_BAUD = parseInt(process.env.SERIAL_BAUD ?? '230400', 10);
const SVELTE_BUILD = process.env.SVELTE_BUILD ?? '';

// --- Initialize components ---

const dataCache    = new DataCache();
const upgradeManager = new UpgradeManager();

// Nova bridge (COBS + nanopb over UART1) � single, authoritative firmware
// transport.  The legacy ASCII RTS/ACK SerialBridge has been retired.
const novaBridge    = new NovaSerialBridge({ port: SERIAL_PORT, baudRate: SERIAL_BAUD });
const novaStore     = new NovaDataStore();
const novaLogStore  = new NovaLogDataStore(novaBridge, dataCache);
const novaFwManager = new NovaFwUpdateManager(novaBridge);

// Cross-panel outside-air sync. Replaces the legacy AS2 UDP broadcast
// with a confirmed HTTP push between bridges. Idle until the panel is
// configured as Master (push side) or Slave (receive side) via the
// Level-2 master/slave page. See masterSlaveSync.ts for protocol
// details.
const masterSlaveSync = new MasterSlaveSync(
  novaStore,
  novaBridge,
  () => dataCache.getByVarName('PanelName')?.value || 'Agristar Panel',
  () => getNovaId(),
);
masterSlaveSync.start();

// Persistent UUID-keyed list of OTHER panels we want to monitor.
// Self-heals across DHCP changes by matching on novaId. CRUD via
// /iot/remote-systems/* (see apiRoutes.ts). Operator's groupings
// in localStorage stay valid because they reference our entry IDs,
// which never change.
const remoteSystemsSync = new RemoteSystemsSync(() => getNovaId());
remoteSystemsSync.start();

// Hydrate DJANGO_TOKEN from persisted iotConfig BEFORE createDjangoSync runs
// so it sees the token and enables itself. apiRoutes.ts also hydrates this for
// /iot/cloud/* endpoints, but that happens too late for createDjangoSync.
if (!process.env.DJANGO_TOKEN) {
  const savedIot = loadConfig<{ AccessToken?: string }>('iotConfig');
  if (savedIot?.AccessToken) {
    process.env.DJANGO_TOKEN = savedIot.AccessToken;
    console.log('[Bootstrap] Loaded DJANGO_TOKEN from iotConfig');
  }
}

// DjangoSync: push controller data to Django backend (replaces iotclient ? Azure IoT Hub)
const djangoSync = createDjangoSync(dataCache);

// Wire up command executor for Django-pushed commands (Nova protobuf).
{
  /**
   * Nova command executor � translates ASCII POST bodies to protobuf commands.
   * Supports:
   *   - Equipment toggles: fanBtn=0,1 -> sendEquipmentCmd(0, 1)
   *   - System commands: ClearAlarm=ClearAlarm -> sendSystemCmd(1)
   *   - Settings: PlenumTempSet=68.5 -> sendSettingsUpdate(...)
   */
  djangoSync.setCommandExecutor(async (body: string) => {
    try {
      // Parse the POST body (key=value&key=value or key=val,val)
      const params = new URLSearchParams(body.replace(/,/g, '%2C'));

      for (const [key, value] of params) {
        // Equipment button toggles: "fanBtn=Auto", "aux1Btn=On", or
        // (legacy numeric form) "fanBtn=0,1" meaning eqIndex=0,newState=1.
        if (key.endsWith('Btn')) {
          let eqIndex: number | undefined;
          let newState: number | undefined;

          if (value.includes('%2C')) {
            // Numeric "eqIdx,state" form
            const parts = value.split('%2C');
            const a = parseInt(parts[0], 10);
            const b = parseInt(parts[1], 10);
            if (!isNaN(a) && !isNaN(b)) { eqIndex = a; newState = b; }
          } else if (key in BUTTON_TO_RO) {
            // Symbolic value form: "Auto"/"Off"/"On" (lights = "Toggle")
            eqIndex = BUTTON_TO_RO[key];
            if (key === 'lights1Btn' || key === 'lights2Btn') {
              const cur = novaStore?.equipmentStatus?.items.find(
                e => e.eqIndex === eqIndex,
              );
              newState = lightsButtonNextState(cur?.remoteOff ?? REMOTE_AUTO);
            } else {
              newState = decodeRemoteOffValue(value);
            }
          }

          if (eqIndex !== undefined && newState !== undefined) {
            await novaBridge.sendEquipmentCmd(eqIndex, newState);
            console.log(
              `[NovaCmdExec] ${key}=${value} ? equipCmd(${eqIndex}, ${newState})`,
            );
          } else {
            console.warn(`[NovaCmdExec] Unrecognized button command: ${key}=${value}`);
          }
          continue;
        }

        // System commands
        if (key === 'ClearAlarm') {
          await novaBridge.sendSystemCmd(1 /* CMD_CLEAR_ALARM */);
          console.log('[NovaCmdExec] ClearAlarm');
          continue;
        }
        if (key === 'SetDefault') {
          await novaBridge.sendSystemCmd(2 /* CMD_SET_DEFAULT */);
          console.log('[NovaCmdExec] SetDefault');
          continue;
        }
        if (key === 'PanelDefault') {
          await novaBridge.sendSystemCmd(3 /* CMD_PANEL_DEFAULT */);
          console.log('[NovaCmdExec] PanelDefault');
          continue;
        }
        if (key === 'FactoryDefault') {
          await novaBridge.sendSystemCmd(4 /* CMD_FACTORY_DEFAULT */);
          console.log('[NovaCmdExec] FactoryDefault');
          continue;
        }
        if (key === 'ClearRefrigDiag') {
          await novaBridge.sendSystemCmd(6 /* CMD_CLEAR_DIAG */);
          console.log('[NovaCmdExec] ClearRefrigDiag');
          continue;
        }
        if (key === 'FindBoard') {
          await novaBridge.sendSystemCmd(7 /* CMD_FIND_BOARD */);
          console.log('[NovaCmdExec] FindBoard');
          continue;
        }
        if (key === 'RemoteStop') {
          if (value === '1') {
            await novaBridge.sendSystemStop();
            console.log('[NovaCmdExec] RemoteStop: SYSTEM_STOP engaged (latching)');
          } else {
            await novaBridge.clearSystemStop();
            console.log('[NovaCmdExec] RemoteStop: cleared via ClearAlarm');
          }
          continue;
        }

        // Password authentication
        if (key === 'PasswordAuth') {
          const parts = value.split('%2C');
          if (parts.length >= 2) {
            await novaBridge.sendPasswordAuth(parts[0], parts[1]);
            console.log(`[NovaCmdExec] PasswordAuth user=${parts[0]}`);
          }
          continue;
        }

        // Data requests
        if (key === 'RequestData') {
          const reqType = parseInt(value, 10);
          if (!isNaN(reqType)) {
            novaBridge.sendDataRequest(reqType);
            console.log(`[NovaCmdExec] DataRequest type=${reqType}`);
          }
          continue;
        }

        // Orbit discovery request
        if (key === 'OrbitDiscovery') {
          novaBridge.requestOrbitDiscovery();
          console.log('[NovaCmdExec] OrbitDiscovery');
          continue;
        }

        // For settings updates, we'd need to build SettingsUpdate protobuf messages.
        // This requires mapping each settings tag to the correct proto field.
        // For now, log unhandled commands.
        console.log(`[NovaCmdExec] Unhandled: ${key}=${value}`);
      }
      return 'OK'; // CommandExecutor expects Promise<string>
    } catch (err) {
      console.error('[NovaCmdExec] Error:', err);
      throw err;
    }
  });
}

// Wire up upgrade manager for cloud-triggered upgrades
djangoSync.setUpgradeManager(upgradeManager);

const app = express();
const httpServer = createServer(app);

// Middleware
app.use(cors({ origin: true, credentials: true }));
app.use(express.json({ limit: '10mb' }));
app.use(express.urlencoded({ extended: true, limit: '10mb' }));

// --- Health Endpoint ---
// Reports bridge connectivity, protocol stats, log pipeline status,
// and uptime for monitoring. Returns 503 when any critical subsystem is
// down (Nova disconnected, pg unreachable) so simple ops checks like
// `curl -fs /health` flip red on real outages.
const serverStartTime = Date.now();
app.get('/health', async (_req, res) => {
  const novaConnected = novaBridge.isConnected();
  const protocolStats = novaBridge.getProtocolStats();
  const fwVersion = novaBridge.getFirmwareProtocolVersion();
  const uptimeSec = Math.floor((Date.now() - serverStartTime) / 1000);

  /* Logging health: pg ping + spool occupancy. The spool covers pg
   * outages, so a non-empty spool with pg.ok=true means the drain is
   * still in progress; pg.ok=false with growing spool means pg is
   * actually down. /health stays snappy via 1 s ping timeout. */
  let logging: any = undefined;
  if (novaLogStore) {
    const [pg, spool] = await Promise.all([
      novaLogStore.logDb.ping(1000),
      novaLogStore.logDb.spoolStats().catch(() => null),
    ]);
    logging = { pg, spool };
  }

  const pgOk = !logging || logging.pg.ok;
  const status = (novaConnected && pgOk) ? 'healthy' : 'degraded';
  const httpCode = (novaConnected && pgOk) ? 200 : 503;

  res.status(httpCode).json({
    status,
    uptime: uptimeSec,
    protocol: 'nova',
    nova: {
      connected: novaConnected,
      firmwareProtocolVersion: fwVersion,
      stats: protocolStats,
    },
    logging,
    vfd: vfdClient ? { enabled: VFD_ENABLED, host: VFD_HOST, port: VFD_PORT } : undefined,
  });
});

// --- System Stop Endpoints ---
// Emergency system stop � latching, requires explicit clear.
app.post('/api/system-stop', async (_req, res) => {
  if (!novaBridge) {
    res.status(400).json({ error: 'Nova bridge not available' });
    return;
  }
  try {
    await novaBridge.sendSystemStop();
    log.info('SYSTEM STOP engaged via REST API');
    res.json({ ok: true, message: 'System stop engaged (latching)' });
  } catch (err: any) {
    res.status(500).json({ error: err.message });
  }
});

app.post('/api/system-stop/clear', async (_req, res) => {
  if (!novaBridge) {
    res.status(400).json({ error: 'Nova bridge not available' });
    return;
  }
  try {
    await novaBridge.clearSystemStop();
    log.info('System stop cleared via REST API');
    res.json({ ok: true, message: 'System stop cleared' });
  } catch (err: any) {
    res.status(500).json({ error: err.message });
  }
});

// DEBUG — remove when Phase 4 lands. Smoke test for the Nova OTA broker:
// sends envelope tag 136 FwFleetProbe over UART and surfaces the broker's
// tag 142 FwFleetSnapshot reply for visual inspection. Schema: see
// proto/agristar/firmware.proto §FwFleetSnapshot. Hand-rolled decode (no
// generated TS stubs on the dev box) — keep in lockstep with the .proto.
app.get('/api/_debug/broker-fleet-probe', async (_req, res) => {
  if (!novaBridge) { res.status(400).json({ ok: false, error: 'Nova bridge not available' }); return; }
  const t0 = Date.now();
  try {
    const raw = await novaBridge.sendFleetProbe(5000);
    const snapFields = pbDecodeFields(raw);
    const members = pbGetAllSubmsg(snapFields, 1).map((mbuf) => {
      const f = pbDecodeFields(mbuf);
      return {
        host:           pbGetString(f, 1),
        reachable:      pbGetVarint(f, 2) !== 0,
        current_role:   pbGetVarint(f, 3),
        active_version: pbGetString(f, 4),
        active_bank:    pbGetVarint(f, 5),  // FwBankId: 0=A, 1=B, 2=GOLDEN
        bank_a_valid:   pbGetVarint(f, 6) !== 0,
        bank_b_valid:   pbGetVarint(f, 7) !== 0,
        boot_count:     fmtBootCount(pbGetVarint(f, 8)),
        error:          pbGetString(f, 9),
        boot_reason:    pbGetVarint(f, 10),  // 0=normal, 1=watchdog, 2=FALLBACK (skip-highest fired)
      };
    });
    res.json({ ok: true, members, rawHex: raw.toString('hex'), elapsed_ms: Date.now() - t0 });
  } catch (err: any) {
    res.status(504).json({ ok: false, error: err?.message ?? String(err), elapsed_ms: Date.now() - t0 });
  }
});


// Tiny wire helpers, scoped to the debug endpoint above. DEBUG — remove with endpoint.
// O3 (Phase 4 bench round): render the OSPI-erased sentinel 0xFFFFFFFF (or 0
// for an unset field) as '?' so debug responses + audit logs don't surface
// "boot_count: 4294967295" for fleet members whose LP isn't tracking it.
function fmtBootCount(n: number): string {
  if (n === 0xFFFFFFFF || n === undefined || n === 0) return '?';
  return String(n);
}
function encVarint(v: number): Buffer {
  const out: number[] = [];
  do { let b = v & 0x7F; v >>>= 7; if (v > 0) b |= 0x80; out.push(b); } while (v > 0);
  return Buffer.from(out);
}
function encField(field: number, wireType: number, payload: Buffer): Buffer {
  const tag = encVarint((field << 3) | wireType);
  if (wireType === 0) return Buffer.concat([tag, payload]);          // varint payload pre-encoded
  if (wireType === 2) return Buffer.concat([tag, encVarint(payload.length), payload]);
  throw new Error(`unsupported wireType ${wireType}`);
}

// Mount API routes at /iot (matching the UI's fetch URLs)
const VFD_ENABLED = (process.env.VFD_ENABLED ?? 'false').toLowerCase() === 'true';
const VFD_HOST = process.env.VFD_HOST ?? '127.0.0.1';
const VFD_PORT = Number(process.env.VFD_PORT ?? '5020');
const VFD_MAX_SCAN = Number(process.env.VFD_MAX_SCAN ?? '8');
const vfdClient: VFDClient | null = VFD_ENABLED
  ? new VFDClient({ host: VFD_HOST, port: VFD_PORT, maxScanId: VFD_MAX_SCAN, pollIntervalMs: 1000 })
  : null;

// -- Orbit Modbus TCP path --
// REMOVED 2026-04-25. The bridge is now a TRANSPARENT TRANSPORT GATEWAY
// only. The LP AM2434 firmware is the sole Modbus TCP client to the
// orbit boards (host 10.1.2.230, ports 5502+, 1 s poll); it forwards
// sensor banks (envelope tag 124) and orbit status (tag 120) over the
// COBS+protobuf serial link. Bridge-side polling, sensor-overlay,
// equipment-output coil writes, REST topology probing, and Triton master
// broadcast are all gone � the firmware owns the data path end-to-end.
//
// Triton control writes (POST /iot/triton/{slot}/{manual,setpoints,...})
// currently return 503 until the LP firmware gains a proto-side
// "write Triton register" handler. See /memories/repo/triton-write-pending.md.
//
// Removed env vars (no longer read): ORBIT_ENABLED, ORBIT_HOST,
// ORBIT_BASE_PORT, DEV_ORBIT_PROBE.

/**
 * Sync equipment status outputs to orbit boards.
 * REMOVED 2026-04-25 � LP firmware writes orbit DO coils directly.
 *
 * Helpers deleted: EQ_TO_STATUS map, overlaySensorData(),
 * syncEquipOutputsViaModbus(). The bridge no longer touches Modbus on
 * the orbit side; sensor values flow through OrbitSensorBank (tag 124)
 * and DO state flows through firmware coil writes.
 */

// In NOVA mode, create a shim that translates legacy sendPost/sendCommand to protobuf
// commands. After Phase 5.1 (Apr 2026) only a thin slice of the original
// shim survives � system commands (ClearAlarm/SetDefault/...), equipment
// remote-off toggles, refrig diagnostic buttons, and the IO rename
// passthrough. Every settings-page POST has been migrated to a direct
// `/proto/write/<field>` call from the SvelteKit UI, so the legacy
// CSV ? protobuf encoders that used to live here have been removed.
// What remains intact:
//   - `buildSettingsUpdate(field, innerBuf)`: still used by the generic
//     `/proto/write/<field>` raw passthrough handler below.
//   - `commandDispatcher.sendPost`: handles only the still-reachable firstTags
//     (system commands, *Btn equipment toggles, refr/defrost buttons,
//     ioRename, FanRemoteOff).

/**
 * Build a PB-encoded SettingsUpdate submessage for a given settings type.
 * settingsField: the oneof field number in SettingsUpdate (1-36)
 * innerBuf: pre-encoded protobuf bytes for the inner settings message
 */
function buildSettingsUpdate(settingsField: number, innerBuf: Buffer): Buffer {
  const parts: Buffer[] = [];
  // Write tag for the settings field (length-delimited = wire type 2)
  const tag = (settingsField << 3) | 2;
  const tagBytes: number[] = [];
  let t = tag;
  while (t > 0x7F) { tagBytes.push((t & 0x7F) | 0x80); t >>>= 7; }
  tagBytes.push(t & 0x7F);
  parts.push(Buffer.from(tagBytes));
  // Write length
  const lenBytes: number[] = [];
  let l = innerBuf.length;
  while (l > 0x7F) { lenBytes.push((l & 0x7F) | 0x80); l >>>= 7; }
  lenBytes.push(l & 0x7F);
  parts.push(Buffer.from(lenBytes));
  parts.push(innerBuf);
  return Buffer.concat(parts);
}

const commandDispatcher: any = {
  sendPost: async (body: string) => {
    const parts = body.split('&');
    const fields: Record<string, string> = {};
    // Parse all fields
    for (const part of parts) {
      const eq = part.indexOf('=');
      if (eq < 0) continue;
      fields[part.slice(0, eq)] = part.slice(eq + 1);
    }

    const firstTag = Object.keys(fields)[0] ?? '';
    const firstVal = fields[firstTag] ?? '';

    // -- System commands --
    if (firstTag === 'ClearAlarm') {
      await novaBridge!.sendSystemCmd(1); return;
    }
    if (firstTag === 'SetDefault' || firstTag === 'SaveSettings') {
      await novaBridge!.sendSystemCmd(2); return;
    }
    if (firstTag === 'PanelDefault') {
      await novaBridge!.sendSystemCmd(3); return;
    }
    if (firstTag === 'FactoryDefault') {
      await novaBridge!.sendSystemCmd(4); return;
    }
    if (firstTag === 'ClearDiag') {
      await novaBridge!.sendSystemCmd(6); return;
    }
    if (firstTag === 'ResetIoConfig') {
      await novaBridge!.sendSystemCmd(8); return;
    }

    // (Apr 2026 proto-direct migration: removed `firstTag === 'NextAux'`
    // dispatch. The aux page no longer issues NextAux commands; the
    // firmware AuxProgramBundle (tag 72) broadcasts the full set every
    // 5 s instead. See proto-direct-redesign-plan.md.)

    // (S9k cleanup 2026-04-21: removed dead sendPost branches that no
    // caller in apiRoutes / UI ever sends:
    //   - FindBoard  (djangoSync.ts uses sendSystemCmd(7) directly)
    //   - NextBoard / PrevBoard / SameBoard / PrevAux  (no callers)
    //   - Button     (UI never POSTs ButtonId; bridge branch removed too)
    // )

    // -- Equipment commands --
    if (firstTag === 'FanRemoteOff') {
      // Called by apiRoutes /settings/restore.  Same as fanBtn (RO_FAN).
      await novaBridge!.sendEquipmentCmd(RO.FAN, parseInt(firstVal, 10) || 0);
      return;
    }
    // Equipment switch buttons from Svelte UI: "fanBtn=Auto", "climacellBtn=Off",
    // "lights1Btn=Toggle", "aux3Btn=On", etc.  Constellation has no physical
    // panel switches � these software switches replace them and route through
    // MSG_EQUIPMENT_CMD with the legacy EQ_REMOTE_OFF index.  See
    // `equipmentIds.ts` for the authoritative tag?slot mapping (mirrors
    // `UI_EquipPosts[]` in the AS2 firmware).
    if (firstTag.endsWith('Btn') && firstTag in BUTTON_TO_RO) {
      const eqIdx = BUTTON_TO_RO[firstTag];
      let newState: number;
      if (firstTag === 'lights1Btn' || firstTag === 'lights2Btn') {
        // Lights are toggle-only in legacy (`ToggleRemoteOff`).  Read
        // current state from the equipment-status mirror and flip it.
        const cur = novaStore?.equipmentStatus?.items.find(
          e => e.eqIndex === eqIdx,
        );
        newState = lightsButtonNextState(cur?.remoteOff ?? REMOTE_AUTO);
      } else {
        newState = decodeRemoteOffValue(firstVal);
      }
      await novaBridge!.sendEquipmentCmd(eqIdx, newState);
      console.log(
        `[CmdDispatch] ${firstTag} = ${firstVal} ? equipCmd(${eqIdx}, ${newState})`,
      );
      return;
    }
    // Per-stage refrigeration diagnostic buttons:
    //   refr1Btn..refr8Btn   ? Settings.Refrig.Stage[0..7].Diagnostic
    //   defrost1Btn,defrost2Btn ? Settings.Refrig.Defrost[0..1].Diagnostic
    // Value semantics mirror StoreRefrigDiag(): "On" = engage diag (=2);
    // anything else = clear diag (=1).  Cycle order is enforced firmware-side.
    {
      const refrMatch = /^refr([1-8])Btn$/.exec(firstTag);
      const defMatch  = /^defrost([12])Btn$/.exec(firstTag);
      if (refrMatch || defMatch) {
        const stageKind = refrMatch ? 0 : 1;
        const idx       = (refrMatch ? parseInt(refrMatch[1], 10)
                                     : parseInt(defMatch![1], 10)) - 1;
        const value     = firstVal === 'On' ? 2 : 1;
        await novaBridge!.sendRefrigDiagCmd(stageKind, idx, value);
        console.log(
          `[CmdDispatch] ${firstTag}=${firstVal} ? refrigDiag(kind=${stageKind}, idx=${idx}, val=${value})`,
        );
        return;
      }
    }

    // (S9k cleanup 2026-04-21: removed unreachable `firstTag === 'Button'`
    // branch � the /iot/button bridge handler no longer accepts the
    // `body.ButtonId` body shape that produced this firstTag.)

    // -- System start/stop (home page Start/Stop button) --
    // UI shape: { tag: 'button2', remoteStop: 'Stop'|'Start' } (lowercase
    // 'r' � case matches the UI literal in routes/+page.svelte::remoteOff).
    // 'Stop'  ? engage SYSTEM_STOP (latching; firmware enters CurrentMode=20)
    // 'Start' ? clear SYSTEM_STOP via CMD_CLEAR_ALARM
    // Mirrors the cloud-command-queue `RemoteStop` handler in
    // executeNovaCommand() above, which is for Django-pushed commands
    // and uses '1'/'0' instead of 'Stop'/'Start'. Don't unify the two �
    // the UI literal is part of the legacy AS2 RTS/ACK shape and is
    // load-bearing across many cached front-matter parsers.
    if (firstTag === 'remoteStop') {
      if (firstVal === 'Stop') {
        await novaBridge!.sendSystemStop();
        console.log('[CmdDispatch] remoteStop=Stop ? SYSTEM_STOP engaged');
      } else {
        await novaBridge!.clearSystemStop();
        console.log(`[CmdDispatch] remoteStop=${firstVal} ? CLEAR_ALARM (system resumed)`);
      }
      return;
    }

    // (Apr 2026 IoNameUpdate proto-direct migration: removed the
    // `firstTag === 'ioRename'` dispatch. The level2/ioconfig page now
    // writes IoNameUpdate via /proto/write/40 and Nova firmware
    // LpSettings_ApplyIoName persists it directly. See
    // /memories/repo/io-name-update.md.)

    // -- Door diagnostic clear (DoorDiagRow "Clear" button) --
    // AS2 itself never wired a TAG_A_DOORDIAGCLEAR; the diag accumulator
    // was actually cleared by the same path the refrig diag clear used.
    // Route to firmware CMD_CLEAR_DIAG (=6 ? CtrlRefrigDiagClear), which
    // already wipes the per-door diag rows on the equipment table.
    if (firstTag === 'ClearDoorDiag') {
      await novaBridge!.sendSystemCmd(6 /* CMD_CLEAR_DIAG */);
      console.log('[CmdDispatch] ClearDoorDiag ? CMD_CLEAR_DIAG');
      return;
    }

    // -- PG log clears (level2/log + level2/pid clear buttons) --
    // On AS2 these reset the SD-card ring buffer (UserLogReset /
    // SystemLogReset / PIDLogReset). On Constellation the rows live in
    // the rpi5 Postgres `paneldb` (see /memories/repo/pg-logging-on-rpi5.md),
    // so the clear is a TRUNCATE that bypasses firmware entirely.
    // novaLogStore is always non-null in production startup (constructed
    // unconditionally above); the optional-chain is defensive only.
    if (firstTag === 'ClearUserLog') {
      await novaLogStore?.logDb.clearUserLog();
      console.log('[CmdDispatch] ClearUserLog ? TRUNCATE user_log (cascade sensor_log)');
      return;
    }
    if (firstTag === 'ClearSystemLog') {
      await novaLogStore?.logDb.clearActivityLog();
      console.log('[CmdDispatch] ClearSystemLog ? TRUNCATE activity_log');
      return;
    }
    if (firstTag === 'PIDClearLog') {
      await novaLogStore?.logDb.clearPidLog();
      console.log('[CmdDispatch] PIDClearLog ? TRUNCATE pid_log');
      return;
    }

    // -- Settings updates ----------------------------------------------
    // S9k cleanup (Phase 5.1 finale, 2026-04-20): every settings page
    // now writes protobuf directly via /proto/write/<field> from the
    // SvelteKit UI. The legacy CSV ? protobuf shims that lived here
    // for fields 1..43 (plenum/burner/co2/refrigeration/failures/...
    // /aux/analog/pwm/etc.) have been deleted. The historical migration
    // notes that documented each conversion are preserved in the git
    // history of this file plus
    // /memories/repo/proto-migration-state.md. If you need to wire a
    // new save path, do it in the Svelte page � DO NOT add a CSV
    // handler here.
    //
    // Anything that lands at the unhandled-warning below means a
    // /iot/button or /iot/PostSave.jsp body slipped through with a
    // firstTag this shim doesn't recognise � investigate the caller.

    console.warn(`[NOVA] Unhandled legacy POST: ${firstTag}=${firstVal.slice(0, 40)}`);
  },
  on: (..._args: any[]) => {},
  open: async () => {},
  close: () => {},
};

// ── Typed proto forwards (NOT commands — these are direct passthroughs
//    from REST handlers in apiRoutes to typed `novaBridge.send*` calls,
//    grouped here only because the `CommandBridge` interface in
//    apiRoutes.ts exposes them on the same object). ──
const protoForwards = {
  /* POST /iot/orbits/role → envelope tag 122 OrbitRoleAssign. */
  assignOrbitRole: async (slot: number, role: number, opts: { zoneId?: number; legacySlot?: number; refrigStage?: number } = {}) => {
    await novaBridge!.sendOrbitRoleAssign(slot, role, opts);
  },
  /* POST /iot/orbits/aoequip → persists Settings.AoEquip[slot][channel]
   * in OSPI via SettingsUpdate. */
  assignAoEquip: async (slot: number, channel: number, equip: number) => {
    await novaBridge!.sendAoEquipAssign(slot, channel, equip);
  },
  /* POST /iot/triton/{slot}/{...} → envelope tag 125 TritonRegWrite,
   * forwarded by the LP firmware's Modbus TCP client. */
  tritonRegWrite: async (slot: number, addr: number, value: number) => {
    await novaBridge!.sendTritonRegWrite(slot, addr, value);
  },
  /* POST /iot/gdc/{stages,calibrate} → envelope tag 126 OrbitRegWrite.
   * Nova picks FC06 for 1 value or FC16 for a block. Phase 4b Sub-2. */
  orbitRegWrite: async (slot: number, addr: number, values: number[]) => {
    await novaBridge!.sendOrbitRegWrite(slot, addr, values);
  },
  /* POST /iot/system/reboot → SystemCmd { cmd_type=CMD_REBOOT_SOC=50 }.
   * Firmware acks then warm-resets the SoC via DMSC; the bridge's
   * Nova reconnect logic surfaces the new image automatically. */
  rebootSoc: async () => {
    await novaBridge!.sendRebootSoc();
  },
  /* POST /iot/settings/import — replays a previously-exported settings
   * blob back into firmware via SettingsUpdate writes. The blob is the
   * JSON shape produced by GET /iot/settings/export (see apiRoutes.ts):
   *   { fields: [{ settingsField: <int>, b64: <base64 inner bytes> }, ...] }
   *
   * Each entry replays through the same NovaProto_SendRaw mutex used by
   * the per-page /proto/write/<n> endpoint — transparent passthrough,
   * firmware decoder is the only authority. */
  importSettings: async (entries: { settingsField: number; b64: string }[]): Promise<{ ok: number; total: number; failed: { settingsField: number; error: string }[] }> => {
    const failed: { settingsField: number; error: string }[] = [];
    let ok = 0;
    for (const e of entries) {
      const f = e.settingsField | 0;
      if (!Number.isFinite(f) || f <= 0 || f > 1023) {
        failed.push({ settingsField: f, error: 'invalid_settings_field' });
        continue;
      }
      let inner: Buffer;
      try { inner = Buffer.from(e.b64, 'base64'); }
      catch (err: any) {
        failed.push({ settingsField: f, error: 'invalid_base64: ' + err.message });
        continue;
      }
      try {
        await novaBridge!.sendSettingsUpdate(buildSettingsUpdate(f, inner));
        ok++;
        /* Pace the flood. The firmware processes each SettingsUpdate
         * end-to-end (decode → apply → OSPI bank write → re-broadcast)
         * before the next one is safe to send; back-to-back saves can
         * overflow the RX FIFO on the LP UART2 path. 60 ms matches
         * the per-page save cadence the UI uses for ioconfig. */
        await new Promise((r) => setTimeout(r, 60));
      } catch (err: any) {
        failed.push({ settingsField: f, error: err?.message ?? String(err) });
      }
    }
    return { ok, total: entries.length, failed };
  },
};

const apiSerialBridge = { ...commandDispatcher, ...protoForwards };
app.use('/iot', createApiRoutes(dataCache, apiSerialBridge, upgradeManager, novaLogStore ?? undefined, vfdClient ?? undefined, novaFwManager ?? undefined, undefined, novaStore, masterSlaveSync, remoteSystemsSync));

// --- Phase 0.4: /proto/write/:settingsField ---------------------------
// Direct proto-write endpoint. UI POSTs raw protobuf-encoded bytes for
// the inner SettingsUpdate submessage; bridge wraps them as field
// `:settingsField` of SettingsUpdate and forwards through the existing
// NovaProto_SendRaw mutex via novaBridge.sendSettingsUpdate().
//
// :settingsField � numeric SettingsUpdate oneof field (1=Plenum,
//   2=FanSpeed, 33=Runtimes, 35=AlertSetup, etc � see envelope.proto
//   SettingsUpdate oneof and Nova_Firmware/Platform/nova_dataexc.c
//   handle_settings_update() switch).
//
// Body � application/octet-stream, max 64 KiB. Must be a valid encoded
//   proto submessage; bridge does NOT decode/inspect it (transparent
//   passthrough � firmware decoder is the only authority).
//
// Response � { ok: true } on success, { ok: false, error: ... } on
//   transport/timeout failure. Cache update happens via the firmware's
//   subsequent settings re-broadcast; clients should subscribe via
//   /proto/stream and rely on the proto-store push, not the response.
app.post(
  '/proto/write/:settingsField',
  express.raw({ type: 'application/octet-stream', limit: '64kb' }),
  async (req, res) => {
    const field = parseInt(req.params.settingsField, 10);
    if (!Number.isFinite(field) || field <= 0 || field > 1023) {
      return res.status(400).json({ ok: false, error: 'invalid_settings_field' });
    }
    if (!novaBridge) {
      return res.status(503).json({ ok: false, error: 'nova_bridge_not_ready' });
    }
    const body: Buffer = Buffer.isBuffer(req.body) ? req.body : Buffer.alloc(0);
    // NOTE: empty bodies are intentionally allowed. For wire-authoritative
    // replace handlers (AlertSettings, BayName, IoConfig, Account, �)
    // an empty payload is the legitimate "wipe this list" signal �
    // ts-proto encodes a cleared `repeated` as zero bytes. The firmware
    // decoders interpret len==0 as "clear" (see lp_settings.c).
    // Scalar handlers see len==0 as a no-op (returns false ? no save).
    if (body.length === 0) {
      console.log(`[proto/write/${field}] empty payload (interpreted as wipe by firmware)`);
    }
    /* TEMP DEBUG: hex-dump IoConfig (SettingsUpdate field 39) saves so we
     * can see exactly what the UI sent when a save mysteriously reverts. */
    if (field === 39) {
      const hex = body.toString('hex').match(/.{1,2}/g)?.join(' ') ?? '';
      console.log(`[proto/write/39 IoConfig] ${body.length}B: ${hex}`);
    }
    try {
      await novaBridge.sendSettingsUpdate(buildSettingsUpdate(field, body));
      // -- Accountability ---------------------------------------------
      // Every successful settings write is logged with the currently
      // authenticated actor + slot + access level. NDJSON file (via
      // accountStore) and Postgres `audit_log` table (via novaLogStore)
      // both receive the entry � file = portable export, db = queryable.
      try {
        const auth = getCurrentAuth();
        const route = `proto/write/${field}`;
        const detail = `SettingsUpdate field=${field} bytes=${body.length}`;
        const ip = (req.ip || req.socket?.remoteAddress || '') as string;
        recordSave(auth.actor, auth.slot, auth.level, route, detail, ip);
        if (novaLogStore) {
          void novaLogStore.logDb.insertAudit({
            kind: 'save',
            actor: auth.actor,
            slot: auth.slot,
            level: auth.level,
            route,
            detail,
            ip,
          }).catch(err => console.error('[ProtoWrite] audit pg insert failed:', err.message));
        }
      } catch (auditErr: any) {
        console.error('[ProtoWrite] audit hook error:', auditErr?.message ?? auditErr);
      }
      res.json({ ok: true, field, bytes: body.length });
    } catch (err: any) {
      console.error(`[ProtoWrite] field=${field} failed:`, err.message);
      res.status(503).json({ ok: false, error: err.message });
    }
  }
);

// --- Cloud ? Bridge command push ---
// Django (cloud) calls this to push a command to the controller without waiting
// for the next periodic command-queue poll. Auth: shared bearer token = the
// IoTClient token we already use for outbound sync (DJANGO_TOKEN).
app.post('/api/bridge/command', async (req, res) => {
  const expected = process.env.DJANGO_TOKEN;
  if (!expected) {
    return res.status(503).json({ ok: false, error: 'bridge not configured for cloud push (no token)' });
  }
  const auth = req.header('authorization') || '';
  const presented = auth.replace(/^Bearer\s+/i, '').replace(/^Token\s+/i, '').trim();
  if (presented !== expected) {
    return res.status(401).json({ ok: false, error: 'unauthorized' });
  }

  const { method, payload } = (req.body ?? {}) as { method?: string; payload?: Record<string, unknown> };
  if (!method || !payload || typeof payload !== 'object') {
    return res.status(400).json({ ok: false, error: 'method and payload required' });
  }

  try {
    const result = await djangoSync.executeCommand(method, payload);
    return res.json({ ok: true, result });
  } catch (err: any) {
    console.error('[Bridge] /api/bridge/command error:', err?.message ?? err);
    return res.status(503).json({ ok: false, error: err?.message ?? String(err) });
  }
});

// Optionally serve the SvelteKit UI from this same process.
//
// `SVELTE_BUILD` should point to the @sveltejs/adapter-node build output
// directory (the one containing `handler.js`, `client/`, `server/`,
// `index.js`). When set, we dynamically import the SvelteKit
// `handler` middleware and mount it as the LAST express handler so it
// owns everything not already claimed by /iot, /api, /proto, etc.
//
// This collapses the old two-process layout (uisvelte:3000 + bridge:9001)
// into a single port. The UI's same-origin `/iot/*` fetches now resolve
// without a separate proxy.
if (SVELTE_BUILD) {
  const path = await import('path');
  const url = await import('url');
  const buildPath = path.resolve(SVELTE_BUILD);
  const handlerUrl = url.pathToFileURL(path.join(buildPath, 'handler.js')).href;
  try {
    const mod = await import(handlerUrl);
    if (typeof mod.handler !== 'function') {
      throw new Error(`handler.js at ${handlerUrl} did not export a 'handler' function`);
    }
    app.use(mod.handler);
    console.log(`[Server] Mounted SvelteKit handler from ${buildPath}`);
  } catch (err: any) {
    console.error(`[Server] Failed to mount SvelteKit handler from ${buildPath}: ${err?.message ?? err}`);
  }
}

// Attach WebSocket server
const wsManager = new WsManager(httpServer, dataCache);

// Wire UpgradeManager to WebSocket manager for real-time upgrade status broadcasts
wsManager.attachUpgradeManager(upgradeManager);

// Wire the bridge-managed remote-systems list into the `tcpip-data`
// channel so the header selector picks up DHCP-resilient additions
// instantly (instead of on the firmware-driven P2NodeSetupData path).
wsManager.attachRemoteSystemsSync(remoteSystemsSync);

// Wire the firmware installer's progress broadcasts (firmwareInstaller.ts)
// onto the `firmware-progress` WS channel so the version-page UI sees
// per-component state changes during a .cfu install.
import { setFirmwareProgressBroadcast } from './apiRoutes.js';
setFirmwareProgressBroadcast((data: unknown) => {
  wsManager.broadcast('firmware-progress', data);
});

// Phase 4 (UART-airgap migration) — inject the Nova bridge handle into
// firmwareInstaller so it can drive the broker via FwInstall* envelopes
// over UART instead of direct TCP to LP :5503. See
// docs/uart-airgap-architecture.md §"Phase 4".
import { setNovaBridge as setInstallerBridge } from './firmwareInstaller.js';
setInstallerBridge(novaBridge);

// Phase 0.3+ proto-direct binary stream. Subscribers receive raw nanopb
// frames keyed by envelope tag (firmware tags 10�129 + bridge-emitted
// 200+).  This is the SOLE feed for typed proto stores in the SvelteKit
// UI � without it, $frontMatterComposite never produces data and most
// pages get filtered out by the visibility gate in paging.ts.
const protoStream = new ProtoStream(httpServer, { store: novaStore, vfdClient });

// HTTP-server upgrade dispatcher.  Both `WsManager` (`/iot/ws`) and
// `ProtoStream` (`/proto/stream`) run in `noServer: true` mode so we
// can host two distinct WS apps on the same port.  Without this
// dispatcher every WS upgrade dies with ECONNRESET (Node closes the
// socket because no listener consumed the upgrade event).
httpServer.on('upgrade', (req, socket, head) => {
  const url = req.url ?? '';
  if (url.startsWith(protoStream.path)) {
    protoStream.handleUpgrade(req, socket, head);
  } else if (url.startsWith(wsManager.path)) {
    wsManager.handleUpgrade(req, socket, head);
  } else {
    // Unknown path � close the socket cleanly so the client doesn't hang.
    socket.destroy();
  }
});

// --- Wire Nova bridge ? cache + WebSocket ---

{
  // -- NOVA PROTOCOL: wire protobuf bridge ? data store ? legacy cache --
  createNovaAdapter(novaBridge, novaStore, dataCache, wsManager);
  novaBridge.on('error', (err: Error) => {
    console.error('[NovaBridge] Error:', err.message);
  });

  // -- RTC auto-sync on connect --
  // The AM2434 has no battery-backed RTC (see /memories/repo/nova-rtc.md);
  // boot lands at 01/01/1900 12:00:00. Push the host wall-clock as soon
  // as the bridge sees its first protocol-version frame so any subsequent
  // log inserts (activity_log, user_log, pid_log, load_log) carry real
  // timestamps. Without this, every record stamps as 1900-01-01.
  // Re-fires on every reconnect — cheap, and keeps cold-boot timestamps
  // honest after any LP reset / power cycle.
  novaBridge.on('connected', () => {
    try {
      const now = new Date();
      const mm = String(now.getMonth() + 1).padStart(2, '0');
      const dd = String(now.getDate()).padStart(2, '0');
      const yyyy = String(now.getFullYear());
      const dateStr = `${mm}/${dd}/${yyyy}`;
      const hh24 = now.getHours();
      const amPm = hh24 >= 12 ? 1 : 0;
      // RTC.c::SetDateTimeStr expects 12 for both noon and midnight.
      const h12 = (hh24 % 12) || 12;
      const timeStr = `${String(h12).padStart(2, '0')}:`
                    + `${String(now.getMinutes()).padStart(2, '0')}:`
                    + `${String(now.getSeconds()).padStart(2, '0')}`;
      // Encode DateTimeUpdate{1=dateStr, 2=timeStr, 3=amPm} inline.
      // (The bridge does not import generated/ts; hand-rolled keeps
      // index.ts dependency-free and matches the buildSettingsUpdate
      // pattern used elsewhere in this file.)
      const enc: Buffer[] = [];
      const writeStr = (fnum: number, s: string) => {
        const buf = Buffer.from(s, 'utf8');
        enc.push(Buffer.from([(fnum << 3) | 2, buf.length]));
        enc.push(buf);
      };
      const writeVarint = (fnum: number, v: number) => {
        enc.push(Buffer.from([(fnum << 3) | 0]));
        const out: number[] = [];
        let x = v >>> 0;
        while (x > 0x7F) { out.push((x & 0x7F) | 0x80); x >>>= 7; }
        out.push(x & 0x7F);
        enc.push(Buffer.from(out));
      };
      writeStr(1, dateStr);
      writeStr(2, timeStr);
      writeVarint(3, amPm);
      const inner = Buffer.concat(enc);
      void novaBridge.sendSettingsUpdate(buildSettingsUpdate(26, inner))
        .then(() => console.log(`[NovaBridge] RTC synced: ${dateStr} ${timeStr} ${amPm ? 'PM' : 'AM'}`))
        .catch((e: Error) => console.error('[NovaBridge] RTC sync failed:', e.message));
    } catch (err: any) {
      console.error('[NovaBridge] RTC sync prep failed:', err.message);
    }
  });

  // -- VFD Fan Speed Forwarding --
  // Removed: Nova firmware now writes the fan-speed reference directly
  // to each storage orbit's VFD passthrough block (nova_vfd.c::nova_vfd_tick),
  // and to any orbit AO that's mapped to "fan speed" in the IO config
  // (nova_fan_output.c). The bridge MUST NOT forward speed back to the
  // VFDs � that path duplicates control authority and will fight Nova
  // when the rpi5 is offline. The bridge keeps `vfdClient` only for the
  // /level2/fans display proto stream.

  console.log('[Server] Nova bridge wired (COBS+Protobuf)');
}


// --- Start ---

async function start() {
  // Start HTTP server first — the UI/API must be available even if
  // the firmware serial link isn't up yet (LP still booting, etc.).
  httpServer.listen(PORT, () => {
    console.log('');
    console.log('  +----------------------------------------------+');
    console.log('  �    Constellation Bridge Server               �');
    console.log('  �----------------------------------------------�');
    console.log(`  �  HTTP/WS : http://localhost:${PORT}            �`);
    console.log(`  �  Serial  : ${SERIAL_PORT.padEnd(33)}�`);
    console.log(`  �  Baud    : ${String(SERIAL_BAUD).padEnd(33)}�`);
    console.log(`  �  Protocol: ${'NOVA (COBS+Protobuf)'.padEnd(33)}�`);
    console.log(`  �  VFD     : ${VFD_ENABLED ? (VFD_HOST + ':' + VFD_PORT).padEnd(33) : 'disabled'.padEnd(33)}�`);
    console.log(`  �  WS path : /iot/ws                           �`);
    console.log(`  �  REST    : /iot/*                             �`);
    console.log(`  �  Django  : ${process.env.DJANGO_SYNC_ENABLED === 'true' ? (process.env.DJANGO_URL || 'http://localhost:8000').padEnd(33) : 'disabled'.padEnd(33)}�`);
    console.log('  +----------------------------------------------+');
    console.log('');

    // Start Django cloud sync (if enabled)
    djangoSync.start();

    // Start audit forwarder � ships save/login events to Django for dashboards.
    startAuditForwarder();
  });

  // Open serial connection � retry in background if firmware not ready yet.
  // This is non-blocking so the HTTP/WS server stays available for the UI.
  const openSerial = async () => {
    try {
      await novaBridge.open();
      log.info('serial link established');
    } catch (err: any) {
      log.error('serial link not available', { error: err.message || String(err) });
      log.info('retrying serial connection', { delayMs: 5000 });
      setTimeout(openSerial, 5000);
    }
  };
  openSerial();

  // Start VFD Modbus TCP client (non-blocking, will retry connection)
  if (vfdClient) {
    vfdClient.start().then(async () => {
      // Load labels & manufacturers from sim config into client snapshots
      const { loadConfig } = await import('./simConfig.js');
      const saved = loadConfig('vfd-drives');
      if (Array.isArray(saved)) {
        for (const d of saved as any[]) {
          if (d.unitId && (d.label || d.manufacturer)) {
            vfdClient.setDriveMeta(d.unitId, { label: d.label, manufacturer: d.manufacturer });
          }
        }
      }
    }).catch(err => {
      log.error('VFD client start error', { error: err.message });
    });
  }

  // Orbit topology + sensor data: REMOVED 2026-04-25.
  // The LP firmware is the sole Modbus client to the orbit boards. It
  // emits OrbitStatus (envelope tag 120) and OrbitSensorBank (tag 124)
  // over the COBS+protobuf serial link; novaAdapter populates
  // dataCache.orbitBoards / novaStore.orbitSensorBanks from those.
  // Bridge-side discovery probes, sensor overlay, and Triton broadcast
  // are gone � the firmware owns the data path end-to-end.

  // --- VFD updates ? UI -----------------------------------------------
  // Drive snapshots are still polled by vfdClient and fan out to the UI
  // through the vfdStatus proto stream (envelope tag 200) for the
  // /level2/fans display + per-drive fault detail.
  //
  // Fault DETECTION + fan-failure escalation moved to Nova firmware
  // (see Nova_Firmware/Platform/nova_vfd.c and nova_failures.c::FanFailChk).
  // Nova reads each STORAGE-role orbit's HR 100-163 passthrough block,
  // OR's any drive-faulted into FanFailChk's existing condition, and
  // reuses the legacy Settings.Failure[FAIL_FAN].Timer for escalation.
  // The bridge is now a transparent passthrough on this safety path �
  // the system stays safe even when the RPi5 / bridge is offline.
  if (vfdClient) vfdClient.onUpdate(() => {
    wsManager.broadcast('vfd-data');
  });

  // -- VFD Fan Speed Forwarding --
  // VFD fan-speed forwarding for NOVA flows through NovaDataStore events;
  // wired earlier in this file when novaStore is created.  Nothing to do here.
}

// --- Graceful shutdown ---
// Production target is `systemctl restart constellation-bridge`. systemd
// sends SIGTERM, waits TimeoutStopSec (default 90 s), then SIGKILL. Our
// budget here is intentionally well under that � 8 s hard cap � so we
// either flush cleanly or die cleanly, never wedge the unit.
//
// Order of operations matters:
//   1. Stop accepting new HTTP/WS work       (httpServer.close, wsManager.close)
//   2. Stop new firmware traffic              (novaBridge.close)
//   3. Drain in-memory log spool to pg        (logDb.drainSpool)
//   4. Close pg pool                          (logDb.close, via novaLogStore)
//   5. Misc clients                           (vfdClient.stop)
// Everything is best-effort; a hung step can't block the next one
// because each await is wrapped in a per-step try/catch.

let shuttingDown = false;
process.on('SIGINT',  () => { void shutdown('SIGINT');  });
process.on('SIGTERM', () => { void shutdown('SIGTERM'); });

async function shutdown(signal: string): Promise<void> {
  if (shuttingDown) return;
  shuttingDown = true;
  console.log(`\n[Server] Received ${signal}, shutting down...`);

  // Hard cap. If we're still here in 8 s, something is wedged � exit
  // non-zero so systemd records the failure.
  const killer = setTimeout(() => {
    console.error('[Server] Shutdown deadline exceeded; forcing exit.');
    process.exit(1);
  }, 8000);
  killer.unref?.();

  const step = async (label: string, fn: () => unknown) => {
    try { await fn(); }
    catch (err) { console.error(`[Server] ${label} failed:`, (err as Error).message); }
  };

  // 1. Stop ingress.
  await step('http close', () => new Promise<void>((resolve) => httpServer.close(() => resolve())));
  await step('ws close', () => wsManager.close());

  // 2. Stop firmware traffic so spool snapshot is consistent.
  await step('nova bridge close', () => novaBridge.close());

  // 3. Drain the spool � last chance to land buffered log/activity rows
  //    in pg before the pool is torn down.
  await step('log spool drain', () => novaLogStore.logDb.drainSpool());

  // 4. Close pg pool (also clears the retention timer).
  await step('log store close', () => novaLogStore.close());

  // 5. Misc.
  await step('vfd stop', () => vfdClient?.stop());

  console.log('[Server] Bye.');
  process.exit(0);
}

start();
