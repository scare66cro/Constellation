/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     SystemLogs.h

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:

COMMENTS:

***************************************************************************/

#ifndef SYSTEMLOGS_H
#define SYSTEMLOGS_H

/*** include files ***/

#include "DataExc.h"
#include "SDCard.h"
#include "SerialShift.h"
#include "Settings.h"
#include "Warnings.h"

/*** defines ***/

#define NUM_SYSLOGS_PER_DAY         (24 * 60)   // once a minute
#define NUM_SYSLOG_RECS             (14 * NUM_SYSLOGS_PER_DAY)    // 2 weeks of log data

// log main items
#define LOG_PLENTEMPSET             0   // NOTE: these values are used by the http server to send translated descriptions
#define LOG_PLENTEMP                1   // to the ARM at startup, any changes here must be reflected in LtxWarnings.h
#define LOG_COOLAVAILTEMP           2
#define LOG_PLENHUMSET              3
#define LOG_MODE                    4
#define LOG_FANSPEED                5
#define LOG_COOLOUTPUT              6
#define LOG_REFRIGOUTPUT            7
#define LOG_BURNOUTPUT              8
#define LOG_CALCHUM                 9
#define LOG_DAILYFANRUNTIME         10
  // the above items are used in the UserLog and the SysLog records
  // for the 'main' items which are the non-sensor items from the
  // system monitor page (the sensors are kept in the sensor array)
  // they are also used in the DataQuery array

// total number of log main items
#define LOG_MAINITEMS               11
  // it is used to define the length of the MainPage array and also
  // to distinguish the main items from the special query tags and
  // sensors in the DataQuery array

// special query tags
#define QUERYTAG_CO2                11
  // the CO2 tag is used by the UserLog and the SysLog
#define QUERYTAG_ALLITEMS           12
#define QUERYTAG_EQUIPSTATUS        13
#define QUERYTAG_REMOTESTATUS       14
#define QUERYTAG_WARNINGS           15
  // these tags are only used by the SysLog (activity log)

// number of DataQuery main tags
#define DATAQUERY_MAINTAGS          16
  // this is used to distinguish the 'main' tags from the sensor tags
  // in the DataQuery - it includes the log main items plus the query tags

// sensor query tags
#define QUERYTAG_TEMPSNSR           16
#define QUERYTAG_HUMSNSR            17

// total number of DataQuery tags
#define DATAQUERY_TOTALTAGS         18

#define DATAQUERY_MAXSELECTIONS     10
  // determined by UI - the UI only allow 10 items to be picked at one time

#define DATAQUERY_LEN               DATAQUERY_MAINTAGS + (2*DATAQUERY_MAXSELECTIONS)
  // The data query from the UI can only have 10 items in it, but it could be 10
  // main items, 10 temperature sensors, or 10 humidity sensors (or any combination).
  // The DataQuery array is set up to be one-to-one with the log main items plus 10
  // temperature and 10 humidity sensors locations.  The sensor locations store the
  // unique sensor ID (SID), but because the sensors have different units (degrees &
  // percent) they have to be distinguishable.  The location in the DataQuery array
  // determines what type they are.

// start positions in the DataQuery array for the temperature and humidity sensors
#define DATAQUERY_TEMPSNSR_START    DATAQUERY_MAINTAGS
#define DATAQUERY_HUMIDSNSR_START   DATAQUERY_TEMPSNSR_START+DATAQUERY_MAXSELECTIONS

