/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     Analog_Input.c

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:  Analog board communication

COMMENTS:

***************************************************************************/

/*** include files ***/

#include <string.h>
#include <stdio.h>

// Platform
#include "system.h"

// FreeRTOS Includes
#include "FreeRTOS.h"
#include "task.h"

// Gellert
#include "Analog_Input.h"
#include "DataExc.h"
#include "LoadLogs.h"
#include "RS485.h"
#include "Settings.h"
#include "States.h"
#include "ThreadMonitor.h"
#include "Timer.h"
#include "Warnings.h"

/*** defines ***/

/*** typedefs and structures ***/

/*** module variables ***/

float *OutsideTemp = &(Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Sensor[SENSOR_OUTSIDE_TEMP].Value);
float PlenumTempAvg = SENSOR_VAL_UNDEFINED;
float StartTemp = SENSOR_VAL_UNDEFINED;

static TEMP_TABLE TempTable[TEMP_TABLE_SIZE];

static char NextAnalogBoardRead = ANALOG_BOARD_READ_DEFAULT_SIZE;
static unsigned int DiscoverAnalogBoardsStatus;

/*** static functions ***/

static void CalculatePlenumTemp(void);
static int ConvertToCO2(int ADC);
static int ConvertToHumid(int ADC);
static float ConvertToIrTemp(int ADC);
static float ConvertToTemp(int ADC);
static int ReadSensors(int Board);

/***************************************************************************

FUNCTION:   CalculatePlenumTemp()

PURPOSE:    Calculate the average plenum temperature.

COMMENTS:   The plenum temperature on the display is an average of the
            plenum 1 & plenum 2 sensors.  If one or the other of the sensor
            values is invalid, it will display the other and report an error.
            If they are both invalid, it will display "--" and report an error.

***************************************************************************/
void CalculatePlenumTemp(void)
{
  int   ValidSensors = 0;

  if (Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Sensor[SENSOR_PLENUM_TEMP_1].Value == SENSOR_VAL_UNDEFINED)
  {
    WarningsSet(WARN_PLENTEMP1, FM_ALARM, FM_ALARM, NA);
  }
  else
  {
    PlenumTempAvg = Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Sensor[SENSOR_PLENUM_TEMP_1].Value;
    ValidSensors++;
  }

  if (Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Sensor[SENSOR_PLENUM_TEMP_2].Value == SENSOR_VAL_UNDEFINED)
  {
    WarningsSet(WARN_PLENTEMP2, FM_ALARM, FM_ALARM, NA);
  }
  else
  {
    if (ValidSensors == 1)
      PlenumTempAvg += Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Sensor[SENSOR_PLENUM_TEMP_2].Value;
    else
      PlenumTempAvg = Settings.AnalogBoard[DEFAULT_TEMP_BOARD].Sensor[SENSOR_PLENUM_TEMP_2].Value;
    ValidSensors++;
  }

  // calculate Plenum Temp
  if (ValidSensors > 0)
    PlenumTempAvg /= ValidSensors;
  else
    PlenumTempAvg = SENSOR_VAL_UNDEFINED;
} // end CalculatePlenumTemp()

/***************************************************************************

FUNCTION:   ConvertToCO2()

PURPOSE:    Convert ADC to CO2 value

COMMENTS:   CO2 response is linear from 0-10000ppm on the 4-20mA input.
            The ADC reads from 0 - ~22.75mA, but the valid range is 4-20mA.
            So the 4-20 range is ~180-900.  So we only convert values within
            that range.

            Note: the A/D count comes from the analog board multiplied by 16
            to increase resolution.  That's why it's immediately divided by 16.

            Note: 10/27/14, reducing the operating range of the sensor to
            avoid bad sensors getting a valid 10000 value.

***************************************************************************/
int ConvertToCO2(int ADC)
{
  float ScaledADC = (float)ADC/16.0;

  if ((ScaledADC < 180) || (ScaledADC > 895))   // reduce range to 9930.5
    return(SENSOR_VAL_UNDEFINED);

  return(((ScaledADC - 180)/720.0)*10000.0);
} // end ConvertToCO2()

