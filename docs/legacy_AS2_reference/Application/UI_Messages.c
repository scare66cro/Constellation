/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     UI_Messages.c

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:  Lantronix/UI messages

COMMENTS: The multi-message structure is set up for potentially large messages
          with an indeterminate size like sensor messages, warnings, and log
          data.

          The mechanism for multi-messaging is for the ARM to send a message
          with the MultiMsg tag followed by the multi-message type (LogData,
          Warning, etc.) followed by the message tags contained in that particular
          multi-message sequence.  For example, the ARM will call
          UI_SendMultiHdr("LogData,Key,Dates,TimeStamps,Data,Warnings", SessionID)
          which will create the message
            MultiMsg=0,LogData,Key,Dates,TimeStamps,Data,Warnings.

          An array of structures is then created with each element containing
          the message tag (Key, Dates, etc.) and a pointer to the linked list
          of message "vars" (CGI vars).

          When a message comes in, it is first
          checked against the array of standard CGI tags/messages.  If it can't
          be found there, it is checked against the multi-message structures.
          If it is found, then it's var string (it's message payload) is added
          to the appropriate linked list.

          When a web page perforns a 'get', GetMultiMsgData() traverses these
          linked lists writing each one out to the CGI file (HttpWriteData)
          essentially creating one long var in the CGI file for the web page
          to read, parse, and display.

          Each time a new MultiMsg message is received, the existing linked list
          for that multi-message type is freed.  This obviously prevents the
          lists from continually growing.  It also makes each multi-message
          sequence behave like a standard message where each time it is sent, it
          overwrites the previous message.

          NOTE: Only the UI_SendMultiHdr() calls associated with the logs pass
          a valid SessionID.  It is used by the LTX to notify the UI if too
          much data was requested and the LTX ran out of memory to malloc.
          Because only the logs have the potential to send enough data to cause
          this, the LTX only checks for memory errors when receiving log data
          so it's not necessary to send it with the types of mult-messages.
          They send the NO_SESSIONID value instead.

***************************************************************************/

/*** include files ***/

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <float.h>

// FreeRTOS Includes
#include "FreeRTOS.h"
#include "task.h"

// Platform
#include "system.h"

// Gellert
#include "Analog_Input.h"
#include "Controls.h"
#include "DataExc.h"
#include "LoadLogs.h"
#include "Modes.h"
#include "PWM.h"
#include "RTC.h"
#include "SDCard.h"
#include "SerialShift.h"
#include "Settings.h"
#include "States.h"
#include "StorePostData.h"
#include "ThreadFileReceive.h"
#include "ThreadMonitor.h"
#include "Timer.h"
#include "UI_Messages.h"
#include "Usart.h"
#include "UserLogs.h"
#include "Warnings.h"

/*** defines ***/

/*** typedefs and structures ***/

/*** module variables ***/

/*** static functions ***/

static int AddToMsg(char *msg, char *element);

static void FormatCoolingOutput(char *CoolOutput, char *CoolLabel, char *BurnerOutput);
static void FormatDefaultHumidBoard(char *PlenumHumid, char *OutsideHumid, char *ReturnHumid, char *Co2, char *Mode);
static void FormatDefaultTempBoard(char *PlenumTemp, char *OutsideTemp, char *ReturnTemp, char *Mode);
static void FormatFanSpeed(char *FanSpeed);
static void FormatPlenumTemp(char *ValStr);
static void FormatSensorValue(int Board, int Sensor, int WarningIndex, char *ValStr, char *FormatStr);

static void UI_SendMessage(char *message, int SendOption);

/***************************************************************************

FUNCTION:   AddToMsg()

PURPOSE:    Attempts to add a string to a message buffer without overflowing the buffer

COMMENTS:   Tested with 6 boards with sensor labels long enough to cause overflow.

***************************************************************************/
int AddToMsg(char *msg, char *element)
{
  if ((strlen(msg) + strlen(element)) < (MSG_TX_CGIVAR_SIZE - 3)) // -3 so there's always room for the message delimiter
  {
    strcat(msg, element);
    return 1;
  }
  return 0;
} // end AddToMsg()

/***************************************************************************

FUNCTION:   AnalogBoardPostReply()

PURPOSE:    Send the reply to the post message for the analog boards

COMMENTS:   This helper function just calls UI_SendAnalogBoard() with the
            correct board & direction parameters.  It is necessary because
            UI_SendAnalogBoard() can't be passed as the 'Reply' function
            in UI_ProgramPosts[] directly because the passed parameters are
            incompatible - all the other reply functions just pass an 'int'.

***************************************************************************/
void AnalogBoardPostReply(int SendOption)
{
  UI_SendAnalogBoard((atoi(PostValue[0]) - 1), 0, SendOption);
} // end AnalogBoardPostReply()

/***************************************************************************

FUNCTION:   AuxProgramPostReply()

PURPOSE:    Send the reply to the post message for the auxiliary output programs

COMMENTS:   This helper function just calls UI_SendAuxProgram() with the
            correct output & direction parameters.  It is necessary because
            UI_SendAuxProgram() can't be passed as the 'Reply' function
            in UI_ProgramPosts[] directly because the passed parameters are
            incompatible - all the other reply functions just pass an 'int'.

***************************************************************************/
void AuxProgramPostReply(int SendOption)
{
  UI_SendAuxProgram((EQUIPMENT_IO) (atoi(PostValue[0]) - 1), 1, SendOption);
} // end AuxProgramPostReply()

/***************************************************************************

FUNCTION:   FormatCoolingOutput()

PURPOSE:    Format the cooling output string for the UI

COMMENTS:   The cooling output element can represent either cooling,
            refrigeration, or burner - CoolLabel = 0, 1, or 2.
            The UI looks at the CoolLabel and changes the display accordingly.

***************************************************************************/
void FormatCoolingOutput(char *CoolOutput, char *CoolLabel, char *BurnerOutput)
{
  float PWMPercent = 0;

  if (   (   SystemState == ST_REFRIG
          || SystemState == ST_REFRIGDEHUMID
          || SystemState == ST_DEFROST)
       && CurrentMode != UI_PURGE)
  {
    PWMPercent = PWMValToPercent(PwmChannel[PWM_REFRIGERATION].Output);
    sprintf(CoolOutput, "%.0f", PWMPercent);
    strcpy(CoolLabel, "1");

    if (SystemState == ST_REFRIGDEHUMID)
    {
      PWMPercent = PWMValToPercent(PwmChannel[PWM_BURNER].Output);
      sprintf(BurnerOutput, "%.0f", PWMPercent);
    }
  }
  else if (   Settings.SystemMode == SM_ONION
           && CheckInputs(SW_CURE_AUTO))
  {
    PWMPercent = PWMValToPercent(PwmChannel[PWM_BURNER].Output);
    sprintf(BurnerOutput, "%.0f", PWMPercent);

    if (CheckInputs(SW_FRESHAIR_MANUAL))
    {
      strcpy(CoolOutput, "Manual");
    }
    else if (CheckInputs(SW_FRESHAIR_AUTO))
    {
      PWMPercent = PWMValToPercent(PwmChannel[PWM_DOORS].Output);
      sprintf(CoolOutput, "%.0f", PWMPercent);
    }
    else
    {
      strcpy(CoolOutput, "Off");
    }

    strcpy(CoolLabel, "2");
  }
  else
  {
    if (CheckInputs(SW_FRESHAIR_MANUAL))
    {
      strcpy(CoolOutput, "Manual");
      strcpy(CoolLabel, "0");
    }
    else if (CheckInputs(SW_FRESHAIR_AUTO))
    {
      if (   SystemState == ST_COOLING
          || SystemState == ST_COOLDEHUMID
          || CurrentMode == UI_PURGE)
      {
        PWMPercent = PWMValToPercent(PwmChannel[PWM_DOORS].Output);
        sprintf(CoolOutput, "%.0f", PWMPercent);
        strcpy(CoolLabel, "0");

        if (SystemState == ST_COOLDEHUMID)
        {
          PWMPercent = PWMValToPercent(PwmChannel[PWM_BURNER].Output);
          sprintf(BurnerOutput, "%.0f", PWMPercent);
        }
      }
    }
    else
    {
      strcpy(CoolOutput, "Off");
      strcpy(CoolLabel, "0");
    }
  }
} // end FormatCoolingOutput()

/***************************************************************************

FUNCTION:   FormatDefaultHumidBoard()

PURPOSE:    Format the default humidity board values for the UI

COMMENTS:   "dis" is sent to the UI to indicate that the sensors are disabled.
            The UI changes the display accordingly.

***************************************************************************/
void FormatDefaultHumidBoard(char *PlenumHumid, char *OutsideHumid, char *ReturnHumid, char *Co2, char *Mode)
{
  if (   Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Disabled == 1
      || Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Present == 0)
  {
    strcpy(PlenumHumid, "dis");
    strcpy(OutsideHumid, "dis");
    strcpy(ReturnHumid, "dis");
    strcpy(Co2, "dis");
  }
  else
  {
    if (Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Present == 1)
    {
      if (Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Type == ANALOG_BOARD_TYPE_HUMID)
      {
        // get the sensor values
        FormatSensorValue(DEFAULT_HUMID_BOARD, SENSOR_PLENUM_HUMID, WARN_INVALIDPLENHUMID, PlenumHumid, "%.0f");
        FormatSensorValue(DEFAULT_HUMID_BOARD, SENSOR_OUTSIDE_HUMID, NO_WARNING, OutsideHumid, "%.0f");
        FormatSensorValue(DEFAULT_HUMID_BOARD, SENSOR_RETURN_HUMID, WARN_RETURNAIRHUMID, ReturnHumid, "%.0f");

        if (Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_CO2].Type == ANALOG_SENSOR_TYPE_CO2)
        {
        if (MainTimer[MT_SYSUPTIME] >= 30)
          {
            FormatSensorValue(DEFAULT_HUMID_BOARD, SENSOR_CO2, WARN_INVALIDCO2, Co2, "%.1f");
          }
        }
        else
        {
          strcpy(Co2, "dis");
        }

        if (   Settings.MasterSlave == MSMODE_SLAVE
            || Settings.MasterSlave == MSMODE_SLAVE_NOBROADCAST)
        {
          *Mode = 1;    // slave mode, no broadcast - using local sensor
        }
      }
      else
      {
        WarningsSet(WARN_BOARDNOTHUMID, FM_ALARM, FM_ALARM, NA);
      }
    }
  }

  // if in slave mode, report the outside humidity sent from master
  if (Settings.MasterSlave == MSMODE_SLAVE)
  {
    strcpy(OutsideHumid, "--");
    Settings.AnalogBoard[DEFAULT_HUMID_BOARD].Sensor[SENSOR_OUTSIDE_HUMID].Disabled = 0;
    FormatSensorValue(DEFAULT_HUMID_BOARD, SENSOR_OUTSIDE_HUMID, NO_WARNING, OutsideHumid, "%.0f");
    *Mode = 2;    // slave mode - using remote sensor
  }
} // end FormatDefaultHumidBoard()

