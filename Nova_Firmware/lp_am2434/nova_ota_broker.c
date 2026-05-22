/*
 * nova_ota_broker.c — Pi5 ↔ Nova install-orchestration broker (Phase 3
 *                      scaffold, UART-airgap OTA migration).
 *
 * Scope of this file (Phase 3):
 *   - Decode envelope-130..136 inner bodies into a small per-component
 *     state struct.
 *   - Track an `s_state` (IDLE / PROBING / INSTALLING / ERROR) so the
 *     rest of `main.c` can gate non-essential broadcasts via
 *     `NovaOtaBroker_IsInInstallMode()`.
 *   - Emit `FwInstallProgress` (envelope tag 140) and
 *     `FwInstallResult`  (envelope tag 141) and
 *     `FwFleetSnapshot`  (envelope tag 142) via `NovaProto_SendRaw`.
 *   - **Stub** every leaf operation (no TCP push, no NovaFwUpdate_* call,
 *     no fleet probe). Each STUB path emits
 *     FwInstallProgress(state=FAILED, error_code=99, error_message=
 *     "broker leaf not implemented") so Pi5's `firmwareInstaller.ts`
 *     surfaces the gap immediately instead of hanging on a missing
 *     reply.
 *
 * Invariants enforced here (per `CLAUDE.md`):
 *   1. proto3 zero-suppression — `pb_uint32_force` for 0-meaningful
 *      fields. Specifically: FwInstallProgress.component_index,
 *      FwInstallProgress.state (varint=0 is FW_INSTALL_PENDING), and
 *      FwFleetMember.current_role.
 *   3. No `static` counters inside repeated-submsg decoders. We don't
 *      decode any repeated submsg today (FwFleetProbe's only field is
 *      `timeout_ms`), but if Phase 3.5 adds one this file must follow
 *      the rule.
 *   4. TX path goes through `NovaProto_SendRaw` only. We never bypass.
 *   8. No hardcoded sensor / setpoint / unit values — none introduced.
 *
 * Wire reference:
 *   proto/agristar/envelope.proto         (envelope tags 130..142)
 *   proto/agristar/firmware.proto         (FwInstall* / FwFleet*)
 *   docs/uart-airgap-architecture.md      (production migration plan)
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */
#include "nova_ota_broker.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>

#include <kernel/dpl/DebugP.h>

#include "nova_protocol.h"

/* ─── Envelope tag wire-keys (length-delimited, wire-type 2) ──────────
 *
 * Envelope.fw_install_progress = 140 → tag word = (140<<3)|2 = 1122
 *   Varint encode: byte0 = 0xE2 (0x62 | 0x80), byte1 = 0x08
 * Envelope.fw_install_result   = 141 → tag word = (141<<3)|2 = 1130
 *   Varint encode: byte0 = 0xEA (0x6A | 0x80), byte1 = 0x08
 * Envelope.fw_fleet_snapshot   = 142 → tag word = (142<<3)|2 = 1138
 *   Varint encode: byte0 = 0xF2 (0x72 | 0x80), byte1 = 0x08
 *
 * (Matches the prologue comment in nova_ota_broker.h.)
 */
#define ENV_TAG_FW_INSTALL_PROGRESS_B0   0xE2u
#define ENV_TAG_FW_INSTALL_PROGRESS_B1   0x08u
#define ENV_TAG_FW_INSTALL_RESULT_B0     0xEAu
#define ENV_TAG_FW_INSTALL_RESULT_B1     0x08u
#define ENV_TAG_FW_FLEET_SNAPSHOT_B0     0xF2u
#define ENV_TAG_FW_FLEET_SNAPSHOT_B1     0x08u

/* Phase-3 stub-failure code. Anything ≥ 100 is reserved for runtime-side
 * errors; 99 is "intentional Phase-3 scaffold gap" so Pi5 can surface a
 * distinct message ("broker leaf not implemented yet"). */
#define BROKER_ERR_STUB_LEAF_NOT_IMPL    99u

