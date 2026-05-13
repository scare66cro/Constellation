/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     SystemLogs.c

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:  Read and write the system logs

COMMENTS: SystemLog => UI Activity Log

***************************************************************************/

/*** include files ***/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Platform
#include "KFS/kfs.h"
#include "debug.h"

// Gellert
#include "Controls.h"
#include "DataExc.h"
#include "LoadLogs.h"
#include "PIDLogs.h"
#include "PWM.h"
#include "RS485.h"
#include "RTC.h"
#include "SDCard.h"
#include "States.h"
#include "StorePostData.h"
#include "SystemLogs.h"
#include "ThreadMonitor.h"
#include "UI_Messages.h"
#include "UserLogs.h"

/*** typedefs and structures ***/

/*** module variables ***/

QUERY_TAGS QueryTags[DATAQUERY_TOTALTAGS];
SYSLOG_LABELS SysLogRecordLabels[NUM_SYSLOG_RECORD_LABELS];

char DataMsg[MSG_TX_BUFFER_SIZE];
char DatesMsg[MSG_TX_BUFFER_SIZE];
char KeyMsg[MSG_TX_BUFFER_SIZE];
char TimesMsg[MSG_TX_BUFFER_SIZE];
char AlarmsMsg[MSG_TX_BUFFER_SIZE];

char SensorList[ANALOG_BOARDS_PER_SYSTEM * ANALOG_SENSORS_PER_BOARD];  // tracks which sensors have log values

signed char DataQuery[DATAQUERY_LEN];

static SYSLOG_RECORD SysLog;
static SYSLOG_LABELS SysLogEquipLabels[SYSLOG_EQUIP_ITEMS];
static SYSLOG_LABELS SysLogRemoteLabels[SYSLOG_REMOTE_NUM_LABELS];
static MODE_LABELS SysModes[NUM_UI_MODES];
static WARNING_LOG  PreviousWarnings[NUM_WARNINGS];

static char SysLogDate[DATE_LEN+1] = "";

static int  PointsPerDate = 0;
static char RequestType = 0;
static int  SessionID;
static int  WarningsPerRecord = 0;
static int  WarningCount = 0;

/*** static functions ***/

static void BuildMessages(void);
static void BuildSensorList(void);
static void BuildWarningList(char *Msg, char *Tag);
static void FindWarnings(void);
static void FormatLogEquipStatus(int Mode, int Item);
static void FormatLogRemoteStatus(int Mode, int Item);
static void GetLogRecords(int StartRecord, int EndRecord, void (*CallFunction)(void));
static void GetWarnings(int TotalWarnings);
static void KeyForAllItems(void);
static void KeyForFile(void);
static void KeyForQueryItems(void);
static void SendAllItems(void);
static void SendQueryItems(void);
static void SystemLogRead(int BeginPage, int EndPage, void (*CallFunction)(void));
static void SystemLogReadWarnings(int EndPage, int TotalWarnings);

/***************************************************************************

FUNCTION:   BuildMessages()

PURPOSE:    Send the data from each record to the LTX

COMMENTS:   This function keeps track of the information for the following
            messages:

            Dates = "09/28/2007:20,";
            TimeStamps = "09:25:45#12,09:24:45#12,09:23:44#0,09:22:44#0,..."

            It tracks how many data points were taken for each date (PointsPerDate)
            and how many warnings there were for each record (WaringsPerRecord)

***************************************************************************/
void BuildMessages(void)
{
  char str[20];

  // store the first date
  if (strcmp(SysLogDate, "") == 0)
    StringCopy(SysLogDate, SysLog.Date, DATE_LEN+1);

  // check for start of new date
  if (strcmp(SysLog.Date, SysLogDate) != 0)
  {
    // add the date and PointsPerDate to the message
    snprintf(str, sizeof(str), "%s?%d,", SysLogDate, PointsPerDate);
    MultiMsgAdd(DatesMsg, "Dates=", str, 0);
    StringCopy(SysLogDate, SysLog.Date, DATE_LEN+1);
    PointsPerDate = 0;
  }

  WarningsPerRecord = 0;

  // get the data
  if (DataQuery[QUERYTAG_ALLITEMS] == -1)
    SendQueryItems();
  else
    SendAllItems();

  // add timestamp
  snprintf(str, sizeof(str), "%s#%d,", SysLog.Time, WarningsPerRecord);
  MultiMsgAdd(TimesMsg, "TimeStamps=", str, 0);

  PointsPerDate++;
} // end BuildMessages()

/***************************************************************************

FUNCTION:   BuildSensorList()

PURPOSE:    Checks a system log record to see which sensors have valid values

COMMENTS:   The SensorList is used to build the label string for the sensors
            that is sent to the UI to label the columns.  Because the
            sensor values stored in each data log record won't necessarily
            be consistent, (a sensor may give an invalid reading or there
            may be a comm error with the board causing sensor values to
            be SENSOR_VAL_UNDEFINED) a first pass is made through the system
            log records selected by the query to check each log record for
            valid sensors values so the label string will have a labels
            for all sensors with valid data requested in the query.

***************************************************************************/
void BuildSensorList(void)
{
  int i;

  for (i = 0; i < ANALOG_BOARDS_PER_SYSTEM * ANALOG_SENSORS_PER_BOARD; i++)
    if (SysLog.Sensors[i] != SENSOR_VAL_UNDEFINED)
      SensorList[i] = 1;
} // end BuildSensorList()

/***************************************************************************

FUNCTION:   BuildWarningList()

PURPOSE:    Builds and sends the warning messages for each system log record

COMMENTS:

***************************************************************************/
void BuildWarningList(char *Msg, char *Tag)
{
  int i,k;
  int element,bit;
  int totalBits = sizeof(SysLog.Warning[0].Value[0]) * 8;
  char str[MSG_TX_BUFFER_SIZE] = "";

  for (i = 0; i < NUM_WARNINGS; i++)
  {
    if (SysLog.Warning[i].Status > 0)       // if the warning is set
    {
      snprintf(str, MSG_TX_BUFFER_SIZE, "%d&%d,%u,%u,", i, SysLog.Warning[i].Status, SysLog.Warning[i].Value[0], SysLog.Warning[i].Value[1]);
      MultiMsgAdd(Msg, Tag, str, 0);

      switch (i)
      {
        case WARN_REFRIG_STAGE:
        case WARN_REFRIG_DEFROST:
        case WARN_HUMIDIFIER:
        case WARN_LIGHTS:
        case WARN_SYSCONFIG_EQ:
        case WARN_EXPANSIONBOARD:
        case WARN_NEWBOARD:
        case WARN_BOARDREMOVED:
        case WARN_COMMERR:
          for (k = 0; k < (totalBits * WARNING_VALUE_LEN); k++)    // bitmap of equipment
          {
            element = k / totalBits;
            bit = k % totalBits;
            if ((SysLog.Warning[i].Value[element] & ((uint32_t) 1 << bit)) == ((uint32_t) 1 << bit))
            {
              WarningsPerRecord++;
            }
          }
          break;

        default:
          WarningsPerRecord++;
          break;
      }
    }
  }
} // end BuildWarningList()

/***************************************************************************

FUNCTION:   FindWarnings()

PURPOSE:    Determines if the log record has warnings and if they are a
            different set of alarms than the last record

COMMENTS:

***************************************************************************/
void FindWarnings(void)
{
  int i;
  int warnings = 0;
  char str[20];

  for (i = 0; i < NUM_WARNINGS && warnings == 0; i++)
  {
    if (SysLog.Warning[i].Status != 0)     // if the warning is set
    {
      if (memcmp(SysLog.Warning, PreviousWarnings, sizeof(WARNING_LOG) * NUM_WARNINGS) != 0)
      {
        memcpy(PreviousWarnings, SysLog.Warning, sizeof(WARNING_LOG) * NUM_WARNINGS);
        warnings = 1;
      }
    }
  }

  if (warnings == 1)
  {
    // store the first date
    if (strcmp(SysLogDate, "") == 0)
      StringCopy(SysLogDate, SysLog.Date, DATE_LEN+1);

    // check for start of new date
    if (strcmp(SysLog.Date, SysLogDate) != 0)
    {
      // add the date and PointsPerDate to the message
      snprintf(str, sizeof(str), "%s?%d,", SysLogDate, PointsPerDate);
      MultiMsgAdd(DatesMsg, "Dates=", str, 0);
      StringCopy(SysLogDate, SysLog.Date, DATE_LEN+1);
      PointsPerDate = 0;
    }

    WarningsPerRecord = 0;
    FormatLogMainItems(SysLog.MainPage[LOG_MODE], LOG_MODE, TABLE_MODE, LOG_DATA);
    BuildWarningList(AlarmsMsg, "LogAlarms=");

    if (WarningsPerRecord > 0)
      WarningCount++;

    // add timestamp
    snprintf(str, sizeof(str), "%s#%d,", SysLog.Time, WarningsPerRecord);
    MultiMsgAdd(TimesMsg, "TimeStamps=", str, 0);

    PointsPerDate++;
  }
} // end FindWarnings()

