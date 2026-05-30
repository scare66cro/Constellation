/*
 * run_phy_tuning_writer.js — DSS driver for the OSPI_PHY_TUNING_WRITER
 * auto-flasher build.
 *
 * Loads `sbl_jtag_uniflash.release.out` (built with -DOSPI_PHY_TUNING_WRITER)
 * onto R5FSS0_0 via JTAG, lets it open OSPI + flash, write the 128-byte
 * OSPI PHY attack-vector at Flash_Attrs.phyTuningOffset (0x2000000 on the
 * S25HL512T), and verify by readback. Then polls `g_ospi_phy_writer.
 * magic_done` for the completion sentinel (0xF1A5DA7B) and reports the
 * result struct.
 *
 * Required state: LP in NOBOOT mode (SW4: 1, 2, 3, 4 ON), USB-C plugged
 * in, only the target XDS110 probe enumerated.
 *
 * Same pattern as run_nv_recover.js — the two binaries share the spin-
 * loop scaffold; only main() differs at compile time.
 *
 * Background: see memories/repo/lp-am2434-ospi-boot-missing-phy-tuning.md
 * for why this exists. tl;dr: the prebuilt SBL silently wedges in
 * Flash_norOspiPhyTune at boot unless the attack vector is present at
 * 0x2000000. Working chips have it from the original Uniflash
 * commissioning; NV-recovered chips need it re-written.
 */

var sdkPath      = "C:/ti/mcu_plus_sdk_am243x_12_00_00_26";
var ccxmlPath    = java.lang.System.getenv("LP_CCXML")
                || "F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP_B.ccxml";
var ccs_init_elf = sdkPath + "/tools/ccs_load/am243x/sciclient_ccs_init.release.out";
var writer_elf   = "F:/Constellation/Nova_Firmware/lp_am2434/flasher_uart/ti-arm-clang/sbl_jtag_uniflash.release.out";

var STATUS_NAMES = {
    0x00: "SUCCESS — 128-byte attack vector written + verified at phyTuningOffset",
    0xA1: "ERROR — Board_driversOpen failed (Flash_open didn't return)",
    0xA2: "ERROR — Flash_getAttrs returned NULL",
    0xA3: "ERROR — Flash_offsetToBlkPage failed (offset out of range?)",
    0xA4: "ERROR — Flash_eraseBlk failed",
    0xA5: "ERROR — Flash_write failed (INDIRECT_WRITE rejected — bug?)",
    0xA6: "ERROR — Flash_read verify failed",
    0xA7: "ERROR — readback mismatch (write claimed success but bytes differ)",
    0xA8: "ERROR — OSPI_phyGetTuningData returned bad values"
};

importPackage(Packages.com.ti.debug.engine.scripting);
importPackage(Packages.com.ti.ccstudio.scripting.environment);
importPackage(Packages.java.lang);

function go() {
    script.setScriptTimeout(120 * 1000);

    print("=== OSPI PHY Tuning Writer (sbl_jtag_uniflash -DOSPI_PHY_TUNING_WRITER) ===");
    print("  ccxml:    " + ccxmlPath);
    print("  writer:   " + writer_elf);
    print("");

    /* Step 1: prime R5 + load sciclient_ccs_init so DMSC firewalls are
     * open before the writer runs. Same ritual as run_nv_recover.js. */
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

    /* Step 2: load the writer binary. */
    print("=== Step 2: load OSPI_PHY_TUNING_WRITER binary ===");
    dsR5_0.target.reset();
    dsR5_0.target.halt();
    dsR5_0.memory.loadProgram(writer_elf);
    print("  binary loaded; PC at main (halted).");

    var structAddr = dsR5_0.symbol.getAddress("g_ospi_phy_writer");
    print("  g_ospi_phy_writer @ 0x" + java.lang.Long.toHexString(structAddr));
    print("");

    /* Layout must match OspiPhyWriter_t in main.c.
     * struct {
     *   uint32_t magic_done;        // +0
     *   uint32_t status;            // +4
     *   uint32_t phy_tuning_offset; // +8
     *   uint32_t tuning_data_addr;  // +12
     *   uint32_t tuning_data_size;  // +16
     *   uint8_t  verify_match;      // +20
     *   uint8_t  reserved[3];       // +21..23
     *   uint8_t  read_back[32];     // +24..55
     *   uint8_t  source_head[32];   // +56..87
     * };
     */
    var OFF_MAGIC        = 0;
    var OFF_STATUS       = 4;
    var OFF_PHY_OFFSET   = 8;
    var OFF_DATA_ADDR    = 12;
    var OFF_DATA_SIZE    = 16;
    var OFF_VERIFY_MATCH = 20;
    var OFF_READ_BACK    = 24;
    var OFF_SOURCE_HEAD  = 56;
    var DONE_MAGIC       = 0xF1A5DA7B;

    /* Zero out so we can see what changed. */
    dsR5_0.memory.writeWord(0, structAddr + OFF_MAGIC, 0);
    dsR5_0.memory.writeWord(0, structAddr + OFF_STATUS, 0);

    print("=== Step 3: run target ===");
    dsR5_0.target.runAsynch();

    /* Erase + 128-byte write + verify should complete well under 30 s.
     * Erase of one 256 KB block on S25HL512T is ~700 ms; the rest is
     * microseconds. */
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
        print("[ERROR] writer timed out after 30 s (magic=0x" +
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
    function dumpBytes(off, n) {
        var pieces = [];
        for (var i = 0; i < n; i++) pieces.push(hex(r8(off + i)));
        return pieces.join(" ");
    }

    var status     = r32(OFF_STATUS);
    var phyOffset  = r32(OFF_PHY_OFFSET);
    var dataAddr   = r32(OFF_DATA_ADDR);
    var dataSize   = r32(OFF_DATA_SIZE);
    var verifyMatch = r8(OFF_VERIFY_MATCH);

    print("");
    print("=== PHY tuning writer result ===");
    print("  phyTuningOffset: 0x" + hex(phyOffset, 8) +
          "   (expected 0x02000000 for S25HL512T)");
    print("  tuning data:     0x" + hex(dataAddr, 8) + " (" + dataSize + " B)" +
          "   (expected 128 B from gOspiFlashAttackVector[])");
    print("  verify match:    " + (verifyMatch ? "YES" : "NO"));
    print("  source[0..31]:   " + dumpBytes(OFF_SOURCE_HEAD, 32));
    print("  readback[0..31]: " + dumpBytes(OFF_READ_BACK, 32));
    var statusStr = STATUS_NAMES[status];
    if (statusStr === undefined) statusStr = "UNKNOWN (0x" + hex(status, 8) + ")";
    print("  STATUS:          " + statusStr);
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
