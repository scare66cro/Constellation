/*
 * hal_flash.c — QSPI Flash HAL for AM2434 (Settings Vault / OTA)
 *
 * Settings + OTA payloads live on the QSPI NOR flash (W25Q64JV)
 * connected to the OSPI controller, memory-mapped at 0x50000000.
 *
 * Implementation uses the TI MCU+ SDK Flash driver for write/erase
 * via OSPI indirect mode. The LP build keeps OSPI in indirect mode
 * (DAC disabled by syscfg, see lp_device_config.c §rationale), so
 * memory-mapped reads from 0x50000000 are NOT used here — every
 * access goes through the driver.
 *
 * Linked into the LP image starting fw 0.A.60 (F2c work, May 2026).
 * Earlier comment said "NOT linked" — that's no longer true. See
 * docs/lp-am2434-f2c-sbl-chooser-design.md for the OTA wiring.
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */

#include "hal.h"     /* declares debug_printf, hal_flash_* prototypes */
#include <string.h>
#include <stdbool.h>
#include <kernel/dpl/HwiP.h>     /* for HwiP_disable / HwiP_restore */
#include <kernel/dpl/CacheP.h>   /* CacheP_wbInv — see hal_flash_dac_pp */
#include <kernel/dpl/DebugP.h>   /* DebugP_log (failure logging) */

/* FreeRTOS scheduler-suspend wrappers around Flash_*: the TI Board
 * Flash driver's `Flash_write` / `Flash_eraseBlk` do internal polling
 * (WIP bit), DMA, and OSPI register manipulation that is not safe to
 * preempt — a task switch in the middle leaves the controller in a
 * state where the call returns SystemP_FAILURE. Empirical: writes
 * succeed in the auto-flasher (NoRTOS) and pre-scheduler init
 * (LpDeviceConfig_Save in *_Init), but always fail when called from
 * a running FreeRTOS task. Wrapping with vTaskSuspendAll/Resume keeps
 * the operation atomic w.r.t. other tasks while still allowing the
 * R5F's IRQs (incl. tick) to fire so the OSPI driver's own
 * interrupt-driven completion still works.
 *
 * NB: this comment described the intended fix for OTA Flash_write but
 * the wrap was never actually implemented until 0.A.111+. The session
 * 0.A.78–0.A.108 OTA debug spiral (`docs/session-resume-ota-flash-
 * write.md`) chased every chip/SDK/peripheral angle but missed the
 * piece the original author had already diagnosed. The actual wrap is
 * now in `hal_flash_write` and `hal_flash_erase_sector` below.
 *
 * Why HwiP_disable was tried first and didn't help: 0.A.87 wrapped the
 * Flash_write call with global IRQ mask. That blocked task preemption
 * BUT also blocked the R5F clock-tick interrupt — `ClockP_usleep`
 * (used internally by the OSPI driver and by our retry-loop sleeps)
 * sits forever waiting for a tick that never comes. vTaskSuspendAll
 * is the right primitive: blocks task switches WITHOUT disabling
 * interrupts, so timers + OSPI + watchdog keep ticking. */
#include "FreeRTOS.h"
#include "task.h"

/* Atomic-write helper: prevent a FreeRTOS task switch from preempting
 * the OSPI driver's polled FIFO-fill loop in `OSPI_lld_writeIndirect`.
 * If the scheduler isn't running yet (pre-scheduler save path), this
 * is a no-op and the call goes through directly. */
static inline void hal_flash_atomic_enter(void)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) {
        vTaskSuspendAll();
    }
}
static inline void hal_flash_atomic_exit(void)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_SUSPENDED) {
        (void)xTaskResumeAll();
    }
}

/* QSPI NOR flash is memory-mapped at this address by OSPI controller */
#define OSPI_DATA_BASE  0x50000000
#define OSPI_FLASH_SIZE (64 * 1024 * 1024) /* 64 MB — Cypress S25HL512T */
#define BLOCK_SIZE      (256 * 1024)        /* 256 KB block-erase granularity */

/* ─── OSPI implementation ─────────────────────────────────────────────── */
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
#include <board/flash/ospi/flash_nor_ospi.h>  /* Flash_NorOspiObject */
#include <drivers/ospi.h>                     /* OSPI_writeCmd, OSPI_WriteCmdParams */
#include <kernel/dpl/SystemP.h>
#include <kernel/dpl/ClockP.h>
/* 0.A.118: `Sciclient_pmSetModuleRst` and the entire OSPI-peripheral-PM-
 * reset path is gone. The 0.A.104 design — recover-on-failure by
 * resetting the OSPI peripheral and chip-NV via the SDK Flash driver —
 * was the wrong approach. The actual fix for the OTA Flash_write
 * problem was scheduler-suspend (0.A.111); chip-NV writes should
 * happen ONLY in the auto-flasher (NoRTOS, JTAG-supervised). */

/* Handle ownership: the SDK populates `gFlashHandle[CONFIG_FLASH0]`
 * during `Board_driversOpen()` (called from main.c before scheduler
 * start). We share that handle rather than calling `Flash_open`
 * ourselves — opening twice produces inconsistent driver state.
 * Same access pattern as `lp_am2434/lp_device_config.c`. */
#include "ti_drivers_open_close.h"
#include "ti_board_open_close.h"   /* CONFIG_FLASH0, gFlashHandle[] */
extern Flash_Handle gFlashHandle[];

static inline Flash_Handle flash_handle(void)
{
    return gFlashHandle[CONFIG_FLASH0];
}

/* 0.A.91: chip software reset (Cypress S25HL512T). Sends RSTEN (0x66)
 * + RST (0x99) via raw OSPI writeCmd. Used at hal_flash_init so any
 * wedged state from a previous session (e.g. WIP stuck high after a
 * failed multi-PP Flash_write trapped post-scheduler in our OTA code)
 * is cleared before any new flash operations.
 *
 * Why this matters: `Flash_norOspiWaitReady` (`flash_nor_ospi.c:138`)
 * loops `while ((status != SUCCESS) || (timeOut > 0))`. If `cmdRead`
 * fails (chip not responding), `status != SUCCESS` is permanently true
 * and the loop NEVER exits regardless of the timeOut counter. SDK bug.
 * Once the chip wedges, every subsequent Flash_write call hangs
 * indefinitely. Software reset clears the chip's volatile state +
 * status register, restoring responsiveness.
 *
 * RSTEN+RST is special: the chip accepts these commands even when
 * busy. Per Cypress datasheet, ~30 µs delay after RST before chip is
 * usable again (we wait 1 ms to be safe). */
/* 0.A.98: clear Cypress S25HL512T's program/erase failure flags
 * (CLPEF = command 0x30). After a failed Page Program (e.g. our
 * post-scheduler wedge), the chip latches PRGERR=1 in SR1 and
 * REJECTS all subsequent PPs until this command runs. The SDK's
 * Flash_norOspiWrite does WREN + PP but never clears these flags,
 * and Flash_quirkQSPIEarlyFixup only sends CLPEF when SafeBoot
 * detection triggers (which our state may not satisfy). Issuing
 * CLPEF before each Flash_write attempt unblocks PPs from this
 * sticky-error state. CLPEF requires no WREN, no addr, no data —
 * just the opcode. ~few µs to complete. */
static void hal_flash_clear_errors(Flash_Handle h)
{
    if (h == NULL) return;
    Flash_Config *cfg = (Flash_Config *)h;
    Flash_NorOspiObject *obj = (Flash_NorOspiObject *)cfg->object;
    if (obj == NULL || obj->ospiHandle == NULL) return;

    OSPI_WriteCmdParams p;
    OSPI_WriteCmdParams_init(&p);
    p.cmd          = 0x30;            /* CLPEF — clear PRGERR/ERSERR */
    p.cmdAddr      = OSPI_CMD_INVALID_ADDR;
    p.numAddrBytes = 0;
    p.txDataBuf    = NULL;
    p.txDataLen    = 0;
    int32_t st = OSPI_writeCmd(obj->ospiHandle, &p);
    debug_printf("[Flash] CLPEF issued st=%ld\r\n", (long)st);
}

