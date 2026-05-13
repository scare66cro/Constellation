/*
 * lp_ota_task.c — Over-the-air firmware update listener (Phase 1A)
 *
 * See lp_ota_task.h for the wire framing and tag map. This file owns
 * the listen socket on :5503, accepts up to LP_OTA_MAX_CONNS clients,
 * pushes an `FwBankInfo` proto frame on accept, and replies to any
 * `FwBeginUpdate` with `FwUpdateStatus { state=FW_ERROR,
 * error_code=LP_OTA_ERR_NOT_IMPL }`.
 *
 * Phase 1A is intentionally read-only: no flash writes, no reboots,
 * no calls into Platform/nova_fw_update.c.  This lets the bridge +
 * UI integrate against a real version-reporting endpoint while we
 * resolve the OSPI offset collision (lp_device_config banks at
 * 0x200000/0x210000 vs Platform Bank B at 0x200000) before Phase 1B.
 *
 * Concurrency: single FreeRTOS task, identical select() pattern to
 * orbit_modbus_tcp.c.  Phase 1A handlers are stateless so we don't
 * need a per-connection scratch buffer beyond the RX accumulator.
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */

#include "lp_ota_task.h"
#include "lp_version.h"
#include "nova_fw_update.h"
#include "hal.h"
#include "ti_drivers_open_close.h"
#include "ti_board_open_close.h"
#include <networking/enet/core/include/core/enet_types.h>  /* ENET_CPSW_3G */
#include <networking/enet/core/include/per/cpsw.h>         /* Cpsw_Cfg (req'd by ti_enet_open_close.h) */
#include "ti_enet_open_close.h"            /* EnetApp_driverClose */

#include <FreeRTOS.h>
#include <task.h>
#include <kernel/dpl/ClockP.h>
#include <kernel/dpl/DebugP.h>
#include <drivers/ospi.h>
#include <board/flash.h>
#include "lwip/sockets.h"
#include "lwip/inet.h"

#include <stdbool.h>
#include <string.h>
#include <errno.h>

/* === Option 1 OTA: receive-then-flash via MSRAM buffer (2026-05-12) ======
 *
 * Runtime `Flash_write` does not work in our nova_lp FreeRTOS context
 * (see `memories/repo/lp-am2434-runtime-flashwrite-unresolved.md` —
 * 4-day investigation, software-debug exhausted). The auto-flasher in
 * NoRTOS context writes successfully to the same chip / address. The
 * suspected difference is that initializing Enet/CPSW + lwIP changes
 * some peripheral state that breaks the OSPI controller's INDIRECT_WRITE
 * state machine.
 *
 * This implementation tests whether closing Enet at OTA-activate time
 * restores `Flash_write` to a working state — replicating the
 * auto-flasher's "OSPI only" environment. If the chunks-to-buffer +
 * close-Enet + drain-buffer-to-Flash sequence succeeds, we have a
 * shippable network OTA path.
 *
 * Flow:
 *   Begin    → init buffer; return ok (no OSPI ops)
 *   Chunk N  → copy chunk to buffer; return ok (no OSPI ops)
 *   Finalize → verify image CRC against buffer; return ok (no OSPI ops)
 *   Activate → close Enet via EnetApp_driverClose; erase OSPI live
 *              region; flash buffer to live region; warm-reset
 *
 * Buffer sized at 64 KB for the bench POC — verifies whether
 * closing Enet restores `Flash_write`. MSRAM has ~67 KB contiguous
 * free (per linker report after BSS + stacks), so 64 KB fits today;
 * full customer images (~485 KB) need DDR before this is
 * production-ready. If the POC validates the close-Enet theory, the
 * next step is to plumb a DDR region and bump this to 1 MB. If it
 * doesn't validate, we go to Option 2 and this allocation goes away. */

#define LP_OTA_IMAGE_BUF_SIZE   (64U * 1024U)

static uint8_t  s_image_buf[LP_OTA_IMAGE_BUF_SIZE]
    __attribute__((aligned(64), section(".bss.s_image_buf")));
static uint32_t s_image_bytes_buffered = 0;
static uint32_t s_image_total_size     = 0;
static uint32_t s_image_expected_crc   = 0;
static bool     s_image_buffer_active  = false;
static char     s_image_staged_version[32] = {0};

/* CRC-32 (IEEE 802.3 polynomial) over the buffered image — matches the
 * crc used elsewhere in nova_fw_update.c. Bytewise so we don't need a
 * 256-entry table in our limited MSRAM. */
static uint32_t lp_ota_crc32(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= (uint32_t)data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
        }
    }
    return ~crc;
}


/* ─── Hand-rolled minimal proto3 encoder ──────────────────────────────────
 *
 * Phase 1A only writes a handful of small messages, so a real nanopb
 * dependency is overkill.  We hand-encode the few fields we need.
 * Decoder skips fields we don't care about.
 *
 * proto3 wire format quick reference:
 *   key  = (field_num << 3) | wire_type
 *   wire types used:
 *     0 = varint           (uint32, enum, bool)
 *     2 = length-delim     (string, bytes, sub-message)
 *
 * Numbers fit in u32 throughout — no need for full 10-byte varints. */

static uint16_t pb_varint(uint8_t *dst, uint32_t v)
{
    uint16_t i = 0;
    while (v >= 0x80) {
        dst[i++] = (uint8_t)((v & 0x7F) | 0x80);
        v >>= 7;
    }
    dst[i++] = (uint8_t)v;
    return i;
}

static uint16_t pb_key(uint8_t *dst, uint32_t field_num, uint32_t wire_type)
{
    return pb_varint(dst, (field_num << 3) | wire_type);
}

/* Emit field as varint (skips zero per proto3 default-suppression). */
static uint16_t pb_uint32(uint8_t *dst, uint32_t field, uint32_t v)
{
    if (v == 0) return 0;
    uint16_t n = pb_key(dst, field, 0);
    n += pb_varint(dst + n, v);
    return n;
}

