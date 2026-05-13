/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     ThreadUIUpdate.c

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:  Thread to update the UI

COMMENTS:

***************************************************************************/

/*** include files ***/

#include <string.h>

// FreeRTOS
#include "FreeRTOS.h"
#include "task.h"

// Platform
#include "system.h"

// Gellert
#include "Controls.h"
#include "DataExc.h"
#include "LoadLogs.h"
#include "PIDLogs.h"
#include "RTC.h"
#include "Settings.h"
#include "StorePostData.h"
#include "SystemLogs.h"
#include "ThreadFileReceive.h"
#include "ThreadMonitor.h"
#include "ThreadUIUpdate.h"
#include "Timer.h"
#include "UI_Messages.h"
#include "Usart.h"
#include "UserLogs.h"
#include "Warnings.h"

/*** defines ***/

/*** typedefs and structures ***/

/*** module variables ***/

 /*** static functions ***/

/***************************************************************************

FUNCTION:   ThreadUIUpdate()

PURPOSE:    Thread to update the UI

COMMENTS:

***************************************************************************/

void ThreadUIUpdate(void)
{
  /*** test code ***/
//  unsigned int i;
//  i = sizeof(SYSTEM_SETTINGS);      // 15360
//  i = sizeof(USERLOG_RECORD);       // 304
//  i = sizeof(SYSLOG_RECORD);        // 1564
//  i = sizeof(UI_MESSAGE_QUEUE);     // 236
//  i = sizeof(PIDLOG_RECORD);        // 36
//  i = sizeof(SDCARD_HEADER);        // 152
//  i = sizeof(int);        // 4
//  i = sizeof(float);      // 4
//  char testMe[128];
//  i = sizeof(testMe);
  /*** end test code ***/

  unsigned int UpTime = 0;

  SaveSettingsRequest.BlockTimeout = 5 / portTICK_RATE_MS;
  vSemaphoreCreateBinary(SaveSettingsRequest.Semaphore);

  SDCard_Init();            // checks if SD Card is accessible and if it's the same one

  // UI messaging initialization
  UI_Message.State = MSG_STATE_WAITING;
#ifdef QEMU_BUILD
  MessagingStatus = MS_RESPONDING;
  LtxInitialized = 1;
#else
  MessagingStatus = MS_NOT_INITIALIZED;
#endif

  // initialize the timers/counters
  MainTimer_Init();

  // clear any active CO2 purge cycles
  Settings.Co2.Purge.Start = 0;
  Settings.Co2.Purge.Last = XTimerVal;

  // check that DST didn't stop or start while we were shutdown
  DaylightSavingCheck(1, 1);    // (force the check, adjust the clock if necessary)

  // start the control loop
  while(1)
  {
    if (FileReceive.Status == FR_IDLE)
    {
      UpTime = XTimer();

      if (UpTime - MainTimer[MT_SYSTEMSTATE] >= 1 * T_SECS)
      {
        UpdateSystemTime();
        MainTimer[MT_SYSUPTIME] = UpTime;
        MainTimer[MT_SYSTEMSTATE] = UpTime;
        SDCardMonitor();

        if (DiscoverAnalogBoards() == AB_REPORT)
        {
          UI_SendSensorLabels();
          UI_SendSensorData();
          DiscoverAnalogBoardsRequest(AB_CLEAR);
          debug_printf("SendAnalogBoards\r\n");
        }

        ThreadMonitorUpdate(TM_UI_UPDATE);
      }

      // update the system monitor page
      if (UpTime - MainTimer[MT_READSENSORS] >= 3 * T_SECS)
      {
        if (DiscoverAnalogBoards() == AB_CLEAR)
        {
          UI_SendMain(MSG_QUEUE);
          UI_SendMode();
        }

        UI_SendDateTime(MSG_QUEUE);
        UI_SendHumidModes();
        UI_SendEquipStatus(MSG_QUEUE);
        UI_SendAuxSwitches(MSG_QUEUE);
        UI_SendPlenSetPoints(MSG_QUEUE);
        MainTimer[MT_READSENSORS] = UpTime;
      }

      // send the sensor data
      if (UpTime - MainTimer[MT_SENDSENSORS] >= 7 * T_SECS)
      {
        UI_SendSensorData();
        UI_SendWarnings(0);
        MainTimer[MT_SENDSENSORS] = UpTime;

        // reset the LTX watchdog timer
  //      if (MessagingStatus != MS_NOT_RESPONDING)
  //        IntervalTimer[IT_LTXWATCHDOG] = XTimerVal;
  //
  //      // reset the LTX if the watchdog timer expires
  //      if (XTimerVal - IntervalTimer[IT_LTXWATCHDOG] >= 180 * T_SECS)
  //      {
  //        ResetLTX();
  //        DBGU_SendString("LTX watchdog timeout - LTX reset from ARM");
  //        IntervalTimer[IT_LTXWATCHDOG] = XTimerVal;
  //      }
      }

      // send full update of UI
      if (   UpTime - MainTimer[MT_UI_ALL] >= 50 * T_SECS
          || LtxInitialized == 1)
      {
        UI_SendAllSettings();
        UI_SendSensorLabels();

        if (LtxInitialized == 1)
        {
          UI_SendHttpPort(MSG_QUEUE);
          UI_SendNetMonMode(MSG_QUEUE);
          UI_SendAnalogBoard(0, 1, MSG_QUEUE);   // just send the first one, the UI will ask for then next one when it needs it
          UI_SendAuxProgram(EQ_AUX1, 1, MSG_QUEUE);   // just send the first one, the UI will ask for then next one when it needs it
          UI_SendPgmLevel(MSG_QUEUE);
          LtxInitialized = 0;
        }

        DailyJobs();
        DaylightSavingCheck(0, 1);    // (don't force the check, adjust the clock if necessary)
        MainTimer[MT_UI_ALL] = UpTime;

  //      if (MessagingStatus == MS_RESPONDING)
  //        debug_printf("%s\n\r","LTX Responding");
  //      else
  //        debug_printf("%s\n\r","LTX NOT Responding");
      }

      // save the system settings to flash
      if (   (   SaveSettingsRequest.Status == SR_REQUEST
              && UpTime - MainTimer[MT_SYSTEMSETTINGS] >= 2 * T_SECS)
          || UpTime - MainTimer[MT_SYSTEMSETTINGS] >= 60 * T_MINS)
      {
        if (SaveSettingsRequest.Semaphore != NULL)
        {
          // Obtain the semaphore - don't block if the semaphore is not immediately available.
          if (xSemaphoreTake(SaveSettingsRequest.Semaphore, SaveSettingsRequest.BlockTimeout) == pdTRUE)
          {
            SaveSettings(SaveSettingsRequest.Type);
            MainTimer[MT_SYSTEMSETTINGS] = UpTime;

            // Release the semaphore.
            xSemaphoreGive(SaveSettingsRequest.Semaphore);
          }
          else
          {
            debug_printf("SaveSettings blocked for ReadAnalogBoards\r\n");
          }
        }
      }

      // write the user data log (history log)
      if (   Settings.Log.User.Interval != 0
          && UpTime - MainTimer[MT_USERLOG] >= Settings.Log.User.Interval * T_MINS)
      {
        UserLogWrite();
        MainTimer[MT_USERLOG] = UpTime;
      }

      // write the system activity log
      if (UpTime - MainTimer[MT_SYSTEMLOG] >= 1 * T_MINS)
      {
        SystemLogWrite(0);
        MainTimer[MT_SYSTEMLOG] = UpTime;
      }

      // write the loading monitor log
      if (UpTime - MainTimer[MT_LOADLOG] >= 5 * T_MINS)
      {
        LoadLogWrite(0);
        MainTimer[MT_LOADLOG] = UpTime;
      }

      // send time/temp/humid update to slaves
      if (   Settings.MasterSlave == MSMODE_MASTER
          && UpTime - MainTimer[MT_SLAVEUPDATE] >= 30 * T_SECS)
      {
        UI_SendSlaveUpdate(MSG_QUEUE);
        MainTimer[MT_SLAVEUPDATE] = UpTime;
      }

      if (FileReceive.Status == FR_IDLE)
      {
        // send message to UI if needed
        ProcessMsgQueue();

        // check for message from UI
        if (Usart_CharsBuffered())
        {
          if (Usart_ProcessBuffer(UI_Message.RxBuffer) == 1)
          {
            ProcessUIMessage();
          }
        }
      }
    }
    else
    {
      if (FileReceive.Status == FR_PENDING)
      {
        FreeMsgQueue();
        FileReceive.Status = FR_RECEIVING;
        debug_printf("FileReceive.Status = FR_RECEIVING\r\n");
      }

      ThreadMonitorUpdate(TM_UI_UPDATE);
      vTaskDelay(1000/portTICK_RATE_MS);
    }

    vTaskDelay(10/portTICK_RATE_MS);
  }
} // ThreadUIUpdate()

/***   End Of File   ***/