/***************************************************************************

FUNCTION:   FormatLogEquipStatus()

PURPOSE:    Format the data or label for the equipment status items

COMMENTS:   In the FILE_MODE case it uses the 'data' message tag because
            it is just treated like a regular data record when it is written
            out to the USB storage device.

***************************************************************************/
void FormatLogEquipStatus(int Mode, int Item)
{
  int i;
  int includeStatus;
  char str[50];
  char label[LOG_LABELS];

  for (i = 0; i < EQUIPSTATUS_LEN; i++)
  {
    includeStatus = 0;

    switch (i)
    {
      case LOG_FANSWITCH:
      case LOG_FANFAIL:
      case LOG_FANOUTPUT:
        if (Settings.EquipIo[EQ_FAN].Enabled)
        {
          includeStatus = 1;
        }
        break;

      case LOG_CLIMACELLSWITCH:   // burner switch in onion mode
        if (   (Settings.SystemMode == SM_POTATO && Settings.EquipIo[EQ_CLIMACELL].Enabled)
            || (Settings.SystemMode == SM_ONION && Settings.EquipIo[EQ_BURNER].Enabled) )
        {
          includeStatus = 1;
        }
        break;
      case LOG_CLIMACELLFAIL:
      case LOG_CLIMACELLOUTPUT:
        if (Settings.EquipIo[EQ_CLIMACELL].Enabled)
        {
          includeStatus = 1;
        }
        break;
      case LOG_BURNERFAIL:
      case LOG_BURNEROUTPUT:
        if (Settings.EquipIo[EQ_BURNER].Enabled)
        {
          includeStatus = 1;
        }
        break;

      case LOG_HUMIDSWITCH:   // cure switch in onion mode
        if (   (Settings.SystemMode == SM_POTATO && Settings.EquipIo[EQ_HUMID_HEAD1].Enabled)
            || (Settings.SystemMode == SM_ONION) )
        {
          includeStatus = 1;
        }
        break;
      case LOG_HUMID1FAIL:
      case LOG_HUMID1HEADOUTPUT:
      case LOG_HUMID1PUMPOUTPUT:
        if (Settings.EquipIo[EQ_HUMID_HEAD1].Enabled)
        {
          includeStatus = 1;
        }
        break;

      case LOG_HUMID2FAIL:
      case LOG_HUMID2HEADOUTPUT:
      case LOG_HUMID2PUMPOUTPUT:
        if (Settings.EquipIo[EQ_HUMID_HEAD2].Enabled)
        {
          includeStatus = 1;
        }
        break;

      case LOG_HUMID3FAIL:
      case LOG_HUMID3HEADOUTPUT:
      case LOG_HUMID3PUMPOUTPUT:
        if (Settings.EquipIo[EQ_HUMID_HEAD3].Enabled)
        {
          includeStatus = 1;
        }
        break;

      case LOG_REFSWITCH:
        if (Settings.PWM[PWM_REFRIGERATION].Enabled || Settings.EquipIo[EQ_REFRIG_STAGE1].Enabled)
        {
          includeStatus = 1;
        }
        break;
      case LOG_REFFAIL:
        if (Settings.EquipIo[EQ_REFRIGERATION].Enabled)
        {
          includeStatus = 1;
        }
        break;
      case LOG_REF1FAIL:
      case LOG_REF1OUTPUT:
        if (Settings.EquipIo[EQ_REFRIG_STAGE1].Enabled)
        {
          includeStatus = 1;
        }
        break;
      case LOG_REF2FAIL:
      case LOG_REF2OUTPUT:
        if (Settings.EquipIo[EQ_REFRIG_STAGE2].Enabled)
        {
          includeStatus = 1;
        }
        break;
      case LOG_REF3FAIL:
      case LOG_REF3OUTPUT:
        if (Settings.EquipIo[EQ_REFRIG_STAGE3].Enabled)
        {
          includeStatus = 1;
        }
        break;
      case LOG_REF4FAIL:
      case LOG_REF4OUTPUT:
        if (Settings.EquipIo[EQ_REFRIG_STAGE4].Enabled)
        {
          includeStatus = 1;
        }
        break;
      case LOG_REF5FAIL:
      case LOG_REF5OUTPUT:
        if (Settings.EquipIo[EQ_REFRIG_STAGE5].Enabled)
        {
          includeStatus = 1;
        }
        break;
      case LOG_REF6FAIL:
      case LOG_REF6OUTPUT:
        if (Settings.EquipIo[EQ_REFRIG_STAGE6].Enabled)
        {
          includeStatus = 1;
        }
        break;
      case LOG_REF7FAIL:
      case LOG_REF7OUTPUT:
        if (Settings.EquipIo[EQ_REFRIG_STAGE7].Enabled)
        {
          includeStatus = 1;
        }
        break;
      case LOG_REF8FAIL:
      case LOG_REF8OUTPUT:
        if (Settings.EquipIo[EQ_REFRIG_STAGE8].Enabled)
        {
          includeStatus = 1;
        }
        break;
      case LOG_REFDEFROST1FAIL:
      case LOG_REFDEFROST1OUTPUT:
        if (Settings.EquipIo[EQ_REFRIG_DEFROST1].Enabled)
        {
          includeStatus = 1;
        }
        break;
      case LOG_REFDEFROST2FAIL:
      case LOG_REFDEFROST2OUTPUT:
        if (Settings.EquipIo[EQ_REFRIG_DEFROST2].Enabled)
        {
          includeStatus = 1;
        }
        break;

      case LOG_HEATFAIL:
      case LOG_HEATOUTPUT:
        if (Settings.EquipIo[EQ_HEAT].Enabled)
        {
          includeStatus = 1;
        }
        break;

      case LOG_DOORSWITCH:
        if (Settings.PWM[PWM_DOORS].Enabled || Settings.EquipIo[EQ_PULSEDOOR_POWER].Output)
        {
          includeStatus = 1;
        }
        break;
      case LOG_DOOROUTPUT:
        if (Settings.EquipIo[EQ_PULSEDOOR_POWER].Output)
        {
          includeStatus = 1;
        }
        break;

      case LOG_CAVHEATFAIL:
      case LOG_CAVHEATOUTPUT:
        if (Settings.EquipIo[EQ_CAVITY_HEAT].Enabled)
        {
          includeStatus = 1;
        }
        break;

      case LOG_GREEN:
      case LOG_YELLOW:
      case LOG_RED:
        includeStatus = 1;
        break;

      case LOG_LIGHTS1IN:
      case LOG_LIGHTS1OUTPUT:
        if (Settings.EquipIo[EQ_LIGHTS1].Enabled)
        {
          includeStatus = 1;
        }
        break;

      case LOG_LIGHTS2IN:
      case LOG_LIGHTS2OUTPUT:
        if (Settings.EquipIo[EQ_LIGHTS2].Enabled)
        {
          includeStatus = 1;
        }
        break;

      case LOG_AUX1FAIL:
      case LOG_AUX1OUTPUT:
        if (Settings.EquipIo[EQ_AUX1].Enabled)
        {
          includeStatus = 1;
        }
        break;
      case LOG_AUX2FAIL:
      case LOG_AUX2OUTPUT:
        if (Settings.EquipIo[EQ_AUX2].Enabled)
        {
          includeStatus = 1;
        }
        break;
      case LOG_AUX3FAIL:
      case LOG_AUX3OUTPUT:
        if (Settings.EquipIo[EQ_AUX3].Enabled)
        {
          includeStatus = 1;
        }
        break;
      case LOG_AUX4FAIL:
      case LOG_AUX4OUTPUT:
        if (Settings.EquipIo[EQ_AUX4].Enabled)
        {
          includeStatus = 1;
        }
        break;
      case LOG_AUX5FAIL:
      case LOG_AUX5OUTPUT:
        if (Settings.EquipIo[EQ_AUX5].Enabled)
        {
          includeStatus = 1;
        }
        break;
      case LOG_AUX6FAIL:
      case LOG_AUX6OUTPUT:
        if (Settings.EquipIo[EQ_AUX6].Enabled)
        {
          includeStatus = 1;
        }
        break;
      case LOG_AUX7FAIL:
      case LOG_AUX7OUTPUT:
        if (Settings.EquipIo[EQ_AUX7].Enabled)
        {
          includeStatus = 1;
        }
        break;
      case LOG_AUX8FAIL:
      case LOG_AUX8OUTPUT:
        if (Settings.EquipIo[EQ_AUX8].Enabled)
        {
          includeStatus = 1;
        }
        break;

      default:
        break;
    }

    if (includeStatus)
    {
      // flip values for display -> '1' means light is on
      if (   i == LOG_LIGHTS1IN
          || i == LOG_LIGHTS2IN)
      {
        SysLog.EquipStatus[i] = (SysLog.EquipStatus[i] + 1) % 2;
      }

      if (Item == LOG_LABEL)
      {
        switch(i)
        {
          case LOG_CAVHEATOUTPUT:
            if (Settings.CavityHeat.Label == 1)
            {
              StringCopy(label, SysLogRecordLabels[SL_PILEFAN_OUTPUT].Label, LOG_LABELS);
            }
            else
            {
              StringCopy(label, SysLogEquipLabels[i].Label, LOG_LABELS);
            }
            break;
          case LOG_CAVHEATFAIL:
            if (Settings.CavityHeat.Label == 1)
            {
              StringCopy(label, SysLogRecordLabels[SL_PILEFAN_FAIL].Label, LOG_LABELS);
            }
            else
            {
              StringCopy(label, SysLogEquipLabels[i].Label, LOG_LABELS);
            }
            break;
          case LOG_CLIMACELLSWITCH:
            if (Settings.SystemMode == SM_ONION)
            {
              StringCopy(label, SysLogEquipLabels[LOG_BURNERSWITCH].Label, LOG_LABELS);
            }
            else
            {
              StringCopy(label, SysLogEquipLabels[i].Label, LOG_LABELS);
            }
            break;
          case LOG_HUMIDSWITCH:
            if (Settings.SystemMode == SM_ONION)
            {
              StringCopy(label, SysLogEquipLabels[LOG_CURESWITCH].Label, LOG_LABELS);
            }
            else
            {
              StringCopy(label, SysLogEquipLabels[i].Label, LOG_LABELS);
            }
            break;
          case LOG_AUX1FAIL:
            snprintf(label, LOG_LABELS, "%s Fail", Settings.EquipIo[EQ_AUX1].Name);
            break;
          case LOG_AUX1OUTPUT:
            snprintf(label, LOG_LABELS, "%s", Settings.EquipIo[EQ_AUX1].Name);
            break;
          case LOG_AUX2FAIL:
            snprintf(label, LOG_LABELS, "%s Fail", Settings.EquipIo[EQ_AUX2].Name);
            break;
          case LOG_AUX2OUTPUT:
            snprintf(label, LOG_LABELS, "%s", Settings.EquipIo[EQ_AUX2].Name);
            break;
          case LOG_AUX3FAIL:
            snprintf(label, LOG_LABELS, "%s Fail", Settings.EquipIo[EQ_AUX3].Name);
            break;
          case LOG_AUX3OUTPUT:
            snprintf(label, LOG_LABELS, "%s", Settings.EquipIo[EQ_AUX3].Name);
            break;
          case LOG_AUX4FAIL:
            snprintf(label, LOG_LABELS, "%s Fail", Settings.EquipIo[EQ_AUX4].Name);
            break;
          case LOG_AUX4OUTPUT:
            snprintf(label, LOG_LABELS, "%s", Settings.EquipIo[EQ_AUX4].Name);
            break;
          case LOG_AUX5FAIL:
            snprintf(label, LOG_LABELS, "%s Fail", Settings.EquipIo[EQ_AUX5].Name);
            break;
          case LOG_AUX5OUTPUT:
            snprintf(label, LOG_LABELS, "%s", Settings.EquipIo[EQ_AUX5].Name);
            break;
          case LOG_AUX6FAIL:
            snprintf(label, LOG_LABELS, "%s Fail", Settings.EquipIo[EQ_AUX6].Name);
            break;
          case LOG_AUX6OUTPUT:
            snprintf(label, LOG_LABELS, "%s", Settings.EquipIo[EQ_AUX6].Name);
            break;
          case LOG_AUX7FAIL:
            snprintf(label, LOG_LABELS, "%s Fail", Settings.EquipIo[EQ_AUX7].Name);
            break;
          case LOG_AUX7OUTPUT:
            snprintf(label, LOG_LABELS, "%s", Settings.EquipIo[EQ_AUX7].Name);
            break;
          case LOG_AUX8FAIL:
            snprintf(label, LOG_LABELS, "%s Fail", Settings.EquipIo[EQ_AUX8].Name);
            break;
          case LOG_AUX8OUTPUT:
            snprintf(label, LOG_LABELS, "%s", Settings.EquipIo[EQ_AUX8].Name);
            break;
          default:
            StringCopy(label, SysLogEquipLabels[i].Label, LOG_LABELS);
            break;
        }

        if (Mode == FILE_MODE)
        {
          snprintf(str, sizeof(str), "%s,", label);
          MultiMsgAdd(DataMsg, "Data=", str, 0);
        }
        else
        {
          snprintf(str, sizeof(str), "%s:,", label);
          MultiMsgAdd(KeyMsg, "Key=", str, 0);
        }
      }
      else
      {
        snprintf(str, sizeof(str), "%d,", SysLog.EquipStatus[i]);
        MultiMsgAdd(DataMsg, "Data=", str, 0);
      }
    }
  }
} // end FormatLogEquipStatus()

/***************************************************************************

FUNCTION:   FormatLogMainItems()

PURPOSE:    Format the data or label for the system monitor items

COMMENTS:   In the FILE_MODE case it uses the 'data' message tag because
            it is just treated like a regular data record when it is written
            out to the USB storage device.

***************************************************************************/
void FormatLogMainItems(short MainItem, int i, int Mode, int Item)
{
  char str[50];

  if (   Settings.SystemMode == SM_POTATO
      && (   i == LOG_BURNOUTPUT
          || i == LOG_CALCHUM))
  {
    return;
  }

  if (Item == LOG_LABEL)
  {
    if (Mode == FILE_MODE)
    {
      snprintf(str, sizeof(str), "%s,", QueryTags[i].Label);
      MultiMsgAdd(DataMsg, "Data=", str, 0);
    }
    else
    {
      snprintf(str, sizeof(str), "%s:%s,", QueryTags[i].Label, QueryTags[i].Units);
      MultiMsgAdd(KeyMsg, "Key=", str, 0);
    }
  }
  else
  {
    if (MainItem == SENSOR_VAL_UNDEFINED)
    {
      strcpy(str, "*,");
    }
    else
    {
      if (   i == LOG_PLENTEMPSET
          || i == LOG_PLENTEMP
          || i == LOG_COOLAVAILTEMP
          || i == LOG_DAILYFANRUNTIME )
      {
        snprintf(str, sizeof(str), "%.1f,", MainItem/10.0);
      }
      else if (i == LOG_MODE)
      {
        if (Mode == GRAPH_MODE)
        {
          snprintf(str, sizeof(str), "%d,", MainItem);
        }
        else
        {
          snprintf(str, sizeof(str), "%s,", SysModes[MainItem].Label);
        }
      }
      else
      {
        snprintf(str, sizeof(str), "%d,", MainItem);
      }
    }

    MultiMsgAdd(DataMsg, "Data=", str, 0);
  }
} // end FormatLogMainItems()

