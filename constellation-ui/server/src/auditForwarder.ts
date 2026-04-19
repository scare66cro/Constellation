/**
 * Audit → Django forwarder.
 *
 * Subscribes to accountStore's onAuditAppend and periodically flushes
 * batches to Django /api/bridge/audit/ so operators can query who
 * changed what across their fleet.
 *
 * At-least-once in-memory delivery:
 *   - Entries queued on append.
 *   - Every FLUSH_INTERVAL_MS we try to POST up to BATCH_SIZE at once.
 *   - On success, drop them from the queue.
 *   - On failure, keep them and try again next tick.
 *   - If the queue exceeds MAX_QUEUE, drop the oldest (rare — requires
 *     Django to be down for hours of active usage).
 *
 * Bridge restarts lose any un-forwarded entries still in memory. That's
 * acceptable because the local NDJSON audit-log is the authoritative
 * on-device record — Django is the cross-device dashboard mirror.
 */

import type { AuditEntry } from './accountStore.js';
import { onAuditAppend } from './accountStore.js';

const FLUSH_INTERVAL_MS = 30_000;
const BATCH_SIZE        = 100;
const MAX_QUEUE         = 2000;

const queue: AuditEntry[] = [];
let timer: NodeJS.Timeout | null = null;
let inFlight = false;

function djangoBase(): string {
  return process.env.DJANGO_URL || 'http://localhost:8000';
}

function deviceToken(): string | null {
  return process.env.DJANGO_TOKEN || null;
}

function enabled(): boolean {
  // Mirror djangoSync's policy: enabled whenever we have a token,
  // unless explicitly disabled. Keeps audit + telemetry symmetric.
  if (process.env.DJANGO_SYNC_ENABLED === 'false') return false;
  return !!deviceToken();
}

async function flushOnce(): Promise<void> {
  if (inFlight) return;
  if (queue.length === 0) return;
  if (!enabled()) return;

  inFlight = true;
  const batch = queue.slice(0, BATCH_SIZE);
  try {
    const resp = await fetch(`${djangoBase()}/api/bridge/audit/`, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'Authorization': `Token ${deviceToken()}`,
      },
      body: JSON.stringify({
        entries: batch.map(e => ({
          ts:     e.ts,
          kind:   e.kind,
          actor:  e.actor,
          slot:   e.slot,
          level:  e.level,
          route:  e.route,
          detail: e.diff ? { summary: e.detail, changed: e.diff } : e.detail,
          ip:     e.ip,
        })),
      }),
      signal: AbortSignal.timeout(10_000),
    });

    if (resp.ok) {
      // Success — drop the batch from the head of the queue.
      queue.splice(0, batch.length);
    } else {
      console.warn(`[auditForwarder] Django returned ${resp.status}; will retry`);
    }
  } catch (e: any) {
    // Network error, Django down, etc. Keep queue; try again next tick.
    console.warn(`[auditForwarder] Flush failed (${e.message}); will retry`);
  } finally {
    inFlight = false;
  }
}

function enqueue(entry: AuditEntry): void {
  queue.push(entry);
  if (queue.length > MAX_QUEUE) {
    const dropped = queue.length - MAX_QUEUE;
    queue.splice(0, dropped);
    console.warn(`[auditForwarder] Queue overflow, dropped ${dropped} oldest entries`);
  }
}

/** Start the forwarder. Safe to call multiple times; second call is a no-op. */
export function startAuditForwarder(): void {
  if (timer) return;
  onAuditAppend(enqueue);
  timer = setInterval(() => { void flushOnce(); }, FLUSH_INTERVAL_MS);
  // Run an initial flush soon so long-running buffered entries don't wait the full tick.
  setTimeout(() => { void flushOnce(); }, 2_000).unref?.();
  timer.unref?.();
  console.log('[auditForwarder] Started (interval=30s, batch=100, max=2000)');
}

/** Force a flush (e.g. for testing). Returns number of entries sent. */
export async function flushAuditQueueNow(): Promise<number> {
  const before = queue.length;
  await flushOnce();
  return before - queue.length;
}

export function auditQueueDepth(): number {
  return queue.length;
}
