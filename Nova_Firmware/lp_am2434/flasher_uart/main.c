/*
 * main.c (Constellation auto-mode JTAG flasher for AM243x-LP)
 *
 * Modified from TI SBL JTAG Uniflash example. The original drives an
 * interactive UART menu via gets() — but gets() backed by UART debug_log
 * is non-blocking on this SDK, so it spins. Instead we run autonomously:
 *
 *   1. Init OSPI + Flash (Drivers_open / Board_driversOpen).
 *   2. Spin waiting for `g_flash_request.magic == 0xF1A5C0DE`. The DSS
 *      driver (uniflash_run.js) loads file bytes into gFileBuf via
 *      loadRaw, then writes size + offset into the request struct, then
 *      sets magic.
 *   3. Erase the affected blocks.
 *   4. Write the file in chunk-size pages.
 *   5. Verify by reading back and memcmp.
 *   6. Set g_flash_request.status (0 = success, non-zero = step that failed)
 *      and g_flash_request.done = 1.
 *   7. Print progress over UART0 (115200 8N1) for human visibility,
 *      but the orchestrator does NOT depend on it for control flow.
 *
 * Build with the same syscfg/makefile/linker as the original example
 * (in the parent flasher_uart/ directory) — only this main.c is changed.
 */

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <kernel/dpl/DebugP.h>
#include <kernel/dpl/ClockP.h>
#include <drivers/bootloader.h>
#include <drivers/sciclient.h>
#include <drivers/ospi.h>
#include <board/flash.h>
#include "ti_drivers_open_close.h"
#include "ti_board_open_close.h"
#include "ti_drivers_config.h"
#include "ti_board_config.h"
/* Build-time variant selection — exactly one (or none) of these macros
 * may be defined via DEFINES_common on the gmake command line:
 *
 *   (none)                    → default auto-flasher main (the request-
 *                                queue spin-loop driven by uniflash_run.js)
 *   -DOSPI_NV_RECOVER         → CFR3N=0x00 NV recovery (memories/repo/
 *                                lp-am2434-s24l0707-unrecoverable.md;
 *                                bench-verified 2026-05-15)
 *   -DOSPI_PHY_TUNING_WRITER  → write the 128-byte OSPI PHY attack
 *                                vector to phyTuningOffset (0x2000000 on
 *                                S25HL512T). Required once after NV-
 *                                recovery or on a virgin chip before any
 *                                SBL flash, otherwise the SBL wedges
 *                                silently inside Flash_norOspiPhyTune.
 *                                See memories/repo/lp-am2434-ospi-boot-
 *                                missing-phy-tuning.md.
 *
 * IMPORTANT: pass these via DEFINES_common (not CFLAGS_PROFILE — that
 * env var is silently ignored by this makefile). Example:
 *   gmake PROFILE=release DEFINES_common='-DSOC_AM243X -DOS_NORTOS \
 *     -DOSPI_PHY_TUNING_WRITER' all
 */

#define FILE_MAX_SIZE   (0x150000) /* matches MSRAM_2 size in syscfg linker */

uint8_t  gFileBuf[FILE_MAX_SIZE]   __attribute__((aligned(128), section(".bss.filebuf")));
uint8_t  gVerifyBuf[0x10000]       __attribute__((aligned(128)));

/* Request control block — DSS resolves by symbol name and writes via
 * memory_writeWord. Volatile so the spin loop re-reads. */
typedef struct FlashRequest_s {
    volatile uint32_t magic;     /* DSS writes 0xF1A5C0DE last to start */
    volatile uint32_t size;      /* bytes to write from gFileBuf */
    volatile uint32_t offset;    /* OSPI offset (must be erase-block aligned) */
    volatile uint32_t status;    /* 0 = ok; non-zero = step that failed */
    volatile uint32_t done;      /* flasher sets to 1 when finished */
    volatile uint32_t progress;  /* 0..100 */
} FlashRequest_t;

FlashRequest_t g_flash_request = { 0, 0, 0, 0, 0, 0 };

#define MAGIC_GO     0xF1A5C0DEu
#define MAGIC_REBOOT 0xF1A5DEADu  /* DSS post-flash: trigger SoC warm reset */

#define STEP_OK             0
#define STEP_BAD_OFFSET     1
#define STEP_BAD_SIZE       2
#define STEP_FLASH_OPEN     3
#define STEP_ERASE          4
#define STEP_WRITE          5
#define STEP_VERIFY_READ    6
#define STEP_VERIFY_DIFF    7

extern Flash_Handle gFlashHandle[];

