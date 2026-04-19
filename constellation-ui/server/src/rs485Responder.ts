/**
 * RS485 Analog Board Responder
 * 
 * Connects to QEMU's UART2 (RS485 bus) and responds to the ARM firmware's
 * analog board polling queries with simulated sensor data.
 * 
 * Protocol: Binary RS485 with byte-stuffing (0x7E/0x7D transparency)
 * Baud: 9600 8N1 (virtual - TCP socket)
 * 
 * Simulates 2 boards:
 *   Board 0 (addr 0): Temperature board - Plenum1, Plenum2, Outside, Return temps
 *   Board 1 (addr 1): Humidity board    - OutHumid, PlenumHumid, ReturnHumid, CO2
 * 
 * Commands handled:
 *   1 = QRY_FIRMWARE  → responds with version bytes
 *   2 = QRY_SENSORS   → responds with sensor type byte + 4x 16-bit ADC values
 */

import * as net from 'net';
import { saveConfig, loadConfig } from './simConfig.js';

// ─── RS485 Protocol Constants ───────────────────────────────────────────────
const RS485_START_BYTE          = 0x7E;
const RS485_TRANSPARENCY_BYTE   = 0x7D;
const RS485_TRANSPARENT_XOR     = 0x20;

const RS485_MSG_ADDRESS_BYTE    = 1;
const RS485_MSG_LEN_BYTE        = 2;
const RS485_MSG_CTRL_BYTE       = 3;
const RS485_MSG_CMD_BYTE        = 4;
const RS485_MSG_BEGIN_DATA      = 5;
const RS485_MSG_OVERHEAD        = 7;
const RS485_MSG_ADDRESS_MASK    = 0x3F;

const RS485_QRY_FIRMWARE        = 1;
const RS485_QRY_SENSORS         = 2;
const RS485_QRY_VOLTAGE         = 3;

// Sensor type codes (2 bits each, packed into byte 0 of sensor response data)
const SENSOR_TYPE_TEMP_IR       = 0; // IR Temperature
const SENSOR_TYPE_HUMID         = 1; // Humidity
const SENSOR_TYPE_CO2           = 2; // CO2
const SENSOR_TYPE_TEMP          = 3; // NTC Temperature

// ─── Simulated Sensor State ────────────────────────────────────────────────

interface SensorState {
  // Board 0: Temperature board (NTC thermistors)
  plenumTemp1: number;   // °C
  plenumTemp2: number;   // °C
  outsideTemp: number;   // °C
  returnTemp: number;    // °C
  // Board 1: Humidity board
  outsideHumid: number;  // 0-100 %
  plenumHumid: number;   // 0-100 %
  returnHumid: number;   // 0-100 %
  co2: number;           // 0-9900 ppm (firmware rejects ScaledADC>895 ≈ 9930ppm)
}

// Default sensor readings
const defaultState: SensorState = {
  plenumTemp1: 9.0,
  plenumTemp2: 8.6,
  outsideTemp: 9.0,
  returnTemp: 9.0,
  outsideHumid: 19,
  plenumHumid: 69,
  returnHumid: 100,
  co2: 800,
};

// ─── ADC Conversion (reverse of firmware's ConvertToTemp/ConvertToHumid/etc.) ─

