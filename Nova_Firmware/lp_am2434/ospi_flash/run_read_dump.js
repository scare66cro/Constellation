/*
 * run_read_dump.js — DSS driver for the OSPI_READ_DUMP diagnostic.
 *
 * Loads `sbl_jtag_uniflash.release.out` (built with -DOSPI_READ_DUMP) onto
 * R5FSS0_0 via JTAG, runs its main() which does:
 *   - System_init, Drivers_open, Board_driversOpen
 *   - Flash_read(0x0,        buf, 128) -> sbl_head[]
 *   - Flash_read(0x2000000,  buf, 128) -> phy_tune_head[]
 *   - sets magic_done = 0xF1A5DA7C
 *
 * Then polls for the magic via JTAG and dumps both 128-byte regions.
 *
 * This uses Flash_read (STIG-backed in the SDK driver), independent of
 * OSPI DAC mode state. Catches silent write corruption that the
 * memory-mapped XIP read could miss when controller isn't in DAC mode.
 *
 * Required state: LP in NOBOOT mode (SW4: 1, 2, 3, 4 ON), USB-C plugged
 * in, only the target XDS110 probe enumerated.
 */

var sdkPath      = "C:/ti/mcu_plus_sdk_am243x_12_00_00_26";
var ccxmlPath    = java.lang.System.getenv("LP_CCXML")
                || "F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP_B.ccxml";
var ccs_init_elf = sdkPath + "/tools/ccs_load/am243x/sciclient_ccs_init.release.out";
var dumper_elf   = "F:/Constellation/Nova_Firmware/lp_am2434/flasher_uart/ti-arm-clang/sbl_jtag_uniflash.release.out";

var STATUS_NAMES = {
    0x00: "SUCCESS — both regions read into MSRAM struct",
    0xB1: "ERROR — Board_driversOpen failed",
    0xB2: "ERROR — Flash_read at OSPI 0x0 failed",
    0xB3: "ERROR — Flash_read at OSPI 0x2000000 failed"
};

importPackage(Packages.com.ti.debug.engine.scripting);
importPackage(Packages.com.ti.ccstudio.scripting.environment);
importPackage(Packages.java.lang);

function go() {
    script.setScriptTimeout(120 * 1000);

    print("=== OSPI Read Dump (sbl_jtag_uniflash -DOSPI_READ_DUMP) ===");
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

    print("=== Step 2: load OSPI_READ_DUMP binary ===");
    dsR5_0.target.reset();
    dsR5_0.target.halt();
    dsR5_0.memory.loadProgram(dumper_elf);
    print("  binary loaded; PC at main (halted).");

    var structAddr = dsR5_0.symbol.getAddress("g_ospi_read_dump");
    print("  g_ospi_read_dump @ 0x" + java.lang.Long.toHexString(structAddr));
    print("");

    /* Layout — must match OspiReadDump_t in main.c:
     *   uint32_t magic_done;          // +0
     *   uint32_t status;              // +4
     *   uint8_t  sbl_head[128];       // +8..135
     *   uint8_t  phy_tune_head[128];  // +136..263
     */
    var OFF_MAGIC          = 0;
    var OFF_STATUS         = 4;
    var OFF_SBL_HEAD       = 8;
    var OFF_PHY_TUNE_HEAD  = 136;
    var DONE_MAGIC         = 0xF1A5DA7C;

    dsR5_0.memory.writeWord(0, structAddr + OFF_MAGIC, 0);
    dsR5_0.memory.writeWord(0, structAddr + OFF_STATUS, 0);

    print("=== Step 3: run target ===");
    dsR5_0.target.runAsynch();

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
        print("[ERROR] read-dump timed out after 30 s (magic=0x" +
              java.lang.Long.toHexString(magic) + ")");
        dsR5_0.target.disconnect();
        java.lang.System.exit(2);
    }

    function r8(off) { return dsR5_0.memory.readData(0, structAddr + off, 8) & 0xFF; }
    function r32(off) { return dsR5_0.memory.readWord(0, structAddr + off) >>> 0; }
    function hex(v, w) {
        var s = java.lang.Long.toHexString(v);
        while (s.length < (w || 2)) s = "0" + s;
        return s;
    }
    function dumpRegion(label, baseOff, len, ospiAddr) {
        print("");
        print("--- " + label + " @ OSPI 0x" + hex(ospiAddr, 8) + " (" + len + " B) ---");
        var ascii = "";
        var hexline = "";
        for (var i = 0; i < len; i++) {
            var b = r8(baseOff + i);
            hexline += hex(b) + " ";
            ascii += (b >= 0x20 && b < 0x7f) ? String.fromCharCode(b) : ".";
            if ((i % 16) == 15) {
                var off = i - 15;
                print("  +0x" + hex(off, 3) + ":  " + hexline + " | " + ascii);
                hexline = "";
                ascii = "";
            }
        }
    }

    var status = r32(OFF_STATUS);
    dumpRegion("SBL header (expected to start with 0x30 0x82 — X.509 SEQUENCE)",
        OFF_SBL_HEAD, 128, 0x00000000);
    dumpRegion("PHY tuning vector (expected fixed pattern, ~32 bytes of structured data)",
        OFF_PHY_TUNE_HEAD, 128, 0x02000000);

    print("");
    var statusStr = STATUS_NAMES[status];
    if (statusStr === undefined) statusStr = "UNKNOWN (0x" + hex(status, 8) + ")";
    print("STATUS: " + statusStr);
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
