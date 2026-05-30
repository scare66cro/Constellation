/**
 * logDatabase.ts — PostgreSQL log storage for Constellation
 *
 * Replaces the SD card. Firmware pushes LogRecord/ActivityEvent/PidLogRecord/
 * LoadLogRecord messages via protobuf; this module stores them persistently
 * in PostgreSQL on the rpi5 (database `paneldb`, owned by `gellert`).
 *
 * Tables:
 *   user_log     — periodic sensor/process snapshots (graph/table views)
 *   sensor_log   — per-record temp/humid sensor values (child of user_log)
 *   activity_log — equipment state changes, warnings, mode changes
 *   pid_log      — per-loop PID controller telemetry
 *   load_log     — periodic onion-quality monitor records
 *   load_log_bay — per-bay child rows of load_log
 *   audit_log    — user-driven settings changes (accountability)
 *
 * Connection: PG_DSN env var, e.g. `postgres:///paneldb` (Unix socket,
 * peer auth as the bridge's run-user). Default falls back to that.
 *
 * All public methods are async — pg has no sync mode. Insert call sites
 * in novaLogDataStore.ts use fire-and-forget (`void insertX(...).catch(...)`)
 * so the firmware-event handlers stay non-blocking.
 */

import pg from 'pg';
import { LogSpool } from './logSpool.js';
const { Pool } = pg;

const DEFAULT_DSN = 'postgres:///paneldb?host=/var/run/postgresql';
const PG_DSN = process.env.PG_DSN ?? DEFAULT_DSN;

// ── Public row types (unchanged from the SQLite version) ─────────────────

export interface UserLogRow {
  id: number;
  date: string;
  time: string;
  ts: string;
  plenum_temp_sp: number;
  plenum_temp: number;
  cool_avl_temp: number;
  plenum_humid_sp: number;
  mode: number;
  fan_speed: number;
  cool_output: number;
  refrig_output: number;
  burner_output: number;
  calc_humid: number;
  fan_runtime_min: number;
  co2_level: number;
}

export interface SensorLogRow {
  log_id: number;
  sensor_id: number;
  sensor_type: string;
  value: number;
}

export interface ActivityLogRow {
  id: number;
  date: string;
  time: string;
  ts: string;
  event_type: number;
  eq_index: number;
  description: string;
  new_state: number;
  mode: number;
}

export interface PidLogRow {
  id: number;
  date: string;
  time: string;
  ts: string;
  loop_index: number;
  p_term: number;
  i_term: number;
  d_term: number;
  error: number;
  output: number;
  sequence: number;
}

export interface LoadLogRow {
  id: number;
  date: string;
  time: string;
  ts: string;
  record_num: number;
  sequence: number;
}

export interface LoadLogBayRow {
  load_id: number;
  bay_index: number;
  pipe: number;
  sensor_x10: number;
  status: number;
}

export interface AuditLogRow {
  id: number;
  ts: string;
  kind: string;        // 'save' | 'login' | 'logout' | 'login_fail' | 'level_change'
  actor: string;       // username or 'factory'
  slot: number | null; // 0..9 user slot, null if N/A
  level: number;       // 0/1/2 access level
  route: string;       // e.g. '/proto/write/14'
  detail: string;      // e.g. 'FailureSettings field 14'
  ip: string | null;
}

interface InsertUserLogRecord {
  date: string;
  time: string;
  plenum_temp_sp: number;
  plenum_temp: number;
  cool_avl_temp: number;
  plenum_humid_sp: number;
  mode: number;
  fan_speed: number;
  cool_output: number;
  refrig_output: number;
  burner_output: number;
  calc_humid: number;
  fan_runtime_min: number;
  co2_level: number;
  temps?: Array<{ sensor_id: number; value: number }>;
  humids?: Array<{ sensor_id: number; value: number }>;
}

interface InsertPidLogRecord {
  date: string;
  time: string;
  loop_index: number;
  p_term: number;
  i_term: number;
  d_term: number;
  error: number;
  output: number;
  sequence: number;
}

