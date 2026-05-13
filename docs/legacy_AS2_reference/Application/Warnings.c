/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     Warnings.c

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:  UI warning system

COMMENTS:

***************************************************************************/

/*** include files ***/

#include <string.h>
#include <stdio.h>

#include "Settings.h"
#include "States.h"
#include "UI_Messages.h"
#include "Warnings.h"

/*** defines ***/

/*** typedefs and structures ***/

/*** module variables ***/

static WARNING Warning[NUM_WARNINGS];
static WARNING ChkWarning[NUM_WARNINGS];

/*** static functions ***/

static void WarningsSetBitMap(WARNING_ITEMS index, uint32_t eqIo);

/***************************************************************************

FUNCTION: IsBoardWarning()

PURPOSE:  Check if the warning should initiate analog board discovery

COMMENTS: Clear the status & value

***************************************************************************/
int IsBoardWarning(void)
{
  if (   Warning[WARN_NEWBOARD].Status != 0
      || Warning[WARN_BOARDREMOVED].Status != 0
      || Warning[WARN_COMMERR].Status != 0
      || Warning[WARN_DEFAULTTEMP].Status != 0
      || Warning[WARN_BOARDNOTTEMP].Status != 0
      || Warning[WARN_BOARDNOTHUMID].Status != 0
      || Warning[WARN_DEFTEMPDIS].Status != 0
      || Warning[WARN_DEFHUMDIS].Status != 0)
  {
    return 1;
  }
  else
  {
    return 0;
  }
} // end IsBoardWarning()

/***************************************************************************

FUNCTION: WarningsClear()

PURPOSE:  Clear the warnings

COMMENTS: Clear the status & value

***************************************************************************/
void WarningsClear(void)
{
  memset(Warning, 0, sizeof(Warning));
} // end WarningsClear()

/***************************************************************************

FUNCTION: WarningsClear()

PURPOSE:  Clear the warnings

COMMENTS: Clear the status & value

***************************************************************************/
void WarningsClearChk(void)
{
  memset(ChkWarning, 0, sizeof(ChkWarning));
} // end WarningsClear()

/***************************************************************************

FUNCTION: WarningsSendToUI()

PURPOSE:  Send the warnings to the UI

COMMENTS:

***************************************************************************/
void WarningsSendToUI(int ForceSend)
{
  int  i;
  char str[MSG_TX_BUFFER_SIZE] = "";
  char WarningMsg[MSG_TX_BUFFER_SIZE] = "AlarmData=";

  // check to see if warnings have changed before sending
  if (   memcmp(ChkWarning, Warning, sizeof(Warning)) == 0
      && ForceSend == 0)
  {
    return;
  }

  memcpy(ChkWarning, Warning, sizeof(Warning));

  // initiate the multi-message transfer
  UI_SendMultiHdr("Warning,AlarmData", NO_SESSIONID);

  for (i = 0; i < NUM_WARNINGS; i++)
  {
    if (   Warning[i].Status > 0      // if the warning is set
        && Warning[i].Status != FM_PRELIM)  // and not a prelim comm err
    {
      snprintf(str, MSG_TX_BUFFER_SIZE, "%d&%d,%u,%u,", i, Warning[i].Status, Warning[i].Value[0], Warning[i].Value[1]);
      MultiMsgAdd(WarningMsg, "AlarmData=", str, 0);
    }
  }

  // terminate and send remaining message
  MultiMsgAdd(WarningMsg, "", "", 1);

  // terminate the multi-message transfer
  UI_SendMultiEnd("Warning");
} // end WarningsSendToUI()

/***************************************************************************

FUNCTION: WarningsSet()

PURPOSE:  Set a warning

COMMENTS:

***************************************************************************/
void WarningsSet(WARNING_ITEMS index, char status, uint32_t value, uint32_t eqIo)
{
  Warning[index].Status = status;   // TODO: .Status isn't really necessary

  switch (index)
  {
    case WARN_SYSCONFIG_EQ:
    case WARN_NO_OUTPUT:
    case WARN_NEWBOARD:
    case WARN_COMMERR:
    case WARN_BOARDREMOVED:
      WarningsSetBitMap(index, eqIo);
      break;

    case WARN_EXPANSIONBOARD:
      WarningsSetBitMap(index, eqIo - EXPANSION_1);
      break;

    case WARN_LIGHTS:
      WarningsSetBitMap(index, eqIo - EQ_LIGHTS1);
      break;

    case WARN_REFRIG_STAGE:
      WarningsSetBitMap(index, eqIo);
      Warning[index].Value[1] = value;
      break;

    case WARN_REFRIG_DEFROST:
      WarningsSetBitMap(index, eqIo - EQ_REFRIG_DEFROST1);
      Warning[index].Value[1] = value;
      break;

    case WARN_AUX:
      WarningsSetBitMap(index, eqIo);
      Warning[index].Status = value;      // TODO: this is non-standard - fix this convolution
      break;

    case WARN_HUMIDIFIER:
      switch (eqIo)
      {
        case EQ_HUMID_HEAD1:
          WarningsSetBitMap(index, 0);
          break;

        case EQ_HUMID_HEAD2:
          WarningsSetBitMap(index, 1);
          break;

        case EQ_HUMID_HEAD3:
        default:
          WarningsSetBitMap(index, 2);
          break;
      }
      Warning[index].Value[1] = value;
      break;

    default:
      Warning[index].Value[0] = value;
      break;
  }
} // end WarningsSet()

/***************************************************************************

FUNCTION: WarningsSetBitMap()

PURPOSE:  Set the appropriate bit the value array

COMMENTS:

***************************************************************************/
void WarningsSetBitMap(WARNING_ITEMS index, uint32_t eqIo)
{
  int element = eqIo / (sizeof(Warning[0].Value[0]) * 8);
  int bit = eqIo % (sizeof(Warning[0].Value[0]) * 8);
  Warning[index].Value[element] |= (1 << bit);
} // end WarningsSetBitMap()

/***************************************************************************

FUNCTION: WarningStatus()

PURPOSE:  Return the status

COMMENTS:

***************************************************************************/
char WarningStatus(WARNING_ITEMS index)
{
  return Warning[index].Status;
} // end WarningStatus()

/***************************************************************************

FUNCTION: WarningValue()

PURPOSE:  Return the value

COMMENTS:

***************************************************************************/
void WarningValue(WARNING_ITEMS index, uint32_t *value)
{
  int i;

  for (i = 0; i < WARNING_VALUE_LEN; ++i)
  {
    value[i] = Warning[index].Value[i];
  }
} // end WarningValue()

/***   End Of File   ***/
