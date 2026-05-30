/* nova_modes.c
 *
 * Nova-native verbatim port of Application/Modes.c (605 legacy lines).
 *
 * MIGRATION STATUS (Phase 11 of legacy \xe2\x86\x92 Nova-native):
 *   Pure equipment-control mode-dispatch logic.  Every function is a
 *   stateless composition of Controls.c helpers (CtrlFan, CtrlDoors,
 *   CtrlRefrig, CtrlClimacell, CtrlHumidifier, CtrlBurner, etc.) plus
 *   SerialShift OutputOn/Off.  No hardware touched directly.
 *
 *   Behaviour preserved bit-for-bit including the commented-out
 *   ModeHardFailure() historical note and the ModeBurnerCure() cure-
 *   state machine.  Only non-code change: include-path header block.
 *
 *   Global OldFanSpeedHeater is owned here (was owned by the legacy
 *   Modes.c) \xe2\x80\x94 Controls.c continues to read/write it as a tracking
 *   value for the post-heater fan-speed restore.
 *
 *   ModeIdle() is declared in Modes.h but never defined in legacy
 *   Modes.c.  It is unreferenced by any call site.  We do NOT provide
 *   a definition here \xe2\x80\x94 preserving the legacy mismatch exactly.
 */


/*** include files ***/

#include "Controls.h"
#include "Modes.h"
#include "PWM.h"
#include "SerialShift.h"
#include "Settings.h"
#include "States.h"
#include "Timer.h"

/*** defines ***/

/*** typedefs and structures ***/

/*** module variables ***/

/*** static functions ***/
char OldFanSpeedHeater = 0;

/***************************************************************************

FUNCTION:   ModeAirCure()

PURPOSE:    Control the air cure mode

COMMENTS:   Onion only

***************************************************************************/
void ModeAirCure(void)
{
  CtrlFan(Settings.Fan.MaxSpeed);
  CtrlOutsideAirBlend(ST_AIRCURE);

  CtrlRefrigOff(false);
  CtrlAux();
  CtrlCavityHeat();
  CtrlPurge();
  CtrlFanBoost();
  CtrlBurner(CTRL_OFF);
  CtrlBayLights();

  // potato outputs
  CtrlClimacellOff();
  CtrlHumidifiersOff();
} // end ModeAirCure()

