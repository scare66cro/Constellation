/***************************************************************************
              ALL RIGHTS RESERVED BY INFINETIX CORPORATION
       REPRODUCTION OR USE WITHOUT EXPRESS PERMISSION PROHIBITED

$Header: $

FILE:     PWM.c

AUTHOR:   CBostic

COMPANY:  Infinetix

PURPOSE:  Pulse Width Modulation control

COMMENTS:

***************************************************************************/

/*** include files ***/
#include "PWM.h"
#include "Settings.h"
#include "States.h"

/*** defines ***/

/*** typedefs and structures ***/

/*** module variables ***/

//PWM_VALUES PWMValue;
PWM_INFO PwmChannel[PWM_TOTAL_EQ];

/*** static functions ***/

/***************************************************************************

FUNCTION:   PWM_Init()

PURPOSE:    Initialize the PWM controller

COMMENTS:

***************************************************************************/
void PWM_Init(void)
{
  SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
  SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOG);
  SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOK);
  SysCtlPeripheralEnable(SYSCTL_PERIPH_PWM0);

  // Wait for the PWM0 module to be ready.
  while(!SysCtlPeripheralReady(SYSCTL_PERIPH_PWM0));

  GPIOPinConfigure(GPIO_PF1_M0PWM1);
  GPIOPinConfigure(GPIO_PF2_M0PWM2);
  GPIOPinConfigure(GPIO_PF3_M0PWM3);
  GPIOPinConfigure(GPIO_PG0_M0PWM4);
  GPIOPinConfigure(GPIO_PG1_M0PWM5);
  GPIOPinConfigure(GPIO_PK4_M0PWM6);

  GPIOPinTypePWM(GPIO_PORTF_BASE, GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3);
  GPIOPinTypePWM(GPIO_PORTG_BASE, GPIO_PIN_0 | GPIO_PIN_1);
  GPIOPinTypePWM(GPIO_PORTK_BASE, GPIO_PIN_4);

  // divide the system clock by 32 (120 MHz/32 = 3.75 MHz)
  PWMClockSet(PWM0_BASE, PWM_SYSCLK_DIV_32);

  // configure the PWM generators
  // PWM_GEN_MODE_NO_SYNC - Unsynchronized. The PWM generator and its two output signals are used alone, independent
  //   of other PWM generators.
  // PWM_GEN_MODE_GEN_SYNC_LOCAL - Locally Synchronized. The write value does not affect the logic until the counter
  // reaches the value zero at the end of the PWM cycle. In this case, the effect of the write is deferred, providing
  // a guaranteed defined behavior and preventing overly short or overly long output PWM pulses.
  PWMGenConfigure(PWM0_BASE, PWM_GEN_0, PWM_GEN_MODE_DOWN | PWM_GEN_MODE_NO_SYNC | PWM_GEN_MODE_GEN_SYNC_LOCAL);
  PWMGenConfigure(PWM0_BASE, PWM_GEN_1, PWM_GEN_MODE_DOWN | PWM_GEN_MODE_NO_SYNC | PWM_GEN_MODE_GEN_SYNC_LOCAL);
  PWMGenConfigure(PWM0_BASE, PWM_GEN_2, PWM_GEN_MODE_DOWN | PWM_GEN_MODE_NO_SYNC | PWM_GEN_MODE_GEN_SYNC_LOCAL);
  PWMGenConfigure(PWM0_BASE, PWM_GEN_3, PWM_GEN_MODE_DOWN | PWM_GEN_MODE_NO_SYNC | PWM_GEN_MODE_GEN_SYNC_LOCAL);

  // set the period to generate 10KHz
  // 1/3.75 MHz = 2.67e-7 nsec
  // 1/10KHz = .0001 nsec
  // .0001/2.67e-7 = 375
  PWMGenPeriodSet(PWM0_BASE, PWM_GEN_0, PWM_PERIOD);
  PWMGenPeriodSet(PWM0_BASE, PWM_GEN_1, PWM_PERIOD);
  PWMGenPeriodSet(PWM0_BASE, PWM_GEN_2, PWM_PERIOD);
  PWMGenPeriodSet(PWM0_BASE, PWM_GEN_3, PWM_PERIOD);

  PWMGenEnable(PWM0_BASE, PWM_GEN_0);
  PWMGenEnable(PWM0_BASE, PWM_GEN_1);
  PWMGenEnable(PWM0_BASE, PWM_GEN_2);
  PWMGenEnable(PWM0_BASE, PWM_GEN_3);

  PWMOutputState(PWM0_BASE, PWM_OUT_1_BIT, true);
  PWMOutputState(PWM0_BASE, PWM_OUT_2_BIT, true);
  PWMOutputState(PWM0_BASE, PWM_OUT_3_BIT, true);
  PWMOutputState(PWM0_BASE, PWM_OUT_4_BIT, true);
  PWMOutputState(PWM0_BASE, PWM_OUT_5_BIT, true);
  PWMOutputState(PWM0_BASE, PWM_OUT_6_BIT, true);

  // set the channel output to the minimum
