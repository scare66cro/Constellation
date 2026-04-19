import 'reflect-metadata';
import { DataSource } from 'typeorm';
import IoTLog from './entity/IoTLog';
import IoTClient from './entity/IoTClient';
import { getTime } from 'date-fns';

// eslint-disable-next-line @typescript-eslint/no-explicit-any
function processMessage(message: any): { realtime: any, settings: any, frontMatter: any } {
  let pileAvg = 0;
  const payload = message.payload ? message.payload : message;
  if (payload.Sensors?.length > 8) {
    const sensors = payload.Sensors.reduce((result: Array<string[]>, _: string, index: number, array: string[]) => {
      if (index % 4 === 0) {
          result.push(array.slice(index, index + 4));
      }
      return result;
    }, []);
    const valid = sensors.filter((i: string[], index: number) => index >= 2 && i[3] !== '--' && i[2] === '9');
    if (valid.length > 0) {
        pileAvg = valid.reduce(
        (prev: number, current: number) => prev + current[3] * 1,
        0,
        ) / valid.length;
    }
  } else if (payload.PileTempsData?.length > 0) {
    const valid = payload.PileTempsData?.filter((i: string, index: number) => index % 2 === 0 && i !== '--');
    if (valid.length > 0) {
      pileAvg = valid.reduce(
        (prev: number, current: number) => prev + current * 1,
        0,
      ) / valid.length;
    }
  }
  const beeMode = payload.P2BasicSetupData[4] === '2';
  const purgeMode = payload.Co2PurgeData[0];
  const co2SetPoint = purgeMode === '2' ? payload.Co2PurgeData[beeMode ? 7 : 4] : '1200';

  const realtime = {
    Type: payload.MainData.length > 20 ? 'Realtime3' : 'Realtime2',
    StorageID: message.deviceId,
    UnixTime: getTime(message.timestamp),
    AlarmData: payload.AlarmData,
    MainData: payload.MainData,
    EquipStatusData: payload.EquipStatusData,
    CurrentMode: payload.CurrentMode,
    DailyFanRun: payload.DailyFanRun,
    TotalFanRun: payload.TotalFanRun,
    DateTimeData: payload.DateTimeData,
    PgmData: payload.PgmData,
    PileTempsData: (
      payload.PileTempsData?.length > 1 ? payload.PileTempsData : null),
    PileHumidsData: (
      payload.PileHumidsData?.length > 1 ? payload.PileHumidsData : null),
  };
  const settings = {
    IoTClientVersion: payload.IoTClientVersion,
    Protocol: payload.Protocol,
    BoardType: payload.BoardType,
    OutsideAirData: payload.OutsideAirData,
    AirCureData: payload.AirCureData,
    FreqCtrlData: payload.FreqCtrlData,
    RampRateData: payload.RampRateData,
    HumidCtrlData: payload.HumidCtrlData,
    Co2PurgeData: payload.Co2PurgeData,
    MiscData: payload.MiscData,
    PlenTempDevData: payload.PlenTempDevData,
    HumidModes: payload.HumidModes,
    PileTempsLabels: payload.PileTempsLabels,
    PileHumidsLabels: payload.PileHumidsLabels,
    AvailableIoData: payload.AvailableIoData,
    ClimacellTimesData: payload.ClimacellTimesData,
    LtxVersion: payload.LtxVersion,
    PgmData: payload.PgmData,
    P2BasicSetupData: payload.P2BasicSetupData,
    ControllerList: payload.ControllerList,
    DisplayList: payload.DisplayList,
    LoadMonitorData: payload.LoadMonitorData,

    P2AnalogBoardData: payload.P2AnalogBoardData,
    P2FreshAirData: payload.P2FreshAirData,
    P2RefrigerationData: payload.P2RefrigerationData,
    P2ClimacellData: payload.P2ClimacellData,
    P2FailuresData: payload.P2FailuresData,
    P2Failures2Data: payload.P2Failures2Data,
    P2ServiceData: payload.P2ServiceData,
    P2BurnerData: payload.P2BurnerData,
    UserAccounts: payload.UserAccounts,
    AlertSetupData: payload.AlertSetupData,
    P2PwmChannelData: payload.P2PwmChannelData,
    OutputConfigData: payload.OutputConfigData,
    InputConfigData: payload.InputConfigData,
    AuxProgramData: payload.AuxProgramData,
    AuxSwitchesData: payload.AuxSwitchesData,
    SysVersion: payload.SysVersions,
    EmailAlertData: payload.EmailAlertData,
    IoNames: payload.IoNames,
    UserLogSettings: payload.UserLogSettings,
  };
  const frontMatter = {
    main: [
      payload.CurrentMode?.[0], // 0 - currentMode
      payload.P2BasicSetupData?.[1], // 1 - TempType
      payload.MainData?.[0], // 2 - plenumTemp
      payload.PgmData?.[0], // 3 - plenumTempSet
      payload.PgmData.length > 5 ? payload.PgmData[5] : null, // 4 - plenumTempSet2
      payload.MainData?.[1], // 5 - plenumHumid
      payload.PgmData?.[1], // 6 - plenumHumidSet
      payload.MainData?.[2], // 7 - outsideTemp
      payload.MainData?.[4], // 8 - outsideHumid
      payload.MainData?.[8], // 9 - returnTemp
      payload.MainData?.[7], // 10 - returnHumid
      payload.AirCureData?.[0], // 11 - CureStartTemp
      payload.AirCureData?.[2], // 12 - CureStartHumid
      pileAvg.toString(), // 13 - PileTempAvg
      payload.MainData?.[10], // 14 - fanSpeed
      payload.MainData.length > 20 ? payload.MainData?.[18] : payload.MainData?.[11], // 15 - Cooling Output or (cooling/refrig)
      payload.MainData?.[12], // 16 - Mode
      payload.MainData?.[9], // 17 - co2Level
      payload.MainData?.[13], // 18 - BurnerOutput
      payload.EquipStatusData?.[56], // 19 - BayLight1
      payload.LoadMonitorData?.[0], // 20 - BayLight1Name
      payload.EquipStatusData?.[58], // 21 - BayLight2
      payload.LoadMonitorData?.[1], // 22 - BayLight2Name
      payload.P2BasicSetupData?.[4], // 23 - SystemMode (Potato, Onion, etc.)
      payload.EquipStatusData?.[8], // 24 - Cure Output
      payload.EquipStatusData?.[52], // 25 - Cure Remote Off
      payload.PlenTempDevData?.[4], // 26 - Cure Temp Low
      payload.PlenTempDevData?.[5], // 27 - Cure Temp High
      payload.AirCureData?.[3], // 28 - Cure Humid High Limit
      payload.PgmData?.[2], // 29 - Plenum Humid Reference
      payload.AirCureData?.[1], // 30 - Cure Humid Reference
      payload.MainData?.[14], // 31 - Calc Humid
      payload.Co2PurgeData?.[0], // 32 - CO2 Purge Mode
      co2SetPoint, // 33 - CO2 Set Point
      payload.MainData?.[17], // 34 - Refrigeration Output
      payload.MainData?.[19], // 35 - Return Humidity 2
      payload.MainData?.[20], // 36 - CO2 2
      payload.MainData?.[21], // 37 - Return Temp 2
      payload.MainData?.[22], // 38 - Moisture Loss Index 1
      payload.MainData?.[23], // 39 - Moisture Loss Index 2
    ],
    misc: [
      payload.BoardType?.[0], // 0 - BoardType
      payload.P2BasicSetupData?.[0], // 1 - panelName
      payload.IoTClientVersion, // 2 - IoTClientVersion
      payload.SysVersions?.[0], // 3 - ControllerVersion
    ],
    AlarmData: payload.AlarmData,
  };

  return { realtime, settings, frontMatter };
}

