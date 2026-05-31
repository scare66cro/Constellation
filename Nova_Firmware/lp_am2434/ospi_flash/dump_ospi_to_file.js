/*
 * dump_ospi_to_file.js — JTAG-only dump of an OSPI region to a host file.
 *
 * Reads OSPI flash via memory-mapped XIP at 0x60000000 + offset, after
 * loading the auto-flasher (which does OSPI controller init + DAC enable).
 * No DIP switches, no UART boot, no USB cycling — pure JTAG.
 *
 * Usage (headless):
 *   $env:DUMP_OFFSET = "0x000000"      # OSPI byte offset (default 0)
 *   $env:DUMP_SIZE   = "0x060000"      # bytes to read (default 384 KB = SBL region)
 *   $env:DUMP_FILE   = "F:/Constellation/backups/sbl_stock_A.bin"
 *   $env:LP_CCXML    = "F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP_A.ccxml"
 *   "C:\ti\ccs2050\ccs\ccs_base\scripting\bin\dss.bat" dump_ospi_to_file.js
 *
 * Output format: raw bytes, no header, no padding. Re-flashable directly
 * via uniflash_run.js with UNIFLASH_FILE=<this file> + UNIFLASH_OFFSET=
 * matching DUMP_OFFSET.
 *
 * Implementation:
 *   1. Load sciclient_ccs_init (DMSC board config for HS-FS)
 *   2. Load the auto-flasher (sbl_jtag_uniflash.release.out) onto R5F0-0
 *   3. Run flasher async — it does Drivers_open → Board_driversOpen →
 *      OSPI controller init → DAC enable → poll loop. Once it's in the
 *      poll loop, OSPI memory-mapped XIP region at 0x60000000 is live.
 *   4. Halt R5F0-0 (so JTAG memory reads are safe)
 *   5. memory.readData(0x60000000 + DUMP_OFFSET, DUMP_SIZE) — JTAG read
 *      via DAP, comes back as a Java byte array
 *   6. Write to DUMP_FILE
 *   7. Disconnect
 *
 * Independent read path from the flasher's Flash_read — catches silent
 * write corruption that pure Flash_read would miss when the controller
 * isn't in DAC mode.
 */

var sdkPath      = "C:/ti/mcu_plus_sdk_am243x_12_00_00_26";
var ccxmlPath    = java.lang.System.getenv("LP_CCXML")
                || "F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP.ccxml";
var ccs_init_elf = sdkPath + "/tools/ccs_load/am243x/sciclient_ccs_init.release.out";
var flasher_elf  = "F:/Constellation/Nova_Firmware/lp_am2434/flasher_uart/ti-arm-clang/sbl_jtag_uniflash.release.out";

importPackage(Packages.com.ti.debug.engine.scripting);
importPackage(Packages.com.ti.ccstudio.scripting.environment);
importPackage(Packages.java.lang);
importPackage(java.io);
importPackage(java.lang);

