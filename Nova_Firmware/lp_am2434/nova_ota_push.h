/*
 * nova_ota_push.h — Nova-side TCP client that drives a remote LP's
 *                   `lp_ota_task` state machine.
 *
 * Phase 3.5 of the UART-airgap migration (docs/uart-airgap-architecture.md):
 * Pi5 → UART → Nova → TCP → orbit LP. This module replaces the
 * Pi5-side `constellation-ui/server/src/orbitOtaPush.ts` for the
 * production data path. The TS code stays on the Pi5 only for the
 * bench-dual-homed dev rig until the airgap closes.
 *
 * Wire framing (length-prefixed; same as `lp_ota_task.h`):
 *
 *     [u32 BE total_len][u8 tag][proto3 body]
 *
 * Outbound tags: 0x10 Begin / 0x11 Chunk / 0x12 Finalize / 0x13 Activate.
 * Inbound  tags: 0x20 Status / 0x21 BankInfo. See `lp_ota_task.h` for the
 * `LP_OTA_ERR_*` taxonomy returned in `FwUpdateStatus.error_code`.
 *
 * Lifecycle:
 *   1. NovaOtaPush_BeginToLp() — opens TCP, recvs auto-pushed
 *      FwBankInfo, cross-checks role + downgrade, sends FwBeginUpdate,
 *      waits for the LP's first FwUpdateStatus (which signals
 *      ERASING/RECEIVING).
 *   2. NovaOtaPush_WriteChunkToLp() — N calls, one per chunk.
 *      Internally handles LP_OTA_ERR_BANK_B_REDO by reopening the
 *      session (re-erasing Bank B = fresh slate), same as orbitOtaPush.ts.
 *   3. NovaOtaPush_FinalizeAndActivateLp() — sends Finalize, waits for
 *      VERIFIED, sends Activate(reboot=true). The LP reboots and closes
 *      the socket; the post-Activate socket close is treated as success.
 *   4. NovaOtaPush_AbortLp() — no Finalize, just close. LP's Bank B is
 *      uncommitted (no Activate was sent), so it boots from Bank A.
 *
 * Or — for the fleet-probe path used by FwFleetProbe (envelope 136):
 *   NovaOtaPush_ProbeFleet() iterates the orbit IPs registered with
 *   OrbitClient_Init (10.47.27.2/3/4 in today's bench rig), opens a
 *   short-lived TCP to each, recvs FwBankInfo, and fills a small
 *   FwFleetMember[] array. No state is kept across calls.
 *
 * Socket lifetime: a single static socket is held across the
 * BeginToLp → WriteChunkToLp* → FinalizeAndActivateLp / AbortLp
 * sequence. The module is single-tenant — only one install at a time.
 * NovaOtaBroker enforces that invariant at the envelope-dispatch layer.
 *
 * Timeouts (mirrors orbitOtaPush.ts):
 *   connect          5 s
 *   Begin Status     120 s (LP erases Bank B which can take ~5 s)
 *   per-chunk Status 3 s
 *   Finalize Status  10 s
 *   Activate Status  3 s (treated as ok on timeout — LP rebooted)
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */
#ifndef NOVA_OTA_PUSH_H
#define NOVA_OTA_PUSH_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─── Fleet member descriptor ────────────────────────────────────────
 *
 * Hand-rolled to match `FwFleetMember` (proto/agristar/firmware.proto
 * §FwFleetMember). The broker's `emit_fleet_snapshot` reads this struct
 * directly and encodes it on the wire — no nanopb in the loop. Keep the
 * field set in lock-step with the .proto definition.
 *
 * String fields are stored inline (small, bounded) so the caller doesn't
 * have to keep a separate string pool alive after the probe returns.
 */
#define NOVA_OTA_PUSH_HOST_MAX        16U   /* "xxx.xxx.xxx.xxx" + NUL */
#define NOVA_OTA_PUSH_VERSION_MAX     48U   /* room for "0.A.<n>+<sha>-dirty" */
#define NOVA_OTA_PUSH_ERROR_MAX       64U

typedef struct NovaFwFleetMember {
    char     host[NOVA_OTA_PUSH_HOST_MAX];       /* proto field 1 */
    bool     reachable;                          /* proto field 2 */
    uint32_t current_role;                       /* proto field 3 — force-encode */
    char     active_version[NOVA_OTA_PUSH_VERSION_MAX]; /* proto field 4 */
    uint32_t active_bank;                        /* proto field 5 (FwBankId) */
    bool     bank_a_valid;                       /* proto field 6 */
    bool     bank_b_valid;                       /* proto field 7 */
    uint32_t boot_count;                         /* proto field 8 */
    char     error[NOVA_OTA_PUSH_ERROR_MAX];     /* proto field 9 — set when !reachable */
    uint32_t boot_reason;                        /* proto field 10 — SBL chooser writes
                                                  * 0=normal, 1=watchdog, 2=FALLBACK
                                                  * (skip-highest fired after strikes>=3).
                                                  * Added 0.A.213 for probe-side rollback
                                                  * observability — see memories/repo/
                                                  * sbl-chooser-wcc-bug-2026-05-31.md. */
} NovaFwFleetMember;

