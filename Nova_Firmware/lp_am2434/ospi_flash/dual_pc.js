/* dump PC of both R5FSS0_0 (main) and R5FSS1_0 (watchdog) */
var ccxmlPath = java.lang.System.getenv("LP_CCXML")
             || "F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP.ccxml";

importPackage(Packages.com.ti.debug.engine.scripting);
importPackage(Packages.com.ti.ccstudio.scripting.environment);

var script = ScriptingEnvironment.instance();
script.setScriptTimeout(60 * 1000);
var ds = script.getServer("DebugServer.1");
ds.setConfig(ccxmlPath);

function dump(matchPattern, label) {
    try {
        var s = ds.openSession(matchPattern);
        s.target.connect();
        try { s.target.halt(); } catch (e) {}
        try {
            var pc = s.memory.readRegister("PC");
            var lr = s.memory.readRegister("LR");
            var sp = s.memory.readRegister("SP");
            print(label + ": PC=0x" + java.lang.Long.toHexString(pc).toUpperCase() +
                  "  LR=0x" + java.lang.Long.toHexString(lr).toUpperCase() +
                  "  SP=0x" + java.lang.Long.toHexString(sp).toUpperCase());
        } catch (e) { print(label + ": reg read err: " + e); }
        try { s.target.runAsynch(); } catch(e) {}
        try { s.target.disconnect(); } catch(e) {}
    } catch (e) { print(label + ": session err: " + e); }
}

dump(".*MAIN_Cortex_R5_0_0", "R5FSS0_0 (main)    ");
dump(".*MAIN_Cortex_R5_1_0", "R5FSS1_0 (watchdog)");
dump(".*MAIN_Cortex_R5_0_1", "R5FSS0_1 (idle?)   ");
dump(".*MAIN_Cortex_R5_1_1", "R5FSS1_1 (idle?)   ");
ds.stop();
