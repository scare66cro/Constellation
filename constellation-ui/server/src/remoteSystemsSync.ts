/*
 * remoteSystemsSync.ts — bridge-managed list of OTHER Constellation
 * panels on the LAN, with UUID-keyed auto-heal across DHCP changes.
 *
 * Design
 * ------
 * The Level-2 "Remote Systems" page lets the operator name a set of
 * peer panels they want to monitor. The list previously rode the
 * legacy CSV `P2NodeSetupData` cache, which had no concept of
 * persistent identity — a DHCP IP change broke the entry silently
 * and renaming a panel made it unrecognizable to its peers.
 *
 * This module replaces the bridge-side state with:
 *   - JSON-backed list of {name?, host, port, novaId?} tuples
 *   - Polled liveness check via `/iot/identity` (30 s cadence)
 *   - Persistent (host:port → novaId) cache so DHCP can be auto-healed
 *   - Same /24 LAN sweep as master-slave discovery
 *   - On N consecutive failures: find a peer whose novaId matches the
 *     cached one at a NEW host:port, rewrite the entry in place, save
 *     to disk, continue. Operator never has to touch the list.
 *
 * Trust root: the persistent novaId UUID generated on first boot
 * (dataCache.getNovaId). Survives panel renames AND DHCP moves.
 */
import { EventEmitter } from 'node:events';
import * as os from 'node:os';
import * as fs from 'node:fs';
import * as path from 'node:path';
import { randomUUID } from 'node:crypto';

const PEER_HTTP_PORT      = 9001;
const POLL_INTERVAL_MS    = 30_000;       /* identity probe cadence */
const PROBE_TIMEOUT_MS    = 800;
const HEAL_FAILURE_THRESHOLD = 3;          /* ~90 s of dead air */

const STATE_FILE_PATHS = [
  process.env.REMOTE_SYSTEMS_FILE,
  '/var/lib/agristar/remote-systems.json',
  path.join(os.homedir() || '/tmp', '.agristar-remote-systems.json'),
  '/tmp/agristar-remote-systems.json',
].filter(Boolean) as string[];

export interface RemoteSystem {
  /** Bridge-local UUID for this entry. Stable across renames AND
   *  novaId changes; what the UI / groups should reference. */
  id: string;
  /** Operator-set label (defaults to the peer's panelName). */
  name: string;
  host: string;
  port: number;
  /** Persistent UUID of the peer panel — populated once we've
   *  successfully probed `/iot/identity`. Auto-heal key. */
  novaId: string;
  /** Most recent panelName seen via /iot/identity (display only). */
  panelName: string;
  /* Liveness tracking. */
  online: boolean;
  lastAttemptAt:       number | null;
  lastSuccessAt:       number | null;
  lastError:           string | null;
  consecutiveFailures: number;
  /** If auto-heal moved this entry from one host to another, the
   *  previous host:port string, for one operator-visible cycle. */
  healedFromHost:      string | null;
}

interface PersistedState {
  version: 1;
  systems: RemoteSystem[];
}

interface IdentityProbe {
  host: string;
  port: number;
  novaId: string;
  panelName: string;
}

export class RemoteSystemsSync extends EventEmitter {
  private timer: NodeJS.Timeout | null = null;
  private systems: RemoteSystem[] = [];
  private stateFile: string | null = null;
  private healInFlight = new Set<string>();   /* by entry.id */

  constructor(private getLocalNovaId: () => string = () => '') {
    super();
    this.load();
  }

  start(): void {
    if (this.timer) return;
    this.timer = setInterval(() => { void this.tick(); }, POLL_INTERVAL_MS);
    setImmediate(() => { void this.tick(); });
    console.log(`[RemoteSystems] started (poll every ${POLL_INTERVAL_MS / 1000} s)`);
  }

  stop(): void {
    if (this.timer) { clearInterval(this.timer); this.timer = null; }
  }

  list(): RemoteSystem[] {
    return this.systems.map((s) => ({ ...s }));
  }

  /** Add a new remote. Caller supplies host+port; name defaults to
   *  whatever the peer reports. novaId fills in on first probe. */
  async add(input: { name?: string; host: string; port?: number }): Promise<RemoteSystem> {
    const host = (input.host || '').trim();
    const port = input.port && input.port > 0 ? input.port : PEER_HTTP_PORT;
    if (!host) throw new Error('host required');

    /* Prevent duplicates by host:port. The novaId-based dedup happens
     * after the first probe (see mergeOrUpdate). */
    const existing = this.systems.find((s) => s.host === host && s.port === port);
    if (existing) return existing;

    const probe = await probeIdentity(host, port);
    /* Avoid adding the local panel as a remote of itself. */
    if (probe && probe.novaId && probe.novaId === this.getLocalNovaId()) {
      throw new Error('cannot add local panel as a remote');
    }

    /* If we already track this novaId at a different host:port, treat
     * the add as an explicit relocate instead of a new entry. */
    if (probe && probe.novaId) {
      const dup = this.systems.find((s) => s.novaId === probe.novaId);
      if (dup) {
        dup.host = host;
        dup.port = port;
        dup.panelName = probe.panelName || dup.panelName;
        if (!input.name) dup.name = probe.panelName || dup.name;
        dup.healedFromHost = null;
        dup.consecutiveFailures = 0;
        this.save();
        return dup;
      }
    }

    const entry: RemoteSystem = {
      id:    randomUUID(),
      name:  input.name?.trim() || probe?.panelName || `${host}:${port}`,
      host,
      port,
      novaId:    probe?.novaId    ?? '',
      panelName: probe?.panelName ?? '',
      online:    !!probe,
      lastAttemptAt: Date.now(),
      lastSuccessAt: probe ? Date.now() : null,
      lastError:     probe ? null : 'no_response',
      consecutiveFailures: probe ? 0 : 1,
      healedFromHost: null,
    };
    this.systems.push(entry);
    this.save();
    return entry;
  }

