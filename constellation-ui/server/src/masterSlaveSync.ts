/*
 * masterSlaveSync.ts — bridge-to-bridge outside-air sync.
 *
 * Background
 * ----------
 * Legacy AS2 multi-building setups had a "master" panel periodically
 * UDP-broadcast its outside_temp / outside_humid so "slave" panels in
 * neighbouring storages could substitute the value when they had no
 * outside sensor of their own. There was no acknowledgement; if a
 * broadcast was missed the slave silently kept stale data until the
 * next one (or never received any if a switch dropped multicast).
 *
 * Constellation rewrite (May 2026):
 *   - Confirmed delivery via HTTP POST (master ⇒ each slave).
 *   - Master keeps an explicit slave list (`MasterSlaveSettings.slaveIps`).
 *   - Slave keeps a source-IP allowlist (`allowedMasters`) and a
 *     selected master (`masterIp`); only pushes from the selected
 *     master are accepted.
 *   - Bidirectional visibility: master remembers per-slave last-success
 *     timestamp, slave remembers last-master-push timestamp. Both
 *     surface through `GET /iot/master-slave/status`.
 *   - Firmware-side override is gated on `mode === Slave` AND a fresh
 *     RemoteOutsideAir cache, so leaving slave mode (or letting the
 *     cache expire) cleanly falls back to the local sensor.
 *
 * Time sync (legacy AS2 also relayed clock) is NOT implemented yet —
 * deferred until the controller LP grows a real RTC.
 */
import { EventEmitter } from 'node:events';
import * as os from 'node:os';
import * as fs from 'node:fs';
import * as path from 'node:path';
import type { NovaDataStore } from './novaDataStore.js';
import type { NovaSerialBridge } from './novaSerialBridge.js';
import {
  MasterSlaveSettings,
  RemoteOutsideAir,
  SettingsUpdate,
} from '../../../generated/ts/agristar/settings.js';

const POLL_INTERVAL_MS  = 5_000;   /* legacy AS2 cadence */
const PUSH_TIMEOUT_MS   = 3_000;
const TTL_SECS_DEFAULT  = 30;
const INT16_MIN         = -32_768;

/* Auto-heal: once a slave fails this many consecutive pushes we run
 * the discovery sweep and try to find it under a new IP using its
 * cached panel name. 3 ticks = ~15 s of dead-air before we react —
 * tight enough to recover within the 10-min no-broadcast watchdog,
 * loose enough that a single dropped packet doesn't trip rebinding. */
const HEAL_FAILURE_THRESHOLD = 3;

/* Bridge HTTP port — slaves run on the same port as masters since
 * every panel has the same bridge.service. The "9001" literal is
 * ALSO baked into apiRoutes / index.ts; if that ever moves, this
 * fallback should follow. */
const PEER_HTTP_PORT = 9001;

/* On-disk peer-name cache. Lives next to the other bridge state so
 * restarts don't lose the (ip → panelName) map that auto-heal needs.
 * Falls back to /tmp on read-only deploys. */
const PEER_CACHE_PATHS = [
  process.env.MS_PEER_CACHE,
  '/var/lib/agristar/master-slave-peers.json',
  path.join(os.homedir() || '/tmp', '.agristar-master-slave-peers.json'),
  '/tmp/agristar-master-slave-peers.json',
].filter(Boolean) as string[];

export interface SlavePushBody {
  /** Tenths of degree °C (orbit native HR encoding). INT16_MIN = invalid. */
  tempX10: number;
  /** Tenths of percent RH. INT16_MIN = invalid. */
  humidX10: number;
  /** Freshness window the slave's firmware applies to its cache. */
  ttlSecs?: number;
  /** Master's own LAN IP — slave validates against its allowlist
   *  (defence-in-depth; req.ip is the primary check). */
  masterIp: string;
}

