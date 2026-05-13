/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     LoadLogs.c

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:  Read and write the data logs

COMMENTS: LoadLog => Storage Loading Monitor Log data

***************************************************************************/

/*** include files ***/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"

#include "RS485.h"
#include "RTC.h"
#include "SDCard.h"
#include "States.h"
#include "SystemLogs.h"
#include "ThreadMonitor.h"
#include "UI_Messages.h"
#include "UserLogs.h"
#include "LoadLogs.h"

/*** typedefs and structures ***/

/*** module variables ***/

static LOADLOG_RECORD LoadLog[SDCARD_LOADLOG_RECSPERBLOCK];
static LOADLOG_RECORD LoadLogBuf[SDCARD_LOADLOG_RECSPERBLOCK];
static LOADLOG_AVERAGES LoadingTemps[NUM_LOADLOG_SENSORS];

static char LoadLogIndex = 0;
static int  SessionID;

/*** static functions ***/

static void KeyForFile(void);
static void KeyForUI(int Bay, int Mode);
static void SendQueryItems(LOADLOG_RECORD *LoadLog, int Bay, char *RecordLabel, int *PointsPerLabel, int Mode);
static void SendAllItems(LOADLOG_RECORD *LoadLog);

/***************************************************************************

FUNCTION:   KeyForFile()

PURPOSE:    Send the key (column labels) to the LTX (then to USB storage device)

COMMENTS:   It builds a string that looks like this:

            Data = "Plen Temp SP:deg,Plen Temp:deg,Cool Avl Temp:deg,..."

            In this case the 'key' is sent with the 'data' message tag because
            it is just treated like a regular data record when it is written
            out to the USB storage device.

***************************************************************************/
void KeyForFile(void)
{
  char str[50];

  // initiate the multi-message transfer
  UI_SendMultiHdr("LogData,Data", SessionID);

  // send the record info header
  snprintf(DataMsg,
           MSG_TX_BUFFER_SIZE,
           "Data=%s,%s,%s,%s,%s,%s,%s",
           SysLogRecordLabels[SL_RECORD].Label,
           SysLogRecordLabels[SL_DATE].Label,
           SysLogRecordLabels[SL_TIME].Label,
           Settings.LoadMonitor.Bay[0].Label,
           SysLogRecordLabels[SL_PIPE].Label,
           Settings.LoadMonitor.Bay[1].Label,
           SysLogRecordLabels[SL_PIPE].Label);

  MultiMsgAdd(DataMsg, "", "", 1);

  // terminate the multi-message transfer
  UI_SendMultiEnd("LogData");

  snprintf(str, sizeof(str), "LogToFile=EOR,%d", SessionID);
  SendMsgAndWaitForResponse(str, UI_DELAY_LONG);
} // end KeyForFile()

/***************************************************************************

FUNCTION:   KeyForUI()

PURPOSE:    Build the label string (key) to be used in the UI

COMMENTS:   It builds a string that looks like this:

            Key = "Plen Temp SP:deg,Plen Temp:deg,Cool Avl Temp:deg,Plen Hum SP:%..."

***************************************************************************/
void KeyForUI(int Bay, int Mode)
{
  // send the record info header
  if (Mode == GRAPH_MODE)
  {
    if (Bay == 2) // live mode
    {
      snprintf(KeyMsg,
               MSG_TX_BUFFER_SIZE,
               "Key=%s:deg,%s:deg,",
               Settings.LoadMonitor.Bay[0].Label,
               Settings.LoadMonitor.Bay[1].Label);
    }
    else  // history log
    {
      snprintf(KeyMsg,
               MSG_TX_BUFFER_SIZE,
               "Key=%s:deg,",
               Settings.LoadMonitor.Bay[Bay].Label);
    }
  }
  else
  {
    snprintf(KeyMsg,
             MSG_TX_BUFFER_SIZE,
             "Key=%s:deg,%s:,%s:deg,%s:,",
             Settings.LoadMonitor.Bay[0].Label,
             SysLogRecordLabels[SL_PIPE].Label,
             Settings.LoadMonitor.Bay[1].Label,
             SysLogRecordLabels[SL_PIPE].Label);
  }

  // terminate and send the message
  MultiMsgAdd(KeyMsg, "", "", 1);
} // end KeyForUI()

