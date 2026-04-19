export enum BoardTypes {
  BOARD_TEMP_IR = '0',
  BOARD_HUMID = '1',
  BOARD_CO2 = '2',
  BOARD_TEMP = '3',
}

// must match iotclient
export enum SensorTypes {
  SENSOR_TEMP_IR = '0',
  SENSOR_HUMID = '1',
  SENSOR_CO2_1 = '2',
  SENSOR_TEMP = '3',
  SENSOR_RETURN_TEMP_1 = '4',
  SENSOR_RETURN_TEMP_2 = '5',
  SENSOR_RETURN_HUMID_1 = '6',
  SENSOR_RETURN_HUMID_2 = '7',
  SENSOR_CO2_2 = '8',
  SENSOR_PILE_TEMP = '9',
  SENSOR_PILE_HUMID = '10',
  SENSOR_STATIC_PRESS = '11',
  SENSOR_UNDEFINED = '255',
}

export type SensorInfo = {
  id: number;
  label: string;
  type: string;
  value: string | null;
  offset: string;
  disabled: boolean;
};

export function isValidSensor(value: string | undefined): boolean {
  return (value && value !== 'dis' && value !== '--') ? true : false;
}
