/**
 * Warning text translation — replicates LtxWarnings.c from GellertServerD.
 *
 * The ARM firmware sends AlarmData in compact wire format:
 *   "INDEX&STATUS,VALUE0,VALUE1,INDEX&STATUS,VALUE0,VALUE1,..."
 *
 * GellertServerD translates these numeric indices into human-readable alarm
 * strings before delivering them to the UI.  The bridge must do the same.
 *
 * The Svelte Alarms.svelte component renders each alarm entry with:
 *   alarm.split('=')[1]
 * And the layout checks for:
 *   alarm.includes('WARN_SLAVENOBROADCAST')
 *
 * So the output format must be:  "WARN_KEY=Human readable alarm text"
 */

// ────────────────────────────────────────────────────────────────────
// WARNING_ITEMS enum (matches Mini_IO/Application/Warnings.h)
// The index in this array IS the numeric warning index from the firmware.
// ────────────────────────────────────────────────────────────────────
export const WARNING_KEYS: string[] = [
  /* 0  */ 'WARN_PLENTEMP1',
  /* 1  */ 'WARN_PLENTEMP2',
  /* 2  */ 'WARN_NO_PLENTEMP',
  /* 3  */ 'WARN_LOWPLENTEMP',
  /* 4  */ 'WARN_HIGHPLENTEMP',
  /* 5  */ 'WARN_PLENSENSOR',
  /* 6  */ 'WARN_RETURNAIRTEMP',
  /* 7  */ 'WARN_AUXLOWPLENTEMP',
  /* 8  */ 'WARN_OUTTEMPSENSOR',
  /* 9  */ 'WARN_NO_STARTTEMP',
  /* 10 */ 'WARN_STARTTEMP',
  /* 11 */ 'WARN_PLENHUMID',
  /* 12 */ 'WARN_INVALIDPLENHUMID',
  /* 13 */ 'WARN_RETURNAIRHUMID',
  /* 14 */ 'WARN_OUTHUMIDSENSOR',
  /* 15 */ 'WARN_OUTHUMIDVAR',
  /* 16 */ 'WARN_INVALIDCO2',
  /* 17 */ 'WARN_AIRFLOW',
  /* 18 */ 'WARN_FAN',
  /* 19 */ 'WARN_REFRIG_AS1',
  /* 20 */ 'WARN_REFRIG_PWM',
  /* 21 */ 'WARN_REFRIG_STAGE',
  /* 22 */ 'WARN_REFRIG_DEFROST',
  /* 23 */ 'WARN_CLIMACELL',
  /* 24 */ 'WARN_HUMID1_AS1',
  /* 25 */ 'WARN_HUMID2_AS1',
  /* 26 */ 'WARN_HUMIDIFIER',
  /* 27 */ 'WARN_HIGHCO2',
  /* 28 */ 'WARN_AUX1_AS1',
  /* 29 */ 'WARN_AUX2_AS1',
  /* 30 */ 'WARN_AUX_AS1',
  /* 31 */ 'WARN_AUX',
  /* 32 */ 'WARN_HEAT',
  /* 33 */ 'WARN_CAVITYHEAT',
  /* 34 */ 'WARN_CAVHEATCALC',
  /* 35 */ 'WARN_BURNER',
  /* 36 */ 'WARN_POWER',
  /* 37 */ 'WARN_REMOTESTANDBY',
  /* 38 */ 'WARN_REFRIGSTANDBY',
  /* 39 */ 'WARN_NOBROADCAST',
  /* 40 */ 'WARN_SLAVENOBROADCAST',
  /* 41 */ 'WARN_TIMERESET',
  /* 42 */ 'WARN_DATETIME',
  /* 43 */ 'WARN_UI',
  /* 44 */ 'WARN_ARMCOMM',
  /* 45 */ 'WARN_MODECHANGE',
  /* 46 */ 'WARN_LOADLOG_CLEAR',
  /* 47 */ 'WARN_LOADMON_BAY1',
  /* 48 */ 'WARN_LOADMON_BAY2',
  /* 49 */ 'WARN_SYSCONFIG_EQ',
  /* 50 */ 'WARN_NO_OUTPUT',
  /* 51 */ 'WARN_EXPANSIONBOARD',
  /* 52 */ 'WARN_NEWBOARD',
  /* 53 */ 'WARN_BOARDREMOVED',
  /* 54 */ 'WARN_COMMERR',
  /* 55 */ 'WARN_DEFAULTTEMP',
  /* 56 */ 'WARN_BOARDNOTTEMP',
  /* 57 */ 'WARN_BOARDNOTHUMID',
  /* 58 */ 'WARN_DEFTEMPDIS',
  /* 59 */ 'WARN_DEFHUMDIS',
  /* 60 */ 'WARN_DATALOGWRITE',
  /* 61 */ 'WARN_DATALOGREAD',
  /* 62 */ 'WARN_DATALOGFULL',
  /* 63 */ 'WARN_USERLOG_CLEAR',
  /* 64 */ 'WARN_ACTLOGWRITE',
  /* 65 */ 'WARN_ACTLOGREAD',
  /* 66 */ 'WARN_ACTLOG_CLEAR',
  /* 67 */ 'WARN_SDCARD',
  /* 68 */ 'WARN_SDCARD_DIFF',
  /* 69 */ 'WARN_SDCARD_INCOMPAT',
  /* 70 */ 'WARN_SDCARD_INIT',
  /* 71 */ 'WARN_SDCARD_UNINIT',
  /* 72 */ 'WARN_SDCARD_LOCKED',
  /* 73 */ 'WARN_SDCARD_NONE',
  /* 74 */ 'WARN_SAVESETTINGS',
  /* 75 */ 'WARN_FACTORYDEFAULT',
  /* 76 */ 'WARN_RTCACCESS',
  /* 77 */ 'WARN_EEPROMACCESS',
  /* 78 */ 'WARN_MALLOC',
  /* 79 */ 'WARN_INVALIDDATETIME',
  /* 80 */ 'WARN_DSTSTART',
  /* 81 */ 'WARN_DSTSTOP',
  /* 82 */ 'WARN_SETTINGSCONVERT',
  /* 83 */ 'WARN_SETTINGSCNVRTERR',
  /* 84 */ 'WARN_SETTINGSSIZE',
  /* 85 */ 'WARN_VERSION',
  /* 86 */ 'WARN_FILEACCESS',
  /* 87 */ 'WARN_FILENAME',
  /* 88 */ 'WARN_FILEWRITE',
  /* 89 */ 'WARN_SOFTWAREUPDATE',
  /* 90 */ 'WARN_CLEARALERTS',
  /* 91 */ 'WARN_LIGHTS1_AS1',
  /* 92 */ 'WARN_LIGHTS2_AS1',
  /* 93 */ 'WARN_LIGHTS',
  /* 94 */ 'WARN_LOADLOG_FULL',
  /* 95 */ 'WARN_ALARMS_FILE',
  /* 96 */ 'WARN_EQUIPDESC_FILE',
];