interface InsertLoadLogRecord {
  date: string;
  time: string;
  record_num: number;
  sequence: number;
  bays: Array<{ pipe: number; sensor_x10: number; status: number }>;
}

interface InsertActivityRecord {
  date: string;
  time: string;
  event_type: number;
  eq_index: number;
  description: string;
  new_state: number;
  mode: number;
}

interface InsertAuditRecord {
  kind: string;
  actor: string;
  slot: number | null;
  level: number;
  route: string;
  detail: string;
  ip?: string | null;
}

export class LogDatabase {
  private pool: pg.Pool;
  private ready: Promise<void>;
  /**
   * Offline-resilient spool. When a live insert fails (pg down, schema
   * mid-migration, transient network blip), the row is appended to
   * `~/.constellation/log_spool/<kind>.ndjson` and replayed once pg is
   * back. Bounded ~10 MB per file (rotating-circle semantics) so the
   * disk footprint stays predictable across multi-hour outages. See
   * logSpool.ts for the full design + sim/prod-identical caveats.
   */
  private readonly spool: LogSpool;
  /** Retention sweep timer; cleared on close(). */
  private retentionTimer: ReturnType<typeof setInterval> | null = null;

  constructor() {
    this.pool = new Pool({
      connectionString: PG_DSN,
      max: 8,
      idleTimeoutMillis: 30_000,
    });
    this.pool.on('error', (err: Error) => {
      console.error('[LogDB] pool error:', err.message);
    });
    this.spool = new LogSpool();
    /* Register replay handlers BEFORE schema init so a drain attempted
     * during cold-start sees a fully-wired spool. The handlers call
     * the same insert*Direct() methods used by the live path — anything
     * that can be inserted live can be replayed. */
    this.spool.registerReplay<InsertUserLogRecord>('user_log',
      (rec) => this.insertUserLogDirect(rec).then(() => undefined));
    this.spool.registerReplay<InsertActivityRecord>('activity_log',
      (rec) => this.insertActivityEventDirect(rec));
    this.spool.registerReplay<InsertPidLogRecord>('pid_log',
      (rec) => this.insertPidLogDirect(rec));
    this.spool.registerReplay<InsertLoadLogRecord>('load_log',
      (rec) => this.insertLoadLogDirect(rec).then(() => undefined));
    this.ready = this.createSchema().catch(err => {
      // Don't crash the bridge when Postgres isn't reachable (common
      // on Windows dev hosts that don't run a local PG). The Nova
      // log/audit insert call sites await this promise — they'll log
      // and skip if it stays rejected. Re-thrown so callers see it.
      console.error('[LogDB] schema init failed (PG unavailable, log/audit disabled):', err.message);
      throw err;
    });
    // Swallow the unhandled-rejection on this.ready itself; only
    // call sites that await it will see the rejection.
    this.ready.catch(() => { /* observed */ });
    // Best-effort startup drain — non-blocking. Anything that hits the
    // backoff window (no recent attempt) gets one shot now; subsequent
    // attempts are kicked from the live insert path.
    void this.ready.then(() => this.spool.drainAll())
      .catch(() => { /* schema not ready yet; live path will trigger later */ });

    /* Retention timer. Deletes rows older than RETENTION_DAYS (default
     * 90 d) once on startup + every 24 h thereafter. Bounds pg disk
     * footprint so the rpi5 SSD can't fill silently across a long
     * production run. To disable, set RETENTION_DAYS=0. */
    const retentionDays = Number(process.env.RETENTION_DAYS ?? '90');
    if (retentionDays > 0) {
      void this.ready.then(() => this.pruneOlderThan(retentionDays))
        .catch(() => { /* ready rejected; nothing to prune */ });
      this.retentionTimer = setInterval(
        () => { void this.pruneOlderThan(retentionDays); },
        24 * 60 * 60 * 1000,
      );
      // Don't keep the event loop alive solely for this timer.
      this.retentionTimer.unref?.();
    }
  }

  /** Resolves once schema is ready. Insert/query call sites await this internally. */
  whenReady(): Promise<void> {
    return this.ready;
  }

