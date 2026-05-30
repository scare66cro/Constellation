/* read_str.js — read string at given address. */
var ScriptingEnvironment =
    Packages.com.ti.ccstudio.scripting.environment.ScriptingEnvironment;
var script = ScriptingEnvironment.instance();
var ds = script.getServer("DebugServer.1");
ds.setConfig(java.lang.System.getenv("LP_CCXML"));
var s = ds.openSession(".*Cortex_R5_0_0");
s.target.connect();
try { s.target.halt(); } catch (e) {}
var addr = 0x70096748;
var str = "";
for (var i = 0; i < 200; i++) {
    var b = s.memory.readData(0, addr + i, 8, 1) & 0xFF;
    if (b == 0) break;
    str += String.fromCharCode(b);
}
print("STR @ 0x" + java.lang.Long.toHexString(addr) + " = '" + str + "'");
try { s.target.disconnect(); } catch (e) {}
try { ds.stop(); } catch (e) {}
