/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     SerialShift.c

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:  Support serial shift registers input and output

COMMENTS: There are 32 input registers, but only 24 output registers

***************************************************************************/

/*** include files ***/

#include <stdio.h>
#include <string.h>

// Platform
#include "pinout.h"

// Gellert
#include "debug.h"
#include "PWM.h"
#include "SerialShift.h"
#include "Settings.h"
#include "States.h"
#include "ThreadSerialShift.h"
#include "Timer.h"
#include "UI_Messages.h"

/*** defines ***/

/*** typedefs and structures ***/

/*** module variables ***/

EQUIPMENT_IO PulseDoor;
int  PulseDoorFlag = 0;
int  PulseDoorInit = 0;
int  PulseDoorMove = 0;
int  PulseDoorPosition = 0;

BOARD_INFO IoBoard[BOARD_COUNT];

/*** static functions ***/

/***************************************************************************

FUNCTION:   CheckInputs()

PURPOSE:    Check input status

COMMENTS:

***************************************************************************/
int CheckInputs(unsigned int eqIndex)
{
  unsigned int checkBit;
  unsigned int board;
  unsigned int input;
  int returnVal = 0;

  if (eqIndex < EQ_TOTAL_IO)
  {
    if (Settings.EquipIo[eqIndex].Input == IO_UNDEFINED)
    {
      if (   eqIndex == EQ_POWER
          || eqIndex == EQ_REFRIG_STANDBY
          || eqIndex == EQ_REMOTE_STANDBY)
      {
        returnVal = 1;
      }
      else
      {
        returnVal = 0;
      }
    }
    else
    {
      if (eqIndex < SW_START_STOP)
      {
        board = Settings.EquipIo[eqIndex].Input / SS_PORT_ID_MULTIPLIER;
        input = Settings.EquipIo[eqIndex].Input % SS_PORT_ID_MULTIPLIER;
        checkBit = IoBoard[board].Input[input];
      }
      else
      {
        board = MAIN;
        input = Settings.EquipIo[eqIndex].Input;
        checkBit = input;
      }

      if ((IoBoard[board].InputState & checkBit) == checkBit)
      {
        returnVal = 1;
      }
      else
      {
        returnVal = 0;
      }

      // flip the return value for the bay light inputs
      if (eqIndex == EQ_LIGHTS1 || eqIndex == EQ_LIGHTS2)
      {
        returnVal ^= 1;
      }
    }
  }

  return returnVal;
} // CheckInputs()

/***************************************************************************

FUNCTION:   CheckOutputs()

PURPOSE:    Check output status

COMMENTS:

***************************************************************************/
int CheckOutputs(unsigned int eqIndex)
{
  unsigned int checkBit;
  unsigned int board;
  unsigned int output;

  if (   eqIndex < SW_START_STOP
      && Settings.EquipIo[eqIndex].Output != IO_UNDEFINED)
  {
    board = Settings.EquipIo[eqIndex].Output / SS_PORT_ID_MULTIPLIER;
    output = Settings.EquipIo[eqIndex].Output % SS_PORT_ID_MULTIPLIER;
    checkBit = IoBoard[board].Output[output];

    if ((IoBoard[board].OutputState & checkBit) == checkBit)
    {
      return 1;
    }
    else
    {
      return 0;
    }
  }
  return 0;
} // CheckOutputs()

/***************************************************************************

FUNCTION:   FanOn()

PURPOSE:    Check if the fan is on

COMMENTS:

***************************************************************************/
int FanOn(void)
{
  if (   CheckInputs(SW_FAN_AUTO) && CheckOutputs(EQ_FAN)
      || CheckInputs(SW_FAN_MANUAL) )
  {
    return 1;
  }
  else
  {
    return 0;
  }
} // FanOn()

