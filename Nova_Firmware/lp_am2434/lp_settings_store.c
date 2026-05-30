/*
 * lp_settings_store.c — RAM-backed ping-pong settings persistence (LP-AM2434)
 *
 * See lp_settings_store.h for the API contract and the bridge-replay
 * scheme that gives this stand-in apparent power-cycle persistence.
 *
 * Bank layout in MSRAM (per bank):
 *   [0..15]  LpBankHeader { magic, image_size, image_crc, sequence, schema[] }
 *   [16..]   blob bytes
 *
 * Total per bank = 16 hdr + LP_SETTINGS_MAX_BLOB_SIZE = 4112 bytes.
 * Two banks ≈ 8.2 KB out of MSRAM 0x70080000+0x160000 = 1.4 MB.
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */

#include "lp_settings_store.h"
#include <string.h>
#include <kernel/dpl/DebugP.h>
#include <FreeRTOS.h>
#include <task.h>

/* Phase D log helper: DebugP_log called BEFORE vTaskStartScheduler()
 * hangs the firmware on this LP build (semihosting / putchar blocks
 * before the kernel starts draining UART0). Gate every log inside
 * lp_settings_store on "scheduler is running" so smoke saves at
 * boot stay silent on UART0 but mid-session saves still log. */
#define LPS_LOG(...) do { \
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) { \
        DebugP_log(__VA_ARGS__); \
    } \
} while (0)

/* ─── Bank storage (in MSRAM via plain BSS) ───────────────────────────── */

typedef struct {
    uint32_t magic;
    uint32_t image_size;
    uint32_t image_crc;
    uint32_t sequence;
    char     schema[8];   /* zero-padded copy of LP_SETTINGS_SCHEMA_TAG */
} LpBankHeader;

#define LP_BANK_HDR_SIZE     ((uint32_t)sizeof(LpBankHeader))
#define LP_BANK_TOTAL_SIZE   (LP_BANK_HDR_SIZE + LP_SETTINGS_MAX_BLOB_SIZE)

static uint8_t s_bank_a[LP_BANK_TOTAL_SIZE];
static uint8_t s_bank_b[LP_BANK_TOTAL_SIZE];

/* Panel-default snapshot bank — operator's saved commissioning baseline.
 * Same wire format as the active banks (LpBankHeader followed by blob).
 * Sequence number is independent of the active-bank ping-pong sequence.
 * Marked dirty by SavePanelDefaults / RestorePanelDefaults so the bridge
 * cadence emits envelope field 33. */
static uint8_t s_panel_bank[LP_BANK_TOTAL_SIZE];
static bool    s_panel_dirty = false;

static int      s_active_bank = -1;
static uint32_t s_sequence    = 0;
static bool     s_blob_dirty  = false;
static LpSettingsApplyFn s_apply_cb = NULL;

void LpSettings_SetApplyCallback(LpSettingsApplyFn fn) { s_apply_cb = fn; }

/* ─── CRC-32 (Ethernet/zlib polynomial, matches Platform/) ────────────── */

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len)
{
    crc = ~crc;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320U & (-(crc & 1U)));
        }
    }
    return ~crc;
}

/* ─── Bank helpers ────────────────────────────────────────────────────── */

static uint8_t *bank_storage(int bank)
{
    return (bank == 0) ? s_bank_a : s_bank_b;
}

/* Validate header + blob CRC + schema tag. Returns true if usable.
 * Copies the header out via *out (may be NULL). */
static bool bank_is_valid(int bank, LpBankHeader *out)
{
    const uint8_t *p = bank_storage(bank);
    LpBankHeader h;
    memcpy(&h, p, LP_BANK_HDR_SIZE);

    if (h.magic != LP_SETTINGS_MAGIC)             return false;
    if (h.image_size == 0)                        return false;
    if (h.image_size > LP_SETTINGS_MAX_BLOB_SIZE) return false;
    if (memcmp(h.schema, LP_SETTINGS_SCHEMA_TAG,
               sizeof(LP_SETTINGS_SCHEMA_TAG)) != 0) {
        return false;
    }

    uint32_t crc = crc32_update(0, p + LP_BANK_HDR_SIZE, h.image_size);
    if (crc != h.image_crc) return false;

    if (out) *out = h;
    return true;
}

