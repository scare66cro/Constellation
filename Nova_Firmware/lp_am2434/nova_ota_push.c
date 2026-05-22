/*
 * nova_ota_push.c — Nova-side TCP client that drives a remote LP's
 *                   `lp_ota_task` state machine.
 *
 * See nova_ota_push.h for the public contract and the lifecycle diagram.
 *
 * Wire framing (matches lp_ota_task.h exactly):
 *
 *     [u32 BE total_len][u8 tag][proto3 body]
 *
 * Implementation notes:
 *  - One static socket, single-tenant. NovaOtaBroker enforces no
 *    concurrent installs at the envelope layer.
 *  - All proto3 encode/decode is hand-rolled (no nanopb runtime) using
 *    the same byte-budget conventions as the rest of the firmware.
 *  - Force-encoded uint32 fields: FwBeginUpdate.expected_role (5),
 *    FwDataChunk.offset (2), FwFleetMember.current_role (3). See
 *    CLAUDE.md hard invariant #1.
 *  - lwIP BSD-socket style (lwip_socket / lwip_setsockopt /
 *    lwip_connect / lwip_send / lwip_recv / lwip_close) matching
 *    orbit_client.c so error-handling, timeouts, and PHY-fixup races
 *    look identical across the codebase.
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */
#include "nova_ota_push.h"

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "lwip/errno.h"

#include <kernel/dpl/DebugP.h>
#include <kernel/dpl/ClockP.h>

#include "lp_ota_task.h"      /* LP_OTA_PORT, LP_OTA_TAG_*, LP_OTA_ERR_* */

/* ─── Tunables (mirror orbitOtaPush.ts) ─────────────────────────────── */
#define PUSH_CONNECT_TIMEOUT_S     5
#define PUSH_BEGIN_RECV_TIMEOUT_S  120   /* Bank B erase takes ~5 s; pad for slow chips */
#define PUSH_CHUNK_RECV_TIMEOUT_S  3
#define PUSH_FINAL_RECV_TIMEOUT_S  10
#define PUSH_ACTIVATE_RECV_TIMEOUT_S 3
#define PUSH_PROBE_DEFAULT_MS      300U
#define PUSH_BANK_INFO_TIMEOUT_S   3    /* per-host BankInfo recv on Begin */

/* Outbound frame buffer cap — Begin/Finalize/Activate are tiny; chunks
 * are the upper bound. Pi5's chunk payload is up to 1024 bytes; with
 * proto field keys + varint headers + a 5 B framing prefix we need
 * ~1050. Round up for headroom. */
#define PUSH_TX_FRAME_MAX          2048U

/* Inbound frame buffer cap — Status frames are ~64 B; BankInfo is
 * up to ~200 B with all strings. Cap at 512. */
#define PUSH_RX_FRAME_MAX          512U

/* ─── Module state (single-tenant) ──────────────────────────────────── */
static int      s_push_sock = -1;
static uint32_t s_push_target_ip;        /* host order, for diagnostics */

/* ─── proto3 hand-encoders ──────────────────────────────────────────── */

static size_t enc_varint(uint8_t *buf, uint32_t value)
{
    size_t n = 0;
    while (value > 0x7Fu) {
        buf[n++] = (uint8_t)(0x80u | (value & 0x7Fu));
        value >>= 7u;
    }
    buf[n++] = (uint8_t)value;
    return n;
}

static size_t dec_varint(const uint8_t *buf, size_t len, uint32_t *out)
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

static size_t enc_tag(uint8_t *buf, uint32_t field, uint8_t wire)
{
    return enc_varint(buf, (field << 3) | wire);
}

/* uint32 — proto3 default suppression (matches pb_uint32). */
static size_t enc_uint32(uint8_t *buf, uint32_t field, uint32_t v)
{
    if (v == 0u) return 0u;
    size_t n = enc_tag(buf, field, 0u);
    n += enc_varint(buf + n, v);
    return n;
}

/* Force-encoded uint32 — required for 0-meaningful fields per CLAUDE.md #1. */
static size_t enc_uint32_force(uint8_t *buf, uint32_t field, uint32_t v)
{
    size_t n = enc_tag(buf, field, 0u);
    n += enc_varint(buf + n, v);
    return n;
}

