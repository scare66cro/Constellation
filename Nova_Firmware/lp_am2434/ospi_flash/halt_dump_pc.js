/* halt_dump_pc.js — connect to running R5F0, halt, dump PC + LR + a few regs */
var ccxmlPath = java.lang.System.getenv("LP_CCXML")
             || "F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP.ccxml";

importPackage(Packages.com.ti.debug.engine.scripting);
importPackage(Packages.com.ti.ccstudio.scripting.environment);
importPackage(Packages.java.lang);

function go() {
    var script = ScriptingEnvironment.instance();
    script.setScriptTimeout(60 * 1000);
    var debugServer = script.getServer("DebugServer.1");
    debugServer.setConfig(ccxmlPath);
    var ds = debugServer.openSession(".*MAIN_Cortex_R5_0_0");
    ds.target.connect();
    try { ds.target.halt(); } catch (e) { print("[halt] " + e); }

    var regs = ["PC","LR","SP","R0","R1","R2","R3","CPSR"];
    for (var i = 0; i < regs.length; i++) {
        try {
            var v = ds.memory.readRegister(regs[i]);
            print("  " + regs[i] + " = 0x" + java.lang.Long.toHexString(v));
        } catch (e) { print("  " + regs[i] + " err: " + e); }
    }

    /* Sample PC three more times to see if firmware is moving */
    for (var s = 0; s < 4; s++) {
        try { ds.target.runAsynch(); } catch (e) {}
        java.lang.Thread.sleep(400);
        try { ds.target.halt(); } catch (e) {}
        try {
            var pc2 = ds.memory.readRegister("PC");
            var lr2 = ds.memory.readRegister("LR");
            print("  sample[" + s + "] PC=0x" + java.lang.Long.toHexString(pc2) + " LR=0x" + java.lang.Long.toHexString(lr2));
        } catch (e) {}
    }

    try { ds.target.runAsynch(); } catch (e) {}
    ds.target.disconnect();
    debugServer.stop();
}
go();