/***************************************************************************

FUNCTION:   ConvertToHumid()

PURPOSE:    Convert ADC to humidity value

COMMENTS:   The humidity response is linear from 0-100% on the 4-20mA input.
            The ADC reads from 0 - ~22.75mA, but the valid range is 4-20mA.
            So the 4-20 range is ~180-900.  So we only convert values within
            that range. Values above 900 are returned as 100% per Gellert.

            Note: the A/D count comes from the analog board multiplied by 16
            to increase resolution.  That's why it's immediately divided by 16.

            Testing
            ScaledADC = 536, humid = 49
            ScaledADC = 179, humid = undefined  ScaledADC = 901, humid = 100
            ScaledADC = 180, humid = 0          ScaledADC = 900, humid = 100

***************************************************************************/
int ConvertToHumid(int ADC)
{
  float ScaledADC = (float)ADC/16.0;

  if (ScaledADC < 180)
    return(SENSOR_VAL_UNDEFINED);

  // if the sensor saturates, return 100% (Gellert says it is common)
  if (ScaledADC > 900)
    return(100);

  return(((ScaledADC - 180)/720.0)*100.0);
} // end ConvertToHumid()

/***************************************************************************

FUNCTION:   ConvertToIrTemp()

PURPOSE:    Convert ADC to temperature value

COMMENTS:   The temperature response is linear on the 4-20mA input.
            The ADC reads from 0 - ~22.75mA, but the valid range is 4-20mA.
            So the 4-20 range is ~180-900.  So we only convert values within
            that range.

            The sensor manufacturer provided the result calculation as the
            line described by (4mA,32F),(20mA,482F) or y = 28.125x - 80.5

            Note: the A/D count comes from the analog board multiplied by 16
            to increase resolution.  That's why it's immediately divided by 16.

***************************************************************************/
float ConvertToIrTemp(int ADC)
{
  float ScaledADC = (float)ADC/16.0;

  if ((ScaledADC < 180) || (ScaledADC > 900))
    return(SENSOR_VAL_UNDEFINED);

  float m420 = (((ScaledADC - 180)/720.0) * 16) + 4;
  float temp = (m420 * 28.125) - 80.5;
  return temp;
} // end ConvertToIrTemp()

/***************************************************************************

FUNCTION:   ConvertToTemp()

PURPOSE:    Convert ADC to temperature

COMMENTS:   Rt is calculated based on the resistor values and then looked up in a
            table.  A linear interpolation is performed to calculate the temp
            from the table values.

            The table contains values from -40 to 83 C (-40 to 181.4 F).
            The table can be increased if needed.

            Note: the A/D count comes from the analog board multiplied by 16
            to increase resolution.  That's why it's immediately divided by 16.

            The ADC is a 10 bit value so it saturates at 1023 (which comes as
            1023 * 16 or 16368).  That computes to an Rt value of ~255.9 which
            is just beyond the Rt value for 83 C.  83 C was chosen as the highest
            temperature value so when the ADC/sensor saturates, SENSOR_VAL_UNDEFINED
            is used to indicate saturation and the UI can display "--" instead of
            'pegging' at 84 C.

            Testing
            ADC = 11967, temp = 10.414 C
            ADC = 2123 gives Rt of 75781 which is beyond the table, temp = undefined
            ADC = 15908, temp = 58.9 C
            ADC = 15909 gives Rt of 580 which is beyond the table, temp = undefined

***************************************************************************/
float ConvertToTemp(int ADC)
{
  int   i     = 0;
  float Rt    = 0.0;
  int   Rg1   = 27400;
  int   Rg2   = 10000;
  int   Rth1  = 10000;
  int   Rth2  = 1000;
  int   R1ref = 30100;
  int   R2ref = 15000;
  float temp;
  int   Xa;
  int   Xb;
  int   Ya;
  int   Yb;

  //       (1024)(1 + (Rg1/Rg2))(Rth2(R1ref+R2ref))
  // Rt = -------------------------------------------  -  (Rth1 + Rth2)
  //                (A/D count)(R2ref)

  Rt = ((1024.0 * ((1.0+((float)Rg1/Rg2)) * (Rth2*(R1ref + R2ref))))/(((float)ADC/16.0)*R2ref)) - (Rth1+Rth2);

  // range check it to see if it's in the table
  if ((Rt > TempTable[0].RtValue) || (Rt < TempTable[TEMP_TABLE_SIZE-1].RtValue))
    return(SENSOR_VAL_UNDEFINED);

  // look it up in the table
  while ((TempTable[i].RtValue > Rt) && (i < TEMP_TABLE_SIZE))
    i++;

  // perform the linear interpolation
  Xa = TempTable[i-1].RtValue;
  Ya = TempTable[i-1].DegreesC;
  Xb = TempTable[i].RtValue;
  Yb = TempTable[i].DegreesC;

  temp = Ya + (((Rt-Xa)*(Yb-Ya))/((float)Xb-Xa));

  if (Settings.TempType == 1)
    return(temp);
  else
    return((temp*1.8) + 32.0);
} // end ConvertToTemp()

