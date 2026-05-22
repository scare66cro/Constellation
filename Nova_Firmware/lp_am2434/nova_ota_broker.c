/*
 * nova_ota_broker.c — Pi5 ↔ Nova install-orchestration broker
 *                      (UART-airgap OTA migration).
 *
 * Scope of this file:
 *   - Decode envelope-130..136 inner bodies into a small per-component
 *     state struct.
 *   - Track an `s_state` (IDLE / PROBING / INSTALLING / ERROR) so the
 *     rest of `main.c` can gate non-essential broadcasts via
 *     `NovaOtaBroker_IsInInstallMode()`.
 *   - Emit `FwInstallProgress` (envelope tag 140),
 *     `FwInstallResult`  (envelope tag 141), and
 *     `FwFleetSnapshot`  (envelope tag 142) via `NovaProto_SendRaw`.
 *   - Phase 3.5 (2026-05-22) — orbit-LP push path wired: each Pi5 chunk
 *     is forwarded to the target LP over TCP via `NovaOtaPush_*`. Fleet
 *     snapshot is built by probing known orbit IPs on first component.
 *   - **Stub** still applies to the controller-self-update branch
 *     (role == own role) — Phase 3.6 wires that to NovaFwUpdate_*.
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
#include "nova_ota_push.h"
#include "orbit_server/orbit_role.h"
#include "lp_device_config.h"

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
#define BROKER_FLEET_MAX          8       /* cached members per install */
#define BROKER_PROGRESS_PCT       5u      /* emit PUSHING every ~N% */

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
    uint32_t cur_image_crc;          /* accumulated chunk CRC for Finalize */
    uint32_t cur_next_progress;      /* next bytes_written threshold to emit */
    bool     cur_push_active;        /* a remote-LP push session is open */
    bool     cur_failed;              /* sticky for the in-flight component */
    FwInstallComponentStateLocal cur_state;
    /* Fleet snapshot — populated lazily on first decode_component_begin
     * of each install, dropped on Complete/Abort. */
    NovaFwFleetMember fleet[BROKER_FLEET_MAX];
    size_t            fleet_count;
    bool              fleet_probed;
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

/* Encode one FwFleetMember submsg into `dst`. Field map mirrors
 * proto/agristar/firmware.proto §FwFleetMember:
 *   1 host (string), 2 reachable (bool), 3 current_role (force),
 *   4 active_version (string), 5 active_bank (varint),
 *   6 bank_a_valid (bool), 7 bank_b_valid (bool),
 *   8 boot_count (varint), 9 error (string).
 * Returns bytes written. */
static size_t emit_fleet_member(uint8_t *dst, const NovaFwFleetMember *m)
{
    size_t n = 0;
    n += bk_pb_string(dst + n, 1u, m->host);
    if (m->reachable) {
        n += bk_encode_tag(dst + n, 2u, 0u);
        n += bk_encode_varint(dst + n, 1u);
    }
    n += bk_pb_u32_force(dst + n, 3u, m->current_role);
    n += bk_pb_string(dst + n, 4u, m->active_version);
    if (m->active_bank != 0u) {
        n += bk_encode_tag(dst + n, 5u, 0u);
        n += bk_encode_varint(dst + n, m->active_bank);
    }
    if (m->bank_a_valid) {
        n += bk_encode_tag(dst + n, 6u, 0u);
        n += bk_encode_varint(dst + n, 1u);
    }
    if (m->bank_b_valid) {
        n += bk_encode_tag(dst + n, 7u, 0u);
        n += bk_encode_varint(dst + n, 1u);
    }
    if (m->boot_count != 0u) {
        n += bk_encode_tag(dst + n, 8u, 0u);
        n += bk_encode_varint(dst + n, m->boot_count);
    }
    if (!m->reachable && m->error[0]) {
        n += bk_pb_string(dst + n, 9u, m->error);
    }
    return n;
}

/* Build Envelope { fw_fleet_snapshot = { members: [...] } } from the
 * cached `members[]`. */