/***************************************************************************

FUNCTION:   GetAvailableIo()

PURPOSE:    Get the available system I/O

COMMENTS:

***************************************************************************/
void GetAvailableIo(char *message)
{
  int i;
  char str[20];

  strcpy(message, "AvailableIo=");

  for (i = 0; i < BOARD_COUNT; i++)
  {
    if (IoBoard[i].Version != 0x0F)
    {
      snprintf(str,
               sizeof(str),
               "%s:%d:%d:%d,",
               IoBoard[i].Name,
               IoBoard[i].NumOutputs,
               IoBoard[i].NumInputs,
               IoBoard[i].NumPwms);
    }
    else
    {
      snprintf(str, sizeof(str), "%s:%d:%d:%d,", "none", 0, 0, 0);
    }

    strncat(message, str, MSG_TX_BUFFER_SIZE-strlen(message)-1);
  }
} // end GetAvailableIo()

/***************************************************************************

FUNCTION:   GetEquipStatus()

PURPOSE:    Get the input and output status for each system component

COMMENTS:   The green light doesn't have its own status, it looks at the fan
            status.

***************************************************************************/
void GetEquipStatus(char *status, int len)
{
  memset(status, 0, len);

  if (CheckInputs(SW_FAN_AUTO))   status[0] = 1;
  if (CheckInputs(SW_FAN_MANUAL)) status[0] = 2;
  if (CheckInputs(EQ_FAN))        status[1] = 1;
  if (CheckOutputs(EQ_FAN))       status[2] = 1;

  // onion Burner switch (NOTE: Burner switch doesn't have a manual position)
  if (CheckInputs(SW_CLIMACELL_AUTO))     status[3] = 1;
  if (Settings.SystemMode == SM_POTATO)
  {
    if (CheckInputs(SW_CLIMACELL_MANUAL)) status[3] = 2;
  }

  if (CheckInputs(EQ_CLIMACELL))  status[4] = 1;
  if (CheckOutputs(EQ_CLIMACELL)) status[5] = 1;

  // onion (uses different input)
  if (CheckInputs(EQ_BURNER))  status[6] = 1;
  if (CheckOutputs(EQ_BURNER)) status[7] = 1;

  // onion Cure switch (NOTE: Cure switch doesn't have a manual position)
  if (CheckInputs(SW_HUMID_AUTO)) status[8] = 1;
  if (Settings.SystemMode == SM_POTATO)
  {
    if (CheckInputs(SW_HUMID_MANUAL)) status[8] = 2;
  }

  if (CheckInputs(EQ_HUMID_HEAD1))  status[9] = 1;
  if (CheckOutputs(EQ_HUMID_HEAD1)) status[10] = 1;
  if (CheckOutputs(EQ_HUMID_PUMP1)) status[11] = 1;
  if (CheckInputs(EQ_HUMID_HEAD2))  status[12] = 1;
  if (CheckOutputs(EQ_HUMID_HEAD2)) status[13] = 1;
  if (CheckOutputs(EQ_HUMID_PUMP2)) status[14] = 1;

  if (CheckInputs(SW_REFRIG_AUTO))    status[15] = 1;
  if (CheckInputs(EQ_REFRIGERATION))  status[16] = 1;
  if (CheckOutputs(EQ_REFRIG_STAGE1)) status[17] = 1;
  if (CheckOutputs(EQ_REFRIG_STAGE2)) status[18] = 1;
  if (CheckOutputs(EQ_REFRIG_STAGE3)) status[19] = 1;
  if (CheckOutputs(EQ_REFRIG_STAGE4)) status[20] = 1;
  if (CheckOutputs(EQ_REFRIG_STAGE5)) status[21] = 1;
  if (CheckOutputs(EQ_REFRIG_STAGE6)) status[22] = 1;
  if (CheckOutputs(EQ_REFRIG_STAGE7)) status[23] = 1;
  if (CheckOutputs(EQ_REFRIG_STAGE8)) status[24] = 1;

  if (CheckOutputs(EQ_REFRIG_DEFROST1)) status[25] = 1;
  if (CheckOutputs(EQ_REFRIG_DEFROST2)) status[26] = 1;

  // onion Aux1
//  if (CheckInputs(SW_AUX_AUTO))   status[23] = 1;
//  if (CheckInputs(SW_AUX_MANUAL)) status[23] = 2;
//  if (CheckInputs(EQ_AUX))        status[24] = 1;
//  if (CheckOutputs(EQ_AUX))       status[25] = 1;

  // onion Aux2
//  if (CheckInputs(SW_HEAT_AUTO))   status[26] = 1;
//  if (CheckInputs(SW_HEAT_MANUAL)) status[26] = 2;

  if (CheckInputs(EQ_HEAT))        status[27] = 1;
  if (CheckOutputs(EQ_HEAT))       status[28] = 1;

  if (CheckInputs(SW_FRESHAIR_AUTO))   status[29] = 1;
  if (CheckInputs(SW_FRESHAIR_MANUAL)) status[29] = 2;
  if (PulseDoorMove == 0)
  {
    status[30] = 0;
  }
  else
  {
    if (PulseDoor == EQ_PULSEDOOR_CLOSE)
    {
      status[30] = 1;
    }
    else
    {
      status[30] = 2;
    }
  }

  if (CheckInputs(EQ_CAVITY_HEAT))  status[31] = 1;
  if (CheckOutputs(EQ_CAVITY_HEAT)) status[32] = 1;

  // green light - have to check if fan is on
  if (  CheckInputs(SW_START_STOP)
      &&(  (CheckInputs(SW_FAN_AUTO) && CheckOutputs(EQ_FAN))
         || CheckInputs(SW_FAN_MANUAL)))
  {
    status[33] = 1;
  }

  if (LightStatus.Yellow == LT_ON)    status[34] = 1;
  if (LightStatus.Yellow == LT_BLINK) status[34] = 2;
  if (LightStatus.Red    == LT_ON)    status[35] = 1;
  if (LightStatus.Red    == LT_BLINK) status[35] = 2;

  // bay lights
  if (!CheckInputs(EQ_LIGHTS1)) status[36] = 1;
  if (CheckOutputs(EQ_LIGHTS1)) status[37] = 1;
  if (!CheckInputs(EQ_LIGHTS2)) status[38] = 1;
  if (CheckOutputs(EQ_LIGHTS2)) status[39] = 1;

  // renamable outputs
  if (CheckInputs(EQ_AUX1))  status[40] = 1;
  if (CheckOutputs(EQ_AUX1)) status[41] = 1;
  if (CheckInputs(EQ_AUX2))  status[42] = 1;
  if (CheckOutputs(EQ_AUX2)) status[43] = 1;
  if (CheckInputs(EQ_AUX3))  status[44] = 1;
  if (CheckOutputs(EQ_AUX3)) status[45] = 1;
  if (CheckInputs(EQ_AUX4))  status[46] = 1;
  if (CheckOutputs(EQ_AUX4)) status[47] = 1;
  if (CheckInputs(EQ_AUX5))  status[48] = 1;
  if (CheckOutputs(EQ_AUX5)) status[49] = 1;
  if (CheckInputs(EQ_AUX6))  status[50] = 1;
  if (CheckOutputs(EQ_AUX6)) status[51] = 1;
  if (CheckInputs(EQ_AUX7))  status[52] = 1;
  if (CheckOutputs(EQ_AUX7)) status[53] = 1;
  if (CheckInputs(EQ_AUX8))  status[54] = 1;
  if (CheckOutputs(EQ_AUX8)) status[55] = 1;

  // additional humidifier
  if (CheckInputs(EQ_HUMID_HEAD3))  status[56] = 1;
  if (CheckOutputs(EQ_HUMID_HEAD3)) status[57] = 1;
  if (CheckOutputs(EQ_HUMID_PUMP3)) status[58] = 1;

  // refrigeration stage inputs
  int i;
  for (i = 0; i < NUM_REFRIG_STAGES; ++i)
  {
    status[59 + i] = CheckInputs((EQUIPMENT_IO) (EQ_REFRIG_STAGE1 + i)); // 59 - 66
  }
  for (i = 0; i < NUM_DEFROST_STAGES; ++i)
  {
    status[67 + i] = CheckInputs((EQUIPMENT_IO) (EQ_REFRIG_DEFROST1 + i)); // 67 - 68
  }
} // end GetEquipStatus()

