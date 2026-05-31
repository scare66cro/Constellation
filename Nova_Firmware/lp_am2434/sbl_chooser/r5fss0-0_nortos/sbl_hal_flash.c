/*
 * sbl_hal_flash.c — bare-metal STIG + DAC OSPI flash helpers for the SBL chooser.
 *
 * NoRTOS port of `Platform/hal_flash.c`'s erase_block_dac + write_dac chunks.
 * See sbl_hal_flash.h for the rationale (negative rollback test bench
 * 2026-05-31 — SDK Flash_write/Flash_eraseBlk silently no-op on the
 * Cypress S25HL512T even in the SBL's pre-CPSW context).
 *
 * Differences from the Platform/ source:
 *   1. No FreeRTOS (single-threaded NoRTOS, no atomic_enter/exit suspension).
 *   2. No debug_printf (bare DebugP_log on UART0 for SBL boot trace).
 *   3. No HwiP_disable (no IRQs run in SBL context).
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */

#include "sbl_hal_flash.h"
#include <stdbool.h>
#include <stddef.h>
#include <kernel/dpl/CacheP.h>
#include <kernel/dpl/DebugP.h>

/* ─── OSPI controller register map (AM2434, CSL_FSS0_OSPI_PIPE_CFG_BASE) ──── */
#define HAL_OSPI_BASE                        0x0FC40000u
#define HAL_OSPI_DATA_BASE                   0x60000000u  /* CSL_FSS0_DAT_REG1_BASE */

#define HAL_OSPI_CONFIG_REG                  (HAL_OSPI_BASE + 0x00)
#define HAL_OSPI_IND_AHB_ADDR_TRIGGER        (HAL_OSPI_BASE + 0x1C)
#define HAL_OSPI_INDIRECT_WR_CTRL            (HAL_OSPI_BASE + 0x70)
#define HAL_OSPI_FLASH_CMD_CTRL              (HAL_OSPI_BASE + 0x90)
#define HAL_OSPI_FLASH_CMD_ADDR              (HAL_OSPI_BASE + 0x94)
#define HAL_OSPI_FLASH_RD_DATA_LOWER         (HAL_OSPI_BASE + 0xA0)

#define HAL_OSPI_CFG_ENB_DIR_ACC_CTLR_BIT    (1U << 7)
#define HAL_OSPI_CONFIG_IDLE_BIT             (1U << 31)
#define HAL_STIG_CMD_EXEC                    (1U << 0)
#define HAL_STIG_CMD_EXEC_STATUS             (1U << 1)
#define HAL_STIG_CMD_ENB_COMD_ADDR           (1U << 19)
#define HAL_STIG_CMD_ENB_READ_DATA           (1U << 23)
#define HAL_INDWR_CANCEL                     (1U << 1)
#define HAL_INDWR_OPS_DONE                   (1U << 5)

#define HAL_FLASH_BLOCK_ERASE_CMD            0xDCU
#define HAL_FLASH_DAC_SUB_PP                 32U   /* see Platform/hal_flash.c comment block */

/* ─── MMIO helpers ─────────────────────────────────────────────────────── */

static inline uint32_t hal_rd32(uint32_t addr)             { return *(volatile uint32_t *)(uintptr_t)addr; }
static inline void     hal_wr32(uint32_t addr, uint32_t v) { *(volatile uint32_t *)(uintptr_t)addr = v; }

/* ─── STIG helpers (lifted from Platform/hal_flash.c) ──────────────────── */

static int hal_stig_wait(void)
{
    uint32_t i;
    for (i = 0; i < 200000U; i++) {
        if ((hal_rd32(HAL_OSPI_FLASH_CMD_CTRL) & HAL_STIG_CMD_EXEC_STATUS) == 0) break;
    }
    if (i == 200000U) return -1;
    for (i = 0; i < 200000U; i++) {
        if ((hal_rd32(HAL_OSPI_CONFIG_REG) & HAL_OSPI_CONFIG_IDLE_BIT) != 0) return 0;
    }
    return -1;
}

static int hal_stig_cmd(uint8_t opcode)
{
    uint32_t ctrl = ((uint32_t)opcode << 24) | HAL_STIG_CMD_EXEC;
    hal_wr32(HAL_OSPI_FLASH_CMD_CTRL, ctrl);
    return hal_stig_wait();
}

static int32_t hal_stig_read(uint8_t opcode, uint8_t n_bytes)
{
    uint32_t ctrl = ((uint32_t)opcode << 24)
                  | HAL_STIG_CMD_ENB_READ_DATA
                  | (((n_bytes - 1U) & 0x7U) << 20)
                  | HAL_STIG_CMD_EXEC;
    hal_wr32(HAL_OSPI_FLASH_CMD_CTRL, ctrl);
    if (hal_stig_wait() != 0) return -1;
    return (int32_t)hal_rd32(HAL_OSPI_FLASH_RD_DATA_LOWER);
}