// ────────────────────────────────────────────────────────────────────
// Default English warning text (from /var/www/languageAlarms.txt)
// ────────────────────────────────────────────────────────────────────
export const DEFAULT_WARNING_TEXT: Record<string, string> = {
  WARN_PLENTEMP1:         'Invalid Plenum Temperature 1',
  WARN_PLENTEMP2:         'Invalid Plenum Temperature 2',
  WARN_NO_PLENTEMP:       'No valid Plenum Temperature is available',
  WARN_LOWPLENTEMP:       'Low Plenum Temperature Failure',
  WARN_HIGHPLENTEMP:      'High Plenum Temperature Failure',
  WARN_PLENSENSOR:        'Plenum Temperature Sensor Variance',
  WARN_RETURNAIRTEMP:     'Invalid Return Air Temperature',
  WARN_AUXLOWPLENTEMP:    'Auxiliary Low Plenum Temperature Failure',
  WARN_OUTTEMPSENSOR:     'Invalid Outside Air Temperature',
  WARN_NO_STARTTEMP:      'The Cooling Available Temperature can\'t be calculated',
  WARN_STARTTEMP:         'Unable to calculate Start Temperature',
  WARN_PLENHUMID:         'Low Plenum Humidity',
  WARN_INVALIDPLENHUMID:  'Invalid Plenum Humidity',
  WARN_RETURNAIRHUMID:    'Invalid Return Air Humidity',
  WARN_OUTHUMIDSENSOR:    'Invalid Outside Air Humidity',
  WARN_OUTHUMIDVAR:       'Outside Air Humidity Variance',
  WARN_INVALIDCO2:        'Invalid CO2 value',
  WARN_AIRFLOW:           'Air Flow Restriction Failure',
  WARN_FAN:               'Fan Failure',
  WARN_REFRIG_AS1:        'Refrigeration',
  WARN_REFRIG_PWM:        'Refrigeration',
  WARN_REFRIG_STAGE:      'Refrigeration Stage',
  WARN_REFRIG_DEFROST:    'Refrigeration Defrost',
  WARN_CLIMACELL:         'ClimaCell',
  WARN_HUMID1_AS1:        'Humidifier 1',
  WARN_HUMID2_AS1:        'Humidifier 2',
  WARN_HUMIDIFIER:        'Humidifier',
  WARN_HIGHCO2:           'High CO2 Level',
  WARN_AUX1_AS1:          'Auxiliary 1',
  WARN_AUX2_AS1:          'Auxiliary 2',
  WARN_AUX_AS1:           'Auxiliary',
  WARN_AUX:               'Auxiliary',
  WARN_HEAT:              'Heat',
  WARN_CAVITYHEAT:        'Cavity Heater / Pile Fan',
  WARN_CAVHEATCALC:       'Unable to calculate Cavity Heat / Pile Fan Differential - invalid reference temperature',
  WARN_BURNER:            'Burner',
  WARN_POWER:             'System power Failure',
  WARN_REMOTESTANDBY:     'System in Remote Standby',
  WARN_REFRIGSTANDBY:     'System in Refrigeration Standby',
  WARN_NOBROADCAST:       'This controller is configured as a Slave and has not received a Master broadcast in 10 minutes',
  WARN_SLAVENOBROADCAST:  'Slave using local Outside Temperature and Humidity until communication with Master restored',
  WARN_TIMERESET:         'Invalid system Date/Time - system clock reset to 12:00pm (check system battery)',
  WARN_DATETIME:          'Invalid system Date/Time (check system battery)',
  WARN_UI:                'User interface did not respond (check Lantronix)',
  WARN_ARMCOMM:           'System controller is not responding',
  WARN_MODECHANGE:        'System Mode changed to',
  WARN_LOADLOG_CLEAR:     'Loading Monitor Data Log Cleared',
  WARN_LOADMON_BAY1:      'Loading Monitor High Temperature',
  WARN_LOADMON_BAY2:      'Loading Monitor Low Temperature',
  WARN_SYSCONFIG_EQ:      'I/O configuration error',
  WARN_NO_OUTPUT:         'Mode configuration error',
  WARN_EXPANSIONBOARD:    'I/O Expansion board removed',
  WARN_NEWBOARD:          'New Analog Board detected',
  WARN_BOARDREMOVED:      'Analog Board removed',
  WARN_COMMERR:           'Analog Board communication error',
  WARN_DEFAULTTEMP:       'Default Temperature Board missing',
  WARN_BOARDNOTTEMP:      'Analog Board 1 is not a temperature board',
  WARN_BOARDNOTHUMID:     'Analog Board 2 is not a humidity board',
  WARN_DEFTEMPDIS:        'Default Temperature Board disabled',
  WARN_DEFHUMDIS:         'Default Humidity Board disabled',
  WARN_DATALOGWRITE:      'Failure writing to History Log (check SD Card)',
  WARN_DATALOGREAD:       'Failure reading from History Log (check SD Card)',
  WARN_DATALOGFULL:       'History Log (SD Card) is full - set card to overwrite old records or insert new card',
  WARN_USERLOG_CLEAR:     'History Log cleared',
  WARN_ACTLOGWRITE:       'Failure writing to Activity Log (check SD Card)',
  WARN_ACTLOGREAD:        'Failure reading from Activity Log (check SD Card)',
  WARN_ACTLOG_CLEAR:      'Activity Log cleared',
  WARN_SDCARD:            'Unable to access SD Card',
  WARN_SDCARD_DIFF:       'Different SD Card detected - continuing to use',
  WARN_SDCARD_INCOMPAT:   'Incompatible SD Card detected - SD Card will be erased',
  WARN_SDCARD_INIT:       'SD Card initialized',
  WARN_SDCARD_UNINIT:     'Unable to initialize SD Card',
  WARN_SDCARD_LOCKED:     'Unable to access SD Card - SD Card is locked (write protected)',
  WARN_SDCARD_NONE:       'SD Card is not inserted - please insert system SD Card',
  WARN_SAVESETTINGS:      'Unable to read or save System Settings',
  WARN_FACTORYDEFAULT:    'System Settings restored to Factory Default',
  WARN_RTCACCESS:         'Unable to access system clock',
  WARN_EEPROMACCESS:      'Unable to access system memory (EEPROM)',
  WARN_MALLOC:            'Unable to allocate memory for message (ARM)',
  WARN_INVALIDDATETIME:   'Clock could not be set due to an invalid date or time value',
  WARN_DSTSTART:          'Clock has been adjusted for Daylight Saving Time',
  WARN_DSTSTOP:           'Clock has been adjusted for Standard Time',
  WARN_SETTINGSCONVERT:   'System Settings converted to new format',
  WARN_SETTINGSCNVRTERR:  'Unable to convert System Settings to new format',
  WARN_SETTINGSSIZE:      'System Settings size exceeds allocated EEPROM space',
  WARN_VERSION:           'The controller software version does not match the web server software version',
  WARN_FILEACCESS:        'File download error - Unable to access storage device',
  WARN_FILENAME:          'File download error - No file name or no IP address supplied',
  WARN_FILEWRITE:         'File download error - Unable to write to storage device (may be full)',
  WARN_SOFTWAREUPDATE:    'Software upgrade in process',
  WARN_CLEARALERTS:       'Alarms cleared',
  WARN_LIGHTS1_AS1:       'Lights 1',
  WARN_LIGHTS2_AS1:       'Lights 2',
  WARN_LIGHTS:            'Lights',
  WARN_LOADLOG_FULL:      'Loading Monitor Log Full - Terminating data acquisition',
};

