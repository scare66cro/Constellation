/* ============================================================================
 * nova_controls.c — Nova-native port of legacy Application/Controls.c
 * ----------------------------------------------------------------------------
 *
 * MIGRATION STATUS (Phase 17 of legacy → Nova-native — FINAL PHASE):
 *
 *   Controls.c is the equipment-engine workhorse: PID controller core,
 *   per-equipment cycle decisions (heaters, refrig stages, humidifiers,
 *   CO2 purge, burners, climacell, fan boost, ramp), Aux Program rule
 *   evaluation, and the cross-bay control coupling logic.
 *
 *   This is a **verbatim port** of the legacy logic with only the file
 *   header replaced. Every behaviour from the AS2 baseline that was
 *   field-validated against real cure / cool / heat hardware is
 *   preserved bit-for-bit:
 *     - PID algorithm with integral wind-up clamp
 *     - DUTYCYCLE_INC_VALUE step quantization
 *     - CHANGE_STATE toggle macro semantics
 *     - HE_CYCLE_OFF / HE_CYCLE_ON / HE_CONTROL_OFF heater ladder
 *     - HM_MANUAL / HM_TIMER / HM_AUTO humidity dispatcher
 *     - Aux Program rule evaluator (RT_*, AND/OR, operator)
 *     - OldFanSpeedHeater handoff (owned in nova_modes.c)
 *
 *   Note on PID float vs uint16_t precision: the transitional header
 *   mods keep PID_CTRL.Kp/Ki/Kd as float and PID_PARAMS.P/I/D/U as
 *   float for now. Reverting those headers requires reviewing every
 *   PID call site in this file and is tracked as a follow-up cleanup
 *   phase, NOT mixed into this migration port.
 *
 *   Cleanups go in separate, named follow-up phases — never mixed into
 *   the migration port.
 * ============================================================================
 */


/*** include files ***/

#include <string.h>

#include "Analog_Input.h"
#include "Controls.h"
#include "Modes.h"
#include "PIDLogs.h"
#include "PWM.h"
#include "RTC.h"
#include "SerialShift.h"
#include "Settings.h"
#include "States.h"
#include "Timer.h"
#include "Warnings.h"

/* Mirror of `ao_equip_t` (canonical numeric mapping lives in
 * Nova_Firmware/lp_am2434/main.c — `AO_EQUIP_FAN=1 DOORS=2 REFRIG=3
 * BURNER=4`). Duplicated here as literals so Platform code can sanity-
 * check the operator's AO assignment without depending on controller-
 * only headers. Keep in sync with main.c if new equipment kinds are
 * added. Settings.h:122-130 references `nova_fan_output.h` for this
 * enum but no such file exists; main.c's `#define`s are the truth. */
#ifndef AO_EQUIP_FAN
#define AO_EQUIP_FAN     1U
#endif
#ifndef AO_EQUIP_DOORS
#define AO_EQUIP_DOORS   2U
#endif
#ifndef AO_EQUIP_REFRIG
#define AO_EQUIP_REFRIG  3U
#endif
#ifndef AO_EQUIP_BURNER
#define AO_EQUIP_BURNER  4U
#endif

/* "Does the operator have at least one orbit AO slot assigned to this
 * equipment kind?" — used by the Nova-architecture output-path gates
 * in CtrlDoors/CtrlBurner/CtrlFan/CtrlRefrig. Walks the Settings.AoEquip
 * matrix (16 orbit slots × 2 AO channels each). Returns true on the
 * first match; early-exit keeps the runtime overhead negligible. */
static bool nova_any_ao_assigned_for(uint8_t equip)
{
  for (unsigned slot = 0; slot < 16U; slot++) {
    for (unsigned ch = 0; ch < 2U; ch++) {
      if (Settings.AoEquip[slot][ch] == equip) {
        return true;
      }
    }
  }
  return false;
}

/*** defines ***/

/*** typedefs and structures ***/

/*** module variables ***/

PID_CTRL  PIDCtrl[NUM_PID_EQUIP];

static HUMIDITY_CTRL HumidCtrlInfo[TOTAL_HUMID_EQUIP] =
  {
    IT_HUMID1CYCLE,
    EQ_HUMID_PUMP1,
    EQ_HUMID_HEAD1,
    AL_HUMID1,
    &Settings.RemoteOff[RO_HUMIDIFIER1],
    0,
    0,

    IT_HUMID2CYCLE,
    EQ_HUMID_PUMP2,
    EQ_HUMID_HEAD2,
    AL_HUMID2,
    &Settings.RemoteOff[RO_HUMIDIFIER2],
    0,
    0,

    IT_HUMID3CYCLE,
    EQ_HUMID_PUMP3,
    EQ_HUMID_HEAD3,
    AL_HUMID3,
    &Settings.RemoteOff[RO_HUMIDIFIER3],
    0,
    0,

    IT_CLIMACELLCYCLE,
    EQ_CLIMACELL,
    EQ_CLIMACELL,
    AL_CLIMACELL,
    &Settings.RemoteOff[RO_CLIMACELL],
    0,
    0,
  };

static char CavityHeatSysOn = 0;
static char CavityHeatCycle = 1;
static int  OldDoorPWMValue = 0;

/*** static functions ***/

static int AuxProgramEvaluateRule(int index);
static int CtrlDoorsPulsed(int PWM);
static int DutyCycle(int State, int DutyCycle, int Period, unsigned int *Timer);
static int EvaluateSensorOrReferenceValue(AUXPROG_OPERATOR op, uint8_t outputState, float value, float target, float reference);
static float GetSensorOrReferenceValue(int16_t index);
static int PIDController(PID_EQ pidIndex);
static void PurgeOn(void);

/***************************************************************************

FUNCTION:   AdjustDoors()

PURPOSE:    Set the PWM value for the doors and update the channel

COMMENTS:   Calls CtrlDoorsPulsed() to control the pulsed door output

***************************************************************************/
int AdjustDoors(int PWM)
{
  PwmChannel[PWM_DOORS].Output = PWM;

  if (PwmChannel[PWM_DOORS].Output > PWM_MAX_VALUE)
    PwmChannel[PWM_DOORS].Output = PWM_MAX_VALUE;
  if (PwmChannel[PWM_DOORS].Output < PWM_MIN_VALUE)
    PwmChannel[PWM_DOORS].Output = PWM_MIN_VALUE;

  if (CtrlDoorsPulsed(PwmChannel[PWM_DOORS].Output) == 1)
  {
    PWM_UpdateChannel(PWM_DOORS);
//    PWM_UpdateChannel(Settings.PWM_doors, PWMValue.FreshAir);
    return 1;
  }
  else
  {
    return 0;
  }
} // end AdjustDoors()

/***************************************************************************

FUNCTION:   ClimacellClockMode()

PURPOSE:    Determine the climacell mode

COMMENTS:
            Tested with 12:07am, 12:29am, 12:30am, 12:00am, 12:29pm, 12:30pm,
            11:29pm, 11:30pm, 11:59pm

***************************************************************************/
int ClimacellClockMode(void)
{
  int  i    = 0;
  char Hour = 0;
  char Min  = 0;
  char Sec  = 0;

  GetTime(&Hour, &Min, &Sec);
  i = (Hour * 2);
  if (Min >= 30)
    i++;

  return(Settings.Climacell.Times[i]);
} // end ClimacellClockMode()

/***************************************************************************

FUNCTION:   CtrlAux()

PURPOSE:    Turn the auxiliary outputs on or off

COMMENTS:

***************************************************************************/
void CtrlAux(void)
{
  int i;
  float percent;
  uint16_t period;
  uint32_t Counter = XTimerVal;

  for (i = 0; i < NUM_AUX_OUTPUTS; ++i)
  {
    if (   Settings.RemoteOff[RO_AUX1 + i] == 0
        && SystemAlarm[AL_AUX1 + i] == FM_NONE
        && AuxProgramEvaluateRule(i) == 1)
    {
      Settings.AuxProgram[i].RuleStatus = 1;  // rule met

      if (Settings.AuxProgram[i].DutyCycle == 100)
      {
        Settings.AuxProgram[i].OutputState = 1;
      }
      else if (Settings.AuxProgram[i].Timer <= Counter)
      {
        CHANGE_STATE(Settings.AuxProgram[i].OutputState);

        percent = (float)Settings.AuxProgram[i].DutyCycle/100.0;
        period = Settings.AuxProgram[i].Period * 60;
        if (Settings.AuxProgram[i].Units == 1)   // hours
        {
          period *= 60;
        }

        if (Settings.AuxProgram[i].OutputState)
        {
          Settings.AuxProgram[i].Timer = Counter + (period * percent);
        }
        else
        {
          Settings.AuxProgram[i].Timer = Counter + (period - (period * percent));
        }
      }
    }
    else
    {
      Settings.AuxProgram[i].RuleStatus = 0;
      Settings.AuxProgram[i].OutputState = 0;
      Settings.AuxProgram[i].Timer = 0;
    }

    if (Settings.AuxProgram[i].OutputState)
    {
      OutputOn((EQUIPMENT_IO)(EQ_AUX1 + i));
    }
    else
    {
      OutputOff((EQUIPMENT_IO)(EQ_AUX1 + i));
    }
  }
} // end CtrlAux()

