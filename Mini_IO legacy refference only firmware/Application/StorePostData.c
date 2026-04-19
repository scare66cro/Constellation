/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     StorePostData.c

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:  Store Lantronix/UI post data

COMMENTS:

***************************************************************************/

/*** include files ***/

//#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>

// Platform
#include "debug.h"

// Gellert
#include "Analog_Input.h"
#include "Controls.h"
#include "DataExc.h"
#include "LoadLogs.h"
#include "Modes.h"
#include "PWM.h"
#include "RTC.h"
#include "Settings.h"
#include "SerialShift.h"
#include "SDCard.h"
#include "States.h"
#include "StorePostData.h"
#include "Timer.h"
#include "UI_Messages.h"

/*** defines ***/

/*** typedefs and structures ***/

/*** module variables ***/

UI_POST_ACTION      UI_ActionPosts[UI_NUM_ACTIONPOSTS];
UI_POST_EQUIPSTATUS UI_EquipPosts[UI_NUM_EQUIPPOSTS];
UI_POST_PROGRAMPAGE UI_ProgramPosts[UI_NUM_PROGRAMPOSTS];

char LtxInitialized = 0;
char LtxHttpSession = 0;
signed char LtxPgmLevel = 0;

/*** static functions ***/

static void DeleteNetworkNode(void);
static void PasswordAuth(void);
static void PasswordGen(char *Magic, int Length, char *Password);

static void SetCurrentFanSpeed(void);
static void StoreClimacell(void);
static void StoreClimacellTimes(void);
static void StoreAirCure(void);
static void StoreCureLimits(void);
static void StoreAnalogBoard(void);
static void StoreAuxProgram(void);
static void StoreBasic(void);
static void StoreBayNames(void);
static void StoreBurner(void);
static void StoreClimacell(void);
static void StoreCO2(void);
static void StoreDateTime(void);
static void StoreDoor(void);
static void StoreEmail(void);
static void StoreFailures(void);
static void StoreFailures2(void);
static void StoreFanBoost(void);
static void StoreFanDailyRun(void);
static void StoreFanSpeeds(void);
static void StoreFanTotalRun(void);
static void StorePublicAddress(void);
static void StoreHttpPort(void);
static void StoreHumidCtrl(void);
static void StoreIoConfig(void);
static void StoreIoName(void);
static void StoreIoTranslate(void);
static void StorePwmName(void);
static void StoreAnalogBoardLabel(void);
static void StoreTempSensorLabel(void);
static void StoreHumidSensorLabel(void);
static void StoreBayLabel(void);
static void StoreLoadMonitor(void);
static void StoreLoadPause(void);
static void StoreLoadPipe(void);
static void StoreLoadStop(void);
static void StoreLtxInit(void);
static void StoreMasterSlave(void);
static void StoreMisc(void);
static void StoreNetworkNode(void);
static void StoreNetworkNodeUpdate(void);
static void StoreNodeDiscovery(void);
static void StoreOutsideAir(void);
static void StorePassword(void);
static void StorePIDLog(uint8_t *PIDLogging);
static void StorePIDSettings(void);
static void StorePlenumSetup(void);
static void StoreRampRate(void);
static void StoreRefrig(void);
static void StoreRefrigDiag(uint8_t *Diagnostic);
static void StoreRemoteOff(uint8_t *RemoteOff);
static void ToggleRemoteOff(uint8_t *RemoteOff);
static void StoreSystemOff(uint8_t *RemoteOff);
static void StoreRuntimes(void);
static void StoreServiceInfo(void);
static void StoreTempAlarms(void);
static void StoreUserLog(void);

/***************************************************************************

FUNCTION:   ConvertSpecialChars()

PURPOSE:    Convert the escape sequences sent from UI to actual characters

COMMENTS:   Tested with each trapped character and that others get replaced
            with a space

***************************************************************************/
void ConvertSpecialChars(char *str, int length)
{
  int read = 0;
  int write = 0;
  char specialChar[5];

  while (str[read] != '\0' && read < length)
  {
    if (str[read] == '%')
    {
      read++;
      specialChar[0] = str[read++];
      specialChar[1] = str[read++];
      specialChar[2] = '\0';

      if (strcmp(specialChar, "23") == 0)
        str[write++] = '#';
      else if (strcmp(specialChar, "24") == 0)
        str[write++] = '$';
      else if (strcmp(specialChar, "25") == 0)
        str[write++] = '%';
      else if (strcmp(specialChar, "27") == 0)
        str[write++] = '\'';
      else if (strcmp(specialChar, "28") == 0)
        str[write++] = '(';
      else if (strcmp(specialChar, "29") == 0)
        str[write++] = ')';
      else if (strcmp(specialChar, "2C") == 0)
        str[write++] = ',';
      else if (strcmp(specialChar, "2F") == 0)
        str[write++] = '/';
      else if (strcmp(specialChar, "3A") == 0)
        str[write++] = ':';
//      else if (strcmp(SpecialChar, "3B") == 0)  // can't allow ';' - endline in JavaScript
//        str[Write++] = ';';
      else if (strcmp(specialChar, "3D") == 0)
        str[write++] = '=';
      else if (strcmp(specialChar, "3F") == 0)
        str[write++] = '?';
      else if (strcmp(specialChar, "40") == 0)
        str[write++] = '@';
      else
        str[write++] = ' ';
    }
    else if (str[read] == '+')
    {
      str[write++] = ' ';
      read++;
    }
    else
    {
      str[write++] = str[read++];
    }
  }

  str[write] = '\0';
} // end ConvertSpecialChars()

/***************************************************************************

FUNCTION:   DeleteNetworkNode()

PURPOSE:    Delete a network (remote) node from the node list

COMMENTS:

***************************************************************************/
void DeleteNetworkNode(void)
{
  NetworkNodeDelete(PostValue[0]);
} // end DeleteNetworkNode()

/***************************************************************************

FUNCTION:   PasswordAuth()

PURPOSE:    Authenticate the passwords

COMMENTS:

***************************************************************************/
void PasswordAuth(void)
{
  char Password[25];

  LtxPgmLevel = -2;
  LtxHttpSession = atoi(PostValue[4]);

  if (strcmp(PostValue[1], "clear") == 0)
  {
    LtxPgmLevel = 0;
  }
  else if (strcmp(PostValue[1], "leveldown") == 0)
  {
    LtxPgmLevel = 1;
  }
  else if (strcmp(PostValue[1], Settings.FactoryBd) == 0)
  {
    LtxPgmLevel = 2;
  }
  else if (strcmp(PostValue[0], "login") == 0)
  {
    if (strcmp(PostValue[1], Settings.LoginPw) == 0)
      LtxPgmLevel = 0;
  }
  else if (UserAcctAuth(PostValue[0], PostValue[1]))
  {
    LtxPgmLevel = 1;
  }
  else
  {
    PasswordGen(PostValue[2], atoi(PostValue[3]), Password);

    if (strcmp(PostValue[1], Password) == 0)
      LtxPgmLevel = 2;
  }

//  DBGU_SendString("PgmLevel = ");
//  DBGU_SendInt(LtxPgmLevel);
} // end PasswordAuth()

/***************************************************************************

FUNCTION:   PasswordGen()

PURPOSE:    Generate the monthly password

COMMENTS:

***************************************************************************/
void PasswordGen(char *Magic, int Length, char *Password)
{
  int  i;
  char Temp[5];
  char Lookup[65] = {"0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"};

  for (i = 0; i+3 < 16 && i < Length; i++)
  {
    // pick 2-digit numbers from magic number to build password
    Temp[0] = Magic[i];
    Temp[1] = Magic[i+1];
    Temp[2] = 0;
    Password[i] = Lookup[(atoi(Temp) % 62)];
  }
  Password[i] = 0;
} // end PasswordGen()

/***************************************************************************

FUNCTION:   SetCurrentFanSpeed()

PURPOSE:    Set the current fan speed

COMMENTS:

***************************************************************************/
void SetCurrentFanSpeed(void)
{
  Settings.Fan.PrevSpeed = atoi(PostValue[0]);

  if (   SystemState == ST_COOLING
      || SystemState == ST_COOLDEHUMID
      || SystemState == ST_HEATING
      || SystemState == ST_FAN_MANUAL)
  {
    CtrlFan(Settings.Fan.PrevSpeed);
    IntervalTimer[IT_FANSPEEDUPD] = XTimerVal;
  }
} // end SetCurrentFanSpeed()

/***************************************************************************

FUNCTION:   StoreAirCure()

PURPOSE:    Store outside air settings

COMMENTS:

***************************************************************************/
void StoreAirCure(void)
{
  int PostIndex = 0;

  Settings.Cure.StartTemp = atof(PostValue[PostIndex++]);
  Settings.Cure.HumidRef = atof(PostValue[PostIndex++]);
  Settings.Cure.StartHumid = atoi(PostValue[PostIndex++]);
  Settings.Cure.HumidHighLimit = atoi(PostValue[PostIndex++]);
} // end StoreAirCure()

/***************************************************************************

FUNCTION:   StoreCureLimits()

PURPOSE:    Store air cure temperature limit settings

COMMENTS:

***************************************************************************/
void StoreCureLimits(void)
{
  int PostIndex = 0;

  Settings.Cure.TempLowLimit = atof(PostValue[PostIndex++]);
  Settings.Plenum.LowAlarmTimer = atoi(PostValue[PostIndex++]);
  Settings.Cure.TempHighLimit = atof(PostValue[PostIndex++]);
  Settings.Plenum.HighAlarmTimer = atoi(PostValue[PostIndex++]);
} // end StoreCureLimits()

/***************************************************************************

FUNCTION:   StoreAlerts()

PURPOSE:    Store the alerts that should be sent out via email

COMMENTS:

***************************************************************************/
void StoreAlerts(void)
{
  StorePostValue("AlertSetup", &Settings.AlertsToSend, ET_STRING, NUM_WARNINGS+1);
} // end StoreAlerts()

/***************************************************************************

FUNCTION:   StoreAnalogBoard()

PURPOSE:    Store the analog board setup data

COMMENTS:

***************************************************************************/
void StoreAnalogBoard(void)
{
  uint8_t boardIndex;
  char postValue[MSG_MAX_VALUE_LEN];

  if (   StorePostValue("BAdd", &boardIndex, ET_UINT8, NULL) == SPV_SUCCESS
      && --boardIndex < ANALOG_BOARDS_PER_SYSTEM)   // board index = address - 1
  {
    GetPostValueByField("BdLbl", postValue, MSG_MAX_VALUE_LEN);
    ConvertSpecialChars(postValue, MSG_MAX_VALUE_LEN);
    StringCopy(Settings.AnalogBoard[boardIndex].Label, postValue, BOARD_LABELS+1);
    StorePostValue("BDis", &Settings.AnalogBoard[boardIndex].Disabled, ET_UINT8, NULL);

    GetPostValueByField("Sen1Lbl", postValue, MSG_MAX_VALUE_LEN);
    ConvertSpecialChars(postValue, MSG_MAX_VALUE_LEN);
    StringCopy(Settings.AnalogBoard[boardIndex].Sensor[0].Label, postValue, BOARD_LABELS+1);
    StorePostValue("Sen1Off", &Settings.AnalogBoard[boardIndex].Sensor[0].Offset, ET_FLOAT, NULL);
    StorePostValue("Sen1Dis", &Settings.AnalogBoard[boardIndex].Sensor[0].Disabled, ET_UINT8, NULL);

    GetPostValueByField("Sen2Lbl", postValue, MSG_MAX_VALUE_LEN);
    ConvertSpecialChars(postValue, MSG_MAX_VALUE_LEN);
    StringCopy(Settings.AnalogBoard[boardIndex].Sensor[1].Label, postValue, BOARD_LABELS+1);
    StorePostValue("Sen2Off", &Settings.AnalogBoard[boardIndex].Sensor[1].Offset, ET_FLOAT, NULL);
    StorePostValue("Sen2Dis", &Settings.AnalogBoard[boardIndex].Sensor[1].Disabled, ET_UINT8, NULL);

    GetPostValueByField("Sen3Lbl", postValue, MSG_MAX_VALUE_LEN);
    ConvertSpecialChars(postValue, MSG_MAX_VALUE_LEN);
    StringCopy(Settings.AnalogBoard[boardIndex].Sensor[2].Label, postValue, BOARD_LABELS+1);
    StorePostValue("Sen3Off", &Settings.AnalogBoard[boardIndex].Sensor[2].Offset, ET_FLOAT, NULL);
    StorePostValue("Sen3Dis", &Settings.AnalogBoard[boardIndex].Sensor[2].Disabled, ET_UINT8, NULL);

    GetPostValueByField("Sen4Lbl", postValue, MSG_MAX_VALUE_LEN);
    ConvertSpecialChars(postValue, MSG_MAX_VALUE_LEN);
    StringCopy(Settings.AnalogBoard[boardIndex].Sensor[3].Label, postValue, BOARD_LABELS+1);
    StorePostValue("Sen4Off", &Settings.AnalogBoard[boardIndex].Sensor[3].Offset, ET_FLOAT, NULL);
    StorePostValue("Sen4Dis", &Settings.AnalogBoard[boardIndex].Sensor[3].Disabled, ET_UINT8, NULL);

    if (Settings.AnalogBoard[boardIndex].Disabled == 1)
    {
      Settings.AnalogBoard[boardIndex].Sensor[0].Disabled = 1;
      Settings.AnalogBoard[boardIndex].Sensor[1].Disabled = 1;
      Settings.AnalogBoard[boardIndex].Sensor[2].Disabled = 1;
      Settings.AnalogBoard[boardIndex].Sensor[3].Disabled = 1;
    }
  }
} // end StoreAnalogBoard()

/***************************************************************************

FUNCTION:   StoreAuxProgram()

PURPOSE:    Store the parameters for the auxiliary output program

COMMENTS:

***************************************************************************/
void StoreAuxProgram(void)
{
  int i;
  int programIndex;
  char field[25];
  char value[25];

  if (GetPostValueByField(UI_ProgramPosts[TAG_P2_AUXPROG].Tag, value, sizeof(value)))
  {
    programIndex = atoi(value);

    // if it's a valid auxiliary output index
    if (programIndex >= EQ_AUX1 && programIndex <= EQ_AUX8)
    {
      programIndex -= EQ_AUX1;

      // store the rules
      for (i = 0; i < NUM_AUX_PROGRAM_RULES;  ++i)
      {
        if (i > 0)
        {
          snprintf(field, sizeof(field), "andOr%d", i+1);
          if (GetPostValueByField(field, value, sizeof(value)))
          {
            Settings.AuxProgram[programIndex].Rule[i].AndOr = (AUXPROG_AND_OR) atoi(value);
          }
        }

        snprintf(field, sizeof(field), "type%d", i + 1);
        if (GetPostValueByField(field, value, sizeof(value)))
        {
          Settings.AuxProgram[programIndex].Rule[i].Type = (AUXPROG_RULE_TYPE) atoi(value);
        }

        snprintf(field, sizeof(field), "io%d", i + 1);
        if (GetPostValueByField(field, value, sizeof(value)))
        {
          Settings.AuxProgram[programIndex].Rule[i].IoIndex = atoi(value);
        }

        snprintf(field, sizeof(field), "st%d", i + 1);
        if (GetPostValueByField(field, value, sizeof(value)))
        {
          Settings.AuxProgram[programIndex].Rule[i].State = atoi(value);
        }

        snprintf(field, sizeof(field), "op%d", i + 1);
        if (GetPostValueByField(field, value, sizeof(value)))
        {
          Settings.AuxProgram[programIndex].Rule[i].Op = (AUXPROG_OPERATOR) atoi(value);
        }

        snprintf(field, sizeof(field), "ref%d", i + 1);
        if (GetPostValueByField(field, value, sizeof(value)))
        {
          Settings.AuxProgram[programIndex].Rule[i].ReferenceIndex = atoi(value);
        }

        if (Settings.AuxProgram[programIndex].Rule[i].ReferenceIndex == RT_UNDEFINED)
        {
          snprintf(field, sizeof(field), "sen%d", i+1);
          if (GetPostValueByField(field, value, sizeof(value)))
          {
            Settings.AuxProgram[programIndex].Rule[i].SensorValue = atoi(value);
          }
        }
        else
        {
          snprintf(field, sizeof(field), "diff%d", i+1);
          if (GetPostValueByField(field, value, sizeof(value)))
          {
            ConvertSpecialChars(value, 25);   // remove the '+' sign
            Settings.AuxProgram[programIndex].Rule[i].SensorValue = atoi(value);
          }
        }
      }
    }

    StorePostValue("dutyCycle", &Settings.AuxProgram[programIndex].DutyCycle, ET_UINT8, NULL);
    StorePostValue("period", &Settings.AuxProgram[programIndex].Period, ET_UINT32, NULL);
    StorePostValue("units", &Settings.AuxProgram[programIndex].Units, ET_UINT8, NULL);

    // reset the duty cycle
    Settings.AuxProgram[programIndex].RuleStatus = 0;
    Settings.AuxProgram[programIndex].OutputState = 0;
    Settings.AuxProgram[programIndex].Timer = 0;
  }
} // end StoreAuxProgram()

/***************************************************************************

FUNCTION:   StoreBasic()

PURPOSE:    Store the P2 basic setup data

COMMENTS:

***************************************************************************/
void StoreBasic(void)
{
  uint8_t type;
  char postValue[MSG_MAX_VALUE_LEN];

  GetPostValueByField("StorageName", postValue, MSG_MAX_VALUE_LEN);
  ConvertSpecialChars(postValue, MSG_MAX_VALUE_LEN);
  StringCopy(Settings.StorageName, postValue, SITE_ID_LEN+1);

  StorePostValue("HomePage", &Settings.HomePage, ET_STRING, HOMEPAGE_LEN);

  GetPostValueByField("dlr0", postValue, MSG_MAX_VALUE_LEN);
  ConvertSpecialChars(postValue, MSG_MAX_VALUE_LEN);
  StringCopy(Settings.LoginPw, postValue, PASSWORD_LEN+1);

  StorePostValue("loginSecure", &Settings.LocalLogin, ET_UINT8, NULL);

  // if changing between F and C
  StorePostValue("TempType", &type, ET_UINT8, NULL);
  if (Settings.TempType != type)
  {
    if (Settings.TempType == 0)    // Fahrenheit
    {
      Settings.Plenum.TempSet = (Settings.Plenum.TempSet - 32.0)/1.8;
      PlenumTempAvg = (PlenumTempAvg - 32.0)/1.8;
      StartTemp = (StartTemp - 32.0)/1.8;
      *OutsideTemp = (*OutsideTemp - 32.0)/1.8;
    }
    else
    {
      Settings.Plenum.TempSet = ((Settings.Plenum.TempSet*1.8) + 32.0);
      PlenumTempAvg = ((PlenumTempAvg*1.8) + 32.0);
      StartTemp = ((StartTemp*1.8) + 32.0);
      *OutsideTemp = ((*OutsideTemp*1.8) + 32.0);
    }

    Settings.TempType = type;
    Settings.Ramp.TargetTemp = Settings.Plenum.TempSet;  // so we don't inadvertently start ramping
    ReadAnalogBoards(RT_ALL);
  }

  // if changing between potato and onion mode
  StorePostValue("SystemMode", &type, ET_UINT8, NULL);
  if (Settings.SystemMode != type)
  {
    Settings.SystemMode = type;
    UI_SendRuntimes(MSG_QUEUE);
    SystemState = ST_UNDEFINED;

    ClearAlarms(1);
    WarningsClear();
    UI_SendWarnings(1);

    ClearIntervalTimers();
    CtrlRefrigDiagClear();

    EquipIoInit();
    UI_SendIoConfig(MSG_SEND);
    EquipPwmInit();
    UI_SendPWMChannels(MSG_SEND);
//    SystemLogLabel_Init();
  }

  StorePostValue("MultiView", &Settings.MultiviewSessions, ET_UINT8, NULL);
  StorePostValue("Animations", &Settings.Animations, ET_UINT8, NULL);
} // end StoreBasic()

/***************************************************************************

FUNCTION:   StoreBayNames()

PURPOSE:    Store the bay names from the bay light control page

COMMENTS:   StoreLoadMonitor() also stores bay names

***************************************************************************/
void StoreBayNames(void)
{
  ConvertSpecialChars(PostValue[0], MSG_MAX_VALUE_LEN);
  StringCopy(Settings.LoadMonitor.Bay[0].Label, PostValue[0], BOARD_LABELS+1);

  ConvertSpecialChars(PostValue[1], MSG_MAX_VALUE_LEN);
  StringCopy(Settings.LoadMonitor.Bay[1].Label, PostValue[1], BOARD_LABELS+1);
} // end StoreBayNames()