/***************************************************************************

FUNCTION:   FormatLogRemoteStatus()

PURPOSE:    Format the data or label for the remote status items

COMMENTS:   In the FILE_MODE case it uses the 'data' message tag because
            it is just treated like a regular data record when it is written
            out to the USB storage device.

***************************************************************************/
void FormatLogRemoteStatus(int Mode, int Item)
{
  int i;
  int includeStatus;
  char str[50];

  for (i = 0; i < SYSLOG_REMOTE_ITEMS; i++)
  {
    includeStatus = 0;

    switch (i)
    {
      case LOG_DIAG_REF1:
        if (Settings.EquipIo[EQ_REFRIG_STAGE1].Enabled)
        {
          includeStatus = 1;
        }
        break;
      case LOG_DIAG_REF2:
        if (Settings.EquipIo[EQ_REFRIG_STAGE2].Enabled)
        {
          includeStatus = 1;
        }
        break;
      case LOG_DIAG_REF3:
        if (Settings.EquipIo[EQ_REFRIG_STAGE3].Enabled)
        {
          includeStatus = 1;
        }
        break;
      case LOG_DIAG_REF4:
        if (Settings.EquipIo[EQ_REFRIG_STAGE4].Enabled)
        {
          includeStatus = 1;
        }
        break;
      case LOG_DIAG_REF5:
        if (Settings.EquipIo[EQ_REFRIG_STAGE5].Enabled)
        {
          includeStatus = 1;
        }
        break;
      case LOG_DIAG_REF6:
        if (Settings.EquipIo[EQ_REFRIG_STAGE6].Enabled)
        {
          includeStatus = 1;
        }
        break;
      case LOG_DIAG_REF7:
        if (Settings.EquipIo[EQ_REFRIG_STAGE7].Enabled)
        {
          includeStatus = 1;
        }
        break;
      case LOG_DIAG_REF8:
        if (Settings.EquipIo[EQ_REFRIG_STAGE8].Enabled)
        {
          includeStatus = 1;
        }
        break;
      case LOG_DIAG_DEFROST1:
        if (Settings.EquipIo[EQ_REFRIG_DEFROST1].Enabled)
        {
          includeStatus = 1;
        }
        break;
      case LOG_DIAG_DEFROST2:
        if (Settings.EquipIo[EQ_REFRIG_DEFROST2].Enabled)
        {
          includeStatus = 1;
        }
        break;

      case LOG_REMOTEOFF_REF:
        if (Settings.PWM[PWM_REFRIGERATION].Enabled || Settings.EquipIo[EQ_REFRIG_STAGE1].Enabled)
        {
          includeStatus = 1;
        }
        break;

      case LOG_REMOTEOFF_FAN:
        if (Settings.PWM[PWM_FAN].Enabled || Settings.EquipIo[EQ_FAN].Enabled)
        {
          includeStatus = 1;
        }
        break;

      case LOG_REMOTEOFF_CLIMACELL: // burner in onion mode
        if (   (Settings.SystemMode == SM_POTATO && Settings.EquipIo[EQ_CLIMACELL].Enabled)
            || (Settings.SystemMode == SM_ONION && Settings.EquipIo[EQ_BURNER].Enabled) )
        {
          includeStatus = 1;
        }
        break;

      case LOG_REMOTEOFF_HUMID1:
        if (   (Settings.SystemMode == SM_POTATO && Settings.EquipIo[EQ_HUMID_HEAD1].Enabled)
            || (Settings.SystemMode == SM_ONION) )
        {
          includeStatus = 1;
        }
        break;
      case LOG_REMOTEOFF_HUMID2:
        if (Settings.EquipIo[EQ_HUMID_HEAD2].Enabled)
        {
          includeStatus = 1;
        }
        break;
      case LOG_REMOTEOFF_HUMID3:
        if (Settings.EquipIo[EQ_HUMID_HEAD3].Enabled)
        {
          includeStatus = 1;
        }
        break;

      case LOG_REMOTEOFF_HEAT:
        if (Settings.EquipIo[EQ_HEAT].Enabled)
        {
          includeStatus = 1;
        }
        break;

      case LOG_REMOTEOFF_AUX1:
        if (Settings.EquipIo[EQ_AUX1].Enabled)
        {
          includeStatus = 1;
        }
        break;
      case LOG_REMOTEOFF_AUX2:
        if (Settings.EquipIo[EQ_AUX2].Enabled)
        {
          includeStatus = 1;
        }
        break;
      case LOG_REMOTEOFF_AUX3:
        if (Settings.EquipIo[EQ_AUX3].Enabled)
        {
          includeStatus = 1;
        }
        break;
      case LOG_REMOTEOFF_AUX4:
        if (Settings.EquipIo[EQ_AUX4].Enabled)
        {
          includeStatus = 1;
        }
        break;
      case LOG_REMOTEOFF_AUX5:
        if (Settings.EquipIo[EQ_AUX5].Enabled)
        {
          includeStatus = 1;
        }
        break;
      case LOG_REMOTEOFF_AUX6:
        if (Settings.EquipIo[EQ_AUX6].Enabled)
        {
          includeStatus = 1;
        }
        break;
      case LOG_REMOTEOFF_AUX7:
        if (Settings.EquipIo[EQ_AUX7].Enabled)
        {
          includeStatus = 1;
        }
        break;
      case LOG_REMOTEOFF_AUX8:
        if (Settings.EquipIo[EQ_AUX8].Enabled)
        {
          includeStatus = 1;
        }
        break;

      case LOG_REMOTEOFF_CAVHEAT:
        if (Settings.EquipIo[EQ_CAVITY_HEAT].Enabled)
        {
          includeStatus = 1;
        }
        break;

      case LOG_REMOTEOFF_LIGHTS1:
        if (Settings.EquipIo[EQ_LIGHTS1].Enabled)
        {
          includeStatus = 1;
        }
        break;

      case LOG_REMOTEOFF_LIGHTS2:
        if (Settings.EquipIo[EQ_LIGHTS2].Enabled)
        {
          includeStatus = 1;
        }
        break;
    }

    if (includeStatus)
    {
      if (Item == LOG_LABEL)
      {
        // remap the onion labels
        int remap = i;

        if (Settings.SystemMode == SM_ONION)
        {
          switch (i)
          {
            case LOG_REMOTEOFF_CLIMACELL: remap = LOG_REMOTEOFF_BURNER; break;
            case LOG_REMOTEOFF_HUMID1:    remap = LOG_REMOTEOFF_CURE;   break;
          }
        }

        if (Mode == FILE_MODE)
        {
          snprintf(str, sizeof(str), "%s,", SysLogRemoteLabels[remap].Label);
          MultiMsgAdd(DataMsg, "Data=", str, 0);
        }
        else
        {
          snprintf(str, sizeof(str), "%s:,", SysLogRemoteLabels[remap].Label);
          MultiMsgAdd(KeyMsg, "Key=", str, 0);
        }
      }
      else
      {
        snprintf(str, sizeof(str), "%d,", SysLog.RemoteOff[i]);
        MultiMsgAdd(DataMsg, "Data=", str, 0);
      }
    }
  }
} // end FormatLogRemoteStatus()

/***************************************************************************

FUNCTION:   FormatLogSensor()

PURPOSE:    Format the data or label for the sensors

COMMENTS:   In the FILE_MODE case it uses the 'data' message tag because
            it is just treated like a regular data record when it is written
            out to the USB storage device.

***************************************************************************/
void FormatLogSensor(short *Sensors, int SensorID, int Mode, int Item)
{
  int Board = SensorID / ANALOG_SENSORS_PER_BOARD;
  int Sensor = SensorID % ANALOG_SENSORS_PER_BOARD;
  char str[50];
  char Units[50];

  if (Item == LOG_LABEL)
  {
    if (Mode == FILE_MODE)
    {
      snprintf(str, sizeof(str), "%s,", Settings.AnalogBoard[Board].Sensor[Sensor].Label);
      MultiMsgAdd(DataMsg, "Data=", str, 0);
    }
    else
    {
      if (   Settings.AnalogBoard[Board].Sensor[Sensor].Type == ANALOG_SENSOR_TYPE_TEMP
          || Settings.AnalogBoard[Board].Sensor[Sensor].Type == ANALOG_SENSOR_TYPE_TEMP_IR)
      {
        StringCopy(Units, QueryTags[QUERYTAG_TEMPSNSR].Units, sizeof(Units));
      }
      else if (Settings.AnalogBoard[Board].Sensor[Sensor].Type == ANALOG_SENSOR_TYPE_HUMID)
      {
        StringCopy(Units, QueryTags[QUERYTAG_HUMSNSR].Units, sizeof(Units));
      }
      else if (Settings.AnalogBoard[Board].Sensor[Sensor].Type == ANALOG_SENSOR_TYPE_CO2)
      {
        StringCopy(Units, QueryTags[QUERYTAG_CO2].Units, sizeof(Units));
      }

      snprintf(str, sizeof(str), "%s:%s,", Settings.AnalogBoard[Board].Sensor[Sensor].Label, Units);
      MultiMsgAdd(KeyMsg, "Key=", str, 0);
    }
  }
  else
  {
    if (Sensors[SensorID] == SENSOR_VAL_UNDEFINED)
    {
      strcpy(str, "*,");
    }
    else
    {
      if (   Settings.AnalogBoard[Board].Sensor[Sensor].Type == ANALOG_SENSOR_TYPE_TEMP
          || Settings.AnalogBoard[Board].Sensor[Sensor].Type == ANALOG_SENSOR_TYPE_TEMP_IR)
      {
        snprintf(str, sizeof(str), "%.1f,", Sensors[SensorID]/10.0);
      }
      else
      {
        snprintf(str, sizeof(str), "%d,", Sensors[SensorID]);
      }
    }

    MultiMsgAdd(DataMsg, "Data=", str, 0);
  }
} // end FormatLogSensor()

/***************************************************************************

FUNCTION:   GetLogRecords()

PURPOSE:    Determine which records to read

COMMENTS:   The system/activity log is circular

***************************************************************************/
void GetLogRecords(int StartRecord, int EndRecord, void (*CallFunction)(void))
{
  int CurrentPage = 0;
  int EndPage = 0;
  int FirstPartRecs = 0;
  int LastPage = SDCardHeader.SysLog.StartBlock + (NUM_SYSLOG_RECS * SDCardHeader.SysLog.BlocksPerRecord);
  int TotalRecords = EndRecord - StartRecord + 1;

  // check if any records available
  if (SDCardHeader.SysLog.NumRecords == 0)
    return;

  // don't look for more records than are available
  if (TotalRecords > SDCardHeader.SysLog.NumRecords)
    TotalRecords = SDCardHeader.SysLog.NumRecords;

  if (SDCardHeader.SysLog.WriteBlock == SDCardHeader.SysLog.StartBlock) // file has just wrapped
    CurrentPage = LastPage;
  else
    CurrentPage = SDCardHeader.SysLog.WriteBlock - SDCardHeader.SysLog.BlocksPerRecord;

  // Because the log is circular and wraps, the first (or oldest) record is not
  // necessarily at the beginning of the file (StartPage). FirstPartRecs is the
  // number of records from the beginning of the file to the CurrentPage
  FirstPartRecs = ((CurrentPage - SDCardHeader.SysLog.StartBlock) / SDCardHeader.SysLog.BlocksPerRecord) + 1;
  if (StartRecord > FirstPartRecs)
    EndPage = LastPage - (((StartRecord - 1) - FirstPartRecs) * SDCardHeader.SysLog.BlocksPerRecord);
  else
    EndPage = CurrentPage - ((StartRecord - 1) * SDCardHeader.SysLog.BlocksPerRecord);

  // get the records
  SystemLogRead(EndPage, TotalRecords, CallFunction);
} // end GetLogRecords()

