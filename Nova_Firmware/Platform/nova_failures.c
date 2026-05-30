/* nova_failures.c
 *
 * Nova-native implementation of the system failure checks.
 *
 * MIGRATION STATUS (Phase 2 of legacy → Nova-native):
 *   This module replaces docs/legacy_AS2_reference/Application/Failures.c
 *   in the Nova firmware build.  See /memories/repo/legacy-migration-plan.md.
 *
 *   Public API is `int SystemFailuresChk(void)` (declared in legacy
 *   Failures.h).  Called once per fast-tick from legacy States.c — and
 *   eventually from the Nova-native states module after Phase 3+.
 *
 * Behaviour preserved bit-for-bit vs. legacy Failures.c with ONE
 * deliberate addition:
 *
 *   • FM_LIGHTSOFF=3 ("3-way switch toggle") in BayLightsMonitor.
 *     Legacy AS2 silently no-op'd mode==3.  Nova implements it as: when
 *     bay-lights detect-input still shows ON past the programmed timer,
 *     flip Settings.RemoteOff[RO_LIGHTSn] between 0 (auto / commanded
 *     on) and 1 (commanded off) — emulating someone flipping a 3-way
 *     wall switch.  Re-arms after each toggle; raises NO warning.
 *     Value 2 ("forced on") is normalised to 0 before flipping.
 *
 * No #ifdef CONSTELLATION_NOVA, no weak hooks, no legacy build flags.
 * This file is Nova-native and stays under Nova_Firmware/Platform/.
 */

#include <stdbool.h>
#include <stdint.h>

#include "Analog_Input.h"
#include "Controls.h"
#include "Failures.h"
#include "PWM.h"
#include "SerialShift.h"
#include "Settings.h"
#include "States.h"
#include "Timer.h"
#include "Warnings.h"
#include "nova_vfd.h"

/* FM_LIGHTSOFF — Nova-only failure mode for FAIL_LIGHTS slot.
 * Legacy enum: FM_NONE=0, FM_ALARM=1, FM_FAIL=2.  We extend with 3.
 * The proto/UI already accepts and persists this value (see
 * apply_failures case 12 in nova_dataexc.c). */
#define FM_LIGHTSOFF              3

/*** module variables ***/
static float HumidVarChkValue = 0;

/*** static fail-check functions ***/
static int  AirFlowFailChk(void);
static int  AuxFailChk(void);
static int  AuxLowPlenumTempFailChk(void);
static int  BayLightsMonitor(EQUIPMENT_IO io, EQUIP_ALARMS alarm);
static int  BurnerFailChk(void);
static int  CavFailChk(void);
static int  ClimacellFailChk(void);
static int  CO2FailChk(void);
static int  FanFailChk(void);
static int  HeatFailChk(void);
static int  HumidFailChk(EQUIPMENT_IO io, EQUIP_ALARMS alarm);
static void MasterBroadcastFailChk(void);
static int  OutsideHumidSensorFailChk(void);
static int  OutsideHumidVarFailChk(void);
static int  OutsideTempSensorFailChk(void);
static int  PlenumHumidFailChk(void);
static int  PlenumSensorFailChk(void);
static int  PlenumTempFailChk(void);
static int  PlenumTempFailCureModeHighChk(void);
static int  PlenumTempFailCureModeLowChk(void);
static int  PlenumTempFailHighChk(void);
static int  PlenumTempFailLowChk(void);
static int  RefrigFailChk(void);
static int  Refrig420Chk(void);
static int  RefrigStageChk(int io, int alarm);

/* ─── Air flow ─────────────────────────────────────────────────────────── */
static int AirFlowFailChk(void)
{
    if (CheckInputs(EQ_AIR_FLOW)
        || SystemAlarm[AL_AIRFLOW] == FM_FAIL) {
        SystemAlarm[AL_AIRFLOW] = FM_FAIL;
        WarningsSet(WARN_AIRFLOW, FM_ALARM, FM_FAIL, NA);
        return 1;
    }
    return 0;
}

/* ─── Auxiliary outputs ────────────────────────────────────────────────── */
static int AuxFailChk(void)
{
    int failures = 0;
    unsigned int Counter = XTimerVal;

    for (int i = 0; i < NUM_AUX_OUTPUTS; ++i) {
        if (((Settings.Failure[FAIL_AUXILIARY].Mode != FM_NONE
              && (CheckOutputs(EQ_AUX1 + i) || SystemAlarm[AL_AUX1 + i] != FM_NONE)
              && CheckInputs(EQ_AUX1 + i)))
            || SystemAlarm[AL_AUX1 + i] == FM_FAIL) {
            if (AlarmTimer[AL_AUX1 + i] == 0) {
                AlarmTimer[AL_AUX1 + i] = Counter;
            } else if (Counter - AlarmTimer[AL_AUX1 + i]
                       >= Settings.Failure[FAIL_AUXILIARY].Timer * T_MINS) {
                WarningsSet(WARN_AUX, FM_ALARM,
                            Settings.Failure[FAIL_AUXILIARY].Mode,
                            EQ_AUX1 + i);
                SystemAlarm[AL_AUX1 + i] = Settings.Failure[FAIL_AUXILIARY].Mode;
                if (Settings.Failure[FAIL_AUXILIARY].Mode == FM_FAIL) {
                    failures++;
                }
            }
        } else {
            SystemAlarm[AL_AUX1 + i] = FM_NONE;
            AlarmTimer[AL_AUX1 + i]  = 0;
        }
    }
    return failures;
}

