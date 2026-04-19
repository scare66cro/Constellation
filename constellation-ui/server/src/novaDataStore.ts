/**
 * novaDataStore.ts — Typed data store for Constellation protocol
 *
 * Replaces dataCache.ts (93-entry CGI string table) with typed objects
 * decoded from protobuf messages. Each message type maps to a typed
 * interface — no more positional CSV parsing.
 *
 * The store:
 *   - Receives raw protobuf submessages from NovaSerialBridge
 *   - Decodes them using the manual PB decoder (or ts-proto later)
 *   - Stores typed objects in memory
 *   - Provides typed getters for the REST API
 *   - Emits change events for WebSocket push
 *
 * When ts-proto codegen is integrated, the manual decode functions
 * will be replaced with generated decode calls.
 */

import { EventEmitter } from 'events';

/* ──────────────────────────────────────────────────────────────────────── *
 *  Typed interfaces matching .proto definitions                            *
 * ──────────────────────────────────────────────────────────────────────── */

export interface SystemStatus {
  plenumTemp: number;
  plenumHumid: number;
  outsideTemp: number;
  remoteTemp: number;
  outsideHumid: number;
  remoteHumid: number;
  startTemp: number;
  returnHumid: number;
  returnTemp: number;
  co2Level: number;
  fanSpeed: string;
  coolOutput: string;
  coolLabel: string;
  burnerOutput: string;
  calcHumid: number;
  currentMode: number;
  /** Burner-cure substate (legacy CureState global; 0 = CS_OFF / not curing). */
  cureState: number;
}

export interface EquipState {
  eqIndex: number;
  outputOn: boolean;
  remoteOff: number;
  alarm: number;
  label: string;
}

export interface EquipmentStatus {
  items: EquipState[];
}

export interface DateTime {
  dateStr: string;
  timeStr: string;
  amPm: number;
}

export interface BasicSetup {
  storageName: string;
  tempType: number;
  mode: number;
  homePage: string;
  systemMode: number;
  language: number;
  masterIp: string;
  multiView: number;
  loginPw: string;
  localLogin: number;
  animations: number;
}

export interface Warning {
  code: number;
  severity: number;
  message: string;
  eqIndex: number;
  timestamp: number;
}

/** Orbit-specific warning codes (match hal_orbit.h ORBIT_WARN_*) */
export const ORBIT_WARN_NAMES: Record<number, string> = {
  0x100: 'Orbit E-Stop Active',
  0x101: 'Orbit Watchdog Safe Mode',
  0x102: 'Orbit Communication Lost',
  0x103: 'Orbit Sensor Fault (0x7FFF)',
  0x104: 'Orbit CPU Over-Temperature',
};

export interface WarningReport {
  warnings: Warning[];
}

export interface Heartbeat {
  uptimeSec: number;
  freeHeap: number;
  msgTxCount: number;
  msgRxCount: number;
}

export interface PlenumSettings {
  tempSetpoint: number;
  humidSetpoint: number;
  humidSetpointRef: number;
  burnerTempSetpoint: number;
  burnerThreshold: string;
}

export interface BurnerSettings {
  on: number;
  low: number;
  pGain: number;
  iGain: number;
  dGain: number;
  uLimit: number;
  mode: number;
  manual: number;
}

export interface Co2Settings {
  mode: number;
  minTemp: number;
  maxTemp: number;
  durationMinutes: number;
  cycleOrSet: number;
  fanOutput: number;
  doorOutput: number;
}

export interface CureSettings {
  startTemp: number;
  humidRef: number;
  startHumid: number;
  humidHighLimit: number;
}

export interface FanBoostSettings {
  mode: number;
  speed: number;
  interval: number;
  duration: number;
  temp: number;
}

export interface ClimacellSettings {
  efficiency: number;
  altitude: number;
  altUnits: number;
  pGain: number;
  iGain: number;
  dGain: number;
  uLimit: number;
}

export interface FanSpeedSettings {
  maxSpeed: number;
  minSpeed: number;
  coolSpeed: number;
  recircSpeed: number;
  updatePeriod: number;
}

export interface RampRateSettings {
  ratePerDay: number;
  enabled: number;
}

export interface RefrigSettings {
  mode: number;
  /** 8 stages.  `on`/`off` are uint8 thresholds (0..100); `diagnostic` is
   * 0=auto, 1=off, 2=on (mirrors `Settings.Refrig.Stage[i]` in legacy). */
  stages: { on: number; off: number; diagnostic: number }[];
  /** 2 defrost stages with the same {on,off,diagnostic} layout. */
  defrosts: { on: number; off: number; diagnostic: number }[];
  pGain: number;
  iGain: number;
  dGain: number;
  uLimit: number;
  defrostInterval: number;
  defrostDuration: number;
  /** RefrigerationPurge mode (uint8). */
  purge: number;
  /** PurgeThreshold — fan % at which purge stage engages.  Cross-coupled
   * with `Settings.Co2.Purge.RefrigThresh` in the firmware. */
  purgeThreshold: number;
  /** Onion-only refrig limit (degrees). */
  limit: number;
  /** Refrig fail-safe behaviour (uint8 enum). */
  failMode: number;
}

export interface HumidCtrlEntry {
  index:    number;  // 0..2
  mode:     number;  // 0=Manual, 1=Timer, 2=Auto
  coolOn:   number;
  coolOff:  number;
  recOn:    number;
  recOff:   number;
  refOn:    number;
  refOff:   number;
}