// Temperature lookup table: DegreesC → Rt value
// Copied from Analog_Input.c TempTable_Init()
const TEMP_TABLE: Array<{ degC: number; rt: number }> = [
  { degC: -40, rt: 75777 }, { degC: -39, rt: 70918 }, { degC: -38, rt: 66401 },
  { degC: -37, rt: 62200 }, { degC: -36, rt: 58292 }, { degC: -35, rt: 54653 },
  { degC: -34, rt: 51264 }, { degC: -33, rt: 48106 }, { degC: -32, rt: 45162 },
  { degC: -31, rt: 42416 }, { degC: -30, rt: 39855 }, { degC: -29, rt: 37463 },
  { degC: -28, rt: 35230 }, { degC: -27, rt: 33144 }, { degC: -26, rt: 31194 },
  { degC: -25, rt: 29371 }, { degC: -24, rt: 27665 }, { degC: -23, rt: 26069 },
  { degC: -22, rt: 24574 }, { degC: -21, rt: 23174 }, { degC: -20, rt: 21862 },
  { degC: -19, rt: 20633 }, { degC: -18, rt: 19480 }, { degC: -17, rt: 18398 },
  { degC: -16, rt: 17383 }, { degC: -15, rt: 16430 }, { degC: -14, rt: 15535 },
  { degC: -13, rt: 14694 }, { degC: -12, rt: 13903 }, { degC: -11, rt: 13160 },
  { degC: -10, rt: 12461 }, { degC: -9, rt: 11803 }, { degC: -8, rt: 11183 },
  { degC: -7, rt: 10600 }, { degC: -6, rt: 10051 }, { degC: -5, rt: 9533 },
  { degC: -4, rt: 9045 }, { degC: -3, rt: 8585 }, { degC: -2, rt: 8151 },
  { degC: -1, rt: 7741 }, { degC: 0, rt: 7353 }, { degC: 1, rt: 6988 },
  { degC: 2, rt: 6643 }, { degC: 3, rt: 6318 }, { degC: 4, rt: 6010 },
  { degC: 5, rt: 5719 }, { degC: 6, rt: 5444 }, { degC: 7, rt: 5183 },
  { degC: 8, rt: 4937 }, { degC: 9, rt: 4703 }, { degC: 10, rt: 4482 },
  { degC: 11, rt: 4273 }, { degC: 12, rt: 4075 }, { degC: 13, rt: 3886 },
  { degC: 14, rt: 3708 }, { degC: 15, rt: 3539 }, { degC: 16, rt: 3378 },
  { degC: 17, rt: 3226 }, { degC: 18, rt: 3081 }, { degC: 19, rt: 2944 },
  { degC: 20, rt: 2814 }, { degC: 21, rt: 2690 }, { degC: 22, rt: 2572 },
  { degC: 23, rt: 2460 }, { degC: 24, rt: 2353 }, { degC: 25, rt: 2252 },
  { degC: 26, rt: 2156 }, { degC: 27, rt: 2064 }, { degC: 28, rt: 1977 },
  { degC: 29, rt: 1894 }, { degC: 30, rt: 1814 }, { degC: 31, rt: 1739 },
  { degC: 32, rt: 1667 }, { degC: 33, rt: 1598 }, { degC: 34, rt: 1533 },
  { degC: 35, rt: 1471 }, { degC: 36, rt: 1411 }, { degC: 37, rt: 1355 },
  { degC: 38, rt: 1300 }, { degC: 39, rt: 1249 }, { degC: 40, rt: 1199 },
  { degC: 41, rt: 1152 }, { degC: 42, rt: 1107 }, { degC: 43, rt: 1064 },
  { degC: 44, rt: 1023 }, { degC: 45, rt: 983.6 }, { degC: 46, rt: 946 },
  { degC: 47, rt: 910 }, { degC: 48, rt: 875.6 }, { degC: 49, rt: 842.6 },
  { degC: 50, rt: 811.1 }, { degC: 51, rt: 780.9 }, { degC: 52, rt: 752 },
  { degC: 53, rt: 724.3 }, { degC: 54, rt: 697.8 }, { degC: 55, rt: 672.4 },
  { degC: 56, rt: 648 }, { degC: 57, rt: 624.7 }, { degC: 58, rt: 602.3 },
  { degC: 59, rt: 580.8 }, { degC: 60, rt: 560.2 }, { degC: 61, rt: 540.2 },
  { degC: 62, rt: 521.5 }, { degC: 63, rt: 503.3 }, { degC: 64, rt: 485.8 },
  { degC: 65, rt: 469 }, { degC: 66, rt: 452.9 }, { degC: 67, rt: 437.4 },
  { degC: 68, rt: 422.6 }, { degC: 69, rt: 408.3 }, { degC: 70, rt: 394.6 },
  { degC: 71, rt: 381.3 }, { degC: 72, rt: 368.6 }, { degC: 73, rt: 356.4 },
  { degC: 74, rt: 344.7 }, { degC: 75, rt: 333.4 }, { degC: 76, rt: 322.5 },
  { degC: 77, rt: 312.1 }, { degC: 78, rt: 302 }, { degC: 79, rt: 292.3 },
  { degC: 80, rt: 282.9 }, { degC: 81, rt: 273.9 }, { degC: 82, rt: 265.3 },
  { degC: 83, rt: 256.9 },
];

/**
 * Convert a temperature in °C to the 16-bit ADC value (×16) the analog board would send.
 * 
 * Firmware formula (Analog_Input.c ConvertToTemp):
 *   Rt = (1024 * (1 + Rg1/Rg2) * Rth2 * (R1ref+R2ref)) / ((ADC/16) * R2ref) - (Rth1+Rth2)
 * 
 * Inverting: ADC = 16 * (1024 * (1+Rg1/Rg2) * Rth2 * (R1ref+R2ref)) / ((Rt + Rth1+Rth2) * R2ref)
 */
