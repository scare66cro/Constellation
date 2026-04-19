/**
 * novaSerialBridge.ts — Constellation binary serial bridge
 *
 * Replaces serialBridge.ts (RTS/ACK/REPOST state machine) with a
 * push + request/response model using COBS + CRC-16 + Protobuf.
 *
 * Architecture:
 *   - Firmware pushes SystemStatus, EquipmentStatus, Warnings automatically
 *   - Bridge sends commands (EquipmentCmd, SettingsUpdate, SystemCmd)
 *   - Firmware responds with ACK (seq-number based matching)
 *   - No polling, no RTS/ACK handshake, no MultiMsg fragmentation
 *
 * The bridge emits typed events that the data store and WS manager consume.
 */

import { EventEmitter } from 'events';
import { NovaProtocol, NOVA_PROTOCOL_VERSION, type NovaProtocolStats } from './novaProtocol.js';

/* ──────────────────────────────────────────────────────────────────────── *
 *  Minimal protobuf encode/decode helpers                                  *
 *  These will be replaced with ts-proto generated code once codegen runs.  *
 * ──────────────────────────────────────────────────────────────────────── */

const PB_VARINT = 0;
const PB_64BIT = 1;
const PB_LEN = 2;
const PB_32BIT = 5;

interface PbField {
  field: number;
  wireType: number;
  value: number | bigint | Buffer;
}

function pbDecodeFields(buf: Buffer): PbField[] {
  const fields: PbField[] = [];
  let pos = 0;

  function readVarint(): number {
    let val = 0, shift = 0;
    while (pos < buf.length) {
      const byte = buf[pos++];
      val |= (byte & 0x7F) << shift;
      if ((byte & 0x80) === 0) return val >>> 0;
      shift += 7;
      if (shift >= 35) break;
    }
    return val >>> 0;
  }

  while (pos < buf.length) {
    const tag = readVarint();
    const wireType = tag & 0x07;
    const fieldNum = tag >>> 3;
    if (fieldNum === 0) break;

    switch (wireType) {
      case PB_VARINT:
        fields.push({ field: fieldNum, wireType, value: readVarint() });
        break;
      case PB_64BIT:
        fields.push({ field: fieldNum, wireType, value: buf.subarray(pos, pos + 8) });
        pos += 8;
        break;
      case PB_LEN: {
        const len = readVarint();
        fields.push({ field: fieldNum, wireType, value: Buffer.from(buf.subarray(pos, pos + len)) });
        pos += len;
        break;
      }
      case PB_32BIT: {
        const fbuf = buf.subarray(pos, pos + 4);
        fields.push({ field: fieldNum, wireType, value: Buffer.from(fbuf) });
        pos += 4;
        break;
      }
      default:
        return fields; // Unknown wire type — stop
    }
  }
  return fields;
}

function pbGetVarint(fields: PbField[], fieldNum: number): number {
  const f = fields.find(f => f.field === fieldNum && f.wireType === PB_VARINT);
  return f ? (f.value as number) : 0;
}

function pbGetSubmsg(fields: PbField[], fieldNum: number): Buffer | null {
  const f = fields.find(f => f.field === fieldNum && f.wireType === PB_LEN);
  return f ? (f.value as Buffer) : null;
}

function pbGetFloat(fields: PbField[], fieldNum: number): number {
  const f = fields.find(f => f.field === fieldNum && f.wireType === PB_32BIT);
  if (!f) return 0;
  const buf = f.value as Buffer;
  return buf.readFloatLE(0);
}

function pbGetString(fields: PbField[], fieldNum: number): string {
  const f = fields.find(f => f.field === fieldNum && f.wireType === PB_LEN);
  return f ? (f.value as Buffer).toString('utf8') : '';
}

function pbGetAllSubmsg(fields: PbField[], fieldNum: number): Buffer[] {
  return fields
    .filter(f => f.field === fieldNum && f.wireType === PB_LEN)
    .map(f => f.value as Buffer);
}

/* ──────────────────────────────────────────────────────────────────────── *
 *  Minimal protobuf encoder                                                *
 * ──────────────────────────────────────────────────────────────────────── */

