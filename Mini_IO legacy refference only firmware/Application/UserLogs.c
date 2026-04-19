/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     UserLogs.c

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:  Read and write the data logs

COMMENTS: UserLog => UI History Log

***************************************************************************/

/*** include files ***/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "debug.h"

#include "Controls.h"
#include "DataExc.h"
#include "PWM.h"
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

USERLOG_RECORD UserLog;
char SendingLogData = 0;

static int SessionID;

/*** static functions ***/

static void BuildSensorList(void);
static void GetLogRecords(int BeginBlock, int EndBlock, void (*CallFunction)(void));
static void KeyForFile(void);
static void KeyForQueryItems(void);
static void SendAllItems(void);
static void SendQueryItems(int Mode);
static int UserLogFindDate(char *TargetDate);
static int UserLogRangeBegin(char *BeginDate);
static int UserLogRangeEnd(char *EndDate);
static void UserLogRead(int BeginBlock, int EndBlock, void (*CallFunction)(void));
static int UserLogSearch(int FirstBlock, int  LastBlock, char *TargetDate);

/***************************************************************************

FUNCTION:   BuildSensorList()

PURPOSE:    Checks a user log record to see which sensors have valid values

COMMENTS:   The SensorList is used to build the label string for the sensors
            that is sent to the UI to label the columns.  Because the
            sensor values stored in each data log record won't necessarily
            be consistent, (a sensor may give an invalid reading or there
            may be a comm error with the board causing sensor values to
            be SENSOR_VAL_UNDEFINED) a first pass is made through the user
            log records selected by the query to check each log record for
            valid sensors values so the label string will have a labels
            for all sensors with valid data requested in the query.

***************************************************************************/
void BuildSensorList(void)
{
  int i;

  for (i = 0; i < ANALOG_BOARDS_PER_SYSTEM * ANALOG_SENSORS_PER_BOARD; i++)
  {
    if (UserLog.Sensors[i] != SENSOR_VAL_UNDEFINED)
    {
      SensorList[i] = 1;
    }
  }
} // end BuildSensorList()

/***************************************************************************

FUNCTION:   DateToInt()

PURPOSE:    Converts a date string to integers

COMMENTS:

***************************************************************************/
int DateToInt(char *DateStr)
{
  short Year = 0;
  char  Month = 0;
  char  Day = 0;
  char  str[DATE_LEN+1];

  if (strlen(DateStr) != DATE_LEN)
    return 0;

  // month
  str[0] = DateStr[0];
  str[1] = DateStr[1];
  str[2] = 0;
  Month = atoi(str);

  // day
  str[0] = DateStr[3];
  str[1] = DateStr[4];
  str[2] = 0;
  Day = atoi(str);

  // year
  str[0] = DateStr[6];
  str[1] = DateStr[7];
  str[2] = DateStr[8];
  str[3] = DateStr[9];
  str[4] = 0;
  Year = atoi(str);

  return((Year * 10000) + (Month * 100) + Day);
} // end DateToInt()

/***************************************************************************

FUNCTION:   GetLogRecords()

PURPOSE:    Determine which records to read

COMMENTS:   The user/history log is circular

***************************************************************************/
void GetLogRecords(int BeginBlock, int EndBlock, void (*CallFunction)(void))
{
  // if the records are 'wrapped around'
  if (BeginBlock > EndBlock)
  {
    // get the records from the start date to the end before it wraps around
    UserLogRead(BeginBlock, SDCardHeader.UserLog.RecordsPerCard, CallFunction);

    // set up to get the remaining ones at the beginning
    BeginBlock = SDCardHeader.UserLog.StartBlock;
  }

  UserLogRead(BeginBlock, EndBlock, CallFunction);
} // end GetLogRecords()

/***************************************************************************

FUNCTION:   KeyForFile()

PURPOSE:    Send the key (column labels) to the LTX (then to USB storage device)

COMMENTS:   Uses the various structures to extract the appropriate data
            element labels to build the string.

            QueryTags[] - defined in QueryTags_Init()
            AnalogBoard[].Sensor[].Label - user defined, stored in Settings
            SysLogRecordLabels[] - defined in SystemLogLabel_Init()

            See comment in SystemLogBuildSensorKey() for explanation
            of how SensorList is used to find appropriate sensor labels.

            It builds a string that looks like this:

            Data = "Plen Temp SP:deg,Plen Temp:deg,Cool Avl Temp:deg,..."

            In this case the 'key' is sent with the 'data' message tag because
            it is just treated like a regular data record when it is written
            out to the USB storage device.

***************************************************************************/
void KeyForFile(void)
{
  int  i;
  char str[50];

  // initiate the multi-message transfer
  UI_SendMultiHdr("LogData,Data", SessionID);

  // set up the messages
  strcpy(DataMsg, "Data=");

  // send the record info header
  snprintf(str,
           sizeof(str),
           "%s,%s,%s,",
           SysLogRecordLabels[SL_RECORD].Label,
           SysLogRecordLabels[SL_DATE].Label,
           SysLogRecordLabels[SL_TIME].Label);
  MultiMsgAdd(DataMsg, "Data=", str, 0);

  // send the main page labels
  for (i = 0; i < LOG_MAINITEMS; i++)
    FormatLogMainItems(UserLog.MainPage[i], i, FILE_MODE, LOG_LABEL);

  // send the sensor labels
  for (i = 0; i < (ANALOG_BOARDS_PER_SYSTEM * ANALOG_SENSORS_PER_BOARD); i++)
    if (SensorList[i] == 1)
      FormatLogSensor(UserLog.Sensors, i, FILE_MODE, LOG_LABEL);

  // terminate and send the remaining message
  MultiMsgAdd(DataMsg, "", "", 1);

  // terminate the multi-message transfer
  UI_SendMultiEnd("LogData");

  // send end-of-record (EOR) message - causes LTX to write record to the file
  snprintf(str, sizeof(str), "LogToFile=EOR,%d", SessionID);
  SendMsgAndWaitForResponse(str, UI_DELAY_LONG);
} // end KeyForFile()