/* ─── Aux low plenum temp (latched until start/stop cycle) ─────────────── */
static int AuxLowPlenumTempFailChk(void)
{
    if (CheckInputs(EQ_LOW_TEMP) || SystemAlarm[AL_AUXLOWPLENTEMP] == FM_FAIL) {
        SystemAlarm[AL_AUXLOWPLENTEMP] = FM_FAIL;
        WarningsSet(WARN_AUXLOWPLENTEMP, FM_ALARM, FM_FAIL, NA);
        return 1;
    }
    return 0;
}

/* ─── Bay-lights monitor (mode 1 = legacy alarm, mode 3 = Nova toggle) ─── */
static int BayLightsMonitor(EQUIPMENT_IO io, EQUIP_ALARMS alarm)
{
    unsigned int Counter = XTimerVal;
    uint8_t mode = Settings.Failure[FAIL_LIGHTS].Mode;

    /* Mode 3: 3-way switch toggle (Nova-only).  When the bay-lights
     * detect-input shows lights still on past the timer, flip
     * RemoteOff[RO_LIGHTSn] (0↔1) so CtrlBayLights commands the
     * opposite state next pass.  Treat value 2 ("forced on") as 0
     * before flipping.  Re-arms after each toggle so it keeps
     * trying until the input clears.  No warning is raised. */
    if (mode == FM_LIGHTSOFF) {
        if (CheckInputs(io)) {
            if (AlarmTimer[alarm] == 0) {
                AlarmTimer[alarm] = Counter;
            } else if (Counter - AlarmTimer[alarm]
                       >= Settings.Failure[FAIL_LIGHTS].Timer
                          * Settings.LightsFailUnits * T_MINS) {
                uint8_t ro_idx = (io == EQ_LIGHTS2) ? RO_LIGHTS2 : RO_LIGHTS1;
                uint8_t cur = Settings.RemoteOff[ro_idx];
                if (cur == 2) cur = 0;             /* normalise forced-on */
                Settings.RemoteOff[ro_idx] = (cur == 0) ? 1 : 0;
                AlarmTimer[alarm] = 0;             /* re-arm */
            }
        } else {
            AlarmTimer[alarm] = 0;
        }
        SystemAlarm[alarm] = FM_NONE;
        return 0;
    }

    /* Legacy mode 1 (FM_ALARM): raise WARN_LIGHTS when timer elapses.
     * Failure mode (FM_FAIL=2) is not allowed for bay-lights — legacy
     * comment: "failure is not allowed for bay light monitor". */
    if (mode == FM_ALARM && CheckInputs(io)) {
        if (AlarmTimer[alarm] == 0) {
            AlarmTimer[alarm] = Counter;
        } else if (Counter - AlarmTimer[alarm]
                   >= Settings.Failure[FAIL_LIGHTS].Timer
                      * Settings.LightsFailUnits * T_MINS) {
            WarningsSet(WARN_LIGHTS, FM_ALARM, mode, io);
            SystemAlarm[alarm] = mode;
        }
    } else {
        SystemAlarm[alarm] = FM_NONE;
        AlarmTimer[alarm]  = 0;
    }
    return 0;
}

/* ─── Burner ───────────────────────────────────────────────────────────── */
static int BurnerFailChk(void)
{
    unsigned int Counter = XTimerVal;

    if (((Settings.Failure[FAIL_BURNER].Mode != FM_NONE
          && CheckInputs(SW_BURNER_AUTO)
          && (CheckOutputs(EQ_BURNER) || SystemAlarm[AL_BURNER] != FM_NONE)
          && CheckInputs(EQ_BURNER)))
        || SystemAlarm[AL_BURNER] == FM_FAIL) {
        if (AlarmTimer[AL_BURNER] == 0) {
            AlarmTimer[AL_BURNER] = Counter;
        } else if (Counter - AlarmTimer[AL_BURNER]
                   >= Settings.Failure[FAIL_BURNER].Timer * T_MINS) {
            WarningsSet(WARN_BURNER, FM_ALARM,
                        Settings.Failure[FAIL_BURNER].Mode, NA);
            SystemAlarm[AL_BURNER] = Settings.Failure[FAIL_BURNER].Mode;
            if (Settings.Failure[FAIL_BURNER].Mode == FM_FAIL) return 1;
        }
    } else {
        SystemAlarm[AL_BURNER] = FM_NONE;
        AlarmTimer[AL_BURNER]  = 0;
    }
    return 0;
}