class PbEncoder {
  private parts: Buffer[] = [];

  varint(field: number, val: number): this {
    if (val === 0) return this;
    this.writeTag(field, PB_VARINT);
    this.writeVarint(val);
    return this;
  }

  /** Same as `varint` but always emits the field, even when `val === 0`.
   * Use for fields where the receiver must distinguish "explicitly zero"
   * from "absent" — index fields, mode enums where 0 is meaningful, etc.
   * Mirrors firmware-side `pb_uint32_force`. */
  varintForce(field: number, val: number): this {
    this.writeTag(field, PB_VARINT);
    this.writeVarint(val);
    return this;
  }

  float(field: number, val: number): this {
    if (val === 0) return this;
    this.writeTag(field, PB_32BIT);
    const buf = Buffer.alloc(4);
    buf.writeFloatLE(val, 0);
    this.parts.push(buf);
    return this;
  }

  string(field: number, val: string): this {
    if (!val) return this;
    const data = Buffer.from(val, 'utf8');
    this.writeTag(field, PB_LEN);
    this.writeVarint(data.length);
    this.parts.push(data);
    return this;
  }

  submsg(field: number, data: Buffer): this {
    /* Note: empty submsg IS valid protobuf and carries meaning (field presence).
     * In particular, a DataRequest with request_type=0 and param=0 produces an
     * empty inner buffer; we must still emit the outer submsg so the firmware
     * can dispatch by message type. */
    this.writeTag(field, PB_LEN);
    this.writeVarint(data.length);
    if (data.length > 0) this.parts.push(data);
    return this;
  }

  bool(field: number, val: boolean): this {
    if (!val) return this;
    this.writeTag(field, PB_VARINT);
    this.writeVarint(1);
    return this;
  }

  encode(): Buffer {
    return Buffer.concat(this.parts);
  }

  private writeTag(field: number, wireType: number) {
    this.writeVarint((field << 3) | wireType);
  }

  private writeVarint(val: number) {
    const bytes: number[] = [];
    do {
      let byte = val & 0x7F;
      val >>>= 7;
      if (val > 0) byte |= 0x80;
      bytes.push(byte);
    } while (val > 0);
    this.parts.push(Buffer.from(bytes));
  }
}

/* ──────────────────────────────────────────────────────────────────────── *
 *  Envelope field numbers (must match envelope.proto)                      *
 * ──────────────────────────────────────────────────────────────────────── */

// Firmware → Bridge: push
const MSG_SYSTEM_STATUS      = 10;
const MSG_EQUIPMENT_STATUS   = 11;
const MSG_WARNING_REPORT     = 12;
const MSG_SENSOR_DATA        = 13;
const MSG_RUNTIMES           = 14;
const MSG_HUMID_MODES        = 15;
const MSG_AUX_SWITCHES       = 16;
const MSG_DATA_LOAD_STATUS   = 17;
const MSG_FAN_RUNTIME        = 18;

// Firmware → Bridge: settings
const MSG_BASIC_SETUP        = 20;
const MSG_DATE_TIME          = 21;
const MSG_VERSION_INFO       = 22;
const MSG_SERVICE_INFO       = 23;
const MSG_IO_CONFIG          = 24;
const MSG_IO_DEFINITION      = 25;
const MSG_ANALOG_BOARD       = 26;
const MSG_AVAILABLE_IO       = 27;
const MSG_SENSOR_LABELS      = 28;
const MSG_ACCOUNT_SETTINGS   = 29;

// Firmware → Bridge: responses
const MSG_LOG_CHUNK          = 30;
const MSG_PASSWORD_RESPONSE  = 31;

