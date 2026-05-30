/**
 * protoStreamClient.ts — Phase 0.7 of the proto-direct redesign.
 *
 * Singleton browser-side client for the bridge's `/proto/stream` binary
 * WebSocket. Owns:
 *
 *   1. The single WebSocket connection (auto-reconnect with exponential
 *      backoff capped at 10 s).
 *   2. The binary frame parser:  [u16 LE tag][u32 LE len][len bytes].
 *   3. The control plane (text JSON):
 *        { action: 'subscribe',   tags: [...] }   — additive
 *        { action: 'subscribe',   all: true  }    — firehose
 *        { action: 'unsubscribe', tags: [...] }
 *        { action: 'unsubscribe', all: true  }
 *        { action: 'ping' }                       — server replies pong
 *   4. Per-tag subscriber ref-counting so the WebSocket only asks for
 *      what's actually being read by a mounted Svelte store.
 *
 * Consumers are expected to be `protoStores.ts`-style readable stores;
 * direct use from components is discouraged. See
 * docs/proto-migration-pattern.md for the per-page recipe.
 */

import { isKnownTag, type Tag } from './protoTags.js';

type FrameListener = (tag: number, payload: Uint8Array) => void;

interface SubscribeMsg {
	action: 'subscribe' | 'unsubscribe';
	tags?: number[];
	all?: boolean;
}

interface PingMsg {
	action: 'ping';
}

const RECONNECT_BASE_MS = 500;
const RECONNECT_MAX_MS = 10_000;
const PING_INTERVAL_MS = 25_000;

class ProtoStreamClient {
	private ws: WebSocket | null = null;
	private url: string;
	private reconnectAttempts = 0;
	private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
	private pingTimer: ReturnType<typeof setInterval> | null = null;
	private closed = false;

	/** Tag → number of mounted subscribers. */
	private refCounts = new Map<number, number>();

	/** Listeners keyed by tag. -1 means "any tag" (rarely useful). */
	private listeners = new Set<FrameListener>();

	/** Tags we have asked the server for (so we don't re-subscribe spam). */
	private remoteTags = new Set<number>();

	constructor(url = '/proto/stream') {
		// Build absolute ws[s]:// URL — relative paths are not allowed by
		// the WebSocket constructor, but the dev/prod proxy still hands
		// us a same-origin path.
		if (url.startsWith('ws://') || url.startsWith('wss://')) {
			this.url = url;
		} else if (typeof window !== 'undefined') {
			const proto = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
			this.url = `${proto}//${window.location.host}${url}`;
		} else {
			// SSR / Node: defer; consumers shouldn't connect during SSR.
			this.url = url;
		}
	}

	// ─── Public API ─────────────────────────────────────────────────────

	/** Register a frame listener. Returns an unsubscribe function. */
	addListener(fn: FrameListener): () => void {
		this.listeners.add(fn);
		this.ensureOpen();
		return () => this.listeners.delete(fn);
	}

	/**
	 * Increment subscription ref-count for `tag`. Sends a `subscribe`
	 * frame to the bridge when the ref-count crosses 0 → 1. Returns a
	 * release fn that decrements; on 1 → 0 it sends `unsubscribe`.
	 */
	acquireTag(tag: Tag): () => void {
		const next = (this.refCounts.get(tag) ?? 0) + 1;
		this.refCounts.set(tag, next);
		if (next === 1) {
			this.sendSubscribe([tag]);
		}
		return () => this.releaseTag(tag);
	}

	/** Force-close the connection (e.g. during a controlled reboot). */
	close(): void {
		this.closed = true;
		if (this.reconnectTimer) {
			clearTimeout(this.reconnectTimer);
			this.reconnectTimer = null;
		}
		if (this.pingTimer) {
			clearInterval(this.pingTimer);
			this.pingTimer = null;
		}
		if (this.ws) {
			try {
				this.ws.close(1000, 'client close');
			} catch {
				/* ignore */
			}
			this.ws = null;
		}
	}

	// ─── Internals ──────────────────────────────────────────────────────

	private releaseTag(tag: Tag): void {
		const cur = this.refCounts.get(tag) ?? 0;
		if (cur <= 1) {
			this.refCounts.delete(tag);
			this.sendUnsubscribe([tag]);
		} else {
			this.refCounts.set(tag, cur - 1);
		}
	}