  /** Spool occupancy snapshot for /health and /iot/datainfo. */
  spoolStats() {
    return this.spool.stats();
  }

  /** Manual drain trigger (used by /health endpoint, tests). */
  drainSpool() {
    return this.spool.drainAll();
  }

  /**
   * Cheap pg connectivity probe for /health. Times out fast (default
   * 1 s) so /health stays snappy even when the pool is wedged. Returns
   * `{ ok, latencyMs, error? }` — NEVER throws so the caller can
   * shove the result straight into a JSON response.
   */
  async ping(timeoutMs = 1000): Promise<{ ok: boolean; latencyMs: number; error?: string }> {
    const start = Date.now();
    try {
      await Promise.race([
        this.pool.query('SELECT 1'),
        new Promise<never>((_, rej) =>
          setTimeout(() => rej(new Error('pg ping timeout')), timeoutMs)),
      ]);
      return { ok: true, latencyMs: Date.now() - start };
    } catch (err) {
      return { ok: false, latencyMs: Date.now() - start, error: (err as Error).message };
    }
  }

  /**
   * Bounded retention. Deletes rows older than `days` from each log
   * table. Returns per-table delete counts. Cascades naturally:
   *   user_log → sensor_log (FK ON DELETE CASCADE)
   *   load_log → load_log_bay (FK ON DELETE CASCADE)
   * audit_log is included — 90 d default keeps a quarter of audit
   * history, which matches the Constellation production policy. To
   * keep audit indefinitely, pass a separate `auditDays`.
   *
   * Production cadence: called once on bridge boot + once every 24 h
   * by the timer set up in the constructor. Manual invocation via
   * `logDb.pruneOlderThan(N)` is also safe.
   */
  async pruneOlderThan(days: number, opts: { auditDays?: number } = {}):
    Promise<Record<string, number>> {
    await this.ready;
    const auditDays = opts.auditDays ?? days;
    const out: Record<string, number> = {};
    /* TIMESTAMPTZ comparison against NOW() - INTERVAL is timezone-safe;
     * INTERVAL is built via parameter to avoid SQL injection on the
     * day count. */
    const tables: Array<{ name: string; days: number }> = [
      { name: 'user_log',     days },
      { name: 'activity_log', days },
      { name: 'pid_log',      days },
      { name: 'load_log',     days },
      { name: 'audit_log',    days: auditDays },
    ];
    for (const t of tables) {
      try {
        const r = await this.pool.query(
          `DELETE FROM ${t.name} WHERE ts < NOW() - ($1 || ' days')::interval`,
          [String(t.days)],
        );
        out[t.name] = r.rowCount ?? 0;
      } catch (err) {
        console.error(`[LogDB] prune(${t.name}) failed:`, (err as Error).message);
        out[t.name] = -1;
      }
    }
    const total = Object.values(out).reduce((a, b) => a + Math.max(0, b), 0);
    if (total > 0) {
      console.log(`[LogDB] retention prune: ${total} row(s) older than ${days} d removed:`,
                  out);
    }
    return out;
  }

