/**
 * In-memory cache of CGI variables received from the ARM controller.
 * Replaces the C-based CGI_DATA CGI[68] array in GellertServer.
 *
 * Each ARM message carries a tag (e.g. "main", "EquipStatus") and a comma-separated value string.
 * This cache stores the latest value for each tag and provides JSON accessors for the UI.
 */

import { randomUUID } from 'crypto';
import { translateAlarmData } from './warningTranslator.js';
import { loadConfig, saveConfig } from './simConfig.js';

/**
 * Get or create a persistent Nova identity UUID.
 * Survives process restarts via .sim-config/novaId.json.
 * Used so site groups survive DHCP IP changes.
 */
let _novaId: string | null = null;
export function getNovaId(): string {
  if (!_novaId) {
    const stored = loadConfig<{ novaId: string }>('novaId');
    if (stored?.novaId) {
      _novaId = stored.novaId;
    } else {
      _novaId = randomUUID();
      saveConfig('novaId', { novaId: _novaId });
      console.log(`[DataCache] Generated new novaId: ${_novaId}`);
    }
  }
  return _novaId;
}

export interface CgiEntry {
  /** ARM serial message tag (e.g. "main", "p1Plenum") */
  msgTag: string;
  /** JavaScript variable name for the UI (e.g. "MainData", "PgmData") */
  varName: string;
  /** Current comma-delimited values received from ARM */
  value: string;
  /** Timestamp of last update (ms since epoch) */
  updatedAt: number;
}

/**
 * CGI variable table mirroring the C enum CGI_INDEX and tag mapping.
 * Index → { msgTag, varName }
 *
 * Sourced from LinuxHttpServer/GellertServer/src/CGI.h and CGI.c
 */
const CGI_DEFINITIONS: Array<{ msgTag: string; varName: string }> = [
  // ═══════════════════════════════════════════════════════════════════
  // ARM CGI table — matches CgiDataInit() from GellertServer/src/CGI.c
  // msgTag = exact string the ARM uses in ^tag=value$CRC! serial frames
  // varName = internal key used by bridge data builders & REST API
  // ═══════════════════════════════════════════════════════════════════
  /*  0 CGI_MAINDATA      */ { msgTag: 'main',              varName: 'MainData' },
  /*  1 CGI_PGMDATA       */ { msgTag: 'p1Plenum',          varName: 'PgmData' },
  /*  2 CGI_DAILYFAN      */ { msgTag: 'DailyFanRuntime',   varName: 'DailyFanRun' },
  /*  3 CGI_TOTALFAN      */ { msgTag: 'TotalFanRuntime',   varName: 'TotalFanRun' },
  /*  4 CGI_OUTSIDE       */ { msgTag: 'ctrlMode',          varName: 'OutsideAirData' },
  /*  5 CGI_RUNTIMES      */ { msgTag: 'runTimes',          varName: 'RunTimesData' },
  /*  6 CGI_FREQCTRL      */ { msgTag: 'maxFanSpeed',       varName: 'FreqCtrlData' },
  /*  7 CGI_RAMPRATE      */ { msgTag: 'updTemp',           varName: 'RampRateData' },
  /*  8 CGI_HUMIDCTRL     */ { msgTag: 'selHumidType',      varName: 'HumidCtrlData' },
  /*  9 CGI_CO2PURGE      */ { msgTag: 'selPurgeMode',      varName: 'Co2PurgeData' },
  /* 10 CGI_EQUIPSTATUS   */ { msgTag: 'EquipStatus',       varName: 'EquipStatusData' },
  /* 11 CGI_MISCDATA      */ { msgTag: 'p1Misc',            varName: 'MiscData' },
  /* 12 CGI_BASICSETUP    */ { msgTag: 'StorageName',       varName: 'P2BasicSetupData' },
  /* 13 CGI_PASSWORD      */ { msgTag: 'Passwords',         varName: 'P2Password' },
  /* 14 CGI_BOARDS        */ { msgTag: 'BAdd',              varName: 'P2AnalogBoardData' },
  /* 15 CGI_FRESHAIR      */ { msgTag: 'PAirValue',         varName: 'P2FreshAirData' },
  /* 16 CGI_REFRIG        */ { msgTag: 'p2Refrigeration',   varName: 'P2RefrigData' },
  /* 17 CGI_CLIIMACELL    */ { msgTag: 'ClimacellEff',      varName: 'P2ClimaCellData' },
  /* 18 CGI_FAILURES1     */ { msgTag: 'FanMode',           varName: 'FailureData1' },
  /* 19 CGI_FAILURES2     */ { msgTag: 'OutAirMode',        varName: 'FailureData2' },
  /* 20 CGI_MODE          */ { msgTag: 'CurrentMode',       varName: 'CurrentMode' },
  /* 21 CGI_TEMPDEV       */ { msgTag: 'AlarmTempLow',      varName: 'PlenTempDevData' },
  /* 22 CGI_DATETIME      */ { msgTag: 'Date',              varName: 'DateTimeData' },
  /* 23 CGI_SERVICE       */ { msgTag: 'dealerName',        varName: 'P2ServiceData' },
  /* 24 CGI_USERLOG       */ { msgTag: 'recInterval',       varName: 'UserLogSettings' },
  /* 25 CGI_MYDISPLAY     */ { msgTag: 'MyDisplay',         varName: 'MyDisplay' },
  /* 26 CGI_IPADD         */ { msgTag: 'LocalIpAdd',        varName: 'LocalIpAdd' },
  /* 27 CGI_IPMASK        */ { msgTag: 'LocalIpMask',       varName: 'LocalIpMask' },
  /* 28 CGI_IPGATEWAY     */ { msgTag: 'LocalIpGateway',    varName: 'LocalIpGateway' },
  /* 29 CGI_IPDMODE       */ { msgTag: 'LocalIpMode',       varName: 'LocalIpMode' },
  /* 30 CGI_HTTPPORT      */ { msgTag: 'HttpPort',          varName: 'HttpPort' },
  /* 31 CGI_LTXVERSION    */ { msgTag: 'LtxVersion',        varName: 'LtxVersion' },
  /* 32 CGI_AUTHORIZE     */ { msgTag: 'dlr',               varName: 'DlrMode' },
  /* 33 CGI_BURNER        */ { msgTag: 'selBurnerMode',     varName: 'P2BurnerData' },
  /* 34 CGI_NETMONITOR    */ { msgTag: 'NetMonitorMode',    varName: 'NetMonitorEnabled' },
  /* 35 CGI_USERACCTS     */ { msgTag: 'AcctId0',           varName: 'UserAccounts' },
  /* 36 CGI_ALERTSETUP    */ { msgTag: 'AlertSetup',        varName: 'AlertSetupData' },
  /* 37 CGI_LOGTOTAL      */ { msgTag: 'LogTotal',          varName: 'LogTotal' },
  /* 38 CGI_GRAPHFAVS     */ { msgTag: 'GraphFavorites',    varName: 'GraphFavorites' },
  /* 39 CGI_FANBOOST      */ { msgTag: 'selBoostMode',      varName: 'FanBoostData' },
  /* 40 CGI_AIRCURE       */ { msgTag: 'CureStartTemp',     varName: 'AirCureData' },
  /* 41 CGI_MAC           */ { msgTag: 'MAC',               varName: 'MAC' },
  /* 42 CGI_SETTINGS      */ { msgTag: 'RestoreSettings',   varName: 'RestoreSettings' },
  /* 43 CGI_CLIMACELLTIMES*/ { msgTag: 'climacellTimes',    varName: 'P2ClimaCellTimesData' },
  /* 44 CGI_HUMIDMODES    */ { msgTag: 'HumidModes',        varName: 'HumidModes' },
  /* 45 CGI_PWMCHANNELS   */ { msgTag: 'p2PwmOutputs',      varName: 'PWMData' },
  /* 46 CGI_LOADMONITOR   */ { msgTag: 'bay1Label',         varName: 'LoadMonitorData' },
  /* 47 CGI_AVAILABLEIO   */ { msgTag: 'AvailableIo',       varName: 'AvailableIoData' },
  /* 48 CGI_OUTPUTCONFIG  */ { msgTag: 'OutputConfig',      varName: 'IoConfigOutData' },
  /* 49 CGI_INPUTCONFIG   */ { msgTag: 'InputConfig',       varName: 'IoConfigInData' },
  /* 50 CGI_AUXPROGRAM    */ { msgTag: 'AuxProgram',        varName: 'AuxProgramData' },
  /* 51 CGI_AUXSWITCHES   */ { msgTag: 'AuxSwitches',       varName: 'EquipAuxData' },

  // ═══════════════════════════════════════════════════════════════════
  // Bridge-only entries — NOT in the C CGI table.
  // Populated by: MultiMsg system or derived from composite ARM data.
  // These keep the UI data builders working without changes.
  // ═══════════════════════════════════════════════════════════════════
  /* 52 */ { msgTag: 'WarningData',       varName: 'WarningData' },
  /* 53 */ { msgTag: 'SensorList',        varName: 'SensorListData' },
  /* 54 */ { msgTag: 'FirmwareVersion',   varName: 'FirmwareVersion' },
  /* 55 */ { msgTag: 'PanelName',         varName: 'PanelName' },
  /* 56 */ { msgTag: 'AlarmData',         varName: 'AlarmData' },
  /* 57 */ { msgTag: 'StatusData',        varName: 'StatusData' },
  /* 58 */ { msgTag: 'IoNames',           varName: 'IoNamesData' },
  /* 59 */ { msgTag: 'EmailConfig',       varName: 'EmailConfigData' },
  /* 60 */ { msgTag: 'MasterSlaveData',   varName: 'MasterSlaveData' },
  /* 61 */ { msgTag: 'DoorData',          varName: 'DoorData' },
  /* 62 */ { msgTag: 'LightsData',        varName: 'LightsData' },
  /* 63 */ { msgTag: 'PidData',           varName: 'PidData' },
  /* 64 */ { msgTag: 'PileSetup',         varName: 'PileSetupData' },
  /* 65 */ { msgTag: 'AnalogAll',         varName: 'AnalogAllData' },
  /* 66 */ { msgTag: 'AnalogData',        varName: 'AnalogData' },
  /* 67 */ { msgTag: 'AccountData',       varName: 'AccountData' },
  /* 68 */ { msgTag: 'EquipSwitch',       varName: 'EquipSwitchData' },
  /* 69 */ { msgTag: 'p2Outside',         varName: 'P2OutsideData' },
  /* 70 */ { msgTag: 'p2FanSpeed',        varName: 'P2FanSpeedData' },
  /* 71 */ { msgTag: 'p2Ramp',            varName: 'P2RampData' },
  /* 72 */ { msgTag: 'p2Humid',           varName: 'P2HumidData' },
  /* 73 */ { msgTag: 'p2Co2',             varName: 'P2Co2Data' },
  /* 74 */ { msgTag: 'p2Misc',            varName: 'P2MiscData' },
  /* 75 */ { msgTag: 'p2Plenum',          varName: 'P2PlenumData' },
  /* 76 */ { msgTag: 'p2FanBoost',        varName: 'P2FanBoostData' },
  /* 77 */ { msgTag: 'p2Lights',          varName: 'P2LightsData' },
  /* 78 */ { msgTag: 'p2DoorData',        varName: 'P2DoorData' },
  /* 79 */ { msgTag: 'p2AlertSetup',      varName: 'P2AlertSetupData' },
  /* 80 */ { msgTag: 'p2Log',             varName: 'P2LogData' },
  /* 81 */ { msgTag: 'ClimaCellData',     varName: 'ClimaCellData' },
  /* 82 */ { msgTag: 'DailyBurner',       varName: 'DailyBurnerRun' },
  /* 83 */ { msgTag: 'TotalBurner',       varName: 'TotalBurnerRun' },
  /* 84 */ { msgTag: 'LogConfig',         varName: 'LogConfigData' },
  /* 85 */ { msgTag: 'RefrigData',        varName: 'RefrigData' },
  /* 86 */ { msgTag: 'BurnerData',        varName: 'BurnerData' },
  /* 87 */ { msgTag: 'p1Service',         varName: 'P1ServiceData' },
  /* 88 */ { msgTag: 'P2PWMData',         varName: 'P2PWMData' },
  /* 89 */ { msgTag: 'BoardType',         varName: 'BoardType' },
  /* 90 */ { msgTag: 'P2NodeSetupData',   varName: 'P2NodeSetupData' },
  /* 91 */ { msgTag: 'SensorLabelData',   varName: 'SensorLabelData' },
  /* 92 */ { msgTag: 'VfdAlarmData',      varName: 'VfdAlarmData' },
  /* 93 */ { msgTag: 'VfdFailureActive', varName: 'VfdFailureActive' },
];

