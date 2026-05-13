/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     Settings.c

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:  Store and read the system settings

COMMENTS:

***************************************************************************/

/*** include files ***/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Platform
//#include "driverlib/sysctl.h"
#include "debug.h"

// FreeRTOS Includes
#include "FreeRTOS.h"
#include "task.h"

// Gellert
#include "DataExc.h"
#include "Flash.h"
#include "PWM.h"
#include "RTC.h"
#include "Settings.h"
#include "States.h"
#include "StorePostData.h"
#include "ThreadFileReceive.h"
#include "ThreadMonitor.h"
#include "Timer.h"
#include "Warnings.h"

/*** defines ***/

#define FLASHFILE_HDR_LEN   50

/*** typedefs and structures ***/

/*** module variables ***/

// public variable used by all modules
SYSTEM_SETTINGS Settings;
SAVE_SETTINGS_INFO SaveSettingsRequest;

// A/B dual-bank: track which bank was written last so we alternate
static int LastWrittenBank = 0;  // 0 = A, 1 = B

extern void _EnableWrite(void);

/*** static functions ***/

static void EquipDefine(void);
static void SendSettingsRecord(uint32_t *bytesWritten, void *Element, char *Label, ELEMENT_TYPE Type);
static void StoreSettingsValue(char *Msg);

/***************************************************************************

FUNCTION:   CRC()

PURPOSE:    Calculate a CRC value

COMMENTS:

***************************************************************************/
unsigned int CRC(char *buf, int len)
{
  unsigned int crc = 0xFFFFFFFF;
  unsigned int temp;
  int j;

  while (len--)
  {
    temp = (uint32_t)((crc & 0xFF) ^ *buf++);
    for (j = 0; j < 8; j++)
    {
      if (temp & 0x1)
        temp = (temp >> 1) ^ 0xEDB88320;
      else
        temp >>= 1;
    }
    crc = (crc >> 8) ^ temp;
  }
  return crc ^ 0xFFFFFFFF;
} // end CRC()

/***************************************************************************

FUNCTION:   StringCopy()

PURPOSE:    Safe string copy

COMMENTS:

***************************************************************************/
void StringCopy(char *buffer, const char *string, int max)
{
  strncpy(buffer, string, max-1);
  buffer[max-1] = 0;
} // end StringCopy()

/***************************************************************************

FUNCTION:   GetFactoryDefault()

PURPOSE:    Load the factory default settings into Settings

COMMENTS:

***************************************************************************/
void GetFactoryDefault(void)
{
  int i,j;
  unsigned int Counter = XTimerVal;

  memset(&Settings, 0, sizeof(SYSTEM_SETTINGS));

  // system settings
  strcpy(Settings.SettingsVersion, SETTINGS_VERSION);

  Settings.DST = 0;
  Settings.HttpPort = 80;

  // logs
  Settings.Log.User.Interval = 60;
  Settings.Log.User.Wrap = 1;      // wrap
  Settings.Log.PID.Wrap = 1;       // wrap
  Settings.Log.PID.Door = 0;
  Settings.Log.PID.Refrig = 0;
  strcpy(Settings.Log.GraphFavorites, "");

  // load monitor
  strcpy(Settings.LoadMonitor.Bay[0].Label, "");
  strcpy(Settings.LoadMonitor.Bay[1].Label, "");
//  Settings.LoadMonitor.Bay[0].Status = LL_PAUSED;
//  Settings.LoadMonitor.Bay[1].Status = LL_PAUSED;
  Settings.LoadMonitor.Bay[0].Pipe = 1;
  Settings.LoadMonitor.Bay[1].Pipe = 1;
  Settings.LoadMonitor.AlarmTemp[0] = 60;
  Settings.LoadMonitor.AlarmTemp[1] = 40;
  Settings.LoadMonitor.IrSensorsAvailable[0] = 0;
  Settings.LoadMonitor.IrSensorsAvailable[1] = 0;

  // basic setup page
  strcpy(Settings.StorageName,"Gellert Agri-Star");    // facility
  Settings.TempType = 0;                               // Fahrenheit/Celcius
  Settings.MasterSlave = MSMODE_LOCAL;                 // local (stand alone)
  Settings.MasterIP[0] = 0;
  strcpy(Settings.HomePage, "mnMainData.htm");
  Settings.SystemMode = 0;                             // potato
  Settings.MultiviewSessions = 6;
  strcpy(Settings.PublicIP, "0.0.0.0");
  Settings.Language = 0;
  strcpy(Settings.FactoryBd, DEFAULT_P2PASSWORD);
  strcpy(Settings.LoginPw, DEFAULT_LOGINPASSWORD);
  Settings.LocalLogin = 0;
  Settings.Animations = 1;

  // password page
//  memset(Settings.User, 0, sizeof(Settings.User));
  strcpy(Settings.User[0].ID, DEFAULT_USER);
  strcpy(Settings.User[0].Password, DEFAULT_PASSWORD);

  // analog board setup page
//  memset(Settings.AnalogBoard, 0, sizeof(Settings.AnalogBoard));
  for (i = 0; i < ANALOG_BOARDS_PER_SYSTEM; i++)
  {
    for (j = 0; j < ANALOG_SENSORS_PER_BOARD; j++)
    {
      Settings.AnalogBoard[i].Sensor[j].Value = SENSOR_VAL_UNDEFINED;
    }
  }

  // don't set the default labels here, allow them to be read in from the translated file
//  strcpy(Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Label, "");
//  strcpy(Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Sensor[SENSOR_PLENUM_TEMP_1].Label, "");
//  strcpy(Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Sensor[SENSOR_PLENUM_TEMP_2].Label, "");
//  strcpy(Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Sensor[SENSOR_OUTSIDE_TEMP].Label, "");
//  strcpy(Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Sensor[SENSOR_RETURN_TEMP].Label, "");
//  strcpy(Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Label, "");
//  strcpy(Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_OUTSIDE_HUMID].Label, "");
//  strcpy(Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_PLENUM_HUMID].Label, "");
//  strcpy(Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_RETURN_HUMID].Label, "");
//  strcpy(Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_CO2].Label, "");

  // fresh air door setup page
  Settings.Door.PID.P = 5;
  Settings.Door.PID.I = 15;
  Settings.Door.PID.D = 2;
  Settings.Door.PID.U = 3;
  Settings.Door.ActuatorTime = 180;     // secs
  Settings.Door.CoolAirCycle = 10;      // mins

  // refrigeration setup page
  Settings.Refrig.Stage[0].On = 20;
  Settings.Refrig.Stage[0].Off = 10;
  Settings.Refrig.Stage[1].On = 30;
  Settings.Refrig.Stage[1].Off = 20;
  Settings.Refrig.Stage[2].On = 40;
  Settings.Refrig.Stage[2].Off = 30;
  Settings.Refrig.Stage[3].On = 50;
  Settings.Refrig.Stage[3].Off = 40;
  Settings.Refrig.Stage[4].On = 60;
  Settings.Refrig.Stage[4].Off = 50;
  Settings.Refrig.Stage[5].On = 70;
  Settings.Refrig.Stage[5].Off = 60;
  Settings.Refrig.Stage[6].On = 80;
  Settings.Refrig.Stage[6].Off = 70;
  Settings.Refrig.Stage[7].On = 90;
  Settings.Refrig.Stage[7].Off = 80;
  Settings.Refrig.PID.P = 5;
  Settings.Refrig.PID.I = 15;
  Settings.Refrig.PID.D = 2;
  Settings.Refrig.PID.U = 3;
  Settings.Refrig.Purge = 0;            // normal
  for (i = 0; i < NUM_REFRIG_STAGES; i++)
  {
    Settings.Refrig.Stage[i].Diagnostic = 0;
  }

  // climacell setup page
  Settings.Climacell.Efficiency = 90;   // %
  Settings.Climacell.PID.P = 5;
  Settings.Climacell.PID.I = 15;
  Settings.Climacell.PID.D = 2;
  Settings.Climacell.PID.U = 3;
  Settings.Climacell.Altitude = 0;
  Settings.Climacell.AltitudeUnits= 0;  // feet/meters

  // burner setup page
  Settings.Burner.Mode = 0;             // BURNER_OFF;
  Settings.Burner.On = 10;              // %
  Settings.Burner.Low = 25;             // %
  Settings.Burner.PID.P = 5;
  Settings.Burner.PID.I = 15;
  Settings.Burner.PID.D = 2;
  Settings.Burner.PID.U = 3;
  Settings.Burner.Manual = 75;          // %

  // failures 1 setup page
  for (i = 0; i < NUM_FAILURES; i++)
  {
    Settings.Failure[i].Mode = 0;
    Settings.Failure[i].Timer = 10;
  }
  Settings.Failure[FAIL_FAN].Mode = 2;  // fail
  Settings.Failure[FAIL_FAN].Timer = 3; // mins
  Settings.Refrig.FailMode = 255;       // undefined
  Settings.Failure[FAIL_LIGHTS].Timer = 1;
  Settings.LightsFailUnits = 60;        // hours (1 = minutes, 60 = hours)

  // failures 2 setup page
  Settings.Failure[FAIL_PLENUM_SENSOR].Mode = 1;    // alarm
  Settings.Plenum.SensorDiff = 2;                   // degrees
  Settings.Failure[FAIL_OUTSIDE_AIR].Mode = 1;      // alarm
  Settings.Failure[FAIL_OUTSIDE_HUMIDITY].Mode = 1; // alarm
  Settings.Plenum.HumidLowFailure = 80;             // %
  Settings.Failure[FAIL_HIGH_CO2].Mode = 1;         // alarm
  Settings.Co2.HighFailure = 2500;                  // ppm

  // controller access page
//  memset(Settings.Node, 0, sizeof(Settings.Node));

  // service info page
  strcpy(Settings.DealerName, "");
  strcpy(Settings.DealerPhone, "");
  strcpy(Settings.TechName, "");
  strcpy(Settings.TechPhone, "");

  // plenum setup page
  Settings.Plenum.TempSet = 46;         // degrees
  Settings.Plenum.HumidSet = 95;        // %
  Settings.Fan.DailyRunTime = 0;
  Settings.Fan.TotalRunTime = 0;
  Settings.Fan.TotalRefrigTime = 0;
  Settings.Fan.TotalCoolingTime = 0;
  Settings.Fan.TotalRecircTime = 0;
  Settings.Fan.TotalStandbyTime = 0;
  Settings.Fan.TotalCureTime = 0;
  Settings.Plenum.HumidSetpointRef = 0; // plenum
  Settings.Burner.TempSet = 75;         // degrees (shared for manual mode max temp)
  Settings.Burner.Threshold = 50;       // %       (shared for manual mode restart temp)

  // outside air cooling control page
  Settings.OutsideAir.Diff = 2;
  Settings.OutsideAir.CtrlMode = OSA_CTRL_OUTSIDE;
  Settings.OutsideAir.AboveBelow = 0;   // above
  Settings.OutsideAir.TempRef = 255;    // plenum setpoint
  Settings.OutsideAir.CalcHumidMax = 80;
  // air cure mode
  Settings.Cure.StartTemp = 60;
  Settings.Cure.StartHumid = 70;
  Settings.Cure.HumidHighLimit = 85;
  Settings.Cure.HumidRef = 0;           // plenum

  // plenum temp deviation alarm page
  Settings.Plenum.LowAlarmTemp = 5;     // degrees below setpoint
  Settings.Plenum.LowAlarmTimer = 10;   // mins
  Settings.Plenum.HighAlarmTemp = 5;    // degrees above setpoint
  Settings.Plenum.HighAlarmTimer = 10;  // mins
  // air cure mode
  Settings.Cure.TempLowLimit = 35;      // degrees
  Settings.Cure.TempHighLimit = 110;    // degrees

  // system runtimes page
  memset(Settings.RunTimes, 3, sizeof(Settings.RunTimes));                // standby
  memset(Settings.Cure.RunTimes, 3, sizeof(Settings.Cure.RunTimes));      // standby
  memset(Settings.Climacell.Times, 2, sizeof(Settings.Climacell.Times));  // on

  // frequency drive control page
  Settings.Fan.MaxSpeed = 100;
  Settings.Fan.MinSpeed = 25;
  Settings.Fan.RefrigSpeed = 75;
  Settings.Fan.RecircSpeed = 50;
  Settings.Fan.UpdatePeriod = 5;        // hours
  Settings.Fan.TempDiff = 1;
  Settings.Fan.TempRef1 = 0;
  Settings.Fan.TempRef2 = 255;
  Settings.Fan.UpdateMode = 0;
  Settings.Fan.PrevSpeed = Settings.Fan.MinSpeed;

  // ramp rate page
  Settings.Ramp.UpdateTemp = 1;
  Settings.Ramp.UpdatePeriod = 2;
  Settings.Ramp.TempDiff = 1;
  Settings.Ramp.TempRef = 0;
  Settings.Ramp.TargetTemp = 46;

  // humidity control page
  for (i = 0; i < NUM_HUMIDIFIERS; i++)
  {
    Settings.HumidCtrl[i].Mode = 1;     // manual

    for (j = 0; j < 3; j++) // j = system modes cool, recirc, refrig
    {
      Settings.HumidCtrl[i].DutyCycle[j].On = 60;
      Settings.HumidCtrl[i].DutyCycle[j].Off = 60;
    }
  }

  // high CO2 level purge page
  Settings.Co2.Purge.Mode = 0;          // none
  Settings.Co2.Set = 1200;
  Settings.Co2.CylceTime = 24;
  Settings.Co2.Purge.MaxTemp = 50;
  Settings.Co2.Purge.MinTemp = 40;
  Settings.Co2.Purge.Duration = 20;
  Settings.Co2.Purge.Last = Counter;
  Settings.Co2.Purge.Start = 0;
  Settings.Co2.FanOutput = 100;
  Settings.Co2.DoorOutput = 100;

  // miscellaneous parameters page
  Settings.Refrig.Mode = 0;             // economizer
  Settings.Refrig.DefrostPeriod = 0;
  Settings.Refrig.DefrostDuration = 10;
  Settings.HeatTempThresh = 10;
  Settings.Refrig.Limit = 27;
  Settings.CavityHeat.Label= 0;         // cavity heater
  Settings.CavityHeat.Mode = 1;                 // off
  Settings.CavityHeat.Diff = -5;
  Settings.CavityHeat.DutyCycle = 50;
  Settings.CavityHeat.StandbyOn = 0;     // off in standby
  Settings.KbPref = 0;                  // standard/non-secure

  // email page
  Settings.Email.Alerts = 1;            // disabled
  strcpy(Settings.Email.To,"MyAccount@gmail.com");
  strcpy(Settings.Email.From,"AgriStar.Alerts@Gellert.com");
  strcpy(Settings.Email.Server,"smtp.gmail.com");
  Settings.Email.AuthType = 0;          // startTLS
  Settings.Email.Port = 587;
  strcpy(Settings.Email.Account,"AgriStar.Alerts@gmail.com");
  strcpy(Settings.Email.Password,"4gri*st4r4l3rts");
  strcpy(Settings.Email.Display,"not selected");

  // email alerts setup page
  memset(Settings.AlertsToSend, '0', sizeof(Settings.AlertsToSend));
  for (i = 0; i < WARN_NEWBOARD; i++)
  {
   Settings.AlertsToSend[i] = '1';
  }

  // software/remote off
//  memset(Settings.RemoteOff, 0, sizeof(Settings.RemoteOff));
  Settings.RemoteOff[RO_LIGHTS1] = 1;
  Settings.RemoteOff[RO_LIGHTS2] = 1;

  Settings.FanBoost.Mode = 0;
  Settings.FanBoost.Speed = 80;
  Settings.FanBoost.Interval = 12;
  Settings.FanBoost.Duration = 20;
  Settings.FanBoost.Temp = 40;

  EquipDefine();
  EquipIoInit();
  EquipPwmInit();

  for (i = 0; i < NUM_AUX_OUTPUTS; i++)
  {
    for (j = 0; j < NUM_AUX_PROGRAM_RULES; ++j)
    {
      Settings.AuxProgram[i].Rule[j].Type = RT_UNDEFINED;
      Settings.AuxProgram[i].Rule[j].AndOr = AO_END;
    }
  }
} // end GetFactoryDefault()

