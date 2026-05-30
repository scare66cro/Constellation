/*
 * read_bootmode.js — read MAIN_DEVSTAT to learn what boot mode the
 * SoC actually latched at the most recent POR.
 *
 * MAIN_CTRL_MMR / MAIN_DEVSTAT @ 0x44030014.
 */
var ScriptingEnvironment =
    Packages.com.ti.ccstudio.scripting.environment.ScriptingEnvironment;
var script = ScriptingEnvironment.instance();
var ds = script.getServer("DebugServer.1");
ds.setConfig(java.lang.System.getenv("LP_CCXML"));
var s = ds.openSession(".*Cortex_R5_0_0");
s.target.connect();
try { s.target.halt(); } catch (e) {}

function rd(name, addr) {
    try {
        var v = s.memory.readData(0, addr, 32) & 0xFFFFFFFF;
        print("  " + name + " @ 0x" + java.lang.Long.toHexString(addr) +
              " = 0x" + java.lang.Long.toHexString(v));
        return v;
    } catch (e) {
        print("  " + name + " READ FAIL: " + e);
        return -1;
    }
}

print("Bootmode-related registers:");
var main_devstat = rd("MAIN_DEVSTAT", 0x44030014);
rd("WKUP_DEVSTAT", 0x43000014);

if (main_devstat !== -1) {
    var primary = main_devstat & 0xff;
    var backup  = (main_devstat >> 8) & 0xff;
    var bin = "";
    for (var b = 7; b >= 0; b--) bin += ((primary >> b) & 1);
    print("  Primary bootmode bits (sw1..sw8): " + bin +
          "  (raw 0x" + java.lang.Long.toHexString(primary) + ")");
    var bin2 = "";
    for (var b2 = 7; b2 >= 0; b2--) bin2 += ((backup >> b2) & 1);
    print("  Backup  bootmode bits           : " + bin2 +
          "  (raw 0x" + java.lang.Long.toHexString(backup) + ")");
    var cfg = primary & 0x7;
    var dev = (primary >> 3) & 0xf;
    var bckup_en = (primary >> 7) & 0x1;
    print("  cfg=" + cfg + "  dev=" + dev + "  bckup_en=" + bckup_en);
    var devs = ["NOBOOT","MMCSD","EMMC","OSPI_NAND","OSPI","XSPI",
                "HYPERFLASH","UART","I2C","ETH_RGMII","ETH_RMII","DFU(USB)",
                "PCIE","SPI","XSPI_NOR_QUAD","RESERVED"];
    print("  Device decode: " + (devs[dev] || ("UNKNOWN(" + dev + ")")));
}

try { s.target.runAsynch(); } catch (e) {}
try { s.target.disconnect(); } catch (e) {}
try { ds.stop(); } catch (e) {}
