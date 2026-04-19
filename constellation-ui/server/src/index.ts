/**
 * Constellation Bridge Server — main entry point.
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
 *   PORT           — HTTP listen port (default: 3001)
 *   SERIAL_PORT    — Serial device path or tcp://host:port (default: 'tcp://localhost:9000')
 *   SERIAL_BAUD    — Baud rate (default: 230400)
 *   SVELTE_BUILD   — Path to SvelteKit build output (optional, for serving static files)
 */

import { createServer } from 'http';
import express from 'express';
import cors from 'cors';
import { DataCache } from './dataCache.js';
import { WsManager } from './wsManager.js';
import { createApiRoutes, syncIoConfigToOrbit } from './apiRoutes.js';
import { UpgradeManager } from './upgradeManager.js';
import { loadConfig } from './simConfig.js';
import { VFDClient } from './vfdClient.js';
import { getProfile } from './vfdRegisterMaps.js';
import { OrbitClient, type SensorBoardData } from './orbitClient.js';
import { NovaSerialBridge } from './novaSerialBridge.js';
import { NovaDataStore } from './novaDataStore.js';
import { createNovaAdapter } from './novaAdapter.js';
import { NovaLogDataStore } from './novaLogDataStore.js';
import { NovaFwUpdateManager } from './novaFwUpdateManager.js';
import { createDjangoSync } from './djangoSync.js';
import { startAuditForwarder } from './auditForwarder.js';
import { createLogger } from './logger.js';
import {
  BUTTON_TO_RO,
  RO,
  REMOTE_AUTO,
  REMOTE_OFF,
  decodeRemoteOffValue,
  lightsButtonNextState,
} from './equipmentIds.js';

const log = createLogger('Server');

/* ── Temperature unit helpers ── */
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

// ─── Default Equipment Names ───
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

// ─── Configuration ───

const PORT        = parseInt(process.env.PORT ?? '9001', 10);
const SERIAL_PORT = process.env.SERIAL_PORT ?? 'tcp://localhost:9000';
const SERIAL_BAUD = parseInt(process.env.SERIAL_BAUD ?? '230400', 10);
const SVELTE_BUILD = process.env.SVELTE_BUILD ?? '';

// ─── Initialize components ───

const dataCache    = new DataCache();
const upgradeManager = new UpgradeManager();

// Nova bridge (COBS + nanopb over UART1) — single, authoritative firmware
// transport.  The legacy ASCII RTS/ACK SerialBridge has been retired.
const novaBridge    = new NovaSerialBridge({ port: SERIAL_PORT, baudRate: 921600 });
const novaStore     = new NovaDataStore();
const novaLogStore  = new NovaLogDataStore(novaBridge, dataCache);
const novaFwManager = new NovaFwUpdateManager(novaBridge);
void SERIAL_BAUD; // kept for backwards-compat env parsing only

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

// DjangoSync: push controller data to Django backend (replaces iotclient → Azure IoT Hub)
const djangoSync = createDjangoSync(dataCache);