/***************************************************************************

FUNCTION:   GetWarnings()

PURPOSE:    Determine which records to read

COMMENTS:   The system/activity log is circular

***************************************************************************/
void GetWarnings(int TotalWarnings)
{
  int CurrentPage = 0;

  // check if any records available
  if (SDCardHeader.SysLog.NumRecords == 0)
    return;

  // don't look for more records than are available
  if (TotalWarnings > SDCardHeader.SysLog.NumRecords)
    TotalWarnings = SDCardHeader.SysLog.NumRecords;

  if (SDCardHeader.SysLog.WriteBlock == SDCardHeader.SysLog.StartBlock)   // file has just wrapped
    CurrentPage = SDCardHeader.SysLog.StartBlock + (NUM_SYSLOG_RECS * SDCardHeader.SysLog.BlocksPerRecord);   // last block/page of file
  else
    CurrentPage = SDCardHeader.SysLog.WriteBlock - SDCardHeader.SysLog.BlocksPerRecord;

  // get the records
  SystemLogReadWarnings(CurrentPage, TotalWarnings);
} // end GetWarnings()

/***************************************************************************

FUNCTION:   KeyForAllItems()

PURPOSE:    Build a key (labels) with all the system log items

COMMENTS:   The UI has an options to display the entire system (activity) log
            record instead of just up to 10 items.  This function is used to
            build the label string with all the items.

            See comment in BuildSensorList() (above) for explanation
            of how SensorList is used to find appropriate sensor labels.

            It builds a string that looks like this:

            Key = "Plen Temp SP:deg,Plen Temp:deg,Cool Avl Temp:deg,Plen Hum SP:%..."

***************************************************************************/
void KeyForAllItems(void)
{
  int  i;

  // set up the message
  strcpy(KeyMsg, "Key=");

  // debug - RecordNum
//  MultiMsgAdd(KeyMsg, "Key=", "Record: ,", 0);

  // get the main page labels
  for (i = 0; i < LOG_MAINITEMS; i++)
  {
    FormatLogMainItems(SysLog.MainPage[i], i, TABLE_MODE, LOG_LABEL);
  }

  // get the sensor labels
  for (i = 0; i < (ANALOG_BOARDS_PER_SYSTEM * ANALOG_SENSORS_PER_BOARD); i++)
  {
    if (SensorList[i] == 1)
    {
      FormatLogSensor(SysLog.Sensors, i, TABLE_MODE, LOG_LABEL);
    }
  }

  FormatLogEquipStatus(TABLE_MODE, LOG_LABEL);
  FormatLogRemoteStatus(TABLE_MODE, LOG_LABEL);

  // terminate and send the remaining message
  MultiMsgAdd(KeyMsg, "", "", 1);
} // end KeyForAllItems()

/***************************************************************************

FUNCTION:   KeyForFile()

PURPOSE:    Build a key (column labels) with all the system log items

COMMENTS:   All log items are sent to the file

            See comment in BuildSensorList() for explanation
            of how SensorList is used to find appropriate sensor labels.

            It builds a string that looks like this:

            Data = "Plen Temp SP:deg,Plen Temp:deg,Cool Avl Temp:deg,..."

***************************************************************************/
void KeyForFile(void)
{
  int  i;
  char str[50];

  if (RequestType == REQTYPE_TOFILE)
  {
    // initiate the multi-message transfer
    UI_SendMultiHdr("LogData,Data", SessionID);

    // set up the messages
    strcpy(DataMsg, "Data=");
  }

  // send the record info header
  snprintf(str,
           sizeof(str),
           "%s,%s,%s,",
           SysLogRecordLabels[SL_RECORD].Label,
           SysLogRecordLabels[SL_DATE].Label,
           SysLogRecordLabels[SL_TIME].Label);
  MultiMsgAdd(DataMsg, "Data=", str, 0);

  // send the main page headers
  for (i = 0; i < LOG_MAINITEMS; i++)
  {
    FormatLogMainItems(SysLog.MainPage[i], i, FILE_MODE, LOG_LABEL);
  }

  // send the sensor labels
  for (i = 0; i < (ANALOG_BOARDS_PER_SYSTEM * ANALOG_SENSORS_PER_BOARD); i++)
  {
    if (SensorList[i] == 1)
    {
      FormatLogSensor(SysLog.Sensors, i, FILE_MODE, LOG_LABEL);
    }
  }

  FormatLogEquipStatus(FILE_MODE, LOG_LABEL);
  FormatLogRemoteStatus(FILE_MODE, LOG_LABEL);

  // send terminator
  if (RequestType == REQTYPE_TOFILE)
  {
    // terminate and send remaining messages
    MultiMsgAdd(DataMsg, "", "", 1);

    // terminate the multi-message transfer
    UI_SendMultiEnd("LogData");

    // send end-of-record (EOR) message - causes LTX to write record to the file
    snprintf(str, sizeof(str), "LogToFile=EOR,%d", SessionID);
    SendMsgAndWaitForResponse(str, UI_DELAY_LONG);
  }
} // end KeyForFile()

/***************************************************************************

FUNCTION:   KeyForQueryItems()

PURPOSE:    Build a key (labels) for the items requested by the UI

COMMENTS:   It builds a string that looks like this:

            Key = "Plen Temp SP:deg,Plen Temp:deg,Cool Avl Temp:deg,Plen Hum SP:%..."

            Note: There is no column label for the 'warnings' query tag.  Each
            warning is displayed on its own line.

***************************************************************************/
void KeyForQueryItems(void)
{
  int i;

  // set up the message
  strcpy(KeyMsg, "Key=");

  // process the query
  for (i = 0; i < DATAQUERY_LEN; i++) // length of query array
  {
    if (DataQuery[i] != -1)   // requested item
    {
      if (i < LOG_MAINITEMS)
        FormatLogMainItems(SysLog.MainPage[i], i, TABLE_MODE, LOG_LABEL);

      else if (i == QUERYTAG_EQUIPSTATUS)
        FormatLogEquipStatus(TABLE_MODE, LOG_LABEL);

      else if (i == QUERYTAG_REMOTESTATUS)
        FormatLogRemoteStatus(TABLE_MODE, LOG_LABEL);

      else if (i == QUERYTAG_CO2 || i >= DATAQUERY_TEMPSNSR_START)
        FormatLogSensor(SysLog.Sensors, DataQuery[i], TABLE_MODE, LOG_LABEL);
    }
  }

  // terminate and send the remaining message
  MultiMsgAdd(KeyMsg, "", "", 1);
} // end KeyForQueryItems()

/***************************************************************************

FUNCTION:   ProcessLogQuery()

PURPOSE:    Parses the UI log query string

COMMENTS:   The query item search references the QueryTags array which is
            defined in QueryTags_Init() and contains the tag, label, and units
            for the activity & history log displays.  The items in the
            query string should either be a QueryTag (hsPlenTmpSet -
            plenum temperature setpoint, hsFanSpd - fan speed, etc.) or a
            temperature or humidity sensor tag (tsen8, hsen116, etc.).  The
            number in the sensor tag is the sensor ID (SID).  The SID can
            be converted to Board (SID/ANALOG_SENSORS_PER_BOARD) and
            Sensor (SID % ANALOG_SENSORS_PER_BOARD).

***************************************************************************/
void ProcessLogQuery(char *QueryItems)
{
  int  HumidIndex = 0;
  char Item[MSG_MAX_VALUE_LEN];
  int  ItemIndex = 0;
  int  QryIndex = 0;
  int  TagFound = 0;
  int  TagIndex = 0;
  int  TempIndex = 0;

  memset(DataQuery, -1, sizeof(DataQuery));

  // parse the UI query string
  while (QryIndex < strlen(QueryItems))
  {
    ItemIndex = 0;
    while (  (QueryItems[QryIndex] != ',') && (QryIndex < strlen(QueryItems))  )
      Item[ItemIndex++] = QueryItems[QryIndex++];
    Item[ItemIndex] = '\0';
    QryIndex++;

    // attempt to find the query item in the QueryTags list
    TagFound = 0;
    TagIndex = 0;
    while (   TagIndex < DATAQUERY_MAINTAGS
           && !TagFound)
    {
      if (strcmp(Item, QueryTags[TagIndex].Tag) == 0)
      {
        if (TagIndex == QUERYTAG_CO2)
          DataQuery[TagIndex] = 7;  // the SID of the CO2 sensor is 7
        else
          DataQuery[TagIndex] = 1;

        TagFound = 1;
      }
      else
        TagIndex++;
    }

    // check if query item is a sensor (tsen8, hsen116, etc. - the numeric part is the sensor ID (SID))
    if (!TagFound)
    {
      // check if temperature sensor (QueryTags[QUERYTAG_TEMPSNSR].Tag = "tsen")
      if (strncmp(Item, QueryTags[QUERYTAG_TEMPSNSR].Tag,
                  strlen(QueryTags[QUERYTAG_TEMPSNSR].Tag)) == 0)
        DataQuery[DATAQUERY_TEMPSNSR_START+TempIndex++] =
                  atoi(Item+strlen(QueryTags[QUERYTAG_TEMPSNSR].Tag)); // extract SID

      // check if humidity sensor (QueryTags[QUERYTAG_HUMSNSR].Tag = "hsen")
      if (strncmp(Item, QueryTags[QUERYTAG_HUMSNSR].Tag,
                  strlen(QueryTags[QUERYTAG_HUMSNSR].Tag)) == 0)
        DataQuery[DATAQUERY_HUMIDSNSR_START+HumidIndex++] =
                  atoi(Item+strlen(QueryTags[QUERYTAG_HUMSNSR].Tag));  // extract SID
    }
  }
} // end ProcessLogQuery()

/***************************************************************************

FUNCTION:   QueryTags_Init()

PURPOSE:    Set up the query tags, column labels, and units for the logs

COMMENTS:   The tag is the item identifier sent in the query string from the UI
            indicating which data point has been requested.  The label is the
            column label for the table or the line label for the graph.  The
            units are also displayed in the table or graph.

***************************************************************************/
void QueryTags_Init(void)
{
  strcpy(QueryTags[LOG_PLENTEMPSET].Tag, "hsPlenTmpSet");
  strcpy(QueryTags[LOG_PLENTEMPSET].Label, "Plen Temp SP");
  strcpy(QueryTags[LOG_PLENTEMPSET].Units, "deg");

  strcpy(QueryTags[LOG_PLENTEMP].Tag, "hsPlenTmp");
  strcpy(QueryTags[LOG_PLENTEMP].Label, "Plen Temp");
  strcpy(QueryTags[LOG_PLENTEMP].Units, "deg");

  strcpy(QueryTags[LOG_COOLAVAILTEMP].Tag, "hsCoolAvl");
  strcpy(QueryTags[LOG_COOLAVAILTEMP].Label, "Cool Avl Temp");
  strcpy(QueryTags[LOG_COOLAVAILTEMP].Units, "deg");

  strcpy(QueryTags[LOG_PLENHUMSET].Tag, "hsPlenHumSet");
  strcpy(QueryTags[LOG_PLENHUMSET].Label, "Plen Hum SP");
  strcpy(QueryTags[LOG_PLENHUMSET].Units, "%");

  strcpy(QueryTags[LOG_MODE].Tag, "hsMode");
  strcpy(QueryTags[LOG_MODE].Label, "Mode");
  strcpy(QueryTags[LOG_MODE].Units, "");

  strcpy(QueryTags[LOG_FANSPEED].Tag, "hsFanSpd");
  strcpy(QueryTags[LOG_FANSPEED].Label, "Fan Speed");
  strcpy(QueryTags[LOG_FANSPEED].Units, "%");

  strcpy(QueryTags[LOG_COOLOUTPUT].Tag, "hsCoolOut");
  strcpy(QueryTags[LOG_COOLOUTPUT].Label, "Cool Output");
  strcpy(QueryTags[LOG_COOLOUTPUT].Units, "%");

  strcpy(QueryTags[LOG_REFRIGOUTPUT].Tag, "hsRefOut");
  strcpy(QueryTags[LOG_REFRIGOUTPUT].Label, "Ref Output");
  strcpy(QueryTags[LOG_REFRIGOUTPUT].Units, "%");

  strcpy(QueryTags[LOG_DAILYFANRUNTIME].Tag, "hsFanRun");
  strcpy(QueryTags[LOG_DAILYFANRUNTIME].Label, "Fan RT");
  strcpy(QueryTags[LOG_DAILYFANRUNTIME].Units, "hrs");

  strcpy(QueryTags[QUERYTAG_CO2].Tag, "csen7");
  strcpy(QueryTags[QUERYTAG_CO2].Units, "ppm");

  strcpy(QueryTags[QUERYTAG_TEMPSNSR].Tag, "tsen");
  strcpy(QueryTags[QUERYTAG_TEMPSNSR].Units, "deg");

  strcpy(QueryTags[QUERYTAG_HUMSNSR].Tag, "hsen");
  strcpy(QueryTags[QUERYTAG_HUMSNSR].Units, "%");

  strcpy(QueryTags[QUERYTAG_ALLITEMS].Tag, "all");
  strcpy(QueryTags[QUERYTAG_EQUIPSTATUS].Tag, "equip");
  strcpy(QueryTags[QUERYTAG_REMOTESTATUS].Tag, "remote");
  strcpy(QueryTags[QUERYTAG_WARNINGS].Tag, "warn");

  // onion elements
  strcpy(QueryTags[LOG_BURNOUTPUT].Tag, "hsBurnOut");
  strcpy(QueryTags[LOG_BURNOUTPUT].Label, "Burn Output");
  strcpy(QueryTags[LOG_BURNOUTPUT].Units, "%");

  strcpy(QueryTags[LOG_CALCHUM].Tag, "hsCalcHum");
  strcpy(QueryTags[LOG_CALCHUM].Label, "Calc Hum");
  strcpy(QueryTags[LOG_CALCHUM].Units, "%");
} // end QueryTags_Init()