// AlarmFail suffix array: maps Value[0] to status text
// From languageAlarms.txt: ALARM_FAIL=Alarm,Fail,Pipe #,output not configured
const ALARM_FAIL = ['', 'Alarm', 'Fail', 'Pipe #', 'output not configured'];

// System mode names (from languageAlarms.txt SYSTEM_MODES line)
const SYSTEM_MODES = [
  'NONE', 'SHUTDOWN', 'STANDBY', 'REMOTE STANDBY', 'COOLING',
  'REFRIGERATION', 'RECIRCULATING', 'HEATING', 'DEFROSTING',
  'PURGING CO2', 'COOLING (RAMPING)', 'REFRIG (RAMPING)',
  'FAN MANUAL', 'FAN SWITCH OFF', 'FAN REMOTE OFF',
  'REFRIG REMOTE OFF', 'OUTSIDE AIR CURE', 'BURNER CURE',
  'COOLING (DEHUMID)', 'REFRIG (DEHUMID)', 'REMOTE OFF',
  'FAILURE', 'FAN BOOST',
];

// Warning indices that use bitmapped Value fields for equipment errors
const BITMAPPED_WARNINGS = new Set([
  'WARN_REFRIG_STAGE', 'WARN_REFRIG_DEFROST', 'WARN_HUMIDIFIER',
  'WARN_LIGHTS', 'WARN_AUX', 'WARN_SYSCONFIG_EQ', 'WARN_NO_OUTPUT',
  'WARN_EXPANSIONBOARD', 'WARN_NEWBOARD', 'WARN_BOARDREMOVED', 'WARN_COMMERR',
]);