  remove(id: string): boolean {
    const i = this.systems.findIndex((s) => s.id === id);
    if (i < 0) return false;
    this.systems.splice(i, 1);
    this.save();
    return true;
  }

  rename(id: string, name: string): boolean {
    const s = this.systems.find((s) => s.id === id);
    if (!s) return false;
    s.name = name.trim() || s.name;
    this.save();
    return true;
  }

  /** Sweep the local /24 and return peers reachable via /iot/identity
   *  that aren't already in the list. */
  async discover(): Promise<IdentityProbe[]> {
    const hosts = enumerateLocalSubnetHosts();
    const probes = await Promise.all(
      hosts.map((h) => probeIdentity(h, PEER_HTTP_PORT)),
    );
    const seen = new Set(this.systems.map((s) => s.novaId).filter(Boolean));
    const local = this.getLocalNovaId();
    return probes
      .filter((p): p is IdentityProbe => p !== null
                                       && !!p.novaId
                                       && p.novaId !== local
                                       && !seen.has(p.novaId));
  }

  // ── Polling tick ───────────────────────────────────────────────────
  private async tick(): Promise<void> {
    if (this.systems.length === 0) return;
    await Promise.allSettled(
      this.systems.map((s) => this.probeOne(s)),
    );
  }

  private async probeOne(s: RemoteSystem): Promise<void> {
    s.lastAttemptAt = Date.now();
    const probe = await probeIdentity(s.host, s.port);
    if (!probe) {
      s.online = false;
      s.lastError = 'no_response';
      s.consecutiveFailures += 1;
      this.save();
      this.maybeHeal(s);
      return;
    }

    /* Identity drift: same host but different novaId means the peer
     * hardware was swapped. Adopt the new novaId — the operator's
     * intent is "monitor whatever lives at this host". */
    if (s.novaId && probe.novaId && probe.novaId !== s.novaId) {
      console.log(`[RemoteSystems] novaId at ${s.host}:${s.port} changed (${s.novaId} → ${probe.novaId}); adopting`);
    }
    s.novaId = probe.novaId;
    s.panelName = probe.panelName;
    s.online = true;
    s.lastSuccessAt = Date.now();
    s.lastError = null;
    s.consecutiveFailures = 0;
    /* Clear the heal banner once we've had one good cycle. */
    s.healedFromHost = null;
    this.save();
  }

  // ── Auto-heal ──────────────────────────────────────────────────────
  private maybeHeal(s: RemoteSystem): void {
    if (s.consecutiveFailures < HEAL_FAILURE_THRESHOLD) return;
    if (!s.novaId) {
      /* Never successfully probed; nothing to match against. */
      return;
    }
    if (this.healInFlight.has(s.id)) return;
    this.healInFlight.add(s.id);
    void (async () => {
      try { await this.heal(s); }
      catch (err) { console.warn('[RemoteSystems] heal failed for', s.host, err); }
      finally { this.healInFlight.delete(s.id); }
    })();
  }

  private async heal(s: RemoteSystem): Promise<void> {
    const expectedId = s.novaId;
    const oldHost = `${s.host}:${s.port}`;
    const hosts = enumerateLocalSubnetHosts();
    const probes = await Promise.all(
      hosts.map((h) => probeIdentity(h, PEER_HTTP_PORT)),
    );
    const match = probes.find(
      (p): p is IdentityProbe => p !== null && p.novaId === expectedId,
    );
    if (!match) {
      console.log(`[RemoteSystems] heal: no peer with novaId ${expectedId} found (was ${oldHost}, name="${s.name}")`);
      return;
    }
    if (match.host === s.host && match.port === s.port) {
      /* Same host responded to /identity but not to /probeOne's path?
       * Just clear the failure counter. */
      s.consecutiveFailures = 0;
      this.save();
      return;
    }

    /* Collapse a duplicate if the operator already had the new host
     * listed under a different entry. */
    const dup = this.systems.find(
      (x) => x.id !== s.id && x.host === match.host && x.port === match.port,
    );
    if (dup) {
      this.systems = this.systems.filter((x) => x.id !== dup.id);
    }

    s.healedFromHost = oldHost;
    s.host = match.host;
    s.port = match.port;
    s.panelName = match.panelName || s.panelName;
    /* Keep the operator's name override if they set one explicitly,
     * otherwise track the peer's panelName. */
    if (!s.name || s.name === oldHost || s.name === s.panelName) {
      s.name = match.panelName || s.name;
    }
    s.consecutiveFailures = 0;
    s.online = true;
    s.lastError = null;
    s.lastSuccessAt = Date.now();
    this.save();
    console.log(`[RemoteSystems] healed "${s.name}" (${expectedId}): ${oldHost} → ${match.host}:${match.port}`);
  }

