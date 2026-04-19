/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     PIDLogs.c

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:  Read and write the PID logs

COMMENTS:

***************************************************************************/

/*** include files ***/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"

#include "Controls.h"
#include "PIDLogs.h"
#include "RS485.h"
#include "RTC.h"
#include "SDCard.h"
#include "States.h"
#include "SystemLogs.h"
#include "ThreadMonitor.h"
#include "UI_Messages.h"
#include "UserLogs.h"

/*** typedefs and structures ***/

/*** module variables ***/

PIDLOG_LABELS PIDLogLabels[NUM_PIDLOG_ITEMS];

static PIDLOG_RECORD PIDLog[SDCARD_PIDLOG_RECSPERBLOCK];
static PIDLOG_RECORD PIDLogBuf[SDCARD_PIDLOG_RECSPERBLOCK];

static char PIDLogDate[DATE_LEN+1] = "";
static char PIDLogIndex = 0;
static int  PointsPerDate = 0;
static char RequestType = 0;
static int  SessionID;

static int  BeginRecord = 0;
static int  EndRecord = 0;
static int  MaxRecords = 0;
static int  SelectedRecords = 0;
static char RecordType = 0;

/*** static functions ***/

static void BuildMessages(int StartIndex);
static void KeyForFile(void);
static void KeyForUI(void);
static void PIDLogRead(void);
static void SendToFile(int PIDIndex);
static void SendToUI(int PIDIndex);

/***************************************************************************

FUNCTION:   BuildMessages()

PURPOSE:    Determines which PID records to send to the LTX

COMMENTS:

***************************************************************************/
void BuildMessages(int StartIndex)
{
  int j = StartIndex;

  while (   j < SDCARD_PIDLOG_RECSPERBLOCK
         && SelectedRecords > 0
         && MaxRecords > 0)
  {
    if (PIDLogBuf[j].Type == RecordType)
    {
      if (RequestType == REQTYPE_TOFILE)
        SendToFile(j);
      else
        SendToUI(j);

      SelectedRecords--;
    }
    j++;
    MaxRecords--;
  }
} // end BuildMessages()

/***************************************************************************

FUNCTION:   KeyForFile()

PURPOSE:    Send the key (column labels) to the LTX (then to USB storage device)

COMMENTS:   It builds a string that looks like this:

            Data = "P,I,D,Output,Type"

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
           "Data=%s,%s,%s,%s,%s,%s,%s,%s,%s,",
           SysLogRecordLabels[SL_RECORD].Label,
           SysLogRecordLabels[SL_DATE].Label,
           SysLogRecordLabels[SL_TIME].Label,
           PIDLogLabels[0].Label,
           PIDLogLabels[1].Label,
           PIDLogLabels[2].Label,
           PIDLogLabels[3].Label,
           PIDLogLabels[4].Label,
           PIDLogLabels[5].Label);

  MultiMsgAdd(DataMsg, "", "", 1);

  // terminate the multi-message transfer
  UI_SendMultiEnd("LogData");

  snprintf(str, sizeof(str), "LogToFile=EOR,%d", SessionID);
  SendMsgAndWaitForResponse(str, UI_DELAY_LONG);
} // end KeyForFile()

/***************************************************************************

FUNCTION:   KeyForUI()

PURPOSE:    Build the label string (key) to be used in the UI

COMMENTS:

***************************************************************************/
void KeyForUI(void)
{
  // send the record info header
  snprintf(KeyMsg,
           MSG_TX_BUFFER_SIZE,
           "Key=%s:,%s:,%s:,%s:,%s:,",
           PIDLogLabels[0].Label,
           PIDLogLabels[1].Label,
           PIDLogLabels[2].Label,
           PIDLogLabels[3].Label,
           PIDLogLabels[4].Label);

  // terminate and send the message
  MultiMsgAdd(KeyMsg, "", "", 1);
} // end KeyForUI()

