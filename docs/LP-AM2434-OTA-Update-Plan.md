# LP-AM2434 Orbit Board OTA Update — Design Plan

> **Status (2026-05-13): RUNTIME OTA WORKS.** The 2026-05-12 "bridge-side flash only" pivot below is **OBSOLETE**. After exhaustive forensic work across 0.A.118-0.A.140, the root cause was finally found: the SDK's `Flash_norOspiWrite` / `OSPI_lld_writeIndirect` (INDIRECT_WRITE_XFER subsystem) IS broken in our FreeRTOS+CPSW runtime context, BUT the OSPI controller has a completely separate **DAC (Direct Access Controller) mode** write path that works fine — it just needed a `CacheP_wbInv` after the memcpy (SDK does this in `OSPI_writeDirect` at `drivers/ospi/v0/ospi_v0.c:994`; we missed it). New public function `hal_flash_write_dac()` in [`Nova_Firmware/Platform/hal_flash.c`](../Nova_Firmware/Platform/hal_flash.c). Bench-verified in 0.A.140-0.A.141: 32 KB programmed via OTA in 73 ms with 5 sample-byte readbacks matching source. Full resolution write-up: [`memories/repo/lp-am2434-ota-dac-mode-fix.md`](../memories/repo/lp-am2434-ota-dac-mode-fix.md).
>
> **Implications for the plan below:**
> - **Phase 1B / Phase 2 are NO LONGER DEPRECATED** — runtime OTA write works via DAC mode.
> - **Bridge-side flash via auto-flasher REMAINS a valid path** for first-flash-from-bare-chip and recovery scenarios where the LP isn't running.
> - **Customers can now update LP firmware over the network** via the existing OTA listener on `:5503` (Cloud → bridge → TCP to LP :5503 → DAC-mode write to Bank B → optional copy to Bank A + warm-reset).
> - Phase 3 (custom SBL chooser for A/B banking with hardware rollback) is still desirable for production but no longer strictly required — Bank A → Bank A overwrite with verify+rollback-via-watchdog-reset is workable as an interim.
>
> **Still-open work items:**
> - DDR-backed image buffer (current 64 KB MSRAM buffer is sufficient for test images but real LP firmware is ~485 KB) — needed before shipping
> - Two-stage activate flow polished: write to Bank B → verify → erase Bank A → copy B→A via `hal_flash_write_dac` → warm-reset. `lp_ota_stage_copy_b_to_a` already updated to use DAC; the activate handler's `reboot=1` path now drives this in 0.A.141 but it's only exercised when the bridge requests reboot.
> - Bridge-side image signing + version metadata so the LP can refuse downgrades / unsigned images
> - UI integration so an operator can trigger an LP firmware update from the panel
>
> Below sections preserved for context but the deprecation strikethroughs from 2026-05-12 should be lifted in a future docs pass.

> **Goal:** Push new orbit firmware (`nova_lp.release.mcelf.hs_fs`) from
> the rpi5 to STORAGE / GDC / TRITON boards over the existing Ethernet
> LAN, with no JTAG, no USB, no DIP-switch toggling.

## 2026-05-06 design update — dual-mcelf reality

As of fw `0.A.61`, every LP image is **two independently signed
mcelfs** flashed at separate OSPI partitions, not one bundle. See
[`/memories/repo/lp-am2434-watchdog-mcelf-trap.md`](../memories/repo/lp-am2434-watchdog-mcelf-trap.md)
for the full root-cause story.

| Image | OSPI offset | Cluster | Purpose |
|---|---|---|---|
| `nova_lp.release.mcelf.hs_fs` (~485 KB) | `0x080000` | R5FSS0_0 | Main FreeRTOS app — equipment / Modbus / OTA listener |
| `watchdog.release.mcelf.hs_fs` (~53 KB) | `0x180000` | R5FSS1_0 | NoRTOS watchdog (USART1) |

**OTA scope decision (locked in 2026-05-06): main image only.**
Watchdog stays JTAG-only via `Flash-LP.ps1` for the foreseeable
future. Rationale:

- Watchdog is small, slow-moving, and rebuilt rarely. Field updates
  to it are not on the roadmap.
- An OTA path that touches `0x180000` while the SBL is mid-load of
  R5FSS1_0 could brick the board in a way no rpi5-side recovery can
  fix (DAP-locked + no JTAG on customer site).
- All the proto / banking / staging machinery designed below
  (`FwBeginUpdate`, Bank A/B at `0x010000`/`0x200000`, Golden at
  `0x400000`) targets the **main** image only. The watchdog mcelf
  is treated as part of the immutable boot stack from the OTA
  protocol's perspective.

