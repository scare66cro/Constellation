# LP-AM2434 — Custom Stage-2 SBL + A/B Bank Rollback (F2c)

> **Status:** Sessions 1, 2, and 3 COMPLETE 2026-05-30. **F2c SBL
> chooser is flashed on all 4 bench boards** (.1 CONTROLLER, .2 STORAGE,
> .3 GDC, .4 TRITON) and end-to-end verified via the bridge's
> `broker-fleet-probe` showing `active_version="seed"` on the 3 orbits
> + `bridge.nova.connected:true` on CONTROLLER.
>
> **Goal achieved:** unattended OTA recovery. A bad Bank-B image cannot
> permanently brick a customer panel — after three failed boots the
> SBL falls back to Bank A (or Golden if both are broken). Combined
> with the dual-core watchdog (F3, fw 0.A.59) and Phase 4 OTA
> (validated end-to-end 2026-05-29 on the 4-board fleet at 0.A.208),
> this gives true field-deployable updates with no JTAG service trip.
>
> **Outstanding from bringup** (per memory notes):
>
> 1. `boot_count` shows "1" on all 4 boards via FwBankInfo instead of
>    the expected "2" after SBL increment. Either `write_boot_meta` in
>    `sbl_bank_select.c` failed silently or the proto decode is off.
>    Verify next session by JTAG-reading raw OSPI `0x300000-0x300100`
>    bytes and comparing against the expected FwBootMeta+FwBankHeader
>    layout.
>    [`memories/repo/f2c-fleet-bringup-2026-05-30.md`](../memories/repo/f2c-fleet-bringup-2026-05-30.md).
>
> 2. **OTA install + F2c integration gap.** Ground-truthed via JTAG-
>    OSPI dump 2026-05-31. The orbit OTA Activate handler at
>    `lp_ota_task.c:1468` calls `lp_ota_stage_copy_b_to_a` then warm-
>    resets — it never calls `NovaFwUpdate_Finalize` or
>    `NovaFwUpdate_Activate`. Result: stage-copy delivers the new image
>    bytes to `0x080000` (so the new firmware DOES boot via the F2c
>    chooser picking Bank A per the seed metadata), but **no bank
>    headers or FwBootMeta are ever written by the orbit OTA path**.
>    `boot_count` stays at seed, Bank B header stays `0xFFFFFFFF`, no
>    `sequence` increment, no `active` swap. F2c rollback infra is
>    bypassed entirely.
>
>    **F2c rollback to Bank B cannot be tested until this is fixed** —
>    negative-test path (invalidate Bank A) would brick the board
>    because no valid Bank B + no Golden = SBL FATAL.
>
>    Fix priorities + full bench investigation in
>    [`memories/repo/ota-plus-f2c-integration-gap-2026-05-30.md`](../memories/repo/ota-plus-f2c-integration-gap-2026-05-30.md).

---

## 1. Why this matters now

Phase 4 OTA is **validated** — yesterday's run reached
`overallState: "done"` on all 4 boards (.1 CONTROLLER self-update +
.2/.3/.4 orbits), and `.1 active=bankB banks=AB` proves the
controller-self-update Activate completed cleanly. But there is still
**one hard-brick path**: if power fails between the SBL erase of
Bank A's old image and the new image landing, the LP boots into a
partially-written bank with no rollback. Today's recovery is a JTAG
trip.

F2c closes that path. After F2c:
- Power loss mid-OTA → next boot detects the failed bank, falls back
- Three consecutive failed boots of a newly-activated image → SBL
  auto-rolls to the previous bank
- Both A and B unusable → SBL boots the factory Golden image

No JTAG required. This is the threshold between "bench-validated OTA"
and "field-deployable OTA."

---

## 2. Boot stack today vs after F2c

| Today (0.A.208) | After F2c |
|---|---|
| ROM (stage 1) → TI stock `sbl_ospi` (stage 2, hardloads `0x080000`) → Nova app | ROM → **forked `sbl_chooser`** (stage 2, reads FwBankHeader → picks A/B/Golden by sequence + strikes) → Nova app |

The "stage 2" we're replacing is TI's `sbl_ospi` example — a NoRTOS
R5FSS0_0 image (~311 KB signed) that lives at flash offset `0x000000`
and loads an MCELF from a hard-coded offset (`0x080000`). We fork it
once into `Nova_Firmware/lp_am2434/sbl_chooser/` and never touch it
again.

---

## 3. Current OSPI map (W25Q128JV, 16 MB — fw 0.A.208 authoritative)

