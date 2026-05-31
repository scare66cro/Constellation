/*
 * nova_fw_update.h — Firmware update manager for Nova (AM2434)
 *
 * Current OSPI flash layout (W25Q64JV / W25Q128JV, fw 0.A.208+):
 *   0x000000 - 0x05FFFF  TI stock sbl_ospi (~311 KB) — F2c custom
 *                        sbl_chooser will replace this
 *   0x080000 - 0x1FFFFF  Bank A firmware image (mcelf.hs_fs)
 *   0x300000 - 0x33FFFF  FwBootMeta + Bank A/B FwBankHeader (relocated
 *                        from 0x060000 in 0.A.208 to escape the SBL
 *                        footprint — see write_meta_block_atomic
 *                        comments for the 7-layer SBL-wipe saga)
 *   0x600000 - 0x61FFFF  lp_device_config ping-pong banks
 *   0x620000 - 0x7FFFFF  LpSettings vault (future; MSRAM today)
 *   0x900000 - 0xA7FFFF  Bank B firmware image (mcelf.hs_fs) — relocated
 *                        from 0x200000 in 0.A.102 to force 4-byte
 *                        addressing (variant-D probe)
 *   0xC00000 - 0xD7FFFF  Golden recovery image (factory-flashed,
 *                        write-once) — F2c fallback target
 *
 * The actual #define values below are authoritative; the layout
 * comment above is a quick-reference summary. Full design + rationale
 * in docs/lp-am2434-f2c-sbl-chooser-design.md §2.
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */
#ifndef NOVA_FW_UPDATE_H
#define NOVA_FW_UPDATE_H

#include <stdint.h>
#include <stdbool.h>

/* ─── Flash offsets (within OSPI, byte addresses) ─────────────────── */
/*
 * Canonical OSPI map (W25Q64JV, 8 MB) — finalised May 2026 alongside
 * the F2c stage-2 SBL chooser. Full design + migration plan in
 * docs/lp-am2434-f2c-sbl-chooser-design.md §2.
 *
 *   0x000000-0x05FFFF (384 KB)  custom sbl_chooser placeholder; today
 *                                TI's stock sbl_ospi (~311 KB) lives
 *                                here, its tail extending to ~0x4BE2D
 *   0x080000-0x1FFFFF (1.5 MB)  Bank A app image (mcelf.hs_fs) — also
 *                                where TI's stock SBL hardloads, so
 *                                first F2c flash needs no app move
 *   0x180000-0x1FFFFF ( 512 KB) Watchdog R5FSS1_0 mcelf (overlaps end
 *                                of Bank A footprint by design — app
 *                                images stay well under 1 MB)
 *   0x200000-0x2FFFFF (1.0 MB)  Bank B app image (NOTE: moved to
 *                                0x900000 in 0.A.102 — see below)
 *   0x300000-0x33FFFF (256 KB)  FwBootMeta + Bank A/B FwBankHeader
 *                                (0.A.208: relocated from 0x060000
 *                                to escape the SBL footprint; the
 *                                old location wiped SBL tail every
 *                                metadata update — see write_meta_-
 *                                block_atomic comments)
 *   0x340000-0x4FFFFF (1.75 MB) Reserved / unused
 *   0x500000-0x5FFFFF ( 1   MB) RESERVED
 *   0x600000-0x61FFFF (128 KB)  lp_device_config ping-pong banks
 *   0x620000-0x7FFFFF (~2 MB)   LpSettings vault (future, MSRAM today)
 *   0x900000-0xA7FFFF (1.5 MB)  Bank B app image (0.A.102 relocation)
 */
#define FW_HEADER_OFFSET      0x300000U  /* Boot meta + bank A header
                                          * (0.A.208: was 0x060000, see
                                          * write_meta_block_atomic) */
#define FW_HEADER_SIZE        0x010000U  /* 64 KB */
#define FW_BANK_B_HDR_SECTOR  0x310000U  /* Bank B header sector
                                          * (0.A.208: was 0x070000) */