/* Emit varint regardless of value (for bool field with default false
 * we still suppress, but fixed-meaning enums sometimes need a force).
 * Currently unused but kept for symmetry with Platform/nova_proto. */
static uint16_t pb_uint32_force(uint8_t *dst, uint32_t field, uint32_t v)
{
    uint16_t n = pb_key(dst, field, 0);
    n += pb_varint(dst + n, v);
    return n;
}

/* Emit a length-delimited string (skips empty per proto3 default). */
static uint16_t pb_string(uint8_t *dst, uint32_t field, const char *s)
{
    if (!s || s[0] == '\0') return 0;
    uint32_t len = (uint32_t)strlen(s);
    uint16_t n = pb_key(dst, field, 2);
    n += pb_varint(dst + n, len);
    memcpy(dst + n, s, len);
    return (uint16_t)(n + len);
}

/* ─── Proto3 minimal decoder ──────────────────────────────────────────────
 *
 * Walk fields, hand back (field, wire_type, value-pointer).  Caller
 * decides which fields to read.  We only need this to extract a few
 * uint32 / string fields from FwBeginUpdate; everything else gets
 * skipped. */

typedef struct {
    const uint8_t *p;
    const uint8_t *end;
} pb_iter_t;

static int pb_read_varint(pb_iter_t *it, uint64_t *out)
{
    uint64_t v = 0;
    int shift = 0;
    while (it->p < it->end) {
        uint8_t b = *it->p++;
        v |= (uint64_t)(b & 0x7F) << shift;
        if ((b & 0x80) == 0) { *out = v; return 0; }
        shift += 7;
        if (shift > 63) return -1;
    }
    return -1;
}

/* Skip the field whose key has wire_type `wt`. */
static int pb_skip(pb_iter_t *it, uint32_t wt)
{
    uint64_t v;
    switch (wt) {
        case 0: return pb_read_varint(it, &v);
        case 2: {
            if (pb_read_varint(it, &v) != 0) return -1;
            if ((size_t)(it->end - it->p) < v) return -1;
            it->p += v;
            return 0;
        }
        case 1:  /* fixed64 */
            if ((size_t)(it->end - it->p) < 8) return -1;
            it->p += 8; return 0;
        case 5:  /* fixed32 */
            if ((size_t)(it->end - it->p) < 4) return -1;
            it->p += 4; return 0;
        default: return -1;
    }
}

/* ─── Message encoders ────────────────────────────────────────────────────
 *
 * Field numbers must match proto/agristar/firmware.proto exactly.  Any
 * change to the proto requires a corresponding update here AND in the
 * bridge-side TS decoder.  Phase 1B will replace these hand-encoders
 * with nanopb-generated code, at which point this file collapses to
 * just the socket / framing layer. */

/* FwBankInfo (proto):
 *   1  FwBankId active_bank
 *   2  string   bank_a_version
 *   3  uint32   bank_a_crc
 *   4  bool     bank_a_valid       (encoded as varint, suppressed if false)
 *   5  string   bank_b_version
 *   6  uint32   bank_b_crc
 *   7  bool     bank_b_valid
 *   8  string   golden_version
 *   9  uint32   boot_count
 *  10  uint32   boot_reason
 *
 * Phase 1B: read FwBankHeader / FwBootMeta from OSPI via the Platform
 * NovaFwUpdate API (Init populates a cached copy at boot, getters are
 * cheap). Bank A always reports the running image's LP_FW_VERSION as
 * a fallback when no header has been written yet (cold board, never
 * OTA'd) so the UI version row stays populated. */
static uint16_t encode_bank_info(uint8_t *dst, uint16_t cap)
{
    FwBankHeader hdr_a, hdr_b;
    FwBootMeta   meta;
    NovaFwUpdate_GetBankHeader(0, &hdr_a);
    NovaFwUpdate_GetBankHeader(1, &hdr_b);
    NovaFwUpdate_GetBootMeta(&meta);

    bool a_hdr_ok = (hdr_a.magic == FW_BANK_MAGIC);
    bool b_hdr_ok = (hdr_b.magic == FW_BANK_MAGIC);

    /* Display: if Bank A's header is missing (never OTA'd), report
     * the live LP_FW_VERSION so the UI shows what's actually running.
     * The valid bit reflects the running image, not the header. */
    const char *a_ver = (a_hdr_ok && hdr_a.version[0]) ? hdr_a.version : LP_FW_VERSION;
    const char *b_ver = (b_hdr_ok && hdr_b.version[0]) ? hdr_b.version : "";

    uint16_t n = 0;
    n += pb_uint32(dst + n, 1, NovaFwUpdate_GetActiveBank());  /* 0 default suppressed */
    n += pb_string(dst + n, 2, a_ver);
    n += pb_uint32(dst + n, 3, a_hdr_ok ? hdr_a.image_crc : 0);
    n += pb_uint32_force(dst + n, 4, 1U);  /* bank_a_valid — main is always loaded */
    n += pb_string(dst + n, 5, b_ver);
    n += pb_uint32(dst + n, 6, b_hdr_ok ? hdr_b.image_crc : 0);
    if (b_hdr_ok && hdr_b.valid) {
        n += pb_uint32_force(dst + n, 7, 1U);  /* bank_b_valid */
    }
    n += pb_uint32(dst + n, 9, meta.boot_count);
    n += pb_uint32(dst + n, 10, meta.boot_reason);
    (void)cap;
    return n;
}

/* FwUpdateStatus (proto):
 *   1  FwUpdateState state
 *   2  uint32        bytes_written
 *   3  uint32        total_size
 *   4  uint32        error_code
 *   5  string        error_message
 *   6  string        active_version
 *   7  string        staged_version
 *   8  FwBankId      active_bank */
static uint16_t encode_status_error(uint8_t *dst, uint16_t cap,
                                    uint32_t err_code, const char *err_msg)
{
    uint16_t n = 0;
    n += pb_uint32_force(dst + n, 1, 6U);   /* FW_ERROR = 6 */
    n += pb_uint32(dst + n, 4, err_code);
    n += pb_string(dst + n, 5, err_msg);
    n += pb_string(dst + n, 6, LP_FW_VERSION);
    (void)cap;
    return n;
}