export class DataCache {
  private entries: CgiEntry[] = [];
  private tagIndex = new Map<string, number>();
  private varNameIndex = new Map<string, number>();

  /**
   * Accumulates AuxProgram entries keyed by equipment index string ('24'..'31').
   * The ARM sends one AuxProgram at a time; this map collects all received programs
   * so the /aux/all endpoint can return the complete set.
   */
  private auxPrograms = new Map<string, string>();

  private persistTimer: ReturnType<typeof setTimeout> | null = null;
  private static readonly PERSIST_DEBOUNCE_MS = 2000;

  constructor() {
    this.entries = CGI_DEFINITIONS.map(def => ({
      msgTag: def.msgTag,
      varName: def.varName,
      value: '',
      updatedAt: 0,
    }));

    // Build fast lookup indices
    for (let i = 0; i < this.entries.length; i++) {
      this.tagIndex.set(this.entries[i].msgTag, i);
      this.varNameIndex.set(this.entries[i].varName, i);
    }

    // Restore previously persisted values
    this.loadFromDisk();
  }

  /** Restore cached values from disk (called once at construction) */
  private loadFromDisk(): void {
    const saved = loadConfig<Record<string, string>>('dataCache');
    if (!saved) return;
    let count = 0;
    for (const [varName, value] of Object.entries(saved)) {
      const idx = this.varNameIndex.get(varName);
      if (idx !== undefined && value) {
        this.entries[idx].value = value;
        this.entries[idx].updatedAt = Date.now();
        count++;
      }
    }
    if (count > 0) {
      console.log(`[DataCache] Restored ${count} cached values from disk`);
    }
  }

  /** Schedule a debounced write of all non-empty values to disk */
  private schedulePersist(): void {
    if (this.persistTimer) return; // already scheduled
    this.persistTimer = setTimeout(() => {
      this.persistTimer = null;
      const snapshot: Record<string, string> = {};
      for (const entry of this.entries) {
        if (entry.value) {
          snapshot[entry.varName] = entry.value;
        }
      }
      saveConfig('dataCache', snapshot);
    }, DataCache.PERSIST_DEBOUNCE_MS);
  }

  /** Update a CGI variable from an ARM serial message */
  updateFromArm(tag: string, value: string): boolean {
    const idx = this.tagIndex.get(tag);
    if (idx === undefined) {
      // Unknown tag — might be a control message (ACK, NAK, etc.)
      return false;
    }
    this.entries[idx].value = value;
    this.entries[idx].updatedAt = Date.now();

    // Persist to disk (debounced)
    this.schedulePersist();

    // Accumulate AuxProgram entries by equipment index.
    // The ARM sends one aux program per AuxProgram message; we collect them all
    // so the aux page can display/navigate between all defined programs.
    if (tag === 'AuxProgram' && value) {
      const commaIdx = value.indexOf(',');
      const header = commaIdx > 0 ? value.slice(0, commaIdx) : value;
      const eqIndex = header.split(':')[0];
      if (eqIndex && !isNaN(Number(eqIndex)) && !header.includes('undefined')) {
        this.auxPrograms.set(eqIndex, value);
      }
    }

    return true;
  }

  /** Get all accumulated AuxProgram entries keyed by eqIndex string */
  getAllAuxPrograms(): Map<string, string> {
    return this.auxPrograms;
  }

  /** Get a CGI entry by its CGI index */
  getByIndex(index: number): CgiEntry | undefined {
    return this.entries[index];
  }

  /** Get a CGI entry by ARM message tag */
  getByTag(tag: string): CgiEntry | undefined {
    const idx = this.tagIndex.get(tag);
    return idx !== undefined ? this.entries[idx] : undefined;
  }

  /** Get a CGI entry by JavaScript variable name */
  getByVarName(varName: string): CgiEntry | undefined {
    const idx = this.varNameIndex.get(varName);
    return idx !== undefined ? this.entries[idx] : undefined;
  }

  /** Resolve a CGI index to its JSON key-value for the UI (index-based access like GetJsonItem) */
  getJsonItem(index: number): Record<string, string> | null {
    const entry = this.entries[index];
    if (!entry || !entry.value) return null;
    return { [entry.varName]: entry.value };
  }

  // ─────────────────────────────────────────────
  // IO Config format conversion utilities
  // ─────────────────────────────────────────────

  /**
   * Convert port-indexed OutputConfig/InputConfig to eq-indexed format.
   *
   * The ARM simulator stores configs in port-indexed format:
   *   portIndexed[portId] = equipmentIndex (or '-1')
   * The IO config page expects this format (reads outputConfig[PID]).
   *
   * But the equipment status page and failures page expect eq-indexed format:
   *   eqIndexed[equipmentIndex] = portId (or '-1')
   * This matches the original firmware convention (EquipIo[eqIdx].Output = portId).
   *
   * This method converts port-indexed → eq-indexed.
   */
  portToEqIndexed(portIndexed: string[]): string[] {
    // Max equipment index that might appear as a value
    const maxEq = Math.max(portIndexed.length, 60);
    const eqIndexed = new Array(maxEq).fill('-1');
    for (let portId = 0; portId < portIndexed.length; portId++) {
      const eqIdx = parseInt(portIndexed[portId], 10);
      if (eqIdx >= 0 && eqIdx < maxEq) {
        eqIndexed[eqIdx] = String(portId);
      }
    }
    return eqIndexed;
  }

