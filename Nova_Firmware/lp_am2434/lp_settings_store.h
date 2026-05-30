/*
 * lp_settings_store.h — Ping-pong settings persistence for LP-AM2434
 *
 * API mirror of Nova_Firmware/Platform/nova_settings_store.{h,c} so the
 * Phase E settings-save code can move to OSPI-backed Platform/ later
 * with zero call-site changes.  Storage backend is the only difference:
 *
 *   - Platform build → OSPI W25Q64JV via hal_flash + Flash_open()
 *   - LP build (today) → two MSRAM-resident banks + bridge-side file
 *                        replay on firmware-ready (see notes below)
 *
 * The MSRAM banks survive only across soft resets, NOT across a power
 * cycle. To get apparent power-cycle persistence:
 *
 *   1. After every successful LpSettings_Save(), the bridge_uart_task
 *      emits the active bank's blob over a new envelope (field 32,
 *      "settings_blob" — field 30 is already MSG_LOG_CHUNK). The
 *      bridge writes it verbatim to ~/.constellation/lp_settings.bin.
 *
 *   2. On firmware-ready (after the bridge sees DataLoadStatus), the
 *      bridge POSTs the file's contents back via envelope field 91
 *      ("settings_blob_restore"). The LP's RX handler pre-seeds bank A
 *      with that blob and bumps the in-RAM seq counter, so the next
 *      LpSettings_Save() correctly flips to bank B.
 *
 *   3. Subsequent in-session saves loop through (1) again. Steady
 *      state: bridge file always reflects the latest committed Settings.
 *
 * When OSPI lands in the LP's example.syscfg, drop this header in
 * favor of nova_settings_store.h and delete this file. The wire
 * envelopes (30/91) become unnecessary at that point but are harmless
 * to keep emitting (the bridge can ignore them).
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */
#ifndef LP_SETTINGS_STORE_H
#define LP_SETTINGS_STORE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ─── Schema versioning ────────────────────────────────────────────────
 * Bump LP_SETTINGS_SCHEMA_TAG whenever the Settings struct grows or
 * field semantics change in a way that should invalidate older blobs.
 * read_and_verify_header() rejects mismatched tags so a stale
 * bridge-side file can't pollute a newer firmware. */
#define LP_SETTINGS_SCHEMA_TAG     "LPSv1"

/* Maximum bytes a single bank can hold. Phase D struct is empty (just
 * the header is meaningful), Phase E will grow this. 4 KB is plenty
 * to hold every Settings field the UI exposes today. */
#define LP_SETTINGS_MAX_BLOB_SIZE  4096U

/* Magic for the bank header — distinct from FW_BANK_MAGIC so the
 * Platform OSPI code and this LP RAM stand-in cannot accidentally
 * cross-load each other's blobs. */
#define LP_SETTINGS_MAGIC          0x4C504E53U   /* "LPNS" */

typedef enum {
    LP_NSS_OK              = 0,
    LP_NSS_ERR_NO_BANK     = 1,  /* No valid bank (first boot / both stale) */
    LP_NSS_ERR_SIZE        = 2,  /* Blob exceeds LP_SETTINGS_MAX_BLOB_SIZE */
    LP_NSS_ERR_CRC         = 3,  /* CRC mismatch on read-back */
    LP_NSS_ERR_INTERNAL    = 4,  /* Should-never-happen (bad bank index, etc.) */
} LpNssResult;

/* ─── Public API (mirrors NovaSettings_*) ──────────────────────────── */

/** Initialize: scan banks A and B, pick the higher-seq valid one.
 *  Safe to call multiple times — re-validates on each call. */
void LpSettings_Init(void);

/** Active bank: 0=A, 1=B, -1=none yet (first boot, both blank). */
int  LpSettings_GetActiveBank(void);

/** Current monotonic sequence (0 if no valid bank). */
uint32_t LpSettings_GetSequence(void);

/** Read the active bank's blob into caller's buffer. */
LpNssResult LpSettings_Load(void *buf, size_t buf_size, size_t *out_len);