If a future situation forces watchdog OTA, the cleanest extension
is a `target_cluster` field on `FwBeginUpdate` (default = main =
0). Don't bake assumptions either way into the framing today —
keep the protocol oblivious so we don't have to renumber.

## What already exists (re-use, don't redesign)

| Asset | Location | Notes |
|---|---|---|
| Wire protocol | [proto/agristar/firmware.proto](../proto/agristar/firmware.proto) | `FwBeginUpdate`, `FwDataChunk`, `FwFinalizeUpdate`, `FwActivateBank`, `FwUpdateStatus`. State enum `FW_IDLE…FW_ERROR` |
| OSPI layout (designed) | Same proto header, lines 6-10 | `0x000000` header • `0x010000` Bank A • `0x200000` Bank B • `0x400000` Golden • `0x600000` Settings |
| Update manager | [Nova_Firmware/Platform/nova_fw_update.c](../Nova_Firmware/Platform/nova_fw_update.c) | Bank-header CRC-32, sequence-number-based active selection, chunk write, finalize, activate. Already complete. Sits on `hal_flash_*` API |
| Flash HAL | [Nova_Firmware/Platform/hal_flash.c](../Nova_Firmware/Platform/hal_flash.c) | Generic `hal_flash_read/write/erase_sector` |
| Auto-flasher reference | [Nova_Firmware/lp_am2434/flasher_uart/main.c](../Nova_Firmware/lp_am2434/flasher_uart/main.c) | Same `Flash_eraseBlk`/`Flash_write`/`Flash_read` calls the OTA path will reuse |

## What does NOT exist yet (scope of this work)

1. LP firmware doesn't link `nova_fw_update.c` or `hal_flash.c` —
   they're Nova-controller-only today.
2. No carrier on LP for proto `Fw*` messages — orbit boards speak
   Modbus TCP only on `:5502`.
3. **The big one:** the SBL on the LP-AM2434 doesn't know about A/B
   banks. Today's SBL hard-loads from `0x80000`. The whole
   `nova_fw_update` design assumes a custom stage-2 chooser that reads
   the bank header at `0x000000` and picks A vs B by `sequence`+`valid`,
   with a watchdog-strike fallback to the other bank or the Golden.
4. Existing LP boards are flashed at `0x80000` (single-image layout),
   not the `0x010000`/`0x200000` A/B layout in the proto. Migrating
   means one last JTAG re-flash per existing board.
5. No bridge-side push module. The bridge speaks proto-over-COBS to
   Nova but Modbus-TCP to orbits — needs a new orbit-OTA module.
6. No UI page (per-board version, per-board Update button, progress).

## Phased plan

### Phase 1 — transport on LP (unblocks Phase 2)

- Open a dedicated TCP port on each orbit board (proposal: `:5503`).
  Length-prefixed framing carrying the `Fw*` proto messages directly.
  Avoid stuffing 1 KB chunks through Modbus FC16 (32-register limit
  per write = ~64 bytes; would balloon round-trips and contend with
  control-plane reads).
- Link `nova_fw_update.c` + `hal_flash.c` into `nova_lp` builds.
  They're already designed to be portable — no LP-specific changes
  expected, just add to the Makefile sources list and call
  `NovaFwUpdate_Init()` after `LpDeviceConfig_Init`.
- Implement a small task `lp_ota_task` that owns the `:5503` socket,
  parses incoming proto frames, calls `NovaFwUpdate_Begin/WriteChunk/
  Finalize/Activate`, and emits `FwUpdateStatus` after each chunk.

> **DEPRECATED 2026-05-12 — runtime Flash_write doesn't work.** The
> code listed above (`nova_fw_update.c`, `hal_flash.c`, `lp_ota_task`)
> is all still in the firmware build, but `hal_flash_write` returns
> -1 in our runtime context (root cause: not software-debuggable;
> see [`memories/repo/lp-am2434-runtime-flashwrite-unresolved.md`](../memories/repo/lp-am2434-runtime-flashwrite-unresolved.md)).
> The OTA listener correctly receives chunks and dispatches to
> `NovaFwUpdate_WriteChunk`; the chunk write always fails cleanly
> (no chip mutation — brick-safe). For actual flashing, see the new
> bridge-side path under "Replacement: bridge-side flash" below.

### Phase 2 — staging without SBL changes (today's safest interim)

> **DEPRECATED 2026-05-12.** Same reason as Phase 1B — the runtime
> copy-write from staging-area to `0x080000` calls `Flash_write`
> which doesn't work in our FreeRTOS context. The original Phase 2
> design retained below for historical context. The
> **bridge-side replacement** (next section) achieves the same goal
> (atomic A/B-like swap with rollback) without runtime Flash_write.

~~While the SBL still hard-loads `0x80000`, we can't use Bank A/B
boot-time selection. Workaround:~~