/***************************************************************************

FUNCTION: EquipDefine()

PURPOSE:  Setup the IO configuration options

COMMENTS:

***************************************************************************/
void EquipDefine(void)
{
  IO_CONFIG *IO = Settings.EquipIo;
  PWM_CONFIG *PWM = Settings.PWM;

  memset(IO, 0, sizeof(Settings.EquipIo));
  memset(PWM, 0, sizeof(Settings.PWM));

  IO[EQ_FAN].Mode = MODE_ALL;                 IO[EQ_FAN].IO = IO_BOTH;                IO[EQ_FAN].Renamable = 0;
  IO[EQ_DOORS].Mode = MODE_NONE;              IO[EQ_DOORS].IO = IO_NONE;              IO[EQ_DOORS].Renamable = 0;
  IO[EQ_CLIMACELL].Mode = MODE_POTATO;        IO[EQ_CLIMACELL].IO = IO_BOTH;          IO[EQ_CLIMACELL].Renamable = 0;
  IO[EQ_HUMID_HEAD1].Mode = MODE_POTATO;      IO[EQ_HUMID_HEAD1].IO = IO_BOTH;        IO[EQ_HUMID_HEAD1].Renamable = 0;
  IO[EQ_HUMID_PUMP1].Mode = MODE_POTATO;      IO[EQ_HUMID_PUMP1].IO = IO_OUTPUT;      IO[EQ_HUMID_PUMP1].Renamable = 0;
  IO[EQ_HUMID_HEAD2].Mode = MODE_POTATO;      IO[EQ_HUMID_HEAD2].IO = IO_BOTH;        IO[EQ_HUMID_HEAD2].Renamable = 0;
  IO[EQ_HUMID_PUMP2].Mode = MODE_POTATO;      IO[EQ_HUMID_PUMP2].IO = IO_OUTPUT;      IO[EQ_HUMID_PUMP2].Renamable = 0;
  IO[EQ_HUMID_HEAD3].Mode = MODE_POTATO;      IO[EQ_HUMID_HEAD3].IO = IO_BOTH;        IO[EQ_HUMID_HEAD3].Renamable = 0;
  IO[EQ_HUMID_PUMP3].Mode = MODE_POTATO;      IO[EQ_HUMID_PUMP3].IO = IO_OUTPUT;      IO[EQ_HUMID_PUMP3].Renamable = 0;
  IO[EQ_REFRIG_STAGE1].Mode = MODE_ALL;       IO[EQ_REFRIG_STAGE1].IO = IO_BOTH;      IO[EQ_REFRIG_STAGE1].Renamable = 1;
  IO[EQ_REFRIG_STAGE2].Mode = MODE_ALL;       IO[EQ_REFRIG_STAGE2].IO = IO_BOTH;      IO[EQ_REFRIG_STAGE2].Renamable = 1;
  IO[EQ_REFRIG_STAGE3].Mode = MODE_ALL;       IO[EQ_REFRIG_STAGE3].IO = IO_BOTH;      IO[EQ_REFRIG_STAGE3].Renamable = 1;
  IO[EQ_REFRIG_STAGE4].Mode = MODE_ALL;       IO[EQ_REFRIG_STAGE4].IO = IO_BOTH;      IO[EQ_REFRIG_STAGE4].Renamable = 1;
  IO[EQ_REFRIG_STAGE5].Mode = MODE_ALL;       IO[EQ_REFRIG_STAGE5].IO = IO_BOTH;      IO[EQ_REFRIG_STAGE5].Renamable = 1;
  IO[EQ_REFRIG_STAGE6].Mode = MODE_ALL;       IO[EQ_REFRIG_STAGE6].IO = IO_BOTH;      IO[EQ_REFRIG_STAGE6].Renamable = 1;
  IO[EQ_REFRIG_STAGE7].Mode = MODE_ALL;       IO[EQ_REFRIG_STAGE7].IO = IO_BOTH;      IO[EQ_REFRIG_STAGE7].Renamable = 1;
  IO[EQ_REFRIG_STAGE8].Mode = MODE_ALL;       IO[EQ_REFRIG_STAGE8].IO = IO_BOTH;      IO[EQ_REFRIG_STAGE8].Renamable = 1;
  IO[EQ_REFRIG_DEFROST1].Mode = MODE_ALL;     IO[EQ_REFRIG_DEFROST1].IO = IO_BOTH;    IO[EQ_REFRIG_DEFROST1].Renamable = 0;
  IO[EQ_REFRIG_DEFROST2].Mode = MODE_ALL;     IO[EQ_REFRIG_DEFROST2].IO = IO_BOTH;    IO[EQ_REFRIG_DEFROST2].Renamable = 0;
  IO[EQ_HEAT].Mode = MODE_POTATO;             IO[EQ_HEAT].IO = IO_BOTH;               IO[EQ_HEAT].Renamable = 0;
  IO[EQ_CAVITY_HEAT].Mode = MODE_ALL;         IO[EQ_CAVITY_HEAT].IO = IO_BOTH;        IO[EQ_CAVITY_HEAT].Renamable = 0;
  IO[EQ_LIGHTS1].Mode = MODE_ALL;             IO[EQ_LIGHTS1].IO = IO_BOTH;            IO[EQ_LIGHTS1].Renamable = 0;
  IO[EQ_LIGHTS2].Mode = MODE_ALL;             IO[EQ_LIGHTS2].IO = IO_BOTH;            IO[EQ_LIGHTS2].Renamable = 0;
  IO[EQ_BURNER].Mode = MODE_ONION;            IO[EQ_BURNER].IO = IO_BOTH;             IO[EQ_BURNER].Renamable = 0;
  IO[EQ_AUX1].Mode = MODE_ALL;                IO[EQ_AUX1].IO = IO_BOTH;               IO[EQ_AUX1].Renamable = 1;
  IO[EQ_AUX2].Mode = MODE_ALL;                IO[EQ_AUX2].IO = IO_BOTH;               IO[EQ_AUX2].Renamable = 1;
  IO[EQ_AUX3].Mode = MODE_ALL;                IO[EQ_AUX3].IO = IO_BOTH;               IO[EQ_AUX3].Renamable = 1;
  IO[EQ_AUX4].Mode = MODE_ALL;                IO[EQ_AUX4].IO = IO_BOTH;               IO[EQ_AUX4].Renamable = 1;
  IO[EQ_AUX5].Mode = MODE_ALL;                IO[EQ_AUX5].IO = IO_BOTH;               IO[EQ_AUX5].Renamable = 1;
  IO[EQ_AUX6].Mode = MODE_ALL;                IO[EQ_AUX6].IO = IO_BOTH;               IO[EQ_AUX6].Renamable = 1;
  IO[EQ_AUX7].Mode = MODE_ALL;                IO[EQ_AUX7].IO = IO_BOTH;               IO[EQ_AUX7].Renamable = 1;
  IO[EQ_AUX8].Mode = MODE_ALL;                IO[EQ_AUX8].IO = IO_BOTH;               IO[EQ_AUX8].Renamable = 1;
  IO[EQ_POWER].Mode = MODE_NONE;              IO[EQ_POWER].IO = IO_INPUT;             IO[EQ_POWER].Renamable = 0;
  IO[EQ_REMOTE_STANDBY].Mode = MODE_ALL;      IO[EQ_REMOTE_STANDBY].IO = IO_INPUT;    IO[EQ_REMOTE_STANDBY].Renamable = 0;
  IO[EQ_REFRIG_STANDBY].Mode = MODE_ALL;      IO[EQ_REFRIG_STANDBY].IO = IO_INPUT;    IO[EQ_REFRIG_STANDBY].Renamable = 0;
  IO[EQ_REFRIGERATION].Mode = MODE_ALL;       IO[EQ_REFRIGERATION].IO = IO_INPUT;     IO[EQ_REFRIGERATION].Renamable = 0;
  IO[EQ_AIR_FLOW].Mode = MODE_NONE;           IO[EQ_AIR_FLOW].IO = IO_INPUT;          IO[EQ_AIR_FLOW].Renamable = 0;
  IO[EQ_LOW_TEMP].Mode = MODE_NONE;           IO[EQ_LOW_TEMP].IO = IO_INPUT;          IO[EQ_LOW_TEMP].Renamable = 0;
  IO[EQ_REDLIGHT].Mode = MODE_NONE;           IO[EQ_REDLIGHT].IO = IO_OUTPUT;         IO[EQ_REDLIGHT].Renamable = 0;
  IO[EQ_YELLOWLIGHT].Mode = MODE_NONE;        IO[EQ_YELLOWLIGHT].IO = IO_OUTPUT;      IO[EQ_YELLOWLIGHT].Renamable = 0;
  IO[EQ_PULSEDOOR_POWER].Mode = MODE_NONE;    IO[EQ_PULSEDOOR_POWER].IO = IO_OUTPUT;  IO[EQ_PULSEDOOR_POWER].Renamable = 0;
  IO[EQ_PULSEDOOR_OPEN].Mode = MODE_NONE;     IO[EQ_PULSEDOOR_OPEN].IO = IO_OUTPUT;   IO[EQ_PULSEDOOR_OPEN].Renamable = 0;
  IO[EQ_PULSEDOOR_CLOSE].Mode = MODE_NONE;    IO[EQ_PULSEDOOR_CLOSE].IO = IO_OUTPUT;  IO[EQ_PULSEDOOR_CLOSE].Renamable = 0;

  IO[SW_START_STOP].Mode = MODE_ALL;          IO[SW_START_STOP].IO = IO_SWITCH;       IO[SW_START_STOP].Renamable = 0;
  IO[SW_FAN_AUTO].Mode = MODE_ALL;            IO[SW_FAN_AUTO].IO = IO_SWITCH;         IO[SW_FAN_AUTO].Renamable = 0;
  IO[SW_FAN_MANUAL].Mode = MODE_ALL;          IO[SW_FAN_MANUAL].IO = IO_SWITCH;       IO[SW_FAN_MANUAL].Renamable = 0;
  IO[SW_FRESHAIR_AUTO].Mode = MODE_ALL;       IO[SW_FRESHAIR_AUTO].IO = IO_SWITCH;    IO[SW_FRESHAIR_AUTO].Renamable = 0;
  IO[SW_FRESHAIR_MANUAL].Mode = MODE_ALL;     IO[SW_FRESHAIR_MANUAL].IO = IO_SWITCH;  IO[SW_FRESHAIR_MANUAL].Renamable = 0;
  IO[SW_CLIMACELL_AUTO].Mode = MODE_POTATO;   IO[SW_CLIMACELL_AUTO].IO = IO_SWITCH;   IO[SW_CLIMACELL_AUTO].Renamable = 0;
  IO[SW_CLIMACELL_MANUAL].Mode = MODE_POTATO; IO[SW_CLIMACELL_MANUAL].IO = IO_SWITCH; IO[SW_CLIMACELL_MANUAL].Renamable = 0;
  IO[SW_HUMID_AUTO].Mode = MODE_POTATO;       IO[SW_HUMID_AUTO].IO = IO_SWITCH;       IO[SW_HUMID_AUTO].Renamable = 0;
  IO[SW_HUMID_MANUAL].Mode = MODE_POTATO;     IO[SW_HUMID_MANUAL].IO = IO_SWITCH;     IO[SW_HUMID_MANUAL].Renamable = 0;
  IO[SW_REFRIG_AUTO].Mode = MODE_ALL;         IO[SW_REFRIG_AUTO].IO = IO_SWITCH;      IO[SW_REFRIG_AUTO].Renamable = 0;
  IO[SW_AUX1_AUTO].Mode = MODE_ALL;           IO[SW_AUX1_AUTO].IO = IO_SWITCH;        IO[SW_AUX1_AUTO].Renamable = 1;
  IO[SW_AUX1_MANUAL].Mode = MODE_ALL;         IO[SW_AUX1_MANUAL].IO = IO_SWITCH;      IO[SW_AUX1_MANUAL].Renamable = 1;
  IO[SW_AUX2_AUTO].Mode = MODE_ALL;           IO[SW_AUX2_AUTO].IO = IO_SWITCH;        IO[SW_AUX2_AUTO].Renamable = 1;
  IO[SW_AUX2_MANUAL].Mode = MODE_ALL;         IO[SW_AUX2_MANUAL].IO = IO_SWITCH;      IO[SW_AUX2_MANUAL].Renamable = 1;
  IO[SW_CURE_AUTO].Mode = MODE_ONION;         IO[SW_CURE_AUTO].IO = IO_SWITCH;        IO[SW_CURE_AUTO].Renamable = 0;
  IO[SW_BURNER_AUTO].Mode = MODE_ONION;       IO[SW_BURNER_AUTO].IO = IO_SWITCH;      IO[SW_BURNER_AUTO].Renamable = 0;

  PWM[PWM_DOORS].Mode = MODE_ALL;
  PWM[PWM_REFRIGERATION].Mode = MODE_ALL;
  PWM[PWM_FAN].Mode = MODE_ALL;
  PWM[PWM_BURNER].Mode = MODE_ONION;
} // end EquipDefine()

