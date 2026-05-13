/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     States.h

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:

COMMENTS:

***************************************************************************/
#ifndef STATES_H
#define STATES_H

/*** include files ***/

/*** defines ***/

// failure and alarm modes - defined in UI
#define FM_NONE               0
#define FM_ALARM              1
#define FM_FAIL               2
#define FM_PRELIM             255

//typedef enum
//{
//  FM_NONE = 0,
//  FM_ALARM = 1,
//  FM_FAIL = 2,
//  FM_PRELIM = 255
//};

// ramp modes
#define RAMP_DOWN             2
#define RAMP_OFF              0
#define RAMP_ON               1
#define RAMP_UP               3

// run clock modes - defined in UI
#define RC_COOLING            1
#define RC_CURE               5
#define RC_RECIRC             2
#define RC_REFRIG             4
#define RC_STANDBY            3

// system modes
#define SM_POTATO             0
#define SM_ONION              1

// system states
#define ST_UNDEFINED          0
//#define ST_AIRFLOWFAIL        1
//#define ST_AUXFAIL            2
//#define ST_AUXLOWPLENTEMPFAIL 3
//#define ST_CAVITYHEATFAIL     4
//#define ST_CLIMACELLFAIL      5
#define ST_COOLING            6
#define ST_DEFROST            7
#define ST_FAN_MANUAL         8
#define ST_FAN_OFF            9
#define ST_FAN_REMOTEOFF      10
#define ST_FAILURE            11
//#define ST_FANFAIL            11
//#define ST_HEATFAIL           12
#define ST_HEATING            13
//#define ST_HIGHCO2FAIL        14
//#define ST_HIGHPLENTEMPFAIL   15
//#define ST_HUMIDFAIL          16
//#define ST_SYSCONFIGFAIL      17
//#define ST_LOWPLENTEMPFAIL    18
//#define ST_NO_PLENTEMP        19
//#define ST_NO_STARTTEMP       20
//#define ST_OUTHUMIDSENSORFAIL 21
//#define ST_OUTHUMIDVARFAIL    22
//#define ST_OUTTEMPSENSORFAIL  23
//#define ST_PLENHUMIDFAIL      24
//#define ST_PLENSENSORFAIL     25
//#define ST_POWERFAIL          26
#define ST_RECIRC             27
#define ST_REFRIG             28
//#define ST_REFRIGFAIL         29
#define ST_REFRIG_REMOTEOFF   30
#define ST_REFRIG_STANDBY     31
#define ST_REMOTE_STANDBY     32
#define ST_SHUTDOWN           33
#define ST_STANDBY            34

// system states (onion)
#define ST_AIRCURE            35
#define ST_AUX2FAIL           36
#define ST_BURNERCURE         37
//#define ST_BURNERFAIL         38
#define ST_COOLDEHUMID        39
#define ST_REFRIGDEHUMID      40

#define ST_SYSTEM_REMOTEOFF   41

// UI modes
#define UI_SHUTDOWN           1   // NOTE: these values are used by the http server to send translated descriptions
#define UI_STANDBY            2   // to the ARM at startup, any changes here must be reflected in LtxWarnings.h
#define UI_REMOTE_STANDBY     3
#define UI_COOLING            4
#define UI_REFRIG             5
#define UI_RECIRC             6
#define UI_HEATING            7
#define UI_DEFROST            8
#define UI_PURGE              9
#define UI_RAMPCOOL           10
#define UI_RAMPREFRIG         11
#define UI_FAN_MANUAL         12
#define UI_FAN_OFF            13
#define UI_FAN_REMOTEOFF      14
#define UI_REFRIG_REMOTEOFF   15
#define UI_AIRCURE            16
#define UI_BURNERCURE         17
#define UI_COOLDEHUMID        18
#define UI_REFRIGDEHUMID      19
#define UI_SYSTEM_REMOTEOFF   20
#define UI_FAILURE            21
#define UI_FANBOOST           22

// NOTE: changes to this value must be also be reflected in LtxWarning.h in the Lantronix code
//       changes or additions to the UI modes above need to be reflected in jsDataExcLib.js\getCurrentMode() in the UI code
#define NUM_UI_MODES          23

/*** typedefs and structures ***/

typedef enum {
  OSA_CTRL_OUTSIDE,
  OSA_CTRL_PLENUM
} OSA_CTRL_TYPE;

typedef enum {
  BURNER_OFF      = 0,
  BURNER_MANUAL   = 1,
  BURNER_ECONOMY  = 2,
  BURNER_MAX      = 3,
} BURNER_MODE;

typedef enum {
  CS_OFF                    = 0,
  CS_AIR                    = 1,
  CS_MANUAL                 = 2,
  CS_BURNER                 = 3,
  CS_DEHUMID                = 4,
  CS_MOD_DOOR               = 5,
  CS_MOD_BURNER             = 6,
  CS_MOD_BURNER_DOOR_LOCK   = 7,
  CS_MOD_BURNER_DOOR_UNLOCK = 8,
  CS_HOLD_BURNER_MOD_DOOR   = 9
} CURE_STATE;

typedef struct {
  char         Status;
  float        Ref;
} RAMP_RATE;

typedef struct
{
  float CurError;
  float PrevError;
  float IntError;
  char  PWMValue;
} PWM_OUTPUT_STATE;

/*** external variables ***/

extern RAMP_RATE Ramp;

extern char CurrentMode;
extern char SystemState;
extern char CureState;

/*** external functions ***/

extern int CalculatedHumidity(void);
extern void FanRunTimer(void);
extern int RunClockMode(void);
extern void SetMode(void);
extern void SetSystemState(void);

#endif

/***   End Of File   ***/