// Firmware → Bridge: settings pages (40-65)
const MSG_PLENUM_SETTINGS    = 40;
const MSG_FAN_SPEED_SETTINGS = 41;
const MSG_FAN_BOOST_SETTINGS = 42;
const MSG_RAMP_RATE_SETTINGS = 43;
const MSG_REFRIG_SETTINGS    = 44;
const MSG_BURNER_SETTINGS    = 45;
const MSG_CO2_SETTINGS       = 46;
const MSG_CURE_SETTINGS      = 47;
const MSG_CLIMACELL_SETTINGS = 48;
const MSG_CLIMACELL_TIMES    = 49;
const MSG_HUMID_CTRL         = 50;
const MSG_OUTSIDE_AIR        = 51;
const MSG_MISC_SETTINGS      = 52;
const MSG_FAILURE_SETTINGS   = 53;
const MSG_FAILURE_SETTINGS2  = 54;
const MSG_TEMP_ALARM         = 55;
const MSG_DOOR_SETTINGS      = 56;
const MSG_LOAD_MONITOR       = 57;
const MSG_AUX_PROGRAM        = 58;
const MSG_USER_LOG           = 59;
const MSG_PID_SETTINGS       = 60;
const MSG_GRAPH_FAVORITES    = 61;
const MSG_EMAIL_SETTINGS     = 62;
const MSG_ALERT_SETTINGS     = 63;
const MSG_PWM_SETTINGS       = 64;
const MSG_NETWORK_NODES      = 65;

// Firmware → Bridge: log data push
const MSG_LOG_RECORD         = 70;
const MSG_ACTIVITY_EVENT     = 71;

// Bridge → Firmware: commands
const MSG_EQUIPMENT_CMD      = 80;
const MSG_REFRIG_DIAG_CMD    = 81;
const MSG_SYSTEM_CMD         = 82;
const MSG_LOG_QUERY          = 83;
const MSG_PASSWORD_AUTH      = 84;
const MSG_NETWORK_NODE_CMD   = 85;
const MSG_IO_NAME_UPDATE     = 86;

// Bridge → Firmware: settings updates
const MSG_SETTINGS_UPDATE    = 90;

// Protocol control
const MSG_ACK                = 100;
const MSG_HEARTBEAT          = 101;
const MSG_DATA_REQUEST       = 102;

// Firmware update (fields match firmware.proto / envelope.proto 110-115)
const MSG_FW_BEGIN_UPDATE    = 110;
const MSG_FW_DATA_CHUNK      = 111;
const MSG_FW_FINALIZE_UPDATE = 112;
const MSG_FW_ACTIVATE_BANK   = 113;
const MSG_FW_UPDATE_STATUS   = 114;
const MSG_FW_BANK_INFO       = 115;

// Orbit module management (120-129)
const MSG_ORBIT_STATUS       = 120;
const MSG_ORBIT_DISCOVERY    = 121;
const MSG_ORBIT_ROLE_ASSIGN  = 122;

/* ──────────────────────────────────────────────────────────────────────── *
 *  Message name map for logging                                           *
 * ──────────────────────────────────────────────────────────────────────── */

const MSG_NAMES: Record<number, string> = {
  [MSG_SYSTEM_STATUS]:    'SystemStatus',
  [MSG_EQUIPMENT_STATUS]: 'EquipmentStatus',
  [MSG_WARNING_REPORT]:   'WarningReport',
  [MSG_SENSOR_DATA]:      'SensorData',
  [MSG_RUNTIMES]:         'Runtimes',
  [MSG_HUMID_MODES]:      'HumidModes',
  [MSG_AUX_SWITCHES]:     'AuxSwitches',
  [MSG_DATA_LOAD_STATUS]: 'DataLoadStatus',
  [MSG_FAN_RUNTIME]:      'FanRuntime',
  [MSG_BASIC_SETUP]:      'BasicSetup',
  [MSG_DATE_TIME]:        'DateTime',
  [MSG_VERSION_INFO]:     'VersionInfo',
  [MSG_IO_CONFIG]:        'IoConfig',
  [MSG_IO_DEFINITION]:    'IoDefinition',
  [MSG_ANALOG_BOARD]:     'AnalogBoard',
  [MSG_SENSOR_LABELS]:    'SensorLabels',
  [MSG_ACCOUNT_SETTINGS]: 'AccountSettings',
  [MSG_LOG_CHUNK]:        'LogChunk',
  [MSG_LOG_RECORD]:       'LogRecord',
  [MSG_ACTIVITY_EVENT]:   'ActivityEvent',
  [MSG_ACK]:              'Ack',
  [MSG_HEARTBEAT]:        'Heartbeat',
  [MSG_PLENUM_SETTINGS]:  'PlenumSettings',
  [MSG_BURNER_SETTINGS]:  'BurnerSettings',
  [MSG_CO2_SETTINGS]:     'Co2Settings',
  [MSG_CURE_SETTINGS]:    'CureSettings',
  [MSG_FAN_BOOST_SETTINGS]: 'FanBoostSettings',
  [MSG_CLIMACELL_SETTINGS]: 'ClimacellSettings',
  [MSG_FW_UPDATE_STATUS]:   'FwUpdateStatus',
  [MSG_FW_BANK_INFO]:       'FwBankInfo',
  [MSG_ORBIT_STATUS]:       'OrbitStatus',
  [MSG_ORBIT_DISCOVERY]:    'OrbitDiscovery',
};