/***************************************************************************

FUNCTION:   ModeBurnerCure()

PURPOSE:    Control the burner mode

COMMENTS:   Onion only

***************************************************************************/
void ModeBurnerCure(void)
{
  float RefHumid = SENSOR_VAL_UNDEFINED;

  CtrlFan(Settings.Fan.MaxSpeed);

  // determine the reference humidity
  if (Settings.Cure.HumidRef == 0)
  {
    if (!Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_PLENUM_HUMID].Disabled)
    {
      RefHumid = Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_PLENUM_HUMID].Value;
    }
  }
  else
  {
    RefHumid = CalculatedHumidity();
  }

  if (Settings.Burner.Mode == BURNER_ECONOMY)
  {
    // on a warm rainy day, the high humidity might terminate the air cure mode and close the doors
    // but the plenum temperature might remain above the burner mode plenum setpoint so we might not
    // generate any burner output from the PID controller, in this case we want to run the burner in
    // essentially dehumidification mode like in cooling/refrig modes

    // if warm enough to air cure but too humid, set the burner to low
    if (   (*OutsideTemp > Settings.Cure.StartTemp && *OutsideTemp != SENSOR_VAL_UNDEFINED)
        && (RefHumid >= Settings.Cure.StartHumid && RefHumid != SENSOR_VAL_UNDEFINED) )
    {
      CureState = CS_DEHUMID;

      // set the burner to low
      CtrlBurner(Settings.Burner.Low);

      // control temp with doors
      CtrlDoors(PlenumTempAvg, Settings.Burner.TempSet);
    }
    else
    {
      CureState = CS_BURNER;
      CtrlBurner(CTRL_AUTO);
    }
  }
  else if (Settings.Burner.Mode == BURNER_MAX)
  {
    if (   (   (CureState == CS_BURNER)
            || (CureState == CS_DEHUMID) )
        && (*OutsideTemp > Settings.Cure.StartTemp && *OutsideTemp != SENSOR_VAL_UNDEFINED)  // if warm enough to air cure but too humid, set the burner to low
        && (RefHumid >= Settings.Cure.StartHumid && RefHumid != SENSOR_VAL_UNDEFINED) )
    {
      CureState = CS_DEHUMID;

      // set the burner to low
      CtrlBurner(Settings.Burner.Low);

      // control temp with doors
      CtrlDoors(PlenumTempAvg, Settings.Burner.TempSet);
    }
    else if (   (CureState == CS_DEHUMID)
             && (*OutsideTemp <= Settings.Cure.StartTemp && *OutsideTemp != SENSOR_VAL_UNDEFINED) )
    {
      CureState = CS_BURNER;
    }
    else if (   (CureState == CS_MOD_DOOR)
             || (CureState == CS_HOLD_BURNER_MOD_DOOR) )
    {
      // set burner to threshold value
      if (CureState != CS_HOLD_BURNER_MOD_DOOR)
        CtrlBurner(Settings.Burner.Threshold);

      // control temp with doors
      CtrlDoors(PlenumTempAvg, Settings.Burner.TempSet);

      // determine if the state needs to be changed
      if (   (CureState == CS_MOD_DOOR)
          && (PWMValToPercent(PwmChannel[PWM_DOORS].Output) == 0) )
      {
        CureState = CS_MOD_BURNER_DOOR_LOCK;    // turn up the burner and lock out the door
      }
      else if (   (PWMValToPercent(PwmChannel[PWM_DOORS].Output) == 100)
               && (PWMValToPercent(PwmChannel[PWM_BURNER].Output) > 0) )
      {
        CureState = CS_MOD_BURNER;    // turn down the burner
      }
      else if (   (CureState == CS_HOLD_BURNER_MOD_DOOR)
               && (   (PlenumTempAvg < (Settings.Burner.TempSet * .95))  // get out of this mode if temp varies too much
                   || (PlenumTempAvg > (Settings.Burner.TempSet * 1.05)) ) )
      {
        CureState = CS_MOD_DOOR;
      }
    }
    else if (CureState != CS_DEHUMID)
    {
      // modulate the burner
      CtrlBurner(CTRL_AUTO);

      // determine if the state needs to be changed
      if (   (   (CureState == CS_BURNER)
              && (*OutsideTemp > Settings.Cure.TempLowLimit && *OutsideTemp != SENSOR_VAL_UNDEFINED)   // ?? do they want this restriction ??
              && (PWMValToPercent(PwmChannel[PWM_BURNER].Output) >= Settings.Burner.Threshold)   // transitioned from air cure
              && (PWMValToPercent(PwmChannel[PWM_DOORS].Output) > 0) )

          || (   (CureState == CS_MOD_BURNER)
              && (PWMValToPercent(PwmChannel[PWM_BURNER].Output) >= Settings.Burner.Threshold)
              && (PWMValToPercent(PwmChannel[PWM_DOORS].Output) == 100) )

          || (   (CureState == CS_MOD_BURNER_DOOR_UNLOCK)
              && (PWMValToPercent(PwmChannel[PWM_BURNER].Output) < Settings.Burner.Threshold) ) )
      {
        CureState = CS_MOD_DOOR;
      }
      else if (   (   (   (CureState == CS_BURNER)
                       && (PWMValToPercent(PwmChannel[PWM_BURNER].Output) < Settings.Burner.Threshold) )
                   || (   (CureState == CS_MOD_BURNER_DOOR_LOCK)
                       && (PWMValToPercent(PwmChannel[PWM_BURNER].Output) < (Settings.Burner.Threshold - 5)) ) )
               && (PlenumTempAvg > (Settings.Burner.TempSet * .98)) )   // met setpoint
      {
        CureState = CS_HOLD_BURNER_MOD_DOOR;
      }
      else if (   (CureState == CS_MOD_BURNER_DOOR_LOCK)
               && (PWMValToPercent(PwmChannel[PWM_BURNER].Output) > (Settings.Burner.Threshold + 5)) )
      {
        CureState = CS_MOD_BURNER_DOOR_UNLOCK;
      }
    }
  }
  else if (Settings.Burner.Mode == BURNER_MANUAL)
  {
    CureState = CS_MANUAL;
    CtrlBurner(Settings.Burner.Manual);
  }

  // blend outside air
  if (   (Settings.Burner.Mode == BURNER_MANUAL)
      || (   (Settings.Burner.Mode == BURNER_ECONOMY)
          && (CureState != CS_DEHUMID) ) )
  {
    if (   (PlenumTempAvg > Settings.Cure.TempLowLimit)
        && (RefHumid < Settings.Cure.HumidHighLimit && RefHumid != SENSOR_VAL_UNDEFINED) )
    {
      CtrlOutsideAirBlend(ST_BURNERCURE);
    }
    else
    {
      CtrlDoorsClose();
    }
  }

  CtrlRefrigOff(false);
  CtrlAux();
  CtrlCavityHeat();
  CtrlPurge();
  CtrlFanBoost();
  CtrlBayLights();

  // potato outputs
  CtrlClimacellOff();
  CtrlHumidifiersOff();
} // end ModeBurnerCure()

