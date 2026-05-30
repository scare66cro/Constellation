/*
 * ospi_nv_recover.js — Recover a Cypress S25HL512T OSPI flash chip whose
 * non-volatile config registers were written to a non-default state by
 * the runtime firmware's `hal_flash_try_nv_recovery`.
 *
 * BACKGROUND
 * ----------
 * 0.A.115 of the Nova firmware contained a `hal_flash_try_nv_recovery`
 * call inside `hal_flash_write`'s retry envelope that issued a WRRSB
 * (cmd 0x01) with 5 bytes:
 *
 *     STR1N=0x00  CFR1N=0x00  CFR2N=0x08  CFR3N=0x00  CFR4N=0x08
 *
 * The SDK's own `Flash_quirkQSPIEarlyFixup` documents the correct
 * factory-default values for recovery as:
 *
 *     CFR2N=0x08  CFR3N=0x08  CFR4N=0x08
 *
 * — i.e., CFR3N must be 0x08, not 0x00. With CFR3N=0x00 the chip
 * requires non-zero command dummy cycles on every command; SBL
 * doesn't send any; every `Flash_open` attempt against this chip
 * hangs forever inside `Flash_norOspiSetProtocol` → `Flash_set444mode`
 * → `Flash_norOspiWaitReady`.
 *
 * The chip can't recover by itself across power cycles (NV writes are,
 * by definition, non-volatile). The standard `sbl_uart_uniflash`
 * recovery hangs in `Board_driversOpen` for the same reason.
 *
 * WHAT THIS SCRIPT DOES
 * ---------------------
 * 1. Halt R5, write spinloop, load `sciclient_ccs_init` to open the
 *    DMSC firewalls (same dance as `uniflash_run.js`).
 * 2. Halt R5 with firewalls open. From this point JTAG DAP can write
 *    to peripheral registers, including OSPI controller @ 0x0FC40000.
 * 3. Configure the OSPI controller for 1S-1S-1S to match the wedged
 *    chip's current state.
 * 4. Issue WREN (cmd 0x06) via the OSPI controller's STIG path.
 * 5. Issue WRRSB (cmd 0x01) with `00 00 08 08 08` via STIG. This
 *    rewrites the chip's NV config registers to the SDK's documented
 *    factory defaults.
 * 6. Poll the chip's WIP bit until 0 (NV write completes in <500 ms).
 * 7. Issue RSTEN (0x66) + RST (0x99) so the new NV config takes
 *    effect immediately.
 *
 * After the script completes, set SW4 back to OSPI boot mode and
 * power-cycle. The chip will now boot normally and `Flash-LP.ps1`
 * works again.
 *
 * USAGE
 * -----
 *   1. Set SW4 to **NOBOOT / DEV BOOT mode**: switches 1, 2, 3, 4 ON,
 *      rest OFF (BOOTMODE pattern `1111 0000` per SDK
 *      `docs/api_guide_am243x/EVM_SETUP_PAGE.html`). This is the
 *      ONLY mode where DMSC firewalls are open at power-up before any
 *      bootloader runs, which is required because the chip wedge
 *      prevents DMSC from loading in OSPI/UART/DFU modes.
 *      DO NOT use UART boot (1+2+3 ON) — DMSC won't be running and
 *      JTAG writes will fail with `Error -1065`.
 *   2. Power-cycle the LP (full 30 s unplug to drain bulk caps), then
 *      wait ~5 s after replug for the chip to settle.
 *   3. Confirm only the target XDS110 probe is plugged in:
 *        xdsdfu -e
 *   4. Run:
 *        $env:LP_CCXML = 'F:/.../AM2434_LP_B.ccxml'
 *        dss.bat ospi_nv_recover.js
 *   5. Wait for "=== DONE ===" on stdout.
 *   6. Switch SW4 back to OSPI boot (1+2 ON for PCB 190A) and
 *      power-cycle the LP. Then re-run Flash-LP.ps1.
 */

var sdkPath      = "C:/ti/mcu_plus_sdk_am243x_12_00_00_26";
var ccxmlPath    = java.lang.System.getenv("LP_CCXML")
                || "F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP.ccxml";
var ccs_init_elf = sdkPath + "/tools/ccs_load/am243x/sciclient_ccs_init.release.out";

/* AM2434 OSPI controller register map — base address and offsets per
 * `C:/ti/.../source/drivers/ospi/v0/cslr_ospi.h`. */
