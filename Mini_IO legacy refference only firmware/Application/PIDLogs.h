/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     PIDLogs.h

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:

COMMENTS:

***************************************************************************/

#ifndef PIDLOGS_H
#define PIDLOGS_H

/*** include files ***/

#include "SDCard.h"
#include "Settings.h"

/*** defines ***/

#define NUM_PIDLOG_ITEMS      6

/*** typedefs and structures ***/

typedef struct {
  char Label[LOG_LABELS];
} PIDLOG_LABELS;

typedef struct {
  unsigned int RecordNum;
  char         Date[DATE_LEN+1];
  char         Time[TIME_LEN+1];
  char         Type;
  char         Output;
  short        P;
  short        I;
  short        D;
  short        Error;
} PIDLOG_RECORD;

/*** external variables ***/

extern PIDLOG_LABELS PIDLogLabels[NUM_PIDLOG_ITEMS];

/*** external functions ***/

extern void PIDLogReset(PIDLOG_HEADER *Header, int WriteHeader);
extern int PIDLogSendToFile(int BeginRecord, int EndRecord, char Type, int SessionID);
extern int PIDLogSendToUI(int BeginRecord, int EndRecord, char Type, int SessionID);
extern int PIDLogWrite(char Type, float P, float I, float D, int Output, float Error);

#endif

/***   End Of File   ***/
