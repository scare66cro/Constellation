/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     SettingsVxx.h

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:  Previous settings structure definitions - used for conversions

COMMENTS:

***************************************************************************/

#ifndef SETTINGSVXX_H
#define SETTINGSVXX_H

/*** include files ***/

#include "Settings.h"

/*** defines ***/

#define SETTINGS_VERSION_V34          "3.4"
#define SETTINGS_VERSION_V35          "3.5"
#define SETTINGS_VERSION_V36          "3.6"
#define SETTINGS_VERSION_V37          "3.7"
#define SETTINGS_VERSION_V39          "3.9"
#define SETTINGS_VERSION_V312         "3.12"
#define SETTINGS_VERSION_V316         "3.16"

#define EMAIL_TO_LEN_V28               60
#define EMAIL_FROM_LEN_V28             35

#define NUM_WARNINGS_V34               78
#define NUM_WARNINGS_V35               78
#define NUM_WARNINGS_V36               78
#define NUM_WARNINGS_V37               78
#define NUM_WARNINGS_V39               80
#define NUM_WARNINGS_V312              82
#define NUM_WARNINGS_V316              82

typedef struct {
  char  ID[SITE_ID_LEN+1];
  char  IP[IP_ADD_LEN+1];
  short HttpPort;
} NETWORK_NODES_V35;

/**************************************************************************
 * v3.4 definition
***************************************************************************/
// system settings
//typedef struct {
//  char  SettingsVersion[VERSION_LEN+1];   // Settings structure version
//  char  ArmVersion[VERSION_LEN+1];        // software version
//  char  DST;                              // Daylight Saving Time
//  char  PrevFanSpeed;                     // % (stores the cooling mode fan speed)
//  unsigned short HttpPort;
//  char  NetMonitorMode;
//  char  Co2PurgeRefrigThresh;             // %
//  char  MultiviewSessions;                // number of systems to tile in multi-view mode
//  char  Unused[10];                       // (not used) was display IP address, now using as reserved space
//
//  // logs
//  short UserLogInterval;            // mins
//  char  UserLogWrap;
//  char  PIDLogWrap;
//  char  LogPIDDoor;
//  char  LogPIDRefrig;
//  char  GraphFavorites[MSG_TX_CGIVAR_SIZE];
//
//  // basic setup page
//  char  StorageName[SITE_ID_LEN+1]; // facility
//  char  TempType;                   // Fahrenheit/Celcius
//  char  MasterSlave;
//  char  MasterIP[IP_ADD_LEN+1];
//  char  HomePage[HOMEPAGE_LEN];     // select
//  char  SystemMode;                 // 0 = Potato, 1 = Onion
//  char  Language;                   // select
//  char  FactoryBd[PASSWORD_LEN+1];
//
//  // password page
//  PASSWORDS User[NUM_PASSWORDS];
//
//  // analog board setup page
//  ANALOG_BOARD AnalogBoard[ANALOG_BOARDS_PER_SYSTEM];
//
//  // fresh air door setup page
//  short DoorP;                      // PID values
//  short DoorI;
//  short DoorD;
//  short DoorU;
//  short DoorActuatorTime;           // secs (1-500)
//  char  CoolAirCycle;               // mins (1-30)
//
//  // refrigeration setup page
//  REFRIG Refr[NUM_REFRIG_STAGES];
//  short RefrP;                      // PID values
//  short RefrI;
//  short RefrD;
//  short RefrU;
//  char  RefrPurge;                  // select
//
//  // climacell setup page
//  char  ClimacellEff;               // %
//  short ClimP;                      // PID values
//  short ClimI;
//  short ClimD;
//  short ClimU;
//  short Altitude;
//  char  AltitudeType;               // feet/meters
//
//  // burner setup page (onion)
//  char  BurnerOn;                   // %
//  char  BurnerLow;                  // %
//  short BurnerP;                    // PID values
//  short BurnerI;
//  short BurnerD;
//  short BurnerU;
//
//  // failures 1 setup page
//  char FanFailMode;                 // select
//  char FanFailTimer;                // mins (1-10)
//  char ClimacellFailMode;           // select
//  char ClimacellFailTimer;          // mins (1-120)
//  char RefrFailMode;                // select
//  char RefrFailTimer;               // mins (1-15)
//  char RefrRunFailMode;             // select
//  HUMID_MODE Humid[NUM_HUMIDIFIERS];
//  char AuxFailMode;                 // select
//  char AuxFailTimer;                // mins (1-15)
//  char HeatFailMode;                // select
//  char HeatFailTimer;               // mins (1-15)
//  char CavHeatFailMode;             // select
//  char CavHeatFailTimer;            // mins (1-10)
//  char AirRestFailMode;             // unused
//  char BurnerFailMode;              // select
//  char BurnerFailTimer;             // mins (1-10)
//
//  // failures 2 setup page
//  char  PlenSenFailMode;            // select
//  char  PlenSenFailTimer;           // mins (1-180)
//  float PlenSenDiff;                // degrees
//  char  OutAirFailMode;             // select
//  char  OutAirFailTimer;            // mins (1-180)
//  char  OutHumFailMode;             // select
//  char  OutHumFailTimer;            // mins (1-180)
//  char  PlenHumFailMode;            // select
//  char  PlenHumFailTimer;           // mins (1-180)
//  char  LowHumSet;                  // %
//  char  HighCo2FailMode;            // select
//  char  HighCo2FailTimer;           // mins (1-180)
//  int   HighCo2Set;                 // ppm
//
//  // controller access page
//  NETWORK_NODES_V35 Node[NUM_NETWORK_NODES];
//
//  // service info page
//  char DealerName[NAME_LEN+1];
//  char DealerPhone[PHONE_LEN+1];
//  char TechName[NAME_LEN+1];
//  char TechPhone[PHONE_LEN+1];
//
//  // plenum setup page
//  float        PlenTempSet;         // degrees
//  char         PlenHumSet;          // %
//  unsigned int FanDailyRunTime;     // secs (displayed as hours)
//  unsigned int FanDailyCounter;     // counter (secs)
//  unsigned int FanDailyDay;         // day value for daily reset
//  unsigned int FanTotalRunTime;     // secs (displayed as hours)
//  unsigned int FanTotalCounter;     // counter (secs)
//  char         HumSetpointRef;      // 0 - plenum, 1 - return air (onion)
//
//  // outside air cooling control page
//  float OutAirDiff;                 // degrees
//  char  OutAirAboveBelow;           // select
//  char  OutAirTempRef;              // select
//  char  CalcHumMax;                 // % (onion)
//
//  // plenum temp deviation alarm page
//  float PlenLowAlarmTemp;           // degrees
//  char  PlenLowAlarmTimer;          // mins
//  float PlenHighAlarmTemp;          // degrees
//  char  PlenHighAlarmTimer;         // mins
//
//  // system runtimes page
//  char RunTimes[48];                // 48 elements represent each half hour in 24 hours
//  char CureRunTimes[48];            // cure mode runtimes
//
//  // frequency drive control page
//  char  MaxFanSpeed;                // %
//  char  MinFanSpeed;                // %
//  char  RefrFanSpeed;               // %
//  char  RecircFanSpeed;             // %
//  char  UpdFanSpeed;                // hours
//  float TempDiff;                   // degrees
//  char  TempRef1;                   // select
//  char  TempRef2;                   // select
//  char  FanUpdMode;                 // select (onion)
//
//  // ramp rate page
//  float RampUpdTemp;                // degrees
//  char  RampUpdHours;               // hours
//  float RampTempDiff;               // degrees
//  char  RampTempRef;                // select
//  float RampTargetTemp;             // degrees
//
//  // humidity control page
//  char HumMode;                     // select
//  char HumType;                     // select
//  HUMID_CYCLE HumDutyCycle[3];
//
//  // high CO2 level purge page
//  short        Co2Set;              // ppm (1-10000)
//  char         Co2CylceTime;        // hours between purges
//  char         Co2PurgeMode;        // select
//  float        Co2PurgeMaxTemp;     // degrees
//  float        Co2PurgeMinTemp;     // degrees
//  char         Co2PurgeDur;         // mins
//  unsigned int Co2LastPurge;        // counter (secs)
//  unsigned int Co2PurgeStart;       // counter (secs)
//
//  // miscellaneous parameters page
//  char  RefrMode;                   // select
//  char  DefrostInt;                 // hours
//  char  DefrostDur;                 // mins
//  float HeatTempThresh;             // degrees
//  float RefrigLimit;                // degrees (onion)
//  char  CavHeat;                    // select
//  float CavDiff;                    // degrees
//  char  CavDutyCycle;               // %
//  char  KbPref;                     // select
//
//  // email page
//  char  EmailAlerts;                 // enable/disable
//  char  EmailTo[EMAIL_TO_LEN+1];
//  char  EmailFrom[EMAIL_FROM_LEN+1];
//  char  EmailServer[EMAIL_FROM_LEN+1];
//  char  EmailAuthType;               // select
//  unsigned short EmailPort;
//  char  EmailAccount[EMAIL_FROM_LEN+1];
//  char  EmailPassword[EMAIL_FROM_LEN+1];
//  char  EmailDisplay[IP_ADD_LEN+1+SITE_ID_LEN+1];  // IP address & name of display that sends the email
//
//  // email alerts setup page
//  char  AlertsToSend[NUM_WARNINGS_V34+12];            // flags to indicate which alerts actually get sent via email
//                                     // must be at least NUM_WARNINGS long - currently larger to allow
//                                     // adding more warnings without having to change/convert settings
//
//  // software/remote off
//  char AuxRemoteOff;                // onion Aux1
//  char CavRemoteOff;
//  char ClimacellRemoteOff;
//  char FanRemoteOff;
//  char HeatRemoteOff;
//  char Hum1RemoteOff;
//  char Hum2RemoteOff;
//  char RefrRemoteOff;
//
//  // software/remote off (onion)
//  char  Aux2RemoteOff;
//  char  BurnerRemoteOff;
//  char  CureRemoteOff;
//
//  // fan boost setup page
////  char  FanBoostMode;               // select
////  char  FanBoostSpeed;              // %
////  char  FanBoostInterval;           // hours (between periods or continuous runtime)
////  char  FanBoostDur;                // mins
////  float FanBoostTemp;               // degrees
//} SYSTEM_SETTINGS_V34;

