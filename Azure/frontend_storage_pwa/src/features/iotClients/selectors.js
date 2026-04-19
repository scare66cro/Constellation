import { FormattedMessage } from "react-intl";
import { getCustomizedTemperature } from "../../utilities/appUnitsOfMeasurements";
import { agristar2NumberToModeMap, modeToCategoryMap, tempTypeMap } from "../../utilities/dataMapAgristar2";
import { as2AlarmTranslations } from "../../utilities/translationObjects";
/*
   Notes:
   - There are 3 concepts in this file:
      1- SELECTORS -> these are passed state and are for selecting specific parts of state and can be memoised
      2- EXTRACTORS -> these are passed objects and sometime parameters and extract very specific things from the given objects
      3- INTERPRETERS -> these are meant to leverage selected and extracted 
      .......data and interpret from them meaningful things like fanRemoteOff = 0 means the fan is not remotely off (can still be off though)
   - See bottom for data schema examples
   - Guidelines for using selectors. See for more details https://redux.js.org/usage/deriving-data-selectors
*/

export const selectActiveToken = state => state.iotClients?.activeToken;
export const selectTokenRetrieved = state => state.iotClients?.tokenRetrieved;
export const selectStatus = state => state.iotClients?._status
export const selectIoTStatus = state => state.iotClients?._iot;
export const selectIoTClientList = state => state.iotClients?.iotClients
export const selectSelectedIoTClient = state => selectIoTClientList(state)?.find(i => i.id === state.iotClients._selectedIotClient)
export const selectIoTClientById = (state, id) => selectIoTClientList(state)?.find(i => i.id === id)
export const selectIoTClientsBySite = (state, id) => selectIoTClientList(state)?.filter(i => i.site === id)
export const selectUpgrade = (state) => state.sites.upgrade;
export const selectSaving = (state) => state.iotClients?.saving;
export const isVersionAtLeast = (version, major, minor, patch) => {
   if (!version) return false;
   const [maj = 0, min = 0, pat = 0] = version.split('.').map((i) => parseInt(i, 10));
   if (maj > major) return true;
   if (maj === major && min > minor) return true;
   if (maj === major && min === minor && pat >= patch) return true;
   return false;
}

// -----------------------------EXTRACTORS---------------------------------------------
// extractors -> expect a specific type of object and extract a specific value from it
export const extractLastLogFromIoTClient = (iotClient) => iotClient?.last_log
export const extractPermissionsFromIoTClient = (iotClient, permLevel) => iotClient?.obj_permissions

// ------------------------AGRISTAR2 Payload EXTRACTORS v1.0.0 Potato mode----------------------------------------
export const extractAgristar2PayloadFromLastLog = (lastLog) => lastLog?.payload?.payload ? lastLog?.payload?.payload: lastLog?.payload;

export const extractAgristar2PayloadFromIoTClient = (iotClient) => {
   const last_log = extractLastLogFromIoTClient(iotClient)
   return extractAgristar2PayloadFromLastLog(last_log)
}
export const extractASBoardTypeFromIoTClient = (iotClient) => {
   const payload = extractAgristar2PayloadFromIoTClient(iotClient)
   return payload?.['BoardType']?.[0] || 'AS2'
}

export const extractAgristar1AlertSetupDataFromIoTClient = (iotClient) => {
   const payload = extractAgristar2PayloadFromIoTClient(iotClient);
   const alertSetup = payload?.['AlertSetupData'];
   const alertArray = alertSetup[0].split('');
   const alerts = {};
   const keys = [
      'WARN_PLENTEMP1', 'WARN_PLENTEMP2', 'WARN_NO_PLENTEMP', 'WARN_LOWPLENTEMP',
      'WARN_HIGHPLENTEMP', 'WARN_PLENSENSOR', 'WARN_RETURNAIRTEMP', 'WARN_AUXLOWPLENTEMP',
      'WARN_OUTTEMPSENSOR', 'WARN_NO_STARTTEMP', 'WARN_STARTTEMP', 'WARN_PLENHUMID',
      'WARN_INVALIDPLENHUMID', 'WARN_RETURNAIRHUMID', 'WARN_OUTHUMIDSENSOR', 'WARN_OUTHUMIDVAR',
      'WARN_INVALIDCO2', 'WARN_AIRFLOW', 'WARN_FAN', 'WARN_REFRIG_AS1',
      'WARN_CLIMACELL', 'WARN_HUMID1_AS1', 'WARN_HUMID2_AS1', 'WARN_HIGHCO2',
      'WARN_AUX1_AS1', 'WARN_AUX2_AS1', 'WARN_AUX_AS1',
      'WARN_HEAT', 'WARN_CAVITYHEAT', 'WARN_CAVHEATCALC', 'WARN_BURNER', 'WARN_POWER',
      'WARN_REMOTESTANDBY', 'WARN_REFRIGSTANDBY', 'WARN_NOBROADCAST', 'WARN_SLAVENOBROADCAST',
      'WARN_TIMERESET', 'WARN_DATETIME', 'WARN_UI', 'WARN_ARMCOMM', 'WARN_MODECHANGE',
      'WARN_LOADLOG_CLEAR', 'WARN_LOADMON_BAY1', 'WARN_LOADMON_BAY2',
      'WARN_NEWBOARD', 'WARN_BOARDREMOVED',
      'WARN_COMMERR', 'WARN_DEFAULTTEMP', 'WARN_BOARDNOTTEMP', 'WARN_BOARDNOTHUMID',
      'WARN_DEFTEMPDIS', 'WARN_DEFHUMDIS', 'WARN_DATALOGWRITE', 'WARN_DATALOGREAD',
      'WARN_DATALOGFULL', 'WARN_USERLOG_CLEAR', 'WARN_ACTLOGWRITE', 'WARN_ACTLOGREAD',
      'WARN_ACTLOG_CLEAR', 'WARN_SDCARD', 'WARN_SDCARD_DIFF', 'WARN_SDCARD_INCOMPAT',
      'WARN_SDCARD_INIT', 'WARN_SDCARD_UNINIT', 'WARN_SDCARD_LOCKED', 'WARN_SDCARD_NONE',
      'WARN_SAVESETTINGS', 'WARN_FACTORYDEFAULT', 'WARN_RTCACCESS', 'WARN_EEPROMACCESS',
      'WARN_MALLOC', 'WARN_INVALIDDATETIME', 'WARN_DSTSTART', 'WARN_DSTSTOP', 'WARN_SETTINGSCONVERT',
      'WARN_SETTINGSCNVRTERR', 'WARN_SETTINGSSIZE', 'WARN_VERSION', 'WARN_FILEACCESS',
      'WARN_FILENAME', 'WARN_FILEWRITE', 'WARN_SOFTWAREUPDATE', 'WARN_CLEARALERTS',
      'WARN_LIGHTS1_AS1', 'WARN_LIGHTS2_AS1', 'WARN_LOADLOG_FULL', 'WARN_ALARMS_FILE',
      'WARN_EQUIPDESC_FILE',
   ];

   alerts['WARN_PLENTEMP1'] = alertArray[0] === '1' ? 'on' : 'off';
   alerts['WARN_PLENTEMP2'] = alertArray[1] === '1' ? 'on' : 'off';
   alerts['WARN_NO_PLENTEMP'] = alertArray[2] === '1' ? 'on' : 'off';
   alerts['WARN_LOWPLENTEMP'] = alertArray[3] === '1' ? 'on' : 'off';
   alerts['WARN_HIGHPLENTEMP'] = alertArray[4] === '1' ? 'on' : 'off';
   alerts['WARN_PLENSENSOR'] = alertArray[5] === '1' ? 'on' : 'off';
   alerts['WARN_RETURNAIRTEMP'] = alertArray[6] === '1' ? 'on' : 'off';
   alerts['WARN_AUXLOWPLENTEMP'] = alertArray[7] === '1' ? 'on' : 'off';
   alerts['WARN_OUTTEMPSENSOR'] = alertArray[8] === '1' ? 'on' : 'off';
   alerts['WARN_NO_STARTTEMP'] = alertArray[9] === '1' ? 'on' : 'off';
   alerts['WARN_STARTTEMP'] = alertArray[10] === '1' ? 'on' : 'off';
   alerts['WARN_PLENHUMID'] = alertArray[11] === '1' ? 'on' : 'off';
   alerts['WARN_INVALIDPLENHUMID'] = alertArray[12] === '1' ? 'on' : 'off';
   alerts['WARN_RETURNAIRHUMID'] = alertArray[13] === '1' ? 'on' : 'off';
   alerts['WARN_OUTHUMIDSENSOR'] = alertArray[14] === '1' ? 'on' : 'off';
   alerts['WARN_OUTHUMIDVAR'] = alertArray[15] === '1' ? 'on' : 'off';
   alerts['WARN_INVALIDCO2'] = alertArray[16] === '1' ? 'on' : 'off';
   alerts['WARN_AIRFLOW'] = alertArray[17] === '1' ? 'on' : 'off';
   alerts['WARN_FAN'] = alertArray[18] === '1' ? 'on' : 'off';
   alerts['WARN_REFRIG_AS1'] = alertArray[19] === '1' ? 'on' : 'off';
   alerts['WARN_CLIMACELL'] = alertArray[20] === '1' ? 'on' : 'off';
   alerts['WARN_HUMID1_AS1'] = alertArray[21] === '1' ? 'on' : 'off';
   alerts['WARN_HUMID2_AS1'] = alertArray[22] === '1' ? 'on' : 'off';
   alerts['WARN_HIGHCO2'] = alertArray[23] === '1' ? 'on' : 'off';
   alerts['WARN_AUX1_AS1'] = alertArray[24] === '1' ? 'on' : 'off';
   alerts['WARN_AUX2_AS1'] = alertArray[25] === '1' ? 'on' : 'off';
   alerts['WARN_AUX_AS1'] = alertArray[26] === '1' ? 'on' : 'off';
   alerts['WARN_HEAT'] = alertArray[27] === '1' ? 'on' : 'off';
   alerts['WARN_CAVITYHEAT'] = alertArray[28] === '1' ? 'on' : 'off';
   alerts['WARN_CAVHEATCALC'] = alertArray[29] === '1' ? 'on' : 'off';
   alerts['WARN_BURNER'] = alertArray[30] === '1' ? 'on' : 'off';
   alerts['WARN_POWER'] = alertArray[31] === '1' ? 'on' : 'off';
   alerts['WARN_REMOTESTANDBY'] = alertArray[32] === '1' ? 'on' : 'off';
   alerts['WARN_REFRIGSTANDBY'] = alertArray[33] === '1' ? 'on' : 'off';
   alerts['WARN_NOBROADCAST'] = alertArray[34] === '1' ? 'on' : 'off';
   alerts['WARN_SLAVENOBROADCAST'] = alertArray[35] === '1' ? 'on' : 'off';
   alerts['WARN_TIMERESET'] = alertArray[36] === '1' ? 'on' : 'off';
   alerts['WARN_DATETIME'] = alertArray[37] === '1' ? 'on' : 'off';
   alerts['WARN_UI'] = alertArray[38] === '1' ? 'on' : 'off';
   alerts['WARN_ARMCOMM'] = alertArray[39] === '1' ? 'on' : 'off';
   alerts['WARN_MODECHANGE'] = alertArray[40] === '1' ? 'on' : 'off';
   alerts['WARN_LOADLOG_CLEAR'] = alertArray[41] === '1' ? 'on' : 'off';
   alerts['WARN_LOADMON_BAY1'] = alertArray[42] === '1' ? 'on' : 'off';
   alerts['WARN_LOADMON_BAY2'] = alertArray[43] === '1' ? 'on' : 'off';
   alerts['WARN_NEWBOARD'] = alertArray[44] === '1' ? 'on' : 'off';
   alerts['WARN_BOARDREMOVED'] = alertArray[45] === '1' ? 'on' : 'off';
   alerts['WARN_COMMERR'] = alertArray[46] === '1' ? 'on' : 'off';
   alerts['WARN_DEFAULTTEMP'] = alertArray[47] === '1' ? 'on' : 'off';
   alerts['WARN_BOARDNOTTEMP'] = alertArray[48] === '1' ? 'on' : 'off';
   alerts['WARN_BOARDNOTHUMID'] = alertArray[49] === '1' ? 'on' : 'off';
   alerts['WARN_DEFTEMPDIS'] = alertArray[50] === '1' ? 'on' : 'off';
   alerts['WARN_DEFHUMDIS'] = alertArray[51] === '1' ? 'on' : 'off';
   alerts['WARN_DATALOGWRITE'] = alertArray[52] === '1' ? 'on' : 'off';
   alerts['WARN_DATALOGREAD'] = alertArray[53] === '1' ? 'on' : 'off';
   alerts['WARN_DATALOGFULL'] = alertArray[54] === '1' ? 'on' : 'off';
   alerts['WARN_USERLOG_CLEAR'] = alertArray[55] === '1' ? 'on' : 'off';
   alerts['WARN_ACTLOGWRITE'] = alertArray[56] === '1' ? 'on' : 'off';
   alerts['WARN_ACTLOGREAD'] = alertArray[57] === '1' ? 'on' : 'off';
   alerts['WARN_ACTLOG_CLEAR'] = alertArray[58] === '1' ? 'on' : 'off';
   alerts['WARN_SDCARD'] = alertArray[59] === '1' ? 'on' : 'off';
   alerts['WARN_SDCARD_DIFF'] = alertArray[60] === '1' ? 'on' : 'off';
   alerts['WARN_SDCARD_INCOMPAT'] = alertArray[61] === '1' ? 'on' : 'off';
   alerts['WARN_SDCARD_INIT'] = alertArray[62] === '1' ? 'on' : 'off';
   alerts['WARN_SDCARD_UNINIT'] = alertArray[63] === '1' ? 'on' : 'off';
   alerts['WARN_SDCARD_LOCKED'] = alertArray[64] === '1' ? 'on' : 'off';
   alerts['WARN_SDCARD_NONE'] = alertArray[65] === '1' ? 'on' : 'off';
   alerts['WARN_SAVESETTINGS'] = alertArray[66] === '1' ? 'on' : 'off';
   alerts['WARN_FACTORYDEFAULT'] = alertArray[67] === '1' ? 'on' : 'off';
   alerts['WARN_RTCACCESS'] = alertArray[68] === '1' ? 'on' : 'off';
   alerts['WARN_EEPROMACCESS'] = alertArray[69] === '1' ? 'on' : 'off';
   alerts['WARN_MALLOC'] = alertArray[70] === '1' ? 'on' : 'off';
   alerts['WARN_INVALIDDATETIME'] = alertArray[71] === '1' ? 'on' : 'off';
   alerts['WARN_DSTSTART'] = alertArray[72] === '1' ? 'on' : 'off';
   alerts['WARN_DSTSTOP'] = alertArray[73] === '1' ? 'on' : 'off';
   alerts['WARN_SETTINGSCONVERT'] = alertArray[74] === '1' ? 'on' : 'off';
   alerts['WARN_SETTINGSCNVRTERR'] = alertArray[75] === '1' ? 'on' : 'off';
   alerts['WARN_SETTINGSSIZE'] = alertArray[76] === '1' ? 'on' : 'off';
   alerts['WARN_VERSION'] = alertArray[77] === '1' ? 'on' : 'off';
   alerts['WARN_FILEACCESS'] = alertArray[78] === '1' ? 'on' : 'off';
   alerts['WARN_FILENAME'] = alertArray[79] === '1' ? 'on' : 'off';
   alerts['WARN_FILEWRITE'] = alertArray[80] === '1' ? 'on' : 'off';
   alerts['WARN_SOFTWAREUPDATE'] = alertArray[81] === '1' ? 'on' : 'off';
   alerts['WARN_CLEARALERTS'] = alertArray[82] === '1' ? 'on' : 'off';
   alerts['WARN_LIGHTS1_AS1'] = alertArray[83] === '1' ? 'on' : 'off';
   alerts['WARN_LIGHTS2_AS1'] = alertArray[84] === '1' ? 'on' : 'off';
   alerts['WARN_LOADLOG_FULL'] = alertArray[85] === '1' ? 'on' : 'off';
   alerts['WARN_ALARMS_FILE'] = alertArray[86] === '1' ? 'on' : 'off';
   alerts['WARN_EQUIPDESC_FILE'] = alertArray[87] === '1' ? 'on' : 'off';
   return [alerts, keys];
}

