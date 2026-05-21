/*
 * lp_device_config.c — Per-board provisioning record (OSPI-backed)
 *
 * Two-bank ping-pong on the LP-AM2434's OSPI NOR flash (W25Q64JV,
 * memory-mapped at 0x60000000 by the SBL DAC config).  See the header
 * for the bank layout and offset rationale.
 *
 * Boot precedence:
 *   1. Higher-sequence bank with valid CRC32 → use it (production path)
 *   2. Other bank if it validates              → use it (recovery path)
 *   3. Compile-time defaults (CONFIG_NOVA_LP_*) → last-resort fallback
 *      so a bench unit with no flash, or a brand-new board never
 *      provisioned, still boots and reaches the network.
 *
 * Reads use the memory-mapped DAC window (cheap, no driver call).
 * Writes/erases use the TI MCU+ SDK `Flash_*` driver, which switches
 * OSPI into indirect mode for the program/erase command and back to
 * DAC mode afterwards.  We never touch the QSPI peripheral registers
 * directly — per project rule, no raw QSPI driver.
 *
 * Failure handling:
 *   - Flash_open() returns NULL on a unit without OSPI populated:
 *     LpDeviceConfig_Init falls through to compile-time defaults and
 *     LpDeviceConfig_Save returns false.  Boot proceeds normally.
 *   - Save validates the read-back via DAC mode after Flash_write so
 *     a silent flash failure can't be masked.
 *
 * Copyright (c) 2026 Agristar
 * SPDX-License-Identifier: MIT
 */

#include "lp_device_config.h"
#include "ti_drivers_open_close.h"
#include "ti_board_open_close.h"
#include "hal.h"               /* hal_flash_write_dac — see bank_program */
#include <board/flash.h>
#include <kernel/dpl/DebugP.h>
#include <kernel/dpl/SystemP.h>
#include <FreeRTOS.h>
#include <task.h>
#include <string.h>
#include <stdio.h>

/* SDK populates this array of Flash handles in Board_driversOpen().
 * gFlashHandle[CONFIG_FLASH0] is non-NULL once the OSPI controller
 * has handshaken with the W25Q64JV. Same access pattern flasher_uart
 * uses for the auto-flasher firmware. */
extern Flash_Handle gFlashHandle[];

/* ─── OSPI memory-mapped (DAC) window — populated by SBL ─────────────── */
#define OSPI_DAC_BASE   0x60000000U

/* Flash sector erase granularity (W25Q64JV is uniform 4 KiB). */
#define FLASH_SECTOR_SIZE   0x1000U

/* ─── Compile-time bootstrap defaults (last-resort only) ─────────────── */
#ifndef CONFIG_NOVA_LP_ROLE
#define CONFIG_NOVA_LP_ROLE  ORBIT_ROLE_CONTROLLER
#endif
#ifndef CONFIG_NOVA_LP_IP
#define CONFIG_NOVA_LP_IP    0U
#endif
#ifndef CONFIG_NOVA_LP_NETMASK
#define CONFIG_NOVA_LP_NETMASK 0U
#endif
#ifndef CONFIG_NOVA_LP_GATEWAY
#define CONFIG_NOVA_LP_GATEWAY 0U
#endif
#ifndef CONFIG_NOVA_LP_BOARD_ID
#define CONFIG_NOVA_LP_BOARD_ID 0U
#endif

/* ─── On-flash bank layout (matches header doc-block) ────────────────── */
typedef struct __attribute__((packed)) {
    uint32_t       bank_magic;     /* LP_DEVCFG_BANK_MAGIC */
    uint32_t       sequence;       /* monotonic across saves, higher wins */
    uint32_t       payload_size;   /* must equal sizeof(LpDeviceConfig) */
    LpDeviceConfig payload;
    uint32_t       crc32;          /* over [0 .. offsetof(crc32)) */
} LpDevCfgBank;

/* Lock the on-flash record size so future struct edits cannot silently
 * shift the CRC offset between firmware revisions on the same board. */
_Static_assert(sizeof(LpDeviceConfig) == 16U * sizeof(uint32_t),
               "LpDeviceConfig size changed — bump LP_DEVCFG_VERSION and "
               "audit the CRC layout before flashing onto deployed boards");