/***************************************************************************

FUNCTION:   SaveSensor()

PURPOSE:    Stores a sensor value in a data log record

COMMENTS:   Sensor[].Value is a float and this function is converting it to
            a short.  Temperatures are displayed to the tenth of a degree,
            so temperature values have to be rounded and then multiplied by
            10 before they are stored in the short - 46.5967 => 466.

            The values are stored as shorts to save space in the data log
            records.

***************************************************************************/
void SaveSensor(short *save, int board, int sensor)
{
  if (Settings.AnalogBoard[board].Sensor[sensor].Value == SENSOR_VAL_UNDEFINED)
    *save = SENSOR_VAL_UNDEFINED;
  else
    if (   Settings.AnalogBoard[board].Sensor[sensor].Type == ANALOG_SENSOR_TYPE_TEMP
        || Settings.AnalogBoard[board].Sensor[sensor].Type == ANALOG_SENSOR_TYPE_TEMP_IR)
    {
      *save = (Settings.AnalogBoard[board].Sensor[sensor].Value + .05) * 10;
    }
    else
    {
      *save = Settings.AnalogBoard[board].Sensor[sensor].Value;
    }
} // end SaveSensor()

/***************************************************************************

FUNCTION:   SendAllItems()

PURPOSE:    Sends all system log items

COMMENTS:   This sends a series of messages to the LTX that look like this:

            Data = "46.0,*,51.0,95,1,45,0,0,21.4,*,...,1,0,0,1,0,0,1,0,..."
            Warnings = "Analog Board Communication Error - 1,..."

***************************************************************************/
void SendAllItems(void)
{
  int  i;
  char str[50];

  // send the record info
  if (RequestType == REQTYPE_TOFILE)
  {
    // initiate the multi-message transfer
    UI_SendMultiHdr("LogData,Data,LogAlarms", SessionID);

    // set up the messages
    strcpy(DataMsg, "Data=");
    strcpy(AlarmsMsg, "LogAlarms=");

    // send the record info
    snprintf(str, sizeof(str), "%u,%s,%s,", SysLog.RecordNum, SysLog.Date, SysLog.Time);
    MultiMsgAdd(DataMsg, "Data=", str, 0);
  }

  // debug - RecordNum
//  snprintf(str, sizeof(str), "%u,", SysLog.RecordNum);
//  MultiMsgAdd(DataMsg, "Data=", str, 0);
//  DBGU_SendInt(SysLog.RecordNum);

  // send the main page items
  for (i = 0; i < LOG_MAINITEMS; i++)
  {
    FormatLogMainItems(SysLog.MainPage[i], i, TABLE_MODE, LOG_DATA);
  }

  // send the sensor data
  for (i = 0; i < (ANALOG_BOARDS_PER_SYSTEM * ANALOG_SENSORS_PER_BOARD); i++)
  {
    if (SensorList[i] == 1)
    {
      FormatLogSensor(SysLog.Sensors, i, TABLE_MODE, LOG_DATA);
    }
  }

  FormatLogEquipStatus(TABLE_MODE, LOG_DATA);
  FormatLogRemoteStatus(TABLE_MODE, LOG_DATA);
  BuildWarningList(AlarmsMsg, "LogAlarms=");

  // send the terminator
  if (RequestType == REQTYPE_TOFILE)
  {
    // terminate and send remaining messages
    MultiMsgAdd(DataMsg, "", "", 1);
    MultiMsgAdd(AlarmsMsg, "", "", 1);

    // terminate the multi-message transfer
    UI_SendMultiEnd("LogData");

    // send end-of-record (EOR) message - causes LTX to write record to the file
    snprintf(str, sizeof(str), "LogToFile=EOR,%d", SessionID);
    SendMsgAndWaitForResponse(str, UI_DELAY_LONG);
  }
} // end SendAllItems()

/***************************************************************************

FUNCTION:   SendQueryItems()

PURPOSE:    Sends requested system log items

COMMENTS:   This sends a series of messages to the LTX that look like this:

            Data = "46.0,*,51.0,95,1,45,0,0,21.4,*,...,1,0,0,1,0,0,1,0,..."
            Warnings = "Analog Board Communication Error - 1,..."

***************************************************************************/
void SendQueryItems(void)
{
  int i;

  // process the query
  for (i = 0; i < DATAQUERY_LEN; i++)
  {
    if (DataQuery[i] != -1)   // requested item
    {
      if (i < LOG_MAINITEMS)
        FormatLogMainItems(SysLog.MainPage[i], i, TABLE_MODE, LOG_DATA);

      else if (i == QUERYTAG_EQUIPSTATUS)
        FormatLogEquipStatus(TABLE_MODE, LOG_DATA);

      else if (i == QUERYTAG_REMOTESTATUS)
        FormatLogRemoteStatus(TABLE_MODE, LOG_DATA);

      else if (i == QUERYTAG_WARNINGS)
        BuildWarningList(AlarmsMsg, "LogAlarms=");

      else if (i == QUERYTAG_CO2 || i >= DATAQUERY_TEMPSNSR_START)
        FormatLogSensor(SysLog.Sensors, DataQuery[i], TABLE_MODE, LOG_DATA);   // sensor elements are sensor IDs
    }
  }
} // end SendQueryItems()