/***************************************************************************

FUNCTION:   PIDLogReset()

PURPOSE:    Reset/initialize the PID log

COMMENTS:   Callers that don't want the header written must obvioulsy write
            the header themselves and therefore should lock the sd card
            semaphore.  Callers wanting the header written should not lock
            the sd card semaphore before this call.

***************************************************************************/
void PIDLogReset(PIDLOG_HEADER *Header, int WriteHeader)
{
  int NumBlocks = ActiveSDCard.Size;

  Header->StartBlock = NumBlocks - SDCARD_PIDLOG_MAXBLOCKS;
  Header->WriteBlock = Header->StartBlock;
  Header->NumRecords = 0;

  // initialize the PIDLog record
  memset(PIDLog, 0, sizeof(PIDLog));
  PIDLogIndex = 0;

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
        debug_printf("PIDLogReset blocked at SDCardHeaderUpdate\r\n");
      }
    }
    else
    {
      ActiveSDCard.Initialized = 0;
    }
  }

//  DBGU_SendString("PID Log initialized");
} // end PIDLogReset()

/***************************************************************************

FUNCTION:   PIDLogSendToFile()

PURPOSE:    Send the requested PID log records to USB storage device

COMMENTS:   This sends the records to the LTX which sends them to the Linux
            display board which creates a file on a USB storage device.

            This is going to send a series of messages to the LTX that look
            like this:

            Data = "12/15/07,10:15,1.65,8.2,1.6,...
            DataLoadStatus = "true";

***************************************************************************/
int PIDLogSendToFile(int BeginRec, int EndRec, char Type, int Session)
{
  int  TotalRecords;
  char str[50];

  // set the modular SessionID variable and clear the kill flag
  SessionID = Session;
  KillMessage = -1;

  if (SDCardHeader.PIDLog.NumRecords == 0)
  {
    // close the file opened on the USB device
    snprintf(str, sizeof(str), "LogToFile=EOF,%d", SessionID);
    SendMsgAndWaitForResponse(str, UI_DELAY_LONG);
    return 0;
  }

  // suspend slave mode during log transmission
  SendingLogData = 1;
  UI_SendBasicSetup(MSG_SEND);

  // set module variables
  BeginRecord = BeginRec;
  EndRecord = EndRec;
  RecordType = Type;
  RequestType = REQTYPE_TOFILE;

  TotalRecords = (EndRecord - BeginRecord) + 1;
  snprintf(str, sizeof(str), "LogTotal=%d,%d", TotalRecords, SessionID);
  SendMsgAndWaitForResponse(str, UI_DELAY_LONG);

  // send the key/header
  KeyForFile();

  // send the data
  PIDLogRead();

  // send the end of file
  snprintf(str, sizeof(str), "LogToFile=EOF,%d", SessionID);
  SendMsgAndWaitForResponse(str, UI_DELAY_LONG);

  // re-enable slave mode (if in slave mode)
  SendingLogData = 0;
  UI_SendBasicSetup(MSG_SEND);

  return 1;
} // end PIDLogSendToFile()