/* 0.A.118 — REMOVED: `hal_flash_try_nv_recovery` and
 * `hal_flash_force_chip_dtr`.
 *
 * Both wrote to the Cypress S25HL512T's non-volatile / volatile
 * configuration registers (CFR2N/CFR3N/CFR4N via WRRSB, CFR2V via
 * WRAR) as part of `hal_flash_write`'s retry envelope. They were
 * added in 0.A.107/108/115 as a guess at recovering from the OTA
 * Flash_write wedge — but the actual root cause was preemption (fixed
 * with `vTaskSuspendAll` in 0.A.111), not chip-side protocol drift.
 *
 * 0.A.115's `hal_flash_try_nv_recovery` shipped with a typo in the
 * supposedly-factory-default bytes: `CFR3N=0x00` instead of `0x08`.
 * When the OTA Flash_write retry path tripped this function on a live
 * board, the chip's NV registers were rewritten to a state where SBL's
 * `Flash_open` hangs forever and the LP can't boot. The dev CONTROLLER
 * (S24L0707) was bricked this way on 2026-05-11; software recovery via
 * JTAG-direct OSPI register pokes is non-trivial because the OSPI
 * peripheral needs DMSC-managed clock + syscfg init that's hard to
 * reproduce purely from a DSS script.
 *
 * The fix here is to stop touching the chip's NV/V config registers
 * from runtime firmware AT ALL. Recovery from a failed Flash_write is:
 *
 *   1. Suspend scheduler (already done — `hal_flash_atomic_enter`)
 *   2. Retry the same Flash_write call
 *   3. If still failing, abort the OTA — Bank A is still valid, so
 *      the customer is no worse off
 *
 * Chip-level recovery (if a chip is genuinely in SafeBoot or wedged
 * by something else) is the auto-flasher's job, not the runtime
 * firmware's. The auto-flasher runs NoRTOS, is JTAG-loaded, and has
 * a human watching the bench — that's the right place for risky
 * operations on chip config registers. */

static uint8_t hal_flash_read_sr1(Flash_Handle h)
{
    uint8_t sr1 = 0xFF;
    if (h == NULL) return sr1;
    Flash_Config *cfg = (Flash_Config *)h;
    Flash_NorOspiObject *obj = (Flash_NorOspiObject *)cfg->object;
    if (obj == NULL || obj->ospiHandle == NULL) return sr1;

    OSPI_ReadCmdParams p;
    OSPI_ReadCmdParams_init(&p);
    p.cmd          = 0x05;            /* RDSR — Read Status Register 1 */
    p.cmdAddr      = OSPI_CMD_INVALID_ADDR;
    p.numAddrBytes = 0;
    p.dummyBits    = 0;
    p.rxDataBuf    = &sr1;
    p.rxDataLen    = 1;
    (void)OSPI_readCmd(obj->ospiHandle, &p);
    return sr1;
}

/* 0.A.119: read the Cypress S25HL512T's volatile config registers via
 * RDAR (cmd 0x65). CFR2V[1]=QPI_EN, CFR2V[7]=DTR_EN, CFR2V[6:5]=ADP/lvl.
 * Used at hal_flash_write entry to log the chip's current protocol mode
 * vs what the OSPI controller thinks it's sending.
 *
 * Read-only — zero brick risk. Uses the controller's current protocol
 * for STIG (whatever Flash_open set up). If the chip is in 4S-4D-4D
 * DTR, RDAR also goes out as DTR; chip should respond. If chip is in
 * a mismatched mode, the read returns garbage (0xFF) which is itself
 * diagnostic. */
static uint8_t hal_flash_read_cfr(Flash_Handle h, uint32_t reg_addr)
{
    uint8_t v = 0xFF;
    if (h == NULL) return v;
    Flash_Config *cfg = (Flash_Config *)h;
    Flash_NorOspiObject *obj = (Flash_NorOspiObject *)cfg->object;
    if (obj == NULL || obj->ospiHandle == NULL) return v;

    OSPI_ReadCmdParams p;
    OSPI_ReadCmdParams_init(&p);
    p.cmd          = 0x65;            /* RDAR — Read Any Register */
    p.cmdAddr      = reg_addr;
    p.numAddrBytes = obj->numAddrBytes;
    p.dummyBits    = 8;               /* 1 dummy byte in non-XIP mode */
    p.rxDataBuf    = &v;
    p.rxDataLen    = 1;
    (void)OSPI_readCmd(obj->ospiHandle, &p);
    return v;
}

static void hal_flash_chip_reset(void)
{
    /* 0.A.92: pre-scheduler safe — no ClockP_usleep (which can fault
     * pre-scheduler in some SDK builds). Use a calibrated busy-wait. */
    extern void bb_uart0_puts(const char *s);
    bb_uart0_puts("[BB] chip_reset: enter\r\n");

    Flash_Handle h = flash_handle();
    if (h == NULL) { bb_uart0_puts("[BB] chip_reset: NULL handle\r\n"); return; }
    Flash_Config *cfg = (Flash_Config *)h;
    Flash_NorOspiObject *obj = (Flash_NorOspiObject *)cfg->object;
    if (obj == NULL) { bb_uart0_puts("[BB] chip_reset: NULL obj\r\n"); return; }
    if (obj->ospiHandle == NULL) { bb_uart0_puts("[BB] chip_reset: NULL ospi\r\n"); return; }

    OSPI_WriteCmdParams p;
    OSPI_WriteCmdParams_init(&p);
    p.cmd          = 0x66;            /* RSTEN — Reset Enable */
    p.cmdAddr      = OSPI_CMD_INVALID_ADDR;
    p.numAddrBytes = 0;
    p.txDataBuf    = NULL;
    p.txDataLen    = 0;
    (void)OSPI_writeCmd(obj->ospiHandle, &p);
    bb_uart0_puts("[BB] chip_reset: RSTEN sent\r\n");

    OSPI_WriteCmdParams_init(&p);
    p.cmd          = 0x99;            /* RST — Reset Memory */
    p.cmdAddr      = OSPI_CMD_INVALID_ADDR;
    p.numAddrBytes = 0;
    p.txDataBuf    = NULL;
    p.txDataLen    = 0;
    (void)OSPI_writeCmd(obj->ospiHandle, &p);
    bb_uart0_puts("[BB] chip_reset: RST sent\r\n");

    /* Cypress tRPH ≤ 35 µs typical. Calibrated busy-wait at 800 MHz =
     * 800 cycles/µs × 1500 µs = 1.2M cycles. Volatile counter so the
     * compiler doesn't optimize the loop away. */
    volatile uint32_t spin = 1200000u;
    while (spin--) { /* busy-wait ~1.5 ms */ }
    bb_uart0_puts("[BB] chip_reset: done\r\n");
}

void hal_flash_init(void)
{
    /* 0.A.118: hal_flash_init is a no-op. The runtime firmware does
     * NOT touch chip NV registers or do chip-level recovery — that's
     * the auto-flasher's responsibility. */
    (void)hal_flash_chip_reset;     /* kept as a dead reference */
}

int hal_flash_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    if (addr + len > OSPI_FLASH_SIZE) return -1;
    Flash_Handle h = flash_handle();
    if (h == NULL) return -1;
    /* LP build keeps OSPI in indirect mode (DAC disabled by syscfg)
     * so memory-mapped reads from 0x50000000 are unsafe. Always go
     * through the driver. */
    int32_t st = Flash_read(h, addr, buf, len);
    return (st == SystemP_SUCCESS) ? 0 : -1;
}