  private async createSchema(): Promise<void> {
    const client = await this.pool.connect();
    try {
      // Use TIMESTAMPTZ for ts so date-range queries work natively.
      // date/time strings preserved alongside for back-compat with the
      // existing CSV download endpoints + UI rendering.
      await client.query(`
        CREATE TABLE IF NOT EXISTS user_log (
          id              BIGSERIAL PRIMARY KEY,
          date            TEXT NOT NULL,
          time            TEXT NOT NULL,
          ts              TIMESTAMPTZ NOT NULL,
          plenum_temp_sp  REAL DEFAULT 0,
          plenum_temp     REAL DEFAULT 0,
          cool_avl_temp   REAL DEFAULT 0,
          plenum_humid_sp REAL DEFAULT 0,
          mode            INTEGER DEFAULT 0,
          fan_speed       INTEGER DEFAULT 0,
          cool_output     INTEGER DEFAULT 0,
          refrig_output   INTEGER DEFAULT 0,
          burner_output   INTEGER DEFAULT 0,
          calc_humid      REAL DEFAULT 0,
          fan_runtime_min INTEGER DEFAULT 0,
          co2_level       REAL DEFAULT 0
        );
        CREATE INDEX IF NOT EXISTS idx_user_log_ts   ON user_log(ts);
        CREATE INDEX IF NOT EXISTS idx_user_log_date ON user_log(date);

        CREATE TABLE IF NOT EXISTS sensor_log (
          log_id      BIGINT NOT NULL REFERENCES user_log(id) ON DELETE CASCADE,
          sensor_id   INTEGER NOT NULL,
          sensor_type TEXT NOT NULL,
          value       REAL NOT NULL
        );
        CREATE INDEX IF NOT EXISTS idx_sensor_log_id ON sensor_log(log_id);

        CREATE TABLE IF NOT EXISTS activity_log (
          id          BIGSERIAL PRIMARY KEY,
          date        TEXT NOT NULL,
          time        TEXT NOT NULL,
          ts          TIMESTAMPTZ NOT NULL,
          event_type  INTEGER DEFAULT 0,
          eq_index    INTEGER DEFAULT 0,
          description TEXT DEFAULT '',
          new_state   INTEGER DEFAULT 0,
          mode        INTEGER DEFAULT 0
        );
        CREATE INDEX IF NOT EXISTS idx_activity_log_ts   ON activity_log(ts);
        CREATE INDEX IF NOT EXISTS idx_activity_log_date ON activity_log(date);

        CREATE TABLE IF NOT EXISTS pid_log (
          id          BIGSERIAL PRIMARY KEY,
          date        TEXT NOT NULL,
          time        TEXT NOT NULL,
          ts          TIMESTAMPTZ NOT NULL,
          loop_index  INTEGER NOT NULL,
          p_term      REAL DEFAULT 0,
          i_term      REAL DEFAULT 0,
          d_term      REAL DEFAULT 0,
          error       REAL DEFAULT 0,
          output      INTEGER DEFAULT 0,
          sequence    INTEGER DEFAULT 0
        );
        CREATE INDEX IF NOT EXISTS idx_pid_log_ts   ON pid_log(ts);
        CREATE INDEX IF NOT EXISTS idx_pid_log_loop ON pid_log(loop_index, ts);

        CREATE TABLE IF NOT EXISTS load_log (
          id          BIGSERIAL PRIMARY KEY,
          date        TEXT NOT NULL,
          time        TEXT NOT NULL,
          ts          TIMESTAMPTZ NOT NULL,
          record_num  INTEGER NOT NULL,
          sequence    INTEGER DEFAULT 0
        );
        CREATE INDEX IF NOT EXISTS idx_load_log_ts ON load_log(ts);

        CREATE TABLE IF NOT EXISTS load_log_bay (
          load_id    BIGINT NOT NULL REFERENCES load_log(id) ON DELETE CASCADE,
          bay_index  INTEGER NOT NULL,
          pipe       INTEGER DEFAULT 0,
          sensor_x10 INTEGER DEFAULT 0,
          status     INTEGER DEFAULT 0
        );
        CREATE INDEX IF NOT EXISTS idx_load_log_bay_id ON load_log_bay(load_id);

        CREATE TABLE IF NOT EXISTS audit_log (
          id      BIGSERIAL PRIMARY KEY,
          ts      TIMESTAMPTZ NOT NULL DEFAULT NOW(),
          kind    TEXT NOT NULL,
          actor   TEXT NOT NULL,
          slot    INTEGER,
          level   INTEGER DEFAULT 0,
          route   TEXT DEFAULT '',
          detail  TEXT DEFAULT '',
          ip      TEXT
        );
        CREATE INDEX IF NOT EXISTS idx_audit_log_ts    ON audit_log(ts);
        CREATE INDEX IF NOT EXISTS idx_audit_log_actor ON audit_log(actor, ts);
      `);
      console.log(`[LogDB] schema ready (PG_DSN=${PG_DSN.replace(/:[^@/]*@/, ':***@')})`);
    } finally {
      client.release();
    }
  }

