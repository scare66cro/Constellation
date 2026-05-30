/*
 * uniflash_run.js — Single-shot OSPI flash via DSS + auto-flasher firmware.
 *
 * Self-contained. No PowerShell orchestration, no UART menu interaction.
 *
 * Sequence:
 *   1. SOC init via sciclient_ccs_init.
 *   2. Load flasher_uart/.../sbl_jtag_uniflash.release.out (Constellation auto-flasher).
 *   3. runAsynch — flasher prints banner over UART0 (COM4) then spins
 *      waiting for g_flash_request.magic = 0xF1A5C0DE.
 *   4. Re-attach (no halt). loadRaw the file bytes into gFileBuf.
 *   5. Write g_flash_request.size, .offset, then magic = 0xF1A5C0DE.
 *   6. Poll g_flash_request.done until 1 (or timeout).
 *   7. Read g_flash_request.status. Print result. Disconnect.
 *
 * Configurable via env vars:
 *   UNIFLASH_FILE   absolute path to file to write (forward-slash)
 *   UNIFLASH_OFFSET hex string, e.g. "0x80000"  (default)
 *
 * Symbol addresses (from sbl_jtag_uniflash.release.map after build):
 *   gFileBuf        @ 0x70090000   (FILE_MAX_SIZE = 0x150000)
 *   g_flash_request @ 0x7000e24c   (24 bytes; field offsets:
 *                                    +0  magic
 *                                    +4  size
 *                                    +8  offset
 *                                    +12 status
 *                                    +16 done
 *                                    +20 progress)
 */

var sdkPath      = "C:/ti/mcu_plus_sdk_am243x_12_00_00_26";
// Per-probe override via LP_CCXML env var; see Flash-LP.ps1.
var ccxmlPath    = java.lang.System.getenv("LP_CCXML")
                || "F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP.ccxml";
var ccs_init_elf = sdkPath + "/tools/ccs_load/am243x/sciclient_ccs_init.release.out";
var flasher_elf  = "F:/Constellation/Nova_Firmware/lp_am2434/flasher_uart/ti-arm-clang/sbl_jtag_uniflash.release.out";

var GFILEBUF_ADDR;        /* resolved at runtime from flasher ELF symbols */
var GFLASHREQ_ADDR;       /* (was hardcoded; symbols moved when we added Sciclient_pmDeviceReset) */
var FIELD_MAGIC          = 0;
var FIELD_SIZE           = 4;
var FIELD_OFFSET         = 8;
var FIELD_STATUS         = 12;
var FIELD_DONE           = 16;
var FIELD_PROGRESS       = 20;
var MAGIC_GO             = 0xF1A5C0DE;

importPackage(Packages.com.ti.debug.engine.scripting);
importPackage(Packages.com.ti.ccstudio.scripting.environment);
importPackage(Packages.java.lang);
importPackage(java.io);
importPackage(java.lang);

function fileSizeBytes(path) {
    var f = new File(path);
    if (!f.isFile()) {
        print("[ERROR] File not found: " + path);
        java.lang.System.exit(2);
    }
    return f.length();
}