int hal_flash_write(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    /* 0.A.84: override SDK pageSize from 256 to 16 on first call.
     *
     * Bench evidence: 16-byte Page Programs through Flash_write succeed
     * post-scheduler (0.A.73 variant-D at 0x700000; 0.A.79's first slice
     * at 0x200000); 256-byte (full-page) Page Programs fail (0.A.78
     * chunk 0; 0.A.82 256 KiB flush at 0x200000; 0.A.83 same at
     * 0x400000). The SDK's `Flash_norOspiWrite` walks any-size buffer
     * in `attrs->pageSize`-sized PPs, so dropping pageSize from 256 to
     * 16 makes every internal PP a 16-byte transaction — the size we
     * have proof works on this Cypress S25HL512T 4S_4D_4D DTR mode.
     * Cost: ~16x more PP commands per write; tPP is still the
     * bottleneck (~200 µs each), so a 256 KiB block flush takes
     * ~3-4 s instead of ~0.2 s. Acceptable for OTA. The chip itself
     * accepts PPs of 1-256 bytes per command — pageSize is a SDK
     * abstraction, not a chip constraint. */
    /* 0.A.94: pageSize override REMOVED. 0.A.93 with pageSize=16 wedged
     * on the very first 16-byte Flash_write — the override itself
     * appears to break Flash_norOspiWrite somehow (auto-flasher works
     * with default pageSize=256). For chunk_size=16 with default
     * pageSize=256, the FIRST chunk at addr=0x400000 is 256-aligned so
     * passes the SDK alignment check, and chunkLen reduces to 16
     * internally (matches variant-D's known-working pattern). Chained
     * chunks at 0x400010, 0x400020 etc. would FAIL alignment (16 % 256
     * != 0); we need to find a different way to handle that — TBD.
     * For 0.A.94 the first chunk should at least succeed cleanly,
     * letting us measure how the FIRST single-PP write behaves on
     * a clean chip. */
    extern Flash_DevConfig gFlashDevCfg_S25HL512T;
    static bool s_busy_clamp_done = false;
    if (!s_busy_clamp_done) {
        gFlashDevCfg_S25HL512T.flashBusyTimeout = 100000;
        s_busy_clamp_done = true;
        debug_printf("[Flash] busyTimeout clamp: 6000000 -> 100000\r\n");
    }

    /* 0.A.82: simplified to a single Flash_write call (with retry).
     * Trust the TI SDK's Flash_write to handle multi-page programs on
     * the Cypress S25HL512T — `flasher_uart` writes up to 256 KiB per
     * call against the same handle and succeeds. The 0.A.65 page-split
     * commentary applied to a different chip (W25Q64JV) and is stale.
     *
     * Caller contract has tightened: every callsite must issue exactly
     * ONE hal_flash_write per Flash_eraseBlk cycle on its target block.
     * 0.A.79 bench evidence proved that consecutive Flash_writes after
     * one erase trip a "second call fails for all retries" trap on this
     * chip — the only known-good consumers (`lp_device_config::Save`,
     * `flasher_uart::do_erase_write_verify`) follow the one-write-per-
     * erase rule. OTA now matches that via the block buffer in
     * `Platform/nova_fw_update.c`; bank-header writes are 4 KiB sectors
     * already (one write per erase). The retry loop here only covers
     * the post-erase first-write transient (variant-D evidence: ~20 ms
     * gap, 1-2 attempts). */
    if (addr + len > OSPI_FLASH_SIZE) return -1;
    Flash_Handle h = flash_handle();
    if (h == NULL) return -1;

    /* 0.A.88: HwiP_disable removed. 0.A.87 bench-confirmed it does
     * NOT help (same chunk-0 fail with same 5-attempt envelope) AND
     * leaves the chip in a wedged state where subsequent Flash_eraseBlk
     * times out — likely because the long IRQ-masked window prevents
     * any chip-recovery sequence from running. The trap is genuinely
     * chip-side: WIP gets stuck high after some PP. Reverted to clean
     * retry loop; pageSize=16 override above is retained. */
    /* 0.A.90: log only the first 3 hal_flash_write calls per session so
     * a wedged Flash_write is visible on UART without flooding 30,000+
     * lines for chunk_size=16. After 3, only the FAIL log below fires
     * (only on actual failure). If we see the 1st ENTER but no 1st EXIT,
     * we're stuck inside Flash_write's polling loop. */
    static uint32_t s_log_enter_count = 0;
    bool log_this = (s_log_enter_count < 3u);
    if (log_this) {
        s_log_enter_count++;
        debug_printf("[Flash] Write ENTER #%lu addr=0x%06lX len=%lu\r\n",
                     (unsigned long)s_log_enter_count,
                     (unsigned long)addr, (unsigned long)len);
    }
    /* 0.A.118 — brick-safe retry path.
     *
     * Three plain retries with the scheduler-suspend wrap. NO chip-NV
     * register writes, NO `Flash_close+Flash_open`, NO peripheral PM
     * reset, NO `Sciclient_pmSetModuleRst`. The 0.A.107-0.A.115 recovery
     * envelope bricked the dev CONTROLLER on 2026-05-11 because the
     * "recovery" code mutated chip non-volatile registers with values
     * that made SBL `Flash_open` hang on next boot.
     *
     * Failure mode for the runtime path is now: "Flash_write failed
     * three times → return -1 → caller (OTA) aborts → customer's Bank
     * A is still valid, no harm done". Chip-level recovery is the
     * auto-flasher's job, not the runtime firmware's.
     *
     * Kept the OSPI controller register snapshot diagnostic (CTRL /
     * SRAM_FILL / NUM_BYTES before+after attempt 1) — those are read-
     * only, can't brick anything, and are still useful for working out
     * what the SDK is actually doing inside `Flash_write`. */
    #define HAL_FLASH_OSPI_BASE 0x0FC40000UL
    #define HAL_FLASH_OSPI_CONFIG      (*(volatile uint32_t *)(HAL_FLASH_OSPI_BASE + 0x00U))
    #define HAL_FLASH_OSPI_DEV_INSTR_RD_CFG (*(volatile uint32_t *)(HAL_FLASH_OSPI_BASE + 0x04U))
    #define HAL_FLASH_OSPI_DEV_INSTR_WR_CFG (*(volatile uint32_t *)(HAL_FLASH_OSPI_BASE + 0x08U))
    #define HAL_FLASH_OSPI_IND_WR_CTRL (*(volatile uint32_t *)(HAL_FLASH_OSPI_BASE + 0x70U))
    #define HAL_FLASH_OSPI_IND_WR_NUM  (*(volatile uint32_t *)(HAL_FLASH_OSPI_BASE + 0x7CU))
    #define HAL_FLASH_OSPI_SRAM_FILL   (*(volatile uint32_t *)(HAL_FLASH_OSPI_BASE + 0x2CU))

    int32_t status = SystemP_FAILURE;
    int attempts_used = 0;
    for (int attempt = 0; attempt < 3; attempt++) {
        uint32_t ctrl_pre  = 0;
        uint32_t sram_pre  = 0;
        if (attempt == 0) {
            ctrl_pre = HAL_FLASH_OSPI_IND_WR_CTRL;
            sram_pre = HAL_FLASH_OSPI_SRAM_FILL;
        }

        /* 0.A.120: clear stale DONE_STATUS + CANCEL bits in CTRL_REG.
         * Bench confirmed: pre-clear shows ctrl_pre=0x00, but post-call
         * ctrl_post still=0x60 — controller still asserts DONE late,
         * stale state wasn't the cause. Kept for hygiene though. */
        {
            uint32_t clear =
                (1u << 5) |   /* IND_OPS_DONE_STATUS_FLD (write-1-to-clear) */
                (1u << 1);    /* CANCEL_FLD                                 */
            HAL_FLASH_OSPI_IND_WR_CTRL = clear;
        }

        /* Layered isolation around the SDK Flash_write call:
         *   vTaskSuspendAll  → blocks FreeRTOS task switches (0.A.111)
         *   HwiP_disable     → masks all CPU IRQs (0.A.121)
         *
         * Both are brick-safe (no chip writes; SDK uses spin-loops, not
         * sleep, so masking IRQs doesn't break SDK timing).
         *
         * 0.A.122 added a third layer (`lp_enet_dma_pause`) that HW-paused
         * CPSW TX+RX UDMA channels via `Udma_chPause`. Bench-verified
         * working (`rx_ch_pause_rc=0 numRxCh=1`) but DID NOT fix the
         * runtime Flash_write failure — confirmed CPSW DMA activity is
         * not the cause. Removed from this hot path on 2026-05-12 to
         * avoid runtime cost (~few µs per retry); helper stays in
         * `Nova_Firmware/lp_am2434/lp_enet_dma_pause.c` as reference for
         * any future "isolate the OSPI peripheral" code.
         *
         * Customer OTA is now via the bridge-side auto-flasher path, not
         * via this runtime path — see
         * `memories/repo/lp-am2434-runtime-flashwrite-unresolved.md`. */
        uint32_t hwi_key = HwiP_disable();
        hal_flash_atomic_enter();
        status = Flash_write(h, addr, (uint8_t *)buf, len);
        hal_flash_atomic_exit();
        HwiP_restore(hwi_key);
        attempts_used = attempt + 1;
        if (attempt == 0) {
            uint32_t ctrl_post = HAL_FLASH_OSPI_IND_WR_CTRL;
            uint32_t sram_post = HAL_FLASH_OSPI_SRAM_FILL;
            uint32_t num_bytes = HAL_FLASH_OSPI_IND_WR_NUM;
            uint8_t sr_after = hal_flash_read_sr1(h);

            /* 0.A.119 diagnostic: dump controller's R/W protocol setup +
             * chip's volatile config registers, to test the "chip is in
             * DTR but AM2434 OSPI controller hardware can't do DTR for
             * writes (no DDR_EN bit in DEV_INSTR_WR_CONFIG_REG, unlike
             * DEV_INSTR_RD_CONFIG_REG)" hypothesis.
             *
             * Decode keys:
             *   DEV_INSTR_RD_CFG:
             *     [7:0]   RD_OPCODE        — expect cmd 0xED (Cypress 4DTR Read)
             *     [9:8]   INSTR_TYPE       — protocol of opcode (0=1S, 2=4S)
             *     [10]    DDR_EN           — 1 = DTR clocking for read
             *     [13:12] ADDR_XFER_TYPE   — 0=1S, 2=4S
             *     [17:16] DATA_XFER_TYPE   — 0=1S, 2=4S
             *     [28:24] DUMMY_RD_CYCLES
             *   DEV_INSTR_WR_CFG:
             *     [7:0]   WR_OPCODE        — expect 0x02 (Cypress PP)
             *     [13:12] ADDR_XFER_TYPE   — 0=1S, 2=4S
             *     [17:16] DATA_XFER_TYPE   — 0=1S, 2=4S
             *     NB: NO DDR_EN bit exists for the WR path.
             *   CFR2V (Cypress S25HL512T volatile config):
             *     [0]     CFR2V_TBPROT     — top/bot protect direction
             *     [1]     CFR2V_QPI_EN     — 1 = QPI enabled (4-bit-wide cmds)
             *     [6:5]   ADP/lvl
             *     [7]     CFR2V_DTR_EN     — 1 = DTR enabled (chip clocks on both edges)
             *
             * If CFR2V[7]=1 (chip in DTR) but DEV_INSTR_WR_CFG has no
             * way to express DTR (which it doesn't), every PP we send
             * is clocked out SDR while the chip expects DTR → silent
             * reject. That's the exact failure mode `ctrl_post=0x60`
             * (controller-clocks-DONE / chip-doesn't-program). */
            uint32_t cfg_reg   = HAL_FLASH_OSPI_CONFIG;
            uint32_t rd_cfg    = HAL_FLASH_OSPI_DEV_INSTR_RD_CFG;
            uint32_t wr_cfg    = HAL_FLASH_OSPI_DEV_INSTR_WR_CFG;
            uint8_t  cfr2v     = hal_flash_read_cfr(h, 0x800003U);
            uint8_t  cfr3v     = hal_flash_read_cfr(h, 0x800004U);
            uint8_t  cfr4v     = hal_flash_read_cfr(h, 0x800005U);

            debug_printf("[Flash] BARE attempt1 sdk=%ld sr1=0x%02X "
                         "ctrl_pre=0x%08lX ctrl_post=0x%08lX "
                         "sram_pre=0x%08lX sram_post=0x%08lX "
                         "num_bytes=0x%08lX\r\n",
                         (long)status, sr_after,
                         (unsigned long)ctrl_pre, (unsigned long)ctrl_post,
                         (unsigned long)sram_pre, (unsigned long)sram_post,
                         (unsigned long)num_bytes);
            debug_printf("[Flash] CTRL_MODE cfg=0x%08lX rd_cfg=0x%08lX wr_cfg=0x%08lX  "
                         "chip cfr2v=0x%02X cfr3v=0x%02X cfr4v=0x%02X  "
                         "obj.numAddrBytes=%u obj.currentProtocol=%u\r\n",
                         (unsigned long)cfg_reg,
                         (unsigned long)rd_cfg,
                         (unsigned long)wr_cfg,
                         cfr2v, cfr3v, cfr4v,
                         (unsigned)((Flash_NorOspiObject *)((Flash_Config *)h)->object)->numAddrBytes,
                         (unsigned)((Flash_NorOspiObject *)((Flash_Config *)h)->object)->currentProtocol);
        } else {
            uint8_t sr_after = hal_flash_read_sr1(h);
            debug_printf("[Flash] retry %d sdk=%ld sr1_after=0x%02X\r\n",
                         attempt + 1, (long)status, sr_after);
        }
        if (status == SystemP_SUCCESS) break;
        ClockP_usleep(100000);
    }
    /* 0.A.101: post-failure read-back — if data landed despite FAIL,
     * we know the SDK is reporting false failure. Read-only, safe. */
    if (status != SystemP_SUCCESS) {
        uint8_t verify[16] = {0};
        if (Flash_read(h, addr, verify, sizeof(verify)) == SystemP_SUCCESS) {
            debug_printf("[Flash] post-fail verify @0x%06lX: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
                         (unsigned long)addr,
                         verify[0], verify[1], verify[2], verify[3],
                         verify[4], verify[5], verify[6], verify[7]);
        }
    }
    if (log_this) {
        debug_printf("[Flash] Write EXIT  #%lu addr=0x%06lX len=%lu st=%ld attempts=%d\r\n",
                     (unsigned long)s_log_enter_count,
                     (unsigned long)addr, (unsigned long)len,
                     (long)status, attempts_used);
    }
    if (status != SystemP_SUCCESS) {
        debug_printf("[Flash] Write FAIL addr=0x%06lX len=%lu err=%ld attempts=%d\r\n",
                     (unsigned long)addr, (unsigned long)len,
                     (long)status, attempts_used);
        return -1;
    }
    if (attempts_used > 1) {
        debug_printf("[Flash] Write OK addr=0x%06lX len=%lu attempts=%d\r\n",
                     (unsigned long)addr, (unsigned long)len, attempts_used);
    }
    return 0;
}