  // ═══════════════════════════════════════════════════════════════════
  // Insert operations
  // ═══════════════════════════════════════════════════════════════════
  //
  // Public insert methods are spool-wrapped: they try the live pg path,
  // and on failure (pg down, schema migrating, etc.) push the record
  // onto the on-disk NDJSON spool. A successful live insert also kicks
  // a one-shot drain so any spooled rows from a previous outage replay.
  // The actual SQL lives in insertXDirect() helpers below; the spool's
  // replay handlers call the same helpers so spooled rows take the
  // exact same code path as live rows once pg comes back.

  /** Insert a user log record + its sensor children atomically. */
  async insertUserLog(record: InsertUserLogRecord): Promise<number> {
    try {
      const id = await this.insertUserLogDirect(record);
      void this.spool.drain('user_log');
      return id;
    } catch (err) {
      void this.spool.append('user_log', record);
      throw err;
    }
  }

  async insertActivityEvent(event: InsertActivityRecord): Promise<void> {
    try {
      await this.insertActivityEventDirect(event);
      void this.spool.drain('activity_log');
    } catch (err) {
      void this.spool.append('activity_log', event);
      throw err;
    }
  }

  async insertPidLog(record: InsertPidLogRecord): Promise<void> {
    try {
      await this.insertPidLogDirect(record);
      void this.spool.drain('pid_log');
    } catch (err) {
      void this.spool.append('pid_log', record);
      throw err;
    }
  }

  async insertLoadLog(record: InsertLoadLogRecord): Promise<number> {
    try {
      const id = await this.insertLoadLogDirect(record);
      void this.spool.drain('load_log');
      return id;
    } catch (err) {
      void this.spool.append('load_log', record);
      throw err;
    }
  }

  private async insertUserLogDirect(record: InsertUserLogRecord): Promise<number> {
    await this.ready;
    const client = await this.pool.connect();
    try {
      await client.query('BEGIN');
      const ts = this.dateTimeToIso(record.date, record.time);
      const ins = await client.query<{ id: string }>(
        `INSERT INTO user_log
           (date, time, ts, plenum_temp_sp, plenum_temp, cool_avl_temp,
            plenum_humid_sp, mode, fan_speed, cool_output, refrig_output,
            burner_output, calc_humid, fan_runtime_min, co2_level)
         VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15)
         RETURNING id`,
        [
          record.date, record.time, ts,
          record.plenum_temp_sp, record.plenum_temp, record.cool_avl_temp,
          record.plenum_humid_sp, record.mode, record.fan_speed,
          record.cool_output, record.refrig_output, record.burner_output,
          record.calc_humid, record.fan_runtime_min, record.co2_level,
        ],
      );
      const logId = Number(ins.rows[0].id);

      for (const s of record.temps ?? []) {
        await client.query(
          `INSERT INTO sensor_log (log_id, sensor_id, sensor_type, value)
           VALUES ($1, $2, 'temp', $3)`,
          [logId, s.sensor_id, s.value],
        );
      }
      for (const s of record.humids ?? []) {
        await client.query(
          `INSERT INTO sensor_log (log_id, sensor_id, sensor_type, value)
           VALUES ($1, $2, 'humid', $3)`,
          [logId, s.sensor_id, s.value],
        );
      }

      await client.query('COMMIT');
      return logId;
    } catch (err) {
      await client.query('ROLLBACK').catch(() => {});
      throw err;
    } finally {
      client.release();
    }
  }

  private async insertActivityEventDirect(event: InsertActivityRecord): Promise<void> {
    await this.ready;
    // pg's UTF-8 text type rejects NUL (0x00) bytes; firmware-built strings
    // sometimes include trailing fixed-buffer padding. Strip embedded NULs
    // (and trim) so well-formed text always lands.
    const date        = String(event.date        ?? '').replace(/\u0000/g, '').trim();
    const time        = String(event.time        ?? '').replace(/\u0000/g, '').trim();
    const description = String(event.description ?? '').replace(/\u0000/g, '').trim();
    const ts = this.dateTimeToIso(date, time);
    await this.pool.query(
      `INSERT INTO activity_log
         (date, time, ts, event_type, eq_index, description, new_state, mode)
       VALUES ($1,$2,$3,$4,$5,$6,$7,$8)`,
      [
        date, time, ts,
        event.event_type, event.eq_index, description,
        event.new_state, event.mode,
      ],
    );
  }

