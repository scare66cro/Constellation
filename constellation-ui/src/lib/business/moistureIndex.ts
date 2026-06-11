/**
 * Moisture Loss Index ("mi") — 1:1 port of the AS2 2.0.1.b control-firmware calc
 * (Mini_IO/Application/States.c::MoistureIndex + CalculateMoistureLossIndex,
 * lines ~1672 / ~1705).
 *
 * It is STATELESS / instantaneous — a pure function of the current sensor
 * values + plenum setpoints + altitude + temperature unit (no accumulation, no
 * firmware-internal state). So computing it here yields the IDENTICAL number the
 * firmware emits as MainData[22]/[23] ("Moisture Loss 1/2", unit "mi"), per bay.
 *
 * MoistureIndex(temp,humid) = humidity ratio (mixing ratio) in g/kg:
 *   BP  = barometric pressure from altitude
 *   Svp = saturation vapour pressure at temp;  Avp = Svp * RH/100
 *   MI  = 0.622 * Avp/(BP-Avp) * 1000
 * MLI per bay = theoretical pickup per +1°C at setpoint
 *               − actual pickup (miReturn − miPlenum).
 */

/** tempType: 0 = Fahrenheit (converted to C), 1 = Celsius.
 *  altUnits: 0 = feet (converted to m), else metres. */
export function moistureIndex(
  temp: number,
  humid: number,
  tempType: number,
  altitude: number,
  altUnits: number,
): number {
  let tempC = temp;
  if (tempType === 0) tempC = (tempC - 32) / 1.8;
  let altM = altitude;
  if (altUnits === 0) altM *= 0.3048;
  const BP = 101.3 * Math.pow((293 - 0.0065 * altM) / 293, 5.26) * 100;
  const Svp = 6.11 * Math.pow(10, (7.5 * tempC) / (237.7 + tempC)) * 100;
  const Avp = Svp * (humid / 100);
  return (0.622 * Avp / (BP - Avp)) * 1000;
}

export interface MoistureLossInputs {
  plenumTempAvg?: number | null;   // PlenumTempAvg
  plenumHumid?: number | null;     // SENSOR_PLENUM_HUMID
  returnTemp?: number | null;      // SENSOR_RETURN_TEMP
  returnHumid1?: number | null;    // SENSOR_RETURN_HUMID_1 (bay 1)
  returnTemp2?: number | null;     // SENSOR_RETURN_TEMP_2  (bay 2, may be absent)
  returnHumid2?: number | null;    // SENSOR_RETURN_HUMID_2 (bay 2, may be absent)
  tempSet?: number | null;         // Settings.Plenum.TempSet
  humidSet?: number | null;        // Settings.Plenum.HumidSet
  altitude?: number | null;        // Settings.Climacell.Altitude
  altUnits?: number | null;        // Settings.Climacell.AltitudeUnits (0 = feet)
  tempType?: number | null;        // Settings.TempType (0 = F, 1 = C)
}

const fin = (v: unknown): v is number => Number.isFinite(Number(v));

/**
 * Per-bay moisture loss index. Returns null for a bay whose return sensors
 * aren't configured/valid (AS2 shows "--" — e.g. a single-bay system has no
 * return-2, so mli2 is null). Returns both null if the plenum setpoints (needed
 * for the theoretical reference) aren't available yet.
 */
export function moistureLossIndex(i: MoistureLossInputs): { mli1: number | null; mli2: number | null } {
  let mli1: number | null = null;
  let mli2: number | null = null;
  if (!fin(i.tempSet) || !fin(i.humidSet)) return { mli1, mli2 };

  const tempType = fin(i.tempType) ? Number(i.tempType) : 0;
  const altitude = fin(i.altitude) ? Number(i.altitude) : 0;     // sea level if not yet loaded
  const altUnits = fin(i.altUnits) ? Number(i.altUnits) : 0;
  const mi = (t: number, h: number) => moistureIndex(t, h, tempType, altitude, altUnits);

  const mliTheo = mi(Number(i.tempSet) + 1, Number(i.humidSet)) - mi(Number(i.tempSet), Number(i.humidSet));

  if (fin(i.plenumTempAvg) && fin(i.plenumHumid)) {
    const miPlenum = mi(Number(i.plenumTempAvg), Number(i.plenumHumid));
    if (fin(i.returnTemp) && fin(i.returnHumid1)) {
      mli1 = mliTheo - (mi(Number(i.returnTemp), Number(i.returnHumid1)) - miPlenum);
    }
    if (fin(i.returnTemp2) && fin(i.returnHumid2)) {
      mli2 = mliTheo - (mi(Number(i.returnTemp2), Number(i.returnHumid2)) - miPlenum);
    }
  }
  return { mli1, mli2 };
}
