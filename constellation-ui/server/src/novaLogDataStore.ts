/**
 * novaLogDataStore.ts â€” NOVA log data store backed by SQLite
 *
 * Architecture: The firmware pushes LogRecord (periodic) and ActivityEvent
 * (on-change) protobufs. This module decodes them, stores them in SQLite
 * via LogDatabase, and serves the same public API that the UI's apiRoutes
 * expects (getUserDates, getUserSensorData, getActivityDates, etc.).
 *
 * There is no SD card. The RPi5 is the single source of truth for all logs.
 */

import type { NovaSerialBridge } from './novaSerialBridge.js';
import type { DataCache } from './dataCache.js';
import { LogDatabase } from './logDatabase.js';
import { statfs } from 'fs/promises';

// â”€â”€ Re-export interfaces for API route compatibility â”€â”€

export type LogDeviceType = 'nova' | 'orbit' | 'triton';

export interface LogSource {
  type: LogDeviceType;
  id: number;
  label: string;
  connected: boolean;
}

// â”€â”€ Protobuf field numbers â”€â”€
// LogRecord: date(1), time(2), plenum_temp_sp(3), plenum_temp(4),
//   cool_avl_temp(5), plenum_humid_sp(6), mode(7), fan_speed(8),
//   cool_output(9), refrig_output(10), burner_output(11), calc_humid(12),
//   fan_runtime_min(13), co2_level(14), temps(15 repeated), humids(16 repeated)
//
// ActivityEvent: date(1), time(2), event_type(3), eq_index(4),
//   description(5), new_state(6), mode(7)
//
// SensorValue: sensor_id(1), value(2)

const DEFAULT_SOURCE = 'nova-1';

// â”€â”€ QueryTag names matching legacy system â”€â”€
const QUERY_TAG_NAMES = [
  'hsPlenTmpSet', 'hsPlenTmp', 'hsCoolAvl', 'hsPlenHumSet', 'hsMode',
  'hsFanSpd', 'hsCoolOut', 'hsRefOut', 'hsBurnOut', 'hsCalcHum', 'hsFanRun',
] as const;

/**
 * NOVA log data store backed by SQLite on the RPi5.
 */
export class NovaLogDataStore {
  private bridge: NovaSerialBridge;
  private dataCache: DataCache;
  /**
   * Public so audit hooks (e.g. /proto/write/:field in index.ts) can
   * call insertAudit() directly without going through a wrapper method.
   */
  readonly logDb: LogDatabase;
  /** Public read-only handle for direct SQLite queries (CSV exports etc).
   *  Writes still go through the typed insert* methods on this class so
   *  the gap-detection counters stay consistent. */
  public get db(): LogDatabase { return this.logDb; }
  private sources = new Map<string, LogSource>();

  /** Graph favorites stored locally */
  private graphFavorites = '';

  /** Last seen LogRecord sequence number — used to detect gaps */
  private lastSeq = -1;

  /** Last seen PidLogRecord sequence per loop_index — gap detection */
  private lastPidSeq = new Map<number, number>();

  /** Last seen LoadLogRecord sequence — gap detection */
  private lastLoadLogSeq = -1;

  /** VFD alarm event log */
  private vfdAlarmLog: Array<{ date: string; time: string; text: string; status: 'ON' | 'OFF' }> = [];
  private static readonly MAX_VFD_LOG = 500;

  constructor(bridge: NovaSerialBridge, dataCache: DataCache) {
    this.bridge = bridge;
    this.dataCache = dataCache;
    this.logDb = new LogDatabase();

    this.registerSource({
      type: 'nova',
      id: 1,
      label: 'Main Controller',
      connected: true,
    });

    // Listen for LogRecord messages from firmware
    bridge.on('LogRecord', (data: Buffer) => {
      this.handleLogRecord(data);
    });

    // Listen for ActivityEvent messages from firmware
    bridge.on('ActivityEvent', (data: Buffer) => {
      this.handleActivityEvent(data);
    });

    // Listen for PidLogRecord messages from firmware (replaces SD-card PIDLog)
    bridge.on('PidLogRecord', (data: Buffer) => {
      this.handlePidLogRecord(data);
    });

    // Listen for LoadLogRecord messages from firmware (replaces SD-card LoadLog)
    bridge.on('LoadLogRecord', (data: Buffer) => {
      this.handleLoadLogRecord(data);
    });

    console.log('[NovaLogStore] PostgreSQL log store initialized');
  }

