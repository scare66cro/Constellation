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
 *   - Phase 3.6 (2026-05-22) — controller-self-update branch wired:
 *     when the incoming component's role equals our own
 *     (`LpDeviceConfig_Get()->role`), the same `FwInstallChunk` byte
 *     stream is routed into `NovaFwUpdate_Begin/WriteChunk/Finalize/
 *     Activate(reboot=true)` instead of `NovaOtaPush_*`. The broker
 *     emits VERIFYING → ACTIVATING → REBOOTING progress envelopes
 *     around the local Activate call (it triggers a warm SoC reset, so
 *     CONFIRMED / DONE never get sent for the self-update — Pi5
 *     reconciles via post-reboot Heartbeat / VersionInfo). See
 *     `decode_component_begin` for the bundle-ordering constraint that
 *     keeps this safe.
 *   - Phase 3.7 (2026-05-23) — concurrency split. Up through 3.6 every
 *     decoder ran on the same task that owned UART RX and the 100 ms
 *     heartbeat emit (`bridge_uart_task` in main.c). The leaf
 *     operations (TCP push to remote LP via lwip_send/lwip_recv,
 *     OSPI flash writes via hal_flash_write_dac) are synchronous and
 *     block for seconds — long enough for the bridge to declare the
 *     UART dead and reconnect-loop. This file now splits into:
 *       (a) `NovaOtaBroker_OnEnvelope` runs on `bridge_uart_task` and
 *           does only COPY-and-signal — sub-millisecond.
 *       (b) `nova_ota_broker_task` (created in `NovaOtaBroker_Init`,
 *           runs once the scheduler starts) drains the chunk ring +
 *           command queue and performs ALL leaf I/O. Heartbeat keeps
 *           flowing on `bridge_uart_task` regardless of install state.
 *     Memory: 16-slot chunk ring × 1024 B per slot = 16 KB MSRAM.
 *     Command queue depth 4, ≤256 B body each ≈ 1.1 KB.
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
 *   9. No DebugP_log before the scheduler starts — Init uses bb_uart0_puts
 *      indirectly via the same pattern as main.c's banner. All decoder
 *      DebugP_log calls now run on `nova_ota_broker_task` which is
 *      created pre-scheduler but only RUNS post-scheduler-start, so the
 *      logs are scheduler-safe.
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

#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <kernel/dpl/DebugP.h>
#include <kernel/dpl/ClockP.h>

#include "nova_protocol.h"
#include "nova_ota_push.h"
#include "nova_fw_update.h"
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
/* Phase 3.7: queue/ring overflow errors. ≥100 = runtime. */
#define BROKER_ERR_CHUNK_RING_FULL      104u
#define BROKER_ERR_CMD_QUEUE_FULL       105u

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
#define BROKER_VERSION_MAX        48      /* "0.A.<n>+<sha>-dirty" headroom */
#define BROKER_FLEET_MAX          8       /* cached members per install */
#define BROKER_PROGRESS_PCT       5u      /* emit PUSHING every ~N% */

/* Synthetic target_host label used for the controller self-update path.
 * Pi5's UI keys progress envelopes off (component_index, target_host)
 * so an empty / numeric host on the self path would visually collide
 * with the orbit-push paths. "self" is unambiguous. */
#define BROKER_SELF_HOST          "self"

