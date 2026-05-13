/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     Controls.h

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:

COMMENTS:

***************************************************************************/

#ifndef CONTROLS_H
#define CONTROLS_H

/*** include files ***/

#include "SerialShift.h"

/*** defines ***/

#define HUMID_CTRL_HUMID_1        0
#define HUMID_CTRL_HUMID_2        1
#define HUMID_CTRL_HUMID_3        2
#define HUMID_CTRL_CLIMACELL      3

// climacell clock modes - defined in UI
#define CC_OFF                  1
#define CC_ON                   2
#define CC_COOL_ONLY            3
#define CC_AUTO                 4

#define DUTYCYCLE_INC_VALUE     5
#define PID_WINDUP_LIMIT        5
#define CONTINUOUSFAN_THRESHOLD 80

#define PURGE_TEMP_LIMIT        5
#define PURGE_AUTO_CYCLE_LIMIT  10

#define ENUM_UNDEFINED          (0x000000FF)

#define CHANGE_STATE(state) state = (state) ? 0 : 1

/*** typedefs and structures ***/

typedef enum
{
  CTRL_AUTO = 254,
  CTRL_OFF = 255
} EQ_CONTROL;

typedef enum
{
  HE_CYCLE_OFF   = 0,
  HE_CYCLE_ON    = 1,
  HE_CONTROL_OFF = 2,
} HE_CONTROL;

typedef enum
{
  HM_MANUAL = 0,
  HM_TIMER  = 1,
  HM_AUTO   = 2,
  HM_OFF    = 255
} HUMIDITY_MODE;

typedef enum RuleType
{
  RT_MANUAL = 0,
  RT_OUTPUT = 1,
  RT_INPUT = 2,
  RT_SWITCH = 3,
  RT_SENSOR = 4,
  RT_MODE = 5,
  RT_UNDEFINED = ENUM_UNDEFINED
} AUXPROG_RULE_TYPE;

typedef enum Operator
{
  OP_EQ = 0,
  OP_GT = 1,
  OP_LT = 2,
  OP_UNDEFINED = ENUM_UNDEFINED
} AUXPROG_OPERATOR;

typedef enum AndOr
{
  AO_AND = 0,
  AO_OR = 1,
  AO_END = ENUM_UNDEFINED
} AUXPROG_AND_OR;

typedef enum SystemMode
{
  AM_COOLING = 0,
  AM_REFRIG = 1,
  AM_RECIRC = 2,
  AM_HEATING = 3,
  AM_PURGE = 4,
  AM_DEFROST = 5,
  AM_STANDBY = 6,
  AM_SHUTDOWN = 7,
  AM_CURE = 8,
  AM_END = ENUM_UNDEFINED
} AUXPROG_MODE;

typedef struct
{
  int   Timer;
  EQUIPMENT_IO Output;
  EQUIPMENT_IO HeadOutput;
  int   Alarm;
  uint8_t *RemoteOff;
  char  Control;
  int   PIDValue;
} HUMIDITY_CTRL;

typedef struct
{
  float CurError;
  float PrevError;
  float IntError;
  int   RangeMin;
  int   RangeMax;
  int   Kp;
  int   Ki;
  int   Kd;
  unsigned int Timer;
} PID_CTRL;

typedef enum PidEquip
{
  PID_DOOR,
  PID_REFRIGERATION,
  PID_CLIMACELL,
  PID_HUMIDIFIER,
  PID_BURNER,
  NUM_PID_EQUIP
} PID_EQ;

/*** external variables ***/

extern PID_CTRL  PIDCtrl[NUM_PID_EQUIP];

/*** external functions ***/

extern int AdjustDoors(int PWM);
extern void FanBoostOff(void);
extern int ClimacellClockMode(void);

extern void CtrlAux(void);
extern void CtrlBayLights(void);
extern void CtrlBurner(int Output);
extern void CtrlCavityHeat(void);
extern void CtrlClimacell(int EquipType, int SysMode);
extern void CtrlClimacellOff(void);
extern void CtrlDoors(float Actual, float Target);
extern void CtrlDoorsClose(void);
extern void CtrlDoorsPulsed_Init(void);
extern void CtrlFan(int speed);
extern void CtrlFanBoost(void);
extern void CtrlHeat(void);
extern void CtrlHumidifier(int EquipType, int SysMode);
extern void CtrlHumidifiersOff(void);
extern void CtrlOutsideAirBlend(int CureMode);
extern void CtrlPurge(void);
extern void CtrlRefrig(float ActualTemp, float TargetTemp);
extern void CtrlRefrigDiag(void);
extern void CtrlRefrigDiagClear(void);
extern void CtrlRefrigOff(bool);

extern void PurgeOff(void);

extern void ApplyManualOverrides(void);

extern int PWMValToPercent(unsigned int PWM);
extern unsigned int PercentToPWMVal(int percent);

extern void PIDClear(PID_EQ pidIndex);
extern void PID_Init(void);

#endif

/***   End Of File   ***/
