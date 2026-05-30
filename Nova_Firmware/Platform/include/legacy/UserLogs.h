/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     UserLogs.h

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:

COMMENTS:

***************************************************************************/

#ifndef USERLOGS_H
#define USERLOGS_H

/*** include files ***/

#include "SDCard.h"
#include "SystemLogs.h"

/*** defines ***/

/*** typedefs and structures ***/

typedef struct {
  unsigned int RecordNum;
  char         Date[DATE_LEN+1];
  char         Time[TIME_LEN+1];
  short        MainPage[LOG_MAINITEMS];
  short        Sensors[ANALOG_BOARDS_PER_SYSTEM * ANALOG_SENSORS_PER_BOARD];
} USERLOG_RECORD;

/*** external variables ***/

extern char SendingLogData;

/*** external functions ***/

extern int DateToInt(char *DateStr);

extern void UserLogReset(SDCARD_HEADER *Header, int WriteHeader);
extern int UserLogSendToFile(char *BeginDate, char *EndDate, int SessionID);
extern int UserLogSendToUI(char *BeginDate, char *EndDate, int Mode, int SessionID);
extern int UserLogWrite(void);

#endif

/***   End Of File   ***/