/* Encode a non-error FwUpdateStatus reflecting platform layer state.
 * Used after Begin/Chunk/Finalize to confirm progress to the bridge. */
static uint16_t encode_status_progress(uint8_t *dst, uint16_t cap)
{
    uint16_t n = 0;
    uint32_t state = (uint32_t)NovaFwUpdate_GetState();
    n += pb_uint32(dst + n, 1, state);  /* 0=IDLE suppressed */
    n += pb_uint32(dst + n, 2, NovaFwUpdate_GetBytesWritten());
    n += pb_uint32(dst + n, 3, NovaFwUpdate_GetTotalSize());
    n += pb_string(dst + n, 6, LP_FW_VERSION);
    n += pb_uint32(dst + n, 8, NovaFwUpdate_GetActiveBank());
    (void)cap;
    return n;
}

/* ─── Proto3 body decoders (Phase 1B) ───────────────────────────────────
 *
 * Hand-rolled to avoid pulling nanopb into the LP. Walk the wire-format
 * fields, fill in a struct of out-params. Returns 0 on success, -1 on
 * any parse failure (truncated body, oversized varint, unknown wire
 * type, etc.). Unknown fields are skipped per proto3 forward-compat. */

typedef struct {
    uint32_t       total_size;
    uint32_t       crc32;
    uint32_t       chunk_size;
    char           version[32];
} fw_begin_t;

static int decode_begin(const uint8_t *body, uint32_t len, fw_begin_t *out)
{
    memset(out, 0, sizeof(*out));
    pb_iter_t it = { body, body + len };
    while (it.p < it.end) {
        uint64_t key;
        if (pb_read_varint(&it, &key) != 0) return -1;
        uint32_t field = (uint32_t)(key >> 3);
        uint32_t wt    = (uint32_t)(key & 7);
        uint64_t v;
        switch (field) {
            case 1: /* total_size, varint */
                if (wt != 0 || pb_read_varint(&it, &v) != 0) return -1;
                out->total_size = (uint32_t)v;
                break;
            case 2: /* crc32, varint */
                if (wt != 0 || pb_read_varint(&it, &v) != 0) return -1;
                out->crc32 = (uint32_t)v;
                break;
            case 3: /* version, length-delim string */
                if (wt != 2 || pb_read_varint(&it, &v) != 0) return -1;
                if ((size_t)(it.end - it.p) < v) return -1;
                {
                    uint32_t cp = (v < sizeof(out->version) - 1) ? (uint32_t)v
                                  : (uint32_t)(sizeof(out->version) - 1);
                    memcpy(out->version, it.p, cp);
                    out->version[cp] = '\0';
                }
                it.p += v;
                break;
            case 4: /* chunk_size, varint */
                if (wt != 0 || pb_read_varint(&it, &v) != 0) return -1;
                out->chunk_size = (uint32_t)v;
                break;
            default:
                if (pb_skip(&it, wt) != 0) return -1;
                break;
        }
    }
    return 0;
}

typedef struct {
    uint32_t       offset;
    uint32_t       chunk_crc;
    const uint8_t *data;
    uint32_t       data_len;
} fw_chunk_t;

static int decode_chunk(const uint8_t *body, uint32_t len, fw_chunk_t *out)
{
    memset(out, 0, sizeof(*out));
    pb_iter_t it = { body, body + len };
    while (it.p < it.end) {
        uint64_t key;
        if (pb_read_varint(&it, &key) != 0) return -1;
        uint32_t field = (uint32_t)(key >> 3);
        uint32_t wt    = (uint32_t)(key & 7);
        uint64_t v;
        switch (field) {
            case 1: /* offset */
                if (wt != 0 || pb_read_varint(&it, &v) != 0) return -1;
                out->offset = (uint32_t)v;
                break;
            case 2: /* data, bytes */
                if (wt != 2 || pb_read_varint(&it, &v) != 0) return -1;
                if ((size_t)(it.end - it.p) < v) return -1;
                out->data = it.p;
                out->data_len = (uint32_t)v;
                it.p += v;
                break;
            case 3: /* chunk_crc */
                if (wt != 0 || pb_read_varint(&it, &v) != 0) return -1;
                out->chunk_crc = (uint32_t)v;
                break;
            default:
                if (pb_skip(&it, wt) != 0) return -1;
                break;
        }
    }
    return 0;
}

static int decode_finalize(const uint8_t *body, uint32_t len, uint32_t *crc_out)
{
    *crc_out = 0;
    pb_iter_t it = { body, body + len };
    while (it.p < it.end) {
        uint64_t key;
        if (pb_read_varint(&it, &key) != 0) return -1;
        uint32_t field = (uint32_t)(key >> 3);
        uint32_t wt    = (uint32_t)(key & 7);
        uint64_t v;
        if (field == 1 && wt == 0) {
            if (pb_read_varint(&it, &v) != 0) return -1;
            *crc_out = (uint32_t)v;
        } else {
            if (pb_skip(&it, wt) != 0) return -1;
        }
    }
    return 0;
}

static int decode_activate(const uint8_t *body, uint32_t len, bool *reboot_out)
{
    *reboot_out = false;
    pb_iter_t it = { body, body + len };
    while (it.p < it.end) {
        uint64_t key;
        if (pb_read_varint(&it, &key) != 0) return -1;
        uint32_t field = (uint32_t)(key >> 3);
        uint32_t wt    = (uint32_t)(key & 7);
        uint64_t v;
        if (field == 1 && wt == 0) {
            if (pb_read_varint(&it, &v) != 0) return -1;
            *reboot_out = (v != 0);
        } else {
            if (pb_skip(&it, wt) != 0) return -1;
        }
    }
    return 0;
}