static int32_t do_erase_write_verify(uint32_t flashOffset, uint32_t fileSize)
{
    Flash_Attrs *attrs = Flash_getAttrs(CONFIG_FLASH0);
    if (attrs == NULL) return STEP_FLASH_OPEN;

    uint32_t eraseBlockSize = attrs->pageCount * attrs->pageSize;
    if (eraseBlockSize == 0 || eraseBlockSize > FILE_MAX_SIZE) return STEP_FLASH_OPEN;
    if ((flashOffset % attrs->blockSize) != 0)                  return STEP_BAD_OFFSET;
    if ((flashOffset + fileSize) > attrs->flashSize)             return STEP_BAD_SIZE;

    /* Erase + write loop — one chunk per erase block. */
    uint32_t curOffset = flashOffset;
    uint32_t remain    = fileSize;
    uint8_t *src       = gFileBuf;
    uint32_t totalChunks = (fileSize + eraseBlockSize - 1) / eraseBlockSize;
    uint32_t curChunk    = 0;

    while (remain > 0) {
        uint32_t chunk = (remain < eraseBlockSize) ? remain : eraseBlockSize;
        uint32_t blk, page;

        if (Flash_offsetToBlkPage(gFlashHandle[CONFIG_FLASH0], curOffset, &blk, &page) != SystemP_SUCCESS)
            return STEP_ERASE;
        if (Flash_eraseBlk(gFlashHandle[CONFIG_FLASH0], blk) != SystemP_SUCCESS)
            return STEP_ERASE;
        if (Flash_write(gFlashHandle[CONFIG_FLASH0], curOffset, src, chunk) != SystemP_SUCCESS)
            return STEP_WRITE;

        curOffset += chunk;
        src       += chunk;
        remain    -= chunk;
        curChunk++;
        g_flash_request.progress = (curChunk * 50u) / totalChunks;
        DebugP_log("[FLASH] write %u/%u (%u bytes) ok\r\n", curChunk, totalChunks, chunk);
    }

    /* Verify in chunks of gVerifyBuf size. */
    curOffset = flashOffset;
    remain    = fileSize;
    src       = gFileBuf;
    while (remain > 0) {
        uint32_t chunk = (remain < sizeof(gVerifyBuf)) ? remain : sizeof(gVerifyBuf);
        if (Flash_read(gFlashHandle[CONFIG_FLASH0], curOffset, gVerifyBuf, chunk) != SystemP_SUCCESS)
            return STEP_VERIFY_READ;
        if (memcmp(gVerifyBuf, src, chunk) != 0)
            return STEP_VERIFY_DIFF;
        curOffset += chunk;
        src       += chunk;
        remain    -= chunk;
    }
    g_flash_request.progress = 100;
    return STEP_OK;
}

#if defined(OSPI_NV_RECOVER)

/* ─── OSPI NV register recovery main ────────────────────────────────── */

/* Diagnostic readback area. DSS reads these post-recovery to verify
 * what landed on the chip. Volatile so the linker doesn't fold them. */
typedef struct OspiNvRecover_s {
    volatile uint32_t magic_done;   /* set to 0xF1A5DA7A on completion */
    volatile uint32_t status;       /* 0 = success */
    volatile uint8_t  id_manuf;     /* RDID byte 0 (expect 0x34) */
    volatile uint8_t  id_dev_hi;    /* RDID byte 1 (expect 0x2A) */
    volatile uint8_t  id_dev_lo;    /* RDID byte 2 (expect 0x1A) */
    volatile uint8_t  sr1_pre;
    volatile uint8_t  sr1_after_wren;
    volatile uint8_t  cfr2v_pre;
    volatile uint8_t  cfr3v_pre;
    volatile uint8_t  cfr4v_pre;
    volatile uint8_t  cfr2n_post;
    volatile uint8_t  cfr3n_post;
    volatile uint8_t  cfr4n_post;
    volatile uint8_t  wip_polls;
} OspiNvRecover_t;

OspiNvRecover_t g_ospi_nv_recover = {0};

#define OSPI_NV_RECOVER_DONE_MAGIC  0xF1A5DA7Au

static int recover_writeCmd(OSPI_Handle h, uint8_t cmd, uint8_t *buf, uint32_t len)
{
    OSPI_WriteCmdParams p;
    OSPI_WriteCmdParams_init(&p);
    p.cmd          = cmd;
    p.cmdAddr      = OSPI_CMD_INVALID_ADDR;
    p.numAddrBytes = 0;
    p.txDataBuf    = buf;
    p.txDataLen    = len;
    return OSPI_writeCmd(h, &p);
}

static int recover_readCmd(OSPI_Handle h, uint8_t cmd, uint8_t *buf, uint32_t len)
{
    OSPI_ReadCmdParams p;
    OSPI_ReadCmdParams_init(&p);
    p.cmd          = cmd;
    p.cmdAddr      = OSPI_CMD_INVALID_ADDR;
    p.numAddrBytes = 0;
    p.dummyBits    = 0;
    p.rxDataBuf    = buf;
    p.rxDataLen    = len;
    return OSPI_readCmd(h, &p);
}

static int recover_readRegAddr(OSPI_Handle h, uint32_t addr, uint8_t *out)
{
    OSPI_ReadCmdParams p;
    OSPI_ReadCmdParams_init(&p);
    p.cmd          = 0x65;          /* RDAR */
    p.cmdAddr      = addr;
    p.numAddrBytes = 3;
    p.dummyBits    = 8;             /* 1S RDAR default latency */
    p.rxDataBuf    = out;
    p.rxDataLen    = 1;
    return OSPI_readCmd(h, &p);
}

