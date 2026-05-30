/*
 * uniflash_loadraw.js - Push a host file into the running flasher's RX buffer
 *
 * Reads target address (hex) and host file path from environment vars:
 *   UNIFLASH_ADDR  e.g. "0x80000020"
 *   UNIFLASH_FILE  e.g. "F:/Constellation/Nova_Firmware/lp_am2434/ti-arm-clang/nova_lp.release.mcelf.hs_fs"
 *
 * Usage (headless):
 *   $env:UNIFLASH_ADDR="0x80000020"
 *   $env:UNIFLASH_FILE="F:/.../nova_lp.release.mcelf.hs_fs"
 *   "C:\ti\ccs2050\ccs\ccs_base\scripting\bin\dss.bat" uniflash_loadraw.js
 *
 * Notes:
 *   - Re-attaches to a RUNNING R5F0-0 (no halt). loadRaw issues memory
 *     writes via DAP which work on a running CPU; the flasher's app code
 *     waits in a UART read so memory is quiescent.
 *   - The flasher invalidates D-cache before reading the buffer, so we
 *     don't need a flush from this side.
 */

// Per-probe override via LP_CCXML env var; see Flash-LP.ps1.
var ccxmlPath = java.lang.System.getenv("LP_CCXML")
              || "F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP.ccxml";

importPackage(Packages.com.ti.debug.engine.scripting);
importPackage(Packages.com.ti.ccstudio.scripting.environment);
importPackage(Packages.java.lang);
importPackage(java.io);
importPackage(java.lang);

function doLoad() {
    script.setScriptTimeout(300 * 1000);

    var addrStr = java.lang.System.getenv("UNIFLASH_ADDR");
    var fileStr = java.lang.System.getenv("UNIFLASH_FILE");
    if (addrStr === null || fileStr === null) {
        print("[ERROR] Set UNIFLASH_ADDR and UNIFLASH_FILE env vars.");
        java.lang.System.exit(2);
    }
    print("  addr = " + addrStr);
    print("  file = " + fileStr);

    var addr = parseInt(addrStr, 16);

    var dsR5_0 = debugServer.openSession(".*MAIN_Cortex_R5_0_0");
    dsR5_0.target.connect();
    print("  Connected (no halt). Loading raw bytes...");

    // loadRaw signature: (address, page, file, wordSize, byteSwap)
    dsR5_0.memory.loadRaw(addr, 0, fileStr, 32, false);

    print("  loadRaw complete. Disconnecting.");
    try { dsR5_0.target.disconnect(); } catch (e) { print("disconnect: " + e); }
}

var ds, debugServer, script;
var withinCCS = (ds !== undefined);
if (!withinCCS) {
    script = ScriptingEnvironment.instance();
    debugServer = script.getServer("DebugServer.1");
    debugServer.setConfig(ccxmlPath);
    doLoad();
} else {
    debugServer = ds;
    script = env;
    doLoad();
}
