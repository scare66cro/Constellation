/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     Warnings.h

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:  UI warning system

COMMENTS:

***************************************************************************/
#ifndef WARNINGS_H
#define WARNINGS_H

/*** include files ***/

/*** defines ***/

#define STARTUP_ERR_TIMER     30  // secs

#define NO_WARNING            -1

/*** typedefs and structures ***/

typedef enum
{
  FAIL_FAN,
  FAIL_CLIMACELL,
  FAIL_REFRIGERATION,
  FAIL_REFRIG_STAGES,   // and defrost
  FAIL_HUMIDIFIERS,
  FAIL_AUXILIARY,
  FAIL_HEAT,
  FAIL_CAVITY_HEAT,
  FAIL_BURNER,
  FAIL_LIGHTS,
  FAIL_PLENUM_SENSOR,
  FAIL_OUTSIDE_AIR,
  FAIL_OUTSIDE_HUMIDITY,
  FAIL_PLENUM_HUMIDITY,
  FAIL_HIGH_CO2,
  NUM_FAILURES
} FAILURES;

// the warnings are grouped into primary and secondary alerts
// grouping/ordering them here allows the UI to easily present them in groups
// on the 'Select Email Alerts' page
// NOTE: changes to any of these values must be also be reflected in LtxWarnings.h in the Lantronix code
typedef enum
{
  WARN_PLENTEMP1,   // primary alert group
  WARN_PLENTEMP2,
  WARN_NO_PLENTEMP,
  WARN_LOWPLENTEMP,
  WARN_HIGHPLENTEMP,
  WARN_PLENSENSOR,
  WARN_RETURNAIRTEMP,
  WARN_AUXLOWPLENTEMP,
  WARN_OUTTEMPSENSOR,
  WARN_NO_STARTTEMP,
  WARN_STARTTEMP,
  WARN_PLENHUMID,
  WARN_INVALIDPLENHUMID,
  WARN_RETURNAIRHUMID,
  WARN_OUTHUMIDSENSOR,
  WARN_OUTHUMIDVAR,
  WARN_INVALIDCO2,
  WARN_AIRFLOW,
  WARN_FAN,
  WARN_REFRIG_AS1,      // Agri-Star only
  WARN_REFRIG_PWM,      // AS2 only
  WARN_REFRIG_STAGE,    // AS2 only
  WARN_REFRIG_DEFROST,  // AS2 only
  WARN_CLIMACELL,
  WARN_HUMID1_AS1,      // Agri-Star only
  WARN_HUMID2_AS1,      // Agri-Star only
  WARN_HUMIDIFIER,      // AS2 only
  WARN_HIGHCO2,
  WARN_AUX1_AS1,        // Agri-Star only (onion)
  WARN_AUX2_AS1,        // Agri-Star only (onion)
  WARN_AUX_AS1,         // Agri-Star only (potato)
  WARN_AUX,             // AS2 only
  WARN_HEAT,
  WARN_CAVITYHEAT,
  WARN_CAVHEATCALC,
  WARN_BURNER,
  WARN_POWER,
  WARN_REMOTESTANDBY,
  WARN_REFRIGSTANDBY,
  WARN_NOBROADCAST,
  WARN_SLAVENOBROADCAST,
  WARN_TIMERESET,
  WARN_DATETIME,
  WARN_UI,
  WARN_ARMCOMM,
  WARN_MODECHANGE,
  WARN_LOADLOG_CLEAR,
  WARN_LOADMON_BAY1,
  WARN_LOADMON_BAY2,
  WARN_SYSCONFIG_EQ,
  WARN_NO_OUTPUT,
  WARN_EXPANSIONBOARD,
  WARN_NEWBOARD,    // secondary alert group
  WARN_BOARDREMOVED,
  WARN_COMMERR,
  WARN_DEFAULTTEMP,
  WARN_BOARDNOTTEMP,
  WARN_BOARDNOTHUMID,
  WARN_DEFTEMPDIS,
  WARN_DEFHUMDIS,
  WARN_DATALOGWRITE,
  WARN_DATALOGREAD,
  WARN_DATALOGFULL,
  WARN_USERLOG_CLEAR,
  WARN_ACTLOGWRITE,
  WARN_ACTLOGREAD,
  WARN_ACTLOG_CLEAR,
  WARN_SDCARD,
  WARN_SDCARD_DIFF,
  WARN_SDCARD_INCOMPAT,
  WARN_SDCARD_INIT,
  WARN_SDCARD_UNINIT,
  WARN_SDCARD_LOCKED,
  WARN_SDCARD_NONE,
  WARN_SAVESETTINGS,
  WARN_FACTORYDEFAULT,
  WARN_RTCACCESS,
  WARN_EEPROMACCESS,
  WARN_MALLOC,
  WARN_INVALIDDATETIME,
  WARN_DSTSTART,
  WARN_DSTSTOP,
  WARN_SETTINGSCONVERT,
  WARN_SETTINGSCNVRTERR,
  WARN_SETTINGSSIZE,
  WARN_VERSION,
  WARN_FILEACCESS,
  WARN_FILENAME,
  WARN_FILEWRITE,
  WARN_SOFTWAREUPDATE,
  WARN_CLEARALERTS,
  WARN_LIGHTS1_AS1,   // Agri-Star only
  WARN_LIGHTS2_AS1,   // Agri-Star only
  WARN_LIGHTS,        // AS2 only
  WARN_LOADLOG_FULL,

  WARN_ALARMS_FILE,   // keep these at the end
  WARN_EQUIPDESC_FILE,
  NUM_WARNINGS
} WARNING_ITEMS;  // NOTE: changes to any of these values must be also be reflected in LtxWarnings.h in the http server code

#define WARNING_VALUE_LEN   (2)
#define NA                  (0)

typedef struct {
  char Status;
  uint32_t Value[WARNING_VALUE_LEN];      // to hold analog board address bitmap & I/O configuration errors
} WARNING;

/*** external variables ***/

/*** external functions ***/

extern int IsBoardWarning(void);
extern void WarningsClear(void);
extern void WarningsClearChk(void);
extern void WarningsSendToUI(int ForceSend);
extern void WarningsSet(WARNING_ITEMS index, char status, uint32_t value, uint32_t eqIo);
extern char WarningStatus(WARNING_ITEMS index);
extern void WarningValue(WARNING_ITEMS index, uint32_t *value);

#endif

/***   End Of File   ***/