static size_t enc_bool(uint8_t *buf, uint32_t field, bool v)
{
    if (!v) return 0u;
    size_t n = enc_tag(buf, field, 0u);
    n += enc_varint(buf + n, 1u);
    return n;
}

static size_t enc_string(uint8_t *buf, uint32_t field, const char *s)
{
    if (s == NULL || s[0] == '\0') return 0u;
    size_t slen = strlen(s);
    size_t n = enc_tag(buf, field, 2u);
    n += enc_varint(buf + n, (uint32_t)slen);
    memcpy(buf + n, s, slen);
    n += slen;
    return n;
}

static size_t enc_bytes(uint8_t *buf, uint32_t field,
                        const uint8_t *data, uint32_t len)
{
    if (len == 0u) return 0u;
    size_t n = enc_tag(buf, field, 2u);
    n += enc_varint(buf + n, len);
    memcpy(buf + n, data, len);
    n += len;
    return n;
}

static size_t skip_field(uint8_t wire, const uint8_t *buf, size_t len)
{
    switch (wire) {
        case 0: { uint32_t d; return dec_varint(buf, len, &d); }
        case 1: return (len >= 8U) ? 8U : 0U;
        case 2: {
            uint32_t sublen;
            size_t n = dec_varint(buf, len, &sublen);
            if (n == 0u || n + sublen > len) return 0u;
            return n + sublen;
        }
        case 5: return (len >= 4U) ? 4U : 0U;
        default: return 0u;
    }
}

/* ─── Wire framing helpers ──────────────────────────────────────────── */

/* Wrap a tagged proto body in the [u32 BE total_len][u8 tag][body] frame
 * and write to dst[]. Returns total bytes written, or 0 on overflow. */
static size_t make_frame(uint8_t *dst, size_t cap,
                         uint8_t tag, const uint8_t *body, size_t body_len)
{
    if (cap < (size_t)(5u + body_len)) return 0u;
    uint32_t total = (uint32_t)(1u + body_len);   /* tag + body */
    dst[0] = (uint8_t)((total >> 24) & 0xFFu);
    dst[1] = (uint8_t)((total >> 16) & 0xFFu);
    dst[2] = (uint8_t)((total >>  8) & 0xFFu);
    dst[3] = (uint8_t)( total        & 0xFFu);
    dst[4] = tag;
    if (body_len > 0u) memcpy(dst + 5, body, body_len);
    return 5u + body_len;
}

/* Send N bytes, blocking until all written. Returns true on success. */
static bool sock_send_all(int sock, const uint8_t *buf, size_t len)
{
    size_t off = 0;
    while (off < len) {
        int n = lwip_send(sock, buf + off, len - off, 0);
        if (n <= 0) {
            return false;
        }
        off += (size_t)n;
    }
    return true;
}

/* Receive exactly `want` bytes into buf. Returns 0 on success, -1 on
 * timeout/error/close. The socket's SO_RCVTIMEO governs per-recv() wait. */
static int sock_recv_all(int sock, uint8_t *buf, size_t want)
{
    size_t got = 0;
    while (got < want) {
        int n = lwip_recv(sock, buf + got, want - got, 0);
        if (n <= 0) {
            return -1;
        }
        got += (size_t)n;
    }
    return 0;
}

/* Drain any pending bytes from the socket (best-effort, non-blocking).
 * Used before sending Begin so we don't have a stale half-frame from a
 * previous auto-push lingering. We set SO_RCVTIMEO=10 ms briefly. */
static void sock_drain_pending(int sock)
{
    struct timeval tv = { .tv_sec = 0, .tv_usec = 10000 };
    lwip_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    uint8_t scratch[64];
    for (;;) {
        int n = lwip_recv(sock, scratch, sizeof(scratch), 0);
        if (n <= 0) break;
    }
}

