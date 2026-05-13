//*****************************************************************************
//
// freertos_demo.c - Simple FreeRTOS example.
//
// Copyright (c) 2009-2014 Texas Instruments Incorporated.  All rights reserved.
// Software License Agreement
//
// Texas Instruments (TI) is supplying this software for use solely and
// exclusively on TI's microcontroller products. The software is owned by
// TI and/or its suppliers, and is protected under applicable copyright
// laws. You may not combine this software with "viral" open-source
// software in order to form a larger program.
//
// THIS SOFTWARE IS PROVIDED "AS IS" AND WITH ALL FAULTS.
// NO WARRANTIES, WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING, BUT
// NOT LIMITED TO, IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE APPLY TO THIS SOFTWARE. TI SHALL NOT, UNDER ANY
// CIRCUMSTANCES, BE LIABLE FOR SPECIAL, INCIDENTAL, OR CONSEQUENTIAL
// DAMAGES, FOR ANY REASON WHATSOEVER.
//
// This is part of revision 2.1.0.12573 of the DK-TM4C129X Firmware Package.
//
//*****************************************************************************

/*** include files ***/

// Platform
#include <stdint.h>
#include <stdbool.h>

#include "driverlib/sysctl.h"
#include "drivers/pinout.h"
#include "driverlib/interrupt.h"

#include "debug.h"
#include "flash.h"
#include "system.h"

#include "KFS/kfs.h"

// freeRTOS
#include "FreeRTOS.h"
#include "task.h"

// Gellert
#include "Controls.h"
#include "PIDLogs.h"
#include "PWM.h"
#include "RS485.h"
#include "RTC.h"
#include "SDCard.h"
#include "SerialShift.h"
#include "Settings.h"
#include "StorePostData.h"
#include "SystemLogs.h"
#include "ThreadFileReceive.h"
#include "ThreadMonitor.h"
#include "ThreadSerialCom.h"
#include "ThreadSerialShift.h"
#include "ThreadUIUpdate.h"
#include "Timer.h"
#include "UI_Messages.h"
#include "Usart.h"

/*** defines ***/

/*** typedefs and structures ***/

/*** module variables ***/

unsigned int system_clock_speed;
unsigned int uptime_sec = 0;
unsigned int uptime_ms = 0;
unsigned int reset_cause = 0;

/*** static functions ***/

static void monitor_thread(void *pvParameters);
static void UI_update_thread(void *pvParameters);
static void system_control_thread(void *pvParameters);
static void serial_shift_thread(void *pvParameters);
static void file_receive_thread(void *pvParameters);
static void app_main_idle_hook(void);

