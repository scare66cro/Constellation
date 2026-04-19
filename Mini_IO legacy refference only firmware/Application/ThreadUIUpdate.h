/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     ThreadUIUpdate.h

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:

COMMENTS:

***************************************************************************/

#ifndef THREADUIUPDATE_H
#define THREADUIUPDATE_H

/*** include files ***/

/*** defines ***/

#define THREADUIUPDATE_PRIORITY     4
#define THREADUIUPDATE_STACK_SIZE   1024

/*** typedefs and structures ***/

/*** external variables ***/

extern unsigned int ThreadFlags;

/*** external functions ***/

extern void ThreadUIUpdate(void);

#endif

/***   End Of File   ***/