var OSPI_BASE                       = 0x0FC40000;
var REG_CONFIG                      = OSPI_BASE + 0x00;  /* CONFIG_REG */
var REG_DEV_INSTR_RD_CONFIG         = OSPI_BASE + 0x04;
var REG_DEV_INSTR_WR_CONFIG         = OSPI_BASE + 0x08;
var REG_DELAY_REG                   = OSPI_BASE + 0x0C;
var REG_FLASH_CMD_CTRL              = OSPI_BASE + 0x90;
var REG_FLASH_CMD_ADDR              = OSPI_BASE + 0x94;
var REG_FLASH_RD_DATA_LOWER         = OSPI_BASE + 0xA0;
var REG_FLASH_RD_DATA_UPPER         = OSPI_BASE + 0xA4;
var REG_FLASH_WR_DATA_LOWER         = OSPI_BASE + 0xA8;
var REG_FLASH_WR_DATA_UPPER         = OSPI_BASE + 0xAC;

/* CONFIG_REG bits */
var CONFIG_ENB_DIR_ACC_CTLR_BIT     = (1 << 7);   /* 1 = DAC mode (we want 0 = indirect) */
var CONFIG_ENB_DIR_ACC_CTLR_SHIFT   = 7;

/* FLASH_CMD_CTRL_REG bits */
var CMD_EXEC_BIT                    = (1 << 0);
var CMD_EXEC_STATUS_BIT             = (1 << 1);
var CMD_ENB_WRITE_DATA_BIT          = (1 << 15);
var CMD_ENB_COMD_ADDR_BIT           = (1 << 19);
var CMD_ENB_READ_DATA_BIT           = (1 << 23);
/* NUM_WR_DATA_BYTES at bits [14:12], encoded as (N-1) */
function wrBytesField(n)   { return ((n - 1) & 0x7) << 12; }
function rdBytesField(n)   { return ((n - 1) & 0x7) << 20; }
function addrBytesField(n) { return ((n - 1) & 0x3) << 16; }
function opcodeField(op)   { return (op & 0xFF) << 24; }

importPackage(Packages.com.ti.debug.engine.scripting);
importPackage(Packages.com.ti.ccstudio.scripting.environment);
importPackage(Packages.java.lang);

function execStig(target, cmd) {
    /* Single STIG command (no addr, no data) — e.g. WREN, RSTEN, RST. */
    var ctrl = opcodeField(cmd) | CMD_EXEC_BIT;
    target.memory.writeWord(0, REG_FLASH_CMD_CTRL, ctrl);
    return waitStig(target);
}

function execStigWrite(target, cmd, dataLower, dataUpper, nBytes) {
    /* STIG command with write-data payload (e.g. WRRSB with 5 bytes). */
    target.memory.writeWord(0, REG_FLASH_WR_DATA_LOWER, dataLower);
    target.memory.writeWord(0, REG_FLASH_WR_DATA_UPPER, dataUpper);
    var ctrl = opcodeField(cmd)
             | CMD_ENB_WRITE_DATA_BIT
             | wrBytesField(nBytes)
             | CMD_EXEC_BIT;
    target.memory.writeWord(0, REG_FLASH_CMD_CTRL, ctrl);
    return waitStig(target);
}

function execStigRead(target, cmd, nBytes) {
    /* STIG command with read-data payload (e.g. RDSR with 1 byte).
     * Returns the FLASH_RD_DATA_LOWER value (callers extract bytes). */
    var ctrl = opcodeField(cmd)
             | CMD_ENB_READ_DATA_BIT
             | rdBytesField(nBytes)
             | CMD_EXEC_BIT;
    target.memory.writeWord(0, REG_FLASH_CMD_CTRL, ctrl);
    if (!waitStig(target)) return -1;
    return target.memory.readWord(0, REG_FLASH_RD_DATA_LOWER);
}

function execStigReadAddr(target, cmd, addr, nAddrBytes, dummyBits, nBytes) {
    /* STIG command with address + dummy bits + read data (e.g. RDAR cmd 0x65). */
    target.memory.writeWord(0, REG_FLASH_CMD_ADDR, addr);
    /* Dummy bits go in bits [11:7] of CTRL (NUM_DUMMY_CYCLES field is bits 11:7). */
    var ctrl = opcodeField(cmd)
             | CMD_ENB_COMD_ADDR_BIT
             | addrBytesField(nAddrBytes)
             | ((dummyBits & 0x1F) << 7)
             | CMD_ENB_READ_DATA_BIT
             | rdBytesField(nBytes)
             | CMD_EXEC_BIT;
    target.memory.writeWord(0, REG_FLASH_CMD_CTRL, ctrl);
    if (!waitStig(target)) return -1;
    return target.memory.readWord(0, REG_FLASH_RD_DATA_LOWER);
}