int hal_flash_erase_sector(uint32_t addr)
{
    if (addr >= OSPI_FLASH_SIZE) return -1;
    Flash_Handle h = flash_handle();
    if (h == NULL) return -1;

    /* Align to 256-KB block boundary. */
    addr &= ~(BLOCK_SIZE - 1);

    /* IMPORTANT — use Flash_eraseBlk (256 KB block, cmd 0xDC), NOT
     * Flash_eraseSector (4 KB sector, cmd 0x20/0x21).
     *
     * The Cypress S25HL512T running in 4S_4D_4D DTR mode (per syscfg
     * `flash1.peripheralDriver`) accepts the 0xDC block-erase opcode
     * but the 0x20/0x21 sector-erase opcodes leave the OSPI controller
     * in a stuck state where every subsequent Flash_write returns
     * SystemP_FAILURE immediately (no chip-busy timeout). The chip
     * itself is also unresponsive to Flash_readId until power-cycle.
     *
     * The auto-flasher (`flasher_uart/main.c`) and `lp_device_config.c`
     * both use Flash_eraseBlk against the same OSPI handle and write
     * successfully. OTA is the third flash consumer; we use the same
     * proven pattern. */
    /* 0.A.118: brick-safe retry — three attempts with the scheduler-
     * suspend wrap, no Flash_close/open dance, no CLPEF-style chip
     * commands. If erase fails three times, return -1 and let the OTA
     * caller decide what to do (probably: abort and leave Bank A
     * running). */
    uint32_t blk_num = 0, page_num = 0;
    if (Flash_offsetToBlkPage(h, addr, &blk_num, &page_num) != SystemP_SUCCESS) {
        return -1;
    }
    int32_t status = SystemP_FAILURE;
    for (int attempt = 0; attempt < 3; attempt++) {
        /* Scheduler suspend prevents a FreeRTOS tick from preempting
         * the SDK's polled WIP-clear loop inside Flash_eraseBlk. */
        hal_flash_atomic_enter();
        status = Flash_eraseBlk(h, blk_num);
        hal_flash_atomic_exit();
        if (status == SystemP_SUCCESS) break;
        ClockP_usleep(50000);
    }
    if (status != SystemP_SUCCESS) {
        debug_printf("[Flash] Erase failed at 0x%06lX blk=%lu err=%ld\r\n",
                     (unsigned long)addr, (unsigned long)blk_num, (long)status);
        return -1;
    }
    return 0;
}

