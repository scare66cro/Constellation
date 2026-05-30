/* catch_abort_pc.js — BP right after `mov r0, lr` in the data-abort
 * trampoline so we can read the abort-mode LR (= faulting PC + 4 or 8).
 */
var ScriptingEnvironment =
    Packages.com.ti.ccstudio.scripting.environment.ScriptingEnvironment;
var script = ScriptingEnvironment.instance();
var ds = script.getServer("DebugServer.1");
ds.setConfig(java.lang.System.getenv("LP_CCXML"));
var s = ds.openSession(".*Cortex_R5_0_0");
s.target.connect();
try { s.target.halt(); } catch (e) {}

try { s.breakpoint.removeAll(); } catch (e) {}

// Just after `mov r0, lr; mrs r2, spsr`. r0 holds raw abort-LR; r2 holds SPSR.
var BP_TRAMP = 0x701ca230;
// And BP at the test points on either side of phyTune to confirm where we get to.
var BP_PHYTUNE_ENTRY = 0x70164a0c;

s.breakpoint.add(BP_TRAMP);
s.breakpoint.add(BP_PHYTUNE_ENTRY);
print("BPs set");

s.target.reset();
print("reset OK");

for (var i = 0; i < 8; i++) {
    print("\n[" + i + "] running...");
    try {
        s.target.run();
        var pc   = s.expression.evaluate("PC")   & 0xFFFFFFFF;
        var r0   = s.expression.evaluate("R0")   & 0xFFFFFFFF;
        var r1   = s.expression.evaluate("R1")   & 0xFFFFFFFF;
        var r2   = s.expression.evaluate("R2")   & 0xFFFFFFFF;
        var lr   = s.expression.evaluate("LR")   & 0xFFFFFFFF;
        var sp   = s.expression.evaluate("SP")   & 0xFFFFFFFF;
        var cpsr = s.expression.evaluate("CPSR") & 0xFFFFFFFF;
        print("  PC=0x"   + java.lang.Long.toHexString(pc) +
              "  R0=0x"   + java.lang.Long.toHexString(r0) +
              "  R1=0x"   + java.lang.Long.toHexString(r1) +
              "  R2=0x"   + java.lang.Long.toHexString(r2) +
              "  LR=0x"   + java.lang.Long.toHexString(lr) +
              "  SP=0x"   + java.lang.Long.toHexString(sp) +
              "  CPSR=0x" + java.lang.Long.toHexString(cpsr));
        if (pc === BP_TRAMP) {
            // r0 = abort-mode LR (raw). For data abort:
            //   ARM   : faulting PC = r0 - 8
            //   Thumb : faulting PC = r0 - 4 (approximately)
            print("  >>> ABORT TRAMPOLINE HIT");
            print("  >>> Raw abort-LR (r0) = 0x" + java.lang.Long.toHexString(r0));
            print("  >>> Faulting PC if ARM   = 0x" + java.lang.Long.toHexString((r0 - 8) >>> 0));
            print("  >>> Faulting PC if Thumb = 0x" + java.lang.Long.toHexString((r0 - 4) >>> 0));
            print("  >>> SPSR (r2) T-bit = " + ((r2 & 0x20) ? "Thumb" : "ARM"));
            // Try to read DFAR/DFSR via CP15 alias (ABT mode is current)
            try {
                var dfar = s.memory.readData(0,
                    s.expression.evaluate("R15") & 0xFFFFFFFF, 32, 1);
                // ignore; we'll dump near faulting PC instead
            } catch (e) {}
            break;
        }
    } catch (e) {
        print("  err: " + e);
        break;
    }
}

try { s.target.halt(); } catch (e) {}
try { s.target.disconnect(); } catch (e) {}
try { ds.stop(); } catch (e) {}
