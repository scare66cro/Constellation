/*
 * load_nova_noreset.js — like load_nova.js but skip the target.reset()
 * between sciclient_ccs_init and the Nova ELF load. After a SW_MAIN_WARMRST
 * the second reset can revert DMSC firewall config that ccs_init just
 * applied, producing -1065 "Unable to access device memory" when loading
 * .text at 0x701784F0+.
 *
 * Used as one-shot recovery only when the standard load_nova.js fails
 * post-warm-reset on chip S24L0707 (OSPI cold-boot already broken).
 */
var sdkPath = "C:/ti/mcu_plus_sdk_am243x_12_00_00_26";
var fwPath  = "F:/Constellation/Nova_Firmware/lp_am2434/ti-arm-clang";
var ccxmlPath = java.lang.System.getenv("LP_CCXML")
             || "F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP.ccxml";

var ccs_init_elf = sdkPath + "/tools/ccs_load/am243x/sciclient_ccs_init.release.out";
var nova_elf     = fwPath + "/nova_lp.release.out";

importPackage(Packages.com.ti.debug.engine.scripting);
importPackage(Packages.com.ti.ccstudio.scripting.environment);
importPackage(Packages.java.lang);
importPackage(java.io);
importPackage(java.lang);

var script = ScriptingEnvironment.instance();
script.setScriptTimeout(180 * 1000);
var debugServer = script.getServer("DebugServer.1");
debugServer.setConfig(ccxmlPath);

print("=== Step 1: DMSC Board Configuration (HS-FS) ===");
var ds = debugServer.openSession(".*MAIN_Cortex_R5_0_0");
ds.target.connect();

/* Spin pad so any unexpected execution lands in a safe loop */
ds.memory.fill(0x78000000, 0, 0x2000, 0);
ds.memory.writeWord(0, 0x78000000, 0xE59FF004);
ds.memory.writeWord(0, 0x78000004, 0x38);
ds.memory.writeWord(0, 0x78000038, 0xEAFFFFFE);

try { ds.target.halt(); } catch (e) {}
try { ds.target.reset(); } catch (e) { print("[r5 reset] " + e); }

print("  Loading sciclient_ccs_init...");
ds.memory.loadProgram(ccs_init_elf);
try { ds.target.halt(); } catch (e) {}
ds.target.runAsynch();
java.lang.Thread.sleep(5000);
try { ds.target.halt(); } catch (e) {}
print("  DMSC init done.");

print("=== Step 2: Loading Nova Firmware (NO second reset) ===");
/* IMPORTANT: skip target.reset() here — keep DMSC firewall state alive */
print("  Loading " + nova_elf);
try {
    ds.memory.loadProgram(nova_elf);
} catch (e) {
    print("[loadProgram FAILED] " + e);
    try { ds.target.disconnect(); } catch (e2) {}
    try { debugServer.stop(); } catch (e3) {}
    java.lang.System.exit(1);
}

var pc = ds.expression.evaluate("PC");
print("  PC after load: 0x" + java.lang.Long.toHexString(pc));

print("=== Step 3: Running ===");
ds.target.runAsynch();
java.lang.Thread.sleep(3000);
print("  Nova firmware running on R5F core 0.");

try { ds.target.disconnect(); } catch (e) {}
try { debugServer.stop(); } catch (e) {}
print("=== DONE ===");
