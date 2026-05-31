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

/* FreeRTOS yield support — used during multi-second OSPI erase loops
 * so the watchdog-feed task and lwIP can keep running.  Only the LP
 * cluster links this file; the NoRTOS watchdog cluster does not.  We
 * still guard with xTaskGetSchedulerState so pre-scheduler callers
 * (NovaFwUpdate_BootValidation from main()) don't hit an assert. */
#include "FreeRTOS.h"
#include "task.h"

/* OSPI block-erase granularity (Cypress S25HL512T 4S_4D_4D DTR mode
 * uses 256 KiB block-erase opcode 0xDC). Must match the BLOCK constant
 * in `Platform/hal_flash.c` and the BLOCK in the legacy erase_bank_range
 * loop. Centralised here as the OTA path owns the lazy-erase contract. */
#define FW_OTA_ERASE_BLOCK   (256u * 1024u)

/* OSPI Page Program granularity. Mirrors `HAL_FLASH_PAGE_SIZE` in
 * `Platform/hal_flash.c` (private to that TU). `hal_flash_write_dac`
 * requires the address AND length to be multiples of this — the last
 * OTA chunk of an image whose size isn't a multiple of 256 must be
 * tail-padded to one full page with 0xFF (erased state, idempotent
 * on NOR re-program of the padding bytes). */
#define NOVA_FLASH_PAGE       256u

/* Persistent 256-byte buffer for tail-padding the last OTA chunk when
 * its length isn't a page multiple. Static (BSS, ~256 B) so it doesn't
 * eat the broker task's stack budget on every WriteChunk. memset'd to
 * 0xFF on each use; no cross-call state retained. */
static uint8_t s_chunk_page_pad[NOVA_FLASH_PAGE];

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
/* 0.A.85: lazy block erase + direct chunk write. The 0.A.82 block
 * buffer (256 KiB scratch) was a workaround for what 0.A.79 looked
 * like a consecutive-Flash_write trap, but that diagnosis was wrong —
 * the second 16-byte Flash_write at addr=0x200010 failed the SDK's
 * page-alignment check (0x200010 % 256 != 0), not a hardware trap.
 * 0.A.84 then proved that LARGE single Flash_write calls (262144 B
 * = 16384 internal PPs) fail post-scheduler regardless of pageSize.
 * The remaining hypothesis: moderate-size Flash_write calls (1 KiB =
 * 64 PPs at pageSize=16, or 4 PPs at pageSize=256) chained after one
 * erase will work — we never actually tested that pattern cleanly.
 * Reverts to the lazy-erase + per-chunk write shape of 0.A.78 and
 * frees the 256 KiB BSS. */
static uint32_t s_erased_through = 0;     /* bytes erased from bank base */

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
    /* Per the canonical OSPI map (see nova_fw_update.h §layout):
     *   Bank A header: FW_HEADER_OFFSET + 0x80 (right after FwBootMeta)
     *   Bank B header: FW_BANK_B_HDR_SECTOR + 0
     * Separate sectors so a write to one bank's header doesn't erase
     * the other's. */
    uint32_t sector = (bank == 0) ? FW_HEADER_OFFSET    : FW_BANK_B_HDR_SECTOR;
    uint32_t off    = (bank == 0) ? FW_BANK_A_HDR_OFFSET : FW_BANK_B_HDR_OFFSET;
    hal_flash_read(sector + off, (uint8_t *)hdr, sizeof(FwBankHeader));
}