  // â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
  // Source management
  // â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

  registerSource(source: LogSource): void {
    const key = `${source.type}-${source.id}`;
    this.sources.set(key, source);
  }

  unregisterSource(sourceKey: string): void {
    this.sources.delete(sourceKey);
  }

  setSourceConnected(sourceKey: string, connected: boolean): void {
    const source = this.sources.get(sourceKey);
    if (source) source.connected = connected;
  }

  // (S9k cleanup 2026-04-21: getLogSources() removed — only consumer was
  // the deleted GET /iot/log/sources route.)

  // â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
  // Protobuf decoders
  // â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

  private handleLogRecord(raw: Buffer): void {
    try {
      const rec = this.decodeLogRecord(raw);
      if (!rec) return;

      // Sequence gap detection
      if (this.lastSeq >= 0 && rec.sequence > 0) {
        const gap = rec.sequence - this.lastSeq - 1;
        if (gap > 0) {
          console.warn(`[NovaLogStore] Sequence gap: expected ${this.lastSeq + 1}, got ${rec.sequence} (${gap} record(s) lost)`);
        }
      }
      if (rec.sequence > 0 || this.lastSeq < 0) {
        this.lastSeq = rec.sequence;
      }

      void this.logDb.insertUserLog(rec)
        .then(() => console.log(`[NovaLogStore] LogRecord stored: ${rec.date} ${rec.time} seq=${rec.sequence}`))
        .catch(err => console.error('[NovaLogStore] LogRecord pg insert error:', err.message));
    } catch (err: any) {
      console.error('[NovaLogStore] LogRecord insert error:', err.message);
    }
  }

  private handleActivityEvent(raw: Buffer): void {
    try {
      const evt = this.decodeActivityEvent(raw);
      if (!evt) return;

      void this.logDb.insertActivityEvent(evt)
        .then(() => console.log(`[NovaLogStore] ActivityEvent: type=${evt.event_type} eq=${evt.eq_index} "${evt.description}" state=${evt.new_state}`))
        .catch(err => console.error('[NovaLogStore] ActivityEvent pg insert error:', err.message));
    } catch (err: any) {
      console.error('[NovaLogStore] ActivityEvent insert error:', err.message);
    }
  }

  private decodeLogRecord(raw: Buffer): {
    date: string; time: string;
    plenum_temp_sp: number; plenum_temp: number; cool_avl_temp: number;
    plenum_humid_sp: number; mode: number; fan_speed: number;
    cool_output: number; refrig_output: number; burner_output: number;
    calc_humid: number; fan_runtime_min: number; co2_level: number;
    temps: Array<{ sensor_id: number; value: number }>;
    humids: Array<{ sensor_id: number; value: number }>;
    sequence: number;
  } | null {
    const fields = this.pbDecode(raw);
    const temps: Array<{ sensor_id: number; value: number }> = [];
    const humids: Array<{ sensor_id: number; value: number }> = [];

    // Parse repeated SensorValue submessages
    for (const f of fields) {
      if (f.field === 15 && f.wireType === 2) {
        const sv = this.decodeSensorValue(f.value as Buffer);
        if (sv) temps.push(sv);
      }
      if (f.field === 16 && f.wireType === 2) {
        const sv = this.decodeSensorValue(f.value as Buffer);
        if (sv) humids.push(sv);
      }
    }

    return {
      date: this.pbString(fields, 1),
      time: this.pbString(fields, 2),
      plenum_temp_sp: this.pbFloat(fields, 3),
      plenum_temp: this.pbFloat(fields, 4),
      cool_avl_temp: this.pbFloat(fields, 5),
      plenum_humid_sp: this.pbFloat(fields, 6),
      mode: this.pbVarint(fields, 7),
      fan_speed: this.pbVarint(fields, 8),
      cool_output: this.pbVarint(fields, 9),
      refrig_output: this.pbVarint(fields, 10),
      burner_output: this.pbVarint(fields, 11),
      calc_humid: this.pbFloat(fields, 12),
      fan_runtime_min: this.pbVarint(fields, 13),
      co2_level: this.pbFloat(fields, 14),
      temps,
      humids,
      sequence: this.pbVarint(fields, 17),
    };
  }

