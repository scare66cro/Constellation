/*=============================================================================
Copyright 2017 Infinetix Corp.

This file is the property of Infinetix Corp and shall not be
reproduced, copied, or used as the basis for the manufacture or sale
of equipment without the express written permission of Infinetix Corp.

FILE NAME: issi_flash.c

PURPOSE:
    Provides a library for reading to and writing from the IS25LP128-JBLE

NOTES:


CHANGE HISTORY:
    Revision: 1.0
    User: JD Scott
    Date: 05/24/17
    Details:
        Initial work.

=============================================================================*/

#include "issi_flash.h"
#include "issi_flash_port.h"
#include "debug.h"

/* Static functions */

static uint32_t issi_get_id(void);
static void issi_enable_write(void);
static uint8_t issi_get_status(void);
static int32_t issi_poll_ready(void);
static int32_t issi_erase_blocks(uint32_t from, uint32_t count);
static int32_t issi_erase_sectors(uint32_t from, uint32_t count);
void issi_pad(uint8_t len_bytes);

/* END Static functions */



/* Flash Commands */

#define FLASH_WRITE_ENABLE      0x06
#define FLASH_READ_STATUS       0x05
#define FLASH_ID                0x9F
#define FLASH_NORMAL_READ       0x03
#define FLASH_FAST_READ         0x0B
#define FLASH_PROGRAM_PAGE      0x02
#define FLASH_ERASE_SECTOR      0xD7
#define FLASH_ERASE_BLOCK       0xD8

#define FLASH_BUSY              (1 << 0)

#define MAX_ERASE_TIME_MS       360

/* END Flash Commands */

#define FLASH_DEVICE_ID         0x9D6018
#define FLASH_BLOCK_SIZE        (64 * 1024UL)
#define FLASH_SECTOR_SIZE       (4 * 1024UL)
#define FLASH_PAGE_SIZE         256UL


int8_t issi_flash_init(void) {
    uint64_t id;

    issi_flash_port_claim_mutex();
    issi_flash_port_flush();

    id = issi_get_id();

    if (FLASH_DEVICE_ID != id) {
        debug_printf("Flash ID %08X is not ISSI.\r\n", id);
//        issi_flash_port_debug_printf("Flash ID %08X is not ISSI.\r\n", id);
        issi_flash_port_release_mutex();

        return (-1);
    }

    debug_printf("Flash ID %08X is ISSI.\r\n", id);
//    issi_flash_port_debug_printf("Flash ID %08X is ISSI.\r\n", id);
    issi_flash_port_release_mutex();

    return 0;
}

uint32_t issi_flash_read(uint8_t *buffer, uint32_t from, uint32_t len) {
    uint32_t i;

    if (0 == len) {
        return 0;
    }

    issi_flash_port_claim_mutex();

    if (0 == issi_poll_ready()) {
        issi_flash_port_release_mutex();

        return 0;
    }

    issi_flash_port_cs_assert();

    issi_flash_port_transfer(FLASH_FAST_READ);
    issi_flash_port_transfer(0x7F & (from >> 16));
    issi_flash_port_transfer(0xFF & (from >> 8));
    issi_flash_port_transfer(0xFF & from);
    issi_pad(1);

    for (i = 0; i < len; i++) {
        *(buffer + i) = issi_flash_port_transfer(0x00);
    }

    issi_flash_port_cs_release();
    issi_flash_port_release_mutex();

    return i;
}

/**
 * This function assumes erase operations begin on a SECTOR boundary.
 */
int32_t issi_flash_erase(uint32_t from, uint32_t len) {
    uint32_t block_count = 0;
    uint32_t sector_count = 0;
    uint32_t address;

    if (0 == len) {
        return 0;
    }

    if (FLASH_BLOCK_SIZE <= len) {
        // Determine if the start is on a block boundary
        // Get the start address
        address = (from / FLASH_BLOCK_SIZE) * FLASH_BLOCK_SIZE;

        // If the black boundary address is before our starting address, this is not on a block boundary.
        if (address < from) {
            // get the address up to the next block
            address += FLASH_BLOCK_SIZE;

            // determine the number of sectors between the start address and the next block boundary
            sector_count = (address - from) / FLASH_SECTOR_SIZE;

            // erase those sectors
            if (0 != issi_erase_sectors(from, sector_count)) {
                return 1;
            }

            // adjust our length
            len -= sector_count * FLASH_SECTOR_SIZE;
            // and adjust the starting address to the next block
            from = address;
        }

        // Determine if still have any blocks to erase
        if (FLASH_BLOCK_SIZE <= len) {
            block_count = len / FLASH_BLOCK_SIZE;

            if (0 != issi_erase_blocks(from, block_count)) {
                return 1;
            }

            len -= block_count * FLASH_BLOCK_SIZE;
            from += block_count * FLASH_BLOCK_SIZE;
        }
    }

    // Get the start address
    address = (from / FLASH_SECTOR_SIZE) * FLASH_SECTOR_SIZE;

    sector_count = len / FLASH_SECTOR_SIZE;

    if (0 < (len % FLASH_SECTOR_SIZE)) {
        sector_count++;
    }

    return issi_erase_sectors(from, sector_count);
}

