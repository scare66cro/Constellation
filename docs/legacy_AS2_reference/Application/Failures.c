/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     Failures.c

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:  Check for system alarms and failures

COMMENTS:

***************************************************************************/

/*** include files ***/

#include "Analog_Input.h"
#include "Controls.h"
#include "Failures.h"
#include "PWM.h"
#include "SerialShift.h"
#include "Settings.h"
#include "States.h"
#include "Timer.h"
#include "Warnings.h"

/*** defines ***/

/*** typedefs and structures ***/

/*** module variables ***/

static float HumidVarChkValue = 0;

/*** static functions ***/

static int AirFlowFailChk(void);
static int AuxLowPlenumTempFailChk(void);
static int AuxFailChk(void);
static int BayLightsMonitor(EQUIPMENT_IO io, EQUIP_ALARMS alarm);
static int BurnerFailChk(void);
static int CavFailChk(void);
static int ClimacellFailChk(void);
static int CO2FailChk(void);
static int FanFailChk(void);
static int HeatFailChk(void);
static int HumidFailChk(EQUIPMENT_IO io, EQUIP_ALARMS alarm);
static void MasterBroadcastFailChk(void);
static int OutsideHumidSensorFailChk(void);
static int OutsideHumidVarFailChk(void);
static int OutsideTempSensorFailChk(void);
static int PlenumHumidFailChk(void);
static int PlenumSensorFailChk(void);
static int PlenumTempFailChk(void);
static int PlenumTempFailCureModeHighChk(void);
static int PlenumTempFailCureModeLowChk(void);
static int PlenumTempFailLowChk(void);
static int PlenumTempFailHighChk(void);
static int RefrigFailChk(void);
static int Refrig420Chk(void);
static int RefrigStageChk(int io, int alarm);

/***************************************************************************

FUNCTION:   AirFlowFailChk()

PURPOSE:    Monitor for air flow failure

COMMENTS:

***************************************************************************/
int AirFlowFailChk(void)
{
  if (   CheckInputs(EQ_AIR_FLOW)
      || SystemAlarm[AL_AIRFLOW] == FM_FAIL)  // latch the failure
  {
    SystemAlarm[AL_AIRFLOW] = FM_FAIL;
    WarningsSet(WARN_AIRFLOW, FM_ALARM, FM_FAIL, NA);
//    SystemState = ST_AIRFLOWFAIL;
    return 1;
  }
//  else
//    SystemAlarm[AL_AIRFLOW] = FM_NONE;    // clear the alarm

  return 0;
} // end AirFlowFailChk()

/***************************************************************************

FUNCTION:   AuxFailChk()

PURPOSE:    Monitor for auxiliary system failure

COMMENTS:   3/21/07
            Tested with input failure, UI = none, alarm, fail
            switch not in auto

            Retested in onion mode (aux1) - 7/31/08

            11/6/09
            Tested failure latch

***************************************************************************/
int AuxFailChk(void)
{
  int i;
  int failures = 0;
  unsigned int Counter = XTimerVal;

//  WARNING_ITEMS warn = WARN_AUX;
//
//  // generate different text for this failure in onion mode
//  if (Settings.SystemMode == SM_ONION)
//  {
//    warn = WARN_AUX1;
//  }

  for (i = 0; i < NUM_AUX_OUTPUTS; ++i)
  {
    if (  (   Settings.Failure[FAIL_AUXILIARY].Mode != FM_NONE
           && (CheckOutputs(EQ_AUX1 + i) || SystemAlarm[AL_AUX1 + i] != FM_NONE)
           && CheckInputs(EQ_AUX1 + i))
        || SystemAlarm[AL_AUX1 + i] == FM_FAIL)  // latch the failure
    {
      if (AlarmTimer[AL_AUX1 + i] == 0)
      {
        AlarmTimer[AL_AUX1 + i] = Counter;
      }
      else
      {
        if (Counter - AlarmTimer[AL_AUX1 + i] >= Settings.Failure[FAIL_AUXILIARY].Timer * T_MINS)
        {
          WarningsSet(WARN_AUX, FM_ALARM, Settings.Failure[FAIL_AUXILIARY].Mode, EQ_AUX1 + i);
          SystemAlarm[AL_AUX1 + i] = Settings.Failure[FAIL_AUXILIARY].Mode;

          if (Settings.Failure[FAIL_AUXILIARY].Mode == FM_FAIL)
          {
            failures++;
          }
        }
      }
    }
    else
    {
      SystemAlarm[AL_AUX1 + i] = FM_NONE;    // clear the alarm
      AlarmTimer[AL_AUX1 + i] = 0;     // clear the timer
    }
  }

  return failures;
} // end AuxFailChk()

/***************************************************************************

FUNCTION:   AuxLowPlenumTempFailChk()

PURPOSE:    Monitor for auxiliary low plenum temperature failure

COMMENTS:   This failure can only be reset with the start/stop switch.
            Gellert wants it to require a site visit.

            3/21/07
            Tested with input failure, alarm doesn't clear from alarm window,
            must cycle start/stop switch to clear alarm and then clear alarm
            window to clear the UI warning

            11/6/09
            Tested failure latch

***************************************************************************/
int AuxLowPlenumTempFailChk(void)
{
  if (   CheckInputs(EQ_LOW_TEMP)
      || SystemAlarm[AL_AUXLOWPLENTEMP] == FM_FAIL)
  {
    SystemAlarm[AL_AUXLOWPLENTEMP] = FM_FAIL;
    WarningsSet(WARN_AUXLOWPLENTEMP, FM_ALARM, FM_FAIL, NA);
//    SystemState = ST_AUXLOWPLENTEMPFAIL;
    return 1;
  }

  return 0;
} // end AuxLowPlenumTempFailChk()