_Static_assert(sizeof(LpDevCfgBank) <= LP_DEVCFG_BANK_SIZE,
               "Bank record exceeds reserved 64 KiB OSPI sector");

/* ─── Module state ───────────────────────────────────────────────────── */
static LpDeviceConfig s_cfg;
static bool           s_initialized = false;
static int            s_active_bank = -1;   /* 0=A, 1=B, -1=defaults */
static uint32_t       s_sequence    = 0;
static Flash_Handle   s_flash       = NULL; /* NULL → write path disabled */

/* Quiet logging until FreeRTOS is up — DebugP_log called pre-scheduler
 * stalls UART0 on this board (see lp_settings_store.c for full
 * background). */
#define LPDC_LOG(...) do { \
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) { \
        DebugP_log(__VA_ARGS__); \
    } \
} while (0)

/* ─── CRC-32 (Ethernet/zlib polynomial) ──────────────────────────────── *
 * Same polynomial as Platform/nova_settings_store.c and
 * lp_settings_store.c — keep them aligned so a single helper script
 * can verify any of them. */
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

/* ─── Bank helpers ───────────────────────────────────────────────────── */

static uint32_t bank_offset(int bank) {
    return (bank == 0) ? LP_DEVCFG_BANK_A_OFF : LP_DEVCFG_BANK_B_OFF;
}

/* Read into a local copy. Uses Flash_read() (indirect XIP read via the
 * OSPI controller) rather than memcpy() from the DAC window — the
 * SysConfig-generated OSPI driver opens with `dacEnable = FALSE`, so
 * accessing OSPI_DAC_BASE directly produces a data abort. Indirect
 * reads work regardless of DAC state. */
static void bank_read(int bank, LpDevCfgBank *out)
{
    if (s_flash == NULL) {
        memset(out, 0xFF, sizeof(*out));   /* signal "blank" */
        return;
    }
    if (Flash_read(s_flash, bank_offset(bank),
                   (uint8_t *)out, sizeof(*out)) != SystemP_SUCCESS) {
        memset(out, 0xFF, sizeof(*out));
    }
}

/* Read a bank record from an explicit byte offset — used by the
 * one-shot legacy-bank migration in LpDeviceConfig_Init. Same Flash_read
 * mechanics as bank_read(); the offset is just not bound to the canonical
 * BANK_A/B offsets. */
static void bank_read_at(uint32_t off, LpDevCfgBank *out)
{
    if (s_flash == NULL) {
        memset(out, 0xFF, sizeof(*out));
        return;
    }
    if (Flash_read(s_flash, off,
                   (uint8_t *)out, sizeof(*out)) != SystemP_SUCCESS) {
        memset(out, 0xFF, sizeof(*out));
    }
}

static bool bank_validate(const LpDevCfgBank *b)
{
    if (b->bank_magic != LP_DEVCFG_BANK_MAGIC)         return false;
    if (b->payload_size != sizeof(LpDeviceConfig))     return false;
    if (b->payload.magic != LP_DEVCFG_MAGIC)           return false;
    /* CRC covers everything up to (but not including) the crc32 field. */
    const size_t crc_span = offsetof(LpDevCfgBank, crc32);
    uint32_t calc = crc32_update(0, (const uint8_t *)b, crc_span);
    return calc == b->crc32;
}

/* Erase the 16 × 4 KiB sectors covering one bank. Returns 0 on success. */
static int bank_erase(int bank)
{
    if (s_flash == NULL) return -1;
    uint32_t off = bank_offset(bank);
    for (uint32_t s = 0; s < LP_DEVCFG_BANK_SIZE; s += FLASH_SECTOR_SIZE) {
        uint32_t blk_num = 0, page_num = 0;
        Flash_offsetToBlkPage(s_flash, off + s, &blk_num, &page_num);
        int32_t est = Flash_eraseBlk(s_flash, blk_num);
        /* Pre-scheduler diagnostic — log every Flash_eraseBlk return.
         * On Cypress S25HL512T in 4S-4D-4D mode, sector-erase opcode is
         * silently mapped to block-erase by the controller; all 16
         * iterations target the same 256 KB block, but the SDK call
         * should still return SUCCESS each time. */
        if (s == 0 || est != SystemP_SUCCESS) {
            extern void bb_uart0_puts(const char *s);
            char buf[140];
            snprintf(buf, sizeof(buf),
                "[DevCfg-Erase] iter=%u off=0x%06X blk=%u rc=%ld\r\n",
                (unsigned)(s / FLASH_SECTOR_SIZE),
                (unsigned)(off + s), (unsigned)blk_num, (long)est);
            bb_uart0_puts(buf);
        }
        if (est != SystemP_SUCCESS) {
            LPDC_LOG("[DevCfg] erase fail off=0x%06X blk=%u\r\n",
                     (unsigned)(off + s), (unsigned)blk_num);
            return -1;
        }
    }
    return 0;
}