/* ─── proto3 enum mirror (must track firmware.proto) ─────────────────── */
typedef enum {
    FW_INSTALL_PENDING      = 0,
    FW_INSTALL_BEGIN        = 1,
    FW_INSTALL_ERASING      = 2,
    FW_INSTALL_PUSHING      = 3,
    FW_INSTALL_VERIFYING    = 4,
    FW_INSTALL_ACTIVATING   = 5,
    FW_INSTALL_REBOOTING    = 6,
    FW_INSTALL_CONFIRMED    = 7,
    FW_INSTALL_DONE         = 8,
    FW_INSTALL_SKIPPED      = 9,
    FW_INSTALL_FAILED       = 10
} FwInstallComponentStateLocal;

typedef enum {
    FW_INSTALL_OVERALL_IDLE        = 0,
    FW_INSTALL_OVERALL_PROBING     = 1,
    FW_INSTALL_OVERALL_INSTALLING  = 2,
    FW_INSTALL_OVERALL_DONE        = 3,
    FW_INSTALL_OVERALL_FAILED      = 4,
    FW_INSTALL_OVERALL_ABORTED     = 5
} FwInstallOverallStateLocal;

/* ─── Broker state machine ───────────────────────────────────────────── */
typedef enum {
    BROKER_IDLE        = 0,
    BROKER_PROBING     = 1,
    BROKER_INSTALLING  = 2,   /* any sub-state of an install bundle */
    BROKER_ERROR       = 3    /* sticky until next FwInstallBegin/Abort */
} NovaOtaBrokerState;

/* Per-bundle / per-component scratch. Sized for one chunk in flight. */
#define BROKER_NAME_MAX           48
#define BROKER_TARGET_HOST_MAX    24

typedef struct {
    /* Bundle-wide */
    uint32_t component_count;
    uint32_t chunk_size;            /* hint from Pi5 (0 = default 1024) */
    bool     allow_downgrade;
    /* Current component */
    uint32_t cur_component_index;
    char     cur_component_name[BROKER_NAME_MAX];
    char     cur_target_host[BROKER_TARGET_HOST_MAX];
    uint32_t cur_role;
    uint32_t cur_total_size;
    uint32_t cur_bytes_written;
    FwInstallComponentStateLocal cur_state;
    /* Last seq Pi5 included on the install envelope (diagnostics only). */
    uint32_t last_envelope_seq;
} NovaOtaBrokerCtx;

static NovaOtaBrokerState s_state;
static NovaOtaBrokerCtx   s_ctx;

/* ─── Local varint helpers (don't share `static` ones from main.c) ───── */
static size_t bk_encode_varint(uint8_t *buf, uint32_t value)
{
    size_t n = 0;
    while (value > 0x7Fu) {
        buf[n++] = (uint8_t)(0x80u | (value & 0x7Fu));
        value >>= 7u;
    }
    buf[n++] = (uint8_t)value;
    return n;
}

static size_t bk_decode_varint(const uint8_t *buf, size_t len, uint32_t *out)
{
    uint32_t v = 0;
    uint32_t shift = 0;
    for (size_t i = 0; i < len && i < 5U; i++) {
        v |= ((uint32_t)(buf[i] & 0x7F)) << shift;
        if ((buf[i] & 0x80) == 0U) {
            *out = v;
            return i + 1U;
        }
        shift += 7U;
    }
    return 0U;
}

static size_t bk_skip_field(uint8_t wire, const uint8_t *buf, size_t len)
{
    switch (wire) {
        case 0: { /* varint */
            uint32_t dummy;
            return bk_decode_varint(buf, len, &dummy);
        }
        case 1: /* fixed64 */
            return (len >= 8U) ? 8U : 0U;
        case 2: { /* length-delimited */
            uint32_t sublen;
            size_t n = bk_decode_varint(buf, len, &sublen);
            if (n == 0U || n + sublen > len) return 0U;
            return n + sublen;
        }
        case 5: /* fixed32 */
            return (len >= 4U) ? 4U : 0U;
        default:
            return 0U;
    }
}