int main(void)
{
    System_init();
    Drivers_open();
    /* SKIP Board_driversOpen() — it calls Flash_open which hangs on a
     * Cypress S25HL512T with CFR3N=0x00 (the 0.A.115 bug). Drivers_open()
     * alone has already powered up OSPI and configured the controller
     * baseline via DMSC TISCI, which is all we need for STIG ops. */

    OSPI_Handle h = gOspiHandle[CONFIG_OSPI0];
    if (h == NULL) {
        g_ospi_nv_recover.status = 0xFF;
        g_ospi_nv_recover.magic_done = OSPI_NV_RECOVER_DONE_MAGIC;
        while (1) { ClockP_usleep(10 * 1000); }
    }

    /* Force controller into 1S-1S-1S SDR (no DDR, no dummy) to match a
     * wedged chip's factory state. */
    OSPI_setProtocol(h, OSPI_NOR_PROTOCOL(1,1,1,0));
    OSPI_disableDdrRdCmds(h);
    OSPI_setCmdDummyCycles(h, 0);

    /* Wake from deep power down (no-op if already awake). */
    (void)recover_writeCmd(h, 0xAB, NULL, 0);
    ClockP_usleep(100);

    /* RDID — proves the controller is actually clocking bytes to the chip. */
    uint8_t id[3] = {0xFF, 0xFF, 0xFF};
    {
        OSPI_ReadCmdParams p;
        OSPI_ReadCmdParams_init(&p);
        p.cmd          = 0x9F;
        p.cmdAddr      = OSPI_CMD_INVALID_ADDR;
        p.numAddrBytes = 0;
        p.dummyBits    = 0;
        p.rxDataBuf    = id;
        p.rxDataLen    = 3;
        (void)OSPI_readCmd(h, &p);
    }
    g_ospi_nv_recover.id_manuf  = id[0];
    g_ospi_nv_recover.id_dev_hi = id[1];
    g_ospi_nv_recover.id_dev_lo = id[2];

    /* Read SR1 + current volatile config registers. */
    uint8_t sr1 = 0xFF;
    (void)recover_readCmd(h, 0x05, &sr1, 1);
    g_ospi_nv_recover.sr1_pre = sr1;

    uint8_t cfr2v = 0xFF, cfr3v = 0xFF, cfr4v = 0xFF;
    (void)recover_readRegAddr(h, 0x800003, &cfr2v);
    (void)recover_readRegAddr(h, 0x800004, &cfr3v);
    (void)recover_readRegAddr(h, 0x800005, &cfr4v);
    g_ospi_nv_recover.cfr2v_pre = cfr2v;
    g_ospi_nv_recover.cfr3v_pre = cfr3v;
    g_ospi_nv_recover.cfr4v_pre = cfr4v;

    /* If the chip didn't even ACK RDID, we're talking to dead air. */
    if (id[0] != 0x34) {
        g_ospi_nv_recover.status = 0xE1;       /* RDID wrong */
        g_ospi_nv_recover.magic_done = OSPI_NV_RECOVER_DONE_MAGIC;
        while (1) { ClockP_usleep(10 * 1000); }
    }

    /* WREN. */
    (void)recover_writeCmd(h, 0x06, NULL, 0);
    sr1 = 0xFF;
    (void)recover_readCmd(h, 0x05, &sr1, 1);
    g_ospi_nv_recover.sr1_after_wren = sr1;

    if ((sr1 & 0x02) == 0) {
        g_ospi_nv_recover.status = 0xE2;       /* WEL not set */
        g_ospi_nv_recover.magic_done = OSPI_NV_RECOVER_DONE_MAGIC;
        while (1) { ClockP_usleep(10 * 1000); }
    }

    /* WRRSB cmd 0x01 with 5 bytes — correct values for THIS chip's
     * working state, verified 2026-05-18 by reading CFRs on the known-
     * good STORAGE board (S24L0417) and comparing to Nova (S24L0707):
     *   STR1N=00, CFR1N=02, CFR2N=08, CFR3N=08, CFR4N=08.
     *
     * Note CFR1N=0x02 — the SDK's `Flash_quirkQSPIEarlyFixup` SafeBoot
     * recovery uses CFR1N=0x00 as its "default" value, but that path
     * only runs when SR1 != 0x00 (chip in SafeBoot mode). On a healthy
     * chip with SR1=0x00, the SDK fixup is SKIPPED entirely — and the
     * chip's actual factory default for CFR1N (per STORAGE's working
     * state) is 0x02. Previous recovery runs wrote CFR1N=0x00 which
     * left Nova's chip in a state where ROM cold-boot failed silently
     * even though all other CFRs were correct and SBL bytes intact.
     *
     * See memories/repo/lp-am2434-ospi-boot-missing-phy-tuning.md
     * (2026-05-18 update). */
    uint8_t regs[5] = {0x00, 0x02, 0x08, 0x08, 0x08};
    (void)recover_writeCmd(h, 0x01, regs, 5);

    /* Poll WIP=0 (NV write can take up to ~500 ms). */
    uint32_t polls = 0;
    for (polls = 0; polls < 200; polls++) {
        sr1 = 0xFF;
        (void)recover_readCmd(h, 0x05, &sr1, 1);
        if ((sr1 & 0x01) == 0) break;
        ClockP_usleep(5000);
    }
    g_ospi_nv_recover.wip_polls = (uint8_t)polls;

    /* RSTEN + RST to apply the new NV. */
    (void)recover_writeCmd(h, 0x66, NULL, 0);
    (void)recover_writeCmd(h, 0x99, NULL, 0);
    ClockP_usleep(200);

    /* Verify NV registers landed. */
    uint8_t cfr2n = 0xFF, cfr3n = 0xFF, cfr4n = 0xFF;
    (void)recover_readRegAddr(h, 0x000003, &cfr2n);
    (void)recover_readRegAddr(h, 0x000004, &cfr3n);
    (void)recover_readRegAddr(h, 0x000005, &cfr4n);
    g_ospi_nv_recover.cfr2n_post = cfr2n;
    g_ospi_nv_recover.cfr3n_post = cfr3n;
    g_ospi_nv_recover.cfr4n_post = cfr4n;

    if (cfr2n == 0x08 && cfr3n == 0x08 && cfr4n == 0x08) {
        g_ospi_nv_recover.status = 0;          /* success */
    } else {
        g_ospi_nv_recover.status = 0xE3;       /* NV didn't take */
    }
    g_ospi_nv_recover.magic_done = OSPI_NV_RECOVER_DONE_MAGIC;

    while (1) { ClockP_usleep(10 * 1000); }
}

