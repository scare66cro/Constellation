/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     Timer.c

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:  Timer functions

COMMENTS:

***************************************************************************/

/*** include files ***/

#include <string.h>
#include <stdio.h>

// Platform
#include <stdbool.h>
#include <stdint.h>

#include "pinout.h"
#include "system.h"
#include "driverlib/interrupt.h"
#include "driverlib/watchdog.h"

// Gellert
#include "Controls.h"
#include "PWM.h"
#include "RTC.h"
#include "SerialShift.h"
#include "Timer.h"

/*** defines ***/

/*** typedefs and structures ***/

/*** module variables ***/

unsigned char SystemAlarm[NUM_ALARMS];
unsigned int AlarmTimer[NUM_ALARMS];
unsigned int IntervalTimer[IT_NUM_TIMERS];
unsigned int MainTimer[MT_NUM_TIMERS];

unsigned int XTimerVal = 0;

static int LightCount = 0;
static int PulseCount = 0;

static char DailyReset = 0;

static unsigned int WatchdogExternalOutput = 0;

LIGHT_STATUS LightStatus;

/*** static functions ***/

static void SerialShiftTimerISR(void);
static void WatchdogExternalISR(void);

/***************************************************************************

FUNCTION: ClearAlarms()

PURPOSE:  Clear the alarms

COMMENTS: A software reset doesn't clear the AL_AUXLOWPLENTEMP, only a
          hardware reset will.  Gellert wants it to require a site visit.

***************************************************************************/
void ClearAlarms(char soft)
{
  int i;
  for (i = 0; i < NUM_ALARMS; i++)
  {
    if (soft == 1 && i == AL_AUXLOWPLENTEMP)
    {
      continue;
    }

    SystemAlarm[i] = 0;
    AlarmTimer[i] = 0;
  }

  AlarmTimer[AL_NOBROADCAST] = XTimerVal;
} // end ClearAlarms()

/***************************************************************************

FUNCTION: ClearIntervalTimers()

PURPOSE:  Clear the interval timers

COMMENTS:

***************************************************************************/
void ClearIntervalTimers(void)
{
  int i;
  for (i = 0; i < IT_NUM_TIMERS; i++)
  {
    if (i != IT_SHUTDOWN)
    {
      IntervalTimer[i] = 0;
    }
  }
} // end ClearIntervalTimers()

/***************************************************************************

FUNCTION:   DailyJobs()

PURPOSE:    Run daily jobs

COMMENTS:

***************************************************************************/
void DailyJobs(void)
{
  char Hour = 255;
  char Min = 255;
  char Sec = 255;

  GetTime(&Hour, &Min, &Sec);
  if (   Hour == 2
      && Min == 0
      && DailyReset == 0)
  {
    CtrlDoorsPulsed_Init();

    // so it doesn't reset multiple times during the :00 minute
    DailyReset = 1;
  }
  else
  {
    DailyReset = 0;
  }
} // end DailyJobs()

/***************************************************************************

FUNCTION:   MainTimer_Init()

PURPOSE:    Initialize the event timers used in the UI thread

COMMENTS:

***************************************************************************/
void MainTimer_Init(void)
{
  int i;
  unsigned int Counter = XTimerVal;

  for (i = 0; i < MT_NUM_TIMERS; i++)
  {
    MainTimer[i] = Counter;
  }
} // end MainTimer_Init()