// Parsed warning from the firmware wire format
interface ParsedWarning {
  index: number;
  status: number;
  value0: number;
  value1: number;
}

/**
 * Parse the raw AlarmData wire format from the ARM firmware.
 * Format: "INDEX&STATUS,VALUE0,VALUE1,INDEX&STATUS,VALUE0,VALUE1,..."
 * Each warning is a triplet of comma-separated values.
 */
function parseRawAlarmData(raw: string): ParsedWarning[] {
  const warnings: ParsedWarning[] = [];
  const parts = raw.split(',').filter(s => s.trim() !== '');

  let i = 0;
  while (i < parts.length) {
    const first = parts[i];
    if (!first.includes('&')) {
      i++;
      continue;  // skip malformed entries
    }
    const [indexStr, statusStr] = first.split('&');
    const index = parseInt(indexStr, 10);
    const status = parseInt(statusStr, 10);
    const value0 = i + 1 < parts.length ? parseInt(parts[i + 1], 10) : 0;
    const value1 = i + 2 < parts.length ? parseInt(parts[i + 2], 10) : 0;

    if (!isNaN(index) && !isNaN(status)) {
      warnings.push({ index, status, value0, value1 });
    }
    i += 3;
  }

  return warnings;
}

/**
 * Get the warning key and text for a given warning index.
 */