/***************************************************************************

FUNCTION:   BayLightsMonitor()

PURPOSE:    Monitor for bay lights left on

COMMENTS:

***************************************************************************/
int BayLightsMonitor(EQUIPMENT_IO io, EQUIP_ALARMS alarm)
{
  unsigned int Counter = XTimerVal;

  if (   Settings.Failure[FAIL_LIGHTS].Mode == FM_ALARM    // failure is not allowed for bay light monitor
      && CheckInputs(io))
  {
    if (AlarmTimer[alarm] == 0)
    {
      AlarmTimer[alarm] = Counter;
    }
    else
    {
      if (Counter - AlarmTimer[alarm] >= Settings.Failure[FAIL_LIGHTS].Timer * Settings.LightsFailUnits * T_MINS)
      {
        WarningsSet(WARN_LIGHTS, FM_ALARM, Settings.Failure[FAIL_LIGHTS].Mode, io);
        SystemAlarm[alarm] = Settings.Failure[FAIL_LIGHTS].Mode;
      }
    }
  }
  else
  {
    SystemAlarm[alarm] = FM_NONE;    // clear the alarm
    AlarmTimer[alarm] = 0;     // clear the timer
  }

  return 0;
} // end BayLightsMonitor()

/***************************************************************************

FUNCTION:   BurnerFailChk()

PURPOSE:    Monitor for burner system failure

COMMENTS:   7/31/08
            Tested with input failure, UI = none, alarm, fail

            11/6/09
            Tested failure latch

***************************************************************************/
int BurnerFailChk(void)
{
  unsigned int Counter = XTimerVal;

  if (  (   Settings.Failure[FAIL_BURNER].Mode != FM_NONE
         && CheckInputs(SW_BURNER_AUTO)
         && (CheckOutputs(EQ_BURNER) || SystemAlarm[AL_BURNER] != FM_NONE)
         && CheckInputs(EQ_BURNER))
      || SystemAlarm[AL_BURNER] == FM_FAIL)  // latch the failure
  {
    if (AlarmTimer[AL_BURNER] == 0)
    {
      AlarmTimer[AL_BURNER] = Counter;
    }
    else
    {
      if (Counter - AlarmTimer[AL_BURNER] >= Settings.Failure[FAIL_BURNER].Timer * T_MINS)
      {
        WarningsSet(WARN_BURNER, FM_ALARM, Settings.Failure[FAIL_BURNER].Mode, NA);
        SystemAlarm[AL_BURNER] = Settings.Failure[FAIL_BURNER].Mode;

        if (Settings.Failure[FAIL_BURNER].Mode == FM_FAIL)
        {
          return 1;
        }
      }
    }
  }
  else
  {
    SystemAlarm[AL_BURNER] = FM_NONE;    // clear the alarm
    AlarmTimer[AL_BURNER] = 0;     // clear the timer
  }

  return 0;
} // end BurnerFailChk()

/***************************************************************************

FUNCTION:   CavFailChk()

PURPOSE:    Monitor for cavity heat failure

COMMENTS:   3/21/07
            Tested with heat on, no failure
            heat on, failure, UI = none, alarm, fail

            11/6/09
            Tested failure latch

***************************************************************************/
int CavFailChk(void)
{
  unsigned int Counter = XTimerVal;

  if (  (  Settings.Failure[FAIL_CAVITY_HEAT].Mode != FM_NONE
         &&(CheckOutputs(EQ_CAVITY_HEAT) || SystemAlarm[AL_CAVITYHEAT] != FM_NONE)
         && CheckInputs(EQ_CAVITY_HEAT))
      || SystemAlarm[AL_CAVITYHEAT] == FM_FAIL)  // latch the failure
  {
    if (AlarmTimer[AL_CAVITYHEAT] == 0)
    {
      AlarmTimer[AL_CAVITYHEAT] = Counter;
    }
    else
    {
      if (Counter - AlarmTimer[AL_CAVITYHEAT] >= Settings.Failure[FAIL_CAVITY_HEAT].Timer * T_MINS)
      {
        WarningsSet(WARN_CAVITYHEAT, FM_ALARM, Settings.Failure[FAIL_CAVITY_HEAT].Mode, NA);
        SystemAlarm[AL_CAVITYHEAT] = Settings.Failure[FAIL_CAVITY_HEAT].Mode;

        if (Settings.Failure[FAIL_CAVITY_HEAT].Mode == FM_FAIL)
        {
          return 1;
        }
      }
    }
  }
  else
  {
    SystemAlarm[AL_CAVITYHEAT] = FM_NONE;    // clear the alarm
    AlarmTimer[AL_CAVITYHEAT] = 0;     // clear the timer
  }

  return 0;
} // end CavFailChk()

/***************************************************************************

FUNCTION:   ClimacellFailChk()

PURPOSE:    Monitor for climacell failure

COMMENTS:   3/21/07
            Tested with climacell on, no failure
            climacell on, failure, UI = none, alarm, fail

            11/6/09
            Tested failure latch

***************************************************************************/
int ClimacellFailChk(void)
{
  unsigned int Counter = XTimerVal;

  if (  (   Settings.Failure[FAIL_CLIMACELL].Mode != FM_NONE
         && CheckInputs(SW_CLIMACELL_AUTO)
         && (CheckOutputs(EQ_CLIMACELL) || SystemAlarm[AL_CLIMACELL] != FM_NONE)
         && CheckInputs(EQ_CLIMACELL))
      || SystemAlarm[AL_CLIMACELL] == FM_FAIL)  // latch the failure
  {
    if (AlarmTimer[AL_CLIMACELL] == 0)
    {
      AlarmTimer[AL_CLIMACELL] = Counter;
    }
    else
    {
      if (Counter - AlarmTimer[AL_CLIMACELL] >= Settings.Failure[FAIL_CLIMACELL].Timer * T_MINS)
      {
        WarningsSet(WARN_CLIMACELL, FM_ALARM, Settings.Failure[FAIL_CLIMACELL].Mode, NA);
        SystemAlarm[AL_CLIMACELL] = Settings.Failure[FAIL_CLIMACELL].Mode;

        if (Settings.Failure[FAIL_CLIMACELL].Mode == FM_FAIL)
        {
//          SystemState = ST_FAILURE; // ST_CLIMACELLFAIL;
          return 1;
        }
      }
    }
  }
  else
  {
    SystemAlarm[AL_CLIMACELL] = FM_NONE;    // clear the alarm
    AlarmTimer[AL_CLIMACELL] = 0;     // clear the timer
  }

  return 0;
} // end ClimacellFailChk()