  private decodeSensorValue(raw: Buffer): { sensor_id: number; value: number } | null {
    const fields = this.pbDecode(raw);
    return {
      sensor_id: this.pbVarint(fields, 1),
      value: this.pbFloat(fields, 2),
    };
  }

  private decodeActivityEvent(raw: Buffer): {
    date: string; time: string; event_type: number;
    eq_index: number; description: string; new_state: number; mode: number;
  } | null {
    const fields = this.pbDecode(raw);
    return {
      date: this.pbString(fields, 1),
      time: this.pbString(fields, 2),
      event_type: this.pbVarint(fields, 3),
      eq_index: this.pbVarint(fields, 4),
      description: this.pbString(fields, 5),
      new_state: this.pbVarint(fields, 6),
      mode: this.pbVarint(fields, 7),
    };
  }

  private handlePidLogRecord(raw: Buffer): void {
    try {
      const rec = this.decodePidLogRecord(raw);
      if (!rec) return;

      // Per-loop sequence-gap detection
      const prev = this.lastPidSeq.get(rec.loop_index);
      if (prev !== undefined && rec.sequence > 0) {
        const gap = rec.sequence - prev - 1;
        if (gap > 0) {
          console.warn(`[NovaLogStore] PID loop=${rec.loop_index} sequence gap: expected ${prev + 1}, got ${rec.sequence} (${gap} record(s) lost)`);
        }
      }
      if (rec.sequence > 0 || prev === undefined) {
        this.lastPidSeq.set(rec.loop_index, rec.sequence);
      }

      void this.logDb.insertPidLog(rec)
        .catch(err => console.error('[NovaLogStore] PidLog pg insert error:', err.message));
    } catch (err: any) {
      console.error('[NovaLogStore] PidLogRecord insert error:', err.message);
    }
  }

  private decodePidLogRecord(raw: Buffer): {
    date: string; time: string; loop_index: number;
    p_term: number; i_term: number; d_term: number;
    error: number; output: number; sequence: number;
  } | null {
    const fields = this.pbDecode(raw);
    // output (field 8) is wire-encoded as a varint of two's-complement
    // uint32 form by the firmware; convert back to signed int32.
    const outU = this.pbVarint(fields, 8);
    const outS = outU >= 0x80000000 ? outU - 0x100000000 : outU;
    return {
      date: this.pbString(fields, 1),
      time: this.pbString(fields, 2),
      loop_index: this.pbVarint(fields, 3),
      p_term: this.pbFloat(fields, 4),
      i_term: this.pbFloat(fields, 5),
      d_term: this.pbFloat(fields, 6),
      error: this.pbFloat(fields, 7),
      output: outS,
      sequence: this.pbVarint(fields, 9),
    };
  }