function getWarningInfo(index: number): { key: string; text: string } {
  const key = index < WARNING_KEYS.length ? WARNING_KEYS[index] : `WARN_UNKNOWN_${index}`;
  const text = DEFAULT_WARNING_TEXT[key] ?? `Warning ${index}`;
  return { key, text };
}

/**
 * Build human-readable alarm strings from a bitmapped warning value.
 * Some warnings (WARN_NEWBOARD, WARN_COMMERR, etc.) use Value[0]/Value[1]
 * as a bitmap where each bit represents a board or equipment number.
 *
 * Replicates BuildBitMappedErrors() from LtxWarnings.c
 */
function buildBitmappedAlarms(
  w: ParsedWarning,
  key: string,
  text: string,
  ioNames: string | undefined,
): string[] {
  const results: string[] = [];
  const combined = w.value0 | (w.value1 << 32);  // firmware only uses 32 bits, but be safe

  // Check up to 32 bits for standard, or 64 for SYSCONFIG/NO_OUTPUT/AUX
  const totalBits = (key === 'WARN_SYSCONFIG_EQ' || key === 'WARN_NO_OUTPUT' || key === 'WARN_AUX')
    ? 64 : 32;

  for (let bit = 0; bit < totalBits; bit++) {
    const element = bit < 32 ? 0 : 1;
    const bitPos = bit % 32;
    const value = element === 0 ? w.value0 : w.value1;

    if ((value & (1 << bitPos)) !== 0) {
      let msg: string;

      switch (key) {
        case 'WARN_SYSCONFIG_EQ':
        case 'WARN_NO_OUTPUT': {
          // Look up equipment name from IoNamesData
          const eqName = getEquipmentName(ioNames, bit);
          msg = `${text}: ${eqName} ${ALARM_FAIL[4]}`;
          break;
        }
        case 'WARN_AUX': {
          const eqName = getEquipmentName(ioNames, bit);
          msg = `${eqName} - ${ALARM_FAIL[w.status] ?? 'Alarm'}`;
          break;
        }
        case 'WARN_REFRIG_STAGE': {
          const eqName = getEquipmentName(ioNames, bit);
          msg = `${eqName} - ${ALARM_FAIL[w.value1] ?? 'Alarm'}`;
          break;
        }
        case 'WARN_NEWBOARD':
        case 'WARN_COMMERR':
        case 'WARN_BOARDREMOVED':
        case 'WARN_EXPANSIONBOARD':
          msg = `${text}: ${bit + 1}`;
          break;
        default:
          msg = `${text}: ${bit + 1} - ${ALARM_FAIL[w.value1] ?? 'Alarm'}`;
          break;
      }

      results.push(`${key}=${msg}`);
    }
  }

  return results;
}