  /**
   * Get eq-indexed OutputConfig from port-indexed cache data.
   * Used by equipment-status and failures pages.
   */
  getEqIndexedOutputConfig(): string[] {
    const raw = this.getByVarName('IoConfigOutData')?.value ?? '';
    return raw ? this.portToEqIndexed(raw.split(',')) : [];
  }

  /**
   * Get eq-indexed InputConfig from port-indexed cache data.
   * Used by failures pages.
   */
  getEqIndexedInputConfig(): string[] {
    const raw = this.getByVarName('IoConfigInData')?.value ?? '';
    return raw ? this.portToEqIndexed(raw.split(',')) : [];
  }

  /**
   * Get EquipStatusData array with VFD fault overrides applied.
   * When VFD drive faults are active, zeros all equipment outputs and
   * activates red/yellow lights — matching firmware failure mode behavior.
   */
  getEquipStatus(): string[] {
    const raw = this.getByVarName('EquipStatusData')?.value ?? '';
    const eq = raw ? raw.split(',') : [];
    const vfdFailure = this.getByVarName('VfdFailureActive')?.value ?? '';
    if (vfdFailure === '1' && eq.length > 35) {
      // Zero all equipment outputs (same as firmware ST_FAILURE)
      eq[2] = '0';               // fan output
      eq[5] = '0';               // climacell output
      eq[7] = '0';               // burner output
      eq[10] = '0'; eq[11] = '0'; // humid1 head/pump
      eq[13] = '0'; eq[14] = '0'; // humid2 head/pump
      for (let i = 17; i <= 26; i++) eq[i] = '0'; // refrig stages + defrost
      eq[28] = '0';              // heat output
      eq[32] = '0';              // cavity heat output
      // Status lights
      eq[33] = '0';              // green (fan) → off
      eq[34] = '1';              // yellow → alarm active
      eq[35] = '1';              // red → failure active
      // Humid3 head/pump (indices from armSimulator buildEquipStatus)
      if (eq.length > 78) { eq[77] = '0'; eq[78] = '0'; }
      // Aux outputs
      for (const i of [61,63,65,67,69,71,73,75]) {
        if (i < eq.length) eq[i] = '0';
      }
    }
    return eq;
  }

  // ─────────────────────────────────────────────
  // Composite data builders for WebSocket channels
  // ─────────────────────────────────────────────