/***************************************************************************

FUNCTION:   StoreBurner()

PURPOSE:    Store the burner settings

COMMENTS:

***************************************************************************/
void StoreBurner(void)
{
  int PostIndex = 0;

  Settings.Burner.Mode = atoi(PostValue[PostIndex++]);

  if (Settings.Burner.Mode == BURNER_MANUAL)
  {
    Settings.Burner.Manual = atoi(PostValue[PostIndex++]);
  }
  else if (   Settings.Burner.Mode == BURNER_ECONOMY
           || Settings.Burner.Mode == BURNER_MAX)
  {
    Settings.Burner.On = atoi(PostValue[PostIndex++]);
    Settings.Burner.Low = atoi(PostValue[PostIndex++]);

    Settings.Burner.PID.P = atoi(PostValue[PostIndex++]);
    PIDCtrl[PID_BURNER].Kp = Settings.Burner.PID.P;

    Settings.Burner.PID.I = atoi(PostValue[PostIndex++]);
    PIDCtrl[PID_BURNER].Ki = Settings.Burner.PID.I;

    Settings.Burner.PID.D = atoi(PostValue[PostIndex++]);
    PIDCtrl[PID_BURNER].Kd = Settings.Burner.PID.D;

    Settings.Burner.PID.U = atoi(PostValue[PostIndex++]);
  }
  Settings.Climacell.Altitude = atoi(PostValue[PostIndex++]);
  Settings.Climacell.AltitudeUnits = atoi(PostValue[PostIndex++]);
} // end StoreBurner()

/***************************************************************************

FUNCTION:   StoreClimacell()

PURPOSE:    Store the climacell setup data

COMMENTS:

***************************************************************************/
void StoreClimacell(void)
{
  Settings.Climacell.Efficiency = atoi(PostValue[0]);
  Settings.Climacell.Altitude = atoi(PostValue[1]);
  Settings.Climacell.AltitudeUnits = atoi(PostValue[2]);

  Settings.Climacell.PID.P = atoi(PostValue[3]);
  PIDCtrl[PID_CLIMACELL].Kp = Settings.Climacell.PID.P;
  PIDCtrl[PID_HUMIDIFIER].Kp = Settings.Climacell.PID.P;        // same PID values used for climacell & humidifiers

  Settings.Climacell.PID.I = atoi(PostValue[4]);
  PIDCtrl[PID_CLIMACELL].Ki = Settings.Climacell.PID.I;
  PIDCtrl[PID_HUMIDIFIER].Ki = Settings.Climacell.PID.I;

  Settings.Climacell.PID.D = atoi(PostValue[5]);
  PIDCtrl[PID_CLIMACELL].Kd = Settings.Climacell.PID.D;
  PIDCtrl[PID_HUMIDIFIER].Kd = Settings.Climacell.PID.D;

  Settings.Climacell.PID.U = atoi(PostValue[6]);
} // end StoreClimacell()

/***************************************************************************

FUNCTION:   StoreClimacellTimes()

PURPOSE:    Store the climacell clock settings

COMMENTS:

***************************************************************************/
void StoreClimacellTimes(void)
{
  int SetIndex = 0;
  int StrIndex = 0;
  int RtIndex = 0;
  char RtValue[5];

  while (SetIndex < 48)
  {
    if ((PostValue[0][StrIndex] == '+') || (PostValue[0][StrIndex] == '\0'))
    {
      RtValue[RtIndex] = '\0';
      Settings.Climacell.Times[SetIndex++] = atoi(RtValue);
      RtIndex = 0;
      if (PostValue[0][StrIndex] == '\0')   // safety
        break;
      StrIndex++;
    }
    else
      RtValue[RtIndex++] = PostValue[0][StrIndex++];
  }
} // end StoreClimacellTimes()

/***************************************************************************

FUNCTION:   StoreCO2()

PURPOSE:    Store the CO2 purge setup data

COMMENTS:

***************************************************************************/
void StoreCO2(void)
{
  // if changing from auto, reset the timer
  if (Settings.Co2.Purge.Mode == 1 && atoi(PostValue[0]) != 1)   // manual
    Settings.Co2.Purge.Last = XTimerVal;

  Settings.Co2.Purge.Mode = atoi(PostValue[0]);

  if (Settings.Co2.Purge.Mode != 0)    // none
  {
    if (Settings.Co2.Purge.Mode == 1)    // manual
      Settings.Co2.CylceTime = atoi(PostValue[1]);
    else                                // auto
      Settings.Co2.Set = atoi(PostValue[1]);

    Settings.Co2.Purge.MinTemp = atof(PostValue[2]);
    Settings.Co2.Purge.MaxTemp = atof(PostValue[3]);
    Settings.Co2.Purge.Duration = atoi(PostValue[4]);
    Settings.Co2.FanOutput = atoi(PostValue[5]);
    Settings.Co2.DoorOutput = atoi(PostValue[6]);
  }
} // end StoreCO2()

/***************************************************************************

FUNCTION:   StoreDateTime()

PURPOSE:    Store the data & time

COMMENTS:

***************************************************************************/
void StoreDateTime(void)
{
  ConvertSpecialChars(PostValue[0], MSG_MAX_VALUE_LEN);
  ConvertSpecialChars(PostValue[1], MSG_MAX_VALUE_LEN);
  if (SetDateTimeStr(PostValue[0], PostValue[1], atoi(PostValue[2])) == 0)
    WarningsSet(WARN_INVALIDDATETIME, FM_ALARM, FM_ALARM, NA);
} // end StoreDateTime()

/***************************************************************************

FUNCTION:   StoreDoor()

PURPOSE:    Store the door setup data

COMMENTS:

***************************************************************************/
void StoreDoor(void)
{
  Settings.Door.PID.P = atoi(PostValue[0]);
  PIDCtrl[PID_DOOR].Kp = Settings.Door.PID.P;

  Settings.Door.PID.I = atoi(PostValue[1]);
  PIDCtrl[PID_DOOR].Ki = Settings.Door.PID.I;

  Settings.Door.PID.D = atoi(PostValue[2]);
  PIDCtrl[PID_DOOR].Kd = Settings.Door.PID.D;

  Settings.Door.PID.U = atoi(PostValue[3]);
  Settings.Door.ActuatorTime = atoi(PostValue[4]);
  Settings.Door.CoolAirCycle = atoi(PostValue[5]);
} // end StoreDoor()

/***************************************************************************

FUNCTION:   StoreEmail()

PURPOSE:    Store the email alert settings

COMMENTS:

***************************************************************************/
void StoreEmail(void)
{
  StorePostValue("selEmailAlert", &Settings.Email.Alerts, ET_UINT8, NULL);

  if (Settings.Email.Alerts == 0)  // enabled
  {
    ConvertSpecialChars(PostValue[1], MSG_MAX_VALUE_LEN);
    ConvertSpecialChars(PostValue[2], MSG_MAX_VALUE_LEN);
    ConvertSpecialChars(PostValue[3], MSG_MAX_VALUE_LEN);
    ConvertSpecialChars(PostValue[6], MSG_MAX_VALUE_LEN);
    ConvertSpecialChars(PostValue[7], MSG_MAX_VALUE_LEN);
    ConvertSpecialChars(PostValue[8], MSG_MAX_VALUE_LEN);

    StringCopy(Settings.Email.To, PostValue[1], EMAIL_TO_LEN+1);
    StringCopy(Settings.Email.From, PostValue[2], EMAIL_FROM_LEN+1);
    StringCopy(Settings.Email.Server, PostValue[3], EMAIL_FROM_LEN+1);

    StorePostValue("selEmailAuthType", &Settings.Email.AuthType, ET_UINT8, NULL);
    StorePostValue("emailPort", &Settings.Email.Port, ET_UINT16, NULL);

    StringCopy(Settings.Email.Account, PostValue[6], EMAIL_FROM_LEN+1);
    StringCopy(Settings.Email.Password, PostValue[7], EMAIL_FROM_LEN+1);
    StringCopy(Settings.Email.Display, PostValue[8], IP_ADD_LEN+1+SITE_ID_LEN+1);
  }
} // end StoreEmail()

/***************************************************************************

FUNCTION:   StoreFailures()

PURPOSE:    Store the failures(1) set up parameters

COMMENTS:

***************************************************************************/
void StoreFailures(void)
{
  StorePostValue("FanMode", &Settings.Failure[FAIL_FAN].Mode, ET_UINT8, NULL);
  StorePostValue("FanTimer", &Settings.Failure[FAIL_FAN].Timer, ET_UINT8, NULL);

  StorePostValue("ClimacellMode", &Settings.Failure[FAIL_CLIMACELL].Mode, ET_UINT8, NULL);
  StorePostValue("ClimacellTimer", &Settings.Failure[FAIL_CLIMACELL].Timer, ET_UINT8, NULL);

  StorePostValue("RefridgeMode", &Settings.Failure[FAIL_REFRIGERATION].Mode, ET_UINT8, NULL);
  StorePostValue("RefridgeTimer", &Settings.Failure[FAIL_REFRIGERATION].Timer, ET_UINT8, NULL);
  StorePostValue("RefridgeRun", &Settings.Refrig.FailMode, ET_UINT8, NULL);

  StorePostValue("RefrStagesMode", &Settings.Failure[FAIL_REFRIG_STAGES].Mode, ET_UINT8, NULL);
  StorePostValue("RefrStagesTimer", &Settings.Failure[FAIL_REFRIG_STAGES].Timer, ET_UINT8, NULL);

  StorePostValue("HumidifiersMode", &Settings.Failure[FAIL_HUMIDIFIERS].Mode, ET_UINT8, NULL);
  StorePostValue("HumidifiersTimer", &Settings.Failure[FAIL_HUMIDIFIERS].Timer, ET_UINT8, NULL);

  StorePostValue("AuxMode", &Settings.Failure[FAIL_AUXILIARY].Mode, ET_UINT8, NULL);
  StorePostValue("AuxTimer", &Settings.Failure[FAIL_AUXILIARY].Timer, ET_UINT8, NULL);

  StorePostValue("HeatMode", &Settings.Failure[FAIL_HEAT].Mode, ET_UINT8, NULL);
  StorePostValue("HeatTimer", &Settings.Failure[FAIL_HEAT].Timer, ET_UINT8, NULL);

  StorePostValue("CavityHeatMode", &Settings.Failure[FAIL_CAVITY_HEAT].Mode, ET_UINT8, NULL);
  StorePostValue("CavityHeatTimer", &Settings.Failure[FAIL_CAVITY_HEAT].Timer, ET_UINT8, NULL);

  StorePostValue("LightsMode", &Settings.Failure[FAIL_LIGHTS].Mode, ET_UINT8, NULL);
  StorePostValue("LightsTimer", &Settings.Failure[FAIL_LIGHTS].Timer, ET_UINT8, NULL);
  StorePostValue("LightsUnits", &Settings.LightsFailUnits, ET_UINT8, NULL);

  StorePostValue("BurnerMode", &Settings.Failure[FAIL_BURNER].Mode, ET_UINT8, NULL);
  StorePostValue("BurnerTimer", &Settings.Failure[FAIL_BURNER].Timer, ET_UINT8, NULL);
} // end StoreFailures()

/***************************************************************************

FUNCTION:   StoreFailures2()

PURPOSE:    Store the failures(2) set up parameters

COMMENTS:

***************************************************************************/
void StoreFailures2(void)
{
  StorePostValue("OutAirMode", &Settings.Failure[FAIL_OUTSIDE_AIR].Mode, ET_UINT8, NULL);
  StorePostValue("OutAirTimer", &Settings.Failure[FAIL_OUTSIDE_AIR].Timer, ET_UINT8, NULL);

  StorePostValue("OutHumidMode", &Settings.Failure[FAIL_OUTSIDE_HUMIDITY].Mode, ET_UINT8, NULL);
  StorePostValue("OutHumidTimer", &Settings.Failure[FAIL_OUTSIDE_HUMIDITY].Timer, ET_UINT8, NULL);

  StorePostValue("HighCo2Mode", &Settings.Failure[FAIL_HIGH_CO2].Mode, ET_UINT8, NULL);
  StorePostValue("HighCo2Timer", &Settings.Failure[FAIL_HIGH_CO2].Timer, ET_UINT8, NULL);
  StorePostValue("Co2Setpt", &Settings.Co2.HighFailure, ET_UINT16, NULL);

  StorePostValue("LowHumidMode", &Settings.Failure[FAIL_PLENUM_HUMIDITY].Mode, ET_UINT8, NULL);
  StorePostValue("LowHumidTimer", &Settings.Failure[FAIL_PLENUM_HUMIDITY].Timer, ET_UINT8, NULL);
  StorePostValue("LowHumidSet", &Settings.Plenum.HumidLowFailure, ET_UINT8, NULL);

  StorePostValue("PlenSenMode", &Settings.Failure[FAIL_PLENUM_SENSOR].Mode, ET_UINT8, NULL);
  StorePostValue("PlenSenTimer", &Settings.Failure[FAIL_PLENUM_SENSOR].Timer, ET_UINT8, NULL);
  StorePostValue("PlenSenDiff", &Settings.Plenum.SensorDiff, ET_FLOAT, NULL);
} // end StoreFailures2()

/***************************************************************************

FUNCTION:   StoreFanBoost()

PURPOSE:    Store the fan boost setup data

COMMENTS:

***************************************************************************/
void StoreFanBoost(void)
{
  // if changing modes, reset the interval timer
  if (Settings.FanBoost.Mode != atoi(PostValue[0]))
    IntervalTimer[IT_FANBOOSTINTERVAL] = XTimerVal;

  Settings.FanBoost.Mode = atoi(PostValue[0]);

  if (Settings.FanBoost.Mode != 0)    // none
  {
    Settings.FanBoost.Speed = atof(PostValue[1]);

    // safety range checking
    if (Settings.FanBoost.Speed > Settings.Fan.MaxSpeed)
      Settings.FanBoost.Speed = Settings.Fan.MaxSpeed;
    if (Settings.FanBoost.Speed < Settings.Fan.MinSpeed)
      Settings.FanBoost.Speed = Settings.Fan.MinSpeed;

    if (Settings.FanBoost.Mode == 1)    // temp based
    {
      Settings.FanBoost.Temp = atof(PostValue[2]);
      Settings.FanBoost.Interval = atoi(PostValue[3]);
      Settings.FanBoost.Duration = atoi(PostValue[4]);
    }
    else                                // runtime based
    {
      Settings.FanBoost.Interval = atoi(PostValue[2]);
      Settings.FanBoost.Duration = atoi(PostValue[3]);
    }
  }
} // end StoreFanBoost()

/***************************************************************************

FUNCTION:   StoreFanDailyRun()

PURPOSE:    Reset the daily fan run time

COMMENTS:

***************************************************************************/
void StoreFanDailyRun(void)
{
  Settings.Fan.DailyRunTime = 0;
  IntervalTimer[IT_FANDAILYCOUNTER] = XTimerVal;
} // end StoreFanDailyRun()

/***************************************************************************

FUNCTION:   StoreFanSpeeds()

PURPOSE:    Store the fan speed setup data

COMMENTS:

***************************************************************************/
void StoreFanSpeeds(void)
{
//  SetCurrentFanSpeed();

  Settings.Fan.MaxSpeed = atoi(PostValue[0]);
  Settings.Fan.MinSpeed = atoi(PostValue[1]);
  Settings.Fan.RefrigSpeed = atoi(PostValue[2]);
  Settings.Fan.RecircSpeed = atoi(PostValue[3]);
  Settings.Fan.UpdatePeriod = atoi(PostValue[4]);

  if (Settings.SystemMode == SM_POTATO)
  {
    Settings.Fan.TempDiff = atof(PostValue[5]);
    Settings.Fan.TempRef1 = atoi(PostValue[6]);
    Settings.Fan.TempRef2 = atoi(PostValue[7]);
  }
  else
  {
    Settings.Fan.UpdateMode = atof(PostValue[5]);
    if (Settings.Fan.UpdateMode == 0)
    {
      Settings.Fan.TempDiff = atof(PostValue[6]);
      Settings.Fan.TempRef1 = atoi(PostValue[7]);
      Settings.Fan.TempRef2 = atoi(PostValue[8]);
    }
  }

  // safety range checking
  if (Settings.Fan.MinSpeed > Settings.Fan.MaxSpeed)
    Settings.Fan.MinSpeed = Settings.Fan.MaxSpeed;

//  min and max values only apply to cooling mode 2/20/14
//  if (Settings.RefrFanSpeed > Settings.MaxFanSpeed)
//    Settings.RefrFanSpeed = Settings.MaxFanSpeed;
//  if (Settings.RefrFanSpeed < Settings.MinFanSpeed)
//    Settings.RefrFanSpeed = Settings.MinFanSpeed;
//
//  if (Settings.RecircFanSpeed > Settings.MaxFanSpeed)
//    Settings.RecircFanSpeed = Settings.MaxFanSpeed;
//  if (Settings.RecircFanSpeed < Settings.MinFanSpeed)
//    Settings.RecircFanSpeed = Settings.MinFanSpeed;

  if (Settings.FanBoost.Speed > Settings.Fan.MaxSpeed)
    Settings.FanBoost.Speed = Settings.Fan.MaxSpeed;
  if (Settings.FanBoost.Speed < Settings.Fan.MinSpeed)
    Settings.FanBoost.Speed = Settings.Fan.MinSpeed;
} // end StoreFanSpeeds()

/***************************************************************************

FUNCTION:   StoreFanTotalRun()

PURPOSE:    Reset the total fan run time

COMMENTS:

***************************************************************************/
void StoreFanTotalRun(void)
{
  Settings.Fan.TotalRunTime = 0;
  IntervalTimer[IT_FANTOTALCOUNTER] = XTimerVal;
} // end StoreFanTotalRun()

/***************************************************************************

FUNCTION:   StoreGraphFavorites()

PURPOSE:    Store graph favorite settings

COMMENTS:

***************************************************************************/
void StoreGraphFavorites(void)
{
  StringCopy(Settings.Log.GraphFavorites, PostValue[0], sizeof(Settings.Log.GraphFavorites));
} // end StorePIDSettings()

/***************************************************************************

FUNCTION:   StorePublicAddress()

PURPOSE:    Store the public IP address for the http server

COMMENTS:   This accepts the message from the AS2 UI TCP/IP Setup page that
            currently only allows setting the public address - other values
            are set via Display Setup application

***************************************************************************/
void StorePublicAddress(void)
{
  char octet[4][5];

  if (   GetPostValueByField("publicOct1", octet[0], 5)
      && GetPostValueByField("publicOct2", octet[1], 5)
      && GetPostValueByField("publicOct3", octet[2], 5)
      && GetPostValueByField("publicOct4", octet[3], 5))
  {
    snprintf(Settings.PublicIP, IP_ADD_LEN+1, "%s.%s.%s.%s", octet[0], octet[1], octet[2], octet[3]);
  }
} // end StorePublicAddress()

/***************************************************************************

FUNCTION:   StoreHttpPort()

PURPOSE:    Store the http port number for the LTX web server

COMMENTS:   Because the LTX does not retain its http port setting, the ARM
            must store the port number and send it to the LTX to be set on
            startup.

            This accepts the message from LTX/DeviceComm/DeviceSetIp() when
            the http port is set via the Display setup application and
            stores the port number.

***************************************************************************/
void StoreHttpPort(void)
{
  Settings.HttpPort = atoi(PostValue[0]);
  StringCopy(Settings.PublicIP, PostValue[1], IP_ADD_LEN+1);
} // end StoreHttpPort()