/***************************************************************************

FUNCTION:   PIDLogSendToUI()

PURPOSE:    Send the requested system log records to the LTX/UI

COMMENTS:   This is going to send a series of messages to the LTX that look
            like this:

            Key = "Plen Temp SP:deg,Plen Temp:deg,Cool Avl Temp:deg,..."
            Dates = "09/28/2007:20,"
            TimeStamps = "09:25:45#12,09:24:45#12,09:23:44#0,09:22:44#0,..."
            Data = "1.64,8.2,-310.7,-1.2,100,..."
            DataLoadStatus = "true";

***************************************************************************/
int PIDLogSendToUI(int BeginRec, int EndRec, char Type, int Session)
{
  char str[20];

  // set the modular SessionID variable and clear the kill flag
  SessionID = Session;
  KillMessage = -1;

  if (SDCardHeader.PIDLog.NumRecords == 0)
  {
    // clear the variables in the LTX
    UI_SendMultiHdr("LogData,Key,Dates,TimeStamps,Data", SessionID);
    UI_SendMultiEnd("LogData");
    return 0;
  }

  // suspend slave mode during log transmission
  SendingLogData = 1;
  UI_SendBasicSetup(MSG_SEND);

  // set module variables
  BeginRecord = BeginRec;
  EndRecord = EndRec;
  RecordType = Type;
  RequestType = REQTYPE_UI;
  PointsPerDate = 0;

  // when sending to the UI the messages sent don't correspond to the number of
  // history records so send a zero so the UI can change the display
  snprintf(str, sizeof(str), "LogTotal=%d,%d", 0, SessionID);
  SendMsgAndWaitForResponse(str, UI_DELAY_LONG);

  // initiate the multi-message transfer
  UI_SendMultiHdr("LogData,Key,Dates,TimeStamps,Data", SessionID);

  // set up the messages
  strcpy(DatesMsg, "Dates=");
  strcpy(TimesMsg, "TimeStamps=");
  strcpy(DataMsg, "Data=");

  // build the key (labels)
  KeyForUI();

  // send the data
  PIDLogRead();

  if (KillMessage != SessionID)
  {
    // terminate and send remaining messages
    snprintf(str, sizeof(str), "%s?%d,", PIDLogDate, PointsPerDate);
    MultiMsgAdd(DatesMsg, "Dates=", str, 1);
    MultiMsgAdd(TimesMsg, "", "", 1);
    MultiMsgAdd(DataMsg, "", "", 1);
  }

  // terminate the multi-message transfer
  UI_SendMultiEnd("LogData");

  // re-enable slave mode (if in slave mode)
  SendingLogData = 0;
  UI_SendBasicSetup(MSG_SEND);

  return 1;
} // end PIDLogSendToUI()

