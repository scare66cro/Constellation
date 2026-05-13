/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     DataExc.h

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:

COMMENTS:

***************************************************************************/

#ifndef DATAEXC_H
#define DATAEXC_H

/*** include files ***/

#include <stdlib.h>

/*** defines ***/

#define MSG_STATE_ACK           1
#define MSG_STATE_RECEIVE       2
#define MSG_STATE_SEND          3
#define MSG_STATE_WAITING       4
#define MSG_STATE_RETRY         5

#define MSG_CRC_DIVIDER         '~'
#define MSG_DIVIDER             '='
#define MSG_END_DELIMITER       '!'
#define MSG_FIELD_DELIMITER     '&'
#define MSG_START_DELIMITER     '^'
#define MSG_TERMINATOR          '$'

#define MSG_END_DELIMITER_STR   "!"
#define MSG_START_DELIMITER_STR "^"
#define MSG_TERMINATOR_STR      "$"

#define MSG_RX_BUFFER_SIZE      600   // Lantronix TX_BUFFER_SIZE = 600
#define MSG_TX_BUFFER_SIZE      230   // Lantronix RX_BUFFER_SIZE = 230
#define MSG_TX_CGIVAR_SIZE      210   // Lantronix VAR_BUFFER_SIZE = 210

#define MSG_CRC_LEN             20
#define MSG_MAX_POST_ITEMS      53    // auxiliary output programming + SessionID
#define MSG_MAX_TAG_LEN         20    // Lantronix CGI_TAG_LEN = 20
#define MSG_MAX_VALUE_LEN       MSG_RX_BUFFER_SIZE

#define MS_NOT_INITIALIZED      0
#define MS_RESPONDING           1
#define MS_NOT_RESPONDING       2

#define UI_DELAY_STANDARD       2U
#define UI_DELAY_LONG           10U
#define UI_INIT_TIMEOUT         15

#define CRC_LEN                 20    // CRC checksum value

/*** typedefs and structures ***/

typedef struct {
  char RxBuffer[MSG_RX_BUFFER_SIZE];
  char TxBuffer[MSG_TX_BUFFER_SIZE];
  char Tag[MSG_MAX_TAG_LEN+1];
  char State;
  char NAKcount;
  int  InErrors;
  int  OutErrors;
} UI_MESSAGE_BUFFER;

typedef struct {
  char  Buffer[MSG_TX_BUFFER_SIZE];
  void *Next;
} UI_MESSAGE_QUEUE;

/*** external variables ***/

extern UI_MESSAGE_BUFFER  UI_Message;

extern signed char KillMessage;
extern char MessagingStatus;

extern char PostTag[MSG_MAX_POST_ITEMS][MSG_MAX_TAG_LEN];
extern char PostValue[MSG_MAX_POST_ITEMS][MSG_MAX_VALUE_LEN];

/*** external functions ***/

extern void FreeMsgQueue(void);
extern int GetNumPostItems(void);
extern int GetPostValueByField(char *field, char *value, size_t length);
extern void ParsePost(void);
extern void ProcessMsgQueue(void);
extern void ProcessUIMessage(void);
extern void QueueMessage(char *msg);
extern void SendMsgAndNoWait(char *msg);
extern int SendMsgAndWaitForResponse(char *Msg, unsigned int Delay);

#endif

/***   End Of File   ***/
