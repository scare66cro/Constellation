/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     DataExc.c

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:  Lantronix/UI communication

COMMENTS:

***************************************************************************/

/*** include files ***/

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <float.h>

// Platform
#include "system.h"
#include "tools.h"
#include "utils/uartstdio.h"

// FreeRTOS Includes
#include "FreeRTOS.h"
#include "task.h"

// Gellert
#include "Analog_Input.h"
#include "Controls.h"
#include "DataExc.h"
#include "LoadLogs.h"
#include "PWM.h"
#include "PIDLogs.h"
#include "RTC.h"
#include "SDCard.h"
#include "SerialShift.h"
#include "Settings.h"
#include "States.h"
#include "StorePostData.h"
#include "SystemLogs.h"
#include "ThreadFileReceive.h"
#include "ThreadMonitor.h"
#include "Timer.h"
#include "UI_Messages.h"
#include "Usart.h"
#include "UserLogs.h"
#include "Warnings.h"

/*** defines ***/

/*** typedefs and structures ***/

/*** module variables ***/

UI_MESSAGE_BUFFER  UI_Message;

signed char KillMessage = -1;
char MessagingStatus = MS_NOT_INITIALIZED;

char PostTag[MSG_MAX_POST_ITEMS][MSG_MAX_TAG_LEN];
char PostValue[MSG_MAX_POST_ITEMS][MSG_MAX_VALUE_LEN];

static int PostItemIndex = 0;

static int FailedMessages = 0;
volatile static char ProcessingLtxMessage = 0;

static int  SessionID;

static UI_MESSAGE_QUEUE  *UI_MsgQ = NULL;

/*** static functions ***/

static void AddMsgDelimiters(char *Msg);
static int ExecuteActionPosts(int PostIndex);
static int CheckCRC(char *Msg);
static void GetMessage(char *RxBuf, char *Msg);
static void GetMessageCRC(char *Msg, char *CrcStr);
static void GetMessageTag(char *Msg, char *Tag);
static void GetReplyTag(char *Msg, char *Tag);
static void ProcessPost(int PostType, int PostIndex);
static void SendDataLoadStatus(char *Status);
static void SendMessage(void);
static void SendNAK(char *MsgTag);
static int WaitForMsgResponse(unsigned int Delay);

/***************************************************************************

FUNCTION:   AddMsgDelimiters()

PURPOSE:    Add the delimiters and CRC to a message

COMMENTS:

***************************************************************************/
void AddMsgDelimiters(char *Msg)
{
  char CrcStr[MSG_CRC_LEN];
  unsigned int Crc = CRC(Msg, strlen(Msg));

  sprintf(CrcStr, "%u", Crc);
  strcpy(UI_Message.TxBuffer, MSG_START_DELIMITER_STR);
  strncat(UI_Message.TxBuffer, Msg,                   (sizeof(UI_Message.TxBuffer)-strlen(UI_Message.TxBuffer)-1));
  strncat(UI_Message.TxBuffer, MSG_TERMINATOR_STR,    (sizeof(UI_Message.TxBuffer)-strlen(UI_Message.TxBuffer)-1));
  strncat(UI_Message.TxBuffer, CrcStr,                (sizeof(UI_Message.TxBuffer)-strlen(UI_Message.TxBuffer)-1));
  strncat(UI_Message.TxBuffer, MSG_END_DELIMITER_STR, (sizeof(UI_Message.TxBuffer)-strlen(UI_Message.TxBuffer)-1));
} // AddMsgDelimiters()