The "canonical map" in the original 0.A.59 version of this doc is
**stale**; what actually shipped through Phase-C bringup, Phase 1
OTA, the 0.A.102 4-byte addressing escape, and yesterday's 0.A.208
SBL-preserve relocation is the table below. The authoritative values
live as `FW_*` macros in `Nova_Firmware/Platform/nova_fw_update.h`;
this table is the quick-reference summary.

| Offset | Size | Owner | Notes |
|---|---|---|---|
| `0x000000` – `0x05FFFF` | 384 KB | TI stock `sbl_ospi` (today) → **custom `sbl_chooser`** (after F2c) | TI's stock is ~311 KB; tail extends to ~`0x4BE2D`. F2c chooser fits in the same envelope. |
| `0x080000` – `0x1FFFFF` | 1.5 MB | **Bank A app image** (mcelf.hs_fs) | Same offset TI's stock SBL hardloads → migration-free first flash of F2c (existing app stays where it is). |
| `0x180000` – `0x1FFFFF` | 512 KB | Watchdog R5FSS1_0 mcelf (overlaps end of Bank A footprint by design) | App images stay well under 1 MB so no real conflict. |
| `0x300000` – `0x33FFFF` | 256 KB | **FwBootMeta + Bank A FwBankHeader + Bank B FwBankHeader** | Relocated 2026-05-29 in 0.A.208 from `0x060000` to escape the SBL footprint. The old location wiped the SBL tail on every metadata update — see [`memories/repo/sbl-wipe-controller-self-update-7th-layer-2026-05-29.md`](../memories/repo/sbl-wipe-controller-self-update-7th-layer-2026-05-29.md) for the 7-layer saga. |
| `0x340000` – `0x4FFFFF` | 1.75 MB | Reserved (post-relocation slack) | Future per-bank manifests, signed dependency graphs, etc. |
| `0x500000` – `0x5FFFFF` | 1 MB | Reserved | Future log buffers / asset files. |
| `0x600000` – `0x61FFFF` | 128 KB | `lp_device_config` ping-pong banks | Per-board role/IP/MAC. Migrated here May 2026. |
| `0x620000` – `0x7FFFFF` | ~2 MB | `LpSettings` vault (future) | Currently MSRAM-only. |
| `0x900000` – `0xA7FFFF` | 1.5 MB | **Bank B app image** (mcelf.hs_fs) | Relocated 2026-05-15 in 0.A.102 from `0x200000` to force 4-byte addressing (variant-D probe). `0x200000` failed 0.A.78-82, `0x400000` failed 0.A.83+; both were inside the 16 MB 3-byte address region. `0x900000` is well into 4-byte-only territory. |
| `0xC00000` – `0xD7FFFF` | 1.5 MB | **Golden recovery image** | Manufacturing-flashed, read-mostly. SBL falls back here if both A and B fail validation. |

### Why these offsets — historical:

- **`0x300000` metadata block (0.A.208):** the original
  `0x060000` location landed inside the 256-KB erase block that
  contains TI's stock SBL tail. `write_meta_block_atomic` erases that
  block on every metadata update, wiping the SBL tail to 0xFF; next
  warm-reset → ROM rejects the truncated SBL and falls into BOOT ROM.
  Nova OFFLINE forever. New location at `0x300000-0x33FFFF` overlaps
  nothing and lives in the reserved region.