// Wire up command executor for Django-pushed commands (Nova protobuf).
{
  /**
   * Nova command executor — translates ASCII POST bodies to protobuf commands.
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
              `[NovaCmdExec] ${key}=${value} → equipCmd(${eqIndex}, ${newState})`,
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

// ─── Health Endpoint ───
// Reports bridge connectivity, protocol stats, and uptime for monitoring.
const serverStartTime = Date.now();
app.get('/health', (_req, res) => {
  const novaConnected = novaBridge.isConnected();
  const protocolStats = novaBridge.getProtocolStats();
  const fwVersion = novaBridge.getFirmwareProtocolVersion();
  const uptimeSec = Math.floor((Date.now() - serverStartTime) / 1000);

  const status = novaConnected ? 'healthy' : 'degraded';
  const httpCode = novaConnected ? 200 : 503;

  res.status(httpCode).json({
    status,
    uptime: uptimeSec,
    protocol: 'nova',
    nova: {
      connected: novaConnected,
      firmwareProtocolVersion: fwVersion,
      stats: protocolStats,
    },
    orbit: orbitClient ? { enabled: ORBIT_ENABLED } : undefined,
    vfd: vfdClient ? { enabled: VFD_ENABLED, host: VFD_HOST, port: VFD_PORT } : undefined,
  });
});

// ─── System Stop Endpoints ───
// Emergency system stop — latching, requires explicit clear.
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

// Mount API routes at /iot (matching the UI's fetch URLs)
const VFD_ENABLED = (process.env.VFD_ENABLED ?? 'false').toLowerCase() === 'true';
const VFD_HOST = process.env.VFD_HOST ?? '127.0.0.1';
const VFD_PORT = Number(process.env.VFD_PORT ?? '5020');
const VFD_MAX_SCAN = Number(process.env.VFD_MAX_SCAN ?? '8');
const vfdClient: VFDClient | null = VFD_ENABLED
  ? new VFDClient({ host: VFD_HOST, port: VFD_PORT, maxScanId: VFD_MAX_SCAN, pollIntervalMs: 1000 })
  : null;

// ── Orbit Modbus TCP Client ──
// Communicates with Orbit I/O boards via Modbus TCP (like the real Nova firmware)
const ORBIT_ENABLED = (process.env.ORBIT_ENABLED ?? 'true').toLowerCase() === 'true';
const ORBIT_HOST = process.env.ORBIT_HOST ?? '127.0.0.1';
const ORBIT_BASE_PORT = Number(process.env.ORBIT_BASE_PORT ?? '5500'); // Orbit N uses port 5500+N
const orbitClient: OrbitClient | null = ORBIT_ENABLED ? new OrbitClient({ pollIntervalMs: 500 }) : null;

// Add default orbit board (ID 2 = main board) and GDC board (ID 3)
if (orbitClient) {
  orbitClient.addBoard({ id: 2, host: ORBIT_HOST, port: ORBIT_BASE_PORT + 2 });
  orbitClient.addBoard({ id: 3, host: ORBIT_HOST, port: ORBIT_BASE_PORT + 3 });
}

/**
 * Sync equipment status outputs to orbit boards via Modbus TCP.
 * Reads the actual OutputConfig (port-indexed) to determine which equipment
 * is assigned to each DO, then checks EquipStatusData for that equipment's
 * output state.  This way the orbit LEDs match whatever the user configured
 * in the IO Config page — not a hardcoded table.
 *
 * EquipStatusData index mapping (from buildEquipStatus):
 *   EQ_FAN(0)→eq[2], EQ_CLIMACELL(3)→eq[5], EQ_HEAT(4)→eq[28],
 *   EQ_CAVITY_HEAT(5)→eq[32], EQ_BURNER(6)→eq[7],
 *   EQ_HUMID_HEAD1(7)→eq[10], EQ_HUMID_PUMP1(8)→eq[11],
 *   EQ_HUMID_HEAD2(9)→eq[13], EQ_HUMID_PUMP2(10)→eq[14],
 *   EQ_HUMID_HEAD3(11)→eq[10] (reuse), EQ_HUMID_PUMP3(12)→eq[11] (reuse),
 *   EQ_REFRIG_STAGE1-8(13-20)→eq[17-24],
 *   EQ_REFRIG_DEFROST1(21)→eq[25], EQ_REFRIG_DEFROST2(22)→eq[26],
 *   EQ_LIGHTS1(23)→panel bay lights, EQ_LIGHTS2(24)→panel bay lights,
 *   EQ_REDLIGHT(38)→eq[35], EQ_YELLOWLIGHT(39)→eq[34],
 *   EQ_PULSEDOOR_OPEN(41)→eq[30]==2, EQ_PULSEDOOR_CLOSE(42)→eq[30]==1
 */
// Map from equipment ID (EQ enum) to EquipStatusData index + ON value
const EQ_TO_STATUS: Record<number, { idx: number; onVal?: string }> = {
  0:  { idx: 2 },              // FAN → eq[2]
  3:  { idx: 5 },              // CLIMACELL → eq[5]
  4:  { idx: 28 },             // HEAT → eq[28]
  5:  { idx: 32 },             // CAVITY_HEAT → eq[32]
  6:  { idx: 7 },              // BURNER → eq[7]
  7:  { idx: 10 },             // HUMID_HEAD1 → eq[10]
  8:  { idx: 11 },             // HUMID_PUMP1 → eq[11]
  9:  { idx: 13 },             // HUMID_HEAD2 → eq[13]
  10: { idx: 14 },             // HUMID_PUMP2 → eq[14]
  11: { idx: 10 },             // HUMID_HEAD3 (shares eq[10] pattern)
  12: { idx: 11 },             // HUMID_PUMP3 (shares eq[11] pattern)
  13: { idx: 17 },             // REFRIG_STAGE1 → eq[17]
  14: { idx: 18 },             // REFRIG_STAGE2
  15: { idx: 19 },             // REFRIG_STAGE3
  16: { idx: 20 },             // REFRIG_STAGE4
  17: { idx: 21 },             // REFRIG_STAGE5
  18: { idx: 22 },             // REFRIG_STAGE6
  19: { idx: 23 },             // REFRIG_STAGE7
  20: { idx: 24 },             // REFRIG_STAGE8
  21: { idx: 25 },             // REFRIG_DEFROST1 → eq[25]
  22: { idx: 26 },             // REFRIG_DEFROST2 → eq[26]
  38: { idx: 35 },             // REDLIGHT → eq[35]
  39: { idx: 34 },             // YELLOWLIGHT → eq[34]
  41: { idx: 30, onVal: '2' }, // PULSEDOOR_OPEN → eq[30]=='2'
  42: { idx: 30, onVal: '1' }, // PULSEDOOR_CLOSE → eq[30]=='1'
};

/**
 * Overlay orbit sensor board data into the bridge cache.
 * The orbit reads real sensor boards via RS-485 Modbus RTU, storing values
 * in holding registers 200+.  This function takes parsed board data and
 * overwrites MainData fields (indices 0-9) in the cache so that
 * buildFrontmatterData() uses orbit-sourced values instead of the ARM
 * simulator's physics-engine estimates.
 *
 * Orbit register value format:
 *   Temp: °C × 10 (int16),  Humid: % × 10 (int16),  CO2: raw ppm
 *
 * MainData field mapping (raw °C, matching ARM buildMain()):
 *   [0]=PlenumTemp, [1]=PlenumHumid, [2]=OutsideTemp, [3]=RemoteTemp,
 *   [4]=OutsideHumid, [5]=RemoteHumid, [6]=StartTemp, [7]=ReturnHumid,
 *   [8]=ReturnTemp, [9]=Co2
 */
function overlaySensorData(sensorBoards: SensorBoardData[], cache: DataCache): void {
  const UNDEF = 0x7FFF;
  const entry = cache.getByVarName('MainData');
  if (!entry?.value) return;
  const fields = entry.value.split(',');
  if (fields.length < 10) return;

  let changed = false;

  for (const b of sensorBoards) {
    if (!b.present) continue;
    for (let si = 0; si < 4; si++) {
      const raw = b.sensorValues[si];
      if (raw === UNDEF || raw === undefined) continue;
      const sType = b.sensorTypes[si];

      if (b.address === 0) {
        // Temperature board (type 3): values ×10 → °C
        const degC = (raw / 10).toFixed(1);
        if (si === 0) { fields[0] = degC; changed = true; }       // Plenum 1
        // si === 1: Plenum 2 (UI averages with Plenum 1 via SensorListData)
        else if (si === 2) { fields[2] = degC; changed = true; }  // Outside
        else if (si === 3) { fields[8] = degC; changed = true; }  // Return
      } else if (b.address === 1) {
        if (sType === 1) {
          // Humidity: ×10 → %
          const pct = Math.round(raw / 10).toString();
          if (si === 0) { fields[4] = pct; changed = true; }      // Outside RH
          else if (si === 1) { fields[1] = pct; changed = true; } // Plenum RH
          else if (si === 2) { fields[7] = pct; changed = true; } // Return RH
        } else if (sType === 2) {
          // CO2: raw ppm
          fields[9] = Math.round(raw).toString(); changed = true;
        }
      }
    }
  }

  if (changed) {
    cache.updateFromArm('main', fields.join(','));
  }

  // Also overlay SensorListData (stride 6: Label,SID,Type,Value,Offset,Disabled)
  // so sensor detail pages and fallback logic use orbit values.
  const slEntry = cache.getByVarName('SensorListData');
  if (!slEntry?.value) return;
  const slFields = slEntry.value.split(',');
  let slChanged = false;

  for (const b of sensorBoards) {
    if (!b.present) continue;
    for (let si = 0; si < 4; si++) {
      const raw = b.sensorValues[si];
      if (raw === UNDEF || raw === undefined) continue;
      const sid = b.address * 4 + si;
      const sType = b.sensorTypes[si];

      // Find this SID in SensorListData — stride 6, field[1] = SID
      for (let i = 1; i < slFields.length; i += 6) {
        if (parseInt(slFields[i], 10) === sid) {
          // field[i+2] = value
          if (sType === 2) {
            // CO2: raw ppm
            slFields[i + 2] = Math.round(raw).toString();
          } else {
            // Temp/Humid: ×10 → engineering value
            slFields[i + 2] = (raw / 10).toFixed(1);
          }
          slChanged = true;
          break;
        }
      }
    }
  }

  if (slChanged) {
    cache.updateFromArm('SensorList', slFields.join(','));
  }
}

function syncEquipOutputsViaModbus(cache: DataCache, client: OrbitClient): void {
  const eqRaw = cache.getByVarName('EquipStatusData')?.value ?? '';
  const eq = eqRaw.split(',');
  if (eq.length < 36) return;

  // Read port-indexed OutputConfig: portArr[portId] = equipmentId (or '-1')
  const ocRaw = cache.getByVarName('IoConfigOutData')?.value ?? '';
  const portCfg = ocRaw ? ocRaw.split(',') : [];

  // For each orbit board, resolve its 10 DO states from the OutputConfig
  const boards = cache.getOrbitBoards?.() ?? [];
  const boardCount = Math.max(boards.length, 1); // at least the main board

  for (let board = 0; board < boardCount; board++) {
    const outputs: boolean[] = [];
    // Each board has 10 DO pins.  Port IDs are: board*12 + pin + 1 (pin 0-9)
    for (let pin = 0; pin < 10; pin++) {
      const pid = board * 12 + pin + 1;
      const eqId = parseInt(portCfg[pid] ?? '-1', 10);
      if (eqId < 0) {
        outputs.push(false);
        continue;
      }
      const mapping = EQ_TO_STATUS[eqId];
      if (!mapping) {
        // Equipment type not yet mapped (aux, lights, etc.) — leave OFF
        outputs.push(false);
        continue;
      }
      const val = eq[mapping.idx] ?? '0';
      outputs.push(mapping.onVal ? val === mapping.onVal : val === '1');
    }
    // Orbit board IDs start at 2 (MAIN=2, EXP_1=3, ...)
    client.setOutputs(board + 2, outputs);
  }

  // ── GDC: Write coolOutput → register 300 (freshAirDoorPct × 10) ──
  // MainData[11] = coolOutput (0-100%) from ARM sim door PID
  const mainRaw = cache.getByVarName('MainData')?.value ?? '';
  const mainFields = mainRaw.split(',');
  const coolOutput = parseInt(mainFields[11] ?? '0', 10) || 0;
  // GDC register 300 expects 0-1000 (pct × 10)
  client.writeHoldingRegister(3, 300, coolOutput * 10);
}

// In NOVA mode, create a shim that translates legacy sendPost/sendCommand to protobuf
// commands. This allows the existing apiRoutes to work until they're migrated.
// In NOVA mode, create a shim that translates legacy sendPost/sendCommand to protobuf
// commands. This allows the existing apiRoutes to work until they're migrated.
// The shim parses the legacy "armTag=val1&key2=val2" format and builds the
// appropriate protobuf SettingsUpdate submessage or SystemCmd.

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

/** Encode a varint field */
function pbVarint(field: number, val: number): Buffer {
  if (val === 0) return Buffer.alloc(0);
  const parts: number[] = [];
  let tag = (field << 3) | 0;
  while (tag > 0x7F) { parts.push((tag & 0x7F) | 0x80); tag >>>= 7; }
  parts.push(tag & 0x7F);
  let v = val >>> 0;
  while (v > 0x7F) { parts.push((v & 0x7F) | 0x80); v >>>= 7; }
  parts.push(v & 0x7F);
  return Buffer.from(parts);
}

/** Encode a varint field UNCONDITIONALLY (does not skip zero).
 *  Use for fields where the receiver must distinguish "explicitly zero"
 *  from "absent" — e.g. an index field, or an enum where 0 is a valid value. */
function pbVarintForce(field: number, val: number): Buffer {
  const parts: number[] = [];
  let tag = (field << 3) | 0;
  while (tag > 0x7F) { parts.push((tag & 0x7F) | 0x80); tag >>>= 7; }
  parts.push(tag & 0x7F);
  let v = val >>> 0;
  while (v > 0x7F) { parts.push((v & 0x7F) | 0x80); v >>>= 7; }
  parts.push(v & 0x7F);
  return Buffer.from(parts);
}

/** Encode a float field */
function pbFloat(field: number, val: number): Buffer {
  if (val === 0) return Buffer.alloc(0);
  const tag = Buffer.from([(field << 3) | 5]);
  const buf = Buffer.alloc(4);
  buf.writeFloatLE(val, 0);
  return Buffer.concat([tag, buf]);
}

/** Encode a length-delimited (submessage/bytes) field */
function pbLenDelim(field: number, data: Buffer): Buffer {
  if (data.length === 0) return Buffer.alloc(0);
  const tag = (field << 3) | 2;
  const parts: number[] = [];
  let t = tag;
  while (t > 0x7F) { parts.push((t & 0x7F) | 0x80); t >>>= 7; }
  parts.push(t & 0x7F);
  let l = data.length;
  while (l > 0x7F) { parts.push((l & 0x7F) | 0x80); l >>>= 7; }
  parts.push(l & 0x7F);
  return Buffer.concat([Buffer.from(parts), data]);
}

/** Encode a string field */
function pbString(field: number, val: string): Buffer {
  if (!val) return Buffer.alloc(0);
  const data = Buffer.from(val, 'utf8');
  const tag = (field << 3) | 2;
  const parts: number[] = [];
  let t = tag;
  while (t > 0x7F) { parts.push((t & 0x7F) | 0x80); t >>>= 7; }
  parts.push(t & 0x7F);
  let l = data.length;
  while (l > 0x7F) { parts.push((l & 0x7F) | 0x80); l >>>= 7; }
  parts.push(l & 0x7F);
  return Buffer.concat([Buffer.from(parts), data]);
}

/**
 * legacyShim — translates legacy AS2-style POST bodies into Nova
 * `MSG_SETTINGS_UPDATE` envelopes.
 *
 * Each per-tag handler below is the bridge-side encoder paired with an
 * `apply_*()` decoder in `Nova_Firmware/Platform/nova_dataexc.c`. The
 * wire layout MUST match exactly between the two — silent corruption
 * is the failure mode if they drift apart (the wire ack is still
 * `{"ok":true}`).
 *
 * Invariants for any new or modified handler (see
 * `docs/firmware-bridge-protocol.md` for the full reference):
 *
 *   1. Use `pbVarintForce` (NOT `pbVarint`) for any field where 0 is a
 *      meaningful value the firmware must distinguish from "absent" —
 *      Mode enums where 0 = OFF, indexes/channels where 0 is a real
 *      slot, threshold/cycle counts where 0 means "disabled", every
 *      slot of a fixed-length repeated array, and any cross-coupled
 *      setting written via a different page's save path.
 *
 *   2. Mode-dependent encoders mirror the legacy `StoreXxx()` positional
 *      layout exactly. Switch on `mode` and emit only the fields that
 *      mode carries; never send a fixed-position superset (the firmware
 *      decoder treats missing fields as "leave alone", so an over-broad
 *      encoder corrupts unrelated state).
 *
 *   3. The body shape consumed here is what
 *      `apiRoutes.pageSaveMap.<page>.extract()` produces, not the raw
 *      UI POST. Trace UI → pageSaveMap → here → apply_*() before
 *      changing wire layout in either direction.
 *
 *   4. eqIdx vs. hwChan: `eqIdx` indexes `Settings.PWM[]` /
 *      `Settings.AuxProgram[]`; `hwChan` indexes physical peripherals.
 *      AuxProgram subtracts `EQ_AUX1` (=25) to convert eqIdx →
 *      0-based auxIdx. Always reject out-of-range rather than
 *      silently truncating.
 *
 *   5. Smoke-test after every change: 3+ POSTs across all interesting
 *      modes including the explicit-zero case, expect `{"ok":true}`,
 *      then check `/health` for `rxCrcErrors:0`.
 */
const legacyShim: any = {
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

    // ── System commands ──
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
    if (firstTag === 'FindBoard') {
      await novaBridge!.sendSystemCmd(7); return;
    }
    if (firstTag === 'ResetIoConfig') {
      await novaBridge!.sendSystemCmd(8); return;
    }
    if (firstTag === 'NextBoard') {
      await novaBridge!.sendSystemCmd(42, parseInt(firstVal, 10) || 0); return;
    }
    if (firstTag === 'PrevBoard') {
      await novaBridge!.sendSystemCmd(43, parseInt(firstVal, 10) || 0); return;
    }
    if (firstTag === 'SameBoard') {
      await novaBridge!.sendSystemCmd(44, parseInt(firstVal, 10) || 0); return;
    }
    if (firstTag === 'NextAux') {
      await novaBridge!.sendSystemCmd(40, parseInt(firstVal, 10) || 0); return;
    }
    if (firstTag === 'PrevAux') {
      await novaBridge!.sendSystemCmd(41, parseInt(firstVal, 10) || 0); return;
    }

    // ── Equipment commands ──
    if (firstTag === 'FanRemoteOff') {
      // Legacy panel — same as fanBtn (RO_FAN).
      await novaBridge!.sendEquipmentCmd(RO.FAN, parseInt(firstVal, 10) || 0);
      return;
    }
    // Equipment switch buttons from Svelte UI: "fanBtn=Auto", "climacellBtn=Off",
    // "lights1Btn=Toggle", "aux3Btn=On", etc.  Constellation has no physical
    // panel switches — these software switches replace them and route through
    // MSG_EQUIPMENT_CMD with the legacy EQ_REMOTE_OFF index.  See
    // `equipmentIds.ts` for the authoritative tag→slot mapping (mirrors
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
        `[LegacyShim] ${firstTag} = ${firstVal} → equipCmd(${eqIdx}, ${newState})`,
      );
      return;
    }
    // Per-stage refrigeration diagnostic buttons:
    //   refr1Btn..refr8Btn   → Settings.Refrig.Stage[0..7].Diagnostic
    //   defrost1Btn,defrost2Btn → Settings.Refrig.Defrost[0..1].Diagnostic
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
          `[LegacyShim] ${firstTag}=${firstVal} → refrigDiag(kind=${stageKind}, idx=${idx}, val=${value})`,
        );
        return;
      }
    }

    // Generic equipment toggle: "Button=eqIndex"
    if (firstTag === 'Button') {
      const eqIdx = parseInt(firstVal, 10);
      if (!isNaN(eqIdx)) {
        // Toggle: current state → next state (0→2, 2→1, 1→0)
        const curEquip = novaStore?.equipmentStatus?.items.find(e => e.eqIndex === eqIdx);
        const curState = curEquip?.remoteOff ?? 0;
        const newState = curState === 0 ? 2 : curState === 2 ? 1 : 0;
        await novaBridge!.sendEquipmentCmd(eqIdx, newState);
      }
      return;
    }

    // ── IO rename ──
    if (firstTag === 'ioRename') {
      const idx = parseInt(firstVal, 10);
      const name = fields['name'] ?? '';
      if (!isNaN(idx) && name) {
        await novaBridge!.sendIoNameUpdate(idx, name);
      }
      return;
    }

    // ── Settings updates — translate armTag=positional_csv to protobuf ──
    // Each settings page maps to a SettingsUpdate oneof field
    const vals = firstVal.split(',');

    // Plenum settings: "p1Plenum=AS2&PlenumTempSet=X&..."
    if (firstTag === 'p1Plenum' || firstTag === 'AS2' || (firstTag in fields && fields['PlenumTempSet'])) {
      const tempSet = parseFloat(fields['PlenumTempSet'] ?? vals[0]) || 0;
      const humidSet = parseInt(fields['PlenumHumidSet'] ?? vals[1]) || 0;
      const humidRef = parseInt(fields['selHumSetpointRef'] ?? vals[2]) || 0;
      const burnerTemp = parseFloat(fields['BurnerTempSet'] ?? vals[3]) || 0;
      const inner = Buffer.concat([
        pbFloat(1, tempSet), pbVarint(2, humidSet), pbVarint(3, humidRef), pbFloat(4, burnerTemp)
      ]);
      await novaBridge!.sendSettingsUpdate(buildSettingsUpdate(1, inner));
      return;
    }

    // Burner: "selBurnerMode=..." — mode-dependent positional encoding (mirrors
    // legacy StoreBurner).  apiRoutes pageSaveMap.burner emits one of:
    //   OFF      (mode=0):  mode, altitude, altUnit
    //   MANUAL   (mode=1):  mode, manual%, altitude, altUnit
    //   ECONOMY  (mode=2):
    //   MAX_CURE (mode=3):  mode, on, low, P, I, D, U, altitude, altUnit
    //
    // The Mode field MUST be force-encoded so saving Mode=OFF (0) is not
    // silently dropped by pbVarint's empty-zero suppression.  The non-mode
    // numeric fields (on, low, manual, altitude, altUnit) are also force-
    // encoded because 0 is a legitimate value (e.g. "Manual = 0%").
    //
    // Firmware decoder fields:
    //   1=On  2=Low  3=PID.P  4=PID.I  5=PID.D  6=PID.U  7=Mode  8=Manual
    //   9=Climacell.Altitude  10=Climacell.AltitudeUnits  (added for parity)
    if (firstTag === 'selBurnerMode') {
      const mode = parseInt(vals[0] ?? '0') || 0;
      const parts: Buffer[] = [pbVarintForce(7, mode)];
      if (mode === 1) {
        parts.push(pbVarintForce(8, parseInt(vals[1] ?? '0') || 0));
        if (vals[2] !== undefined) parts.push(pbVarintForce(9,  parseInt(vals[2]) || 0));
        if (vals[3] !== undefined) parts.push(pbVarintForce(10, parseInt(vals[3]) || 0));
      } else if (mode === 2 || mode === 3) {
        parts.push(pbVarintForce(1, parseInt(vals[1] ?? '0') || 0));
        parts.push(pbVarintForce(2, parseInt(vals[2] ?? '0') || 0));
        parts.push(pbFloat(3, parseFloat(vals[3] ?? '0') || 0));
        parts.push(pbFloat(4, parseFloat(vals[4] ?? '0') || 0));
        parts.push(pbFloat(5, parseFloat(vals[5] ?? '0') || 0));
        parts.push(pbFloat(6, parseFloat(vals[6] ?? '0') || 0));
        if (vals[7] !== undefined) parts.push(pbVarintForce(9,  parseInt(vals[7]) || 0));
        if (vals[8] !== undefined) parts.push(pbVarintForce(10, parseInt(vals[8]) || 0));
      } else {
        // OFF: just mode + altitude/altUnit (pass-through to Climacell)
        if (vals[1] !== undefined) parts.push(pbVarintForce(9,  parseInt(vals[1]) || 0));
        if (vals[2] !== undefined) parts.push(pbVarintForce(10, parseInt(vals[2]) || 0));
      }
      await novaBridge!.sendSettingsUpdate(buildSettingsUpdate(6, Buffer.concat(parts)));
      console.log(`[LegacyShim] selBurnerMode: mode=${mode} (${parts.length} fields)`);
      return;
    }

    // Fan Boost: "selBoostMode=..." — mode-dependent positional (mirrors
    // legacy StoreFanBoost).  apiRoutes pageSaveMap.fanboost emits one of:
    //   OFF     (mode=0):  mode
    //   TEMP    (mode=1):  mode, speed, temp, interval, duration
    //   RUNTIME (mode=2):  mode, speed, interval, duration
    //
    // Firmware decoder fields: 1=Mode 2=Speed 3=Interval 4=Duration 5=Temp.
    if (firstTag === 'selBoostMode') {
      const mode = parseInt(vals[0] ?? '0') || 0;
      const parts: Buffer[] = [pbVarintForce(1, mode)];
      if (mode === 1) {
        parts.push(pbFloat(2, parseFloat(vals[1] ?? '0') || 0));
        parts.push(pbFloat(5, parseFloat(vals[2] ?? '0') || 0));   // temp
        parts.push(pbVarintForce(3, parseInt(vals[3] ?? '0') || 0)); // interval
        parts.push(pbVarintForce(4, parseInt(vals[4] ?? '0') || 0)); // duration
      } else if (mode === 2) {
        parts.push(pbFloat(2, parseFloat(vals[1] ?? '0') || 0));
        parts.push(pbVarintForce(3, parseInt(vals[2] ?? '0') || 0));
        parts.push(pbVarintForce(4, parseInt(vals[3] ?? '0') || 0));
      }
      await novaBridge!.sendSettingsUpdate(buildSettingsUpdate(3, Buffer.concat(parts)));
      console.log(`[LegacyShim] selBoostMode: mode=${mode} (${parts.length} fields)`);
      return;
    }

    // Fan Speed: "maxFanSpeed=max,min,refrig,recirc,period,diff,ref1,ref2"
    if (firstTag === 'maxFanSpeed') {
      const inner = Buffer.concat([
        pbVarint(1, parseInt(vals[0]) || 0),
        pbVarint(2, parseInt(vals[1]) || 0),
        pbVarint(3, parseInt(vals[2]) || 0),
        pbVarint(7, parseInt(vals[3]) || 0),
        pbVarint(5, parseInt(vals[4]) || 0),
      ]);
      await novaBridge!.sendSettingsUpdate(buildSettingsUpdate(2, inner));
      return;
    }

    // CO2 Purge: "selPurgeMode=..." — mode-dependent positional (mirrors
    // legacy StoreCO2).  apiRoutes pageSaveMap.co2 emits one of:
    //   OFF    (mode=0):  mode
    //   MANUAL (mode=1):  mode, cycleTime, minTemp, maxTemp, duration, fanOut, doorOut
    //   AUTO   (mode=2):  mode, co2Set,    minTemp, maxTemp, duration, fanOut, doorOut
    //
    // Firmware decoder is already mode-aware on field 5 (cycleTime vs Set);
    // the bridge just needs to force-encode Mode (so OFF=0 is not dropped)
    // and emit the rest only when mode != 0.
    if (firstTag === 'selPurgeMode') {
      const mode = parseInt(vals[0] ?? '0') || 0;
      const parts: Buffer[] = [pbVarintForce(1, mode)];
      if (mode !== 0) {
        parts.push(pbFloat(2, parseFloat(vals[2] ?? '0') || 0));        // minTemp
        parts.push(pbFloat(3, parseFloat(vals[3] ?? '0') || 0));        // maxTemp
        parts.push(pbVarintForce(4, parseInt(vals[4] ?? '0') || 0));    // duration
        parts.push(pbVarintForce(5, parseInt(vals[1] ?? '0') || 0));    // cycleTime|Set
        parts.push(pbVarintForce(6, parseInt(vals[5] ?? '0') || 0));    // fanOut
        parts.push(pbVarintForce(7, parseInt(vals[6] ?? '0') || 0));    // doorOut
      }
      await novaBridge!.sendSettingsUpdate(buildSettingsUpdate(7, Buffer.concat(parts)));
      console.log(`[LegacyShim] selPurgeMode: mode=${mode} (${parts.length} fields)`);
      return;
    }

    // Climacell: "ClimacellEff=eff,alt,altUnit,P,I,D,U"
    if (firstTag === 'ClimacellEff') {
      const inner = Buffer.concat([
        pbVarint(1, parseInt(vals[0]) || 0),
        pbVarint(2, parseInt(vals[1]) || 0),
        pbVarint(3, parseInt(vals[2]) || 0),
        pbFloat(4, parseFloat(vals[3]) || 0),
        pbFloat(5, parseFloat(vals[4]) || 0),
        pbFloat(6, parseFloat(vals[5]) || 0),
        pbFloat(7, parseFloat(vals[6]) || 0),
      ]);
      await novaBridge!.sendSettingsUpdate(buildSettingsUpdate(9, inner));
      return;
    }

    // Door: "PAirValue=P,I,D,U,actTime,coolAir"
    if (firstTag === 'PAirValue') {
      const inner = Buffer.concat([
        pbFloat(1, parseFloat(vals[0]) || 0),
        pbFloat(2, parseFloat(vals[1]) || 0),
        pbFloat(3, parseFloat(vals[2]) || 0),
        pbFloat(4, parseFloat(vals[3]) || 0),
        pbVarint(5, parseInt(vals[4]) || 0),
        pbVarint(6, parseInt(vals[5]) || 0),
      ]);
      await novaBridge!.sendSettingsUpdate(buildSettingsUpdate(19, inner));
      return;
    }

    // Date: "Date=MM/DD/YYYY,HH:MM:SS,amPm"
    if (firstTag === 'Date') {
      const inner = Buffer.concat([
        pbString(1, vals[0] ?? ''),
        pbString(2, vals[1] ?? ''),
        pbVarint(3, parseInt(vals[2]) || 0),
      ]);
      await novaBridge!.sendSettingsUpdate(buildSettingsUpdate(26, inner));
      return;
    }

    // Cure: "CureStartTemp=startTemp,humidRef,startHumid,humidHighLimit"
    if (firstTag === 'CureStartTemp') {
      const inner = Buffer.concat([
        pbFloat(1, parseFloat(vals[0]) || 0),
        pbVarint(2, parseInt(vals[1]) || 0),
        pbFloat(3, parseFloat(vals[2]) || 0),
        pbFloat(4, parseFloat(vals[3]) || 0),
      ]);
      await novaBridge!.sendSettingsUpdate(buildSettingsUpdate(8, inner));
      return;
    }

    // Email: "selEmailAlert=enable,to,from,server,authType,port,acct,pw,display"
    if (firstTag === 'selEmailAlert') {
      const inner = Buffer.concat([
        pbVarint(7, parseInt(vals[0]) || 0),
        pbString(6, fields['_1'] ?? vals[1] ?? ''),
        pbString(5, fields['_2'] ?? vals[2] ?? ''),
        pbString(1, fields['_3'] ?? vals[3] ?? ''),
        pbVarint(2, parseInt(fields['emailPort'] ?? vals[5]) || 0),
        pbString(3, fields['_6'] ?? vals[6] ?? ''),
        pbString(4, fields['_7'] ?? vals[7] ?? ''),
      ]);
      await novaBridge!.sendSettingsUpdate(buildSettingsUpdate(27, inner));
      return;
    }

    // Service info: "dealerName=name,phone,techName,techPhone"
    if (firstTag === 'dealerName') {
      const inner = Buffer.concat([
        pbString(1, vals[0] ?? ''),
        pbString(2, vals[1] ?? ''),
        pbString(3, vals[2] ?? ''),
        pbString(4, vals[3] ?? ''),
      ]);
      await novaBridge!.sendSettingsUpdate(buildSettingsUpdate(31, inner));
      return;
    }

    // Ramp rate: "updTemp=rate,diff,ref,target,hours"
    if (firstTag === 'updTemp') {
      const inner = Buffer.concat([
        pbFloat(1, parseFloat(vals[0]) || 0),
        pbVarint(2, parseInt(vals[4]) || 0),
      ]);
      await novaBridge!.sendSettingsUpdate(buildSettingsUpdate(4, inner));
      return;
    }

    // Failures page 1: "FanMode=m,t,cc_m,cc_t,ref_m,ref_t,rfr,rs_m,rs_t,hum_m,hum_t,aux_m,aux_t,heat_m,heat_t,ch_m,ch_t,lt_m,lt_t,ltU,brn_m,brn_t"
    if (firstTag === 'FanMode') {
      const inner = Buffer.concat([
        pbVarint(1, parseInt(vals[0]) || 0),
        pbVarint(2, parseInt(vals[1]) || 0),
        pbVarint(3, parseInt(vals[13]) || 0),
        pbVarint(4, parseInt(vals[14]) || 0),
        pbVarint(5, parseInt(vals[4]) || 0),
        pbVarint(6, parseInt(vals[5]) || 0),
        pbVarint(7, parseInt(vals[6]) || 0),
        pbVarint(8, parseInt(vals[19]) || 0),
        pbVarint(9, parseInt(vals[20]) || 0),
        pbVarint(10, parseInt(vals[10]) || 0),
        pbVarint(11, parseInt(vals[3]) || 0),
      ]);
      await novaBridge!.sendSettingsUpdate(buildSettingsUpdate(14, inner));
      return;
    }

    // Failures page 2: "OutAirMode=m,t,outHumM,outHumT,co2M,co2T,co2Set,lowHumM,lowHumT,lowHumSet,plenSenM,plenSenT,plenSenDiff"
    if (firstTag === 'OutAirMode') {
      const inner = Buffer.concat([
        pbVarint(1, parseInt(vals[0]) || 0),
        pbVarint(2, parseInt(vals[1]) || 0),
        pbVarint(3, parseInt(vals[2]) || 0),
        pbVarint(4, parseInt(vals[3]) || 0),
        pbVarint(5, parseInt(vals[4]) || 0),
        pbVarint(6, parseInt(vals[5]) || 0),
        pbVarint(7, parseInt(vals[6]) || 0),
        pbVarint(8, parseInt(vals[7]) || 0),
        pbVarint(9, parseInt(vals[8]) || 0),
        pbVarint(10, parseInt(vals[9]) || 0),
        pbVarint(11, parseInt(vals[10]) || 0),
        pbVarint(12, parseInt(vals[11]) || 0),
        pbFloat(13, parseFloat(vals[12]) || 0),
      ]);
      await novaBridge!.sendSettingsUpdate(buildSettingsUpdate(15, inner));
      return;
    }

    // Outside air: "ctrlMode=mode,diff,aboveBelow,tempRef,calcHumidMax"
    if (firstTag === 'ctrlMode') {
      const inner = Buffer.concat([
        pbVarint(1, parseInt(vals[0]) || 0),
        pbFloat(2, parseFloat(vals[1]) || 0),
        pbVarint(3, parseInt(vals[2]) || 0),
        pbVarint(4, parseInt(vals[3]) || 0),
        pbVarint(5, parseInt(vals[4]) || 0),
      ]);
      await novaBridge!.sendSettingsUpdate(buildSettingsUpdate(12, inner));
      return;
    }

    // Misc: "p1Misc=AS2&selRefrMode=...&kbPref=...&cavStandbyOn=..."
    if (firstTag === 'p1Misc') {
      const inner = Buffer.concat([
        pbFloat(1, parseFloat(fields['tempThresh'] ?? '0') || 0),
        pbVarint(2, parseInt(fields['kbPref'] ?? '0') || 0),
        pbVarint(3, parseInt(fields['LightsUnits'] ?? '0') || 0),
        pbVarint(4, parseInt(fields['cavStandbyOn'] ?? '0') || 0),
      ]);
      await novaBridge!.sendSettingsUpdate(buildSettingsUpdate(13, inner));
      return;
    }

    // Temp alarms: "AlarmTempLow=lowTemp,lowMin,highTemp,highMin[,cureLow,cureHigh]"
    // Mirrors legacy StoreTempAlarms.  Wire layout MUST match the firmware
    // apply_temp_alarms decoder and NovaMsg_SendTempAlarmSettings encoder.
    // pageSaveMap.plensetup emits the 4-field form (no cure values); the
    // CureTempLowLimit tag below carries cureLow/cureHigh independently.
    if (firstTag === 'AlarmTempLow') {
      const parts: Buffer[] = [
        pbFloat   (1, parseFloat(vals[0]) || 0),  // lowTemp
        pbVarintForce(2, parseInt(vals[1]) || 0), // lowMin
        pbFloat   (3, parseFloat(vals[2]) || 0),  // highTemp
        pbVarintForce(4, parseInt(vals[3]) || 0), // highMin
      ];
      if (vals.length >= 6) {
        parts.push(pbFloat(5, parseFloat(vals[4]) || 0));  // cureLow
        parts.push(pbFloat(6, parseFloat(vals[5]) || 0));  // cureHigh
      }
      await novaBridge!.sendSettingsUpdate(buildSettingsUpdate(16, Buffer.concat(parts)));
      return;
    }

    // Cure limits: "CureTempLowLimit=low,high"
    if (firstTag === 'CureTempLowLimit') {
      const inner = Buffer.concat([
        pbFloat(1, parseFloat(vals[0]) || 0),
        pbFloat(2, parseFloat(vals[1]) || 0),
      ]);
      await novaBridge!.sendSettingsUpdate(buildSettingsUpdate(17, inner));
      return;
    }

    // Basic setup: wire body is "StorageName=name&TempType=0&HomePage=...&SystemMode=0&MultiView=0&dlr0=&loginSecure=0&Animations=1&SessionID=0"
    // (built by apiRoutes.basic.extract). The first bare-value field is the storage name;
    // remaining fields use named keys, so we read them from the `fields` map rather than
    // splitting `firstVal` (which only contains the storage name itself).
    if (firstTag === 'StorageName') {
      const name        = fields['StorageName'] ?? '';
      const tempType    = parseInt(fields['TempType']    ?? '0', 10) || 0;
      const systemMode  = parseInt(fields['SystemMode']  ?? '0', 10) || 0;
      const homePage    = fields['HomePage']    ?? '';
      const multiView   = parseInt(fields['MultiView']   ?? '0', 10) || 0;
      const dlr0        = fields['dlr0']        ?? '';
      const loginSecure = parseInt(fields['loginSecure'] ?? '0', 10) || 0;
      const animations  = parseInt(fields['Animations']  ?? '0', 10) || 0;
      const inner = Buffer.concat([
        pbString(1, name),
        pbVarint(2, tempType),
        pbVarint(5, systemMode),
        pbString(4, homePage),
        pbVarint(8, multiView),
        pbString(9, dlr0),
        pbVarint(10, loginSecure),
        pbVarint(11, animations),
      ]);
      await novaBridge!.sendSettingsUpdate(buildSettingsUpdate(32, inner));
      return;
    }

    // Refrigeration: "p2Refrigeration=AS2&Stage1On=20&Stage1Off=10&...&Stage8Off=60
    //                 &PRefrValue=5&IRefrValue=15&DRefrValue=2&URefrValue=3
    //                 &RefrigerationPurge=1&PurgeThreshold=100"
    //
    // Mirrors `StoreRefrig()` in Mini_IO/Application/StorePostData.c.  All
    // 8 stages, all 4 PID gains, the global Purge mode and PurgeThreshold
    // are sent in one POST (no per-stage selection — diagnostic per stage
    // is a separate command, MSG_REFRIG_DIAG_CMD).  We do NOT carry mode
    // / defrost period / defrost duration here because the legacy POST
    // body for AS2 doesn't include them; those live on the cure / aux
    // pages (and arrive via different SettingsUpdate fields when edited).
    if (firstTag === 'p2Refrigeration') {
      // Stage submessage: { on(1), off(2), diagnostic(3) } — `force` so
      // a 0 threshold (legitimate) round-trips correctly.
      const buildStage = (on: number, off: number): Buffer =>
        Buffer.concat([pbVarintForce(1, on), pbVarintForce(2, off)]);
      const stageBufs: Buffer[] = [];
      for (let i = 1; i <= 8; i++) {
        const on  = parseInt(fields[`Stage${i}On`]  ?? '0', 10) || 0;
        const off = parseInt(fields[`Stage${i}Off`] ?? '0', 10) || 0;
        stageBufs.push(pbLenDelim(2, buildStage(on, off)));
      }
      const pGain = parseFloat(fields['PRefrValue'] ?? '0') || 0;
      const iGain = parseFloat(fields['IRefrValue'] ?? '0') || 0;
      const dGain = parseFloat(fields['DRefrValue'] ?? '0') || 0;
      const uLimit = parseFloat(fields['URefrValue'] ?? '0') || 0;
      const purge = parseInt(fields['RefrigerationPurge'] ?? '0', 10) || 0;
      const purgeThresh = parseInt(fields['PurgeThreshold'] ?? '0', 10) || 0;
      const inner = Buffer.concat([
        ...stageBufs,
        pbFloat(3, pGain), pbFloat(4, iGain), pbFloat(5, dGain), pbFloat(6, uLimit),
        // Force-encode `RefrigerationPurge` (field 10) and `PurgeThreshold`
        // (field 11 — cross-coupled to `Settings.Co2.Purge.RefrigThresh` in
        // the firmware) so an explicit 0 ("purge disabled" / "no fan-speed
        // gate") round-trips correctly instead of being silently dropped.
        pbVarintForce(10, purge),
        pbVarintForce(11, purgeThresh),
      ]);
      await novaBridge!.sendSettingsUpdate(buildSettingsUpdate(5, inner));
      console.log(
        `[LegacyShim] p2Refrigeration: 8 stages saved, PID=[${pGain},${iGain},${dGain},${uLimit}], purge=${purge}, thresh=${purgeThresh}`,
      );
      return;
    }

    // Humidifier: positional wire format from apiRoutes pageSaveMap.humidifier:
    //   "<index>&_1=<mode>&_2=<coolOn>&_3=<coolOff>&_4=<recircOn>&_5=<recircOff>&_6=<refrigOn>&_7=<refrigOff>"
    // The system supports up to 3 humidifiers; the UI saves ONE at a time.
    // Encode as HumidCtrlSettings → repeated HumidCtrlEntry (field 1).
    if (firstTag === 'selHumidType') {
      // firstVal is the bare index (e.g. "2"); _1.._7 carry the rest.
      const hIndex   = parseInt(firstVal, 10) || 0;
      const hMode    = parseInt(fields['_1'] ?? '0', 10) || 0;
      const coolOn   = parseInt(fields['_2'] ?? '0', 10) || 0;
      const coolOff  = parseInt(fields['_3'] ?? '0', 10) || 0;
      const recOn    = parseInt(fields['_4'] ?? '0', 10) || 0;
      const recOff   = parseInt(fields['_5'] ?? '0', 10) || 0;
      const refOn    = parseInt(fields['_6'] ?? '0', 10) || 0;
      const refOff   = parseInt(fields['_7'] ?? '0', 10) || 0;

      // Build the HumidCtrlEntry submessage (proto fields 1..8).
      // Use pbVarintForce for index AND mode: both can legitimately be 0
      // (humidifier #1 = index 0, Manual mode = 0) and must NOT be
      // skipped — the firmware decoder needs the index field to know
      // which humidifier to update, and saving "Mode = Manual" is a
      // legitimate edit that would silently no-op if dropped.
      const entry = Buffer.concat([
        pbVarintForce(1, hIndex),
        pbVarintForce(2, hMode),
        pbVarint(3, coolOn),
        pbVarint(4, coolOff),
        pbVarint(5, recOn),
        pbVarint(6, recOff),
        pbVarint(7, refOn),
        pbVarint(8, refOff),
      ]);
      // Wrap as `repeated HumidCtrlEntry entries = 1` inside HumidCtrlSettings
      const inner = pbLenDelim(1, entry);
      // SettingsUpdate field 11 = humid_ctrl
      await novaBridge!.sendSettingsUpdate(buildSettingsUpdate(11, inner));
      console.log(`[LegacyShim] selHumidType: humidifier ${hIndex+1} mode=${hMode} cycles=[${coolOn},${coolOff},${recOn},${recOff},${refOn},${refOff}]`);
      return;
    }

    // Climacell times: "climacellTimes=v1+v2+v3+...+v48" (48 half-hour slots)
    //
    // Each slot is a single-byte efficiency value 0..N where 0 is a legitimate
    // value meaning "climacell off for this half-hour".  Use pbVarintForce so
    // zero-valued slots are actually emitted on the wire — with plain pbVarint
    // they would be silently dropped, leaving the firmware decoder unable to
    // distinguish "slot zeroed" from "slot unchanged", which manifests as the
    // saved schedule being only the non-zero slots overlaid on whatever was
    // there before.
    if (firstTag === 'climacellTimes') {
      const NUM_SLOTS = 48;
      const timeVals = firstVal.split('+').map(v => parseInt(v, 10) || 0);
      // Pad/truncate to exactly NUM_SLOTS so the firmware always replaces the
      // full schedule, not a prefix of it.
      while (timeVals.length < NUM_SLOTS) timeVals.push(0);
      timeVals.length = NUM_SLOTS;
      const bufs: Buffer[] = [];
      for (const tv of timeVals) bufs.push(pbVarintForce(1, tv));
      await novaBridge!.sendSettingsUpdate(buildSettingsUpdate(10, Buffer.concat(bufs)));
      console.log(`[LegacyShim] climacellTimes: ${NUM_SLOTS} slots saved`);
      return;
    }

    // Runtimes: "runTimes=v1+v2+...+v48"
    if (firstTag === 'runTimes') {
      const rtVals = firstVal.split('+').map(v => parseInt(v) || 0);
      const bufs: Buffer[] = [];
      for (const rv of rtVals) bufs.push(pbVarint(1, rv));
      await novaBridge!.sendSettingsUpdate(buildSettingsUpdate(33, Buffer.concat(bufs)));
      return;
    }

    // Master/slave: "selMasterSlaveMode=mode,ip"
    if (firstTag === 'selMasterSlaveMode') {
      const inner = Buffer.concat([
        pbVarint(1, parseInt(vals[0]) || 0),
        pbString(2, vals[1] ?? ''),
      ]);
      await novaBridge!.sendSettingsUpdate(buildSettingsUpdate(40, inner));
      return;
    }

    // Alerts: "AlertSetup=010101..." (string of 0/1 chars)
    if (firstTag === 'AlertSetup') {
      const bufs: Buffer[] = [];
      for (let i = 0; i < firstVal.length; i++) {
        if (firstVal[i] === '1') bufs.push(pbVarint(1, i));
      }
      await novaBridge!.sendSettingsUpdate(buildSettingsUpdate(35, Buffer.concat(bufs)));
      return;
    }

    // IO config: "p2IoConfig=AS2&..." — simplified pass-through
    if (firstTag === 'p2IoConfig') {
      // IO config is complex; for now just request a refresh after the UI saves
      await novaBridge!.sendSystemCmd(8); // ResetIoConfig triggers resend
      return;
    }

    // Log interval: "recInterval=interval,wrap"
    if (firstTag === 'recInterval') {
      const inner = Buffer.concat([
        pbVarint(1, parseInt(vals[0]) || 0),
        pbVarint(2, parseInt(vals[1]) || 0),
      ]);
      await novaBridge!.sendSettingsUpdate(buildSettingsUpdate(37, inner));
      return;
    }

    // Accounts: "AcctId0=id0,pw0,id1,pw1,..." — pairs of id,pw
    if (firstTag.startsWith('AcctId')) {
      // Encode all account fields from form
      const bufs: Buffer[] = [];
      for (let i = 0; i < 10; i++) {
        const id = fields[`AcctId${i}`] ?? '';
        const pw = fields[`AcctPw${i}`] ?? '';
        if (!id && !pw) continue;
        const userBuf = Buffer.concat([pbVarint(1, i), pbString(2, id), pbString(3, pw)]);
        const tag = (1 << 3) | 2;
        bufs.push(Buffer.from([tag, userBuf.length]), userBuf);
      }
      if (fields['LoginPw']) bufs.push(pbString(2, fields['LoginPw']));
      await novaBridge!.sendSettingsUpdate(buildSettingsUpdate(34, Buffer.concat(bufs)));
      return;
    }

    // PID settings: "pidWrap=wrap"
    if (firstTag === 'pidWrap') {
      const inner = Buffer.concat([pbVarint(6, parseInt(vals[0]) || 0)]);
      await novaBridge!.sendSettingsUpdate(buildSettingsUpdate(38, inner));
      return;
    }

    // PWM: "p2PwmOutputs=AS2&0=<eqIdx>&1=<eqIdx>&...&5=<eqIdx>" → SettingsUpdate field 39
    //
    // Wire format from UI (matches legacy StorePWMChannels):
    //   key   = hardware channel index 0..5
    //   value = PWM_EQUIPMENT enum (0..PWM_TOTAL_EQ-1) or 255 (PWM_UNDEFINED)
    //
    // Firmware semantics (mirrors legacy):
    //   Settings.PWM[] is indexed by equipment.
    //   For each defined channel, write Settings.PWM[eqIdx] = { Enabled=1, Channel=hwChan }.
    //   Unassigned equipments (eqIdx not present in this POST) get cleared by
    //   the firmware decoder before applying.
    //
    // The previous bridge implementation had channel and eqIdx inverted
    // (idx=hwChannel, channel=eqIdx), which caused all per-channel saves to
    // land in the wrong Settings.PWM slot — silently no-op for valid eqIdx
    // and out-of-range writes for hwChannel>=PWM_TOTAL_EQ.
    if (firstTag === 'p2PwmOutputs') {
      const PWM_UNDEFINED = 255;
      const entries: Buffer[] = [];
      for (const [key, val] of Object.entries(fields)) {
        if (!/^\d+$/.test(key)) continue;       // skip 'p2PwmOutputs', 'AS2', PID names
        const hwChan = parseInt(key, 10);
        const eqIdx  = parseInt(val, 10);
        if (!Number.isFinite(eqIdx) || eqIdx === PWM_UNDEFINED) continue;
        if (eqIdx < 0 || eqIdx > 32) continue;  // sanity bound (PWM_TOTAL_EQ in GRC = 9)

        // Force-encode all three fields: idx=0 (PWM_CONDENSER_1) and channel=0
        // are both legitimate values that must NOT be dropped.
        const entry = Buffer.concat([
          pbVarintForce(1, eqIdx),     // index = equipment
          pbVarintForce(2, 1),          // enabled (always 1 for present entries)
          pbVarintForce(3, hwChan),    // channel = hardware PWM channel
        ]);
        entries.push(pbLenDelim(1, entry));
      }
      await novaBridge!.sendSettingsUpdate(buildSettingsUpdate(39, Buffer.concat(entries)));
      console.log(`[LegacyShim] p2PwmOutputs: ${entries.length} channel assignments saved`);
      return;
    }

    // Analog board: "BAdd=boardIdx&BdLbl=...&Sen1Lbl=...&Sen1Off=...&Sen1Dis=..."
    // → SettingsUpdate field 42: boardIdx(1), repeated sensor submsg(6)
    if (firstTag === 'BAdd') {
      const boardIdx = parseInt(firstVal, 10) || 0;
      const sensors: Buffer[] = [];
      for (let s = 1; s <= 4; s++) {
        const label = fields[`Sen${s}Lbl`] ?? '';
        const offset = parseFloat(fields[`Sen${s}Off`] ?? '0') || 0;
        const disabled = parseInt(fields[`Sen${s}Dis`] ?? '0') || 0;
        const sensor = Buffer.concat([
          pbVarint(1, s - 1),        // slot (0-based)
          pbString(3, label),         // label
          pbFloat(4, offset),         // offset
          pbVarint(5, disabled),      // disabled
        ]);
        sensors.push(pbLenDelim(6, sensor));
      }
      const inner = Buffer.concat([
        pbVarint(1, boardIdx),
        ...sensors,
      ]);
      await novaBridge!.sendSettingsUpdate(buildSettingsUpdate(42, inner));
      return;
    }

    // Aux program: "AuxProgram=<eqIndex>&type1=..&io1=..&st1=..&op1=..&ref1=..&sen1=..&diff1=..&andOr1=..&...&dutyCycle=..&period=..&units=.."
    // → SettingsUpdate field 41: repeated AuxProgramEntry submsg(1)
    //
    // The legacy POST tag carries the **equipment index** (EQ_AUX1=25 .. EQ_AUX8=32),
    // matching legacy StorePostData which does `programIndex -= EQ_AUX1`.  The proto
    // AuxProgramEntry.aux_index field is 0-based, so we must subtract EQ_AUX1 here.
    // Without this conversion, eq=25 would land in Settings.AuxProgram[25] (out of
    // bounds → silently dropped by the firmware decoder), so no save would ever
    // take effect.
    if (firstTag === 'AuxProgram') {
      const EQ_AUX1 = 25;
      const NUM_AUX_OUTPUTS = 8;
      const eqIdx = parseInt(firstVal, 10) || 0;
      const auxIdx = eqIdx - EQ_AUX1;
      if (auxIdx < 0 || auxIdx >= NUM_AUX_OUTPUTS) {
        console.warn(`[LegacyShim] AuxProgram: invalid eqIdx=${eqIdx} (expected ${EQ_AUX1}..${EQ_AUX1 + NUM_AUX_OUTPUTS - 1})`);
        return;
      }
      const dutyCycle = parseInt(fields['dutyCycle'] ?? '0') || 0;
      const period = parseInt(fields['period'] ?? '0') || 0;
      const units = parseInt(fields['units'] ?? '0') || 0;

      // Collect rules: type1/io1/st1/op1/ref1/sen1/andOr1, type2/io2/...
      // Use pbVarintForce on every rule field so a legitimate 0 (e.g. RT_MANUAL=0,
      // io=0, state=0, op=0, ref=0) is not silently dropped.
      const ruleBufs: Buffer[] = [];
      for (let r = 1; r <= 10; r++) {
        if (fields[`type${r}`] === undefined) break;
        const rule = Buffer.concat([
          pbVarintForce(1, parseInt(fields[`type${r}`] ?? '0') || 0),
          pbVarintForce(2, parseInt(fields[`io${r}`] ?? '0') || 0),
          pbVarintForce(3, parseInt(fields[`st${r}`] ?? '0') || 0),
          pbVarintForce(4, parseInt(fields[`op${r}`] ?? '0') || 0),
          pbFloat(5, parseFloat(fields[`sen${r}`] ?? '0') || 0),
          pbVarintForce(6, parseInt(fields[`andOr${r}`] ?? '256') || 0),
          pbVarintForce(7, parseInt(fields[`ref${r}`] ?? '0') || 0),
        ]);
        ruleBufs.push(pbLenDelim(6, rule));
      }

      // Force-encode aux_index (auxIdx=0 = first aux is legitimate) and the
      // duty/period/units numerics (all of which can validly be 0).
      const entry = Buffer.concat([
        pbVarintForce(1, auxIdx),
        pbVarintForce(3, dutyCycle),
        pbVarintForce(4, period),
        pbVarintForce(5, units),
        ...ruleBufs,
      ]);
      const inner = pbLenDelim(1, entry);
      await novaBridge!.sendSettingsUpdate(buildSettingsUpdate(41, inner));
      console.log(`[LegacyShim] AuxProgram: aux ${auxIdx + 1} (eq ${eqIdx}) duty=${dutyCycle} period=${period} units=${units} rules=${ruleBufs.length}`);
      return;
    }

    // Graph favorites
    if (firstTag === 'GraphFavorites') {
      const inner = pbString(1, firstVal);
      await novaBridge!.sendSettingsUpdate(buildSettingsUpdate(43, inner));
      return;
    }

    console.warn(`[NOVA] Unhandled legacy POST: ${firstTag}=${firstVal.slice(0, 40)}`);
  },
  sendCommand: (tag: string, value: string) => {
    // sendCommand is used for action-style commands like NextBoard
    if (tag === 'NextBoard') {
      novaBridge!.sendSystemCmd(42, parseInt(value, 10) || 0);
    } else if (tag === 'PrevBoard') {
      novaBridge!.sendSystemCmd(43, parseInt(value, 10) || 0);
    } else if (tag === 'NextAux') {
      novaBridge!.sendSystemCmd(40, parseInt(value, 10) || 0);
    } else {
      console.warn('[NOVA] Legacy sendCommand shim:', tag, value);
    }
  },
  on: (..._args: any[]) => {},
  open: async () => {},
  close: () => {},
};