/***************************************************************************

FUNCTION:   EquipIoInit()

PURPOSE:    Equipment I/O default definition

COMMENTS:   The value is the board and port combination which is of the form
            value = (board * SS_PORT_ID_MULTIPLIER) + port (similar to sensor IDs)

***************************************************************************/
void EquipIoInit(void)
{
  int ioIndex;
  int mainBoard = MAIN * SS_PORT_ID_MULTIPLIER;
  char input = 'i';
  char output = 'o';

  for (ioIndex = 0; ioIndex < SW_START_STOP; ioIndex++)
  {
    switch (ioIndex)
    {
      // lights
      case EQ_REDLIGHT:
        SetIoConfig((EQUIPMENT_IO) ioIndex, input, IO_UNDEFINED);
        SetIoConfig((EQUIPMENT_IO) ioIndex, output, mainBoard + 1);
        break;
      case EQ_YELLOWLIGHT:
        SetIoConfig((EQUIPMENT_IO) ioIndex, input, IO_UNDEFINED);
        SetIoConfig((EQUIPMENT_IO) ioIndex, output, mainBoard + 2);
        break;

      // equipment
      case EQ_FAN:
        SetIoConfig((EQUIPMENT_IO) ioIndex, input, mainBoard + 3);
        SetIoConfig((EQUIPMENT_IO) ioIndex, output, mainBoard + 3);
        break;
      case EQ_CLIMACELL:
        if (Settings.SystemMode == SM_POTATO)
        {
          SetIoConfig((EQUIPMENT_IO) ioIndex, input, mainBoard + 4);
          SetIoConfig((EQUIPMENT_IO) ioIndex, output, mainBoard + 4);
        }
        else
        {
          SetIoConfig((EQUIPMENT_IO) ioIndex, input, IO_UNDEFINED);
          SetIoConfig((EQUIPMENT_IO) ioIndex, output, IO_UNDEFINED);
        }
        break;
      case EQ_HUMID_HEAD1:
        if (Settings.SystemMode == SM_POTATO)
        {
          SetIoConfig((EQUIPMENT_IO) ioIndex, input, mainBoard + 5);
          SetIoConfig((EQUIPMENT_IO) ioIndex, output, mainBoard + 5);
        }
        else
        {
          SetIoConfig((EQUIPMENT_IO) ioIndex, input, IO_UNDEFINED);
          SetIoConfig((EQUIPMENT_IO) ioIndex, output, IO_UNDEFINED);
        }
        break;
      case EQ_HUMID_PUMP1:
        if (Settings.SystemMode == SM_POTATO)
        {
          SetIoConfig((EQUIPMENT_IO) ioIndex, input, IO_UNDEFINED);
          SetIoConfig((EQUIPMENT_IO) ioIndex, output, mainBoard + 6);
        }
        else
        {
          SetIoConfig((EQUIPMENT_IO) ioIndex, input, IO_UNDEFINED);
          SetIoConfig((EQUIPMENT_IO) ioIndex, output, IO_UNDEFINED);
        }
        break;
      case EQ_PULSEDOOR_POWER:
        SetIoConfig((EQUIPMENT_IO) ioIndex, input, IO_UNDEFINED);
//        SetIoConfig((EQUIPMENT_IO) ioIndex, output, mainBoard + 9);
        Settings.EquipIo[EQ_PULSEDOOR_POWER].Output = 1;  // indicates pulse doors are enabled
        break;
      case EQ_PULSEDOOR_CLOSE:
        SetIoConfig((EQUIPMENT_IO) ioIndex, input, IO_UNDEFINED);
        SetIoConfig((EQUIPMENT_IO) ioIndex, output, mainBoard + 10);
        break;
      case EQ_PULSEDOOR_OPEN:
        SetIoConfig((EQUIPMENT_IO) ioIndex, input, IO_UNDEFINED);
        SetIoConfig((EQUIPMENT_IO) ioIndex, output, mainBoard + 11);
        break;

      // inputs only
      case EQ_POWER:
        SetIoConfig((EQUIPMENT_IO) ioIndex, input, mainBoard + 1);
        SetIoConfig((EQUIPMENT_IO) ioIndex, output, IO_UNDEFINED);
        break;
      case EQ_REMOTE_STANDBY:
        SetIoConfig((EQUIPMENT_IO) ioIndex, input, mainBoard + 2);
        SetIoConfig((EQUIPMENT_IO) ioIndex, output, IO_UNDEFINED);
        break;
      case EQ_AIR_FLOW:
        SetIoConfig((EQUIPMENT_IO) ioIndex, input, mainBoard + 9);
        SetIoConfig((EQUIPMENT_IO) ioIndex, output, IO_UNDEFINED);
        break;
      case EQ_LOW_TEMP:
        SetIoConfig((EQUIPMENT_IO) ioIndex, input, mainBoard + 10);
        SetIoConfig((EQUIPMENT_IO) ioIndex, output, IO_UNDEFINED);
        break;

      default:
        SetIoConfig((EQUIPMENT_IO) ioIndex, input, IO_UNDEFINED);
        SetIoConfig((EQUIPMENT_IO) ioIndex, output, IO_UNDEFINED);
        break;
    }
  }
} // end EquipIoInit()