/***************************************************************************

FUNCTION:   SendAllItems()

PURPOSE:    Send the load log data to the LTX (then to USB storage device)

COMMENTS:   This sends a series of messages to the LTX that look like this:

            Data = "46.0,*,51.0,95,1,45,0,0,21.4,*,..."

***************************************************************************/
void SendAllItems(LOADLOG_RECORD *LoadLog)
{
  int  j;
  char str[50];
  char sensor1[10];
  char sensor2[10];

  j = 0;
  while (j < SDCARD_LOADLOG_RECSPERBLOCK)
  {
    if (LoadLog[j].RecordNum != 0)
    {
      if (LoadLog[j].Bay[0].Sensor == SENSOR_VAL_UNDEFINED)
      {
        strcpy(sensor1, "*");
      }
      else
      {
        snprintf(sensor1, sizeof(sensor1), "%.1f", LoadLog[j].Bay[0].Sensor/10.0);
      }

      if (LoadLog[j].Bay[1].Sensor == SENSOR_VAL_UNDEFINED)
      {
        strcpy(sensor2, "*");
      }
      else
      {
        snprintf(sensor2, sizeof(sensor2), "%.1f", LoadLog[j].Bay[1].Sensor/10.0);
      }

      // initiate the multi-message transfer
      UI_SendMultiHdr("LogData,Data", SessionID);

      // send the sensor data
      snprintf(DataMsg,
               MSG_TX_BUFFER_SIZE,
               "Data=%u,%s,%s,%s,%d,%s,%d,",
               LoadLog[j].RecordNum,
               LoadLog[j].Date,
               LoadLog[j].Time,
               sensor1,
               LoadLog[j].Bay[0].Pipe,
               sensor2,
               LoadLog[j].Bay[1].Pipe);

      // terminate and send the message
      MultiMsgAdd(DataMsg, "", "", 1);

      // terminate the multi-message transfer
      UI_SendMultiEnd("LogData");

      // send the end of record message
      snprintf(str, sizeof(str), "LogToFile=EOR,%d", SessionID);
      SendMsgAndWaitForResponse(str, UI_DELAY_LONG);
    }
    j++;
  }
} // end SendAllItems()

