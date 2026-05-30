/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     SettingsTypes.h

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:

COMMENTS:

***************************************************************************/

#ifndef SETTINGSTYPES_H
#define SETTINGSTYPES_H

/*** include files ***/

/*** defines ***/

#define NUM_LOADLOG_SENSORS       2
#define NUM_NETWORK_NODES         36
#define NUM_AUX_OUTPUTS           8
#define NUM_AUX_PROGRAM_RULES     6
#define NUM_AUX_SWITCHES          2
#define NUM_REFRIG_STAGES         8
#define NUM_DEFROST_STAGES        2
#define NUM_HUMIDIFIERS           3
#define NUM_CLIMACELLS            1
#define TOTAL_HUMID_EQUIP         (NUM_HUMIDIFIERS + NUM_CLIMACELLS)

#define DATE_LEN                  10    // 12/31/2007
#define EMAIL_FROM_LEN            45
#define EMAIL_TO_LEN              110
#define HOMEPAGE_LEN              25
#define HUMID_LEN                 3     // 100
#define IP_ADD_LEN                15    // 192.168.200.242
#define NAME_LEN                  25
#define NUM_PASSWORDS             10
#define PASSWORD_LEN              10
#define PHONE_LEN                 15    // (509) 922-5629
#define HTTPPORT_LEN              5     // 65535
#define SITE_ID_LEN               35
#define TEMP_LEN                  5     // 101.5, -40.9
#define TIME_LEN                  8     // 12:30:05
#define VERSION_LEN               20    // software version string length

// master/slave modes
#define MSMODE_LOCAL              0
#define MSMODE_MASTER             1
#define MSMODE_SLAVE              2
#define MSMODE_SLAVE_NOBROADCAST  3

#define DEFAULT_USER              "DEFAULT"
#define DEFAULT_PASSWORD          "GELLERT"
#define DEFAULT_P2PASSWORD        "4GR1*"
#define DEFAULT_LOGINPASSWORD     "GELLERT"

/*** typedefs and structures ***/

typedef struct
{
  float   TempSet;          // degrees
  uint8_t HumidSet;         // %
  uint8_t HumidSetpointRef; // 0 - plenum, 1 - return air (onion)
  uint8_t HumidLowFailure;  // %
  float   SensorDiff;       // degrees
  float   LowAlarmTemp;     // degrees
  char    LowAlarmTimer;    // mins
  float   HighAlarmTemp;    // degrees
  char    HighAlarmTimer;   // mins
} PLENUM_PARAMS;

typedef struct
{
  float P;
  float I;
  float D;
  float U;
} PID_PARAMS;

typedef struct
{
  float Diff;           // degrees
  uint8_t CtrlMode;     // outside or plenum temp
  uint8_t AboveBelow;   // select
  uint8_t TempRef;      // select
  uint8_t CalcHumidMax; // % (onion)
} OUTSIDE_AIR_CTRL;

typedef struct
{
  PID_PARAMS PID;
  char  Mode;           // select
  char  On;             // %
  char  Low;            // %
  char  Manual;         // %
  float TempSet;        // degrees (shared for manual mode max temp)
  float Threshold;      // %       (shared for manual mode restart temp)
} BURNER_CTRL;

typedef struct
{
  float StartTemp;      // degrees
  char  StartHumid;     // %
  char  HumidHighLimit; // %
  char  HumidRef;       // select
  float TempLowLimit;   // degrees
  float TempHighLimit;  // degrees
  char  RunTimes[48];   // cure mode runtimes
} CURE_CTRL;

typedef struct
{
  PID_PARAMS PID;
  char  Efficiency;     // %
  short Altitude;
  char  AltitudeUnits;  // feet/meters
  char Times[48];       // climacell runtimes
} CLIMACELL_CTRL;

typedef struct
{
  PID_PARAMS PID;
  short ActuatorTime;   // secs (1-500)
  char CoolAirCycle;    // mins (1-30)
} DOOR_CTRL;

typedef struct
{
  uint8_t On;
  uint8_t Off;
  uint8_t Diagnostic;      // diagnostic off/on
} REFRIG;