/***************************************************************************

FUNCTION:   EquipPwmInit()

PURPOSE:    Equipment PWM default definition

COMMENTS:

***************************************************************************/
void EquipPwmInit(void)
{
  Settings.PWM[PWM_DOORS].Enabled = 1;
  Settings.PWM[PWM_DOORS].Channel = 0;  // PWM channel index
  Settings.PWM[PWM_DOORS].SysConfigWarnIoIndex = EQ_DOORS;

  Settings.PWM[PWM_REFRIGERATION].Enabled = 0;
  Settings.PWM[PWM_REFRIGERATION].Channel = PWM_UNDEFINED;
  Settings.PWM[PWM_REFRIGERATION].SysConfigWarnIoIndex = EQ_REFRIGERATION;

  Settings.PWM[PWM_FAN].Enabled = 1;
  Settings.PWM[PWM_FAN].Channel = 1;    // PWM channel index
  Settings.PWM[PWM_FAN].SysConfigWarnIoIndex = EQ_FAN;

  Settings.PWM[PWM_BURNER].Enabled = 0;
  Settings.PWM[PWM_BURNER].Channel = PWM_UNDEFINED;
  Settings.PWM[PWM_BURNER].SysConfigWarnIoIndex = EQ_BURNER;
} // end EquipPwmInit()

/***************************************************************************

FUNCTION:   NetworkNodeAdd()

PURPOSE:    Add/edit a node in the network (remote) node list

COMMENTS:

***************************************************************************/
void NetworkNodeAdd(char *ID, char *IP, char *PublicIP, short Port)
{
  int i = 0;

  // see if the node is in the list
  while (strcmp(Settings.Node[i].IP, IP) != 0 && i++ < NUM_NETWORK_NODES);

  // update it
  if (i < NUM_NETWORK_NODES)
  {
    StringCopy(Settings.Node[i].ID, ID, SITE_ID_LEN+1);
    StringCopy(Settings.Node[i].IP, IP, IP_ADD_LEN+1);
    StringCopy(Settings.Node[i].PublicIP, PublicIP, IP_ADD_LEN+1);
    Settings.Node[i].HttpPort = Port;
    return;
  }

  // otherwise, if there is room, add it
  i = 0;
  while (strcmp(Settings.Node[i].ID, "") != 0 && i++ < NUM_NETWORK_NODES);

  if (i < NUM_NETWORK_NODES)
  {
    StringCopy(Settings.Node[i].ID, ID, SITE_ID_LEN+1);
    StringCopy(Settings.Node[i].IP, IP, IP_ADD_LEN+1);
    StringCopy(Settings.Node[i].PublicIP, PublicIP, IP_ADD_LEN+1);
    Settings.Node[i].HttpPort = Port;
  }
} // end NetworkNodeAdd()

/***************************************************************************

FUNCTION:   NetworkNodeDelete()

PURPOSE:    Delete a network (remote) node from the node list

COMMENTS:

***************************************************************************/
void NetworkNodeDelete(char *IP)
{
  int i = 0;

  while (strcmp(Settings.Node[i].IP, IP) != 0 && i++ < NUM_NETWORK_NODES);

  if (i < NUM_NETWORK_NODES)
    memset(&(Settings.Node[i]), 0, sizeof(NETWORK_NODES));
} // end NetworkNodeDelete()

/***************************************************************************

FUNCTION:   DeleteNeNetworkNodeDeleteAllworkNodeAll()

PURPOSE:    Delete all network (remote) nodes from the node list

COMMENTS:

***************************************************************************/
void NetworkNodeDeleteAll(void)
{
  memset(Settings.Node, 0, sizeof(Settings.Node));
} // end NetworkNodeDeleteAll()

/***************************************************************************

FUNCTION:   SettingsRestoreFromLocalFlash()

PURPOSE:    Restore the settings from external flash

COMMENTS:

***************************************************************************/
int SettingsRestoreFromLocalFlash(void)
{
  int bufLen;
  int bufIndex = 0;
  int strIndex;
  char *length;
  char str[MSG_TX_BUFFER_SIZE] = "";
  unsigned char header[FLASHFILE_HDR_LEN+1] = "";
  unsigned char *readBuffer;

  FlashRead(header, FLASH_SETTINGS_SAVE_FILE, FLASHFILE_HDR_LEN);
  debug_printf("Firmware header:%s\r\n", header);

  if (strncmp((const char *) header, SETTINGS_FILE_TAG, strlen(SETTINGS_FILE_TAG)) == 0)
  {
    length = strchr((const char *) header, ':');
    length++;
    bufLen = atoi(length);
    readBuffer = malloc(bufLen);
    if (readBuffer == 0)
    {
      WarningsSet(WARN_SETTINGSCNVRTERR, FM_ALARM, FM_ALARM, NA);
      return 0;
    }

    FlashRead(readBuffer, FLASH_SETTINGS_SAVE_FILE+FLASHFILE_HDR_LEN, SETTINGS_FILE_SIZE);
    do
    {
      strIndex = 0;

      while (readBuffer[bufIndex] != 0 && strIndex < MSG_TX_BUFFER_SIZE)
      {
        str[strIndex++] = readBuffer[bufIndex++];
      }

      bufIndex++;
      str[strIndex] = 0;    // terminate the string

      if (strlen((const char *) str) > 0)
      {
        StoreSettingsValue(str);
//        debug_printf("%s\r\n", str);
      }
    }
    while (strcmp((const char *) str, "EOF") != 0 && bufIndex <= bufLen);
    free(readBuffer);
  }

  debug_printf(" done!\r\n");

  if (strcmp((const char *) str, "EOF") == 0)
  {
    return 1;
  }
  else
  {
    return 0;
  }
} // end SettingsRestoreFromLocalFlash()

/***************************************************************************

FUNCTION:   StoreSettingsValue()

PURPOSE:    Restore the settings from the ASCII flash file

COMMENTS:

***************************************************************************/
void StoreSettingsValue(char *Msg)
{
  // format the settings as if they were received from the LTX during a
  // settings restore from a file on the Display, this allows the use
  // of StoreSettings() to restore the system settings
  strcpy(UI_Message.RxBuffer, MSG_START_DELIMITER_STR);
  strncat(UI_Message.RxBuffer, Msg,                   (sizeof(UI_Message.RxBuffer)-strlen(UI_Message.RxBuffer)-1));
  strncat(UI_Message.RxBuffer, MSG_TERMINATOR_STR,    (sizeof(UI_Message.RxBuffer)-strlen(UI_Message.RxBuffer)-1));
  strncat(UI_Message.RxBuffer, "0",                   (sizeof(UI_Message.RxBuffer)-strlen(UI_Message.RxBuffer)-1));
  strncat(UI_Message.RxBuffer, MSG_END_DELIMITER_STR, (sizeof(UI_Message.RxBuffer)-strlen(UI_Message.RxBuffer)-1));

  // parse the post to prepare it for StoreSettings()
  ParsePost();
  StoreSettings();
} // end StoreSettingsValue()

/***************************************************************************

FUNCTION:   ReadSettingsBank()

PURPOSE:    Read and CRC-validate a single settings bank

COMMENTS:   Returns 1 if valid, 0 if CRC mismatch or empty.

***************************************************************************/
static unsigned int ReadSettingsBank(void *SettingsPtr, int Size, unsigned int FlashAddr)
{
  unsigned int CalculatedCrc;
  unsigned int StoredCrc;

  FlashRead((unsigned char*) SettingsPtr, FlashAddr, Size);

  StoredCrc = *((unsigned int *)(((char *)(SettingsPtr))+(Size - sizeof(Settings.CRC))));
  CalculatedCrc = CRC((char *) SettingsPtr, Size - sizeof(Settings.CRC));

  if (StoredCrc == CalculatedCrc)
  {
    debug_printf("Bank @ 0x%08X CRC:%08X OK\r\n", FlashAddr, CalculatedCrc);
    return 1;
  }
  else
  {
    debug_printf("Bank @ 0x%08X BAD CRC:%08X, Stored:%08X\r\n", FlashAddr, CalculatedCrc, StoredCrc);
    return 0;
  }
}