/* Set recv timeout (seconds) on the active socket. */
static void sock_set_recv_timeout(int sock, int seconds)
{
    struct timeval tv = { .tv_sec = seconds, .tv_usec = 0 };
    lwip_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

/* Receive one framed message: [u32 BE total_len][u8 tag][body].
 *  out_tag      [out] tag byte
 *  out_body     [out] body bytes (caller's buffer, cap PUSH_RX_FRAME_MAX)
 *  out_body_len [out] body length
 * Returns 0 on success, -1 on timeout / close / malformed length. */
static int recv_frame(int sock, uint8_t *out_tag,
                      uint8_t *out_body, size_t *out_body_len)
{
    uint8_t hdr[5];
    if (sock_recv_all(sock, hdr, 5u) != 0) {
        return -1;
    }
    uint32_t total = ((uint32_t)hdr[0] << 24) | ((uint32_t)hdr[1] << 16)
                   | ((uint32_t)hdr[2] <<  8) | ((uint32_t)hdr[3]);
    if (total == 0u || total > LP_OTA_MAX_FRAME) {
        return -1;
    }
    *out_tag = hdr[4];
    uint32_t body_len = total - 1u;   /* tag is included in total_len */
    if (body_len > PUSH_RX_FRAME_MAX) {
        return -1;
    }
    if (body_len > 0u) {
        if (sock_recv_all(sock, out_body, body_len) != 0) {
            return -1;
        }
    }
    *out_body_len = body_len;
    return 0;
}

/* ─── FwUpdateStatus decoder ────────────────────────────────────────── */
typedef struct {
    uint32_t state;          /* 0=Idle 1=Erasing 2=Receiving 3=Verifying 4=Verified 5=Activating 6=Error */
    uint32_t bytes_written;
    uint32_t total_size;
    uint32_t error_code;
    char     error_msg[64];
} LpStatus;

static void decode_status(const uint8_t *body, size_t len, LpStatus *out)
{
    memset(out, 0, sizeof(*out));
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t n = dec_varint(body + pos, len - pos, &tag);
        if (n == 0u) return;
        pos += n;
        uint32_t field = tag >> 3;
        uint8_t  wire  = (uint8_t)(tag & 0x07u);

        if (wire == 0u && (field == 1u || field == 2u || field == 3u || field == 4u || field == 8u)) {
            uint32_t v;
            size_t vn = dec_varint(body + pos, len - pos, &v);
            if (vn == 0u) return;
            pos += vn;
            if      (field == 1u) out->state         = v;
            else if (field == 2u) out->bytes_written = v;
            else if (field == 3u) out->total_size    = v;
            else if (field == 4u) out->error_code    = v;
            /* field 8 active_bank — ignored here */
        } else if (wire == 2u && field == 5u) {
            uint32_t slen;
            size_t ln = dec_varint(body + pos, len - pos, &slen);
            if (ln == 0u || pos + ln + slen > len) return;
            pos += ln;
            size_t cp = (slen < sizeof(out->error_msg) - 1u) ? slen : sizeof(out->error_msg) - 1u;
            memcpy(out->error_msg, body + pos, cp);
            out->error_msg[cp] = '\0';
            pos += slen;
        } else {
            size_t sk = skip_field(wire, body + pos, len - pos);
            if (sk == 0u) return;
            pos += sk;
        }
    }
}

/* ─── FwBankInfo decoder ────────────────────────────────────────────── */
typedef struct {
    uint32_t active_bank;
    char     bank_a_version[NOVA_OTA_PUSH_VERSION_MAX];
    uint32_t bank_a_crc;
    bool     bank_a_valid;
    char     bank_b_version[NOVA_OTA_PUSH_VERSION_MAX];
    bool     bank_b_valid;
    uint32_t boot_count;
    uint32_t current_role;        /* 0 = CONTROLLER (force-emitted by LP) */
    bool     has_current_role;    /* true if LP firmware emitted field 11 */
} LpBankInfo;

static void copy_string_field(char *dst, size_t dst_cap,
                              const uint8_t *src, uint32_t slen)
{
    size_t cp = (slen < dst_cap - 1u) ? slen : dst_cap - 1u;
    memcpy(dst, src, cp);
    dst[cp] = '\0';
}