/***************************************************************************

FUNCTION:   FormatDefaultTempBoard()

PURPOSE:    Format the default temperature board values for the UI

COMMENTS:   Reports an error on startup if the default temperature board is
            disabled.

            Reports an error if the board is not present or if the board is
            not a temperature board.

***************************************************************************/
void FormatDefaultTempBoard(char *PlenumTemp, char *OutsideTemp, char *ReturnTemp, char *Mode)
{
  unsigned int Counter = XTimerVal;

  if (Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Disabled == 1)
  {
    if (Counter < STARTUP_ERR_TIMER)
    {
      WarningsSet(WARN_DEFTEMPDIS, FM_ALARM, FM_ALARM, NA);   // board disabled
    }
  }
  else
  {
    if (Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Present == 1)
    {
      if (Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Type == ANALOG_BOARD_TYPE_TEMP)
      {
        // get the sensor values
        FormatPlenumTemp(PlenumTemp);
        FormatSensorValue(DEFAULT_TEMP_BOARD, SENSOR_OUTSIDE_TEMP, NO_WARNING, OutsideTemp, "%.1f");
        FormatSensorValue(DEFAULT_TEMP_BOARD, SENSOR_RETURN_TEMP, WARN_RETURNAIRTEMP, ReturnTemp, "%.1f");

        if (Settings.MasterSlave == MSMODE_SLAVE)
          *Mode = 2;    // slave mode - using remote sensor
        else if (Settings.MasterSlave == MSMODE_SLAVE_NOBROADCAST)
          *Mode = 1;    // slave mode, no broadcast - using local sensor
      }
      else
      {
        WarningsSet(WARN_BOARDNOTTEMP, FM_ALARM, FM_ALARM, NA);   // not a temperature board
      }
    }
    else
    {
      WarningsSet(WARN_DEFAULTTEMP, FM_ALARM, FM_ALARM, NA);    // no board
    }
  }
} // end FormatDefaultTempBoard()

/***************************************************************************

FUNCTION:   FormatFanSpeed()

PURPOSE:    Format the fan speed for the UI

COMMENTS:

***************************************************************************/
void FormatFanSpeed(char *FanSpeed)
{
  if (   CheckInputs(SW_FAN_AUTO)
      && CheckOutputs(EQ_FAN))
  {
    sprintf(FanSpeed, "%d", PWMValToPercent(PwmChannel[PWM_FAN].Output));
  }
  else if (   CheckInputs(SW_FAN_MANUAL)
           && CheckOutputs(EQ_FAN))
  {
    strcpy(FanSpeed, "Manual");
  }
  else
  {
    strcpy(FanSpeed, "Off");
  }

  // NOTE: the low temp limit and airflow restriction failures leave the
  // fan output on to keep the close pulse door output on.  But because the
  // board cuts the power to the outputs for these two failures, the fan
  // doesn't actually run.  So the UI needs to report the fan is off.
  if (CurrentMode == UI_FAILURE)
  {
    strcpy(FanSpeed, "Off");
  }
} // end FormatFanSpeed()

/***************************************************************************

FUNCTION:   FormatPlenumTemp()

PURPOSE:    Calculate and format the plenum temperature for the UI

COMMENTS:   The plenum temperature on the display is an average of the
            plenum 1 & plenum 2 sensors.  If one or the other of the sensor
            values is invalid, it will display the other and report an error.
            If they are both invalid, it will display "--" and report an error.

***************************************************************************/
void FormatPlenumTemp(char *ValStr)
{
  if (PlenumTempAvg != SENSOR_VAL_UNDEFINED)
    sprintf(ValStr, "%.1f", PlenumTempAvg);
  else
    strcpy(ValStr, "--");
} // end FormatPlenumTemp()

/***************************************************************************

FUNCTION:   FormatSensorValue()

PURPOSE:    Format a sensor value for the UI

COMMENTS:   If the sensor value is SENSOR_VAL_UNDEFINED, it will report an
            error with the passed error index.

***************************************************************************/
void FormatSensorValue(int Board, int Sensor, int WarningIndex, char *ValStr, char *FormatStr)
{
  if (   Settings.AnalogBoard[Board].Sensor[Sensor].Disabled == 0
      || (   Settings.AnalogBoard[Board].Sensor[Sensor].Disabled == 1
          && Settings.MasterSlave == MSMODE_SLAVE
          && (   (Board == DEFAULT_TEMP_BOARD && Sensor == SENSOR_OUTSIDE_TEMP)
              || (Board == DEFAULT_HUMID_BOARD && Sensor == SENSOR_OUTSIDE_HUMID) ) ) )
  {
    if (Settings.AnalogBoard[Board].Sensor[Sensor].Value == SENSOR_VAL_UNDEFINED)
    {
      if (WarningIndex != NO_WARNING)
      {
        WarningsSet((WARNING_ITEMS)WarningIndex, FM_ALARM, FM_ALARM, NA);
      }
    }
    else
    {
      sprintf(ValStr, FormatStr, Settings.AnalogBoard[Board].Sensor[Sensor].Value);
    }
  }
  else
  {
    strcpy(ValStr, "dis");
  }
} // end FormatSensorValue()

/***************************************************************************

FUNCTION:   MultiMsgAdd()

PURPOSE:    Add a string to a multi-message

COMMENTS:   Attempts to add a string to the message buffer.  If there isn't
            enough space in the buffer to add it, it sends the full buffer
            to the LTX and starts a new message by copying in the message
            tag and adding the string.

            The function can also be used to just send the buffer by passing
            it a null item string and a 'terminate' value of 1.

            For an explanation of multi-messages, see the comments for this
            module or the comments for UI_SendMultiHdr().

***************************************************************************/
void MultiMsgAdd(char *msg, char *tag, char *item, int terminate)
{
  if (strcmp(item, "") != 0)
  {
    if (AddToMsg(msg, item) == 0)
    {
      // if the message is full, send it
      SendMsgAndWaitForResponse(msg, UI_DELAY_STANDARD);

      // start a new message
      strcpy(msg, tag);
      AddToMsg(msg, item);
    }
  }

  if (terminate == 1)
    SendMsgAndWaitForResponse(msg, UI_DELAY_STANDARD);
} // end MultiMsgAdd()

/***************************************************************************

FUNCTION:   UI_SendMessage()

PURPOSE:    Either queue or send the message

COMMENTS:

***************************************************************************/
void UI_SendMessage(char *message, int SendOption)
{
  if (SendOption == MSG_QUEUE)
    QueueMessage(message);
  else
  {
//    DBGU_SendString(message);
    SendMsgAndWaitForResponse(message, UI_DELAY_STANDARD);
  }
} // end UI_SendMessage()

/***************************************************************************

FUNCTION:   UI_SendAccounts()

PURPOSE:    Send user account IDs

COMMENTS:

***************************************************************************/
void UI_SendAccounts(int SendOption)
{
  int i;
  int count = 1;
  int password = 1;
  char str[100];
  char message[MSG_TX_BUFFER_SIZE];

  snprintf(message,
           MSG_TX_BUFFER_SIZE,
           "%s=%s",
           UI_ProgramPosts[TAG_P2_PASSWORD].Tag,
           Settings.User[0].ID);

  for (i = 1; i < NUM_PASSWORDS; i++)
  {
    strncat(message, ",", sizeof(message)-strlen(message)-1);
    strncat(message, Settings.User[i].ID, sizeof(message)-strlen(message)-1);

    if (strcmp(Settings.User[i].ID, "") != 0)
      count++;
  }

  // if no level one password defined
  if (strcmp(Settings.User[0].Password, "") == 0)
    password = 0;

  snprintf(str,
           sizeof(str),
           ",%d,%d",
           count,
           password);
  strncat(message, str, sizeof(message)-strlen(message)-1);

  UI_SendMessage(message, SendOption);
} // end UI_SendAccounts()

/***************************************************************************

FUNCTION:   UI_SendAirCure()

PURPOSE:    Send outside air settings

COMMENTS:

***************************************************************************/
void UI_SendAirCure(int SendOption)
{
  char message[MSG_TX_BUFFER_SIZE];

  snprintf(message,
           MSG_TX_BUFFER_SIZE,
           "%s=%.1f,%d,%d,%d",
           UI_ProgramPosts[TAG_P1_AIRCURE].Tag,
           Settings.Cure.StartTemp,
           Settings.Cure.HumidRef,
           Settings.Cure.StartHumid,
           Settings.Cure.HumidHighLimit);

  UI_SendMessage(message, SendOption);
} // end UI_SendAirCure()

/***************************************************************************

FUNCTION:   UI_SendAllSettings()

PURPOSE:    Send all the UI settings

COMMENTS:

***************************************************************************/
void UI_SendAllSettings(void)
{
  UI_SendMain(MSG_QUEUE);
  UI_SendMode();
  UI_SendHumidModes();
  UI_SendFanDailyRun(MSG_QUEUE);
  UI_SendFanTotalRun(MSG_QUEUE);
  UI_SendBasicSetup(MSG_QUEUE);
  UI_SendNetworkNodes(MSG_QUEUE);
  UI_SendDoor(MSG_QUEUE);
  UI_SendPWMChannels(MSG_QUEUE);
  UI_SendRefrig(MSG_QUEUE);
  UI_SendClimacell(MSG_QUEUE);
  UI_SendClimacellTimes(MSG_QUEUE);
  UI_SendBurner(MSG_QUEUE);
  UI_SendFailures(MSG_QUEUE);
  UI_SendFailures2(MSG_QUEUE);
  UI_SendService(MSG_QUEUE);
  UI_SendPlenSetPoints(MSG_QUEUE);
  UI_SendOutsideAir(MSG_QUEUE);
  UI_SendAirCure(MSG_QUEUE);
  UI_SendTempDevAlarms(MSG_QUEUE);
  UI_SendRuntimes(MSG_QUEUE);
  UI_SendFanSpeed(MSG_QUEUE);
  UI_SendFanBoost(MSG_QUEUE);
  UI_SendRampRate(MSG_QUEUE);
  UI_SendHumCtrl(MSG_QUEUE);
  UI_SendCo2(MSG_QUEUE);
  UI_SendEquipStatus(MSG_QUEUE);
  UI_SendMisc(MSG_QUEUE);
  UI_SendEmail(MSG_QUEUE);
  UI_SendEmailAlertFlags(MSG_QUEUE);
  UI_SendUserLogSettings(MSG_QUEUE);
  UI_SendVersions();
  UI_SendAccounts(MSG_QUEUE);
  UI_SendGraphFavorites(MSG_QUEUE);
  UI_SendLoadMonitor(MSG_QUEUE);
  UI_SendIoDefinition(MSG_QUEUE);
  UI_SendIoConfig(MSG_QUEUE);
  UI_SendAvailableIo(MSG_QUEUE);
  UI_SendAuxSwitches(MSG_QUEUE);
} // end UI_SendAllSettings()