function go() {
    script.setScriptTimeout(600 * 1000);  // 10 min

    var offsetStr = java.lang.System.getenv("DUMP_OFFSET");
    var sizeStr   = java.lang.System.getenv("DUMP_SIZE");
    var fileStr   = java.lang.System.getenv("DUMP_FILE");

    if (offsetStr === null) offsetStr = "0x000000";
    if (sizeStr   === null) sizeStr   = "0x060000";
    if (fileStr   === null) {
        print("[ERROR] Set DUMP_FILE env var (target host path).");
        java.lang.System.exit(2);
    }

    var ospiOffset = parseInt(offsetStr, 16);
    var dumpSize   = parseInt(sizeStr, 16);
    var xipAddr    = 0x60000000 + ospiOffset;

    print("=== dump_ospi_to_file ===");
    print("  ccxml:       " + ccxmlPath);
    print("  ospi offset: 0x" + java.lang.Long.toHexString(ospiOffset));
    print("  size:        " + dumpSize + " bytes (0x" + java.lang.Long.toHexString(dumpSize) + ")");
    print("  xip addr:    0x" + java.lang.Long.toHexString(xipAddr));
    print("  out file:    " + fileStr);

    /* Standard sciclient init + flasher load — same dance as uniflash_run.js
     * up to the point where the flasher is in its poll loop. */
    print("=== Step 1: DMSC board config (HS-FS) ===");
    var dsR5_0 = debugServer.openSession(".*MAIN_Cortex_R5_0_0");
    dsR5_0.target.connect();
    try { dsR5_0.target.halt(); } catch (e) { print("[step1] pre-halt: " + e); }
    try { dsR5_0.target.reset(); } catch (e) { print("[step1] pre-reset: " + e); }
    try { dsR5_0.target.halt(); } catch (e) { print("[step1] post-reset halt: " + e); }
    dsR5_0.memory.fill(0x78000000, 0, 0x2000, 0);
    dsR5_0.memory.writeWord(0, 0x78000000, 0xE59FF004);
    dsR5_0.memory.writeWord(0, 0x78000004, 0x38);
    dsR5_0.memory.writeWord(0, 0x78000038, 0xEAFFFFFE);
    dsR5_0.target.halt();
    dsR5_0.target.reset();
    dsR5_0.memory.loadProgram(ccs_init_elf);
    dsR5_0.target.halt();
    dsR5_0.target.runAsynch();
    java.lang.Thread.sleep(5000);
    dsR5_0.target.halt();
    print("  DMSC init done.");

    print("=== Step 2: Load auto-flasher ===");
    dsR5_0.target.reset();
    dsR5_0.target.halt();
    dsR5_0.memory.loadProgram(flasher_elf);
    dsR5_0.target.runAsynch();
    /* Give the flasher time to do Drivers_open + Board_driversOpen +
     * OSPI XIP setup before we halt and read. The poll loop is
     * Drivers_open-clean within ~2 s; we wait a generous 5 s. */
    java.lang.Thread.sleep(5000);
    dsR5_0.target.halt();
    print("  Flasher halted in poll loop.");

    /* ─── Enable OSPI DAC mode for XIP reads ─────────────────────────
     * The flasher's Drivers_open puts the controller in indirect mode
     * (the flasher only uses Flash_read/Flash_write which are STIG/
     * indirect ops). XIP reads at 0x60000000 require DAC mode bit set
     * in CONFIG_REG (HAL_OSPI_BASE = 0x0FC40000, bit 7 = ENB_DIR_ACC).
     * Without this, all XIP reads return 0x00.
     *
     * Discovered 2026-05-31 when dump_ospi_to_file.js returned all
     * zeros from 0x60300000 — same code path that worked yesterday
     * for OSPI 0x60000000 (the SBL region). Difference: dump was
     * being attempted DEEPER into the chip, where the auto-flasher's
     * implicit XIP window must not extend. The fix (enable DAC bit
     * + write 0x04000000 to IND_AHB_ADDR_TRIGGER) mirrors what
     * hal_flash_dac_pp_one does on every write call.
     *
     * Safe because we're halted — no concurrent ops. */
    var HAL_OSPI_CONFIG_REG               = 0x0FC40000;
    var HAL_OSPI_IND_AHB_ADDR_TRIGGER     = 0x0FC4001C;
    var HAL_OSPI_CFG_ENB_DIR_ACC_CTLR_BIT = 0x80;
    var cfg_pre = dsR5_0.memory.readData(0, HAL_OSPI_CONFIG_REG, 32) & 0xFFFFFFFF;
    print("  CONFIG_REG before: 0x" + java.lang.Long.toHexString(cfg_pre & 0xFFFFFFFF));
    if ((cfg_pre & HAL_OSPI_CFG_ENB_DIR_ACC_CTLR_BIT) == 0) {
        dsR5_0.memory.writeWord(0, HAL_OSPI_CONFIG_REG, cfg_pre | HAL_OSPI_CFG_ENB_DIR_ACC_CTLR_BIT);
        dsR5_0.memory.writeWord(0, HAL_OSPI_IND_AHB_ADDR_TRIGGER, 0x04000000);
        var cfg_post = dsR5_0.memory.readData(0, HAL_OSPI_CONFIG_REG, 32) & 0xFFFFFFFF;
        print("  CONFIG_REG after enable: 0x" + java.lang.Long.toHexString(cfg_post & 0xFFFFFFFF));
    } else {
        print("  DAC already enabled.");
    }

    print("=== Step 3: Read OSPI XIP region (byte-by-byte) ===");
    /* The 4-arg readData(pageId, addr, sizeBits, count) overload returns
     * zeros on the AM2434 XIP region for reasons not yet diagnosed.
     * The proven-working pattern from ospi_read_dump.js is to read one
     * byte at a time with the 3-arg readData(pageId, addr, sizeBits).
     * Slow (~30 s per 384 KB on this workstation) but reliable. */
    var fos = new FileOutputStream(fileStr);
    var buf = java.lang.reflect.Array.newInstance(java.lang.Byte.TYPE, 4096);
    var bufIdx = 0;
    var progressEvery = 0x10000;  // 64 KB
    for (var i = 0; i < dumpSize; i++) {
        var b = dsR5_0.memory.readData(0, xipAddr + i, 8) & 0xff;
        /* Java byte is signed [-128..127]; values >127 need wrapping
         * to the negative half so the byte-array assignment is in range. */
        if (b > 127) b = b - 256;
        buf[bufIdx++] = b;
        if (bufIdx == 4096) {
            fos.write(buf, 0, 4096);
            bufIdx = 0;
        }
        if ((i + 1) % progressEvery == 0) {
            print("  ... " + ((i + 1) >> 10) + " KB / " + (dumpSize >> 10) + " KB");
        }
    }
    if (bufIdx > 0) {
        fos.write(buf, 0, bufIdx);
    }
    fos.close();
    print("  Wrote " + dumpSize + " bytes to " + fileStr);

    dsR5_0.target.disconnect();
    print("=== Done ===");
}

var ScriptingEnvironment =
    Packages.com.ti.ccstudio.scripting.environment.ScriptingEnvironment;
var ds, debugServer, script;
var withinCCS = (ds !== undefined);
if (!withinCCS) {
    script = ScriptingEnvironment.instance();
    debugServer = script.getServer("DebugServer.1");
    debugServer.setConfig(ccxmlPath);
    try {
        go();
    } catch (e) {
        print("[FATAL] " + e);
        java.lang.System.exit(1);
    }
    java.lang.System.exit(0);
} else {
    debugServer = ds;
    script = env;
    try {
        go();
    } catch (e) {
        print("[FATAL] " + e);
        java.lang.System.exit(1);
    }
}