/**************************************************************************
 * v3.5 definition
***************************************************************************/
// system settings
//typedef struct {
//  char  SettingsVersion[VERSION_LEN+1];   // Settings structure version
//  char  ArmVersion[VERSION_LEN+1];        // software version
//  char  DST;                              // Daylight Saving Time
//  char  PrevFanSpeed;                     // % (stores the cooling mode fan speed)
//  unsigned short HttpPort;
//  char  NetMonitorMode;
//  char  Co2PurgeRefrigThresh;             // %
//  char  MultiviewSessions;                // number of systems to tile in multi-view mode
//  char  Unused[10];                       // (not used) was display IP address, now using as reserved space
//
//  // logs
//  short UserLogInterval;            // mins
//  char  UserLogWrap;
//  char  PIDLogWrap;
//  char  LogPIDDoor;
//  char  LogPIDRefrig;
//  char  GraphFavorites[MSG_TX_CGIVAR_SIZE];
//
//  // basic setup page
//  char  StorageName[SITE_ID_LEN+1]; // facility
//  char  TempType;                   // Fahrenheit/Celcius
//  char  MasterSlave;
//  char  MasterIP[IP_ADD_LEN+1];
//  char  HomePage[HOMEPAGE_LEN];     // select
//  char  SystemMode;                 // 0 = Potato, 1 = Onion
//  char  Language;                   // select
//  char  FactoryBd[PASSWORD_LEN+1];
//
//  // password page
//  PASSWORDS User[NUM_PASSWORDS];
//
//  // analog board setup page
//  ANALOG_BOARD AnalogBoard[ANALOG_BOARDS_PER_SYSTEM];
//
//  // fresh air door setup page
//  short DoorP;                      // PID values
//  short DoorI;
//  short DoorD;
//  short DoorU;
//  short DoorActuatorTime;           // secs (1-500)
//  char  CoolAirCycle;               // mins (1-30)
//
//  // refrigeration setup page
//  REFRIG Refr[NUM_REFRIG_STAGES];
//  short RefrP;                      // PID values
//  short RefrI;
//  short RefrD;
//  short RefrU;
//  char  RefrPurge;                  // select
//
//  // climacell setup page
//  char  ClimacellEff;               // %
//  short ClimP;                      // PID values
//  short ClimI;
//  short ClimD;
//  short ClimU;
//  short Altitude;
//  char  AltitudeType;               // feet/meters
//
//  // burner setup page (onion)
//  char  BurnerOn;                   // %
//  char  BurnerLow;                  // %
//  short BurnerP;                    // PID values
//  short BurnerI;
//  short BurnerD;
//  short BurnerU;
//
//  // failures 1 setup page
//  char FanFailMode;                 // select
//  char FanFailTimer;                // mins (1-10)
//  char ClimacellFailMode;           // select
//  char ClimacellFailTimer;          // mins (1-120)
//  char RefrFailMode;                // select
//  char RefrFailTimer;               // mins (1-15)
//  char RefrRunFailMode;             // select
//  HUMID_MODE Humid[NUM_HUMIDIFIERS];
//  char AuxFailMode;                 // select
//  char AuxFailTimer;                // mins (1-15)
//  char HeatFailMode;                // select
//  char HeatFailTimer;               // mins (1-15)
//  char CavHeatFailMode;             // select
//  char CavHeatFailTimer;            // mins (1-10)
//  char AirRestFailMode;             // unused
//  char BurnerFailMode;              // select
//  char BurnerFailTimer;             // mins (1-10)
//
//  // failures 2 setup page
//  char  PlenSenFailMode;            // select
//  char  PlenSenFailTimer;           // mins (1-180)
//  float PlenSenDiff;                // degrees
//  char  OutAirFailMode;             // select
//  char  OutAirFailTimer;            // mins (1-180)
//  char  OutHumFailMode;             // select
//  char  OutHumFailTimer;            // mins (1-180)
//  char  PlenHumFailMode;            // select
//  char  PlenHumFailTimer;           // mins (1-180)
//  char  LowHumSet;                  // %
//  char  HighCo2FailMode;            // select
//  char  HighCo2FailTimer;           // mins (1-180)
//  int   HighCo2Set;                 // ppm
//
//  // controller access page
//  NETWORK_NODES_V35 Node[NUM_NETWORK_NODES];
//
//  // service info page
//  char DealerName[NAME_LEN+1];
//  char DealerPhone[PHONE_LEN+1];
//  char TechName[NAME_LEN+1];
//  char TechPhone[PHONE_LEN+1];
//
//  // plenum setup page
//  float        PlenTempSet;         // degrees
//  char         PlenHumSet;          // %
//  unsigned int FanDailyRunTime;     // secs (displayed as hours)
//  unsigned int FanDailyCounter;     // counter (secs)
//  unsigned int FanDailyDay;         // day value for daily reset
//  unsigned int FanTotalRunTime;     // secs (displayed as hours)
//  unsigned int FanTotalCounter;     // counter (secs)
//  char         HumSetpointRef;      // 0 - plenum, 1 - return air (onion)
//
//  // outside air cooling control page
//  float OutAirDiff;                 // degrees
//  char  OutAirAboveBelow;           // select
//  char  OutAirTempRef;              // select
//  char  CalcHumMax;                 // % (onion)
//
//  // plenum temp deviation alarm page
//  float PlenLowAlarmTemp;           // degrees
//  char  PlenLowAlarmTimer;          // mins
//  float PlenHighAlarmTemp;          // degrees
//  char  PlenHighAlarmTimer;         // mins
//
//  // system runtimes page
//  char RunTimes[48];                // 48 elements represent each half hour in 24 hours
//  char CureRunTimes[48];            // cure mode runtimes
//
//  // frequency drive control page
//  char  MaxFanSpeed;                // %
//  char  MinFanSpeed;                // %
//  char  RefrFanSpeed;               // %
//  char  RecircFanSpeed;             // %
//  char  UpdFanSpeed;                // hours
//  float TempDiff;                   // degrees
//  char  TempRef1;                   // select
//  char  TempRef2;                   // select
//  char  FanUpdMode;                 // select (onion)
//
//  // ramp rate page
//  float RampUpdTemp;                // degrees
//  char  RampUpdHours;               // hours
//  float RampTempDiff;               // degrees
//  char  RampTempRef;                // select
//  float RampTargetTemp;             // degrees
//
//  // humidity control page
//  char HumMode;                     // select
//  char HumType;                     // select
//  HUMID_CYCLE HumDutyCycle[3];
//
//  // high CO2 level purge page
//  short        Co2Set;              // ppm (1-10000)
//  char         Co2CylceTime;        // hours between purges
//  char         Co2PurgeMode;        // select
//  float        Co2PurgeMaxTemp;     // degrees
//  float        Co2PurgeMinTemp;     // degrees
//  char         Co2PurgeDur;         // mins
//  unsigned int Co2LastPurge;        // counter (secs)
//  unsigned int Co2PurgeStart;       // counter (secs)
//
//  // miscellaneous parameters page
//  char  RefrMode;                   // select
//  char  DefrostInt;                 // hours
//  char  DefrostDur;                 // mins
//  float HeatTempThresh;             // degrees
//  float RefrigLimit;                // degrees (onion)
//  char  CavHeat;                    // select
//  float CavDiff;                    // degrees
//  char  CavDutyCycle;               // %
//  char  KbPref;                     // select
//
//  // email page
//  char  EmailAlerts;                 // enable/disable
//  char  EmailTo[EMAIL_TO_LEN+1];
//  char  EmailFrom[EMAIL_FROM_LEN+1];
//  char  EmailServer[EMAIL_FROM_LEN+1];
//  char  EmailAuthType;               // select
//  unsigned short EmailPort;
//  char  EmailAccount[EMAIL_FROM_LEN+1];
//  char  EmailPassword[EMAIL_FROM_LEN+1];
//  char  EmailDisplay[IP_ADD_LEN+1+SITE_ID_LEN+1];  // IP address & name of display that sends the email
//
//  // email alerts setup page
//  char  AlertsToSend[NUM_WARNINGS_V35+12];            // flags to indicate which alerts actually get sent via email
//                                     // must be at least NUM_WARNINGS long - currently larger to allow
//                                     // adding more warnings without having to change/convert settings
//
//  // software/remote off
//  char AuxRemoteOff;                // onion Aux1
//  char CavRemoteOff;
//  char ClimacellRemoteOff;
//  char FanRemoteOff;
//  char HeatRemoteOff;
//  char Hum1RemoteOff;
//  char Hum2RemoteOff;
//  char RefrRemoteOff;
//
//  // software/remote off (onion)
//  char  Aux2RemoteOff;
//  char  BurnerRemoteOff;
//  char  CureRemoteOff;
//
//  // fan boost setup page
//  char  FanBoostMode;               // select
//  char  FanBoostSpeed;              // %
//  char  FanBoostInterval;           // hours (between periods or continuous runtime)
//  char  FanBoostDur;                // mins
//  float FanBoostTemp;               // degrees
//} SYSTEM_SETTINGS_V35;

