/* halt R5F core 0 — no other action. Use to prove whether firmware is
 * sourcing UART tx by watching bridge rxFrames stall. */
var ScriptingEnvironment =
    Packages.com.ti.ccstudio.scripting.environment.ScriptingEnvironment;
var script = ScriptingEnvironment.instance();
var ds = script.getServer("DebugServer.1");
var ccxmlPath = java.lang.System.getenv("LP_CCXML")
             || "F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP.ccxml";
ds.setConfig(ccxmlPath);
var s = ds.openSession(".*Cortex_R5_0_0");
s.target.connect();
try { s.target.halt(); print("halted"); var pc = s.expression.evaluate("PC"); print("PC=0x" + java.lang.Long.toHexString(pc & 0xFFFFFFFF)); } catch (e) { print("halt failed: " + e); }
try { s.target.disconnect(); } catch (e) {}
try { ds.stop(); } catch (e) {}
