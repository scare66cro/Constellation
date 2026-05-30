/*
 * load_nova_keepalive.js — Load Nova firmware via JTAG and KEEP THE DSS
 * SESSION OPEN so CIO (semihosting) breakpoints are serviced.
 *
 * Same load path as load_nova.js, but does NOT exit after the firmware
 * is running. Stays connected forever so any `printf()` calls in Nova
 * (which go via __TI_writemsg -> HOSTwrite -> BKPT) get serviced by
 * DSS instead of wedging the chip.
 *
 * Background: Nova links against libsysbm.a which provides CIO. A
 * standalone load (no debugger attached) wedges any time `printf` is
 * called because the BKPT instruction has no handler. Memory note:
 * lp-am2434-ospi-boot-missing-phy-tuning.md 2026-05-18 trail.
 *
 * Usage:
 *   $env:LP_CCXML = '...\AM2434_LP_B.ccxml'
 *   & 'C:\ti\ccs2050\ccs\ccs_base\scripting\bin\dss.bat' `
 *     'F:\Constellation\Nova_Firmware\lp_am2434\load_nova_keepalive.js'
 *
 * The script will print "Nova running — CIO handler active. Ctrl+C
 * this window to disconnect (will wedge Nova on next printf)."
 * Leave the window open as long as Nova needs to run.
 */

var sdkPath = "C:/ti/mcu_plus_sdk_am243x_12_00_00_26";
var fwPath  = "F:/Constellation/Nova_Firmware/lp_am2434/ti-arm-clang";
var ccxmlPath = java.lang.System.getenv("LP_CCXML")
             || "F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP.ccxml";

var ccs_init_elf = sdkPath + "/tools/ccs_load/am243x/sciclient_ccs_init.release.out";
var nova_elf     = fwPath + "/nova_lp.release.out";

function doEverything() {
    /* Long timeout — we intend to run forever. 24h cap as safety. */
    script.setScriptTimeout(24 * 60 * 60 * 1000);

    print("=== Step 1: DMSC Board Configuration (HS-FS) ===");
    var dsR5_0 = debugServer.openSession(".*MAIN_Cortex_R5_0_0");

    dsR5_0.target.connect();
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

    print("=== Step 2: Loading Nova Firmware ===");
    dsR5_0.target.reset();
    dsR5_0.target.halt();
    print("  Loading " + nova_elf);
    dsR5_0.memory.loadProgram(nova_elf);

    var pc = dsR5_0.expression.evaluate("PC");
    print("  PC after load: 0x" + java.lang.Long.toHexString(pc));

    print("=== Step 3: Running ===");
    dsR5_0.target.runAsynch();
    java.lang.Thread.sleep(3000);
    print("");
    print("============================================================");
    print(" Nova running — CIO handler active.");
    print(" DSS session staying open to service printf() breakpoints.");
    print(" Ctrl+C this window to disconnect — will wedge Nova on the");
    print(" next printf() call. Leave it open while you use Nova.");
    print("============================================================");
    print("");

    /* Loop forever, sleeping. DSS internally services CIO/semihosting
     * breakpoints during this time as long as the session is alive. */
    while (true) {
        java.lang.Thread.sleep(60000);
    }
}

importPackage(Packages.com.ti.debug.engine.scripting);
importPackage(Packages.com.ti.ccstudio.scripting.environment);
importPackage(Packages.java.lang);
importPackage(java.io);
importPackage(java.lang);

var ds;
var debugServer;
var script;
var withinCCS = (ds !== undefined);

if (!withinCCS) {
    script = ScriptingEnvironment.instance();
    debugServer = script.getServer("DebugServer.1");

    if (!File(ccxmlPath).isFile()) {
        print("[ERROR] CCXML not found: " + ccxmlPath);
    } else {
        debugServer.setConfig(ccxmlPath);
        doEverything();
    }
} else {
    debugServer = ds;
    script = env;
    doEverything();
}