/**************************************************************************
 * v3.6 definition
***************************************************************************/
// system settings
//typedef struct {
//  char  SettingsVersion[VERSION_LEN+1];   // Settings structure version
//  char  ArmVersion[VERSION_LEN+1];        // software version
//  char  DST;                              // Daylight Saving Time
//  char  PrevFanSpeed;                     // % (stores the cooling mode fan speed)
//  unsigned short HttpPort;
//  char  NetMonitorMode;
//  char  Co2PurgeRefrigThresh;             // %
//  char  MultiviewSessions;                // number of systems to tile in multi-view mode
//  char  PublicIP[IP_ADD_LEN+1];
//  char  Unused[10];                       // (not used) was display IP address, now using as reserved space
//
//  // logs
//  short UserLogInterval;            // mins
//  char  UserLogWrap;
//  char  PIDLogWrap;
//  char  LogPIDDoor;
//  char  LogPIDRefrig;
//  char  GraphFavorites[MSG_TX_CGIVAR_SIZE];
//
//  // basic setup page
//  char  StorageName[SITE_ID_LEN+1]; // facility
//  char  TempType;                   // Fahrenheit/Celcius
//  char  MasterSlave;
//  char  MasterIP[IP_ADD_LEN+1];
//  char  HomePage[HOMEPAGE_LEN];     // select
//  char  SystemMode;                 // 0 = Potato, 1 = Onion
//  char  Language;                   // select
//  char  FactoryBd[PASSWORD_LEN+1];
////  char  LoginPw[PASSWORD_LEN+1];
////  char  LocalLogin;                 // 0 = No, 1 = Yes
//
//  // password page
//  PASSWORDS User[NUM_PASSWORDS];
//
//  // analog board setup page
//  ANALOG_BOARD AnalogBoard[ANALOG_BOARDS_PER_SYSTEM];
//
//  // fresh air door setup page
//  short DoorP;                      // PID values
//  short DoorI;
//  short DoorD;
//  short DoorU;
//  short DoorActuatorTime;           // secs (1-500)
//  char  CoolAirCycle;               // mins (1-30)
//
//  // refrigeration setup page
//  REFRIG Refr[NUM_REFRIG_STAGES];
//  short RefrP;                      // PID values
//  short RefrI;
//  short RefrD;
//  short RefrU;
//  char  RefrPurge;                  // select
//
//  // climacell setup page
//  char  ClimacellEff;               // %
//  short ClimP;                      // PID values
//  short ClimI;
//  short ClimD;
//  short ClimU;
//  short Altitude;
//  char  AltitudeType;               // feet/meters
//
//  // burner setup page (onion)
//  char  BurnerMode;                 // select
//  char  BurnerOn;                   // %
//  char  BurnerLow;                  // %
//  short BurnerP;                    // PID values
//  short BurnerI;
//  short BurnerD;
//  short BurnerU;
//  char  BurnerManual;               // %
//
//  // failures 1 setup page
//  char FanFailMode;                 // select
//  char FanFailTimer;                // mins (1-10)
//  char ClimacellFailMode;           // select
//  char ClimacellFailTimer;          // mins (1-120)
//  char RefrFailMode;                // select
//  char RefrFailTimer;               // mins (1-15)
//  char RefrRunFailMode;             // select
//  HUMID_MODE Humid[NUM_HUMIDIFIERS];
//  char AuxFailMode;                 // select
//  char AuxFailTimer;                // mins (1-15)
//  char HeatFailMode;                // select
//  char HeatFailTimer;               // mins (1-15)
//  char CavHeatFailMode;             // select
//  char CavHeatFailTimer;            // mins (1-10)
//  char AirRestFailMode;             // unused
//  char BurnerFailMode;              // select
//  char BurnerFailTimer;             // mins (1-10)
//
//  // failures 2 setup page
//  char  PlenSenFailMode;            // select
//  char  PlenSenFailTimer;           // mins (1-180)
//  float PlenSenDiff;                // degrees
//  char  OutAirFailMode;             // select
//  char  OutAirFailTimer;            // mins (1-180)
//  char  OutHumFailMode;             // select
//  char  OutHumFailTimer;            // mins (1-180)
//  char  PlenHumFailMode;            // select
//  char  PlenHumFailTimer;           // mins (1-180)
//  char  LowHumSet;                  // %
//  char  HighCo2FailMode;            // select
//  char  HighCo2FailTimer;           // mins (1-180)
//  int   HighCo2Set;                 // ppm
//
//  // controller access page
//  NETWORK_NODES Node[NUM_NETWORK_NODES];
//
//  // service info page
//  char DealerName[NAME_LEN+1];
//  char DealerPhone[PHONE_LEN+1];
//  char TechName[NAME_LEN+1];
//  char TechPhone[PHONE_LEN+1];
//
//  // plenum setup page
//  float        PlenTempSet;         // degrees
//  char         PlenHumSet;          // %
//  unsigned int FanDailyRunTime;     // secs (displayed as hours)
//  unsigned int FanDailyCounter;     // counter (secs)
//  unsigned int FanDailyDay;         // day value for daily reset
//  unsigned int FanTotalRunTime;     // secs (displayed as hours)
//  unsigned int FanTotalCounter;     // counter (secs)
//  char         HumSetpointRef;      // 0 - plenum, 1 - return air (onion)
//  float        BurnerTempSet;       // degrees
//
//  // outside air cooling control page
//  float OutAirDiff;                 // degrees
//  char  OutAirAboveBelow;           // select
//  char  OutAirTempRef;              // select
//  char  CalcHumMax;                 // % (onion)
//  // air cure mode (onion)
//  float CureStartTemp;              // degrees
//  char  CureStartHumid;             // %
//  char  CureHumidHighLimit;         // %
//  char  CureHumidRef;               // select
//
//  // plenum temp deviation alarm page
//  float PlenLowAlarmTemp;           // degrees
//  char  PlenLowAlarmTimer;          // mins
//  float PlenHighAlarmTemp;          // degrees
//  char  PlenHighAlarmTimer;         // mins
//  // air cure mode (onion)
//  float CureTempLowLimit;           // degrees
//  float CureTempHighLimit;          // degrees
//
//  // system runtimes page
//  char RunTimes[48];                // 48 elements represent each half hour in 24 hours
//  char CureRunTimes[48];            // cure mode runtimes
//
//  // frequency drive control page
//  char  MaxFanSpeed;                // %
//  char  MinFanSpeed;                // %
//  char  RefrFanSpeed;               // %
//  char  RecircFanSpeed;             // %
//  char  UpdFanSpeed;                // hours
//  float TempDiff;                   // degrees
//  char  TempRef1;                   // select
//  char  TempRef2;                   // select
//  char  FanUpdMode;                 // select (onion)
//
//  // ramp rate page
//  float RampUpdTemp;                // degrees
//  char  RampUpdHours;               // hours
//  float RampTempDiff;               // degrees
//  char  RampTempRef;                // select
//  float RampTargetTemp;             // degrees
//
//  // humidity control page
//  char HumMode;                     // select
//  char HumType;                     // select
//  HUMID_CYCLE HumDutyCycle[3];
//
//  // high CO2 level purge page
//  short        Co2Set;              // ppm (1-10000)
//  char         Co2CylceTime;        // hours between purges
//  char         Co2PurgeMode;        // select
//  float        Co2PurgeMaxTemp;     // degrees
//  float        Co2PurgeMinTemp;     // degrees
//  char         Co2PurgeDur;         // mins
//  unsigned int Co2LastPurge;        // counter (secs)
//  unsigned int Co2PurgeStart;       // counter (secs)
//
//  // miscellaneous parameters page
//  char  RefrMode;                   // select
//  char  DefrostInt;                 // hours
//  char  DefrostDur;                 // mins
//  float HeatTempThresh;             // degrees
//  float RefrigLimit;                // degrees (onion)
//  char  CavHeat;                    // select
//  float CavDiff;                    // degrees
//  char  CavDutyCycle;               // %
//  char  KbPref;                     // select
//
//  // email page
//  char  EmailAlerts;                 // enable/disable
//  char  EmailTo[EMAIL_TO_LEN+1];
//  char  EmailFrom[EMAIL_FROM_LEN+1];
//  char  EmailServer[EMAIL_FROM_LEN+1];
//  char  EmailAuthType;               // select
//  unsigned short EmailPort;
//  char  EmailAccount[EMAIL_FROM_LEN+1];
//  char  EmailPassword[EMAIL_FROM_LEN+1];
//  char  EmailDisplay[IP_ADD_LEN+1+SITE_ID_LEN+1];  // IP address & name of display that sends the email
//
//  // email alerts setup page
//  char  AlertsToSend[NUM_WARNINGS_V36+12];            // flags to indicate which alerts actually get sent via email
//                                     // must be at least NUM_WARNINGS long - currently larger to allow
//                                     // adding more warnings without having to change/convert settings
//
//  // software/remote off
//  char AuxRemoteOff;                // onion Aux1
//  char CavRemoteOff;
//  char ClimacellRemoteOff;
//  char FanRemoteOff;
//  char HeatRemoteOff;
//  char Hum1RemoteOff;
//  char Hum2RemoteOff;
//  char RefrRemoteOff;
//
//  // software/remote off (onion)
//  char  Aux2RemoteOff;
//  char  BurnerRemoteOff;
//  char  CureRemoteOff;
//
//  // fan boost setup page
//  char  FanBoostMode;               // select
//  char  FanBoostSpeed;              // %
//  char  FanBoostInterval;           // hours (between periods or continuous runtime)
//  char  FanBoostDur;                // mins
//  float FanBoostTemp;               // degrees
//} SYSTEM_SETTINGS_V36;