/***************************************************************************

FUNCTION:   StoreHumidCtrl()

PURPOSE:    Store the humidity control parameters

COMMENTS:

***************************************************************************/
void StoreHumidCtrl(void)
{
  int HumidIndex = atoi(PostValue[0]);   // humidifier #1, #2 or #3

  Settings.HumidCtrl[HumidIndex].Mode = atoi(PostValue[1]);  // manual, timer, auto

  if (Settings.HumidCtrl[HumidIndex].Mode == 1)   // timer
  {
    Settings.HumidCtrl[HumidIndex].DutyCycle[HUMID_MODE_COOL].On    = atoi(PostValue[2]);
    Settings.HumidCtrl[HumidIndex].DutyCycle[HUMID_MODE_COOL].Off   = atoi(PostValue[3]);
    Settings.HumidCtrl[HumidIndex].DutyCycle[HUMID_MODE_RECIRC].On  = atoi(PostValue[4]);
    Settings.HumidCtrl[HumidIndex].DutyCycle[HUMID_MODE_RECIRC].Off = atoi(PostValue[5]);
    Settings.HumidCtrl[HumidIndex].DutyCycle[HUMID_MODE_REFRIG].On  = atoi(PostValue[6]);
    Settings.HumidCtrl[HumidIndex].DutyCycle[HUMID_MODE_REFRIG].Off = atoi(PostValue[7]);
  }
//  else if (Settings.HumMode == 2)    // auto
//    Settings.HumType = atoi(PostValue[1]);
} // end StoreHumidCtrl()

/***************************************************************************

FUNCTION:   StoreIoConfig()

PURPOSE:    Store the system I/O configuration

COMMENTS:   Post looks like o0=4&i0=5&o1=5&i1=6&o2=6&i2=-1...
            The tag (o0,i0,o1,i1,etc.) indicates whether it's an input or
            output and the port ID
            o0 - 'o' indicates output, 0 is the port ID
            The port ID is the board and port combination which is of the form
            portId = (board * SS_PORT_ID_MULTIPLIER) + port (similar to sensor IDs)

***************************************************************************/
void StoreIoConfig(void)
{
  int i;
  int portId;
  char ioType;
  char input = 'i';
  char output = 'o';
  char *tagIndex;
  char value[25];
  char sendAuxProg = 0;
  EQUIPMENT_IO eqIndex;
  int postItems = GetNumPostItems() - 1;  // exclude Session ID

  if (GetPostValueByField(UI_ProgramPosts[TAG_P2_IOCONFIG].Tag, value, sizeof(value)))
  {
    // check if this a post for this board type (AS2)
    if (strcmp(BOARD_TYPE, value) == 0)
    {
      // clear the I/O settings
      for (i = 0; i < SW_START_STOP; i++)
      {
        if (   i != EQ_REDLIGHT
            && i != EQ_YELLOWLIGHT
            && i != EQ_FAN
            && i != EQ_PULSEDOOR_POWER
            && i != EQ_PULSEDOOR_OPEN
            && i != EQ_PULSEDOOR_CLOSE
            && i != EQ_POWER
            && i != EQ_AIR_FLOW
            && i != EQ_LOW_TEMP)
        {
          SetIoConfig((EQUIPMENT_IO) i, input, IO_UNDEFINED);
          SetIoConfig((EQUIPMENT_IO) i, output, IO_UNDEFINED);
        }
      }

      if (GetPostValueByField("pulseDoor", value, sizeof(value)))
      {
        Settings.EquipIo[EQ_PULSEDOOR_POWER].Output = atoi(value);  // for the UI
        Settings.EquipIo[EQ_PULSEDOOR_OPEN].Enabled = atoi(value);
        Settings.EquipIo[EQ_PULSEDOOR_CLOSE].Enabled = atoi(value);
      }

      for (i = 1; i < postItems; i++)   // starting at 1 because index 0 is page name
      {
        eqIndex = (EQUIPMENT_IO) atoi(PostValue[i]);

        if (strcmp(PostTag[i], "pulseDoor") != 0)
        {
          tagIndex = PostTag[i];
          ioType = *tagIndex;       // 'o' or 'i'
          tagIndex++;               // move to next character
          portId = atoi(tagIndex);  // port ID

          SetIoConfig(eqIndex, ioType, portId);
        }

        // set newly defined auxiliary outputs to manual
        if (eqIndex >= EQ_AUX1 && eqIndex <= EQ_AUX8)
        {
          if (Settings.AuxProgram[eqIndex - EQ_AUX1].Rule[0].Type == RT_UNDEFINED)
          {
            Settings.AuxProgram[eqIndex - EQ_AUX1].Rule[0].Type = RT_MANUAL;
            Settings.AuxProgram[eqIndex - EQ_AUX1].DutyCycle = 100;
            Settings.AuxProgram[eqIndex - EQ_AUX1].Period = 1;
            Settings.AuxProgram[eqIndex - EQ_AUX1].Units = 1;  // hour
            sendAuxProg = 1;
          }
        }
      }

      // clear programs for auxiliary outputs no longer defined
      for (i = EQ_AUX1; i <= EQ_AUX8; i++)
      {
        if (!Settings.EquipIo[i].Enabled)
        {
          Settings.AuxProgram[i - EQ_AUX1].Rule[0].Type = RT_UNDEFINED;
          sendAuxProg = 1;
        }
      }

      // update the UI if the auxiliary output configuration changes
      if (sendAuxProg)
      {
        UI_SendAuxProgram(EQ_AUX1, 1, MSG_SEND);
      }
    }
  }
} // end StoreIoConfig()

/***************************************************************************

FUNCTION:   StoreIoName()

PURPOSE:    Rename the equipment I/O

COMMENTS:

***************************************************************************/
void StoreIoName(void)
{
  uint8_t eqIndex;

  if (   StorePostValue("ioRename", &eqIndex, ET_UINT8, NULL) == SPV_SUCCESS
      && eqIndex < EQ_TOTAL_IO
      && Settings.EquipIo[eqIndex].Renamable)
  {
    ConvertSpecialChars(PostValue[1], MSG_MAX_VALUE_LEN);
    StringCopy(Settings.EquipIo[eqIndex].Name, PostValue[1], LOG_LABELS);
  }
} // end StoreIoName()

/***************************************************************************

FUNCTION:   StoreIoTranslate()

PURPOSE:    Translate the equipment I/O

COMMENTS:

***************************************************************************/
void StoreIoTranslate(void)
{
  uint8_t eqIndex;

  // because some labels can be edited by the user, only load the translated
  // string if the label is blank to avoid over writing user definitions
  if (   StorePostValue("equipDesc", &eqIndex, ET_UINT8, NULL) == SPV_SUCCESS
      && eqIndex < EQ_TOTAL_IO
      && (   !Settings.EquipIo[eqIndex].Renamable
          || (   Settings.EquipIo[eqIndex].Renamable
              && strcmp(Settings.EquipIo[eqIndex].Name, "") == 0)))
  {
    if (StorePostValue("name", Settings.EquipIo[eqIndex].Name, ET_STRING, LOG_LABELS) == SPV_SUCCESS)
    {
      debug_printf("Equip Translate - %s\r\n", Settings.EquipIo[eqIndex].Name);
    }
    else
    {
      debug_printf("EquipDesc Error - %s\r\n", PostValue[1]);
    }
  }
  else
  {
    debug_printf("EquipDesc - Not Blank\r\n");
  }
} // end StoreIoTranslate()

/***************************************************************************

FUNCTION:   StorePwmName()

PURPOSE:    Translate the PWM outputs

COMMENTS:

***************************************************************************/
void StorePwmName(void)
{
  uint8_t pwmIndex;

  if (   StorePostValue("pwmDesc", &pwmIndex, ET_UINT8, NULL) == SPV_SUCCESS
      && pwmIndex < PWM_TOTAL_EQ)
  {
    if (StorePostValue("name", Settings.PWM[pwmIndex].Name, ET_STRING, LOG_LABELS) == SPV_SUCCESS)
    {
      debug_printf("PWM Translate - %s\r\n", Settings.PWM[pwmIndex].Name);
    }
    else
    {
      debug_printf("PWM Desc Error - %s\r\n", PostValue[1]);
    }
  }
  else
  {
    debug_printf("PWM Desc - Not Blank\r\n");
  }
} // end StorePwmName()

/***************************************************************************

FUNCTION:   StoreAnalogBoardLabel()

PURPOSE:    Translate the analog board labels

COMMENTS:

***************************************************************************/
void StoreAnalogBoardLabel(void)
{
  uint8_t boardIndex;

  // because this label can be edited by the user, only load the translated
  // string if the label is blank to avoid over writing user definitions
  if (   StorePostValue("boardLabel", &boardIndex, ET_UINT8, NULL) == SPV_SUCCESS
      && boardIndex < ANALOG_BOARDS_PER_SYSTEM
      && strcmp(Settings.AnalogBoard[boardIndex].Label, "") == 0)
  {
    if (StorePostValue("name", Settings.AnalogBoard[boardIndex].Label, ET_STRING, BOARD_LABELS+1) == SPV_SUCCESS)
    {
      debug_printf("Board Translate - %s\r\n", Settings.AnalogBoard[boardIndex].Label);
    }
    else
    {
      debug_printf("AnalogBoard Error - %s\r\n", PostValue[1]);
    }
  }
  else
  {
    debug_printf("AnalogBoard - Not Blank\r\n");
  }
} // end StoreAnalogBoardLabel()

/***************************************************************************

FUNCTION:   StoreTempSensorLabel()

PURPOSE:    Translate the sensor labels

COMMENTS:

***************************************************************************/
void StoreTempSensorLabel(void)
{
  uint8_t sensorIndex;

  // because this label can be edited by the user, only load the translated
  // string if the label is blank to avoid over writing user definitions
  if (   StorePostValue("tempSensor", &sensorIndex, ET_UINT8, NULL) == SPV_SUCCESS
      && sensorIndex < ANALOG_SENSORS_PER_BOARD
      && strcmp(Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Sensor[sensorIndex].Label, "") == 0)
  {
    if (StorePostValue("name", Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Sensor[sensorIndex].Label, ET_STRING, BOARD_LABELS+1) == SPV_SUCCESS)
    {
      debug_printf("Sensor Translate - %s\r\n", Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Sensor[sensorIndex].Label);
    }
    else
    {
      debug_printf("AnalogBoard Temp Sensor Error - %s\r\n", PostValue[1]);
    }
  }
  else
  {
    debug_printf("AnalogBoard Temp Sensor - Not Blank\r\n");
  }
} // end StoreTempSensorLabel()

/***************************************************************************

FUNCTION:   StoreHumidSensorLabel()

PURPOSE:    Translate the sensor labels

COMMENTS:

***************************************************************************/
void StoreHumidSensorLabel(void)
{
  uint8_t sensorIndex;

  // because this label can be edited by the user, only load the translated
  // string if the label is blank to avoid over writing user definitions
  if (   StorePostValue("humidSensor", &sensorIndex, ET_UINT8, NULL) == SPV_SUCCESS
      && sensorIndex < ANALOG_SENSORS_PER_BOARD
      && strcmp(Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[sensorIndex].Label, "") == 0)
  {
    if (StorePostValue("name", Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[sensorIndex].Label, ET_STRING, BOARD_LABELS+1) == SPV_SUCCESS)
    {
      debug_printf("Sensor Translate - %s\r\n", Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[sensorIndex].Label);
    }
    else
    {
      debug_printf("AnalogBoard Humidity Sensor Error - %s\r\n", PostValue[1]);
    }
  }
  else
  {
    debug_printf("AnalogBoard Humidity Sensor - Not Blank\r\n");
  }
} // end StoreHumidSensorLabel()

/***************************************************************************

FUNCTION:   StoreBayLabel()

PURPOSE:    Translate the bay labels

COMMENTS:

***************************************************************************/
void StoreBayLabel(void)
{
  uint8_t bayIndex;

  // because this label can be edited by the user, only load the translated
  // string if the label is blank to avoid over writing user definitions
  if (   StorePostValue("bayLabel", &bayIndex, ET_UINT8, NULL) == SPV_SUCCESS
      && bayIndex < NUM_LOADLOG_SENSORS
      && strcmp(Settings.LoadMonitor.Bay[bayIndex].Label, "") == 0)
  {
    if (StorePostValue("name", Settings.LoadMonitor.Bay[bayIndex].Label, ET_STRING, BOARD_LABELS+1) == SPV_SUCCESS)
    {
      debug_printf("Bay Translate - %s\r\n", Settings.LoadMonitor.Bay[bayIndex].Label);
    }
    else
    {
      debug_printf("LoadMonitor Bay Error - %s\r\n", PostValue[1]);
    }
  }
  else
  {
    debug_printf("LoadMonitor Bay - Not Blank\r\n");
  }
} // end StoreBayLabel()

/***************************************************************************

FUNCTION:   StoreLoadMonitor()

PURPOSE:    Store the loading monitor settings

COMMENTS:   StoreBayNames() also stores bay names from the light control page

***************************************************************************/
void StoreLoadMonitor(void)
{
  ConvertSpecialChars(PostValue[0], MSG_MAX_VALUE_LEN);
  StringCopy(Settings.LoadMonitor.Bay[0].Label, PostValue[0], BOARD_LABELS+1);

  ConvertSpecialChars(PostValue[2], MSG_MAX_VALUE_LEN);
  StringCopy(Settings.LoadMonitor.Bay[1].Label, PostValue[2], BOARD_LABELS+1);

  StorePostValue("selBay1IrSensor", &Settings.LoadMonitor.Bay[0].SensorID, ET_UINT8, NULL);
  StorePostValue("selBay2IrSensor", &Settings.LoadMonitor.Bay[1].SensorID, ET_UINT8, NULL);

  StorePostValue("LoadHighTemp", &Settings.LoadMonitor.AlarmTemp[0], ET_FLOAT, NULL);
  StorePostValue("LoadLowTemp", &Settings.LoadMonitor.AlarmTemp[1], ET_FLOAT, NULL);
} // end StoreLoadMonitor()

/***************************************************************************

FUNCTION:   StoreLoadPause()

PURPOSE:    Pause/resume temperature logging for a bay

COMMENTS:

***************************************************************************/
void StoreLoadPause(void)
{
  int paused = atoi(PostValue[0]);
  int bay = atoi(PostValue[1]);

  Settings.LoadMonitor.Bay[bay].Status = paused;

  // the very first record is a dummy record that gets created when the 'start new season'
  // button is pressed on load monitor setup page (the grapher needs one point to open)
  // it is overwritten when data actually starts being logged when the 'resume' button is
  // pressed in the grapher
  if (   paused == 0
      && SDCardHeader.LoadLog.NumRecords == 0
      && LoadLogUnwrittenRecords() == 1)
  {
    LoadLogIndexReset();
  }

  if (paused == 0)
  {
    LoadLogTempAccumulatorReset(bay);
  }
} //  end StoreLoadPause()

/***************************************************************************

FUNCTION:   StoreLoadPipe()

PURPOSE:    Set the pipe number for a bay

COMMENTS:

***************************************************************************/
void StoreLoadPipe(void)
{
  Settings.LoadMonitor.Bay[atoi(PostValue[1])].Pipe = atoi(PostValue[0]);
} //  end StoreLoadPipe()

/***************************************************************************

FUNCTION:   StoreLoadStop()

PURPOSE:    Stop temperature logging for a bay

COMMENTS:

***************************************************************************/
void StoreLoadStop(void)
{
  LoadLogWrite(1);  // force the write
  Settings.LoadMonitor.Bay[atoi(PostValue[1])].Status = LL_FULL;
} //  end StoreLoadStop()

/***************************************************************************

FUNCTION:   StoreLtxInit()

PURPOSE:    Check the LTX firmware version against the ARM version

COMMENTS:

***************************************************************************/
void StoreLtxInit(void)
{
  if (strcmp(PostValue[1], ARM_FIRMWARE_VERSION) == 0)
  {
    MessagingStatus = MS_RESPONDING;
    LtxInitialized = 1;
    FreeMsgQueue();
    WarningsClearChk();
  }
  else
  {
    MessagingStatus = MS_NOT_INITIALIZED;
  }
} // end StoreLtxInit()

/***************************************************************************

FUNCTION:   StoreMasterSlave()

PURPOSE:    Store the master/slave mode

COMMENTS:

***************************************************************************/
void StoreMasterSlave(void)
{
  // if changing to slave mode
  if (   Settings.MasterSlave != MSMODE_SLAVE
      && atoi(PostValue[0]) == MSMODE_SLAVE)
  {
    Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Sensor[SENSOR_OUTSIDE_TEMP].Value = SENSOR_VAL_UNDEFINED;
    Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_OUTSIDE_HUMID].Value = SENSOR_VAL_UNDEFINED;

    // start the no broadcast timer
    AlarmTimer[AL_NOBROADCAST] = XTimerVal;
  }
  Settings.MasterSlave = atoi(PostValue[0]);

  if (Settings.MasterSlave == MSMODE_SLAVE)
  {
    StringCopy(Settings.MasterIP, PostValue[1], IP_ADD_LEN+1);
  }
  else
  {
    Settings.MasterIP[0] = 0;
  }
} // end StoreMasterSlave()

/***************************************************************************

FUNCTION:   StoreMisc()

PURPOSE:    Store the miscellaneous setup data

COMMENTS:

***************************************************************************/
void StoreMisc(void)
{
  char value[25];

  if (GetPostValueByField(UI_ProgramPosts[TAG_P1_MISC].Tag, value, sizeof(value)))
  {
    // check if this a post for this board type (AS2)
    if (strcmp(BOARD_TYPE, value) == 0)
    {
      StorePostValue("selRefrMode", &Settings.Refrig.Mode, ET_UINT8, NULL);
      StorePostValue("defrostInterval", &Settings.Refrig.DefrostPeriod, ET_UINT8, NULL);
      StorePostValue("defrostTime", &Settings.Refrig.DefrostDuration, ET_UINT8, NULL);

      if (Settings.SystemMode == SM_POTATO)
      {
        StorePostValue("tempThresh", &Settings.HeatTempThresh, ET_FLOAT, NULL);
      }
      else
      {
        StorePostValue("tempThresh", &Settings.Refrig.Limit, ET_FLOAT, NULL);
      }

      StorePostValue("selCtrlMode", &Settings.CavityHeat.Label, ET_UINT8, NULL);
      StorePostValue("selCavityCtrl", &Settings.CavityHeat.Mode, ET_UINT8, NULL);
      StorePostValue("cavityDiff", &Settings.CavityHeat.Diff, ET_FLOAT, NULL);
      StorePostValue("cavityDutyCycle", &Settings.CavityHeat.DutyCycle, ET_UINT8, NULL);      // manual = duty cycle
      StorePostValue("selCavityCtrlSensor", &Settings.CavityHeat.DutyCycle, ET_UINT8, NULL);  // auto = sensor ID
      StorePostValue("cavStandbyOn", &Settings.CavityHeat.StandbyOn, ET_UINT8, NULL);

      if (Settings.CavityHeat.Mode == 1)   // off
      {
        GetPostValueByField("cavityCtrlType", value, sizeof(value));
        if (strcmp(value, "3") == 0)   // in auto mode
        {
          Settings.CavityHeat.Mode = 0;    // off in auto mode (switched from auto to off - UI still in auto mode)
        }

        // reset the duty cycle timer
        IntervalTimer[IT_CAVITYHEATCYCLE] = 0;
      }

      StorePostValue("kbPref", &Settings.KbPref, ET_UINT8, NULL);
    }
  }
} // end StoreMisc()

/***************************************************************************

FUNCTION:   StoreNetworkNode()

PURPOSE:    Edit a network node name

COMMENTS:

***************************************************************************/
void StoreNetworkNode(void)
{
  char IpAdd[IP_ADD_LEN+1];

  ConvertSpecialChars(PostValue[5], MSG_MAX_VALUE_LEN);
  snprintf(IpAdd, IP_ADD_LEN+1, "%s.%s.%s.%s", PostValue[0], PostValue[1], PostValue[2], PostValue[3]);
  NetworkNodeAdd(PostValue[5], IpAdd, IpAdd, atoi(PostValue[4]));
} // end StoreNetworkNode()

/***************************************************************************

FUNCTION:   StoreNetworkNodeUpdate()

PURPOSE:    Edit a network node

COMMENTS:

***************************************************************************/
void StoreNetworkNodeUpdate(void)
{
  NetworkNodeAdd(PostValue[0], PostValue[1], PostValue[2], atoi(PostValue[3]));
} // end StoreNetworkNodeUpdate()

