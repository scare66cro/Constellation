/**
 * novaAdapter.ts — Bridges the new protobuf protocol to the existing
 * DataCache + REST/WebSocket infrastructure.
 *
 * Strategy: The existing apiRoutes.ts and wsManager.ts work with DataCache
 * (string-based CGI table). Rather than rewriting all API endpoints at once,
 * this adapter:
 *   1. Listens to NovaSerialBridge 'message' events
 *   2. Decodes protobuf messages into the NovaDataStore (typed)
 *   3. ALSO updates the legacy DataCache with CSV strings (compatibility)
 *   4. Triggers WS broadcasts on the existing channels
 *
 * This enables incremental migration:
 *   - Phase 1: New protocol layer, existing REST/WS interface (this adapter)
 *   - Phase 2: New REST endpoints read from NovaDataStore directly
 *   - Phase 3: Remove DataCache and this adapter entirely
 *
 * Wire protocol:  Firmware ←COBS+PB→ NovaSerialBridge → NovaAdapter → DataCache → API/WS
 *                                                      → NovaDataStore (typed, for new endpoints)
 */

import type { NovaSerialBridge } from './novaSerialBridge.js';
import type { NovaDataStore, SystemStatus, EquipmentStatus } from './novaDataStore.js';
import type { DataCache } from './dataCache.js';
import type { WsManager } from './wsManager.js';

// Message IDs (must match envelope.proto field numbers)
const MSG_SYSTEM_STATUS    = 10;
const MSG_EQUIPMENT_STATUS = 11;
const MSG_WARNING_REPORT   = 12;
const MSG_DATA_LOAD_STATUS = 17;
const MSG_BASIC_SETUP      = 20;
const MSG_DATE_TIME        = 21;
const MSG_HEARTBEAT        = 101;

/**
 * Aggregated analog-board state used to rebuild AnalogAllData /
 * SensorListData any time the firmware reports an AnalogBoard message.
 * Keyed by board.address (1-based per the AS2 contract — see
 * Nova_Firmware/Platform/nova_thread_overrides.c::ReadAnalogBoards()).
 */
const analogBoardAgg = new Map<number, any>();

function rebuildAnalogAggregates(dataCache: DataCache): void {
  const sortedAddrs = Array.from(analogBoardAgg.keys()).sort((a, b) => a - b);

  // AnalogAllData stride 5: addr, type, label, version, disabled
  const analogParts: string[] = [];
  // SensorListData stride 6: label, sid, type, value, offset, disabled
  const sensorParts: string[] = [];

  for (const addr of sortedAddrs) {
    const b = analogBoardAgg.get(addr);
    if (!b) continue;
    analogParts.push(
      String(b.address),
      String(b.type),
      b.label ?? '',
      b.version ?? '',
      b.disabled ? '1' : '0',
    );
    const boardIdx0 = Math.max(0, b.address - 1);
    for (const s of b.sensors ?? []) {
      const sid = boardIdx0 * 4 + (s.slot ?? 0);
      sensorParts.push(
        s.label ?? '',
        String(sid),
        String(s.type),
        Number.isFinite(s.value) ? s.value.toFixed(1) : '--',
        Number.isFinite(s.offset) ? String(s.offset) : '0.0',
        s.disabled ? '1' : '0',
      );
    }
  }

  dataCache.updateFromArm('AnalogAll', analogParts.join(','));
  dataCache.updateFromArm('SensorList', sensorParts.join(','));
}

/**
 * Aux-switch CSV — must match the legacy AS2 wire format that the
 * Svelte equipment-status page (`getEquipment` in
 * `lib/business/equipmentStatus.ts`) parses with
 * `equipment.auxSwitches[c].split(':')`.
 *
 * Format: `s0:p0_1:p0_2:...:p0_8,s1:p1_1:p1_2:...:p1_8`
 *   sN  = current AuxN switch position (0=off, 1=auto, 2=manual)
 *   pN_K = sN if any rule in AuxProgram[K-1] references SW_AUXN_AUTO
 *          or SW_AUXN_MANUAL; otherwise 5 (sentinel for "not assigned").
 *
 * Without this, the legacy code would explode on `auxSwitches[0].split(':')`
 * (no optional chaining), which silently kills the entire equipment page.
 *
 * Equipment-IO indices below come from the legacy SerialShift.h enum
 * (mirrored verbatim by the Nova firmware) — SW_AUX1_AUTO=55,
 * SW_AUX1_MANUAL=56, SW_AUX2_AUTO=57, SW_AUX2_MANUAL=58.
 */
const SW_AUX1_AUTO   = 55;
const SW_AUX1_MANUAL = 56;
const SW_AUX2_AUTO   = 57;
const SW_AUX2_MANUAL = 58;
const NUM_AUX_OUTPUTS = 8;

