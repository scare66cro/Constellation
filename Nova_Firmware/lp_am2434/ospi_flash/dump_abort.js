/* dump_abort.js — snapshot register state when stopped in abort handler.
 * For ARM data abort: SPSR_abt holds the previous CPSR, LR_abt = abort PC + 8
 * (data abort) or +4 (prefetch). We print everything useful. */
var ScriptingEnvironment =
    Packages.com.ti.ccstudio.scripting.environment.ScriptingEnvironment;
var script = ScriptingEnvironment.instance();
var ds = script.getServer("DebugServer.1");
ds.setConfig(java.lang.System.getenv("LP_CCXML"));
var s = ds.openSession(".*Cortex_R5_0_0");
s.target.connect();
try { s.target.halt(); } catch (e) {}
function show(name) {
    try {
        var v = s.expression.evaluate(name) & 0xFFFFFFFF;
        print("  " + name + " = 0x" + java.lang.Long.toHexString(v));
    } catch (e) { print("  " + name + " FAILED: " + e); }
}
print("R5F state:");
show("PC"); show("LR"); show("SP"); show("CPSR");
show("R0"); show("R1"); show("R2"); show("R3");
show("R4"); show("R5"); show("R6"); show("R7");
/* banked registers; some debuggers expose them as LR_abt / SP_abt */
show("LR_abt"); show("SP_abt"); show("SPSR_abt");
show("LR_und"); show("SP_und"); show("SPSR_und");
show("DFSR"); show("DFAR");
try {
    /* Read the on-stack frame the SDK abort wrapper saves before
     * jumping to HwiP_user_data_abort_handler_c. The wrapper layout
     * (HwiP_armv7r_handlers_asm.S) pushes R0-R12, LR, SPSR. So the
     * first dword on the abort stack is R0 of the faulting context;
     * the last one is the saved SPSR; the LR-abt minus 8 is the PC. */
    var sp = s.expression.evaluate("SP") & 0xFFFFFFFF;
    print("Stack @ SP (16 dwords):");
    for (var i = 0; i < 16; i++) {
        var addr = sp + i * 4;
        var v = s.memory.readData(0, addr, 32, 1) & 0xFFFFFFFF;
        print("  [SP+0x" + java.lang.Integer.toHexString(i*4) + "] @0x"
              + java.lang.Long.toHexString(addr) + " = 0x"
              + java.lang.Long.toHexString(v));
    }
} catch (e) { print("stack dump failed: " + e); }
try { s.target.disconnect(); } catch (e) {}
try { ds.stop(); } catch (e) {}
