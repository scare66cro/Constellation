/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     StorePostData.h

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:

COMMENTS:

***************************************************************************/

#ifndef STOREPOSTDATA_H
#define STOREPOSTDATA_H

/*** include files ***/

/*** defines ***/

// action post tags
#define TAG_A_SETDEFAULT          0
#define TAG_A_PANELDEFAULT        1
#define TAG_A_FACTORYDEFAULT      2
#define TAG_A_CLEARALARM          3
#define TAG_A_USERLOGGRAPH        4
#define TAG_A_USERLOGTABLE        5
#define TAG_A_USERLOGDOWNLOAD     6
#define TAG_A_USERLOGCLEAR        7
#define TAG_A_SYSTEMLOGSHOW       8
#define TAG_A_SYSTEMLOGDOWNLOAD   9
#define TAG_A_SYSTEMLOGCLEAR      10
#define TAG_A_WARNINGLOGSHOW      11
#define TAG_A_SDCARDINIT          12
#define TAG_A_NEXTBOARD           13
#define TAG_A_PREVBOARD           14
#define TAG_A_SAMEBOARD           15
#define TAG_A_FINDBOARD           16
#define TAG_A_BOOTLOADER          17
#define TAG_A_CLEARDIAG           18
#define TAG_A_PIDLOGDOWNLOAD      19
#define TAG_A_PIDLOGCLEAR         20
#define TAG_A_PIDLOGSHOW          21
#define TAG_A_SLAVEUPD            22
#define TAG_A_SHOWPASSWORD        23
#define TAG_A_SETTINGSDOWNLOAD    24
#define TAG_A_LOADLOGGRAPH        25
#define TAG_A_LOADLOGTABLE        26
#define TAG_A_LOADLOGCLEAR        27
#define TAG_A_LOADLOGSTART        28
#define TAG_A_LOADLOGCLEARSENSORS 29
#define TAG_A_LOADLOGDOWNLOAD     30
#define TAG_A_RESETIOCONFIG       31
#define TAG_A_RESETPWMCONFIG      32
#define TAG_A_NEXTAUXPROGRAM      33
#define TAG_A_PREVAUXPROGRAM      34
#define TAG_A_FILESTART           35
#define TAG_A_NODEDELETEALL       36

#define UI_NUM_ACTIONPOSTS        37

// program post tags
#define TAG_P1_PLENSETUP          0
#define TAG_P1_FANDAILY           1
#define TAG_P1_FANTOTAL           2
#define TAG_P1_OUTSIDEAIR         3
#define TAG_P1_TEMPALARMS         4
#define TAG_P1_RUNTIMES           5
#define TAG_P1_CURFANSPEED        6
#define TAG_P1_FANSPEEDS          7
#define TAG_P1_RAMPRATE           8
#define TAG_P1_HUMIDCTRL          9
#define TAG_P1_CO2PURGE           10
#define TAG_P1_MISC               11
#define TAG_P1_EMAIL              12
#define TAG_P1_ALERTS             13
#define TAG_P1_DATETIME           14
#define TAG_P1_HTTPPORT           15
#define TAG_P1_LTXINIT            16
#define TAG_P1_PASSWORD           17
#define TAG_P1_FANBOOST           18
#define TAG_P1_AIRCURE            19
#define TAG_P1_CURELIMITS         20
#define TAG_P1_BURNERSETUP        21
#define TAG_P1_BURNERMANSETUP     22
#define TAG_P1_CLIMACELLTIMES     23
#define TAG_P1_LOADMONITOR        24
#define TAG_P1_LOADLOGPIPE        25
#define TAG_P1_LOADLOGPAUSE       26
#define TAG_P1_LOADLOGSTOP        27
#define TAG_P1_BAYNAMES           28
#define TAG_P1_PUBLICIP           29

#define UI_NUM_P1_TAGS            30