/***************************************************************************

FUNCTION:   ReadSettings()

PURPOSE:    Read the system Settings from A/B dual-bank flash

COMMENTS:   Tries both banks.  If both are valid, the one with the higher
            SaveSeq wins.  If only one is valid, that one is used.
            LastWrittenBank is set so the next save goes to the OTHER bank.

***************************************************************************/
unsigned int ReadSettings(void *SettingsPtr, int Size, int Type)
{
  SYSTEM_SETTINGS TempA, TempB;
  int ValidA = 0, ValidB = 0;

  if (Type != ACTIVE_SETTINGS)
    return 0;

  ValidA = ReadSettingsBank(&TempA, Size, FLASH_SETTINGS_BANK_A);
  ValidB = ReadSettingsBank(&TempB, Size, FLASH_SETTINGS_BANK_B);

  if (ValidA && ValidB)
  {
    // Both valid — pick highest sequence number
    if (TempB.SaveSeq > TempA.SaveSeq)
    {
      memcpy(SettingsPtr, &TempB, Size);
      LastWrittenBank = 1;  // B was newest, next write goes to A
      debug_printf("A/B: Both valid, B newer (seq %u > %u)\r\n", TempB.SaveSeq, TempA.SaveSeq);
    }
    else
    {
      memcpy(SettingsPtr, &TempA, Size);
      LastWrittenBank = 0;  // A was newest, next write goes to B
      debug_printf("A/B: Both valid, A newer (seq %u >= %u)\r\n", TempA.SaveSeq, TempB.SaveSeq);
    }
    return 1;
  }
  else if (ValidA)
  {
    memcpy(SettingsPtr, &TempA, Size);
    LastWrittenBank = 0;  // A is the only valid copy, next write goes to B
    debug_printf("A/B: Only Bank A valid (seq %u)\r\n", TempA.SaveSeq);
    return 1;
  }
  else if (ValidB)
  {
    memcpy(SettingsPtr, &TempB, Size);
    LastWrittenBank = 1;  // B is the only valid copy, next write goes to A
    debug_printf("A/B: Only Bank B valid (seq %u)\r\n", TempB.SaveSeq);
    return 1;
  }
  else
  {
    debug_printf("A/B: BOTH banks invalid — factory default required\r\n");
    WarningsSet(WARN_SAVESETTINGS, FM_ALARM, FM_ALARM, NA);
    return 0;
  }
} // end ReadSettings()

/***************************************************************************

FUNCTION:   SaveSettings()

PURPOSE:    Write the system Settings to flash using A/B dual-bank

COMMENTS:   Always writes to the OPPOSITE bank from the last successful
            read/write.  This ensures that if power is lost during erase/write,
            the other bank still holds a valid copy.
            Steps:  1. Increment SaveSeq
                    2. Compute CRC
                    3. Erase + write the inactive bank
                    4. Flip LastWrittenBank

***************************************************************************/
int SaveSettings(SYSTEM_SETTINGS_TYPE Type)
{
  if (Type == ACTIVE_SETTINGS)
  {
    // Write to the bank we did NOT read from (the stale one)
    int TargetBank = (LastWrittenBank == 0) ? 1 : 0;
    unsigned int TargetAddr = (TargetBank == 0) ? FLASH_SETTINGS_BANK_A : FLASH_SETTINGS_BANK_B;

    Settings.SaveSeq++;
    Settings.CRC = CRC((char *)(&Settings), sizeof(Settings) - sizeof(Settings.CRC));

    debug_printf("A/B: Writing Bank %c (seq %u)\r\n", TargetBank ? 'B' : 'A', Settings.SaveSeq);

    FlashEraseArea(TargetAddr, sizeof(Settings));
    FlashWrite(TargetAddr, (unsigned char*)&Settings, sizeof(Settings));

    LastWrittenBank = TargetBank;
  }
  else
  {
    SettingsSendToLocalFlash();
  }

  RequestSettingsSave(SR_CLEAR, Type);
  return 1;
} // end SaveSettings()

/***************************************************************************

FUNCTION:   RequestSettingsSave()

PURPOSE:    Request settings save

COMMENTS:

***************************************************************************/
void RequestSettingsSave(SAVE_SETTINGS_REQUEST Request, SYSTEM_SETTINGS_TYPE Type)
{
  SaveSettingsRequest.Status = Request;
  SaveSettingsRequest.Type = Type;
} // end RequestSettingsSave()

/***************************************************************************

FUNCTION:   SendSettingsRecord()

PURPOSE:    ASCII format a settings element and store in external flash
            or send to the Display to be saved in a text file

COMMENTS:   The length of label string must be less than MSG_MAX_TAG_LEN-1
            because the restore process uses the PostTag[]/PostValue[]
            structures. If not, it will be truncated and therefore won't
            match the target string in StoreSettings().

***************************************************************************/
void SendSettingsRecord(uint32_t *bytesWritten, void *Element, char *Label, ELEMENT_TYPE Type)
{
  int i;
  char value[10];
  char tag[50];
  char message[MSG_TX_BUFFER_SIZE];
  char temp[100] = {0};

  if (*bytesWritten > 0)
  {
    strcpy(tag, "RestoreSettings=1&");
  }
  else
  {
    strcpy(tag, "SaveSettings=");
  }

  switch (Type)
  {
  case ET_STRING:
    snprintf(message, MSG_TX_BUFFER_SIZE, "%s%s=%s", tag, Label, (char *) Element);
    break;

  case ET_CHAR:
    snprintf(message, MSG_TX_BUFFER_SIZE, "%s%s=%d", tag, Label, *((char *) Element));
    break;

  case ET_SHORT:
    snprintf(message, MSG_TX_BUFFER_SIZE, "%s%s=%d", tag, Label, *((int16_t *) Element));
    break;

  case ET_USHORT:
    snprintf(message, MSG_TX_BUFFER_SIZE, "%s%s=%d", tag, Label, *((uint16_t *) Element));
    break;

  case ET_INT32:
    snprintf(message, MSG_TX_BUFFER_SIZE, "%s%s=%d", tag, Label, *((int32_t *) Element));
    break;

  case ET_UINT32:
    snprintf(message, MSG_TX_BUFFER_SIZE, "%s%s=%d", tag, Label, *((uint32_t *) Element));
    break;

  case ET_FLOAT:
    snprintf(message, MSG_TX_BUFFER_SIZE, "%s%s=%.1f", tag, Label, *((float *) Element));
    break;

  case ET_RUNTIME:
    for (i = 0; i < 48; i++)
    {
      snprintf(value, sizeof(value), "%d", ((char *)Element)[i]);

      if ((sizeof(temp)-1) > (strlen(temp) + strlen(value)))
      {
        strcat(temp, value);
      }
    }
    snprintf(message, MSG_TX_BUFFER_SIZE, "%s%s=%s", tag, Label, temp);
    break;

  default:
    break;
  }

  if (*bytesWritten > 0)
  {
//    debug_printf("%s\r\n", str);
    FlashWrite(FLASH_SETTINGS_SAVE_FILE + *bytesWritten, (unsigned char*)message, strlen(message)+1);
    *bytesWritten += (strlen(message)+1);
//    debug_printf("Total settings bytes written to flash:%d\r\n", *bytesWritten);
  }
  else
  {
    SendMsgAndWaitForResponse(message, UI_DELAY_LONG);
    ThreadMonitorUpdate(TM_UI_UPDATE);
  }

  // send end-of-record (EOR) message - causes LTX to write record to the file
//  snprintf(str, MSG_TX_BUFFER_SIZE, "LogToFile=EOR,%d", SessionID);
//  SendMsgAndWaitForResponse(str, UI_DELAY_LONG);
} // end SendSettingsRecord()

