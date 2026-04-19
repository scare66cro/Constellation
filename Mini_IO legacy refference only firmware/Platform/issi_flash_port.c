/*
 * issi_flash_port.c
 *
 *  Created on: Nov 22, 2017
 *      Author: Carey
 */

#include <stdint.h>
#include <stdbool.h>

#include "inc/hw_ssi.h"
#include "inc/hw_sysctl.h"

#include "driverlib/ssi.h"
#include "driverlib/rom.h"
#include "driverlib/rom_map.h"
#include "driverlib/sysctl.h"

#include "system.h"

#include "issi_flash.h"
#include "issi_flash_port.h"

#include "pinout.h"
#include "debug.h"

/**
 * This port function should clock out the provided byte value,
 * and return the clocked in byte, be it 0x00 or 0xff. Timing
 * considerations are up to the port provided code.
 */
uint8_t issi_flash_port_transfer(uint8_t data)
{
  uint32_t ret;

  MAP_SSIDataPut(SD_BASE, data);

  issi_flash_port_wait_ready();

  MAP_SSIDataGet(SD_BASE, &ret);

  return ret & 0xff;
}

/**
 * This port function should flush the TX and RX FIFOs if applicable.
 */
void issi_flash_port_flush(void)
{
  uint32_t b;

  while (MAP_SSIDataGetNonBlocking(SD_BASE, &b))
  {

  }

  while (MAP_SSIBusy(SD_BASE))
  {

  }
}

/**
 * This port function will assert the chip-select line for the flash part.
 */
void issi_flash_port_cs_assert(void)
{
  set_output(FLASH_CS, 0);
}

/**
 * This port function will release the chip-select line for the flash part.
 */
void issi_flash_port_cs_release(void)
{
  set_output(FLASH_CS, 1);
}

/**
 * This port function will claim a mutex protecting the SPI bus, if
 * applicable.
 */
void issi_flash_port_claim_mutex(void)
{
  spi_lock(1);
}

/**
 * This port function will release a mutex protecting the SPI bus, if
 * applicable.
 */
void issi_flash_port_release_mutex(void)
{
  spi_unlock();
}

/**
 * This port function will wait until the SSI peripheral is ready for
 * more data, or do nothing.
 */
void issi_flash_port_wait_ready(void)
{
  while (MAP_SSIBusy(SD_BASE)) {
      ; // Do nothing
  }
}

/**
 * This port function will wait the provided number of milliseconds.
 */
void issi_flash_port_delay_ms(uint32_t delay_ms)
{
  SysCtlDelay((system_clock_speed / (3*1000)) * delay_ms);
}

/**
 * This port function will print a formatted string with arguments,
 * similar to the UNIX printf function.
 */
void issi_flash_port_debug_printf(const char *string, ...)
{
  va_list vaArgP;

  va_start(vaArgP, string);
  debug_printf(string, vaArgP);
  va_end(vaArgP);
}
