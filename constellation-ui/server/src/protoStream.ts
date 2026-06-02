/**
 * protoStream.ts — Phase 0.3+ of the proto-direct redesign.
 *
 * Exposes a single binary WebSocket at /proto/stream that ships raw nanopb
 * inner-message bytes to the SvelteKit UI. The UI decodes them with the
 * same ts-proto types the firmware/bridge use — there is no CSV or
 * legacy-CGI translation.
 *
 * Wire format (server → client):
 *   binary frames of [u16 LE msgTag][u32 LE payloadLen][payloadLen bytes proto]
 *
 * Wire format (client → server):
 *   text frames of JSON control messages:
 *     { "action": "subscribe",   "tags": [10,11,200] }
 *     { "action": "subscribe",   "all": true }
 *     { "action": "unsubscribe", "tags": [10] }
 *     { "action": "unsubscribe", "all": true }
 *     { "action": "ping" }                       → server replies pong
 *
 * Subscriptions are additive. On every successful subscribe the server
 * immediately replays the latest cached frame for each newly-subscribed
 * tag (initial snapshot), so a freshly-loaded page has data without
 * having to wait for the next firmware push.
 *
 * Source of truth for cached frames:
 *   - tags 10–129 (firmware messages): NovaDataStore.getRawMessages()
 *     (raw nanopb bytes captured in handleMessage()).
 *   - tags 200–209 (bridge-emitted): synthesized here from in-memory state
 *     and encoded with ts-proto. First occupant: VfdStatus = 200.
 *
 * NOT a replacement for /iot/ws yet — runs alongside it during the
 * page-by-page UI migration. /iot/ws stays until Phase 5 deletes it.
 */

import { WebSocketServer, WebSocket, type RawData } from 'ws';
import type { Server as HttpServer } from 'http';
import type { IncomingMessage } from 'http';
import * as os from 'os';
import type { NovaDataStore } from './novaDataStore.js';
import type { VFDClient } from './vfdClient.js';
import { VfdStatus, VfdDrive } from '../../../generated/ts/agristar/vfd.js';
import { NetworkConfig } from '../../../generated/ts/agristar/system.js';
import { loadConfig } from './simConfig.js';

// ─── Bridge-emitted tag range. ────────────────────────────────────────────
// Firmware envelope tags 10–129 normally come straight from NovaDataStore.
// Tag 19 (NetworkConfig) is *temporarily* bridge-synthesized while the
// firmware encoder lands — the simulator persists user TCP/IP intent in
// `simConfig` (`tcpip.json`) and reports the host's actual address+MAC for
// the read-only fields. When firmware starts emitting envelope tag 19 the
// firmware-emitted frame takes precedence (NovaDataStore caches it; the
// `synthesize()` path is only consulted for tags in `BRIDGE_EMITTED_TAGS`)
// and this entry should be removed from the set.
export const TAG_VFD_STATUS     = 200;
export const TAG_NETWORK_CONFIG = 19;

const BRIDGE_EMITTED_TAGS: ReadonlySet<number> = new Set([
  TAG_VFD_STATUS,
  TAG_NETWORK_CONFIG,
]);

// Maximum payload we will ever forward in a single frame. Defensive.
const MAX_FRAME_PAYLOAD = 256 * 1024;

interface ProtoClient {
  ws: WebSocket;
  id: string;
  /** Tag IDs this client wants. If `all` is true, this set is ignored. */
  tags: Set<number>;
  all: boolean;
  alive: boolean;
}

export interface ProtoStreamOptions {
  store: NovaDataStore;
  vfdClient?: VFDClient | null;
  /** Path to mount on. Defaults to "/proto/stream". */
  path?: string;
}

export class ProtoStream {
  private wss: WebSocketServer;
  private clients = new Map<string, ProtoClient>();
  private store: NovaDataStore;
  private vfdClient: VFDClient | null;
  private clientCounter = 0;
  private heartbeatTimer: NodeJS.Timeout | null = null;
  readonly path: string;

