/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     RTC.c

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:  Real time clock functions

COMMENTS:

***************************************************************************/

/*** include files ***/

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// Platform
#include <stdint.h>
#include <stdbool.h>

#include "time.h"
#include "system.h"
#include "inc/hw_hibernate.h"
#include "inc/hw_memmap.h"
#include "inc/hw_types.h"
#include "driverlib/gpio.h"
#include "driverlib/hibernate.h"
#include "driverlib/pin_map.h"
#include "driverlib/sysctl.h"

// Gellert
#include "RTC.h"
#include "Settings.h"
#include "States.h"
#include "Warnings.h"

/*** defines ***/

/*** typedefs and structures ***/

/*** module variables ***/

static struct tm systemTime;

  /*** static functions ***/

static int DaylightSavingBegin(int Year);
static int DaylightSavingEnd(int Year);
static void SetClockAhead(void);
static void SetClockBack(void);

/***************************************************************************

FUNCTION:   DateInMonth()

PURPOSE:    Returns the date of a particular occurrence of a day in the month

COMMENTS:   For example, to find the date of the second Sunday in March, 2008
            pass the function (2, 0, 3, 2008).  It returns 9 => the second
            Sunday in March, 2008 is the 9th.

            Parameters
              Occurrence -> 1-5 (week) (Note: the returned value for week needs to
                            be checked that it's not beyond the end of the month
              Day -> 0 (Sunday) - 6 (Saturday)
              Month -> 1-12
              Year -> four digit year, i.e. 2008

            This algorithm is credited to Marcos J. Montes
              (http://www.smart.net/~mmontes/ushols.html)
            His basic formula is
              QN = (7*Q) - 6 + (N - DayOfWeek(Year, Month, 1))%7
              where
                Q = Occurrence
                N = weekday
                DayOfWeek() is a function/algorithm he credits to Claus Tondering
                  (http://www.tondering.dk/claus/cal/node3.html#SECTION00360000000000000000)

            He defines -7%7=0, -6%7=1, -5%7=2, ... -1%7=6

            Because N can only be 0-6 and DayOfWeek returns 0-6,
            N - DayOfWeek() can only be -6 to 6 so the %7 has no effect.
            Because of this, the (N - DayOfWeek(Year, Month, 1))%7 portion of
            the formula has been modified. If N - DayOfWeek() is negative, it
            is adjusted according to his rule above, otherwise it is used directly.

***************************************************************************/
int DateInMonth(char Occurrence, char Day, char Month, int Year)
{
  int DoW = DayOfWeek(Year, Month, 1);
  int d = Day - DoW;

  // if d is negative, adjust according to rule (see comments)
  if (d < 0)
    d = 7 + d;

  return (7*Occurrence) - 6 + d;
} // end DateInMonth()