export const extractAgristar2AlertSetupDataFromIoTClient = (iotClient) => {
   const payload = extractAgristar2PayloadFromIoTClient(iotClient);
   const alertSetup = payload?.['AlertSetupData'];
   const alertArray = alertSetup[0].split('');
   const alerts = {};
   const keys = [
      'WARN_PLENTEMP1', 'WARN_PLENTEMP2', 'WARN_NO_PLENTEMP', 'WARN_LOWPLENTEMP',
      'WARN_HIGHPLENTEMP', 'WARN_PLENSENSOR', 'WARN_RETURNAIRTEMP', 'WARN_AUXLOWPLENTEMP',
      'WARN_OUTTEMPSENSOR', 'WARN_NO_STARTTEMP', 'WARN_STARTTEMP', 'WARN_PLENHUMID',
      'WARN_INVALIDPLENHUMID', 'WARN_RETURNAIRHUMID', 'WARN_OUTHUMIDSENSOR', 'WARN_OUTHUMIDVAR',
      'WARN_INVALIDCO2', 'WARN_AIRFLOW', 'WARN_FAN', 'WARN_REFRIG_PWM', 'WARN_REFRIG_STAGE',
      'WARN_REFRIG_DEFROST', 'WARN_CLIMACELL', 'WARN_HUMIDIFIER', 'WARN_HIGHCO2', 'WARN_AUX',
      'WARN_HEAT', 'WARN_CAVITYHEAT', 'WARN_CAVHEATCALC', 'WARN_BURNER', 'WARN_POWER',
      'WARN_REMOTESTANDBY', 'WARN_REFRIGSTANDBY', 'WARN_NOBROADCAST', 'WARN_SLAVENOBROADCAST',
      'WARN_TIMERESET', 'WARN_DATETIME', 'WARN_UI', 'WARN_ARMCOMM', 'WARN_MODECHANGE',
      'WARN_LOADLOG_CLEAR', 'WARN_LOADMON_BAY1', 'WARN_LOADMON_BAY2', 'WARN_SYSCONFIG_EQ',
      'WARN_NO_OUTPUT', 'WARN_EXPANSIONBOARD', 'WARN_NEWBOARD', 'WARN_BOARDREMOVED',
      'WARN_COMMERR', 'WARN_DEFAULTTEMP', 'WARN_BOARDNOTTEMP', 'WARN_BOARDNOTHUMID',
      'WARN_DEFTEMPDIS', 'WARN_DEFHUMDIS', 'WARN_DATALOGWRITE', 'WARN_DATALOGREAD',
      'WARN_DATALOGFULL', 'WARN_USERLOG_CLEAR', 'WARN_ACTLOGWRITE', 'WARN_ACTLOGREAD',
      'WARN_ACTLOG_CLEAR', 'WARN_SDCARD', 'WARN_SDCARD_DIFF', 'WARN_SDCARD_INCOMPAT',
      'WARN_SDCARD_INIT', 'WARN_SDCARD_UNINIT', 'WARN_SDCARD_LOCKED', 'WARN_SDCARD_NONE',
      'WARN_SAVESETTINGS', 'WARN_FACTORYDEFAULT', 'WARN_RTCACCESS', 'WARN_EEPROMACCESS',
      'WARN_MALLOC', 'WARN_INVALIDDATETIME', 'WARN_DSTSTART', 'WARN_DSTSTOP', 'WARN_SETTINGSCONVERT',
      'WARN_SETTINGSCNVRTERR', 'WARN_SETTINGSSIZE', 'WARN_VERSION', 'WARN_FILEACCESS',
      'WARN_FILENAME', 'WARN_FILEWRITE', 'WARN_SOFTWAREUPDATE', 'WARN_CLEARALERTS',
      'WARN_LIGHTS', 'WARN_LOADLOG_FULL', 'WARN_LIGHTS_OFF', 'WARN_ALARMS_FILE', 'WARN_EQUIPDESC_FILE',
   ];

   alerts['WARN_PLENTEMP1'] = alertArray[0] === '1' ? 'on' : 'off';
   alerts['WARN_PLENTEMP2'] = alertArray[1] === '1' ? 'on' : 'off';
   alerts['WARN_NO_PLENTEMP'] = alertArray[2] === '1' ? 'on' : 'off';
   alerts['WARN_LOWPLENTEMP'] = alertArray[3] === '1' ? 'on' : 'off';
   alerts['WARN_HIGHPLENTEMP'] = alertArray[4] === '1' ? 'on' : 'off';
   alerts['WARN_PLENSENSOR'] = alertArray[5] === '1' ? 'on' : 'off';
   alerts['WARN_RETURNAIRTEMP'] = alertArray[6] === '1' ? 'on' : 'off';
   alerts['WARN_AUXLOWPLENTEMP'] = alertArray[7] === '1' ? 'on' : 'off';
   alerts['WARN_OUTTEMPSENSOR'] = alertArray[8] === '1' ? 'on' : 'off';
   alerts['WARN_NO_STARTTEMP'] = alertArray[9] === '1' ? 'on' : 'off';
   alerts['WARN_STARTTEMP'] = alertArray[10] === '1' ? 'on' : 'off';
   alerts['WARN_PLENHUMID'] = alertArray[11] === '1' ? 'on' : 'off';
   alerts['WARN_INVALIDPLENHUMID'] = alertArray[12] === '1' ? 'on' : 'off';
   alerts['WARN_RETURNAIRHUMID'] = alertArray[13] === '1' ? 'on' : 'off';
   alerts['WARN_OUTHUMIDSENSOR'] = alertArray[14] === '1' ? 'on' : 'off';
   alerts['WARN_OUTHUMIDVAR'] = alertArray[15] === '1' ? 'on' : 'off';
   alerts['WARN_INVALIDCO2'] = alertArray[16] === '1' ? 'on' : 'off';
   alerts['WARN_AIRFLOW'] = alertArray[17] === '1' ? 'on' : 'off';
   alerts['WARN_FAN'] = alertArray[18] === '1' ? 'on' : 'off';
   alerts['WARN_REFRIG_PWM'] = alertArray[19] === '1' ? 'on' : 'off';
   alerts['WARN_REFRIG_STAGE'] = alertArray[20] === '1' ? 'on' : 'off';
   alerts['WARN_REFRIG_DEFROST'] = alertArray[21] === '1' ? 'on' : 'off';
   alerts['WARN_CLIMACELL'] = alertArray[22] === '1' ? 'on' : 'off';
   alerts['WARN_HUMIDIFIER'] = alertArray[23] === '1' ? 'on' : 'off';
   alerts['WARN_HIGHCO2'] = alertArray[24] === '1' ? 'on' : 'off';
   alerts['WARN_AUX'] = alertArray[25] === '1' ? 'on' : 'off';
   alerts['WARN_HEAT'] = alertArray[26] === '1' ? 'on' : 'off';
   alerts['WARN_CAVITYHEAT'] = alertArray[27] === '1' ? 'on' : 'off';
   alerts['WARN_CAVHEATCALC'] = alertArray[28] === '1' ? 'on' : 'off';
   alerts['WARN_BURNER'] = alertArray[29] === '1' ? 'on' : 'off';
   alerts['WARN_POWER'] = alertArray[30] === '1' ? 'on' : 'off';
   alerts['WARN_REMOTESTANDBY'] = alertArray[31] === '1' ? 'on' : 'off';
   alerts['WARN_REFRIGSTANDBY'] = alertArray[32] === '1' ? 'on' : 'off';
   alerts['WARN_NOBROADCAST'] = alertArray[33] === '1' ? 'on' : 'off';
   alerts['WARN_SLAVENOBROADCAST'] = alertArray[34] === '1' ? 'on' : 'off';
   alerts['WARN_TIMERESET'] = alertArray[35] === '1' ? 'on' : 'off';
   alerts['WARN_DATETIME'] = alertArray[36] === '1' ? 'on' : 'off';
   alerts['WARN_UI'] = alertArray[37] === '1' ? 'on' : 'off';
   alerts['WARN_ARMCOMM'] = alertArray[38] === '1' ? 'on' : 'off';
   alerts['WARN_MODECHANGE'] = alertArray[39] === '1' ? 'on' : 'off';
   alerts['WARN_LOADLOG_CLEAR'] = alertArray[40] === '1' ? 'on' : 'off';
   alerts['WARN_LOADMON_BAY1'] = alertArray[41] === '1' ? 'on' : 'off';
   alerts['WARN_LOADMON_BAY2'] = alertArray[42] === '1' ? 'on' : 'off';
   alerts['WARN_SYSCONFIG_EQ'] = alertArray[43] === '1' ? 'on' : 'off';
   alerts['WARN_NO_OUTPUT'] = alertArray[44] === '1' ? 'on' : 'off';
   alerts['WARN_EXPANSIONBOARD'] = alertArray[45] === '1' ? 'on' : 'off';
   alerts['WARN_NEWBOARD'] = alertArray[46] === '1' ? 'on' : 'off';
   alerts['WARN_BOARDREMOVED'] = alertArray[47] === '1' ? 'on' : 'off';
   alerts['WARN_COMMERR'] = alertArray[48] === '1' ? 'on' : 'off';
   alerts['WARN_DEFAULTTEMP'] = alertArray[49] === '1' ? 'on' : 'off';
   alerts['WARN_BOARDNOTTEMP'] = alertArray[50] === '1' ? 'on' : 'off';
   alerts['WARN_BOARDNOTHUMID'] = alertArray[51] === '1' ? 'on' : 'off';
   alerts['WARN_DEFTEMPDIS'] = alertArray[52] === '1' ? 'on' : 'off';
   alerts['WARN_DEFHUMDIS'] = alertArray[53] === '1' ? 'on' : 'off';
   alerts['WARN_DATALOGWRITE'] = alertArray[54] === '1' ? 'on' : 'off';
   alerts['WARN_DATALOGREAD'] = alertArray[55] === '1' ? 'on' : 'off';
   alerts['WARN_DATALOGFULL'] = alertArray[56] === '1' ? 'on' : 'off';
   alerts['WARN_USERLOG_CLEAR'] = alertArray[57] === '1' ? 'on' : 'off';
   alerts['WARN_ACTLOGWRITE'] = alertArray[58] === '1' ? 'on' : 'off';
   alerts['WARN_ACTLOGREAD'] = alertArray[59] === '1' ? 'on' : 'off';
   alerts['WARN_ACTLOG_CLEAR'] = alertArray[60] === '1' ? 'on' : 'off';
   alerts['WARN_SDCARD'] = alertArray[61] === '1' ? 'on' : 'off';
   alerts['WARN_SDCARD_DIFF'] = alertArray[62] === '1' ? 'on' : 'off';
   alerts['WARN_SDCARD_INCOMPAT'] = alertArray[63] === '1' ? 'on' : 'off';
   alerts['WARN_SDCARD_INIT'] = alertArray[64] === '1' ? 'on' : 'off';
   alerts['WARN_SDCARD_UNINIT'] = alertArray[65] === '1' ? 'on' : 'off';
   alerts['WARN_SDCARD_LOCKED'] = alertArray[66] === '1' ? 'on' : 'off';
   alerts['WARN_SDCARD_NONE'] = alertArray[67] === '1' ? 'on' : 'off';
   alerts['WARN_SAVESETTINGS'] = alertArray[68] === '1' ? 'on' : 'off';
   alerts['WARN_FACTORYDEFAULT'] = alertArray[69] === '1' ? 'on' : 'off';
   alerts['WARN_RTCACCESS'] = alertArray[70] === '1' ? 'on' : 'off';
   alerts['WARN_EEPROMACCESS'] = alertArray[71] === '1' ? 'on' : 'off';
   alerts['WARN_MALLOC'] = alertArray[72] === '1' ? 'on' : 'off';
   alerts['WARN_INVALIDDATETIME'] = alertArray[73] === '1' ? 'on' : 'off';
   alerts['WARN_DSTSTART'] = alertArray[74] === '1' ? 'on' : 'off';
   alerts['WARN_DSTSTOP'] = alertArray[75] === '1' ? 'on' : 'off';
   alerts['WARN_SETTINGSCONVERT'] = alertArray[76] === '1' ? 'on' : 'off';
   alerts['WARN_SETTINGSCNVRTERR'] = alertArray[77] === '1' ? 'on' : 'off';
   alerts['WARN_SETTINGSSIZE'] = alertArray[78] === '1' ? 'on' : 'off';
   alerts['WARN_VERSION'] = alertArray[79] === '1' ? 'on' : 'off';
   alerts['WARN_FILEACCESS'] = alertArray[80] === '1' ? 'on' : 'off';
   alerts['WARN_FILENAME'] = alertArray[81] === '1' ? 'on' : 'off';
   alerts['WARN_FILEWRITE'] = alertArray[82] === '1' ? 'on' : 'off';
   alerts['WARN_SOFTWAREUPDATE'] = alertArray[83] === '1' ? 'on' : 'off';
   alerts['WARN_CLEARALERTS'] = alertArray[84] === '1' ? 'on' : 'off';
   alerts['WARN_LIGHTS'] = alertArray[85] === '1' ? 'on' : 'off';
   alerts['WARN_LOADLOG_FULL'] = alertArray[86] === '1' ? 'on' : 'off';
   alerts['WARN_LIGHTS_OFF'] = alertArray[87] === '1' ? 'on' : 'off';
   alerts['WARN_ALARMS_FILE'] = alertArray[88] === '1' ? 'on' : 'off';
   alerts['WARN_EQUIPDESC_FILE'] = alertArray[89] === '1' ? 'on' : 'off';
   return [alerts, keys];
}