/* Write a fully-populated bank record to OSPI. Bank must already be
 * erased (caller's responsibility). The ~80-byte record fits in a
 * single 256-byte program-page so this is one PP command.
 *
 * 2026-05-21 layer-4 fix: SDK `Flash_write` returns -1 for an 80-byte
 * write at `0x600000` in the pre-scheduler boot context (same root
 * issue documented in `memories/repo/lp-am2434-ota-dac-mode-fix.md`
 * for the OTA path — SDK `Flash_norOspiWrite` INDIRECT_WRITE_XFER is
 * broken in this firmware's runtime/init context). Switched to
 * `hal_flash_write_dac`, which the OTA path proved works for OSPI
 * writes in both pre-scheduler and post-scheduler contexts. The helper
 * requires 256-byte page-aligned addr+len, so we pad the 80-byte
 * record to a full page with 0xFF (erased state — programmable later
 * if anyone ever wants to expand `LpDevCfgBank`). */
static int bank_program(int bank, const LpDevCfgBank *b)
{
    if (s_flash == NULL) return -1;

    /* Pad to a full 256-byte PP page so hal_flash_write_dac accepts it. */
    uint8_t page[256];
    memset(page, 0xFF, sizeof(page));
    memcpy(page, b, sizeof(*b));   /* sizeof(LpDevCfgBank) is 80 << 256 */

    int32_t st = hal_flash_write_dac(bank_offset(bank), page, sizeof(page));
    /* Pre-scheduler diagnostic — write return code. */
    {
        extern void bb_uart0_puts(const char *s);
        char buf[160];
        snprintf(buf, sizeof(buf),
            "[DevCfg-Program] hal_flash_write_dac bank=%c addr=0x%06X len=%u rc=%ld (rec=%u padded to page)\r\n",
            bank == 0 ? 'A' : 'B', (unsigned)bank_offset(bank),
            (unsigned)sizeof(page), (long)st, (unsigned)sizeof(*b));
        bb_uart0_puts(buf);
    }
    if (st != SystemP_SUCCESS) {
        LPDC_LOG("[DevCfg] write fail bank=%c st=%ld\r\n",
                 bank == 0 ? 'A' : 'B', (long)st);
        return -1;
    }
    /* Read-back verify via DAC window — proves the indirect write
     * landed and the OSPI controller switched back to DAC cleanly. */
    LpDevCfgBank chk;
    bank_read(bank, &chk);
    int cmp = memcmp(&chk, b, sizeof(*b));
    /* Pre-scheduler diagnostic — verify result + first 32 bytes of
     * what we read back. If memcmp != 0, knowing exactly which bytes
     * came back wrong tells us whether the write landed partially
     * (most-likely bug) or not at all (read-path issue). */
    {
        extern void bb_uart0_puts(const char *s);
        char buf[200];
        const uint8_t *cp = (const uint8_t *)&chk;
        snprintf(buf, sizeof(buf),
            "[DevCfg-Program] verify bank=%c memcmp=%d  readback[0..15]=%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\r\n",
            bank == 0 ? 'A' : 'B', cmp,
            cp[0],cp[1],cp[2],cp[3],cp[4],cp[5],cp[6],cp[7],
            cp[8],cp[9],cp[10],cp[11],cp[12],cp[13],cp[14],cp[15]);
        bb_uart0_puts(buf);
    }
    if (cmp != 0) {
        LPDC_LOG("[DevCfg] verify mismatch bank=%c\r\n",
                 bank == 0 ? 'A' : 'B');
        return -1;
    }
    return 0;
}