/***************************************************************************

FUNCTION:   SystemLogLabel_Init()

PURPOSE:    Set up the labels for the system log items

COMMENTS:

***************************************************************************/
void SystemLogLabel_Init(void)
{
  // record labels
  strcpy(SysLogRecordLabels[SL_RECORD].Label, "Record");
  strcpy(SysLogRecordLabels[SL_DATE].Label, "Date");
  strcpy(SysLogRecordLabels[SL_TIME].Label, "Time");
  // NOTE: these labels aren't record labels there just added here for ease of translation
  strcpy(SysLogRecordLabels[SL_PIPE].Label, "Pipe");  // Load log label
  strcpy(SysLogRecordLabels[SL_PILEFAN_OUTPUT].Label, "Pile Fan Out");
  strcpy(SysLogRecordLabels[SL_PILEFAN_FAIL].Label, "Pile Fan Fail");

  // equipment status labels
  strcpy(SysLogEquipLabels[LOG_FANSWITCH].Label, "Fan Sw");
  strcpy(SysLogEquipLabels[LOG_FANFAIL].Label, "Fan Fail");
  strcpy(SysLogEquipLabels[LOG_FANOUTPUT].Label, "Fan Out");

  strcpy(SysLogEquipLabels[LOG_LIGHTS1IN].Label, "Bay1 Lts In");
  strcpy(SysLogEquipLabels[LOG_LIGHTS2IN].Label, "Bay2 Lts In");
  strcpy(SysLogEquipLabels[LOG_LIGHTS2OUTPUT].Label, "Bay2 Lts Out");

  strcpy(SysLogEquipLabels[LOG_LIGHTS1OUTPUT].Label, "Bay1 Lts Out");

  strcpy(SysLogEquipLabels[LOG_CLIMACELLSWITCH].Label, "Clim Sw");
  strcpy(SysLogEquipLabels[LOG_CLIMACELLFAIL].Label, "Clim Fail");
  strcpy(SysLogEquipLabels[LOG_CLIMACELLOUTPUT].Label, "Clim Out");

  strcpy(SysLogEquipLabels[LOG_HUMIDSWITCH].Label, "Hum Sw");
  strcpy(SysLogEquipLabels[LOG_HUMID1FAIL].Label, "Hum1 Fail");
  strcpy(SysLogEquipLabels[LOG_HUMID1HEADOUTPUT].Label, "Hum1Hd Out");
  strcpy(SysLogEquipLabels[LOG_HUMID1PUMPOUTPUT].Label, "Hum1Pmp Out");
  strcpy(SysLogEquipLabels[LOG_HUMID2FAIL].Label, "Hum2 Fail");
  strcpy(SysLogEquipLabels[LOG_HUMID2HEADOUTPUT].Label, "Hum2Hd Out");
  strcpy(SysLogEquipLabels[LOG_HUMID2PUMPOUTPUT].Label, "Hum2Pmp Out");
  strcpy(SysLogEquipLabels[LOG_HUMID3FAIL].Label, "Hum3 Fail");
  strcpy(SysLogEquipLabels[LOG_HUMID3HEADOUTPUT].Label, "Hum3Hd Out");
  strcpy(SysLogEquipLabels[LOG_HUMID3PUMPOUTPUT].Label, "Hum3Pmp Out");

  strcpy(SysLogEquipLabels[LOG_BURNERSWITCH].Label, "Burn Sw");
  strcpy(SysLogEquipLabels[LOG_BURNERFAIL].Label, "Burn Fail");
  strcpy(SysLogEquipLabels[LOG_BURNEROUTPUT].Label, "Burn Out");

  strcpy(SysLogEquipLabels[LOG_CURESWITCH].Label, "Cure Sw");

  strcpy(SysLogEquipLabels[LOG_REFSWITCH].Label, "Ref Sw");
  strcpy(SysLogEquipLabels[LOG_REFFAIL].Label, "Ref Fail");
  strcpy(SysLogEquipLabels[LOG_REF1OUTPUT].Label, "Ref S1 Out");
  strcpy(SysLogEquipLabels[LOG_REF2OUTPUT].Label, "Ref S2 Out");
  strcpy(SysLogEquipLabels[LOG_REF3OUTPUT].Label, "Ref S3 Out");
  strcpy(SysLogEquipLabels[LOG_REF4OUTPUT].Label, "Ref S4 Out");
  strcpy(SysLogEquipLabels[LOG_REF5OUTPUT].Label, "Ref S5 Out");
  strcpy(SysLogEquipLabels[LOG_REF6OUTPUT].Label, "Ref S6 Out");
  strcpy(SysLogEquipLabels[LOG_REF7OUTPUT].Label, "Ref S7 Out");
  strcpy(SysLogEquipLabels[LOG_REF8OUTPUT].Label, "Ref S8 Out");
  strcpy(SysLogEquipLabels[LOG_REFDEFROST1OUTPUT].Label, "Ref D1 Out");
  strcpy(SysLogEquipLabels[LOG_REFDEFROST2OUTPUT].Label, "Ref D2 Out");

  strcpy(SysLogEquipLabels[LOG_REF1FAIL].Label, "Ref S1 Fail");
  strcpy(SysLogEquipLabels[LOG_REF2FAIL].Label, "Ref S2 Fail");
  strcpy(SysLogEquipLabels[LOG_REF3FAIL].Label, "Ref S3 Fail");
  strcpy(SysLogEquipLabels[LOG_REF4FAIL].Label, "Ref S4 Fail");
  strcpy(SysLogEquipLabels[LOG_REF5FAIL].Label, "Ref S5 Fail");
  strcpy(SysLogEquipLabels[LOG_REF6FAIL].Label, "Ref S6 Fail");
  strcpy(SysLogEquipLabels[LOG_REF7FAIL].Label, "Ref S7 Fail");
  strcpy(SysLogEquipLabels[LOG_REF8FAIL].Label, "Ref S8 Fail");
  strcpy(SysLogEquipLabels[LOG_REFDEFROST1FAIL].Label, "Ref D1 Fail");
  strcpy(SysLogEquipLabels[LOG_REFDEFROST2FAIL].Label, "Ref D2 Fail");

  strcpy(SysLogEquipLabels[LOG_AUX1FAIL].Label, "Aux1 Fail");
  strcpy(SysLogEquipLabels[LOG_AUX1OUTPUT].Label, "Aux1 Out");
  strcpy(SysLogEquipLabels[LOG_AUX2FAIL].Label, "Aux2 Fail");
  strcpy(SysLogEquipLabels[LOG_AUX2OUTPUT].Label, "Aux2 Out");
  strcpy(SysLogEquipLabels[LOG_AUX3FAIL].Label, "Aux3 Fail");
  strcpy(SysLogEquipLabels[LOG_AUX3OUTPUT].Label, "Aux3 Out");
  strcpy(SysLogEquipLabels[LOG_AUX4FAIL].Label, "Aux4 Fail");
  strcpy(SysLogEquipLabels[LOG_AUX4OUTPUT].Label, "Aux4 Out");
  strcpy(SysLogEquipLabels[LOG_AUX5FAIL].Label, "Aux5 Fail");
  strcpy(SysLogEquipLabels[LOG_AUX5OUTPUT].Label, "Aux5 Out");
  strcpy(SysLogEquipLabels[LOG_AUX6FAIL].Label, "Aux6 Fail");
  strcpy(SysLogEquipLabels[LOG_AUX6OUTPUT].Label, "Aux6 Out");
  strcpy(SysLogEquipLabels[LOG_AUX7FAIL].Label, "Aux7 Fail");
  strcpy(SysLogEquipLabels[LOG_AUX7OUTPUT].Label, "Aux7 Out");
  strcpy(SysLogEquipLabels[LOG_AUX8FAIL].Label, "Aux8 Fail");
  strcpy(SysLogEquipLabels[LOG_AUX8OUTPUT].Label, "Aux8 Out");

  strcpy(SysLogEquipLabels[LOG_HEATFAIL].Label, "Heat Fail");
  strcpy(SysLogEquipLabels[LOG_HEATOUTPUT].Label, "Heat Out");

  strcpy(SysLogEquipLabels[LOG_DOORSWITCH].Label, "Door Sw");
  strcpy(SysLogEquipLabels[LOG_DOOROUTPUT].Label, "Door Out");

  strcpy(SysLogEquipLabels[LOG_CAVHEATFAIL].Label, "CavHt Fail");
  strcpy(SysLogEquipLabels[LOG_CAVHEATOUTPUT].Label, "CavHt Out");

  strcpy(SysLogEquipLabels[LOG_GREEN].Label, "Green Lt");
  strcpy(SysLogEquipLabels[LOG_YELLOW].Label, "Yellow Lt");
  strcpy(SysLogEquipLabels[LOG_RED].Label, "Red Lt");

  // equipment remote off labels
  strcpy(SysLogRemoteLabels[LOG_DIAG_REF1].Label, "Diag R1");
  strcpy(SysLogRemoteLabels[LOG_DIAG_REF2].Label, "Diag R2");
  strcpy(SysLogRemoteLabels[LOG_DIAG_REF3].Label, "Diag R3");
  strcpy(SysLogRemoteLabels[LOG_DIAG_REF4].Label, "Diag R4");
  strcpy(SysLogRemoteLabels[LOG_DIAG_REF5].Label, "Diag R5");
  strcpy(SysLogRemoteLabels[LOG_DIAG_REF6].Label, "Diag R6");
  strcpy(SysLogRemoteLabels[LOG_DIAG_REF7].Label, "Diag R7");
  strcpy(SysLogRemoteLabels[LOG_DIAG_REF8].Label, "Diag R8");
  strcpy(SysLogRemoteLabels[LOG_DIAG_DEFROST1].Label, "Diag Def1");
  strcpy(SysLogRemoteLabels[LOG_DIAG_DEFROST2].Label, "Diag Def2");

  strcpy(SysLogRemoteLabels[LOG_REMOTEOFF_REF].Label, "RemOff Ref");
  strcpy(SysLogRemoteLabels[LOG_REMOTEOFF_FAN].Label, "RemOff Fan");

  strcpy(SysLogRemoteLabels[LOG_REMOTEOFF_CLIMACELL].Label, "RemOff Clim");
  strcpy(SysLogRemoteLabels[LOG_REMOTEOFF_HUMID1].Label, "RemOff Hum1");
  strcpy(SysLogRemoteLabels[LOG_REMOTEOFF_BURNER].Label, "RemOff Burn");
  strcpy(SysLogRemoteLabels[LOG_REMOTEOFF_CURE].Label, "RemOff Cure");

  strcpy(SysLogRemoteLabels[LOG_REMOTEOFF_HUMID2].Label, "RemOff Hum2");
  strcpy(SysLogRemoteLabels[LOG_REMOTEOFF_HUMID3].Label, "RemOff Hum3");
  strcpy(SysLogRemoteLabels[LOG_REMOTEOFF_LIGHTS1].Label, "RemOff Bay1 Lts");
  strcpy(SysLogRemoteLabels[LOG_REMOTEOFF_LIGHTS2].Label, "RemOff Bay2 Lts");

  strcpy(SysLogRemoteLabels[LOG_REMOTEOFF_HEAT].Label, "RemOff Ht");

  strcpy(SysLogRemoteLabels[LOG_REMOTEOFF_AUX1].Label, "RemOff Aux1");
  strcpy(SysLogRemoteLabels[LOG_REMOTEOFF_AUX2].Label, "RemOff Aux2");
  strcpy(SysLogRemoteLabels[LOG_REMOTEOFF_AUX3].Label, "RemOff Aux3");
  strcpy(SysLogRemoteLabels[LOG_REMOTEOFF_AUX4].Label, "RemOff Aux4");
  strcpy(SysLogRemoteLabels[LOG_REMOTEOFF_AUX5].Label, "RemOff Aux5");
  strcpy(SysLogRemoteLabels[LOG_REMOTEOFF_AUX6].Label, "RemOff Aux6");
  strcpy(SysLogRemoteLabels[LOG_REMOTEOFF_AUX7].Label, "RemOff Aux7");
  strcpy(SysLogRemoteLabels[LOG_REMOTEOFF_AUX8].Label, "RemOff Aux8");

  strcpy(SysLogRemoteLabels[LOG_REMOTEOFF_CAVHEAT].Label, "RemOff CvHt");

  // PID log labels
  strcpy(PIDLogLabels[0].Label, "Error");
  strcpy(PIDLogLabels[1].Label, "P");
  strcpy(PIDLogLabels[2].Label, "I");
  strcpy(PIDLogLabels[3].Label, "D");
  strcpy(PIDLogLabels[4].Label, "Output");
  strcpy(PIDLogLabels[5].Label, "Type");

  // mode labels
  strcpy(SysModes[UI_SHUTDOWN].Label,         "Shutdown");
  strcpy(SysModes[UI_STANDBY].Label,          "Standby");
  strcpy(SysModes[UI_REMOTE_STANDBY].Label,   "Rem Stby");
  strcpy(SysModes[UI_COOLING].Label,          "Cooling");
  strcpy(SysModes[UI_REFRIG].Label,           "Refrig");
  strcpy(SysModes[UI_RECIRC].Label,           "Recirc");
  strcpy(SysModes[UI_HEATING].Label,          "Heating");
  strcpy(SysModes[UI_DEFROST].Label,          "Defrost");
  strcpy(SysModes[UI_PURGE].Label,            "CO2 Purge");
  strcpy(SysModes[UI_RAMPCOOL].Label,         "Cool Ramp");
  strcpy(SysModes[UI_RAMPREFRIG].Label,       "Refrig Ramp");
  strcpy(SysModes[UI_FAN_MANUAL].Label,       "Fan Manual");
  strcpy(SysModes[UI_FAN_OFF].Label,          "Fan Switch Off");
  strcpy(SysModes[UI_FAN_REMOTEOFF].Label,    "Fan Remote Off");
  strcpy(SysModes[UI_REFRIG_REMOTEOFF].Label, "Refrig Remote Off");
  strcpy(SysModes[UI_AIRCURE].Label,          "Air Cure");
  strcpy(SysModes[UI_BURNERCURE].Label,       "Burner Cure");
  strcpy(SysModes[UI_COOLDEHUMID].Label,      "Cool Dehumid");
  strcpy(SysModes[UI_REFRIGDEHUMID].Label,    "Refrig Dehumid");
  strcpy(SysModes[UI_SYSTEM_REMOTEOFF].Label, "Remote Off");
  strcpy(SysModes[UI_FAILURE].Label,          "Failure");
} // end SystemLogLabel_Init()

/***************************************************************************

FUNCTION:   StoreQueryTag()

PURPOSE:    Translate the log query tags

COMMENTS:

***************************************************************************/
void StoreQueryTag(void)
{
  uint8_t queryTag;

  if (   StorePostValue("queryTag", &queryTag, ET_UINT8, NULL) == SPV_SUCCESS
      && queryTag < DATAQUERY_TOTALTAGS)
  {
    if (StorePostValue("name", QueryTags[queryTag].Label, ET_STRING, LOG_LABELS) == SPV_SUCCESS)
    {
      debug_printf("Query Tag Translate - %s\r\n", QueryTags[queryTag].Label);
    }
    else
    {
      debug_printf("QueryTags Error - %s\r\n", PostValue[1]);
    }
  }
  else
  {
    debug_printf("QueryTags Index Error\r\n");
  }
} // end StoreQueryTag()

/***************************************************************************

FUNCTION:   StoreSysLogRec()

PURPOSE:    Translate the system log record labels

COMMENTS:

***************************************************************************/
void StoreSysLogRec(void)
{
  uint8_t recLabel;

  if (   StorePostValue("syslogRec", &recLabel, ET_UINT8, NULL) == SPV_SUCCESS
      && recLabel < NUM_SYSLOG_RECORD_LABELS)
  {
    if (StorePostValue("name", SysLogRecordLabels[recLabel].Label, ET_STRING, LOG_LABELS) == SPV_SUCCESS)
    {
      debug_printf("SysLog Rec Translate - %s\r\n", SysLogRecordLabels[recLabel].Label);
    }
    else
    {
      debug_printf("SysLogRecordLabels Error - %s\r\n", PostValue[1]);
    }
  }
  else
  {
    debug_printf("SysLogRecordLabels Index Error\r\n");
  }
} // end StoreSysLogRec()

/***************************************************************************

FUNCTION:   StoreSysLogEquip()

PURPOSE:    Translate the system log eqipment labels

COMMENTS:

***************************************************************************/
void StoreSysLogEquip(void)
{
  uint8_t logLabel;

  if (   StorePostValue("syslogEquip", &logLabel, ET_UINT8, NULL) == SPV_SUCCESS
      && logLabel < SYSLOG_EQUIP_ITEMS)
  {
    if (StorePostValue("name", SysLogEquipLabels[logLabel].Label, ET_STRING, LOG_LABELS) == SPV_SUCCESS)
    {
      debug_printf("SysLog Equip Translate - %s\r\n", SysLogEquipLabels[logLabel].Label);
    }
    else
    {
      debug_printf("SysLogEquipLabels Error - %s\r\n", PostValue[1]);
    }
  }
  else
  {
    debug_printf("SysLogEquipLabels Index Error\r\n");
  }
} // end StoreSysLogEquip()