/***************************************************************************

FUNCTION:   ExecuteActionPosts()

PURPOSE:    Perform action for execution posts

COMMENTS:   Performs function for posts that don't send/store data.
            Typically used for buttons on UI pages like save, restore, set, etc.

***************************************************************************/
int ExecuteActionPosts(int PostIndex)
{
  int  RetVal = 1;
  char InitializeBoards = 0;

  switch (PostIndex)
  {
  case TAG_A_SETDEFAULT:
    RequestSettingsSave(SR_REQUEST, SAVED_SETTINGS);
    break;

  case TAG_A_PANELDEFAULT:
    FileReceive.Status = FR_CONVERTING;
    ProcessingLtxMessage = 0;
    SettingsRestoreFromLocalFlash();
    FileReceive.Status = FR_IDLE;

    RequestSettingsSave(SR_REQUEST, ACTIVE_SETTINGS);
    UI_SendAllSettings();
    UI_SendAnalogBoard(0, 1, MSG_QUEUE);
    UI_SendAuxProgram(EQ_AUX1, 1, MSG_QUEUE);
    UI_SendHttpPort(MSG_QUEUE);
    break;

  case TAG_A_FACTORYDEFAULT:
    GetFactoryDefault();
    RequestSettingsSave(SR_REQUEST, ACTIVE_SETTINGS);
    UI_SendAllSettings();
    UI_SendAuxProgram(EQ_AUX1, 1, MSG_QUEUE);
    UI_SendHttpPort(MSG_QUEUE);
    DiscoverAnalogBoardsRequest(AB_DISCOVER);
    break;

  case TAG_A_CLEARALARM:
    if (IsBoardWarning())
    {
      InitializeBoards = 1;
    }

    ClearAlarms(0);
    WarningsClear();
    UI_SendWarnings(1);
    SystemLogWrite(1);

    if (InitializeBoards == 1)
    {
      DiscoverAnalogBoardsRequest(AB_DISCOVER);
    }
    break;

  case TAG_A_CLEARDIAG:
    CtrlRefrigDiagClear();
    break;

  case TAG_A_USERLOGGRAPH:
    ConvertSpecialChars(PostValue[0], MSG_MAX_VALUE_LEN);    // query
    ConvertSpecialChars(PostValue[1], MSG_MAX_VALUE_LEN);    // start date
    ConvertSpecialChars(PostValue[2], MSG_MAX_VALUE_LEN);    // end date
    ProcessLogQuery(PostValue[0]);
    RetVal = UserLogSendToUI(PostValue[1], PostValue[2], GRAPH_MODE, SessionID);
    break;

  case TAG_A_USERLOGTABLE:
    ConvertSpecialChars(PostValue[0], MSG_MAX_VALUE_LEN);    // query
    ConvertSpecialChars(PostValue[1], MSG_MAX_VALUE_LEN);    // start date
    ConvertSpecialChars(PostValue[2], MSG_MAX_VALUE_LEN);    // end date
    ProcessLogQuery(PostValue[0]);
    RetVal = UserLogSendToUI(PostValue[1], PostValue[2], TABLE_MODE, SessionID);
    break;

  case TAG_A_USERLOGDOWNLOAD:
    ConvertSpecialChars(PostValue[1], MSG_MAX_VALUE_LEN);    // start date
    ConvertSpecialChars(PostValue[2], MSG_MAX_VALUE_LEN);    // end date
    RetVal = UserLogSendToFile(PostValue[1], PostValue[2], SessionID);
    break;

  case TAG_A_USERLOGCLEAR:
    UserLogReset(&SDCardHeader, 1);
    UI_SendUserLogSettings(MSG_SEND);
    break;

  case TAG_A_LOADLOGCLEAR:
    LoadLogReset(&SDCardHeader, 1);
    UI_SendLoadMonitor(MSG_SEND);
    break;

  case TAG_A_LOADLOGCLEARSENSORS:
    Settings.LoadMonitor.IrSensorsAvailable[0] = 0;
    Settings.LoadMonitor.IrSensorsAvailable[1] = 0;
    Settings.LoadMonitor.Bay[0].SensorID = 0;
    Settings.LoadMonitor.Bay[1].SensorID = 0;
//    UI_SendLoadMonitor(MSG_SEND);
    break;

  case TAG_A_LOADLOGSTART:
     if (Settings.LoadMonitor.Bay[0].SensorID != 0)
     {
        Settings.LoadMonitor.Bay[0].Status = LL_ACQUIRING;
     }
     if (   Settings.LoadMonitor.Bay[1].SensorID != 0
         && Settings.LoadMonitor.Bay[1].SensorID != Settings.LoadMonitor.Bay[0].SensorID)
     {
        Settings.LoadMonitor.Bay[1].Status = LL_ACQUIRING;
     }

    LoadLogWrite(0);

    Settings.LoadMonitor.Bay[0].Status = LL_PAUSED;
    Settings.LoadMonitor.Bay[1].Status = LL_PAUSED;
//    UI_SendLoadMonitor(MSG_SEND);
    break;

  case TAG_A_LOADLOGGRAPH:
    RetVal = LoadLogSendToUI(atoi(PostValue[2]), GRAPH_MODE, SessionID);
    break;

  case TAG_A_LOADLOGTABLE:
    RetVal = LoadLogSendToUI(2, TABLE_MODE, SessionID);
    break;

  case TAG_A_LOADLOGDOWNLOAD:
    RetVal = LoadLogSendToUI(2, FILE_MODE, SessionID);
    break;

  case TAG_A_WARNINGLOGSHOW:
    RetVal = SystemLogSendToUI(0, atoi(PostValue[0]), SessionID, 1);
    break;

  case TAG_A_SYSTEMLOGSHOW:
    ConvertSpecialChars(PostValue[2], MSG_MAX_VALUE_LEN);    // query
    ProcessLogQuery(PostValue[2]);
    RetVal = SystemLogSendToUI(atoi(PostValue[0]), atoi(PostValue[1]), SessionID, 0);
    break;

  case TAG_A_SYSTEMLOGDOWNLOAD:
    SystemLogSendToFile(atoi(PostValue[0]), atoi(PostValue[1]), SessionID);
    break;

  case TAG_A_SYSTEMLOGCLEAR:
    SystemLogReset(&SDCardHeader.SysLog, 1);
    break;

  case TAG_A_SDCARDINIT:
    SDHeaderReset(&SDCardHeader, 1);
    UI_SendUserLogSettings(MSG_SEND);
    break;

  case TAG_A_PIDLOGDOWNLOAD:
    RetVal = PIDLogSendToFile(atoi(PostValue[0]), atoi(PostValue[1]), atoi(PostValue[2]), SessionID);
    break;

  case TAG_A_PIDLOGSHOW:
    RetVal = PIDLogSendToUI(atoi(PostValue[0]), atoi(PostValue[1]), atoi(PostValue[2]), SessionID);
    break;

  case TAG_A_PIDLOGCLEAR:
    PIDLogReset(&SDCardHeader.PIDLog, 1);
    break;

  case TAG_A_SETTINGSDOWNLOAD:
    RetVal = SettingsSendToFile(SessionID);
    break;

  case TAG_A_NEXTAUXPROGRAM:
    UI_SendAuxProgram((EQUIPMENT_IO) atoi(PostValue[0]), 1, MSG_SEND);
    break;

  case TAG_A_PREVAUXPROGRAM:
    UI_SendAuxProgram((EQUIPMENT_IO) atoi(PostValue[0]), 0, MSG_SEND);
    break;

  case TAG_A_NEXTBOARD:
    UI_SendAnalogBoard(atoi(PostValue[0]), 1, MSG_SEND);
    break;

  case TAG_A_SAMEBOARD:
    UI_SendAnalogBoard((atoi(PostValue[0]) - 1), 1, MSG_SEND); // (value - 1) because board[0] reports as #1
    break;

  case TAG_A_PREVBOARD:
    UI_SendAnalogBoard((atoi(PostValue[0]) - 2), 0, MSG_SEND);
    break;

  case TAG_A_FINDBOARD:
    DiscoverAnalogBoardsRequest(AB_DISCOVER);
    break;

  case TAG_A_SLAVEUPD:
  {
    char dateStr[DATE_LEN+1];
    char timeStr[TIME_LEN+1];
    char humidStr[10];
    char tempStr[10];
    uint8_t amPm;

    if (   StorePostValue("SlaveUpdate", dateStr, ET_STRING, DATE_LEN+1) == SPV_SUCCESS
        && StorePostValue("Time", timeStr, ET_STRING, TIME_LEN+1) == SPV_SUCCESS
        && StorePostValue("AmPm", &amPm, ET_UINT8, NULL) == SPV_SUCCESS
        && StorePostValue("OutsideTemp", tempStr, ET_STRING, 10) == SPV_SUCCESS
        && StorePostValue("OutsideHumid", humidStr, ET_STRING, 10) == SPV_SUCCESS)
    {
      StoreSlaveUpdate(dateStr, timeStr, amPm, tempStr, humidStr);
    }
    break;
  }
  case TAG_A_SHOWPASSWORD:
    UI_SendPassword(MSG_SEND);
    break;

  case TAG_A_RESETIOCONFIG:
    EquipIoInit();
    UI_SendIoConfig(MSG_SEND);
    break;

  case TAG_A_RESETPWMCONFIG:
    EquipPwmInit();
    UI_SendPWMChannels(MSG_SEND);
    break;

  case TAG_A_NODEDELETEALL:
    NetworkNodeDeleteAll();
    UI_SendNetworkNodes(MSG_SEND);
    break;

  case TAG_A_FILESTART:
    if (strcmp(PostValue[0], "1") == 0)
    {
      debug_printf("Start Equipment Descriptions...\r\n");
      FileReceive.Type = FRT_EQUIPDESC;
      FileReceive.Status = FR_PENDING;
    }
    else
    {
      debug_printf("Stop Equipment Descriptions...\r\n");
      FileReceive.Type = FRT_STARTUP;
      FileReceive.Status = FR_IDLE;
    }
    break;

  case TAG_A_BOOTLOADER:
    FileReceive.Type = FRT_FIRMWARE;
    FileReceive.Status = FR_PENDING;
    break;
  }

  return RetVal;
} // end ExecuteActionPosts()