/* 0.A.200: atomic write-back of FwBootMeta + Bank A header + Bank B
 * header. **CRITICAL** — `nova_fw_update.h`'s comment claims each
 * header "lives in its own sector so updating one bank's metadata
 * cannot accidentally erase the other's", but the Cypress S25HL512T
 * in 4S_4D_4D DTR mode only supports 256-KB BLOCK erase (opcode
 * 0xDC); the 4-KB sector erase (0x20/0x21) leaves the OSPI controller
 * wedged (see `hal_flash.c::hal_flash_erase_sector` comment +
 * `memories/repo/lp-am2434-runtime-flashwrite-unresolved.md`). So
 * `hal_flash_erase_sector(FW_HEADER_OFFSET=0x60000)` actually erases
 * the entire 0x40000-0x80000 block — which also contains
 * `FW_BANK_B_HDR_SECTOR=0x70000`. Updating one header sector wiped
 * the OTHER sector to all-0xFF and then only wrote one back, leaving
 * the other bank's header invalid in flash (cache stayed correct
 * in RAM — that's why the bug was invisible until end-to-end OTA).
 *
 * Bench evidence (2026-05-26 with 0.A.199): controller self-update
 * pushed all 522306 bytes successfully (DAC migration worked), reached
 * VERIFYING + ACTIVATING, then `.1` went OFFLINE and stayed OFFLINE
 * across power-cycles. Root cause: Activate's three header writes left
 * both bank headers wiped in flash; SBL chooser found no valid bank
 * and fell into BOOT ROM. Recovery required JTAG re-flash.
 *
 * Fix: any update to ANY of the three metadata structures (FwBootMeta,
 * Bank A header, Bank B header) rewrites the entire 256-KB block in one
 * atomic transaction. The in-memory cache (`s_boot_meta`, `s_bank_a_hdr`,
 * `s_bank_b_hdr`) is the source of truth; flash is reconstructed from
 * it.
 *
 * 2026-05-29 (0.A.208) — SEVENTH-layer controller self-update brick:
 *   The 0.A.200 design comment claimed "Other 252 KB of the block
 *   (0x40000-0x5FFFF, 0x61000-0x6FFFF, 0x71000-0x7FFFF) stays 0xFF —
 *   currently unused by anything per the canonical OSPI map in
 *   nova_fw_update.h." **That assumption was wrong** until the F2c
 *   stage-2 SBL chooser ships (Gap 6 of docs/lp-am2434-ota-hardening-
 *   plan.md). We currently run TI's stock sbl_ospi.release.hs_fs.tiimage
 *   (~311 KB / 0x4BE2D) at OSPI 0x0. Its tail extends into 0x40000-
 *   0x4BE2D — well inside our metadata block. Every write_meta_block_-
 *   atomic call wiped that SBL tail to 0xFF; next warm-reset, ROM
 *   rejected the truncated SBL and fell into BOOT ROM = Nova OFFLINE
 *   forever. This is what caused the 0.A.200-207 brick saga. Each
 *   individual fix was real but layered on top of this latent SBL-
 *   erase bug; stage-copy verified Bank A bit-perfect, then Activate
 *   wiped SBL.
 *
 *   Fix: relocate FW_HEADER_OFFSET from 0x060000 to 0x300000 (RESERVED
 *   1.5 MB region per the OSPI map — 0x300000-0x4FFFFF was unused).
 *   The 256-KB erase block at 0x300000-0x33FFFF is now completely
 *   outside both SBL (0x0-0x4BE2D) and Bank A (0x80000-0x1FFFFF) and
 *   the watchdog (0x180000). Nothing else needs to change in the erase
 *   logic — block-aligned addresses still work.
 *
 *   Backward compat: existing boards have stale metadata at the OLD
 *   0x060000 location. After the 0.A.208 self-update or JTAG reflash,
 *   NovaFwUpdate_Init reads from the NEW 0x300000 location. First
 *   read is 0xFF (erased) — magic check fails, defaults to Bank A.
 *   That's correct because TI stock SBL hard-loaded Bank A anyway. */