/***************************************************************************

FUNCTION:   DaylightSavingCheck()

PURPOSE:    Check if current date is in Daylight Saving Time or not

COMMENTS:   By default (Force = 0) this function only performs the check once
            an hour on the hour (minutes = :00) to reduce the number of checks
            performed when the function is called from the main() loop.

            The 'Force' parameter can be used to force the check to be performed.
            This is done when the ARM starts up (to see if DST status has changed
            while the ARM was shutdown) and when the date/time is set from the
            UI (to set the DST state variable Settings.DST appropriately).
            When the date/time is set from the UI, the 'AdjustClock' must also
            be set to '0'.  This allows the check to adjust the state variable
            but avoids adjusting the clock immediately after the user set the
            time.  For example, if the date/time is currently Standard Time and
            the user adjusted the date/time to a date/time that is Daylight Time,
            like 7/1/2008 8:00 the check would automatically adjust the time to
            9:00 if AdjustClock is not set to 0.

***************************************************************************/
void DaylightSavingCheck(char Force, char AdjustClock)
{
  short Year;
  char  Month;
  char  Day;
  char  Hour;
  char  Min;
  char  Sec;
  int   DSTBegin;
  int   DSTEnd;
  int   Today;

  if (   GetDate(&Year, &Month, &Day) == 0
      || GetTime(&Hour, &Min, &Sec) == 0)
  {
    return;
  }

  // only run this check once an hour on the hour, unless forced on startup
  if (Min != 0 && Force == 0)
  {
    return;
  }

  Today = (Year*10000) + (Month*100) + Day;
  DSTBegin = (Year*10000) + (3*100) + DaylightSavingBegin(Year);
  DSTEnd = (Year*10000) + (11*100) + DaylightSavingEnd(Year);

  if (Today < DSTBegin)
  {
    if (Settings.DST == 1)
    {
      if (AdjustClock == 1)
      {
        SetClockBack();
      }
      Settings.DST = 0;
    }
  }
  else if (Today == DSTBegin)
  {
    if (Hour >= 2)
    {
      if (Settings.DST == 0)
      {
        if (AdjustClock == 1)
        {
          SetClockAhead();
        }
        Settings.DST = 1;
      }
    }
  }
  else if (Today > DSTBegin && Today < DSTEnd)
  {
    if (Settings.DST == 0)
    {
      if (AdjustClock == 1)
      {
        SetClockAhead();
      }
      Settings.DST = 1;
    }
  }
  else if (Today == DSTEnd)
  {
    if (Hour >= 1)
    {
      if (Settings.DST == 1)
      {
        if (AdjustClock == 1)
        {
          SetClockBack();
        }
        Settings.DST = 0;
      }
    }
  }
  else if (Today > DSTEnd)
  {
    if (Settings.DST == 1)
    {
      if (AdjustClock == 1)
      {
        SetClockBack();
      }
      Settings.DST = 0;
    }
  }

} // end DaylightSavingCheck()

/***************************************************************************

FUNCTION:   DaylightSavingBegin()

PURPOSE:    Determine when DST starts

COMMENTS:   Currently in the US, DST begins the second Sunday in March.

***************************************************************************/
int DaylightSavingBegin(int Year)
{
  // DST begins the second (2) Sunday (0) in March (3)
  return DateInMonth(2, 0, 3, Year);
} // end DaylightSavingBegin()

/***************************************************************************

FUNCTION:   DaylightSavingEnd()

PURPOSE:    Determine when DST ends

COMMENTS:   Currently in the US, DST ends the first Sunday in November

***************************************************************************/
int DaylightSavingEnd(int Year)
{
  // DST end the first (1) Sunday (0) in November (11)
  return DateInMonth(1, 0, 11, Year);
} // end DaylightSavingEnd()

/***************************************************************************

FUNCTION:   DayOfWeek()

PURPOSE:    Returns the day of the week of a given date

COMMENTS:   For example, given 3/9/2008 it will return 0 (Sunday)

            Parameters
              Year - four digit year, i.e. 2008
              Month - 1-12
              Day - 1-31

            This formula/algorithm is referenced by Marcos J. Montes
              (http://www.smart.net/~mmontes/ushols.html)
            He credits the algorithm to Claus Tondering
              (http://www.tondering.dk/claus/cal/node3.html#SECTION00360000000000000000)

***************************************************************************/
int DayOfWeek(int Year, char Month, char Day)
{
  int a = (14 - Month)/12;
  int y = Year - a;
  int m = Month + (12*a) - 2;

  return (Day + y + (y/4) - (y/100) + (y/400) + ((31*m)/12)) % 7;
} // end DayOfWeek()

/***************************************************************************

FUNCTION:   GetDate()

PURPOSE:    Get the date from the RTC

COMMENTS:   systemTime is maintained by UpdateSystemTime called from the
            UI thread

***************************************************************************/
int GetDate(short *Year, char *Month, char *Day)
{
  *Year = systemTime.tm_year + 1900;
  *Month = systemTime.tm_mon + 1;
  *Day = systemTime.tm_mday;

  if (   (systemTime.tm_mon < 0 || systemTime.tm_mon > 11)
      || (systemTime.tm_mday < 1 || systemTime.tm_mday > 31)
      || (systemTime.tm_year < 0 || systemTime.tm_year > 200))
  {
    WarningsSet(WARN_DATETIME, FM_ALARM, FM_ALARM, NA);
    return 0;
  }

  return 1;
} // end GetDate()

