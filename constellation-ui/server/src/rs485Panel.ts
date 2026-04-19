/**
 * RS485 Responder Control Panel — with Physics Engine
 * ─────────────────────────────────────────────────────
 * Web dashboard (port 9001) for controlling simulated analog board
 * sensor values that the RS485 responder feeds to real ARM firmware
 * running in QEMU.
 *
 * Physics modelled:
 *   • Diurnal outside temperature (sinusoidal day/night cycle)
 *   • Outside humidity inverse-coupled to temperature
 *   • Potato respiration heat (metabolic warming of stored product)
 *   • Potato respiration CO₂ + moisture release
 *   • Insulation drift (plenum slowly tracks outside through walls)
 *   • Temperature-humidity psychrometric coupling
 *   • CO₂ natural leakage toward ambient
 *   • Return air warmed by passing through potato pile
 *   • Optional equipment feedback via bridge API polling
 *
 * Routes:
 *   GET  /             → HTML control panel
 *   GET  /api/status   → JSON snapshot (sensors, stats, physics, switches, config)
 *   POST /api/sensors  → manual sensor overrides
 *   POST /api/preset   → apply named preset
 *   POST /api/physics  → update physics parameters
 *   POST /api/switches → update CPLD switch positions
 *   POST /api/reset    → reset everything to defaults
 */

import http from 'http';
import net from 'net';
import RS485Responder from './rs485Responder.js';
import { saveConfig, loadConfig, deleteConfig, getConfigDir } from './simConfig.js';

// ── Equipment name table (matches SerialShift.h EQUIPMENT_IO enum) ──
const EQ_NAMES: Record<number, string> = {
  0: 'Fan', 1: 'Door', 2: 'Refrigeration', 3: 'ClimaCell',
  4: 'Heat', 5: 'Cavity Heat', 6: 'Burner',
  7: 'Humid 1 Head', 8: 'Humid 1 Pump', 9: 'Humid 2 Head', 10: 'Humid 2 Pump',
  11: 'Humid 3 Head', 12: 'Humid 3 Pump',
  13: 'Refrig Stage 1', 14: 'Refrig Stage 2', 15: 'Refrig Stage 3', 16: 'Refrig Stage 4',
  17: 'Refrig Stage 5', 18: 'Refrig Stage 6', 19: 'Refrig Stage 7', 20: 'Refrig Stage 8',
  21: 'Defrost 1', 22: 'Defrost 2', 23: 'Lights 1', 24: 'Lights 2',
  25: 'Aux 1', 26: 'Aux 2', 27: 'Aux 3', 28: 'Aux 4',
  29: 'Aux 5', 30: 'Aux 6', 31: 'Aux 7', 32: 'Aux 8',
  33: 'Power', 34: 'Remote Standby', 35: 'Refrig Standby',
  36: 'Air Flow', 37: 'Low Temp', 38: 'Red Light', 39: 'Yellow Light',
  40: 'Pulse Door Power', 41: 'Pulse Door Open', 42: 'Pulse Door Close',
};

/** Mode name lookup table (matches firmware States.h UI_* enum values) */
const MODE_NAMES: Record<number, string> = {
  1: 'Shutdown', 2: 'Standby', 3: 'Rem Stby', 4: 'Cooling', 5: 'Refrig',
  6: 'Recirc', 7: 'Heating', 8: 'Defrost', 9: 'CO2 Purge', 10: 'Cool Ramp',
  11: 'Refrig Ramp', 12: 'Fan Manual', 13: 'Fan Switch Off', 14: 'Fan Remote Off',
  15: 'Refrig Remote Off', 16: 'Air Cure', 17: 'Burner Cure', 18: 'Cool Dehumid',
  19: 'Refrig Dehumid', 20: 'Remote Off', 21: 'Failure', 22: 'Fan Boost',
};

/** Auto modes where ARM controls outputs (firmware in active HVAC control) */
const AUTO_MODES = new Set([4, 5, 6, 7, 8, 9, 10, 11, 16, 17, 18, 19, 22]);

/** Decoded output port entry */
interface DecodedOutput {
  port: number;       // global port ID (0-35)
  board: string;      // 'main' | 'ex1' | 'ex2'
  bit: number;        // bit position in the CPLD output word
  eqIndex: number;    // EQUIPMENT_IO enum value
  eqName: string;     // human-readable name
  on: boolean;        // true if CPLD bit is set (physical output active)
  armOn?: boolean;    // true if ARM EquipStatus reports output ON (firmware state)
}

/** Map equipment index → EquipStatusData wire-format index for the output state.
 *
 *  Wire format (100 fields from UI_SendEquipStatus):
 *    [0-35]  = GetEquipStatus[0-35]   (StatusStr)
 *    [36-55] = Injected settings      (CavityHeat mode, RemoteOff, Diagnostics)
 *    [56-88] = GetEquipStatus[36-68]  (ExtendedStatusStr: lights, aux, humid3)
 *    [89-99] = More injected settings (RefStage7-8 Diag, Humid3/Aux3-8 RemoteOff)
 *
 *  CPLD shift register bit order (from firmware SerialShift.h):
 *    Main:  DO1=bit10..DO8=bit3, PD_PWR=bit2, PD_DN=bit0, PD_UP=bit1
 *    Expan: DO1=bit7..DO8=bit0
 */
const EQ_TO_EQSTATUS_OUTPUT: Record<number, number> = {
  // Do NOT map Fan (0) — VFD-controlled, CPLD bit only for manual mode
  // Do NOT map Doors (1) — PIDU actuator, not simple on/off
  3: 5,    // ClimaCell  → eqStatus[5]
  4: 28,   // Heat       → eqStatus[28]
  5: 32,   // CavityHeat → eqStatus[32]
  6: 7,    // Burner     → eqStatus[7]
  7: 10,   // Humid 1 Head → eqStatus[10]
  8: 11,   // Humid 1 Pump → eqStatus[11]
  9: 13,   // Humid 2 Head → eqStatus[13]
  10: 14,  // Humid 2 Pump → eqStatus[14]
  11: 77,  // Humid 3 Head → eqStatus[77]
  12: 78,  // Humid 3 Pump → eqStatus[78]
  13: 17,  // Refrig Stage 1 → eqStatus[17]
  14: 18,  // Refrig Stage 2 → eqStatus[18]
  15: 19,  // Refrig Stage 3 → eqStatus[19]
  16: 20,  // Refrig Stage 4 → eqStatus[20]
  17: 21,  // Refrig Stage 5 → eqStatus[21]
  18: 22,  // Refrig Stage 6 → eqStatus[22]
  19: 23,  // Refrig Stage 7 → eqStatus[23]
  20: 24,  // Refrig Stage 8 → eqStatus[24]
  21: 25,  // Defrost 1 → eqStatus[25]
  22: 26,  // Defrost 2 → eqStatus[26]
  23: 57,  // Lights 1  → eqStatus[57]
  24: 59,  // Lights 2  → eqStatus[59]
  25: 61,  // Aux 1     → eqStatus[61]
  26: 63,  // Aux 2     → eqStatus[63]
  27: 65,  // Aux 3     → eqStatus[65]
  28: 67,  // Aux 4     → eqStatus[67]
  29: 69,  // Aux 5     → eqStatus[69]
  30: 71,  // Aux 6     → eqStatus[71]
  31: 73,  // Aux 7     → eqStatus[73]
  32: 75,  // Aux 8     → eqStatus[75]
};

/** Decode CPLD output words using port-indexed OutputConfig.
 *  OutputConfig is port-indexed: outputConfig[portId] = eqIndex (or '-1').
 *
 *  CPLD shift register bit ordering (from firmware SerialShift.h):
 *    Main board: DO1=bit10, DO2=bit9, ..., DO8=bit3, PD_PWR=bit2, PD_DN=bit0, PD_UP=bit1
 *    Expansion:  DO1=bit7, DO2=bit6, ..., DO8=bit0
 *  Port numbering: Main ports 1-11, Ex1 ports start at ex1Start, Ex2 at ex2Start.
 */

// Main board: bit → board-relative port (1-indexed)
//   bits 3-10 = DO8..DO1 (reversed), bits 0-1 = PD_DN, PD_UP
const MAIN_BIT_TO_PORT: Record<number, number> = {
  0:  9,  // SS_MAIN_PD_DN  → port 9  (Door Close)
  1: 10,  // SS_MAIN_PD_UP  → port 10 (Door Open)
  3:  8,  // SS_MAIN_DO8    → port 8
  4:  7,  // SS_MAIN_DO7    → port 7
  5:  6,  // SS_MAIN_DO6    → port 6
  6:  5,  // SS_MAIN_DO5    → port 5
  7:  4,  // SS_MAIN_DO4    → port 4
  8:  3,  // SS_MAIN_DO3    → port 3
  9:  2,  // SS_MAIN_DO2    → port 2
  10: 1,  // SS_MAIN_DO1    → port 1
};

function decodeCPLDOutputs(
  cpldOut: CPLDOutputState,
  outputConfig: string[],
  boardOffsets: { mainStart: number; mainOutBits: number; ex1Start: number; ex2Start: number },
): DecodedOutput[] {
  const results: DecodedOutput[] = [];

  // Main board: use explicit bit→port lookup
  for (let bit = 0; bit < boardOffsets.mainOutBits; bit++) {
    const relPort = MAIN_BIT_TO_PORT[bit];
    if (relPort === undefined) continue;
    const port = boardOffsets.mainStart + relPort - 1;  // mainStart is 1-based port 1
    const eqStr = outputConfig[port];
    if (!eqStr || eqStr === '-1') continue;
    const eqIndex = parseInt(eqStr, 10);
    if (isNaN(eqIndex) || eqIndex < 0) continue;
    const on = (cpldOut.main & (1 << bit)) !== 0;
    results.push({ port, board: 'main', bit, eqIndex, eqName: EQ_NAMES[eqIndex] ?? `EQ ${eqIndex}`, on });
  }

  // Expansion boards: DO1=bit7, DO2=bit6, ..., DO8=bit0 (reversed)
  for (const [word, offset, name] of [
    [cpldOut.ex1, boardOffsets.ex1Start, 'ex1'],
    [cpldOut.ex2, boardOffsets.ex2Start, 'ex2'],
  ] as const) {
    for (let bit = 0; bit < 8; bit++) {
      const port = offset + 7 - bit;  // bit 7 → first port, bit 0 → last port
      const eqStr = outputConfig[port];
      if (!eqStr || eqStr === '-1') continue;
      const eqIndex = parseInt(eqStr, 10);
      if (isNaN(eqIndex) || eqIndex < 0) continue;
      const on = (word & (1 << bit)) !== 0;
      results.push({ port, board: name, bit, eqIndex, eqName: EQ_NAMES[eqIndex] ?? `EQ ${eqIndex}`, on });
    }
  }

  return results;
}

// ══════════════════════════════════════════════════════════════════
// Types
// ══════════════════════════════════════════════════════════════════

interface PhysicsConfig {
  enabled: boolean;

  // ── Diurnal outside temperature cycle ──
  outsideTempMin: number;         // night low °C            (default -2)
  outsideTempMax: number;         // day high °C             (default 15)
  outsideTempNoise: number;       // ±°C noise               (default 0.3)
  diurnalPeriodSec: number;       // cycle period sec        (120 = demo, 86400 = real)
  peakHour: number;               // hour of day for peak    (15 = 3pm)

  // ── Outside humidity ──
  outsideHumidBase: number;       // % at mid-temp           (default 60)
  outsideHumidTempCoeff: number;  // % drop per normalized °C (default 15)
  outsideHumidNoise: number;      // ±% noise                (default 2)

  // ── Building / Insulation ──
  insulationFactor: number;       // drift rate toward outside  (default 0.001)

  // ── Potato respiration ──
  potatoMass: number;             // 0–3 relative mass       (default 1.0)
  respirationHeatRate: number;    // °C/tick base rate        (default 0.00015)
  respirationCo2Rate: number;     // ppm/tick base rate       (default 0.8)
  respirationMoistureRate: number;// %/tick base rate         (default 0.01)

  // ── Temperature-humidity coupling (psychrometric) ──
  tempHumidCoeff: number;         // %RH change per °C rise   (default 2.0)

  // ── CO₂ dynamics ──
  co2AmbientPpm: number;          // outdoor ambient          (default 420)
  co2LeakRate: number;            // natural leak proportion/tick (default 0.002)

  // ── Return air ──
  returnTempOffset: number;       // °C warmer than plenum    (default 0.3)
  returnHumidOffset: number;      // % lower than plenum      (default -2)

  // ── Equipment feedback rates (when bridge polling active) ──
  coolingRate: number;            // fan+outside air rate      (default 0.03)
  heatingRate: number;            // heater/burner rate        (default 0.06)
  refrigRate: number;             // refrig cooling rate       (default 0.04)
  humidifyRate: number;           // humidifier %/tick         (default 0.2)
  naturalDryRate: number;         // natural drying %/tick     (default 0.03)

  // ── Noise levels ──
  plenumTempNoise: number;        // ±°C   (default 0.08)
  returnTempNoise: number;        // ±°C   (default 0.08)
  humidNoise: number;             // ±%    (default 0.3)
  co2Noise: number;               // ±ppm  (default 3)

  // ── Bridge equipment polling ──
  bridgeUrl: string;              // e.g. "http://localhost:3001"
  equipmentPolling: boolean;      // whether to poll bridge
}

/** Per-sensor manual override — physics won't touch overridden sensors */
interface ManualOverrides {
  outsideTemp: boolean;
  plenumTemp: boolean;
  returnTemp: boolean;
  outsideHumid: boolean;
  plenumHumid: boolean;
  returnHumid: boolean;
  co2: boolean;
}

/** Equipment state read from bridge (optional feedback loop) */
interface EquipmentState {
  fanOn: boolean;
  coolingMode: boolean;
  refrigOn: boolean;
  heatOn: boolean;
  burnerOn: boolean;
  humidOn: boolean;
  lastPoll: number;
}

/**
 * CPLD Switch State — matches firmware SerialShift.h EQUIPMENT_IO enum.
 * Each 3-position switch has AUTO/MANUAL CPLD bits.
 * Position: 'off' = neither bit set, 'auto' = AUTO bit, 'manual' = MANUAL bit.
 * Start/Stop and Refrig are 2-position (binary).
 */
type SwitchPosition3 = 'auto' | 'off' | 'manual';
type SwitchPosition2 = 'auto' | 'off';
type StartStopPosition = 'start' | 'shutdown';

interface SwitchState {
  startStop: StartStopPosition;       // SW_START_STOP (43)
  fan: SwitchPosition3;               // SW_FAN_AUTO(44) / SW_FAN_MANUAL(45)
  freshAir: SwitchPosition3;          // SW_FRESHAIR_AUTO(46) / SW_FRESHAIR_MANUAL(47)  — Door switch
  climacell: SwitchPosition3;         // SW_CLIMACELL_AUTO(48) / SW_CLIMACELL_MANUAL(49)
  humid: SwitchPosition3;             // SW_HUMID_AUTO(50) / SW_HUMID_MANUAL(51)
  refrig: SwitchPosition2;            // SW_REFRIG_AUTO(52)
}

/** CPLD bit mapping for switch state → firmware EQUIPMENT_IO indices */
const SWITCH_EQ_MAP = {
  startStop: { eq: 43 },                           // SW_START_STOP
  fan:       { eqAuto: 44, eqManual: 45 },         // SW_FAN_AUTO, SW_FAN_MANUAL
  freshAir:  { eqAuto: 46, eqManual: 47 },         // SW_FRESHAIR_AUTO, SW_FRESHAIR_MANUAL
  climacell: { eqAuto: 48, eqManual: 49 },         // SW_CLIMACELL_AUTO, SW_CLIMACELL_MANUAL
  humid:     { eqAuto: 50, eqManual: 51 },         // SW_HUMID_AUTO, SW_HUMID_MANUAL
  refrig:    { eqAuto: 52 },                        // SW_REFRIG_AUTO
} as const;

/**
 * Digital Input State — per-board, per-port boolean (true = closed/proven, false = open/fault).
 * These get packed into CPLD InputState words and sent to QEMU via TCP chardev.
 *
 * Main board v2 (10 inputs):
 *   Port 1-8 = DI1-DI8 at bits 31-24 (reversed: DI1=MSB)
 *   Port 9   = AIRFLOW at bit 22
 *   Port 10  = LOWTEMP at bit 23
 *
 * Expansion boards (8 inputs each):
 *   Port 1-8 = DI1-DI8 at bits 13-6 (reversed: DI1=bit13)
 */
interface DigitalInputState {
  /** Main board: 10 inputs indexed [0]=Port1/DI1 .. [7]=Port8/DI8, [8]=AIRFLOW, [9]=LOWTEMP */
  main: boolean[];
  /** Expansion 1: 8 inputs indexed [0]=Port1/DI1 .. [7]=Port8/DI8 */
  ex1: boolean[];
  /** Expansion 2: 8 inputs */
  ex2: boolean[];
  /** Whether expansion boards are installed (sends version 1 vs 0xF) */
  ex1Installed: boolean;
  ex2Installed: boolean;
}

/** Which inputs use inverted logic (close-to-trigger instead of close-to-prove).
 *  Stored as arrays of port indices (0-based). */
interface InvertedInputs {
  main: number[];
  ex1: number[];
  ex2: number[];
}

function defaultInvertedInputs(): InvertedInputs {
  return { main: [0, 1], ex1: [], ex2: [] }; // Power Detect and Remote Standby
}

/** CPLD output state reported by QEMU (what firmware is commanding) */
interface CPLDOutputState {
  main: number;
  ex1: number;
  ex2: number;
}

/** Factory default equipment names per input port (from IO Config defaults) */
const MAIN_INPUT_LABELS: string[] = [
  'Power Detect',       // Port 1 (DI1) → EQ_POWER
  'Remote Standby',     // Port 2 (DI2) → EQ_REMOTE_STANDBY
  'Fan Proof',          // Port 3 (DI3) → EQ_FAN
  'Climacell Proof',    // Port 4 (DI4) → EQ_CLIMACELL
  'Humid 1 Proof',      // Port 5 (DI5) → EQ_HUMID_HEAD1
  '(Unassigned)',       // Port 6 (DI6)
  'Humid 2 Proof',      // Port 7 (DI7) → EQ_HUMID_HEAD2
  '(Unassigned)',       // Port 8 (DI8)
  'Air Flow Switch',    // Port 9 (AIRFLOW)
  'Low Temp Switch',    // Port 10 (LOWTEMP)
];

const EX_INPUT_LABELS: string[] = [
  'Heat Proof',         // Port 1 (DI1) → EQ_HEAT (Ex1 factory default)
  '(Unassigned)',
  '(Unassigned)',
  '(Unassigned)',
  '(Unassigned)',
  '(Unassigned)',
  '(Unassigned)',
  '(Unassigned)',
];

// ══════════════════════════════════════════════════════════════════
// Defaults
// ══════════════════════════════════════════════════════════════════

function defaultPhysics(): PhysicsConfig {
  return {
    enabled: true,
    outsideTempMin: -2,
    outsideTempMax: 15,
    outsideTempNoise: 0.3,
    diurnalPeriodSec: 120,
    peakHour: 15,
    outsideHumidBase: 60,
    outsideHumidTempCoeff: 15,
    outsideHumidNoise: 2,
    insulationFactor: 0.001,
    potatoMass: 1.0,
    respirationHeatRate: 0.00015,
    respirationCo2Rate: 0.8,
    respirationMoistureRate: 0.01,
    tempHumidCoeff: 2.0,
    co2AmbientPpm: 420,
    co2LeakRate: 0.002,
    returnTempOffset: 0.3,
    returnHumidOffset: -2,
    coolingRate: 0.03,
    heatingRate: 0.06,
    refrigRate: 0.04,
    humidifyRate: 0.2,
    naturalDryRate: 0.03,
    plenumTempNoise: 0.08,
    returnTempNoise: 0.08,
    humidNoise: 0.3,
    co2Noise: 3,
    bridgeUrl: 'http://localhost:3001',
    equipmentPolling: false,
  };
}

function defaultOverrides(): ManualOverrides {
  return {
    outsideTemp: false, plenumTemp: false, returnTemp: false,
    outsideHumid: false, plenumHumid: false, returnHumid: false, co2: false,
  };
}

function defaultEquipment(): EquipmentState {
  return {
    fanOn: false, coolingMode: false, refrigOn: false,
    heatOn: false, burnerOn: false, humidOn: false, lastPoll: 0,
  };
}

function defaultSwitchState(): SwitchState {
  return {
    startStop: 'start',
    fan: 'auto',
    freshAir: 'auto',
    climacell: 'auto',
    humid: 'auto',
    refrig: 'auto',
  };
}

function defaultDigitalInputs(): DigitalInputState {
  // true = closed.  Most inputs are "close-to-prove" (closed = proven).
  // Power (0) and Remote Standby (1) are "close-to-trigger" — their
  // normal/safe state is OPEN (false).
  const main = new Array(10).fill(true);  // Ports 1-8: DI, Port 9: AIRFLOW, Port 10: LOWTEMP
  main[0] = false; // Power Detect: open = no power failure (normal)
  main[1] = false; // Remote Standby: open = not in standby (normal)
  return {
    main,
    ex1: new Array(8).fill(true),
    ex2: new Array(8).fill(true),
    ex1Installed: false,
    ex2Installed: false,
  };
}

function defaultCPLDOutputs(): CPLDOutputState {
  return { main: 0, ex1: 0, ex2: 0 };
}

/**
 * Build the 32-bit CPLD InputState for the main board (v2 CPLD).
 * Layout: [3:0]=version, [21:4]=switches, [31:22]=digital inputs
 */
function buildMainInputState(sw: SwitchState, di: DigitalInputState, invertedInputs: InvertedInputs): number {
  let state = 0;

  // CPLD version 2: reversed bits [3:0] = 0x4
  state |= 0x4;

  // Switches for v2 CPLD (mapped to SS_MAIN_SWx bit positions)
  if (sw.startStop === 'start')     state |= (1 << 12); // SW8
  if (sw.refrig === 'auto')         state |= (1 << 13); // SW9
  if (sw.fan === 'manual')          state |= (1 << 14); // SW10
  if (sw.fan === 'auto')            state |= (1 << 15); // SW11
  if (sw.climacell === 'manual')    state |= (1 << 16); // SW12
  if (sw.climacell === 'auto')      state |= (1 << 17); // SW13
  if (sw.humid === 'manual')        state |= (1 << 18); // SW14
  if (sw.humid === 'auto')          state |= (1 << 19); // SW15
  if (sw.freshAir === 'auto')       state |= (1 << 20); // SW16
  if (sw.freshAir === 'manual')     state |= (1 << 21); // SW17

  // Digital inputs: CPLD is active-LOW — bit 0 = contact closed/proven,
  // bit 1 = contact open/fault.  The firmware reads bits via CheckInputs(mask)
  // which returns true when bit is CLEAR (active-low).
  //
  // Normal (close-to-prove): Closed → bit 0 (proven), Open → bit 1 (fault)
  // Inverted (close-to-trigger): Closed → bit 1 (fault), Open → bit 0 (normal)
  // Port 1-8 map to bits 31-24 (DI1=31, DI8=24)
  for (let i = 0; i < 8; i++) {
    const inv = invertedInputs.main.includes(i);
    if (inv ? di.main[i] : !di.main[i]) state |= (1 << (31 - i));
  }
  // Port 9 = AIRFLOW at bit 22, Port 10 = LOWTEMP at bit 23
  if (!di.main[8]) state |= (1 << 22);
  if (!di.main[9]) state |= (1 << 23);

  return state >>> 0; // Ensure unsigned
}

/**
 * Build the 22-bit CPLD InputState for an expansion board.
 * Layout: [3:0]=version, [4]=header, [5]=fault, [13:6]=DI, [21:14]=switches
 */
function buildExInputState(di: boolean[], installed: boolean, invertedPorts: number[] = []): number {
  if (!installed) return 0x0000000F; // Version = 0xF (not installed)

  let state = 0;
  // CPLD version 1: reversed bits [3:0] = 0x8
  state |= 0x8;
  // Header present (bit 4)
  state |= (1 << 4);
  // No fault (bit 5 = 0)
  // Digital inputs: CPLD is active-LOW — bit 0 = closed/proven, 1 = open/fault.
  // DI1=bit13, DI2=bit12, ... DI8=bit6
  for (let i = 0; i < 8; i++) {
    const inv = invertedPorts.includes(i);
    if (inv ? di[i] : !di[i]) state |= (1 << (13 - i));
  }
  return state >>> 0;
}

// ══════════════════════════════════════════════════════════════════
// Presets (physics-informed starting conditions)
// ══════════════════════════════════════════════════════════════════

interface SensorPreset {
  label: string;
  icon: string;
  values: Record<string, number>;
  physics?: Partial<PhysicsConfig>;
}

const PRESETS: Record<string, SensorPreset> = {
  'cold-night': {
    label: 'Cold Night', icon: '❄️',
    values: {
      plenumTemp1: -5, plenumTemp2: -5.3, outsideTemp: -12, returnTemp: -2,
      outsideHumid: 80, plenumHumid: 75, returnHumid: 70, co2: 380,
    },
    physics: { outsideTempMin: -18, outsideTempMax: -5, outsideHumidBase: 78 },
  },
  'mild-day': {
    label: 'Mild Day', icon: '🌤️',
    values: {
      plenumTemp1: 8, plenumTemp2: 8.2, outsideTemp: 12, returnTemp: 10,
      outsideHumid: 55, plenumHumid: 60, returnHumid: 58, co2: 420,
    },
    physics: { outsideTempMin: 4, outsideTempMax: 16, outsideHumidBase: 55 },
  },
  'warm-day': {
    label: 'Warm Day', icon: '☀️',
    values: {
      plenumTemp1: 18, plenumTemp2: 18.5, outsideTemp: 28, returnTemp: 22,
      outsideHumid: 35, plenumHumid: 40, returnHumid: 38, co2: 500,
    },
    physics: { outsideTempMin: 18, outsideTempMax: 32, outsideHumidBase: 40 },
  },
  'hot-humid': {
    label: 'Hot & Humid', icon: '🥵',
    values: {
      plenumTemp1: 30, plenumTemp2: 30.8, outsideTemp: 38, returnTemp: 33,
      outsideHumid: 85, plenumHumid: 88, returnHumid: 82, co2: 600,
    },
    physics: { outsideTempMin: 28, outsideTempMax: 42, outsideHumidBase: 82 },
  },
  'freezing': {
    label: 'Freezing', icon: '🧊',
    values: {
      plenumTemp1: -15, plenumTemp2: -15.5, outsideTemp: -25, returnTemp: -12,
      outsideHumid: 90, plenumHumid: 85, returnHumid: 80, co2: 350,
    },
    physics: { outsideTempMin: -30, outsideTempMax: -18, outsideHumidBase: 88 },
  },
  'potato-storage': {
    label: 'Potato Storage', icon: '🥔',
    values: {
      plenumTemp1: 7.5, plenumTemp2: 7.7, outsideTemp: 2, returnTemp: 12.5,
      outsideHumid: 65, plenumHumid: 92, returnHumid: 58, co2: 2500,
    },
    physics: { outsideTempMin: -2, outsideTempMax: 15, potatoMass: 2.0, outsideHumidBase: 60 },
  },
};

// ── Helpers ──

function clamp(v: number, lo: number, hi: number): number { return Math.max(lo, Math.min(hi, v)); }
function noise(amp: number): number { return (Math.random() - 0.5) * 2 * amp; }

// ══════════════════════════════════════════════════════════════════
// HTML Panel
// ══════════════════════════════════════════════════════════════════