int32_t issi_erase_blocks(uint32_t from, uint32_t count) {
    uint32_t address;

    if (0 == count) {
        return 0;
    }

    address = from - (from % FLASH_BLOCK_SIZE);

    do {
        // Enable flash
        issi_enable_write();

        issi_flash_port_cs_assert();

        // Send command and address
        issi_flash_port_transfer(FLASH_ERASE_BLOCK);
        issi_flash_port_transfer(0x7F & (address >> 16));
        issi_flash_port_transfer(0xFF & (address >> 8));
        issi_flash_port_transfer(0xFF & address);

        issi_flash_port_cs_release();

        // Wait for the chip to complete
        if (0 == issi_poll_ready()) {
            return -1;
        }

        // Increment the address
        address += FLASH_BLOCK_SIZE;

        // Decrement the unit count
        count -= 1;

    } while (0 < count); // Do all this until we've erased all the units

    return 0;
}

int32_t issi_erase_sectors(uint32_t from, uint32_t count) {
    uint32_t address;

    // If there are no units to delete, return
    if (0 == count) {
        return 0;
    }

    // Get the starting address of the unit
    address = from - (from % FLASH_SECTOR_SIZE);

    do {
        // Enable flash
        issi_enable_write();

        issi_flash_port_cs_assert();

        // Send command and address
        issi_flash_port_transfer(FLASH_ERASE_SECTOR);
        issi_flash_port_transfer(0x7F & (address >> 16));
        issi_flash_port_transfer(0xFF & (address >> 8));
        issi_flash_port_transfer(0xFF & address);

        issi_flash_port_cs_release();

        // Wait for the chip to complete
        if (0 == issi_poll_ready()) {
            return -1;
        }

        // Increment the address
        address += FLASH_SECTOR_SIZE;

        // Decrement the unit count
        count -= 1;

    } while (0 < count); // Do all this until we've erased all the units

    return 0;
}

int32_t issi_flash_write(const uint8_t *buffer, uint32_t address, uint32_t length) {
    uint32_t i;
    uint32_t page_index;
    uint8_t first_page = 1;

    // Get the initial index in the page
    page_index = address % FLASH_PAGE_SIZE;

    issi_flash_port_claim_mutex();

    for (i = 0; i < length; i++, page_index++) {
        // If we've hit a page boundary...
        if (0 != first_page || 0 == (page_index % FLASH_PAGE_SIZE)) {
            first_page = 0;

            // ...Release the chip-select
            issi_flash_port_cs_release();                                       // 1 CS, 0 bytes

            // ...wait for the chip to finish the page program operation
            // If the flash never becomes available, return an error
            if (0 == issi_poll_ready()) {                                       // 2 CS, 2 bytes
                issi_flash_port_release_mutex();

                return -1;
            }

            // ...Enable write for the next page
            issi_enable_write();                                                // 2 CS, 1 byte

            issi_flash_port_cs_assert();                                        // 1 CS, 0 bytes

            // Set up the next page write
            issi_flash_port_transfer(FLASH_PROGRAM_PAGE);
            issi_flash_port_transfer(0x7F & ((address + i) >> 16));
            issi_flash_port_transfer(0xFF & ((address + i) >> 8));
            issi_flash_port_transfer(0xFF & (address + i));                     // 0 CS, 4 bytes
        }

        // Write the data to the given page
        issi_flash_port_transfer(*(buffer + i));                                // 0 CS, length bytes
    }

    issi_flash_port_cs_release();                                               // 1 CS, 0 bytes

    // If the last page fails to complete, return an error
    if (0 == issi_poll_ready()) {
        issi_flash_port_release_mutex();

        return -1;
    }

    issi_flash_port_release_mutex();

    return 0;
}

uint32_t issi_get_id(void) {
    uint64_t id = 0;

    issi_flash_port_cs_assert();
    issi_flash_port_transfer(FLASH_ID);

    id = (0xFF & issi_flash_port_transfer(0)) << 16;
    id |= (0xFF & issi_flash_port_transfer(0)) << 8;
    id |= (0xFF & issi_flash_port_transfer(0));

    issi_flash_port_wait_ready();
    issi_flash_port_cs_release();

    return id;
}

void issi_enable_write(void) {
    issi_flash_port_cs_assert();
    issi_flash_port_transfer(FLASH_WRITE_ENABLE);
    issi_flash_port_wait_ready();
    issi_flash_port_cs_release();
}

uint8_t issi_get_status(void) {
    uint8_t ret;

    issi_flash_port_cs_assert();
    issi_flash_port_transfer(FLASH_READ_STATUS);
    ret = issi_flash_port_transfer(0x00);
    issi_flash_port_wait_ready();
    issi_flash_port_cs_release();

    return ret;
}

int32_t issi_poll_ready(void) {
    uint32_t i;

    for (i = 0; i < MAX_ERASE_TIME_MS; i++) {
        if (FLASH_BUSY != (FLASH_BUSY & issi_get_status())) {
            return 1;
        }

        issi_flash_port_delay_ms(1);
    }

    return 0;
}

void issi_pad(uint8_t len_bytes) {
    while (len_bytes-- > 0) {
        issi_flash_port_transfer(0);
    }

    issi_flash_port_wait_ready();
}