/* ─── Cavity heat ──────────────────────────────────────────────────────── */
static int CavFailChk(void)
{
    unsigned int Counter = XTimerVal;

    if (((Settings.Failure[FAIL_CAVITY_HEAT].Mode != FM_NONE
          && (CheckOutputs(EQ_CAVITY_HEAT) || SystemAlarm[AL_CAVITYHEAT] != FM_NONE)
          && CheckInputs(EQ_CAVITY_HEAT)))
        || SystemAlarm[AL_CAVITYHEAT] == FM_FAIL) {
        if (AlarmTimer[AL_CAVITYHEAT] == 0) {
            AlarmTimer[AL_CAVITYHEAT] = Counter;
        } else if (Counter - AlarmTimer[AL_CAVITYHEAT]
                   >= Settings.Failure[FAIL_CAVITY_HEAT].Timer * T_MINS) {
            WarningsSet(WARN_CAVITYHEAT, FM_ALARM,
                        Settings.Failure[FAIL_CAVITY_HEAT].Mode, NA);
            SystemAlarm[AL_CAVITYHEAT] = Settings.Failure[FAIL_CAVITY_HEAT].Mode;
            if (Settings.Failure[FAIL_CAVITY_HEAT].Mode == FM_FAIL) return 1;
        }
    } else {
        SystemAlarm[AL_CAVITYHEAT] = FM_NONE;
        AlarmTimer[AL_CAVITYHEAT]  = 0;
    }
    return 0;
}

/* ─── Climacell ────────────────────────────────────────────────────────── */
static int ClimacellFailChk(void)
{
    unsigned int Counter = XTimerVal;

    if (((Settings.Failure[FAIL_CLIMACELL].Mode != FM_NONE
          && CheckInputs(SW_CLIMACELL_AUTO)
          && (CheckOutputs(EQ_CLIMACELL) || SystemAlarm[AL_CLIMACELL] != FM_NONE)
          && CheckInputs(EQ_CLIMACELL)))
        || SystemAlarm[AL_CLIMACELL] == FM_FAIL) {
        if (AlarmTimer[AL_CLIMACELL] == 0) {
            AlarmTimer[AL_CLIMACELL] = Counter;
        } else if (Counter - AlarmTimer[AL_CLIMACELL]
                   >= Settings.Failure[FAIL_CLIMACELL].Timer * T_MINS) {
            WarningsSet(WARN_CLIMACELL, FM_ALARM,
                        Settings.Failure[FAIL_CLIMACELL].Mode, NA);
            SystemAlarm[AL_CLIMACELL] = Settings.Failure[FAIL_CLIMACELL].Mode;
            if (Settings.Failure[FAIL_CLIMACELL].Mode == FM_FAIL) return 1;
        }
    } else {
        SystemAlarm[AL_CLIMACELL] = FM_NONE;
        AlarmTimer[AL_CLIMACELL]  = 0;
    }
    return 0;
}

/* ─── CO2 high ─────────────────────────────────────────────────────────── */
static int CO2FailChk(void)
{
    unsigned int Counter = XTimerVal;

    if (((Settings.Failure[FAIL_HIGH_CO2].Mode != FM_NONE
          && Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Present == 1
          && Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_CO2].Type == ANALOG_SENSOR_TYPE_CO2
          && Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_CO2].Disabled == 0
          && Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_CO2].Value
             > Settings.Co2.HighFailure))
        || SystemAlarm[AL_HIGHCO2] == FM_FAIL) {
        if (AlarmTimer[AL_HIGHCO2] == 0) {
            AlarmTimer[AL_HIGHCO2] = Counter;
        } else if (Counter - AlarmTimer[AL_HIGHCO2]
                   >= Settings.Failure[FAIL_HIGH_CO2].Timer * T_MINS) {
            WarningsSet(WARN_HIGHCO2, FM_ALARM,
                        Settings.Failure[FAIL_HIGH_CO2].Mode, NA);
            SystemAlarm[AL_HIGHCO2] = Settings.Failure[FAIL_HIGH_CO2].Mode;
            if (Settings.Failure[FAIL_HIGH_CO2].Mode == FM_FAIL) return 1;
        }
    } else {
        SystemAlarm[AL_HIGHCO2] = FM_NONE;
        AlarmTimer[AL_HIGHCO2]  = 0;
    }
    return 0;
}

/* ─── Fan ──────────────────────────────────────────────────────────────── */
/* Fan-failure condition is the OR of two sources:
 *
 *  1. Legacy contactor feedback path — `CheckInputs(EQ_FAN)` indicates a
 *     wired fan-proof input is asserting failure (or `SystemAlarm[AL_FAN]`
 *     was already latched at FM_FAIL).
 *  2. VFD path — any drive on a STORAGE-role orbit reports its fault
 *     bit set (`nova_vfd_any_faulted()`). The bridge's vfdStatus proto
 *     stream provides per-drive fault detail to the UI; the firmware
 *     decision (escalate or not) lives here so the system stays safe
 *     even if the bridge / RPi5 is offline.
 *
 * In BOTH cases the existing `Settings.Failure[FAIL_FAN].Timer * T_MINS`
 * delay gates escalation — there is intentionally no parallel timer.
 */
