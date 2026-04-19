/*
 * nova_settings_store.c — Ping-pong settings persistence (AM2434 OSPI)
 *
 * Real-hardware counterpart to the banking logic in
 * constellation-ui/server/src/armSimulator.ts.  Identical semantics:
 *
 *   - FwBankHeader {magic, image_size, image_crc, valid, active, sequence,
 *                   version, reserved}  (reused from nova_fw_update.h)
 *   - CRC-32 (Ethernet polynomial 0xEDB88320, zlib-compatible)
 *   - Writes target the *inactive* bank, then flip
 *   - Boot picks the bank with the highest valid sequence that passes CRC
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */

#include "nova_settings_store.h"
#include "nova_fw_update.h"   /* FwBankHeader, FW_BANK_MAGIC, FW_SETTINGS_OFFSET */
#include "hal.h"
#include "debug.h"
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

static uint32_t crc32_flash(uint32_t addr, size_t len)
{
    uint8_t tmp[256];
    uint32_t crc = 0;
    while (len > 0) {
        size_t chunk = len > sizeof(tmp) ? sizeof(tmp) : len;
        if (hal_flash_read(addr, tmp, chunk) != 0) return 0xFFFFFFFF;
        crc = crc32_update(crc, tmp, chunk);
        addr += chunk;
        len  -= chunk;
    }
    return crc;
}

/* ─── Bank metadata ───────────────────────────────────────────────────── */

typedef struct {
    uint32_t hdr_off;   /* absolute flash address of header sector */
    uint32_t data_off;  /* absolute flash address of data region */
} BankRegion;

static const BankRegion BANK_A = {
    FW_SETTINGS_OFFSET + NSS_BANK_A_HDR_REL,
    FW_SETTINGS_OFFSET + NSS_BANK_A_DATA_REL,
};
static const BankRegion BANK_B = {
    FW_SETTINGS_OFFSET + NSS_BANK_B_HDR_REL,
    FW_SETTINGS_OFFSET + NSS_BANK_B_DATA_REL,
};
static const BankRegion DEFAULTS = {
    FW_SETTINGS_OFFSET + NSS_DEFAULTS_HDR_REL,
    FW_SETTINGS_OFFSET + NSS_DEFAULTS_DATA_REL,
};

/* ─── Static state ────────────────────────────────────────────────────── */

static int      s_active_bank = -1;     /* 0 = A, 1 = B, -1 = none */
static uint32_t s_sequence    = 0;

/* ─── Header helpers ──────────────────────────────────────────────────── */

/** Read a bank header and validate magic + valid flag + data CRC.
 *  Returns true iff the bank is usable. */
static bool read_and_verify_header(const BankRegion *b, FwBankHeader *out)
{
    FwBankHeader hdr;
    if (hal_flash_read(b->hdr_off, (uint8_t *)&hdr, sizeof(hdr)) != 0) {
        return false;
    }
    if (hdr.magic != FW_BANK_MAGIC)        return false;
    if (hdr.valid != 1)                    return false;
    if (hdr.image_size == 0)               return false;
    if (hdr.image_size > NSS_MAX_BLOB_SIZE) return false;

    /* Schema-version guard: reject blobs from a previous Settings layout
     * (or different factory-default IO map).  Bump NSS_SCHEMA_VERSION /
     * NSS_SCHEMA_TAG in nova_settings_store.h to force a regen. */
    if (memcmp(hdr.version, NSS_SCHEMA_TAG, sizeof(NSS_SCHEMA_TAG)) != 0) {
        debug_printf("[Settings] bank@0x%06lX schema mismatch: stored='%.31s' expected='%s'\r\n",
                     (unsigned long)b->hdr_off,
                     hdr.version,
                     NSS_SCHEMA_TAG);
        return false;
    }

    uint32_t crc = crc32_flash(b->data_off, hdr.image_size);
    if (crc != hdr.image_crc) {
        debug_printf("[Settings] bank@0x%06lX CRC mismatch: calc=0x%08lX hdr=0x%08lX\r\n",
                     (unsigned long)b->hdr_off,
                     (unsigned long)crc,
                     (unsigned long)hdr.image_crc);
        return false;
    }

    if (out) *out = hdr;
    return true;
}

/** Erase a header sector then program a fresh header. */
static int write_header(const BankRegion *b, const FwBankHeader *hdr)
{
    if (hal_flash_erase_sector(b->hdr_off) != 0) return -1;
    return hal_flash_write(b->hdr_off, (const uint8_t *)hdr, sizeof(*hdr));
}

/** Erase all sectors covering [data_off, data_off+len) and program the blob. */
static int write_blob(const BankRegion *b, const void *blob, size_t len)
{
    uint32_t addr = b->data_off;
    uint32_t end  = addr + len;
    for (uint32_t s = addr & ~(FW_SECTOR_SIZE - 1); s < end; s += FW_SECTOR_SIZE) {
        if (hal_flash_erase_sector(s) != 0) return -1;
    }
    return hal_flash_write(addr, (const uint8_t *)blob, len);
}

/* ─── Public API ──────────────────────────────────────────────────────── */