// ------------------------AGRISTAR2 EXTRACT items from PAYLOAD-----------------------------------
export const extractBoardTypeFromAgristarPayload = payload => {
   const typeMap = {
      'Agri-Star':'AS1',
      'AS2':'AS2'
   }
   return (payload && payload['BoardType']) ? typeMap[payload['BoardType']] : 'AS2';
}

export const extractSystemModeFromAgristar2Payload = (payload) => payload?.P2BasicSetupData?.[4];
export const extractNameFromAgristar2Payload = (payload) => payload?.P2BasicSetupData?.[0];
export const extractModeFromAgristar2Payload = (payload) => payload && payload.CurrentMode ? agristar2NumberToModeMap[payload['CurrentMode'][0]] : undefined;
export const extractTempTypeFromAgristar2Payload = (payload) => payload && payload['P2BasicSetupData'] ? tempTypeMap[payload['P2BasicSetupData'][1]] : undefined;
export const extractPlenumTempFromAgristar2Payload = (payload) => payload && payload['MainData'] && payload['MainData'][0] !== 'dis' ? payload['MainData'][0] : '--';
export const extractOnionModeFromFrontMatter = (payload) => {
   const systemMode = payload?.system_mode;
   const clientVersion = payload?.iotClientVersion;
   return systemMode === '1' && isVersionAtLeast(clientVersion, 1, 0, 4);
}
export const extractOnionModeFromAgristar2Payload = (payload) => {
   const systemMode = extractSystemModeFromAgristar2Payload(payload);
   const clientVersion = extractIoTClientVersion(payload);
   return systemMode === '1' && isVersionAtLeast(clientVersion, 1, 0, 4);
}
export const extractBeeModeFromAgristar2Payload = (payload) => {
   const systemMode = extractSystemModeFromAgristar2Payload(payload);
   const clientVersion = extractIoTClientVersion(payload);
   return systemMode === '2' && isVersionAtLeast(clientVersion, 1, 0, 3);
}
export const extractPecanModeFromAgristar2Payload = (payload) => {
   const systemMode = extractSystemModeFromAgristar2Payload(payload);
   const clientVersion = extractIoTClientVersion(payload);
   return systemMode === '3' && isVersionAtLeast(clientVersion, 1, 0, 4);
}
/**
 * If in Bee Mode and refrigeration returns the setpoint 2
 * @param {payload} payload 
 * @returns Temperature based on mode
 */
export const extractPlenumTempSetFromAgristar2Payload = (payload) => {
   const currentMode = extractModeFromAgristar2Payload(payload);
   const beeMode = extractSystemModeFromAgristar2Payload(payload) === '2';
   const useTemp2 = beeMode && (currentMode === 'REFRIGERATION' || currentMode === 'REFRIG (RAMPING)' || currentMode === 'REFRIG (DEHUMID)' || currentMode === 'DEFROSTING');

   if (useTemp2) {
      return (payload && payload['PgmData'] && payload['PgmData'][5] !== 'dis') ? payload['PgmData'][5] : '--';
   } else {
      return (payload && payload['PgmData'] && payload['PgmData'][0] !== 'dis') ? payload['PgmData'][0] : '--';
   }
}
export const extractPlenumHumidFromAgristar2Payload = (payload) => payload && payload['MainData'] && payload['MainData'][1] !== 'dis' ? payload['MainData'][1] : '--';
export const extractPlenumHumidSetFromAgristar2Payload = (payload) => payload && payload['PgmData'] && payload['PgmData'][1] !== 'dis' ? payload['PgmData'][1] : '--';
export const extractHumidRefFromPayload = (payload, onion) => {
   if (onion && payload && payload['MainData'][1] === 'dis' && payload['MainData'][7] !== 'dis') {
      return '1';
   } else if (onion && payload && payload['MainData'][7] === 'dis') {
      return '0';
   } else {
      return payload?.['PgmData']?.[2];
   }
};

export const extractOutsideTempFromAgristar2Payload = (payload) => {
   const arr = payload?.['MainData'];
   const result = arr?.[2] !== '0' ? arr?.[2] : arr?.[3]; //return remoteTemp if outside temp is 0
   return !result || result === 'dis' ? '--' : result;
}
export const extractOutsideHumidFromAgristar2Payload = (payload) => {
   const arr = payload?.['MainData'];
   const result = arr?.[4] !== '0' ? arr?.[4] : arr?.[5]; //return remoteHumid if outside Humid is 0
   return !result || result === 'dis' ? '--' : result;
}
export const extractReturnHumidFromAgristar2Payload = (payload) => payload && payload['MainData'] && payload['MainData'][7] !== 'dis' ? payload['MainData'][7] : '--';
export const extractReturnTempFromAgristar2Payload = (payload) => payload && payload['MainData'] && payload['MainData'][8] !== 'dis' ? payload['MainData'][8] : '--';
export const extractCoolingAvailableFromAgristar2Payload = (payload) => payload && payload['MainData'] && payload['MainData'][6] !== 'dis' ? payload['MainData'][6] : '--';
export const extractCo2FromAgristar2Payload = (payload) => payload && payload['MainData'] && payload['MainData'][9] !== 'dis' ? payload['MainData'][9] : '--';
export const extractAlarmDataFromAgristar2Payload = (payload) => {
   let alarmData = payload?.['AlarmData']
   const beeMode = extractBeeModeFromAgristar2Payload(payload);

   if (alarmData?.length > 0 && alarmData[0] !== '' && beeMode) {
      alarmData = alarmData.map((alarm) => {
         const warning = alarm.split('=')[0];
         if (warning === 'WARN_LOWPLENTEMP') {
            return `WARN_LOWPLENTEMP_BEE=${as2AlarmTranslations['WARN_LOWPLENTEMP_BEE']}`;
         } else if (warning === 'WARN_HIGHPLENTEMP') {
            return `WARN_HIGHPLENTEMP_BEE=${as2AlarmTranslations['WARN_HIGHPLENTEMP_BEE']}`;
         } else {
            return alarm;
         }
      })
   }
   return alarmData?.length > 0 && alarmData[0] !== '' ? alarmData : undefined
}
export const extractFanSpeedFromAgristar2Payload = (payload) => payload?.['MainData']?.[10];
export const extractDoorValueFromAgristar2FrontMatter = (frontMatter) => {
   const mode = frontMatter?.main[16];
   const output = frontMatter?.main[15];
   switch (mode) {
      case '1':
         return '0'
      default:
         // when system is off mode returns '0' but output is not a Number, it is instead 'Off'
         return output === 'Off' ? '0' : output
   }
}
export const extractRefrigValueFromAgristar2FrontMatter = (frontMatter, is200plus) => {
   if (is200plus) {
      return frontMatter?.main[34];
   } else {
      const mode = frontMatter?.main[16];
      const output = frontMatter?.main[15];
      if (mode === '1') {
         return output;
      }
      return 0;
   }
}

