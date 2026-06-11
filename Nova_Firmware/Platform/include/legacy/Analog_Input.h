/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     Analog_Input.h

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:

COMMENTS:

***************************************************************************/

#ifndef ANALOG_INPUT_H
#define ANALOG_INPUT_H

/*** include files ***/

/*** defines ***/

#define ANALOG_BOARDS_PER_SYSTEM        32
#define ANALOG_SENSORS_PER_BOARD        4

#define ANALOG_BOARD_COMM_RETRIES       3

#define ANALOG_BOARD_TYPE_CO2           2
#define ANALOG_BOARD_TYPE_HUMID         1
#define ANALOG_BOARD_TYPE_TEMP          3
#define ANALOG_BOARD_TYPE_TEMP_IR       0

#define ANALOG_SENSOR_TYPE_CO2          2
#define ANALOG_SENSOR_TYPE_HUMID        1
#define ANALOG_SENSOR_TYPE_TEMP         3
#define ANALOG_SENSOR_TYPE_TEMP_IR      0
#define ANALOG_SENSOR_TYPE_PRESSURE     4
#define ANALOG_SENSOR_TYPE_MA           5   /* Generic 4-20 mA, scaled per-sensor */
/* Static-pressure 4-20 mA → 0-2.5"wc sensor (newer Mini_IO 2.0.1.b,
 * Analog_Input.h:48). Value 11 matches AS2 AND the UI sensor-type
 * picker (AnalogConfigForm.svelte offers '11'). SetAnalogBoardTypes()
 * scans every present+enabled board/sensor and records the slot whose
 * Type == this value into the SENSOR_STATIC_PRESSURE global below. */
#define ANALOG_SENSOR_TYPE_STATIC_PRESS 11

#define BOARD_LABELS                    25

#define DEFAULT_TEMP_BOARD              0   // NOTE: these values are used by the http server to send translated descriptions
#define DEFAULT_HUMID_BOARD             1   // to the ARM at startup, any changes here must be reflected in LtxWarnings.h
#define ANALOG_BOARD_READ_DEFAULT_SIZE  2
#define ANALOG_BOARD_READ_GROUP_SIZE    4

// analog board group read types (RT)
#define RT_DEFAULT                      0
#define RT_GROUPS                       1
#define RT_ALL                          3

#define SENSOR_PLENUM_TEMP_1            0   // NOTE: these values are used by the http server to send translated descriptions
#define SENSOR_PLENUM_TEMP_2            1   // to the ARM at startup, any changes here must be reflected in LtxWarnings.h
#define SENSOR_OUTSIDE_TEMP             2
#define SENSOR_RETURN_TEMP              3
#define SENSOR_OUTSIDE_HUMID            0
#define SENSOR_PLENUM_HUMID             1
#define SENSOR_RETURN_HUMID             2
#define SENSOR_CO2                      3
#define SENSOR_TEMP_IR                  SENSOR_CO2

#define SENSOR_VAL_UNDEFINED            0x7FFF

#define TEMP_TABLE_SIZE                 124     // temperature lookup table

/*** typedefs and structures ***/

typedef enum
{
  AB_CLEAR = 0,
  AB_DISCOVER = 1,
  AB_REPORT = 2
} ANALOG_BOARD_DISCOVERY;

typedef struct {
  float Value;
  char  Label[BOARD_LABELS+1];
  char  SID;    // unique sensor ID 0-127
} SENSOR_ARRAY;

typedef struct {
  int   DegreesC;
  float RtValue;
} TEMP_TABLE;

/*** external variables ***/

extern float *OutsideTemp;
extern float PlenumTempAvg;
extern float StartTemp;

/* Runtime-resolved sensor-role index (newer Mini_IO 2.0.1.b port).
 * -1 = no static-pressure sensor present. When >= 0 it is the global
 * sensor index board*ANALOG_SENSORS_PER_BOARD + sensor; decode it back
 * as board = idx/ANALOG_SENSORS_PER_BOARD, sensor = idx%ANALOG_SENSORS_PER_BOARD.
 * Set by SetAnalogBoardTypes() (Analog_Input.c:82-119), consumed by
 * AdjustFansForStaticPressure() (Controls) and SystemFailuresChk()
 * (Failures). This is why SensorConfig.type is persisted — the
 * controller learns each sensor's role from the operator config. */
extern int SENSOR_STATIC_PRESSURE;

/*** external functions ***/

extern unsigned int DiscoverAnalogBoards(void);
extern void DiscoverAnalogBoardsRequest(unsigned int request);

extern void FindAnalogBoards(void);
extern void ReadAnalogBoards(char ReadType);
extern void TempTable_Init(void);

/* Rescan Settings.AnalogBoard[].Sensor[].Type and update the
 * SENSOR_STATIC_PRESSURE role index. Call after any sensor-config
 * change takes effect (the LP shim calls it at the tail of every
 * mirror_sensors pass). 1:1 port of Mini_IO SetAnalogBoardTypes(),
 * scoped to the one role Constellation consumes today. */
extern void SetAnalogBoardTypes(void);

#endif

/***   End Of File ***/
