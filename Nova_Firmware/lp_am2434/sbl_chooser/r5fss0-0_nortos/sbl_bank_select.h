/*
 * sbl_bank_select.h — F2c bank selection for the Constellation custom SBL.
 *
 * Bare-metal NoRTOS port of NovaFwUpdate_BootValidation
 * (Nova_Firmware/Platform/nova_fw_update.c). Runs after Drivers_open()
 * / Board_driversOpen() in main.c, before Bootloader_open(), and
 * decides which OSPI offset the bootloader will load the app from.
 *
 * Authoritative offset macros live in
 * Nova_Firmware/Platform/nova_fw_update.h. We mirror them as local
 * constants so the SBL build can be SDK-internal (no Platform/
 * include path pollution into the SDK's example tree).
 *
 * Full design: docs/lp-am2434-f2c-sbl-chooser-design.md §4.
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */
#ifndef SBL_BANK_SELECT_H
#define SBL_BANK_SELECT_H

#include <stdint.h>
#include <stdbool.h>
#include <board/flash.h>

/* ─── OSPI layout (must match Nova_Firmware/Platform/nova_fw_update.h) ── */
#define SBL_FW_HEADER_OFFSET       0x300000U  /* FwBootMeta + Bank A header sector */
#define SBL_FW_BANK_B_HDR_SECTOR   0x310000U  /* Bank B header sector */
#define SBL_FW_BANK_A_OFFSET       0x080000U
#define SBL_FW_BANK_B_OFFSET       0x900000U
#define SBL_FW_GOLDEN_OFFSET       0xC00000U
#define SBL_FW_BOOT_META_OFFSET    0x000000U  /* in FW_HEADER_OFFSET sector */
#define SBL_FW_BANK_A_HDR_OFFSET   0x000080U  /* in FW_HEADER_OFFSET sector */
#define SBL_FW_BANK_B_HDR_OFFSET   0x000000U  /* in FW_BANK_B_HDR_SECTOR sector */
#define SBL_FW_BANK_MAGIC          0x4E4F5641U  /* "NOVA" */
#define SBL_MAX_WATCHDOG_STRIKES   3U

/* ─── Struct layouts mirrored from Platform/nova_fw_update.h ──────────── */
/* Keep these byte-for-byte identical to the Platform versions. The
 * compile-time check in sbl_bank_select.c verifies sizeof matches. */

typedef struct __attribute__((packed)) {
    uint32_t magic;            /* Must be SBL_FW_BANK_MAGIC */
    uint32_t image_size;
    uint32_t image_crc;
    uint32_t valid;            /* 1 = image verified OK */
    uint32_t active;           /* 1 = this is the active bank */
    uint32_t sequence;         /* Monotonic: higher = newer */
    char     version[32];
    uint8_t  reserved[80];     /* Total struct: 24 + 32 + 80 = 136 bytes.
                                * Platform/nova_fw_update.h's "Pad to 128"
                                * comment is wrong; the actual on-flash
                                * record is 136 bytes. Header sector at
                                * FW_HEADER_OFFSET has 64 KB to fit it. */
} SblFwBankHeader;

typedef struct __attribute__((packed)) {
    uint32_t boot_count;
    uint32_t boot_reason;      /* 0=normal, 1=watchdog, 2=fallback */
    uint32_t watchdog_strikes;
    uint8_t  reserved[116];    /* Pad to 128 */
} SblFwBootMeta;

/* ─── Chosen bank result ──────────────────────────────────────────────── */

typedef enum {
    SBL_BANK_A      = 0,
    SBL_BANK_B      = 1,
    SBL_BANK_GOLDEN = 2,
    SBL_BANK_LEGACY = 3,   /* Pre-F2c board: no header magic, just boot 0x080000 */
} SblChosenBank;

typedef struct {
    SblChosenBank bank;       /* Which bank we picked */
    uint32_t      flash_off;  /* OSPI byte offset to pass to Bootloader_open */
    uint32_t      sequence;   /* For boot trace */
    uint32_t      boot_count; /* Updated counter after this boot */
    uint32_t      strikes;    /* Strike count after this boot */
    uint32_t      reason;     /* boot_reason written to FwBootMeta */
} SblBankSelection;

/* ─── Public API ──────────────────────────────────────────────────────── */

/* Read metadata from `fHandle`, decide which bank to boot, write the
 * updated FwBootMeta (boot_count++, strikes++) BEFORE returning.
 * The strike write is what makes Session 1's ConfirmBoot path useful:
 * the SBL bumps strikes, the app clears them once healthy.
 *
 * Idempotent in the face of power loss BETWEEN the meta write and the
 * bank jump: next boot just sees the higher counts and proceeds — worst
 * case is one extra strike accounted to a fault that wasn't a real hang.
 *
 * Returns SystemP_SUCCESS / SystemP_FAILURE; the chosen bank is in *out
 * even on partial failure (defaults to LEGACY/0x080000).
 */
int32_t SblBankSelect_Choose(Flash_Handle fHandle, SblBankSelection *out);

/* Diagnostic: print the selection to the SBL's UART log. Safe to call
 * after SblBankSelect_Choose. Format:
 *   [SBL] bank=A seq=12 off=0x080000 boots=42 strikes=1 reason=0
 */
void SblBankSelect_LogSelection(const SblBankSelection *sel);

#endif /* SBL_BANK_SELECT_H */