/***************************************************************************

FUNCTION:   KeyForQueryItems()

PURPOSE:    Build the label string (key) to be used in the UI

COMMENTS:   Uses the various structures to extract the appropriate data
            element labels and units to build the string.

            QueryTags[] - defined in QueryTags_Init()
            AnalogBoard[].Sensor[].Label - user defined, stored in Settings

            It builds a string that looks like this:

            Key = "Plen Temp SP:deg,Plen Temp:deg,Cool Avl Temp:deg,Plen Hum SP:%..."

***************************************************************************/
void KeyForQueryItems(void)
{
  int  i;

  // set up the message
  strcpy(KeyMsg, "Key=");

  // process the query
  for (i = 0; i < DATAQUERY_LEN; i++) // length of query array
    if (DataQuery[i] != -1)   // requested item
    {
      if (i < LOG_MAINITEMS)        // main array elements are flags
        FormatLogMainItems(UserLog.MainPage[i], i, TABLE_MODE, LOG_LABEL);
      else
        FormatLogSensor(UserLog.Sensors, DataQuery[i], TABLE_MODE, LOG_LABEL);
    }

  // terminate and send the remaining message
  MultiMsgAdd(KeyMsg, "", "", 1);
} // end KeyForQueryItems()

/***************************************************************************

FUNCTION:   SendAllItems()

PURPOSE:    Send the user log data to the LTX (then to USB storage device)

COMMENTS:   This sends a series of messages to the LTX that look like this:

            Data = "46.0,*,51.0,95,1,45,0,0,21.4,*,..."

***************************************************************************/
void SendAllItems(void)
{
  int  i;
  char str[50];

  // set up the message
  strcpy(DataMsg, "Data=");

  // send the record header/labels
  snprintf(str, sizeof(str), "%u,%s,%s,", UserLog.RecordNum, UserLog.Date, UserLog.Time);
  MultiMsgAdd(DataMsg, "Data=", str, 0);

  // send the main page items
  for (i = 0; i < LOG_MAINITEMS; i++)
    FormatLogMainItems(UserLog.MainPage[i], i, TABLE_MODE, LOG_DATA);

  // send the sensor data
  for (i = 0; i < (ANALOG_BOARDS_PER_SYSTEM * ANALOG_SENSORS_PER_BOARD); i++)
    if (SensorList[i] == 1)
      FormatLogSensor(UserLog.Sensors, i, TABLE_MODE, LOG_DATA);

  // terminate and send remaining message
  MultiMsgAdd(DataMsg, "Data=", "EOR", 1);
} // end SendAllItems()

/***************************************************************************

FUNCTION:   SendQueryItems()

PURPOSE:    Send the data from a user log record to the LTX/UI

COMMENTS:   This sends a series of messages to the LTX that look like this:

            Data = "46.0,*,51.0,95,1,45,0,0,21.4,*,..."

***************************************************************************/
void SendQueryItems(int Mode)
{
  int i;

  // process the query
  for (i = 0; i < DATAQUERY_LEN; i++)
  {
    if (DataQuery[i] != -1)   // requested item
    {
      if (i < LOG_MAINITEMS)
      {
        FormatLogMainItems(UserLog.MainPage[i], i, Mode, LOG_DATA);
      }
      else
      {
        FormatLogSensor(UserLog.Sensors, DataQuery[i], Mode, LOG_DATA);   // sensor elements are sensor IDs
      }
    }
  }
} // end SendQueryItems()

