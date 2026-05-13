/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     ThreadSerialCom.c

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:  Thread for serial communication with web server

COMMENTS:

***************************************************************************/

/*** include files ***/

// FreeRTOS
#include "FreeRTOS.h"
#include "task.h"

// Platform
#include "system.h"

// Gellert
#include "Analog_Input.h"
#include "DataExc.h"
#include "RTC.h"
#include "SDCard.h"
#include "Settings.h"
#include "States.h"
#include "ThreadMonitor.h"
#include "ThreadSerialCom.h"
#include "ThreadSerialShift.h"
#include "Timer.h"
#include "Usart.h"

/*** defines ***/

/*** typedefs and structures ***/

/*** module variables ***/

/*** static functions ***/

/***************************************************************************

FUNCTION:   ThreadSerialCom()

PURPOSE:    Thread monitor

COMMENTS:

***************************************************************************/
void ThreadSerialCom(void)
{
  DiscoverAnalogBoardsRequest(AB_DISCOVER);

  // read the sensors & set the system state & mode
  while(1)
  {
    if (SaveSettingsRequest.Semaphore != NULL)
    {
      // Obtain the semaphore - block if the semaphore is not immediately available.
      if (xSemaphoreTake(SaveSettingsRequest.Semaphore, SaveSettingsRequest.BlockTimeout) == pdTRUE)
      {
        if (DiscoverAnalogBoards() == AB_DISCOVER)
        {
          FindAnalogBoards();
          ReadAnalogBoards(RT_ALL);
          DiscoverAnalogBoardsRequest(AB_REPORT);
        }
        else
        {
          ReadAnalogBoards(RT_DEFAULT);
          ReadAnalogBoards(RT_GROUPS);
        }

        if (SerialShift.Semaphore != NULL)
        {
          // Obtain the semaphore - block if the semaphore is not immediately available.
          if (xSemaphoreTake(SerialShift.Semaphore, SerialShift.BlockTimeout) == pdTRUE)
          {
            SetSystemState();   // reads the serial shift register to set the state
            SetMode();
            FanRunTimer();

            // Release the semaphore.
            xSemaphoreGive(SerialShift.Semaphore);
          }
          else
          {
            debug_printf("ThreadSerialCom blocked for SerialShift\r\n");
          }
        }

        // Release the semaphore.
        xSemaphoreGive(SaveSettingsRequest.Semaphore);
      }
      else
      {
        debug_printf("ReadAnalogBoards blocked\r\n");
      }
    }

    WatchdogInternalReset();
    ThreadMonitorUpdate(TM_SERIAL_COM);
    vTaskDelay(1000/portTICK_RATE_MS);
  }
} // end ThreadSerialCom()

/***   End Of File   ***/