static int FanFailChk(void)
{
    unsigned int Counter = XTimerVal;

    bool vfd_faulted = nova_vfd_any_faulted(NULL, NULL);

    if (((Settings.Failure[FAIL_FAN].Mode != FM_NONE
          && CheckInputs(SW_FAN_AUTO)
          && (CheckOutputs(EQ_FAN) || SystemAlarm[AL_FAN] != FM_NONE)
          && (CheckInputs(EQ_FAN) || vfd_faulted)))
        || SystemAlarm[AL_FAN] == FM_FAIL) {
        if (AlarmTimer[AL_FAN] == 0) {
            AlarmTimer[AL_FAN] = Counter;
        } else if (Counter - AlarmTimer[AL_FAN]
                   >= Settings.Failure[FAIL_FAN].Timer * T_MINS) {
            SystemAlarm[AL_FAN] = FM_FAIL;
            /* Pass the offending drive index in eqIo so the activity
             * log + warning push can identify which VFD tripped. 0 =
             * legacy contactor source (no specific drive). */
            uint8_t drv_idx = 0;
            (void)nova_vfd_any_faulted(&drv_idx, NULL);
            WarningsSet(WARN_FAN, FM_ALARM,
                        Settings.Failure[FAIL_FAN].Mode,
                        (uint32_t)drv_idx);
            return 1;
        }
    } else {
        SystemAlarm[AL_FAN] = FM_NONE;
        AlarmTimer[AL_FAN]  = 0;
    }
    return 0;
}

/* ─── Heat ─────────────────────────────────────────────────────────────── */
static int HeatFailChk(void)
{
    unsigned int Counter = XTimerVal;

    if (((Settings.Failure[FAIL_HEAT].Mode != FM_NONE
          && (CheckOutputs(EQ_HEAT) || SystemAlarm[AL_HEAT] != FM_NONE)
          && CheckInputs(EQ_HEAT)))
        || SystemAlarm[AL_HEAT] == FM_FAIL) {
        if (AlarmTimer[AL_HEAT] == 0) {
            AlarmTimer[AL_HEAT] = Counter;
        } else if (Counter - AlarmTimer[AL_HEAT]
                   >= Settings.Failure[FAIL_HEAT].Timer * T_MINS) {
            WarningsSet(WARN_HEAT, FM_ALARM,
                        Settings.Failure[FAIL_HEAT].Mode, NA);
            SystemAlarm[AL_HEAT] = Settings.Failure[FAIL_HEAT].Mode;
            if (Settings.Failure[FAIL_HEAT].Mode == FM_FAIL) return 1;
        }
    } else {
        SystemAlarm[AL_HEAT] = FM_NONE;
        AlarmTimer[AL_HEAT]  = 0;
    }
    return 0;
}

/* ─── Humidifier (per-head) ────────────────────────────────────────────── */
static int HumidFailChk(EQUIPMENT_IO io, EQUIP_ALARMS alarm)
{
    unsigned int Counter = XTimerVal;

    if (((Settings.Failure[FAIL_HUMIDIFIERS].Mode != FM_NONE
          && CheckInputs(SW_HUMID_AUTO)
          && (CheckOutputs(io) || SystemAlarm[alarm] != FM_NONE)
          && CheckInputs(io)))
        || SystemAlarm[alarm] == FM_FAIL) {
        if (AlarmTimer[alarm] == 0) {
            AlarmTimer[alarm] = Counter;
        } else if (Counter - AlarmTimer[alarm]
                   >= Settings.Failure[FAIL_HUMIDIFIERS].Timer * T_MINS) {
            WarningsSet(WARN_HUMIDIFIER, FM_ALARM,
                        Settings.Failure[FAIL_HUMIDIFIERS].Mode, io);
            SystemAlarm[alarm] = Settings.Failure[FAIL_HUMIDIFIERS].Mode;
            if (Settings.Failure[FAIL_HUMIDIFIERS].Mode == FM_FAIL) return 1;
        }
    } else {
        SystemAlarm[alarm] = FM_NONE;
        AlarmTimer[alarm]  = 0;
    }
    return 0;
}

/* ─── Master broadcast (slave-mode comm watchdog) ──────────────────────── */
static void MasterBroadcastFailChk(void)
{
    unsigned int Counter = XTimerVal;

    if ((Settings.MasterSlave == MSMODE_SLAVE
         || Settings.MasterSlave == MSMODE_SLAVE_NOBROADCAST)
        && Counter - AlarmTimer[AL_NOBROADCAST] >= 10 * T_MINS) {
        SystemAlarm[AL_NOBROADCAST] = 1;
        WarningsSet(WARN_NOBROADCAST, FM_ALARM, FM_ALARM, NA);

        if (Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Sensor[SENSOR_OUTSIDE_TEMP].Disabled == 0) {
            Settings.MasterSlave = MSMODE_SLAVE_NOBROADCAST;
            WarningsSet(WARN_SLAVENOBROADCAST, FM_ALARM, FM_ALARM, NA);
        } else {
            Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Sensor[SENSOR_OUTSIDE_TEMP].Value
                = SENSOR_VAL_UNDEFINED;
        }

        if (Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Disabled == 0
            && Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_OUTSIDE_HUMID].Disabled == 0) {
            Settings.MasterSlave = MSMODE_SLAVE_NOBROADCAST;
            WarningsSet(WARN_SLAVENOBROADCAST, FM_ALARM, FM_ALARM, NA);
        } else {
            Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_OUTSIDE_HUMID].Value
                = SENSOR_VAL_UNDEFINED;
        }
    }
}