function rebuildAuxSwitches(dataStore: NovaDataStore, dataCache: DataCache): void {
  const states = (dataStore as any).auxSwitches?.states ?? [0, 0];
  const s0 = Number(states[0] ?? 0);
  const s1 = Number(states[1] ?? 0);

  // Per-aux assignments — start with sentinel 5 ("unassigned"), promote
  // to the corresponding switch state when any rule references that
  // switch's IoIndex.
  const usage0 = new Array<number>(NUM_AUX_OUTPUTS).fill(5);
  const usage1 = new Array<number>(NUM_AUX_OUTPUTS).fill(5);

  const programs = (dataStore as any).auxProgram?.programs as
    Array<{ auxIndex: number; rules: Array<{ ioIndex: number }> }> | undefined;

  if (programs) {
    for (const prog of programs) {
      const k = prog.auxIndex;
      if (k < 0 || k >= NUM_AUX_OUTPUTS) continue;
      for (const rule of prog.rules ?? []) {
        if (rule.ioIndex === SW_AUX1_AUTO || rule.ioIndex === SW_AUX1_MANUAL) {
          usage0[k] = s0;
        }
        if (rule.ioIndex === SW_AUX2_AUTO || rule.ioIndex === SW_AUX2_MANUAL) {
          usage1[k] = s1;
        }
      }
    }
  }

  const part0 = [s0, ...usage0].join(':');
  const part1 = [s1, ...usage1].join(':');
  dataCache.updateFromArm('AuxSwitches', `${part0},${part1}`);
}

/**
 * Wire up the new protocol to the existing data pipeline.
 */