// Export MSG IDs so the firmware update manager can reference them
export {
  MSG_FW_BEGIN_UPDATE, MSG_FW_DATA_CHUNK, MSG_FW_FINALIZE_UPDATE,
  MSG_FW_ACTIVATE_BANK, MSG_FW_UPDATE_STATUS, MSG_FW_BANK_INFO,
  MSG_ORBIT_STATUS, MSG_ORBIT_DISCOVERY, MSG_ORBIT_ROLE_ASSIGN,
};

// Re-export PB helpers for use by the firmware update manager
export { pbDecodeFields, pbGetVarint, pbGetString, pbGetSubmsg };

/* ──────────────────────────────────────────────────────────────────────── *
 *  Pending command tracking                                                *
 * ──────────────────────────────────────────────────────────────────────── */

interface PendingCmd {
  seq: number;
  resolve: (ackStatus: number) => void;
  reject: (err: Error) => void;
  timer: NodeJS.Timeout;
}

/* ──────────────────────────────────────────────────────────────────────── *
 *  NovaSerialBridge                                                        *
 * ──────────────────────────────────────────────────────────────────────── */

export interface NovaSerialBridgeOptions {
  port: string;       // Serial device or tcp://host:port
  baudRate?: number;   // Default: 921600
}

export class NovaSerialBridge extends EventEmitter {
  private portPath: string;
  private baudRate: number;
  private isTcp: boolean;
  private protocol: NovaProtocol;
  private seqCounter = 0;
  private pendingCmds: Map<number, PendingCmd> = new Map();
  private connected = false;
  private serialPort: any = null;
  private tcpSocket: import('net').Socket | null = null;
  private reconnectTimer: NodeJS.Timeout | null = null;
  private reconnectAttempts = 0;
  private static readonly RECONNECT_BASE_MS = 1000;
  private static readonly RECONNECT_MAX_MS = 30000;
  private heartbeatTimer: NodeJS.Timeout | null = null;
  private lastFirmwareMsg = 0;
  private firmwareProtocolVersion: number | null = null;

  constructor(options: NovaSerialBridgeOptions) {
    super();
    this.portPath = options.port;
    this.baudRate = options.baudRate ?? 921600;
    this.isTcp = options.port.startsWith('tcp://');
    this.protocol = new NovaProtocol();

    // Wire up protocol events
    this.protocol.on('message', (payload: Buffer) => this.handleMessage(payload));
    this.protocol.on('error', (err: Error) => this.emit('error', err));
  }

  /**
   * Open the serial connection and start the protocol.
   */
  async open(): Promise<void> {
    if (this.isTcp) {
      await this.openTcp();
    } else {
      await this.openSerial();
    }

    // Request all settings from firmware
    this.sendDataRequest(0 /* REQ_ALL_SETTINGS */);

    // Heartbeat watchdog
    this.lastFirmwareMsg = Date.now();
    this.heartbeatTimer = setInterval(() => {
      const elapsed = (Date.now() - this.lastFirmwareMsg) / 1000;
      if (elapsed > 15) {
        console.warn('[NovaBridge] No firmware data for 15s – reconnecting');
        this.reconnect();
      }
    }, 5000);
  }

