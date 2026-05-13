/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     ThreadFileReceive.c

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:  Thread to receive files from the web server

COMMENTS:

***************************************************************************/

/*** include files ***/

#include <stdlib.h>
#include <string.h>

// FreeRTOS
#include "FreeRTOS.h"
#include "task.h"

// Platform
#include <stdint.h>
#include <stdbool.h>

#include "inc/hw_types.h"
#include "inc/hw_nvic.h"
#include "driverlib/sysctl.h"
#include "system.h"
#include "flash.h"

// Gellert
#include "StorePostData.h"
#include "ThreadFileReceive.h"
#include "UI_Messages.h"
#include "Usart.h"

/*** defines ***/

/*** typedefs and structures ***/

/*** module variables ***/

FILE_RECEIVE_INFO FileReceive;

 /*** static functions ***/

/******************************************************************************

FUNCTION: htoi

PURPOSE:  Takes a character from '0' - '9' 'a' - 'f' or
            'A' - 'F' and converts it to the correct integer
            value.

INPUTS:

OUTPUTS:

NOTES:
    None

VERSION HISTORY:
    Version: 1.00  Date: 01/15/2007  By: Ken Farr

******************************************************************************/
int htoi(char hexc)
{
  if ((hexc >= 'a') && (hexc <= 'z'))
  {
    hexc = 'A' + (hexc - 'a');
  }

  if ((hexc >= '0') && (hexc <= '9'))
  {
    return ((int) (hexc - '0'));
  }
  else
  {
    if ((hexc >= 'A') && (hexc <= 'F'))
    {
      return ((int) (hexc - 'A' + 10));
    }
    else
    {
      return 0;
    }
  }
} // end htoi()

