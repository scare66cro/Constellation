import { statusStore, type Headers } from "$lib/store";

export type UpgradeStatus = { UpgradeStatus: string, UpgradingSoftware: boolean, isEmpty: boolean };
export type DownloadProgress = { current: number | undefined, total: number | undefined };
export type TcpipUpdate = { 
  newIpAddress: string, 
  newPort: string, 
  protocol: string,
  restartRequired: boolean 
};

type SetData = (data: Record<string, string | string[]> | Headers | UpgradeStatus | DownloadProgress | TcpipUpdate) => void;

export default class PollingClient {
  // Track all active client instances so we can clear polling intervals (reboot, navigation, etc.)
  private static instances: Set<PollingClient> = new Set();
  /** Closes all active polling loops */
  public static closeAll(reason = 'Reboot pause'): void {
    for (const inst of Array.from(PollingClient.instances)) {
      try {
        inst.close(4001, reason);
      } catch (e) {
        console.warn('Error stopping poller during reboot', e);
      }
    }
  }

  private url: string;
  private protocol: string;
  private cb: SetData;
  private handle: NodeJS.Timeout | null = null;
  private failures = 0;
  private backoffMs = 0;
  private nextAllowedPoll = 0;
  private abortController: AbortController | null = null;

  constructor(url: string, protocol: string, cb: SetData) {
    this.url = url;
    this.protocol = protocol;
    this.cb = cb;
  }

  private normalizeBase(url: string): string {
    // Allow callers to still pass ws:// or wss://; convert to http(s) for polling
    if (url.startsWith('ws://')) return 'http://' + url.substring(5);
    if (url.startsWith('wss://')) return 'https://' + url.substring(6);
    return url;
  }

  private async handleData(raw: unknown) {
    try {
      const parsed = typeof raw === 'string' ? JSON.parse(raw) : raw;
      this.cb(parsed as Record<string, string | string[]>);
    } catch (e) {
      console.error('[PollingClient] Failed to process data', e, raw);
    }
  }

  public connect(): void {
    if (this.handle !== null) {
      console.warn('PollingClient is already connected or connecting.');
      return;
    }
    PollingClient.instances.add(this);
    const base = this.normalizeBase(this.url).replace(/\/$/, '');
    const endpointName = this.protocol;
    const fullUrl = `${base}/${endpointName}`;

    this.handle = setInterval(async () => {
      if (Date.now() < this.nextAllowedPoll) {
        return;
      }
      
      // Cancel previous request if still pending
      if (this.abortController) {
        this.abortController.abort();
      }
      this.abortController = new AbortController();
      
      try {
        const resp = await fetch(fullUrl, {
          cache: 'no-store',
          credentials: 'include',  // Include cookies for cross-origin requests (loopback <-> network IP)
          signal: this.abortController.signal
        });
        statusStore.update(s => ({ ...s, status: resp.status }));
        // Attempt to parse JSON; if the body is not JSON (likely an HTML/browser error page),
        // construct a synthetic JSON object with status details so downstream code can react.
        let json: unknown;
        try {
          // If the backend is rebooting during an upgrade, it often returns HTTP 503 with an HTML body.
          // Treat this as a transient browser error so the UI keeps waiting instead of surfacing a 503 page.
          if (resp.status === 503 && this.protocol === 'upgrade-data') {
            json = {
              browserError: true,
              status: resp.status,
              statusText: resp.statusText
            };
          } else {
            // Prefer JSON when possible
            json = await resp.json();
          }
        } catch (_) {
          // Fallback: build an object indicating a browser-side error response body
          json = {
            browserError: true,
            status: resp.status,
            statusText: resp.statusText
          };
        }
        if (!resp.ok && this.protocol !== 'upgrade-data') {
          this.failures += 1;
          if (this.failures % 5 === 0) {
            console.warn(`[PollingClient] Poll failed ${this.failures}x`, resp.status, resp.statusText);
          }
          return;
        }
        // For upgrade-data polls, consider 503 transient and keep waiting without failing backoff.
        if (!resp.ok && this.protocol === 'upgrade-data' && resp.status === 503) {
          this.failures = 0;
          this.backoffMs = 0;
          this.nextAllowedPoll = Date.now() + 3000;
          await this.handleData(json);
          return;
        }
        this.failures = 0;
        this.backoffMs = 0;
        this.nextAllowedPoll = 0;
        await this.handleData(json);
      } catch (e) {
        this.failures += 1;
        statusStore.update(s => ({ ...s, status: 0 }));
        const message = e instanceof Error ? e.message : String(e);
        const isConnectionRefused = message.includes('ERR_CONNECTION_REFUSED') || message.includes('Failed to fetch');
        if (isConnectionRefused && this.protocol === 'upgrade-data') {
          await this.handleData({ browserError: true, status: 0, statusText: 'Connection refused' });
        }
        const nextBackoff = this.backoffMs === 0 ? 1000 : Math.min(5000, this.backoffMs * 2);
        this.backoffMs = nextBackoff;
        this.nextAllowedPoll = Date.now() + this.backoffMs;
        if (this.failures === 3 || this.failures % 10 === 0) {
          console.error('[PollingClient] Repeated polling failure', this.failures, e, `backing off ${this.backoffMs}ms`);
        }
      }
    }, 3000);
  }

  public close(code = 1000, reason = 'Client closed connection'): void { // code/reason kept for API parity
        // Cancel any pending fetch requests
        if (this.abortController) {
          this.abortController.abort();
          this.abortController = null;
        }
    if (this.handle) {
      clearInterval(this.handle);
      this.handle = null;
    }
    PollingClient.instances.delete(this);
  }
}

export type { SetData };
