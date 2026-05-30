/**
 * WebSocket connection manager.
 * Manages client connections, channel subscriptions, and broadcasts.
 *
 * Channel names (consumed by the UI's WsClient):
 *   - "frontmatter-data"  → main dashboard data
 *   - "tcpip-data"        → network node list for dropdown
 *   - "upgrade-data"      → firmware upgrade progress
 *   - "download-data"     → log download progress
 *
 * Server pushes data on these channels whenever the firmware sends an update,
 * or on a configurable interval. Retired channels (header-data, network-data,
 * sensor-data, vfd-data, equipment-data) — all UI consumers now derive
 * these payloads from typed proto stores.
 */

import { WebSocketServer, WebSocket, type RawData } from 'ws';
import type { Server as HttpServer } from 'http';
import type { IncomingMessage } from 'http';
import type { DataCache } from './dataCache.js';
import type { UpgradeManager, UpgradeStatus } from './upgradeManager.js';
import type { RemoteSystemsSync } from './remoteSystemsSync.js';

export interface WsClient {
  ws: WebSocket;
  /** Channels this client is subscribed to */
  channels: Set<string>;
  /** Client identifier for logging */
  id: string;
  /** Whether the client is alive (for heartbeat) */
  isAlive: boolean;
}

export interface ChannelMessage {
  channel: string;
  data: unknown;
  /** Timestamp of the data */
  ts: number;
}

export class WsManager {
  private wss: WebSocketServer;
  private clients = new Map<string, WsClient>();
  private clientCounter = 0;
  private heartbeatTimer: NodeJS.Timeout | null = null;
  private pushTimers = new Map<string, NodeJS.Timeout>();
  private dataCache: DataCache;
  private upgradeManager: UpgradeManager | null = null;
  private remoteSystemsSync: RemoteSystemsSync | null = null;
  // S9k cleanup (2026-04-20): vfdClient member removed — the only
  // consumer was the retired `vfd-data` channel.

  constructor(server: HttpServer, dataCache: DataCache) {
    this.dataCache = dataCache;

    // noServer mode: index.ts owns the HTTP `upgrade` event and dispatches
    // by path to the right WebSocketServer instance. Multiple WS apps on
    // the same HTTP server cannot use {server,path} together — the first
    // one to fire abortHandshake(400) on path mismatch kills the socket
    // before the second one's listener runs.
    this.wss = new WebSocketServer({ noServer: true });

    this.wss.on('connection', (ws, req) => this.handleConnection(ws, req));
    this.wss.on('error', (err) => console.error('[WsManager] Server error:', err));

    // Start heartbeat to detect dead connections
    this.heartbeatTimer = setInterval(() => this.heartbeat(), 30_000);

    // Start periodic push for dashboard channels.
    // NOTE: `frontmatter-data` and `header-data` are no longer pushed — the
    // SvelteKit UI now builds those payloads client-side from the typed-proto
    // stream (`/proto/stream` → `frontMatterComposite` / `headerComposite`).
    // S9k cleanup (2026-04-20): `sensor-data` periodic push retired —
    // no UI consumer remained (pile page reads $sensorList directly).
    this.startPeriodicPush('tcpip-data', 10_000);

    console.log('[WsManager] WebSocket server attached at /iot/ws (noServer mode)');
  }

  /** Path this server expects (matches against req.url). */
  readonly path = '/iot/ws';

  /** Called by the HTTP-server `upgrade` dispatcher in index.ts. */
  handleUpgrade(req: IncomingMessage, socket: any, head: Buffer): void {
    this.wss.handleUpgrade(req, socket, head, (ws) => {
      this.wss.emit('connection', ws, req);
    });
  }

  /**
   * Attach an UpgradeManager to receive real-time upgrade status events.
   * Listens for 'status' events and broadcasts to all 'upgrade-data' subscribers.
   */
  attachUpgradeManager(mgr: UpgradeManager): void {
    this.upgradeManager = mgr;
    mgr.on('status', (status: UpgradeStatus) => {
      this.broadcast('upgrade-data', status);
    });
    console.log('[WsManager] UpgradeManager attached for upgrade-data broadcasts');
  }

