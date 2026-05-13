/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     Usart.c

AUTHOR:   KFarr, CBostic

COMPANY:  Infinetix

PURPOSE:  Serial interface support

COMMENTS:

***************************************************************************/

#ifndef USART_C
#define USART_C
#endif

/*** include files ***/

#include <string.h>

// Platform
#include "system.h"
#include "debug.h"
#include "pinout.h"
#include "driverlib/uart.h"
#include "driverlib/interrupt.h"
#include "driverlib/rom_map.h"
#include "utils/uartstdio.h"

// FreeRTOS Includes
#include "FreeRTOS.h"
#include "task.h"

// Gellert
#include "DataExc.h"
#include "Usart.h"

/*** defines ***/

#define TX_ENABLE()	set_output(LTX_TX, 1)
#define RX_ENABLE()	set_output(LTX_TX, 0)

/*** typedefs and structures ***/

/*** module variables ***/

static USART_SERIAL_BUFFER UsartRx;

/*** static functions ***/

/***************************************************************************

FUNCTION:   Usart_CharsBuffered()

PURPOSE:    Return a bool indicating if there are any
            characters that need processed

COMMENTS:

***************************************************************************/
int Usart_CharsBuffered()
{
  if (UsartRx.ReadIndex != UsartRx.WriteIndex)
  {
    return 1;
  }
  else
  {
    return 0;
  }
} // end Usart_CharsBuffered()

/***************************************************************************

FUNCTION:   Usart_FlushBuffer()

PURPOSE:    Flushes the Usart Buffer

COMMENTS:

***************************************************************************/

void Usart_FlushBuffer(void)
{
  UsartRx.ReadIndex = 0;
  UsartRx.WriteIndex = 0;
} // end Usart_FlushBuffer()

/***************************************************************************

FUNCTION:   Usart_Init()

PURPOSE:    Initialize the Usart, COM0

COMMENTS:

***************************************************************************/
void Usart_Init(void)
{
	IntEnable(LTX_UART_INT);
	UARTIntEnable(LTX_UART, UART_INT_RX | UART_INT_RT);
} // end Usart_Init()

/***************************************************************************

FUNCTION:   Usart_ISR()

PURPOSE:    Process the serial port

COMMENTS:   This function is called whenever a character
            is sent to the serial port.  It moves that
            character to UsartRx.Buffer in a circular
            fashion.  This is a shared buffer
            for the rest of the Usart functions.

            Usart_ProcessBuffer() is responsible for
            removing characters from this buffer.

***************************************************************************/
void Usart_ISR(void)
{
  unsigned long Status;

  Status = UARTIntStatus(LTX_UART, true); // dummy read to properly clear errors

  if ((Status & (UART_INT_RT | UART_INT_RX)))
  {
    while (UARTCharsAvail(LTX_UART))
    {
      UsartRx.Buffer[UsartRx.WriteIndex++] = UARTCharGetNonBlocking(LTX_UART);
      if (UsartRx.WriteIndex >= USART_BUFFER_SIZE) // increment the buffer with wrap around
      {
        UsartRx.WriteIndex = 0;
      }
    }
  }

  UARTRxErrorClear(LTX_UART);

  // Clear the asserted interrupts.
  UARTIntClear(LTX_UART, Status);
}

