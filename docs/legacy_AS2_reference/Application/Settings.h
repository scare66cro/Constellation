/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     Settings.h

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:

COMMENTS:

***************************************************************************/

#ifndef SETTINGS_H
#define SETTINGS_H

/*** include files ***/

// FreeRTOS
#include "FreeRTOS.h"
#include "semphr.h"

// Gellert
#include "Analog_Input.h"
#include "Controls.h"
#include "DataExc.h"
#include "PWM.h"
#include "SerialShift.h"
#include "SettingsTypes.h"
#include "Warnings.h"

/*** defines ***/

#define BOARD_TYPE                "AS2"
#define ARM_FIRMWARE_VERSION      "1.07"
#define SETTINGS_VERSION          "3.19"   // NOTE: only rev this when SYSTEM_SETTINGS length changes
#define NETMONITOR_VERSION        1        // 1 = on, 0 = off
#define SETTINGS_FILE_TAG         "ASCII Settings File"

/*** typedefs and structures ***/

// system settings
typedef struct SytemSettings
{
  char     SettingsVersion[VERSION_LEN+1];   // Settings structure version
  char     ArmVersion[VERSION_LEN+1];        // software version
  char     DST;                              // Daylight Saving Time
  uint16_t HttpPort;
  char     NetMonitorMode;
  uint8_t  MultiviewSessions;                // number of systems to tile in multi-view mode
  char     PublicIP[IP_ADD_LEN+1];

  // basic setup page
  char    StorageName[SITE_ID_LEN+1]; // facility
  char    TempType;                   // Fahrenheit/Celcius
  char    MasterSlave;
  char    MasterIP[IP_ADD_LEN+1];
  char    HomePage[HOMEPAGE_LEN];     // select
  uint8_t SystemMode;                 // 0 = Potato, 1 = Onion
  char    Language;                   // select
  char    FactoryBd[PASSWORD_LEN+1];
  char    LoginPw[PASSWORD_LEN+1];
  uint8_t LocalLogin;                 // 0 = No, 1 = Yes
  uint8_t Animations;                 // 0 = No, 1 = Yes

  // service info page
  char DealerName[NAME_LEN+1];
  char DealerPhone[PHONE_LEN+1];
  char TechName[NAME_LEN+1];
  char TechPhone[PHONE_LEN+1];

  // system runtimes page
  char RunTimes[48];                // 48 elements represent each half hour in 24 hours

  // miscellaneous parameters page
  float HeatTempThresh;             // degrees
  uint8_t KbPref;                   // select

  uint8_t LightsFailUnits;          // select (mins or hours)
  uint8_t RemoteOff[NUM_REMOTE_OFF];

  PLENUM_PARAMS Plenum;
  OUTSIDE_AIR_CTRL OutsideAir;
  FAN_CTRL Fan;
  DOOR_CTRL Door;
  REFRIG_CTRL Refrig;
  CLIMACELL_CTRL Climacell;
  CAVITYHEAT_CTRL CavityHeat;
  CURE_CTRL Cure;
  BURNER_CTRL Burner;
  HUMID_CONTROL HumidCtrl[NUM_HUMIDIFIERS];
  CO2_CTRL Co2;
  RAMP_CTRL Ramp;
  FANBOOST_CTRL FanBoost;
  LOG_CTRL Log;

  FAILURE_MODE Failure[NUM_FAILURES];
  NETWORK_NODES Node[NUM_NETWORK_NODES];
  PASSWORDS User[NUM_PASSWORDS];
  ANALOG_BOARD AnalogBoard[ANALOG_BOARDS_PER_SYSTEM];
  LOAD_MONITOR LoadMonitor;
  PWM_CONFIG PWM[PWM_TOTAL_EQ];
  IO_CONFIG EquipIo[EQ_TOTAL_IO];
  AUX_PROGRAM AuxProgram[NUM_AUX_OUTPUTS];
  EMAIL_SETUP Email;

  // email alerts setup page
  char  AlertsToSend[NUM_WARNINGS+1];

  unsigned int SaveSeq;     // A/B dual-bank monotonic sequence number (higher = newer)
  unsigned int CRC;         // must remain the last field
} SYSTEM_SETTINGS;

/*** external variables ***/

extern SYSTEM_SETTINGS Settings;
extern SAVE_SETTINGS_INFO SaveSettingsRequest;

/*** external functions ***/

extern unsigned int CRC(char *buf, int len);
extern void StringCopy(char *buffer, const char *string, int max);
extern void EquipIoInit(void);
extern void EquipPwmInit(void);
extern void GetFactoryDefault(void);
extern void NetworkNodeAdd(char *ID, char *IP, char *PublicIP, short Port);
extern void NetworkNodeDelete(char *IP);
extern void NetworkNodeDeleteAll(void);
extern unsigned int ReadSettings(void *SettingsPtr, int Size, int Type);
extern int SaveSettings(SYSTEM_SETTINGS_TYPE Type);
extern void RequestSettingsSave(SAVE_SETTINGS_REQUEST request, SYSTEM_SETTINGS_TYPE Type);
extern int SettingsSendToFile(int SessionID);
extern int SettingsSendToLocalFlash(void);
extern int SettingsRestoreFromLocalFlash(void);
extern void Settings_Init(void);
extern int UserAcctAuth(char *ID, char *Password);

#endif

/***   End Of File   ***/
