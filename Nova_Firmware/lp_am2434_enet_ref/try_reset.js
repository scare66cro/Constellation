/*
 * try_reset.js — Attempt every flavor of reset DSS exposes, hoping one
 * of them clears the CPSW peripheral state without a USB power-cycle.
 *
 * If this script does NOT produce a fresh "DMSC Firmware Version" banner
 * on UART0 within ~10s, only a real power cycle of the LP will work.
 */
var ccxmlPath = "F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP.ccxml";

importPackage(Packages.com.ti.debug.engine.scripting);
importPackage(Packages.com.ti.ccstudio.scripting.environment);
importPackage(Packages.java.lang);
importPackage(java.io);
importPackage(java.lang);

var script = ScriptingEnvironment.instance();
script.traceSetConsoleLevel(TraceLevel.INFO);
var debugServer = script.getServer("DebugServer.1");
debugServer.setConfig(ccxmlPath);

try {
    script.setScriptTimeout(60 * 1000);

    // Open every R5F core and reset each one
    var cores = [".*MAIN_Cortex_R5_0_0", ".*MAIN_Cortex_R5_0_1",
                 ".*MAIN_Cortex_R5_1_0", ".*MAIN_Cortex_R5_1_1"];
    for (var i = 0; i < cores.length; i++) {
        try {
            print("--- " + cores[i] + " ---");
            var s = debugServer.openSession(cores[i]);
            s.target.connect();
            try { s.target.halt(); } catch (e) { print("  halt: " + e); }
            try { s.target.reset(); print("  reset()"); } catch (e) { print("  reset: " + e); }
            try { s.target.disconnect(); } catch (e) {}
        } catch (e) {
            print("  (skipped: " + e + ")");
        }
    }

    // Try a board-level reset via XDS110 (not always available)
    try {
        var s2 = debugServer.openSession(".*MAIN_Cortex_R5_0_0");
        s2.target.connect();
        // CCS exposes some reset types via numeric IDs; try a few
        for (var rt = 0; rt < 8; rt++) {
            try {
                s2.target.reset(rt);
                print("  reset(" + rt + ") OK");
            } catch (e) {
                print("  reset(" + rt + ") -> " + e);
            }
        }
    } catch (e) {
        print("xds110 reset: " + e);
    }

    print("=== Done ===");
} catch (e) {
    print("ERROR: " + e);
}
java.lang.System.exit(0);
