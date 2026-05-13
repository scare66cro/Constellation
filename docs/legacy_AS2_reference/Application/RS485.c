/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     RS485.c

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:  RS485 Communications (and Debug port)

COMMENTS:

***************************************************************************/

/*** include files ***/

// FreeRTOS
#include "FreeRTOS.h"
#include "task.h"

// Platform
#include "system.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "Drivers/pinout.h"
#include "driverlib/uart.h"
#include "driverlib/interrupt.h"
#include "driverlib/rom_map.h"

#include "Analog_Input.h"
#include "DataExc.h"
#include "RS485.h"
#include "RTC.h"
#include "Settings.h"
#include "Timer.h"

#include "UI_Messages.h"

/*** typedefs and structures ***/

/*** module variables ***/

static RS485_SERIAL_BUFFER RS485Rx;
RS485_MESSAGE_BUFFER  RS485_Message;

/*** static functions ***/

static unsigned char RS485_CheckSum(unsigned char *Packet, int PktLen);
static void RS485_FlushBuffer(void);
static void RS485_ISR(void);
static int RS485_ProcessMessage(void);
static void RS485_QueueMessage(unsigned char *msg, int len);
static void RS485_ReadIndexInc(void);
static void RS485_SendChar(unsigned char Value);
static void RS485_SendPacket(unsigned char *Packet, int PacketSize);
static int RS485_TransparencyCheck(unsigned char *Packet, int PktLen);
static unsigned char RS485_XOR(unsigned char *Packet, int PktLen);

/***************************************************************************

FUNCTION:   RS485_BuildPacket()

PURPOSE:    Build and queue an RS485 packet

COMMENTS:   Queues the packet

***************************************************************************/
void RS485_BuildPacket(unsigned char  Address,
                       unsigned char  Command,
                       unsigned char  Delay,
                       unsigned char *Data,
                       unsigned char  DataLength)
{
  int            i;
  unsigned char  Packet[RS485_BUFFER_SIZE];
  int            PktIndex = 0;
  unsigned char *PktLen;

  memset(Packet, 0, sizeof(Packet));
  Packet[PktIndex++] = RS485_START_BYTE;
  Packet[PktIndex++] = Address;
  PktLen = &Packet[PktIndex++];
  Packet[PktIndex++] = Delay;
  Packet[PktIndex++] = Command;

  for (i = 0; i < DataLength; i++)
    Packet[PktIndex++] = Data[i];

  *PktLen = PktIndex + 2;
  Packet[PktIndex++] = RS485_CheckSum(Packet, *PktLen - 2);
  Packet[PktIndex++] = RS485_XOR(Packet, *PktLen - 1);
  PktIndex = RS485_TransparencyCheck(Packet, PktIndex);

  RS485_QueueMessage(Packet, PktIndex);
} // end RS485_BuildPacket()

/***************************************************************************

FUNCTION:   RS485_CharsBuffered()

PURPOSE:    Checks if there are characters waiting to be processed

COMMENTS:

***************************************************************************/
int RS485_CharsBuffered(void)
{
  if (RS485Rx.ReadIndex != RS485Rx.WriteIndex)
    return 1;
  else
    return 0;
} // end RS485_CharsBuffered()

/***************************************************************************

FUNCTION:   RS485_Checksum()

PURPOSE:    Calculate the packet checksum

COMMENTS:

***************************************************************************/
unsigned char RS485_CheckSum(unsigned char *Packet, int PktLen)
{
  unsigned char CheckSum = 0;
  int i;

  for (i = 0; i < PktLen; i++)
    CheckSum = CheckSum + Packet[i];
  return(CheckSum);
} // end RS485_Checksum()

/***************************************************************************

FUNCTION:   RS485_FlushBuffer()

PURPOSE:    Flush the buffer - clear the read & write indexes

COMMENTS:

***************************************************************************/
void RS485_FlushBuffer(void)
{
  RS485Rx.WriteIndex = 0;
  RS485Rx.ReadIndex = 0;
} // end RS485_FlushBuffer()

/***************************************************************************

FUNCTION:   RS485_Init()

PURPOSE:    Initialize the RS485 communications

COMMENTS:

***************************************************************************/
void RS485_Init(void)
{
  IntRegister(AUX_UART_INT, RS485_ISR);
  IntEnable(AUX_UART_INT);
  UARTTxIntModeSet(AUX_UART, UART_TXINT_MODE_EOT);
  UARTIntEnable(AUX_UART, 0xFFFFFFFF | UART_INT_RX | UART_INT_TX | UART_INT_RT);  // RT is the other way to Rx.
  IntPrioritySet(AUX_UART_INT, 0);

  // Initialize comm buffer
  RS485_FlushBuffer();

  // Set message state
  RS485_Message.State = RS485_MSG_IDLE;
} // end RS485_Init()