export interface HumidCtrlSettings {
  /** One entry per humidifier (typically 3). Sorted by index. */
  entries: HumidCtrlEntry[];
}

export interface OutsideAirSettings {
  mode: number;
  differential: number;
}

export interface MiscSettings {
  heatTempThresh: number;
  kbPref: number;
  lightsFailUnits: number;
  cavStandbyOn: number;
}

export interface FailureSettings {
  values: Record<string, number>;
}

export interface TempAlarmSettings {
  lowLimit: number;
  highLimit: number;
  devLimit: number;
  delayMins: number;
}

export interface DoorSettings {
  pGain: number;
  iGain: number;
  dGain: number;
  uLimit: number;
  actuatorTime: number;
  coolAirCycle: number;
}

export interface VersionInfo {
  armVersion: string;
  bootloaderVersion: string;
}

export interface ServiceInfo {
  dealerName: string;
  dealerPhone: string;
  techName: string;
  techPhone: string;
}

export interface IoConfig {
  outputMap: number[];
  inputMap: number[];
}

export interface IoEntry {
  index: number;
  name: string;
  mode: number;
  ioPin: number;
  renamable: boolean;
  visible: boolean;
}

export interface IoDefinition {
  entries: IoEntry[];
}

export interface EmailSettings {
  server: string;
  port: number;
  username: string;
  password: string;
  fromAddr: string;
  toAddr: string;
  enabled: number;
}

export interface LoadMonitorSettings {
  bay1Label: string;
  bay2Label: string;
  bayCount: number;
}

export interface PasswordResponse {
  level: number;
  session: number;
}

export interface RuntimeEntry {
  slot: number;
  value: number;
}

export interface Runtimes {
  entries: RuntimeEntry[];
}

export interface FanRuntime {
  dailyHours: number;
  dailyMinutes: number;
  totalHours: number;
  totalMinutes: number;
}

export interface HumidModes {
  head1: number;
  pump1: number;
  head2: number;
  pump2: number;
}

export interface AuxSwitches {
  states: number[];
}

export interface AnalogSensor {
  slot: number;
  type: number;
  label: string;
  offset: number;
  disabled: boolean;
  value: number;
}

export interface AnalogBoard {
  address: number;
  type: number;
  label: string;
  version: string;
  disabled: boolean;
  sensors: AnalogSensor[];
}

export interface AvailableIo {
  outputPins: number[];
  inputPins: number[];
}

export interface SensorReading {
  index: number;
  value: number;
  valid: boolean;
}

export interface SensorData {
  temps: SensorReading[];
  humids: SensorReading[];
}

export interface SensorLabel {
  index: number;
  label: string;
}

export interface SensorLabels {
  temps: SensorLabel[];
  humids: SensorLabel[];
}

export interface AccountSettings {
  users: { slot: number; userId: string }[];
  count: number;
  hasLoginPw: boolean;
}

export interface AuxRule {
  type: number;
  ioIndex: number;
  state: number;
  op: number;
  sensorValue: number;
  andOr: number;
  referenceIndex: number;
}

export interface AuxProgramEntry {
  auxIndex: number;
  eqIndex: number;
  dutyCycle: number;
  period: number;
  units: number;
  rules: AuxRule[];
}

export interface AuxProgram {
  programs: AuxProgramEntry[];
}

export interface UserLogSettings {
  interval: number;
  wrap: number;
}

export interface PidSettings {
  eqIndex: number;
  wrap: number;
}

export interface GraphFavorites {
  csv: string;
}

export interface AlertSettings {
  flags: number[];
}

export interface PwmChannel {
  index: number;
  enabled: number;
  channel: number;
}

export interface PwmSettings {
  channels: PwmChannel[];
}

export interface NetworkNode {
  slot: number;
  ip: string;
  id: string;
}

export interface NetworkNodes {
  nodes: NetworkNode[];
}

export interface ClimacellTimes {
  slots: number[];
}

/* ─── Orbit module types ─────────────────────────────────────────────────── */

export enum OrbitRole {
  UNASSIGNED = 0,
  STORAGE = 1,    // Standard 10 DI / 10 DO I/O board
  GDC = 2,        // Gellert Door Controller: 5 actuators
  TRITON = 3,     // Triton: refrigeration/cooling control
  PULSAR = 4,     // Pulsar: advanced I/O module
}

export interface OrbitBoardStatus {
  slot: number;
  dipswitchId: number;
  connected: boolean;
  commErrors: number;
  estopActive: boolean;
  safeMode: boolean;
  cpuTemp: number;
  uptimeSecs: number;
  firmwareVer: number;
  role: OrbitRole;
  zoneId: number;
  legacySlot: number;  // -1 if not mapped
  refrigStage: number;
  ipAddress: string;
}

export interface OrbitStatus {
  boards: OrbitBoardStatus[];
}

export interface OrbitDiscovery {
  maxSlots: number;
  boardsFound: number;
  boards: OrbitBoardStatus[];
}

/* ──────────────────────────────────────────────────────────────────────── *
 *  Minimal protobuf decode helpers (duplicated from novaSerialBridge)      *
 *  Will be replaced by shared import from generated ts-proto code.         *
 * ──────────────────────────────────────────────────────────────────────── */

const PB_VARINT = 0;
const PB_LEN = 2;
const PB_32BIT = 5;

interface PbField {
  field: number;
  wireType: number;
  value: number | Buffer;
}