/***************************************************************************

FUNCTION:   SendQueryItems()

PURPOSE:    Send the load log data to the LTX (then to USB storage device)

COMMENTS:   This sends a series of messages to the LTX that look like this:

            Data = "46.0,*,51.0,95,1,45,0,0,21.4,*,..."

***************************************************************************/
void SendQueryItems(LOADLOG_RECORD *LoadLog, int Bay, char *RecordLabel, int *PointsPerLabel, int Mode)
{
  int  i,j;
  char str[50];
  char tmp[50];

  j = 0;
  while (j < SDCARD_LOADLOG_RECSPERBLOCK)
  {
    if (LoadLog[j].RecordNum != 0)
    {
      // store the first label
      if (strcmp(RecordLabel, "") == 0)
      {
        if (Mode == TABLE_MODE)
        {
          strcpy(RecordLabel, LoadLog[j].Date);
        }
        else  // live graph or standard history graph
        {
          // if Bay == 2 then it is live mode and the labels are hours
          // otherwise the labels are the pipe numbers
          if (Bay == 2)
          {
            if (Mode == GRAPH_MODE)
            {
              // take the hour from the time
              strncpy(str, LoadLog[j].Time, 2);
              str[2] = 0;
              sprintf(RecordLabel, "%s:00", str);
            }
            else
            {
              strcpy(RecordLabel, LoadLog[j].Time);
            }
          }
          else
          {
            sprintf(RecordLabel, "%d", LoadLog[j].Bay[0].Pipe);
          }
        }
      }

      // check for start of new label
      if (Mode == TABLE_MODE)
      {
        if (strcmp(RecordLabel, LoadLog[j].Date) != 0)
        {
          // add the date and data points per date to the message
          snprintf(str, sizeof(str), "%s?%d,", RecordLabel, *PointsPerLabel);
          MultiMsgAdd(DatesMsg, "Dates=", str, 0);
          strcpy(RecordLabel, LoadLog[j].Date);
          *PointsPerLabel = 0;
        }
      }
      else
      {
        if (Bay == 2)
        {
          if (strncmp(RecordLabel, LoadLog[j].Time, 2) != 0)
          {
            // add the date and data points per date to the message
            snprintf(str, sizeof(str), "%s?%d,", RecordLabel, *PointsPerLabel);
            MultiMsgAdd(DatesMsg, "Dates=", str, 0);
            *PointsPerLabel = 0;

            if (Mode == GRAPH_MODE)
            {
              // take the hour from the time
              strncpy(str, LoadLog[j].Time, 2);
              str[2] = 0;
              sprintf(RecordLabel, "%s:00", str);
            }
            else
            {
              strcpy(RecordLabel, LoadLog[j].Time);
            }
          }
        }
        else
        {
          snprintf(str, sizeof(str), "%d", LoadLog[j].Bay[0].Pipe);
          if (strcmp(str, RecordLabel) != 0)
          {
            // add the date and data points per date to the message
            snprintf(str, sizeof(str), "%s?%d,", RecordLabel, *PointsPerLabel);
            MultiMsgAdd(DatesMsg, "Dates=", str, 0);
            sprintf(RecordLabel, "%d", LoadLog[j].Bay[0].Pipe);
            *PointsPerLabel = 0;
          }
        }
      }

      // send the sensor data
      for (i = 0; i < NUM_LOADLOG_SENSORS; i++)
      {
        if (   i == Bay
            || Bay == 2)  // both bays
        {
          if (LoadLog[j].Bay[i].Sensor == SENSOR_VAL_UNDEFINED)
          {
            strcpy(str, "*,");
          }
          else
          {
            snprintf(str, sizeof(str), "%.1f,", LoadLog[j].Bay[i].Sensor/10.0);
          }

          if (Mode == TABLE_MODE)
          {
            snprintf(tmp, sizeof(tmp), "%d,", LoadLog[j].Bay[i].Pipe);
            strncat(str, tmp, sizeof(str)-strlen(str)-1);
          }

          MultiMsgAdd(DataMsg, "Data=", str, 0);
        }
      }

      // add timestamp
      snprintf(str, sizeof(str), "%s,", LoadLog[j].Time);
      MultiMsgAdd(TimesMsg, "TimeStamps=", str, 0);

      (*PointsPerLabel)++;
    }
    j++;
  }
} // end SendQueryItems()

/***************************************************************************

FUNCTION:   LoadLogIndexReset()

PURPOSE:    Reset the memory resident array index

COMMENTS:

***************************************************************************/
void LoadLogIndexReset(void)
{
   LoadLogIndex = 0;
} // end LoadLogIndexReset()