/***************************************************************************

FUNCTION:   GetDateStr()

PURPOSE:    Get the date from the RTC

COMMENTS:   Returns a formatted string MM/DD/YYYY

***************************************************************************/
int GetDateStr(char *DateStr)
{
  short Year;
  char  Month;
  char  Day;

  if (GetDate(&Year, &Month, &Day) == 0)
  {
    strcpy(DateStr, "--/--/--");
    return 0;
  }

  snprintf(DateStr, DATE_LEN+1, "%02d/%02d/%04d", Month, Day, Year);
  return 1;
} // end GetDateStr()

/***************************************************************************

FUNCTION:   GetTime()

PURPOSE:    Get the time from the RTC

COMMENTS:   systemTime is maintained by UpdateSystemTime called from the
            UI thread

***************************************************************************/
int GetTime(char *Hour, char *Minute, char *Second)
{
  *Hour = systemTime.tm_hour;
  *Minute = systemTime.tm_min;
  *Second = systemTime.tm_sec;

  if (   (systemTime.tm_hour > 23 || systemTime.tm_hour < 0)
      || (systemTime.tm_min > 59 || systemTime.tm_min < 0)
      || (systemTime.tm_sec > 59 || systemTime.tm_sec < 0) )
  {
    WarningsSet(WARN_DATETIME, FM_ALARM, FM_ALARM, NA);
    return 0;
  }

  return 1;
} // end GetTime()

/***************************************************************************

FUNCTION:   GetTimeStr()

PURPOSE:    Get the time from the RTC

COMMENTS:   Returns a formatted string HH:MM:SS

            Tested with 11:00 am/pm, 12:00 am/pm, 1:00 am/pm

***************************************************************************/
int GetTimeStr(char *TimeStr, uint8_t *AmPm)
{
  char Hour;
  char Min;
  char Sec;

  if (GetTime(&Hour, &Min, &Sec) == 0)
  {
    strcpy(TimeStr, "--:--:--");
    return 0;
  }

  if (Hour >= 12)
  {
    *AmPm = 1;
    if (Hour > 12)
      Hour -= 12;
  }
  else
  {
    *AmPm = 0;
    if (Hour == 0)
      Hour = 12;
  }

  snprintf(TimeStr, TIME_LEN+1, "%d:%02d:%02d", Hour, Min, Sec);
  return 1;
} // end GetTimeStr()

/***************************************************************************

FUNCTION:   RTC_Init()

PURPOSE:    Intialize the internal RTC

COMMENTS:   The RTC is accessed via the hibernate module

***************************************************************************/
void RTC_Init(void)
{
  // Enable GPIO Module and Configure the Pin Mux.
  SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOP);
  while(!(SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOP)));

  GPIOPinConfigure(GPIO_PP3_RTCCLK);
  GPIODirModeSet(GPIO_PORTP_BASE, GPIO_PIN_3, GPIO_DIR_MODE_HW);
  GPIOPadConfigSet(GPIO_PORTP_BASE, GPIO_PIN_3, GPIO_STRENGTH_8MA, GPIO_PIN_TYPE_STD);

  // Enable Hibernate Module Clock and wait for module to be ready
  SysCtlPeripheralEnable(SYSCTL_PERIPH_HIBERNATE);
  while(!(SysCtlPeripheralReady(SYSCTL_PERIPH_HIBERNATE)));

  // Configure Hibernate module clock.
  HibernateEnableExpClk(system_clock_speed);

  // TODO - clb: wait here to allow crystal to power up and stabilize ???

  // Enable RTC mode.
  HibernateRTCEnable();

  // Configure the hibernate module counter to 24-hour calendar mode.
  HibernateCounterMode(HIBERNATE_COUNTER_24HR);

  // Configure HIBCC for RTCOSC available
  HWREG(HIB_CC) = HIB_CC_SYSCLKEN;
}

