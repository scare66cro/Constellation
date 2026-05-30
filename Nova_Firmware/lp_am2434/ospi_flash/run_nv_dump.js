/*
 * run_nv_dump.js — DSS driver for the OSPI_NV_DUMP diagnostic.
 *
 * Read-only dump of ALL Cypress S25HL512T NV+V configuration registers
 * via STIG (RDAR cmd 0x65). Built to investigate the 2026-05-17 finding
 * that the chip can't ROM-boot from OSPI even with CFR2/3/4N correctly
 * set to 0x08 — the leading hypothesis is that CFR1N and/or CFR5N
 * (never touched by `OSPI_NV_RECOVER`) drifted to bad values during
 * the original 0.A.115 brick.
 *
 * NON-DESTRUCTIVE — no WREN, no WRRSB. Just reads.
 *
 * Required state: LP in NOBOOT mode (SW4: 1,2,3,4 ON), USB-C plugged
 * in, only the target XDS110 probe enumerated (use Set-Probe Solo).
 *
 * Run: gmake PROFILE=release DEFINES_common='-DSOC_AM243X -DOS_NORTOS -DOSPI_NV_DUMP' all
 *      then this script. Restore default flasher with a plain gmake after.
 *
 * Expected values for a HEALTHY S25HL512T (matching Flash_quirkQSPI
 * EarlyFixup factory defaults):
 *   RDID  = 34 2A 1A
 *   SR1N  = 0x00
 *   SR1V  = 0x00 (or 0x02 transiently if WEL set)
 *   CFR1N = 0x00     CFR1V = 0x00
 *   CFR2N = 0x08     CFR2V = 0x08
 *   CFR3N = 0x08     CFR3V = 0x08
 *   CFR4N = 0x08     CFR4V = 0x08
 *   CFR5N = ???      CFR5V = ???    (not in SDK fixup table — read for first time)
 */

var sdkPath      = "C:/ti/mcu_plus_sdk_am243x_12_00_00_26";
var ccxmlPath    = java.lang.System.getenv("LP_CCXML")
                || "F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP_B.ccxml";
var ccs_init_elf = sdkPath + "/tools/ccs_load/am243x/sciclient_ccs_init.release.out";
var dumper_elf   = "F:/Constellation/Nova_Firmware/lp_am2434/flasher_uart/ti-arm-clang/sbl_jtag_uniflash.release.out";

importPackage(Packages.com.ti.debug.engine.scripting);
importPackage(Packages.com.ti.ccstudio.scripting.environment);
importPackage(Packages.java.lang);