  private async insertPidLogDirect(record: InsertPidLogRecord): Promise<void> {
    await this.ready;
    const ts = this.dateTimeToIso(record.date, record.time);
    await this.pool.query(
      `INSERT INTO pid_log
         (date, time, ts, loop_index, p_term, i_term, d_term, error, output, sequence)
       VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10)`,
      [
        record.date, record.time, ts,
        record.loop_index, record.p_term, record.i_term, record.d_term,
        record.error, record.output, record.sequence,
      ],
    );
  }

  private async insertLoadLogDirect(record: InsertLoadLogRecord): Promise<number> {
    await this.ready;
    const client = await this.pool.connect();
    try {
      await client.query('BEGIN');
      const ts = this.dateTimeToIso(record.date, record.time);
      const ins = await client.query<{ id: string }>(
        `INSERT INTO load_log (date, time, ts, record_num, sequence)
         VALUES ($1,$2,$3,$4,$5) RETURNING id`,
        [record.date, record.time, ts, record.record_num, record.sequence],
      );
      const loadId = Number(ins.rows[0].id);
      for (let i = 0; i < record.bays.length; i++) {
        const b = record.bays[i];
        await client.query(
          `INSERT INTO load_log_bay (load_id, bay_index, pipe, sensor_x10, status)
           VALUES ($1,$2,$3,$4,$5)`,
          [loadId, i, b.pipe, b.sensor_x10, b.status],
        );
      }
      await client.query('COMMIT');
      return loadId;
    } catch (err) {
      await client.query('ROLLBACK').catch(() => {});
      throw err;
    } finally {
      client.release();
    }
  }

  /**
   * Append an audit-log entry. Used for save accountability — every
   * /proto/write/:field call records who, what, and where.
   * Server timestamp (NOW()) — independent of firmware RTC drift.
   */
  async insertAudit(rec: InsertAuditRecord): Promise<void> {
    await this.ready;
    await this.pool.query(
      `INSERT INTO audit_log (kind, actor, slot, level, route, detail, ip)
       VALUES ($1,$2,$3,$4,$5,$6,$7)`,
      [
        rec.kind, rec.actor, rec.slot, rec.level,
        rec.route, rec.detail, rec.ip ?? null,
      ],
    );
  }

  // ═══════════════════════════════════════════════════════════════════
  // Query operations
  // ═══════════════════════════════════════════════════════════════════

  async getUserLogByDateRange(startDate: string, endDate: string): Promise<UserLogRow[]> {
    await this.ready;
    const startIso = this.dateToIso(startDate);
    const endIso = this.dateToIso(endDate, true);
    const r = await this.pool.query<UserLogRow>(
      `SELECT * FROM user_log WHERE ts >= $1 AND ts <= $2 ORDER BY ts ASC`,
      [startIso, endIso],
    );
    return this.normalizeRows(r.rows);
  }

  async getSensorLogByLogIds(logIds: number[], sensorType?: string): Promise<SensorLogRow[]> {
    await this.ready;
    if (logIds.length === 0) return [];
    const params: any[] = [logIds];
    let sql = `SELECT log_id, sensor_id, sensor_type, value
                 FROM sensor_log
                WHERE log_id = ANY($1::bigint[])`;
    if (sensorType) {
      params.push(sensorType);
      sql += ` AND sensor_type = $2`;
    }
    sql += ` ORDER BY log_id, sensor_id`;
    const r = await this.pool.query<SensorLogRow>(sql, params);
    // pg returns bigint as string for log_id; cast to number for callers.
    return r.rows.map(row => ({ ...row, log_id: Number(row.log_id) }));
  }

