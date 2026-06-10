/**
 * index.ts — Sensor Injector
 *
 * One process per orbit LP. Connects to the orbit LP at
 * `<HOST>:<PORT>` (default 10.1.2.200:5502 = STORAGE on the lab LAN),
 * exposes a tiny REST + static-panel server on `:9100`, and pushes any
 * UI changes through to the LP via FC16 to HR 200..263.
 *
 * Design intent: replace the orbit-simulator's sensor-board RTU
 * stand-in. The orbit LP firmware now serves Modbus TCP itself; this
 * tool just provides the engineer-facing way to push synthetic sensor
 * values into the LP's writable HR 200..263 block until real RS-485
 * sensor boards arrive.
 *
 * Env vars:
 *   ORBIT_HOST   target orbit LP IP        (default 10.1.2.200)
 *   ORBIT_PORT   Modbus TCP port           (default 5502)
 *   ORBIT_UNIT   Modbus unit id            (default 1)
 *   PANEL_PORT   web panel listen port     (default 9100)
 *
 * REST API:
 *   GET  /api/state
 *     → { connected, target, channels: [{ board, ch, type, value, hr }, ...] }
 *   POST /api/channel
 *     body { board, ch, type, value }
 *     → updates one channel and pushes via FC06
 *   POST /api/push
 *     body { values: [{ board, ch, type, value }, ...] }
 *     → batched FC16 push for the whole sensor block
 */

import * as http from 'http';
import * as fs from 'fs';
import * as path from 'path';
import { fileURLToPath } from 'url';

import WebSocket from 'ws';
import { ModbusTcpClient } from './mbtcp.js';
import {
  engToHrInt16, hrInt16ToEng, SENSOR_VAL_UNDEF,
  SENSOR_TYPE_TEMP, SENSOR_TYPE_HUMID, SENSOR_TYPE_CO2, SENSOR_TYPE_NONE,
  type SensorType, typeName,
} from './sensorTypes.js';

const HOST       = process.env.ORBIT_HOST ?? '10.1.2.200';
const PORT       = parseInt(process.env.ORBIT_PORT ?? '5502', 10);
const UNIT       = parseInt(process.env.ORBIT_UNIT ?? '1', 10);
const PANEL_PORT = parseInt(process.env.PANEL_PORT ?? '9100', 10);
const BRIDGE     = process.env.BRIDGE_HOST ?? '10.47.27.108:9001';

// Temperature display unit — FOLLOWS Level-2 Basic Settings
// (BasicSetup.temp_type: 0 = Fahrenheit, 1 = Celsius), read live from the
// bridge proto stream so the injector panel speaks the same unit as the
// System Monitor. The wire/engineering is ALWAYS °C (engToHrInt16); we only
// convert at the panel boundary. Default Fahrenheit until the bridge reports.
let tempType = 0;
const tempUnitSym = () => (tempType === 1 ? '°C' : '°F');

const HR_SENSOR_BASE = 200;
const N_BOARDS       = 16;
const N_CH_PER_BOARD = 4;
const N_CHANNELS     = N_BOARDS * N_CH_PER_BOARD;

/* Equipment-IO panel constants. Layout mirrors orbit_storage.h:
 *   coils 0..9       = DO 0..9   (R via FC01, W by CONTROLLER via FC05)
 *   discrete 0..9    = DI 0..9   (R via FC02)
 *   discrete 10      = E-stop    (R via FC02)
 *   discrete 11..14  = DC24V 0..3 (R via FC02)
 *   HR 41000..41014  = Bench DI inject (R/W via FC03/FC06) — same layout
 *                       as discrete inputs above */
const N_DO            = 10;
const N_DI_TOTAL      = 15; // 10 DI + E-stop + 4 DC24V
const HR_DI_INJECT    = 41000;
const DO_LABELS = [
  'DO1','DO2','DO3','DO4','DO5','DO6','DO7','DO8','DO9','DO10',
];
const DI_LABELS = [
  'DI1','DI2','DI3','DI4','DI5','DI6','DI7','DI8','DI9','DI10',
  'E-stop',
  'DC24V-1','DC24V-2','DC24V-3','DC24V-4',
];

interface Channel {
  board: number;       // 0..15
  ch: number;          // 0..3
  type: SensorType;    // sensor type
  value: number;       // engineering units (e.g. 21.5 for °C)
}

/* Real Gellert sensor layout (16 boards × 4 channels = HR 200..263).
 *   Board 0 — air temps     : Plenum 1, Plenum 2, Outside Air, Return Air
 *   Board 1 — humid + CO2   : Outside Air RH, Plenum RH, Return RH, CO2
 *   Board 2 — pile #1 temps : Pile1 T1..T4 (top → bottom)
 *   Boards 3..15            : pile temps OR humidities, four sensors per pile
 *                             (default = TEMP; flip the row's type to HUMID
 *                             on the panel for a humidity pile). */