typedef struct {
    /* Bundle-wide */
    uint32_t component_count;
    uint32_t chunk_size;            /* hint from Pi5 (0 = default 1024) */
    bool     allow_downgrade;
    /* Current component */
    uint32_t cur_component_index;
    char     cur_component_name[BROKER_NAME_MAX];
    char     cur_target_host[BROKER_TARGET_HOST_MAX];
    char     cur_expected_version[BROKER_VERSION_MAX];
    uint32_t cur_role;
    uint32_t cur_total_size;
    uint32_t cur_bytes_written;
    uint32_t cur_image_crc;          /* accumulated chunk CRC for Finalize */
    uint32_t cur_next_progress;      /* next bytes_written threshold to emit */
    bool     cur_is_self;             /* this component targets our own R5F */
    bool     cur_push_active;        /* a remote-LP push session is open */
    bool     cur_self_active;         /* a NovaFwUpdate self-update session is open */
    bool     cur_rebooting;           /* self-update Activate has been invoked */
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

/* Lock-free flag for `NovaOtaBroker_IsInInstallMode()`. Updated by the
 * broker task on state transitions; read by main.c's per-tick
 * broadcasters without needing the mutex. `volatile` ensures the
 * compiler doesn't cache it in a register across the read. */
static volatile bool s_install_active = false;

/* ─── Concurrency primitives (Phase 3.7) ─────────────────────────────── */

/* Recursive mutex protecting `s_ctx`, `s_state`, the chunk ring head/
 * tail pointers, and the command queue head/tail. UART RX (enqueue
 * side) takes it briefly per envelope; the broker task takes it during
 * state transitions and to dequeue. Hold windows MUST be short — no
 * I/O inside the locked region. Leaf operations run unlocked on the
 * broker task. */
static SemaphoreHandle_t s_ctx_mtx = NULL;
static StaticSemaphore_t s_ctx_mtx_buf;

/* Binary semaphore — UART RX gives, broker task takes (with timeout =
 * tick period). One give covers any number of enqueues; broker drains
 * everything it sees per wake. */
static SemaphoreHandle_t s_wake_sem = NULL;
static StaticSemaphore_t s_wake_sem_buf;

/* Chunk ring. Each slot owns up to BROKER_CHUNK_MAX_BYTES of payload
 * plus the envelope metadata. With chunk_size = 1024 and depth = 16
 * we burn 16 KB MSRAM, sized to absorb a few-second Bank-B erase pause
 * on the LP without ever back-pressuring the UART RX side. If the
 * bridge somehow streams faster than the LP drains for the full
 * 16-slot window, the next enqueue fails clean (see
 * BROKER_ERR_CHUNK_RING_FULL).
 *
 * Today's bridge ships 1024-byte chunks; we cap at 2048 in case a
 * future bridge negotiates larger chunks via the FwInstallBegin
 * `chunk_size` hint. Reduce if MSRAM budget tightens. */
#define BROKER_CHUNK_MAX_BYTES    2048u
#define BROKER_CHUNK_RING_DEPTH   16u

typedef struct {
    uint32_t tag;                /* always 132 — kept for symmetry */
    uint32_t envelope_seq;
    uint32_t body_len;
    uint8_t  body[BROKER_CHUNK_MAX_BYTES];
} BrokerChunkSlot;

static BrokerChunkSlot s_chunk_ring[BROKER_CHUNK_RING_DEPTH];
static uint32_t        s_chunk_ring_head = 0;   /* next slot to write */
static uint32_t        s_chunk_ring_tail = 0;   /* next slot to read  */
static uint32_t        s_chunk_ring_count = 0;

/* Command queue for the non-chunk envelopes (Begin / ComponentBegin /
 * ComponentFinalize / Complete / Abort / FleetProbe). Body cap of 256
 * bytes is generous — the largest payload here is FwInstallBegin which
 * carries a 32-byte SHA + version string + a handful of varints. */
#define BROKER_CMD_MAX_BYTES      256u
#define BROKER_CMD_QUEUE_DEPTH    4u

typedef struct {
    uint32_t tag;                /* 130/131/133/134/135/136 */
    uint32_t envelope_seq;
    uint32_t body_len;
    uint8_t  body[BROKER_CMD_MAX_BYTES];
} BrokerCmdSlot;

static BrokerCmdSlot s_cmd_queue[BROKER_CMD_QUEUE_DEPTH];
static uint32_t      s_cmd_queue_head = 0;
static uint32_t      s_cmd_queue_tail = 0;
static uint32_t      s_cmd_queue_count = 0;

/* Broker task. Mirrors lp_ota_task's sizing & priority (CLAUDE.md
 * mention of the same TCP listener pattern). Priority lower than
 * UART RX so RX always wins under contention; higher than idle. */
#define BROKER_TASK_STACK_BYTES   (8u * 1024u)
#define BROKER_TASK_STACK_WORDS   (BROKER_TASK_STACK_BYTES / sizeof(configSTACK_DEPTH_TYPE))
#define BROKER_TASK_PRI           (configMAX_PRIORITIES - 6)

static StackType_t   s_broker_task_stack[BROKER_TASK_STACK_WORDS] __attribute__((aligned(32)));
static StaticTask_t  s_broker_task_obj;
static TaskHandle_t  s_broker_task_handle = NULL;

static void nova_ota_broker_task(void *args);

/* ─── Mutex helpers — short, named, never-fail at runtime ─────────────── */
static inline void broker_lock(void)
{
    if (s_ctx_mtx != NULL) {
        (void)xSemaphoreTakeRecursive(s_ctx_mtx, portMAX_DELAY);
    }
}
static inline void broker_unlock(void)
{
    if (s_ctx_mtx != NULL) {
        (void)xSemaphoreGiveRecursive(s_ctx_mtx);
    }
}

/* Mark install-active state. Called only from the broker task on state
 * transitions; readers (main.c gates + IsInInstallMode) read the
 * volatile flag lock-free. */
static inline void broker_set_state_locked(NovaOtaBrokerState st)
{
    s_state = st;
    s_install_active = (st == BROKER_INSTALLING) || (st == BROKER_ERROR);
}

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
 * encode overflow. NovaProto_SendRaw is mutex-serialized per CLAUDE.md
 * invariant #4 — safe to call from the broker task alongside heartbeat
 * emissions from bridge_uart_task. */
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
 * pay the probe cost. Runs on the broker task — the underlying probe
 * does multiple lwip_connect calls and may block for `timeout_ms`. */
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

/* Forward decl for the dispatcher (defined alongside the chunk decoder). */
static uint32_t broker_crc32_continue(uint32_t state,
                                      const uint8_t *data, size_t len);

/* ─── Target-agnostic dispatch layer ──────────────────────────────────
 *
 * Phase 3.6: replaces the direct `NovaOtaPush_*` calls inside the
 * begin/chunk/finalize/abort decoders with one of two leaves chosen by
 * `s_ctx.cur_is_self`:
 *
 *     true  → NovaFwUpdate_*  (controller self-update, local OSPI write)
 *     false → NovaOtaPush_*   (remote orbit LP, TCP push, port 5503)
 *
 * Keeps the decoder code identical between paths — they emit the same
 * FwInstallProgress states, just route the bytes differently. The
 * self-update path's Activate is special-cased in the Finalize decoder
 * because it triggers a warm SoC reset that Pi5 has to discover from
 * the missing UART, not from a CONFIRMED/DONE envelope.
 *
 * Return codes: 0 = success, non-zero = error (passed through as the
 * `error_code` in FwInstallProgress(state=FAILED)). Push paths translate
 * NOVA_OTA_PUSH_ERR_* into the corresponding LP_OTA_ERR_* via out_lp_err;
 * the self path returns NovaFwUpdate_* error codes directly (1..5).
 *
 * Phase 3.7: these now run exclusively on `nova_ota_broker_task`, so
 * blocking lwip_send/lwip_recv / hal_flash_write_dac calls no longer
 * starve the heartbeat.
 */
static uint32_t target_begin(uint32_t total_size, uint32_t chunk_size,
                             uint32_t role, bool allow_downgrade,
                             const char *version, uint32_t *out_lp_err)
{
    if (s_ctx.cur_is_self) {
        /* No fleet pre-flight, no role gate (we ARE the target — the
         * bundle's manifest already chose us). The NovaFwUpdate layer
         * gates on its own state machine. */
        uint32_t rc = NovaFwUpdate_Begin(total_size,
                                         0u, /* image_crc accumulated chunk-by-chunk */
                                         version,
                                         chunk_size ? chunk_size : 1024u);
        if (out_lp_err) *out_lp_err = rc;
        return rc;
    }

    uint32_t lp_err = 0;
    uint32_t target_ip_host = parse_ipv4_host(s_ctx.cur_target_host);
    int32_t  rc = NovaOtaPush_BeginToLp(target_ip_host,
                                        total_size,
                                        0u, /* image_crc TBD at Finalize */
                                        version,
                                        chunk_size ? chunk_size : 1024u,
                                        role,
                                        allow_downgrade,
                                        &lp_err);
    if (out_lp_err) *out_lp_err = lp_err;
    return (rc == NOVA_OTA_PUSH_OK) ? 0u : (lp_err ? lp_err : 101u);
}

static uint32_t target_chunk(uint32_t offset, const uint8_t *data,
                             uint32_t len, uint32_t chunk_crc,
                             uint32_t *out_lp_err)
{
    if (s_ctx.cur_is_self) {
        uint32_t rc = NovaFwUpdate_WriteChunk(offset, data, len, chunk_crc);
        if (out_lp_err) *out_lp_err = rc;
        return rc;
    }

    uint32_t lp_err = 0;
    int32_t  rc = NovaOtaPush_WriteChunkToLp(offset, data, len,
                                             chunk_crc, &lp_err);
    if (out_lp_err) *out_lp_err = lp_err;
    return (rc == NOVA_OTA_PUSH_OK) ? 0u : (lp_err ? lp_err : 102u);
}

/* Finalize ONLY — does NOT call Activate. The decoder calls Activate
 * separately so it can interleave VERIFYING / ACTIVATING / REBOOTING
 * progress emissions around the local Activate (which triggers a warm
 * reset and never returns control for the self path). For the orbit-
 * push path the LP-side handles the same sequence internally, so we
 * call NovaOtaPush_FinalizeAndActivateLp as one shot. */
static uint32_t target_finalize_and_maybe_activate(uint32_t expected_crc,
                                                   uint32_t *out_lp_err)
{
    if (s_ctx.cur_is_self) {
        /* Self path: Finalize verifies, writes Bank B header. Activate
         * is invoked separately by the caller after the VERIFYING
         * progress emission. */
        uint32_t rc = NovaFwUpdate_Finalize(expected_crc);
        if (out_lp_err) *out_lp_err = rc;
        return rc;
    }

    uint32_t lp_err = 0;
    int32_t  rc = NovaOtaPush_FinalizeAndActivateLp(expected_crc, &lp_err);
    if (out_lp_err) *out_lp_err = lp_err;
    return (rc == NOVA_OTA_PUSH_OK) ? 0u : (lp_err ? lp_err : 103u);
}

static void target_abort(void)
{
    if (s_ctx.cur_is_self) {
        if (s_ctx.cur_self_active) {
            NovaFwUpdate_Abort();
            s_ctx.cur_self_active = false;
        }
        return;
    }
    if (s_ctx.cur_push_active) {
        NovaOtaPush_AbortLp();
        s_ctx.cur_push_active = false;
    }
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

/* Decode FwInstallBegin (envelope 130). Runs on the broker task. */
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
    broker_lock();
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_ctx.component_count = component_count;
    s_ctx.chunk_size      = chunk_size;
    s_ctx.allow_downgrade = allow_downgrade;
    broker_set_state_locked(BROKER_INSTALLING);
    broker_unlock();

    DebugP_log("[OTA-BROKER] InstallBegin bundle=\"%s\" components=%u "
               "chunk=%u downgrade=%d\r\n",
               bundle_version, (unsigned)component_count,
               (unsigned)chunk_size, (int)allow_downgrade);
}

/* Decode FwInstallComponentBegin (envelope 131). Runs on the broker task. */
static void decode_component_begin(const uint8_t *body, size_t len)
{
    uint32_t component_index = 0;
    uint32_t role             = 0;
    uint32_t total_size       = 0;
    char     component_name[BROKER_NAME_MAX] = {0};
    char     expected_version[BROKER_VERSION_MAX] = {0};

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
            } else if (field == 6u) {
                /* Phase 3.6: capture expected_version so the self-update
                 * path can pass it through to NovaFwUpdate_Begin (it
                 * lands in the Bank B FwBankHeader.version field, which
                 * the post-reboot VersionInfo broadcast surfaces). The
                 * orbit-push path also forwards it via FwBeginUpdate. */
                bk_copy_string(expected_version, sizeof(expected_version),
                               body + pos, slen);
            }
            /* field 5 image_sha256: still not used (Pi5 verifies the
             * bundle SHA pre-stream). */
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

    /* Stage the per-component scratch. Locked because the UART RX side
     * peeks at cur_component_index / cur_is_self for the chunk-enqueue
     * gate (see NovaOtaBroker_OnEnvelope). */
    broker_lock();
    s_ctx.cur_component_index = component_index;
    s_ctx.cur_role            = role;
    s_ctx.cur_total_size      = total_size;
    s_ctx.cur_bytes_written   = 0u;
    s_ctx.cur_image_crc       = 0u;
    s_ctx.cur_next_progress   = 0u;
    s_ctx.cur_push_active     = false;
    s_ctx.cur_self_active     = false;
    s_ctx.cur_rebooting       = false;
    s_ctx.cur_failed          = false;
    s_ctx.cur_state           = FW_INSTALL_BEGIN;
    bk_copy_string(s_ctx.cur_component_name,
                   sizeof(s_ctx.cur_component_name),
                   (const uint8_t *)component_name, strlen(component_name));
    bk_copy_string(s_ctx.cur_expected_version,
                   sizeof(s_ctx.cur_expected_version),
                   (const uint8_t *)expected_version, strlen(expected_version));
    s_ctx.cur_target_host[0]  = '\0';

    /* Phase 3.6: own role → self-update path; else → remote orbit push.
     *
     * BUNDLE ORDERING CONSTRAINT — Pi5's responsibility, not ours:
     * Controller (self-update) component MUST be installed last in the
     * bundle. The Activate call inside `decode_component_finalize`
     * triggers a warm SoC reset (`NovaFwUpdate_Activate(true)` →
     * `Sciclient_pmDeviceReset`), which drops all install state in
     * `s_ctx` and closes the UART. Any components scheduled AFTER the
     * controller in the bundle will never be pushed — their
     * `FwInstallComponentBegin` envelopes hit a freshly-booted Nova
     * that's in IDLE state and treats them as orphaned. Pi5's
     * `firmwareInstaller.ts` sorts the manifest so the controller
     * component is appended last; the broker does not enforce this and
     * has no way to (it doesn't see the full bundle layout until each
     * ComponentBegin arrives).
     */
    OrbitRole own_role = (OrbitRole)LpDeviceConfig_Get()->role;
    s_ctx.cur_is_self = ((uint32_t)own_role == role);
    broker_unlock();

    DebugP_log("[OTA-BROKER] ComponentBegin idx=%u name=\"%s\" "
               "role=%u total=%u is_self=%d ver=\"%s\"\r\n",
               (unsigned)component_index, component_name,
               (unsigned)role, (unsigned)total_size,
               (int)s_ctx.cur_is_self, expected_version);

    if (s_ctx.cur_is_self) {
        /* Synthetic target_host so Pi5's progress-row key is unique. */
        broker_lock();
        bk_copy_string(s_ctx.cur_target_host,
                       sizeof(s_ctx.cur_target_host),
                       (const uint8_t *)BROKER_SELF_HOST,
                       strlen(BROKER_SELF_HOST));
        broker_unlock();
    } else {
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
            broker_lock();
            s_ctx.cur_failed = true;
            s_ctx.cur_state  = FW_INSTALL_FAILED;
            broker_set_state_locked(BROKER_ERROR);
            broker_unlock();
            return;
        }
        broker_lock();
        bk_copy_string(s_ctx.cur_target_host,
                       sizeof(s_ctx.cur_target_host),
                       (const uint8_t *)m->host, strlen(m->host));
        broker_unlock();
    }

    /* Emit BEGIN so Pi5's UI moves off "queued". The image_crc and the
     * full chunk-stream haven't started yet — total_size is the only
     * meaningful counter at this point. */
    emit_install_progress(component_index, component_name,
                          s_ctx.cur_target_host,
                          FW_INSTALL_BEGIN, 0u, total_size,
                          0u, "",
                          FW_INSTALL_OVERALL_INSTALLING);

    /* Dispatch to the target leaf. For the self path this calls
     * NovaFwUpdate_Begin (synchronous block-erase of first sector +
     * state transition to RECEIVING). For the orbit path this opens
     * TCP, runs the LP-side pre-flight (FwBankInfo cross-check), sends
     * FwBeginUpdate, and waits for the first FwUpdateStatus. The image
     * CRC for Begin is left 0 on both paths — Pi5 never streams the
     * full CRC to us in a single message; we accumulate as we forward
     * chunks and pass the running CRC to Finalize. */
    uint32_t lp_err = 0;
    uint32_t rc = target_begin(total_size,
                               s_ctx.chunk_size,
                               role,
                               s_ctx.allow_downgrade,
                               s_ctx.cur_expected_version,
                               &lp_err);
    if (rc != 0u) {
        DebugP_log("[OTA-BROKER] Begin %s failed rc=%u lp_err=%u\r\n",
                   s_ctx.cur_is_self ? "self" : "push",
                   (unsigned)rc, (unsigned)lp_err);
        emit_install_progress(component_index, component_name,
                              s_ctx.cur_target_host,
                              FW_INSTALL_FAILED, 0u, total_size,
                              lp_err ? lp_err : 101u,
                              s_ctx.cur_is_self
                                  ? "self-update Begin failed"
                                  : "push Begin failed",
                              FW_INSTALL_OVERALL_FAILED);
        broker_lock();
        s_ctx.cur_failed = true;
        s_ctx.cur_state  = FW_INSTALL_FAILED;
        broker_set_state_locked(BROKER_ERROR);
        broker_unlock();
        return;
    }
    broker_lock();
    if (s_ctx.cur_is_self) s_ctx.cur_self_active = true;
    else                    s_ctx.cur_push_active = true;
    broker_unlock();

    /* Begin succeeded → emit ERASING / PUSHING in quick succession so
     * Pi5's UI animation is faithful. (Self path: NovaFwUpdate_Begin
     * already pre-erased the first sector and lazy-erases on demand.
     * Push path: the LP did a synchronous Bank-B block erase before
     * replying.) */
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
    broker_lock();
    s_ctx.cur_state = FW_INSTALL_PUSHING;
    /* First PUSHING progress threshold. */
    s_ctx.cur_next_progress = (total_size * BROKER_PROGRESS_PCT) / 100u;
    broker_unlock();
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
 * round-trip to react to our FAILED progress envelope.
 *
 * Phase 3.7: runs on the broker task; `body` points into the broker-
 * owned chunk ring slot, which is stable until we return. */
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
    bool any_active = s_ctx.cur_is_self
                         ? s_ctx.cur_self_active
                         : s_ctx.cur_push_active;
    if (s_ctx.cur_failed || !any_active) {
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
    uint32_t rc = target_chunk(offset, data, data_len, chunk_crc, &lp_err);
    if (rc != 0u) {
        DebugP_log("[OTA-BROKER] Chunk@%u %s failed rc=%u lp_err=%u\r\n",
                   (unsigned)offset,
                   s_ctx.cur_is_self ? "self" : "push",
                   (unsigned)rc, (unsigned)lp_err);
        emit_install_progress(s_ctx.cur_component_index,
                              s_ctx.cur_component_name,
                              s_ctx.cur_target_host,
                              FW_INSTALL_FAILED,
                              s_ctx.cur_bytes_written, s_ctx.cur_total_size,
                              lp_err ? lp_err : 102u,
                              s_ctx.cur_is_self
                                  ? "self-update chunk failed"
                                  : "chunk push failed",
                              FW_INSTALL_OVERALL_FAILED);
        broker_lock();
        s_ctx.cur_failed = true;
        s_ctx.cur_state  = FW_INSTALL_FAILED;
        s_ctx.cur_push_active = false;
        s_ctx.cur_self_active = false;
        broker_set_state_locked(BROKER_ERROR);
        broker_unlock();
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
    bool any_active = s_ctx.cur_is_self
                         ? s_ctx.cur_self_active
                         : s_ctx.cur_push_active;
    if (!any_active) {
        return;
    }

    /* VERIFYING → ACTIVATING → REBOOTING → CONFIRMED → DONE.
     * On the orbit-push path the LP collapses these sub-steps inside
     * NovaOtaPush_FinalizeAndActivateLp:
     *   Finalize sent     → LP runs full image CRC ............ VERIFYING
     *   Status=Verified   → we send Activate(reboot=true) ..... ACTIVATING
     *   Activate sent     → LP starts stage copy + reset ...... REBOOTING
     *   socket close/timeout → LP rebooted ................... CONFIRMED
     *   bundle still has more components OR FwInstallComplete  DONE
     * For the self-update path we issue Finalize and Activate as two
     * separate steps so we can emit progress envelopes between them
     * (the Activate call triggers a warm SoC reset that never returns;
     * CONFIRMED / DONE for self-update are reconstructed Pi5-side from
     * the post-reboot Heartbeat / VersionInfo broadcasts in Phase 4). */
    emit_install_progress(s_ctx.cur_component_index,
                          s_ctx.cur_component_name,
                          s_ctx.cur_target_host,
                          FW_INSTALL_VERIFYING,
                          s_ctx.cur_bytes_written, s_ctx.cur_total_size,
                          0u, "",
                          FW_INSTALL_OVERALL_INSTALLING);

    uint32_t lp_err = 0;
    uint32_t rc = target_finalize_and_maybe_activate(s_ctx.cur_image_crc,
                                                     &lp_err);
    if (rc != 0u) {
        DebugP_log("[OTA-BROKER] Finalize %s failed rc=%u lp_err=%u\r\n",
                   s_ctx.cur_is_self ? "self" : "push",
                   (unsigned)rc, (unsigned)lp_err);
        if (s_ctx.cur_is_self) {
            /* Roll back the half-written bank so a subsequent retry
             * starts clean. (Activate was never called → no bank flip,
             * but the FwBankHeader for Bank B is left invalid by
             * Finalize-time abort regardless.) */
            NovaFwUpdate_Abort();
            broker_lock();
            s_ctx.cur_self_active = false;
            broker_unlock();
        } else {
            broker_lock();
            s_ctx.cur_push_active = false;
            broker_unlock();
        }
        emit_install_progress(s_ctx.cur_component_index,
                              s_ctx.cur_component_name,
                              s_ctx.cur_target_host,
                              FW_INSTALL_FAILED,
                              s_ctx.cur_bytes_written, s_ctx.cur_total_size,
                              lp_err ? lp_err : 103u,
                              s_ctx.cur_is_self
                                  ? "self-update finalize/verify failed"
                                  : "finalize/activate failed",
                              FW_INSTALL_OVERALL_FAILED);
        broker_lock();
        s_ctx.cur_failed = true;
        s_ctx.cur_state  = FW_INSTALL_FAILED;
        broker_set_state_locked(BROKER_ERROR);
        broker_unlock();
        return;
    }

    if (s_ctx.cur_is_self) {
        /* Self path — Finalize verified Bank B and wrote its header
         * (valid=1, active=0). Now run the activation sequence: emit
         * ACTIVATING immediately before the local Activate call (which
         * flips the active bit and triggers a warm SoC reset via
         * Sciclient_pmDeviceReset), then emit REBOOTING immediately
         * after so the bridge sees one last envelope before UART goes
         * silent. The reset latency between Activate's bank-header
         * write and the actual CPU reset gives us a handful of
         * milliseconds — enough for one envelope, not enough for many.
         * Do NOT emit CONFIRMED/DONE here — Nova won't be alive to
         * transmit them. Pi5's firmwareInstaller reconciles
         * controller-self success from the post-reboot Heartbeat +
         * VersionInfo broadcasts (Phase 4 work). */
        emit_install_progress(s_ctx.cur_component_index,
                              s_ctx.cur_component_name,
                              s_ctx.cur_target_host,
                              FW_INSTALL_ACTIVATING,
                              s_ctx.cur_bytes_written, s_ctx.cur_total_size,
                              0u, "",
                              FW_INSTALL_OVERALL_INSTALLING);

        broker_lock();
        s_ctx.cur_rebooting = true;
        s_ctx.cur_state     = FW_INSTALL_REBOOTING;
        broker_unlock();

        DebugP_log("[OTA-BROKER] self-update Activate (warm reset imminent)\r\n");
        uint32_t arc = NovaFwUpdate_Activate(true);
        /* NovaFwUpdate_Activate(reboot=true) calls Sciclient_pmDeviceReset
         * which normally never returns. If we *do* see control again,
         * the reset failed; treat that as a hard error so Pi5 isn't
         * left waiting for a heartbeat that won't change. */
        broker_lock();
        s_ctx.cur_self_active = false;
        broker_unlock();
        if (arc != 0u) {
            DebugP_log("[OTA-BROKER] self-update Activate returned rc=%u (reset failed?)\r\n",
                       (unsigned)arc);
            emit_install_progress(s_ctx.cur_component_index,
                                  s_ctx.cur_component_name,
                                  s_ctx.cur_target_host,
                                  FW_INSTALL_FAILED,
                                  s_ctx.cur_bytes_written, s_ctx.cur_total_size,
                                  arc,
                                  "self-update Activate failed (no reset)",
                                  FW_INSTALL_OVERALL_FAILED);
            broker_lock();
            s_ctx.cur_failed = true;
            s_ctx.cur_state  = FW_INSTALL_FAILED;
            broker_set_state_locked(BROKER_ERROR);
            broker_unlock();
            return;
        }

        /* Best-effort REBOOTING tick — the reset usually fires before
         * this envelope clears the UART, but the bridge's COBS framer
         * is robust to a mid-envelope cut so partial transmits don't
         * corrupt the next post-reboot stream. */
        emit_install_progress(s_ctx.cur_component_index,
                              s_ctx.cur_component_name,
                              s_ctx.cur_target_host,
                              FW_INSTALL_REBOOTING,
                              s_ctx.cur_bytes_written, s_ctx.cur_total_size,
                              0u, "",
                              FW_INSTALL_OVERALL_INSTALLING);
        /* Fall through. The CPU reset typically fires within a few ms;
         * if it somehow doesn't, the broker stays in BROKER_INSTALLING
         * with cur_rebooting=true and any FwInstallComplete envelope
         * that arrives is short-circuited in decode_complete. */
        return;
    }

    /* Orbit-push path: the push module already did Activate inline.
     * Emit the externally-visible tail states around its single call. */
    broker_lock();
    s_ctx.cur_push_active = false;
    broker_unlock();
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
    broker_lock();
    s_ctx.cur_state = FW_INSTALL_DONE;
    broker_unlock();
}

/* Decode FwInstallComplete (envelope 134). Body has no fields today;
 * we just transition state. */
static void decode_complete(const uint8_t *body, size_t len)
{
    (void)body;
    (void)len;

    /* Phase 3.6 race: if the controller (self-update) component was the
     * last in the bundle, Pi5 sends FwInstallComponentFinalize → Nova
     * runs NovaFwUpdate_Activate(true) → warm reset → Pi5 sends
     * FwInstallComplete. In the ~ms between Activate's bank-header
     * write and the actual CPU reset, the Complete envelope may arrive
     * and land here. We CANNOT emit FwInstallResult — Nova will reset
     * mid-transmit and the partial envelope corrupts Pi5's framer for
     * the next post-reboot stream. Just acknowledge state and let the
     * reset fire. Pi5 reconciles overall success from the post-reboot
     * Heartbeat / VersionInfo (Phase 4 work). */
    if (s_ctx.cur_rebooting) {
        DebugP_log("[OTA-BROKER] InstallComplete during self-update REBOOTING — "
                   "swallowing (reset imminent)\r\n");
        return;
    }

    /* Best-effort close — if anything was left in-flight after a
     * partial bundle, drop the socket / abort the self session cleanly. */
    target_abort();
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
    broker_lock();
    memset(&s_ctx, 0, sizeof(s_ctx));
    broker_set_state_locked(BROKER_IDLE);
    broker_unlock();
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
    /* Roll back whatever leaf is mid-stream:
     *   self path → NovaFwUpdate_Abort (Bank B FwBankHeader stays
     *               invalid; next boot picks Bank A regardless).
     *   push path → NovaOtaPush_AbortLp (close the TCP socket without
     *               sending Finalize/Activate; the remote LP's Bank B
     *               stays uncommitted). */
    target_abort();
    /* Nova doesn't reboot for an abort; emit the result envelope normally. */
    emit_install_result(FW_INSTALL_OVERALL_ABORTED,
                        reason[0] ? reason : "aborted by Pi5");
    broker_lock();
    memset(&s_ctx, 0, sizeof(s_ctx));
    broker_set_state_locked(BROKER_IDLE);
    broker_unlock();
}

/* Decode FwFleetProbe (envelope 136). */
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
    broker_lock();
    /* Probing is transient — don't promote install_active for this. */
    s_state = BROKER_PROBING;
    broker_unlock();
    NovaFwFleetMember members[BROKER_FLEET_MAX];
    size_t count = NovaOtaPush_ProbeFleet(members, BROKER_FLEET_MAX,
                                          timeout_ms);
    emit_fleet_snapshot(members, count);
    broker_lock();
    /* Only fall back to IDLE if no install slipped in while we were
     * probing (rare — bridge serialises envelopes — but cheap to gate). */
    if (s_state == BROKER_PROBING) {
        broker_set_state_locked(BROKER_IDLE);
    }
    broker_unlock();
}