  /**
   * Close the serial connection.
   */
  close(): void {
    if (this.heartbeatTimer) clearInterval(this.heartbeatTimer);
    if (this.reconnectTimer) clearTimeout(this.reconnectTimer);
    this.pendingCmds.forEach(cmd => {
      clearTimeout(cmd.timer);
      cmd.reject(new Error('Bridge closing'));
    });
    this.pendingCmds.clear();

    if (this.tcpSocket) {
      this.tcpSocket.destroy();
      this.tcpSocket = null;
    }
    if (this.serialPort) {
      this.serialPort.close();
      this.serialPort = null;
    }
    this.connected = false;
  }

  /* ════════════════════════════════════════════════════════════════════ *
   *  Public command API (Bridge → Firmware)                              *
   * ════════════════════════════════════════════════════════════════════ */

  /**
   * Send an equipment toggle command.
   * Returns a Promise that resolves when the firmware ACKs.
   */
  async sendEquipmentCmd(eqIndex: number, newState: number): Promise<number> {
    /* Both fields use varintForce: eqIndex=0 (RO_FAN) is a valid target,
     * and newState=0 (REMOTE_AUTO) is a meaningful selection. */
    const inner = new PbEncoder()
      .varintForce(1, eqIndex)
      .varintForce(2, newState)
      .encode();
    return this.sendCommand(MSG_EQUIPMENT_CMD, inner);
  }

  /**
   * Send a system command (clear alarm, factory default, etc.)
   */
  async sendSystemCmd(cmdType: number, param = 0): Promise<number> {
    const inner = new PbEncoder()
      .varint(1, cmdType)
      .varint(2, param)
      .encode();
    return this.sendCommand(MSG_SYSTEM_CMD, inner);
  }

  /**
   * Send a per-stage refrigeration diagnostic command.  Mirrors the
   * legacy `StoreRefrigDiag()` semantics:
   *   - `stageKind=0` targets `Settings.Refrig.Stage[index]`     (index 0..7)
   *   - `stageKind=1` targets `Settings.Refrig.Defrost[index]`  (index 0..1)
   *   - `value=2` ("On") engages diagnostic; any other value is treated as Off.
   *
   * All three fields use `varintForce` because 0 is a meaningful value
   * (stage 0, kind 0, value 0=auto-clear) and proto3's default-skip rule
   * would otherwise drop them.
   */
  async sendRefrigDiagCmd(stageKind: number, index: number, value: number): Promise<number> {
    const inner = new PbEncoder()
      .varintForce(1, stageKind)
      .varintForce(2, index)
      .varintForce(3, value)
      .encode();
    return this.sendCommand(MSG_REFRIG_DIAG_CMD, inner);
  }

  /**
   * Send a settings update. The payload is a pre-encoded SettingsUpdate submessage.
   */
  async sendSettingsUpdate(settingsPayload: Buffer): Promise<number> {
    return this.sendCommand(MSG_SETTINGS_UPDATE, settingsPayload);
  }

  /**
   * Request specific data from firmware.
   */
  sendDataRequest(requestType: number, param = 0): void {
    const inner = new PbEncoder()
      .varint(1, requestType)
      .varint(2, param)
      .encode();
    // Fire-and-forget (no ACK expected for data requests)
    this.sendEnvelope(MSG_DATA_REQUEST, inner);
  }

  /**
   * Send an IO name rename command.
   */
  async sendIoNameUpdate(eqIndex: number, name: string): Promise<number> {
    const inner = new PbEncoder()
      .varint(1, eqIndex)
      .string(2, name)
      .encode();
    return this.sendCommand(MSG_IO_NAME_UPDATE, inner);
  }

  /**
   * Send a password authentication request.
   */
  async sendPasswordAuth(userId: string, password: string): Promise<number> {
    const inner = new PbEncoder()
      .string(1, userId)
      .string(2, password)
      .encode();
    return this.sendCommand(MSG_PASSWORD_AUTH, inner);
  }

