/* catch_abort_entry.js — break at the EARLIEST point of abort:
 * the asm HwiP_data_abort_handler. At that moment LR holds the
 * abort link (faulting PC + 8 in ARM, +4 in Thumb). Print LR + SPSR.
 *
 * Also catches prefetch_abort and undefined to disambiguate which
 * exception is firing. */
var ScriptingEnvironment =
    Packages.com.ti.ccstudio.scripting.environment.ScriptingEnvironment;
var script = ScriptingEnvironment.instance();
var ds = script.getServer("DebugServer.1");
ds.setConfig(java.lang.System.getenv("LP_CCXML"));
var s = ds.openSession(".*Cortex_R5_0_0");
s.target.connect();

try { s.breakpoint.removeAll(); } catch (e) {}
s.breakpoint.add(0x701ca528); // HwiP_data_abort_handler (asm entry)
s.breakpoint.add(0x701ca56c); // HwiP_prefetch_abort_handler
s.breakpoint.add(0x701ca5b8); // HwiP_undefined_handler

print("Armed BPs at asm exception entries.");

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

/* Run blocking — should hit one of the BPs immediately if abort fires. */
try {
    s.target.run();
    var pc = s.expression.evaluate("PC") & 0xFFFFFFFF;
    var lr = s.expression.evaluate("LR") & 0xFFFFFFFF;
    var sp = s.expression.evaluate("SP") & 0xFFFFFFFF;
    var cpsr = s.expression.evaluate("CPSR") & 0xFFFFFFFF;
    var which = "?";
    if (pc == 0x701ca528) which = "DATA_ABORT";
    else if (pc == 0x701ca56c) which = "PREFETCH_ABORT";
    else if (pc == 0x701ca5b8) which = "UNDEFINED";
    print("STOP @ 0x" + java.lang.Long.toHexString(pc) + " (" + which + ")");
    print("  LR (return addr from exception, faulting PC + 8 ARM / + 4 Thumb)");
    print("    LR  = 0x" + java.lang.Long.toHexString(lr));
    if (which === "DATA_ABORT") {
        var thumbLR = lr - 4;   /* SUBNE lr,lr,#6 in handler subtracts further */
        var armLR   = lr - 4;
        print("  Suspect faulting PC: ARM=" + java.lang.Long.toHexString(armLR - 4)
              + " Thumb=" + java.lang.Long.toHexString(thumbLR - 4));
    }
    print("  SP  = 0x" + java.lang.Long.toHexString(sp));
    print("  CPSR= 0x" + java.lang.Long.toHexString(cpsr) + "  (mode=" + (cpsr & 0x1f).toString(16) + ")");
} catch (e) { print("run err: " + e); }

try { s.target.halt(); } catch (e) {}
try { s.target.disconnect(); } catch (e) {}
try { ds.stop(); } catch (e) {}