static void write_meta_block_atomic(void)
{
    static uint8_t s_sec_a[FW_SECTOR_SIZE];   /* sector at FW_HEADER_OFFSET = 0x300000 */
    static uint8_t s_sec_b[FW_SECTOR_SIZE];   /* sector at FW_BANK_B_HDR_SECTOR = 0x310000 */

    debug_printf("[FwUpd] write_meta_block: enter\r\n");

    memset(s_sec_a, 0xFF, sizeof(s_sec_a));
    memcpy(s_sec_a + FW_BOOT_META_OFFSET,  &s_boot_meta,  sizeof(s_boot_meta));
    memcpy(s_sec_a + FW_BANK_A_HDR_OFFSET, &s_bank_a_hdr, sizeof(s_bank_a_hdr));

    memset(s_sec_b, 0xFF, sizeof(s_sec_b));
    memcpy(s_sec_b + FW_BANK_B_HDR_OFFSET, &s_bank_b_hdr, sizeof(s_bank_b_hdr));

    /* Erase the 256-KB block. 0.A.208: FW_HEADER_OFFSET = 0x300000, so the
     * containing block is 0x300000-0x33FFFF — well outside SBL (0-0x4BE2D),
     * Bank A (0x80000-0x1FFFFF), and watchdog (0x180000). */
    uint32_t meta_block = FW_HEADER_OFFSET & ~(FW_OTA_ERASE_BLOCK - 1U);
    debug_printf("[FwUpd] write_meta_block: erase block @0x%06lx (DAC-STIG)\r\n",
                 (unsigned long)meta_block);
    int erc = hal_flash_erase_block_dac(meta_block);
    debug_printf("[FwUpd] write_meta_block: erase rc=%d\r\n", erc);

    /* Write both sectors back via DAC. */
    debug_printf("[FwUpd] write_meta_block: write sec_a @0x%06lx (4KB)\r\n",
                 (unsigned long)FW_HEADER_OFFSET);
    int rca = hal_flash_write_dac(FW_HEADER_OFFSET, s_sec_a, sizeof(s_sec_a));
    debug_printf("[FwUpd] write_meta_block: write sec_a rc=%d\r\n", rca);

    debug_printf("[FwUpd] write_meta_block: write sec_b @0x%06lx (4KB)\r\n",
                 (unsigned long)FW_BANK_B_HDR_SECTOR);
    int rcb = hal_flash_write_dac(FW_BANK_B_HDR_SECTOR, s_sec_b, sizeof(s_sec_b));
    debug_printf("[FwUpd] write_meta_block: write sec_b rc=%d\r\n", rcb);

    debug_printf("[FwUpd] write_meta_block: exit\r\n");
}

static void write_bank_header(uint32_t bank, const FwBankHeader *hdr)
{
    /* Update the in-memory cache (some callers pass a pointer that
     * already aliases the cache; that's a self-assignment and safe). */
    if (bank == 0) s_bank_a_hdr = *hdr;
    else           s_bank_b_hdr = *hdr;
    /* Rewrite the entire metadata block from the cache. See
     * write_meta_block_atomic() above for why we can't just write one
     * sector. */
    write_meta_block_atomic();
}

static void read_boot_meta(FwBootMeta *meta)
{
    hal_flash_read(FW_HEADER_OFFSET + FW_BOOT_META_OFFSET,
                   (uint8_t *)meta, sizeof(FwBootMeta));
}

static void write_boot_meta(const FwBootMeta *meta)
{
    s_boot_meta = *meta;
    /* Rewrite the entire metadata block from the cache — see
     * write_meta_block_atomic() above. */
    write_meta_block_atomic();
}

/* Erase `nbytes` (rounded up to sector size) starting at the bank base.
 * Each AM2434 OSPI 4 KB sector erase takes ~40-100 ms.  We yield to the
 * scheduler so LpWatchdog's client task and lwIP's TCP/IP thread keep
 * making progress, but `vTaskDelay(1)` between every sector adds
 * ~1 tick (≈1-2 ms) of pure latency per sector — for a 491 KB image
 * that turns a ~10 s erase into ~110 s.  `taskYIELD()` lets equal-or-
 * higher-priority tasks run without burning a tick boundary.  We still
 * `vTaskDelay(1)` every 16 sectors so any task waiting on a tick
 * (LpWatchdog client) is guaranteed to run.  Pre-scheduler callers
 * (BootValidation from main()) skip both. */
