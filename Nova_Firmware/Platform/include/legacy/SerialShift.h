/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     SerialShift.h

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:

COMMENTS:

***************************************************************************/

#ifndef SERIALSHIFT_H
#define SERIALSHIFT_H

/*** include files ***/

// Platform
#include "pinout.h"

// Gellert
#include "Analog_Input.h"

/*** defines ***/

#define LOG_LABELS                50    // this value is used for the equipment name length, if it's changed it must
                                        // also be changed in the http server code LtxWarnings.h (for translated strings)

#define EQUIPSTATUS_LEN           (69)
#define LIGHTSTATUS_START         (36)

#define MAIN_PWM_OUTPUTS          (2)
#define EXPANSION_PWM_OUTPUTS     (2)

#define IO_UNDEFINED              (0xFFFFFFFF)

typedef enum system_boards
{
  MAIN,
  EXPANSION_1,
  EXPANSION_2,
  BOARD_COUNT
} SYSTEM_BOARDS;

typedef enum equipment_io
{
  EQ_FAN,
  EQ_DOORS,
  EQ_REFRIGERATION,
  EQ_CLIMACELL,
  EQ_HEAT,
  EQ_CAVITY_HEAT,
  EQ_BURNER,
  EQ_HUMID_HEAD1,
  EQ_HUMID_PUMP1,
  EQ_HUMID_HEAD2,
  EQ_HUMID_PUMP2,
  EQ_HUMID_HEAD3,
  EQ_HUMID_PUMP3,
  EQ_REFRIG_STAGE1,
  EQ_REFRIG_STAGE2,
  EQ_REFRIG_STAGE3,
  EQ_REFRIG_STAGE4,
  EQ_REFRIG_STAGE5,
  EQ_REFRIG_STAGE6,
  EQ_REFRIG_STAGE7,
  EQ_REFRIG_STAGE8,
  EQ_REFRIG_DEFROST1,
  EQ_REFRIG_DEFROST2,
  EQ_LIGHTS1,
  EQ_LIGHTS2,
  EQ_AUX1,
  EQ_AUX2,
  EQ_AUX3,
  EQ_AUX4,
  EQ_AUX5,
  EQ_AUX6,
  EQ_AUX7,
  EQ_AUX8,
  EQ_POWER,
  EQ_REMOTE_STANDBY,
  EQ_REFRIG_STANDBY,
  EQ_AIR_FLOW,
  EQ_LOW_TEMP,
  EQ_REDLIGHT,
  EQ_YELLOWLIGHT,
  EQ_PULSEDOOR_POWER,
  EQ_PULSEDOOR_OPEN,
  EQ_PULSEDOOR_CLOSE,
  SW_START_STOP,
  SW_FAN_AUTO,
  SW_FAN_MANUAL,
  SW_FRESHAIR_AUTO,
  SW_FRESHAIR_MANUAL,
  SW_CLIMACELL_AUTO,
  SW_CLIMACELL_MANUAL,
  SW_HUMID_AUTO,
  SW_HUMID_MANUAL,
  SW_REFRIG_AUTO,
  SW_CURE_AUTO,
  SW_BURNER_AUTO,
  SW_AUX1_AUTO,
  SW_AUX1_MANUAL,
  SW_AUX2_AUTO,
  SW_AUX2_MANUAL,
  EQ_TOTAL_IO,
} EQUIPMENT_IO;   // NOTE: changes to any of these values must be also be reflected in LtxWarnings.h in the http server code

// Version
#define SS_MAIN_V0                (1ul<<0)
#define SS_MAIN_V1                (1ul<<1)
#define SS_MAIN_V2                (1ul<<2)
#define SS_MAIN_V3                (1ul<<3)

#define SS_CPLD_VERSION_LENGTH    (4)

// Switches (main IO)
#define SS_MAIN_SW0               (1ul<<4)
#define SS_MAIN_SW1               (1ul<<5)
#define SS_MAIN_SW2               (1ul<<6)
#define SS_MAIN_SW3               (1ul<<7)
#define SS_MAIN_SW4               (1ul<<8)
#define SS_MAIN_SW5               (1ul<<9)
#define SS_MAIN_SW6               (1ul<<10)
#define SS_MAIN_SW7               (1ul<<11)
#define SS_MAIN_SW8               (1ul<<12)
#define SS_MAIN_SW9               (1ul<<13)

