/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     States.c

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:  Determine the system state and call the appropriate mode function

COMMENTS:

***************************************************************************/

/*** include files ***/

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Analog_Input.h"
#include "Controls.h"
#include "Failures.h"
#include "Modes.h"
#include "PWM.h"
#include "RTC.h"
#include "SerialShift.h"
#include "Settings.h"
#include "States.h"
#include "Timer.h"
#include "UI_Messages.h"
#include "UserLogs.h"
#include "Warnings.h"

/*** defines ***/

/*** typedefs and structures ***/

/*** module variables ***/

RAMP_RATE Ramp;

char CurrentMode = 0;
char PrevMode = 0;
char SystemState = ST_UNDEFINED;
char CureState = CS_OFF;

static char PrevDoorSwitchManual = 0;
static char PrevCureSwitchAuto = 255;
static char PrevCureRemoteOff = 0;

static PWM_OUTPUT_STATE OldRefrigOutput;

/*** static functions ***/

static void CheckCureSwitchStatus(void);
static void CheckDoorStatus(void);
static void CheckOutsideTempSensorStatus(int PrevState, int RunClock);
static void CheckPlenumSensorStatus(int RunClock);
static int CheckStartSwitchStatus(unsigned int Counter);
static int CheckSystemStatus(int RunClock, unsigned int Counter);
static void RefrigRunTimer(void);
static void SetDehumidification(int PrevState);
static void SetLightStatus(int light, int status);
static void SetRamp(void);
static int SetStartTemp(void);
static void SetStateCooling(int PrevState, int RunClock, unsigned int Counter);
static void SetStateCure(int PrevState, int RunClock, int CalcOutsideHumid);
static void SetStateDefrost(int PrevState, unsigned int Counter);
static void SetStateHeating(int PrevState, int RunClock);
static void SetStateOnion(int PrevState, int RunClock, unsigned int Counter);
static void SetStatePotato(int PrevState, int RunClock, unsigned int Counter);
static void SetStateRefrig(int RunClock);
static void StartShortCycleTimer(int PrevState, unsigned int Counter);
static float WetBulbDepression(float *DewPoint);

/***************************************************************************

FUNCTION:   CalculatedHumidity()

PURPOSE:    Calculate the relative humidity

COMMENTS:   This is used to calculate what the relative humidity of the outside
            air will be after it is warmed or cooled to the plenum temperature.

            8/11/08
            tested - see Gellert Psychrometric Calculation.doc

***************************************************************************/
int CalculatedHumidity(void)
{
  float a = -4.9283;
  float b = -2937.4;
  float d = 273.0;
  float Tdp = 0.0;

  if (   Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_OUTSIDE_HUMID].Disabled == 1
      || Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_OUTSIDE_HUMID].Value == SENSOR_VAL_UNDEFINED
      || Settings.SystemMode == SM_POTATO)
    return SENSOR_VAL_UNDEFINED;

  // temperature
  float T = PlenumTempAvg;
  if (Settings.TempType == 0)  // if F convert to C
    T = (T - 32.0)/1.8;

  // get the dew point temp from the wet bulb calculation
  (void)WetBulbDepression(&Tdp);

  // calculate the relative humidity
  float sub1 = pow(((Tdp + d)/(T + d)), a);
  float sub2 = pow(10, (b * ((1.0/(Tdp + d)) - (1.0/(T + d)))));
  float U = (sub1 * sub2) * 100.0;

  // don't allow the result to be greater than 100%
  if (U > 100)
    U = 100;

  return U;
} // end CalculatedHumidity()

/***************************************************************************

FUNCTION:   CheckCureSwitchStatus()

PURPOSE:    Check if the cure switch or the remote off status has changed

COMMENTS:   If the status of the cure switch changes, this functions resends
            the runtime settings (there are a set of runtimes for cure mode
            and a set for the regular mode) to update the UI.

            8/21/08
            tested with the front panel switch and with equip status/remote off

***************************************************************************/
void CheckCureSwitchStatus(void)
{
  if (   CheckInputs(SW_CURE_AUTO) != PrevCureSwitchAuto
      || Settings.RemoteOff[RO_CURE] != PrevCureRemoteOff)
  {
    UI_SendRuntimes(MSG_QUEUE);
    PrevCureSwitchAuto = CheckInputs(SW_CURE_AUTO);
    PrevCureRemoteOff = Settings.RemoteOff[RO_CURE];
  }
} // end CheckCureSwitchStatus()

/***************************************************************************

FUNCTION:   CheckDoorStatus()

PURPOSE:    Checks if the doors need to be initialized (closed)

COMMENTS:   If the doors switch has been put in manual or off position or if
            the start/stop switch has been put in the stop position, the doors
            need to be closed all the way (initialized) so we know where they
            are.

            3/23/07
            Tested by transitioning switch from off to auto, manual to auto

            7/9/07
            Tested with start/stop switch

***************************************************************************/
void CheckDoorStatus(void)
{
  if (   CheckInputs(SW_FRESHAIR_AUTO)
      && CheckInputs(SW_START_STOP))
  {
    if (PrevDoorSwitchManual == 1)
    {
      CtrlDoorsPulsed_Init();
      PrevDoorSwitchManual = 0;
    }
  }
  else
  {
    PrevDoorSwitchManual = 1;
  }
} // end CheckDoorStatus()

/***************************************************************************

FUNCTION:   CheckOutsideTempSensorStatus()

PURPOSE:    Check the outside temp sensor alarm status

COMMENTS:   Adjusts the system state
            9/9/9
            Tested by unplugging the sensor

***************************************************************************/
void CheckOutsideTempSensorStatus(int PrevState, int RunClock)
{
  if (SystemAlarm[AL_OUTTEMPSENSOR] == 1)
  {
    if (   RunClock == RC_COOLING
        || RunClock == RC_REFRIG
        || RunClock == RC_RECIRC)
    {
      if (PrevState == ST_REFRIG)
        SystemState = ST_REFRIG;
      else if (PrevState == ST_REFRIGDEHUMID)
        SystemState = ST_REFRIGDEHUMID;
      else
        SystemState = ST_RECIRC;
    }
    else
      SystemState = ST_STANDBY;
  }
} // end CheckOutsideTempSensorStatus()