/***************************************************************************

FUNCTION:   CO2FailChk()

PURPOSE:    Monitor the CO2 level

COMMENTS:   3/21/07
            Tested by changing setpoint to trigger, UI = alarm, fail, none

            11/6/09
            Tested failure latch

***************************************************************************/
int CO2FailChk(void)
{
  unsigned int Counter = XTimerVal;

  if (  (   Settings.Failure[FAIL_HIGH_CO2].Mode != FM_NONE
         && Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Present == 1
         && Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_CO2].Type == ANALOG_SENSOR_TYPE_CO2
         && Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_CO2].Disabled == 0
         && Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_CO2].Value > Settings.Co2.HighFailure)
      || SystemAlarm[AL_HIGHCO2] == FM_FAIL)  // latch the failure
  {
    if (AlarmTimer[AL_HIGHCO2] == 0)
    {
      AlarmTimer[AL_HIGHCO2] = Counter;
    }
    else
    {
      if (Counter - AlarmTimer[AL_HIGHCO2] >= Settings.Failure[FAIL_HIGH_CO2].Timer * T_MINS)
      {
        WarningsSet(WARN_HIGHCO2, FM_ALARM, Settings.Failure[FAIL_HIGH_CO2].Mode, NA);
        SystemAlarm[AL_HIGHCO2] = Settings.Failure[FAIL_HIGH_CO2].Mode;

        if (Settings.Failure[FAIL_HIGH_CO2].Mode == FM_FAIL)
        {
          return 1;
        }
      }
    }
  }
  else
  {
    SystemAlarm[AL_HIGHCO2] = FM_NONE;    // clear the alarm
    AlarmTimer[AL_HIGHCO2] = 0;     // clear the timer
  }

  return 0;
} // end CO2FailChk()

/***************************************************************************

FUNCTION:   FanFailChk()

PURPOSE:    Monitor for a fan failure

COMMENTS:   3/21/07
            Tested with switch not in auto and with failure

            11/6/09
            Tested failure latch

***************************************************************************/
int FanFailChk(void)
{
  unsigned int Counter = XTimerVal;

  if (  (   Settings.Failure[FAIL_FAN].Mode != FM_NONE
         && CheckInputs(SW_FAN_AUTO)
         && (CheckOutputs(EQ_FAN) || SystemAlarm[AL_FAN] != FM_NONE)
         && CheckInputs(EQ_FAN))
      || SystemAlarm[AL_FAN] == FM_FAIL)  // latch the failure
  {
    if (AlarmTimer[AL_FAN] == 0)
    {
      AlarmTimer[AL_FAN] = Counter;
    }
    else
    {
      if (Counter - AlarmTimer[AL_FAN] >= Settings.Failure[FAIL_FAN].Timer * T_MINS)
      {
        SystemAlarm[AL_FAN] = FM_FAIL;
        WarningsSet(WARN_FAN, FM_ALARM, Settings.Failure[FAIL_FAN].Mode, NA);
        return 1;
      }
    }
  }
  else
  {
    SystemAlarm[AL_FAN] = FM_NONE;    // clear the alarm
    AlarmTimer[AL_FAN] = 0;     // clear the timer
  }

  return 0;
} // end FanFailChk()

/***************************************************************************

FUNCTION:   HeatFailChk()

PURPOSE:    Monitor for a heater failure

COMMENTS:   3/21/07
            Tested with switch not in auto, output not on
            heat on, fail, UI = alarm, fail, none

            Retested in onion mode (aux2) - 7/31/08

            11/6/09
            Tested failure latch

***************************************************************************/
int HeatFailChk(void)
{
  unsigned int Counter = XTimerVal;
//  WARNING_ITEMS warn = WARN_HEAT;
//
//  // generate different text for this failure in onion mode
//  if (Settings.SystemMode == SM_ONION)
//    warn = WARN_AUX2;

  if (  (   Settings.Failure[FAIL_HEAT].Mode != FM_NONE
//         && CheckInputs(SW_HEAT_AUTO)
         && (CheckOutputs(EQ_HEAT) || SystemAlarm[AL_HEAT] != FM_NONE)
         && CheckInputs(EQ_HEAT))
      || SystemAlarm[AL_HEAT] == FM_FAIL)  // latch the failure
  {
    if (AlarmTimer[AL_HEAT] == 0)
    {
      AlarmTimer[AL_HEAT] = Counter;
    }
    else
    {
      if (Counter - AlarmTimer[AL_HEAT] >= Settings.Failure[FAIL_HEAT].Timer * T_MINS)
      {
        WarningsSet(WARN_HEAT, FM_ALARM, Settings.Failure[FAIL_HEAT].Mode, NA);
        SystemAlarm[AL_HEAT] = Settings.Failure[FAIL_HEAT].Mode;

        if (Settings.Failure[FAIL_HEAT].Mode == FM_FAIL)
        {
          return 1;
        }
      }
    }
  }
  else
  {
    SystemAlarm[AL_HEAT] = FM_NONE;    // clear the alarm
    AlarmTimer[AL_HEAT] = 0;     // clear the timer
  }

  return 0;
} // end HeatFailChk()

