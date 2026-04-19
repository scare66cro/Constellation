/*
 * nova_settings_store.h — Ping-pong settings persistence for Nova (AM2434)
 *
 * Mirrors the settings-vault pattern implemented in the ARM simulator
 * (constellation-ui/server/src/armSimulator.ts) so that code verified on
 * the emulator runs on real hardware unchanged.
 *
 * Layout inside FW_SETTINGS_OFFSET (0x600000, 2 MB):
 *
 *   +0x000000  Settings Bank A header  (4 KB sector, FwBankHeader)
 *   +0x001000  Settings Bank A data    (up to ~508 KB)
 *   +0x080000  Settings Bank B header  (4 KB sector, FwBankHeader)
 *   +0x081000  Settings Bank B data    (up to ~508 KB)
 *   +0x100000  Panel defaults header   (4 KB sector, FwBankHeader)
 *   +0x101000  Panel defaults data     (up to ~508 KB)
 *   +0x180000  Reserved                (~512 KB)
 *
 * Save flow (ping-pong):
 *   1. Serialize the current Settings blob into RAM
 *   2. Compute CRC-32 over the blob (same polynomial as nova_fw_update)
 *   3. Erase + write the blob into the *inactive* bank's data region
 *   4. Erase + write the *inactive* bank's header with:
 *        magic = FW_BANK_MAGIC, valid = 1, sequence = active_seq + 1,
 *        image_size = blob size, image_crc = CRC-32
 *   5. Flip active_bank to the one just written
 *
 * Load flow (boot):
 *   1. Read both bank headers
 *   2. Reject any bank whose magic != FW_BANK_MAGIC or valid != 1
 *   3. For valid banks, read the data region and verify CRC
 *   4. Pick the bank with the highest sequence that passes CRC
 *   5. Copy its blob into the runtime Settings struct
 *
 * Panel defaults follow the same header layout but occupy a single
 * dedicated region (no ping-pong — operator save is infrequent and
 * always overwrites in place with erase-then-write).
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */
#ifndef NOVA_SETTINGS_STORE_H
#define NOVA_SETTINGS_STORE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ─── Settings vault layout (relative to FW_SETTINGS_OFFSET) ──────────── */

#define NSS_BANK_A_HDR_REL    0x000000
#define NSS_BANK_A_DATA_REL   0x001000
#define NSS_BANK_B_HDR_REL    0x080000
#define NSS_BANK_B_DATA_REL   0x081000
#define NSS_DEFAULTS_HDR_REL  0x100000
#define NSS_DEFAULTS_DATA_REL 0x101000

/* Each bank's data region is capped to keep erase times bounded and to
 * catch runaway growth.  ~500 KB is >>10x current Settings struct size. */
#define NSS_MAX_BLOB_SIZE     (500 * 1024)

/* ─── Schema version ──────────────────────────────────────────────────────
 * Stored in FwBankHeader.version[] (string form "NSSv<N>").  Bumped any
 * time the Settings struct layout OR factory defaults change in a way
 * that should invalidate previously-saved blobs.  On boot, banks whose
 * version tag does NOT match this constant are rejected so Settings_Init
 * + SerialShift_Init defaults take over.
 *
 * Bump history:
 *   1 — initial Nova settings schema (Apr 2026, IO map: DO1=REDLIGHT,
 *       DO2=YELLOWLIGHT, DO3=FAN, DO4=DOORS, DO5=REFRIG, DO6=HEAT,
 *       DO7=HUMID_HEAD1, DO8=HUMID_PUMP1, DO9=CLIMACELL,
 *       DO10=REFRIG_STAGE1; cooling-available formula
 *       Start = RefTemp + Diff + Eff*WB).
 *   2 — operator-confirmed IO map (Apr 2026): DO1=REDLIGHT,
 *       DO2=YELLOWLIGHT, DO3=FAN(+greenlight parallel), DO4=CLIMACELL,
 *       DO5=HUMID_HEAD1, DO6=HUMID_PUMP1, DO7=REFRIG_STAGE1,
 *       DO8=HEAT, DO9=PULSEDOOR_OPEN, DO10=PULSEDOOR_CLOSE.
 *       EQ_REFRIGERATION and EQ_DOORS demoted to IO_UNDEFINED.
 */
#define NSS_SCHEMA_VERSION    2
#define NSS_SCHEMA_TAG        "NSSv2"

/* ─── Result codes ────────────────────────────────────────────────────── */

typedef enum {
    NSS_OK = 0,
    NSS_ERR_NO_VALID_BANK  = 1, /* Both banks bad/blank — use factory defaults */
    NSS_ERR_SIZE           = 2, /* Blob larger than NSS_MAX_BLOB_SIZE */
    NSS_ERR_FLASH          = 3, /* Underlying flash read/write/erase failed */
    NSS_ERR_CRC            = 4  /* CRC mismatch on read-back */
} NssResult;

/* ─── Public API ──────────────────────────────────────────────────────── */

/** Initialize: read both bank headers, select newer valid one.
 *  Must be called once at boot before Load/Save. */
void NovaSettings_Init(void);

/** Active bank as selected at init (0 = A, 1 = B).
 *  -1 if no valid bank exists yet (first boot). */
int  NovaSettings_GetActiveBank(void);

/** Current settings sequence (0 if no valid bank). */
uint32_t NovaSettings_GetSequence(void);

/** Load the active bank's blob into caller's buffer.
 *  out_len receives the stored size. CRC is verified. */
NssResult NovaSettings_Load(void *buf, size_t buf_size, size_t *out_len);

/** Save a blob to the *inactive* bank and flip active pointer.
 *  Erase → write data → erase → write header, then flip in RAM. */
NssResult NovaSettings_Save(const void *blob, size_t len);

/** Snapshot current active bank into the panel-defaults region. */
NssResult NovaSettings_SavePanelDefaults(const void *blob, size_t len);

/** Load panel defaults into caller's buffer. */
NssResult NovaSettings_LoadPanelDefaults(void *buf, size_t buf_size,
                                          size_t *out_len);

/** Is panel defaults region populated with a valid blob? */
bool NovaSettings_HasPanelDefaults(void);

#endif /* NOVA_SETTINGS_STORE_H */