static size_t emit_fleet_snapshot(const NovaFwFleetMember *members,
                                  size_t count)
{
    /* Outer body holds N members; each ~120 B worst case → cap at 1 KB. */
    uint8_t inner[1024];
    size_t  ipos = 0;
    for (size_t i = 0; i < count; i++) {
        /* Encode submsg into a scratch buffer first so we know its
         * length, then write tag + length + body. */
        uint8_t sub[200];
        size_t  sublen = emit_fleet_member(sub, &members[i]);
        if (ipos + 2u + 5u + sublen > sizeof(inner)) break;   /* safety */
        ipos += bk_encode_tag(inner + ipos, 1u, 2u);
        ipos += bk_encode_varint(inner + ipos, (uint32_t)sublen);
        memcpy(inner + ipos, sub, sublen);
        ipos += sublen;
    }

    uint8_t env[1200];
    size_t  epos = 0;
    env[epos++] = ENV_TAG_FW_FLEET_SNAPSHOT_B0;
    env[epos++] = ENV_TAG_FW_FLEET_SNAPSHOT_B1;
    epos += bk_encode_varint(env + epos, (uint32_t)ipos);
    if (epos + ipos > sizeof(env)) return 0u;
    memcpy(env + epos, inner, ipos);
    epos += ipos;

    NovaProto_SendRaw(env, epos);
    return epos;
}

/* Ensure the cached fleet is populated. Idempotent: only probes if
 * `fleet_probed` is false. Called lazily on the first component begin
 * of each install so a fleet-less install (controller-only) doesn't
 * pay the probe cost. */
static void ensure_fleet_snapshot(uint32_t timeout_ms)
{
    if (s_ctx.fleet_probed) return;
    s_ctx.fleet_count = NovaOtaPush_ProbeFleet(
        s_ctx.fleet, BROKER_FLEET_MAX,
        timeout_ms == 0u ? 500u : timeout_ms);
    s_ctx.fleet_probed = true;
    DebugP_log("[OTA-BROKER] fleet probe → %u members\r\n",
               (unsigned)s_ctx.fleet_count);
}

/* Look up an orbit member by role. Returns pointer into s_ctx.fleet[]
 * or NULL if no reachable LP advertises that role. */
static const NovaFwFleetMember *fleet_find_by_role(uint32_t role)
{
    for (size_t i = 0; i < s_ctx.fleet_count; i++) {
        const NovaFwFleetMember *m = &s_ctx.fleet[i];
        if (m->reachable && m->current_role == role) {
            return m;
        }
    }
    return NULL;
}