/* Encode (field,wire) as a tag varint, return bytes written. */
static size_t bk_encode_tag(uint8_t *buf, uint32_t field, uint8_t wire)
{
    return bk_encode_varint(buf, (field << 3) | wire);
}

/* Force-encode a uint32 (writes the field even when v == 0). Mirrors
 * pb_uint32_force in the rest of the firmware — required by CLAUDE.md #1
 * for fields where 0 is meaningful (component_index, state varint=0,
 * role=CONTROLLER). */
static size_t bk_pb_u32_force(uint8_t *buf, uint32_t field, uint32_t v)
{
    size_t n = bk_encode_tag(buf, field, 0u);
    n += bk_encode_varint(buf + n, v);
    return n;
}

/* Encode a string field. Empty string → field omitted (proto3 default). */
static size_t bk_pb_string(uint8_t *buf, uint32_t field, const char *s)
{
    if (s == NULL || s[0] == '\0') return 0u;
    size_t slen = strlen(s);
    size_t n = bk_encode_tag(buf, field, 2u);
    n += bk_encode_varint(buf + n, (uint32_t)slen);
    memcpy(buf + n, s, slen);
    n += slen;
    return n;
}

/* ─── Envelope writers ───────────────────────────────────────────────── */

/* Build full Envelope { fw_install_progress = { … } } and ship via
 * NovaProto_SendRaw. Returns bytes pushed to the TX path, or 0 on
 * encode overflow. */
static size_t emit_install_progress(uint32_t            component_index,
                                    const char         *component_name,
                                    const char         *target_host,
                                    uint32_t            state,
                                    uint32_t            bytes_written,
                                    uint32_t            total_size,
                                    uint32_t            error_code,
                                    const char         *error_message,
                                    uint32_t            overall)
{
    /* Inner FwInstallProgress body. Worst case is 2× 32-char strings +
     * ~24 B of varints, well under 256 B. */
    uint8_t inner[320];
    size_t  ipos = 0;

    /* field 1 component_index — force */
    ipos += bk_pb_u32_force(inner + ipos, 1u, component_index);
    /* field 2 component_name */
    ipos += bk_pb_string(inner + ipos, 2u, component_name);
    /* field 3 target_host */
    ipos += bk_pb_string(inner + ipos, 3u, target_host);
    /* field 4 state — varint=0 means PENDING, valid meaning, force */
    ipos += bk_pb_u32_force(inner + ipos, 4u, state);
    /* field 5 bytes_written — proto3 default-suppress is OK (counter) */
    if (bytes_written != 0u) {
        ipos += bk_encode_tag(inner + ipos, 5u, 0u);
        ipos += bk_encode_varint(inner + ipos, bytes_written);
    }
    /* field 6 total_size */
    if (total_size != 0u) {
        ipos += bk_encode_tag(inner + ipos, 6u, 0u);
        ipos += bk_encode_varint(inner + ipos, total_size);
    }
    /* field 7 error_code */
    if (error_code != 0u) {
        ipos += bk_encode_tag(inner + ipos, 7u, 0u);
        ipos += bk_encode_varint(inner + ipos, error_code);
    }
    /* field 8 error_message */
    ipos += bk_pb_string(inner + ipos, 8u, error_message);
    /* field 9 overall — IDLE=0 is a valid wire state, force */
    ipos += bk_pb_u32_force(inner + ipos, 9u, overall);

    /* Wrap as Envelope.fw_install_progress (tag 140, length-delimited). */
    uint8_t env[384];
    size_t  epos = 0;
    env[epos++] = ENV_TAG_FW_INSTALL_PROGRESS_B0;
    env[epos++] = ENV_TAG_FW_INSTALL_PROGRESS_B1;
    epos += bk_encode_varint(env + epos, (uint32_t)ipos);
    if (epos + ipos > sizeof(env)) return 0u;
    memcpy(env + epos, inner, ipos);
    epos += ipos;

    NovaProto_SendRaw(env, epos);
    return epos;
}

