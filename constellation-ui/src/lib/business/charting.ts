import { get } from 'svelte/store';
import { SYSTEM_MODE } from './mode';
import { dataSelectionStore, plotDataStore, cachedDataStore, historyStore, modeToColorStore, equipListStore, remoteListStore } from '$lib/store';
import { format } from 'date-fns';
import { t } from "svelte-i18n";
import { getAdornment, getHttpUrl, parseSensorFeeds, safeJsonParse, type SensorInfo } from './util';

export enum AxisType {
  Primary,
  Secondary,
};

export type MetricConfig = {
  display: boolean;
  axis: AxisType;
}

export type MetricConfigs = {
  [metric: string]: MetricConfig;
}

function getArrayFromIndex(eq: string[], index: number[]) {
  return index.map((i) => parseInt(eq[i], 10));
}

// Map sensor type to appropriate prefix for historical data lookup
function getSensorIdPrefix(type: string, humidCount: number, tempCount: number): [string, number, number, number] {
  switch(type) {
    case '1': // SENSOR_HUMID
    case '6': // SENSOR_RETURN_HUMID_1
    case '7': // SENSOR_RETURN_HUMID_2
    case '10': // SENSOR_PILE_HUMID
      return ['hsen', humidCount, humidCount + 1, tempCount];
    case '2': // SENSOR_CO2_1
      return ['csen', 9, humidCount, tempCount];
    case '8': // SENSOR_CO2_2
      return ['csen', humidCount, humidCount + 1, tempCount];
    case '11': // SENSOR_STATIC_PRESS
      return ['spress', humidCount, humidCount + 1, tempCount];
    case '0': // SENSOR_TEMP_IR
    case '3': // SENSOR_TEMP
    case '4': // SENSOR_RETURN_TEMP_1
    case '5': // SENSOR_RETURN_TEMP_2
    case '9': // SENSOR_PILE_TEMP
    default:
      return ['tsen', tempCount, humidCount, tempCount + 1];
  }
}

