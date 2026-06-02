/**
 * In-memory cache of CGI variables received from the ARM controller.
 * Replaces the C-based CGI_DATA CGI[68] array in GellertServer.
 *
 * Each ARM message carries a tag (e.g. "main", "EquipStatus") and a comma-separated value string.
 * This cache stores the latest value for each tag and provides JSON accessors for the UI.
 */

import { randomUUID } from 'crypto';
import { loadConfig, saveConfig } from './simConfig.js';
import type { IoEntry } from './novaDataStore.js';

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

/**
 * Per-orbit-board cache row. Mirrors firmware `OrbitBoardStatus`
 * (proto3, see `proto/agristar/orbit.proto`) field-for-field with
 * ts-proto camelCase naming. Required fields (`slot`, `role`,
 * `connected`) are present even before the first OrbitStatus push;
 * everything else is `?` and is populated once firmware sends an
 * OrbitStatus envelope (tag 120). Per data-path-rules: NO temperature
 * conversion, scaling, or fix-ups — values forwarded verbatim from
 * firmware (cpuTemp is float °C, uptimeSecs is uint32 seconds, etc.).
 */
export interface OrbitBoardCacheRow {
  slot: number;
  role: number;
  connected: boolean;
  /** Per-AO equipment program (Settings.AoEquip[slot][ch]). Length 2. */
  aoEquip?: number[];
  // ── Mirrored from OrbitBoardStatus proto (populated by firmware push) ──
  dipswitchId?: number;     // proto: dipswitch_id (DIP value 1..63)
  commErrors?: number;      // proto: comm_errors  (consecutive Modbus errors)
  estopActive?: boolean;    // proto: estop_active
  safeMode?: boolean;       // proto: safe_mode
  cpuTemp?: number;         // proto: cpu_temp (float °C, verbatim)
  uptimeSecs?: number;      // proto: uptime_secs
  firmwareVer?: number;     // proto: firmware_ver (orbit FW version)
  zoneId?: number;          // proto: zone_id
  legacySlot?: number;      // proto: legacy_slot (-1 = unmapped)
  refrigStage?: number;     // proto: refrig_stage (REFRIG role only)
  ipAddress?: string;       // proto: ip_address

  // ── Phase 4b Sub-1 audit follow-up (2026-06-02): proto fields 16-23.
  //    LP firmware has emitted these since the April 2026 LP-I/O extension
  //    but the bridge decoder was dropping them on the floor — see
  //    novaDataStore.ts::decodeOrbitBoardStatus. Each is independently
  //    optional on the wire (proto3 zero-suppression); a missing field
  //    decodes as `undefined` so consumers can distinguish "never polled"
  //    from "all bits clear".
  digitalInputs?: number;      // bitmap: DI 0..9 + estop(10) + DC24V mon 11..14
  digitalOutputs?: number;     // bitmap: DO 0..9
  dc24vOutputs?: number;       // bitmap: DC24V output 0..3
  analogOutputsX10?: number[]; // percent×10 per AO channel (len 2 today)
  vfdActivitySecs?: number[];  // per-VFD comm age (len 24, 0xFFFFFFFF=never)
  sensorActivitySecs?: number[]; // per-sensor comm age (len 16, same sentinel)
  outputLabels?: string[];     // operator-assigned DO labels (len 10)
  inputLabels?: string[];      // operator-assigned DI labels (len 10)
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
  /* 92 */ { msgTag: 'PidLogData',       varName: 'PidLogData' },
];

export class DataCache {
  private entries: CgiEntry[] = [];
  private tagIndex = new Map<string, number>();
  private varNameIndex = new Map<string, number>();

  /**
   * Structured I/O definition list received from firmware (msg 25
   * IoDefinition). Replaces the legacy `Name:Mode:IO:Renamable:Index` CSV
   * passthrough — UI consumers (equipment-status, ioconfig, auxiliary)
   * read this directly via getIoEntries(). djangoSync still serialises a
   * CSV view via getIoNamesCsv() because the Azure backend hasn't been
   * migrated yet.
   */
  private ioEntries: IoEntry[] = [];

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