/* ─── Broker task — drains chunk ring + command queue (Phase 3.7) ─────
 *
 * Wakes on s_wake_sem (one give per enqueue from UART RX) with a 100 ms
 * fallback timeout that also serves as the periodic tick. Drains
 * chunks first (they're the high-rate path), then commands, then
 * loops back to wait. Leaf I/O happens unlocked — the mutex is only
 * held for short queue-pointer manipulation and state writes.
 */
static bool chunk_ring_pop_locked(BrokerChunkSlot *out)
{
    if (s_chunk_ring_count == 0u) return false;
    *out = s_chunk_ring[s_chunk_ring_tail];
    s_chunk_ring_tail = (s_chunk_ring_tail + 1u) % BROKER_CHUNK_RING_DEPTH;
    s_chunk_ring_count--;
    return true;
}

static bool cmd_queue_pop_locked(BrokerCmdSlot *out)
{
    if (s_cmd_queue_count == 0u) return false;
    *out = s_cmd_queue[s_cmd_queue_tail];
    s_cmd_queue_tail = (s_cmd_queue_tail + 1u) % BROKER_CMD_QUEUE_DEPTH;
    s_cmd_queue_count--;
    return true;
}

static void dispatch_cmd(const BrokerCmdSlot *slot)
{
    switch (slot->tag) {
    case 130u: decode_install_begin      (slot->body, slot->body_len); break;
    case 131u: decode_component_begin    (slot->body, slot->body_len); break;
    case 133u: decode_component_finalize (slot->body, slot->body_len); break;
    case 134u: decode_complete           (slot->body, slot->body_len); break;
    case 135u: decode_abort              (slot->body, slot->body_len); break;
    case 136u: decode_fleet_probe        (slot->body, slot->body_len); break;
    default:
        DebugP_log("[OTA-BROKER] dispatch_cmd: unknown tag %u\r\n",
                   (unsigned)slot->tag);
        break;
    }
}

