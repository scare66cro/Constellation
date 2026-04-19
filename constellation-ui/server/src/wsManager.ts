/**
 * WebSocket connection manager.
 * Manages client connections, channel subscriptions, and broadcasts.
 *
 * Channels map to the "protocol" names used by the UI's PollingClient:
 *   - "frontmatter-data"  → main dashboard data (polled every 3s)
 *   - "header-data"       → panel name, mode, date/time
 *   - "tcpip-data"        → network node list for dropdown
 *   - "upgrade-data"      → firmware upgrade progress
 *   - "network-data"      → network monitor data
 *   - "download-data"     → log download progress
 *   - "equipment-data"    → equipment status images
 *
 * Instead of the UI polling every 3s, the server pushes data on these channels
 * whenever the ARM sends an update, or on a configurable interval.
 */

import { WebSocketServer, WebSocket, type RawData } from 'ws';
import type { Server as HttpServer } from 'http';
import type { IncomingMessage } from 'http';
import type { DataCache } from './dataCache.js';
import type { UpgradeManager, UpgradeStatus } from './upgradeManager.js';

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
  private vfdClient: any = null;

  constructor(server: HttpServer, dataCache: DataCache, vfdClient?: any) {
    this.dataCache = dataCache;
    this.vfdClient = vfdClient ?? null;

    this.wss = new WebSocketServer({
      server,
      path: '/iot/ws',
      // Verify origin for security (allow all during development)
      verifyClient: (_info: unknown) => true,
    });

    this.wss.on('connection', (ws, req) => this.handleConnection(ws, req));
    this.wss.on('error', (err) => console.error('[WsManager] Server error:', err));

    // Start heartbeat to detect dead connections
    this.heartbeatTimer = setInterval(() => this.heartbeat(), 30_000);

    // Start periodic push for dashboard channels
    this.startPeriodicPush('frontmatter-data', 3_000);
    this.startPeriodicPush('header-data', 3_000);
    this.startPeriodicPush('sensor-data', 5_000);
    this.startPeriodicPush('tcpip-data', 10_000);

    console.log('[WsManager] WebSocket server attached at /iot/ws');
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
      case 'frontmatter-data':
        return this.dataCache.buildFrontmatterData();

      case 'header-data':
        return this.dataCache.buildHeaderData();

      case 'tcpip-data':
        return this.dataCache.buildTcpIpData();

      case 'upgrade-data':
        if (this.upgradeManager) {
          return this.upgradeManager.getStatus();
        }
        return { UpgradeStatus: '', UpgradingSoftware: false, isEmpty: true };

      case 'network-data':
        return this.dataCache.buildPageData('network') ?? {};

      case 'sensor-data':
        return this.dataCache.buildPageData('sensors') ?? {};

      case 'vfd-data':
        return this.vfdClient ? { drives: this.vfdClient.getDrives() } : { drives: [] };

      case 'equipment-data': {
        const eqStatus = this.dataCache.getEquipStatus();
        const pwmRaw = this.dataCache.getByVarName('PWMData')?.value ?? '';
        const ioNamesRaw = this.dataCache.getByVarName('IoNamesData')?.value ?? '';
        const auxRaw = this.dataCache.getByVarName('EquipAuxData')?.value ?? '';
        const miscRaw = this.dataCache.getByVarName('MiscData')?.value ?? '';
        const basicRaw = this.dataCache.getByVarName('P2BasicSetupData')?.value ?? '';
        const basicArr = basicRaw ? basicRaw.split(',') : [];
        return {
          eqStatus,
          pwmConfig: pwmRaw ? pwmRaw.split(',') : ['0'],
          outputConfig: this.dataCache.getEqIndexedOutputConfig(),
          ioNames: ioNamesRaw ? ioNamesRaw.split(',') : [],
          auxSwitches: auxRaw ? auxRaw.split(',') : [],
          miscData: miscRaw ? miscRaw.split(',') : ['0'],
          systemMode: basicArr[4] ?? '0',
        };
      }

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
    // Map ARM message tags to affected WebSocket channels
    const tagChannels: Record<string, string[]> = {
      'main':           ['frontmatter-data'],
      'p1Plenum':       ['frontmatter-data'],
      'EquipStatus':    ['frontmatter-data', 'equipment-data'],
      'EquipSwitch':    ['frontmatter-data'],
      'EquipAux':       ['frontmatter-data'],
      'Mode':           ['frontmatter-data'],
      'CurrentMode':    ['frontmatter-data', 'header-data'],
      'PanelName':      ['header-data', 'tcpip-data'],
      'DateTime':       ['header-data'],
      'WarningData':    ['frontmatter-data'],
      'AlarmData':      ['frontmatter-data'],
      'StatusData':     ['frontmatter-data'],
      'SensorList':     ['frontmatter-data', 'sensor-data'],
      'SensorData':     ['sensor-data'],
      'AnalogAll':      ['sensor-data'],
      'FirmwareVersion':['frontmatter-data'],
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