/***************************************************************************

FUNCTION:   CheckPlenumSensorStatus()

PURPOSE:    Check the plenum sensor alarm status

COMMENTS:   Adjusts the system state
            9/9/9
            Tested by adjusting one sensor out of tolerance

***************************************************************************/
void CheckPlenumSensorStatus(int RunClock)
{
  if (SystemAlarm[AL_PLENSENSOR] == 1)
  {
    if (   RunClock == RC_COOLING
        || RunClock == RC_REFRIG
        || RunClock == RC_RECIRC)
    {
      SystemState = ST_RECIRC;
    }
    else
    {
      SystemState = ST_STANDBY;
    }
  }
} // end CheckPlenumSensorStatus()

/***************************************************************************

FUNCTION:   CheckStartSwitchStatus()

PURPOSE:    Check the start/stop switch

COMMENTS:

***************************************************************************/
int CheckStartSwitchStatus(unsigned int Counter)
{
  if (!CheckInputs(SW_START_STOP))
  {
    SystemState = ST_SHUTDOWN;
    ClearAlarms(0);
    ClearIntervalTimers();
    CtrlRefrigDiagClear();

//    // guarantees Timer1_ISR() pulse door handler won't set the outputs
//    // before CtrlDoorsPulsed_Init() gets called
//    PulseDoorFlag = 1;
//    // clears the pulse door outputs so Timer1_ISR() shift register handler
//    // won't turn on the outputs before CtrlDoorsPulsed_Init() gets called
//    OutputOff(DO_PULSEDOOR_CLOSE);
//    OutputOff(DO_PULSEDOOR_OPEN);
    return 0;
  }
  else
    IntervalTimer[IT_SHUTDOWN] = Counter;

  return 1;
} // end CheckStartSwitchStatus()

/***************************************************************************

FUNCTION:   CheckSystemStatus()

PURPOSE:    Check the status of various system elements

COMMENTS:   8/21/08
            tested - start temp, power fail, start/stop switch, stanby,
            remote standby, various system alarms, & manual fan mode

***************************************************************************/
int CheckSystemStatus(int RunClock, unsigned int Counter)
{
  // check to see if fresh air doors need to be initialized (closed)
  CheckDoorStatus();

  // check the cure switch status (to update the UI run clock)
  CheckCureSwitchStatus();

  // check the status of the start/stop switch
  if (CheckStartSwitchStatus(Counter) == 0)
  {
    return 0;
  }

  // check for power failure
  if (!CheckInputs(EQ_POWER))
  {
    SystemState = ST_FAILURE;
    WarningsSet(WARN_POWER, FM_ALARM, FM_ALARM, NA);
    return 0;
  }

  // check if the fan switch is in manual
  if (CheckInputs(SW_FAN_MANUAL))
  {
    SystemState = ST_FAN_MANUAL;
    return 0;
  }

  // check if the fan switch is off
  if (!CheckInputs(SW_FAN_AUTO) && !CheckInputs(SW_FAN_MANUAL))
  {
    SystemState = ST_FAN_OFF;
    return 0;
  }

  // check if the fan is remote off
  if (Settings.RemoteOff[RO_FAN] == 1)
  {
    SystemState = ST_FAN_REMOTEOFF;
    return 0;
  }

  // check if the system is remote off (remoteStop=Stop → value 3)
  if (Settings.RemoteOff[RO_FAN] == 3)
  {
    SystemState = ST_SYSTEM_REMOTEOFF;
    return 0;
  }

  // check if the system should be in standby
  if (RunClock == RC_STANDBY)
  {
    SystemState = ST_STANDBY;
    return 0;
  }

  // check if the system is in remote standby
  if (!CheckInputs(EQ_REMOTE_STANDBY))
  {
    SystemState = ST_REMOTE_STANDBY;
    WarningsSet(WARN_REMOTESTANDBY, FM_ALARM, FM_ALARM, NA);
    return 0;
  }

  // check for a valid plenum temperature
  if (   Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Present == 0
      || PlenumTempAvg == SENSOR_VAL_UNDEFINED)
  {
    SystemState = ST_FAILURE;
    WarningsSet(WARN_NO_PLENTEMP, FM_ALARM, FM_ALARM, NA);
    return 0;
  }

  // get the start temp
  if (SetStartTemp() == 0)
  {
    SystemState = ST_FAILURE;
    WarningsSet(WARN_NO_STARTTEMP, FM_ALARM, FM_ALARM, NA);
    return 0;
  }

  // check for system failures
  if (SystemFailuresChk() != 0)
  {
    SystemState = ST_FAILURE;
    return 0;
  }

  // check for IO configuration errors
//  if (WarningStatus(WARN_SYSCONFIG_EQ) != 0)
//  {
//    SystemState = ST_FAILURE;
//    return 0;
//  }

  return 1;
} // end CheckSystemStatus()