  /**
   * Build the "frontmatter-data" JSON payload.
   *
   * The Svelte UI expects specific short keys with values that are either
   * string arrays (comma-split) or scalar strings. The old Node.js server
   * assembled composite arrays from multiple CGI variables. We replicate
   * that shape here.
   *
   * Keys consumed by the UI:
   *   main        string[]  (40 elements — temps, setpoints, outputs, sensors)
   *   panel       string[]  (31 elements — runtimes, equipment status, mode)
   *   animations  string    ('true' | 'false')
   *   misc        string[]  (board type at [0])
   *   AlarmData   string[]  (list of active alarm strings)
   *   refrigData  string[]  (refrigeration data)
   *   hasAux      string    ('true' | 'false')
   *   hasPileSensor string  ('true' | 'false')
   *   keyboardType  string  ('0' | '1')
   *   localLogin    string  ('true' | 'false')
   *   hasLevel1Password string ('true' | 'false')
   */
  buildFrontmatterData(): Record<string, string | string[] | number> {
    // Helper: get cached value split into an array, or fallback
    const getArr = (vn: string): string[] => {
      const v = this.getByVarName(vn)?.value;
      return v ? v.split(',') : [];
    };
    const getVal = (vn: string): string => this.getByVarName(vn)?.value ?? '';

    const normalizeCo2 = (value?: string): string | undefined => {
      if (!value) return value;
      const trimmed = value.trim();
      const match = trimmed.match(/^([0-9]+)\.([0-9]+)$/);
      if (match && /^[0]+$/.test(match[2])) {
        return match[1];
      }
      return trimmed;
    };

    // --- Build composite `main` array (40 elements) ---
    // Interleaves MainData fields with setpoints (PgmData), cure flags, CO2 data, etc.
    const rawMain = getArr('MainData');      // 15 fields from ARM UI_SendMain()
    const rawPgm  = getArr('PgmData');       // PgmData setpoints
    const rawCo2  = getArr('Co2PurgeData');  // CO2 purge settings
    const rawEquip = getArr('EquipStatusData');

    // ── Sensor data overlay ──
    // In QEMU the firmware can't read RS-485 sensor boards, so MainData
    // fields 0-9 arrive as '--'.  Fall back to SensorListData (stride 6:
    // Label,SID,Type,Value,Offset,Disabled) injected from the RS485 responder.
    //
    // Temperature unit: firmware sends temps already converted to the
    // user-selected TempType (matches legacy AS2 contract — Sensor.Value
    // is always in display units, never raw °C).  No conversion needed
    // here.  P2BasicSetupData[1] still controls the unit label only.

    const sensorArr = getArr('SensorListData');
    const sensorBySid = new Map<number, string>();
    for (let i = 0; i + 5 < sensorArr.length; i += 6) {
      const sid = parseInt(sensorArr[i + 1], 10);
      const disabled = sensorArr[i + 5] === '1';
      if (!isNaN(sid) && !disabled) {
        sensorBySid.set(sid, sensorArr[i + 3]);
      }
    }
    const sensorVal = (sid: number): string | undefined => sensorBySid.get(sid);
    const isMissing = (v?: string): boolean => !v || v === '--' || v === 'dis';

    // Compute averaged plenum temp from SID 0 (Plenum 1) & SID 1 (Plenum 2)
    const computePlenumTemp = (): string | undefined => {
      const v0 = sensorVal(0), v1 = sensorVal(1);
      const n0 = v0 ? parseFloat(v0) : NaN, n1 = v1 ? parseFloat(v1) : NaN;
      if (!isNaN(n0) && !isNaN(n1)) return ((n0 + n1) / 2).toFixed(1);
      if (!isNaN(n0)) return n0.toFixed(1);
      if (!isNaN(n1)) return n1.toFixed(1);
      return undefined;
    };

    // Apply sensor fallbacks where firmware sends '--' or nothing
    const plenumTemp  = isMissing(rawMain[0]) ? (computePlenumTemp() ?? rawMain[0]) : rawMain[0];
    const plenumHumid = isMissing(rawMain[1]) ? (sensorVal(5) ?? rawMain[1]) : rawMain[1]; // SID 5 = Plenum RH
    const outsideTemp = isMissing(rawMain[2]) ? (sensorVal(2) ?? rawMain[2]) : rawMain[2]; // SID 2 = Outside
    const outsideHumid= isMissing(rawMain[4]) ? (sensorVal(4) ?? rawMain[4]) : rawMain[4]; // SID 4 = Outside RH
    const returnHumid = isMissing(rawMain[7]) ? (sensorVal(6) ?? rawMain[7]) : rawMain[7]; // SID 6 = Return RH
    const returnTemp  = isMissing(rawMain[8]) ? (sensorVal(3) ?? rawMain[8]) : rawMain[8]; // SID 3 = Return
    const co2Reading  = isMissing(rawMain[9]) ? (sensorVal(7) ?? rawMain[9]) : rawMain[9]; // SID 7 = CO2

    // MainData indices (from ARM UI_SendMain):
    //  0=PlenumTemp, 1=PlenumHumid, 2=OutsideTemp, 3=RemoteTemp,
    //  4=OutsideHumid, 5=RemoteHumid, 6=StartTemp, 7=ReturnHumid,
    //  8=ReturnTemp, 9=Co2, 10=FanSpeed, 11=CoolOutput, 12=CoolLabel,
    //  13=BurnerOutput, 14=CalcHumid
    const main: string[] = new Array(40).fill('dis');
    //  [0]  = plenum temp raw (from MainData[0])
    main[0]  = plenumTemp ?? 'dis';
    //  [1]  = plenum humidity raw
    main[1]  = plenumHumid ?? 'dis';
    //  [2]  = plenum temperature
    main[2]  = plenumTemp ?? 'dis';
    //  [3]  = plenum temp setpoint (PgmData[0]) — ARM sends in display units
    main[3]  = rawPgm[0] ?? 'dis';
    //  [4]  = (spare)
    main[4]  = '0';
    //  [5]  = plenum humidity
    main[5]  = plenumHumid ?? 'dis';
    //  [6]  = humidity setpoint (PgmData[1])
    main[6]  = rawPgm[1] ?? 'dis';
    //  [7]  = outside temperature
    main[7]  = outsideTemp ?? 'dis';
    //  [8]  = outside humidity
    main[8]  = outsideHumid ?? 'dis';
    //  [9]  = return temperature #1
    main[9]  = returnTemp ?? 'dis';
    //  [10] = return humidity #1
    main[10] = returnHumid ?? 'dis';
    //  [11] = cure start temp
    main[11] = rawMain[6] ?? 'dis';
    //  [12] = cure start humidity
    main[12] = rawMain[5] ?? 'dis';
    //  [13] = (spare)
    main[13] = '0';
    //  [14] = current fan speed
    main[14] = rawMain[10] ?? 'Off';
    //  [15] = cooling output
    main[15] = rawMain[11] ?? '0';
    //  [16] = output type (0=cool, 1=refrig, 2=burner, 3=diag)
    main[16] = rawMain[12] === 'Auto' ? '0' : rawMain[12] ?? '0';
    //  [17] = CO2 reading #1
    main[17] = normalizeCo2(co2Reading) ?? 'dis';
    //  [18] = cure/burner output
    main[18] = rawMain[13] ?? '0';
    //  [19-23] = spare
    main[19] = '0'; main[20] = '0'; main[21] = '0'; main[22] = '0'; main[23] = '0';
    //  [24] = cure flag 1 (0=off)
    main[24] = '0';
    //  [25] = cure flag 2 (0=off)
    main[25] = '0';
    //  [26-28] = spare
    main[26] = '0'; main[27] = '0'; main[28] = '0';
    //  [29] = humid cure control flag
    main[29] = '0';
    //  [30] = spare
    main[30] = '0';
    //  [31] = return humid calc
    main[31] = rawMain[14] ?? 'dis';
    //  [32] = CO2 mode active (0 or 1)
    main[32] = (rawCo2[0] === '0' || rawCo2[0] === 'Off' || !rawCo2[0]) ? '0' : '1';
    //  [33] = CO2 setpoint (raw[4] = SetOrCycle; for auto mode this is the ppm threshold)
    main[33] = rawCo2[4] ?? '1000';
    //  [34] = refrig output (populated when CoolLabel='1', i.e. firmware is reporting refrig output)
    main[34] = (rawMain[12] === '1') ? (rawMain[11] ?? '0') : '0';
    //  [35] = return humidity #2 (dis if not installed)
    main[35] = 'dis';
    //  [36] = CO2 #2 (dis if not installed)
    main[36] = 'dis';
    //  [37] = return temp #2 (dis if not installed)
    main[37] = 'dis';
    //  [38] = moisture loss #1
    main[38] = '--';
    //  [39] = moisture loss #2
    main[39] = '--';

    // --- Build composite `panel` array (31 elements) ---
    const rawBasic = getArr('P2BasicSetupData');
    const rawMaster = getArr('MasterSlaveData');

    const panel: string[] = new Array(31).fill('0');
    //  [0]  = blend/available temperature (StartTemp from MainData[6])
    panel[0]  = rawMain[6] ?? '0';
    //  [1]  = daily fan runtime
    panel[1]  = getVal('DailyFanRun') || '0.0';
    //  [2-7] = total runtimes from TotalFanRuntime (6 CSV fields):
    //          fan, refrigeration, cooling, recirculation, cure, standby
    const totalParts = (getVal('TotalFanRun') || '').split(',');
    panel[2]  = totalParts[0] || '0';   // total fan runtime
    panel[3]  = totalParts[1] || '0';   // refrigeration runtime
    panel[4]  = totalParts[2] || '0';   // cooling runtime
    panel[5]  = totalParts[3] || '0';   // recirculation runtime
    panel[6]  = totalParts[4] || '0';   // cure/heating runtime
    panel[7]  = totalParts[5] || '0';   // standby runtime
    //  [8]  = system mode (0=potato, 1=onion)  from P2BasicSetupData[4]
    panel[8]  = rawBasic[4] ?? '0';
    //  [9-12] = climacell equipment/switch/input/output
    panel[9]  = rawEquip[0] ?? '0';
    panel[10] = rawEquip[3] ?? '0';
    panel[11] = rawEquip[4] ?? '0';
    panel[12] = rawEquip[5] ?? '0';
    //  [13] = humidifier switch
    panel[13] = rawEquip[8] ?? '0';
    //  [14-17] = humidifier #1 equip/input/headOut/pumpOut
    panel[14] = '0'; panel[15] = rawEquip[9] ?? '0';
    panel[16] = rawEquip[10] ?? '0'; panel[17] = rawEquip[11] ?? '0';
    //  [18-21] = humidifier #2
    panel[18] = '0'; panel[19] = rawEquip[12] ?? '0';
    panel[20] = rawEquip[13] ?? '0'; panel[21] = rawEquip[14] ?? '0';
    //  [22-25] = humidifier #3
    panel[22] = '0'; panel[23] = rawEquip[76] ?? '0';
    panel[24] = rawEquip[77] ?? '0'; panel[25] = rawEquip[78] ?? '0';
    //  [26-29] = bay lights
    panel[26] = '0'; panel[27] = '0';
    panel[28] = rawEquip[56] ?? '0'; panel[29] = rawEquip[58] ?? '0';
    //  [30] = master/slave status (0=none, 1=master, 2=slave)
    panel[30] = rawMaster[0] ?? '0';

    // --- VFD Fault override: zero all equipment outputs like firmware failure mode ---
    // Only activates after the programmed fan fail delay has elapsed
    const vfdFailureFlag = getVal('VfdFailureActive');
    if (vfdFailureFlag === '1') {
      main[14] = 'Off';                  // fan speed → Off
      main[15] = '0';                    // cooling output → 0
      main[18] = '0';                    // burner output → 0
      panel[10] = '0';                   // climacell switch → off
      panel[12] = '0';                   // climacell output → off
      panel[13] = '0';                   // humidifier switch → off
      panel[16] = '0'; panel[17] = '0'; // humidifier 1 head/pump → off
      panel[20] = '0'; panel[21] = '0'; // humidifier 2 head/pump → off
      panel[24] = '0'; panel[25] = '0'; // humidifier 3 head/pump → off
    }

    // --- Misc ---
    const rawMisc = getArr('MiscData');

    // Board type: comes from ARM Initialize handshake (stored as 'BoardType')
    // 'Agri-Star' = original board (all icons show without equipment-defined check)
    // 'AS2' = MiniIO board (icons only show for configured equipment)
    const boardType = getVal('BoardType') || 'Agri-Star';

    // The deployed (compiled) Svelte code reads boardType from misc[0], not from
    // the separate boardType field.  Override misc[0] with the board-type string
    // so the UI can distinguish AS2 from Agri-Star boards.
    if (rawMisc.length === 0) rawMisc.push(boardType);
    else rawMisc[0] = boardType;

    // --- AlarmData ---
    // Raw firmware format: "36&1,1,0,45&1,4,0,..." (index&status,value0,value1,...)
    // Translate to human-readable "WARN_KEY=Text" entries matching GellertServerD output
    const alarmRaw = getVal('AlarmData');
    const ioNames = getVal('IoNamesData');
    const alarmData: string[] = alarmRaw ? translateAlarmData(alarmRaw, ioNames || undefined) : [];

    // --- VFD Drive Fault Alarms ---
    // When fanControlMode is 'vfd', inject alarms for any faulted VFD drives.
    // These use the same KEY=text format as translated alarms.
    const vfdAlarms = getVal('VfdAlarmData');
    if (vfdAlarms) {
      alarmData.push(...vfdAlarms.split('|').filter(s => s.length > 0));
    }

    // --- Derived flags ---
    const auxRaw = getVal('EquipAuxData');
    const hasAux = auxRaw ? (auxRaw.split(',').some(v => v !== 'Off' && v !== '0' && v !== '') ? 'true' : 'false') : 'false';
    // rawBasic[10] = Animations flag. Default to 'true' when the field is missing/unset.
    const animations = (rawBasic[10] === undefined || rawBasic[10] === '' || rawBasic[10] === '1') ? 'true' : 'false';
    // rawBasic[9] = loginSecure (Require Remote Login). Drives the System Login overlay.
    const localLogin = rawBasic[9] === '1' ? 'true' : 'false';
    // UserAccounts[11] = Level 1 password set flag.
    const rawUsers = getArr('UserAccounts');
    const hasLevel1Password = (rawUsers.length >= 12 && rawUsers[11] === '1') ? 'true' : 'false';
    const refrigRaw = getArr('P2RefrigData');
    const refrigData = refrigRaw.length > 0 ? refrigRaw : getArr('RefrigData');

    // --- Hide unassigned equipment using eq-indexed OutputConfig ---
    // eqIndexed[eqId] = portId, or '-1' if unassigned.
    const eqOutCfg = this.getEqIndexedOutputConfig();
    if (eqOutCfg.length > 0) {
      // Humidifier "equip defined" — UI checks panel[14/18/22] !== '-1'
      // OutputConfig equipment IDs: 7=Humid1Head, 9=Humid2Head, 11=Humid3Head
      panel[14] = eqOutCfg[7]  ?? '-1';  // Humid1Head
      panel[18] = eqOutCfg[9]  ?? '-1';  // Humid2Head
      panel[22] = eqOutCfg[11] ?? '-1';  // Humid3Head

      // Bay lights "equip defined" — panel[26/27] likewise
      panel[26] = eqOutCfg[23] ?? '-1';  // Lights1
      panel[27] = eqOutCfg[24] ?? '-1';  // Lights2

      // Refrigeration stages: hide unassigned
      // EQ_REFRIG_STAGE1=13 .. EQ_REFRIG_STAGE8=20
      for (let stage = 0; stage < 8; stage++) {
        const eqId = 13 + stage;
        if (eqId < eqOutCfg.length && eqOutCfg[eqId] === '-1') {
          if (stage < refrigData.length) {
            refrigData[stage] = '-1';
          }
        }
      }
    }

    // Determine hasPileSensor from AnalogAllData (stride 5: addr, type, label, ver, disabled).
    // The firmware never sends a PileSetup tag — that was armSimulator-only.
    // If there are more than the 2 default boards (temp + humid), pile sensors exist.
    const analogAll = getArr('AnalogAllData');
    const boardCount = analogAll.length >= 5 ? Math.floor(analogAll.length / 5) : 0;
    const hasPileSensor = boardCount > 2 ? 'true' : 'false';

    // Triton presence — derived from orbit-board discovery so the level-2
    // refrigeration page (which now hosts the Triton SCADA UI) is reachable
    // even when the legacy GRC refrig stage table is empty.
    const hasTriton = this.orbitBoards.some(b => b.role === 3 && b.connected) ? 'true' : 'false';

    return {
      main,
      panel,
      animations,
      misc: rawMisc.length > 0 ? rawMisc : ['0'],
      boardType,
      AlarmData: alarmData,
      refrigData: refrigData.length > 0 ? refrigData : ['0'],
      hasAux,
      hasPileSensor,
      hasTriton,
      keyboardType: '0',
      localLogin,
      hasLevel1Password,
    };
  }

