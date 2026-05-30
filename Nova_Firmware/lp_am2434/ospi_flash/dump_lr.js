/* dump_lr.js — halt and dump LR + stack to find caller of abort. */
var ScriptingEnvironment =
    Packages.com.ti.ccstudio.scripting.environment.ScriptingEnvironment;
var script = ScriptingEnvironment.instance();
var ds = script.getServer("DebugServer.1");
ds.setConfig(java.lang.System.getenv("LP_CCXML"));
var s = ds.openSession(".*Cortex_R5_0_0");
s.target.connect();
try { s.target.halt(); } catch (e) {}
function show(n) {
    try { var v = s.expression.evaluate(n) & 0xFFFFFFFF;
        print("  " + n + " = 0x" + java.lang.Long.toHexString(v)); }
    catch (e) { print("  " + n + " err"); }
}
show("PC"); show("LR"); show("SP"); show("CPSR");
show("R0"); show("R1"); show("R2"); show("R3");
try {
    var sp = s.expression.evaluate("SP") & 0xFFFFFFFF;
    print("Stack @ SP (32 dwords):");
    for (var i = 0; i < 32; i++) {
        var addr = sp + i * 4;
        var v = s.memory.readData(0, addr, 32, 1) & 0xFFFFFFFF;
        print("  +0x" + java.lang.Integer.toHexString(i*4) + " = 0x"
              + java.lang.Long.toHexString(v));
    }
} catch (e) { print("stack err: " + e); }
try { s.target.disconnect(); } catch (e) {}
try { ds.stop(); } catch (e) {}