function pbDecode(buf: Buffer): PbField[] {
  const fields: PbField[] = [];
  let pos = 0;
  function rv(): number {
    let val = 0, shift = 0;
    while (pos < buf.length) {
      const b = buf[pos++];
      val |= (b & 0x7F) << shift;
      if (!(b & 0x80)) return val >>> 0;
      shift += 7;
      if (shift >= 35) break;
    }
    return val >>> 0;
  }
  while (pos < buf.length) {
    const tag = rv();
    const wt = tag & 7, fn = tag >>> 3;
    if (fn === 0) break;
    switch (wt) {
      case 0: fields.push({ field: fn, wireType: wt, value: rv() }); break;
      case 1: pos += 8; break;
      case 2: { const len = rv(); fields.push({ field: fn, wireType: wt, value: Buffer.from(buf.subarray(pos, pos + len)) }); pos += len; } break;
      case 5: { fields.push({ field: fn, wireType: wt, value: Buffer.from(buf.subarray(pos, pos + 4)) }); pos += 4; } break;
      default: return fields;
    }
  }
  return fields;
}

function gv(fields: PbField[], f: number): number {
  const e = fields.find(x => x.field === f && x.wireType === PB_VARINT);
  return e ? (e.value as number) : 0;
}

function gf(fields: PbField[], f: number): number {
  const e = fields.find(x => x.field === f && x.wireType === PB_32BIT);
  return e ? (e.value as Buffer).readFloatLE(0) : 0;
}

function gs(fields: PbField[], f: number): string {
  const e = fields.find(x => x.field === f && x.wireType === PB_LEN);
  return e ? (e.value as Buffer).toString('utf8') : '';
}

function ga(fields: PbField[], f: number): Buffer[] {
  return fields.filter(x => x.field === f && x.wireType === PB_LEN).map(x => x.value as Buffer);
}

/* ──────────────────────────────────────────────────────────────────────── *
 *  NovaDataStore                                                           *
 * ──────────────────────────────────────────────────────────────────────── */

export class NovaDataStore extends EventEmitter {
  systemStatus: SystemStatus | null = null;
  equipmentStatus: EquipmentStatus | null = null;
  dateTime: DateTime | null = null;
  basicSetup: BasicSetup | null = null;
  warningReport: WarningReport | null = null;
  heartbeat: Heartbeat | null = null;
  plenumSettings: PlenumSettings | null = null;
  burnerSettings: BurnerSettings | null = null;
  co2Settings: Co2Settings | null = null;
  cureSettings: CureSettings | null = null;
  fanBoostSettings: FanBoostSettings | null = null;
  climacellSettings: ClimacellSettings | null = null;
  fanSpeedSettings: FanSpeedSettings | null = null;
  rampRateSettings: RampRateSettings | null = null;
  refrigSettings: RefrigSettings | null = null;
  humidCtrlSettings: HumidCtrlSettings | null = null;
  outsideAirSettings: OutsideAirSettings | null = null;
  miscSettings: MiscSettings | null = null;
  failureSettings: FailureSettings | null = null;
  failureSettings2: FailureSettings | null = null;
  tempAlarmSettings: TempAlarmSettings | null = null;
  doorSettings: DoorSettings | null = null;
  loadMonitorSettings: LoadMonitorSettings | null = null;
  versionInfo: VersionInfo | null = null;
  serviceInfo: ServiceInfo | null = null;
  ioConfig: IoConfig | null = null;
  ioDefinition: IoDefinition | null = null;
  emailSettings: EmailSettings | null = null;
  passwordResponse: PasswordResponse | null = null;
  runtimes: Runtimes | null = null;
  fanRuntime: FanRuntime | null = null;
  humidModes: HumidModes | null = null;
  auxSwitches: AuxSwitches | null = null;
  analogBoards: Map<number, AnalogBoard> = new Map();
  availableIo: AvailableIo | null = null;
  sensorData: SensorData | null = null;
  sensorLabels: SensorLabels | null = null;
  accountSettings: AccountSettings | null = null;
  auxProgram: AuxProgram | null = null;
  userLogSettings: UserLogSettings | null = null;
  pidSettings: PidSettings | null = null;
  graphFavorites: GraphFavorites | null = null;
  alertSettings: AlertSettings | null = null;
  pwmSettings: PwmSettings | null = null;
  networkNodes: NetworkNodes | null = null;
  climacellTimes: ClimacellTimes | null = null;
  orbitStatus: OrbitStatus | null = null;
  orbitDiscovery: OrbitDiscovery | null = null;

  // Raw storage for message types not yet decoded into typed interfaces
  private rawMessages: Map<number, Buffer> = new Map();