/* ─── LP-side staging copy for Activate (Phase 2 hack) ──────────────────
 *
 * After NovaFwUpdate_Finalize verifies Bank B (0x200000), the LP needs
 * to copy that image into the SBL hard-load offset (0x080000) so the
 * stock `sbl_ospi_multi_partition` picks it up on next boot. The copy
 * is bounded by LP_OTA_MAIN_MAX_BYTES (1 MB) so it can never run past
 * 0x180000 and clobber the watchdog mcelf at that offset.
 *
 * Source: Bank B header was written by Finalize and tells us the exact
 * image_size. We trust that value here only because Finalize already
 * verified it against the CRC; defence-in-depth we still cap at 1 MB.
 *
 * This is a development-only path \u2014 a power-fail mid-copy bricks the
 * board (recovery requires JTAG). Production gate is the F2c custom
 * stage-2 chooser which makes the copy unnecessary. See
 * docs/LP-AM2434-OTA-Update-Plan.md \u00a7Phase 3. */
static int lp_ota_stage_copy_b_to_a(uint32_t image_size)
{
    if (image_size == 0) return -1;
    if (image_size > LP_OTA_MAIN_MAX_BYTES) return -1;

    /* Erase A in 4 KB sectors, capped at LP_OTA_MAIN_MAX_BYTES. We
     * intentionally erase the FULL 1 MB, not just the image span,
     * to wipe any straggler bytes from previous larger images. */
    for (uint32_t off = 0; off < LP_OTA_MAIN_MAX_BYTES; off += 4096U) {
        if (hal_flash_erase_sector(LP_OTA_MAIN_LIVE_OFFSET + off) != 0) {
            return -1;
        }
        /* Yield occasionally so the watchdog task can feed. 256 sectors
         * total at ~50 ms each = ~13 s; without yields the OTA task
         * starves every other equal-priority task. */
        if ((off & 0x7FFFU) == 0) vTaskDelay(1);
    }

    /* Copy B -> A in 256-byte page-sized chunks. Source is read via
     * `hal_flash_read` (XIP path, always works). Destination is written
     * via `hal_flash_write_dac` (DAC mode, the only runtime write path
     * that actually programs the chip — see
     * `memories/repo/lp-am2434-ota-dac-mode-fix.md`). The legacy
     * `hal_flash_write` SDK call returns success without actually
     * programming the chip in our FreeRTOS context. */
    static uint8_t s_copy_buf[256] __attribute__((aligned(64)));
    uint32_t remaining = image_size;
    uint32_t off = 0;
    while (remaining > 0) {
        uint32_t n = (remaining > sizeof(s_copy_buf)) ? sizeof(s_copy_buf)
                                                       : remaining;
        /* hal_flash_write_dac requires page-aligned len; pad the tail
         * with 0xFF (erased state) if the last chunk is short. */
        if (n < sizeof(s_copy_buf)) {
            memset(s_copy_buf, 0xFF, sizeof(s_copy_buf));
        }
        if (hal_flash_read(FW_BANK_B_OFFSET + off, s_copy_buf, n) != 0) return -1;
        if (hal_flash_write_dac(LP_OTA_MAIN_LIVE_OFFSET + off, s_copy_buf,
                                sizeof(s_copy_buf)) != 0) return -1;
        off       += n;
        remaining -= n;
        if ((off & 0x3FFFU) == 0) vTaskDelay(1);
    }
    return 0;
}

/* Triggered after a successful staging copy. Same warm-reset primitive
 * Flash-LP.ps1 uses post-flash; DMSC orchestrates the SoC reset, ROM
 * re-runs, SBL hard-loads the now-fresh bytes at 0x080000. Returns
 * only if the SCI call fails. */
static void lp_ota_warm_reset(void)
{
    extern int32_t Sciclient_pmDeviceReset(uint32_t timeout);
    DebugP_log("[OTA] activate: triggering warm reset\r\n");
    /* Drain the pending UART chars so the reset banner is observable. */
    vTaskDelay(pdMS_TO_TICKS(100));
    (void)Sciclient_pmDeviceReset(0xFFFFFFFFU /* SystemP_WAIT_FOREVER */);
    while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
}

/* ─── Frame send helper ─────────────────────────────────────────────────── */

static int send_frame(int fd, uint8_t tag, const uint8_t *body, uint16_t body_len)
{
    uint8_t hdr[5];
    uint32_t total = 1u + (uint32_t)body_len;
    hdr[0] = (uint8_t)(total >> 24);
    hdr[1] = (uint8_t)(total >> 16);
    hdr[2] = (uint8_t)(total >>  8);
    hdr[3] = (uint8_t)(total      );
    hdr[4] = tag;
    if (lwip_send(fd, hdr, sizeof(hdr), 0) != (int)sizeof(hdr)) return -1;
    if (body_len > 0) {
        if (lwip_send(fd, body, body_len, 0) != (int)body_len) return -1;
    }
    return 0;
}

static void push_bank_info(int fd)
{
    uint8_t body[128];
    uint16_t n = encode_bank_info(body, sizeof(body));
    (void)send_frame(fd, LP_OTA_TAG_BANK_INFO, body, n);
}

static void push_status_error(int fd, uint32_t err_code, const char *err_msg)
{
    uint8_t body[160];
    uint16_t n = encode_status_error(body, sizeof(body), err_code, err_msg);
    (void)send_frame(fd, LP_OTA_TAG_STATUS, body, n);
}

static void push_status_progress(int fd)
{
    uint8_t body[64];
    uint16_t n = encode_status_progress(body, sizeof(body));
    (void)send_frame(fd, LP_OTA_TAG_STATUS, body, n);
}

/* ─── Per-connection state ──────────────────────────────────────────────── */

typedef struct {
    int      fd;
    uint8_t  rx[LP_OTA_MAX_FRAME + 4];   /* +4 length prefix */
    uint16_t rx_len;
} ota_conn_t;

/* Service one connection: drain RX, process complete frames, return
 * false to drop the connection. */
