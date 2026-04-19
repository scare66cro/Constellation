/*
 * nova_fw_update.c — Firmware update manager for Nova (AM2434)
 *
 * Implements dual-bank flash management, chunk reception, CRC verification,
 * and bank activation. Works with OSPI flash via hal_flash.c.
 *
 * CRC-32 uses the standard Ethernet polynomial (0xEDB88320) for
 * compatibility with zlib/crc32 on the bridge side.
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */

#include "nova_fw_update.h"
#include "hal.h"
#include <string.h>

/* ─── CRC-32 (Ethernet/zlib polynomial) ───────────────────────────────── */

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len)
{
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & (-(crc & 1)));
        }
    }
    return ~crc;
}

static uint32_t crc32_buf(const uint8_t *data, size_t len)
{
    return crc32_update(0, data, len);
}

/* ─── Static state ────────────────────────────────────────────────────── */

static FwUpdateState s_state = FW_STATE_IDLE;
static uint32_t s_active_bank = 0;        /* 0=A, 1=B */
static uint32_t s_target_bank = 0;        /* Bank being written to */
static uint32_t s_total_size = 0;
static uint32_t s_expected_crc = 0;
static uint32_t s_bytes_written = 0;
static uint32_t s_chunk_size = 1024;
static char     s_staged_version[32];

static FwBankHeader s_bank_a_hdr;
static FwBankHeader s_bank_b_hdr;
static FwBootMeta   s_boot_meta;

/* ─── Flash helpers ───────────────────────────────────────────────────── */

static uint32_t bank_flash_offset(uint32_t bank)
{
    return (bank == 0) ? FW_BANK_A_OFFSET : FW_BANK_B_OFFSET;
}

static void read_bank_header(uint32_t bank, FwBankHeader *hdr)
{
    uint32_t off = (bank == 0) ? FW_BANK_A_HDR_OFFSET : FW_BANK_B_HDR_OFFSET;
    hal_flash_read(FW_HEADER_OFFSET + off, (uint8_t *)hdr, sizeof(FwBankHeader));
}

static void write_bank_header(uint32_t bank, const FwBankHeader *hdr)
{
    uint32_t off = (bank == 0) ? FW_BANK_A_HDR_OFFSET : FW_BANK_B_HDR_OFFSET;
    /* Header sector is shared — read all, modify one entry, rewrite sector */
    uint8_t sector[FW_SECTOR_SIZE];
    hal_flash_read(FW_HEADER_OFFSET, sector, sizeof(sector));
    memcpy(sector + off, hdr, sizeof(FwBankHeader));
    hal_flash_erase_sector(FW_HEADER_OFFSET);
    hal_flash_write(FW_HEADER_OFFSET, sector, sizeof(sector));
}

static void read_boot_meta(FwBootMeta *meta)
{
    hal_flash_read(FW_HEADER_OFFSET + FW_BOOT_META_OFFSET,
                   (uint8_t *)meta, sizeof(FwBootMeta));
}

static void write_boot_meta(const FwBootMeta *meta)
{
    uint8_t sector[FW_SECTOR_SIZE];
    hal_flash_read(FW_HEADER_OFFSET, sector, sizeof(sector));
    memcpy(sector + FW_BOOT_META_OFFSET, meta, sizeof(FwBootMeta));
    hal_flash_erase_sector(FW_HEADER_OFFSET);
    hal_flash_write(FW_HEADER_OFFSET, sector, sizeof(sector));
}

static int erase_bank(uint32_t bank)
{
    uint32_t base = bank_flash_offset(bank);
    /* Erase in 4 KB sectors.  FW_BANK_MAX_SIZE / 4096 = ~496 sectors */
    for (uint32_t off = 0; off < FW_BANK_MAX_SIZE; off += FW_SECTOR_SIZE) {
        if (hal_flash_erase_sector(base + off) != 0)
            return -1;
    }
    return 0;
}

/* ─── Initialization ──────────────────────────────────────────────────── */

void NovaFwUpdate_Init(void)
{
    read_bank_header(0, &s_bank_a_hdr);
    read_bank_header(1, &s_bank_b_hdr);
    read_boot_meta(&s_boot_meta);

    /* Determine active bank from headers */
    bool a_valid = (s_bank_a_hdr.magic == FW_BANK_MAGIC && s_bank_a_hdr.valid);
    bool b_valid = (s_bank_b_hdr.magic == FW_BANK_MAGIC && s_bank_b_hdr.valid);

    if (a_valid && b_valid) {
        /* Both valid — pick the one with higher sequence number */
        if (s_bank_b_hdr.sequence > s_bank_a_hdr.sequence)
            s_active_bank = 1;
        else
            s_active_bank = 0;
    } else if (b_valid) {
        s_active_bank = 1;
    } else {
        s_active_bank = 0;  /* Default to A, or golden fallback */
    }

    s_state = FW_STATE_IDLE;
}