static int erase_bank_range(uint32_t bank, uint32_t nbytes)
{
    if (nbytes == 0) return 0;
    if (nbytes > FW_BANK_MAX_SIZE) return -1;
    uint32_t base = bank_flash_offset(bank);
    /* hal_flash_erase_sector() erases a full 256-KB block (Cypress
     * S25HL512T 4S_4D_4D DTR mode only honors block-erase 0xDC; the
     * 4-KB sector-erase opcode 0x21 leaves the OSPI controller stuck).
     * Round image size up to the next block boundary. */
    const uint32_t BLOCK = 256u * 1024u;
    uint32_t end = (nbytes + BLOCK - 1u) & ~(BLOCK - 1u);
    const bool yield_ok =
        (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED);
    uint32_t blocks_done = 0;
    for (uint32_t off = 0; off < end; off += BLOCK) {
        if (hal_flash_erase_sector(base + off) != 0)
            return -1;
        if (yield_ok) {
            /* 256-KB block erase takes ~150 ms on this chip — yield
             * after every block so the watchdog client gets serviced. */
            (void)blocks_done;
            vTaskDelay(1);
        }
    }
    return 0;
}

static int erase_bank(uint32_t bank)
{
    return erase_bank_range(bank, FW_BANK_MAX_SIZE);
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

    /* 0.A.85: lazy block erase. Begin pre-erases the first block;
     * WriteChunk erases subsequent blocks on demand.
     * 0.A.205: bare-metal STIG erase (was SDK `hal_flash_erase_sector`
     * → `Flash_eraseBlk` which hangs in CPSW-active context). Bench-
     * 2026-05-27 with the controller-only .cfu confirmed: when no
     * orbit OTAs ran first to "warm up" CPSW state, Begin's pre-erase
     * wedges inside Flash_norOspiWaitReady and broker can't ack any
     * chunk (bridge times out at offset 0). Same wedge family as the
     * SDK read/write paths we already swapped to bare-metal DAC/STIG. */
    s_state = FW_STATE_ERASING;
    uint32_t base = bank_flash_offset(s_target_bank);
    debug_printf("[FwUpd] Begin: pre-erase Bank%u @0x%06lx (DAC-STIG)\r\n",
                 (unsigned)s_target_bank, (unsigned long)base);
    if (hal_flash_erase_block_dac(base) != 0) {
        s_state = FW_STATE_ERROR;
        return 3;  /* Erase failed */
    }
    s_erased_through = FW_OTA_ERASE_BLOCK;
    debug_printf("[OTA] BEGIN size=%lu — first block erased at 0x%06lx (lazy-erase, direct write)\r\n",
                 (unsigned long)total_size, (unsigned long)base);

    s_state = FW_STATE_RECEIVING;
    return 0;
}

/* ─── Write chunk ─────────────────────────────────────────────────────── */

