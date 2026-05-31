/*
 * sbl_bank_select.c — F2c bank selection logic for the Constellation SBL.
 *
 * Bare-metal NoRTOS port of NovaFwUpdate_BootValidation from
 * Nova_Firmware/Platform/nova_fw_update.c. Differences from the
 * Platform version:
 *
 *   1. No FreeRTOS mutex (single thread, no concurrent OSPI access).
 *   2. Uses the SBL's Flash driver handle directly (Flash_read /
 *      Flash_eraseBlk / Flash_write). The Platform version goes through
 *      hal_flash, which adds a mutex + DAC-mode handling that is FreeRTOS-
 *      specific. In the SBL we're indirect-mode-only and single-threaded.
 *   3. No DAC-mode complexity. The SBL has not enabled DAC mode yet
 *      (that happens late in main.c per the TI sbl_ospi reference, just
 *      before bank A handoff). All header reads/writes are indirect.
 *   4. No CRC validation of the image — that's the app's job (the SBL
 *      just trusts the FwBankHeader's `valid` bit, which was set by the
 *      app's NovaFwUpdate_Finalize after a successful CRC + signature
 *      check). The SBL's job is purely about bank-selection + strike
 *      counting; image-integrity is an app-level concern.
 *
 * Full algorithm in docs/lp-am2434-f2c-sbl-chooser-design.md §4.
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */

#include "sbl_bank_select.h"
#include "sbl_hal_flash.h"
#include <string.h>
#include <kernel/dpl/DebugP.h>
#include <kernel/dpl/SystemP.h>
#include <board/flash.h>

/* Compile-time invariant: struct sizes must match Platform/nova_fw_update.h. */
_Static_assert(sizeof(SblFwBankHeader) == 136,
               "SblFwBankHeader must be 136 bytes (24 fixed + 32 version + 80 reserved)");
_Static_assert(sizeof(SblFwBootMeta)   == 128,
               "SblFwBootMeta must be 128 bytes (12 fixed + 116 reserved)");

/* ─── Read helpers (indirect-mode OSPI via Flash driver) ─────────────── */

static int32_t read_bank_header(Flash_Handle f, uint32_t bank,
                                SblFwBankHeader *hdr)
{
    uint32_t off = (bank == 0)
        ? (SBL_FW_HEADER_OFFSET + SBL_FW_BANK_A_HDR_OFFSET)
        : (SBL_FW_BANK_B_HDR_SECTOR + SBL_FW_BANK_B_HDR_OFFSET);
    return Flash_read(f, off, (uint8_t *)hdr, sizeof(*hdr));
}

static int32_t read_boot_meta(Flash_Handle f, SblFwBootMeta *meta)
{
    return Flash_read(f, SBL_FW_HEADER_OFFSET + SBL_FW_BOOT_META_OFFSET,
                      (uint8_t *)meta, sizeof(*meta));
}

/* ─── Atomic metadata block rewrite (bare-metal STIG erase + DAC write) ── */
/*
 * 2026-05-31 rewrite + iteration 3: bypass SDK Flash_eraseBlk/Flash_write
 * entirely. Bench evidence: SDK paths silently no-op on Cypress S25HL512T
 * in the SBL context. Iteration 1 (this commit's port) successfully wrote
 * boot_count=2 to OSPI but bricked STORAGE because sbl_hal_flash_write left
 * DAC mode enabled and main.c expects DAC disabled across
 * Bootloader_parseAndLoadMultiCoreELF. Iteration 2 disables DAC at end of
 * sbl_hal_flash_write (mirroring hal_flash_read_dac's
 * save-and-restore pattern). The atomic metadata-block dance is the same
 * approach as Platform/nova_fw_update.c::write_meta_block_atomic:
 *
 *   1. Read existing Bank A + Bank B headers.
 *   2. Build two 4-KB scratch sectors in BSS (sec_a holds FwBootMeta +
 *      Bank A header at +0x80; sec_b holds Bank B header at +0x00).
 *   3. Erase the 256-KB block at SBL_FW_HEADER_OFFSET (Cypress S25HL512T
 *      only honors block-erase 0xDC; sector erase 0x20/0x21 wedges the
 *      OSPI controller).
 *   4. DAC-write both sectors via sbl_hal_flash_write — Bank B header at
 *      0x310000 is preserved across the block erase because step 4
 *      programs it back.
 *
 * See memories/repo/sbl-chooser-wcc-bug-2026-05-31.md for the full
 * negative rollback test that motivated this. */
#define SBL_FW_SECTOR_SIZE  4096U
static uint8_t s_sec_a[SBL_FW_SECTOR_SIZE];   /* sector at SBL_FW_HEADER_OFFSET (0x300000) */
static uint8_t s_sec_b[SBL_FW_SECTOR_SIZE];   /* sector at SBL_FW_BANK_B_HDR_SECTOR (0x310000) */

