/**
 * Orbit Simulator — Multi-Orbit Entry Point
 * ──────────────────────────────────────────
 * Starts the Orbit I/O board simulator with support for multiple orbits:
 *   • Each orbit gets Modbus TCP + REST API on sequential ports
 *   • Master API on port 9000 for orbit management
 *   • VFD simulator shared across all orbits
 *
 * Port scheme:
 *   Orbit ID N → Modbus TCP 5500+N, API 9008+N
 *   Orbit 2 → TCP 5502, API 9010 (default/main)
 *   Orbit 3 → TCP 5503, API 9011 (expansion 1)
 *
 * Usage:
 *   npm start                               # starts orbit 2 by default
 *   ORBIT_IDS=2,3,4 npm start               # start multiple orbits
 */

import * as http from 'http';
import { OrbitSimulator, OrbitRole } from './orbitSimulator.js';
import { VFDSimulator } from './vfdSimulator.js';
import { loadConfig, saveConfig } from './simConfig.js';

const MASTER_API_PORT = parseInt(process.env.MASTER_API_PORT ?? '9005', 10);
const VFD_PORT        = parseInt(process.env.VFD_PORT ?? '5020', 10);
const VFD_RTU_PORT    = parseInt(process.env.VFD_RTU_PORT ?? '5520', 10);

// Parse orbit IDs from env or default to just orbit 2
const defaultOrbitIds = process.env.ORBIT_IDS?.split(',').map(s => parseInt(s.trim(), 10)).filter(n => !isNaN(n)) ?? [2];

console.log('╔══════════════════════════════════════════╗');
console.log('║   Constellation Multi-Orbit Simulator    ║');
console.log('╚══════════════════════════════════════════╝');

// ─── Orbit Manager ─────────────────────────────────────────────────────────

interface OrbitEntry {
  orbit: OrbitSimulator;
  tcpPort: number;
  apiPort: number;
  role: OrbitRole;
}

const orbits = new Map<number, OrbitEntry>();

/** Calculate ports for a given orbit ID */
function getPortsForOrbit(id: number): { tcpPort: number; apiPort: number } {
  return {
    tcpPort: 5500 + id,  // Orbit 2 → 5502, Orbit 3 → 5503
    apiPort: 9008 + id,  // Orbit 2 → 9010, Orbit 3 → 9011
  };
}

// Start the VFD drive simulator (shared across all orbits)
const vfdSim = new VFDSimulator(VFD_PORT);
vfdSim.start();
// Also start RTU server — orbit connects here as an RTU client (mirrors production RS-485 bus B)
if (VFD_RTU_PORT > 0) {
  vfdSim.startRtu(VFD_RTU_PORT);
}

/** Start a single orbit instance */
function startOrbit(id: number, role: OrbitRole = OrbitRole.STORAGE): boolean {
  if (orbits.has(id)) {
    console.log(`[Manager] Orbit ${id} already running`);
    return false;
  }
  const { tcpPort, apiPort } = getPortsForOrbit(id);
  const orbit = new OrbitSimulator(id, role);
  orbit.start(tcpPort, apiPort);
  
  // Wire VFD fault injection
  orbit.setVFDFaultHandler((unitId, faultCode) => vfdSim.injectFault(unitId, faultCode));
  orbit.setVFDSimulator(vfdSim);
  
  // Use orbit.state.role: the constructor may have loaded a different role
  // from the per-orbit save file (overriding the parameter we passed in).
  orbits.set(id, { orbit, tcpPort, apiPort, role: orbit.state.role });
  console.log(`[Manager] Started Orbit ${id} (${OrbitRole[orbit.state.role]}) — Modbus:${tcpPort}, API:${apiPort}`);
  return true;
}

/** Stop a single orbit instance */
function stopOrbit(id: number): boolean {
  const entry = orbits.get(id);
  if (!entry) {
    console.log(`[Manager] Orbit ${id} not running`);
    return false;
  }
  entry.orbit.stop();
  orbits.delete(id);
  console.log(`[Manager] Stopped Orbit ${id}`);
  return true;
}

// ─── Master Management API ─────────────────────────────────────────────────