/***************************************************************************

FUNCTION:   RS485_ISR()

PURPOSE:    Process the RS485 communications

COMMENTS:   This function is called whenever a character is sent to the RS485
            port.  It moves that character to RS485.Buffer in a circular
            fashion.  This is a shared buffer for the rest of the RS485 functions.

            RS485_ProcessBuffer() is responsible for removing characters from
            this buffer.

***************************************************************************/
void RS485_ISR(void)
{
  unsigned long Status;
  unsigned long Error;

  Status = UARTIntStatus(AUX_UART, false);
  UARTIntClear(AUX_UART, Status);

  Error = UARTRxErrorGet(AUX_UART);
  UARTRxErrorClear(AUX_UART);

  if ((Status & UART_INT_TX) && (!UARTBusy(AUX_UART)))
  {
    // the TX interrupt is generated when the TX is complete
    // so set the TX direction output low and enable all the interrupts
    // except the TX interrupt
    set_output(AUX_DIR, 0);
    UARTIntEnable(AUX_UART, 0xFFFF);
    UARTIntDisable(AUX_UART, UART_INT_TX);
  }
  else if ((Status & (UART_INT_RT | UART_INT_RX)) && Error == 0)
  {
    while (UARTCharsAvail(AUX_UART))
    {
      RS485Rx.Buffer[RS485Rx.WriteIndex++] = UARTCharGetNonBlocking(AUX_UART); // put it in the buffer
      if (RS485Rx.WriteIndex >= RS485_BUFFER_SIZE) // increment the buffer with wrap around
      {
        RS485Rx.WriteIndex = 0;
      }
    }
  }
} // end RS485_ISR()

/***************************************************************************

FUNCTION:   RS485_ProcessBuffer()

PURPOSE:    If there is a complete message in the RS485Rx.Buffer move it to
            the message buffer

COMMENTS:   This function looks at the RS485Rx.Buffer to see if there is a
            complete message.  If so, it will move the characters from the
            RS485Rx.Buffer to the message buffer and process the message.

***************************************************************************/
int RS485_ProcessBuffer(void)
{
  int            BytesInQ = 0;
  int            i;
  int            LenIndex = 0;
  unsigned char *MsgBuf = RS485_Message.RxBuffer;
  int            MsgIndex = 0;
  int            MsgLen = 0;
  int            MsgStart = -1;
  int            ReturnValue = -1;

  // look for a message
  while (RS485Rx.ReadIndex != RS485Rx.WriteIndex)
  {
    if (RS485Rx.Buffer[RS485Rx.ReadIndex] == RS485_START_BYTE)
    {
      MsgStart = RS485Rx.ReadIndex;
      break;
    }
    else
      RS485_ReadIndexInc();   // throw away anything before the START_BYTE
  }

  if (MsgStart == -1)   // not found
    return(ReturnValue);

  // determine number of bytes in the queue
  if (RS485Rx.ReadIndex < RS485Rx.WriteIndex)
    BytesInQ = RS485Rx.WriteIndex - RS485Rx.ReadIndex;
  else
    BytesInQ = (RS485_BUFFER_SIZE - RS485Rx.ReadIndex) + RS485Rx.WriteIndex;

  // get the length of the message
  if (BytesInQ < 4)    // is the length byte + a possible transparency byte available
    return(ReturnValue);

  LenIndex = MsgStart + 2;
  if (LenIndex >= RS485_BUFFER_SIZE)
    LenIndex -= RS485_BUFFER_SIZE;

  if (RS485Rx.Buffer[LenIndex] == RS485_TRANSPARENCY_BYTE)
  {
    LenIndex++;   // discard the transparency byte
    if (LenIndex >= RS485_BUFFER_SIZE)
      LenIndex -= RS485_BUFFER_SIZE;
    MsgLen = RS485Rx.Buffer[LenIndex] ^ RS485_TRANSPARENT_XOR_VALUE;
  }
  else
    MsgLen = RS485Rx.Buffer[LenIndex];

  // simple validation of length
  if (MsgLen < RS485_MSG_OVERHEAD)  // message is corrupted
  {
    RS485_ReadIndexInc();   // increment the read index to move past the corrupted message
    return(ReturnValue);
  }

  // have enough bytes been received to possibly have the message
  // note: message can be longer than length value due to transparency bytes
  if (BytesInQ < MsgLen)
    return(ReturnValue);

  // the start of a message was found
  // if the message is complete, move it to the message buffer
  i = MsgStart;
  while ((MsgIndex < MsgLen) && (i != RS485Rx.WriteIndex))
  {
    if (RS485Rx.Buffer[i] == RS485_TRANSPARENCY_BYTE)
    {
      i++;    // discard the transparency byte
      if (i >= RS485_BUFFER_SIZE)
        i -= RS485_BUFFER_SIZE;

      // if the next byte is available, store the XOR of the next byte
      if (i != RS485Rx.WriteIndex)
        MsgBuf[MsgIndex++] = RS485Rx.Buffer[i] ^ RS485_TRANSPARENT_XOR_VALUE;
      else
        break;
    }
    else
    {
      MsgBuf[MsgIndex++] = RS485Rx.Buffer[i];
    }
    i++;
    if (i >= RS485_BUFFER_SIZE)
      i -= RS485_BUFFER_SIZE;
  }

  // if we have the entire message
  if (MsgIndex == MsgLen)
  {
    RS485Rx.ReadIndex = i;  // adjust the read index
    RS485_Message.RxBufLen = MsgLen;
    ReturnValue = RS485_ProcessMessage();
  }

  return(ReturnValue);
} // end RS485_ProcessBuffer()