/***************************************************************************

FUNCTION:   StoreNodeDiscovery()

PURPOSE:    Add network (remote) nodes discovered from UI 'find' function

COMMENTS:

***************************************************************************/
void StoreNodeDiscovery(void)
{
  int  BufIndex = 0;
  int  IpIndex = 0;
  char IpAdd[IP_ADD_LEN+1];
  int  NameIndex = 0;
  char Name[SITE_ID_LEN+1];
  int  PortIndex = 0;
  char Port[HTTPPORT_LEN+1];

  while (PostValue[0][BufIndex] != 0 && BufIndex < MSG_RX_BUFFER_SIZE)
  {
    // parse out the storage name
    while (PostValue[0][BufIndex] != ',' && PostValue[0][BufIndex] != 0)
      Name[NameIndex++] = PostValue[0][BufIndex++];
    Name[NameIndex] = 0;
    BufIndex++;   // skip the ','

    // parse out the IP
    while (PostValue[0][BufIndex] != ',' && PostValue[0][BufIndex] != 0)
      IpAdd[IpIndex++] = PostValue[0][BufIndex++];
    IpAdd[IpIndex] = 0;
    BufIndex++;   // skip the ','

    // parse out the port
    while (PostValue[0][BufIndex] != ',' && PostValue[0][BufIndex] != 0)
      Port[PortIndex++] = PostValue[0][BufIndex++];
    Port[PortIndex] = 0;

    NetworkNodeAdd(Name, IpAdd, "", atoi(Port));
    BufIndex++;   // skip the ','
    NameIndex = 0;
    IpIndex = 0;
    PortIndex = 0;
  }
} // end StoreNodeDiscovery()

/***************************************************************************

FUNCTION:   StoreOutsideAir()

PURPOSE:    Store outside air settings

COMMENTS:

***************************************************************************/
void StoreOutsideAir(void)
{
  StorePostValue("ctrlMode", &Settings.OutsideAir.CtrlMode, ET_UINT8, NULL);
  StorePostValue("OutsideAirSet", &Settings.OutsideAir.Diff, ET_FLOAT, NULL);
  StorePostValue("selAboveBelow", &Settings.OutsideAir.AboveBelow, ET_UINT8, NULL);
  StorePostValue("selTempRef", &Settings.OutsideAir.TempRef, ET_UINT8, NULL);
  StorePostValue("calcHumid", &Settings.OutsideAir.CalcHumidMax, ET_UINT8, NULL);
} // end StoreOutsideAir()

/***************************************************************************

FUNCTION:   StorePassword()

PURPOSE:    Store the level one usernames & passwords

COMMENTS:

***************************************************************************/
void StorePassword(void)
{
  int i;
  int additionalAccts = 0;

  // count accounts other than the default account
  for (i = 1; i < NUM_PASSWORDS; i++)
  {
    if (strcmp(PostValue[i*2], "") != 0)
    {
      ConvertSpecialChars(PostValue[i*2], MSG_MAX_VALUE_LEN);
      ConvertSpecialChars(PostValue[(i*2)+1], MSG_MAX_VALUE_LEN);
      additionalAccts++;
    }
  }

  // store the default account
  ConvertSpecialChars(PostValue[0], MSG_MAX_VALUE_LEN);
  ConvertSpecialChars(PostValue[1], MSG_MAX_VALUE_LEN);

  if (strcmp(PostValue[0], "") != 0)
  {
    StringCopy(Settings.User[0].ID, PostValue[0], PASSWORD_LEN+1);

    if (strcmp(PostValue[1], "") != 0)
    {
      StringCopy(Settings.User[0].Password, PostValue[1], PASSWORD_LEN+1);
    }
    else
    {
      if (additionalAccts == 0)
      {
        StringCopy(Settings.User[0].Password, PostValue[1], PASSWORD_LEN+1);
      }
      else
      {
        StringCopy(Settings.User[0].Password, DEFAULT_PASSWORD, PASSWORD_LEN+1);
      }
    }
  }
  else
  {
    // don't allow the default user account to be deleted
    StringCopy(Settings.User[0].ID, DEFAULT_USER, PASSWORD_LEN+1);

    if (strcmp(PostValue[1], "") == 0 && additionalAccts > 0)
    {
      StringCopy(Settings.User[0].Password, DEFAULT_PASSWORD, PASSWORD_LEN+1);
    }
    else
    {
      StringCopy(Settings.User[0].Password, PostValue[1], PASSWORD_LEN+1);
    }
  }

  // store the additional accounts
  for (i = 1; i < NUM_PASSWORDS; i++)
  {
    if (strcmp(PostValue[i*2], "") != 0)
    {
      // only update the user account if there is an ID and a password
      if (strcmp(PostValue[(i*2)+1], "") != 0)
      {
        StringCopy(Settings.User[i].ID, PostValue[i*2], PASSWORD_LEN+1);
        StringCopy(Settings.User[i].Password, PostValue[(i*2)+1], PASSWORD_LEN+1);
      }
    }
    else
    {
      // delete the user account if the ID is blank
      StringCopy(Settings.User[i].ID, "", PASSWORD_LEN+1);
      StringCopy(Settings.User[i].Password, "", PASSWORD_LEN+1);
    }
  }
} // end StorePassword()

/***************************************************************************

FUNCTION:   StorePIDLog()

PURPOSE:    Set the PID logging mode

COMMENTS:

***************************************************************************/
void StorePIDLog(uint8_t *PIDLogging)
{
  if (strcmp(PostValue[0], "Turn On") == 0)
    *PIDLogging = 1;
  else
    *PIDLogging = 0;
} // end StorePIDLog()

/***************************************************************************

FUNCTION:   StorePIDSettings()

PURPOSE:    Store the PID log settings

COMMENTS:

***************************************************************************/
void StorePIDSettings(void)
{
  Settings.Log.PID.Wrap = atoi(PostValue[0]);
} // end StorePIDSettings()

/***************************************************************************

FUNCTION:   StorePostValue()

PURPOSE:    Store a post value

COMMENTS:

***************************************************************************/
STOREPOST_RESULT StorePostValue(char *element, void *setting, ELEMENT_TYPE type, size_t length)
{
  STOREPOST_RESULT retVal = SPV_ELEMENT;   // UI element not found in post
  char *ptr;
  char value[50];

  if (type == ET_STRING && length != NULL)
  {
    if (GetPostValueByField(element, (char *)setting, length))
    {
      retVal = SPV_SUCCESS;
    }
  }
  else
  {
    if (GetPostValueByField(element, value, sizeof(value)))
    {
      switch (type)
      {
        case ET_UINT8:
        case ET_UINT16:
        case ET_UINT32:
        {
          errno = 0;
          uint32_t result = strtoul(value, &ptr, 10);

          if (ptr == value)
          {
            retVal = SPV_NAN;
          }
          else if (   (errno == ERANGE && result == ULONG_MAX)
                   || (errno != 0 && result == 0))
          {
            retVal = SPV_RANGE;
          }
          else
          {
            switch (type)
            {
              case ET_UINT32:
              {
                *((uint32_t *) setting) = (uint32_t) result;
                retVal = SPV_SUCCESS;
                break;
              }
              case ET_UINT16:
              {
                if (result > 0xFFFF)
                {
                  retVal = SPV_RANGE;
                }
                else
                {
                  *((uint16_t *) setting) = (uint16_t) result;
                  retVal = SPV_SUCCESS;
                }
                break;
              }
              case ET_UINT8:
              {
                if (result > CHAR_MAX)
                {
                  retVal = SPV_RANGE;
                }
                else
                {
                  *((uint8_t *) setting) = (uint8_t) result;
                  retVal = SPV_SUCCESS;
                }
                break;
              }
            }
          }
          break;
        }
        case ET_INT8:
        case ET_INT16:
        case ET_INT32:
        {
          errno = 0;
          int32_t result = strtol(value, &ptr, 10);

          if (ptr == value)
          {
            retVal = SPV_NAN;
          }
          else if (   (errno == ERANGE && (result == LONG_MAX || result == LONG_MIN))
                   || (errno != 0 && result == 0))
          {
            retVal = SPV_RANGE;
          }
          else
          {
            switch (type)
            {
              case ET_INT32:
              {
                *((int32_t *) setting) = (int32_t) result;
                retVal = SPV_SUCCESS;
                break;
              }
              case ET_INT16:
              {
                if (result > SHRT_MAX || result < SHRT_MIN)
                {
                  retVal = SPV_RANGE;
                }
                else
                {
                  *((int16_t *) setting) = (int16_t) result;
                  retVal = SPV_SUCCESS;
                }
                break;
              }
              case ET_INT8:
              {
                if (result > SCHAR_MAX || result < SCHAR_MIN)
                {
                  retVal = SPV_RANGE;
                }
                else
                {
                  *((int8_t *) setting) = (int8_t) result;
                  retVal = SPV_SUCCESS;
                }
                break;
              }
            }
          }
          break;
        }
        case ET_FLOAT:
        {
          errno = 0;
          float result = strtof(value, &ptr);

          if (   (errno == ERANGE)
              || (errno != 0 && result == 0))
          {
            retVal = SPV_RANGE;
          }
          else if (ptr == value)
          {
            retVal = SPV_NAN;
          }
          else
          {
            *((float *) setting) = (float) result;
            retVal = SPV_SUCCESS;
          }
          break;
        }
        default:
        {
          retVal = SPV_TYPE;
          break;
        }
      }
    }
  }
  return retVal;
} // end StorePostValue()

/***************************************************************************

FUNCTION:   StorePlenumSetup()

PURPOSE:    Store the plenum temperature & humidity settings

COMMENTS:

***************************************************************************/
void StorePlenumSetup(void)
{
  char boardType[50];
  float NewSetpoint;

  if (GetPostValueByField(UI_ProgramPosts[TAG_P1_PLENSETUP].Tag, boardType, sizeof(boardType)))
  {
    // check if this a post for this board type (AS2)
    if (strcmp(BOARD_TYPE, boardType) == 0)
    {
      if (StorePostValue("PlenumTempSet", &NewSetpoint, ET_FLOAT, NULL) == 0)
      {
        if (Settings.Plenum.TempSet != NewSetpoint)
        {
          Settings.Plenum.TempSet = NewSetpoint;
          Settings.Ramp.TargetTemp = Settings.Plenum.TempSet;  // so we don't inadvertently start ramping
        }
      }

      StorePostValue("PlenumHumidSet", &Settings.Plenum.HumidSet, ET_UINT8, NULL);
      StorePostValue("selHumSetpointRef", &Settings.Plenum.HumidSetpointRef, ET_UINT8, NULL);

      // onion/cure modes
      // economy and maximum modes
      StorePostValue("BurnerTempSet", &Settings.Burner.TempSet, ET_FLOAT, NULL);

      // maximum mode
      StorePostValue("BurnerThreshold", &Settings.Burner.Threshold, ET_FLOAT, NULL);

      // manual mode
      StorePostValue("BurnerManualMax", &Settings.Burner.TempSet, ET_FLOAT, NULL);
      StorePostValue("BurnerManualRestart", &Settings.Burner.Threshold, ET_FLOAT, NULL);
    }
  }
} // end StorePlenumSetup()

/***************************************************************************

FUNCTION:   StorePWMChannels()

PURPOSE:    Store the PWM channels for 4-20 outputs

COMMENTS:

***************************************************************************/
void StorePWMChannels(void)
{
  int i;
  int channel;
  int eqIndex;
  int postItems = GetNumPostItems() - 1;  // exclude Session ID
  char boardType[50];

  if (GetPostValueByField(UI_ProgramPosts[TAG_P2_PWMCHANNEL].Tag, boardType, sizeof(boardType)))
  {
    // check if this a post for this board type (Agri-Star)
    if (strcmp(BOARD_TYPE, boardType) == 0)
    {
      // clear the settings
      for (i = 0; i < PWM_TOTAL_EQ; ++i)
      {
        if (i != PWM_FAN)
        {
          PwmChannel[i].Output = PWM_MIN_VALUE;
          PWM_UpdateChannel((PWM_EQUIPMENT) i);
          Settings.PWM[i].Enabled = 0;
          Settings.PWM[i].Channel = PWM_UNDEFINED;
        }
      }

      for (i = 1; i < postItems; i++)   // starting at 1 because index 0 is page name
      {
        channel = atoi(PostTag[i]);
        eqIndex = atof(PostValue[i]);

        if (eqIndex != -1)
        {
          Settings.PWM[eqIndex].Enabled = 1;
          Settings.PWM[eqIndex].Channel = channel;
        }
      }
    }
  }
} // end StorePWMChannels()

/***************************************************************************

FUNCTION:   StoreRampRate()

PURPOSE:    Store the temperature ramping parameters

COMMENTS:

***************************************************************************/
void StoreRampRate(void)
{
  int hours;

  StorePostValue("updTemp", &Settings.Ramp.UpdateTemp, ET_FLOAT, NULL);
  StorePostValue("rampTempDiff", &Settings.Ramp.TempDiff, ET_FLOAT, NULL);
  StorePostValue("selTemp", &Settings.Ramp.TempRef, ET_UINT8, NULL);
  StorePostValue("targetTemp", &Settings.Ramp.TargetTemp, ET_FLOAT, NULL);

  if (StorePostValue("rampUpdateHours", &hours, ET_INT32, NULL) == SPV_NAN) // this indicates the "Automatically" string
  {
    Settings.Ramp.UpdatePeriod = 255;
  }
  else
  {
    Settings.Ramp.UpdatePeriod = hours;
  }
} // end StoreRampRate()

/***************************************************************************

FUNCTION:   StoreRefrig()

PURPOSE:    Store the refrigeration settings

COMMENTS:

***************************************************************************/
void StoreRefrig(void)
{
  char value[25];

  if (GetPostValueByField(UI_ProgramPosts[TAG_P2_REFRIG].Tag, value, sizeof(value)))
  {
    // check if this a post for this board type (AS2)
    if (strcmp(BOARD_TYPE, value) == 0)
    {
      StorePostValue("Stage1On",  &Settings.Refrig.Stage[0].On,  ET_UINT8, NULL);
      StorePostValue("Stage1Off", &Settings.Refrig.Stage[0].Off, ET_UINT8, NULL);
      StorePostValue("Stage2On",  &Settings.Refrig.Stage[1].On,  ET_UINT8, NULL);
      StorePostValue("Stage2Off", &Settings.Refrig.Stage[1].Off, ET_UINT8, NULL);
      StorePostValue("Stage3On",  &Settings.Refrig.Stage[2].On,  ET_UINT8, NULL);
      StorePostValue("Stage3Off", &Settings.Refrig.Stage[2].Off, ET_UINT8, NULL);
      StorePostValue("Stage4On",  &Settings.Refrig.Stage[3].On,  ET_UINT8, NULL);
      StorePostValue("Stage4Off", &Settings.Refrig.Stage[3].Off, ET_UINT8, NULL);
      StorePostValue("Stage5On",  &Settings.Refrig.Stage[4].On,  ET_UINT8, NULL);
      StorePostValue("Stage5Off", &Settings.Refrig.Stage[4].Off, ET_UINT8, NULL);
      StorePostValue("Stage6On",  &Settings.Refrig.Stage[5].On,  ET_UINT8, NULL);
      StorePostValue("Stage6Off", &Settings.Refrig.Stage[5].Off, ET_UINT8, NULL);
      StorePostValue("Stage7On",  &Settings.Refrig.Stage[6].On,  ET_UINT8, NULL);
      StorePostValue("Stage7Off", &Settings.Refrig.Stage[6].Off, ET_UINT8, NULL);
      StorePostValue("Stage8On",  &Settings.Refrig.Stage[7].On,  ET_UINT8, NULL);
      StorePostValue("Stage8Off", &Settings.Refrig.Stage[7].Off, ET_UINT8, NULL);

      if (StorePostValue("PRefrValue", &Settings.Refrig.PID.P, ET_UINT16, NULL) == SPV_SUCCESS)
      {
        PIDCtrl[PID_REFRIGERATION].Kp = Settings.Refrig.PID.P;
      }

      if (StorePostValue("IRefrValue", &Settings.Refrig.PID.I, ET_UINT16, NULL) == SPV_SUCCESS)
      {
        PIDCtrl[PID_REFRIGERATION].Ki = Settings.Refrig.PID.I;
      }

      if (StorePostValue("DRefrValue", &Settings.Refrig.PID.D, ET_UINT16, NULL) == SPV_SUCCESS)
      {
        PIDCtrl[PID_REFRIGERATION].Kd = Settings.Refrig.PID.D;
      }

      StorePostValue("URefrValue", &Settings.Refrig.PID.U, ET_UINT16, NULL);
      StorePostValue("RefrigerationPurge", &Settings.Refrig.Purge, ET_UINT8, NULL);
      StorePostValue("PurgeThreshold", &Settings.Co2.Purge.RefrigThresh, ET_UINT8, NULL);
    }
  }
} // end StoreRefrig()

/***************************************************************************

FUNCTION:   StoreRefrigDiag()

PURPOSE:    Set the refrigeration diagnostic mode

COMMENTS:

***************************************************************************/
void StoreRefrigDiag(uint8_t *Diagnostic)
{
  // don't allow diag mode if system or refrig switch is off
  if (   !CheckInputs(SW_START_STOP)
      || !CheckInputs(SW_REFRIG_AUTO))
  {
    return;
  }

  if (strcmp(PostValue[0], "On") == 0)
  {
    *Diagnostic = 2;
  }
  else
  {
    *Diagnostic = 1;
  }

  // reset the timer
  IntervalTimer[IT_REFRIGDIAG] = XTimerVal;
} // end StoreRefrigDiag()

/***************************************************************************

FUNCTION:   StoreRemoteOff()

PURPOSE:    Set the remote off for the passed equipment component

COMMENTS:

***************************************************************************/
void StoreRemoteOff(uint8_t *RemoteOff)
{
  if (strcmp(PostValue[0], "Off") == 0)
    *RemoteOff = 1;
  else if (strcmp(PostValue[0], "On") == 0)
    *RemoteOff = 2;   // Manual: force output ON (replaces CPLD panel switch)
  else
    *RemoteOff = 0;
} // end StoreRemoteOff()

/***************************************************************************

FUNCTION:   ToggleRemoteOff()

PURPOSE:    Toggle the remote off for the passed equipment component

COMMENTS:

***************************************************************************/
void ToggleRemoteOff(uint8_t *RemoteOff)
{
  if (*RemoteOff == 0)
    *RemoteOff = 1;
  else
    *RemoteOff = 0;
} // end ToggleRemoteOff()

/***************************************************************************

FUNCTION:   StoreSystemOff()

PURPOSE:    Set the remote off for the whole system

COMMENTS:   Uses the fan remote off

***************************************************************************/
void StoreSystemOff(uint8_t *RemoteOff)
{
  if (strcmp(PostValue[0], "Stop") == 0)
    *RemoteOff = 3;   // System stop (distinct from Manual=2)
  else
    *RemoteOff = 0;
} // end StoreSystemOff()

/***************************************************************************

FUNCTION:   StoreRuntimes()

PURPOSE:    Store the run clock settings

COMMENTS:

***************************************************************************/
void StoreRuntimes(void)
{
  int SetIndex = 0;
  int StrIndex = 0;
  int RtIndex = 0;
  char RtValue[5];
  char *RunTimes;

  if (   Settings.SystemMode == SM_ONION
      && CheckInputs(SW_CURE_AUTO)
      && Settings.RemoteOff[RO_CURE] == 0)
  {
    RunTimes = Settings.Cure.RunTimes;
  }
  else
  {
    RunTimes = Settings.RunTimes;
  }

  while (SetIndex < 48)
  {
    if ((PostValue[0][StrIndex] == '+') || (PostValue[0][StrIndex] == '\0'))
    {
      RtValue[RtIndex] = '\0';
      RunTimes[SetIndex++] = atoi(RtValue);
      RtIndex = 0;
      if (PostValue[0][StrIndex] == '\0')   // safety
      {
        break;
      }
      StrIndex++;
    }
    else
    {
      RtValue[RtIndex++] = PostValue[0][StrIndex++];
    }
  }
} // end StoreRuntimes()

/***************************************************************************

FUNCTION:   StoreServiceInfo()

PURPOSE:    Store the service information

COMMENTS:

***************************************************************************/
void StoreServiceInfo(void)
{
  ConvertSpecialChars(PostValue[0], MSG_MAX_VALUE_LEN);
  StringCopy(Settings.DealerName, PostValue[0], NAME_LEN+1);

  ConvertSpecialChars(PostValue[1], MSG_MAX_VALUE_LEN);
  StringCopy(Settings.DealerPhone, PostValue[1], PHONE_LEN+1);

  ConvertSpecialChars(PostValue[2], MSG_MAX_VALUE_LEN);
  StringCopy(Settings.TechName, PostValue[2], NAME_LEN+1);

  ConvertSpecialChars(PostValue[3], MSG_MAX_VALUE_LEN);
  StringCopy(Settings.TechPhone, PostValue[3], PHONE_LEN+1);
} // end StoreServiceInfo()

