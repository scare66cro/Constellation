/* probe_msram.js — read/write probe several MSRAM bank boundaries to
 * determine which firewalls are open after the current DMSC state.
 *
 * Returns OK/FAIL per address so we can tell exactly which banks block.
 */
var ccxmlPath = java.lang.System.getenv("LP_CCXML")
             || "F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP.ccxml";

importPackage(Packages.com.ti.debug.engine.scripting);
importPackage(Packages.com.ti.ccstudio.scripting.environment);
importPackage(Packages.java.lang);

var script = ScriptingEnvironment.instance();
script.setScriptTimeout(60 * 1000);
var ds = script.getServer("DebugServer.1");
ds.setConfig(ccxmlPath);
var s = ds.openSession(".*Cortex_R5_0_0");
s.target.connect();
try { s.target.halt(); } catch (e) {}

/* MSRAM bank starts on AM243x (per SDK bootloader_soc.c):
 *   BANK0 0x70000000, BANK1 0x70040000, BANK2 0x70080000, BANK3 0x700C0000,
 *   BANK4 0x70100000, BANK5 0x70140000, BANK6 0x70180000, BANK7 0x701C0000 */
var addrs = [0x70000000, 0x70040000, 0x70080000, 0x700C0000,
             0x70100000, 0x70140000, 0x70180000, 0x701C0000,
             0x701804E0];  // exact failing offset from loadProgram

for (var i = 0; i < addrs.length; i++) {
    var a = addrs[i];
    var r = "<unread>";
    var w = "<unwritten>";
    try {
        r = "0x" + java.lang.Long.toHexString(s.memory.readWord(0, a) & 0xFFFFFFFF);
    } catch (e) { r = "READ-FAIL: " + e; }
    try {
        s.memory.writeWord(0, a, 0xDEADBEEF);
        var v = s.memory.readWord(0, a) & 0xFFFFFFFF;
        w = (v == 0xDEADBEEF) ? "WRITE-OK" : ("WRITE-MISMATCH 0x" + java.lang.Long.toHexString(v));
    } catch (e) { w = "WRITE-FAIL: " + e; }
    print("  0x" + java.lang.Long.toHexString(a) + "  read=" + r + "  " + w);
}

try { s.target.disconnect(); } catch (e) {}
try { ds.stop(); } catch (e) {}
