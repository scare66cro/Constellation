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
import * as fs from 'fs';
import * as path from 'path';
import * as os from 'os';
import { NovaProtocol, NOVA_PROTOCOL_VERSION, type NovaProtocolStats } from './novaProtocol.js';

/* ──────────────────────────────────────────────────────────────────────── *
 *  Phase D — LP settings vault file backing                                *
 *                                                                          *
 *  The LP-AM2434 example does not yet expose OSPI in its syscfg, so the    *
 *  firmware-side LpSettings store keeps two ping-pong banks in MSRAM.      *
 *  Banks survive soft resets but not power cycles. This bridge maintains   *
 *  a file mirror so the user perceives persistence:                        *
 *                                                                          *
 *    LP → bridge: envelope field 32 (MSG_SETTINGS_BLOB) carries the active *
 *      bank's [LpBankHeader || blob] bytes verbatim. Written here to       *
 *      LP_SETTINGS_FILE atomically (write tmp → rename).                   *
 *    bridge → LP: on the disconnected → connected edge, if the file       *
 *      exists, encode it as envelope field 91 and SendRaw it back. The    *
 *      LP RX handler calls LpSettings_Restore() to seed bank A.           *
 * ──────────────────────────────────────────────────────────────────────── */

const LP_SETTINGS_DIR  = path.join(os.homedir(), '.constellation');
const LP_SETTINGS_FILE = path.join(LP_SETTINGS_DIR, 'lp_settings.bin');
/* Panel-default snapshot — separate file mirroring the LP's panel
 * snapshot bank. LP→bridge env field 33, bridge→LP env field 92. */
const LP_PANEL_DEFAULTS_FILE = path.join(LP_SETTINGS_DIR, 'lp_panel_defaults.bin');

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

/** Wire-side shape of a single VfdPollEntry (mirrors proto/agristar/vfd.proto).
 *  Defined here to keep novaSerialBridge.ts free of cross-imports — the
 *  bridge-side semantic shape (VfdPollEntry) lives in vfdRegisterMaps.ts
 *  and is widened to this on the way to the wire. */
export interface VfdPollEntryWire {
  cacheSlot: number;
  unitId: number;
  nativeAddr: number;
  fc: number;
  pollRateMs: number;
  writable: boolean;
}

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
/* Field 32 — LP settings blob push (Phase D, RAM ping-pong replay).
 * Body = LpBankHeader (24B) || blob bytes from LpSettings_TakeBlob.
 * Bridge writes payload verbatim to ~/.constellation/lp_settings.bin
 * and replays it on firmware-ready as envelope field 91.            */
const MSG_SETTINGS_BLOB      = 32;
/* Field 33 — LP panel-default snapshot push.
 * Body = LpBankHeader (24B) || blob bytes from LpSettings_TakePanelBlob.
 * Bridge writes payload verbatim to ~/.constellation/lp_panel_defaults.bin
 * and replays it on firmware-ready as envelope field 92.           */
const MSG_PANEL_DEFAULTS_BLOB = 33;

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
const MSG_PID_LOG_RECORD     = 72;
const MSG_LOAD_LOG_RECORD    = 73;

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

// Pi5↔Nova install-orchestration (UART airgap migration, Phase 2 — see
// docs/uart-airgap-architecture.md and proto/agristar/firmware.proto §Pi5↔Nova).
// Envelope tags landed in Phase 2; full bridge↔broker wiring is Phase 4.
const MSG_FW_INSTALL_BEGIN              = 130;
const MSG_FW_INSTALL_COMPONENT_BEGIN    = 131;
const MSG_FW_INSTALL_CHUNK              = 132;
const MSG_FW_INSTALL_COMPONENT_FINALIZE = 133;
const MSG_FW_INSTALL_COMPLETE           = 134;
const MSG_FW_INSTALL_ABORT              = 135;
const MSG_FW_FLEET_PROBE                = 136;
const MSG_FW_INSTALL_PROGRESS           = 140;
const MSG_FW_INSTALL_RESULT             = 141;
const MSG_FW_FLEET_SNAPSHOT             = 142;

