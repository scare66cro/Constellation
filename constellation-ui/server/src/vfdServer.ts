/**
 * VFD Mini-Server — Modbus TCP fan control ONLY.
 *
 * Runs alongside the original production stack (gellertserverd + iotclient).
 * Listens on its own port (default 3002) and serves /vfd/* endpoints.
 * Does NOT touch serial, does NOT replace any existing service.
 *
 * lighttpd proxies /vfd/* → this server.
 *
 * Integrations:
 *   - Polls gellertserverd CGI (/get/data?page=main) for ARM fan speed
 *   - Forwards fan speed to VFD drives when mode is 'vfd'
 *   - Monitors VFD faults → exposes /vfd/alarms for the UI alarm system
 *   - Respects fan fail delay before escalating to FAILURE mode
 *
 * Endpoints:
 *   GET  /vfd/fans          → drive snapshots
 *   GET  /vfd/alarms        → active VFD fault alarms (KEY=Text format)
 *   POST /vfd/fans/control  → start/stop/reset/direction
 *   POST /vfd/fans/param    → write single register
 *   POST /vfd/fans/set-all  → bulk command to all drives
 *   GET  /vfd/fans/config   → fan control mode
 *   POST /vfd/fans/config   → set fan control mode
 *   POST /vfd/fans/meta     → set drive label/manufacturer
 */

import express from 'express';
import http from 'http';
import { VFDClient } from './vfdClient.js';
import { getProfile } from './vfdRegisterMaps.js';
import * as fs from 'fs';
import * as path from 'path';

const PORT = parseInt(process.env.VFD_PORT_HTTP || '3002', 10);
const VFD_HOST = process.env.VFD_HOST || '127.0.0.1';
const VFD_PORT = parseInt(process.env.VFD_PORT || '502', 10);
const VFD_MAX_SCAN = parseInt(process.env.VFD_MAX_SCAN || '8', 10);

// Simple JSON config file for fan control mode
const CONFIG_DIR = path.resolve(
  typeof import.meta?.url === 'string'
    ? path.dirname(new URL(import.meta.url).pathname.replace(/^\/([A-Z]:)/, '$1'))
    : __dirname,
  '..',
  '.vfd-config',
);

function loadConfig<T>(key: string): T | undefined {
  const file = path.join(CONFIG_DIR, `${key}.json`);
  try { return JSON.parse(fs.readFileSync(file, 'utf8')); } catch { return undefined; }
}

function saveConfig<T>(key: string, value: T): void {
  if (!fs.existsSync(CONFIG_DIR)) fs.mkdirSync(CONFIG_DIR, { recursive: true });
  fs.writeFileSync(path.join(CONFIG_DIR, `${key}.json`), JSON.stringify(value));
}

// ── Setup ──

const app = express();
app.use(express.json());

const vfdClient = new VFDClient({
  host: VFD_HOST,
  port: VFD_PORT,
  maxScanId: VFD_MAX_SCAN,
  pollIntervalMs: 1000,
});

// ── CGI Poller — reads ARM fan speed from gellertserverd ──

const CGI_URL = process.env.CGI_URL || 'http://localhost/get/data?page=main';
const CGI_POLL_MS = 3000;

/** Parse the gellertserverd CGI response (JavaScript var declarations) */
function parseCgiVars(body: string): Record<string, string> {
  const vars: Record<string, string> = {};
  const re = /var\s+(\w+)\s*=\s*"([^"]*)"/g;
  let m: RegExpExecArray | null;
  while ((m = re.exec(body)) !== null) {
    vars[m[1]] = m[2];
  }
  return vars;
}

/** Fetch CGI data via http.get (no external deps) */
function fetchCgi(url: string): Promise<string> {
  return new Promise((resolve, reject) => {
    const req = http.get(url, { timeout: 5000 }, (res) => {
      if (res.statusCode !== 200) {
        res.resume();
        reject(new Error(`CGI HTTP ${res.statusCode}`));
        return;
      }
      let data = '';
      res.on('data', (chunk: Buffer) => { data += chunk.toString(); });
      res.on('end', () => resolve(data));
    });
    req.on('error', reject);
    req.on('timeout', () => { req.destroy(); reject(new Error('CGI timeout')); });
  });
}