static int32_t write_boot_meta(Flash_Handle f, const SblFwBootMeta *meta)
{
    /* Read existing Bank A + Bank B headers so we can repack them
     * around the new FwBootMeta. */
    SblFwBankHeader hdr_a;
    SblFwBankHeader hdr_b;
    int32_t status = read_bank_header(f, 0, &hdr_a);
    if (status != SystemP_SUCCESS) return status;
    status = read_bank_header(f, 1, &hdr_b);
    if (status != SystemP_SUCCESS) return status;

    /* Build the two 4-KB scratch sectors. Init to 0xFF so the unused
     * remainder matches post-erase state. */
    memset(s_sec_a, 0xFF, sizeof(s_sec_a));
    memcpy(s_sec_a + SBL_FW_BOOT_META_OFFSET,  meta,   sizeof(*meta));
    memcpy(s_sec_a + SBL_FW_BANK_A_HDR_OFFSET, &hdr_a, sizeof(hdr_a));

    memset(s_sec_b, 0xFF, sizeof(s_sec_b));
    memcpy(s_sec_b + SBL_FW_BANK_B_HDR_OFFSET, &hdr_b, sizeof(hdr_b));

    /* Erase the 256-KB block via bare-metal STIG (bypasses SDK). */
    int rce = sbl_hal_flash_erase_block(SBL_FW_HEADER_OFFSET);
    if (rce != 0) {
        DebugP_log("[SBL] write_boot_meta: erase failed rc=%d\r\n", rce);
        return (int32_t)rce;
    }

    /* Write both sectors via bare-metal DAC. sbl_hal_flash_write
     * captures+restores DAC state so the SDK's follow-on
     * Bootloader_parseAndLoadMultiCoreELF sees the OSPI controller in
     * the mode it expects (DAC disabled until main.c:243). */
    int rca = sbl_hal_flash_write(SBL_FW_HEADER_OFFSET, s_sec_a, sizeof(s_sec_a));
    if (rca != 0) {
        DebugP_log("[SBL] write_boot_meta: sec_a write failed rc=%d\r\n", rca);
        return (int32_t)rca;
    }
    int rcb = sbl_hal_flash_write(SBL_FW_BANK_B_HDR_SECTOR, s_sec_b, sizeof(s_sec_b));
    if (rcb != 0) {
        DebugP_log("[SBL] write_boot_meta: sec_b write failed rc=%d\r\n", rcb);
        return (int32_t)rcb;
    }
    return SystemP_SUCCESS;
}

/* ─── Selection algorithm ────────────────────────────────────────────── */

/* Pick the best candidate bank.
 *
 * Sort key: (sequence DESC, valid DESC). Among equally-valid candidates,
 * the higher sequence wins. If the highest-sequence bank has strikes >=
 * MAX_WATCHDOG_STRIKES, skip it (assume previous boot bricked) and pick
 * the next.
 *
 * Returns SBL_BANK_GOLDEN if neither A nor B is usable.
 * Returns SBL_BANK_LEGACY if BOTH banks have invalid magic AND boot
 * count is 0 (fresh-from-manufacturing board, JTAG bringup mode — boot
 * 0x080000 unconditionally).
 */
static SblChosenBank pick_bank(const SblFwBankHeader *a,
                               const SblFwBankHeader *b,
                               const SblFwBootMeta   *meta,
                               bool *out_skip_highest)
{
    *out_skip_highest = false;

    const bool a_ok = (a->magic == SBL_FW_BANK_MAGIC) && a->valid;
    const bool b_ok = (b->magic == SBL_FW_BANK_MAGIC) && b->valid;

    /* Fresh board — both headers blank, boot_count fresh. Just try to
     * boot whatever's at 0x080000 (the JTAG-flashed app). */
    if (a->magic != SBL_FW_BANK_MAGIC &&
        b->magic != SBL_FW_BANK_MAGIC &&
        meta->boot_count == 0)
    {
        return SBL_BANK_LEGACY;
    }

    /* Build sorted candidate list (highest sequence first). */
    const SblFwBankHeader *first  = NULL;
    const SblFwBankHeader *second = NULL;
    SblChosenBank          first_id  = SBL_BANK_GOLDEN;
    SblChosenBank          second_id = SBL_BANK_GOLDEN;

    if (a_ok && b_ok) {
        if (a->sequence >= b->sequence) {
            first = a; first_id = SBL_BANK_A;
            second = b; second_id = SBL_BANK_B;
        } else {
            first = b; first_id = SBL_BANK_B;
            second = a; second_id = SBL_BANK_A;
        }
    } else if (a_ok) {
        first = a; first_id = SBL_BANK_A;
    } else if (b_ok) {
        first = b; first_id = SBL_BANK_B;
    }
    /* else: both invalid — fall through to GOLDEN. */

    if (first == NULL) {
        return SBL_BANK_GOLDEN;
    }

    /* If strikes have exceeded the threshold AND we have a runner-up,
     * skip the highest-sequence bank (it likely bricked). The strike
     * counter is already-incremented (this boot's increment happened
     * before pick_bank was called) so >= MAX means we've now seen the
     * threshold reached. */
    if (meta->watchdog_strikes >= SBL_MAX_WATCHDOG_STRIKES) {
        *out_skip_highest = true;
        if (second != NULL) {
            return second_id;
        }
        /* No fallback bank — go to Golden. */
        return SBL_BANK_GOLDEN;
    }

    return first_id;
}