/***************************************************************************

FUNCTION:   UI_SendAnalogBoard()

PURPOSE:    Send settings for requested analog board

COMMENTS:

***************************************************************************/
void UI_SendAnalogBoard(int board, int direction, int SendOption)
{
  int  i = 0;
  char message[MSG_TX_BUFFER_SIZE];
  char SensorStr[MSG_TX_BUFFER_SIZE];
  char sensorFormat[30];
  char str[20];

  // if the board requested isn't present, find the next board that is
  while (Settings.AnalogBoard[board].Present != 1 && i < ANALOG_BOARDS_PER_SYSTEM)
  {
    if (direction == 1)
      board++;
    else
      board--;
    if (board >= ANALOG_BOARDS_PER_SYSTEM)
      board = 0;
    if (board < 0)
      board = ANALOG_BOARDS_PER_SYSTEM - 1;
    i++;
  }

  snprintf(message, MSG_TX_BUFFER_SIZE, "%s=", UI_ProgramPosts[TAG_P2_ANALOGBRD].Tag);

  if (Settings.AnalogBoard[board].Present == 1)
  {
    snprintf(message,
             MSG_TX_BUFFER_SIZE,
             "%s=%d,%d,%s,%s,%d",
             UI_ProgramPosts[TAG_P2_ANALOGBRD].Tag,
             (Settings.AnalogBoard[board].Address + 1),
             Settings.AnalogBoard[board].Type,
             Settings.AnalogBoard[board].Label,
             Settings.AnalogBoard[board].Version,
             Settings.AnalogBoard[board].Disabled);

    for (i = 0; i < ANALOG_SENSORS_PER_BOARD; i++)
    {
      if (Settings.AnalogBoard[board].Sensor[i].Value != SENSOR_VAL_UNDEFINED)
      {
        if (Settings.AnalogBoard[board].Sensor[i].Type == ANALOG_SENSOR_TYPE_TEMP)
        {
          strcpy(sensorFormat, "%.1f");
        }
        else
        {
          strcpy(sensorFormat, "%.0f");
        }
        snprintf(str, sizeof(str), sensorFormat, Settings.AnalogBoard[board].Sensor[i].Value);
      }
      else
      {
        strcpy(str, "--");
      }

      snprintf(SensorStr,
               MSG_TX_BUFFER_SIZE,
               ",%d,%s,%.1f,%d,%s",
               Settings.AnalogBoard[board].Sensor[i].Type,
               Settings.AnalogBoard[board].Sensor[i].Label,
               Settings.AnalogBoard[board].Sensor[i].Offset,
               Settings.AnalogBoard[board].Sensor[i].Disabled,
               str);

      strncat(message, SensorStr, sizeof(message)-strlen(message)-1);
    } // for num sensors
  } // if board present

  UI_SendMessage(message, SendOption);
} // end UI_SendAnalogBoard()

/***************************************************************************

FUNCTION:   UI_SendAuxProgram()

PURPOSE:    Send the auxiliary output program configuration

COMMENTS:

***************************************************************************/
void UI_SendAuxProgram(EQUIPMENT_IO eqIndex, int direction, int SendOption)
{
  int j;
  int i = 0;
  uint8_t programIndex = eqIndex - EQ_AUX1;
  char programData[100] = "";
  char message[MSG_TX_BUFFER_SIZE] = "";
  char subMessage[MSG_TX_BUFFER_SIZE] = "";

  do
  {
    if (direction == 1)
    {
      programIndex++;
      programIndex %= NUM_AUX_OUTPUTS;
    }
    else
    {
      if (programIndex == 0)
      {
        programIndex = NUM_AUX_OUTPUTS - 1;
      }
      else
      {
        programIndex--;
      }
    }

    i++;
  }
  while (Settings.AuxProgram[programIndex].Rule[0].Type == RT_UNDEFINED && i < NUM_AUX_OUTPUTS);

  if (Settings.AuxProgram[programIndex].Rule[0].Type != RT_UNDEFINED)
  {
    for (j = 0; j < NUM_AUX_PROGRAM_RULES; ++j)
    {
      snprintf(programData,
               sizeof(programData),
               "%d:%d:%d:%d:%.1f:%d:%d,",
               Settings.AuxProgram[programIndex].Rule[j].Type,
               Settings.AuxProgram[programIndex].Rule[j].IoIndex,
               Settings.AuxProgram[programIndex].Rule[j].State,
               Settings.AuxProgram[programIndex].Rule[j].Op,
               Settings.AuxProgram[programIndex].Rule[j].SensorValue,
               Settings.AuxProgram[programIndex].Rule[j].AndOr,
               Settings.AuxProgram[programIndex].Rule[j].ReferenceIndex);
      strncat(subMessage, programData, sizeof(subMessage)-strlen(subMessage)-1);
    }

    snprintf(message,
             MSG_TX_BUFFER_SIZE,
             "%s=%d:%d:%d:%d,%s,",
             UI_ProgramPosts[TAG_P2_AUXPROG].Tag,
             EQ_AUX1 + programIndex,
             Settings.AuxProgram[programIndex].DutyCycle,
             Settings.AuxProgram[programIndex].Period,
             Settings.AuxProgram[programIndex].Units,
             subMessage);
  }
  else
  {
    snprintf(message,
             MSG_TX_BUFFER_SIZE,
             "%s=%d:%s,",
             UI_ProgramPosts[TAG_P2_AUXPROG].Tag,
             EQ_AUX1 + programIndex,
             "undefined");
  }

  UI_SendMessage(message, SendOption);
} // UI_SendAuxProgram()

/***************************************************************************

FUNCTION:   UI_SendAuxSwitches()

PURPOSE:    Send the auxiliary switch configuration

COMMENTS:

***************************************************************************/
void UI_SendAuxSwitches(int SendOption)
{
  int i,j;
  char switchUsage[NUM_AUX_SWITCHES][NUM_AUX_OUTPUTS+1];
  char message[MSG_TX_BUFFER_SIZE] = "AuxSwitches=";
  char subMessage[MSG_TX_BUFFER_SIZE] = "";
  char str[10];

  memset(switchUsage, 5, sizeof(switchUsage));  // set to single-digit invalid value (valid 0-2)
  switchUsage[0][0] = 0;  // set the switch values to 'off'
  switchUsage[1][0] = 0;

  if (CheckInputs(SW_AUX1_AUTO))   switchUsage[0][0] = 1;
  if (CheckInputs(SW_AUX1_MANUAL)) switchUsage[0][0] = 2;
  if (CheckInputs(SW_AUX2_AUTO))   switchUsage[1][0] = 1;
  if (CheckInputs(SW_AUX2_MANUAL)) switchUsage[1][0] = 2;

  for (i = 0; i < NUM_AUX_OUTPUTS; ++i)
  {
    if (Settings.AuxProgram[i].Rule[0].Type != RT_UNDEFINED)
    {
      for (j = 0; j < NUM_AUX_PROGRAM_RULES; ++j)
      {
        if (   Settings.AuxProgram[i].Rule[j].IoIndex == SW_AUX1_AUTO
            || Settings.AuxProgram[i].Rule[j].IoIndex == SW_AUX1_MANUAL)
        {
          switchUsage[0][i+1] = switchUsage[0][0];
        }

        if (   Settings.AuxProgram[i].Rule[j].IoIndex == SW_AUX2_AUTO
            || Settings.AuxProgram[i].Rule[j].IoIndex == SW_AUX2_MANUAL)
        {
          switchUsage[1][i+1] = switchUsage[1][0];
        }
      }
    }
  }

  for (i = 0; i < NUM_AUX_SWITCHES; ++i)
  {
    for (j = 0; j < NUM_AUX_OUTPUTS + 1; ++j)
    {
      snprintf(str, sizeof(str), "%d:", switchUsage[i][j]);
      strncat(subMessage, str, sizeof(subMessage)-strlen(subMessage)-1);
    }

//    snprintf(message, MSG_TX_BUFFER_SIZE, "%s,", message);
    strncat(subMessage, ",", sizeof(subMessage)-strlen(subMessage)-1);
  }

  strncat(message, subMessage, sizeof(message)-strlen(message)-1);
  UI_SendMessage(message, SendOption);
} // UI_SendAuxSwitches()

/***************************************************************************

FUNCTION:   UI_SendAvailableIo()

PURPOSE:    Send the available system I/O

COMMENTS:

***************************************************************************/
void UI_SendAvailableIo(int SendOption)
{
  char message[MSG_TX_BUFFER_SIZE];

  GetAvailableIo(message);
  UI_SendMessage(message, SendOption);
} // UI_SendAvailableIo()

/***************************************************************************

FUNCTION:   UI_SendBasicSetup()

PURPOSE:    Send the program level 2 basic setup page settings

COMMENTS:

***************************************************************************/
void UI_SendBasicSetup(int SendOption)
{
  char message[MSG_TX_BUFFER_SIZE];
  int  Mode;

  if (Settings.MasterSlave == MSMODE_SLAVE_NOBROADCAST)
    Mode = MSMODE_SLAVE; // report slave mode to UI for slave-nobroadcast mode also
  else if (   SendingLogData
           && (   Settings.MasterSlave == MSMODE_SLAVE
               || Settings.MasterSlave == MSMODE_SLAVE_NOBROADCAST) )
    Mode = MSMODE_LOCAL;  // turn off master broadcast relays during log transmission
  else
    Mode = Settings.MasterSlave;

  snprintf(message,
           MSG_TX_BUFFER_SIZE,
           "%s=%s,%d,%d,%s,%d,%d,%s,%d,%s,%d,%d",
           UI_ProgramPosts[TAG_P2_BASIC].Tag,
           Settings.StorageName,
           Settings.TempType,
           Mode,
           Settings.HomePage,
           Settings.SystemMode,
           Settings.Language,
           Settings.MasterIP,
           Settings.MultiviewSessions,
           Settings.LoginPw,
           Settings.LocalLogin,
           Settings.Animations);

  UI_SendMessage(message, SendOption);
} // end UI_SendBasicSetup()

/***************************************************************************

FUNCTION:   UI_SendBurner()

PURPOSE:    Send the burner settings

COMMENTS:

***************************************************************************/
void UI_SendBurner(int SendOption)
{
  char message[MSG_TX_BUFFER_SIZE];

  snprintf(message,
           MSG_TX_BUFFER_SIZE,
           "%s=%d,%d,%d,%d,%d,%d,%d,%d",
           UI_ProgramPosts[TAG_P2_BURNER].Tag,
           Settings.Burner.On,
           Settings.Burner.Low,
           Settings.Burner.PID.P,
           Settings.Burner.PID.I,
           Settings.Burner.PID.D,
           Settings.Burner.PID.U,
           Settings.Burner.Mode,
           Settings.Burner.Manual);

  UI_SendMessage(message, SendOption);
} // end UI_SendBurner()

/***************************************************************************

FUNCTION:   UI_SendClimacell()

PURPOSE:    Send the climacell settings

COMMENTS:

***************************************************************************/
void UI_SendClimacell(int SendOption)
{
  char message[MSG_TX_BUFFER_SIZE];

  snprintf(message,
           MSG_TX_BUFFER_SIZE,
           "%s=%d,%d,%d,%d,%d,%d,%d",
           UI_ProgramPosts[TAG_P2_CLIMACELL].Tag,
           Settings.Climacell.Efficiency,
           Settings.Climacell.Altitude,
           Settings.Climacell.AltitudeUnits,
           Settings.Climacell.PID.P,
           Settings.Climacell.PID.I,
           Settings.Climacell.PID.D,
           Settings.Climacell.PID.U);

  UI_SendMessage(message, SendOption);
} // end UI_SendClimacell()

/***************************************************************************

FUNCTION:   UI_SendClimacellTimes()

PURPOSE:    Send the climacell clock settings

COMMENTS:

***************************************************************************/
void UI_SendClimacellTimes(int SendOption)
{
  int  i;
  char message[MSG_TX_BUFFER_SIZE];
  char str[20];

  snprintf(message, MSG_TX_BUFFER_SIZE, "%s=", UI_ProgramPosts[TAG_P1_CLIMACELLTIMES].Tag);

  for (i = 0; i < 48; i++)
  {
    snprintf(str, sizeof(str), "%d", Settings.Climacell.Times[i]);
    strncat(message, str, sizeof(message)-strlen(message)-1);
    if (i < 48-1)
      strncat(message, ",", sizeof(message)-strlen(message)-1);
  }

  UI_SendMessage(message, SendOption);
} // end UI_SendClimacellTimes()