  constructor(_server: HttpServer, opts: ProtoStreamOptions) {
    this.store = opts.store;
    this.vfdClient = opts.vfdClient ?? null;
    this.path = opts.path ?? '/proto/stream';

    // noServer mode: index.ts owns the HTTP `upgrade` event and dispatches
    // to us via handleUpgrade(). See wsManager.ts for the same pattern.
    this.wss = new WebSocketServer({ noServer: true });

    this.wss.on('connection', (ws, req) => this.handleConnection(ws, req));
    this.wss.on('error', (err) => console.error('[ProtoStream] Server error:', err));

    // Bridge firmware → wire push
    this.store.on('update', (msgId: number) => {
      const buf = this.store.getRawMessage(msgId);
      if (buf) this.broadcastFrame(msgId, buf);
    });

    // Bridge VFD updates → tag 200 push (bridge-emitted)
    if (this.vfdClient) {
      this.vfdClient.onUpdate(() => {
        const bytes = this.encodeVfdStatus();
        if (bytes) this.broadcastFrame(TAG_VFD_STATUS, bytes);
      });
    }

    this.heartbeatTimer = setInterval(() => this.heartbeat(), 30_000);

    console.log(`[ProtoStream] Binary WS attached at ${this.path} (noServer mode)`);
  }

  /** Called by the HTTP-server `upgrade` dispatcher in index.ts. */
  handleUpgrade(req: IncomingMessage, socket: any, head: Buffer): void {
    this.wss.handleUpgrade(req, socket, head, (ws) => {
      this.wss.emit('connection', ws, req);
    });
  }

  // ─── Connection lifecycle ──────────────────────────────────────────────

  private handleConnection(ws: WebSocket, req: IncomingMessage): void {
    const id = `proto-${++this.clientCounter}`;
    const client: ProtoClient = { ws, id, tags: new Set(), all: false, alive: true };
    this.clients.set(id, client);
    console.log(`[ProtoStream] Client ${id} connected from ${req.socket.remoteAddress}`);

    ws.on('message', (raw: RawData) => this.handleMessage(client, raw));
    ws.on('pong', () => { client.alive = true; });
    ws.on('close', () => {
      this.clients.delete(id);
      console.log(`[ProtoStream] Client ${id} disconnected`);
    });
    ws.on('error', (err) => {
      console.error(`[ProtoStream] Client ${id} error:`, err.message);
    });
  }

  private heartbeat(): void {
    for (const [id, c] of this.clients) {
      if (!c.alive) {
        try { c.ws.terminate(); } catch {}
        this.clients.delete(id);
        continue;
      }
      c.alive = false;
      try { c.ws.ping(); } catch {}
    }
  }

  // ─── Control plane (JSON) ──────────────────────────────────────────────

  private handleMessage(client: ProtoClient, raw: RawData): void {
    let msg: { action?: string; tags?: number[]; all?: boolean };
    try {
      msg = JSON.parse(raw.toString());
    } catch {
      this.sendJson(client, { error: 'invalid_json' });
      return;
    }

    switch (msg.action) {
      case 'subscribe': {
        if (msg.all === true) {
          client.all = true;
          this.replayAll(client);
        } else if (Array.isArray(msg.tags)) {
          for (const t of msg.tags) {
            if (typeof t === 'number' && t > 0) {
              client.tags.add(t);
              this.replayTag(client, t);
            }
          }
        }
        this.sendJson(client, { action: 'subscribed', all: client.all, tags: [...client.tags] });
        break;
      }
      case 'unsubscribe': {
        if (msg.all === true) {
          client.all = false;
          client.tags.clear();
        } else if (Array.isArray(msg.tags)) {
          for (const t of msg.tags) client.tags.delete(t);
        }
        this.sendJson(client, { action: 'unsubscribed', all: client.all, tags: [...client.tags] });
        break;
      }
      case 'ping':
        this.sendJson(client, { action: 'pong', ts: Date.now() });
        break;
      default:
        this.sendJson(client, { error: 'unknown_action', action: msg.action ?? null });
    }
  }

  // ─── Snapshot replay ───────────────────────────────────────────────────

  private replayAll(client: ProtoClient): void {
    for (const [tag, buf] of this.store.getRawMessages()) {
      this.sendFrame(client, tag, buf);
    }
    // Bridge-emitted tags must be synthesized fresh.
    for (const tag of BRIDGE_EMITTED_TAGS) {
      const bytes = this.synthesize(tag);
      if (bytes) this.sendFrame(client, tag, bytes);
    }
  }

  private replayTag(client: ProtoClient, tag: number): void {
    if (BRIDGE_EMITTED_TAGS.has(tag)) {
      const bytes = this.synthesize(tag);
      if (bytes) this.sendFrame(client, tag, bytes);
      return;
    }
    const buf = this.store.getRawMessage(tag);
    if (buf) this.sendFrame(client, tag, buf);
  }

  // ─── Bridge-emitted message synthesis ─────────────────────────────────