static void decode_bank_info(const uint8_t *body, size_t len, LpBankInfo *out)
{
    memset(out, 0, sizeof(*out));
    size_t pos = 0;
    while (pos < len) {
        uint32_t tag;
        size_t n = dec_varint(body + pos, len - pos, &tag);
        if (n == 0u) return;
        pos += n;
        uint32_t field = tag >> 3;
        uint8_t  wire  = (uint8_t)(tag & 0x07u);

        if (wire == 0u && (field == 1u || field == 3u || field == 4u
                           || field == 6u || field == 7u || field == 9u
                           || field == 10u || field == 11u)) {
            uint32_t v;
            size_t vn = dec_varint(body + pos, len - pos, &v);
            if (vn == 0u) return;
            pos += vn;
            if      (field == 1u)  out->active_bank      = v;
            else if (field == 3u)  out->bank_a_crc       = v;
            else if (field == 4u)  out->bank_a_valid     = (v != 0u);
            else if (field == 7u)  out->bank_b_valid     = (v != 0u);
            else if (field == 9u)  out->boot_count       = v;
            else if (field == 11u) { out->current_role   = v; out->has_current_role = true; }
            /* field 6 bank_b_crc, field 10 boot_reason — ignored */
        } else if (wire == 2u && (field == 2u || field == 5u || field == 8u)) {
            uint32_t slen;
            size_t ln = dec_varint(body + pos, len - pos, &slen);
            if (ln == 0u || pos + ln + slen > len) return;
            pos += ln;
            if      (field == 2u) copy_string_field(out->bank_a_version, sizeof(out->bank_a_version), body + pos, slen);
            else if (field == 5u) copy_string_field(out->bank_b_version, sizeof(out->bank_b_version), body + pos, slen);
            /* field 8 golden_version — ignored */
            pos += slen;
        } else {
            size_t sk = skip_field(wire, body + pos, len - pos);
            if (sk == 0u) return;
            pos += sk;
        }
    }
}

/* ─── Connection helpers ────────────────────────────────────────────── */

static void ip_to_string(uint32_t ip_host, char *out, size_t cap)
{
    snprintf(out, cap, "%u.%u.%u.%u",
             (unsigned)((ip_host >> 24) & 0xFFu),
             (unsigned)((ip_host >> 16) & 0xFFu),
             (unsigned)((ip_host >>  8) & 0xFFu),
             (unsigned)( ip_host        & 0xFFu));
}

static int open_lp_socket(uint32_t target_ip_host, int recv_timeout_s)
{
    int sock = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        DebugP_log("[OTA-PUSH] socket() failed errno=%d\r\n", errno);
        return -1;
    }

    struct timeval tv = { .tv_sec = recv_timeout_s, .tv_usec = 0 };
    lwip_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    struct timeval tv_send = { .tv_sec = PUSH_CONNECT_TIMEOUT_S, .tv_usec = 0 };
    lwip_setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv_send, sizeof(tv_send));
    int one = 1;
    lwip_setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    char ipstr[NOVA_OTA_PUSH_HOST_MAX];
    ip_to_string(target_ip_host, ipstr, sizeof(ipstr));

    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port   = lwip_htons(LP_OTA_PORT);
    dst.sin_addr.s_addr = ipaddr_addr(ipstr);

    if (lwip_connect(sock, (struct sockaddr *)&dst, sizeof(dst)) < 0) {
        int e = errno;
        DebugP_log("[OTA-PUSH] connect %s:%u failed errno=%d\r\n",
                   ipstr, (unsigned)LP_OTA_PORT, e);
        lwip_close(sock);
        return -1;
    }
    DebugP_log("[OTA-PUSH] connected %s:%u\r\n", ipstr, (unsigned)LP_OTA_PORT);
    return sock;
}

static void close_active_socket(void)
{
    if (s_push_sock >= 0) {
        lwip_close(s_push_sock);
        s_push_sock = -1;
    }
}

/* Wait for one FwUpdateStatus frame on s_push_sock. Skips any
 * intervening BankInfo frame (LP may have queued one before we drained).
 * Returns 0 on success, -1 on timeout/error. */
static int await_status(LpStatus *out_status)
{
    uint8_t body[PUSH_RX_FRAME_MAX];
    for (int attempts = 0; attempts < 4; attempts++) {
        uint8_t tag;
        size_t body_len = 0;
        if (recv_frame(s_push_sock, &tag, body, &body_len) != 0) {
            return -1;
        }
        if (tag == LP_OTA_TAG_STATUS) {
            decode_status(body, body_len, out_status);
            return 0;
        }
        /* BankInfo or unknown — skip and keep waiting. */
        DebugP_log("[OTA-PUSH] await_status: skip tag=0x%02x len=%u\r\n",
                   (unsigned)tag, (unsigned)body_len);
    }
    return -1;
}