export function buildSensorList(systemLog: boolean, frontmatter: string[], setpoints: string[], sensors: string[] | any) : Array<{text: string, value: string, label: string, units: string}>
{
  const sensorList: Array<{text: string, value: string, label: string, units: string}> = [];
  const $t = get(t);
  try {
    const parsedSensors: SensorInfo[] = parseSensorFeeds(sensors);
    const sensorMap = new Map<number, SensorInfo>(parsedSensors.map((s) => [s.id, s]));
    if (frontmatter && frontmatter.length > 0) {
      if (systemLog) {
        sensorList.push({ text: $t('sensor-list.all-items'), value: 'all', label: 'All', units: '' });
        sensorList.push({ text: $t('sensor-list.equipment-status'), value: 'equip', label: 'Equip', units: '' });
        sensorList.push({ text: $t('sensor-list.remote-off-status'), value: 'remote', label: 'Remote Off', units: '' });
        sensorList.push({ text: $t('sensor-list.warnings'), value: 'warn', label: 'Warnings', units: '' });
      }
      
      // Get plenum temp sensor label from sensors array (first sensor, id=0)
      // Note: hsPlenTmp is the average of sensor 1 and 2, so we add (avg) to distinguish from individual sensors
      const plenumTempLabel = sensorMap.get(0)?.label ?? $t('global.plenum-temperature');
      sensorList.push({ text: $t('sensor-list.plenum-temp-setpoint'), value: 'hsPlenTmpSet', label: `${plenumTempLabel} SP`, units: '°' });
      sensorList.push({ text: $t('sensor-list.plenum-temperature'), value: 'hsPlenTmp', label: `${plenumTempLabel} (avg)`, units: '°' });
      if (frontmatter[5] !== 'dis') {
        // Get plenum humidity sensor label from sensors array (sensor id=5)
        const plenHumLabel = sensorMap.get(5)?.label ?? $t('global.plenum-humidity');
        sensorList.push({ text: $t('sensor-list.plenum-humidity'), value: 'hsen5', label: plenHumLabel, units: '%' });
      }
      
      // Get outside temp sensor label (sensor id=2)
      const outsideTempLabel = sensorMap.get(2)?.label ?? 'Out Temp';
      sensorList.push({ text: $t('sensor-list.outside-temperature'), value: 'tsen2', label: outsideTempLabel, units: '°' });
      
      // Get outside humidity sensor label (sensor id=4)
      const outsideHumidLabel = sensorMap.get(4)?.label ?? 'Out Hum';
      sensorList.push({ text: $t('sensor-list.outside-humidity'), value: 'hsen4', label: outsideHumidLabel, units: '%' });
      
      sensorList.push({ text: $t('sensor-list.cooling-available-temp'), value: 'hsCoolAvl', label: 'Cool Avl', units: '°' });
      sensorList.push({ text: $t('sensor-list.cooling-output'), value: 'hsCoolOut', label: 'Cool Output', units: '%' });
      if (frontmatter[9] !== 'dis') {
        // Get return temp sensor label (sensor id=3)
        const returnTempLabel = sensorMap.get(3)?.label ?? 'Return Temp';
        sensorList.push({ text: $t('sensor-list.return-temperature'), value: 'tsen3', label: returnTempLabel, units: '°' });
      }
      if (frontmatter[10] != 'dis') {
        // Get return humidity sensor label (sensor id=6)
        const returnHumidLabel = sensorMap.get(6)?.label ?? 'Return Hum';
        sensorList.push({ text: $t('sensor-list.return-humidity'), value: 'hsen6', label: returnHumidLabel, units: '%' });
      }
      sensorList.push({ text: $t('sensor-list.fan-speed'), value: 'hsFanSpd', label: 'Fan Speed', units: '%' });
      if (frontmatter[17] != 'dis') {
        // Get CO2 sensor label (sensor id=7)
        const co2Label = sensorMap.get(7)?.label ?? 'CO2';
        sensorList.push({ text: $t('sensor-list.co2-level'), value: 'csen7', label: co2Label, units: 'ppm' });
      }
      sensorList.push({ text: $t('sensor-list.refrigeration-output'), value: 'hsRefOut', label: 'Refrig Output', units: '%' });
      sensorList.push({ text: $t('sensor-list.mode'), value: 'hsMode', label: 'Mode', units: '' });
      sensorList.push({ text: $t('sensor-list.daily-fan-runtime'), value: 'hsFanRun', label: 'Fan Runtime', units: 'hrs' });

      if (frontmatter[0] === SYSTEM_MODE.ONION_MODE)
      {
        sensorList.push({ text: $t('sensor-list.burner-output'), value: 'hsBurnOut', label: 'Burner Output', units: '%' });
        sensorList.push({ text: $t('sensor-list.calculated-humidity'), value: 'hsCalcHum', label: 'Calc Hum', units: '%' });
      }

      // Get plenum 1 temp sensor label (sensor id=0, at index 0)
      const plen1TempLabel = sensorMap.get(0)?.label ?? 'Plen #1 Temp';
      sensorList.push({ text: $t('sensor-list.plenum-1-temperature'), value: 'tsen0', label: plen1TempLabel, units: '°' });
      
      // Get plenum 2 temp sensor label (sensor id=1)
      const plen2TempLabel = sensorMap.get(1)?.label ?? 'Plen #2 Temp';
      sensorList.push({ text: $t('sensor-list.plenum-2-temperature'), value: 'tsen1', label: plen2TempLabel, units: '°' });

      // Get plenum humidity sensor label from sensors array (sensor id=5)
      const plenumHumidLabel = sensorMap.get(5)?.label ?? $t('global.plenum-humidity');
      if (frontmatter[0] === SYSTEM_MODE.ONION_MODE && setpoints[2] === '1') {
        sensorList.push({ text: $t('sensor-list.return-air-humidity-setpoint'), value: 'hsPlenHumSet', label: 'Return Hum SP', units: '%' });
      } else {
        sensorList.push({ text: $t('sensor-list.plenum-humidity-setpoint'), value: 'hsPlenHumSet', label: `${plenumHumidLabel} SP`, units: '%' });
      }

      sensorList.push({ text: `${$t('sensor-list.moisture-loss')} #1`, value: 'mi1', label: 'MI #1', units: 'mi' });
      sensorList.push({ text: `${$t('sensor-list.moisture-loss')} #2`, value: 'mi2', label: 'MI #2', units: 'mi' });

      const secondarySensors = parsedSensors
        .filter((s) => s.id >= 8)
        .sort((a, b) => a.id - b.id);
      if (secondarySensors.length > 0) {
        let humidCount = 0;
        let tempCount = 0;
        let prefix = '';
        for (const sensor of secondarySensors) {
          let id = 0;
          [prefix, id, humidCount, tempCount] = getSensorIdPrefix(sensor.type, humidCount, tempCount);
          const sensorId = `${prefix}${id + 8}`;

          sensorList.push({
            text: sensor.label,
            value: sensorId,
            label: sensor.label,
            units: getAdornment(sensor.type, undefined, false)
          });
        }
      }
    }
  }
  catch (err)
  {
    console.error('Error building sensor list:', err);
  }
  return sensorList;
}
function parseNumericString(value: string): number | string {
  const num = parseFloat(value);
  return isNaN(num) ? value : num;
}
export async function getData() {
  const dataSelection = get(dataSelectionStore);
  const modeToColor = get(modeToColorStore);
  const allindex = dataSelection.selections.findIndex((item) => item.value === 'all');
  const selectedItems = dataSelection.selections.filter((_, index) => dataSelection.selected[index] || dataSelection.selected[allindex]);
  const equip = get(equipListStore);
  const remote = get(remoteListStore);
  
  plotDataStore.set({});

  const cachedData = get(cachedDataStore);
  const rangeData = get(historyStore);
  
  // Get the type-specific cache
  const typeCache = cachedData[rangeData.type] || {};
  
  const plotData: Record<string, string[] | number[] | number[][]> = {};
  for (const item of selectedItems) {
    if (typeCache[item.value] === undefined) {
      if (rangeData.type === 'User') {
        const url = new URL(getHttpUrl(`/iot/user/${item.value}`));
        const params: Record<string, string> = { 
          start: format(rangeData.startDate, "MM/dd/yyyy HH:mm:ss"),
          end: format(rangeData.endDate, "MM/dd/yyyy HH:mm:ss"),
        };
        Object.keys(params).forEach((key) => url.searchParams.append(key, params[key]));
        const response = await fetch(url);
        try{
          if (item.value === 'hsMode') {
            // Map numeric mode values to mode text descriptions for User logs
            typeCache[item.value] = (await safeJsonParse(response)).map((data: string) => {
              const idx = parseInt(data, 10);
              return modeToColor[idx]?.text ?? data;
            });
          } else {
            typeCache[item.value] = (await safeJsonParse(response)).map((data: string) => parseNumericString(data));
          }
        } catch (e) {
          console.error(e);
          typeCache[item.value] = [];
        }
      } else if (rangeData.type === 'Activity') {
        const url = new URL(getHttpUrl(`/iot/activity/${item.value === 'remote' ? 'equip' : item.value}`));
        const params: Record<string, string> = { start: rangeData.start, end: rangeData.end };
        Object.keys(params).forEach((key) => url.searchParams.append(key, params[key]))
        const response = await fetch(url);
        try{
          const data = await safeJsonParse(response);
          if (item.value === 'equip' || item.value === 'remote') {
            typeCache['equip'] = data.map((data: string[]) => getArrayFromIndex(data, equip.availEquip));
            typeCache['remote'] = data.map((data: string[]) => getArrayFromIndex(data, remote.availEquip));
          } else if (item.value === 'warn') {
            typeCache['warn'] = data;
          } else if (item.value !== 'all') {
            if (item.value === 'hsMode') {
              typeCache[item.value] = data.map((data: string) => modeToColor[parseInt(data, 10)]?.text)
            } else {
              typeCache[item.value] = data.map((data: string) => parseNumericString(data));
            }
          }
        } catch (e) {
          console.error(e);
          typeCache[item.value] = [];
        }
      }
    }
    if (item.value !== 'all') {
      plotData[item.value] = typeCache[item.value];
    }
  }
  
  // Update the store with the modified cache
  cachedData[rangeData.type] = typeCache;
  cachedDataStore.set({ ...cachedData });
  
  plotDataStore.set({ ...plotData });
}