const CH_LABELS: Record<number, string[]> = {
  0: ['Plenum 1 Temp',     'Plenum 2 Temp',  'Outside Air Temp', 'Return Air Temp'],
  1: ['Outside Air RH',    'Plenum RH',      'Return RH',        'CO2'],
};
for (let b = 2; b < N_BOARDS; b++) {
  const n = b - 1; // pile #1..#14
  CH_LABELS[b] = [`Pile ${n} T1`, `Pile ${n} T2`, `Pile ${n} T3`, `Pile ${n} T4`];
}

/* In-RAM mirror of what we last pushed to the LP. Survives across
 * UI reloads but not across restarts (intentional — restart = clean
 * state, the LP keeps its last-pushed values until next push). */
const channels: Channel[] = [];
for (let b = 0; b < N_BOARDS; b++) {
  for (let c = 0; c < N_CH_PER_BOARD; c++) {
    let type: SensorType = SENSOR_TYPE_NONE;
    let value = 0;
    if (b === 0) {
      // All four channels are temperatures.
      type = SENSOR_TYPE_TEMP;
      value = [21.5, 22.0, 18.0, 20.5][c]!;
    } else if (b === 1) {
      // ch0..2 = RH, ch3 = CO2.
      if (c < 3) { type = SENSOR_TYPE_HUMID; value = [55.0, 60.0, 58.0][c]!; }
      else       { type = SENSOR_TYPE_CO2;   value = 800; }
    } else {
      // Pile boards: default to TEMP. User can flip the whole row to HUMID
      // on the panel for a humidity pile.
      type = SENSOR_TYPE_TEMP;
      value = 20.0;
    }
    channels.push({ board: b, ch: c, type, value });
  }
}

const client = new ModbusTcpClient({ host: HOST, port: PORT, unitId: UNIT });
client.connect();

/* Read a single varint protobuf field from a message buffer. */
function readVarintField(buf: Uint8Array, field: number): number | null {
  let o = 0;
  const rv = (): number => { let s = 0, r = 0, b: number; do { b = buf[o++]; r |= (b & 0x7f) << s; s += 7; } while (b & 0x80); return r >>> 0; };
  while (o < buf.length) {
    const key = rv(); const fn = key >>> 3, wt = key & 7;
    if (wt === 0) { const v = rv(); if (fn === field) return v; }
    else if (wt === 2) { const len = rv(); o += len; }
    else if (wt === 5) o += 4;
    else if (wt === 1) o += 8;
    else break;
  }
  return null;
}

/* Follow Level-2 Basic Settings: subscribe to the bridge's BasicSetup
 * (envelope tag 20) and track temp_type (field 2). Auto-reconnects. */
function watchBasicSetup(): void {
  const connect = () => {
    const ws = new WebSocket(`ws://${BRIDGE}/proto/stream`);
    ws.binaryType = 'arraybuffer';
    ws.on('open', () => ws.send(JSON.stringify({ action: 'subscribe', tags: [20] })));
    ws.on('message', (data: WebSocket.RawData, isBinary: boolean) => {
      if (!isBinary) return;
      const b = new Uint8Array(data as ArrayBuffer);
      const dv = new DataView(b.buffer, b.byteOffset, b.byteLength);
      let off = 0;
      while (off + 6 <= b.length) {
        const tag = dv.getUint16(off, true); const len = dv.getUint32(off + 2, true); off += 6;
        if (tag === 20) {
          const tt = readVarintField(b.subarray(off, off + len), 2);
          if (tt !== null && tt !== tempType) {
            tempType = tt;
            console.log(`[tempUnit] follows Basic Settings: temp_type=${tt} (${tempUnitSym()})`);
          }
        }
        off += len;
      }
    });
    ws.on('close', () => setTimeout(connect, 3000));
    ws.on('error', () => { try { ws.close(); } catch { /* */ } });
  };
  connect();
}
watchBasicSetup();

/* ─── Bay-light (current-sense) DI detection ──────────────────────────
 * The bay lights (EQ.LIGHTS1=23, LIGHTS2=24) are CURRENT-SENSE inputs, NOT
 * proving/fault inputs: DI HIGH = current flowing = lamp ON (the OPPOSITE of
 * the equipment proving DIs, where HIGH = fault). We learn which DI channels
 * they are from the controller's IO-Config input map (IoConfigInData is indexed
 * by input PORT, value = the equipment assigned; the DI bit the controller reads
 * for that equipment = port - 1). With that, the panel presents the bay lights
 * as ON/OFF instead of PROVING/FAULT. Refetched periodically (config rarely
 * changes); if the bridge is unreachable the map stays empty and the lights fall
 * back to the generic DI behaviour — no harm. */