/***************************************************************************

FUNCTION:   StoreSettings()

PURPOSE:    Restore the settings from a file

COMMENTS:   Originally atoi() & atof() were called in the 'if' statements
            based on the element type, but calling them once each at the
            beginning of the function and then using the appropriate local
            variable saved a lot of code space.

            In the sections that deal with arrays of structures 'IntValue'
            is used directly for the array index and sometimes not, depending
            upon the complexity of the array.  Readability was better with
            local variables for 'board' & 'sensor' than 'IntValue', but a
            little more code space could be saved by not doing this.

***************************************************************************/
void StoreSettings(void)
{
  int IntValue = atoi(PostValue[1]);
  int FloatValue = atof(PostValue[1]);
  int i;

  if      (strcmp(PostTag[1], "DST") == 0)                Settings.DST = IntValue;
  else if (strcmp(PostTag[1], "HttpPort") == 0)           Settings.HttpPort = IntValue;
  else if (strcmp(PostTag[1], "UserLogInterval") == 0)    Settings.Log.User.Interval = IntValue;
  else if (strcmp(PostTag[1], "UserLogWrap") == 0)        Settings.Log.User.Wrap = IntValue;
  else if (strcmp(PostTag[1], "PIDLogWrap") == 0)         Settings.Log.PID.Wrap = IntValue;
  else if (strcmp(PostTag[1], "LogPIDDoor") == 0)         Settings.Log.PID.Door = IntValue;
  else if (strcmp(PostTag[1], "LogPIDRefrig") == 0)       Settings.Log.PID.Refrig = IntValue;
  else if (strcmp(PostTag[1], "GraphFavorites") == 0)     StringCopy(Settings.Log.GraphFavorites, PostValue[1], MSG_TX_CGIVAR_SIZE);

  // basic setup page
  else if (strcmp(PostTag[1], "StorageName") == 0)        StringCopy(Settings.StorageName, PostValue[1], SITE_ID_LEN+1);
  else if (strcmp(PostTag[1], "TempType") == 0)           Settings.TempType = IntValue;
  else if (strcmp(PostTag[1], "MasterSlave") == 0)        Settings.MasterSlave = IntValue;
  else if (strcmp(PostTag[1], "MasterIP") == 0)           StringCopy(Settings.MasterIP, PostValue[1], IP_ADD_LEN+1);
  else if (strcmp(PostTag[1], "HomePage") == 0)           StringCopy(Settings.HomePage, PostValue[1], HOMEPAGE_LEN);
  else if (strcmp(PostTag[1], "SystemMode") == 0)         Settings.SystemMode = IntValue;
  else if (strcmp(PostTag[1], "MultiviewSessions") == 0)  Settings.MultiviewSessions = IntValue;
  else if (strcmp(PostTag[1], "PublicIP") == 0)           StringCopy(Settings.PublicIP, PostValue[1], IP_ADD_LEN+1);
  else if (strcmp(PostTag[1], "Language") == 0)           Settings.Language = IntValue;
  else if (strcmp(PostTag[1], "LoginPw") == 0)            StringCopy(Settings.LoginPw, PostValue[1], PASSWORD_LEN+1);
  else if (strcmp(PostTag[1], "LocalLogin") == 0)         Settings.LocalLogin = IntValue;
  else if (strcmp(PostTag[1], "Animations") == 0)         Settings.Animations = IntValue;

  // password page
  else if (strcmp(PostTag[1], "User") == 0)
  {
    if      (strcmp(PostTag[2], "ID") == 0)               StringCopy(Settings.User[IntValue].ID, PostValue[2], PASSWORD_LEN+1);
    else if (strcmp(PostTag[2], "Password") == 0)         StringCopy(Settings.User[IntValue].Password, PostValue[2], PASSWORD_LEN+1);
    else
    {
      debug_printf("Settings Restore - Tag:%s - Bad SubTag:%s\r\n", PostTag[1], PostTag[2]);
    }
  }

  // analog board setup page
  else if (strcmp(PostTag[1], "AnalogBoard") == 0)
  {
    int board = IntValue;   // see function comment
    IntValue = atoi(PostValue[2]);

    if      (strcmp(PostTag[2], "Address") == 0)          Settings.AnalogBoard[board].Address = IntValue;
    else if (strcmp(PostTag[2], "Type") == 0)             Settings.AnalogBoard[board].Type = IntValue;
    else if (strcmp(PostTag[2], "Label") == 0)            StringCopy(Settings.AnalogBoard[board].Label, PostValue[2], BOARD_LABELS+1);
    else if (strcmp(PostTag[2], "Disabled") == 0)         Settings.AnalogBoard[board].Disabled = IntValue;
    else
    {
      debug_printf("Settings Restore - Tag:%s - Bad SubTag:%s\r\n", PostTag[1], PostTag[2]);
    }
  }
  else if (strcmp(PostTag[1], "AnalogSensor") == 0 && strcmp(PostTag[2], "Board") == 0)
  {
    int board = atoi(PostValue[2]);   // see function comment
    int sensor = IntValue;
    IntValue = atoi(PostValue[3]);

    if      (strcmp(PostTag[3], "Type") == 0)             Settings.AnalogBoard[board].Sensor[sensor].Type = IntValue;
    else if (strcmp(PostTag[3], "Label") == 0)            StringCopy(Settings.AnalogBoard[board].Sensor[sensor].Label, PostValue[3], BOARD_LABELS+1);
    else if (strcmp(PostTag[3], "Offset") == 0)           Settings.AnalogBoard[board].Sensor[sensor].Offset = atof(PostValue[3]);
    else if (strcmp(PostTag[3], "Disabled") == 0)         Settings.AnalogBoard[board].Sensor[sensor].Disabled = IntValue;
    else
    {
      debug_printf("Settings Restore - Tag:%s - Bad SubTag:%s\r\n", PostTag[1], PostTag[3]);
    }
  }

  // fresh air door setup page
  else if (strcmp(PostTag[1], "DoorP") == 0)              Settings.Door.PID.P = IntValue;
  else if (strcmp(PostTag[1], "DoorI") == 0)              Settings.Door.PID.I = IntValue;
  else if (strcmp(PostTag[1], "DoorD") == 0)              Settings.Door.PID.D = IntValue;
  else if (strcmp(PostTag[1], "DoorU") == 0)              Settings.Door.PID.U = IntValue;
  else if (strcmp(PostTag[1], "DoorActuatorTime") == 0)   Settings.Door.ActuatorTime = IntValue;
  else if (strcmp(PostTag[1], "CoolAirCycle") == 0)       Settings.Door.CoolAirCycle = IntValue;

  // refrigeration setup page
  else if (strcmp(PostTag[1], "RefrP") == 0)              Settings.Refrig.PID.P = IntValue;
  else if (strcmp(PostTag[1], "RefrI") == 0)              Settings.Refrig.PID.I = IntValue;
  else if (strcmp(PostTag[1], "RefrD") == 0)              Settings.Refrig.PID.D = IntValue;
  else if (strcmp(PostTag[1], "RefrU") == 0)              Settings.Refrig.PID.U = IntValue;
  else if (strcmp(PostTag[1], "RefrPurge") == 0)          Settings.Refrig.Purge = IntValue;
  else if (strcmp(PostTag[1], "Refr") == 0)
  {
    int stage = IntValue;   // see function comment
    IntValue = atoi(PostValue[2]);

    if      (strcmp(PostTag[2], "On") == 0)               Settings.Refrig.Stage[stage].On = IntValue;
    else if (strcmp(PostTag[2], "Off") == 0)              Settings.Refrig.Stage[stage].Off = IntValue;
    else
    {
      debug_printf("Settings Restore - Tag:%s - Bad SubTag:%s\r\n", PostTag[1], PostTag[2]);
    }
  }

  // climacell setup page
  else if (strcmp(PostTag[1], "ClimP") == 0)              Settings.Climacell.PID.P = IntValue;
  else if (strcmp(PostTag[1], "ClimI") == 0)              Settings.Climacell.PID.I = IntValue;
  else if (strcmp(PostTag[1], "ClimD") == 0)              Settings.Climacell.PID.D = IntValue;
  else if (strcmp(PostTag[1], "ClimU") == 0)              Settings.Climacell.PID.U = IntValue;
  else if (strcmp(PostTag[1], "ClimacellEff") == 0)       Settings.Climacell.Efficiency = IntValue;
  else if (strcmp(PostTag[1], "Altitude") == 0)           Settings.Climacell.Altitude = IntValue;
  else if (strcmp(PostTag[1], "AltitudeType") == 0)       Settings.Climacell.AltitudeUnits = IntValue;

  // burner setup page
  else if (strcmp(PostTag[1], "BurnerP") == 0)            Settings.Burner.PID.P = IntValue;
  else if (strcmp(PostTag[1], "BurnerI") == 0)            Settings.Burner.PID.I = IntValue;
  else if (strcmp(PostTag[1], "BurnerD") == 0)            Settings.Burner.PID.D = IntValue;
  else if (strcmp(PostTag[1], "BurnerU") == 0)            Settings.Burner.PID.U = IntValue;
  else if (strcmp(PostTag[1], "BurnerMode") == 0)         Settings.Burner.Mode = IntValue;
  else if (strcmp(PostTag[1], "BurnerOn") == 0)           Settings.Burner.On = IntValue;
  else if (strcmp(PostTag[1], "BurnerLow") == 0)          Settings.Burner.Low = IntValue;
  else if (strcmp(PostTag[1], "BurnerManual") == 0)       Settings.Burner.Manual = IntValue;

  // failures 1 setup page
  else if (strcmp(PostTag[1], "FanFailMode") == 0)        Settings.Failure[FAIL_FAN].Mode = IntValue;
  else if (strcmp(PostTag[1], "FanFailTimer") == 0)       Settings.Failure[FAIL_FAN].Timer = IntValue;
  else if (strcmp(PostTag[1], "ClimacellFailMode") == 0)  Settings.Failure[FAIL_CLIMACELL].Mode = IntValue;
  else if (strcmp(PostTag[1], "ClimacellFailTimer") == 0) Settings.Failure[FAIL_CLIMACELL].Timer = IntValue;
  else if (strcmp(PostTag[1], "RefrFailMode") == 0)       Settings.Failure[FAIL_REFRIGERATION].Mode = IntValue;
  else if (strcmp(PostTag[1], "RefrFailTimer") == 0)      Settings.Failure[FAIL_REFRIGERATION].Timer = IntValue;
  else if (strcmp(PostTag[1], "RefrRunFailMode") == 0)    Settings.Refrig.FailMode = IntValue;
  else if (strcmp(PostTag[1], "HumidFailMode") == 0)      Settings.Failure[FAIL_HUMIDIFIERS].Mode = IntValue;
  else if (strcmp(PostTag[1], "HumidFailTimer") == 0)     Settings.Failure[FAIL_HUMIDIFIERS].Timer = IntValue;
  else if (strcmp(PostTag[1], "AuxFailMode") == 0)        Settings.Failure[FAIL_AUXILIARY].Mode = IntValue;
  else if (strcmp(PostTag[1], "AuxFailTimer") == 0)       Settings.Failure[FAIL_AUXILIARY].Timer = IntValue;
  else if (strcmp(PostTag[1], "HeatFailMode") == 0)       Settings.Failure[FAIL_HEAT].Mode = IntValue;
  else if (strcmp(PostTag[1], "HeatFailTimer") == 0)      Settings.Failure[FAIL_HEAT].Timer = IntValue;
  else if (strcmp(PostTag[1], "CavHeatFailMode") == 0)    Settings.Failure[FAIL_CAVITY_HEAT].Mode = IntValue;
  else if (strcmp(PostTag[1], "CavHeatFailTimer") == 0)   Settings.Failure[FAIL_CAVITY_HEAT].Timer = IntValue;
  else if (strcmp(PostTag[1], "BurnerFailMode") == 0)     Settings.Failure[FAIL_BURNER].Mode = IntValue;
  else if (strcmp(PostTag[1], "BurnerFailTimer") == 0)    Settings.Failure[FAIL_BURNER].Timer = IntValue;
  else if (strcmp(PostTag[1], "LightsFailMode") == 0)     Settings.Failure[FAIL_LIGHTS].Mode = IntValue;
  else if (strcmp(PostTag[1], "LightsFailTimer") == 0)    Settings.Failure[FAIL_LIGHTS].Timer = IntValue;
  else if (strcmp(PostTag[1], "LightsFailUnits") == 0)    Settings.LightsFailUnits = IntValue;

  // failures 2 setup page
  else if (strcmp(PostTag[1], "PlenSenFailMode") == 0)    Settings.Failure[FAIL_PLENUM_SENSOR].Mode = IntValue;
  else if (strcmp(PostTag[1], "PlenSenFailTimer") == 0)   Settings.Failure[FAIL_PLENUM_SENSOR].Timer = IntValue;
  else if (strcmp(PostTag[1], "PlenSenDiff") == 0)        Settings.Plenum.SensorDiff = FloatValue;
  else if (strcmp(PostTag[1], "OutAirFailMode") == 0)     Settings.Failure[FAIL_OUTSIDE_AIR].Mode = IntValue;
  else if (strcmp(PostTag[1], "OutAirFailTimer") == 0)    Settings.Failure[FAIL_OUTSIDE_AIR].Timer = IntValue;
  else if (strcmp(PostTag[1], "OutHumFailMode") == 0)     Settings.Failure[FAIL_OUTSIDE_HUMIDITY].Mode = IntValue;
  else if (strcmp(PostTag[1], "OutHumFailTimer") == 0)    Settings.Failure[FAIL_OUTSIDE_HUMIDITY].Timer = IntValue;
  else if (strcmp(PostTag[1], "PlenHumFailMode") == 0)    Settings.Failure[FAIL_PLENUM_HUMIDITY].Mode = IntValue;
  else if (strcmp(PostTag[1], "PlenHumFailTimer") == 0)   Settings.Failure[FAIL_PLENUM_HUMIDITY].Timer = IntValue;
  else if (strcmp(PostTag[1], "LowHumSet") == 0)          Settings.Plenum.HumidLowFailure = IntValue;
  else if (strcmp(PostTag[1], "HighCo2FailMode") == 0)    Settings.Failure[FAIL_HIGH_CO2].Mode = IntValue;
  else if (strcmp(PostTag[1], "HighCo2FailTimer") == 0)   Settings.Failure[FAIL_HIGH_CO2].Timer = IntValue;
  else if (strcmp(PostTag[1], "HighCo2Set") == 0)         Settings.Co2.HighFailure = IntValue;

  // controller access page
  else if (strcmp(PostTag[1], "Node") == 0)
  {
    if      (strcmp(PostTag[2], "ID") == 0)               StringCopy(Settings.Node[IntValue].ID, PostValue[2], SITE_ID_LEN+1);
    else if (strcmp(PostTag[2], "IP") == 0)               StringCopy(Settings.Node[IntValue].IP, PostValue[2], IP_ADD_LEN+1);
    else if (strcmp(PostTag[2], "PublicIP") == 0)         StringCopy(Settings.Node[IntValue].PublicIP, PostValue[2], IP_ADD_LEN+1);
    else if (strcmp(PostTag[2], "HttpPort") == 0)         Settings.Node[IntValue].HttpPort = atoi(PostValue[2]);
    else
    {
      debug_printf("Settings Restore - Tag:%s - Bad SubTag:%s\r\n", PostTag[1], PostTag[2]);
    }
  }

  // service info page
  else if (strcmp(PostTag[1], "DealerName") == 0)         StringCopy(Settings.DealerName, PostValue[1], NAME_LEN+1);
  else if (strcmp(PostTag[1], "DealerPhone") == 0)        StringCopy(Settings.DealerPhone, PostValue[1], PHONE_LEN+1);
  else if (strcmp(PostTag[1], "TechName") == 0)           StringCopy(Settings.TechName, PostValue[1], NAME_LEN+1);
  else if (strcmp(PostTag[1], "TechPhone") == 0)          StringCopy(Settings.TechPhone, PostValue[1], PHONE_LEN+1);

  // loading monitor setup page
  else if (strcmp(PostTag[1], "LoadMonitor") == 0)
  {
    int bay = atoi(PostValue[2]);   // see function comment

    if (strcmp(PostTag[2], "Bay") == 0)
    {
      if (strcmp(PostTag[3], "Label") == 0)
        StringCopy(Settings.LoadMonitor.Bay[bay].Label, PostValue[3], BOARD_LABELS+1);
      else if (strcmp(PostTag[3], "SensorID") == 0)
        Settings.LoadMonitor.Bay[bay].SensorID = atoi(PostValue[3]);
      else
      {
        debug_printf("Settings Restore - Tag:%s - Bad SubTag:%s\r\n", PostTag[1], PostTag[2]);
      }
    }
    else if (strcmp(PostTag[2], "AlarmTemp") == 0)
      Settings.LoadMonitor.AlarmTemp[bay] = atof(PostValue[3]);
    else if (strcmp(PostTag[2], "IrSensorsAvailable") == 0)
      Settings.LoadMonitor.IrSensorsAvailable[bay] = atof(PostValue[3]);
  }

  // plenum setup page
  else if (strcmp(PostTag[1], "PlenTempSet") == 0)        Settings.Plenum.TempSet = FloatValue;
  else if (strcmp(PostTag[1], "PlenHumSet") == 0)         Settings.Plenum.HumidSet = IntValue;
  else if (strcmp(PostTag[1], "FanDailyDay") == 0)        Settings.Fan.DailyDay = IntValue;
  else if (strcmp(PostTag[1], "FanDailyRunTime") == 0)    Settings.Fan.DailyRunTime = IntValue;
  else if (strcmp(PostTag[1], "FanTotalRunTime") == 0)    Settings.Fan.TotalRunTime = IntValue;
  else if (strcmp(PostTag[1], "FanTotalRefrigTime") == 0) Settings.Fan.TotalRefrigTime = IntValue;
  else if (strcmp(PostTag[1], "FanTotalCoolingTime") == 0)Settings.Fan.TotalCoolingTime = IntValue;
  else if (strcmp(PostTag[1], "FanTotalRecircTime") == 0) Settings.Fan.TotalRecircTime = IntValue;
  else if (strcmp(PostTag[1], "FanTotalStandbyTime") == 0)Settings.Fan.TotalStandbyTime = IntValue;
  else if (strcmp(PostTag[1], "FanTotalCureTime") == 0)   Settings.Fan.TotalCureTime = IntValue;
  else if (strcmp(PostTag[1], "HumSetpointRef") == 0)     Settings.Plenum.HumidSetpointRef = IntValue;
  else if (strcmp(PostTag[1], "BurnerTempSet") == 0)      Settings.Burner.TempSet = FloatValue;
  else if (strcmp(PostTag[1], "BurnerThreshold") == 0)    Settings.Burner.Threshold = FloatValue;

  // outside air cooling control page
  else if (strcmp(PostTag[1], "OutAirDiff") == 0)         Settings.OutsideAir.Diff = FloatValue;
  else if (strcmp(PostTag[1], "OutAirCtrlMode") == 0)     Settings.OutsideAir.CtrlMode = IntValue;
  else if (strcmp(PostTag[1], "OutAirAboveBelow") == 0)   Settings.OutsideAir.AboveBelow = IntValue;
  else if (strcmp(PostTag[1], "OutAirTempRef") == 0)      Settings.OutsideAir.TempRef = IntValue;
  else if (strcmp(PostTag[1], "CalcHumMax") == 0)         Settings.OutsideAir.CalcHumidMax = IntValue;
  else if (strcmp(PostTag[1], "CureStartTemp") == 0)      Settings.Cure.StartTemp = FloatValue;
  else if (strcmp(PostTag[1], "CureStartHumid") == 0)     Settings.Cure.StartHumid = IntValue;
  else if (strcmp(PostTag[1], "CureHumidHighLimit") == 0) Settings.Cure.HumidHighLimit = IntValue;
  else if (strcmp(PostTag[1], "CureHumidRef") == 0)       Settings.Cure.HumidRef = IntValue;

  // plenum temp deviation alarm page
  else if (strcmp(PostTag[1], "PlenLowAlarmTemp") == 0)   Settings.Plenum.LowAlarmTemp = FloatValue;
  else if (strcmp(PostTag[1], "PlenLowAlarmTimer") == 0)  Settings.Plenum.LowAlarmTimer = IntValue;
  else if (strcmp(PostTag[1], "PlenHighAlarmTemp") == 0)  Settings.Plenum.HighAlarmTemp = FloatValue;
  else if (strcmp(PostTag[1], "PlenHighAlarmTimer") == 0) Settings.Plenum.HighAlarmTimer = IntValue;
  else if (strcmp(PostTag[1], "CureTempLowLimit") == 0)   Settings.Cure.TempLowLimit = FloatValue;
  else if (strcmp(PostTag[1], "CureTempHighLimit") == 0)  Settings.Cure.TempHighLimit = FloatValue;

  // system runtimes page
  else if (strcmp(PostTag[1], "RunTimes") == 0)
  {
    for (i = 0; i < 48; i++)
      Settings.RunTimes[i] = PostValue[1][i] - 48;    // convert the ascii character to an integer
  }
  else if (strcmp(PostTag[1], "CureRunTimes") == 0)
  {
    for (i = 0; i < 48; i++)
      Settings.Cure.RunTimes[i] = PostValue[1][i] - 48;
  }
  else if (strcmp(PostTag[1], "ClimacellTimes") == 0)
  {
    for (i = 0; i < 48; i++)
      Settings.Climacell.Times[i] = PostValue[1][i] - 48;
  }

  // fan speed control page
  else if (strcmp(PostTag[1], "MaxFanSpeed") == 0)        Settings.Fan.MaxSpeed = IntValue;
  else if (strcmp(PostTag[1], "MinFanSpeed") == 0)        Settings.Fan.MinSpeed = IntValue;
  else if (strcmp(PostTag[1], "RefrFanSpeed") == 0)       Settings.Fan.RefrigSpeed = IntValue;
  else if (strcmp(PostTag[1], "RecircFanSpeed") == 0)     Settings.Fan.RecircSpeed = IntValue;
  else if (strcmp(PostTag[1], "UpdatePeriod") == 0)       Settings.Fan.UpdatePeriod = IntValue;
  else if (strcmp(PostTag[1], "TempDiff") == 0)           Settings.Fan.TempDiff = FloatValue;
  else if (strcmp(PostTag[1], "TempRef1") == 0)           Settings.Fan.TempRef1 = IntValue;
  else if (strcmp(PostTag[1], "TempRef2") == 0)           Settings.Fan.TempRef2 = IntValue;
  else if (strcmp(PostTag[1], "FanUpdMode") == 0)         Settings.Fan.UpdateMode = IntValue;
  else if (strcmp(PostTag[1], "PrevFanSpeed") == 0)       Settings.Fan.PrevSpeed = IntValue;

  // ramp rate page
  else if (strcmp(PostTag[1], "RampUpdateTemp") == 0)     Settings.Ramp.UpdateTemp = FloatValue;
  else if (strcmp(PostTag[1], "RampUpdatePeriod") == 0)   Settings.Ramp.UpdatePeriod = IntValue;
  else if (strcmp(PostTag[1], "RampTempDiff") == 0)       Settings.Ramp.TempDiff = FloatValue;
  else if (strcmp(PostTag[1], "RampTempRef") == 0)        Settings.Ramp.TempRef = IntValue;
  else if (strcmp(PostTag[1], "RampTargetTemp") == 0)     Settings.Ramp.TargetTemp = FloatValue;

  // humidity control page
  else if (strcmp(PostTag[1], "HumidCtrl") == 0 && IntValue < NUM_HUMIDIFIERS)
  {
    int type = IntValue;    // see function comment

    if (strcmp(PostTag[2], "Mode") == 0)                  Settings.HumidCtrl[type].Mode = atoi(PostValue[2]);
    else
    {
      debug_printf("Settings Restore - Tag:%s - Bad SubTag:%s\r\n", PostTag[1], PostTag[2]);
    }
  }
  else if (strcmp(PostTag[1], "DutyCycle") == 0 && strcmp(PostTag[2], "HumidCtrl") == 0)
  {
    int type = atoi(PostValue[2]);    // see function comment
    int cycle = IntValue;
    IntValue = atoi(PostValue[3]);

    if      (strcmp(PostTag[3], "On") == 0)               Settings.HumidCtrl[type].DutyCycle[cycle].On = IntValue;
    else if (strcmp(PostTag[3], "Off") == 0)              Settings.HumidCtrl[type].DutyCycle[cycle].Off = IntValue;
    else
    {
      debug_printf("Settings Restore - Tag:%s - Bad SubTag:%s\r\n", PostTag[1], PostTag[3]);
    }
  }

  // high CO2 level purge page
  else if (strcmp(PostTag[1], "Co2PurgeMode") == 0)       Settings.Co2.Purge.Mode = IntValue;
  else if (strcmp(PostTag[1], "Co2Set") == 0)             Settings.Co2.Set = IntValue;
  else if (strcmp(PostTag[1], "Co2CylceTime") == 0)       Settings.Co2.CylceTime = IntValue;
  else if (strcmp(PostTag[1], "Co2PurgeMaxTemp") == 0)    Settings.Co2.Purge.MaxTemp = FloatValue;
  else if (strcmp(PostTag[1], "Co2PurgeMinTemp") == 0)    Settings.Co2.Purge.MinTemp = FloatValue;
  else if (strcmp(PostTag[1], "Co2PurgeDur") == 0)        Settings.Co2.Purge.Duration = IntValue;
  else if (strcmp(PostTag[1], "Co2PurgeLast") == 0)       Settings.Co2.Purge.Last = IntValue;
  else if (strcmp(PostTag[1], "Co2PurgeStart") == 0)      Settings.Co2.Purge.Start = IntValue;
  else if (strcmp(PostTag[1], "Co2FanOutput") == 0)       Settings.Co2.FanOutput = IntValue;
  else if (strcmp(PostTag[1], "Co2DoorOutput") == 0)      Settings.Co2.DoorOutput = IntValue;

  // miscellaneous parameters page
  else if (strcmp(PostTag[1], "RefrMode") == 0)           Settings.Refrig.Mode = IntValue;
  else if (strcmp(PostTag[1], "DefrostInt") == 0)         Settings.Refrig.DefrostPeriod = IntValue;
  else if (strcmp(PostTag[1], "DefrostDur") == 0)         Settings.Refrig.DefrostDuration = IntValue;
  else if (strcmp(PostTag[1], "HeatTempThresh") == 0)     Settings.HeatTempThresh = FloatValue;
  else if (strcmp(PostTag[1], "RefrigLimit") == 0)        Settings.Refrig.Limit = FloatValue;
  else if (strcmp(PostTag[1], "CavityOrPileCtrl") == 0)   Settings.CavityHeat.Label= IntValue;
  else if (strcmp(PostTag[1], "CavHeat") == 0)            Settings.CavityHeat.Mode = IntValue;
  else if (strcmp(PostTag[1], "CavDiff") == 0)            Settings.CavityHeat.Diff = FloatValue;
  else if (strcmp(PostTag[1], "CavDutyCycle") == 0)       Settings.CavityHeat.DutyCycle = IntValue;
  else if (strcmp(PostTag[1], "KbPref") == 0)             Settings.KbPref = IntValue;

  // email page
  else if (strcmp(PostTag[1], "EmailAlerts") == 0)        Settings.Email.Alerts = IntValue;
  else if (strcmp(PostTag[1], "EmailTo") == 0)            StringCopy(Settings.Email.To, PostValue[1], EMAIL_TO_LEN+1);
  else if (strcmp(PostTag[1], "EmailFrom") == 0)          StringCopy(Settings.Email.From, PostValue[1], EMAIL_FROM_LEN+1);
  else if (strcmp(PostTag[1], "EmailServer") == 0)        StringCopy(Settings.Email.Server, PostValue[1], EMAIL_FROM_LEN+1);
  else if (strcmp(PostTag[1], "EmailAuthType") == 0)      Settings.Email.AuthType = IntValue;
  else if (strcmp(PostTag[1], "EmailPort") == 0)          Settings.Email.Port = IntValue;
  else if (strcmp(PostTag[1], "EmailAccount") == 0)       StringCopy(Settings.Email.Account, PostValue[1], EMAIL_FROM_LEN+1);
  else if (strcmp(PostTag[1], "EmailPassword") == 0)      StringCopy(Settings.Email.Password, PostValue[1], EMAIL_FROM_LEN+1);
  else if (strcmp(PostTag[1], "EmailDisplay") == 0)       StringCopy(Settings.Email.Display, PostValue[1], IP_ADD_LEN+1+SITE_ID_LEN+1);

  // email alerts setup page
  else if (strcmp(PostTag[1], "AlertsToSend") == 0)       StringCopy(Settings.AlertsToSend, PostValue[1], NUM_WARNINGS+1);

  // software/remote off
  else if (strcmp(PostTag[1], "FanRemoteOff") == 0)       Settings.RemoteOff[RO_FAN] = IntValue;   // to allow putting the system in remote off for restore operation
//  else if (strcmp(PostTag[1], "AuxRemoteOff") == 0)       Settings.AuxRemoteOff = IntValue;
//  else if (strcmp(PostTag[1], "CavRemoteOff") == 0)       Settings.CavRemoteOff = IntValue;
//  else if (strcmp(PostTag[1], "ClimacellRemoteOff") == 0) Settings.ClimacellRemoteOff = IntValue;
//  else if (strcmp(PostTag[1], "HeatRemoteOff") == 0)      Settings.HeatRemoteOff = IntValue;
//  else if (strcmp(PostTag[1], "Hum1RemoteOff") == 0)      Settings.Hum1RemoteOff = IntValue;
//  else if (strcmp(PostTag[1], "Hum2RemoteOff") == 0)      Settings.Hum2RemoteOff = IntValue;
//  else if (strcmp(PostTag[1], "Hum3RemoteOff") == 0)      Settings.Hum3RemoteOff = IntValue;
//  else if (strcmp(PostTag[1], "RefrRemoteOff") == 0)      Settings.RefrRemoteOff = IntValue;
//  else if (strcmp(PostTag[1], "Aux2RemoteOff") == 0)      Settings.Aux2RemoteOff = IntValue;
//  else if (strcmp(PostTag[1], "BurnerRemoteOff") == 0)    Settings.BurnerRemoteOff = IntValue;
//  else if (strcmp(PostTag[1], "CureRemoteOff") == 0)      Settings.CureRemoteOff = IntValue;
//  else if (strcmp(PostTag[1], "Lights1RemoteOff") == 0)   Settings.Lights1RemoteOff = IntValue;
//  else if (strcmp(PostTag[1], "Lights2RemoteOff") == 0)   Settings.Lights2RemoteOff = IntValue;

  // fan boost page
  else if (strcmp(PostTag[1], "FanBoostMode") == 0)       Settings.FanBoost.Mode = IntValue;
  else if (strcmp(PostTag[1], "FanBoostSpeed") == 0)      Settings.FanBoost.Speed = IntValue;
  else if (strcmp(PostTag[1], "FanBoostInterval") == 0)   Settings.FanBoost.Interval = IntValue;
  else if (strcmp(PostTag[1], "FanBoostDur") == 0)        Settings.FanBoost.Duration = IntValue;
  else if (strcmp(PostTag[1], "FanBoostTemp") == 0)       Settings.FanBoost.Temp = FloatValue;

  // PWM setup
  else if (strcmp(PostTag[1], "PWM_doors") == 0)
  {
    Settings.PWM[PWM_DOORS].Channel = IntValue;

    if (Settings.PWM[PWM_DOORS].Channel != PWM_UNDEFINED)
    {
      Settings.PWM[PWM_DOORS].Enabled = 1;
    }
    else
    {
      Settings.PWM[PWM_DOORS].Enabled = 0;
    }
  }
  else if (strcmp(PostTag[1], "PWM_refrig") == 0)
  {
    Settings.PWM[PWM_REFRIGERATION].Channel = IntValue;

    if (Settings.PWM[PWM_REFRIGERATION].Channel != PWM_UNDEFINED)
    {
      Settings.PWM[PWM_REFRIGERATION].Enabled = 1;
    }
    else
    {
      Settings.PWM[PWM_REFRIGERATION].Enabled = 0;
    }
  }
  else if (strcmp(PostTag[1], "PWM_fan") == 0)
  {
    Settings.PWM[PWM_FAN].Channel = IntValue;

    if (Settings.PWM[PWM_FAN].Channel != PWM_UNDEFINED)
    {
      Settings.PWM[PWM_FAN].Enabled = 1;
    }
    else
    {
      Settings.PWM[PWM_FAN].Enabled = 0;
    }
  }
  else if (strcmp(PostTag[1], "PWM_burner") == 0)
  {
    Settings.PWM[PWM_BURNER].Channel = IntValue;

    if (Settings.PWM[PWM_BURNER].Channel != PWM_UNDEFINED)
    {
      Settings.PWM[PWM_BURNER].Enabled = 1;
    }
    else
    {
      Settings.PWM[PWM_BURNER].Enabled = 0;
    }
  }

  // equipment IO setup
  else if (strcmp(PostTag[1], "EquipIo") == 0)
  {
    int io = IntValue;   // see function comment
    IntValue = atoi(PostValue[2]);

    if (strcmp(PostTag[2], "Enabled") == 0)               Settings.EquipIo[io].Enabled = IntValue;
    else if (strcmp(PostTag[2], "Renamable") == 0)        Settings.EquipIo[io].Renamable = IntValue;
    else if (strcmp(PostTag[2], "Name") == 0)             StringCopy(Settings.EquipIo[io].Name, PostValue[2], LOG_LABELS);
    else if (strcmp(PostTag[2], "Mode") == 0)             Settings.EquipIo[io].Mode = (SYSTEM_MODE) IntValue;
    else if (strcmp(PostTag[2], "IO") == 0)               Settings.EquipIo[io].IO = (IO_OPTION) IntValue;
    else if (strcmp(PostTag[2], "Input") == 0)            Settings.EquipIo[io].Input = IntValue;
    else if (strcmp(PostTag[2], "Output") == 0)           Settings.EquipIo[io].Output = IntValue;
    else
    {
      debug_printf("Settings Restore - Tag:%s - Bad SubTag:%s\r\n", PostTag[1], PostTag[2]);
    }
  }

  else if (strcmp(PostTag[1], "AuxProgram") == 0)
  {
    int aux = IntValue;   // see function comment
    IntValue = atoi(PostValue[2]);

    if      (strcmp(PostTag[2], "RuleStatus") == 0)       Settings.AuxProgram[aux].RuleStatus = IntValue;
    else if (strcmp(PostTag[2], "OutputState") == 0)      Settings.AuxProgram[aux].OutputState = IntValue;
    else if (strcmp(PostTag[2], "DutyCycle") == 0)        Settings.AuxProgram[aux].DutyCycle = IntValue;
    else if (strcmp(PostTag[2], "Period") == 0)           Settings.AuxProgram[aux].Period = IntValue;
    else if (strcmp(PostTag[2], "Units") == 0)            Settings.AuxProgram[aux].Units = IntValue;
    else if (strcmp(PostTag[2], "Timer") == 0)            Settings.AuxProgram[aux].Timer = IntValue;
    else
    {
      debug_printf("Settings Restore - Tag:%s - Bad SubTag:%s\r\n", PostTag[1], PostTag[2]);
    }
  }
  else if (strcmp(PostTag[1], "ProgramRule") == 0 && strcmp(PostTag[2], "Rule") == 0)
  {
    int aux = IntValue;
    int rule = atoi(PostValue[2]);   // see function comment
    IntValue = atoi(PostValue[3]);

    if      (strcmp(PostTag[3], "Type") == 0)             Settings.AuxProgram[aux].Rule[rule].Type = (AUXPROG_RULE_TYPE) IntValue;
    else if (strcmp(PostTag[3], "IoIndex") == 0)          Settings.AuxProgram[aux].Rule[rule].IoIndex = IntValue;
    else if (strcmp(PostTag[3], "State") == 0)            Settings.AuxProgram[aux].Rule[rule].State = IntValue;
    else if (strcmp(PostTag[3], "Op") == 0)               Settings.AuxProgram[aux].Rule[rule].Op = (AUXPROG_OPERATOR) IntValue;
    else if (strcmp(PostTag[3], "SensorValue") == 0)      Settings.AuxProgram[aux].Rule[rule].SensorValue = atof(PostValue[3]);
    else if (strcmp(PostTag[3], "AndOr") == 0)            Settings.AuxProgram[aux].Rule[rule].AndOr = (AUXPROG_AND_OR) IntValue;
    else if (strcmp(PostTag[3], "ReferenceIndex") == 0)   Settings.AuxProgram[aux].Rule[rule].ReferenceIndex = IntValue;
    else
    {
      debug_printf("Settings Restore - Tag:%s - Bad SubTag:%s\r\n", PostTag[1], PostTag[3]);
    }
  }
  else
  {
    debug_printf("Settings Restore - Bad Tag: %s\r\n", PostTag[1]);
  }
} // end StoreSettings()