/***************************************************************************

FUNCTION:   RS485_ProcessCmd()

PURPOSE:    Process the command

COMMENTS:

***************************************************************************/
void RS485_ProcessCmd(unsigned char *Packet, void *Data)
{
  unsigned char Cmd = 0;
  int           i = 0;
  int           PktLen = 0;
  unsigned char RawData[RS485_BUFFER_SIZE];
  int           RdIndex = 0;
  int          *SensorData = (int *)Data;
  int           SnsrIndex = 0;
  char         *Version = (char *)Data;

  // pull apart the packet
  PktLen = Packet[RS485_MSG_LEN_BYTE];
  Cmd = Packet[RS485_MSG_CMD_BYTE];
  i = RS485_MSG_BEGIN_DATA;
  while (RdIndex < PktLen - RS485_MSG_OVERHEAD)
    RawData[RdIndex++] = Packet[i++];

  // process the command
  switch (Cmd)
  {
    case RS485_QRY_FIRMWARE:
      sprintf(Version, "%d.%d", RawData[0], RawData[1]);
      break;

    case RS485_QRY_SENSORS:
      SensorData[SnsrIndex++] = RawData[0]; // sensor types

      for (i = 1; i < RdIndex; i += 2)
      {
        SensorData[SnsrIndex++] = RawData[i]<<8 | RawData[i + 1];
      }
      break;

    default:
      break;
  }
} // end RS485_ProcessCmd()

/***************************************************************************

FUNCTION:   RS485_ProcessMessage()

PURPOSE:    Process the incoming message

COMMENTS:

***************************************************************************/
int RS485_ProcessMessage(void)
{
  int ReturnValue = 0;

  if (RS485_Message.State == RS485_MSG_IDLE)
  {
    // error state - not expecting a message
    // OffenderAddress = RS485_Message.RxBuffer[RS485_MSG_ADDRESS_BYTE];
    ReturnValue = RS485_MSG_ERR_UNSOLICITED;
  }
  else if (RS485_Message.State == RS485_MSG_REPLY)
  {
    if (RS485_CheckSum(RS485_Message.RxBuffer, RS485_Message.RxBufLen - 2) != RS485_Message.RxBuffer[RS485_Message.RxBufLen - 2])
    {
      // bad checksum - resend request
      RS485_Message.State = RS485_MSG_SEND;
      RS485_Message.Errors++;
      ReturnValue = RS485_MSG_ERR_CHKSUM;
    }
    else if (RS485_XOR(RS485_Message.RxBuffer, RS485_Message.RxBufLen - 1) != RS485_Message.RxBuffer[RS485_Message.RxBufLen - 1])
    {
      // bad XOR value - resend request
      RS485_Message.State = RS485_MSG_SEND;
      RS485_Message.Errors++;
      ReturnValue = RS485_MSG_ERR_XORVAL;
    }
    else if ((RS485_Message.RxBuffer[RS485_MSG_ADDRESS_BYTE] & RS485_MSG_ADDRESS_MASK) != RS485_Message.TxBuffer[RS485_MSG_ADDRESS_BYTE])
    {
      // reply from the wrong board - resend request
      RS485_Message.State = RS485_MSG_SEND;
      RS485_Message.Errors++;
      ReturnValue = RS485_MSG_ERR_ADDRESS;
    }
    else if (RS485_Message.RxBuffer[RS485_MSG_CMD_BYTE] != RS485_Message.TxBuffer[RS485_MSG_CMD_BYTE])
    {
      // reply with the wrong command - resend request
      RS485_Message.State = RS485_MSG_SEND;
      RS485_Message.Errors++;
      ReturnValue = RS485_MSG_ERR_CMD;
    }
    else
    {
      RS485_Message.State = RS485_MSG_IDLE;
      RS485_Message.Errors = 0;
      ReturnValue = 0;
    }
  }

  return(ReturnValue);
} // end RS485_ProcessMessage()

