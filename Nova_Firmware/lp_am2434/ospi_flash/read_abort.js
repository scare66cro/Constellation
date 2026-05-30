/*
 * read_abort.js — when SoC is stuck in HwiP_user_data_abort_handler_c,
 * read R5F fault status registers (DFSR, DFAR, IFSR, IFAR) and the
 * abort-mode LR/SP to find what address faulted and where.
 *
 * Reads CP15:
 *   DFSR  c5,0,c0,0   -> r0
 *   DFAR  c6,0,c0,0   -> r0
 *   IFSR  c5,0,c0,1   -> r0
 *   IFAR  c6,0,c0,2   -> r0
 *
 * Best-effort: just dump R0..R15, CPSR while halted in the handler.
 */
var ScriptingEnvironment =
    Packages.com.ti.ccstudio.scripting.environment.ScriptingEnvironment;
var script = ScriptingEnvironment.instance();
var ds = script.getServer("DebugServer.1");
ds.setConfig(java.lang.System.getenv("LP_CCXML"));
var s = ds.openSession(".*Cortex_R5_0_0");
s.target.connect();
try { s.target.halt(); } catch (e) {}

function dump(name) {
    try {
        var v = s.expression.evaluate(name) & 0xFFFFFFFF;
        print("  " + name + " = 0x" + java.lang.Long.toHexString(v));
    } catch (e) {
        print("  " + name + " FAIL: " + e);
    }
}

print("CPU registers at halt:");
dump("PC");
dump("LR");
dump("SP");
dump("CPSR");
dump("R0"); dump("R1"); dump("R2"); dump("R3");

// Try CP15 reads — syntax depends on probe; just attempt
print("\nCP15 fault registers (attempts):");
var tryNames = [
    "CP15_DFSR", "CP15_DFAR", "CP15_IFSR", "CP15_IFAR",
    "DFSR", "DFAR", "IFSR", "IFAR",
    "C15_DFSR", "C15_DFAR"
];
for (var i = 0; i < tryNames.length; i++) dump(tryNames[i]);

try { s.target.runAsynch(); } catch (e) {}
try { s.target.disconnect(); } catch (e) {}
try { ds.stop(); } catch (e) {}