/***************************************************************************

FUNCTION:   PIDLogRead()

PURPOSE:    Reads and processes the requested blocks from the SD card

COMMENTS:   The PID log is circular

            Because the door & refrig PID log records can be intermixed within
            each PID log block, it's impossible to calculate which block the
            desired record will be in when looking for n records.  Consequently,
            each block and record must be looked at and counted when looking for
            the correct number and type of records requested by the UI.

            Also, when the UI requests n records, those are the most recent n
            records which will be at the end of the file.  So it starts looking
            at the end (or an n record offset from the end, i.e. the UI requests
            records 500-1200) to find the desired number of records.  But because
            the data can be graphed, the records must be sent in chronological
            order.  So when the data is actually sent to the LTX/UI, it starts
            with the oldest record requested and send the rest going forward.

            This is different from the system/activity log which actually sends
            the data in reverse order.

***************************************************************************/
void PIDLogRead(void)
{
  int j;
  int EndFound = 0;
  int LastPIDBlock = (SDCardHeader.PIDLog.StartBlock + SDCARD_PIDLOG_MAXBLOCKS) - 1;
  int ReadBlock;
  int RecordPosition;
  int SelectStartBlock;
  int SelectStartIndex;
  int Status = 0;

  // check if any records available
  if (SDCardHeader.PIDLog.NumRecords == 0)
  {
    return;
  }

  SelectedRecords = EndRecord - BeginRecord + 1;
  MaxRecords = SDCARD_PIDLOG_MAXBLOCKS * SDCARD_PIDLOG_RECSPERBLOCK;

  // don't look for more records than are available
  if (SelectedRecords > MaxRecords)
  {
    SelectedRecords = MaxRecords;
  }
  if (SelectedRecords > SDCardHeader.PIDLog.NumRecords)
  {
    SelectedRecords = SDCardHeader.PIDLog.NumRecords;
    MaxRecords = SDCardHeader.PIDLog.NumRecords;
  }

  // pre-process to find the correct number of records of the correct type
  // starting at the end of the file going backwards (see comment above)
  RecordPosition = BeginRecord;
  if (SDCardHeader.PIDLog.WriteBlock == SDCardHeader.PIDLog.StartBlock)
  {
    ReadBlock = LastPIDBlock;
  }
  else
  {
    ReadBlock = SDCardHeader.PIDLog.WriteBlock - 1;
  }

  if (ActiveSDCard.Semaphore != NULL)
  {
    // Obtain the semaphore - block if the semaphore is not immediately available.
    if (xSemaphoreTake(ActiveSDCard.Semaphore, ActiveSDCard.BlockTimeout) == pdTRUE)
    {
      while (   RecordPosition > 0
             && MaxRecords > 0)
      {
        Status = SDCardRead((char *)PIDLogBuf, sizeof(PIDLogBuf), ReadBlock);
        if (Status == 0)
        {
          break;
        }

        j = SDCARD_PIDLOG_RECSPERBLOCK - 1;
        while (   j >= 0
               && RecordPosition > 0
               && MaxRecords > 0)
        {
          if (PIDLogBuf[j].Type == RecordType)
          {
            RecordPosition--;
          }

          if (RecordPosition == 0 && EndFound == 0)   // offset found
          {
            RecordPosition = SelectedRecords - 1;     // start looking for n records
            EndFound = 1;
          }

          MaxRecords--;
          j--;
        }

        if (   RecordPosition != 0
            && MaxRecords != 0)
        {
          // decrement with wrap around
          ReadBlock--;
          if (ReadBlock < SDCardHeader.PIDLog.StartBlock)
          {
            ReadBlock = LastPIDBlock;
          }
        }

        // reset the watchdog external timer
        ThreadMonitorUpdate(TM_UI_UPDATE);

        // abort the read if the LTX sent a KILL
        if (KillMessage == SessionID)
        {
          // Release the semaphore.
          xSemaphoreGive(ActiveSDCard.Semaphore);
          return;
        }
      }

      if (EndFound == 0)
      {
        // Release the semaphore.
        xSemaphoreGive(ActiveSDCard.Semaphore);
        return;
      }

      SelectStartBlock = ReadBlock;
      SelectStartIndex = j + 1;
      SelectedRecords -= RecordPosition;
      MaxRecords = SDCARD_PIDLOG_MAXBLOCKS * SDCARD_PIDLOG_RECSPERBLOCK;
      ReadBlock = SelectStartBlock;
      j = 0;

      // send the records to the LTX/UI starting with the oldest moving forward
      while (   SelectedRecords > 0
             && MaxRecords > 0)
      {
        Status = SDCardRead((char *)PIDLogBuf, sizeof(PIDLogBuf), ReadBlock);
        if (Status == 0)
        {
          break;
        }

        if (j == 0)
        {
          BuildMessages(SelectStartIndex);
        }
        else
        {
          BuildMessages(0);
        }

        // increment read block with wrap-around
        ReadBlock++;
        if (ReadBlock > LastPIDBlock)
        {
          ReadBlock = SDCardHeader.PIDLog.StartBlock;
        }

        // reset the watchdog external timer
        ThreadMonitorUpdate(TM_UI_UPDATE);

        // abort the read if the LTX sent a KILL
        if (KillMessage == SessionID)
        {
          break;
        }

        j++;
      }

      // Release the semaphore.
      xSemaphoreGive(ActiveSDCard.Semaphore);
    }
    else
    {
      debug_printf("PIDLogRead blocked\r\n");
    }
  }
  else
  {
    ActiveSDCard.Initialized = 0;
  }
} // end PIDLogRead()