//  PWM_UpdateChannel(0, PWM_MIN_VALUE);
//  PWM_UpdateChannel(1, PWM_MIN_VALUE);
//  PWM_UpdateChannel(2, PWM_MIN_VALUE);
//  PWM_UpdateChannel(3, PWM_MIN_VALUE);
//  PWM_UpdateChannel(4, PWM_MIN_VALUE);
//  PWM_UpdateChannel(5, PWM_MIN_VALUE);
  PWMPulseWidthSet(PWM0_BASE, 0, PWM_MIN_VALUE);
  PWMPulseWidthSet(PWM0_BASE, 1, PWM_MIN_VALUE);
  PWMPulseWidthSet(PWM0_BASE, 2, PWM_MIN_VALUE);
  PWMPulseWidthSet(PWM0_BASE, 3, PWM_MIN_VALUE);
  PWMPulseWidthSet(PWM0_BASE, 4, PWM_MIN_VALUE);
  PWMPulseWidthSet(PWM0_BASE, 5, PWM_MIN_VALUE);

  // initialize the outputs
  int i;
  for (i = 0; i < PWM_TOTAL_EQ; ++i)
  {
    PwmChannel[i].Output = PWM_MIN_VALUE;
  }
//  PWMValue.Fan      = PWM_MIN_VALUE;
//  PWMValue.FreshAir = PWM_MIN_VALUE;
//  PWMValue.Refrig   = PWM_MIN_VALUE;
//  PWMValue.Burner   = PWM_MIN_VALUE;
} // end PWM_Init()

/***************************************************************************

FUNCTION:   PWM_UpdateChannel()

PURPOSE:    Change the duty cycle of a channel

COMMENTS:

***************************************************************************/
//void PWM_UpdateChannel(unsigned int ChannelIndex, unsigned int DutyCycle)
void PWM_UpdateChannel(PWM_EQUIPMENT eqIndex)
{
  // outputs 3 & 4, and 5 & 6 are reversed to make the physical orientation on the expansion boards
  // the same as the main/mini board
  unsigned int PwmOutput[7] = {PWM_OUT_1,PWM_OUT_2,PWM_OUT_4,PWM_OUT_3,PWM_OUT_6,PWM_OUT_5};

  if (Settings.PWM[eqIndex].Enabled)
  {
    if (   Settings.PWM[eqIndex].Channel == PWM_UNDEFINED
        || Settings.PWM[eqIndex].Channel > 5)
    {
  //    SystemAlarm[AL_SYSCONFIG] = FM_FAIL;
      WarningsSet(WARN_SYSCONFIG_EQ, FM_ALARM, NA, Settings.PWM[eqIndex].SysConfigWarnIoIndex);
  //    SystemState = ST_SYSCONFIGFAIL;
    }
    else
    {
      // the output is inverted because the opto-isolators invert the signal
      PWMPulseWidthSet(PWM0_BASE, PwmOutput[Settings.PWM[eqIndex].Channel], PWM_PERIOD - PwmChannel[eqIndex].Output);
    }
  }
} // end PWM_UpdateChannel()

/***   End Of File   ***/