function tempToADC(tempC: number): number {
  // Clamp to table range
  if (tempC < -40) tempC = -40;
  if (tempC > 83) tempC = 83;

  // Interpolate Rt from table
  let rt: number;
  if (tempC <= TEMP_TABLE[0].degC) {
    rt = TEMP_TABLE[0].rt;
  } else if (tempC >= TEMP_TABLE[TEMP_TABLE.length - 1].degC) {
    rt = TEMP_TABLE[TEMP_TABLE.length - 1].rt;
  } else {
    // Find bracketing entries
    let i = 0;
    while (i < TEMP_TABLE.length - 1 && TEMP_TABLE[i + 1].degC < tempC) i++;
    const t0 = TEMP_TABLE[i];
    const t1 = TEMP_TABLE[i + 1];
    const frac = (tempC - t0.degC) / (t1.degC - t0.degC);
    rt = t0.rt + frac * (t1.rt - t0.rt);
  }

  // Now convert Rt back to ADC value
  const Rg1 = 27400, Rg2 = 10000, Rth1 = 10000, Rth2 = 1000;
  const R1ref = 30100, R2ref = 15000;
  const numerator = 1024.0 * (1.0 + Rg1/Rg2) * Rth2 * (R1ref + R2ref);
  const adcDiv16 = numerator / ((rt + Rth1 + Rth2) * R2ref);
  const adc = Math.round(adcDiv16 * 16);
  return Math.max(0, Math.min(16368, adc)); // 10-bit ADC * 16 max
}

/**
 * Convert humidity (0-100%) to 16-bit ADC value (×16).
 * Firmware: humid = ((ScaledADC - 180) / 720) * 100
 * Inverse: ScaledADC = (humid / 100) * 720 + 180
 */
function humidToADC(humid: number): number {
  if (humid < 0) humid = 0;
  if (humid > 100) humid = 100;
  const scaledADC = (humid / 100.0) * 720.0 + 180.0;
  return Math.round(scaledADC * 16);
}

/**
 * Convert CO2 (0-9900 ppm) to 16-bit ADC value (×16).
 * Firmware: co2 = ((ScaledADC - 180) / 720) * 10000
 * Inverse: ScaledADC = (co2 / 10000) * 720 + 180
 * Note: firmware rejects ScaledADC > 895, which maps to ~9930 ppm.
 *       We clamp at 9900 to stay safely within the sensor's valid range.
 */
function co2ToADC(co2: number): number {
  if (co2 < 0) co2 = 0;
  if (co2 > 9900) co2 = 9900;
  const scaledADC = (co2 / 10000.0) * 720.0 + 180.0;
  return Math.round(scaledADC * 16);
}

// ─── RS485 Packet Building ─────────────────────────────────────────────────

function rs485Checksum(packet: Buffer, len: number): number {
  let sum = 0;
  for (let i = 0; i < len; i++) {
    sum = (sum + packet[i]) & 0xFF;
  }
  return sum;
}

function rs485XOR(packet: Buffer, len: number): number {
  let xor = 0xFF;
  for (let i = 0; i < len; i++) {
    xor = (xor ^ packet[i]) & 0xFF;
  }
  return xor;
}

/**
 * Apply transparency encoding to a raw packet.
 * After the START byte (index 0), any 0x7E or 0x7D must be escaped.
 */
function applyTransparency(raw: Buffer): Buffer {
  const out: number[] = [raw[0]]; // START byte verbatim
  for (let i = 1; i < raw.length; i++) {
    if (raw[i] === RS485_START_BYTE) {
      out.push(RS485_TRANSPARENCY_BYTE, 0x5E);
    } else if (raw[i] === RS485_TRANSPARENCY_BYTE) {
      out.push(RS485_TRANSPARENCY_BYTE, 0x5D);
    } else {
      out.push(raw[i]);
    }
  }
  return Buffer.from(out);
}

/**
 * Remove transparency encoding from a received packet.
 */
function removeTransparency(data: Buffer): Buffer {
  const out: number[] = [data[0]]; // START byte
  let i = 1;
  while (i < data.length) {
    if (data[i] === RS485_TRANSPARENCY_BYTE && i + 1 < data.length) {
      out.push(data[i + 1] ^ RS485_TRANSPARENT_XOR);
      i += 2;
    } else {
      out.push(data[i]);
      i++;
    }
  }
  return Buffer.from(out);
}

/**
 * Build an RS485 response packet.
 */
function buildResponse(address: number, command: number, delay: number, data: Buffer): Buffer {
  const pktLen = RS485_MSG_OVERHEAD + data.length;
  const raw = Buffer.alloc(pktLen);
  let idx = 0;
  raw[idx++] = RS485_START_BYTE;
  raw[idx++] = address;
  raw[idx++] = pktLen;       // LENGTH = total packet length including overhead
  raw[idx++] = delay;
  raw[idx++] = command;
  data.copy(raw, idx);
  idx += data.length;
  raw[idx] = rs485Checksum(raw, pktLen - 2);
  idx++;
  raw[idx] = rs485XOR(raw, pktLen - 1);

  return applyTransparency(raw);
}

