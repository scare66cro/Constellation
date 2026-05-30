/*
 * pc_alive.js — sample PC across multiple halt/run cycles to confirm
 * R5F is executing instructions vs. wedged.
 */
var ScriptingEnvironment =
    Packages.com.ti.ccstudio.scripting.environment.ScriptingEnvironment;
var script = ScriptingEnvironment.instance();
var ds = script.getServer("DebugServer.1");
ds.setConfig(java.lang.System.getenv("LP_CCXML"));
var s = ds.openSession(".*Cortex_R5_0_0");
s.target.connect();
print("Sampling PC 5x with 500 ms run between samples:");
for (var i = 0; i < 5; i++) {
    try {
        s.target.halt();
        var pc = s.expression.evaluate("PC") & 0xFFFFFFFF;
        print("  sample " + i + " PC = 0x" + java.lang.Long.toHexString(pc));
        s.target.runAsynch();
    } catch (e) { print("  sample " + i + " failed: " + e); }
    java.lang.Thread.sleep(500);
}
try { s.target.halt(); } catch (e) {}
try { s.target.disconnect(); } catch (e) {}
try { ds.stop(); } catch (e) {}