/***************************************************************************

FUNCTION:   StoreSlaveUpdate()

PURPOSE:    Store the data broadcast by the master controller

COMMENTS:   In master/slave mode, the master sends out broadcasts to slaves
            with the outside temperature and humidity, and the data and time.

            If a system is configured as a slave and it doesn't receive a
            broadcast from a master within 10 minutes, it will report an error.

            It checks the broadcast time, and only sets its time if it's out of
            sync with the master.  This is done to keep from continually
            resetting the clocks on the slaves.

***************************************************************************/
void StoreSlaveUpdate(char *dateStr, char *timeStr, uint8_t amPm, char *tempStr, char *humidStr)
{
  static uint8_t Errors;
  uint8_t Status = 1;
  uint8_t AmPm;
  char DateStr[DATE_LEN+1];
  char TimeStr[TIME_LEN+1];
//  char debug[MSG_TX_BUFFER_SIZE];

//  if (strcmp(PostValue[3], "") == 0)
//  {
//    sprintf(debug,
//            "store - %s:%s %s:%s %s:%s %s:%s %s:%s",
//            PostTag[0], PostValue[0],
//            PostTag[1], PostValue[1],
//            PostTag[2], PostValue[2],
//            PostTag[3], PostValue[3],
//            PostTag[4], PostValue[4]);
//    UI_SendDebug(debug);
//  }

//  char AmPmStr[5];
//  char str[50];

  // get the current date & time (don't fail for bad values, they should get reset)
  GetDateStr(DateStr);
  GetTimeStr(TimeStr, &AmPm);

  // compare the date & time and set RTC if necessary
  if (   strncmp(DateStr, dateStr, DATE_LEN) != 0
      || strncmp(TimeStr, timeStr, strlen("HH:MM")) != 0
      || AmPm != amPm)
  {
    // SetDateTime() will check for valid values before setting date/time
    Status = SetDateTimeStr(dateStr, timeStr, amPm);
    if (Status == 1)    // success
    {
      Errors = 0;
      UI_SendDateTime(MSG_QUEUE);
    }
    else if (Status == 0)   // format error
    {
      Errors++;
      if (Errors > 10)
      {
        WarningsSet(WARN_INVALIDDATETIME, FM_ALARM, FM_ALARM, NA);
        Errors = 0;
      }
    }
  }
  else
  {
    // time was good so clear the error count
    Errors = 0;
  }

//  if (   SystemState != ST_COOLING
//      && SystemState != ST_COOLDEHUMID
//      && atof(PostValue[3]) <= StartTemp)
//  {
//    sprintf(debug, "StoreSlaveUpdate -- StartTemp-%.1f MasterTemp-%s MasterHumid-%s", StartTemp, PostValue[3], PostValue[4]);
//    UI_SendDebug(debug);
//  }

  // set the temperature & humidity
  if (strcmp(tempStr, "") != 0)
  {
    if (strcmp(tempStr, "--") != 0)
    {
      Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Sensor[SENSOR_OUTSIDE_TEMP].Value = atof(tempStr);
    }
    else
    {
      Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Sensor[SENSOR_OUTSIDE_TEMP].Value = SENSOR_VAL_UNDEFINED;
    }
  }
//  else
//  {
//    sprintf(debug, "MasterTemp-%s not used", PostValue[3]);
//    UI_SendDebug(debug);
//  }

  if (strcmp(humidStr, "") != 0)
  {
    if (strcmp(humidStr, "--") != 0)
    {
      Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_OUTSIDE_HUMID].Value = atof(humidStr);
    }
    else
    {
      Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_OUTSIDE_HUMID].Value = SENSOR_VAL_UNDEFINED;
    }
  }

  // set the mode back to slave
  if (Settings.MasterSlave == MSMODE_SLAVE_NOBROADCAST)
  {
    Settings.MasterSlave = MSMODE_SLAVE;
  }

  // clear the alarms
  SystemAlarm[AL_NOBROADCAST] = FM_NONE;
  AlarmTimer[AL_NOBROADCAST] = XTimerVal;