  /**
   * Build the "header-data" JSON payload.
   * Expected shape: { DateTime: string[], CurrentMode: number, PanelName: string }
   *
   * The Svelte UI accesses DateTime as a 3-element array:
   *   [0] = date (e.g. "02/25/2026")
   *   [1] = time (e.g. "10:30:00")
   *   [2] = AM/PM flag ("0" = AM, "1" = PM)
   *
   * CurrentMode is a numeric index into the modeToColor map.
   */
  buildHeaderData(): Record<string, string | string[] | number> {
    // Parse DateTime — ARM sends "02/25/2026,10:30:00,0" as one comma-string
    const dtRaw = this.getByVarName('DateTimeData')?.value ?? '';
    let dateTimeArr: string[];
    if (dtRaw.includes(',')) {
      dateTimeArr = dtRaw.split(',');
    } else if (dtRaw) {
      // Fallback: build from current time
      const now = new Date();
      const h = now.getHours();
      dateTimeArr = [
        `${String(now.getMonth() + 1).padStart(2, '0')}/${String(now.getDate()).padStart(2, '0')}/${now.getFullYear()}`,
        `${String(h > 12 ? h - 12 : h || 12).padStart(2, '0')}:${String(now.getMinutes()).padStart(2, '0')}:${String(now.getSeconds()).padStart(2, '0')}`,
        h >= 12 ? '1' : '0',
      ];
    } else {
      const now = new Date();
      const h = now.getHours();
      dateTimeArr = [
        `${String(now.getMonth() + 1).padStart(2, '0')}/${String(now.getDate()).padStart(2, '0')}/${now.getFullYear()}`,
        `${String(h > 12 ? h - 12 : h || 12).padStart(2, '0')}:${String(now.getMinutes()).padStart(2, '0')}:${String(now.getSeconds()).padStart(2, '0')}`,
        h >= 12 ? '1' : '0',
      ];
    }

    // Parse CurrentMode — ARM sends numeric mode index (e.g. "4" for Cooling)
    const modeRaw = this.getByVarName('CurrentMode')?.value ?? '0';
    let currentMode = parseInt(modeRaw, 10) || 0;

    // VFD fault override: force FAILURE mode (21) when VFD failure delay has expired
    const vfdFailure = this.getByVarName('VfdFailureActive')?.value ?? '';
    if (vfdFailure === '1') {
      currentMode = 21;
    }

    return {
      DateTime: dateTimeArr,
      CurrentMode: currentMode,
      PanelName: this.getByVarName('PanelName')?.value || 'Agristar Panel',
    };
  }

  /**
   * Build TCP/IP node data for the header dropdown and remote systems page.
   * Includes the current system plus any configured remote nodes from P2NodeSetupData.
   * Each node has an `id` field (persistent UUID) so site groups survive DHCP IP changes.
   */
  buildTcpIpData(): { nodes: Array<{ text: string; value: string; id: string }>; localIP: string } {
    const panelName = this.getByVarName('PanelName')?.value || 'Agristar Panel';
    const localIP = this.getByVarName('LocalIpAdd')?.value?.split(',')[0] || '127.0.0.1';
    const nodes: Array<{ text: string; value: string; id: string }> = [];

    // Always include current system with its persistent novaId
    nodes.push({ text: panelName, value: `${localIP}:80`, id: getNovaId() });

    // Add any configured remote nodes (stride-4: name, host, port, status)
    // Remote nodes get their fetched novaId if known, otherwise address-based fallback
    const nodeRaw = this.getByVarName('P2NodeSetupData')?.value ?? '';
    if (nodeRaw) {
      const parts = nodeRaw.split(',');
      for (let i = 0; i + 2 < parts.length; i += 4) {
        const name = parts[i];
        const host = parts[i + 1];
        const port = parts[i + 2];
        if (name && host && port) {
          const addr = `${host}:${port}`;
          const remoteId = this.remoteNovaIds.get(addr) ?? addr;
          nodes.push({ text: name, value: addr, id: remoteId });
        }
      }
    }

    return { nodes, localIP };
  }

  /** Cache of remote system novaIds, keyed by address (host:port) */
  private remoteNovaIds = new Map<string, string>();

  /** Store a remote system's novaId (called when identity is fetched) */
  setRemoteNovaId(address: string, novaId: string): void {
    this.remoteNovaIds.set(address, novaId);
  }

  /** Orbit board discovery data for Constellation mode */
  private orbitBoards: Array<{ slot: number; role: number; connected: boolean }> = [];

  /**
   * Store discovered orbit boards (called from novaAdapter or orbitClient).
   * @param boards Array of { slot, role, connected } from orbit discovery
   */
  setOrbitBoards(boards: Array<{ slot: number; role: number; connected: boolean }>): void {
    this.orbitBoards = boards;
  }

  getOrbitBoards(): Array<{ slot: number; role: number; connected: boolean }> {
    return this.orbitBoards;
  }

  /**
   * Build ioAvailable for Constellation with Orbit boards.
   * Each discovered orbit board maps to an entry.
   * Slot 0 is always the MAIN storage board (even if not yet discovered).
   *
   * Format: "label:numOutputs:numInputs:boardType"
   * - boardType 1 = Storage (standard 10 DI / 10 DO)
   * - boardType 2 = GDC / Airlock (5 actuators with open/close I/O)
   * - boardType 3 = Triton (refrigeration)
   * - boardType 4 = Pulsar (advanced I/O)
   */
  private buildOrbitAvailableIo(): string[] {
    const result: string[] = [];

    // Always ensure at least the MAIN storage board (slot 0)
    const hasSlot0 = this.orbitBoards.some(b => b.slot === 0);
    if (!hasSlot0) {
      result.push('Orbit 1:10:10:1');
    }

    // Sort by slot for consistent ordering
    const sorted = [...this.orbitBoards].sort((a, b) => a.slot - b.slot);

    for (const b of sorted) {
      const num = result.length + 1;
      const boardType = b.role || 1;
      let label: string;
      switch (b.role) {
        case 2:  label = `GDC ${num}`; break;
        case 3:  label = `Triton ${num}`; break;
        case 4:  label = `Pulsar ${num}`; break;
        default: label = `Orbit ${num}`; break;
      }
      // Disconnected boards still appear so their I/O config slots are preserved
      result.push(`${label}:10:10:${boardType}`);
    }

    return result;
  }

