/*
 * load_nova.js — Load Nova firmware to LP-AM2434 via CCS JTAG (XDS110)
 *
 * This script:
 *   1. Initializes DMSC (board config, security manager) on the HS-FS device
 *   2. Loads nova_lp.release.out to R5F core 0 (r5fss0-0)
 *   3. Runs the firmware
 *
 * Usage from CCS Scripting Console (View > Scripting Console):
 *   js:> loadJSFile "F:/Constellation/Nova_Firmware/lp_am2434/load_nova.js"
 *
 * Or headless from command line:
 *   "C:\ti\ccs2050\ccs\ccs_base\scripting\bin\dss.bat" load_nova.js
 *
 * Prerequisites:
 *   - LP-AM2434 connected via USB (XDS110)
 *   - SW4 on OSPI boot (0100 0100) — works with JTAG regardless
 *   - No serial monitors holding COM4
 */

// ── Paths ────────────────────────────────────────────────────────────────
var sdkPath = "C:/ti/mcu_plus_sdk_am243x_12_00_00_26";
var fwPath  = "F:/Constellation/Nova_Firmware/lp_am2434/ti-arm-clang";
var ccxmlPath = java.lang.System.getenv("LP_CCXML")
             || "F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP.ccxml";

// DMSC board-config init (required for HS-FS devices before loading user code)
var ccs_init_elf = sdkPath + "/tools/ccs_load/am243x/sciclient_ccs_init.release.out";

// Nova firmware ELF
var nova_elf = fwPath + "/nova_lp.release.out";

// ── Helpers ──────────────────────────────────────────────────────────────
function connectHaltReset(ds) {
    ds.target.connect();
    ds.target.halt();
    ds.target.reset();
}

// ── Main ─────────────────────────────────────────────────────────────────
function doEverything() {
    script.setScriptTimeout(120 * 1000);

    // Step 1: Init DMSC via R5F core 0
    print("=== Step 1: DMSC Board Configuration (HS-FS) ===");
    var dsR5_0 = debugServer.openSession(".*MAIN_Cortex_R5_0_0");

    dsR5_0.target.connect();
    // Write a spin loop so the core doesn't run wild
    dsR5_0.memory.fill(0x78000000, 0, 0x2000, 0);
    dsR5_0.memory.writeWord(0, 0x78000000, 0xE59FF004); // ldr pc, [pc, #4]
    dsR5_0.memory.writeWord(0, 0x78000004, 0x38);       // target addr
    dsR5_0.memory.writeWord(0, 0x78000038, 0xEAFFFFFE); // b #0x38 (spin)

    dsR5_0.target.halt();
    dsR5_0.target.reset();

    print("  Loading sciclient_ccs_init...");
    dsR5_0.memory.loadProgram(ccs_init_elf);
    dsR5_0.target.halt();
    // sciclient_ccs_init ends with while(1), so use async run + wait
    dsR5_0.target.runAsynch();
    // Board config completes in ~2-3 seconds on real hardware
    java.lang.Thread.sleep(5000);
    dsR5_0.target.halt();
    print("  DMSC init done.");

    // Step 2: Load Nova firmware
    // Reset clears sciclient_ccs_init state from R5F; halt ensures
    // loadProgram finds a stopped core. DMSC on HSM core survives
    // the R5F reset, so clock/power config will work in nova's
    // System_init().
    print("=== Step 2: Loading Nova Firmware ===");
    dsR5_0.target.reset();
    dsR5_0.target.halt();
    print("  Loading " + nova_elf);
    dsR5_0.memory.loadProgram(nova_elf);

    // Verify entry point was set
    var pc = dsR5_0.expression.evaluate("PC");
    print("  PC after load: 0x" + java.lang.Long.toHexString(pc));

    // Step 3: Run
    print("=== Step 3: Running ===");
    dsR5_0.target.runAsynch();
    // Give firmware time to boot and print banner via semihosting
    java.lang.Thread.sleep(3000);
    print("  Nova firmware running on R5F core 0.");
    print("");
    print("=== DONE — UART2 bridge should be active ===");
}

// ── CCS Scripting Boilerplate ────────────────────────────────────────────
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
        print("Create it via CCS > View > Target Configurations, or use the provided AM2434_LP.ccxml");
    } else {
        debugServer.setConfig(ccxmlPath);
        doEverything();
    }
} else {
    debugServer = ds;
    script = env;
    doEverything();
}
