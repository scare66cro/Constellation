/* jtag_probe.js — minimal JTAG-state diagnostic.
 *
 * Tests precisely which JTAG operations succeed/fail, so we can
 * distinguish probe issue from chip-DMSC issue from R5F-state issue.
 *
 * Each step is wrapped in try/catch so we see how far we got.
 */

var ScriptingEnvironment =
    Packages.com.ti.ccstudio.scripting.environment.ScriptingEnvironment;
var script = ScriptingEnvironment.instance();
var ds = script.getServer("DebugServer.1");
var ccxmlPath = java.lang.System.getenv("LP_CCXML")
             || "F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP_NOVA.ccxml";
ds.setConfig(ccxmlPath);

var step = 0;
function announce(name) {
    step++;
    print("[probe] step " + step + ": " + name);
}
function tryStep(name, fn) {
    announce(name);
    try { fn(); print("  OK"); }
    catch (e) { print("  FAIL: " + e); }
}

var sR5 = ds.openSession(".*MAIN_Cortex_R5_0_0");

tryStep("connect to R5_0_0",      function() { sR5.target.connect(); });
tryStep("isHalted()?",            function() { print("  -> " + sR5.target.isHalted()); });
tryStep("halt R5",                function() { sR5.target.halt(); });
tryStep("reset R5",               function() { sR5.target.reset(); });
tryStep("halt after reset",       function() { sR5.target.halt(); });
tryStep("read 4 bytes @ 0x70000000 (OCSRAM)",
        function() {
            var v = sR5.memory.readData(0, 0x70000000, 32, 1);
            print("  -> 0x" + java.lang.Integer.toHexString(v[0]));
        });
tryStep("read 4 bytes @ 0x78000000 (OCSRAM hi)",
        function() {
            var v = sR5.memory.readData(0, 0x78000000, 32, 1);
            print("  -> 0x" + java.lang.Integer.toHexString(v[0]));
        });
tryStep("write 4 bytes @ 0x78000000 = 0xDEADBEEF",
        function() { sR5.memory.writeWord(0, 0x78000000, 0xDEADBEEF); });
tryStep("read back @ 0x78000000",
        function() {
            var v = sR5.memory.readData(0, 0x78000000, 32, 1);
            print("  -> 0x" + java.lang.Integer.toHexString(v[0]));
        });
tryStep("read 4 bytes @ 0x0 (R5F ATCM)",
        function() {
            var v = sR5.memory.readData(0, 0x0, 32, 1);
            print("  -> 0x" + java.lang.Integer.toHexString(v[0]));
        });
tryStep("fill @ 0x0 length 0x40 (the failing op)",
        function() { sR5.memory.fill(0x0, 0, 0x40, 0); });
tryStep("read after fill @ 0x0",
        function() {
            var v = sR5.memory.readData(0, 0x0, 32, 1);
            print("  -> 0x" + java.lang.Integer.toHexString(v[0]));
        });

print("[probe] done.");

ds.terminateSession(sR5);
ds.stop();
script.traceWrite("");
java.lang.System.exit(0);