/***************************************************************************

FUNCTION:   UserLogFindDate()

PURPOSE:    Finds a record with the target date (or the closest date possible)

COMMENTS:   The data on the SD card is assumed to be in chronological order.
            But the data can wrap around the card.  So this function determines
            if the data has wrapped or not and then calls the binary search
            function with the appropriate range.

            UserLogRangeBegin() and UserLogRangeEnd() are then called to
            sequentially search from that point through the data to find the
            beginning or end of the range.

***************************************************************************/
int UserLogFindDate(char *TargetDate)
{
  int  CardStartBlock;
  int  CardEndBlock;
  int  FirstRecordBlock = SDCardHeader.UserLog.StartBlock;
  int  LastRecordBlock = SDCardHeader.UserLog.WriteBlock - SDCardHeader.UserLog.BlocksPerRecord;
  int  Found = 0;

  // check if the data has wrapped around the card or not
  if (SDCardHeader.UserLog.NumRecords < SDCardHeader.UserLog.RecordsPerCard)
  {
    Found = UserLogSearch(FirstRecordBlock, LastRecordBlock, TargetDate);
  }
  else    // wrapped around
  {
    FirstRecordBlock = SDCardHeader.UserLog.WriteBlock;    // oldest record
    CardStartBlock = SDCardHeader.UserLog.StartBlock;
    CardEndBlock = (  (SDCardHeader.UserLog.RecordsPerCard - 1)
                     * SDCardHeader.UserLog.BlocksPerRecord)
                     + SDCardHeader.UserLog.StartBlock;

    // call the search on first part of the data
    Found = UserLogSearch(FirstRecordBlock, CardEndBlock, TargetDate);

    // if it's not found, call the search for wrapped part of the data
    if (Found <= 0)
      Found = UserLogSearch(CardStartBlock, LastRecordBlock, TargetDate);
  }

  return Found;
} // end UserLogFindDate()

/***************************************************************************

FUNCTION:   UserLogRangeBegin()

PURPOSE:    Find the beginning record in a date range

COMMENTS:   The data on the SD card is assumed to be in chronological order,
            but it can be wrapped around the card.

            UserLogFindDate() performs a binary search to find a record with the
            target date.  UserLogRangeBegin() and UserLogRangeEnd() are then
            called to sequentially search through the data to find the beginning
            and end of the range.

            Tested 10/18/2007
            Begin date before first data point date -> returns first data point
            Begin date after last data point date -> returns last data point
            Arbitrarily caused data to wrap on card
            Begin date before oldest data point date -> returns oldest data point
            Begin date after last data point date -> returns last data point

***************************************************************************/
int UserLogRangeBegin(char *BeginDate)
{
  char RecordDate[SDCARD_DATE_OFFSET+DATE_LEN+1];
  int  CardStartBlock;
  int  Begin;
  int  Block;
  int  Date = 0;
  int  WrapAround = 0;

  Block = UserLogFindDate(BeginDate);
  Begin = DateToInt(BeginDate);

  // not found
  if (Block == -1)
  {
    return -1;
  }

  // determine if the data has wrapped around the card
  if (SDCardHeader.UserLog.NumRecords < SDCardHeader.UserLog.RecordsPerCard)
  {
    CardStartBlock = SDCardHeader.UserLog.StartBlock;
  }
  else    // wrapped around
  {
    if (Block < SDCardHeader.UserLog.WriteBlock)
    {
      CardStartBlock = SDCardHeader.UserLog.StartBlock;
      WrapAround = 1;
    }
    else
    {
      CardStartBlock = SDCardHeader.UserLog.WriteBlock;    // oldest record
    }
  }

  if (ActiveSDCard.Semaphore != NULL)
  {
    // Obtain the semaphore - block if the semaphore is not immediately available.
    if (xSemaphoreTake(ActiveSDCard.Semaphore, ActiveSDCard.BlockTimeout) == pdTRUE)
    {
      while (Block >= CardStartBlock)
      {
        SDCardRead((char *)&RecordDate, SDCARD_DATE_OFFSET+DATE_LEN+1, Block);
        Date = DateToInt(RecordDate + SDCARD_DATE_OFFSET);
        if ((Date == 0) || (Date < Begin))
        {
          Block = Block + SDCardHeader.UserLog.BlocksPerRecord;
          break;
        }

        Block = Block - SDCardHeader.UserLog.BlocksPerRecord;

        // if necessary, start looking from the end/last record on the card
        if (Block < CardStartBlock && WrapAround == 1)
        {
          Block = (  (SDCardHeader.UserLog.RecordsPerCard - 1)
                    * SDCardHeader.UserLog.BlocksPerRecord)
                    + SDCardHeader.UserLog.StartBlock;
          CardStartBlock = SDCardHeader.UserLog.WriteBlock;
          WrapAround = 0;
        }
      }

      // Release the semaphore.
      xSemaphoreGive(ActiveSDCard.Semaphore);
    }
    else
    {
      debug_printf("UserLogRangeBegin blocked\r\n");
    }
  }
  else
  {
    ActiveSDCard.Initialized = 0;
  }

  if (Block < CardStartBlock)
  {
    Block = CardStartBlock;
  }

  return Block;
} // end UserLogRangeBegin()