/***************************************************************************

FUNCTION:   StoreSysLogRemote()

PURPOSE:    Translate the system log remote off labels

COMMENTS:

***************************************************************************/
void StoreSysLogRemote(void)
{
  uint8_t remoteLabel;

  if (   StorePostValue("syslogRemote", &remoteLabel, ET_UINT8, NULL) == SPV_SUCCESS
      && remoteLabel < SYSLOG_REMOTE_NUM_LABELS)
  {
    if (StorePostValue("name", SysLogRemoteLabels[remoteLabel].Label, ET_STRING, LOG_LABELS) == SPV_SUCCESS)
    {
      debug_printf("SysLog Remote Translate - %s\r\n", SysLogRemoteLabels[remoteLabel].Label);
    }
    else
    {
      debug_printf("SysLogRemoteLabels Error - %s\r\n", PostValue[1]);
    }
  }
  else
  {
    debug_printf("SysLogRemoteLabels Index Error\r\n");
  }
} // end StoreSysLogRemote()

/***************************************************************************

FUNCTION:   StorePIDLogLabel()

PURPOSE:    Translate the PID log labels

COMMENTS:

***************************************************************************/
void StorePIDLogLabel(void)
{
  uint8_t pidLabel;

  if (   StorePostValue("pidLog", &pidLabel, ET_UINT8, NULL) == SPV_SUCCESS
      && pidLabel < NUM_PIDLOG_ITEMS)
  {
    if (StorePostValue("name", PIDLogLabels[pidLabel].Label, ET_STRING, LOG_LABELS) == SPV_SUCCESS)
    {
      debug_printf("PID Log Translate - %s\r\n", PIDLogLabels[pidLabel].Label);
    }
    else
    {
      debug_printf("PIDLogLabels Error - %s\r\n", PostValue[1]);
    }
  }
  else
  {
    debug_printf("PIDLogLabels Index Error\r\n");
  }
} // end StorePIDLogLabel()

/***************************************************************************

FUNCTION:   StoreSysMode()

PURPOSE:    Translate the system modes

COMMENTS:

***************************************************************************/
void StoreSysMode(void)
{
  uint8_t sysMode;

  if (   StorePostValue("sysmode", &sysMode, ET_UINT8, NULL) == SPV_SUCCESS
      && sysMode < NUM_UI_MODES)
  {
    if (StorePostValue("name", SysModes[sysMode].Label, ET_STRING, LOG_LABELS) == SPV_SUCCESS)
    {
      debug_printf("System Mode Translate - %s\r\n", SysModes[sysMode].Label);
    }
    else
    {
      debug_printf("PIDLogLabels Error - %s\r\n", PostValue[1]);
    }
  }
  else
  {
    debug_printf("PIDLogLabels Index Error\r\n");
  }
} // end StoreSysMode()

/***************************************************************************

FUNCTION:   SystemLogRead()

PURPOSE:    Reads and processes the requested records from flash

COMMENTS:   This reads in each record of the requested range and calls the
            passed in function to process the record.

***************************************************************************/
void SystemLogRead(int EndPage, int TotalRecords, void (*CallFunction)(void))
{
  int GetPage = EndPage;
  int Status = 0;

  if (ActiveSDCard.Semaphore != NULL)
  {
    // Obtain the semaphore - block if the semaphore is not immediately available.
    if (xSemaphoreTake(ActiveSDCard.Semaphore, ActiveSDCard.BlockTimeout) == pdTRUE)
    {
      while (TotalRecords--)
      {
        memset(&SysLog, 0, sizeof(SysLog));
        Status = SDCardRead((char *)&SysLog, sizeof(SYSLOG_RECORD), GetPage);
        if (Status == 0)
        {
          break;  // if there is a problem reading a record, skip it and try the next
        }

        // call the passed in function
        (*CallFunction)();

        GetPage -= SDCardHeader.SysLog.BlocksPerRecord;
        if (GetPage < SDCardHeader.SysLog.StartBlock)
        {
          GetPage = ((NUM_SYSLOG_RECS - 1) * SDCardHeader.SysLog.BlocksPerRecord) + SDCardHeader.SysLog.StartBlock;
        }

        // reset the watchdog external timer
        ThreadMonitorUpdate(TM_UI_UPDATE);

        // abort the read if the LTX sent a KILL
        if (KillMessage == SessionID)
        {
          break;
        }
      }

      // Release the semaphore.
      xSemaphoreGive(ActiveSDCard.Semaphore);
    }
    else
    {
      debug_printf("SystemLogRead blocked\r\n");
    }
  }
  else
  {
    ActiveSDCard.Initialized = 0;
  }
} // end SystemLogRead()

/***************************************************************************

FUNCTION:   SystemLogReadWarnings()

PURPOSE:    Reads and processes the records from flash

COMMENTS:

***************************************************************************/
void SystemLogReadWarnings(int EndPage, int TotalWarnings)
{
  int i = 0;
  int GetPage = EndPage;
  int Status = 0;
  char str[50];

  memset(PreviousWarnings, 0, sizeof(WARNING_LOG) * NUM_WARNINGS);

  if (ActiveSDCard.Semaphore != NULL)
  {
    // Obtain the semaphore - block if the semaphore is not immediately available.
    if (xSemaphoreTake(ActiveSDCard.Semaphore, ActiveSDCard.BlockTimeout) == pdTRUE)
    {
      while (WarningCount < TotalWarnings && i < SDCardHeader.SysLog.NumRecords)
      {
        memset(&SysLog, 0, sizeof(SysLog));
        Status = SDCardRead((char *)&SysLog, sizeof(SYSLOG_RECORD), GetPage);
        if (Status == 0)
        {
          break;  // if there is a problem reading a record, skip it and try the next
        }

        FindWarnings();

        i++;
        GetPage -= SDCardHeader.SysLog.BlocksPerRecord;
        if (GetPage < SDCardHeader.SysLog.StartBlock)
        {
          GetPage = ((NUM_SYSLOG_RECS - 1) * SDCardHeader.SysLog.BlocksPerRecord) + SDCardHeader.SysLog.StartBlock;
        }

        // reset the watchdog external timer
        ThreadMonitorUpdate(TM_UI_UPDATE);

        // send a keep alive so the UI doesn't timeout
        if ((i % 1000) == 0)
        {
          snprintf(str, sizeof(str), "LogSearch=%d,%d", i, SessionID);
          SendMsgAndWaitForResponse(str, UI_DELAY_LONG);
        }

        // abort the read if the LTX sent a KILL
        if (KillMessage == SessionID)
        {
          break;
        }
      }

      // Release the semaphore.
      xSemaphoreGive(ActiveSDCard.Semaphore);
    }
    else
    {
      debug_printf("SystemLogReadWarnings blocked\r\n");
    }
  }
  else
  {
    ActiveSDCard.Initialized = 0;
  }
} // end SystemLogReadWarnings()

/***************************************************************************

FUNCTION:   SystemLogReset()

PURPOSE:    Reset/initializes the system log and flash header

COMMENTS:   Callers that don't want the header written must obvioulsy write
            the header themselves and therefore should lock the sd card
            semaphore.  Callers wanting the header written should not lock
            the sd card semaphore before this call.

***************************************************************************/
void SystemLogReset(SYSLOG_HEADER *Header, int WriteHeader)
{
  int NumBlocks = ActiveSDCard.Size;

  WarningsSet(WARN_ACTLOG_CLEAR, FM_ALARM, FM_ALARM, NA);

  Header->RecordLen = sizeof(SYSLOG_RECORD);

  Header->BlocksPerRecord = Header->RecordLen/SDCARD_BLOCK_SIZE;
  if ((Header->BlocksPerRecord * SDCARD_BLOCK_SIZE) < Header->RecordLen)
  {
    Header->BlocksPerRecord++;
  }

  Header->MaxBlocks = NUM_SYSLOG_RECS * Header->BlocksPerRecord;
  Header->StartBlock = NumBlocks - Header->MaxBlocks - SDCARD_PIDLOG_MAXBLOCKS;
  Header->WriteBlock = Header->StartBlock;
  Header->NumRecords = 0;

  if (WriteHeader)
  {
    if (ActiveSDCard.Semaphore != NULL)
    {
      // Obtain the semaphore - block if the semaphore is not immediately available.
      if (xSemaphoreTake(ActiveSDCard.Semaphore, ActiveSDCard.BlockTimeout) == pdTRUE)
      {
        SDCardHeaderUpdate();

        // Release the semaphore.
        xSemaphoreGive(ActiveSDCard.Semaphore);
      }
      else
      {
        debug_printf("SystemLogReset blocked at SDCardHeaderUpdate\r\n");
      }
    }
    else
    {
      ActiveSDCard.Initialized = 0;
    }
  }
} // end SystemLogReset()

/***************************************************************************

FUNCTION:   SystemLogSendToFile()

PURPOSE:    Send the requested system log records to USB storage device

COMMENTS:   This sends the records to the LTX which sends them to the Linux
            display board which creates a file on a USB storage device.

            This is going to send a series of messages to the LTX that look
            like this:

            Data = "46.0,*,51.0,95,1,45,0,0,21.4,*,...,1,0,0,1,0,0,1,0,..."
            Warnings = "Analog Board Communication Error - 1,..."
            Data_Loaded = "true";

            This will make a first pass through the requested data to
            determine the appropriate sensor labels needed for the key - for
            an explanation of this process see BuildSensorList().
            Then it sets up each of the messages and processes the data
            sending multi-messages as necessary.  Then it sends the transfer
            complete message.

            Although it's very similar to SystemLogSendToUI(), this function
            doesn't send the key, dates, and timestamps messages because the
            date and time information is included in every record and the
            key (labels) is sent and treated like a regular data record.

***************************************************************************/
int SystemLogSendToFile(int BeginRecord, int EndRecord, int Session)
{
  int  TotalRecords;
  char str[50];

  // set the modular SessionID variable and clear the kill flag
  SessionID = Session;
  KillMessage = -1;

  if (   SDCardHeader.SysLog.NumRecords == 0
      || BeginRecord > EndRecord)
  {
    // close the file opened on the USB device
    snprintf(str, sizeof(str), "LogToFile=EOF,%d", SessionID);
    SendMsgAndWaitForResponse(str, UI_DELAY_LONG);
    return 0;
  }

  // suspend slave mode during log transmission
  SendingLogData = 1;
  UI_SendBasicSetup(MSG_SEND);

  RequestType = REQTYPE_TOFILE;

  TotalRecords = (EndRecord - BeginRecord) + 1;
  snprintf(str, sizeof(str), "LogTotal=%d,%d", TotalRecords, SessionID);
  SendMsgAndWaitForResponse(str, UI_DELAY_LONG);

  //  first pass through the data to build the SensorList
  memset(SensorList, 0, sizeof(SensorList));
  GetLogRecords(BeginRecord, EndRecord, BuildSensorList);

  // send the key (labels)
  KeyForFile();

  // send the data
  GetLogRecords(BeginRecord, EndRecord, SendAllItems);

  // send the end of file
  snprintf(str, sizeof(str), "LogToFile=EOF,%d", SessionID);
  SendMsgAndWaitForResponse(str, UI_DELAY_LONG);

  // re-enable slave mode (if in slave mode)
  SendingLogData = 0;
  UI_SendBasicSetup(MSG_SEND);

  return 1;
} // end SystemLogSendToFile()