const PANEL_HTML = `<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>RS485 Sensor Control Panel</title>
<style>
  :root {
    --bg: #1a1d23; --bg2: #22262e; --bg3: #2a2f3a; --border: #3a3f4a;
    --text: #e0e4ec; --text2: #8890a0; --accent: #4fc3f7; --accent2: #81c784;
    --warn: #ffb74d; --err: #ef5350; --on: #66bb6a; --off: #555;
    --purple: #ce93d8; --orange: #ff9800;
  }
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body { font-family: 'Segoe UI', system-ui, sans-serif; background: var(--bg); color: var(--text); font-size: 14px; line-height: 1.5; }
  header { background: var(--bg2); border-bottom: 1px solid var(--border); padding: 12px 24px; display: flex; justify-content: space-between; align-items: center; }
  header h1 { font-size: 18px; font-weight: 600; color: var(--accent); }
  header .meta { font-size: 12px; color: var(--text2); display: flex; align-items: center; gap: 12px; }
  .status-dot { display: inline-block; width: 8px; height: 8px; border-radius: 50%; margin-right: 4px; }
  .status-dot.ok { background: var(--on); box-shadow: 0 0 6px var(--on); }
  .status-dot.err { background: var(--err); box-shadow: 0 0 6px var(--err); }
  .stat-badge { background: var(--bg3); padding: 2px 8px; border-radius: 4px; font-variant-numeric: tabular-nums; }
  .physics-badge { font-size: 11px; padding: 2px 8px; border-radius: 10px; font-weight: 600; }
  .physics-badge.on { background: #1b5e2055; color: var(--on); border: 1px solid var(--on); }
  .physics-badge.off { background: #b7171755; color: var(--err); border: 1px solid var(--err); }
  main { max-width: 1400px; margin: 0 auto; padding: 0 16px 16px; }
  /* ── Tab bar ── */
  .tab-bar { display: flex; gap: 0; border-bottom: 2px solid var(--border); margin-bottom: 16px; position: sticky; top: 0; z-index: 20; background: var(--bg); padding-top: 12px; }
  .tab-btn { padding: 10px 24px; font-size: 13px; font-weight: 600; letter-spacing: 0.5px; text-transform: uppercase; color: var(--text2); background: transparent; border: none; cursor: pointer; border-bottom: 2px solid transparent; margin-bottom: -2px; transition: color 0.15s, border-color 0.15s; }
  .tab-btn:hover { color: var(--text); }
  .tab-btn.active { color: var(--accent); border-bottom-color: var(--accent); }
  .tab-content { display: none; }
  .tab-content.active { display: grid; gap: 16px; grid-template-columns: 1fr 1fr; }
  section { background: var(--bg2); border: 1px solid var(--border); border-radius: 8px; padding: 16px; }
  section.full { grid-column: 1 / -1; }
  h2 { font-size: 14px; text-transform: uppercase; letter-spacing: 1px; color: var(--text2); margin-bottom: 12px; border-bottom: 1px solid var(--border); padding-bottom: 6px; }
  h3 { font-size: 13px; color: var(--accent); margin: 12px 0 6px; }
  .grid { display: grid; gap: 8px; }
  .grid-2 { grid-template-columns: 1fr 1fr; }
  .grid-3 { grid-template-columns: 1fr 1fr 1fr; }
  .grid-4 { grid-template-columns: 1fr 1fr 1fr 1fr; }

  .sensor-card { background: var(--bg3); border-radius: 6px; padding: 12px 14px; position: relative; }
  .sensor-card .label { font-size: 11px; color: var(--text2); text-transform: uppercase; letter-spacing: 0.5px; }
  .sensor-card .value { font-size: 24px; font-weight: 700; font-variant-numeric: tabular-nums; margin: 4px 0; }
  .sensor-card .value.temp { color: var(--accent); }
  .sensor-card .value.humid { color: var(--accent2); }
  .sensor-card .value.co2 { color: var(--warn); }
  .sensor-card .sub { font-size: 11px; color: var(--text2); }
  .sensor-card .sparkline { position: absolute; bottom: 4px; right: 8px; width: 60px; height: 20px; }
  .sensor-card .sparkline polyline { fill: none; stroke: var(--accent); stroke-width: 1.2; opacity: 0.4; }
  .sensor-card .override-badge { position: absolute; top: 6px; right: 8px; font-size: 9px; background: var(--warn); color: #000; padding: 1px 5px; border-radius: 3px; font-weight: 700; display: none; }
  .sensor-card .override-badge.active { display: inline; }

  .control-row { display: flex; align-items: center; gap: 10px; margin-bottom: 10px; padding: 8px 12px; background: var(--bg3); border-radius: 6px; }
  .control-row .ctrl-label { min-width: 120px; font-size: 13px; color: var(--text2); }
  .control-row input[type=range] {
    flex: 1; -webkit-appearance: none; height: 6px; border-radius: 3px;
    background: var(--border); outline: none; cursor: pointer;
  }
  .control-row input[type=range]::-webkit-slider-thumb {
    -webkit-appearance: none; width: 16px; height: 16px; border-radius: 50%;
    background: var(--accent); cursor: pointer; border: 2px solid var(--bg);
  }
  .control-row input[type=range].humid::-webkit-slider-thumb { background: var(--accent2); }
  .control-row input[type=range].co2::-webkit-slider-thumb { background: var(--warn); }
  .control-row input[type=range].physics::-webkit-slider-thumb { background: var(--purple); }
  .control-row input[type=number] {
    width: 80px; padding: 4px 6px; background: var(--bg); border: 1px solid var(--border);
    border-radius: 4px; color: var(--text); font-size: 13px; font-variant-numeric: tabular-nums;
    text-align: right;
  }
  .control-row input[type=number]:focus { outline: none; border-color: var(--accent); }
  .control-row .unit { font-size: 12px; color: var(--text2); min-width: 40px; }

  .toggle-row { display: flex; align-items: center; gap: 10px; margin-bottom: 8px; }
  .toggle-row label { font-size: 13px; flex: 1; }
  .toggle { position: relative; display: inline-block; width: 42px; height: 22px; flex-shrink: 0; cursor: pointer; }
  .toggle input { opacity: 0; width: 0; height: 0; }
  .toggle .slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background: var(--border); border-radius: 22px; transition: 0.2s; }
  .toggle .slider:before { position: absolute; content: ''; height: 16px; width: 16px; left: 3px; bottom: 3px; background: var(--text2); border-radius: 50%; transition: 0.2s; }
  .toggle input:checked + .slider { background: var(--accent); }
  .toggle input:checked + .slider:before { transform: translateX(20px); background: white; }

  .field { margin-bottom: 8px; }
  .field label { display: block; font-size: 12px; color: var(--text2); margin-bottom: 2px; }
  .field input[type=number] {
    width: 100%; padding: 6px 8px; background: var(--bg); border: 1px solid var(--border);
    border-radius: 4px; color: var(--text); font-size: 13px; font-variant-numeric: tabular-nums;
  }
  .field input[type=number]:focus { outline: none; border-color: var(--accent); }

  button { padding: 8px 16px; border: 1px solid var(--border); border-radius: 4px; background: var(--bg3); color: var(--text); font-size: 13px; cursor: pointer; transition: all 0.15s; }
  button:hover { background: var(--border); border-color: var(--accent); }
  button.primary { background: var(--accent); color: var(--bg); border-color: var(--accent); font-weight: 600; }
  button.primary:hover { opacity: 0.85; }
  button.warn { background: var(--warn); color: var(--bg); border-color: var(--warn); font-weight: 600; }
  .btn-row { display: flex; gap: 8px; flex-wrap: wrap; margin-top: 8px; }
  .preset-btn { min-width: 110px; text-align: center; }
  .preset-btn:hover { transform: translateY(-1px); box-shadow: 0 2px 8px rgba(0,0,0,0.3); }

  .physics-status { display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 8px; margin-bottom: 12px; }
  .phys-stat { background: var(--bg3); border-radius: 6px; padding: 8px 12px; text-align: center; }
  .phys-stat .phys-label { font-size: 10px; color: var(--text2); text-transform: uppercase; letter-spacing: 0.5px; }
  .phys-stat .phys-val { font-size: 16px; font-weight: 700; font-variant-numeric: tabular-nums; margin-top: 2px; }

  .equip-grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 6px; margin-top: 8px; }
  .equip-item { display: flex; align-items: center; gap: 6px; padding: 4px 8px; background: var(--bg3); border-radius: 4px; font-size: 12px; }
  .equip-dot { width: 8px; height: 8px; border-radius: 50%; }
  .equip-dot.on { background: var(--on); box-shadow: 0 0 4px var(--on); }
  .equip-dot.off { background: var(--off); }

  .eq-row { display: flex; align-items: center; gap: 8px; padding: 6px 10px; background: var(--bg3); border-radius: 4px; }
  .eq-row .dot { width: 10px; height: 10px; border-radius: 50%; flex-shrink: 0; }
  .eq-row .dot.on { background: var(--on); box-shadow: 0 0 6px var(--on); }
  .eq-row .dot.off { background: var(--off); }
  .eq-row .name { flex: 1; font-size: 13px; }
  .eq-row .val { font-size: 13px; color: var(--text2); font-variant-numeric: tabular-nums; min-width: 50px; text-align: right; }

  .potato-visual { display: flex; align-items: center; gap: 12px; padding: 10px 14px; background: var(--bg3); border-radius: 6px; margin-bottom: 12px; }
  .potato-pile { font-size: 28px; filter: grayscale(0.3); }
  .potato-info { flex: 1; }
  .potato-info .mass-label { font-size: 12px; color: var(--text2); }
  .potato-info .mass-val { font-size: 20px; font-weight: 700; color: var(--warn); }
  .potato-info .mass-desc { font-size: 11px; color: var(--text2); }

  .toast { position: fixed; bottom: 20px; right: 20px; background: var(--bg3); border: 1px solid var(--accent); border-radius: 6px; padding: 10px 16px; font-size: 13px; opacity: 0; transition: opacity 0.3s; pointer-events: none; z-index: 100; }
  .toast.show { opacity: 1; }
  .section-note { font-size: 11px; color: var(--text2); margin-bottom: 8px; padding: 6px 10px; background: var(--bg); border-radius: 4px; border-left: 3px solid var(--accent); }

  /* ── Dynamic Equipment Outputs ── */
  .dyn-out-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(200px, 1fr)); gap: 6px; }
  .dyn-out-item {
    display: flex; align-items: center; gap: 8px; padding: 8px 12px;
    background: var(--bg3); border-radius: 6px; border: 1px solid var(--border);
    transition: border-color 0.2s, box-shadow 0.2s;
  }
  .dyn-out-item.active { border-color: var(--on); box-shadow: 0 0 6px rgba(102,187,106,0.15); }
  .dyn-out-item.mismatch { border-color: var(--warn); box-shadow: 0 0 6px rgba(255,183,77,0.2); }
  .dyn-out-dot { width: 10px; height: 10px; border-radius: 50%; flex-shrink: 0; }
  .dyn-out-dot.on { background: var(--on); box-shadow: 0 0 6px var(--on); }
  .dyn-out-dot.off { background: var(--off); }
  .dyn-out-info { flex: 1; min-width: 0; }
  .dyn-out-name { font-size: 12px; font-weight: 600; color: var(--text); white-space: nowrap; overflow: hidden; text-overflow: ellipsis; }
  .dyn-out-port { font-size: 10px; color: var(--text2); }
  .dyn-out-state { font-size: 11px; font-weight: 700; min-width: 28px; text-align: right; }
  .dyn-out-state.on { color: var(--on); }
  .dyn-out-state.off { color: var(--off); }
  .dyn-out-states { display: flex; flex-direction: column; align-items: flex-end; gap: 1px; min-width: 50px; }
  .dyn-out-arm { font-size: 9px; color: var(--text2); font-weight: 500; }
  .dyn-out-arm.mismatch { color: var(--warn); font-weight: 700; }
  .dyn-out-badge { display: inline-flex; align-items: center; gap: 6px; padding: 3px 10px; border-radius: 12px; font-size: 11px; font-weight: 600; }
  .dyn-out-badge.loaded { background: #1b5e2055; color: var(--on); border: 1px solid var(--on); }
  .dyn-out-badge.waiting { background: #b7171755; color: var(--err); border: 1px solid var(--err); }

  /* ── CPLD Switch Panel ── */
  .switch-panel { display: grid; grid-template-columns: repeat(3, 1fr); gap: 10px; }
  .switch-group {
    background: var(--bg3); border-radius: 8px; padding: 14px; text-align: center;
    border: 1px solid var(--border); transition: border-color 0.2s, box-shadow 0.2s;
  }
  .switch-group.active { border-color: var(--on); box-shadow: 0 0 8px rgba(102,187,106,0.15); }
  .switch-group .sw-label {
    font-size: 11px; text-transform: uppercase; letter-spacing: 1px;
    color: var(--text2); margin-bottom: 6px; font-weight: 600;
  }
  .switch-group .sw-eq-id { font-size: 9px; color: var(--text2); opacity: 0.5; margin-bottom: 6px; }
  .switch-group .sw-indicator {
    font-size: 20px; font-weight: 700; margin: 4px 0 8px;
    font-variant-numeric: tabular-nums; min-height: 28px;
  }
  .switch-group .sw-indicator.pos-auto { color: var(--on); }
  .switch-group .sw-indicator.pos-off { color: var(--off); }
  .switch-group .sw-indicator.pos-manual { color: var(--warn); }
  .switch-group .sw-indicator.pos-start { color: var(--on); }
  .switch-group .sw-indicator.pos-shutdown { color: var(--err); }

  /* 3-position toggle using segmented control */
  .sw-toggle { display: inline-flex; border-radius: 6px; overflow: hidden; border: 1px solid var(--border); }
  .sw-toggle input[type=radio] { display: none; }
  .sw-toggle label {
    padding: 6px 14px; font-size: 11px; font-weight: 600; cursor: pointer;
    color: var(--text2); background: var(--bg); text-transform: uppercase;
    letter-spacing: 0.5px; transition: all 0.15s; user-select: none;
    border-right: 1px solid var(--border);
  }
  .sw-toggle label:last-child { border-right: none; }
  .sw-toggle label:hover { background: var(--bg3); color: var(--text); }
  .sw-toggle input[type=radio]:checked + label.auto-label { background: var(--on); color: #000; }
  .sw-toggle input[type=radio]:checked + label.off-label { background: var(--border); color: var(--text); }
  .sw-toggle input[type=radio]:checked + label.manual-label { background: var(--warn); color: #000; }
  .sw-toggle input[type=radio]:checked + label.start-label { background: var(--on); color: #000; }
  .sw-toggle input[type=radio]:checked + label.shutdown-label { background: var(--err); color: #000; }

  /* 2-position toggle */
  .sw-toggle-2 label { padding: 6px 18px; }

  /* Start/Stop special styling */
  .switch-group.start-stop { grid-column: 1 / -1; }
  .switch-group.start-stop .sw-indicator { font-size: 24px; }

  /* ── Digital Input Proof Panel ── */
  .di-board { margin-bottom: 12px; }
  .di-board-header {
    display: flex; justify-content: space-between; align-items: center;
    padding: 6px 10px; background: var(--bg); border-radius: 4px; margin-bottom: 6px;
    font-size: 12px; font-weight: 600; color: var(--text2); text-transform: uppercase; letter-spacing: 0.5px;
  }
  .di-board-header .di-install-toggle { display: flex; align-items: center; gap: 6px; font-size: 11px; text-transform: none; }
  .di-grid { display: grid; grid-template-columns: 1fr 1fr; gap: 4px; }
  .di-row {
    display: flex; align-items: center; gap: 8px; padding: 6px 10px;
    background: var(--bg3); border-radius: 4px; border: 1px solid var(--border);
    transition: border-color 0.2s;
  }
  .di-row.proven { border-color: var(--on); }
  .di-row.fault  { border-color: var(--err); }
  .di-dot { width: 8px; height: 8px; border-radius: 50%; flex-shrink: 0; }
  .di-dot.proven { background: var(--on); box-shadow: 0 0 4px var(--on); }
  .di-dot.fault  { background: var(--err); box-shadow: 0 0 4px var(--err); }
  .di-label { flex: 1; font-size: 11px; }
  .di-port { font-weight: 600; color: var(--text); }
  .di-eq { color: var(--text2); font-size: 10px; }
  .di-btn {
    padding: 3px 8px; font-size: 10px; font-weight: 600; border: 1px solid var(--border);
    border-radius: 3px; cursor: pointer; text-transform: uppercase; letter-spacing: 0.5px;
    transition: all 0.15s; background: var(--bg);
  }
  .di-btn.proven { color: var(--on); }
  .di-btn.fault  { color: var(--err); }
  .di-btn:hover { background: var(--bg3); }
  .di-btn.active { font-weight: 700; }
  .di-btn.active.proven { background: var(--on); color: #000; border-color: var(--on); }
  .di-btn.active.fault  { background: var(--err); color: #fff; border-color: var(--err); }
  .di-inv-btn {
    padding: 2px 5px; font-size: 9px; font-weight: 600; border: 1px solid var(--border);
    border-radius: 3px; cursor: pointer; background: var(--bg); color: var(--text2);
    letter-spacing: 0.3px; transition: all 0.15s; line-height: 1;
  }
  .di-inv-btn:hover { background: var(--bg3); }
  .di-inv-btn.active { background: var(--accent2); color: #000; border-color: var(--accent2); }
  .di-not-installed { opacity: 0.4; pointer-events: none; }
  .di-cpld-status { display: flex; align-items: center; gap: 8px; margin-bottom: 8px; font-size: 11px; }
  .di-cpld-dot { width: 8px; height: 8px; border-radius: 50%; }
  .di-cpld-dot.ok { background: var(--on); }
  .di-cpld-dot.err { background: var(--err); }
  .di-output-row { display: flex; gap: 8px; font-size: 10px; color: var(--text2); padding: 4px 10px; }
  .di-output-hex { font-family: 'JetBrains Mono', monospace; color: var(--accent2); }

  /* ── Analog Board Cards ── */
  .board-card { background: var(--bg3); border-radius: 8px; padding: 14px; margin-bottom: 10px; border: 1px solid var(--border); }
  .board-card-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px; }
  .board-card-header .bc-title { font-weight: 600; font-size: 14px; color: var(--accent); }
  .board-card-header .bc-type { font-size: 11px; padding: 2px 8px; border-radius: 4px; background: var(--bg); color: var(--text2); }
  .board-card-header .bc-remove { cursor: pointer; font-size: 11px; padding: 2px 8px; border-radius: 4px; background: var(--err); color: #fff; border: none; font-weight: 600; }
  .board-card-header .bc-remove:hover { opacity: 0.8; }
  .board-sensors { display: grid; grid-template-columns: 1fr 1fr; gap: 6px; }
  .bs-item { background: var(--bg); border-radius: 6px; padding: 8px 10px; }
  .bs-item .bs-label { font-size: 10px; color: var(--text2); text-transform: uppercase; letter-spacing: 0.5px; }
  .bs-item .bs-val { font-size: 18px; font-weight: 700; font-variant-numeric: tabular-nums; color: var(--accent); margin: 2px 0; }
  .bs-item .bs-val.humid { color: var(--accent2); }
  .bs-item .bs-val.co2 { color: var(--warn); }
  .bs-item input[type=range] { width: 100%; height: 4px; -webkit-appearance: none; background: var(--border); border-radius: 2px; cursor: pointer; margin-top: 4px; }
  .bs-item input[type=range]::-webkit-slider-thumb { -webkit-appearance: none; width: 12px; height: 12px; border-radius: 50%; background: var(--accent); cursor: pointer; }
  .board-type-select { padding: 3px 6px; background: var(--bg); border: 1px solid var(--border); border-radius: 4px; color: var(--text); font-size: 12px; }
  .board-physics-note { font-size: 10px; color: var(--warn); font-style: italic; margin-top: 4px; }

  /* ── VFD Drive Cards ── */
  .vfd-card { background: var(--bg3); border-radius: 8px; padding: 14px; margin-bottom: 12px; border: 2px solid var(--border); transition: border-color 0.3s; }
  .vfd-card.vfd-running { border-color: var(--ok); }
  .vfd-card.vfd-fault { border-color: var(--err); }
  .vfd-card.vfd-stopped { border-color: var(--text2); }
  .vfd-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px; }
  .vfd-label { font-weight: 700; font-size: 15px; color: var(--accent); }
  .vfd-status { font-size: 12px; font-weight: 600; padding: 3px 10px; border-radius: 12px; }
  .vfd-running .vfd-status { background: var(--ok); color: #fff; }
  .vfd-fault .vfd-status { background: var(--err); color: #fff; }
  .vfd-stopped .vfd-status { background: var(--text2); color: #fff; }
  .vfd-remove { cursor: pointer; font-size: 18px; background: none; border: none; color: var(--text2); padding: 0 4px; }
  .vfd-remove:hover { color: var(--err); }
  .vfd-metrics { display: grid; grid-template-columns: repeat(5, 1fr); gap: 8px; margin-bottom: 10px; }
  .vfd-metric { background: var(--bg); border-radius: 6px; padding: 8px; text-align: center; }
  .vfd-val { font-size: 16px; font-weight: 700; font-variant-numeric: tabular-nums; color: var(--text); display: block; }
  .vfd-lbl { font-size: 10px; color: var(--text2); text-transform: uppercase; letter-spacing: 0.5px; }
  .vfd-controls { border-top: 1px solid var(--border); padding-top: 10px; }
  .vfd-controls label { display: flex; align-items: center; gap: 8px; font-size: 13px; color: var(--text2); }
  .vfd-controls input[type=range] { flex: 1; height: 4px; -webkit-appearance: none; background: var(--border); border-radius: 2px; cursor: pointer; }
  .vfd-controls input[type=range]::-webkit-slider-thumb { -webkit-appearance: none; width: 14px; height: 14px; border-radius: 50%; background: var(--accent); cursor: pointer; }

  @media (max-width: 900px) { main { grid-template-columns: 1fr; } .grid-4 { grid-template-columns: 1fr 1fr; } .physics-status { grid-template-columns: 1fr 1fr; } .switch-panel { grid-template-columns: 1fr 1fr; } .di-grid { grid-template-columns: 1fr; } .vfd-metrics { grid-template-columns: repeat(2, 1fr); } }
</style>
</head>
<body>

<header>
  <h1>\u{1F33E} RS485 Sensor Control Panel</h1>
  <div class="meta">
    <span><span class="status-dot" id="connDot"></span><span id="connLabel">---</span></span>
    <span class="physics-badge off" id="physicsBadge">PHYSICS OFF</span>
    <span class="stat-badge">Q: <span id="statQueries">0</span></span>
    <span class="stat-badge">R: <span id="statResponses">0</span></span>
    <span class="stat-badge" id="uptimeBadge">Up: 0s</span>
  </div>
</header>

<main>
  <div class="tab-bar">
    <button class="tab-btn active" onclick="switchTab('sensors')">\u{1F321}\uFE0F Sensors & Physics</button>
    <button class="tab-btn" onclick="switchTab('equipment')">\u26A1 Equipment & I/O</button>
    <button class="tab-btn" onclick="switchTab('test')">\u{1F9EA} Test</button>
  </div>

  <div id="tab-sensors" class="tab-content active">
  <!-- Live Sensor Readings -->
  <section>
    <h2>Live Sensor Readings</h2>
    <div class="grid grid-4" id="sensorGrid">
      <div class="sensor-card" id="card_plenumTemp1">
        <div class="label">Plenum Temp 1</div>
        <div class="value temp" id="v_plenumTemp1">--\u00b0F</div>
        <div class="sub" id="c_plenumTemp1">--\u00b0C</div>
        <span class="override-badge" id="ovr_plenumTemp">MAN</span>
        <svg class="sparkline" viewBox="0 0 60 20"><polyline id="sp_plenumTemp1" points=""></polyline></svg>
      </div>
      <div class="sensor-card" id="card_plenumTemp2">
        <div class="label">Plenum Temp 2</div>
        <div class="value temp" id="v_plenumTemp2">--\u00b0F</div>
        <div class="sub" id="c_plenumTemp2">--\u00b0C</div>
        <svg class="sparkline" viewBox="0 0 60 20"><polyline id="sp_plenumTemp2" points=""></polyline></svg>
      </div>
      <div class="sensor-card" id="card_outsideTemp">
        <div class="label">Outside Temp</div>
        <div class="value temp" id="v_outsideTemp">--\u00b0F</div>
        <div class="sub" id="c_outsideTemp">--\u00b0C</div>
        <span class="override-badge" id="ovr_outsideTemp">MAN</span>
        <svg class="sparkline" viewBox="0 0 60 20"><polyline id="sp_outsideTemp" points=""></polyline></svg>
      </div>
      <div class="sensor-card" id="card_returnTemp">
        <div class="label">Return Temp</div>
        <div class="value temp" id="v_returnTemp">--\u00b0F</div>
        <div class="sub" id="c_returnTemp">--\u00b0C</div>
        <span class="override-badge" id="ovr_returnTemp">MAN</span>
        <svg class="sparkline" viewBox="0 0 60 20"><polyline id="sp_returnTemp" points=""></polyline></svg>
      </div>
      <div class="sensor-card" id="card_outsideHumid">
        <div class="label">Outside Humid</div>
        <div class="value humid" id="v_outsideHumid">--%</div>
        <div class="sub">&nbsp;</div>
        <span class="override-badge" id="ovr_outsideHumid">MAN</span>
        <svg class="sparkline" viewBox="0 0 60 20"><polyline id="sp_outsideHumid" points="" style="stroke:var(--accent2)"></polyline></svg>
      </div>
      <div class="sensor-card" id="card_plenumHumid">
        <div class="label">Plenum Humid</div>
        <div class="value humid" id="v_plenumHumid">--%</div>
        <div class="sub">&nbsp;</div>
        <span class="override-badge" id="ovr_plenumHumid">MAN</span>
        <svg class="sparkline" viewBox="0 0 60 20"><polyline id="sp_plenumHumid" points="" style="stroke:var(--accent2)"></polyline></svg>
      </div>
      <div class="sensor-card" id="card_returnHumid">
        <div class="label">Return Humid</div>
        <div class="value humid" id="v_returnHumid">--%</div>
        <div class="sub">&nbsp;</div>
        <span class="override-badge" id="ovr_returnHumid">MAN</span>
        <svg class="sparkline" viewBox="0 0 60 20"><polyline id="sp_returnHumid" points="" style="stroke:var(--accent2)"></polyline></svg>
      </div>
      <div class="sensor-card" id="card_co2">
        <div class="label">CO\u2082</div>
        <div class="value co2" id="v_co2">-- ppm</div>
        <div class="sub">&nbsp;</div>
        <span class="override-badge" id="ovr_co2">MAN</span>
        <svg class="sparkline" viewBox="0 0 60 20"><polyline id="sp_co2" points="" style="stroke:var(--warn)"></polyline></svg>
      </div>
    </div>
  </section>

  <!-- Equipment Status -->
  <section>
    <h2>Equipment Status</h2>
    <div style="display:flex;align-items:center;gap:12px;margin-bottom:12px;padding:8px 12px;border-radius:8px;background:var(--card);border:1px solid var(--border)">
      <div style="font-size:12px;color:var(--text2);text-transform:uppercase;letter-spacing:0.5px">Control Mode</div>
      <div id="modeDisplay" style="font-size:16px;font-weight:700;color:var(--text)">--</div>
      <div id="modeBadge" style="font-size:10px;padding:2px 8px;border-radius:4px;font-weight:600;text-transform:uppercase;background:var(--border);color:var(--text2)">--</div>
    </div>
    <div class="grid grid-2" id="equipGrid">
      <div class="eq-row"><div class="dot off" id="eq_fan"></div><div class="name">Fan</div><div class="val" id="eq_fan_val">OFF</div></div>
      <div class="eq-row"><div class="dot off" id="eq_doors"></div><div class="name">Fresh Air Doors</div><div class="val" id="eq_doors_val">CLOSED</div></div>
      <div class="eq-row"><div class="dot off" id="eq_heat"></div><div class="name">Heater</div><div class="val" id="eq_heat_val">OFF</div></div>
      <div class="eq-row"><div class="dot off" id="eq_burner"></div><div class="name">Burner</div><div class="val" id="eq_burner_val">OFF</div></div>
      <div class="eq-row"><div class="dot off" id="eq_refrig"></div><div class="name">Refrigeration</div><div class="val" id="eq_refrig_val">OFF</div></div>
      <div class="eq-row"><div class="dot off" id="eq_climacell"></div><div class="name">Climacell</div><div class="val" id="eq_climacell_val">OFF</div></div>
      <div class="eq-row"><div class="dot off" id="eq_humid"></div><div class="name">Humidifier</div><div class="val" id="eq_humid_val">OFF</div></div>
      <div class="eq-row"><div class="dot off" id="eq_cooling"></div><div class="name">Cooling Mode</div><div class="val" id="eq_cooling_val">OFF</div></div>
    </div>
    <div class="toggle-row" style="margin-top:12px">
      <label style="font-size:12px">Poll Bridge for Equipment State</label>
      <label class="toggle">
        <input type="checkbox" id="phys_equipmentPolling" onchange="savePhysics()">
        <span class="slider"></span>
      </label>
    </div>
    <div class="section-note" style="margin-top:8px" id="eqSourceNote">Enable polling for a full feedback loop: Physics \u2192 QEMU \u2192 Equipment \u2192 Bridge \u2192 Physics.</div>
  </section>

  <!-- Physics Engine -->
  <section>
    <h2>\u{1F52C} Physics Engine</h2>
    <div class="toggle-row">
      <label style="font-weight:600">Enable Physics Simulation</label>
      <label class="toggle">
        <input type="checkbox" id="phys_enabled" onchange="savePhysics()" checked>
        <span class="slider"></span>
      </label>
    </div>
    <div class="section-note">When enabled, sensors evolve realistically. Manual slider changes create temporary overrides (release them to resume physics).</div>

    <div class="physics-status" id="physicsStatusGrid">
      <div class="phys-stat"><div class="phys-label">Diurnal Phase</div><div class="phys-val" id="phys_phase">--</div></div>
      <div class="phys-stat"><div class="phys-label">Heat Flow</div><div class="phys-val" id="phys_heatflow">--</div></div>
      <div class="phys-stat"><div class="phys-label">CO\u2082 Trend</div><div class="phys-val" id="phys_co2trend">--</div></div>
    </div>

    <h3>\u{1F954} Potato Respiration</h3>
    <div class="potato-visual">
      <div class="potato-pile" id="potatoPile">\u{1F954}</div>
      <div class="potato-info">
        <div class="mass-label">Stored Product Mass</div>
        <div class="mass-val" id="massDisplay">1.0\u00d7</div>
        <div class="mass-desc" id="massDesc">Normal load \u2014 moderate heat + CO\u2082 + moisture</div>
      </div>
    </div>
    <div class="control-row">
      <span class="ctrl-label">Potato Mass</span>
      <input type="range" class="physics" min="0" max="3" step="0.1" id="phys_potatoMass" value="1" oninput="onPhysicsSlider('potatoMass')">
      <input type="number" step="0.1" min="0" max="3" id="phys_potatoMass_num" value="1" style="width:60px" onchange="onPhysicsNumber('potatoMass')">
      <span class="unit">\u00d7</span>
    </div>
    <div class="section-note">
      Potatoes generate metabolic heat (~0.15 mW/kg), release CO\u2082 through respiration (3\u20136 mg/kg/hr),
      and emit moisture. Higher mass = more warming, CO\u2082 buildup, and humidity rise. Respiration rates
      increase with temperature \u2014 a positive feedback loop.
    </div>

    <h3>\u{1F305} Diurnal Temperature Cycle</h3>
    <div class="grid grid-3" style="margin-top:4px">
      <div class="field"><label>Night Low (\u00b0C)</label><input type="number" step="0.5" id="phys_outsideTempMin" onchange="savePhysics()"></div>
      <div class="field"><label>Day High (\u00b0C)</label><input type="number" step="0.5" id="phys_outsideTempMax" onchange="savePhysics()"></div>
      <div class="field"><label>Cycle Period (sec)</label><input type="number" step="10" min="10" id="phys_diurnalPeriodSec" onchange="savePhysics()"></div>
    </div>
    <div class="section-note">Outside temp follows a sinusoidal day/night arc. Peak at 3pm, trough at 3am. 86400 = real 24hr, 120 = 2-min demo.</div>

    <h3>\u{1F3D7}\uFE0F Building & Insulation</h3>
    <div class="control-row">
      <span class="ctrl-label">Insulation</span>
      <input type="range" class="physics" min="0.0001" max="0.01" step="0.0001" id="phys_insulationFactor" oninput="onPhysicsSlider('insulationFactor')">
      <input type="number" step="0.0001" id="phys_insulationFactor_num" style="width:80px" onchange="onPhysicsNumber('insulationFactor')">
      <span class="unit">rate</span>
    </div>
    <div class="control-row">
      <span class="ctrl-label">Return Offset</span>
      <input type="range" class="physics" min="-2" max="8" step="0.1" id="phys_returnTempOffset" oninput="onPhysicsSlider('returnTempOffset')">
      <input type="number" step="0.1" id="phys_returnTempOffset_num" style="width:80px" onchange="onPhysicsNumber('returnTempOffset')">
      <span class="unit">\u00b0C</span>
    </div>
    <div class="section-note">Insulation: how fast plenum drifts toward outside temp through the building walls. Return offset: air warms as it passes through the potato pile.</div>

    <h3>\u{1F4A7} Humidity Physics</h3>
    <div class="control-row">
      <span class="ctrl-label">Temp\u2192Humid</span>
      <input type="range" class="physics" min="0" max="5" step="0.1" id="phys_tempHumidCoeff" oninput="onPhysicsSlider('tempHumidCoeff')">
      <input type="number" step="0.1" id="phys_tempHumidCoeff_num" style="width:60px" onchange="onPhysicsNumber('tempHumidCoeff')">
      <span class="unit">%/\u00b0C</span>
    </div>
    <div class="control-row">
      <span class="ctrl-label">Outside RH Base</span>
      <input type="range" class="physics" min="10" max="100" step="1" id="phys_outsideHumidBase" oninput="onPhysicsSlider('outsideHumidBase')">
      <input type="number" step="1" id="phys_outsideHumidBase_num" style="width:60px" onchange="onPhysicsNumber('outsideHumidBase')">
      <span class="unit">%</span>
    </div>
    <div class="section-note">Psychrometric coupling: warmer air \u2192 lower relative humidity because warm air holds more moisture, lowering RH even if absolute moisture is constant.</div>

    <h3>\u{1FAB7} CO\u2082 Dynamics</h3>
    <div class="grid grid-2">
      <div class="field"><label>Ambient CO\u2082 (ppm)</label><input type="number" step="10" id="phys_co2AmbientPpm" onchange="savePhysics()"></div>
      <div class="field"><label>Leak Rate</label><input type="number" step="0.0005" id="phys_co2LeakRate" onchange="savePhysics()"></div>
    </div>
    <div class="section-note">CO\u2082 builds from potato respiration (rate increases with temperature) and naturally leaks toward ambient. Fans accelerate purging.</div>
  </section>

  <!-- Manual Sensor Controls -->
  <section>
    <h2>Manual Sensor Controls</h2>
    <div class="section-note">Drag a slider to override physics for that sensor. Click "Release Overrides" to let physics resume.</div>

    <h3>\u{1F321}\uFE0F Temperature Controls</h3>
    <div class="control-row">
      <span class="ctrl-label">Plenum 1</span>
      <input type="range" min="-40" max="83" step="0.5" id="sl_plenumTemp1" oninput="onSlider('plenumTemp1','plenumTemp')">
      <input type="number" step="0.5" id="num_plenumTemp1" onchange="onNumber('plenumTemp1','plenumTemp')">
      <span class="unit">\u00b0C</span>
    </div>
    <div class="control-row">
      <span class="ctrl-label">Plenum 2</span>
      <input type="range" min="-40" max="83" step="0.5" id="sl_plenumTemp2" oninput="onSlider('plenumTemp2','plenumTemp')">
      <input type="number" step="0.5" id="num_plenumTemp2" onchange="onNumber('plenumTemp2','plenumTemp')">
      <span class="unit">\u00b0C</span>
    </div>
    <div class="control-row">
      <span class="ctrl-label">Outside</span>
      <input type="range" min="-40" max="83" step="0.5" id="sl_outsideTemp" oninput="onSlider('outsideTemp','outsideTemp')">
      <input type="number" step="0.5" id="num_outsideTemp" onchange="onNumber('outsideTemp','outsideTemp')">
      <span class="unit">\u00b0C</span>
    </div>
    <div class="control-row">
      <span class="ctrl-label">Return</span>
      <input type="range" min="-40" max="83" step="0.5" id="sl_returnTemp" oninput="onSlider('returnTemp','returnTemp')">
      <input type="number" step="0.5" id="num_returnTemp" onchange="onNumber('returnTemp','returnTemp')">
      <span class="unit">\u00b0C</span>
    </div>

    <h3>\u{1F4A7} Humidity & CO\u2082</h3>
    <div class="control-row">
      <span class="ctrl-label">Outside Humid</span>
      <input type="range" class="humid" min="0" max="100" step="1" id="sl_outsideHumid" oninput="onSlider('outsideHumid','outsideHumid')">
      <input type="number" step="1" min="0" max="100" id="num_outsideHumid" onchange="onNumber('outsideHumid','outsideHumid')">
      <span class="unit">%</span>
    </div>
    <div class="control-row">
      <span class="ctrl-label">Plenum Humid</span>
      <input type="range" class="humid" min="0" max="100" step="1" id="sl_plenumHumid" oninput="onSlider('plenumHumid','plenumHumid')">
      <input type="number" step="1" min="0" max="100" id="num_plenumHumid" onchange="onNumber('plenumHumid','plenumHumid')">
      <span class="unit">%</span>
    </div>
    <div class="control-row">
      <span class="ctrl-label">Return Humid</span>
      <input type="range" class="humid" min="0" max="100" step="1" id="sl_returnHumid" oninput="onSlider('returnHumid','returnHumid')">
      <input type="number" step="1" min="0" max="100" id="num_returnHumid" onchange="onNumber('returnHumid','returnHumid')">
      <span class="unit">%</span>
    </div>
    <div class="control-row">
      <span class="ctrl-label">CO\u2082</span>
      <input type="range" class="co2" min="0" max="10000" step="10" id="sl_co2" oninput="onSlider('co2','co2')">
      <input type="number" step="10" min="0" max="10000" id="num_co2" onchange="onNumber('co2','co2')">
      <span class="unit">ppm</span>
    </div>

    <div class="btn-row" style="margin-top:12px">
      <button class="warn" onclick="releaseOverrides()">Release All Overrides</button>
    </div>
  </section>

  <!-- Analog Boards -->
  <section class="full">
    <h2>\u{1F4CB} Analog Boards</h2>
    <div class="section-note">Configure simulated analog boards on the RS485 bus. Boards 0-1 (Temp/Humid) are driven by the physics engine. Additional boards (pile temp/humid) have individually settable sensor values.</div>
    <div id="boardsContainer"></div>
    <div class="btn-row" style="margin-top:12px">
      <button class="primary" onclick="addBoard()">+ Add Board</button>
    </div>
  </section>

  <!-- VFD Drives (Modbus TCP) -->
  <section class="full">
    <h2>\u{2699}\uFE0F VFD Drives (Modbus TCP)</h2>
    <div class="section-note">ABB ACS310 variable frequency drive simulator. Modbus TCP server on port 5020. Connect from RPi5 using modbus-serial to read status / write control commands.</div>
    <div id="vfdContainer"></div>
    <div class="btn-row" style="margin-top:12px">
      <button class="primary" onclick="addVFD()">+ Add Drive</button>
    </div>
  </section>

  <!-- Presets -->
  <section class="full">
    <h2>Presets & Actions</h2>
    <div class="section-note">Presets set both sensor starting values and physics parameters for realistic scenarios.</div>
    <div class="btn-row">
      <button class="preset-btn" onclick="applyPreset('cold-night')">\u2744\uFE0F Cold Night</button>
      <button class="preset-btn" onclick="applyPreset('mild-day')">\u{1F324}\uFE0F Mild Day</button>
      <button class="preset-btn" onclick="applyPreset('warm-day')">\u2600\uFE0F Warm Day</button>
      <button class="preset-btn" onclick="applyPreset('hot-humid')">\u{1F975} Hot & Humid</button>
      <button class="preset-btn" onclick="applyPreset('freezing')">\u{1F9CA} Freezing</button>
      <button class="preset-btn" onclick="applyPreset('potato-storage')">\u{1F954} Potato Storage</button>
      <span style="flex:1"></span>
      <button onclick="resetAll()">Reset All</button>
    </div>
  </section>
  </div><!-- end tab-sensors -->

  <div id="tab-equipment" class="tab-content">
  <!-- Dynamic Equipment Outputs -->
  <section class="full">
    <h2>\u{1F50C} Dynamic Equipment Outputs</h2>
    <div class="section-note">
      Decoded from CPLD shift-register output words using the IO configuration.
      Shows which physical output ports the firmware is actually commanding ON/OFF in real time.
      This is the ground truth \u2014 directly from the ARM firmware via QEMU CPLD emulation.
    </div>
    <div style="display:flex; align-items:center; gap:12px; margin-bottom:10px">
      <span class="dyn-out-badge waiting" id="dynOutBadge">Waiting for IO Config\u2026</span>
      <span style="font-size:11px;color:var(--text2)" id="dynOutCount"></span>
    </div>
    <div class="dyn-out-grid" id="dynOutGrid">
      <div style="color:var(--text2);font-size:13px;padding:8px">Loading IO configuration from bridge\u2026</div>
    </div>
  </section>

  <!-- CPLD Switch Panel -->
  <section class="full">
    <h2>\u{1F39B}\uFE0F CPLD Switch Panel</h2>
    <div class="section-note">Physical switches on the CPLD board (SerialShift.h). These control equipment modes in the firmware.
      Each 3-position switch has OFF (center), AUTO, and MANUAL positions. The firmware reads these via SPI shift registers.</div>

    <div class="switch-panel" id="switchPanel">

      <!-- Start/Stop — spans full width -->
      <div class="switch-group start-stop" id="swg_startStop">
        <div class="sw-label">\u26A1 Start / Stop</div>
        <div class="sw-eq-id">SW_START_STOP (EQ 43) \u2022 SS_MAIN_SW0</div>
        <div class="sw-indicator pos-shutdown" id="swi_startStop">SHUTDOWN</div>
        <div class="sw-toggle sw-toggle-2">
          <input type="radio" name="sw_startStop" id="sw_startStop_shutdown" value="shutdown" checked>
          <label for="sw_startStop_shutdown" class="shutdown-label" onclick="onSwitch('startStop','shutdown')">Shutdown</label>
          <input type="radio" name="sw_startStop" id="sw_startStop_start" value="start">
          <label for="sw_startStop_start" class="start-label" onclick="onSwitch('startStop','start')">Start</label>
        </div>
      </div>

      <!-- Fan — 3 position -->
      <div class="switch-group" id="swg_fan">
        <div class="sw-label">\u{1F4A8} Fan</div>
        <div class="sw-eq-id">SW_FAN_AUTO/MANUAL (EQ 44\u201345)</div>
        <div class="sw-indicator pos-off" id="swi_fan">OFF</div>
        <div class="sw-toggle">
          <input type="radio" name="sw_fan" id="sw_fan_manual" value="manual">
          <label for="sw_fan_manual" class="manual-label" onclick="onSwitch('fan','manual')">Manual</label>
          <input type="radio" name="sw_fan" id="sw_fan_off" value="off" checked>
          <label for="sw_fan_off" class="off-label" onclick="onSwitch('fan','off')">Off</label>
          <input type="radio" name="sw_fan" id="sw_fan_auto" value="auto">
          <label for="sw_fan_auto" class="auto-label" onclick="onSwitch('fan','auto')">Auto</label>
        </div>
      </div>

      <!-- Fresh Air / Door — 3 position -->
      <div class="switch-group" id="swg_freshAir">
        <div class="sw-label">\u{1F6AA} Fresh Air (Door)</div>
        <div class="sw-eq-id">SW_FRESHAIR_AUTO/MANUAL (EQ 46\u201347)</div>
        <div class="sw-indicator pos-off" id="swi_freshAir">OFF</div>
        <div class="sw-toggle">
          <input type="radio" name="sw_freshAir" id="sw_freshAir_manual" value="manual">
          <label for="sw_freshAir_manual" class="manual-label" onclick="onSwitch('freshAir','manual')">Manual</label>
          <input type="radio" name="sw_freshAir" id="sw_freshAir_off" value="off" checked>
          <label for="sw_freshAir_off" class="off-label" onclick="onSwitch('freshAir','off')">Off</label>
          <input type="radio" name="sw_freshAir" id="sw_freshAir_auto" value="auto">
          <label for="sw_freshAir_auto" class="auto-label" onclick="onSwitch('freshAir','auto')">Auto</label>
        </div>
      </div>

      <!-- Climacell — 3 position -->
      <div class="switch-group" id="swg_climacell">
        <div class="sw-label">\u2744\uFE0F Climacell</div>
        <div class="sw-eq-id">SW_CLIMACELL_AUTO/MANUAL (EQ 48\u201349)</div>
        <div class="sw-indicator pos-off" id="swi_climacell">OFF</div>
        <div class="sw-toggle">
          <input type="radio" name="sw_climacell" id="sw_climacell_manual" value="manual">
          <label for="sw_climacell_manual" class="manual-label" onclick="onSwitch('climacell','manual')">Manual</label>
          <input type="radio" name="sw_climacell" id="sw_climacell_off" value="off" checked>
          <label for="sw_climacell_off" class="off-label" onclick="onSwitch('climacell','off')">Off</label>
          <input type="radio" name="sw_climacell" id="sw_climacell_auto" value="auto">
          <label for="sw_climacell_auto" class="auto-label" onclick="onSwitch('climacell','auto')">Auto</label>
        </div>
      </div>

      <!-- Humidifier — 3 position -->
      <div class="switch-group" id="swg_humid">
        <div class="sw-label">\u{1F4A7} Humidifier</div>
        <div class="sw-eq-id">SW_HUMID_AUTO/MANUAL (EQ 50\u201351)</div>
        <div class="sw-indicator pos-off" id="swi_humid">OFF</div>
        <div class="sw-toggle">
          <input type="radio" name="sw_humid" id="sw_humid_manual" value="manual">
          <label for="sw_humid_manual" class="manual-label" onclick="onSwitch('humid','manual')">Manual</label>
          <input type="radio" name="sw_humid" id="sw_humid_off" value="off" checked>
          <label for="sw_humid_off" class="off-label" onclick="onSwitch('humid','off')">Off</label>
          <input type="radio" name="sw_humid" id="sw_humid_auto" value="auto">
          <label for="sw_humid_auto" class="auto-label" onclick="onSwitch('humid','auto')">Auto</label>
        </div>
      </div>

      <!-- Refrigeration — 2 position -->
      <div class="switch-group" id="swg_refrig">
        <div class="sw-label">\u{1F9CA} Refrigeration</div>
        <div class="sw-eq-id">SW_REFRIG_AUTO (EQ 52)</div>
        <div class="sw-indicator pos-off" id="swi_refrig">OFF</div>
        <div class="sw-toggle sw-toggle-2">
          <input type="radio" name="sw_refrig" id="sw_refrig_off" value="off" checked>
          <label for="sw_refrig_off" class="off-label" onclick="onSwitch('refrig','off')">Off</label>
          <input type="radio" name="sw_refrig" id="sw_refrig_auto" value="auto">
          <label for="sw_refrig_auto" class="auto-label" onclick="onSwitch('refrig','auto')">Auto</label>
        </div>
      </div>

    </div>
  </section>

  <!-- Digital Input Proof Panel -->
  <section class="full">
    <h2>\u{1F50C} Digital Input Proof Signals</h2>
    <div class="section-note">Simulate field wiring: flow switches, current relays, VFD fault relays.
      Closed = contact made = equipment proven. Open = contact broken = proof fault \u2192 firmware alarm.
      Injected directly into QEMU via CPLD shift register emulation.</div>
    <div class="di-cpld-status">
      <span class="di-cpld-dot" id="cpldDot"></span>
      <span id="cpldLabel">CPLD: ---</span>
    </div>

    <!-- Main Board (10 inputs) -->
    <div class="di-board" id="di_main">
      <div class="di-board-header">
        <span>\u{1F4DF} Main Board \u2014 Digital Inputs</span>
      </div>
      <div class="di-grid" id="di_main_grid"></div>
    </div>

    <!-- Expansion Board 1 (8 inputs) -->
    <div class="di-board" id="di_ex1">
      <div class="di-board-header">
        <span>\u{1F9E9} Expansion 1</span>
        <div class="di-install-toggle">
          <label>Installed</label>
          <label class="toggle"><input type="checkbox" id="di_ex1_install" onchange="onExInstall('ex1', this.checked)"><span class="slider"></span></label>
        </div>
      </div>
      <div class="di-grid" id="di_ex1_grid"></div>
    </div>

    <!-- Expansion Board 2 (8 inputs) -->
    <div class="di-board" id="di_ex2">
      <div class="di-board-header">
        <span>\u{1F9E9} Expansion 2</span>
        <div class="di-install-toggle">
          <label>Installed</label>
          <label class="toggle"><input type="checkbox" id="di_ex2_install" onchange="onExInstall('ex2', this.checked)"><span class="slider"></span></label>
        </div>
      </div>
      <div class="di-grid" id="di_ex2_grid"></div>
    </div>

    <!-- CPLD Output Readback -->
    <div class="di-output-row">
      <span>CPLD Outputs:</span>
      <span>Main: <span class="di-output-hex" id="cpldOutMain">0x00000000</span></span>
      <span>Ex1: <span class="di-output-hex" id="cpldOutEx1">0x00000000</span></span>
      <span>Ex2: <span class="di-output-hex" id="cpldOutEx2">0x00000000</span></span>
    </div>
  </section>
  </div><!-- end tab-equipment -->

  <div id="tab-test" class="tab-content">
  <section class="full">
    <h2>\u{1F9EA} Functional Test Lab</h2>
    <div class="section-note">Run automated tests against the live firmware. Tests manipulate sensor values and digital inputs, then observe mode changes, equipment output, and alarm responses via the bridge API. Each test saves and restores sensor state.</div>

    <!-- Status bar -->
    <div style="display:flex;align-items:center;gap:12px;margin-bottom:12px;padding:8px 12px;background:var(--bg3);border-radius:6px;">
      <span style="font-size:12px;color:var(--text2);">Bridge:</span>
      <span id="testBridgeStatus" style="font-size:12px;color:var(--err);">\u2716 Not checked</span>
      <span style="margin-left:auto;font-size:12px;color:var(--text2);">Setpoint:</span>
      <span id="testSetpoint" style="font-size:13px;font-weight:700;color:var(--accent);">--</span>
      <span style="font-size:12px;color:var(--text2);">Mode:</span>
      <span id="testMode" style="font-size:13px;font-weight:700;color:var(--on);">--</span>
      <span style="font-size:12px;color:var(--text2);">Plenum:</span>
      <span id="testPlenum" style="font-size:13px;font-weight:700;color:var(--accent);">--</span>
    </div>

    <!-- Controls -->
    <div style="display:flex;gap:8px;margin-bottom:16px;align-items:center;">
      <button class="primary" onclick="runAllTests()">\u25B6 Run All Tests</button>
      <button onclick="stopTests()">\u23F9 Stop</button>
      <button onclick="clearTestLog()">\u{1F5D1} Clear Log</button>
      <label style="margin-left:12px;font-size:12px;color:var(--text2);display:flex;align-items:center;gap:4px;cursor:pointer;">
        <input type="checkbox" id="skipDefrost"> Skip Defrost (saves ~60 min)
      </label>
      <span id="testProgress" style="margin-left:auto;font-size:12px;color:var(--text2);align-self:center;"></span>
    </div>

    <!-- Test cards grid -->
    <div id="testCards" style="display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-bottom:16px;"></div>
  </section>

  <section class="full">
    <h2>\u{1F4CB} Test Results Log</h2>
    <div id="testLog" style="font-family:'JetBrains Mono',monospace;font-size:12px;line-height:1.7;max-height:500px;overflow-y:auto;padding:12px;background:var(--bg);border-radius:6px;border:1px solid var(--border);">
      <div style="color:var(--text2);">No tests run yet. Click a test or "Run All" to begin.</div>
    </div>
  </section>
  </div><!-- end tab-test -->
</main>

<div class="toast" id="toast"></div>

<script>
const API = '';
const TEMP_SENSORS = ['plenumTemp1','plenumTemp2','outsideTemp','returnTemp'];
const HUMID_SENSORS = ['outsideHumid','plenumHumid','returnHumid'];
const CO2_SENSORS = ['co2'];
const ALL_SENSORS = [...TEMP_SENSORS, ...HUMID_SENSORS, ...CO2_SENSORS];

// ── Tab switching ──
function switchTab(tabId) {
  document.querySelectorAll('.tab-content').forEach(el => el.classList.remove('active'));
  document.querySelectorAll('.tab-btn').forEach(el => el.classList.remove('active'));
  const panel = document.getElementById('tab-' + tabId);
  if (panel) panel.classList.add('active');
  // find the clicked button by matching tabId in onclick
  document.querySelectorAll('.tab-btn').forEach(btn => {
    if (btn.getAttribute('onclick') && btn.getAttribute('onclick').includes(tabId)) {
      btn.classList.add('active');
    }
  });
}

const HISTORY_LEN = 40;
const history = {};
ALL_SENSORS.forEach(s => history[s] = []);

let lastQueryCount = 0, lastRespCount = 0, lastRateTime = Date.now();
const startTime = Date.now();
let userEditing = null, editTimeout = null;
let configLoaded = false;

function showToast(msg) {
  const t = document.getElementById('toast');
  t.textContent = msg; t.classList.add('show');
  setTimeout(() => t.classList.remove('show'), 2000);
}

function cToF(c) { return ((c * 9) / 5 + 32).toFixed(1); }

// ── Manual sensor slider handlers ──
function onSlider(sensor, overrideKey) {
  const sl = document.getElementById('sl_' + sensor);
  const num = document.getElementById('num_' + sensor);
  num.value = sl.value;
  markEditing(sensor);
  sendSensorUpdate(sensor, parseFloat(sl.value), overrideKey);
}
function onNumber(sensor, overrideKey) {
  const sl = document.getElementById('sl_' + sensor);
  const num = document.getElementById('num_' + sensor);
  sl.value = num.value;
  markEditing(sensor);
  sendSensorUpdate(sensor, parseFloat(num.value), overrideKey);
}
function markEditing(sensor) {
  userEditing = sensor;
  if (editTimeout) clearTimeout(editTimeout);
  editTimeout = setTimeout(() => { userEditing = null; }, 3000);
}
async function sendSensorUpdate(sensor, value, overrideKey) {
  try {
    await fetch(API + '/api/sensors', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ [sensor]: value, _override: overrideKey }),
    });
  } catch(e) {}
}

// ── Physics parameter handlers ──
function onPhysicsSlider(param) {
  const sl = document.getElementById('phys_' + param);
  const num = document.getElementById('phys_' + param + '_num');
  num.value = sl.value;
  updatePotatoVisual(param);
  savePhysics();
}
function onPhysicsNumber(param) {
  const sl = document.getElementById('phys_' + param);
  const num = document.getElementById('phys_' + param + '_num');
  sl.value = num.value;
  updatePotatoVisual(param);
  savePhysics();
}

function updatePotatoVisual(param) {
  if (param !== 'potatoMass') return;
  const mass = parseFloat(document.getElementById('phys_potatoMass').value) || 0;
  const pile = document.getElementById('potatoPile');
  const disp = document.getElementById('massDisplay');
  const desc = document.getElementById('massDesc');
  disp.textContent = mass.toFixed(1) + '\\u00d7';
  if (mass < 0.1) { pile.textContent = '\\u2205'; desc.textContent = 'Empty \\u2014 no respiration effects'; }
  else if (mass < 0.5) { pile.textContent = '\\ud83e\\udd54'; desc.textContent = 'Light load \\u2014 minimal heat + CO\\u2082'; }
  else if (mass < 1.5) { pile.textContent = '\\ud83e\\udd54\\ud83e\\udd54'; desc.textContent = 'Normal load \\u2014 moderate heat + CO\\u2082 + moisture'; }
  else if (mass < 2.5) { pile.textContent = '\\ud83e\\udd54\\ud83e\\udd54\\ud83e\\udd54'; desc.textContent = 'Heavy load \\u2014 significant warming + high CO\\u2082'; }
  else { pile.textContent = '\\ud83e\\udd54\\ud83e\\udd54\\ud83e\\udd54\\ud83e\\udd54'; desc.textContent = 'Maximum \\u2014 intense respiration, rapid CO\\u2082 buildup'; }
}

async function savePhysics() {
  const cfg = {
    enabled: document.getElementById('phys_enabled').checked,
    outsideTempMin: parseFloat(document.getElementById('phys_outsideTempMin').value),
    outsideTempMax: parseFloat(document.getElementById('phys_outsideTempMax').value),
    diurnalPeriodSec: parseInt(document.getElementById('phys_diurnalPeriodSec').value) || 120,
    potatoMass: parseFloat(document.getElementById('phys_potatoMass').value),
    insulationFactor: parseFloat(document.getElementById('phys_insulationFactor').value),
    returnTempOffset: parseFloat(document.getElementById('phys_returnTempOffset').value),
    tempHumidCoeff: parseFloat(document.getElementById('phys_tempHumidCoeff').value),
    outsideHumidBase: parseFloat(document.getElementById('phys_outsideHumidBase').value),
    co2AmbientPpm: parseFloat(document.getElementById('phys_co2AmbientPpm').value),
    co2LeakRate: parseFloat(document.getElementById('phys_co2LeakRate').value),
    equipmentPolling: document.getElementById('phys_equipmentPolling').checked,
  };
  try {
    await fetch(API + '/api/physics', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(cfg),
    });
  } catch(e) { showToast('Error saving physics'); }
}

async function releaseOverrides() {
  try {
    await fetch(API + '/api/sensors', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ _releaseAll: true }),
    });
    showToast('Overrides released \\u2014 physics resumed');
  } catch(e) { showToast('Error releasing overrides'); }
}

// ── CPLD Switch handlers ──
const SWITCH_NAMES = ['startStop', 'fan', 'freshAir', 'climacell', 'humid', 'refrig'];

async function onSwitch(name, position) {
  try {
    await fetch(API + '/api/switches', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ [name]: position }),
    });
    showToast(name + ' \\u2192 ' + position.toUpperCase());
  } catch(e) { showToast('Error setting switch'); }
}

function updateSwitchDisplay(switches) {
  if (!switches) return;
  SWITCH_NAMES.forEach(name => {
    const pos = switches[name];
    if (!pos) return;
    // Update indicator
    const ind = document.getElementById('swi_' + name);
    if (ind) {
      ind.textContent = pos.toUpperCase();
      ind.className = 'sw-indicator pos-' + pos;
    }
    // Update radio button
    const radio = document.getElementById('sw_' + name + '_' + pos);
    if (radio) radio.checked = true;
    // Update group active state
    const grp = document.getElementById('swg_' + name);
    if (grp) {
      const isActive = (name === 'startStop') ? pos === 'start' : pos !== 'off';
      grp.classList.toggle('active', isActive);
    }
  });
}

// ── Digital Input Proof handlers ──
let MAIN_LABELS = ['Power Detect','Remote Standby','Fan Proof','Climacell Proof','Humid 1 Proof',
  '(Unassigned)','Humid 2 Proof','(Unassigned)','Air Flow Switch','Low Temp Switch'];
let EX1_LABELS = ['Heat Proof','(Unassigned)','(Unassigned)','(Unassigned)',
  '(Unassigned)','(Unassigned)','(Unassigned)','(Unassigned)'];
let EX2_LABELS = ['(Unassigned)','(Unassigned)','(Unassigned)','(Unassigned)',
  '(Unassigned)','(Unassigned)','(Unassigned)','(Unassigned)'];

function getLabels(board) {
  if (board === 'main') return MAIN_LABELS;
  if (board === 'ex1') return EX1_LABELS;
  return EX2_LABELS;
}

// Inputs that "close to trigger" (not "close to prove").
// For these, Closed = fault/triggered, Open = normal.
let INVERTED_MAIN = [0, 1]; // Power Detect, Remote Standby (defaults, updated from server)
let INVERTED_EX1 = [];
let INVERTED_EX2 = [];
function getInvertedArr(board) {
  if (board === 'main') return INVERTED_MAIN;
  if (board === 'ex1') return INVERTED_EX1;
  return INVERTED_EX2;
}
function isInverted(board, idx) { return getInvertedArr(board).includes(idx); }

async function onInvertToggle(board, port) {
  try {
    const res = await fetch(API + '/api/digital-io/invert', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ board, port }),
    });
    const data = await res.json();
    if (data.invertedInputs) {
      INVERTED_MAIN = data.invertedInputs.main || [];
      INVERTED_EX1 = data.invertedInputs.ex1 || [];
      INVERTED_EX2 = data.invertedInputs.ex2 || [];
    }
    const nowInv = isInverted(board, port);
    const label = getLabels(board)[port];
    showToast(label + ' \\u2192 ' + (nowInv ? 'Close-to-trigger' : 'Close-to-prove'));
  } catch(e) { showToast('Error toggling inversion'); }
}

function buildDIGrid(containerId, board, labels, count) {
  const grid = document.getElementById(containerId);
  if (!grid) return;
  grid.innerHTML = '';
  for (let i = 0; i < count; i++) {
    const portNum = i + 1;
    const portLabel = (board === 'main' && i >= 8) ?
      (i === 8 ? 'AIRFLOW' : 'LOWTEMP') : 'DI' + portNum;
    const row = document.createElement('div');
    row.className = 'di-row proven';
    row.id = 'di_' + board + '_' + i;
    const inv = isInverted(board, i);
    const openCls = inv ? 'proven' : 'fault';
    const closedCls = inv ? 'fault' : 'proven';
    // Inverted inputs default to Open (normal), others to Closed (proven)
    const defClosed = !inv;
    const dotCls = defClosed ? 'proven' : (inv ? 'proven' : 'fault');
    row.className = 'di-row ' + dotCls;
    row.innerHTML =
      '<span class="di-dot ' + dotCls + '" id="did_' + board + '_' + i + '"></span>' +
      '<div class="di-label"><span class="di-port">' + portLabel + '</span> ' +
      '<span class="di-eq">' + labels[i] + '</span></div>' +
      '<button class="di-inv-btn' + (inv ? ' active' : '') + '" id="diinv_' + board + '_' + i + '" onclick="onInvertToggle(\\'' + board + '\\',' + i + ')" title="Toggle close-to-trigger">INV</button>' +
      '<button class="di-btn ' + openCls + (!defClosed ? ' active' : '') + '" onclick="onDI(\\'' + board + '\\',' + i + ',false)">Open</button>' +
      '<button class="di-btn ' + closedCls + (defClosed ? ' active' : '') + '" onclick="onDI(\\'' + board + '\\',' + i + ',true)">Closed</button>';
    grid.appendChild(row);
  }
}

// Build grids on load (with defaults, then update from IO Config)
buildDIGrid('di_main_grid', 'main', MAIN_LABELS, 10);
buildDIGrid('di_ex1_grid', 'ex1', EX1_LABELS, 8);
buildDIGrid('di_ex2_grid', 'ex2', EX2_LABELS, 8);

// Fetch dynamic labels from bridge IO Config and update labels in-place
async function fetchInputLabels() {
  try {
    const res = await fetch(API + '/api/input-labels');
    if (!res.ok) return;
    const data = await res.json();
    if (!data.loaded) return;
    MAIN_LABELS = data.main;
    EX1_LABELS = data.ex1;
    EX2_LABELS = data.ex2;
    // Update label text in-place (no grid rebuild — avoids flash)
    function updateLabels(board, labels, count) {
      for (let i = 0; i < count; i++) {
        const eq = document.querySelector('#di_' + board + '_' + i + ' .di-eq');
        if (eq) eq.textContent = labels[i];
      }
    }
    updateLabels('main', MAIN_LABELS, 10);
    updateLabels('ex1', EX1_LABELS, 8);
    updateLabels('ex2', EX2_LABELS, 8);
  } catch(e) { /* retry later */ }
}
// Fetch labels after bridge has had time to start
setTimeout(fetchInputLabels, 3000);
setTimeout(fetchInputLabels, 10000);
setInterval(fetchInputLabels, 30000);

async function onDI(board, port, closed) {
  try {
    await fetch(API + '/api/digital-io', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ board, port, closed }),
    });
    const label = getLabels(board)[port];
    const inv = isInverted(board, port);
    const stateText = inv
      ? (closed ? 'CLOSED (triggered)' : 'OPEN (normal)')
      : (closed ? 'CLOSED (proven)' : 'OPEN (fault)');
    showToast(label + ' \\u2192 ' + stateText);
  } catch(e) { showToast('Error setting digital input'); }
}

async function onExInstall(board, installed) {
  try {
    await fetch(API + '/api/digital-io', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ board, install: installed }),
    });
    showToast(board.toUpperCase() + (installed ? ' installed' : ' removed'));
  } catch(e) { showToast('Error toggling expansion board'); }
}

function updateDigitalIO(di, cpldOut, cpldConnected) {
  // CPLD connection status
  const dot = document.getElementById('cpldDot');
  const lbl = document.getElementById('cpldLabel');
  if (dot && lbl) {
    dot.className = 'di-cpld-dot ' + (cpldConnected ? 'ok' : 'err');
    lbl.textContent = 'CPLD: ' + (cpldConnected ? 'Connected (port 9003)' : 'Disconnected');
  }

  if (di) {
    // Main board
    for (let i = 0; i < 10; i++) {
      const closed = di.main[i];
      const inv = isInverted('main', i);
      // For inverted inputs: closed = fault/triggered, open = normal/proven
      const isNormal = inv ? !closed : closed;
      const row = document.getElementById('di_main_' + i);
      const ddot = document.getElementById('did_main_' + i);
      if (row) row.className = 'di-row ' + (isNormal ? 'proven' : 'fault');
      if (ddot) ddot.className = 'di-dot ' + (isNormal ? 'proven' : 'fault');
      // Update buttons: for inverted, Open=proven Closed=fault
      const openCls = inv ? 'proven' : 'fault';
      const closedCls = inv ? 'fault' : 'proven';
      const btns = row ? row.querySelectorAll('.di-btn') : [];
      if (btns.length === 2) {
        btns[0].className = 'di-btn ' + openCls + (!closed ? ' active' : '');
        btns[1].className = 'di-btn ' + closedCls + (closed ? ' active' : '');
      }
      // Update INV button highlight
      const invBtn = document.getElementById('diinv_main_' + i);
      if (invBtn) invBtn.className = 'di-inv-btn' + (inv ? ' active' : '');
    }
    // Expansion boards
    ['ex1', 'ex2'].forEach(board => {
      const installed = di[board + 'Installed'];
      const container = document.getElementById('di_' + board + '_grid');
      if (container) container.className = 'di-grid' + (installed ? '' : ' di-not-installed');
      const cb = document.getElementById('di_' + board + '_install');
      if (cb) cb.checked = installed;
      for (let i = 0; i < 8; i++) {
        const closed = di[board][i];
        const inv = isInverted(board, i);
        const isNormal = inv ? !closed : closed;
        const row = document.getElementById('di_' + board + '_' + i);
        const ddot = document.getElementById('did_' + board + '_' + i);
        if (row) row.className = 'di-row ' + (isNormal ? 'proven' : 'fault');
        if (ddot) ddot.className = 'di-dot ' + (isNormal ? 'proven' : 'fault');
        const openCls = inv ? 'proven' : 'fault';
        const closedCls = inv ? 'fault' : 'proven';
        const btns = row ? row.querySelectorAll('.di-btn') : [];
        if (btns.length === 2) {
          btns[0].className = 'di-btn ' + openCls + (!closed ? ' active' : '');
          btns[1].className = 'di-btn ' + closedCls + (closed ? ' active' : '');
        }
        // Update INV button highlight
        const invBtn = document.getElementById('diinv_' + board + '_' + i);
        if (invBtn) invBtn.className = 'di-inv-btn' + (inv ? ' active' : '');
      }
    });
  }

  // CPLD output readback
  if (cpldOut) {
    document.getElementById('cpldOutMain').textContent = '0x' + (cpldOut.main >>> 0).toString(16).padStart(8, '0');
    document.getElementById('cpldOutEx1').textContent = '0x' + (cpldOut.ex1 >>> 0).toString(16).padStart(8, '0');
    document.getElementById('cpldOutEx2').textContent = '0x' + (cpldOut.ex2 >>> 0).toString(16).padStart(8, '0');
  }
}

// ── Dynamic Equipment Outputs ──
function updateDynamicOutputs(decoded, configLoaded) {
  const badge = document.getElementById('dynOutBadge');
  const countEl = document.getElementById('dynOutCount');
  const grid = document.getElementById('dynOutGrid');
  if (!badge || !grid) return;

  if (!configLoaded) {
    badge.className = 'dyn-out-badge waiting';
    badge.textContent = 'Waiting for IO Config\\u2026';
    countEl.textContent = '';
    return;
  }

  if (!decoded || decoded.length === 0) {
    badge.className = 'dyn-out-badge loaded';
    badge.textContent = 'IO Config Loaded';
    countEl.textContent = 'No outputs assigned';
    grid.innerHTML = '<div style="color:var(--text2);font-size:13px;padding:8px">No equipment outputs configured in IO config</div>';
    return;
  }

  const activeCount = decoded.filter(function(d) { return d.on; }).length;
  var mismatchCount = decoded.filter(function(d) {
    return typeof d.armOn === 'boolean' && d.on !== d.armOn;
  }).length;
  badge.className = 'dyn-out-badge loaded';
  badge.textContent = 'IO Config Active';
  var countText = activeCount + ' of ' + decoded.length + ' outputs ON';
  if (mismatchCount > 0) countText += ' \\u26a0 ' + mismatchCount + ' ARM mismatch';
  countEl.textContent = countText;

  grid.innerHTML = decoded.map(function(d) {
    var hasMismatch = typeof d.armOn === 'boolean' && d.on !== d.armOn;
    var itemCls = d.on ? 'active' : '';
    if (hasMismatch) itemCls += ' mismatch';
    var dotCls = d.on ? 'on' : 'off';
    var stateCls = d.on ? 'on' : 'off';
    var stateText = d.on ? 'ON' : 'OFF';
    var boardLabel = d.board === 'main' ? 'Main' : d.board.toUpperCase();
    // ARM status line (only for equipment where we have the mapping)
    var armHtml = '';
    if (typeof d.armOn === 'boolean') {
      var armCls = hasMismatch ? 'dyn-out-arm mismatch' : 'dyn-out-arm';
      var armText = 'ARM: ' + (d.armOn ? 'ON' : 'OFF');
      if (hasMismatch) armText = '\\u26a0 ' + armText;
      armHtml = '<div class=\"' + armCls + '\">' + armText + '</div>';
    }
    return '<div class=\"dyn-out-item ' + itemCls + '\">' +
      '<span class=\"dyn-out-dot ' + dotCls + '\"></span>' +
      '<div class=\"dyn-out-info\">' +
        '<div class=\"dyn-out-name\">' + d.eqName + '</div>' +
        '<div class=\"dyn-out-port\">' + boardLabel + ' P' + d.port + ' (EQ ' + d.eqIndex + ')</div>' +
      '</div>' +
      '<div class=\"dyn-out-states\">' +
        '<span class=\"dyn-out-state ' + stateCls + '\">CPLD: ' + stateText + '</span>' +
        armHtml +
      '</div>' +
    '</div>';
  }).join('');
}

// ── Sparklines ──
function updateSparkline(sensor, value) {
  const hist = history[sensor];
  hist.push(value);
  if (hist.length > HISTORY_LEN) hist.shift();
  if (hist.length < 2) return;
  const el = document.getElementById('sp_' + sensor);
  if (!el) return;
  const min = Math.min(...hist), max = Math.max(...hist), range = max - min || 1;
  const points = hist.map((v, i) => {
    const x = (i / (HISTORY_LEN - 1)) * 60;
    const y = 18 - ((v - min) / range) * 16;
    return x.toFixed(1) + ',' + y.toFixed(1);
  }).join(' ');
  el.setAttribute('points', points);
}

// ── Display update ──
function updateDisplay(data) {
  const s = data.sensors, stats = data.stats, phys = data.physics;
  const overrides = data.overrides, equip = data.equipment;

  // Connection
  document.getElementById('connDot').className = 'status-dot ' + (stats.connected ? 'ok' : 'err');
  document.getElementById('connLabel').textContent = stats.connected ? 'QEMU Connected' : 'Waiting for QEMU';

  // Physics badge
  const badge = document.getElementById('physicsBadge');
  badge.className = 'physics-badge ' + (phys.enabled ? 'on' : 'off');
  badge.textContent = phys.enabled ? 'PHYSICS ON' : 'PHYSICS OFF';

  document.getElementById('statQueries').textContent = stats.queries;
  document.getElementById('statResponses').textContent = stats.responses;

  // Rates
  const now = Date.now(), elapsed = (now - lastRateTime) / 1000;
  if (elapsed >= 2) {
    lastQueryCount = stats.queries; lastRespCount = stats.responses; lastRateTime = now;
  }

  // Uptime
  const upSec = Math.floor((now - startTime) / 1000), upMin = Math.floor(upSec / 60);
  document.getElementById('uptimeBadge').textContent = upMin > 0 ? 'Up: ' + upMin + 'm ' + (upSec % 60) + 's' : 'Up: ' + upSec + 's';

  // Sensors
  TEMP_SENSORS.forEach(key => {
    document.getElementById('v_' + key).textContent = cToF(s[key]) + '\\u00b0F';
    document.getElementById('c_' + key).textContent = s[key].toFixed(1) + '\\u00b0C';
    if (userEditing !== key) {
      document.getElementById('sl_' + key).value = s[key];
      document.getElementById('num_' + key).value = s[key].toFixed(1);
    }
    updateSparkline(key, s[key]);
  });
  HUMID_SENSORS.forEach(key => {
    document.getElementById('v_' + key).textContent = s[key].toFixed(0) + '%';
    if (userEditing !== key) {
      document.getElementById('sl_' + key).value = s[key];
      document.getElementById('num_' + key).value = s[key].toFixed(0);
    }
    updateSparkline(key, s[key]);
  });
  CO2_SENSORS.forEach(key => {
    document.getElementById('v_' + key).textContent = Math.round(s[key]) + ' ppm';
    if (userEditing !== key) {
      document.getElementById('sl_' + key).value = s[key];
      document.getElementById('num_' + key).value = Math.round(s[key]);
    }
    updateSparkline(key, s[key]);
  });

  // Override badges
  if (overrides) {
    ['outsideTemp','plenumTemp','returnTemp','outsideHumid','plenumHumid','returnHumid','co2'].forEach(k => {
      const el = document.getElementById('ovr_' + k);
      if (el) el.className = 'override-badge' + (overrides[k] ? ' active' : '');
    });
  }

  // Physics status
  if (data.physicsStatus) {
    const ps = data.physicsStatus;
    document.getElementById('phys_phase').textContent = ps.diurnalPhase;
    document.getElementById('phys_phase').style.color = ps.diurnalPhase === 'Day' ? 'var(--warn)' : 'var(--accent)';
    document.getElementById('phys_heatflow').textContent = ps.heatFlow;
    document.getElementById('phys_heatflow').style.color = ps.heatFlowDir > 0 ? 'var(--err)' : ps.heatFlowDir < 0 ? 'var(--accent)' : 'var(--text2)';
    document.getElementById('phys_co2trend').textContent = ps.co2Trend;
    document.getElementById('phys_co2trend').style.color = ps.co2TrendDir > 0 ? 'var(--warn)' : 'var(--accent2)';
  }

  // Equipment (simPanel-style dots)
  if (equip) {
    // Control Mode display
    var modeDisp = document.getElementById('modeDisplay');
    var modeBadge = document.getElementById('modeBadge');
    if (modeDisp && data.currentModeName) {
      modeDisp.textContent = data.currentModeName;
      var m = data.currentMode || 0;
      // Color-code the mode name
      if (m === 21) { modeDisp.style.color = 'var(--err)'; }        // Failure = red
      else if (m <= 3 || m === 20) { modeDisp.style.color = 'var(--text2)'; } // Standby/Off = dim
      else { modeDisp.style.color = 'var(--on)'; }                  // Active = green
    }
    if (modeBadge) {
      if (data.isAutoMode) {
        modeBadge.textContent = 'ARM AUTO';
        modeBadge.style.background = 'var(--on)';
        modeBadge.style.color = '#000';
      } else if (data.currentMode === 12) {
        modeBadge.textContent = 'MANUAL';
        modeBadge.style.background = 'var(--warn)';
        modeBadge.style.color = '#000';
      } else if (data.currentMode === 21) {
        modeBadge.textContent = 'FAILURE';
        modeBadge.style.background = 'var(--err)';
        modeBadge.style.color = '#fff';
      } else {
        modeBadge.textContent = data.currentMode > 0 ? 'IDLE' : '--';
        modeBadge.style.background = 'var(--border)';
        modeBadge.style.color = 'var(--text2)';
      }
    }

    const sw = data.switches || {};
    const decoded = data.decodedOutputs || [];
    const hasDecoded = data.outputConfigLoaded && decoded.length > 0;
    // When decoded outputs are available, show actual firmware output state
    const isOnDecoded = function(eqIdx) { return decoded.some(function(d) { return d.eqIndex === eqIdx && d.on; }); };
    // ARM EquipStatus — for equipment where mapping is known, check armOn
    const armOnDecoded = function(eqIdx) {
      for (var i = 0; i < decoded.length; i++) {
        if (decoded[i].eqIndex === eqIdx && typeof decoded[i].armOn === 'boolean') return decoded[i].armOn;
      }
      return undefined;  // no ARM data for this equipment
    };
    // For climacell: prefer ARM EquipStatus since SvelteKit UI uses it
    var climaCpld = hasDecoded ? isOnDecoded(3) : false;
    var climaArm = hasDecoded ? armOnDecoded(3) : undefined;
    var climaOn = typeof climaArm === 'boolean' ? climaArm : (hasDecoded ? climaCpld : sw.climacell !== 'off');
    var climaLabel = climaOn ? 'ON' : 'OFF';
    var climaMismatch = typeof climaArm === 'boolean' && climaCpld !== climaArm;
    if (climaMismatch) climaLabel += (climaArm ? ' (ARM)' : '') + ' / CPLD: ' + (climaCpld ? 'ON' : 'OFF');
    if (!hasDecoded) climaLabel = sw.climacell === 'off' ? 'OFF' : (sw.climacell || '').toUpperCase();

    // Door state: PIDU-controlled proportional actuators, not simple on/off.
    // Door opening % comes from ARM MainData[11] via armDoorOutput (0-100).
    // CPLD bit EQ.DOORS (index 1) indicates door motor is active.
    // Doors are open if CPLD bit is set OR ARM reports >0% opening.
    var doorPct = data.armDoorOutput || 0;
    var doorsOpen = hasDecoded ? (isOnDecoded(1) || doorPct > 0) : (sw.freshAir !== 'off');
    var doorLabel;
    if (doorsOpen) {
      doorLabel = doorPct > 0 ? 'OPEN (' + doorPct + '%)' : 'OPEN';
    } else {
      doorLabel = 'CLOSED';
    }
    if (!hasDecoded) doorLabel = sw.freshAir === 'off' ? 'CLOSED' : (sw.freshAir || '').toUpperCase();

    const eqItems = [
      { id: 'fan', on: equip.fanOn, label: equip.fanOn ? ('ON' + (data.armFanSpeed > 0 ? ' (' + data.armFanSpeed + '%)' : '')) : 'OFF' },
      { id: 'doors', on: doorsOpen, label: doorLabel },
      { id: 'heat', on: equip.heatOn, label: equip.heatOn ? 'ON' : 'OFF' },
      { id: 'burner', on: equip.burnerOn, label: equip.burnerOn ? 'ON' : 'OFF' },
      { id: 'refrig', on: equip.refrigOn, label: equip.refrigOn ? 'ON' : 'OFF' },
      { id: 'climacell', on: climaOn, label: climaLabel },
      { id: 'humid', on: equip.humidOn, label: equip.humidOn ? 'ON' : 'OFF' },
      { id: 'cooling', on: equip.coolingMode, label: equip.coolingMode ? 'COOLING' : 'OFF' },
    ];
    eqItems.forEach(function(item) {
      const dot = document.getElementById('eq_' + item.id);
      const val = document.getElementById('eq_' + item.id + '_val');
      if (dot) dot.className = 'dot ' + (item.on ? 'on' : 'off');
      if (val) val.textContent = item.label;
    });
    // Update source note
    const note = document.getElementById('eqSourceNote');
    if (note) {
      var modeLabel = data.currentModeName || 'Unknown';
      if (data.isAutoMode && hasDecoded) {
        var noteText = '\\u26a1 ARM AUTO (' + modeLabel + ') \\u2014 outputs controlled by firmware via CPLD';
        if (data.armEquipStatusLoaded) noteText += ' + EquipStatus';
        if (climaMismatch) noteText += '\\n\\u26a0 ClimaCell: ARM says ON but CPLD port is OFF';
        note.textContent = noteText;
        note.style.borderLeftColor = climaMismatch ? 'var(--warn)' : 'var(--on)';
      } else if (hasDecoded && data.armEquipStatusLoaded) {
        note.textContent = '\\u26a1 Mode: ' + modeLabel + ' \\u2014 Equipment from CPLD + ARM EquipStatus';
        note.style.borderLeftColor = 'var(--on)';
      } else if (hasDecoded) {
        note.textContent = '\\u26a1 Mode: ' + modeLabel + ' \\u2014 Equipment from CPLD decoded outputs';
        note.style.borderLeftColor = 'var(--on)';
      } else if (equip.lastPoll > 0 && data.physics && data.physics.equipmentPolling) {
        note.textContent = '\\ud83d\\udd04 Equipment polled from bridge API (HTTP feedback loop)';
        note.style.borderLeftColor = 'var(--accent)';
      } else {
        note.textContent = '\\ud83d\\udee0\\ufe0f Equipment derived from switch positions (no firmware feedback)';
        note.style.borderLeftColor = 'var(--warn)';
      }
    }
  }

  // CPLD Switches
  updateSwitchDisplay(data.switches);

  // Digital Input Proof Signals
  if (data.invertedInputs) {
    INVERTED_MAIN = data.invertedInputs.main || [];
    INVERTED_EX1 = data.invertedInputs.ex1 || [];
    INVERTED_EX2 = data.invertedInputs.ex2 || [];
  }
  updateDigitalIO(data.digitalInputs, data.cpldOutputs, data.cpldConnected);

  // Dynamic Equipment Outputs
  updateDynamicOutputs(data.decodedOutputs, data.outputConfigLoaded);

  // Analog boards
  if (data.boards) {
    renderBoards(data.boards);
  }

  // VFD Drives
  if (data.vfdDrives) {
    window._vfdDrives = data.vfdDrives;
    renderVFDs(data.vfdDrives);
  }

  // Populate physics config fields once
  if (!configLoaded && phys) {
    document.getElementById('phys_enabled').checked = phys.enabled;
    document.getElementById('phys_outsideTempMin').value = phys.outsideTempMin;
    document.getElementById('phys_outsideTempMax').value = phys.outsideTempMax;
    document.getElementById('phys_diurnalPeriodSec').value = phys.diurnalPeriodSec;
    document.getElementById('phys_potatoMass').value = phys.potatoMass;
    document.getElementById('phys_potatoMass_num').value = phys.potatoMass;
    document.getElementById('phys_insulationFactor').value = phys.insulationFactor;
    document.getElementById('phys_insulationFactor_num').value = phys.insulationFactor;
    document.getElementById('phys_returnTempOffset').value = phys.returnTempOffset;
    document.getElementById('phys_returnTempOffset_num').value = phys.returnTempOffset;
    document.getElementById('phys_tempHumidCoeff').value = phys.tempHumidCoeff;
    document.getElementById('phys_tempHumidCoeff_num').value = phys.tempHumidCoeff;
    document.getElementById('phys_outsideHumidBase').value = phys.outsideHumidBase;
    document.getElementById('phys_outsideHumidBase_num').value = phys.outsideHumidBase;
    document.getElementById('phys_co2AmbientPpm').value = phys.co2AmbientPpm;
    document.getElementById('phys_co2LeakRate').value = phys.co2LeakRate;
    document.getElementById('phys_equipmentPolling').value = phys.equipmentPolling;
    updatePotatoVisual('potatoMass');
    configLoaded = true;
  }
}

// ── Presets ──
async function applyPreset(name) {
  try {
    const res = await fetch(API + '/api/preset', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ name }),
    });
    const data = await res.json();
    if (data.ok) { configLoaded = false; showToast('Preset: ' + name); }
    else { showToast('Unknown preset: ' + name); }
  } catch(e) { showToast('Error applying preset'); }
}

async function resetAll() {
  try {
    await fetch(API + '/api/reset', { method: 'POST' });
    configLoaded = false;
    showToast('Reset to defaults');
  } catch(e) { showToast('Error resetting'); }
}

// ── Analog Board Management ──
const BOARD_TYPES = { 0: 'Temp IR', 1: 'Humidity', 2: 'CO2', 3: 'Temperature' };
const SENSOR_TYPES = {
  0: 'Temp IR', 1: 'Humidity', 2: 'CO2 #1', 3: 'Temperature',
  4: 'Return Temp 1', 5: 'Return Temp 2', 6: 'Return Humid 1', 7: 'Return Humid 2',
  8: 'CO2 #2', 9: 'Pile Temp', 10: 'Pile Humid', 11: 'Static Press', 255: 'Undefined'
};
const TEMP_UI_TYPES = [0, 3, 4, 5, 9];
const HUMID_UI_TYPES = [1, 6, 7, 10, 11];
const CO2_UI_TYPES = [2, 8];

function sensorValueClass(uiType) {
  if (HUMID_UI_TYPES.includes(uiType)) return 'humid';
  if (CO2_UI_TYPES.includes(uiType)) return 'co2';
  return '';
}
function sensorUnit(uiType) {
  if (HUMID_UI_TYPES.includes(uiType)) return '%';
  if (CO2_UI_TYPES.includes(uiType)) return ' ppm';
  return '\\u00b0C';
}
function sensorRange(uiType) {
  if (HUMID_UI_TYPES.includes(uiType)) return { min: 0, max: 100, step: 0.5 };
  if (CO2_UI_TYPES.includes(uiType)) return { min: 0, max: 10000, step: 10 };
  return { min: -40, max: 83, step: 0.1 };
}

let boardsData = [];

function renderBoards(boards) {
  boardsData = boards;
  const c = document.getElementById('boardsContainer');
  if (!c) return;
  if (!boards || boards.length === 0) {
    c.innerHTML = '<div style="color:var(--text2);font-size:13px;padding:12px">No boards configured</div>';
    return;
  }
  c.innerHTML = boards.map((b, bi) => {
    const isPhysics = b.address <= 1;
    const typeLabel = BOARD_TYPES[b.boardType] || 'Unknown';
    return '<div class="board-card">' +
      '<div class="board-card-header">' +
        '<span class="bc-title">Addr ' + b.address + ' \\u2014 ' + (b.label || ('Board ' + (b.address + 1))) + '</span>' +
        '<span class="bc-type">' + typeLabel + ' (v' + b.firmwareVersion[0] + '.' + b.firmwareVersion[1] + ')</span>' +
        (b.address >= 2 ? '<button class="bc-remove" onclick="removeBoard(' + b.address + ')">\\u2717 Remove</button>' : '') +
      '</div>' +
      (isPhysics ? '<div class="board-physics-note">\\u26a0 Sensor values driven by Physics Engine (use sliders above to override)</div>' : '') +
      '<div class="board-sensors">' +
        b.sensors.map((s, si) => {
          const cls = sensorValueClass(s.uiType);
          const unit = sensorUnit(s.uiType);
          const rng = sensorRange(s.uiType);
          const valDisp = s.disabled ? 'dis' : (TEMP_UI_TYPES.includes(s.uiType) ? cToF(s.value) + '\\u00b0F' : s.value.toFixed(1) + unit);
          return '<div class="bs-item">' +
            '<div class="bs-label">' + (s.label || ('S' + (si + 1))) + ' <span style="opacity:.5">SID ' + (b.address * 4 + si) + '</span></div>' +
            '<div class="bs-val ' + cls + '">' + valDisp + '</div>' +
            (isPhysics ? '' : (
              '<input type="range" min="' + rng.min + '" max="' + rng.max + '" step="' + rng.step + '" value="' + s.value + '" ' +
              'oninput="onBoardSensor(' + b.address + ',' + si + ',this.value)">'
            )) +
          '</div>';
        }).join('') +
      '</div>' +
    '</div>';
  }).join('');
}

async function onBoardSensor(addr, sensorIdx, value) {
  try {
    await fetch(API + '/api/boards/sensor', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ address: addr, sensor: sensorIdx, value: parseFloat(value) }),
    });
  } catch(e) {}
}

async function addBoard() {
  // Find next available address
  const used = boardsData.map(b => b.address);
  let addr = 2;
  while (used.includes(addr) && addr < 32) addr++;
  if (addr >= 32) { showToast('Max 32 boards'); return; }

  const boardNum = addr + 1;
  const config = {
    address: addr,
    boardType: 3,
    label: 'Pile Temp',
    firmwareVersion: [1, 0],
    disabled: false,
    sensors: [
      { uiType: 9, label: 'Bd ' + boardNum + ' - S 1', offset: 0, disabled: false, value: 7.0 + Math.random() },
      { uiType: 9, label: 'Bd ' + boardNum + ' - S 2', offset: 0, disabled: false, value: 7.0 + Math.random() },
      { uiType: 9, label: 'Bd ' + boardNum + ' - S 3', offset: 0, disabled: false, value: 7.0 + Math.random() },
      { uiType: 9, label: 'Bd ' + boardNum + ' - S 4', offset: 0, disabled: false, value: 7.0 + Math.random() },
    ],
  };
  try {
    const res = await fetch(API + '/api/boards/add', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(config),
    });
    const data = await res.json();
    if (data.ok) { showToast('Board added at addr ' + addr); }
    else { showToast(data.error || 'Failed to add board'); }
  } catch(e) { showToast('Error adding board'); }
}

async function removeBoard(addr) {
  if (!confirm('Remove board at address ' + addr + '?')) return;
  try {
    const res = await fetch(API + '/api/boards/remove', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ address: addr }),
    });
    const data = await res.json();
    if (data.ok) { showToast('Board removed'); }
    else { showToast(data.error || 'Failed to remove board'); }
  } catch(e) { showToast('Error removing board'); }
}

// ── VFD Drive Rendering ──
const VFD_FAULTS = {
  0x0001: 'Overcurrent', 0x0002: 'DC overvoltage', 0x0003: 'Overtemp',
  0x0004: 'Short circuit', 0x0007: 'Motor overtemp', 0x0008: 'Motor stall',
  0x000A: 'Output phase loss', 0x000B: 'Input phase loss', 0x000C: 'Earth fault',
  0x0032: 'External fault', 0x0037: 'Fieldbus comm loss',
};

function renderVFDs(drives) {
  const el = document.getElementById('vfdContainer');
  if (!el) return;
  if (!drives || drives.length === 0) {
    el.innerHTML = '<div style="color:var(--text2);font-size:13px;padding:8px">No VFD drives configured. Click + Add Drive to add one.</div>';
    return;
  }
  el.innerHTML = drives.map(function(d) {
    const speedPct = (d.actualSpeedPercent / 100).toFixed(1);
    const refPct = (d.speedRefPercent / 100).toFixed(1);
    const freqHz = (d.outputFreqHz / 10).toFixed(1);
    const currentA = (d.motorCurrentA / 10).toFixed(1);
    const powerkW = (d.motorPowerkW / 10).toFixed(1);
    const volts = (d.outputVoltage / 10).toFixed(0);
    const rpm = d.motorSpeedRpm || 0;
    const temp = d.driveTemp || 0;
    const statusClass = d.faulted ? 'vfd-fault' : d.running ? 'vfd-running' : 'vfd-stopped';
    const statusText = d.faulted ? 'FAULT' : d.running ? 'RUNNING' : 'STOPPED';
    const faultText = d.faulted ? (VFD_FAULTS[d.lastFaultCode] || 'Code 0x' + d.lastFaultCode.toString(16)) : '';
    const dirText = d.direction ? 'REV' : 'FWD';
    return '<div class=\"vfd-card ' + statusClass + '\">' +
      '<div class=\"vfd-header\">' +
        '<span class=\"vfd-label\">' + d.label + ' (Unit ' + d.unitId + ')</span>' +
        '<span class=\"vfd-status\">' + statusText + (d.faulted ? ' \\u2014 ' + faultText : '') + '</span>' +
        '<button class=\"vfd-remove\" onclick=\"removeVFD(' + d.unitId + ')\">&times;</button>' +
      '</div>' +
      '<div class=\"vfd-metrics\">' +
        '<div class=\"vfd-metric\"><span class=\"vfd-val\">' + speedPct + '%</span><span class=\"vfd-lbl\">Speed</span></div>' +
        '<div class=\"vfd-metric\"><span class=\"vfd-val\">' + freqHz + ' Hz</span><span class=\"vfd-lbl\">Frequency</span></div>' +
        '<div class=\"vfd-metric\"><span class=\"vfd-val\">' + rpm + '</span><span class=\"vfd-lbl\">RPM</span></div>' +
        '<div class=\"vfd-metric\"><span class=\"vfd-val\">' + currentA + ' A</span><span class=\"vfd-lbl\">Current</span></div>' +
        '<div class=\"vfd-metric\"><span class=\"vfd-val\">' + powerkW + ' kW</span><span class=\"vfd-lbl\">Power</span></div>' +
        '<div class=\"vfd-metric\"><span class=\"vfd-val\">' + volts + ' V</span><span class=\"vfd-lbl\">Output V</span></div>' +
        '<div class=\"vfd-metric\"><span class=\"vfd-val\">' + d.dcBusVoltage + ' V</span><span class=\"vfd-lbl\">DC Bus</span></div>' +
        '<div class=\"vfd-metric\"><span class=\"vfd-val\">' + temp + ' \\u00b0C</span><span class=\"vfd-lbl\">Drive Temp</span></div>' +
        '<div class=\"vfd-metric\"><span class=\"vfd-val\">' + dirText + '</span><span class=\"vfd-lbl\">Direction</span></div>' +
        '<div class=\"vfd-metric\"><span class=\"vfd-val\">' + refPct + '%</span><span class=\"vfd-lbl\">Ref</span></div>' +
      '</div>' +
      '<div class=\"vfd-controls\">' +
        '<label>Speed Ref: <input type=\"range\" min=\"0\" max=\"10000\" value=\"' + d.speedRefPercent +
          '\" oninput=\"vfdSetSpeed(' + d.unitId + ',this.value);this.nextElementSibling.textContent=((this.value/100).toFixed(1))+\\'%\\'\">' +
          '<span>' + refPct + '%</span></label>' +
        '<div class=\"btn-row\" style=\"margin-top:6px\">' +
          '<button class=\"primary\" onclick=\"vfdStart(' + d.unitId + ')\">\\u25B6 Start</button>' +
          '<button onclick=\"vfdStop(' + d.unitId + ')\">\\u23F9 Stop</button>' +
          '<button onclick=\"vfdFault(' + d.unitId + ')\">\\u26A0 Inject Fault</button>' +
          '<button onclick=\"vfdReset(' + d.unitId + ')\">\\u{1F504} Reset</button>' +
        '</div>' +
      '</div>' +
    '</div>';
  }).join('');
}

async function vfdStart(unitId) {
  await fetch(API + '/api/vfd/control', {
    method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ unitId, controlWord: 0x047F }),
  });
}
async function vfdStop(unitId) {
  await fetch(API + '/api/vfd/control', {
    method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ unitId, controlWord: 0x040E }),
  });
}
async function vfdSetSpeed(unitId, val) {
  await fetch(API + '/api/vfd/control', {
    method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ unitId, controlWord: 0x047F, speedRefPercent: parseInt(val) }),
  });
}
async function vfdFault(unitId) {
  await fetch(API + '/api/vfd/fault', {
    method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ unitId, faultCode: 0x0032 }),
  });
}
async function vfdReset(unitId) {
  await fetch(API + '/api/vfd/control', {
    method: 'POST', headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ unitId, controlWord: 0x04FE }),
  });
  setTimeout(function() {
    fetch(API + '/api/vfd/control', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ unitId, controlWord: 0x040E }),
    });
  }, 500);
}
async function addVFD() {
  const used = (window._vfdDrives || []).map(function(d) { return d.unitId; });
  var uid = 1;
  while (used.includes(uid) && uid < 248) uid++;
  if (uid >= 248) { showToast('Max 247 drives'); return; }
  const label = prompt('Drive label:', 'Fan VFD ' + uid);
  if (!label) return;
  try {
    await fetch(API + '/api/vfd/add', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ unitId: uid, label: label }),
    });
    showToast('Drive added (unit ' + uid + ')');
  } catch(e) { showToast('Error adding drive'); }
}
async function removeVFD(unitId) {
  if (!confirm('Remove drive unit ' + unitId + '?')) return;
  try {
    await fetch(API + '/api/vfd/remove', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ unitId: unitId }),
    });
    showToast('Drive removed');
  } catch(e) { showToast('Error removing drive'); }
}

// ── Polling ──
async function poll() {
  try {
    const res = await fetch(API + '/api/status');
    const data = await res.json();
    updateDisplay(data);
  } catch(e) {
    document.getElementById('connDot').className = 'status-dot err';
    document.getElementById('connLabel').textContent = 'Panel disconnected';
  }
}
poll();
setInterval(poll, 1500);

// ══════════════════════════════════════════════════════════
// Test Lab — client-side test orchestration
// ══════════════════════════════════════════════════════════

const BRIDGE = 'http://localhost:3001';
let testAbort = false;
let testRunning = false;

// ── Helpers ──
function testLog(msg, type) {
  var log = document.getElementById('testLog');
  if (log.querySelector('[style]') && !log.querySelector('.test-entry')) log.innerHTML = '';
  var ts = new Date().toLocaleTimeString('en-US', {hour12:false});
  var colors = { pass:'var(--on)', fail:'var(--err)', info:'var(--accent)', warn:'var(--warn)', step:'var(--text2)' };
  var icons = { pass:'\\u2705', fail:'\\u274C', info:'\\u2139\\uFE0F', warn:'\\u26A0\\uFE0F', step:'  \\u25B8' };
  var div = document.createElement('div');
  div.className = 'test-entry';
  div.style.color = colors[type] || 'var(--text)';
  div.textContent = '[' + ts + '] ' + (icons[type]||'') + ' ' + msg;
  log.appendChild(div);
  log.scrollTop = log.scrollHeight;
}

function sleep(ms) { return new Promise(function(r) { setTimeout(r, ms); }); }

async function fetchBridge(path) {
  var res = await fetch(BRIDGE + path);
  return res.json();
}

async function setSensors(obj) {
  await fetch(API + '/api/sensors', {
    method:'POST', headers:{'Content-Type':'application/json'},
    body: JSON.stringify(obj)
  });
}

async function setDigitalInput(board, port, closed) {
  await fetch(API + '/api/digital-io', {
    method:'POST', headers:{'Content-Type':'application/json'},
    body: JSON.stringify({ board: board, port: port, closed: closed })
  });
}

async function saveSensorBaseline() {
  var res = await fetch(API + '/api/status');
  var data = await res.json();
  return data.sensors;
}

async function restoreSensors(baseline) {
  await setSensors(baseline);
  // Release all manual overrides so physics can resume
  await fetch(API + '/api/sensors', {
    method:'POST', headers:{'Content-Type':'application/json'},
    body: JSON.stringify({ _releaseAll: true })
  });
}

async function getSetpoints() {
  var data = await fetchBridge('/iot/frontmatter');
  var main = data.main || [];
  // Firmware sends 'Manual' when fan switch is in manual mode (CPLD drives fan at 100%)
  var rawFan = main[14] || '0';
  var fanSpeed = (rawFan === 'Manual') ? 100 : (parseFloat(rawFan) || 0);
  return {
    tempSetpoint: parseFloat(main[3]) || 0,
    humidSetpoint: parseFloat(main[6]) || 0,
    plenumTemp: parseFloat(main[2]) || 0,
    plenumHumid: parseFloat(main[5]) || 0,
    outsideTemp: parseFloat(main[7]) || 0,
    fanSpeed: fanSpeed,
    fanSpeedRaw: rawFan,
    coolOutput: parseFloat(main[15]) || 0,
    co2: parseFloat(main[17]) || 0,
    co2Setpoint: parseFloat(main[33]) || 1000,
  };
}

async function getCurrentMode() {
  var data = await fetchBridge('/iot/ws/header-data');
  return { mode: data.CurrentMode || 0 };
}

async function getAlarms() {
  var data = await fetchBridge('/iot/frontmatter');
  return data.AlarmData || [];
}

async function getEquipmentStatus() {
  var data = await fetchBridge('/iot/ws/equipment-data');
  return data.eqStatus || [];
}

// ── Read CPLD pulsed-door output state ──
// The firmware drives EQ_PULSEDOOR_CLOSE (Main bit 0), EQ_PULSEDOOR_OPEN (bit 1)
// via the CPLD shift register.  These outputs are NOT
// reported in EquipStatus — they're only visible in the CPLD output words.
// CPLD Main board bit mapping (from SerialShift.h):
//   bit 0 = SS_MAIN_PD_DN  → Pulse Door Close (port 9)
//   bit 1 = SS_MAIN_PD_UP  → Pulse Door Open  (port 10)
async function getCpldDoorState() {
  var resp = await fetch(API + '/api/status');
  var data = await resp.json();
  var main = data.cpldOutputs ? data.cpldOutputs.main : 0;
  return {
    close: (main & 1) !== 0,   // bit 0 = PD_DN
    open:  (main & 2) !== 0,   // bit 1 = PD_UP
    connected: data.cpldConnected || false,
    raw: main
  };
}

// Wait for a condition, polling every interval ms, up to timeout ms
async function waitFor(desc, checkFn, timeoutMs, intervalMs) {
  intervalMs = intervalMs || 2000;
  var deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (testAbort) throw new Error('Test aborted');
    var result = await checkFn();
    if (result) return result;
    await sleep(intervalMs);
  }
  return null;
}

function setCardStatus(id, status) {
  var card = document.getElementById('tc_' + id);
  if (!card) return;
  var badge = card.querySelector('.tc-badge');
  if (!badge) return;
  var map = { idle:['--','var(--text2)'], running:['RUNNING','var(--warn)'], pass:['PASS','var(--on)'], fail:['FAIL','var(--err)'], skip:['SKIP','var(--text2)'] };
  var s = map[status] || map.idle;
  badge.textContent = s[0];
  badge.style.color = s[1];
  badge.style.borderColor = s[1];
}

// ── Refresh status bar ──
async function refreshTestStatus() {
  try {
    var sp = await getSetpoints();
    var mode = await getCurrentMode();
    var modeNames = {1:'Shutdown',2:'Standby',3:'Rem Stby',4:'Cooling',5:'Refrig',6:'Recirc',7:'Heating',8:'Defrost',9:'CO2 Purge',10:'Cool Ramp',11:'Refrig Ramp',12:'Fan Manual',13:'Fan Off',14:'Fan Rem Off',15:'Refrig Rem Off',16:'Air Cure',17:'Burner Cure',18:'Cool Dehum',19:'Refrig Dehum',20:'Sys Rem Off',21:'Failure',22:'Fan Boost'};
    document.getElementById('testBridgeStatus').textContent = '\\u2714 Connected';
    document.getElementById('testBridgeStatus').style.color = 'var(--on)';
    document.getElementById('testSetpoint').textContent = sp.tempSetpoint.toFixed(1) + '\\u00b0F';
    document.getElementById('testMode').textContent = modeNames[mode.mode] || ('Mode ' + mode.mode);
    document.getElementById('testPlenum').textContent = sp.plenumTemp.toFixed(1) + '\\u00b0F';
  } catch(e) {
    document.getElementById('testBridgeStatus').textContent = '\\u2716 Disconnected';
    document.getElementById('testBridgeStatus').style.color = 'var(--err)';
  }
}

// ══════════════════════════════════════════════════════════
// Test Definitions — Full Equipment & Mode Verification Matrix
// ══════════════════════════════════════════════════════════

// ── Mode IDs ──
var MODE = {OFF:0,SHUTDOWN:1,STANDBY:2,REM_STANDBY:3,COOLING:4,REFRIG:5,RECIRC:6,HEATING:7,
  DEFROST:8,CO2_PURGE:9,COOL_RAMP:10,REFRIG_RAMP:11,FAN_MANUAL:12,FAN_OFF:13,
  FAN_REMOTEOFF:14,REFRIG_REMOTEOFF:15,AIRCURE:16,BURNERCURE:17,
  COOL_DEHUMID:18,REFRIG_DEHUMID:19,SYS_REMOTEOFF:20,FAILURE:21,FAN_BOOST:22};
var MODE_NAME = {0:'Off',1:'Shutdown',2:'Standby',3:'Remote Standby',4:'Cooling',5:'Refrig',6:'Recirc',
  7:'Heating',8:'Defrost',9:'CO2 Purge',10:'Cool Ramp',11:'Refrig Ramp',12:'Fan Manual',
  13:'Fan Off',14:'Fan Remote Off',15:'Refrig Remote Off',16:'Air Cure',17:'Burner Cure',
  18:'Cool Dehumid',19:'Refrig Dehumid',20:'System Remote Off',21:'Failure',22:'Fan Boost'};

// ── Equipment status indices ──
var EQI = {
  FAN_SW:0, FAN_INP:1, FAN_OUT:2,
  CC_SW:3, CC_INP:4, CC_OUT:5,
  BURN_INP:6, BURN_OUT:7,
  HUM_SW:8,
  H1_INP:9, H1_HEAD:10, H1_PUMP:11,
  H2_INP:12, H2_HEAD:13, H2_PUMP:14,
  DOOR_REFRIG:15, REFRIG_INP:16,
  RS1:17, RS2:18, RS3:19, RS4:20, RS5:21, RS6:22, RS7:23, RS8:24,
  DEFR1:25, DEFR2:26,
  HEAT_INP:27, HEAT_OUT:28,
  DOOR_SW:29, DOOR_DIR:30,   // eq[29]=SW_FRESHAIR(0/1/2), eq[30]=movement(0=stop,1=close,2=open)
  CAVH_INP:31, CAVH_OUT:32,
  GREEN:33, YELLOW:34, RED:35,
  // NOTE: Pulsed door CPLD outputs (close/open/power) are NOT in EquipStatus.
  // Use getCpldDoorState() to read them from the CPLD shift register (port 9003).
  AUX1:47,
  H3_INP:76, H3_HEAD:77, H3_PUMP:78
};

// ── F to C conversion ──
function f2c(f) { return (f - 32) * 5 / 9; }

// ── Post ClearAlarm through bridge ──
async function clearAlarmsBridge() {
  await fetch(BRIDGE + '/iot/PostSave.jsp', {
    method:'POST', headers:{'Content-Type':'application/json'},
    body: JSON.stringify({ ClearAlarm:'ClearAlarm' })
  });
  await sleep(2000);
}

// ── Set switches via panel API ──
async function setSwitchesAPI(obj) {
  await fetch(API + '/api/switches', {
    method:'POST', headers:{'Content-Type':'application/json'},
    body: JSON.stringify(obj)
  });
}

// ── Disable/Enable physics ──
async function setPhysics(on) {
  await fetch(API + '/api/physics', {
    method:'POST', headers:{'Content-Type':'application/json'},
    body: JSON.stringify({ enabled: on })
  });
}

// ── Check eqStatus field ──
function eqOn(eq, idx) { return eq[idx] === '1'; }
function eqStr(eq, idx, lbl) { return lbl + ':' + (eqOn(eq,idx)?'ON':'OFF'); }

// ── Assert helper ──
function chk(cond, pass, fail) {
  if (cond) { testLog(pass, 'pass'); return true; }
  else { testLog(fail, 'fail'); return false; }
}

// ── Wait for mode ──
async function waitForMode(modes, ms, desc) {
  desc = desc || modes.map(function(m){return MODE_NAME[m]||m;}).join('/');
  testLog('Waiting for ' + desc + ' (up to ' + Math.round(ms/1000) + 's)...', 'step');
  return await waitFor(desc, async function() {
    var m = await getCurrentMode();
    if (modes.indexOf(m.mode) !== -1) return m;
    return null;
  }, ms, 2000);
}

// ── Log a snapshot line ──
async function snap(label) {
  var sp = await getSetpoints(); var mode = await getCurrentMode(); var eq = await getEquipmentStatus();
  var cpld = await getCpldDoorState();
  testLog('[' + label + '] Mode=' + (MODE_NAME[mode.mode]||mode.mode) +
    ' Plenum=' + sp.plenumTemp.toFixed(1) + 'F Cool=' + sp.coolOutput + '%' +
    ' Fan=' + sp.fanSpeed + '% ' +
    eqStr(eq,EQI.FAN_OUT,'Fan') + ' ' + eqStr(eq,EQI.DOOR_SW,'DSw') + ' ' +
    'DDir:' + (eq[EQI.DOOR_DIR]||'0') + ' ' +
    'DCls:' + (cpld.close?'ON':'OFF') + ' DOpn:' + (cpld.open?'ON':'OFF') +
    ' DPwr:' + (cpld.power?'ON':'OFF') + ' ' +
    eqStr(eq,EQI.CC_OUT,'CC') + ' ' + eqStr(eq,EQI.HEAT_OUT,'Heat') + ' ' +
    eqStr(eq,EQI.GREEN,'Grn') + ' ' + eqStr(eq,EQI.YELLOW,'Yel') + ' ' +
    eqStr(eq,EQI.RED,'Red'), 'info');
}

// ── Proof test: open DI, wait for alarm/failure, verify, restore, clear ──
async function testProof(brd, port, label, eqIdx, delayMs, isSysFail, eqIdx2) {
  var ok = true;
  testLog('Testing ' + label + ' proof failure...', 'step');
  await setDigitalInput(brd, port, false);
  testLog(label + ' proof OPENED. Waiting ' + Math.round(delayMs/1000) + 's...', 'step');

  var alarm = await waitFor(label+' alarm', async function() {
    if (isSysFail) { var m = await getCurrentMode(); if (m.mode === MODE.FAILURE) return 'System Failure'; }
    var al = await getAlarms();
    for (var i = 0; i < al.length; i++) { if (al[i] && al[i].length > 0) return al[i]; }
    return null;
  }, delayMs + 30000, 3000);

  if (alarm) { testLog('Alarm: ' + alarm, 'pass'); }
  else { testLog('No alarm for ' + label, 'fail'); ok = false; }

  // Wait 10s for equipment to shut down after alarm
  await sleep(10000);

  if (isSysFail) {
    var m = await getCurrentMode();
    if (!chk(m.mode === MODE.FAILURE, 'FAILURE mode entered', 'Expected FAILURE, got ' + (MODE_NAME[m.mode]||m.mode))) ok = false;
    var eq = await getEquipmentStatus();
    chk(!eqOn(eq,EQI.FAN_OUT), 'Fan OFF in failure', 'Fan still ON');
    chk(!eqOn(eq,EQI.GREEN), 'Green OFF', 'Green still ON');
  } else if (eqIdx >= 0) {
    var eq = await getEquipmentStatus();
    if (!chk(!eqOn(eq,eqIdx), label+' output OFF', label+' still ON')) ok = false;
    if (eqIdx2 !== undefined) { if (!chk(!eqOn(eq,eqIdx2), label+' head OFF', label+' head still ON')) ok = false; }
    chk(eqOn(eq,EQI.FAN_OUT), 'Fan still running', 'Fan stopped unexpectedly');
  }

  await setDigitalInput(brd, port, true);
  testLog(label + ' proof restored', 'step');
  await sleep(2000);
  await clearAlarmsBridge();

  if (isSysFail) {
    var rec = await waitFor('recovery', async function() {
      var m = await getCurrentMode(); return m.mode !== MODE.FAILURE ? m : null;
    }, 90000, 3000);
    if (rec) testLog('Recovered to ' + (MODE_NAME[rec.mode]||rec.mode), 'pass');
    else { testLog('No recovery from failure', 'fail'); ok = false; }
  }
  await sleep(3000);
  return ok;
}

// ── Verify mode outputs ──
async function verifyOut(name, exp) {
  var eq = await getEquipmentStatus(); var sp = await getSetpoints(); var ok = true;
  testLog('Verifying ' + name + ' outputs...', 'step');
  // EquipStatus-based checks (for equipment the ARM reports directly)
  var checks = [
    ['fan', EQI.FAN_OUT, 'Fan'],
    ['door', EQI.DOOR_SW, 'Door Switch (AUTO)'],
    ['climacell', EQI.CC_OUT, 'Climacell'],
    ['heat', EQI.HEAT_OUT, 'Heat'],
    ['green', EQI.GREEN, 'Green'],
    ['yellow', EQI.YELLOW, 'Yellow'],
    ['red', EQI.RED, 'Red'],
    ['humid1', EQI.H1_PUMP, 'Humid1 Pump']
  ];
  for (var i = 0; i < checks.length; i++) {
    var k = checks[i][0], idx = checks[i][1], lbl = checks[i][2];
    if (k in exp) {
      var want = exp[k] ? 'ON' : 'OFF', got = eqOn(eq,idx) ? 'ON' : 'OFF';
      if (!chk(want===got, lbl+': '+want+' \\u2714', lbl+': expected '+want+' got '+got)) ok = false;
    }
  }
  // CPLD-based door output checks (pulsed door outputs are only in the shift register)
  if ('doorClose' in exp || 'doorOpen' in exp) {
    var cpld = await getCpldDoorState();
    if (!cpld.connected) { testLog('CPLD not connected — cannot verify door outputs', 'warn'); }
    else {
      if ('doorClose' in exp) {
        var wc = exp.doorClose ? 'ON' : 'OFF', gc = cpld.close ? 'ON' : 'OFF';
        if (!chk(wc===gc, 'CPLD Door Close: '+wc+' \\u2714', 'CPLD Door Close: expected '+wc+' got '+gc)) ok = false;
      }
      if ('doorOpen' in exp) {
        var wo = exp.doorOpen ? 'ON' : 'OFF', go = cpld.open ? 'ON' : 'OFF';
        if (!chk(wo===go, 'CPLD Door Open: '+wo+' \\u2714', 'CPLD Door Open: expected '+wo+' got '+go)) ok = false;
      }
    }
  }
  if ('fanSpeed' in exp) {
    var label = sp.fanSpeedRaw === 'Manual' ? sp.fanSpeed+'% (Manual)' : sp.fanSpeed+'%';
    var diff = Math.abs(sp.fanSpeed - exp.fanSpeed);
    if (!chk(diff<=5, 'Fan speed: '+label+' (~'+exp.fanSpeed+'%) \\u2714',
      'Fan speed: '+label+' (expected ~'+exp.fanSpeed+'%)')) ok = false;
  }
  return ok;
}

// ══════════════════════════════════════════════════════════
// TEST DEFINITIONS
// ══════════════════════════════════════════════════════════

var TEST_DEFS = [
  // ─── Phase 1: COOLING MODE ───
  {
    id: 'p1_cooling',
    name: '1. Cooling Mode + Proofs',
    desc: 'Enter cooling, verify outputs, test climacell/humid proofs, verify cool output drop',
    run: async function() {
      var ok = true; await setPhysics(false); var baseline = await saveSensorBaseline();
      try {
        var sp = await getSetpoints();
        testLog('\u2550\u2550 PHASE 1: COOLING MODE \u2550\u2550', 'info');
        testLog('Temp SP: ' + sp.tempSetpoint.toFixed(1) + 'F, Humid SP: ' + sp.humidSetpoint.toFixed(0) + '%', 'step');
        var pF = sp.tempSetpoint + 3, oF = 30;
        await setSensors({ plenumTemp1:f2c(pF), plenumTemp2:f2c(pF)+0.1, outsideTemp:f2c(oF), outsideHumid:55, _override:'plenumTemp' });
        testLog('Sensors: plenum=' + pF + 'F, outside=' + oF + 'F', 'step');

        var mr = await waitForMode([MODE.COOLING,MODE.COOL_RAMP], 90000, 'Cooling');
        if (!mr) { testLog('Did not enter Cooling', 'fail'); return false; }
        testLog('Entered ' + (MODE_NAME[mr.mode]||mr.mode), 'pass');
        await sleep(6000); await snap('Cooling steady');

        if (!await verifyOut('Cooling', {fan:true,fanSpeed:25,climacell:true,heat:false,green:true,yellow:false,red:false})) ok = false;

        var eq = await getEquipmentStatus();
        if (!chk(eqOn(eq,EQI.H1_PUMP), 'Humid1 pump ON (manual) \u2714', 'Humid1 pump should be ON')) ok = false;

        // Climacell proof
        if (!await testProof('main',3,'Climacell',EQI.CC_OUT,60000,false)) ok = false;

        // Humid1 proof
        eq = await getEquipmentStatus();
        if (eqOn(eq,EQI.H1_PUMP)) { if (!await testProof('main',4,'Humid1',EQI.H1_PUMP,60000,false,EQI.H1_HEAD)) ok = false; }
        else testLog('Humid1 not active, skip proof', 'warn');

        // Cool output > 0
        var sp2 = await getSetpoints();
        if (!chk(sp2.coolOutput > 0, 'Cool output > 0%: ' + sp2.coolOutput + '% \u2714', 'Cool output 0%')) ok = false;

        // Drop below SP: cool output drops (PID — can take up to 10 min)
        testLog('Dropping plenum below SP...', 'step');
        var lowF = sp.tempSetpoint - 3;
        await setSensors({ plenumTemp1:f2c(lowF), plenumTemp2:f2c(lowF)+0.1, _override:'plenumTemp' });
        var lastCool = 999, stallCount = 0, cd = false;
        for (var ci = 0; ci < 90; ci++) { // 90 × 10s = 15 min max
          await sleep(10000);
          var cs = await getSetpoints();
          testLog('Cool output: ' + cs.coolOutput + '%', 'step');
          if (cs.coolOutput <= 0) { cd = true; break; }
          if (cs.coolOutput < lastCool) { stallCount = 0; lastCool = cs.coolOutput; }
          else { stallCount++; if (stallCount >= 18) { testLog('Cool output stalled at ' + cs.coolOutput + '%', 'warn'); break; } }
        }
        if (cd) {
          var cpld = await getCpldDoorState();
          if (!chk(cpld.close, 'CPLD door close ON at cool=0 \u2714', 'CPLD door close not ON (raw=0x'+cpld.raw.toString(16)+')')) ok = false;
          eq = await getEquipmentStatus();
          if (!chk(eqOn(eq,EQI.FAN_OUT), 'Fan stays ON below SP \u2714', 'Fan OFF')) ok = false;
          testLog('Cool output 0%, door closed, fan on', 'pass');
        } else { testLog('Cool output did not drop to 0%', 'fail'); ok = false; }
        return ok;
      } finally { await restoreSensors(baseline); }
    }
  },

  // --- Phase 2: REFRIG MODE + STAGE SEQUENCING ---
  {
    id: 'p2_refrig',
    name: '2. Refrig Mode + Stage Sequencing',
    desc: 'Enter refrig, verify stages come on 1-6, test stage proofs, humid2 timer',
    run: async function() {
      var ok = true; await setPhysics(false); var baseline = await saveSensorBaseline();
      try {
        var sp = await getSetpoints();
        testLog('\u2550\u2550 PHASE 2: REFRIGERATION MODE \u2550\u2550', 'info');
        var pF = sp.tempSetpoint + 4, oF = 60;
        await setSensors({ plenumTemp1:f2c(pF), plenumTemp2:f2c(pF)+0.1, outsideTemp:f2c(oF), outsideHumid:70, _override:'plenumTemp' });
        testLog('Sensors: plenum=' + pF + 'F, outside=' + oF + 'F (above cooling avail)', 'step');

        // Stop/Start cycle to enter refrig directly (avoids cooling ramp)
        testLog('Stop/Start cycle to force direct refrig entry...', 'step');
        await setSwitchesAPI({ startStop: 'shutdown' });
        var sd = await waitForMode([MODE.SHUTDOWN], 60000, 'Shutdown');
        if (!sd) { testLog('Did not enter Shutdown', 'fail'); return false; }
        testLog('Shutdown confirmed', 'pass');
        await sleep(2000);
        await setSwitchesAPI({ startStop: 'start' });
        testLog('Started. Waiting for Refrigeration...', 'step');

        var mr = await waitForMode([MODE.REFRIG,MODE.REFRIG_RAMP], 120000, 'Refrigeration');
        if (!mr) { testLog('Did not enter Refrig', 'fail'); return false; }
        testLog('Entered ' + (MODE_NAME[mr.mode]||mr.mode), 'pass');
        await sleep(6000); await snap('Refrig entry');

        if (!await verifyOut('Refrig', {fan:true,fanSpeed:80,door:true,doorClose:true,climacell:true,heat:false,green:true,yellow:true,red:false})) ok = false;

        // Stage sequencing ON
        testLog('Waiting for stages (PID building, up to 12 min)...', 'step');
        var found = 0, onTimes = {}, t0 = Date.now();
        while (Date.now()-t0 < 720000 && found < 6) {
          if (testAbort) throw new Error('Aborted');
          var eq = await getEquipmentStatus();
          for (var s = 1; s <= 6; s++) {
            if (!(s in onTimes) && eqOn(eq, EQI.RS1+s-1)) {
              onTimes[s] = Math.round((Date.now()-t0)/1000);
              found++;
              testLog('Stage ' + s + ' ON at t+' + onTimes[s] + 's', 'pass');
            }
          }
          if (found < 6) {
            var el = Math.round((Date.now()-t0)/1000);
            if (el % 60 < 4) testLog('  ... ' + found + '/6 stages', 'step');
            await sleep(3000);
          }
        }
        if (!chk(found >= 1, found + '/6 stages activated', 'No stages in 12 min')) ok = false;
        if (found < 6) testLog('Only ' + found + '/6 (may need more time)', 'warn');

        // Test first 3 active stage proofs
        for (var s = 1; s <= Math.min(found, 3); s++) {
          var eq = await getEquipmentStatus();
          if (eqOn(eq, EQI.RS1+s-1)) {
            if (!await testProof('ex2', s-1, 'Refrig Stage '+s, EQI.RS1+s-1, 60000, false)) ok = false;
          }
        }

        // Humid2 timer test (60s on/off)
        testLog('Checking Humid2 timer cycling...', 'step');
        var h2 = [], eq;
        for (var i = 0; i < 8; i++) { eq = await getEquipmentStatus(); h2.push(eqOn(eq,EQI.H2_PUMP)?'ON':'OFF'); await sleep(20000); }
        var ch = 0; for (var i = 1; i < h2.length; i++) { if (h2[i]!==h2[i-1]) ch++; }
        testLog('Humid2 over ~2.5min: ' + h2.join(','), 'step');
        if (!chk(ch >= 1, 'Humid2 cycling (' + ch + ' transitions) \u2714', 'No Humid2 cycling')) ok = false;

        // Humid2 proof — check head (stays on entire timed cycle) not pump (cycles)
        eq = await getEquipmentStatus();
        if (eqOn(eq,EQI.H2_HEAD)) { if (!await testProof('main',6,'Humid2',EQI.H2_PUMP,60000,false,EQI.H2_HEAD)) ok = false; }
        else testLog('Humid2 head not active, skip proof', 'warn');

        return ok;
      } finally { await restoreSensors(baseline); }
    }
  },

  // --- Phase 3: DEFROST (wait up to 60 min) ---
  {
    id: 'p3_defrost',
    name: '3. Defrost Cycle + Proofs',
    desc: 'Wait in refrig for defrost (up to 60 min), verify outputs, test defrost proofs',
    run: async function() {
      var ok = true; await setPhysics(false); var baseline = await saveSensorBaseline();
      try {
        testLog('\u2550\u2550 PHASE 3: DEFROST CYCLE \u2550\u2550', 'info');
        var sp = await getSetpoints();
        await setSensors({ plenumTemp1:f2c(sp.tempSetpoint+4), plenumTemp2:f2c(sp.tempSetpoint+4)+0.1, outsideTemp:f2c(60), outsideHumid:70, _override:'plenumTemp' });

        // Stop/Start cycle to enter refrig directly
        testLog('Stop/Start cycle to enter Refrigeration...', 'step');
        await setSwitchesAPI({ startStop: 'shutdown' });
        await waitForMode([MODE.SHUTDOWN], 60000, 'Shutdown');
        await sleep(2000);
        await setSwitchesAPI({ startStop: 'start' });

        var mr = await waitForMode([MODE.REFRIG,MODE.REFRIG_RAMP], 120000, 'Refrigeration');
        if (!mr) { testLog('Not in Refrig', 'fail'); return false; }

        testLog('Waiting for defrost (up to 60 min)...', 'step');
        var t0 = Date.now(), found = false;
        while (Date.now()-t0 < 3600000) {
          if (testAbort) throw new Error('Aborted');
          var m = await getCurrentMode();
          if (m.mode === MODE.DEFROST) { found = true; testLog('Defrost started after ' + Math.round((Date.now()-t0)/60000) + ' min!', 'pass'); break; }
          if ((Date.now()-t0) % 300000 < 4000) { testLog('  Still waiting... ' + Math.round((Date.now()-t0)/60000) + ' min', 'step'); await snap('Defrost wait'); }
          await sleep(3000);
        }
        if (!found) { testLog('No defrost in 60 min', 'fail'); return false; }

        await sleep(5000); await snap('Defrost active');
        if (!await verifyOut('Defrost', {fan:true,fanSpeed:100,door:true,doorClose:true,green:true,yellow:true,red:false})) ok = false;

        var eq = await getEquipmentStatus();
        if (!chk(eqOn(eq,EQI.DEFR1), 'Defrost 1 ON \u2714', 'Defrost 1 OFF')) ok = false;
        if (!chk(eqOn(eq,EQI.DEFR2), 'Defrost 2 ON \u2714', 'Defrost 2 OFF')) ok = false;
        var anyStage = false;
        for (var s = 0; s < 6; s++) if (eqOn(eq,EQI.RS1+s)) anyStage = true;
        if (!chk(!anyStage, 'Refrig stages OFF in defrost \u2714', 'Stages still ON')) ok = false;

        if (!await testProof('ex2',6,'Defrost 1',EQI.DEFR1,60000,false)) ok = false;
        var m2 = await getCurrentMode();
        if (m2.mode === MODE.DEFROST) { if (!await testProof('ex2',7,'Defrost 2',EQI.DEFR2,60000,false)) ok = false; }
        else testLog('Defrost ended before testing Defrost 2', 'warn');

        testLog('Waiting for defrost to end (up to 15 min)...', 'step');
        var back = await waitFor('back to refrig', async function() { var m = await getCurrentMode(); return m.mode===MODE.REFRIG?m:null; }, 900000, 5000);
        if (back) testLog('Returned to Refrig', 'pass'); else testLog('Did not return to Refrig', 'warn');
        return ok;
      } finally { await restoreSensors(baseline); }
    }
  },

  // --- Phase 4: RECIRC MODE (fail all stages) ---
  {
    id: 'p4_recirc',
    name: '4. Recirc Mode (Refrig Main Fail)',
    desc: 'Fail all refrig stages to force recirc fallback, test humid3 auto',
    run: async function() {
      var ok = true; await setPhysics(false); var baseline = await saveSensorBaseline();
      try {
        testLog('\u2550\u2550 PHASE 4: RECIRCULATION MODE \u2550\u2550', 'info');
        var sp = await getSetpoints();
        await setSensors({ plenumTemp1:f2c(sp.tempSetpoint+4), plenumTemp2:f2c(sp.tempSetpoint+4)+0.1, outsideTemp:f2c(60), outsideHumid:70, _override:'plenumTemp' });

        // Stop/Start cycle to enter refrig directly
        testLog('Stop/Start cycle to force direct refrig entry...', 'step');
        await setSwitchesAPI({ startStop: 'shutdown' });
        var sd = await waitForMode([MODE.SHUTDOWN], 60000, 'Shutdown');
        if (!sd) { testLog('Did not enter Shutdown', 'fail'); return false; }
        testLog('Shutdown confirmed', 'pass');
        await sleep(2000);
        await setSwitchesAPI({ startStop: 'start' });
        testLog('Started. Waiting for Refrigeration...', 'step');

        var mr = await waitForMode([MODE.REFRIG,MODE.REFRIG_RAMP], 120000, 'Refrigeration');
        if (!mr) { testLog('Not in Refrig', 'fail'); return false; }

        // Wait for ALL 6 stages to be active (check every 60s after initial 10 min)
        testLog('Waiting 10 min for all 6 stages to activate...', 'step');
        await sleep(600000); // 10 minute initial wait
        var allOn = false;
        for (var attempt = 0; attempt < 15; attempt++) {
          if (testAbort) throw new Error('Aborted');
          var eq = await getEquipmentStatus();
          var count = 0;
          for (var s = 0; s < 6; s++) if (eqOn(eq,EQI.RS1+s)) count++;
          testLog('  Stage check: ' + count + '/6 active', 'step');
          if (count >= 6) { allOn = true; break; }
          testLog('  Not all stages on, waiting 1 more min...', 'step');
          await sleep(60000);
        }

        if (!allOn) {
          testLog('Not all 6 stages active after 25 min', 'fail');
          return false;
        }
        testLog('All 6 stages ON!', 'pass');

        // Open all 6 stage DIs and wait > 1 min for recirc
        testLog('Failing all 6 stage proofs...', 'step');
        for (var s = 0; s < 6; s++) await setDigitalInput('ex2', s, false);
        testLog('All proofs OPENED. Waiting >1 min for recirc...', 'step');
        await sleep(75000); // wait 75s (> 1 min alarm delay)
        var rr = await waitForMode([MODE.RECIRC], 90000, 'Recirc fallback');
        if (!rr) {
          testLog('No recirc fallback', 'fail');
          for (var s = 0; s < 6; s++) await setDigitalInput('ex2', s, true);
          await clearAlarmsBridge(); return false;
        }
        testLog('Fell to Recirculation', 'pass');

        await sleep(6000); await snap('Recirc steady');
        if (!await verifyOut('Recirc', {fan:true,fanSpeed:50,door:true,doorClose:true,climacell:true,heat:false,green:true,yellow:true,red:false})) ok = false;

        // Humid3 auto test (inline proof — do NOT clearAlarms until after all humid3 checks,
        // because clearing alarms would also clear the refrig stage alarms keeping us in recirc)
        testLog('Testing Humid3 auto mode...', 'step');
        await setSensors({ plenumHumid:80, _override:'plenumHumid' });
        testLog('Plenum humid 80% (SP=95%, 15% below). Waiting...', 'step');
        var h3 = await waitFor('h3 on', async function() { var eq = await getEquipmentStatus(); return eqOn(eq,EQI.H3_PUMP)?true:null; }, 90000, 3000);
        if (h3) {
          testLog('Humid3 (auto) ON at 80%', 'pass');

          // Inline proof test: open DI, wait for output OFF — do NOT clear alarms
          // (refrig alarms are already present, so we count alarms before/after to detect a new one)
          testLog('Testing Humid3 proof failure...', 'step');
          var alBefore = await getAlarms(); var alCountBefore = 0;
          for (var ai = 0; ai < alBefore.length; ai++) { if (alBefore[ai] && alBefore[ai].length > 0) alCountBefore++; }
          await setDigitalInput('ex1', 0, false);
          testLog('Humid3 proof OPENED. Waiting for output OFF...', 'step');
          var h3off2 = await waitFor('h3 proof off', async function() {
            var eq = await getEquipmentStatus(); return !eqOn(eq,EQI.H3_PUMP)?true:null;
          }, 90000, 3000);
          if (h3off2) { testLog('Humid3 output OFF after proof fail', 'pass'); }
          else { testLog('Humid3 still ON after proof fail', 'fail'); ok = false; }
          // Check for new alarm (count increased)
          var alAfter = await getAlarms(); var alCountAfter = 0;
          for (var ai = 0; ai < alAfter.length; ai++) { if (alAfter[ai] && alAfter[ai].length > 0) alCountAfter++; }
          if (alCountAfter > alCountBefore) testLog('New alarm detected (count ' + alCountBefore + ' -> ' + alCountAfter + ')', 'pass');
          else testLog('No new alarm detected (count still ' + alCountAfter + ')', 'warn');
          var eq = await getEquipmentStatus();
          if (!chk(!eqOn(eq,EQI.H3_PUMP), 'Humid3 output OFF', 'Humid3 still ON')) ok = false;
          if (!chk(!eqOn(eq,EQI.H3_HEAD), 'Humid3 head OFF', 'Humid3 head still ON')) ok = false;
          chk(eqOn(eq,EQI.FAN_OUT), 'Fan still running', 'Fan stopped unexpectedly');
          await setDigitalInput('ex1', 0, true);
          testLog('Humid3 proof restored (refrig alarm kept)', 'step');
          await sleep(3000);
        } else { testLog('Humid3 did not activate', 'fail'); ok = false; }

        // Humid above SP → humid3 should turn off (refrig alarm still active, still in recirc)
        await setSensors({ plenumHumid:100, _override:'plenumHumid' });
        testLog('Plenum humid 100%. Waiting for humid3 off...', 'step');
        var h3off = await waitFor('h3 off', async function() { var eq = await getEquipmentStatus(); return !eqOn(eq,EQI.H3_PUMP)?true:null; }, 90000, 3000);
        if (h3off) testLog('Humid3 OFF above SP', 'pass'); else { testLog('Humid3 still on', 'fail'); ok = false; }

        // NOW clean up: restore stage DIs and clear all alarms
        for (var s = 0; s < 6; s++) await setDigitalInput('ex2', s, true);
        await clearAlarmsBridge();
        return ok;
      } finally { await restoreSensors(baseline); }
    }
  },

  // --- Phase 5: HEATING MODE ---
  {
    id: 'p5_heating',
    name: '5. Heating Mode + Proofs',
    desc: 'Drop plenum 15F below SP, verify heating, test heat + cavity heat proofs',
    run: async function() {
      var ok = true; await setPhysics(false); var baseline = await saveSensorBaseline();
      try {
        var sp = await getSetpoints();
        testLog('\u2550\u2550 PHASE 5: HEATING MODE \u2550\u2550', 'info');

        // Ensure cavity heat is enabled: Mode 2 = ON (uses outside temp), Diff=-5°C
        // Threshold = TempSet + Diff ≈ 0.6 + (-5) = -4.4°C ≈ 24°F → ON when outside < 24°F
        testLog('Configuring cavity heat: Mode=ON, Diff=-5°C...', 'step');
        await fetch(BRIDGE + '/iot/PostSave.jsp', {
          method:'POST', headers:{'Content-Type':'application/json'},
          body: JSON.stringify({ p1Misc:'AS2', selCavityCtrl:'2', cavityDiff:'-5.0' })
        });
        await sleep(3000);

        var pF = sp.tempSetpoint - 15, oF = 20;
        await setSensors({ plenumTemp1:f2c(pF), plenumTemp2:f2c(pF)+0.1, outsideTemp:f2c(oF), outsideHumid:60, _override:'plenumTemp' });
        testLog('Sensors: plenum=' + pF + 'F, outside=' + oF + 'F', 'step');

        var mr = await waitForMode([MODE.HEATING], 120000, 'Heating');
        if (!mr) { testLog('Did not enter Heating', 'fail'); return false; }
        testLog('Entered Heating', 'pass');
        await sleep(6000); await snap('Heating steady');

        if (!await verifyOut('Heating', {fan:true,fanSpeed:25,door:true,doorClose:true,climacell:true,heat:true,green:true,yellow:false,red:false})) ok = false;

        // Cavity heat ON (outside < ~24F with Diff=-5°C)
        // Wait up to 30s for cavity heat to activate (firmware processes every control cycle)
        var cavOk = await waitFor('cavity heat', async function() { var eq = await getEquipmentStatus(); return eqOn(eq,EQI.CAVH_OUT)?true:null; }, 30000, 3000);
        if (cavOk) {
          testLog('Cavity heat ON (outside '+oF+'F<24F) \u2714', 'pass');
        } else {
          // Check if I/O is configured
          var eqData = await fetchBridge('/iot/ws/equipment-data');
          var outCfg = eqData.outputConfig || [];
          if (outCfg[5] === '-1' || outCfg[5] === undefined) {
            testLog('Cavity heat I/O not configured (no output port assigned)', 'warn');
          } else {
            testLog('Cavity heat OFF (expected ON at '+oF+'F)', 'fail');
          }
          ok = false;
        }

        if (!await testProof('ex1',2,'Heat',EQI.HEAT_OUT,60000,false)) ok = false;

        var eq = await getEquipmentStatus();
        if (eqOn(eq,EQI.CAVH_OUT)) { if (!await testProof('ex1',3,'Cavity Heat',EQI.CAVH_OUT,60000,false)) ok = false; }
        else testLog('Cavity heat not active, skip proof', 'warn');

        // Verify heating stops at SP
        testLog('Raising plenum to SP...', 'step');
        await setSensors({ plenumTemp1:f2c(sp.tempSetpoint), plenumTemp2:f2c(sp.tempSetpoint)+0.1, _override:'plenumTemp' });
        var hs = await waitFor('heat ends', async function() { var eq = await getEquipmentStatus(); return !eqOn(eq,EQI.HEAT_OUT)?true:null; }, 60000, 2000);
        if (hs) testLog('Heat OFF at SP \u2714', 'pass'); else { testLog('Heat still ON at SP', 'fail'); ok = false; }
        return ok;
      } finally { await restoreSensors(baseline); }
    }
  },

  // --- Phase 6: CO2 PURGE ---
  {
    id: 'p6_co2',
    name: '6. CO2 Purge + Recovery',
    desc: 'Trigger CO2 purge, verify fan+doors 100%, verify return to previous mode',
    run: async function() {
      var ok = true; await setPhysics(false); var baseline = await saveSensorBaseline();
      try {
        testLog('\u2550\u2550 PHASE 6: CO2 PURGE \u2550\u2550', 'info');
        var sp = await getSetpoints();
        // CO2 purge must be tested from Refrigeration (does not activate in Cooling)
        var pF = sp.tempSetpoint + 4, oF = 60;
        await setSensors({ plenumTemp1:f2c(pF), plenumTemp2:f2c(pF)+0.1, outsideTemp:f2c(oF), outsideHumid:70, co2:400, _override:'plenumTemp' });

        // Stop/Start cycle to enter refrig directly
        testLog('Stop/Start cycle to enter Refrigeration...', 'step');
        await setSwitchesAPI({ startStop: 'shutdown' });
        await waitForMode([MODE.SHUTDOWN], 60000, 'Shutdown');
        await sleep(2000);
        await setSwitchesAPI({ startStop: 'start' });

        var mr = await waitForMode([MODE.REFRIG,MODE.REFRIG_RAMP], 120000, 'Refrigeration');
        if (!mr) { testLog('Could not get to refrig baseline', 'fail'); return false; }

        await sleep(6000);
        var pre = await getSetpoints(), preMode = await getCurrentMode();
        testLog('Pre-purge: mode=' + (MODE_NAME[preMode.mode]||preMode.mode) + ' fan=' + pre.fanSpeed + '% cool=' + pre.coolOutput + '%', 'step');

        await setSensors({ co2:4000, outsideTemp:f2c(45), _override:'plenumTemp' });
        testLog('CO2=4000ppm, outside=45F. Waiting for purge...', 'step');

        var pr = await waitForMode([MODE.CO2_PURGE], 120000, 'CO2 Purge');
        if (!pr) { testLog('CO2 purge did not start', 'fail'); return false; }
        testLog('CO2 Purge started!', 'pass');
        await sleep(5000); await snap('CO2 Purge');

        var spP = await getSetpoints();
        if (!chk(spP.fanSpeed >= 95, 'Fan 100% in purge: '+spP.fanSpeed+'% \u2714', 'Fan not 100%: '+spP.fanSpeed+'%')) ok = false;
        if (!chk(spP.coolOutput >= 95, 'Door 100% in purge: '+spP.coolOutput+'% \u2714', 'Door not 100%: '+spP.coolOutput+'%')) ok = false;

        await setSensors({ co2:400, _override:'plenumTemp' });
        testLog('Waiting for purge to end (up to 7 min)...', 'step');

        var pe = await waitFor('purge end', async function() { var m = await getCurrentMode(); return m.mode!==MODE.CO2_PURGE?m:null; }, 420000, 5000);
        if (pe) {
          testLog('Purge ended: ' + (MODE_NAME[pe.mode]||pe.mode), 'pass');
          await sleep(5000);
          var post = await getSetpoints();
          testLog('Post-purge: fan=' + post.fanSpeed + '% cool=' + post.coolOutput + '%', 'step');
          var fd = Math.abs(post.fanSpeed - pre.fanSpeed);
          if (!chk(fd <= 10, 'Fan restored to ~' + pre.fanSpeed + '% (got '+post.fanSpeed+'%) \u2714', 'Fan not restored: '+post.fanSpeed+'%')) ok = false;
        } else { testLog('Purge did not end in 7 min', 'fail'); ok = false; }
        return ok;
      } finally { await restoreSensors(baseline); }
    }
  },

  // --- Phase 7: AUX1 HEAT TAPE ---
  {
    id: 'p7_aux',
    name: '7. Aux1 Heat Tape + Proof',
    desc: 'Verify Aux1 ON below 28F, OFF above, test proof',
    run: async function() {
      var ok = true; await setPhysics(false); var baseline = await saveSensorBaseline();
      try {
        testLog('\u2550\u2550 PHASE 7: AUX1 DOOR HEAT TAPE \u2550\u2550', 'info');
        var sp = await getSetpoints();
        await setSensors({ plenumTemp1:f2c(sp.tempSetpoint+3), plenumTemp2:f2c(sp.tempSetpoint+3)+0.1, outsideTemp:f2c(20), outsideHumid:55, _override:'plenumTemp' });
        await waitForMode([MODE.COOLING,MODE.COOL_RAMP,MODE.HEATING], 90000, 'running mode');
        await sleep(5000);

        testLog('Outside=20F (<28F threshold)', 'step');
        var a1 = await waitFor('Aux1 ON', async function() { var eq = await getEquipmentStatus(); return eqOn(eq,EQI.AUX1)?true:null; }, 60000, 3000);
        if (!chk(!!a1, 'Aux1 ON at 20F \u2714', 'Aux1 not ON')) ok = false;
        if (a1) { if (!await testProof('ex1',4,'Aux1',EQI.AUX1,60000,false)) ok = false; }

        await setSensors({ outsideTemp:f2c(35), _override:'plenumTemp' });
        testLog('Outside=35F (>28F)', 'step');
        var a1off = await waitFor('Aux1 OFF', async function() { var eq = await getEquipmentStatus(); return !eqOn(eq,EQI.AUX1)?true:null; }, 60000, 3000);
        if (!chk(!!a1off, 'Aux1 OFF at 35F \u2714', 'Aux1 still ON')) ok = false;
        return ok;
      } finally { await restoreSensors(baseline); }
    }
  },

  // --- Phase 8: SYSTEM FAILURE TESTS ---
  {
    id: 'p8_failures',
    name: '8. System Failure Tests',
    desc: 'Fan proof (1 min), airflow (immediate), low temp (immediate)',
    run: async function() {
      var ok = true; await setPhysics(false); var baseline = await saveSensorBaseline();
      try {
        testLog('\u2550\u2550 PHASE 8: SYSTEM FAILURE TESTS \u2550\u2550', 'info');
        var sp = await getSetpoints();
        await setSensors({ plenumTemp1:f2c(sp.tempSetpoint+3), plenumTemp2:f2c(sp.tempSetpoint+3)+0.1, outsideTemp:f2c(30), outsideHumid:55, _override:'plenumTemp' });
        var mr = await waitForMode([MODE.COOLING,MODE.COOL_RAMP], 90000, 'Cooling');
        if (!mr) { testLog('Cannot get to Cooling', 'fail'); return false; }

        var eq = await getEquipmentStatus();
        if (!eqOn(eq,EQI.FAN_OUT)) await waitFor('fan', async function() { var eq = await getEquipmentStatus(); return eqOn(eq,EQI.FAN_OUT)?true:null; }, 30000, 2000);

        // 8a. Fan proof (1 min delay -> system failure)
        testLog('\u2500\u2500 8a. Fan Proof \u2500\u2500', 'info');
        if (!await testProof('main',2,'Fan Proof',-1,60000,true)) ok = false;

        await sleep(5000);
        await setSensors({ plenumTemp1:f2c(sp.tempSetpoint+3), plenumTemp2:f2c(sp.tempSetpoint+3)+0.1, _override:'plenumTemp' });
        await waitForMode([MODE.COOLING,MODE.COOL_RAMP], 90000, 'Cooling recovery');

        // 8b. Airflow (IMMEDIATE)
        testLog('\u2500\u2500 8b. Airflow Restriction (Immediate) \u2500\u2500', 'info');
        eq = await getEquipmentStatus();
        if (eqOn(eq,EQI.FAN_OUT)) {
          await setDigitalInput('main', 8, false);
          testLog('Airflow OPENED. Checking immediate failure...', 'step');
          var fr = await waitForMode([MODE.FAILURE], 20000, 'Immediate Failure');
          if (!chk(!!fr, 'FAILURE immediate \u2714', 'No immediate failure')) ok = false;
          if (fr) {
            eq = await getEquipmentStatus();
            chk(!eqOn(eq,EQI.FAN_OUT), 'Fan OFF \u2714', 'Fan ON'); chk(eqOn(eq,EQI.RED), 'Red ON \u2714', 'Red OFF');
            chk(!eqOn(eq,EQI.GREEN), 'Green OFF \u2714', 'Green ON'); chk(!eqOn(eq,EQI.YELLOW), 'Yellow OFF \u2714', 'Yellow ON');
          }
          await setDigitalInput('main', 8, true); await sleep(2000); await clearAlarmsBridge();
          await waitFor('recovery', async function() { var m = await getCurrentMode(); return m.mode!==MODE.FAILURE?m:null; }, 90000, 3000);
        }

        await sleep(5000);
        await setSensors({ plenumTemp1:f2c(sp.tempSetpoint+3), plenumTemp2:f2c(sp.tempSetpoint+3)+0.1, _override:'plenumTemp' });
        await waitForMode([MODE.COOLING,MODE.COOL_RAMP], 90000, 'Cooling recovery 2');

        // 8c. Low Temp (IMMEDIATE)
        testLog('\u2500\u2500 8c. Low Temp (Immediate) \u2500\u2500', 'info');
        await setDigitalInput('main', 9, false);
        testLog('Low Temp OPENED...', 'step');
        var lt = await waitForMode([MODE.FAILURE], 20000, 'Low Temp Failure');
        if (!chk(!!lt, 'FAILURE on Low Temp \u2714', 'No Low Temp failure')) ok = false;

        await setDigitalInput('main', 9, true); await sleep(2000); await clearAlarmsBridge();
        var ltRec = await waitFor('recovery', async function() { var m = await getCurrentMode(); return m.mode!==MODE.FAILURE?m:null; }, 90000, 3000);
        if (ltRec) testLog('Recovered from Low Temp', 'pass'); else { testLog('No recovery', 'fail'); ok = false; }
        return ok;
      } finally { await restoreSensors(baseline); }
    }
  },

  // --- Phase 9: POWER FAILURE ---
  {
    id: 'p9_power',
    name: '9. Power Failure + Recovery',
    desc: 'Close power DI (inverted: closed=fault), verify alarm, auto-recovery, ClearAlarm',
    run: async function() {
      var ok = true; await setPhysics(false); var baseline = await saveSensorBaseline();
      try {
        testLog('\u2550\u2550 PHASE 9: POWER FAILURE \u2550\u2550', 'info');
        var sp = await getSetpoints();
        await setSensors({ plenumTemp1:f2c(sp.tempSetpoint+3), plenumTemp2:f2c(sp.tempSetpoint+3)+0.1, outsideTemp:f2c(30), outsideHumid:55, _override:'plenumTemp' });
        await waitForMode([MODE.COOLING,MODE.COOL_RAMP], 90000, 'Cooling');
        await sleep(5000);

        testLog('Closing Power input (main port 0, inverted: closed=fault)...', 'step');
        await setDigitalInput('main', 0, true);

        var al = await waitFor('power alarm', async function() {
          var a = await getAlarms();
          for (var i = 0; i < a.length; i++) if (a[i] && (a[i].includes('Power')||a[i].includes('POWER'))) return a[i];
          return null;
        }, 90000, 3000);
        if (!chk(!!al, 'Power alarm: '+al, 'No power alarm')) ok = false;

        testLog('Restoring power (open DI)...', 'step');
        await setDigitalInput('main', 0, false);
        await sleep(5000);

        var alarms = await getAlarms();
        chk(alarms.length > 0, 'Alarm persists after restore (needs ClearAlarm)', 'Alarm auto-cleared');

        await clearAlarmsBridge();
        alarms = await getAlarms();
        var gone = true;
        for (var i = 0; i < alarms.length; i++) if (alarms[i] && (alarms[i].includes('Power')||alarms[i].includes('POWER'))) gone = false;
        if (!chk(gone, 'Power alarm cleared \u2714', 'Power alarm persists')) ok = false;
        return ok;
      } finally { await restoreSensors(baseline); }
    }
  },

  // --- Phase 10: REMOTE STANDBY ---
  {
    id: 'p10_remote_standby',
    name: '10. Remote Standby',
    desc: 'Close Remote Standby DI, verify mode + all off + yellow light',
    run: async function() {
      var ok = true; await setPhysics(false); var baseline = await saveSensorBaseline();
      try {
        testLog('\u2550\u2550 PHASE 10: REMOTE STANDBY \u2550\u2550', 'info');
        var sp = await getSetpoints();
        await setSensors({ plenumTemp1:f2c(sp.tempSetpoint+3), plenumTemp2:f2c(sp.tempSetpoint+3)+0.1, outsideTemp:f2c(30), outsideHumid:55, _override:'plenumTemp' });
        await waitForMode([MODE.COOLING,MODE.COOL_RAMP], 90000, 'Cooling');
        await sleep(5000);

        testLog('Closing Remote Standby (main port 1, inverted)...', 'step');
        await setDigitalInput('main', 1, true);

        var rs = await waitForMode([MODE.REM_STANDBY], 60000, 'Remote Standby');
        if (!chk(!!rs, 'Remote Standby entered \u2714', 'Did not enter Rem Stby')) ok = false;
        await sleep(5000); await snap('Remote Standby');

        if (!await verifyOut('Remote Standby', {fan:false,door:true,doorClose:true,climacell:false,heat:false,green:false,yellow:true,red:false})) ok = false;

        testLog('Opening Remote Standby DI...', 'step');
        await setDigitalInput('main', 1, false);

        var rec = await waitFor('recovery', async function() { var m = await getCurrentMode(); return m.mode!==MODE.REM_STANDBY&&m.mode!==MODE.STANDBY?m:null; }, 120000, 3000);
        if (rec) testLog('Recovered to ' + (MODE_NAME[rec.mode]||rec.mode), 'pass');
        else { testLog('No recovery from Rem Stby', 'fail'); ok = false; }
        return ok;
      } finally { await restoreSensors(baseline); }
    }
  },

  // --- Phase 11: FAN MANUAL ---
  {
    id: 'p11_fan_manual',
    name: '11. Fan Manual Mode',
    desc: 'Switch fan to manual, verify 100%, all 3 lights solid ON',
    run: async function() {
      var ok = true; await setPhysics(false);
      try {
        testLog('\u2550\u2550 PHASE 11: FAN MANUAL \u2550\u2550', 'info');
        await setSwitchesAPI({ fan: 'manual' });
        testLog('Fan switch -> MANUAL', 'step');

        var mr = await waitForMode([MODE.FAN_MANUAL], 60000, 'Fan Manual');
        if (!chk(!!mr, 'Fan Manual entered \u2714', 'Did not enter Fan Manual')) ok = false;
        await sleep(5000); await snap('Fan Manual');

        if (!await verifyOut('Fan Manual', {fan:true,fanSpeed:100,green:true,yellow:true,red:true})) ok = false;

        // Confirm lights stable (not flashing)
        await sleep(3000);
        var eq = await getEquipmentStatus();
        if (!chk(eqOn(eq,EQI.GREEN)&&eqOn(eq,EQI.YELLOW)&&eqOn(eq,EQI.RED), 'All 3 lights still solid \u2714', 'Lights may be flashing')) ok = false;

        await setSwitchesAPI({ fan: 'auto' });
        testLog('Fan switch -> AUTO', 'step');
        await sleep(5000);
        return ok;
      } finally {}
    }
  },

  // --- Phase 12: SHUTDOWN ---
  {
    id: 'p12_shutdown',
    name: '12. Shutdown',
    desc: 'Switch Start to Stop, verify Red solid, everything off',
    run: async function() {
      var ok = true; await setPhysics(false);
      try {
        testLog('\u2550\u2550 PHASE 12: SHUTDOWN \u2550\u2550', 'info');
        await setSwitchesAPI({ startStop: 'shutdown' });
        testLog('Start/Stop -> SHUTDOWN', 'step');

        var mr = await waitForMode([MODE.SHUTDOWN], 60000, 'Shutdown');
        if (!chk(!!mr, 'Shutdown entered \u2714', 'Did not enter Shutdown')) ok = false;
        await sleep(5000); await snap('Shutdown');

        if (!await verifyOut('Shutdown', {fan:false,door:true,doorClose:true,climacell:false,heat:false,green:false,yellow:false,red:true})) ok = false;

        await setSwitchesAPI({ startStop: 'start' });
        testLog('Start/Stop -> START', 'step');
        var rec = await waitFor('resume', async function() { var m = await getCurrentMode(); return m.mode!==MODE.SHUTDOWN&&m.mode!==MODE.OFF?m:null; }, 120000, 3000);
        if (rec) testLog('Resumed to ' + (MODE_NAME[rec.mode]||rec.mode), 'pass');
        else { testLog('Did not resume', 'fail'); ok = false; }
        return ok;
      } finally {}
    }
  },

  // --- Phase 13: REFRIG STAGE SEQUENCING (detailed) ---
  {
    id: 'p13_staging',
    name: '13. Refrig Stage Sequencing',
    desc: 'Watch stages ON 1->6 as PID builds, then OFF 6->1 as output drops',
    run: async function() {
      var ok = true; await setPhysics(false); var baseline = await saveSensorBaseline();
      try {
        testLog('\u2550\u2550 PHASE 13: REFRIG STAGE SEQUENCING \u2550\u2550', 'info');
        var sp = await getSetpoints();
        await setSensors({ plenumTemp1:f2c(sp.tempSetpoint+4), plenumTemp2:f2c(sp.tempSetpoint+4)+0.1, outsideTemp:f2c(60), outsideHumid:70, _override:'plenumTemp' });

        // Stop/Start cycle to enter refrig directly
        testLog('Stop/Start cycle to enter Refrigeration...', 'step');
        await setSwitchesAPI({ startStop: 'shutdown' });
        await waitForMode([MODE.SHUTDOWN], 60000, 'Shutdown');
        await sleep(2000);
        await setSwitchesAPI({ startStop: 'start' });

        var mr = await waitForMode([MODE.REFRIG,MODE.REFRIG_RAMP], 120000, 'Refrigeration');
        if (!mr) { testLog('Cannot get to Refrig', 'fail'); return false; }

        // ON sequencing
        testLog('Watching stages ON (S1@20%, S2@30%...S6@70%)...', 'step');
        var order = [], onT = {}, t0 = Date.now();
        while (Date.now()-t0 < 720000 && order.length < 6) {
          if (testAbort) throw new Error('Aborted');
          var eq = await getEquipmentStatus();
          for (var s = 1; s <= 6; s++) {
            if (order.indexOf(s)===-1 && eqOn(eq,EQI.RS1+s-1)) {
              onT[s] = Math.round((Date.now()-t0)/1000);
              order.push(s);
              testLog('Stage '+s+' ON (#'+order.length+') t+'+onT[s]+'s', order.length===s?'pass':'warn');
            }
          }
          if (order.length < 6) { var el = Math.round((Date.now()-t0)/1000); if (el%60<4) testLog('  ... '+order.length+'/6', 'step'); await sleep(3000); }
        }

        var okOrd = true;
        for (var i = 0; i < order.length; i++) if (order[i]!==i+1) okOrd = false;
        if (!chk(order.length >= 6, 'All 6 stages ON', 'Only '+order.length+'/6')) ok = false;
        if (!chk(okOrd, 'Order correct: '+order.join('->') + ' \u2714', 'Order wrong: '+order.join('->'))) ok = false;

        // OFF sequencing
        testLog('Dropping plenum below SP to watch stages OFF...', 'step');
        await setSensors({ plenumTemp1:f2c(sp.tempSetpoint-2), plenumTemp2:f2c(sp.tempSetpoint-2)+0.1, _override:'plenumTemp' });

        var offOrd = [], t0off = Date.now(), prev = order.slice();
        while (Date.now()-t0off < 720000 && offOrd.length < prev.length) {
          if (testAbort) throw new Error('Aborted');
          var eq = await getEquipmentStatus();
          for (var i = prev.length-1; i >= 0; i--) {
            var s = prev[i];
            if (offOrd.indexOf(s)===-1 && !eqOn(eq,EQI.RS1+s-1)) {
              offOrd.push(s);
              testLog('Stage '+s+' OFF (#'+offOrd.length+') t+'+Math.round((Date.now()-t0off)/1000)+'s', 'step');
            }
          }
          if (offOrd.length < prev.length) { if ((Date.now()-t0off)%60000<4000) testLog('  ... '+offOrd.length+'/'+prev.length+' off', 'step'); await sleep(3000); }
        }

        var okOff = true;
        for (var i = 0; i < offOrd.length; i++) if (offOrd[i]!==prev.length-i) okOff = false;
        if (!chk(offOrd.length >= prev.length, 'All stages OFF', 'Only '+offOrd.length+'/'+prev.length+' off')) ok = false;
        if (!chk(okOff, 'OFF order: '+offOrd.join('->') + ' \u2714', 'OFF order wrong: '+offOrd.join('->'))) ok = false;
        return ok;
      } finally { await restoreSensors(baseline); }
    }
  },

  // --- Phase 14: STANDBY ---
  {
    id: 'p14_standby',
    name: '14. Standby Mode',
    desc: 'Plenum at SP with outside warm, verify standby outputs',
    run: async function() {
      var ok = true; await setPhysics(false); var baseline = await saveSensorBaseline();
      try {
        testLog('\u2550\u2550 PHASE 14: STANDBY MODE \u2550\u2550', 'info');
        var sp = await getSetpoints();
        await setSensors({ plenumTemp1:f2c(sp.tempSetpoint-1), plenumTemp2:f2c(sp.tempSetpoint-1)+0.1, outsideTemp:f2c(sp.tempSetpoint+15), outsideHumid:50, _override:'plenumTemp' });
        testLog('Plenum at SP-1, outside warm. Waiting...', 'step');

        var mr = await waitForMode([MODE.STANDBY], 180000, 'Standby');
        if (!mr) { var c = await getCurrentMode(); testLog('No Standby ('+MODE_NAME[c.mode]+'). May need run clock.', 'fail'); return false; }
        testLog('Standby entered \u2714', 'pass');
        await sleep(5000); await snap('Standby');

        if (!await verifyOut('Standby', {fan:false,door:true,doorClose:true,climacell:false,heat:false,green:false,yellow:true,red:false})) ok = false;
        return ok;
      } finally { await restoreSensors(baseline); }
    }
  }
];

// ── Build test cards ──
(function buildTestCards() {
  var container = document.getElementById('testCards');
  if (!container) return;
  container.innerHTML = '';
  TEST_DEFS.forEach(function(t) {
    var card = document.createElement('div');
    card.id = 'tc_' + t.id;
    card.style.cssText = 'background:var(--bg3);border-radius:6px;padding:12px 14px;border:1px solid var(--border);display:flex;align-items:center;gap:10px;';
    card.innerHTML =
      '<div style="flex:1;">' +
        '<div style="font-size:13px;font-weight:600;color:var(--text);">' + t.name + '</div>' +
        '<div style="font-size:11px;color:var(--text2);margin-top:2px;">' + t.desc + '</div>' +
      '</div>' +
      '<span class="tc-badge" style="font-size:10px;font-weight:700;padding:2px 8px;border-radius:4px;border:1px solid var(--text2);color:var(--text2);">--</span>' +
      '<button style="font-size:11px;padding:5px 12px;" onclick="runSingleTest(\\'' + t.id + '\\')">Run</button>';
    container.appendChild(card);
  });
})();

// ── Run single test ──
async function runSingleTest(id) {
  if (testRunning) { showToast('A test is already running'); return; }
  var def = TEST_DEFS.find(function(t) { return t.id === id; });
  if (!def) return;
  testRunning = true;
  testAbort = false;
  setCardStatus(id, 'running');
  testLog('\\u2501\\u2501 Running: ' + def.name + ' \\u2501\\u2501', 'info');
  try {
    await refreshTestStatus();
    var passed = await def.run();
    setCardStatus(id, passed ? 'pass' : 'fail');
    testLog(def.name + ': ' + (passed ? 'PASSED' : 'FAILED'), passed ? 'pass' : 'fail');
  } catch(e) {
    setCardStatus(id, 'fail');
    testLog(def.name + ': ERROR — ' + e.message, 'fail');
  }
  testRunning = false;
  await refreshTestStatus();
}

// ── Run all tests ──
async function runAllTests() {
  if (testRunning) { showToast('Tests already running'); return; }
  testRunning = true;
  testAbort = false;
  var total = TEST_DEFS.length, passed = 0, failed = 0;
  testLog('\\u2550\\u2550\\u2550 Running all ' + total + ' tests \\u2550\\u2550\\u2550', 'info');

  // Reset all cards
  TEST_DEFS.forEach(function(t) { setCardStatus(t.id, 'idle'); });

  var skipDefrost = document.getElementById('skipDefrost').checked;
  for (var i = 0; i < TEST_DEFS.length; i++) {
    if (testAbort) { testLog('Tests aborted by user', 'warn'); break; }
    var t = TEST_DEFS[i];
    if (skipDefrost && t.id === 'p3_defrost') {
      setCardStatus(t.id, 'idle');
      testLog('[' + (i+1) + '/' + total + '] ' + t.name + ' \u2014 SKIPPED (defrost disabled)', 'warn');
      continue;
    }
    document.getElementById('testProgress').textContent = 'Test ' + (i+1) + '/' + total;
    setCardStatus(t.id, 'running');
    testLog('\\u2501\\u2501 [' + (i+1) + '/' + total + '] ' + t.name + ' \\u2501\\u2501', 'info');
    try {
      await refreshTestStatus();
      var ok = await t.run();
      setCardStatus(t.id, ok ? 'pass' : 'fail');
      testLog(t.name + ': ' + (ok ? 'PASSED' : 'FAILED'), ok ? 'pass' : 'fail');
      if (ok) passed++; else failed++;
    } catch(e) {
      setCardStatus(t.id, 'fail');
      testLog(t.name + ': ERROR — ' + e.message, 'fail');
      failed++;
    }
    // Brief pause between tests for firmware to settle
    if (i < TEST_DEFS.length - 1) await sleep(3000);
  }

  document.getElementById('testProgress').textContent = '';
  testLog('\\u2550\\u2550\\u2550 Results: ' + passed + ' passed, ' + failed + ' failed, ' + (total - passed - failed) + ' skipped \\u2550\\u2550\\u2550', failed > 0 ? 'warn' : 'pass');
  testRunning = false;
  await refreshTestStatus();
}

function stopTests() {
  if (testRunning) {
    testAbort = true;
    testLog('Stop requested — finishing current test...', 'warn');
  }
}

function clearTestLog() {
  document.getElementById('testLog').innerHTML = '<div style="color:var(--text2);">Log cleared.</div>';
  TEST_DEFS.forEach(function(t) { setCardStatus(t.id, 'idle'); });
}

// Refresh status bar when switching to test tab
var origSwitchTab = switchTab;
switchTab = function(tabId) {
  origSwitchTab(tabId);
  if (tabId === 'test') refreshTestStatus();
};
</script>
</body>
</html>`;