/***************************************************************************

FUNCTION:   SetClockAhead()

PURPOSE:    Set the clock ahead one hour (for Daylight Saving Time)

COMMENTS:

***************************************************************************/
void SetClockAhead(void)
{
  short Year;
  char  Month;
  char  Day;
  char  Hour;
  char  Min;
  char  Sec;

  if (   GetDate(&Year, &Month, &Day) == 0
      || GetTime(&Hour, &Min, &Sec) == 0)
  {
    return;
  }

  Hour++;
  if (Hour > 23)
  {
    Hour = 0;
    Day++;
    if (   (   (Month == 1 || Month == 3 || Month == 5 || Month == 7 || Month == 8 || Month == 10 || Month == 12)
            && Day > 31)
        || (   (Month == 4 || Month == 6 || Month == 9 || Month == 11)
            && Day > 30)
        || (   (Month == 2)
            && (   ((Year%4) != 0 && Day > 28)
                || ((Year%4) == 0 && Day > 29)) )  )
    {
      Day = 1;
      Month++;
      if (Month > 12)
      {
        Month = 1;
        Year++;
      }
    }
  }

  SetDateTime(Year, Month, Day, Hour, Min, Sec);
  WarningsSet(WARN_DSTSTART, FM_ALARM, FM_ALARM, NA);
} // end SetClockAhead()

/***************************************************************************

FUNCTION:   SetClockBack()

PURPOSE:    Set clock back one hour (for Standard Time)

COMMENTS:

***************************************************************************/
void SetClockBack(void)
{
  short Year;
  char  Month;
  char  Day;
  char  Hour;
  char  Min;
  char  Sec;

  if (   GetDate(&Year, &Month, &Day) == 0
      || GetTime(&Hour, &Min, &Sec) == 0)
  {
    return;
  }

  if (Hour > 0)
    Hour--;
  else
  {
    Hour = 23;
    if (Day == 1)
    {
      if (Month == 1 ||  Month == 2 ||  Month == 4 || Month == 6 || Month == 8 || Month == 9 || Month == 11)
      {
        Day = 31;
      }
      else if (Month == 5 || Month == 7 || Month == 10 || Month == 12)
      {
        Day = 30;
      }
      else if (Month == 3)
      {
        if ((Year%4) != 0)
          Day = 28;
        else
          Day = 29;
      }

      Month--;
      if (Month == 0)
      {
        Month = 12;
        Year--;
      }
    }
    else
    {
      Day--;
    }
  }

  SetDateTime(Year, Month, Day, Hour, Min, Sec);
  WarningsSet(WARN_DSTSTOP, FM_ALARM, FM_ALARM, NA);
} // end SetClockBack()

/***************************************************************************

FUNCTION:   SetDateTime()

PURPOSE:    Sets the Tiva internal RTC date & time

COMMENTS:   Expects the hour in 24-hour format

            An external RTC is no longer used.

***************************************************************************/
int SetDateTime(uint16_t Year, uint8_t Month, uint8_t Day, uint8_t Hour, uint8_t Minute, uint8_t Second)
{
  // range checking
  if (   (Second > 59)
      || (Minute > 59)
      || (Hour > 23)
      || (Day < 1 || Day > 31)
      || (Month < 1 || Month > 12)
      || (Year < 1900 || Year > 2099))
  {
    WarningsSet(WARN_INVALIDDATETIME, FM_ALARM, FM_ALARM, NA);
    return 0;
  }

  systemTime.tm_year = Year - 1900;
  systemTime.tm_mon = Month - 1;
  systemTime.tm_mday = Day;
  systemTime.tm_hour = Hour;
  systemTime.tm_min = Minute;
  systemTime.tm_sec = Second;

  // set the Tiva internal RTC
  HibernateCalendarSet(&systemTime);

  return 1;
} // end SetDateTime()