/***************************************************************************

FUNCTION:   SystemLogSendToUI()

PURPOSE:    Send the requested system log records to the LTX/UI

COMMENTS:   This is going to send a series of messages to the LTX that look
            like this:

            Key = "Plen Temp SP:deg,Plen Temp:deg,Cool Avl Temp:deg,..."
            Dates = "09/28/2007:20,"
            TimeStamps = "09:25:45#12,09:24:45#12,09:23:44#0,09:22:44#0,..."
            Data = "46.0,*,51.0,95,1,45,0,0,21.4,*,...,1,0,0,1,0,0,1,0,..."
            Warnings = "Analog Board Communication Error - 1,..."
            Data_Loaded = "true";

            This will make a first pass through the requested data to
            determine the appropriate sensor labels needed for the key - for
            an explanation of this process see BuildSensorList().
            Then it sets up each of the messages and processes the data
            sending multi-messages as necessary.  Then it will terminate
            and send any unfull/unsent messages and send the transfer
            complete message.

***************************************************************************/
int SystemLogSendToUI(int BeginRecord, int EndRecord, int Session, int WarningsOnly)
{
  char str[20];

  // set the modular SessionID variable and clear the kill flag
  SessionID = Session;
  KillMessage = -1;

  if (   SDCardHeader.SysLog.NumRecords == 0
      || BeginRecord > EndRecord)
  {
    // clear the variables in the LTX
    UI_SendMultiHdr("LogData,Key,Dates,TimeStamps,Data,LogAlarms", SessionID);
    UI_SendMultiEnd("LogData");
    return 0;
  }

  // suspend slave mode during log transmission
  SendingLogData = 1;
  UI_SendBasicSetup(MSG_SEND);

  RequestType = REQTYPE_UI;
  PointsPerDate = 0;

  // when sending to the UI the messages sent don't correspond to the number of
  // history records so send a zero so the UI can change the display
  snprintf(str, sizeof(str), "LogTotal=%d,%d", 0, SessionID);
  SendMsgAndWaitForResponse(str, UI_DELAY_LONG);

  if (WarningsOnly)
  {
    // set DataQuery manually for the warning query
    memset(DataQuery, -1, sizeof(DataQuery));
    DataQuery[LOG_MODE] = 1;
    DataQuery[QUERYTAG_WARNINGS] = 1;
    WarningCount = 0;
  }
  else
  {
    // first pass through the data to build the SensorList
    memset(SensorList, 0, sizeof(SensorList));
    GetLogRecords(BeginRecord, EndRecord, BuildSensorList);
  }

  // initiate the multi-message transfer
  UI_SendMultiHdr("LogData,Key,Dates,TimeStamps,Data,LogAlarms", SessionID);

  // set up the messages
  strcpy(DatesMsg, "Dates=");
  strcpy(TimesMsg, "TimeStamps=");
  strcpy(DataMsg, "Data=");
  strcpy(AlarmsMsg, "LogAlarms=");

  // read the log & send the data
  if (WarningsOnly)
  {
    GetWarnings(EndRecord);
  }
  else
  {
    GetLogRecords(BeginRecord, EndRecord, BuildMessages);
  }

  if (KillMessage != SessionID)
  {
    // terminate and send remaining messages
    snprintf(str, sizeof(str), "%s?%d,", SysLogDate, PointsPerDate);
    MultiMsgAdd(DatesMsg, "Dates=", str, 1);
    MultiMsgAdd(TimesMsg, "", "", 1);
    MultiMsgAdd(DataMsg, "", "", 1);
    MultiMsgAdd(AlarmsMsg, "", "", 1);

    // build the key (labels)
    if (DataQuery[QUERYTAG_ALLITEMS] == -1)
    {
      KeyForQueryItems();
    }
    else
    {
      KeyForAllItems();
    }
  }

  // terminate the multi-message transfer
  UI_SendMultiEnd("LogData");

  // re-enable slave mode (if in slave mode)
  SendingLogData = 0;
  UI_SendBasicSetup(MSG_SEND);

  return 1;
} // end SystemLogSendToUI()

/***************************************************************************

FUNCTION:   SystemLogWrite()

PURPOSE:    Build a system log record and write it to flash

COMMENTS:

***************************************************************************/
int SystemLogWrite(int AlarmCleared)
{
  int  i,j;
  char Hour = 0;
  char Min = 0;
  char Sec = 0;
  int  Status = 1;
  int  WriteSuccess = 0;
  int  WriteRetries = 0;
  int  ReadRetries = 0;
  SYSLOG_RECORD CheckRec;

  if (   ActiveSDCard.Initialized == 0
      || ActiveSDCard.Semaphore == NULL)
  {
    ActiveSDCard.Initialized = 0;
    WarningsSet(WARN_SDCARD_NONE, FM_ALARM, FM_ALARM, NA);
    return 1;
  }

  // initialize the activity log record
  memset(&SysLog, 0, sizeof(SysLog));
  for (i = 0; i < (ANALOG_BOARDS_PER_SYSTEM * ANALOG_SENSORS_PER_BOARD); i++)
  {
    SysLog.Sensors[i] = SENSOR_VAL_UNDEFINED;
  }

  if (   GetDateStr(SysLog.Date) == 0
      || GetTime(&Hour, &Min, &Sec) == 0)
  {
    return 1;
  }

  // store the record information
  SysLog.RecordNum = SDCardHeader.SysLog.NumRecords + 1;
  snprintf(SysLog.Time, TIME_LEN+1, "%02d:%02d:%02d", Hour, Min, Sec);

  // store system monitor page information
  if (PlenumTempAvg == SENSOR_VAL_UNDEFINED)
  {
    SysLog.MainPage[LOG_PLENTEMP] = SENSOR_VAL_UNDEFINED;
  }
  else
  {
    SysLog.MainPage[LOG_PLENTEMP] = (PlenumTempAvg + .05) * 10;
  }

  if (CurrentMode == UI_FAILURE)
  {
    SysLog.MainPage[LOG_FANSPEED] = PWMValToPercent(PWM_MIN_VALUE);
  }
  else
  {
    SysLog.MainPage[LOG_FANSPEED] = PWMValToPercent(PwmChannel[PWM_FAN].Output);
  }

  SysLog.MainPage[LOG_PLENTEMPSET]     = (Settings.Plenum.TempSet + .05) * 10;
  SysLog.MainPage[LOG_COOLAVAILTEMP]   = (StartTemp + .05) * 10;
  SysLog.MainPage[LOG_PLENHUMSET]      = Settings.Plenum.HumidSet;
  SysLog.MainPage[LOG_MODE]            = CurrentMode;
  SysLog.MainPage[LOG_DAILYFANRUNTIME] = (Settings.Fan.DailyRunTime/3600.0 + .05) * 10;
  SysLog.MainPage[LOG_REFRIGOUTPUT]    = PWMValToPercent(PwmChannel[PWM_REFRIGERATION].Output);
  SysLog.MainPage[LOG_COOLOUTPUT]      = PWMValToPercent(PwmChannel[PWM_DOORS].Output);
  SysLog.MainPage[LOG_BURNOUTPUT]      = PWMValToPercent(PwmChannel[PWM_BURNER].Output);    // onion
  SysLog.MainPage[LOG_CALCHUM]         = CalculatedHumidity();   // onion

  // store the sensor values
  for (i = 0; i < ANALOG_BOARDS_PER_SYSTEM; i++)
  {
    if (Settings.AnalogBoard[i].Present == 1)
    {
      for (j = 0; j < ANALOG_SENSORS_PER_BOARD; j++)
      {
        SaveSensor(&(SysLog.Sensors[(i*ANALOG_SENSORS_PER_BOARD) + j]), i, j);
      }
    }
  }

  // store the equipment status
  GetEquipStatus(SysLog.EquipStatus, EQUIPSTATUS_LEN);

  // store the remote off and diagnostic control settings
  i = 0;
  for (j = 0; j < NUM_REFRIG_STAGES; j++)
  {
    SysLog.RemoteOff[i++] = Settings.Refrig.Stage[j].Diagnostic;
  }

  for (j = 0; j < NUM_DEFROST_STAGES; j++)
  {
    SysLog.RemoteOff[i++] = Settings.Refrig.Defrost[j].Diagnostic;
  }

  SysLog.RemoteOff[i++] = Settings.RemoteOff[RO_REFRIGERATION];
  SysLog.RemoteOff[i++] = Settings.RemoteOff[RO_FAN];

  if (Settings.SystemMode == SM_POTATO)
  {
    SysLog.RemoteOff[i++] = Settings.RemoteOff[RO_CLIMACELL];
    SysLog.RemoteOff[i++] = Settings.RemoteOff[RO_HUMIDIFIER1];
    SysLog.RemoteOff[i++] = Settings.RemoteOff[RO_HUMIDIFIER2];
    SysLog.RemoteOff[i++] = Settings.RemoteOff[RO_HUMIDIFIER3];
//    SysLog.RemoteOff[i++] = Settings.RemoteOff[RO_AUX1];
    SysLog.RemoteOff[i++] = Settings.RemoteOff[RO_HEAT];
  }
  else
  {
    SysLog.RemoteOff[i++] = Settings.RemoteOff[RO_BURNER];
    SysLog.RemoteOff[i++] = Settings.RemoteOff[RO_CURE];
    SysLog.RemoteOff[i++] = 0;  // unused in onion mode
    SysLog.RemoteOff[i++] = 0;  // unused in onion mode
    SysLog.RemoteOff[i++] = 0;  // unused in onion mode
//    SysLog.RemoteOff[i++] = Settings.RemoteOff[RO_AUX1];
//    SysLog.RemoteOff[i++] = Settings.RemoteOff[RO_AUX2];
  }

  for (j = 0; j < NUM_AUX_OUTPUTS; j++)
  {
    SysLog.RemoteOff[i++] = Settings.RemoteOff[RO_AUX1 + j];
  }

  SysLog.RemoteOff[i++] = Settings.RemoteOff[RO_CAVITY_HEAT];

  if (Settings.SystemMode == SM_POTATO)
  {
    SysLog.RemoteOff[i++] = Settings.RemoteOff[RO_LIGHTS1];
    SysLog.RemoteOff[i++] = Settings.RemoteOff[RO_LIGHTS2];
  }

  // store the warnings
  for (i = 0; i < NUM_WARNINGS; i++)
  {
    if (i != WARN_MODECHANGE)   // don't log mode change alarms (they're email notifications only)
    {
      SysLog.Warning[i].Status = WarningStatus((WARNING_ITEMS) i);
      WarningValue((WARNING_ITEMS) i, SysLog.Warning[i].Value);
    }
  }

  if (AlarmCleared == 1)
  {
    SysLog.Warning[WARN_CLEARALERTS].Status = 1;
    SysLog.Warning[WARN_CLEARALERTS].Value[0] = 0;
    SysLog.Warning[WARN_CLEARALERTS].Value[1] = 0;
  }

  // Obtain the semaphore - block if the semaphore is not immediately available.
  if (xSemaphoreTake(ActiveSDCard.Semaphore, ActiveSDCard.BlockTimeout) == pdTRUE)
  {
    while (!WriteSuccess && WriteRetries < 3)
    {
      // write the record to the SD card
      Status = SDCardSave((unsigned char *)&SysLog, SDCardHeader.SysLog.RecordLen, SDCardHeader.SysLog.WriteBlock);
      if (Status == 0)
      {
        // check the accuracy of the write
        ReadRetries = 0;
        while (   SDCardRead((char *)&CheckRec, SDCardHeader.SysLog.RecordLen, SDCardHeader.SysLog.WriteBlock) != 1
               && ReadRetries++ < 3)
        {
          debug_printf("SD card/Activity Log read retry\r\n");
        }

        if (memcmp(&CheckRec, &SysLog, sizeof(SYSLOG_RECORD)) != 0)
        {
          debug_printf("SD card/Activity Log write retry\r\n");
          WriteRetries++;
        }
        else
        {
          // write was successful, update header
          WriteSuccess = 1;

          if (SDCardHeader.SysLog.NumRecords < NUM_SYSLOG_RECS)
          {
            SDCardHeader.SysLog.NumRecords++;
          }

          SDCardHeader.SysLog.WriteBlock += SDCardHeader.SysLog.BlocksPerRecord;

          // handle wrapping the data around on the card
          if (SDCardHeader.SysLog.WriteBlock >= (SDCardHeader.SysLog.StartBlock + SDCardHeader.SysLog.MaxBlocks))
          {
            SDCardHeader.SysLog.WriteBlock = SDCardHeader.SysLog.StartBlock;
          }
        }
      }
      else
      {
        WriteRetries++;
      }
    } // while (!WriteSuccess)

    // if the write failed, report the error
    if (WriteRetries >= 3)
    {
      SDCardHeader.Errors.SystemLog++;
      SDCardHeader.Errors.Write++;
      ActiveSDCard.Initialized = 0;

      if ((SDCardHeader.Errors.Write % 20) == 0)
      {
        WarningsSet(WARN_ACTLOGWRITE, FM_ALARM, FM_ALARM, NA);
      }
    }

    SDCardHeaderUpdate();

    // Release the semaphore.
    xSemaphoreGive(ActiveSDCard.Semaphore);
  }
  else
  {
    debug_printf("SystemLogWrite blocked for SDCardSave\r\n");
  }

  return(Status);
} // end SystemLogWrite()

/***   End Of File   ***/