/***************************************************************************

FUNCTION:   CheckCRC()

PURPOSE:    Validates message integrity using the CRC

COMMENTS:

***************************************************************************/
int CheckCRC(char *Msg)
{
  char CrcStr[MSG_CRC_LEN];
  char MsgNoCrc[MSG_RX_BUFFER_SIZE];
  uint32_t Crc;

  GetMessage(Msg, MsgNoCrc);
  GetMessageCRC(Msg, CrcStr);


  Crc = strtoul(CrcStr, NULL, 10);

//  uint32_t CrcCalc = CRC(MsgNoCrc, strlen(MsgNoCrc));
//  debug_printf("Msg:%s - CRC:%s\r\n",MsgNoCrc,CrcStr);
//  debug_printf("Msg:");
//  int var;
//  for (var = 0; var < strlen(MsgNoCrc); ++var) {
//    debug_printf("%02X ",MsgNoCrc[var]);
//  }
//  debug_printf("\r\n");
//
//  debug_printf("CRC:%u - Calc:%u\r\n",Crc,CrcCalc);

  if (Crc == CRC(MsgNoCrc, strlen(MsgNoCrc)))
    return 1;
  else
    return 0;
} // CheckCRC()

/***************************************************************************

FUNCTION:   FreeMsgQueue()

PURPOSE:    Free the message queue

COMMENTS:

***************************************************************************/
void FreeMsgQueue(void)
{
  UI_MESSAGE_QUEUE *FreeNode;
  UI_MESSAGE_QUEUE *NextNode = NULL;

  if (UI_MsgQ == NULL)
    return;

  FreeNode = UI_MsgQ;
  while (FreeNode->Next != NULL)
  {
    NextNode = FreeNode->Next;
    free(FreeNode);
    FreeNode = NextNode;
  }

  free(FreeNode);
  UI_MsgQ = NULL;
} // end FreeMsgQueue()