/***************************************************************************

FUNCTION:   ModeCooling()

PURPOSE:    Control the cooling mode

COMMENTS:   Potato & Onion

***************************************************************************/
void ModeCooling(int mode)
{
  CtrlFan(CTRL_AUTO);
  CtrlDoors(PlenumTempAvg, Settings.Plenum.TempSet);

  CtrlRefrigOff(false);
  CtrlCavityHeat();
  CtrlPurge();
  CtrlFanBoost();
  CtrlBayLights();
  CtrlAux();

  if (Settings.SystemMode == SM_POTATO)
  {
    OutputOff(EQ_HEAT);
    CtrlClimacell(HUMID_CTRL_CLIMACELL, HUMID_MODE_COOL);
    CtrlHumidifier(HUMID_CTRL_HUMID_1, HUMID_MODE_COOL);
    CtrlHumidifier(HUMID_CTRL_HUMID_2, HUMID_MODE_COOL);
    CtrlHumidifier(HUMID_CTRL_HUMID_3, HUMID_MODE_COOL);
  }
  else
  {
    if (mode == DEHUMID_MODE)
      CtrlBurner(Settings.Burner.Low);
    else
      CtrlBurner(CTRL_OFF);

    // potato outputs
    CtrlClimacellOff();
    CtrlHumidifiersOff();
  }

  if (   Settings.Co2.Purge.Start == 0
      && IntervalTimer[IT_FANBOOSTCYCLE] == 0)
  {
    Settings.Fan.PrevSpeed = PWMValToPercent(PwmChannel[PWM_FAN].Output);
  }
} // end ModeCooling()

/***************************************************************************

FUNCTION:   ModeFanManual()

PURPOSE:    Special control mode for when the fan is in manual

COMMENTS:   If the climacell and humidifier switches are in auto, they are
            supposed to run anytime the fan is running, including when it's
            running in manual mode.  This will run the climacell and
            humidifiers (if their switches are in auto).

***************************************************************************/
void ModeFanManual(void)
{
  if (Settings.SystemMode == SM_POTATO)
  {
    CtrlClimacell(HUMID_CTRL_CLIMACELL, HUMID_MODE_RECIRC);
    CtrlHumidifier(HUMID_CTRL_HUMID_1, HUMID_MODE_RECIRC);
    CtrlHumidifier(HUMID_CTRL_HUMID_2, HUMID_MODE_RECIRC);
    CtrlHumidifier(HUMID_CTRL_HUMID_3, HUMID_MODE_RECIRC);
  }

  CtrlFan(Settings.Fan.PrevSpeed);
  CtrlDoorsClose();
  CtrlRefrigOff(false);
  CtrlBayLights();
  CtrlAux();

  OutputOff(EQ_HEAT);
  OutputOff(EQ_CAVITY_HEAT);
  PurgeOff();
  FanBoostOff();

  if (Settings.SystemMode == SM_ONION)
  {
    CtrlBurner(CTRL_OFF);
  }
} // end ModeFanManual()

