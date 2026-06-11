/**
 * ADC → Engineering Unit Conversions (forward only)
 * ──────────────────────────────────────────────────
 * Used by the Orbit simulator to convert raw ADC × 16 values received
 * from sensor board RTU slaves into engineering units for the Modbus TCP
 * register block that Nova reads.
 *
 * All formulas match Mini_IO/Application/Analog_Input.c exactly.
 */

// ─── NTC Temperature Lookup Table ──────────────────────────────────────────
const TEMP_TABLE: ReadonlyArray<{ degC: number; rt: number }> = [
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

const Rg1   = 27400;
const Rg2   = 10000;
const Rth1  = 10000;
const Rth2  = 1000;
const R1ref = 30100;
const R2ref = 15000;
const BRIDGE_NUMERATOR = 1024.0 * (1.0 + Rg1 / Rg2) * Rth2 * (R1ref + R2ref);

export const SENSOR_VAL_UNDEF = 0x7FFF;

export const SENSOR_TYPE_IR_TEMP = 0;
export const SENSOR_TYPE_HUMID   = 1;
export const SENSOR_TYPE_CO2     = 2;
export const SENSOR_TYPE_TEMP    = 3;
// NEW Gellert sensor type — 4-20 mA static-pressure transducer (0–2.5 "wc).
// Nibble value 11 matches AS2 (ANALOG_SENSOR_TYPE_STATIC_PRESS=11), the Nova
// orbit firmware, and the UI AnalogConfigForm sensor-type '11'. Fits the 4-bit
// type nibble (0–15). DO NOT renumber.
export const SENSOR_TYPE_STATIC_PRESSURE = 11;
export const SENSOR_TYPE_NONE    = 0xF;

/**
 * Convert raw ADC × 16 → temperature in °C.
 * Returns NaN if out of table range (firmware returns SENSOR_VAL_UNDEFINED).
 */
export function adcToTemp(adc: number): number {
  if (adc <= 0 || adc === SENSOR_VAL_UNDEF) return NaN;
  const adcDiv16 = adc / 16.0;
  const rt = BRIDGE_NUMERATOR / (adcDiv16 * R2ref) - (Rth1 + Rth2);

  if (rt > TEMP_TABLE[0].rt || rt < TEMP_TABLE[TEMP_TABLE.length - 1].rt) return NaN;

  let i = 0;
  while (i < TEMP_TABLE.length - 1 && TEMP_TABLE[i].rt > rt) i++;
  if (i === 0) return TEMP_TABLE[0].degC;

  const xa = TEMP_TABLE[i - 1].rt, ya = TEMP_TABLE[i - 1].degC;
  const xb = TEMP_TABLE[i].rt,     yb = TEMP_TABLE[i].degC;
  return ya + ((rt - xa) * (yb - ya)) / (xb - xa);
}

/** Convert raw ADC × 16 → humidity (0–100 %RH). */
export function adcToHumid(adc: number): number {
  if (adc <= 0 || adc === SENSOR_VAL_UNDEF) return NaN;
  return ((adc / 16.0) - 180.0) / 720.0 * 100.0;
}

/** Convert raw ADC × 16 → CO₂ (ppm). */
export function adcToCo2(adc: number): number {
  if (adc <= 0 || adc === SENSOR_VAL_UNDEF) return NaN;
  const scaled = adc / 16.0;
  if (scaled > 895) return NaN;
  return ((scaled - 180.0) / 720.0) * 10000.0;
}

/**
 * Convert raw ADC × 16 → static pressure (inches water column, 0–2.5 "wc).
 *
 * Verbatim AS2 `ConvertToStaticPressure` math (the AS2 SOURCE has the
 * conversion shape; only the ×100 wire encoding below is the new Gellert
 * addition — see adcToOrbitRegister):
 *   scaled = adc / 16
 *   scaled <  180  → UNDEF  (below the 4 mA floor)
 *   scaled >  900  → 2.5    (clamp at full scale / 20 mA)
 *   else           → ((scaled - 180) / 720) * 2.5
 *
 * Returns NaN below 180 (firmware returns SENSOR_VAL_UNDEFINED).
 */
export function adcToStaticP(adc: number): number {
  if (adc <= 0 || adc === SENSOR_VAL_UNDEF) return NaN;
  const scaled = adc / 16.0;
  if (scaled < 180.0) return NaN;       // below 4 mA → UNDEF
  if (scaled > 900.0) return 2.5;       // clamp at full scale
  return ((scaled - 180.0) / 720.0) * 2.5;
}

/**
 * Convert raw ADC × 16 to engineering value, then scale for Orbit's
 * sensor register format.
 *
 * Orbit register format (matching nova_thread_overrides.c expectations):
 *   • Temp/Humid:   value × 10,  as int16   (e.g., 21.5°C → 215)
 *   • CO2:          raw ppm,     as int16   (e.g., 800 ppm → 800)
 *   • Static press: value × 100, as int16   (e.g., 1.25 "wc → 125)
 *
 * ⚠ Static pressure uses a ×100 wire encoding — NOT the ×10 temp/humid
 *   convention. This is a NEW Gellert encoding with no AS2 ancestor: AS2
 *   stored static pressure as a plain integer "wc, the ×100 one-hundredths
 *   packing is Constellation-only. The Nova controller descales ÷100. Keep
 *   this bit-exact with the orbit firmware adc_convert.c static-pressure case.
 *
 * Returns SENSOR_VAL_UNDEF (0x7FFF) if the conversion fails.
 */
export function adcToOrbitRegister(adc: number, sensorType: number): number {
  if (adc === SENSOR_VAL_UNDEF || adc <= 0) return SENSOR_VAL_UNDEF;

  let eng: number;
  switch (sensorType) {
    case SENSOR_TYPE_IR_TEMP:
    case SENSOR_TYPE_TEMP:
      eng = adcToTemp(adc);
      if (isNaN(eng)) return SENSOR_VAL_UNDEF;
      return Math.round(eng * 10) & 0xFFFF;  // × 10

    case SENSOR_TYPE_HUMID:
      eng = adcToHumid(adc);
      if (isNaN(eng)) return SENSOR_VAL_UNDEF;
      return Math.round(eng * 10) & 0xFFFF;  // × 10

    case SENSOR_TYPE_CO2:
      eng = adcToCo2(adc);
      if (isNaN(eng)) return SENSOR_VAL_UNDEF;
      return Math.round(eng) & 0xFFFF;       // raw ppm

    case SENSOR_TYPE_STATIC_PRESSURE:
      eng = adcToStaticP(adc);
      if (isNaN(eng)) return SENSOR_VAL_UNDEF;
      return Math.round(eng * 100) & 0xFFFF; // × 100 (one-hundredths "wc)

    default:
      return SENSOR_VAL_UNDEF;
  }
}