const LIGHTS1_EQ = 23, LIGHTS2_EQ = 24;
const currentSenseDi = new Map<number, string>();   // di idx -> friendly label
async function refreshIoConfig(): Promise<void> {
  try {
    const res = await fetch(`http://${BRIDGE}/iot/debug/cache`);
    if (!res.ok) return;
    const cache = await res.json() as Record<string, unknown>;
    const csv = cache.IoConfigInData;
    if (typeof csv !== 'string') return;
    const next = new Map<number, string>();
    csv.split(',').map((s) => parseInt(s, 10)).forEach((eq, port) => {
      const di = port - 1;   // controller reads di_bitmap bit (input_map - 1)
      if (di < 0 || di >= N_DO) return;
      if (eq === LIGHTS1_EQ) next.set(di, 'Bay Light 1');
      else if (eq === LIGHTS2_EQ) next.set(di, 'Bay Light 2');
    });
    currentSenseDi.clear();
    for (const [k, v] of next) currentSenseDi.set(k, v);
  } catch { /* bridge not up yet — retry on the timer */ }
}
refreshIoConfig();
setInterval(refreshIoConfig, 30000);

/* Push one channel via FC06. Returns the wire int16 on success. */
async function pushChannel(c: Channel): Promise<number> {
  const hr  = HR_SENSOR_BASE + c.board * N_CH_PER_BOARD + c.ch;
  const v16 = engToHrInt16(c.value, c.type);
  if (!client.isConnected()) throw new Error('LP not connected');
  await client.writeRegister(hr, v16);
  return v16;
}

/* Push the whole 64-register block via one FC16. */
async function pushAll(): Promise<void> {
  if (!client.isConnected()) throw new Error('LP not connected');
  const block: number[] = [];
  for (const c of channels) block.push(engToHrInt16(c.value, c.type));
  await client.writeRegisters(HR_SENSOR_BASE, block);
}

/* Periodic re-push so a brief socket drop doesn't leave the LP stuck
 * on stale values once it reconnects. 5 s is well under the LP's
 * comm-loss safe-mode timeout (also 5 s) so safe-mode never trips
 * just because the injector blipped. */
const REPUSH_MS = 5000;
let pendingRepush = false;
setInterval(async () => {
  if (pendingRepush || !client.isConnected()) return;
  pendingRepush = true;
  try { await pushAll(); }
  catch (e) { console.log(`[push] periodic re-push failed: ${(e as Error).message}`); }
  finally { pendingRepush = false; }
}, REPUSH_MS);

/* ─── Auto-prove ─────────────────────────────────────────────────────
 * "Until we get hardware": drive the STORAGE orbit's digital inputs to the
 * HEALTHY state so the controller's (now-active) failure detection is
 * satisfied. IMPORTANT — the two input groups have OPPOSITE polarity:
 *   • Equipment DIs 0..9 are FAULT inputs (active-high): CheckInputs(EQ)=1
 *     ⇒ the controller reads a FAULT (nova_serialshift.c:77, used as the
 *     failure condition in nova_failures.c). Healthy = LOW (0).
 *   • E-Stop (idx 10) is NORMALLY-CLOSED (lp_engine_shim.c:307: di10=1
 *     healthy, 0 tripped; orbit-unreachable also fail-safes to tripped).
 *     Healthy = HIGH (1).
 * So: hold 0..9 LOW and E-Stop HIGH. Manual "pins" win, so panel writes
 * stick — pin an equipment DI HIGH to fault-test it, or pin E-Stop to 0 to
 * trip it. (Note: the fan also fails on a faulted/absent VFD via
 * nova_vfd_any_faulted(), independent of DI 0..9.) */
const AUTO_PROVE   = (process.env.AUTO_PROVE ?? '1') !== '0';
const AUTOPROVE_MS = parseInt(process.env.AUTOPROVE_MS ?? '1000', 10);
const ESTOP_IDX    = 10;
/** Forced overrides (idx → bool) that win over auto-prove. Set by a manual
 *  DI write; released back to auto with { idx, auto:true }. */
const pinned = new Map<number, boolean>();
/** Last value written to each DI-inject reg, so we only write on change. */
const lastDi = new Map<number, number>();

