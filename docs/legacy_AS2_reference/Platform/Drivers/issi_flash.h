/*
 * issi_flash.h
 *
 *  Created on: Jun 6, 2017
 *      Author: jdscott
 */

#ifndef ISSI_FLASH_H_
#define ISSI_FLASH_H_

#include <stdint.h>
#include <stdbool.h>

int8_t issi_flash_init(void);
int32_t issi_flash_erase(uint32_t from, uint32_t len);
uint32_t issi_flash_read(uint8_t *buffer, uint32_t from, uint32_t len);
int32_t issi_flash_write(const uint8_t *buffer, uint32_t address, uint32_t length);

#endif /* ISSI_FLASH_H_ */