/******************************************************************************

FUNCTION: SrecRead

PURPOSE:  Reads in from the serial port using the Usart
          function "Usart_GetLastChar()" a Motorola S28
          hex file.  The very first address it gets is
          saved in the global variable "BaseAddress" for
          later retrieval.

          This function only extracts one record at a time
          and will return "END_OF_FILE" when the last record
          is processed.  Other return variables include

          BAD_CS      -- Bad CheckSum on last record
          RECORD_OK   -- Retreived record and placed in buffer
          BUFFER_FULL -- The number of unsigned chars availiable is
                         greater than the number of unsigned chars requested
          BAD_FILE    -- File doesn't match what's expected


INPUTS:

OUTPUTS:

NOTES:
    None

VERSION HISTORY:
    Version: 1.00  Date: 01/15/2007  By: Ken Farr

******************************************************************************/
int SrecRead(unsigned char *Stream, char *RecordType, unsigned char *Buffer, int *BytesWritten, int BytesRequested)
{
  int AvailableBytes = 0;
  unsigned char BytesInPacket = 0;
  unsigned char *ch;
  unsigned char CalcCheckSum = 0;
  unsigned char FileCheckSum = 0;
  int i;
  unsigned int Address = 0;
  char A;

  *BytesWritten = 0;

  // We need a minimum of 10 bytes from the stream for the smallest of Srec's
  // so we check that first
  AvailableBytes = strlen((char*) Stream);

  if (AvailableBytes < 10)
  {
    return LESS_BYTES;
  }

  // We have enought bytes for the minimum packet, lets try to process
  ch = Stream;

  if (*ch != 'S')
  {
    // The first character is wrong, return error
    return BAD_FILE;
  }

  ch++; // increment to second character
  AvailableBytes--;

  // now calculate record type
  *RecordType = htoi(*ch);
  ch++; // increment to third character
  AvailableBytes--;

  BytesInPacket = htoi(*ch) << 4;
  ch++; // increment to forth character
  BytesInPacket |= htoi(*ch);
  ch++; // increment to fifth character

  AvailableBytes -= 2;
  CalcCheckSum += BytesInPacket;

  if (((int)BytesInPacket - 4) > BytesRequested)
  {
    return BUFFER_FULL;
  }

  switch (*RecordType)
  {
    case 0:
    {
      // We already have the S0 and the number of bytes removed from the
      // stream, we now have to remove the address portion, this should
      // be 4 ASCII 0's representing 2 bytes, still 0 though
      // Since these values are all 0's no need up update the CheckSum
      for (i = 0; i < 4; i++)
      {
        if (htoi(*ch) != 0)
        {
          return BAD_FILE;
        }
        ch++;
      }
      BytesInPacket -= 2;

      // Now lets start calculating the CheckSum for the remaining bytes
      for (i = 0; i < (BytesInPacket - 1); i++)
      {
        A = htoi(*ch) << 4;
        ch++;
        A |= htoi(*ch);
        ch++;
        CalcCheckSum += A;

        // Move the informational string into the buffer
        Buffer[i] = A;
      }
      Buffer[i] = 0;  // terminate the string
      *BytesWritten = i;

      // Take the one's compliment of the sum to get the checksum
      CalcCheckSum = ~CalcCheckSum;

      // Now get the file checksum for the record
      A = htoi(*ch) << 4;
      ch++;
      A |= htoi(*ch);
      ch++;
      FileCheckSum = A;

      // Now check the CheckSum
      if (CalcCheckSum != FileCheckSum)
      {
        return BAD_CS;
      }
      else
      {
        return RECORD_OK;
      }
    }
    case 1:
    {/*
     // S1 type Srec, this is the main data record with a 2 bytes address
     // First extract the Address
     *((unsigned short*)(&Address)) = htoi( *ch ) << 12;
     ch++;
     *((unsigned short*)(&Address))|= htoi( *ch ) <<  8;
     ch++;
     *((unsigned short*)(&Address))|= htoi( *ch ) <<  4;
     ch++;
     *((unsigned short*)(&Address))|= htoi( *ch );
     ch++;

     CalcCheckSum += ( ( *((unsigned short*)(&Address)) & 0xFF00 ) >> 8 );
     CalcCheckSum += ( ( *((unsigned short*)(&Address)) & 0x00FF )      );

     BytesInPacket-=2;

     // Now compute and move each byte pair, except the end CheckSum
     for( i = 0; i < ( BytesInPacket - 1 ); i++ )
     {
     Buffer[ i ]  = htoi( *ch ) << 4;
     ch++;
     Buffer[ i ] |= htoi( *ch );
     ch++;
     CalcCheckSum += Buffer[ i ];
     }

     // Now add the last byte, the File CheckSum
     FileCheckSum = htoi( *ch );
     ch++;
     FileCheckSum = htoi( *ch );
     ch++;

     CalcCheckSum += FileCheckSum;

     if( CalcCheckSum != 0xFF )
     {
     return BAD_CS;
     }
     else
     {
     *BytesWritten = BytesInPacket - 1;
     return RECORD_OK;
     }*/

      return UNHANDLED_TYPE;
    }
    case 2:
    {
      // S2 type Srec, this is the main data record with a 3 bytes address
      // First extract the Address
      *(((unsigned short*) (&Address)) + 1) = htoi(*ch) << 4;
      ch++;
      *(((unsigned short*) (&Address)) + 1) |= htoi(*ch);
      ch++;
      *((unsigned short*) (&Address)) = htoi(*ch) << 12;
      ch++;
      *((unsigned short*) (&Address)) |= htoi(*ch) << 8;
      ch++;
      *((unsigned short*) (&Address)) |= htoi(*ch) << 4;
      ch++;
      *((unsigned short*) (&Address)) |= htoi(*ch);
      ch++;

      CalcCheckSum += ((*(((unsigned short*) (&Address)) + 1) & 0x00FF));
      CalcCheckSum += ((*(((unsigned short*) (&Address))) & 0xFF00) >> 8);
      CalcCheckSum += ((*(((unsigned short*) (&Address))) & 0x00FF));

      BytesInPacket -= 3;

      // Now compute and move each byte pair, except the end CheckSum
      for (i = 0; i < (BytesInPacket - 1); i++)
      {
        A = htoi(*ch) << 4;
        ch++;
        A |= htoi(*ch);
        ch++;
        CalcCheckSum += A;
        Buffer[i] = A;
      }

      // Now get the file checksum for the record
      FileCheckSum = htoi(*ch) << 4;
      ch++;
      FileCheckSum |= htoi(*ch);
      ch++;

      // Take the one's compliment of the sum to get the checksum
      CalcCheckSum = ~CalcCheckSum;

      if (CalcCheckSum != FileCheckSum)
      {
        return BAD_CS;
      }
      else
      {
        *BytesWritten = BytesInPacket - 1;
        return RECORD_OK;
      }
    }
    case 5:
    {
      // S5 is the end record that has the count of previous records
      // the count is stored as a string (hex) so just move it into the buffer
      // and temrinate the string
      for (i = 0; i < (BytesInPacket - 1); i++)
      {
        A = htoi(*ch) << 4;
        *Buffer = *ch;
        ch++;
        Buffer++;

        A |= htoi(*ch);
        *Buffer = *ch;
        ch++;
        Buffer++;

        CalcCheckSum += A;
      }
      *Buffer = 0;  // terminate the string

      FileCheckSum += htoi(*ch) << 4;
      ch++;
      FileCheckSum += htoi(*ch);
      ch++;

      // Take the one's compliment of the sum to get the checksum
      CalcCheckSum = ~CalcCheckSum;

      if (CalcCheckSum != FileCheckSum)
      {
        return BAD_CS;
      }
      else
      {
        *BytesWritten = BytesInPacket - 1;
        return RECORD_OK;
      }
    }
    case 8:
    {
      return END_OF_FILE;
    }
    default:
    {
      return UNHANDLED_TYPE;
    }
  }
} // end SrecRead()