#elif defined(OSPI_PHY_TUNING_WRITER)

/* ─── OSPI PHY tuning attack-vector writer main ──────────────────────────
 *
 * On AM243x-LP the prebuilt SBL at offset 0x0 calibrates its OSPI PHY by
 * reading a known byte pattern (the "attack vector") from
 * Flash_Attrs.phyTuningOffset (0x2000000 on the S25HL512T per the syscfg).
 * If that pattern is absent or wrong, the SBL wedges silently inside
 * Flash_norOspiPhyTune at boot — no UART output, ROM still firewalls R5,
 * JTAG attach returns -1065. Working chips have the vector from the
 * original TI Uniflash commissioning; chips that have been NV-recovered
 * via OSPI_NV_RECOVER do NOT and need it re-written.
 *
 * This is a 1:1 port of TI's Bootloader_uniflashFlashPhyTuningData
 * (SDK 12.00, bootloader_uniflash_common.c:362), wrapped in the same
 * symbol-poll / done-magic pattern as OSPI_NV_RECOVER so run_phy_tuning_
 * writer.js can drive it via DSS.
 *
 * Crucial: phyEnable is FALSE in this flasher's syscfg, so the regular
 * Board_flashOpen path skips the auto-flash-tuning-vector branch entirely.
 * We have to call the SDK functions explicitly here. The data lives in
 * the linked OSPI PHY LLD (gOspiFlashAttackVector[], 128 B) and is the
 * SAME byte pattern the SBL will look for.
 */

typedef struct OspiPhyWriter_s {
    volatile uint32_t magic_done;        /* 0xF1A5DA7B on completion */
    volatile uint32_t status;            /* 0 = success */
    volatile uint32_t phy_tuning_offset; /* the OSPI offset we wrote to */
    volatile uint32_t tuning_data_addr;  /* SoC address of source pattern */
    volatile uint32_t tuning_data_size;  /* expected 128 for S25HL512T */
    volatile uint8_t  verify_match;      /* 1 = readback equals source */
    volatile uint8_t  reserved[3];
    volatile uint8_t  read_back[32];     /* first 32 bytes of readback */
    volatile uint8_t  source_head[32];   /* first 32 bytes of source data */
} OspiPhyWriter_t;

OspiPhyWriter_t g_ospi_phy_writer = {0};

#define OSPI_PHY_WRITER_DONE_MAGIC  0xF1A5DA7Bu

#define PHYW_OK            0x00
#define PHYW_FLASH_OPEN    0xA1
#define PHYW_BAD_ATTRS     0xA2
#define PHYW_BLK_LOOKUP    0xA3
#define PHYW_ERASE         0xA4
#define PHYW_WRITE         0xA5
#define PHYW_READ          0xA6
#define PHYW_VERIFY_DIFF   0xA7
#define PHYW_NO_TUNING     0xA8

static void phyw_park(uint32_t code)
{
    g_ospi_phy_writer.status = code;
    g_ospi_phy_writer.magic_done = OSPI_PHY_WRITER_DONE_MAGIC;
    while (1) { ClockP_usleep(10 * 1000); }
}