/***************************************************************************

FUNCTION:   GetMessage()

PURPOSE:    Get the message content without the CRC value

COMMENTS:

***************************************************************************/
void GetMessage(char *RxBuf, char *Msg)
{
  int BufIndex = 1;   // skip the delimiter
  int MsgIndex = 0;

  while (RxBuf[BufIndex] != MSG_TERMINATOR && MsgIndex < MSG_RX_BUFFER_SIZE)
    Msg[MsgIndex++] = RxBuf[BufIndex++];

  Msg[MsgIndex] = 0;   // null terminate the string
} // end GetMessage()

/***************************************************************************

FUNCTION:   GetMessageCRC()

PURPOSE:    Get the message CRC value as a string

COMMENTS:

***************************************************************************/
void GetMessageCRC(char *Msg, char *CrcStr)
{
  char *BufPtr = strchr(Msg, MSG_TERMINATOR);
  int  CrcIndex = 0;

  if (!BufPtr)
  {
    CrcStr[0] = 0;    // terminate the string
    return;
  }
  BufPtr++;  // skip the terminator

  while (*BufPtr != MSG_END_DELIMITER && CrcIndex < MSG_CRC_LEN)
    CrcStr[CrcIndex++] = *BufPtr++;

  CrcStr[CrcIndex] = 0;    // terminate the string
} // end GetMessageCRC()

/***************************************************************************

FUNCTION:   GetMessageTag()

PURPOSE:    Get the message tag

COMMENTS:

***************************************************************************/
void GetMessageTag(char *Msg, char *Tag)
{
  int MsgIndex = 1;    // skip the start delimiter
  int TagIndex = 0;

  while (Msg[MsgIndex] != MSG_DIVIDER && TagIndex < MSG_MAX_TAG_LEN)
    Tag[TagIndex++] = Msg[MsgIndex++];

  Tag[TagIndex] = 0;    // terminate the string
} // end GetMessageTag()

/***************************************************************************

FUNCTION:   GetMessageTag2()

PURPOSE:    Get the message tag

COMMENTS:

***************************************************************************/
void GetMessageTag2(char *Msg, char *Tag)
{
  int MsgIndex = 0;    // don't skip the first element
  int TagIndex = 0;

  while (Msg[MsgIndex] != MSG_DIVIDER && TagIndex < MSG_MAX_TAG_LEN)
  {
    Tag[TagIndex++] = Msg[MsgIndex++];
  }

  Tag[TagIndex] = 0;    // terminate the string
} // end GetMessageTag2()

/***************************************************************************

FUNCTION:   GetNumPostItems()

PURPOSE:    Get the number of post items

COMMENTS:

***************************************************************************/
int GetNumPostItems(void)
{
  return PostItemIndex;
} // end GetNumPostItems();

/***************************************************************************

FUNCTION:   GetPostValueByField()

PURPOSE:    Gets the field value for a passed field name from the post data

COMMENTS:

***************************************************************************/
int GetPostValueByField(char *field, char *value, size_t length)
{
  int  i = 0;

  while (i < PostItemIndex)
  {
    if (strcmp(PostTag[i], field) == 0)
    {
      StringCopy(value, PostValue[i], length);
      return 1;
    }
    i++;
  }

  return 0;
} // end GetPostValueByField();

/***************************************************************************

FUNCTION:   GetReplyTag()

PURPOSE:    Get the message reply tag

COMMENTS:

***************************************************************************/
void GetReplyTag(char *Msg, char *Tag)
{
  int  TagIndex = 0;
  char *BufPtr = strchr(Msg, MSG_DIVIDER);

  if (!BufPtr)
  {
    Tag[0] = 0;    // terminate the string
    return;
  }
  BufPtr++;  // skip the divider

  while (   *BufPtr != MSG_TERMINATOR
         && *BufPtr != MSG_FIELD_DELIMITER
         && TagIndex < MSG_MAX_TAG_LEN)
  {
    Tag[TagIndex++] = *BufPtr++;
  }

  Tag[TagIndex] = 0;    // terminate the string
} // end GetReplyTag()