  private handleLoadLogRecord(raw: Buffer): void {
    try {
      const rec = this.decodeLoadLogRecord(raw);
      if (!rec) return;

      if (this.lastLoadLogSeq >= 0 && rec.sequence > 0) {
        const gap = rec.sequence - this.lastLoadLogSeq - 1;
        if (gap > 0) {
          console.warn(`[NovaLogStore] LoadLog sequence gap: expected ${this.lastLoadLogSeq + 1}, got ${rec.sequence} (${gap} record(s) lost)`);
        }
      }
      if (rec.sequence > 0 || this.lastLoadLogSeq < 0) {
        this.lastLoadLogSeq = rec.sequence;
      }

      void this.logDb.insertLoadLog(rec)
        .then(() => console.log(`[NovaLogStore] LoadLogRecord stored: ${rec.date} ${rec.time} rec=${rec.record_num} bays=${rec.bays.length}`))
        .catch(err => console.error('[NovaLogStore] LoadLog pg insert error:', err.message));
    } catch (err: any) {
      console.error('[NovaLogStore] LoadLogRecord insert error:', err.message);
    }
  }

  private decodeLoadLogRecord(raw: Buffer): {
    date: string; time: string; record_num: number; sequence: number;
    bays: Array<{ pipe: number; sensor_x10: number; status: number }>;
  } | null {
    const fields = this.pbDecode(raw);
    const bays: Array<{ pipe: number; sensor_x10: number; status: number }> = [];
    for (const f of fields) {
      if (f.field === 5 && f.wireType === 2) {
        const sub = this.pbDecode(f.value as Buffer);
        const sensorU = this.pbVarint(sub, 2);
        const sensorS = sensorU >= 0x80000000 ? sensorU - 0x100000000 : sensorU;
        bays.push({
          pipe: this.pbVarint(sub, 1),
          sensor_x10: sensorS,
          status: this.pbVarint(sub, 3),
        });
      }
    }
    return {
      date: this.pbString(fields, 1),
      time: this.pbString(fields, 2),
      record_num: this.pbVarint(fields, 3),
      sequence: this.pbVarint(fields, 4),
      bays,
    };
  }

  // â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
  // User log endpoints (same interface as LogDataStore)
  // â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

  async getUserDates(startDate: string, endDate: string, _source?: string): Promise<string[]> {
    const start10 = startDate.slice(0, 10);
    const end10 = endDate.slice(0, 10);
    const rows = await this.logDb.getUserLogByDateRange(start10, end10);
    return rows.map(r => `${r.date} ${r.time}`);
  }

  async getUserSensorData(
    sensorId: string,
    startDate: string,
    endDate: string,
    _source?: string,
  ): Promise<string[]> {
    const start10 = startDate.slice(0, 10);
    const end10 = endDate.slice(0, 10);
    const rows = await this.logDb.getUserLogByDateRange(start10, end10);

    // Map sensorId to column
    const colIndex = (QUERY_TAG_NAMES as readonly string[]).indexOf(sensorId);
    if (colIndex >= 0) {
      return rows.map(r => {
        switch (colIndex) {
          case 0: return String(r.plenum_temp_sp);
          case 1: return String(r.plenum_temp);
          case 2: return String(r.cool_avl_temp);
          case 3: return String(r.plenum_humid_sp);
          case 4: return String(r.mode);
          case 5: return String(r.fan_speed);
          case 6: return String(r.cool_output);
          case 7: return String(r.refrig_output);
          case 8: return String(r.burner_output);
          case 9: return String(r.calc_humid);
          case 10: return String(r.fan_runtime_min);
          default: return '0';
        }
      });
    }

    // CO2
    if (sensorId === 'csen7') {
      return rows.map(r => String(r.co2_level));
    }

    // Temp sensors (tsenN)
    if (sensorId.startsWith('tsen')) {
      const sid = parseInt(sensorId.slice(4), 10);
      const logIds = rows.map(r => r.id);
      const sensorRows = await this.logDb.getSensorLogByLogIds(logIds, 'temp');
      // Build a map: logId â†’ value for this sensor
      const valMap = new Map<number, number>();
      for (const sr of sensorRows) {
        if (sr.sensor_id === sid) valMap.set(sr.log_id, sr.value);
      }
      return rows.map(r => String(valMap.get(r.id) ?? 0));
    }

    // Humid sensors (hsenN)
    if (sensorId.startsWith('hsen')) {
      const sid = parseInt(sensorId.slice(4), 10);
      const logIds = rows.map(r => r.id);
      const sensorRows = await this.logDb.getSensorLogByLogIds(logIds, 'humid');
      const valMap = new Map<number, number>();
      for (const sr of sensorRows) {
        if (sr.sensor_id === sid) valMap.set(sr.log_id, sr.value);
      }
      return rows.map(r => String(valMap.get(r.id) ?? 0));
    }

    // Moisture loss â€” not tracked
    if (sensorId === 'mi1' || sensorId === 'mi2') {
      return rows.map(() => '0');
    }

    return rows.map(() => '0');
  }