/***************************************************************************

FUNCTION:   DiscoverAnalogBoards()

PURPOSE:    Check for analog board discovery request

COMMENTS:

***************************************************************************/
unsigned int DiscoverAnalogBoards(void)
{
  return DiscoverAnalogBoardsStatus;
} // end DiscoverAnalogBoards()

/***************************************************************************

FUNCTION:   DiscoverAnalogBoardsRequest()

PURPOSE:    Request analog board discovery

COMMENTS:

***************************************************************************/
void DiscoverAnalogBoardsRequest(unsigned int request)
{
  DiscoverAnalogBoardsStatus = request;
} // end DiscoverAnalogBoardsRequest()

/***************************************************************************

FUNCTION:   FindAnalogBoards()

PURPOSE:    Discover analog boards present on RS485 bus

COMMENTS:   It is necessary to wait not only between sending subsequent messages
            to the same board, but also to the next board.

            3/21/07
            Tested with board address 0-5, 8-13, 24-29, 30, 31 and with gaps

***************************************************************************/
void FindAnalogBoards(void)
{
  int  i;
  int  Msg1Count;
  int  MsgResponse;
  int  Msg1Received;
  unsigned int TimeOut;

  debug_printf("FindAnalogBoards\r\n");

  for (i = 0; i < ANALOG_BOARDS_PER_SYSTEM; i++)
  {
//    RS485_BuildPacket(0, RS485_QRY_FIRMWARE, RS485_MSG_DELAY_10ms, 0, 0);
    RS485_BuildPacket(i, RS485_QRY_FIRMWARE, RS485_MSG_DELAY_10ms, 0, 0);
    MsgResponse = -5;
    Msg1Received = 0;
    Msg1Count = 0;

    while (   (Msg1Received == 0)
           && (RS485_Message.Errors < RS485_MSG_ERROR_LIMIT)
           && (Msg1Count < ANALOG_BOARD_COMM_RETRIES)  )
    {
      RS485_SendMessage();
      Msg1Count++;
      TimeOut = uptime_ms + 50U;
      while ((uptime_ms < TimeOut) && (Msg1Received == 0))
      {
        vTaskDelay(10/portTICK_RATE_MS);

        if (RS485_CharsBuffered())
        {
          MsgResponse = RS485_ProcessBuffer();
          if (MsgResponse == 0)   // good response
          {
            Msg1Received = 1;
            Settings.AnalogBoard[i].Address = RS485_Message.RxBuffer[RS485_MSG_ADDRESS_BYTE] & RS485_MSG_ADDRESS_MASK;
            RS485_ProcessCmd(RS485_Message.RxBuffer, Settings.AnalogBoard[i].Version);
            if (Settings.AnalogBoard[i].Present == 0)
            {
              WarningsSet(WARN_NEWBOARD, FM_ALARM, NA, i);
            }
            Settings.AnalogBoard[i].Present = 1;

            // if it's not a default board and the label is blank, create a label
            // NOTE: the default temperature and humidity boards will get translated names
            //       on startup if their labels are blank
            if (   i != DEFAULT_TEMP_BOARD
                && i != DEFAULT_HUMID_BOARD
                && strcmp(Settings.AnalogBoard[i].Label, "") == 0)
            {
              sprintf(Settings.AnalogBoard[i].Label, "Board - %d", (Settings.AnalogBoard[i].Address + 1));
            }

            // wait before sending the next message
            vTaskDelay(15/portTICK_RATE_MS);
          } // if good response
        } // if chars buffered
      } // while timeout msg1

      // timed out, resend the message
      if (Msg1Received == 0)
        RS485_Message.State = RS485_MSG_SEND;
    } // while error limit msg1

    if (RS485_Message.Errors >= RS485_MSG_ERROR_LIMIT)
    {
      // set the warning and store the address
      WarningsSet(WARN_COMMERR, FM_ALARM, NA, i);

      // move on
      RS485_Message.State = RS485_MSG_IDLE;
      RS485_Message.Errors = 0;
    }

    if (MsgResponse != 0)
    {
      if (Settings.AnalogBoard[i].Present == 1)
      {
        // set the warning and store the address
        WarningsSet(WARN_BOARDREMOVED, FM_ALARM, NA, i);
      }
      Settings.AnalogBoard[i].Present = 0;
      Settings.AnalogBoard[i].Type = 0;
      Settings.AnalogBoard[i].Address = 0;
      Settings.AnalogBoard[i].Version[0] = 0;

      // move on
      RS485_Message.State = RS485_MSG_IDLE;
      RS485_Message.Errors = 0;
    }

    ThreadMonitorUpdate(TM_SERIAL_COM);
  } // for (all boards)
} // end FindAnalogBoards()