/***************************************************************************

FUNCTION:   SetIoConfig()

PURPOSE:    Set I/O configuration element

COMMENTS:

***************************************************************************/
void SetIoConfig(EQUIPMENT_IO eqIndex, char ioType, unsigned int ioPort)
{
  int outputOn = 0;

  if (eqIndex < SW_START_STOP)
  {
    if (ioType == 'i')
    {
      Settings.EquipIo[eqIndex].Input = ioPort;
    }
    else if (ioType == 'o')
    {
      if (Settings.EquipIo[eqIndex].Output != ioPort)
      {
        outputOn = CheckOutputs(eqIndex);
        OutputOff(eqIndex);
      }

      Settings.EquipIo[eqIndex].Output = ioPort;

      if (ioPort == IO_UNDEFINED)
      {
        Settings.EquipIo[eqIndex].Enabled = 0;
      }
      else
      {
        Settings.EquipIo[eqIndex].Enabled = 1;
      }

      if (ioPort == IO_UNDEFINED)
      {
        OutputOff(eqIndex);
      }
      else if (outputOn)
      {
        OutputOn(eqIndex);
      }
    }
  }
} // end SetIoConfig()

/***************************************************************************

FUNCTION:   OutputOff()

PURPOSE:    Turn off an output

COMMENTS:

***************************************************************************/
void OutputOff(EQUIPMENT_IO eqIndex)
{
  unsigned int checkBit;
  unsigned int board;
  unsigned int output;

  // if the output is defined, turn it off
  if (   eqIndex < SW_START_STOP
      && Settings.EquipIo[eqIndex].Output != IO_UNDEFINED)
  {
    board = Settings.EquipIo[eqIndex].Output / SS_PORT_ID_MULTIPLIER;
    output = Settings.EquipIo[eqIndex].Output % SS_PORT_ID_MULTIPLIER;
    checkBit = IoBoard[board].Output[output];

    IoBoard[board].OutputState &= ~(checkBit);
  }
} // OutputOff()