/***************************************************************************

FUNCTION:   HumidFailChk()

PURPOSE:    Monitor for a humidifier failure

COMMENTS:   3/21/07
            Tested with switch not in auto and with a combination of failures
            on 1 & 2 and with the UI = alarm, fail, none

            11/6/09
            Tested failure latch

***************************************************************************/
int HumidFailChk(EQUIPMENT_IO io, EQUIP_ALARMS alarm)
{
  unsigned int Counter = XTimerVal;

  if (  (   Settings.Failure[FAIL_HUMIDIFIERS].Mode != FM_NONE
         && CheckInputs(SW_HUMID_AUTO)
         && (CheckOutputs(io) || SystemAlarm[alarm] != FM_NONE)
         && CheckInputs(io))
      || SystemAlarm[alarm] == FM_FAIL)  // latch the failure
  {
    if (AlarmTimer[alarm] == 0)
    {
      AlarmTimer[alarm] = Counter;
    }
    else
    {
      if (Counter - AlarmTimer[alarm] >= Settings.Failure[FAIL_HUMIDIFIERS].Timer * T_MINS)
      {
        WarningsSet(WARN_HUMIDIFIER, FM_ALARM, Settings.Failure[FAIL_HUMIDIFIERS].Mode, io);
        SystemAlarm[alarm] = Settings.Failure[FAIL_HUMIDIFIERS].Mode;

        if (Settings.Failure[FAIL_HUMIDIFIERS].Mode == FM_FAIL)
        {
//          SystemState = ST_FAILURE;
          return 1;
        }
      }
    }
  }
  else
  {
    SystemAlarm[alarm] = FM_NONE;   // clear the alarm
    AlarmTimer[alarm] = 0;    // clear the timer
  }

  return 0;
} // end HumidFailChk()

/***************************************************************************

FUNCTION:   MasterBroadcastFailChk()

PURPOSE:    Report error if master broadcasts are not being received

COMMENTS:

***************************************************************************/
void MasterBroadcastFailChk(void)
{
  unsigned int Counter = XTimerVal;

  if (   (   Settings.MasterSlave == MSMODE_SLAVE
          || Settings.MasterSlave == MSMODE_SLAVE_NOBROADCAST)
      && Counter - AlarmTimer[AL_NOBROADCAST] >= 10 * T_MINS)
  {
    SystemAlarm[AL_NOBROADCAST] = 1;
    WarningsSet(WARN_NOBROADCAST, FM_ALARM, FM_ALARM, NA);

    // use local sensors if available
    if (Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Sensor[SENSOR_OUTSIDE_TEMP].Disabled == 0)
    {
      Settings.MasterSlave = MSMODE_SLAVE_NOBROADCAST;
      WarningsSet(WARN_SLAVENOBROADCAST, FM_ALARM, FM_ALARM, NA);
    }
    else  // otherwise clear the last master broadcast values
    {
      Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Sensor[SENSOR_OUTSIDE_TEMP].Value = SENSOR_VAL_UNDEFINED;
    }

    if (   Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Disabled == 0
        && Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_OUTSIDE_HUMID].Disabled == 0)
    {
      Settings.MasterSlave = MSMODE_SLAVE_NOBROADCAST;
      WarningsSet(WARN_SLAVENOBROADCAST, FM_ALARM, FM_ALARM, NA);
    }
    else  // otherwise clear the last master broadcast values
    {
      Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_OUTSIDE_HUMID].Value = SENSOR_VAL_UNDEFINED;
    }
  }
} // end MasterBroadcastFailChk()

/***************************************************************************

FUNCTION:   OutsideHumidSensorFailChk()

PURPOSE:    Monitor for an outside humidity sensor failure

COMMENTS:   3/21/07
            Tested by unplugging sensor, UI = alarm, fail

            11/6/09
            Tested failure latch

***************************************************************************/
int OutsideHumidSensorFailChk(void)
{
  unsigned int Counter = XTimerVal;

  if (  (   Settings.Failure[FAIL_OUTSIDE_HUMIDITY].Mode != FM_NONE
         && Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Present == 1
         && Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_OUTSIDE_HUMID].Disabled == 0
         && Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_OUTSIDE_HUMID].Value == SENSOR_VAL_UNDEFINED)
      || SystemAlarm[AL_OUTHUMIDSENSOR] == FM_FAIL)  // latch the failure
  {
    if (AlarmTimer[AL_OUTHUMIDSENSOR] == 0)
    {
      AlarmTimer[AL_OUTHUMIDSENSOR] = Counter;
    }
    else
    {
      if (Counter - AlarmTimer[AL_OUTHUMIDSENSOR] >= Settings.Failure[FAIL_OUTSIDE_HUMIDITY].Timer * T_MINS)
      {
        WarningsSet(WARN_OUTHUMIDSENSOR, FM_ALARM, Settings.Failure[FAIL_OUTSIDE_HUMIDITY].Mode, NA);
        SystemAlarm[AL_OUTHUMIDSENSOR] = Settings.Failure[FAIL_OUTSIDE_HUMIDITY].Mode;

        if (Settings.Failure[FAIL_OUTSIDE_HUMIDITY].Mode == FM_FAIL)
        {
          return 1;
        }
      }
    }
  }
  else
  {
    SystemAlarm[AL_OUTHUMIDSENSOR] = FM_NONE;    // clear the alarm
    AlarmTimer[AL_OUTHUMIDSENSOR] = 0;     // clear the timer
  }

  return 0;
} // end OutsideHumidSensorFailChk()

