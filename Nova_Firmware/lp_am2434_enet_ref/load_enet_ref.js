/*
 * load_enet_ref.js — Load SDK enet_lwip_cpsw onto LP-AM2434 (Phase A bringup).
 * Mirrors load_nova.js from lp_am2434/.
 */
var sdkPath  = "C:/ti/mcu_plus_sdk_am243x_12_00_00_26";
var refPath  = "F:/Constellation/Nova_Firmware/lp_am2434_enet_ref/am243x-lp/r5fss0-0_freertos/ti-arm-clang";
var ccxmlPath = "F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP.ccxml";
var ccs_init_elf = sdkPath + "/tools/ccs_load/am243x/sciclient_ccs_init.release.out";
var enet_elf     = refPath + "/enet_lwip_cpsw.release.out";

function doEverything() {
    script.setScriptTimeout(120 * 1000);

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

    print("=== Step 2: Loading enet_lwip_cpsw reference ===");
    dsR5_0.target.reset();
    dsR5_0.target.halt();
    print("  Loading " + enet_elf);
    dsR5_0.memory.loadProgram(enet_elf);
    var pc = dsR5_0.expression.evaluate("PC");
    print("  PC after load: 0x" + java.lang.Long.toHexString(pc));

    print("=== Step 3: Running ===");
    dsR5_0.target.runAsynch();
    java.lang.Thread.sleep(3000);
    print("  enet_lwip_cpsw running on R5F core 0.");
    print("=== DONE — UART0 (COM4 @115200) should print DHCP banner ===");
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