  handleMessage(msgId: number, data: Buffer): void {
    this.rawMessages.set(msgId, data);

    switch (msgId) {
      case 10: this.decodeSystemStatus(data); break;
      case 11: this.decodeEquipmentStatus(data); break;
      case 12: this.decodeWarningReport(data); break;
      case 13: this.decodeSensorData(data); break;
      case 14: this.decodeRuntimes(data); break;
      case 15: this.decodeHumidModes(data); break;
      case 16: this.decodeAuxSwitches(data); break;
      case 17: /* DataLoadStatus — handled by bridge */ break;
      case 18: this.decodeFanRuntime(data); break;
      case 20: this.decodeBasicSetup(data); break;
      case 21: this.decodeDateTime(data); break;
      case 22: this.decodeVersionInfo(data); break;
      case 23: this.decodeServiceInfo(data); break;
      case 24: this.decodeIoConfig(data); break;
      case 25: this.decodeIoDefinition(data); break;
      case 26: this.decodeAnalogBoard(data); break;
      case 27: this.decodeAvailableIo(data); break;
      case 28: this.decodeSensorLabels(data); break;
      case 29: this.decodeAccountSettings(data); break;
      case 31: this.decodePasswordResponse(data); break;
      case 40: this.decodePlenumSettings(data); break;
      case 41: this.decodeFanSpeedSettings(data); break;
      case 42: this.decodeFanBoostSettings(data); break;
      case 43: this.decodeRampRateSettings(data); break;
      case 44: this.decodeRefrigSettings(data); break;
      case 45: this.decodeBurnerSettings(data); break;
      case 46: this.decodeCo2Settings(data); break;
      case 47: this.decodeCureSettings(data); break;
      case 48: this.decodeClimacellSettings(data); break;
      case 49: this.decodeClimacellTimes(data); break;
      case 50: this.decodeHumidCtrlSettings(data); break;
      case 51: this.decodeOutsideAirSettings(data); break;
      case 52: this.decodeMiscSettings(data); break;
      case 53: this.decodeFailureSettings(data); break;
      case 54: this.decodeFailureSettings2(data); break;
      case 55: this.decodeTempAlarmSettings(data); break;
      case 56: this.decodeDoorSettings(data); break;
      case 57: this.decodeLoadMonitorSettings(data); break;
      case 58: this.decodeAuxProgram(data); break;
      case 59: this.decodeUserLogSettings(data); break;
      case 60: this.decodePidSettings(data); break;
      case 61: this.decodeGraphFavorites(data); break;
      case 62: this.decodeEmailSettings(data); break;
      case 63: this.decodeAlertSettings(data); break;
      case 64: this.decodePwmSettings(data); break;
      case 65: this.decodeNetworkNodes(data); break;
      case 101: this.decodeHeartbeat(data); break;
      case 120: this.decodeOrbitStatus(data); break;
      case 121: this.decodeOrbitDiscovery(data); break;
      default:
        // Store raw for messages we pass through (climacellTimes, auxProgram, etc.)
        break;
    }

    this.emit('update', msgId);
  }

  /**
   * Get all data as a JSON-serializable object for REST API.
   */
  toJSON(): Record<string, any> {
    return {
      systemStatus: this.systemStatus,
      equipmentStatus: this.equipmentStatus,
      dateTime: this.dateTime,
      basicSetup: this.basicSetup,
      warningReport: this.warningReport,
      heartbeat: this.heartbeat,
      plenumSettings: this.plenumSettings,
      burnerSettings: this.burnerSettings,
      co2Settings: this.co2Settings,
      cureSettings: this.cureSettings,
      fanBoostSettings: this.fanBoostSettings,
      climacellSettings: this.climacellSettings,
      fanSpeedSettings: this.fanSpeedSettings,
      rampRateSettings: this.rampRateSettings,
      refrigSettings: this.refrigSettings,
      humidCtrlSettings: this.humidCtrlSettings,
      outsideAirSettings: this.outsideAirSettings,
      miscSettings: this.miscSettings,
      failureSettings: this.failureSettings,
      failureSettings2: this.failureSettings2,
      tempAlarmSettings: this.tempAlarmSettings,
      doorSettings: this.doorSettings,
      loadMonitorSettings: this.loadMonitorSettings,
      versionInfo: this.versionInfo,
      serviceInfo: this.serviceInfo,
      ioConfig: this.ioConfig,
      ioDefinition: this.ioDefinition,
      emailSettings: this.emailSettings,
      runtimes: this.runtimes,
      fanRuntime: this.fanRuntime,
      humidModes: this.humidModes,
      auxSwitches: this.auxSwitches,
      analogBoards: Object.fromEntries(this.analogBoards),
      availableIo: this.availableIo,
      sensorData: this.sensorData,
      sensorLabels: this.sensorLabels,
      accountSettings: this.accountSettings,
      auxProgram: this.auxProgram,
      userLogSettings: this.userLogSettings,
      pidSettings: this.pidSettings,
      graphFavorites: this.graphFavorites,
      alertSettings: this.alertSettings,
      pwmSettings: this.pwmSettings,
      networkNodes: this.networkNodes,
      climacellTimes: this.climacellTimes,
      orbitStatus: this.orbitStatus,
      orbitDiscovery: this.orbitDiscovery,
    };
  }

  /* ── Decoders ────────────────────────────────────────────────────── */

  private decodeSystemStatus(data: Buffer): void {
    const f = pbDecode(data);
    this.systemStatus = {
      plenumTemp:   gf(f, 1),
      plenumHumid:  gf(f, 2),
      outsideTemp:  gf(f, 3),
      remoteTemp:   gf(f, 4),
      outsideHumid: gf(f, 5),
      remoteHumid:  gf(f, 6),
      startTemp:    gf(f, 7),
      returnHumid:  gf(f, 8),
      returnTemp:   gf(f, 9),
      co2Level:     gf(f, 10),
      fanSpeed:     gs(f, 11),
      coolOutput:   gs(f, 12),
      coolLabel:    gs(f, 13),
      burnerOutput: gs(f, 14),
      calcHumid:    gf(f, 15),
      currentMode:  gv(f, 16),
      cureState:    gv(f, 17),
    };
    this.emit('systemStatus', this.systemStatus);
  }