export function createNovaAdapter(
  bridge: NovaSerialBridge,
  dataStore: NovaDataStore,
  dataCache: DataCache,
  wsManager: WsManager | null,
): void {
  // Forward all messages from bridge to data store
  bridge.on('message', (msgId: number, data: Buffer, seq: number) => {
    dataStore.handleMessage(msgId, data);
  });

  // Convert typed data back to CSV strings for legacy DataCache compatibility
  dataStore.on('systemStatus', (status: SystemStatus) => {
    // Reconstruct the "main" tag CSV that the old system expects
    const csv = [
      formatTemp(status.plenumTemp),
      formatTemp(status.plenumHumid),
      formatTemp(status.outsideTemp),
      status.remoteTemp.toFixed(1),
      formatTemp(status.outsideHumid),
      status.remoteHumid.toFixed(1),
      formatTemp(status.startTemp),
      formatTemp(status.returnHumid),
      formatTemp(status.returnTemp),
      formatTemp(status.co2Level),
      status.fanSpeed || '0',
      status.coolOutput || '',
      status.coolLabel || '',
      status.burnerOutput || '',
      formatTemp(status.calcHumid),
    ].join(',');

    dataCache.updateFromArm('main', csv);
    dataCache.updateFromArm('CurrentMode', String(status.currentMode));
    // Burner-cure substate (legacy CureState global).  The dashboards/log
    // surface this so operators can see whether the system is running an
    // air cure, burner cure, modulating door, etc.  Always publish (even
    // 0/CS_OFF) so consumers can clear stale displays when curing ends.
    dataCache.updateFromArm('CureState', String(status.cureState ?? 0));
  });

  dataStore.on('equipmentStatus', (equip: EquipmentStatus) => {
    // Build the EquipStatus CSV that the old system expects.
    // The firmware packs the 102-field legacy format as 34 EquipState items
    // (3 fields each: outputOn→field0, remoteOff→field1, alarm→field2).
    const parts = equip.items.map(item =>
      `${item.outputOn ? 1 : 0},${item.remoteOff},${item.alarm}`
    );
    const csv = parts.join(',');
    const eq = csv.split(',');

    // Override switch display fields — Constellation has no physical CPLD
    // panel switches. The firmware reads switch inputs that are always 0 in
    // QEMU. Map the software switch state (remoteOff) to the switch display:
    //   remoteOff 0 (AUTO)   → switch '1' (AUTO)
    //   remoteOff 1 (OFF)    → switch '0' (OFF)
    //   remoteOff 2 (MANUAL) → switch '2' (MANUAL)
    //
    // Use the live eq-indexed IO config from the firmware (outputMap[eqIdx] = portId).
    // Fallback to persisted cache data if firmware hasn't sent IoConfig yet.
    const ioOutMap = dataStore.ioConfig?.outputMap ?? [];
    const cachedEqCfg = ioOutMap.length === 0 ? dataCache.getEqIndexedOutputConfig() : [];
    const isConfigured = (eqIdx: number): boolean => {
      if (ioOutMap.length > 0) {
        // Live firmware data — unassigned ports are 255, 0xFFFF, or
        // IO_UNDEFINED (0xFFFFFFFF), and 0 is "no port".
        const p = ioOutMap[eqIdx];
        return eqIdx < ioOutMap.length && p !== 255 && p !== 0xFFFF && p !== 0xFFFFFFFF && p !== 0;
      }
      // Fallback to persisted cache (eq-indexed: eqCfg[eqIdx] = portId)
      return eqIdx < cachedEqCfg.length && cachedEqCfg[eqIdx] !== '-1' && cachedEqCfg[eqIdx] !== undefined;
    };
    const remoteToSwitch = (eqIdx: number, remoteField: number): string => {
      if (!isConfigured(eqIdx)) return '0';
      const r = eq[remoteField] ?? '0';
      return r === '1' ? '0' : r === '2' ? '2' : '1';  // off→0, manual→2, auto→1
    };

    // Fan switch (eq[0]): remoteOff is at eq[37]
    if (eq.length > 37) eq[0] = remoteToSwitch(0, 37);
    // Climacell/Burner switch (eq[3]): remoteOff at eq[38]
    if (eq.length > 38) eq[3] = (isConfigured(3) || isConfigured(6)) ? remoteToSwitch(3, 38) : '0';
    // Humid switch (eq[8]): remoteOff at eq[39]
    if (eq.length > 39) eq[8] = (isConfigured(7) || isConfigured(9) || isConfigured(11)) ? remoteToSwitch(7, 39) : '0';
    // Door switch (eq[29]): AUTO when doors configured
    if (eq.length > 29) eq[29] = isConfigured(41) || isConfigured(42) ? '1' : '0';

    dataCache.updateFromArm('EquipStatus', eq.join(','));
  });

  dataStore.on('basicSetup', (setup: any) => {
    const csv = [
      setup.storageName,
      setup.tempType,
      setup.mode,
      setup.homePage || '',
      setup.systemMode,
      setup.language,
      setup.masterIp || '',
      setup.multiView,
      setup.loginPw || '',
      setup.localLogin,
      setup.animations,
    ].join(',');
    dataCache.updateFromArm('StorageName', csv);
    // PanelName is the same string as StorageName but is published on the
    // header-data WS frame (var 55) and used for the GellertHeader title,
    // alarm-monitor banner and node dropdowns. Without this mirror the
    // header would forever show the "Agristar Panel" default instead of
    // the user's configured storage name (e.g. "Gellert Nova").
    if (setup.storageName) {
      dataCache.updateFromArm('PanelName', setup.storageName);
    }
  });

  dataStore.on('dateTime', (dt: any) => {
    dataCache.updateFromArm('Date', `${dt.dateStr},${dt.timeStr},${dt.amPm}`);
  });

  dataStore.on('plenumSettings', (ps: any) => {
    const csv = [
      ps.tempSetpoint.toFixed(1),
      ps.humidSetpoint,
      ps.humidSetpointRef,
      ps.burnerTempSetpoint.toFixed(1),
      ps.burnerThreshold.toFixed(1),
    ].join(',');
    dataCache.updateFromArm('p1Plenum', csv);
  });

  dataStore.on('burnerSettings', (bs: any) => {
    const csv = [
      bs.on, bs.low, bs.pGain, bs.iGain, bs.dGain, bs.uLimit, bs.mode, bs.manual
    ].join(',');
    dataCache.updateFromArm('selBurnerMode', csv);
  });

  dataStore.on('co2Settings', (cs: any) => {
    const csv = [
      cs.mode, cs.minTemp, cs.maxTemp, cs.durationMinutes,
      cs.cycleOrSet, cs.fanOutput, cs.doorOutput
    ].join(',');
    dataCache.updateFromArm('selPurgeMode', csv);
  });

  dataStore.on('cureSettings', (cure: any) => {
    const csv = [
      cure.startTemp, cure.humidRef, cure.startHumid, cure.humidHighLimit
    ].join(',');
    dataCache.updateFromArm('CureStartTemp', csv);
  });

  dataStore.on('fanBoostSettings', (fb: any) => {
    const csv = [fb.mode, fb.speed, fb.interval, fb.duration, fb.temp].join(',');
    dataCache.updateFromArm('selBoostMode', csv);
  });

  dataStore.on('climacellSettings', (cc: any) => {
    const csv = [
      cc.efficiency, cc.altitude, cc.altUnits,
      cc.pGain, cc.iGain, cc.dGain, cc.uLimit
    ].join(',');
    dataCache.updateFromArm('ClimacellEff', csv);
  });

  dataStore.on('fanSpeedSettings', (fs: any) => {
    // Legacy FreqCtrlData CSV layout (parsed by dataCache 'fanspeed'):
    //   [0]=MaxSpeed  [1]=MinSpeed  [2]=RefrigSpeed  [3]=RecircSpeed
    //   [4]=UpdatePeriod  [5]=TempDiff  [6]=TempRef1  [7]=TempRef2
    //   [8]=PrevSpeed  [9]=UpdateMode
    const csv = [
      fs.maxSpeed ?? 60,
      fs.minSpeed ?? 20,
      fs.refrigSpeed ?? 100,
      fs.recircSpeed ?? 50,
      fs.updatePeriod ?? 4,
      fs.tempDiff ?? 2.0,
      fs.tempRef1 ?? 0,
      fs.tempRef2 ?? 0,
      fs.prevSpeed ?? 75,
      fs.updateMode ?? 0,
    ].join(',');
    // Cache row keys by msgTag; CGI#6 msgTag is 'maxFanSpeed'.
    dataCache.updateFromArm('maxFanSpeed', csv);
  });

  dataStore.on('rampRateSettings', (rr: any) => {
    // CSV order matches dataCache `case 'ramp'` and apply_ramp_rate decoder:
    //   [0]=ratePerDay, [1]=updatePeriod (255→'Automatically'), [2]=tempDiff,
    //   [3]=tempRef, [4]=targetTemp
    // updateFromArm() keys off msgTag, not varName — entry 7 is
    //   { msgTag: 'updTemp', varName: 'RampRateData' }. Pass the msgTag.
    const period = (rr.updatePeriod === 255) ? 'Automatically' : String(rr.updatePeriod ?? 0);
    // Format float to 1 decimal so the UI doesn't show 0.4000000059604645.
    const fmt = (v: number) => Number.isFinite(v) ? (Math.round(v * 10) / 10).toString() : '0';
    const csv = `${fmt(rr.ratePerDay)},${period},${fmt(rr.tempDiff)},${rr.tempRef},${fmt(rr.targetTemp)}`;
    dataCache.updateFromArm('updTemp', csv);
  });

  dataStore.on('refrigSettings', (rs: any) => {
    // Match the legacy UI_SendRefrig() CSV layout exactly so the
    // dataCache 'refrigeration' page handler can flow it through to the
    // UI without re-ordering. 23 elements:
    //   [0..11]  stages 1-6 (on/off pairs)
    //   [12..15] PID p,i,d,u
    //   [16]     Refrig.Purge mode
    //   [17]     Co2.Purge.RefrigThresh
    //   [18]     Log.PID.Refrig (refrig PID logging flag)
    //   [19,20]  stage 7 on/off
    //   [21,22]  stage 8 on/off
    const stages = rs.stages ?? [];
    const stage = (i: number) => ({
      on:  stages[i]?.on  ?? 0,
      off: stages[i]?.off ?? 0,
    });
    const csvParts: (number | string)[] = [];
    for (let i = 0; i < 6; i++) {
      const s = stage(i);
      csvParts.push(s.on, s.off);
    }
    csvParts.push(rs.pGain ?? 0, rs.iGain ?? 0, rs.dGain ?? 0, rs.uLimit ?? 0);
    csvParts.push(rs.purge ?? 0, rs.purgeThreshold ?? 0);
    // Log.PID.Refrig flag isn't carried in RefrigSettings — pull from
    // pidLogSettings if available, else 0. Keeps round-trip symmetry
    // with apiRoutes (which doesn't write this slot from refrig page).
    const pl = (dataStore as any).pidLogSettings;
    csvParts.push(pl?.logRefrig ?? 0);
    const s6 = stage(6);
    const s7 = stage(7);
    csvParts.push(s6.on, s6.off, s7.on, s7.off);
    // varName='RefrigData' but updateFromArm keys off msgTag (entry 85
    // in CGI_DEFINITIONS uses both names equal: 'RefrigData').
    dataCache.updateFromArm('RefrigData', csvParts.join(','));
  });

  dataStore.on('humidCtrlSettings', (hc: any) => {
    // Build the 21-element CSV expected by HumidCtrlData consumers:
    //   3 humidifiers × 7 fields each (mode, 6 cycle durations).
    // The dataCache 'humidifier' page handler in dataCache.ts splits
    // this into 3 rows of 7 for the UI.
    const entries = (hc?.entries ?? []) as Array<{
      index: number; mode: number;
      coolOn: number; coolOff: number;
      recOn: number;  recOff: number;
      refOn: number;  refOff: number;
    }>;
    // Pre-fill 21 zeros, then splice each entry into its index*7 offset.
    const flat = new Array(21).fill('0');
    for (const e of entries) {
      if (e.index < 0 || e.index >= 3) continue;
      const off = e.index * 7;
      flat[off    ] = String(e.mode);
      flat[off + 1] = String(e.coolOn);
      flat[off + 2] = String(e.coolOff);
      flat[off + 3] = String(e.recOn);
      flat[off + 4] = String(e.recOff);
      flat[off + 5] = String(e.refOn);
      flat[off + 6] = String(e.refOff);
    }
    dataCache.updateFromArm('selHumidType', flat.join(','));
  });

  dataStore.on('outsideAirSettings', (oa: any) => {
    // dataCache buildPageData('outside') parses this as a `&`-delimited
    // tokens list: first token is positional ctrlMode, rest are key=value
    // pairs (OutsideAirSet=, selAboveBelow=, selTempRef=, calcHumid=).
    const parts = [
      String(oa.mode ?? 0),
      `OutsideAirSet=${oa.differential ?? 0}`,
      `selAboveBelow=${oa.aboveBelow ?? 0}`,
      `selTempRef=${oa.tempRef ?? 255}`,
      `calcHumid=${oa.calcHumidMax ?? 0}`,
    ];
    dataCache.updateFromArm('ctrlMode', parts.join('&'));
  });

  dataStore.on('miscSettings', (mc: any) => {
    // Legacy p1Misc CSV layout (consumed by dataCache 'misc' pageBuilder):
    //   [0]=refrMode  [1]=defrostInterval [2]=defrostTime [3]=tempThresh
    //   [4]=ctrlMode  [5]=cavityMode      [6]=cavityDiff  [7]=dutyOrSensor
    //   [8]=cavSensor (legacy duplicate of [7]) [9]=kbPref [10]=cavStandbyOn
    const csv = [
      mc.refrigMode ?? 0,
      mc.defrostInterval ?? 0,
      mc.defrostDuration ?? 0,
      mc.heatTempThresh ?? 0,
      mc.cavityTarget ?? 0,
      mc.cavityMode ?? 1,
      mc.cavityDiff ?? 0,
      mc.cavityDutyOrSensor ?? 0,
      mc.cavityDutyOrSensor ?? 0,
      mc.kbPref ?? 0,
      mc.cavityStandbyOn ?? 0,
    ].join(',');
    dataCache.updateFromArm('p1Misc', csv);
  });

  dataStore.on('failureSettings', (fs: any) => {
    // The UI failures1 page treats FailureData1 as a 22-element CSV
    // matching apiRoutes pageSaveMap.failures1 input order:
    //   [0]FanMode  [1]FanTimer  [2]ClimacellMode  [3]ClimacellTimer
    //   [4]RefridgeMode [5]RefridgeTimer [6]RefridgeRun
    //   [7]RefrStagesMode [8]RefrStagesTimer
    //   [9]HumidifiersMode [10]HumidifiersTimer
    //   [11]AuxMode [12]AuxTimer [13]HeatMode [14]HeatTimer
    //   [15]CavityHeatMode [16]CavityHeatTimer
    //   [17]BurnerMode [18]BurnerTimer
    //   [19]LightsMode [20]LightsTimer [21]LightsUnits
    // Firmware only persists 11 of those — the rest are slot-padded with 0.
    const v = fs.values || {};
    const slots = new Array(22).fill('0');
    slots[0]  = String(v.fanMode           ?? 0);
    slots[1]  = String(v.fanTimer          ?? 0);
    slots[2]  = String(v.climacellMode     ?? 0);
    slots[3]  = String(v.climacellTimer    ?? 0);
    slots[4]  = String(v.refrigMode        ?? 0);
    slots[5]  = String(v.refrigTimer       ?? 0);
    slots[6]  = String(v.refrigRun         ?? 0);
    slots[7]  = String(v.refrigStagesMode  ?? 0);
    slots[8]  = String(v.refrigStagesTimer ?? 0);
    slots[9]  = String(v.humidMode         ?? 0);
    slots[10] = String(v.humidTimer        ?? 0);
    slots[11] = String(v.auxMode           ?? 0);
    slots[12] = String(v.auxTimer          ?? 0);
    slots[13] = String(v.heatMode          ?? 0);
    slots[14] = String(v.heatTimer         ?? 0);
    slots[15] = String(v.cavityHeatMode    ?? 0);
    slots[16] = String(v.cavityHeatTimer   ?? 0);
    slots[17] = String(v.burnerMode        ?? 0);
    slots[18] = String(v.burnerTimer       ?? 0);
    slots[19] = String(v.lightsMode        ?? 0);
    slots[20] = String(v.lightsTimer       ?? 0);
    slots[21] = String(v.lightsUnits       ?? 0);
    // varName='FailureData1' but updateFromArm keys off msgTag='FanMode'.
    dataCache.updateFromArm('FanMode', slots.join(','));
  });

  dataStore.on('failureSettings2', (fs2: any) => {
    // 13-element CSV matching apiRoutes pageSaveMap.failures2 input order:
    //   [0]OutAirMode [1]OutAirTimer
    //   [2]OutHumidMode [3]OutHumidTimer
    //   [4]HighCo2Mode [5]HighCo2Timer [6]Co2Setpt
    //   [7]LowHumidMode [8]LowHumidTimer [9]LowHumidSet
    //   [10]PlenSenMode [11]PlenSenTimer [12]PlenSenDiff (float).
    const v = fs2.values || {};
    const slots = [
      String(v.outAirMode    ?? 0),
      String(v.outAirTimer   ?? 0),
      String(v.outHumidMode  ?? 0),
      String(v.outHumidTimer ?? 0),
      String(v.highCo2Mode   ?? 0),
      String(v.highCo2Timer  ?? 0),
      String(v.co2Setpt      ?? 0),
      String(v.lowHumidMode  ?? 0),
      String(v.lowHumidTimer ?? 0),
      String(v.lowHumidSet   ?? 0),
      String(v.plenSenMode   ?? 0),
      String(v.plenSenTimer  ?? 0),
      String(v.plenSenDiff   ?? 0),
    ];
    // varName='FailureData2' but updateFromArm keys off msgTag='OutAirMode'.
    dataCache.updateFromArm('OutAirMode', slots.join(','));
  });

  dataStore.on('tempAlarmSettings', (ta: any) => {
    // Plenum page composer (dataCache.ts plensetup) reads PlenTempDevData,
    // populated via the legacy ARM tag 'AlarmTempLow'.  updateFromArm keys
    // off the legacy tag, NOT the varName — passing 'PlenTempDevData' here
    // is a silent no-op (returns false, drops the value on the floor).
    // CSV order must match legacy AlarmTempLow=:
    //   lowTemp, lowTimer, highTemp, highTimer, cureLow, cureHigh.
    dataCache.updateFromArm(
      'AlarmTempLow',
      `${ta.lowTemp ?? 0},${ta.lowTimer ?? 0},${ta.highTemp ?? 0},${ta.highTimer ?? 0},${ta.cureLow ?? 0},${ta.cureHigh ?? 0}`
    );
  });

  dataStore.on('doorSettings', (ds: any) => {
    // dataCache.case 'door' reads P2FreshAirData first (primary), DoorData fallback.
    // 6 positional fields: PID_P,PID_I,PID_D,PID_U,ActuatorTime,CoolAirCycle.
    // Field numbers + order MUST match proto/agristar/settings.proto DoorSettings
    // and Nova_Firmware/Platform/nova_dataexc.c::apply_door().
    // msgTag-vs-varName trap: updateFromArm keys by msgTag.
    //   CGI_FRESHAIR:  msgTag='PAirValue',  varName='P2FreshAirData'
    //   CGI_DOOR_LOG:  msgTag='DoorData',   varName='DoorData' (identical)
    const csv = `${ds.pGain ?? 0},${ds.iGain ?? 0},${ds.dGain ?? 0},${ds.uLimit ?? 0},${ds.actuatorTime ?? 0},${ds.coolAirCycle ?? 0}`;
    dataCache.updateFromArm('PAirValue', csv);
    dataCache.updateFromArm('DoorData', csv);
  });

  dataStore.on('loadMonitorSettings', (lm: any) => {
    dataCache.updateFromArm('LoadMonitorData', `${lm.bay1Label},${lm.bay2Label},${lm.bayCount}`);
  });

  dataStore.on('versionInfo', (vi: any) => {
    dataCache.updateFromArm('FirmwareVersion', `${vi.armVersion},${vi.bootloaderVersion}`);
  });

  dataStore.on('serviceInfo', (si: any) => {
    // msgTag-vs-varName trap (see runclock fix, docs/firmware-bridge-protocol.md).
    // dataCache.updateFromArm keys by msgTag (='dealerName'), NOT varName
    // ('P2ServiceData' or 'ServiceData').
    dataCache.updateFromArm('dealerName', `${si.dealerName},${si.dealerPhone},${si.techName},${si.techPhone}`);
  });

  dataStore.on('ioConfig', (io: any) => {
    // Firmware sends eq-indexed maps (outputMap[eqIdx] = portId).
    // The cache tag 'OutputConfig' → 'IoConfigOutData' is expected to be
    // port-indexed by getEqIndexedOutputConfig(). Convert eq→port first.
    //
    // Unassigned ports come through as IO_UNDEFINED (0xFFFFFFFF) — must be
    // filtered out BEFORE Math.max, otherwise maxPorts becomes 4 billion+
    // and `new Array(...)` throws RangeError, leaving the cache stale.
    const IO_UNDEF = 0xFFFFFFFF;
    // Real port IDs are SS_PORT_ID_MULTIPLIER (12) × boardIdx + outputIdx,
    // so even with 16 boards the max valid port is ~200. Anything larger is
    // garbage (uninitialized memory / corrupted enum offset) and would
    // explode the array allocation below.
    const MAX_VALID_PORT = 255;
    const isAssigned = (p: number) =>
      p !== 255 && p !== 0xFFFF && p !== IO_UNDEF && p > 0 && p <= MAX_VALID_PORT;
    const validPorts: number[] = [];
    for (const p of io.outputMap) if (isAssigned(p)) validPorts.push(p);
    for (const p of io.inputMap)  if (isAssigned(p)) validPorts.push(p);
    const maxPort = validPorts.length ? Math.max(...validPorts) : 0;
    const slotCount = Math.max(maxPort + 1, io.outputMap.length, io.inputMap.length, 60);

    const portOut = new Array(slotCount).fill('-1');
    for (let eq = 0; eq < io.outputMap.length; eq++) {
      const port = io.outputMap[eq];
      if (isAssigned(port) && port < portOut.length) {
        portOut[port] = String(eq);
      }
    }
    const portIn = new Array(slotCount).fill('-1');
    for (let eq = 0; eq < io.inputMap.length; eq++) {
      const port = io.inputMap[eq];
      if (isAssigned(port) && port < portIn.length) {
        portIn[port] = String(eq);
      }
    }
    dataCache.updateFromArm('OutputConfig', portOut.join(','));
    dataCache.updateFromArm('InputConfig', portIn.join(','));
  });

  dataStore.on('ioDefinition', (ioDef: any) => {
    // Pass the firmware's IoEntry[] through verbatim. The firmware is the
    // single source of truth for equipment names — if a name is empty here
    // it means the firmware hasn't initialised it yet, and the UI must not
    // synthesise a placeholder.
    dataCache.setIoEntries(ioDef.entries);
  });

  dataStore.on('emailSettings', (em: any) => {
    // Legacy CSV layout (consumed by dataCache 'email' pageBuilder):
    //   [0]=enabled, [1]=server, [2]=authType, [3]=port, [4]=account,
    //   [5]=password, [6]=displayId, [7]=toAddr, [8]=fromAddr
    const csv = [
      em.enabled ?? 1,
      em.server ?? '',
      em.authType ?? 0,
      em.port ?? '',
      em.username ?? '',
      em.password ?? '',
      em.displayId ?? '',
      em.toAddr ?? '',
      em.fromAddr ?? '',
    ].join(',');
    dataCache.updateFromArm('EmailConfig', csv);
  });

  dataStore.on('warningReport', (report: any) => {
    // Translate the protobuf warning list into the legacy AlarmData wire
    // format expected by translateAlarmData() in dataCache:
    //   "INDEX&STATUS,VALUE0,VALUE1,INDEX&STATUS,VALUE0,VALUE1,..."
    // INDEX = warning code (matches Mini_IO Warnings.h enum)
    // STATUS = AlarmSeverity (1=ALARM, 2=FAIL)
    // VALUE0/VALUE1 = bitmap fields used by some warnings; we put eqIndex
    // in VALUE0 and 0 in VALUE1 since the proto does not carry them yet.
    const warnings = (report && Array.isArray(report.warnings)) ? report.warnings : [];
    const parts: string[] = [];
    for (const w of warnings) {
      // Skip orbit-domain warnings (code >= 0x100) — they don't map to the
      // legacy WARNING_KEYS table; they still flow through WarningData.
      if (w.code >= 0x100) continue;
      if (!w.severity) continue; // 0 = FM_NONE, not an active alarm
      parts.push(`${w.code}&${w.severity}`);
      parts.push(`${w.eqIndex || 0}`);
      parts.push(`0`);
    }
    const csv = parts.join(',');
    dataCache.updateFromArm('AlarmData', csv);
    dataCache.updateFromArm('WarningData', csv);
    if (wsManager) {
      wsManager.broadcastArmUpdate('AlarmData');
      wsManager.broadcastArmUpdate('WarningData');
    }
  });

  // ── Additional message type converters ──

  dataStore.on('runtimes', (rt: any) => {
    const slots = new Array(48).fill(0);
    for (const e of rt.entries) { if (e.slot < 48) slots[e.slot] = e.mode; }
    // updateFromArm keys by msgTag, NOT varName. dataCache cgiTable maps
    // msgTag 'runTimes' -> varName 'RunTimesData'; getByVarName then
    // returns this entry to buildPageData('runtimes').
    dataCache.updateFromArm('runTimes', slots.join(','));
  });

  dataStore.on('fanRuntime', (fr: any) => {
    dataCache.updateFromArm('FanRunTimeData', `${fr.dailyHours},${fr.dailyMinutes},${fr.totalHours},${fr.totalMinutes}`);
  });

  dataStore.on('humidModes', (hm: any) => {
    dataCache.updateFromArm('HumidModeData', `${hm.head1},${hm.pump1},${hm.head2},${hm.pump2}`);
  });

  dataStore.on('auxSwitches', (_sw: any) => {
    rebuildAuxSwitches(dataStore, dataCache);
  });
  dataStore.on('auxProgram', (_ap: any) => {
    rebuildAuxSwitches(dataStore, dataCache);
  });

  dataStore.on('analogBoard', (board: any) => {
    // Aggregate every reported AnalogBoard into the two CSVs that
    // GET /iot/analog/all reads:
    //   AnalogAllData   stride 5: addr,type,label,version,disabled
    //   SensorListData  stride 6: label,sid,type,value,offset,disabled
    // SID = (boardAddr-1)*4 + slot — must match apiRoutes.ts merge logic.
    //
    // Per-board key kept for any legacy code that still references it,
    // but the API endpoint itself relies on the two aggregated keys.
    const sensorCsv = board.sensors.map((s: any) =>
      `${s.slot},${s.type},${s.label},${s.offset},${s.disabled ? 1 : 0},${s.value.toFixed(1)}`
    ).join('|');
    dataCache.updateFromArm(`AnalogBoard${board.address}`, `${board.address},${board.type},${board.label},${board.version},${board.disabled ? 1 : 0}|${sensorCsv}`);

    analogBoardAgg.set(board.address, board);
    rebuildAnalogAggregates(dataCache);
  });

  dataStore.on('availableIo', (aio: any) => {
    dataCache.updateFromArm('AvailableOutputs', aio.outputPins.join(','));
    dataCache.updateFromArm('AvailableInputs', aio.inputPins.join(','));
  });

  dataStore.on('sensorData', (sd: any) => {
    const tempCsv = sd.temps.map((r: any) => `${r.index},${r.value.toFixed(1)},${r.valid ? 1 : 0}`).join('|');
    const humidCsv = sd.humids.map((r: any) => `${r.index},${r.value.toFixed(1)},${r.valid ? 1 : 0}`).join('|');
    dataCache.updateFromArm('PileTempsData', tempCsv);
    dataCache.updateFromArm('PileHumidsData', humidCsv);
  });

  dataStore.on('sensorLabels', (sl: any) => {
    const tempLabels = sl.temps.map((l: any) => `${l.index},${l.label}`).join('|');
    const humidLabels = sl.humids.map((l: any) => `${l.index},${l.label}`).join('|');
    dataCache.updateFromArm('SensorLabelData', `${tempLabels}$${humidLabels}`);
  });

  dataStore.on('accountSettings', (acct: any) => {
    // Page reads `Object.values(data.array).slice(0, 10)` so the cache
    // value must be a comma-CSV of usernames indexed by slot 0..9.
    // dataCache CGI table maps msgTag 'AcctId0' → varName 'UserAccounts',
    // and getArr() splits on ','. Sparse slots are empty strings.
    const slots: string[] = Array(10).fill('');
    for (const u of acct.users ?? []) {
      if (typeof u?.slot === 'number' && u.slot >= 0 && u.slot < 10) {
        slots[u.slot] = u.userId ?? '';
      }
    }
    dataCache.updateFromArm('AcctId0', slots.join(','));
  });

  // (Apr 2026 proto-direct migration: removed `dataStore.on('auxProgram')`
  // CSV-translator that fed dataCache.auxPrograms via the AuxProgram tag.
  // The auxiliary page now consumes the typed AuxProgramBundle store
  // (`auxiliaryComposite` in protoStores.ts) directly. The /iot/aux/all
  // GET, parseAuxProgram, buildDefaultAuxRules, discoverAuxPrograms,
  // dataCache.getAllAuxPrograms, and the NextAux command dispatch are
  // all retired in this same change.)

  dataStore.on('userLogSettings', (ul: any) => {
    dataCache.updateFromArm('recInterval', `${ul.interval},${ul.wrap}`);
  });

  dataStore.on('pidSettings', (pid: any) => {
    dataCache.updateFromArm('pidWrap', `${pid.eqIndex},${pid.wrap}`);
  });

  dataStore.on('pidLogSettings', (pl: any) => {
    // dataCache 'pid' page reads PidLogData as CSV "wrap,doors,refrig"
    // and rebuilds the {pidWrap, logDoors, logRefrig} object the Svelte
    // page expects.
    dataCache.updateFromArm('PidLogData', `${pl.wrap},${pl.logDoors},${pl.logRefrig}`);
  });

  dataStore.on('graphFavorites', (gfav: any) => {
    dataCache.updateFromArm('GraphFavData', gfav.csv);
  });

  dataStore.on('alertSettings', (alerts: any) => {
    // Match WARNING_KEYS.length (97) — firmware tracks up to NUM_WARNINGS
    // entries which exceeds 64. A 64-bit bitmap silently drops indices
    // ≥64 (WARN_ACTLOGWRITE and later).
    const SIZE = 128;
    const bits = new Array(SIZE).fill('0');
    for (const idx of alerts.flags) { if (idx < SIZE) bits[idx] = '1'; }
    dataCache.updateFromArm('AlertSetup', bits.join(''));
  });

  dataStore.on('pwmSettings', (pwm: any) => {
    const parts = pwm.channels.map((ch: any) => `${ch.index},${ch.enabled},${ch.channel}`).join('|');
    dataCache.updateFromArm('PwmOutputData', parts);
  });

  dataStore.on('networkNodes', (nn: any) => {
    const parts = nn.nodes.map((n: any) => `${n.slot},${n.ip},${n.id}`).join('|');
    dataCache.updateFromArm('P2NodeSetupData', parts);
    dataCache.updateFromArm('MasterSlaveData', nn.nodes.length > 0 ? '1' : '0');
  });

  dataStore.on('climacellTimes', (ct: any) => {
    dataCache.updateFromArm('climacellTimes', ct.slots.join('+'));
  });

  // ── Orbit module status ──
  //
  // Firmware is the sole source of truth. The bridge no longer probes
  // the orbit-simulator REST API (Apr 29 2026 mbtcp migration); the
  // QEMU PTY-collapse workaround that used to force `dropFirmwareConnected`
  // is also gone. Production behaviour and dev behaviour are identical:
  // dataCache.orbitBoards mirrors firmware OrbitStatus / OrbitDiscovery
  // pushes verbatim. If sim shows wrong slot connectivity, the fix
  // belongs in the firmware OrbitStatus emitter, not here.
  function mergeFirmwareBoards(boards: any[]): void {
    // Mirror every OrbitBoardStatus proto field into dataCache verbatim
    // (see OrbitBoardCacheRow in dataCache.ts). No conversion/scaling —
    // /memories/repo/data-path-rules.md mandates the bridge is a
    // transparent passthrough; UI/proto consumers handle units.
    dataCache.setOrbitBoards(boards.map((b: any) => ({
      slot:        b.slot,
      role:        b.role,
      connected:   b.connected,
      aoEquip:     b.aoEquip ?? [0, 0],
      dipswitchId: b.dipswitchId,
      commErrors:  b.commErrors,
      estopActive: b.estopActive,
      safeMode:    b.safeMode,
      cpuTemp:     b.cpuTemp,
      uptimeSecs:  b.uptimeSecs,
      firmwareVer: b.firmwareVer,
      zoneId:      b.zoneId,
      legacySlot:  b.legacySlot,
      refrigStage: b.refrigStage,
      ipAddress:   b.ipAddress,
    })));
  }

  dataStore.on('orbitStatus', (os: any) => {
    // Build a CSV for legacy compatibility: slot,dip,conn,role,zone,legacy|...
    const parts = os.boards.map((b: any) =>
      `${b.slot},${b.dipswitchId},${b.connected ? 1 : 0},${b.role},${b.zoneId},${b.legacySlot},${b.estopActive ? 1 : 0},${b.cpuTemp.toFixed(1)},${b.uptimeSecs}`
    );
    dataCache.updateFromArm('OrbitStatusData', parts.join('|'));

    mergeFirmwareBoards(os.boards);

    // Broadcast orbit update via WS
    if (wsManager) wsManager.broadcastArmUpdate('OrbitStatus');
  });

  dataStore.on('orbitDiscovery', (od: any) => {
    // Full discovery with max slots info
    const parts = od.boards.map((b: any) =>
      `${b.slot},${b.dipswitchId},${b.connected ? 1 : 0},${b.role},${b.zoneId},${b.legacySlot},${b.estopActive ? 1 : 0},${b.ipAddress}`
    );
    dataCache.updateFromArm('OrbitDiscoveryData', `${od.maxSlots},${od.boardsFound}|${parts.join('|')}`);

    mergeFirmwareBoards(od.boards);

    if (wsManager) wsManager.broadcastArmUpdate('OrbitDiscovery');
  });

  console.log('[NovaAdapter] Wired protobuf messages → legacy DataCache');
}

function formatTemp(val: number): string {
  return val === 0 ? '0' : val.toFixed(1);
}