static void apply_compile_time_defaults(void)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.magic    = LP_DEVCFG_MAGIC;
    s_cfg.version  = LP_DEVCFG_VERSION;
    s_cfg.role     = (uint32_t)CONFIG_NOVA_LP_ROLE;
    s_cfg.ip       = (uint32_t)CONFIG_NOVA_LP_IP;
    s_cfg.netmask  = (uint32_t)CONFIG_NOVA_LP_NETMASK;
    s_cfg.gateway  = (uint32_t)CONFIG_NOVA_LP_GATEWAY;
    s_cfg.board_id = (uint32_t)CONFIG_NOVA_LP_BOARD_ID;
    s_active_bank  = -1;
    s_sequence     = 0;
}

/* ─── Public API ─────────────────────────────────────────────────────── */

void LpDeviceConfig_Init(void)
{
    if (s_initialized) return;
    s_initialized = true;

    /* Pick up the Board_driversOpen-managed handle. NULL on a unit with
     * no OSPI installed or where Board_driversOpen failed earlier — log
     * and continue with compile-time defaults. Boot must not block. */
    s_flash = gFlashHandle[CONFIG_FLASH0];
    if (s_flash == NULL) {
        LPDC_LOG("[DevCfg] Flash handle NULL — using compile-time defaults\r\n");
        apply_compile_time_defaults();
        return;
    }

    /* Read both banks via DAC mode and pick the higher-seq valid one. */
    LpDevCfgBank a, b;
    bank_read(0, &a);
    bank_read(1, &b);
    bool va = bank_validate(&a);
    bool vb = bank_validate(&b);

    /* 0.A.190 layer-3 diagnostic: pre-scheduler hex dump of the first
     * 32 bytes of each bank record (covers magic + seq + payload_size +
     * payload.magic + payload.version + payload.role + payload.ip — the
     * fields that disambiguate "valid STORAGE record" vs "blank" vs
     * "corrupt"). Same bb_uart0_puts+snprintf pattern as main.c:2051-2060.
     * Validate result is appended so we can tell at a glance whether
     * the read+CRC check passed. Remove once layer 3 is rooted. */
    {
        extern void bb_uart0_puts(const char *s);
        char buf[160];
        const uint8_t *ap = (const uint8_t *)&a;
        const uint8_t *bp = (const uint8_t *)&b;
        snprintf(buf, sizeof(buf),
            "[DevCfg-Diag] BankA @0x600000 valid=%d  "
            "%02X %02X %02X %02X  %02X %02X %02X %02X  %02X %02X %02X %02X  "
            "%02X %02X %02X %02X  %02X %02X %02X %02X  %02X %02X %02X %02X  "
            "%02X %02X %02X %02X  %02X %02X %02X %02X\r\n",
            (int)va,
            ap[0],ap[1],ap[2],ap[3],   ap[4],ap[5],ap[6],ap[7],
            ap[8],ap[9],ap[10],ap[11], ap[12],ap[13],ap[14],ap[15],
            ap[16],ap[17],ap[18],ap[19], ap[20],ap[21],ap[22],ap[23],
            ap[24],ap[25],ap[26],ap[27], ap[28],ap[29],ap[30],ap[31]);
        bb_uart0_puts(buf);
        snprintf(buf, sizeof(buf),
            "[DevCfg-Diag] BankB @0x610000 valid=%d  "
            "%02X %02X %02X %02X  %02X %02X %02X %02X  %02X %02X %02X %02X  "
            "%02X %02X %02X %02X  %02X %02X %02X %02X  %02X %02X %02X %02X  "
            "%02X %02X %02X %02X  %02X %02X %02X %02X\r\n",
            (int)vb,
            bp[0],bp[1],bp[2],bp[3],   bp[4],bp[5],bp[6],bp[7],
            bp[8],bp[9],bp[10],bp[11], bp[12],bp[13],bp[14],bp[15],
            bp[16],bp[17],bp[18],bp[19], bp[20],bp[21],bp[22],bp[23],
            bp[24],bp[25],bp[26],bp[27], bp[28],bp[29],bp[30],bp[31]);
        bb_uart0_puts(buf);
    }

    const LpDevCfgBank *winner = NULL;
    if (va && vb)       { winner = (b.sequence > a.sequence) ? &b : &a;
                          s_active_bank = (winner == &b) ? 1 : 0; }
    else if (va)        { winner = &a; s_active_bank = 0; }
    else if (vb)        { winner = &b; s_active_bank = 1; }

    if (winner != NULL) {
        s_cfg      = winner->payload;
        s_sequence = winner->sequence;
        LPDC_LOG("[DevCfg] loaded bank=%c seq=%u role=%u ip=0x%08X\r\n",
                 s_active_bank == 0 ? 'A' : 'B',
                 (unsigned)s_sequence, (unsigned)s_cfg.role,
                 (unsigned)s_cfg.ip);
    } else {
        /* ─── Legacy-bank migration (pre-2026-05 offsets) ───────────────
         * Both new banks blank → this might be a board that was
         * provisioned under the old 0x200000/0x210000 layout. Scan
         * those offsets once; if a valid record is found, copy it
         * forward to the new banks via LpDeviceConfig_Save. The legacy
         * sectors are intentionally NOT erased here — the next OTA
         * push to firmware Bank B (0x200000-0x3FFFFF) will erase them
         * as part of normal flash programming, and leaving them intact
         * preserves recovery if this migration save fails partway. */
        LpDevCfgBank la, lb;
        bank_read_at(LP_DEVCFG_LEGACY_BANK_A_OFF, &la);
        bank_read_at(LP_DEVCFG_LEGACY_BANK_B_OFF, &lb);
        bool vla = bank_validate(&la);
        bool vlb = bank_validate(&lb);
        const LpDevCfgBank *legacy = NULL;
        if (vla && vlb)  legacy = (lb.sequence > la.sequence) ? &lb : &la;
        else if (vla)    legacy = &la;
        else if (vlb)    legacy = &lb;

        if (legacy != NULL) {
            LPDC_LOG("[DevCfg] legacy bank found seq=%u role=%u ip=0x%08X — migrating forward\r\n",
                     (unsigned)legacy->sequence,
                     (unsigned)legacy->payload.role,
                     (unsigned)legacy->payload.ip);
            /* Seed runtime state from the legacy record so this boot
             * comes up at the correct role/IP regardless of whether
             * the migration save succeeds. */
            s_cfg         = legacy->payload;
            s_sequence    = legacy->sequence;
            s_active_bank = -1;        /* force first new save → bank A */
            /* Persist forward. Save is best-effort; if it fails we
             * still booted with correct config from the RAM cache, and
             * we'll retry on the next boot. */
            LpDeviceConfig migrated = s_cfg;
            migrated.magic = LP_DEVCFG_MAGIC;
            (void)LpDeviceConfig_Save(&migrated);
        } else {
            LPDC_LOG("[DevCfg] no valid bank (A=%s B=%s legacyA=%s legacyB=%s) — compile-time defaults\r\n",
                     va  ? "ok" : "bad", vb  ? "ok" : "bad",
                     vla ? "ok" : "bad", vlb ? "ok" : "bad");
            apply_compile_time_defaults();
        }
    }

    /* Factory-provisioning override:
     *   When the build sets a non-zero CONFIG_NOVA_LP_IP and either
     *   (a) no valid stored bank was found (first-time provisioning of
     *       a blank board — `s_active_bank < 0`) OR
     *   (b) the stored bank holds a different IP than the build's
     *       compile-time value,
     *   the build-time value wins AND is persisted to OSPI. This makes
     *   `Flash-LP.ps1 -Ip 10.1.2.X` behave as "reprovision this board
     *   to .X" — operator no longer needs a separate erase or
     *   settings-save step. Operator-driven IP changes (via
     *   LpDeviceConfig_Save from the bridge) are unaffected because
     *   they go out in a build that does NOT redefine CONFIG_NOVA_LP_IP.
     *
     *   2026-05-21 layer-3 fix: the `s_active_bank < 0` clause is the
     *   missing path. Previously the override only fired when a
     *   pre-existing record had a different IP — which meant a
     *   fresh board (banks blank) had `apply_compile_time_defaults`
     *   set `s_cfg.ip = CONFIG_NOVA_LP_IP`, then this `!=` check saw
     *   them equal and skipped the save. OSPI stayed blank forever.
     *   Every probe via :5503 returned the correct STORAGE record
     *   from the RAM cache, masking the persistence failure. The OTA
     *   reboot then booted a universal binary against blank OSPI and
     *   fell back to its own compile-time defaults (CONTROLLER + ip=0).
     *   See memories/repo/ota-bench-2026-05-20-three-layers.md and
     *   memories/repo/devcfg-blank-banks-override-never-fired.md. */
#if defined(CONFIG_NOVA_LP_IP) && (CONFIG_NOVA_LP_IP != 0U)
    if (s_active_bank < 0 || s_cfg.ip != (uint32_t)CONFIG_NOVA_LP_IP) {
        /* Pre-scheduler diagnostic — confirms the override fired this
         * boot. Pair with the [DevCfg-Diag] dump above to see "bank
         * was blank → save now firing" cause-and-effect. */
        {
            extern void bb_uart0_puts(const char *s);
            char obuf[140];
            snprintf(obuf, sizeof(obuf),
                "[DevCfg-Override] firing  s_active_bank=%d  stored.ip=0x%08X  "
                "CONFIG_NOVA_LP_IP=0x%08X  CONFIG_NOVA_LP_ROLE=%u  -> SAVING\r\n",
                s_active_bank, (unsigned)s_cfg.ip,
                (unsigned)CONFIG_NOVA_LP_IP, (unsigned)CONFIG_NOVA_LP_ROLE);
            bb_uart0_puts(obuf);
        }
        LPDC_LOG("[DevCfg] build override: stored ip=0x%08X != CONFIG=0x%08X — re-saving\r\n",
                 (unsigned)s_cfg.ip, (unsigned)CONFIG_NOVA_LP_IP);
        LpDeviceConfig new_cfg = s_cfg;
        new_cfg.magic = LP_DEVCFG_MAGIC;
        new_cfg.ip    = (uint32_t)CONFIG_NOVA_LP_IP;
#ifdef CONFIG_NOVA_LP_ROLE
        new_cfg.role  = (uint32_t)CONFIG_NOVA_LP_ROLE;
#endif
#ifdef CONFIG_NOVA_LP_NETMASK
        new_cfg.netmask = (uint32_t)CONFIG_NOVA_LP_NETMASK;
#endif
#ifdef CONFIG_NOVA_LP_GATEWAY
        new_cfg.gateway = (uint32_t)CONFIG_NOVA_LP_GATEWAY;
#endif
        /* Update RAM cache immediately so this boot uses the new IP
         * even if the flash write below fails. */
        s_cfg = new_cfg;
        bool save_ok = LpDeviceConfig_Save(&new_cfg);
        /* Pre-scheduler diagnostic — was the OSPI persist successful?
         * Without this we'd see the bank still blank on the next boot
         * and have no idea whether bank_erase, bank_program's
         * Flash_write, or bank_program's read-back verify failed. */
        {
            extern void bb_uart0_puts(const char *s);
            char rbuf[100];
            snprintf(rbuf, sizeof(rbuf),
                "[DevCfg-Override] save returned %s  (s_active_bank=%d after)\r\n",
                save_ok ? "TRUE" : "FALSE", s_active_bank);
            bb_uart0_puts(rbuf);
        }
    }
#endif
}