/* ─── Getters ─────────────────────────────────────────────────────────── */

uint32_t NovaFwUpdate_GetActiveBank(void)    { return s_active_bank; }
uint32_t NovaFwUpdate_GetInactiveBank(void)  { return s_active_bank ? 0 : 1; }
FwUpdateState NovaFwUpdate_GetState(void)    { return s_state; }
uint32_t NovaFwUpdate_GetBytesWritten(void)  { return s_bytes_written; }
uint32_t NovaFwUpdate_GetTotalSize(void)     { return s_total_size; }

void NovaFwUpdate_GetBankHeader(uint32_t bank, FwBankHeader *hdr)
{
    if (bank == 0) *hdr = s_bank_a_hdr;
    else           *hdr = s_bank_b_hdr;
}

void NovaFwUpdate_GetBootMeta(FwBootMeta *meta)
{
    *meta = s_boot_meta;
}

/* ─── Begin update ────────────────────────────────────────────────────── */

uint32_t NovaFwUpdate_Begin(uint32_t total_size, uint32_t crc32,
                             const char *version, uint32_t chunk_size)
{
    if (s_state != FW_STATE_IDLE && s_state != FW_STATE_ERROR)
        return 1;  /* Already in progress */

    if (total_size == 0 || total_size > FW_BANK_MAX_SIZE)
        return 2;  /* Image too large or zero */

    s_total_size = total_size;
    s_expected_crc = crc32;
    s_bytes_written = 0;
    s_chunk_size = (chunk_size > 0 && chunk_size <= 4096) ? chunk_size : 1024;
    s_target_bank = NovaFwUpdate_GetInactiveBank();

    strncpy(s_staged_version, version ? version : "", sizeof(s_staged_version) - 1);
    s_staged_version[sizeof(s_staged_version) - 1] = '\0';

    /* Erase the target bank */
    s_state = FW_STATE_ERASING;
    if (erase_bank(s_target_bank) != 0) {
        s_state = FW_STATE_ERROR;
        return 3;  /* Erase failed */
    }

    s_state = FW_STATE_RECEIVING;
    return 0;
}

/* ─── Write chunk ─────────────────────────────────────────────────────── */

uint32_t NovaFwUpdate_WriteChunk(uint32_t offset, const uint8_t *data,
                                  uint32_t len, uint32_t chunk_crc)
{
    if (s_state != FW_STATE_RECEIVING)
        return 1;  /* Not in receiving state */

    if (offset != s_bytes_written)
        return 2;  /* Out-of-order chunk */

    if (offset + len > s_total_size)
        return 3;  /* Would exceed declared image size */

    /* Verify chunk CRC */
    uint32_t calc = crc32_buf(data, len);
    if (calc != chunk_crc)
        return 4;  /* Chunk CRC mismatch */

    /* Write to flash */
    uint32_t flash_addr = bank_flash_offset(s_target_bank) + offset;
    if (hal_flash_write(flash_addr, data, len) != 0) {
        s_state = FW_STATE_ERROR;
        return 5;  /* Flash write failed */
    }

    s_bytes_written += len;
    return 0;
}

/* ─── Finalize: verify full image CRC ─────────────────────────────────── */

uint32_t NovaFwUpdate_Finalize(uint32_t expected_crc)
{
    if (s_state != FW_STATE_RECEIVING)
        return 1;

    if (s_bytes_written != s_total_size)
        return 2;  /* Incomplete transfer */

    s_state = FW_STATE_VERIFYING;

    /* Read back from flash and compute CRC in chunks */
    uint32_t flash_base = bank_flash_offset(s_target_bank);
    uint32_t crc = 0;
    uint8_t tmp[256];
    uint32_t remaining = s_total_size;
    uint32_t off = 0;

    while (remaining > 0) {
        uint32_t chunk = (remaining > sizeof(tmp)) ? sizeof(tmp) : remaining;
        hal_flash_read(flash_base + off, tmp, chunk);
        crc = crc32_update(crc, tmp, chunk);
        off += chunk;
        remaining -= chunk;
    }

    if (crc != expected_crc) {
        s_state = FW_STATE_ERROR;
        return 3;  /* Full image CRC mismatch */
    }

    /* Write bank header for the target bank */
    FwBankHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = FW_BANK_MAGIC;
    hdr.image_size = s_total_size;
    hdr.image_crc = expected_crc;
    hdr.valid = 1;
    hdr.active = 0;  /* Not yet active — wait for Activate command */

    /* Sequence: higher than both existing banks */
    uint32_t maxSeq = s_bank_a_hdr.sequence;
    if (s_bank_b_hdr.sequence > maxSeq) maxSeq = s_bank_b_hdr.sequence;
    hdr.sequence = maxSeq + 1;

    strncpy(hdr.version, s_staged_version, sizeof(hdr.version) - 1);
    write_bank_header(s_target_bank, &hdr);

    /* Update cached copy */
    if (s_target_bank == 0) s_bank_a_hdr = hdr;
    else                     s_bank_b_hdr = hdr;

    s_state = FW_STATE_VERIFIED;
    return 0;
}