/* Convert "10.47.27.2" → 0x0A2F1B02 (host order). Returns 0 on parse error. */
static uint32_t parse_ipv4_host(const char *ip)
{
    if (ip == NULL) return 0u;
    uint32_t a = 0, b = 0, c = 0, d = 0;
    uint32_t parts[4] = {0};
    int idx = 0;
    for (const char *p = ip; *p && idx < 4; p++) {
        if (*p >= '0' && *p <= '9') {
            parts[idx] = parts[idx] * 10u + (uint32_t)(*p - '0');
        } else if (*p == '.') {
            idx++;
        } else {
            return 0u;
        }
    }
    if (idx != 3) return 0u;
    a = parts[0]; b = parts[1]; c = parts[2]; d = parts[3];
    if (a > 255u || b > 255u || c > 255u || d > 255u) return 0u;
    return (a << 24) | (b << 16) | (c << 8) | d;
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

    /* Reset state for the new bundle — including fleet cache. */
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

    /* Stage the per-component scratch. */
    s_ctx.cur_component_index = component_index;
    s_ctx.cur_role            = role;
    s_ctx.cur_total_size      = total_size;
    s_ctx.cur_bytes_written   = 0u;
    s_ctx.cur_image_crc       = 0u;
    s_ctx.cur_next_progress   = 0u;
    s_ctx.cur_push_active     = false;
    s_ctx.cur_failed          = false;
    s_ctx.cur_state           = FW_INSTALL_BEGIN;
    bk_copy_string(s_ctx.cur_component_name,
                   sizeof(s_ctx.cur_component_name),
                   (const uint8_t *)component_name, strlen(component_name));
    s_ctx.cur_target_host[0]  = '\0';

    DebugP_log("[OTA-BROKER] ComponentBegin idx=%u name=\"%s\" "
               "role=%u total=%u\r\n",
               (unsigned)component_index, component_name,
               (unsigned)role, (unsigned)total_size);

    /* Branch on role: own-role → Phase 3.6 self-update (still stubbed),
     * else → look up target IP in fleet snapshot and open TCP push. */
    OrbitRole own_role = (OrbitRole)LpDeviceConfig_Get()->role;
    if ((uint32_t)own_role == role) {
        /* Phase 3.6 owes this. Surface a clearer message than the
         * generic "leaf not implemented" so Pi5 log triage is easy. */
        emit_install_progress(component_index, component_name, "self",
                              FW_INSTALL_BEGIN, 0u, total_size,
                              0u, "",
                              FW_INSTALL_OVERALL_INSTALLING);
        emit_install_progress(component_index, component_name, "self",
                              FW_INSTALL_FAILED, 0u, total_size,
                              BROKER_ERR_STUB_LEAF_NOT_IMPL,
                              "controller self-update not yet wired (Phase 3.6)",
                              FW_INSTALL_OVERALL_FAILED);
        s_ctx.cur_failed = true;
        s_ctx.cur_state  = FW_INSTALL_FAILED;
        s_state          = BROKER_ERROR;
        return;
    }

    /* Remote-LP path: ensure the fleet is probed, find the target. */
    ensure_fleet_snapshot(500u);
    const NovaFwFleetMember *m = fleet_find_by_role(role);
    if (m == NULL) {
        DebugP_log("[OTA-BROKER] ComponentBegin: no fleet member with role=%u\r\n",
                   (unsigned)role);
        emit_install_progress(component_index, component_name, "",
                              FW_INSTALL_FAILED, 0u, total_size,
                              100u, "no reachable LP for component role",
                              FW_INSTALL_OVERALL_FAILED);
        s_ctx.cur_failed = true;
        s_ctx.cur_state  = FW_INSTALL_FAILED;
        s_state          = BROKER_ERROR;
        return;
    }
    bk_copy_string(s_ctx.cur_target_host,
                   sizeof(s_ctx.cur_target_host),
                   (const uint8_t *)m->host, strlen(m->host));

    /* Emit BEGIN so Pi5's UI moves off "queued". The image_crc and the
     * full chunk-stream haven't started yet — total_size is the only
     * meaningful counter at this point. */
    emit_install_progress(component_index, component_name,
                          s_ctx.cur_target_host,
                          FW_INSTALL_BEGIN, 0u, total_size,
                          0u, "",
                          FW_INSTALL_OVERALL_INSTALLING);

    /* Open TCP, run pre-flight + send Begin. The LP's first Status
     * after Begin means it's already started ERASING (Phase 1B is
     * synchronous on Bank B erase before replying), so we report
     * ERASING-then-PUSHING in quick succession. The image CRC for
     * Begin is left 0 — the bridge-side Pi5 never streams the full
     * CRC to us in a single message; we accumulate as we forward
     * chunks and pass the running CRC to Finalize. (This differs
     * slightly from orbitOtaPush.ts, which has the full image up
     * front; here we trust Pi5's per-chunk CRCs and let the LP's
     * Finalize-time full-image CRC catch any mid-stream corruption.) */
    uint32_t lp_err = 0;
    uint32_t target_ip_host = parse_ipv4_host(s_ctx.cur_target_host);
    int32_t  rc = NovaOtaPush_BeginToLp(target_ip_host,
                                        total_size,
                                        0u, /* image_crc TBD at Finalize */
                                        "",  /* version forwarded later */
                                        s_ctx.chunk_size ? s_ctx.chunk_size : 1024u,
                                        role,
                                        s_ctx.allow_downgrade,
                                        &lp_err);
    if (rc != NOVA_OTA_PUSH_OK) {
        DebugP_log("[OTA-BROKER] Begin push failed rc=%d lp_err=%u\r\n",
                   (int)rc, (unsigned)lp_err);
        emit_install_progress(component_index, component_name,
                              s_ctx.cur_target_host,
                              FW_INSTALL_FAILED, 0u, total_size,
                              lp_err ? lp_err : 101u,
                              "push Begin failed",
                              FW_INSTALL_OVERALL_FAILED);
        s_ctx.cur_failed = true;
        s_ctx.cur_state  = FW_INSTALL_FAILED;
        s_state          = BROKER_ERROR;
        return;
    }
    s_ctx.cur_push_active = true;

    /* Begin succeeded → LP is ERASING / RECEIVING. Emit those states in
     * order so Pi5's UI animation is faithful. */
    emit_install_progress(component_index, component_name,
                          s_ctx.cur_target_host,
                          FW_INSTALL_ERASING, 0u, total_size,
                          0u, "",
                          FW_INSTALL_OVERALL_INSTALLING);
    emit_install_progress(component_index, component_name,
                          s_ctx.cur_target_host,
                          FW_INSTALL_PUSHING, 0u, total_size,
                          0u, "",
                          FW_INSTALL_OVERALL_INSTALLING);
    s_ctx.cur_state = FW_INSTALL_PUSHING;
    /* First PUSHING progress threshold. */
    s_ctx.cur_next_progress = (total_size * BROKER_PROGRESS_PCT) / 100u;
}