/***************************************************************************

FUNCTION:   UserLogRangeEnd()

PURPOSE:    Find the ending record in a date range

COMMENTS:   The data on the SD card is assumed to be in chronological order,
            but it can be wrapped around the card.

            UserLogFindDate() performs a binary search to find a record with the
            target date.  UserLogRangeBegin() and UserLogRangeEnd() are then
            called to sequentially search through the data to find the beginning
            and end of the range.

            Tested 10/18/2007
            End date before first data point date -> returns first data point
            End date after last data point date -> returns last data point
            Arbitrarily caused data to wrap on card
            End date before oldest data point date -> returns oldest data point
            End date after last data point date -> returns last data point

***************************************************************************/
int UserLogRangeEnd(char *EndDate)
{
  char RecordDate[SDCARD_DATE_OFFSET+DATE_LEN+1];
  int  CardEndBlock;
  int  Block;
  int  End;
  int  Date = 0;
  int  WrapAround = 0;

  Block = UserLogFindDate(EndDate);
  End = DateToInt(EndDate);

  // not found
  if (Block == -1)
  {
    return -1;
  }

  // determine if the data has wrapped around the card
  if (SDCardHeader.UserLog.NumRecords < SDCardHeader.UserLog.RecordsPerCard)
  {
    CardEndBlock = SDCardHeader.UserLog.WriteBlock;
  }
  else    // wrapped around
  {
    if (Block < SDCardHeader.UserLog.WriteBlock)
    {
      CardEndBlock = SDCardHeader.UserLog.WriteBlock;
    }
    else
    {
      CardEndBlock = (  (SDCardHeader.UserLog.RecordsPerCard - 1)
                       * SDCardHeader.UserLog.BlocksPerRecord)
                       + SDCardHeader.UserLog.StartBlock;
      WrapAround = 1;
    }
  }

  if (ActiveSDCard.Semaphore != NULL)
  {
    // Obtain the semaphore - block if the semaphore is not immediately available.
    if (xSemaphoreTake(ActiveSDCard.Semaphore, ActiveSDCard.BlockTimeout) == pdTRUE)
    {
      while (Block < CardEndBlock)
      {
        SDCardRead((char *)&RecordDate, SDCARD_DATE_OFFSET+DATE_LEN+1, Block);
        Date = DateToInt(RecordDate + SDCARD_DATE_OFFSET);
        if ((Date == 0) || (Date > End))
        {
          Block = Block - SDCardHeader.UserLog.BlocksPerRecord;
          break;
        }

        Block = Block + SDCardHeader.UserLog.BlocksPerRecord;

        // if necessary, start looking from the beginning/first record on the card
        if (Block == CardEndBlock && WrapAround == 1)
        {
          Block = SDCardHeader.UserLog.StartBlock;
          CardEndBlock = SDCardHeader.UserLog.WriteBlock;
          WrapAround = 0;
        }
      }

      // Release the semaphore.
      xSemaphoreGive(ActiveSDCard.Semaphore);
    }
    else
    {
      debug_printf("UserLogRangeEnd blocked\r\n");
    }
  }
  else
  {
    ActiveSDCard.Initialized = 0;
  }

  if (   Block >= SDCardHeader.UserLog.WriteBlock
      || Block < SDCardHeader.UserLog.StartBlock)
  {
    Block = SDCardHeader.UserLog.WriteBlock - SDCardHeader.UserLog.BlocksPerRecord;
  }

  return Block;
} // end UserLogRangeEnd()

/***************************************************************************

FUNCTION:   UserLogRead()

PURPOSE:    Reads and processes the requested blocks from the SD card

COMMENTS:   This reads in each record of the requested range and calls the
            passed in function to process the record.

***************************************************************************/
void UserLogRead(int BeginBlock, int EndBlock, void (*CallFunction)(void))
{
  int i;
  int Status = 0;
  char str[50];

  if (ActiveSDCard.Semaphore != NULL)
  {
    // Obtain the semaphore - block if the semaphore is not immediately available.
    if (xSemaphoreTake(ActiveSDCard.Semaphore, ActiveSDCard.BlockTimeout) == pdTRUE)
    {
      for (i = BeginBlock; i <= EndBlock; i++)
      {
        Status = SDCardRead((char *)&UserLog, sizeof(USERLOG_RECORD), i);
        if (Status == 0)
        {
          break;
        }
        // call the passed in function
        (*CallFunction)();

        // reset the watchdog external timer
        ThreadMonitorUpdate(TM_UI_UPDATE);

        // send a keep alive so the UI doesn't timeout
        if ((i % 1000) == 0)
        {
          snprintf(str, sizeof(str), "LogSearch=%d,%d", 0, SessionID);
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
      debug_printf("UserLogRead blocked\r\n");
    }
  }
  else
  {
    ActiveSDCard.Initialized = 0;
  }
} // end UserLogRead

/***************************************************************************

FUNCTION:   UserLogReset()

PURPOSE:    Reset/initialize the user log and SD card header

COMMENTS:   Callers that don't want the header written must obvioulsy write
            the header themselves and therefore should lock the sd card
            semaphore.  Callers wanting the header written should not lock
            the sd card semaphore before this call.

***************************************************************************/
void UserLogReset(SDCARD_HEADER *Header, int WriteHeader)
{
  int NumBlocks = 0;

  WarningsSet(WARN_USERLOG_CLEAR, FM_ALARM, FM_ALARM, NA);

  Header->UserLog.StartBlock = SDCARD_USERLOG_STARTBLOCK;
  Header->UserLog.WriteBlock = SDCARD_USERLOG_STARTBLOCK;
  Header->UserLog.NumRecords = 0;
  Header->UserLog.RecordLen = sizeof(USERLOG_RECORD);
  Header->UserLog.DataStartDate[0] = 0;

  Header->UserLog.BlocksPerRecord = Header->UserLog.RecordLen/SDCARD_BLOCK_SIZE;
  if ((Header->UserLog.BlocksPerRecord * SDCARD_BLOCK_SIZE) < Header->UserLog.RecordLen)
  {
    Header->UserLog.BlocksPerRecord++;
  }

  // determine the number of blocks available for the user log
  NumBlocks = ActiveSDCard.Size;            // total
  NumBlocks -= SDCARD_USERLOG_STARTBLOCK;   // subtract those reserved for the header
  NumBlocks -= SDCARD_LOADLOG_MAXBLOCKS;    // subtract the load log records
  NumBlocks -= Header->SysLog.MaxBlocks;    // subtract the system log records stored next to the end
  NumBlocks -= SDCARD_PIDLOG_MAXBLOCKS;     // subtract the PID log records stored at the end

  // determine the number of records on the card
  Header->UserLog.RecordsPerCard = NumBlocks/Header->UserLog.BlocksPerRecord;

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
        debug_printf("UserLogReset blocked at SDCardHeaderUpdate\r\n");
      }
    }
    else
    {
      ActiveSDCard.Initialized = 0;
    }
  }
} // end UserLogReset()

