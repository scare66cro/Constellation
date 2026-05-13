/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     ThreadFileReceive.h

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:

COMMENTS:

***************************************************************************/

#ifndef THREADFILERECEIVE_H
#define THREADFILERECEIVE_H

/*** include files ***/

// FreeRTOS
#include "FreeRTOS.h"
#include "semphr.h"

/*** defines ***/

#define THREADFILERECEIVE_PRIORITY    4
#define THREADFILERECEIVE_STACK_SIZE  1024

#define UPGRADE_FILE_TAG        "Gellert AS2"

#define BOOTLOADER_START        "^BOOT_START=1!"
#define BOOTLOADER_ARM_MSG      "^ARM_PROG=1!"
#define BOOTLOADER_SET_MSG      "^BOOTLOADER=1$4293960025!"
#define BOOTLOADER_SET_CMD      "BOOTLOADER=1$4293960025"
#define BOOTLOADER_ARM_CMD      "ARM_PROG=1"

#define SREC_LEN        100

#define BAD_CS          700
#define RECORD_OK       701
#define END_OF_FILE     702
#define BUFFER_FULL     703
#define BAD_FILE        704
#define LESS_BYTES      705
#define UNHANDLED_TYPE  706
#define TIMEOUT         707

/*** typedefs and structures ***/

typedef enum restart_address
{
  BOOTLOADER = 0x00000000,
  APPLICATION = 0x0001c000
} RESTART_ADDRESS;

typedef enum file_receive_type
{
  FRT_STARTUP,
  FRT_FIRMWARE,
  FRT_SETTINGS,
  FRT_EQUIPDESC
} FILE_RECEIVE_TYPE;

typedef enum file_receive_status
{
  FR_IDLE,
  FR_PENDING,
  FR_RECEIVING,
  FR_CONVERTING
} FILE_RECEIVE_STATUS;

typedef struct file_receive_info
{
  FILE_RECEIVE_TYPE Type;
  FILE_RECEIVE_STATUS Status;
} FILE_RECEIVE_INFO;

/*** external variables ***/

extern FILE_RECEIVE_INFO FileReceive;

/*** external functions ***/

extern void Restart(RESTART_ADDRESS address);
extern int SrecRead(unsigned char *Stream, char *RecordType, unsigned char *Buffer, int *BytesWritten, int BytesRequested);
extern void ThreadFileReceive(void);

#endif

/***   End Of File   ***/
