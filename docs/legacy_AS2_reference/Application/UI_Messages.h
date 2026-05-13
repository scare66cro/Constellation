/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     UI_Messages.h

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:

COMMENTS:

***************************************************************************/

#ifndef UI_MESSAGES_H
#define UI_MESSAGES_H

/*** include files ***/

/*** defines ***/

#define MSG_QUEUE             1
#define MSG_SEND              0

#define NO_SESSIONID          -1

/*** typedefs and structures ***/

/*** external variables ***/

/*** external functions ***/

extern void MultiMsgAdd(char *msg, char *tag, char *item, int add);

extern void AnalogBoardPostReply(int SendOption);
extern void AuxProgramPostReply(int SendOption);

extern void UI_SendAccounts(int SendOption);
extern void UI_SendAirCure(int SendOption);
extern void UI_SendAllSettings(void);
extern void UI_SendAnalogBoard(int board, int direction, int queue);
extern void UI_SendAuxProgram(EQUIPMENT_IO eqIndex, int direction, int SendOption);
extern void UI_SendAuxSwitches(int SendOption);
extern void UI_SendAvailableIo(int SendOption);
extern void UI_SendBasicSetup(int SendOption);
extern void UI_SendBurner(int SendOption);
extern void UI_SendClimacell(int SendOption);
extern void UI_SendClimacellTimes(int SendOption);
extern void UI_SendCo2(int SendOption);
extern void UI_SendDateTime(int SendOption);
extern void UI_SendDebug(char *debug);
extern void UI_SendDoor(int SendOption);
extern void UI_SendEmail(int SendOption);
extern void UI_SendEmailAlertFlags(int SendOption);
extern void UI_SendEquipStatus(int SendOption);
extern void UI_SendEquipTranslateAck(int SendOption);
extern void UI_SendFailures(int SendOption);
extern void UI_SendFailures2(int SendOption);
extern void UI_SendFanBoost(int SendOption);
extern void UI_SendFanDailyRun(int SendOption);
extern void UI_SendFanTotalRun(int SendOption);
extern void UI_SendFanSpeed(int SendOption);
extern void UI_SendGraphFavorites(int SendOption);
extern void UI_SendHttpPort(int SendOption);
extern void UI_SendHumCtrl(int SendOption);
extern void UI_SendHumidModes(void);
extern void UI_SendIoConfig(int SendOption);
extern void UI_SendIoDefinition(int SendOption);
extern void UI_SendLoadMonitor(int SendOption);
extern void UI_SendLtxInit(int SendOption);
extern void UI_SendMain(int SendOption);
extern void UI_SendMisc(int SendOption);
extern void UI_SendMode(void);
extern void UI_SendMultiEnd(char *Type);
extern void UI_SendMultiHdr(char *TagList, int SessionID);
extern void UI_SendNetMonMode(int SendOption);
extern void UI_SendNetworkNodes(int SendOption);
extern void UI_SendOutsideAir(int SendOption);
extern void UI_SendPassword(int SendOption);
extern void UI_SendPgmLevel(int SendOption);
extern void UI_SendPlenSetPoints(int SendOption);
extern void UI_SendPWMChannels(int SendOption);
extern void UI_SendRampRate(int SendOption);
extern void UI_SendRuntimes(int SendOption);
extern void UI_SendRefrig(int SendOption);
extern void UI_SendSensorData(void);
extern void UI_SendSensorLabels(void);
extern void UI_SendService(int SendOption);
extern void UI_SendSettingsAck(int SendOption);
extern void UI_SendSlaveUpdate(int SendOption);
extern void UI_SendTempDevAlarms(int SendOption);
extern void UI_SendUserLogSettings(int SendOption);
extern void UI_SendVersions(void);
extern void UI_SendWarnings(int ForceSend);

extern void UI_SendFileTransferAck(int start);

#endif

/***   End Of File   ***/