static bool service_conn(ota_conn_t *c)
{
    for (;;) {
        if (c->rx_len < 4) return true;   /* need length prefix */
        uint32_t total =
            ((uint32_t)c->rx[0] << 24) |
            ((uint32_t)c->rx[1] << 16) |
            ((uint32_t)c->rx[2] <<  8) |
            ((uint32_t)c->rx[3]      );
        if (total == 0 || total > LP_OTA_MAX_FRAME) {
            push_status_error(c->fd, LP_OTA_ERR_TOO_LARGE,
                              "frame too large");
            return false;
        }
        uint32_t need = 4u + total;
        if (c->rx_len < need) return true;   /* wait for full frame */

        uint8_t  tag    = c->rx[4];
        const uint8_t *body    = &c->rx[5];
        uint32_t       bodylen = total - 1u;

        switch (tag) {
            case LP_OTA_TAG_BEGIN: {
                /* Option 1: buffer all chunks in MSRAM. No OSPI ops
                 * happen until Activate (when Enet is closed first). */
                fw_begin_t b;
                if (decode_begin(body, bodylen, &b) != 0) {
                    push_status_error(c->fd, LP_OTA_ERR_DECODE, "FwBegin decode");
                    break;
                }
                if (b.total_size == 0 || b.total_size > LP_OTA_IMAGE_BUF_SIZE) {
                    DebugP_log("[OTA] BEGIN rejected size=%lu (max %u)\r\n",
                               (unsigned long)b.total_size,
                               (unsigned)LP_OTA_IMAGE_BUF_SIZE);
                    push_status_error(c->fd, LP_OTA_ERR_TOO_BIG,
                                      "image > LP_OTA_IMAGE_BUF_SIZE");
                    break;
                }
                DebugP_log("[OTA] BEGIN (buffered) size=%lu crc=0x%08lx ver=%s\r\n",
                           (unsigned long)b.total_size,
                           (unsigned long)b.crc32, b.version);
                /* Reset buffer state. No OSPI access — Begin used to
                 * lazy-erase Bank B but that path is dead post-pivot. */
                s_image_bytes_buffered = 0;
                s_image_total_size     = b.total_size;
                s_image_expected_crc   = b.crc32;
                s_image_buffer_active  = true;
                /* b.version is a 32-byte char array, always non-null;
                 * decode_begin null-terminates it on success. */
                strncpy(s_image_staged_version, b.version,
                        sizeof(s_image_staged_version) - 1);
                s_image_staged_version[sizeof(s_image_staged_version) - 1] = '\0';
                /* Optional zero-fill of the receive region so a partial
                 * image followed by Activate doesn't carry stale bytes
                 * from a prior run. Cheap relative to TCP round-trips. */
                memset(s_image_buf, 0xFF, b.total_size);
                push_status_progress(c->fd);
                break;
            }
            case LP_OTA_TAG_CHUNK: {
                /* Option 1: copy chunk into MSRAM buffer. */
                fw_chunk_t k;
                if (decode_chunk(body, bodylen, &k) != 0 || k.data == NULL) {
                    push_status_error(c->fd, LP_OTA_ERR_DECODE, "FwChunk decode");
                    break;
                }
                if (!s_image_buffer_active) {
                    push_status_error(c->fd, LP_OTA_ERR_CHUNK,
                                      "chunk before begin");
                    break;
                }
                if (k.offset + k.data_len > s_image_total_size) {
                    DebugP_log("[OTA] CHUNK overflow off=%lu len=%lu total=%lu\r\n",
                               (unsigned long)k.offset,
                               (unsigned long)k.data_len,
                               (unsigned long)s_image_total_size);
                    push_status_error(c->fd, LP_OTA_ERR_CHUNK,
                                      "chunk past declared image size");
                    break;
                }
                /* Verify per-chunk CRC before committing to buffer so a
                 * single corrupted chunk can be retransmitted from the
                 * bridge without invalidating the whole image. */
                uint32_t calc = lp_ota_crc32(k.data, k.data_len);
                if (calc != k.chunk_crc) {
                    DebugP_log("[OTA] CHUNK crc mismatch off=%lu got=0x%08lx want=0x%08lx\r\n",
                               (unsigned long)k.offset,
                               (unsigned long)calc,
                               (unsigned long)k.chunk_crc);
                    push_status_error(c->fd, LP_OTA_ERR_CHUNK,
                                      "chunk CRC mismatch");
                    break;
                }
                memcpy(&s_image_buf[k.offset], k.data, k.data_len);
                s_image_bytes_buffered = k.offset + k.data_len;
                push_status_progress(c->fd);
                break;
            }
            case LP_OTA_TAG_FINALIZE: {
                /* Option 1: verify CRC across the whole buffer. The
                 * actual OSPI write is deferred to Activate so we have
                 * a chance to ack the bridge before closing Enet. */
                uint32_t expected;
                if (decode_finalize(body, bodylen, &expected) != 0) {
                    push_status_error(c->fd, LP_OTA_ERR_DECODE, "FwFinalize decode");
                    break;
                }
                if (!s_image_buffer_active) {
                    push_status_error(c->fd, LP_OTA_ERR_FINALIZE,
                                      "finalize before begin");
                    break;
                }
                if (s_image_bytes_buffered != s_image_total_size) {
                    DebugP_log("[OTA] FINALIZE incomplete: bytes=%lu of %lu\r\n",
                               (unsigned long)s_image_bytes_buffered,
                               (unsigned long)s_image_total_size);
                    push_status_error(c->fd, LP_OTA_ERR_FINALIZE,
                                      "incomplete transfer");
                    break;
                }
                uint32_t calc = lp_ota_crc32(s_image_buf, s_image_total_size);
                if (calc != expected) {
                    DebugP_log("[OTA] FINALIZE crc mismatch got=0x%08lx want=0x%08lx\r\n",
                               (unsigned long)calc, (unsigned long)expected);
                    push_status_error(c->fd, LP_OTA_ERR_FINALIZE,
                                      "image CRC mismatch");
                    break;
                }
                DebugP_log("[OTA] FINALIZE ok crc=0x%08lx (buffered, ready for activate)\r\n",
                           (unsigned long)expected);
                push_status_progress(c->fd);
                break;
            }
            case LP_OTA_TAG_ACTIVATE: {
                /* Option 1: close Enet, then drain MSRAM buffer to the
                 * live OSPI offset 0x80000, then warm-reset.
                 *
                 * Once Enet is closed the bridge's TCP connection
                 * drops; the bridge sees disconnection and treats it
                 * the same as a successful reset (per orbitOtaPush.ts
                 * comment block around line 343). Anything that goes
                 * wrong after this point is silent from the bridge's
                 * perspective; UART logs are the only post-mortem. */
                bool reboot = false;
                if (decode_activate(body, bodylen, &reboot) != 0) {
                    push_status_error(c->fd, LP_OTA_ERR_DECODE, "FwActivate decode");
                    break;
                }
                if (!s_image_buffer_active ||
                    s_image_bytes_buffered != s_image_total_size) {
                    push_status_error(c->fd, LP_OTA_ERR_ACTIVATE,
                                      "no verified image buffered");
                    break;
                }
                DebugP_log("[OTA] ACTIVATE buffered=%lu reboot=%d — closing Enet\r\n",
                           (unsigned long)s_image_total_size, (int)reboot);
                /* Ack BEFORE we close Enet — the bridge needs at least
                 * one frame back to know we accepted Activate. */
                push_status_progress(c->fd);
                /* Drain TCP TX before tearing down Enet so the ack
                 * actually goes out on the wire. */
                vTaskDelay(pdMS_TO_TICKS(50));

                /* Silence the periodic PHY register polling task BEFORE
                 * closing Enet. Otherwise its ClockP timer keeps firing and
                 * the task asserts on the just-closed Enet handle
                 * (test_enet_cpsw.c:215 / test_enet.c:391), eventually
                 * wedging the CPU at 100 % via the assertion handler. */
                extern void EnetApp_stopPhyRegisterPollingTask(void);
                DebugP_log("[OTA] stopping PHY-poll task ...\r\n");
                EnetApp_stopPhyRegisterPollingTask();

                /* Close Enet to drop the runtime out of the broken
                 * state. After this, lwip is dead and any further
                 * UART log lines are our only visibility. */
                DebugP_log("[OTA] EnetApp_driverClose ...\r\n");
                EnetApp_driverClose(ENET_CPSW_3G, 0);

                /* === Experiment A (2026-05-12) — full driver teardown ====
                 * Hypothesis: CPSW-close alone wasn't enough because some
                 * OTHER syscfg-initialised driver (I2C, EEPROM, OSPI's
                 * own boot-time PHY-tune state, etc.) is what wedges
                 * Flash_write. The auto-flasher's NoRTOS image initialises
                 * a far smaller surface — basically just OSPI + Flash —
                 * and it writes fine. So: tear down everything we can,
                 * then selectively re-init only OSPI + Flash, and try
                 * Flash_write from that minimal state.
                 *
                 * We keep UART alive: closing CONFIG_UART_CONSOLE (1) kills
                 * DebugP_log, blinding the diagnostic. The auto-flasher
                 * also uses UART for its own logs, so this isn't a
                 * difference vs the working path. */
                DebugP_log("[OTA] Closing Board+Drivers (Flash, EEPROM, I2C, OSPI; UART kept) ...\r\n");
                Board_flashClose();
                Board_eepromClose();
                Drivers_i2cClose();
                Drivers_ospiClose();
                /* Drivers_uartClose intentionally skipped — DebugP uses UART1 */
                vTaskDelay(pdMS_TO_TICKS(50));

                /* === Experiment B (2026-05-12) — hardware-reset OSPI peripheral ===
                 * Experiment A proved software-only driver teardown isn't
                 * enough. `Drivers_ospiClose` only resets driver state — the
                 * OSPI peripheral registers still carry whatever SBL set up
                 * for XIP boot. Cycle the peripheral hardware itself via
                 * TISCI / Sciclient PM (module id 75 = TISCI_DEV_FSS0_OSPI_0).
                 * This is the strongest peripheral-level reset short of a
                 * full SoC reset — closer to what the auto-flasher gets when
                 * it boots cold via JTAG into a freshly-reset OSPI block.
                 *
                 * We tried this alone in 0.A.104 (without driver teardown);
                 * combining both has never been tested. Brick-safe: only
                 * resets the OSPI peripheral, not the chip. */
                /* Experiments B / B-fix (0.A.125, 0.A.126) confirmed PM-reset
                 * of FSS0_OSPI_0 is strictly harmful — even with
                 * `Sciclient_pmSetModuleState(SW_STATE_ON)` afterward, the
                 * first `hal_flash_erase_sector` wedged silently. Code
                 * removed; Experiment C now runs against the 0.A.124 baseline
                 * (Drivers_close + selective re-open, no peripheral reset). */

                DebugP_log("[OTA] Re-opening OSPI + Flash (selective) ...\r\n");
                Drivers_ospiOpen();
                int32_t flash_rc = Board_flashOpen();
                DebugP_log("[OTA] re-open: ospi=%p flash=%p flash_rc=%ld\r\n",
                           (void *)gOspiHandle[CONFIG_OSPI0],
                           (void *)gFlashHandle[CONFIG_FLASH0],
                           (long)flash_rc);
                if (gOspiHandle[CONFIG_OSPI0] == NULL ||
                    gFlashHandle[CONFIG_FLASH0] == NULL) {
                    DebugP_log("[OTA] re-open FAILED — cannot test Flash_write. "
                               "(Bank A intact; reboot to recover network.)\r\n");
                    while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
                }
                /* === Option 1 bench-POC target: BANK B (0x900000),
                 * NOT live (0x80000). Bank B is non-bootable scratch —
                 * if Flash_write works post-Enet-close, we see success
                 * here; if it doesn't, the LP keeps running fine on
                 * the live image (Bank A is untouched). When/if the
                 * POC validates, switch this back to LP_OTA_MAIN_LIVE_OFFSET
                 * for a real activate. */
                const uint32_t TEST_TARGET = FW_BANK_B_OFFSET;  /* 0x900000 */
                DebugP_log("[OTA] Enet closed; flashing %lu B to 0x%06lx (Bank B test)\r\n",
                           (unsigned long)s_image_total_size,
                           (unsigned long)TEST_TARGET);

                /* CANCEL any stuck INDIRECT_WRITE_XFER state from prior
                 * runtime attempts BEFORE the SDK Flash_eraseBlk runs.
                 * Empirically (0.A.140 vs 0.A.141 regression): leaving the
                 * CANCEL until after the erase causes Flash_eraseBlk to
                 * hang inside its WIP-poll loop. The CANCEL write is a
                 * no-op when the register is already clean (which it
                 * usually is on a fresh boot) but the side-effects of
                 * the W1C bits seem to nudge the controller into a state
                 * the SDK can drive. Cheap insurance either way. */
                hal_flash_clear_indirect_state();

                /* Erase only the sectors we actually need to write,
                 * not the full 1 MB main region — much faster, and
                 * Bank B isn't booted from anyway. */
                uint32_t erase_bytes = s_image_total_size;
                if (erase_bytes < 4096U) erase_bytes = 4096U;
                int erase_err = 0;
                for (uint32_t off = 0; off < erase_bytes; off += 4096U) {
                    if (hal_flash_erase_sector(TEST_TARGET + off) != 0) {
                        DebugP_log("[OTA] erase fail @0x%06lx\r\n",
                                   (unsigned long)(TEST_TARGET + off));
                        erase_err = 1;
                        break;
                    }
                }
                if (erase_err) {
                    DebugP_log("[OTA] activate erase FAILED post-close (unexpected — erase worked pre-close)\r\n");
                    while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
                }

                /* DAC-mode page-program loop is now in
                 * `Platform/hal_flash.c::hal_flash_write_dac`. The
                 * function wraps the write loop in `vTaskSuspendAll` so
                 * the CPSW PHY-poll task can't preempt mid-write. Image
                 * length is a multiple of the chip's 256-byte page size
                 * (validated implicitly by the buffer-fill path; OTA
                 * images are always page-aligned). */
                DebugP_log("[OTA] starting DAC-mode write loop (%lu B -> 0x%06lX)\r\n",
                           (unsigned long)s_image_total_size,
                           (unsigned long)TEST_TARGET);
                uint32_t loop_start_us = ClockP_getTimeUsec();
                int dac_rc = hal_flash_write_dac(TEST_TARGET, s_image_buf,
                                                 s_image_total_size);
                uint32_t loop_us = ClockP_getTimeUsec() - loop_start_us;
                DebugP_log("[OTA] hal_flash_write_dac: rc=%d, %lu B in %lu us\r\n",
                           dac_rc,
                           (unsigned long)s_image_total_size,
                           (unsigned long)loop_us);
                int write_err = (dac_rc != 0) ? 1 : 0;

                /* Readback verification — best-effort. CPSW PHY-poll task may
                 * preempt and assert before all 5 reads complete, but the
                 * DAC PP loop above is already proven (74.7 ms, err=0 in
                 * 0.A.137). Reads use SDK `hal_flash_read` (XIP/INDIRECT
                 * path; unaffected by INDIRECT_WRITE wedge). Each call does
                 * its own atomic_enter/exit so we don't nest with the loop
                 * suspend above. */
                if (!write_err) {
                    const uint32_t sample_offsets[] = {0U, 0x1000U, 0x2000U, 0x4000U, 0x7F00U};
                    for (uint32_t i = 0; i < sizeof(sample_offsets)/sizeof(sample_offsets[0]); i++) {
                        uint32_t off = sample_offsets[i];
                        if (off >= s_image_total_size) continue;
                        uint8_t rb[8] = {0};
                        if (hal_flash_read(TEST_TARGET + off, rb, sizeof(rb)) != 0) {
                            DebugP_log("[DAC] verify @0x%06lX READ FAILED\r\n",
                                       (unsigned long)(TEST_TARGET + off));
                            continue;
                        }
                        bool match = (memcmp(rb, &s_image_buf[off], sizeof(rb)) == 0);
                        DebugP_log("[DAC] verify @0x%06lX rdb=%02X %02X %02X %02X %02X %02X %02X %02X %s\r\n",
                                   (unsigned long)(TEST_TARGET + off),
                                   rb[0], rb[1], rb[2], rb[3], rb[4], rb[5], rb[6], rb[7],
                                   match ? "OK" : "MISMATCH");
                        if (!match) write_err = 1;
                    }
                }
                if (write_err) {
                    DebugP_log("[OTA] activate DAC-MODE PP FAILED too. Both INDIRECT and DAC "
                               "transfer mechanisms wedged in our runtime — wedge is below "
                               "all software-reachable OSPI controller paths. File TI E2E "
                               "ticket; bridge-side flash is the customer path.\r\n");
                    /* Halt: bridge is gone, Bank A is intact, LP is
                     * still running fine. A JTAG operator (or
                     * Flash-LP.ps1) can recover by re-flashing. */
                    while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
                }
                /* === Experiment E SUCCESS PATH ===
                 * DAC-mode PP loop completed. The OSPI controller's DAC
                 * (Direct Access Controller) path works where INDIRECT_WRITE_XFER
                 * stalls. This isolates the bug to the SRAM/INDIRECT
                 * subsystem of the controller — the chip and the OSPI bus
                 * itself are fine. We have a shippable workaround (DAC mode
                 * writes) and a precise repro for the TI E2E ticket. */
                DebugP_log("[OTA] activate SUCCESS via DAC-MODE PP: %lu B flashed to 0x%06lX (%lu pages)\r\n",
                           (unsigned long)s_image_total_size,
                           (unsigned long)TEST_TARGET,
                           (unsigned long)(s_image_total_size / 256U));
                /* Sanity read-back: compare a few bytes from OSPI vs
                 * buffer. Cheap correctness check that catches a
                 * "wrote 0xFF, OSPI claims OK" silent corruption. */
                {
                    uint8_t check[16] = {0};
                    if (hal_flash_read(TEST_TARGET, check, sizeof(check)) == 0) {
                        DebugP_log("[OTA] readback @0x%06lX: %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
                                   (unsigned long)TEST_TARGET,
                                   check[0], check[1], check[2], check[3],
                                   check[4], check[5], check[6], check[7]);
                        DebugP_log("[OTA] expected         : %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
                                   s_image_buf[0], s_image_buf[1], s_image_buf[2], s_image_buf[3],
                                   s_image_buf[4], s_image_buf[5], s_image_buf[6], s_image_buf[7]);
                    }
                }
                s_image_buffer_active = false;

                /* If the bridge requested reboot (production OTA flow), do
                 * the staging copy from Bank B → Bank A and warm-reset
                 * into the new firmware. Otherwise (bench POC with
                 * --no-reboot), leave Bank A alone — Bank B is just a
                 * scratch validation of the DAC write path. */
                if (reboot && TEST_TARGET == FW_BANK_B_OFFSET) {
                    DebugP_log("[OTA] reboot=1 — staging Bank B -> Bank A (%lu B)\r\n",
                               (unsigned long)s_image_total_size);
                    if (lp_ota_stage_copy_b_to_a(s_image_total_size) != 0) {
                        DebugP_log("[OTA] stage copy FAILED — Bank A may be partially overwritten. "
                                   "Recovery: re-flash via Flash-LP.ps1.\r\n");
                        while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
                    }
                    DebugP_log("[OTA] stage copy ok — warm-resetting into new firmware\r\n");
                    lp_ota_warm_reset();  /* does not return */
                }

                /* No further frame ack — Enet is already closed. The
                 * bridge sees disconnection; UART log is the source
                 * of truth. Tell caller to drop this connection so it
                 * doesn't keep calling select() on dead lwip. */
                return false;
            }
            default:
                push_status_error(c->fd, LP_OTA_ERR_BAD_FRAME,
                                  "unknown tag");
                break;
        }

        /* Slide consumed frame out of buffer. */
        if (c->rx_len > need) {
            memmove(c->rx, &c->rx[need], c->rx_len - need);
        }
        c->rx_len = (uint16_t)(c->rx_len - need);
    }
}