/* Build full Envelope { fw_install_result = { overall, failure_reason,
 * (no per-component results in the Phase 3 scaffold — Pi5 reconstructs
 * from progress envelopes for now) } }. */
static size_t emit_install_result(uint32_t    overall,
                                  const char *failure_reason)
{
    uint8_t inner[128];
    size_t  ipos = 0;

    /* field 1 overall — IDLE=0 is valid, force */
    ipos += bk_pb_u32_force(inner + ipos, 1u, overall);
    /* field 2 failure_reason */
    ipos += bk_pb_string(inner + ipos, 2u, failure_reason);
    /* field 3 components[] — empty in the Phase 3 scaffold. Phase 3.5
     * populates from per-component leaf results. */

    uint8_t env[160];
    size_t  epos = 0;
    env[epos++] = ENV_TAG_FW_INSTALL_RESULT_B0;
    env[epos++] = ENV_TAG_FW_INSTALL_RESULT_B1;
    epos += bk_encode_varint(env + epos, (uint32_t)ipos);
    if (epos + ipos > sizeof(env)) return 0u;
    memcpy(env + epos, inner, ipos);
    epos += ipos;

    NovaProto_SendRaw(env, epos);
    return epos;
}

/* Build Envelope { fw_fleet_snapshot = { members: [] } }. Empty in the
 * Phase 3 scaffold — Phase 3.5 walks orbit_client / dipswitch discovery. */
static size_t emit_fleet_snapshot_empty(void)
{
    uint8_t env[16];
    size_t  epos = 0;
    env[epos++] = ENV_TAG_FW_FLEET_SNAPSHOT_B0;
    env[epos++] = ENV_TAG_FW_FLEET_SNAPSHOT_B1;
    /* Empty inner body → length=0. */
    epos += bk_encode_varint(env + epos, 0u);
    NovaProto_SendRaw(env, epos);
    return epos;
}

/* ─── Decoders for the seven Pi5 → Nova messages ─────────────────────── */

/* Copy at most (dst_max - 1) bytes from a length-delimited proto3 string
 * field, NUL-terminate. */
static void bk_copy_string(char *dst, size_t dst_max,
                           const uint8_t *src, size_t slen)
{
    size_t cp = (slen < dst_max - 1u) ? slen : (dst_max - 1u);
    memcpy(dst, src, cp);
    dst[cp] = '\0';
}

/* Decode FwInstallBegin (envelope 130). */
static void decode_install_begin(const uint8_t *body, size_t len)
{
    char     bundle_version[48] = {0};
    uint32_t component_count = 0;
    uint32_t chunk_size      = 0;
    bool     allow_downgrade = false;
    /* manifest_sha256 (field 2, bytes) is decoded only for log purposes
     * in Phase 3 — the scaffold doesn't verify the manifest yet. */

    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t n = bk_decode_varint(body + pos, len - pos, &tag);
        if (n == 0u) return;
        pos += n;
        uint32_t field = tag >> 3;
        uint8_t  wire  = (uint8_t)(tag & 0x07u);

        if (wire == 2u && (field == 1u || field == 2u)) {
            uint32_t slen;
            size_t ln = bk_decode_varint(body + pos, len - pos, &slen);
            if (ln == 0u || pos + ln + slen > len) return;
            pos += ln;
            if (field == 1u) {
                bk_copy_string(bundle_version, sizeof(bundle_version),
                               body + pos, slen);
            }
            pos += slen;
        } else if (wire == 0u && (field == 3u || field == 4u || field == 5u)) {
            uint32_t v;
            size_t vn = bk_decode_varint(body + pos, len - pos, &v);
            if (vn == 0u) return;
            pos += vn;
            if      (field == 3u) component_count = v;
            else if (field == 4u) chunk_size      = v;
            else if (field == 5u) allow_downgrade = (v != 0u);
        } else {
            size_t sk = bk_skip_field(wire, body + pos, len - pos);
            if (sk == 0u) return;
            pos += sk;
        }
    }

    /* Reset state for the new bundle. */
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.component_count = component_count;
    s_ctx.chunk_size      = chunk_size;
    s_ctx.allow_downgrade = allow_downgrade;
    s_state = BROKER_INSTALLING;

    DebugP_log("[OTA-BROKER] InstallBegin bundle=\"%s\" components=%u "
               "chunk=%u downgrade=%d\r\n",
               bundle_version, (unsigned)component_count,
               (unsigned)chunk_size, (int)allow_downgrade);
}

