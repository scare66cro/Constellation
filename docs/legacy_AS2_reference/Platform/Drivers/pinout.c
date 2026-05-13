//*****************************************************************************
//
// pinout.c - Function to configure the device pins on the EK-TM4C1294XL.
//
// Copyright (c) 2013-2014 Texas Instruments Incorporated.  All rights reserved.
// Software License Agreement
// 
// Texas Instruments (TI) is supplying this software for use solely and
// exclusively on TI's microcontroller products. The software is owned by
// TI and/or its suppliers, and is protected under applicable copyright
// laws. You may not combine this software with "viral" open-source
// software in order to form a larger program.
// 
// THIS SOFTWARE IS PROVIDED "AS IS" AND WITH ALL FAULTS.
// NO WARRANTIES, WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING, BUT
// NOT LIMITED TO, IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE APPLY TO THIS SOFTWARE. TI SHALL NOT, UNDER ANY
// CIRCUMSTANCES, BE LIABLE FOR SPECIAL, INCIDENTAL, OR CONSEQUENTIAL
// DAMAGES, FOR ANY REASON WHATSOEVER.
// 
// This is part of revision 2.1.0.12573 of the EK-TM4C1294XL Firmware Package.
//
//*****************************************************************************


#include <stdint.h>

#include "drivers/pinout.h"

#include "inc/hw_gpio.h"
#include "inc/hw_memmap.h"
#include "inc/hw_types.h"
#include "driverlib/gpio.h"
#include "driverlib/pin_map.h"
#include "driverlib/rom.h"
#include "driverlib/rom_map.h"
#include "driverlib/adc.h"
#include "driverlib/ssi.h"
#include "driverlib/i2c.h"
#include "driverlib/uart.h"


#include "system.h"
#include "debug.h"

#include "FreeRTOS.h"
#include "semphr.h"

static xSemaphoreHandle spi_semaphore;

int spi_lock(unsigned char block)
{
	if (xSemaphoreTake(spi_semaphore, block?portMAX_DELAY:0)==pdTRUE) return 1;
	return 0;
}

void spi_unlock(void)
{
	xSemaphoreGive(spi_semaphore);
}

void set_output(_pin_str pin, unsigned int state)
{
	GPIOPinWrite(pin.port, pin.pin, state?pin.pin:0);
}

unsigned int read_input(_pin_str pin)
{
	return GPIOPinRead(pin.port, pin.pin)?1:0;
}

void configure_output_pin(_pin_str pin)
{
	SysCtlPeripheralEnable(pin.periph);
	MAP_GPIOPadConfigSet(pin.port, pin.pin, pin.strength, pin.type);
	ROM_GPIODirModeSet(pin.port, pin.pin, GPIO_DIR_MODE_OUT);
	ROM_GPIOPinWrite(pin.port, pin.pin, pin.initial_state?pin.pin:0);
}

static void configure_input_pin(_pin_str pin)
{
	SysCtlPeripheralEnable(pin.periph);
	GPIOPinTypeGPIOInput(pin.port, pin.pin );
	MAP_GPIOPadConfigSet(pin.port, pin.pin, pin.strength, pin.type);
	ROM_GPIOPinWrite(pin.port, pin.pin, pin.initial_state?pin.pin:0);
}

void CreateSPILocks(void)
{
	// Create SPI Semaphore
	if ((spi_semaphore = xSemaphoreCreateBinary())==NULL)
	{
		debug_printf("\r\n");
		debug_printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\r\n");
		debug_printf("!!Failed to create spi_semaphore!!\r\n");
		debug_printf("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\r\n");
		debug_printf("\r\n");
	}
	else
	{
		xSemaphoreGive(spi_semaphore);
	}
}