/***************************************************************************

FUNCTION:   UI_SendCo2()

PURPOSE:    Send the CO2 purge settings

COMMENTS:

***************************************************************************/
void UI_SendCo2(int SendOption)
{
  char  message[MSG_TX_BUFFER_SIZE];
  char  str[20];

  if (Settings.Co2.Purge.Mode == 2)    // auto
    snprintf(str, sizeof(str), "%d", Settings.Co2.Set);
  else
    snprintf(str, sizeof(str), "%d", Settings.Co2.CylceTime);

  snprintf(message,
           MSG_TX_BUFFER_SIZE,
           "%s=%d,%.1f,%.1f,%d,%s,%d,%d",
           UI_ProgramPosts[TAG_P1_CO2PURGE].Tag,
           Settings.Co2.Purge.Mode,
           Settings.Co2.Purge.MinTemp,
           Settings.Co2.Purge.MaxTemp,
           Settings.Co2.Purge.Duration,
           str,
           Settings.Co2.FanOutput,
           Settings.Co2.DoorOutput);

  UI_SendMessage(message, SendOption);
} // end UI_SendCo2()

/***************************************************************************

FUNCTION:   UI_SendFanBoost()

PURPOSE:    Send the fan boost settings

COMMENTS:

***************************************************************************/
void UI_SendFanBoost(int SendOption)
{
  char  message[MSG_TX_BUFFER_SIZE];

  snprintf(message,
           MSG_TX_BUFFER_SIZE,
           "%s=%d,%d,%d,%d,%.1f",
           UI_ProgramPosts[TAG_P1_FANBOOST].Tag,
           Settings.FanBoost.Mode,
           Settings.FanBoost.Speed,
           Settings.FanBoost.Interval,
           Settings.FanBoost.Duration,
           Settings.FanBoost.Temp);

  UI_SendMessage(message, SendOption);
} // end UI_SendFanBoost()

/***************************************************************************

FUNCTION:   UI_SendDateTime()

PURPOSE:    Send the date & time

COMMENTS:

***************************************************************************/
void UI_SendDateTime(int SendOption)
{
  uint8_t AmPm = 0;
  char message[MSG_TX_BUFFER_SIZE];
  char DateStr[DATE_LEN+1];
  char TimeStr[TIME_LEN+1];

  GetDateStr(DateStr);
  GetTimeStr(TimeStr, &AmPm);

  snprintf(message,
           MSG_TX_BUFFER_SIZE,
           "%s=%s,%s,%d",
           UI_ProgramPosts[TAG_P1_DATETIME].Tag,
           DateStr,
           TimeStr,
           AmPm);

  UI_SendMessage(message, SendOption);
} // end UI_SendDateTime()

/***************************************************************************

FUNCTION:   UI_SendDebug()

PURPOSE:    Send a debug message to the LTX

COMMENTS:

***************************************************************************/
void UI_SendDebug(char *debug)
{
  uint8_t AmPm = 0;
  char message[MSG_TX_BUFFER_SIZE];
  char DateStr[DATE_LEN+1];
  char TimeStr[TIME_LEN+1];

  GetDateStr(DateStr);
  GetTimeStr(TimeStr, &AmPm);

  snprintf(message,
           MSG_TX_BUFFER_SIZE,
           "Debug=%s %s%s %s",
           DateStr,
           TimeStr,
           AmPm == 0 ? "am" : "pm",
           debug);

  UI_SendMessage(message, MSG_QUEUE);
} // end UI_SendDebug()

/***************************************************************************

FUNCTION:   UI_SendDoor()

PURPOSE:    Send the fesh air door settings

COMMENTS:

***************************************************************************/
void UI_SendDoor(int SendOption)
{
  char message[MSG_TX_BUFFER_SIZE];

  snprintf(message,
           MSG_TX_BUFFER_SIZE,
           "%s=%d,%d,%d,%d,%d,%d,%d",
           UI_ProgramPosts[TAG_P2_DOOR].Tag,
           Settings.Door.PID.P,
           Settings.Door.PID.I,
           Settings.Door.PID.D,
           Settings.Door.PID.U,
           Settings.Door.ActuatorTime,
           Settings.Door.CoolAirCycle,
           Settings.Log.PID.Door);

  UI_SendMessage(message, SendOption);
} // end UI_SendDoor()

/***************************************************************************

FUNCTION:   UI_SendEmail()

PURPOSE:    Send email alert settings

COMMENTS:

***************************************************************************/
void UI_SendEmail(int SendOption)
{
  char message[MSG_TX_BUFFER_SIZE] = "EmailAlertData=";
  char temp[MSG_TX_BUFFER_SIZE];

  // initiate the multi-message transfer
  UI_SendMultiHdr("Email,EmailAlertData", NO_SESSIONID);

  snprintf(temp, MSG_TX_BUFFER_SIZE, "%d,", Settings.Email.Alerts);
  MultiMsgAdd(message, "EmailAlertData=", temp, 0);

  snprintf(temp, MSG_TX_BUFFER_SIZE, "%s,", Settings.Email.Server);
  MultiMsgAdd(message, "EmailAlertData=", temp, 0);

  snprintf(temp, MSG_TX_BUFFER_SIZE, "%d,", Settings.Email.AuthType);
  MultiMsgAdd(message, "EmailAlertData=", temp, 0);

  snprintf(temp, MSG_TX_BUFFER_SIZE, "%d,", Settings.Email.Port);
  MultiMsgAdd(message, "EmailAlertData=", temp, 0);

  snprintf(temp, MSG_TX_BUFFER_SIZE, "%s,", Settings.Email.Account);
  MultiMsgAdd(message, "EmailAlertData=", temp, 0);

  snprintf(temp, MSG_TX_BUFFER_SIZE, "%s,", Settings.Email.Password);
  MultiMsgAdd(message, "EmailAlertData=", temp, 0);

  snprintf(temp, MSG_TX_BUFFER_SIZE, "%s,", Settings.Email.Display);
  MultiMsgAdd(message, "EmailAlertData=", temp, 0);

  snprintf(temp, MSG_TX_BUFFER_SIZE, "%s,", Settings.Email.To);
  MultiMsgAdd(message, "EmailAlertData=", temp, 0);

  snprintf(temp, MSG_TX_BUFFER_SIZE, "%s", Settings.Email.From);
  MultiMsgAdd(message, "EmailAlertData=", temp, 0);

  // terminate and send the remaining message
  MultiMsgAdd(message, "", "", 1);

  // terminate the multi-message transfer
  UI_SendMultiEnd("Email");
} // end UI_SendEmail()

/***************************************************************************

FUNCTION:   UI_SendEmailAlertFlags()

PURPOSE:    Send flags that indicate if the alert should be emailed

COMMENTS:

***************************************************************************/
void UI_SendEmailAlertFlags(int SendOption)
{
  char message[MSG_TX_BUFFER_SIZE];

  snprintf(message,
           MSG_TX_BUFFER_SIZE,
           "%s=%s",
           UI_ProgramPosts[TAG_P1_ALERTS].Tag,
           Settings.AlertsToSend);

  UI_SendMessage(message, SendOption);
} // end UI_SendEmailAlertFlags()

/***************************************************************************

FUNCTION:   UI_SendEquipStatus()

PURPOSE:    Send the equipment status

COMMENTS:

***************************************************************************/
void UI_SendEquipStatus(int SendOption)
{
  int  i;
  char EqCavHeat;
  char EquipStatus[EQUIPSTATUS_LEN];
  char message[MSG_TX_BUFFER_SIZE];
  char StatusStr[MSG_TX_BUFFER_SIZE] = "";
  char ExtendedStatusStr[MSG_TX_BUFFER_SIZE] = "";
  char str[20];

  GetEquipStatus(EquipStatus, EQUIPSTATUS_LEN);
  for (i = 0; i < EQUIPSTATUS_LEN; i++)
  {
    snprintf(str, sizeof(str), "%d,", EquipStatus[i]);

    if (i < LIGHTSTATUS_START)
      strncat(StatusStr, str, sizeof(StatusStr)-strlen(StatusStr)-1);
    else
      strncat(ExtendedStatusStr, str, sizeof(ExtendedStatusStr)-strlen(ExtendedStatusStr)-1);
  }

  // equip status: 0 - off, 1 - auto, 2 - manual, 3 - on
  // ARM: 0 - auto/off, 1 - off, 2 - on, 3 - auto
  if (Settings.CavityHeat.Mode < 2)
    EqCavHeat = 0;
  else if (Settings.CavityHeat.Mode == 2)
    EqCavHeat = 3;
  else if (Settings.CavityHeat.Mode == 3)
    EqCavHeat = 1;

  snprintf(message,
           MSG_TX_BUFFER_SIZE,
           "EquipStatus=%s%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,",
           StatusStr,
           EqCavHeat,                            // 36
           Settings.RemoteOff[RO_FAN],           // 37
           Settings.RemoteOff[RO_CLIMACELL],     // 38
           Settings.RemoteOff[RO_HUMIDIFIER1],   // 39
           Settings.RemoteOff[RO_HUMIDIFIER2],   // 40
           Settings.Refrig.Stage[0].Diagnostic,  // 41
           Settings.Refrig.Stage[1].Diagnostic,  // 42
           Settings.Refrig.Stage[2].Diagnostic,  // 43
           Settings.Refrig.Stage[3].Diagnostic,  // 44
           Settings.Refrig.Stage[4].Diagnostic,  // 45
           Settings.Refrig.Stage[5].Diagnostic,  // 46
           Settings.RemoteOff[RO_AUX1],          // 47
           Settings.RemoteOff[RO_HEAT],          // 48
           Settings.RemoteOff[RO_CAVITY_HEAT],   // 49
           Settings.RemoteOff[RO_REFRIGERATION], // 50
           Settings.RemoteOff[RO_BURNER],        // 51
           Settings.RemoteOff[RO_CURE],          // 52
           Settings.RemoteOff[RO_AUX2],          // 53
           Settings.RemoteOff[RO_LIGHTS1],       // 54
           Settings.RemoteOff[RO_LIGHTS2]);      // 55

//  snprintf(message, MSG_TX_BUFFER_SIZE, "%s%s", message, ExtendedStatusStr);
  strncat(message, ExtendedStatusStr, sizeof(message)-strlen(message)-1);

  snprintf(ExtendedStatusStr,
           MSG_TX_BUFFER_SIZE,
           "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,",
           Settings.Refrig.Stage[6].Diagnostic,    // 89
           Settings.Refrig.Stage[7].Diagnostic,    // 90
           Settings.Refrig.Defrost[0].Diagnostic,  // 91
           Settings.Refrig.Defrost[1].Diagnostic,  // 92
           Settings.RemoteOff[RO_HUMIDIFIER3],     // 93
           Settings.RemoteOff[RO_AUX3],            // 94
           Settings.RemoteOff[RO_AUX4],            // 95
           Settings.RemoteOff[RO_AUX5],            // 96
           Settings.RemoteOff[RO_AUX6],            // 97
           Settings.RemoteOff[RO_AUX7],            // 98
           Settings.RemoteOff[RO_AUX8]);           // 99

//  snprintf(message, MSG_TX_BUFFER_SIZE, "%s%s", message, ExtendedStatusStr);
  strncat(message, ExtendedStatusStr, sizeof(message)-strlen(message)-1);

  UI_SendMessage(message, SendOption);
} // end UI_SendEquipStatus()

/***************************************************************************

FUNCTION:   UI_SendFileTransferAck()

PURPOSE:    Send the file transfer response

COMMENTS:

***************************************************************************/
void UI_SendFileTransferAck(int response)
{
  char message[MSG_TX_BUFFER_SIZE];

  snprintf(message,
           MSG_TX_BUFFER_SIZE,
           "%s=%d",
           UI_ActionPosts[TAG_A_FILESTART].Tag,
           response);

  SendMsgAndNoWait(message);
} // end UI_SendFileTransferAck()