/***************************************************************************

FUNCTION:   ParsePost()

PURPOSE:    Parse the post message into tags (fields) and values

COMMENTS:

***************************************************************************/
void ParsePost(void)
{
  int BufIndex = 1;   // skip the message delimiter
  int StrIndex = 0;

  memset(PostTag, 0, sizeof(PostTag));
  memset(PostValue, 0, sizeof(PostValue));
  PostItemIndex = 0;

  while (   UI_Message.RxBuffer[BufIndex] != MSG_TERMINATOR
         && UI_Message.RxBuffer[BufIndex] != 0
         && PostItemIndex < MSG_MAX_POST_ITEMS
         && BufIndex < MSG_RX_BUFFER_SIZE)
  {
    while (   UI_Message.RxBuffer[BufIndex] != MSG_DIVIDER
           && UI_Message.RxBuffer[BufIndex] != MSG_TERMINATOR
           && UI_Message.RxBuffer[BufIndex] != 0)
    {
      if (StrIndex < MSG_MAX_TAG_LEN-1)
      {
        PostTag[PostItemIndex][StrIndex] = UI_Message.RxBuffer[BufIndex];
        StrIndex++;
      }

      BufIndex++;
    }

    PostTag[PostItemIndex][StrIndex] = 0;    // terminate the string
    BufIndex++;   // skip the divider
    StrIndex = 0;

    while (   UI_Message.RxBuffer[BufIndex] != MSG_FIELD_DELIMITER
           && UI_Message.RxBuffer[BufIndex] != MSG_TERMINATOR
           && UI_Message.RxBuffer[BufIndex] != 0)
    {
      if (StrIndex < MSG_MAX_VALUE_LEN-1)
      {
        PostValue[PostItemIndex][StrIndex] = UI_Message.RxBuffer[BufIndex];
        StrIndex++;
      }

      BufIndex++;
    }

    PostValue[PostItemIndex][StrIndex] = 0;    // terminate the string
    PostItemIndex++;   // move to the next value
    StrIndex = 0;

    if (UI_Message.RxBuffer[BufIndex] == MSG_FIELD_DELIMITER)
    {
      BufIndex++;   // skip the field delimiter
    }
  }
} // ParsePost()

/***************************************************************************

FUNCTION:   ProcessMsgQueue()

PURPOSE:    Remove a message from the queue and send it

COMMENTS:

***************************************************************************/
void ProcessMsgQueue(void)
{
  UI_MESSAGE_QUEUE *MsgQNode = NULL;

  if (   UI_Message.State == MSG_STATE_WAITING
      && UI_MsgQ != NULL
      && MessagingStatus == MS_RESPONDING)
  {
    MsgQNode = UI_MsgQ;
    AddMsgDelimiters(MsgQNode->Buffer);
    UI_Message.State = MSG_STATE_SEND;

    UI_MsgQ = MsgQNode->Next;
    free(MsgQNode);
  }

  if (UI_Message.State == MSG_STATE_SEND)
  {
    SendMessage();
  }
} // end ProcessMsgQueue()