const LpDeviceConfig *LpDeviceConfig_Get(void)
{
    if (!s_initialized) LpDeviceConfig_Init();
    return &s_cfg;
}

uint32_t LpDeviceConfig_GetIp(void)
{
    const LpDeviceConfig *c = LpDeviceConfig_Get();
    if (c->ip != 0U) return c->ip;
    return (c->role == ORBIT_ROLE_CONTROLLER)
        ? LP_DEVCFG_DEFAULT_IP_CONTROLLER
        : LP_DEVCFG_DEFAULT_IP_ORBIT;
}

uint32_t LpDeviceConfig_GetNetmask(void)
{
    const LpDeviceConfig *c = LpDeviceConfig_Get();
    return (c->netmask != 0U) ? c->netmask : LP_DEVCFG_DEFAULT_NETMASK;
}

uint32_t LpDeviceConfig_GetGateway(void)
{
    const LpDeviceConfig *c = LpDeviceConfig_Get();
    if (c->gateway != 0U) return c->gateway;
    uint32_t ip = LpDeviceConfig_GetIp();
    return (ip & 0xFFFFFF00U) | 0x01U;
}

bool LpDeviceConfig_Save(const LpDeviceConfig *cfg)
{
    if (!s_initialized) LpDeviceConfig_Init();
    if (cfg == NULL || cfg->magic != LP_DEVCFG_MAGIC) return false;

    /* If OSPI didn't come up at boot, the write path is permanently
     * disabled — caller still gets a runtime cache update so this boot
     * uses the new value, but the change is NOT durable. */
    if (s_flash == NULL) {
        LPDC_LOG("[DevCfg] Save: no flash — RAM cache updated, NOT persisted\r\n");
        s_cfg = *cfg;
        s_cfg.crc32 = 0;
        return false;
    }

    /* Build the bank record on the stack (fits comfortably). */
    LpDevCfgBank rec;
    memset(&rec, 0, sizeof(rec));
    rec.bank_magic   = LP_DEVCFG_BANK_MAGIC;
    rec.sequence     = s_sequence + 1U;
    rec.payload_size = sizeof(LpDeviceConfig);
    rec.payload      = *cfg;
    rec.payload.crc32 = 0;   /* legacy field — bank-level CRC supersedes */
    const size_t crc_span = offsetof(LpDevCfgBank, crc32);
    rec.crc32 = crc32_update(0, (const uint8_t *)&rec, crc_span);

    /* Pick the inactive bank — first save (active=-1) targets A. */
    int target = (s_active_bank == 0) ? 1 : 0;

    /* Pre-scheduler-safe step-by-step diagnostic. LPDC_LOG is gated on
     * scheduler-running so a boot-time save failure shows nothing
     * unless we use bb_uart0_puts directly. */
    {
        extern void bb_uart0_puts(const char *s);
        char dbuf[120];
        snprintf(dbuf, sizeof(dbuf),
            "[DevCfg-Save] bank_erase target=%c entering ...\r\n",
            target == 0 ? 'A' : 'B');
        bb_uart0_puts(dbuf);
    }
    int erc = bank_erase(target);
    {
        extern void bb_uart0_puts(const char *s);
        char dbuf[120];
        snprintf(dbuf, sizeof(dbuf),
            "[DevCfg-Save] bank_erase rc=%d\r\n", erc);
        bb_uart0_puts(dbuf);
    }
    if (erc != 0) return false;

    {
        extern void bb_uart0_puts(const char *s);
        char dbuf[120];
        snprintf(dbuf, sizeof(dbuf),
            "[DevCfg-Save] bank_program target=%c entering (rec %u bytes) ...\r\n",
            target == 0 ? 'A' : 'B', (unsigned)sizeof(rec));
        bb_uart0_puts(dbuf);
    }
    int prc = bank_program(target, &rec);
    {
        extern void bb_uart0_puts(const char *s);
        char dbuf[120];
        snprintf(dbuf, sizeof(dbuf),
            "[DevCfg-Save] bank_program rc=%d\r\n", prc);
        bb_uart0_puts(dbuf);
    }
    if (prc != 0) return false;

    /* Commit only after a successful write+verify so a mid-write
     * fault leaves the previously-active bank intact. */
    s_cfg          = rec.payload;
    s_active_bank  = target;
    s_sequence     = rec.sequence;

    LPDC_LOG("[DevCfg] Save bank=%c seq=%u role=%u ip=0x%08X\r\n",
             target == 0 ? 'A' : 'B',
             (unsigned)rec.sequence,
             (unsigned)cfg->role, (unsigned)cfg->ip);
    return true;
}

bool LpDeviceConfig_Erase(uint32_t confirm_magic)
{
    if (confirm_magic != LP_DEVCFG_ERASE_CONFIRM) return false;
    if (!s_initialized) LpDeviceConfig_Init();
    if (s_flash == NULL) return false;

    bool ok_a = (bank_erase(0) == 0);
    bool ok_b = (bank_erase(1) == 0);

    /* Drop runtime cache back to compile-time defaults so a subsequent
     * Get() reflects the wiped state. */
    apply_compile_time_defaults();

    LPDC_LOG("[DevCfg] Erase: A=%s B=%s\r\n",
             ok_a ? "ok" : "fail", ok_b ? "ok" : "fail");
    return ok_a && ok_b;
}
