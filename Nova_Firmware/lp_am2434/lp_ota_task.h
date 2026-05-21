/*
 * lp_ota_task.h — Over-the-air firmware update listener (Phase 1A)
 *
 * Phase 1A scope: open a TCP listener on :LP_OTA_PORT, push an
 * `FwBankInfo` proto frame to every client on connect, and respond
 * to any update attempt with `FwUpdateStatus { state=FW_ERROR,
 * error_code=LP_OTA_ERR_NOT_IMPL }`.  No flash writes happen yet —
 * that gets unblocked in Phase 1B once the OSPI offset collision
 * with `lp_device_config` (banks at 0x200000/0x210000 vs Platform
 * Bank B at 0x200000) is resolved.
 *
 * Wire framing (length-prefixed; identical in both directions):
 *
 *     +----+----+----+----+----+--- … ---+
 *     | total_len_be (u32) | tag |  body |
 *     +----+----+----+----+----+--- … ---+
 *
 *   total_len    Total payload length INCLUDING the tag byte.
 *                Big-endian.  Max LP_OTA_MAX_FRAME bytes.
 *   tag          One of LP_OTA_TAG_*. Identifies the message type.
 *                Avoids any nanopb dependency on the LP — the body
 *                bytes that follow are still proto3 wire-format,
 *                matching `proto/agristar/firmware.proto`, so the
 *                bridge can decode with `ts-proto`.
 *   body         Proto3 wire bytes for the message named below.
 *
 * Tag map (matches proto/agristar/firmware.proto messages):
 *   0x10  FwBeginUpdate        (Bridge → LP)
 *   0x11  FwDataChunk          (Bridge → LP)
 *   0x12  FwFinalizeUpdate     (Bridge → LP)
 *   0x13  FwActivateBank       (Bridge → LP)
 *   0x20  FwUpdateStatus       (LP     → Bridge)
 *   0x21  FwBankInfo           (LP     → Bridge)
 *
 * Why a custom tag byte instead of a oneof envelope:
 *   The full FwUpdateEnvelope would force us to wire nanopb into the
 *   LP build before the more interesting Phase 1B work.  A 1-byte
 *   discriminator is enough for now and reads identically on both
 *   sides.  When Phase 1B lands we can either keep this framing or
 *   switch to nanopb-decoded `FwUpdate` envelope — the wire body
 *   bytes are unchanged.
 *
 * Spawned only on CONTROLLER and orbit roles where Ethernet is up.
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */
#ifndef LP_AM2434_LP_OTA_TASK_H
#define LP_AM2434_LP_OTA_TASK_H

#include <stdint.h>

#define LP_OTA_PORT          5503
#define LP_OTA_MAX_CONNS     2
#define LP_OTA_MAX_FRAME     4096   /* fits 3-4 streaming chunk frames (~1040 B each) */

/* Wire tags */
#define LP_OTA_TAG_BEGIN     0x10
#define LP_OTA_TAG_CHUNK     0x11
#define LP_OTA_TAG_FINALIZE  0x12
#define LP_OTA_TAG_ACTIVATE  0x13
#define LP_OTA_TAG_STATUS    0x20
#define LP_OTA_TAG_BANK_INFO 0x21

/* Error codes returned in FwUpdateStatus.error_code.
 *
 * 1-9   reserved for transport / framing problems (LP-side)
 * 10-19 reserved for state-machine / parsing problems (LP-side)
 * 20-29 underlying NovaFwUpdate_* failure (Platform layer); the
 *       low byte mirrors the platform return code 1:1 so the bridge
 *       can map it back to the originating call site.
 */