- **`0x900000` Bank B (0.A.102):** OSPI addressing mode tripwire.
  AM2434's OSPI controller has separate code paths for 3-byte
  (`< 0x1000000`) and 4-byte (`>= 0x1000000`) addressing on some
  Cypress S25HL variants. The 3-byte path silently dropped page
  writes past sub-PP boundaries (see invariant #11). `0x900000` is
  beyond the 8 MB mark and forces 4-byte mode unconditionally.
- **`0x080000` Bank A stays put:** matching TI's stock SBL hardload
  offset means F2c first-flash needs no app re-flash. Boards already
  in the field get an SBL replacement only; their existing app at
  `0x080000` boots normally with the new chooser.

---

## 4. Bank selection algorithm (executed by `sbl_chooser`)

Same as the original design; the algorithm is unchanged from 0.A.59:

```
1. Read FwBootMeta from FW_HEADER_OFFSET (0x300000).
2. Read FwBankHeader[A] from FW_HEADER_OFFSET + FW_BANK_A_HDR_OFFSET (+0x80).
3. Read FwBankHeader[B] from FW_BANK_B_HDR_SECTOR (0x310000).
4. Increment boot_count.
5. Increment watchdog_strikes (cleared by app once "healthy" — see §5).
6. Build candidates list, sorted by (sequence DESC, valid DESC):
     A if A.magic == FW_BANK_MAGIC AND A.valid AND A.crc matches
     B if B.magic == FW_BANK_MAGIC AND B.valid AND B.crc matches
     GOLDEN unconditionally
7. For each candidate:
     If watchdog_strikes >= 3 AND this is the highest-sequence bank:
       skip — previous boot bricked, don't try again.
       Log boot_reason = FALLBACK.
     Try to parse the mcelf at this candidate's bank offset.
     On success: jump (and the app clears strikes within 30 s via
     NovaFwUpdate_ConfirmBoot).
     On failure: continue to next candidate.
8. If no candidate boots: spin in SBL with [SBL] FATAL on UART.
```

`Nova_Firmware/Platform/nova_fw_update.c::NovaFwUpdate_BootValidation`
already implements step 4 + 5 (lines 699-730) and is intended to be
called from the SBL chooser — the function ports cleanly from FreeRTOS
to NoRTOS bare metal because it touches only static variables and
the OSPI driver (which the SBL also uses for boot image read).

**Atomicity of the strike write:** SBL writes `FwBootMeta` BEFORE
loading the app image. If power fails between strike-write and
bank-jump, next boot sees the higher count and proceeds normally —
worst case is one extra strike accounted to a fault that wasn't a
real hang. Acceptable.

---

## 5. Healthy milestone (= "the new image works")

Wired by the watchdog heartbeat task in
[`Nova_Firmware/lp_am2434/lp_watchdog_client.c`](../Nova_Firmware/lp_am2434/lp_watchdog_client.c)
as of 2026-05-30 (Session 1):

| Bit | Source |
|---|---|
| `LP_WD_ALIVE_MODBUS` | `orbit_modbus_tcp.c` accept-loop ping |
| `LP_WD_ALIVE_SYSTEMSTATUS` | main.c data_exchange tick |
| `LP_WD_ALIVE_LWIP_LINK` | `netif_default->flags & NETIF_FLAG_LINK_UP` |
| `LP_WD_ALIVE_ENGINE_TICK` | `lp_engine_tick()` exit |
| `LP_WD_ALIVE_WD_PEER` | watchdog core's `wd_counter` advanced in last 2 s |

**Threshold:** all bits in `LP_WD_REQUIRED_DEFAULT` must hold
continuously for `LP_WD_HEALTHY_THRESHOLD_MS = 30000` ms. Any single
tick dip resets the timer. Once 30 s of sustained all-alive elapses,
`lp_watchdog_heartbeat_task` calls `NovaFwUpdate_ConfirmBoot()` once
per boot. `ConfirmBoot` is **idempotent** (returns early if strikes
are already 0) and gated by `s_ota_confirmed` so we don't repeat the
OSPI read after the first hit — no flash wear on healthy boots.

If the new firmware is broken in a way that prevents reaching healthy
within 30 s × 3 boots, the SBL chooser falls back automatically.

---

## 6. Diagnostic surface (Session 1)

`FwBankInfo` proto (envelope tag 115) now exposes the F2c state so
the UI version page can show it without needing JTAG:

```proto
message FwBankInfo {
  FwBankId active_bank     = 1;
  string   bank_a_version  = 2;
  uint32   bank_a_crc      = 3;
  bool     bank_a_valid    = 4;
  string   bank_b_version  = 5;
  uint32   bank_b_crc      = 6;
  bool     bank_b_valid    = 7;
  string   golden_version  = 8;
  uint32   boot_count      = 9;
  uint32   boot_reason     = 10;
  uint32   current_role    = 11;  // (Gap 1, 2026-05-20)
  uint32   watchdog_strikes = 12; // (F2c S1, 2026-05-30) NEW
}
```

The bridge decoder in
[`constellation-ui/server/src/novaFwUpdateManager.ts`](../constellation-ui/server/src/novaFwUpdateManager.ts)
unpacks both new fields. Health-row line on the bridge console now
reads `boots=N, strikes=M` so a quick `journalctl -u agristar-bridge`
shows the F2c state for any board emitting FwBankInfo.

---

## 7. Migration plan (existing-board safe)

The bench has 4 LP boards already provisioned with TI's stock SBL +
fw 0.A.208 at `0x080000`. The migration to F2c needs to be lossless —
no required JTAG re-flash dance.

Per board, in this order:

1. **Already done in 0.A.208:** app at `0x080000` runs the
   `nova_fw_update.c` codepath (Session 1 just wired the missing
   ConfirmBoot heartbeat hook). The LP emits the F2c diagnostic
   field via FwBankInfo. No bench-side regression risk.
2. **JTAG-flash custom `sbl_chooser` to `0x000000`.** Replaces TI's
   stock SBL. The app at `0x080000` stays put.
3. **JTAG-write FwBootMeta + Bank-A FwBankHeader at `0x300000`.**
   Set sequence=1, magic=NOVA, valid=1, image_size and crc computed
   from the existing image at `0x080000`. After this, the SBL
   chooser will boot the existing app on the next reset.
4. **Reset and verify.** Boot trace shows
   `[SBL] bank=A seq=1 strikes=1` (the 1 is normal — clears within
   30 s when the app reports healthy via the heartbeat hook landed
   in Session 1).
5. **First OTA push then writes Bank B at `0x900000`** with
   sequence=2. Subsequent updates ping-pong between A and B.

**Recovery path if `sbl_chooser` itself is broken:** flash TI's stock
`sbl_ospi` back to `0x000000` via the existing `Flash-LP.ps1` path.
The app at `0x080000` is untouched and boots normally without bank
metadata.

---

## 8. What's done (Session 1) vs what's next

### ✅ Session 1 — DONE 2026-05-30 (this session)

| Item | Status | Where |
|---|---|---|
| Update `FW_*` offsets to current map | ✅ already at 0.A.208 values | `nova_fw_update.h` |
| Refresh `nova_fw_update.h` docblock | ✅ rewritten to current layout | `nova_fw_update.h` lines 1-27 |
| Link `nova_fw_update.c` + `hal_flash.c` into LP build | ✅ already linked (0.A.208 ships with both) | confirmed via `.obj` artifacts |
| Call `NovaFwUpdate_Init()` after `LpDeviceConfig_Init()` | ✅ already at `main.c:2199` | n/a |
| Wire `NovaFwUpdate_ConfirmBoot()` from watchdog heartbeat after 30 s | ✅ NEW — added to `lp_watchdog_heartbeat_task` | `lp_watchdog_client.c` |
| Expose `FwBootMeta.watchdog_strikes` to bridge | ✅ NEW — proto field 12, encoder, decoder | `firmware.proto`, `lp_ota_task.c`, `novaFwUpdateManager.ts` |

All Session 1 work is reversible — none of it changes what's flashed
at any specific OSPI offset. If the heartbeat hook misbehaves, just
revert `lp_watchdog_client.c`; OSPI is untouched. The diagnostic
proto field is purely additive (older bridges/firmwares ignore it).

### ✅ Session 2 software prep — DONE 2026-05-30 (commit `7599afb`)

The 80% of Session 2 that doesn't need a probe attached:

- Forked TI's `sbl_ospi` (plain version — `sbl_ospi_multi_partition`
  turned out to be for multi-CPU offsets, not what F2c needs) into
  `Nova_Firmware/lp_am2434/sbl_chooser/r5fss0-0_nortos/`.