async function setDi(idx: number, on: boolean): Promise<void> {
  const v = on ? 1 : 0;
  if (lastDi.get(idx) === v) return;
  await client.writeRegister(HR_DI_INJECT + idx, v);
  lastDi.set(idx, v);
}

let autoProveBusy = false;
if (AUTO_PROVE) {
  setInterval(async () => {
    if (autoProveBusy || !client.isConnected()) return;
    autoProveBusy = true;
    try {
      // Equipment DIs 0..9 are FAULT inputs: CheckInputs(EQ)=1 (DI high) is
      // read as a FAULT by the controller (nova_failures.c / nova_serialshift.c:77).
      // So "healthy" = DI LOW (0). Hold them low unless the operator pins one
      // HIGH to fault-test that equipment. Pins win, so manual writes stick.
      for (let i = 0; i < N_DO; i++) {
        await setDi(i, pinned.has(i) ? pinned.get(i)! : false);
      }
      // E-Stop is the opposite — NORMALLY-CLOSED (lp_engine_shim.c:307:
      // di10=1 healthy, 0 tripped). Hold HIGH unless pinned to 0 to trip it.
      await setDi(ESTOP_IDX, pinned.has(ESTOP_IDX) ? pinned.get(ESTOP_IDX)! : true);
    } catch {
      /* transient (socket blip) — next tick retries */
    } finally {
      autoProveBusy = false;
    }
  }, AUTOPROVE_MS);
}

/* ─── REST + static panel ────────────────────────────────────────── */

const __filename = fileURLToPath(import.meta.url);
const __dirname  = path.dirname(__filename);
const PANEL_DIR  = path.resolve(__dirname, '../panel');

function readBody(req: http.IncomingMessage): Promise<string> {
  return new Promise((resolve, reject) => {
    const chunks: Buffer[] = [];
    req.on('data', (c) => chunks.push(c as Buffer));
    req.on('end',  () => resolve(Buffer.concat(chunks).toString('utf8')));
    req.on('error', reject);
  });
}

function jsonRes(res: http.ServerResponse, status: number, body: unknown) {
  res.writeHead(status, { 'content-type': 'application/json' });
  res.end(JSON.stringify(body));
}

function boardLabel(b: number): string {
  if (b === 0) return 'Air Temperatures';
  if (b === 1) return 'Humidity + CO2';
  return `Pile ${b - 1}`;
}

function snapshot() {
  return {
    connected: client.isConnected(),
    target:    client.target(),
    unit:      UNIT,
    boards:    N_BOARDS,
    channelsPerBoard: N_CH_PER_BOARD,
    hrBase:    HR_SENSOR_BASE,
    tempUnit:  tempUnitSym(),   // follows Level-2 Basic Settings
    boardLabels: Array.from({ length: N_BOARDS }, (_, b) => boardLabel(b)),
    channelLabels: Array.from({ length: N_BOARDS }, (_, b) => CH_LABELS[b]),
    // value/wire stay canonical °C; the panel converts for display per tempUnit.
    channels:  channels.map((c) => ({
      board: c.board, ch: c.ch, type: c.type, typeName: typeName(c.type),
      value: c.value, hr: HR_SENSOR_BASE + c.board * N_CH_PER_BOARD + c.ch,
      wire: engToHrInt16(c.value, c.type),
      label: CH_LABELS[c.board]?.[c.ch] ?? `Ch${c.ch}`,
    })),
  };
}