int main(void)
{
    uint8_t readBuf[OSPI_FLASH_ATTACK_VECTOR_SIZE];

    System_init();
    Drivers_open();

    int32_t status = Board_driversOpen();   /* opens flash (no PHY, no DAC) */
    if (status != SystemP_SUCCESS) {
        DebugP_log("[PHYW] Board_driversOpen FAILED (status=%d)\r\n", (int)status);
        phyw_park(PHYW_FLASH_OPEN);
    }

    Flash_Handle hFlash = gFlashHandle[CONFIG_FLASH0];
    Flash_Attrs *attrs  = Flash_getAttrs(CONFIG_FLASH0);
    if (hFlash == NULL || attrs == NULL) {
        DebugP_log("[PHYW] Flash_getAttrs returned NULL\r\n");
        phyw_park(PHYW_BAD_ATTRS);
    }

    /* Get the constant tuning pattern from the linked OSPI PHY LLD. The
     * SDK's `OSPI_phyGetTuningData` sets `tuningData` to the SoC address
     * of the static `gOspiFlashAttackVector[]` array (128 bytes for
     * AM243x). The same pattern is what `OSPI_phyReadAttackVector`
     * compares against at SBL boot — so writing it here makes the SBL
     * happy without us needing to know the bytes. */
    uint32_t tuningDataAddr = 0, tuningDataSize = 0;
    OSPI_phyGetTuningData(&tuningDataAddr, &tuningDataSize);
    if (tuningDataAddr == 0 || tuningDataSize == 0 ||
        tuningDataSize > sizeof(readBuf)) {
        DebugP_log("[PHYW] OSPI_phyGetTuningData returned bad values "
                   "(addr=0x%08x size=%u)\r\n", tuningDataAddr, tuningDataSize);
        phyw_park(PHYW_NO_TUNING);
    }
    g_ospi_phy_writer.tuning_data_addr = tuningDataAddr;
    g_ospi_phy_writer.tuning_data_size = tuningDataSize;

    uint8_t *src = (uint8_t *)tuningDataAddr;
    for (uint32_t i = 0; i < 32 && i < tuningDataSize; i++) {
        g_ospi_phy_writer.source_head[i] = src[i];
    }

    uint32_t phyOff = Flash_getPhyTuningOffset(hFlash);
    g_ospi_phy_writer.phy_tuning_offset = phyOff;
    DebugP_log("[PHYW] phyTuningOffset=0x%08x dataSize=%u src=0x%08x\r\n",
        phyOff, tuningDataSize, tuningDataAddr);

    /* Erase the full erase-block that contains phyTuningOffset. The SDK's
     * own auto-recover path does the same (flash_nor_ospi.c:1479-1480). */
    uint32_t blk = 0, page = 0;
    if (Flash_offsetToBlkPage(hFlash, phyOff, &blk, &page) != SystemP_SUCCESS) {
        DebugP_log("[PHYW] Flash_offsetToBlkPage failed\r\n");
        phyw_park(PHYW_BLK_LOOKUP);
    }
    if (Flash_eraseBlk(hFlash, blk) != SystemP_SUCCESS) {
        DebugP_log("[PHYW] Flash_eraseBlk(%u) failed\r\n", blk);
        phyw_park(PHYW_ERASE);
    }

    /* Write the 128-byte attack vector. NoRTOS context — INDIRECT_WRITE
     * works fine here (unlike the runtime FreeRTOS context, see
     * memories/repo/lp-am2434-runtime-flashwrite-unresolved.md). */
    if (Flash_write(hFlash, phyOff, src, tuningDataSize) != SystemP_SUCCESS) {
        DebugP_log("[PHYW] Flash_write failed\r\n");
        phyw_park(PHYW_WRITE);
    }

    /* Read it back and memcmp — this is the same kind of self-check the
     * auto-flasher does for app-image writes. */
    memset(readBuf, 0, sizeof(readBuf));
    if (Flash_read(hFlash, phyOff, readBuf, tuningDataSize) != SystemP_SUCCESS) {
        DebugP_log("[PHYW] Flash_read verify failed\r\n");
        phyw_park(PHYW_READ);
    }
    for (uint32_t i = 0; i < 32 && i < tuningDataSize; i++) {
        g_ospi_phy_writer.read_back[i] = readBuf[i];
    }
    if (memcmp(readBuf, src, tuningDataSize) != 0) {
        DebugP_log("[PHYW] readback mismatch\r\n");
        g_ospi_phy_writer.verify_match = 0;
        phyw_park(PHYW_VERIFY_DIFF);
    }
    g_ospi_phy_writer.verify_match = 1;

    DebugP_log("[PHYW] SUCCESS — %u bytes written to 0x%08x and verified\r\n",
        tuningDataSize, phyOff);
    phyw_park(PHYW_OK);
}

#elif defined(OSPI_NV_DUMP)