/***************************************************************************

FUNCTION:   ReadAnalogBoards()

PURPOSE:    Read the analog boards that are present and not disabled

COMMENTS:   3/21/07
            Tested with FindAnalogBoards

***************************************************************************/
void ReadAnalogBoards(char ReadType)
{
  int i;
  int Count = 0;
  int GroupSize;

  // determine which boards to read
  if (ReadType == RT_ALL)
  {
    i = 0;
    GroupSize = ANALOG_BOARDS_PER_SYSTEM;
  }
  else if (ReadType == RT_DEFAULT)
  {
    i = 0;
    GroupSize = ANALOG_BOARD_READ_DEFAULT_SIZE;
  }
  else
  {
    i = NextAnalogBoardRead;
    GroupSize = ANALOG_BOARD_READ_GROUP_SIZE;
  }

  // read the boards
  while (   Count < GroupSize
         && i < ANALOG_BOARDS_PER_SYSTEM)
  {
    if (Settings.AnalogBoard[i].Present == 1)
    {
      ReadSensors(i);
      vTaskDelay(15/portTICK_RATE_MS);
      ThreadMonitorUpdate(TM_SERIAL_COM);
      Count++;
    }

    // average the two plenum temperature sensors
    if (i == 0)
      CalculatePlenumTemp();

    i++;
  }

  // adjust the next board to read
  if (ReadType == RT_GROUPS)
  {
    NextAnalogBoardRead = i;
    if (NextAnalogBoardRead == ANALOG_BOARDS_PER_SYSTEM)
    {
      NextAnalogBoardRead = ANALOG_BOARD_READ_DEFAULT_SIZE;
    }
  }
} // end ReadAnalogBoards()