/* ─── Activate: swap active bank, optionally reboot ───────────────────── */

uint32_t NovaFwUpdate_Activate(bool reboot)
{
    if (s_state != FW_STATE_VERIFIED)
        return 1;

    s_state = FW_STATE_ACTIVATING;

    /* Mark the new bank as active and the old bank as inactive */
    FwBankHeader *new_hdr = (s_target_bank == 0) ? &s_bank_a_hdr : &s_bank_b_hdr;
    FwBankHeader *old_hdr = (s_target_bank == 0) ? &s_bank_b_hdr : &s_bank_a_hdr;

    new_hdr->active = 1;
    old_hdr->active = 0;

    write_bank_header(s_target_bank, new_hdr);
    write_bank_header(s_active_bank, old_hdr);

    /* Reset boot watchdog strikes for the new image */
    s_boot_meta.watchdog_strikes = 0;
    write_boot_meta(&s_boot_meta);

    s_active_bank = s_target_bank;
    s_state = FW_STATE_IDLE;

    if (reboot) {
        /* Trigger system reset — on AM2434, write to WDTIMER or SYSRESET */
        /* For QEMU, a simple infinite loop will trigger the watchdog */
        volatile uint32_t *sysreset = (volatile uint32_t *)0x44000000;
        *sysreset = 0x1;  /* Platform-specific reset register */
        while (1) {}       /* Should not reach here */
    }

    return 0;
}

/* ─── Abort ───────────────────────────────────────────────────────────── */

void NovaFwUpdate_Abort(void)
{
    s_state = FW_STATE_IDLE;
    s_bytes_written = 0;
    s_total_size = 0;
}

/* ─── Validate a bank (read back + CRC check) ────────────────────────── */

bool NovaFwUpdate_ValidateBank(uint32_t bank)
{
    FwBankHeader hdr;
    read_bank_header(bank, &hdr);

    if (hdr.magic != FW_BANK_MAGIC || !hdr.valid || hdr.image_size == 0)
        return false;

    uint32_t flash_base = bank_flash_offset(bank);
    uint32_t crc = 0;
    uint8_t tmp[256];
    uint32_t remaining = hdr.image_size;
    uint32_t off = 0;

    while (remaining > 0) {
        uint32_t chunk = (remaining > sizeof(tmp)) ? sizeof(tmp) : remaining;
        hal_flash_read(flash_base + off, tmp, chunk);
        crc = crc32_update(crc, tmp, chunk);
        off += chunk;
        remaining -= chunk;
    }

    return (crc == hdr.image_crc);
}

/* ─── Boot validation (called early in startup) ──────────────────────── */

#define MAX_WATCHDOG_STRIKES 3

void NovaFwUpdate_BootValidation(void)
{
    NovaFwUpdate_Init();

    s_boot_meta.boot_count++;
    s_boot_meta.watchdog_strikes++;

    if (s_boot_meta.watchdog_strikes > MAX_WATCHDOG_STRIKES) {
        /* Too many failed boots — fall back to other bank or golden image */
        uint32_t fallback = NovaFwUpdate_GetInactiveBank();
        FwBankHeader fhdr;
        read_bank_header(fallback, &fhdr);

        if (fhdr.magic == FW_BANK_MAGIC && fhdr.valid) {
            /* Swap to the other bank */
            FwBankHeader *cur = (s_active_bank == 0) ? &s_bank_a_hdr : &s_bank_b_hdr;
            cur->active = 0;
            fhdr.active = 1;
            write_bank_header(s_active_bank, cur);
            write_bank_header(fallback, &fhdr);
            s_active_bank = fallback;
            s_boot_meta.boot_reason = 2;  /* FALLBACK */
        } else {
            /* Both banks failed — golden recovery */
            s_boot_meta.boot_reason = 2;  /* FALLBACK to golden */
            /* Bootloader would load from FW_GOLDEN_OFFSET */
        }
        s_boot_meta.watchdog_strikes = 0;
    } else {
        s_boot_meta.boot_reason = 0;   /* Normal boot */
    }

    write_boot_meta(&s_boot_meta);
}

/* ─── Confirm boot success (clears watchdog strikes) ──────────────────── */

void NovaFwUpdate_ConfirmBoot(void)
{
    s_boot_meta.watchdog_strikes = 0;
    s_boot_meta.boot_reason = 0;
    write_boot_meta(&s_boot_meta);
}