typedef struct
{
  uint8_t Mode;             // select
  PID_PARAMS PID;
  REFRIG Stage[NUM_REFRIG_STAGES];
  REFRIG Defrost[NUM_DEFROST_STAGES];
  uint8_t DefrostPeriod;    // hours
  uint8_t DefrostDuration;  // mins
  uint8_t Purge;            // select
  float Limit;              // degrees (onion)
  uint8_t FailMode;         // select
} REFRIG_CTRL;

typedef struct
{
  char  MaxSpeed;       // %
  char  MinSpeed;       // %
  char  RefrigSpeed;    // %
  char  RecircSpeed;    // %
  char  PrevSpeed;      // % (stores the cooling mode fan speed)
  char  UpdatePeriod;   // hours
  float TempDiff;       // degrees
  char  TempRef1;       // select
  char  TempRef2;       // select
  char  UpdateMode;     // select (onion)
  uint32_t DailyDay;         // day value for daily reset
  uint32_t DailyRunTime;     // secs (displayed as hours)
  uint32_t TotalRefrigTime;  // secs (displayed as hours) (FanTotalRecircTime & FanTotalStandbyTime defined at bottom)
  uint32_t TotalRunTime;     // secs (displayed as hours)
  uint32_t TotalCoolingTime; // secs (displayed as hours)
  uint32_t TotalRecircTime;  // secs (displayed as hours)
  uint32_t TotalStandbyTime; // secs (displayed as hours)
  uint32_t TotalCureTime;    // secs (displayed as hours)
} FAN_CTRL;

typedef struct
{
  float   UpdateTemp;     // degrees
  char    UpdatePeriod;   // hours
  float   TempDiff;       // degrees
  uint8_t TempRef;        // select
  float   TargetTemp;     // degrees
} RAMP_CTRL;

typedef struct
{
  char Mode;            // select
  float MaxTemp;        // degrees
  float MinTemp;        // degrees
  char Duration;        // mins
  uint8_t RefrigThresh; // %
  unsigned int Last;    // counter (secs)
  unsigned int Start;   // counter (secs)
} CO2_PURGE_CTRL;

typedef struct
{
  short Set;            // ppm (1-10000)
  char CylceTime;       // hours between purges
  char FanOutput;       // %
  char DoorOutput;      // %
  int16_t HighFailure;  // ppm
  CO2_PURGE_CTRL Purge;
} CO2_CTRL;

typedef struct
{
  char Mode;            // select
  char Speed;           // %
  char Interval;        // hours (between periods or continuous runtime)
  char Duration;        // mins
  float Temp;           // degrees
} FANBOOST_CTRL;

typedef struct
{
  uint8_t Mode;         // select
  float   Diff;         // degrees
  uint8_t DutyCycle;    // %
  uint8_t Label;        // select
  uint8_t StandbyOn;    // 1 = keep running in standby
} CAVITYHEAT_CTRL;

typedef struct
{
  short Interval;            // mins
  char  Wrap;
} USER_LOG;

typedef struct
{
  char    Wrap;
  uint8_t Door;
  uint8_t Refrig;
} PID_LOG;

typedef struct
{
  USER_LOG User;
  PID_LOG PID;
  char  GraphFavorites[MSG_TX_CGIVAR_SIZE];
} LOG_CTRL;

typedef struct
{
  uint8_t  Alerts;              // enable/disable
  char     To[EMAIL_TO_LEN+1];
  char     From[EMAIL_FROM_LEN+1];
  char     Server[EMAIL_FROM_LEN+1];
  uint8_t  AuthType;            // select
  uint16_t Port;
  char     Account[EMAIL_FROM_LEN+1];
  char     Password[EMAIL_FROM_LEN+1];
  char     Display[IP_ADD_LEN+1+SITE_ID_LEN+1];  // IP address & name of display that sends the email
} EMAIL_SETUP;

typedef enum
{
  ACTIVE_SETTINGS,
  SAVED_SETTINGS
} SYSTEM_SETTINGS_TYPE;

typedef enum
{
  SR_CLEAR = 0,
  SR_REQUEST= 1
} SAVE_SETTINGS_REQUEST;

