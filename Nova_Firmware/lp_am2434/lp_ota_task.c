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
#include "lp_device_config.h"
#include "orbit_server/orbit_role.h"
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
#include <kernel/dpl/HwiP.h>
#include <kernel/dpl/CacheP.h>
#include <drivers/ospi.h>
#include <board/flash.h>
#include "lwip/sockets.h"
#include "lwip/inet.h"

#include <stdbool.h>
#include <string.h>
#include <errno.h>

/* === Streaming OTA path (2026-05-13, 0.A.144+) =========================
 *
 * Each CHUNK is written directly to Bank B flash via `hal_flash_write_dac`
 * as it arrives — no MSRAM image buffer required. Supports arbitrary-size
 * firmware (limited only by the Bank B region size, currently 1 MB).
 *
 * Flow:
 *   Begin    → clear stuck OSPI state; erase Bank B region; reset counters
 *   Chunk N  → verify per-chunk CRC; pad last partial page with 0xFF;
 *              `hal_flash_write_dac` directly into Bank B at chunk offset
 *   Finalize → readback Bank B [0..total_size); recompute CRC32; compare
 *              against expected; push status
 *   Activate → if reboot=1: copy Bank B → Bank A + warm-reset
 *              if reboot=0: bench POC, no action (Bank B holds image)
 *
 * Critical assumptions:
 *  - Chunks arrive in monotonically-increasing offset order (current
 *    bridge implementation in `orbitOtaPush.ts` guarantees this).
 *  - Chunk offsets are 256-byte page-aligned (the chip's PP unit).
 *    Bridge chunk size = 1024 bytes (= 4 pages) which satisfies this.
 *  - Total image size may not itself be page-aligned (real firmware
 *    images aren't); the last chunk handles the partial-page pad.
 *
 * Net memory savings vs the prior 64 KB-buffered design: 64 KB MSRAM
 * freed (the `s_image_buf` array is gone). */

#define LP_OTA_FLASH_PAGE_SIZE  256U
#define LP_OTA_BANK_B_SIZE      (1024U * 1024U)  /* see LP_OTA_MAIN_MAX_BYTES */

static uint32_t s_image_total_size     = 0;
static uint32_t s_image_bytes_written  = 0;  /* bytes successfully written to Bank B */
static uint32_t s_image_expected_crc   = 0;
static bool     s_image_active         = false;
static char     s_image_staged_version[32] = {0};

/* 0.A.183 Option 2: per-chunk scratch-region remap. Each entry records
 * a chunk's logical position (offset within the image) and its actual
 * physical location in scratch (the original Bank B write was corrupt).
 * FINALIZE CRC + stage-copy iter both consult this table to read the
 * "correct" bytes for each chunk. */
typedef struct {
    uint32_t image_offset;   /* logical offset within the OTA image */
    uint32_t length;         /* bytes (== k.data_len for that chunk) */
    uint32_t scratch_offset; /* offset within LP_OTA_SCRATCH_OFFSET region */
} ota_remap_entry_t;

static ota_remap_entry_t s_remap[LP_OTA_REMAP_MAX];
static uint32_t s_remap_count    = 0;
/* Next free scratch page offset (within scratch region). Always page-
 * aligned; chunks may span multiple pages so we allocate ceil-to-page. */
static uint32_t s_scratch_next   = 0;

/* Look up the physical chip address from which to read byte `image_off`
 * within the OTA image. Returns:
 *   true  with `*chip_addr_out` set to the chip address to read from
 *         (Bank B + image_off OR scratch + offset)
 *   false on lookup failure (shouldn't happen for in-range image_off)
 *
 * Linear scan of s_remap is O(N) per byte; for the FINALIZE CRC walk
 * over 500 KB this is up to ~32M ops worst case, but s_remap_count is
 * normally <16 so it's fine. */
static inline uint32_t resolved_chip_addr(uint32_t image_off)
{
    for (uint32_t i = 0; i < s_remap_count; i++) {
        const ota_remap_entry_t *e = &s_remap[i];
        if (image_off >= e->image_offset &&
            image_off <  e->image_offset + e->length) {
            return LP_OTA_SCRATCH_OFFSET + e->scratch_offset
                 + (image_off - e->image_offset);
        }
    }
    return FW_BANK_B_OFFSET + image_off;
}

/* Scratch buffer for one OSPI page (256 B). Used only by the
 * partial-last-page fixup in CHUNK + FINALIZE; doesn't hold the whole
 * image. */
static uint8_t  s_page_pad[LP_OTA_FLASH_PAGE_SIZE]
    __attribute__((aligned(64), section(".bss.s_page_pad")));

/* Readback verify buffer is unused since 0.A.152 (FINALIZE readback
 * skipped pending a working bare-metal read implementation). Reserve
 * `[[maybe_unused]]` via an `__attribute__` to keep the symbol around
 * for when we add STIG-based readback back in. */
__attribute__((unused))
static uint8_t  s_verify_buf[LP_OTA_FLASH_PAGE_SIZE]
    __attribute__((aligned(64), section(".bss.s_verify_buf")));

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

/* Incremental CRC32 update — same polynomial as lp_ota_crc32 above.
 * Caller seeds with `0xFFFFFFFFu`, feeds N chunks, then bit-inverts the
 * final state. Used by FINALIZE's readback verification (we can't fit
 * the full image in RAM to compute the CRC in one shot). */
static uint32_t lp_ota_crc32_update(uint32_t crc, const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        crc ^= (uint32_t)data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
        }
    }
    return crc;
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
 *  11  uint32   current_role       (OrbitRole; force-encoded since 0=CONTROLLER is valid)
 *  12  uint32   watchdog_strikes   (F2c diagnostic; force-encoded since 0 is the
 *                                   healthy-boot value)
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
    /* G1-bridge enabler (2026-05-20): emit the provisioned role so the
     * bridge can cross-check it against the manifest before sending
     * BEGIN. Force-encoded — 0 (CONTROLLER) is a valid role and proto3
     * zero-suppression would otherwise hide it. */
    n += pb_uint32_force(dst + n, 11, (uint32_t)LpDeviceConfig_Get()->role);
    /* F2c Session 1 (2026-05-30): expose the watchdog strike count for
     * UI diagnostics. 0 = healthy; ≥3 means the (future) SBL chooser
     * will fall back to the previous bank on the next boot. Force-
     * encoded because 0 is the desired-state value and proto3 zero-
     * suppression would otherwise hide it. */
    n += pb_uint32_force(dst + n, 12, meta.watchdog_strikes);
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
    /* 2026-05-20 OTA Gap 1-LP / Gap 2-LP: bridge-provided role + downgrade
     * intent. Both fields are optional on the wire — bridges that don't
     * send them get the old behavior (no role check, downgrade allowed). */
    uint32_t       expected_role;     /* OrbitRole; only valid if has_expected_role */
    bool           allow_downgrade;
    bool           has_expected_role; /* true if field 5 was present on the wire */
} fw_begin_t;

/* Parse the integer N out of a "0.A.<N>+<sha>[-dirty]" version string,
 * mirroring the bridge-side `parseAlphaN` in orbitOtaPush.ts so the two
 * sides stay in lockstep. Returns -1 if the string doesn't match the
 * alpha format — caller treats that as "unknown, skip the gate". */