/***************************************************************************

FUNCTION:   UserLogSearch()

PURPOSE:    Perform a binary search to find a record with the target date

COMMENTS:   The data on the SD card is assumed to be in chronological order.
            The function performs a binary search to find a record with the
            target date.

            UserLogRangeBegin() and UserLogRangeEnd() are then called to
            sequentially search from that point through the data to find the
            beginning or end of the range.

***************************************************************************/
int UserLogSearch(int FirstBlock, int  LastBlock, char *TargetDate)
{
  char RecordDate[SDCARD_DATE_OFFSET+DATE_LEN+1];
  int  TargetBlock = FirstBlock;
  int  Target;
  int  Found = 0;
  int  Mid = 0;
  int  MidBlock = 0;
  int  Status = 0;

  Target = DateToInt(TargetDate);

  if (Target == 0)
  {
    debug_printf("Bad TargetDate\r\n");
    return -1;
  }

  if (ActiveSDCard.Semaphore != NULL)
  {
    // Obtain the semaphore - block if the semaphore is not immediately available.
    if (xSemaphoreTake(ActiveSDCard.Semaphore, ActiveSDCard.BlockTimeout) == pdTRUE)
    {
      while (LastBlock >= FirstBlock)
      {
        MidBlock = (FirstBlock + LastBlock)/2;  // this might need help if record > block !!!
        Status = SDCardRead((char *)&RecordDate, SDCARD_DATE_OFFSET+DATE_LEN+1, MidBlock);
        if (Status == 0)
        {
          // Release the semaphore.
          xSemaphoreGive(ActiveSDCard.Semaphore);
          debug_printf("Error reading record:%d\r\n",MidBlock);
          return -1;
        }

        Mid = DateToInt(RecordDate + SDCARD_DATE_OFFSET);
        if (Mid == 0)
        {
          // Release the semaphore.
          xSemaphoreGive(ActiveSDCard.Semaphore);
          debug_printf("Bad date - record:%d\r\n",MidBlock);
          return -1;
        }

        TargetBlock = MidBlock;

        if (Target < Mid)
        {
          LastBlock = MidBlock - SDCardHeader.UserLog.BlocksPerRecord;
        }
        else if (Target > Mid)
        {
          FirstBlock = MidBlock + SDCardHeader.UserLog.BlocksPerRecord;
        }
        else
        {
          Found = 1;
          break;
        }
      }

      // Release the semaphore.
      xSemaphoreGive(ActiveSDCard.Semaphore);
    }
    else
    {
      debug_printf("UserLogSearch blocked\r\n");
    }
  }
  else
  {
    ActiveSDCard.Initialized = 0;
  }

  if (!Found)
  {
    StringCopy(TargetDate, RecordDate + SDCARD_DATE_OFFSET, DATE_LEN+1);
    debug_printf("Record not found - date adjusted:%s\r\n",TargetDate);
  }

  return TargetBlock;
} // end UserLogSearch()