/***************************************************************************

FUNCTION:   ProcessPost()

PURPOSE:    Parse the post message, store and save the settings, build and
            send the 're-post' message

COMMENTS:   SendDataLoadStatus() is not called for ClearAlarm and messaging
            intialization because PostSave() will not get called on the LTX to
            properly change the DataLoaded status back to waitLTX because it's
            not a standard save post.

***************************************************************************/
void ProcessPost(int PostType, int PostIndex)
{
  char PostReply[MSG_TX_BUFFER_SIZE];
  int  Status;

  ParsePost();
  StorePostValue("SessionID", &SessionID, ET_INT32, NULL);

  switch (PostType)
  {
  case PT_ACTION:
    // reply to the LTX/UI post
    if (PostIndex != TAG_A_BOOTLOADER)
    {
      snprintf(PostReply, MSG_TX_BUFFER_SIZE, "%s=%s", PostTag[0], PostValue[0]);
      SendMsgAndWaitForResponse(PostReply, UI_DELAY_STANDARD);
    }

    // execute the request
    Status = ExecuteActionPosts(PostIndex);

    // tell the LTX/UI the request is complete
    if (   PostIndex != TAG_A_CLEARALARM    // see comment above
        && PostIndex != TAG_A_FILESTART
        && PostIndex != TAG_A_BOOTLOADER)
    {
      if (Status == 1)
        SendDataLoadStatus("true");    // normal completion
      else
        SendDataLoadStatus("none");    // no available log data
    }
    break;

  case PT_PROGRAM:
    // store the data from the LTX/UI post
    (*(UI_ProgramPosts[PostIndex].StoreData))();

    if (   PostIndex != TAG_P1_LTXINIT
        && PostIndex != TAG_EQUIPDESC   // these are used for getting the translated descriptions and column headers
        && PostIndex != TAG_QUERYTAG    // from the http server and are not system settings, therefore SaveSettings()
        && PostIndex != TAG_SYSLOGREC   // does not need to be called
        && PostIndex != TAG_SYSLOGEQUIP
        && PostIndex != TAG_SYSLOGREMOTE
        && PostIndex != TAG_PIDLOG
        && PostIndex != TAG_SYSMODE
        && PostIndex != TAG_BOARDLABEL
        && PostIndex != TAG_TEMPSENSOR
        && PostIndex != TAG_HUMIDSENSOR
        && PostIndex != TAG_BAYLABEL)
    {
      RequestSettingsSave(SR_REQUEST, ACTIVE_SETTINGS);
    }

    // handle special cases
    if (   PostIndex == TAG_P1_CURFANSPEED
        || PostIndex == TAG_P2_IOCONFIG
        || PostIndex == TAG_P2_NODEADD
        || PostIndex == TAG_P2_NODEDELETE
        || PostIndex == TAG_P2_NODEDISCOVER
        || PostIndex == TAG_P2_PIDLOG)
    {
      // reply to the button/post message to keep LTX messaging happy
      // before sending actual data message (Reply) below
      snprintf(PostReply, MSG_TX_BUFFER_SIZE, "%s=%s", PostTag[0], PostValue[0]);
      SendMsgAndWaitForResponse(PostReply, UI_DELAY_STANDARD);
    }

    // reply to the LTX/UI post
    (*(UI_ProgramPosts[PostIndex].Reply))(MSG_SEND);

    // tell the LTX/UI the data transfer is complete
    if (   PostIndex != TAG_P1_LTXINIT  // see comment above
        && PostIndex != TAG_EQUIPDESC
        && PostIndex != TAG_PWMDESC
        && PostIndex != TAG_QUERYTAG
        && PostIndex != TAG_SYSLOGREC
        && PostIndex != TAG_SYSLOGEQUIP
        && PostIndex != TAG_SYSLOGREMOTE
        && PostIndex != TAG_PIDLOG
        && PostIndex != TAG_SYSMODE
        && PostIndex != TAG_BOARDLABEL
        && PostIndex != TAG_TEMPSENSOR
        && PostIndex != TAG_HUMIDSENSOR
        && PostIndex != TAG_BAYLABEL)
    {
      SendDataLoadStatus("true");
    }
    break;

  case PT_EQUIP:
    // store the data from the LTX/UI post
    (*(UI_EquipPosts[PostIndex].StoreData))(UI_EquipPosts[PostIndex].SettingsPtr);
    RequestSettingsSave(SR_REQUEST, ACTIVE_SETTINGS);

    // reply to the button/post message to keep LTX messaging happy
    // before sending actual equipment status message below
    snprintf(PostReply, MSG_TX_BUFFER_SIZE, "%s=%s", PostTag[0], PostValue[0]);
    SendMsgAndWaitForResponse(PostReply, UI_DELAY_STANDARD);

    // reply to the LTX/UI post
    (*(UI_EquipPosts[PostIndex].Reply))(MSG_SEND);

    // tell the LTX/UI the data transfer is complete
    SendDataLoadStatus("true");
    break;
  }
} // end ProcessPost()

/***************************************************************************

FUNCTION:   SendDataLoadStatus()

PURPOSE:    Send the DataLoadStatus

COMMENTS:

***************************************************************************/
void SendDataLoadStatus(char *Status)
{
  char ReplyMsg[50];

  snprintf(ReplyMsg,
           sizeof(ReplyMsg),
           "DataLoadStatus=%s,%d",
           Status,
           SessionID);

  SendMsgAndWaitForResponse(ReplyMsg, UI_DELAY_STANDARD);
} // end SendDataLoadStatus()