/***************************************************************************

FUNCTION:   FanRunTimer()

PURPOSE:    Accumulate the fan run times - daily and total

COMMENTS:   3/23/07
            Tested with a combination of start/stop switch off, fan switch off,
            fan output off, and failure.  Tested daily run time reset at
            11:59am and 11:59pm

***************************************************************************/
void FanRunTimer(void)
{
  char Hour = 255;
  char Min = 255;
  char Sec = 255;
  char DateStr[DATE_LEN+1];
  unsigned int DayValue;
  unsigned int Counter = XTimerVal;

  if (   CheckInputs(SW_START_STOP)
      && FanOn())
  {
    Settings.Fan.DailyRunTime += Counter - IntervalTimer[IT_FANDAILYCOUNTER];
    Settings.Fan.TotalRunTime += Counter - IntervalTimer[IT_FANTOTALCOUNTER];
    IntervalTimer[IT_FANCONTINUOUSTIME] += Counter - IntervalTimer[IT_FANCONTCOUNTER];

    if (   SystemState == ST_REFRIG
        || SystemState == ST_REFRIGDEHUMID)
    {
      Settings.Fan.TotalRefrigTime += Counter - IntervalTimer[IT_FANREFRIGCOUNTER];
      IntervalTimer[IT_RAMPRUNTIME] += Counter - IntervalTimer[IT_RAMPCOUNTER];
    }
    else if (   SystemState == ST_COOLING
             || SystemState == ST_COOLDEHUMID)
    {
      Settings.Fan.TotalCoolingTime += Counter - IntervalTimer[IT_FANCOOLINGCOUNTER];
      IntervalTimer[IT_RAMPRUNTIME] += Counter - IntervalTimer[IT_RAMPCOUNTER];
    }
    else if (SystemState == ST_RECIRC)
    {
      Settings.Fan.TotalRecircTime += Counter - IntervalTimer[IT_FANRECIRCCOUNTER];
    }
    else if (   SystemState == ST_AIRCURE
             || SystemState == ST_BURNERCURE)
    {
      Settings.Fan.TotalCureTime += Counter - IntervalTimer[IT_FANCURECOUNTER];
    }
  }
  else
  {
    IntervalTimer[IT_FANCONTINUOUSTIME] = 0;
  }

  if (SystemState == ST_STANDBY)
  {
    Settings.Fan.TotalStandbyTime += Counter - IntervalTimer[IT_FANSTANDBYCOUNTER];
  }

  IntervalTimer[IT_FANDAILYCOUNTER] = Counter;
  IntervalTimer[IT_FANTOTALCOUNTER] = Counter;
  IntervalTimer[IT_FANCONTCOUNTER] = Counter;
  IntervalTimer[IT_FANREFRIGCOUNTER] = Counter;
  IntervalTimer[IT_FANCOOLINGCOUNTER] = Counter;
  IntervalTimer[IT_FANRECIRCCOUNTER] = Counter;
  IntervalTimer[IT_FANCURECOUNTER] = Counter;
  IntervalTimer[IT_FANSTANDBYCOUNTER] = Counter;
  IntervalTimer[IT_RAMPCOUNTER] = Counter;

  // continuous fan runtime is used to initiate a runtime based fan boost cycle
  // consequently, we only accumulate fan runtime while we're in cooling, recirc,
  // or heating modes and the fan output is less than 80%
  if (   PWMValToPercent(PwmChannel[PWM_FAN].Output) > CONTINUOUSFAN_THRESHOLD
      || (   SystemState != ST_COOLING
          && SystemState != ST_RECIRC
          && SystemState != ST_HEATING) )
  {
    IntervalTimer[IT_FANCONTINUOUSTIME] = 0;
  }

  RefrigRunTimer();

  // reset the daily fan run timer at noon
  if (   GetDateStr(DateStr) == 1
      && GetTime(&Hour, &Min, &Sec) == 1)
  {
    DayValue = DateToInt(DateStr);
    if (   DayValue != Settings.Fan.DailyDay
        && Hour >= 12)
    {
      Settings.Fan.DailyRunTime = 0;
      Settings.Fan.DailyDay = DayValue;
      RequestSettingsSave(SR_REQUEST, ACTIVE_SETTINGS);
    }
  }
} // end FanRunTimer()

/***************************************************************************

FUNCTION:   RefrigRunTimer()

PURPOSE:    Accumulate the refrigeratio run time

COMMENTS:   The refrig run time is used to determine when to defrost.

***************************************************************************/
void RefrigRunTimer(void)
{
  unsigned int Counter = XTimerVal;

  if (  (CheckInputs(SW_FAN_AUTO) && CheckOutputs(EQ_FAN))
      && CheckInputs(SW_REFRIG_AUTO)
      && PwmChannel[PWM_REFRIGERATION].Output > PWM_MIN_VALUE)
  {
    // if the counter has been initialized (not 0 (reset)), start accumulating the run time
    if (IntervalTimer[IT_REFRIGCOUNTER] != 0)
      IntervalTimer[IT_REFRIGRUNTIME] += Counter - IntervalTimer[IT_REFRIGCOUNTER];
  }
  else
    IntervalTimer[IT_REFRIGRUNTIME] = 0;

  IntervalTimer[IT_REFRIGCOUNTER] = Counter;
} // end RefrigRunTimer()

/***************************************************************************

FUNCTION:   RunClockMode()

PURPOSE:    Determine the programmed mode of the system from the run clock

COMMENTS:   3/23/07
            Tested with 12:07am, 12:29am, 12:30am, 12:00am, 12:29pm, 12:30pm,
            11:29pm, 11:30pm, 11:59pm

***************************************************************************/
int RunClockMode(void)
{
  int  i    = 0;
  char Hour = 0;
  char Min  = 0;
  char Sec  = 0;
  char *RunTimes;

  if (   Settings.SystemMode == SM_ONION
      && CheckInputs(SW_CURE_AUTO)
      && Settings.RemoteOff[RO_CURE] != 1)  // 0=auto or 2=manual
  {
    RunTimes = Settings.Cure.RunTimes;
  }
  else
  {
    RunTimes = Settings.RunTimes;
  }

  GetTime(&Hour, &Min, &Sec);
  i = (Hour * 2);
  if (Min >= 30)
  {
    i++;
  }

  return(RunTimes[i]);
} // end RunClockMode()

/***************************************************************************

FUNCTION:   SetDehumidification()

PURPOSE:    Add dehumidification to cooling or refrig mode if necessary

COMMENTS:   9/9/9
            Tested in refrig mode by adjusting the return humidity above the
            plenum humidity setpoint to turn it on and then adjusting the
            return humidity below the setpoint-5% to check the hysteresis
            range before it turned off.
            Also tested that it turns off at 95% refrig

            Tested in cooling mode by adjusting the return humidity above the
            plenum humidity setpoint to turn it on and then adjusting the
            return humidity below the setpoint-5% to check the hysteresis
            range before it turned off.
            Also tested that the outside temperature must remain between
            the refrig low limit temp and the plenum setpoint-4�.

***************************************************************************/
void SetDehumidification(int PrevState)
{
  int ReturnHumid = Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_RETURN_HUMID].Value;

  if (   SystemState == ST_REFRIG
      || SystemState == ST_REFRIGDEHUMID)
  {
    if (   CheckInputs(SW_BURNER_AUTO)
        && Settings.RemoteOff[RO_BURNER] != 1
        && ReturnHumid != SENSOR_VAL_UNDEFINED
        && PWMValToPercent(PwmChannel[PWM_REFRIGERATION].Output) < 90
        && (   ReturnHumid > Settings.Plenum.HumidSet    // get it in
            || (   PrevState == ST_REFRIGDEHUMID    // keep it in
                && ReturnHumid >= (Settings.Plenum.HumidSet * 0.95) ) ) )  // -5%
    {
      SystemState = ST_REFRIGDEHUMID;
    }
  }

  if (   SystemState == ST_COOLING
      || SystemState == ST_COOLDEHUMID)
  {
    if (   CheckInputs(SW_BURNER_AUTO)
        && Settings.RemoteOff[RO_BURNER] != 1
        && ReturnHumid != SENSOR_VAL_UNDEFINED
        && *OutsideTemp != SENSOR_VAL_UNDEFINED
        && *OutsideTemp > Settings.Refrig.Limit
        && *OutsideTemp <= (Settings.Plenum.TempSet - 4)
        && (   ReturnHumid > Settings.Plenum.HumidSet    // get it in
            || (   PrevState == ST_COOLDEHUMID      // keep it in
                && ReturnHumid >= (Settings.Plenum.HumidSet * 0.95) ) ) )  // -5%
    {
      SystemState = ST_COOLDEHUMID;
    }
  }
} // end SetDehumidification()