// Additional switches for v2 CPLD
#define SS_MAIN_SW10              (1ul<<14)
#define SS_MAIN_SW11              (1ul<<15)
#define SS_MAIN_SW12              (1ul<<16)
#define SS_MAIN_SW13              (1ul<<17)
#define SS_MAIN_SW14              (1ul<<18)
#define SS_MAIN_SW15              (1ul<<19)
#define SS_MAIN_SW16              (1ul<<20)
#define SS_MAIN_SW17              (1ul<<21)

#define SS_MAIN_SW_INPUTS_V1      (10)
#define SS_MAIN_SW_INPUTS_V2      (18)

// Inputs (main IO) v1 CPLD
#define SS_MAIN_AIRFLOW_V1        (1ul<<14)
#define SS_MAIN_LOWTEMP_V1        (1ul<<15)
#define SS_MAIN_DI8_V1            (1ul<<16)
#define SS_MAIN_DI7_V1            (1ul<<17)
#define SS_MAIN_DI6_V1            (1ul<<18)
#define SS_MAIN_DI5_V1            (1ul<<19)
#define SS_MAIN_DI4_V1            (1ul<<20)
#define SS_MAIN_DI3_V1            (1ul<<21)
#define SS_MAIN_DI2_V1            (1ul<<22)
#define SS_MAIN_DI1_V1            (1ul<<23)

// Inputs (main IO) v2 CPLD
#define SS_MAIN_AIRFLOW_V2        (1ul<<22)
#define SS_MAIN_LOWTEMP_V2        (1ul<<23)
#define SS_MAIN_DI8_V2            (1ul<<24)
#define SS_MAIN_DI7_V2            (1ul<<25)
#define SS_MAIN_DI6_V2            (1ul<<26)
#define SS_MAIN_DI5_V2            (1ul<<27)
#define SS_MAIN_DI4_V2            (1ul<<28)
#define SS_MAIN_DI3_V2            (1ul<<29)
#define SS_MAIN_DI2_V2            (1ul<<30)
#define SS_MAIN_DI1_V2            (1ul<<31)

#define SS_MAIN_DI_INPUTS         (10)
#define SS_MAIN_IO_INPUTS_V1      (SS_CPLD_VERSION_LENGTH + SS_MAIN_SW_INPUTS_V1 + SS_MAIN_DI_INPUTS)
#define SS_MAIN_IO_INPUTS_V2      (SS_CPLD_VERSION_LENGTH + SS_MAIN_SW_INPUTS_V2 + SS_MAIN_DI_INPUTS)

// Outputs (main IO)
#define SS_MAIN_PD_DN             (1ul<<0)
#define SS_MAIN_PD_UP             (1ul<<1)
#define SS_MAIN_PD_PWR            (1ul<<2)  // unused - the CPLD automatically turns the power on
#define SS_MAIN_DO8               (1ul<<3)
#define SS_MAIN_DO7               (1ul<<4)
#define SS_MAIN_DO6               (1ul<<5)
#define SS_MAIN_DO5               (1ul<<6)
#define SS_MAIN_DO4               (1ul<<7)
#define SS_MAIN_DO3               (1ul<<8)
#define SS_MAIN_DO2               (1ul<<9)
#define SS_MAIN_DO1               (1ul<<10)

#define SS_MAIN_IO_OUTPUTS        (11)

// State (expansion IO)
#define SS_EX_HEADER              (1ul<<4)
#define SS_EX_FAULT               (1ul<<5)

#define SS_CPLD_HEADER_LENGTH     (2)

// Inputs (expansion IO)
#define SS_EX_DI8                 (1ul<<6)
#define SS_EX_DI7                 (1ul<<7)
#define SS_EX_DI6                 (1ul<<8)
#define SS_EX_DI5                 (1ul<<9)
#define SS_EX_DI4                 (1ul<<10)
#define SS_EX_DI3                 (1ul<<11)
#define SS_EX_DI2                 (1ul<<12)
#define SS_EX_DI1                 (1ul<<13)

