/* step_init2.js — bisect with proper system reset (system_reset.js style).
 * Edit BPs object to refine which functions to instrument. */
var ScriptingEnvironment =
    Packages.com.ti.ccstudio.scripting.environment.ScriptingEnvironment;
var script = ScriptingEnvironment.instance();
var ds = script.getServer("DebugServer.1");
ds.setConfig(java.lang.System.getenv("LP_CCXML"));
var s = ds.openSession(".*Cortex_R5_0_0");
s.target.connect();

var BPs = {
    "main"               : 0x7015d34c,
    "Board_init"         : 0x7014d446,
    "Drivers_open"       : 0x7019608e,
    "Board_driversOpen"  : 0x701959dc,
    "Board_flashOpen"    : 0x70187ed6,
    "Flash_norOspiPhyTune": 0x70164a0c,
    "NovaProto_Init"     : 0x701925ce,
    "LpRtc_Init"         : 0x701847b4,
    "LpSettings_DataInit": 0x7015de46,
    "LpSettings_Init"    : 0x70187884,
    "LpDeviceConfig_Init": 0x7018e010,
    "OrbitState_Init"    : 0x7017b01c,
    "vTaskStartScheduler": 0x70181d40,
    "nova_main_task"     : 0x7018cb7a,
    "abort_handler"      : 0x701c9e9a
};

var nameByAddr = {};
try { s.breakpoint.removeAll(); } catch (e) {}
var count = 0;
for (var name in BPs) {
    s.breakpoint.add(BPs[name]);
    nameByAddr[BPs[name].toString(16)] = name;
    count++;
}
print("BPs armed: " + count);

/* Issue a true SYSTEM reset (re-runs ROM → SBL → app from OSPI). */
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
    print("No system reset; falling back to target.reset()");
    try { s.target.reset(); } catch (e) {}
}

/* After system reset the CPU must be re-halted before we can run() it. */
try { s.target.halt(); } catch (e) {}

for (var i = 0; i < 25; i++) {
    try {
        s.target.run();   // blocks until BP hit
        var pc = s.expression.evaluate("PC") & 0xFFFFFFFF;
        var pcHex = java.lang.Long.toHexString(pc);
        var which = nameByAddr[pcHex] || "?";
        var lr = s.expression.evaluate("LR") & 0xFFFFFFFF;
        print("[" + i + "] STOP @ 0x" + pcHex + " (" + which + ")  LR=0x" +
              java.lang.Long.toHexString(lr));
        if (which === "abort_handler") { print("  >>> ABORT"); break; }
        if (which === "nova_main_task") { print("  >>> REACHED nova_main_task!"); break; }
    } catch (e) { print("run err: " + e); break; }
}

try { s.target.halt(); } catch (e) {}
try { s.target.disconnect(); } catch (e) {}
try { ds.stop(); } catch (e) {}