#define FW_BANK_A_OFFSET      0x080000U
/* 0.A.102: FW_BANK_B moved to 0x900000 (9 MB into the 64 MB chip).
 * 0x200000 failed (0.A.78-82). 0x400000 failed (0.A.83+). Both inside
 * the 16 MB 3-byte address region. Variant-D probe wrote successfully
 * at 0x700000. Auto-flasher writes at 0x80000 (bank A) successfully.
 * Trying 0x900000 — past the 8 MB mark, well into 4-byte-only region.
 * Forces 4-byte addressing path explicitly, as a different test
 * vector from prior failed attempts. */
#define FW_BANK_B_OFFSET      0x900000U
#define FW_GOLDEN_OFFSET      0xC00000U
#define FW_SETTINGS_OFFSET    0x620000U  /* future use; MSRAM today */
#define FW_BANK_MAX_SIZE      0x180000U  /* 1.5 MB per bank */
#define FW_SECTOR_SIZE        4096U

/* ─── Bank header (per-bank, in its own erase sector) ────────────── */
/* 128-byte FwBankHeader. Bank A's lives at FW_HEADER_OFFSET + 0x80
 * (the first 128 B at FW_HEADER_OFFSET hold FwBootMeta). Bank B's
 * lives in its own sector at FW_BANK_B_HDR_SECTOR so updating one
 * bank's metadata cannot accidentally erase the other's. The 64 KB
 * sector around each header is otherwise reserved for future per-
 * bank metadata (signed manifest, dependency graph, etc.). */

#define FW_BANK_MAGIC         0x4E4F5641U  /* "NOVA" */
#define FW_BOOT_META_OFFSET   0x000000U   /* in FW_HEADER_OFFSET sector, first 128 B */
#define FW_BANK_A_HDR_OFFSET  0x000080U   /* in FW_HEADER_OFFSET sector, +128 B */
#define FW_BANK_B_HDR_OFFSET  0x000000U   /* in FW_BANK_B_HDR_SECTOR sector, first 128 B */

typedef struct __attribute__((packed)) {
    uint32_t magic;           /* Must be FW_BANK_MAGIC */
    uint32_t image_size;      /* Size of firmware image in bytes */
    uint32_t image_crc;       /* CRC-32 of the image data */
    uint32_t valid;           /* 1 = image verified OK, 0 = not valid */
    uint32_t active;          /* 1 = this is the active bank */
    uint32_t sequence;        /* Monotonic: higher = newer */
    char     version[32];     /* Version string, null-terminated */
    uint8_t  reserved[80];    /* Total struct: 24 + 32 + 80 = 136 bytes
                               * (the pre-2026-05-30 comment claimed
                               * "Pad to 128" but the math doesn't add
                               * up — actual on-flash record is 136
                               * bytes; the F2c sbl_bank_select port
                               * caught this via _Static_assert) */
} FwBankHeader;

typedef struct __attribute__((packed)) {
    uint32_t boot_count;      /* Total boot count */
    uint32_t boot_reason;     /* 0=normal, 1=watchdog, 2=fallback */
    uint32_t watchdog_strikes; /* Consecutive failed boots */
    uint8_t  reserved[116];   /* Pad to 128 bytes */
} FwBootMeta;

/* ─── Update states ───────────────────────────────────────────────────── */

typedef enum {
    FW_STATE_IDLE      = 0,
    FW_STATE_ERASING   = 1,
    FW_STATE_RECEIVING = 2,
    FW_STATE_VERIFYING = 3,
    FW_STATE_VERIFIED  = 4,
    FW_STATE_ACTIVATING = 5,
    FW_STATE_ERROR     = 6
} FwUpdateState;

/* ─── Public API ──────────────────────────────────────────────────────── */

/** Initialize: read bank headers, determine active bank */
void NovaFwUpdate_Init(void);

/** Get the currently active bank (0=A, 1=B) */
uint32_t NovaFwUpdate_GetActiveBank(void);

/** Get the inactive bank (opposite of active) */
uint32_t NovaFwUpdate_GetInactiveBank(void);

/** Get the current update state */
FwUpdateState NovaFwUpdate_GetState(void);

/** Handle FwBeginUpdate command from bridge.
 *  Erases inactive bank, prepares for receiving chunks.
 *  Returns 0 on success, error code on failure. */
uint32_t NovaFwUpdate_Begin(uint32_t total_size, uint32_t crc32,
                             const char *version, uint32_t chunk_size);