/***************************************************************************

FUNCTION:   SendSettings()

PURPOSE:    Send the settings to be ASCII formatted

COMMENTS:   The length of label string sent to SendSettingsRecord() must be
            less than MSG_MAX_TAG_LEN-1 because the restore process uses the
            PostTag[]/PostValue[] structures. If not, it will be truncated
            and therefore won't match the target string in StoreSettings().

***************************************************************************/
void SendSettings(uint32_t *bytesWritten)
{
  int  i,j;
  uint8_t AmPm = 0;
  char str[MSG_TX_BUFFER_SIZE];
  char DateStr[DATE_LEN+1];
  char TimeStr[TIME_LEN+1];

  GetDateStr(DateStr);
  GetTimeStr(TimeStr, &AmPm);
  snprintf(str, MSG_TX_BUFFER_SIZE, "%s %s%s", DateStr, TimeStr, (AmPm == 0 ? "am" : "pm"));
  SendSettingsRecord(bytesWritten, str, "Time", ET_STRING);

  SendSettingsRecord(bytesWritten, &Settings.SettingsVersion, "SettingsVersion", ET_STRING);

  SendSettingsRecord(bytesWritten, &Settings.DST, "DST", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.HttpPort, "HttpPort", ET_USHORT);

  SendSettingsRecord(bytesWritten, &Settings.Log.User.Interval, "UserLogInterval", ET_SHORT);
  SendSettingsRecord(bytesWritten, &Settings.Log.User.Wrap, "UserLogWrap", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Log.PID.Wrap, "PIDLogWrap", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Log.PID.Door, "LogPIDDoor", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Log.PID.Refrig, "LogPIDRefrig", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Log.GraphFavorites, "GraphFavorites", ET_STRING);

  // basic setup page
  SendSettingsRecord(bytesWritten, &Settings.StorageName, "StorageName", ET_STRING);
  SendSettingsRecord(bytesWritten, &Settings.TempType, "TempType", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.MasterSlave, "MasterSlave", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.MasterIP, "MasterIP", ET_STRING);
  SendSettingsRecord(bytesWritten, &Settings.HomePage, "HomePage", ET_STRING);
  SendSettingsRecord(bytesWritten, &Settings.SystemMode, "SystemMode", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.MultiviewSessions, "MultiviewSessions", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.PublicIP, "PublicIP", ET_STRING);
//  SendSettingsRecord(bytesWritten, &Settings.Language, "Language", ET_CHAR);
//  SendSettingsRecord(bytesWritten, &Settings.FactoryBd, "FactoryBd", ET_STRING);
  SendSettingsRecord(bytesWritten, &Settings.LoginPw, "LoginPw", ET_STRING);
  SendSettingsRecord(bytesWritten, &Settings.LocalLogin, "LocalLogin", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Animations, "Animations", ET_CHAR);

  // password page
  for (i = 0; i < NUM_PASSWORDS; i++)
  {
    if (Settings.User[i].ID[0] != 0)
    {
      snprintf(str, MSG_TX_BUFFER_SIZE, "User=%d&ID", i);
      SendSettingsRecord(bytesWritten, &Settings.User[i].ID, str, ET_STRING);

      snprintf(str, MSG_TX_BUFFER_SIZE, "User=%d&Password", i);
      SendSettingsRecord(bytesWritten, &Settings.User[i].Password, str, ET_STRING);
    }
  }

  // analog board setup page
  for (i = 0; i < ANALOG_BOARDS_PER_SYSTEM; i++)
  {
    if (Settings.AnalogBoard[i].Present == 1)
    {
      snprintf(str, MSG_TX_BUFFER_SIZE, "AnalogBoard=%d&Address", i);
      SendSettingsRecord(bytesWritten, &Settings.AnalogBoard[i].Address, str, ET_CHAR);

      snprintf(str, MSG_TX_BUFFER_SIZE, "AnalogBoard=%d&Type", i);
      SendSettingsRecord(bytesWritten, &Settings.AnalogBoard[i].Type, str, ET_CHAR);

      snprintf(str, MSG_TX_BUFFER_SIZE, "AnalogBoard=%d&Label", i);
      SendSettingsRecord(bytesWritten, &Settings.AnalogBoard[i].Label, str, ET_STRING);

      snprintf(str, MSG_TX_BUFFER_SIZE, "AnalogBoard=%d&Disabled", i);
      SendSettingsRecord(bytesWritten, &Settings.AnalogBoard[i].Disabled, str, ET_CHAR);

      for (j = 0; j < ANALOG_SENSORS_PER_BOARD; j++)
      {
        snprintf(str, MSG_TX_BUFFER_SIZE, "AnalogSensor=%d&Board=%d&Type", j, i);
        SendSettingsRecord(bytesWritten, &Settings.AnalogBoard[i].Sensor[j].Type, str, ET_CHAR);

        snprintf(str, MSG_TX_BUFFER_SIZE, "AnalogSensor=%d&Board=%d&Label", j, i);
        SendSettingsRecord(bytesWritten, &Settings.AnalogBoard[i].Sensor[j].Label, str, ET_STRING);

        snprintf(str, MSG_TX_BUFFER_SIZE, "AnalogSensor=%d&Board=%d&Offset", j, i);
        SendSettingsRecord(bytesWritten, &Settings.AnalogBoard[i].Sensor[j].Offset, str, ET_FLOAT);

        snprintf(str, MSG_TX_BUFFER_SIZE, "AnalogSensor=%d&Board=%d&Disabled", j, i);
        SendSettingsRecord(bytesWritten, &Settings.AnalogBoard[i].Sensor[j].Disabled, str, ET_CHAR);
      }
    }
  }

  // fresh air door setup page
  SendSettingsRecord(bytesWritten, &Settings.Door.PID.P, "DoorP", ET_SHORT);
  SendSettingsRecord(bytesWritten, &Settings.Door.PID.I, "DoorI", ET_SHORT);
  SendSettingsRecord(bytesWritten, &Settings.Door.PID.D, "DoorD", ET_SHORT);
  SendSettingsRecord(bytesWritten, &Settings.Door.PID.U, "DoorU", ET_SHORT);
  SendSettingsRecord(bytesWritten, &Settings.Door.ActuatorTime, "DoorActuatorTime", ET_SHORT);
  SendSettingsRecord(bytesWritten, &Settings.Door.CoolAirCycle, "CoolAirCycle", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.PWM[PWM_DOORS].Channel, "PWM_doors", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.PWM[PWM_REFRIGERATION].Channel, "PWM_refrig", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.PWM[PWM_FAN].Channel, "PWM_fan", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.PWM[PWM_BURNER].Channel, "PWM_burner", ET_CHAR);

  // refrigeration setup page
  SendSettingsRecord(bytesWritten, &Settings.Refrig.PID.P, "RefrP", ET_SHORT);
  SendSettingsRecord(bytesWritten, &Settings.Refrig.PID.I, "RefrI", ET_SHORT);
  SendSettingsRecord(bytesWritten, &Settings.Refrig.PID.D, "RefrD", ET_SHORT);
  SendSettingsRecord(bytesWritten, &Settings.Refrig.PID.U, "RefrU", ET_SHORT);
  SendSettingsRecord(bytesWritten, &Settings.Refrig.Purge, "RefrPurge", ET_CHAR);

  for (i = 0; i < NUM_REFRIG_STAGES; i++)
  {
    snprintf(str, MSG_TX_BUFFER_SIZE, "Refr=%d&On", i);
    SendSettingsRecord(bytesWritten, &Settings.Refrig.Stage[i].On, str, ET_CHAR);

    snprintf(str, MSG_TX_BUFFER_SIZE, "Refr=%d&Off", i);
    SendSettingsRecord(bytesWritten, &Settings.Refrig.Stage[i].Off, str, ET_CHAR);
  }

  // climacell setup page
  SendSettingsRecord(bytesWritten, &Settings.Climacell.PID.P, "ClimP", ET_SHORT);
  SendSettingsRecord(bytesWritten, &Settings.Climacell.PID.I, "ClimI", ET_SHORT);
  SendSettingsRecord(bytesWritten, &Settings.Climacell.PID.D, "ClimD", ET_SHORT);
  SendSettingsRecord(bytesWritten, &Settings.Climacell.PID.U, "ClimU", ET_SHORT);
  SendSettingsRecord(bytesWritten, &Settings.Climacell.Efficiency, "ClimacellEff", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Climacell.Altitude, "Altitude", ET_SHORT);
  SendSettingsRecord(bytesWritten, &Settings.Climacell.AltitudeUnits, "AltitudeType", ET_CHAR);

  // burner setup page
  SendSettingsRecord(bytesWritten, &Settings.Burner.PID.P, "BurnerP", ET_SHORT);
  SendSettingsRecord(bytesWritten, &Settings.Burner.PID.I, "BurnerI", ET_SHORT);
  SendSettingsRecord(bytesWritten, &Settings.Burner.PID.D, "BurnerD", ET_SHORT);
  SendSettingsRecord(bytesWritten, &Settings.Burner.PID.U, "BurnerU", ET_SHORT);
  SendSettingsRecord(bytesWritten, &Settings.Burner.Mode, "BurnerMode", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Burner.On, "BurnerOn", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Burner.Low, "BurnerLow", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Burner.Manual, "BurnerManual", ET_CHAR);

  // failures 1 setup page
  SendSettingsRecord(bytesWritten, &Settings.Failure[FAIL_FAN].Mode, "FanFailMode", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Failure[FAIL_FAN].Timer, "FanFailTimer", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Failure[FAIL_CLIMACELL].Mode, "ClimacellFailMode", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Failure[FAIL_CLIMACELL].Timer, "ClimacellFailTimer", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Failure[FAIL_REFRIGERATION].Mode, "RefrFailMode", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Failure[FAIL_REFRIGERATION].Timer, "RefrFailTimer", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Refrig.FailMode, "RefrRunFailMode", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Failure[FAIL_HUMIDIFIERS].Mode, "HumidFailMode", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Failure[FAIL_HUMIDIFIERS].Timer, "HumidFailTimer", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Failure[FAIL_AUXILIARY].Mode, "AuxFailMode", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Failure[FAIL_AUXILIARY].Timer, "AuxFailTimer", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Failure[FAIL_HEAT].Mode, "HeatFailMode", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Failure[FAIL_HEAT].Timer, "HeatFailTimer", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Failure[FAIL_CAVITY_HEAT].Mode, "CavHeatFailMode", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Failure[FAIL_CAVITY_HEAT].Timer, "CavHeatFailTimer", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Failure[FAIL_BURNER].Mode, "BurnerFailMode", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Failure[FAIL_BURNER].Timer, "BurnerFailTimer", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Failure[FAIL_LIGHTS].Mode, "LightsFailMode", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Failure[FAIL_LIGHTS].Timer, "LightsFailTimer", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.LightsFailUnits, "LightsFailUnits", ET_CHAR);

  // failures 2 setup page
  SendSettingsRecord(bytesWritten, &Settings.Failure[FAIL_PLENUM_SENSOR].Mode, "PlenSenFailMode", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Failure[FAIL_PLENUM_SENSOR].Timer, "PlenSenFailTimer", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Plenum.SensorDiff, "PlenSenDiff", ET_FLOAT);
  SendSettingsRecord(bytesWritten, &Settings.Failure[FAIL_OUTSIDE_AIR].Mode, "OutAirFailMode", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Failure[FAIL_OUTSIDE_AIR].Timer, "OutAirFailTimer", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Failure[FAIL_OUTSIDE_HUMIDITY].Mode, "OutHumFailMode", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Failure[FAIL_OUTSIDE_HUMIDITY].Timer, "OutHumFailTimer", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Failure[FAIL_PLENUM_HUMIDITY].Mode, "PlenHumFailMode", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Failure[FAIL_PLENUM_HUMIDITY].Timer, "PlenHumFailTimer", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Plenum.HumidLowFailure, "LowHumSet", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Failure[FAIL_HIGH_CO2].Mode, "HighCo2FailMode", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Failure[FAIL_HIGH_CO2].Timer, "HighCo2FailTimer", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Co2.HighFailure, "HighCo2Set", ET_INT32);

  // controller access page
  for (i = 0; i < NUM_NETWORK_NODES; i++)
  {
    if (strcmp(Settings.Node[i].ID, "") != 0)
    {
      snprintf(str, MSG_TX_BUFFER_SIZE, "Node=%d&ID", i);
      SendSettingsRecord(bytesWritten, &Settings.Node[i].ID, str, ET_STRING);

      snprintf(str, MSG_TX_BUFFER_SIZE, "Node=%d&IP", i);
      SendSettingsRecord(bytesWritten, &Settings.Node[i].IP, str, ET_STRING);

      snprintf(str, MSG_TX_BUFFER_SIZE, "Node=%d&PublicIP", i);
      SendSettingsRecord(bytesWritten, &Settings.Node[i].PublicIP, str, ET_STRING);

      snprintf(str, MSG_TX_BUFFER_SIZE, "Node=%d&HttpPort", i);
      SendSettingsRecord(bytesWritten, &Settings.Node[i].HttpPort, str, ET_SHORT);
    }
  }

  // service info page
  SendSettingsRecord(bytesWritten, &Settings.DealerName, "DealerName", ET_STRING);
  SendSettingsRecord(bytesWritten, &Settings.DealerPhone, "DealerPhone", ET_STRING);
  SendSettingsRecord(bytesWritten, &Settings.TechName, "TechName", ET_STRING);
  SendSettingsRecord(bytesWritten, &Settings.TechPhone, "TechPhone", ET_STRING);

  // plenum setup page
  SendSettingsRecord(bytesWritten, &Settings.Plenum.TempSet, "PlenTempSet", ET_FLOAT);
  SendSettingsRecord(bytesWritten, &Settings.Plenum.HumidSet, "PlenHumSet", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Fan.DailyDay, "FanDailyDay", ET_UINT32);
  SendSettingsRecord(bytesWritten, &Settings.Fan.DailyRunTime, "FanDailyRunTime", ET_UINT32);
  SendSettingsRecord(bytesWritten, &Settings.Fan.TotalRunTime, "FanTotalRunTime", ET_UINT32);
  SendSettingsRecord(bytesWritten, &Settings.Fan.TotalRefrigTime, "FanTotalRefrigTime", ET_UINT32);
  SendSettingsRecord(bytesWritten, &Settings.Fan.TotalCoolingTime, "FanTotalCoolingTime", ET_UINT32);
  SendSettingsRecord(bytesWritten, &Settings.Fan.TotalRecircTime, "FanTotalRecircTime", ET_UINT32);
  SendSettingsRecord(bytesWritten, &Settings.Fan.TotalStandbyTime, "FanTotalStandbyTime", ET_UINT32);
  SendSettingsRecord(bytesWritten, &Settings.Fan.TotalCureTime, "FanTotalCureTime", ET_UINT32);
  SendSettingsRecord(bytesWritten, &Settings.Plenum.HumidSetpointRef, "HumSetpointRef", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Burner.TempSet, "BurnerTempSet", ET_FLOAT);
  SendSettingsRecord(bytesWritten, &Settings.Burner.Threshold, "BurnerThreshold", ET_FLOAT);

  // loading monitor setup page
  for (i = 0; i < NUM_LOADLOG_SENSORS; i++)
  {
    // LoadMonitor=0 is non-standard because the '0' isn't used in the restore because LoadMonitor is not an array
    snprintf(str, MSG_TX_BUFFER_SIZE, "LoadMonitor=0&Bay=%d&Label", i);
    SendSettingsRecord(bytesWritten, &Settings.LoadMonitor.Bay[i].Label, str, ET_STRING);

    snprintf(str, MSG_TX_BUFFER_SIZE, "LoadMonitor=0&Bay=%d&SensorID", i);
    SendSettingsRecord(bytesWritten, &Settings.LoadMonitor.Bay[i].SensorID, str, ET_CHAR);

    snprintf(str, MSG_TX_BUFFER_SIZE, "LoadMonitor=0&AlarmTemp=%d&value", i);
    SendSettingsRecord(bytesWritten, &Settings.LoadMonitor.AlarmTemp[i], str, ET_FLOAT);

    snprintf(str, MSG_TX_BUFFER_SIZE, "LoadMonitor=0&IrSensorsAvailable=%d&value", i);
    SendSettingsRecord(bytesWritten, &Settings.LoadMonitor.IrSensorsAvailable[i], str, ET_CHAR);
  }

  // outside air cooling control page
  SendSettingsRecord(bytesWritten, &Settings.OutsideAir.Diff, "OutAirDiff", ET_FLOAT);
  SendSettingsRecord(bytesWritten, &Settings.OutsideAir.CtrlMode, "OutAirCtrlMode", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.OutsideAir.AboveBelow, "OutAirAboveBelow", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.OutsideAir.TempRef, "OutAirTempRef", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.OutsideAir.CalcHumidMax, "CalcHumMax", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Cure.StartTemp, "CureStartTemp", ET_FLOAT);
  SendSettingsRecord(bytesWritten, &Settings.Cure.StartHumid, "CureStartHumid", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Cure.HumidHighLimit, "CureHumidHighLimit", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Cure.HumidRef, "CureHumidRef", ET_CHAR);

  // plenum temp deviation alarm page
  SendSettingsRecord(bytesWritten, &Settings.Plenum.LowAlarmTemp, "PlenLowAlarmTemp", ET_FLOAT);
  SendSettingsRecord(bytesWritten, &Settings.Plenum.LowAlarmTimer, "PlenLowAlarmTimer", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Plenum.HighAlarmTemp, "PlenHighAlarmTemp", ET_FLOAT);
  SendSettingsRecord(bytesWritten, &Settings.Plenum.HighAlarmTimer, "PlenHighAlarmTimer", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Cure.TempLowLimit, "CureTempLowLimit", ET_FLOAT);
  SendSettingsRecord(bytesWritten, &Settings.Cure.TempHighLimit, "CureTempHighLimit", ET_FLOAT);

  // system runtimes page
  SendSettingsRecord(bytesWritten, &Settings.RunTimes, "RunTimes", ET_RUNTIME);
  SendSettingsRecord(bytesWritten, &Settings.Cure.RunTimes, "CureRunTimes", ET_RUNTIME);
  SendSettingsRecord(bytesWritten, &Settings.Climacell.Times, "ClimacellTimes", ET_RUNTIME);

  // fan speed control page
  SendSettingsRecord(bytesWritten, &Settings.Fan.MaxSpeed, "MaxFanSpeed", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Fan.MinSpeed, "MinFanSpeed", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Fan.RefrigSpeed, "RefrFanSpeed", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Fan.RecircSpeed, "RecircFanSpeed", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Fan.UpdatePeriod, "UpdFanSpeed", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Fan.TempDiff, "TempDiff", ET_FLOAT);
  SendSettingsRecord(bytesWritten, &Settings.Fan.TempRef1, "TempRef1", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Fan.TempRef2, "TempRef2", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Fan.UpdateMode, "FanUpdMode", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Fan.PrevSpeed, "PrevFanSpeed", ET_CHAR);

  // ramp rate page
  SendSettingsRecord(bytesWritten, &Settings.Ramp.UpdateTemp, "RampUpdTemp", ET_FLOAT);
  SendSettingsRecord(bytesWritten, &Settings.Ramp.UpdatePeriod, "RampUpdHours", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Ramp.TempDiff, "RampTempDiff", ET_FLOAT);
  SendSettingsRecord(bytesWritten, &Settings.Ramp.TempRef, "RampTempRef", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Ramp.TargetTemp, "RampTargetTemp", ET_FLOAT);

  // humidity control page
  for (i = 0; i < NUM_HUMIDIFIERS; i++)
  {
    snprintf(str, MSG_TX_BUFFER_SIZE, "HumidCtrl=%d&Mode", i);
    SendSettingsRecord(bytesWritten, &Settings.HumidCtrl[i].Mode, str, ET_CHAR);

    for (j = 0; j < 3; j++) // j = system modes cool, recirc, refrig
    {
      snprintf(str, MSG_TX_BUFFER_SIZE, "DutyCycle=%d&HumidCtrl=%d&On", j, i);
      SendSettingsRecord(bytesWritten, &Settings.HumidCtrl[i].DutyCycle[j].On, str, ET_CHAR);

      snprintf(str, MSG_TX_BUFFER_SIZE, "DutyCycle=%d&HumidCtrl=%d&Off", j, i);
      SendSettingsRecord(bytesWritten, &Settings.HumidCtrl[i].DutyCycle[j].Off, str, ET_CHAR);
    }
  }

  // high CO2 level purge page
  SendSettingsRecord(bytesWritten, &Settings.Co2.Purge.Mode, "Co2PurgeMode", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Co2.Set, "Co2Set", ET_SHORT);
  SendSettingsRecord(bytesWritten, &Settings.Co2.CylceTime, "Co2CylceTime", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Co2.Purge.MaxTemp, "Co2PurgeMaxTemp", ET_FLOAT);
  SendSettingsRecord(bytesWritten, &Settings.Co2.Purge.MinTemp, "Co2PurgeMinTemp", ET_FLOAT);
  SendSettingsRecord(bytesWritten, &Settings.Co2.Purge.Duration, "Co2PurgeDur", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Co2.Purge.Last, "Co2LastPurge", ET_UINT32);
  SendSettingsRecord(bytesWritten, &Settings.Co2.Purge.Start, "Co2PurgeStart", ET_UINT32);
  SendSettingsRecord(bytesWritten, &Settings.Co2.FanOutput, "Co2FanOutput", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Co2.DoorOutput, "Co2DoorOutput", ET_CHAR);

  // miscellaneous parameters page
  SendSettingsRecord(bytesWritten, &Settings.Refrig.Mode, "RefrMode", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Refrig.DefrostPeriod, "DefrostInt", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Refrig.DefrostDuration, "DefrostDur", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.HeatTempThresh, "HeatTempThresh", ET_FLOAT);
  SendSettingsRecord(bytesWritten, &Settings.Refrig.Limit, "RefrigLimit", ET_FLOAT);
  SendSettingsRecord(bytesWritten, &Settings.CavityHeat.Label, "CavityOrPileCtrl", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.CavityHeat.Mode, "CavHeat", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.CavityHeat.Diff, "CavDiff", ET_FLOAT);
  SendSettingsRecord(bytesWritten, &Settings.CavityHeat.DutyCycle, "CavDutyCycle", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.CavityHeat.StandbyOn, "CavStandbyOn", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.KbPref, "KbPref", ET_CHAR);

  // email page
  SendSettingsRecord(bytesWritten, &Settings.Email.Alerts, "EmailAlerts", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Email.To, "EmailTo", ET_STRING);
  SendSettingsRecord(bytesWritten, &Settings.Email.From, "EmailFrom", ET_STRING);
  SendSettingsRecord(bytesWritten, &Settings.Email.Server, "EmailServer", ET_STRING);
  SendSettingsRecord(bytesWritten, &Settings.Email.AuthType, "EmailAuthType", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.Email.Port, "EmailPort", ET_USHORT);
  SendSettingsRecord(bytesWritten, &Settings.Email.Account, "EmailAccount", ET_STRING);
  SendSettingsRecord(bytesWritten, &Settings.Email.Password, "EmailPassword", ET_STRING);
  SendSettingsRecord(bytesWritten, &Settings.Email.Display, "EmailDisplay", ET_STRING);

  // email alerts setup page
  SendSettingsRecord(bytesWritten, &Settings.AlertsToSend, "AlertsToSend", ET_STRING);

  // software/remote off
//  SendSettingsRecord(&Settings.RemoteOff[RO_AUX1], "AuxRemoteOff", ET_CHAR);
//  SendSettingsRecord(&Settings.RemoteOff[RO_CAVITY_HEAT], "CavRemoteOff", ET_CHAR);
//  SendSettingsRecord(&Settings.RemoteOff[RO_CLIMACELL], "ClimacellRemoteOff", ET_CHAR);
//  SendSettingsRecord(&Settings.RemoteOff[RO_FAN], "FanRemoteOff", ET_CHAR);
//  SendSettingsRecord(&Settings.RemoteOff[RO_HEAT], "HeatRemoteOff", ET_CHAR);
//  SendSettingsRecord(&Settings.RemoteOff[RO_HUMIDIFIER1], "Hum1RemoteOff", ET_CHAR);
//  SendSettingsRecord(&Settings.RemoteOff[RO_HUMIDIFIER2], "Hum2RemoteOff", ET_CHAR);
//  SendSettingsRecord(&Settings.RemoteOff[RO_HUMIDIFIER3], "Hum3RemoteOff", ET_CHAR);
//  SendSettingsRecord(&Settings.RemoteOff[RO_REFRIGERATION], "RefrRemoteOff", ET_CHAR);
//  SendSettingsRecord(&Settings.RemoteOff[RO_LIGHTS1], "Lights1RemoteOff", ET_CHAR);
//  SendSettingsRecord(&Settings.RemoteOff[RO_LIGHTS2], "Lights2RemoteOff", ET_CHAR);
//  SendSettingsRecord(&Settings.RemoteOff[RO_AUX2], "Aux2RemoteOff", ET_CHAR);
//  SendSettingsRecord(&Settings.RemoteOff[RO_BURNER], "BurnerRemoteOff", ET_CHAR);
//  SendSettingsRecord(&Settings.RemoteOff[RO_CURE], "CureRemoteOff", ET_CHAR);

  // fan boost page
  SendSettingsRecord(bytesWritten, &Settings.FanBoost.Mode, "FanBoostMode", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.FanBoost.Speed, "FanBoostSpeed", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.FanBoost.Interval, "FanBoostInterval", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.FanBoost.Duration, "FanBoostDur", ET_CHAR);
  SendSettingsRecord(bytesWritten, &Settings.FanBoost.Temp, "FanBoostTemp", ET_FLOAT);

  // equipment IO setup
  for (i = 0; i < SW_START_STOP; i++)
  {
    if (Settings.EquipIo[i].Enabled)
    {
      snprintf(str, MSG_TX_BUFFER_SIZE, "EquipIo=%d&Enabled", i);
      SendSettingsRecord(bytesWritten, &Settings.EquipIo[i].Enabled, str, ET_CHAR);
      snprintf(str, MSG_TX_BUFFER_SIZE, "EquipIo=%d&Renamable", i);
      SendSettingsRecord(bytesWritten, &Settings.EquipIo[i].Renamable, str, ET_CHAR);
      snprintf(str, MSG_TX_BUFFER_SIZE, "EquipIo=%d&Name", i);
      SendSettingsRecord(bytesWritten, &Settings.EquipIo[i].Name, str, ET_STRING);
      snprintf(str, MSG_TX_BUFFER_SIZE, "EquipIo=%d&Mode", i);
      SendSettingsRecord(bytesWritten, &Settings.EquipIo[i].Mode, str, ET_CHAR);
      snprintf(str, MSG_TX_BUFFER_SIZE, "EquipIo=%d&IO", i);
      SendSettingsRecord(bytesWritten, &Settings.EquipIo[i].IO, str, ET_CHAR);

      if (Settings.EquipIo[i].Output != IO_UNDEFINED)
      {
        snprintf(str, MSG_TX_BUFFER_SIZE, "EquipIo=%d&Output", i);
        SendSettingsRecord(bytesWritten, &Settings.EquipIo[i].Output, str, ET_UINT32);
      }

      if (Settings.EquipIo[i].Input != IO_UNDEFINED)
      {
        snprintf(str, MSG_TX_BUFFER_SIZE, "EquipIo=%d&Input", i);
        SendSettingsRecord(bytesWritten, &Settings.EquipIo[i].Input, str, ET_UINT32);
      }
    }
  }

  // auxiliary output program rules setup
  for (i = 0; i < NUM_AUX_OUTPUTS; i++)
  {
    if (Settings.AuxProgram[i].Rule[0].Type != RT_UNDEFINED)
    {
      snprintf(str, MSG_TX_BUFFER_SIZE, "AuxProgram=%d&RuleStatus", i);
      SendSettingsRecord(bytesWritten, &Settings.AuxProgram[i].RuleStatus, str, ET_CHAR);
      snprintf(str, MSG_TX_BUFFER_SIZE, "AuxProgram=%d&OutputState", i);
      SendSettingsRecord(bytesWritten, &Settings.AuxProgram[i].OutputState, str, ET_CHAR);
      snprintf(str, MSG_TX_BUFFER_SIZE, "AuxProgram=%d&DutyCycle", i);
      SendSettingsRecord(bytesWritten, &Settings.AuxProgram[i].DutyCycle, str, ET_CHAR);
      snprintf(str, MSG_TX_BUFFER_SIZE, "AuxProgram=%d&Period", i);
      SendSettingsRecord(bytesWritten, &Settings.AuxProgram[i].Period, str, ET_INT32);
      snprintf(str, MSG_TX_BUFFER_SIZE, "AuxProgram=%d&Units", i);
      SendSettingsRecord(bytesWritten, &Settings.AuxProgram[i].Units, str, ET_CHAR);
      snprintf(str, MSG_TX_BUFFER_SIZE, "AuxProgram=%d&Timer", i);
      SendSettingsRecord(bytesWritten, &Settings.AuxProgram[i].Timer, str, ET_INT32);

      for (j = 0; j < NUM_AUX_PROGRAM_RULES; j++)
      {
        snprintf(str, MSG_TX_BUFFER_SIZE, "ProgramRule=%d&Rule=%d&Type", i, j);
        SendSettingsRecord(bytesWritten, &Settings.AuxProgram[i].Rule[j].Type, str, ET_CHAR);
        snprintf(str, MSG_TX_BUFFER_SIZE, "ProgramRule=%d&Rule=%d&IoIndex", i, j);
        SendSettingsRecord(bytesWritten, &Settings.AuxProgram[i].Rule[j].IoIndex, str, ET_INT32);
        snprintf(str, MSG_TX_BUFFER_SIZE, "ProgramRule=%d&Rule=%d&State", i, j);
        SendSettingsRecord(bytesWritten, &Settings.AuxProgram[i].Rule[j].State, str, ET_CHAR);
        snprintf(str, MSG_TX_BUFFER_SIZE, "ProgramRule=%d&Rule=%d&Op", i, j);
        SendSettingsRecord(bytesWritten, &Settings.AuxProgram[i].Rule[j].Op, str, ET_CHAR);
        snprintf(str, MSG_TX_BUFFER_SIZE, "ProgramRule=%d&Rule=%d&SensorValue", i, j);
        SendSettingsRecord(bytesWritten, &Settings.AuxProgram[i].Rule[j].SensorValue, str, ET_FLOAT);
        snprintf(str, MSG_TX_BUFFER_SIZE, "ProgramRule=%d&Rule=%d&AndOr", i, j);
        SendSettingsRecord(bytesWritten, &Settings.AuxProgram[i].Rule[j].AndOr, str, ET_CHAR);
        snprintf(str, MSG_TX_BUFFER_SIZE, "ProgramRule=%d&Rule=%d&ReferenceIndex", i, j);
        SendSettingsRecord(bytesWritten, &Settings.AuxProgram[i].Rule[j].ReferenceIndex, str, ET_INT32);
      }
    }
  }
} // end SendSettings()

