# sbl_chooser — F2c custom Stage-2 SBL with A/B/Golden bank rollback

Forked from TI's `mcu_plus_sdk_am243x_12_00_00_26/examples/drivers/boot/sbl_ospi/am243x-lp/r5fss0-0_nortos`
on 2026-05-30 as the F2c Session 2 baseline. The single behavioural change
from the stock SBL is the bank-selection step in `main.c` between
`Bootloader_open()` and `Bootloader_parseAndLoadMultiCoreELF()`:

1. Read `FwBootMeta` + `FwBankHeader[A]` + `FwBankHeader[B]` from OSPI.
2. Bump `boot_count` and `watchdog_strikes`.
3. Pick the highest-sequence valid bank, OR fall back to the other bank
   if strikes ≥ 3, OR fall back to Golden if both banks are bad.
4. Write the updated `FwBootMeta` back to OSPI (atomically — single
   64 KB sector at `0x300000`).
5. Override `Bootloader_FlashArgs::appImageOffset` to point at the
   chosen bank's offset.
6. Let the rest of TI's SBL boilerplate load and run the image.

Algorithm and design rationale: `docs/lp-am2434-f2c-sbl-chooser-design.md`.

## File map

```
sbl_chooser/
├── README.md                       ← you are here
└── r5fss0-0_nortos/
    ├── main.c                      ← TI stock + F2c insert (lines ~145-180)
    ├── sbl_bank_select.h           ← F2c bank selection API
    ├── sbl_bank_select.c           ← NoRTOS port of NovaFwUpdate_BootValidation
    ├── example.syscfg              ← TI stock (UNMODIFIED for now — needs Flash
    │                                 driver added at the bench, see §4)
    └── ti-arm-clang/
        └── makefile                ← TI stock + `sbl_bank_select.c` added to FILES
```

## What's reusable from Platform/nova_fw_update.{c,h}

The Nova app's `NovaFwUpdate_BootValidation` (Platform/nova_fw_update.c
line 699) is the FreeRTOS-context reference for this algorithm. We
cannot link it directly because:

- It depends on `hal_flash.c` which uses FreeRTOS mutexes
- It includes Nova-app headers that pull in the entire equipment engine
- The SBL is bare-metal NoRTOS (no FreeRTOS in the SBL)

So `sbl_bank_select.c` is a hand-port that keeps the same algorithm but
uses the SDK's `Flash_read` / `Flash_eraseBlk` / `Flash_write` directly,
no HAL layer. The struct layouts (`SblFwBankHeader`, `SblFwBootMeta`)
are byte-for-byte mirrors of the Platform versions with a
`_Static_assert` on `sizeof()` to catch drift at compile time. **If the
Platform structs ever change, this header MUST be updated in lockstep.**

## OSPI map (must match Platform/nova_fw_update.h)

| Offset | Owner |
|---|---|
| `0x000000` | This SBL (replaces TI stock sbl_ospi when flashed) |
| `0x080000` | Bank A app image |
| `0x300000` | FwBootMeta + Bank A FwBankHeader (single 64 KB sector) |
| `0x310000` | Bank B FwBankHeader (separate sector for isolation) |
| `0x900000` | Bank B app image |
| `0xC00000` | Golden recovery image |

Defined as compile-time constants in `sbl_bank_select.h` (`SBL_FW_*`).
**Single source of truth is `Nova_Firmware/Platform/nova_fw_update.h`**
— if those macros change, mirror the change here.

## Session 2 runbook (JTAG-only — no bench items required)

The full Session 2 workflow runs over JTAG via XDS110. No DIP switches,
no UART boot mode, no USB cycling. The orchestration script
[`Flash-SblChooser.ps1`](Flash-SblChooser.ps1) handles backup, flash,
seed, and the negative test as one-button operations.

### Prerequisites (workstation, no bench)

1. **CCS 12.0+** with TI MCU+ SDK 12.00 at
   `C:\ti\mcu_plus_sdk_am243x_12_00_00_26`.
2. **`sysconfig_1.27.0`** at `C:\ti\sysconfig_1.27.0`.
3. **ti-arm-clang 4.0.4 LTS** at `C:\ti\ti-cgt-armllvm_4.0.4.LTS`.
4. **The target board is wired up + powered + reachable via XDS110.**
   Any one of the four bench probes (`A`, `N`, `P`, `T`). Recommend `A`
   (STORAGE, S24L0417) so a worst-case brick doesn't affect the
   controller.

### Step 1 — Build (no SysConfig GUI step needed)

**Correction from an earlier draft of this README:** TI's stock
`sbl_ospi` `example.syscfg` already has a Flash driver instance
declared (`CONFIG_FLASH0` — used by the bootloader's image-loading
path). `Board_driversOpen()` populates `gFlashHandle[CONFIG_FLASH0]`
before our bank-select call runs, so no GUI step is required.

Build with the SDK toolchain (verified working 2026-05-30, fw-side
state at 0.A.208):