/***************************************************************************

FUNCTION:   SerialShiftTimerISR()

PURPOSE:    Interrupt Service Routine (ISR) for the serial shift timer

COMMENTS:   The pulse door outputs are controlled here because the
            control pulses need to be accurate and the timer interrupt was the
            only way to keep them accurate.  The main() loop is too slow and
            inconsistent.

            The pulse doors are controlled with a one-second-on/one-second-off
            pulse, either opening or closing, except when fully open or closed,
            or initializing, a steady 'pulse' is sent.

            PulseDoorFlag is used as a semaphore between this function and
            CtrlDoorsPulsed() to lock changes to the door position and direction.

            The lights are controller here for the same reason, to keep them
            blinking consistently.

            The shift registers are also controlled here because that's how the
            door and light outputs are actually changed.

***************************************************************************/
void SerialShiftTimerISR(void)
{
  TimerIntClear(TIMER2_BASE, TIMER_A);

  LightCount++;
  PulseCount++;

  // control the pulse door output
  if (PulseCount == 4)    // 1 second
  {
    if (PulseDoorFlag == 0)   // check the semaphore
    {
      if (   PwmChannel[PWM_DOORS].Output == PWM_MAX_VALUE
          && PulseDoorMove == 0
          && PulseDoorInit == 0)
      {
        // supply a steady open 'pulse' when door is at it's max value
        OutputOn(EQ_PULSEDOOR_OPEN);
        OutputOff(EQ_PULSEDOOR_CLOSE);
      }
      else if (   PwmChannel[PWM_DOORS].Output == PWM_MIN_VALUE
               && PulseDoorMove == 0
               && PulseDoorInit == 0)
      {
        // supply a steady close 'pulse' when door is at it's min value
        OutputOn(EQ_PULSEDOOR_CLOSE);
        OutputOff(EQ_PULSEDOOR_OPEN);
      }
      else
      {
        if (   PulseDoorMove != 0
            && PulseDoorFlag == 0)
        {
          PulseDoorFlag = 1;    // set the semaphore
          OutputOn(PulseDoor);
          PulseDoorMove--;
          if (PulseDoor == EQ_PULSEDOOR_OPEN)
          {
            PulseDoorPosition++;
            OutputOff(EQ_PULSEDOOR_CLOSE);
          }
          else
          {
            PulseDoorPosition--;
            OutputOff(EQ_PULSEDOOR_OPEN);
          }

          PulseDoorFlag = 0;    // clear the semephore
        }
        else
        {
          OutputOff(EQ_PULSEDOOR_OPEN);
          OutputOff(EQ_PULSEDOOR_CLOSE);
        }
      }
    }
    PulseCount = 0;
  }

  // control the lights
  if (LightCount == 4)    // 1 second
  {
    if (LightStatus.Red == LT_ON)
    {
      OutputOn(EQ_REDLIGHT);
      LightStatus.RedOn = 1;
    }
    else if (LightStatus.Red == LT_OFF)
    {
      OutputOff(EQ_REDLIGHT);
      LightStatus.RedOn = 0;
    }
    else if (LightStatus.Red == LT_BLINK)
    {
      if (LightStatus.RedOn == 0)
      {
        OutputOn(EQ_REDLIGHT);
        LightStatus.RedOn = 1;
        if (LightStatus.Yellow == LT_BLINK) // to make them alternate
        {
          OutputOff(EQ_YELLOWLIGHT);
          LightStatus.YellowOn = 0;
        }
      }
      else
      {
        OutputOff(EQ_REDLIGHT);
        LightStatus.RedOn = 0;
        if (LightStatus.Yellow == LT_BLINK)
        {
          OutputOn(EQ_YELLOWLIGHT);
          LightStatus.YellowOn = 1;
        }
      }
    }

    if (LightStatus.Yellow == LT_ON)
    {
      OutputOn(EQ_YELLOWLIGHT);
      LightStatus.YellowOn = 1;
    }
    else if (LightStatus.Yellow == LT_OFF)
    {
      OutputOff(EQ_YELLOWLIGHT);
      LightStatus.YellowOn = 0;
    }
    else if (LightStatus.Yellow == LT_BLINK && LightStatus.Red != LT_BLINK)
    {
      if (LightStatus.YellowOn == 0)
      {
        OutputOn(EQ_YELLOWLIGHT);
        LightStatus.YellowOn = 1;
      }
      else
      {
        OutputOff(EQ_YELLOWLIGHT);
        LightStatus.YellowOn = 0;
      }
    }

    LightCount = 0;
  }
} // End SerialShiftTimerISR()

/***************************************************************************

FUNCTION:   SerialShiftTimer_Init()

PURPOSE:    Initialize the timer for serial shift registers

COMMENTS:

***************************************************************************/
void SerialShiftTimer_Init(void)
{
  // Enable the Timer2 peripheral
  SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER2);

  // Wait for the Timer2 module to be ready.
  while(!SysCtlPeripheralReady(SYSCTL_PERIPH_TIMER2));

  TimerIntRegister(TIMER2_BASE, TIMER_A, SerialShiftTimerISR);
  TimerConfigure(TIMER2_BASE, TIMER_CFG_PERIODIC);

  // Set the count time
  TimerLoadSet(TIMER2_BASE, TIMER_A, 30000000);

  // enable the interrupt
  IntEnable(INT_TIMER2A);
  TimerIntEnable(TIMER2_BASE, TIMER_TIMA_TIMEOUT);

  // Enable the timer.
  TimerEnable(TIMER2_BASE, TIMER_A);
} // End of SerialShiftTimer_Init()

