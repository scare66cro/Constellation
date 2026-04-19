/**
 * WebSocket client for the Agristar bridge server.
 *
 * Replaces PollingClient with a true WebSocket connection:
 *   - Subscribes to named channels (frontmatter-data, header-data, etc.)
 *   - Receives real-time pushes instead of polling every 3s
 *   - Automatic reconnection with exponential backoff
 *   - Falls back to HTTP polling if WebSocket fails persistently
 *   - Same public API as PollingClient for drop-in replacement
 *
 * Protocol:
 *   Client → Server:  { "action": "subscribe",   "channel": "frontmatter-data" }
 *   Client → Server:  { "action": "unsubscribe", "channel": "frontmatter-data" }
 *   Server → Client:  { "channel": "frontmatter-data", "data": {...}, "ts": 123 }
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

  // Polling fallback state
  private pollHandle: ReturnType<typeof setInterval> | null = null;
  private abortController: AbortController | null = null;
  private pollingFallback = false;
  private wsFailures = 0;
  private readonly WS_FAILURE_THRESHOLD = 5; // fall back to polling after N consecutive WS failures

  constructor(url: string, channel: string, cb: SetData) {
    this.url = url;
    this.channel = channel;
    this.cb = cb;
  }

  // ─── Public API (matches PollingClient) ───

  /**
   * Open the WebSocket connection and subscribe to the channel.
   * Falls back to HTTP polling if WebSocket is unavailable.
   */
  public connect(): void {
    if (this.ws || this.pollHandle) {
      console.warn('[WsClient] Already connected or connecting.');
      return;
    }
    this.closed = false;
    WsClient.instances.add(this);
    this.openWebSocket();
  }

  /**
   * Close the connection and stop all polling/reconnection.
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

    // Stop polling fallback
    this.stopPolling();

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
        this.wsFailures = 0;
        this.pollingFallback = false;
        this.stopPolling(); // Stop fallback polling if it was active

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
        this.wsFailures++;
        // The onclose handler will fire after onerror, which handles reconnection
        if (this.wsFailures >= this.WS_FAILURE_THRESHOLD && !this.pollingFallback) {
          console.warn(
            `[WsClient] ${this.wsFailures} consecutive WS failures for ${this.channel}, falling back to HTTP polling`
          );
          this.pollingFallback = true;
          this.startPollingFallback();
        }
      };
    } catch (err) {
      console.error(`[WsClient] Failed to create WebSocket for ${this.channel}:`, err);
      this.wsFailures++;
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

  // ─── HTTP polling fallback ───

  private startPollingFallback(): void {
    if (this.pollHandle) return;

    const base = this.normalizeBase(this.url).replace(/\/$/, '');
    const fullUrl = `${base}/${this.channel}`;

    this.pollHandle = setInterval(async () => {
      if (this.abortController) {
        this.abortController.abort();
      }
      this.abortController = new AbortController();

      try {
        const resp = await fetch(fullUrl, {
          cache: 'no-store',
          credentials: 'include',
          signal: this.abortController.signal,
        });
        statusStore.update((s: StatusState) => ({ ...s, status: resp.status }));

        if (resp.ok) {
          const json = await resp.json();
          this.cb(json as Record<string, string | string[]>);
        }
      } catch (e) {
        if (e instanceof Error && e.name === 'AbortError') return;
        statusStore.update((s: StatusState) => ({ ...s, status: 0 }));
      }
    }, 3000);
  }

  private stopPolling(): void {
    if (this.abortController) {
      this.abortController.abort();
      this.abortController = null;
    }
    if (this.pollHandle) {
      clearInterval(this.pollHandle);
      this.pollHandle = null;
    }
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

  private normalizeBase(url: string): string {
    if (url.startsWith('ws://')) return 'http://' + url.substring(5);
    if (url.startsWith('wss://')) return 'https://' + url.substring(6);
    return url;
  }
}

export type { SetData };