/***************************************************************************

FUNCTION:   ProcessUIMessage()

PURPOSE:    Process the incoming message

COMMENTS:   The ARM tries to defend itself from nested posts, i.e. two browsers
            posting at the same time.  The LTX is the primary defense against
            nested posts.  If a second post is executed before the ARM finishes
            servicing the previous post, the LTX notifies the user that the ARM
            is busy.

            The ARM defends itself anyway ignoring any post it receives while
            it is servicing a previous post - it reports the error out the
            debug port.

***************************************************************************/
void ProcessUIMessage(void)
{
  char MsgTag[MSG_MAX_TAG_LEN+1] = "";
  char ReplyTag[MSG_MAX_TAG_LEN+1] = "";
  int  PostIndex = 0;
  int  PostType = 0;

  GetMessageTag(UI_Message.RxBuffer, MsgTag);

  if (   strcmp(MsgTag, "ACK") == 0
      || strcmp(MsgTag, "KILL") == 0)
  {
    if (UI_Message.State == MSG_STATE_ACK)
    {
      GetReplyTag(UI_Message.RxBuffer, ReplyTag);
      if (strcmp(ReplyTag, UI_Message.Tag) == 0)
      {
        if (strcmp(MsgTag, "KILL") == 0)
        {
          ParsePost();
          StorePostValue("SessionID", &KillMessage, ET_INT8, NULL);
        }

        UI_Message.State = MSG_STATE_WAITING;
      }
      else
        SendNAK(UI_Message.Tag);
    }

    UI_Message.NAKcount = 0;
  }
  else if (strcmp(MsgTag, "NAK") == 0)
  {
    if (UI_Message.NAKcount < 5)
    {
      UI_Message.NAKcount++;
      UI_Message.State = MSG_STATE_RETRY;    // retry
    }
    else
    {
      UI_Message.NAKcount = 0;
      UI_Message.State = MSG_STATE_WAITING; // give up
    }
    UI_Message.OutErrors++;
  }
  else if (strcmp(MsgTag, "RTS") == 0)
  {
    if (ProcessingLtxMessage == 1)    // guard against nested posts
    {
      debug_printf("NAK LTX RTS\r\n");
      SendNAK("RTS");
    }
    else
    {
      SendMsgAndNoWait("ACK=RTS");
    }

    UI_Message.NAKcount = 0;
  }
  else  // post received from LTX (or garbage)
  {
    if (   CheckCRC(UI_Message.RxBuffer) == 1
        && (PostIndex = ValidatePostTag(MsgTag, &PostType)) >= 0)
    {
      if (ProcessingLtxMessage == 1)    // guard against nested posts
      {
        // report the nested post and then NAK it
        debug_printf("NAK Nested Message - (%s)\r\n", MsgTag);
        SendNAK(UI_Message.Tag);
      }
      else
      {
//        MessagingStatus = MS_RESPONDING;
        ProcessingLtxMessage = 1;
        ProcessPost(PostType, PostIndex);
        ProcessingLtxMessage = 0;
      }

      UI_Message.NAKcount = 0;
    }
    else
      SendNAK(UI_Message.Tag);
  }
} // end ProcessUIMessage()

/***************************************************************************

FUNCTION:   QueueMessage()

PURPOSE:    Add a message to the message queue

COMMENTS:   The ARM won't queue messages if LTX/ARM messaging is not initialized

***************************************************************************/
void QueueMessage(char *Msg)
{
  UI_MESSAGE_QUEUE *MsgQNode;
  UI_MESSAGE_QUEUE *Traverse = NULL;

  if (MessagingStatus != MS_RESPONDING)
    return;

  // malloc and load the node
  MsgQNode = malloc(sizeof(UI_MESSAGE_QUEUE));
  if (MsgQNode == 0)
  {
    debug_printf("Malloc Failure - QueueMessage\r\n");
    return;
  }

  StringCopy(MsgQNode->Buffer, Msg, MSG_TX_BUFFER_SIZE);
  MsgQNode->Next = NULL;

  // add the node to the queue
  if (UI_MsgQ == NULL)
    UI_MsgQ = MsgQNode;
  else
  {
    Traverse = UI_MsgQ;
    while (Traverse->Next != NULL)
      Traverse = Traverse->Next;
    Traverse->Next = MsgQNode;
  }
} // UI_QueueMessage()

/***************************************************************************

FUNCTION:   SendMessage()

PURPOSE:    Send message to Lantronix/UI

COMMENTS:

***************************************************************************/
void SendMessage(void)
{
  GetMessageTag(UI_Message.TxBuffer, UI_Message.Tag);
  Usart_SendPacket(UI_Message.TxBuffer, strlen(UI_Message.TxBuffer));
#ifdef QEMU_BUILD
  /* In QEMU, skip ACK wait — no external UI to send ACKs.
   * Keep MSG_STATE_WAITING so ProcessMsgQueue can send the next message. */
  UI_Message.State = MSG_STATE_WAITING;
#else
  UI_Message.State = MSG_STATE_ACK;
#endif
} // end SendMessage()

/***************************************************************************

FUNCTION:   SendMsgAndNoWait()

PURPOSE:    Add message delimiters and send message

COMMENTS:   The ARM won't send messages if LTX/ARM messaging is not initialized

***************************************************************************/
void SendMsgAndNoWait(char *Msg)
{
  char MsgTag[MSG_MAX_TAG_LEN+1] = "";

  GetMessageTag2(Msg, MsgTag);

  if (   MessagingStatus != MS_RESPONDING
      && strcmp(MsgTag, "ACK") != 0
      && strcmp(MsgTag, UI_ProgramPosts[TAG_P1_LTXINIT].Tag) != 0)
  {
   return;
  }

  AddMsgDelimiters(Msg);
  SendMessage();
} // end SendMsgAndNoWait()