/***************************************************************************

FUNCTION:   OutsideHumidVarFailChk()

PURPOSE:    Monitor for outside humidity variance failure

COMMENTS:   If the humidity doesn't vary by more than 3% over a 24 hour
            period, then it's considered a sensor error.  The sensor must
            not be working properly.

            3/21/07
            Test by changing timer to 60secs to trigger, UI = alarm, fail
            breathed on sensor to cause change to clear the alarm

            11/6/09
            Tested failure latch

***************************************************************************/
int OutsideHumidVarFailChk(void)
{
  float HumidDiff = 0;
  float Variance = 0;
  float *OutsideHumid = &(Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_OUTSIDE_HUMID].Value);
  unsigned int Counter = XTimerVal;

  // if the sensor is not present or disabled, return
  if (   Settings.Failure[FAIL_OUTSIDE_HUMIDITY].Mode == FM_NONE
      || Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Present == 0
      || Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_OUTSIDE_HUMID].Disabled == 1)
  {
    return 0;
  }

  // get a starting value
  if (AlarmTimer[AL_OUTHUMIDVAR] == 0)
  {
    HumidVarChkValue = *OutsideHumid;
    AlarmTimer[AL_OUTHUMIDVAR] = Counter;
    SystemAlarm[AL_OUTHUMIDVAR] = FM_NONE;    // clear the alarm
    return 0;
  }

  // determine the variance
  Variance = HumidVarChkValue * 3.0;
  HumidDiff = HumidVarChkValue - *OutsideHumid;
  if (HumidDiff < 0)
  {
    HumidDiff *= -1;
  }

  // if the variance > 3%, restart the counter and return
  if (   (HumidDiff * 100.0) > Variance
      && SystemAlarm[AL_OUTHUMIDVAR] != FM_FAIL)  // latch the failure
  {
    HumidVarChkValue = *OutsideHumid;
    AlarmTimer[AL_OUTHUMIDVAR] = Counter;
    SystemAlarm[AL_OUTHUMIDVAR] = FM_NONE;    // clear the alarm
    return 0;
  }

  if (   Counter - AlarmTimer[AL_OUTHUMIDVAR] >= 24 * T_HOURS
      || SystemAlarm[AL_OUTHUMIDVAR] == FM_FAIL)  // latch the failure
  {
    WarningsSet(WARN_OUTHUMIDVAR, FM_ALARM, Settings.Failure[FAIL_OUTSIDE_HUMIDITY].Mode, NA);  // TODO: is this the correct failure setting
    SystemAlarm[AL_OUTHUMIDVAR] = Settings.Failure[FAIL_OUTSIDE_HUMIDITY].Mode;

    if (Settings.Failure[FAIL_OUTSIDE_HUMIDITY].Mode == FM_FAIL)
    {
      return 1;
    }
  }

  return 0;
} // end OutsideHumidVarFailChk()

/***************************************************************************

FUNCTION:   OutsideTempSensorFailChk()

PURPOSE:    Monitor for outside temperature sensor failure

COMMENTS:   3/21/07
            Tested by unplugging sensor, UI = alarm, fail
            9/9/9 - retested

            11/6/09
            Tested failure latch

***************************************************************************/
int OutsideTempSensorFailChk(void)
{
  unsigned int Counter = XTimerVal;

  if (   (   Settings.Failure[FAIL_OUTSIDE_AIR].Mode != FM_NONE
          && Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Sensor[SENSOR_OUTSIDE_TEMP].Disabled == 0
          && *OutsideTemp == SENSOR_VAL_UNDEFINED)
      || SystemAlarm[AL_OUTTEMPSENSOR] == FM_FAIL)  // latch the failure
  {
    if (AlarmTimer[AL_OUTTEMPSENSOR] == 0)
    {
      AlarmTimer[AL_OUTTEMPSENSOR] = Counter;
    }
    else
    {
      if (Counter - AlarmTimer[AL_OUTTEMPSENSOR] >= Settings.Failure[FAIL_OUTSIDE_AIR].Timer * T_MINS)
      {
        WarningsSet(WARN_OUTTEMPSENSOR, FM_ALARM, Settings.Failure[FAIL_OUTSIDE_AIR].Mode, NA);
        SystemAlarm[AL_OUTTEMPSENSOR] = Settings.Failure[FAIL_OUTSIDE_AIR].Mode;

        if (Settings.Failure[FAIL_OUTSIDE_AIR].Mode == FM_FAIL)
        {
          return 1;
        }
      }
    }
  }
  else
  {
    SystemAlarm[AL_OUTTEMPSENSOR] = FM_NONE;    // clear the alarm
    AlarmTimer[AL_OUTTEMPSENSOR] = 0;     // clear the timer
  }

  return 0;
} // end OutsideTempSensorFailChk()

/***************************************************************************

FUNCTION:   PlenumHumidFailChk()

PURPOSE:    Monitor for plenum humidity failure

COMMENTS:   3/21/07
            Tested by adjusting setpoint to trigger, UI = none, alarm, fail

            11/6/09
            Tested failure latch

***************************************************************************/
int PlenumHumidFailChk(void)
{
  unsigned int Counter = XTimerVal;

  if (  (   Settings.Failure[FAIL_PLENUM_HUMIDITY].Mode != FM_NONE
         && Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Present == 1
         && Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_PLENUM_HUMID].Disabled == 0
         && (   Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_PLENUM_HUMID].Value < Settings.Plenum.HumidLowFailure
             || Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_PLENUM_HUMID].Value == SENSOR_VAL_UNDEFINED) )
      || SystemAlarm[AL_PLENHUMID] == FM_FAIL)  // latch the failure
  {
    if (AlarmTimer[AL_PLENHUMID] == 0)
    {
      AlarmTimer[AL_PLENHUMID] = Counter;
    }
    else
    {
      if (Counter - AlarmTimer[AL_PLENHUMID] >= Settings.Failure[FAIL_PLENUM_HUMIDITY].Timer * T_MINS)
      {
        WarningsSet(WARN_PLENHUMID, FM_ALARM, Settings.Failure[FAIL_PLENUM_HUMIDITY].Mode, NA);
        SystemAlarm[AL_PLENHUMID] = Settings.Failure[FAIL_PLENUM_HUMIDITY].Mode;

        if (Settings.Failure[FAIL_PLENUM_HUMIDITY].Mode == FM_FAIL)
        {
          return 1;
        }
      }
    }
  }
  else
  {
    SystemAlarm[AL_PLENHUMID] = FM_NONE;    // clear the alarm
    AlarmTimer[AL_PLENHUMID] = 0;     // clear the timer
  }

  return 0;
} // end PlenumHumidFailChk()