  private decodeEquipmentStatus(data: Buffer): void {
    const items = ga(pbDecode(data), 1).map(itemBuf => {
      const f = pbDecode(itemBuf);
      return {
        eqIndex:   gv(f, 1),
        outputOn:  gv(f, 2) !== 0,
        remoteOff: gv(f, 3),
        alarm:     gv(f, 4),
        label:     gs(f, 5),
      } as EquipState;
    });
    this.equipmentStatus = { items };
    this.emit('equipmentStatus', this.equipmentStatus);
  }

  private decodeWarningReport(data: Buffer): void {
    const warnings = ga(pbDecode(data), 1).map(wBuf => {
      const f = pbDecode(wBuf);
      const code = gv(f, 1);
      const message = gs(f, 3) || ORBIT_WARN_NAMES[code] || '';
      return {
        code,
        severity:  gv(f, 2),
        message,
        eqIndex:   gv(f, 4),
        timestamp: gv(f, 5),
      } as Warning;
    });
    this.warningReport = { warnings };
    this.emit('warningReport', this.warningReport);
  }

  private decodeDateTime(data: Buffer): void {
    const f = pbDecode(data);
    this.dateTime = {
      dateStr: gs(f, 1),
      timeStr: gs(f, 2),
      amPm:    gv(f, 3),
    };
    this.emit('dateTime', this.dateTime);
  }

  private decodeBasicSetup(data: Buffer): void {
    const f = pbDecode(data);
    this.basicSetup = {
      storageName: gs(f, 1),
      tempType:    gv(f, 2),
      mode:        gv(f, 3),
      homePage:    gs(f, 4),
      systemMode:  gv(f, 5),
      language:    gv(f, 6),
      masterIp:    gs(f, 7),
      multiView:   gv(f, 8),
      loginPw:     gs(f, 9),
      localLogin:  gv(f, 10),
      animations:  gv(f, 11),
    };
    this.emit('basicSetup', this.basicSetup);
  }

  private decodeHeartbeat(data: Buffer): void {
    const f = pbDecode(data);
    this.heartbeat = {
      uptimeSec:  gv(f, 1),
      freeHeap:   gv(f, 2),
      msgTxCount: gv(f, 3),
      msgRxCount: gv(f, 4),
    };
    this.emit('heartbeat', this.heartbeat);
  }

  private decodePlenumSettings(data: Buffer): void {
    const f = pbDecode(data);
    this.plenumSettings = {
      tempSetpoint:      gf(f, 1),
      humidSetpoint:     gv(f, 2),
      humidSetpointRef:  gv(f, 3),
      burnerTempSetpoint: gf(f, 4),
      burnerThreshold:   gs(f, 5),
    };
    this.emit('plenumSettings', this.plenumSettings);
  }

  private decodeBurnerSettings(data: Buffer): void {
    const f = pbDecode(data);
    this.burnerSettings = {
      on:     gv(f, 1),
      low:    gv(f, 2),
      pGain:  gf(f, 3),
      iGain:  gf(f, 4),
      dGain:  gf(f, 5),
      uLimit: gf(f, 6),
      mode:   gv(f, 7),
      manual: gv(f, 8),
    };
    this.emit('burnerSettings', this.burnerSettings);
  }

  private decodeCo2Settings(data: Buffer): void {
    const f = pbDecode(data);
    this.co2Settings = {
      mode:            gv(f, 1),
      minTemp:         gf(f, 2),
      maxTemp:         gf(f, 3),
      durationMinutes: gv(f, 4),
      cycleOrSet:      gv(f, 5),
      fanOutput:       gv(f, 6),
      doorOutput:      gv(f, 7),
    };
    this.emit('co2Settings', this.co2Settings);
  }

  private decodeCureSettings(data: Buffer): void {
    const f = pbDecode(data);
    this.cureSettings = {
      startTemp:      gf(f, 1),
      humidRef:       gv(f, 2),
      startHumid:     gf(f, 3),
      humidHighLimit: gf(f, 4),
    };
    this.emit('cureSettings', this.cureSettings);
  }

  private decodeFanBoostSettings(data: Buffer): void {
    const f = pbDecode(data);
    this.fanBoostSettings = {
      mode:     gv(f, 1),
      speed:    gv(f, 2),
      interval: gv(f, 3),
      duration: gv(f, 4),
      temp:     gf(f, 5),
    };
    this.emit('fanBoostSettings', this.fanBoostSettings);
  }

  private decodeClimacellSettings(data: Buffer): void {
    const f = pbDecode(data);
    this.climacellSettings = {
      efficiency: gv(f, 1),
      altitude:   gv(f, 2),
      altUnits:   gv(f, 3),
      pGain:      gf(f, 4),
      iGain:      gf(f, 5),
      dGain:      gf(f, 6),
      uLimit:     gf(f, 7),
    };
    this.emit('climacellSettings', this.climacellSettings);
  }

  private decodeFanSpeedSettings(data: Buffer): void {
    const f = pbDecode(data);
    this.fanSpeedSettings = {
      maxSpeed:     gv(f, 1),
      minSpeed:     gv(f, 2),
      coolSpeed:    gv(f, 3),
      recircSpeed:  gv(f, 7),
      updatePeriod: gv(f, 5),
    };
    this.emit('fanSpeedSettings', this.fanSpeedSettings);
  }

