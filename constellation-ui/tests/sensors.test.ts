import { expect, test } from '@playwright/test';
import { parseSensorFeeds } from '$lib/business/sensorFeeds';
import { SensorTypes } from '$lib/business/analog';

test.describe('Sensor feed parsing (unified)', () => {
  test('handles SensorValues without labels/settings (RealTime frame)', () => {
    const payload = {
      SensorValues: ['0', '12.3', '5', '--'],
    } as unknown;

    const sensors = parseSensorFeeds(payload);

    expect(sensors).toHaveLength(2);
    expect(sensors[0]).toEqual({
      id: 0,
      label: '',
      type: SensorTypes.SENSOR_UNDEFINED,
      value: '12.3',
      offset: '0.0',
      disabled: false,
    });
    expect(sensors[1]).toEqual({
      id: 5,
      label: '',
      type: SensorTypes.SENSOR_UNDEFINED,
      value: '--',
      offset: '0.0',
      disabled: false,
    });
  });

  test('applies labels/settings when provided', () => {
    const payload = {
      SensorLabels: ['0', 'Outside Temp', '5', 'Plenum Humid'],
      SensorValues: ['0', '12.3', '5', '85'],
      SensorSettings: ['0', SensorTypes.SENSOR_TEMP, '0.1', '0', '5', SensorTypes.SENSOR_HUMID, '0.0', '1'],
    } as unknown;

    const sensors = parseSensorFeeds(payload);

    expect(sensors).toEqual([
      {
        id: 0,
        label: 'Outside Temp',
        type: SensorTypes.SENSOR_TEMP,
        value: '12.3',
        offset: '0.1',
        disabled: false,
      },
      {
        id: 5,
        label: 'Plenum Humid',
        type: SensorTypes.SENSOR_HUMID,
        value: '85',
        offset: '0.0',
        disabled: true,
      },
    ]);
  });
});

test.describe('Sensor feed parsing (legacy fallback)', () => {
  test('supports legacy stride-6 Sensors array when unified feeds absent', () => {
    const payload = {
      Sensors: [
        'Outside Temp', '0', SensorTypes.SENSOR_TEMP, '12.3', '0.1', '0',
        'Plenum Humid', '5', SensorTypes.SENSOR_HUMID, '85', '0.0', '1',
      ],
    } as unknown;

    const sensors = parseSensorFeeds(payload);

    expect(sensors).toEqual([
      {
        id: 0,
        label: 'Outside Temp',
        type: SensorTypes.SENSOR_TEMP,
        value: '12.3',
        offset: '0.1',
        disabled: false,
      },
      {
        id: 5,
        label: 'Plenum Humid',
        type: SensorTypes.SENSOR_HUMID,
        value: '85',
        offset: '0.0',
        disabled: true,
      },
    ]);
  });
});