typedef enum
{
  ET_STRING,
  ET_CHAR,
  ET_SHORT,
  ET_USHORT,
  ET_INT8,
  ET_INT16,
  ET_INT32,
  ET_UINT8,
  ET_UINT16,
  ET_UINT32,
  ET_FLOAT,
  ET_RUNTIME,
} ELEMENT_TYPE;

typedef enum
{
  RO_FAN,
  RO_REFRIGERATION,
  RO_CLIMACELL,
  RO_HEAT,
  RO_CAVITY_HEAT,
  RO_BURNER,
  RO_CURE,
  RO_HUMIDIFIER1,
  RO_HUMIDIFIER2,
  RO_HUMIDIFIER3,
  RO_LIGHTS1,
  RO_LIGHTS2,
  RO_AUX1,
  RO_AUX2,
  RO_AUX3,
  RO_AUX4,
  RO_AUX5,
  RO_AUX6,
  RO_AUX7,
  RO_AUX8,
  NUM_REMOTE_OFF
} EQ_REMOTE_OFF;

typedef struct
{
  SAVE_SETTINGS_REQUEST Status;
  SYSTEM_SETTINGS_TYPE Type;
  portTickType BlockTimeout;
  xSemaphoreHandle Semaphore;
} SAVE_SETTINGS_INFO;

typedef struct {
  uint8_t Type;
  uint8_t Disabled;
  char  Label[BOARD_LABELS+1];
  float Value;
  float Offset;
  /* Custom 4-20 mA scaling — only consulted when Type == ANALOG_SENSOR_TYPE_MA (5)
   * AND EngMin != EngMax. ReadAnalogBoards then scales raw mA into engineering
   * units: Value = EngMin + (mA - 4) * (EngMax - EngMin) / 16.
   * DisplayUnit drives the UI suffix (see proto/agristar/io.proto for enum). */
  uint8_t DisplayUnit;
  float EngMin;
  float EngMax;
} ANALOG_SENSOR;

typedef struct {
  uint8_t Present;
  uint8_t Type;
  uint8_t Disabled;
  uint8_t Address;
  char Label[BOARD_LABELS+1];
  char Version[VERSION_LEN+1];
  uint8_t CommErr;
  ANALOG_SENSOR Sensor[ANALOG_SENSORS_PER_BOARD];
} ANALOG_BOARD;

typedef struct {
  char On;
  char Off;
} HUMID_CYCLE;

typedef struct {
  char Mode;
  HUMID_CYCLE DutyCycle[3];   // cool, recirc, refrig
} HUMID_CONTROL;

typedef struct {
  uint8_t Mode;
  uint8_t Timer;
} FAILURE_MODE;

typedef struct {
  char  ID[SITE_ID_LEN+1];
  char  IP[IP_ADD_LEN+1];
  char  PublicIP[IP_ADD_LEN+1];
  short HttpPort;
} NETWORK_NODES;

typedef struct {
  char  ID[PASSWORD_LEN+1];
  char  Password[PASSWORD_LEN+1];
} PASSWORDS;

typedef struct {
  char    Label[BOARD_LABELS+1];
  uint8_t SensorID;
  uint8_t Pipe;
  char Status;
} LOADLOG_INFO;

typedef struct {
  LOADLOG_INFO Bay[NUM_LOADLOG_SENSORS];
  float        AlarmTemp[NUM_LOADLOG_SENSORS];  // degrees
  char         IrSensorsAvailable[NUM_LOADLOG_SENSORS];
} LOAD_MONITOR;

typedef struct ProgramRule
{
  AUXPROG_RULE_TYPE Type;
  int16_t IoIndex;
  uint8_t State;
  AUXPROG_OPERATOR Op;
  float SensorValue;
  AUXPROG_AND_OR AndOr;
  int16_t ReferenceIndex;
} AUX_PROGRAM_RULE;

typedef struct AuxProgram
{
  uint8_t RuleStatus;
  uint8_t OutputState;
  uint8_t DutyCycle;
  uint8_t Units;
  uint32_t Period;
  uint32_t Timer;
  AUX_PROGRAM_RULE Rule[NUM_AUX_PROGRAM_RULES];
} AUX_PROGRAM;

/*** external variables ***/

/*** external functions ***/

#endif

/***   End Of File   ***/
