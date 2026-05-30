/*
 * pc_check.js — connect to LP via JTAG, halt R5F0, print PC and a few
 * registers + stack to see where firmware is parked. Read-only.
 */
var ScriptingEnvironment =
    Packages.com.ti.ccstudio.scripting.environment.ScriptingEnvironment;
var script = ScriptingEnvironment.instance();
var ds = script.getServer("DebugServer.1");
var ccxmlPath = java.lang.System.getenv("LP_CCXML")
             || "F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP.ccxml";
ds.setConfig(ccxmlPath);
var s = ds.openSession(".*Cortex_R5_0_0");
s.target.connect();
try { s.target.halt(); } catch (e) { print("halt threw: " + e); }
try {
    var pc  = s.expression.evaluate("PC");
    var lr  = s.expression.evaluate("LR");
    var sp  = s.expression.evaluate("SP");
    var cpsr = s.expression.evaluate("CPSR");
    print("PC   = 0x" + java.lang.Long.toHexString(pc & 0xFFFFFFFF));
    print("LR   = 0x" + java.lang.Long.toHexString(lr & 0xFFFFFFFF));
    print("SP   = 0x" + java.lang.Long.toHexString(sp & 0xFFFFFFFF));
    print("CPSR = 0x" + java.lang.Long.toHexString(cpsr & 0xFFFFFFFF));
} catch (e) {
    print("reg read threw: " + e);
}
try { s.target.disconnect(); } catch (e) {}
try { ds.stop(); } catch (e) {}
