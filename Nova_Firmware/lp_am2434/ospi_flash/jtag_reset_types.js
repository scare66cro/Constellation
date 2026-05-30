/* Enumerate available reset types and try the most aggressive one. */
var ScriptingEnvironment =
    Packages.com.ti.ccstudio.scripting.environment.ScriptingEnvironment;
var script = ScriptingEnvironment.instance();
var ds = script.getServer("DebugServer.1");
var ccxmlPath = java.lang.System.getenv("LP_CCXML")
             || "F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP_NOVA.ccxml";
ds.setConfig(ccxmlPath);
var s = ds.openSession(".*MAIN_Cortex_R5_0_0");
s.target.connect();

print("Available reset types on R5_0_0:");
var n = s.target.getNumResetTypes();
for (var i = 0; i < n; i++) {
    var rt = s.target.getResetType(i);
    print("  [" + i + "] " + rt.getName() + " — " + rt.getDescription() +
          " (allowed=" + rt.isAllowed() + ")");
}

print("");
print("Trying most aggressive allowed reset (Board > System > POR > others)...");
var preferred = ["Board Reset", "System Reset", "POR Reset", "Power-On Reset"];
var picked = null;
for (var p = 0; p < preferred.length; p++) {
    for (var i = 0; i < n; i++) {
        var rt = s.target.getResetType(i);
        if (rt.getName().toLowerCase().indexOf(preferred[p].toLowerCase()) >= 0 && rt.isAllowed()) {
            picked = rt;
            break;
        }
    }
    if (picked) break;
}
if (!picked) {
    /* fallback: highest-index allowed reset */
    for (var i = n - 1; i >= 0; i--) {
        var rt = s.target.getResetType(i);
        if (rt.isAllowed()) { picked = rt; break; }
    }
}
if (picked) {
    print("Issuing: " + picked.getName());
    try { picked.issueReset(); print("  done."); }
    catch (e) { print("  failed: " + e); }
} else {
    print("No allowed reset types — that's bad.");
}

print("");
print("State after reset:");
try { print("  isHalted = " + s.target.isHalted()); } catch (e) { print("  isHalted err: " + e); }
try {
    var v = s.memory.readData(0, 0x0, 32, 1);
    print("  read @ 0x0 = 0x" + java.lang.Long.toHexString(v[0] & 0xFFFFFFFF));
} catch (e) { print("  read 0x0 err: " + e); }
try {
    s.memory.writeWord(0, 0x0, 0x12345678);
    print("  write @ 0x0 = OK");
} catch (e) { print("  write 0x0 err: " + e); }

print("done.");
java.lang.System.exit(0);
