/**
 * logDatabase.ts — SQLite log storage for Constellation
 *
 * Replaces the SD card. The firmware pushes LogRecord and ActivityEvent
 * messages via protobuf; this module stores them persistently in SQLite
 * on the RPi5. The UI queries logs through the NovaLogDataStore which
 * reads from this database.
 *
 * Tables:
 *   user_log     — periodic sensor/process snapshots (graph/table views)
 *   activity_log — equipment state changes, warnings, mode changes
 *   sensor_log   — per-record temp/humid sensor values (child of user_log)
 */

import Database from 'better-sqlite3';
import path from 'path';
import fs from 'fs';

const DB_DIR = process.env.LOG_DB_DIR ?? path.join(process.cwd(), 'data');
const DB_FILE = path.join(DB_DIR, 'constellation_logs.db');

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

export interface UserLogRow {
  id: number;
  date: string;        // MM/DD/YYYY
  time: string;        // HH:MM:SS
  ts: string;          // ISO-8601 for sorting
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
  sensor_type: string;   // 'temp' | 'humid'
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

export class LogDatabase {
  private db: Database.Database;

  /* Prepared statements (lazily initialized after schema creation) */
  private stmtInsertUserLog!: Database.Statement;
  private stmtInsertSensorLog!: Database.Statement;
  private stmtInsertActivity!: Database.Statement;
  private insertUserLogTxn!: Database.Transaction<(record: InsertUserLogRecord) => number>;

  constructor() {
    fs.mkdirSync(DB_DIR, { recursive: true });
    this.db = new Database(DB_FILE);
    this.db.pragma('journal_mode = WAL');
    this.db.pragma('synchronous = FULL');
    this.createSchema();
    this.prepareStatements();
  }

  private createSchema(): void {
    this.db.exec(`
      CREATE TABLE IF NOT EXISTS user_log (
        id              INTEGER PRIMARY KEY AUTOINCREMENT,
        date            TEXT NOT NULL,
        time            TEXT NOT NULL,
        ts              TEXT NOT NULL,
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

      CREATE INDEX IF NOT EXISTS idx_user_log_ts ON user_log(ts);
      CREATE INDEX IF NOT EXISTS idx_user_log_date ON user_log(date);

      CREATE TABLE IF NOT EXISTS sensor_log (
        log_id      INTEGER NOT NULL,
        sensor_id   INTEGER NOT NULL,
        sensor_type TEXT NOT NULL,
        value       REAL NOT NULL,
        FOREIGN KEY (log_id) REFERENCES user_log(id)
      );

      CREATE INDEX IF NOT EXISTS idx_sensor_log_id ON sensor_log(log_id);

      CREATE TABLE IF NOT EXISTS activity_log (
        id          INTEGER PRIMARY KEY AUTOINCREMENT,
        date        TEXT NOT NULL,
        time        TEXT NOT NULL,
        ts          TEXT NOT NULL,
        event_type  INTEGER DEFAULT 0,
        eq_index    INTEGER DEFAULT 0,
        description TEXT DEFAULT '',
        new_state   INTEGER DEFAULT 0,
        mode        INTEGER DEFAULT 0
      );

      CREATE INDEX IF NOT EXISTS idx_activity_log_ts ON activity_log(ts);
      CREATE INDEX IF NOT EXISTS idx_activity_log_date ON activity_log(date);
    `);
  }

  private prepareStatements(): void {
    this.stmtInsertUserLog = this.db.prepare(`
      INSERT INTO user_log (date, time, ts, plenum_temp_sp, plenum_temp,
        cool_avl_temp, plenum_humid_sp, mode, fan_speed, cool_output,
        refrig_output, burner_output, calc_humid, fan_runtime_min, co2_level)
      VALUES (@date, @time, @ts, @plenum_temp_sp, @plenum_temp,
        @cool_avl_temp, @plenum_humid_sp, @mode, @fan_speed, @cool_output,
        @refrig_output, @burner_output, @calc_humid, @fan_runtime_min, @co2_level)
    `);

    this.stmtInsertSensorLog = this.db.prepare(`
      INSERT INTO sensor_log (log_id, sensor_id, sensor_type, value)
      VALUES (@log_id, @sensor_id, @sensor_type, @value)
    `);

    this.stmtInsertActivity = this.db.prepare(`
      INSERT INTO activity_log (date, time, ts, event_type, eq_index,
        description, new_state, mode)
      VALUES (@date, @time, @ts, @event_type, @eq_index,
        @description, @new_state, @mode)
    `);

    this.insertUserLogTxn = this.db.transaction((record: InsertUserLogRecord): number => {
      const ts = this.dateTimeToIso(record.date, record.time);

      const result = this.stmtInsertUserLog.run({
        date: record.date,
        time: record.time,
        ts,
        plenum_temp_sp: record.plenum_temp_sp,
        plenum_temp: record.plenum_temp,
        cool_avl_temp: record.cool_avl_temp,
        plenum_humid_sp: record.plenum_humid_sp,
        mode: record.mode,
        fan_speed: record.fan_speed,
        cool_output: record.cool_output,
        refrig_output: record.refrig_output,
        burner_output: record.burner_output,
        calc_humid: record.calc_humid,
        fan_runtime_min: record.fan_runtime_min,
        co2_level: record.co2_level,
      });

      const logId = Number(result.lastInsertRowid);

      if (record.temps) {
        for (const s of record.temps) {
          this.stmtInsertSensorLog.run({
            log_id: logId,
            sensor_id: s.sensor_id,
            sensor_type: 'temp',
            value: s.value,
          });
        }
      }
      if (record.humids) {
        for (const s of record.humids) {
          this.stmtInsertSensorLog.run({
            log_id: logId,
            sensor_id: s.sensor_id,
            sensor_type: 'humid',
            value: s.value,
          });
        }
      }

      return logId;
    });
  }