void PinoutSet(void)
{
	unsigned int ulCharRx;

	// I2C0
	SysCtlPeripheralEnable(SYSCTL_PERIPH_I2C0);

	// Wait for the I2C module to be ready.
  while(!SysCtlPeripheralReady(SYSCTL_PERIPH_I2C0));

  SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);
	GPIOPinConfigure(GPIO_PB2_I2C0SCL);
	GPIOPinConfigure(GPIO_PB3_I2C0SDA);
	GPIOPinTypeI2CSCL(GPIO_PORTB_BASE, GPIO_PIN_2);
	GPIOPinTypeI2C(GPIO_PORTB_BASE, GPIO_PIN_3);
	I2CMasterInitExpClk(RTC_I2C_BASE, system_clock_speed, 1);

	// UART0
	ROM_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
	ROM_GPIOPinConfigure(GPIO_PA0_U0RX);
	ROM_GPIOPinConfigure(GPIO_PA1_U0TX);
	ROM_GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);

	// LTX_UART used for UART1.
	SysCtlPeripheralEnable(SYSCTL_PERIPH_UART1);
	ROM_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);
	ROM_GPIOPinConfigure(GPIO_PB0_U1RX);
	ROM_GPIOPinConfigure(GPIO_PB1_U1TX);
	ROM_GPIOPinTypeUART(GPIO_PORTB_BASE, GPIO_PIN_0 | GPIO_PIN_1);
	UARTConfigSetExpClk(LTX_UART, system_clock_speed, 230400, UART_CONFIG_WLEN_8 | UART_CONFIG_PAR_NONE | UART_CONFIG_STOP_ONE);
//	configure_output_pin(LTX_RX);
//	configure_input_pin(LTX_TX);

	// RS485 to analog boards
  SysCtlPeripheralEnable(SYSCTL_PERIPH_UART2);
//  ROM_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
  ROM_GPIOPinConfigure(GPIO_PA6_U2RX);
  ROM_GPIOPinConfigure(GPIO_PA7_U2TX);
  ROM_GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_6 | GPIO_PIN_7);
  UARTConfigSetExpClk(AUX_UART, system_clock_speed, 9600, UART_CONFIG_WLEN_8 | UART_CONFIG_PAR_NONE | UART_CONFIG_STOP_ONE);
  UARTFIFOLevelSet(AUX_UART, UART_FIFO_TX4_8, UART_FIFO_RX4_8);
  configure_output_pin(AUX_DIR);

	// Watchdog / Heartbeat
  configure_output_pin(WD_CPLD);

  // shift registers
  configure_input_pin(SS_MISO);
  configure_output_pin(SS_MOSI);
  configure_output_pin(SS_SCLK);
  configure_output_pin(SS_RCLK);
  configure_output_pin(SS_CS0);
  configure_output_pin(SS_CS1);
  configure_output_pin(SS_CS2);

  // PWM
//  SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
//	SysCtlPeripheralEnable(SYSCTL_PERIPH_PWM0);
//
//	// Wait for the PWM0 module to be ready.
//	while(!SysCtlPeripheralReady(SYSCTL_PERIPH_PWM0));
//
//  GPIOPinConfigure(GPIO_PF1_M0PWM1);
//  GPIOPinConfigure(GPIO_PF2_M0PWM2);
//  GPIOPinTypePWM(GPIO_PORTF_BASE, GPIO_PIN_1 | GPIO_PIN_2);

	// SPI0
	ROM_SysCtlPeripheralEnable(SYSCTL_PERIPH_SSI0);
	ROM_GPIOPinConfigure(GPIO_PA2_SSI0CLK);
	ROM_GPIOPinConfigure(GPIO_PA4_SSI0XDAT0);
	ROM_GPIOPinConfigure(GPIO_PA5_SSI0XDAT1);
	ROM_GPIOPinTypeSSI(GPIO_PORTA_BASE, GPIO_PIN_5 | GPIO_PIN_4 | GPIO_PIN_2);
	SSIConfigSetExpClk(SSI0_BASE, system_clock_speed, SSI_FRF_MOTO_MODE_0,SSI_MODE_MASTER, 25000000, 8);
	SSIEnable(SSI0_BASE);
	while(SSIDataGetNonBlocking(SSI0_BASE, &ulCharRx)){}
	configure_output_pin(FLASH_CS);

  ROM_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
  GPIOPinConfigure(GPIO_PIN_2);
	configure_output_pin(SD_CS);

	ROM_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOL);
  GPIOPinConfigure(GPIO_PIN_2);