function go() {
    script.setScriptTimeout(120 * 1000);

    print("=== OSPI NV Dump (sbl_jtag_uniflash -DOSPI_NV_DUMP) ===");
    print("  ccxml:    " + ccxmlPath);
    print("  dumper:   " + dumper_elf);
    print("");

    print("=== Step 1: open session, prime R5, load sciclient_ccs_init ===");
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
    print("  DMSC firewalls open.");
    print("");

    print("=== Step 2: load OSPI_NV_DUMP binary ===");
    dsR5_0.target.reset();
    dsR5_0.target.halt();
    dsR5_0.memory.loadProgram(dumper_elf);
    print("  binary loaded; PC at main (halted).");

    var structAddr = dsR5_0.symbol.getAddress("g_ospi_nv_dump");
    print("  g_ospi_nv_dump @ 0x" + java.lang.Long.toHexString(structAddr));
    print("");

    /* Layout must match OspiNvDump_t in main.c:
     *   uint32_t magic_done;  // +0
     *   uint32_t status;      // +4
     *   uint8_t  id_manuf;    // +8
     *   uint8_t  id_dev_hi;   // +9
     *   uint8_t  id_dev_lo;   // +10
     *   uint8_t  pad0;        // +11
     *   uint8_t  sr1n;        // +12
     *   uint8_t  sr1v;        // +13
     *   uint8_t  cfr1n;       // +14
     *   uint8_t  cfr1v;       // +15
     *   uint8_t  cfr2n;       // +16
     *   uint8_t  cfr2v;       // +17
     *   uint8_t  cfr3n;       // +18
     *   uint8_t  cfr3v;       // +19
     *   uint8_t  cfr4n;       // +20
     *   uint8_t  cfr4v;       // +21
     *   uint8_t  cfr5n;       // +22
     *   uint8_t  cfr5v;       // +23
     */
    var OFF_MAGIC   = 0,  OFF_STATUS = 4;
    var OFF_MANUF   = 8,  OFF_DEVHI  = 9,  OFF_DEVLO = 10;
    var OFF_SR1N    = 12, OFF_SR1V   = 13;
    var OFF_CFR1N   = 14, OFF_CFR1V  = 15;
    var OFF_CFR2N   = 16, OFF_CFR2V  = 17;
    var OFF_CFR3N   = 18, OFF_CFR3V  = 19;
    var OFF_CFR4N   = 20, OFF_CFR4V  = 21;
    var OFF_CFR5N   = 22, OFF_CFR5V  = 23;
    var DONE_MAGIC  = 0xF1A5DA7D;

    dsR5_0.memory.writeWord(0, structAddr + OFF_MAGIC, 0);
    dsR5_0.memory.writeWord(0, structAddr + OFF_STATUS, 0);

    print("=== Step 3: run target ===");
    dsR5_0.target.runAsynch();

    var deadline = java.lang.System.currentTimeMillis() + 30000;
    var done = false;
    var magic = 0;
    while (java.lang.System.currentTimeMillis() < deadline) {
        java.lang.Thread.sleep(300);
        try {
            dsR5_0.target.halt();
            magic = dsR5_0.memory.readWord(0, structAddr + OFF_MAGIC) >>> 0;
            if (magic == DONE_MAGIC) {
                done = true;
                break;
            }
            dsR5_0.target.runAsynch();
        } catch (e) { print("  poll error: " + e); }
    }
    if (!done) {
        try { dsR5_0.target.halt(); } catch (e) {}
        print("[ERROR] NV dump timed out after 30 s (magic=0x" +
              java.lang.Long.toHexString(magic) + ")");
        dsR5_0.target.disconnect();
        java.lang.System.exit(2);
    }

    function r8(off) { return dsR5_0.memory.readData(0, structAddr + off, 8) & 0xFF; }
    function r32(off) { return dsR5_0.memory.readWord(0, structAddr + off) >>> 0; }
    function hex8(v) {
        var s = java.lang.Long.toHexString(v);
        while (s.length < 2) s = "0" + s;
        return "0x" + s;
    }

    var status = r32(OFF_STATUS);
    var manuf  = r8(OFF_MANUF);
    var devhi  = r8(OFF_DEVHI);
    var devlo  = r8(OFF_DEVLO);

    print("");
    print("=== Cypress S25HL512T NV/V register dump ===");
    print("  RDID:    " + hex8(manuf) + " " + hex8(devhi) + " " + hex8(devlo) +
          "   (expect 0x34 0x2A 0x1A)");
    print("");
    print("  Register | Non-volatile | Volatile  | SDK-expected (N)");
    print("  ---------+--------------+-----------+-----------------");
    print("  SR1      | " + hex8(r8(OFF_SR1N))  + "         | " + hex8(r8(OFF_SR1V))  + "      | 0x00");
    print("  CFR1     | " + hex8(r8(OFF_CFR1N)) + "         | " + hex8(r8(OFF_CFR1V)) + "      | 0x00  ** UNTESTED before today **");
    print("  CFR2     | " + hex8(r8(OFF_CFR2N)) + "         | " + hex8(r8(OFF_CFR2V)) + "      | 0x08");
    print("  CFR3     | " + hex8(r8(OFF_CFR3N)) + "         | " + hex8(r8(OFF_CFR3V)) + "      | 0x08");
    print("  CFR4     | " + hex8(r8(OFF_CFR4N)) + "         | " + hex8(r8(OFF_CFR4V)) + "      | 0x08");
    print("  CFR5     | " + hex8(r8(OFF_CFR5N)) + "         | " + hex8(r8(OFF_CFR5V)) + "      |  ?    ** NEVER AUDITED **");
    print("");

    /* Quick highlight of any value that doesn't match SDK-expected. */
    var anyBad = false;
    function check(name, val, expected) {
        if (val !== expected) {
            print("  *** MISMATCH: " + name + " = " + hex8(val) +
                  ", SDK-expected " + hex8(expected) + " ***");
            anyBad = true;
        }
    }
    check("SR1N",  r8(OFF_SR1N),  0x00);
    check("CFR1N", r8(OFF_CFR1N), 0x00);
    check("CFR2N", r8(OFF_CFR2N), 0x08);
    check("CFR3N", r8(OFF_CFR3N), 0x08);
    check("CFR4N", r8(OFF_CFR4N), 0x08);

    if (!anyBad) {
        print("  All SR1N/CFR1-4N match SDK-expected factory defaults.");
        print("  CFR5N is the only unknown — its value above is the");
        print("  first time we've measured it on this chip.");
    }

    if (status == 0) {
        print("");
        print("STATUS: SUCCESS — dump complete");
    } else {
        print("");
        print("STATUS: FAIL — status code 0x" + java.lang.Long.toHexString(status));
    }
    print("");

    try { dsR5_0.target.disconnect(); } catch (e) {}
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