/***************************************************************************

FUNCTION:   UserLogSendToFile()

PURPOSE:    Send the requested user log records to USB storage device

COMMENTS:   This sends the records to the LTX which sends them to the Linux
            display board which creates a file on a USB storage device.

            This is going to send a series of messages to the LTX that look
            like this:

            Data_Loaded = "false";
            Data = "46.0,*,51.0,95,1,45,0,0,21.4,*,...,1,0,0,1,0,0,1,0,..."
            Data_Loaded = "true";

            This will make a first pass through the requested data to
            determine the appropriate sensor labels needed for the key - for
            an explanation of this process see SystemLogBuildSensorKey().
            Then it sets up each of the messages and processes the data
            sending multi-messages as necessary.  Then it sends the transfer
            complete message.

***************************************************************************/
int UserLogSendToFile(char *BeginDate, char *EndDate, int Session)
{
  int BeginBlock;
  int EndBlock;
  int TotalRecords;
  char str[50];

  // set the modular SessionID variable and clear the kill flag
  SessionID = Session;
  KillMessage = -1;

  if (SDCardHeader.UserLog.NumRecords == 0)
  {
    // close the file opened on the USB device
    snprintf(str, sizeof(str), "LogToFile=EOF,%d", SessionID);
    SendMsgAndWaitForResponse(str, UI_DELAY_LONG);
    return 0;
  }

  BeginBlock = UserLogRangeBegin(BeginDate);
  EndBlock = UserLogRangeEnd(EndDate);

  if (BeginBlock == -1 || EndBlock == -1)
  {
    // close the file opened on the USB device
    snprintf(str, sizeof(str), "LogToFile=EOF,%d", SessionID);
    SendMsgAndWaitForResponse(str, UI_DELAY_LONG);
    return 0;
  }

  // suspend slave mode during log transmission
  SendingLogData = 1;
  UI_SendBasicSetup(MSG_SEND);

  // determine if the data has wrapped around the card
  if (EndBlock >= BeginBlock)
  {
    TotalRecords = (EndBlock - BeginBlock + 1)/SDCardHeader.UserLog.BlocksPerRecord;
  }
  else  // wrapped around
  {
    // the ones at the end before it wraps
    TotalRecords = ((SDCardHeader.UserLog.RecordsPerCard + SDCardHeader.UserLog.StartBlock) - BeginBlock)/SDCardHeader.UserLog.BlocksPerRecord;
    // plus the ones at the beginning
    TotalRecords += ((EndBlock - SDCardHeader.UserLog.StartBlock) + 1)/SDCardHeader.UserLog.BlocksPerRecord;
  }

  // send total records being transmitted
  snprintf(str, sizeof(str), "LogTotal=%d,%d", TotalRecords, SessionID);
  SendMsgAndWaitForResponse(str, UI_DELAY_LONG);

  // first pass through the data to build the SensorList
  memset(SensorList, 0, sizeof(SensorList));
  GetLogRecords(BeginBlock, EndBlock, BuildSensorList);

  // send the key/header
  KeyForFile();

  // initiate the multi-message transfer
  UI_SendMultiHdr("LogData,Data", SessionID);

  // send the data
  GetLogRecords(BeginBlock, EndBlock, SendAllItems);

  // terminate the multi-message transfer
  UI_SendMultiEnd("LogData");

  // send the end of file
  snprintf(str, sizeof(str), "LogToFile=EOF,%d", SessionID);
  SendMsgAndWaitForResponse(str, UI_DELAY_LONG);

  // re-enable slave mode (if in slave mode)
  SendingLogData = 0;
  UI_SendBasicSetup(MSG_SEND);

  return 1;
} // end UserLogSendToFile()