#define TAG_P2_BASIC              UI_NUM_P1_TAGS+0
#define TAG_P2_USERLOG            UI_NUM_P1_TAGS+1
#define TAG_P2_PASSWORD           UI_NUM_P1_TAGS+2
#define TAG_P2_ANALOGBRD          UI_NUM_P1_TAGS+3
#define TAG_P2_DOOR               UI_NUM_P1_TAGS+4
#define TAG_P2_REFRIG             UI_NUM_P1_TAGS+5
#define TAG_P2_CLIMACELL          UI_NUM_P1_TAGS+6
#define TAG_P2_FAIL1              UI_NUM_P1_TAGS+7
#define TAG_P2_FAIL2              UI_NUM_P1_TAGS+8
#define TAG_P2_NODEADD            UI_NUM_P1_TAGS+9
#define TAG_P2_NODEDELETE         UI_NUM_P1_TAGS+10
#define TAG_P2_NODEDISCOVER       UI_NUM_P1_TAGS+11
#define TAG_P2_SERVICE            UI_NUM_P1_TAGS+12
#define TAG_P2_PIDLOG             UI_NUM_P1_TAGS+13
#define TAG_P2_BURNER             UI_NUM_P1_TAGS+14
#define TAG_P2_MASTERSLAVE        UI_NUM_P1_TAGS+15
#define TAG_P2_NODEUPDATE         UI_NUM_P1_TAGS+16
#define TAG_P2_PWMCHANNEL         UI_NUM_P1_TAGS+17
#define TAG_P2_IOCONFIG           UI_NUM_P1_TAGS+18
#define TAG_P2_IORENAME           UI_NUM_P1_TAGS+19
#define TAG_P2_AUXPROG            UI_NUM_P1_TAGS+20
#define TAG_GRAPHFAVORITES        UI_NUM_P1_TAGS+21
#define TAG_SAVESETTINGS          UI_NUM_P1_TAGS+22
#define TAG_EQUIPDESC             UI_NUM_P1_TAGS+23
#define TAG_PWMDESC               UI_NUM_P1_TAGS+24
#define TAG_QUERYTAG              UI_NUM_P1_TAGS+25
#define TAG_SYSLOGREC             UI_NUM_P1_TAGS+26
#define TAG_SYSLOGEQUIP           UI_NUM_P1_TAGS+27
#define TAG_SYSLOGREMOTE          UI_NUM_P1_TAGS+28
#define TAG_PIDLOG                UI_NUM_P1_TAGS+29
#define TAG_SYSMODE               UI_NUM_P1_TAGS+30
#define TAG_BOARDLABEL            UI_NUM_P1_TAGS+31
#define TAG_TEMPSENSOR            UI_NUM_P1_TAGS+32
#define TAG_HUMIDSENSOR           UI_NUM_P1_TAGS+33
#define TAG_BAYLABEL              UI_NUM_P1_TAGS+34

#define UI_NUM_PROGRAMPOSTS       UI_NUM_P1_TAGS+35

#define UI_NUM_EQUIPPOSTS         33

// Post Types (PT)
#define PT_ACTION                 1
#define PT_EQUIP                  2
#define PT_PROGRAM                3

/*** typedefs and structures ***/

typedef enum
{
  SPV_RANGE   = -2,
  SPV_NAN     = -1,
  SPV_SUCCESS = 0,
  SPV_ELEMENT = 1,
  SPV_TYPE    = 2
} STOREPOST_RESULT;

typedef struct
{
  char Tag[MSG_MAX_TAG_LEN+1];
} UI_POST_ACTION;

typedef struct
{
  char Tag[MSG_MAX_TAG_LEN+1];
  uint8_t *SettingsPtr;
  void (*StoreData)(uint8_t *);
  void (*Reply)(int);
} UI_POST_EQUIPSTATUS;

typedef struct
{
  char Tag[MSG_MAX_TAG_LEN+1];
  void (*StoreData)(void);
  void (*Reply)(int);
} UI_POST_PROGRAMPAGE;

/*** external variables ***/

extern UI_POST_ACTION      UI_ActionPosts[UI_NUM_ACTIONPOSTS];
extern UI_POST_EQUIPSTATUS UI_EquipPosts[UI_NUM_EQUIPPOSTS];
extern UI_POST_PROGRAMPAGE UI_ProgramPosts[UI_NUM_PROGRAMPOSTS];

extern char LtxInitialized;
extern char LtxHttpSession;
extern signed char LtxPgmLevel;

/*** external functions ***/

extern void ConvertSpecialChars(char *str, int length);
extern STOREPOST_RESULT StorePostValue(char *element, void *setting, ELEMENT_TYPE type, size_t length);
extern void StoreSettings(void);
extern void StoreSlaveUpdate(char *dateStr, char *timeStr, uint8_t amPm, char *tempStr, char *humidStr);
extern void UI_PostMsg_Init(void);
extern int ValidatePostTag(char *Tag, int *PostType);

#endif

/***   End Of File   ***/