/***************************************************************************

FUNCTION:   PIDLogWrite()

PURPOSE:    Build a PID log record and write it to the SD card

COMMENTS:

***************************************************************************/
int PIDLogWrite(char Type, float P, float I, float D, int Output, float Error)
{
  char Hour = 0;
  char Min = 0;
  char Sec = 0;
  int  LastPIDBlock = 0;
  int  Status = 1;
  int  WriteSuccess = 0;
  int  WriteRetries = 0;
  int  ReadRetries = 0;
  PIDLOG_RECORD CheckRec[SDCARD_PIDLOG_RECSPERBLOCK];

  if (   ActiveSDCard.Initialized == 0
      || ActiveSDCard.Semaphore == NULL)
  {
    ActiveSDCard.Initialized = 0;
    WarningsSet(WARN_SDCARD_NONE, FM_ALARM, FM_ALARM, NA);
    return 1;
  }

  if (   GetDateStr(PIDLog[PIDLogIndex].Date) == 0
      || GetTime(&Hour, &Min, &Sec) == 0)
  {
    return 1;
  }

  // store the record information
  PIDLog[PIDLogIndex].RecordNum = SDCardHeader.PIDLog.NumRecords + PIDLogIndex + 1;
  snprintf(PIDLog[PIDLogIndex].Time, TIME_LEN+1, "%02d:%02d:%02d", Hour, Min, Sec);

  // store the PID values (convert from float to short)
  PIDLog[PIDLogIndex].P = (P + .05) * 10;
  PIDLog[PIDLogIndex].I = (I + .05) * 10;
  PIDLog[PIDLogIndex].D = (D + .05) * 10;
  PIDLog[PIDLogIndex].Error = (Error + .005) * 100;
  PIDLog[PIDLogIndex].Output = Output;
  PIDLog[PIDLogIndex].Type = Type;

  PIDLogIndex++;

  if (PIDLogIndex == SDCARD_PIDLOG_RECSPERBLOCK)
  {
    // Obtain the semaphore - block if the semaphore is not immediately available.
    if (xSemaphoreTake(ActiveSDCard.Semaphore, ActiveSDCard.BlockTimeout) == pdTRUE)
    {
      while (!WriteSuccess && WriteRetries < 3)
      {
        // write the record to the SD card
        Status = SDCardSave((unsigned char *)PIDLog, sizeof(PIDLog), SDCardHeader.PIDLog.WriteBlock);
        if (Status == 0)
        {
          // check the accuracy of the write
          ReadRetries = 0;
          while (   SDCardRead((char *)CheckRec, sizeof(CheckRec), SDCardHeader.PIDLog.WriteBlock) != 1
                 && ReadRetries++ < 3)
          {
            debug_printf("SD card/PID Log read retry\r\n");
          }

          if (memcmp(CheckRec, PIDLog, sizeof(PIDLog)) != 0)
          {
            debug_printf("SD card/PID Log write retry\r\n");
            WriteRetries++;
          }
          else
          {
            // initialize the PIDLog record
            memset(PIDLog, 0, sizeof(PIDLog));

            // write was successful, update header
            WriteSuccess = 1;
            SDCardHeader.PIDLog.WriteBlock++;
            SDCardHeader.PIDLog.NumRecords += PIDLogIndex;

            // handle wrapping the data around on the card
            LastPIDBlock = (SDCardHeader.PIDLog.StartBlock + SDCARD_PIDLOG_MAXBLOCKS) - 1;
            if (SDCardHeader.PIDLog.WriteBlock > LastPIDBlock)
            {
              // if the data wraps or not is user configurable
              if (Settings.Log.PID.Wrap == 1)
              {
                SDCardHeader.PIDLog.WriteBlock = SDCardHeader.PIDLog.StartBlock;
              }
              else  // stop logging
              {
                Settings.Log.PID.Door = 0;
                Settings.Log.PID.Refrig = 0;
              }
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
        debug_printf("SD card/PID Log write error\r\n");
        SDCardHeader.Errors.Write++;
        ActiveSDCard.Initialized = 0;
      }

      SDCardHeaderUpdate();

      // Release the semaphore.
      xSemaphoreGive(ActiveSDCard.Semaphore);
    }
    else
    {
      debug_printf("PIDLogWrite blocked for SDCardSave\r\n");
    }

    if (WriteSuccess)
    {
      // start new block of PID values
      PIDLogIndex = 0;
    }
    else
    {
      // failed to write block so just overwrite the last item in the block
      // and hope to write the block successfully next time and only lose 1
      // value instead of entire block
      PIDLogIndex--;
    }
  }

  return(Status);
} // end PIDLogWrite()

/***************************************************************************

FUNCTION:   SendToFile()

PURPOSE:    Send the data from each record to the LTX

COMMENTS:   This sends a series of messages to the LTX that look like this:

            Data = "12/15/07,10:15,1.65,8.2,1.6,...

***************************************************************************/
void SendToFile(int PIDIndex)
{
  char str[50];

  // initiate the multi-message transfer
  UI_SendMultiHdr("LogData,Data", SessionID);

  // set up the message
  snprintf(DataMsg,
           MSG_TX_BUFFER_SIZE,
           "Data=%u,%s,%s,%.2f,%.1f,%.1f,%.1f,%d,%d,",
           PIDLogBuf[PIDIndex].RecordNum,
           PIDLogBuf[PIDIndex].Date,
           PIDLogBuf[PIDIndex].Time,
           PIDLogBuf[PIDIndex].Error/100.0,
           PIDLogBuf[PIDIndex].P/10.0,
           PIDLogBuf[PIDIndex].I/10.0,
           PIDLogBuf[PIDIndex].D/10.0,
           PIDLogBuf[PIDIndex].Output,
           PIDLogBuf[PIDIndex].Type);

  // terminate and send the message
  MultiMsgAdd(DataMsg, "", "", 1);

  // terminate the multi-message transfer
  UI_SendMultiEnd("LogData");

  // send the end of record message
  snprintf(str, sizeof(str), "LogToFile=EOR,%d", SessionID);
  SendMsgAndWaitForResponse(str, UI_DELAY_LONG);
} // end SendToFile()

/***************************************************************************

FUNCTION:   SendToUI()

PURPOSE:    Send the data from each record to the LTX

COMMENTS:   This function keeps track of the information for the following
            messages:

            Data = "1.65,8.2,1.6,...
            Dates = "09/28/2007:20,";
            TimeStamps = "09:25:45#12,09:24:45#12,09:23:44#0,09:22:44#0,..."

            It tracks how many data points were taken for each date (PointsPerDate)

***************************************************************************/
void SendToUI(int PIDIndex)
{
  char str[100];

  // store the first date
  if (strcmp(PIDLogDate, "") == 0)
    StringCopy(PIDLogDate, PIDLogBuf[PIDIndex].Date, DATE_LEN+1);

  // check for start of new date
  if (strcmp(PIDLogBuf[PIDIndex].Date, PIDLogDate) != 0)
  {
    // add the date and PointsPerDate to the message
    snprintf(str, sizeof(str), "%s?%d,", PIDLogDate, PointsPerDate);
    MultiMsgAdd(DatesMsg, "Dates=", str, 0);
    StringCopy(PIDLogDate, PIDLogBuf[PIDIndex].Date, DATE_LEN+1);
    PointsPerDate = 0;
  }

  // set up the message
  snprintf(str,
           sizeof(str),
           "%.2f,%.1f,%.1f,%.1f,%d,",
           PIDLogBuf[PIDIndex].Error/100.0,
           PIDLogBuf[PIDIndex].P/10.0,
           PIDLogBuf[PIDIndex].I/10.0,
           PIDLogBuf[PIDIndex].D/10.0,
           PIDLogBuf[PIDIndex].Output);

  // terminate and send the message
  MultiMsgAdd(DataMsg, "Data=", str, 0);

  // add timestamp
  snprintf(str, sizeof(str), "%s,", PIDLogBuf[PIDIndex].Time);
  MultiMsgAdd(TimesMsg, "TimeStamps=", str, 0);

  PointsPerDate++;
} // end SendToUI()

/***   End Of File   ***/