/** Handle FwDataChunk: write one chunk to the inactive bank.
 *  Returns 0 on success, error code on failure. */
uint32_t NovaFwUpdate_WriteChunk(uint32_t offset, const uint8_t *data,
                                  uint32_t len, uint32_t chunk_crc);

/** Handle FwFinalizeUpdate: verify full-image CRC.
 *  Returns 0 on success, error code on failure. */
uint32_t NovaFwUpdate_Finalize(uint32_t expected_crc);

/** Handle FwActivateBank: mark the just-written bank as active.
 *  If reboot=true, triggers a system reset.
 *  Returns 0 on success. */
uint32_t NovaFwUpdate_Activate(bool reboot);

/** Abort an in-progress update, return to IDLE */
void NovaFwUpdate_Abort(void);

/** Get bytes written so far (for progress reporting) */
uint32_t NovaFwUpdate_GetBytesWritten(void);

/** Get total expected size */
uint32_t NovaFwUpdate_GetTotalSize(void);

/** Read bank header for a given bank (0=A, 1=B) */
void NovaFwUpdate_GetBankHeader(uint32_t bank, FwBankHeader *hdr);

/** Read boot metadata */
void NovaFwUpdate_GetBootMeta(FwBootMeta *meta);

/** Validate a bank's image (read + CRC check). Returns true if valid. */
bool NovaFwUpdate_ValidateBank(uint32_t bank);

/** Called by bootloader: increment boot count, check watchdog strikes */
void NovaFwUpdate_BootValidation(void);

/** Called by application after successful boot (clears watchdog strikes) */
void NovaFwUpdate_ConfirmBoot(void);

/** Orbit-OTA path metadata-write helper. After lp_ota_task.c has
 *  written + CRC-verified the new image at Bank B (OSPI 0x900000),
 *  call this to update the FwBankHeader at OSPI 0x310000 + the
 *  Bank A header's active bit + FwBootMeta strikes so the F2c
 *  chooser picks Bank B on next boot.
 *
 *  - Writes Bank B FwBankHeader with magic=NOVA, valid=1, active=1,
 *    sequence = max(A.seq, B.seq) + 1, the supplied image_size /
 *    image_crc / version.
 *  - Flips s_bank_a_hdr.active = 0.
 *  - Sets s_boot_meta.watchdog_strikes = 0 (fresh strike budget
 *    for the new image).
 *  - Calls write_meta_block_atomic to persist all three to OSPI.
 *
 *  Does NOT touch image data at 0x900000 or 0x080000 — caller has
 *  already written those via the OTA chunk path.
 *
 *  Does NOT initiate a SoC warm reset — caller (lp_ota_task.c
 *  FwActivate handler) does that after this returns and after the
 *  optional legacy stage-copy.
 *
 *  Returns 0 on success.  */
uint32_t NovaFwUpdate_OrbitFinalize(uint32_t image_size,
                                    uint32_t image_crc,
                                    const char *version);

/* ─── Firmware Image Signing ──────────────────────────────────────────── */
/*
 * Images must include a 256-byte signature trailer:
 *   [firmware payload][256-byte ECDSA-P256 signature]
 *
 * The signature is over SHA-256(payload).
 * The public key is embedded in the golden recovery image and cannot
 * be updated — only the corresponding private key (held by Agristar
 * build server) can produce valid signatures.
 *
 * Verification happens during Finalize, before marking the bank as valid.
 * If verification fails, the bank is NOT marked valid and Activate will
 * refuse to switch to it.
 */

#define FW_SIGNATURE_SIZE       256   /* ECDSA P-256 DER signature max */
#define FW_PUBKEY_SIZE          64    /* Uncompressed P-256 public key (x || y) */

/** Set the signing public key (called at boot from golden image) */
void NovaFwUpdate_SetSigningKey(const uint8_t pubkey[FW_PUBKEY_SIZE]);

/** Verify the signature of a bank's image. Returns true if valid.
 *  The last FW_SIGNATURE_SIZE bytes of the image are the signature. */
bool NovaFwUpdate_VerifySignature(uint32_t bank);

#endif /* NOVA_FW_UPDATE_H */