/* ─── Outside humidity sensor ──────────────────────────────────────────── */
static int OutsideHumidSensorFailChk(void)
{
    unsigned int Counter = XTimerVal;

    if (((Settings.Failure[FAIL_OUTSIDE_HUMIDITY].Mode != FM_NONE
          && Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Present == 1
          && Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_OUTSIDE_HUMID].Disabled == 0
          && Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_OUTSIDE_HUMID].Value
             == SENSOR_VAL_UNDEFINED))
        || SystemAlarm[AL_OUTHUMIDSENSOR] == FM_FAIL) {
        if (AlarmTimer[AL_OUTHUMIDSENSOR] == 0) {
            AlarmTimer[AL_OUTHUMIDSENSOR] = Counter;
        } else if (Counter - AlarmTimer[AL_OUTHUMIDSENSOR]
                   >= Settings.Failure[FAIL_OUTSIDE_HUMIDITY].Timer * T_MINS) {
            WarningsSet(WARN_OUTHUMIDSENSOR, FM_ALARM,
                        Settings.Failure[FAIL_OUTSIDE_HUMIDITY].Mode, NA);
            SystemAlarm[AL_OUTHUMIDSENSOR] = Settings.Failure[FAIL_OUTSIDE_HUMIDITY].Mode;
            if (Settings.Failure[FAIL_OUTSIDE_HUMIDITY].Mode == FM_FAIL) return 1;
        }
    } else {
        SystemAlarm[AL_OUTHUMIDSENSOR] = FM_NONE;
        AlarmTimer[AL_OUTHUMIDSENSOR]  = 0;
    }
    return 0;
}

/* ─── Outside humidity variance (24-hr stuck-sensor watchdog) ──────────── */
static int OutsideHumidVarFailChk(void)
{
    float HumidDiff = 0;
    float Variance  = 0;
    float *OutsideHumid =
        &(Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_OUTSIDE_HUMID].Value);
    unsigned int Counter = XTimerVal;

    if (Settings.Failure[FAIL_OUTSIDE_HUMIDITY].Mode == FM_NONE
        || Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Present == 0
        || Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_OUTSIDE_HUMID].Disabled == 1) {
        return 0;
    }

    if (AlarmTimer[AL_OUTHUMIDVAR] == 0) {
        HumidVarChkValue = *OutsideHumid;
        AlarmTimer[AL_OUTHUMIDVAR] = Counter;
        SystemAlarm[AL_OUTHUMIDVAR] = FM_NONE;
        return 0;
    }

    Variance  = HumidVarChkValue * 3.0f;
    HumidDiff = HumidVarChkValue - *OutsideHumid;
    if (HumidDiff < 0) HumidDiff *= -1;

    if ((HumidDiff * 100.0f) > Variance
        && SystemAlarm[AL_OUTHUMIDVAR] != FM_FAIL) {
        HumidVarChkValue = *OutsideHumid;
        AlarmTimer[AL_OUTHUMIDVAR] = Counter;
        SystemAlarm[AL_OUTHUMIDVAR] = FM_NONE;
        return 0;
    }

    if (Counter - AlarmTimer[AL_OUTHUMIDVAR] >= 24 * T_HOURS
        || SystemAlarm[AL_OUTHUMIDVAR] == FM_FAIL) {
        WarningsSet(WARN_OUTHUMIDVAR, FM_ALARM,
                    Settings.Failure[FAIL_OUTSIDE_HUMIDITY].Mode, NA);
        SystemAlarm[AL_OUTHUMIDVAR] = Settings.Failure[FAIL_OUTSIDE_HUMIDITY].Mode;
        if (Settings.Failure[FAIL_OUTSIDE_HUMIDITY].Mode == FM_FAIL) return 1;
    }
    return 0;
}

/* ─── Outside temp sensor ──────────────────────────────────────────────── */
static int OutsideTempSensorFailChk(void)
{
    unsigned int Counter = XTimerVal;

    if (((Settings.Failure[FAIL_OUTSIDE_AIR].Mode != FM_NONE
          && Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Sensor[SENSOR_OUTSIDE_TEMP].Disabled == 0
          && *OutsideTemp == SENSOR_VAL_UNDEFINED))
        || SystemAlarm[AL_OUTTEMPSENSOR] == FM_FAIL) {
        if (AlarmTimer[AL_OUTTEMPSENSOR] == 0) {
            AlarmTimer[AL_OUTTEMPSENSOR] = Counter;
        } else if (Counter - AlarmTimer[AL_OUTTEMPSENSOR]
                   >= Settings.Failure[FAIL_OUTSIDE_AIR].Timer * T_MINS) {
            WarningsSet(WARN_OUTTEMPSENSOR, FM_ALARM,
                        Settings.Failure[FAIL_OUTSIDE_AIR].Mode, NA);
            SystemAlarm[AL_OUTTEMPSENSOR] = Settings.Failure[FAIL_OUTSIDE_AIR].Mode;
            if (Settings.Failure[FAIL_OUTSIDE_AIR].Mode == FM_FAIL) return 1;
        }
    } else {
        SystemAlarm[AL_OUTTEMPSENSOR] = FM_NONE;
        AlarmTimer[AL_OUTTEMPSENSOR]  = 0;
    }
    return 0;
}