/***************************************************************************

FUNCTION:   AuxProgramEvaluateRule()

PURPOSE:    Evaluate the program parameters for the aux output

COMMENTS:

***************************************************************************/
int AuxProgramEvaluateRule(int index)
{
  int i;
  int operand;
  int eval = 0;

  for (i = 0; i < NUM_AUX_PROGRAM_RULES && Settings.AuxProgram[index].Rule[i].Type != RT_UNDEFINED; ++i)
  {
    operand = 0;

    switch (Settings.AuxProgram[index].Rule[i].Type)
    {
      case RT_MANUAL:
        operand = 1;
        break;

      case RT_OUTPUT:
        if (CheckOutputs(Settings.AuxProgram[index].Rule[i].IoIndex) == Settings.AuxProgram[index].Rule[i].State)
        {
          operand = 1;
        }
        break;

      case RT_INPUT:
        if (!CheckInputs(Settings.AuxProgram[index].Rule[i].IoIndex) == Settings.AuxProgram[index].Rule[i].State)
        {
          operand = 1;
        }
        break;

      case RT_SWITCH:
        if (CheckInputs(Settings.AuxProgram[index].Rule[i].IoIndex) == Settings.AuxProgram[index].Rule[i].State)
        {
          operand = 1;
        }
        break;

      case RT_SENSOR:
      {
        float value = GetSensorOrReferenceValue(Settings.AuxProgram[index].Rule[i].IoIndex);

        if (value != SENSOR_VAL_UNDEFINED)
        {
          if (Settings.AuxProgram[index].Rule[i].ReferenceIndex == RT_UNDEFINED)
          {
            // evaluate a single sensor, speed, or output value against the rule's target value
            float target = Settings.AuxProgram[index].Rule[i].SensorValue;
            operand = EvaluateSensorOrReferenceValue(Settings.AuxProgram[index].Rule[i].Op,
                                                     Settings.AuxProgram[index].OutputState,
                                                     value,
                                                     target,
                                                     0);
          }
          else
          {
            // evaluate a sensor, speed, or output value against another sensor or setpoint
            // value plus a differential value
            float differential = Settings.AuxProgram[index].Rule[i].SensorValue;
            float referenceValue = GetSensorOrReferenceValue(Settings.AuxProgram[index].Rule[i].ReferenceIndex);
            if (referenceValue != SENSOR_VAL_UNDEFINED)
            {
              operand = EvaluateSensorOrReferenceValue(Settings.AuxProgram[index].Rule[i].Op,
                                                       Settings.AuxProgram[index].OutputState,
                                                       value,
                                                       differential,
                                                       referenceValue);
            }
          }
        }
        else
        {
          operand = 0;
        }
        break;
      }

      case RT_MODE:
        switch (Settings.AuxProgram[index].Rule[i].IoIndex)
        {
          case AM_COOLING:
            if (   SystemState == ST_COOLING
                || SystemState == ST_COOLDEHUMID)
            {
              operand = 1;
            }
            break;

          case AM_REFRIG:
            if (   SystemState == ST_REFRIG
                || SystemState == ST_REFRIGDEHUMID)
            {
              operand = 1;
            }
            break;

          case AM_CURE:
            if (   SystemState == ST_AIRCURE
                || SystemState == ST_BURNERCURE)
            {
              operand = 1;
            }
            break;

          case AM_RECIRC:
            if (SystemState == ST_RECIRC)
            {
              operand = 1;
            }
            break;

          case AM_PURGE:
            if (Settings.Co2.Purge.Start != 0)
            {
              operand = 1;
            }
            break;

          case AM_DEFROST:
            if (SystemState == ST_DEFROST)
            {
              operand = 1;
            }
            break;

          case AM_STANDBY:
            if (SystemState == ST_STANDBY)
            {
              operand = 1;
            }
            break;

          case AM_SHUTDOWN:
            if (SystemState == ST_SHUTDOWN)
            {
              operand = 1;
            }
            break;
        }
        break;

      default:
        operand = 0;
        break;
    }

    // evaluate the expression so far
    if (i == 0)
    {
      eval = operand;
    }
    else
    {
      switch (Settings.AuxProgram[index].Rule[i].AndOr)
      {
        case AO_AND:
          eval = eval && operand;
          break;
        case AO_OR:
          eval = eval || operand;
          break;
      }
    }
  }

  return eval;
} // end AuxProgramEvaluateRule()

/***************************************************************************

FUNCTION:   EvaluateSensorOrReferenceValue()

PURPOSE:    Evaluate a sensor, speed, or output value against another sensor
            or setpoint value plus a differential value

COMMENTS:   To evaluate a single sensor, speed, or output value against the
            rule's target value, pass a reference value of 0

***************************************************************************/
int EvaluateSensorOrReferenceValue(AUXPROG_OPERATOR op,
                                   uint8_t outputState,
                                   float value,
                                   float target,
                                   float reference)
{
  int operand = 0;

  switch (op)
  {
    case OP_EQ:
      if (value == target)
      {
        operand = 1;
      }
      break;

    case OP_GT:
      if (outputState == 0)
      {
        if (value > reference + target)
        {
          operand = 1;
        }
      }
      else
      {
        if (value > reference + (target - (target * .02)))  // 2% hysteresis to turn it off
        {
          operand = 1;
        }
      }
      break;

    case OP_LT:
      if (outputState == 0)
      {
        if (value < reference + target)
        {
          operand = 1;
        }
      }
      else
      {
        if (value < reference + (target + (target * .02)))  // 2% hysteresis to turn it off
        {
          operand = 1;
        }
      }
      break;

    default:
      operand = 0;
      break;
  }

  return operand;
} // end EvaluateSensorOrReferenceValue()

/***************************************************************************

FUNCTION:   GetSensorOrReferenceValue()

PURPOSE:    Get the sensor, output, or setpoint value

COMMENTS:

***************************************************************************/
float GetSensorOrReferenceValue(int16_t index)
{
  int board;
  int sensor;
  float value;

  if (index >= 0)
  {
    board = index / ANALOG_SENSORS_PER_BOARD;
    sensor = index % ANALOG_SENSORS_PER_BOARD;
    value = Settings.AnalogBoard[board].Sensor[sensor].Value;
  }
  else
  {
    switch (index)
    {
      case -1:
        value = PlenumTempAvg;
        break;

      case -2:
        value = Settings.Plenum.TempSet;
        break;

      case -3:
        value = Settings.Plenum.HumidSet;
        break;

      case -4:
        value = PWMValToPercent(PwmChannel[PWM_FAN].Output);
        break;

      case -5:
        value = PWMValToPercent(PwmChannel[PWM_REFRIGERATION].Output);
        break;

      case -6:
        value = PWMValToPercent(PwmChannel[PWM_DOORS].Output);
        break;

      case -7:
        value = StartTemp;
        break;

      default:
        value = 0;
        break;
    }
  }

  return value;
} // end GetSensorOrReferenceValue()

/***************************************************************************

FUNCTION:   CtrlBayLights()

PURPOSE:    Turn the bay lights outputs on or off

COMMENTS:

***************************************************************************/
void CtrlBayLights(void)
{
  if (Settings.RemoteOff[RO_LIGHTS1] == 0)
    OutputOn(EQ_LIGHTS1);
  else
    OutputOff(EQ_LIGHTS1);

  if (Settings.RemoteOff[RO_LIGHTS2] == 0)
    OutputOn(EQ_LIGHTS2);
  else
    OutputOff(EQ_LIGHTS2);
} // end CtrlBayLights()

/***************************************************************************

FUNCTION:   CtrlBurner()

PURPOSE:    Controls the burner outputs  (onion)

COMMENTS:

***************************************************************************/
void CtrlBurner(int Output)
{
  int BurnerPercent;
  int PIDVal = 0;
  unsigned int Counter = XTimerVal;

  if (   Output == CTRL_OFF
      || Settings.RemoteOff[RO_BURNER] == 1
      || SystemAlarm[AL_BURNER] != FM_NONE
      || SystemAlarm[AL_HIGHPLENTEMP] != FM_NONE)
  {
    PwmChannel[PWM_BURNER].Output = PWM_MIN_VALUE;
    PWM_UpdateChannel(PWM_BURNER);
//    PWMValue.Burner = PWM_MIN_VALUE;
//    PWM_UpdateChannel(Settings.PWM_burner, PWMValue.Burner);
    OutputOff(EQ_BURNER);
    return;
  }

  /* Nova-architecture output-path gate (2026-06-02). See CtrlDoors
   * (below) for the full rationale. Burner runs only in Onion-mode
   * Cure path so the trap was masked here for most bench testing, but
   * the legacy `Settings.PWM[PWM_BURNER].Enabled` check would still
   * false-positive WARN_NO_OUTPUT for every Nova-architecture install
   * configured for AO-driven burner control. Trip the warning only
   * when neither output path is wired. */
  if (Settings.EquipIo[EQ_BURNER].Output == IO_UNDEFINED
      && !nova_any_ao_assigned_for(AO_EQUIP_BURNER))
  {
    WarningsSet(WARN_NO_OUTPUT, FM_ALARM, NA, EQ_BURNER);
    return;
  }

  if (Output != CTRL_AUTO)
  {
    PwmChannel[PWM_BURNER].Output = PercentToPWMVal(Output);
    PWM_UpdateChannel(PWM_BURNER);
//    PWM_UpdateChannel(Settings.PWM_burner, PWMValue.Burner);
  }
  else
  {
    // PID control (update interval based on "U" value)
    if (PIDCtrl[PID_BURNER].Timer == 0)
      PIDCtrl[PID_BURNER].Timer = Counter + Settings.Burner.PID.U;

    // don't call the PID controller until the timer has elapsed
    if (Counter < PIDCtrl[PID_BURNER].Timer)
      return;

    // reset the timer
    PIDCtrl[PID_BURNER].Timer = Counter + Settings.Burner.PID.U;

    // calculate the error
    PIDCtrl[PID_BURNER].CurError = Settings.Burner.TempSet - PlenumTempAvg;
    if (Settings.TempType == 1)  // if �C convert to �F
      PIDCtrl[PID_BURNER].CurError *= 1.8;

    // call the PID controller
    PIDVal = PIDController(PID_BURNER);

    // check against previous value and restrict the adjustment size
    if (PwmChannel[PWM_BURNER].Output < PIDVal)
    {
      if ((PIDVal - PwmChannel[PWM_BURNER].Output) > PWM_INC_VALUE)
        PwmChannel[PWM_BURNER].Output += PWM_INC_VALUE;
      else
        PwmChannel[PWM_BURNER].Output = PIDVal;
    }
    else
    {
      if ((PwmChannel[PWM_BURNER].Output - PIDVal) > PWM_INC_VALUE)
        PwmChannel[PWM_BURNER].Output -= PWM_INC_VALUE;
      else
        PwmChannel[PWM_BURNER].Output = PIDVal;
    }

    if (PwmChannel[PWM_BURNER].Output > PWM_MAX_VALUE)
      PwmChannel[PWM_BURNER].Output = PWM_MAX_VALUE;
    if (PwmChannel[PWM_BURNER].Output < PWM_MIN_VALUE)
      PwmChannel[PWM_BURNER].Output = PWM_MIN_VALUE;

    // set the burner PWM
    PWM_UpdateChannel(PWM_BURNER);
//    PWM_UpdateChannel(Settings.PWM_burner, PWMValue.Burner);
  }

  BurnerPercent = PWMValToPercent(PwmChannel[PWM_BURNER].Output);

  // ignite the burner
  if (BurnerPercent >= Settings.Burner.On)
    OutputOn(EQ_BURNER);

  // turn off the burner when it's 3% below BurnerOn
  if (BurnerPercent <= (Settings.Burner.On - 3))
    OutputOff(EQ_BURNER);
} // end CtrlBurner()