/***************************************************************************

FUNCTION:   main()

PURPOSE:    Initialize FreeRTOS and start the initial set of tasks.

COMMENTS:

***************************************************************************/
int main(void)
{
  // Run from the PLL at 120 MHz.
  system_clock_speed = SysCtlClockFreqSet((SYSCTL_XTAL_25MHZ | SYSCTL_OSC_MAIN | SYSCTL_USE_PLL | SYSCTL_CFG_VCO_480), 120000000);

  reset_cause=SysCtlResetCauseGet();
  SysCtlResetCauseClear(reset_cause);

  // Initialize the device pinout appropriately for this board.
  PinoutSet();

  IntMasterEnable();
  UARTStdioConfig(0, 115200, system_clock_speed);  // Configure UART.

  // Clear the terminal and print banner.
  debug_printf("\033[2J\033[H");
  debug_printf("AS2: v%s\r\n", ARM_FIRMWARE_VERSION);
  if (reset_cause&SYSCTL_CAUSE_HSRVREQ) debug_printf("Hardware System Service Request, ");
  if (reset_cause&SYSCTL_CAUSE_HIB)     debug_printf("Hibernate reset, ");
  if (reset_cause&SYSCTL_CAUSE_WDOG1)   debug_printf("Watchdog 1 reset, ");
  if (reset_cause&SYSCTL_CAUSE_SW)      debug_printf("Software reset, ");
  if (reset_cause&SYSCTL_CAUSE_WDOG0)   debug_printf("Watchdog 0 reset, ");
  if (reset_cause&SYSCTL_CAUSE_BOR)     debug_printf("Brown-out reset, ");
  if (reset_cause&SYSCTL_CAUSE_POR)     debug_printf("Power on reset, ");
  if (reset_cause&SYSCTL_CAUSE_EXT)     debug_printf("External reset, ");
  debug_printf("\r\n");

  // Make sure the main oscillator is enabled because this is required by the PHY.
  // The system must have a 25MHz crystal attached to the OSC pins.
  // The SYSCTL_MOSC_HIGHFREQ parameter is used when the crystal frequency is 10MHz or higher.
  SysCtlMOSCConfigSet(SYSCTL_MOSC_HIGHFREQ);

  CreateSPILocks();
  FlashInit();

  Usart_Init();             // serial communications (Lantronix/UI)
  RS485_Init();             // serial communications (analog boards)
  SerialShiftTimer_Init();  // serial shift register timer
  RTC_Init();               // set up the internal RTC
  WatchdogInternal_Init();  // set up the Tiva watchdog timer.
  WatchdogExternal_Init();  // set up the GPIO that's monitored by the CPLD
  PWM_Init();               // pulse width modulation (fan, doors, refrig)
  UI_PostMsg_Init();        // setup the post tags
  QueryTags_Init();         // query tags for data logs
  TempTable_Init();         // temperature lookup table
  Settings_Init();          // read in the system settings (convert to new size if necessary)
  SerialShift_Init();       // shift registers for input/output
  SystemTime_Init();        // if the system clock doesn't have a valid time, set one
  SystemLogLabel_Init();    // setup activity log labels
  PID_Init();               // initializes the PID control values with Settings data
  CtrlDoorsPulsed_Init();   // close the doors

  FileReceive.Status = FR_IDLE;

  // start the threads
  debug_printf("Starting Serial Shift Thread\r\n");
  if (xTaskCreate(serial_shift_thread, (signed portCHAR *)"SERIALSHIFT", THREADSERIALSHIFT_STACK_SIZE, NULL, tskIDLE_PRIORITY + THREADSERIALSHIFT_PRIORITY, NULL) != pdTRUE)
  {
    debug_printf("!!!Failed to start serial_shift_thread!!!\r\n");
    return 1;
  }

  debug_printf("Starting System Control Thread\r\n");
  if (xTaskCreate(system_control_thread, (signed portCHAR *)"SYSTEM_CTRL", THREADSERIALCOM_STACK_SIZE, NULL, tskIDLE_PRIORITY + THREADSERIALCOM_PRIORITY, NULL) != pdTRUE)
  {
    debug_printf("!!!Failed to start system_control_thread!!!\r\n");
    return 1;
  }

  debug_printf("Starting UI Update Thread\r\n");
  if (xTaskCreate(UI_update_thread, (signed portCHAR *)"UI_UPDATE", THREADUIUPDATE_STACK_SIZE, NULL, tskIDLE_PRIORITY + THREADUIUPDATE_PRIORITY, NULL) != pdTRUE)
  {
    debug_printf("!!!Failed to start UI_update_thread!!!\r\n");
    return 1;
  }

  debug_printf("Starting Thread Monitor\r\n");
  if (xTaskCreate(monitor_thread, (signed portCHAR *)"TASKMONITOR", THREADMONITOR_STACK_SIZE, NULL, tskIDLE_PRIORITY + THREADMONITOR_PRIORITY, NULL) != pdTRUE)
  {
    debug_printf("!!!Failed to start monitor_thread!!!\r\n");
    return 1;
  }

  debug_printf("Starting File Receive Thread\r\n");
  if (xTaskCreate(file_receive_thread, (signed portCHAR *)"FILERECEIVE", THREADFILERECEIVE_STACK_SIZE, NULL, tskIDLE_PRIORITY + THREADFILERECEIVE_PRIORITY, NULL) != pdTRUE)
  {
    debug_printf("!!!Failed to start file_receive_thread!!!\r\n");
    return 1;
  }

  debug_printf("Starting Scheduler\r\n");
  vTaskStartScheduler();  // this should not return.

  while(1)
  {
  }
} // end main()