uint32_t NovaFwUpdate_WriteChunk(uint32_t offset, const uint8_t *data,
                                  uint32_t len, uint32_t chunk_crc)
{
    if (s_state != FW_STATE_RECEIVING) {
        debug_printf("[FwUpd] WriteChunk: bad state %d off=%lu\r\n",
                     (int)s_state, (unsigned long)offset);
        return 1;  /* Not in receiving state */
    }

    if (offset != s_bytes_written) {
        debug_printf("[FwUpd] WriteChunk: out-of-order off=%lu expect=%lu\r\n",
                     (unsigned long)offset, (unsigned long)s_bytes_written);
        return 2;  /* Out-of-order chunk */
    }

    if (offset + len > s_total_size) {
        debug_printf("[FwUpd] WriteChunk: overrun off=%lu len=%lu total=%lu\r\n",
                     (unsigned long)offset, (unsigned long)len,
                     (unsigned long)s_total_size);
        return 3;  /* Would exceed declared image size */
    }

    /* Verify chunk CRC */
    uint32_t calc = crc32_buf(data, len);
    if (calc != chunk_crc) {
        debug_printf("[FwUpd] WriteChunk: CRC mismatch off=%lu got=0x%08lx want=0x%08lx\r\n",
                     (unsigned long)offset, (unsigned long)calc,
                     (unsigned long)chunk_crc);
        return 4;  /* Chunk CRC mismatch */
    }

    /* 0.A.85: lazy block-erase + direct chunk write. If this chunk
     * would write past the currently-erased range, erase the next
     * block(s) on demand (one block per crossing). Each block-erase
     * yields a tick so LpWatchdog client + lwIP keep ticking. */
    while (offset + len > s_erased_through) {
        if (s_erased_through >= FW_BANK_MAX_SIZE) {
            debug_printf("[FwUpd] WriteChunk: erase head past bank max off=%lu erased=%lu\r\n",
                         (unsigned long)offset, (unsigned long)s_erased_through);
            s_state = FW_STATE_ERROR;
            return 5;
        }
        uint32_t next_block = bank_flash_offset(s_target_bank) + s_erased_through;
        /* 0.A.205: bare-metal STIG erase (see NovaFwUpdate_Begin
         * comment for rationale — SDK Flash_eraseBlk wedges in CPSW). */
        if (hal_flash_erase_block_dac(next_block) != 0) {
            debug_printf("[FwUpd] WriteChunk: lazy erase fail at 0x%06lx\r\n",
                         (unsigned long)next_block);
            s_state = FW_STATE_ERROR;
            return 5;
        }
        s_erased_through += FW_OTA_ERASE_BLOCK;
        if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
            vTaskDelay(1);
        }
    }

    /* 0.A.199: switch the chunk write from SDK `hal_flash_write`
     * (INDIRECT_WRITE_XFER) to `hal_flash_write_dac` (DAC-mode CPU
     * memcpy). The SDK path wedges silently when called with CPSW
     * active — lp_ota_task migrated for exactly this reason in 0.A.187
     * (memories/repo/lp-am2434-ota-dac-mode-fix.md). The controller
     * self-update inherited the broken pattern and bench-2026-05-26
     * confirmed it locks up Nova on the FIRST chunk (UART silence after
     * "[Flash] Write ENTER", broker can't emit FwInstallProgress,
     * bridge times out, requires power-cycle to recover).
     *
     * DAC requires page-aligned addr + len. `flash_addr` is naturally
     * page-aligned (bank_base + offset, both 256-byte multiples in the
     * standard 1024-byte chunk stream). The LAST chunk of an image
     * whose size isn't a 256-multiple is tail-padded with 0xFF in
     * `s_chunk_page_pad` and written as one full page — NOR re-program
     * of 0xFF padding bytes is a no-op (no bit transitions). */
    uint32_t flash_addr        = bank_flash_offset(s_target_bank) + offset;
    uint32_t full_pages_bytes  = (len / NOVA_FLASH_PAGE) * NOVA_FLASH_PAGE;
    uint32_t remainder         = len - full_pages_bytes;

    if (full_pages_bytes > 0u) {
        int rc = hal_flash_write_dac(flash_addr, data, full_pages_bytes);
        if (rc != 0) {
            debug_printf("[FwUpd] WriteChunk: hal_flash_write_dac fail addr=0x%06lx len=%lu rc=%d\r\n",
                         (unsigned long)flash_addr,
                         (unsigned long)full_pages_bytes, rc);
            s_state = FW_STATE_ERROR;
            return 5;
        }
    }
    if (remainder > 0u) {
        memset(s_chunk_page_pad, 0xFF, NOVA_FLASH_PAGE);
        memcpy(s_chunk_page_pad, data + full_pages_bytes, remainder);
        int rc = hal_flash_write_dac(flash_addr + full_pages_bytes,
                                     s_chunk_page_pad, NOVA_FLASH_PAGE);
        if (rc != 0) {
            debug_printf("[FwUpd] WriteChunk: hal_flash_write_dac pad fail addr=0x%06lx remainder=%lu rc=%d\r\n",
                         (unsigned long)(flash_addr + full_pages_bytes),
                         (unsigned long)remainder, rc);
            s_state = FW_STATE_ERROR;
            return 5;
        }
    }

    s_bytes_written += len;
    /* 0.A.89: per-100-chunk progress trace so we can tell from UART
     * whether chunks are landing silently (success) vs the connection
     * is stalling. Without this, chunk_size=16 produces no [Flash]
     * output (single-PP writes log nothing on first-attempt success). */
    {
        static uint32_t s_chunk_count = 0;
        s_chunk_count++;
        if ((s_chunk_count % 100u) == 0u) {
            debug_printf("[FwUpd] chunk progress: count=%lu bytes=%lu/%lu\r\n",
                         (unsigned long)s_chunk_count,
                         (unsigned long)s_bytes_written,
                         (unsigned long)s_total_size);
        }
        if (s_bytes_written == s_total_size) {
            /* Reset on completion so the next OTA session starts clean. */
            s_chunk_count = 0;
        }
    }
    return 0;
}