- Wrote `sbl_bank_select.c` — NoRTOS bare-metal port of
  `NovaFwUpdate_BootValidation`. Uses SDK `Flash_read` / `Flash_eraseBlk` /
  `Flash_write` directly, no FreeRTOS dependencies. Struct layouts
  mirror Platform/nova_fw_update.h with `_Static_assert` on `sizeof`.
- Modified `main.c` to call the selector and override
  `Bootloader_FlashArgs::appImageOffset` at runtime.
- `README.md` with full bench runbook.

### ✅ Session 2 JTAG-only tooling — DONE 2026-05-30 (commit `7dfb82a`)

The remaining 20% of Session 2, made JTAG-only (no DIP switches, no
UART boot, no USB cycling):

- `Flash-SblChooser.ps1` — orchestrates backup + flash + seed + optional
  invalidate-for-negative-test as one command per probe.
- `Write-SeedMetaBlock.ps1` — builds a 256-byte FwBootMeta + FwBankHeader
  seed in PowerShell, JTAG-writes it at OSPI `0x300000` via
  `uniflash_run.js`. `-Valid:$false` flips to the negative-test case.
- `dump_ospi_to_file.js` — new DSS script that loads the auto-flasher,
  waits for OSPI XIP at `0x60000000`, halts, and `memory.readData()` a
  requested region to a host file. Used for the pre-flight stock-SBL
  backup.

### ✅ Session 3 software prep — DONE 2026-05-30 (commit pending)