	private ensureOpen(): void {
		if (typeof window === 'undefined') return; // SSR no-op
		if (this.ws || this.closed) return;
		this.openSocket();
	}

	private openSocket(): void {
		this.closed = false;
		try {
			this.ws = new WebSocket(this.url);
		} catch (err) {
			console.error('[protoStream] WebSocket constructor threw:', err);
			this.scheduleReconnect();
			return;
		}
		this.ws.binaryType = 'arraybuffer';

		this.ws.onopen = () => {
			this.reconnectAttempts = 0;
			// Re-subscribe to all currently held tags after a reconnect.
			const tags = Array.from(this.refCounts.keys());
			this.remoteTags.clear();
			if (tags.length > 0) this.sendSubscribe(tags);

			if (this.pingTimer) clearInterval(this.pingTimer);
			this.pingTimer = setInterval(() => this.sendCtl({ action: 'ping' } as PingMsg), PING_INTERVAL_MS);
		};

		this.ws.onmessage = (ev) => this.handleMessage(ev.data);

		this.ws.onerror = () => {
			// Let onclose drive reconnect.
		};

		this.ws.onclose = () => {
			this.ws = null;
			if (this.pingTimer) {
				clearInterval(this.pingTimer);
				this.pingTimer = null;
			}
			if (!this.closed) this.scheduleReconnect();
		};
	}

	private scheduleReconnect(): void {
		if (this.reconnectTimer) return;
		const delay = Math.min(RECONNECT_BASE_MS * 2 ** this.reconnectAttempts, RECONNECT_MAX_MS);
		this.reconnectAttempts += 1;
		this.reconnectTimer = setTimeout(() => {
			this.reconnectTimer = null;
			if (!this.closed && this.refCounts.size + this.listeners.size > 0) this.openSocket();
		}, delay);
	}

	private handleMessage(data: ArrayBuffer | string | Blob): void {
		if (typeof data === 'string') {
			// Control-plane reply (subscribed/unsubscribed/pong/error).
			// Currently informational only; surface for debug.
			try {
				const obj = JSON.parse(data);
				if (obj?.action === 'error') {
					console.warn('[protoStream] server error:', obj);
				}
			} catch {
				/* ignore */
			}
			return;
		}
		if (!(data instanceof ArrayBuffer)) {
			// Blob path (Firefox sometimes); coerce.
			(data as Blob).arrayBuffer().then((buf) => this.parseFrames(buf));
			return;
		}
		this.parseFrames(data);
	}

	private parseFrames(buf: ArrayBuffer): void {
		const view = new DataView(buf);
		let off = 0;
		while (off + 6 <= buf.byteLength) {
			const tag = view.getUint16(off, true);
			const len = view.getUint32(off + 2, true);
			off += 6;
			if (off + len > buf.byteLength) {
				console.warn('[protoStream] truncated frame', { tag, len, remain: buf.byteLength - off });
				return;
			}
			const payload = new Uint8Array(buf, off, len);
			off += len;
			for (const fn of this.listeners) {
				try {
					fn(tag, payload);
				} catch (e) {
					console.error('[protoStream] listener threw for tag', tag, e);
				}
			}
		}
	}

	private sendSubscribe(tags: number[]): void {
		const fresh = tags.filter((t) => !this.remoteTags.has(t));
		if (fresh.length === 0) return;
		fresh.forEach((t) => this.remoteTags.add(t));
		this.sendCtl({ action: 'subscribe', tags: fresh });
	}

	private sendUnsubscribe(tags: number[]): void {
		const known = tags.filter((t) => this.remoteTags.has(t));
		if (known.length === 0) return;
		known.forEach((t) => this.remoteTags.delete(t));
		this.sendCtl({ action: 'unsubscribe', tags: known });
	}

	private sendCtl(msg: SubscribeMsg | PingMsg): void {
		if (!this.ws || this.ws.readyState !== WebSocket.OPEN) return;
		try {
			this.ws.send(JSON.stringify(msg));
		} catch (err) {
			console.warn('[protoStream] send failed:', err);
		}
	}
}

// Singleton — mirrors the `/iot/ws` pattern (one socket per tab).
export const protoStream = new ProtoStreamClient();

// Re-export for tests / advanced consumers that want their own instance.
export { ProtoStreamClient };

// Sanity export so tag registry stays imported even if a page only uses
// the singleton, helping tree-shaking + intellisense.
export { isKnownTag };
