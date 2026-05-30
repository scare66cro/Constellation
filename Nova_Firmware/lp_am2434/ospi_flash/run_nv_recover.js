/*
 * run_nv_recover.js — DSS driver for the OSPI_NV_RECOVER auto-flasher build.
 *
 * Loads `sbl_jtag_uniflash.release.out` (built with -DOSPI_NV_RECOVER) onto
 * R5FSS0_0 via JTAG, lets it run, polls `g_ospi_nv_recover.magic_done` for
 * the completion sentinel (0xF1A5DA7A), then reports the diagnostic
 * struct (RDID, SR1, CFR2/3/4V before, CFR2/3/4N after).
 *
 * Required state: LP in NOBOOT mode (SW4: 1, 2, 3, 4 ON), USB-C plugged
 * in, only the target XDS110 probe enumerated.
 */

var sdkPath      = "C:/ti/mcu_plus_sdk_am243x_12_00_00_26";
var ccxmlPath    = java.lang.System.getenv("LP_CCXML")
                || "F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP_B.ccxml";
var ccs_init_elf = sdkPath + "/tools/ccs_load/am243x/sciclient_ccs_init.release.out";
var recover_elf  = "F:/Constellation/Nova_Firmware/lp_am2434/flasher_uart/ti-arm-clang/sbl_jtag_uniflash.release.out";

var STATUS_NAMES = {
    0x00: "SUCCESS — CFR2/3/4N all 0x08",
    0xE1: "ERROR — RDID returned wrong manufacturer ID (chip not responding)",
    0xE2: "ERROR — WEL not set after WREN (chip not accepting writes)",
    0xE3: "ERROR — NV registers didn't take after WRRSB",
    0xFF: "ERROR — OSPI handle was NULL after Drivers_open"
};

importPackage(Packages.com.ti.debug.engine.scripting);
importPackage(Packages.com.ti.ccstudio.scripting.environment);
importPackage(Packages.java.lang);