/* Write blob + header into the given bank slot. Caller is responsible
 * for picking the inactive bank. */
static LpNssResult bank_write(int bank, const void *blob, size_t len,
                              uint32_t seq)
{
    if (len == 0 || len > LP_SETTINGS_MAX_BLOB_SIZE) return LP_NSS_ERR_SIZE;

    uint8_t *p = bank_storage(bank);

    LpBankHeader h;
    memset(&h, 0, sizeof(h));
    h.magic      = LP_SETTINGS_MAGIC;
    h.image_size = (uint32_t)len;
    h.image_crc  = crc32_update(0, (const uint8_t *)blob, len);
    h.sequence   = seq;
    memcpy(h.schema, LP_SETTINGS_SCHEMA_TAG,
           sizeof(LP_SETTINGS_SCHEMA_TAG));

    /* Write blob first, then header — same ordering the OSPI
     * implementation uses, so a torn write recovers identically. */
    memcpy(p + LP_BANK_HDR_SIZE, blob, len);
    memcpy(p, &h, LP_BANK_HDR_SIZE);

    /* Read-back verify (catches genuinely-broken DRAM, would catch
     * bus glitches on the OSPI variant). */
    LpBankHeader chk;
    if (!bank_is_valid(bank, &chk) || chk.image_crc != h.image_crc) {
        return LP_NSS_ERR_CRC;
    }
    return LP_NSS_OK;
}

/* ─── Public API ─────────────────────────────────────────────────────── */

void LpSettings_Init(void)
{
    LpBankHeader ha = {0}, hb = {0};
    bool va = bank_is_valid(0, &ha);
    bool vb = bank_is_valid(1, &hb);

    if (va && vb) {
        if (hb.sequence > ha.sequence) {
            s_active_bank = 1; s_sequence = hb.sequence;
        } else {
            s_active_bank = 0; s_sequence = ha.sequence;
        }
    } else if (va) { s_active_bank = 0; s_sequence = ha.sequence; }
    else if (vb)   { s_active_bank = 1; s_sequence = hb.sequence; }
    else           { s_active_bank = -1; s_sequence = 0; }

    /* DebugP_log here is intentionally suppressed: it executes BEFORE
     * vTaskStartScheduler() and BEFORE the bridge has begun draining
     * UART0. Calling it during early init produced firmware silence
     * on this board (semihosting / putchar blocked). Bridge sees the
     * Init result later via emitted SettingsBlob field 32 anyway. */
    (void)va; (void)vb;
}
int      LpSettings_GetActiveBank(void) { return s_active_bank; }
uint32_t LpSettings_GetSequence(void)   { return s_sequence; }

LpNssResult LpSettings_Load(void *buf, size_t buf_size, size_t *out_len)
{
    if (s_active_bank < 0) return LP_NSS_ERR_NO_BANK;

    LpBankHeader h;
    if (!bank_is_valid(s_active_bank, &h)) {
        /* Active bank silently went bad — try the other. */
        int alt = s_active_bank ^ 1;
        if (!bank_is_valid(alt, &h)) return LP_NSS_ERR_CRC;
        s_active_bank = alt;
        s_sequence    = h.sequence;
    }

    if (h.image_size > buf_size) return LP_NSS_ERR_SIZE;
    memcpy(buf, bank_storage(s_active_bank) + LP_BANK_HDR_SIZE,
           h.image_size);
    if (out_len) *out_len = h.image_size;
    return LP_NSS_OK;
}