/**
 * Extract equipment name from IoNamesData at a given index.
 * IoNamesData format: "Name:type:mode:board:index,Name2:type2:..."
 * Each entry separated by ',', fields within by ':'.
 */
function getEquipmentName(ioNames: string | undefined, index: number): string {
  if (!ioNames) return `Equipment ${index + 1}`;
  const entries = ioNames.split(',');
  if (index >= entries.length) return `Equipment ${index + 1}`;
  const entry = entries[index];
  const colonIdx = entry.indexOf(':');
  return colonIdx >= 0 ? entry.substring(0, colonIdx) : entry;
}

/**
 * Translate raw AlarmData from the ARM firmware into human-readable alarm
 * strings that the Svelte UI can display.
 *
 * Input:  "36&1,1,0,45&1,4,0,52&1,7,0,55&1,1,0,73&1,1,0,74&1,1,0,75&1,1,0,"
 * Output: ["WARN_POWER=System power Failure - Alarm",
 *          "WARN_NEWBOARD=New Analog Board detected: 1",
 *          "WARN_NEWBOARD=New Analog Board detected: 2",
 *          "WARN_NEWBOARD=New Analog Board detected: 3",
 *          "WARN_DEFAULTTEMP=Default Temperature Board missing - Alarm",
 *          ...]
 *
 * @param rawAlarmData  The raw comma-separated AlarmData string from the ARM
 * @param ioNames       Optional IoNamesData for equipment name lookups
 * @returns Array of "WARN_KEY=Human text" strings for Alarms.svelte
 */
export function translateAlarmData(rawAlarmData: string, ioNames?: string): string[] {
  if (!rawAlarmData || rawAlarmData.trim() === '') return [];

  // If the data is already in translated format (KEY=text, e.g. from the ARM simulator
  // or bridge-injected VFD alarms), pass it through directly.
  // Real firmware format always contains '&' (INDEX&STATUS,VALUE0,VALUE1,...)
  if (!rawAlarmData.includes('&')) {
    return rawAlarmData.split(',').filter(s => s.includes('=')).map(s => s.trim());
  }

  const parsed = parseRawAlarmData(rawAlarmData);
  const results: string[] = [];

  for (const w of parsed) {
    const { key, text } = getWarningInfo(w.index);

    // WARN_MODECHANGE is email-only in GellertServerD — skip for UI display
    if (key === 'WARN_MODECHANGE') continue;

    // SD card warnings are not relevant on Constellation (no SD card;
    // logging is handled by the RPi5).  Suppress silently.
    if (key.startsWith('WARN_SDCARD')) continue;

    // Bitmapped warnings produce multiple alarm entries
    if (BITMAPPED_WARNINGS.has(key)) {
      results.push(...buildBitmappedAlarms(w, key, text, ioNames));
      continue;
    }

    // Handle value-dependent text
    if (w.value0 !== 0 || w.value1 !== 0) {
      // Special case: mode change (already skipped above, but handle if ever un-skipped)
      const suffix = ALARM_FAIL[w.value0] ?? '';
      if (suffix) {
        results.push(`${key}=${text} - ${suffix}`);
      } else {
        results.push(`${key}=${text}`);
      }
    } else {
      // No value — just the message text
      results.push(`${key}=${text}`);
    }
  }

  return results;
}