static void nova_ota_broker_task(void *args)
{
    (void)args;

    DebugP_log("[OTA-BROKER] task entered (stack=%uB pri=%d)\r\n",
               (unsigned)BROKER_TASK_STACK_BYTES, (int)BROKER_TASK_PRI);

    while (1) {
        /* Block on the wake semaphore; the 100 ms timeout also acts as
         * the periodic tick for any future time-driven maintenance.
         * Today's broker has no internal timers (Activate returns or
         * the SoC resets — there's nothing the broker needs to nudge
         * on the wall clock), so this is essentially a wait-for-work. */
        (void)xSemaphoreTake(s_wake_sem, pdMS_TO_TICKS(100));

        /* Drain everything currently visible. Chunks first — they're
         * the bulk-throughput path and the bridge will already be
         * back-pressured if we let them sit. Commands second. The
         * UART RX side may concurrently push more while we drain;
         * the next semaphore-give wakes us again. */
        for (;;) {
            BrokerChunkSlot ch;
            bool got_chunk = false;
            broker_lock();
            got_chunk = chunk_ring_pop_locked(&ch);
            broker_unlock();
            if (!got_chunk) break;

            decode_chunk(ch.body, ch.body_len);
        }

        for (;;) {
            BrokerCmdSlot cmd;
            bool got_cmd = false;
            broker_lock();
            got_cmd = cmd_queue_pop_locked(&cmd);
            broker_unlock();
            if (!got_cmd) break;

            dispatch_cmd(&cmd);
        }
    }
}