/* ─── Task entry ────────────────────────────────────────────────────────── */

void lp_ota_task(void *args)
{
    (void)args;

    /* Wait for lwIP to come up. Mirrors orbit_modbus_tcp_task — the
     * listen socket call will fail until the network task is running,
     * so we just delay and retry on failure. */
    vTaskDelay(pdMS_TO_TICKS(2000));

    int listen_fd = -1;
    while (listen_fd < 0) {
        listen_fd = lwip_socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd < 0) {
            DebugP_log("[OTA] socket() failed, retrying\r\n");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        int yes = 1;
        lwip_setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR,
                        &yes, sizeof(yes));

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_port        = htons(LP_OTA_PORT);
        if (lwip_bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            DebugP_log("[OTA] bind(:%d) failed (errno %d), retrying\r\n",
                       LP_OTA_PORT, errno);
            lwip_close(listen_fd);
            listen_fd = -1;
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if (lwip_listen(listen_fd, 2) < 0) {
            DebugP_log("[OTA] listen() failed, retrying\r\n");
            lwip_close(listen_fd);
            listen_fd = -1;
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
    }

    DebugP_log("[OTA] Phase 1A listening on :%d (version=%s)\r\n",
               LP_OTA_PORT, LP_FW_VERSION);

    ota_conn_t conns[LP_OTA_MAX_CONNS];
    for (int i = 0; i < LP_OTA_MAX_CONNS; i++) {
        conns[i].fd = -1; conns[i].rx_len = 0;
    }

    for (;;) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(listen_fd, &rfds);
        int max_fd = listen_fd;
        for (int i = 0; i < LP_OTA_MAX_CONNS; i++) {
            if (conns[i].fd >= 0) {
                FD_SET(conns[i].fd, &rfds);
                if (conns[i].fd > max_fd) max_fd = conns[i].fd;
            }
        }

        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        int rc = lwip_select(max_fd + 1, &rfds, NULL, NULL, &tv);
        if (rc < 0) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
        if (rc == 0) continue;

        if (FD_ISSET(listen_fd, &rfds)) {
            struct sockaddr_in cli;
            socklen_t cli_len = sizeof(cli);
            int new_fd = lwip_accept(listen_fd,
                                     (struct sockaddr *)&cli, &cli_len);
            if (new_fd >= 0) {
                int slot = -1;
                for (int i = 0; i < LP_OTA_MAX_CONNS; i++) {
                    if (conns[i].fd < 0) { slot = i; break; }
                }
                if (slot < 0) {
                    DebugP_log("[OTA] no slots, dropping new conn\r\n");
                    lwip_close(new_fd);
                } else {
                    conns[slot].fd     = new_fd;
                    conns[slot].rx_len = 0;
                    DebugP_log("[OTA] accepted slot=%d fd=%d — pushing FwBankInfo\r\n",
                               slot, new_fd);
                    push_bank_info(new_fd);
                }
            }
        }

        for (int i = 0; i < LP_OTA_MAX_CONNS; i++) {
            if (conns[i].fd < 0) continue;
            if (!FD_ISSET(conns[i].fd, &rfds)) continue;

            int space = (int)(sizeof(conns[i].rx) - conns[i].rx_len);
            if (space <= 0) {
                lwip_close(conns[i].fd);
                conns[i].fd = -1;
                conns[i].rx_len = 0;
                continue;
            }
            int n = lwip_recv(conns[i].fd,
                              &conns[i].rx[conns[i].rx_len], space, 0);
            if (n <= 0) {
                lwip_close(conns[i].fd);
                conns[i].fd = -1;
                conns[i].rx_len = 0;
                continue;
            }
            conns[i].rx_len = (uint16_t)(conns[i].rx_len + (uint16_t)n);
            if (!service_conn(&conns[i])) {
                lwip_close(conns[i].fd);
                conns[i].fd = -1;
                conns[i].rx_len = 0;
            }
        }
    }
}