/***************************************************************************

FUNCTION:   SetLightStatus()

PURPOSE:    Set the light status - on/off/blink

COMMENTS:   3/23/07
            Tested with red on, off, blink and yellow on, off, blink

***************************************************************************/
void SetLightStatus(int light, int status)
{
  if (light == LT_RED)
    LightStatus.Red = status;
  else if (light == LT_YELLOW)
    LightStatus.Yellow = status;
} // end SetLightStatus()

/***************************************************************************

FUNCTION:   SetMode()

PURPOSE:    Controls the system mode

COMMENTS:

***************************************************************************/
void SetMode(void)
{
  int i;

  switch (SystemState)
  {
  case ST_COOLING:
    CurrentMode = UI_COOLING;
    if (Ramp.Status == RAMP_ON)
      CurrentMode = UI_RAMPCOOL;
    ModeCooling(COOL_MODE);
    SetLightStatus(LT_RED, LT_OFF);
    SetLightStatus(LT_YELLOW, LT_OFF);
    break;

  case ST_HEATING:
    CurrentMode = UI_HEATING;
    ModeHeating();
    SetLightStatus(LT_RED, LT_OFF);
    SetLightStatus(LT_YELLOW, LT_OFF);
    break;

  case ST_RECIRC:
    CurrentMode = UI_RECIRC;
    ModeRecirc();
    SetLightStatus(LT_RED, LT_OFF);
    SetLightStatus(LT_YELLOW, LT_ON);
    break;

  case ST_REFRIG:
    CurrentMode = UI_REFRIG;
    if (Ramp.Status == RAMP_ON)
      CurrentMode = UI_RAMPREFRIG;
    ModeRefrig(REFRIG_MODE);
    SetLightStatus(LT_RED, LT_OFF);
    SetLightStatus(LT_YELLOW, LT_ON);
    break;

  case ST_DEFROST:
    CurrentMode = UI_DEFROST;
    ModeRefrig(DEFROST_MODE);
    SetLightStatus(LT_RED, LT_OFF);
    SetLightStatus(LT_YELLOW, LT_OFF);
    break;

  case ST_SHUTDOWN:
    CurrentMode = UI_SHUTDOWN;
    ModeShutdown();
    SetLightStatus(LT_RED, LT_ON);
    SetLightStatus(LT_YELLOW, LT_OFF);
    break;

  case ST_STANDBY:
  case ST_REFRIG_STANDBY:
    CurrentMode = UI_STANDBY;
    ModeStandby();
    SetLightStatus(LT_RED, LT_OFF);
    SetLightStatus(LT_YELLOW, LT_ON);
    break;

  case ST_REMOTE_STANDBY:
    CurrentMode = UI_REMOTE_STANDBY;
    ModeStandby();
    SetLightStatus(LT_RED, LT_ON);
    SetLightStatus(LT_YELLOW, LT_ON);
    break;

  case ST_SYSTEM_REMOTEOFF:
    CurrentMode = UI_SYSTEM_REMOTEOFF;
    ModeShutdown();
    SetLightStatus(LT_RED, LT_ON);
    SetLightStatus(LT_YELLOW, LT_OFF);
    break;

  case ST_FAN_MANUAL:
    CurrentMode = UI_FAN_MANUAL;
    ModeFanManual();
    SetLightStatus(LT_RED, LT_ON);
    SetLightStatus(LT_YELLOW, LT_ON);
    break;

  case ST_FAN_OFF:
    CurrentMode = UI_FAN_OFF;
    ModeFanOff();
    SetLightStatus(LT_RED, LT_OFF);
    SetLightStatus(LT_YELLOW, LT_ON);
    break;

  case ST_FAN_REMOTEOFF:
    CurrentMode = UI_FAN_REMOTEOFF;
    ModeFanOff();
    SetLightStatus(LT_RED, LT_ON);
    SetLightStatus(LT_YELLOW, LT_ON);
    break;

  case ST_REFRIG_REMOTEOFF:
    CurrentMode = UI_REFRIG_REMOTEOFF;
    ModeStandby();
    SetLightStatus(LT_RED, LT_ON);
    SetLightStatus(LT_YELLOW, LT_ON);
    break;

  case ST_AIRCURE:   // onion
    CurrentMode = UI_AIRCURE;
    ModeAirCure();
    SetLightStatus(LT_RED, LT_OFF);
    SetLightStatus(LT_YELLOW, LT_OFF);
    break;

  case ST_BURNERCURE:   // onion
    CurrentMode = UI_BURNERCURE;
    ModeBurnerCure();
    SetLightStatus(LT_RED, LT_OFF);
    SetLightStatus(LT_YELLOW, LT_OFF);
    break;

  case ST_COOLDEHUMID:   // onion
    CurrentMode = UI_COOLDEHUMID;
    if (Ramp.Status == RAMP_ON)
      CurrentMode = UI_RAMPCOOL;
    ModeCooling(DEHUMID_MODE);
    SetLightStatus(LT_RED, LT_OFF);
    SetLightStatus(LT_YELLOW, LT_OFF);
    break;

  case ST_REFRIGDEHUMID:   // onion
    CurrentMode = UI_REFRIGDEHUMID;
    if (Ramp.Status == RAMP_ON)
      CurrentMode = UI_RAMPREFRIG;
    ModeRefrig(DEHUMID_MODE);
    SetLightStatus(LT_RED, LT_OFF);
    SetLightStatus(LT_YELLOW, LT_ON);
    break;

//  case ST_AIRFLOWFAIL:
//  case ST_AUXLOWPLENTEMPFAIL:
//    CurrentMode = UI_FAILURE;
//    ModeHardFailure();
//    SetLightStatus(LT_RED, LT_ON);
//    SetLightStatus(LT_YELLOW, LT_ON);
//    break;

//  case ST_AUXFAIL:    // onion Aux1
//  case ST_CAVITYHEATFAIL:
//  case ST_CLIMACELLFAIL:
//  case ST_FANFAIL:
//  case ST_HEATFAIL:   // onion Aux2
//  case ST_HIGHCO2FAIL:
//  case ST_HIGHPLENTEMPFAIL:
//  case ST_HUMIDFAIL:
//  case ST_LOWPLENTEMPFAIL:
//  case ST_NO_PLENTEMP:
//  case ST_NO_STARTTEMP:
//  case ST_OUTHUMIDSENSORFAIL:
//  case ST_OUTHUMIDVARFAIL:
//  case ST_OUTTEMPSENSORFAIL:
//  case ST_PLENHUMIDFAIL:
//  case ST_PLENSENSORFAIL:
//  case ST_POWERFAIL:
//  case ST_REFRIGFAIL:
//  case ST_BURNERFAIL:   // onion
//  case ST_SYSCONFIGFAIL:
  case ST_FAILURE:
    CurrentMode = UI_FAILURE;
    ModeStandby();
    SetLightStatus(LT_RED, LT_ON);
    SetLightStatus(LT_YELLOW, LT_ON);
    break;

  case ST_UNDEFINED:
  default:
    CurrentMode = UI_STANDBY;
    ModeStandby();
    SetLightStatus(LT_RED, LT_OFF);
    SetLightStatus(LT_YELLOW, LT_ON);
    break;
  }

  // run refrig stages in diagnostic mode if necessary
  CtrlRefrigDiag();

  // Apply software manual overrides (replaces CPLD panel switches).
  // Manual forces outputs ON in any mode except E-Stop shutdown and system remote off.
  if (SystemState != ST_SHUTDOWN && SystemState != ST_SYSTEM_REMOTEOFF)
    ApplyManualOverrides();

  // check for purge
  if (Settings.Co2.Purge.Start != 0)
    CurrentMode = UI_PURGE;

  // check for fan boost
  if (IntervalTimer[IT_FANBOOSTCYCLE] != 0)
    CurrentMode = UI_FANBOOST;

  // check for mode changes (don't send alarm for purge or fan boost cycles)
  if (   CurrentMode != UI_PURGE
      && CurrentMode != UI_FANBOOST)
  {
    if (PrevMode != CurrentMode)
    {
      WarningsSet(WARN_MODECHANGE, FM_ALARM, CurrentMode, NA);
      PrevMode = CurrentMode;
    }
  }

  // check for alarms
  for (i = 0; i < NUM_ALARMS; i++)
  {
    if (SystemAlarm[i] == 1)
    {
      SetLightStatus(LT_RED, LT_BLINK);
      break;
    }
  }
} // end SetMode()