function waitStig(target) {
    /* Spin reading CMD_CTRL_REG until CMD_EXEC_STATUS clears (controller
     * reports the STIG transaction is complete). DSS round-trip is slow
     * enough that explicit sleeps are unnecessary. */
    for (var i = 0; i < 200; i++) {
        var c = target.memory.readWord(0, REG_FLASH_CMD_CTRL);
        if ((c & CMD_EXEC_STATUS_BIT) == 0) return true;
        java.lang.Thread.sleep(10);
    }
    print("[ERROR] STIG wait timeout — CMD_EXEC_STATUS stuck high");
    return false;
}

function go() {
    script.setScriptTimeout(120 * 1000);

    print("=== OSPI NV Recovery — fix CFR3N back to 0x08 ===");
    print("  ccxml: " + ccxmlPath);
    print("");

    print("=== Step 1: DMSC board config (open firewalls) ===");
    var dsR5_0 = debugServer.openSession(".*MAIN_Cortex_R5_0_0");
    dsR5_0.target.connect();
    try { dsR5_0.target.halt(); } catch (e) { print("[step1] pre-halt: " + e); }
    try { dsR5_0.target.reset(); } catch (e) { print("[step1] pre-reset: " + e); }
    try { dsR5_0.target.halt(); } catch (e) { print("[step1] post-reset halt: " + e); }
    /* Write a tiny spinloop at 0x78000000 (MSRAM) and let the R5 run it.
     * Necessary because subsequent loadProgram needs a permissive MPU
     * state, which a halted-from-ROM-boot R5 doesn't have. */
    dsR5_0.memory.fill(0x78000000, 0, 0x2000, 0);
    dsR5_0.memory.writeWord(0, 0x78000000, 0xE59FF004);  /* LDR PC,[PC,#4] */
    dsR5_0.memory.writeWord(0, 0x78000004, 0x38);
    dsR5_0.memory.writeWord(0, 0x78000038, 0xEAFFFFFE);  /* B . */
    dsR5_0.target.halt();
    dsR5_0.target.reset();
    dsR5_0.memory.loadProgram(ccs_init_elf);
    dsR5_0.target.halt();
    dsR5_0.target.runAsynch();
    java.lang.Thread.sleep(5000);
    dsR5_0.target.halt();
    print("  DMSC firewalls open.");
    print("");

    print("=== Step 1b: Unlock PADCFG (LOCK0 KICK0+KICK1) + pinmux OSPI0 pins ===");
    /* 2026-05-15 (round 2): Initial write-without-unlock showed PADCFG
     * registers read back 0x214007 regardless of what we wrote — mode bits
     * stuck at 0b111 (GPIO). AM2434 CTRL_MMR partitions have a lock-kick
     * mechanism: write 0x68EF3490 to LOCK0_KICK0 then 0xD172BC5A to
     * LOCK0_KICK1 to unlock the LOCK0 region. PADCFG pin offsets
     * 0x0000-0x4FFF live in LOCK0. SDK does this in pinmux.c::Pinmux_lock_unlock.
     *
     * PADCFG base (CSL_PADCFG_CTRL0_CFG0_BASE) = 0xF0000.
     * LOCK0_KICK0 at base + 0x1008. LOCK0_KICK1 at base + 0x100C.
     * Pin config regs at base + 0x4000 + per-pin offset (PMUX_OFFSET 0x4000). */
    var PADCFG_LOCK_BASE = 0xF0000;
    dsR5_0.memory.writeWord(0, PADCFG_LOCK_BASE + 0x1008, 0x68EF3490);  /* LOCK0 KICK0 */
    dsR5_0.memory.writeWord(0, PADCFG_LOCK_BASE + 0x100C, 0xD172BC5A);  /* LOCK0 KICK1 */
    java.lang.Thread.sleep(1);
    print("  LOCK0 KICK0/KICK1 issued.");

    var PADCFG_BASE = 0xF4000;   /* PADCFG + PMUX_OFFSET */
    var PIN_FN_OUT  = 0x00010000;   /* MODE(0) | PULL_DIS(bit 16) */
    var PIN_FN_IN   = 0x00050000;   /* MODE(0) | INPUT_EN(bit 18) | PULL_DIS(bit 16) */
    dsR5_0.memory.writeWord(0, PADCFG_BASE + 0x0000, PIN_FN_OUT);  /* CLK  */
    dsR5_0.memory.writeWord(0, PADCFG_BASE + 0x002C, PIN_FN_OUT);  /* CSN0 */
    dsR5_0.memory.writeWord(0, PADCFG_BASE + 0x000C, PIN_FN_IN);   /* D0   */
    dsR5_0.memory.writeWord(0, PADCFG_BASE + 0x0010, PIN_FN_IN);   /* D1   */
    dsR5_0.memory.writeWord(0, PADCFG_BASE + 0x0014, PIN_FN_IN);   /* D2   */
    dsR5_0.memory.writeWord(0, PADCFG_BASE + 0x0018, PIN_FN_IN);   /* D3   */
    print("  OSPI0 pads written: CLK + CSN0 (out), D0..D3 (bidir).");
    print("  PADCFG[CLK]  = 0x" + java.lang.Long.toHexString(
            dsR5_0.memory.readWord(0, PADCFG_BASE + 0x0000) & 0xFFFFFFFF));
    print("  PADCFG[CSN0] = 0x" + java.lang.Long.toHexString(
            dsR5_0.memory.readWord(0, PADCFG_BASE + 0x002C) & 0xFFFFFFFF));
    print("  PADCFG[D0]   = 0x" + java.lang.Long.toHexString(
            dsR5_0.memory.readWord(0, PADCFG_BASE + 0x000C) & 0xFFFFFFFF));
    print("  PADCFG[D1]   = 0x" + java.lang.Long.toHexString(
            dsR5_0.memory.readWord(0, PADCFG_BASE + 0x0010) & 0xFFFFFFFF));
    /* Sanity: if read-back shows MODE 0 (bits[2:0]==0), the unlock worked. */
    var ckpad = dsR5_0.memory.readWord(0, PADCFG_BASE + 0x0000) & 0x7;
    if (ckpad != 0) {
        print("  [WARN] CLK pad mode = " + ckpad + " (expected 0). LOCK still active?");
    } else {
        print("  CLK pad MODE 0 confirmed — PADCFG unlock worked.");
    }
    print("");

    print("=== Step 2: Configure OSPI controller for 1S-1S-1S SDR + CS0 ===");
    /* CONFIG_REG fixes:
     *   bit  7    ENB_DIR_ACC_CTLR     → 0 (indirect mode for STIG)
     *   bits 13:10 PERIPH_CS_LINES     → 0xE (CS0 active, others inactive
     *                                   per `OSPI_CHIP_SELECT(0) = ~(1<<0) & 0xF`
     *                                   in ospi_lld.h:214 — active-low encoding)
     *   bits 22:19 MSTR_BAUD_DIV        → 0xF (slowest = /32, safest for recovery)
     *   bit  0    ENB_SPI              → 1 (keep controller enabled)
     *
     * The crucial fix vs prior runs: PERIPH_CS_LINES was 0x0, which
     * asserts ALL CS lines simultaneously — chip can't see a clean
     * single-CS edge, so doesn't respond to any STIG command. RDID and
     * SR1 returned 0x00 because the controller never actually clocked
     * a valid transaction to the chip. */
    var CONFIG_PERIPH_CS_LINES_MASK  = 0x00003C00;
    var CONFIG_PERIPH_CS_LINES_CS0   = 0x00003800;   /* 0xE << 10 */
    var cfg = dsR5_0.memory.readWord(0, REG_CONFIG);
    print("  CONFIG_REG before               = 0x" + java.lang.Long.toHexString(cfg & 0xFFFFFFFF));
    cfg &= ~CONFIG_ENB_DIR_ACC_CTLR_BIT;
    cfg &= ~CONFIG_PERIPH_CS_LINES_MASK;
    cfg |=  CONFIG_PERIPH_CS_LINES_CS0;
    dsR5_0.memory.writeWord(0, REG_CONFIG, cfg);
    print("  CONFIG_REG after                = 0x" + java.lang.Long.toHexString(
            dsR5_0.memory.readWord(0, REG_CONFIG) & 0xFFFFFFFF));

    /* DEV_INSTR_RD_CONFIG_REG / DEV_INSTR_WR_CONFIG_REG: explicitly set
     * to post-DMSC-reset defaults (cmd 0x03 1S READ / cmd 0x02 1S PP).
     * Without these set, STIG ops have weird behavior — empirically
     * confirmed in the 2nd recovery attempt when both regs were 0x0. */
    var rdcfg = dsR5_0.memory.readWord(0, REG_DEV_INSTR_RD_CONFIG);
    var wrcfg = dsR5_0.memory.readWord(0, REG_DEV_INSTR_WR_CONFIG);
    print("  DEV_INSTR_RD_CONFIG before      = 0x" + java.lang.Long.toHexString(rdcfg & 0xFFFFFFFF));
    print("  DEV_INSTR_WR_CONFIG before      = 0x" + java.lang.Long.toHexString(wrcfg & 0xFFFFFFFF));
    dsR5_0.memory.writeWord(0, REG_DEV_INSTR_RD_CONFIG, 0x03);
    dsR5_0.memory.writeWord(0, REG_DEV_INSTR_WR_CONFIG, 0x02);
    print("  DEV_INSTR_RD_CONFIG after       = 0x" + java.lang.Long.toHexString(
            dsR5_0.memory.readWord(0, REG_DEV_INSTR_RD_CONFIG) & 0xFFFFFFFF));
    print("  DEV_INSTR_WR_CONFIG after       = 0x" + java.lang.Long.toHexString(
            dsR5_0.memory.readWord(0, REG_DEV_INSTR_WR_CONFIG) & 0xFFFFFFFF));
    print("");

    print("=== Step 2a2: HARDWARE /RESET pulse via OSPI controller's reset-pin output ===");
    /* CONFIG_REG bit 6 = RESET_CFG_FLD (enable /RESET pin output mode).
     * CONFIG_REG bit 5 = RESET_PIN_FLD (the level the controller drives).
     * On LP-AM2434 the OSPI flash chip's /RESET pin is wired to the SoC's
     * OSPI_RESET output (assuming TI reference layout). Pulsing /RESET
     * low forces a chip hardware reset that BYPASSES all volatile chip
     * state including any latched-into-bad-protocol-mode condition. The
     * chip's NV state survives (so CFR3N=0x00 is still wrong after) but
     * the chip will re-enter its boot-up I/O config which is 1S-SDR. */
    var cfgRst = dsR5_0.memory.readWord(0, REG_CONFIG);
    /* Step 1: enable RESET pin output, drive HIGH (idle). */
    cfgRst |= (1 << 6);              /* RESET_CFG = 1 */
    cfgRst |= (1 << 5);              /* RESET_PIN = 1 (idle high) */
    dsR5_0.memory.writeWord(0, REG_CONFIG, cfgRst);
    java.lang.Thread.sleep(1);
    /* Step 2: assert /RESET low for ~5 ms. */
    cfgRst &= ~(1 << 5);             /* RESET_PIN = 0 (assert) */
    dsR5_0.memory.writeWord(0, REG_CONFIG, cfgRst);
    java.lang.Thread.sleep(5);
    /* Step 3: release /RESET high; wait tRPH. */
    cfgRst |= (1 << 5);              /* RESET_PIN = 1 (release) */
    dsR5_0.memory.writeWord(0, REG_CONFIG, cfgRst);
    java.lang.Thread.sleep(10);
    /* Step 4: disable RESET output so subsequent ops can use the pin
     * for its normal purpose if applicable. */
    cfgRst &= ~(1 << 6);
    dsR5_0.memory.writeWord(0, REG_CONFIG, cfgRst);
    java.lang.Thread.sleep(10);
    print("  /RESET pulse issued (5 ms low → 10 ms tRPH).");
    print("  CONFIG_REG after = 0x" + java.lang.Long.toHexString(
            dsR5_0.memory.readWord(0, REG_CONFIG) & 0xFFFFFFFF));
    print("");

    print("=== Step 2b: RDPD wake-up (cmd 0xAB) — in case chip is in deep power down ===");
    /* RDPD also has the side-effect of waking from DPD; harmless on a
     * chip that's already awake. The chip needs ~30 µs after this
     * before it'll accept further commands. */
    execStig(dsR5_0, 0xAB);
    java.lang.Thread.sleep(2);
    print("  RDPD issued.");
    print("");

    print("=== Step 2b2: BLIND RSTEN+RST — force chip back to default I/O mode ===");
    execStig(dsR5_0, 0x66);  /* RSTEN */
    java.lang.Thread.sleep(1);
    execStig(dsR5_0, 0x99);  /* RST */
    java.lang.Thread.sleep(50);
    print("  RSTEN+RST issued blind.");
    print("");

    print("=== Step 2b3: Mode Bit Reset (cmd 0xFF) — exit continuous-read / performance-enhance ===");
    /* Some Cypress states latch a continuous-read or PE-mode bit that
     * makes the chip ignore opcodes and just stream data. Cmd 0xFF is
     * "Reset Default I/O Mode" per Cypress S25HL datasheet — it works
     * regardless of current chip-internal state. Issue twice to be
     * safe (some chips require two consecutive 0xFF bytes to detect
     * the pattern). */
    execStig(dsR5_0, 0xFF);
    java.lang.Thread.sleep(1);
    execStig(dsR5_0, 0xFF);
    java.lang.Thread.sleep(5);
    /* Re-issue RSTEN+RST after the mode reset, in case 0xFF re-enabled
     * some default that needs another reset to settle. */
    execStig(dsR5_0, 0x66);
    java.lang.Thread.sleep(1);
    execStig(dsR5_0, 0x99);
    java.lang.Thread.sleep(50);
    print("  Mode-bit reset (0xFF×2) + RSTEN+RST issued.");
    print("");

    print("=== Step 2b4: MULTI-PROTOCOL blind RSTEN+RST sweep ===");
    /* 2026-05-15: S24L0707 doesn't respond to ANY 1S-SDR command. The chip
     * may be stuck in QPI / QPI+DTR / OPI / OPI+DTR mode from before the
     * brick session — maybe the brick code's WRRSB wrote CFR2N=0x08 which
     * on this chip rev enables some persistent multi-line mode we don't
     * know about. Sweep through every protocol the AM2434 OSPI controller
     * can speak; for each, issue RSTEN+RST (which the chip MUST recognize
     * in its current mode per Cypress spec); then restore controller to
     * 1S-SDR and try RDID. One of the sweeps should match the chip's
     * current mode, reset it back to factory I/O, and let 1S-SDR RDID
     * succeed. INSTR_TYPE encoding in DEV_INSTR_RD_CONFIG[9:8]:
     *   0=1S, 1=2S, 2=4S, 3=8S; bit 10 = DDR_EN. */
    function tryProtocolReset(target, instrType, ddrEn, label) {
        /* OPCODE field doesn't matter (STIG ctrl reg overrides); set
         * INSTR_TYPE + DDR_EN; keep ADDR/DATA xfer at 0 (don't matter). */
        var rd = 0x03 | (instrType << 8) | (ddrEn << 10);
        target.memory.writeWord(0, REG_DEV_INSTR_RD_CONFIG, rd);
        java.lang.Thread.sleep(1);
        execStig(target, 0x66);   /* RSTEN in protocol */
        java.lang.Thread.sleep(2);
        execStig(target, 0x99);   /* RST */
        java.lang.Thread.sleep(50);
        /* Back to 1S-SDR for RDID probe. */
        target.memory.writeWord(0, REG_DEV_INSTR_RD_CONFIG, 0x03);
        java.lang.Thread.sleep(2);
        var idraw = execStigRead(target, 0x9F, 3);
        var b0 = (idraw & 0xFF);
        var b1 = ((idraw >> 8) & 0xFF);
        var b2 = ((idraw >> 16) & 0xFF);
        print("  protocol=" + label + " → RDID = 0x"
              + java.lang.Long.toHexString(b0) + " 0x"
              + java.lang.Long.toHexString(b1) + " 0x"
              + java.lang.Long.toHexString(b2));
        return (b0 == 0x34);
    }
    var found = false;
    if (tryProtocolReset(dsR5_0, 1, 0, "2S-SDR"))     { found = true; print("  *** 2S-SDR reset successful ***"); }
    if (!found && tryProtocolReset(dsR5_0, 2, 0, "4S-SDR (QPI)")) { found = true; print("  *** 4S-SDR (QPI) reset successful ***"); }
    if (!found && tryProtocolReset(dsR5_0, 2, 1, "4S-DTR (QPI+DTR)")) { found = true; print("  *** 4S-DTR reset successful ***"); }
    if (!found && tryProtocolReset(dsR5_0, 3, 0, "8S-SDR (OPI)")) { found = true; print("  *** 8S-SDR (OPI) reset successful ***"); }
    if (!found && tryProtocolReset(dsR5_0, 3, 1, "8S-DTR (OPI+DTR)")) { found = true; print("  *** 8S-DTR reset successful ***"); }
    if (!found) {
        print("  No protocol matched — chip not responding to RSTEN+RST in any mode.");
    } else {
        print("  Chip is now in 1S-SDR factory mode. Continuing recovery.");
    }
    print("");

    print("=== Step 2c: RDID (cmd 0x9F) — verify STIG actually clocks bytes ===");
    /* Cypress S25HL512T returns 0x34 0x2A 0x1A (manuf, device-id hi, lo).
     * 2026-05-15: S24L0707 returns 0x00 even after blind RSTEN+RST. We
     * suspect chip is in a non-standard read protocol where 1S RDID
     * doesn't clock back correctly, but writes may still land. Try WREN
     * → RDSR-for-WEL as a separate liveness probe. If chip latches WEL,
     * the WRRSB to fix CFR3N will land and a final RSTEN+RST will recover. */
    var idraw = execStigRead(dsR5_0, 0x9F, 3);
    var idBytes = [(idraw & 0xFF), ((idraw >> 8) & 0xFF), ((idraw >> 16) & 0xFF)];
    print("  RDID = 0x" + java.lang.Long.toHexString(idBytes[0])
          + " 0x" + java.lang.Long.toHexString(idBytes[1])
          + " 0x" + java.lang.Long.toHexString(idBytes[2])
          + "  (expected 0x34 0x2A 0x1A for S25HL512T)");
    if (idBytes[0] != 0x34) {
        print("  RDID failed — falling back to WREN-liveness probe (writes might still work)");
        /* WREN → RDSR; if WEL gets set, chip accepts writes even though
         * it can't read out the ID cleanly. */
        execStig(dsR5_0, 0x06);   /* WREN */
        java.lang.Thread.sleep(2);
        var sr1probe = execStigRead(dsR5_0, 0x05, 1) & 0xFF;
        print("  post-WREN SR1 = 0x" + java.lang.Long.toHexString(sr1probe));
        if ((sr1probe & 0x02) == 0) {
            print("[ERROR] WEL not latched after WREN — chip not accepting writes either.");
            print("        Most likely culprits in priority order:");
            print("        1. Chip is in QPI/OPI/DTR mode and 1S-SDR commands don't reach it");
            print("           → controller needs to send commands in matching protocol");
            print("        2. Physical fault — chip not powered, MISO stuck low, dead chip");
            print("        3. Wrong board (SW4 not in NOBOOT mode, or different PCB rev)");
            dsR5_0.target.disconnect();
            java.lang.System.exit(3);
        }
        print("  WEL is set — chip is accepting writes. Proceeding BLIND.");
    } else {
        print("  STIG fully working — chip is responsive to real commands.");
    }
    print("");

    print("=== Step 2d: Read CFR2V / CFR3V / CFR4V — current chip state ===");
    /* RDAR (cmd 0x65) with 3-byte address. CFR2V is at 0x800003,
     * CFR3V at 0x800004, CFR4V at 0x800005. The 1S RDAR needs 8 dummy
     * cycles per Cypress datasheet (default latency code).
     *
     * NB: V (volatile) registers reflect the chip's CURRENT state, not
     * what's in NV. They're useful for sanity, but the NV registers
     * (N suffix) are what survive power cycles. We read V because RDAR
     * for V is simpler (the chip honours it more readily). */
    var cfr2v = execStigReadAddr(dsR5_0, 0x65, 0x800003, 3, 8, 1) & 0xFF;
    var cfr3v = execStigReadAddr(dsR5_0, 0x65, 0x800004, 3, 8, 1) & 0xFF;
    var cfr4v = execStigReadAddr(dsR5_0, 0x65, 0x800005, 3, 8, 1) & 0xFF;
    print("  CFR2V = 0x" + java.lang.Long.toHexString(cfr2v));
    print("  CFR3V = 0x" + java.lang.Long.toHexString(cfr3v));
    print("  CFR4V = 0x" + java.lang.Long.toHexString(cfr4v));
    print("  (factory-default targets after recovery: 0x08 / 0x08 / 0x08)");
    print("");

    print("=== Step 3: Read SR1 (RDSR cmd 0x05) — pre-WREN sanity check ===");
    var sr1raw = execStigRead(dsR5_0, 0x05, 1);
    if (sr1raw == -1) {
        print("[ERROR] RDSR failed");
        dsR5_0.target.disconnect();
        java.lang.System.exit(3);
    }
    var sr1 = sr1raw & 0xFF;
    print("  SR1 = 0x" + java.lang.Long.toHexString(sr1));
    print("");

    print("=== Step 4: WREN (cmd 0x06) ===");
    if (!execStig(dsR5_0, 0x06)) {
        print("[ERROR] WREN STIG timeout");
        dsR5_0.target.disconnect();
        java.lang.System.exit(3);
    }
    /* Verify WEL set */
    var sr1afterWren = execStigRead(dsR5_0, 0x05, 1) & 0xFF;
    print("  SR1 after WREN = 0x" + java.lang.Long.toHexString(sr1afterWren)
          + "   (WEL=" + ((sr1afterWren >> 1) & 1) + ")");
    if ((sr1afterWren & 0x02) == 0) {
        print("[ERROR] WEL not set after WREN — chip rejecting commands");
        dsR5_0.target.disconnect();
        java.lang.System.exit(3);
    }
    print("");

    print("=== Step 5: WRRSB (cmd 0x01) with 5 bytes: 00 00 08 08 08 ===");
    /* Bytes (in order written to chip): STR1N=0x00, CFR1N=0x00,
     * CFR2N=0x08, CFR3N=0x08, CFR4N=0x08.
     * WR_DATA_LOWER  = (CFR3N<<24) | (CFR2N<<16) | (CFR1N<<8) | STR1N
     *                = 0x08080000
     * WR_DATA_UPPER  = CFR4N
     *                = 0x00000008 */
    if (!execStigWrite(dsR5_0, 0x01, 0x08080000, 0x00000008, 5)) {
        print("[ERROR] WRRSB STIG timeout");
        dsR5_0.target.disconnect();
        java.lang.System.exit(3);
    }
    print("  WRRSB issued.");
    print("");

    print("=== Step 6: Poll WIP (NV write can take up to 500 ms) ===");
    var wipPolls = 0;
    var sr1poll = 0xFF;
    for (wipPolls = 0; wipPolls < 200; wipPolls++) {
        var w = execStigRead(dsR5_0, 0x05, 1);
        if (w == -1) {
            print("[WARN] RDSR poll #" + wipPolls + " failed");
            java.lang.Thread.sleep(10);
            continue;
        }
        sr1poll = w & 0xFF;
        if ((sr1poll & 0x01) == 0) break;   /* WIP clear */
        java.lang.Thread.sleep(10);
    }
    print("  WIP cleared after " + wipPolls + " polls (~"
          + (wipPolls * 10) + " ms), SR1 = 0x"
          + java.lang.Long.toHexString(sr1poll));
    if ((sr1poll & 0x01) != 0) {
        print("[ERROR] WIP still set after 2 s — NV write didn't complete");
        dsR5_0.target.disconnect();
        java.lang.System.exit(3);
    }
    print("");

    print("=== Step 7: RSTEN (0x66) + RST (0x99) ===");
    if (!execStig(dsR5_0, 0x66)) {
        print("[WARN] RSTEN STIG timeout — continuing anyway");
    }
    if (!execStig(dsR5_0, 0x99)) {
        print("[WARN] RST STIG timeout — continuing anyway");
    }
    /* Cypress tRPH ~35 µs; wait 1 ms to be safe. */
    java.lang.Thread.sleep(2);
    print("  Chip reset issued.");
    print("");

    print("=== Step 8: Verify NV registers — read CFR2N / CFR3N / CFR4N ===");
    /* After RSTEN+RST the chip reloads V from N. So V should now match
     * what we wrote to N. Cross-check by reading V via RDAR @ 0x80000X
     * AND N via RDAR @ 0x00000X. Both should show 0x08 for CFR2/3/4. */
    var cfr2v_post = execStigReadAddr(dsR5_0, 0x65, 0x800003, 3, 8, 1) & 0xFF;
    var cfr3v_post = execStigReadAddr(dsR5_0, 0x65, 0x800004, 3, 8, 1) & 0xFF;
    var cfr4v_post = execStigReadAddr(dsR5_0, 0x65, 0x800005, 3, 8, 1) & 0xFF;
    var cfr2n_post = execStigReadAddr(dsR5_0, 0x65, 0x000003, 3, 8, 1) & 0xFF;
    var cfr3n_post = execStigReadAddr(dsR5_0, 0x65, 0x000004, 3, 8, 1) & 0xFF;
    var cfr4n_post = execStigReadAddr(dsR5_0, 0x65, 0x000005, 3, 8, 1) & 0xFF;
    print("  Volatile  CFR2V/3V/4V = 0x" + java.lang.Long.toHexString(cfr2v_post)
          + " / 0x" + java.lang.Long.toHexString(cfr3v_post)
          + " / 0x" + java.lang.Long.toHexString(cfr4v_post));
    print("  Nonvolat. CFR2N/3N/4N = 0x" + java.lang.Long.toHexString(cfr2n_post)
          + " / 0x" + java.lang.Long.toHexString(cfr3n_post)
          + " / 0x" + java.lang.Long.toHexString(cfr4n_post));
    var ok = (cfr2n_post == 0x08) && (cfr3n_post == 0x08) && (cfr4n_post == 0x08);
    if (ok) {
        print("  ✓ Chip NV is now in clean factory-default state.");
    } else {
        print("  ✗ WRRSB did NOT take. NV values are wrong — recovery failed.");
        print("    If CFR3N==0x00 specifically, the chip rejected our WRRSB");
        print("    sequence and is in a deeper wedge than this script can fix.");
    }
    print("");

    print("=== DONE ===");
    print("");
    print("Next steps:");
    print("  1. Set SW4 back to OSPI boot mode (1+2 ON for PCB 190A,");
    print("     or 2+6 ON for older revs).");
    print("  2. Power-cycle the LP (full 30 s unplug).");
    print("  3. Re-run Flash-LP.ps1 to reflash the firmware.");
    print("");

    try { dsR5_0.target.disconnect(); } catch (e) { print("disconnect: " + e); }
}

var ds, debugServer, script;
var withinCCS = (ds !== undefined);
if (!withinCCS) {
    script = ScriptingEnvironment.instance();
    debugServer = script.getServer("DebugServer.1");
    debugServer.setConfig(ccxmlPath);
    go();
} else {
    debugServer = ds;
    script = ScriptingEnvironment.instance();
    go();
}
