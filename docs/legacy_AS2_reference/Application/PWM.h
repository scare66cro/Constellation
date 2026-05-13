/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     PWM.h

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:

COMMENTS:

***************************************************************************/

#ifndef PWM_H
#define PWM_H

/*** include files ***/

#include <stdbool.h>
#include <stdint.h>

#include "inc/hw_memmap.h"
#include "driverlib/gpio.h"
#include "driverlib/pin_map.h"
#include "driverlib/pwm.h"
#include "driverlib/sysctl.h"

// Gellert
#include "SerialShift.h"
#include "Timer.h"
#include "Warnings.h"

/*** defines ***/

#define PWM_PERIOD          (375)
#define PWM_INC_VALUE       (10)
#define PWM_MAX_VALUE       (277)
#define PWM_MIN_VALUE       (55)
#define PWM_RANGE           (PWM_MAX_VALUE - PWM_MIN_VALUE)
#define PWM_UNDEFINED       (255)

/*** typedefs and structures ***/

typedef enum            // NOTE: these values are used by the http server to send translated descriptions
{                       // to the ARM at startup, any changes here must be reflected in LtxWarnings.h
  PWM_DOORS,
  PWM_REFRIGERATION,
  PWM_FAN,
  PWM_BURNER,
  PWM_TOTAL_EQ
} PWM_EQUIPMENT;

typedef struct
{
  char Name[LOG_LABELS];
  unsigned char Enabled;
  unsigned char Channel;
  SYSTEM_MODE Mode;
  EQUIPMENT_IO SysConfigWarnIoIndex;
}PWM_CONFIG;

typedef struct
{
  unsigned int Output;
  WARNING_ITEMS Warning;
  EQUIP_ALARMS Alarm;
} PWM_INFO;

/*** external variables ***/

extern PWM_INFO PwmChannel[PWM_TOTAL_EQ];

/*** external functions ***/

extern void PWM_Init(void);
extern void PWM_UpdateChannel(PWM_EQUIPMENT eqIndex);
//extern void PWM_UpdateChannel(unsigned int channel, unsigned int mod);

#endif

/***   End Of File   ***/