/***************************************************************************

FUNCTION:   CtrlCavityHeat()

PURPOSE:    Turns the cavity heat output on or off

COMMENTS:   12/21/07
            Remote off
            tested with alarm/fail/none modes
            tested turning on at CavDiff value, off at CavDiff - 1.5
            tested duty cycle
            outside temp undefined

***************************************************************************/
void CtrlCavityHeat(void)
{
  int Board;
  int Sensor;
  float RefTemp = SENSOR_VAL_UNDEFINED;

  if (   Settings.EquipIo[EQ_CAVITY_HEAT].Enabled == 0
      || Settings.CavityHeat.Mode < 2            // cavity control is off
      || Settings.RemoteOff[RO_CAVITY_HEAT] == 1 // or remote off
      || SystemAlarm[AL_CAVITYHEAT] != FM_NONE)  // or in failure
  {
    CavityHeatSysOn = 0;
    OutputOff(EQ_CAVITY_HEAT);
    return;
  }

  if (Settings.CavityHeat.Mode == 2)      // on - duty cycle
    RefTemp = *OutsideTemp;
  else if (Settings.CavityHeat.Mode == 3) // auto
  {
    // CavDutyCycle is a sensor ID
    Board = Settings.CavityHeat.DutyCycle/ANALOG_SENSORS_PER_BOARD;
    Sensor = Settings.CavityHeat.DutyCycle % ANALOG_SENSORS_PER_BOARD;
    RefTemp = Settings.AnalogBoard[Board].Sensor[Sensor].Value;
  }

  if (RefTemp != SENSOR_VAL_UNDEFINED)
  {
    if (   RefTemp < Settings.Plenum.TempSet + Settings.CavityHeat.Diff
        || (   CavityHeatSysOn == 1
            && RefTemp < Settings.Plenum.TempSet + (Settings.CavityHeat.Diff + 1.0)))    // hysteresis
    {
      CavityHeatSysOn = 1;  // flag for CavFailChk()

      if (Settings.CavityHeat.Mode == 2)      // on - duty cycle
      {
        if ((CavityHeatCycle = DutyCycle(CavityHeatCycle, Settings.CavityHeat.DutyCycle, 60 * T_MINS, &IntervalTimer[IT_CAVITYHEATCYCLE])) == 1)
          OutputOn(EQ_CAVITY_HEAT);
        else
          OutputOff(EQ_CAVITY_HEAT);
      }
      else if (Settings.CavityHeat.Mode == 3) // auto
      {
        OutputOn(EQ_CAVITY_HEAT);
      }
    }
    else
    {
      CavityHeatSysOn = 0;
      OutputOff(EQ_CAVITY_HEAT);
    }
  }
  else
  {
    CavityHeatSysOn = 0;
    OutputOff(EQ_CAVITY_HEAT);
    WarningsSet(WARN_CAVHEATCALC, FM_ALARM, FM_ALARM, NA);
  }
} // end CtrlCavityHeat()

/***************************************************************************

FUNCTION:   CtrlClimacell()

PURPOSE:    Turns the Climacell output on or off

COMMENTS:   3/21/07
            Tested in none, manual, & auto modes with type climacell & both
            Tested transitioning between needing and not needing evaporative
            cooling
            tested with alarm

            6/27/07
            Added code to run climacell with fan if not being controlled by UI
            Tested with climacell switch auto, UI controlling and not controlling
            and fan on/off/auto.  Tested with climacell switch off/manual.

***************************************************************************/
void CtrlClimacell(int EquipType, int SysMode)
{
  int PIDVal = 0;
  int Factor = 1;
  HUMIDITY_CTRL *HumidEquip = &HumidCtrlInfo[EquipType];
  float TempDiff = Settings.OutsideAir.Diff;
  int ClimacellClock = ClimacellClockMode();
  unsigned int Counter = XTimerVal;

  if (Settings.OutsideAir.AboveBelow == 1)  // below
    TempDiff *= -1;

  if (Settings.EquipIo[EQ_CLIMACELL].Enabled == 0)
  {
    HumidEquip->Control = HE_CYCLE_OFF;
  }
  else if (   CheckInputs(SW_CLIMACELL_AUTO)   // the climacell should run when the fan runs if the switch is in auto
           && CheckInputs(SW_FAN_MANUAL)       // and the fan switch is in manual
           && ClimacellClock != CC_OFF)
  {
    HumidEquip->Control = HE_CYCLE_ON;
  }
  else if (   CheckInputs(SW_CLIMACELL_AUTO)
           && ClimacellClock != CC_OFF
           && CheckInputs(SW_FAN_AUTO)
           && *OutsideTemp != SENSOR_VAL_UNDEFINED
           && *OutsideTemp > Settings.Plenum.TempSet    // the climacell is needed for evaporative cooling
           && SystemState != ST_REFRIG)
  {
    HumidEquip->Control = HE_CYCLE_ON;
  }
  else if (   CheckInputs(SW_CLIMACELL_AUTO)
           && ClimacellClock == CC_ON
           && CheckInputs(SW_FAN_AUTO) )
  {
    HumidEquip->Control = HE_CYCLE_ON;
  }
  else if (CheckInputs(SW_CLIMACELL_MANUAL))
  {
    HumidEquip->Control = HE_CYCLE_ON;
  }
  else if (   CheckInputs(SW_CLIMACELL_AUTO)
           && ClimacellClock == CC_AUTO
           && CheckInputs(SW_FAN_AUTO) )
  {
    // PID controller (update interval based on "U" value)
    if (PIDCtrl[PID_CLIMACELL].Timer == 0)
    {
//      PIDCtrl[PID_CLIMACELL].Timer = Timer_Sec() + (Timer_SecGetVal(Settings.ClimU)); // DEMO MODE
      PIDCtrl[PID_CLIMACELL].Timer = Counter + (Settings.Climacell.PID.U * T_MINS);
    }

    if (   Counter > PIDCtrl[PID_CLIMACELL].Timer
        && Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_PLENUM_HUMID].Value != SENSOR_VAL_UNDEFINED)
    {
//      PIDCtrl[PID_CLIMACELL].Timer = Timer_Sec() + (Timer_SecGetVal(Settings.Climacell.PID.U)); // DEMO MODE
      PIDCtrl[PID_CLIMACELL].Timer = Counter + (Settings.Climacell.PID.U * T_MINS);
      PIDCtrl[PID_CLIMACELL].CurError = Settings.Plenum.HumidSet - Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_PLENUM_HUMID].Value;
      PIDVal = PIDController(PID_CLIMACELL);

      if (PIDVal >= 100)
      {
        HumidEquip->PIDValue = 100;
        HumidEquip->Control = HE_CYCLE_ON;
      }
      else if (PIDVal <= 0)
      {
        HumidEquip->PIDValue = 0;
        HumidEquip->Control = HE_CYCLE_OFF;
      }
      else
      {
        if (HumidEquip->PIDValue > PIDVal)
          Factor = -1;

        // check against previous value and restrict the adjustment size
        if (((PIDVal - HumidEquip->PIDValue) * Factor) > DUTYCYCLE_INC_VALUE)
          HumidEquip->PIDValue += (DUTYCYCLE_INC_VALUE * Factor);
        else
          HumidEquip->PIDValue = PIDVal;

//        HumidEquip->Control = DutyCycle(HumidEquip->Control, HumidEquip->PIDValue, 100, &IntervalTimer[HumidEquip->Timer]); // DEMO MODE
        HumidEquip->Control = DutyCycle(HumidEquip->Control, HumidEquip->PIDValue, 100 * T_MINS, &IntervalTimer[HumidEquip->Timer]);
      }
    }
  }
  else
  {
    HumidEquip->Control = HE_CONTROL_OFF;
  }

  if (   SystemAlarm[HumidEquip->Alarm] != FM_NONE
      || *HumidEquip->RemoteOff == 1)
  {
    HumidEquip->Control = HE_CONTROL_OFF;
  }

  // set the outputs
  if (HumidEquip->Control == HE_CYCLE_OFF)
  {
    OutputOff(HumidEquip->Output);
  }
  else if (HumidEquip->Control == HE_CYCLE_ON)
  {
    OutputOn(HumidEquip->Output);
  }

  if (HumidEquip->Control == HE_CONTROL_OFF)
  {
    OutputOff(HumidEquip->Output);
    IntervalTimer[HumidEquip->Timer] = 0;
  }
} // end CtrlClimacell()

/***************************************************************************

FUNCTION:   CtrlClimacellOff()

PURPOSE:    Turns the Climacell off

COMMENTS:

***************************************************************************/
void CtrlClimacellOff(void)
{
  OutputOff(EQ_CLIMACELL);
} // end CtrlClimacellOff()