/***************************************************************************

FUNCTION:   LoadLogReset()

PURPOSE:    Reset/initialize the user log and SD card header

COMMENTS:   Callers that don't want the header written must obvioulsy write
            the header themselves and therefore should lock the sd card
            semaphore.  Callers wanting the header written should not lock
            the sd card semaphore before this call.

***************************************************************************/
void LoadLogReset(SDCARD_HEADER *Header, int WriteHeader)
{
  int NumBlocks;

  WarningsSet(WARN_LOADLOG_CLEAR, FM_ALARM, FM_ALARM, NA);

  Header->LoadLog.RecordLen = sizeof(LOADLOG_RECORD);
  Header->LoadLog.RecordsPerBlock = SDCARD_BLOCK_SIZE/Header->LoadLog.RecordLen;
  Header->LoadLog.RecordsPerCard = SDCARD_LOADLOG_RECSPERBLOCK * SDCARD_LOADLOG_MAXBLOCKS;
  Header->LoadLog.NumRecords = 0;

  NumBlocks = ActiveSDCard.Size;
  NumBlocks -= SDCARD_PIDLOG_MAXBLOCKS;
  NumBlocks -= Header->SysLog.MaxBlocks;
  NumBlocks -= SDCARD_LOADLOG_MAXBLOCKS;

  Header->LoadLog.StartBlock = NumBlocks;
  Header->LoadLog.WriteBlock = Header->LoadLog.StartBlock;

  // initialize the LoadLog record
  memset(LoadLog, 0, sizeof(LoadLog));
  LoadLogIndex = 0;

  Settings.LoadMonitor.Bay[0].Pipe = 1;
  Settings.LoadMonitor.Bay[1].Pipe = 1;
  Settings.LoadMonitor.Bay[0].Status = LL_PAUSED;
  Settings.LoadMonitor.Bay[1].Status = LL_PAUSED;

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
        debug_printf("LoadLogReset blocked at SDCardHeaderUpdate\r\n");
      }
    }
    else
    {
      ActiveSDCard.Initialized = 0;
    }
  }
} // end LoadLogReset()

