/*
 * uniflash_start.js - Boot the SBL JTAG Uniflash app on R5F0-0
 *
 * Loads sciclient_ccs_init (DMSC board cfg) then sbl_jtag_uniflash.release.out
 * to R5F0-0 and starts it asynchronously, then disconnects DSS so the CPU
 * keeps running the flasher. From this point the flasher serves an
 * interactive menu on UART0 (COM4 @ 115200) - drive it from PowerShell.
 *
 * Usage (headless):
 *   "C:\ti\ccs2050\ccs\ccs_base\scripting\bin\dss.bat" \
 *     "F:\Constellation\Nova_Firmware\lp_am2434\ospi_flash\uniflash_start.js"
 */

var sdkPath   = "C:/ti/mcu_plus_sdk_am243x_12_00_00_26";
// Allow per-probe override via LP_CCXML env var (set by Flash-LP.ps1
// to AM2434_LP_A.ccxml or AM2434_LP_B.ccxml when two boards are
// attached). Falls back to the legacy single-board ccxml.
var ccxmlPath = java.lang.System.getenv("LP_CCXML")
              || "F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP.ccxml";
var ccs_init_elf = sdkPath + "/tools/ccs_load/am243x/sciclient_ccs_init.release.out";
// Local UART-debug-log build of the SBL JTAG Uniflash. The SDK's prebuilt
// uses semihosting CIO for menu I/O which is unusable from headless DSS;
// our local rebuild routes DebugP_log to UART0 (COM4 @ 115200) so PowerShell
// can drive the menu.
var uniflash_elf = "F:/Constellation/Nova_Firmware/lp_am2434/flasher_uart/ti-arm-clang/sbl_jtag_uniflash.release.out";

importPackage(Packages.com.ti.debug.engine.scripting);
importPackage(Packages.com.ti.ccstudio.scripting.environment);
importPackage(Packages.java.lang);
importPackage(java.io);
importPackage(java.lang);

function doStart() {
    script.setScriptTimeout(120 * 1000);

    print("=== Step 1: DMSC board config (HS-FS) ===");
    var dsR5_0 = debugServer.openSession(".*MAIN_Cortex_R5_0_0");
    dsR5_0.target.connect();

    // Spin loop park so the core does not run wild between resets.
    dsR5_0.memory.fill(0x78000000, 0, 0x2000, 0);
    dsR5_0.memory.writeWord(0, 0x78000000, 0xE59FF004);
    dsR5_0.memory.writeWord(0, 0x78000004, 0x38);
    dsR5_0.memory.writeWord(0, 0x78000038, 0xEAFFFFFE);

    dsR5_0.target.halt();
    dsR5_0.target.reset();

    print("  Loading sciclient_ccs_init...");
    dsR5_0.memory.loadProgram(ccs_init_elf);
    dsR5_0.target.halt();
    dsR5_0.target.runAsynch();
    java.lang.Thread.sleep(5000);
    dsR5_0.target.halt();
    print("  DMSC init done.");

    print("=== Step 2: Loading SBL JTAG Uniflash ===");
    dsR5_0.target.reset();
    dsR5_0.target.halt();
    print("  ELF: " + uniflash_elf);
    dsR5_0.memory.loadProgram(uniflash_elf);
    var pc = dsR5_0.expression.evaluate("PC");
    print("  PC after load: 0x" + java.lang.Long.toHexString(pc));

    print("=== Step 3: Run + disconnect ===");
    dsR5_0.target.runAsynch();
    java.lang.Thread.sleep(2000);
    // Disconnect cleanly so PowerShell can drive the UART menu while
    // the CPU stays running. Re-attach later via uniflash_loadraw.js.
    try { dsR5_0.target.disconnect(); } catch (e) { print("disconnect: " + e); }
    print("=== DONE - flasher menu live on COM4 @ 115200 ===");
}

var ds, debugServer, script;
var withinCCS = (ds !== undefined);
if (!withinCCS) {
    script = ScriptingEnvironment.instance();
    debugServer = script.getServer("DebugServer.1");
    debugServer.setConfig(ccxmlPath);
    doStart();
} else {
    debugServer = ds;
    script = env;
    doStart();
}