export function toggleSelection(index: number) {
  const cachedData = get(cachedDataStore);
  const dataSelection = get(dataSelectionStore);
  const rangeData = get(historyStore);
  
  // Get the type-specific cache
  const typeCache = cachedData[rangeData.type] || {};

  dataSelection.selected[index] = !dataSelection.selected[index];
  // if warn is not selected then delete from cachedDataStore
  if (dataSelection.selections[index].value === 'warn' &&
    dataSelection.selected[index]) {
    // Remove 'warn' key and value from type-specific cache
    delete typeCache['warn'];
  }
  // if all is selected then unselect all others
  if (dataSelection.selections[index].value === 'all' &&
    dataSelection.selected[index]) {
    dataSelection.selected.forEach((_, i) => {
      if (index !== i) {
        dataSelection.selected[i] = false;
      }
    });
  } else if (dataSelection.selected[index]) {
    // if any other is selected then unselect all if it is selected
    const allindex = dataSelection.selections.findIndex((item) => item.value === 'all');
    const warnindex = dataSelection.selections.findIndex((item) => item.value === 'warn');
    if (allindex !== -1) {
      if (dataSelection.selected[allindex]) {
        dataSelection.selected[allindex] = false;
        // remove warn from type-specific cache if it is not selected
        if (warnindex !== index) {
          delete typeCache['warn'];
        }
      }
    }
  }
  
  // Update the store with the modified cache
  cachedData[rangeData.type] = typeCache;
  dataSelectionStore.set({ ...dataSelection });
  cachedDataStore.set({ ...cachedData });
}

export function clearCacheForType(logType: string) {
  const cachedData = get(cachedDataStore);
  
  // Clear the cache for the specific log type
  if (cachedData[logType]) {
    cachedData[logType] = {};
    cachedDataStore.set({ ...cachedData });
  }
}