const masterServer = http.createServer((req, res) => {
  // CORS
  res.setHeader('Access-Control-Allow-Origin', '*');
  res.setHeader('Access-Control-Allow-Methods', 'GET, POST, DELETE, OPTIONS');
  res.setHeader('Access-Control-Allow-Headers', 'Content-Type');
  if (req.method === 'OPTIONS') { res.writeHead(204); res.end(); return; }
  
  const url = new URL(req.url ?? '/', `http://localhost:${MASTER_API_PORT}`);
  
  // GET /api/orbits — list running orbits
  if (req.method === 'GET' && url.pathname === '/api/orbits') {
    const list = Array.from(orbits.entries()).map(([id, e]) => {
      // Always read role from the live orbit state — it may have been
      // changed via POST /api/role on the orbit's own panel.
      const liveRole = e.orbit.state.role;
      e.role = liveRole;
      return {
        id,
        tcpPort: e.tcpPort,
        apiPort: e.apiPort,
        role: liveRole,
        roleName: OrbitRole[liveRole],
        panelUrl: `http://localhost:${e.apiPort}/`,
      };
    });
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ orbits: list, roles: ['UNASSIGNED', 'STORAGE', 'GDC', 'TRITON', 'PULSAR'] }));
    return;
  }
  
  // POST /api/orbits { id: number, role?: number|string } — add an orbit
  if (req.method === 'POST' && url.pathname === '/api/orbits') {
    let body = '';
    req.on('data', c => body += c);
    req.on('end', () => {
      try {
        const { id, role: rawRole } = JSON.parse(body);
        if (typeof id !== 'number' || id < 2 || id > 33) {
          res.writeHead(400, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ ok: false, error: 'id must be 2-33' }));
          return;
        }
        // Parse role: accept number (0-4) or string ("STORAGE", "GDC", "TRITON", "PULSAR")
        let role: OrbitRole = OrbitRole.STORAGE;
        if (typeof rawRole === 'number' && rawRole >= 0 && rawRole <= 4) {
          role = rawRole;
        } else if (typeof rawRole === 'string') {
          const parsed = OrbitRole[rawRole.toUpperCase() as keyof typeof OrbitRole];
          if (parsed !== undefined) role = parsed;
        }
        const ok = startOrbit(id, role);
        persistOrbitIds();
        res.writeHead(ok ? 200 : 409, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok, id, role, roleName: OrbitRole[role], ...getPortsForOrbit(id) }));
      } catch {
        res.writeHead(400, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: false, error: 'Invalid JSON' }));
      }
    });
    return;
  }
  
  // DELETE /api/orbits/:id — remove an orbit
  const delMatch = url.pathname.match(/^\/api\/orbits\/(\d+)$/);
  if (req.method === 'DELETE' && delMatch) {
    const id = parseInt(delMatch[1], 10);
    const ok = stopOrbit(id);
    persistOrbitIds();
    res.writeHead(ok ? 200 : 404, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ ok }));
    return;
  }
  
  // 404
  res.writeHead(404, { 'Content-Type': 'application/json' });
  res.end(JSON.stringify({ error: 'Not found' }));
});

masterServer.listen(MASTER_API_PORT, () => {
  console.log(`[Manager] Master API listening on http://localhost:${MASTER_API_PORT}/`);
  console.log(`          GET  /api/orbits       — list orbits`);
  console.log(`          POST /api/orbits       — add orbit { id }`);
  console.log(`          DELETE /api/orbits/:id — remove orbit`);
});

// ─── Persist orbit IDs for restart ────────────────────────────────────────
//
// Role is NOT stored here — it lives in each orbit's own state file
// (orbit-state-N.json), which is the simulator's stand-in for the board's
// on-arm NVRAM.  The manager only needs to know which slots to instantiate;
// each orbit reloads its own role at boot.

const ORBITS_CONFIG_KEY = 'active-orbit-ids';

function persistOrbitIds(): void {
  const ids = Array.from(orbits.keys());
  saveConfig(ORBITS_CONFIG_KEY, ids);
}

function loadPersistedOrbitIds(): number[] {
  const saved = loadConfig(ORBITS_CONFIG_KEY) as Array<number | { id: number }> | null;
  if (!Array.isArray(saved) || saved.length === 0) return defaultOrbitIds;
  // Tolerate the legacy {id, role}[] shape that briefly existed.
  return saved.map(item => typeof item === 'number' ? item : item.id).filter(n => Number.isFinite(n));
}

// ─── Startup ───────────────────────────────────────────────────────────────

// Default roles applied ONLY when an orbit is started for the very first
// time (no per-orbit save file exists yet).  Once the orbit has run once,
// its own state file is the sole source of truth for its role.
const DEFAULT_ROLES: Record<number, OrbitRole> = { 3: OrbitRole.GDC };

const initialIds = loadPersistedOrbitIds();
console.log(`[Manager] Starting orbits: ${initialIds.join(', ')}`);
for (const id of initialIds) {
  // Pass the default role as a hint; OrbitSimulator's constructor will
  // override it with the persisted role if a save file exists.
  startOrbit(id, DEFAULT_ROLES[id] ?? OrbitRole.STORAGE);
}

// Activity blinker reset (for all orbits)
setInterval(() => {
  for (const { orbit } of orbits.values()) {
    orbit.state.vfdActivity = false;
    orbit.state.sensorActivity = false;
  }
}, 2000);

// Periodic save (per-orbit state — role + sensors + setpoints all live here).
setInterval(() => {
  for (const { orbit } of orbits.values()) {
    orbit.save();
  }
}, 30000);

// Graceful shutdown
function shutdown(): void {
  console.log('\n[Manager] Shutting down...');
  for (const { orbit } of orbits.values()) {
    orbit.stop();
  }
  vfdSim.stop();
  masterServer.close();
  process.exit(0);
}

process.on('SIGINT',  shutdown);
process.on('SIGTERM', shutdown);