/***************************************************************************

FUNCTION:   OutputOn()

PURPOSE:    Turn on an output

COMMENTS:

***************************************************************************/
void OutputOn(EQUIPMENT_IO eqIndex)
{
  unsigned int checkBit;
  unsigned int board;
  unsigned int output;

  // if the output is enabled but undefined it indicates a missing expansion
  // board, so report an error
  if (   Settings.EquipIo[eqIndex].Enabled
      && Settings.EquipIo[eqIndex].Output == IO_UNDEFINED)
  {
//    SystemAlarm[AL_SYSCONFIG] = FM_FAIL;
    WarningsSet(WARN_SYSCONFIG_EQ, FM_ALARM, NA, eqIndex);
//    SystemState = ST_SYSCONFIGFAIL;
  }

  // if the output is defined, turn it on
  if (   eqIndex < SW_START_STOP
      && Settings.EquipIo[eqIndex].Output != IO_UNDEFINED)
  {
    board = Settings.EquipIo[eqIndex].Output / SS_PORT_ID_MULTIPLIER;
    output = Settings.EquipIo[eqIndex].Output % SS_PORT_ID_MULTIPLIER;
    checkBit = IoBoard[board].Output[output];

    IoBoard[board].OutputState |= checkBit;
  }
} // OutputOn()

