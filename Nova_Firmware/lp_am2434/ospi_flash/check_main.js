/* check_main.js — does the CPU even reach main()? */
var ScriptingEnvironment =
    Packages.com.ti.ccstudio.scripting.environment.ScriptingEnvironment;
var script = ScriptingEnvironment.instance();
var ds = script.getServer("DebugServer.1");
ds.setConfig(java.lang.System.getenv("LP_CCXML"));
var s = ds.openSession(".*Cortex_R5_0_0");
s.target.connect();

try { s.breakpoint.removeAll(); } catch (e) {}
s.breakpoint.add(0x701cad6c); // _c_int00
s.breakpoint.add(0x70154c1c); // main
s.breakpoint.add(0x70154c20); // main + 4 (after first bb_uart0_puts call setup)
s.breakpoint.add(0x701ca32a); // user_data_abort_h

print("BPs: _c_int00=0x701cad6c main=0x70154c1c abort=0x701ca32a");

try {
    var n = s.target.getNumResetTypes();
    for (var i = 0; i < n; i++) {
        var rt = s.target.getResetType(i);
        if (rt.isAllowed() && String(rt.getName()).toLowerCase().indexOf("system") >= 0) {
            print("Issuing: " + rt.getName());
            rt.issueReset();
            break;
        }
    }
} catch (e) { print("reset err: " + e); }

try { s.target.halt(); } catch (e) {}

for (var i = 0; i < 6; i++) {
    try {
        /* timed run: don't block forever */
        s.target.runAsynch();
        java.lang.Thread.sleep(800);
        s.target.halt();
        var pc = s.expression.evaluate("PC") & 0xFFFFFFFF;
        var lr = s.expression.evaluate("LR") & 0xFFFFFFFF;
        print("[" + i + "] PC=0x" + java.lang.Long.toHexString(pc)
              + "  LR=0x" + java.lang.Long.toHexString(lr));
    } catch (e) { print("err: " + e); break; }
}

try { s.target.disconnect(); } catch (e) {}
try { ds.stop(); } catch (e) {}
