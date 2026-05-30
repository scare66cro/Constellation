/**
 * logSpool.ts — bounded NDJSON spool for offline-resilient log inserts.
 *
 * When Postgres on the rpi5 is unreachable (process restart, schema
 * migration, brief network blip, full power-off of the Pi while the
 * bridge runs in WSL/QEMU), the regular fire-and-forget insert path
 * drops records on the floor. This spool catches the failed inserts
 * and persists them as NDJSON files so the rows can be replayed once
 * Postgres is back.
 *
 * Design:
 * - One file per logical table (`user_log.ndjson`, etc.) under
 *   `~/.constellation/log_spool/`. Append-only, line-delimited JSON.
 * - Bounded by a per-file byte budget (10 MB default ≈ several hours of
 *   user_log + activity_log at production cadence). When the cap is
 *   hit, the oldest half of the file is rotated off (`.old`) so we
 *   prefer keeping the most recent records — this is the "rotating
 *   circle" semantics requested for the 2 h-power-off survival window.
 * - On startup and on every successful drain attempt, the spool tries
 *   to replay all spooled rows in insertion order; replayed rows that
 *   succeed are removed atomically by truncating the file once it's
 *   fully drained, or by rewriting the unspooled remainder on partial
 *   success.
 * - Drain is also kicked from the next-insert path: a record whose
 *   live insert succeeds triggers a one-shot drain so the spool
 *   doesn't sit forever after pg comes back.
 *
 * NOT a long-term archival store. The pg `paneldb` on the rpi5 SSD
 * remains the source of truth; the spool is purely a recovery buffer.
 *
 * Sim vs. production: identical. The spool dir lives under the bridge
 * process owner's $HOME, which on the Pi5 is `/home/gellert/...` and
 * on Windows dev hosts is `%USERPROFILE%\.constellation\log_spool\`.
 */

import { promises as fs } from 'node:fs';
import * as path from 'node:path';
import * as os from 'node:os';

/** Per-file byte cap. ~10 MB. Tunable via CONSTELLATION_SPOOL_MAX_BYTES. */
const DEFAULT_MAX_BYTES = 10 * 1024 * 1024;

/** Quiet period between auto-drain attempts when pg is failing. */
const DRAIN_BACKOFF_MS = 30_000;

export type SpoolKind = 'user_log' | 'activity_log' | 'pid_log' | 'load_log';

export interface SpoolEntry<T = unknown> {
  ts: string;     // ISO-8601 of when the spool happened (not the row's own ts).
  payload: T;     // Original insert record.
}

/** Caller plugs in their own replay function per kind. */
export type SpoolReplayFn<T = unknown> = (rec: T) => Promise<void>;

export class LogSpool {
  private readonly dir: string;
  private readonly maxBytes: number;
  private readonly replayFns = new Map<SpoolKind, SpoolReplayFn>();
  /** Per-kind last drain attempt (epoch ms). Avoids hammering pg. */
  private lastDrainAttempt = new Map<SpoolKind, number>();
  /** Single in-flight drain per kind to prevent re-entrancy. */
  private drainInFlight = new Set<SpoolKind>();

  constructor(opts: { dir?: string; maxBytes?: number } = {}) {
    this.dir = opts.dir
      ?? path.join(os.homedir(), '.constellation', 'log_spool');
    this.maxBytes = opts.maxBytes
      ?? (Number(process.env.CONSTELLATION_SPOOL_MAX_BYTES) || DEFAULT_MAX_BYTES);
  }

  /** Register a replay handler for a kind. Required before drain works. */
  registerReplay<T>(kind: SpoolKind, fn: SpoolReplayFn<T>): void {
    this.replayFns.set(kind, fn as SpoolReplayFn);
  }

  /**
   * Append a failed-insert record to the spool. Synchronous-feeling
   * fire-and-forget: returns void, errors are logged but don't propagate
   * (the caller is already on the failure path).
   */
  async append<T>(kind: SpoolKind, payload: T): Promise<void> {
    try {
      await fs.mkdir(this.dir, { recursive: true });
      const file = this.fileFor(kind);
      const entry: SpoolEntry<T> = {
        ts: new Date().toISOString(),
        payload,
      };
      const line = JSON.stringify(entry) + '\n';
      await fs.appendFile(file, line, 'utf8');

      // Rotate if oversized. Cheap stat after every write — files are
      // small, this is O(syscall) not O(content).
      const st = await fs.stat(file);
      if (st.size > this.maxBytes) {
        await this.rotate(kind, file);
      }
    } catch (err) {
      console.error(`[LogSpool] append(${kind}) failed:`, (err as Error).message);
    }
  }