/* ─── OSPI NV register READ-ONLY dump diagnostic ─────────────────────────
 *
 * Non-destructive variant of OSPI_NV_RECOVER. Reads RDID + SR1V/N +
 * CFR1V/N + CFR2V/N + CFR3V/N + CFR4V/N + CFR5V/N via STIG (RDAR
 * cmd 0x65 with address). Stores all 12 values in a struct. No WREN,
 * no WRRSB, no RSTEN+RST — pure read.
 *
 * Built to investigate 2026-05-17 finding: chip can't ROM-boot from
 * OSPI even with SBL bytes + PHY tuning + CFR2/3/4N all verified
 * intact. The recovery script only ever touches CFR2/3/4N — CFR1N
 * and CFR5N may have been clobbered by the original 0.A.115 brick
 * firmware and our recovery never noticed.
 *
 * Cypress S25HL512T register map (addresses for cmd 0x65 RDAR):
 *   0x000000 = STR1N   0x800000 = STR1V
 *   0x000002 = CFR1N   0x800002 = CFR1V
 *   0x000003 = CFR2N   0x800003 = CFR2V
 *   0x000004 = CFR3N   0x800004 = CFR3V
 *   0x000005 = CFR4N   0x800005 = CFR4V
 *   0x000006 = CFR5N   0x800006 = CFR5V
 *
 * DSS driver = `ospi_flash/run_nv_dump.js`.
 */

typedef struct OspiNvDump_s {
    volatile uint32_t magic_done;   /* 0xF1A5DA7D on completion */
    volatile uint32_t status;       /* 0 = success */
    volatile uint8_t  id_manuf;     /* RDID byte 0 (expect 0x34) */
    volatile uint8_t  id_dev_hi;    /* RDID byte 1 (expect 0x2A) */
    volatile uint8_t  id_dev_lo;    /* RDID byte 2 (expect 0x1A) */
    volatile uint8_t  pad0;
    volatile uint8_t  sr1n;         /* expected 0x00 */
    volatile uint8_t  sr1v;
    volatile uint8_t  cfr1n;        /* expected 0x00 (per SDK fixup) */
    volatile uint8_t  cfr1v;
    volatile uint8_t  cfr2n;        /* expected 0x08 */
    volatile uint8_t  cfr2v;
    volatile uint8_t  cfr3n;        /* expected 0x08 */
    volatile uint8_t  cfr3v;
    volatile uint8_t  cfr4n;        /* expected 0x08 */
    volatile uint8_t  cfr4v;
    volatile uint8_t  cfr5n;        /* unknown — never audited */
    volatile uint8_t  cfr5v;
} OspiNvDump_t;

OspiNvDump_t g_ospi_nv_dump = {0};

#define OSPI_NV_DUMP_DONE_MAGIC  0xF1A5DA7Du

static int dump_readRegAddr(OSPI_Handle h, uint32_t addr, uint8_t *out)
{
    OSPI_ReadCmdParams p;
    OSPI_ReadCmdParams_init(&p);
    p.cmd          = 0x65;          /* RDAR */
    p.cmdAddr      = addr;
    p.numAddrBytes = 3;
    p.dummyBits    = 8;
    p.rxDataBuf    = out;
    p.rxDataLen    = 1;
    return OSPI_readCmd(h, &p);
}

