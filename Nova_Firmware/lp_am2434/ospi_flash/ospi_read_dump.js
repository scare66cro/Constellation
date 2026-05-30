/*
 * ospi_read_dump.js — diagnostic readback of OSPI flash contents.
 *
 * Loads the default sbl_jtag_uniflash auto-flasher (which does
 * Drivers_open -> Board_flashOpen -> OSPI controller init incl
 * memory-mapped XIP region), lets it reach the poll loop, halts,
 * then reads OSPI memory-mapped at 0x60000000.
 *
 * Dumps:
 *   - 64 bytes at 0x60000000 (OSPI flash offset 0x0    — SBL header)
 *   - 64 bytes at 0x62000000 (OSPI flash offset 0x2M   — PHY tuning vector)
 *
 * Independent read path from the flasher's Flash_read — catches silent
 * write corruption (the SDK's Flash_write/Flash_read pair could share
 * the same controller-state bug; memory-mapped XIP reads from the
 * controller AHB region instead).
 *
 * Expected outputs:
 *   OSPI 0x0:        starts with 0x30 0x82 (X.509 cert SEQUENCE header)
 *                    of TI HS_FS .tiimage
 *   OSPI 0x2000000:  128-byte attack vector (gOspiFlashAttackVector
 *                    in SDK source/board/flash/ospi/ — fixed constant
 *                    pattern, contains repeating "TST" / 0x55 0xAA
 *                    sequences per Cypress tuning spec)
 *   If either is 0xFF 0xFF ...   -> erased, write failed silently
 *   If random / unexpected         -> write corruption
 */

var ScriptingEnvironment =
    Packages.com.ti.ccstudio.scripting.environment.ScriptingEnvironment;
var script = ScriptingEnvironment.instance();
var ds = script.getServer("DebugServer.1");

var sdkPath     = "C:/ti/mcu_plus_sdk_am243x_12_00_00_26";
var ccxmlPath   = java.lang.System.getenv("LP_CCXML")
               || "F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP.ccxml";
var ccsInitElf  = sdkPath + "/tools/ccs_load/am243x/sciclient_ccs_init.release.out";
var flasherElf  = "F:/Constellation/Nova_Firmware/lp_am2434/flasher_uart/ti-arm-clang/sbl_jtag_uniflash.release.out";

ds.setConfig(ccxmlPath);
var s = ds.openSession(".*Cortex_R5_0_0");
s.target.connect();

print("=== Step 1: DMSC init ===");
try { s.target.reset(); } catch (e) {}
s.target.halt();
s.memory.loadProgram(ccsInitElf);
s.target.runAsynch();
java.lang.Thread.sleep(2000);
s.target.halt();
print("  DMSC firewalls open.");

print("=== Step 2: load auto-flasher (init OSPI via Drivers_open) ===");
s.memory.loadProgram(flasherElf);
print("  flasher loaded; running to reach poll loop...");
s.target.runAsynch();
java.lang.Thread.sleep(3000);
s.target.halt();
print("  halted after init.");

function hexbyte(v) {
    var h = java.lang.Long.toHexString(v & 0xff);
    if (h.length() === 1) return "0" + h;
    return "" + h;
}

function dump(addr, n, label) {
    print("");
    print("=== " + label + " @ 0x" + java.lang.Long.toHexString(addr) +
          " (OSPI offset 0x" + java.lang.Long.toHexString(addr - 0x60000000) + ") ===");
    var line = "";
    var ascii = "";
    try {
        for (var i = 0; i < n; i++) {
            var b = s.memory.readData(0, addr + i, 8) & 0xff;
            line += hexbyte(b) + " ";
            ascii += (b >= 0x20 && b < 0x7f) ? String.fromCharCode(b) : ".";
            if ((i & 0xf) === 0xf) {
                print("  +" + java.lang.Long.toHexString((i & ~0xf)) +
                      ":  " + line + " | " + ascii);
                line = "";
                ascii = "";
            }
        }
        if (line.length() > 0) {
            print("  +" + java.lang.Long.toHexString(n & ~0xf) +
                  ":  " + line + " | " + ascii);
        }
    } catch (e) {
        print("  READ FAILED: " + e);
    }
}

dump(0x60000000, 64, "OSPI 0x0 (SBL header)");
dump(0x62000000, 64, "OSPI 0x2000000 (PHY tuning vector)");

try { s.target.disconnect(); } catch (e) {}
try { ds.stop(); } catch (e) {}