/***************************************************************************

FUNCTION:   monitor_thread()

PURPOSE:    Call the thread monitor

COMMENTS:

***************************************************************************/
static void monitor_thread(void *pvParameters)
{
  ThreadMonitor();

  vTaskDelete(NULL);
} // monitor_thread()

/***************************************************************************

FUNCTION:   UI_update_thread()

PURPOSE:

COMMENTS:

***************************************************************************/
static void UI_update_thread(void *pvParameters)
{
  ThreadUIUpdate();

  vTaskDelete(NULL);
} // UI_update_thread()

/***************************************************************************

FUNCTION:   serial_shift_thread()

PURPOSE:

COMMENTS:

***************************************************************************/
static void serial_shift_thread(void *pvParameters)
{
  ThreadSerialShift();

  vTaskDelete(NULL);
} // serial_shift_thread()

/***************************************************************************

FUNCTION:   system_control_thread()

PURPOSE:

COMMENTS:

***************************************************************************/
static void system_control_thread(void *pvParameters)
{
  ThreadSerialCom();

  vTaskDelete(NULL);
} // system_control_thread()

/***************************************************************************

FUNCTION:   file_receive_thread()

PURPOSE:

COMMENTS:

***************************************************************************/
static void file_receive_thread(void *pvParameters)
{
  ThreadFileReceive();

  vTaskDelete(NULL);
} // file_receive_thread()

//*****************************************************************************
//
// This hook is called by FreeRTOS when an stack overflow error is detected.
//
//*****************************************************************************
void vApplicationStackOverflowHook(xTaskHandle *pxTask, signed char *pcTaskName)
{
  //
  // This function can not return, so loop forever.  Interrupts are disabled
  // on entry to this function, so no processor interrupts will interrupt
  // this loop.
  //
  debug_printf("StackOverflow: %s\r\n", pcTaskName);

  while(1)
  {
  }
}

//*****************************************************************************
//
// This hook is called by the FreeRTOS idle task when no other tasks are runnable.
//
//*****************************************************************************
void vApplicationIdleHook(void)
{
//  unsigned int addr;
//  static unsigned long twirl_index = 0;
  static unsigned int last_sec=0;

  if (last_sec!=uptime_sec)
  {
    last_sec=uptime_sec;

    // Get the current IP address.
//    addr = lwIPLocalIPAddrGet();
//
//    if ((addr==0)||(addr==0xFFFFFFFF))
//    {
//      debug_printf("\b%c", twirl[twirl_index]);
//      twirl_index = (twirl_index+1)&3;
//    }
//    else if ((g_bLinkActive==1)&&(net_up!=1))
//    {
//      net_up=1;
//      app_main_network_up();
//    }
//    if ((g_bLinkActive==0)&&(net_up!=0))
//    {
//      net_up=0;
//      app_main_network_down();
//    }
  }

  debug_periodic();

  app_main_idle_hook();
}

//*****************************************************************************
//
// This function operates OUTSIDE of the RTOS threads, it's raw hardware
//
//*****************************************************************************
static void app_main_idle_hook(void)
{
  static unsigned int last_sec=0;

  if (last_sec!=uptime_sec)
  {
    last_sec=uptime_sec;

    if (!(uptime_sec % 10))
      debug_printf("uptime: %d\r\n", uptime_sec);
  }

//  if ((reset_sec>0)&&(uptime_sec==reset_sec))
//  {
//    debug_printf("Resetting...\r\n");
//    SysCtlReset();
//  }

//  debug_periodic();
}

//*****************************************************************************
//
// The error routine that is called if the driver library encounters an error.
//
//*****************************************************************************
void __error__(char *pcFilename, uint32_t ui32Line)
{
  debug_printf("ERROR!!!!!\r\n");
  while(1){}
}

/***   End Of File   ***/