/***************************************************************************

FUNCTION:   UI_SendEquipTranslateAck()

PURPOSE:    Send the equipment name translated response

COMMENTS:

***************************************************************************/
void UI_SendEquipTranslateAck(int SendOption)
{
  char message[MSG_TX_BUFFER_SIZE];

  snprintf(message,
           MSG_TX_BUFFER_SIZE,
           "%s=%s",
           UI_ProgramPosts[TAG_EQUIPDESC].Tag,
           "0");

  SendMsgAndNoWait(message);
} // end UI_SendEquipTranslateAck()

/***************************************************************************

FUNCTION:   UI_SendFailures()

PURPOSE:    Send the failures(1) page settings

COMMENTS:

***************************************************************************/
void UI_SendFailures(int SendOption)
{
  char message[MSG_TX_BUFFER_SIZE];

  snprintf(message,
           MSG_TX_BUFFER_SIZE,
           "%s=%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
           UI_ProgramPosts[TAG_P2_FAIL1].Tag,
           Settings.Failure[FAIL_FAN].Mode,              // 0
           Settings.Failure[FAIL_FAN].Timer,
           Settings.Failure[FAIL_CLIMACELL].Mode,
           Settings.Failure[FAIL_CLIMACELL].Timer,
           Settings.Failure[FAIL_REFRIGERATION].Mode,
           Settings.Failure[FAIL_REFRIGERATION].Timer,   // 5
           Settings.Refrig.FailMode,
           Settings.Failure[FAIL_REFRIG_STAGES].Mode,
           Settings.Failure[FAIL_REFRIG_STAGES].Timer,
           Settings.Failure[FAIL_HUMIDIFIERS].Mode,
           Settings.Failure[FAIL_HUMIDIFIERS].Timer,     // 10
           Settings.Failure[FAIL_AUXILIARY].Mode,
           Settings.Failure[FAIL_AUXILIARY].Timer,
           Settings.Failure[FAIL_HEAT].Mode,
           Settings.Failure[FAIL_HEAT].Timer,
           Settings.Failure[FAIL_CAVITY_HEAT].Mode,      // 15
           Settings.Failure[FAIL_CAVITY_HEAT].Timer,
           Settings.Failure[FAIL_BURNER].Mode,
           Settings.Failure[FAIL_BURNER].Timer,
           Settings.Failure[FAIL_LIGHTS].Mode,
           Settings.Failure[FAIL_LIGHTS].Timer,          // 20
           Settings.LightsFailUnits);

  UI_SendMessage(message, SendOption);
} // end UI_SendFailures()

/***************************************************************************

FUNCTION:   UI_SendFailures2()

PURPOSE:    Send the failures(2) page settings

COMMENTS:

***************************************************************************/
void UI_SendFailures2(int SendOption)
{
  char message[MSG_TX_BUFFER_SIZE];

  snprintf(message,
           MSG_TX_BUFFER_SIZE,
           "%s=%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%.1f",
           UI_ProgramPosts[TAG_P2_FAIL2].Tag,
           Settings.Failure[FAIL_OUTSIDE_AIR].Mode,
           Settings.Failure[FAIL_OUTSIDE_AIR].Timer,
           Settings.Failure[FAIL_OUTSIDE_HUMIDITY].Mode,
           Settings.Failure[FAIL_OUTSIDE_HUMIDITY].Timer,
           Settings.Failure[FAIL_HIGH_CO2].Mode,
           Settings.Failure[FAIL_HIGH_CO2].Timer,
           Settings.Co2.HighFailure,
           Settings.Failure[FAIL_PLENUM_HUMIDITY].Mode,
           Settings.Failure[FAIL_PLENUM_HUMIDITY].Timer,
           Settings.Plenum.HumidLowFailure,
           Settings.Failure[FAIL_PLENUM_SENSOR].Mode,
           Settings.Failure[FAIL_PLENUM_SENSOR].Timer,
           Settings.Plenum.SensorDiff);

  UI_SendMessage(message, SendOption);
} // end UI_SendFailures2()

/***************************************************************************

FUNCTION:   UI_SendFanDailyRun()

PURPOSE:    Send the daily fan run time

COMMENTS:

***************************************************************************/
void UI_SendFanDailyRun(int SendOption)
{
  char message[MSG_TX_BUFFER_SIZE];

  snprintf(message,
           MSG_TX_BUFFER_SIZE,
           "%s=%.1f",
           UI_ProgramPosts[TAG_P1_FANDAILY].Tag,
           Settings.Fan.DailyRunTime/3600.0);

  UI_SendMessage(message, SendOption);
} // end UI_SendFanDailyRun()

/***************************************************************************

FUNCTION:   UI_SendFanTotalRun()

PURPOSE:    Send the total fan run times

COMMENTS:

***************************************************************************/
void UI_SendFanTotalRun(int SendOption)
{
  char message[MSG_TX_BUFFER_SIZE];

  snprintf(message,
           MSG_TX_BUFFER_SIZE,
           "%s=%.1f,%.1f,%.1f,%.1f,%.1f,%.1f",
           UI_ProgramPosts[TAG_P1_FANTOTAL].Tag,
           Settings.Fan.TotalRunTime/3600.0,
           Settings.Fan.TotalRefrigTime/3600.0,
           Settings.Fan.TotalCoolingTime/3600.0,
           Settings.Fan.TotalRecircTime/3600.0,
           Settings.Fan.TotalCureTime/3600.0,
           Settings.Fan.TotalStandbyTime/3600.0);

  UI_SendMessage(message, SendOption);
} // end UI_SendFanTotalRun()

/***************************************************************************

FUNCTION:   UI_SendFanSpeed()

PURPOSE:    Send the fan speed settings

COMMENTS:

***************************************************************************/
void UI_SendFanSpeed(int SendOption)
{
  char message[MSG_TX_BUFFER_SIZE];

  snprintf(message,
           MSG_TX_BUFFER_SIZE,
           "%s=%d,%d,%d,%d,%d,%.1f,%d,%d,%d,%d",
           UI_ProgramPosts[TAG_P1_FANSPEEDS].Tag,
           Settings.Fan.MaxSpeed,
           Settings.Fan.MinSpeed,
           Settings.Fan.RefrigSpeed,
           Settings.Fan.RecircSpeed,
           Settings.Fan.UpdatePeriod,
           Settings.Fan.TempDiff,
           Settings.Fan.TempRef1,
           Settings.Fan.TempRef2,
           Settings.Fan.PrevSpeed,
           Settings.Fan.UpdateMode);

  UI_SendMessage(message, SendOption);
} // end UI_SendFanSpeed()

/***************************************************************************

FUNCTION:   UI_SendGraphFavorites()

PURPOSE:    Send the graph favorites

COMMENTS:

***************************************************************************/
void UI_SendGraphFavorites(int SendOption)
{
  char message[MSG_TX_BUFFER_SIZE];

  snprintf(message,
           MSG_TX_BUFFER_SIZE,
           "%s=%s",
           UI_ProgramPosts[TAG_GRAPHFAVORITES].Tag,
           Settings.Log.GraphFavorites);

  UI_SendMessage(message, SendOption);
} // end UI_SendGraphFavorites()

/***************************************************************************

FUNCTION:   UI_SendHttpPort()

PURPOSE:    Send http port number

COMMENTS:   Because the LTX doesn't retain its http port setting, the ARM
            must save the port number and then send it to the LTX to be set
            on startup. So as long LTX is supported this function will
            need to be used.  When LTX is no longer supported, the function
            UI_SendPublicAddress() could be used instead.

***************************************************************************/
void UI_SendHttpPort(int SendOption)
{
  char message[MSG_TX_BUFFER_SIZE];

  snprintf(message,
           MSG_TX_BUFFER_SIZE,
           "%s=%d,%s",
           UI_ProgramPosts[TAG_P1_HTTPPORT].Tag,
           Settings.HttpPort,
           Settings.PublicIP);

  UI_SendMessage(message, SendOption);
} // end UI_SendHttpPort()

/***************************************************************************

FUNCTION:   UI_SendHumCtrl()

PURPOSE:    Send humidity control settings

COMMENTS:

***************************************************************************/
void UI_SendHumCtrl(int SendOption)
{
  int i;
  char subMessage[100];
  char message[MSG_TX_BUFFER_SIZE];

  snprintf(message,
           MSG_TX_BUFFER_SIZE,
           "%s=",
           UI_ProgramPosts[TAG_P1_HUMIDCTRL].Tag);

  for (i = 0; i < NUM_HUMIDIFIERS; i++)
  {
    snprintf(subMessage,
             sizeof(subMessage),
             "%d,%d,%d,%d,%d,%d,%d,",
             Settings.HumidCtrl[i].Mode,
             Settings.HumidCtrl[i].DutyCycle[HUMID_MODE_COOL].On,
             Settings.HumidCtrl[i].DutyCycle[HUMID_MODE_COOL].Off,
             Settings.HumidCtrl[i].DutyCycle[HUMID_MODE_RECIRC].On,
             Settings.HumidCtrl[i].DutyCycle[HUMID_MODE_RECIRC].Off,
             Settings.HumidCtrl[i].DutyCycle[HUMID_MODE_REFRIG].On,
             Settings.HumidCtrl[i].DutyCycle[HUMID_MODE_REFRIG].Off);
    strncat(message, subMessage, sizeof(message)-strlen(message)-1);
  }

  UI_SendMessage(message, SendOption);
} // end UI_SendHumCtrl()

/***************************************************************************

FUNCTION:   UI_SendHumidModes()

PURPOSE:    Send the humidifier and climacell modes

COMMENTS:

***************************************************************************/
void UI_SendHumidModes(void)
{
  char message[MSG_TX_BUFFER_SIZE];

  snprintf(message,
           MSG_TX_BUFFER_SIZE,
           "HumidModes=%d,%d,%d,%d",
           Settings.RemoteOff[RO_HUMIDIFIER1] ? 3 : Settings.HumidCtrl[0].Mode,
           Settings.RemoteOff[RO_HUMIDIFIER2] ? 3 : Settings.HumidCtrl[1].Mode,
           Settings.RemoteOff[RO_CLIMACELL]   ? 5 : ClimacellClockMode(),
           Settings.RemoteOff[RO_HUMIDIFIER3] ? 3 : Settings.HumidCtrl[2].Mode);

  QueueMessage(message);
} // end UI_SendHumidModes()

/***************************************************************************

FUNCTION:   UI_SendIoConfig()

PURPOSE:    Send the I/O configuration

COMMENTS:

***************************************************************************/
void UI_SendIoConfig(int SendOption)
{
  int i;
  char str[20];
  char output[MSG_TX_BUFFER_SIZE] = "OutputConfig=";
  char input[MSG_TX_BUFFER_SIZE] = "InputConfig=";

  for (i = 0; i < SW_START_STOP; i++)
  {
    snprintf(str, sizeof(str), "%d,", Settings.EquipIo[i].Output);
    strncat(output, str, sizeof(output)-strlen(output)-1);

    snprintf(str, sizeof(str), "%d,", Settings.EquipIo[i].Input);
    strncat(input, str, sizeof(input)-strlen(input)-1);
  }

  UI_SendMessage(output, SendOption);
  UI_SendMessage(input, SendOption);
} // UI_SendIoConfig()

