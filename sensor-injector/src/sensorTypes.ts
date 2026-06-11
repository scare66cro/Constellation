/**
 * adcConversion.ts — engineering value → orbit HR int16 (mirror of
 * Nova_Firmware/lp_am2434/orbit_server/adc_convert.c).
 *
 * The injector takes user-friendly engineering inputs (°C, %RH, ppm)
 * and writes them into the orbit LP's HR 200..263 in the same int16
 * format the firmware expects. We do NOT reverse through the NTC LUT
 * to a raw ADC value — the orbit LP firmware stores engineering int16
 * directly in HR 200..263 (the RTU sweep, when present, performs the
 * ADC→eng conversion before writing). So injection is just:
 *   - TEMP / IR_TEMP : round(°C * 10)
 *   - HUMID          : round(%RH * 10)
 *   - CO2            : round(ppm)
 *   - STATIC_P       : round("wc * 100)   (×100 wire — Gellert-only encoding)
 *   - NONE           : 0x7FFF (UNDEF)
 *
 * The injector pushes "wc directly (no unit conversion) — e.g.
 * engToHrInt16(1.25, 11) → 125. The Nova controller descales ÷100.
 */

export const SENSOR_VAL_UNDEF = 0x7FFF;

export const SENSOR_TYPE_IR_TEMP = 0;
export const SENSOR_TYPE_HUMID   = 1;
export const SENSOR_TYPE_CO2     = 2;
export const SENSOR_TYPE_TEMP    = 3;
// NEW Gellert 4-20 mA static-pressure transducer (0–2.5 "wc). Nibble 11 matches
// AS2 (ANALOG_SENSOR_TYPE_STATIC_PRESS=11), Nova firmware, and the UI.
export const SENSOR_TYPE_STATIC_PRESSURE = 11;
export const SENSOR_TYPE_NONE    = 0xF;

export type SensorType = 0 | 1 | 2 | 3 | 11 | 0xF;

export function engToHrInt16(value: number, type: SensorType): number {
  if (!Number.isFinite(value)) return SENSOR_VAL_UNDEF;
  switch (type) {
    case SENSOR_TYPE_IR_TEMP:
    case SENSOR_TYPE_TEMP:
    case SENSOR_TYPE_HUMID:
      return Math.round(value * 10) & 0xFFFF;
    case SENSOR_TYPE_CO2:
      return Math.round(value) & 0xFFFF;
    case SENSOR_TYPE_STATIC_PRESSURE:
      return Math.round(value * 100) & 0xFFFF;  // ×100 ("wc one-hundredths)
    default:
      return SENSOR_VAL_UNDEF;
  }
}

/** Decode an HR int16 back to engineering value for display. */
export function hrInt16ToEng(raw: number, type: SensorType): number | null {
  if (raw === SENSOR_VAL_UNDEF) return null;
  // Sign-extend
  const s = raw & 0x8000 ? raw - 0x10000 : raw;
  switch (type) {
    case SENSOR_TYPE_IR_TEMP:
    case SENSOR_TYPE_TEMP:
    case SENSOR_TYPE_HUMID:
      return s / 10;
    case SENSOR_TYPE_CO2:
      return s;
    case SENSOR_TYPE_STATIC_PRESSURE:
      return s / 100;   // ×100 wire → "wc
    default:
      return null;
  }
}

export function typeName(t: SensorType): string {
  switch (t) {
    case SENSOR_TYPE_IR_TEMP: return 'IR_TEMP';
    case SENSOR_TYPE_HUMID:   return 'HUMID';
    case SENSOR_TYPE_CO2:     return 'CO2';
    case SENSOR_TYPE_TEMP:    return 'TEMP';
    case SENSOR_TYPE_STATIC_PRESSURE: return 'STATIC_P';
    case SENSOR_TYPE_NONE:    return 'NONE';
    default:                  return '?';
  }
}