  // ── Disk persistence ───────────────────────────────────────────────
  private load(): void {
    for (const candidate of STATE_FILE_PATHS) {
      try {
        if (!fs.existsSync(candidate)) continue;
        const raw = fs.readFileSync(candidate, 'utf8');
        const j = JSON.parse(raw) as PersistedState;
        if (j && Array.isArray(j.systems)) {
          this.systems = j.systems.map(normalizeOnLoad);
        }
        this.stateFile = candidate;
        return;
      } catch {
        /* keep trying */
      }
    }
    /* Pick first writable location for future saves. */
    for (const candidate of STATE_FILE_PATHS) {
      try {
        const dir = path.dirname(candidate);
        fs.mkdirSync(dir, { recursive: true });
        fs.writeFileSync(candidate, JSON.stringify({ version: 1, systems: [] }), 'utf8');
        this.stateFile = candidate;
        return;
      } catch { /* keep trying */ }
    }
  }

  private save(): void {
    if (!this.stateFile) return;
    try {
      const out: PersistedState = { version: 1, systems: this.systems };
      fs.writeFileSync(this.stateFile, JSON.stringify(out, null, 2), 'utf8');
    } catch (err) {
      console.warn('[RemoteSystems] failed to persist state:', err);
    }
    /* Single chokepoint for any list mutation \u2014 listeners (wsManager
     * tcpip-data broadcast) hook here so the header selector reflects
     * add/remove/heal without waiting for the periodic push tick. */
    this.emit('change');
  }
}

// ─── Helpers ─────────────────────────────────────────────────────────
function normalizeOnLoad(raw: any): RemoteSystem {
  return {
    id:    typeof raw?.id    === 'string' && raw.id    ? raw.id    : randomUUID(),
    name:  typeof raw?.name  === 'string' ? raw.name  : '',
    host:  typeof raw?.host  === 'string' ? raw.host  : '',
    port:  Number.isInteger(raw?.port) && raw.port > 0 ? raw.port : PEER_HTTP_PORT,
    novaId:    typeof raw?.novaId    === 'string' ? raw.novaId    : '',
    panelName: typeof raw?.panelName === 'string' ? raw.panelName : '',
    online:    !!raw?.online,
    lastAttemptAt:       typeof raw?.lastAttemptAt === 'number' ? raw.lastAttemptAt : null,
    lastSuccessAt:       typeof raw?.lastSuccessAt === 'number' ? raw.lastSuccessAt : null,
    lastError:           typeof raw?.lastError     === 'string' ? raw.lastError     : null,
    consecutiveFailures: Number.isInteger(raw?.consecutiveFailures) ? raw.consecutiveFailures : 0,
    healedFromHost:      typeof raw?.healedFromHost === 'string' ? raw.healedFromHost : null,
  };
}

async function probeIdentity(host: string, port: number): Promise<IdentityProbe | null> {
  const ctrl = new AbortController();
  const t = setTimeout(() => ctrl.abort(), PROBE_TIMEOUT_MS);
  try {
    const res = await fetch(`http://${host}:${port}/iot/identity`, { signal: ctrl.signal });
    if (!res.ok) return null;
    const j = await res.json() as { novaId?: string; panelName?: string };
    if (!j || typeof j.novaId !== 'string' || !j.novaId) return null;
    return {
      host, port,
      novaId:    j.novaId,
      panelName: typeof j.panelName === 'string' ? j.panelName : '',
    };
  } catch {
    return null;
  } finally {
    clearTimeout(t);
  }
}

function enumerateLocalSubnetHosts(): string[] {
  const out = new Set<string>();
  const ifaces = os.networkInterfaces();
  for (const list of Object.values(ifaces)) {
    if (!list) continue;
    for (const i of list) {
      if (i.family !== 'IPv4' || i.internal) continue;
      const parts = i.address.split('.').map((n) => Number(n));
      if (parts.length !== 4 || parts.some((n) => !Number.isInteger(n))) continue;
      const prefix = `${parts[0]}.${parts[1]}.${parts[2]}.`;
      for (let h = 1; h <= 254; h++) {
        const ip = prefix + h;
        if (ip === i.address) continue;
        out.add(ip);
      }
    }
  }
  return Array.from(out);
}