/***************************************************************************

FUNCTION:   SerialShift_Init()

PURPOSE:    Initialize the serial shift registers

COMMENTS:

***************************************************************************/
void SerialShift_Init(void)
{
  int i,j;
  unsigned int configuration;
  _pin_str chipSelect[BOARD_COUNT] = {SS_CS0, SS_CS1, SS_CS2};

  memset(IoBoard, 0, sizeof(IoBoard));

  // main board
  configuration = ReadInput(chipSelect[MAIN], SS_CPLD_VERSION_LENGTH);
  for (i = 0; i < SS_CPLD_VERSION_LENGTH; i++)
  {
    if (configuration & (1 << (3-i)))
    {
      IoBoard[MAIN].Version |= 1 << i;
    }
  }
  debug_printf("Main CPLD v%d\r\n", IoBoard[MAIN].Version);

  // no header information
  IoBoard[MAIN].SwitchesActive = 1;   // switches active by default
  IoBoard[MAIN].Fault = 0;

  strcpy(IoBoard[MAIN].Name, "Main");
  memcpy(&IoBoard[MAIN].ChipSelect, &(chipSelect[MAIN]), sizeof(_pin_str));
  IoBoard[MAIN].OutputShiftLength = SS_MAIN_IO_OUTPUTS;
  IoBoard[MAIN].NumInputs = SS_MAIN_DI_INPUTS;
  IoBoard[MAIN].NumOutputs = SS_MAIN_IO_OUTPUTS;
  IoBoard[MAIN].NumPwms = MAIN_PWM_OUTPUTS;

  // digital outputs
  IoBoard[MAIN].Output[1] = SS_MAIN_DO1;
  IoBoard[MAIN].Output[2] = SS_MAIN_DO2;
  IoBoard[MAIN].Output[3] = SS_MAIN_DO3;
  IoBoard[MAIN].Output[4] = SS_MAIN_DO4;
  IoBoard[MAIN].Output[5] = SS_MAIN_DO5;
  IoBoard[MAIN].Output[6] = SS_MAIN_DO6;
  IoBoard[MAIN].Output[7] = SS_MAIN_DO7;
  IoBoard[MAIN].Output[8] = SS_MAIN_DO8;
  IoBoard[MAIN].Output[9] = SS_MAIN_PD_PWR;
  IoBoard[MAIN].Output[10] = SS_MAIN_PD_DN;
  IoBoard[MAIN].Output[11] = SS_MAIN_PD_UP;

  // set the switch outputs
  for (i = SW_START_STOP; i < EQ_TOTAL_IO; i++)
  {
    Settings.EquipIo[i].Output = IO_UNDEFINED;
  }

  switch (IoBoard[MAIN].Version)
  {
    case 1:
      IoBoard[MAIN].InputShiftLength = SS_MAIN_IO_INPUTS_V1;

      // digital inputs
      IoBoard[MAIN].Input[1] = SS_MAIN_DI1_V1;
      IoBoard[MAIN].Input[2] = SS_MAIN_DI2_V1;
      IoBoard[MAIN].Input[3] = SS_MAIN_DI3_V1;
      IoBoard[MAIN].Input[4] = SS_MAIN_DI4_V1;
      IoBoard[MAIN].Input[5] = SS_MAIN_DI5_V1;
      IoBoard[MAIN].Input[6] = SS_MAIN_DI6_V1;
      IoBoard[MAIN].Input[7] = SS_MAIN_DI7_V1;
      IoBoard[MAIN].Input[8] = SS_MAIN_DI8_V1;
      IoBoard[MAIN].Input[9] = SS_MAIN_AIRFLOW_V1;
      IoBoard[MAIN].Input[10] = SS_MAIN_LOWTEMP_V1;

      // set the switch inputs
      // potato switches
      Settings.EquipIo[SW_START_STOP].Input = SS_MAIN_SW0;
      Settings.EquipIo[SW_REFRIG_AUTO].Input = SS_MAIN_SW1;
      Settings.EquipIo[SW_FAN_MANUAL].Input = SS_MAIN_SW2;
      Settings.EquipIo[SW_FAN_AUTO].Input = SS_MAIN_SW3;
      Settings.EquipIo[SW_CLIMACELL_MANUAL].Input = SS_MAIN_SW4;
      Settings.EquipIo[SW_CLIMACELL_AUTO].Input = SS_MAIN_SW5;
      Settings.EquipIo[SW_HUMID_MANUAL].Input = SS_MAIN_SW6;
      Settings.EquipIo[SW_HUMID_AUTO].Input = SS_MAIN_SW7;
      Settings.EquipIo[SW_FRESHAIR_AUTO].Input = SS_MAIN_SW8;
      Settings.EquipIo[SW_FRESHAIR_MANUAL].Input = SS_MAIN_SW9;
      break;

    case 2:
    case 3:
      IoBoard[MAIN].InputShiftLength = SS_MAIN_IO_INPUTS_V2;

      IoBoard[MAIN].Input[1] = SS_MAIN_DI1_V2;
      IoBoard[MAIN].Input[2] = SS_MAIN_DI2_V2;
      IoBoard[MAIN].Input[3] = SS_MAIN_DI3_V2;
      IoBoard[MAIN].Input[4] = SS_MAIN_DI4_V2;
      IoBoard[MAIN].Input[5] = SS_MAIN_DI5_V2;
      IoBoard[MAIN].Input[6] = SS_MAIN_DI6_V2;
      IoBoard[MAIN].Input[7] = SS_MAIN_DI7_V2;
      IoBoard[MAIN].Input[8] = SS_MAIN_DI8_V2;
      IoBoard[MAIN].Input[9] = SS_MAIN_AIRFLOW_V2;
      IoBoard[MAIN].Input[10] = SS_MAIN_LOWTEMP_V2;

      // set the switch inputs
      // potato switches
      Settings.EquipIo[SW_AUX1_MANUAL].Input = SS_MAIN_SW2;
      Settings.EquipIo[SW_AUX1_AUTO].Input = SS_MAIN_SW3;
      Settings.EquipIo[SW_AUX2_MANUAL].Input = SS_MAIN_SW4;
      Settings.EquipIo[SW_AUX2_AUTO].Input = SS_MAIN_SW5;

      Settings.EquipIo[SW_START_STOP].Input = SS_MAIN_SW8;
      Settings.EquipIo[SW_REFRIG_AUTO].Input = SS_MAIN_SW9;
      Settings.EquipIo[SW_FAN_MANUAL].Input = SS_MAIN_SW10;
      Settings.EquipIo[SW_FAN_AUTO].Input = SS_MAIN_SW11;
      Settings.EquipIo[SW_CLIMACELL_MANUAL].Input = SS_MAIN_SW12;
      Settings.EquipIo[SW_CLIMACELL_AUTO].Input = SS_MAIN_SW13;
      Settings.EquipIo[SW_HUMID_MANUAL].Input = SS_MAIN_SW14;
      Settings.EquipIo[SW_HUMID_AUTO].Input = SS_MAIN_SW15;
      Settings.EquipIo[SW_FRESHAIR_AUTO].Input = SS_MAIN_SW16;
      Settings.EquipIo[SW_FRESHAIR_MANUAL].Input = SS_MAIN_SW17;
      break;
  }

  // onion switches
  Settings.EquipIo[SW_CURE_AUTO].Input   = Settings.EquipIo[SW_HUMID_AUTO].Input;
  Settings.EquipIo[SW_BURNER_AUTO].Input = Settings.EquipIo[SW_CLIMACELL_AUTO].Input;

  // expansion boards
  strcpy(IoBoard[EXPANSION_1].Name, "Ex 1");
  strcpy(IoBoard[EXPANSION_2].Name, "Ex 2");

  for (j = EXPANSION_1; j <= EXPANSION_2; j++)
  {
    configuration = ReadInput(chipSelect[j], SS_CPLD_VERSION_LENGTH+SS_CPLD_HEADER_LENGTH);

    for (i = 0; i < SS_CPLD_VERSION_LENGTH; i++)
    {
      if (configuration & (1 << (3-i)))
      {
        IoBoard[j].Version |= 1 << i;
      }
    }

    if (IoBoard[j].Version != 0x0F)
    {
      debug_printf("Ex %d CPLD v%d\r\n", j, IoBoard[j].Version);

      if (BIT_SET(4, configuration))
      {
        IoBoard[j].SwitchesActive = 1;
      }
      if (BIT_SET(5, configuration))
      {
        IoBoard[j].Fault = 1;
      }

      memcpy(&IoBoard[j].ChipSelect, &(chipSelect[j]), sizeof(_pin_str));
      IoBoard[j].InputShiftLength = SS_EX_IO_INPUTS;
      IoBoard[j].OutputShiftLength = SS_EX_IO_OUTPUTS;
      IoBoard[j].NumInputs = SS_EX_DI_INPUTS;
      IoBoard[j].NumOutputs = SS_EX_IO_OUTPUTS;
      IoBoard[j].NumPwms = EXPANSION_PWM_OUTPUTS;

      // digital inputs
      IoBoard[j].Input[1] = SS_EX_DI1;
      IoBoard[j].Input[2] = SS_EX_DI2;
      IoBoard[j].Input[3] = SS_EX_DI3;
      IoBoard[j].Input[4] = SS_EX_DI4;
      IoBoard[j].Input[5] = SS_EX_DI5;
      IoBoard[j].Input[6] = SS_EX_DI6;
      IoBoard[j].Input[7] = SS_EX_DI7;
      IoBoard[j].Input[8] = SS_EX_DI8;

      // digital outputs
      IoBoard[j].Output[1] = SS_EX_DO1;
      IoBoard[j].Output[2] = SS_EX_DO2;
      IoBoard[j].Output[3] = SS_EX_DO3;
      IoBoard[j].Output[4] = SS_EX_DO4;
      IoBoard[j].Output[5] = SS_EX_DO5;
      IoBoard[j].Output[6] = SS_EX_DO6;
      IoBoard[j].Output[7] = SS_EX_DO7;
      IoBoard[j].Output[8] = SS_EX_DO8;
    }
    else
    {
      debug_printf("Ex %d not installed.\r\n", j);

      if (ClearBoardIo((SYSTEM_BOARDS) j) > 0)
      {
        WarningsSet(WARN_EXPANSIONBOARD, FM_ALARM, NA, j);
      }
    }
  }
} // SerialShift_Init()