export interface SlavePeerStatus {
  ip: string;
  /** Cached panel name (from last successful contact / discovery
   *  sweep). Used by auto-heal to relocate the peer after DHCP
   *  reassigns it a new IP. Empty string until first probe. */
  panelName: string;
  lastAttemptAt: number | null;   /* epoch ms */
  lastSuccessAt: number | null;
  lastError:     string | null;
  lastTempX10:   number;
  lastHumidX10:  number;
  /** Consecutive failed pushes — drives auto-heal. */
  consecutiveFailures: number;
  /** If auto-heal moved this peer to a new IP, the previous one. */
  healedFromIp:  string | null;
}

export interface MasterSlaveStatus {
  mode: 'standalone' | 'master' | 'slave';
  /** This panel's name (from `PanelName` var). Surfaced so a peer
   *  master's discovery sweep can show a friendly label next to the IP. */
  panelName: string;
  /** Persistent UUID — survives operator-driven panel renames. The
   *  auto-heal cache keys on this so a relocated panel can be
   *  matched back to its slot even if the name was changed in the
   *  meantime. */
  novaId: string;
  masterIp: string;
  slaveIps: string[];
  allowedMasters: string[];
  /* Master view: per-slave delivery state. */
  slavePeers: SlavePeerStatus[];
  /* Slave view: when did our selected master last reach us? */
  lastMasterSeenAt: number | null;
  lastMasterTempX10: number;
  lastMasterHumidX10: number;
}

export interface DiscoveredPeer {
  ip: string;
  panelName: string;
  novaId:    string;
  mode: 'standalone' | 'master' | 'slave';
}

/* MasterSlaveSettings is broadcast on envelope tag 66 every 5 s by
 * `Nova_Firmware/lp_am2434/main.c::bridge_uart_task`. NovaDataStore
 * caches every raw inner-message payload so we just decode on demand
 * (the bridge has no MasterSlaveSettings struct field of its own). */
const MASTER_SLAVE_ENVELOPE_TAG = 66;

function decodeMasterSlave(store: NovaDataStore): MasterSlaveSettings | null {
  const buf = store.getRawMessage(MASTER_SLAVE_ENVELOPE_TAG);
  if (!buf || buf.length === 0) return null;
  try {
    return MasterSlaveSettings.decode(buf);
  } catch (err) {
    console.warn('[MS-Sync] MasterSlaveSettings decode failed:', err);
    return null;
  }
}

function clamp16(n: number): number {
  if (!Number.isFinite(n)) return INT16_MIN;
  if (n >  32_767) return  32_767;
  if (n < -32_768) return -32_768;
  return Math.round(n);
}

/* Convert master's `SystemStatus.outsideTemp` (in user units, per
 * BasicSetup.tempType) to tenths-of-°C for transmission. The slave's
 * firmware writes the value straight into `sample.sensorHr[]` which
 * is always tenths-of-°C — keeping the wire in canonical units means
 * no conversion happens on the slave side. */
function toCx10(displayValue: number, tempType: number): number {
  if (!Number.isFinite(displayValue) || displayValue === 0) return INT16_MIN;
  const celsius = tempType === 0 /* Fahrenheit */
    ? (displayValue - 32) / 1.8
    : displayValue;
  return clamp16(celsius * 10);
}

/* %RH on the wire is tenths-of-percent. SystemStatus.outsideHumid
 * is plain percent; multiply. Treat exactly 0 as "no reading" because
 * proto3 zero-suppression collapses an unset humid value to 0 and
 * "0% outside humidity" is not a real-world value anyone needs to
 * propagate to a peer. */
function humidToX10(displayValue: number): number {
  if (!Number.isFinite(displayValue) || displayValue === 0) return INT16_MIN;
  return clamp16(displayValue * 10);
}

export class MasterSlaveSync extends EventEmitter {
  private timer: NodeJS.Timeout | null = null;
  private slavePeers = new Map<string, SlavePeerStatus>();
  private lastMasterSeenAt: number | null = null;
  private lastMasterTempX10  = INT16_MIN;
  private lastMasterHumidX10 = INT16_MIN;