  private synthesize(tag: number): Buffer | null {
    if (tag === TAG_VFD_STATUS)     return this.encodeVfdStatus();
    if (tag === TAG_NETWORK_CONFIG) return this.encodeNetworkConfig();
    return null;
  }

  /**
   * Build a NetworkConfig frame for tag 19. Read-only fields
   * (`ip_addr`, `mac`) come from the host's first non-internal IPv4
   * NIC because in the QEMU sim the firmware does not own the network
   * stack (Linux WSL does). User-config fields (`ip_mode`, `http_port`,
   * `public_ip`, `dns`, `ip_mask`, `ip_gateway`) are read from
   * `simConfig:tcpip.json` — written by `POST /iot/tcpip`. Defaults
   * mirror the legacy bridge stub when no save has happened yet.
   *
   * Production note: when running on the Pi 5 / real Nova hardware
   * this method should be replaced by a passthrough of the firmware-
   * emitted tag 19 frame (drop `TAG_NETWORK_CONFIG` from
   * `BRIDGE_EMITTED_TAGS`).
   */
  private encodeNetworkConfig(): Buffer | null {
    const persisted = loadConfig<{
      ipMode?: number;
      ipAddr?: string;
      ipMask?: string;
      ipGateway?: string;
      httpPort?: number;
      publicIp?: string;
      dns?: string[];
    }>('tcpip') ?? {};

    const ifaces = os.networkInterfaces();
    const nic = Object.values(ifaces)
      .flat()
      .find((i) => i && i.family === 'IPv4' && !i.internal);

    // Loopback mode (kiosk-only, no LAN) is a deliberate persisted state —
    // the /iot/tcpip handler skips nmcli for 127.x and "localhost" so eth0
    // keeps whatever address it had. We must report 127.x back to the page
    // anyway, otherwise reloading would re-hydrate from the real NIC IP
    // and the user couldn't tell loopback mode "stuck".
    //
    // Otherwise prefer the live NIC address — it's the truth on the wire.
    // Fall back to persisted ipAddr only when NM hasn't brought the link
    // up yet (e.g. immediately after a reboot from a /iot/tcpip save).
    const persistedIsLoopback = !!persisted.ipAddr && (
      persisted.ipAddr === 'localhost' || persisted.ipAddr.startsWith('127.')
    );
    const cfg: NetworkConfig = {
      ipAddr:    persistedIsLoopback
                   ? persisted.ipAddr!
                   : (nic?.address ?? persisted.ipAddr ?? '127.0.0.1'),
      ipMask:    persisted.ipMask    ?? nic?.netmask ?? '255.255.255.0',
      ipGateway: persisted.ipGateway ?? '',
      ipMode:    persisted.ipMode    ?? 1,                   // 1=DHCP default
      httpPort:  persisted.httpPort  ?? Number(process.env.BRIDGE_PUBLIC_PORT ?? '81'),
      mac:       (nic?.mac ?? '').toUpperCase(),
      publicIp:  persisted.publicIp  ?? '',
      dns:       persisted.dns       ?? [],
    };
    const writer = NetworkConfig.encode(cfg);
    return Buffer.from(writer.finish());
  }

  /** Push a fresh NetworkConfig frame to all subscribers. Call after
   *  every successful `POST /iot/tcpip` save. */
  broadcastNetworkConfig(): void {
    const bytes = this.encodeNetworkConfig();
    if (bytes) this.broadcastFrame(TAG_NETWORK_CONFIG, bytes);
  }

