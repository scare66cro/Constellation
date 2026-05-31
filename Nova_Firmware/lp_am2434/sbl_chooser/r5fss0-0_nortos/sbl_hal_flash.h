/*
 * sbl_hal_flash.h — bare-metal STIG + DAC OSPI flash helpers for the F2c SBL chooser.
 *
 * NoRTOS port of the runtime nova_lp's `Platform/hal_flash.c::hal_flash_erase_block_dac`
 * + `hal_flash_write_dac`. Same OSPI controller (AM2434), same chip (Cypress
 * S25HL512T at the time of writing), same bare-metal STIG + DAC mode strategy
 * that worked for `nova_fw_update.c::write_meta_block_atomic` in the runtime
 * (0.A.199-205 saga).
 *
 * Why the runtime SDK Flash_eraseBlk/Flash_write don't work here: bench-2026-05-31
 * negative rollback test on STORAGE proved the SBL chooser's
 * `sbl_bank_select.c::write_boot_meta` silently fails on Cypress S25HL512T even
 * when WCC=0xFF auto-poll is neutralised at SBL init time (boot_count stayed
 * pinned at 1 across 4 chooser executions). The SBL's pre-CPSW context isn't
 * enough to make SDK INDIRECT writes land bytes; the chip's mode interaction
 * with the SDK's PP/erase path is broken the same way as the runtime's was
 * before 0.A.199 swapped to DAC. The bare-metal MMIO path doesn't go through
 * SDK's `Flash_norOspiWaitReady` or `Flash_norOspiWriteIndirect` and works.
 *
 * This file is INDEPENDENT of Platform/hal_flash.c — copied (not linked) so
 * the SBL build doesn't pull in FreeRTOS, the SDK Flash driver wrappers, or
 * the runtime's hwip suspension. Single-threaded NoRTOS atomic_enter is a
 * no-op.
 *
 * See memories/repo/sbl-chooser-wcc-bug-2026-05-31.md for the negative-test
 * bench evidence that motivated this port.
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */
#ifndef SBL_HAL_FLASH_H
#define SBL_HAL_FLASH_H

#include <stdint.h>

/* Bare-metal 256-KB block erase via STIG. Bypasses SDK Flash_eraseBlk
 * (which silently no-ops on Cypress S25HL512T in the SBL context).
 *
 * `addr` MUST be 256-KB block-aligned (multiple of SBL_HAL_FLASH_BLOCK_SIZE).
 * Cypress S25HL512T in 4S_4D_4D DTR mode only honors the 0xDC 256-KB
 * block-erase opcode — the 4-KB sector opcodes (0x20/0x21) leave the
 * controller wedged. Callers must work with 256-KB granularity.
 *
 * Returns:
 *   0  success
 *  -1  WIP/STIG timeout pre-erase, or WREN didn't set WEL, or addr unaligned
 *  -3  WIP stuck after erase
 *  -4  WEL stuck (chip rejected the erase) */
int sbl_hal_flash_erase_block(uint32_t addr);

/* Bare-metal multi-page DAC-mode Page Program write. Bypasses SDK Flash_write
 * (which silently no-ops on Cypress S25HL512T in the SBL context).
 *
 * Programs `len` bytes from `src` to flash `addr`. Both `addr` and `len`
 * must be page-aligned (multiple of 256). Performs WREN + DAC write +
 * WIP poll per page, lazily enabling DAC mode on the first call and
 * keeping it on across pages (per `docs/lp-am2434-ospi-dac-writes.md`
 * — toggling DAC bit between sub-PPs silently drops bytes).
 *
 * Returns:
 *   0  success
 *  -1  alignment / size error, or per-page WIP/WREN failure
 *  -3  WIP stuck after PP
 *  -4  WEL stuck (PP rejected) */
int sbl_hal_flash_write(uint32_t addr, const uint8_t *src, uint32_t len);

#define SBL_HAL_FLASH_PAGE_SIZE     256U
#define SBL_HAL_FLASH_BLOCK_SIZE    (256U * 1024U)

#endif /* SBL_HAL_FLASH_H */