LpNssResult LpSettings_Save(const void *blob, size_t len)
{
    /* Inactive bank — on first boot (active=-1) target A. */
    int target = (s_active_bank == 0) ? 1 : 0;
    uint32_t new_seq = s_sequence + 1;

    LpNssResult r = bank_write(target, blob, len, new_seq);
    if (r != LP_NSS_OK) return r;

    /* Flip in-RAM state only AFTER a successful write+verify, so a
     * mid-write fault leaves the previously-active bank intact. */
    s_active_bank = target;
    s_sequence    = new_seq;
    s_blob_dirty  = true;

    LPS_LOG("[LpSettings] Save bank=%c seq=%u len=%u\r\n",
               target == 0 ? 'A' : 'B',
               (unsigned)new_seq, (unsigned)len);
    return LP_NSS_OK;
}

LpNssResult LpSettings_Restore(const void *blob, size_t len, uint32_t seq)
{
    /* Bridge replay overwrites bank A with seq from the file. We
     * intentionally bypass the inactive-bank flip because a restore is
     * an authoritative "this is the world state at last save". */
    LpNssResult r = bank_write(0, blob, len, seq);
    if (r != LP_NSS_OK) {
        LPS_LOG("[LpSettings] Restore FAILED rc=%d len=%u\r\n",
                   (int)r, (unsigned)len);
        return r;
    }

    /* Invalidate bank B so a later spurious validation can't pick a
     * stale higher-seq blob from a previous session. */
    memset(s_bank_b, 0, sizeof(s_bank_b));

    s_active_bank = 0;
    s_sequence    = seq;
    s_blob_dirty  = false;   /* We just received THIS blob from disk;
                              * no need to bounce it straight back. */

    /* Hand the freshly-restored bytes to the apply callback so the
     * in-memory Settings struct reflects the persisted state before
     * any cadence emit fires. Skipped silently if no consumer
     * registered (keeps the store unit-testable in isolation). */
    if (s_apply_cb != NULL) {
        s_apply_cb((const uint8_t *)blob, len);
    }

    LPS_LOG("[LpSettings] Restored bank A seq=%u len=%u\r\n",
               (unsigned)seq, (unsigned)len);
    return LP_NSS_OK;
}
bool LpSettings_BlobDirty(void) { return s_blob_dirty; }

size_t LpSettings_TakeBlob(uint8_t *buf, size_t buf_size)
{
    if (!s_blob_dirty || s_active_bank < 0) return 0;

    LpBankHeader h;
    if (!bank_is_valid(s_active_bank, &h)) return 0;

    /* Wire format = header bytes followed by blob bytes. The bridge
     * stores this verbatim and POSTs it back in full on replay; the
     * Restore path strips header.image_size off the front to find the
     * blob payload. */
    size_t total = LP_BANK_HDR_SIZE + h.image_size;
    if (total > buf_size) return 0;

    const uint8_t *p = bank_storage(s_active_bank);
    memcpy(buf, p, total);

    s_blob_dirty = false;
    return total;
}

/* ─── Panel-default bank ─────────────────────────────────────────────── */

/* Validate a bank-formatted buffer (header || blob). Returns true if
 * the magic, schema tag, image_size bound, and CRC all check.
 * Mirrors bank_is_valid() but operates on a raw pointer instead of
 * the active-bank index, so it can validate the panel snapshot too. */
static bool bank_buf_is_valid(const uint8_t *buf, LpBankHeader *out)
{
    LpBankHeader h;
    memcpy(&h, buf, LP_BANK_HDR_SIZE);
    if (h.magic != LP_SETTINGS_MAGIC)             return false;
    if (h.image_size == 0)                        return false;
    if (h.image_size > LP_SETTINGS_MAX_BLOB_SIZE) return false;
    if (memcmp(h.schema, LP_SETTINGS_SCHEMA_TAG,
               sizeof(LP_SETTINGS_SCHEMA_TAG)) != 0) {
        return false;
    }
    uint32_t crc = crc32_update(0, buf + LP_BANK_HDR_SIZE, h.image_size);
    if (crc != h.image_crc) return false;
    if (out) *out = h;
    return true;
}

