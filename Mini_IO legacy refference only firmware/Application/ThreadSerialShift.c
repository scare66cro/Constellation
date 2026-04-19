/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     ThreadSerialShift.c

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:  Thread for serial shift registers to read the DIs and panel
          switches and control DOs

COMMENTS:

***************************************************************************/

/*** include files ***/

// FreeRTOS
#include "FreeRTOS.h"
#include "task.h"

// Platform
#include "system.h"
#include "pinout.h"

// Gellert
#include "DataExc.h"
#include "SerialShift.h"
#include "States.h"
#include "ThreadMonitor.h"
#include "ThreadSerialShift.h"
#include "Timer.h"
#include "Usart.h"
#include "Warnings.h"

/*** defines ***/

/*** typedefs and structures ***/

/*** module variables ***/

THREAD_SERIAL_SHIFT_INFO SerialShift;

/*** static functions ***/

static void SetOutput(_pin_str CS, unsigned int outputs, unsigned int shiftOutput);

/***************************************************************************

FUNCTION:   ThreadSerialShift()

PURPOSE:    Read the switches and digital inputs and set the digital outputs

COMMENTS:

***************************************************************************/
void ThreadSerialShift(void)
{
  int i,j;
  uint32_t inputState1;
  uint32_t inputState2;
  static uint32_t pendingInputState[BOARD_COUNT];

  SerialShift.BlockTimeout = 5 / portTICK_RATE_MS;
  vSemaphoreCreateBinary(SerialShift.Semaphore);

  while(1)
  {
    if (SerialShift.Semaphore != NULL)
    {
      // Obtain the semaphore - block if the semaphore is not immediately available.
      if (xSemaphoreTake(SerialShift.Semaphore, SerialShift.BlockTimeout) == pdTRUE)
      {
        for (i = 0; i < BOARD_COUNT; i++)
        {
          if (IoBoard[i].Version != 0x0F)
          {
            inputState1 = ReadInput(IoBoard[i].ChipSelect, IoBoard[i].InputShiftLength);
            inputState2 = ReadInput(IoBoard[i].ChipSelect, IoBoard[i].InputShiftLength);

            if (inputState1 == inputState2)
            {
              if (pendingInputState[i] == inputState1)
              {
                if (IoBoard[i].InputState != pendingInputState[i])
                {
                  IoBoard[i].InputState = pendingInputState[i];
                  debug_printf("ThreadSerialShift: state changed: board %d - 0x%08X\r\n",i,IoBoard[i].InputState);
                }
              }
              else
              {
                pendingInputState[i] = inputState1;
                debug_printf("ThreadSerialShift: state change pending: board %d - 0x%08X\r\n",i,inputState1);
              }
            }
            else
            {
              debug_printf("ThreadSerialShift: back-to-back reads not equal: board %d - 0x%08X\r\n",i,inputState1);
              debug_printf("ThreadSerialShift: back-to-back reads not equal: board %d - 0x%08X\r\n",i,inputState2);
            }

            SetOutput(IoBoard[i].ChipSelect, IoBoard[i].OutputShiftLength, IoBoard[i].OutputState);

            for (j = 0; j < SS_CPLD_VERSION_LENGTH; j++)
            {
              if (IoBoard[i].InputState & (1 << (3-j)))
              {
                IoBoard[i].Version |= 1 << j;
              }
            }
          }
          else
          {
            if (ClearBoardIo((SYSTEM_BOARDS) i) > 0)
            {
              WarningsSet(WARN_EXPANSIONBOARD, FM_ALARM, NA, i);
            }
          }
        }

        // Release the semaphore.
        xSemaphoreGive(SerialShift.Semaphore);
      }
      else
      {
        debug_printf("ThreadSerialShift blocked\r\n");
      }

      ThreadMonitorUpdate(TM_SERIAL_SHIFT);
      vTaskDelay(250/portTICK_RATE_MS);
    }
  }
} // end ThreadSerialShift()

/***************************************************************************

FUNCTION:   ReadInput()

PURPOSE:    Read serial shift input

COMMENTS:

***************************************************************************/
unsigned int ReadInput(_pin_str CS, unsigned int inputs)
{
  unsigned int shiftInput = 0;
  int i;

  set_output(CS, 0);

  for (i = 0; i < inputs; i++)
  {
    shiftInput |= (read_input(SS_MISO))<<i;
    set_output(SS_SCLK, 1);
    set_output(SS_SCLK, 0);
  }

  set_output(CS, 1);

  return shiftInput;
} // end ReadInput()

/***************************************************************************

FUNCTION:   SetOutput()

PURPOSE:    Set serial shift output

COMMENTS:

***************************************************************************/
void SetOutput(_pin_str CS, unsigned int outputs, unsigned int shiftOutput)
{
  int i;

  set_output(CS, 0);

  for (i = 0; i < outputs; i++)
  {
    if (BIT_SET(i, shiftOutput))
      set_output(SS_MOSI, 1);
    else
      set_output(SS_MOSI, 0);

    set_output(SS_SCLK, 1);
    set_output(SS_SCLK, 0);
  }

  set_output(SS_RCLK, 1);
  set_output(SS_RCLK, 0);

  set_output(CS, 1);
} // end SetOutput()

/***   End Of File   ***/
