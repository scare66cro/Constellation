/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     SDCard.c

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:  High level functions to read and write to the SD card

COMMENTS:

***************************************************************************/

/*** include files ***/

#include <stdlib.h>
#include <string.h>

// Platform
#include "KFS/kfs_port.h"
#include "debug.h"
#include "flash.h"

// Gellert
#include "LoadLogs.h"
#include "PIDLogs.h"
#include "SDCard.h"
#include "States.h"
#include "SystemLogs.h"
#include "UI_Messages.h"
#include "UserLogs.h"
#include "Warnings.h"

/*** defines ***/

/*** typedefs and structures ***/

/*** module variables ***/

ACTIVE_SDCARD ActiveSDCard;
SDCARD_HEADER SDCardHeader;

/*** static functions ***/

static void ConvertSDHdrFromV1(void *SDHdrVxx);
static int SDCardHeaderRead(char *SDHdr, int Size);

/***************************************************************************

FUNCTION:   ConvertSDHdrCheck()

PURPOSE:    Check if the SD card header needs to be converted from
            previous version

COMMENTS:

***************************************************************************/
void ConvertSDHdrCheck(void)
{
  void *SDHdrVxx;
  int Status = 0;

  SDHdrVxx = malloc(sizeof(SDCARD_HEADER_V1));
  if (SDHdrVxx == 0)
  {
    WarningsSet(WARN_SETTINGSCNVRTERR, FM_ALARM, FM_ALARM, NA);
    return;
  }

  Status = SDCardHeaderRead((char *)SDHdrVxx, sizeof(SDCARD_HEADER_V1));
  if (Status == 0)
  {
    free(SDHdrVxx);
    return;
  }

  if (((SDCARD_HEADER_V1 *)SDHdrVxx)->CRC == CRC((char *)SDHdrVxx, sizeof(SDCARD_HEADER_V1)-sizeof(unsigned int)))
  {
    ConvertSDHdrFromV1(SDHdrVxx);
  }
  else
  {
    SDHeaderReset(&SDCardHeader, 0);
  }

  SDCardHeaderUpdate();
  free(SDHdrVxx);
} // end ConvertSDHdrCheck()

/***************************************************************************

FUNCTION:   ConvertSDHdrFromV1()

PURPOSE:    Convert SD card header structure from one size to another

COMMENTS:

***************************************************************************/
void ConvertSDHdrFromV1(void *SDHdrVxx)
{
  int NumBlocks;
  SDCARD_HEADER_V1 *OldHdr = SDHdrVxx;

  memset(&SDCardHeader, 0, sizeof(SDCARD_HEADER));

  StringCopy(SDCardHeader.Version, SETTINGS_VERSION, VERSION_LEN+1);
  SDCardHeader.ID = OldHdr->ID;
  SDCardHeader.Size = OldHdr->Size;
  SDCardHeader.CardFull = OldHdr->CardFull;
//    SDCardHeader.CRC = OldHdr->CRC;

  SDCardHeader.UserLog.StartBlock = OldHdr->UserLog.StartBlock;
  SDCardHeader.UserLog.WriteBlock = OldHdr->UserLog.WriteBlock;
  SDCardHeader.UserLog.NumRecords = OldHdr->UserLog.NumRecords;
  SDCardHeader.UserLog.RecordLen = OldHdr->UserLog.RecordLen;
  SDCardHeader.UserLog.BlocksPerRecord = OldHdr->UserLog.BlocksPerRecord;
  SDCardHeader.UserLog.RecordsPerCard = OldHdr->UserLog.RecordsPerCard;
  StringCopy(SDCardHeader.UserLog.DataStartDate, OldHdr->UserLog.DataStartDate, DATE_LEN+1);

  SDCardHeader.SysLog.StartBlock = OldHdr->SysLog.StartBlock;
  SDCardHeader.SysLog.WriteBlock = OldHdr->SysLog.WriteBlock;
  SDCardHeader.SysLog.NumRecords = OldHdr->SysLog.NumRecords;
  SDCardHeader.SysLog.RecordLen = OldHdr->SysLog.RecordLen;
  SDCardHeader.SysLog.BlocksPerRecord = OldHdr->SysLog.BlocksPerRecord;
  SDCardHeader.SysLog.MaxBlocks = OldHdr->SysLog.MaxBlocks;

  SDCardHeader.PIDLog.StartBlock = OldHdr->PIDLog.StartBlock;
  SDCardHeader.PIDLog.WriteBlock = OldHdr->PIDLog.WriteBlock;
  SDCardHeader.PIDLog.NumRecords = OldHdr->PIDLog.NumRecords;

  SDCardHeader.Errors.SystemLog = OldHdr->Errors.SystemLog;
  SDCardHeader.Errors.UserLog = OldHdr->Errors.UserLog;
  SDCardHeader.Errors.Header = OldHdr->Errors.Header;
  SDCardHeader.Errors.Write = OldHdr->Errors.Write;

  LoadLogReset(&SDCardHeader, 0);

  if (SDCardHeader.LoadLog.StartBlock <= SDCardHeader.UserLog.WriteBlock)
  {
    UserLogReset(&SDCardHeader, 0);
  }
  else
  {
    // reduce the number of blocks available for the user log
    NumBlocks = ActiveSDCard.Size;                // total
    NumBlocks -= SDCARD_USERLOG_STARTBLOCK;       // subtract those reserved for the header
    NumBlocks -= SDCARD_LOADLOG_MAXBLOCKS;        // subtract the load log records
    NumBlocks -= SDCardHeader.SysLog.MaxBlocks;   // subtract the system log records stored next to the end
    NumBlocks -= SDCARD_PIDLOG_MAXBLOCKS;         // subtract the PID log records stored at the end

    // define the number of records on the card
    SDCardHeader.UserLog.RecordsPerCard = NumBlocks/SDCardHeader.UserLog.BlocksPerRecord;
  }

//  SDCardHeaderUpdate();
} // end ConvertSDHdrFromV1()

