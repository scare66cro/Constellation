/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     RTC.h

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:

COMMENTS:

***************************************************************************/

#ifndef RTC_H
#define RTC_H

/*** include files ***/

/*** defines ***/

/*** typedefs and structures ***/

/*** external variables ***/

/*** external functions ***/

extern int DateInMonth(char Occurrence, char Day, char Month, int Year);
extern void DaylightSavingCheck(char Force, char AdjustClock);
extern int DayOfWeek(int Year, char Month, char Day);

extern int GetDate(short *Year, char *Month, char *Day);
extern int GetDateStr(char *DateStr);
extern int GetTime(char *Hour, char *Minute, char *Second);
extern int GetTimeStr(char *TimeStr, uint8_t *AmPm);

extern void RTC_Init(void);

extern int SetDateTime(uint16_t Year, uint8_t Month, uint8_t Day, uint8_t Hour, uint8_t Minute, uint8_t Second);
extern int SetDateTimeStr(char *DateStr, char *TimeStr, uint8_t AmPm);

extern void SystemTime_Init(void);
extern void UpdateSystemTime(void);

#endif

/***   End Of File   ***/