const apiSerialBridge = legacyShim;
app.use('/iot', createApiRoutes(dataCache, apiSerialBridge, upgradeManager, novaLogStore ?? undefined, vfdClient ?? undefined, novaFwManager ?? undefined, orbitClient ?? undefined));

// ─── Cloud → Bridge command push ───
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

// Optionally serve SvelteKit build output
if (SVELTE_BUILD) {
  const path = await import('path');
  const buildPath = path.resolve(SVELTE_BUILD);
  app.use(express.static(buildPath));
  // SPA fallback
  app.get('*', (_req, res) => {
    res.sendFile(path.join(buildPath, 'index.html'));
  });
  console.log(`[Server] Serving SvelteKit build from ${buildPath}`);
}

// Attach WebSocket server
const wsManager = new WsManager(httpServer, dataCache, vfdClient ?? undefined);

// Wire UpgradeManager to WebSocket manager for real-time upgrade status broadcasts
wsManager.attachUpgradeManager(upgradeManager);

// ─── Wire Nova bridge → cache + WebSocket ───

{
  // ── NOVA PROTOCOL: wire protobuf bridge → data store → legacy cache ──
  createNovaAdapter(novaBridge, novaStore, dataCache, wsManager);
  novaBridge.on('error', (err: Error) => {
    console.error('[NovaBridge] Error:', err.message);
  });

  // ── VFD Fan Speed Forwarding (NOVA mode) ──
  // When fanControlMode is 'vfd', intercept fan speed from SystemStatus
  // and forward to VFD drives via Modbus TCP.
  if (vfdClient) {
    let lastNovaVfdFanSpeed = -1;
    let novaVfdForwardBusy = false;
    novaStore.on('systemStatus', (status: { fanSpeed: string }) => {
      const fanMode = loadConfig<string>('fan-control-mode') ?? 'legacy';
      if (fanMode !== 'vfd') return;

      const rawSpeed = status.fanSpeed;
      const speedPercent = rawSpeed === 'Off' || rawSpeed === 'Manual' ? 0 : parseInt(rawSpeed, 10) || 0;
      if (speedPercent === lastNovaVfdFanSpeed) return;
      lastNovaVfdFanSpeed = speedPercent;

      if (novaVfdForwardBusy) return;
      novaVfdForwardBusy = true;
      (async () => {
        const drives = vfdClient!.getDrives();
        let count = 0;
        for (const drv of drives) {
          if (!drv.online || drv.faulted) continue;
          try {
            if (speedPercent > 0) {
              await vfdClient!.sendAction(drv.unitId, 'start', speedPercent * 100);
            } else {
              await vfdClient!.sendAction(drv.unitId, 'stop');
            }
            count++;
          } catch { /* retry next event */ }
        }
        if (count > 0) console.log(`[VFD Fan] NOVA: Forwarded fan speed ${speedPercent}% to ${count} drives`);
      })().finally(() => { novaVfdForwardBusy = false; });
    });
  }

  console.log('[Server] Nova bridge wired (COBS+Protobuf)');
}