/***************************************************************************

FUNCTION:   UserLogSendToUI()

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
int UserLogSendToUI(char *BeginDate, char *EndDate, int Mode, int Session)
{
  int  BeginBlock = 0;
  int  EndBlock = 0;
  int  GetBlock = 0;
  int  PointsPerDate = 0;
  char RecordsDate[DATE_LEN+1] = "";
  char str[20];
  int  Status = 1;
  int  TotalRecords;

  // set the modular SessionID variable and clear the kill flag
  SessionID = Session;
  KillMessage = -1;

  if (SDCardHeader.UserLog.NumRecords == 0)
  {
    // clear the variables in the LTX
    UI_SendMultiHdr("LogData,Key,Dates,TimeStamps,Data", SessionID);
    UI_SendMultiEnd("LogData");
    return 0;
  }

  BeginBlock = UserLogRangeBegin(BeginDate);
  EndBlock = UserLogRangeEnd(EndDate);

  if (BeginBlock == -1 || EndBlock == -1)
  {
    // clear the variables in the LTX
    UI_SendMultiHdr("LogData,Key,Dates,TimeStamps,Data", SessionID);
    UI_SendMultiEnd("LogData");
    return 0;
  }

  // suspend slave mode during log transmission
  SendingLogData = 1;
  UI_SendBasicSetup(MSG_SEND);

  // determine if the data has wrapped around the card
  if (EndBlock >= BeginBlock)
  {
    TotalRecords = (EndBlock - BeginBlock + 1)/SDCardHeader.UserLog.BlocksPerRecord;
  }
  else  // wrapped around
  {
    // the ones at the end before it wraps
    TotalRecords = ((SDCardHeader.UserLog.RecordsPerCard + SDCardHeader.UserLog.StartBlock) - BeginBlock)/SDCardHeader.UserLog.BlocksPerRecord;
    // plus the ones at the beginning
    TotalRecords += ((EndBlock - SDCardHeader.UserLog.StartBlock) + 1)/SDCardHeader.UserLog.BlocksPerRecord;
  }

  // when sending to the UI the messages sent don't correspond to the number of
  // history records so send a zero so the UI can change the display
  snprintf(str, sizeof(str), "LogTotal=%d,%d", 0, SessionID);
  SendMsgAndWaitForResponse(str, UI_DELAY_LONG);

  // initiate the multi-message transfer
  UI_SendMultiHdr("LogData,Key,Dates,TimeStamps,Data", SessionID);

  // set up the messages
  KeyForQueryItems();
  strcpy(DatesMsg, "Dates=");
  strcpy(TimesMsg, "TimeStamps=");
  strcpy(DataMsg, "Data=");

  GetBlock = BeginBlock;

  if (ActiveSDCard.Semaphore != NULL)
  {
    // Obtain the semaphore - block if the semaphore is not immediately available.
    if (xSemaphoreTake(ActiveSDCard.Semaphore, ActiveSDCard.BlockTimeout) == pdTRUE)
    {
      while (TotalRecords--)
      {
        Status = SDCardRead((char *)&UserLog, sizeof(USERLOG_RECORD), GetBlock);
        if (Status == 0)
        {
          break;
        }

        // store the first date
        if (strcmp(RecordsDate, "") == 0)
        {
          StringCopy(RecordsDate, UserLog.Date, DATE_LEN+1);
        }

        // check for start of new date
        if (strcmp(UserLog.Date, RecordsDate) != 0)
        {
          // add the date and data points per date to the message
          snprintf(str, sizeof(str), "%s?%d,", RecordsDate, PointsPerDate);
          MultiMsgAdd(DatesMsg, "Dates=", str, 0);
          StringCopy(RecordsDate, UserLog.Date, DATE_LEN+1);
          PointsPerDate = 0;
        }

        // add timestamp
        snprintf(str, sizeof(str), "%s,", UserLog.Time);
        MultiMsgAdd(TimesMsg, "TimeStamps=", str, 0);

        // get the data points
        SendQueryItems(Mode);
        PointsPerDate++;

        GetBlock += SDCardHeader.UserLog.BlocksPerRecord;
        if (GetBlock > (   (SDCardHeader.UserLog.RecordsPerCard - 1)
                           * SDCardHeader.UserLog.BlocksPerRecord)
                         + SDCardHeader.UserLog.StartBlock)
        {
          GetBlock = SDCardHeader.UserLog.StartBlock;
        }

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
      debug_printf("UserLogSendToUI blocked\r\n");
    }
  }
  else
  {
    ActiveSDCard.Initialized = 0;
  }

  if (KillMessage != SessionID)
  {
    // terminate and send remaining messages
    snprintf(str, sizeof(str), "%s?%d,", RecordsDate, PointsPerDate);
    MultiMsgAdd(DatesMsg, "Dates=", str, 1);
    MultiMsgAdd(TimesMsg, "", "", 1);
    MultiMsgAdd(DataMsg, "", "", 1);
  }

  // terminate the multi-message transfer
  UI_SendMultiEnd("LogData");

  // re-enable slave mode (if in slave mode)
  SendingLogData = 0;
  UI_SendBasicSetup(MSG_SEND);

  if (Status == 0)
  {
    WarningsSet(WARN_DATALOGREAD, FM_ALARM, FM_ALARM, NA);
    ActiveSDCard.Initialized = 0;
  }

  return 1;
} // end UserLogSendToUI()

/***************************************************************************

FUNCTION:   UserLogWrite()

PURPOSE:    Build a user log record and write it to the SD card

COMMENTS:

***************************************************************************/
int UserLogWrite(void)
{
  int  i,j;
  char Hour = 0;
  char Min = 0;
  char Sec = 0;
  int  Status = 1;
  int  WriteSuccess = 0;
  int  WriteRetries = 0;
  int  ReadRetries = 0;
  USERLOG_RECORD CheckRec;
  USERLOG_RECORD OldestRecord;

  if (   ActiveSDCard.Initialized == 0
      || ActiveSDCard.Semaphore == NULL)
  {
    ActiveSDCard.Initialized = 0;
    WarningsSet(WARN_SDCARD_NONE, FM_ALARM, FM_ALARM, NA);
    return 0;
  }

  if (SDCardHeader.CardFull)
  {
    WarningsSet(WARN_DATALOGFULL, FM_ALARM, FM_ALARM, NA);
    return 0;
  }

  // initialize the datalog record
  memset(&UserLog.MainPage, 0, sizeof(UserLog.MainPage));
  for (i = 0; i < (ANALOG_BOARDS_PER_SYSTEM * ANALOG_SENSORS_PER_BOARD); i++)
  {
    UserLog.Sensors[i] = SENSOR_VAL_UNDEFINED;
  }

  if (   GetDateStr(UserLog.Date) == 0
      || GetTime(&Hour, &Min, &Sec) == 0)
  {
    return 1;
  }

  // store the record information
  UserLog.RecordNum = SDCardHeader.UserLog.NumRecords + 1;
  snprintf(UserLog.Time, TIME_LEN+1, "%02d:%02d:%02d", Hour, Min, Sec);
//  snprintf(UserLog.Time, TIME_LEN+1, "%02d:%02d", Hour, Min);

  // store system monitor page information
  if (PlenumTempAvg == SENSOR_VAL_UNDEFINED)
  {
    UserLog.MainPage[LOG_PLENTEMP] = SENSOR_VAL_UNDEFINED;
  }
  else
  {
    UserLog.MainPage[LOG_PLENTEMP] = (PlenumTempAvg + .05) * 10;
  }

  if (CurrentMode == UI_FAILURE)
  {
    UserLog.MainPage[LOG_FANSPEED] = PWMValToPercent(PWM_MIN_VALUE);
  }
  else
  {
    UserLog.MainPage[LOG_FANSPEED] = PWMValToPercent(PwmChannel[PWM_FAN].Output);
  }

  UserLog.MainPage[LOG_PLENTEMPSET]     = (Settings.Plenum.TempSet + .05) * 10;
  UserLog.MainPage[LOG_COOLAVAILTEMP]   = (StartTemp + .05) * 10;
  UserLog.MainPage[LOG_PLENHUMSET]      = Settings.Plenum.HumidSet;
  UserLog.MainPage[LOG_MODE]            = CurrentMode;
  UserLog.MainPage[LOG_DAILYFANRUNTIME] = (Settings.Fan.DailyRunTime/3600.0 + .05) * 10;
  UserLog.MainPage[LOG_REFRIGOUTPUT]    = PWMValToPercent(PwmChannel[PWM_REFRIGERATION].Output);
  UserLog.MainPage[LOG_COOLOUTPUT]      = PWMValToPercent(PwmChannel[PWM_DOORS].Output);
  UserLog.MainPage[LOG_BURNOUTPUT]      = PWMValToPercent(PwmChannel[PWM_BURNER].Output);    // onion
  UserLog.MainPage[LOG_CALCHUM]         = CalculatedHumidity();   // onion

  // store the sensor values
  for (i = 0; i < ANALOG_BOARDS_PER_SYSTEM; i++)
  {
    if (Settings.AnalogBoard[i].Present == 1)
    {
      for (j = 0; j < ANALOG_SENSORS_PER_BOARD; j++)
      {
        SaveSensor(&(UserLog.Sensors[(i*ANALOG_SENSORS_PER_BOARD) + j]), i, j);
      }
    }
  }

  // Obtain the semaphore - block if the semaphore is not immediately available.
  if (xSemaphoreTake(ActiveSDCard.Semaphore, ActiveSDCard.BlockTimeout) == pdTRUE)
  {
    while (!WriteSuccess && WriteRetries < 3)
    {
      // write the record to the SD card
      Status = SDCardSave((unsigned char *)&UserLog, SDCardHeader.UserLog.RecordLen, SDCardHeader.UserLog.WriteBlock);
      if (Status == 0)
      {
        // check the accuracy of the write
        ReadRetries = 0;
        while (   SDCardRead((char *)&CheckRec, SDCardHeader.UserLog.RecordLen, SDCardHeader.UserLog.WriteBlock) != 1
               && ReadRetries++ < 3)
        {
          debug_printf("SD card/History Log read retry\r\n");
        }

        if (memcmp(&CheckRec, &UserLog, sizeof(USERLOG_RECORD)) != 0)
        {
          debug_printf("SD card/History Log write retry\r\n");
          WriteRetries++;
        }
        else
        {
          // write was successful, update header
          WriteSuccess = 1;
          SDCardHeader.UserLog.NumRecords++;

          // store the date of the first available data point on the SD card
          // the UI uses it to set the valid range for the start date calendar
          if (SDCardHeader.UserLog.NumRecords == 1)
          {
            StringCopy(SDCardHeader.UserLog.DataStartDate, UserLog.Date, DATE_LEN+1);
          }
          else if (SDCardHeader.UserLog.NumRecords > SDCardHeader.UserLog.RecordsPerCard)
          {
            // get the date from the next record which is now the oldest record available
            SDCardRead((char *)&OldestRecord,
                       SDCardHeader.UserLog.RecordLen,
                       SDCardHeader.UserLog.WriteBlock + SDCardHeader.UserLog.BlocksPerRecord);
            StringCopy(SDCardHeader.UserLog.DataStartDate, OldestRecord.Date, DATE_LEN+1);
          }

          SDCardHeader.UserLog.WriteBlock += SDCardHeader.UserLog.BlocksPerRecord;

          // handle wrapping the data around on the card
          if (SDCardHeader.UserLog.WriteBlock >= (SDCardHeader.UserLog.RecordsPerCard
                                                  * SDCardHeader.UserLog.BlocksPerRecord)
                                                  + SDCardHeader.UserLog.StartBlock)
          {
            // if the data wraps or not is user configurable
            if (Settings.Log.User.Wrap == 1)
            {
              SDCardHeader.UserLog.WriteBlock = SDCardHeader.UserLog.StartBlock;
            }
            else
            {
              SDCardHeader.CardFull = 1;
              WarningsSet(WARN_DATALOGFULL, FM_ALARM, FM_ALARM, NA);
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
      SDCardHeader.Errors.UserLog++;
      SDCardHeader.Errors.Write++;
      ActiveSDCard.Initialized = 0;

      if ((SDCardHeader.Errors.Write % 20) == 0)
      {
        WarningsSet(WARN_DATALOGWRITE, FM_ALARM, FM_ALARM, NA);
      }
    }

    SDCardHeaderUpdate();

    // Release the semaphore.
    xSemaphoreGive(ActiveSDCard.Semaphore);
  }
  else
  {
    debug_printf("UserLogWrite blocked for SDCardSave\r\n");
  }

  return(Status);
} // end UserLogWrite()

/***   End Of File   ***/