/***************************************************************************

FUNCTION:   Settings_Init()

PURPOSE:    Read the system settings from flash

COMMENTS:   Converts to the new structure if necessary

***************************************************************************/
void Settings_Init(void)
{
  int Retries = 0;
  int Status = 0;

  do
  {
    if (Retries > 0)
      SysCtlDelay(1000);

// Tiva delay ???
//  void delayMS(int ms) {
//      SysCtlDelay( (SysCtlClockGet()/(3*1000))*ms ) ;

    // get the current settings from flash
    Status = ReadSettings((char *)&Settings, sizeof(Settings), ACTIVE_SETTINGS);
  }
  while (Status == 0 && Retries++ < 3);

  // check if conversion is necessary
  if (Status == 0)
  {
    // get the factory defaults which will set any new elements to valid settings
    GetFactoryDefault();

    if (SettingsRestoreFromLocalFlash())
    {
      // Seed A/B dual-bank: write both banks so both are valid
      Settings.SaveSeq = 0;
      SaveSettings(ACTIVE_SETTINGS);  // writes Bank B (opposite of initial 0=A)
      LastWrittenBank = 0;            // reset so next save goes to B again normally
      SaveSettings(ACTIVE_SETTINGS);  // writes Bank A — now both banks valid
      WarningsSet(WARN_SETTINGSCONVERT, FM_ALARM, FM_ALARM, NA);
    }
    else
    {
      // Pure factory default — seed both banks
      Settings.SaveSeq = 0;
      SaveSettings(ACTIVE_SETTINGS);
      LastWrittenBank = 0;
      SaveSettings(ACTIVE_SETTINGS);
      WarningsSet(WARN_FACTORYDEFAULT, FM_ALARM, FM_ALARM, NA);
    }
  }
  else
  {
    // clear warnings from intial failed attempts if settings successfully read
    WarningsSet(WARN_EEPROMACCESS, FM_NONE, FM_NONE, NA);
    WarningsSet(WARN_SAVESETTINGS, FM_NONE, FM_NONE, NA);
  }

  // store the Settings & ARM firmware versions
  strcpy(Settings.SettingsVersion, SETTINGS_VERSION);
  strcpy(Settings.ArmVersion, ARM_FIRMWARE_VERSION);
  strcpy(Settings.FactoryBd, DEFAULT_P2PASSWORD);
  Settings.NetMonitorMode = NETMONITOR_VERSION;

  // clear the sensor values that can be set by the master if this is a slave
  // NOTE: this is done to prevent using old/saved values when the system comes up in
  // slave mode and there is no master broadcast being received
  Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Sensor[SENSOR_OUTSIDE_TEMP].Value = SENSOR_VAL_UNDEFINED;
  Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_OUTSIDE_HUMID].Value = SENSOR_VAL_UNDEFINED;
} // end Settings_Init()