function go() {
    script.setScriptTimeout(1800 * 1000);  // 30 min — multi-segment XIP can take a while

    /* Build the work list. Two modes:
     *
     *   Single-file (legacy):
     *     UNIFLASH_FILE   absolute path to file
     *     UNIFLASH_OFFSET hex offset (default 0x80000)
     *
     *   Multi-file (XIP, multi-segment):
     *     UNIFLASH_MANIFEST  path to a text file. Each non-empty,
     *                         non-comment line is "<offset_hex> <abs_path>".
     *                         Lines beginning with # are ignored.
     *                         All entries flashed in order, in ONE DSS
     *                         session (auto-flasher loops back to wait
     *                         for next magic after each flash).
     *
     * If both are set, MANIFEST wins. */
    var workList = [];   // [{ file: "...", offset: int, size: int }]

    var manifestPath = java.lang.System.getenv("UNIFLASH_MANIFEST");
    if (manifestPath !== null && manifestPath != "") {
        var br = new java.io.BufferedReader(new java.io.FileReader(manifestPath));
        var line;
        while ((line = br.readLine()) !== null) {
            line = String(line).replace(/^\s+|\s+$/g, "");
            if (line.length == 0 || line.charAt(0) == "#") continue;
            var sp = line.indexOf(" ");
            if (sp < 0) sp = line.indexOf("\t");
            if (sp < 0) {
                print("[ERROR] manifest malformed line: " + line);
                java.lang.System.exit(2);
            }
            var off = parseInt(line.substring(0, sp), 16);
            var fp  = line.substring(sp + 1).replace(/^\s+/, "");
            workList.push({ file: fp, offset: off, size: fileSizeBytes(fp) });
        }
        br.close();
        if (workList.length == 0) {
            print("[ERROR] manifest is empty: " + manifestPath);
            java.lang.System.exit(2);
        }
        print("=== Constellation auto-uniflash (manifest mode) ===");
        print("  manifest   : " + manifestPath);
        print("  entries    : " + workList.length);
    } else {
        var fileStr = java.lang.System.getenv("UNIFLASH_FILE");
        if (fileStr === null) {
            fileStr = "F:/Constellation/Nova_Firmware/lp_am2434/ti-arm-clang/nova_lp.release.mcelf.hs_fs";
        }
        var offsetStr = java.lang.System.getenv("UNIFLASH_OFFSET");
        if (offsetStr === null) offsetStr = "0x80000";
        var flashOffset = parseInt(offsetStr, 16);
        workList.push({ file: fileStr, offset: flashOffset, size: fileSizeBytes(fileStr) });
        print("=== Constellation auto-uniflash (single-file mode) ===");
        print("  file       : " + fileStr);
        print("  size       : " + workList[0].size + " bytes");
        print("  ospi offset: 0x" + java.lang.Long.toHexString(flashOffset));
    }

    print("=== Step 1: DMSC board config (HS-FS) ===");
    var dsR5_0 = debugServer.openSession(".*MAIN_Cortex_R5_0_0");
    dsR5_0.target.connect();
    /* Halt + reset BEFORE touching MSRAM. The currently-running app may
     * have the MPU configured so JTAG writes to 0x78000000 fault with
     * "Error -1065 Unable to access device memory" (seen after the
     * 0.A.20 XIP-attempt boot left the LP in a half-initialized state).
     * Halting first kills the MPU; reset() then re-enters ROM with all
     * permissive defaults so the spinloop write below succeeds. */
    try { dsR5_0.target.halt(); } catch (e) { print("[step1] pre-halt: " + e); }
    try { dsR5_0.target.reset(); } catch (e) { print("[step1] pre-reset: " + e); }
    try { dsR5_0.target.halt(); } catch (e) { print("[step1] post-reset halt: " + e); }
    dsR5_0.memory.fill(0x78000000, 0, 0x2000, 0);
    dsR5_0.memory.writeWord(0, 0x78000000, 0xE59FF004);
    dsR5_0.memory.writeWord(0, 0x78000004, 0x38);
    dsR5_0.memory.writeWord(0, 0x78000038, 0xEAFFFFFE);
    dsR5_0.target.halt();
    dsR5_0.target.reset();
    dsR5_0.memory.loadProgram(ccs_init_elf);
    dsR5_0.target.halt();
    dsR5_0.target.runAsynch();
    java.lang.Thread.sleep(5000);
    dsR5_0.target.halt();
    print("  DMSC init done.");

    print("=== Step 2: Load auto-flasher ===");
    dsR5_0.target.reset();
    dsR5_0.target.halt();
    dsR5_0.memory.loadProgram(flasher_elf);
    var pc = dsR5_0.expression.evaluate("PC");
    print("  PC after load: 0x" + java.lang.Long.toHexString(pc));
    /* Resolve symbol addresses from the freshly-loaded ELF so the script
     * survives flasher source changes (g_flash_request moves whenever
     * the .data layout shifts). DSS Rhino: symbol.getAddress() returns
     * the runtime address of a global. expression.evaluate("&sym") is
     * unreliable for data symbols on this CCS version. */
    GFLASHREQ_ADDR = dsR5_0.symbol.getAddress("g_flash_request");
    GFILEBUF_ADDR  = dsR5_0.symbol.getAddress("gFileBuf");
    print("  g_flash_request @ 0x" + java.lang.Long.toHexString(GFLASHREQ_ADDR));
    print("  gFileBuf        @ 0x" + java.lang.Long.toHexString(GFILEBUF_ADDR));
    dsR5_0.target.runAsynch();
    print("  Flasher running. Waiting 2 s for it to reach poll loop...");
    java.lang.Thread.sleep(2000);

    /* === Steps 3-6: For each entry in workList, push bytes + drive
     * one g_flash_request.magic=MAGIC_GO cycle. Auto-flasher firmware
     * loops back to wait for the next magic after each completion. */
    var lastStatus = 0;
    for (var wi = 0; wi < workList.length; wi++) {
        var w = workList[wi];
        print("");
        print("--- Flash " + (wi+1) + " / " + workList.length + " ---");
        print("  file       : " + w.file);
        print("  size       : " + w.size + " bytes");
        print("  ospi offset: 0x" + java.lang.Long.toHexString(w.offset));

        if (w.size > 0x150000) {
            print("[ERROR] segment exceeds gFileBuf capacity (1.3 MB): " + w.size + " bytes");
            lastStatus = 99;
            break;
        }

        print("=== Step 3: Push file bytes via loadRaw ===");
        /* loadRaw requires a halted target. Halt briefly, push bytes +
         * request fields, then resume. Flasher's poll loop sees magic
         * and proceeds. */
        dsR5_0.target.halt();
        /* DSS signature: loadRaw(page, address, filename, typeSize, byteSwap) */
        dsR5_0.memory.loadRaw(0, GFILEBUF_ADDR, w.file, 32, false);
        print("  loadRaw complete.");

        print("=== Step 4: Set request fields then magic ===");
        dsR5_0.memory.writeWord(0, GFLASHREQ_ADDR + FIELD_SIZE,   w.size);
        dsR5_0.memory.writeWord(0, GFLASHREQ_ADDR + FIELD_OFFSET, w.offset);
        dsR5_0.memory.writeWord(0, GFLASHREQ_ADDR + FIELD_STATUS, 0);
        dsR5_0.memory.writeWord(0, GFLASHREQ_ADDR + FIELD_DONE,   0);
        dsR5_0.memory.writeWord(0, GFLASHREQ_ADDR + FIELD_PROGRESS, 0);
        /* Write magic LAST so flasher only proceeds with all fields valid. */
        dsR5_0.memory.writeWord(0, GFLASHREQ_ADDR + FIELD_MAGIC,  MAGIC_GO);
        print("  magic written; resuming flasher.");
        dsR5_0.target.runAsynch();

        print("=== Step 5: Poll done flag ===");
        /* DSS DAP cannot read R5F memory while target is running on this
         * device. Halt briefly each poll to read status, then resume. */
        var startMs = java.lang.System.currentTimeMillis();
        var lastProg = -1;
        var done = 0;
        while (true) {
            dsR5_0.target.halt();
            done = dsR5_0.memory.readWord(0, GFLASHREQ_ADDR + FIELD_DONE);
            var prog = dsR5_0.memory.readWord(0, GFLASHREQ_ADDR + FIELD_PROGRESS);
            if (done == 0) dsR5_0.target.runAsynch();
            if (prog != lastProg) {
                print("  progress: " + prog + " %");
                lastProg = prog;
            }
            if (done != 0) {
                print("  flasher reports DONE.");
                break;
            }
            if ((java.lang.System.currentTimeMillis() - startMs) > 480000) {
                print("[ERROR] timeout waiting for flasher done.");
                break;
            }
            java.lang.Thread.sleep(500);
        }

        /* Read final status with target halted. */
        lastStatus = dsR5_0.memory.readWord(0, GFLASHREQ_ADDR + FIELD_STATUS);
        print("  status = " + lastStatus + "  (0 = success)");
        if (lastStatus != 0) {
            print("[ERROR] flash failed for " + w.file + " — aborting remaining segments.");
            break;
        }

        /* Clear magic so the flasher's "wait for magic to clear" loop
         * exits and goes back to waiting for the next MAGIC_GO. Then
         * also clear done so the next iteration starts fresh. */
        dsR5_0.memory.writeWord(0, GFLASHREQ_ADDR + FIELD_MAGIC, 0);
        dsR5_0.memory.writeWord(0, GFLASHREQ_ADDR + FIELD_DONE,  0);
        /* Resume so the flasher progresses to the top-of-loop wait. */
        dsR5_0.target.runAsynch();
        java.lang.Thread.sleep(200);
    }

    var status = lastStatus;
    print("=== Final result ===");
    print("  segments completed: " + (status == 0 ? workList.length : "partial"));
    print("  status = " + status + "  (0 = success)");

    /* === Step 7: Trigger SoC warm reset via auto-flasher ===
     *
     * On status==OK, set request.magic = MAGIC_REBOOT (0xF1A5DEAD). The
     * flasher's idle loop checks for this and calls
     * Sciclient_pmDeviceReset() — a TISCI_MSG_SYS_RESET to DMSC — which
     * orchestrates a full SoC warm reset that re-runs ROM/SBL and loads
     * the freshly-flashed OSPI image. This is the only verified remote
     * reboot path (target.reset() only resets the R5 core, DSS "System
     * Reset" / SW_MAIN_WARMRST register write only resets the MAIN
     * domain so ROM never re-runs — see lp-am2434-no-remote-reset.md).
     *
     * The reset takes ~1-2 s; ROM banner appears on UART0 (debug) and
     * production firmware bridge traffic on UART2 within 3-5 s.
     */
    var MAGIC_REBOOT = 0xF1A5DEAD;
    var noReset = java.lang.System.getenv("UNIFLASH_NO_RESET");
    if (status == 0 && (noReset === null || noReset == "" || noReset == "0")) {
        print("=== Step 7: Triggering SoC warm reset (Sciclient_pmDeviceReset) ===");
        try {
            dsR5_0.target.runAsynch();
            java.lang.Thread.sleep(200);
            dsR5_0.target.halt();
            dsR5_0.memory.writeWord(0, GFLASHREQ_ADDR + FIELD_MAGIC, MAGIC_REBOOT);
            dsR5_0.target.runAsynch();
            print("  reboot magic written; flasher should reset SoC within ~5 s.");
        } catch (e) {
            print("[WARN] Step 7 failed: " + e);
            print("       OSPI image is good; manual power-cycle starts it.");
        }
    }

    try { dsR5_0.target.disconnect(); } catch (e) { print("disconnect: " + e); }

    if (status != 0) java.lang.System.exit(3);
    print("=== DONE ===");
}

var ds, debugServer, script;
var withinCCS = (ds !== undefined);
if (!withinCCS) {
    script = ScriptingEnvironment.instance();
    debugServer = script.getServer("DebugServer.1");
    debugServer.setConfig(ccxmlPath);
    go();
} else {
    debugServer = ds;
    script = env;
    go();
}
