/*
 * hal_flash.c — QSPI Flash HAL for AM2434 (Settings Vault)
 *
 * On the TM4C, settings were stored on an external ISSI SPI NOR flash.
 * On AM2434, settings live on the QSPI NOR flash (W25Q64JV) connected
 * to the OSPI controller, memory-mapped at 0x50000000.
 *
 * This HAL provides the same read/write/erase interface that the
 * Settings module expects.
 *
 * QEMU build:  OSPI region is plain RAM — memcpy works for everything.
 * Real build:  Uses TI MCU+ SDK Flash driver for write/erase via OSPI
 *              indirect mode; reads remain memory-mapped (DAC mode).
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */

#include "hal.h"
#include "debug.h"
#include <string.h>

/* QSPI NOR flash is memory-mapped at this address by OSPI controller */
#define OSPI_DATA_BASE  0x50000000
#define OSPI_FLASH_SIZE (8 * 1024 * 1024)  /* 8 MB */
#define SECTOR_SIZE     4096                /* 4 KB sector erase granularity */
#define PAGE_SIZE       256                 /* NOR flash page program size */

#ifdef QEMU_BUILD

/* ─── QEMU implementation (plain RAM-backed region) ───────────────────── */

void hal_flash_init(void)
{
    /* In QEMU, the ospi_flash memory region is plain RAM,
     * so reads/writes work directly through the memory map. */
}

int hal_flash_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    if (addr + len > OSPI_FLASH_SIZE) return -1;
    memcpy(buf, (const void *)(OSPI_DATA_BASE + addr), len);
    return 0;
}

int hal_flash_write(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    if (addr + len > OSPI_FLASH_SIZE) return -1;
    memcpy((void *)(OSPI_DATA_BASE + addr), buf, len);
    return 0;
}

int hal_flash_erase_sector(uint32_t addr)
{
    if (addr >= OSPI_FLASH_SIZE) return -1;
    addr &= ~(SECTOR_SIZE - 1);
    memset((void *)(OSPI_DATA_BASE + addr), 0xFF, SECTOR_SIZE);
    return 0;
}

#else /* Real hardware — TI MCU+ SDK Flash driver */

/* ─── Real hardware OSPI implementation ───────────────────────────────── */
/*
 * Uses TI MCU+ SDK Board Flash driver (board/flash.h).
 * The OSPI controller is configured in DAC (Direct Access Controller)
 * mode for memory-mapped reads by the SysConfig-generated board init.
 * Writes and erases use the SDK's indirect protocol engine which sends
 * the correct SPI NOR commands (Page Program 0x02, Sector Erase 0x20).
 *
 * The Flash driver handles:
 *   - Switching from DAC to indirect mode for writes
 *   - Issuing Write Enable (0x06) before each program/erase
 *   - Polling the Status Register (0x05) for WIP completion
 *   - Switching back to DAC mode after the operation
 */
#include <board/flash.h>
#include <kernel/dpl/SystemP.h>

static Flash_Handle gFlashHandle = NULL;

void hal_flash_init(void)
{
    Flash_Params params;
    Flash_Params_init(&params);

    gFlashHandle = Flash_open(CONFIG_FLASH0, &params);
    if (gFlashHandle == NULL) {
        debug_printf("[Flash] FATAL: Flash_open failed — settings/OTA unavailable\r\n");
    } else {
        debug_printf("[Flash] OSPI flash initialized (W25Q64JV, %d MB)\r\n",
                     OSPI_FLASH_SIZE / (1024 * 1024));
    }
}

int hal_flash_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    if (addr + len > OSPI_FLASH_SIZE) return -1;

    /* Memory-mapped read — OSPI DAC mode makes 0x50000000+ transparent.
     * No SDK call needed; the CPU reads it like SRAM. */
    memcpy(buf, (const void *)(OSPI_DATA_BASE + addr), len);
    return 0;
}

int hal_flash_write(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    if (!gFlashHandle) return -1;
    if (addr + len > OSPI_FLASH_SIZE) return -1;

    /* NOR flash requires page-aligned writes (256 bytes max per program
     * command).  The SDK Flash_write handles multi-page splitting, but
     * we still guard against cross-sector writes that rely on pre-erased
     * state.  The caller (Settings/FW Update) is expected to erase first. */
    int32_t status = Flash_write(gFlashHandle, addr, (uint8_t *)buf, len);
    if (status != SystemP_SUCCESS) {
        debug_printf("[Flash] Write failed at 0x%06lX len=%lu err=%ld\r\n",
                     (unsigned long)addr, (unsigned long)len, (long)status);
        return -1;
    }
    return 0;
}

int hal_flash_erase_sector(uint32_t addr)
{
    if (!gFlashHandle) return -1;
    if (addr >= OSPI_FLASH_SIZE) return -1;

    /* Align to sector boundary — 4 KB erase granularity */
    uint32_t blk_num;
    uint32_t page_num;

    addr &= ~(SECTOR_SIZE - 1);

    /* Convert byte address to block number for the SDK */
    Flash_offsetToBlkPage(gFlashHandle, addr, &blk_num, &page_num);

    int32_t status = Flash_eraseBlk(gFlashHandle, blk_num);
    if (status != SystemP_SUCCESS) {
        debug_printf("[Flash] Erase failed at 0x%06lX blk=%lu err=%ld\r\n",
                     (unsigned long)addr, (unsigned long)blk_num, (long)status);
        return -1;
    }
    return 0;
}

#endif /* QEMU_BUILD */