// Climacell equipment status is determined by the climacell output 0 is 'off', 1 is 'on'
const extractClimacellStatusFromAgristar2Payload = (payload) => payload?.['EquipStatusData']?.[5]
const extractPlenumHeaterStatusFromAgristar2Payload = (payload) => payload?.['EquipStatusData']?.[28]
const extractHumidifierHead1Output = (payload) => payload?.['EquipStatusData']?.[10]
const extractHumidifierHead2Output = (payload) => payload?.['EquipStatusData']?.[13]
const extractHumidifierHead3Output = (payload) => payload?.['EquipStatusData']?.[77]
const extractHumidifierPump1Output = (payload) => payload?.['EquipStatusData']?.[11]
const extractHumidifierPump2Output = (payload) => payload?.['EquipStatusData']?.[14]
const extractHumidifierPump3Output = (payload) => payload?.['EquipStatusData']?.[78]
const extractCavityHeaterOutput = (payload) => payload?.['EquipStatusData']?.[49]
const extractSystemRemoteOff = (payload) => (payload?.['EquipStatusData']?.[37] === '2' ? true : false)
export const extractPileTempsLabelsArray = (payload) => payload?.['PileTempsLabels']?.map((item, index, array)=>{
   // PileTempsLabels = "NEW1,8,NEW2,9,Bd 3 - S 3,10,Bd 3 - S 4,11"
   // [ { 8:'NEW1' }, { 9:'NEW2' } ]
   if(index % 2 === 0 && array.length > 1){
      return {
         [array[index+1]]:item
      }
   }
   return [];
}).filter(i => i !== undefined);
export const extractPileHumidLabelsArray = (payload) => payload?.['PileHumidsLabels']?.map((item, index, array) => {
   if(index % 2 === 0 && array.length > 1){
      return {
         [array[index+1]]:item
      }
   }
   return [];
}).filter(i => i !== undefined);

export const extractIoTClientVersion = (payload) => payload?.['IoTClientVersion'];
export const extractControllerVersion = (payload) => payload?.['SysVersions']?.[0]

// ----------LIST ITEM AS2 DATA WITH USER PREFERENCES-----------
// ...unit conversions included
export const extractAgristar2ListItemDataFromIoTClient = (iotClient, usersSystemOfMeasurement) => {
   const frontMatter = iotClient.front_matter;
   const is200plus = isVersionAtLeast(frontMatter?.misc[2], 2, 0, 0);
   const deviceName = frontMatter?.misc[1] || iotClient?.name || 'Agri-Star';
   const deviceType = frontMatter?.misc[0];
   const deviceTempType = tempTypeMap[frontMatter?.main[1]];
   const plenumTemp = frontMatter?.main[2];
   const plenumTempSet = frontMatter?.main[3];
   const plenumTempSet2 = frontMatter?.main[4];
   const bayLight1 = frontMatter?.main[19];
   const bayLight2 = frontMatter?.main[21];
   const baylight1Name = frontMatter?.main[20];
   const baylight2Name = frontMatter?.main[22];

   const baylight1Short = baylight1Name?.[0] === baylight2Name?.[0] ? '1' : baylight1Name?.[0]
   const baylight2Short = baylight1Name?.[0] === baylight2Name?.[0] ? '2' : baylight2Name?.[0]
   const iotClientVersion = frontMatter?.misc[2];
   const controllerVersion = frontMatter?.misc[3];
   const cureStartTemp =  frontMatter?.main[11];
   const cureStartHumid = frontMatter?.main[12];
   const doorValue = extractDoorValueFromAgristar2FrontMatter(frontMatter)
   const refrigValue = extractRefrigValueFromAgristar2FrontMatter(frontMatter, is200plus)

   const returnTemp = frontMatter?.main[9];
   const returnTemp2 = frontMatter?.main.length > 37 ? frontMatter?.main[37] : undefined;
   const returnHumid = frontMatter?.main[10];
   const returnHumid2 = frontMatter?.main.length > 35 ? frontMatter?.main[35] : undefined;
   const outsideTemp = frontMatter?.main[7];
   const outsideHumid = frontMatter?.main[8];
   const co2 = frontMatter?.main[17];
   const co2_2 = frontMatter?.main.length > 36 ? frontMatter?.main[36] : undefined;
   const mli1 = frontMatter?.main.length > 38 ? frontMatter?.main[38] : undefined;
   const mli2 = frontMatter?.main.length > 39 ? frontMatter?.main[39] : undefined;

   // const co2_set = payload?.['Co2PurgeData'][0] === '2' ? payload?.['Co2PurgeData'][4] : null

   const fanSpeed = frontMatter?.main[14];
   const current_mode = agristar2NumberToModeMap[frontMatter?.main[0]];
   return {
      device_type: deviceType,
      time_stamp: iotClient.time_stamp,
      unsecured_ip: iotClient.unsecured_ip,
      temp_type: deviceTempType,
      device_name: deviceName,
      return_temp: getCustomizedTemperature(returnTemp, usersSystemOfMeasurement, deviceTempType),
      return_temp2: getCustomizedTemperature(returnTemp2, usersSystemOfMeasurement, deviceTempType),
      return_humid: returnHumid,
      return_humid2: returnHumid2,
      outside_temp: getCustomizedTemperature(outsideTemp, usersSystemOfMeasurement, deviceTempType),
      outside_humid: outsideHumid,
      co2,
      co2_2,
      // co2_set,
      fan_speed: fanSpeed, // off, manual, OR a number
      current_mode,
      door_value: doorValue, // can sometimes send '--' otherwise is 0-100
      refrig_value: modeToCategoryMap[current_mode] === 'refrigeration' ? refrigValue : '0',
      burner_output: frontMatter?.main[18],
      plenum_temp: getCustomizedTemperature(plenumTemp, usersSystemOfMeasurement, deviceTempType),
      plenum_temp_set: getCustomizedTemperature(plenumTempSet, usersSystemOfMeasurement, deviceTempType),
      plenum_temp_set2: getCustomizedTemperature(plenumTempSet2, usersSystemOfMeasurement, deviceTempType),
      plenum_humid: frontMatter?.main[5],
      plenum_humid_set: frontMatter?.main[6],
      alarm_data: frontMatter?.AlarmData,
      cure_start_temp: getCustomizedTemperature(cureStartTemp, usersSystemOfMeasurement, deviceTempType),
      cure_start_humid: cureStartHumid,
      bay_light1: bayLight1,
      bay_light2: bayLight2,
      bay_light1_name: baylight1Name,
      bay_light2_name: baylight2Name,
      bay_light1_short: baylight1Short,
      bay_light2_short: baylight2Short,
      iot_client_version: iotClientVersion,
      controller_version: controllerVersion,
      error: frontMatter?.error ?? false,
      pileAvg: frontMatter?.main[13],
      system_mode: frontMatter?.main[23],
      cure_output: frontMatter?.main[24],
      cure_remote: frontMatter?.main[25],
      cure_temp_low: getCustomizedTemperature(frontMatter?.main[26], usersSystemOfMeasurement, deviceTempType),
      cure_temp_high: getCustomizedTemperature(frontMatter?.main[27], usersSystemOfMeasurement, deviceTempType),
      cure_humid_high: frontMatter?.main[28],
      humid_set_reference: frontMatter?.main[29],
      cooling_available: frontMatter?.main[15],
      air_cure_humid_reference: frontMatter?.main[30],
      calc_humid: frontMatter?.main[31],
      co2_purge_mode: frontMatter?.main[32],
      co2_set_point: frontMatter?.main[33],
      mli1,
      mli2,
   }
}
// ----------FULL AS2 DIAGRAM DATA WITH USER PREFERENCES----------------
export const extractAgristar2DiagramDataFromIoTClient = (iotClient, usersSystemOfMeasurement) => {
   const payload = extractAgristar2PayloadFromIoTClient(iotClient)
   const lineItemVariables = extractAgristar2ListItemDataFromIoTClient(iotClient, usersSystemOfMeasurement)
   const coolingAvailable = extractCoolingAvailableFromAgristar2Payload(payload)
   // plenum heating
   // humidifiers
   return {
      ...lineItemVariables,
      calc_humid: payload?.MainData?.[14],
      cooling_available: getCustomizedTemperature(coolingAvailable, usersSystemOfMeasurement, lineItemVariables.temp_type),
      climacell_output: extractClimacellStatusFromAgristar2Payload(payload),
      plenum_heater_output: extractPlenumHeaterStatusFromAgristar2Payload(payload),
      cavity_heater_output: extractCavityHeaterOutput(payload),
      humidifier_head1_output: extractHumidifierHead1Output(payload),
      humidifier_head2_output: extractHumidifierHead2Output(payload),
      humidifier_head3_output: extractHumidifierHead3Output(payload),
      humidifier_pump1_output: extractHumidifierPump1Output(payload),
      humidifier_pump2_output: extractHumidifierPump2Output(payload),
      humidifier_pump3_output: extractHumidifierPump3Output(payload),
      system_remote_off: extractSystemRemoteOff(payload),
   }
}

// -------------------------AS2 P1PLENUM-----------------------------------------
export const extractAgristar2PlenumSettingsFromIoTClient = (iotClient) => {
   const payload = extractAgristar2PayloadFromIoTClient(iotClient)
   const temp_type = extractTempTypeFromAgristar2Payload(payload)
   const temp_set = (payload && payload['PgmData'] && payload['PgmData'][0] !== 'dis') ? payload['PgmData'][0] : '--';
   const humid_set = extractPlenumHumidSetFromAgristar2Payload(payload)
   const onionMode = extractOnionModeFromAgristar2Payload(payload);
   const beeMode = extractBeeModeFromAgristar2Payload(payload);
   const humid_ref = extractHumidRefFromPayload(payload, onionMode);
   const temp_set2 =  (beeMode && payload && payload['PgmData'] && payload['PgmData'][5] !== 'dis') ? payload['PgmData'][5] : '--';
   const use_refrig = (beeMode && payload && payload['PgmData']) ? payload['PgmData'][6] : '0';
   const aux_switch = (beeMode && payload && payload['PgmData']) ? payload['PgmData'][7] : '0';
   const burner_set = payload?.['PgmData']?.[3];
   const burner_thresh = payload?.['PgmData']?.[4];
   const burner_max = payload?.['PgmData']?.[3];
   const burner_restart = payload?.['PgmData']?.[4];

   return{
      temp_type, temp_set, humid_set, temp_set2, use_refrig, aux_switch, humid_ref,
      burner_set, burner_thresh, burner_max, burner_restart,
   }
}

// -------------------------AS2 P1PLENUM TEMP DEV-----------------------------------------
export const extractAgristar2TempDevFromIoTClient = (iotClient) => {
   const payload = extractAgristar2PayloadFromIoTClient(iotClient);
   const tempdev = payload?.['PlenTempDevData'];
   return {
      below_temp: tempdev?.[0],
      below_time: tempdev?.[1],
      above_temp: tempdev?.[2],
      above_time: tempdev?.[3],
      cure_temp_low: tempdev?.[4],
      cure_temp_high: tempdev?.[5],
   };
}

