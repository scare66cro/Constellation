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

## Bench session-2 runbook (build + flash + verify)

### Pre-flight (do BEFORE flashing anything)

1. **Image one bench board's existing OSPI sector 0 to a backup `.bin`**
   so you can cross-flash the stock SBL back if `sbl_chooser` bricks:
   ```powershell
   # From CCS Debug Server Scripting (DSS):
   cd F:\Constellation\Nova_Firmware\lp_am2434\ospi_flash
   $env:LP_CCXML="F:\Constellation\Nova_Firmware\lp_am2434\AM2434_LP_A.ccxml"
   dss.bat .\ospi_read_dump.js     # writes ospi_dump_0x000000_0x60000.bin
   # Copy it somewhere safe (e.g. F:\Constellation\backups\sbl_stock_A.bin)
   ```
2. **Confirm only one XDS110 probe is enumerated.** Invariant #7 is
   load-bearing here:
   ```powershell
   & "C:\ti\ccs2050\ccs\ccs_base\common\uscif\xds110\xdsdfu.exe" -e
   # Must show exactly one device. Unplug the others.
   ```
3. **Pick the smoke-test board.** Probably Probe A (STORAGE, S24L0417)
   so a brick doesn't affect the controller. Confirm with the user.

### Step 1 — Wire SysConfig for the Flash driver

The TI stock `sbl_ospi` `example.syscfg` opens the OSPI driver via
`Drivers_open()` but does NOT open a `Flash` driver instance — the
stock SBL talks to OSPI directly via the `Bootloader_FlashArgs` path.

We need a Flash driver instance so `sbl_bank_select.c` can call
`Flash_read` / `Flash_eraseBlk` / `Flash_write`. Add it to
`example.syscfg`:

1. Open SysConfig GUI on `example.syscfg`.
2. **TI DRIVERS → Flash → ADD**.
3. Match the OSPI configuration of the running Nova app (W25Q128JV
   or W25Q64JV depending on the board — check `flash_log.txt` from
   the last `Flash-LP.ps1` run if unsure).
4. Save. The generated `ti_drivers_config.c` will now declare
   `gFlashHandle[CONFIG_FLASH0]`.

### Step 2 — Build

```powershell
cd F:\Constellation\Nova_Firmware\lp_am2434\sbl_chooser\r5fss0-0_nortos\ti-arm-clang
$env:MCU_PLUS_SDK_PATH="C:\ti\mcu_plus_sdk_am243x_12_00_00_26"
$env:SYSCFG_PATH="C:\ti\sysconfig_1.27.0"
$env:CG_TOOL_ROOT="C:\ti\ti-cgt-armllvm_4.0.4.LTS"
gmake -s PROFILE=release all
# → sbl_chooser.release.tiimage   (signed for HS_FS boot)
```

Build failures to expect on first try:
- `gFlashHandle[CONFIG_FLASH0]` undefined → SysConfig step 1 was skipped.
- Linker can't find sbl_bank_select.o → makefile FILES list is wrong.
- `Bootloader_FlashArgs` not found → missing `#include
  <drivers/bootloader/bootloader_flash.h>` (already in our main.c).

### Step 3 — Flash to one bench board

Use the existing UART-flash path (SW4 → UART boot mode) so the JTAG
path stays available for recovery:

```powershell
cd F:\Constellation\Nova_Firmware\lp_am2434
# Set DIP switches to UART boot mode (SW4: 1+2+3 ON)
# USB-cycle Probe A
& python uart_uniflash.py -p COM5 `
    --cfg .\sbl_chooser\flash_chooser_only.cfg     # see template below