/***************************************************************************

FUNCTION:   CtrlDoors()

PURPOSE:    Turns the fresh air door output on or off

COMMENTS:   A new door PWM value and pulse position are not determined until
            the previous pulse door move has completed.  So the call to the
            PID controller not only has to wait for the programmed "U" (PID
            timer value set in the UI) timer to expire, it also has to make
            sure the previous pulse move is complete.

***************************************************************************/
void CtrlDoors(float Actual, float Target)
{
  int PIDVal = 0;
  unsigned int Counter = XTimerVal;

  // don't adjust the doors during a purge or fan boost cycle
  if (   Settings.Co2.Purge.Start != 0
      || IntervalTimer[IT_FANBOOSTCYCLE] != 0)
  {
    return;
  }

  // PID control (update interval based on "U" value)
  if (PIDCtrl[PID_DOOR].Timer == 0)
  {
    PIDCtrl[PID_DOOR].Timer = Counter + Settings.Door.PID.U;
  }

  // don't call the PID controller until the timer has elapsed and
  // the previous pulse door adjustment is complete
  if (   Counter < PIDCtrl[PID_DOOR].Timer
      || PulseDoorMove != 0)
  {
    return;
  }

  /* Nova-architecture output-path gate (replaces legacy
   * `Settings.PWM[PWM_DOORS].Enabled` check, 2026-06-02).
   *
   * In legacy AS2 doors were driven either by direct shift-register
   * pulse-door coils OR by a dedicated hardware PWM channel on the
   * TM4C. In Nova:
   *   - Pulse-coil path: operator assigns EQ_PULSEDOOR_OPEN AND
   *     EQ_PULSEDOOR_CLOSE coils via IO Config (typically to GDC-orbit
   *     DOs); `CtrlDoorsPulsed` bit-bangs them, equipment_output_sync
   *     pushes coil state out to the orbit DO regs each tick.
   *   - 4-20mA path: operator assigns PWM_DOORS to an orbit AO via
   *     PWM Config (Settings.AoEquip[slot][ch] == AO_EQUIP_DOORS);
   *     equipment_ao_sync_task pushes PwmChannel[PWM_DOORS].Output
   *     to that orbit's HR 0/1 each tick.
   * The legacy `Settings.PWM[PWM_DOORS].Enabled` field has no meaning
   * in Nova IO Config — its check would trip the warning for every
   * customer using the new configuration path. Check the Nova-shape
   * fields instead. Gate trips ONLY when neither path is wired. */
  {
    bool have_pulse_coils =
        (Settings.EquipIo[EQ_PULSEDOOR_OPEN ].Output != IO_UNDEFINED) &&
        (Settings.EquipIo[EQ_PULSEDOOR_CLOSE].Output != IO_UNDEFINED);
    if (!have_pulse_coils && !nova_any_ao_assigned_for(AO_EQUIP_DOORS)) {
      WarningsSet(WARN_NO_OUTPUT, FM_ALARM, NA, EQ_DOORS);
      return;
    }
  }

  // reset the timer
  PIDCtrl[PID_DOOR].Timer = Counter + Settings.Door.PID.U;

  // calculate the error
  PIDCtrl[PID_DOOR].CurError = Actual - Target;
  if (Settings.TempType == 1)  // if �C convert to �F
  {
    PIDCtrl[PID_DOOR].CurError *= 1.8;
  }

  // call the PID controller
  PIDVal = PIDController(PID_DOOR);

  // check against previous value and restrict the adjustment size
  if (PwmChannel[PWM_DOORS].Output < PIDVal)
  {
    if ((PIDVal - PwmChannel[PWM_DOORS].Output) > PWM_INC_VALUE)
    {
      PwmChannel[PWM_DOORS].Output += PWM_INC_VALUE;
    }
    else
    {
      PwmChannel[PWM_DOORS].Output = PIDVal;
    }
  }
  else
  {
    if ((PwmChannel[PWM_DOORS].Output - PIDVal) > PWM_INC_VALUE)
    {
      PwmChannel[PWM_DOORS].Output -= PWM_INC_VALUE;
    }
    else
    {
      PwmChannel[PWM_DOORS].Output = PIDVal;
    }
  }

  AdjustDoors(PwmChannel[PWM_DOORS].Output);
} // end CtrlDoors()

/***************************************************************************

FUNCTION:   CtrlDoorsClose()

PURPOSE:    Closes the doors

COMMENTS:   3/21/07
            Tested while purging

***************************************************************************/
void CtrlDoorsClose(void)
{
  // close the doors (if not purging)
  if (Settings.Co2.Purge.Start == 0)
  {
    PIDClear(PID_DOOR);
    AdjustDoors(PWM_MIN_VALUE);

    if (PulseDoorPosition != 0)
      PulseDoorInit = 1;
  }
} // end CtrlDoorsClose()

/***************************************************************************

FUNCTION:   CtrlDoorsPulsed()

PURPOSE:    Determines the next move for the pulse door output

COMMENTS:   This function doesn't actually control the outputs.  It determines
            if the door needs to be moved and if so, which direction.  The door
            outputs are actually controlled in Timer1_ISR() because the
            control pulses need to be accurate and the timer interrupt was the
            only way to keep them accurate.  The main() loop is too slow and
            inconsistent.

            PulseDoorFlag is used as a semaphore between this function and the
            IRQ handler to lock changes to the door position and direction.

            A new door PWM value and pulse position are not determined until
            the previous pulse door move has completed.  So the call to the
            PID controller not only has to wait for the programmed "U" (PID
            timer value set in the UI) timer to expire, it also has to make
            sure the previous pulse move is complete.

***************************************************************************/
int CtrlDoorsPulsed(int PWM)
{
  float DoorPercent = ((float)PWM - PWM_MIN_VALUE)/PWM_RANGE;
  int   NewPosition = Settings.Door.ActuatorTime * DoorPercent;

  // if the semaphore is set, the timer IRQ is currently moving the door, so return
  if (PulseDoorFlag == 1)
    return 0;

  // check that the door front panel switch is in auto
  if (!CheckInputs(SW_FRESHAIR_AUTO))
  {
    PulseDoorMove = 0;
    return 0;
  }

  // clear the init flag if the initializing move is complete
  if (PulseDoorInit == 1)
  {
    if (PulseDoorMove == 0)
      PulseDoorInit = 0;
    else
      return 0;
  }

  // set the semaphore to lock out the timer IRQ handler
  PulseDoorFlag = 1;

  // determine the new position and direction
  if (NewPosition < PulseDoorPosition)
  {
    PulseDoorMove = PulseDoorPosition - NewPosition;
    PulseDoor = EQ_PULSEDOOR_CLOSE;
  }
  else
  {
    PulseDoorMove = NewPosition - PulseDoorPosition;
    PulseDoor = EQ_PULSEDOOR_OPEN;
  }

  // clear the semaphore
  PulseDoorFlag = 0;

  return 1;
} // end CtrlDoorsPulsed()

/***************************************************************************

FUNCTION:   CtrlDoorsPulsed_Init()

PURPOSE:    Initialize (close) the pulse doors

COMMENTS:   Uses the PulseDoorFlag semaphore

            The board (rev D+) holds the pulse door close output high while
            the system is in shutdown (start/stop switch = stop) to close and
            keep the doors closed.  Therefore, the amount of time that the
            system is shutdown can be subtracted from the time it takes to
            initialize the doors.

***************************************************************************/
void CtrlDoorsPulsed_Init(void)
{
  if (CheckInputs(SW_FRESHAIR_AUTO))
  {
    // set the semaphore to lock out the timer IRQ handler
    PulseDoorFlag = 1;

    // set the position and the direction (close all the way)
    PulseDoorMove = Settings.Door.ActuatorTime;

    // subtract the time the system has been in shutdown (see comment)
    if (IntervalTimer[IT_SHUTDOWN] != 0)
      PulseDoorMove -= (XTimerVal - IntervalTimer[IT_SHUTDOWN]);

    if (PulseDoorMove < 0)
      PulseDoorMove = 0;

    PulseDoorPosition = PulseDoorMove;
    PulseDoor = EQ_PULSEDOOR_CLOSE;

    // set the init flag to indicate initialization mode
    PulseDoorInit = 1;

    // clear the semaphore
    PulseDoorFlag = 0;

    // set the PWM value
    PwmChannel[PWM_DOORS].Output = PWM_MIN_VALUE;
  }
} // end CtrlDoorsPulsed_Init()

/***************************************************************************

FUNCTION:   UpdateFanSpeed

PURPOSE:    Checks the fanspeed is in range and sets fan to prev value

***************************************************************************/
void UpdateFanSpeed(char *prev) {
    // make sure the previous fan speed is still in range
    if (*prev > Settings.Fan.MaxSpeed)
    {
      *prev = Settings.Fan.MaxSpeed;
    }
    else if (*prev < Settings.Fan.MinSpeed)
    {
      *prev = Settings.Fan.MinSpeed;
    }

    // set the fan speed to the previous speed
    PwmChannel[PWM_FAN].Output = PercentToPWMVal(*prev);
    PWM_UpdateChannel(PWM_FAN);
}

/***************************************************************************

FUNCTION:   CtrlFan()

PURPOSE:    Turn the fan output on and off

COMMENTS:   When cooling mode is entered, the fan should return to the speed
            it was running the last time the system was in cooling mode.

            3/21/07
            Tested auto mode with setpoint & return air (defaults) and plenum
            temp & board 4 sensor 4.  Adjust reference to cause fan speed to
            increase and decrease.

***************************************************************************/
void CtrlFan(int speed)
{
  int Board;
  unsigned int MaxFanPWM;
  unsigned int MinFanPWM;
  int Sensor;
  float Diff = 0;
  float Ref1 = SENSOR_VAL_UNDEFINED;
  float Ref2 = SENSOR_VAL_UNDEFINED;
  unsigned int Counter = XTimerVal;

  /* Nova-architecture output-path gate (2026-06-02). See CtrlDoors
   * (below) for the full rationale. Fan PID is the most-exercised
   * control loop so this gate fires the most visibly when broken;
   * legacy `Settings.PWM[PWM_FAN].Enabled` is the AS2 TM4C hardware
   * PWM flag and has no Nova analogue. Pulse-coil path: a single
   * EQ_FAN coil (Settings.EquipIo[EQ_FAN]); AO path:
   * AO_EQUIP_FAN = 1 (the most common configuration, since most fan
   * VFDs accept a 0-10V or 4-20mA speed reference). */
  if (Settings.EquipIo[EQ_FAN].Output == IO_UNDEFINED
      && !nova_any_ao_assigned_for(AO_EQUIP_FAN))
  {
    WarningsSet(WARN_NO_OUTPUT, FM_ALARM, NA, EQ_FAN);
    return;
  }

  // turn the fan off - standby mode
  if (   speed == CTRL_OFF
      || Settings.RemoteOff[RO_FAN] == 1)
  {
//    PWMValue.Fan = PWM_MIN_VALUE;
//    PWM_UpdateChannel(Settings.PWM_fan, PWMValue.Fan);
    PwmChannel[PWM_FAN].Output = PWM_MIN_VALUE;
    PWM_UpdateChannel(PWM_FAN);
    OutputOff(EQ_FAN);
    IntervalTimer[IT_FANSPEEDUPD] = 0;
    return;
  }

  // don't adjust the fan during a purge or fan boost cycle
  if (   Settings.Co2.Purge.Start != 0
      || IntervalTimer[IT_FANBOOSTCYCLE] != 0)
  {
    return;
  }

  // make sure the fan is on before adjusting the speed
  if (!CheckOutputs(EQ_FAN))
  {
    OutputOn(EQ_FAN);
  }

  // determine the fan speed
  if (speed != CTRL_AUTO)
  {
    PwmChannel[PWM_FAN].Output = PercentToPWMVal(speed);
    PWM_UpdateChannel(PWM_FAN);
//    PWM_UpdateChannel(Settings.PWM_fan, PWMValue.Fan);
  }
  else
  {
    if (IntervalTimer[IT_FANSPEEDUPD] == 0)   // just starting
    {
      UpdateFanSpeed(&Settings.Fan.PrevSpeed);
      IntervalTimer[IT_FANSPEEDUPD] = Counter;
      return;
    }

    // check the update interval
//    if ((Counter - IntervalTimer[IT_FANSPEEDUPD]) < (Settings.Fan.UpdatePeriod * T_MINS) // DEMO MODE
    if ((Counter - IntervalTimer[IT_FANSPEEDUPD]) < (Settings.Fan.UpdatePeriod * T_HOURS)
        || Settings.Fan.UpdatePeriod == 0)
    {
      return;
    }

    // reset the timer
    IntervalTimer[IT_FANSPEEDUPD] = Counter;

    // determine the reference temperatures
    if (   Settings.SystemMode == SM_POTATO
        || (   Settings.SystemMode == SM_ONION
            && Settings.Fan.UpdateMode == 0))
    {
      if (Settings.Fan.TempRef1 == 0)
      {
        Ref1 = Settings.Plenum.TempSet;
      }
      else
      {
        Ref1 = PlenumTempAvg;
      }

      if (Settings.Fan.TempRef2 == 255)
      {
        Ref2 = Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Sensor[SENSOR_RETURN_TEMP].Value;
      }
      else
      {
        // TempRef2 is a sensor ID
        Board = Settings.Fan.TempRef2/ANALOG_SENSORS_PER_BOARD;
        Sensor = Settings.Fan.TempRef2 % ANALOG_SENSORS_PER_BOARD;
        Ref2 = Settings.AnalogBoard[Board].Sensor[Sensor].Value;
      }

      Diff = Settings.Fan.TempDiff;
    }
    else
    {
      Ref1 = Settings.Plenum.HumidSet;
      if (Settings.Plenum.HumidSetpointRef == 0)
      {
        Ref2 = Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_PLENUM_HUMID].Value;
      }
      else
      {
        Ref2 = Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_RETURN_HUMID].Value;
      }
    }

    if (Ref1 == SENSOR_VAL_UNDEFINED || Ref2 == SENSOR_VAL_UNDEFINED)
    {
      return;
    }
    // determine the temperature differential and set the fan speed accordingly
    if (Ref2 - Ref1 > Diff)
    {
      MaxFanPWM = PercentToPWMVal(Settings.Fan.MaxSpeed);

      PwmChannel[PWM_FAN].Output += PWM_INC_VALUE;
      if (PwmChannel[PWM_FAN].Output > MaxFanPWM)
      {
        PwmChannel[PWM_FAN].Output = MaxFanPWM;
      }

      PWM_UpdateChannel(PWM_FAN);
//      PWM_UpdateChannel(Settings.PWM_fan, PWMValue.Fan);
    }
    else
    {
      MinFanPWM = PercentToPWMVal(Settings.Fan.MinSpeed);

      PwmChannel[PWM_FAN].Output -= PWM_INC_VALUE;
      if (PwmChannel[PWM_FAN].Output < MinFanPWM)
      {
        PwmChannel[PWM_FAN].Output = MinFanPWM;
      }

      PWM_UpdateChannel(PWM_FAN);
//      PWM_UpdateChannel(Settings.PWM_fan, PWMValue.Fan);
    }
  }
} // end CtrlFan()

