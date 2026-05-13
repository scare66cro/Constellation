/*
 * issi_flash_port.h
 *
 *  Created on: Jun 6, 2017
 *      Author: jdscott
 */

#ifndef ISSI_FLASH_PORT_H_
#define ISSI_FLASH_PORT_H_

#include <stdint.h>
#include <stdbool.h>

/**
 * This port function should clock out the provided byte value,
 * and return the clocked in byte, be it 0x00 or 0xff. Timing
 * considerations are up to the port provided code.
 */
uint8_t issi_flash_port_transfer(uint8_t data);

/**
 * This port function should flush the TX and RX FIFOs if applicable.
 */
void issi_flash_port_flush(void);

/**
 * This port function will assert the chip-select line for the flash part.
 */
void issi_flash_port_cs_assert(void);

/**
 * This port function will release the chip-select line for the flash part.
 */
void issi_flash_port_cs_release(void);

/**
 * This port function will claim a mutex protecting the SPI bus, if
 * applicable.
 */
void issi_flash_port_claim_mutex(void);

/**
 * This port function will release a mutex protecting the SPI bus, if
 * applicable.
 */
void issi_flash_port_release_mutex(void);

/**
 * This port function will wait until the SSI peripheral is ready for
 * more data, or do nothing.
 */
void issi_flash_port_wait_ready(void);

/**
 * This port function will wait the provided number of milliseconds.
 */
void issi_flash_port_delay_ms(uint32_t delay_ms);

/**
 * This port function will print a formatted string with arguments,
 * similar to the UNIX printf function.
 */
void issi_flash_port_debug_printf(const char *string, ...);

#endif /* ISSI_FLASH_PORT_H_ */