/***************************************************************************

FUNCTION:   SettingsSendToFile()

PURPOSE:    Send the settings to be stored in ASCII format on the Display

COMMENTS:

***************************************************************************/
int SettingsSendToFile(int SessionID)
{
  char str[50];
  uint32_t bytesWritten = 0;

  // set the modular SessionID variable and clear the kill flag
  KillMessage = -1;

  snprintf(str, sizeof(str), "LogTotal=%d,%d", 652, SessionID);
  SendMsgAndWaitForResponse(str, UI_DELAY_LONG);

  // send the data
  SendSettings(&bytesWritten);

  // send the end of file
  snprintf(str, sizeof(str), "LogToFile=EOF,%d", SessionID);
  SendMsgAndWaitForResponse(str, UI_DELAY_LONG);

  return 1;
} // end SettingsSendToFile()

/***************************************************************************

FUNCTION:   SettingsSendToLocalFlash()

PURPOSE:    Send the settings to be stored in ASCII format in external flash

COMMENTS:

***************************************************************************/
int SettingsSendToLocalFlash(void)
{
  char header[FLASHFILE_HDR_LEN] = "";
  uint32_t flashWrite = FLASHFILE_HDR_LEN;

  debug_printf("Erasing External Flash for Settings Save...");
  FlashEraseArea(FLASH_SETTINGS_SAVE_FILE, SETTINGS_FILE_SIZE);
  debug_printf("Done\r\n");
  debug_printf("Saving Settings...");

  // send the data
  SendSettings(&flashWrite);

  snprintf(header, FLASHFILE_HDR_LEN, "ASCII Settings File:%lu", flashWrite);
  FlashWrite(FLASH_SETTINGS_SAVE_FILE, (unsigned char*)header, strlen(header)+1);
  FlashWrite(FLASH_SETTINGS_SAVE_FILE+flashWrite, (unsigned char*)"EOF", strlen("EOF")+1);
  debug_printf("Done\r\nTotal settings bytes written to flash:%d\r\n", flashWrite);

  return 1;
} // end SettingsSendToLocalFlash()

/***************************************************************************

FUNCTION:   UserAcctAuth()

PURPOSE:    Authenticate user account

COMMENTS:

***************************************************************************/
int UserAcctAuth(char *ID, char *Password)
{
  int i = 0;

  // if the user ID is blank then fail
  if (ID[0] == 0)
  {
    return 0;
  }

  while (i < NUM_PASSWORDS)
  {
    if (strcmp(Settings.User[i].ID, ID) == 0)
    {
      if (strcmp(Settings.User[i].Password, Password) == 0)
      {
        return 1;
      }
    }
    i++;
  }

  return 0;
} // end UserAcctAuth()

/***   End Of File   ***/