// ─── Board Definitions ─────────────────────────────────────────────────────

// Board type constants (exported for panel use)
export const BOARD_TYPE_TEMP_IR = 0;
export const BOARD_TYPE_HUMID   = 1;
export const BOARD_TYPE_CO2     = 2;
export const BOARD_TYPE_TEMP    = 3;

// UI sensor types (broader than the 2-bit RS485 types)
export const UI_SENSOR_TEMP_IR       = 0;
export const UI_SENSOR_HUMID         = 1;
export const UI_SENSOR_CO2_1         = 2;
export const UI_SENSOR_TEMP          = 3;
export const UI_SENSOR_RETURN_TEMP_1 = 4;
export const UI_SENSOR_RETURN_TEMP_2 = 5;
export const UI_SENSOR_RETURN_HUMID_1= 6;
export const UI_SENSOR_RETURN_HUMID_2= 7;
export const UI_SENSOR_CO2_2         = 8;
export const UI_SENSOR_PILE_TEMP     = 9;
export const UI_SENSOR_PILE_HUMID    = 10;
export const UI_SENSOR_STATIC_PRESS  = 11;
export const UI_SENSOR_UNDEFINED     = 255;

/** Convert a UI sensor type to its 2-bit RS485 type (ADC conversion method) */
export function uiTypeToRs485Type(uiType: number): number {
  switch (uiType) {
    case 0:  return SENSOR_TYPE_TEMP_IR;
    case 1: case 6: case 7: case 10: return SENSOR_TYPE_HUMID;
    case 2: case 8:  return SENSOR_TYPE_CO2;
    case 3: case 4: case 5: case 9: case 11: return SENSOR_TYPE_TEMP;
    default: return SENSOR_TYPE_TEMP;
  }
}

/** Per-sensor configuration (exported for panel/simulator use) */
export interface BoardSensorConfig {
  uiType: number;        // UI SensorType enum (0-11, 255)
  label: string;         // Display label
  offset: number;        // Calibration offset
  disabled: boolean;
  value: number;         // Current reading (°C for temp, % for humid, ppm for CO2)
}

/** Full board configuration (exported for panel/simulator use) */
export interface BoardConfig {
  address: number;
  boardType: number;       // 0=TempIR, 1=Humid, 2=CO2, 3=Temp
  label: string;           // Display label
  firmwareVersion: [number, number];
  disabled: boolean;
  sensors: [BoardSensorConfig, BoardSensorConfig, BoardSensorConfig, BoardSensorConfig];
}

/** Convert a sensor value to ADC using the appropriate conversion */
function sensorValueToADC(value: number, uiType: number): number {
  const rs485Type = uiTypeToRs485Type(uiType);
  switch (rs485Type) {
    case SENSOR_TYPE_TEMP_IR:
    case SENSOR_TYPE_TEMP:  return tempToADC(value);
    case SENSOR_TYPE_HUMID: return humidToADC(value);
    case SENSOR_TYPE_CO2:   return co2ToADC(value);
    default: return tempToADC(value);
  }
}

interface AnalogBoard {
  address: number;
  firmwareVersion: [number, number]; // [major, minor]
  sensorTypes: [number, number, number, number]; // 4 sensor type codes (2 bits each)
  getSensorADC: (state: SensorState) => [number, number, number, number]; // ADC values × 16
}

/** Create default board configurations:
 *  Board 0 (addr 0) = Temperature board: Plenum1, Plenum2, Outside, Return
 *  Board 1 (addr 1) = Humidity board:    OutHumid, PlenumHumid, ReturnHumid, CO2
 *  Board 2 (addr 2) = Pile Temp board:   Bd 3 S1-S4
 */