/***************************************************************************

FUNCTION:   CtrlFanBoost()

PURPOSE:    Control the fan boost

COMMENTS:   3/21/07
            Tested in none, manual, & auto modes
            Manual & auto during cooling - in and out of temp range
            Manual & auto during refrig - in and out of temp range and in range
            but refrig at +50%

***************************************************************************/
void CtrlFanBoost(void)
{
  unsigned int Counter = XTimerVal;

  // determine if fan boost should be initiated
  if (  (   Settings.FanBoost.Mode == 1    // temp based
         && (*OutsideTemp < Settings.FanBoost.Temp && *OutsideTemp != SENSOR_VAL_UNDEFINED)
         && PWMValToPercent(PwmChannel[PWM_FAN].Output) <= CONTINUOUSFAN_THRESHOLD
//         && Counter - IntervalTimer[IT_FANBOOSTINTERVAL] >= Settings.FanBoost.Interval * T_MINS) // DEMO Mode
         && Counter - IntervalTimer[IT_FANBOOSTINTERVAL] >= Settings.FanBoost.Interval * T_HOURS)
      ||(   Settings.FanBoost.Mode == 2   // runtime based
         && IntervalTimer[IT_FANCONTINUOUSTIME] >= Settings.FanBoost.Interval * T_HOURS))
  {
    // start fan boost
    if (   (   SystemState == ST_COOLING    // we're in cooling, recirc, or heating
            || SystemState == ST_RECIRC
            || SystemState == ST_HEATING)
        && Settings.Co2.Purge.Start == 0)     // a purge cycle has not started
    {
      if (IntervalTimer[IT_FANBOOSTCYCLE] == 0)
      {
        OldFanSpeedHeater = PWMValToPercent(PwmChannel[PWM_FAN].Output);
        CtrlFan(Settings.FanBoost.Speed);

        IntervalTimer[IT_FANBOOSTCYCLE] = Counter;  // start counter
        IntervalTimer[IT_FANBOOSTINTERVAL] = Counter;   // TODO - clb: hmmm...
      }
    }
  }

  // stop fan boost
  if (IntervalTimer[IT_FANBOOSTCYCLE] != 0)
  {
    if (   Counter - IntervalTimer[IT_FANBOOSTCYCLE] >= Settings.FanBoost.Duration * T_MINS // the cycle has completed
        || (   SystemState != ST_COOLING    // we're not cooling, recirc, or heating anymore
            && SystemState != ST_RECIRC
            && SystemState != ST_HEATING)
        || Settings.Co2.Purge.Start != 0      // a purge cycle has started
        || Settings.FanBoost.Mode == 0)      // the UI set the fan boost to none
    {
      FanBoostOff();
    }
  }

  // if we're in temp based mode and the fan is above the 80% threshold
  // and a boost cycle has not been initiated, then reset the boost cycle
  // interval timer.  If the fan is above the limit, we don't want to initiate
  // a boost cycle as soon as it drops below the limit.  We want to wait the
  // appropriate interval time again.
  if (   Settings.FanBoost.Mode == 1    // temp based
      && PWMValToPercent(PwmChannel[PWM_FAN].Output) > CONTINUOUSFAN_THRESHOLD
      && IntervalTimer[IT_FANBOOSTCYCLE] == 0)
  {
    IntervalTimer[IT_FANBOOSTINTERVAL] = Counter;
  }
} // end CtrlFanBoost()

/***************************************************************************

FUNCTION:   CtrlHeat()

PURPOSE:    Turn the heat outputs on and off

COMMENTS:   3/21/07
            Tested remote off and switch not in auto
            tested with alarm

***************************************************************************/
void CtrlHeat(void)
{
  // check for failure
  if (SystemAlarm[AL_HEAT] != FM_NONE)
  {
    OutputOff(EQ_HEAT);
    return;
  }

//  if (   CheckInputs(SW_HEAT_AUTO)   // safety - should be redundant SetSystemState()
//      && Settings.RemoteOff[RO_HEAT] == 0)
  if (Settings.RemoteOff[RO_HEAT] == 0)
  {
    OutputOn(EQ_HEAT);
  }
  else
  {
    OutputOff(EQ_HEAT);
  }
} // end CtrlHeat()

/***************************************************************************

FUNCTION:   CtrlHumidifier()

PURPOSE:    Turn the humidifier outputs on and off

COMMENTS:   The humidifier heads are supposed to run continuously (during a
            specified mode - cooling, etc.) due to wear/humidity/rust problems,
            just the pumps are duty cycled.

            3/21/07
            Tested with manual and 50% & 25% duty cycles, auto with & without
            need for humidity, remote off, and switch not in auto.
            tested with alarm

            6/27/07
            Added code to run humidifiers with fan if not being controlled by UI
            Tested with humidifier switch auto, UI controlling and not controlling
            and fan on/off/auto.  Tested with humidifier switch off/manual.

***************************************************************************/
void CtrlHumidifier(int EquipType, int SysMode)
{
  int PIDVal = 0;
  int Factor = 1;
  unsigned int Counter = XTimerVal;
  HUMIDITY_CTRL *HumidEquip = &HumidCtrlInfo[EquipType];

  if (Settings.EquipIo[HumidEquip->HeadOutput].Enabled == 0)
  {
    HumidEquip->Control = HE_CYCLE_OFF;
  }
  else if (   CheckInputs(SW_HUMID_AUTO)   // the humidifiers should run when the fan runs if the switch is in auto
           && (   CheckInputs(SW_FAN_MANUAL) //  and the fan switch is in manual or if the mode = manual
               || (   Settings.HumidCtrl[EquipType].Mode == HM_MANUAL
                   && FanOn()) ) )
  {
    HumidEquip->Control = HE_CYCLE_ON;
  }
  else if (CheckInputs(SW_HUMID_MANUAL))
  {
    HumidEquip->Control = HE_CYCLE_ON;
  }
  else if (   CheckInputs(SW_HUMID_AUTO)
           && CheckInputs(SW_FAN_AUTO)
           && Settings.HumidCtrl[EquipType].Mode == HM_TIMER)   // duty cycle
  {
    if (   HumidEquip->Control != HE_CYCLE_ON
        && (   IntervalTimer[HumidEquip->Timer] == 0
            || IntervalTimer[HumidEquip->Timer] < Counter) )
    {
      IntervalTimer[HumidEquip->Timer] = Counter + Settings.HumidCtrl[EquipType].DutyCycle[SysMode].On;
      HumidEquip->Control = HE_CYCLE_ON;
    }
    else if (   HumidEquip->Control == HE_CYCLE_ON
             && IntervalTimer[HumidEquip->Timer] < Counter)
    {
      IntervalTimer[HumidEquip->Timer] = Counter + Settings.HumidCtrl[EquipType].DutyCycle[SysMode].Off;
      HumidEquip->Control = HE_CYCLE_OFF;
    }
  }
  else if (   CheckInputs(SW_HUMID_AUTO)
           && CheckInputs(SW_FAN_AUTO)
           && Settings.HumidCtrl[EquipType].Mode == HM_AUTO)   // auto (PID controller)
  {
    if (   EquipType != HUMID_CTRL_HUMID_1
        && Settings.HumidCtrl[HUMID_CTRL_HUMID_1].Mode == HM_AUTO)
    {
      // if all humidifiers are set to auto, drive #2 & #3 the same as #1
      HumidEquip->Control = HumidCtrlInfo[HUMID_CTRL_HUMID_1].Control;
    }
    else
    {
      // PID controller (update interval based on "U" value)
      if (PIDCtrl[PID_HUMIDIFIER].Timer == 0)
      {
        PIDCtrl[PID_HUMIDIFIER].Timer = Counter + Settings.Climacell.PID.U;  // same PID values used for climacell & humidifiers
      }

      if (   Counter > PIDCtrl[PID_HUMIDIFIER].Timer
          && Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_PLENUM_HUMID].Value != SENSOR_VAL_UNDEFINED)
      {
        PIDCtrl[PID_HUMIDIFIER].Timer = Counter + Settings.Climacell.PID.U;  // same PID values used for climacell & humidifiers
        PIDCtrl[PID_HUMIDIFIER].CurError = Settings.Plenum.HumidSet - Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_PLENUM_HUMID].Value;
        PIDVal = PIDController(PID_HUMIDIFIER);

        if (PIDVal >= 100)
        {
          HumidEquip->PIDValue = 100;
          HumidEquip->Control = HE_CYCLE_ON;
        }
        else if (PIDVal <= 0)
        {
          HumidEquip->PIDValue = 0;
          HumidEquip->Control = HE_CYCLE_OFF;
        }
        else
        {
          if (HumidEquip->PIDValue > PIDVal)
            Factor = -1;

          // check against previous value and restrict the adjustment size
          if (((PIDVal - HumidEquip->PIDValue) * Factor) > DUTYCYCLE_INC_VALUE)
            HumidEquip->PIDValue += (DUTYCYCLE_INC_VALUE * Factor);
          else
            HumidEquip->PIDValue = PIDVal;

          HumidEquip->Control = DutyCycle(HumidEquip->Control, HumidEquip->PIDValue, 100, &IntervalTimer[HumidEquip->Timer]);
        }
      }
    }
  }
  else
  {
    HumidEquip->Control = HE_CONTROL_OFF;
  }

  if (   SystemAlarm[HumidEquip->Alarm] != FM_NONE
      || *HumidEquip->RemoteOff == 1)
  {
    HumidEquip->Control = HE_CONTROL_OFF;
  }

  // set the outputs
  if (HumidEquip->Control == HE_CYCLE_OFF)
  {
    OutputOff(HumidEquip->Output);
  }
  else if (HumidEquip->Control == HE_CYCLE_ON)
  {
    OutputOn(HumidEquip->Output);
    OutputOn(HumidEquip->HeadOutput);
  }

  if (HumidEquip->Control == HE_CONTROL_OFF)
  {
    OutputOff(HumidEquip->Output);
    OutputOff(HumidEquip->HeadOutput);
    IntervalTimer[HumidEquip->Timer] = 0;
  }
} // end CtrlHumidifier()