  /**
   * Build ioAvailable for the Level-2 4-20mA / PWM page.
   * Same labels as buildOrbitAvailableIo() but the 4th field is the number of
   * PWM-capable analog outputs per board (Orbit hardware exposes 2 AOs that
   * can be jumpered for 0-10V or 4-20mA — both usable as PWM-style outputs).
   * We must NOT reuse buildOrbitAvailableIo() here because that one packs
   * boardType into index 3 for ioconfig's benefit; the PWM page reads index 3
   * as numPwms (entry format: name:numOuts:numIns:numPwms).
   */
  private buildOrbitAvailableIoForPwm(): string[] {
    const PWMS_PER_ORBIT = 2;
    const result: string[] = [];

    const hasSlot0 = this.orbitBoards.some(b => b.slot === 0);
    if (!hasSlot0) {
      result.push(`Orbit 1:10:10:${PWMS_PER_ORBIT}`);
    }

    const sorted = [...this.orbitBoards].sort((a, b) => a.slot - b.slot);
    for (const b of sorted) {
      const num = result.length + 1;
      let label: string;
      switch (b.role) {
        case 2:  label = `GDC ${num}`; break;
        case 3:  label = `Triton ${num}`; break;
        case 4:  label = `Pulsar ${num}`; break;
        default: label = `Orbit ${num}`; break;
      }
      result.push(`${label}:10:10:${PWMS_PER_ORBIT}`);
    }

    return result;
  }