function createDefaultBoardConfigs(): BoardConfig[] {
  return [
    {
      address: 0, boardType: BOARD_TYPE_TEMP, label: 'Default Temperature',
      firmwareVersion: [2, 3], disabled: false,
      sensors: [
        { uiType: UI_SENSOR_TEMP, label: 'Plenum 1 Temp',  offset: 0, disabled: false, value: 48.1 },
        { uiType: UI_SENSOR_TEMP, label: 'Plenum 2 Temp',  offset: 0, disabled: false, value: 47.5 },
        { uiType: UI_SENSOR_TEMP, label: 'Outside Temp',   offset: 0, disabled: false, value: 48.2 },
        { uiType: UI_SENSOR_RETURN_TEMP_1, label: 'Return Temp', offset: 0, disabled: false, value: 48.2 },
      ],
    },
    {
      address: 1, boardType: BOARD_TYPE_HUMID, label: 'Default Humidity',
      firmwareVersion: [2, 3], disabled: false,
      sensors: [
        { uiType: UI_SENSOR_HUMID, label: 'Outside Humidity', offset: 0, disabled: false, value: 19 },
        { uiType: UI_SENSOR_HUMID, label: 'Plenum Humidity',  offset: 0, disabled: false, value: 69 },
        { uiType: UI_SENSOR_RETURN_HUMID_1, label: 'Return Humidity', offset: 0, disabled: false, value: 100 },
        { uiType: UI_SENSOR_CO2_1, label: 'CO2',           offset: 0, disabled: false, value: 4110 },
      ],
    },
    {
      address: 2, boardType: BOARD_TYPE_TEMP, label: 'Pile Temp',
      firmwareVersion: [2, 3], disabled: false,
      sensors: [
        { uiType: UI_SENSOR_PILE_TEMP, label: 'Bd 3 - S 1', offset: 0, disabled: false, value: 7.2 },
        { uiType: UI_SENSOR_PILE_TEMP, label: 'Bd 3 - S 2', offset: 0, disabled: false, value: 7.4 },
        { uiType: UI_SENSOR_PILE_TEMP, label: 'Bd 3 - S 3', offset: 0, disabled: false, value: 7.1 },
        { uiType: UI_SENSOR_PILE_TEMP, label: 'Bd 3 - S 4', offset: 0, disabled: false, value: 7.3 },
      ],
    },
  ];
}

// ─── RS485 Responder ───────────────────────────────────────────────────────

export class RS485Responder {
  private socket: net.Socket | null = null;
  private server: net.Server | null = null;
  private rxBuffer: Buffer = Buffer.alloc(0);
  private state: SensorState;
  private connected = false;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private host: string;
  private port: number;
  private verbose: boolean;
  private queryCount = 0;
  private responseCount = 0;
  private mode: 'client' | 'server' = 'client';

  /** Full board configurations — single source of truth for all boards */
  private boardConfigs: BoardConfig[];

  constructor(host: string, port: number, initialState?: Partial<SensorState>, verbose = false) {
    this.host = host;
    this.port = port;
    this.state = { ...defaultState, ...initialState };
    this.verbose = verbose;

    // Load persisted board configs, or fall back to defaults
    const saved = loadConfig<BoardConfig[]>('boards');
    if (saved && Array.isArray(saved) && saved.length > 0) {
      this.boardConfigs = saved;
      console.log(`[RS485] Loaded ${saved.length} board configs from disk`);
    } else {
      this.boardConfigs = createDefaultBoardConfigs();
      console.log(`[RS485] Using default board configs (${this.boardConfigs.length} boards)`);
    }
  }

  /** Save current board configurations to disk */
  private persistBoards(): void {
    saveConfig('boards', this.boardConfigs);
  }

  // ─── Board Configuration API ───────────────────────────────────────

  /** Get all board configurations */
  getBoardConfigs(): BoardConfig[] {
    return this.boardConfigs.map(b => ({
      ...b,
      sensors: b.sensors.map(s => ({ ...s })) as BoardConfig['sensors'],
    }));
  }

  /** Set all board configurations (replaces existing) */
  setBoardConfigs(configs: BoardConfig[]): void {
    this.boardConfigs = configs;
    this.persistBoards();
    console.log(`[RS485] Board configs updated: ${configs.length} boards`);
  }

  /** Add a new board. Returns true on success. */
  addBoard(config: BoardConfig): boolean {
    if (this.boardConfigs.length >= 32) return false;
    if (this.boardConfigs.some(b => b.address === config.address)) return false;
    this.boardConfigs.push(config);
    this.boardConfigs.sort((a, b) => a.address - b.address);
    this.persistBoards();
    console.log(`[RS485] Board added at addr ${config.address} (${config.label}), ${this.boardConfigs.length} total`);
    return true;
  }

  /** Remove a board by address. Returns true on success. */
  removeBoard(address: number): boolean {
    const idx = this.boardConfigs.findIndex(b => b.address === address);
    if (idx < 0) return false;
    this.boardConfigs.splice(idx, 1);
    this.persistBoards();
    console.log(`[RS485] Board removed at addr ${address}, ${this.boardConfigs.length} remaining`);
    return true;
  }

  /** Update a board's config. Returns true on success. */
  updateBoard(address: number, updates: Partial<BoardConfig>): boolean {
    const board = this.boardConfigs.find(b => b.address === address);
    if (!board) return false;
    if (updates.boardType !== undefined) board.boardType = updates.boardType;
    if (updates.label !== undefined) board.label = updates.label;
    if (updates.disabled !== undefined) board.disabled = updates.disabled;
    if (updates.firmwareVersion !== undefined) board.firmwareVersion = updates.firmwareVersion;
    if (updates.sensors) {
      for (let i = 0; i < 4 && i < updates.sensors.length; i++) {
        Object.assign(board.sensors[i], updates.sensors[i]);
      }
    }
    this.persistBoards();
    return true;
  }