/* ─── Plenum humidity ──────────────────────────────────────────────────── */
static int PlenumHumidFailChk(void)
{
    unsigned int Counter = XTimerVal;

    if (((Settings.Failure[FAIL_PLENUM_HUMIDITY].Mode != FM_NONE
          && Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Present == 1
          && Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_PLENUM_HUMID].Disabled == 0
          && (Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_PLENUM_HUMID].Value
                  < Settings.Plenum.HumidLowFailure
              || Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_PLENUM_HUMID].Value
                  == SENSOR_VAL_UNDEFINED)))
        || SystemAlarm[AL_PLENHUMID] == FM_FAIL) {
        if (AlarmTimer[AL_PLENHUMID] == 0) {
            AlarmTimer[AL_PLENHUMID] = Counter;
        } else if (Counter - AlarmTimer[AL_PLENHUMID]
                   >= Settings.Failure[FAIL_PLENUM_HUMIDITY].Timer * T_MINS) {
            WarningsSet(WARN_PLENHUMID, FM_ALARM,
                        Settings.Failure[FAIL_PLENUM_HUMIDITY].Mode, NA);
            SystemAlarm[AL_PLENHUMID] = Settings.Failure[FAIL_PLENUM_HUMIDITY].Mode;
            if (Settings.Failure[FAIL_PLENUM_HUMIDITY].Mode == FM_FAIL) return 1;
        }
    } else {
        SystemAlarm[AL_PLENHUMID] = FM_NONE;
        AlarmTimer[AL_PLENHUMID]  = 0;
    }
    return 0;
}

/* ─── Plenum sensor variance (sensor 1 vs sensor 2) ────────────────────── */
static int PlenumSensorFailChk(void)
{
    unsigned int Counter = XTimerVal;
    float TempDiff =
        Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Sensor[SENSOR_PLENUM_TEMP_1].Value
        - Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Sensor[SENSOR_PLENUM_TEMP_2].Value;
    if (TempDiff < 0) TempDiff *= -1;

    if (((Settings.Failure[FAIL_PLENUM_SENSOR].Mode != FM_NONE
          && TempDiff > Settings.Plenum.SensorDiff))
        || SystemAlarm[AL_PLENSENSOR] == FM_FAIL) {
        if (AlarmTimer[AL_PLENSENSOR] == 0) {
            AlarmTimer[AL_PLENSENSOR] = Counter;
        } else if (Counter - AlarmTimer[AL_PLENSENSOR]
                   >= Settings.Failure[FAIL_PLENUM_SENSOR].Timer * T_MINS) {
            WarningsSet(WARN_PLENSENSOR, FM_ALARM,
                        Settings.Failure[FAIL_PLENUM_SENSOR].Mode, NA);
            SystemAlarm[AL_PLENSENSOR] = Settings.Failure[FAIL_PLENUM_SENSOR].Mode;
            if (Settings.Failure[FAIL_PLENUM_SENSOR].Mode == FM_FAIL) return 1;
        }
    } else {
        SystemAlarm[AL_PLENSENSOR] = FM_NONE;
        AlarmTimer[AL_PLENSENSOR]  = 0;
    }
    return 0;
}

/* ─── Plenum temp dispatcher (cure mode vs storage mode) ──────────────── */
static int PlenumTempFailChk(void)
{
    int RetVal = 0;

    if (Settings.Co2.Purge.Start == 0) {
        if (Settings.SystemMode == SM_ONION && CheckInputs(SW_CURE_AUTO)) {
            RetVal += PlenumTempFailCureModeLowChk();
            RetVal += PlenumTempFailCureModeHighChk();
        } else {
            RetVal += PlenumTempFailLowChk();
            RetVal += PlenumTempFailHighChk();
        }
    } else {
        AlarmTimer[AL_HIGHPLENTEMP] = 0;
        AlarmTimer[AL_LOWPLENTEMP]  = 0;
    }
    return RetVal;
}

/* ─── Plenum temp high — cure mode ────────────────────────────────────── */
static int PlenumTempFailCureModeHighChk(void)
{
    unsigned int Counter = XTimerVal;

    if (CheckOutputs(EQ_FAN) || SystemAlarm[AL_HIGHPLENTEMP] != FM_NONE) {
        /* alarm-state burner-stop logic — bit-for-bit from legacy */
        if (((CureState == CS_DEHUMID)
             && (PlenumTempAvg > (Settings.Burner.TempSet * 1.10f)))
            || ((CureState == CS_MANUAL)
                && (PlenumTempAvg > Settings.Burner.TempSet))) {
            SystemAlarm[AL_HIGHPLENTEMP] = FM_ALARM;
        } else if ((SystemAlarm[AL_HIGHPLENTEMP] == FM_ALARM)
                   && (((CureState == CS_DEHUMID)
                        && (PlenumTempAvg < (Settings.Burner.TempSet * 0.90f)))
                       || ((CureState == CS_MANUAL)
                           && (PlenumTempAvg < Settings.Burner.Threshold)))) {
            SystemAlarm[AL_HIGHPLENTEMP] = FM_NONE;
        }

        if (PlenumTempAvg > Settings.Cure.TempHighLimit
            || SystemAlarm[AL_HIGHPLENTEMP] == FM_FAIL) {
            if (AlarmTimer[AL_HIGHPLENTEMP] == 0) {
                AlarmTimer[AL_HIGHPLENTEMP] = Counter;
            } else if (Counter - AlarmTimer[AL_HIGHPLENTEMP]
                       >= Settings.Plenum.HighAlarmTimer * T_MINS) {
                SystemAlarm[AL_HIGHPLENTEMP] = FM_FAIL;
                WarningsSet(WARN_HIGHPLENTEMP, FM_ALARM, FM_ALARM, NA);
                return 1;
            }
        } else {
            AlarmTimer[AL_HIGHPLENTEMP] = 0;
        }
    }
    return 0;
}