//  configure_input_pin(SD_SW);

	// Net
	ROM_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
	ROM_GPIOPinConfigure(GPIO_PF0_EN0LED0);
	ROM_GPIOPinConfigure(GPIO_PF4_EN0LED1);
	GPIOPinTypeEthernetLED(GPIO_PORTF_BASE, GPIO_PIN_0 | GPIO_PIN_4);



	// Analog inputs
	SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC0);
	ADCSequenceConfigure(ADC0_BASE, 0, ADC_TRIGGER_PROCESSOR, 0);
	ADCSequenceStepConfigure(ADC0_BASE, 0, 0, ADC_CTL_CH6);
	ADCSequenceStepConfigure(ADC0_BASE, 0, 1, ADC_CTL_CH7 | ADC_CTL_IE| ADC_CTL_END);
	ADCSequenceEnable(ADC0_BASE, 0);


	// Whats

	//configure_output_pin(AES_MUX);
	SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0);
	SysCtlPeripheralEnable(AES_MUX.periph);
	GPIOPinConfigure(GPIO_PL4_T0CCP0);
	SysCtlPeripheralEnable(AES_MUX.periph);
	GPIOPinTypeTimer(AES_MUX.port, AES_MUX.pin);
	TimerConfigure(AES_MUX_TIMER_BASE, TIMER_CFG_A_CAP_COUNT_UP);
	TimerControlEvent(AES_MUX_TIMER_BASE, TIMER_A, TIMER_EVENT_POS_EDGE);
	TimerEnable(AES_MUX_TIMER_BASE, TIMER_A);

	configure_input_pin(AES_ERROR);
	configure_input_pin(DEFAULT);

	// SPI2 Shifters
	/*ROM_SysCtlPeripheralEnable(SYSCTL_PERIPH_SSI2);
	ROM_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOD);
	ROM_GPIOPinConfigure(GPIO_PD3_SSI2CLK);
	ROM_GPIOPinConfigure(GPIO_PD1_SSI2XDAT0);
	ROM_GPIOPinConfigure(GPIO_PD0_SSI2XDAT1);
	ROM_GPIOPinTypeSSI(GPIO_PORTD_BASE, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_3);
	SSIConfigSetExpClk(SSI2_BASE, SysCtlClockGet(), SSI_FRF_MOTO_MODE_0, SSI_MODE_MASTER, 1000, 8);
	SSIEnable(SSI2_BASE);
	while(SSIDataGetNonBlocking(SSI2_BASE, &ulCharRx)){}*/
	configure_output_pin(SDO);
	configure_output_pin(SCK);
	configure_input_pin(SDI);
	configure_output_pin(CS_595);
	configure_output_pin(CS_597);

	// SPI1 Codec
//	SysCtlPeripheralEnable(SYSCTL_PERIPH_SSI1);
//	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
//	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);
//	GPIOPinConfigure(GPIO_PB5_SSI1CLK);
//	GPIOPinConfigure(GPIO_PE4_SSI1XDAT0);
//	GPIOPinConfigure(GPIO_PE5_SSI1XDAT1);
//	GPIOPinTypeSSI(GPIO_PORTE_BASE, GPIO_PIN_4 | GPIO_PIN_5);
//	GPIOPinTypeSSI(GPIO_PORTB_BASE, GPIO_PIN_5);
//
//	MAP_GPIOPadConfigSet(SCLK.port, SCLK.pin, SCLK.strength, SCLK.type);
//	MAP_GPIOPadConfigSet(SO.port, SO.pin, SO.strength, SO.type);
//
//	SSIConfigSetExpClk(SSI1_BASE, SysCtlClockGet(), SSI_FRF_MOTO_MODE_0,  SSI_MODE_MASTER, (700000), 8);
//	SSIEnable(SSI1_BASE);
//	while(SSIDataGetNonBlocking(SSI1_BASE, &ulCharRx)){}
//
//	configure_input_pin(DREQ);
//	configure_output_pin(XCS);
//	configure_output_pin(XDCS);
//	configure_output_pin(VLSI_RST);


}

//*****************************************************************************
//
// END GPIO PIN SETUP.
//
//*****************************************************************************