/***************************************************************************

FUNCTION:   LoadLogSendToUI()

PURPOSE:    Send the requested user log records to the LTX/UI

COMMENTS:   This is going to send a series of messages to the LTX that look
            like this:

            Data_Loaded = "false";
            Key = "Plen Temp SP:deg,Plen Temp:deg,Cool Avl Temp:deg,..."
            Dates = "09/28/2007:20,"
            TimeStamps = "09:25:45#12,09:24:45#12,09:23:44#0,09:22:44#0,..."
            Data = "46.0,*,51.0,95,1,45,0,0,21.4,*,...,1,0,0,1,0,0,1,0,..."
            Data_Loaded = "true";

            This will make a first pass through the requested data to
            determine the appropriate sensor labels needed for the key - for
            an explanation of this process see SystemLogBuildSensorKey().
            Then it sets up each of the messages and processes the data
            sending multi-messages as necessary.  Then it will terminate
            and send any unfull/unsent messages and send the transfer
            complete message.

            This function also keeps track of the information for the following
            message:

            Dates = "09/28/2007:20,";

            It tracks how many data points were taken for each date (PointsPerDate)

***************************************************************************/
int LoadLogSendToUI(int Bay, int Mode, int Session)
{
  int  BeginBlock = 0;
  int  EndBlock = 0;
  int  GetBlock = 0;
  int  TotalBlocks;
  int  PointsPerLabel = 0;
  char RecordLabel[TIME_LEN+1] = "";
  char str[20];
  int  Status = 1;

  // set the modular SessionID variable and clear the kill flag
  SessionID = Session;
  KillMessage = -1;

  // suspend slave mode during log transmission
  SendingLogData = 1;
  UI_SendBasicSetup(MSG_SEND);

  // initiate the multi-message transfer
  if (Mode == FILE_MODE)
  {
    snprintf(str, sizeof(str), "LogTotal=%d,%d", SDCardHeader.LoadLog.NumRecords, SessionID);
    SendMsgAndWaitForResponse(str, UI_DELAY_LONG);

    KeyForFile();
  }
  else
  {
    // when sending to the UI the messages sent don't correspond to the number of
    // history records so send a zero so the UI can change the display
    snprintf(str, sizeof(str), "LogTotal=%d,%d", 0, SessionID);
    SendMsgAndWaitForResponse(str, UI_DELAY_LONG);

    UI_SendMultiHdr("LogData,Key,Dates,TimeStamps,Data", SessionID);

    // set up the messages
    strcpy(DatesMsg, "Dates=");
    strcpy(TimesMsg, "TimeStamps=");
    strcpy(DataMsg, "Data=");
    KeyForUI(Bay, Mode);
  }

  if (SDCardHeader.LoadLog.NumRecords > 0)
  {
    // for the live graph, only get the last 24hrs of data for performance (18 blocks)
    if (Mode == GRAPH_MODE && Bay == 2)
    {
      EndBlock = SDCardHeader.LoadLog.WriteBlock - 1;

      if (EndBlock - (int)SDCardHeader.LoadLog.StartBlock >= 17)
      {
        BeginBlock = EndBlock - 17;
      }
      else
      {
        BeginBlock = (int)SDCardHeader.LoadLog.StartBlock;
      }
    }
    else
    {
      BeginBlock = SDCardHeader.LoadLog.StartBlock;
      EndBlock = SDCardHeader.LoadLog.WriteBlock - 1;
    }

    TotalBlocks = (EndBlock - BeginBlock + 1);
    GetBlock = BeginBlock;

    if (ActiveSDCard.Semaphore != NULL)
    {
      // Obtain the semaphore - block if the semaphore is not immediately available.
      if (xSemaphoreTake(ActiveSDCard.Semaphore, ActiveSDCard.BlockTimeout) == pdTRUE)
      {
        while (TotalBlocks--)
        {
          Status = SDCardRead((char *)&LoadLogBuf, sizeof(LoadLogBuf), GetBlock);
          if (Status == 0)
          {
            break;
          }

          // get the data points
          if (Mode == FILE_MODE)
          {
            SendAllItems(LoadLogBuf);
          }
          else
          {
            SendQueryItems(LoadLogBuf, Bay, RecordLabel, &PointsPerLabel, Mode);
          }

          GetBlock++;

          // reset the watchdog external timer
          ThreadMonitorUpdate(TM_UI_UPDATE);

          // abort the send if the LTX sent a KILL
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
        debug_printf("LoadLogSendToUI blocked at SDCardRead\r\n");
      }
    }
    else
    {
      ActiveSDCard.Initialized = 0;
    }
  }

  if (KillMessage != SessionID)
  {
    // send the items in the memory resident buffer
    if (Mode == FILE_MODE)
    {
      SendAllItems(LoadLog);
    }
    else
    {
      SendQueryItems(LoadLog, Bay, RecordLabel, &PointsPerLabel, Mode);
    }

    // terminate and send remaining messages
    snprintf(str, sizeof(str), "%s?%d,", RecordLabel, PointsPerLabel);

    if (Mode != FILE_MODE)
    {
      MultiMsgAdd(DatesMsg, "Dates=", str, 1);
      MultiMsgAdd(TimesMsg, "", "", 1);
      MultiMsgAdd(DataMsg, "", "", 1);
    }
  }

  if (Mode == FILE_MODE)
  {
    // send the end of file
    snprintf(str, sizeof(str), "LogToFile=EOF,%d", SessionID);
    SendMsgAndWaitForResponse(str, UI_DELAY_LONG);
  }
  else
  {
    // terminate the multi-message transfer
    UI_SendMultiEnd("LogData");
  }

  // re-enable slave mode (if in slave mode)
  SendingLogData = 0;
  UI_SendBasicSetup(MSG_SEND);

  if (Status == 0)
  {
    WarningsSet(WARN_DATALOGREAD, FM_ALARM, FM_ALARM, NA);
    ActiveSDCard.Initialized = 0;
  }

  return 1;
} // end LoadLogSendToUI()

/***************************************************************************

FUNCTION:   LoadLogTempAccumulator()

PURPOSE:    Accumulate the IR temperature sensor values to calculate the average

COMMENTS:   Technically 0 is a valid sensor ID, but not for IR temperature
            sensors because they can only be sensor 3 on humidity boards -
            the configurable sensor.

***************************************************************************/
void LoadLogTempAccumulator(int SensorID, float Temp)
{
  // store available IR temperature sensors
  if (Settings.LoadMonitor.IrSensorsAvailable[0] == 0)
  {
    Settings.LoadMonitor.IrSensorsAvailable[0]= SensorID;

    if (Settings.LoadMonitor.Bay[0].SensorID == 0)
    {
      Settings.LoadMonitor.Bay[0].SensorID = SensorID;
    }
  }
  else if (   Settings.LoadMonitor.IrSensorsAvailable[1] == 0
           ||(   SensorID != Settings.LoadMonitor.IrSensorsAvailable[1]
              && Settings.LoadMonitor.IrSensorsAvailable[1] == Settings.LoadMonitor.IrSensorsAvailable[0]))
  {
    Settings.LoadMonitor.IrSensorsAvailable[1]= SensorID;

    if (Settings.LoadMonitor.Bay[1].SensorID == 0)
    {
      Settings.LoadMonitor.Bay[1].SensorID = SensorID;
    }
  }

  // accumulate the temperature values
  if (SensorID == Settings.LoadMonitor.Bay[0].SensorID)
  {
    LoadingTemps[0].Sum += Temp;
    LoadingTemps[0].Count++;
  }

  // if there is only one IR sensor the value will get stored for both bays
  if (SensorID == Settings.LoadMonitor.Bay[1].SensorID)
  {
    LoadingTemps[1].Sum += Temp;
    LoadingTemps[1].Count++;
  }
} // end LoadLogTempAccumulator()

/***************************************************************************

FUNCTION:   LoadLogIndexReset()

PURPOSE:    Reset the temperature accumulator for the bay

COMMENTS:

***************************************************************************/
void LoadLogTempAccumulatorReset(int Bay)
{
  LoadingTemps[Bay].Count = 0;
  LoadingTemps[Bay].Sum = 0.0;
} // end LoadLogIndexReset()

/***************************************************************************

FUNCTION:   LoadLogUnwrittenRecords()

PURPOSE:    Return how many records are in the array

COMMENTS:

***************************************************************************/
unsigned int LoadLogUnwrittenRecords(void)
{
   return LoadLogIndex;
} // end LoadLogUnwrittenRecords()

/***************************************************************************

FUNCTION:   LoadLogWrite()

PURPOSE:    Build a user log record and write it to the SD card

COMMENTS:

***************************************************************************/
int LoadLogWrite(char Force)
{
  int  i;
  char Hour = 0;
  char Min = 0;
  char Sec = 0;
  int  LastLoadBlock = 0;
  int  Status = 1;
  int  WriteSuccess = 0;
  int  WriteRetries = 0;
  int  ReadRetries = 0;
  LOADLOG_RECORD CheckRec[SDCARD_LOADLOG_RECSPERBLOCK];

  if (   ActiveSDCard.Initialized == 0
      || ActiveSDCard.Semaphore == NULL)
  {
    ActiveSDCard.Initialized = 0;
    WarningsSet(WARN_SDCARD_NONE, FM_ALARM, FM_ALARM, NA);
    return 1;
  }

  if (   Settings.LoadMonitor.Bay[0].Status != LL_ACQUIRING
      && Settings.LoadMonitor.Bay[1].Status != LL_ACQUIRING)
  {
    return 1;
  }

  if (   GetDateStr(LoadLog[LoadLogIndex].Date) == 0
      || GetTime(&Hour, &Min, &Sec) == 0)
  {
    return 1;
  }

  // store the record information
  LoadLog[LoadLogIndex].RecordNum = SDCardHeader.LoadLog.NumRecords + LoadLogIndex + 1;
  snprintf(LoadLog[LoadLogIndex].Time, TIME_LEN+1, "%02d:%02d", Hour, Min);

  // store the sensor values
  for (i = 0; i < NUM_LOADLOG_SENSORS; i++)
  {
    LoadLog[LoadLogIndex].Bay[i].Pipe = Settings.LoadMonitor.Bay[i].Pipe;

    if (Settings.LoadMonitor.Bay[i].Status == LL_ACQUIRING && LoadingTemps[i].Count > 0)
    {
      LoadLog[LoadLogIndex].Bay[i].Sensor = ((LoadingTemps[i].Sum/LoadingTemps[i].Count)+ .05) * 10;

      if (   LoadLog[LoadLogIndex].Bay[i].Sensor >= (Settings.LoadMonitor.AlarmTemp[0] * 10)
          && LoadLog[LoadLogIndex].RecordNum > 1)
      {
        WarningsSet((WARNING_ITEMS) (WARN_LOADMON_BAY1 + i), FM_ALARM, Settings.LoadMonitor.Bay[i].Pipe, NA);
      }
      else if (   LoadLog[LoadLogIndex].Bay[i].Sensor <= (Settings.LoadMonitor.AlarmTemp[1] * 10)
               && LoadLog[LoadLogIndex].RecordNum > 1)
      {
        WarningsSet((WARNING_ITEMS) (WARN_LOADMON_BAY1 + i), FM_FAIL, Settings.LoadMonitor.Bay[i].Pipe, NA);
      }
    }
    else
    {
      LoadLog[LoadLogIndex].Bay[i].Sensor = SENSOR_VAL_UNDEFINED;
    }

    LoadingTemps[i].Count = 0;
    LoadingTemps[i].Sum = 0.0;
  }

  LoadLogIndex++;

  if (   LoadLogIndex == SDCARD_LOADLOG_RECSPERBLOCK
      || Force)
  {
    // Obtain the semaphore - block if the semaphore is not immediately available.
    if (xSemaphoreTake(ActiveSDCard.Semaphore, ActiveSDCard.BlockTimeout) == pdTRUE)
    {
      while (!WriteSuccess && WriteRetries < 3)
      {
        // write the record to the SD card
        Status = SDCardSave((unsigned char *)LoadLog, sizeof(LoadLog), SDCardHeader.LoadLog.WriteBlock);
        if (Status == 0)
        {
          // check the accuracy of the write
          ReadRetries = 0;
          while (   SDCardRead((char *)CheckRec, sizeof(CheckRec), SDCardHeader.LoadLog.WriteBlock) != 1
                 && ReadRetries++ < 3)
          {
            debug_printf("SD card/Load Log read retry\r\n");
          }

          if (memcmp(CheckRec, LoadLog, sizeof(LoadLog)) != 0)
          {
            debug_printf("SD card/Load Log write retry\r\n");
            WriteRetries++;
          }
          else
          {
            // initialize the LoadLog record
            memset(LoadLog, 0, sizeof(LoadLog));

            // write was successful, update header
            WriteSuccess = 1;
            SDCardHeader.LoadLog.WriteBlock++;
            SDCardHeader.LoadLog.NumRecords += LoadLogIndex;

            // stop logging when full
            LastLoadBlock = (SDCardHeader.LoadLog.StartBlock + SDCARD_LOADLOG_MAXBLOCKS) - 1;
            if (SDCardHeader.LoadLog.WriteBlock > LastLoadBlock)
            {
              Settings.LoadMonitor.Bay[0].Status = LL_FULL;
              Settings.LoadMonitor.Bay[1].Status = LL_FULL;
              WarningsSet(WARN_LOADLOG_FULL, FM_ALARM, FM_ALARM, NA);
              UI_SendLoadMonitor(MSG_SEND);
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
        //    SDCardHeader.LoadLog.Errors++;
        SDCardHeader.Errors.Write++;
        ActiveSDCard.Initialized = 0;

        if ((SDCardHeader.Errors.Write % 20) == 0)
          WarningsSet(WARN_DATALOGWRITE, FM_ALARM, FM_ALARM, NA);
      }

      SDCardHeaderUpdate();

      // Release the semaphore.
      xSemaphoreGive(ActiveSDCard.Semaphore);
    }
    else
    {
      debug_printf("LoadLogWrite blocked for SDCardSave\r\n");
    }

    if (WriteSuccess)
    {
      // start new block of PID values
      LoadLogIndex = 0;
    }
    else
    {
      // failed to write block so just overwrite the last item in the block
      // and hope to write the block successfully next time and only lose 1
      // value instead of entire block
      LoadLogIndex--;
    }
  }

  return(Status);
} // end LoadLogWrite()

/***   End Of File   ***/
