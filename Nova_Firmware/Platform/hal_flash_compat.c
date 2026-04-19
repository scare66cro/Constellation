/*
 * hal_flash_compat.c — FlashRead/FlashWrite/FlashEraseArea wrappers
 *
 * Application code calls these TM4C flash functions.
 * On AM2434 Nova we route to the HAL QSPI functions.
 * For QEMU, the flash is just a memory region.
 */

#include <string.h>
#include <stdint.h>
#include "hal.h"

/* QSPI flash region mapped at this base address by the QEMU machine model */
#define FLASH_WINDOW_BASE   0x60000000
#define FLASH_WINDOW_SIZE   (2 * 1024 * 1024)  /* 2 MB internal flash */

int FlashInit(void)
{
    hal_flash_init();
    return 0;
}

void FlashEraseArea(unsigned int addr, unsigned int length)
{
    /* In QEMU, flash is just RAM — zero the area */
    if (addr < FLASH_WINDOW_SIZE) {
        unsigned int end = addr + length;
        if (end > FLASH_WINDOW_SIZE) end = FLASH_WINDOW_SIZE;
        memset((void *)(FLASH_WINDOW_BASE + addr), 0xFF, end - addr);
    }
}

int FlashWrite(unsigned int to, unsigned char *from, unsigned int length)
{
    if (to < FLASH_WINDOW_SIZE) {
        unsigned int safe_len = length;
        if (to + safe_len > FLASH_WINDOW_SIZE) safe_len = FLASH_WINDOW_SIZE - to;
        memcpy((void *)(FLASH_WINDOW_BASE + to), from, safe_len);
    }
    return 0;
}

unsigned int FlashRead(unsigned char *to, unsigned int from, unsigned int length)
{
    if (from < FLASH_WINDOW_SIZE) {
        unsigned int safe_len = length;
        if (from + safe_len > FLASH_WINDOW_SIZE) safe_len = FLASH_WINDOW_SIZE - from;
        memcpy(to, (void *)(FLASH_WINDOW_BASE + from), safe_len);
        return safe_len;
    }
    return 0;
}