  private decodeRampRateSettings(data: Buffer): void {
    const f = pbDecode(data);
    this.rampRateSettings = {
      ratePerDay: gf(f, 1),
      enabled:    gv(f, 2),
    };
    this.emit('rampRateSettings', this.rampRateSettings);
  }

  private decodeRefrigSettings(data: Buffer): void {
    const f = pbDecode(data);
    const decodeStage = (buf: Buffer) => {
      const sf = pbDecode(buf);
      return { on: gv(sf, 1), off: gv(sf, 2), diagnostic: gv(sf, 3) };
    };
    this.refrigSettings = {
      mode:             gv(f, 7) || gv(f, 1),
      stages:           ga(f, 2).map(decodeStage),
      defrosts:         ga(f, 14).map(decodeStage),
      pGain:            gf(f, 3),
      iGain:            gf(f, 4),
      dGain:            gf(f, 5),
      uLimit:           gf(f, 6),
      defrostInterval:  gv(f, 8),
      defrostDuration:  gv(f, 9),
      purge:            gv(f, 10),
      purgeThreshold:   gv(f, 11),
      limit:            gf(f, 12),
      failMode:         gv(f, 13),
    };
    this.emit('refrigSettings', this.refrigSettings);
  }

  private decodeHumidCtrlSettings(data: Buffer): void {
    // HumidCtrlSettings = repeated HumidCtrlEntry entries = 1
    // Each entry: index(1), mode(2), coolOn(3), coolOff(4),
    //             recOn(5), recOff(6), refOn(7), refOff(8)
    const f = pbDecode(data);
    const subs = ga(f, 1);
    const entries: HumidCtrlEntry[] = subs.map(buf => {
      const ef = pbDecode(buf);
      return {
        index:   gv(ef, 1),
        mode:    gv(ef, 2),
        coolOn:  gv(ef, 3),
        coolOff: gv(ef, 4),
        recOn:   gv(ef, 5),
        recOff:  gv(ef, 6),
        refOn:   gv(ef, 7),
        refOff:  gv(ef, 8),
      };
    });
    // Sort by index so consumers can rely on order
    entries.sort((a, b) => a.index - b.index);
    this.humidCtrlSettings = { entries };
    this.emit('humidCtrlSettings', this.humidCtrlSettings);
  }

  private decodeOutsideAirSettings(data: Buffer): void {
    const f = pbDecode(data);
    this.outsideAirSettings = {
      mode:         gv(f, 1),
      differential: gf(f, 2),
    };
    this.emit('outsideAirSettings', this.outsideAirSettings);
  }

  private decodeMiscSettings(data: Buffer): void {
    const f = pbDecode(data);
    this.miscSettings = {
      heatTempThresh: gf(f, 1),
      kbPref:         gv(f, 2),
      lightsFailUnits: gv(f, 3),
      cavStandbyOn:   gv(f, 4),
    };
    this.emit('miscSettings', this.miscSettings);
  }

  private decodeFailureSettings(data: Buffer): void {
    const f = pbDecode(data);
    this.failureSettings = {
      values: {
        fanMode:  gv(f, 1),
        fanTimer: gv(f, 2),
      },
    };
    // Also decode repeated extra_limits (field 12) if present
    const extras = ga(f, 12);
    extras.forEach((buf, i) => {
      const ef = pbDecode(buf);
      this.failureSettings!.values[`fail${i}Mode`]  = gv(ef, 1);
      this.failureSettings!.values[`fail${i}Timer`] = gv(ef, 2);
    });
    this.emit('failureSettings', this.failureSettings);
  }

  private decodeFailureSettings2(data: Buffer): void {
    const f = pbDecode(data);
    this.failureSettings2 = {
      values: {
        outAirMode: gv(f, 1),
        sensorMode: gv(f, 2),
      },
    };
    this.emit('failureSettings2', this.failureSettings2);
  }

  private decodeTempAlarmSettings(data: Buffer): void {
    // Wire layout matches firmware NovaMsg_SendTempAlarmSettings + the
    // legacy AlarmTempLow= CSV that the plensetup page consumes:
    //   1=lowTemp, 2=lowMin, 3=highTemp, 4=highMin, 5=cureLow, 6=cureHigh.
    const f = pbDecode(data);
    this.tempAlarmSettings = {
      lowTemp:  gf(f, 1),
      lowMin:   gv(f, 2),
      highTemp: gf(f, 3),
      highMin:  gv(f, 4),
      cureLow:  gf(f, 5),
      cureHigh: gf(f, 6),
    } as any;
    this.emit('tempAlarmSettings', this.tempAlarmSettings);
  }

  private decodeDoorSettings(data: Buffer): void {
    const f = pbDecode(data);
    this.doorSettings = {
      pGain:        gf(f, 1),
      iGain:        gf(f, 2),
      dGain:        gf(f, 3),
      uLimit:       gf(f, 4),
      actuatorTime: gv(f, 5),
      coolAirCycle: gv(f, 6),
    };
    this.emit('doorSettings', this.doorSettings);
  }

  private decodeLoadMonitorSettings(data: Buffer): void {
    const f = pbDecode(data);
    this.loadMonitorSettings = {
      bay1Label: gs(f, 1),
      bay2Label: gs(f, 2),
      bayCount:  gv(f, 3),
    };
    this.emit('loadMonitorSettings', this.loadMonitorSettings);
  }