/**
 * Receives JSON objects { deviceId: , messageId: , timestamp: , payload: ,}
 * When using the function locally make sure there is a trailing comma?
 * @param context Context
 * @param IoTHubMessages messages from iot client
 */
export async function IoTHubTrigger(
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  messages: any,
): Promise<void> {
  const logs: IoTLog[] = [];
  console.log(`Processing messages: ${JSON.stringify(messages.bindings.IoTHubMessages.length)}`)
  const connection = new DataSource({
    type: 'postgres',
    host: process.env.DB_Host,
    port: parseInt(process.env.DB_Port, 10),
    username: process.env.DB_User,
    password: process.env.DB_Password,
    database: process.env.DB_Name,
    entities: [
      'dist/IoTHub_EventHub/entity/**/*.js',
    ],
    ssl: true,
  });
  const connect = async () => {
    await connection.initialize();
  };
  try {
    await connect();
    messages.bindings.IoTHubMessages.forEach(async (message) => {
      const { realtime, settings, frontMatter } = processMessage(message);
      const log = new IoTLog();
      log.timeStamp = message.timestamp;

      log.iotClientId = message.deviceId;
      log.payload = realtime;
      logs.push(log);
      try {
        await connection
          .createQueryBuilder()
          .update(IoTClient)
          .set({
            last_log: message.payload ? message : { payload: message },
            realtime,
            front_matter: frontMatter,
            settings,
            time_stamp: log.timeStamp,
            time_zone: message.timezone ?? Intl.DateTimeFormat().resolvedOptions().timeZone,
          })
          .where('id = :id', { id: message.deviceId })
          .execute();
      } catch (e) {
        console.log('Error updating client', e.message);
      }
    });
    try {
      await connection.manager.save(logs);
    } catch (e) {
      console.log('Error saving logs', e.message);
    }
  } catch (e) {
    console.log('Other error', e.message);
  } finally {
    if (connection) {
      await connection.destroy();
    }
  }
};

