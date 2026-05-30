/*
 * Force SW_MAIN_WARMRST on the AM2434 MAIN domain via JTAG.
 *
 * DSS's "system reset" only resets the R5F cores — CPSW peripheral and
 * DMSC RM allocations survive (see memories/repo/lp-am2434-cpsw-reflash-trap.md).
 * The AM243x CTRL_MMR0 RST_CTRL register can pulse the MAIN-domain
 * WARMRSTn signal, which resets every MAIN-domain peripheral including
 * CPSW0. That clears the "ghost CPSW from old boot" condition without
 * a physical USB-C cycle.
 *
 * Mirrors SDK source/drivers/soc/am64x_am243x/soc.c::
 *   SOC_generateSwWarmResetMainDomain()
 *
 * Register map (CTRL_MMR0_CFG0_BASE = 0x43000000):
 *   0x43019008 LOCK6_KICK0 = 0x68EF3490
 *   0x4301900C LOCK6_KICK1 = 0xD172BC5A
 *   0x43018170 RST_CTRL    : bits[3:0] = 0x6 → SW_MAIN_WARMRST
 *
 * After write the JTAG link drops; the SBL re-runs from ROM and loads
 * the OSPI image. Disconnect cleanly so DSS doesn't report a stuck
 * session next run.
 */
var ScriptingEnvironment =
    Packages.com.ti.ccstudio.scripting.environment.ScriptingEnvironment;
var script = ScriptingEnvironment.instance();
var ds = script.getServer("DebugServer.1");
var ccxmlPath = java.lang.System.getenv("LP_CCXML")
             || "F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP.ccxml";
ds.setConfig(ccxmlPath);
var s = ds.openSession(".*Cortex_R5_0_0");
s.target.connect();

try {
    try { s.target.halt(); } catch (eh) { print("halt failed: " + eh); }

    print("Unlocking CTRL_MMR0 partition 6...");
    s.memory.writeWord(0, 0x43019008, 0x68EF3490);
    s.memory.writeWord(0, 0x4301900C, 0xD172BC5A);

    var cur = s.memory.readWord(0, 0x43018170) & 0xFFFFFFF0;
    print("  RST_CTRL before = 0x" +
          java.lang.Long.toHexString(cur & 0xFFFFFFFF));
    print("  Writing SW_MAIN_WARMRST (0x6)...");
    s.memory.writeWord(0, 0x43018170, cur | 0x00000006);
    print("  RST_CTRL written.");
} catch (ew) {
    print("Warm-reset write failed: " + ew);
}

try { s.target.disconnect(); } catch (e) {}
try { ds.stop(); } catch (e) {}
print("Done. Wait ~10 s for SBL to reload from OSPI.");