/* ─── Public API ──────────────────────────────────────────────────────── */

int32_t SblBankSelect_Choose(Flash_Handle fHandle, SblBankSelection *out)
{
    SblFwBankHeader hdr_a;
    SblFwBankHeader hdr_b;
    SblFwBootMeta   meta;
    bool            skip_highest = false;

    /* Default to LEGACY/0x080000 in case we early-return on an error.
     * That's the safest fallback — it matches the pre-F2c behavior. */
    out->bank       = SBL_BANK_LEGACY;
    out->flash_off  = SBL_FW_BANK_A_OFFSET;
    out->sequence   = 0;
    out->boot_count = 0;
    out->strikes    = 0;
    out->reason     = 0;

    int32_t status = read_bank_header(fHandle, 0, &hdr_a);
    if (status != SystemP_SUCCESS) return status;
    status = read_bank_header(fHandle, 1, &hdr_b);
    if (status != SystemP_SUCCESS) return status;
    status = read_boot_meta(fHandle, &meta);
    if (status != SystemP_SUCCESS) return status;

    /* Bump counters. The strike write happens BEFORE we hand off to
     * the app — see comment in sbl_bank_select.h. */
    meta.boot_count++;
    meta.watchdog_strikes++;

    SblChosenBank chosen = pick_bank(&hdr_a, &hdr_b, &meta, &skip_highest);

    /* If we fell back due to strikes, mark boot_reason = FALLBACK. */
    if (skip_highest) {
        meta.boot_reason = 2u;       /* FALLBACK */
        meta.watchdog_strikes = 0u;  /* Reset so the fallback bank gets a fresh budget. */
    } else if (chosen == SBL_BANK_LEGACY) {
        /* Fresh board — boot_reason stays 0. */
        meta.boot_reason = 0u;
    } else {
        meta.boot_reason = 0u;       /* Normal boot */
    }

    /* Persist the updated meta BEFORE handing off to the app. */
    status = write_boot_meta(fHandle, &meta);
    if (status != SystemP_SUCCESS) {
        /* Couldn't persist — still hand off so the board boots
         * (and gets a chance to recover via the next OTA cycle).
         * Worst case: strike counter doesn't increment and we lose
         * the rollback safety net for this one boot. */
        DebugP_log("[SBL] WARN: meta write failed (status=%d), booting anyway\r\n",
                   (int)status);
    }

    /* Translate chosen bank into the flash offset the bootloader will
     * load from. The Bootloader_Config's offset is what gets used. */
    out->bank       = chosen;
    out->sequence   = (chosen == SBL_BANK_A) ? hdr_a.sequence
                    : (chosen == SBL_BANK_B) ? hdr_b.sequence
                    : 0u;
    out->boot_count = meta.boot_count;
    out->strikes    = meta.watchdog_strikes;
    out->reason     = meta.boot_reason;

    switch (chosen) {
        case SBL_BANK_A:      out->flash_off = SBL_FW_BANK_A_OFFSET;  break;
        case SBL_BANK_B:      out->flash_off = SBL_FW_BANK_B_OFFSET;  break;
        case SBL_BANK_GOLDEN: out->flash_off = SBL_FW_GOLDEN_OFFSET;  break;
        case SBL_BANK_LEGACY: out->flash_off = SBL_FW_BANK_A_OFFSET;  break;
    }

    return SystemP_SUCCESS;
}

void SblBankSelect_LogSelection(const SblBankSelection *sel)
{
    const char *bank_name = "?";
    switch (sel->bank) {
        case SBL_BANK_A:      bank_name = "A";       break;
        case SBL_BANK_B:      bank_name = "B";       break;
        case SBL_BANK_GOLDEN: bank_name = "GOLDEN";  break;
        case SBL_BANK_LEGACY: bank_name = "LEGACY";  break;
    }
    DebugP_log("[SBL] bank=%s seq=%u off=0x%06x boots=%u strikes=%u reason=%u\r\n",
               bank_name,
               (unsigned)sel->sequence,
               (unsigned)sel->flash_off,
               (unsigned)sel->boot_count,
               (unsigned)sel->strikes,
               (unsigned)sel->reason);
}