// ─── Start ───

async function start() {
  // Start HTTP server first — the UI/API must be available even if
  // the firmware serial link isn't up yet (QEMU still booting, etc.).
  httpServer.listen(PORT, () => {
    console.log('');
    console.log('  ╔══════════════════════════════════════════════╗');
    console.log('  ║    Constellation Bridge Server               ║');
    console.log('  ╠══════════════════════════════════════════════╣');
    console.log(`  ║  HTTP/WS : http://localhost:${PORT}            ║`);
    console.log(`  ║  Serial  : ${SERIAL_PORT.padEnd(33)}║`);
    console.log(`  ║  Baud    : ${String(921600).padEnd(33)}║`);
    console.log(`  ║  Protocol: ${'NOVA (COBS+Protobuf)'.padEnd(33)}║`);
    console.log(`  ║  VFD     : ${VFD_ENABLED ? (VFD_HOST + ':' + VFD_PORT).padEnd(33) : 'disabled'.padEnd(33)}║`);
    console.log(`  ║  WS path : /iot/ws                           ║`);
    console.log(`  ║  REST    : /iot/*                             ║`);
    console.log(`  ║  Django  : ${process.env.DJANGO_SYNC_ENABLED === 'true' ? (process.env.DJANGO_URL || 'http://localhost:8000').padEnd(33) : 'disabled'.padEnd(33)}║`);
    console.log('  ╚══════════════════════════════════════════════╝');
    console.log('');

    // Start Django cloud sync (if enabled)
    djangoSync.start();

    // Start audit forwarder — ships save/login events to Django for dashboards.
    startAuditForwarder();
  });

  // Open serial connection — retry in background if firmware not ready yet.
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

  // Start Orbit Modbus TCP client (polls orbit boards like real Nova firmware)
  if (orbitClient) {
    // When orbit polls complete, overlay sensor data into the cache.
    // The orbit's RS-485 sensor bus is the truth for temperatures/humidity/CO2.
    // This overrides the ARM simulator's physics-engine values.
    orbitClient.onUpdate(() => {
      const boards = orbitClient.getBoards();
      const roleBySlot = new Map<number, number>();
      for (const ob of dataCache.getOrbitBoards()) roleBySlot.set(ob.slot, ob.role);
      for (const board of boards) {
        if (!board.online || !board.sensorBoards.length) continue;
        // Only overlay STORAGE-role orbits onto the legacy storage UI fields.
        const role = roleBySlot.get(board.id - 2);
        if (role !== undefined && role !== 1 /* STORAGE */) continue;
        overlaySensorData(board.sensorBoards, dataCache);
      }
    });

    orbitClient.start().then(() => {
      console.log('[Server] Orbit Modbus TCP client started');
    }).catch(err => {
      log.error('Orbit client start error', { error: err.message });
    });
  }

  // ── Discover orbit boards from simulator API ──
  // In dev mode, the real Nova firmware doesn't send protobuf discovery.
  // Scan the orbit simulator API ports to populate dataCache.orbitBoards
  // so the UI can detect GDC / Triton / Pulsar boards.
  //
  // Runs periodically so that role changes made *after* the bridge starts
  // (e.g. flipping a slot to Triton via the orbit-sim panel) are picked up
  // without restarting the bridge.
  const NOVA_MAX_ORBIT_SLOTS = 16; // matches NOVA_MAX_ORBITS in apiRoutes
  async function discoverOrbitBoards(): Promise<number> {
    const orbitApiBase = parseInt(process.env.ORBIT_API_PORT ?? '9010', 10);
    const boards: Array<{ slot: number; role: number; connected: boolean }> = [];
    // Scan in parallel — 16 short-timeout fetches finish in well under a second.
    const probes = await Promise.all(
      Array.from({ length: NOVA_MAX_ORBIT_SLOTS }, (_, slot) => (async () => {
        const port = orbitApiBase + slot;
        try {
          const resp = await fetch(`http://127.0.0.1:${port}/api/status`, {
            signal: AbortSignal.timeout(500),
          });
          if (!resp.ok) return null;
          const data = await resp.json() as any;
          const roleNum = typeof data.role === 'number' ? data.role
            : data.roleName === 'STORAGE' ? 1
            : data.roleName === 'GDC' ? 2
            : data.roleName === 'TRITON' ? 3
            : data.roleName === 'PULSAR' ? 4 : 0;
          return { slot, role: roleNum, connected: true };
        } catch { return null; }
      })()),
    );
    for (const p of probes) if (p) boards.push(p);

    // Always publish the latest snapshot, even if shorter than before, so
    // role downgrades / unplugged orbits propagate.
    dataCache.setOrbitBoards(boards);

    // Make sure orbitClient knows about every discovered slot so we can
    // actually talk Modbus to the Tritons (initial bootstrap only added
    // slots 2 + 3).  addBoard is a no-op for already-known ids.
    //
    // The orbit simulator maps orbit-id N → Modbus port 5500+N and
    // API port 9008+N.  Our discovery iterates `slot = 0..15` based on
    // API-port offset from 9010, so the corresponding orbit-id is
    // `slot + 2`.  We register the Modbus client under that orbit-id so
    // the rest of the code can keep using `slot` as the public handle.
    if (orbitClient) {
      for (const b of boards) {
        const orbitId = b.slot + 2;
        orbitClient.addBoard({ id: orbitId, host: ORBIT_HOST, port: ORBIT_BASE_PORT + orbitId });
      }
    }
    return boards.length;
  }

  /**
   * Master/slave broadcast: push the storage panel's outside-air-temp
   * (and Nova-supplied demand, when wired) into every Triton orbit via
   * Modbus regs 320/321.  This is the modern equivalent of the legacy
   * "panel master/slave" broadcast that used to share time + outside
   * air across panels.
   *
   * Outside air is taken from MainData[2] (already resolved by the cache
   * with sensor fallback).  Demand is a TODO — once we know which Nova
   * field carries the rack demand we'll pipe it in here; until then each
   * Triton's local `demand` placeholder is left untouched (sentinel −1).
   */
  function broadcastTritonInputs(): void {
    if (!orbitClient) return;
    const tritonSlots = dataCache.getOrbitBoards()
      .filter(b => b.role === 3 && b.connected)
      .map(b => b.slot);
    if (tritonSlots.length === 0) return;

    const mainFields = (dataCache.getByVarName('MainData')?.value ?? '').split(',');
    const rawOutside = mainFields[2];
    const outsideF = rawOutside && rawOutside !== 'dis' && rawOutside !== '--'
      ? parseFloat(rawOutside)
      : NaN;
    if (!isFinite(outsideF)) return;

    const tempReg = Math.round(outsideF * 10) & 0xFFFF;
    for (const slot of tritonSlots) {
      // slot → orbit-id translation (see discoverOrbitBoards).
      orbitClient.writeHoldingRegister(slot + 2, 320, tempReg).catch(() => {});
      // Demand (reg 321) — wire here once Nova exposes it; -1 = no broadcast.
    }
  }

  // Initial discovery after a short delay (allow simulators to finish starting)
  setTimeout(async () => {
    console.log('[Server] Running orbit board discovery scan...');
    const found = await discoverOrbitBoards();
    console.log(`[Server] Discovery scan found ${found} board(s)`);
  }, 3000);

  // Periodic re-scan so live role changes show up without a bridge restart.
  setInterval(() => { discoverOrbitBoards().catch(() => {}); }, 5000);
  // Periodic master/slave broadcast (ambient + demand → all Tritons).
  setInterval(broadcastTritonInputs, 5000);

  let vfdFaultActive = false;
  let vfdFailureTimer: ReturnType<typeof setTimeout> | null = null;
  let vfdLoggedFaults = new Set<string>();  // track which faults have been logged
  if (vfdClient) vfdClient.onUpdate(() => {
    wsManager.broadcast('vfd-data');

    // ── VFD Fault → System Alarm injection ──
    // When fanControlMode is 'vfd', check all drives for faults and inject
    // alarm entries so they appear in the standard Agristar alarm system.
    // Failure mode is delayed by the programmed fan fail timer (FailureData1[1])
    // to match firmware behaviour — alarms show immediately but system stays
    // running until the delay elapses with the fault still present.
    const fanMode = loadConfig<string>('fan-control-mode') ?? 'legacy';
    if (fanMode === 'vfd') {
      const drives = vfdClient.getDrives();
      const faultAlarms: string[] = [];
      for (const drv of drives) {
        if (drv.faulted && drv.faultCode) {
          const profile = getProfile(drv.manufacturer);
          const faultName = profile.faultNames[drv.faultCode]
            ?? `Code 0x${drv.faultCode.toString(16).padStart(4, '0')}`;
          faultAlarms.push(
            `WARN_VFD=VFD Drive Fault - ${drv.label || 'Unit ' + drv.unitId}: ${faultName}`
          );
        }
      }
      // Store VFD alarms immediately so buildFrontmatterData can merge them
      dataCache.updateFromArm('VfdAlarmData', faultAlarms.join('|'));

      if (faultAlarms.length > 0) {
        // Stop faulted drives immediately (they can't run anyway)
        for (const drv of drives) {
          if (drv.online) {
            vfdClient.sendAction(drv.unitId, 'stop').catch(() => {});
          }
        }

        if (!vfdFaultActive) {
          vfdFaultActive = true;
          // Log each fault alarm to the alarm history
          for (const alarm of faultAlarms) {
            const eqIdx = alarm.indexOf('=');
            const text = eqIdx > 0 ? alarm.slice(eqIdx + 1) : alarm;
            if (!vfdLoggedFaults.has(text)) {
              vfdLoggedFaults.add(text);
              novaLogStore?.logVfdAlarm(alarm, 'ON');
            }
          }
          // Read programmed fan fail delay from FailureData1 (FanMode) index 1
          const failData = dataCache.getByVarName('FailureData1')?.value ?? '';
          const failFields = failData.split(',');
          const fanFailMinutes = parseInt(failFields[1], 10) || 1;
          const delayMs = fanFailMinutes * 60_000;
          console.log(`[VFD Fault] ${faultAlarms.length} fault(s) detected — alarm active, FAILURE in ${fanFailMinutes} min`);
          // Start the failure delay timer
          vfdFailureTimer = setTimeout(() => {
            vfdFailureTimer = null;
            // Re-check that faults are still present before escalating
            if (vfdFaultActive) {
              dataCache.updateFromArm('VfdFailureActive', '1');
              console.log(`[VFD Fault] Fan fail delay elapsed — SYSTEM FAILURE active`);
            }
          }, delayMs);
        }
      } else if (vfdFaultActive) {
        // All faults cleared — cancel pending timer and clear failure
        vfdFaultActive = false;
        // Log cleared events for each previously logged fault
        for (const text of vfdLoggedFaults) {
          novaLogStore?.logVfdAlarm(`WARN_VFD=${text}`, 'OFF');
        }
        vfdLoggedFaults.clear();
        if (vfdFailureTimer) {
          clearTimeout(vfdFailureTimer);
          vfdFailureTimer = null;
          console.log('[VFD Fault] Faults cleared before delay elapsed — failure averted');
        } else {
          console.log('[VFD Fault] All faults cleared, resuming normal operation');
        }
        dataCache.updateFromArm('VfdFailureActive', '');
      }
    } else {
      // Clear VFD alarms and failure flag when not in VFD mode
      dataCache.updateFromArm('VfdAlarmData', '');
      dataCache.updateFromArm('VfdFailureActive', '');
    }
  });

  // ── VFD Fan Speed Forwarding ──
  // VFD fan-speed forwarding for NOVA flows through NovaDataStore events;
  // wired earlier in this file when novaStore is created.  Nothing to do here.
}

// ─── Graceful shutdown ───

process.on('SIGINT', () => shutdown('SIGINT'));
process.on('SIGTERM', () => shutdown('SIGTERM'));

function shutdown(signal: string) {
  console.log(`\n[Server] Received ${signal}, shutting down...`);
  vfdClient?.stop();
  wsManager.close();
  novaLogStore.close();
  novaBridge.close();
  httpServer.close(() => {
    console.log('[Server] Bye.');
    process.exit(0);
  });
  // Force exit after 5s
  setTimeout(() => process.exit(1), 5000);
}

start();