  // â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
  // Activity log endpoints
  // â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

  async getActivityDates(startRec: string, endRec: string, _source?: string): Promise<string[]> {
    const start = parseInt(startRec, 10) || 1;
    const end = parseInt(endRec, 10) || 100;
    const rows = await this.logDb.getActivityLogByRange(start, end);
    return rows.map(r => `${r.date} ${r.time}`);
  }

  /**
   * Return PID log records for the level-2 PID-tuning page.
   * loopIndex undefined = all loops; otherwise filter to that loop only.
   * Replaces the legacy SD-card PIDLog query path.
   */
  async getPidLogRecords(startDate: string, endDate: string, loopIndex?: number): Promise<Array<{
    date: string; time: string; loop_index: number;
    p_term: number; i_term: number; d_term: number;
    error: number; output: number; sequence: number;
  }>> {
    const rows = await this.logDb.getPidLogByDateRange(startDate, endDate, loopIndex);
    return rows.map(r => ({
      date: r.date,
      time: r.time,
      loop_index: r.loop_index,
      p_term: r.p_term,
      i_term: r.i_term,
      d_term: r.d_term,
      error: r.error,
      output: r.output,
      sequence: r.sequence,
    }));
  }

  async getPidLogCount(loopIndex?: number): Promise<number> {
    return this.logDb.getPidLogCount(loopIndex);
  }

  /**
   * Return LoadLog records (parent + per-bay) for a date range.
   * Replaces the legacy SD-card LoadLog query path.
   */
  async getLoadLogRecords(startDate: string, endDate: string): Promise<Array<{
    date: string; time: string; record_num: number; sequence: number;
    bays: Array<{ pipe: number; sensor_x10: number; status: number }>;
  }>> {
    const parents = await this.logDb.getLoadLogByDateRange(startDate, endDate);
    if (parents.length === 0) return [];
    const bayRows = await this.logDb.getLoadLogBays(parents.map(p => p.id));
    const byParent = new Map<number, Array<{ pipe: number; sensor_x10: number; status: number }>>();
    for (const b of bayRows) {
      if (!byParent.has(b.load_id)) byParent.set(b.load_id, []);
      byParent.get(b.load_id)!.push({ pipe: b.pipe, sensor_x10: b.sensor_x10, status: b.status });
    }
    return parents.map(p => ({
      date: p.date,
      time: p.time,
      record_num: p.record_num,
      sequence: p.sequence,
      bays: byParent.get(p.id) ?? [],
    }));
  }

  async getLoadLogCount(): Promise<number> {
    return this.logDb.getLoadLogCount();
  }

  async getActivitySensorData(
    sensorId: string,
    startRec: string,
    endRec: string,
    _source?: string,
  ): Promise<string[] | string[][]> {
    const start = parseInt(startRec, 10) || 1;
    const end = parseInt(endRec, 10) || 100;
    const rows = await this.logDb.getActivityLogByRange(start, end);

    if (sensorId === 'hsMode') {
      return rows.map(r => String(r.mode));
    }

    if (sensorId === 'equip' || sensorId === 'remote') {
      // Return equipment/remote state changes as row arrays
      return rows
        .filter(r => r.event_type === 0) // EVENT_EQUIP_CHANGE
        .map(r => [`${r.description}=${r.new_state ? 'On' : 'Off'}`]);
    }

    if (sensorId === 'warn') {
      return rows
        .filter(r => r.event_type === 1 || r.event_type === 2)
        .map(r => `${r.description}=${r.new_state ? 'Active' : 'Cleared'}`);
    }

    return rows.map(() => '0');
  }