  /**
   * Attach the bridge-managed remote-systems list. Once set, the
   * `tcpip-data` channel emits a node list merged from this source
   * (UUID-keyed, DHCP-resilient) instead of the legacy
   * `P2NodeSetupData` CSV path.
   */
  attachRemoteSystemsSync(rs: RemoteSystemsSync): void {
    this.remoteSystemsSync = rs;
    /* Push immediately whenever the list mutates so the header
     * selector reflects add/remove/heal without waiting for the
     * 10 s periodic tick. */
    rs.on('change', () => this.broadcast('tcpip-data'));
    console.log('[WsManager] RemoteSystemsSync attached for tcpip-data merge');
  }

  private handleConnection(ws: WebSocket, req: IncomingMessage): void {
    const clientId = `ws-${++this.clientCounter}`;
    const client: WsClient = {
      ws,
      channels: new Set(),
      id: clientId,
      isAlive: true,
    };

    this.clients.set(clientId, client);
    console.log(`[WsManager] Client connected: ${clientId} from ${req.socket.remoteAddress}`);

    ws.on('message', (raw: RawData) => this.handleMessage(client, raw));
    ws.on('pong', () => { client.isAlive = true; });
    ws.on('close', () => {
      this.clients.delete(clientId);
      console.log(`[WsManager] Client disconnected: ${clientId}`);
    });
    ws.on('error', (err) => {
      console.error(`[WsManager] Client error (${clientId}):`, err.message);
    });
  }

  /**
   * Handle incoming messages from WebSocket clients.
   * Protocol:
   *   { "action": "subscribe",   "channel": "frontmatter-data" }
   *   { "action": "unsubscribe", "channel": "frontmatter-data" }
   *   { "action": "post",        "endpoint": "/iot/button", "body": {...} }
   */
  private handleMessage(client: WsClient, raw: RawData): void {
    try {
      const msg = JSON.parse(raw.toString());

      switch (msg.action) {
        case 'subscribe':
          if (msg.channel) {
            client.channels.add(msg.channel);
            console.log(`[WsManager] ${client.id} subscribed to ${msg.channel}`);
            // Send current data immediately upon subscribe
            this.sendChannelData(client, msg.channel);
          }
          break;

        case 'unsubscribe':
          if (msg.channel) {
            client.channels.delete(msg.channel);
          }
          break;

        case 'post':
          // Forward POST actions to the serial bridge (will be implemented)
          this.handlePostAction(client, msg);
          break;

        default:
          console.warn(`[WsManager] Unknown action from ${client.id}:`, msg.action);
      }
    } catch (err) {
      console.error(`[WsManager] Failed to parse message from ${client.id}:`, err);
    }
  }

  /**
   * Send current data for a specific channel to a single client.
   */
  private sendChannelData(client: WsClient, channel: string): void {
    const data = this.buildChannelData(channel);
    if (data === null) return;

    const message: ChannelMessage = {
      channel,
      data,
      ts: Date.now(),
    };

    this.send(client, message);
  }

  /**
   * Build channel-specific data from the cache.
   */
  private buildChannelData(channel: string): unknown {
    switch (channel) {
      case 'tcpip-data':
        return this.dataCache.buildTcpIpData(this.remoteSystemsSync?.list());

      case 'upgrade-data':
        if (this.upgradeManager) {
          return this.upgradeManager.getStatus();
        }
        return { UpgradeStatus: '', UpgradingSoftware: false, isEmpty: true };

      // S9k cleanup (2026-04-20): retired channels with no live UI subscribers.
      // Active channels: tcpip-data (GellertHeader), upgrade-data (level1/version),
      // download-data (history pages). Removed: header-data, network-data,
      // sensor-data, vfd-data, equipment-data — all UI consumers now derive
      // these payloads from the typed-proto store registry.

      case 'download-data':
        return { current: undefined, total: undefined };

      default:
        return null;
    }
  }