- ~~Treat one of the A/B regions (say Bank B at `0x200000`) as a
  **staging** area. Write the new image there.~~
- ~~On `FwActivateBank`, run a critical section in app context that:
  1. Computes CRC of the staged image (already done by Finalize).
  2. Erases `0x80000` and copy-writes from `0x200000` → `0x80000` in
     a single uninterruptible loop. **(Watchdog image at `0x180000`
     is NOT touched — staging area lives entirely above it.)**
  3. Triggers warm reset via the **`CMD_REBOOT_SOC` primitive
     (verified working 2026-05-03)** — see
     [firmware-bridge-protocol.md §13](firmware-bridge-protocol.md).
     Same `Sciclient_pmDeviceReset(SystemP_WAIT_FOREVER)` call that
     `Flash-LP.ps1`'s auto-flasher uses; DMSC orchestrates a true
     warm reset, ROM re-runs, SBL loads OSPI 0x80000 (main) and
     0x180000 (watchdog, untouched).~~
- ~~**Staging-area size constraint:** main image is ~485 KB today;
  Bank B at `0x200000` extends to `0x400000` (Golden) = 2 MB headroom.
  Plenty for foreseeable growth. Watchdog partition at `0x180000` ends
  at `0x200000` (512 KB reserved) — never reused as staging.~~
- ~~Risk: power-fail during the copy = brick → JTAG recovery needed.
  Acceptable for development / on-site updates with backup power, NOT
  for unattended deployment. This is why Phase 3 is mandatory before
  shipping to customers.~~

### Replacement: bridge-side flash via the auto-flasher (active path post-pivot)

The auto-flasher (`flasher_uart/main.c` loaded into MSRAM via DSS
or UART) writes any OSPI offset successfully — verified across many
months of dev work, AND specifically at the OTA target offset
`0x900000` on 2026-05-12 (Experiment 1 in the runtime-Flash_write
investigation). The customer OTA flow becomes:

1. **Cloud → bridge.** Firmware image (`nova_lp.release.mcelf.hs_fs`)
   uploaded to the bridge (RPi5) via the existing Azure / agristar-bridge
   plumbing. Bridge verifies the signed mcelf using the same X.509 cert
   chain that ROM bootloader checks.
2. **Bridge → LP.** Bridge runs an evolution of
   [`qemu-constellation/flash-lp.sh`](../qemu-constellation/flash-lp.sh):
   stops `agristar-bridge` to free `/dev/ttyAMA0` (or, in the dev rig,
   uses DSS over USB), runs `uart_uniflash.py` with the prebuilt
   `sbl_uart_uniflash.release.hs_fs.tiimage` flash-writer + the new
   `nova_lp.release.mcelf.hs_fs`, then restarts `agristar-bridge`.
3. **LP boots into new image.** The auto-flasher's `Sciclient_pmDeviceReset`
   triggers a true SoC warm reset; ROM re-runs, SBL loads the freshly-written
   `0x080000`, new firmware boots.

What's already done:
- Auto-flasher binary builds and works (`flasher_uart/ti-arm-clang`).
- `uniflash_run.js` orchestrates DSS-based load (Windows dev rig).
- `uart_uniflash.py` orchestrates UART-based load (existing TI SDK tool).
- `flash-lp.sh` end-to-end on RPi5 for the dev/test path.

What's NOT done yet — the new "Phase 1B/2 replacement" workstream:

| Gap | Effort | Notes |
|---|---|---|
| Programmatic boot-mode toggle | Hardware + small bridge code | LP-AM2434 SW4 is physical DIP. For production we either: (a) wire boot-mode pins to bridge GPIO, (b) leave LP permanently in UART boot mode (works for fields with no recovery path; risky if OSPI image goes bad), (c) require human present (loses unattended OTA). Recommend (a) for production controller PCB rev. |
| Programmatic power-cycle | Hardware + bridge GPIO | Bridge GPIO drives LP reset / power-rail. Simple FET + GPIO. |
| Auth on bridge → LP transfer | Bridge code | UART boot is wide open — anyone with the cable can flash. Production needs the bridge to verify the mcelf signature before forwarding, AND ideally the LP's pinmux to disallow UART boot once a valid OSPI image is present. |
| Progress reporting | Bridge code | `uart_uniflash.py` outputs progress lines; pipe to UI via existing bridge↔UI WebSocket. |
| Rollback / dual-bank | Phase 3 (still relevant) | Custom SBL chooser still needed for A/B + watchdog-strike fallback. Independent of the runtime-Flash_write problem. |
| Status reporting from new firmware | Phase 1A (already done) | LP `:5503` continues to report `FwBankInfo` so the UI can confirm the new version booted. |