#define LP_OTA_ERR_NONE        0
#define LP_OTA_ERR_NOT_IMPL    1   /* unwired path (kept for back-compat) */
#define LP_OTA_ERR_BAD_FRAME   2   /* malformed length-prefix or tag */
#define LP_OTA_ERR_TOO_LARGE   3   /* total_len > LP_OTA_MAX_FRAME */
#define LP_OTA_ERR_DECODE      10  /* proto3 body parse failure */
#define LP_OTA_ERR_TOO_BIG     11  /* image > LP_OTA_MAIN_MAX_BYTES */
#define LP_OTA_ERR_BEGIN       20  /* NovaFwUpdate_Begin failed */
#define LP_OTA_ERR_CHUNK       21  /* NovaFwUpdate_WriteChunk failed */
#define LP_OTA_ERR_FINALIZE    22  /* NovaFwUpdate_Finalize failed */
#define LP_OTA_ERR_ACTIVATE    23  /* staging copy / reset failed */
#define LP_OTA_ERR_CHUNK_RETRY 25  /* per-chunk verify mismatch — bridge
                                    * should resend this chunk. bytes_written
                                    * in the FwUpdateStatus indicates the
                                    * chunk offset that needs re-send.
                                    * DEPRECATED 2026-05-15: same-page retry
                                    * wedges the chip. Use BANK_B_REDO. */
#define LP_OTA_ERR_BANK_B_REDO 26  /* FINALIZE CRC mismatch — bridge should
                                    * re-issue BEGIN (which re-erases Bank B
                                    * giving the chip a fresh slate) and
                                    * re-stream all chunks. Up to N full
                                    * retries per OTA attempt. */
#define LP_OTA_ERR_ROLE_MISMATCH 27 /* FwBeginUpdate.expected_role didn't match
                                    * this LP's lp_device_config.role. Bridge
                                    * is pushing the wrong-role binary at us.
                                    * See docs/lp-am2434-ota-hardening-plan.md
                                    * Gap 1-LP. */
#define LP_OTA_ERR_DOWNGRADE     28 /* Incoming version's 0.A.<N> integer is
                                    * lower than the running LP_FW_VERSION's
                                    * N, and FwBeginUpdate.allow_downgrade
                                    * was false. Bridge must set allow_downgrade
                                    * explicitly to roll back. See Gap 2-LP. */

/* LP staging-hack constants (Phase 2; see
 * docs/LP-AM2434-OTA-Update-Plan.md §"Phase 2").
 *
 * The MAIN image lives at OSPI 0x080000 (where TI's stock SBL hard-
 * loads cluster 0). The WATCHDOG mcelf lives at 0x180000. The
 * staging copy on Activate erases A and copies B→A; we MUST cap the
 * copy at (0x180000 - 0x080000) so we never clobber the watchdog. */
#define LP_OTA_MAIN_LIVE_OFFSET   0x080000U
#define LP_OTA_WATCHDOG_OFFSET    0x180000U
#define LP_OTA_MAIN_MAX_BYTES     (LP_OTA_WATCHDOG_OFFSET - LP_OTA_MAIN_LIVE_OFFSET) /* 1 MB */

/* 0.A.183 Option 2 (scratch-region remap). The chip has stochastic
 * DAC byte-loss on Bank B writes. Per-chunk verify after write; on
 * mismatch, redirect that chunk's data to a fresh page in this
 * scratch region (different chip address = no same-page back-to-back
 * PP, so no wedge). A small remap table (chunk offset → scratch
 * offset) is consulted at FINALIZE CRC and at stage-copy time.
 *
 * 0xA00000 is immediately after Bank B (0x900000 + 1 MB) and ahead
 * of any allocated region (config is at 0x600000, well below). One
 * 256 KB block at 0xA00000 gives us 256 KB of scratch = 256 1-KB
 * chunks of headroom. Typical OTA needs 1-10 redirects. */
#define LP_OTA_SCRATCH_OFFSET     0xA00000U
#define LP_OTA_SCRATCH_SIZE       (256U * 1024U)
#define LP_OTA_REMAP_MAX          64U   /* max redirected chunks per OTA */

/* FreeRTOS task entry. Spawn after Ethernet is brought up. */
void lp_ota_task(void *args);

#endif /* LP_AM2434_LP_OTA_TASK_H */