int main(void)
{
    System_init();
    Drivers_open();
    /* Skip Board_driversOpen — same as OSPI_NV_RECOVER. We only need
     * Drivers_open's TISCI pinmux + OSPI controller baseline for STIG. */

    OSPI_Handle h = gOspiHandle[CONFIG_OSPI0];
    if (h == NULL) {
        g_ospi_nv_dump.status = 0xFF;
        g_ospi_nv_dump.magic_done = OSPI_NV_DUMP_DONE_MAGIC;
        while (1) { ClockP_usleep(10 * 1000); }
    }

    /* 1S-1S-1S SDR baseline — matches what ROM uses at POR. */
    OSPI_setProtocol(h, OSPI_NOR_PROTOCOL(1,1,1,0));
    OSPI_disableDdrRdCmds(h);
    OSPI_setCmdDummyCycles(h, 0);

    /* Wake from any deep-PD state (no-op if awake). */
    {
        OSPI_WriteCmdParams p;
        OSPI_WriteCmdParams_init(&p);
        p.cmd = 0xAB;
        p.cmdAddr = OSPI_CMD_INVALID_ADDR;
        p.numAddrBytes = 0;
        p.txDataBuf = NULL;
        p.txDataLen = 0;
        (void)OSPI_writeCmd(h, &p);
    }
    ClockP_usleep(100);

    /* RDID. */
    uint8_t id[3] = {0xFF, 0xFF, 0xFF};
    {
        OSPI_ReadCmdParams p;
        OSPI_ReadCmdParams_init(&p);
        p.cmd          = 0x9F;
        p.cmdAddr      = OSPI_CMD_INVALID_ADDR;
        p.numAddrBytes = 0;
        p.dummyBits    = 0;
        p.rxDataBuf    = id;
        p.rxDataLen    = 3;
        (void)OSPI_readCmd(h, &p);
    }
    g_ospi_nv_dump.id_manuf  = id[0];
    g_ospi_nv_dump.id_dev_hi = id[1];
    g_ospi_nv_dump.id_dev_lo = id[2];

    /* SR1 via cmd 0x05 (RDSR). */
    {
        OSPI_ReadCmdParams p;
        OSPI_ReadCmdParams_init(&p);
        p.cmd          = 0x05;
        p.cmdAddr      = OSPI_CMD_INVALID_ADDR;
        p.numAddrBytes = 0;
        p.dummyBits    = 0;
        uint8_t v = 0xFF;
        p.rxDataBuf = &v; p.rxDataLen = 1;
        (void)OSPI_readCmd(h, &p);
        g_ospi_nv_dump.sr1v = v;
    }

    /* All CFR registers via RDAR (cmd 0x65). N at low addresses,
     * V at 0x800000+. STR1N at 0x000000, STR1V at 0x800000.
     * Note: SR1V we already read via RDSR; we read STR1N via RDAR
     * to confirm. */
    uint8_t v;
    v = 0xFF; (void)dump_readRegAddr(h, 0x000000, &v); g_ospi_nv_dump.sr1n  = v;
    v = 0xFF; (void)dump_readRegAddr(h, 0x000002, &v); g_ospi_nv_dump.cfr1n = v;
    v = 0xFF; (void)dump_readRegAddr(h, 0x800002, &v); g_ospi_nv_dump.cfr1v = v;
    v = 0xFF; (void)dump_readRegAddr(h, 0x000003, &v); g_ospi_nv_dump.cfr2n = v;
    v = 0xFF; (void)dump_readRegAddr(h, 0x800003, &v); g_ospi_nv_dump.cfr2v = v;
    v = 0xFF; (void)dump_readRegAddr(h, 0x000004, &v); g_ospi_nv_dump.cfr3n = v;
    v = 0xFF; (void)dump_readRegAddr(h, 0x800004, &v); g_ospi_nv_dump.cfr3v = v;
    v = 0xFF; (void)dump_readRegAddr(h, 0x000005, &v); g_ospi_nv_dump.cfr4n = v;
    v = 0xFF; (void)dump_readRegAddr(h, 0x800005, &v); g_ospi_nv_dump.cfr4v = v;
    v = 0xFF; (void)dump_readRegAddr(h, 0x000006, &v); g_ospi_nv_dump.cfr5n = v;
    v = 0xFF; (void)dump_readRegAddr(h, 0x800006, &v); g_ospi_nv_dump.cfr5v = v;

    g_ospi_nv_dump.status = (id[0] == 0x34) ? 0 : 0xE1;
    g_ospi_nv_dump.magic_done = OSPI_NV_DUMP_DONE_MAGIC;

    while (1) { ClockP_usleep(10 * 1000); }
}

#elif defined(OSPI_READ_DUMP)

/* ─── OSPI read-dump diagnostic main ─────────────────────────────────────
 *
 * Diagnostic that uses Flash_read (STIG-backed, NOT memory-mapped) to
 * read back OSPI 0x0 (SBL header) and OSPI 0x2000000 (PHY tuning vector)
 * into a known-address struct that DSS can read via JTAG memory poke.
 *
 * Independent of:
 *   - OSPI controller DAC mode state (avoids the AHB-region-returns-0
 *     trap that ospi_read_dump.js hit when halting the regular flasher
 *     in its poll loop)
 *   - the flasher's normal write/verify path (so we won't get fooled
 *     by a Flash_write that silently corrupts data which Flash_read
 *     happens to read back identically)
 *
 * Pattern matches OSPI_NV_RECOVER / OSPI_PHY_TUNING_WRITER. DSS driver
 * = `ospi_flash/run_read_dump.js`.
 */

typedef struct OspiReadDump_s {
    volatile uint32_t magic_done;          /* 0xF1A5DA7C on completion */
    volatile uint32_t status;              /* 0 = success */
    volatile uint8_t  sbl_head[128];       /* OSPI 0x0 first 128 B */
    volatile uint8_t  phy_tune_head[128];  /* OSPI 0x2000000 first 128 B */
} OspiReadDump_t;

OspiReadDump_t g_ospi_read_dump = {0};

#define OSPI_READ_DUMP_DONE_MAGIC  0xF1A5DA7Cu

#define RDD_OK             0x00
#define RDD_FLASH_OPEN     0xB1
#define RDD_READ_SBL       0xB2
#define RDD_READ_PHY       0xB3

static void rdd_park(uint32_t code)
{
    g_ospi_read_dump.status      = code;
    g_ospi_read_dump.magic_done  = OSPI_READ_DUMP_DONE_MAGIC;
    while (1) { ClockP_usleep(10 * 1000); }
}