/* Decode FwInstallComponentBegin (envelope 131). */
static void decode_component_begin(const uint8_t *body, size_t len)
{
    uint32_t component_index = 0;
    uint32_t role             = 0;
    uint32_t total_size       = 0;
    char     component_name[BROKER_NAME_MAX] = {0};

    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t n = bk_decode_varint(body + pos, len - pos, &tag);
        if (n == 0u) return;
        pos += n;
        uint32_t field = tag >> 3;
        uint8_t  wire  = (uint8_t)(tag & 0x07u);

        if (wire == 2u && (field == 1u || field == 5u || field == 6u)) {
            uint32_t slen;
            size_t ln = bk_decode_varint(body + pos, len - pos, &slen);
            if (ln == 0u || pos + ln + slen > len) return;
            pos += ln;
            if (field == 1u) {
                bk_copy_string(component_name, sizeof(component_name),
                               body + pos, slen);
            }
            /* field 5 image_sha256 / field 6 expected_version: not used
             * in the Phase 3 scaffold. */
            pos += slen;
        } else if (wire == 0u && (field == 2u || field == 3u || field == 4u)) {
            uint32_t v;
            size_t vn = bk_decode_varint(body + pos, len - pos, &v);
            if (vn == 0u) return;
            pos += vn;
            if      (field == 2u) component_index = v;
            else if (field == 3u) role            = v;
            else if (field == 4u) total_size      = v;
        } else {
            size_t sk = bk_skip_field(wire, body + pos, len - pos);
            if (sk == 0u) return;
            pos += sk;
        }
    }

    /* Stage the per-component scratch — emit BEGIN, then immediately
     * surface the Phase-3 stub failure so Pi5 doesn't wait forever on
     * the chunk-write side. */
    s_ctx.cur_component_index = component_index;
    s_ctx.cur_role            = role;
    s_ctx.cur_total_size      = total_size;
    s_ctx.cur_bytes_written   = 0u;
    s_ctx.cur_state           = FW_INSTALL_BEGIN;
    bk_copy_string(s_ctx.cur_component_name,
                   sizeof(s_ctx.cur_component_name),
                   (const uint8_t *)component_name, strlen(component_name));
    s_ctx.cur_target_host[0]  = '\0';   /* "self" not resolved yet */

    DebugP_log("[OTA-BROKER] ComponentBegin idx=%u name=\"%s\" "
               "role=%u total=%u (STUB → FAILED)\r\n",
               (unsigned)component_index, component_name,
               (unsigned)role, (unsigned)total_size);

    /* Phase-3 scaffold: report BEGIN, then FAILED immediately. */
    emit_install_progress(component_index, component_name, "stub",
                          FW_INSTALL_BEGIN, 0u, total_size,
                          0u, "",
                          FW_INSTALL_OVERALL_INSTALLING);
    emit_install_progress(component_index, component_name, "stub",
                          FW_INSTALL_FAILED, 0u, total_size,
                          BROKER_ERR_STUB_LEAF_NOT_IMPL,
                          "broker leaf not implemented",
                          FW_INSTALL_OVERALL_FAILED);
    s_ctx.cur_state = FW_INSTALL_FAILED;
    s_state         = BROKER_ERROR;
}