/***************************************************************************

FUNCTION:   CtrlHumidifiersOff()

PURPOSE:    Turns the humidifiers off

COMMENTS:

***************************************************************************/
void CtrlHumidifiersOff(void)
{
  OutputOff(EQ_HUMID_PUMP1);
  OutputOff(EQ_HUMID_HEAD1);
  OutputOff(EQ_HUMID_PUMP2);
  OutputOff(EQ_HUMID_HEAD2);
  OutputOff(EQ_HUMID_PUMP3);
  OutputOff(EQ_HUMID_HEAD3);
} // end CtrlHumidifiersOff()

/***************************************************************************

FUNCTION:   CtrlOutsideAirBlend()

PURPOSE:    Control the outside air blend for cure mode

COMMENTS:   Onion only

***************************************************************************/
void CtrlOutsideAirBlend(int CureMode)
{
  float RefHumid = SENSOR_VAL_UNDEFINED;
  float TempDiff = 0.0;
  float HumidDiff = 0.0;

  // get the reference humidity
  if (Settings.Cure.HumidRef == 0)
  {
    if (!Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_PLENUM_HUMID].Disabled)
      RefHumid = Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_PLENUM_HUMID].Value;
  }
  else
    RefHumid = CalculatedHumidity();

  if (RefHumid != SENSOR_VAL_UNDEFINED)
  {
    if (CureMode == ST_AIRCURE)
    {
      // if temperature or humidity is outside the limits, close the doors
      if (   PlenumTempAvg <= Settings.Cure.TempLowLimit
          || RefHumid >= Settings.Cure.HumidHighLimit)
      {
        CtrlDoorsClose();
      }
      else
      {
        // the PID targets for the doors in air cure are 10% short of the limits
        TempDiff = PlenumTempAvg - (Settings.Cure.TempLowLimit * 1.10);
        HumidDiff = (Settings.Cure.HumidHighLimit * .90) - RefHumid;

        // use the lesser of the two differences to drive the doors so that either the
        // temperature or the humidity can close the doors
        if (TempDiff < HumidDiff)
          CtrlDoors(TempDiff, 0);
        else
          CtrlDoors(HumidDiff, 0);
      }
    }
    else
    {
      if (   Settings.Cure.StartTemp > Settings.Cure.TempLowLimit
          && (*OutsideTemp > Settings.Cure.TempLowLimit && *OutsideTemp != SENSOR_VAL_UNDEFINED))
      {
        // in burner mode determine the difference of the outside temperature and the low limit as a
        // percentage within the range from low limit to cure start temperature
        TempDiff = ((*OutsideTemp - Settings.Cure.TempLowLimit)/(Settings.Cure.StartTemp - Settings.Cure.TempLowLimit)) * 100.0;
      }

      if (   Settings.Cure.StartHumid < Settings.Cure.HumidHighLimit
          && RefHumid < Settings.Cure.HumidHighLimit)
      {
        // similarly determine the difference of the reference humidity and the high limit as a
        // percentage within the range of high limit to cure start humidity
        HumidDiff = ((Settings.Cure.HumidHighLimit - RefHumid)/(Settings.Cure.HumidHighLimit - Settings.Cure.StartHumid)) * 100.0;
      }

      // use the lesser of the two percentages to drive the doors so that either the
      // temperature or the humidity can close the doors
      if (TempDiff < HumidDiff)
        AdjustDoors(PercentToPWMVal(TempDiff));
      else
        AdjustDoors(PercentToPWMVal(HumidDiff));
    }
  }
  else
  {
    // if the reference humidity is bad
    CtrlDoorsClose();
  }
} // end CtrlOutsideAirBlend()

/***************************************************************************

FUNCTION:   CtrlPurge()

PURPOSE:    Control the CO2 purge

COMMENTS:   3/21/07
            Tested in none, manual, & auto modes
            Manual & auto during cooling - in and out of temp range
            Manual & auto during refrig - in and out of temp range and in range
            but refrig at +50%

***************************************************************************/
void CtrlPurge(void)
{
  unsigned int Counter = XTimerVal;

  // determine if a purge should/can be performed
  if (  (   Settings.Co2.Purge.Mode == 2    // automatic mode
         && CheckInputs(SW_FRESHAIR_AUTO)
         && Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_CO2].Type == ANALOG_SENSOR_TYPE_CO2
         && Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_CO2].Disabled == 0
         && Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_CO2].Value != SENSOR_VAL_UNDEFINED
         && Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_CO2].Value >= Settings.Co2.Set
//         && Counter - Settings.Co2.Purge.Last >= T_MINS) // DEMO
         && Counter - Settings.Co2.Purge.Last >= PURGE_AUTO_CYCLE_LIMIT * T_MINS)
      ||(   Settings.Co2.Purge.Mode == 1   // manual mode
         && Counter - Settings.Co2.Purge.Last >= Settings.Co2.CylceTime * T_HOURS)
      ||(   Settings.Co2.Purge.Start != 0  // purging
         && Settings.Co2.Purge.Mode != 0) ) // mode none (stops the purge)
  {
    // don't allow or stop the purge
    if (   *OutsideTemp > Settings.Co2.Purge.MaxTemp                                            // too warm outside
        || *OutsideTemp < Settings.Co2.Purge.MinTemp                                            // too cold outside
        || *OutsideTemp == SENSOR_VAL_UNDEFINED                                                 // don't know
        || PlenumTempAvg < (Settings.Plenum.TempSet - PURGE_TEMP_LIMIT)                         // driving the plenum temp low
        || PlenumTempAvg > (Settings.Plenum.TempSet + PURGE_TEMP_LIMIT)                         // driving the plenum temp high
        ||(   (Settings.Co2.Purge.Start != 0)                                                   // purging
           && (Counter - Settings.Co2.Purge.Start >= Settings.Co2.Purge.Duration * T_MINS) )    // and it has finished
        ||(   (SystemState == ST_REFRIG || SystemState == ST_REFRIGDEHUMID)
           && (PWMValToPercent(PwmChannel[PWM_REFRIGERATION].Output) > Settings.Co2.Purge.RefrigThresh) ) // refrig output too high
        ||(   (Settings.Co2.Purge.Mode == 2)                                                    // automatic mode & sensor went bad
           && (Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_CO2].Value == SENSOR_VAL_UNDEFINED) ) )
    {
      PurgeOff();
    }
    else
    {
      PurgeOn();
    }
  }
  else
  {
    PurgeOff();
  }
} // end CtrlPurge()