/* Map FwUpdateStatus to push return code. Sets *out_lp_error to the
 * LP_OTA_ERR_* code (or 0 when none). */
static int32_t status_to_rc(const LpStatus *s, uint32_t *out_lp_error,
                            const char *where)
{
    if (out_lp_error) *out_lp_error = s->error_code;
    if (s->state == 6u /* FW_ERROR */ || s->error_code != 0u) {
        DebugP_log("[OTA-PUSH] %s: LP error code=%u msg=\"%s\"\r\n",
                   where, (unsigned)s->error_code, s->error_msg);
        return NOVA_OTA_PUSH_ERR_LP_ERROR;
    }
    return NOVA_OTA_PUSH_OK;
}

/* ─── Public API: BeginToLp ─────────────────────────────────────────── */

int32_t NovaOtaPush_BeginToLp(uint32_t target_ip,
                              uint32_t total_size,
                              uint32_t image_crc,
                              const char *version,
                              uint32_t chunk_size,
                              uint32_t expected_role,
                              bool allow_downgrade,
                              uint32_t *out_lp_error)
{
    if (out_lp_error) *out_lp_error = 0u;

    /* Close any stale socket first — defensive. */
    close_active_socket();
    s_push_target_ip = target_ip;

    /* Open with the long Begin-recv timeout (Bank B erase ~5 s). */
    int sock = open_lp_socket(target_ip, PUSH_BEGIN_RECV_TIMEOUT_S);
    if (sock < 0) {
        return NOVA_OTA_PUSH_ERR_TRANSPORT;
    }
    s_push_sock = sock;

    /* LP auto-pushes FwBankInfo on connect. Drain it (best-effort) so
     * the post-Begin Status read isn't preceded by a stale BankInfo we
     * have to skip. We log the bank info as it arrives. */
    {
        sock_set_recv_timeout(sock, PUSH_BANK_INFO_TIMEOUT_S);
        uint8_t body[PUSH_RX_FRAME_MAX];
        uint8_t tag = 0;
        size_t body_len = 0;
        if (recv_frame(sock, &tag, body, &body_len) == 0
                && tag == LP_OTA_TAG_BANK_INFO) {
            LpBankInfo bi;
            decode_bank_info(body, body_len, &bi);
            DebugP_log("[OTA-PUSH] BankInfo active=%u roleEmitted=%d "
                       "role=%u A.ver=\"%s\" B.ver=\"%s\" boot=%u\r\n",
                       (unsigned)bi.active_bank,
                       (int)bi.has_current_role,
                       (unsigned)bi.current_role,
                       bi.bank_a_version, bi.bank_b_version,
                       (unsigned)bi.boot_count);
            /* Bridge-side role-mismatch fast-fail: if LP told us its role
             * and it doesn't match what we're about to push, abort before
             * Begin to save a Bank B erase cycle. (LP firmware also
             * gates this server-side as LP_OTA_ERR_ROLE_MISMATCH = 27,
             * so this is belt-and-suspenders for the round-trip cost.) */
            if (bi.has_current_role && bi.current_role != expected_role) {
                DebugP_log("[OTA-PUSH] role mismatch: LP=%u, bundle=%u — aborting before Begin\r\n",
                           (unsigned)bi.current_role, (unsigned)expected_role);
                if (out_lp_error) *out_lp_error = 27u;   /* LP_OTA_ERR_ROLE_MISMATCH */
                close_active_socket();
                return NOVA_OTA_PUSH_ERR_LP_ERROR;
            }
        }
        /* Fall through whether we got BankInfo or not — soft gate. */
    }

    /* Build FwBeginUpdate body. Field map (mirror orbitOtaPush.ts +
     * proto/agristar/firmware.proto):
     *   1 total_size, 2 crc32, 3 version, 4 chunk_size,
     *   5 expected_role (force), 6 allow_downgrade (default-omit). */
    uint8_t body[256];
    size_t  bpos = 0;
    bpos += enc_uint32      (body + bpos, 1u, total_size);
    bpos += enc_uint32      (body + bpos, 2u, image_crc);
    bpos += enc_string      (body + bpos, 3u, version ? version : "");
    bpos += enc_uint32      (body + bpos, 4u, chunk_size);
    bpos += enc_uint32_force(body + bpos, 5u, expected_role);
    bpos += enc_bool        (body + bpos, 6u, allow_downgrade);

    uint8_t frame[PUSH_TX_FRAME_MAX];
    size_t  flen = make_frame(frame, sizeof(frame),
                              LP_OTA_TAG_BEGIN, body, bpos);
    if (flen == 0u) {
        DebugP_log("[OTA-PUSH] Begin: frame overflow\r\n");
        close_active_socket();
        return NOVA_OTA_PUSH_ERR_TRANSPORT;
    }
    if (!sock_send_all(sock, frame, flen)) {
        DebugP_log("[OTA-PUSH] Begin: send failed errno=%d\r\n", errno);
        close_active_socket();
        return NOVA_OTA_PUSH_ERR_TRANSPORT;
    }

    /* Wait for the LP's first Status — could take ~5 s on Bank B erase. */
    LpStatus st;
    if (await_status(&st) != 0) {
        DebugP_log("[OTA-PUSH] Begin: no Status (timeout/disconnect)\r\n");
        close_active_socket();
        return NOVA_OTA_PUSH_ERR_TIMEOUT;
    }
    int32_t rc = status_to_rc(&st, out_lp_error, "Begin");
    if (rc != NOVA_OTA_PUSH_OK) {
        close_active_socket();
        return rc;
    }

    /* Switch to the shorter per-chunk recv timeout for the rest. */
    sock_set_recv_timeout(sock, PUSH_CHUNK_RECV_TIMEOUT_S);

    DebugP_log("[OTA-PUSH] Begin OK state=%u (total=%u crc=0x%08x role=%u)\r\n",
               (unsigned)st.state, (unsigned)total_size,
               (unsigned)image_crc, (unsigned)expected_role);
    return NOVA_OTA_PUSH_OK;
}