  async getActivityLogByRange(startRec: number, endRec: number): Promise<ActivityLogRow[]> {
    await this.ready;
    const limit = Math.max(endRec - startRec + 1, 1);
    const offset = Math.max(startRec - 1, 0);
    const r = await this.pool.query<ActivityLogRow>(
      `SELECT * FROM activity_log ORDER BY ts DESC LIMIT $1 OFFSET $2`,
      [limit, offset],
    );
    return this.normalizeRows(r.rows);
  }

  async getActivityLogByDateRange(startDate: string, endDate: string): Promise<ActivityLogRow[]> {
    await this.ready;
    const startIso = this.dateToIso(startDate);
    const endIso = this.dateToIso(endDate, true);
    const r = await this.pool.query<ActivityLogRow>(
      `SELECT * FROM activity_log WHERE ts >= $1 AND ts <= $2 ORDER BY ts ASC`,
      [startIso, endIso],
    );
    return this.normalizeRows(r.rows);
  }

  async getUserLogCount(): Promise<number> {
    await this.ready;
    const r = await this.pool.query<{ cnt: string }>('SELECT COUNT(*)::text as cnt FROM user_log');
    return Number(r.rows[0].cnt);
  }

  async getActivityLogCount(): Promise<number> {
    await this.ready;
    const r = await this.pool.query<{ cnt: string }>('SELECT COUNT(*)::text as cnt FROM activity_log');
    return Number(r.rows[0].cnt);
  }

  async getPidLogByDateRange(
    startDate: string, endDate: string, loopIndex?: number,
  ): Promise<PidLogRow[]> {
    await this.ready;
    const startIso = this.dateToIso(startDate);
    const endIso = this.dateToIso(endDate, true);
    if (loopIndex !== undefined) {
      const r = await this.pool.query<PidLogRow>(
        `SELECT * FROM pid_log WHERE ts >= $1 AND ts <= $2 AND loop_index = $3
          ORDER BY ts ASC`,
        [startIso, endIso, loopIndex],
      );
      return this.normalizeRows(r.rows);
    }
    const r = await this.pool.query<PidLogRow>(
      `SELECT * FROM pid_log WHERE ts >= $1 AND ts <= $2 ORDER BY ts ASC`,
      [startIso, endIso],
    );
    return this.normalizeRows(r.rows);
  }

  async getPidLogCount(loopIndex?: number): Promise<number> {
    await this.ready;
    if (loopIndex !== undefined) {
      const r = await this.pool.query<{ cnt: string }>(
        'SELECT COUNT(*)::text as cnt FROM pid_log WHERE loop_index = $1',
        [loopIndex],
      );
      return Number(r.rows[0].cnt);
    }
    const r = await this.pool.query<{ cnt: string }>('SELECT COUNT(*)::text as cnt FROM pid_log');
    return Number(r.rows[0].cnt);
  }

  async getLoadLogByDateRange(startDate: string, endDate: string): Promise<LoadLogRow[]> {
    await this.ready;
    const startIso = this.dateToIso(startDate);
    const endIso = this.dateToIso(endDate, true);
    const r = await this.pool.query<LoadLogRow>(
      `SELECT * FROM load_log WHERE ts >= $1 AND ts <= $2 ORDER BY ts ASC`,
      [startIso, endIso],
    );
    return this.normalizeRows(r.rows);
  }

  async getLoadLogBays(loadIds: number[]): Promise<LoadLogBayRow[]> {
    await this.ready;
    if (loadIds.length === 0) return [];
    const r = await this.pool.query<LoadLogBayRow>(
      `SELECT load_id, bay_index, pipe, sensor_x10, status
         FROM load_log_bay
        WHERE load_id = ANY($1::bigint[])
        ORDER BY load_id, bay_index`,
      [loadIds],
    );
    return r.rows.map(row => ({ ...row, load_id: Number(row.load_id) }));
  }

  async getLoadLogCount(): Promise<number> {
    await this.ready;
    const r = await this.pool.query<{ cnt: string }>('SELECT COUNT(*)::text as cnt FROM load_log');
    return Number(r.rows[0].cnt);
  }