/***************************************************************************

FUNCTION:   UI_SendIoDefinition()

PURPOSE:    Send the I/O definition

COMMENTS:

***************************************************************************/
void UI_SendIoDefinition(int SendOption)
{
  int i;
  char message[MSG_TX_BUFFER_SIZE] = "IoNames=";
  char temp[BOARD_LABELS+20];

  // initiate the multi-message transfer
  UI_SendMultiHdr("IoDefinition,IoNames", NO_SESSIONID);

  for (i = 0; i < EQ_TOTAL_IO; i++)
  {
    snprintf(temp,
             sizeof(temp),
             "%s:%d:%d:%d:%d,",
             Settings.EquipIo[i].Name,
             Settings.EquipIo[i].Mode,
             Settings.EquipIo[i].IO,
             Settings.EquipIo[i].Renamable,
             i); // i = EQ_REFRIG_STAGE1, etc.

    MultiMsgAdd(message, "IoNames=", temp, 0);
  }

  // terminate and send the remaining message
  MultiMsgAdd(message, "", "", 1);

  // terminate the multi-message transfer
  UI_SendMultiEnd("IoDefinition");
} // UI_SendIoDefinition()

/***************************************************************************

FUNCTION:   UI_SendLoadMonitor()

PURPOSE:    Send loading monitor setup

COMMENTS:

***************************************************************************/
void UI_SendLoadMonitor(int SendOption)
{
  char message[MSG_TX_BUFFER_SIZE];
  char irSensor0 = Settings.LoadMonitor.IrSensorsAvailable[0];
  char irSensor1 = Settings.LoadMonitor.IrSensorsAvailable[1];

  snprintf(message,
           MSG_TX_BUFFER_SIZE,
           "%s=%s,%s,%d,%d,%s,%d,%s,%d,%d,%d,%d,%d,%.1f,%.1f,%u",
           UI_ProgramPosts[TAG_P1_LOADMONITOR].Tag,
           Settings.LoadMonitor.Bay[0].Label,
           Settings.LoadMonitor.Bay[1].Label,
           Settings.LoadMonitor.Bay[0].SensorID,
           Settings.LoadMonitor.Bay[1].SensorID,
           Settings.AnalogBoard[irSensor0/ANALOG_SENSORS_PER_BOARD].Sensor[irSensor0 % ANALOG_SENSORS_PER_BOARD].Label,
           irSensor0,
           Settings.AnalogBoard[irSensor1/ANALOG_SENSORS_PER_BOARD].Sensor[irSensor1 % ANALOG_SENSORS_PER_BOARD].Label,
           irSensor1,
           Settings.LoadMonitor.Bay[0].Pipe,
           Settings.LoadMonitor.Bay[1].Pipe,
           Settings.LoadMonitor.Bay[0].Status,
           Settings.LoadMonitor.Bay[1].Status,
           Settings.LoadMonitor.AlarmTemp[0],
           Settings.LoadMonitor.AlarmTemp[1],
           SDCardHeader.LoadLog.NumRecords + LoadLogUnwrittenRecords());

  UI_SendMessage(message, SendOption);
} // end UI_SendLoadMonitor()

/***************************************************************************

FUNCTION:   UI_SendLtxInit()

PURPOSE:    Send LTX initialization response

COMMENTS:

***************************************************************************/
void UI_SendLtxInit(int SendOption)
{
  char message[MSG_TX_BUFFER_SIZE];

  snprintf(message,
           MSG_TX_BUFFER_SIZE,
           "%s=%s,%s",
           UI_ProgramPosts[TAG_P1_LTXINIT].Tag,
           ARM_FIRMWARE_VERSION,
           BOARD_TYPE);

  UI_SendMessage(message, SendOption);
} // end UI_SendLtxInit()

/***************************************************************************

FUNCTION:   UI_SendMain()

PURPOSE:    Send the main page (system monitor) data

COMMENTS:   Tested with boards 0 & 1 missing
            board 0 not temp, board 1 not humid

***************************************************************************/
void UI_SendMain(int SendOption)
{
  char BurnerOutput[10] = "--";
  char Co2[10] = "--";
  char CoolLabel[2] = "0";
  char CoolOutput[10] = "--";
  char FanSpeed[10] = "--";
  char message[MSG_TX_BUFFER_SIZE];
  char OutsideHumid[HUMID_LEN+1] = "--";
  char OutsideTemp[TEMP_LEN+1] = "--";
  char PlenumHumid[HUMID_LEN+1] = "--";
  char PlenumTemp[TEMP_LEN+1] = "--";
  char ReturnHumid[HUMID_LEN+1] = "--";
  char ReturnTemp[TEMP_LEN+1] = "--";
  char StartTempStr[TEMP_LEN+1] = "--";
  char RemoteTemp = 0;
  char RemoteHumid = 0;
  char CalcHumidStr[HUMID_LEN+1] = "--";
  int  CalcHumid = CalculatedHumidity();
  float PWMPercent = 0;

  FormatDefaultTempBoard(PlenumTemp, OutsideTemp, ReturnTemp, &RemoteTemp);
  FormatDefaultHumidBoard(PlenumHumid, OutsideHumid, ReturnHumid, Co2, &RemoteHumid);
  FormatFanSpeed(FanSpeed);
  FormatCoolingOutput(CoolOutput, CoolLabel, BurnerOutput);

  if (CalcHumid != SENSOR_VAL_UNDEFINED)
    snprintf(CalcHumidStr, HUMID_LEN+1, "%d", CalcHumid);

  if (   Settings.SystemMode == SM_ONION
      && CheckInputs(SW_CURE_AUTO)
      && Settings.RemoteOff[RO_CURE] == 0)
  {
    PWMPercent = PWMValToPercent(PwmChannel[PWM_DOORS].Output);
    snprintf(StartTempStr, TEMP_LEN+1, "%.0f", PWMPercent);
  }
  else
  {
    // start temperature (cooling avail) (sets warning in failures.c)
    if (StartTemp != SENSOR_VAL_UNDEFINED)
      snprintf(StartTempStr, TEMP_LEN+1, "%.1f", StartTemp);
  }

  snprintf(message,
           MSG_TX_BUFFER_SIZE,
           "main=%s,%s,%s,%d,%s,%d,%s,%s,%s,%s,%s,%s,%s,%s,%s",
           PlenumTemp,       // 0
           PlenumHumid,      // 1
           OutsideTemp,      // 2
           RemoteTemp,       // 3
           OutsideHumid,     // 4
           RemoteHumid,      // 5
           StartTempStr,     // 6
           ReturnHumid,      // 7
           ReturnTemp,       // 8
           Co2,              // 9
           FanSpeed,         // 10
           CoolOutput,       // 11
           CoolLabel,        // 12
           BurnerOutput,     // 13
           CalcHumidStr);    // 14

  UI_SendMessage(message, SendOption);
} // end UI_SendMain()

/***************************************************************************

FUNCTION:   UI_SendMisc()

PURPOSE:    Send miscellaneous page settings

COMMENTS:

***************************************************************************/
void UI_SendMisc(int SendOption)
{
  char  message[MSG_TX_BUFFER_SIZE];
  char  secure;
  char  status;
  char  type;
  char  pref;
  float temp;

  if (Settings.SystemMode == SM_POTATO)
    temp = Settings.HeatTempThresh;
  else
    temp = Settings.Refrig.Limit;

  if (Settings.KbPref < 2)
  {
    secure = 0;
    pref = Settings.KbPref;
  }
  else
  {
    secure = 1;
    pref = Settings.KbPref - 2;
  }

  status = Settings.CavityHeat.Mode;    // select list - off, on, auto
  type = Settings.CavityHeat.Mode;      // type - duty cycle or auto
  if (Settings.CavityHeat.Mode == 0)   // encoded
  {
    status = 1;   // select list - off
    type = 3;     // type - auto
  }

  snprintf(message,
           MSG_TX_BUFFER_SIZE,
           "%s=%d,%d,%d,%.1f,%d,%d,%.1f,%d,%d,%d,%d",
           UI_ProgramPosts[TAG_P1_MISC].Tag,
           Settings.Refrig.Mode,             // 0
           Settings.Refrig.DefrostPeriod,    // 1
           Settings.Refrig.DefrostDuration,  // 2
           temp,                             // 3
           status,                           // 4
           type,                             // 5
           Settings.CavityHeat.Diff,         // 6
           Settings.CavityHeat.DutyCycle,    // 7
           pref,                             // 8
           secure,                           // 9
           Settings.CavityHeat.Label);

  UI_SendMessage(message, SendOption);
} // end UI_SendMisc()

/***************************************************************************

FUNCTION:   UI_SendMode()

PURPOSE:    Send the current mode

COMMENTS:

***************************************************************************/
void UI_SendMode(void)
{
  char message[MSG_TX_BUFFER_SIZE];

  snprintf(message, MSG_TX_BUFFER_SIZE, "CurrentMode=%d", CurrentMode);
  QueueMessage(message);
} // end UI_SendMode()

/***************************************************************************

FUNCTION:   UI_SendMultiEnd()

PURPOSE:    Send the multi-message end message

COMMENTS:   For explanation of multi-messages, see UI_SendMultiHdr() below.

***************************************************************************/
void UI_SendMultiEnd(char *Type)
{
  char message[MSG_TX_BUFFER_SIZE];

  snprintf(message, MSG_TX_BUFFER_SIZE, "MultiEnd=%s", Type);
  SendMsgAndWaitForResponse(message, UI_DELAY_STANDARD);
} // end UI_SendMultiEnd()

/***************************************************************************

FUNCTION: UI_SendMultiHdr()

PURPOSE:  Start multi-message sequence

COMMENTS: The multi-message structure is set up for potentially large messages
          with an indeterminate size like sensor messages, warnings, and log
          data.

          The mechanism for multi-messaging is for the ARM to send a message
          with the MultiMsg tag followed by the multi-message type (LogData,
          Warning, etc.) followed by the message tags contained in that particular
          multi-message sequence.  For example, the ARM will call
          UI_SendMultiHdr("LogData,Key,Dates,TimeStamps,Data,Warnings", SessionID)
          which will create the message
            MultiMsg=0,LogData,Key,Dates,TimeStamps,Data,Warnings.

          An array of structures is then created with each element containing
          the message tag (Key, Dates, etc.) and a pointer to the linked list
          of message "vars" (CGI vars).

          When a message comes in, it is first
          checked against the array of standard CGI tags/messages.  If it can't
          be found there, it is checked against the multi-message structures.
          If it is found, then it's var string (it's message payload) is added
          to the appropriate linked list.

          When a web page perforns a 'get', GetMultiMsgData() traverses these
          linked listed writing each one out to the CGI file (HttpWriteData)
          essentially creating one long var in the CGI file for the web page
          to read, parse, and display.

          Each time a new MultiMsg message is received, the existing linked list
          for that multi-message type is freed.  This obviously prevents the
          lists from continually growing.  It also makes each multi-message
          sequence behave like a standard message where each time it is sent, it
          overwrites the previous message.

          NOTE: Only the UI_SendMultiHdr() calls associated with the logs pass
          a valid SessionID.  It is used by the LTX to notify the UI if too
          much data was requested and the LTX ran out of memory to malloc.
          Because only the logs have the potential to send enough data to cause
          this, the LTX only checks for memory errors when receiving log data
          so it's not necessary to send it with the types of mult-messages.
          They send the NO_SESSIONID value instead.

***************************************************************************/
void UI_SendMultiHdr(char *TagList, int SessionID)
{
  char message[MSG_TX_BUFFER_SIZE];
  unsigned int StartTimeMsg;

  snprintf(message,
           MSG_TX_BUFFER_SIZE,
           "MultiMsg=%d,%s",
           SessionID,
           TagList);

  StartTimeMsg = uptime_ms;
  while (   (uptime_ms - StartTimeMsg) < 2000U
         && UI_Message.State != MSG_STATE_WAITING)
    {
      vTaskDelay(10/portTICK_RATE_MS);

      // check for message from LTX/UI
      if (Usart_CharsBuffered())
      {
//        debug_printf("UI_SendMultiHdr - Check for response\r\n");
        if (Usart_ProcessBuffer(UI_Message.RxBuffer) == 1)
        {
          ProcessUIMessage();
        }
      }
//      debug_printf("UI_SendMultiHdr - Looping:%u\r\n",uptime_ms);

      // reset the watchdog external timer
      ThreadMonitorUpdate(TM_UI_UPDATE);
    }

  if (UI_Message.State == MSG_STATE_WAITING)
  {
    SendMsgAndWaitForResponse(message, UI_DELAY_STANDARD);
  }
} // end UI_SendMultiHdr()