  private decodeVersionInfo(data: Buffer): void {
    const f = pbDecode(data);
    this.versionInfo = {
      armVersion:        gs(f, 1),
      bootloaderVersion: gs(f, 2),
    };
    this.emit('versionInfo', this.versionInfo);
  }

  private decodeServiceInfo(data: Buffer): void {
    const f = pbDecode(data);
    this.serviceInfo = {
      dealerName:  gs(f, 1),
      dealerPhone: gs(f, 2),
      techName:    gs(f, 3),
      techPhone:   gs(f, 4),
    };
    this.emit('serviceInfo', this.serviceInfo);
  }

  private decodeIoConfig(data: Buffer): void {
    const f = pbDecode(data);
    // Repeated uint32 fields
    const outs = f.filter(x => x.field === 1 && x.wireType === PB_VARINT).map(x => x.value as number);
    const ins  = f.filter(x => x.field === 2 && x.wireType === PB_VARINT).map(x => x.value as number);
    this.ioConfig = { outputMap: outs, inputMap: ins };
    this.emit('ioConfig', this.ioConfig);
  }

  private decodeIoDefinition(data: Buffer): void {
    const entryBufs = ga(pbDecode(data), 1);
    const entries = entryBufs.map(buf => {
      const ef = pbDecode(buf);
      return {
        index:     gv(ef, 1),
        name:      gs(ef, 2),
        mode:      gv(ef, 3),
        ioPin:     gv(ef, 4),
        renamable: gv(ef, 5) !== 0,
        visible:   gv(ef, 6) !== 0,
      } as IoEntry;
    });
    this.ioDefinition = { entries };
    this.emit('ioDefinition', this.ioDefinition);
  }

  private decodeEmailSettings(data: Buffer): void {
    const f = pbDecode(data);
    this.emailSettings = {
      server:   gs(f, 1),
      port:     gv(f, 2) || parseInt(gs(f, 2)) || 0,
      username: gs(f, 3),
      password: gs(f, 4),
      fromAddr: gs(f, 5),
      toAddr:   gs(f, 6),
      enabled:  gv(f, 7),
    };
    this.emit('emailSettings', this.emailSettings);
  }

  private decodePasswordResponse(data: Buffer): void {
    const f = pbDecode(data);
    this.passwordResponse = {
      level:   gv(f, 1),
      session: gv(f, 2),
    };
    this.emit('passwordResponse', this.passwordResponse);
  }

  private decodeRuntimes(data: Buffer): void {
    const f = pbDecode(data);
    const entries = ga(f, 1).map(buf => {
      const ef = pbDecode(buf);
      return { slot: gv(ef, 1), value: gv(ef, 2) };
    });
    this.runtimes = { entries };
    this.emit('runtimes', this.runtimes);
  }

  private decodeFanRuntime(data: Buffer): void {
    const f = pbDecode(data);
    this.fanRuntime = {
      dailyHours: gv(f, 1), dailyMinutes: gv(f, 2),
      totalHours: gv(f, 3), totalMinutes: gv(f, 4),
    };
    this.emit('fanRuntime', this.fanRuntime);
  }

  private decodeHumidModes(data: Buffer): void {
    const f = pbDecode(data);
    this.humidModes = {
      head1: gv(f, 1), pump1: gv(f, 2),
      head2: gv(f, 3), pump2: gv(f, 4),
    };
    this.emit('humidModes', this.humidModes);
  }

  private decodeAuxSwitches(data: Buffer): void {
    const f = pbDecode(data);
    const states = f.filter(x => x.field === 1 && x.wireType === PB_VARINT).map(x => x.value as number);
    this.auxSwitches = { states };
    this.emit('auxSwitches', this.auxSwitches);
  }

  private decodeAnalogBoard(data: Buffer): void {
    const f = pbDecode(data);
    const sensors = ga(f, 6).map(buf => {
      const sf = pbDecode(buf);
      return {
        slot: gv(sf, 1), type: gv(sf, 2), label: gs(sf, 3),
        offset: gf(sf, 4), disabled: gv(sf, 5) !== 0, value: gf(sf, 6),
      } as AnalogSensor;
    });
    const board: AnalogBoard = {
      address: gv(f, 1), type: gv(f, 2), label: gs(f, 3),
      version: gs(f, 4), disabled: gv(f, 5) !== 0, sensors,
    };
    this.analogBoards.set(board.address, board);
    this.emit('analogBoard', board);
  }

  private decodeAvailableIo(data: Buffer): void {
    const f = pbDecode(data);
    const outputPins = f.filter(x => x.field === 1 && x.wireType === PB_VARINT).map(x => x.value as number);
    const inputPins  = f.filter(x => x.field === 2 && x.wireType === PB_VARINT).map(x => x.value as number);
    this.availableIo = { outputPins, inputPins };
    this.emit('availableIo', this.availableIo);
  }

  private decodeSensorData(data: Buffer): void {
    const f = pbDecode(data);
    const decodeReadings = (field: number) => ga(f, field).map(buf => {
      const sf = pbDecode(buf);
      return { index: gv(sf, 1), value: gf(sf, 2), valid: gv(sf, 3) !== 0 } as SensorReading;
    });
    this.sensorData = { temps: decodeReadings(1), humids: decodeReadings(2) };
    this.emit('sensorData', this.sensorData);
  }