  /**
   * Broadcast data to all clients subscribed to a specific channel.
   * Called when the ARM sends an update that affects this channel.
   */
  broadcast(channel: string, data?: unknown): void {
    const payload = data ?? this.buildChannelData(channel);
    if (payload === null) return;

    const message: ChannelMessage = {
      channel,
      data: payload,
      ts: Date.now(),
    };

    const json = JSON.stringify(message);

    for (const client of this.clients.values()) {
      if (client.channels.has(channel) && client.ws.readyState === WebSocket.OPEN) {
        client.ws.send(json);
      }
    }
  }

  /**
   * Broadcast an update triggered by an ARM serial message.
   * Determines which channels are affected by the incoming tag and pushes updates.
   */
  broadcastArmUpdate(tag: string): void {
    // Map ARM message tags to affected WebSocket channels.
    // `frontmatter-data` and `header-data` are intentionally absent — the UI
    // derives those payloads locally from the typed-proto stream now.
    // S9k cleanup (2026-04-20): EquipStatus/SensorList/SensorData/AnalogAll
    // mappings removed — all targeted retired channels (equipment-data,
    // sensor-data) and produced no UI updates. tcpip-data is the only
    // surviving event-driven channel.
    const tagChannels: Record<string, string[]> = {
      'PanelName':      ['tcpip-data'],
    };

    const channels = tagChannels[tag];
    if (channels) {
      for (const ch of channels) {
        this.broadcast(ch);
      }
    }
  }

  /**
   * Handle POST actions forwarded from WebSocket clients.
   * These replace the HTTP POST endpoints (/iot/button, /iot/PostSave.jsp, etc.)
   */
  private handlePostAction(client: WsClient, msg: { endpoint?: string; body?: unknown }): void {
    // This will be wired to the serial bridge to send commands to the ARM
    console.log(`[WsManager] POST action from ${client.id}: ${msg.endpoint}`);

    // For now, acknowledge receipt
    this.send(client, {
      channel: 'post-response',
      data: { ok: true, endpoint: msg.endpoint },
      ts: Date.now(),
    });
  }

  /**
   * Start periodic push for a channel.
   * This serves clients that don't receive event-driven updates (e.g. during development
   * when no ARM is connected) — ensures data is pushed on a schedule.
   */
  private startPeriodicPush(channel: string, intervalMs: number): void {
    const timer = setInterval(() => {
      this.broadcast(channel);
    }, intervalMs);
    this.pushTimers.set(channel, timer);
  }

  /** Stop a periodic push */
  stopPeriodicPush(channel: string): void {
    const timer = this.pushTimers.get(channel);
    if (timer) {
      clearInterval(timer);
      this.pushTimers.delete(channel);
    }
  }

  private send(client: WsClient, data: unknown): void {
    if (client.ws.readyState === WebSocket.OPEN) {
      client.ws.send(JSON.stringify(data));
    }
  }

  /** Heartbeat: ping all clients, drop those that didn't pong */
  private heartbeat(): void {
    for (const [id, client] of this.clients) {
      if (!client.isAlive) {
        console.log(`[WsManager] Dropping dead client: ${id}`);
        client.ws.terminate();
        this.clients.delete(id);
        continue;
      }
      client.isAlive = false;
      client.ws.ping();
    }
  }

  /** Get number of connected clients */
  get clientCount(): number {
    return this.clients.size;
  }

  /** Graceful shutdown */
  close(): void {
    if (this.heartbeatTimer) clearInterval(this.heartbeatTimer);
    for (const timer of this.pushTimers.values()) clearInterval(timer);
    this.pushTimers.clear();

    for (const client of this.clients.values()) {
      client.ws.close(1001, 'Server shutting down');
    }
    this.clients.clear();
    this.wss.close();
    console.log('[WsManager] Shut down');
  }
}