/* ─── Public API: WriteChunkToLp ────────────────────────────────────── */

int32_t NovaOtaPush_WriteChunkToLp(uint32_t offset,
                                   const uint8_t *data,
                                   uint32_t len,
                                   uint32_t chunk_crc,
                                   uint32_t *out_lp_error)
{
    if (out_lp_error) *out_lp_error = 0u;
    if (s_push_sock < 0) {
        return NOVA_OTA_PUSH_ERR_SEQUENCE;
    }
    if (len > 1500u) {        /* sanity — Pi5 caps chunk at ~1024 */
        return NOVA_OTA_PUSH_ERR_SEQUENCE;
    }

    /* Build FwDataChunk body. Field map:
     *   1 offset (force — 0 is the first chunk),
     *   2 data,
     *   3 chunk_crc. */
    uint8_t body[1600];
    size_t  bpos = 0;
    bpos += enc_uint32_force(body + bpos, 1u, offset);
    bpos += enc_bytes       (body + bpos, 2u, data, len);
    bpos += enc_uint32      (body + bpos, 3u, chunk_crc);

    uint8_t frame[PUSH_TX_FRAME_MAX];
    size_t  flen = make_frame(frame, sizeof(frame),
                              LP_OTA_TAG_CHUNK, body, bpos);
    if (flen == 0u) {
        return NOVA_OTA_PUSH_ERR_TRANSPORT;
    }
    if (!sock_send_all(s_push_sock, frame, flen)) {
        DebugP_log("[OTA-PUSH] Chunk@%u: send failed errno=%d\r\n",
                   (unsigned)offset, errno);
        close_active_socket();
        return NOVA_OTA_PUSH_ERR_TRANSPORT;
    }

    LpStatus st;
    if (await_status(&st) != 0) {
        DebugP_log("[OTA-PUSH] Chunk@%u: no Status\r\n", (unsigned)offset);
        close_active_socket();
        return NOVA_OTA_PUSH_ERR_TIMEOUT;
    }
    int32_t rc = status_to_rc(&st, out_lp_error, "Chunk");
    if (rc != NOVA_OTA_PUSH_OK) {
        /* On any LP error (including BANK_B_REDO=26 — caller decides
         * whether to reopen Begin from offset 0) the socket is no
         * longer useful — close so a subsequent BeginToLp gets a fresh
         * connection. */
        close_active_socket();
    }
    return rc;
}

