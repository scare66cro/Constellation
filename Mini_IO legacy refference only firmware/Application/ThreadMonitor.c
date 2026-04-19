/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     ThreadUIUpdate.c

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:  Thread to monitor the other threads

COMMENTS:

***************************************************************************/

/*** include files ***/

// FreeRTOS
#include "FreeRTOS.h"
#include "task.h"

// Platform
#include "system.h"

// Gellert
#include "ThreadMonitor.h"
#include "Timer.h"

/*** defines ***/

/*** typedefs and structures ***/

/*** module variables ***/

static unsigned int ThreadFlags = 0;

/*** static functions ***/

/***************************************************************************

FUNCTION:   ThreadMonitor()

PURPOSE:    Thread monitor

COMMENTS:

***************************************************************************/
void ThreadMonitor(void)
{
//  char taskInfo[400];

  // start the monitor loop
  while(1)
  {
    vTaskDelay(2000/portTICK_RATE_MS);

//    vTaskList(taskInfo);
//    debug_printf(taskInfo);
//    debug_printf("\r\n");

    //    debug_printf("ThreadFlags:%u\r\n", ThreadFlags);

    if (ThreadFlags)
    {
      debug_printf("ThreadMonitor FAILURE:%u\r\n", ThreadFlags);
      WatchdogExternalStop();
    }
    else
    {
//      debug_printf("ThreadMonitor Restart:%u\r\n", ThreadFlags);
      WatchdogExternalStart();
    }

    ThreadFlags = (TM_UI_UPDATE | TM_SERIAL_SHIFT | TM_SERIAL_COM);
//    debug_printf("ThreadFlags:%u\r\n", ThreadFlags);
  }
} // end ThreadMonitor()

/***************************************************************************

FUNCTION:   ThreadMonitorUpdate()

PURPOSE:    Clear the appropriate bit in the thread monitor

COMMENTS:

***************************************************************************/
void ThreadMonitorUpdate(unsigned int thread)
{
  ThreadFlags &= ~(thread);
} // ThreadMonitorUpdate()

/***   End Of File   ***/