/***************************************************************************

FUNCTION:   UI_SendNetMonMode()

PURPOSE:    Send network monitor mode

COMMENTS:

***************************************************************************/
void UI_SendNetMonMode(int SendOption)
{
  char message[MSG_TX_BUFFER_SIZE];

  snprintf(message,
           MSG_TX_BUFFER_SIZE,
           "%s=%d",
           "NetMonitorMode",
           Settings.NetMonitorMode);

  UI_SendMessage(message, SendOption);
} // end UI_SendNetMonMode()

/***************************************************************************

FUNCTION:   UI_SendNetworkNodes()

PURPOSE:    Send network (remote) nodes

COMMENTS:

***************************************************************************/
void UI_SendNetworkNodes(int SendOption)
{
  int i;
  char message[MSG_TX_BUFFER_SIZE] = "P2NodeSetupData=";
  char temp[MSG_TX_BUFFER_SIZE];

  // initiate the multi-message transfer
  UI_SendMultiHdr("Network,P2NodeSetupData", NO_SESSIONID);

  for (i = 0; i < NUM_NETWORK_NODES; i++)
  {
    if (strcmp(Settings.Node[i].ID, "") != 0)
    {
      snprintf(temp,
               MSG_TX_BUFFER_SIZE,
               "%s,%s,%d,%s,",
               Settings.Node[i].ID,
               Settings.Node[i].IP,
               Settings.Node[i].HttpPort,
               Settings.Node[i].PublicIP);
      MultiMsgAdd(message, "P2NodeSetupData=", temp, 0);
    }
  }

  // terminate and send the remaining message
  MultiMsgAdd(message, "", "", 1);

  // terminate the multi-message transfer
  UI_SendMultiEnd("Network");
} // end UI_SendNetworkNodes()

/***************************************************************************

FUNCTION:   UI_SendOutsideAir()

PURPOSE:    Send outside air settings

COMMENTS:

***************************************************************************/
void UI_SendOutsideAir(int SendOption)
{
  char message[MSG_TX_BUFFER_SIZE];

  snprintf(message,
           MSG_TX_BUFFER_SIZE,
           "%s=%.1f,%d,%d,%d,%d",
           UI_ProgramPosts[TAG_P1_OUTSIDEAIR].Tag,
           Settings.OutsideAir.Diff,
           Settings.OutsideAir.AboveBelow,
           Settings.OutsideAir.TempRef,
           Settings.OutsideAir.CalcHumidMax,
           Settings.OutsideAir.CtrlMode);

  UI_SendMessage(message, SendOption);
} // end UI_SendOutsideAir()

/***************************************************************************

FUNCTION:   UI_SendPassword()

PURPOSE:    Send user passwords

COMMENTS:

***************************************************************************/
void UI_SendPassword(int SendOption)
{
  int i;
  char message[MSG_TX_BUFFER_SIZE];

  snprintf(message,
           MSG_TX_BUFFER_SIZE,
           "Passwords=%s",
           Settings.User[0].Password);

  for (i = 1; i < NUM_PASSWORDS; i++)
  {
    strncat(message, ",", sizeof(message)-strlen(message)-1);
    strncat(message, Settings.User[i].Password, sizeof(message)-strlen(message)-1);
  }

  UI_SendMessage(message, SendOption);
} // end UI_SendPassword()

/***************************************************************************

FUNCTION:   UI_SendPgmLevel()

PURPOSE:    Send the program authentication level

COMMENTS:

***************************************************************************/
void UI_SendPgmLevel(int SendOption)
{
  char message[MSG_TX_BUFFER_SIZE];

  snprintf(message,
           MSG_TX_BUFFER_SIZE,
           "%s=%d,%d",
           UI_ProgramPosts[TAG_P1_PASSWORD].Tag,
           LtxPgmLevel,
           LtxHttpSession);

  UI_SendMessage(message, SendOption);
} // end UI_SendPgmLevel()

/***************************************************************************

FUNCTION:   UI_SendPlenSetPoints()

PURPOSE:    Send plenum temperature & humidity set points

COMMENTS:

***************************************************************************/
void UI_SendPlenSetPoints(int SendOption)
{
  char message[MSG_TX_BUFFER_SIZE];
  char threshold[10];

  if (Settings.Burner.Mode == BURNER_MANUAL)
  {
    // in manual mode this value is used as the restart temp
    // so it needs to be formatted with one decimal
    snprintf(threshold, sizeof(threshold), "%.1f", Settings.Burner.Threshold);
  }
  else
  {
    // in auto modes this value is a percent output
    // so it needs to be formatted with no decimal
    snprintf(threshold, sizeof(threshold), "%.0f", Settings.Burner.Threshold);
  }

  snprintf(message,
           MSG_TX_BUFFER_SIZE,
           "%s=%.1f,%d,%d,%.1f,%s",
           UI_ProgramPosts[TAG_P1_PLENSETUP].Tag,
           Settings.Plenum.TempSet,
           Settings.Plenum.HumidSet,
           Settings.Plenum.HumidSetpointRef,
           Settings.Burner.TempSet,
           threshold);

  UI_SendMessage(message, SendOption);
} // end UI_SendPlenSetPoints()

/***************************************************************************

FUNCTION:   UI_SendPWMChannels()

PURPOSE:    Send the PWM channels

COMMENTS:   The UI displays outputs as 1-4 instead of 0-3, thus the +1

***************************************************************************/
void UI_SendPWMChannels(int SendOption)
{
  int i;
  char str[50];
  char message[MSG_TX_BUFFER_SIZE];

  snprintf(message, MSG_TX_BUFFER_SIZE, "%s=", UI_ProgramPosts[TAG_P2_PWMCHANNEL].Tag);

  for (i = 0; i < PWM_TOTAL_EQ; ++i)
  {
    snprintf(str,
             sizeof(str),
             "%s:%d:%d:%d,",
             Settings.PWM[i].Name,
             Settings.PWM[i].Mode,
             Settings.PWM[i].Channel == 255 ? -1 : Settings.PWM[i].Channel,
             i);
    strncat(message, str, sizeof(message)-strlen(message)-1);
  }

UI_SendMessage(message, SendOption);
} // end UI_SendPWMChannels()

/***************************************************************************

FUNCTION:   UI_SendRampRate()

PURPOSE:    Send the temperature ramping settings

COMMENTS:

***************************************************************************/
void UI_SendRampRate(int SendOption)
{
  char message[MSG_TX_BUFFER_SIZE];
  char str[20];

  if (Settings.Ramp.UpdatePeriod== 255)
    strcpy(str, "Automatically");
  else
    snprintf(str, sizeof(str), "%d", Settings.Ramp.UpdatePeriod);

  snprintf(message,
           MSG_TX_BUFFER_SIZE,
           "%s=%.1f,%s,%.1f,%d,%.1f",
           UI_ProgramPosts[TAG_P1_RAMPRATE].Tag,
           Settings.Ramp.UpdateTemp,
           str,
           Settings.Ramp.TempDiff,
           Settings.Ramp.TempRef,
           Settings.Ramp.TargetTemp);

  UI_SendMessage(message, SendOption);
} // end UI_SendRampRate()

/***************************************************************************

FUNCTION:   UI_SendRefrig()

PURPOSE:    Send refrigeration settings

COMMENTS:

***************************************************************************/
void UI_SendRefrig(int SendOption)
{
  char message[MSG_TX_BUFFER_SIZE];

  snprintf(message,
           MSG_TX_BUFFER_SIZE,
           "%s=%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d",
           UI_ProgramPosts[TAG_P2_REFRIG].Tag,
           Settings.Refrig.Stage[0].On,
           Settings.Refrig.Stage[0].Off,
           Settings.Refrig.Stage[1].On,
           Settings.Refrig.Stage[1].Off,
           Settings.Refrig.Stage[2].On,
           Settings.Refrig.Stage[2].Off,
           Settings.Refrig.Stage[3].On,
           Settings.Refrig.Stage[3].Off,
           Settings.Refrig.Stage[4].On,
           Settings.Refrig.Stage[4].Off,
           Settings.Refrig.Stage[5].On,
           Settings.Refrig.Stage[5].Off,
           Settings.Refrig.PID.P,
           Settings.Refrig.PID.I,
           Settings.Refrig.PID.D,
           Settings.Refrig.PID.U,
           Settings.Refrig.Purge,
           Settings.Co2.Purge.RefrigThresh,
           Settings.Log.PID.Refrig,
           Settings.Refrig.Stage[6].On,
           Settings.Refrig.Stage[6].Off,
           Settings.Refrig.Stage[7].On,
           Settings.Refrig.Stage[7].Off);

  UI_SendMessage(message, SendOption);
} // end UI_SendRefrig()

/***************************************************************************

FUNCTION:   UI_SendRuntimes()

PURPOSE:    Send the run clock settings

COMMENTS:

***************************************************************************/
void UI_SendRuntimes(int SendOption)
{
  int  i;
  char message[MSG_TX_BUFFER_SIZE];
  char str[20];
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

  snprintf(message, MSG_TX_BUFFER_SIZE, "%s=", UI_ProgramPosts[TAG_P1_RUNTIMES].Tag);

  for (i = 0; i < 48; i++)
  {
    snprintf(str, sizeof(str), "%d", RunTimes[i]);
    strncat(message, str, sizeof(message)-strlen(message)-1);
    if (i < 48-1)
    {
      strncat(message, ",", sizeof(message)-strlen(message)-1);
    }
  }

  UI_SendMessage(message, SendOption);
} // end UI_SendRuntimes()