/* CRC-32 (Ethernet/zlib polynomial — matches firmware + orbitOtaPush.ts). */
static uint32_t broker_crc32_continue(uint32_t state,
                                      const uint8_t *data, size_t len)
{
    uint32_t c = state ^ 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        c ^= data[i];
        for (int k = 0; k < 8; k++) {
            c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
    }
    return c ^ 0xFFFFFFFFu;
}

/* Decode FwInstallChunk (envelope 132). Forwards the chunk to the
 * remote LP via the push state machine; on LP error, marks the
 * component FAILED and surfaces the error code. Subsequent chunks
 * for a FAILED component are dropped silently — Pi5 needs ~one UART
 * round-trip to react to our FAILED progress envelope. */
static void decode_chunk(const uint8_t *body, size_t len)
{
    uint32_t component_index = 0;
    uint32_t offset          = 0;
    const uint8_t *data      = NULL;   /* pointer into `body` — no copy */
    uint32_t data_len        = 0;
    uint32_t chunk_crc       = 0;
    bool     chunk_crc_seen  = false;

    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t n = bk_decode_varint(body + pos, len - pos, &tag);
        if (n == 0u) return;
        pos += n;
        uint32_t field = tag >> 3;
        uint8_t  wire  = (uint8_t)(tag & 0x07u);

        if (wire == 0u && (field == 1u || field == 2u || field == 4u)) {
            uint32_t v;
            size_t vn = bk_decode_varint(body + pos, len - pos, &v);
            if (vn == 0u) return;
            pos += vn;
            if      (field == 1u) component_index = v;
            else if (field == 2u) offset          = v;
            else if (field == 4u) { chunk_crc = v; chunk_crc_seen = true; }
        } else if (wire == 2u && field == 3u) {
            uint32_t slen;
            size_t ln = bk_decode_varint(body + pos, len - pos, &slen);
            if (ln == 0u || pos + ln + slen > len) return;
            pos += ln;
            data = body + pos;
            data_len = slen;
            pos += slen;
        } else {
            size_t sk = bk_skip_field(wire, body + pos, len - pos);
            if (sk == 0u) return;
            pos += sk;
        }
    }

    if (component_index != s_ctx.cur_component_index) {
        DebugP_log("[OTA-BROKER] Chunk for idx=%u but current=%u — dropping\r\n",
                   (unsigned)component_index,
                   (unsigned)s_ctx.cur_component_index);
        return;
    }
    if (s_ctx.cur_failed || !s_ctx.cur_push_active) {
        /* Pi5 hasn't seen the FAILED progress yet; swallow quietly. */
        return;
    }
    if (data == NULL || data_len == 0u) {
        return;
    }
    /* If Pi5 didn't supply chunk_crc (rare; legacy bundle), compute it
     * locally so the LP's per-chunk verify still has something to gate on. */
    if (!chunk_crc_seen) {
        chunk_crc = broker_crc32_continue(0u, data, data_len);
    }

    /* Accumulate the running image CRC so Finalize can use it. */
    s_ctx.cur_image_crc = broker_crc32_continue(s_ctx.cur_image_crc,
                                                data, data_len);

    uint32_t lp_err = 0;
    int32_t  rc = NovaOtaPush_WriteChunkToLp(offset, data, data_len,
                                             chunk_crc, &lp_err);
    if (rc != NOVA_OTA_PUSH_OK) {
        DebugP_log("[OTA-BROKER] Chunk@%u push failed rc=%d lp_err=%u\r\n",
                   (unsigned)offset, (int)rc, (unsigned)lp_err);
        emit_install_progress(s_ctx.cur_component_index,
                              s_ctx.cur_component_name,
                              s_ctx.cur_target_host,
                              FW_INSTALL_FAILED,
                              s_ctx.cur_bytes_written, s_ctx.cur_total_size,
                              lp_err ? lp_err : 102u,
                              "chunk push failed",
                              FW_INSTALL_OVERALL_FAILED);
        s_ctx.cur_failed = true;
        s_ctx.cur_state  = FW_INSTALL_FAILED;
        s_ctx.cur_push_active = false;
        s_state          = BROKER_ERROR;
        return;
    }

    s_ctx.cur_bytes_written = offset + data_len;

    /* Throttled PUSHING progress: emit every ~5% of total_size. */
    if (s_ctx.cur_total_size != 0u
            && s_ctx.cur_bytes_written >= s_ctx.cur_next_progress) {
        emit_install_progress(s_ctx.cur_component_index,
                              s_ctx.cur_component_name,
                              s_ctx.cur_target_host,
                              FW_INSTALL_PUSHING,
                              s_ctx.cur_bytes_written, s_ctx.cur_total_size,
                              0u, "",
                              FW_INSTALL_OVERALL_INSTALLING);
        /* Advance threshold by 5% (clamped at total_size to fire the
         * "100%" tick exactly once on the boundary). */
        uint32_t step = (s_ctx.cur_total_size * BROKER_PROGRESS_PCT) / 100u;
        if (step == 0u) step = 1u;
        s_ctx.cur_next_progress += step;
        if (s_ctx.cur_next_progress > s_ctx.cur_total_size) {
            s_ctx.cur_next_progress = s_ctx.cur_total_size;
        }
    }
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

    DebugP_log("[OTA-BROKER] ComponentFinalize idx=%u\r\n",
               (unsigned)component_index);

    if (component_index != s_ctx.cur_component_index || s_ctx.cur_failed) {
        return;
    }
    if (!s_ctx.cur_push_active) {
        return;
    }

    /* VERIFYING → ACTIVATING → REBOOTING → CONFIRMED → DONE.
     * The actual LP-side state transitions inside
     * NovaOtaPush_FinalizeAndActivateLp are:
     *   Finalize sent     → LP runs full image CRC ............ VERIFYING
     *   Status=Verified   → we send Activate(reboot=true) ..... ACTIVATING
     *   Activate sent     → LP starts stage copy + reset ...... REBOOTING
     *   socket close/timeout → LP rebooted ................... CONFIRMED
     *   bundle still has more components OR FwInstallComplete  DONE
     *
     * The push module collapses the sub-step status reads internally,
     * so we emit the externally-visible states around the single call. */
    emit_install_progress(s_ctx.cur_component_index,
                          s_ctx.cur_component_name,
                          s_ctx.cur_target_host,
                          FW_INSTALL_VERIFYING,
                          s_ctx.cur_bytes_written, s_ctx.cur_total_size,
                          0u, "",
                          FW_INSTALL_OVERALL_INSTALLING);

    uint32_t lp_err = 0;
    int32_t  rc = NovaOtaPush_FinalizeAndActivateLp(s_ctx.cur_image_crc,
                                                    &lp_err);
    s_ctx.cur_push_active = false;
    if (rc != NOVA_OTA_PUSH_OK) {
        DebugP_log("[OTA-BROKER] Finalize push failed rc=%d lp_err=%u\r\n",
                   (int)rc, (unsigned)lp_err);
        emit_install_progress(s_ctx.cur_component_index,
                              s_ctx.cur_component_name,
                              s_ctx.cur_target_host,
                              FW_INSTALL_FAILED,
                              s_ctx.cur_bytes_written, s_ctx.cur_total_size,
                              lp_err ? lp_err : 103u,
                              "finalize/activate failed",
                              FW_INSTALL_OVERALL_FAILED);
        s_ctx.cur_failed = true;
        s_ctx.cur_state  = FW_INSTALL_FAILED;
        s_state          = BROKER_ERROR;
        return;
    }

    /* Successful sequence — emit the externally-visible tail states. */
    emit_install_progress(s_ctx.cur_component_index,
                          s_ctx.cur_component_name,
                          s_ctx.cur_target_host,
                          FW_INSTALL_ACTIVATING,
                          s_ctx.cur_bytes_written, s_ctx.cur_total_size,
                          0u, "",
                          FW_INSTALL_OVERALL_INSTALLING);
    emit_install_progress(s_ctx.cur_component_index,
                          s_ctx.cur_component_name,
                          s_ctx.cur_target_host,
                          FW_INSTALL_REBOOTING,
                          s_ctx.cur_bytes_written, s_ctx.cur_total_size,
                          0u, "",
                          FW_INSTALL_OVERALL_INSTALLING);
    emit_install_progress(s_ctx.cur_component_index,
                          s_ctx.cur_component_name,
                          s_ctx.cur_target_host,
                          FW_INSTALL_CONFIRMED,
                          s_ctx.cur_bytes_written, s_ctx.cur_total_size,
                          0u, "",
                          FW_INSTALL_OVERALL_INSTALLING);
    emit_install_progress(s_ctx.cur_component_index,
                          s_ctx.cur_component_name,
                          s_ctx.cur_target_host,
                          FW_INSTALL_DONE,
                          s_ctx.cur_bytes_written, s_ctx.cur_total_size,
                          0u, "",
                          FW_INSTALL_OVERALL_INSTALLING);
    s_ctx.cur_state = FW_INSTALL_DONE;
}