// ══════════════════════════════════════════════════════════════════
// Physics Engine + HTTP Server
// ══════════════════════════════════════════════════════════════════

export function startRS485Panel(responder: RS485Responder, port: number): void {
  // Load persisted state or fall back to defaults
  const savedPhysics = loadConfig<PhysicsConfig>('physics');
  const savedSwitches = loadConfig<SwitchState>('switches');
  const savedDigitalInputs = loadConfig<DigitalInputState>('digitalInputs');
  const savedInvertedInputs = loadConfig<InvertedInputs>('invertedInputs');

  const physics: PhysicsConfig = savedPhysics ? { ...defaultPhysics(), ...savedPhysics } : defaultPhysics();
  const overrides: ManualOverrides = defaultOverrides();
  const equipment: EquipmentState = defaultEquipment();
  const switches: SwitchState = savedSwitches ? { ...defaultSwitchState(), ...savedSwitches } : defaultSwitchState();
  const digitalInputs: DigitalInputState = savedDigitalInputs ? { ...defaultDigitalInputs(), ...savedDigitalInputs } : defaultDigitalInputs();
  const invertedInputs: InvertedInputs = savedInvertedInputs ? { ...defaultInvertedInputs(), ...savedInvertedInputs } : defaultInvertedInputs();
  const cpldOutputs: CPLDOutputState = defaultCPLDOutputs();

  if (savedPhysics) console.log(`[RS485] Loaded physics config from disk`);
  if (savedSwitches) console.log(`[RS485] Loaded switch state from disk`);
  if (savedDigitalInputs) console.log(`[RS485] Loaded digital input state from disk`);
  if (savedInvertedInputs) console.log(`[RS485] Loaded inverted inputs from disk`);
  console.log(`[RS485] Config dir: ${getConfigDir()}`);

  let cpldConnected = false;
  let cpldSocket: net.Socket | null = null;
  let cpldBuffer = '';
  let cpldReconnecting = false;

  let lastPlenumTemp: number = responder.getState().plenumTemp1;

  // ────────────────────────────────────────────────
  // CPLD TCP Client — connects to QEMU chardev on port 9003
  // ────────────────────────────────────────────────
  function sendCPLDState(): void {
    if (!cpldSocket || !cpldConnected) return;
    const mainWord = buildMainInputState(switches, digitalInputs, invertedInputs);
    const ex1Word = buildExInputState(digitalInputs.ex1, digitalInputs.ex1Installed, invertedInputs.ex1);
    const ex2Word = buildExInputState(digitalInputs.ex2, digitalInputs.ex2Installed, invertedInputs.ex2);
    const msg = `M=${(mainWord >>> 0).toString(16)}\nE1=${(ex1Word >>> 0).toString(16)}\nE2=${(ex2Word >>> 0).toString(16)}\n`;
    console.log(`[RS485] CPLD TX: M=${(mainWord >>> 0).toString(16).padStart(8,'0')} (AIRFLOW=${digitalInputs.main[8]}, LOWTEMP=${digitalInputs.main[9]}, sw=${JSON.stringify(switches)})`);
    cpldSocket.write(msg);
  }

  function scheduleCPLDReconnect(): void {
    if (cpldReconnecting) return;
    cpldReconnecting = true;
    setTimeout(() => { cpldReconnecting = false; connectCPLD(); }, 3000);
  }

  function connectCPLD(): void {
    if (cpldSocket) { cpldSocket.removeAllListeners(); cpldSocket.destroy(); cpldSocket = null; }
    cpldConnected = false;
    const sock = new net.Socket();
    cpldSocket = sock;
    cpldBuffer = '';

    sock.setKeepAlive(true, 10000);
    sock.setNoDelay(true);

    sock.connect(9003, '127.0.0.1', () => {
      cpldConnected = true;
      console.log('[RS485] CPLD chardev connected on port 9003');
      // Request current output state from QEMU CPLD model
      sock.write('GET\n');
      // Send initial input state after a small delay to let QEMU settle
      setTimeout(() => sendCPLDState(), 100);
    });

    sock.on('data', (data: Buffer) => {
      cpldBuffer += data.toString();
      let nl: number;
      while ((nl = cpldBuffer.indexOf('\n')) !== -1) {
        const line = cpldBuffer.slice(0, nl).trim();
        cpldBuffer = cpldBuffer.slice(nl + 1);
        if (line.startsWith('OUT=')) {
          const parts = line.slice(4).split(',');
          if (parts.length === 3) {
            cpldOutputs.main = parseInt(parts[0], 16) || 0;
            cpldOutputs.ex1 = parseInt(parts[1], 16) || 0;
            cpldOutputs.ex2 = parseInt(parts[2], 16) || 0;
          }
        }
      }
    });

    sock.on('close', (hadError: boolean) => {
      cpldConnected = false;
      console.log(`[RS485] CPLD chardev disconnected (hadError=${hadError}) — reconnecting in 3s`);
      scheduleCPLDReconnect();
    });

    sock.on('error', (err: Error) => {
      cpldConnected = false;
      if ((err as NodeJS.ErrnoException).code !== 'ECONNREFUSED') {
        console.log('[RS485] CPLD error:', (err as NodeJS.ErrnoException).code, err.message);
      }
    });
  }

  // Start CPLD connection (delay to let QEMU start first)
  setTimeout(connectCPLD, 5000);

  // ────────────────────────────────────────────────
  // Dynamic Output Config — poll bridge for port-indexed OutputConfig
  // so we can decode CPLD output bits into equipment names.
  // Board start indices are computed from ioAvailable: each board gets
  // max(numOutputs, numInputs) port slots with a padding slot between boards.
  // ────────────────────────────────────────────────
  let portOutputConfig: string[] = [];    // port-indexed: [portId] = eqIndex
  let decodedOutputs: DecodedOutput[] = [];
  let outputConfigLoaded = false;
  let boardOffsets = { mainStart: 1, mainOutBits: 10, mainInCount: 10, ex1Start: 11, ex2Start: 19 };
  let armEquipStatus: string[] = [];      // raw EquipStatusData from ARM via bridge
  let currentMode: number = 0;            // ARM CurrentMode (UI_* enum from States.h)
  let currentModeName: string = 'Unknown';
  let isAutoMode: boolean = false;        // true when ARM is in an auto-control mode
  let armFanSpeed: number = 0;            // VFD fan speed % from ARM MainData[10] (0-100)
  let armDoorOutput: number = 0;          // Door opening % from ARM MainData[11] (0-100, PIDU controlled)
  let armRefrigPercent: number = 0;       // Refrig output % from ARM MainData[11] when CoolLabel=1 (0-100, PIDU controlled)

  /** Parse ioAvailable to compute board start indices in config arrays.
   *  Format: ["Main:numOut:numIn:ver", "Ex 1:numOut:numIn:ver", "Ex 2:numOut:numIn:ver", ""]
   *  Each board occupies max(numOut, numIn) port slots with a padding slot between boards.
   */
  function parseIoAvailable(ioAvailable: string[]): void {
    const boards: Array<{ name: string; numOut: number; numIn: number; maxPorts: number }> = [];
    for (const entry of ioAvailable) {
      if (!entry) continue;
      const parts = entry.split(':');
      const numOut = parseInt(parts[1], 10) || 0;
      const numIn = parseInt(parts[2], 10) || 0;
      boards.push({ name: parts[0], numOut, numIn, maxPorts: Math.max(numOut, numIn) });
    }
    if (boards.length < 1) return;
    // Firmware uses SS_PORT_ID_MULTIPLIER=12 per board slot.
    // PortId = board * 12 + boardRelativePort (port 0 is padding, outputs start at 1).
    const SLOT = 12;
    boardOffsets.mainStart = 1;              // board 0, port 1
    boardOffsets.mainOutBits = boards[0].numOut;
    boardOffsets.mainInCount = boards[0].numIn;
    if (boards.length >= 2) {
      boardOffsets.ex1Start = 1 * SLOT + 1;  // board 1, port 1 = PortId 13
    }
    if (boards.length >= 3) {
      boardOffsets.ex2Start = 2 * SLOT + 1;  // board 2, port 1 = PortId 25
    }
    console.log(`[RS485] Board offsets from ioAvailable: main=${boardOffsets.mainStart} (${boards[0].numOut}out/${boards[0].numIn}in), ex1=${boardOffsets.ex1Start}, ex2=${boardOffsets.ex2Start}`);
  }



  async function fetchOutputConfig(): Promise<void> {
    try {
      const res = await fetch(physics.bridgeUrl + '/iot/ioconfig');
      if (!res.ok) return;
      const data = await res.json() as Record<string, unknown>;
      if (data && typeof data === 'object') {
        // The /iot/ioconfig endpoint returns { ioAvailable, config: { outputConfig, inputConfig }, ioNames, systemMode }
        // Parse ioAvailable first to get correct board offsets
        if (Array.isArray(data.ioAvailable)) {
          parseIoAvailable(data.ioAvailable as string[]);
        }
        const config = data.config as Record<string, unknown> | undefined;
        if (config && Array.isArray(config.outputConfig) && (config.outputConfig as string[]).length > 0) {
          portOutputConfig = config.outputConfig as string[];
          if (!outputConfigLoaded) {
            console.log(`[RS485] OutputConfig loaded from bridge: ${portOutputConfig.length} ports`);
            outputConfigLoaded = true;
          }
        }

      }
    } catch {
      // Bridge not available yet — will retry
    }
  }

  // ── Dynamic Input Labels from IO Config ──
  let inputLabelsMain: string[] = [...MAIN_INPUT_LABELS];
  let inputLabelsEx1: string[] = [...EX_INPUT_LABELS];
  let inputLabelsEx2: string[] = [...EX_INPUT_LABELS];
  let inputLabelsLoaded = false;

  async function fetchInputLabels(): Promise<void> {
    try {
      const res = await fetch(physics.bridgeUrl + '/iot/ioconfig');
      if (!res.ok) return;
      const data = await res.json() as Record<string, unknown>;
      if (!data || typeof data !== 'object') return;
      const config = data.config as Record<string, unknown> | undefined;
      const ioNames = data.ioNames as string[] | undefined;
      if (!config || !Array.isArray(config.inputConfig) || !Array.isArray(ioNames)) return;
      const inputConfig = config.inputConfig as (number | string)[];

      function resolveLabel(portIdx: number, fallback: string): string {
        if (portIdx >= inputConfig.length) return fallback;
        const eqId = Number(inputConfig[portIdx]);
        if (isNaN(eqId) || eqId < 0 || eqId >= ioNames!.length) return fallback;
        const full = ioNames![eqId];
        if (typeof full !== 'string') return fallback;
        const name = full.split(':')[0];
        return name || fallback;
      }

      // Parse ioAvailable to get correct board start indices
      if (Array.isArray(data.ioAvailable)) {
        parseIoAvailable(data.ioAvailable as string[]);
      }

      // Use dynamic board offsets computed from ioAvailable
      // Main board: boardOffsets.mainStart + i for i=0..mainInCount-1
      const newMain: string[] = [];
      for (let i = 0; i < boardOffsets.mainInCount; i++) newMain.push(resolveLabel(boardOffsets.mainStart + i, '(Unassigned)'));
      // Expansion 1: boardOffsets.ex1Start + i for i=0..7
      const newEx1: string[] = [];
      for (let i = 0; i < 8; i++) newEx1.push(resolveLabel(boardOffsets.ex1Start + i, '(Unassigned)'));
      // Expansion 2: boardOffsets.ex2Start + i for i=0..7
      const newEx2: string[] = [];
      for (let i = 0; i < 8; i++) newEx2.push(resolveLabel(boardOffsets.ex2Start + i, '(Unassigned)'));

      inputLabelsMain = newMain;
      inputLabelsEx1 = newEx1;
      inputLabelsEx2 = newEx2;
      if (!inputLabelsLoaded) {
        console.log(`[RS485] Input labels loaded from bridge IO Config`);
        inputLabelsLoaded = true;
      }
    } catch {
      // Bridge not available yet — will retry
    }
  }

  /** Fetch CurrentMode from bridge header-data endpoint */
  async function fetchCurrentMode(): Promise<void> {
    try {
      const res = await fetch(physics.bridgeUrl + '/iot/ws/header-data');
      if (!res.ok) return;
      const data = await res.json() as Record<string, unknown>;
      if (data && typeof data.CurrentMode === 'number') {
        currentMode = data.CurrentMode as number;
        currentModeName = MODE_NAMES[currentMode] ?? `Mode ${currentMode}`;
        isAutoMode = AUTO_MODES.has(currentMode);
      }
    } catch {
      // Bridge not available — will retry
    }
  }

  /** Fetch ARM EquipStatusData from bridge to compare against CPLD physical outputs */
  async function fetchArmEquipStatus(): Promise<void> {
    try {
      const res = await fetch(physics.bridgeUrl + '/iot/ws/equipment-data');
      if (!res.ok) return;
      const data = await res.json() as Record<string, unknown>;
      if (data && Array.isArray(data.eqStatus)) {
        armEquipStatus = data.eqStatus as string[];
      }
    } catch {
      // Bridge not available — will retry
    }
  }

  /** Fetch ARM fan speed + door output from bridge frontmatter */
  async function fetchArmFanSpeed(): Promise<void> {
    try {
      const res = await fetch(physics.bridgeUrl + '/iot/frontmatter');
      if (!res.ok) return;
      const data = await res.json() as Record<string, unknown>;
      const mainArr = data.main as unknown[];
      if (Array.isArray(mainArr)) {
        // main[14] = rawMain[10] (FanSpeed via VFD)
        if (mainArr.length > 14) {
          const speed = Number(mainArr[14]);
          armFanSpeed = isNaN(speed) ? 0 : speed;
        }
        // main[15] = rawMain[11] (CoolOutput = door opening % from PIDU loop,
        //                          OR refrig PWM % when CoolLabel=1 in refrig/defrost/recirc)
        // main[16] = rawMain[12] (CoolLabel: '0'=cooling/doors, '1'=refrig)
        const coolLabel = mainArr.length > 16 ? String(mainArr[16]) : '0';
        if (mainArr.length > 15) {
          const pct = Number(mainArr[15]);
          const v = isNaN(pct) ? 0 : pct;
          if (coolLabel === '1') {
            armRefrigPercent = v;
            // Door output decays back to 0 while refrig owns the CoolOutput field
            armDoorOutput = 0;
          } else {
            armDoorOutput = v;
            armRefrigPercent = 0;
          }
        }
      }
    } catch {
      // Bridge not available — will retry
    }
  }

  /** Decode CPLD output bits and update equipment state from physical outputs */
  function updateDecodedOutputs(): void {
    if (portOutputConfig.length === 0) return;
    decodedOutputs = decodeCPLDOutputs(cpldOutputs, portOutputConfig, boardOffsets);

    // Enrich with ARM EquipStatus data where mapping is known
    if (armEquipStatus.length > 0) {
      for (const d of decodedOutputs) {
        const statusIdx = EQ_TO_EQSTATUS_OUTPUT[d.eqIndex];
        if (statusIdx !== undefined && statusIdx < armEquipStatus.length) {
          d.armOn = armEquipStatus[statusIdx] === '1';
        }
      }
    }

    // Drive equipment state from decoded CPLD output bits (real firmware decisions)
    // In AUTO modes, ARM controls outputs — CPLD bits are the source of truth
    // Exception: Fan is VFD-controlled (Variable Frequency Drive) — its CPLD bit (port 3)
    // is only set in Fan Manual mode. In auto modes, fan speed comes from MainData[10].
    // Exception: Doors use PIDU loop with proportional actuators — CPLD EQ.DOORS bit
    // indicates motor activity but NOT opening percentage. Door opening % comes from
    // MainData[11] (armDoorOutput). Doors are open if CPLD bit set OR armDoorOutput > 0.
    if (decodedOutputs.length > 0) {
      const isOn = (eqIdx: number) => decodedOutputs.some(d => d.eqIndex === eqIdx && d.on);
      // Fan: CPLD bit only fires in manual mode; use VFD speed from ARM otherwise
      const fanRunning = isOn(0) || armFanSpeed > 0;
      // Doors: PIDU-controlled actuators, CPLD bit + ARM door output %
      const doorsActive = isOn(1) || armDoorOutput > 0;
      if (isAutoMode) {
        // ARM is in auto mode — it controls all outputs via CPLD
        equipment.fanOn = fanRunning;     // EQ.FAN (VFD or CPLD)
        equipment.coolingMode = doorsActive; // PIDU doors open (CPLD or ARM %)
        equipment.refrigOn = isOn(2) || isOn(13) || isOn(14); // EQ.REFRIGERATION + stages
        equipment.heatOn = isOn(4);       // EQ.HEAT
        equipment.burnerOn = isOn(6);     // EQ.BURNER
        equipment.humidOn = isOn(7) || isOn(9) || isOn(11); // HUMID_HEAD 1/2/3
      } else {
        // Not in auto mode — CPLD still reflects outputs but log any discrepancy
        equipment.fanOn = fanRunning;
        equipment.coolingMode = doorsActive;
        equipment.refrigOn = isOn(2) || isOn(13) || isOn(14);
        equipment.heatOn = isOn(4);
        equipment.burnerOn = isOn(6);
        equipment.humidOn = isOn(7) || isOn(9) || isOn(11);
      }
      equipment.lastPoll = Date.now();
    }
  }

  // Fetch OutputConfig after a delay (bridge needs time to boot + init)
  setTimeout(fetchOutputConfig, 8000);
  // Retry sooner if first fetch got empty data (bridge wasn't ready)
  setTimeout(() => { if (portOutputConfig.length === 0) fetchOutputConfig(); }, 15000);
  setTimeout(() => { if (portOutputConfig.length === 0) fetchOutputConfig(); }, 22000);
  // Refresh periodically (in case IO config is changed)
  setInterval(fetchOutputConfig, 30000);
  // Fetch Input Labels from IO Config (same schedule as OutputConfig)
  setTimeout(fetchInputLabels, 8500);
  setTimeout(() => { if (!inputLabelsLoaded) fetchInputLabels(); }, 16000);
  setInterval(fetchInputLabels, 30000);
  // Fetch ARM EquipStatus on same schedule as OutputConfig
  setTimeout(fetchArmEquipStatus, 9000);
  setInterval(fetchArmEquipStatus, 10000);
  // Fetch ARM fan speed (VFD) from bridge frontmatter
  setTimeout(fetchArmFanSpeed, 9500);
  setInterval(fetchArmFanSpeed, 5000);
  // Fetch CurrentMode from ARM
  setTimeout(fetchCurrentMode, 8500);
  setInterval(fetchCurrentMode, 5000);
  // Decode outputs every 2s (same rate as physics tick)
  setInterval(updateDecodedOutputs, 2000);

  // ────────────────────────────────────────────────
  // Diurnal outside temperature
  // ────────────────────────────────────────────────
  function getDiurnalOutsideTemp(): number {
    const p = physics;
    const period = p.diurnalPeriodSec * 1000;
    const phase = (Date.now() % period) / period;
    const peakPhase = p.peakHour / 24;
    const angle = (phase - peakPhase) * 2 * Math.PI;
    const mid = (p.outsideTempMin + p.outsideTempMax) / 2;
    const amp = (p.outsideTempMax - p.outsideTempMin) / 2;
    return mid + amp * Math.cos(angle) + noise(p.outsideTempNoise);
  }

  // ────────────────────────────────────────────────
  // Physics status for display
  // ────────────────────────────────────────────────
  function getPhysicsStatus() {
    const s = responder.getState();
    const p = physics;
    const period = p.diurnalPeriodSec * 1000;
    const phase = (Date.now() % period) / period;
    const peakPhase = p.peakHour / 24;
    const dist = Math.abs(((phase - peakPhase + 0.5) % 1) - 0.5);
    const isDaytime = dist < 0.25;

    // Heat flow
    const insulationFlow = (s.outsideTemp - s.plenumTemp1) * p.insulationFactor;
    const respirationFlow = p.respirationHeatRate * p.potatoMass;
    // In auto mode, ARM controls outputs — always use CPLD-driven equipment state
    const eq = (isAutoMode && cpldConnected && decodedOutputs.length > 0)
      ? equipment  // ARM auto: CPLD outputs are authoritative
      : (outputConfigLoaded && cpldConnected && decodedOutputs.length > 0)
        ? equipment
        : (p.equipmentPolling && equipment.lastPoll > 0)
          ? equipment
          : getEquipFromSwitches();
    const systemActive = switches.startStop === 'start';
    let equipFlow = 0;
    if (eq.lastPoll > 0 && systemActive) {
      if (eq.fanOn && eq.coolingMode) equipFlow = (s.outsideTemp - s.plenumTemp1) * p.coolingRate;
      // Refrig cooling scaled by ARM PIDU output (matches production: PWM % → compressor staging)
      if (eq.refrigOn) equipFlow -= p.refrigRate * (armRefrigPercent > 0 ? armRefrigPercent / 100 : 1);
      if (eq.heatOn || eq.burnerOn) equipFlow += p.heatingRate;
      // Use ARM EquipStatus for climacell (preferred — matches SvelteKit UI),
      // fall back to decoded CPLD outputs, then switches
      let climacellOn: boolean;
      if (outputConfigLoaded && decodedOutputs.length > 0) {
        const ccDecoded = decodedOutputs.find(d => d.eqIndex === 3);
        climacellOn = ccDecoded
          ? (typeof ccDecoded.armOn === 'boolean' ? ccDecoded.armOn : ccDecoded.on)
          : switches.climacell !== 'off';
      } else {
        climacellOn = switches.climacell !== 'off';
      }
      if (climacellOn && s.plenumTemp1 > 5) {
        equipFlow -= p.coolingRate * 0.5 * Math.min(1, (s.plenumTemp1 - 5) / 20);
      }
    }
    const totalFlow = insulationFlow + respirationFlow + equipFlow;

    // CO2 trend
    const co2Build = p.respirationCo2Rate * p.potatoMass;
    const co2Leak = (s.co2 - p.co2AmbientPpm) * p.co2LeakRate;
    const co2Delta = co2Build - co2Leak;

    return {
      diurnalPhase: isDaytime ? 'Day' : 'Night',
      heatFlow: (totalFlow >= 0 ? '+' : '') + (totalFlow * 1000).toFixed(1) + ' m\u00b0C/tick',
      heatFlowDir: totalFlow > 0.0001 ? 1 : totalFlow < -0.0001 ? -1 : 0,
      co2Trend: (co2Delta >= 0 ? '+' : '') + co2Delta.toFixed(1) + ' ppm/tick',
      co2TrendDir: co2Delta > 0.5 ? 1 : co2Delta < -0.5 ? -1 : 0,
    };
  }

  // ────────────────────────────────────────────────
  // Derive equipment behavior from CPLD switches
  // (used when bridge polling is off — switches directly affect physics)
  // ────────────────────────────────────────────────
  function getEquipFromSwitches(): EquipmentState {
    const systemOn = switches.startStop === 'start';
    return {
      fanOn: systemOn && (switches.fan === 'manual' || switches.fan === 'auto'),
      coolingMode: systemOn && switches.freshAir !== 'off',
      refrigOn: systemOn && switches.refrig === 'auto',
      heatOn: false,
      burnerOn: false,
      humidOn: systemOn && (switches.humid === 'manual' || switches.humid === 'auto'),
      lastPoll: systemOn ? Date.now() : 0,
    };
  }

  // ────────────────────────────────────────────────
  // Main physics tick (every 2 seconds)
  // ────────────────────────────────────────────────
  function physicsTick(): void {
    if (!physics.enabled) return;

    const p = physics;
    const s = responder.getState();
    const update: Record<string, number> = {};

    // Effective equipment: In auto mode ARM controls outputs via CPLD (authoritative).
    // Otherwise: CPLD decoded > bridge-polled > switch-derived fallback.
    const eq = (isAutoMode && cpldConnected && decodedOutputs.length > 0)
      ? equipment  // ARM auto: CPLD outputs are the source of truth
      : (outputConfigLoaded && cpldConnected && decodedOutputs.length > 0)
        ? equipment  // driven by updateDecodedOutputs()
        : (p.equipmentPolling && equipment.lastPoll > 0)
          ? equipment
          : getEquipFromSwitches();
    const systemActive = switches.startStop === 'start';

    // ─ Outside Temperature: diurnal sinusoidal cycle ─
    if (!overrides.outsideTemp) {
      update.outsideTemp = getDiurnalOutsideTemp();
    }

    // ─ Outside Humidity: inverse-coupled to outside temp ─
    if (!overrides.outsideHumid) {
      const currentOutside = update.outsideTemp ?? s.outsideTemp;
      const mid = (p.outsideTempMin + p.outsideTempMax) / 2;
      const range = (p.outsideTempMax - p.outsideTempMin) || 1;
      const tempFactor = (currentOutside - mid) / range;
      update.outsideHumid = clamp(
        p.outsideHumidBase - tempFactor * p.outsideHumidTempCoeff + noise(p.outsideHumidNoise),
        5, 100,
      );
    }

    // ─ Plenum Temperature: insulation + respiration + equipment ─
    if (!overrides.plenumTemp) {
      const outsideT = update.outsideTemp ?? s.outsideTemp;
      let delta = 0;

      if (eq.lastPoll > 0 && systemActive) {
        // Fan with doors open = ventilation cooling (draws outside air)
        // Door opening is PIDU-controlled: armDoorOutput (0-100%) determines airflow
        // proportion. In production, partially-open doors proportionally limit heat exchange.
        if (eq.fanOn && eq.coolingMode) {
          const fanRate = switches.fan === 'manual' ? p.coolingRate * 1.5 : p.coolingRate;
          // Proportional door opening from ARM PIDU loop (0-100%)
          // armDoorOutput=0 means closed (no airflow), 100 means fully open
          const doorFraction = armDoorOutput > 0 ? armDoorOutput / 100 : (switches.freshAir === 'manual' ? 1.0 : 0.6);
          delta += (outsideT - s.plenumTemp1) * fanRate * doorFraction;
        }
        // Refrigeration active cooling — scaled by ARM PIDU output %
        // (same slew+PID structure as doors in armSimulator::CtrlRefrig mirror)
        if (eq.refrigOn) {
          const refrigFrac = armRefrigPercent > 0 ? armRefrigPercent / 100 : 1;
          delta -= p.refrigRate * refrigFrac;
        }
        // Climacell evaporative cooling (only effective when warm)
        // Use ARM EquipStatus when available (matches SvelteKit UI), else CPLD, else switches
        let ccOn = switches.climacell !== 'off';
        if (outputConfigLoaded && decodedOutputs.length > 0) {
          const ccD = decodedOutputs.find(d => d.eqIndex === 3);
          if (ccD) ccOn = typeof ccD.armOn === 'boolean' ? ccD.armOn : ccD.on;
        }
        if (ccOn && systemActive) {
          const climacellRate = switches.climacell === 'manual' ? p.coolingRate * 0.8 : p.coolingRate * 0.5;
          if (s.plenumTemp1 > 5) {
            delta -= climacellRate * Math.min(1, (s.plenumTemp1 - 5) / 20);
          }
        }
        // Heaters (from bridge only, no switch — heat is auto-controlled)
        if (eq.heatOn || eq.burnerOn) {
          delta += p.heatingRate * (eq.burnerOn ? 1.5 : 1.0);
        }
        // Background insulation always applies
        delta += (outsideT - s.plenumTemp1) * p.insulationFactor;
      } else {
        // System off or no equipment data — only insulation drift
        delta += (outsideT - s.plenumTemp1) * p.insulationFactor;
      }

      // Potato respiration heat — rate increases with temperature
      const tempFactor = 1.0 + Math.max(0, (s.plenumTemp1 - 7) * 0.03);
      delta += p.respirationHeatRate * p.potatoMass * tempFactor;

      update.plenumTemp1 = clamp(s.plenumTemp1 + delta + noise(p.plenumTempNoise), -40, 65);
      update.plenumTemp2 = (update.plenumTemp1) + noise(0.3);
    }

    // ─ Return Temperature: warmed by passing through potato pile ─
    if (!overrides.returnTemp) {
      const plenumT = update.plenumTemp1 ?? s.plenumTemp1;
      const massOffset = p.returnTempOffset * (0.5 + p.potatoMass * 0.5);
      update.returnTemp = plenumT + massOffset + noise(p.returnTempNoise);
    }

    // ─ Plenum Humidity: psychrometric coupling + respiration moisture ─
    if (!overrides.plenumHumid) {
      let delta = 0;

      // Temperature-humidity coupling: warming → RH drops, cooling → RH rises
      const currentPlenum = update.plenumTemp1 ?? s.plenumTemp1;
      const tempChange = currentPlenum - lastPlenumTemp;
      delta -= tempChange * p.tempHumidCoeff;

      // Potato respiration transpiration
      delta += p.respirationMoistureRate * p.potatoMass;

      // Humidifier effect from switch
      if (eq.humidOn && systemActive) {
        const humidRate = switches.humid === 'manual' ? p.humidifyRate * 1.2 : p.humidifyRate;
        delta += humidRate;
      } else {
        // Climacell also adds some moisture (evaporative cooler)
        if (switches.climacell !== 'off' && systemActive) {
          delta += p.humidifyRate * 0.3;
        }
        delta -= p.naturalDryRate * 0.3;
      }

      update.plenumHumid = clamp(s.plenumHumid + delta + noise(p.humidNoise), 0, 100);
    }

    // ─ Return Humidity: slightly lower than plenum ─
    if (!overrides.returnHumid) {
      const plenumH = update.plenumHumid ?? s.plenumHumid;
      update.returnHumid = clamp(plenumH + p.returnHumidOffset + noise(p.humidNoise), 0, 100);
    }

    // ─ CO₂: respiration builds, leakage purges ─
    if (!overrides.co2) {
      let delta = 0;

      // Potato respiration CO₂ — rate rises with temperature
      const co2TempFactor = 1.0 + Math.max(0, (s.plenumTemp1 - 7) * 0.05);
      delta += p.respirationCo2Rate * p.potatoMass * co2TempFactor;

      // Natural leakage toward ambient — enforce minimum leak rate and ambient
      // to prevent CO₂ from accumulating above the sensor's 9930ppm hardware limit
      const co2Ambient = p.co2AmbientPpm > 0 ? p.co2AmbientPpm : 420;
      const co2Leak = Math.max(p.co2LeakRate, 0.0005);
      delta -= (s.co2 - co2Ambient) * co2Leak;

      // Fan purge — ventilation flushes CO₂
      if (eq.fanOn && systemActive) {
        const purgeRate = switches.fan === 'manual' ? 0.015 : 0.01;
        delta -= (s.co2 - co2Ambient) * purgeRate;
        // Doors open = proportional air exchange (PIDU door opening affects CO₂ flush rate)
        if (eq.coolingMode || switches.freshAir !== 'off') {
          const doorFrac = armDoorOutput > 0 ? armDoorOutput / 100 : (switches.freshAir !== 'off' ? 0.5 : 0);
          delta -= (s.co2 - co2Ambient) * 0.01 * doorFrac;
        }
      }

      update.co2 = clamp(s.co2 + delta + noise(p.co2Noise), 150, 9900);
    }

    lastPlenumTemp = update.plenumTemp1 ?? s.plenumTemp1;
    responder.updateState(update);

    // Sync board config sensor values for boards 0-1 from physics-driven state
    const updatedState = responder.getState();
    responder.updateBoard(0, { sensors: [
      { uiType: 3, label: 'Plenum 1 Temp', offset: 0, disabled: false, value: updatedState.plenumTemp1 },
      { uiType: 3, label: 'Plenum 2 Temp', offset: 0, disabled: false, value: updatedState.plenumTemp2 },
      { uiType: 3, label: 'Outside Temp',  offset: 0, disabled: false, value: updatedState.outsideTemp },
      { uiType: 4, label: 'Return Temp',   offset: 0, disabled: false, value: updatedState.returnTemp },
    ] as any });
    responder.updateBoard(1, { sensors: [
      { uiType: 1, label: 'Outside Humidity', offset: 0, disabled: false, value: updatedState.outsideHumid },
      { uiType: 1, label: 'Plenum Humidity',  offset: 0, disabled: false, value: updatedState.plenumHumid },
      { uiType: 6, label: 'Return Humidity',  offset: 0, disabled: false, value: updatedState.returnHumid },
      { uiType: 2, label: 'CO2',              offset: 0, disabled: false, value: updatedState.co2 },
    ] as any });

    // Add small jitter to pile board sensor values (boards 2+) for realism
    const boards = responder.getBoardConfigs();
    for (const board of boards) {
      if (board.address >= 2 && !board.disabled) {
        for (let si = 0; si < 4; si++) {
          const sensor = board.sensors[si];
          if (!sensor.disabled) {
            const jitterAmp = [1, 6, 7, 10, 11].includes(sensor.uiType) ? 0.3 : 0.05;
            const jittered = sensor.value + noise(jitterAmp);
            responder.setBoardSensorValue(board.address, si, jittered);
          }
        }
      }
    }
  }

  // ────────────────────────────────────────────────
  // Equipment state polling from bridge
  // ────────────────────────────────────────────────
  async function pollEquipment(): Promise<void> {
    if (!physics.equipmentPolling) return;
    // When CPLD decoded outputs are available, they drive equipment state directly —
    // no need to poll the bridge for equipment status (avoids overwrite conflicts)
    if (outputConfigLoaded && cpldConnected && decodedOutputs.length > 0) return;
    try {
      const res = await fetch(physics.bridgeUrl + '/iot/equipment-data');
      if (!res.ok) return;
      const data = await res.json() as Record<string, unknown>;
      if (data && typeof data === 'object') {
        if ('equipStatus' in data) {
          const eq = data.equipStatus as Record<string, boolean>;
          equipment.fanOn = !!eq.fan;
          equipment.coolingMode = !!eq.cooling;
          equipment.refrigOn = !!eq.refrig;
          equipment.heatOn = !!eq.heat;
          equipment.burnerOn = !!eq.burner;
          equipment.humidOn = !!eq.humid;
        } else if ('raw' in data || typeof data === 'string') {
          const raw = String((data as Record<string, string>).raw || data);
          const parts = raw.split(',').map(s => s.trim());
          equipment.fanOn = parts[0] === '1' || parts[0] === 'ON';
          equipment.refrigOn = parts[7] === '1' || parts[7] === 'ON';
          equipment.heatOn = parts[8] === '1' || parts[8] === 'ON';
          equipment.burnerOn = parts[14] === '1' || parts[14] === 'ON';
          equipment.humidOn = parts[9] === '1' || parts[9] === 'ON';
          equipment.coolingMode = equipment.fanOn && !equipment.refrigOn && !equipment.heatOn;
        }
        equipment.lastPoll = Date.now();
      }
    } catch {
      // Bridge not available — continue without equipment feedback
    }
  }

  // ────────────────────────────────────────────────
  // Start timers
  // ────────────────────────────────────────────────
  setInterval(physicsTick, 2000);
  setInterval(pollEquipment, 3000);
  console.log('[RS485] Physics engine started (2s tick)');

  // ────────────────────────────────────────────────
  // HTTP helpers
  // ────────────────────────────────────────────────
  function parseBody(req: http.IncomingMessage): Promise<Record<string, unknown>> {
    return new Promise((resolve, reject) => {
      let body = '';
      req.on('data', (chunk: Buffer) => { body += chunk.toString(); });
      req.on('end', () => { try { resolve(JSON.parse(body)); } catch (e) { reject(e); } });
    });
  }

  // ────────────────────────────────────────────────
  // HTTP Server
  // ────────────────────────────────────────────────
  const server = http.createServer(async (req, res) => {
    const url = new URL(req.url ?? '/', `http://localhost:${port}`);
    const method = req.method ?? 'GET';

    res.setHeader('Access-Control-Allow-Origin', '*');
    res.setHeader('Access-Control-Allow-Methods', 'GET,POST,OPTIONS');
    res.setHeader('Access-Control-Allow-Headers', 'Content-Type');
    if (method === 'OPTIONS') { res.writeHead(204); res.end(); return; }

    try {
      // GET / → HTML panel
      if (method === 'GET' && url.pathname === '/') {
        res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
        res.end(PANEL_HTML);
        return;
      }

      // GET /api/status → full snapshot
      if (method === 'GET' && url.pathname === '/api/status') {
        res.writeHead(200, { 'Content-Type': 'application/json' });
        const vfd = (globalThis as any).__vfdSimulator;
        res.end(JSON.stringify({
          sensors: responder.getState(),
          stats: responder.getStats(),
          physics,
          overrides,
          equipment,
          switches,
          physicsStatus: getPhysicsStatus(),
          digitalInputs,
          invertedInputs,
          cpldOutputs,
          cpldConnected,
          boards: responder.getBoardConfigs(),
          decodedOutputs,
          outputConfigLoaded,
          armEquipStatusLoaded: armEquipStatus.length > 0,
          armFanSpeed,
          armDoorOutput,
          currentMode,
          currentModeName,
          isAutoMode,
          vfdDrives: vfd?.getDrives() ?? [],
        }));
        return;
      }

      // GET /api/input-labels → dynamic DI labels from IO Config
      if (method === 'GET' && url.pathname === '/api/input-labels') {
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({
          main: inputLabelsMain,
          ex1: inputLabelsEx1,
          ex2: inputLabelsEx2,
          loaded: inputLabelsLoaded,
        }));
        return;
      }

      // POST /api/sensors → manual overrides
      if (method === 'POST' && url.pathname === '/api/sensors') {
        const body = await parseBody(req);

        if (body._releaseAll) {
          (Object.keys(overrides) as Array<keyof ManualOverrides>).forEach(k => { overrides[k] = false; });
          res.writeHead(200, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ ok: true, overrides }));
          return;
        }

        const overrideKey = body._override as string | undefined;
        if (overrideKey && overrideKey in overrides) {
          (overrides as unknown as Record<string, boolean>)[overrideKey] = true;
        }

        const validKeys = ['plenumTemp1','plenumTemp2','outsideTemp','returnTemp',
                           'outsideHumid','plenumHumid','returnHumid','co2'];
        const update: Record<string, number> = {};
        for (const key of validKeys) {
          if (key in body && typeof body[key] === 'number') update[key] = body[key] as number;
        }
        if (Object.keys(update).length > 0) responder.updateState(update);

        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: true, sensors: responder.getState(), overrides }));
        return;
      }

      // POST /api/preset
      if (method === 'POST' && url.pathname === '/api/preset') {
        const body = await parseBody(req);
        const name = body.name as string;
        const preset = PRESETS[name];
        if (preset) {
          responder.updateState(preset.values);
          if (preset.physics) Object.assign(physics, preset.physics);
          (Object.keys(overrides) as Array<keyof ManualOverrides>).forEach(k => { overrides[k] = false; });
          saveConfig('physics', physics);
          res.writeHead(200, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ ok: true, preset: name, sensors: responder.getState(), physics }));
        } else {
          res.writeHead(200, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ ok: false, error: 'Unknown preset: ' + name }));
        }
        return;
      }

      // POST /api/physics → update physics parameters
      if (method === 'POST' && url.pathname === '/api/physics') {
        const body = await parseBody(req);
        const numKeys: Array<keyof PhysicsConfig> = [
          'outsideTempMin','outsideTempMax','outsideTempNoise','diurnalPeriodSec','peakHour',
          'outsideHumidBase','outsideHumidTempCoeff','outsideHumidNoise','insulationFactor',
          'potatoMass','respirationHeatRate','respirationCo2Rate','respirationMoistureRate',
          'tempHumidCoeff','co2AmbientPpm','co2LeakRate','returnTempOffset','returnHumidOffset',
          'coolingRate','heatingRate','refrigRate','humidifyRate','naturalDryRate',
          'plenumTempNoise','returnTempNoise','humidNoise','co2Noise',
        ];
        const boolKeys: Array<keyof PhysicsConfig> = ['enabled', 'equipmentPolling'];

        for (const key of numKeys) {
          if (key in body) (physics as unknown as Record<string, unknown>)[key] = Number(body[key as string]) || 0;
        }
        for (const key of boolKeys) {
          if (key in body) (physics as unknown as Record<string, unknown>)[key] = !!body[key as string];
        }
        if ('bridgeUrl' in body && typeof body.bridgeUrl === 'string') physics.bridgeUrl = body.bridgeUrl;
        physics.diurnalPeriodSec = Math.max(10, physics.diurnalPeriodSec);

        saveConfig('physics', physics);
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: true, physics }));
        return;
      }

      // POST /api/switches → update CPLD switch positions
      if (method === 'POST' && url.pathname === '/api/switches') {
        const body = await parseBody(req);

        // Validate and apply switch changes
        const valid3pos = ['auto', 'off', 'manual'];
        const valid2pos = ['auto', 'off'];
        const validStartStop = ['start', 'shutdown'];

        if ('startStop' in body && validStartStop.includes(body.startStop as string)) {
          switches.startStop = body.startStop as StartStopPosition;
        }
        if ('fan' in body && valid3pos.includes(body.fan as string)) {
          switches.fan = body.fan as SwitchPosition3;
        }
        if ('freshAir' in body && valid3pos.includes(body.freshAir as string)) {
          switches.freshAir = body.freshAir as SwitchPosition3;
        }
        if ('climacell' in body && valid3pos.includes(body.climacell as string)) {
          switches.climacell = body.climacell as SwitchPosition3;
        }
        if ('humid' in body && valid3pos.includes(body.humid as string)) {
          switches.humid = body.humid as SwitchPosition3;
        }
        if ('refrig' in body && valid2pos.includes(body.refrig as string)) {
          switches.refrig = body.refrig as SwitchPosition2;
        }

        console.log('[RS485] Switches updated:', JSON.stringify(switches));
        sendCPLDState();
        saveConfig('switches', switches);
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: true, switches }));
        return;
      }

      // POST /api/digital-io → toggle digital input proof signals
      if (method === 'POST' && url.pathname === '/api/digital-io') {
        const body = await parseBody(req);
        const board = body.board as string;

        // Handle expansion board install/uninstall
        if ('install' in body) {
          if (board === 'ex1') digitalInputs.ex1Installed = !!body.install;
          if (board === 'ex2') digitalInputs.ex2Installed = !!body.install;
          console.log(`[RS485] ${board} ${body.install ? 'installed' : 'removed'}`);
          sendCPLDState();
          saveConfig('digitalInputs', digitalInputs);
          res.writeHead(200, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ ok: true, digitalInputs }));
          return;
        }

        // Handle individual port toggle
        const port = typeof body.port === 'number' ? body.port : -1;
        const closed = !!body.closed;
        if (board === 'main' && port >= 0 && port < 10) {
          digitalInputs.main[port] = closed;
        } else if (board === 'ex1' && port >= 0 && port < 8) {
          digitalInputs.ex1[port] = closed;
        } else if (board === 'ex2' && port >= 0 && port < 8) {
          digitalInputs.ex2[port] = closed;
        } else {
          res.writeHead(400, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ ok: false, error: 'Invalid board/port' }));
          return;
        }
        console.log(`[RS485] DI ${board}[${port}] = ${closed ? 'CLOSED' : 'OPEN'}`);
        sendCPLDState();
        saveConfig('digitalInputs', digitalInputs);
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: true, digitalInputs }));
        return;
      }

      // POST /api/digital-io/invert → toggle inversion for a specific port
      if (method === 'POST' && url.pathname === '/api/digital-io/invert') {
        const body = await parseBody(req);
        const board = body.board as string;
        const port = typeof body.port === 'number' ? body.port : -1;

        const arr = board === 'main' ? invertedInputs.main
                  : board === 'ex1' ? invertedInputs.ex1
                  : board === 'ex2' ? invertedInputs.ex2
                  : null;
        const maxPort = board === 'main' ? 10 : 8;

        if (!arr || port < 0 || port >= maxPort) {
          res.writeHead(400, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ ok: false, error: 'Invalid board/port' }));
          return;
        }

        const idx = arr.indexOf(port);
        if (idx >= 0) {
          arr.splice(idx, 1); // remove — now close-to-prove
        } else {
          arr.push(port);     // add — now close-to-trigger
        }

        // Flip the contact state to the new default so it doesn't immediately alarm
        const nowInverted = arr.includes(port);
        const diArr = board === 'main' ? digitalInputs.main
                    : board === 'ex1' ? digitalInputs.ex1
                    : digitalInputs.ex2;
        // Inverted default = Open (false), Normal default = Closed (true)
        diArr[port] = !nowInverted;

        console.log(`[RS485] Inversion toggled: ${board}[${port}] → ${nowInverted ? 'INVERTED' : 'NORMAL'}, state reset to ${diArr[port] ? 'CLOSED' : 'OPEN'}`);
        sendCPLDState();
        saveConfig('invertedInputs', invertedInputs);
        saveConfig('digitalInputs', digitalInputs);
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: true, invertedInputs, digitalInputs }));
        return;
      }

      // ── Board management endpoints ──

      // GET /api/boards → list all board configs
      if (method === 'GET' && url.pathname === '/api/boards') {
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: true, boards: responder.getBoardConfigs() }));
        return;
      }

      // POST /api/boards/add → add a new board
      if (method === 'POST' && url.pathname === '/api/boards/add') {
        const body = await parseBody(req);
        const config = body as any;
        if (typeof config.address !== 'number' || config.address < 0 || config.address > 31) {
          res.writeHead(400, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ ok: false, error: 'Invalid address (0-31)' }));
          return;
        }
        if (!config.sensors || config.sensors.length !== 4) {
          res.writeHead(400, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ ok: false, error: 'Must have exactly 4 sensors' }));
          return;
        }
        const success = responder.addBoard(config);
        if (!success) {
          res.writeHead(400, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ ok: false, error: 'Address already in use or max 32 boards' }));
          return;
        }
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: true, boards: responder.getBoardConfigs() }));
        return;
      }

      // POST /api/boards/remove → remove a board
      if (method === 'POST' && url.pathname === '/api/boards/remove') {
        const body = await parseBody(req);
        const addr = body.address as number;
        if (typeof addr !== 'number') {
          res.writeHead(400, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ ok: false, error: 'Missing address' }));
          return;
        }
        if (addr <= 1) {
          res.writeHead(400, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ ok: false, error: 'Cannot remove core boards (addr 0-1)' }));
          return;
        }
        const success = responder.removeBoard(addr);
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: success, boards: responder.getBoardConfigs() }));
        return;
      }

      // POST /api/boards/sensor → update a sensor value on a specific board
      if (method === 'POST' && url.pathname === '/api/boards/sensor') {
        const body = await parseBody(req);
        const addr = body.address as number;
        const sensor = body.sensor as number;
        const value = body.value as number;
        if (typeof addr !== 'number' || typeof sensor !== 'number' || typeof value !== 'number') {
          res.writeHead(400, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ ok: false, error: 'Missing address, sensor, or value' }));
          return;
        }
        const success = responder.setBoardSensorValue(addr, sensor, value);
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: success }));
        return;
      }

      // POST /api/boards/update → update board properties
      if (method === 'POST' && url.pathname === '/api/boards/update') {
        const body = await parseBody(req);
        const addr = body.address as number;
        if (typeof addr !== 'number') {
          res.writeHead(400, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ ok: false, error: 'Missing address' }));
          return;
        }
        const success = responder.updateBoard(addr, body as any);
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: success }));
        return;
      }

      // POST /api/reset
      if (method === 'POST' && url.pathname === '/api/reset') {
        Object.assign(physics, defaultPhysics());
        Object.assign(overrides, defaultOverrides());
        Object.assign(equipment, defaultEquipment());
        Object.assign(switches, defaultSwitchState());
        Object.assign(digitalInputs, defaultDigitalInputs());
        Object.assign(cpldOutputs, defaultCPLDOutputs());
        responder.updateState({
          plenumTemp1: 7.5, plenumTemp2: 7.7, outsideTemp: 2.0, returnTemp: 12.5,
          outsideHumid: 65, plenumHumid: 72, returnHumid: 58, co2: 450,
        });
        lastPlenumTemp = 7.5;
        sendCPLDState();
        // Delete persisted configs so next startup uses defaults
        deleteConfig('physics');
        deleteConfig('switches');
        deleteConfig('digitalInputs');
        deleteConfig('boards');
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: true, physics, sensors: responder.getState() }));
        return;
      }

      // ── VFD Simulator API ──
      const vfd = (globalThis as any).__vfdSimulator;

      // GET /api/vfd → all drives
      if (method === 'GET' && url.pathname === '/api/vfd') {
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ drives: vfd?.getDrives() ?? [] }));
        return;
      }

      // POST /api/vfd/control → set control word + speed ref
      if (method === 'POST' && url.pathname === '/api/vfd/control') {
        const body = await parseBody(req);
        if (!vfd || typeof body.unitId !== 'number') {
          res.writeHead(400, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ ok: false, error: 'Missing unitId' }));
          return;
        }
        vfd.setControl(body.unitId as number, body.controlWord as number, body.speedRefPercent as number | undefined);
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok: true }));
        return;
      }

      // POST /api/vfd/fault → inject fault
      if (method === 'POST' && url.pathname === '/api/vfd/fault') {
        const body = await parseBody(req);
        if (!vfd || typeof body.unitId !== 'number') {
          res.writeHead(400, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ ok: false, error: 'Missing unitId' }));
          return;
        }
        const ok = vfd.injectFault(body.unitId as number, (body.faultCode as number) ?? 0x0032);
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok }));
        return;
      }

      // POST /api/vfd/add → add a drive
      if (method === 'POST' && url.pathname === '/api/vfd/add') {
        const body = await parseBody(req);
        if (!vfd || typeof body.unitId !== 'number') {
          res.writeHead(400, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ ok: false, error: 'Missing unitId' }));
          return;
        }
        const ok = vfd.addDrive(body.unitId as number, (body.label as string) ?? 'Drive', body as any);
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok }));
        return;
      }

      // POST /api/vfd/remove → remove a drive
      if (method === 'POST' && url.pathname === '/api/vfd/remove') {
        const body = await parseBody(req);
        if (!vfd || typeof body.unitId !== 'number') {
          res.writeHead(400, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ ok: false, error: 'Missing unitId' }));
          return;
        }
        const ok = vfd.removeDrive(body.unitId as number);
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok }));
        return;
      }

      // POST /api/vfd/update → update drive nameplate
      if (method === 'POST' && url.pathname === '/api/vfd/update') {
        const body = await parseBody(req);
        if (!vfd || typeof body.unitId !== 'number') {
          res.writeHead(400, { 'Content-Type': 'application/json' });
          res.end(JSON.stringify({ ok: false, error: 'Missing unitId' }));
          return;
        }
        const ok = vfd.updateDrive(body.unitId as number, body as any);
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ ok }));
        return;
      }

      res.writeHead(404, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ error: 'Not found' }));
    } catch (e) {
      res.writeHead(400, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify({ ok: false, error: 'Invalid request' }));
    }
  });

  server.listen(port, () => {
    console.log(`[RS485] Control Panel: http://localhost:${port}`);
    console.log(`[RS485] Physics: diurnal ${physics.outsideTempMin}\u00b0C\u2013${physics.outsideTempMax}\u00b0C, period ${physics.diurnalPeriodSec}s, potato mass ${physics.potatoMass}\u00d7`);
  });
}