  /**
   * Send a log query request to firmware.
   * logType: 0=USER_GRAPH, 1=USER_TABLE, 2=SYSTEM, 3=PID, 4=LOAD
   */
  async sendLogQuery(logType: number, startDate: string, endDate: string, mode = 0): Promise<number> {
    const inner = new PbEncoder()
      .varint(1, logType)
      .string(2, startDate)
      .string(3, endDate)
      .varint(4, mode)
      .encode();
    return this.sendCommand(MSG_LOG_QUERY, inner);
  }

  /**
   * Send FwBeginUpdate to start a firmware update session.
   * Longer timeout — firmware needs to erase the inactive bank.
   */
  async sendFwBeginUpdate(totalSize: number, crc32: number, version: string, chunkSize = 1024): Promise<number> {
    const inner = new PbEncoder()
      .varint(1, totalSize)
      .varint(2, crc32)
      .string(3, version)
      .varint(4, chunkSize)
      .encode();
    return this.sendCommand(MSG_FW_BEGIN_UPDATE, inner, 30_000);
  }

  /**
   * Send one firmware data chunk. Timeout is short — write should be fast.
   */
  async sendFwDataChunk(offset: number, data: Buffer, chunkCrc: number): Promise<number> {
    const inner = new PbEncoder()
      .varint(1, offset)
      .submsg(2, data)
      .varint(3, chunkCrc)
      .encode();
    return this.sendCommand(MSG_FW_DATA_CHUNK, inner, 5000);
  }

  /**
   * Send FwFinalizeUpdate — firmware verifies the full image CRC.
   */
  async sendFwFinalizeUpdate(crc32: number): Promise<number> {
    const inner = new PbEncoder()
      .varint(1, crc32)
      .encode();
    return this.sendCommand(MSG_FW_FINALIZE_UPDATE, inner, 30_000);
  }

  /**
   * Send FwActivateBank — swap active bank and optionally reboot.
   */
  async sendFwActivateBank(reboot = true): Promise<number> {
    const inner = new PbEncoder()
      .bool(1, reboot)
      .encode();
    return this.sendCommand(MSG_FW_ACTIVATE_BANK, inner, 10_000);
  }

  /**
   * Request orbit discovery from firmware.
   * Fire-and-forget — firmware will push OrbitDiscovery message.
   */
  requestOrbitDiscovery(): void {
    this.sendDataRequest(8 /* REQ_ORBIT_DISCOVERY */);
  }

  /**
   * Send an orbit role assignment command.
   * @param slot - Discovery slot index (0-15)
   * @param role - OrbitRole enum (0=UNASSIGNED, 1=STORAGE, 2=DOOR, 3=REFRIG)
   * @param zoneId - Zone index (0=Room A, 1=Room B, ...)
   * @param legacySlot - IoBoard[] mapping (0-2), or -1 for none
   * @param refrigStage - Compressor stage for REFRIG role (1-8)
   */
  async sendOrbitRoleAssign(
    slot: number,
    role: number,
    zoneId: number,
    legacySlot: number,
    refrigStage: number
  ): Promise<number> {
    // Encode legacy_slot as zigzag for signed int32
    const zigzag = legacySlot >= 0 ? legacySlot << 1 : ((-legacySlot) << 1) - 1;
    const inner = new PbEncoder()
      .varint(1, slot)
      .varint(2, role)
      .varint(3, zoneId)
      .varint(4, zigzag)
      .varint(5, refrigStage)
      .encode();
    return this.sendCommand(MSG_ORBIT_ROLE_ASSIGN, inner);
  }

  /**
   * Send a system-wide emergency stop (CMD_SYSTEM_STOP = 5).
   * This is a latching stop — all equipment is set to REMOTE_SYSSTOP.
   * Requires CMD_CLEAR_ALARM (sendSystemCmd(1)) to reset.
   */
  async sendSystemStop(): Promise<number> {
    return this.sendSystemCmd(5 /* CMD_SYSTEM_STOP */);
  }

