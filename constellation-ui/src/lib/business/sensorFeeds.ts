import { SensorTypes } from './analog';

export type SensorInfo = {
  id: number;
  label: string;
  type: string;
  value: string;
  offset: string;
  disabled: boolean;
};

const TEMP_SENSOR_TYPES = new Set([
  SensorTypes.SENSOR_TEMP_IR,
  SensorTypes.SENSOR_TEMP,
  SensorTypes.SENSOR_RETURN_TEMP_1,
  SensorTypes.SENSOR_RETURN_TEMP_2,
  SensorTypes.SENSOR_PILE_TEMP,
]);

const HUMID_SENSOR_TYPES = new Set([
  SensorTypes.SENSOR_HUMID,
  SensorTypes.SENSOR_RETURN_HUMID_1,
  SensorTypes.SENSOR_RETURN_HUMID_2,
  SensorTypes.SENSOR_PILE_HUMID,
  SensorTypes.SENSOR_STATIC_PRESS,
]);

function coerceArrayCandidate(payload: unknown): unknown {
  if (payload && typeof payload === 'object' && 'array' in (payload as any) && Array.isArray((payload as any).array)) {
    return (payload as any).array;
  }
  return payload;
}

export function isTempSensorType(type: string | undefined): boolean {
  return type ? TEMP_SENSOR_TYPES.has(type as SensorTypes) : false;
}

export function isHumidSensorType(type: string | undefined): boolean {
  return type ? HUMID_SENSOR_TYPES.has(type as SensorTypes) : false;
}

export function sensorDisplayValue(sensor: SensorInfo): string {
  if (sensor.disabled) return 'dis';
  return sensor.value ?? '--';
}

/**
 * Parses unified sensor feeds (SensorLabels/SensorValues/SensorSettings) into a typed list.
 * Falls back to legacy Sensors stride-6 arrays when unified feeds are absent.
 */
export function parseSensorFeeds(payload: unknown): SensorInfo[] {
  const candidate = coerceArrayCandidate(payload);

  const labels = (candidate as any)?.SensorLabels;
  const values = (candidate as any)?.SensorValues;
  const settings = (candidate as any)?.SensorSettings;

  const hasUnified = Array.isArray(labels) || Array.isArray(values) || Array.isArray(settings);

  if (hasUnified) {
    const labelMap = new Map<number, string>();
    const valueMap = new Map<number, string>();
    const settingsMap = new Map<number, { type: string; offset: string; disabled: boolean }>();

    if (Array.isArray(labels)) {
      for (let i = 0; i < labels.length; i += 2) {
        const sid = Number.parseInt(labels[i], 10);
        if (!Number.isNaN(sid)) labelMap.set(sid, labels[i + 1] ?? '');
      }
    }

    if (Array.isArray(values)) {
      for (let i = 0; i < values.length; i += 2) {
        const sid = Number.parseInt(values[i], 10);
        if (!Number.isNaN(sid)) valueMap.set(sid, values[i + 1] ?? '--');
      }
    }

    if (Array.isArray(settings)) {
      for (let i = 0; i < settings.length; i += 4) {
        const sid = Number.parseInt(settings[i], 10);
        if (!Number.isNaN(sid)) {
          settingsMap.set(sid, {
            type: settings[i + 1] ?? SensorTypes.SENSOR_UNDEFINED,
            offset: settings[i + 2] ?? '0.0',
            disabled: (settings[i + 3] ?? '0') === '1',
          });
        }
      }
    }

    const ids = new Set<number>([...labelMap.keys(), ...valueMap.keys(), ...settingsMap.keys()]);

    return [...ids].sort((a, b) => a - b).map((id) => {
      const settingsEntry = settingsMap.get(id);
      return {
        id,
        label: labelMap.get(id) ?? '',
        type: settingsEntry?.type ?? SensorTypes.SENSOR_UNDEFINED,
        value: valueMap.get(id) ?? '--',
        offset: settingsEntry?.offset ?? '0.0',
        disabled: settingsEntry?.disabled ?? false,
      };
    });
  }

  const legacySensors = Array.isArray((candidate as any)?.Sensors)
    ? (candidate as any)?.Sensors
    : Array.isArray((candidate as any)?.sensors)
      ? (candidate as any)?.sensors
      : Array.isArray(candidate)
        ? candidate
        : [];

  if (!Array.isArray(legacySensors) || legacySensors.length === 0) return [];

  const stride = 6;
  const result: SensorInfo[] = [];
  for (let i = 0; i < legacySensors.length; i += stride) {
    const id = Number.parseInt(legacySensors[i + 1] ?? '-1', 10);
    if (Number.isNaN(id)) continue;
    result.push({
      id,
      label: legacySensors[i] ?? '',
      type: legacySensors[i + 2] ?? SensorTypes.SENSOR_UNDEFINED,
      value: legacySensors[i + 3] ?? '--',
      offset: legacySensors[i + 4] ?? '0',
      disabled: (legacySensors[i + 5] ?? '0') === '1',
    });
  }

  return result;
}