/** Write a blob to the inactive bank, then flip active. Returns LP_NSS_OK
 *  on success. After success, callers SHOULD trigger emission of the
 *  envelope-30 settings_blob frame so the bridge file stays in sync. */
LpNssResult LpSettings_Save(const void *blob, size_t len);

/** Pre-seed bank A from a bridge replay (envelope field 91 receipt).
 *  Bumps the in-RAM seq so the next Save flips correctly to bank B.
 *  Called from the LP's bridge_rx_callback on a field-91 sub-msg. */
LpNssResult LpSettings_Restore(const void *blob, size_t len, uint32_t seq);

/** Callback signature for "blob just written to a bank — apply it to
 *  the in-memory Settings struct." The store calls this from inside
 *  Restore() after the bank is committed. lp_settings.c registers
 *  itself via LpSettings_SetApplyCallback at boot. */
typedef void (*LpSettingsApplyFn)(const uint8_t *blob, size_t len);

/** Register the apply callback. May be NULL to detach. The store
 *  intentionally does not depend on lp_settings.c at link time so the
 *  store remains testable in isolation. */
void LpSettings_SetApplyCallback(LpSettingsApplyFn fn);

/** True when LpSettings_Save has produced a fresh blob since the last
 *  emit. The bridge_uart_task polls this and clears it after sending
 *  the envelope-30 frame. Decouples Save from TX so we never call
 *  NovaProto_SendRaw from inside an RX callback context. */
bool LpSettings_BlobDirty(void);

/** Read-and-clear of the dirty flag + copy of the active blob. Returns
 *  the byte length copied (0 if nothing to emit). buf MUST be at least
 *  LP_SETTINGS_MAX_BLOB_SIZE bytes. */
size_t LpSettings_TakeBlob(uint8_t *buf, size_t buf_size);

/* ─── Panel-default snapshot bank ──────────────────────────────────────
 * Separate in-memory bank holding the operator's saved commissioning
 * baseline. Driven by the Level 2 Save Settings page:
 *   "Save as Panel Default" → SystemCmd cmd_type=2 → SavePanelDefaults
 *   "Restore Panel Default" → SystemCmd cmd_type=3 → LoadPanelDefaults
 * Persisted across power cycles by mirroring to the bridge: emit on
 * envelope field 33 (LP→bridge), restore on envelope field 92
 * (bridge→LP) at firmware-ready. Same scheme as the active bank
 * (fields 32/91) but a separate file (lp_panel_defaults.bin). */

/** Snapshot the active bank into the panel-default bank. Idempotent
 *  if the active bank header hasn't changed. Marks the panel bank
 *  dirty so the bridge cadence emits the field-33 envelope. */
LpNssResult LpSettings_SavePanelDefaults(void);

/** True if the panel-default bank holds a valid blob (header magic +
 *  schema tag + CRC all check). */
bool LpSettings_HasPanelDefaults(void);

/** Read the panel-default blob into caller's buffer (no header — just
 *  the bytes that LpSettings_Deserialize accepts). */
LpNssResult LpSettings_LoadPanelDefaults(void *buf, size_t buf_size,
                                          size_t *out_len);

/** True when the panel bank was just (re)written and the bridge has
 *  not yet drained it via TakePanelBlob. */
bool LpSettings_PanelDirty(void);

/** Read-and-clear of the panel dirty flag + copy of the panel-bank
 *  bytes (header + blob, same format as TakeBlob). Returns 0 if
 *  nothing to emit. buf MUST be at least
 *  LP_SETTINGS_MAX_BLOB_SIZE + 24 bytes. */
size_t LpSettings_TakePanelBlob(uint8_t *buf, size_t buf_size);

/** Pre-seed the panel bank from a bridge replay (envelope field 92).
 *  Same wire format as field 91 active replay (LpBankHeader || blob).
 *  Does NOT touch the active bank or s_data. */
LpNssResult LpSettings_RestorePanelDefaults(const void *blob, size_t len,
                                             uint32_t seq);

#endif /* LP_SETTINGS_STORE_H */