/* Decode FwInstallChunk (envelope 132). Phase 3 silently swallows chunks
 * for any component already in FAILED state — Pi5 may keep pushing for
 * a few hundred milliseconds before its TS-side state machine sees the
 * FAILED progress envelope. We don't re-emit FAILED on every chunk; one
 * is enough per component. */
static void decode_chunk(const uint8_t *body, size_t len)
{
    uint32_t component_index = 0;
    uint32_t offset          = 0;
    uint32_t data_len        = 0;

    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t n = bk_decode_varint(body + pos, len - pos, &tag);
        if (n == 0u) return;
        pos += n;
        uint32_t field = tag >> 3;
        uint8_t  wire  = (uint8_t)(tag & 0x07u);

        if (wire == 0u && (field == 1u || field == 2u)) {
            uint32_t v;
            size_t vn = bk_decode_varint(body + pos, len - pos, &v);
            if (vn == 0u) return;
            pos += vn;
            if      (field == 1u) component_index = v;
            else if (field == 2u) offset          = v;
        } else if (wire == 2u && field == 3u) {
            uint32_t slen;
            size_t ln = bk_decode_varint(body + pos, len - pos, &slen);
            if (ln == 0u || pos + ln + slen > len) return;
            pos += ln;
            data_len = slen;
            pos += slen;
        } else {
            size_t sk = bk_skip_field(wire, body + pos, len - pos);
            if (sk == 0u) return;
            pos += sk;
        }
    }

    /* Stub: do nothing with the bytes. Suppress the noisy log past the
     * first chunk of each component. */
    if (offset == 0u) {
        DebugP_log("[OTA-BROKER] Chunk idx=%u off=%u len=%u "
                   "(STUB, dropping)\r\n",
                   (unsigned)component_index, (unsigned)offset,
                   (unsigned)data_len);
    }
    (void)component_index;
    (void)offset;
}

/* Decode FwInstallComponentFinalize (envelope 133). */
static void decode_component_finalize(const uint8_t *body, size_t len)
{
    uint32_t component_index = 0;

    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t n = bk_decode_varint(body + pos, len - pos, &tag);
        if (n == 0u) return;
        pos += n;
        uint32_t field = tag >> 3;
        uint8_t  wire  = (uint8_t)(tag & 0x07u);

        if (wire == 0u && field == 1u) {
            uint32_t v;
            size_t vn = bk_decode_varint(body + pos, len - pos, &v);
            if (vn == 0u) return;
            pos += vn;
            component_index = v;
        } else {
            size_t sk = bk_skip_field(wire, body + pos, len - pos);
            if (sk == 0u) return;
            pos += sk;
        }
    }

    DebugP_log("[OTA-BROKER] ComponentFinalize idx=%u (STUB)\r\n",
               (unsigned)component_index);
    /* No additional progress envelope here — the component already moved
     * to FAILED on Begin. */
}

/* Decode FwInstallComplete (envelope 134). Body has no fields today;
 * we just transition state. */
static void decode_complete(const uint8_t *body, size_t len)
{
    (void)body;
    (void)len;
    DebugP_log("[OTA-BROKER] InstallComplete (STUB → emit Result FAILED)\r\n");
    emit_install_result(FW_INSTALL_OVERALL_FAILED,
                        "broker leaf not implemented");
    /* Bundle is done as far as Pi5 is concerned; return to IDLE so
     * non-essential broadcasts resume. */
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_state = BROKER_IDLE;
}

/* Decode FwInstallAbort (envelope 135). */
static void decode_abort(const uint8_t *body, size_t len)
{
    char reason[64] = {0};

    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t n = bk_decode_varint(body + pos, len - pos, &tag);
        if (n == 0u) return;
        pos += n;
        uint32_t field = tag >> 3;
        uint8_t  wire  = (uint8_t)(tag & 0x07u);

        if (wire == 2u && field == 1u) {
            uint32_t slen;
            size_t ln = bk_decode_varint(body + pos, len - pos, &slen);
            if (ln == 0u || pos + ln + slen > len) return;
            pos += ln;
            bk_copy_string(reason, sizeof(reason), body + pos, slen);
            pos += slen;
        } else {
            size_t sk = bk_skip_field(wire, body + pos, len - pos);
            if (sk == 0u) return;
            pos += sk;
        }
    }

    DebugP_log("[OTA-BROKER] InstallAbort reason=\"%s\"\r\n", reason);
    emit_install_result(FW_INSTALL_OVERALL_ABORTED,
                        reason[0] ? reason : "aborted by Pi5");
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_state = BROKER_IDLE;
}