/***************************************************************************

FUNCTION:   Usart_ProcessBuffer()

PURPOSE:    If there is a complete message in the UsartRx.Buffer move it to
            the message buffer

COMMENTS:   This function looks at the UsartRx.Buffer to see if there
            is a complete message.  If so, it will move the characters from
            UsartRx.Buffer to the message buffer and process the message.

***************************************************************************/
int Usart_ProcessBuffer(char *MsgBuf)
{
  int   i;
  int   MsgEnd = -1;
  int   MsgIndex = 0;
  int   MsgLen = 0;
  int   MsgStart = -1;

  // look for a complete post message with start & end delimiters
  i = UsartRx.ReadIndex;
  while (i != UsartRx.WriteIndex)
  {
    if (UsartRx.Buffer[i] == MSG_START_DELIMITER)
    {
      MsgStart = i;
    }
    else
    {
      if (MsgStart == -1)
      {
        Usart_ReadIndexInc();   // throw away anything before the start delimiter
      }
    }

    if (UsartRx.Buffer[i] == MSG_END_DELIMITER)
    {
      MsgEnd = i;
      break;
    }

    i++;
    if (i >= USART_BUFFER_SIZE)
    {
      i = 0;
    }
  }

  // not found
  if ((MsgStart == -1)||(MsgEnd == -1))
  {
    return 0;
  }

  if (MsgStart < MsgEnd)
  {
    MsgLen = MsgEnd - MsgStart;
  }
  else
  {
    MsgLen = (USART_BUFFER_SIZE - MsgStart) + MsgEnd;
  }

  // found - move it to message buffer
  while (MsgIndex <= MsgLen)
  {
    MsgBuf[MsgIndex++] = UsartRx.Buffer[UsartRx.ReadIndex];
    Usart_ReadIndexInc();
  }

  MsgBuf[MsgIndex] = 0;

  return 1;
} // end Usart_ProcessBuffer()

/***************************************************************************

FUNCTION:   Usart_GetLastChar()

PURPOSE:    Returns last buffered char received from COM0

COMMENTS:

***************************************************************************/
int Usart_GetLastChar(char *ret)
{
  unsigned int delay = uptime_ms;

  while (   UsartRx.WriteIndex == UsartRx.ReadIndex
         && uptime_ms - delay < 5000U)
  {
    vTaskDelay(10 / portTICK_RATE_MS);
  }

  if (UsartRx.WriteIndex == UsartRx.ReadIndex)
  {
    return -1;
  }
  else
  {
    *ret = UsartRx.Buffer[UsartRx.ReadIndex++];

    if (UsartRx.ReadIndex >= USART_BUFFER_SIZE)
    {
      UsartRx.ReadIndex = 0;
    }

    return 0;
  }
} // End of Usart_GetLastChar()

/***************************************************************************

FUNCTION:   Usart_GetLine()

PURPOSE:

COMMENTS:

***************************************************************************/
int Usart_GetLine(unsigned char *buf, int length)
{
//  int BytesRemoved = 0;
  char ch;
  int Done = 0;
  int ByteCounter = 0;

  while ((ByteCounter < length) && (Done == 0))
  {
    if (Usart_GetLastChar(&ch) == 0)
    {
//    BytesRemoved++;

      if (ch == 0x0D)  // carriage return
      {
        Usart_GetLastChar(&ch);
        ch = '\0';
        Done = 1;
      }
      else if (ch == 0x0A) // line feed
      {
        ch = '\0';
        Done = 1;
      }

      buf[ByteCounter++] = ch;
    }
    else
    {
      ByteCounter = -1;
      Done = 1;
    }
  }

  return ByteCounter;
} // end Usart_GetLine()

/***************************************************************************

FUNCTION:   Usart_ReadIndexInc()

PURPOSE:    Increment the read index with wrap around

COMMENTS:

***************************************************************************/
void Usart_ReadIndexInc(void)
{
  UsartRx.ReadIndex++;
  if (UsartRx.ReadIndex >= USART_BUFFER_SIZE)
  {
    UsartRx.ReadIndex = 0;
  }
} // Usart_ReadIndexInc()

/***************************************************************************

FUNCTION:   Usart_SendPacket()

PURPOSE:    Sends a packet to the serial port

COMMENTS:

***************************************************************************/
void Usart_SendPacket(char *Packet, int PacketSize)
{
	int i;

	for (i = 0; i < PacketSize; i++)
	{
		MAP_UARTCharPut(LTX_UART, Packet[i]);
	}
} // end Usart_SendPacket()

/***   End of File   ***/