// syslog equipment status labels
typedef enum
{
  LOG_FANSWITCH,    // NOTE: these values are used by the http server to send translated descriptions
  LOG_FANFAIL,      // to the ARM at startup, any changes here must be reflected in LtxWarnings.h
  LOG_FANOUTPUT,
  LOG_CLIMACELLSWITCH,
  LOG_CLIMACELLFAIL,
  LOG_CLIMACELLOUTPUT,
  LOG_BURNERFAIL,
  LOG_BURNEROUTPUT,
  LOG_HUMIDSWITCH,
  LOG_HUMID1FAIL,
  LOG_HUMID1HEADOUTPUT,
  LOG_HUMID1PUMPOUTPUT,
  LOG_HUMID2FAIL,
  LOG_HUMID2HEADOUTPUT,
  LOG_HUMID2PUMPOUTPUT,
  LOG_REFSWITCH,
  LOG_REFFAIL,
  LOG_REF1OUTPUT,
  LOG_REF2OUTPUT,
  LOG_REF3OUTPUT,
  LOG_REF4OUTPUT,
  LOG_REF5OUTPUT,
  LOG_REF6OUTPUT,
  LOG_REF7OUTPUT,
  LOG_REF8OUTPUT,
  LOG_REFDEFROST1OUTPUT,
  LOG_REFDEFROST2OUTPUT,
  LOG_HEATFAIL,
  LOG_HEATOUTPUT,
  LOG_DOORSWITCH,
  LOG_DOOROUTPUT,
  LOG_CAVHEATFAIL,
  LOG_CAVHEATOUTPUT,
  LOG_GREEN,
  LOG_YELLOW,
  LOG_RED,
  LOG_LIGHTS1IN,
  LOG_LIGHTS1OUTPUT,
  LOG_LIGHTS2IN,
  LOG_LIGHTS2OUTPUT,

  LOG_AUX1FAIL,
  LOG_AUX1OUTPUT,
  LOG_AUX2FAIL,
  LOG_AUX2OUTPUT,
  LOG_AUX3FAIL,
  LOG_AUX3OUTPUT,
  LOG_AUX4FAIL,
  LOG_AUX4OUTPUT,
  LOG_AUX5FAIL,
  LOG_AUX5OUTPUT,
  LOG_AUX6FAIL,
  LOG_AUX6OUTPUT,
  LOG_AUX7FAIL,
  LOG_AUX7OUTPUT,
  LOG_AUX8FAIL,
  LOG_AUX8OUTPUT,

  LOG_HUMID3FAIL,
  LOG_HUMID3HEADOUTPUT,
  LOG_HUMID3PUMPOUTPUT,

  LOG_REF1FAIL,
  LOG_REF2FAIL,
  LOG_REF3FAIL,
  LOG_REF4FAIL,
  LOG_REF5FAIL,
  LOG_REF6FAIL,
  LOG_REF7FAIL,
  LOG_REF8FAIL,
  LOG_REFDEFROST1FAIL,
  LOG_REFDEFROST2FAIL,

  LOG_BURNERSWITCH,
  LOG_CURESWITCH,

  SYSLOG_EQUIP_ITEMS
} SYSLOG_EQUIP_LABELS;

// syslog remote labels
typedef enum
{
  LOG_DIAG_REF1,    // NOTE: these values are used by the http server to send translated descriptions
  LOG_DIAG_REF2,    // to the ARM at startup, any changes here must be reflected in LtxWarnings.h
  LOG_DIAG_REF3,
  LOG_DIAG_REF4,
  LOG_DIAG_REF5,
  LOG_DIAG_REF6,
  LOG_DIAG_REF7,
  LOG_DIAG_REF8,
  LOG_DIAG_DEFROST1,
  LOG_DIAG_DEFROST2,
  LOG_REMOTEOFF_REF,
  LOG_REMOTEOFF_FAN,
  LOG_REMOTEOFF_CLIMACELL,
  LOG_REMOTEOFF_HUMID1,
  LOG_REMOTEOFF_HUMID2,
  LOG_REMOTEOFF_HUMID3,
  LOG_REMOTEOFF_HEAT,
  LOG_REMOTEOFF_AUX1,
  LOG_REMOTEOFF_AUX2,
  LOG_REMOTEOFF_AUX3,
  LOG_REMOTEOFF_AUX4,
  LOG_REMOTEOFF_AUX5,
  LOG_REMOTEOFF_AUX6,
  LOG_REMOTEOFF_AUX7,
  LOG_REMOTEOFF_AUX8,
  LOG_REMOTEOFF_CAVHEAT,
  LOG_REMOTEOFF_LIGHTS1,
  LOG_REMOTEOFF_LIGHTS2,

  LOG_REMOTEOFF_BURNER,
  LOG_REMOTEOFF_CURE,

  SYSLOG_REMOTE_NUM_LABELS
} SYSLOG_REMOTE_LABELS;

