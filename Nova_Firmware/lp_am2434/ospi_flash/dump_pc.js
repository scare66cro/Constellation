/*
 * dump_pc.js — disassemble around PC + read 0x40 bytes
 */
var ScriptingEnvironment =
    Packages.com.ti.ccstudio.scripting.environment.ScriptingEnvironment;
var script = ScriptingEnvironment.instance();
var ds = script.getServer("DebugServer.1");
var ccxmlPath = java.lang.System.getenv("LP_CCXML");
ds.setConfig(ccxmlPath);
var s = ds.openSession(".*Cortex_R5_0_0");
s.target.connect();
try { s.target.halt(); } catch (e) {}
try {
    var pc = s.expression.evaluate("PC") & 0xFFFFFFFF;
    print("PC = 0x" + java.lang.Long.toHexString(pc));
    /* read 16 words around PC-0x10 */
    for (var i = -4; i <= 8; i++) {
        var addr = (pc + (i * 4)) & 0xFFFFFFFF;
        try {
            var w = s.memory.readWord(0, addr) & 0xFFFFFFFF;
            print("  0x" + java.lang.Long.toHexString(addr) + " : 0x" + java.lang.Long.toHexString(w));
        } catch (e2) {
            print("  0x" + java.lang.Long.toHexString(addr) + " : <unreadable>");
        }
    }
    /* try to symbolize */
    try {
        var sym = s.symbol.getName(pc);
        print("symbol@PC: " + sym);
    } catch (e3) { print("symbol lookup failed: " + e3); }
    /* check DDR/MSRAM at nova entry */
    print("");
    print("Nova entry region (0x70000000 / 0x60000000):");
    try { print("  [0x70000000] = 0x" + java.lang.Long.toHexString(s.memory.readWord(0,0x70000000)&0xFFFFFFFF)); } catch(e4){ print("  [0x70000000] = unreadable"); }
    try { print("  [0x60000000] = 0x" + java.lang.Long.toHexString(s.memory.readWord(0,0x60000000)&0xFFFFFFFF)); } catch(e5){ print("  [0x60000000] = unreadable"); }
} catch (e) {
    print("dump threw: " + e);
}
try { s.target.disconnect(); } catch (e) {}
try { ds.stop(); } catch (e) {}
