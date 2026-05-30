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
#include <string.h>
#include <kernel/dpl/DebugP.h>
#include <kernel/dpl/SystemP.h>
#include <drivers/flash.h>

/* Compile-time invariant: struct sizes must match Platform/nova_fw_update.h. */
_Static_assert(sizeof(SblFwBankHeader) == 128,
               "SblFwBankHeader must be 128 bytes (matches FwBankHeader)");
_Static_assert(sizeof(SblFwBootMeta)   == 128,
               "SblFwBootMeta must be 128 bytes (matches FwBootMeta)");

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

/* ─── Write helper (erase header sector, repack, program back) ───────── */
/*
 * The 64 KB header sector holds FwBootMeta at offset 0 + Bank A header
 * at offset 0x80 + the rest is reserved for future per-bank metadata.
 * To update FwBootMeta we must:
 *
 *   1. Read the existing 64 KB sector (or at least the first 256 B —
 *      everything else stays 0xFF after erase).
 *   2. Erase the 64 KB block.
 *   3. Program back the updated FwBootMeta + the existing Bank A header.
 *
 * Bank B's header lives in a separate sector (FW_BANK_B_HDR_SECTOR),
 * untouched here. That's the whole point of the dual-sector layout —
 * updating bank A metadata cannot wipe bank B and vice versa.
 *
 * The 0.A.208 SBL-preserve invariant (CLAUDE.md #11): the OSPI DAC bit
 * must NOT be toggled between sub-page programs. The SBL hasn't enabled
 * DAC yet, so this code path is safe — Flash_write goes through
 * indirect mode end-to-end.
 */
static int32_t write_boot_meta(Flash_Handle f, const SblFwBootMeta *meta)
{
    /* Read back the existing Bank A header so we can re-program it
     * alongside the updated meta in the same erased block. */
    SblFwBankHeader hdr_a;
    int32_t status = read_bank_header(f, 0, &hdr_a);
    if (status != SystemP_SUCCESS) return status;

    /* Erase the 64 KB sector at FW_HEADER_OFFSET. Flash_eraseBlk takes
     * a byte address and snaps to the underlying block size (64 KB on
     * W25Q128JV). */
    status = Flash_eraseBlk(f, SBL_FW_HEADER_OFFSET);
    if (status != SystemP_SUCCESS) return status;

    /* Program FwBootMeta at offset 0. */
    status = Flash_write(f,
                         SBL_FW_HEADER_OFFSET + SBL_FW_BOOT_META_OFFSET,
                         (uint8_t *)meta, sizeof(*meta));
    if (status != SystemP_SUCCESS) return status;

    /* Re-program Bank A header at offset +0x80. */
    status = Flash_write(f,
                         SBL_FW_HEADER_OFFSET + SBL_FW_BANK_A_HDR_OFFSET,
                         (uint8_t *)&hdr_a, sizeof(hdr_a));
    return status;
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