/* ─── DAC-mode Page Program runtime write path ───────────────────────────
 *
 * The SDK's `Flash_norOspiWrite` / `OSPI_lld_writeIndirect` (INDIRECT_WRITE_XFER
 * subsystem) is BROKEN in our FreeRTOS+CPSW+lwIP runtime context — the
 * controller reports `DONE_STATUS=1` with no error but the chip's WEL
 * stays set and the target page reads 0xFF. The same SDK call works in
 * the NoRTOS auto-flasher context against the same chip + same address.
 * 4 days of forensic investigation across 18 firmware iterations
 * (0.A.118 → 0.A.139) ruled out every plausible cause (CPU IRQs, task
 * switches, CPSW UDMA HW, driver state, peripheral PM reset,
 * `WRITE_COMPLETION_CTRL_REG` state, IRQ_STATUS, SRAM_PARTITION).
 *
 * The fix that works: bypass `INDIRECT_WRITE_XFER` entirely and use the
 * OSPI controller's DAC (Direct Access Controller) mode. CPU memcpy to
 * `0x60000000 + flash_addr` lets the controller intercept AHB writes and
 * turn them into PP commands at the chip via `DEV_INSTR_WR_CONFIG`. The
 * critical piece our bare-metal version was missing initially: a
 * `CacheP_wbInv` after the memcpy to flush dirty cache lines out of the
 * CPU data cache (the OSPI region is mapped Normal Cacheable for fast XIP
 * reads). The SDK's `OSPI_writeDirect` does this — see `ospi_v0.c:994`.
 * Without it, CPU writes sit in cache and never reach the controller.
 *
 * Bench-verified 0.A.140: 128 pages (32 KB) programmed in 73.5 ms with
 * 5 sample-byte readbacks matching source. See
 * `memories/repo/lp-am2434-ota-dac-mode-fix.md`. */

#define HAL_OSPI_BASE              0x0FC40000u
#define HAL_OSPI_DATA_BASE         0x60000000u  /* CSL_FSS0_DAT_REG1_BASE */

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

#define HAL_FLASH_PAGE_SIZE                  256U
#define HAL_FLASH_BLOCK_SIZE                 (256U * 1024U)  /* 4-byte block-erase: 0xDC */
#define HAL_FLASH_BLOCK_ERASE_CMD            0xDCU

/* 0.A.172: BACK to 32 B sub-PP. 0.A.171 dropped to 16 B and the chip
 * wedged on the FIRST CHUNK PP — "WIP stuck after PP" + subsequent
 * erases also failing. 16 B is a PARTIAL cache line (R5F line = 32 B),
 * and writing < line causes the cache controller's write-allocate to
 * READ-then-WRITE the line, which during DAC mode triggers a read of
 * the OSPI region in the middle of an in-progress PP cycle. That
 * read confuses both the controller's pipeline and the chip's WIP
 * state. So 32 B = 1 cache line is the FLOOR — anything smaller
 * triggers cache-allocate hazards. With HwiP_disable now added in
 * the stage-copy iter loop (0.A.171 lp_ota_task.c change kept), the
 * 8 residual mismatches from 0.A.170's 32 B run may finally clear. */
#define HAL_FLASH_DAC_SUB_PP                 32U
/* SRAM_PARTITION_REG (offset 0x18) — diagnostic, lets us read what
 * the SDK Cypress driver set for the read/write split. */
#define HAL_OSPI_SRAM_PARTITION_REG          (HAL_OSPI_BASE + 0x18)

static inline uint32_t hal_rd32(uint32_t addr)            { return *(volatile uint32_t *)(uintptr_t)addr; }
static inline void     hal_wr32(uint32_t addr, uint32_t v){ *(volatile uint32_t *)(uintptr_t)addr = v; }

/* Spin until STIG command completes (CMD_EXEC_STATUS clears) AND the
 * controller reaches IDLE state. The IDLE wait mirrors the SDK's
 * `OSPI_flashExecCmd` behavior — without it, switching from STIG path
 * to DAC mode while the controller is still clocking the tail of the
 * prior STIG garbles the next PP. */
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

/* Issue a STIG command with no address and no data payload (WREN cmd 0x06,
 * RSTEN cmd 0x66, RST cmd 0x99, etc.). */
static int hal_stig_cmd(uint8_t opcode)
{
    uint32_t ctrl = ((uint32_t)opcode << 24) | HAL_STIG_CMD_EXEC;
    hal_wr32(HAL_OSPI_FLASH_CMD_CTRL, ctrl);
    return hal_stig_wait();
}

/* Issue a STIG read command (RDSR cmd 0x05 with n_bytes=1 returns SR1).
 * Returns FLASH_RD_DATA_LOWER on success or -1 on timeout. Caller masks
 * the low byte. */
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

/* Poll RDSR(cmd 0x05) until WIP bit (SR1[0]) clears. Up to ~250 ms wait
 * for in-progress PP. Returns final SR1 (≥0) or -1 on timeout. */
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