/* ─── Public API ─────────────────────────────────────────────────────── */

void NovaOtaBroker_Init(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
    s_state          = BROKER_IDLE;
    s_install_active = false;

    s_chunk_ring_head  = s_chunk_ring_tail  = s_chunk_ring_count  = 0u;
    s_cmd_queue_head   = s_cmd_queue_tail   = s_cmd_queue_count   = 0u;

    s_ctx_mtx = xSemaphoreCreateRecursiveMutexStatic(&s_ctx_mtx_buf);
    configASSERT(s_ctx_mtx != NULL);

    s_wake_sem = xSemaphoreCreateBinaryStatic(&s_wake_sem_buf);
    configASSERT(s_wake_sem != NULL);

    /* Create the task. It will sit in xSemaphoreTake until the
     * scheduler starts. Mirrors lp_ota_task's xTaskCreateStatic
     * pattern in main.c, but the storage is local to this file so
     * main.c doesn't need to know our internal sizing. */
    s_broker_task_handle = xTaskCreateStatic(
        nova_ota_broker_task,
        "ota_broker",
        BROKER_TASK_STACK_WORDS,
        NULL,
        BROKER_TASK_PRI,
        s_broker_task_stack,
        &s_broker_task_obj);
    configASSERT(s_broker_task_handle != NULL);

    /* No DebugP_log here — Init runs pre-scheduler (called from main()
     * alongside NovaFwUpdate_Init), and DebugP_log uses the FreeRTOS
     * mutex which is undefined before vTaskStartScheduler.
     * Per CLAUDE.md hard-invariant #9. main.c logs the banner. */
}