/***************************************************************************

FUNCTION:   RS485_QueueMessage()

PURPOSE:    Add a message to the message queue

COMMENTS:   The message queue only holds one message

***************************************************************************/
void RS485_QueueMessage(unsigned char *msg, int len)
{
  memcpy(RS485_Message.TxBuffer, msg, len);
  RS485_Message.TxBufLen = len;
  RS485_Message.State = RS485_MSG_SEND;
} // RS485_QueueMessage()

/***************************************************************************

FUNCTION:   RS485_ReadIndexInc()

PURPOSE:    Increment the read index with wrap around

COMMENTS:

***************************************************************************/
void RS485_ReadIndexInc(void)
{
  RS485Rx.ReadIndex++;
  if (RS485Rx.ReadIndex >= RS485_BUFFER_SIZE)
    RS485Rx.ReadIndex = 0;
} // RS485_ReadIndexInc()

/***************************************************************************

FUNCTION:   RS485_SendChar()

PURPOSE:    Sends a character to the RS485 port

COMMENTS:

***************************************************************************/
void RS485_SendChar(unsigned char Value)
{
  UARTCharPut(AUX_UART, Value);
} // end RS485_SendChar()

/***************************************************************************

FUNCTION:   RS485_SendPacket()

PURPOSE:    Sends a packet to the RS485 port

COMMENTS:   It sets PIO PA20 to high, sends the packet, and then clears PA20

***************************************************************************/
void RS485_SendPacket(unsigned char *Packet, int PacketSize)
{
  int i;
  // TODO - clb: this function should indicate whether or not the packet was sent so the
  // message state is only changed appropriately upon return

  if (!UARTBusy(AUX_UART))
  {
    // disable all interrupts
    UARTIntDisable(AUX_UART, 0xFFFF);

    // the TX direction output high
    set_output(AUX_DIR, 1);

    for (i = 0; i < PacketSize; i++)
      RS485_SendChar(Packet[i]);

    // enable the TX interrupt so that when the TX is complete
    // an interrupt is generated so that the TX direction output
    // can be set low and the interrupts reset
    UARTIntEnable(AUX_UART, UART_INT_TX);
  }
} // end RS485_SendPacket()

/***************************************************************************

FUNCTION:   RS485_SendMessage()

PURPOSE:    Send message to analog boards

COMMENTS:

***************************************************************************/
void RS485_SendMessage(void)
{
  if (RS485_Message.State == RS485_MSG_SEND)
  {
    RS485_SendPacket(RS485_Message.TxBuffer, RS485_Message.TxBufLen);
//    UARTwrite(RS485_Message.TxBuffer, RS485_Message.TxBufLen);

    RS485_Message.Count++;
    RS485_Message.State = RS485_MSG_REPLY;
  }
} // end RS485_SendMessage()

/***************************************************************************

FUNCTION:   RS485_TransparencyCheck()

PURPOSE:    Add any necessary transparency bytes to the packet

COMMENTS:   See Infinetix RS-485 Communication Protocol documentation for
            transparency byte rules

***************************************************************************/
int RS485_TransparencyCheck(unsigned char *Packet, int PktLen)
{
  int i;
  int PktIndex = 1;
  char TempBuf[RS485_BUFFER_SIZE];

  // safety check
  if (PktLen > RS485_BUFFER_SIZE)
    return(0);

  // copy the packet into temporary buffer
  for (i = 0; i < PktLen; i++)
    TempBuf[i] = Packet[i];

  // add any necessary transparency bytes
  // NOTE: the loop starts at 1 - after the packet start byte
  for (i = 1; i < PktLen; i++)
  {
    if (TempBuf[i] == RS485_START_BYTE)
    {
      Packet[PktIndex++] = RS485_TRANSPARENCY_BYTE;
      Packet[PktIndex++] = RS485_TRANSPARENT_START_BYTE;
    }
    else if (TempBuf[i] == RS485_TRANSPARENCY_BYTE)
    {
      Packet[PktIndex++] = RS485_TRANSPARENCY_BYTE;
      Packet[PktIndex++] = RS485_TRANSPARENT_TRANS_BYTE;
    }
    else
      Packet[PktIndex++] = TempBuf[i];
  }
  return(PktIndex);
} // end RS485_TransparencyCheck()

/***************************************************************************

FUNCTION:   RS485_XOR()

PURPOSE:    Determine the XOR value for the packet

COMMENTS:

***************************************************************************/
unsigned char RS485_XOR(unsigned char *Packet, int PktLen)
{
  unsigned char XOR_Value = 0xFF;
  int i;

  for (i = 0; i < PktLen; i++)
    XOR_Value = XOR_Value ^ Packet[i];

  return(XOR_Value);
} // end RS485_XOR()

/***   End Of File   ***/