/***************************************************************************

FUNCTION:   SetRamp()

PURPOSE:    Controls temperature ramping mode

COMMENTS:   3/23/07
            Tested manual up & down, auto down, auto up terminates and returns

***************************************************************************/
void SetRamp(void)
{
  int Board;
  int Sensor;

  if (   Settings.Ramp.TargetTemp != Settings.Plenum.TempSet
      && Settings.Ramp.UpdatePeriod != 0
      && Settings.Ramp.UpdateTemp != 0)
  {
    if (Ramp.Status == RAMP_OFF)
    {
      Ramp.Status = RAMP_ON;
      IntervalTimer[IT_RAMPRUNTIME] = 0;
    }

    if (Settings.Ramp.UpdatePeriod== 255)   // automatic mode
    {
      // ramping up in auto mode is not allowed - the UI traps it, but just in case
      if (Settings.Ramp.TargetTemp > Settings.Plenum.TempSet)
      {
        Ramp.Status = RAMP_OFF;
        Settings.Ramp.TargetTemp = Settings.Plenum.TempSet;
        return;
      }

      if (Settings.Ramp.TempRef == 255)   // return air temp
        Ramp.Ref = Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Sensor[SENSOR_RETURN_TEMP].Value;
      else
      {
        // RampTempRef is a sensor ID
        Board = Settings.Ramp.TempRef/ANALOG_SENSORS_PER_BOARD;
        Sensor = Settings.Ramp.TempRef % ANALOG_SENSORS_PER_BOARD;
        Ramp.Ref = Settings.AnalogBoard[Board].Sensor[Sensor].Value;
      }

      if (   Ramp.Ref != SENSOR_VAL_UNDEFINED
          && Ramp.Ref - Settings.Plenum.TempSet <= Settings.Ramp.TempDiff)
      {
        Settings.Plenum.TempSet -= Settings.Ramp.UpdateTemp;
        if (Settings.Plenum.TempSet < Settings.Ramp.TargetTemp)
          Settings.Plenum.TempSet = Settings.Ramp.TargetTemp;
      }
    }
    else
    {
      // if the step interval is over, start a new one
      if (IntervalTimer[IT_RAMPRUNTIME] >= Settings.Ramp.UpdatePeriod* T_HOURS)
      {
        if (Settings.Ramp.TargetTemp > Settings.Plenum.TempSet)
        {
          Settings.Plenum.TempSet += Settings.Ramp.UpdateTemp;
          if (Settings.Plenum.TempSet > Settings.Ramp.TargetTemp)
            Settings.Plenum.TempSet = Settings.Ramp.TargetTemp;
        }
        else
        {
          Settings.Plenum.TempSet -= Settings.Ramp.UpdateTemp;
          if (Settings.Plenum.TempSet < Settings.Ramp.TargetTemp)
            Settings.Plenum.TempSet = Settings.Ramp.TargetTemp;
        }

        // reset the timer
        IntervalTimer[IT_RAMPRUNTIME] = 0;
      }
    }
  }
  else
  {
    IntervalTimer[IT_RAMPRUNTIME] = 0;
    Ramp.Status = RAMP_OFF;
  }
} // end SetRamp()