/**************************************************************************
 * v3.7 definition
***************************************************************************/
// system settings
//typedef struct {
//  char  SettingsVersion[VERSION_LEN+1];   // Settings structure version
//  char  ArmVersion[VERSION_LEN+1];        // software version
//  char  DST;                              // Daylight Saving Time
//  char  PrevFanSpeed;                     // % (stores the cooling mode fan speed)
//  unsigned short HttpPort;
//  char  NetMonitorMode;
//  char  Co2PurgeRefrigThresh;             // %
//  char  MultiviewSessions;                // number of systems to tile in multi-view mode
//  char  PublicIP[IP_ADD_LEN+1];
//  char  Unused[10];                       // (not used) was display IP address, now using as reserved space
//
//  // logs
//  short UserLogInterval;            // mins
//  char  UserLogWrap;
//  char  PIDLogWrap;
//  char  LogPIDDoor;
//  char  LogPIDRefrig;
//  char  GraphFavorites[MSG_TX_CGIVAR_SIZE];
//
//  // basic setup page
//  char  StorageName[SITE_ID_LEN+1]; // facility
//  char  TempType;                   // Fahrenheit/Celcius
//  char  MasterSlave;
//  char  MasterIP[IP_ADD_LEN+1];
//  char  HomePage[HOMEPAGE_LEN];     // select
//  char  SystemMode;                 // 0 = Potato, 1 = Onion
//  char  Language;                   // select
//  char  FactoryBd[PASSWORD_LEN+1];
//  char  LoginPw[PASSWORD_LEN+1];
//  char  LocalLogin;                 // 0 = No, 1 = Yes
//
//  // password page
//  PASSWORDS User[NUM_PASSWORDS];
//
//  // analog board setup page
//  ANALOG_BOARD AnalogBoard[ANALOG_BOARDS_PER_SYSTEM];
//
//  // fresh air door setup page
//  short DoorP;                      // PID values
//  short DoorI;
//  short DoorD;
//  short DoorU;
//  short DoorActuatorTime;           // secs (1-500)
//  char  CoolAirCycle;               // mins (1-30)
//
//  // refrigeration setup page
//  REFRIG Refr[NUM_REFRIG_STAGES];
//  short RefrP;                      // PID values
//  short RefrI;
//  short RefrD;
//  short RefrU;
//  char  RefrPurge;                  // select
//
//  // climacell setup page
//  char  ClimacellEff;               // %
//  short ClimP;                      // PID values
//  short ClimI;
//  short ClimD;
//  short ClimU;
//  short Altitude;
//  char  AltitudeType;               // feet/meters
//
//  // burner setup page (onion)
//  char  BurnerMode;                 // select
//  char  BurnerOn;                   // %
//  char  BurnerLow;                  // %
//  short BurnerP;                    // PID values
//  short BurnerI;
//  short BurnerD;
//  short BurnerU;
//  char  BurnerManual;               // %
//
//  // failures 1 setup page
//  char FanFailMode;                 // select
//  char FanFailTimer;                // mins (1-10)
//  char ClimacellFailMode;           // select
//  char ClimacellFailTimer;          // mins (1-120)
//  char RefrFailMode;                // select
//  char RefrFailTimer;               // mins (1-15)
//  char RefrRunFailMode;             // select
//  HUMID_MODE Humid[NUM_HUMIDIFIERS];
//  char AuxFailMode;                 // select
//  char AuxFailTimer;                // mins (1-15)
//  char HeatFailMode;                // select
//  char HeatFailTimer;               // mins (1-15)
//  char CavHeatFailMode;             // select
//  char CavHeatFailTimer;            // mins (1-10)
//  char AirRestFailMode;             // unused
//  char BurnerFailMode;              // select
//  char BurnerFailTimer;             // mins (1-10)
//
//  // failures 2 setup page
//  char  PlenSenFailMode;            // select
//  char  PlenSenFailTimer;           // mins (1-180)
//  float PlenSenDiff;                // degrees
//  char  OutAirFailMode;             // select
//  char  OutAirFailTimer;            // mins (1-180)
//  char  OutHumFailMode;             // select
//  char  OutHumFailTimer;            // mins (1-180)
//  char  PlenHumFailMode;            // select
//  char  PlenHumFailTimer;           // mins (1-180)
//  char  LowHumSet;                  // %
//  char  HighCo2FailMode;            // select
//  char  HighCo2FailTimer;           // mins (1-180)
//  int   HighCo2Set;                 // ppm
//
//  // controller access page
//  NETWORK_NODES Node[NUM_NETWORK_NODES];
//
//  // service info page
//  char DealerName[NAME_LEN+1];
//  char DealerPhone[PHONE_LEN+1];
//  char TechName[NAME_LEN+1];
//  char TechPhone[PHONE_LEN+1];
//
//  // plenum setup page
//  float        PlenTempSet;         // degrees
//  char         PlenHumSet;          // %
//  unsigned int FanDailyRunTime;     // secs (displayed as hours)
//  unsigned int FanDailyCounter;     // counter (secs)
//  unsigned int FanDailyDay;         // day value for daily reset
//  unsigned int FanTotalRunTime;     // secs (displayed as hours)
//  unsigned int FanTotalCounter;     // counter (secs)
//  char         HumSetpointRef;      // 0 - plenum, 1 - return air (onion)
//  float        BurnerTempSet;       // degrees
//  float        BurnerThreshold;     // degrees
//
//  // outside air cooling control page
//  float OutAirDiff;                 // degrees
//  char  OutAirAboveBelow;           // select
//  char  OutAirTempRef;              // select
//  char  CalcHumMax;                 // % (onion)
//  // air cure mode (onion)
//  float CureStartTemp;              // degrees
//  char  CureStartHumid;             // %
//  char  CureHumidHighLimit;         // %
//  char  CureHumidRef;               // select
//
//  // plenum temp deviation alarm page
//  float PlenLowAlarmTemp;           // degrees
//  char  PlenLowAlarmTimer;          // mins
//  float PlenHighAlarmTemp;          // degrees
//  char  PlenHighAlarmTimer;         // mins
//  // air cure mode (onion)
//  float CureTempLowLimit;           // degrees
//  float CureTempHighLimit;          // degrees
//
//  // system runtimes page
//  char RunTimes[48];                // 48 elements represent each half hour in 24 hours
//  char CureRunTimes[48];            // cure mode runtimes
//
//  // frequency drive control page
//  char  MaxFanSpeed;                // %
//  char  MinFanSpeed;                // %
//  char  RefrFanSpeed;               // %
//  char  RecircFanSpeed;             // %
//  char  UpdFanSpeed;                // hours
//  float TempDiff;                   // degrees
//  char  TempRef1;                   // select
//  char  TempRef2;                   // select
//  char  FanUpdMode;                 // select (onion)
//
//  // ramp rate page
//  float RampUpdTemp;                // degrees
//  char  RampUpdHours;               // hours
//  float RampTempDiff;               // degrees
//  char  RampTempRef;                // select
//  float RampTargetTemp;             // degrees
//
//  // humidity control page
//  char HumMode;                     // select
//  char HumType;                     // select
//  HUMID_CYCLE HumDutyCycle[3];
//
//  // high CO2 level purge page
//  short        Co2Set;              // ppm (1-10000)
//  char         Co2CylceTime;        // hours between purges
//  char         Co2PurgeMode;        // select
//  float        Co2PurgeMaxTemp;     // degrees
//  float        Co2PurgeMinTemp;     // degrees
//  char         Co2PurgeDur;         // mins
//  unsigned int Co2LastPurge;        // counter (secs)
//  unsigned int Co2PurgeStart;       // counter (secs)
//
//  // miscellaneous parameters page
//  char  RefrMode;                   // select
//  char  DefrostInt;                 // hours
//  char  DefrostDur;                 // mins
//  float HeatTempThresh;             // degrees
//  float RefrigLimit;                // degrees (onion)
//  char  CavHeat;                    // select
//  float CavDiff;                    // degrees
//  char  CavDutyCycle;               // %
//  char  KbPref;                     // select
//
//  // email page
//  char  EmailAlerts;                 // enable/disable
//  char  EmailTo[EMAIL_TO_LEN+1];
//  char  EmailFrom[EMAIL_FROM_LEN+1];
//  char  EmailServer[EMAIL_FROM_LEN+1];
//  char  EmailAuthType;               // select
//  unsigned short EmailPort;
//  char  EmailAccount[EMAIL_FROM_LEN+1];
//  char  EmailPassword[EMAIL_FROM_LEN+1];
//  char  EmailDisplay[IP_ADD_LEN+1+SITE_ID_LEN+1];  // IP address & name of display that sends the email
//
//  // email alerts setup page
//  char  AlertsToSend[NUM_WARNINGS_V37+12];            // flags to indicate which alerts actually get sent via email
//                                     // must be at least NUM_WARNINGS long - currently larger to allow
//                                     // adding more warnings without having to change/convert settings
//
//  // software/remote off
//  char AuxRemoteOff;                // onion Aux1
//  char CavRemoteOff;
//  char ClimacellRemoteOff;
//  char FanRemoteOff;
//  char HeatRemoteOff;
//  char Hum1RemoteOff;
//  char Hum2RemoteOff;
//  char RefrRemoteOff;
//
//  // software/remote off (onion)
//  char  Aux2RemoteOff;
//  char  BurnerRemoteOff;
//  char  CureRemoteOff;
//
//  // fan boost setup page
//  char  FanBoostMode;               // select
//  char  FanBoostSpeed;              // %
//  char  FanBoostInterval;           // hours (between periods or continuous runtime)
//  char  FanBoostDur;                // mins
//  float FanBoostTemp;               // degrees
//} SYSTEM_SETTINGS_V37;