  /** Update a specific sensor's value on a board */
  setBoardSensorValue(address: number, sensorIndex: number, value: number): boolean {
    const board = this.boardConfigs.find(b => b.address === address);
    if (!board || sensorIndex < 0 || sensorIndex > 3) return false;
    board.sensors[sensorIndex].value = value;
    return true;
  }

  /** Update sensor readings (can be called from control panel) */
  updateState(partial: Partial<SensorState>): void {
    Object.assign(this.state, partial);
    if (this.verbose) {
      console.log('[RS485] State updated:', this.state);
    }
  }

  getState(): SensorState {
    return { ...this.state };
  }

  getStats(): { queries: number; responses: number; connected: boolean } {
    return { queries: this.queryCount, responses: this.responseCount, connected: this.connected };
  }

  connect(): void {
    this.mode = 'client';
    if (this.socket) {
      this.socket.destroy();
      this.socket = null;
    }

    console.log(`[RS485] Connecting to QEMU UART2 at ${this.host}:${this.port}...`);
    
    this.socket = new net.Socket();
    this.setupSocket(this.socket);
    this.socket.connect(this.port, this.host);
  }

  /**
   * Listen as a TCP server. QEMU connects to us as a client.
   * This ensures the responder is ready BEFORE QEMU boots the firmware,
   * so the RS485 discovery scan finds our simulated boards.
   */
  listen(): Promise<void> {
    this.mode = 'server';
    return new Promise((resolve, reject) => {
      this.server = net.createServer((clientSocket) => {
        console.log(`[RS485] QEMU UART2 connected from ${clientSocket.remoteAddress}:${clientSocket.remotePort}`);
        // Only accept one connection at a time
        if (this.socket) {
          this.socket.destroy();
        }
        this.socket = clientSocket;
        this.connected = true;
        this.rxBuffer = Buffer.alloc(0);
        this.setupSocket(clientSocket);
      });

      this.server.on('error', (err) => {
        console.error(`[RS485] Server error: ${err.message}`);
        reject(err);
      });

      this.server.listen(this.port, this.host, () => {
        console.log(`[RS485] Listening on ${this.host}:${this.port} (waiting for QEMU UART2 to connect)`);
        resolve();
      });
    });
  }

  private setupSocket(sock: net.Socket): void {
    sock.on('connect', () => {
      this.connected = true;
      console.log(`[RS485] Connected to QEMU UART2 at ${this.host}:${this.port}`);
    });

    sock.on('data', (data: Buffer) => {
      this.rxBuffer = Buffer.concat([this.rxBuffer, data]);
      this.processBuffer();
    });

    sock.on('error', (err: Error) => {
      if (this.verbose) {
        console.log(`[RS485] Socket error: ${err.message}`);
      }
    });

    sock.on('close', () => {
      this.connected = false;
      if (this.mode === 'client') {
        console.log('[RS485] Connection closed, reconnecting in 2s...');
        this.scheduleReconnect();
      } else {
        console.log('[RS485] QEMU disconnected, waiting for reconnect...');
      }
    });
  }

  disconnect(): void {
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
    if (this.socket) {
      this.socket.destroy();
      this.socket = null;
    }
    if (this.server) {
      this.server.close();
      this.server = null;
    }
    this.connected = false;
  }