// Orbit module management (120-129)
const MSG_ORBIT_STATUS       = 120;
const MSG_ORBIT_DISCOVERY    = 121;
const MSG_ORBIT_ROLE_ASSIGN  = 122;
const MSG_AO_EQUIP_ASSIGN    = 123;
const MSG_TRITON_REG_WRITE   = 125;
const MSG_ORBIT_REG_WRITE    = 126;
const MSG_VFD_POLL_CONFIG    = 127;

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
  [MSG_SETTINGS_BLOB]:    'SettingsBlob',
  [MSG_PANEL_DEFAULTS_BLOB]: 'PanelDefaultsBlob',
  [MSG_LOG_RECORD]:       'LogRecord',
  [MSG_ACTIVITY_EVENT]:   'ActivityEvent',
  [MSG_PID_LOG_RECORD]:   'PidLogRecord',
  [MSG_LOAD_LOG_RECORD]:  'LoadLogRecord',
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
  [MSG_FW_INSTALL_PROGRESS]: 'FwInstallProgress',
  [MSG_FW_INSTALL_RESULT]:   'FwInstallResult',
  [MSG_FW_FLEET_SNAPSHOT]:   'FwFleetSnapshot',
  [MSG_ORBIT_STATUS]:       'OrbitStatus',
  [MSG_ORBIT_DISCOVERY]:    'OrbitDiscovery',
  [MSG_AO_EQUIP_ASSIGN]:    'AoEquipAssign',
  [MSG_TRITON_REG_WRITE]:   'TritonRegWrite',
  [MSG_ORBIT_REG_WRITE]:    'OrbitRegWrite',
  [MSG_VFD_POLL_CONFIG]:    'VfdPollConfig',
};

// Export MSG IDs so the firmware update manager can reference them
export {
  MSG_FW_BEGIN_UPDATE, MSG_FW_DATA_CHUNK, MSG_FW_FINALIZE_UPDATE,
  MSG_FW_ACTIVATE_BANK, MSG_FW_UPDATE_STATUS, MSG_FW_BANK_INFO,
  MSG_ORBIT_STATUS, MSG_ORBIT_DISCOVERY, MSG_ORBIT_ROLE_ASSIGN, MSG_AO_EQUIP_ASSIGN,
  MSG_TRITON_REG_WRITE, MSG_ORBIT_REG_WRITE, MSG_VFD_POLL_CONFIG,
};

// Re-export PB helpers for use by the firmware update manager
export { pbDecodeFields, pbGetVarint, pbGetString, pbGetSubmsg, pbGetAllSubmsg };

// Exported for the Phase-2 OTA-broker debug endpoint
// (`GET /api/_debug/broker-fleet-probe`).
export { MSG_FW_FLEET_PROBE, MSG_FW_FLEET_SNAPSHOT };