/***************************************************************************

FUNCTION:   ReceiveFirmwareFile()

PURPOSE:

COMMENTS:

***************************************************************************/
int ReceiveFirmwareFile(void)
{
  char RecordType = 255;
  int RecordLength = 0;
  int RecordCount = 0;
  int FileRecordCount = 0;
  int BytesWritten = 0;
  int FlashBytes = 0;
  unsigned char FlashBuffer[SREC_LEN];
  unsigned char Srec[SREC_LEN];
  int SrecReturn = RECORD_OK;

  while (   SrecReturn == RECORD_OK
         && FlashBytes < UPGRADE_FILE_SIZE)
  {
    memset(Srec, 0xFF, sizeof(Srec));

    RecordLength = Usart_GetLine(Srec, SREC_LEN);
    if (RecordLength != -1)
    {
      SrecReturn = SrecRead(Srec, &RecordType, FlashBuffer, &BytesWritten, SREC_LEN);
      if (SrecReturn == RECORD_OK)
      {
        if (RecordCount == 0)
        {
          if (   RecordType != 0    // header record
              || strcmp((const char *) FlashBuffer, UPGRADE_FILE_TAG) != 0)
          {
            SrecReturn = BAD_FILE;
          }
        }
        else if (RecordType == 5)   // record count
        {
          FileRecordCount = strtol((const char *) FlashBuffer, NULL, 16);
          if (FileRecordCount != RecordCount - 1)
          {
            SrecReturn = BAD_FILE;
          }
        }

        // write to external flash
        FlashWrite(FLASH_UPGRADE_IMAGE + FlashBytes, (unsigned char*)Srec, RecordLength);
        FlashBytes += RecordLength;

        Usart_SendPacket(".", strlen("."));
        RecordCount++;
        if ((RecordCount % 1000) == 0)
          debug_printf("Total image bytes written to flash:%d\r\n", FlashBytes);
      }
      else if (SrecReturn == END_OF_FILE)
      {
        if (   RecordCount > 0
            && RecordCount - 2 != FileRecordCount)
        {
          SrecReturn = BAD_FILE;
        }
        Usart_SendPacket("*", strlen("."));
      }
      else if (SrecReturn == BAD_CS)
      {
        Usart_SendPacket("#", strlen("."));
        SrecReturn = RECORD_OK;
      }
    }
    else
    {
      SrecReturn = TIMEOUT;
    }
  }

  return SrecReturn;
} //end ReceiveFirmwareFile()

/***************************************************************************

FUNCTION:   Restart()

PURPOSE:    Restart at the appropriate location

COMMENTS:

***************************************************************************/
void Restart(RESTART_ADDRESS address)
{
//  if (*((unsigned int*)0x1C000)==0xFFFFFFFF) return;

  HWREG(NVIC_VTABLE) = address;


  // Load the stack pointer from the application's vector table.
  __asm("    ldr     r1, [r0]\n"
      "    mov     sp, r1\n");

  // Load the initial PC from the application's vector table and branch to the application's entry point.
  __asm("    ldr     r0, [r0, #4]\n"
      "    bx      r0\n");
} // end Restart()