/***************************************************************************

FUNCTION:   PlenumSensorFailChk()

PURPOSE:    Monitor for variance between the two plenum temperature sensors

COMMENTS:   3/21/07
            Tested by adjusting setpoint to trigger, UI = alarm, fail
            9/9/9 - retested

            11/6/09
            Tested failure latch

***************************************************************************/
int PlenumSensorFailChk(void)
{
  unsigned int Counter = XTimerVal;
  float            TempDiff = 0;

  TempDiff = Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Sensor[SENSOR_PLENUM_TEMP_1].Value
             - Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Sensor[SENSOR_PLENUM_TEMP_2].Value;
  if (TempDiff < 0)
  {
    TempDiff *= -1;
  }

  if (   (   Settings.Failure[FAIL_PLENUM_SENSOR].Mode != FM_NONE
          && TempDiff > Settings.Plenum.SensorDiff)
      || SystemAlarm[AL_PLENSENSOR] == FM_FAIL)  // latch the failure
  {
    if (AlarmTimer[AL_PLENSENSOR] == 0)
    {
      AlarmTimer[AL_PLENSENSOR] = Counter;
    }
    else
    {
      if (Counter - AlarmTimer[AL_PLENSENSOR] >= Settings.Failure[FAIL_PLENUM_SENSOR].Timer * T_MINS)
      {
        WarningsSet(WARN_PLENSENSOR, FM_ALARM, Settings.Failure[FAIL_PLENUM_SENSOR].Mode, NA);
        SystemAlarm[AL_PLENSENSOR] = Settings.Failure[FAIL_PLENUM_SENSOR].Mode;

        if (Settings.Failure[FAIL_PLENUM_SENSOR].Mode == FM_FAIL)
        {
          return 1;
        }
      }
    }
  }
  else
  {
    SystemAlarm[AL_PLENSENSOR] = FM_NONE;    // clear the alarm
    AlarmTimer[AL_PLENSENSOR] = 0;     // clear the timer
  }

  return 0;
} // end PlenumSensorFailChk()

/***************************************************************************

FUNCTION:   PlenumTempFailChk()

PURPOSE:    Monitor for plenum temperature out of limits

COMMENTS:   3/21/07
            Tested by adjusting setpoints to trigger

            7/31/08
            Tested onion mode auto clear of the high deviation alarm

            11/6/09
            Tested failure latch

***************************************************************************/
int PlenumTempFailChk(void)
{
  int RetVal = 0;

  if (Settings.Co2.Purge.Start == 0)   // not purging
  {
    if (   Settings.SystemMode == SM_ONION
        && CheckInputs(SW_CURE_AUTO))
    {
      RetVal += PlenumTempFailCureModeLowChk();
      RetVal += PlenumTempFailCureModeHighChk();
    }
    else
    {
      RetVal += PlenumTempFailLowChk();
      RetVal += PlenumTempFailHighChk();
    }
  }
  else
  {
    AlarmTimer[AL_HIGHPLENTEMP] = 0;    // clear the timer
    AlarmTimer[AL_LOWPLENTEMP] = 0;     // clear the timer
  }

  return RetVal;
} // end PlenumTempFailChk()

/***************************************************************************

FUNCTION:   PlenumTempFailCureModeHighChk()

PURPOSE:    Monitor for plenum temperature out of limits

COMMENTS:

***************************************************************************/
int PlenumTempFailCureModeHighChk(void)
{
  unsigned int Counter = XTimerVal;

  if (   CheckOutputs(EQ_FAN)
      || SystemAlarm[AL_HIGHPLENTEMP] != FM_NONE)
  {
    // NOTE: The plenum deviation failures normally don't use the 'alarm' state,
    // they just 'fail'. In cure mode, the 'alarm' state is used to stop the burner
    // if either the manual mode, or low burner setting drives the plenum temp to
    // the manual mode high temp limit (BurnerTempSet shared for limit & setpoint)
    // to avoid actually causing the high deviation failure.
    if (   (   (CureState == CS_DEHUMID)
            && (PlenumTempAvg > (Settings.Burner.TempSet * 1.10)) )
        || (   (CureState == CS_MANUAL)
            && (PlenumTempAvg > Settings.Burner.TempSet) ) )    // max temp
    {
      SystemAlarm[AL_HIGHPLENTEMP] = FM_ALARM;    // causes the burner to stop
    }
    else if (   (SystemAlarm[AL_HIGHPLENTEMP] == FM_ALARM)
             && (   (   (CureState == CS_DEHUMID)
                     && (PlenumTempAvg < (Settings.Burner.TempSet * .90)) )
                 || (   (CureState == CS_MANUAL)
                     && (PlenumTempAvg < Settings.Burner.Threshold) ) ) )   // restart temp
    {
      // if it hit the high limit because of manual mode or low burner and the
      // plenum temp has dropped below the restart temp, then allow the burner to run again
      SystemAlarm[AL_HIGHPLENTEMP] = FM_NONE;
    }

    // regular check
    if (   PlenumTempAvg > Settings.Cure.TempHighLimit
        || SystemAlarm[AL_HIGHPLENTEMP] == FM_FAIL)  // latch the failure
    {
      if (AlarmTimer[AL_HIGHPLENTEMP] == 0)
        AlarmTimer[AL_HIGHPLENTEMP] = Counter;
      else
      {
        if (Counter - AlarmTimer[AL_HIGHPLENTEMP] >= Settings.Plenum.HighAlarmTimer * T_MINS)
        {
          SystemAlarm[AL_HIGHPLENTEMP] = FM_FAIL;
          WarningsSet(WARN_HIGHPLENTEMP, FM_ALARM, FM_ALARM, NA);
          return 1;
        }
      }
    }
    else
    {
      // don't clear the alarm state here, only above
//      SystemAlarm[AL_HIGHPLENTEMP] = FM_NONE;    // clear the alarm
      AlarmTimer[AL_HIGHPLENTEMP] = 0;     // clear the timer
    }
  }

  return 0;
} // end PlenumTempFailCureModeHighChk()