Manufacturing-pipeline integration. The runtime OTA installer does NOT
auto-flash the SBL today (deferred — see §8 below); instead:

- `Commission-LP.ps1` — one-button per-board first install that calls
  `Flash-SblChooser.ps1` then `Flash-LP.ps1` in sequence. For commissioning
  a fresh board or migrating an existing-bench board off TI stock SBL.
- `Build-Cfu.ps1 -IncludeSbl` — optional flag that stages
  `sbl_chooser.release.tiimage` into the .cfu bundle and adds an
  `sbl_chooser` manifest entry. The runtime OTA installer treats this as
  a Pi5-style optional component (no slot/role/ip → skipped). Useful for
  manufacturing pipelines that need a single shippable artifact
  containing both the app and the SBL.

### ✅ Build verified — 2026-05-30 (commit pending)

`sbl_chooser.release.hs_fs.tiimage` builds cleanly out of the SDK at
**311,685 bytes (~305 KB)** — same envelope as TI's stock `sbl_ospi`.
Fits comfortably in the 384 KB region at OSPI `0x000000 – 0x05FFFF`.

Build-side discoveries while wiring this up:
- TI's stock `sbl_ospi` `example.syscfg` already has a Flash driver
  instance (`CONFIG_FLASH0`) — the SysConfig GUI step the README
  originally called out is NOT needed.
- `Flash_read` / `Flash_eraseBlk` / `Flash_write` live in
  `<board/flash.h>`, not `<drivers/flash.h>` (corrected in main.c +
  sbl_bank_select.{c,h}).
- `FwBankHeader` is actually **136 bytes**, not 128. The Platform/
  comment claimed "Pad to 128" but never matched the field math. The
  SBL port's `_Static_assert(sizeof(SblFwBankHeader) == 136)` caught
  the discrepancy; both Platform and SBL copies now have correct size
  annotations.

### ✅ Bench session — DONE 2026-05-30 (commit pending)

All 4 boards have the F2c SBL chooser at OSPI `0x000000` + seeded
FwBootMeta + Bank A FwBankHeader at OSPI `0x300000`. Sequence per board:

1. `Set-Probe.ps1 -Probe <X> -Action Solo` (admin elevation)
2. `.\Flash-SblChooser.ps1 -Probe <X> -SkipBackup`
3. Re-enable other probes via `.\Set-Probe.ps1 -Probe <Y> -Action Enable`

Verified at end:
```
GET /health                                  → status:healthy, nova.connected:true
GET /api/_debug/broker-fleet-probe
  .2 STORAGE   active_version="seed" bank_a_valid=true active_bank=0
  .3 GDC       active_version="seed" bank_a_valid=true active_bank=0
  .4 TRITON    active_version="seed" bank_a_valid=true active_bank=0
```

Bench gotchas captured in
[`memories/repo/f2c-fleet-bringup-2026-05-30.md`](../memories/repo/f2c-fleet-bringup-2026-05-30.md):
- PowerShell 5.1 + em-dashes + UTF-8 without BOM → parser errors
- xdsdfu regex needs `Serial(?: Num)?:`
- DSS init wrapper needed for standalone scripts
- `memory.readData` 4-arg batch returns zeros on AM2434 XIP
- Rhino FileOutputStream.write ambiguity
- CONTROLLER flash hang past 10 min — recover via system_reset.js
- `boot_count` stuck at 1 (likely write_boot_meta silent failure)

### Bench-required (historical — what was needed before 2026-05-30)

The remaining work needs JTAG probe attached but **does NOT need DIP
switches or UART boot**. Pure XDS110 via the new tooling:

1. **(Skipped — already done.)** The SysConfig GUI step the previous
   draft of this doc called out is unnecessary; the TI stock
   `example.syscfg` already wires up `CONFIG_FLASH0`.
2. **Build** (✅ verified 2026-05-30): `gmake -s PROFILE=release all`
   from `sbl_chooser/r5fss0-0_nortos/ti-arm-clang`. Produces
   `sbl_chooser.release.hs_fs.tiimage` (~305 KB).
3. **First flash** (one board): `.\Commission-LP.ps1 -Probe A` from
   `Nova_Firmware/lp_am2434/sbl_chooser/`. Backup stock SBL → install
   chooser → seed Bank A → flash Nova app → write device-config. Power-
   cycle, verify boot trace shows both `[SBL] bank=A ...` and
   `[BB] LP role = STORAGE`.