/* ─── Finalize: verify full image CRC ─────────────────────────────────── */

uint32_t NovaFwUpdate_Finalize(uint32_t expected_crc)
{
    /* 0.A.202: granular bracketing so the next COM9 capture pinpoints
     * the exact line where Finalize wedges on the controller self-
     * update path. Bench-2026-05-26 with 0.A.201 ended at
     * "[OTA-BROKER] ComponentFinalize idx=3" then total UART silence —
     * meaning the hang is in Finalize itself (CRC scan or
     * write_meta_block_atomic), not Activate. Removing once rooted. */
    debug_printf("[FwUpd] Finalize: enter expected_crc=0x%08lx bank=%u total=%lu\r\n",
                 (unsigned long)expected_crc,
                 (unsigned)s_target_bank,
                 (unsigned long)s_total_size);

    if (s_state != FW_STATE_RECEIVING)
        return 1;

    if (s_bytes_written != s_total_size)
        return 2;  /* Incomplete transfer */

    s_state = FW_STATE_VERIFYING;

    /* 0.A.204: read back via `hal_flash_read_dac` (XIP memcpy from
     * 0x60000000+addr) instead of `hal_flash_read_stig`. 0.A.203 swap
     * to STIG fixed the brick (SDK INDIRECT_READ wedge in CPSW
     * context) but bench-2026-05-27 showed STIG reads return DIFFERENT
     * BYTES than what was written: broker accumulated CRC over chunks
     * == 0x05b58d15, Finalize STIG-read CRC == 0xfa07de84. Since both
     * CRC32 algos are identical (Ethernet polynomial, both bit-
     * reflected) and DAC writes are known reliable (orbit OTAs verify
     * Bank-A CRC bit-exact via XIP memcpy), the only remaining
     * suspect is STIG-read returning wrong bytes — possibly the
     * READ4B opcode + 8 dummy cycles don't match the chip's current
     * 4S_4D_4D DTR protocol mode. lp_ota_task's Bank-A CRC scan uses
     * inline XIP memcpy from `0x60000000+addr` and gets bit-exact
     * matches — `hal_flash_read_dac` is the helper-wrapped version of
     * that pattern. The "alternation hangs" caveat in hal.h applies
     * to interleaved read/write loops (stage-copy); Finalize is a
     * single bulk read AFTER all writes are done, so the caveat
     * doesn't apply. */
    uint32_t flash_base = bank_flash_offset(s_target_bank);
    uint32_t crc = 0;
    uint8_t tmp[256];
    uint32_t remaining = s_total_size;
    uint32_t off = 0;

    while (remaining > 0) {
        uint32_t chunk = (remaining > sizeof(tmp)) ? sizeof(tmp) : remaining;
        int rrc = hal_flash_read_dac(flash_base + off, tmp, chunk);
        if (rrc != 0) {
            debug_printf("[FwUpd] Finalize: hal_flash_read_dac fail off=%lu rc=%d\r\n",
                         (unsigned long)off, rrc);
            s_state = FW_STATE_ERROR;
            return 4;  /* Read fail mid-scan */
        }
        crc = crc32_update(crc, tmp, chunk);
        off += chunk;
        remaining -= chunk;
    }

    debug_printf("[FwUpd] Finalize: CRC scan done crc=0x%08lx (expected=0x%08lx)\r\n",
                 (unsigned long)crc, (unsigned long)expected_crc);

    if (crc != expected_crc) {
        debug_printf("[FwUpd] Finalize: CRC MISMATCH — aborting\r\n");
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
    debug_printf("[FwUpd] Finalize: calling write_bank_header(bank=%u, seq=%lu)\r\n",
                 (unsigned)s_target_bank, (unsigned long)hdr.sequence);
    write_bank_header(s_target_bank, &hdr);
    debug_printf("[FwUpd] Finalize: write_bank_header returned\r\n");

    /* Update cached copy */
    if (s_target_bank == 0) s_bank_a_hdr = hdr;
    else                     s_bank_b_hdr = hdr;

    s_state = FW_STATE_VERIFIED;
    debug_printf("[FwUpd] Finalize: exit OK\r\n");
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
        /* 0.A.201: Cypress software reset (RSTEN 0x66 + RST 0x99 via
         * STIG) BEFORE the SoC warm-reset. The chip's volatile config
         * is in 4S_4D_4D DTR mode after `Board_flashOpen` + ~500 KB of
         * DAC writes; the next SBL boot tries to issue setup commands
         * in single-line SDR and fails because the chip isn't listening
         * on the single-line protocol — straight to BOOT ROM, board
         * goes OFFLINE forever (bench-bricked .1 three times before
         * spotting this). RSTEN+RST clears the chip's volatile config
         * and returns it to factory single-line SDR — same state as
         * if we'd power-cycled. Mirror of `lp_ota_task.c:1471-1483`
         * which does the same dance for orbit OTAs.
         *
         * `hal_flash_chip_soft_reset` issues both RSTEN and RST in the
         * current protocol, then re-issues in single-line SDR to catch
         * the case where the chip is already partially reset. Returns
         * 0 on STIG success, -1 on timeout (which we log but do not
         * gate on — soldiering into Sciclient_pmDeviceReset is still
         * better than hanging here). */
        DebugP_log("[FwUpd] chip soft-reset (RSTEN+RST) before warm-reset\r\n");
        int rst_rc = hal_flash_chip_soft_reset();
        DebugP_log("[FwUpd] chip soft-reset rc=%d\r\n", rst_rc);

        /* Give UART one more tick to drain the diagnostic prints before
         * the SoC reset eats the FIFO. (Matches lp_ota_task pattern.) */
        if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }

        /* Warm SoC reset via DMSC. Same path used by:
         *   - lp_am2434/main.c CMD_REBOOT_SOC handler (verified working
         *     2026-05-03, see docs/firmware-version-current.md row 3)
         *   - the JTAG auto-flasher's Step-7 finalisation.
         * Returns only on failure; on success the SoC restarts. */
        extern int32_t Sciclient_pmDeviceReset(uint32_t timeout);
        (void)Sciclient_pmDeviceReset(0xFFFFFFFFU /* SystemP_WAIT_FOREVER */);
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

/* ─── Orbit-OTA path metadata helper ─────────────────────────────────────
 * Used by lp_ota_task.c's FwActivate handler. The orbit OTA path has its
 * own chunk-tracking state machine separate from NovaFwUpdate's state,
 * so we can't just call Finalize+Activate (those expect s_target_bank /
 * s_total_size / etc. to have been set via Begin / WriteChunk). This
 * helper takes the orbit-OTA-tracked values and writes the F2c metadata.
 *
 * See nova_fw_update.h for full contract.
 *
 * Added 2026-05-31 after F2c rollback was proven on silicon (the chooser
 * works; the orbit OTA path just wasn't telling it Bank B was newer). */
uint32_t NovaFwUpdate_OrbitFinalize(uint32_t image_size,
                                    uint32_t image_crc,
                                    const char *version)
{
    /* Build the new Bank B header. */
    FwBankHeader new_b;
    memset(&new_b, 0, sizeof(new_b));
    new_b.magic      = FW_BANK_MAGIC;
    new_b.image_size = image_size;
    new_b.image_crc  = image_crc;
    new_b.valid      = 1U;
    new_b.active     = 1U;

    /* Sequence: strictly higher than both existing banks so the F2c
     * chooser's pick_bank picks Bank B by sequence on next boot.
     *
     * Guard each bank's sequence on magic validity. A blank OSPI sector
     * (all 0xFF, as Write-SeedMetaBlock.ps1 leaves the Bank B sector)
     * loads as sequence=0xFFFFFFFF; using that as `maxSeq` overflows to
     * 0 on `+1`, producing a Bank B header with seq=0 < Bank A's seq=1
     * — chooser then picks A and ignores our new bank. Discovered
     * 2026-05-31: TRITON + GDC hit this because their Bank B sectors
     * stayed blank from the F2c seeding step; STORAGE escaped because
     * yesterday's rollback test left magic=NOVA bytes at 0x310000. */
    uint32_t aSeq = (s_bank_a_hdr.magic == FW_BANK_MAGIC) ? s_bank_a_hdr.sequence : 0U;
    uint32_t bSeq = (s_bank_b_hdr.magic == FW_BANK_MAGIC) ? s_bank_b_hdr.sequence : 0U;
    new_b.sequence = ((aSeq > bSeq) ? aSeq : bSeq) + 1U;

    if (version != NULL) {
        strncpy(new_b.version, version, sizeof(new_b.version) - 1U);
        new_b.version[sizeof(new_b.version) - 1U] = '\0';
    }

    /* Update in-RAM caches that write_meta_block_atomic flushes. */
    s_bank_b_hdr = new_b;
    s_bank_a_hdr.active = 0U;            /* old active bit cleared */
    s_boot_meta.watchdog_strikes = 0U;   /* fresh strike budget for new image */
    s_boot_meta.boot_reason = 0U;        /* normal-boot expected */
    s_active_bank = 1U;                  /* Bank B is the new active bank */

    debug_printf("[FwUpd] OrbitFinalize: seq=%lu valid=1 active=1 size=%lu crc=0x%08lX version='%s'\r\n",
                 (unsigned long)new_b.sequence,
                 (unsigned long)image_size,
                 (unsigned long)image_crc,
                 version ? version : "(null)");

    /* Persist all three (FwBootMeta, Bank A header, Bank B header) in
     * one erase-write cycle of the 256-KB block at 0x300000. */
    write_meta_block_atomic();
    return 0U;
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
        /* 0.A.204: DAC-mode XIP read (was STIG read in 0.A.203 — STIG
         * returns wrong bytes post-DAC-writes; see `NovaFwUpdate_Finalize`
         * comment block for the diagnosis). */
        if (hal_flash_read_dac(flash_base + off, tmp, chunk) != 0) return false;
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
/*
 * Idempotent: writes OSPI only when there's something to clear. The
 * watchdog client (lp_watchdog_client.c) calls this once per 30 s
 * "all alive bits held" milestone, so an unconditional write would
 * grind a flash sector every 30 s of healthy uptime.
 */
void NovaFwUpdate_ConfirmBoot(void)
{
    if (s_boot_meta.watchdog_strikes == 0 && s_boot_meta.boot_reason == 0) {
        return;  /* already healthy */
    }
    s_boot_meta.watchdog_strikes = 0;
    s_boot_meta.boot_reason = 0;
    write_boot_meta(&s_boot_meta);
}
