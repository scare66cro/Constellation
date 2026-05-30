/**
 * WebSocket client for the Agristar bridge server.
 *
 * Pure WebSocket transport — subscribes to named channels
 * (frontmatter-data, header-data, etc.) and receives real-time pushes.
 * Auto-reconnects with exponential backoff capped at 10 s.
 *
 * Protocol:
 *   Client → Server:  { "action": "subscribe",   "channel": "frontmatter-data" }
 *   Client → Server:  { "action": "unsubscribe", "channel": "frontmatter-data" }
 *   Server → Client:  { "channel": "frontmatter-data", "data": {...}, "ts": 123 }
 *
 * Note (S9k cleanup 2026-04-21): the HTTP polling fallback was removed
 * — the per-channel `/iot/ws/{channel}` GET endpoints it used to hit
 * (download-data, upgrade-data, tcpip-data) were retired with the
 * legacy PollingClient.  WS reconnect is the only recovery path now.
 */

import { statusStore, type Headers } from '$lib/store';

export type UpgradeStatus = { UpgradeStatus: string; UpgradingSoftware: boolean; isEmpty: boolean };
export type DownloadProgress = { current: number | undefined; total: number | undefined };
export type TcpipUpdate = {
  newIpAddress: string;
  newPort: string;
  protocol: string;
  restartRequired: boolean;
};

type StatusState = { status?: number };

type SetData = (
  data:
    | Record<string, string | string[]>
    | Headers
    | UpgradeStatus
    | DownloadProgress
    | TcpipUpdate
) => void;

interface ChannelMessage {
  channel: string;
  data: unknown;
  ts: number;
}

export default class WsClient {
  // ─── Static: track all instances for bulk operations (reboot, etc.) ───
  private static instances: Set<WsClient> = new Set();

  /** Close all active WebSocket connections */
  public static closeAll(reason = 'Reboot pause'): void {
    for (const inst of Array.from(WsClient.instances)) {
      try {
        inst.close(4001, reason);
      } catch (e) {
        console.warn('Error closing WsClient during reboot', e);
      }
    }
  }

  // ─── Instance state ───
  private url: string;
  private channel: string;
  private cb: SetData;
  private ws: WebSocket | null = null;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private reconnectAttempts = 0;
  private maxReconnectDelay = 10_000;
  private closed = false;

  constructor(url: string, channel: string, cb: SetData) {
    this.url = url;
    this.channel = channel;
    this.cb = cb;
  }

  // ─── Public API ───

  /**
   * Open the WebSocket connection and subscribe to the channel.
   */
  public connect(): void {
    if (this.ws) {
      console.warn('[WsClient] Already connected or connecting.');
      return;
    }
    this.closed = false;
    WsClient.instances.add(this);
    this.openWebSocket();
  }

  /**
   * Close the connection and stop reconnection.
   */
  public close(code = 1000, reason = 'Client closed connection'): void {
    this.closed = true;

    // Clear reconnection timer
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }

    // Close WebSocket
    if (this.ws) {
      try {
        this.ws.close(code, reason);
      } catch {
        // Ignore close errors
      }
      this.ws = null;
    }

    WsClient.instances.delete(this);
  }

  // ─── WebSocket connection management ───

  private openWebSocket(): void {
    if (this.closed) return;

    try {
      const wsUrl = this.buildWsUrl();
      this.ws = new WebSocket(wsUrl);

      this.ws.onopen = () => {
        console.log(`[WsClient] Connected to ${this.channel}`);
        this.reconnectAttempts = 0;

        // Subscribe to the channel
        this.ws!.send(JSON.stringify({ action: 'subscribe', channel: this.channel }));

        statusStore.update((s: StatusState) => ({ ...s, status: 200 }));
      };

      this.ws.onmessage = (event) => {
        this.handleMessage(event.data);
      };

      this.ws.onclose = (event) => {
        console.log(`[WsClient] Disconnected from ${this.channel}: ${event.code} ${event.reason}`);
        this.ws = null;
        statusStore.update((s: StatusState) => ({ ...s, status: 0 }));

        if (!this.closed) {
          this.scheduleReconnect();
        }
      };

      this.ws.onerror = (_event) => {
        // The onclose handler will fire after onerror, which handles reconnection.
      };
    } catch (err) {
      console.error(`[WsClient] Failed to create WebSocket for ${this.channel}:`, err);
      this.scheduleReconnect();
    }
  }

  private handleMessage(raw: string | ArrayBuffer | Blob): void {
    try {
      const text = typeof raw === 'string' ? raw : new TextDecoder().decode(raw as ArrayBuffer);
      const msg: ChannelMessage = JSON.parse(text);

      // Only process messages for our channel
      if (msg.channel === this.channel && msg.data != null) {
        this.cb(msg.data as Record<string, string | string[]>);
      }
    } catch (err) {
      console.error(`[WsClient] Failed to parse message for ${this.channel}:`, err);
    }
  }

  private scheduleReconnect(): void {
    if (this.closed || this.reconnectTimer) return;

    this.reconnectAttempts++;
    // Exponential backoff: 1s, 2s, 4s, 8s, capped at maxReconnectDelay
    const delay = Math.min(1000 * Math.pow(2, this.reconnectAttempts - 1), this.maxReconnectDelay);

    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      if (!this.closed) {
        this.openWebSocket();
      }
    }, delay);
  }

  // ─── Helpers ───

  private buildWsUrl(): string {
    let url = this.url;
    // Convert http(s) to ws(s)
    if (url.startsWith('http://')) url = 'ws://' + url.substring(7);
    else if (url.startsWith('https://')) url = 'wss://' + url.substring(8);
    else if (!url.startsWith('ws://') && !url.startsWith('wss://')) {
      url = 'ws://' + url;
    }
    return url;
  }
}

export type { SetData };