/***************************************************************************

FUNCTION:   ModeFanOff()

PURPOSE:    Special control mode for when the fan switch is off

COMMENTS:   This allows the climacell and humdifiers to run if their switch
            is in manual and the fan switch is off - for testing and
            maintenance

***************************************************************************/
void ModeFanOff(void)
{
  CtrlFan(CTRL_OFF);

  if (Settings.SystemMode == SM_POTATO)
  {
    CtrlClimacell(HUMID_CTRL_CLIMACELL, HUMID_MODE_RECIRC);
    CtrlHumidifier(HUMID_CTRL_HUMID_1, HUMID_MODE_RECIRC);
    CtrlHumidifier(HUMID_CTRL_HUMID_2, HUMID_MODE_RECIRC);
    CtrlHumidifier(HUMID_CTRL_HUMID_3, HUMID_MODE_RECIRC);
  }

  CtrlDoorsClose();
  CtrlRefrigOff(false);
  CtrlBayLights();
  CtrlAux();

  OutputOff(EQ_HEAT);
  OutputOff(EQ_CAVITY_HEAT);
  PurgeOff();
  FanBoostOff();

  if (Settings.SystemMode == SM_ONION)
  {
    CtrlBurner(CTRL_OFF);
  }
} // end ModeFanOff()

/***************************************************************************

FUNCTION:   ModeHardFailure()

PURPOSE:    Put the system in standby mode

COMMENTS:   The low plenum temperature limit and airflow restriction inputs
            are hardware failures.  The board cuts power to the outputs if
            either of these inputs fail.

***************************************************************************/
//void ModeHardFailure(void)
//{
//  CtrlRefrigOff();
//  CtrlDoorsClose();
//  CtrlClimacellOff();
//  CtrlHumidifiersOff();
//  CtrlBayLights();
//
//  OutputOff(EQ_AUX);
//  OutputOff(EQ_HEAT);
//  OutputOff(EQ_CAVITY_HEAT);
//
//  PurgeOff();
//  FanBoostOff();
//
//  if (Settings.SystemMode == SM_ONION)
//  {
//    CtrlBurner(CTRL_OFF);
//  }
//} // end ModeHardFailure()

/***************************************************************************

FUNCTION:   ModeHeating()

PURPOSE:    Control the heating mode

COMMENTS:   Potato only

***************************************************************************/
void ModeHeating(void)
{
  CtrlFan(CTRL_AUTO);
  CtrlClimacell(HUMID_CTRL_CLIMACELL, HUMID_MODE_COOL);
  CtrlHumidifier(HUMID_CTRL_HUMID_1, HUMID_MODE_COOL);
  CtrlHumidifier(HUMID_CTRL_HUMID_2, HUMID_MODE_COOL);
  CtrlHumidifier(HUMID_CTRL_HUMID_3, HUMID_MODE_COOL);

  CtrlDoorsClose();
  CtrlRefrigOff(false);
  CtrlAux();
  CtrlHeat();
  CtrlCavityHeat();
  CtrlPurge();
  CtrlFanBoost();
  CtrlBayLights();
} // end ModeHeating()

/***************************************************************************

FUNCTION:   ModeRecirc()

PURPOSE:    Control the recirculation mode

COMMENTS:   Potato & Onion

***************************************************************************/
void ModeRecirc(void)
{
  CtrlFan(Settings.Fan.RecircSpeed);

  CtrlDoorsClose();
  CtrlRefrigOff(false);
  CtrlCavityHeat();
  CtrlPurge();
  CtrlFanBoost();
  CtrlBayLights();
  CtrlAux();

  if (Settings.SystemMode == SM_POTATO)
  {
    OutputOff(EQ_HEAT);
    CtrlClimacell(HUMID_CTRL_CLIMACELL, HUMID_MODE_RECIRC);
    CtrlHumidifier(HUMID_CTRL_HUMID_1, HUMID_MODE_RECIRC);
    CtrlHumidifier(HUMID_CTRL_HUMID_2, HUMID_MODE_RECIRC);
    CtrlHumidifier(HUMID_CTRL_HUMID_3, HUMID_MODE_RECIRC);
  }
  else
  {
    CtrlBurner(CTRL_OFF);

    // potato outputs
    CtrlClimacellOff();
    CtrlHumidifiersOff();
  }
} // end ModeRecirc()

