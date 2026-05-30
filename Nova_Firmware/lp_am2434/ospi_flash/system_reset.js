/*
 * Force a full SoC reset via DSS so the SBL re-runs and loads the
 * freshly-flashed application from OSPI.
 *
 * DSS exposes ResetType objects via target.getNumResetTypes() /
 * target.getResetType(i). Each has getName(), getDescription(),
 * isAllowed(), and issueReset(). We pick the most aggressive allowed
 * reset whose name matches Board / System / POR (those re-run the ROM
 * bootloader and therefore SBL from OSPI).
 *
 * If no matching ResetType is allowed (target locked while halted, etc.)
 * we fall back to writing the AM243x MAIN CTRL_MMR0 SW_MAIN_WARMRST bits
 * after kick-unlocking partition 6 (mirrors SDK SOC_generateSwWarmReset-
 * MainDomain in source/drivers/soc/am64x_am243x/soc.c).
 */
var ScriptingEnvironment =
    Packages.com.ti.ccstudio.scripting.environment.ScriptingEnvironment;
var script = ScriptingEnvironment.instance();
var ds = script.getServer("DebugServer.1");
// Per-probe override via LP_CCXML env var; see Flash-LP.ps1.
var ccxmlPath = java.lang.System.getenv("LP_CCXML")
             || "F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP.ccxml";
ds.setConfig(ccxmlPath);
var s = ds.openSession(".*Cortex_R5_0_0");
s.target.connect();

var didReset = false;

try {
    var n = s.target.getNumResetTypes();
    print("ResetType count: " + n);
    var prefer = ["board", "system", "hard", "por", "warm", "soft"];
    var found = [];
    for (var i = 0; i < n; i++) {
        var rt = s.target.getResetType(i);
        var nm  = String(rt.getName());
        var ds_ = String(rt.getDescription());
        var al  = rt.isAllowed();
        print("  [" + i + "] name='" + nm + "' allowed=" + al + " desc='" + ds_ + "'");
        if (al) found.push({ idx: i, name: nm.toLowerCase(), obj: rt });
    }
    for (var p = 0; p < prefer.length && !didReset; p++) {
        for (var k = 0; k < found.length && !didReset; k++) {
            if (found[k].name.indexOf(prefer[p]) >= 0) {
                print("Issuing reset: '" + found[k].name + "'");
                try {
                    found[k].obj.issueReset();
                    didReset = true;
                } catch (eIssue) {
                    print("  issueReset threw: " + eIssue);
                }
            }
        }
    }
    if (!didReset && found.length > 0) {
        print("No preferred match; issuing first allowed reset: '" +
              found[0].name + "'");
        try {
            found[0].obj.issueReset();
            didReset = true;
        } catch (eIssue2) {
            print("  issueReset threw: " + eIssue2);
        }
    }
} catch (eEnum) {
    print("ResetType enumeration failed: " + eEnum);
}

if (!didReset) {
    print("Falling back: writing CTRL_MMR0 SW_MAIN_WARMRST via partition-6 unlock.");
    try { s.target.halt(); } catch (eh) {}
    /* AM243x CTRL_MMR0_CFG0_BASE = 0x43000000.
     *   LOCK_n_KICK0 offset = 0x1008 + 0x4000 * n   (n=6 -> 0x19008)
     *   LOCK_n_KICK1 = LOCK_n_KICK0 + 4
     *   KICK0 = 0x68EF3490, KICK1 = 0xD172BC5A
     * RST_CTRL is at offset 0x18170, SW_MAIN_WARMRST = bits[3:0] = 0x6.
     * Source: SDK source/drivers/soc/am64x_am243x/soc.c. */
    try {
        s.memory.writeWord(0, 0x43019008, 0x68EF3490);
        s.memory.writeWord(0, 0x4301900C, 0xD172BC5A);
        var cur = s.memory.readWord(0, 0x43018170) & 0xFFFFFFF0;
        s.memory.writeWord(0, 0x43018170, cur | 0x00000006);
        print("  warm-reset triggered (RST_CTRL = 0x" +
              java.lang.Long.toHexString((cur | 0x6) & 0xFFFFFFFF) + ").");
    } catch (ew) {
        print("  CTRL_MMR0 write failed: " + ew);
    }
}

/* After a board/system reset the JTAG link drops.  Don't try to resume;
 * just disconnect cleanly and let SBL run from OSPI. */
try { s.target.disconnect(); } catch (e) {}
try { ds.stop(); } catch (e) {}
