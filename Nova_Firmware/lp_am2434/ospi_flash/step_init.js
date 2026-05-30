/*
 * step_init.js — JTAG-bisect early init. Sets BPs at main, System_init,
 * Board_init, Drivers_open, Board_driversOpen, and the abort handler.
 * Resets target, runs, reports which BP we hit (or 'abort' if abort fires).
 */
var ScriptingEnvironment =
    Packages.com.ti.ccstudio.scripting.environment.ScriptingEnvironment;
var script = ScriptingEnvironment.instance();
var ds = script.getServer("DebugServer.1");
ds.setConfig(java.lang.System.getenv("LP_CCXML"));
var s = ds.openSession(".*Cortex_R5_0_0");
s.target.connect();

// SBL already loaded our app at 0x80000 in OSPI which copies to 0x70080000
// MSRAM and starts. Just halt and inspect — don't reset (would reload SBL).
try { s.target.halt(); } catch (e) { print("halt err: " + e); }

var BPs = {
    "Board_driversOpen"   : 0x70195af8,
    "LpDeviceConfig_Init" : 0x7016a1fe,
    "OrbitState_Init"     : 0x7017b1a4,
    "vTaskStartScheduler" : 0x70181ec8,
    "nova_main_task"      : 0x7018cc42,
    "enet_task_entry"     : 0x701965c4,
    "orbit_modbus_tcp_task": 0x7014b1ec,
    "orbit_safemode_task" : 0x7017ace8,
    "orbit_sensor_rtu_task": 0x70172be0,
    "OrbitGdc_Init"       : 0x70156eb6,
    "orbit_gdc_task"      : 0x70153754,
    "abort_handler"       : 0x701ca0aa
};

// Clear any old BPs
try { s.breakpoint.removeAll(); } catch (e) {}

var nameByAddr = {};
for (var name in BPs) {
    try {
        s.breakpoint.add(BPs[name]);
        nameByAddr[BPs[name].toString(16)] = name;
        print("BP set @ 0x" + BPs[name].toString(16) + " (" + name + ")");
    } catch (e) {
        print("BP fail " + name + ": " + e);
    }
}

// Issue a system reset so we run from cold-boot path
try {
    print("Issuing system reset...");
    s.target.reset();
    print("reset OK");
} catch (e) {
    print("reset err: " + e);
}

// Now run and wait for first BP
for (var i = 0; i < 30; i++) {
    try {
        print("\n[" + i + "] running...");
        s.target.run();   // blocks until BP hit OR halt timeout
        var pc = s.expression.evaluate("PC") & 0xFFFFFFFF;
        var lr = s.expression.evaluate("LR") & 0xFFFFFFFF;
        var cpsr = s.expression.evaluate("CPSR") & 0xFFFFFFFF;
        var pcHex = java.lang.Long.toHexString(pc);
        var which = nameByAddr[pcHex] || "?";
        print("  STOP @ 0x" + pcHex + " (" + which + ")  LR=0x" +
              java.lang.Long.toHexString(lr) + "  CPSR=0x" +
              java.lang.Long.toHexString(cpsr));
        if (which === "abort_handler") {
            print("  >>> ABORT REACHED. LR_abt = 0x" +
                  java.lang.Long.toHexString(lr));
            break;
        }
    } catch (e) {
        print("  run/halt err: " + e);
        break;
    }
}

try { s.target.halt(); } catch (e) {}
try { s.target.disconnect(); } catch (e) {}
try { ds.stop(); } catch (e) {}