/***************************************************************************

FUNCTION:   WatchdogExternal_Init()

PURPOSE:    Initialize the output for the external watchdog timer

COMMENTS:   This timer is used to toggle a GPIO that is monitored by the
            CPLD/FPGA. If the output frequency is not maintained, the CPLD
            takes control of the outputs, turning off DO1 - DO8 and turning
            on GDC_PWR1 & P_DWN1 to close the doors. It also turns on the
            red LCD to indicate the fault.

***************************************************************************/
void WatchdogExternal_Init(void)
{
  // Enable the Timer1 peripheral
  SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER1);

  // Wait for the Timer1 module to be ready.
  while(!SysCtlPeripheralReady(SYSCTL_PERIPH_TIMER1));

  TimerIntRegister(TIMER1_BASE, TIMER_A, WatchdogExternalISR);
  TimerConfigure(TIMER1_BASE, TIMER_CFG_PERIODIC);

  // Set the count time
  TimerLoadSet(TIMER1_BASE, TIMER_A, 150000);

  IntEnable(INT_TIMER1A);
  TimerIntEnable(TIMER1_BASE, TIMER_TIMA_TIMEOUT);

  // Enable the timer.
  TimerEnable(TIMER1_BASE, TIMER_A);
} // end WatchdogExternal_Init()

/***************************************************************************

FUNCTION:   WatchdogExternalISR()

PURPOSE:    Output for the heartbeat to CPLD/FPGA

COMMENTS:

***************************************************************************/
void WatchdogExternalISR(void)
{
  TimerIntClear(TIMER1_BASE, TIMER_A);

  WatchdogExternalOutput ^= 0x02; // Toggle state
  set_output(WD_CPLD, WatchdogExternalOutput);
} // end WatchdogExternalISR()

/***************************************************************************

FUNCTION:   WatchdogExternalStart()

PURPOSE:    Enable the timer for the heartbeat

COMMENTS:

***************************************************************************/
void WatchdogExternalStart(void)
{
  TimerEnable(TIMER1_BASE, TIMER_A);
} // end WatchdogExternalStart()

/***************************************************************************

FUNCTION:   WatchdogExternalStop()

PURPOSE:    Disable the timer for the heartbeat

COMMENTS:

***************************************************************************/
void WatchdogExternalStop(void)
{
  TimerDisable(TIMER1_BASE, TIMER_A);
} // end WatchdogExternalStop()

/***************************************************************************

FUNCTION:   WatchdogInternal_Init()

PURPOSE:    Initialize the internal Tiva watchdog timer

COMMENTS:   This watchdog timer is used by the SerialCom thread that
            reads the analog boards and sets the system state.  If the
            timer is not reset quickly enough, the Tiva processor is reset.

***************************************************************************/
void WatchdogInternal_Init(void)
{
  // Enable the watchdog peripheral.
  SysCtlPeripheralEnable(SYSCTL_PERIPH_WDOG0);

  // Enable the watchdog interrupt.
//  IntRegister(INT_WATCHDOG, WatchdogInternalReset);
//  IntEnable(INT_WATCHDOG);

  // Set the period of the watchdog timer.
  WatchdogReloadSet(WATCHDOG0_BASE, 0x3FFFFFFF);

  // Enable reset generation from the watchdog timer.
  WatchdogResetEnable(WATCHDOG0_BASE);

  // Enable the watchdog timer.
  WatchdogEnable(WATCHDOG0_BASE);

  // Allow debugging, disable in product.
  WatchdogStallEnable(WATCHDOG0_BASE);

  // Disable writes to the watchdog configuration registers.
  WatchdogLock(WATCHDOG0_BASE);
} // WatchdogInternal_Init()

/***************************************************************************

FUNCTION:   WatchdogInternalReset()

PURPOSE:

COMMENTS:

***************************************************************************/
//static unsigned int elapsedTime = 0;
void WatchdogInternalReset(void)
{
//  debug_printf("WatchdogInternalReset elapsedTime:%u\r\n", uptime_sec - elapsedTime);
//  elapsedTime = uptime_sec;

 // Clear the watchdog interrupt.
  WatchdogIntClear(WATCHDOG0_BASE);
} // end WatchdogInternalReset()

/***************************************************************************

FUNCTION:   XTimer()

PURPOSE:    Originally a timer value generated from external clock

COMMENTS:   The external clock was the only way to get a consistent, accurate
            seconds timer.  The internal Atmel RTT drifts too much.

            This function is called in the main loop and updates the global
            timer value (XTimerVal).  Normally, the function gets called roughly
            every second, but because the Atmel ARM is not multi-threaded, the
            main loop can be delayed during a lengthy process like transmitting
            log data.

            This is the primary timer used by the application so it was
            ported to the TI Tiva and just returns the global uptime value.

***************************************************************************/
unsigned int XTimer(void)
{
  XTimerVal = uptime_sec;
  return XTimerVal;
} // end XTimer()
/***   End Of File   ***/