/* ─── Plenum temp low — cure mode ─────────────────────────────────────── */
static int PlenumTempFailCureModeLowChk(void)
{
    unsigned int Counter = XTimerVal;

    if (CheckOutputs(EQ_FAN) || SystemAlarm[AL_LOWPLENTEMP] != FM_NONE) {
        if (PlenumTempAvg < Settings.Cure.TempLowLimit
            || SystemAlarm[AL_LOWPLENTEMP] == FM_FAIL) {
            if (AlarmTimer[AL_LOWPLENTEMP] == 0) {
                AlarmTimer[AL_LOWPLENTEMP] = Counter;
            } else if (Counter - AlarmTimer[AL_LOWPLENTEMP]
                       >= Settings.Plenum.LowAlarmTimer * T_MINS) {
                SystemAlarm[AL_LOWPLENTEMP] = FM_FAIL;
                WarningsSet(WARN_LOWPLENTEMP, FM_ALARM, FM_ALARM, NA);
                return 1;
            }
        } else {
            SystemAlarm[AL_LOWPLENTEMP] = FM_NONE;
            AlarmTimer[AL_LOWPLENTEMP]  = 0;
        }
    }
    return 0;
}

/* ─── Plenum temp high — storage mode ─────────────────────────────────── */
static int PlenumTempFailHighChk(void)
{
    int Board, Sensor;
    float RefTemp = SENSOR_VAL_UNDEFINED;
    unsigned int Counter = XTimerVal;

    if (CheckOutputs(EQ_FAN) || SystemAlarm[AL_HIGHPLENTEMP] != FM_NONE) {
        if (Settings.OutsideAir.TempRef == 255) {
            RefTemp = Settings.Plenum.TempSet;
        } else if (Settings.OutsideAir.TempRef == 254) {
            RefTemp = Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Sensor[SENSOR_RETURN_TEMP].Value;
        } else {
            Board  = Settings.OutsideAir.TempRef / ANALOG_SENSORS_PER_BOARD;
            Sensor = Settings.OutsideAir.TempRef % ANALOG_SENSORS_PER_BOARD;
            RefTemp = Settings.AnalogBoard[Board].Sensor[Sensor].Value;
        }

        if (PlenumTempAvg > RefTemp + Settings.Plenum.HighAlarmTemp
            || RefTemp == SENSOR_VAL_UNDEFINED
            || SystemAlarm[AL_HIGHPLENTEMP] == FM_FAIL) {
            if (AlarmTimer[AL_HIGHPLENTEMP] == 0) {
                AlarmTimer[AL_HIGHPLENTEMP] = Counter;
            } else if (Counter - AlarmTimer[AL_HIGHPLENTEMP]
                       >= Settings.Plenum.HighAlarmTimer * T_MINS) {
                SystemAlarm[AL_HIGHPLENTEMP] = FM_FAIL;
                WarningsSet(WARN_HIGHPLENTEMP, FM_ALARM, FM_ALARM, NA);
                return 1;
            }
        } else {
            SystemAlarm[AL_HIGHPLENTEMP] = FM_NONE;
            AlarmTimer[AL_HIGHPLENTEMP]  = 0;
        }
    }
    return 0;
}

/* ─── Plenum temp low — storage mode ──────────────────────────────────── */
static int PlenumTempFailLowChk(void)
{
    unsigned int Counter = XTimerVal;

    if (CheckOutputs(EQ_FAN) || SystemAlarm[AL_LOWPLENTEMP] != FM_NONE) {
        if (PlenumTempAvg < Settings.Plenum.TempSet - Settings.Plenum.LowAlarmTemp
            || SystemAlarm[AL_LOWPLENTEMP] == FM_FAIL) {
            if (AlarmTimer[AL_LOWPLENTEMP] == 0) {
                AlarmTimer[AL_LOWPLENTEMP] = Counter;
            } else if (Counter - AlarmTimer[AL_LOWPLENTEMP]
                       >= Settings.Plenum.LowAlarmTimer * T_MINS) {
                SystemAlarm[AL_LOWPLENTEMP] = FM_FAIL;
                WarningsSet(WARN_LOWPLENTEMP, FM_ALARM, FM_ALARM, NA);
                return 1;
            }
        } else {
            SystemAlarm[AL_LOWPLENTEMP] = FM_NONE;
            AlarmTimer[AL_LOWPLENTEMP]  = 0;
        }
    }
    return 0;
}