// Phase 4 (UART-airgap migration). Exported for the install-orchestration
// callers in `firmwareInstaller.ts` (and the pre-Phase-4 debug endpoints
// in `index.ts` — those will be deleted in Phase 5).
export {
  MSG_FW_INSTALL_BEGIN, MSG_FW_INSTALL_COMPONENT_BEGIN,
  MSG_FW_INSTALL_ABORT, MSG_FW_INSTALL_PROGRESS, MSG_FW_INSTALL_RESULT,
};

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
  /** Last envelope-seq seen from the firmware, keyed by oneof msgId.
   * The LP-AM2434 bringup firmware (`lp_am2434/main.c`) gives every
   * periodic broadcast its OWN ++_seq counter (refrig_seq, plenum_seq,
   * version_seq, …) instead of the single global `s_seq_counter` used
   * by the higher-rate streams in `nova_messages.c`. So the bridge sees
   * N independent monotonic streams interleaved on UART, and a single
   * global `lastEnvelopeSeq` would false-positive on every slow stream
   * frame. A real firmware reset resets ALL streams simultaneously, so
   * the per-msgId regression test still fires correctly on actual
   * reboots. UART transports never see `socket.close`, so without this
   * we'd stay stuck `connected=true` across a firmware reboot and never
   * replay the settings blob. */
  private lastEnvelopeSeqByMsg: Map<number, number> = new Map();
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

    // Heartbeat watchdog — clear any prior timer from a reconnect cycle
    // to prevent orphaned intervals accumulating.
    if (this.heartbeatTimer) clearInterval(this.heartbeatTimer);
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
   * Assign an Orbit slot's zone role and persist it to firmware OSPI.
   *
   * Both `slot` and `role` use varintForce because 0 is a meaningful
   * value for both (slot 0 is the main board, role 0 is UNASSIGNED —
   * which the operator may legitimately want to pick to clear a slot).
   *
   * Optional fields (zone_id, legacy_slot, refrig_stage) are only sent
   * when the caller provides them; firmware applies sensible defaults.
   */
  async sendOrbitRoleAssign(
    slot: number,
    role: number,
    opts: { zoneId?: number; legacySlot?: number; refrigStage?: number } = {},
  ): Promise<number> {
    const enc = new PbEncoder()
      .varintForce(1, slot)
      .varintForce(2, role);
    if (opts.zoneId !== undefined)     enc.varintForce(3, opts.zoneId);
    if (opts.legacySlot !== undefined) enc.varintForce(4, opts.legacySlot);
    if (opts.refrigStage !== undefined) enc.varintForce(5, opts.refrigStage);
    return this.sendCommand(MSG_ORBIT_ROLE_ASSIGN, enc.encode());
  }

  /**
   * Assign a per-AO equipment program (which equipment a given orbit
   * AO drives). Persists to OSPI Settings.AoEquip[slot][channel] and
   * takes effect on the next nova_fan_output_tick pass.
   *
   * All three fields use varintForce because 0 is a meaningful value
   * for each (slot 0 is the main board, channel 0 is AO1, equip 0 is
   * AO_EQUIP_UNUSED — the default the operator can re-pick).
   */
  async sendAoEquipAssign(slot: number, channel: number, equip: number): Promise<number> {
    const inner = new PbEncoder()
      .varintForce(1, slot)
      .varintForce(2, channel)
      .varintForce(3, equip)
      .encode();
    return this.sendCommand(MSG_AO_EQUIP_ASSIGN, inner);
  }

  /**
   * Forward a single Modbus holding-register write to a Triton orbit.
   *
   * Wraps a TritonRegWrite { slot, addr, value } envelope (tag 125) and
   * ships it to the LP firmware, which calls
   * `OrbitClient_WriteHoldingRegister` against its existing Modbus TCP
   * client. Used by the `/iot/triton/{slot}/{manual,setpoints,ioconfig,
   * failures,ack}` REST routes.
   *
   * All three fields use varintForce because 0 is a meaningful value
   * (slot 0 is the main board, addr 0 is HR[0], value 0 clears a bit).
   */
  async sendTritonRegWrite(slot: number, addr: number, value: number): Promise<number> {
    const inner = new PbEncoder()
      .varintForce(1, slot)
      .varintForce(2, addr)
      .varintForce(3, value & 0xFFFF)
      .encode();
    return this.sendCommand(MSG_TRITON_REG_WRITE, inner);
  }

  /**
   * Forward a role-agnostic Modbus HR write to any orbit. Phase 4b
   * Sub-phase 2 (2026-06-01). For GDC's stage assignment block (5
   * regs at HR_GDC_STAGE_BASE=325) and similar non-TRITON writes the
   * Pi5 used to make over direct Modbus TCP. Nova's handler picks
   * FC06 for `values.length === 1` and FC16 otherwise.
   *
   * All fields use varintForce because 0 is meaningful (slot 0, addr 0,
   * value 0=clear).
   */
  async sendOrbitRegWrite(slot: number, addr: number, values: number[]): Promise<number> {
    if (!Array.isArray(values) || values.length === 0) {
      throw new Error('sendOrbitRegWrite: values must be a non-empty array');
    }
    if (values.length > 123) {
      throw new Error(`sendOrbitRegWrite: too many values (${values.length} > 123)`);
    }
    const enc = new PbEncoder()
      .varintForce(1, slot)
      .varintForce(2, addr);
    // Unpacked-repeated (one tag-3 varint per value). The LP handler
    // accepts both unpacked and packed; unpacked is simpler to emit.
    for (const v of values) {
      enc.varintForce(3, v & 0xFFFF);
    }
    return this.sendCommand(MSG_ORBIT_REG_WRITE, enc.encode());
  }

  /**
   * Phase 4b Sub-3 (2026-06-02): vendor-agnostic VFD poll schedule.
   *
   * Bridge composes one VfdPollConfig per STORAGE orbit from
   * `vfdRegisterMaps.ts` profiles + operator-configured drive list
   * (unit_id ↔ manufacturer). Replays on every Nova reconnect so the
   * orbit doesn't need to OSPI-persist. Until the orbit firmware grows
   * the scheduler + RS485 master, Nova's handler is an ack-only stub
   * (logs the byte count, drops the body) — so this send round-trips
   * cleanly today and lights up the moment orbit-side support lands.
   */
  async sendVfdPollConfig(slot: number, entries: VfdPollEntryWire[]): Promise<number> {
    if (!Number.isInteger(slot) || slot < 0) {
      throw new Error('sendVfdPollConfig: slot must be a non-negative integer');
    }
    if (!Array.isArray(entries)) {
      throw new Error('sendVfdPollConfig: entries must be an array');
    }
    if (entries.length > 48) {
      throw new Error(`sendVfdPollConfig: too many entries (${entries.length} > 48 — one per HR 100..147 cache slot)`);
    }
    const outer = new PbEncoder()
      .varintForce(1, slot);
    for (const e of entries) {
      const inner = new PbEncoder()
        .varintForce(1, e.cacheSlot  & 0xFFFF)
        .varintForce(2, e.unitId     & 0xFF)
        .varintForce(3, e.nativeAddr & 0xFFFF)
        .varintForce(4, e.fc         & 0xFFFF)
        .varintForce(5, e.pollRateMs & 0xFFFF)
        .varintForce(6, e.writable ? 1 : 0)
        .encode();
      outer.submsg(2, inner);
    }
    return this.sendCommand(MSG_VFD_POLL_CONFIG, outer.encode());
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
   * DEBUG — Phase-2 OTA broker probe. Sends FwFleetProbe (envelope tag 136,
   * empty body / timeout_ms=0 → broker's default) and waits for the
   * matching FwFleetSnapshot (tag 142) reply. Returns the raw inner bytes
   * for the caller to decode; remove when Phase 4 (firmwareInstaller wiring)
   * lands. See docs/uart-airgap-architecture.md §"Phase 2".
   */
  async sendFleetProbe(timeoutMs = 5000): Promise<Buffer> {
    return new Promise<Buffer>((resolve, reject) => {
      const onMessage = (msgId: number, data: Buffer) => {
        if (msgId !== MSG_FW_FLEET_SNAPSHOT) return;
        clearTimeout(timer);
        this.off('message', onMessage);
        resolve(data);
      };
      const timer = setTimeout(() => {
        this.off('message', onMessage);
        reject(new Error(`FwFleetSnapshot timeout after ${timeoutMs} ms`));
      }, timeoutMs);
      this.on('message', onMessage);
      // Empty inner = timeout_ms field absent → broker uses its default.
      this.sendEnvelope(MSG_FW_FLEET_PROBE, Buffer.alloc(0));
    });
  }

  /**
   * DEBUG — remove when Phase 4 lands. Fire-and-forget senders for the
   * Phase-2 OTA-install envelopes, used by
   * `GET /api/_debug/broker-install-fail-test`. Replies arrive
   * asynchronously as FwInstallProgress (140) / FwInstallResult (141)
   * envelopes, observed via the bridge's generic `'message'` event.
   */
  sendFwInstallBegin(inner: Buffer): void {
    this.sendEnvelope(MSG_FW_INSTALL_BEGIN, inner);
  }
  sendFwInstallComponentBegin(inner: Buffer): void {
    this.sendEnvelope(MSG_FW_INSTALL_COMPONENT_BEGIN, inner);
  }
  sendFwInstallAbort(inner: Buffer): void {
    this.sendEnvelope(MSG_FW_INSTALL_ABORT, inner);
  }

  /**
   * Phase 4 (UART-airgap migration) — fire-and-forget senders for the
   * streaming-side install envelopes. firmwareInstaller.ts uses these
   * to ship the per-component chunk stream + Finalize + Complete. The
   * broker reports terminal state via FwInstallProgress / FwInstallResult
   * on the bridge's generic `'message'` event (no per-envelope ack
   * expected — see firmware.proto §"Memory model" for the open-loop
   * streaming rationale).
   */
  sendFwInstallChunk(inner: Buffer): void {
    this.sendEnvelope(MSG_FW_INSTALL_CHUNK, inner);
  }
  sendFwInstallComponentFinalize(inner: Buffer): void {
    this.sendEnvelope(MSG_FW_INSTALL_COMPONENT_FINALIZE, inner);
  }
  sendFwInstallComplete(): void {
    // FwInstallComplete carries no fields — empty inner is correct.
    this.sendEnvelope(MSG_FW_INSTALL_COMPLETE, Buffer.alloc(0));
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
   *
   * NOTE: superseded by the options-bag overload above.  Kept as a
   * doc-only stub so the JSDoc metadata isn't lost.  Do NOT re-add
   * a body here — duplicate methods silently override each other in
   * TypeScript and the wrong one wins (verified during the orbit-
   * topology bring-up: encoding `opts` object as varint gave NaN and
   * firmware never received MSG 122).
   */

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
   * Request a controller-SoC warm reset (CMD_REBOOT_SOC = 50).
   *
   * Firmware acks then calls Sciclient_pmDeviceReset(SystemP_WAIT_FOREVER)
   * after a brief UART-drain delay. DMSC tears down the SoC, ROM re-runs,
   * SBL loads OSPI 0x80000 — same path the JTAG auto-flasher uses.
   *
   * The Ack typically lands before the disconnect; if it doesn't, the
   * bridge's existing reconnect logic still picks the firmware back up
   * once it re-handshakes (~30 s end-to-end).
   *
   * Used by:
   *   - POST /iot/system/reboot (operator-initiated)
   *   - The OTA Activate step (orbitOtaPush.ts → activate firmware that
   *     was just written to OSPI Bank B).
   *
   * Returns the seq number of the outgoing envelope (the Ack with that
   * seq is the success signal).
   */
  async sendRebootSoc(): Promise<number> {
    return this.sendSystemCmd(50 /* CMD_REBOOT_SOC */);
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

    // Phase D — LP settings vault: cache blob to disk for cold-boot replay.
    if (msgId === MSG_SETTINGS_BLOB && msgData) {
      this.persistSettingsBlob(msgData);
      // Fall through so listeners can also observe (no-op today).
    }

    // Panel-default snapshot — separate cache file, replayed on
    // firmware-ready as envelope field 92.
    if (msgId === MSG_PANEL_DEFAULTS_BLOB && msgData) {
      this.persistPanelDefaultsBlob(msgData);
    }

    // Firmware-reset detection. UART transports never raise
    // socket.close, so without this we'd stay `connected=true` across
    // a reboot and never re-fire replaySettingsBlob — operator
    // settings would silently be replaced by firmware defaults on
    // every reset. Each msgId in the LP firmware has its own ++_seq
    // counter (see `lp_am2434/main.c`), so we track regression PER
    // msgId. A real reset zeroes every counter simultaneously, so the
    // per-stream test still fires correctly. Tolerance of 8 absorbs
    // out-of-order / dropped frames at steady state.
    const lastForMsg = this.lastEnvelopeSeqByMsg.get(msgId) ?? 0;
    if (seq > 0 && this.connected && seq + 8 < lastForMsg) {
      console.log(`[NovaBridge] Firmware reset detected (seq ${seq} << ${lastForMsg} on msg=${msgName}/${msgId}) — re-arming connect/replay`);
      this.connected = false;
      this.lastEnvelopeSeqByMsg.clear();
    }
    if (seq > lastForMsg) this.lastEnvelopeSeqByMsg.set(msgId, seq);

    // Handle data load status — marks successful connection.
    // The firmware re-broadcasts DataLoadStatus(ready=true) at the tail
    // of every UI_SendAllSettings burst (~50s), so suppress repeats —
    // only fire 'connected' / log on the disconnected → connected edge.
    if (msgId === MSG_DATA_LOAD_STATUS && msgData) {
      const dlFields = pbDecodeFields(msgData);
      const ready = pbGetVarint(dlFields, 1);
      if (ready && !this.connected) {
        this.connected = true;
        this.emit('connected');
        console.log(`[NovaBridge] Connected — firmware ready`);
        this.replaySettingsBlob();
        this.replayPanelDefaultsBlob();
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
   *  Phase D — LP settings vault file backing                            *
   *                                                                      *
   *  Wire layout reference:                                              *
   *    LP→bridge envelope field 32 (MSG_SETTINGS_BLOB) body =            *
   *      LpBankHeader(24B) || blob bytes                                 *
   *    bridge→LP envelope field 91 body = same bytes verbatim            *
   *                                                                      *
   *  We store the inner sub-msg bytes (header+blob) so the file is       *
   *  symmetric: same payload going out as came in.                       *
   * ════════════════════════════════════════════════════════════════════ */

  private persistSettingsBlob(blob: Buffer): void {
    try {
      if (!fs.existsSync(LP_SETTINGS_DIR)) {
        fs.mkdirSync(LP_SETTINGS_DIR, { recursive: true });
      }
      const tmp = LP_SETTINGS_FILE + '.tmp';
      fs.writeFileSync(tmp, blob);
      fs.renameSync(tmp, LP_SETTINGS_FILE);
      console.log(`[NovaBridge] LP settings cached: ${blob.length} B → ${LP_SETTINGS_FILE}`);
    } catch (err) {
      console.error(`[NovaBridge] Failed to persist LP settings blob:`, err);
    }
  }

  private replaySettingsBlob(): void {
    let blob: Buffer;
    try {
      if (!fs.existsSync(LP_SETTINGS_FILE)) {
        console.log(`[NovaBridge] LP settings: no cached blob to replay`);
        return;
      }
      blob = fs.readFileSync(LP_SETTINGS_FILE);
    } catch (err) {
      console.error(`[NovaBridge] Failed to read LP settings blob:`, err);
      return;
    }
    if (blob.length < 24) {
      console.warn(`[NovaBridge] LP settings blob too short (${blob.length} B), skipping replay`);
      return;
    }
    // Build envelope manually with field 91 length-delimited.
    // We do NOT use sendEnvelope() because that consumes a sequence
    // counter slot; restore is bridge-internal and never expects an Ack.
    const seq = this.nextSeq();
    const envelope = new PbEncoder()
      .varint(1, NOVA_PROTOCOL_VERSION)
      .varint(2, seq)
      .submsg(91, blob)
      .encode();
    const frame = this.protocol.buildFrame(envelope);
    this.writeRaw(frame);
    console.log(`[NovaBridge] LP settings replayed: ${blob.length} B (seq=${seq})`);
  }

  /* Panel-default snapshot persistence. Mirrors the active-bank pair
   * (persistSettingsBlob / replaySettingsBlob) but uses a separate file
   * and envelope fields 33/92 so the operator's saved baseline survives
   * power cycles independently of the live editable settings. */
  private persistPanelDefaultsBlob(blob: Buffer): void {
    try {
      if (!fs.existsSync(LP_SETTINGS_DIR)) {
        fs.mkdirSync(LP_SETTINGS_DIR, { recursive: true });
      }
      const tmp = LP_PANEL_DEFAULTS_FILE + '.tmp';
      fs.writeFileSync(tmp, blob);
      fs.renameSync(tmp, LP_PANEL_DEFAULTS_FILE);
      console.log(`[NovaBridge] LP panel default cached: ${blob.length} B → ${LP_PANEL_DEFAULTS_FILE}`);
    } catch (err) {
      console.error(`[NovaBridge] Failed to persist LP panel default blob:`, err);
    }
  }

  private replayPanelDefaultsBlob(): void {
    let blob: Buffer;
    try {
      if (!fs.existsSync(LP_PANEL_DEFAULTS_FILE)) {
        console.log(`[NovaBridge] LP panel default: no cached blob to replay`);
        return;
      }
      blob = fs.readFileSync(LP_PANEL_DEFAULTS_FILE);
    } catch (err) {
      console.error(`[NovaBridge] Failed to read LP panel default blob:`, err);
      return;
    }
    if (blob.length < 24) {
      console.warn(`[NovaBridge] LP panel default blob too short (${blob.length} B), skipping replay`);
      return;
    }
    const seq = this.nextSeq();
    const envelope = new PbEncoder()
      .varint(1, NOVA_PROTOCOL_VERSION)
      .varint(2, seq)
      .submsg(92, blob)
      .encode();
    const frame = this.protocol.buildFrame(envelope);
    this.writeRaw(frame);
    console.log(`[NovaBridge] LP panel default replayed: ${blob.length} B (seq=${seq})`);
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

    // Tear down any prior socket before opening a new one. Otherwise the
    // old (zombie) connection can survive a transient error and keep the
    // peer's listening socket attached to it, while the new socket we're
    // about to create gets accepted but ignored. Bridge writes then
    // disappear into the new socket while the peer still reads from the
    // old one. Symptom: txFrames climbs but firmware RX bytes counter is
    // frozen and every sendCommand times out. (TCP transport is rarely
    // used now that production is UART per invariant #12.)
    if (this.tcpSocket) {
      try { this.tcpSocket.removeAllListeners(); this.tcpSocket.destroy(); } catch {}
      this.tcpSocket = null;
    }

    const net = await import('net');
    return new Promise((resolve, reject) => {
      let settled = false;
      const socket = net.createConnection({ host, port }, () => {
        console.log(`[NovaBridge] TCP connected to ${host}:${port}`);
        this.tcpSocket = socket;
        settled = true;
        resolve();
      });

      socket.on('data', (data: Buffer) => {
        this.protocol.feedBytes(data);
      });

      socket.on('close', () => {
        console.log('[NovaBridge] TCP disconnected');
        this.connected = false;
        // Drop our reference if this was the active socket — guards
        // against a stale reference being reused by writeRaw.
        if (this.tcpSocket === socket) this.tcpSocket = null;
        this.emit('disconnected');
        this.scheduleReconnect();
      });

      socket.on('error', (err: Error) => {
        console.error(`[NovaBridge] TCP error: ${err.message}`);
        this.emit('error', err);
        // Destroy the failed socket so it can't linger half-open and
        // race the next connect attempt (see comment above).
        try { socket.destroy(); } catch {}
        if (!settled) { settled = true; reject(err); }
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
        rtscts: false,
        xon: false,
        xoff: false,
        xany: false,
      }, (err) => {
        if (err) {
          reject(err);
          return;
        }
        console.log(`[NovaBridge] Serial opened: ${this.portPath} @ ${this.baudRate}`);
        resolve();
      });

      // Log the first chunk of raw bytes received to diagnose framing issues.
      // This fires once per open, then removes itself.
      const diagHandler = (data: Buffer) => {
        console.log(`[NovaBridge] First ${data.length} raw bytes: ${data.toString('hex')}`);
        this.serialPort?.removeListener('data', diagHandler);
        this.serialPort?.on('data', normalHandler);
        // Also feed these bytes through the protocol
        this.protocol.feedBytes(data);
      };
      const normalHandler = (data: Buffer) => {
        this.protocol.feedBytes(data);
      };

      this.serialPort.on('data', diagHandler);

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