/***************************************************************************

FUNCTION:   PlenumTempFailCureModeLowChk()

PURPOSE:    Monitor for plenum temperature out of limits

COMMENTS:

***************************************************************************/
int PlenumTempFailCureModeLowChk(void)
{
  unsigned int Counter = XTimerVal;

  if (   CheckOutputs(EQ_FAN)
      || SystemAlarm[AL_LOWPLENTEMP] != FM_NONE)
  {
    if (   PlenumTempAvg < Settings.Cure.TempLowLimit
        || SystemAlarm[AL_LOWPLENTEMP] == FM_FAIL)  // latch the failure
    {
      if (AlarmTimer[AL_LOWPLENTEMP] == 0)
      {
        AlarmTimer[AL_LOWPLENTEMP] = Counter;
      }
      else
      {
        if (Counter - AlarmTimer[AL_LOWPLENTEMP] >= Settings.Plenum.LowAlarmTimer * T_MINS)
        {
          SystemAlarm[AL_LOWPLENTEMP] = FM_FAIL;
          WarningsSet(WARN_LOWPLENTEMP, FM_ALARM, FM_ALARM, NA);
          return 1;
        }
      }
    }
    else
    {
      SystemAlarm[AL_LOWPLENTEMP] = FM_NONE;    // clear the alarm
      AlarmTimer[AL_LOWPLENTEMP] = 0;     // clear the timer
    }
  }

  return 0;
} // end PlenumTempFailCureModeLowChk()

/***************************************************************************

FUNCTION:   PlenumTempFailHighChk()

PURPOSE:    Monitor for plenum temperature out of limits

COMMENTS:

***************************************************************************/
int PlenumTempFailHighChk(void)
{
  int Board;
  int Sensor;
  float RefTemp = SENSOR_VAL_UNDEFINED;
  unsigned int Counter = XTimerVal;

  if (   CheckOutputs(EQ_FAN)
      || SystemAlarm[AL_HIGHPLENTEMP] != FM_NONE)
  {
    // NOTE: the high temp check automatically uses the sensor referenced for outside air control
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

    if (   PlenumTempAvg > RefTemp + Settings.Plenum.HighAlarmTemp
        || RefTemp == SENSOR_VAL_UNDEFINED
        || SystemAlarm[AL_HIGHPLENTEMP] == FM_FAIL)  // latch the failure
    {
      if (AlarmTimer[AL_HIGHPLENTEMP] == 0)
      {
        AlarmTimer[AL_HIGHPLENTEMP] = Counter;
      }
      else
      {
        if (Counter - AlarmTimer[AL_HIGHPLENTEMP] >= Settings.Plenum.HighAlarmTimer * T_MINS)
        {
          SystemAlarm[AL_HIGHPLENTEMP] = FM_FAIL;
          WarningsSet(WARN_HIGHPLENTEMP, FM_ALARM, FM_ALARM, NA);
          return 1;
        }
      }
    }
    else
    {
      SystemAlarm[AL_HIGHPLENTEMP] = FM_NONE;    // clear the alarm
      AlarmTimer[AL_HIGHPLENTEMP] = 0;     // clear the timer
    }
  }

  return 0;
} // end PlenumTempFailHighChk()

/***************************************************************************

FUNCTION:   PlenumTempFailLowChk()

PURPOSE:    Monitor for plenum temperature out of limits

COMMENTS:

***************************************************************************/
int PlenumTempFailLowChk(void)
{
  unsigned int Counter = XTimerVal;

  if (   CheckOutputs(EQ_FAN)
      || SystemAlarm[AL_LOWPLENTEMP] != FM_NONE)
  {
    if (   PlenumTempAvg < Settings.Plenum.TempSet - Settings.Plenum.LowAlarmTemp
        || SystemAlarm[AL_LOWPLENTEMP] == FM_FAIL)  // latch the failure
    {
      if (AlarmTimer[AL_LOWPLENTEMP] == 0)
      {
        AlarmTimer[AL_LOWPLENTEMP] = Counter;
      }
      else
      {
        if (Counter - AlarmTimer[AL_LOWPLENTEMP] >= Settings.Plenum.LowAlarmTimer * T_MINS)
        {
          SystemAlarm[AL_LOWPLENTEMP] = FM_FAIL;
          WarningsSet(WARN_LOWPLENTEMP, FM_ALARM, FM_ALARM, NA);
          return 1;
        }
      }
    }
    else
    {
      SystemAlarm[AL_LOWPLENTEMP] = FM_NONE;    // clear the alarm
      AlarmTimer[AL_LOWPLENTEMP] = 0;     // clear the timer
    }
  }

  return 0;
} // end PlenumTempFailLowChk()

/***************************************************************************

FUNCTION:   RefrigFailChk()

PURPOSE:    Monitor for refrigeration failures

COMMENTS:

***************************************************************************/
int RefrigFailChk(void)
{
  int i;
  int alarms = 0;
  int stagesEnabled = 0;
  int RetVal = 0;

  for (i = 0; i < NUM_DEFROST_STAGES; ++i)
  {
    RetVal += RefrigStageChk(EQ_REFRIG_DEFROST1 + i, AL_REFRIG_DEFROST1 + i);
  }

  for (i = 0; i < NUM_REFRIG_STAGES; ++i)
  {
    RetVal += RefrigStageChk(EQ_REFRIG_STAGE1 + i, AL_REFRIG_STAGE1 + i);

    if (SystemAlarm[AL_REFRIG_STAGE1 + i] != FM_NONE)
    {
      alarms++;
    }
    if (Settings.EquipIo[EQ_REFRIG_STAGE1 + i].Input != IO_UNDEFINED)
    {
      stagesEnabled++;
    }
  }

  // if all stages in alarm, set the refrigeration system alarm to prelim
  if (   alarms > 0
      && alarms == stagesEnabled)
  {
    SystemAlarm[AL_REFRIGERATION] = FM_PRELIM;
  }

  RetVal += Refrig420Chk();

  return RetVal;
} // end RefrigFailChk()