/***************************************************************************

FUNCTION:   ReadSensors()

PURPOSE:    Read and store the analog board sensors

COMMENTS:   3/21/07
            Tested with ReadAnalogBoards

***************************************************************************/
int ReadSensors(int Board)
{
  int  i;
  int  Msg2Count = 0;
  int  MsgResponse = -5;
  int  Msg2Received = 0;
  int  NumRetries = ANALOG_BOARD_COMM_RETRIES;
  int  SensorData[ANALOG_SENSORS_PER_BOARD+1];
  char SensorType;
  float SensorValue;
  unsigned int TimeOut;

  if (Settings.AnalogBoard[Board].CommErr > 0)
    NumRetries = 1;

//  RS485_BuildPacket(0, RS485_QRY_SENSORS, RS485_MSG_DELAY_10ms, 0, 0);
  RS485_BuildPacket(Settings.AnalogBoard[Board].Address, RS485_QRY_SENSORS, RS485_MSG_DELAY_10ms, 0, 0);
//  debug_printf("Read board:%d\r\n", Settings.AnalogBoard[Board].Address);

  while (   (Msg2Received == 0)
         && (RS485_Message.Errors < RS485_MSG_ERROR_LIMIT)
         && (Msg2Count < NumRetries)   )
  {
    RS485_SendMessage();
    Msg2Count++;
    TimeOut = uptime_ms + 50U;
    while ((uptime_ms < TimeOut) && (Msg2Received == 0))
    {
      vTaskDelay(10/portTICK_RATE_MS);

      if (RS485_CharsBuffered())
      {
        MsgResponse = RS485_ProcessBuffer();
        if (MsgResponse == 0)
        {
          Msg2Received = 1;
          RS485_ProcessCmd(RS485_Message.RxBuffer, SensorData);

          // set the board type to the type of the first sensor
          if ((SensorData[0] & 0x3) == ANALOG_SENSOR_TYPE_TEMP)
            Settings.AnalogBoard[Board].Type = ANALOG_BOARD_TYPE_TEMP;
          else
            Settings.AnalogBoard[Board].Type = ANALOG_BOARD_TYPE_HUMID;

          // convert and store the sensor readings (if the sensors are not disabled)
          for (i = 0; i < ANALOG_SENSORS_PER_BOARD; i++)
          {
            // don't modify outside temp or humidity if in slave mode
            if (   Settings.MasterSlave == MSMODE_SLAVE
                && (   (Board == DEFAULT_TEMP_BOARD && i == SENSOR_OUTSIDE_TEMP)
                    || (Board == DEFAULT_HUMID_BOARD && i == SENSOR_OUTSIDE_HUMID) ) )
            {
              continue;
            }

            SensorType = (SensorData[0] & (0x3<<(i*2)))>>(i*2);

            if (Settings.AnalogBoard[Board].Sensor[i].Disabled == 0)
            {
              if (SensorType == ANALOG_SENSOR_TYPE_TEMP)
              {
                SensorValue = ConvertToTemp(SensorData[i+1]);
              }
              else if (SensorType == ANALOG_SENSOR_TYPE_HUMID)
              {
                SensorValue = ConvertToHumid(SensorData[i+1]);
              }
              else if (SensorType == ANALOG_SENSOR_TYPE_CO2)
              {
                SensorValue = ConvertToCO2(SensorData[i+1]);
              }
              else if (SensorType == ANALOG_SENSOR_TYPE_TEMP_IR)
              {
                SensorValue = ConvertToIrTemp(SensorData[i+1]);
              }
            }
            else
            {
              SensorValue = SENSOR_VAL_UNDEFINED;
            }

            // add the sensor offset
            if (SensorValue != SENSOR_VAL_UNDEFINED)
            {
              SensorValue += Settings.AnalogBoard[Board].Sensor[i].Offset;

              // range check value after adding the offset
              if (SensorType == ANALOG_SENSOR_TYPE_HUMID)
              {
                if (SensorValue < 0)
                {
                  SensorValue = 0;
                }
                else if (SensorValue > 100)
                {
                  SensorValue = 100;
                }
              }
              else if (SensorType == ANALOG_SENSOR_TYPE_CO2)
              {
                if (SensorValue < 0)
                {
                  SensorValue = 0;
                }
                else if (SensorValue > 10000)
                {
                  SensorValue = 10000;
                }
              }
            }

            Settings.AnalogBoard[Board].Sensor[i].Type = SensorType;
            Settings.AnalogBoard[Board].Sensor[i].Value = SensorValue;

            if (SensorType == ANALOG_SENSOR_TYPE_TEMP_IR)
            {
              LoadLogTempAccumulator((Board * ANALOG_SENSORS_PER_BOARD) + i, SensorValue);
            }

            // if it's not a default board and the label is blank, create a label
            // NOTE: the default temperature and humidity boards will get translated names
            //       on startup if their labels are blank
            if (   Board != DEFAULT_TEMP_BOARD
                && Board != DEFAULT_HUMID_BOARD
                && strcmp(Settings.AnalogBoard[Board].Sensor[i].Label, "") == 0)
            {
              sprintf(Settings.AnalogBoard[Board].Sensor[i].Label, "Bd %d - S %d", (Board + 1), (i + 1));
            }
          }

          // clear the comm error flag for the board
          Settings.AnalogBoard[Board].CommErr = 0;

          // if the warning status is preliminary, clear it
          if (WarningStatus(WARN_COMMERR) == FM_PRELIM)
            WarningsSet(WARN_COMMERR, FM_NONE, NA, Board);
        } // if good response
      } // if chars buffered
    } // while timeout msg2

    // timed out, resend the message
    if (Msg2Received == 0)
      RS485_Message.State = RS485_MSG_SEND;
  } // while error limit

  // timed out, report comm error and move on
  if (Msg2Received == 0)
  {
    Settings.AnalogBoard[Board].CommErr++;

    if (Settings.AnalogBoard[Board].CommErr > 55)   // ~3 mins
    {
      // set the warning and store the address
      WarningsSet(WARN_COMMERR, FM_ALARM, NA, Board);

      // mark the sensor values as undefined so old values don't continue to be used
      for (i = 0; i < ANALOG_SENSORS_PER_BOARD; i++)
        Settings.AnalogBoard[Board].Sensor[i].Value = SENSOR_VAL_UNDEFINED;
    }
    else
    {
      // use the status of 255 (preliminary) to allow the comm error to be logged in the system
      // log, but not reported to the UI (until in error for ~2 mins - see above)
      WarningsSet(WARN_COMMERR, FM_PRELIM, NA, Board);
    }

    RS485_Message.Errors = 0;
  }

  RS485_Message.State = RS485_MSG_IDLE;

  return(MsgResponse);
} // end ReadSensors()

