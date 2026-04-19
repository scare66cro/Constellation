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

// Default equipment names (must match the array in index.ts)
const DEFAULT_EQUIP_NAMES: Record<number, string> = {
  0: 'Fan/Green Light', 1: 'Door', 2: 'Refrigeration', 3: 'ClimaCell',
  4: 'Heat', 5: 'Cavity Heat', 6: 'Burner',
  7: 'Humidifier 1 - Head', 8: 'Humidifier 1 - Pump',
  9: 'Humidifier 2 - Head', 10: 'Humidifier 2 - Pump',
  11: 'Humidifier 3 - Head', 12: 'Humidifier 3 - Pump',
  13: 'Refrigeration Stage 1', 14: 'Refrigeration Stage 2',
  15: 'Refrigeration Stage 3', 16: 'Refrigeration Stage 4',
  17: 'Refrigeration Stage 5', 18: 'Refrigeration Stage 6',
  19: 'Refrigeration Stage 7', 20: 'Refrigeration Stage 8',
  21: 'Defrost 1', 22: 'Defrost 2',
  23: 'Bay Lights 1', 24: 'Bay Lights 2',
  25: 'Auxiliary 1', 26: 'Auxiliary 2', 27: 'Auxiliary 3', 28: 'Auxiliary 4',
  29: 'Auxiliary 5', 30: 'Auxiliary 6', 31: 'Auxiliary 7', 32: 'Auxiliary 8',
};

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
      ps.burnerThreshold || '',
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
    const csv = [
      fs.maxSpeed, fs.minSpeed, fs.coolSpeed, fs.recircSpeed, fs.updatePeriod
    ].join(',');
    dataCache.updateFromArm('FanSpeedData', csv);
  });

  dataStore.on('rampRateSettings', (rr: any) => {
    dataCache.updateFromArm('RampRateData', `${rr.ratePerDay},${rr.enabled}`);
  });

  dataStore.on('refrigSettings', (rs: any) => {
    const stageParts = rs.stages.flatMap((s: any) => [s.on, s.off]);
    const csv = [
      ...stageParts,
      rs.pGain, rs.iGain, rs.dGain, rs.uLimit, rs.mode,
      rs.defrostInterval, rs.defrostDuration
    ].join(',');
    dataCache.updateFromArm('RefrigData', csv);
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
    dataCache.updateFromArm('HumidCtrlData', flat.join(','));
  });

  dataStore.on('outsideAirSettings', (oa: any) => {
    dataCache.updateFromArm('OutsideAirData', `${oa.mode},${oa.differential}`);
  });

  dataStore.on('miscSettings', (mc: any) => {
    dataCache.updateFromArm('MiscData', `${mc.heatTempThresh},${mc.kbPref},${mc.lightsFailUnits},${mc.cavStandbyOn}`);
  });

  dataStore.on('failureSettings', (fs: any) => {
    // Flatten to CSV preserving the order expected by the UI
    const v = fs.values;
    const parts: string[] = [];
    for (let i = 0; i < 11; i++) {
      parts.push(String(v[`fail${i}Mode`] ?? 0), String(v[`fail${i}Timer`] ?? 0));
    }
    dataCache.updateFromArm('FailureData1', `${v.fanMode ?? 0},${v.fanTimer ?? 0},${parts.join(',')}`);
  });

  dataStore.on('failureSettings2', (fs2: any) => {
    const v = fs2.values;
    dataCache.updateFromArm('FailureData2', `${v.outAirMode ?? 0},${v.sensorMode ?? 0}`);
  });

  dataStore.on('tempAlarmSettings', (ta: any) => {
    // Plenum page composer (dataCache.ts plensetup) reads PlenTempDevData,
    // populated via the legacy ARM tag 'AlarmTempLow'.  updateFromArm keys
    // off the legacy tag, NOT the varName — passing 'PlenTempDevData' here
    // is a silent no-op (returns false, drops the value on the floor).
    // CSV order must match legacy AlarmTempLow=:
    //   lowTemp, lowMin, highTemp, highMin, cureLow, cureHigh.
    dataCache.updateFromArm(
      'AlarmTempLow',
      `${ta.lowTemp ?? 0},${ta.lowMin ?? 0},${ta.highTemp ?? 0},${ta.highMin ?? 0},${ta.cureLow ?? 0},${ta.cureHigh ?? 0}`
    );
  });

  dataStore.on('doorSettings', (ds: any) => {
    dataCache.updateFromArm('DoorData', `${ds.pGain},${ds.iGain},${ds.dGain},${ds.uLimit},${ds.actuatorTime},${ds.coolAirCycle}`);
  });

  dataStore.on('loadMonitorSettings', (lm: any) => {
    dataCache.updateFromArm('LoadMonitorData', `${lm.bay1Label},${lm.bay2Label},${lm.bayCount}`);
  });

  dataStore.on('versionInfo', (vi: any) => {
    dataCache.updateFromArm('FirmwareVersion', `${vi.armVersion},${vi.bootloaderVersion}`);
  });

  dataStore.on('serviceInfo', (si: any) => {
    dataCache.updateFromArm('ServiceData', `${si.dealerName},${si.dealerPhone},${si.techName},${si.techPhone}`);
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
    // Build the IoNames CSV: "Name:Mode:IO:Renamable:Index,..."
    const parts = ioDef.entries.map((e: any) => {
      const name = e.name || DEFAULT_EQUIP_NAMES[e.index] || '';
      return `${name}:${e.mode}:${e.ioPin}:${e.renamable ? 1 : 0}:${e.index}`;
    });
    dataCache.updateFromArm('IoNames', parts.join(','));
  });

  dataStore.on('emailSettings', (em: any) => {
    const csv = [em.enabled, em.toAddr, em.fromAddr, em.server, 0, em.port, em.username, em.password, ''].join(',');
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
    for (const e of rt.entries) { if (e.slot < 48) slots[e.slot] = e.value; }
    dataCache.updateFromArm('RunTimesData', slots.join(','));
  });

  dataStore.on('fanRuntime', (fr: any) => {
    dataCache.updateFromArm('FanRunTimeData', `${fr.dailyHours},${fr.dailyMinutes},${fr.totalHours},${fr.totalMinutes}`);
  });

  dataStore.on('humidModes', (hm: any) => {
    dataCache.updateFromArm('HumidModeData', `${hm.head1},${hm.pump1},${hm.head2},${hm.pump2}`);
  });

  dataStore.on('auxSwitches', (sw: any) => {
    dataCache.updateFromArm('AuxSwitchData', sw.states.join(','));
  });

  dataStore.on('analogBoard', (board: any) => {
    const sensorCsv = board.sensors.map((s: any) =>
      `${s.slot},${s.type},${s.label},${s.offset},${s.disabled ? 1 : 0},${s.value.toFixed(1)}`
    ).join('|');
    dataCache.updateFromArm(`AnalogBoard${board.address}`, `${board.address},${board.type},${board.label},${board.version},${board.disabled ? 1 : 0}|${sensorCsv}`);
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
    const userParts = acct.users.map((u: any) => `${u.slot},${u.userId}`).join('|');
    dataCache.updateFromArm('AcctData', `${userParts}|count=${acct.count}|loginPw=${acct.hasLoginPw ? 1 : 0}`);
  });

  dataStore.on('auxProgram', (ap: any) => {
    for (const prog of ap.programs) {
      const ruleParts = prog.rules.map((r: any) =>
        `${r.type},${r.ioIndex},${r.state},${r.op},${r.sensorValue},${r.andOr},${r.referenceIndex}`
      ).join('|');
      dataCache.updateFromArm('AuxProgram', `${prog.auxIndex},${prog.eqIndex},${prog.dutyCycle},${prog.period},${prog.units}|${ruleParts}`);
    }
  });

  dataStore.on('userLogSettings', (ul: any) => {
    dataCache.updateFromArm('recInterval', `${ul.interval},${ul.wrap}`);
  });

  dataStore.on('pidSettings', (pid: any) => {
    dataCache.updateFromArm('pidWrap', `${pid.eqIndex},${pid.wrap}`);
  });

  dataStore.on('graphFavorites', (gfav: any) => {
    dataCache.updateFromArm('GraphFavData', gfav.csv);
  });

  dataStore.on('alertSettings', (alerts: any) => {
    const bits = new Array(64).fill('0');
    for (const idx of alerts.flags) { if (idx < 64) bits[idx] = '1'; }
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

  dataStore.on('orbitStatus', (os: any) => {
    // Build a CSV for legacy compatibility: slot,dip,conn,role,zone,legacy|...
    const parts = os.boards.map((b: any) =>
      `${b.slot},${b.dipswitchId},${b.connected ? 1 : 0},${b.role},${b.zoneId},${b.legacySlot},${b.estopActive ? 1 : 0},${b.cpuTemp.toFixed(1)},${b.uptimeSecs}`
    );
    dataCache.updateFromArm('OrbitStatusData', parts.join('|'));
    
    // Update orbit boards for IOConfig ioAvailable
    dataCache.setOrbitBoards(os.boards.map((b: any) => ({
      slot: b.slot,
      role: b.role,
      connected: b.connected,
    })));

    // Broadcast orbit update via WS
    if (wsManager) wsManager.broadcastArmUpdate('OrbitStatus');
  });

  dataStore.on('orbitDiscovery', (od: any) => {
    // Full discovery with max slots info
    const parts = od.boards.map((b: any) =>
      `${b.slot},${b.dipswitchId},${b.connected ? 1 : 0},${b.role},${b.zoneId},${b.legacySlot},${b.estopActive ? 1 : 0},${b.ipAddress}`
    );
    dataCache.updateFromArm('OrbitDiscoveryData', `${od.maxSlots},${od.boardsFound}|${parts.join('|')}`);
    
    // Update orbit boards for IOConfig ioAvailable
    dataCache.setOrbitBoards(od.boards.map((b: any) => ({
      slot: b.slot,
      role: b.role,
      connected: b.connected,
    })));

    if (wsManager) wsManager.broadcastArmUpdate('OrbitDiscovery');
  });

  console.log('[NovaAdapter] Wired protobuf messages → legacy DataCache');
}

function formatTemp(val: number): string {
  return val === 0 ? '0' : val.toFixed(1);
}