/* ─── Public API: FinalizeAndActivateLp ─────────────────────────────── */

int32_t NovaOtaPush_FinalizeAndActivateLp(uint32_t expected_crc,
                                          uint32_t *out_lp_error)
{
    if (out_lp_error) *out_lp_error = 0u;
    if (s_push_sock < 0) {
        return NOVA_OTA_PUSH_ERR_SEQUENCE;
    }

    /* Build FwFinalizeUpdate body. Field map:
     *   1 crc32 (echo of image CRC for confirmation). */
    uint8_t body[16];
    size_t  bpos = 0;
    bpos += enc_uint32(body + bpos, 1u, expected_crc);

    uint8_t frame[64];
    size_t  flen = make_frame(frame, sizeof(frame),
                              LP_OTA_TAG_FINALIZE, body, bpos);
    if (flen == 0u) {
        close_active_socket();
        return NOVA_OTA_PUSH_ERR_TRANSPORT;
    }

    /* Bump to the longer Finalize recv timeout — Bank-B CRC verify
     * walks the whole 1 MB image and takes a few hundred ms. */
    sock_set_recv_timeout(s_push_sock, PUSH_FINAL_RECV_TIMEOUT_S);

    if (!sock_send_all(s_push_sock, frame, flen)) {
        DebugP_log("[OTA-PUSH] Finalize: send failed errno=%d\r\n", errno);
        close_active_socket();
        return NOVA_OTA_PUSH_ERR_TRANSPORT;
    }
    LpStatus st;
    if (await_status(&st) != 0) {
        DebugP_log("[OTA-PUSH] Finalize: no Status\r\n");
        close_active_socket();
        return NOVA_OTA_PUSH_ERR_TIMEOUT;
    }
    int32_t rc = status_to_rc(&st, out_lp_error, "Finalize");
    if (rc != NOVA_OTA_PUSH_OK) {
        close_active_socket();
        return rc;
    }

    /* Send Activate(reboot=true). The LP will start the stage copy,
     * close the socket, and reset. */
    bpos = 0;
    bpos += enc_bool(body + bpos, 1u, true);
    flen = make_frame(frame, sizeof(frame),
                      LP_OTA_TAG_ACTIVATE, body, bpos);
    if (flen == 0u) {
        close_active_socket();
        return NOVA_OTA_PUSH_ERR_TRANSPORT;
    }
    sock_set_recv_timeout(s_push_sock, PUSH_ACTIVATE_RECV_TIMEOUT_S);
    if (!sock_send_all(s_push_sock, frame, flen)) {
        DebugP_log("[OTA-PUSH] Activate: send failed errno=%d (LP may have already rebooted)\r\n",
                   errno);
        close_active_socket();
        /* Treat send-failure-after-Finalize as success: LP reboot
         * races our send and the orbitOtaPush.ts logic does the same. */
        return NOVA_OTA_PUSH_OK;
    }

    /* Best-effort wait for Activate ack. Timeout or disconnect is
     * EXPECTED — the LP reboots immediately. */
    LpStatus actst;
    if (await_status(&actst) == 0) {
        DebugP_log("[OTA-PUSH] Activate ack state=%u (reboot pending)\r\n",
                   (unsigned)actst.state);
    } else {
        DebugP_log("[OTA-PUSH] Activate: no ack (LP rebooted — OK)\r\n");
    }
    close_active_socket();
    return NOVA_OTA_PUSH_OK;
}

/* ─── Public API: AbortLp ───────────────────────────────────────────── */

void NovaOtaPush_AbortLp(void)
{
    /* No Finalize — LP's Bank B stays uncommitted; next boot is Bank A. */
    if (s_push_sock >= 0) {
        DebugP_log("[OTA-PUSH] Abort — closing socket without Finalize\r\n");
    }
    close_active_socket();
}

/* ─── Public API: ProbeFleet ────────────────────────────────────────── */

/* Forward decl — defined in orbit_client.h. */
extern size_t      OrbitClient_Count(void);
extern const char *OrbitClient_GetIpv4(uint8_t index);