/***************************************************************************

FUNCTION:   SetStartTemp()

PURPOSE:    Determine the start temperature (cooling available temp)

COMMENTS:   The start temperature determines whether the system can run in
            cooling mode (using outside air for cooling) or must use
            refrigeration.

            3/23/07
            Tested with references of plenum setpoint, return air temp, and
            pile temp.  Increased pile temp - start temp increased accordingly
            removed sensor - gives warning
            climacell alarm, switch not auto, remote off - no wet bulb calc

***************************************************************************/
int SetStartTemp(void)
{
  int Board;
  int Sensor;
  float Unused;
  float RefTemp = SENSOR_VAL_UNDEFINED;
  float WbDepression = 0.0;
  float TempDiff = Settings.OutsideAir.Diff;

  if (Settings.OutsideAir.AboveBelow == 1)  // below
  {
    TempDiff *= -1;
  }

  if (Settings.OutsideAir.TempRef == 255)   // plenum setpoint
  {
    RefTemp = Settings.Plenum.TempSet;
  }
  else if (Settings.OutsideAir.TempRef == 254)  // return air temp
  {
    RefTemp = Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Sensor[SENSOR_RETURN_TEMP].Value;
  }
  else
  {
    // OutAirTempRef is a sensor ID
    Board = Settings.OutsideAir.TempRef/ANALOG_SENSORS_PER_BOARD;
    Sensor = Settings.OutsideAir.TempRef % ANALOG_SENSORS_PER_BOARD;
    RefTemp = Settings.AnalogBoard[Board].Sensor[Sensor].Value;
  }

  if (RefTemp == SENSOR_VAL_UNDEFINED)
  {
    StartTemp = SENSOR_VAL_UNDEFINED;
    WarningsSet(WARN_STARTTEMP, FM_ALARM, FM_ALARM, NA);
    return 0;
  }
  else
  {
    if (   Settings.SystemMode == SM_POTATO
        && (CheckInputs(SW_CLIMACELL_AUTO) || CheckInputs(SW_CLIMACELL_MANUAL))
        && ClimacellClockMode() != CC_OFF
        && Settings.RemoteOff[RO_CLIMACELL] != 1
        && SystemAlarm[AL_CLIMACELL] == 0)
    {
      WbDepression = WetBulbDepression(&Unused);
      StartTemp = RefTemp + TempDiff + (WbDepression * (Settings.Climacell.Efficiency/100.0));
    }
    else
    {
      StartTemp = RefTemp + TempDiff;
    }
  }
  return 1;
} // end SetStartTemp()

/***************************************************************************

FUNCTION:   SetStateCooling()

PURPOSE:    Try to set the system state to cooling

COMMENTS:   Extracted from SetSystemState()
            8/21/08
            tested in potato & onion modes
            9/9/9
            added ST_COOLDEHUMID state and retested

***************************************************************************/
void SetStateCooling(int PrevState, int RunClock, unsigned int Counter)
{
  int mode = Settings.OutsideAir.CtrlMode;
  float TempDiff = Settings.OutsideAir.Diff;

  if (Settings.OutsideAir.AboveBelow == 1)  // below
    TempDiff *= -1;

  if (   (   RunClock == RC_COOLING
          || RunClock == RC_RECIRC
          ||(RunClock == RC_REFRIG && Settings.Refrig.Mode == 0))
      && CheckInputs(SW_FAN_AUTO)
      && CheckInputs(SW_FRESHAIR_AUTO)
      && (  (mode == OSA_CTRL_OUTSIDE && *OutsideTemp != SENSOR_VAL_UNDEFINED)
          ||(mode == OSA_CTRL_PLENUM && PlenumTempAvg != SENSOR_VAL_UNDEFINED) ) )
  {
    if (   PrevState == ST_COOLING
        || PrevState == ST_COOLDEHUMID)
    {
      if (   (  (mode == OSA_CTRL_OUTSIDE && *OutsideTemp > StartTemp)
              ||(mode == OSA_CTRL_PLENUM && PlenumTempAvg - TempDiff > Settings.Plenum.TempSet) )
          && PWMValToPercent(PwmChannel[PWM_DOORS].Output) >= 95)
      {
        IntervalTimer[IT_SHORTCYCLE] = Counter;
      }
      else
      {
        SystemState = ST_COOLING;
        WarningsSet(WARN_REFRIGSTANDBY, FM_NONE, FM_NONE, NA);  // suppress warning if mode is switched to cooling
      }
    }
    else  // not previously in cooling mode
    {
      if (  (mode == OSA_CTRL_OUTSIDE && *OutsideTemp < StartTemp)
          ||(mode == OSA_CTRL_PLENUM && PlenumTempAvg - TempDiff < Settings.Plenum.TempSet) )
      {
        if (   IntervalTimer[IT_SHORTCYCLE] == 0
            ||(Counter - IntervalTimer[IT_SHORTCYCLE] >= Settings.Door.CoolAirCycle * T_MINS))
        {
          SystemState = ST_COOLING;
          WarningsSet(WARN_REFRIGSTANDBY, FM_NONE, FM_NONE, NA);  // suppress warning if mode is switched to cooling
        }
        else
        {
          SystemState = PrevState;
        }
      }
    }
  }
} // end SetStateCooling()

/***************************************************************************

FUNCTION:   SetStateCure()

PURPOSE:    Try to set the system state to cure (air or burner)

COMMENTS:   8/21/08
            tested
            9/9/9
            added 2% hysteresis and retested

***************************************************************************/
void SetStateCure(int PrevState, int RunClock, int CalcOutsideHumid)
{
  float RefHumid = SENSOR_VAL_UNDEFINED;
  int   BurnerAvailable = 0;

  if (   RunClock == RC_CURE
      && CheckInputs(SW_FAN_AUTO))
  {
    if (CheckInputs(SW_BURNER_AUTO) && Settings.Burner.Mode != BURNER_OFF && Settings.RemoteOff[RO_BURNER] != 1)  // burner on
    {
      BurnerAvailable = 1;
    }

    if (Settings.Cure.HumidRef == 0)
    {
      if (!Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_PLENUM_HUMID].Disabled)
      {
        RefHumid = Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_PLENUM_HUMID].Value;
      }
    }
    else
    {
      RefHumid = CalcOutsideHumid;
    }

    if (   (PrevState == ST_AIRCURE)   // keep it in air cure mode
        && (   (   (!BurnerAvailable)
                && (PlenumTempAvg > Settings.Cure.TempLowLimit)
                && (RefHumid < Settings.Cure.HumidHighLimit && RefHumid != SENSOR_VAL_UNDEFINED) )
            || (   (BurnerAvailable)
                && (PlenumTempAvg > Settings.Cure.StartTemp)
                && (RefHumid < Settings.Cure.StartHumid && RefHumid != SENSOR_VAL_UNDEFINED) ) ) )
    {
      SystemState = ST_AIRCURE;
    }
    else if (   (*OutsideTemp > Settings.Cure.StartTemp && *OutsideTemp != SENSOR_VAL_UNDEFINED)   // start air cure mode
             && (PlenumTempAvg > Settings.Cure.TempLowLimit)
             && (RefHumid < Settings.Cure.StartHumid && RefHumid != SENSOR_VAL_UNDEFINED) )
    {
      SystemState = ST_AIRCURE;

      if (PrevState != ST_AIRCURE)
      {
        // arbitrarily set the PID value high enough to keep the doors open when they
        // switch from burner mode where the doors are driven by a percentage instead
        // of the PID controller (IntError is scaled by .01 in the PID controller
        // so 10000 * .01 = 100 -> full output)
        PIDCtrl[PID_DOOR].IntError = 10000;
        CureState = CS_AIR;
      }
    }
    else  // start burner cure
    {
      if (BurnerAvailable)
      {
        SystemState = ST_BURNERCURE;

        if (PrevState != ST_BURNERCURE)
        {
          PIDClear(PID_BURNER);
          CureState = CS_BURNER;
        }
      }
      else
      {
        SystemState = ST_RECIRC;
        CureState = CS_OFF;
      }
    }
  }
} // end SetStateCure()