  private decodeSensorLabels(data: Buffer): void {
    const f = pbDecode(data);
    const decodeLabels = (field: number) => ga(f, field).map(buf => {
      const sf = pbDecode(buf);
      return { index: gv(sf, 1), label: gs(sf, 2) } as SensorLabel;
    });
    this.sensorLabels = { temps: decodeLabels(1), humids: decodeLabels(2) };
    this.emit('sensorLabels', this.sensorLabels);
  }

  private decodeAccountSettings(data: Buffer): void {
    const f = pbDecode(data);
    const users = ga(f, 1).map(buf => {
      const uf = pbDecode(buf);
      return { slot: gv(uf, 1), userId: gs(uf, 2) };
    });
    this.accountSettings = { users, count: gv(f, 2), hasLoginPw: gv(f, 3) !== 0 };
    this.emit('accountSettings', this.accountSettings);
  }

  private decodeAuxProgram(data: Buffer): void {
    const f = pbDecode(data);
    const programs = ga(f, 1).map(buf => {
      const pf = pbDecode(buf);
      const rules = ga(pf, 6).map(rb => {
        const rf = pbDecode(rb);
        return {
          type: gv(rf, 1), ioIndex: gv(rf, 2), state: gv(rf, 3),
          op: gv(rf, 4), sensorValue: gf(rf, 5), andOr: gv(rf, 6), referenceIndex: gv(rf, 7),
        } as AuxRule;
      });
      return {
        auxIndex: gv(pf, 1), eqIndex: gv(pf, 2),
        dutyCycle: gv(pf, 3), period: gv(pf, 4), units: gv(pf, 5), rules,
      } as AuxProgramEntry;
    });
    this.auxProgram = { programs };
    this.emit('auxProgram', this.auxProgram);
  }

  private decodeUserLogSettings(data: Buffer): void {
    const f = pbDecode(data);
    this.userLogSettings = { interval: gv(f, 1), wrap: gv(f, 2) };
    this.emit('userLogSettings', this.userLogSettings);
  }

  private decodePidSettings(data: Buffer): void {
    const f = pbDecode(data);
    this.pidSettings = { eqIndex: gv(f, 1), wrap: gv(f, 6) };
    this.emit('pidSettings', this.pidSettings);
  }

  private decodeGraphFavorites(data: Buffer): void {
    const f = pbDecode(data);
    this.graphFavorites = { csv: gs(f, 1) };
    this.emit('graphFavorites', this.graphFavorites);
  }

  private decodeAlertSettings(data: Buffer): void {
    const f = pbDecode(data);
    const flags = f.filter(x => x.field === 1 && x.wireType === PB_VARINT).map(x => x.value as number);
    this.alertSettings = { flags };
    this.emit('alertSettings', this.alertSettings);
  }

  private decodePwmSettings(data: Buffer): void {
    const f = pbDecode(data);
    const channels = ga(f, 1).map(buf => {
      const cf = pbDecode(buf);
      return { index: gv(cf, 1), enabled: gv(cf, 2), channel: gv(cf, 3) } as PwmChannel;
    });
    this.pwmSettings = { channels };
    this.emit('pwmSettings', this.pwmSettings);
  }

  private decodeNetworkNodes(data: Buffer): void {
    const f = pbDecode(data);
    const nodes = ga(f, 1).map(buf => {
      const nf = pbDecode(buf);
      return { slot: gv(nf, 1), ip: gs(nf, 2), id: gs(nf, 3) } as NetworkNode;
    });
    this.networkNodes = { nodes };
    this.emit('networkNodes', this.networkNodes);
  }

  private decodeClimacellTimes(data: Buffer): void {
    const f = pbDecode(data);
    const slots = f.filter(x => x.field === 1 && x.wireType === PB_VARINT).map(x => x.value as number);
    this.climacellTimes = { slots };
    this.emit('climacellTimes', this.climacellTimes);
  }

  /* ── Orbit decoders ────────────────────────────────────────────────── */

  private decodeOrbitBoardStatus(buf: Buffer): OrbitBoardStatus {
    const f = pbDecode(buf);
    // Decode zigzag for signed legacy_slot
    const zzVal = gv(f, 12);
    const legacySlot = (zzVal & 1) ? -((zzVal >> 1) + 1) : (zzVal >> 1);

    return {
      slot: gv(f, 1),
      dipswitchId: gv(f, 2),
      connected: gv(f, 3) !== 0,
      commErrors: gv(f, 4),
      estopActive: gv(f, 5) !== 0,
      safeMode: gv(f, 6) !== 0,
      cpuTemp: gf(f, 7),
      uptimeSecs: gv(f, 8),
      firmwareVer: gv(f, 9),
      role: gv(f, 10) as OrbitRole,
      zoneId: gv(f, 11),
      legacySlot,
      refrigStage: gv(f, 13),
      ipAddress: gs(f, 14),
    };
  }

  private decodeOrbitStatus(data: Buffer): void {
    const f = pbDecode(data);
    const boards = ga(f, 1).map(buf => this.decodeOrbitBoardStatus(buf));
    this.orbitStatus = { boards };
    this.emit('orbitStatus', this.orbitStatus);
  }

  private decodeOrbitDiscovery(data: Buffer): void {
    const f = pbDecode(data);
    const boards = ga(f, 3).map(buf => this.decodeOrbitBoardStatus(buf));
    this.orbitDiscovery = {
      maxSlots: gv(f, 1),
      boardsFound: gv(f, 2),
      boards,
    };
    this.emit('orbitDiscovery', this.orbitDiscovery);
  }
}