void NovaSettings_Init(void)
{
    FwBankHeader ha, hb;
    bool valid_a = read_and_verify_header(&BANK_A, &ha);
    bool valid_b = read_and_verify_header(&BANK_B, &hb);

    if (valid_a && valid_b) {
        if (hb.sequence > ha.sequence) {
            s_active_bank = 1; s_sequence = hb.sequence;
        } else {
            s_active_bank = 0; s_sequence = ha.sequence;
        }
    } else if (valid_a) {
        s_active_bank = 0; s_sequence = ha.sequence;
    } else if (valid_b) {
        s_active_bank = 1; s_sequence = hb.sequence;
    } else {
        s_active_bank = -1; s_sequence = 0;
    }

    debug_printf("[Settings] Init: active=%s seq=%lu (A=%s B=%s)\r\n",
                 s_active_bank == 0 ? "A" :
                 s_active_bank == 1 ? "B" : "none",
                 (unsigned long)s_sequence,
                 valid_a ? "ok" : "bad",
                 valid_b ? "ok" : "bad");
}

int NovaSettings_GetActiveBank(void) { return s_active_bank; }

uint32_t NovaSettings_GetSequence(void) { return s_sequence; }

NssResult NovaSettings_Load(void *buf, size_t buf_size, size_t *out_len)
{
    if (s_active_bank < 0) return NSS_ERR_NO_VALID_BANK;

    const BankRegion *b = (s_active_bank == 0) ? &BANK_A : &BANK_B;
    FwBankHeader hdr;
    if (!read_and_verify_header(b, &hdr)) {
        /* Active bank went bad since init — try the other */
        const BankRegion *alt = (s_active_bank == 0) ? &BANK_B : &BANK_A;
        if (!read_and_verify_header(alt, &hdr)) return NSS_ERR_CRC;
        s_active_bank ^= 1;
        s_sequence = hdr.sequence;
        b = alt;
    }

    if (hdr.image_size > buf_size) return NSS_ERR_SIZE;
    if (hal_flash_read(b->data_off, (uint8_t *)buf, hdr.image_size) != 0) {
        return NSS_ERR_FLASH;
    }
    if (out_len) *out_len = hdr.image_size;
    return NSS_OK;
}

NssResult NovaSettings_Save(const void *blob, size_t len)
{
    if (len == 0 || len > NSS_MAX_BLOB_SIZE) return NSS_ERR_SIZE;

    /* Pick the inactive bank.  On first boot (active=-1) default to A. */
    int target = (s_active_bank == 0) ? 1 : 0;
    const BankRegion *b = (target == 0) ? &BANK_A : &BANK_B;

    /* 1. Erase + program data region */
    if (write_blob(b, blob, len) != 0) return NSS_ERR_FLASH;

    /* 2. Build header: bumped sequence, valid=1, fresh CRC */
    FwBankHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic      = FW_BANK_MAGIC;
    hdr.image_size = (uint32_t)len;
    hdr.image_crc  = crc32_update(0, (const uint8_t *)blob, len);
    hdr.valid      = 1;
    hdr.active     = 1;
    hdr.sequence   = s_sequence + 1;
    /* Tag the header with our schema version so future firmware can
     * reject blobs from incompatible Settings layouts. */
    memcpy(hdr.version, NSS_SCHEMA_TAG, sizeof(NSS_SCHEMA_TAG));

    /* 3. Erase + program header.  Only after this succeeds do we flip
     *    in-RAM state, so a crash mid-write leaves the previously-active
     *    bank untouched. */
    if (write_header(b, &hdr) != 0) return NSS_ERR_FLASH;

    /* 4. Read-back verify (catches bus glitches / bad sectors) */
    FwBankHeader rb;
    if (!read_and_verify_header(b, &rb) || rb.sequence != hdr.sequence) {
        return NSS_ERR_CRC;
    }

    s_active_bank = target;
    s_sequence    = hdr.sequence;

    debug_printf("[Settings] Saved %lu bytes to bank %s seq=%lu crc=0x%08lX\r\n",
                 (unsigned long)len,
                 target == 0 ? "A" : "B",
                 (unsigned long)hdr.sequence,
                 (unsigned long)hdr.image_crc);
    return NSS_OK;
}

/* ─── Panel defaults (single-slot, erase-in-place) ────────────────────── */

NssResult NovaSettings_SavePanelDefaults(const void *blob, size_t len)
{
    if (len == 0 || len > NSS_MAX_BLOB_SIZE) return NSS_ERR_SIZE;

    if (write_blob(&DEFAULTS, blob, len) != 0) return NSS_ERR_FLASH;

    FwBankHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic      = FW_BANK_MAGIC;
    hdr.image_size = (uint32_t)len;
    hdr.image_crc  = crc32_update(0, (const uint8_t *)blob, len);
    hdr.valid      = 1;
    hdr.active     = 1;
    hdr.sequence   = 1;
    memcpy(hdr.version, NSS_SCHEMA_TAG, sizeof(NSS_SCHEMA_TAG));

    if (write_header(&DEFAULTS, &hdr) != 0) return NSS_ERR_FLASH;
    if (!read_and_verify_header(&DEFAULTS, NULL))  return NSS_ERR_CRC;

    debug_printf("[Settings] Panel defaults saved: %lu bytes\r\n",
                 (unsigned long)len);
    return NSS_OK;
}

NssResult NovaSettings_LoadPanelDefaults(void *buf, size_t buf_size,
                                          size_t *out_len)
{
    FwBankHeader hdr;
    if (!read_and_verify_header(&DEFAULTS, &hdr)) return NSS_ERR_NO_VALID_BANK;
    if (hdr.image_size > buf_size)                return NSS_ERR_SIZE;
    if (hal_flash_read(DEFAULTS.data_off, (uint8_t *)buf, hdr.image_size) != 0) {
        return NSS_ERR_FLASH;
    }
    if (out_len) *out_len = hdr.image_size;
    return NSS_OK;
}

bool NovaSettings_HasPanelDefaults(void)
{
    return read_and_verify_header(&DEFAULTS, NULL);
}