/***************************************************************************

FUNCTION:   CtrlRefrig()

PURPOSE:    Turn the refrigeration outputs on and off

COMMENTS:   3/21/07
            Tested remote off for each stage with multiple combinations
            tested with alarm

***************************************************************************/
void CtrlRefrig(float ActualTemp, float TargetTemp)
{
  int PWM = 0;
  int RefrigPercent = 0;
  unsigned int Counter = XTimerVal;

  // check for failure
  if (   SystemAlarm[AL_REFRIGERATION] != FM_NONE
      && SystemAlarm[AL_REFRIGERATION] != FM_PRELIM)
  {
    CtrlRefrigOff(Settings.Refrig.FailMode == 2);
    if (Settings.Refrig.FailMode != 2) {
      return;
    }
  }

  /* Nova-architecture output-path gate (2026-06-02). See CtrlDoors
   * (below) for the full rationale. Refrig gate uses STAGE1 as the
   * canonical pulse-coil check (the AS2 convention — if the operator
   * assigned STAGE1 they almost certainly assigned the other stages
   * they need; this matches the legacy gate's intent). AO path:
   * AO_EQUIP_REFRIG = 3 (a single 0-100% refrig demand driving a
   * staged controller, e.g. condenser-fan VFD or compressor VSD). */
  if (Settings.EquipIo[EQ_REFRIG_STAGE1].Output == IO_UNDEFINED
      && !nova_any_ao_assigned_for(AO_EQUIP_REFRIG))
  {
    WarningsSet(WARN_NO_OUTPUT, FM_ALARM, NA, EQ_REFRIGERATION);
    return;
  }

  // PID controller (update interval based on "U" value)
  if (PIDCtrl[PID_REFRIGERATION].Timer == 0)
  {
    PIDCtrl[PID_REFRIGERATION].Timer = Counter + Settings.Refrig.PID.U;
  }

  if (Counter < PIDCtrl[PID_REFRIGERATION].Timer)
  {
    return;
  }

  PIDCtrl[PID_REFRIGERATION].Timer = Counter + Settings.Refrig.PID.U;

  // calculate the error
  PIDCtrl[PID_REFRIGERATION].CurError = ActualTemp - TargetTemp;
  if (Settings.TempType == 1)  // if �C convert to �F
  {
    PIDCtrl[PID_REFRIGERATION].CurError *= 1.8;
  }

  // call the PID controller
  PWM = PIDController(PID_REFRIGERATION);

  // check against previous value and restrict the adjustment size
  if (PwmChannel[PWM_REFRIGERATION].Output < PWM)
  {
    if ((PWM - PwmChannel[PWM_REFRIGERATION].Output) > PWM_INC_VALUE)
    {
      PwmChannel[PWM_REFRIGERATION].Output += PWM_INC_VALUE;
    }
    else
    {
      PwmChannel[PWM_REFRIGERATION].Output = PWM;
    }
  }
  else
  {
    if ((PwmChannel[PWM_REFRIGERATION].Output - PWM) > PWM_INC_VALUE)
    {
      PwmChannel[PWM_REFRIGERATION].Output -= PWM_INC_VALUE;
    }
    else
    {
      PwmChannel[PWM_REFRIGERATION].Output = PWM;
    }
  }

  if (PwmChannel[PWM_REFRIGERATION].Output > PWM_MAX_VALUE)
  {
    PwmChannel[PWM_REFRIGERATION].Output = PWM_MAX_VALUE;
  }
  if (PwmChannel[PWM_REFRIGERATION].Output < PWM_MIN_VALUE)
  {
    PwmChannel[PWM_REFRIGERATION].Output = PWM_MIN_VALUE;
  }

  // set the refrig PWM
  PWM_UpdateChannel(PWM_REFRIGERATION);
  RefrigPercent = PWMValToPercent(PwmChannel[PWM_REFRIGERATION].Output);

  // Allow the above to run for continue in refrigeration but return here
  // to avoid setting stages
  if (   SystemAlarm[AL_REFRIGERATION] != FM_NONE
      && SystemAlarm[AL_REFRIGERATION] != FM_PRELIM) {
    return;
  }

  // turn on the appropriate refrig stages
  int i;
  for (i = 0; i < NUM_REFRIG_STAGES; ++i)
  {
    if (Settings.Refrig.Stage[i].Diagnostic == 0)   // if it's not in diag mode
    {
      if (RefrigPercent >= Settings.Refrig.Stage[i].On)
      {
        // check for failure
        if (SystemAlarm[AL_REFRIG_STAGE1 + i] == FM_NONE)
        {
          OutputOn((EQUIPMENT_IO) (EQ_REFRIG_STAGE1 + i));
        }
        else
        {
          OutputOff((EQUIPMENT_IO) (EQ_REFRIG_STAGE1 + i));
        }
      }
      else if (RefrigPercent < Settings.Refrig.Stage[i].Off)
      {
        OutputOff((EQUIPMENT_IO) (EQ_REFRIG_STAGE1 + i));
      }
    }
  }
} // end CtrlRefrig()

/***************************************************************************

FUNCTION:   CtrlRefrigDiag()

PURPOSE:    Turn the refrigeration stages on and off for diagnostic mode

COMMENTS:   Diagnostic mode allows each refrigeration stage to be turned on
            or off manually and independently from the UI.  Diagnostic mode
            runs on timer that will return control to automatic after 60 min.
            The timer gets started/restarted in StoreRefrigDiag().

***************************************************************************/
void CtrlRefrigDiag(void)
{
  unsigned int Counter = XTimerVal;

//  if (   !FanOn()
//      || !CheckInputs(SS_IN_REFRIGSTANDBY))

  if (SystemState != ST_REFRIG)
    return;

  int i;
  for (i = 0; i < NUM_REFRIG_STAGES; ++i)
  {
    if (Settings.Refrig.Stage[i].Diagnostic == 1)
      OutputOff((EQUIPMENT_IO) (EQ_REFRIG_STAGE1 + i));
    else if (Settings.Refrig.Stage[i].Diagnostic == 2)
      OutputOn((EQUIPMENT_IO) (EQ_REFRIG_STAGE1 + i));
  }

  for (i = 0; i < NUM_DEFROST_STAGES; ++i)
  {
    if (Settings.Refrig.Defrost[i].Diagnostic == 1)
      OutputOff((EQUIPMENT_IO) (EQ_REFRIG_DEFROST1 + i));
    else if (Settings.Refrig.Defrost[i].Diagnostic == 2)
      OutputOn((EQUIPMENT_IO) (EQ_REFRIG_DEFROST1 + i));
  }

  // clear diagnostic mode if timer has elapsed
  if (Counter - IntervalTimer[IT_REFRIGDIAG] >= 60 * T_MINS)
    CtrlRefrigDiagClear();
} // end CtrlRefrigDiag()

/***************************************************************************

FUNCTION:   CtrlRefrigDiagClear()

PURPOSE:    Clear/stop the refrigeration diagnostic mode

COMMENTS:

***************************************************************************/
void CtrlRefrigDiagClear(void)
{
  int i;
  for (i = 0; i < NUM_REFRIG_STAGES; i++)
  {
    Settings.Refrig.Stage[i].Diagnostic = 0;
  }

  for (i = 0; i < NUM_DEFROST_STAGES; i++)
  {
    Settings.Refrig.Defrost[i].Diagnostic = 0;
  }
} // end CtrlRefrigDiagClear()

/***************************************************************************

FUNCTION:   CtrlRefrigOff()

PURPOSE:    Turn off the refrigeration outputs

COMMENTS:

***************************************************************************/
void CtrlRefrigOff(bool continueRefrigeration)
{
//  PWM_UpdateChannel(Settings.PWM_refrig, PWMValue.Refrig);

//  if (   !FanOn()
//      || !CheckInputs(SS_IN_REFRIGSTANDBY)
//      || Settings.Refr[5].Diagnostic != 2)
//    OutputOff(EQ_REFRIG_STAGE6);

  if (!continueRefrigeration) {
    PwmChannel[PWM_REFRIGERATION].Output = PWM_MIN_VALUE;
    PWM_UpdateChannel(PWM_REFRIGERATION);
    PIDClear(PID_REFRIGERATION);
  }
  int i;
  for (i = 0; i < NUM_REFRIG_STAGES; ++i)
  {
    OutputOff((EQUIPMENT_IO) (EQ_REFRIG_STAGE1 + i));
  }
} // end CtrlRefrigOff()

/***************************************************************************

FUNCTION:   DutyCycle()

PURPOSE:    Determine a duty cycle state for a control process

COMMENTS:   3/21/07
            Tested with several different duty cycles

***************************************************************************/
int DutyCycle(int State, int DutyCycle, int Period, unsigned int *Timer)
{
  int Cycle = State;
  int CycleDur = 0;
  unsigned int Counter = XTimerVal;

  if (*Timer == 0)   // just starting
  {
    Cycle = 1;
    *Timer = Counter;
  }
  else
  {
    if (DutyCycle == 100)
      Cycle = 1;
    else if (DutyCycle == 0)
      Cycle = 0;
    else
    {
      // determine the current cycle's duration
      CycleDur = Period * ((float)DutyCycle/100.0);
      if (CycleDur < 1)
        CycleDur = 1;

      // if the state is off, get the off part of the period
      if (State == 0)
        CycleDur = Period - CycleDur;

      // if the cycle timer has elapsed, switch the cycle state
      if (Counter - *Timer > CycleDur)
      {
        if (State == 0)
          Cycle = 1;
        else
          Cycle = 0;
        *Timer = Counter;
      }
    }
  }
  return(Cycle);
} // end DutyCycle()

/***************************************************************************

FUNCTION:   FanBoostOff()

PURPOSE:    Stop the fan boost cycle

COMMENTS:

***************************************************************************/
void FanBoostOff(void)
{
  if (IntervalTimer[IT_FANBOOSTCYCLE] != 0)
  {
    // clear the boosting flag
    IntervalTimer[IT_FANBOOSTCYCLE] = 0;
    IntervalTimer[IT_FANCONTINUOUSTIME] = 0;
//    IntervalTimer[IT_FANBOOSTINTERVAL] = XTimerVal;   // TODO - clb: hmmm...

    if (   SystemState == ST_COOLING
        || SystemState == ST_COOLDEHUMID) {
        CtrlFan(Settings.Fan.PrevSpeed);
    }
    if (SystemState == ST_HEATING) {
        CtrlFan(OldFanSpeedHeater);
    }
  }
} // end FanBoostOff()

/***************************************************************************

FUNCTION:   PercentToPWMVal()

PURPOSE:    Given a percent, return a PWM value

COMMENTS:

***************************************************************************/
unsigned int PercentToPWMVal(int percent)
{
  if (percent < 0)
    percent = 0;
  if (percent > 100)
    percent = 100;

  return(((PWM_RANGE * percent)/100) + PWM_MIN_VALUE);
} // end PercentToPWMVal()

