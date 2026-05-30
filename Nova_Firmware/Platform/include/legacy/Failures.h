/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:    Failures.h

AUTHOR:  CBostic

COMPANY: Infinetix

PURPOSE:

COMMENTS:

***************************************************************************/

#ifndef FAILURES_H
#define FAILURES_H

/*** include files ***/

/*** defines ***/

/*** typedefs and structures ***/

/*** external variables ***/

/*** external functions ***/

extern int SystemFailuresChk(void);

/* Sub-check exposed individually so the LP engine can run just the
 * master-broadcast watchdog without enabling the full failure pipeline
 * (the rest of `SystemFailuresChk` depends on equipment state that the
 * LP engine doesn't yet drive). Keeps Constellation slave panels
 * raising WARN_NOBROADCAST after 10 minutes of master silence, the
 * AS2-faithful behaviour. */
extern void NovaFailures_RunMasterBroadcastChk(void);

#endif

/***   End Of File   ***/