  private scheduleReconnect(): void {
    if (this.reconnectTimer) return;
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      this.connect();
    }, 2000);
  }

  private processBuffer(): void {
    // Find START byte
    while (this.rxBuffer.length > 0 && this.rxBuffer[0] !== RS485_START_BYTE) {
      this.rxBuffer = this.rxBuffer.subarray(1);
    }

    if (this.rxBuffer.length < 4) return; // Need at least START + ADDR + LEN + more

    // Get the length byte (handle transparency on the length byte)
    let lenIndex = 2;
    let msgLen: number;
    if (this.rxBuffer.length > lenIndex && this.rxBuffer[lenIndex] === RS485_TRANSPARENCY_BYTE) {
      if (this.rxBuffer.length <= lenIndex + 1) return; // Need more data
      msgLen = this.rxBuffer[lenIndex + 1] ^ RS485_TRANSPARENT_XOR;
    } else {
      msgLen = this.rxBuffer[lenIndex];
    }

    if (msgLen < RS485_MSG_OVERHEAD) {
      // Invalid, skip this start byte
      this.rxBuffer = this.rxBuffer.subarray(1);
      return this.processBuffer();
    }

    // We need at least msgLen bytes (but may be longer due to transparency)
    // Count how many raw bytes we need by scanning
    let rawBytesNeeded = 0;
    let logicalBytes = 0;
    let scanIdx = 0;
    while (scanIdx < this.rxBuffer.length && logicalBytes < msgLen) {
      if (scanIdx > 0 && this.rxBuffer[scanIdx] === RS485_TRANSPARENCY_BYTE) {
        scanIdx += 2; // transparency pair
      } else {
        scanIdx++;
      }
      logicalBytes++;
    }

    if (logicalBytes < msgLen) return; // Not enough data yet

    rawBytesNeeded = scanIdx;

    // Extract and de-transparency the packet
    const rawPacket = this.rxBuffer.subarray(0, rawBytesNeeded);
    const packet = removeTransparency(rawPacket);
    
    // Consume from buffer
    this.rxBuffer = this.rxBuffer.subarray(rawBytesNeeded);

    // Validate the packet
    if (packet.length < RS485_MSG_OVERHEAD) return;

    const pktLen = packet[RS485_MSG_LEN_BYTE];
    if (packet.length < pktLen) return;

    const expectedChecksum = rs485Checksum(packet, pktLen - 2);
    const expectedXOR = rs485XOR(packet, pktLen - 1);

    if (packet[pktLen - 2] !== expectedChecksum || packet[pktLen - 1] !== expectedXOR) {
      if (this.verbose) {
        console.log(`[RS485] Bad checksum/XOR on received packet: CS=${packet[pktLen-2]} exp=${expectedChecksum}, XOR=${packet[pktLen-1]} exp=${expectedXOR}`);
      }
      return this.processBuffer();
    }

    // Valid packet - process it
    const address = packet[RS485_MSG_ADDRESS_BYTE] & RS485_MSG_ADDRESS_MASK;
    const command = packet[RS485_MSG_CMD_BYTE];
    this.queryCount++;

    this.handleQuery(address, command);

    // Check for more messages
    if (this.rxBuffer.length > 0) {
      this.processBuffer();
    }
  }

  private handleQuery(address: number, command: number): void {
    // Find the board config
    const boardCfg = this.boardConfigs.find(b => b.address === address);
    if (!boardCfg) {
      // No response — firmware will time out (normal for absent boards)
      return;
    }

    let responseData: Buffer;

    switch (command) {
      case RS485_QRY_FIRMWARE: {
        // Response: 2 bytes [major, minor]
        responseData = Buffer.from(boardCfg.firmwareVersion);
        if (this.verbose) {
          console.log(`[RS485] Board ${address}: QRY_FIRMWARE → v${boardCfg.firmwareVersion[0]}.${boardCfg.firmwareVersion[1]}`);
        }
        break;
      }

      case RS485_QRY_SENSORS: {
        // Response: 1 byte sensor types + 4× 2-byte ADC values = 9 bytes
        // For boards 0-1, values come from physics-driven SensorState
        // For boards 2+, values come from the board config directly
        let adcValues: [number, number, number, number];

        if (address === 0) {
          // Board 0: Temperature board driven by physics engine
          adcValues = [
            tempToADC(this.state.plenumTemp1),
            tempToADC(this.state.plenumTemp2),
            tempToADC(this.state.outsideTemp),
            tempToADC(this.state.returnTemp),
          ];
        } else if (address === 1) {
          // Board 1: Humidity board driven by physics engine
          adcValues = [
            humidToADC(this.state.outsideHumid),
            humidToADC(this.state.plenumHumid),
            humidToADC(this.state.returnHumid),
            co2ToADC(this.state.co2),
          ];
        } else {
          // Board 2+: values from board config
          adcValues = boardCfg.sensors.map(s =>
            sensorValueToADC(s.value, s.uiType)
          ) as [number, number, number, number];
        }

        // Pack sensor types into 1 byte: 2 bits per sensor, LSB first
        let typesByte = 0;
        for (let i = 0; i < 4; i++) {
          const rs485Type = uiTypeToRs485Type(boardCfg.sensors[i].uiType);
          typesByte |= (rs485Type & 0x3) << (i * 2);
        }

        const data = Buffer.alloc(9);
        data[0] = typesByte;
        for (let i = 0; i < 4; i++) {
          data[1 + i * 2] = (adcValues[i] >> 8) & 0xFF; // High byte
          data[2 + i * 2] = adcValues[i] & 0xFF;         // Low byte
        }
        responseData = data;

        if (this.verbose) {
          const vals = adcValues.map(v => v);
          console.log(`[RS485] Board ${address}: QRY_SENSORS types=0x${typesByte.toString(16)} ADC=[${vals.join(',')}]`);
        }
        break;
      }

      case RS485_QRY_VOLTAGE: {
        // Not commonly used, respond with a dummy value
        responseData = Buffer.from([0x03, 0xE8]); // 1000 = 10.00V
        break;
      }

      default:
        if (this.verbose) {
          console.log(`[RS485] Board ${address}: Unknown cmd=${command}`);
        }
        return; // No response
    }

    // Build and send response immediately — real analog boards respond in
    // microseconds.  Adding an artificial setTimeout delay caused ~3% of
    // responses to miss the firmware's 50 ms timeout window because QEMU's
    // virtualized timers and TCP transport already add latency.  Dropped
    // responses during the first RS485 read cycle leave sensor values at
    // SENSOR_VAL_UNDEFINED, triggering sticky manual-clear alarms
    // (WARN_PLENTEMP1/2, WARN_RETURNAIRTEMP, etc.).
    const response = buildResponse(address, command, 0x00, responseData);

    if (this.verbose && this.responseCount < 5) {
      console.log(`[RS485] TX hex (${response.length}B): ${Buffer.from(response).toString('hex')}`);
    }

    if (this.socket && this.connected) {
      this.socket.write(Buffer.from(response));
      this.responseCount++;
    }
  }
}