  // â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
  // Data info
  // â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

  async getDataInfo(): Promise<{
    database: { activityCount: number; historyCount: number; percentUsed: number; startDate: string };
    storage: {
      mount: string;
      totalBytes: number;
      usedBytes: number;
      freeBytes: number;
      percentUsed: number;
    };
  }> {
    const historyCount  = await this.logDb.getUserLogCount();
    const activityCount = await this.logDb.getActivityLogCount();

    // Storage info: report the partition that holds the bridge process'
    // working directory. On the rpi5 production target this is the SSD
    // mount where Postgres's `paneldb` cluster also lives, so a single
    // statfs covers both pg and any future on-disk artifacts. Falls back
    // to zeros if statfs fails (e.g. on Windows during local dev).
    let storage = { mount: process.cwd(), totalBytes: 0, usedBytes: 0, freeBytes: 0, percentUsed: 0 };
    try {
      const s = await statfs(process.cwd());
      const total = s.blocks * s.bsize;
      const free  = s.bavail * s.bsize;
      const used  = total - free;
      storage = {
        mount: process.cwd(),
        totalBytes: total,
        usedBytes: used,
        freeBytes: free,
        percentUsed: total > 0 ? Math.round((used / total) * 100) : 0,
      };
    } catch (err) {
      console.warn('[NovaLogStore] statfs failed:', (err as Error).message);
    }

    return {
      database: {
        activityCount,
        historyCount,
        percentUsed: storage.percentUsed,  // pg lives on the same SSD
        startDate: new Date().toISOString(),
      },
      storage,
    };
  }

  // â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
  // Graph favorites
  // â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

  getGraphFavorites(): string {
    const cached = this.dataCache.getByVarName('GraphFavoritesData');
    return cached?.value || this.graphFavorites || '';
  }

  async saveGraphFavorites(favorites: string): Promise<void> {
    this.graphFavorites = favorites;
    this.dataCache.updateFromArm('GraphFavorites', favorites);
  }

  // â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
  // Alarm history
  // â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

  logVfdAlarm(text: string, status: 'ON' | 'OFF'): void {
    const now = new Date();
    const mm = String(now.getMonth() + 1).padStart(2, '0');
    const dd = String(now.getDate()).padStart(2, '0');
    const yyyy = now.getFullYear();
    const hh = String(now.getHours()).padStart(2, '0');
    const mi = String(now.getMinutes()).padStart(2, '0');
    const ss = String(now.getSeconds()).padStart(2, '0');
    this.vfdAlarmLog.push({
      date: `${mm}/${dd}/${yyyy}`,
      time: `${hh}:${mi}:${ss}`,
      text,
      status,
    });
    if (this.vfdAlarmLog.length > NovaLogDataStore.MAX_VFD_LOG) {
      this.vfdAlarmLog = this.vfdAlarmLog.slice(-NovaLogDataStore.MAX_VFD_LOG);
    }
  }

  async getAlarmHistory(count: number): Promise<Array<{
    date: string;
    currentmode: string;
    alarmdata: string[];
  }>> {
    // Get warning events from SQLite
    const warnings = await this.logDb.getRecentActivity(count, [1, 2]); // WARNING_ON, WARNING_OFF
    const armAlarms = warnings.map(w => ({
      date: `${w.date} ${w.time}`,
      currentmode: String(w.mode),
      alarmdata: [`${w.description}=${w.new_state ? 'Active' : 'Cleared'}`],
    }));
    return this.mergeVfdAlarms(armAlarms, count);
  }