// actual number of items
#define SYSLOG_REMOTE_ITEMS         LOG_REMOTEOFF_LIGHTS2 + 1

//#define REQTYPE_SERIAL              0
#define REQTYPE_TOFILE              1
#define REQTYPE_UI                  2

#define FILE_MODE                   2
#define GRAPH_MODE                  1
#define TABLE_MODE                  0

#define LOG_LABEL                   0
#define LOG_DATA                    1

typedef enum syslog_record_labels   // NOTE: these values are used by the http server to send translated descriptions
{                                   // to the ARM at startup, any changes here must be reflected in LtxWarnings.h
  SL_RECORD,
  SL_DATE,
  SL_TIME,
  SL_PIPE,
  SL_PILEFAN_OUTPUT,
  SL_PILEFAN_FAIL,
  NUM_SYSLOG_RECORD_LABELS
} SYSLOG_RECORD_LABELS;

/*** typedefs and structures ***/

typedef struct
{
  char Label[LOG_LABELS];
} MODE_LABELS;

typedef struct
{
  char Label[LOG_LABELS];
  char Units[4];
} SYSLOG_LABELS;

typedef struct
{
  char Status;
  uint32_t Value[WARNING_VALUE_LEN];      // to hold analog board address bitmap & I/O configuration errors
} WARNING_LOG;

typedef struct
{
  unsigned int RecordNum;
  char         Date[DATE_LEN+1];
  char         Time[TIME_LEN+1];
  short        MainPage[LOG_MAINITEMS];
  short        Sensors[ANALOG_BOARDS_PER_SYSTEM * ANALOG_SENSORS_PER_BOARD];
  char         EquipStatus[EQUIPSTATUS_LEN];
  char         RemoteOff[SYSLOG_REMOTE_ITEMS];
  WARNING_LOG  Warning[NUM_WARNINGS];
} SYSLOG_RECORD;

typedef struct
{
  char Tag[MSG_MAX_TAG_LEN+1];
  char Label[LOG_LABELS];
  char Units[4];
} QUERY_TAGS;

/*** external variables ***/

extern QUERY_TAGS QueryTags[DATAQUERY_TOTALTAGS];
extern SYSLOG_LABELS SysLogRecordLabels[NUM_SYSLOG_RECORD_LABELS];

extern signed char DataQuery[DATAQUERY_LEN];

extern char SensorList[ANALOG_BOARDS_PER_SYSTEM * ANALOG_SENSORS_PER_BOARD];  // tracks which sensors have log values

extern char DataMsg[MSG_TX_BUFFER_SIZE];
extern char DatesMsg[MSG_TX_BUFFER_SIZE];
extern char KeyMsg[MSG_TX_BUFFER_SIZE];
extern char TimesMsg[MSG_TX_BUFFER_SIZE];
extern char WarningMsg[MSG_TX_BUFFER_SIZE];

/*** external functions ***/

extern void FormatLogMainItems(short MainItem, int i, int Mode, int Item);
extern void FormatLogSensor(short *Sensors, int SensorID, int Mode, int Item);

extern void ProcessLogQuery(char *QueryItems);
extern void QueryTags_Init(void);

extern void SaveSensor(short *save, int board, int sensor);

extern void StoreQueryTag(void);
extern void StoreSysLogRec(void);
extern void StoreSysLogEquip(void);
extern void StoreSysLogRemote(void);
extern void StorePIDLogLabel(void);
extern void StoreSysMode(void);

extern void SystemLogLabel_Init(void);
extern void SystemLogReset(SYSLOG_HEADER *Header, int WriteHeader);
extern int SystemLogSendToFile(int BeginRecord, int EndRecord, int SessionID);
extern int SystemLogSendToUI(int BeginRecord, int EndRecord, int SessionID, int WarningsOnly);
extern int SystemLogWrite(int AlarmCleared);

#endif

/***   End Of File   ***/