```powershell
cd F:\Constellation\Nova_Firmware\lp_am2434\sbl_chooser\r5fss0-0_nortos\ti-arm-clang
$env:MCU_PLUS_SDK_PATH = "C:\ti\mcu_plus_sdk_am243x_12_00_00_26"
$env:SYSCFG_PATH       = "C:\ti\sysconfig_1.27.0"
$env:CG_TOOL_ROOT      = "C:\ti\ti-cgt-armllvm_4.0.4.LTS"
& "C:\ti\ccs2050\ccs\utils\bin\gmake.exe" -s PROFILE=release all
# → sbl_chooser.release.hs_fs.tiimage   (HS-FS signed, ~305 KB)
```

Build size budget: TI's stock `sbl_ospi.release.hs_fs.tiimage` is
~311 KB. Our chooser adds the bank-selection logic but the signed
output ends up at the same ~305-312 KB envelope (the security cert +
RPRC overhead dominates the binary). Well within the 384 KB SBL
region at OSPI `0x000000 – 0x05FFFF`.

Headers + structure caveats discovered while bringing up the build:

- `Flash_read` / `Flash_eraseBlk` / `Flash_write` live in
  `<board/flash.h>`, not `<drivers/flash.h>`.
- `FwBankHeader` is **136 bytes**, not 128 (the original Platform/
  comment claimed "Pad to 128" but the math 6×4 + 32 + 80 = 136
  always added up to 136). The SBL port's `_Static_assert` caught
  this; both files now have correct sizes annotated.
- The Platform `nova_fw_update.h` comment was fixed in the same
  commit as the SBL port's `_Static_assert(... == 136)`.

### Step 3 — JTAG-flash the SBL chooser, seed metadata, and verify (one command)

```powershell
cd F:\Constellation\Nova_Firmware\lp_am2434\sbl_chooser
.\Flash-SblChooser.ps1 -Probe A
```

This runs four operations in order:

| # | Op | Detail |
|---|---|---|
| 1 | Probe check | xdsdfu verifies exactly one XDS110 (invariant #7); aborts if multiple are attached or the target serial is missing |
| 2 | Backup stock SBL | dumps OSPI `0x000000 – 0x05FFFF` (384 KB) via memory-mapped XIP read into `F:\Constellation\backups\sbl_stock_A_YYYYMMDD-HHMMSS.bin`. Used for recovery if the chooser bricks the board. |
| 3 | Flash chooser | writes `sbl_chooser.release.tiimage` to OSPI `0x000000` via the existing `flasher_uart` + `uniflash_run.js` path |
| 4 | Seed metadata | builds a 256-byte FwBootMeta + Bank A FwBankHeader block (magic=NOVA, sequence=1, valid=1, active=1) and writes it to OSPI `0x300000` |

The script prints the expected boot trace and the UART monitoring
command at the end. After it finishes, power-cycle the LP (USB unplug +
replug) so it boots from OSPI, and watch UART0 on the matching COM port.

Each operation is independently skippable via switches (e.g.
`-SkipBackup` if you've already imaged the stock SBL on this board).

### Step 4 — Verify boot trace

Capture UART0 output:

```powershell
cd F:\Constellation\Nova_Firmware\lp_am2434
.\Capture-Com.ps1 -Port COM5    # COM port for Probe A (STORAGE)
```

Expected on cold boot from OSPI:

```
Starting OSPI Bootloader ...
[SBL] bank=A seq=1 off=0x080000 boots=2 strikes=2 reason=0
Image loading done, switching to application ...
[BB] LpDeviceConfig_Init ok
[BB] NovaFwUpdate_Init ok
...
```

`strikes=2` is normal — Session 1's heartbeat hook clears the OSPI
counter to 0 after 30 s of all-alive, but the SBL bumps it back to 1
on the *next* boot's increment. Steady state is `strikes=1` until
something fails. After 30 s healthy you can verify the clear by
booting again and observing `strikes=1` instead of `strikes=2`.

#### Failure modes to recognize on the boot trace

| Trace shows | Means | Action |
|---|---|---|
| `bank=LEGACY off=0x080000 boots=0` | Chooser found no metadata (seed step skipped or wiped) | Boot still works (LEGACY = unconditional 0x080000). Re-run `Flash-SblChooser.ps1 -Probe X -SkipBackup -SkipSblFlash` to seed. |
| `bank=GOLDEN off=0xC00000` | Both banks rejected | Either negative test triggered, or Bank A's seed didn't take. No Golden image is flashed today → SBL spins in FATAL. Restore via `-SkipBackup -SkipSblFlash` to re-seed Bank A. |
| `bank=A seq=1 off=0x080000 strikes=4 reason=2` | Strike fallback path triggered | App isn't reaching healthy in 30 s × 3 boots. Check Session 1's heartbeat hook in `lp_watchdog_client.c`. |
| No `[SBL]` line at all | Chooser didn't run | Either the SBL flash failed silently, or you're reading from a stale terminal. Re-power-cycle. |

### Step 5 — Negative test (rollback proof)

To exercise the SBL fallback path, re-write the seed with `valid=0`:

```powershell
.\Flash-SblChooser.ps1 -Probe A -SkipBackup -SkipSblFlash -InvalidateBankA
```

Power-cycle and watch the boot trace. With no Bank B image yet (a
fresh F2c install hasn't run OTA), SBL will report
`bank=GOLDEN off=0xC00000` and spin in FATAL because no Golden image
exists at `0xC00000`. **That's the expected failure mode without a
Golden image flashed** — it proves the chooser correctly REJECTED
the invalid Bank A.

To recover: re-run the seed with valid=1:

```powershell
.\Flash-SblChooser.ps1 -Probe A -SkipBackup -SkipSblFlash
```

For a more interesting fallback test, do this AFTER a full OTA cycle
that has written Bank B (sequence=2, valid=1) — then invalidate Bank A
and confirm the SBL falls to Bank B:

```
[SBL] bank=B seq=2 off=0x900000 boots=N strikes=1 reason=0
```

`reason=0` because we're not in the strike-fallback path; we followed
the priority order (Bank B was the highest-sequence VALID bank).

### Step 6 — Cross-flash to the other bench boards

Once Session 2 is verified on Probe A, the other three boards get the
same treatment:

```powershell
.\Flash-SblChooser.ps1 -Probe N    # CONTROLLER
.\Flash-SblChooser.ps1 -Probe P    # GDC
.\Flash-SblChooser.ps1 -Probe T    # TRITON
```

Each backup ends up in `F:\Constellation\backups\sbl_stock_<probe>_<timestamp>.bin`
so you have per-board recovery archives.

## Recovery: if `sbl_chooser` bricks a board

The backup from step 3.2 is your insurance:

```powershell
cd F:\Constellation\Nova_Firmware\lp_am2434\sbl_chooser
.\Flash-SblChooser.ps1 -Probe A `
    -SkipBackup `
    -SblImagePath "F:\Constellation\backups\sbl_stock_A_20260530-141523.bin" `
    -SkipSeed
# Replaces the chooser at 0x000000 with the saved stock SBL.
# The app at 0x080000 stays put and boots normally without metadata.
```

If `Flash-SblChooser.ps1` itself can't reach the board (e.g. you flashed
something that breaks JTAG attach), the last-resort is the UART boot
fallback — but **that requires bench access** (DIP switch SW4:
1+2+3 ON, USB-cycle, then `uart_uniflash.py -p COM5 --cfg .\ti-arm-clang\flash_nova_lp_win.cfg`).
That path full-reflashes both the SBL and the app from scratch and is
equivalent to a wrong-probe recovery.

In practice: as long as JTAG attach still works, the JTAG-only recovery
above is sufficient. The TI ROM is in chip mask and can always be
forced into a known-good state by JTAG-resetting before the bad SBL
runs — the boot sequence is `ROM → (load SBL) → (jump to SBL)` and
JTAG halt at the ROM stage is always available.

## Pre-Session-2 (workstation only, no bench)

Before running `Flash-SblChooser.ps1`:

1. Make sure the SDK paths in `Flash-SblChooser.ps1` and the makefile
   resolve to your installs.
2. Do the SysConfig GUI step (§Step 1 above) and check the generated
   `ti_drivers_config.c` declares `gFlashHandle[CONFIG_FLASH0]`.
3. Build (§Step 2 above) and confirm
   `sbl_chooser.release.tiimage` exists.
4. Make sure only one XDS110 is plugged in (the script checks, but
   pre-flight saves a wasted DSS startup).

## What's NOT in this session

- **Bank header seed automation** — the JTAG seed-write (§4) is
  currently a hand-derived JS script. Once it works, productionize as
  `ospi_flash/seed_meta_block.js` with template values.
- **Image signature verification in the SBL.** Today the SBL trusts
  the `valid` bit. If we want defense-in-depth, the SBL could re-verify
  the signature itself. That's a Session-3 extension and would
  significantly increase SBL boot time (currently sub-second).
- **Golden image flashing.** Today no Golden exists at `0xC00000`.
  If both banks fail and pick_bank returns SBL_BANK_GOLDEN, the
  Bootloader_open will fail (nothing at that offset) and the board
  spins in SBL FATAL. Session 4 — flash a known-good Golden image
  during manufacturing.
- **Bootloader_FlashArgs offset override safety.** We're patching
  a TI-internal struct field. If the SDK ever changes the struct
  layout, this breaks silently at compile time (no struct member →
  compile error) or worse, silently at runtime (struct reorder).
  Mitigation: the `_Static_assert` on sizeof in sbl_bank_select.c
  catches our struct drift, but TI's struct is opaque. If SDK 13+
  reshuffles `Bootloader_Config.args`, the override needs revisiting.

## When you finish Session 2

Run through `docs/lp-am2434-f2c-sbl-chooser-design.md` §8.3 — that's
Session 3 (productionize across the fleet). Once the chooser is on
all 4 boards and Bank B header writes via OTA are persisting cleanly,
the foundation work for OTA-as-shippable-feature is done.