# Set DIP switches back to OSPI boot mode (SW4: 2+6 ON)
# USB-cycle Probe A
```

The `flash_chooser_only.cfg` template needs to write ONLY the SBL
chooser at offset `0x000000`, leaving the app at `0x080000` and the
device-config at `0x600000` untouched. Don't have one yet — derive
from `Nova_Firmware/lp_am2434/ti-arm-clang/flash_sbl_ospi_commission.cfg`.

### Step 4 — Write seed metadata (one-shot via JTAG)

Fresh-flashed boards have `0xFF` at `0x300000` (the meta sector).
SblBankSelect_Choose detects this (boot_count=0 + no magic) and falls
through to LEGACY mode (boot 0x080000 unconditionally). That works
for the first boot. To make subsequent boots use the real selection
logic, write the seed metadata:

```javascript
// JTAG-write via DSS — see ospi_flash/seed_meta_block.js (to be written
// at the bench when needed). Sets:
//   FwBootMeta:        boot_count=1, boot_reason=0, watchdog_strikes=1
//   Bank A header:     magic="NOVA", sequence=1, valid=1, active=1,
//                      image_size+crc computed from the existing
//                      image at 0x080000.
```

This is a one-time JTAG operation per board. After the first
successful OTA-via-bridge, Bank B will be written normally and the
app's `NovaFwUpdate_ConfirmBoot` (already wired in Session 1) will
keep both banks' headers fresh.

### Step 5 — Reset and verify boot trace

Expected on UART0:

```
Starting OSPI Bootloader ...
[SBL] bank=A seq=1 off=0x080000 boots=2 strikes=1 reason=0
Image loading done, switching to application ...
[BB] LpDeviceConfig_Init ok
...
```

The `strikes=1` is normal — it clears within 30 s once the app
reaches "healthy" (5 alive bits held continuously via Session 1's
heartbeat hook). Next boot you'll see `strikes=1` again (the SBL
increment) → clears to 0 in OSPI after 30 s healthy → next boot
shows `strikes=1` once more (the increment).

If you see `bank=LEGACY off=0x080000 boots=0`: the SBL didn't find
metadata (you skipped step 4, or it was wiped). Boot still works
because LEGACY mode just unconditionally boots `0x080000`.

If you see `bank=GOLDEN off=0xC00000`: both Bank A and Bank B failed
validation. **This should never happen on a freshly-flashed board**
— it means the seed write in step 4 didn't take, or the image at
`0x080000` got corrupted, or you hit the strike-ceiling (≥3) on the
high-sequence bank. Check the boots= count to see how many SBL
runs the strikes accumulated over.

### Step 6 — Negative test (the actual F2c proof)

Manually corrupt Bank A's `image_crc` field:

```javascript
// JTAG-write 0xDEADBEEF to FW_HEADER_OFFSET + FW_BANK_A_HDR_OFFSET + 8
// (image_crc is the 3rd uint32 in FwBankHeader, offset 8 within the
// header at offset 0x80 of the 0x300000 sector)
```

Reset. Expected trace:

```
[SBL] bank=A seq=N off=0x080000 ...      ← still picks Bank A
                                          (valid bit still true,
                                          we only corrupted CRC; SBL
                                          doesn't CRC-check, just
                                          trusts the valid bit)
```

This is correct behaviour — the SBL is fast and trusts the app to
have validated the image. If we want CRC-checked SBL boot, that's
a Session-3 extension.

To trigger the actual rollback path, instead set Bank A's `valid`
field to `0`:

```javascript
// JTAG-write 0 to FW_HEADER_OFFSET + FW_BANK_A_HDR_OFFSET + 12
// (valid is the 4th uint32 in FwBankHeader)
```

Reset (requires Bank B to be written with a valid image first — chain
this test AFTER doing one full OTA cycle). Expected trace:

```
[SBL] bank=B seq=M off=0x900000 boots=N strikes=1 reason=0
```

`reason=0` because we're not in the strike-fallback path; we just
followed the priority order (Bank B was the highest-sequence VALID
bank).

To trigger strike-based fallback specifically, force the app to hang
before reaching 30 s healthy 3 times in a row. Easiest way: temporarily
patch the watchdog client to never set `s_ota_confirmed`. Reset
three times. On the 4th boot, SBL trace should show:

```
[SBL] bank=B seq=M-1 off=0x900000 boots=4 strikes=0 reason=2
```

(`strikes=0` because pick_bank reset them when falling back; `reason=2`
= FALLBACK.)

### Step 7 — Cross-check Bank B persistence

After Session 2 is proven on one board, the other 3 bench boards
get the same SBL flash in step 3. The seed metadata step (4) is
PER-BOARD because Bank A's image CRC differs.

## Recovery: if `sbl_chooser` bricks the board

JTAG-flash TI's stock `sbl_ospi.release.hs_fs.tiimage` back to
`0x000000`. The app at `0x080000` is untouched and boots normally
without bank metadata.

```powershell
# From the backup taken in Pre-flight step 1:
cd F:\Constellation\Nova_Firmware\lp_am2434\ospi_flash
$env:LP_CCXML="F:\Constellation\Nova_Firmware\lp_am2434\AM2434_LP_A.ccxml"
dss.bat .\uniflash_loadraw.js F:\Constellation\backups\sbl_stock_A.bin 0x000000
```

If that doesn't work, fall back to the UART recovery path:

```powershell
# SW4: 1+2+3 ON (UART boot), USB-cycle Probe A
python uart_uniflash.py -p COM5 --cfg .\ti-arm-clang\flash_nova_lp_win.cfg
# Full nova_lp.bin + stock SBL reflash — same as a wrong-probe recovery.
```

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