/***************************************************************************

FUNCTION:   ClearBoardIo()

PURPOSE:    Clear equipment I/O settings for a board that has been removed

COMMENTS:

***************************************************************************/
int ClearBoardIo(SYSTEM_BOARDS board)
{
  int i,j;
  int ioCleared = 0;

  for (i = 0; i < SW_START_STOP; i++)
  {
    if (   Settings.EquipIo[i].Input >= (board * SS_PORT_ID_MULTIPLIER)
        && Settings.EquipIo[i].Input <= (board * SS_PORT_ID_MULTIPLIER) + SS_EX_IO_INPUTS)
    {
      Settings.EquipIo[i].Input = IO_UNDEFINED;
      ioCleared++;
    }

    if (   Settings.EquipIo[i].Output >= (board * SS_PORT_ID_MULTIPLIER)
        && Settings.EquipIo[i].Output <= (board * SS_PORT_ID_MULTIPLIER) + SS_EX_IO_OUTPUTS)
    {
      Settings.EquipIo[i].Output = IO_UNDEFINED;
      ioCleared++;
    }
  }

  for (i = 0; i < IoBoard[board].NumPwms; i++)
  {
    for (j = 0; j < PWM_TOTAL_EQ; j++)
    {
      if (Settings.PWM[j].Channel == i + (board * 2))
      {
        Settings.PWM[j].Channel = PWM_UNDEFINED;
        ioCleared++;
      }
    }
  }

  return ioCleared;
} // end ClearBoardIo()

/***   End Of File   ***/