// ── Fan Speed Forwarding State ──
let lastForwardedSpeed = -1;
let cgiPollTimer: ReturnType<typeof setInterval> | null = null;
const SPEED_DEADBAND = 2; // ignore ±2% changes to prevent PID-induced bouncing

// ── VFD Fault → Alarm State ──
let vfdFaultActive = false;
let vfdFailureActive = false;
let vfdFailureTimer: ReturnType<typeof setTimeout> | null = null;
let activeVfdAlarms: string[] = [];  // WARN_VFD=text format for UI

/** Poll CGI and forward fan speed to VFD drives */
async function pollCgiAndForward(): Promise<void> {
  const mode = loadConfig<string>('fan-control-mode') ?? 'legacy';
  if (mode !== 'vfd') {
    lastForwardedSpeed = -1;
    return;
  }

  try {
    const body = await fetchCgi(CGI_URL);
    const vars = parseCgiVars(body);
    const mainRaw = vars['MainData'];

    // If CGI didn't return a valid MainData, skip this cycle (keep last speed).
    // The gellertserverd CGI sometimes returns truncated responses (~1013 chars)
    // that don't include MainData — we must NOT treat this as speed=0.
    if (!mainRaw) return;

    const mainFields = mainRaw.split(',');

    // MainData[10] = current fan speed (percentage string or 'Off')
    const rawSpeed = mainFields[10];
    if (rawSpeed === undefined) return;
    const speedPercent = (rawSpeed === 'Off' || rawSpeed === '--') ? 0 : (parseInt(rawSpeed, 10) || 0);

    // Only forward when change exceeds dead-band to prevent PID jitter
    if (lastForwardedSpeed >= 0 && Math.abs(speedPercent - lastForwardedSpeed) < SPEED_DEADBAND) return;
    lastForwardedSpeed = speedPercent;

    const drives = vfdClient.getDrives();
    for (const drv of drives) {
      if (!drv.online || drv.faulted) continue;
      if (speedPercent > 0) {
        const ref = speedPercent * 100; // firmware 0-100% → VFD 0-10000
        vfdClient.sendAction(drv.unitId, 'start', ref).catch(() => {});
      } else {
        vfdClient.sendAction(drv.unitId, 'stop').catch(() => {});
      }
    }
    const onlineCount = drives.filter(d => d.online && !d.faulted).length;
    console.log(`[VFD Fan] Forwarded fan speed ${speedPercent}% to ${onlineCount} drives`);
  } catch {
    // CGI unavailable — will retry next cycle
  }
}

/** Check VFD drives for faults and update alarm state */
function checkVfdFaults(): void {
  const mode = loadConfig<string>('fan-control-mode') ?? 'legacy';
  if (mode !== 'vfd') {
    // Clear everything when not in VFD mode
    if (activeVfdAlarms.length > 0 || vfdFaultActive) {
      activeVfdAlarms = [];
      vfdFaultActive = false;
      vfdFailureActive = false;
      if (vfdFailureTimer) { clearTimeout(vfdFailureTimer); vfdFailureTimer = null; }
    }
    return;
  }

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

  activeVfdAlarms = faultAlarms;

  if (faultAlarms.length > 0) {
    // Stop all drives when any has a fault
    for (const drv of drives) {
      if (drv.online) {
        vfdClient.sendAction(drv.unitId, 'stop').catch(() => {});
      }
    }

    if (!vfdFaultActive) {
      vfdFaultActive = true;
      console.log(`[VFD Fault] ${faultAlarms.length} fault(s) detected — alarm active`);

      // Read fan fail delay from CGI (FailureData1 → P2FailuresData field [1])
      fetchCgi(CGI_URL).then(body => {
        const vars = parseCgiVars(body);
        const failFields = (vars['P2FailuresData'] || '').split(',');
        const fanFailMinutes = parseInt(failFields[1], 10) || 1;
        const delayMs = fanFailMinutes * 60_000;
        console.log(`[VFD Fault] FAILURE escalation in ${fanFailMinutes} min`);

        vfdFailureTimer = setTimeout(() => {
          vfdFailureTimer = null;
          if (vfdFaultActive) {
            vfdFailureActive = true;
            console.log(`[VFD Fault] Fan fail delay elapsed — SYSTEM FAILURE active`);
          }
        }, delayMs);
      }).catch(() => {
        // Can't read delay — use 1 minute default
        vfdFailureTimer = setTimeout(() => {
          vfdFailureTimer = null;
          if (vfdFaultActive) {
            vfdFailureActive = true;
            console.log(`[VFD Fault] Fan fail delay elapsed (default 1 min) — SYSTEM FAILURE active`);
          }
        }, 60_000);
      });
    }
  } else if (vfdFaultActive) {
    // All faults cleared
    vfdFaultActive = false;
    vfdFailureActive = false;
    if (vfdFailureTimer) {
      clearTimeout(vfdFailureTimer);
      vfdFailureTimer = null;
      console.log('[VFD Fault] Faults cleared before delay elapsed — failure averted');
    } else {
      console.log('[VFD Fault] All faults cleared, resuming normal operation');
    }
    lastForwardedSpeed = -1; // Force re-forward on next poll
  }
}