int main(void)
{
    uint8_t buf[128];

    System_init();
    Drivers_open();

    int32_t status = Board_driversOpen();
    if (status != SystemP_SUCCESS) {
        DebugP_log("[RDD] Board_driversOpen FAILED (status=%d)\r\n", (int)status);
        rdd_park(RDD_FLASH_OPEN);
    }

    Flash_Handle hFlash = gFlashHandle[CONFIG_FLASH0];

    DebugP_log("[RDD] reading OSPI 0x00000000 (SBL header)...\r\n");
    if (Flash_read(hFlash, 0x00000000u, buf, 128) != SystemP_SUCCESS) {
        DebugP_log("[RDD] Flash_read(0x0) FAILED\r\n");
        rdd_park(RDD_READ_SBL);
    }
    for (uint32_t i = 0; i < 128; i++) {
        g_ospi_read_dump.sbl_head[i] = buf[i];
    }
    DebugP_log("[RDD] SBL first bytes: %02x %02x %02x %02x %02x %02x %02x %02x\r\n",
        buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);

    DebugP_log("[RDD] reading OSPI 0x02000000 (PHY tuning)...\r\n");
    if (Flash_read(hFlash, 0x02000000u, buf, 128) != SystemP_SUCCESS) {
        DebugP_log("[RDD] Flash_read(0x2000000) FAILED\r\n");
        rdd_park(RDD_READ_PHY);
    }
    for (uint32_t i = 0; i < 128; i++) {
        g_ospi_read_dump.phy_tune_head[i] = buf[i];
    }
    DebugP_log("[RDD] PHY tuning first bytes: %02x %02x %02x %02x %02x %02x %02x %02x\r\n",
        buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);

    DebugP_log("[RDD] SUCCESS - both regions read into MSRAM struct\r\n");
    rdd_park(RDD_OK);
}

#else  /* !OSPI_NV_RECOVER && !OSPI_PHY_TUNING_WRITER && !OSPI_NV_DUMP && !OSPI_READ_DUMP — original auto-flasher main */

int main(void)
{
    System_init();
    Drivers_open();
    int32_t status = Board_driversOpen();
    if (status != SystemP_SUCCESS) {
        DebugP_log("[FLASH] Board_driversOpen FAILED\r\n");
        g_flash_request.status = STEP_FLASH_OPEN;
        g_flash_request.done   = 1;
        while (1) { /* park */ }
    }

    DebugP_log("[FLASH] Constellation auto-flasher ready.\r\n");
    DebugP_log("[FLASH] g_flash_request @ %p, waiting for magic=0x%08X\r\n",
        (void *)&g_flash_request, MAGIC_GO);

    /* Multi-flash loop: each MAGIC_GO performs one flash, sets done=1,
     * then waits for DSS to clear magic+done and write the next request.
     * MAGIC_REBOOT exits the loop and issues a SoC warm reset.
     *
     * This is required for XIP builds: nova_lp.release.mcelf_xip contains
     * multiple PT_LOAD segments that each need to be flashed at their own
     * p_paddr offset, and the non-XIP nova_lp.release.mcelf.hs_fs is then
     * flashed at 0x80000. Each in one DSS session = much faster than
     * cold-booting the auto-flasher between every segment. */
    while (1) {
        /* Wait for next magic (GO or REBOOT). */
        while (g_flash_request.magic != MAGIC_GO &&
               g_flash_request.magic != MAGIC_REBOOT) {
            ClockP_usleep(10 * 1000);
        }

        if (g_flash_request.magic == MAGIC_REBOOT) {
            DebugP_log("[FLASH] reboot magic received — issuing SoC warm reset\r\n");
            /* Brief delay so the log line egresses UART FIFO. */
            ClockP_usleep(50 * 1000);
            (void)Sciclient_pmDeviceReset(SystemP_WAIT_FOREVER);
            /* Should not return; if it does, fall through to idle. */
            DebugP_log("[FLASH] Sciclient_pmDeviceReset returned (unexpected)\r\n");
            g_flash_request.magic = 0;
            continue;
        }

        /* MAGIC_GO: perform one flash. */
        DebugP_log("[FLASH] go: size=%u offset=0x%08x\r\n",
            g_flash_request.size, g_flash_request.offset);

        int32_t step = do_erase_write_verify(g_flash_request.offset, g_flash_request.size);
        g_flash_request.status = (uint32_t)step;
        g_flash_request.done   = 1;

        if (step == STEP_OK) {
            DebugP_log("[FLASH] DONE - SUCCESS\r\n");
        } else {
            DebugP_log("[FLASH] DONE - FAIL step=%d\r\n", step);
        }

        /* Park waiting for DSS to clear magic. DSS reads done, then
         * clears magic to 0 (and writes new size/offset/magic for the
         * next flash, or writes MAGIC_REBOOT). */
        while (g_flash_request.magic == MAGIC_GO) {
            ClockP_usleep(10 * 1000);
        }
    }
}

#endif  /* OSPI_NV_RECOVER / OSPI_PHY_TUNING_WRITER / OSPI_NV_DUMP / OSPI_READ_DUMP */
