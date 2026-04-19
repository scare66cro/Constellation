/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     ThreadSerialShift.h

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:

COMMENTS:

***************************************************************************/

#ifndef THREADSERIALSHIFT_H
#define THREADSERIALSHIFT_H

/*** include files ***/

#include "semphr.h"

/*** defines ***/

#define THREADSERIALSHIFT_PRIORITY    4
#define THREADSERIALSHIFT_STACK_SIZE  512

/*** typedefs and structures ***/

typedef struct
{
  portTickType     BlockTimeout;
  xSemaphoreHandle Semaphore;
} THREAD_SERIAL_SHIFT_INFO;

/*** external variables ***/

extern THREAD_SERIAL_SHIFT_INFO SerialShift;

/*** external functions ***/

extern void ThreadSerialShift(void);
extern unsigned int ReadInput(_pin_str CS, unsigned int inputs);

#endif

/***   End Of File   ***/