/***************************************************************************

FUNCTION:   SendMsgAndWaitForResponse()

PURPOSE:    Send message and wait for respond

COMMENTS:   The ARM won't send messages if LTX/ARM messaging is not initialized

***************************************************************************/
int SendMsgAndWaitForResponse(char *Msg, unsigned int Delay)
{
  int Status = 0;
  char MsgTag[MSG_MAX_TAG_LEN+1] = "";

  GetMessageTag2(Msg, MsgTag);

  if (   MessagingStatus != MS_RESPONDING
      && strcmp(MsgTag, UI_ProgramPosts[TAG_P1_LTXINIT].Tag) != 0)
  {
    return 1;
  }

  SendMsgAndNoWait(Msg);
  Status = WaitForMsgResponse(Delay);

  return(Status);
} // end SendMsgAndWaitForResponse()

/***************************************************************************

FUNCTION:   SendNAK()

PURPOSE:    Send a NAK to the LTX

COMMENTS:

***************************************************************************/
void SendNAK(char *MsgTag)
{
  char NakMsg[MSG_MAX_TAG_LEN+1+MSG_MAX_TAG_LEN+1] = "";
  char SaveTag[MSG_MAX_TAG_LEN+1] = "";
  char SaveTxBuffer[MSG_TX_BUFFER_SIZE+1] = "";

  // build the NAK reply
  snprintf(NakMsg, sizeof(NakMsg), "NAK=%s", MsgTag);
  debug_printf("SendNAK - NakMsg:%s\r\n", NakMsg);

  // save the current tag & message
  StringCopy(SaveTag, UI_Message.Tag, sizeof(SaveTag));
  StringCopy(SaveTxBuffer, UI_Message.TxBuffer, sizeof(SaveTxBuffer));

  Usart_FlushBuffer();

  // send the NAK message
  SendMsgAndNoWait(NakMsg);

  // restore the previous tag & message
  StringCopy(UI_Message.Tag, SaveTag, sizeof(UI_Message.Tag));
  StringCopy(UI_Message.TxBuffer, SaveTxBuffer, sizeof(UI_Message.TxBuffer));

  if (strcmp(MsgTag, "RTS") == 0)
  {
    UI_Message.State = MSG_STATE_RETRY;
  }

//  UI_Message.State = MSG_STATE_SEND;  // retry
  UI_Message.InErrors++;
} // end SendNAK()

/***************************************************************************

FUNCTION:   WaitForMsgResponse()

PURPOSE:    Wait for response from Lantronix/UI

COMMENTS:   The loop is set up (i < 12 with i % 4) to send three NAKs for
            the original message and then send a retry message with three
            NAKs and then send the last retry with up to 3 NAKs.  So each
            message can be sent three times with three NAKs (if the ACK
            itself is bad) each time before the message is considered failed.

            If FileReceive.Status != FR_IDLE then drop out of the loops as
            quickly as possible to initiate the file transfer. The status
            is probably FR_PENDING because the file transfer request was
            recieved during a multi-message transfer. The multi-message
            needs to finish (along with any subsequence multi-messages) to
            return to ThreadUIUpdate() to set the status from FR_PENDING
            to FR_RECEIVING to allow ThreadFileReceive() to run and receive
            the file.

***************************************************************************/
int WaitForMsgResponse(unsigned int Delay)
{
  int i = 1;
  unsigned int StartTimeMsg;

  while (   i < 12
         && UI_Message.State == MSG_STATE_ACK
         && FileReceive.Status == FR_IDLE)    // drop out if FR_PENDING
  {
    StartTimeMsg = uptime_ms;
    while (  (uptime_ms - StartTimeMsg) < (Delay * 1000U)
           && UI_Message.State == MSG_STATE_ACK
           && FileReceive.Status == FR_IDLE)  // drop out if FR_PENDING
    {
      vTaskDelay(10 / portTICK_RATE_MS);

      // check for message from LTX/UI
      if (Usart_CharsBuffered())
      {
        if (Usart_ProcessBuffer(UI_Message.RxBuffer) == 1)
        {
          ProcessUIMessage();
        }
      }

      // reset the watchdog external timer
      ThreadMonitorUpdate(TM_UI_UPDATE);
    }

    if (UI_Message.State != MSG_STATE_WAITING)
    {
      if (UI_Message.State != MSG_STATE_RETRY && i % 4 != 0)
      {
        SendNAK(UI_Message.Tag);
      }
      else
      {
        SendMessage();   // retry
      }
    }
    i++;
  }

  if (UI_Message.State == MSG_STATE_ACK)
  {
    UI_Message.State = MSG_STATE_WAITING;
    debug_printf("Msg Fail:%s\r\n", UI_Message.Tag);

    if (   ++FailedMessages > 6
        && FileReceive.Status == FR_IDLE)   // don't change the status if FR_PENDING
    {
      MessagingStatus = MS_NOT_RESPONDING;
    }

    // if a message fails during a log transmission, abort the transmission
    KillMessage = SessionID;
    return 0;
  }

  FailedMessages = 0;
  return 1;
} // end WaitForMsgResponse()

/***   End Of File   ***/
