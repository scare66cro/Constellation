/*
 * load_nova_via_flasher.js — JTAG-load Nova firmware after first running
 * sbl_jtag_uniflash to open the HS-FS MSRAM firewalls.
 *
 * Why this exists:
 *   sciclient_ccs_init by itself does NOT open the HS-FS MSRAM firewall
 *   on the upper MSRAM partition (0x70180000+). On a chip where OSPI
 *   cold-boot is broken (S24L0707), there's no SBL ever ran, so MSRAM
 *   0x70180000+ stays firewalled and DSS loadProgram for Nova .text
 *   fails with "Error -1065 Unable to access device memory" at the
 *   first byte past 0x70180000.
 *
 *   Per TI's "HS FS Migration Guide":
 *     > all SBLs in the SDK open the MSRAM firewalls right after SYSFW
 *     > boot notification ... using the API Bootloader_socOpenFirewalls
 *
 *   sbl_jtag_uniflash IS a full SBL — it calls Bootloader_socOpenFirewalls
 *   during its Drivers_open(). Once it has been *executed once* on this
 *   power cycle, DMSC remembers the firewall-open state across R5F resets,
 *   and we can JTAG-load Nova on top.
 *
 * Sequence:
 *   1. halt / r5 reset / halt   (get into permissive state)
 *   2. Load sbl_jtag_uniflash, runAsynch, sleep 3 s
 *      -> Drivers_open() runs, Bootloader_socOpenFirewalls() called.
 *   3. halt + r5 reset + halt   (clear R5F state; DMSC firewalls survive)
 *   4. loadProgram(nova_lp.release.out)
 *   5. runAsynch
 */
var sdkPath = "C:/ti/mcu_plus_sdk_am243x_12_00_00_26";
var fwPath  = "F:/Constellation/Nova_Firmware/lp_am2434/ti-arm-clang";
var ccxmlPath = java.lang.System.getenv("LP_CCXML")
             || "F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP.ccxml";

/* 2026-05-20: switched from our sbl_jtag_uniflash to SDK's sbl_dfu —
 *   our flasher's main.c does NOT call Bootloader_socOpenFirewalls
 *   (only Drivers_open), so upper-MSRAM firewall (FWL 23, BANK6,
 *   0x70180000-0x701BFFFF) stays closed.  sbl_dfu does call it. */
var flasher_elf = "C:/ti/mcu_plus_sdk_am243x_12_00_00_26/examples/drivers/boot/sbl_dfu/am243x-evm/r5fss0-0_nortos/ti-arm-clang/sbl_dfu.release.out";
var nova_elf    = fwPath + "/nova_lp.release.out";

importPackage(Packages.com.ti.debug.engine.scripting);
importPackage(Packages.com.ti.ccstudio.scripting.environment);
importPackage(Packages.java.lang);
importPackage(java.io);
importPackage(java.lang);

var script = ScriptingEnvironment.instance();
script.setScriptTimeout(180 * 1000);
var debugServer = script.getServer("DebugServer.1");
debugServer.setConfig(ccxmlPath);

print("=== Step 0: Connect + permissive reset ===");
var ds = debugServer.openSession(".*MAIN_Cortex_R5_0_0");
ds.target.connect();
try { ds.target.halt(); } catch (e) { print("[step0] pre-halt: " + e); }
try { ds.target.reset(); } catch (e) { print("[step0] pre-reset: " + e); }
try { ds.target.halt(); } catch (e) { print("[step0] post-reset halt: " + e); }

/* Spin pad so any stray execution lands somewhere safe */
ds.memory.fill(0x78000000, 0, 0x2000, 0);
ds.memory.writeWord(0, 0x78000000, 0xE59FF004);
ds.memory.writeWord(0, 0x78000004, 0x38);
ds.memory.writeWord(0, 0x78000038, 0xEAFFFFFE);
try { ds.target.halt(); } catch (e) {}

print("=== Step 1: Load sbl_jtag_uniflash to open MSRAM firewalls ===");
try { ds.target.reset(); } catch (e) {}
try { ds.target.halt(); } catch (e) {}
print("  Loading " + flasher_elf);
ds.memory.loadProgram(flasher_elf);
try { ds.target.halt(); } catch (e) {}
print("  Running flasher (Drivers_open -> Bootloader_socOpenFirewalls)...");
ds.target.runAsynch();
java.lang.Thread.sleep(3500);
try { ds.target.halt(); } catch (e) {}
print("  Flasher init done. MSRAM firewalls should be OPEN.");

print("=== Step 2: Reset R5_0 (DMSC firewall state survives) + load Nova ===");
try { ds.target.reset(); } catch (e) { print("[step2] reset: " + e); }
try { ds.target.halt(); } catch (e) {}
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

print("=== Step 3: Run Nova ===");
ds.target.runAsynch();
java.lang.Thread.sleep(3000);
print("  Nova firmware running on R5F core 0.");

try { ds.target.disconnect(); } catch (e) {}
try { debugServer.stop(); } catch (e) {}
print("=== DONE ===");