/* Single sub-page DAC-mode Page Program. Programs up to
 * HAL_FLASH_DAC_SUB_PP bytes from `src` to flash `addr`. Each call is
 * one full PP cycle (WREN + DAC write + WIP poll).
 *
 * ─── LOAD-BEARING INVARIANT (see docs/lp-am2434-ospi-dac-writes.md) ──
 * Do NOT disable the DAC bit (CONFIG_REG[7]) or clear
 * IND_AHB_ADDR_TRIGGER_REG at the end of this function. The SDK's
 * `OSPI_lld_writeDirect` (drivers/ospi/v0/lld/ospi_v0_lld.c:1615)
 * leaves both enabled across calls — and that's load-bearing.
 *
 * If you toggle the DAC bit OFF between sub-PPs, the AM2434 OSPI
 * controller silently drops all AHB writes past the first 5 32-bit
 * words (20 bytes) of every sub-PP from the second onward. The chip
 * still sees PP cmd + addr (WIP cycles "normally") but receives zero
 * data — the just-erased page stays at 0xFF except for the first
 * sub-PP's first 20 bytes. Three days of debugging (0.A.166-0.A.185)
 * found this by adding per-chunk verify with byte-dump diagnostic.
 *
 * Enable DAC lazily on the first call (`if cfg_pre & DAC_BIT == 0`);
 * leave it on for the rest of the write campaign. Toggle off only
 * after ALL writes are done and you need INDIRECT mode for the next
 * op (rare in practice). */
static int hal_flash_dac_pp_one(uint32_t addr, const uint8_t *src, uint32_t len)
{
    /* Wait for chip idle (no prior write/erase in progress). */
    int32_t sr1_pre = hal_wait_wip_clear(50000U);
    if (sr1_pre < 0) return -1;

    /* WREN STIG; verify WEL set. */
    if (hal_stig_cmd(0x06) != 0) return -1;
    int32_t sr1_post_wren = hal_stig_read(0x05, 1U);
    if (sr1_post_wren < 0) return -1;
    if ((((uint8_t)sr1_post_wren) & 0x02U) == 0U) return -1;

    /* Lazy DAC enable — see big comment above. NEVER disable at end. */
    uint32_t cfg_pre = hal_rd32(HAL_OSPI_CONFIG_REG);
    if ((cfg_pre & HAL_OSPI_CFG_ENB_DIR_ACC_CTLR_BIT) == 0U) {
        hal_wr32(HAL_OSPI_CONFIG_REG, cfg_pre | HAL_OSPI_CFG_ENB_DIR_ACC_CTLR_BIT);
        hal_wr32(HAL_OSPI_IND_AHB_ADDR_TRIGGER, 0x04000000U);
    }
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
    /* DAC + IND_AHB_ADDR_TRIGGER stay enabled — see big comment above. */

    /* Wait for chip's PP to complete (WIP=0). */
    int32_t sr1_after = hal_wait_wip_clear(50000U);
    if (sr1_after < 0) return -3;
    /* WEL should now be 0 (the PP consumed it). */
    if (((uint8_t)sr1_after) & 0x02U) return -4;

    return 0;
}

/* Page-level DAC PP. Splits a 256-byte page into TWO 128-byte sub-PPs
 * to fit within the OSPI controller's SRAM write partition. 0.A.166/167
 * bench showed corruption at byte 216 of every affected page — the
 * SRAM write partition is ~54 32-bit words (~216 bytes), and DAC writes
 * past that boundary get silently dropped. Splitting into 128-byte
 * sub-PPs keeps each below the limit. Functionally equivalent to one
 * 256-byte PP since Cypress S25HL512T supports 1-256 byte PPs that
 * each program contiguous bytes from the given address.
 *
 * Return codes match hal_flash_dac_pp_one. */
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

/* Defensive guard: clear any stuck INDIRECT_WRITE_XFER state from prior
 * runtime attempts. Idempotent — no-op when the register is clean. */
static void hal_flash_cancel_stuck_indirect_write(void)
{
    hal_wr32(HAL_OSPI_INDIRECT_WR_CTRL, HAL_INDWR_CANCEL | HAL_INDWR_OPS_DONE);
}

/* Public version of the above — call BEFORE `hal_flash_erase_sector` on
 * a runtime path that may follow earlier failed writes. Empirically the
 * SDK's `Flash_eraseBlk` can hang inside `Flash_norOspiWaitReady` if the
 * OSPI controller's INDIRECT_WRITE_XFER state machine is mid-transfer.
 * The 0.A.140 OTA flow had this write inline in `lp_ota_task.c` and it
 * was load-bearing for erase reliability. Exposed as a HAL API so the
 * OTA activate handler can call it without owning the register defs.
 *
 * 0.A.158: now also clears INDIRECT_READ_XFER_CTRL_REG (offset 0x60).
 * The stage-copy alternation pattern (SDK Flash_read → DAC write → ...)
 * was hanging after ~50 iters; the read side was leaving DONE_STATUS
 * latched between calls, which can interact badly with a DAC-mode
 * write that comes next. Cancel + W1C the read CTRL too. Same write-1
 * semantics as the write path. */
#define HAL_OSPI_INDIRECT_RD_CTRL            (HAL_OSPI_BASE + 0x60)
#define HAL_INDRD_START                      (1U << 0)
#define HAL_INDRD_CANCEL                     (1U << 1)
#define HAL_INDRD_OPS_DONE                   (1U << 5)

void hal_flash_clear_indirect_state(void)
{
    hal_flash_cancel_stuck_indirect_write();
    hal_wr32(HAL_OSPI_INDIRECT_RD_CTRL, HAL_INDRD_CANCEL | HAL_INDRD_OPS_DONE);
}

/* Cypress S25HL512T software reset — clears volatile config and returns
 * chip to power-on factory state (single-line SDR). Required before
 * a warm-reset on the AM2434 LP so the next SBL boot can talk to the
 * chip using its single-line setup commands. Without this, the chip
 * stays in QPI+DTR from the prior `Flash_open` and SBL's reads return
 * garbage (boot fail with "Some tests have failed!!").
 *
 * 0.A.164: dual-protocol attempt. The STIG path follows the controller's
 * DEV_INSTR_RD_CONFIG protocol. After `Flash_open`, that's 4-line DTR
 * for the Cypress in QPI+DTR mode. We:
 *   1. Send RSTEN+RST in the current (4-line DTR) protocol — catches
 *      the chip-still-in-QPI-DTR case (the common case).
 *   2. Switch DEV_INSTR_RD_CONFIG to single-line SDR, then send
 *      RSTEN+RST again — catches the chip-already-in-1S-SDR case
 *      (in case attempt 1 somehow already reset it).
 *   3. Bump the post-reset wait to 10 ms (>> tRPH max for Cypress).
 *
 * One of the two attempts is guaranteed to match the chip's current
 * mode and trigger the reset. After return, the chip is in
 * single-line SDR mode, ready for SBL's fresh setup. */
#define HAL_OSPI_DEV_INSTR_RD_CFG_REG  (HAL_OSPI_BASE + 0x04)
#define HAL_OSPI_DEV_INSTR_WR_CFG_REG  (HAL_OSPI_BASE + 0x08)

/* Cypress CLPEF (Clear Program/Erase Failure flags). Cmd 0x30.
 * Clears CR1V[6:5] (PRGERR/ERSERR). Required between retry attempts of
 * the same PP — after a partial-write fault, the chip latches PRGERR
 * and silently rejects subsequent PPs (manifests as WIP-stuck on the
 * second attempt) until CLPEF runs.
 *
 * 0.A.179: uses SDK `OSPI_writeCmd` instead of our raw STIG. The SDK
 * version goes through the proper protocol-aware command path that's
 * known to work with the chip in QPI+DTR mode. Our raw STIG cmd may
 * have been sent in wrong protocol and silently ignored. */