/***************************************************************************

FUNCTION:   SetStateDefrost()

PURPOSE:    Try to set the system state to defrost

COMMENTS:   Extracted from SetSystemState()
            8/21/08
            tested in potato & onion modes
            9/9/9
            added ST_REFRIGDEHUMID state and retested

***************************************************************************/
void SetStateDefrost(int PrevState, unsigned int Counter)
{
  if (  (   SystemState == ST_REFRIG
         || SystemState == ST_REFRIGDEHUMID)
      && Settings.Refrig.DefrostPeriod > 0)
  {
    if (   IntervalTimer[IT_REFRIGRUNTIME] >= Settings.Refrig.DefrostPeriod * T_HOURS   // get it in
        || PrevState == ST_DEFROST)                                           // keep it in
    {
      if (   IntervalTimer[IT_DEFROSTCYCLE] == 0
          || Counter - IntervalTimer[IT_DEFROSTCYCLE] < Settings.Refrig.DefrostDuration * T_MINS)
      {
        SystemState = ST_DEFROST;
        if (IntervalTimer[IT_DEFROSTCYCLE] == 0)
        {
          IntervalTimer[IT_DEFROSTCYCLE] = Counter;

          // save refrig output state
          OldRefrigOutput.PWMValue  = PwmChannel[PWM_REFRIGERATION].Output;
          OldRefrigOutput.CurError  = PIDCtrl[PID_REFRIGERATION].CurError;
          OldRefrigOutput.PrevError = PIDCtrl[PID_REFRIGERATION].PrevError;
          OldRefrigOutput.IntError  = PIDCtrl[PID_REFRIGERATION].IntError;
        }
      }
      else
      {
        IntervalTimer[IT_REFRIGRUNTIME] = 0;
        IntervalTimer[IT_DEFROSTCYCLE] = 0;

        // restart refrig at previous output level
        PwmChannel[PWM_REFRIGERATION].Output = OldRefrigOutput.PWMValue;
        PIDCtrl[PID_REFRIGERATION].CurError  = OldRefrigOutput.CurError;
        PIDCtrl[PID_REFRIGERATION].PrevError = OldRefrigOutput.PrevError;
        PIDCtrl[PID_REFRIGERATION].IntError  = OldRefrigOutput.IntError;
      }
    }
  }
} // end SetStateDefrost()

/***************************************************************************

FUNCTION:   SetStateHeating()

PURPOSE:    Try to set the system state to heating

COMMENTS:   Extracted from SetSystemState()
            8/21/08
            tested

***************************************************************************/
void SetStateHeating(int PrevState, int RunClock)
{
  if (   Settings.EquipIo[EQ_HEAT].Enabled == 1
      && Settings.RemoteOff[RO_HEAT] != 1
      && CheckInputs(SW_FAN_AUTO)
      && (   RunClock == RC_COOLING
          || RunClock == RC_RECIRC
          || RunClock == RC_REFRIG)
      && (*OutsideTemp < StartTemp && *OutsideTemp != SENSOR_VAL_UNDEFINED)
      && (   PlenumTempAvg < Settings.Plenum.TempSet - Settings.HeatTempThresh   // to start it
          ||(   PrevState == ST_HEATING
             && PlenumTempAvg < Settings.Plenum.TempSet))) // to keep it going
  {
    if (PrevState != ST_HEATING) {
        IntervalTimer[IT_FANSPEEDUPD] = 0;
    }
    SystemState = ST_HEATING;
  }
} // end SetStateHeating()

/***************************************************************************

FUNCTION:   SetStateOnion()

PURPOSE:    Set the system state in onion mode

COMMENTS:   8/21/08
            tested - air cure, burner cure, refrig, cooling, dehumid modes
            9/9/9
            broke out SetDehumidification and retested

***************************************************************************/
void SetStateOnion(int PrevState, int RunClock, unsigned int Counter)
{
  int CalcOutsideHumid = CalculatedHumidity();

  if (CheckInputs(SW_CURE_AUTO) && Settings.RemoteOff[RO_CURE] != 1)
  {
    SetStateCure(PrevState, RunClock, CalcOutsideHumid);
  }
  else
  {
    // refrig mode
    if (   RunClock == RC_REFRIG
        || RunClock == RC_RECIRC)
    {
      SystemState = ST_RECIRC;

      if (*OutsideTemp > Settings.Refrig.Limit || *OutsideTemp == SENSOR_VAL_UNDEFINED)
      {
        SetStateRefrig(RunClock);
      }
    }

    // cooling mode (this will change the state from REFRIG to COOLING if it can)
    if (CalcOutsideHumid <= Settings.OutsideAir.CalcHumidMax && CalcOutsideHumid != SENSOR_VAL_UNDEFINED)
      SetStateCooling(PrevState, RunClock, Counter);
  }

  // check if dehumidification is necessary
  SetDehumidification(PrevState);
} // end SetStateOnion()

/***************************************************************************

FUNCTION:   SetStatePotato()

PURPOSE:    Set the system state in potato mode

COMMENTS:   8/21/08
            tested - heating, refrig, cooling modes

***************************************************************************/
void SetStatePotato(int PrevState, int RunClock, unsigned int Counter)
{
  // refrig mode
  SetStateRefrig(RunClock);

  // cooling mode (this will change the state from REFRIG to COOLING if it can)
  SetStateCooling(PrevState, RunClock, Counter);

  // heating mode
  SetStateHeating(PrevState, RunClock);
} // end SetStatePotato()