static int32_t hal_wait_wip_clear(uint32_t max_polls)
{
    for (uint32_t i = 0; i < max_polls; i++) {
        int32_t v = hal_stig_read(0x05, 1U);
        if (v < 0) return -1;
        uint8_t sr1 = (uint8_t)v;
        if ((sr1 & 0x01U) == 0) return (int32_t)sr1;
        for (volatile uint32_t d = 0; d < 1000U; d++) { /* ~us */ }
    }
    return -1;
}

static void hal_flash_cancel_stuck_indirect_write(void)
{
    hal_wr32(HAL_OSPI_INDIRECT_WR_CTRL, HAL_INDWR_CANCEL | HAL_INDWR_OPS_DONE);
}

/* ─── DAC sub-PP (lifted from Platform/hal_flash.c::hal_flash_dac_pp_one) ─ */

static int hal_flash_dac_pp_one(uint32_t addr, const uint8_t *src, uint32_t len)
{
    /* Wait for chip idle. */
    int32_t sr1_pre = hal_wait_wip_clear(50000U);
    if (sr1_pre < 0) return -1;

    /* WREN, verify WEL set. */
    if (hal_stig_cmd(0x06) != 0) return -1;
    int32_t sr1_post_wren = hal_stig_read(0x05, 1U);
    if (sr1_post_wren < 0) return -1;
    if ((((uint8_t)sr1_post_wren) & 0x02U) == 0U) return -1;

    /* Lazy DAC enable. CRITICAL: do NOT disable DAC between sub-PPs
     * (per docs/lp-am2434-ospi-dac-writes.md) — toggling silently
     * drops AHB writes past first 5 32-bit words of each subsequent
     * sub-PP. Cost 3 days of bench debug in 0.A.166-185. */
    uint32_t cfg_pre = hal_rd32(HAL_OSPI_CONFIG_REG);
    if ((cfg_pre & HAL_OSPI_CFG_ENB_DIR_ACC_CTLR_BIT) == 0U) {
        hal_wr32(HAL_OSPI_CONFIG_REG, cfg_pre | HAL_OSPI_CFG_ENB_DIR_ACC_CTLR_BIT);
        hal_wr32(HAL_OSPI_IND_AHB_ADDR_TRIGGER, 0x04000000U);
    }

    /* Word-by-word AHB store into DAC window. memcpy emits a burst that
     * the controller's DAC path doesn't accept reliably; explicit word
     * stores work (0.A.185 bench). */
    void *dst = (void *)(uintptr_t)(HAL_OSPI_DATA_BASE + addr);
    {
        volatile uint32_t *d32 = (volatile uint32_t *)dst;
        const uint8_t *s8 = src;
        uint32_t words = len / 4U;
        for (uint32_t w = 0; w < words; w++) {
            uint32_t v = ((uint32_t)s8[4U * w + 0])
                       | ((uint32_t)s8[4U * w + 1] << 8)
                       | ((uint32_t)s8[4U * w + 2] << 16)
                       | ((uint32_t)s8[4U * w + 3] << 24);
            d32[w] = v;
        }
        uint32_t tail = len - words * 4U;
        if (tail > 0U) {
            volatile uint8_t *d8 = (volatile uint8_t *)dst;
            for (uint32_t b = 0; b < tail; b++) {
                d8[words * 4U + b] = s8[words * 4U + b];
            }
        }
    }
    __asm__ volatile("dsb sy" : : : "memory");
    CacheP_wbInv(dst, len, CacheP_TYPE_ALL);
    __asm__ volatile("dsb sy" : : : "memory");

    /* WIP poll until PP completes. */
    int32_t sr1_after = hal_wait_wip_clear(50000U);
    if (sr1_after < 0) return -3;
    if (((uint8_t)sr1_after) & 0x02U) return -4;

    return 0;
}

/* Page-level PP: splits each 256-B page into HAL_FLASH_DAC_SUB_PP-sized
 * sub-PPs to stay below the OSPI SRAM write-partition limit (~216 B).
 * Bench evidence in 0.A.166/167 — DAC writes past that boundary get
 * silently dropped. */