  /* (ip → {novaId, panelName}) cache used by auto-heal to find a
   * peer after DHCP changes its IP. Keyed by IP for fast lookup,
   * but heal MATCHES on novaId because UUIDs survive operator
   * renames while panel names don't. panelName is kept alongside
   * for UI display. Persisted so a bridge restart doesn't lose the
   * mapping. Updated on every successful push, accepted receive,
   * and discovery sweep. */
  private peerCache = new Map<string, { novaId: string; panelName: string }>();
  private peerCacheFile: string | null = null;
  private peerCacheDirty = false;

  /* Single-flight guard so two ticks can't both kick a heal at once. */
  private healInFlight = new Set<string>();

  constructor(
    private store:  NovaDataStore,
    private bridge: NovaSerialBridge,
    private getPanelName: () => string = () => 'Agristar Panel',
    private getNovaId:    () => string = () => '',
  ) {
    super();
    this.loadPeerCache();
  }

  start(): void {
    if (this.timer) return;
    this.timer = setInterval(() => { void this.tick(); }, POLL_INTERVAL_MS);
    /* Tick immediately so the first push doesn't wait 5 s. */
    setImmediate(() => { void this.tick(); });
    console.log('[MS-Sync] started (poll every 5 s)');
  }

  stop(): void {
    if (this.timer) {
      clearInterval(this.timer);
      this.timer = null;
    }
  }

  /** Master timer: read settings + system status, push to each slave. */
  private async tick(): Promise<void> {
    const ms = decodeMasterSlave(this.store);
    if (!ms || ms.mode !== 1 /* Master */) {
      /* Drop stale per-slave state when the panel leaves master mode
       * so a re-enable starts clean. */
      if (this.slavePeers.size > 0) this.slavePeers.clear();
      return;
    }
    const slaveIps = Array.from(new Set((ms.slaveIps ?? []).filter(Boolean)));
    if (slaveIps.length === 0) return;

    /* Reap slaves that fell out of the list. */
    for (const ip of [...this.slavePeers.keys()]) {
      if (!slaveIps.includes(ip)) this.slavePeers.delete(ip);
    }

    const status = this.store.systemStatus;
    const basic  = this.store.basicSetup;
    if (!status || !basic) {
      /* Pre-handshake; nothing yet to forward. Don't even POST — the
       * wire would carry INT16_MIN sentinels and waste a roundtrip. */
      return;
    }

    const tempType = basic.tempType ?? 1;
    const body: SlavePushBody = {
      tempX10:  toCx10(status.outsideTemp,  tempType),
      humidX10: humidToX10(status.outsideHumid),
      ttlSecs:  TTL_SECS_DEFAULT,
      masterIp: ms.masterIp || '',
    };

    /* Fire all pushes in parallel — the slaves are independent. */
    await Promise.allSettled(slaveIps.map((ip) => this.pushToSlave(ip, body)));
  }

