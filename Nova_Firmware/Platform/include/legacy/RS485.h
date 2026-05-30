/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     RS485.h

AUTHOR:   KFarr, CBostic

COMPANY:  Infinetix

PURPOSE:

COMMENTS:

***************************************************************************/

#ifndef RS485_H
#define RS485_H

/*** include files ***/

/*** defines ***/

#define AT91_MCK                        48058000
#define AT91_RS485_BAUD_RATE            9600
#define AT91_DBGU_BAUD_RATE             AT91_RS485_BAUD_RATE  // RS485 and Debug use the same port

#define RS485_BUFFER_SIZE               32
#define RS485_INTERRUPT_LEVEL           7

#define RS485_MSG_NO_RESPONSE           0x01
#define RS485_MSG_DELAY_0ms             0x00
#define RS485_MSG_DELAY_1ms             0x08
#define RS485_MSG_DELAY_5ms             0x10
#define RS485_MSG_DELAY_10ms            0x20
#define RS485_MSG_DELAY_20ms            0x40
#define RS485_MSG_DELAY_25ms            0x80

#define RS485_MSG_IDLE                  3
#define RS485_MSG_REPLY                 1
#define RS485_MSG_SEND                  2

#define RS485_MSG_ERROR_LIMIT           3
#define RS485_MSG_ERR_ADDRESS           1
#define RS485_MSG_ERR_CHKSUM            2
#define RS485_MSG_ERR_CMD               3
#define RS485_MSG_ERR_COM               4
#define RS485_MSG_ERR_UNSOLICITED       5
#define RS485_MSG_ERR_XORVAL            6

#define RS485_MSG_ADDRESS_BYTE          1
#define RS485_MSG_BEGIN_DATA            5
#define RS485_MSG_CMD_BYTE              4
#define RS485_MSG_CTRL_BYTE             3
#define RS485_MSG_LEN_BYTE              2
#define RS485_MSG_OVERHEAD              7

#define RS485_MSG_ADDRESS_MASK          0x3F  // 0011 1111
#define RS485_MSG_TYPE_MASK             0xC0  // 1100 0000

#define RS485_QRY_FIRMWARE              1
#define RS485_QRY_SENSORS               2
#define RS485_QRY_VOLTAGE               3
#define RS485_SEND_ADC_OFFSET           4

#define RS485_START_BYTE                0x7E
#define RS485_TRANSPARENCY_BYTE         0x7D
#define RS485_TRANSPARENT_START_BYTE    0x5E
#define RS485_TRANSPARENT_TRANS_BYTE    0x5D
#define RS485_TRANSPARENT_XOR_VALUE     0x20

/*** typedefs and structures ***/

typedef struct {
  int             ReadIndex;
  int             WriteIndex;
  unsigned char   Buffer[RS485_BUFFER_SIZE];
} RS485_SERIAL_BUFFER;

typedef struct {
  unsigned char  RxBuffer[RS485_BUFFER_SIZE];
  int            RxBufLen;
  unsigned char  TxBuffer[RS485_BUFFER_SIZE];
  int            TxBufLen;
  int            State;
  int            Errors;
  int            Count;
} RS485_MESSAGE_BUFFER;

/*** external variables ***/

extern RS485_MESSAGE_BUFFER  RS485_Message;

/*** external functions ***/

extern void RS485_BuildPacket(unsigned char Address, unsigned char Command, unsigned char Delay, unsigned char *Data, unsigned char DataLength);
extern int RS485_CharsBuffered(void);
extern void RS485_Init(void);
extern void RS485_ProcessCmd(unsigned char *Packet, void *Data);
extern int RS485_ProcessBuffer(void);
extern void RS485_SendMessage(void);
extern void RS485_SetBaudRate(int TargetBaudRate);

extern void DBGU_Init(void);
extern void DBGU_SendChar(char Value);
extern void DBGU_SendPacket(char *Packet, int PacketSize, char newline);
extern void DBGU_SendString(char *String);
extern void DBGU_SendInt(int IntVal);

extern void SpuriousInt_Init(void);
extern void SpuriousInt_IRQ_Handler(void);

#endif

/***   End Of File   ***/