/***************************************************************************

FUNCTION:   SDCard_Init()

PURPOSE:    Ready the SD card for use

COMMENTS:   Reads the SD card header from the card and the SD header from
            flash and compares them to determine if the same SD card is
            present.  If it's a new card, it initializes it. If not, it decides
            based on the log record length whether to append to the card or
            initialize it.

***************************************************************************/
void SDCard_Init(void)
{
  SDCARD_HEADER FlashSdHdr;
  unsigned int Result;
  int Status = 0;
  int NumBlocks = 0;

  memset(&ActiveSDCard, 0, sizeof(ActiveSDCard));

  ActiveSDCard.BlockTimeout = 100 / portTICK_RATE_MS;
  vSemaphoreCreateBinary(ActiveSDCard.Semaphore);

  if (kfs_disk_initialize() != KFS_SUCCESS)
  {
    return;
  }

  ActiveSDCard.Initialized = 1;
  ActiveSDCard.Size = kfs_get_sector_count();

  // Obtain the semaphore - block if the semaphore is not immediately available.
  if (xSemaphoreTake(ActiveSDCard.Semaphore, ActiveSDCard.BlockTimeout) == pdTRUE)
  {
    // check SD card header from card
    Status = SDCardHeaderRead((char*)&SDCardHeader, sizeof(SDCARD_HEADER));
    if (Status == 0)
    {
      // Release the semaphore.
      xSemaphoreGive(ActiveSDCard.Semaphore);
      return;
    }

    if (SDCardHeader.CRC != CRC((char*)&SDCardHeader, sizeof(SDCARD_HEADER)-sizeof(unsigned int)))
    {
      // check if conversion is necessary
      ConvertSDHdrCheck();
    }

    // check SD card header stored in flash
    Result = FlashRead((unsigned char*)&FlashSdHdr, FLASH_SDCARD_HEADER, sizeof(SDCARD_HEADER));
    if (Result == sizeof(SDCARD_HEADER))
    {
      Status = 1;
    }

    if (   Status == 0
        || FlashSdHdr.CRC != CRC((char*)&FlashSdHdr, sizeof(SDCARD_HEADER)-sizeof(unsigned int)))
    {
      // if the header from flash is bad or can't be read at all, reset it so that it doesn't
      // cause the logs to be reset below
      memcpy(&FlashSdHdr, &SDCardHeader, sizeof(SDCARD_HEADER));
    }

    if (FlashSdHdr.CRC != SDCardHeader.CRC)    // headers don't match
    {
      if (   SDCardHeader.UserLog.RecordLen == FlashSdHdr.UserLog.RecordLen
          && SDCardHeader.SysLog.RecordLen == FlashSdHdr.SysLog.RecordLen)
      {
        // record sizes compatible - continue
        WarningsSet(WARN_SDCARD_DIFF, FM_ALARM, FM_ALARM, NA);
      }
    }

    if (   SDCardHeader.UserLog.RecordLen != FlashSdHdr.UserLog.RecordLen
        || SDCardHeader.UserLog.RecordLen != sizeof(USERLOG_RECORD))
    {
      UserLogReset(&SDCardHeader, 0);
    }

    if (   SDCardHeader.SysLog.RecordLen != FlashSdHdr.SysLog.RecordLen
        || SDCardHeader.SysLog.RecordLen != sizeof(SYSLOG_RECORD))
    {
      SystemLogReset(&SDCardHeader.SysLog, 0);
    }

    if (   SDCardHeader.LoadLog.RecordLen != FlashSdHdr.LoadLog.RecordLen
        || SDCardHeader.LoadLog.RecordLen != sizeof(LOADLOG_RECORD))
    {
      LoadLogReset(&SDCardHeader, 0);
    }

    // system log size check (increased size 4/27/12)
    if (SDCardHeader.SysLog.MaxBlocks != (NUM_SYSLOG_RECS * SDCardHeader.SysLog.BlocksPerRecord))
    {
      SystemLogReset(&SDCardHeader.SysLog, 0);

      if (SDCardHeader.SysLog.StartBlock <= SDCardHeader.UserLog.WriteBlock)
      {
        UserLogReset(&SDCardHeader, 0);
      }
      else
      {
        // reduce the number of blocks available for the user log
        NumBlocks = ActiveSDCard.Size;                // total
        NumBlocks -= SDCARD_USERLOG_STARTBLOCK;       // subtract those reserved for the header
        NumBlocks -= SDCARD_LOADLOG_MAXBLOCKS;        // subtract the load log records
        NumBlocks -= SDCardHeader.SysLog.MaxBlocks;   // subtract the system log records stored next to the end
        NumBlocks -= SDCARD_PIDLOG_MAXBLOCKS;         // subtract the PID log records stored at the end

        // define the number of records on the card
        SDCardHeader.UserLog.RecordsPerCard = NumBlocks/SDCardHeader.UserLog.BlocksPerRecord;
      }
    }

    SDCardHeaderUpdate();

    // Release the semaphore.
    xSemaphoreGive(ActiveSDCard.Semaphore);
  }

  // fix for total number of SysLog records
  if (SDCardHeader.SysLog.NumRecords > NUM_SYSLOG_RECS)
  {
    SDCardHeader.SysLog.NumRecords = NUM_SYSLOG_RECS;
  }
} // end SDCard_Init()