  /**
   * Build page-specific data by endpoint name.
   * Each page expects a specific JSON shape — not raw CGI variable keys.
   */
  buildPageData(pageName: string): any {
    // Helper: get cached value split into an array
    const getArr = (vn: string): string[] => {
      const v = this.getByVarName(vn)?.value;
      return v ? v.split(',') : [];
    };
    const getVal = (vn: string): string => this.getByVarName(vn)?.value ?? '';

    switch (pageName) {
      // ── outside ─────────────────────────────────────
      // { outside: string[], cure: string[], dev: string, sensors: SensorInfo[] }
      case 'outside': {
        /* OutsideAirData is stored in the legacy &-delimited named-field
         * wire format, e.g.
         *   "0&OutsideAirSet=2&selAboveBelow=1&selTempRef=255&calcHumid=0&SessionID=0"
         * where the first token is ctrlMode (positional) and the rest
         * are key=value pairs.  Parse it into the layout the UI expects:
         *   outside[0] = OutsideAirSet      (cooling-start differential)
         *   outside[1] = selAboveBelow      (0=above, 1=below)
         *   outside[2] = selTempRef         (255=plenum setpoint, 254=return #1, else sensor id)
         *   outside[3] = calcHumid          (0=plenum, 1=calculated)
         *   outside[4] = ctrlMode           (0=outside air, 1=plenum)  */
        const rawStr = this.getByVarName('OutsideAirData')?.value ?? '';
        const tokens = rawStr.split('&');
        let ctrlMode = '0';
        const named: Record<string, string> = {};
        tokens.forEach((tok, idx) => {
          const eq = tok.indexOf('=');
          if (eq < 0) {
            if (idx === 0) ctrlMode = tok;
          } else {
            named[tok.slice(0, eq)] = tok.slice(eq + 1);
          }
        });
        const outside = [
          named['OutsideAirSet'] ?? '2.0',
          named['selAboveBelow']  ?? '0',
          named['selTempRef']     ?? '255',
          named['calcHumid']      ?? '0',
          ctrlMode || '0',
        ];

        const cureRaw = getArr('AirCureData');  // CureStartTemp: StartTemp,HumidRef,StartHumid,HumidHighLimit
        const devArr = getArr('PlenTempDevData'); // AlarmTempLow: first field is LowAlarmTemp
        const sensorRaw = getArr('SensorListData');
        // cure[0]=startTemp, [1]=humidRef, [2]=startHumid, [3]=highLimit
        const cure = [
          cureRaw[0] ?? '32.0', cureRaw[1] ?? '0', cureRaw[2] ?? '80', cureRaw[3] ?? '35',
        ];
        const dev = devArr[0] ?? '1.0';
        // Build sensor list for dropdown — emit stride-6 legacy array
        // (Label, SID, Type, Value, Offset, Disabled) so the outside page's
        // parseSensorFeeds() consumer can ingest it.
        const sensors: string[] = [];
        for (let i = 0; i + 5 < sensorRaw.length; i += 6) {
          sensors.push(
            sensorRaw[i] ?? '',
            sensorRaw[i + 1] ?? '',
            sensorRaw[i + 2] ?? '255',
            sensorRaw[i + 3] ?? '--',
            sensorRaw[i + 4] ?? '0',
            sensorRaw[i + 5] ?? '0'
          );
        }
        return { outside, cure, dev, sensors };
      }

      // ── co2 ─────────────────────────────────────────
      // string[] (at least 8 elements)
      case 'co2': {
        // ARM sends: selPurgeMode=Mode,MinTemp,MaxTemp,Duration,SetOrCycle,FanOut,DoorOut
        // raw[4] is Co2Set (auto mode=2) or CycleTime (manual mode=1)
        const raw = getArr('Co2PurgeData');
        const mode = raw[0] ?? '0';
        const isAuto = mode === '2';
        // UI array: [mode, minTemp, maxTemp, duration, hoursSince, fanOut%, doorOut%, co2Setpoint]
        return [
          mode,
          raw[1] ?? '0',                         // minTemp
          raw[2] ?? '35',                         // maxTemp
          raw[3] ?? '5',                          // duration
          isAuto ? '0' : (raw[4] ?? '0'),         // hoursSince (manual cycle time)
          raw[5] ?? '100',                        // fanOut%
          raw[6] ?? '100',                        // doorOut%
          isAuto ? (raw[4] ?? '450') : '0',       // co2Setpoint (auto threshold)
        ];
      }

      // ── humidifier ──────────────────────────────────
      // { control: string[] (21 elements), boardType: string, humidStatus: string[] }
      case 'humidifier': {
        const raw = getArr('HumidCtrlData');    // selHumidType: all 21 fields (3 humidifiers x 7)
        const misc = getArr('MiscData');
        const equip = getArr('EquipStatusData');
        // 21-element flat array: 3 humidifiers x 7 fields each
        // ARM sends all 21 fields in a single selHumidType tag
        const control = new Array(21).fill('0');
        for (let i = 0; i < raw.length && i < 21; i++) control[i] = raw[i] ?? '0';
        const boardType = getVal('BoardType') || 'Agri-Star';
        // humidStatus: [humid1Input, humid2Input, humid3Input, humid1RemoteOff, humid2RemoteOff, humid3RemoteOff]
        // Production C code indices: Humid1Input=eq[9], Humid2Input=eq[12], Humid3Input=eq[76],
        //                             Humid1RemoteOff=eq[39], Humid2RemoteOff=eq[40], Humid3RemoteOff=eq[93]
        const humidStatus = [
          equip[9]  ?? '0', equip[12] ?? '0', equip[76] ?? '0',
          equip[39] ?? '0', equip[40] ?? '0', equip[93] ?? '0',
        ];
        return { control, boardType, humidStatus };
      }

      // ── climacelltimes ──────────────────────────────
      // Object with numeric keys -> values (runtime operation codes for RunTime component)
      case 'climacelltimes': {
        const raw = getArr('P2ClimaCellTimesData');
        const result: Record<string, string> = {};
        // 48 half-hour slots (24 hrs x 2)
        for (let i = 0; i < 48; i++) {
          result[String(i)] = raw[i] ?? '1';  // '1'=off default
        }
        return result;
      }

      // ── lights ──────────────────────────────────────
      // { name1: string, name2: string }
      case 'lights': {
        const loadMon = getArr('LoadMonitorData');  // bay1Label: Bay1Label,Bay2Label,...
        const raw = getArr('LightsData');
        return { name1: loadMon[0] ?? raw[0] ?? 'Bay Light 1', name2: loadMon[1] ?? raw[1] ?? 'Bay Light 2' };
      }

      // ── ramp ────────────────────────────────────────
      // { rate: string[], plenum: string, pile: string[] }
      case 'ramp': {
        const raw = getArr('RampRateData');     // updTemp: ChangeAmt,UpdatePeriod,TempDiff,TempRef,TargetTemp
        const pgm = getArr('PgmData');
        const sensorRaw = getArr('SensorListData');
        // rate: [0]=changeAmt, [1]=updateHours, [2]=tempDiff, [3]=tempRefSelect, [4]=targetTemp
        const rate = [
          raw[0] ?? '0.5', raw[1] ?? 'Automatically', raw[2] ?? '1.0',
          raw[3] ?? '0', raw[4] ?? '7.0',
        ];
        const plenum = pgm[0] ?? '7.0';
        // pile: label+value pairs for dropdown (from sensor list)
        const pile: string[] = [];
        for (let i = 0; i < sensorRaw.length; i += 6) {
          if (sensorRaw[i] && sensorRaw[i + 5] !== '1') {
            pile.push(sensorRaw[i], sensorRaw[i + 1] ?? String(i / 6));
          }
        }
        return { rate, plenum, pile };
      }

      // ── fanspeed ────────────────────────────────────
      // { speed: string[], pile: string[] }
      case 'fanspeed': {
        const raw = getArr('FreqCtrlData');     // maxFanSpeed: MaxSpeed,MinSpeed,RefrigSpeed,RecircSpeed,UpdatePeriod,TempDiff,TempRef1,TempRef2,PrevSpeed,UpdateMode
        const sensorRaw = getArr('SensorListData');
        // speed: [0]=maxCool, [1]=minCool, [2]=refrig, [3]=recirc, [4]=updateHrs,
        //        [5]=tempDiff, [6]=tempRef1, [7]=tempRef2, [8]=currentFanSpeed,
        //        [9]=spare, [10]=staticPressure
        const speed = [
          raw[0] ?? '60', raw[1] ?? '20', raw[2] ?? '100', raw[3] ?? '50',
          raw[4] ?? '4', raw[5] ?? '2.0', raw[6] ?? '0', raw[7] ?? '0',
          raw[8] ?? '75', '0', raw[9] ?? '0',
        ];
        const pile: string[] = [];
        for (let i = 0; i < sensorRaw.length; i += 6) {
          if (sensorRaw[i] && sensorRaw[i + 5] !== '1') {
            pile.push(sensorRaw[i], sensorRaw[i + 1] ?? String(i / 6));
          }
        }
        return { speed, pile };
      }

      // ── fanboost ────────────────────────────────────
      // string[] (at least 5 elements)
      case 'fanboost': {
        const raw = getArr('FanBoostData');     // selBoostMode: Mode,Speed,Interval,Duration,Temp
        // [0]=mode, [1]=speed%, [2]=hoursThresh, [3]=boostMins, [4]=tempThresh
        return [
          raw[0] ?? '0', raw[1] ?? '100',
          raw[2] ?? '8', raw[3] ?? '30',
          raw[4] ?? '5.0',
        ];
      }

      // ── misc ────────────────────────────────────────
      // { miscData: string[], outputConfig: string[], pwmConfig: string[] }
      case 'misc': {
        const raw = getArr('MiscData');         // p1Misc: 11 fields in schema order
        const pwm  = getArr('PWMData');
        /* ── Schema (p1Misc) vs UI (miscData) layouts ────────────────
         * p1Misc (as stored/sent by ARM):
         *   [0]=selRefrMode   [1]=defrostInterval [2]=defrostTime
         *   [3]=tempThresh    [4]=selCtrlMode (TARGET: 0=cavity heater, 1=pile fan)
         *   [5]=selCavityCtrl (MODE: 1=Off, 2=Manual, 3=Automatic)
         *   [6]=cavityDiff    [7]=cavityDutyCycle  [8]=selCavityCtrlSensor
         *   [9]=kbPref        [10]=cavStandbyOn
         *
         * UI miscData (indices used by +page.svelte + write path in apiRoutes):
         *   [0]=refrMode      [1]=defrostInterval [2]=defrostTime
         *   [3]=tempThresh    [4]=cavityCtrl(MODE)    [5]=legacy mirror
         *   [6]=cavityDiff    [7]=dutyCycle OR sensor (chosen by mode)
         *   [8]=kbPref        [9]=locale reserved
         *   [10]=cavityTarget [11]=reserved           [12]=enthalpyOff
         *   [13]=cavStandbyOn                                       */
        const mode = raw[5] ?? '1';
        const dutyOrSensor = mode === '3' ? (raw[8] ?? '4') : (raw[7] ?? '100');
        const miscData: string[] = [
          raw[0]  ?? '0',        // [0]  refrMode
          raw[1]  ?? '4',        // [1]  defrostInterval
          raw[2]  ?? '20',       // [2]  defrostTime
          raw[3]  ?? '10',       // [3]  tempThresh
          mode,                  // [4]  cavityCtrl (mode)
          mode,                  // [5]  legacy mirror (Svelte reactive block reads [5])
          raw[6]  ?? '5',        // [6]  cavityDiff
          dutyOrSensor,          // [7]  dutyCycle or sensor (mode-dependent)
          raw[9]  ?? '0',        // [8]  kbPref
          '0',                   // [9]  locale reserved
          raw[4]  ?? '0',        // [10] cavityTarget
          '0',                   // [11] reserved
          '0',                   // [12] enthalpyOff (UI-only; not round-tripped)
          raw[10] ?? '0',        // [13] cavStandbyOn
        ];
        // Equipment page uses eq-indexed OutputConfig; convert from port-indexed cache
        return { miscData, outputConfig: this.getEqIndexedOutputConfig(), pwmConfig: pwm.length > 0 ? pwm : ['0'] };
      }

      // ── plensetup ───────────────────────────────────
      // string[] (11 elements).  ARM sends p1Plenum (PgmData, 5 fields) and
      // AlarmTempLow (PlenTempDevData, 6 fields) as separate tags — no p2Plenum
      // tag exists.  Compose the 11-element array from these two sources.
      case 'plensetup': {
        const pgm = getArr('PgmData');           // p1Plenum: TempSet,HumidSet,HumidRef,BurnerTempSet,BurnerThreshold
        const dev = getArr('PlenTempDevData');   // AlarmTempLow: LowTemp,LowMin,HighTemp,HighMin,CureLow,CureHigh
        // [0]=tempSP, [1]=humidSP, [2]=spare, [3]=cureTempSP, [4]=cureHumid,
        // [5]=alarmTempLow, [6]=alarmMinLow, [7]=alarmTempHigh, [8]=alarmMinHigh,
        // [9]=cureTempLow, [10]=cureTempHigh
        const result = new Array(11).fill('0');
        for (let i = 0; i < 5 && i < pgm.length; i++) result[i] = pgm[i] ?? '0';
        for (let i = 0; i < dev.length && i + 5 < result.length; i++) result[i + 5] = dev[i] ?? '0';
        return result;
      }

      // ── runtimes ────────────────────────────────────
      // Object with numeric keys -> values (mode codes for RunTime component)
      case 'runtimes': {
        const raw = getArr('RunTimesData');
        const result: Record<string, string> = {};
        // 48 half-hour slots only — DailyFanRun/TotalFanRun are available
        // through the frontmatter panel array and must NOT leak into the
        // save payload (which would inflate RunTimesData beyond 48 values)
        for (let i = 0; i < 48; i++) {
          result[String(i)] = raw[i] ?? '1';
        }
        return result;
      }

      // ── version ─────────────────────────────────────
      // { controller: string[], webserver: string, ui: string, displays: string[], status: number }
      // SOFTWARE_VERSION env var is written by apply_upgrade.sh alongside the ARM
      // firmware binary.  It represents the version the controller *would* report
      // after the production SRE is flashed.  In QEMU we cannot flash the new SRE
      // (incompatible vector table), so the bridge reads the version from the file
      // that apply_upgrade.sh places next to the firmware — conceptually "pulled
      // from the ARM emulator".
      case 'version': {
        const fw = getVal('FirmwareVersion');
        const swVersion = process.env.SOFTWARE_VERSION || '';

        // controller[0] = main version, controller[1..N] = board name/version pairs
        let controller: string[];
        if (swVersion) {
          controller = [swVersion];
        } else if (fw) {
          controller = [fw];
        } else {
          controller = ['--'];
        }

        return {
          controller,
          webserver: swVersion || '1.0.0-bridge',
          ui: swVersion || '2.0.0',
          displays: [],
          status: 200,
        };
      }

      // ── service ─────────────────────────────────────
      // string[] (at least 4 elements)
      case 'service': {
        const p1 = getArr('P1ServiceData');
        const p2 = getArr('P2ServiceData');
        // [0]=dealerName, [1]=officePhone, [2]=techName, [3]=techPhone
        const data = p2.length >= 4 ? p2 : p1.length >= 4 ? p1 : ['', '', '', ''];
        return data;
      }

      // ── email ───────────────────────────────────────
      // string[] (at least 7 elements)
      case 'email': {
        const raw = getArr('EmailConfigData');
        // [0]=enable, [1]=server, [2]=authType, [3]=port, [4]=account,
        // [5]=password, [6]=localIP, [7]=toAddr, [8]=fromAddr
        // UI gates full form on email.length > 7 — must return >= 9 elements.
        if (raw.length >= 9) return raw;
        const padded = [...raw];
        while (padded.length < 9) padded.push('');
        if (!padded[0]) padded[0] = '1';
        if (!padded[3]) padded[3] = '587';
        return padded;
      }

      // ── alerts ──────────────────────────────────────
      // string[] of '0'/'1' (one per alert checkbox)
      case 'alerts': {
        const raw = getArr('AlertSetupData');   // AlertSetup tag — ARM sends directly
        return raw.length > 0 ? raw : new Array(20).fill('0');
      }

      // ── date ────────────────────────────────────────
      // string[] — [dateStr, timeStr, amPmFlag]
      case 'date': {
        const dtRaw = getVal('DateTimeData');
        if (dtRaw && dtRaw.includes(',')) {
          return dtRaw.split(',');
        }
        const now = new Date();
        const h = now.getHours();
        return [
          `${String(now.getMonth() + 1).padStart(2, '0')}/${String(now.getDate()).padStart(2, '0')}/${now.getFullYear()}`,
          `${String(h > 12 ? h - 12 : h || 12).padStart(2, '0')}:${String(now.getMinutes()).padStart(2, '0')}:${String(now.getSeconds()).padStart(2, '0')}`,
          h >= 12 ? '1' : '0',
        ];
      }

      // ── basic ───────────────────────────────────────
      // string[] (at least 10 elements, index 8 is AES-encrypted password)
      case 'basic': {
        const raw = getArr('P2BasicSetupData');
        // Pad to at least 10 elements
        while (raw.length < 10) raw.push('0');
        // [8] would be encrypted password — set blank if missing
        if (!raw[8]) raw[8] = '';
        // [9] = localRequired flag
        if (!raw[9]) raw[9] = '0';
        return raw;
      }

      // ── refrigeration ───────────────────────────────
      // string[] (at least 23 elements: stage on/off pairs + PIDU + purge + type)
      case 'refrigeration': {
        const raw = getArr('RefrigData');
        const p2  = getArr('P2RefrigData');
        const data = p2.length >= 23 ? p2 : raw.length >= 23 ? raw
          : new Array(23).fill('0');
        return data;
      }

      // ── door ────────────────────────────────────────
      // string[]
      case 'door': {
        // ARM sends PAirValue tag (→ P2FreshAirData, 7 fields): PID_P,PID_I,PID_D,PID_U,ActuatorTime,CoolAirCycle,PIDDoorLog
        const fresh = getArr('P2FreshAirData');
        const raw = getArr('DoorData');
        return fresh.length > 0 ? fresh : raw.length > 0 ? raw : ['0', '0', '0', '0'];
      }

      // ── climacell ───────────────────────────────────
      // string[]
      case 'climacell': {
        const p2  = getArr('P2ClimaCellData');
        const raw = getArr('ClimaCellData');
        return p2.length > 0 ? p2 : raw.length > 0 ? raw : ['0', '0', '0'];
      }

      // ── master ──────────────────────────────────────
      case 'master':
      case 'network': {
        const raw = getArr('MasterSlaveData');
        return raw.length > 0 ? raw : ['0'];
      }

      // ── ioconfig ────────────────────────────────────
      case 'ioconfig': {
        // UI expects IOConfigType: { ioAvailable, config: { outputConfig, inputConfig }, ioNames, systemMode }
        const p2Basic = getArr('P2BasicSetupData');
        
        // Constellation only ever uses Orbit boards — no legacy AS2
        // expansion boards. buildOrbitAvailableIo() guarantees at least one
        // entry (Orbit 1) even before discovery completes.
        return {
          ioAvailable: this.buildOrbitAvailableIo(),
          config: {
            outputConfig: getArr('IoConfigOutData'),
            inputConfig: getArr('IoConfigInData'),
          },
          ioNames: getArr('IoNamesData'),
          systemMode: p2Basic.length > 4 ? p2Basic[4] : '0',
        };
      }

      // ── log ─────────────────────────────────────────
      case 'log': {
        // Firmware sends recInterval tag → mapped to UserLogSettings
        const userLog = getArr('UserLogSettings');
        const raw = getArr('LogConfigData');
        return userLog.length > 0 ? userLog : raw.length > 0 ? raw : ['5', '0'];
      }

      // ── accounts ────────────────────────────────────
      case 'accounts': {
        // Firmware sends AcctId0 tag → mapped to UserAccounts (not AccountData)
        const raw = getArr('UserAccounts');
        return raw.length > 0 ? raw : ['', '', '', ''];
      }

      // ── sensors ─────────────────────────────────────
      // Pile page expects { Sensors: string[] } (stride-6 legacy array).
      // Production firmware only includes boards 3+ (SIDs >= 8) here;
      // boards 1 & 2 are "default" boards whose readings go into frontmatter.
      case 'sensors': {
        const sensorRaw = getArr('SensorListData');
        // Firmware always emits sensor values in °C — convert temperature-family
        // sensors to °F when the user has picked Fahrenheit in Basic Setup.
        // P2BasicSetupData field[1]: '1' = Celsius, anything else = Fahrenheit.
        const basicParts = getArr('P2BasicSetupData');
        const useFahrenheit = (basicParts[1] ?? '') !== '1';
        // SensorTypes that carry a temperature reading (match src/lib/business/sensorFeeds.ts):
        // TEMP_IR=0, TEMP=3, RETURN_TEMP_1=4, RETURN_TEMP_2=5, PILE_TEMP=9
        const TEMP_TYPES = new Set(['0', '3', '4', '5', '9']);
        const convertTemp = (val: string, type: string): string => {
          if (!useFahrenheit) return val;
          if (!TEMP_TYPES.has(type)) return val;
          if (!val || val === '--' || val === 'dis') return val;
          const n = parseFloat(val);
          if (isNaN(n)) return val;
          return (n * 9 / 5 + 32).toFixed(1);
        };
        // Filter to SIDs >= 8 (boards 3+), stride 6: Label,SID,Type,Value,Offset,Disabled
        const pileSensors: string[] = [];
        for (let i = 0; i + 5 < sensorRaw.length; i += 6) {
          const sid = parseInt(sensorRaw[i + 1], 10);
          if (!isNaN(sid) && sid >= 8) {
            pileSensors.push(
              sensorRaw[i],
              sensorRaw[i + 1],
              sensorRaw[i + 2],
              convertTemp(sensorRaw[i + 3], sensorRaw[i + 2]),
              sensorRaw[i + 4],
              sensorRaw[i + 5]
            );
          }
        }
        return {
          Sensors: pileSensors,
          PileSetupData: getVal('PileSetupData'),
        };
      }

      // ── analog ──────────────────────────────────────
      case 'analog': {
        const raw = getArr('AnalogData');
        return raw.length > 0 ? raw : ['0'];
      }

      // ── pid ─────────────────────────────────────────
      case 'pid': {
        const raw = getArr('PidData');
        return raw.length > 0 ? raw : ['0'];
      }

      // ── pwm ─────────────────────────────────────────
      // UI expects { pwmConfig: string[][], pwmChannels: string[], ioAvailable: string[], systemMode: string }
      // pwmConfig entries are sub-arrays: [Name, Mode, Channel, Index]
      // (the Svelte page indexes item[0]=name, item[1]=mode, item[3]=index)
      case 'pwm': {
        const p2  = getArr('P2PWMData');
        const raw = getArr('PWMData');
        const rawEntries = p2.length > 0 ? p2 : raw.length > 0 ? raw : [];
        // Split colon-delimited "Name:Mode:Channel:Index" into sub-arrays;
        // filter out empty trailing entries from firmware's trailing comma
        const pwmConfig: string[][] = rawEntries
          .filter(e => e && e.includes(':'))
          .map(e => e.split(':'));
        // Extract channel assignments: pwmChannels[eqIndex] = physicalChannel | '-1'
        const pwmChannels = pwmConfig.map(parts =>
          parts.length >= 3 ? parts[2] : '-1'
        );
        const basicArr = getArr('P2BasicSetupData');
        // Constellation only ever uses Orbit boards — each Orbit contributes
        // 2 PWM-capable analog-output channels. No legacy AS2 expansion-board
        // fallback.
        return {
          pwmConfig,
          pwmChannels,
          ioAvailable: this.buildOrbitAvailableIoForPwm(),
          systemMode: basicArr[4] ?? '0',
        };
      }

      // ── failures ────────────────────────────────────
      case 'failures1': {
        const raw = getArr('FailureData1');
        const pwm = getArr('PWMData');
        const basicArr = getArr('P2BasicSetupData');
        return {
          InputConfig: this.getEqIndexedInputConfig(),
          OutputConfig: this.getEqIndexedOutputConfig(),
          PwmConfig: pwm.length > 0 ? pwm : ['0'],
          systemMode: basicArr[4] ?? '0',
          boardType: 'AS2',
          controllerVersion: '3',
          failures: raw.length > 0 ? raw : new Array(20).fill('0'),
        };
      }
      case 'failures2': {
        const raw = getArr('FailureData2');
        return raw.length > 0 ? raw : new Array(20).fill('0');
      }

      // ── burner (page-level, not home-page array) ───
      case 'burner': {
        return {
          P2BurnerData: getVal('P2BurnerData') || getVal('BurnerData') || '0,0,0,0,0,0,0',
          BurnerData: getVal('BurnerData') || '0,0,0,0,0,0,0',
        };
      }

      default:
        return null;
    }
  }

  /** Get all cached data as a flat object (for debugging) */
  getAll(): Record<string, string> {
    const result: Record<string, string> = {};
    for (const entry of this.entries) {
      if (entry.value) {
        result[entry.varName] = entry.value;
      }
    }
    return result;
  }

  /** Get total number of CGI variables */
  get length(): number {
    return this.entries.length;
  }
}