  private encodeVfdStatus(): Buffer | null {
    if (!this.vfdClient) return null;
    const snaps = this.vfdClient.getDrives();
    const drives = snaps.map<VfdDrive>((s) => ({
      // Legacy alarm-only fields (1-10) — keep unchanged for back-compat
      address: s.unitId,
      label: s.label ?? '',
      connected: s.online ? 1 : 0,
      running: s.running ? 1 : 0,
      faultActive: s.faulted ? 1 : 0,
      faultCode: s.faultCode | 0,
      faultText: '',
      speedHz: s.outputFreqHz / 10,
      currentA: s.motorCurrentA / 10,
      busVoltage: s.dcBusVoltage,
      // Extended snapshot fields (11+) — full mirror of VFDDriveSnapshot.
      // The UI's level2/fans page consumes these directly via
      // protoStores.vfdStatus, replacing the 5 s `/vfd/fans` HTTP poll.
      manufacturer: s.manufacturer ?? 'generic',
      controlWord: s.controlWord | 0,
      speedRefPercent: s.speedRefPercent | 0,
      statusWord: s.statusWord | 0,
      actualSpeedPercent: s.actualSpeedPercent | 0,
      outputFreqHzX10: s.outputFreqHz | 0,
      freqRefHzX10: s.freqRefHz | 0,
      motorSpeedRpm: s.motorSpeedRpm | 0,
      motorCurrentAX100: s.motorCurrentA | 0,
      motorTorquePctX100: s.motorTorquePercent | 0,
      motorPowerKwX100: s.motorPowerkW | 0,
      outputVoltageX10: s.outputVoltage | 0,
      driveTempX10: s.driveTemp | 0,
      minFreqHzX10: s.minFreqHz | 0,
      maxFreqHzX10: s.maxFreqHz | 0,
      rampUpTimeX10: s.rampUpTime | 0,
      rampDownTimeX10: s.rampDownTime | 0,
      ratedCurrentAX100: s.ratedCurrentA | 0,
      ratedVoltage: s.ratedVoltage | 0,
      ratedFreqHzX10: s.ratedFreqHz | 0,
      ratedSpeedRpm: s.ratedSpeedRpm | 0,
      ratedPowerKwX100: s.ratedPowerkW | 0,
      atReference: s.atReference ? 1 : 0,
      direction: s.direction | 0,
    }));
    const anyFault = drives.some((d) => d.faultActive) ? 1 : 0;
    const writer = VfdStatus.encode({ drives, anyFault });
    return Buffer.from(writer.finish());
  }

  // ─── Outbound framing ──────────────────────────────────────────────────

  /** Pack a single frame: [u16 LE tag][u32 LE len][payload]. */
  private buildFrame(tag: number, payload: Uint8Array | Buffer): Buffer {
    const len = payload.length;
    if (len > MAX_FRAME_PAYLOAD) {
      throw new Error(`ProtoStream: frame for tag ${tag} too large (${len} > ${MAX_FRAME_PAYLOAD})`);
    }
    const out = Buffer.alloc(6 + len);
    out.writeUInt16LE(tag & 0xffff, 0);
    out.writeUInt32LE(len >>> 0, 2);
    if (Buffer.isBuffer(payload)) {
      payload.copy(out, 6);
    } else {
      Buffer.from(payload.buffer, payload.byteOffset, payload.byteLength).copy(out, 6);
    }
    return out;
  }

  private sendFrame(client: ProtoClient, tag: number, payload: Uint8Array | Buffer): void {
    if (client.ws.readyState !== WebSocket.OPEN) return;
    try {
      const frame = this.buildFrame(tag, payload);
      client.ws.send(frame, { binary: true });
    } catch (err) {
      console.error(`[ProtoStream] sendFrame failed for ${client.id} tag ${tag}:`, (err as Error).message);
    }
  }

  private broadcastFrame(tag: number, payload: Uint8Array | Buffer): void {
    let frame: Buffer | null = null;
    for (const client of this.clients.values()) {
      if (!(client.all || client.tags.has(tag))) continue;
      if (client.ws.readyState !== WebSocket.OPEN) continue;
      if (!frame) {
        try { frame = this.buildFrame(tag, payload); }
        catch (err) {
          console.error(`[ProtoStream] broadcast build failed tag ${tag}:`, (err as Error).message);
          return;
        }
      }
      try { client.ws.send(frame, { binary: true }); }
      catch (err) { console.error(`[ProtoStream] send failed for ${client.id}:`, (err as Error).message); }
    }
  }

  private sendJson(client: ProtoClient, obj: unknown): void {
    if (client.ws.readyState !== WebSocket.OPEN) return;
    try { client.ws.send(JSON.stringify(obj)); } catch {}
  }

  // ─── Shutdown ──────────────────────────────────────────────────────────

  close(): void {
    if (this.heartbeatTimer) {
      clearInterval(this.heartbeatTimer);
      this.heartbeatTimer = null;
    }
    for (const c of this.clients.values()) {
      try { c.ws.terminate(); } catch {}
    }
    this.clients.clear();
    try { this.wss.close(); } catch {}
  }

  /** Diagnostics for /health. */
  stats(): { clients: number; subscribers: { all: number; specific: number } } {
    let allCount = 0, specCount = 0;
    for (const c of this.clients.values()) {
      if (c.all) allCount++; else if (c.tags.size > 0) specCount++;
    }
    return { clients: this.clients.size, subscribers: { all: allCount, specific: specCount } };
  }
}