// -------------------------AS2 P1RAMP RATE-----------------------------------------------
export const extractAgristar2RampRateFromIoTClient = (iotClient) => {
   const payload = extractAgristar2PayloadFromIoTClient(iotClient);
   const ramprate = payload?.['RampRateData'];
   const pile = extractPileTempsLabelsArray(payload);
   const pile_labels = pile.length > 1
      ? extractPileTempsLabelsArray(payload).reduce((map, obj) => {
         const key = Object.keys(obj)[0]
         map[key] = obj[key]
         return map
      },{}) // example: {'8':'North Temp','9':'Bay 2 Temp'}
      : [];

   return {
      update_temp: ramprate?.[0],
      update_period: ramprate?.[1],
      temp_diff: ramprate?.[2],
      temp_ref: ramprate?.[3],
      target_temp: ramprate?.[4],
      pile_labels,
   }
}

// -------------------------AS2 P1CO2PURGE SETTINGS-----------------------------------------
export const extractAgristar2Co2PurgeSettingsFromIoTClient = (iotClient) => {
   const payload = extractAgristar2PayloadFromIoTClient(iotClient)
   const tempType = extractTempTypeFromAgristar2Payload(payload)
   const co2Purge = payload?.['Co2PurgeData']
   const iotVersion = extractIoTClientVersion(payload);
   const iot200plus = isVersionAtLeast(iotVersion, 2, 0, 0);
   return{
      temp_type: tempType,
      selPurgeMode: co2Purge?.[0],
      minTemp: co2Purge?.[1],
      maxTemp: co2Purge?.[2],
      time: co2Purge?.[3],
      PurgeHours: co2Purge?.[0] === '1' ? co2Purge?.[4] : '24',
      co2SetPoint: co2Purge?.[0] === '2' ? (iot200plus ? co2Purge?.[7] : co2Purge?.[4]) : '1200',
      fanOutput: co2Purge?.[5],
      doorOutput: co2Purge?.[6],
      co2Target: co2Purge?.[0] === '3' ? co2Purge?.[7] : '1200', // for bee mode
   }
}

// -----------------------AS2 P1OUTSIDEAIR--------------------------------------------------
export const extractAgristar2OutsideAirSettingsFromIoTClient = (iotClient) => {
   const payload = extractAgristar2PayloadFromIoTClient(iotClient)
   const tempType = extractTempTypeFromAgristar2Payload(payload)
   const outsideAirVar = payload?.['OutsideAirData'];
   const airCure = payload?.['AirCureData'];
   const tempDev = payload?.['PlenTempDevData'];
   const pile = extractPileTempsLabelsArray(payload);
   const tempsRef = pile?.length > 1
      ? pile.reduce((map, obj) => {
         const key = Object.keys(obj)[0]
         map[key] = obj[key]
         return map
      },{}) // example: {'8':'North Temp','9':'Bay 2 Temp'}
      : [];

   return{
      tempType: tempType,
      tempsRef: tempsRef,// example: {'8':'North Temp','9':'Bay 2 Temp'}
      ctrlMode: outsideAirVar?.[4], // 0 - 'outside air', 1 - 'plenum'
      OutsideAirSet: (outsideAirVar?.[0]),
      selAboveBelow: outsideAirVar?.[1], // 0 - 'above', 1 - 'below'
      selTempRef: outsideAirVar?.[2], // 255 - 'plenum set', 254 - 'return air', [other#] dynamic selection from tempsRef
      CureStartTemp: airCure?.[0],
      selHumidRef: airCure?.[1],
      CureStartHumid: airCure?.[2],
      CureHumidHighLimit: airCure?.[3],
      CureTempLowLimit: tempDev?.[4],
   }
}