/* ─── Result codes ────────────────────────────────────────────────────
 *
 * Return-code conventions (all functions return int32_t):
 *   0                       success
 *  -1                       generic transport (socket/send/recv) failure
 *  -2                       LP returned FwUpdateStatus.state=FW_ERROR.
 *                           Caller may inspect *out_lp_error for the
 *                           originating LP_OTA_ERR_* code (proto field 4
 *                           of the Status message).
 *  -3                       receive timeout (no Status within wallclock)
 *  -4                       not connected / sequence error (e.g. Chunk
 *                           call without a prior successful BeginToLp)
 *  -5                       bank-B-redo attempts exhausted
 */
#define NOVA_OTA_PUSH_OK              0
#define NOVA_OTA_PUSH_ERR_TRANSPORT  -1
#define NOVA_OTA_PUSH_ERR_LP_ERROR   -2
#define NOVA_OTA_PUSH_ERR_TIMEOUT    -3
#define NOVA_OTA_PUSH_ERR_SEQUENCE   -4
#define NOVA_OTA_PUSH_ERR_REDO_LIMIT -5

/* ─── Public API ────────────────────────────────────────────────────── */

/* Open TCP to `target_ip`:5503, wait for the LP's auto-pushed FwBankInfo,
 * cross-check it against `expected_role` and the running version (unless
 * `allow_downgrade`), then send FwBeginUpdate and wait for the LP's
 * first FwUpdateStatus.
 *
 *   target_ip       IPv4 in host order. (We translate via inet_aton on
 *                   a "x.x.x.x" string built from the four octets.)
 *   total_size      bytes in the staged image
 *   image_crc       CRC-32 (Ethernet/zlib poly) over the full image
 *   version         null-terminated version string for downgrade gate
 *   chunk_size      hint to the LP for buffer sizing (1024 is fine)
 *   expected_role   OrbitRole this image targets — force-encoded
 *   allow_downgrade if true, FwBeginUpdate.allow_downgrade = true
 *   out_lp_error    [out, optional] LP_OTA_ERR_* on -2 return
 *
 * Returns 0 on success, negative NOVA_OTA_PUSH_ERR_* otherwise.
 * On non-zero return the internal socket is already closed.
 */
int32_t NovaOtaPush_BeginToLp(uint32_t target_ip,
                              uint32_t total_size,
                              uint32_t image_crc,
                              const char *version,
                              uint32_t chunk_size,
                              uint32_t expected_role,
                              bool allow_downgrade,
                              uint32_t *out_lp_error);

/* Send one FwDataChunk and wait for FwUpdateStatus.
 *
 * Caller hands us the proto-payload bytes verbatim (Pi5 → UART → us);
 * we encode the chunk envelope on top.
 *
 *   offset      byte offset in the image (matches the LP's running
 *               write position; force-encoded since 0 is the first
 *               chunk).
 *   data        pointer to chunk bytes
 *   len         chunk length
 *   chunk_crc   CRC-32 of `data[0..len-1]`
 *   out_lp_error [out, optional] as above
 *
 * If the LP returns LP_OTA_ERR_BANK_B_REDO (26), we close the current
 * socket and… do NOT internally re-issue Begin here. The broker decides
 * whether to retry from offset 0 (per orbitOtaPush.ts behaviour); we
 * surface the redo code via out_lp_error so the caller can resequence.
 *
 * Returns 0 on success, negative NOVA_OTA_PUSH_ERR_* otherwise. */
int32_t NovaOtaPush_WriteChunkToLp(uint32_t offset,
                                   const uint8_t *data,
                                   uint32_t len,
                                   uint32_t chunk_crc,
                                   uint32_t *out_lp_error);

/* Send FwFinalizeUpdate, wait for VERIFIED Status, then send
 * FwActivateBank(reboot=true). The LP closes the socket on reboot —
 * we treat post-Activate disconnect / Status-timeout as success.
 *
 *   expected_crc  echoed back to LP as confirmation (CLAUDE.md doesn't
 *                 force this to be force-encoded since it's not 0-
 *                 meaningful — the LP rejects 0).
 *   out_lp_error  [out, optional]
 *
 * Returns 0 on success, negative NOVA_OTA_PUSH_ERR_* otherwise. The
 * socket is closed before return either way. */
int32_t NovaOtaPush_FinalizeAndActivateLp(uint32_t expected_crc,
                                          uint32_t *out_lp_error);

/* Abort the in-flight push. Does NOT send Finalize/Activate; the LP's
 * Bank B is left uncommitted and the next boot is from the still-valid
 * Bank A. Idempotent — safe to call when no push is in flight. */
void NovaOtaPush_AbortLp(void);

/* Probe each known orbit IP (from OrbitClient_GetIpv4) with a short
 * TCP connect + recv-FwBankInfo, fill `out_members[]` up to
 * `max_members`, return the count actually populated.
 *
 *   timeout_ms  per-host wallclock budget for connect+recv (0 → 300 ms).
 *
 * No side effects beyond opening + closing one TCP per orbit. Safe to
 * call concurrently with an in-flight install — uses a private socket. */
size_t NovaOtaPush_ProbeFleet(NovaFwFleetMember *out_members,
                              size_t max_members,
                              uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* NOVA_OTA_PUSH_H */
