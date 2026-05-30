/* step_init3.js — bisect with current symbol addresses (post bb_uart insert).
 * Confirms whether code reaches main(), and which init call aborts. */
var ScriptingEnvironment =
    Packages.com.ti.ccstudio.scripting.environment.ScriptingEnvironment;
var script = ScriptingEnvironment.instance();
var ds = script.getServer("DebugServer.1");
ds.setConfig(java.lang.System.getenv("LP_CCXML"));
var s = ds.openSession(".*Cortex_R5_0_0");
s.target.connect();

var BPs = {
    "_c_int00"           : 0x701cad6c,
    "main"               : 0x70154c1c,
    "System_init"        : 0x701869f0,
    "Board_init"         : 0x7014d446,
    "Drivers_open"       : 0x701962fe,
    "Board_driversOpen"  : 0x70195c4c,
    "data_abort_h"       : 0x701ca2ae,
    "user_data_abort_h"  : 0x701ca32a,
    "prefetch_abort_h"   : 0x701ca22a,
    "user_undef_h"       : 0x701ca216
};

var nameByAddr = {};
try { s.breakpoint.removeAll(); } catch (e) {}
for (var name in BPs) {
    s.breakpoint.add(BPs[name]);
    nameByAddr[BPs[name].toString(16)] = name;
}

var didReset = false;
try {
    var n = s.target.getNumResetTypes();
    for (var i = 0; i < n; i++) {
        var rt = s.target.getResetType(i);
        if (rt.isAllowed() && String(rt.getName()).toLowerCase().indexOf("system") >= 0) {
            print("Issuing: " + rt.getName());
            rt.issueReset();
            didReset = true;
            break;
        }
    }
} catch (e) { print("reset enum err: " + e); }
if (!didReset) {
    try { s.target.reset(); } catch (e) {}
}

try { s.target.halt(); } catch (e) {}

for (var i = 0; i < 30; i++) {
    try {
        s.target.run();
        var pc = s.expression.evaluate("PC") & 0xFFFFFFFF;
        var pcHex = java.lang.Long.toHexString(pc);
        var which = nameByAddr[pcHex] || "?";
        var lr = s.expression.evaluate("LR") & 0xFFFFFFFF;
        print("[" + i + "] STOP @ 0x" + pcHex + " (" + which + ")  LR=0x" +
              java.lang.Long.toHexString(lr));
        if (which.indexOf("abort") >= 0 || which.indexOf("undef") >= 0) {
            print("  >>> EXCEPTION");
            break;
        }
    } catch (e) { print("run err: " + e); break; }
}

try { s.target.halt(); } catch (e) {}
try { s.target.disconnect(); } catch (e) {}
try { ds.stop(); } catch (e) {}
