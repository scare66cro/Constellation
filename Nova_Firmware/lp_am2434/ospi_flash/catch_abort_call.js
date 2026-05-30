/* catch_abort_call.js — BP at abort(), reset, run, capture LR at abort entry. */
var ScriptingEnvironment =
    Packages.com.ti.ccstudio.scripting.environment.ScriptingEnvironment;
var script = ScriptingEnvironment.instance();
var ds = script.getServer("DebugServer.1");
ds.setConfig(java.lang.System.getenv("LP_CCXML"));
var s = ds.openSession(".*Cortex_R5_0_0");
s.target.connect();

try { s.breakpoint.removeAll(); } catch (e) {}
s.breakpoint.add(0x70098820); // abort()
print("BP at abort=0x70098820");

try {
    var n = s.target.getNumResetTypes();
    for (var i = 0; i < n; i++) {
        var rt = s.target.getResetType(i);
        if (rt.isAllowed() && String(rt.getName()).toLowerCase().indexOf("system") >= 0) {
            print("Issuing: " + rt.getName());
            rt.issueReset(); break;
        }
    }
} catch (e) { print("reset err: " + e); }
try { s.target.halt(); } catch (e) {}

try {
    s.target.run();
    var pc = s.expression.evaluate("PC") & 0xFFFFFFFF;
    var lr = s.expression.evaluate("LR") & 0xFFFFFFFF;
    var sp = s.expression.evaluate("SP") & 0xFFFFFFFF;
    var cpsr = s.expression.evaluate("CPSR") & 0xFFFFFFFF;
    print("STOP @ PC=0x" + java.lang.Long.toHexString(pc));
    print("  LR  = 0x" + java.lang.Long.toHexString(lr) + "  (caller of abort, -4 for ARM, -2 for Thumb)");
    print("  SP  = 0x" + java.lang.Long.toHexString(sp));
    print("  CPSR= 0x" + java.lang.Long.toHexString(cpsr));
    /* Read 8 stack dwords for back-trace */
    print("Stack:");
    for (var i = 0; i < 8; i++) {
        var v = s.memory.readData(0, sp + i*4, 32, 1) & 0xFFFFFFFF;
        print("  +0x" + java.lang.Integer.toHexString(i*4) + " = 0x" + java.lang.Long.toHexString(v));
    }
} catch (e) { print("run err: " + e); }

try { s.target.halt(); } catch (e) {}
try { s.target.disconnect(); } catch (e) {}
try { ds.stop(); } catch (e) {}