    return true;
  }

  // (Apr 2026 proto-direct migration: removed `tag === 'AuxProgram'`
  // accumulator + `getAllAuxPrograms()` + `auxPrograms` map. The aux page
  // now consumes AuxProgramBundle directly via auxiliaryComposite in
  // constellation-ui/src/lib/business/protoStores.ts — no CSV middleman.)

  // (S9k cleanup 2026-04-21: getByIndex / getByTag / getJsonItem removed
  // — zero callers across the bridge.  getByVarName remains the sole
  // entry-point lookup the bridge uses.)

  /** Get a CGI entry by JavaScript variable name */
  getByVarName(varName: string): CgiEntry | undefined {
    const idx = this.varNameIndex.get(varName);
    return idx !== undefined ? this.entries[idx] : undefined;
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

  // (Apr 2026 cleanup: getEqIndexedInputConfig() removed — zero callers
  // after the failures-pages proto-direct migration. Output variant
  // above is still used by novaAdapter for equipment-status fallback.)

  // ─────────────────────────────────────────────
  // I/O definition (structured) — replaces IoNamesData CSV for UI
  // ─────────────────────────────────────────────

  /** Replace the cached IoEntry[] (called by novaAdapter on IoDefinition msg). */
  setIoEntries(entries: IoEntry[]): void {
    this.ioEntries = entries;
  }

  /** Structured I/O definitions for UI / aux / equipment-status endpoints. */
  getIoEntries(): IoEntry[] {
    return this.ioEntries;
  }

  /**
   * Build the legacy `Name:Mode:IoType:Renamable:Index` CSV view of the
   * I/O definitions. Only djangoSync uses this — the Azure cloud backend
   * still consumes the AS2 CSV shape. Once that side is migrated this
   * method (and the Django passthrough) can be removed.
   */
  getIoNamesCsv(): string {
    return this.ioEntries
      .map(e => `${e.name}:${e.mode}:${e.ioType}:${e.renamable ? 1 : 0}:${e.index}`)
      .join(',');
  }

  // (Apr 2026 cleanup: getEquipStatus() removed — zero callers after
  // the equipment-status page went proto-direct via $equipmentStatus.)

  // ─────────────────────────────────────────────
  // Composite data builders for WebSocket channels
  // ─────────────────────────────────────────────
  //
  // `buildFrontmatterData()` (the 280-line legacy assembler that produced
  // the `frontmatter-data` WS payload from cached CGI variables) was deleted
  // in the Phase 5 cleanup of the proto-direct redesign. The SvelteKit UI
  // now derives the equivalent payload entirely client-side from the typed
  // proto stream (see `frontMatterComposite.ts`). The bridge no longer
  // owns that translation layer; protobufs are forwarded as-is and the UI
  // shapes them.

  /**
   * Build TCP/IP node data for the header dropdown and remote systems page.
   * Always includes the local panel; the caller (wsManager) merges in entries
   * from `remoteSystemsSync` (UUID-keyed, DHCP-resilient — see
   * `remoteSystemsSync.ts`). Each node has an `id` field that prefers the
   * peer's persistent novaId so site groups survive DHCP IP changes.
   *
   * Legacy fallback: if no `remotes` array is supplied, fall back to the
   * firmware-driven `P2NodeSetupData` CSV (kept temporarily so older callers
   * still get a working list — slated for removal once nothing else reads it).
   */
  buildTcpIpData(remotes?: Array<{
    id: string;
    name: string;
    host: string;
    port: number;
    novaId: string;
  }>): { nodes: Array<{ text: string; value: string; id: string }>; localIP: string } {
    const panelName = this.getByVarName('PanelName')?.value || 'Agristar Panel';
    const localIP = this.getByVarName('LocalIpAdd')?.value?.split(',')[0] || '127.0.0.1';
    const nodes: Array<{ text: string; value: string; id: string }> = [];

    // Always include current system with its persistent novaId
    nodes.push({ text: panelName, value: `${localIP}:80`, id: getNovaId() });

    if (remotes && remotes.length > 0) {
      for (const r of remotes) {
        const addr = `${r.host}${r.port && r.port !== 80 ? ':' + r.port : ''}`;
        // novaId when known (DHCP-resilient identity), bridge entry id otherwise.
        nodes.push({ text: r.name, value: addr, id: r.novaId || r.id });
      }
      return { nodes, localIP };
    }

    // ── Legacy fallback (P2NodeSetupData CSV) ─────────────────────────
    // Stride-4: name, host, port, status. Address is the id (no novaId
    // available on this path).
    const nodeRaw = this.getByVarName('P2NodeSetupData')?.value ?? '';
    if (nodeRaw) {
      const parts = nodeRaw.split(',');
      for (let i = 0; i + 2 < parts.length; i += 4) {
        const name = parts[i];
        const host = parts[i + 1];
        const port = parts[i + 2];
        if (name && host && port) {
          const addr = `${host}:${port}`;
          nodes.push({ text: name, value: addr, id: addr });
        }
      }
    }

    return { nodes, localIP };
  }

  /** Orbit board discovery data for Constellation mode.
   *
   *  Row shape mirrors firmware `OrbitBoardStatus` (proto3, see
   *  `proto/agristar/orbit.proto`) field-for-field with ts-proto camelCase
   *  naming. Every non-required field is `?` so a row built before the
   *  first OrbitStatus push (e.g. from a discovery-only path) is still
   *  valid; `/iot/orbit/*` handlers omit absent keys rather than emitting
   *  `null` placeholders. Per data-path-rules: NO temperature conversion,
   *  scaling, or fix-ups happen here — values are forwarded verbatim
   *  from the firmware (cpuTemp is float °C, uptimeSecs is uint32, etc.).
   */
  private orbitBoards: Array<OrbitBoardCacheRow> = [];

  /**
   * Store discovered orbit boards (called from novaAdapter on firmware
   * OrbitStatus / OrbitDiscovery pushes — the LP firmware is the sole
   * orbit Modbus client).
   */
  setOrbitBoards(boards: Array<OrbitBoardCacheRow>): void {
    this.orbitBoards = boards;
  }

  getOrbitBoards(): Array<OrbitBoardCacheRow> {
    return this.orbitBoards;
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