  async getRecentActivity(count: number, eventTypes?: number[]): Promise<ActivityLogRow[]> {
    await this.ready;
    if (eventTypes && eventTypes.length > 0) {
      const r = await this.pool.query<ActivityLogRow>(
        `SELECT * FROM activity_log WHERE event_type = ANY($1::int[])
          ORDER BY ts DESC LIMIT $2`,
        [eventTypes, count],
      );
      return this.normalizeRows(r.rows);
    }
    const r = await this.pool.query<ActivityLogRow>(
      `SELECT * FROM activity_log ORDER BY ts DESC LIMIT $1`,
      [count],
    );
    return this.normalizeRows(r.rows);
  }

  /** Recent audit entries (newest first). */
  async getRecentAudit(count: number): Promise<AuditLogRow[]> {
    await this.ready;
    const r = await this.pool.query<AuditLogRow>(
      `SELECT id, ts, kind, actor, slot, level, route, detail, ip
         FROM audit_log ORDER BY ts DESC LIMIT $1`,
      [count],
    );
    return r.rows.map(row => ({ ...row, id: Number(row.id), ts: this.tsToIso(row.ts) }));
  }

  async close(): Promise<void> {
    if (this.retentionTimer) {
      clearInterval(this.retentionTimer);
      this.retentionTimer = null;
    }
    await this.pool.end();
  }

  // ═══════════════════════════════════════════════════════════════════
  // Clear / truncate operations
  // ═══════════════════════════════════════════════════════════════════
  //
  // Mirror the legacy AS2 *LogReset() entry points. On AS2 these wiped
  // the SD-card ring buffer; on Constellation the rows live in pg on
  // the rpi5, so the clear is a TRUNCATE … RESTART IDENTITY CASCADE
  // (CASCADE picks up sensor_log rows that FK back to user_log, and
  // load_log_bay rows that FK back to load_log).

  /** Wipe every history-log entry. Cascades to sensor_log. */
  async clearUserLog(): Promise<void> {
    await this.ready;
    await this.pool.query('TRUNCATE TABLE user_log RESTART IDENTITY CASCADE');
  }

  /** Wipe every activity-log entry (warnings, equipment events). */
  async clearActivityLog(): Promise<void> {
    await this.ready;
    await this.pool.query('TRUNCATE TABLE activity_log RESTART IDENTITY');
  }

  /** Wipe every PID-loop telemetry entry. */
  async clearPidLog(): Promise<void> {
    await this.ready;
    await this.pool.query('TRUNCATE TABLE pid_log RESTART IDENTITY');
  }

  // ═══════════════════════════════════════════════════════════════════
  // Helpers
  // ═══════════════════════════════════════════════════════════════════

  /**
   * Normalize pg row IDs (returned as bigint strings) to numbers, and
   * convert the TIMESTAMPTZ `ts` field (Date object) to an ISO string
   * for back-compat with the prior SQLite TEXT representation.
   */
  private normalizeRows<T extends { id?: number | string; ts?: any }>(rows: T[]): T[] {
    return rows.map(r => {
      const out: any = { ...r };
      if (r.id !== undefined) out.id = Number(r.id);
      if (r.ts !== undefined) out.ts = this.tsToIso(r.ts);
      return out;
    });
  }

  private tsToIso(ts: any): string {
    if (ts instanceof Date) return ts.toISOString();
    return String(ts ?? '');
  }

  private dateTimeToIso(date: string, time: string): string {
    const parts = date.split('/');
    if (parts.length !== 3) return new Date().toISOString();
    const [mm, dd, yyyy] = parts;
    return `${yyyy}-${mm.padStart(2, '0')}-${dd.padStart(2, '0')}T${time || '00:00:00'}`;
  }

  private dateToIso(date: string, endOfDay = false): string {
    const parts = date.split('/');
    if (parts.length !== 3) {
      return endOfDay ? '9999-12-31T23:59:59' : '0001-01-01T00:00:00';
    }
    const [mm, dd, yyyy] = parts;
    const isoDate = `${yyyy}-${mm.padStart(2, '0')}-${dd.padStart(2, '0')}`;
    return endOfDay ? `${isoDate}T23:59:59` : `${isoDate}T00:00:00`;
  }
}