### Phase 3 — true A/B + custom stage-2 chooser (production gate)

- Build a small position-independent stage-2 stub at `0x80000`
  (~4-8 KB). Its only job: read `FwBankHeader` at `0x000000`, pick
  the bank with the highest `sequence` whose `valid==1` and
  `crc(image)==stored`, then jump to its entry point.
- Increment `boot_count` and `watchdog_strikes` in `FwBootMeta`. If
  `watchdog_strikes >= 3`, fall back to the other bank, then Golden.
- Application clears `watchdog_strikes` once it reaches a
  "healthy" milestone (e.g. PHY link-up + Modbus listening +
  10 s of clean ticks).
- This stub replaces the current `nova_lp.release.mcelf.hs_fs` at
  `0x80000`; the actual app images move to `0x010000` (Bank A) and
  `0x200000` (Bank B) per the proto layout.
- **Watchdog cluster (R5FSS1_0) interaction.** TI's stock
  `sbl_ospi_multi_partition` boots R5FSS1_0 from `0x180000`
  *unconditionally* before handing control to the R5FSS0_0 image at
  `0x080000`. Two implications for the custom stub:
  1. The custom stage-2 stub runs only on R5FSS0_0 (cluster 0). It
     never sees or selects the watchdog image — that's still pinned
     at `0x180000` by the SBL itself.
  2. **The custom stub is itself a fork of `sbl_ospi_multi_partition`**
     (or a thin wrapper that defers to TI's SBL for cluster 1 and
     replaces the cluster-0 load with the A/B chooser logic). Decide
     during Phase 3 design which variant is cheaper. Either way, the
     watchdog mcelf at `0x180000` is treated as part of the boot
     stack, not as user content.

### Phase 4 — Golden image + manufacturing flash

- Define a "golden" build: smallest possible image that can:
  - Bring up CPSW Ethernet at 100M FD with the PHY fixup.
  - Listen on `:5503` for OTA only (no Modbus, no equipment control).
  - Show a clear `[GOLDEN] mode` banner on UART.
- Provision the Golden image once at manufacturing into `0x400000`,
  never overwrite. If both A and B fail validation, the stage-2 stub
  jumps to Golden and the operator can push a fresh image from the
  rpi5 to recover.

### Phase 5 — bridge + UI

- New module on the rpi5 bridge (`constellation-ui/server/src/`) that:
  - Reads a local `.hs_fs` file, computes CRC-32, splits into chunks.
  - Opens TCP `:5503` to the chosen orbit IP, drives the
    `Begin → Chunk* → Finalize → Activate` state machine.
  - Surfaces `FwUpdateStatus` as a server-sent event / WebSocket frame.
- Level 2 system page (`constellation-ui/src/routes/level2/…`):
  - Lists each orbit (from the existing orbit topology / discovery).
  - Shows current vs available firmware version.
  - "Update" button per board with progress bar driven by
    `FwUpdateStatus.bytes_written / total_size`.
  - Confirm-before-activate dialog.

## Pitfalls already known (avoid re-discovering)

- **Don't put the OTA listener on `:5502`.** Modbus polling is
  high-frequency and any stall on the OTA path would degrade control.
- **Don't trust an SBL XMODEM "SUCCESS" after a prior failure** —
  documented in `/memories/repo/lp-am2434-ospi-uniflash.md` "SBL
  XMODEM partial-flash trap". This is exactly why the OTA design must
  do its OWN CRC verification of every chunk before calling it done,
  AND a full-image CRC at finalize time.
- **The PHY fixup must run before lwIP can TX** (cpsw-tx-debug
  Updates 12-14). Golden image MUST include `lwip_phy_fixup_task` or
  the recovery path is broken.
- **Watchdog feeding during long flash erase.** A full bank erase is
  ~496 sectors × 4 KB. If sector erase blocks for more than the
  watchdog timeout, the board will reset mid-update. Either bump
  watchdog timeout during `FW_STATE_ERASING`/`FW_STATE_RECEIVING`,
  or yield + feed between sectors.
- **OSPI XIP region is mapped at `0x60000000`.** App code that runs
  XIP from one bank cannot erase that same bank without first
  switching execution to MSRAM. Today's nova_lp loads to MSRAM at
  boot, so this is fine, but the stage-2 stub MUST be MSRAM-resident.

## Recommended start

Phase 1 alone produces a useful "push from rpi5 → orbit board" path
that works today (with the Phase 2 staging-copy hack on activate).
Phase 3 is the gate before any board ships to a customer site.

Per-board "bring to life" recipe (build, JTAG flash, system reset,
verify) lives in `/memories/repo/lp-am2434-bringup.md` "End-to-end
orbit board bring to life procedure".