//  if (AmPm == 0)
//    strcpy(AmPmStr, "am");
//  else
//    strcpy(AmPmStr, "pm");
//
//  sprintf(str,"%s %s\n\r",TimeStr,AmPmStr);
//  DBGU_SendString(str);
//  DBGU_SendPacket("StoreSlaveUpdate XTimer:", sizeof("StoreSlaveUpdate XTimer:"), 0);
//  DBGU_SendInt(XTimerVal);
} // end StoreSlaveUpdate()

/***************************************************************************

FUNCTION:   StoreTempAlarms()

PURPOSE:    Store temperature deviation alarm settings

COMMENTS:

***************************************************************************/
void StoreTempAlarms(void)
{
  Settings.Plenum.LowAlarmTemp = atof(PostValue[0]);
  Settings.Plenum.LowAlarmTimer = atoi(PostValue[1]);
  Settings.Plenum.HighAlarmTemp = atof(PostValue[2]);
  Settings.Plenum.HighAlarmTimer = atoi(PostValue[3]);
} // end StoreTempAlarms()

/***************************************************************************

FUNCTION:   StoreUserLog()

PURPOSE:    Store the user log settings

COMMENTS:

***************************************************************************/
void StoreUserLog(void)
{
  Settings.Log.User.Interval = atoi(PostValue[0]);

  Settings.Log.User.Wrap = atoi(PostValue[1]);
  if (Settings.Log.User.Wrap == 1)
    SDCardHeader.CardFull = 0;
} // end StoreUserLog()