/**************************************************************************
 * v3.9 definition
***************************************************************************/
// system settings
//typedef struct {
//  char  SettingsVersion[VERSION_LEN+1];   // Settings structure version
//  char  ArmVersion[VERSION_LEN+1];        // software version
//  char  DST;                              // Daylight Saving Time
//  char  PrevFanSpeed;                     // % (stores the cooling mode fan speed)
//  unsigned short HttpPort;
//  char  NetMonitorMode;
//  char  Co2PurgeRefrigThresh;             // %
//  char  MultiviewSessions;                // number of systems to tile in multi-view mode
//  char  PublicIP[IP_ADD_LEN+1];
//  char  Unused[10];                       // (not used) was display IP address, now using as reserved space
//
//  // logs
//  short UserLogInterval;            // mins
//  char  UserLogWrap;
//  char  PIDLogWrap;
//  char  LogPIDDoor;
//  char  LogPIDRefrig;
//  char  GraphFavorites[MSG_TX_CGIVAR_SIZE];
//
//  // basic setup page
//  char  StorageName[SITE_ID_LEN+1]; // facility
//  char  TempType;                   // Fahrenheit/Celcius
//  char  MasterSlave;
//  char  MasterIP[IP_ADD_LEN+1];
//  char  HomePage[HOMEPAGE_LEN];     // select
//  char  SystemMode;                 // 0 = Potato, 1 = Onion
//  char  Language;                   // select
//  char  FactoryBd[PASSWORD_LEN+1];
//  char  LoginPw[PASSWORD_LEN+1];
//  char  LocalLogin;                 // 0 = No, 1 = Yes
////  char  Animations;                 // 0 = No, 1 = Yes
//
//  // password page
//  PASSWORDS User[NUM_PASSWORDS];
//
//  // analog board setup page
//  ANALOG_BOARD AnalogBoard[ANALOG_BOARDS_PER_SYSTEM];
//
//  // fresh air door setup page
//  short DoorP;                      // PID values
//  short DoorI;
//  short DoorD;
//  short DoorU;
//  short DoorActuatorTime;           // secs (1-500)
//  char  CoolAirCycle;               // mins (1-30)
//
//  // refrigeration setup page
//  REFRIG Refr[NUM_REFRIG_STAGES];
//  short RefrP;                      // PID values
//  short RefrI;
//  short RefrD;
//  short RefrU;
//  char  RefrPurge;                  // select
//
//  // climacell setup page
//  char  ClimacellEff;               // %
//  short ClimP;                      // PID values
//  short ClimI;
//  short ClimD;
//  short ClimU;
//  short Altitude;
//  char  AltitudeType;               // feet/meters
//
//  // burner setup page (onion)
//  char  BurnerMode;                 // select
//  char  BurnerOn;                   // %
//  char  BurnerLow;                  // %
//  short BurnerP;                    // PID values
//  short BurnerI;
//  short BurnerD;
//  short BurnerU;
//  char  BurnerManual;               // %
//
//  // failures 1 setup page
//  char FanFailMode;                 // select
//  char FanFailTimer;                // mins (1-10)
//  char ClimacellFailMode;           // select
//  char ClimacellFailTimer;          // mins (1-120)
//  char RefrFailMode;                // select
//  char RefrFailTimer;               // mins (1-15)
//  char RefrRunFailMode;             // select
//  HUMID_MODE Humid[NUM_HUMIDIFIERS];
//  char AuxFailMode;                 // select
//  char AuxFailTimer;                // mins (1-15)
//  char HeatFailMode;                // select
//  char HeatFailTimer;               // mins (1-15)
//  char CavHeatFailMode;             // select
//  char CavHeatFailTimer;            // mins (1-10)
//  char AirRestFailMode;             // unused
//  char BurnerFailMode;              // select
//  char BurnerFailTimer;             // mins (1-10)
////  char LightsFailMode;              // select
////  char LightsFailTimer;             // mins or hours
////  char LightsFailUnits;             // select (mins or hours)
//
//  // failures 2 setup page
//  char  PlenSenFailMode;            // select
//  char  PlenSenFailTimer;           // mins (1-180)
//  float PlenSenDiff;                // degrees
//  char  OutAirFailMode;             // select
//  char  OutAirFailTimer;            // mins (1-180)
//  char  OutHumFailMode;             // select
//  char  OutHumFailTimer;            // mins (1-180)
//  char  PlenHumFailMode;            // select
//  char  PlenHumFailTimer;           // mins (1-180)
//  char  LowHumSet;                  // %
//  char  HighCo2FailMode;            // select
//  char  HighCo2FailTimer;           // mins (1-180)
//  int   HighCo2Set;                 // ppm
//
//  // controller access page
//  NETWORK_NODES Node[NUM_NETWORK_NODES];
//
//  // service info page
//  char DealerName[NAME_LEN+1];
//  char DealerPhone[PHONE_LEN+1];
//  char TechName[NAME_LEN+1];
//  char TechPhone[PHONE_LEN+1];
//
//  // plenum setup page
//  float        PlenTempSet;         // degrees
//  char         PlenHumSet;          // %
//  unsigned int FanDailyRunTime;     // secs (displayed as hours)
//  unsigned int FanDailyCounter;     // counter (secs)
//  unsigned int FanDailyDay;         // day value for daily reset
//  unsigned int FanTotalRunTime;     // secs (displayed as hours)
//  unsigned int FanTotalCounter;     // counter (secs)
//  char         HumSetpointRef;      // 0 - plenum, 1 - return air (onion)
//  float        BurnerTempSet;       // degrees (shared for manual mode max temp)
//  float        BurnerThreshold;     // %       (shared for manual mode restart temp)
//
//  // outside air cooling control page
//  float OutAirDiff;                 // degrees
//  char  OutAirAboveBelow;           // select
//  char  OutAirTempRef;              // select
//  char  CalcHumMax;                 // % (onion)
//  // air cure mode (onion)
//  float CureStartTemp;              // degrees
//  char  CureStartHumid;             // %
//  char  CureHumidHighLimit;         // %
//  char  CureHumidRef;               // select
//
//  // plenum temp deviation alarm page
//  float PlenLowAlarmTemp;           // degrees
//  char  PlenLowAlarmTimer;          // mins
//  float PlenHighAlarmTemp;          // degrees
//  char  PlenHighAlarmTimer;         // mins
//  // air cure mode (onion)
//  float CureTempLowLimit;           // degrees
//  float CureTempHighLimit;          // degrees
//
//  // system runtimes page
//  char RunTimes[48];                // 48 elements represent each half hour in 24 hours
//  char CureRunTimes[48];            // cure mode runtimes
////  char ClimacellTimes[48];          // climacell runtimes
//
//  // frequency drive control page
//  char  MaxFanSpeed;                // %
//  char  MinFanSpeed;                // %
//  char  RefrFanSpeed;               // %
//  char  RecircFanSpeed;             // %
//  char  UpdFanSpeed;                // hours
//  float TempDiff;                   // degrees
//  char  TempRef1;                   // select
//  char  TempRef2;                   // select
//  char  FanUpdMode;                 // select (onion)
//
//  // ramp rate page
//  float RampUpdTemp;                // degrees
//  char  RampUpdHours;               // hours
//  float RampTempDiff;               // degrees
//  char  RampTempRef;                // select
//  float RampTargetTemp;             // degrees
//
//  // humidity control page
//  HUMID_CONTROL HumidCtrl[TOTAL_HUMID_EQUIP];
//
//  // high CO2 level purge page
//  short        Co2Set;              // ppm (1-10000)
//  char         Co2CylceTime;        // hours between purges
//  char         Co2PurgeMode;        // select
//  float        Co2PurgeMaxTemp;     // degrees
//  float        Co2PurgeMinTemp;     // degrees
//  char         Co2PurgeDur;         // mins
//  unsigned int Co2LastPurge;        // counter (secs)
//  unsigned int Co2PurgeStart;       // counter (secs)
////  char         Co2FanOutput;        // %
////  char         Co2DoorOutput;       // %
//
//  // miscellaneous parameters page
//  char  RefrMode;                   // select
//  char  DefrostInt;                 // hours
//  char  DefrostDur;                 // mins
//  float HeatTempThresh;             // degrees
//  float RefrigLimit;                // degrees (onion)
//  char  CavHeat;                    // select
//  float CavDiff;                    // degrees
//  char  CavDutyCycle;               // %
//  char  KbPref;                     // select
//
//  // email page
//  char  EmailAlerts;                 // enable/disable
//  char  EmailTo[EMAIL_TO_LEN+1];
//  char  EmailFrom[EMAIL_FROM_LEN+1];
//  char  EmailServer[EMAIL_FROM_LEN+1];
//  char  EmailAuthType;               // select
//  unsigned short EmailPort;
//  char  EmailAccount[EMAIL_FROM_LEN+1];
//  char  EmailPassword[EMAIL_FROM_LEN+1];
//  char  EmailDisplay[IP_ADD_LEN+1+SITE_ID_LEN+1];  // IP address & name of display that sends the email
//
//  // email alerts setup page
//  char  AlertsToSend[NUM_WARNINGS_V39+8];            // flags to indicate which alerts actually get sent via email
//                                     // must be at least NUM_WARNINGS long - currently larger to allow
//                                     // adding more warnings without having to change/convert settings
//
//  // software/remote off
//  char AuxRemoteOff;                // onion Aux1
//  char CavRemoteOff;
//  char ClimacellRemoteOff;
//  char FanRemoteOff;
//  char HeatRemoteOff;
//  char Hum1RemoteOff;
//  char Hum2RemoteOff;
//  char RefrRemoteOff;
////  char Lights1RemoteOff;
////  char Lights2RemoteOff;
//
//  // software/remote off (onion)
//  char  Aux2RemoteOff;
//  char  BurnerRemoteOff;
//  char  CureRemoteOff;
//
//  // fan boost setup page
//  char  FanBoostMode;               // select
//  char  FanBoostSpeed;              // %
//  char  FanBoostInterval;           // hours (between periods or continuous runtime)
//  char  FanBoostDur;                // mins
//  float FanBoostTemp;               // degrees
//} SYSTEM_SETTINGS_V39;