int hal_flash_clpef(void)
{
    Flash_Handle h = flash_handle();
    if (h == NULL) return -1;
    Flash_Config *cfg = (Flash_Config *)h;
    Flash_NorOspiObject *obj = (Flash_NorOspiObject *)cfg->object;
    if (obj == NULL || obj->ospiHandle == NULL) return -1;

    OSPI_WriteCmdParams p;
    OSPI_WriteCmdParams_init(&p);
    p.cmd          = 0x30;
    p.cmdAddr      = OSPI_CMD_INVALID_ADDR;
    p.numAddrBytes = 0;
    p.txDataBuf    = NULL;
    p.txDataLen    = 0;
    int32_t st = OSPI_writeCmd(obj->ospiHandle, &p);
    /* Read SR1 back so we can log whether PRGERR / ERSERR actually
     * cleared. CR1V[6] = PRGERR, CR1V[5] = ERSERR. SR1 is a different
     * register — but reading something forces a settling pause and
     * helps diagnose. */
    uint8_t sr1 = hal_flash_read_sr1(h);
    debug_printf("[Flash] CLPEF st=%ld sr1=0x%02X\r\n", (long)st, sr1);
    return (st == SystemP_SUCCESS) ? 0 : -1;
}

int hal_flash_chip_soft_reset(void)
{
    /* Clear any stale STIG state. */
    if (hal_stig_wait() != 0) return -1;

    /* ── Attempt 1: send reset in current protocol (4-line DTR) ── */
    (void)hal_stig_cmd(0x04);   /* WRDI — best-effort, ignore status */
    (void)hal_stig_cmd(0x66);   /* RSTEN */
    (void)hal_stig_cmd(0x99);   /* RST */

    /* Short wait — let the chip's tRPH complete before next attempt. */
    {
        volatile uint32_t spin = 1200000U;
        while (spin--) { /* ~1.5 ms */ }
    }

    /* ── Attempt 2: switch controller to single-line SDR, retry reset ── */
    uint32_t rd_cfg_saved = hal_rd32(HAL_OSPI_DEV_INSTR_RD_CFG_REG);
    /* Build a clean single-line SDR read config:
     *   [7:0]   RD_OPCODE        = 0x03 (READ — works in any mode)
     *   [9:8]   INSTR_TYPE       = 0   (single-line)
     *   [10]    DDR_EN           = 0   (SDR)
     *   [13:12] ADDR_XFER_TYPE   = 0
     *   [17:16] DATA_XFER_TYPE   = 0
     *   [28:24] DUMMY_RD_CYCLES  = 0   (no dummies for cmd 0x03)
     * Rest of bits stay 0. */
    hal_wr32(HAL_OSPI_DEV_INSTR_RD_CFG_REG, 0x00000003U);

    (void)hal_stig_cmd(0x04);   /* WRDI in 1S SDR */
    (void)hal_stig_cmd(0x66);   /* RSTEN in 1S SDR */
    (void)hal_stig_cmd(0x99);   /* RST in 1S SDR */

    /* Long wait — make sure chip has fully recovered before warm-reset
     * triggers ROM to start poking it. tRPH max is conservatively
     * 100 ms after a program/erase, but we did WRDI so we should be
     * well under that. 10 ms is comfortable margin. */
    {
        volatile uint32_t spin = 8000000U;
        while (spin--) { /* ~10 ms */ }
    }

    /* Restore the read config for any caller that might do more ops
     * (though the OTA path warm-resets immediately after). */
    hal_wr32(HAL_OSPI_DEV_INSTR_RD_CFG_REG, rd_cfg_saved);

    /* Clear OSPI controller state too — leave it as close to power-on
     * as we can manage so the post-reset SBL doesn't inherit weird
     * state. (Sciclient_pmDeviceReset should reset the peripheral
     * registers, but this is belt-and-suspenders.) */
    uint32_t cfg = hal_rd32(HAL_OSPI_CONFIG_REG);
    hal_wr32(HAL_OSPI_CONFIG_REG, cfg & ~HAL_OSPI_CFG_ENB_DIR_ACC_CTLR_BIT);
    hal_wr32(HAL_OSPI_IND_AHB_ADDR_TRIGGER, 0U);
    hal_wr32(HAL_OSPI_INDIRECT_WR_CTRL, HAL_INDWR_CANCEL | HAL_INDWR_OPS_DONE);
    hal_wr32(HAL_OSPI_INDIRECT_RD_CTRL, HAL_INDRD_CANCEL | HAL_INDRD_OPS_DONE);

    return 0;
}

/* Bare-metal flash read via STIG (Stig = Single Transaction Instruction
 * Generator). Bypasses INDIRECT_READ_XFER entirely. Stage-copy from
 * 0.A.154-0.A.157 saw repeated hangs alternating SDK `Flash_read`
 * (INDIRECT_READ_XFER) with `hal_flash_write_dac` (DAC mode). The
 * controller accumulates state between mode switches that wedges around
 * iter 9 (256 B chunks) or iter 48 (4 KB chunks).
 *
 * STIG uses FLASH_CMD_CTRL_REG (offset 0x90) — completely separate code
 * path from INDIRECT_READ_XFER. STIG reads complete entirely within a
 * single MMIO write, so they can't leave any "in-progress" state for the
 * next call to trip on. Per-STIG payload is 1-8 bytes (FLASH_RD_DATA_LOWER
 * = 4 bytes + FLASH_RD_DATA_UPPER = 4 more bytes).
 *
 * Throughput: ~5 µs per STIG × 8 bytes = ~1.6 MB/s. A 494 KB stage-copy
 * source read takes ~315 ms — slower than SDK Flash_read but never wedges.
 *
 * Opcode 0x13 is the Cypress S25HL512T's READ4B (single-line SDR fast
 * read with 4-byte address, 8 dummy cycles). The chip accepts this even
 * when in 4S-4D-4D DTR mode for its READ command — but the STIG path
 * uses the controller's configured STIG protocol (which the SDK leaves
 * matching the chip's response mode). Empirically RDSR via STIG works
 * for us already (cmd 0x05 in `hal_stig_read`), so opcode 0x13 should
 * work via the same path. */
#define HAL_OSPI_FLASH_RD_DATA_UPPER         (HAL_OSPI_BASE + 0xA4)
#define HAL_STIG_CMD_OPCODE_READ4B           0x13U

static int32_t hal_stig_read_n(uint8_t opcode, uint32_t addr,
                               uint8_t n_dummy_cycles, uint8_t n_bytes,
                               uint8_t *out)
{
    if (n_bytes == 0U || n_bytes > 8U || out == NULL) return -1;

    hal_wr32(HAL_OSPI_FLASH_CMD_ADDR, addr);

    uint32_t ctrl = ((uint32_t)opcode << 24)
                  | HAL_STIG_CMD_ENB_READ_DATA
                  | (((n_bytes - 1U) & 0x7U) << 20)
                  | HAL_STIG_CMD_ENB_COMD_ADDR
                  | (((4U - 1U) & 0x3U) << 16)   /* 4-byte address */
                  | (((uint32_t)n_dummy_cycles & 0x1FU) << 7)
                  | HAL_STIG_CMD_EXEC;

    hal_wr32(HAL_OSPI_FLASH_CMD_CTRL, ctrl);
    if (hal_stig_wait() != 0) return -1;

    uint32_t lo = hal_rd32(HAL_OSPI_FLASH_RD_DATA_LOWER);
    uint32_t hi = (n_bytes > 4U) ? hal_rd32(HAL_OSPI_FLASH_RD_DATA_UPPER) : 0U;

    for (uint32_t i = 0; i < n_bytes; i++) {
        if (i < 4U) out[i] = (uint8_t)(lo >> (8U * i));
        else        out[i] = (uint8_t)(hi >> (8U * (i - 4U)));
    }
    return 0;
}