LpNssResult LpSettings_SavePanelDefaults(void)
{
    /* Snapshot the currently-active bank verbatim (header + blob).
     * If no active bank exists yet (first boot, both A/B blank), the
     * caller hasn't saved anything to snapshot — return an error so
     * the UI can surface it. */
    if (s_active_bank < 0) return LP_NSS_ERR_NO_BANK;

    LpBankHeader h;
    if (!bank_is_valid(s_active_bank, &h)) return LP_NSS_ERR_CRC;

    const uint8_t *src = bank_storage(s_active_bank);
    size_t total = LP_BANK_HDR_SIZE + h.image_size;
    if (total > LP_BANK_TOTAL_SIZE) return LP_NSS_ERR_SIZE;

    memcpy(s_panel_bank, src, total);
    s_panel_dirty = true;

    LPS_LOG("[LpSettings] Panel default saved: %u B (active seq=%u)\r\n",
            (unsigned)h.image_size, (unsigned)h.sequence);
    return LP_NSS_OK;
}

bool LpSettings_HasPanelDefaults(void)
{
    return bank_buf_is_valid(s_panel_bank, NULL);
}

LpNssResult LpSettings_LoadPanelDefaults(void *buf, size_t buf_size,
                                         size_t *out_len)
{
    LpBankHeader h;
    if (!bank_buf_is_valid(s_panel_bank, &h)) return LP_NSS_ERR_NO_BANK;
    if (h.image_size > buf_size)               return LP_NSS_ERR_SIZE;
    memcpy(buf, s_panel_bank + LP_BANK_HDR_SIZE, h.image_size);
    if (out_len) *out_len = h.image_size;
    return LP_NSS_OK;
}

bool LpSettings_PanelDirty(void) { return s_panel_dirty; }

size_t LpSettings_TakePanelBlob(uint8_t *buf, size_t buf_size)
{
    if (!s_panel_dirty) return 0;
    LpBankHeader h;
    if (!bank_buf_is_valid(s_panel_bank, &h)) {
        s_panel_dirty = false;   /* corrupt — drop the dirty flag so we
                                  * don't loop on the bridge cadence */
        return 0;
    }
    size_t total = LP_BANK_HDR_SIZE + h.image_size;
    if (total > buf_size) return 0;
    memcpy(buf, s_panel_bank, total);
    s_panel_dirty = false;
    return total;
}

LpNssResult LpSettings_RestorePanelDefaults(const void *blob, size_t len,
                                            uint32_t seq)
{
    if (len == 0 || len > LP_SETTINGS_MAX_BLOB_SIZE) return LP_NSS_ERR_SIZE;

    LpBankHeader h;
    memset(&h, 0, sizeof(h));
    h.magic      = LP_SETTINGS_MAGIC;
    h.image_size = (uint32_t)len;
    h.image_crc  = crc32_update(0, (const uint8_t *)blob, len);
    h.sequence   = seq;
    memcpy(h.schema, LP_SETTINGS_SCHEMA_TAG,
           sizeof(LP_SETTINGS_SCHEMA_TAG));

    memcpy(s_panel_bank + LP_BANK_HDR_SIZE, blob, len);
    memcpy(s_panel_bank, &h, LP_BANK_HDR_SIZE);

    LpBankHeader chk;
    if (!bank_buf_is_valid(s_panel_bank, &chk)) return LP_NSS_ERR_CRC;

    /* Don't mark dirty — bridge just gave us this snapshot, no need
     * to bounce it back. */
    s_panel_dirty = false;

    LPS_LOG("[LpSettings] Panel default restored from bridge: %u B seq=%u\r\n",
            (unsigned)len, (unsigned)seq);
    return LP_NSS_OK;
}