const server = http.createServer(async (req, res) => {
  // CORS for dev; bridge / browser may be on different origins.
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
  res.setHeader('Access-Control-Allow-Headers', 'content-type');
  if (req.method === 'OPTIONS') { res.writeHead(204); return res.end(); }

  const url = new URL(req.url ?? '/', `http://localhost:${PANEL_PORT}`);

  try {
    if (url.pathname === '/api/state') return jsonRes(res, 200, snapshot());

    if (url.pathname === '/api/channel' && req.method === 'POST') {
      const body = JSON.parse(await readBody(req));
      const { board, ch, type, value } = body;
      if (typeof board !== 'number' || typeof ch !== 'number') {
        return jsonRes(res, 400, { error: 'board, ch required' });
      }
      const c = channels[board * N_CH_PER_BOARD + ch];
      if (!c) return jsonRes(res, 404, { error: 'channel not found' });
      if (typeof type === 'number') c.type = type as SensorType;
      // Panel already converts to canonical °C before posting (see panel
      // fromSlider); store as-is.
      if (typeof value === 'number') c.value = value;
      const wire = await pushChannel(c);
      return jsonRes(res, 200, { ok: true, wire });
    }

    if (url.pathname === '/api/push' && req.method === 'POST') {
      await pushAll();
      return jsonRes(res, 200, { ok: true });
    }

    if (url.pathname === '/api/readback') {
      const regs = await client.readRegisters(HR_SENSOR_BASE, N_CHANNELS);
      const decoded = regs.map((raw, i) => ({
        hr: HR_SENSOR_BASE + i,
        raw,
        eng: hrInt16ToEng(raw, channels[i].type),
        type: typeName(channels[i].type),
      }));
      return jsonRes(res, 200, { decoded });
    }

    /* ── Equipment I/O tab ───────────────────────────────────────── */

    if (url.pathname === '/api/eq/state') {
      if (!client.isConnected()) {
        return jsonRes(res, 503, { error: 'LP not connected' });
      }
      // mbtcp client allows one in-flight transaction at a time, so
      // these reads MUST be sequential, not Promise.all.
      const coils     = await client.readCoils(0, N_DO);
      const discretes = await client.readDiscreteInputs(0, N_DI_TOTAL);
      return jsonRes(res, 200, {
        autoProve: AUTO_PROVE,
        do:     coils.map((on, i)    => ({ idx: i, on, label: DO_LABELS[i] })),
        di:     discretes.map((on, i) => ({
          idx: i, on, label: currentSenseDi.get(i) ?? DI_LABELS[i], pinned: pinned.has(i),
          // 'current' = bay-light current-sense (HIGH=ON), 'proving' = equipment
          // fault DI (HIGH=fault), 'activeHigh' = E-Stop/DC24V (HIGH=OK).
          sense: currentSenseDi.has(i) ? 'current' : (i < N_DO ? 'proving' : 'activeHigh'),
        })),
      });
    }

    if (url.pathname === '/api/eq/di' && req.method === 'POST') {
      /* Inject / pin one DI / E-stop / DC24V bit on the STORAGE orbit.
       * body: { idx: 0..14, on: bool }   → pin to that value (wins over
       *                                     auto-prove; for fault testing)
       *       { idx: 0..14, auto: true }  → release the pin (back to
       *                                     auto-prove for DI 0..9 / E-stop) */
      const body = JSON.parse(await readBody(req));
      const idx = Number(body.idx);
      if (!Number.isFinite(idx) || idx < 0 || idx >= N_DI_TOTAL) {
        return jsonRes(res, 400, { error: 'idx out of range' });
      }
      if (body.auto === true) {
        pinned.delete(idx);
        return jsonRes(res, 200, { ok: true, pinned: false });
      }
      const on = Boolean(body.on);
      pinned.set(idx, on);
      lastDi.delete(idx);          // force the write below + next auto tick
      await client.writeRegister(HR_DI_INJECT + idx, on ? 1 : 0);
      lastDi.set(idx, on ? 1 : 0);
      return jsonRes(res, 200, { ok: true, pinned: true, on });
    }

    /* Static panel. */
    let p = url.pathname === '/' ? '/index.html' : url.pathname;
    const filePath = path.join(PANEL_DIR, p);
    if (!filePath.startsWith(PANEL_DIR)) {
      res.writeHead(403); return res.end('forbidden');
    }
    fs.readFile(filePath, (err, data) => {
      if (err) { res.writeHead(404); return res.end('not found'); }
      const ext = path.extname(filePath);
      const ct = ext === '.html' ? 'text/html'
              : ext === '.js'   ? 'application/javascript'
              : ext === '.css'  ? 'text/css'
              : 'application/octet-stream';
      res.writeHead(200, { 'content-type': ct });
      res.end(data);
    });
  } catch (e) {
    jsonRes(res, 500, { error: (e as Error).message });
  }
});

server.listen(PANEL_PORT, () => {
  console.log(`╔══════════════════════════════════════════╗`);
  console.log(`║  Constellation Sensor Injector           ║`);
  console.log(`╚══════════════════════════════════════════╝`);
  console.log(`  Target  : ${HOST}:${PORT} (unit ${UNIT})`);
  console.log(`  Panel   : http://localhost:${PANEL_PORT}/`);
  console.log(`  HR base : ${HR_SENSOR_BASE} (${N_BOARDS} boards × ${N_CH_PER_BOARD} ch = ${N_CHANNELS} regs)`);
  console.log(`  UNDEF   : 0x${SENSOR_VAL_UNDEF.toString(16).toUpperCase()}`);
  console.log(`  AutoProve: ${AUTO_PROVE ? `ON (DI 0..9 held no-fault, E-stop healthy, every ${AUTOPROVE_MS}ms)` : 'OFF (manual DI only)'}`);
  console.log(`  TempUnit: ${tempUnitSym()} (follows Basic Settings via bridge ${BRIDGE})`);
});