/***************************************************************************

FUNCTION:   TempTable_Init()

PURPOSE:    Define the temperature lookup table

COMMENTS:   More values can be added to this table if necessary

            The ADC is a 10 bit value so it saturates at 1023 (which comes as
            1023 * 16 or 16368).  That computes to an Rt value of ~255.9 which
            is just beyond the Rt value for 83 C.  83 C was chosen as the highest
            temperature value so when the ADC/sensor saturates, SENSOR_VAL_UNDEFINED
            is used to indicate saturation and the UI can display "--" instead of
            'pegging' at 84 C.

***************************************************************************/
void TempTable_Init(void)
{
  TempTable[0].DegreesC  = -40;   TempTable[0].RtValue  = 75777;
  TempTable[1].DegreesC  = -39;   TempTable[1].RtValue  = 70918;
  TempTable[2].DegreesC  = -38;   TempTable[2].RtValue  = 66401;
  TempTable[3].DegreesC  = -37;   TempTable[3].RtValue  = 62200;
  TempTable[4].DegreesC  = -36;   TempTable[4].RtValue  = 58292;
  TempTable[5].DegreesC  = -35;   TempTable[5].RtValue  = 54653;
  TempTable[6].DegreesC  = -34;   TempTable[6].RtValue  = 51264;
  TempTable[7].DegreesC  = -33;   TempTable[7].RtValue  = 48106;
  TempTable[8].DegreesC  = -32;   TempTable[8].RtValue  = 45162;
  TempTable[9].DegreesC  = -31;   TempTable[9].RtValue  = 42416;
  TempTable[10].DegreesC = -30;   TempTable[10].RtValue = 39855;
  TempTable[11].DegreesC = -29;   TempTable[11].RtValue = 37463;
  TempTable[12].DegreesC = -28;   TempTable[12].RtValue = 35230;
  TempTable[13].DegreesC = -27;   TempTable[13].RtValue = 33144;
  TempTable[14].DegreesC = -26;   TempTable[14].RtValue = 31194;
  TempTable[15].DegreesC = -25;   TempTable[15].RtValue = 29371;
  TempTable[16].DegreesC = -24;   TempTable[16].RtValue = 27665;
  TempTable[17].DegreesC = -23;   TempTable[17].RtValue = 26069;
  TempTable[18].DegreesC = -22;   TempTable[18].RtValue = 24574;
  TempTable[19].DegreesC = -21;   TempTable[19].RtValue = 23174;
  TempTable[20].DegreesC = -20;   TempTable[20].RtValue = 21862;
  TempTable[21].DegreesC = -19;   TempTable[21].RtValue = 20633;
  TempTable[22].DegreesC = -18;   TempTable[22].RtValue = 19480;
  TempTable[23].DegreesC = -17;   TempTable[23].RtValue = 18398;
  TempTable[24].DegreesC = -16;   TempTable[24].RtValue = 17383;
  TempTable[25].DegreesC = -15;   TempTable[25].RtValue = 16430;
  TempTable[26].DegreesC = -14;   TempTable[26].RtValue = 15535;
  TempTable[27].DegreesC = -13;   TempTable[27].RtValue = 14694;
  TempTable[28].DegreesC = -12;   TempTable[28].RtValue = 13903;
  TempTable[29].DegreesC = -11;   TempTable[29].RtValue = 13160;
  TempTable[30].DegreesC = -10;   TempTable[30].RtValue = 12461;
  TempTable[31].DegreesC = -9;    TempTable[31].RtValue = 11803;
  TempTable[32].DegreesC = -8;    TempTable[32].RtValue = 11183;
  TempTable[33].DegreesC = -7;    TempTable[33].RtValue = 10600;
  TempTable[34].DegreesC = -6;    TempTable[34].RtValue = 10051;
  TempTable[35].DegreesC = -5;    TempTable[35].RtValue = 9533;
  TempTable[36].DegreesC = -4;    TempTable[36].RtValue = 9045;
  TempTable[37].DegreesC = -3;    TempTable[37].RtValue = 8585;
  TempTable[38].DegreesC = -2;    TempTable[38].RtValue = 8151;
  TempTable[39].DegreesC = -1;    TempTable[39].RtValue = 7741;
  TempTable[40].DegreesC = 0;     TempTable[40].RtValue = 7353;
  TempTable[41].DegreesC = 1;     TempTable[41].RtValue = 6988;
  TempTable[42].DegreesC = 2;     TempTable[42].RtValue = 6643;
  TempTable[43].DegreesC = 3;     TempTable[43].RtValue = 6318;
  TempTable[44].DegreesC = 4;     TempTable[44].RtValue = 6010;
  TempTable[45].DegreesC = 5;     TempTable[45].RtValue = 5719;
  TempTable[46].DegreesC = 6;     TempTable[46].RtValue = 5444;
  TempTable[47].DegreesC = 7;     TempTable[47].RtValue = 5183;
  TempTable[48].DegreesC = 8;     TempTable[48].RtValue = 4937;
  TempTable[49].DegreesC = 9;     TempTable[49].RtValue = 4703;
  TempTable[50].DegreesC = 10;    TempTable[50].RtValue = 4482;
  TempTable[51].DegreesC = 11;    TempTable[51].RtValue = 4273;
  TempTable[52].DegreesC = 12;    TempTable[52].RtValue = 4075;
  TempTable[53].DegreesC = 13;    TempTable[53].RtValue = 3886;
  TempTable[54].DegreesC = 14;    TempTable[54].RtValue = 3708;
  TempTable[55].DegreesC = 15;    TempTable[55].RtValue = 3539;
  TempTable[56].DegreesC = 16;    TempTable[56].RtValue = 3378;
  TempTable[57].DegreesC = 17;    TempTable[57].RtValue = 3226;
  TempTable[58].DegreesC = 18;    TempTable[58].RtValue = 3081;
  TempTable[59].DegreesC = 19;    TempTable[59].RtValue = 2944;
  TempTable[60].DegreesC = 20;    TempTable[60].RtValue = 2814;
  TempTable[61].DegreesC = 21;    TempTable[61].RtValue = 2690;
  TempTable[62].DegreesC = 22;    TempTable[62].RtValue = 2572;
  TempTable[63].DegreesC = 23;    TempTable[63].RtValue = 2460;
  TempTable[64].DegreesC = 24;    TempTable[64].RtValue = 2353;
  TempTable[65].DegreesC = 25;    TempTable[65].RtValue = 2252;
  TempTable[66].DegreesC = 26;    TempTable[66].RtValue = 2156;
  TempTable[67].DegreesC = 27;    TempTable[67].RtValue = 2064;
  TempTable[68].DegreesC = 28;    TempTable[68].RtValue = 1977;
  TempTable[69].DegreesC = 29;    TempTable[69].RtValue = 1894;
  TempTable[70].DegreesC = 30;    TempTable[70].RtValue = 1814;
  TempTable[71].DegreesC = 31;    TempTable[71].RtValue = 1739;
  TempTable[72].DegreesC = 32;    TempTable[72].RtValue = 1667;
  TempTable[73].DegreesC = 33;    TempTable[73].RtValue = 1598;
  TempTable[74].DegreesC = 34;    TempTable[74].RtValue = 1533;
  TempTable[75].DegreesC = 35;    TempTable[75].RtValue = 1471;
  TempTable[76].DegreesC = 36;    TempTable[76].RtValue = 1411;
  TempTable[77].DegreesC = 37;    TempTable[77].RtValue = 1355;
  TempTable[78].DegreesC = 38;    TempTable[78].RtValue = 1300;
  TempTable[79].DegreesC = 39;    TempTable[79].RtValue = 1249;
  TempTable[80].DegreesC = 40;    TempTable[80].RtValue = 1199;
  TempTable[81].DegreesC = 41;    TempTable[81].RtValue = 1152;
  TempTable[82].DegreesC = 42;    TempTable[82].RtValue = 1107;
  TempTable[83].DegreesC = 43;    TempTable[83].RtValue = 1064;
  TempTable[84].DegreesC = 44;    TempTable[84].RtValue = 1023;
  TempTable[85].DegreesC = 45;    TempTable[85].RtValue = 983.6;
  TempTable[86].DegreesC = 46;    TempTable[86].RtValue = 946;
  TempTable[87].DegreesC = 47;    TempTable[87].RtValue = 910;
  TempTable[88].DegreesC = 48;    TempTable[88].RtValue = 875.6;
  TempTable[89].DegreesC = 49;    TempTable[89].RtValue = 842.6;
  TempTable[90].DegreesC = 50;    TempTable[90].RtValue = 811.1;
  TempTable[91].DegreesC = 51;    TempTable[91].RtValue = 780.9;
  TempTable[92].DegreesC = 52;    TempTable[92].RtValue = 752;
  TempTable[93].DegreesC = 53;    TempTable[93].RtValue = 724.3;
  TempTable[94].DegreesC = 54;    TempTable[94].RtValue = 697.8;
  TempTable[95].DegreesC = 55;    TempTable[95].RtValue = 672.4;
  TempTable[96].DegreesC = 56;    TempTable[96].RtValue = 648;
  TempTable[97].DegreesC = 57;    TempTable[97].RtValue = 624.7;
  TempTable[98].DegreesC = 58;    TempTable[98].RtValue = 602.3;
  TempTable[99].DegreesC = 59;    TempTable[99].RtValue = 580.8;
  TempTable[100].DegreesC = 60;    TempTable[100].RtValue = 560.2;
  TempTable[101].DegreesC = 61;    TempTable[101].RtValue = 540.2;
  TempTable[102].DegreesC = 62;    TempTable[102].RtValue = 521.5;
  TempTable[103].DegreesC = 63;    TempTable[103].RtValue = 503.3;
  TempTable[104].DegreesC = 64;    TempTable[104].RtValue = 485.8;
  TempTable[105].DegreesC = 65;    TempTable[105].RtValue = 469;
  TempTable[106].DegreesC = 66;    TempTable[106].RtValue = 452.9;
  TempTable[107].DegreesC = 67;    TempTable[107].RtValue = 437.4;
  TempTable[108].DegreesC = 68;    TempTable[108].RtValue = 422.6;
  TempTable[109].DegreesC = 69;    TempTable[109].RtValue = 408.3;
  TempTable[110].DegreesC = 70;    TempTable[110].RtValue = 394.6;
  TempTable[111].DegreesC = 71;    TempTable[111].RtValue = 381.3;
  TempTable[112].DegreesC = 72;    TempTable[112].RtValue = 368.6;
  TempTable[113].DegreesC = 73;    TempTable[113].RtValue = 356.4;
  TempTable[114].DegreesC = 74;    TempTable[114].RtValue = 344.7;
  TempTable[115].DegreesC = 75;    TempTable[115].RtValue = 333.4;
  TempTable[116].DegreesC = 76;    TempTable[116].RtValue = 322.5;
  TempTable[117].DegreesC = 77;    TempTable[117].RtValue = 312.1;
  TempTable[118].DegreesC = 78;    TempTable[118].RtValue = 302;
  TempTable[119].DegreesC = 79;    TempTable[119].RtValue = 292.3;
  TempTable[120].DegreesC = 80;    TempTable[120].RtValue = 282.9;
  TempTable[121].DegreesC = 81;    TempTable[121].RtValue = 273.9;
  TempTable[122].DegreesC = 82;    TempTable[122].RtValue = 265.3;
  TempTable[123].DegreesC = 83;    TempTable[123].RtValue = 256.9;

  // sensor saturates here

//  TempTable[124].DegreesC = 84;    TempTable[124].RtValue = 248.9;
//  TempTable[125].DegreesC = 85;    TempTable[125].RtValue = 241.1;
//  TempTable[126].DegreesC = 86;    TempTable[126].RtValue = 233.7;
//  TempTable[127].DegreesC = 87;    TempTable[127].RtValue = 226.4;
//  TempTable[128].DegreesC = 88;    TempTable[128].RtValue = 219.5;
//  TempTable[129].DegreesC = 89;    TempTable[129].RtValue = 212.8;
} // end TempTable_Init()

/***   End Of File   ***/