/***************************************************************************

FUNCTION:   PID_Init()

PURPOSE:    Initialize the PID settings

COMMENTS:

***************************************************************************/
void PID_Init(void)
{
  PIDCtrl[PID_DOOR].Kp = Settings.Door.PID.P;
  PIDCtrl[PID_DOOR].Ki = Settings.Door.PID.I;
  PIDCtrl[PID_DOOR].Kd = Settings.Door.PID.D;
  PIDCtrl[PID_DOOR].Timer = 0;
  PIDCtrl[PID_DOOR].RangeMax = PWM_MAX_VALUE;
  PIDCtrl[PID_DOOR].RangeMin = PWM_MIN_VALUE;
  PIDCtrl[PID_DOOR].PrevError = 0xFFFFFFFF;

  PIDCtrl[PID_REFRIGERATION].Kp = Settings.Refrig.PID.P;
  PIDCtrl[PID_REFRIGERATION].Ki = Settings.Refrig.PID.I;
  PIDCtrl[PID_REFRIGERATION].Kd = Settings.Refrig.PID.D;
  PIDCtrl[PID_REFRIGERATION].Timer = 0;
  PIDCtrl[PID_REFRIGERATION].RangeMax = PWM_MAX_VALUE;
  PIDCtrl[PID_REFRIGERATION].RangeMin = PWM_MIN_VALUE;
  PIDCtrl[PID_REFRIGERATION].PrevError = 0xFFFFFFFF;

  PIDCtrl[PID_CLIMACELL].Kp = Settings.Climacell.PID.P;
  PIDCtrl[PID_CLIMACELL].Ki = Settings.Climacell.PID.I;
  PIDCtrl[PID_CLIMACELL].Kd = Settings.Climacell.PID.D;
  PIDCtrl[PID_CLIMACELL].Timer = 0;
  PIDCtrl[PID_CLIMACELL].RangeMax = 100;
  PIDCtrl[PID_CLIMACELL].RangeMin = 0;
  PIDCtrl[PID_CLIMACELL].PrevError = 0xFFFFFFFF;

  PIDCtrl[PID_HUMIDIFIER].Kp = Settings.Climacell.PID.P;    // same PID values used for climacell & humidifiers
  PIDCtrl[PID_HUMIDIFIER].Ki = Settings.Climacell.PID.I;
  PIDCtrl[PID_HUMIDIFIER].Kd = Settings.Climacell.PID.D;
  PIDCtrl[PID_HUMIDIFIER].Timer = 0;
  PIDCtrl[PID_HUMIDIFIER].RangeMax = 100;
  PIDCtrl[PID_HUMIDIFIER].RangeMin = 0;
  PIDCtrl[PID_HUMIDIFIER].PrevError = 0xFFFFFFFF;

  PIDCtrl[PID_BURNER].Kp = Settings.Burner.PID.P;
  PIDCtrl[PID_BURNER].Ki = Settings.Burner.PID.I;
  PIDCtrl[PID_BURNER].Kd = Settings.Burner.PID.D;
  PIDCtrl[PID_BURNER].Timer = 0;
  PIDCtrl[PID_BURNER].RangeMax = PWM_MAX_VALUE;
  PIDCtrl[PID_BURNER].RangeMin = PWM_MIN_VALUE;
  PIDCtrl[PID_BURNER].PrevError = 0xFFFFFFFF;
} // end PID_Init()

/***************************************************************************

FUNCTION:   PIDClear()

PURPOSE:    Clear the PID error values

COMMENTS:

***************************************************************************/
void PIDClear(PID_EQ pidIndex)
{
  PIDCtrl[pidIndex].CurError = 0;
  PIDCtrl[pidIndex].PrevError = 0xFFFFFFFF;
  PIDCtrl[pidIndex].IntError = 0;
} // end PIDClear()

/***************************************************************************

FUNCTION:   PIDController()

PURPOSE:    Determine PID value

COMMENTS:   The WindupLimit is scaled based on the 'I' value entered in the UI
            which is stored as pid->Ki (the K(i) constant).  The WindupLimit
            is inversely proportional to Ki to allow the integral error
            (pid->IntError) to increase enough to allow 'I' to increase to
            the value of range (100) to allow the output (P+I+D) to increase
            to range (100) to saturate the output when 'P' and 'D' are small.

            Small Ki values dampen (slow down) the response of the controller.

***************************************************************************/
int PIDController(PID_EQ pidIndex)
{
  float P = 0.0;
  float I = 0.0;
  float D = 0.0;
  float DiffError = 0.0;
  float Scalar = 0.022;
  int   IntErr = 0;
  int   Output = 0;
  PID_CTRL *pid = &PIDCtrl[pidIndex];
  int   Range = pid->RangeMax - pid->RangeMin;
  float WindupLimit = Range/(Scalar * pid->Ki);

  // round the error
  IntErr = (pid->CurError + .05) * 10;
  pid->CurError = (float)IntErr / 10.0;

  // integrate/sum the error values
  pid->IntError += pid->CurError;

  // limit the integral windup
  if (pid->IntError > WindupLimit)
    pid->IntError = WindupLimit;
  if (pid->IntError < 0.0)
    pid->IntError = 0.0;

  // determine the differential error (ignore the diff error the first time)
  if (pid->PrevError != 0xFFFFFFFF)
    DiffError = pid->CurError - pid->PrevError;
  pid->PrevError = pid->CurError;

  // determine the PID values
  P = pid->Kp * pid->CurError;
  I = pid->Ki * pid->IntError * Scalar;    // scaling for granularity in the UI
  D = pid->Kd * DiffError;

  // calculate the output
  Output = P + I + D;

  // adjust the output range
  if (Output > Range)
    Output = Range;
  else if (Output < 0)
    Output = 0;

  // log the values
  if (   (pidIndex == PID_DOOR && Settings.Log.PID.Door == 1)
      || (pidIndex == PID_REFRIGERATION && Settings.Log.PID.Refrig == 1))
  {
    PIDLogWrite(pidIndex, P, I, D, Output, pid->CurError);
  }

  // shift the output into the defined range
  Output += pid->RangeMin;

  return Output;
} // end PIDController()

/***************************************************************************

FUNCTION:   PurgeOn()

PURPOSE:    Set the outputs to perform the purge

COMMENTS:   3/21/07
            Tested with CtrlPurge

***************************************************************************/
void PurgeOn(void)
{
  if (Settings.Co2.Purge.Start == 0)  // if not purging (safety)
  {
    FanBoostOff();

    //  OldRefrigPWMValue = PWMValue.Refrig;
    OldDoorPWMValue = PwmChannel[PWM_DOORS].Output;
    OldFanSpeedHeater = PWMValToPercent(PwmChannel[PWM_FAN].Output);
    if (AdjustDoors(PercentToPWMVal(Settings.Co2.DoorOutput)) == 1)
    {
      CtrlFan(Settings.Co2.FanOutput);

      Settings.Co2.Purge.Start = XTimerVal;  // start counter
//      Settings.Co2LastPurge = XTimerVal;
    }
  }
} // end PurgeOn()

/***************************************************************************

FUNCTION:   PurgeOff()

PURPOSE:    Stop the CO2 purge

COMMENTS:   3/21/07
            Tested with CtrlPurge

***************************************************************************/
void PurgeOff(void)
{
  if (Settings.Co2.Purge.Start != 0)  // if purging (safety)
  {
    // clear the purging flag
    Settings.Co2.Purge.Start = 0;
    Settings.Co2.Purge.Last = XTimerVal;

    if (   SystemState == ST_COOLING
        || SystemState == ST_COOLDEHUMID)
    {
      AdjustDoors(OldDoorPWMValue);
      // go back to the previous fan speed
      CtrlFan(Settings.Fan.PrevSpeed);
    }
    if (SystemState == ST_HEATING) {
        CtrlFan(OldFanSpeedHeater);
    }
  }
} // end PurgeOff()

/***************************************************************************

FUNCTION:   PWMValToPercent()

PURPOSE:    Return the PWM value as a percent

COMMENTS:

***************************************************************************/
int PWMValToPercent(unsigned int PWM)
{
  int rounder = (((PWM - PWM_MIN_VALUE)*1000)/PWM_RANGE) + 5;
  return (rounder/10);
} // end PWMValToPercent()

/***************************************************************************

FUNCTION:   ApplyManualOverrides()

PURPOSE:    Force equipment ON when RemoteOff == 2 (Manual).
            Called after mode processing so manual works even in standby.
            Skipped during ST_SHUTDOWN and ST_SYSTEM_REMOTEOFF (E-Stop).

COMMENTS:   Replaces the panel-face switches that were in the CPLD.

***************************************************************************/
void ApplyManualOverrides(void)
{
  int i;

  // Fan
  if (Settings.RemoteOff[RO_FAN] == 2)
  {
    if (!CheckOutputs(EQ_FAN))
    {
      OutputOn(EQ_FAN);
      PwmChannel[PWM_FAN].Output = PWM_MAX_VALUE;
      PWM_UpdateChannel(PWM_FAN);
    }
  }

  // Heat
  if (Settings.RemoteOff[RO_HEAT] == 2)
    OutputOn(EQ_HEAT);

  // Cavity Heat
  if (   Settings.RemoteOff[RO_CAVITY_HEAT] == 2
      && Settings.EquipIo[EQ_CAVITY_HEAT].Enabled != 0
      && SystemAlarm[AL_CAVITYHEAT] == FM_NONE)
    OutputOn(EQ_CAVITY_HEAT);

  // Burner
  if (   Settings.RemoteOff[RO_BURNER] == 2
      && SystemAlarm[AL_BURNER] == FM_NONE
      && SystemAlarm[AL_HIGHPLENTEMP] == FM_NONE)
    OutputOn(EQ_BURNER);

  // Climacell
  if (   Settings.RemoteOff[RO_CLIMACELL] == 2
      && SystemAlarm[AL_CLIMACELL] == FM_NONE)
    OutputOn(EQ_CLIMACELL);

  // Humidifier 1
  if (   Settings.RemoteOff[RO_HUMIDIFIER1] == 2
      && SystemAlarm[AL_HUMID1] == FM_NONE)
  {
    OutputOn(EQ_HUMID_PUMP1);
    OutputOn(EQ_HUMID_HEAD1);
  }

  // Humidifier 2
  if (   Settings.RemoteOff[RO_HUMIDIFIER2] == 2
      && SystemAlarm[AL_HUMID2] == FM_NONE)
  {
    OutputOn(EQ_HUMID_PUMP2);
    OutputOn(EQ_HUMID_HEAD2);
  }

  // Humidifier 3
  if (   Settings.RemoteOff[RO_HUMIDIFIER3] == 2
      && SystemAlarm[AL_HUMID3] == FM_NONE)
  {
    OutputOn(EQ_HUMID_PUMP3);
    OutputOn(EQ_HUMID_HEAD3);
  }

  // Bay Lights
  if (Settings.RemoteOff[RO_LIGHTS1] == 2)
    OutputOn(EQ_LIGHTS1);

  if (Settings.RemoteOff[RO_LIGHTS2] == 2)
    OutputOn(EQ_LIGHTS2);

  // Aux 1-8
  for (i = 0; i < NUM_AUX_OUTPUTS; i++)
  {
    if (   Settings.RemoteOff[RO_AUX1 + i] == 2
        && SystemAlarm[AL_AUX1 + i] == FM_NONE)
    {
      OutputOn((EQUIPMENT_IO)(EQ_AUX1 + i));
    }
  }
}

/***   End Of File   ***/
