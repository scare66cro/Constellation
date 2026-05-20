/* test_firewall_after_sbl.js
 * Load sbl_dfu, let it run, halt (no reset!), then probe BANK6 read/write.
 */
var ccxmlPath = java.lang.System.getenv("LP_CCXML")
             || "F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP.ccxml";
var sbl_dfu = "C:/ti/mcu_plus_sdk_am243x_12_00_00_26/examples/drivers/boot/sbl_dfu/am243x-evm/r5fss0-0_nortos/ti-arm-clang/sbl_dfu.release.out";

importPackage(Packages.com.ti.debug.engine.scripting);
importPackage(Packages.com.ti.ccstudio.scripting.environment);
importPackage(Packages.java.lang);

var script = ScriptingEnvironment.instance();
script.setScriptTimeout(120 * 1000);
var ds = script.getServer("DebugServer.1");
ds.setConfig(ccxmlPath);
var s = ds.openSession(".*Cortex_R5_0_0");
s.target.connect();

try { s.target.halt(); } catch (e) {}
try { s.target.reset(); } catch (e) {}
try { s.target.halt(); } catch (e) {}

s.memory.fill(0x78000000, 0, 0x2000, 0);
s.memory.writeWord(0, 0x78000000, 0xE59FF004);
s.memory.writeWord(0, 0x78000004, 0x38);
s.memory.writeWord(0, 0x78000038, 0xEAFFFFFE);
try { s.target.halt(); } catch (e) {}
try { s.target.reset(); } catch (e) {}
try { s.target.halt(); } catch (e) {}

print("Loading sbl_dfu...");
s.memory.loadProgram(sbl_dfu);
try { s.target.halt(); } catch (e) {}
print("Running sbl_dfu for 4s...");
s.target.runAsynch();
java.lang.Thread.sleep(4000);
try { s.target.halt(); } catch (e) {}

var pcAfter = s.expression.evaluate("PC");
print("PC after 4s run: 0x" + java.lang.Long.toHexString(pcAfter));

print("");
print("Probing BANK6 WITHOUT reset between SBL run and probe:");
var addrs = [0x70180000, 0x701804E0, 0x70190000, 0x701B0000];
for (var i = 0; i < addrs.length; i++) {
    var a = addrs[i];
    try {
        s.memory.writeWord(0, a, 0xCAFEBABE);
        var v = s.memory.readWord(0, a) & 0xFFFFFFFF;
        print("  0x" + java.lang.Long.toHexString(a) + "  WRITE+READBACK = 0x" + java.lang.Long.toHexString(v));
    } catch (e) {
        print("  0x" + java.lang.Long.toHexString(a) + "  FAIL: " + e);
    }
}

try { s.target.disconnect(); } catch (e) {}
try { ds.stop(); } catch (e) {}
