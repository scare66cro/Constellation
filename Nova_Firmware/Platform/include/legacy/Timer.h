/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     Timer.h

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:

COMMENTS:

***************************************************************************/
#ifndef TIMER_H
#define TIMER_H

/*** include files ***/

/*** defines ***/

// alarms
typedef enum
{
  AL_FAN,
  AL_REFRIGERATION,
  AL_CLIMACELL,
  AL_HEAT,
  AL_CAVITYHEAT,
  AL_BURNER,            // onion
  AL_HUMID1,
  AL_HUMID2,
  AL_HUMID3,
  AL_REFRIG_STAGE1,
  AL_REFRIG_STAGE2,
  AL_REFRIG_STAGE3,
  AL_REFRIG_STAGE4,
  AL_REFRIG_STAGE5,
  AL_REFRIG_STAGE6,
  AL_REFRIG_STAGE7,
  AL_REFRIG_STAGE8,
  AL_REFRIG_DEFROST1,
  AL_REFRIG_DEFROST2,
  AL_LIGHTS1,
  AL_LIGHTS2,
  AL_AUX1,
  AL_AUX2,
  AL_AUX3,
  AL_AUX4,
  AL_AUX5,
  AL_AUX6,
  AL_AUX7,
  AL_AUX8,
  AL_SYSCONFIG,
  AL_AIRFLOW,
  AL_AUXLOWPLENTEMP,
  AL_HIGHCO2,
  AL_HIGHPLENTEMP,
  AL_LOWPLENTEMP,
  AL_NOBROADCAST,
  AL_OUTHUMIDSENSOR,
  AL_OUTHUMIDVAR,
  AL_OUTTEMPSENSOR,
  AL_PLENHUMID,
  AL_PLENSENSOR,
  AL_STATICPRESSUREHIGH,   // newer Mini_IO 2.0.1.b — high static-pressure fan-fail (= index 41)
  NUM_ALARMS
} EQUIP_ALARMS;

// interval timers (IT)
#define IT_CAVITYHEATCYCLE    0
#define IT_CLIMACELLCYCLE     1
#define IT_DEFROSTCYCLE       2
#define IT_FANSPEEDUPD        3
#define IT_HUMID1CYCLE        4
#define IT_HUMID2CYCLE        5
#define IT_REFRIGCOUNTER      6
#define IT_REFRIGDIAG         7
#define IT_REFRIGRUNTIME      8
#define IT_SHORTCYCLE         9
#define IT_SHUTDOWN           10
#define IT_LTXWATCHDOG        11
#define IT_FANBOOSTCYCLE      12
#define IT_FANBOOSTINTERVAL   13
#define IT_FANCONTINUOUSTIME  14
#define IT_FANCONTCOUNTER     15
#define IT_FANREFRIGCOUNTER   16
#define IT_FANCOOLINGCOUNTER  17
#define IT_FANRECIRCCOUNTER   18
#define IT_FANSTANDBYCOUNTER  19
#define IT_FANCURECOUNTER     20
#define IT_FANDAILYCOUNTER    21
#define IT_FANTOTALCOUNTER    22
#define IT_RAMPCOUNTER        23
#define IT_RAMPRUNTIME        24
#define IT_HUMID3CYCLE        25

#define IT_NUM_TIMERS         26

// main timers (MT)
#define MT_SYSTEMLOG          0
#define MT_USERLOG            1
#define MT_READSENSORS        2
#define MT_SENDSENSORS        3
#define MT_SLAVEUPDATE        4
#define MT_SYSTEMSETTINGS     5
#define MT_SYSTEMSTATE        6
#define MT_SYSUPTIME          7
#define MT_UI_ALL             8
#define MT_LOADLOG            9

#define MT_NUM_TIMERS         10

#define T_SECS                1
#define T_MINS                60
#define T_HOURS               3600

#define TC_CLKS_MCK2          0x0
#define TC_CLKS_MCK8          0x1
#define TC_CLKS_MCK32         0x2
#define TC_CLKS_MCK128        0x3
#define TC_CLKS_MCK1024       0x4

// lights/modes
#define LT_BLINK              2
#define LT_OFF                0
#define LT_ON                 1
#define LT_RED                3
#define LT_YELLOW             4

/*** typedefs and structures ***/

typedef struct {
  char         Red;
  char         RedOn;
  char         Yellow;
  char         YellowOn;
} LIGHT_STATUS;

/*** external variables ***/

extern unsigned char SystemAlarm[NUM_ALARMS];
extern unsigned int AlarmTimer[NUM_ALARMS];
extern unsigned int IntervalTimer[IT_NUM_TIMERS];
extern unsigned int MainTimer[MT_NUM_TIMERS];

extern unsigned int XTimerVal;
extern LIGHT_STATUS LightStatus;

/*** external functions ***/

extern void ClearAlarms(char soft);
extern void ClearIntervalTimers(void);
extern void DailyJobs(void);

extern void MainTimer_Init(void);

extern void SerialShiftTimer_Init(void);
extern void WatchdogExternal_Init(void);
extern void WatchdogExternalStart(void);
extern void WatchdogExternalStop(void);
extern void WatchdogInternal_Init(void);
extern void WatchdogInternalReset(void);

extern unsigned int XTimer(void);

#endif

/***   End Of File   ***/