/**************************************************************************
 * v3.12 definition
***************************************************************************/
// system settings
//typedef struct {
//  char  SettingsVersion[VERSION_LEN+1];   // Settings structure version
//  char  ArmVersion[VERSION_LEN+1];        // software version
//  char  DST;                              // Daylight Saving Time
//  char  PrevFanSpeed;                     // % (stores the cooling mode fan speed)
//  unsigned short HttpPort;
//  char  NetMonitorMode;
//  char  Co2PurgeRefrigThresh;             // %
//  char  MultiviewSessions;                // number of systems to tile in multi-view mode
//  char  PublicIP[IP_ADD_LEN+1];
//  char  CavityOrPileCtrl;
//  char  Unused[9];                        // (not used) was display IP address, now using as reserved space
//
//  // logs
//  short UserLogInterval;            // mins
//  char  UserLogWrap;
//  char  PIDLogWrap;
//  char  LogPIDDoor;
//  char  LogPIDRefrig;
//  char  GraphFavorites[MSG_TX_CGIVAR_SIZE];
//
//  // basic setup page
//  char  StorageName[SITE_ID_LEN+1]; // facility
//  char  TempType;                   // Fahrenheit/Celcius
//  char  MasterSlave;
//  char  MasterIP[IP_ADD_LEN+1];
//  char  HomePage[HOMEPAGE_LEN];     // select
//  char  SystemMode;                 // 0 = Potato, 1 = Onion
//  char  Language;                   // select
//  char  FactoryBd[PASSWORD_LEN+1];
//  char  LoginPw[PASSWORD_LEN+1];
//  char  LocalLogin;                 // 0 = No, 1 = Yes
//  char  Animations;                 // 0 = No, 1 = Yes
//
//  // password page
//  PASSWORDS User[NUM_PASSWORDS];
//
//  // analog board setup page
//  ANALOG_BOARD AnalogBoard[ANALOG_BOARDS_PER_SYSTEM];
//
//  // fresh air door setup page
//  short DoorP;                      // PID values
//  short DoorI;
//  short DoorD;
//  short DoorU;
//  short DoorActuatorTime;           // secs (1-500)
//  char  CoolAirCycle;               // mins (1-30)
//
//  // refrigeration setup page
//  REFRIG Refr[NUM_REFRIG_STAGES];
//  short RefrP;                      // PID values
//  short RefrI;
//  short RefrD;
//  short RefrU;
//  char  RefrPurge;                  // select
//
//  // climacell setup page
//  char  ClimacellEff;               // %
//  short ClimP;                      // PID values
//  short ClimI;
//  short ClimD;
//  short ClimU;
//  short Altitude;
//  char  AltitudeType;               // feet/meters
//
//  // burner setup page (onion)
//  char  BurnerMode;                 // select
//  char  BurnerOn;                   // %
//  char  BurnerLow;                  // %
//  short BurnerP;                    // PID values
//  short BurnerI;
//  short BurnerD;
//  short BurnerU;
//  char  BurnerManual;               // %
//
//  // failures 1 setup page
//  char FanFailMode;                 // select
//  char FanFailTimer;                // mins (1-10)
//  char ClimacellFailMode;           // select
//  char ClimacellFailTimer;          // mins (1-120)
//  char RefrFailMode;                // select
//  char RefrFailTimer;               // mins (1-15)
//  char RefrRunFailMode;             // select
//  HUMID_MODE Humid[NUM_HUMIDIFIERS];
//  char AuxFailMode;                 // select
//  char AuxFailTimer;                // mins (1-15)
//  char HeatFailMode;                // select
//  char HeatFailTimer;               // mins (1-15)
//  char CavHeatFailMode;             // select
//  char CavHeatFailTimer;            // mins (1-10)
//  char AirRestFailMode;             // unused
//  char BurnerFailMode;              // select
//  char BurnerFailTimer;             // mins (1-10)
//  char LightsFailMode;              // select
//  char LightsFailTimer;             // mins or hours
//  char LightsFailUnits;             // select (mins or hours)
//
//  // failures 2 setup page
//  char  PlenSenFailMode;            // select
//  char  PlenSenFailTimer;           // mins (1-180)
//  float PlenSenDiff;                // degrees
//  char  OutAirFailMode;             // select
//  char  OutAirFailTimer;            // mins (1-180)
//  char  OutHumFailMode;             // select
//  char  OutHumFailTimer;            // mins (1-180)
//  char  PlenHumFailMode;            // select
//  char  PlenHumFailTimer;           // mins (1-180)
//  char  LowHumSet;                  // %
//  char  HighCo2FailMode;            // select
//  char  HighCo2FailTimer;           // mins (1-180)
//  int   HighCo2Set;                 // ppm
//
//  // controller access page
//  NETWORK_NODES Node[NUM_NETWORK_NODES];
//
//  // service info page
//  char DealerName[NAME_LEN+1];
//  char DealerPhone[PHONE_LEN+1];
//  char TechName[NAME_LEN+1];
//  char TechPhone[PHONE_LEN+1];
//
//  // plenum setup page
//  float        PlenTempSet;         // degrees
//  char         PlenHumSet;          // %
//  unsigned int FanDailyRunTime;     // secs (displayed as hours)
//  unsigned int FanDailyCounter;     // counter (secs)
//  unsigned int FanDailyDay;         // day value for daily reset
//  unsigned int FanTotalRunTime;     // secs (displayed as hours)
//  unsigned int FanTotalCounter;     // counter (secs)
//  char         HumSetpointRef;      // 0 - plenum, 1 - return air (onion)
//  float        BurnerTempSet;       // degrees (shared for manual mode max temp)
//  float        BurnerThreshold;     // %       (shared for manual mode restart temp)
//
//  // outside air cooling control page
//  float OutAirDiff;                 // degrees
//  char  OutAirAboveBelow;           // select
//  char  OutAirTempRef;              // select
//  char  CalcHumMax;                 // % (onion)
//  // air cure mode (onion)
//  float CureStartTemp;              // degrees
//  char  CureStartHumid;             // %
//  char  CureHumidHighLimit;         // %
//  char  CureHumidRef;               // select
//
//  // plenum temp deviation alarm page
//  float PlenLowAlarmTemp;           // degrees
//  char  PlenLowAlarmTimer;          // mins
//  float PlenHighAlarmTemp;          // degrees
//  char  PlenHighAlarmTimer;         // mins
//  // air cure mode (onion)
//  float CureTempLowLimit;           // degrees
//  float CureTempHighLimit;          // degrees
//
//  // system runtimes page
//  char RunTimes[48];                // 48 elements represent each half hour in 24 hours
//  char CureRunTimes[48];            // cure mode runtimes
//  char ClimacellTimes[48];          // climacell runtimes
//
//  // frequency drive control page
//  char  MaxFanSpeed;                // %
//  char  MinFanSpeed;                // %
//  char  RefrFanSpeed;               // %
//  char  RecircFanSpeed;             // %
//  char  UpdFanSpeed;                // hours
//  float TempDiff;                   // degrees
//  char  TempRef1;                   // select
//  char  TempRef2;                   // select
//  char  FanUpdMode;                 // select (onion)
//
//  // ramp rate page
//  float RampUpdTemp;                // degrees
//  char  RampUpdHours;               // hours
//  float RampTempDiff;               // degrees
//  char  RampTempRef;                // select
//  float RampTargetTemp;             // degrees
//
//  // humidity control page
//  HUMID_CONTROL HumidCtrl[NUM_HUMIDIFIERS];
//
//  // high CO2 level purge page
//  short        Co2Set;              // ppm (1-10000)
//  char         Co2CylceTime;        // hours between purges
//  char         Co2PurgeMode;        // select
//  float        Co2PurgeMaxTemp;     // degrees
//  float        Co2PurgeMinTemp;     // degrees
//  char         Co2PurgeDur;         // mins
//  unsigned int Co2LastPurge;        // counter (secs)
//  unsigned int Co2PurgeStart;       // counter (secs)
//  char         Co2FanOutput;        // %
//  char         Co2DoorOutput;       // %
//
//  // miscellaneous parameters page
//  char  RefrMode;                   // select
//  char  DefrostInt;                 // hours
//  char  DefrostDur;                 // mins
//  float HeatTempThresh;             // degrees
//  float RefrigLimit;                // degrees (onion)
//  char  CavHeat;                    // select
//  float CavDiff;                    // degrees
//  char  CavDutyCycle;               // %
//  char  KbPref;                     // select
//
//  // email page
//  char  EmailAlerts;                 // enable/disable
//  char  EmailTo[EMAIL_TO_LEN+1];
//  char  EmailFrom[EMAIL_FROM_LEN+1];
//  char  EmailServer[EMAIL_FROM_LEN+1];
//  char  EmailAuthType;               // select
//  unsigned short EmailPort;
//  char  EmailAccount[EMAIL_FROM_LEN+1];
//  char  EmailPassword[EMAIL_FROM_LEN+1];
//  char  EmailDisplay[IP_ADD_LEN+1+SITE_ID_LEN+1];  // IP address & name of display that sends the email
//
//  // email alerts setup page
//  char  AlertsToSend[NUM_WARNINGS_V312+8];            // flags to indicate which alerts actually get sent via email
//                                     // must be at least NUM_WARNINGS long - currently larger to allow
//                                     // adding more warnings without having to change/convert settings
//
//  // software/remote off
//  char AuxRemoteOff;                // onion Aux1
//  char CavRemoteOff;
//  char ClimacellRemoteOff;
//  char FanRemoteOff;
//  char HeatRemoteOff;
//  char Hum1RemoteOff;
//  char Hum2RemoteOff;
//  char RefrRemoteOff;
//  char Lights1RemoteOff;
//  char Lights2RemoteOff;
//
//  // software/remote off (onion)
//  char  Aux2RemoteOff;
//  char  BurnerRemoteOff;
//  char  CureRemoteOff;
//
//  // fan boost setup page
//  char  FanBoostMode;               // select
//  char  FanBoostSpeed;              // %
//  char  FanBoostInterval;           // hours (between periods or continuous runtime)
//  char  FanBoostDur;                // mins
//  float FanBoostTemp;               // degrees
//} SYSTEM_SETTINGS_V312;

