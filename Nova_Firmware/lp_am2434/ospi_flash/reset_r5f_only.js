/*
 * Reset just the R5F core via DSS.
 *
 * Use when the auto-flasher (or app firmware) is hung mid-OSPI-write
 * and the SW_MAIN_WARMRST path via CTRL_MMR0 is unavailable
 * (DMSC firewall blocks the partition-6 unlock kick — see
 * memories/repo/lp-am2434-no-remote-reset.md).
 *
 * Effect:
 *   - Halts the R5F core
 *   - Issues target.reset() to put the R5F at the ROM entry vector
 *   - ROM falls through to SBL, SBL loads from OSPI 0x080000
 *   - Whatever was last written to OSPI runs (previous firmware,
 *     or partial-write garbage if the flasher had started writing)
 *
 * Limitations:
 *   - Does NOT reset OSPI controller, CPSW, DMSC, or other peripherals.
 *     If the OSPI controller is in a wedged state from a stuck mid-write,
 *     the loaded firmware might fail to boot. In that case the only
 *     remaining recovery is a physical power-cycle.
 *
 * Required env: LP_CCXML pointing at the right probe's .ccxml.
 */
var ScriptingEnvironment =
    Packages.com.ti.ccstudio.scripting.environment.ScriptingEnvironment;
var script = ScriptingEnvironment.instance();
var ds = script.getServer("DebugServer.1");
var ccxmlPath = java.lang.System.getenv("LP_CCXML")
             || "F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP.ccxml";
print("Using ccxml: " + ccxmlPath);
ds.setConfig(ccxmlPath);
var s = ds.openSession(".*Cortex_R5_0_0");

try {
    print("Connecting to R5F core...");
    s.target.connect();

    try {
        print("Halting...");
        s.target.halt();
    } catch (eh) {
        print("  halt failed (may already be halted or unresponsive): " + eh);
    }

    print("Issuing target.reset()...");
    s.target.reset();
    print("  reset issued.");
} catch (ew) {
    print("Reset attempt failed: " + ew);
} finally {
    try { s.target.disconnect(); } catch (e) {}
    try { ds.stop(); } catch (e) {}
}

print("Done. Wait ~10 s for SBL to reload from OSPI. If the board");
print("doesn't come back on the network, a physical power-cycle is");
print("the only remaining recovery.");