// ─── Standalone Mode ───────────────────────────────────────────────────────

import { fileURLToPath } from 'url';
const __filename_check = typeof import.meta?.url === 'string' ? fileURLToPath(import.meta.url) : '';
const isMain = process.argv[1] && (__filename_check.includes(process.argv[1]) || process.argv[1].includes('rs485Responder'));

if (isMain) {
  const host = process.env.RS485_HOST || '0.0.0.0';
  const port = parseInt(process.env.RS485_PORT || '9002', 10);
  const panelPort = parseInt(process.env.RS485_PANEL_PORT || '9001', 10);
  const verbose = process.env.RS485_VERBOSE === '1' || process.env.RS485_VERBOSE === 'true';
  const mode = (process.env.RS485_MODE || 'server') as 'client' | 'server';

  console.log(`[RS485] RS485 Analog Board Responder`);
  console.log(`[RS485] Mode: ${mode} on ${host}:${port}`);
  console.log(`[RS485] Simulating 3 default boards (temp + humid + pile temp)`);
  console.log(`[RS485] Control Panel: http://localhost:${panelPort}`);
  console.log(`[RS485] Verbose: ${verbose}`);
  console.log('');

  const responder = new RS485Responder(host, port, undefined, verbose);

  if (mode === 'server') {
    responder.listen().then(() => {
      console.log('[RS485] Server ready, waiting for QEMU to connect...');
    }).catch((err) => {
      console.error(`[RS485] Failed to start server: ${err.message}`);
      process.exit(1);
    });
  } else {
    responder.connect();
  }

  // Start the VFD Modbus TCP simulator
  const vfdPort = parseInt(process.env.VFD_PORT || '5020', 10);
  import('./vfdSimulator.js').then(({ VFDSimulator }) => {
    const vfd = new VFDSimulator({ host: '0.0.0.0', port: vfdPort });
    vfd.start();
    // Make VFD instance available to the panel
    (globalThis as any).__vfdSimulator = vfd;
  }).catch((err) => {
    console.error(`[RS485] Failed to start VFD simulator: ${err.message}`);
  });

  // Start the web control panel (handles jitter + diurnal cycle internally)
  import('./rs485Panel.js').then(({ startRS485Panel }) => {
    startRS485Panel(responder, panelPort);
  }).catch((err) => {
    console.error(`[RS485] Failed to start control panel: ${err.message}`);
    // Fall back to basic jitter if panel fails
    setInterval(() => {
      const s = responder.getState();
      const jitter = (range: number) => (Math.random() - 0.5) * range;
      responder.updateState({
        plenumTemp1: s.plenumTemp1 + jitter(0.3),
        plenumTemp2: s.plenumTemp2 + jitter(0.3),
        outsideTemp: s.outsideTemp + jitter(0.5),
        returnTemp: s.returnTemp + jitter(0.3),
        outsideHumid: Math.max(0, Math.min(100, s.outsideHumid + jitter(1))),
        plenumHumid: Math.max(0, Math.min(100, s.plenumHumid + jitter(1))),
        returnHumid: Math.max(0, Math.min(100, s.returnHumid + jitter(1))),
        co2: Math.max(0, Math.min(10000, s.co2 + jitter(10))),
      });
    }, 5000);
  });

  // Status report
  setInterval(() => {
    const stats = responder.getStats();
    console.log(`[RS485] Stats: connected=${stats.connected} queries=${stats.queries} responses=${stats.responses}`);
  }, 15000);

  // Graceful shutdown
  process.on('SIGINT', () => {
    console.log('\n[RS485] Shutting down...');
    responder.disconnect();
    process.exit(0);
  });
  process.on('SIGTERM', () => {
    responder.disconnect();
    process.exit(0);
  });
}

export default RS485Responder;