/* ─── Refrigeration aggregate ─────────────────────────────────────────── */
static int RefrigFailChk(void)
{
    int alarms = 0;
    int stagesEnabled = 0;
    int RetVal = 0;

    for (int i = 0; i < NUM_DEFROST_STAGES; ++i) {
        RetVal += RefrigStageChk(EQ_REFRIG_DEFROST1 + i, AL_REFRIG_DEFROST1 + i);
    }

    for (int i = 0; i < NUM_REFRIG_STAGES; ++i) {
        RetVal += RefrigStageChk(EQ_REFRIG_STAGE1 + i, AL_REFRIG_STAGE1 + i);
        if (SystemAlarm[AL_REFRIG_STAGE1 + i] != FM_NONE) alarms++;
        if (Settings.EquipIo[EQ_REFRIG_STAGE1 + i].Input != IO_UNDEFINED) {
            stagesEnabled++;
        }
    }

    if (alarms > 0 && alarms == stagesEnabled) {
        SystemAlarm[AL_REFRIGERATION] = FM_PRELIM;
    }

    RetVal += Refrig420Chk();
    return RetVal;
}

/* ─── Refrigeration 4-20mA / overall ──────────────────────────────────── */
static int Refrig420Chk(void)
{
    unsigned int Counter = XTimerVal;
    int RefrigPercent = PWMValToPercent(PwmChannel[PWM_REFRIGERATION].Output);

    if (((Settings.Failure[FAIL_REFRIGERATION].Mode != FM_NONE
          && CheckInputs(SW_REFRIG_AUTO)
          && (RefrigPercent > 0 || SystemAlarm[AL_REFRIGERATION] != FM_NONE)
          && CheckInputs(EQ_REFRIGERATION)))
        || SystemAlarm[AL_REFRIGERATION] == FM_FAIL
        || SystemAlarm[AL_REFRIGERATION] == FM_PRELIM) {
        if (AlarmTimer[AL_REFRIGERATION] == 0) {
            AlarmTimer[AL_REFRIGERATION] = Counter;
        } else if (Counter - AlarmTimer[AL_REFRIGERATION]
                   >= Settings.Failure[FAIL_REFRIGERATION].Timer * T_MINS) {
            WarningsSet(WARN_REFRIG_PWM, FM_ALARM,
                        Settings.Failure[FAIL_REFRIGERATION].Mode, NA);
            SystemAlarm[AL_REFRIGERATION] = Settings.Failure[FAIL_REFRIGERATION].Mode;
            if (Settings.Failure[FAIL_REFRIGERATION].Mode == FM_FAIL) return 1;
        }
    } else {
        SystemAlarm[AL_REFRIGERATION] = FM_NONE;
        AlarmTimer[AL_REFRIGERATION]  = 0;
    }
    return 0;
}

/* ─── Refrigeration per-stage (also handles defrost) ──────────────────── */
static int RefrigStageChk(int io, int alarm)
{
    unsigned int Counter = XTimerVal;

    if (((Settings.Failure[FAIL_REFRIG_STAGES].Mode != FM_NONE
          && CheckInputs(SW_REFRIG_AUTO)
          && (CheckOutputs(io) || SystemAlarm[alarm] != FM_NONE)
          && CheckInputs(io)))
        || SystemAlarm[alarm] == FM_FAIL) {
        if (AlarmTimer[alarm] == 0) {
            AlarmTimer[alarm] = Counter;
        } else if (Counter - AlarmTimer[alarm]
                   >= Settings.Failure[FAIL_REFRIG_STAGES].Timer * T_MINS) {
            if (io >= EQ_REFRIG_DEFROST1) {
                WarningsSet(WARN_REFRIG_DEFROST, FM_ALARM,
                            Settings.Failure[FAIL_REFRIG_STAGES].Mode, io);
            } else {
                WarningsSet(WARN_REFRIG_STAGE, FM_ALARM,
                            Settings.Failure[FAIL_REFRIG_STAGES].Mode, io);
            }
            SystemAlarm[alarm] = Settings.Failure[FAIL_REFRIG_STAGES].Mode;
            if (Settings.Failure[FAIL_REFRIG_STAGES].Mode == FM_FAIL) return 1;
        }
    } else {
        SystemAlarm[alarm] = FM_NONE;
        AlarmTimer[alarm]  = 0;
    }
    return 0;
}

/* ─── Public dispatcher ───────────────────────────────────────────────── */
int SystemFailuresChk(void)
{
    int RetVal = 0;

    if (Settings.SystemMode == SM_POTATO) {
        RetVal += ClimacellFailChk();
        RetVal += HumidFailChk(EQ_HUMID_HEAD1, AL_HUMID1);
        RetVal += HumidFailChk(EQ_HUMID_HEAD2, AL_HUMID2);
        RetVal += HumidFailChk(EQ_HUMID_HEAD3, AL_HUMID3);
    } else {
        RetVal += BurnerFailChk();
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

    /* Reports-only — never contributes to RetVal. */
    MasterBroadcastFailChk();

    return RetVal;
}

/* Public wrapper so lp_engine_shim.c can drive just this check from
 * its 1 s tick. The full SystemFailuresChk is still gated off in the
 * LP build because most of its sub-checks expect equipment state
 * (fan, burner, refrig, humidifier, lights, aux IO) that the LP
 * engine doesn't yet produce. */
void NovaFailures_RunMasterBroadcastChk(void)
{
    MasterBroadcastFailChk();
}

/*** End of file ***/