#define SS_EX_DI_INPUTS           (8)

// Switches (expansion IO)
#define SS_EX_SW0                 (1ul<<14)
#define SS_EX_SW1                 (1ul<<15)
#define SS_EX_SW2                 (1ul<<16)
#define SS_EX_SW3                 (1ul<<17)
#define SS_EX_SW4                 (1ul<<18)
#define SS_EX_SW5                 (1ul<<19)
#define SS_EX_SW6                 (1ul<<20)
#define SS_EX_SW7                 (1ul<<21)

#define SS_EX_SW_INPUTS           (8)
#define SS_EX_IO_INPUTS           (SS_CPLD_VERSION_LENGTH + SS_CPLD_HEADER_LENGTH + SS_EX_DI_INPUTS + SS_EX_SW_INPUTS)

// Outputs (expansion IO)
#define SS_EX_DO8                 (1ul<<0)
#define SS_EX_DO7                 (1ul<<1)
#define SS_EX_DO6                 (1ul<<2)
#define SS_EX_DO5                 (1ul<<3)
#define SS_EX_DO4                 (1ul<<4)
#define SS_EX_DO3                 (1ul<<5)
#define SS_EX_DO2                 (1ul<<6)
#define SS_EX_DO1                 (1ul<<7)

#define SS_EX_IO_OUTPUTS          (8)

#define SS_PORT_ID_MULTIPLIER     (SS_MAIN_IO_OUTPUTS + 1)

#define BIT_SET(bit, value)   ((value&(1UL<<bit))&&(1UL<<bit))
//#define SET_BIT(bit, value)   (value|=(1UL<<bit))
//#define CLEAR_BIT(bit, value) (value&=(~(1UL<<bit)))

/*** typedefs and structures ***/

typedef struct
{
  char Name[6];
  unsigned char Version;
  unsigned char SwitchesActive;
  unsigned char Fault;
  _pin_str  ChipSelect;
  unsigned int InputState;
  unsigned int OutputState;
  unsigned int InputShiftLength;
  unsigned int OutputShiftLength;
  unsigned int NumPwms;
  unsigned int NumInputs;
  unsigned int NumOutputs;
  unsigned int Input[SS_MAIN_DI_INPUTS+1];    // +1 so index can be 1-based instead of 0-based
  unsigned int Output[SS_MAIN_IO_OUTPUTS+1];
} BOARD_INFO;

typedef enum {
  MODE_NONE,    // for I/O that's not configurable and shouldn't be in the list like pulse doors
  MODE_POTATO,
  MODE_ONION,
  MODE_BEE,
  MODE_ALL
} SYSTEM_MODE;

typedef enum {
  IO_OUTPUT,
  IO_INPUT,
  IO_BOTH,
  IO_SWITCH,
  IO_NONE
} IO_OPTION;

typedef struct
{
  char Name[LOG_LABELS];
  char Renamable;
  char Enabled;
  unsigned int Input;
  unsigned int Output;
  SYSTEM_MODE Mode;
  IO_OPTION IO;
} IO_CONFIG;

/*** external variables ***/

extern EQUIPMENT_IO PulseDoor;
extern int  PulseDoorFlag;
extern int  PulseDoorInit;
extern int  PulseDoorMove;
extern int  PulseDoorPosition;

extern BOARD_INFO IoBoard[BOARD_COUNT];

/*** external functions ***/

extern int CheckInputs(unsigned int bit_const);
extern int CheckOutputs(unsigned int bit_const);
extern int ClearBoardIo(SYSTEM_BOARDS board);
extern int FanOn(void);
extern void GetAvailableIo(char *message);
extern void GetEquipStatus(char *status, int len);
extern void OutputOff(EQUIPMENT_IO eqIndex);
extern void OutputOn(EQUIPMENT_IO eqIndex);
extern void SerialShift_Init(void);
extern void SetIoConfig(EQUIPMENT_IO eqIndex, char ioType, unsigned int ioPort);

#endif

/***   End Of File   ***/