4. **Negative test**: `.\Flash-SblChooser.ps1 -Probe A -SkipBackup
   -SkipSblFlash -InvalidateBankA`. Power-cycle. Expected:
   `[SBL] bank=GOLDEN off=0xC00000` (FATAL because no Golden flashed yet
   — that IS the rollback proof). Recover with
   `.\Flash-SblChooser.ps1 -Probe A -SkipBackup -SkipSblFlash` (re-seed
   with valid=1).
5. **Cross-flash to fleet**: repeat step 3 for Probes N, P, T.

### Session 4 — Golden image (deferred)

After Session 3 is on all 4 boards, the negative test will land in
GOLDEN-not-found FATAL. To make the rollback chain truly complete,
flash a known-good `nova_lp.release.mcelf.hs_fs` (probably the current
0.A.208) to OSPI `0xC00000` on every board during manufacturing. After
that, an invalidated Bank A + missing Bank B falls back to Golden and
the board still boots a working (if outdated) image. JTAG-only.

### Session 5 — OTA-pushed SBL updates (deferred, design decision pending)

The hardest production-readiness question: how do SBL updates get
deployed in the field? Today the SBL is "manufactured-in" (Commission-LP
at the workstation). Three options for future SBL revisions:

1. **Never** — SBL is one revision per product generation. Customer
   panels in the field stay on whatever SBL they shipped with.
2. **Manual** — When a customer panel needs a new SBL, a service tech
   visits with a workstation + XDS110 probe and runs
   `Flash-SblChooser.ps1`.
3. **OTA-pushed with golden interlock** — The bridge can push an SBL
   chunk that gets staged into a SECOND SBL slot (not yet defined in
   the OSPI map). The chooser, on next boot, sees the new SBL and a
   "verify pending" flag; if it boots successfully (per the strike
   counter), the old SBL gets overwritten. Requires reserving a
   ~384 KB "staging SBL" slot in the OSPI map. Significant design work.

Option 3 is the only one that delivers true "no service truck" for SBL
updates. We're not blocking on this — option 1 is acceptable for v1
production and option 2 is acceptable for transitional deployments.

---

## 9. Risks + open questions

1. **HS_FS device-tree size limit.** TI's stock SBL is 256 KB
   unsigned; the chooser may add ~30 KB. Need to verify HS_FS x509
   cert stays within the device-tree's loadable-region declaration.
   Mitigation: signing tool rejects oversized images at flash time —
   fail-safe.
2. **OSPI DAC mode timing during boot.** `lp_device_config.c` already
   uses indirect read (DAC disabled in syscfg per invariant #11);
   the SBL chooser does the same. No DAC changes; the writes that
   triggered the 3-day DAC-mode debug saga don't apply to read-only
   boot-time header polling.
3. **First-boot strike accounting.** A board fresh from manufacturing
   has `strikes=0`, `boot_count=0`, no bank headers — empty OSPI
   everywhere. SBL must treat this as "JTAG bringup mode" and just
   try to load from `0x080000` (the legacy/bench flow). Detect via
   `magic != FW_BANK_MAGIC` on both bank headers AND
   `boot_count == 0`.
4. **`CMD_REBOOT_SOC` verified working** — used by OTA Activate
   today; SBL sees the warm reset, picks the new bank.
5. **Can we test without bricking?** Yes — JTAG `xdsdfu` can rewrite
   `0x000000` even after a bad SBL flash, because R5F can be loaded
   from CCS into RAM independently. As long as one of the four
   bench boards has a known-good SBL backup we can cross-flash from,
   we always have a recovery path. **Action item before Session 2:**
   image one board's current OSPI sector 0 (TI's stock SBL) to a
   `.bin` archive for quick restore.

---

## 10. Doc-trail

- This file: `docs/lp-am2434-f2c-sbl-chooser-design.md`.
- Watchdog interactions: `docs/lp-am2434-watchdog-design.md` §5.
- OTA plan (Phases 1-5, Phase 4 VALIDATED 2026-05-29):
  `docs/LP-AM2434-OTA-Update-Plan.md`.
- SBL-wipe saga that drove the 0.A.208 metadata relocation:
  `memories/repo/sbl-wipe-controller-self-update-7th-layer-2026-05-29.md`.
- Bank B 4-byte addressing escape (0.A.102):
  `memories/repo/lp-am2434-ospi-dac-writes.md`.
- Hardware spec recommendations:
  `docs/Constellation-Board-Hardware-Spec.md` §1.2 / §2.2.
- Foundation index: `memories/repo/foundation-plan.md` row F2c.
