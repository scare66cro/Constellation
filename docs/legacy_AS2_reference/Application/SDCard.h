/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     SDCard.h

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:

COMMENTS:

***************************************************************************/

#ifndef SDCARD_H
#define SDCARD_H

/*** include files ***/

#include "Settings.h"

/*** defines ***/

#define SDCARD_VERSION              "3.17"   // NOTE: only rev this when SDCARD_HEADER length changes
#define SDCARD_DATA_TOKEN           0xFE
#define SDCARD_USERLOG_STARTBLOCK   5

#define SDCARD_PIDLOG_MAXBLOCKS     6200
#define SDCARD_PIDLOG_RECSPERBLOCK  14

#define SDCARD_LOADLOG_MAXBLOCKS    313   // 5008 records
#define SDCARD_LOADLOG_RECSPERBLOCK 16

#define SDCARD_HEADER_BLOCK         0
#define SDCARD_IDLE                 1
#define SDCARD_READY                0
#define SDCARD_BUSY                 0
#define SDCARD_IS                   1
#define SDCARD_IS_NOT               0
#define SDCARD_DATE_OFFSET          4
#define SDCARD_RESPONSE_TIMEOUT     0xFF000
#define SDCARD_RETRIES              10
#define SDCARD_BLOCK_SIZE           512
#define SDCARD_WRITE_PROTECTED      (0x1<<5)

/*** typedefs and structures ***/

typedef struct {
  char             Present;
  char             Initialized;
  unsigned int     ID;
  unsigned int     Size;
  char             Name[6];
  int              Date;
  portTickType     BlockTimeout;
  xSemaphoreHandle Semaphore;
} ACTIVE_SDCARD;

typedef struct {
  unsigned int StartBlock;
  unsigned int WriteBlock;
  unsigned int NumRecords;
  unsigned int RecordLen;
  unsigned int BlocksPerRecord;
  unsigned int RecordsPerCard;
  char         DataStartDate[DATE_LEN+1];
} USERLOG_HEADER;

typedef struct {
  unsigned int StartBlock;
  unsigned int WriteBlock;
  unsigned int NumRecords;
  unsigned int RecordLen;
  unsigned int RecordsPerBlock;
  unsigned int RecordsPerCard;
} LOADLOG_HEADER;

typedef struct {
  unsigned int StartBlock;
  unsigned int WriteBlock;
  unsigned int NumRecords;
  unsigned int RecordLen;
  unsigned int BlocksPerRecord;
  unsigned int MaxBlocks;
} SYSLOG_HEADER;

typedef struct {
  unsigned int StartBlock;
  unsigned int WriteBlock;
  unsigned int NumRecords;
} PIDLOG_HEADER;

typedef struct {
  unsigned int SystemLog;
  unsigned int UserLog;
  unsigned int Header;
  unsigned int Write;
} SD_ERRORS;

typedef struct {
  char         Version[VERSION_LEN+1];
  unsigned int ID;
  unsigned int Size;
  char         CardFull;

  USERLOG_HEADER UserLog;
  SYSLOG_HEADER SysLog;
  PIDLOG_HEADER PIDLog;
  LOADLOG_HEADER LoadLog;
  SD_ERRORS Errors;

  unsigned int CRC;
} SDCARD_HEADER;

// previous version
typedef struct {
  unsigned int ID;
  unsigned int Size;

  USERLOG_HEADER UserLog;
  SYSLOG_HEADER SysLog;
  PIDLOG_HEADER PIDLog;
  SD_ERRORS Errors;

  char         CardFull;

  unsigned int CRC;
} SDCARD_HEADER_V1;

/*** external variables ***/

extern ACTIVE_SDCARD ActiveSDCard;
extern SDCARD_HEADER SDCardHeader;

/*** external functions ***/

extern void SDCard_Init(void);
extern int SDCardHeaderUpdate(void);
extern void SDCardMonitor(void);
extern int SDCardRead(char *Record, int RecSize, int StartBlock);
extern void SDHeaderReset(SDCARD_HEADER *Header, int WriteHeader);
extern int SDCardSave(unsigned char *Record, int RecSize, int StartBlock);

#endif

/***   End Of File   ***/
