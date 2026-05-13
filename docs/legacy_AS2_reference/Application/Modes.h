/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     Modes.h

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:

COMMENTS:

***************************************************************************/

#ifndef MODES_H
#define MODES_H

/*** include files ***/

/*** defines ***/

#define COOL_MODE        0
#define DEFROST_MODE     1
#define DEHUMID_MODE     2
#define REFRIG_MODE      3

#define HUMID_MODE_COOL           0
#define HUMID_MODE_RECIRC         1
#define HUMID_MODE_REFRIG         2

/*** typedefs and structures ***/

/*** external variables ***/
extern char OldFanSpeedHeater;

/*** external functions ***/

extern void ModeAirCure(void);
extern void ModeBurnerCure(void);
extern void ModeCooling(int mode);
extern void ModeFanManual(void);
extern void ModeFanOff(void);
//extern void ModeHardFailure(void);
extern void ModeHeating(void);
extern void ModeIdle(void);
extern void ModeRefrig(int mode);
extern void ModeRecirc(void);
extern void ModeShutdown(void);
extern void ModeStandby(void);

#endif

/***   End Of File   ***/