int hal_flash_read_stig(uint32_t addr, uint8_t *buf, uint32_t len)
{
    if (buf == NULL) return -1;
    if (len == 0U) return 0;
    if ((addr + len) > OSPI_FLASH_SIZE) return -1;

    /* Defensive: clear any stale INDIRECT state before issuing STIG reads.
     * The chip should be idle, but other ops may have left flags set. */
    hal_flash_clear_indirect_state();

    uint32_t off = 0;
    while (off < len) {
        uint32_t chunk = (len - off > 8U) ? 8U : (len - off);
        if (hal_stig_read_n(HAL_STIG_CMD_OPCODE_READ4B,
                            addr + off,
                            8U,    /* 8 dummy cycles for fast-read */
                            (uint8_t)chunk,
                            buf + off) != 0) {
            return -1;
        }
        off += chunk;
    }
    return 0;
}

/* Bare-metal 256 KB block erase via STIG. Bypasses the SDK's
 * `Flash_eraseBlk` which hangs inside `Flash_norOspiWaitReady` (the
 * `||` vs `&&` bug we documented) when called with CPSW + lwIP
 * running. Our STIG helpers use bounded poll counts so they can't
 * infinite-loop. This is the erase equivalent of `hal_flash_write_dac`
 * — the streaming OTA path uses it to pre-erase Bank B at BEGIN time
 * without closing Enet.
 *
 * Returns:
 *   0  success — block erased, chip clean
 *   -1 WIP/STIG timeout pre-erase, or WREN didn't set WEL
 *   -3 WIP stuck after erase (chip didn't finish)
 *   -4 WEL stuck (chip rejected the erase command)
 *
 * `addr` MUST be block-aligned (multiple of 256 KB). */
int hal_flash_read_dac(uint32_t addr, uint8_t *buf, uint32_t len)
{
    if (buf == NULL) return -1;
    if (len == 0U) return 0;
    if ((addr + len) > OSPI_FLASH_SIZE) return -1;

    /* Enable DAC mode (XIP read window) if it's currently disabled.
     * The OSPI controller's DEV_INSTR_RD_CONFIG was already set up
     * by Flash_open's protocol-enable sequence (4S-4D-4D DTR) so
     * reads via the data window return real chip bytes. */
    uint32_t cfg_pre = hal_rd32(HAL_OSPI_CONFIG_REG);
    bool dac_was_off = ((cfg_pre & HAL_OSPI_CFG_ENB_DIR_ACC_CTLR_BIT) == 0);
    if (dac_was_off) {
        hal_wr32(HAL_OSPI_CONFIG_REG, cfg_pre | HAL_OSPI_CFG_ENB_DIR_ACC_CTLR_BIT);
        hal_wr32(HAL_OSPI_IND_AHB_ADDR_TRIGGER, 0x04000000U);
    }

    /* Invalidate the cache range we're about to read so we get fresh
     * bytes from flash (not stale cached values from prior reads). */
    void *src = (void *)(uintptr_t)(HAL_OSPI_DATA_BASE + addr);
    CacheP_wbInv(src, len, CacheP_TYPE_ALL);

    memcpy(buf, src, len);

    if (dac_was_off) {
        hal_wr32(HAL_OSPI_CONFIG_REG, cfg_pre);
        hal_wr32(HAL_OSPI_IND_AHB_ADDR_TRIGGER, 0U);
    }
    return 0;
}

int hal_flash_erase_block_dac(uint32_t addr)
{
    if ((addr % HAL_FLASH_BLOCK_SIZE) != 0U) return -1;
    if (addr >= OSPI_FLASH_SIZE) return -1;

    /* Unlike hal_flash_write_dac, we do NOT `vTaskSuspendAll` here.
     * A block erase takes 150 ms typical / 2.7 s max, and the
     * streaming OTA path runs this WITH CPSW + lwIP active — if we
     * suspend the scheduler for 900 ms, the bridge's TCP connection
     * times out and gets reset (0.A.145 bench evidence: ECONNRESET
     * 21 s after BEGIN, no chunks received). The bounded STIG poll
     * loops are safe to be preempted; STIG operations are atomic at
     * the controller register level (each is a single MMIO write).
     * The only real hazard would be another task issuing a flash op
     * concurrently, but no other runtime path does that on the LP. */

    hal_flash_cancel_stuck_indirect_write();

    /* Wait for chip idle (no prior op in progress). */
    int32_t sr1_pre = hal_wait_wip_clear(50000U);
    if (sr1_pre < 0) return -1;

    /* WREN STIG; verify WEL set. */
    if (hal_stig_cmd(0x06) != 0) return -1;
    int32_t sr1_wel = hal_stig_read(0x05, 1U);
    if (sr1_wel < 0) return -1;
    if ((((uint8_t)sr1_wel) & 0x02U) == 0U) return -1;

    /* Issue block-erase command 0xDC with 4-byte address.
     * FLASH_CMD_ADDR_REG holds the target address; FLASH_CMD_CTRL_REG
     * gets opcode + addr-bytes count (4-1=3 in [17:16]) + ENB_COMD_ADDR
     * + EXEC. The chip's WIP goes 1 immediately, drops to 0 when
     * the block erase finishes (~150 ms typ / 2.7 s max). */
    hal_wr32(HAL_OSPI_FLASH_CMD_ADDR, addr);
    uint32_t ctrl = ((uint32_t)HAL_FLASH_BLOCK_ERASE_CMD << 24)
                  | HAL_STIG_CMD_ENB_COMD_ADDR
                  | (((4U - 1U) & 0x3U) << 16)
                  | HAL_STIG_CMD_EXEC;
    hal_wr32(HAL_OSPI_FLASH_CMD_CTRL, ctrl);
    if (hal_stig_wait() != 0) return -1;

    /* Wait for chip's erase to complete. Bounded poll count caps the
     * wait at ~25 s worst case — but the call is preemptible so
     * lwIP / CPSW / scheduler tick continue running during the wait. */
    int32_t sr1_after = hal_wait_wip_clear(5000000U);

    if (sr1_after < 0) return -3;
    if (((uint8_t)sr1_after) & 0x02U) return -4;
    return 0;
}

int hal_flash_write_dac(uint32_t addr, const uint8_t *src, uint32_t len)
{
    /* Page-alignment requirements: chip's PP is 256-byte page-bounded;
     * crossing a page boundary in one PP wraps to the start of the same
     * page (programs the wrong bytes). Callers must page-align. */
    if (src == NULL) return -1;
    if (len == 0U) return 0;
    if ((addr % HAL_FLASH_PAGE_SIZE) != 0U) return -1;
    if ((len  % HAL_FLASH_PAGE_SIZE) != 0U) return -1;
    if ((addr + len) > OSPI_FLASH_SIZE) return -1;

    /* Defensive: clear any stuck WR_STATUS/OPS_DONE from prior attempts. */
    hal_flash_cancel_stuck_indirect_write();

    int fail_rc = 0;
    uint32_t fail_addr = 0;
    uint32_t pages_done = 0;

    hal_flash_atomic_enter();
    uint32_t off = 0;
    while (off < len) {
        int rc = hal_flash_dac_pp(addr + off, src + off, HAL_FLASH_PAGE_SIZE);
        if (rc != 0) {
            fail_rc = rc;
            fail_addr = addr + off;
            break;
        }
        off += HAL_FLASH_PAGE_SIZE;
        pages_done++;
    }
    hal_flash_atomic_exit();

    if (fail_rc != 0) {
        const char *reason =
            (fail_rc == -1) ? "WIP/STIG timeout or WREN didn't take" :
            (fail_rc == -3) ? "WIP stuck after PP" :
            (fail_rc == -4) ? "WEL stuck (PP rejected by chip)" :
                              "unknown";
        debug_printf("[Flash-DAC] FAIL @0x%06lX page#%lu rc=%d (%s)\r\n",
                     (unsigned long)fail_addr,
                     (unsigned long)(pages_done + 1U),
                     fail_rc,
                     reason);
        return fail_rc;
    }
    return 0;
}