/* Convert "10.47.27.2" → 0x0A2F1B02 (host order). */
static uint32_t ipstr_to_host(const char *ip)
{
    if (ip == NULL) return 0u;
    unsigned a = 0, b = 0, c = 0, d = 0;
    if (sscanf(ip, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) return 0u;
    return ((a & 0xFFu) << 24) | ((b & 0xFFu) << 16)
         | ((c & 0xFFu) <<  8) | ( d & 0xFFu);
}

/* Single-host probe — open TCP, recv BankInfo, fill member, close. */
static void probe_one_host(const char *ipstr, uint32_t timeout_ms,
                           NovaFwFleetMember *out)
{
    memset(out, 0, sizeof(*out));
    size_t hostlen = strlen(ipstr);
    if (hostlen >= sizeof(out->host)) hostlen = sizeof(out->host) - 1u;
    memcpy(out->host, ipstr, hostlen);
    out->host[hostlen] = '\0';

    int sock = lwip_socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        out->reachable = false;
        snprintf(out->error, sizeof(out->error), "socket errno=%d", errno);
        return;
    }
    /* Translate timeout_ms → struct timeval. Bound at 1 s minimum
     * (lwIP doesn't like sub-tick timeouts on slow links). */
    if (timeout_ms < 100u) timeout_ms = 100u;
    struct timeval tv;
    tv.tv_sec  = (long)(timeout_ms / 1000u);
    tv.tv_usec = (long)((timeout_ms % 1000u) * 1000u);
    lwip_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    lwip_setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    int one = 1;
    lwip_setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port   = lwip_htons(LP_OTA_PORT);
    dst.sin_addr.s_addr = ipaddr_addr(ipstr);

    if (lwip_connect(sock, (struct sockaddr *)&dst, sizeof(dst)) < 0) {
        snprintf(out->error, sizeof(out->error), "connect errno=%d", errno);
        out->reachable = false;
        lwip_close(sock);
        return;
    }

    /* Expect FwBankInfo immediately. */
    uint8_t body[PUSH_RX_FRAME_MAX];
    uint8_t tag = 0;
    size_t body_len = 0;
    if (recv_frame(sock, &tag, body, &body_len) != 0
            || tag != LP_OTA_TAG_BANK_INFO) {
        snprintf(out->error, sizeof(out->error),
                 "no BankInfo (tag=0x%02x)", (unsigned)tag);
        out->reachable = false;
        lwip_close(sock);
        return;
    }
    LpBankInfo bi;
    decode_bank_info(body, body_len, &bi);
    out->reachable    = true;
    out->current_role = bi.has_current_role ? bi.current_role : 0u;
    out->active_bank  = bi.active_bank;
    out->bank_a_valid = bi.bank_a_valid;
    out->bank_b_valid = bi.bank_b_valid;
    out->boot_count   = bi.boot_count;
    /* active version: pick A or B by active_bank. */
    const char *src = (bi.active_bank == 1u /* BankB */)
                    ? bi.bank_b_version : bi.bank_a_version;
    strncpy(out->active_version, src, sizeof(out->active_version) - 1u);
    out->active_version[sizeof(out->active_version) - 1u] = '\0';
    lwip_close(sock);
}

size_t NovaOtaPush_ProbeFleet(NovaFwFleetMember *out_members,
                              size_t max_members,
                              uint32_t timeout_ms)
{
    if (out_members == NULL || max_members == 0u) return 0u;
    if (timeout_ms == 0u) timeout_ms = PUSH_PROBE_DEFAULT_MS;

    size_t count = 0;
    size_t orbits = OrbitClient_Count();
    for (size_t i = 0; i < orbits && count < max_members; i++) {
        const char *ip = OrbitClient_GetIpv4((uint8_t)i);
        if (ip == NULL || ip[0] == '\0') continue;
        probe_one_host(ip, timeout_ms, &out_members[count]);
        count++;
    }
    /* Self (controller) is intentionally NOT probed here. The
     * controller-self-update path is Phase 3.6; the broker's fleet
     * snapshot today is the set of routable orbit IPs only. */
    (void)ipstr_to_host;   /* reserved for future caller-specified hosts */
    return count;
}