// Register VFD fault checker on every poll update
vfdClient.onUpdate(() => {
  checkVfdFaults();
});

// ── Routes (all under /vfd) ──

/** Active VFD alarms in KEY=Text format for the UI alarm system */
app.get('/vfd/alarms', (_req, res) => {
  res.json({
    alarms: activeVfdAlarms,
    failureActive: vfdFailureActive,
    faultActive: vfdFaultActive,
  });
});

app.get('/vfd/fans', (_req, res) => {
  res.json({ drives: vfdClient.getDrives() });
});

app.post('/vfd/fans/control', async (req, res) => {
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
    ok = await vfdClient.writeDrive(unitId, controlWord, speedRefPercent);
  } else {
    res.status(400).json({ ok: false, error: 'action or controlWord required' });
    return;
  }
  res.json({ ok });
});

app.post('/vfd/fans/param', async (req, res) => {
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

app.get('/vfd/fans/config', (_req, res) => {
  const mode = loadConfig<string>('fan-control-mode') ?? 'legacy';
  res.json({ mode });
});

app.post('/vfd/fans/config', (req, res) => {
  const { mode } = req.body;
  if (mode !== 'legacy' && mode !== 'vfd') {
    res.status(400).json({ ok: false, error: 'mode must be "legacy" or "vfd"' });
    return;
  }
  saveConfig('fan-control-mode', mode);
  console.log(`[VFD] Fan control mode set to: ${mode}`);
  res.json({ ok: true, mode });
});

app.post('/vfd/fans/meta', (req, res) => {
  const { unitId, label, manufacturer } = req.body;
  if (typeof unitId !== 'number') {
    res.status(400).json({ ok: false, error: 'unitId required' });
    return;
  }
  vfdClient.setDriveMeta(unitId, { label, manufacturer });
  res.json({ ok: true });
});

app.post('/vfd/fans/set-all', async (req, res) => {
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

// ── Start ──

async function main() {
  console.log(`[VFD Server] Starting...`);
  console.log(`[VFD Server] Modbus: ${VFD_HOST}:${VFD_PORT}, scan 1-${VFD_MAX_SCAN}`);
  console.log(`[VFD Server] CGI poll: ${CGI_URL} every ${CGI_POLL_MS}ms`);

  await vfdClient.start();

  // Start CGI poller for ARM fan speed forwarding
  cgiPollTimer = setInterval(pollCgiAndForward, CGI_POLL_MS);
  // Initial poll immediately
  pollCgiAndForward().catch(() => {});

  app.listen(PORT, () => {
    console.log(`[VFD Server] Listening on port ${PORT}`);
    console.log(`[VFD Server] Endpoints: /vfd/fans, /vfd/alarms, /vfd/fans/control, etc.`);
  });

  process.on('SIGTERM', () => {
    console.log('[VFD Server] Shutting down...');
    if (cgiPollTimer) clearInterval(cgiPollTimer);
    if (vfdFailureTimer) clearTimeout(vfdFailureTimer);
    vfdClient.stop();
    process.exit(0);
  });
}

main().catch(err => {
  console.error('[VFD Server] Fatal:', err);
  process.exit(1);
});