/* Decode FwInstallComplete (envelope 134). Body has no fields today;
 * we just transition state. */
static void decode_complete(const uint8_t *body, size_t len)
{
    (void)body;
    (void)len;
    /* Best-effort close — if anything was left in-flight after a
     * partial bundle, drop the socket cleanly. */
    NovaOtaPush_AbortLp();
    if (s_ctx.cur_failed || s_state == BROKER_ERROR) {
        DebugP_log("[OTA-BROKER] InstallComplete (overall=FAILED)\r\n");
        emit_install_result(FW_INSTALL_OVERALL_FAILED,
                            "one or more components failed");
    } else {
        DebugP_log("[OTA-BROKER] InstallComplete (overall=DONE)\r\n");
        emit_install_result(FW_INSTALL_OVERALL_DONE, "");
    }
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
    /* Close any open push socket without sending Finalize/Activate so
     * the target LP's Bank B is left uncommitted and the next boot
     * comes up on Bank A. */
    NovaOtaPush_AbortLp();
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

    DebugP_log("[OTA-BROKER] FleetProbe timeout=%u\r\n",
               (unsigned)timeout_ms);
    s_state = BROKER_PROBING;
    NovaFwFleetMember members[BROKER_FLEET_MAX];
    size_t count = NovaOtaPush_ProbeFleet(members, BROKER_FLEET_MAX,
                                          timeout_ms);
    emit_fleet_snapshot(members, count);
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