/***************************************************************************

FUNCTION:   SDCardHeaderRead()

PURPOSE:    Reads the SD card header from the SD card

COMMENTS:   If it fails to read the header the first time, it performs a low
            level card init and tries again.  If it fails a second time, it
            throws the error.

***************************************************************************/
int SDCardHeaderRead(char *SDHdr, int Size)
{
  int Status = SDCardRead(SDHdr, Size, SDCARD_HEADER_BLOCK);

  if (Status == 0)
  {
    // try again
    Status = SDCardRead(SDHdr, Size, SDCARD_HEADER_BLOCK);
    if (Status == 0)
    {
      WarningsSet(WARN_SDCARD, FM_ALARM, FM_ALARM, NA);
    }
  }
  return(Status);
} // end SDCardHeaderRead()

/***************************************************************************

FUNCTION:   SDCardHeaderUpdate()

PURPOSE:    Write the SD card header to the SD card

COMMENTS:   The SD card semaphore needs to be locked by the callers

***************************************************************************/
int SDCardHeaderUpdate(void)
{
  int Status = 0;
  int ReadRetries = 0;
  int WriteRetries = 0;
  int WriteSuccess = 0;
  SDCARD_HEADER CheckHdr;

  SDCardHeader.CRC = CRC((char*)&SDCardHeader, sizeof(SDCARD_HEADER)-sizeof(unsigned int));

  while (!WriteSuccess && WriteRetries < 3)
  {
    // write the record to the SD card
    Status = SDCardSave((unsigned char *)&SDCardHeader, sizeof(SDCARD_HEADER), SDCARD_HEADER_BLOCK);

    if (Status == 0)
    {
      // check the accuracy of the write
      while (   SDCardRead((char *)&CheckHdr, sizeof(SDCARD_HEADER), SDCARD_HEADER_BLOCK) != 1
             && ReadRetries++ < 3)
      {
        debug_printf("SD card/Header Update read retry\n\r");
      }

      if (memcmp(&CheckHdr, &SDCardHeader, sizeof(SDCARD_HEADER)) != 0)
      {
        debug_printf("SD card/Header Update write retry\n\r");
        WriteRetries++;
      }
      else
      {
//        debug_printf("SD card/Header Update success\n\r");
        WriteSuccess = 1;
        FlashEraseArea(FLASH_SDCARD_HEADER, sizeof(SDCARD_HEADER));
        FlashWrite(FLASH_SDCARD_HEADER, (unsigned char*)&SDCardHeader, sizeof(SDCARD_HEADER));
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
    SDCardHeader.Errors.Header++;
    SDCardHeader.Errors.Write++;
    ActiveSDCard.Initialized = 0;

    if ((SDCardHeader.Errors.Write % 20) == 0)
      WarningsSet(WARN_DATALOGWRITE, FM_ALARM, FM_ALARM, NA);
  }

  return(Status);
} // end SDCardHeaderUpdate()

/***************************************************************************

FUNCTION:   SDCardMonitor()

PURPOSE:    Monitor if the SD card gets removed

COMMENTS:

***************************************************************************/
void SDCardMonitor(void)
{
  if (ActiveSDCard.Initialized == 0)
  {
    SDCard_Init();
    if (ActiveSDCard.Initialized == 1)
    {
      UI_SendUserLogSettings(MSG_SEND);
    }
  }
} // end SDCardMonitor()

/***************************************************************************

FUNCTION:   SDCardRead()

PURPOSE:    Read a record from the SD card

COMMENTS:

***************************************************************************/
int SDCardRead(char *Record, int RecSize, int StartBlock)
{
  int i;
  int bytes;
  int Status = 0;
  unsigned char buffer[SDCARD_BLOCK_SIZE];

  for (i = 0; i < (RecSize/SDCARD_BLOCK_SIZE); i++)
  {
    Status = kfs_read_sector(buffer, StartBlock + i, 1);
    if (Status == KFS_SUCCESS)
    {
      memcpy(Record, buffer, SDCARD_BLOCK_SIZE);
      Record += SDCARD_BLOCK_SIZE;
      Status = 1;
    }
    else
    {
      return Status;
    }
  }

  // read the last partial block
  if (RecSize - (SDCARD_BLOCK_SIZE * i) != 0)
  {
    Status = kfs_read_sector(buffer, StartBlock + i, 1);
    if (Status == KFS_SUCCESS)
    {
      if (RecSize >= (SDCARD_BLOCK_SIZE * i))
      {
        bytes = RecSize - (SDCARD_BLOCK_SIZE * i);
      }
      else
      {
        bytes = (SDCARD_BLOCK_SIZE * i) - RecSize;
      }

      memcpy(Record, buffer, bytes);
      Status = 1;
    }
  }

  return Status;
} // end SDCardRead()

/***************************************************************************

FUNCTION:   SDHeaderReset()

PURPOSE:    Reset the SD card

COMMENTS:   Callers that don't want the header written must obvioulsy write
            the header themselves and therefore should lock the sd card
            semaphore.  Callers wanting the header written should not lock
            the sd card semaphore before this call.

***************************************************************************/
void SDHeaderReset(SDCARD_HEADER *Header, int WriteHeader)
{
  WarningsSet(WARN_SDCARD_INIT, FM_ALARM, FM_ALARM, NA);

  Header->ID = ActiveSDCard.ID;
  Header->Size = ActiveSDCard.Size;

  PIDLogReset(&Header->PIDLog, 0);    // PID log is stored after the system log
  SystemLogReset(&Header->SysLog, 0); // systen log is stored after the load log
  LoadLogReset(Header, 0);            // load log is stored after the user log
  UserLogReset(Header, 0);            // user log is stored at the beginning and takes up the majority of the card

  Header->Errors.SystemLog = 0;
  Header->Errors.UserLog = 0;
  Header->Errors.Header = 0;
  Header->Errors.Write = 0;
  Header->CardFull = 0;

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
        debug_printf("SDHeaderReset blocked at SDCardHeaderUpdate\r\n");
      }
    }
    else
    {
      ActiveSDCard.Initialized = 0;
    }
  }
} // end SDHeaderReset()

/***************************************************************************

FUNCTION:   SDCardSave()

PURPOSE:    Write a record to the SD card

COMMENTS:

***************************************************************************/
int SDCardSave(unsigned char *Record, int RecSize, int StartBlock)
{
  int Status = 1;
  int blocks = RecSize/SDCARD_BLOCK_SIZE;

  if (ActiveSDCard.Initialized == 1)
  {
    if ((blocks * SDCARD_BLOCK_SIZE) < RecSize)
    {
      blocks++;
    }

    if (kfs_write_sector((const unsigned char *) Record, StartBlock, blocks) == KFS_SUCCESS)
    {
      Status = 0;
    }
  }

  return Status;
} // end SDCardSave()

/***   End Of File   ***/
