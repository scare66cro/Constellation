/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     LoadLogs.h

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:

COMMENTS:

***************************************************************************/

#ifndef LOADLOGS_H
#define LOADLOGS_H

/*** include files ***/

#include "SDCard.h"
#include "SystemLogs.h"

/*** defines ***/

/*** typedefs and structures ***/

typedef enum
{
  LL_ACQUIRING = 0,
  LL_PAUSED    = 1,
  LL_FULL      = 2,
} LL_STATUS;

typedef struct {
  int   Count;
  float Sum;
} LOADLOG_AVERAGES;

typedef struct {
  char         Pipe;
  short        Sensor;
} LOADLOG_BAYTEMP;

typedef struct {
  unsigned int    RecordNum;
  char            Date[DATE_LEN+1];
  char            Time[TIME_LEN+1];
  LOADLOG_BAYTEMP Bay[NUM_LOADLOG_SENSORS];
} LOADLOG_RECORD;

/*** external variables ***/

/*** external functions ***/

extern void LoadLogIndexReset(void);
extern void LoadLogReset(SDCARD_HEADER *Header, int WriteHeader);
extern int LoadLogSendToUI(int Bay, int Mode, int SessionID);
extern void LoadLogTempAccumulator(int SensorID, float Temp);
extern void LoadLogTempAccumulatorReset(int Bay);
extern unsigned int LoadLogUnwrittenRecords(void);
extern int LoadLogWrite(char Force);

#endif

/***   End Of File   ***/