/***************************************************************************

FUNCTION:   SetDateTimeStr()

PURPOSE:    Sets the RTC date

COMMENTS:   Expects a date string (MM/DD/YYYY), time string (HH:MM(:SS))
            seconds are optional, and an am/pm indicator.

***************************************************************************/
int SetDateTimeStr(char *DateStr, char *TimeStr, uint8_t AmPm)
{
  short Year;
  char  Month;
  char  Day;
  char  Hour;
  char  Min;
  char  Sec = 0;
  char  str[10];
  int   DateIndex = 0;
  int   StrIndex = 0;
  int   TimeIndex = 0;
  int   Status = 0;

  // parse the date
  while (DateStr[DateIndex] != '/' && DateStr[DateIndex] != 0)
  {
    str[StrIndex++] = DateStr[DateIndex++];
  }
  str[StrIndex] = 0;
  Month = atoi(str);

  DateIndex++;
  StrIndex = 0;
  while (DateStr[DateIndex] != '/' && DateStr[DateIndex] != 0)
  {
    str[StrIndex++] = DateStr[DateIndex++];
  }
  str[StrIndex] = 0;
  Day = atoi(str);

  DateIndex++;
  StrIndex = 0;
  while (DateStr[DateIndex] != 0)
  {
    str[StrIndex++] = DateStr[DateIndex++];
  }
  str[StrIndex] = 0;
  Year = atoi(str);

  // parse the time
  StrIndex = 0;
  while (TimeStr[TimeIndex] != ':' && TimeStr[TimeIndex] != 0)
  {
    str[StrIndex++] = TimeStr[TimeIndex++];
  }
  str[StrIndex] = 0;
  Hour = atoi(str);
  if (AmPm == 1)    // pm
  {
    if (Hour < 12)
    {
      Hour += 12;
    }
  }
  else              // am
  {
    if (Hour == 12)
    {
      Hour = 0;
    }
  }

  TimeIndex++;
  StrIndex = 0;
  while (TimeStr[TimeIndex] != ':' && TimeStr[TimeIndex] != 0)
  {
    str[StrIndex++] = TimeStr[TimeIndex++];
  }
  str[StrIndex] = 0;
  Min = atoi(str);

  // seconds are optional in the time string
  if (TimeStr[TimeIndex] != 0)
  {
    TimeIndex++;
    StrIndex = 0;
    while (TimeStr[TimeIndex] != 0)
    {
      str[StrIndex++] = TimeStr[TimeIndex++];
    }
    str[StrIndex] = 0;
    Sec = atoi(str);
  }

  Status = SetDateTime(Year, Month, Day, Hour, Min, Sec);

  if (Status == 1)
  {
    DaylightSavingCheck(1, 0);    // (force the check, don't adjust the clock only the DST state variable)
  }

  return(Status);
} // end SetDateTimeStr()

/***************************************************************************

FUNCTION:   SystemTime_Init()

PURPOSE:    If the system clock doesn't have a valid time, set one

COMMENTS:   Arbitrarily sets it to 12:00 noon

***************************************************************************/
void SystemTime_Init(void)
{
  short Year;
  char  Month;
  char  Day;
  char  Hour;
  char  Min;
  char  Sec;

  UpdateSystemTime();

  if (   GetDate(&Year, &Month, &Day) == 0
      || GetTime(&Hour, &Min, &Sec) == 0)
  {
    SetDateTime(2019,1,1,12,0,0);
    WarningsSet(WARN_TIMERESET, FM_ALARM, FM_ALARM, NA);
  }
} // end SystemTime_Init()

/***************************************************************************

FUNCTION:   UpdateSystemTime()

PURPOSE:    Get the time from the RTC

COMMENTS:   UpdateSystemTime is called from the UI thread and maintains the
            systemTime structure

***************************************************************************/
void UpdateSystemTime(void)
{
  HibernateCalendarGet(&systemTime);
} // end UpdateSystemTime()

/***   End Of File   ***/