// ----------------------P1EMAIL--------------------------------------------------------
export const extractAgristarEmailFromIoTClient = (iotClient) => {
   // EmailAlertData: (9) ["1", "smtp.gmail.com", "0", "587", "AgriStar.Alerts@gmail.com", "4gri*st4r4l3rts", "not selected", "MyAccount@gmail.com", "AgriStar.Alerts@Gellert.com"]
   const payload = extractAgristar2PayloadFromIoTClient(iotClient);
   const email = payload?.['EmailAlertData'];
   if (email.length > 8) {
      return {
         selEmailAlert: email?.[0],
         emailTo: email?.[7].toString().replace(/#/g, ','),
         emailFrom: email?.[8].toString().replace(/#/g, ','),
         emailServer: email?.[1],
         selEmailAuthType: email?.[2],
         emailPort: email?.[3],
         emailAccount: email?.[4],
         emailPassword: email?.[5],
         selEmailDisplay: email?.[6],
      }
   } else {
      return {
         selEmailAlert: '', emailTo: '', emailFrom: '', emailServer: '',
         selEmailAuthType: '', emailPort: '', emailAccount: '', emailPassword: '',
         selEmailDisplay: '',
      };
   }
}
// ----------------------DisplayList--------------------------------------------------------
export const extractAgristarDisplayListFromIoTClient = (iotClient) => {
   const payload = extractAgristar2PayloadFromIoTClient(iotClient);
   const displays = {};
   const list = payload?.['DisplayList'];
   for (let i = 0; list && i < list.length; i += 5) {
      displays[list[i]] = `${list[i]}: (...${list[i+2]})`;
   }
   return displays;
}

// ----------------------P1FANSPEED--------------------------------------------------------
export const extractAgristarFanSpeedSettingsFromIoTClient = (iotClient) => {
   const payload = extractAgristar2PayloadFromIoTClient(iotClient)
   const freqCtrlVar = payload?.['FreqCtrlData']
   const pile = extractPileTempsLabelsArray(payload);
   const tempsRef = pile?.length > 1
      ? pile.reduce((map, obj) => {
         const key = Object.keys(obj)[0]
         map[key] = obj[key]
         return map
      },{}) // example: {'8':'North Temp','9':'Bay 2 Temp'}
      : [];

   return{
      setFanSpeed: freqCtrlVar?.[8],
      maxFanSpeed: freqCtrlVar?.[0],
      minFanSpeed: freqCtrlVar?.[1],
      refrFanSpeed: freqCtrlVar?.[2],
      recircFanSpeed: freqCtrlVar?.[3],
      updFanSpeed: freqCtrlVar?.[4],
      selCoolingType: freqCtrlVar?.[9],
      tempDiff: freqCtrlVar?.[5],
      selDiff1: freqCtrlVar?.[6],
      selDiff2: freqCtrlVar?.[7],
      tempsRef
   }
}

// ----------------------P1FANBOOST--------------------------------------------------------
export const extractAgristarFanBoostSettingsFromIoTClient = (iotClient) => {
   const payload = extractAgristar2PayloadFromIoTClient(iotClient)
   const fanboost = payload?.['FanBoostData']
   return{
      selBoostMode: fanboost?.[0],
      speed: fanboost?.[1],
      hours: fanboost?.[2],
      time: fanboost?.[3],
      temp: fanboost?.[4],
   }
}

// ----------------------AS2 Misc----------------------------------------------------------------
export const extractAgristar2MiscSettingsFromIoTClient = (iotClient) => {
   const payload = extractAgristar2PayloadFromIoTClient(iotClient);
   const misc = payload?.['MiscData'];
   const pile = extractPileTempsLabelsArray(payload);
   const tempsRef = pile.length > 1
      ? pile.reduce((map, obj) => {
         const key = Object.keys(obj)[0]
         map[key] = obj[key]
         return map
      },{}) // example: {'8':'North Temp','9':'Bay 2 Temp'}
      : [];

   return {
      selRefrMode: misc?.[0],
      defrostInterval: misc?.[1],
      defrostTime: misc?.[2],
      tempThresh: misc?.[3],
      selCavityCtrl: misc?.[4],
      cavityDiff: misc?.[6],
      cavityDutyCycle: misc?.[7],
      selCavityCtrlSensor: misc?.[7],
      kbPref: misc?.[8],
      selCtrlMode: misc?.[10],
      pileTempsLabels: tempsRef,
      refrigThresh: misc?.[11],
      enthTarget: misc?.length > 12 ? misc[12] : '',
   };
}

// ----------------------AS2 Humidifier Head CONTROL DATA----------------------------------------
export const extractAgristarHumidCtrlFromIoTClient = (iotClient) => {
   const equip = extractAgristarEquipmentControlFromIoTClient(iotClient);
   const humid = [ equip.humidifier1Head, equip.humidifier2Head, equip.humidifier3Head ];
   const payload = extractAgristar2PayloadFromIoTClient(iotClient)
   const humidCtrl = payload?.['HumidCtrlData'];

   let retval = [];
   for (let i = 0; i < 3; i += 1) {
      retval.push({
         humidHead: humid[i],
         mode: humidCtrl?.[i * 7],
         coolOn: humidCtrl?.[i * 7 + 1],
         coolOff: humidCtrl?.[i * 7 + 2],
         recircOn: humidCtrl?.[i * 7 + 3],
         recircOff: humidCtrl?.[i * 7 + 4],
         refrigOn: humidCtrl?.[i * 7 + 5],
         refrigOff: humidCtrl?.[i * 7 + 6],
      });
   }
   return retval;
}

//---------------------------P2 Basic Setup--------------------------
export const extractAgristar2BasicSetupFromIoTClient = (iotClient) => {
   const payload = extractAgristar2PayloadFromIoTClient(iotClient)
   const basicSetup = payload?.['P2BasicSetupData'];

   return {
      StorageName: basicSetup?.[0],
      HomePage: basicSetup?.[3],
      dlr0: basicSetup?.[8],
      loginSecure: basicSetup?.[9],
      TempType: basicSetup?.[1],
      SystemMode: basicSetup?.[4],
      MultiView: basicSetup?.[7],
      Animations: basicSetup?.[10],
      CO2_50K: basicSetup?.[11],
   };
}
//---------------------------P2 Burner-----------------------------------
export const extractAgristar2BurnerFromIoTClient = (iotClient) => {
   const payload = extractAgristar2PayloadFromIoTClient(iotClient);
   const burner = payload?.['P2BurnerData'];
   const climacell = payload?.['P2ClimacellData'];

   return {
      burnerOn: burner?.[0],
      burnerLow: burner?.[1],
      PBurnerValue: burner?.[2],
      IBurnerValue: burner?.[3],
      DBurnerValue: burner?.[4],
      UBurnerValue: burner?.[5],
      selBurnerMode: burner?.[6],
      burnerManual: burner?.[7],
      Altitude: climacell?.[1],
      AltType: climacell?.[2],
   }
}

//---------------------------P2 Fresh Air Setup--------------------------
export const extractAgristar2FreshAirFromIoTClient = (iotClient) => {
   const payload = extractAgristar2PayloadFromIoTClient(iotClient)
   const freshAir = payload?.['P2FreshAirData'];

   return {
      PAirValue: freshAir?.[0],
      IAirValue: freshAir?.[1],
      DAirValue: freshAir?.[2],
      UAirValue: freshAir?.[3],
      ActuatorTimes: freshAir?.[4],
      CoolAirCycle: freshAir?.[5],
      btnPIDDoorLog: freshAir?.[6],
   };
}

//---------------------------P2 ClimaCell Setup--------------------------
export const extractAgristar2ClimaCellFromIoTClient = (iotClient) => {
   const payload = extractAgristar2PayloadFromIoTClient(iotClient)
   const climaCell = payload?.['P2ClimacellData'];

   return {
      ClimacellEff: climaCell?.[0],
      Altitude: climaCell?.[1],
      AltType: climaCell?.[2],
      PClimacellValue: climaCell?.[3],
      IClimacellValue: climaCell?.[4],
      DClimacellValue: climaCell?.[5],
      UClimacellValue: climaCell?.[6],
   };
}

//---------------------------P2 PWM Output--------------------------
export const extractAgristar2PwmOutputFromIoTClient = (iotClient) => {
   const payload = extractAgristar2PayloadFromIoTClient(iotClient)
   const pwmOutput = payload?.['P2PwmChannelData'];

   // remove any trailing commas with slice
   return pwmOutput?.map((item) => item.split(':'));
}
export const extractAgristar2RunTimesFromIoTClient = (iotClient) => {
   const payload = extractAgristar2PayloadFromIoTClient(iotClient);
   const dailyFan = payload?.['DailyFanRun'];
   const total = payload?.['TotalFanRun'];

   return {
      dailyFan: dailyFan?.[0],
      totalFan: total?.[0],
      totalRefrigeration: total?.[1],
      totalCooling: total?.[2],
      totalRecirculation: total?.[3],
      totalCure: total?.[4],
      totalStandby: total?.[5],
   };
}
//---------------------------P1 PWM Output--------------------------
export const extractAgristar1PwmOutputFromIoTClient = (iotClient) => {
   const payload = extractAgristar2PayloadFromIoTClient(iotClient)
   const data = payload?.['P2PwmChannelData'];
   return {
      selDoors: data?.[0],
      selRefrig: data?.[1],
      selFan: data?.[2],
      selBurner: data?.[3],
   };
}

//---------------------------P2 AvailableIoData--------------------------
export const extractAgristar2AvailableIoDataFromIoTClient = (iotClient) => {
   const payload = extractAgristar2PayloadFromIoTClient(iotClient)
   const AvailableIoData = payload?.['AvailableIoData'];
   // use slice to remove any trailing blanks
   return AvailableIoData.slice(0, 3).map((ioInfo) => ioInfo.split(':'));
}

//---------------------------P2 IOConfig Setup--------------------------
export const extractAgristar2IONamesFromIoTClient = (iotClient) => {
   const payload = extractAgristar2PayloadFromIoTClient(iotClient)
   const IoNames = payload?.['IoNames'];
   const sysMode = payload?.['P2BasicSetupData']?.[4];
   const OutputConfig = payload?.['OutputConfigData'];
   const InputConfig = payload?.['InputConfigData'];

   return {
      IoNames,
      sysMode,
      OutputConfig,
      InputConfig,
   };
}

export const extractIOConfigPayload = (AvailableIoData, OutputConfig, InputConfig) => {
   const PayloadToSend = {};

   for (let i = 0; i < 3; i += 1) {
      const ioInfo = AvailableIoData[i];
      if (ioInfo[0].indexOf('none') === -1) {
         for (let j = 1; j <= ioInfo[1]*1; j += 1) {
            const pid = ((i * 12) + (j * 1));

            if (i !== 0 || (j !== 1 && j < 9)) {
               PayloadToSend[`i${pid}`] = InputConfig.indexOf(pid.toString());
            }
            if (i !== 0 || (j > 3 && j < 9)) {
               PayloadToSend[`o${pid}`] = OutputConfig.indexOf(pid.toString());
            }
            if (i === 0 && j === 9) {
               PayloadToSend.pulseDoor = OutputConfig[40];
            }
         }
      }
   }
   return PayloadToSend;
}

//---------------------------P2 IOConfig Setup--------------------------
export const extractAgristar2IOConfigFromIoTClient = (iotClient) => {
   const AvailableIoData = extractAgristar2AvailableIoDataFromIoTClient(iotClient);
   const { IoNames, OutputConfig, InputConfig } = extractAgristar2IONamesFromIoTClient(iotClient);
   const payload = extractAgristar2PayloadFromIoTClient(iotClient);
   const systemMode = extractSystemModeFromAgristar2Payload(payload);
   const EquipListInput = { '-1': 'None' };
   const EquipListOutput = { '-1': 'None' };
   const Lights = {};
   const potatoMode = systemMode === '0';
   const onionMode = systemMode === '1';
   const pecanMode = systemMode === '3';

   for (let i = 0; i < IoNames.length - 1; i+= 1) {
      const listInfo = IoNames[i]?.split(':');

      if ( (((potatoMode || pecanMode) && listInfo[1] === '1') // Potato
         || (onionMode && listInfo[1] === '2') // Onion
         || (listInfo[1] === '4' || listInfo[1] === '5' || listInfo[1] === '6' || listInfo[1] === '7')) // All
         && listInfo[2] !== '3') { // not a switch

         if (listInfo[4] !== '23' && listInfo[4] !== '24') {
            if (listInfo[2] === '0' || listInfo[2] === '2') { // output or both
               EquipListOutput[listInfo[4]] = listInfo[0];
            }
            if (listInfo[2] === '1' || listInfo[2] === '2') { // input or both
               EquipListInput[listInfo[4]] = listInfo[0];
            }
         } else {
            Lights[listInfo[4]] = listInfo[0];
         }
      }
   }

   const PayloadToSend = extractIOConfigPayload(AvailableIoData, OutputConfig, InputConfig);

   return {
      EquipListInput,
      EquipListOutput,
      Lights,
      OutputConfig,
      InputConfig,
      AvailableIoData,
      IoNames,
      PayloadToSend,
   };
}

//---------------------------P2 Aux Programming--------------------------
export const extractAgristar2AuxProgFromIoTClient = (iotClient) => {
   const payload = extractAgristar2PayloadFromIoTClient(iotClient)
   const program = payload?.['AuxProgramData'];
   const rules = [];

   program.slice(1).forEach((item, index) => {
      if (index < 6) {
         const rule = item.split(':');
         // use 256 to not display any rule
         if (rule.length > 1) {
            const last = index > 1 ? program[index].split(':')[5] === '255' : false;
            rules.push({
               type: rule[0],
               io: rule[1],
               st: rule[2],
               op: rule[3],
               sen: rule[4],
               diff: rule[4],
               andOr: last ? '256' : rule[5],
               ref: rule[6],
               first: rule[0] !== '4',
               sensorOption: rule[6] === '255' ? '0' : '1',
            });
         }
      }
   });

   const auxProg = program?.[0].split(':');

   return {
      AuxProgram: auxProg[0],
      dutyCycle: auxProg[1],
      period: auxProg[2],
      units: auxProg[3],
      rules,
   };
}

//---------------------------P2 AnalogBoard Setup--------------------------
export const extractAgristar2AnalogBoardFromIoTClient = (iotClient, usersSystemOfMeasurement) => {
   const payload = extractAgristar2PayloadFromIoTClient(iotClient)
   const tempType = extractTempTypeFromAgristar2Payload(payload)
   const analog = payload?.['P2AnalogBoardData'];
   const sensors = [];

   for (let i = 0; i < 4; i += 1) {
      sensors.push({
         SenTyp: analog?.[i * 5 + 5],
         SenLbl: analog?.[i * 5 + 6],
         SenOff: analog?.[i * 5 + 7],
         SenDis: analog?.[i * 5 + 8],
         SenVal: getCustomizedTemperature(analog?.[i * 5 + 9], usersSystemOfMeasurement, tempType),
      });
   }

   return {
      BAdd: analog?.[0],
      BType: analog?.[1],
      BdLbl: analog?.[2],
      BVer: analog?.[3],
      BDis: analog?.[4],
      sensors,
   };
}

// ----------------------AS2 Refrigeration Setup--------------------------------
export const extractAgristar2RefrigerationSetupFromIoTClient = (iotClient) => {
   const payload = extractAgristar2PayloadFromIoTClient(iotClient);
   const refrig = payload?.['P2RefrigerationData'];
   const Stages = [
      {On: refrig?.[0], Off: refrig?.[1]},
      {On: refrig?.[2], Off: refrig?.[3]},
      {On: refrig?.[4], Off: refrig?.[5]},
      {On: refrig?.[6], Off: refrig?.[7]},
      {On: refrig?.[8], Off: refrig?.[9]},
      {On: refrig?.[10], Off: refrig?.[11]},
      {On: refrig?.[19], Off: refrig?.[20]},
      {On: refrig?.[21], Off: refrig?.[22]},
   ];
   return {
      Stages,
      PRefrValue: refrig?.[12],
      IRefrValue: refrig?.[13],
      DRefrValue: refrig?.[14],
      URefrValue: refrig?.[15],
      RefrigerationPurge: refrig?.[16],
      PurgeThreshold: refrig?.[17],
      btnPIDRefrigLog: refrig?.[18],
   };
}

// ----------------------AS2 Log Settings---------------------------------------
export const extractAgristar2LogSettingsFromIoTClient = (iotClient) => {
   const payload = extractAgristar2PayloadFromIoTClient(iotClient);
   const log = payload?.['UserLogSettings'];
   if (log) {
      return {
         recInterval: log[0],
         sdWrap: log[1],
         pidWrap: log[10],
      };
   }
   return {
      recInterval: '60',
      sdWrap: '1',
      pidWrap: '1',
   };
}

// ----------------------AS2 Failures2----------------------------------------------------------------
export const extractAgristar2Failures2FromIoTClient = (iotClient) => {
   const payload = extractAgristar2PayloadFromIoTClient(iotClient);
   const fail = payload?.['P2Failures2Data'];

   return {
      OutAirMode: fail?.[0],
      OutAirTimer: fail?.[1],
      OutHumidMode: fail?.[2],
      OutHumidTimer: fail?.[3],
      HighCo2Mode: fail?.[4],
      HighCo2Timer: fail?.[5],
      Co2Setpt: fail?.[6],
      LowHumidMode: fail?.[7],
      LowHumidTimer: fail?.[8],
      LowHumidSet: fail?.[9],
      PlenSenMode: fail?.[10],
      PlenSenTimer: fail?.[11],
      PlenSenDiff: fail?.[12],
   };
}
// ----------------------AS1 Failures1----------------------------------------------------------------
export const extractAgristar1Failures1FromIoTClient = (iotClient) => {
   const payload = extractAgristar2PayloadFromIoTClient(iotClient);
   const fail = payload?.['P2FailuresData'];

   return {
      FanMode: fail?.[0],
      FanTimer: fail?.[1],
      ClimacellMode: fail?.[2],
      ClimacellTimer: fail?.[3],
      RefridgeMode: fail?.[4],
      RefridgeTimer: fail?.[5],
      RefridgeRun: fail?.[6],
      Humidifier1Mode: fail?.[7],
      Humidifier1Timer: fail?.[8],
      Humidifier2Mode: fail?.[9],
      Humidifier2Timer: fail?.[10],
      Aux1Mode: fail?.[7],
      Aux1Timer: fail?.[8],
      Aux2Mode: fail?.[9],
      Aux2Timer: fail?.[10],
      AuxMode: fail?.[11],
      AuxTimer: fail?.[12],
      HeatMode: fail?.[13],
      HeatTimer: fail?.[14],
      CavityHeatMode: fail?.[15],
      CavityHeatTimer: fail?.[16],
      BurnerMode: fail?.[17],
      BurnerTimer: fail?.[18],
      LightsMode: fail?.[19],
      LightsTimer: fail?.[20],
      LightsUnits: fail?.[21],
   };
}

// ----------------------AS2 Failures1----------------------------------------------------------------
export const extractAgristar2Failures1FromIoTClient = (iotClient) => {
   const payload = extractAgristar2PayloadFromIoTClient(iotClient);
   const fail = payload?.['P2FailuresData'];

   return {
      FanMode: fail?.[0],
      FanTimer: fail?.[1],
      ClimacellMode: fail?.[2],
      ClimacellTimer: fail?.[3],
      RefridgeMode: fail?.[4],
      RefridgeTimer: fail?.[5],
      RefridgeRun: fail?.[6],
      RefrStagesMode: fail?.[7],
      RefrStagesTimer: fail?.[8],
      HumidifiersMode: fail?.[9],
      HumidifiersTimer: fail?.[10],
      AuxMode: fail?.[11],
      AuxTimer: fail?.[12],
      HeatMode: fail?.[13],
      HeatTimer: fail?.[14],
      CavityHeatMode: fail?.[15],
      CavityHeatTimer: fail?.[16],
      BurnerMode: fail?.[17],
      BurnerTimer: fail?.[18],
      LightsMode: fail?.[19],
      LightsTimer: fail?.[20],
      LightsUnits: fail?.[21],
   };
}

// ----------------------AS2 EQUIPMENT CONTROL DATA----------------------------------------------
export const extractAgristarEquipmentControlFromIoTClient = (iotClient) => {
   // -----
   const payload = extractAgristar2PayloadFromIoTClient(iotClient)
   const boardType = extractBoardTypeFromAgristarPayload(payload)
   const isAS1 = boardType === 'AS1'
   const equipStatus = payload?.['EquipStatusData']
   const pwm = payload?.['P2PwmChannelData']
   const pgmData = payload?.['PgmData'];

   // -----COLORS FOR STATUSES-----
   const red = 'pink'
   const green = 'lightgreen'
   const black = 'lightgrey'
   const blue = 'lightblue'

   // -----------Interpretation logic functions-----------------
   const ioNames = payload?.['IoNames']
   const outConfig = payload?.['OutputConfigData']
   const auxSwitches = payload?.['AuxSwitchesData'];
   // const inConfig = payload?.['inputConfigData']
   const exists = (ioNameObj) =>  isAS1 ? true : (ioNameObj && outConfig?.[ioNameObj[4]] !== '-1')
   const objName = (ioNameObj) => isAS1 ? undefined : ioNameObj?.[0]
   const isRenamable = (ioNameObj) => isAS1 ? false : ioNameObj?.[3] === '1' 
   // if renamable and not named its default return the custom name else return UNDEFINED.
   // ..... the component should provide default translation
   const renamedAs = (ioNameObj, defaultName) => isAS1 ? undefined : (
      isRenamable(ioNameObj) && objName(ioNameObj) !== defaultName ? objName(ioNameObj) : undefined
   )

   function getEquipStatusTextAndColor(remoteStatus, outputStatus, inputStatus) {
      const result = {
         outputStatus: 'off',
         outputColor: red,
         statusText: 'off',
         statusColor: red,
      };

      if (remoteStatus === '1') {
         result.statusText = 'remote off';
         result.statusColor = red;
      } else {
         if (inputStatus === '0') {
            result.statusText = 'on';
            result.statusColor = green;
         } else {
            result.statusText = 'off';
            result.statusColor = red;
         }
      }

      if (outputStatus === '1') {
         result.outputColor = green;
            result.outputStatus = 'on'
      }

      return result;
   }

   function RefrigerationMainStatusTextAndColor(refStagesOutputArr, refStagesDiagArr, refInput, coolMode, coolOutput, refRemOff){
      const result = {
         outputStatus:'off',
         outputColor:red,
         statusText:'off',
         statusColor:red,
      }
      // check all the outputs and diags to see how they impact refrigeration
      let stRefrig = 0
      for (var i = 0; i < refStagesOutputArr?.length; i++){
         if (refStagesOutputArr[i] === '1') {
               stRefrig = 1;
               if (refStagesDiagArr[i] === '2') {
                  stRefrig = 2;
               }
         }
      }

      if (stRefrig === 2) {
         result.statusText = 'diag on';
         result.statusColor = blue;
      } else if (stRefrig === 1 || refInput === '0') {
         result.statusText = 'on';
         result.statusColor = green;
      }

      if (stRefrig === 1 || stRefrig === 2 || (coolMode === '1' && coolOutput * 1 > 0)){
         result.outputColor = result.statusColor;
      }
      else {
         result.outputColor = red;
      }

      if (refInput === '1') {
         result.statusText = 'off';
         result.statusColor = red;
      }

      if (refRemOff === '1') {
         result.statusText = 'remote off';
         result.statusColor = red;
      }

      return result
   }

   function getRefrigStageStatusTextAndColor(diagStatus,remoteStatus,outputStatus,inputStatus){
      const result = {
         outputColor:red,
         statusText:'off',
         statusColor:red,
      }
      
      if (outputStatus === '1')
      {
         result.statusText = 'on';
         result.outputColor = green;
         result.statusColor = green;
   
         if (diagStatus === '2')
         {
         result.statusText = 'diag on';
         result.outputColor = blue;
         result.statusColor = blue;
         }
      }
      // -----status column only from here on----
      if (inputStatus === '1')
      {
         result.statusText = 'off';
         result.statusColor = red;
      }
   
      if (diagStatus === '1')
      {
         result.statusText = 'diag off';
         result.statusColor = red;
      }
      else if (remoteStatus === '1')
      {
         result.statusText = 'remote off';
         result.statusColor = red;
      }

      return result
   }

   const getPanelSwitchStatus = (position) => {
      switch (position) {
         case '1': return 'auto';
         case '2': return 'manual';
         case '3': return 'on';
         default: return 'off';
      }
   }
   const getPanelSwitchColor = (position) => position === '0' ? red : (position === '1' ? green : black)
   const getInputStatus = (position) => position === '0' ? 'on' : 'off'
   const getRemoteOffStatus = (position) => position === '1' // 0 - false/on/green || 1 - true/remote off/red

    // -------Equipment Objects that work with AS1 and AS2---------
   const fanDetails = () => {
      const fanObjAS2 = ioNames?.[0]?.split(':') // Fan/Green Light:4:2:0:0
      const fanPanSwit = equipStatus?.[0]
      const fanInput = equipStatus?.[1]
      const fanOutput = equipStatus?.[2]
      const fanRemOff = equipStatus?.[37]
      return {
         exists: exists(fanObjAS2), // is ALWAYS true for Fan
         renamed_as: renamedAs(fanObjAS2, 'Fan/Green Light'), 
         panel_switch: getPanelSwitchStatus(fanPanSwit), 
         panel_switch_color: getPanelSwitchColor(fanPanSwit),
         input:getInputStatus(fanInput), 
         remoteOff: getRemoteOffStatus(fanRemOff), 
         remSwitchName: 'fanBtn',
         ...getEquipStatusTextAndColor(fanRemOff,fanOutput,fanInput,)
      }
   }

   const climacellDetails = () => {
      const climacellObjAS2 = ioNames?.[3]?.split(':') //"ClimaCell:1:2:0:3"
      const panSwit = equipStatus?.[3]
      const input = equipStatus?.[4]
      const output = equipStatus?.[5]
      const remOff = equipStatus?.[38]
      return{
         exists: exists(climacellObjAS2), 
         renamed_as: renamedAs(climacellObjAS2, 'ClimaCell'),
         panel_switch: getPanelSwitchStatus(panSwit), 
         panel_switch_color: getPanelSwitchColor(panSwit),
         input: getInputStatus(input), 
         remoteOff: getRemoteOffStatus(remOff),
         remSwitchName: 'climacellBtn',
         ...getEquipStatusTextAndColor(remOff,output,input,)
      }
   }

   const heatDetails = () => {
      const heatObjAS2 = ioNames?.[4]?.split(':') //"Heat:1:2:0:4"
      const panSwit = isAS1 ? equipStatus?.[26] : undefined //AS2 has no Heat panel switch
      const input = equipStatus?.[27]
      const output = equipStatus?.[28]
      const remOff = equipStatus?.[48]
      return{
         exists: exists(heatObjAS2), 
         renamed_as: renamedAs(heatObjAS2, 'Heat'), 
         panel_switch: isAS1 ? getPanelSwitchStatus(panSwit) : undefined, 
         panel_switch_color: isAS1 ? getPanelSwitchColor(panSwit) : undefined,
         input: getInputStatus(input), 
         remoteOff: getRemoteOffStatus(remOff),
         remSwitchName: 'heatBtn',
         ...getEquipStatusTextAndColor(remOff,output,input,)
      }
   }

   const cavityHeatDetails = () => {
      const cavityHeatObj = ioNames?.[5]?.split(':') //"Cavity Heat:4:2:0:5"
      const panSwitch = isAS1 ? equipStatus?.[36] : undefined
      const input = equipStatus?.[31]
      const output = equipStatus?.[32]
      const remOff = equipStatus?.[49]
      return{
         exists: exists(cavityHeatObj),
         renamed_as:  renamedAs(cavityHeatObj, 'Cavity Heat'), 
         panel_switch: isAS1 ? getPanelSwitchStatus(panSwitch) : undefined, 
         panel_switch_color: isAS1 ? getPanelSwitchColor(panSwitch) : undefined,
         input: getInputStatus(input), 
         remoteOff: getRemoteOffStatus(remOff),
         remSwitchName: 'cavHeatBtn',
         ...getEquipStatusTextAndColor(remOff,output,input,)
      }
   }

   const getBurnerDetails = () => {
      const burnerObj = ioNames?.[6]?.split(':');
      const panSwitch = equipStatus?.[3];
      const input = equipStatus?.[6];
      const output = equipStatus?.[7];
      const remOff = equipStatus?.[51];
      return {
         exists: exists(burnerObj),
         panel_switch: getPanelSwitchStatus(panSwitch),
         panel_switch_color: getPanelSwitchColor(panSwitch),
         input: getInputStatus(input),
         remoteOff: getRemoteOffStatus(remOff),
         remSwitchName: 'burnerBtn',
         ...getEquipStatusTextAndColor(remOff, output, input),
      };
   };

   const getCureDetails = () => {
      const cureObj = ['Cure', 2, 2, 0, 0];
      const panSwitch = equipStatus?.[8];
      const input = (equipStatus?.[8] === '1' && equipStatus?.[2] === '1' && equipStatus?.[52] === '0') ? '0' : '1';
      const output = equipStatus?.[8];
      const remOff = equipStatus?.[52];
      return {
         exists: exists(cureObj),
         panel_switch: getPanelSwitchStatus(panSwitch),
         panel_switch_color: getPanelSwitchColor(panSwitch),
         input: getInputStatus(input),
         remoteOff: getRemoteOffStatus(remOff),
         remSwitchName: 'cureBtn',
         ...getEquipStatusTextAndColor(remOff, output, input),
      };
   };

   const getHumidifierHeadDetails = (number) => {
      const humidifierPanSwit = equipStatus?.[8] 
      const humidHeadObjAS2 = ioNames?.[7+(number*2)-2]?.split(':') //"Humidifier 1 - Head:1:2:0:7", 
      const humidHeadInput = equipStatus?.[(number === 1 ? 9 : (number === 2 ? 12 : 76))]
      const humidHeadOutput = equipStatus?.[(number === 1 ? 10 : (number === 2 ? 13 : 77))]
      const humidRemOff = equipStatus?.[(number === 1 ? 39 : (number === 2 ? 40 : 93))]
      return {
         // equipStatus 0-switch 1-input 2-output
         humidifierPart:'head',
         humidifierNumber:number,
         exists: exists(humidHeadObjAS2), 
         renamed_as: isAS1 ? undefined : renamedAs(humidHeadObjAS2, `Humidifier ${number} - Head`), 
         panel_switch: getPanelSwitchStatus(humidifierPanSwit), 
         panel_switch_color: getPanelSwitchColor(humidifierPanSwit),
         input: getInputStatus(humidHeadInput),
         remSwitchName: `humid${number}PumpBtn`,
         remoteOff: getRemoteOffStatus(humidRemOff), 
               // 0 - false/on/green || 1 - true/remote off/red
         ...getEquipStatusTextAndColor(humidRemOff,humidHeadOutput,humidHeadInput)
      }
   }

   const getHumidifierPumpDetails = (number) => {
      const humidifierPanSwit = equipStatus?.[8] 
      const humidPumpObj = ioNames?.[8+(number*2)-2]?.split(':') // "Humidifier 1 - Pump:1:0:0:8"
      const humidHeadInput = equipStatus?.[(number === 1 ? 9 : (number === 2 ? 12 : 76))] 
      // .....this is actually HEAD INPUT pump does not have input
      const humidPumpOutput = equipStatus?.[(number === 1 ? 11 : (number === 2 ? 14 : 78))]
      const humidRemOff = equipStatus?.[(number === 1 ? 39 : (number === 2 ? 40 : 93))]
      return {
         // equipStatus 0-switch 1-input 2-output
         humidifierPart:'pump',
         humidifierNumber: number,
         exists: exists(humidPumpObj), 
         renamed_as: renamedAs(humidPumpObj, `Humidifier ${number} - Pump`), 
         panel_switch: getPanelSwitchStatus(humidifierPanSwit), 
         panel_switch_color: getPanelSwitchColor(humidifierPanSwit),
         input: getInputStatus(humidHeadInput), 
         remoteOff: getRemoteOffStatus(humidRemOff), 
         remSwitchName: undefined, // the switch is applied to the head instead
         ...getEquipStatusTextAndColor(humidRemOff,humidPumpOutput,humidHeadInput),
      }
   }

   const RefrigerationMainDetails = () => {
      const refrigObj = ioNames?.[2]?.split(':') // Refrigeration:4:1:0:2
      const refrigExists = () => isAS1 ? true : (pwm?.[1]?.split(':')[2]*1 !== -1 || outConfig?.[13] !== '-1');
      const coolingMode = payload?.['MainData']?.[12]
      const coolingOutput = payload?.['MainData']?.[11]
      const refrigPanSwit = equipStatus?.[15]
      const refrigInput = equipStatus?.[16] // 0-on 1-off
      const refrigStagesOutput = isAS1 ? equipStatus?.slice(17,23) : equipStatus?.slice(17,27) // all refrigStages and defrost outputs
      const refrigStagesDiag = equipStatus ? (isAS1 ?  [...equipStatus.slice(41,47)] : [...equipStatus.slice(41,47), ...equipStatus.slice(89,93)]) : undefined; // all refrigStages and defrost 'diag'
      const refrigRemOff = equipStatus?.[50] // 0-not off 1-remoteoff true
      return {
         exists: refrigExists(), //exists(refrigObj), // is ALWAYS true for Refrigeration //why it not defined either i/o Config?
         renamed_as: renamedAs(refrigObj, 'Refrigeration'), 
         panel_switch: getPanelSwitchStatus(refrigPanSwit), 
         panel_switch_color: getPanelSwitchColor(refrigPanSwit),
         input: getInputStatus(refrigInput), 
         remoteOff: getRemoteOffStatus(refrigRemOff),
         remSwitchName: 'refrigBtn',
         ...RefrigerationMainStatusTextAndColor(refrigStagesOutput, refrigStagesDiag, refrigInput, coolingMode, coolingOutput, refrigRemOff)
      }
   }

   const getRefrigStageDetails = (stageNumber) => {
      const refrigStageNumObj = ioNames?.[12+stageNumber]?.split(':')
      const refrigStageNumInput = equipStatus?.[78+stageNumber]
      const refrigStageNumOutput = equipStatus?.[16+stageNumber] // stage 7 & 8 are abnormal in refrigStageDiag they are located at 89 and 90 in the equipStatusData variable
      const refrigStageNumDiag = equipStatus?.[stageNumber <= 7 ? (40+stageNumber) : (88+(stageNumber-6))]
      const refrigRemOff = equipStatus?.[50]
      // defrost works differently for AS1, stages 5&6 are set to defrost
      const as1DefrostName = (stageNumber) => {
         const p2RefData = payload?.['P2RefrigerationData']
         const defrostText = (val) => 
               <FormattedMessage 
                  id='p2Refrigeration[4].defrost'
                  defaultMessage='Defrost {defrost}'
                  description='Defrost'
                  values={{ defrost: val }}
               />
         switch (stageNumber) {
               case 5:
                  return p2RefData?.[8] === '255' ? defrostText(1) : undefined
               case 6:
                  return p2RefData?.[10] === '255' ? defrostText(2) : undefined
               default:
                  return undefined;
         }            
      }

      return {
         stageNumber: stageNumber,
         exists: exists(refrigStageNumObj),
         renamed_as: isAS1 ? as1DefrostName(stageNumber) : renamedAs(refrigStageNumObj, `Refrigeration Stage ${stageNumber}`), // if is renameable and has new a custom name, put it hear
         panel_switch: undefined, // off auto manual undefined
         output: '', // ??
         input: '', // ??
         remoteOff: getRemoteOffStatus(refrigStageNumDiag), // gets set to 'diag off' as 'remoteOff' feature
         remSwitchName: `refr${stageNumber}Btn`,
         ...getRefrigStageStatusTextAndColor(refrigStageNumDiag,refrigRemOff,refrigStageNumOutput,refrigStageNumInput)
      }
   }

   // AGRISTAR1 always assumes all of the equipment exists
   function AS1EquipmentControl() {
      const as1Aux = (item) => {
         const panSwit = equipStatus?.[23 + (item - 1) * 3]
         const input = equipStatus?.[24 + (item - 1) * 3]
         const output = equipStatus?.[25 + (item - 1) * 3]
         const remOff = equipStatus?.[47 + (item - 1) * 6]
         return{
               exists: true, 
               panel_switch: getPanelSwitchStatus(panSwit), 
               panel_switch_color: getPanelSwitchColor(panSwit),
               input: getInputStatus(input), 
               remoteOff: getRemoteOffStatus(remOff),
               remSwitchName: `aux${item}Btn`,
               ...getEquipStatusTextAndColor(remOff,output,input,)
         }
      }

      return {
         fan: fanDetails(),
         climacell:climacellDetails(),
         heat:heatDetails(),
         cavity_heat:cavityHeatDetails(),
         humidifier1Head:getHumidifierHeadDetails(1),
         humidifier1Pump:getHumidifierPumpDetails(1),
         humidifier2Head:getHumidifierHeadDetails(2),
         humidifier2Pump:getHumidifierPumpDetails(2),
         // humidifier3Head:{exists: false}, // ONLY 2 humidifiers are supported in AS1
         // humidifier3Pump:{exists: false},
         refrigeration: RefrigerationMainDetails(),
         ref_stage1: getRefrigStageDetails(1),
         ref_stage2: getRefrigStageDetails(2),
         ref_stage3:getRefrigStageDetails(3),
         ref_stage4:getRefrigStageDetails(4),
         ref_stage5:getRefrigStageDetails(5),
         ref_stage6:getRefrigStageDetails(6),
         // ref_stage7:getRefrigStageDetails(7),
         // ref_stage8:getRefrigStageDetails(8),
         // defrost1:getDefrostDetails(1),
         // defrost2:getDefrostDetails(2),
         aux1:as1Aux(1),
         aux2:as1Aux(2),
         // aux3:{exists: false},
         // aux4:getAuxillaryDetails(4),
         // aux5:getAuxillaryDetails(5),
         // aux6:getAuxillaryDetails(6),
         // aux7:getAuxillaryDetails(7),
         // aux8:getAuxillaryDetails(8),
         // door: 100%
         // pwms:
         cure:getCureDetails(),
         burner:getBurnerDetails(),
      }
   }

   function AS2EquipmentControl(){
      const getDefrostDetails = (number) => {
         const defrostObj = ioNames?.[(number === 1 ? 21 : 22)]?.split(':') //"Defrost 1:4:2:0:21", "Defrost 2:4:2:0:22"
         const defrostOutput = equipStatus?.[(number === 1 ? 25 : 26)]
         const defrostInput = equipStatus?.[(number === 1 ? 87 : 88)]
         const defrostDiag = equipStatus?.[(number === 1 ? 91 : 92)]
         const refrigRemOff = equipStatus?.[50]
         return {
               number: number,
               exists: exists(defrostObj),
               renamed_as: renamedAs(defrostObj, `Defrost ${number}`), // if is renameable and has new a custom name, put it hear
               panel_switch: undefined, // off auto manual undefined
               output: '', // ??
               input: '', // ??
               remoteOff: defrostDiag === '1', // gets set to 'diag off' as 'remoteOff' feature
               remSwitchName: `defrost${number}Btn`,
               ...getRefrigStageStatusTextAndColor(defrostDiag,refrigRemOff,defrostOutput,defrostInput)
         }
      }
   
      const getAuxillaryDetails = (number) => {
         const auxObj = ioNames?.[25+(number)-1]?.split(':') //"Auxiliary 1:4:2:1:25"
         const auxInput = equipStatus?.[60+(number*2)-2]
         const auxOutput = equipStatus?.[60+(number*2)-1]
         const auxRemOff = equipStatus?.[(number === 1 ? 47 : (number === 2 ? 53 : (94+(number-3))))]
         return {
               number:number,
               exists: exists(auxObj), 
               renamed_as: renamedAs(auxObj, `Humidifier ${number} - Head`), 
               panel_switch: undefined,
               panel_switch_color: undefined,
               input: auxInput === '0' ? 'on' : 'off', 
               remSwitchName: `aux${number}Btn`,
               remoteOff: auxRemOff === '1', 
               ...getEquipStatusTextAndColor(auxRemOff,auxOutput,auxInput)
         }
      }

      const getDehumidifier = () => {
         const dehumidObj = ioNames?.[43]?.split(':');
         const remote = equipStatus?.[100];
         const input = equipStatus?.[101];
         const output = equipStatus?.[102];
         let panSwitch = undefined;
         if (pgmData?.[7] === '56') {
            panSwitch = auxSwitches?.[0].split(':')[0];
         } else if (pgmData?.[7] === '58') {
            panSwitch = auxSwitches?.[1].spit(':')[0];
         }
         return {
            exists: exists(dehumidObj),
            input: input === '0' ? 'on' : 'off',
            remoteOff: remote === '1',
            remSwitchName: 'dehumidBtn',
            panel_switch: panSwitch ? getPanelSwitchStatus(panSwitch) : undefined, 
            panel_switch_color: panSwitch ? getPanelSwitchColor(panSwitch) : undefined,
            ...getEquipStatusTextAndColor(remote, output, input),
         };
      }

      return{
         fan: fanDetails(),
         climacell:climacellDetails(),
         heat:heatDetails(),
         cavity_heat:cavityHeatDetails(),
         humidifier1Head:getHumidifierHeadDetails(1),
         humidifier1Pump:getHumidifierPumpDetails(1),
         humidifier2Head:getHumidifierHeadDetails(2),
         humidifier2Pump:getHumidifierPumpDetails(2),
         humidifier3Head:getHumidifierHeadDetails(3),
         humidifier3Pump:getHumidifierPumpDetails(3),
         refrigeration: RefrigerationMainDetails(),
         ref_stage1: getRefrigStageDetails(1),
         ref_stage2: getRefrigStageDetails(2),
         ref_stage3:getRefrigStageDetails(3),
         ref_stage4:getRefrigStageDetails(4),
         ref_stage5:getRefrigStageDetails(5),
         ref_stage6:getRefrigStageDetails(6),
         ref_stage7:getRefrigStageDetails(7),
         ref_stage8:getRefrigStageDetails(8),
         defrost1:getDefrostDetails(1),
         defrost2:getDefrostDetails(2),
         aux1:getAuxillaryDetails(1),
         aux2:getAuxillaryDetails(2),
         aux3:getAuxillaryDetails(3),
         aux4:getAuxillaryDetails(4),
         aux5:getAuxillaryDetails(5),
         aux6:getAuxillaryDetails(6),
         aux7:getAuxillaryDetails(7),
         aux8:getAuxillaryDetails(8),
         cure: getCureDetails(),
         burner: getBurnerDetails(),
         dehumidifier: getDehumidifier(),
      }
   }

   switch (boardType) {
      case 'AS1':
         return AS1EquipmentControl()
      // AS2
      default:
         return AS2EquipmentControl()
   }
}

// ---------------AGRISTAR2 INTERPRETORS---------------------------------------
export const interpretAgristar2BaylightsIsOn = (io_input) => io_input === '0'  // if '0' its True, it is On, else False

// MEMOIZATION - attempt for iot-client items in the sites list
// this will create a selector for each time it is called, this is good for list item components that need to use it.
// ...call it like: useSelector(makeSelectIoTClientsBySite()(site.id))
// useSelector(state => makeSelectIoTClientsBySite()(state, site.id))[0]?.id
// export const makeSelectIoTClientsBySite = () => {
//     return createSelector(
//         [selectIoTClientsBySite],
//         iotClients => {
//             console.log('...selector fired')
//             return iotClients
//         }
//     );
// }