static int parse_alpha_n(const char *s)
{
    if (!s) return -1;
    if (s[0] != '0' || s[1] != '.' || s[2] != 'A' || s[3] != '.') return -1;
    const char *p = s + 4;
    if (*p < '0' || *p > '9') return -1;
    long n = 0;
    while (*p >= '0' && *p <= '9') {
        n = n * 10 + (*p - '0');
        if (n > 1000000) return -1;   /* sanity cap */
        p++;
    }
    if (*p != '+' && *p != '-' && *p != '\0') return -1;
    return (int)n;
}

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
            case 5: /* expected_role, varint — presence-tracked so 0
                     * (CONTROLLER) is distinguishable from "absent". */
                if (wt != 0 || pb_read_varint(&it, &v) != 0) return -1;
                out->expected_role     = (uint32_t)v;
                out->has_expected_role = true;
                break;
            case 6: /* allow_downgrade, bool (varint 0/1) */
                if (wt != 0 || pb_read_varint(&it, &v) != 0) return -1;
                out->allow_downgrade = (v != 0);
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

    /* Erase Bank A using the bare-metal STIG block-erase. Each block is
     * 256 KB so we only need ceil(image_size / 256 KB) blocks. The old
     * loop called hal_flash_erase_sector 256 times (for 4 KB "sectors")
     * but the SDK actually aligns each call to a 256 KB block boundary,
     * so it erased the same block 64 times — taking minutes per OTA
     * activate. Block-erase covers the full image span with N calls. */
    uint32_t blocks_needed = (image_size + 0x3FFFFu) / 0x40000u;
    if (blocks_needed == 0) blocks_needed = 1;
    DebugP_log("[OTA] stage-copy erasing %lu Bank-A block(s) (%lu KB)\r\n",
               (unsigned long)blocks_needed,
               (unsigned long)(blocks_needed * 256));
    for (uint32_t i = 0; i < blocks_needed; i++) {
        uint32_t blk_addr = LP_OTA_MAIN_LIVE_OFFSET + (i * 0x40000u);
        int rc = hal_flash_erase_block_dac(blk_addr);
        DebugP_log("[OTA] stage-copy erased Bank-A block @0x%06lX rc=%d\r\n",
                   (unsigned long)blk_addr, rc);
        if (rc != 0) {
            return -1;
        }
        vTaskDelay(1);
    }
    DebugP_log("[OTA] stage-copy erase done, starting B->A copy\r\n");

    /* SRAM + write-completion diagnostic. SRAM_PARTITION_REG @0x18
     * shows the read/write split of the controller's 1024 B SRAM.
     * WRITE_COMPLETION_CTRL_REG @0x38 controls auto-polling AND the
     * opcode the controller sends to the chip to poll WIP. */
    {
        volatile uint32_t *sram_part_p = (volatile uint32_t *)(uintptr_t)(0x0FC40000U + 0x18U);
        volatile uint32_t *wcc_p       = (volatile uint32_t *)(uintptr_t)(0x0FC40000U + 0x38U);
        uint32_t sram_part = *sram_part_p;
        uint32_t wcc_before = *wcc_p;
        uint32_t rd_words  = sram_part & 0xFFU;
        uint32_t wr_words  = 256U - rd_words;
        DebugP_log("[OTA] SRAM_PARTITION_REG = 0x%08lX (rd=%lu words/%lu B, wr=%lu words/%lu B)\r\n",
                   (unsigned long)sram_part,
                   (unsigned long)rd_words, (unsigned long)(rd_words * 4U),
                   (unsigned long)wr_words, (unsigned long)(wr_words * 4U));
        DebugP_log("[OTA] WRITE_COMPLETION_CTRL_REG before = 0x%08lX (poll_opcode=0x%02lX)\r\n",
                   (unsigned long)wcc_before, (unsigned long)(wcc_before & 0xFFU));

        /* 0.A.174 attempted to set bit 14 to disable auto-polling, but
         * bench showed bit 14 was ALREADY set (0x40 byte in 0x000340FF
         * is bit 14). So bit 14 must be ENABLE, not DISABLE. 0.A.175:
         * write WCC = 0 (everything off), then read back to see which
         * bits stick. The dangerous opcode 0xFF in low byte (= Cypress
         * "Reset to Default I/O Mode") needs to go regardless of which
         * bit controls polling. */
        *wcc_p = 0x00000000U;
        uint32_t wcc_after = *wcc_p;
        DebugP_log("[OTA] WRITE_COMPLETION_CTRL_REG after  = 0x%08lX  (wrote 0, read back)\r\n",
                   (unsigned long)wcc_after);
    }

    /* 0.A.175: CRC Bank B vs expected. If CHUNK writes were also
     * corrupted by the 0xFF auto-poll (this is the same chip + same
     * controller path as stage-copy), Bank B itself has garbage and
     * stage-copy can't possibly produce a correct Bank A. Knowing
     * Bank B's CRC tells us where the corruption actually lives. */
    {
        volatile uint32_t *cfg_p = (volatile uint32_t *)(uintptr_t)0x0FC40000U;
        *cfg_p = (*cfg_p) | (1U << 7);   /* ensure DAC enabled for XIP read */

        uint32_t crc = 0xFFFFFFFFu;
        const volatile uint8_t *xip_b =
            (const volatile uint8_t *)(uintptr_t)(0x60000000U + FW_BANK_B_OFFSET);
        for (uint32_t i = 0; i < image_size; i++) {
            uint8_t byte = xip_b[i];
            crc ^= (uint32_t)byte;
            for (int b = 0; b < 8; b++) {
                crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
            }
        }
        uint32_t crc_b = ~crc;
        DebugP_log("[OTA] Bank-B full CRC32 = 0x%08lX  expected = 0x%08lX  %s\r\n",
                   (unsigned long)crc_b,
                   (unsigned long)s_image_expected_crc,
                   (crc_b == s_image_expected_crc) ? "MATCH (Bank B is clean)"
                                                    : "MISMATCH (Bank B is corrupt — CHUNK writes were affected too)");
    }

    /* DAC mode state diagnostic — the LP build leaves DAC OFF by default
     * (syscfg config: "indirect mode" — see hal_flash.c comment), so
     * accesses to 0x60000000+ hang until we enable DAC bit. Enable it
     * BEFORE the xip read, verify it stuck, then read. */
    {
        #define HAL_OSPI_CFG    0x0FC40000U
        volatile uint32_t *cfg_p = (volatile uint32_t *)(uintptr_t)HAL_OSPI_CFG;
        uint32_t cfg_pre = *cfg_p;
        DebugP_log("[OTA] OSPI CONFIG_REG pre-enable  = 0x%08lX (DAC bit %s)\r\n",
                   (unsigned long)cfg_pre,
                   (cfg_pre & (1U << 7)) ? "ON" : "OFF");
        *cfg_p = cfg_pre | (1U << 7);
        uint32_t cfg_post = *cfg_p;
        DebugP_log("[OTA] OSPI CONFIG_REG post-enable = 0x%08lX (DAC bit %s)\r\n",
                   (unsigned long)cfg_post,
                   (cfg_post & (1U << 7)) ? "ON" : "OFF");

        uint8_t dac_buf[16] = {0};
        const volatile uint8_t *xip = (const volatile uint8_t *)
                                      (uintptr_t)(0x60000000U + FW_BANK_B_OFFSET);
        for (int k = 0; k < 16; k++) dac_buf[k] = xip[k];
        DebugP_log("[OTA] diag DAC xip: %02X %02X %02X %02X %02X %02X %02X %02X "
                   "%02X %02X %02X %02X %02X %02X %02X %02X\r\n",
                   dac_buf[0], dac_buf[1], dac_buf[2], dac_buf[3],
                   dac_buf[4], dac_buf[5], dac_buf[6], dac_buf[7],
                   dac_buf[8], dac_buf[9], dac_buf[10], dac_buf[11],
                   dac_buf[12], dac_buf[13], dac_buf[14], dac_buf[15]);
    }

    /* Copy B -> A using direct DAC memcpy for reads and `hal_flash_write_dac`
     * for writes. The 0.A.159 bench showed SDK `Flash_read` for 16 bytes
     * works but 8 KB hangs (probably the SDK's INDIRECT_READ_XFER FIFO-
     * burst loop wedges on multi-cycle reads after a DAC write). Bypassing
     * the SDK entirely — pure memcpy from 0x60000000 + addr through the
     * XIP read window — gives us the same hardware path the diagnostic
     * `diag DAC xip` proved works.
     *
     * DAC writes (hal_flash_write_dac) toggle the DAC bit OFF on exit.
     * We re-enable it at the top of each iter before the read memcpy. */
    static uint8_t s_copy_buf[8192] __attribute__((aligned(64)));
    uint32_t remaining = image_size;
    uint32_t off = 0;
    uint32_t iter = 0;
    uint32_t total_iters = (image_size + sizeof(s_copy_buf) - 1U) / sizeof(s_copy_buf);
    DebugP_log("[OTA] stage-copy starting (%lu iters of %u B, DAC-memcpy-read + DAC-write)\r\n",
               (unsigned long)total_iters, (unsigned)sizeof(s_copy_buf));

    #define DAC_CFG_REG_ADDR     0x0FC40000U
    #define DAC_BIT              (1U << 7)

    while (remaining > 0) {
        uint32_t n = (remaining > sizeof(s_copy_buf)) ? sizeof(s_copy_buf)
                                                       : remaining;
        if (n < sizeof(s_copy_buf)) {
            memset(s_copy_buf + n, 0xFF, sizeof(s_copy_buf) - n);
        }
        uint32_t write_len = (n + 255U) & ~255U;
        if (write_len > sizeof(s_copy_buf)) write_len = sizeof(s_copy_buf);

        bool verbose = (iter < 4U) || ((iter & 0x7U) == 0U) || (iter + 1U == total_iters);
        if (verbose) {
            DebugP_log("[OTA] stage-copy iter %lu/%lu: r@0x%06lX w@0x%06lX len=%lu\r\n",
                       (unsigned long)iter, (unsigned long)total_iters,
                       (unsigned long)(FW_BANK_B_OFFSET + off),
                       (unsigned long)(LP_OTA_MAIN_LIVE_OFFSET + off),
                       (unsigned long)write_len);
        }

        /* Ensure DAC is enabled. hal_flash_write_dac always clears it on
         * exit, so we re-enable here. Reads through 0x60000000+ only
         * work when DAC bit is set. */
        {
            volatile uint32_t *cfg = (volatile uint32_t *)(uintptr_t)DAC_CFG_REG_ADDR;
            *cfg = (*cfg) | DAC_BIT;
        }

        /* 0.A.183: read each byte via resolved_chip_addr() so redirected
         * chunks in scratch get pulled from the correct location. Falls
         * back to plain Bank-B reads when remap is empty (common case).
         * Byte-at-a-time read with remap lookup is slow; do the lookup
         * once per byte but skip ahead within an unmapped contiguous
         * range. Most bytes are unmapped; the inner-loop check is cheap
         * when s_remap_count is small. */
        if (s_remap_count == 0U) {
            /* Fast path: no redirects, plain Bank-B read. */
            const volatile uint8_t *xip =
                (const volatile uint8_t *)(uintptr_t)(0x60000000U + FW_BANK_B_OFFSET + off);
            uint32_t *dst32 = (uint32_t *)s_copy_buf;
            const volatile uint32_t *src32 = (const volatile uint32_t *)xip;
            uint32_t words = n / 4U;
            for (uint32_t w = 0; w < words; w++) dst32[w] = src32[w];
            uint32_t tail = n - (words * 4U);
            for (uint32_t b = 0; b < tail; b++) {
                s_copy_buf[words * 4U + b] = xip[words * 4U + b];
            }
        } else {
            /* Slow path: at least one chunk was redirected. Read byte-at-
             * a-time via resolved_chip_addr. */
            for (uint32_t i = 0; i < n; i++) {
                uint32_t chip_addr = resolved_chip_addr(off + i);
                s_copy_buf[i] = *(const volatile uint8_t *)
                                (uintptr_t)(0x60000000U + chip_addr);
            }
        }

        if (iter == 0U) {
            DebugP_log("[OTA] iter0 source [0..15]: %02X %02X %02X %02X %02X %02X %02X %02X "
                       "%02X %02X %02X %02X %02X %02X %02X %02X\r\n",
                       s_copy_buf[0], s_copy_buf[1], s_copy_buf[2], s_copy_buf[3],
                       s_copy_buf[4], s_copy_buf[5], s_copy_buf[6], s_copy_buf[7],
                       s_copy_buf[8], s_copy_buf[9], s_copy_buf[10], s_copy_buf[11],
                       s_copy_buf[12], s_copy_buf[13], s_copy_buf[14], s_copy_buf[15]);
            bool all_ff_or_zero = true;
            for (int k = 0; k < 16; k++) {
                if (s_copy_buf[k] != 0xFF && s_copy_buf[k] != 0x00) {
                    all_ff_or_zero = false;
                    break;
                }
            }
            if (all_ff_or_zero) {
                DebugP_log("[OTA] iter0 source is ALL 0x00/0xFF — aborting stage-copy "
                           "(would brick Bank A)\r\n");
                return -1;
            }
        }

        /* 0.A.187: switch back to hal_flash_write_dac (DAC mode). 0.A.186
         * bench: stage-copy via SDK `hal_flash_write` (INDIRECT_WRITE_XFER)
         * fails with the historical CPSW-broken signature (`sdk=-1 sr1=0x02
         * ctrl_post=0x60 verify=FF`) even though CPSW is closed at this
         * point — INDIRECT_WRITE_XFER is broken in our runtime context
         * regardless of CPSW state (see lp-am2434-runtime-flashwrite-
         * unresolved.md). DAC writes via hal_flash_write_dac (with the
         * 0.A.186 "keep DAC enabled" fix) now reliably program full
         * data — proven by Bank B CHUNK writes hitting clean CRC. */
        int wr_rc = hal_flash_write_dac(LP_OTA_MAIN_LIVE_OFFSET + off, s_copy_buf,
                                    write_len);
        if (wr_rc != 0) {
            DebugP_log("[OTA] stage-copy iter %lu WRITE FAIL rc=%d\r\n",
                       (unsigned long)iter, wr_rc);
            return -1;
        }
        off       += n;
        remaining -= n;
        iter++;
    }

    /* Post-copy verification: full CRC32 of Bank A vs the expected
     * CRC from FwBeginUpdate. If CRC matches, the ENTIRE image is
     * bit-exact (a single-byte error anywhere would shift the CRC).
     * Also do a 3-point spot-check to localize where any mismatch
     * lives if CRC fails. */
    {
        /* Re-enable DAC (write_dac disabled it on exit). */
        volatile uint32_t *cfg_p = (volatile uint32_t *)(uintptr_t)0x0FC40000U;
        *cfg_p = (*cfg_p) | (1U << 7);

        /* Full CRC32 of Bank A from offset 0 to image_size. */
        uint32_t crc = 0xFFFFFFFFu;
        const volatile uint8_t *xip_a =
            (const volatile uint8_t *)(uintptr_t)(0x60000000U + LP_OTA_MAIN_LIVE_OFFSET);
        for (uint32_t i = 0; i < image_size; i++) {
            uint8_t byte = xip_a[i];
            crc ^= (uint32_t)byte;
            for (int b = 0; b < 8; b++) {
                crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
            }
        }
        uint32_t crc_a = ~crc;
        bool crc_ok = (crc_a == s_image_expected_crc);
        DebugP_log("[OTA] Bank-A full CRC32 = 0x%08lX  expected = 0x%08lX  %s\r\n",
                   (unsigned long)crc_a,
                   (unsigned long)s_image_expected_crc,
                   crc_ok ? "MATCH (image bit-exact)" : "MISMATCH");

        if (!crc_ok) {
            /* Byte-by-byte A vs B scan — find first differing byte, count
             * total mismatches, log the surrounding context. Tells us
             * whether corruption is at a page boundary (256), iter
             * boundary (8 KB), or scattered. */
            const volatile uint8_t *xip_b =
                (const volatile uint8_t *)(uintptr_t)(0x60000000U + FW_BANK_B_OFFSET);
            int64_t first_diff = -1;
            uint32_t total_diff = 0;
            for (uint32_t i = 0; i < image_size; i++) {
                if (xip_a[i] != xip_b[i]) {
                    if (first_diff < 0) first_diff = (int64_t)i;
                    total_diff++;
                }
            }
            DebugP_log("[OTA] mismatch count = %lu of %lu bytes  first_diff @ image[+%ld] (chipA @0x%06lX)\r\n",
                       (unsigned long)total_diff,
                       (unsigned long)image_size,
                       (long)first_diff,
                       (long)(LP_OTA_MAIN_LIVE_OFFSET + (uint32_t)first_diff));
            if (first_diff >= 0) {
                /* Context: 16 bytes before + 16 bytes at/after the diff,
                 * from both A and B. Use a safe start that avoids
                 * negative indexing. */
                uint32_t ctx_start = ((uint32_t)first_diff >= 8U)
                                     ? ((uint32_t)first_diff - 8U) : 0U;
                if (ctx_start + 16U > image_size) ctx_start = image_size - 16U;
                uint8_t a16[16], b16[16];
                for (int k = 0; k < 16; k++) {
                    a16[k] = xip_a[ctx_start + k];
                    b16[k] = xip_b[ctx_start + k];
                }
                DebugP_log("[OTA] A[+%lu..+%lu]: %02X %02X %02X %02X %02X %02X %02X %02X "
                           "%02X %02X %02X %02X %02X %02X %02X %02X\r\n",
                           (unsigned long)ctx_start, (unsigned long)(ctx_start + 15U),
                           a16[0], a16[1], a16[2], a16[3], a16[4], a16[5], a16[6], a16[7],
                           a16[8], a16[9], a16[10], a16[11], a16[12], a16[13], a16[14], a16[15]);
                DebugP_log("[OTA] B[+%lu..+%lu]: %02X %02X %02X %02X %02X %02X %02X %02X "
                           "%02X %02X %02X %02X %02X %02X %02X %02X\r\n",
                           (unsigned long)ctx_start, (unsigned long)(ctx_start + 15U),
                           b16[0], b16[1], b16[2], b16[3], b16[4], b16[5], b16[6], b16[7],
                           b16[8], b16[9], b16[10], b16[11], b16[12], b16[13], b16[14], b16[15]);
                /* Diagnose boundary alignment of first_diff. */
                uint32_t fd = (uint32_t)first_diff;
                const char *bnd =
                    (fd % 8192U == 0U) ? "8 KB iter boundary"   :
                    (fd % 4096U == 0U) ? "4 KB boundary"        :
                    (fd % 1024U == 0U) ? "1 KB chunk boundary"  :
                    (fd %  256U == 0U) ? "256 B page boundary"  :
                    (fd %   64U == 0U) ? "64 B cache-line boundary" :
                                          "non-aligned (mid-page)";
                DebugP_log("[OTA] first_diff alignment: %s\r\n", bnd);
            }
            /* Don't trigger warm-reset on bad image — that's how we've
             * been bricking Bank A. Return -1 so the caller aborts. */
            return -1;
        }
    }
    {
        volatile uint32_t *cfg_p = (volatile uint32_t *)(uintptr_t)0x0FC40000U;
        *cfg_p = (*cfg_p) | (1U << 7);  /* re-enable DAC (write_dac disabled it) */

    }
    DebugP_log("[OTA] stage-copy completed (%lu iter, %lu B)\r\n",
               (unsigned long)iter, (unsigned long)off);
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
    int rc = send_frame(fd, LP_OTA_TAG_STATUS, body, n);
    if (rc != 0) {
        DebugP_log("[OTA] push_status_progress send_frame FAIL rc=%d (TCP backpressure / closed?)\r\n", rc);
    }
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
                /* Streaming OTA: erase Bank B up-front so each subsequent
                 * Chunk can be written directly via hal_flash_write_dac.
                 * No MSRAM image buffer required. */
                fw_begin_t b;
                if (decode_begin(body, bodylen, &b) != 0) {
                    push_status_error(c->fd, LP_OTA_ERR_DECODE, "FwBegin decode");
                    break;
                }
                if (b.total_size == 0 || b.total_size > LP_OTA_BANK_B_SIZE) {
                    DebugP_log("[OTA] BEGIN rejected size=%lu (max %u)\r\n",
                               (unsigned long)b.total_size,
                               (unsigned)LP_OTA_BANK_B_SIZE);
                    push_status_error(c->fd, LP_OTA_ERR_TOO_BIG,
                                      "image > LP_OTA_BANK_B_SIZE");
                    break;
                }

                /* G1-LP (2026-05-20): role-mismatch gate. Reject before
                 * the ~5 s Bank-B erase if the bridge's claimed-bundle-
                 * role doesn't match what this LP is provisioned as.
                 * Bridges that don't send the field (older firmwares /
                 * test harnesses) skip the gate. See
                 * docs/lp-am2434-ota-hardening-plan.md Gap 1-LP. */
                if (b.has_expected_role) {
                    OrbitRole self_role = (OrbitRole)LpDeviceConfig_Get()->role;
                    OrbitRole bundle_role = (OrbitRole)b.expected_role;
                    if (bundle_role != self_role) {
                        DebugP_log("[OTA] BEGIN role-mismatch reject: bundle=%s(%u) self=%s(%u)\r\n",
                                   OrbitRole_Name(bundle_role), (unsigned)bundle_role,
                                   OrbitRole_Name(self_role),   (unsigned)self_role);
                        push_status_error(c->fd, LP_OTA_ERR_ROLE_MISMATCH,
                                          "bundle role does not match LP role");
                        break;
                    }
                }

                /* G2-LP (2026-05-20): firmware-side downgrade gate.
                 * If incoming version's 0.A.<N> < running N, reject unless
                 * the bridge has explicitly opted in via allow_downgrade.
                 * Equal N is allowed (re-flash same build with new sha).
                 * Non-alpha version strings → skip gate. */
                {
                    int incoming_n = parse_alpha_n(b.version);
                    int running_n  = parse_alpha_n(LP_FW_VERSION);
                    if (incoming_n >= 0 && running_n >= 0 &&
                        incoming_n < running_n && !b.allow_downgrade)
                    {
                        DebugP_log("[OTA] BEGIN downgrade reject: incoming=0.A.%d running=0.A.%d (allow_downgrade=0)\r\n",
                                   incoming_n, running_n);
                        push_status_error(c->fd, LP_OTA_ERR_DOWNGRADE,
                                          "downgrade rejected (set allow_downgrade=true to force)");
                        break;
                    }
                }

                DebugP_log("[OTA] BEGIN (streaming) size=%lu crc=0x%08lx ver=%s\r\n",
                           (unsigned long)b.total_size,
                           (unsigned long)b.crc32, b.version);
                s_image_bytes_written = 0;
                s_image_total_size    = b.total_size;
                s_image_expected_crc  = b.crc32;
                s_image_active        = true;
                /* 0.A.183: reset scratch remap state per BEGIN. */
                s_remap_count         = 0;
                s_scratch_next        = 0;
                strncpy(s_image_staged_version, b.version,
                        sizeof(s_image_staged_version) - 1);
                s_image_staged_version[sizeof(s_image_staged_version) - 1] = '\0';

                /* Erase Bank B using the bare-metal STIG path so it
                 * works while CPSW + lwIP are still running. The SDK's
                 * `hal_flash_erase_sector` (Flash_eraseBlk) hangs in
                 * this context (0.A.144 bench evidence: BEGIN erase
                 * with CPSW active never returned).
                 *
                 * Block-erase granularity is 256 KB. Erase enough
                 * blocks to cover the full image size, rounded up. */
                uint32_t blocks_needed = (b.total_size + 0x3FFFFu) / 0x40000u;  /* ceil(size / 256KB) */
                if (blocks_needed == 0) blocks_needed = 1;
                DebugP_log("[OTA] BEGIN erasing %lu Bank-B block(s) + 1 scratch block (%lu KB)\r\n",
                           (unsigned long)blocks_needed,
                           (unsigned long)((blocks_needed + 1) * 256));
                uint32_t erase_start_us = ClockP_getTimeUsec();
                for (uint32_t i = 0; i < blocks_needed; i++) {
                    uint32_t blk_addr = FW_BANK_B_OFFSET + (i * 0x40000u);
                    int rc = hal_flash_erase_block_dac(blk_addr);
                    if (rc != 0) {
                        DebugP_log("[OTA] BEGIN erase fail @0x%06lX rc=%d\r\n",
                                   (unsigned long)blk_addr, rc);
                        s_image_active = false;
                        push_status_error(c->fd, LP_OTA_ERR_FINALIZE,
                                          "Bank B erase failed");
                        break;
                    }
                }
                if (!s_image_active) break;
                /* Also erase the scratch block at LP_OTA_SCRATCH_OFFSET so
                 * any chunk redirected here lands on fresh 0xFF pages. */
                {
                    int rc = hal_flash_erase_block_dac(LP_OTA_SCRATCH_OFFSET);
                    if (rc != 0) {
                        DebugP_log("[OTA] BEGIN scratch erase fail @0x%06lX rc=%d\r\n",
                                   (unsigned long)LP_OTA_SCRATCH_OFFSET, rc);
                        s_image_active = false;
                        push_status_error(c->fd, LP_OTA_ERR_FINALIZE,
                                          "scratch erase failed");
                        break;
                    }
                }
                uint32_t erase_us = ClockP_getTimeUsec() - erase_start_us;
                DebugP_log("[OTA] BEGIN erase complete in %lu ms\r\n",
                           (unsigned long)(erase_us / 1000U));
                push_status_progress(c->fd);
                break;
            }
            case LP_OTA_TAG_CHUNK: {
                /* Streaming: write chunk directly to flash via DAC mode. */
                fw_chunk_t k;
                if (decode_chunk(body, bodylen, &k) != 0 || k.data == NULL) {
                    push_status_error(c->fd, LP_OTA_ERR_DECODE, "FwChunk decode");
                    break;
                }
                if (!s_image_active) {
                    push_status_error(c->fd, LP_OTA_ERR_CHUNK,
                                      "chunk before begin");
                    break;
                }
                /* Debug build: log EVERY chunk so we can see exactly
                 * where the stream stalls. 32 KB / 1 KB = 32 lines max. */
                DebugP_log("[OTA] CHUNK off=%lu len=%lu\r\n",
                           (unsigned long)k.offset,
                           (unsigned long)k.data_len);
                if (k.offset + k.data_len > s_image_total_size) {
                    DebugP_log("[OTA] CHUNK overflow off=%lu len=%lu total=%lu\r\n",
                               (unsigned long)k.offset,
                               (unsigned long)k.data_len,
                               (unsigned long)s_image_total_size);
                    push_status_error(c->fd, LP_OTA_ERR_CHUNK,
                                      "chunk past declared image size");
                    break;
                }
                /* Require in-order delivery for now — bridge does this
                 * today (`orbitOtaPush.ts`). Out-of-order would require
                 * a sparse-tracking bitmap; skip for the first cut. */
                if (k.offset != s_image_bytes_written) {
                    DebugP_log("[OTA] CHUNK out-of-order off=%lu expected=%lu\r\n",
                               (unsigned long)k.offset,
                               (unsigned long)s_image_bytes_written);
                    push_status_error(c->fd, LP_OTA_ERR_CHUNK,
                                      "out-of-order chunk");
                    break;
                }
                /* Chunk offset must be page-aligned (every chunk except
                 * possibly the very last writes whole pages). */
                if ((k.offset % LP_OTA_FLASH_PAGE_SIZE) != 0) {
                    push_status_error(c->fd, LP_OTA_ERR_CHUNK,
                                      "chunk offset not page-aligned");
                    break;
                }
                /* Verify per-chunk CRC. */
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

                /* Write whole pages directly from the receive buffer.
                 * The last chunk may be < page-multiple; copy its
                 * remainder to s_page_pad, 0xFF-pad, and write as one
                 * full page. */
                uint32_t full_pages_bytes = (k.data_len / LP_OTA_FLASH_PAGE_SIZE)
                                            * LP_OTA_FLASH_PAGE_SIZE;
                uint32_t remainder         = k.data_len - full_pages_bytes;

                /* 0.A.183 Option 2: write to Bank B; verify via XIP memcpy;
                 * on mismatch redirect to a fresh scratch page (different
                 * chip address = no same-page back-to-back PP = no wedge).
                 * Up to a few scratch retries per chunk if scratch itself
                 * gets byte-loss. Builds s_remap[] which FINALIZE CRC and
                 * stage-copy honor. */
                bool chunk_clean = false;
                {
                    /* Helper: write `payload` bytes to `phys_addr`, padding
                     * the tail with 0xFF if needed. payload is k.data_len
                     * bytes (may be < LP_OTA_FLASH_PAGE_SIZE for last chunk).
                     * Returns rc from hal_flash_write_dac (0 = OK). */
                    /* Attempt 0: write to Bank B at k.offset. */
                    uint32_t attempt_addr = FW_BANK_B_OFFSET + k.offset;
                    bool last_attempt_was_bank_b = true;
                    uint32_t scratch_off_used = 0;
                    int max_attempts = 4;
                    for (int attempt = 0; attempt < max_attempts && !chunk_clean; attempt++) {
                        if (full_pages_bytes > 0) {
                            int rc = hal_flash_write_dac(attempt_addr,
                                                         k.data, full_pages_bytes);
                            if (rc != 0) {
                                DebugP_log("[OTA] CHUNK DAC write FAIL @0x%06lX rc=%d\r\n",
                                           (unsigned long)attempt_addr, rc);
                                /* fall through to next attempt slot */
                            }
                        }
                        if (remainder > 0) {
                            memset(s_page_pad, 0xFF, LP_OTA_FLASH_PAGE_SIZE);
                            memcpy(s_page_pad, k.data + full_pages_bytes, remainder);
                            int rc = hal_flash_write_dac(
                                attempt_addr + full_pages_bytes,
                                s_page_pad, LP_OTA_FLASH_PAGE_SIZE);
                            if (rc != 0) {
                                DebugP_log("[OTA] CHUNK DAC pad FAIL @0x%06lX rc=%d\r\n",
                                           (unsigned long)(attempt_addr + full_pages_bytes), rc);
                            }
                        }
                        /* Verify via XIP memcpy (reads don't wedge the chip). */
                        volatile uint32_t *cfg_p = (volatile uint32_t *)(uintptr_t)0x0FC40000U;
                        *cfg_p = (*cfg_p) | (1U << 7);
                        const volatile uint8_t *xip =
                            (const volatile uint8_t *)(uintptr_t)(0x60000000U + attempt_addr);
                        /* 0.A.184: explicit cache invalidate before read so
                         * we definitely see fresh chip bytes (not stale
                         * cache lines from before the write). */
                        CacheP_inv((void *)xip, k.data_len, CacheP_TYPE_ALL);
                        bool mismatch = false;
                        int32_t first_mismatch = -1;
                        for (uint32_t i = 0; i < k.data_len; i++) {
                            if (xip[i] != k.data[i]) {
                                mismatch = true;
                                first_mismatch = (int32_t)i;
                                break;
                            }
                        }
                        /* 0.A.184: diagnostic for FIRST chunk failure of
                         * each OTA. Tells us whether the write half-worked
                         * (mostly correct, partial tail loss) vs fully
                         * failed (all FF / all garbage). */
                        if (mismatch && k.offset == 0 && attempt < 2) {
                            uint8_t r[16], s[16];
                            uint32_t base = (first_mismatch > 0)
                                            ? (uint32_t)first_mismatch & ~0x7U : 0;
                            for (int x = 0; x < 16 && base + x < k.data_len; x++) {
                                r[x] = xip[base + x];
                                s[x] = k.data[base + x];
                            }
                            DebugP_log("[OTA] CHUNK0 attempt%d first_diff @%ld\r\n",
                                       attempt, (long)first_mismatch);
                            DebugP_log("[OTA]   chip @0x%06lX+%lu: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
                                       (unsigned long)attempt_addr,
                                       (unsigned long)base,
                                       r[0], r[1], r[2], r[3], r[4], r[5], r[6], r[7],
                                       r[8], r[9], r[10], r[11], r[12], r[13], r[14], r[15]);
                            DebugP_log("[OTA]   src                 : %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
                                       s[0], s[1], s[2], s[3], s[4], s[5], s[6], s[7],
                                       s[8], s[9], s[10], s[11], s[12], s[13], s[14], s[15]);
                        }
                        if (!mismatch) {
                            chunk_clean = true;
                            if (!last_attempt_was_bank_b) {
                                /* Record remap entry: image bytes [k.offset,
                                 * k.offset+k.data_len) → scratch at
                                 * scratch_off_used. */
                                if (s_remap_count >= LP_OTA_REMAP_MAX) {
                                    DebugP_log("[OTA] CHUNK remap table full (>%lu)\r\n",
                                               (unsigned long)LP_OTA_REMAP_MAX);
                                    push_status_error(c->fd, LP_OTA_ERR_CHUNK,
                                                      "remap table overflow");
                                    break;
                                }
                                s_remap[s_remap_count].image_offset   = k.offset;
                                s_remap[s_remap_count].length         = k.data_len;
                                s_remap[s_remap_count].scratch_offset = scratch_off_used;
                                s_remap_count++;
                                DebugP_log("[OTA] CHUNK redirected off=0x%06lX -> scratch+0x%06lX (remap #%lu)\r\n",
                                           (unsigned long)k.offset,
                                           (unsigned long)scratch_off_used,
                                           (unsigned long)s_remap_count);
                            }
                            break;
                        }
                        /* Mismatch: pick next scratch slot for retry. */
                        /* Pad chunk size up to a page boundary for the
                         * scratch allocation so writes are page-aligned. */
                        uint32_t chunk_pages = (k.data_len + LP_OTA_FLASH_PAGE_SIZE - 1U)
                                              / LP_OTA_FLASH_PAGE_SIZE;
                        uint32_t alloc_bytes = chunk_pages * LP_OTA_FLASH_PAGE_SIZE;
                        if (s_scratch_next + alloc_bytes > LP_OTA_SCRATCH_SIZE) {
                            DebugP_log("[OTA] CHUNK scratch region full (%lu+%lu > %u)\r\n",
                                       (unsigned long)s_scratch_next,
                                       (unsigned long)alloc_bytes,
                                       (unsigned)LP_OTA_SCRATCH_SIZE);
                            push_status_error(c->fd, LP_OTA_ERR_CHUNK,
                                              "scratch region exhausted");
                            break;
                        }
                        scratch_off_used     = s_scratch_next;
                        attempt_addr         = LP_OTA_SCRATCH_OFFSET + s_scratch_next;
                        s_scratch_next      += alloc_bytes;
                        last_attempt_was_bank_b = false;
                        DebugP_log("[OTA] CHUNK off=0x%06lX verify mismatch (attempt %d) -> retry to scratch+0x%06lX\r\n",
                                   (unsigned long)k.offset, attempt,
                                   (unsigned long)scratch_off_used);
                    }
                }
                if (!chunk_clean) {
                    DebugP_log("[OTA] CHUNK off=0x%06lX exhausted scratch retries\r\n",
                               (unsigned long)k.offset);
                    push_status_error(c->fd, LP_OTA_ERR_CHUNK,
                                      "chunk could not be cleanly written");
                    break;
                }
                s_image_bytes_written += k.data_len;
                push_status_progress(c->fd);
                break;
            }
            case LP_OTA_TAG_FINALIZE: {
                /* Streaming finalize: image is already in Bank B. Read
                 * back the whole image incrementally and recompute CRC32
                 * to verify. */
                DebugP_log("[OTA] FINALIZE entered (bytes_written=%lu of %lu)\r\n",
                           (unsigned long)s_image_bytes_written,
                           (unsigned long)s_image_total_size);
                uint32_t expected;
                if (decode_finalize(body, bodylen, &expected) != 0) {
                    push_status_error(c->fd, LP_OTA_ERR_DECODE, "FwFinalize decode");
                    break;
                }
                if (!s_image_active) {
                    push_status_error(c->fd, LP_OTA_ERR_FINALIZE,
                                      "finalize before begin");
                    break;
                }
                if (s_image_bytes_written != s_image_total_size) {
                    DebugP_log("[OTA] FINALIZE incomplete: bytes=%lu of %lu\r\n",
                               (unsigned long)s_image_bytes_written,
                               (unsigned long)s_image_total_size);
                    push_status_error(c->fd, LP_OTA_ERR_FINALIZE,
                                      "incomplete transfer");
                    break;
                }
                /* 0.A.183: full-image CRC honoring the scratch remap table.
                 * For each image byte, look up its physical chip address
                 * via resolved_chip_addr() — most bytes come from Bank B,
                 * but redirected chunks come from scratch. The remap
                 * lookup is O(N) per byte but s_remap_count is tiny so
                 * the total walk is ~500 KB × constant. */
                {
                    volatile uint32_t *cfg_p = (volatile uint32_t *)(uintptr_t)0x0FC40000U;
                    *cfg_p = (*cfg_p) | (1U << 7);   /* ensure DAC enabled */
                    DebugP_log("[OTA] FINALIZE remap entries: %lu\r\n",
                               (unsigned long)s_remap_count);
                    uint32_t crc = 0xFFFFFFFFu;
                    for (uint32_t i = 0; i < s_image_total_size; i++) {
                        uint32_t chip_addr = resolved_chip_addr(i);
                        uint8_t byte = *(const volatile uint8_t *)
                                       (uintptr_t)(0x60000000U + chip_addr);
                        crc ^= (uint32_t)byte;
                        for (int b = 0; b < 8; b++) {
                            crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
                        }
                    }
                    uint32_t crc_b = ~crc;
                    bool crc_ok = (crc_b == expected);
                    DebugP_log("[OTA] FINALIZE Bank-B (remapped) CRC32 = 0x%08lX  expected = 0x%08lX  %s\r\n",
                               (unsigned long)crc_b,
                               (unsigned long)expected,
                               crc_ok ? "MATCH" : "MISMATCH");
                    if (!crc_ok) {
                        /* Per-chunk scratch retry should have caught all
                         * byte-loss before FINALIZE. If we still have a
                         * CRC mismatch, something more fundamental is
                         * wrong (chunk CRCs on wire don't match what we
                         * wrote, etc.). Bridge can re-OTA from BEGIN. */
                        s_image_active = false;
                        s_image_bytes_written = 0;
                        push_status_error(c->fd, LP_OTA_ERR_BANK_B_REDO,
                                          "post-remap CRC mismatch — please re-BEGIN");
                        break;
                    }
                }
                /* 2026-05-26: update expected CRC to the FINALIZE-time
                 * value so stage-copy's Bank-A vs expected check uses
                 * the correct reference. Broker passes image_crc=0 at
                 * BEGIN (it accumulates running CRC during chunks and
                 * doesn't know the final value upfront); without this
                 * update, stage-copy would always fail its CRC check
                 * even after a bit-exact byte copy. */
                s_image_expected_crc = expected;
                DebugP_log("[OTA] FINALIZE ok (Bank B verified bit-exact) — expected_crc=0x%08lX\r\n",
                           (unsigned long)expected);
                push_status_progress(c->fd);
                break;
            }
            case LP_OTA_TAG_ACTIVATE: {
                /* Streaming OTA: image is already in Bank B and CRC-
                 * verified by FINALIZE. ACTIVATE just needs to decide
                 * whether to promote Bank B → Bank A and warm-reset.
                 *
                 * If `reboot=0` (bench POC) we're done — Bank B holds
                 * the validated image and Bank A is unchanged.
                 *
                 * If `reboot=1` (production OTA) we tear down Enet,
                 * copy Bank B → Bank A using `hal_flash_write_dac`, then
                 * warm-reset into the new firmware. */
                bool reboot = false;
                if (decode_activate(body, bodylen, &reboot) != 0) {
                    push_status_error(c->fd, LP_OTA_ERR_DECODE, "FwActivate decode");
                    break;
                }
                if (!s_image_active || s_image_bytes_written != s_image_total_size) {
                    push_status_error(c->fd, LP_OTA_ERR_ACTIVATE,
                                      "no verified image in Bank B");
                    break;
                }
                DebugP_log("[OTA] ACTIVATE image_in_bank_b=%lu reboot=%d\r\n",
                           (unsigned long)s_image_total_size, (int)reboot);

                /* Ack first so the bridge knows we accepted Activate. */
                push_status_progress(c->fd);
                vTaskDelay(pdMS_TO_TICKS(50));  /* drain TCP TX */

                if (!reboot) {
                    /* Bench / dry-run case: image is committed to Bank B
                     * but we don't promote it. Caller can verify, then
                     * trigger a real activate via a follow-up FwActivate
                     * with reboot=1. */
                    DebugP_log("[OTA] reboot=0 — Bank B image committed, no promotion\r\n");
                    s_image_active = false;
                    break;
                }

                /* Real activate: close Enet, copy B → A, warm-reset. */
                DebugP_log("[OTA] reboot=1 — closing Enet for stage-copy + reboot\r\n");

                /* F2c metadata write (added 2026-05-31). All 4 bench boards
                 * now run the sbl_chooser, so we MUST also write proper
                 * Bank B FwBankHeader + clear strikes + flip Bank A active=0
                 * so the chooser picks Bank B on next boot. Without this,
                 * the chooser would still pick Bank A (per the seed metadata)
                 * and load 0x080000 — which only works because stage-copy
                 * below mirrors Bank B's image to 0x080000. That's the
                 * "TI stock SBL fallback" path. The F2c metadata write
                 * gives us the proper "boot Bank B from 0x900000" path
                 * which is what enables rollback if a future OTA fails.
                 *
                 * Order matters: do this BEFORE stage-copy so even if the
                 * stage-copy or its warm-reset wedges, the metadata is on
                 * flash and the next boot still gets the new image (via
                 * F2c picking Bank B at 0x900000).
                 *
                 * Stage-copy kept as defence-in-depth for boards that
                 * somehow boot the old TI stock SBL (e.g., if F2c chooser
                 * is corrupted at 0x000000). Can be removed once we have
                 * a runtime check "is F2c on the chip?" and confirm yes.
                 *
                 * See memories/repo/f2c-rollback-proven-2026-05-31.md
                 * for the rollback proof; this closes the orbit OTA
                 * integration gap from
                 * memories/repo/ota-plus-f2c-integration-gap-2026-05-30.md. */
                {
                    extern uint32_t NovaFwUpdate_OrbitFinalize(uint32_t, uint32_t, const char *);
                    DebugP_log("[OTA] F2c metadata: NovaFwUpdate_OrbitFinalize(size=%lu, crc=0x%08lX, version='%s')\r\n",
                               (unsigned long)s_image_total_size,
                               (unsigned long)s_image_expected_crc,
                               s_image_staged_version);
                    uint32_t f2c_rc = NovaFwUpdate_OrbitFinalize(
                        s_image_total_size,
                        s_image_expected_crc,
                        s_image_staged_version);
                    DebugP_log("[OTA] F2c metadata write rc=%lu\r\n",
                               (unsigned long)f2c_rc);
                }

                /* Silence PHY-poll task FIRST, then EnetApp_driverClose.
                 * Without this, the polling task fires assertions on the
                 * just-closed handle and wedges CPU at 100 %. */
                extern void EnetApp_stopPhyRegisterPollingTask(void);
                EnetApp_stopPhyRegisterPollingTask();
                DebugP_log("[OTA] EnetApp_driverClose ...\r\n");
                EnetApp_driverClose(ENET_CPSW_3G, 0);

                /* Tear down the rest of syscfg drivers; selectively re-init
                 * just OSPI + Flash (UART left alive for DebugP_log). */
                DebugP_log("[OTA] Closing Board+Drivers (Flash, EEPROM, I2C, OSPI; UART kept) ...\r\n");
                Board_flashClose();
                Board_eepromClose();
                Drivers_i2cClose();
                Drivers_ospiClose();
                vTaskDelay(pdMS_TO_TICKS(50));

                DebugP_log("[OTA] Re-opening OSPI + Flash ...\r\n");
                Drivers_ospiOpen();
                int32_t flash_rc = Board_flashOpen();
                DebugP_log("[OTA] re-open: ospi=%p flash=%p flash_rc=%ld\r\n",
                           (void *)gOspiHandle[CONFIG_OSPI0],
                           (void *)gFlashHandle[CONFIG_FLASH0],
                           (long)flash_rc);
                if (gOspiHandle[CONFIG_OSPI0] == NULL ||
                    gFlashHandle[CONFIG_FLASH0] == NULL) {
                    DebugP_log("[OTA] re-open FAILED — Bank A intact, manual recovery required\r\n");
                    while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
                }

                /* CANCEL any stuck INDIRECT_WRITE_XFER state before the
                 * Bank A erase loop inside lp_ota_stage_copy_b_to_a. */
                hal_flash_clear_indirect_state();

                /* Stage copy Bank B → Bank A using hal_flash_write_dac.
                 * Erases Bank A in 4 KB sectors then copies in 256-byte
                 * pages. Total ~1 s for a 485 KB image. */
                DebugP_log("[OTA] staging Bank B -> Bank A (%lu B)\r\n",
                           (unsigned long)s_image_total_size);
                if (lp_ota_stage_copy_b_to_a(s_image_total_size) != 0) {
                    DebugP_log("[OTA] stage copy FAILED — Bank A may be partially overwritten. "
                               "Recovery: re-flash via Flash-LP.ps1.\r\n");
                    while (1) { vTaskDelay(pdMS_TO_TICKS(1000)); }
                }
                DebugP_log("[OTA] stage copy ok — warm-resetting into new firmware\r\n");
                /* 0.A.162: 5-second pause after verify diagnostic so the
                 * UART can drain and the bench operator can read the
                 * A==B / A!=B verdict BEFORE warm-reset wipes the screen. */
                vTaskDelay(pdMS_TO_TICKS(5000));

                /* 0.A.163: send Cypress software reset BEFORE the SoC
                 * warm-reset. The chip's volatile config still says
                 * "QPI+DTR" from our Flash_open; the next SBL boot
                 * tries to issue setup commands single-line and fails
                 * because the chip isn't listening on the single-line
                 * protocol. Software reset (RSTEN+RST) clears volatile
                 * config and returns the chip to factory single-line
                 * SDR — same state as if we'd power-cycled. */
                DebugP_log("[OTA] chip soft-reset (RSTEN+RST) before warm-reset\r\n");
                int rst_rc = hal_flash_chip_soft_reset();
                DebugP_log("[OTA] chip soft-reset rc=%d\r\n", rst_rc);
                /* Give UART one more tick to drain. */
                vTaskDelay(pdMS_TO_TICKS(100));

                s_image_active = false;
                lp_ota_warm_reset();  /* does not return */
                /* unreachable, but kept for the compiler: */
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

    /* 0.A.176: disable OSPI controller auto-polling at OTA task entry,
     * BEFORE the listen loop. The SDK Cypress driver's Board_flashOpen
     * (run in main) leaves WRITE_COMPLETION_CTRL_REG=0x000340FF whose
     * OPCODE field [7:0] is 0xFF — and on Cypress S25HL512T, opcode
     * 0xFF is "Reset to Default I/O Mode". Every auto-poll cycle the
     * controller issues to wait for WIP=0 actually sends a chip-reset
     * command. That mid-PP reset is the root cause of Bank B + Bank A
     * silent corruption (0.A.175 bench: Bank B CRC also MISMATCH).
     * Setting WCC=0 disables auto-polling entirely; our STIG-based
     * `hal_wait_wip_clear` in hal_flash.c handles WIP timing correctly. */
    {
        volatile uint32_t *wcc_p = (volatile uint32_t *)(uintptr_t)(0x0FC40000U + 0x38U);
        uint32_t wcc_before = *wcc_p;
        *wcc_p = 0x00000000U;
        uint32_t wcc_after = *wcc_p;
        DebugP_log("[OTA] startup: WCC %08lX -> %08lX (auto-poll disabled, kills 0xFF reset opcode)\r\n",
                   (unsigned long)wcc_before, (unsigned long)wcc_after);
    }

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

    /* conns must NOT live on the task stack — at 4108 B per conn the
     * array would smash an 8 KB stack. BSS-allocated instead. */
    static ota_conn_t conns[LP_OTA_MAX_CONNS];
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
                DebugP_log("[OTA] rx buffer FULL (rx_len=%u, cap=%u) — closing conn\r\n",
                           (unsigned)conns[i].rx_len,
                           (unsigned)sizeof(conns[i].rx));
                lwip_close(conns[i].fd);
                conns[i].fd = -1;
                conns[i].rx_len = 0;
                continue;
            }
            int n = lwip_recv(conns[i].fd,
                              &conns[i].rx[conns[i].rx_len], space, 0);
            if (n <= 0) {
                DebugP_log("[OTA] lwip_recv returned %d (errno=%d) — peer closed?\r\n",
                           n, errno);
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