bool NovaOtaBroker_IsInInstallMode(void)
{
    /* Lock-free volatile read; the broker task updates this on state
     * transitions. PROBING is bounded (one envelope round-trip) and
     * intentionally doesn't promote the flag — only INSTALLING and
     * the sticky ERROR tail suspend the broadcasters. */
    return s_install_active;
}

void NovaOtaBroker_OnEnvelope(uint32_t tag,
                              const uint8_t *body,
                              size_t len,
                              uint32_t envelope_seq)
{
    /* Runs on bridge_uart_task. MUST be lightweight: copy + signal.
     * No leaf I/O, no logging on the hot path, no broker_lock during
     * a slow operation. */
    if (body == NULL && len != 0u) return;

    if (tag == 132u) {
        /* Chunk path — copy into ring. Reject if it wouldn't fit; the
         * bridge sees one FwInstallProgress(FAILED, error=104) instead
         * of a silent drop, and the broker state machine aborts cleanly
         * on the next drain pass. */
        if (len > BROKER_CHUNK_MAX_BYTES) {
            /* Body bigger than any slot — protocol bug or chunk_size
             * negotiation drift. Fail clean. */
            broker_lock();
            s_install_active = true;  /* gate broadcasts until we surface */
            broker_unlock();
            /* Defer the FAILED emission to the broker task by stuffing
             * a synthetic abort into the command queue would be more
             * symmetrical, but the same emit path is mutex-serialized
             * already; emit directly here. Heartbeat keeps flowing on
             * bridge_uart_task because emit_install_progress only takes
             * the TX mutex (microseconds). */
            emit_install_progress(0, "", "",
                                  FW_INSTALL_FAILED, 0u, 0u,
                                  BROKER_ERR_CHUNK_RING_FULL,
                                  "chunk body exceeds broker slot size",
                                  FW_INSTALL_OVERALL_FAILED);
            return;
        }

        broker_lock();
        /* Quick gate — if no install is in progress, swallow. Pi5
         * shouldn't send chunks outside an install but the early-drop
         * here keeps the ring from filling with orphans. */
        if (s_state != BROKER_INSTALLING) {
            broker_unlock();
            return;
        }
        if (s_chunk_ring_count >= BROKER_CHUNK_RING_DEPTH) {
            /* Ring full. Mark failed; the broker drain pass will emit
             * the FAILED progress. We DON'T queue a chunk that would
             * overflow. */
            uint32_t idx = s_ctx.cur_component_index;
            uint32_t total = s_ctx.cur_total_size;
            uint32_t wrote = s_ctx.cur_bytes_written;
            char name[BROKER_NAME_MAX];
            char host[BROKER_TARGET_HOST_MAX];
            memcpy(name, s_ctx.cur_component_name, sizeof(name));
            memcpy(host, s_ctx.cur_target_host,    sizeof(host));
            s_ctx.cur_failed = true;
            broker_set_state_locked(BROKER_ERROR);
            broker_unlock();
            emit_install_progress(idx, name, host,
                                  FW_INSTALL_FAILED, wrote, total,
                                  BROKER_ERR_CHUNK_RING_FULL,
                                  "broker chunk ring full",
                                  FW_INSTALL_OVERALL_FAILED);
            /* Wake the broker so it observes the new state quickly. */
            (void)xSemaphoreGive(s_wake_sem);
            return;
        }
        BrokerChunkSlot *slot = &s_chunk_ring[s_chunk_ring_head];
        slot->tag          = tag;
        slot->envelope_seq = envelope_seq;
        slot->body_len     = (uint32_t)len;
        if (len > 0u) memcpy(slot->body, body, len);
        s_chunk_ring_head = (s_chunk_ring_head + 1u) % BROKER_CHUNK_RING_DEPTH;
        s_chunk_ring_count++;
        s_ctx.last_envelope_seq = envelope_seq;
        broker_unlock();
        (void)xSemaphoreGive(s_wake_sem);
        return;
    }

    /* Non-chunk path — small command queue. */
    if (tag != 130u && tag != 131u && tag != 133u &&
        tag != 134u && tag != 135u && tag != 136u) {
        /* Not a broker tag; the caller in main.c gates 130..136, so
         * this is a safety net only. */
        return;
    }
    if (len > BROKER_CMD_MAX_BYTES) {
        /* Body too big for the command slot. Fail clean if we're mid-
         * install; otherwise silently drop. */
        broker_lock();
        bool installing = (s_state == BROKER_INSTALLING);
        broker_unlock();
        if (installing) {
            emit_install_progress(0, "", "",
                                  FW_INSTALL_FAILED, 0u, 0u,
                                  BROKER_ERR_CMD_QUEUE_FULL,
                                  "command body exceeds broker slot size",
                                  FW_INSTALL_OVERALL_FAILED);
        }
        return;
    }

    broker_lock();
    if (s_cmd_queue_count >= BROKER_CMD_QUEUE_DEPTH) {
        /* Queue full. With depth=4 and these envelopes being rare
         * (Begin / ComponentBegin / etc., one each per component +
         * one Begin/Complete per bundle), this is essentially
         * impossible in normal operation. If it does fire, the install
         * is already wedged — emit FAILED. */
        broker_unlock();
        emit_install_progress(0, "", "",
                              FW_INSTALL_FAILED, 0u, 0u,
                              BROKER_ERR_CMD_QUEUE_FULL,
                              "broker command queue full",
                              FW_INSTALL_OVERALL_FAILED);
        return;
    }
    BrokerCmdSlot *cslot = &s_cmd_queue[s_cmd_queue_head];
    cslot->tag          = tag;
    cslot->envelope_seq = envelope_seq;
    cslot->body_len     = (uint32_t)len;
    if (len > 0u) memcpy(cslot->body, body, len);
    s_cmd_queue_head = (s_cmd_queue_head + 1u) % BROKER_CMD_QUEUE_DEPTH;
    s_cmd_queue_count++;
    s_ctx.last_envelope_seq = envelope_seq;
    broker_unlock();
    (void)xSemaphoreGive(s_wake_sem);
}

void NovaOtaBroker_Tick(uint32_t now_ms)
{
    /* Phase 3.7: the broker task now ticks itself on a 100 ms
     * xSemaphoreTake timeout. Retained as a no-op for any caller that
     * hasn't been updated; safe to remove from main.c entirely. */
    (void)now_ms;
}