/***************************************************************************

FUNCTION:   UI_SendSensorData()

PURPOSE:    Send the sensor readings

COMMENTS:   Tested with 6 boards with sensor labels long enough to cause overflow.

***************************************************************************/
void UI_SendSensorData(void)
{
  int  i,j;
  int  HumidsAdded = 0;
  char HumidsMsg[MSG_TX_BUFFER_SIZE] = "PileHumidsData=";
  int  TempsAdded = 0;
  char TempsMsg[MSG_TX_BUFFER_SIZE] = "PileTempsData=";
  char str[20];
  char temp[MSG_TX_BUFFER_SIZE];

  // initiate the multi-message transfer
  UI_SendMultiHdr("SensorData,PileTempsData,PileHumidsData", NO_SESSIONID);

  // NOTE: starts with board 3
  for (i = 2; i < ANALOG_BOARDS_PER_SYSTEM; i++)
  {
    if (Settings.AnalogBoard[i].Present == 1 && Settings.AnalogBoard[i].Disabled == 0)
    {
      for (j = 0; j < ANALOG_SENSORS_PER_BOARD; j++)
      {
        if (Settings.AnalogBoard[i].Sensor[j].Disabled == 0)
        {
          if (   Settings.AnalogBoard[i].Sensor[j].Type == ANALOG_SENSOR_TYPE_TEMP
              || Settings.AnalogBoard[i].Sensor[j].Type == ANALOG_SENSOR_TYPE_TEMP_IR)
          {
            if (TempsAdded > 0)
              strcpy(temp, ",");
            else
              strcpy(temp, "");

            if (Settings.AnalogBoard[i].Sensor[j].Value == SENSOR_VAL_UNDEFINED)
              strcpy(str, "--");
            else
              snprintf(str, sizeof(str), "%.1f", Settings.AnalogBoard[i].Sensor[j].Value);

            strncat(temp, str, sizeof(temp)-strlen(temp)-1);
            strncat(temp, ",0", sizeof(temp)-strlen(temp)-1);   // UI units index
            MultiMsgAdd(TempsMsg, "PileTempsData=", temp, 0);
            TempsAdded++;
          }
          else
          {
            if (HumidsAdded > 0)
              strcpy(temp, ",");
            else
              strcpy(temp, "");

            if (Settings.AnalogBoard[i].Sensor[j].Value == SENSOR_VAL_UNDEFINED)
              strcpy(str, "--");
            else
              snprintf(str, sizeof(str), "%.0f", Settings.AnalogBoard[i].Sensor[j].Value);

            strncat(temp, str, sizeof(temp)-strlen(temp)-1);
            if (Settings.AnalogBoard[i].Sensor[j].Type == ANALOG_SENSOR_TYPE_HUMID)
              strncat(temp, ",1", sizeof(temp)-strlen(temp)-1);   // UI units index
            else
              strncat(temp, ",2", sizeof(temp)-strlen(temp)-1);   // UI units index

            MultiMsgAdd(HumidsMsg, "PileHumidsData=", temp, 0);
            HumidsAdded++;
          }
        } // if sensor disabled
      } // for ANALOG_SENSORS_PER_BOARD
    } // if board disabled
  } // for ANALOG_BOARDS_PER_SYSTEM

  // terminate and send the remaining messages
  MultiMsgAdd(TempsMsg, "", "", 1);
  MultiMsgAdd(HumidsMsg, "", "", 1);

  // terminate the multi-message transfer
  UI_SendMultiEnd("SensorData");

} // end UI_SendSensorData()

/***************************************************************************

FUNCTION:   UI_SendSensorLabels()

PURPOSE:    Send the sensor labels

COMMENTS:   Tested with 6 boards with sensor labels long enough to cause overflow.

***************************************************************************/
void UI_SendSensorLabels(void)
{
  int  i,j;
  int  HumidsAdded = 0;
  char HumidsMsg[MSG_TX_BUFFER_SIZE] = "PileHumidsLabels=";
  int  TempsAdded = 0;
  char TempsMsg[MSG_TX_BUFFER_SIZE] = "PileTempsLabels=";
  char temp[MSG_TX_BUFFER_SIZE];

  // initiate the multi-message transfer
  UI_SendMultiHdr("SensorLabels,PileTempsLabels,PileHumidsLabels", NO_SESSIONID);

  // NOTE: starts with board 3
  for (i = 2; i < ANALOG_BOARDS_PER_SYSTEM; i++)
  {
    if (Settings.AnalogBoard[i].Present == 1 && Settings.AnalogBoard[i].Disabled == 0)
    {
      for (j = 0; j < ANALOG_SENSORS_PER_BOARD; j++)
      {
        if (Settings.AnalogBoard[i].Sensor[j].Disabled == 0)
        {
          if (   Settings.AnalogBoard[i].Sensor[j].Type == ANALOG_SENSOR_TYPE_TEMP
              || Settings.AnalogBoard[i].Sensor[j].Type == ANALOG_SENSOR_TYPE_TEMP_IR)
          {
            if (TempsAdded > 0)
              snprintf(temp, MSG_TX_BUFFER_SIZE, ",%s,%d",
                      Settings.AnalogBoard[i].Sensor[j].Label,
                      (i * ANALOG_SENSORS_PER_BOARD) + j);   // SID - sensor ID
            else
              snprintf(temp, MSG_TX_BUFFER_SIZE, "%s,%d",
                      Settings.AnalogBoard[i].Sensor[j].Label,
                      (i * ANALOG_SENSORS_PER_BOARD) + j);   // SID - sensor ID

            MultiMsgAdd(TempsMsg, "PileTempsLabels=", temp, 0);
            TempsAdded++;
          }
          else
          {
            if (HumidsAdded > 0)
              snprintf(temp, MSG_TX_BUFFER_SIZE, ",%s,%d",
                      Settings.AnalogBoard[i].Sensor[j].Label,
                      (i * ANALOG_SENSORS_PER_BOARD) + j);   // SID - sensor ID
            else
              snprintf(temp, MSG_TX_BUFFER_SIZE, "%s,%d",
                      Settings.AnalogBoard[i].Sensor[j].Label,
                      (i * ANALOG_SENSORS_PER_BOARD) + j);   // SID - sensor ID

            MultiMsgAdd(HumidsMsg, "PileHumidsLabels=", temp, 0);
            HumidsAdded++;
          }
        } // if sensor disabled
      } // for ANALOG_SENSORS_PER_BOARD
    } // if board disabled
  } // for ANALOG_BOARDS_PER_SYSTEM

  // terminate and send the remaining messages
  MultiMsgAdd(TempsMsg, "", "", 1);
  MultiMsgAdd(HumidsMsg, "", "", 1);

  // terminate the multi-message transfer
  UI_SendMultiEnd("SensorLabels");

} // end UI_SendSensorLabels()

/***************************************************************************

FUNCTION:   UI_SendService()

PURPOSE:    Send service settings

COMMENTS:

***************************************************************************/
void UI_SendService(int SendOption)
{
  char message[MSG_TX_BUFFER_SIZE];

  snprintf(message,
           MSG_TX_BUFFER_SIZE,
           "%s=%s,%s,%s,%s,",
           UI_ProgramPosts[TAG_P2_SERVICE].Tag,
           Settings.DealerName,
           Settings.DealerPhone,
           Settings.TechName,
           Settings.TechPhone);

  UI_SendMessage(message, SendOption);
} // end UI_SendService()

/***************************************************************************

FUNCTION:   UI_SendSettingsAck()

PURPOSE:    Send the settings saved response

COMMENTS:

***************************************************************************/
void UI_SendSettingsAck(int SendOption)
{
  char message[MSG_TX_BUFFER_SIZE];

  snprintf(message,
           MSG_TX_BUFFER_SIZE,
           "%s=%s",
           UI_ProgramPosts[TAG_SAVESETTINGS].Tag,
           "0");

  UI_SendMessage(message, SendOption);
} // end UI_SendSettingsAck()

/***************************************************************************

FUNCTION:   UI_SendSlaveUpdate()

PURPOSE:    Send date/time, outside temperature & humidity to slave systems

COMMENTS:

***************************************************************************/
void UI_SendSlaveUpdate(int SendOption)
{
  uint8_t AmPm;
  char DateStr[DATE_LEN+1];
  char TimeStr[TIME_LEN+1];
  char OutsideHumid[HUMID_LEN+1] = "--";
  char OutsideTemp[TEMP_LEN+1] = "--";
  char message[MSG_TX_BUFFER_SIZE];
//  char debug[MSG_TX_BUFFER_SIZE];

  GetDateStr(DateStr);
  GetTimeStr(TimeStr, &AmPm);
  FormatSensorValue(DEFAULT_TEMP_BOARD, SENSOR_OUTSIDE_TEMP, NO_WARNING, OutsideTemp, "%.1f");
  FormatSensorValue(DEFAULT_HUMID_BOARD, SENSOR_OUTSIDE_HUMID, NO_WARNING, OutsideHumid, "%.0f");

  snprintf(message,
           MSG_TX_BUFFER_SIZE,
           "MasterBroadcast=%s&Time=%s&AmPm=%d&OutsideTemp=%s&OutsideHumid=%s",
           DateStr,
           TimeStr,
           AmPm,
           OutsideTemp,
           OutsideHumid);

//  sprintf(debug, "%s\r\n", message);
//  UI_SendDebug(debug);

  UI_SendMessage(message, SendOption);
} // end UI_SendSlaveUpdate()

/***************************************************************************

FUNCTION:   UI_SendTempDevAlarms()

PURPOSE:    Send temperature deviation alarm settings

COMMENTS:

***************************************************************************/
void UI_SendTempDevAlarms(int SendOption)
{
  char message[MSG_TX_BUFFER_SIZE];

  snprintf(message,
           MSG_TX_BUFFER_SIZE,
           "%s=%.1f,%d,%.1f,%d,%.1f,%.1f",
           UI_ProgramPosts[TAG_P1_TEMPALARMS].Tag,
           Settings.Plenum.LowAlarmTemp,
           Settings.Plenum.LowAlarmTimer,
           Settings.Plenum.HighAlarmTemp,
           Settings.Plenum.HighAlarmTimer,
           Settings.Cure.TempLowLimit,
           Settings.Cure.TempHighLimit);

  UI_SendMessage(message, SendOption);
} // end UI_SendTempDevAlarms()

/***************************************************************************

FUNCTION:   UI_SendUserLogSettings()

PURPOSE:    Send user log (history log) settings

COMMENTS:

***************************************************************************/
void UI_SendUserLogSettings(int SendOption)
{
  char message[MSG_TX_BUFFER_SIZE];
  float Percent = 0.0;

  if (SDCardHeader.UserLog.RecordsPerCard > 0)
    Percent = ((float)SDCardHeader.UserLog.NumRecords/SDCardHeader.UserLog.RecordsPerCard)*100.0;

  snprintf(message,
           MSG_TX_BUFFER_SIZE,
           "%s=%d,%d,%s,%u,%u,%.1f,%u,%u,%u,%u,%d",
           UI_ProgramPosts[TAG_P2_USERLOG].Tag,
           Settings.Log.User.Interval,
           Settings.Log.User.Wrap,
           SDCardHeader.UserLog.DataStartDate,
           SDCardHeader.UserLog.RecordsPerCard,
           SDCardHeader.UserLog.NumRecords,
           Percent,
           SDCardHeader.Errors.SystemLog,
           SDCardHeader.Errors.UserLog,
           SDCardHeader.Errors.Header,
           SDCardHeader.Errors.Write,
           Settings.Log.PID.Wrap);

  UI_SendMessage(message, SendOption);
} // end UI_SendUserLogSettings()

/***************************************************************************

FUNCTION:   UI_SendVersions()

PURPOSE:    Send the ARM & analog board software versions

COMMENTS:

***************************************************************************/
void UI_SendVersions(void)
{
  int i;
  char message[MSG_TX_BUFFER_SIZE];
  char str[50];

  // initiate the multi-message transfer
  UI_SendMultiHdr("Versions,SysVersions", NO_SESSIONID);

  snprintf(message, MSG_TX_BUFFER_SIZE, "SysVersions=%s,", Settings.ArmVersion);

  for (i = 0; i < ANALOG_BOARDS_PER_SYSTEM; i++)
  {
    if (Settings.AnalogBoard[i].Present == 1)
    {
      snprintf(str,
               sizeof(str),
               "%s,%s,",
               Settings.AnalogBoard[i].Label,
               Settings.AnalogBoard[i].Version);
      MultiMsgAdd(message, "SysVersions=", str, 0);
    }
  }

  // terminate and send the remaining message
  MultiMsgAdd(message, "", "", 1);

  // terminate the multi-message transfer
  UI_SendMultiEnd("Versions");
} // end UI_SendVersions()

/***************************************************************************

FUNCTION:   UI_SendWarnings()

PURPOSE:    Send warning message

COMMENTS:   There is another version of this code in Logs.c/ActivityLogBuildUIWarnings().
            Any changes here should be reflected in that code also.  The same
            code couldn't be used because the ActivityLog warning stucture is
            a little different.

***************************************************************************/
void UI_SendWarnings(int ForceSend)
{
  WarningsSendToUI(ForceSend);
} // end UI_SendWarnings()

/***   End Of File   ***/