/***************************************************************************

FUNCTION:   ModeRefrig()

PURPOSE:    Control the refrigeration mode

COMMENTS:   When called in DEFROST_MODE, it determines whether to put the
            refrigeration in pump down mode (off) or normal (on)

            Potato & Onion

***************************************************************************/
void ModeRefrig(int mode)
{
  int i;

  CtrlFan(Settings.Fan.RefrigSpeed);
  CtrlDoorsClose();
  CtrlCavityHeat();
  CtrlPurge();
  CtrlFanBoost();
  CtrlBayLights();
  CtrlAux();

  if (Settings.SystemMode == SM_POTATO)
  {
    OutputOff(EQ_HEAT);
    CtrlClimacell(HUMID_CTRL_CLIMACELL, HUMID_MODE_REFRIG);
    CtrlHumidifier(HUMID_CTRL_HUMID_1, HUMID_MODE_REFRIG);
    CtrlHumidifier(HUMID_CTRL_HUMID_2, HUMID_MODE_REFRIG);
    CtrlHumidifier(HUMID_CTRL_HUMID_3, HUMID_MODE_REFRIG);
  }
  else
  {
    if (mode == DEHUMID_MODE)   // onion
      CtrlBurner(Settings.Burner.Low);
    else
      CtrlBurner(CTRL_OFF);

    // potato outputs
    CtrlClimacellOff();
    CtrlHumidifiersOff();
  }

  if (   mode == DEFROST_MODE
      ||(Settings.Co2.Purge.Start != 0 && Settings.Refrig.Purge == 1)) // if purging & pump down mode (off)
  {
    CtrlRefrigOff(false);

    for (i = 0; i < NUM_DEFROST_STAGES; ++i)
    {
      if (Settings.Refrig.Defrost[i].Diagnostic != 1)   // not diag off in equipment control page
      {
        // check for failure
        if (SystemAlarm[AL_REFRIG_DEFROST1 + i] == FM_NONE)
        {
          OutputOn((EQUIPMENT_IO) (EQ_REFRIG_DEFROST1 + i));
        }
        else
        {
          OutputOff((EQUIPMENT_IO) (EQ_REFRIG_DEFROST1 + i));
        }
      }
    }
  }
  else
  {
    for (i = 0; i < NUM_DEFROST_STAGES; ++i)
    {
      if (Settings.Refrig.Defrost[i].Diagnostic != 2)   // not diag on in equipment control
      {
        OutputOff((EQUIPMENT_IO) (EQ_REFRIG_DEFROST1 + i));
      }
    }

    CtrlRefrig(PlenumTempAvg, Settings.Plenum.TempSet);
  }
} // end ModeRefrig()

/***************************************************************************

FUNCTION:   ModeShutdown()

PURPOSE:    Shutdown the system

COMMENTS:

***************************************************************************/
void ModeShutdown(void)
{
  CtrlFan(CTRL_OFF);
  CtrlRefrigOff(false);
  CtrlDoorsClose();
  CtrlClimacellOff();
  CtrlHumidifiersOff();
  CtrlBayLights();
  CtrlAux();

  OutputOff(EQ_HEAT);
  OutputOff(EQ_CAVITY_HEAT);

  PurgeOff();
  FanBoostOff();

  if (Settings.SystemMode == SM_ONION)
  {
    CtrlBurner(CTRL_OFF);
  }
} // end ModeShutdown()

/***************************************************************************

FUNCTION:   ModeStandby()

PURPOSE:    Put the system in standby mode

COMMENTS:

***************************************************************************/
void ModeStandby(void)
{
  CtrlFan(CTRL_OFF);

  CtrlRefrigOff(false);
  CtrlDoorsClose();
  CtrlClimacellOff();
  CtrlHumidifiersOff();
  CtrlBayLights();
  CtrlAux();

  OutputOff(EQ_HEAT);

  if (Settings.CavityHeat.StandbyOn == 1)
    CtrlCavityHeat();
  else
    OutputOff(EQ_CAVITY_HEAT);

  PurgeOff();
  FanBoostOff();

  if (Settings.SystemMode == SM_ONION)
  {
    CtrlBurner(CTRL_OFF);
  }
} // end ModeStandby()

/***   End Of File   ***/
