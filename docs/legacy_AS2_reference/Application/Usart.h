/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     Usart.h

AUTHOR:   KFarr, CBostic

COMPANY:  Infinetix

PURPOSE:

COMMENTS:

***************************************************************************/

#ifndef USART_H
#define USART_H

/*** include files ***/

#include "DataExc.h"

/*** defines ***/

#define USART_BUFFER_SIZE       MSG_RX_BUFFER_SIZE

/*** typedefs and structures ***/

typedef struct {
  int   ReadIndex;
  int   WriteIndex;
  char  Buffer[MSG_RX_BUFFER_SIZE];
} USART_SERIAL_BUFFER;

/*** external variables ***/

/*** external functions ***/

extern int  Usart_CharsBuffered(void);
extern void Usart_FlushBuffer(void);
extern void Usart_Init(void);
extern void Usart_ISR(void);
extern int Usart_ProcessBuffer(char *MsgBuf);
extern void Usart_ReadIndexInc(void);
extern void Usart_SendPacket(char *Packet, int PacketSize );
extern void Usart_SendChar(char Value);

extern int Usart_GetLine(unsigned char *buf, int length);

#endif

/***   End Of File   ***/