  private mergeVfdAlarms(
    armAlarms: Array<{ date: string; currentmode: string; alarmdata: string[] }>,
    count: number,
  ): Array<{ date: string; currentmode: string; alarmdata: string[] }> {
    if (this.vfdAlarmLog.length === 0) return armAlarms;

    const vfdEntries = this.vfdAlarmLog.slice(-count).reverse().map(e => {
      const eqIdx = e.text.indexOf('=');
      const humanText = eqIdx > 0 ? e.text.slice(eqIdx + 1) : e.text;
      return {
        date: `${e.date} ${e.time}`,
        currentmode: '0',
        alarmdata: [`Alarm=${e.status === 'ON' ? humanText : 'Cleared'},Status=${e.status}`],
      };
    });

    const merged = [...vfdEntries, ...armAlarms];
    merged.sort((a, b) => {
      const da = new Date(a.date.replace(/#.*$/, '')).getTime() || 0;
      const db = new Date(b.date.replace(/#.*$/, '')).getTime() || 0;
      return db - da;
    });

    return merged.slice(0, count);
  }

  // â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
  // Activity log labels
  // â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

  /* (Apr 2026: removed `getActivityEquipmentLabels` and
   * `getActivityRemoteLabels` stub methods — they returned hardcoded
   * `['Equipment']`/`['Remote']`. Their callers (`/iot/avail/*` routes)
   * have been deleted; defaults are now seeded directly in the UI's
   * `equipListStore`/`remoteListStore`.) */

  /** Close the PostgreSQL pool cleanly (call on shutdown). */
  async close(): Promise<void> {
    try {
      await this.logDb.close();
      console.log('[NovaLogStore] Database closed');
    } catch (err: any) {
      console.error('[NovaLogStore] Error closing database:', err.message);
    }
  }

  // â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•
  // Minimal protobuf decoder helpers
  // â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

  private pbDecode(buf: Buffer): Array<{ field: number; wireType: number; value: number | Buffer }> {
    const fields: Array<{ field: number; wireType: number; value: number | Buffer }> = [];
    let pos = 0;

    const readVarint = (): number => {
      let val = 0, shift = 0;
      while (pos < buf.length) {
        const byte = buf[pos++];
        val |= (byte & 0x7F) << shift;
        if ((byte & 0x80) === 0) return val >>> 0;
        shift += 7;
        if (shift >= 35) break;
      }
      return val >>> 0;
    };

    while (pos < buf.length) {
      const tag = readVarint();
      const wireType = tag & 0x07;
      const fieldNum = tag >>> 3;
      if (fieldNum === 0) break;

      if (wireType === 0) { // varint
        fields.push({ field: fieldNum, wireType, value: readVarint() });
      } else if (wireType === 2) { // length-delimited
        const len = readVarint();
        fields.push({ field: fieldNum, wireType, value: Buffer.from(buf.subarray(pos, pos + len)) });
        pos += len;
      } else if (wireType === 5) { // fixed32
        const fbuf = buf.subarray(pos, pos + 4);
        fields.push({ field: fieldNum, wireType, value: Buffer.from(fbuf) });
        pos += 4;
      } else if (wireType === 1) { // fixed64
        pos += 8;
      } else {
        break;
      }
    }
    return fields;
  }

  private pbVarint(fields: Array<{ field: number; wireType: number; value: number | Buffer }>, fieldNum: number): number {
    const f = fields.find(f => f.field === fieldNum && f.wireType === 0);
    return f ? (f.value as number) : 0;
  }

  private pbFloat(fields: Array<{ field: number; wireType: number; value: number | Buffer }>, fieldNum: number): number {
    const f = fields.find(f => f.field === fieldNum && f.wireType === 5);
    if (!f) return 0;
    return (f.value as Buffer).readFloatLE(0);
  }

  private pbString(fields: Array<{ field: number; wireType: number; value: number | Buffer }>, fieldNum: number): string {
    const f = fields.find(f => f.field === fieldNum && f.wireType === 2);
    return f ? (f.value as Buffer).toString('utf8') : '';
  }
}