  private async pushToSlave(ip: string, body: SlavePushBody): Promise<void> {
    const peer = this.peerFor(ip);
    peer.lastAttemptAt = Date.now();
    const url = `http://${ip}:${PEER_HTTP_PORT}/iot/master-slave/push`;
    const ctrl = new AbortController();
    const t = setTimeout(() => ctrl.abort(), PUSH_TIMEOUT_MS);
    try {
      const res = await fetch(url, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body),
        signal: ctrl.signal,
      });
      if (!res.ok) {
        peer.lastError = `HTTP ${res.status}`;
        peer.consecutiveFailures += 1;
        this.maybeHealSlave(ip, body);
        return;
      }
      peer.lastSuccessAt = Date.now();
      peer.lastError     = null;
      peer.lastTempX10   = body.tempX10;
      peer.lastHumidX10  = body.humidX10;
      peer.consecutiveFailures = 0;
      /* Refresh name cache opportunistically on every success. The
       * first successful push immediately learns the peer's name so
       * a later DHCP move can be healed. */
      void this.refreshPeerName(ip);
    } catch (err: any) {
      peer.lastError = err?.message ?? String(err);
      peer.consecutiveFailures += 1;
      this.maybeHealSlave(ip, body);
    } finally {
      clearTimeout(t);
    }
  }

  private peerFor(ip: string): SlavePeerStatus {
    let p = this.slavePeers.get(ip);
    if (!p) {
      p = {
        ip,
        panelName:           this.peerCache.get(ip)?.panelName ?? '',
        lastAttemptAt:       null,
        lastSuccessAt:       null,
        lastError:           null,
        lastTempX10:         INT16_MIN,
        lastHumidX10:        INT16_MIN,
        consecutiveFailures: 0,
        healedFromIp:        null,
      };
      this.slavePeers.set(ip, p);
    }
    return p;
  }

  /** Slave-side: invoked by apiRoutes /iot/master-slave/push handler.
   *  Returns null on success, or an error string on validation failure. */
  async receivePush(sourceIp: string, body: SlavePushBody): Promise<string | null> {
    const ms = decodeMasterSlave(this.store);
    if (!ms || ms.mode !== 2 /* Slave */) {
      return 'panel_not_in_slave_mode';
    }
    if (!body || typeof body.tempX10 !== 'number' || typeof body.humidX10 !== 'number') {
      return 'malformed_body';
    }

    const allow = new Set((ms.allowedMasters ?? []).filter(Boolean));
    const allowMatch    = allow.size === 0 || allow.has(sourceIp);
    const selectedMatch = !ms.masterIp || ms.masterIp === sourceIp;

    /* Auto-heal path: a push arrived from an unknown IP. If our cache
     * remembers what panel name belongs to the IP we're expecting,
     * and the new IP reports the same panel name, accept this as a
     * DHCP reassignment, rewrite our settings, and continue. Same
     * trust root (operator-curated names), zero touch. */
    if (!allowMatch || !selectedMatch) {
      const healed = await this.tryHealMaster(ms, sourceIp);
      if (!healed) {
        if (!allowMatch)    return `source_ip_not_in_allowlist (${sourceIp})`;
        if (!selectedMatch) return `not_selected_master (selected=${ms.masterIp}, source=${sourceIp})`;
      }
    }

    /* Forward to firmware via SettingsUpdate.remoteOutside (field 41). */
    const inner = SettingsUpdate.encode({
      remoteOutside: {
        outsideTempX10:  clamp16(body.tempX10),
        outsideHumidX10: clamp16(body.humidX10),
        ttlSecs:         body.ttlSecs && body.ttlSecs > 0
                            ? Math.min(body.ttlSecs, 300)
                            : TTL_SECS_DEFAULT,
        masterIp:        body.masterIp ?? sourceIp,
      },
    } as SettingsUpdate).finish();

    try {
      await this.bridge.sendSettingsUpdate(Buffer.from(inner));
    } catch (err: any) {
      return `firmware_send_failed: ${err?.message ?? err}`;
    }

    this.lastMasterSeenAt    = Date.now();
    this.lastMasterTempX10   = body.tempX10;
    this.lastMasterHumidX10  = body.humidX10;
    /* Learn this master's name for the next DHCP move. */
    void this.refreshPeerName(sourceIp);
    return null;
  }

  // ── Auto-heal helpers (DHCP-resilient peer rebinding) ──────────────
  /* The bridge keeps a (ip → panelName) cache so when DHCP moves a
   * peer to a new IP we can find it again by the operator-curated
   * panel name. The cache is opportunistic: every successful push,
   * accepted receive, or discovery sweep refreshes it. The
   * authoritative slave / master IP lists still live in firmware
   * settings — auto-heal just rewrites them via SettingsUpdate(25)
   * when a name match is found at a new address. */

  /** Probe `ip` for its identity and update the cache if found.
   *  novaId is the heal key; panelName is kept for display. */
  private async refreshPeerName(ip: string): Promise<void> {
    if (!ip) return;
    const peer = await probePeer(ip);
    if (!peer || !peer.novaId) return;
    const prior = this.peerCache.get(ip);
    if (!prior || prior.novaId !== peer.novaId || prior.panelName !== peer.panelName) {
      this.peerCache.set(ip, { novaId: peer.novaId, panelName: peer.panelName });
      this.peerCacheDirty = true;
      this.savePeerCache();
    }
    /* Reflect into the live SlavePeerStatus (master view) so
     * /master-slave/status surfaces the friendly label. */
    const live = this.slavePeers.get(ip);
    if (live) live.panelName = peer.panelName;
  }

  /** Master-side heal: a slave at `oldIp` keeps failing. Look up its
   *  cached name, sweep the LAN, and rewrite slaveIps[] if a peer
   *  with the same name now answers at a different IP. */
  private maybeHealSlave(oldIp: string, body: SlavePushBody): void {
    const peer = this.slavePeers.get(oldIp);
    if (!peer || peer.consecutiveFailures < HEAL_FAILURE_THRESHOLD) return;
    if (this.healInFlight.has(oldIp)) return;
    this.healInFlight.add(oldIp);
    void (async () => {
      try {
        await this.healSlave(oldIp, body);
      } catch (err) {
        console.warn('[MS-Sync] heal failed for', oldIp, err);
      } finally {
        this.healInFlight.delete(oldIp);
      }
    })();
  }

  private async healSlave(oldIp: string, body: SlavePushBody): Promise<void> {
    const cached = this.peerCache.get(oldIp);
    if (!cached || !cached.novaId) {
      console.log(`[MS-Sync] heal skipped for ${oldIp}: no cached novaId`);
      return;
    }
    const ms = decodeMasterSlave(this.store);
    if (!ms || ms.mode !== 1) return;

    const candidates = await this.discoverSlaves();
    /* Match on novaId — the panel may have been renamed since we
     * last saw it. */
    const match = candidates.find(
      (c) => c.novaId === cached.novaId && c.ip !== oldIp,
    );
    if (!match) {
      console.log(`[MS-Sync] heal: no peer with novaId ${cached.novaId} found at a new IP (was ${oldIp}, name="${cached.panelName}")`);
      return;
    }

    const newIps = (ms.slaveIps ?? []).map((ip) => (ip === oldIp ? match.ip : ip));
    const dedup = Array.from(new Set(newIps.filter(Boolean)));
    await this.writeMasterSlaveSettings({ ...ms, slaveIps: dedup });

    /* Repoint live tracking. */
    const oldPeer = this.slavePeers.get(oldIp);
    this.slavePeers.delete(oldIp);
    const newPeer = this.peerFor(match.ip);
    newPeer.panelName    = match.panelName;
    newPeer.healedFromIp = oldIp;
    newPeer.consecutiveFailures = 0;
    if (oldPeer) {
      newPeer.lastSuccessAt = oldPeer.lastSuccessAt;
      newPeer.lastTempX10   = oldPeer.lastTempX10;
      newPeer.lastHumidX10  = oldPeer.lastHumidX10;
    }
    this.peerCache.delete(oldIp);
    this.peerCache.set(match.ip, { novaId: match.novaId, panelName: match.panelName });
    this.peerCacheDirty = true;
    this.savePeerCache();
    console.log(`[MS-Sync] healed slave "${match.panelName}" (${cached.novaId}): ${oldIp} → ${match.ip}`);

    /* Immediate retry on the new IP so the operator sees recovery
     * instead of waiting for the next 5 s tick. */
    void this.pushToSlave(match.ip, body);
  }

  /** Slave-side heal: an unknown source IP just pushed. If its
   *  panelName matches the cached name for our currently selected
   *  master (or any allowed master), rewrite settings and return
   *  true so the push is accepted. */
  private async tryHealMaster(
    ms: MasterSlaveSettings,
    sourceIp: string,
  ): Promise<boolean> {
    const probe = await probePeer(sourceIp);
    if (!probe || !probe.novaId) return false;

    const candidates = [
      ms.masterIp,
      ...((ms.allowedMasters ?? []).filter(Boolean)),
    ].filter((ip): ip is string => Boolean(ip) && ip !== sourceIp);

    /* Match on novaId — operator may have renamed the master since
     * we last cached its identity. */
    let matchedOldIp: string | null = null;
    for (const oldIp of candidates) {
      const cached = this.peerCache.get(oldIp);
      if (cached && cached.novaId === probe.novaId) {
        matchedOldIp = oldIp;
        break;
      }
    }
    if (!matchedOldIp) return false;

    const next: Partial<MasterSlaveSettings> = {};
    if (ms.masterIp === matchedOldIp) next.masterIp = sourceIp;
    const allowed = (ms.allowedMasters ?? []).map(
      (ip) => (ip === matchedOldIp ? sourceIp : ip),
    );
    next.allowedMasters = Array.from(new Set(allowed.filter(Boolean)));

    await this.writeMasterSlaveSettings({ ...ms, ...next });

    this.peerCache.delete(matchedOldIp);
    this.peerCache.set(sourceIp, { novaId: probe.novaId, panelName: probe.panelName });
    this.peerCacheDirty = true;
    this.savePeerCache();
    console.log(`[MS-Sync] healed master "${probe.panelName}" (${probe.novaId}): ${matchedOldIp} → ${sourceIp}`);
    return true;
  }

  /** Encode + send a MasterSlaveSettings update via the standard
   *  SettingsUpdate(field=25) path. Same wire format the UI uses;
   *  firmware persists it to OSPI. */
  private async writeMasterSlaveSettings(ms: MasterSlaveSettings): Promise<void> {
    const inner = SettingsUpdate.encode({
      masterSlave: ms,
    } as SettingsUpdate).finish();
    await this.bridge.sendSettingsUpdate(Buffer.from(inner));
  }

  // ── Peer-id cache disk persistence ──────────────────────────────────
  /** v1 file format (`Record<string, string>`) was {ip: panelName}. v2 is
   *  {ip: {novaId, panelName}}. We accept either on load and write v2. */
  private loadPeerCache(): void {
    for (const candidate of PEER_CACHE_PATHS) {
      try {
        if (!fs.existsSync(candidate)) continue;
        const raw = fs.readFileSync(candidate, 'utf8');
        const j = JSON.parse(raw) as Record<string, unknown>;
        for (const [ip, val] of Object.entries(j)) {
          if (typeof val === 'string' && val) {
            /* v1 entry — keep panelName, novaId blank until next probe
             * refreshes it. Heal won't fire on this entry until then. */
            this.peerCache.set(ip, { novaId: '', panelName: val });
          } else if (val && typeof val === 'object') {
            const o = val as { novaId?: string; panelName?: string };
            this.peerCache.set(ip, {
              novaId:    typeof o.novaId    === 'string' ? o.novaId    : '',
              panelName: typeof o.panelName === 'string' ? o.panelName : '',
            });
          }
        }
        this.peerCacheFile = candidate;
        return;
      } catch {
        /* keep trying */
      }
    }
    /* No existing cache — pick the first writable path for future saves. */
    for (const candidate of PEER_CACHE_PATHS) {
      try {
        const dir = path.dirname(candidate);
        fs.mkdirSync(dir, { recursive: true });
        fs.writeFileSync(candidate, JSON.stringify({}), 'utf8');
        this.peerCacheFile = candidate;
        return;
      } catch {
        /* keep trying */
      }
    }
  }

  private savePeerCache(): void {
    if (!this.peerCacheDirty || !this.peerCacheFile) return;
    try {
      const out: Record<string, { novaId: string; panelName: string }> = {};
      for (const [ip, val] of this.peerCache.entries()) out[ip] = val;
      fs.writeFileSync(this.peerCacheFile, JSON.stringify(out, null, 2), 'utf8');
      this.peerCacheDirty = false;
    } catch (err) {
      console.warn('[MS-Sync] failed to persist peer cache:', err);
    }
  }

  /** GET /iot/master-slave/status — operator-facing snapshot. */
  getStatus(): MasterSlaveStatus {
    const ms = decodeMasterSlave(this.store);
    const mode = ms?.mode === 1 ? 'master'
               : ms?.mode === 2 ? 'slave'
               : 'standalone';
    return {
      mode,
      panelName:          this.getPanelName(),
      novaId:             this.getNovaId(),
      masterIp:           ms?.masterIp ?? '',
      slaveIps:           ms?.slaveIps ?? [],
      allowedMasters:     ms?.allowedMasters ?? [],
      slavePeers:         Array.from(this.slavePeers.values()),
      lastMasterSeenAt:   this.lastMasterSeenAt,
      lastMasterTempX10:  this.lastMasterTempX10,
      lastMasterHumidX10: this.lastMasterHumidX10,
    };
  }

  /**
   * GET /iot/master-slave/discover — sweep this panel's local /24 and
   * return every peer whose `/iot/master-slave/status` reports
   * `mode === 'slave'`. The UI's master view uses this to auto-fill
   * the slaveIps list instead of forcing the operator to type IPs by
   * hand.
   *
   * Implementation notes:
   *   - We probe ALL 254 host addresses on the local /24 in parallel,
   *     each with a tight 800 ms timeout. A full sweep on a quiet
   *     LAN finishes in ~1 s; a noisy/large LAN never blocks longer
   *     than the per-probe timeout.
   *   - We filter by `mode === 'slave'` (not just "responded") so the
   *     master only ever sees panels intentionally configured to
   *     accept its push. Standalone/master peers appear as candidates
   *     only if explicitly added.
   *   - Returns this panel too if it happens to be a slave to itself
   *     (loopback testing); that's the operator's choice to remove.
   *   - No mDNS / broadcast — keeps deployment dependency-free and
   *     works on networks that filter multicast (every customer LAN
   *     we've ever seen).
   */
  async discoverSlaves(): Promise<DiscoveredPeer[]> {
    const candidates = enumerateLocalSubnetHosts();
    const results = await Promise.all(
      candidates.map((ip) => probePeer(ip)),
    );
    const peers = results
      .filter((r): r is DiscoveredPeer => r !== null && r.mode === 'slave');
    /* Opportunistic cache refresh — every responding peer (slave OR
     * master) is a candidate for future heal lookups. */
    let dirty = false;
    for (const r of results) {
      if (!r || !r.novaId) continue;
      const prior = this.peerCache.get(r.ip);
      if (!prior || prior.novaId !== r.novaId || prior.panelName !== r.panelName) {
        this.peerCache.set(r.ip, { novaId: r.novaId, panelName: r.panelName });
        dirty = true;
      }
    }
    if (dirty) {
      this.peerCacheDirty = true;
      this.savePeerCache();
    }
    return peers;
  }
}

/** Enumerate every /24 host address for each non-loopback IPv4 iface.
 *  Skips the panel's own address(es) so probePeer doesn't have to
 *  short-circuit. */
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

async function probePeer(ip: string): Promise<DiscoveredPeer | null> {
  const ctrl = new AbortController();
  const t = setTimeout(() => ctrl.abort(), 800);
  try {
    const res = await fetch(`http://${ip}:${PEER_HTTP_PORT}/iot/master-slave/status`, {
      signal: ctrl.signal,
    });
    if (!res.ok) return null;
    const j = await res.json() as Partial<MasterSlaveStatus>;
    if (!j || (j.mode !== 'slave' && j.mode !== 'master' && j.mode !== 'standalone')) return null;
    return {
      ip,
      panelName: typeof j.panelName === 'string' ? j.panelName : '',
      novaId:    typeof j.novaId    === 'string' ? j.novaId    : '',
      mode:      j.mode,
    };
  } catch {
    return null;
  } finally {
    clearTimeout(t);
  }
}