/* Decode FwFleetProbe (envelope 136). Phase-3 reply is an empty
 * snapshot — Pi5's `orbitFleetResolver.ts` already falls back to its
 * TCP probe path on an empty snapshot, so the airgap migration step
 * here doesn't regress today's UX. */
static void decode_fleet_probe(const uint8_t *body, size_t len)
{
    uint32_t timeout_ms = 0;

    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t n = bk_decode_varint(body + pos, len - pos, &tag);
        if (n == 0u) return;
        pos += n;
        uint32_t field = tag >> 3;
        uint8_t  wire  = (uint8_t)(tag & 0x07u);

        if (wire == 0u && field == 1u) {
            uint32_t v;
            size_t vn = bk_decode_varint(body + pos, len - pos, &v);
            if (vn == 0u) return;
            pos += vn;
            timeout_ms = v;
        } else {
            size_t sk = bk_skip_field(wire, body + pos, len - pos);
            if (sk == 0u) return;
            pos += sk;
        }
    }

    DebugP_log("[OTA-BROKER] FleetProbe timeout=%u (STUB → empty snapshot)\r\n",
               (unsigned)timeout_ms);
    s_state = BROKER_PROBING;
    emit_fleet_snapshot_empty();
    s_state = BROKER_IDLE;
}

/* ─── Public API ─────────────────────────────────────────────────────── */

void NovaOtaBroker_Init(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_state = BROKER_IDLE;
    /* No DebugP_log here — Init may run pre-scheduler (called from
     * main() alongside NovaFwUpdate_Init), and DebugP_log uses the
     * FreeRTOS mutex which is undefined before vTaskStartScheduler.
     * Per CLAUDE.md hard-invariant #9 (lp_am2434 README). */
}

bool NovaOtaBroker_IsInInstallMode(void)
{
    /* PROBING is bounded (one envelope round-trip) and shouldn't gate
     * the periodic emitters. Only INSTALLING (and the sticky ERROR
     * tail, which clears on the next Begin/Abort/Complete) suspend. */
    return (s_state == BROKER_INSTALLING) || (s_state == BROKER_ERROR);
}

void NovaOtaBroker_OnEnvelope(uint32_t tag,
                              const uint8_t *body,
                              size_t len,
                              uint32_t envelope_seq)
{
    s_ctx.last_envelope_seq = envelope_seq;

    switch (tag) {
    case 130u: decode_install_begin       (body, len); break;
    case 131u: decode_component_begin     (body, len); break;
    case 132u: decode_chunk               (body, len); break;
    case 133u: decode_component_finalize  (body, len); break;
    case 134u: decode_complete            (body, len); break;
    case 135u: decode_abort               (body, len); break;
    case 136u: decode_fleet_probe         (body, len); break;
    default:
        DebugP_log("[OTA-BROKER] OnEnvelope: unhandled tag %u (len=%u)\r\n",
                   (unsigned)tag, (unsigned)len);
        break;
    }
}

void NovaOtaBroker_Tick(uint32_t now_ms)
{
    /* Phase 3 scaffold has no time-driven work — chunks are stubbed,
     * fleet probe replies inline, and reboot/probe phases don't run.
     * Reserved entry point so main.c's per-tick block can call us; the
     * cost is one function-call frame when IDLE. Phase 3.5 fills this
     * in (PUSHING progress cadence, REBOOTING wait, post-reboot probe). */
    (void)now_ms;
}