function go() {
    script.setScriptTimeout(120 * 1000);

    print("=== OSPI NV Recovery (on-target sbl_jtag_uniflash -DOSPI_NV_RECOVER) ===");
    print("  ccxml:    " + ccxmlPath);
    print("  recover:  " + recover_elf);
    print("");

    /* Step 1: same ritual as ospi_nv_recover.js — halt+reset+spin-loop
     * before loadProgram so the R5 has a sane state. */
    print("=== Step 1: open session, prime R5, load sciclient_ccs_init ===");
    var dsR5_0 = debugServer.openSession(".*MAIN_Cortex_R5_0_0");
    dsR5_0.target.connect();
    try { dsR5_0.target.halt(); } catch (e) { print("[step1] pre-halt: " + e); }
    try { dsR5_0.target.reset(); } catch (e) { print("[step1] pre-reset: " + e); }
    try { dsR5_0.target.halt(); } catch (e) { print("[step1] post-reset halt: " + e); }
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

    /* Step 2: load the recovery binary. Reset+halt required between
     * loadPrograms in the same session — DSS auto-runs to main after
     * loadProgram and waits for halt. Without a fresh reset the prior
     * binary's state interferes. */
    print("=== Step 2: load OSPI_NV_RECOVER binary ===");
    dsR5_0.target.reset();
    dsR5_0.target.halt();
    dsR5_0.memory.loadProgram(recover_elf);
    print("  binary loaded; PC at main (halted).");

    /* Find the struct symbol BEFORE running so we know where to poll. */
    var structAddr = dsR5_0.symbol.getAddress("g_ospi_nv_recover");
    print("  g_ospi_nv_recover @ 0x" + java.lang.Long.toHexString(structAddr));
    print("");

    /* Offsets per OspiNvRecover_t in main.c:136. */
    var OFF_MAGIC      = 0;
    var OFF_STATUS     = 4;
    var OFF_ID_MANUF   = 8;
    var OFF_ID_HI      = 9;
    var OFF_ID_LO      = 10;
    var OFF_SR1_PRE    = 11;
    var OFF_SR1_WREN   = 12;
    var OFF_CFR2V_PRE  = 13;
    var OFF_CFR3V_PRE  = 14;
    var OFF_CFR4V_PRE  = 15;
    var OFF_CFR2N_POST = 16;
    var OFF_CFR3N_POST = 17;
    var OFF_CFR4N_POST = 18;
    var OFF_WIP_POLLS  = 19;
    var DONE_MAGIC     = 0xF1A5DA7A;

    /* Zero out the result struct so we know what changed. */
    dsR5_0.memory.writeWord(0, structAddr + OFF_MAGIC, 0);
    dsR5_0.memory.writeWord(0, structAddr + OFF_STATUS, 0);

    print("=== Step 3: run target ===");
    dsR5_0.target.runAsynch();

    /* Poll magic_done for up to 30 seconds. */
    var deadline = java.lang.System.currentTimeMillis() + 30000;
    var done = false;
    var magic = 0;
    while (java.lang.System.currentTimeMillis() < deadline) {
        java.lang.Thread.sleep(500);
        try {
            dsR5_0.target.halt();
            magic = dsR5_0.memory.readWord(0, structAddr + OFF_MAGIC) >>> 0;
            if (magic == DONE_MAGIC) {
                done = true;
                break;
            }
            dsR5_0.target.runAsynch();
        } catch (e) {
            print("  poll error: " + e);
        }
    }
    if (!done) {
        try { dsR5_0.target.halt(); } catch (e) {}
        print("[ERROR] recovery timed out after 30 s (magic=0x" +
              java.lang.Long.toHexString(magic) + ")");
        dsR5_0.target.disconnect();
        java.lang.System.exit(2);
    }

    /* Read result struct. readData(page, addr, bits) — bits = 8 for u8. */
    function r8(off) { return dsR5_0.memory.readData(0, structAddr + off, 8) & 0xFF; }
    var status      = dsR5_0.memory.readWord(0, structAddr + OFF_STATUS) >>> 0;
    var idManuf     = r8(OFF_ID_MANUF);
    var idHi        = r8(OFF_ID_HI);
    var idLo        = r8(OFF_ID_LO);
    var sr1Pre      = r8(OFF_SR1_PRE);
    var sr1Wren     = r8(OFF_SR1_WREN);
    var cfr2vPre    = r8(OFF_CFR2V_PRE);
    var cfr3vPre    = r8(OFF_CFR3V_PRE);
    var cfr4vPre    = r8(OFF_CFR4V_PRE);
    var cfr2nPost   = r8(OFF_CFR2N_POST);
    var cfr3nPost   = r8(OFF_CFR3N_POST);
    var cfr4nPost   = r8(OFF_CFR4N_POST);
    var wipPolls    = r8(OFF_WIP_POLLS);

    function hex(v, w) {
        var s = java.lang.Long.toHexString(v);
        while (s.length < (w || 2)) s = "0" + s;
        return s;
    }

    print("");
    print("=== Recovery result ===");
    print("  RDID:        0x" + hex(idManuf) + " 0x" + hex(idHi) + " 0x" + hex(idLo)
          + "   (expected 0x34 0x2A 0x1A for S25HL512T)");
    print("  SR1 pre:     0x" + hex(sr1Pre));
    print("  SR1 +WREN:   0x" + hex(sr1Wren) + "   (expect bit 1 = WEL = 1)");
    print("  CFR2V pre:   0x" + hex(cfr2vPre));
    print("  CFR3V pre:   0x" + hex(cfr3vPre));
    print("  CFR4V pre:   0x" + hex(cfr4vPre));
    print("  CFR2N post:  0x" + hex(cfr2nPost) + "   (target 0x08)");
    print("  CFR3N post:  0x" + hex(cfr3nPost) + "   (target 0x08 ← the fix)");
    print("  CFR4N post:  0x" + hex(cfr4nPost) + "   (target 0x08)");
    print("  WIP polls:   " + wipPolls);
    var statusStr = STATUS_NAMES[status];
    if (statusStr === undefined) statusStr = "UNKNOWN (0x" + hex(status, 8) + ")";
    print("  STATUS:      " + statusStr);
    print("");

    try { dsR5_0.target.disconnect(); } catch (e) { print("disconnect: " + e); }
    java.lang.System.exit(status == 0 ? 0 : 1);
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