/***************************************************************************

FUNCTION:   Refrig420Chk()

PURPOSE:    Monitor for refrigeration failure

COMMENTS:   3/21/07
            Tested with refrig on, failure, UI = none, alarm, fail
            tested that when in alarm it goes to recirc or standby

            11/6/09
            Tested failure latch

***************************************************************************/
int Refrig420Chk(void)
{
  unsigned int Counter = XTimerVal;
  int RefrigPercent = PWMValToPercent(PwmChannel[PWM_REFRIGERATION].Output);

  if (  (   Settings.Failure[FAIL_REFRIGERATION].Mode != FM_NONE
         && CheckInputs(SW_REFRIG_AUTO)
         && (RefrigPercent > 0 || SystemAlarm[AL_REFRIGERATION] != FM_NONE)
         && CheckInputs(EQ_REFRIGERATION))
      || SystemAlarm[AL_REFRIGERATION] == FM_FAIL     // latch the failure
      || SystemAlarm[AL_REFRIGERATION] == FM_PRELIM)  // stage alarms set
  {
    if (AlarmTimer[AL_REFRIGERATION] == 0)
    {
      AlarmTimer[AL_REFRIGERATION] = Counter;
    }
    else
    {
      if (Counter - AlarmTimer[AL_REFRIGERATION] >= Settings.Failure[FAIL_REFRIGERATION].Timer * T_MINS)
      {
        WarningsSet(WARN_REFRIG_PWM, FM_ALARM, Settings.Failure[FAIL_REFRIGERATION].Mode, NA);
        SystemAlarm[AL_REFRIGERATION] = Settings.Failure[FAIL_REFRIGERATION].Mode;

        if (Settings.Failure[FAIL_REFRIGERATION].Mode == FM_FAIL)
        {
//          SystemState = ST_FAILURE;
          return 1;
        }
      }
    }
  }
  else
  {
    SystemAlarm[AL_REFRIGERATION] = FM_NONE;   // clear the alarm
    AlarmTimer[AL_REFRIGERATION] = 0;    // clear the timer
  }

  return 0;
} // end Refrig420Chk()

/***************************************************************************

FUNCTION:   RefrigStageChk()

PURPOSE:    Monitor for refrigeration stage & defrost failures

COMMENTS:

***************************************************************************/
int RefrigStageChk(int io, int alarm)
{
  unsigned int Counter = XTimerVal;

  if (  (   Settings.Failure[FAIL_REFRIG_STAGES].Mode != FM_NONE
         && CheckInputs(SW_REFRIG_AUTO)
         && (CheckOutputs(io) || SystemAlarm[alarm] != FM_NONE)
         && CheckInputs(io))
      || SystemAlarm[alarm] == FM_FAIL)  // latch the failure
  {
    if (AlarmTimer[alarm] == 0)
    {
      AlarmTimer[alarm] = Counter;
    }
    else
    {
      if (Counter - AlarmTimer[alarm] >= Settings.Failure[FAIL_REFRIG_STAGES].Timer * T_MINS)
      {
        if (io >= EQ_REFRIG_DEFROST1)
        {
          WarningsSet(WARN_REFRIG_DEFROST, FM_ALARM, Settings.Failure[FAIL_REFRIG_STAGES].Mode, io);
        }
        else
        {
          WarningsSet(WARN_REFRIG_STAGE, FM_ALARM, Settings.Failure[FAIL_REFRIG_STAGES].Mode, io);
        }
        SystemAlarm[alarm] = Settings.Failure[FAIL_REFRIG_STAGES].Mode;

        if (Settings.Failure[FAIL_REFRIG_STAGES].Mode == FM_FAIL)
        {
//          SystemState = ST_FAILURE;
          return 1;
        }
      }
    }
  }
  else
  {
    SystemAlarm[alarm] = FM_NONE;   // clear the alarm
    AlarmTimer[alarm] = 0;    // clear the timer
  }

  return 0;
} // end RefrigStageChk()

/***************************************************************************

FUNCTION:   SystemFailuresChk()

PURPOSE:    Call all system failure checks

COMMENTS:

***************************************************************************/
int SystemFailuresChk(void)
{
  int RetVal = 0;

  if (Settings.SystemMode == SM_POTATO)
  {
    RetVal += ClimacellFailChk();
    RetVal += HumidFailChk(EQ_HUMID_HEAD1, AL_HUMID1);
    RetVal += HumidFailChk(EQ_HUMID_HEAD2, AL_HUMID2);
    RetVal += HumidFailChk(EQ_HUMID_HEAD3, AL_HUMID3);
  }
  else
  {
    RetVal += BurnerFailChk();   // onion
  }

  RetVal += AuxFailChk();
  RetVal += HeatFailChk();

  RetVal += AuxLowPlenumTempFailChk();
  RetVal += AirFlowFailChk();
  RetVal += FanFailChk();
  RetVal += RefrigFailChk();
  RetVal += PlenumTempFailChk();
  RetVal += PlenumSensorFailChk();
  RetVal += OutsideTempSensorFailChk();
  RetVal += OutsideHumidSensorFailChk();
  RetVal += OutsideHumidVarFailChk();
  RetVal += PlenumHumidFailChk();
  RetVal += CO2FailChk();
  RetVal += CavFailChk();

  RetVal += BayLightsMonitor(EQ_LIGHTS1, AL_LIGHTS1);
  RetVal += BayLightsMonitor(EQ_LIGHTS2, AL_LIGHTS2);

  // this just reports the error, it won't cause a failure (return 1)
  MasterBroadcastFailChk();

  return RetVal;
} // end SystemFailuresChk()

/***   End Of File   ***/