/***************************************************************************

FUNCTION:   ThreadFileReceive()

PURPOSE:    Thread to receive files from the http server

COMMENTS:

***************************************************************************/
void ThreadFileReceive(void)
{
  int i = 1;
  int j = 1;
  int status;
  char command[SREC_LEN];

  while (1)
  {
    vTaskDelay(100 / portTICK_RATE_MS);

    if (FileReceive.Status == FR_RECEIVING)
    {
      if (FileReceive.Type == FRT_FIRMWARE)
      {
        debug_printf("ThreadFileReceive: Firmware...\r\n");

        vTaskDelay(2000 / portTICK_RATE_MS);
        debug_printf("ThreadFileReceive Release\r\n");

        // reply
        Usart_SendPacket(BOOTLOADER_START, strlen(BOOTLOADER_START));

        i = 1;
        while (i == 1)
        {
          vTaskDelay(10 / portTICK_RATE_MS);

          if (Usart_ProcessBuffer(command) == 1)
          {
            if (strcmp(command, BOOTLOADER_ARM_MSG) == 0)
            {
              debug_printf("Erasing External Flash for Upgrade Image...");
              FlashEraseArea(FLASH_UPGRADE_IMAGE, UPGRADE_FILE_SIZE);
              debug_printf("Done\r\n");

              // reply
              Usart_SendPacket(BOOTLOADER_ARM_MSG, strlen(BOOTLOADER_ARM_MSG));

              status = ReceiveFirmwareFile();
              if (status == END_OF_FILE)
              {
                vTaskDelay(10 / portTICK_RATE_MS);
                SettingsSendToLocalFlash();

                debug_printf("Resetting...\r\n");
                vTaskDelay(4000 / portTICK_RATE_MS);
  //              SysCtlReset();
                Restart(BOOTLOADER);
              }
              else
              {
                debug_printf("Firmware file error.\r\n");
              }
            }

            i = 0;
          }

          if ((j++ % 10) == 0)
            debug_printf("Bootloader waiting\r\n");

          // reset the ARM watchdog timer
      //      AT91F_WDTRestart(AT91C_BASE_WDTC);
        }
      }
//      else if (FileReceive.Type == FRT_SETTINGS)
//      {
//        debug_printf("ThreadFileReceive: Settings...\r\n");
//        LtxInitialized = 1;   // this will cause UI_SendAllSettings() to update the UI
//        debug_printf("ThreadFileReceive: Exiting Equipment Descriptions...\r\n");
//      }
      else if (   FileReceive.Type == FRT_EQUIPDESC
               || FileReceive.Type == FRT_SETTINGS)
      {
        int noMessageReceived = 0;
        char remoteOff = 0;

        debug_printf("ThreadFileReceive: Equipment Descriptions...\r\n");

        // to avoid downloading the translated string on every restart, check if the first analog board label
        // is blank which indicates a factory reset of system settings in which case we will accept the strings
//        if (strcmp(Settings.AnalogBoard[0].Label, "") == 0)
//        {
          UI_SendFileTransferAck(1);
//        }
//        else
//        {
//          UI_SendFileTransferAck(0);
//          FileReceive.Status = FR_IDLE;
//        }

        // put the system in remote off
        if (FileReceive.Type == FRT_SETTINGS)
        {
          remoteOff = Settings.RemoteOff[RO_FAN];
          Settings.RemoteOff[RO_FAN] = 2;
        }

        while (   FileReceive.Status == FR_RECEIVING
               && ++noMessageReceived < 1000)
        {
          vTaskDelay(10 / portTICK_RATE_MS);

          // check for message from UI
          if (Usart_CharsBuffered())
          {
            if (Usart_ProcessBuffer(UI_Message.RxBuffer) == 1)
            {
              ProcessUIMessage();
              noMessageReceived = 0;
            }
          }
        }

        // restore the system state
        if (FileReceive.Type == FRT_SETTINGS)
        {
          Settings.RemoteOff[RO_FAN] = remoteOff;
        }

        LtxInitialized = 1;   // this will cause UI_SendAllSettings() to update the UI
        debug_printf("ThreadFileReceive: Exiting Equipment Descriptions...\r\n");
      }

      FileReceive.Status = FR_IDLE;
    }
  }
} // ThreadFileReceive()

/***   End Of File   ***/