// system settings
//typedef struct {
//  char  SettingsVersion[VERSION_LEN+1];   // Settings structure version
//  char  ArmVersion[VERSION_LEN+1];        // software version
//  char  DST;                              // Daylight Saving Time
//  char  PrevFanSpeed;                     // % (stores the cooling mode fan speed)
//  unsigned short HttpPort;
//  char  NetMonitorMode;
//  char  Co2PurgeRefrigThresh;             // %
//  char  MultiviewSessions;                // number of systems to tile in multi-view mode
//  char  PublicIP[IP_ADD_LEN+1];
//  char  CavityOrPileCtrl;
//  char  Unused[9];                        // (not used) was display IP address, now using as reserved space
//
//  // logs
//  short UserLogInterval;            // mins
//  char  UserLogWrap;
//  char  PIDLogWrap;
//  char  LogPIDDoor;
//  char  LogPIDRefrig;
//  char  GraphFavorites[MSG_TX_CGIVAR_SIZE];
//
////  LOAD_MONITOR LoadMonitor;
//
//  // basic setup page
//  char  StorageName[SITE_ID_LEN+1]; // facility
//  char  TempType;                   // Fahrenheit/Celcius
//  char  MasterSlave;
//  char  MasterIP[IP_ADD_LEN+1];
//  char  HomePage[HOMEPAGE_LEN];     // select
//  char  SystemMode;                 // 0 = Potato, 1 = Onion
//  char  Language;                   // select
//  char  FactoryBd[PASSWORD_LEN+1];
//  char  LoginPw[PASSWORD_LEN+1];
//  char  LocalLogin;                 // 0 = No, 1 = Yes
//  char  Animations;                 // 0 = No, 1 = Yes
//
//  // password page
//  PASSWORDS User[NUM_PASSWORDS];
//
//  // analog board setup page
//  ANALOG_BOARD AnalogBoard[ANALOG_BOARDS_PER_SYSTEM];
//
//  // fresh air door setup page
//  short DoorP;                      // PID values
//  short DoorI;
//  short DoorD;
//  short DoorU;
//  short DoorActuatorTime;           // secs (1-500)
//  char  CoolAirCycle;               // mins (1-30)
//
//  // refrigeration setup page
//  REFRIG Refr[NUM_REFRIG_STAGES];
//  short RefrP;                      // PID values
//  short RefrI;
//  short RefrD;
//  short RefrU;
//  char  RefrPurge;                  // select
//
//  // climacell setup page
//  char  ClimacellEff;               // %
//  short ClimP;                      // PID values
//  short ClimI;
//  short ClimD;
//  short ClimU;
//  short Altitude;
//  char  AltitudeType;               // feet/meters
//
//  // burner setup page (onion)
//  char  BurnerMode;                 // select
//  char  BurnerOn;                   // %
//  char  BurnerLow;                  // %
//  short BurnerP;                    // PID values
//  short BurnerI;
//  short BurnerD;
//  short BurnerU;
//  char  BurnerManual;               // %
//
//  // failures 1 setup page
//  char FanFailMode;                 // select
//  char FanFailTimer;                // mins (1-10)
//  char ClimacellFailMode;           // select
//  char ClimacellFailTimer;          // mins (1-120)
//  char RefrFailMode;                // select
//  char RefrFailTimer;               // mins (1-15)
//  char RefrRunFailMode;             // select
//  HUMID_MODE Humid[NUM_HUMIDIFIERS];
//  char AuxFailMode;                 // select
//  char AuxFailTimer;                // mins (1-15)
//  char HeatFailMode;                // select
//  char HeatFailTimer;               // mins (1-15)
//  char CavHeatFailMode;             // select
//  char CavHeatFailTimer;            // mins (1-10)
//  char AirRestFailMode;             // unused
//  char BurnerFailMode;              // select
//  char BurnerFailTimer;             // mins (1-10)
//  char LightsFailMode;              // select
//  char LightsFailTimer;             // mins or hours
//  char LightsFailUnits;             // select (mins or hours)
//
//  // failures 2 setup page
//  char  PlenSenFailMode;            // select
//  char  PlenSenFailTimer;           // mins (1-180)
//  float PlenSenDiff;                // degrees
//  char  OutAirFailMode;             // select
//  char  OutAirFailTimer;            // mins (1-180)
//  char  OutHumFailMode;             // select
//  char  OutHumFailTimer;            // mins (1-180)
//  char  PlenHumFailMode;            // select
//  char  PlenHumFailTimer;           // mins (1-180)
//  char  LowHumSet;                  // %
//  char  HighCo2FailMode;            // select
//  char  HighCo2FailTimer;           // mins (1-180)
//  int   HighCo2Set;                 // ppm
//
//  // controller access page
//  NETWORK_NODES Node[NUM_NETWORK_NODES];
//
//  // service info page
//  char DealerName[NAME_LEN+1];
//  char DealerPhone[PHONE_LEN+1];
//  char TechName[NAME_LEN+1];
//  char TechPhone[PHONE_LEN+1];
//
//  // plenum setup page
//  float        PlenTempSet;         // degrees
//  char         PlenHumSet;          // %
//  unsigned int FanDailyRunTime;     // secs (displayed as hours)
//  unsigned int FanTotalRefrigTime;  // secs (displayed as hours) (FanTotalRecircTime & FanTotalStandbyTime defined at bottom)
//  unsigned int FanDailyDay;         // day value for daily reset
//  unsigned int FanTotalRunTime;     // secs (displayed as hours)
//  unsigned int FanTotalCoolingTime; // secs (displayed as hours)
//  char         HumSetpointRef;      // 0 - plenum, 1 - return air (onion)
//  float        BurnerTempSet;       // degrees (shared for manual mode max temp)
//  float        BurnerThreshold;     // %       (shared for manual mode restart temp)
//
//  // outside air cooling control page
//  float OutAirDiff;                 // degrees
//  char  OutAirAboveBelow;           // select
//  char  OutAirTempRef;              // select
//  char  CalcHumMax;                 // % (onion)
//  // air cure mode (onion)
//  float CureStartTemp;              // degrees
//  char  CureStartHumid;             // %
//  char  CureHumidHighLimit;         // %
//  char  CureHumidRef;               // select
//
//  // plenum temp deviation alarm page
//  float PlenLowAlarmTemp;           // degrees
//  char  PlenLowAlarmTimer;          // mins
//  float PlenHighAlarmTemp;          // degrees
//  char  PlenHighAlarmTimer;         // mins
//  // air cure mode (onion)
//  float CureTempLowLimit;           // degrees
//  float CureTempHighLimit;          // degrees
//
//  // system runtimes page
//  char RunTimes[48];                // 48 elements represent each half hour in 24 hours
//  char CureRunTimes[48];            // cure mode runtimes
//  char ClimacellTimes[48];          // climacell runtimes
//
//  // frequency drive control page
//  char  MaxFanSpeed;                // %
//  char  MinFanSpeed;                // %
//  char  RefrFanSpeed;               // %
//  char  RecircFanSpeed;             // %
//  char  UpdFanSpeed;                // hours
//  float TempDiff;                   // degrees
//  char  TempRef1;                   // select
//  char  TempRef2;                   // select
//  char  FanUpdMode;                 // select (onion)
//
//  // ramp rate page
//  float RampUpdTemp;                // degrees
//  char  RampUpdHours;               // hours
//  float RampTempDiff;               // degrees
//  char  RampTempRef;                // select
//  float RampTargetTemp;             // degrees
//
//  // humidity control page
//  HUMID_CONTROL HumidCtrl[NUM_HUMIDIFIERS];
//
//  // high CO2 level purge page
//  short        Co2Set;              // ppm (1-10000)
//  char         Co2CylceTime;        // hours between purges
//  char         Co2PurgeMode;        // select
//  float        Co2PurgeMaxTemp;     // degrees
//  float        Co2PurgeMinTemp;     // degrees
//  char         Co2PurgeDur;         // mins
//  unsigned int Co2LastPurge;        // counter (secs)
//  unsigned int Co2PurgeStart;       // counter (secs)
//  char         Co2FanOutput;        // %
//  char         Co2DoorOutput;       // %
//
//  // miscellaneous parameters page
//  char  RefrMode;                   // select
//  char  DefrostInt;                 // hours
//  char  DefrostDur;                 // mins
//  float HeatTempThresh;             // degrees
//  float RefrigLimit;                // degrees (onion)
//  char  CavHeat;                    // select
//  float CavDiff;                    // degrees
//  char  CavDutyCycle;               // %
//  char  KbPref;                     // select
//
//  // email page
//  char  EmailAlerts;                 // enable/disable
//  char  EmailTo[EMAIL_TO_LEN+1];
//  char  EmailFrom[EMAIL_FROM_LEN+1];
//  char  EmailServer[EMAIL_FROM_LEN+1];
//  char  EmailAuthType;               // select
//  unsigned short EmailPort;
//  char  EmailAccount[EMAIL_FROM_LEN+1];
//  char  EmailPassword[EMAIL_FROM_LEN+1];
//  char  EmailDisplay[IP_ADD_LEN+1+SITE_ID_LEN+1];  // IP address & name of display that sends the email
//
//  // email alerts setup page
//  char  AlertsToSend[NUM_WARNINGS_V316+8];            // flags to indicate which alerts actually get sent via email
//                                     // must be at least NUM_WARNINGS long - currently larger to allow
//                                     // adding more warnings without having to change/convert settings
//
//  // software/remote off
//  char AuxRemoteOff;                // onion Aux1
//  char CavRemoteOff;
//  char ClimacellRemoteOff;
//  char FanRemoteOff;
//  char HeatRemoteOff;
//  char Hum1RemoteOff;
//  char Hum2RemoteOff;
//  char RefrRemoteOff;
//  char Lights1RemoteOff;
//  char Lights2RemoteOff;
//
//  // software/remote off (onion)
//  char  Aux2RemoteOff;
//  char  BurnerRemoteOff;
//  char  CureRemoteOff;
//
//  // fan boost setup page
//  char  FanBoostMode;               // select
//  char  FanBoostSpeed;              // %
//  char  FanBoostInterval;           // hours (between periods or continuous runtime)
//  char  FanBoostDur;                // mins
//  float FanBoostTemp;               // degrees
//
//  // fan run times (FanTotalRefrigTime & FanTotalCoolingTime defined in plenum setup page section)
//  unsigned int FanTotalRecircTime;  // secs (displayed as hours)
//  unsigned int FanTotalStandbyTime; // secs (displayed as hours)
//  unsigned int FanTotalCureTime;    // secs (displayed as hours)
//
//  // 4-20 output channels
//  char PWM_doors;
//  char PWM_refrig;
//  char PWM_fan;
//  char PWM_burner;
//} SYSTEM_SETTINGS_V316;

/*** external variables ***/

/*** external functions ***/

#endif

/***   End Of File   ***/