/***************************************************************************

FUNCTION:   SetStateRefrig()

PURPOSE:    Try to set the system state to refrigeration

COMMENTS:   Extracted from SetSystemState()
            8/21/08
            tested in potato & onion modes

***************************************************************************/
void SetStateRefrig(int RunClock)
{
  if (   RunClock == RC_REFRIG
      && CheckInputs(SW_REFRIG_AUTO)
      && CheckInputs(SW_FAN_AUTO))
  {
    if (Settings.RemoteOff[RO_REFRIGERATION] == 1)
    {
      SystemState = ST_REFRIG_REMOTEOFF;
      return;
    }

    if (!CheckInputs(EQ_REFRIG_STANDBY))
    {
      WarningsSet(WARN_REFRIGSTANDBY, FM_ALARM, FM_ALARM, NA);
      SystemState = ST_REFRIG_STANDBY;
    }
    else
    {
      if (SystemAlarm[AL_REFRIGERATION] == FM_ALARM)
      {
        if (Settings.Refrig.FailMode == 1)   // standby
        {
          SystemState = ST_STANDBY;
        }
        else if (Settings.Refrig.FailMode == 2) // refrigeration
        {
          SystemState = ST_REFRIG;
        }
        else
        {
          SystemState = ST_RECIRC;
        }
      }
      else
      {
        SystemState = ST_REFRIG;
      }
    }
  }
} // end SetStateRefrig()

/***************************************************************************

FUNCTION:   SetSystemState()

PURPOSE:    Determines the state/mode the system should be in

COMMENTS:   3/23/07
            Tested no start temp, power fail, start/stop stop, run clock standby,
            remote standby, removed plenum temp sensors - all go to standby

            heating - check threshhold to start and remains on without threshhold
            fan switch not auto, heat switch not auto, heat remote off

            refrig - refrig standby switch, refrig switch auto, refrig remote off
            doesn't switch to cooling if outside temp too high or refrig only mode
            tested refrig fail mode with the alarm testing

            cooling - doors switch not auto, short cycle timer
            retested 10/3/07 - doors switch in auto not necessary anymore

            defrost - interval & duration, restart interval when refrig restarts

            recirc - fan switch in auto, outside temp < start temp => cooling

            plenum sensor alarm => recirc
            outside air sensor alarm, in refrig => refrig, cooling => recirc

            8/21/08
            broke into smaller functions and retested in potato & onion modes

            9/9/9
            broke out CheckOutsideTempSensorStatus, CheckPlenumSensorStatus,
            StartShortCycleTimer and retested

***************************************************************************/
void SetSystemState(void)
{
  int PrevState = SystemState;
  int RunClock = RunClockMode();
  unsigned int Counter = XTimerVal;

  SystemState = ST_STANDBY;

  if (CheckSystemStatus(RunClock, Counter) == 0)
  {
    return;
  }

  // check for (and set, if necessary) temperature ramping mode
  SetRamp();

  // recirc mode (this state will change if it can)
  if (RunClock == RC_RECIRC
      && CheckInputs(SW_FAN_AUTO))
  {
    SystemState = ST_RECIRC;
  }

  // determine system mode
  if (Settings.SystemMode == SM_POTATO)
  {
    SetStatePotato(PrevState, RunClock, Counter);
  }
  else
  {
    SetStateOnion(PrevState, RunClock, Counter);
  }

  SetStateDefrost(PrevState, Counter);
  CheckOutsideTempSensorStatus(PrevState, RunClock);
  CheckPlenumSensorStatus(RunClock);

  // if necessary, start the cooling mode short cycle timer
  StartShortCycleTimer(PrevState, Counter);

  return;
} // end SetSystemState()

/***************************************************************************

FUNCTION:   StartShortCycleTimer()

PURPOSE:    Starts the cooling mode short cycle timer when appropriate

COMMENTS:   9/9/9
            added ST_COOLDEHUMID and retested

***************************************************************************/
void StartShortCycleTimer(int PrevState, unsigned int Counter)
{
  if (  (   SystemState != ST_COOLING
         && SystemState != ST_COOLDEHUMID)
      &&(   PrevState == ST_COOLING
         || PrevState == ST_COOLDEHUMID) )
  {
    IntervalTimer[IT_SHORTCYCLE] = Counter;
    IntervalTimer[IT_FANSPEEDUPD] = 0;    // resets the fan speed update cycle
  }
} // end StartShortCycleTimer()

/***************************************************************************

FUNCTION:   WetBulbDepression()

PURPOSE:    Calculate the wet bulb depression value

COMMENTS:   Uses an equation provided by Gellert.

            It's used to determine the additional cooling provided by the
            climacell.

***************************************************************************/
float WetBulbDepression(float *DewPoint)
{
  if (   *OutsideTemp == SENSOR_VAL_UNDEFINED
      || Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_OUTSIDE_HUMID].Disabled == 1
      || Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_OUTSIDE_HUMID].Value < 1
      || Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_OUTSIDE_HUMID].Value == SENSOR_VAL_UNDEFINED)
  {
    return 0.0;
  }

  // temperature
  float TempC = *OutsideTemp;
  if (Settings.TempType == 0)  // if F convert to C
  {
    TempC = (TempC - 32.0)/1.8;
  }

  // altitude
  float AltM = Settings.Climacell.Altitude;
  if (Settings.Climacell.AltitudeUnits == 0)  // if feet convert to meters
  {
    AltM *= .3048;
  }

  // barometric pressure
  float BP = (101.3 * pow(((293 - (0.0065 * AltM))/293), 5.26)) * 10;

  // saturated vapor pressure
  float Svp = 6.11 * pow(10.0, ((7.5*TempC)/(237.7+TempC)));

  // actual vapor pressure
  float Avp = Svp * (Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_OUTSIDE_HUMID].Value/100.0);

  // dew point temperature
  float Tdp = (237.7 * log10(Avp/6.112))/(7.5 - log10(Avp/6.112));

  // "A" variable
  float A = (4098.0 * Avp)/pow((Tdp + 237.7), 2);

  // wet bulb temperature
  float Twb = (((0.00066 * BP) * TempC) + (A*Tdp))/((0.00066 * BP) + A);

  // wet bulb depression
  float Dwb = TempC - Twb;
  if (Settings.TempType == 0)  // if F convert back to F
  {
    Dwb = Dwb * 1.8;
  }

  // return dew point temperature
  *DewPoint = Tdp;

  return(Dwb);
} // end WetBulbDepression()

/***   End Of File   ***/