/***************************************************************************

FUNCTION:   UI_PostMsg_Init()

PURPOSE:    Set up the valid message tags and their appropriate store functions

COMMENTS:   ActionPosts don't have any post data to store.  They are typically
            just buttons on the UI pages like save, set, clear, etc.  The ARM
            performs some action in response to these post items - see function
            DataExc/ExecuteActionPosts().

            ProgramPosts are the typical posts from the program level 1 & 2 pages.
            The function stored in the StoreData element is called in StorePostData()
            to properly store the data sent in the post.  The function stored in the
            Reply element is called to build and send the post reply message back
            to the LTX.

            EuipmentPosts are all from buttons on the euipment status program
            level 2 page.  They call a StoreData function, but also pass a location
            to the StoreData function to store the post data in.

***************************************************************************/
void UI_PostMsg_Init(void)
{
  // action posts
  strcpy(UI_ActionPosts[TAG_A_SETDEFAULT].Tag,          "SetDefault");
  strcpy(UI_ActionPosts[TAG_A_PANELDEFAULT].Tag,        "PanelDefault");
  strcpy(UI_ActionPosts[TAG_A_FACTORYDEFAULT].Tag,      "FactoryDefault");
  strcpy(UI_ActionPosts[TAG_A_CLEARALARM].Tag,          "ClearAlarm");
  strcpy(UI_ActionPosts[TAG_A_USERLOGGRAPH].Tag,        "GraphQuery");
  strcpy(UI_ActionPosts[TAG_A_USERLOGTABLE].Tag,        "RecordsQuery");
  strcpy(UI_ActionPosts[TAG_A_USERLOGDOWNLOAD].Tag,     "SendUserLogUSB");
  strcpy(UI_ActionPosts[TAG_A_USERLOGCLEAR].Tag,        "ClearUserLog");
  strcpy(UI_ActionPosts[TAG_A_SDCARDINIT].Tag,          "SdCardInit");
  strcpy(UI_ActionPosts[TAG_A_WARNINGLOGSHOW].Tag,      "NumWarnings");
  strcpy(UI_ActionPosts[TAG_A_SYSTEMLOGSHOW].Tag,       "SysLogViewStart");
  strcpy(UI_ActionPosts[TAG_A_SYSTEMLOGDOWNLOAD].Tag,   "SysLogUSBStart");
  strcpy(UI_ActionPosts[TAG_A_SYSTEMLOGCLEAR].Tag,      "ClearSystemLog");
  strcpy(UI_ActionPosts[TAG_A_NEXTAUXPROGRAM].Tag,      "NextAux");
  strcpy(UI_ActionPosts[TAG_A_PREVAUXPROGRAM].Tag,      "PrevAux");
  strcpy(UI_ActionPosts[TAG_A_NEXTBOARD].Tag,           "NextBoard");
  strcpy(UI_ActionPosts[TAG_A_PREVBOARD].Tag,           "PrevBoard");
  strcpy(UI_ActionPosts[TAG_A_SAMEBOARD].Tag,           "SameBoard");
  strcpy(UI_ActionPosts[TAG_A_FINDBOARD].Tag,           "FindBoard");
  strcpy(UI_ActionPosts[TAG_A_BOOTLOADER].Tag,          "BOOTLOADER");
  strcpy(UI_ActionPosts[TAG_A_CLEARDIAG].Tag,           "ClearDiag");
  strcpy(UI_ActionPosts[TAG_A_PIDLOGDOWNLOAD].Tag,      "PIDLogUSBStart");
  strcpy(UI_ActionPosts[TAG_A_PIDLOGCLEAR].Tag,         "PIDClearLog");
  strcpy(UI_ActionPosts[TAG_A_PIDLOGSHOW].Tag,          "PIDLogViewStart");
  strcpy(UI_ActionPosts[TAG_A_SETTINGSDOWNLOAD].Tag,    "downloadSettings");
  strcpy(UI_ActionPosts[TAG_A_SLAVEUPD].Tag,            "SlaveUpdate");
  strcpy(UI_ActionPosts[TAG_A_SHOWPASSWORD].Tag,        "ShowPassword");
  strcpy(UI_ActionPosts[TAG_A_LOADLOGGRAPH].Tag,        "LoadLogStartDate");
  strcpy(UI_ActionPosts[TAG_A_LOADLOGTABLE].Tag,        "LoadLogTableView");
  strcpy(UI_ActionPosts[TAG_A_LOADLOGDOWNLOAD].Tag,     "SendLoadLogUSB");
  strcpy(UI_ActionPosts[TAG_A_LOADLOGCLEAR].Tag,        "ClearLoadLog");
  strcpy(UI_ActionPosts[TAG_A_LOADLOGSTART].Tag,        "StartLoadLog");
  strcpy(UI_ActionPosts[TAG_A_LOADLOGCLEARSENSORS].Tag, "ClearIrSensors");
  strcpy(UI_ActionPosts[TAG_A_RESETIOCONFIG].Tag,       "resetIoConfig");
  strcpy(UI_ActionPosts[TAG_A_RESETPWMCONFIG].Tag,      "resetPwmConfig");
  strcpy(UI_ActionPosts[TAG_A_FILESTART].Tag,           "fileStart");
  strcpy(UI_ActionPosts[TAG_A_NODEDELETEALL].Tag,       "deleteNodes");

  // program level 1 pages
  strcpy(UI_ProgramPosts[TAG_P1_PLENSETUP].Tag,       "p1Plenum");            UI_ProgramPosts[TAG_P1_PLENSETUP].StoreData       = StorePlenumSetup;       UI_ProgramPosts[TAG_P1_PLENSETUP].Reply       = UI_SendPlenSetPoints;
  strcpy(UI_ProgramPosts[TAG_P1_FANDAILY].Tag,        "DailyFanRuntime");     UI_ProgramPosts[TAG_P1_FANDAILY].StoreData        = StoreFanDailyRun;       UI_ProgramPosts[TAG_P1_FANDAILY].Reply        = UI_SendFanDailyRun;
  strcpy(UI_ProgramPosts[TAG_P1_FANTOTAL].Tag,        "TotalFanRuntime");     UI_ProgramPosts[TAG_P1_FANTOTAL].StoreData        = StoreFanTotalRun;       UI_ProgramPosts[TAG_P1_FANTOTAL].Reply        = UI_SendFanTotalRun;
  strcpy(UI_ProgramPosts[TAG_P1_OUTSIDEAIR].Tag,      "ctrlMode");            UI_ProgramPosts[TAG_P1_OUTSIDEAIR].StoreData      = StoreOutsideAir;        UI_ProgramPosts[TAG_P1_OUTSIDEAIR].Reply      = UI_SendOutsideAir;
  strcpy(UI_ProgramPosts[TAG_P1_AIRCURE].Tag,         "CureStartTemp");       UI_ProgramPosts[TAG_P1_AIRCURE].StoreData         = StoreAirCure;           UI_ProgramPosts[TAG_P1_AIRCURE].Reply         = UI_SendAirCure;
  strcpy(UI_ProgramPosts[TAG_P1_TEMPALARMS].Tag,      "AlarmTempLow");        UI_ProgramPosts[TAG_P1_TEMPALARMS].StoreData      = StoreTempAlarms;        UI_ProgramPosts[TAG_P1_TEMPALARMS].Reply      = UI_SendTempDevAlarms;
  strcpy(UI_ProgramPosts[TAG_P1_CURELIMITS].Tag,      "CureTempLowLimit");    UI_ProgramPosts[TAG_P1_CURELIMITS].StoreData      = StoreCureLimits;        UI_ProgramPosts[TAG_P1_CURELIMITS].Reply      = UI_SendTempDevAlarms;
  strcpy(UI_ProgramPosts[TAG_P1_RUNTIMES].Tag,        "runTimes");            UI_ProgramPosts[TAG_P1_RUNTIMES].StoreData        = StoreRuntimes;          UI_ProgramPosts[TAG_P1_RUNTIMES].Reply        = UI_SendRuntimes;
  strcpy(UI_ProgramPosts[TAG_P1_CURFANSPEED].Tag,     "setFanSpeed");         UI_ProgramPosts[TAG_P1_CURFANSPEED].StoreData     = SetCurrentFanSpeed;     UI_ProgramPosts[TAG_P1_CURFANSPEED].Reply     = UI_SendFanSpeed;
  strcpy(UI_ProgramPosts[TAG_P1_FANSPEEDS].Tag,       "maxFanSpeed");         UI_ProgramPosts[TAG_P1_FANSPEEDS].StoreData       = StoreFanSpeeds;         UI_ProgramPosts[TAG_P1_FANSPEEDS].Reply       = UI_SendFanSpeed;
  strcpy(UI_ProgramPosts[TAG_P1_RAMPRATE].Tag,        "updTemp");             UI_ProgramPosts[TAG_P1_RAMPRATE].StoreData        = StoreRampRate;          UI_ProgramPosts[TAG_P1_RAMPRATE].Reply        = UI_SendRampRate;
  strcpy(UI_ProgramPosts[TAG_P1_HUMIDCTRL].Tag,       "selHumidType");        UI_ProgramPosts[TAG_P1_HUMIDCTRL].StoreData       = StoreHumidCtrl;         UI_ProgramPosts[TAG_P1_HUMIDCTRL].Reply       = UI_SendHumCtrl;
  strcpy(UI_ProgramPosts[TAG_P1_CO2PURGE].Tag,        "selPurgeMode");        UI_ProgramPosts[TAG_P1_CO2PURGE].StoreData        = StoreCO2;               UI_ProgramPosts[TAG_P1_CO2PURGE].Reply        = UI_SendCo2;
  strcpy(UI_ProgramPosts[TAG_P1_MISC].Tag,            "p1Misc");              UI_ProgramPosts[TAG_P1_MISC].StoreData            = StoreMisc;              UI_ProgramPosts[TAG_P1_MISC].Reply            = UI_SendMisc;
  strcpy(UI_ProgramPosts[TAG_P1_EMAIL].Tag,           "selEmailAlert");       UI_ProgramPosts[TAG_P1_EMAIL].StoreData           = StoreEmail;             UI_ProgramPosts[TAG_P1_EMAIL].Reply           = UI_SendEmail;
  strcpy(UI_ProgramPosts[TAG_P1_ALERTS].Tag,          "AlertSetup");          UI_ProgramPosts[TAG_P1_ALERTS].StoreData          = StoreAlerts;            UI_ProgramPosts[TAG_P1_ALERTS].Reply          = UI_SendEmailAlertFlags;
  strcpy(UI_ProgramPosts[TAG_P1_DATETIME].Tag,        "Date");                UI_ProgramPosts[TAG_P1_DATETIME].StoreData        = StoreDateTime;          UI_ProgramPosts[TAG_P1_DATETIME].Reply        = UI_SendDateTime;
  strcpy(UI_ProgramPosts[TAG_P1_FANBOOST].Tag,        "selBoostMode");        UI_ProgramPosts[TAG_P1_FANBOOST].StoreData        = StoreFanBoost;          UI_ProgramPosts[TAG_P1_FANBOOST].Reply        = UI_SendFanBoost;
  strcpy(UI_ProgramPosts[TAG_P1_CLIMACELLTIMES].Tag,  "climacellTimes");      UI_ProgramPosts[TAG_P1_CLIMACELLTIMES].StoreData  = StoreClimacellTimes;    UI_ProgramPosts[TAG_P1_CLIMACELLTIMES].Reply  = UI_SendClimacellTimes;
  strcpy(UI_ProgramPosts[TAG_P1_LOADMONITOR].Tag,     "bay1Label");           UI_ProgramPosts[TAG_P1_LOADMONITOR].StoreData     = StoreLoadMonitor;       UI_ProgramPosts[TAG_P1_LOADMONITOR].Reply     = UI_SendLoadMonitor;
  strcpy(UI_ProgramPosts[TAG_P1_LOADLOGPIPE].Tag,     "LoadLogPipe");         UI_ProgramPosts[TAG_P1_LOADLOGPIPE].StoreData     = StoreLoadPipe;          UI_ProgramPosts[TAG_P1_LOADLOGPIPE].Reply     = UI_SendLoadMonitor;
  strcpy(UI_ProgramPosts[TAG_P1_LOADLOGPAUSE].Tag,    "LoadLogPause");        UI_ProgramPosts[TAG_P1_LOADLOGPAUSE].StoreData    = StoreLoadPause;         UI_ProgramPosts[TAG_P1_LOADLOGPAUSE].Reply    = UI_SendLoadMonitor;
  strcpy(UI_ProgramPosts[TAG_P1_LOADLOGSTOP].Tag,     "LoadLogStop");         UI_ProgramPosts[TAG_P1_LOADLOGSTOP].StoreData     = StoreLoadStop;          UI_ProgramPosts[TAG_P1_LOADLOGSTOP].Reply     = UI_SendLoadMonitor;
  strcpy(UI_ProgramPosts[TAG_P1_BAYNAMES].Tag,        "lightsBay1Label");     UI_ProgramPosts[TAG_P1_BAYNAMES].StoreData        = StoreBayNames;          UI_ProgramPosts[TAG_P1_BAYNAMES].Reply        = UI_SendLoadMonitor;

  // http port
  // this accepts the message from LTX/DeviceComm/DeviceSetIp() when the http port is set via the Display setup application
  strcpy(UI_ProgramPosts[TAG_P1_HTTPPORT].Tag,        "HttpPort");            UI_ProgramPosts[TAG_P1_HTTPPORT].StoreData        = StoreHttpPort;          UI_ProgramPosts[TAG_P1_HTTPPORT].Reply        = UI_SendHttpPort;
  // this accepts the message from the AS2 UI TCP/IP Setup page that currently only allows setting the public address - other values set via Display Setup application
  strcpy(UI_ProgramPosts[TAG_P1_PUBLICIP].Tag,        "publicOct1");          UI_ProgramPosts[TAG_P1_PUBLICIP].StoreData        = StorePublicAddress;     UI_ProgramPosts[TAG_P1_PUBLICIP].Reply        = UI_SendHttpPort;

  // LTX messaging sync
  strcpy(UI_ProgramPosts[TAG_P1_LTXINIT].Tag,         "Initialize");          UI_ProgramPosts[TAG_P1_LTXINIT].StoreData         = StoreLtxInit;           UI_ProgramPosts[TAG_P1_LTXINIT].Reply         = UI_SendLtxInit;

  // password authentication
  strcpy(UI_ProgramPosts[TAG_P1_PASSWORD].Tag,        "dlr");                 UI_ProgramPosts[TAG_P1_PASSWORD].StoreData        = PasswordAuth;           UI_ProgramPosts[TAG_P1_PASSWORD].Reply        = UI_SendPgmLevel;

  // program level 2 pages
  strcpy(UI_ProgramPosts[TAG_P2_BASIC].Tag,           "StorageName");         UI_ProgramPosts[TAG_P2_BASIC].StoreData           = StoreBasic;             UI_ProgramPosts[TAG_P2_BASIC].Reply           = UI_SendBasicSetup;
  strcpy(UI_ProgramPosts[TAG_P2_MASTERSLAVE].Tag,     "selMasterSlaveMode");  UI_ProgramPosts[TAG_P2_MASTERSLAVE].StoreData     = StoreMasterSlave;       UI_ProgramPosts[TAG_P2_MASTERSLAVE].Reply     = UI_SendBasicSetup;
  strcpy(UI_ProgramPosts[TAG_P2_USERLOG].Tag,         "recInterval");         UI_ProgramPosts[TAG_P2_USERLOG].StoreData         = StoreUserLog;           UI_ProgramPosts[TAG_P2_USERLOG].Reply         = UI_SendUserLogSettings;
  strcpy(UI_ProgramPosts[TAG_P2_PASSWORD].Tag,        "AcctId0");             UI_ProgramPosts[TAG_P2_PASSWORD].StoreData        = StorePassword;          UI_ProgramPosts[TAG_P2_PASSWORD].Reply        = UI_SendAccounts;
  strcpy(UI_ProgramPosts[TAG_P2_ANALOGBRD].Tag,       "BAdd");                UI_ProgramPosts[TAG_P2_ANALOGBRD].StoreData       = StoreAnalogBoard;       UI_ProgramPosts[TAG_P2_ANALOGBRD].Reply       = AnalogBoardPostReply;
  strcpy(UI_ProgramPosts[TAG_P2_DOOR].Tag,            "PAirValue");           UI_ProgramPosts[TAG_P2_DOOR].StoreData            = StoreDoor;              UI_ProgramPosts[TAG_P2_DOOR].Reply            = UI_SendDoor;
  strcpy(UI_ProgramPosts[TAG_P2_REFRIG].Tag,          "p2Refrigeration");     UI_ProgramPosts[TAG_P2_REFRIG].StoreData          = StoreRefrig;            UI_ProgramPosts[TAG_P2_REFRIG].Reply          = UI_SendRefrig;
  strcpy(UI_ProgramPosts[TAG_P2_CLIMACELL].Tag,       "ClimacellEff");        UI_ProgramPosts[TAG_P2_CLIMACELL].StoreData       = StoreClimacell;         UI_ProgramPosts[TAG_P2_CLIMACELL].Reply       = UI_SendClimacell;
  strcpy(UI_ProgramPosts[TAG_P2_FAIL1].Tag,           "FanMode");             UI_ProgramPosts[TAG_P2_FAIL1].StoreData           = StoreFailures;          UI_ProgramPosts[TAG_P2_FAIL1].Reply           = UI_SendFailures;
  strcpy(UI_ProgramPosts[TAG_P2_FAIL2].Tag,           "OutAirMode");          UI_ProgramPosts[TAG_P2_FAIL2].StoreData           = StoreFailures2;         UI_ProgramPosts[TAG_P2_FAIL2].Reply           = UI_SendFailures2;
  strcpy(UI_ProgramPosts[TAG_P2_NODEADD].Tag,         "ipOct1");              UI_ProgramPosts[TAG_P2_NODEADD].StoreData         = StoreNetworkNode;       UI_ProgramPosts[TAG_P2_NODEADD].Reply         = UI_SendNetworkNodes;
  strcpy(UI_ProgramPosts[TAG_P2_NODEUPDATE].Tag,      "NodeUpdate");          UI_ProgramPosts[TAG_P2_NODEUPDATE].StoreData      = StoreNetworkNodeUpdate; UI_ProgramPosts[TAG_P2_NODEUPDATE].Reply      = UI_SendNetworkNodes;
  strcpy(UI_ProgramPosts[TAG_P2_NODEDELETE].Tag,      "selNodeList");         UI_ProgramPosts[TAG_P2_NODEDELETE].StoreData      = DeleteNetworkNode;      UI_ProgramPosts[TAG_P2_NODEDELETE].Reply      = UI_SendNetworkNodes;
  strcpy(UI_ProgramPosts[TAG_P2_NODEDISCOVER].Tag,    "FindNodes");           UI_ProgramPosts[TAG_P2_NODEDISCOVER].StoreData    = StoreNodeDiscovery;     UI_ProgramPosts[TAG_P2_NODEDISCOVER].Reply    = UI_SendNetworkNodes;
  strcpy(UI_ProgramPosts[TAG_P2_SERVICE].Tag,         "dealerName");          UI_ProgramPosts[TAG_P2_SERVICE].StoreData         = StoreServiceInfo;       UI_ProgramPosts[TAG_P2_SERVICE].Reply         = UI_SendService;
  strcpy(UI_ProgramPosts[TAG_P2_BURNER].Tag,          "selBurnerMode");       UI_ProgramPosts[TAG_P2_BURNER].StoreData          = StoreBurner;            UI_ProgramPosts[TAG_P2_BURNER].Reply          = UI_SendBurner;
  strcpy(UI_ProgramPosts[TAG_P2_PWMCHANNEL].Tag,      "p2PwmOutputs");        UI_ProgramPosts[TAG_P2_PWMCHANNEL].StoreData      = StorePWMChannels;       UI_ProgramPosts[TAG_P2_PWMCHANNEL].Reply      = UI_SendPWMChannels;
  strcpy(UI_ProgramPosts[TAG_P2_IOCONFIG].Tag,        "p2IoConfig");          UI_ProgramPosts[TAG_P2_IOCONFIG].StoreData        = StoreIoConfig;          UI_ProgramPosts[TAG_P2_IOCONFIG].Reply        = UI_SendIoConfig;
  strcpy(UI_ProgramPosts[TAG_P2_IORENAME].Tag,        "ioRename");            UI_ProgramPosts[TAG_P2_IORENAME].StoreData        = StoreIoName;            UI_ProgramPosts[TAG_P2_IORENAME].Reply        = UI_SendIoDefinition;
  strcpy(UI_ProgramPosts[TAG_P2_AUXPROG].Tag,         "AuxProgram");          UI_ProgramPosts[TAG_P2_AUXPROG].StoreData         = StoreAuxProgram;        UI_ProgramPosts[TAG_P2_AUXPROG].Reply         = AuxProgramPostReply;

  // log settings
  strcpy(UI_ProgramPosts[TAG_P2_PIDLOG].Tag,          "pidWrap");             UI_ProgramPosts[TAG_P2_PIDLOG].StoreData          = StorePIDSettings;       UI_ProgramPosts[TAG_P2_PIDLOG].Reply          = UI_SendUserLogSettings;
  strcpy(UI_ProgramPosts[TAG_GRAPHFAVORITES].Tag,     "GraphFavorites");      UI_ProgramPosts[TAG_GRAPHFAVORITES].StoreData     = StoreGraphFavorites;    UI_ProgramPosts[TAG_GRAPHFAVORITES].Reply     = UI_SendGraphFavorites;

  // file transfers
  strcpy(UI_ProgramPosts[TAG_SAVESETTINGS].Tag,       "RestoreSettings");     UI_ProgramPosts[TAG_SAVESETTINGS].StoreData       = StoreSettings;          UI_ProgramPosts[TAG_SAVESETTINGS].Reply       = UI_SendSettingsAck;
  strcpy(UI_ProgramPosts[TAG_EQUIPDESC].Tag,          "equipDesc");           UI_ProgramPosts[TAG_EQUIPDESC].StoreData          = StoreIoTranslate;       UI_ProgramPosts[TAG_EQUIPDESC].Reply          = UI_SendEquipTranslateAck;
  strcpy(UI_ProgramPosts[TAG_PWMDESC].Tag,            "pwmDesc");             UI_ProgramPosts[TAG_PWMDESC].StoreData            = StorePwmName;           UI_ProgramPosts[TAG_PWMDESC].Reply            = UI_SendEquipTranslateAck;
  strcpy(UI_ProgramPosts[TAG_QUERYTAG].Tag,           "queryTag");            UI_ProgramPosts[TAG_QUERYTAG].StoreData           = StoreQueryTag;          UI_ProgramPosts[TAG_QUERYTAG].Reply           = UI_SendEquipTranslateAck;
  strcpy(UI_ProgramPosts[TAG_SYSLOGREC].Tag,          "syslogRec");           UI_ProgramPosts[TAG_SYSLOGREC].StoreData          = StoreSysLogRec;         UI_ProgramPosts[TAG_SYSLOGREC].Reply          = UI_SendEquipTranslateAck;
  strcpy(UI_ProgramPosts[TAG_SYSLOGEQUIP].Tag,        "syslogEquip");         UI_ProgramPosts[TAG_SYSLOGEQUIP].StoreData        = StoreSysLogEquip;       UI_ProgramPosts[TAG_SYSLOGEQUIP].Reply        = UI_SendEquipTranslateAck;
  strcpy(UI_ProgramPosts[TAG_SYSLOGREMOTE].Tag,       "syslogRemote");        UI_ProgramPosts[TAG_SYSLOGREMOTE].StoreData       = StoreSysLogRemote;      UI_ProgramPosts[TAG_SYSLOGREMOTE].Reply       = UI_SendEquipTranslateAck;
  strcpy(UI_ProgramPosts[TAG_PIDLOG].Tag,             "pidLog");              UI_ProgramPosts[TAG_PIDLOG].StoreData             = StorePIDLogLabel;       UI_ProgramPosts[TAG_PIDLOG].Reply             = UI_SendEquipTranslateAck;
  strcpy(UI_ProgramPosts[TAG_SYSMODE].Tag,            "sysmode");             UI_ProgramPosts[TAG_SYSMODE].StoreData            = StoreSysMode;           UI_ProgramPosts[TAG_SYSMODE].Reply            = UI_SendEquipTranslateAck;
  strcpy(UI_ProgramPosts[TAG_BOARDLABEL].Tag,         "boardLabel");          UI_ProgramPosts[TAG_BOARDLABEL].StoreData         = StoreAnalogBoardLabel;  UI_ProgramPosts[TAG_BOARDLABEL].Reply         = UI_SendEquipTranslateAck;
  strcpy(UI_ProgramPosts[TAG_TEMPSENSOR].Tag,         "tempSensor");          UI_ProgramPosts[TAG_TEMPSENSOR].StoreData         = StoreTempSensorLabel;   UI_ProgramPosts[TAG_TEMPSENSOR].Reply         = UI_SendEquipTranslateAck;
  strcpy(UI_ProgramPosts[TAG_HUMIDSENSOR].Tag,        "humidSensor");         UI_ProgramPosts[TAG_HUMIDSENSOR].StoreData        = StoreHumidSensorLabel;  UI_ProgramPosts[TAG_HUMIDSENSOR].Reply        = UI_SendEquipTranslateAck;
  strcpy(UI_ProgramPosts[TAG_BAYLABEL].Tag,           "bayLabel");            UI_ProgramPosts[TAG_BAYLABEL].StoreData           = StoreBayLabel;          UI_ProgramPosts[TAG_BAYLABEL].Reply           = UI_SendEquipTranslateAck;

  // equipment status page
  int eqIndex = 0;
  strcpy(UI_EquipPosts[eqIndex].Tag, "fanBtn");          UI_EquipPosts[eqIndex].StoreData = StoreRemoteOff;  UI_EquipPosts[eqIndex].SettingsPtr = &(Settings.RemoteOff[RO_FAN]);           UI_EquipPosts[eqIndex++].Reply = UI_SendEquipStatus;
  strcpy(UI_EquipPosts[eqIndex].Tag, "climacellBtn");    UI_EquipPosts[eqIndex].StoreData = StoreRemoteOff;  UI_EquipPosts[eqIndex].SettingsPtr = &(Settings.RemoteOff[RO_CLIMACELL]);     UI_EquipPosts[eqIndex++].Reply = UI_SendEquipStatus;
  strcpy(UI_EquipPosts[eqIndex].Tag, "humid1PumpBtn");   UI_EquipPosts[eqIndex].StoreData = StoreRemoteOff;  UI_EquipPosts[eqIndex].SettingsPtr = &(Settings.RemoteOff[RO_HUMIDIFIER1]);   UI_EquipPosts[eqIndex++].Reply = UI_SendEquipStatus;
  strcpy(UI_EquipPosts[eqIndex].Tag, "humid2PumpBtn");   UI_EquipPosts[eqIndex].StoreData = StoreRemoteOff;  UI_EquipPosts[eqIndex].SettingsPtr = &(Settings.RemoteOff[RO_HUMIDIFIER2]);   UI_EquipPosts[eqIndex++].Reply = UI_SendEquipStatus;
  strcpy(UI_EquipPosts[eqIndex].Tag, "humid3PumpBtn");   UI_EquipPosts[eqIndex].StoreData = StoreRemoteOff;  UI_EquipPosts[eqIndex].SettingsPtr = &(Settings.RemoteOff[RO_HUMIDIFIER3]);   UI_EquipPosts[eqIndex++].Reply = UI_SendEquipStatus;
  strcpy(UI_EquipPosts[eqIndex].Tag, "heatBtn");         UI_EquipPosts[eqIndex].StoreData = StoreRemoteOff;  UI_EquipPosts[eqIndex].SettingsPtr = &(Settings.RemoteOff[RO_HEAT]);          UI_EquipPosts[eqIndex++].Reply = UI_SendEquipStatus;
  strcpy(UI_EquipPosts[eqIndex].Tag, "cavHeatBtn");      UI_EquipPosts[eqIndex].StoreData = StoreRemoteOff;  UI_EquipPosts[eqIndex].SettingsPtr = &(Settings.RemoteOff[RO_CAVITY_HEAT]);   UI_EquipPosts[eqIndex++].Reply = UI_SendEquipStatus;
  strcpy(UI_EquipPosts[eqIndex].Tag, "refrigBtn");       UI_EquipPosts[eqIndex].StoreData = StoreRemoteOff;  UI_EquipPosts[eqIndex].SettingsPtr = &(Settings.RemoteOff[RO_REFRIGERATION]); UI_EquipPosts[eqIndex++].Reply = UI_SendEquipStatus;

  strcpy(UI_EquipPosts[eqIndex].Tag, "aux1Btn");         UI_EquipPosts[eqIndex].StoreData = StoreRemoteOff;  UI_EquipPosts[eqIndex].SettingsPtr = &(Settings.RemoteOff[RO_AUX1]);          UI_EquipPosts[eqIndex++].Reply = UI_SendEquipStatus;
  strcpy(UI_EquipPosts[eqIndex].Tag, "aux2Btn");         UI_EquipPosts[eqIndex].StoreData = StoreRemoteOff;  UI_EquipPosts[eqIndex].SettingsPtr = &(Settings.RemoteOff[RO_AUX2]);          UI_EquipPosts[eqIndex++].Reply = UI_SendEquipStatus;
  strcpy(UI_EquipPosts[eqIndex].Tag, "aux3Btn");         UI_EquipPosts[eqIndex].StoreData = StoreRemoteOff;  UI_EquipPosts[eqIndex].SettingsPtr = &(Settings.RemoteOff[RO_AUX3]);          UI_EquipPosts[eqIndex++].Reply = UI_SendEquipStatus;
  strcpy(UI_EquipPosts[eqIndex].Tag, "aux4Btn");         UI_EquipPosts[eqIndex].StoreData = StoreRemoteOff;  UI_EquipPosts[eqIndex].SettingsPtr = &(Settings.RemoteOff[RO_AUX4]);          UI_EquipPosts[eqIndex++].Reply = UI_SendEquipStatus;
  strcpy(UI_EquipPosts[eqIndex].Tag, "aux5Btn");         UI_EquipPosts[eqIndex].StoreData = StoreRemoteOff;  UI_EquipPosts[eqIndex].SettingsPtr = &(Settings.RemoteOff[RO_AUX5]);          UI_EquipPosts[eqIndex++].Reply = UI_SendEquipStatus;
  strcpy(UI_EquipPosts[eqIndex].Tag, "aux6Btn");         UI_EquipPosts[eqIndex].StoreData = StoreRemoteOff;  UI_EquipPosts[eqIndex].SettingsPtr = &(Settings.RemoteOff[RO_AUX6]);          UI_EquipPosts[eqIndex++].Reply = UI_SendEquipStatus;
  strcpy(UI_EquipPosts[eqIndex].Tag, "aux7Btn");         UI_EquipPosts[eqIndex].StoreData = StoreRemoteOff;  UI_EquipPosts[eqIndex].SettingsPtr = &(Settings.RemoteOff[RO_AUX7]);          UI_EquipPosts[eqIndex++].Reply = UI_SendEquipStatus;
  strcpy(UI_EquipPosts[eqIndex].Tag, "aux8Btn");         UI_EquipPosts[eqIndex].StoreData = StoreRemoteOff;  UI_EquipPosts[eqIndex].SettingsPtr = &(Settings.RemoteOff[RO_AUX8]);          UI_EquipPosts[eqIndex++].Reply = UI_SendEquipStatus;

  strcpy(UI_EquipPosts[eqIndex].Tag, "refr1Btn");        UI_EquipPosts[eqIndex].StoreData = StoreRefrigDiag; UI_EquipPosts[eqIndex].SettingsPtr = &(Settings.Refrig.Stage[0].Diagnostic);   UI_EquipPosts[eqIndex++].Reply = UI_SendEquipStatus;
  strcpy(UI_EquipPosts[eqIndex].Tag, "refr2Btn");        UI_EquipPosts[eqIndex].StoreData = StoreRefrigDiag; UI_EquipPosts[eqIndex].SettingsPtr = &(Settings.Refrig.Stage[1].Diagnostic);   UI_EquipPosts[eqIndex++].Reply = UI_SendEquipStatus;
  strcpy(UI_EquipPosts[eqIndex].Tag, "refr3Btn");        UI_EquipPosts[eqIndex].StoreData = StoreRefrigDiag; UI_EquipPosts[eqIndex].SettingsPtr = &(Settings.Refrig.Stage[2].Diagnostic);   UI_EquipPosts[eqIndex++].Reply = UI_SendEquipStatus;
  strcpy(UI_EquipPosts[eqIndex].Tag, "refr4Btn");        UI_EquipPosts[eqIndex].StoreData = StoreRefrigDiag; UI_EquipPosts[eqIndex].SettingsPtr = &(Settings.Refrig.Stage[3].Diagnostic);   UI_EquipPosts[eqIndex++].Reply = UI_SendEquipStatus;
  strcpy(UI_EquipPosts[eqIndex].Tag, "refr5Btn");        UI_EquipPosts[eqIndex].StoreData = StoreRefrigDiag; UI_EquipPosts[eqIndex].SettingsPtr = &(Settings.Refrig.Stage[4].Diagnostic);   UI_EquipPosts[eqIndex++].Reply = UI_SendEquipStatus;
  strcpy(UI_EquipPosts[eqIndex].Tag, "refr6Btn");        UI_EquipPosts[eqIndex].StoreData = StoreRefrigDiag; UI_EquipPosts[eqIndex].SettingsPtr = &(Settings.Refrig.Stage[5].Diagnostic);   UI_EquipPosts[eqIndex++].Reply = UI_SendEquipStatus;
  strcpy(UI_EquipPosts[eqIndex].Tag, "refr7Btn");        UI_EquipPosts[eqIndex].StoreData = StoreRefrigDiag; UI_EquipPosts[eqIndex].SettingsPtr = &(Settings.Refrig.Stage[6].Diagnostic);   UI_EquipPosts[eqIndex++].Reply = UI_SendEquipStatus;
  strcpy(UI_EquipPosts[eqIndex].Tag, "refr8Btn");        UI_EquipPosts[eqIndex].StoreData = StoreRefrigDiag; UI_EquipPosts[eqIndex].SettingsPtr = &(Settings.Refrig.Stage[7].Diagnostic);   UI_EquipPosts[eqIndex++].Reply = UI_SendEquipStatus;
  strcpy(UI_EquipPosts[eqIndex].Tag, "defrost1Btn");     UI_EquipPosts[eqIndex].StoreData = StoreRefrigDiag; UI_EquipPosts[eqIndex].SettingsPtr = &(Settings.Refrig.Defrost[0].Diagnostic); UI_EquipPosts[eqIndex++].Reply = UI_SendEquipStatus;
  strcpy(UI_EquipPosts[eqIndex].Tag, "defrost2Btn");     UI_EquipPosts[eqIndex].StoreData = StoreRefrigDiag; UI_EquipPosts[eqIndex].SettingsPtr = &(Settings.Refrig.Defrost[1].Diagnostic); UI_EquipPosts[eqIndex++].Reply = UI_SendEquipStatus;

  // onion
  strcpy(UI_EquipPosts[eqIndex].Tag, "cureBtn");         UI_EquipPosts[eqIndex].StoreData = StoreRemoteOff;  UI_EquipPosts[eqIndex].SettingsPtr = &(Settings.RemoteOff[RO_CURE]);           UI_EquipPosts[eqIndex++].Reply = UI_SendEquipStatus;
  strcpy(UI_EquipPosts[eqIndex].Tag, "burnerBtn");       UI_EquipPosts[eqIndex].StoreData = StoreRemoteOff;  UI_EquipPosts[eqIndex].SettingsPtr = &(Settings.RemoteOff[RO_BURNER]);         UI_EquipPosts[eqIndex++].Reply = UI_SendEquipStatus;

  // PID logging
  strcpy(UI_EquipPosts[eqIndex].Tag, "btnPIDDoorLog");   UI_EquipPosts[eqIndex].StoreData = StorePIDLog;     UI_EquipPosts[eqIndex].SettingsPtr = &Settings.Log.PID.Door;                   UI_EquipPosts[eqIndex++].Reply = UI_SendDoor;
  strcpy(UI_EquipPosts[eqIndex].Tag, "btnPIDRefrigLog"); UI_EquipPosts[eqIndex].StoreData = StorePIDLog;     UI_EquipPosts[eqIndex].SettingsPtr = &Settings.Log.PID.Refrig;                 UI_EquipPosts[eqIndex++].Reply = UI_SendRefrig;

  // remote start/stop on system monitor page
  strcpy(UI_EquipPosts[eqIndex].Tag, "remoteStop");      UI_EquipPosts[eqIndex].StoreData = StoreSystemOff;  UI_EquipPosts[eqIndex].SettingsPtr = &(Settings.RemoteOff[RO_FAN]);            UI_EquipPosts[eqIndex++].Reply = UI_SendEquipStatus;

  // bay lights
  strcpy(UI_EquipPosts[eqIndex].Tag, "lights1Btn");      UI_EquipPosts[eqIndex].StoreData = ToggleRemoteOff; UI_EquipPosts[eqIndex].SettingsPtr = &(Settings.RemoteOff[RO_LIGHTS1]);        UI_EquipPosts[eqIndex++].Reply = UI_SendEquipStatus;
  strcpy(UI_EquipPosts[eqIndex].Tag, "lights2Btn");      UI_EquipPosts[eqIndex].StoreData = ToggleRemoteOff; UI_EquipPosts[eqIndex].SettingsPtr = &(Settings.RemoteOff[RO_LIGHTS2]);        UI_EquipPosts[eqIndex++].Reply = UI_SendEquipStatus;
} // end UI_PostMsg_Init()

/***************************************************************************

FUNCTION:   ValidatePostTag()

PURPOSE:    Checks a passed tag against the defined valid message tags

COMMENTS:

***************************************************************************/
int ValidatePostTag(char *Tag, int *PostType)
{
  int i;

  // check for tag in program posts
  for (i = 0; i < UI_NUM_PROGRAMPOSTS; i++)
  {
    if (strcmp(Tag, UI_ProgramPosts[i].Tag) == 0)
    {
      *PostType = PT_PROGRAM;
      return i;
    }
  }

  // check for tag in action posts
  for (i = 0; i < UI_NUM_ACTIONPOSTS; i++)
  {
    if (strcmp(Tag, UI_ActionPosts[i].Tag) == 0)
    {
      *PostType = PT_ACTION;
      return i;
    }
  }

  // check for tag in equipment status posts
  for (i = 0; i < UI_NUM_EQUIPPOSTS; i++)
  {
    if (strcmp(Tag, UI_EquipPosts[i].Tag) == 0)
    {
      *PostType = PT_EQUIP;
      return i;
    }
  }

  return -1;
} // end ValidatePostTag()

