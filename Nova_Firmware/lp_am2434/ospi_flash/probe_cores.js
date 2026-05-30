/* probe all R5F cores to find which one is alive */
var SE = Packages.com.ti.ccstudio.scripting.environment.ScriptingEnvironment;
var script = SE.instance();
var ds = script.getServer("DebugServer.1");
ds.setConfig(java.lang.System.getenv("LP_CCXML")
    || "F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP.ccxml");

var names = [".*Cortex_R5_0_0", ".*Cortex_R5_0_1", ".*Cortex_R5_1_0", ".*Cortex_R5_1_1"];
for (var i=0; i<names.length; i++) {
    try {
        var s = ds.openSession(names[i]);
        s.target.connect();
        var halted = s.target.isHalted();
        var pc = "?";
        try { pc = "0x" + java.lang.Long.toHexString(s.expression.evaluate("PC") & 0xFFFFFFFF); } catch (e) {}
        print(names[i] + " connected, halted=" + halted + " PC=" + pc);
        try { s.target.disconnect(); } catch (e) {}
    } catch (e) {
        print(names[i] + " FAIL: " + e);
    }
}
try { ds.stop(); } catch (e) {}