  /**
   * Clear the system stop / alarm state.
   * Sends CMD_CLEAR_ALARM to restore normal operation after a system stop.
   */
  async clearSystemStop(): Promise<number> {
    return this.sendSystemCmd(1 /* CMD_CLEAR_ALARM */);
  }

  /**
   * Get connection state.
   */
  isConnected(): boolean {
    return this.connected;
  }

  getProtocolStats(): NovaProtocolStats {
    return this.protocol.getStats();
  }

  /** Get the firmware's protocol version (null if not yet received) */
  getFirmwareProtocolVersion(): number | null {
    return this.firmwareProtocolVersion;
  }

  /* ════════════════════════════════════════════════════════════════════ *
   *  Envelope build + send                                               *
   * ════════════════════════════════════════════════════════════════════ */

  private nextSeq(): number {
    return ++this.seqCounter;
  }

  private sendEnvelope(msgField: number, innerPayload: Buffer): number {
    const seq = this.nextSeq();
    const envelope = new PbEncoder()
      .varint(1, NOVA_PROTOCOL_VERSION)
      .varint(2, seq)
      .submsg(msgField, innerPayload)
      .encode();

    const frame = this.protocol.buildFrame(envelope);
    this.writeRaw(frame);
    return seq;
  }

  private sendCommand(msgField: number, innerPayload: Buffer, timeoutMs = 3000): Promise<number> {
    return new Promise((resolve, reject) => {
      const seq = this.sendEnvelope(msgField, innerPayload);

      const timer = setTimeout(() => {
        this.pendingCmds.delete(seq);
        reject(new Error(`Command timeout (seq=${seq}, msg=${MSG_NAMES[msgField] ?? msgField})`));
      }, timeoutMs);

      this.pendingCmds.set(seq, { seq, resolve, reject, timer });
    });
  }

  /* ════════════════════════════════════════════════════════════════════ *
   *  Incoming message handling                                           *
   * ════════════════════════════════════════════════════════════════════ */

  private handleMessage(payload: Buffer): void {
    this.lastFirmwareMsg = Date.now();

    const fields = pbDecodeFields(payload);
    const version = pbGetVarint(fields, 1);
    const seq = pbGetVarint(fields, 2);

    // ── Protocol version negotiation ──
    // On first message, record firmware's protocol version and warn on mismatch.
    if (this.firmwareProtocolVersion === null && version > 0) {
      this.firmwareProtocolVersion = version;
      if (version !== NOVA_PROTOCOL_VERSION) {
        console.error(
          `[NovaBridge] Protocol version mismatch: firmware=${version}, bridge=${NOVA_PROTOCOL_VERSION}. ` +
          `Communication may be unreliable — update the ${version > NOVA_PROTOCOL_VERSION ? 'bridge' : 'firmware'}.`
        );
        this.emit('version-mismatch', { firmware: version, bridge: NOVA_PROTOCOL_VERSION });
      } else {
        console.log(`[NovaBridge] Protocol version ${version} confirmed`);
      }
    }

    // Reject messages from incompatible future protocol versions
    if (version > NOVA_PROTOCOL_VERSION) {
      console.warn(`[NovaBridge] Dropping message with unsupported protocol version ${version} (bridge supports v${NOVA_PROTOCOL_VERSION})`);
      return;
    }

    // Find the oneof message field (the first non-header field)
    const msgField = fields.find(f => f.field >= 10);
    if (!msgField) return;

    const msgId = msgField.field;
    const msgData = msgField.wireType === PB_LEN ? (msgField.value as Buffer) : null;

    const msgName = MSG_NAMES[msgId] ?? `Unknown(${msgId})`;

    // Handle ACK — resolve pending commands
    if (msgId === MSG_ACK && msgData) {
      this.handleAck(msgData);
      return;
    }

    // Handle data load status — marks successful connection
    if (msgId === MSG_DATA_LOAD_STATUS && msgData) {
      const dlFields = pbDecodeFields(msgData);
      const ready = pbGetVarint(dlFields, 1);
      if (ready) {
        this.connected = true;
        this.emit('connected');
        console.log(`[NovaBridge] Connected — firmware ready`);
      }
    }

    // Emit typed event for the data store to consume
    if (msgData) {
      this.emit('message', msgId, msgData, seq);
      this.emit(msgName, msgData, seq);
    }
  }

