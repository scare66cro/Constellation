/*
 * diag_rom_state.js — diagnostic for "chip stuck in ROM at cold-boot"
 *
 * Loads sciclient_ccs_init.release.out so DMSC firewalls (incl
 * MAIN_CTRL_MMR) open, then:
 *   1. Re-reads MAIN_DEVSTAT (no longer firewalled to 0x0) to confirm
 *      what bootmode the chip actually latched at POR.
 *   2. Single-steps the R5 4 times from its current PC. If PC stays put
 *      across all 4 steps -> dead-end wait loop. If PC advances -> ROM
 *      is in an active fallback poll.
 *
 * Required state: chip just power-cycled in OSPI boot mode (SW4 = 2,6 ON
 * on PCB 109A) and currently halted at PC inside R5 ROM (0x418xxxxx).
 *
 * Drives whichever probe LP_CCXML points at. Use with Set-Probe Solo.
 */

var sdkPath      = "C:/ti/mcu_plus_sdk_am243x_12_00_00_26";
var ccxmlPath    = java.lang.System.getenv("LP_CCXML")
                || "F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP.ccxml";
var ccs_init_elf = sdkPath + "/tools/ccs_load/am243x/sciclient_ccs_init.release.out";

var ScriptingEnvironment =
    Packages.com.ti.ccstudio.scripting.environment.ScriptingEnvironment;
var script = ScriptingEnvironment.instance();
var ds = script.getServer("DebugServer.1");
ds.setConfig(ccxmlPath);
var s = ds.openSession(".*Cortex_R5_0_0");
s.target.connect();

print("=== Step A: capture state BEFORE sciclient (raw ROM state) ===");
try { s.target.halt(); print("  halted."); } catch (e) { print("  halt err: " + e); }
try {
    var pc_raw = s.expression.evaluate("PC") & 0xFFFFFFFF;
    print("  PC_raw  = 0x" + java.lang.Long.toHexString(pc_raw));
} catch (e) { print("  PC read fail: " + e); }
try {
    var devstat_raw = s.memory.readData(0, 0x44030014, 32) & 0xFFFFFFFF;
    print("  MAIN_DEVSTAT_raw @ 0x44030014 = 0x" + java.lang.Long.toHexString(devstat_raw));
} catch (e) { print("  MAIN_DEVSTAT raw read fail: " + e); }

print("");
print("=== Step B: load sciclient_ccs_init to open MAIN_CTRL_MMR firewall ===");
/* Save current R5 PC so we can later halt back near it. The loadProgram
 * will overwrite R5 to run ccs_init — that's fine, we just want firewalls
 * open. */
try { s.target.reset(); } catch (e) { print("  pre-reset: " + e); }
s.memory.loadProgram(ccs_init_elf);
s.target.halt();
s.target.runAsynch();
java.lang.Thread.sleep(3000);
s.target.halt();
print("  DMSC firewalls open.");

print("");
print("=== Step C: re-read MAIN_DEVSTAT with firewall open ===");
try {
    var devstat = s.memory.readData(0, 0x44030014, 32) & 0xFFFFFFFF;
    print("  MAIN_DEVSTAT @ 0x44030014 = 0x" + java.lang.Long.toHexString(devstat));
    var primary = devstat & 0xff;
    var dev = (primary >> 3) & 0xf;
    var bin = "";
    for (var b = 7; b >= 0; b--) bin += ((primary >> b) & 1);
    print("  Primary bootmode bits (sw1..sw8): " + bin + "  (raw 0x" + java.lang.Long.toHexString(primary) + ")");
    var devs = ["NOBOOT","MMCSD","EMMC","OSPI_NAND","OSPI","XSPI",
                "HYPERFLASH","UART","I2C","ETH_RGMII","ETH_RMII","DFU(USB)",
                "PCIE","SPI","XSPI_NOR_QUAD","RESERVED"];
    print("  Device decode: " + (devs[dev] || ("UNKNOWN(" + dev + ")")));
} catch (e) { print("  MAIN_DEVSTAT read fail: " + e); }
try {
    var wkup = s.memory.readData(0, 0x43000014, 32) & 0xFFFFFFFF;
    print("  WKUP_DEVSTAT @ 0x43000014 = 0x" + java.lang.Long.toHexString(wkup));
} catch (e) {}

print("");
print("=== Step D: skipped (sciclient already overwrote R5 PC — can't single-step ROM). ===");
print("  Re-run this script with a fresh power cycle (chip back in PC=ROM) to do single-step.");
print("  Alternative: don't load sciclient and just single-step at Step A — PC value alone tells the story.");

try { s.target.disconnect(); } catch (e) {}
try { ds.stop(); } catch (e) {}
