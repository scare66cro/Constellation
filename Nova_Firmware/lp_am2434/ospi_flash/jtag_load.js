/*
 * Load the freshly-built nova_lp.release.out onto R5F0-0 via JTAG and
 * run it. Use this when you want to test the new image immediately
 * without waiting for a power cycle.
 *
 * For permanent install (cold boot from OSPI), use uniflash_run.js
 * — that writes the same image to OSPI offset 0x80000 so SBL will
 * load it on next power-up.
 */
var ScriptingEnvironment = Packages.com.ti.ccstudio.scripting.environment.ScriptingEnvironment;
var script = ScriptingEnvironment.instance();
var ds = script.getServer("DebugServer.1");
// Per-probe override via LP_CCXML env var; see Flash-LP.ps1.
var ccxmlPath = java.lang.System.getenv("LP_CCXML")
             || "F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP.ccxml";
ds.setConfig(ccxmlPath);

var imgPath = "F:/Constellation/Nova_Firmware/lp_am2434/ti-arm-clang/nova_lp.release.out";

/* Load DMSC board cfg so peripherals are powered (HS-FS device). */
var dmscPath = "C:/ti/mcu_plus_sdk_am243x_12_00_00_26/source/drivers/sciclient/sciclient_ccs_init/sciclient_ccs_init_system.release/sciclient_ccs_init.release.hs_fs/sciclient_ccs_init.release.hs_fs";
try {
    var dmsc = ds.openSession(".*M3_DMSC_0");
    dmsc.target.connect();
    dmsc.memory.loadProgram(dmscPath);
    dmsc.target.runAsynch();
    java.lang.Thread.sleep(2000);
    try { dmsc.target.disconnect(); } catch(e){}
    print("DMSC init done.");
} catch(e) { print("DMSC step skipped: " + e); }

var s = ds.openSession(".*Cortex_R5_0_0");
s.target.connect();
s.target.reset();
print("Loading " + imgPath);
s.memory.loadProgram(imgPath);
print("Running...");
s.target.runAsynch();
print("R5F0-0 launched. Disconnecting.");
try { s.target.disconnect(); } catch(e){}