  private handleAck(data: Buffer): void {
    const fields = pbDecodeFields(data);
    const refSeq = pbGetVarint(fields, 1);
    const status = pbGetVarint(fields, 2);

    const pending = this.pendingCmds.get(refSeq);
    if (pending) {
      clearTimeout(pending.timer);
      this.pendingCmds.delete(refSeq);
      pending.resolve(status);
    }
  }

  /* ════════════════════════════════════════════════════════════════════ *
   *  Transport layer (TCP or serial)                                     *
   * ════════════════════════════════════════════════════════════════════ */

  private writeRaw(data: Buffer): void {
    if (this.isTcp && this.tcpSocket) {
      this.tcpSocket.write(data);
    } else if (this.serialPort) {
      this.serialPort.write(data);
    }
  }

  private async openTcp(): Promise<void> {
    const url = new URL(this.portPath.replace('tcp://', 'http://'));
    const host = url.hostname;
    const port = parseInt(url.port, 10);

    const net = await import('net');
    return new Promise((resolve, reject) => {
      const socket = net.createConnection({ host, port }, () => {
        console.log(`[NovaBridge] TCP connected to ${host}:${port}`);
        this.tcpSocket = socket;
        resolve();
      });

      socket.on('data', (data: Buffer) => {
        this.protocol.feedBytes(data);
      });

      socket.on('close', () => {
        console.log('[NovaBridge] TCP disconnected');
        this.connected = false;
        this.emit('disconnected');
        this.scheduleReconnect();
      });

      socket.on('error', (err: Error) => {
        console.error(`[NovaBridge] TCP error: ${err.message}`);
        this.emit('error', err);
        reject(err);
      });
    });
  }

  private async openSerial(): Promise<void> {
    const { SerialPort } = await import('serialport');
    return new Promise((resolve, reject) => {
      this.serialPort = new SerialPort({
        path: this.portPath,
        baudRate: this.baudRate,
        dataBits: 8,
        parity: 'none',
        stopBits: 1,
      }, (err) => {
        if (err) {
          reject(err);
          return;
        }
        console.log(`[NovaBridge] Serial opened: ${this.portPath} @ ${this.baudRate}`);
        resolve();
      });

      this.serialPort.on('data', (data: Buffer) => {
        this.protocol.feedBytes(data);
      });

      this.serialPort.on('close', () => {
        this.connected = false;
        this.emit('disconnected');
        this.scheduleReconnect();
      });

      this.serialPort.on('error', (err: Error) => {
        this.emit('error', err);
      });
    });
  }

  private scheduleReconnect(): void {
    if (this.reconnectTimer) return;
    const delay = Math.min(
      NovaSerialBridge.RECONNECT_BASE_MS * Math.pow(2, this.reconnectAttempts),
      NovaSerialBridge.RECONNECT_MAX_MS
    );
    this.reconnectAttempts++;
    console.log(`[NovaBridge] Reconnecting in ${delay}ms (attempt ${this.reconnectAttempts})...`);
    this.reconnectTimer = setTimeout(async () => {
      this.reconnectTimer = null;
      try {
        console.log('[NovaBridge] Reconnecting...');
        await this.open();
        this.reconnectAttempts = 0; // Reset on success
      } catch (err) {
        console.error('[NovaBridge] Reconnect failed:', (err as Error).message);
        this.scheduleReconnect();
      }
    }, delay);
  }

  private reconnect(): void {
    if (this.tcpSocket) {
      this.tcpSocket.destroy();
      this.tcpSocket = null;
    }
    if (this.serialPort) {
      try { this.serialPort.close(); } catch {}
      this.serialPort = null;
    }
    this.connected = false;
    this.scheduleReconnect();
  }
}
