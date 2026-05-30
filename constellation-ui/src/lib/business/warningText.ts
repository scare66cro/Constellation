/**
 * warningText.ts — Default English warning text mirror.
 *
 * Single source of truth for human-readable warning strings on the UI
 * side of the wire. Was previously mirrored in the bridge
 * (`server/src/warningTranslator.ts`); that copy was deleted Apr 27 2026
 * once the alerts page started deriving labels client-side via
 * `alertMetadata.ts`. Both tables ultimately derive from
 * `/var/www/languageAlarms.txt` in the legacy AS2 system. Used by
 * `frontMatterComposite::buildAlarmData()` to produce `WARN_KEY=Human text`
 * strings.
 */
export const DEFAULT_WARNING_TEXT: Record<string, string> = {
	WARN_PLENTEMP1: 'Invalid Plenum Temperature 1',
	WARN_PLENTEMP2: 'Invalid Plenum Temperature 2',
	WARN_NO_PLENTEMP: 'No valid Plenum Temperature is available',
	WARN_LOWPLENTEMP: 'Low Plenum Temperature Failure',
	WARN_HIGHPLENTEMP: 'High Plenum Temperature Failure',
	WARN_PLENSENSOR: 'Plenum Temperature Sensor Variance',
	WARN_RETURNAIRTEMP: 'Invalid Return Air Temperature',
	WARN_AUXLOWPLENTEMP: 'Auxiliary Low Plenum Temperature Failure',
	WARN_OUTTEMPSENSOR: 'Invalid Outside Air Temperature',
	WARN_NO_STARTTEMP: "The Cooling Available Temperature can't be calculated",
	WARN_STARTTEMP: 'Unable to calculate Start Temperature',
	WARN_PLENHUMID: 'Low Plenum Humidity',
	WARN_INVALIDPLENHUMID: 'Invalid Plenum Humidity',
	WARN_RETURNAIRHUMID: 'Invalid Return Air Humidity',
	WARN_OUTHUMIDSENSOR: 'Invalid Outside Air Humidity',
	WARN_OUTHUMIDVAR: 'Outside Air Humidity Variance',
	WARN_INVALIDCO2: 'Invalid CO2 value',
	WARN_AIRFLOW: 'Air Flow Restriction Failure',
	WARN_FAN: 'Fan Failure',
	WARN_REFRIG_AS1: 'Refrigeration',
	WARN_REFRIG_PWM: 'Refrigeration',
	WARN_REFRIG_STAGE: 'Refrigeration Stage',
	WARN_REFRIG_DEFROST: 'Refrigeration Defrost',
	WARN_CLIMACELL: 'ClimaCell',
	WARN_HUMID1_AS1: 'Humidifier 1',
	WARN_HUMID2_AS1: 'Humidifier 2',
	WARN_HUMIDIFIER: 'Humidifier',
	WARN_HIGHCO2: 'High CO2 Level',
	WARN_AUX1_AS1: 'Auxiliary 1',
	WARN_AUX2_AS1: 'Auxiliary 2',
	WARN_AUX_AS1: 'Auxiliary',
	WARN_AUX: 'Auxiliary',
	WARN_HEAT: 'Heat',
	WARN_CAVITYHEAT: 'Cavity Heater / Pile Fan',
	WARN_CAVHEATCALC:
		'Unable to calculate Cavity Heat / Pile Fan Differential - invalid reference temperature',
	WARN_BURNER: 'Burner',
	WARN_POWER: 'System power Failure',
	WARN_REMOTESTANDBY: 'System in Remote Standby',
	WARN_REFRIGSTANDBY: 'System in Refrigeration Standby',
	WARN_NOBROADCAST:
		'This controller is configured as a Slave and has not received a Master broadcast in 10 minutes',
	WARN_SLAVENOBROADCAST:
		'Slave using local Outside Temperature and Humidity until communication with Master restored',
	WARN_TIMERESET:
		'Invalid system Date/Time - system clock reset to 12:00pm (check system battery)',
	WARN_DATETIME: 'Invalid system Date/Time (check system battery)',
	WARN_UI: 'User interface did not respond (check Lantronix)',
	WARN_ARMCOMM: 'System controller is not responding',
	WARN_MODECHANGE: 'System Mode changed to',
	WARN_LOADLOG_CLEAR: 'Loading Monitor Data Log Cleared',
	WARN_LOADMON_BAY1: 'Loading Monitor High Temperature',
	WARN_LOADMON_BAY2: 'Loading Monitor Low Temperature',
	WARN_SYSCONFIG_EQ: 'I/O configuration error',
	WARN_NO_OUTPUT: 'Mode configuration error',
	WARN_EXPANSIONBOARD: 'I/O Expansion board removed',
	WARN_NEWBOARD: 'New Analog Board detected',
	WARN_BOARDREMOVED: 'Analog Board removed',
	WARN_COMMERR: 'Analog Board communication error',
	WARN_DEFAULTTEMP: 'Default Temperature Board missing',
	WARN_BOARDNOTTEMP: 'Analog Board 1 is not a temperature board',
	WARN_BOARDNOTHUMID: 'Analog Board 2 is not a humidity board',
	WARN_DEFTEMPDIS: 'Default Temperature Board disabled',
	WARN_DEFHUMDIS: 'Default Humidity Board disabled',
	WARN_DATALOGWRITE: 'Failure writing to History Log (check SD Card)',
	WARN_DATALOGREAD: 'Failure reading from History Log (check SD Card)',
	WARN_DATALOGFULL:
		'History Log (SD Card) is full - set card to overwrite old records or insert new card',
	WARN_USERLOG_CLEAR: 'History Log cleared',
	WARN_ACTLOGWRITE: 'Failure writing to Activity Log (check SD Card)',
	WARN_ACTLOGREAD: 'Failure reading from Activity Log (check SD Card)',
	WARN_ACTLOG_CLEAR: 'Activity Log cleared',
	WARN_SAVESETTINGS: 'Unable to read or save System Settings',
	WARN_FACTORYDEFAULT: 'System Settings restored to Factory Default',
	WARN_RTCACCESS: 'Unable to access system clock',
	WARN_EEPROMACCESS: 'Unable to access system memory (EEPROM)',
	WARN_MALLOC: 'Unable to allocate memory for message (ARM)',
	WARN_INVALIDDATETIME: 'Clock could not be set due to an invalid date or time value',
	WARN_DSTSTART: 'Clock has been adjusted for Daylight Saving Time',
	WARN_DSTSTOP: 'Clock has been adjusted for Standard Time',
	WARN_SETTINGSCONVERT: 'System Settings converted to new format',
	WARN_SETTINGSCNVRTERR: 'Unable to convert System Settings to new format',
	WARN_SETTINGSSIZE: 'System Settings size exceeds allocated EEPROM space',
	WARN_VERSION:
		'The controller software version does not match the web server software version',
	WARN_FILEACCESS: 'File download error - Unable to access storage device',
	WARN_FILENAME: 'File download error - No file name or no IP address supplied',
	WARN_FILEWRITE: 'File download error - Unable to write to storage device (may be full)',
	WARN_SOFTWAREUPDATE: 'Software upgrade in process',
	WARN_CLEARALERTS: 'Alarms cleared',
	WARN_LIGHTS1_AS1: 'Lights 1',
	WARN_LIGHTS2_AS1: 'Lights 2',
	WARN_LIGHTS: 'Lights',
	WARN_LOADLOG_FULL: 'Loading Monitor Log Full - Terminating data acquisition'
};

// ALARM_FAIL suffix array — maps `value0` (or severity) to status text.
// Mirrors `ALARM_FAIL` in `warningTranslator.ts`. Indexed by value.
export const ALARM_FAIL: string[] = ['', 'Alarm', 'Failure', 'Mode', '', 'Active'];