static int hal_flash_dac_pp(uint32_t addr, const uint8_t *src, uint32_t len)
{
    uint32_t off = 0;
    while (off < len) {
        uint32_t sub_len = (len - off > HAL_FLASH_DAC_SUB_PP)
                           ? HAL_FLASH_DAC_SUB_PP : (len - off);
        int rc = hal_flash_dac_pp_one(addr + off, src + off, sub_len);
        if (rc != 0) return rc;
        off += sub_len;
    }
    return 0;
}

/* ─── Public API ─────────────────────────────────────────────────────── */

int sbl_hal_flash_erase_block(uint32_t addr)
{
    if ((addr % SBL_HAL_FLASH_BLOCK_SIZE) != 0U) return -1;

    hal_flash_cancel_stuck_indirect_write();

    /* Wait for chip idle. */
    int32_t sr1_pre = hal_wait_wip_clear(50000U);
    if (sr1_pre < 0) return -1;

    /* WREN, verify WEL set. */
    if (hal_stig_cmd(0x06) != 0) return -1;
    int32_t sr1_wel = hal_stig_read(0x05, 1U);
    if (sr1_wel < 0) return -1;
    if ((((uint8_t)sr1_wel) & 0x02U) == 0U) return -1;

    /* Issue block-erase 0xDC with 4-byte address. */
    hal_wr32(HAL_OSPI_FLASH_CMD_ADDR, addr);
    uint32_t ctrl = ((uint32_t)HAL_FLASH_BLOCK_ERASE_CMD << 24)
                  | HAL_STIG_CMD_ENB_COMD_ADDR
                  | (((4U - 1U) & 0x3U) << 16)
                  | HAL_STIG_CMD_EXEC;
    hal_wr32(HAL_OSPI_FLASH_CMD_CTRL, ctrl);
    if (hal_stig_wait() != 0) return -1;

    /* Wait for erase completion (~150 ms typical, 2.7 s max). */
    int32_t sr1_after = hal_wait_wip_clear(5000000U);
    if (sr1_after < 0) return -3;
    if (((uint8_t)sr1_after) & 0x02U) return -4;
    return 0;
}

int sbl_hal_flash_write(uint32_t addr, const uint8_t *src, uint32_t len)
{
    if (src == NULL) return -1;
    if (len == 0U) return 0;
    if ((addr % SBL_HAL_FLASH_PAGE_SIZE) != 0U) return -1;
    if ((len  % SBL_HAL_FLASH_PAGE_SIZE) != 0U) return -1;

    /* Capture DAC state on entry so we can restore it on exit.
     *
     * 2026-05-31 bench evidence: leaving DAC enabled after our writes
     * BRICKED STORAGE because main.c expects DAC disabled across
     * `Bootloader_parseAndLoadMultiCoreELF` (DAC is enabled later at
     * main.c:243 after the parse-and-load completes). The runtime
     * nova_lp doesn't hit this — it stays in app context where DAC
     * mode is uniformly on. We have to be polite to the SDK's
     * follow-on image load.
     *
     * BUT — within this call we still must NEVER toggle DAC between
     * sub-PPs (per docs/lp-am2434-ospi-dac-writes.md — 0.A.166-185
     * saga). So we enable lazily inside hal_flash_dac_pp_one, keep
     * enabled across all sub-PPs of this campaign, then disable here
     * at the very end only if it was off when we entered. */
    uint32_t cfg_at_entry = hal_rd32(HAL_OSPI_CONFIG_REG);
    bool dac_was_off_on_entry = ((cfg_at_entry & HAL_OSPI_CFG_ENB_DIR_ACC_CTLR_BIT) == 0);

    hal_flash_cancel_stuck_indirect_write();

    int rc_final = 0;
    uint32_t off = 0;
    while (off < len) {
        int rc = hal_flash_dac_pp(addr + off, src + off, SBL_HAL_FLASH_PAGE_SIZE);
        if (rc != 0) {
            DebugP_log("[SBL-FLASH] PP fail @0x%06X page#%u rc=%d\r\n",
                       (unsigned)(addr + off), (unsigned)(off / SBL_HAL_FLASH_PAGE_SIZE), rc);
            rc_final = rc;
            break;
        }
        off += SBL_HAL_FLASH_PAGE_SIZE;
    }

    /* Restore DAC state. After all sub-PPs are done, no more writes to
     * the AHB data window are in flight — safe to disable. */
    if (dac_was_off_on_entry) {
        uint32_t cfg_now = hal_rd32(HAL_OSPI_CONFIG_REG);
        hal_wr32(HAL_OSPI_CONFIG_REG, cfg_now & ~HAL_OSPI_CFG_ENB_DIR_ACC_CTLR_BIT);
        hal_wr32(HAL_OSPI_IND_AHB_ADDR_TRIGGER, 0U);
    }
    return rc_final;
}