  /**
   * Attempt to replay all spooled entries for `kind` against the
   * registered handler. Stops at the first failure so the per-line
   * order (and the gap detector) stays meaningful — partial successes
   * are persisted by rewriting the file with the remainder.
   *
   * No-op if the spool file doesn't exist or the kind has no handler.
   * Backoff prevents hot-loop drains when pg keeps failing.
   */
  async drain(kind: SpoolKind): Promise<{ replayed: number; remaining: number }> {
    if (this.drainInFlight.has(kind)) return { replayed: 0, remaining: -1 };
    const last = this.lastDrainAttempt.get(kind) ?? 0;
    if (Date.now() - last < DRAIN_BACKOFF_MS) {
      return { replayed: 0, remaining: -1 };
    }
    this.lastDrainAttempt.set(kind, Date.now());
    this.drainInFlight.add(kind);

    try {
      const fn = this.replayFns.get(kind);
      if (!fn) return { replayed: 0, remaining: 0 };
      const file = this.fileFor(kind);
      let raw: string;
      try {
        raw = await fs.readFile(file, 'utf8');
      } catch (err: any) {
        if (err.code === 'ENOENT') return { replayed: 0, remaining: 0 };
        throw err;
      }
      if (!raw) return { replayed: 0, remaining: 0 };

      const lines = raw.split('\n').filter(l => l.length > 0);
      let replayed = 0;
      const remaining: string[] = [];
      for (let i = 0; i < lines.length; i++) {
        const line = lines[i];
        let entry: SpoolEntry;
        try {
          entry = JSON.parse(line);
        } catch {
          // Corrupt line — drop it silently rather than wedging the spool.
          continue;
        }
        try {
          await fn(entry.payload);
          replayed++;
        } catch (err) {
          console.warn(`[LogSpool] drain(${kind}) stopped at line ${i + 1}:`,
                       (err as Error).message);
          // Keep this line and everything after it.
          remaining.push(...lines.slice(i));
          break;
        }
      }

      if (remaining.length === 0) {
        await fs.unlink(file).catch(() => {});
      } else {
        // Atomic rewrite via tmp + rename so a crash mid-write doesn't
        // truncate live spool data.
        const tmp = file + '.tmp';
        await fs.writeFile(tmp, remaining.join('\n') + '\n', 'utf8');
        await fs.rename(tmp, file);
      }

      if (replayed > 0) {
        console.log(`[LogSpool] drained ${replayed} ${kind} record(s); ${remaining.length} remaining`);
      }
      return { replayed, remaining: remaining.length };
    } catch (err) {
      console.error(`[LogSpool] drain(${kind}) failed:`,
                    (err as Error).message);
      return { replayed: 0, remaining: -1 };
    } finally {
      this.drainInFlight.delete(kind);
    }
  }

  /** Drain every registered kind. Called on startup. */
  async drainAll(): Promise<void> {
    for (const kind of this.replayFns.keys()) {
      // Reset backoff so startup actually attempts the drain.
      this.lastDrainAttempt.delete(kind);
      await this.drain(kind);
    }
  }

  /** Quick stat for /health: file size per kind. */
  async stats(): Promise<Record<SpoolKind, { bytes: number; entries: number }>> {
    const out: Record<string, { bytes: number; entries: number }> = {};
    for (const kind of ['user_log', 'activity_log', 'pid_log', 'load_log'] as SpoolKind[]) {
      const file = this.fileFor(kind);
      try {
        const st = await fs.stat(file);
        const raw = await fs.readFile(file, 'utf8');
        out[kind] = {
          bytes: st.size,
          entries: raw ? raw.split('\n').filter(l => l.length > 0).length : 0,
        };
      } catch {
        out[kind] = { bytes: 0, entries: 0 };
      }
    }
    return out as Record<SpoolKind, { bytes: number; entries: number }>;
  }

  private fileFor(kind: SpoolKind): string {
    return path.join(this.dir, `${kind}.ndjson`);
  }

  /**
   * Rotation strategy: drop the oldest half. We prefer keeping the
   * most-recent rows because they're the ones the operator is likely
   * about to look at after restoring connectivity, and because the
   * absolute oldest rows are the most likely to have timed out of the
   * UI's display window anyway. The dropped half is moved to `.old` so
   * a debugging session can still recover it if needed.
   */
  private async rotate(kind: SpoolKind, file: string): Promise<void> {
    try {
      const raw = await fs.readFile(file, 'utf8');
      const lines = raw.split('\n').filter(l => l.length > 0);
      const keep = lines.slice(Math.floor(lines.length / 2));
      const drop = lines.slice(0, Math.floor(lines.length / 2));
      await fs.writeFile(file + '.old', drop.join('\n') + '\n', 'utf8');
      await fs.writeFile(file, keep.join('\n') + '\n', 'utf8');
      console.warn(`[LogSpool] rotated ${kind}: kept ${keep.length}, archived ${drop.length} to .old`);
    } catch (err) {
      console.error(`[LogSpool] rotate(${kind}) failed:`,
                    (err as Error).message);
    }
  }
}