  // ═══════════════════════════════════════════════════════════════════
  // Insert operations
  // ═══════════════════════════════════════════════════════════════════

  /**
   * Insert a user log record (from firmware LogRecord message).
   * Wrapped in a single transaction so the main record + all sensor values
   * are atomic — no orphaned rows on power loss.
   * Returns the inserted row ID for linking sensor values.
   */
  insertUserLog(record: InsertUserLogRecord): number {
    return this.insertUserLogTxn(record);
  }

  /**
   * Insert an activity log record (from firmware ActivityEvent message).
   */
  insertActivityEvent(event: {
    date: string;
    time: string;
    event_type: number;
    eq_index: number;
    description: string;
    new_state: number;
    mode: number;
  }): void {
    const ts = this.dateTimeToIso(event.date, event.time);
    this.stmtInsertActivity.run({
      date: event.date,
      time: event.time,
      ts,
      event_type: event.event_type,
      eq_index: event.eq_index,
      description: event.description,
      new_state: event.new_state,
      mode: event.mode,
    });
  }

  // ═══════════════════════════════════════════════════════════════════
  // Query operations
  // ═══════════════════════════════════════════════════════════════════

  /**
   * Get user log records for a date range.
   * Dates in MM/DD/YYYY format.
   */
  getUserLogByDateRange(startDate: string, endDate: string): UserLogRow[] {
    const startIso = this.dateToIso(startDate);
    const endIso = this.dateToIso(endDate, true);
    return this.db.prepare(`
      SELECT * FROM user_log WHERE ts >= @start AND ts <= @end ORDER BY ts ASC
    `).all({ start: startIso, end: endIso }) as UserLogRow[];
  }

  /**
   * Get sensor values for a set of user log records.
   */
  getSensorLogByLogIds(logIds: number[], sensorType?: string): SensorLogRow[] {
    if (logIds.length === 0) return [];
    const placeholders = logIds.map(() => '?').join(',');
    let sql = `SELECT * FROM sensor_log WHERE log_id IN (${placeholders})`;
    const params: any[] = [...logIds];
    if (sensorType) {
      sql += ` AND sensor_type = ?`;
      params.push(sensorType);
    }
    sql += ` ORDER BY log_id, sensor_id`;
    return this.db.prepare(sql).all(...params) as SensorLogRow[];
  }

  /**
   * Get activity log records by record range (newest first).
   */
  getActivityLogByRange(startRec: number, endRec: number): ActivityLogRow[] {
    const limit = Math.max(endRec - startRec + 1, 1);
    const offset = Math.max(startRec - 1, 0);
    return this.db.prepare(`
      SELECT * FROM activity_log ORDER BY ts DESC LIMIT @limit OFFSET @offset
    `).all({ limit, offset }) as ActivityLogRow[];
  }

  /**
   * Get total user log record count.
   */
  getUserLogCount(): number {
    const row = this.db.prepare('SELECT COUNT(*) as cnt FROM user_log').get() as { cnt: number };
    return row.cnt;
  }

  /**
   * Get total activity log record count.
   */
  getActivityLogCount(): number {
    const row = this.db.prepare('SELECT COUNT(*) as cnt FROM activity_log').get() as { cnt: number };
    return row.cnt;
  }

  /**
   * Get most recent N activity records (for alarm history).
   */
  getRecentActivity(count: number, eventTypes?: number[]): ActivityLogRow[] {
    if (eventTypes && eventTypes.length > 0) {
      const placeholders = eventTypes.map(() => '?').join(',');
      return this.db.prepare(`
        SELECT * FROM activity_log WHERE event_type IN (${placeholders})
        ORDER BY ts DESC LIMIT ?
      `).all(...eventTypes, count) as ActivityLogRow[];
    }
    return this.db.prepare(`
      SELECT * FROM activity_log ORDER BY ts DESC LIMIT ?
    `).all(count) as ActivityLogRow[];
  }

  /**
   * Close the database.
   */
  close(): void {
    this.db.close();
  }

  // ═══════════════════════════════════════════════════════════════════
  // Helpers
  // ═══════════════════════════════════════════════════════════════════

  /**
   * Convert MM/DD/YYYY + HH:MM:SS to ISO-8601 for sorting.
   */
  private dateTimeToIso(date: string, time: string): string {
    // date: "MM/DD/YYYY", time: "HH:MM:SS"
    const parts = date.split('/');
    if (parts.length !== 3) return new Date().toISOString();
    const [mm, dd, yyyy] = parts;
    return `${yyyy}-${mm.padStart(2, '0')}-${dd.padStart(2, '0')}T${time || '00:00:00'}`;
  }

  /**
   * Convert MM/DD/YYYY to ISO date for range queries.
   * If endOfDay, returns end of that day.
   */
  private dateToIso(date: string, endOfDay = false): string {
    const parts = date.split('/');
    if (parts.length !== 3) return endOfDay ? '9999-12-31T23:59:59' : '0000-01-01T00:00:00';
    const [mm, dd, yyyy] = parts;
    const isoDate = `${yyyy}-${mm.padStart(2, '0')}-${dd.padStart(2, '0')}`;
    return endOfDay ? `${isoDate}T23:59:59` : `${isoDate}T00:00:00`;
  }
}
