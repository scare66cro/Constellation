/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     ThreadMonitor.h

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:

COMMENTS:

***************************************************************************/

#ifndef THREADMONITOR_H
#define THREADMONITOR_H

/*** include files ***/

/*** defines ***/

#define THREADMONITOR_PRIORITY    4
#define THREADMONITOR_STACK_SIZE  512

#define TM_UI_UPDATE    (1<<0)
#define TM_SERIAL_SHIFT (1<<1)
#define TM_SERIAL_COM   (1<<2)

/*** typedefs and structures ***/

/*** external variables ***/

/*** external functions ***/

extern void ThreadMonitor(void);
extern void ThreadMonitorUpdate(unsigned int thread);

#endif

/***   End Of File   ***/
