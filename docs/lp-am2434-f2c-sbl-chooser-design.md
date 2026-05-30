# LP-AM2434 — Custom Stage-2 SBL + A/B Bank Rollback (F2c)

> **Status:** design + partial scaffold (May 5 2026, fw 0.A.59 baseline).
>
> **Goal:** unattended OTA recovery. A bad Bank-B image can't permanently
> brick the panel — after three failed boots the SBL falls back to
> Bank A (or Golden if both are broken). Combined with the dual-core
> watchdog (F3, fw 0.A.59), this gives true field-deployable updates.

---

## 1. Boot stack today vs after F2c

| Today | After F2c |
|---|---|
| ROM (stage 1) → TI stock `sbl_ospi` (stage 2, hardloads `0x80000`) → Nova app | ROM → **our forked `sbl_chooser`** (stage 2, reads bank header → picks A/B/Golden by sequence + strikes) → Nova app |

The "stage 2" we're replacing is TI's `sbl_ospi` example — a NoRTOS R5FSS0_0 image (~250 KB signed) that lives at flash offset `0x000000` and loads an MCELF from a hard-coded offset (`0x80000` today). We fork it once into
`Nova_Firmware/lp_am2434/sbl_chooser/` and never touch it again.

**Important:** the proto layout in `nova_fw_update.h` claimed "header at
`0x000000`" — that's WRONG. The ROM hard-codes flash `0x000000` as the
SBL load location; that's where our SBL itself lives. The bank-metadata
region must move.

---

## 2. Canonical OSPI map (W25Q64JV, 8 MB)

| Offset | Size | Owner | Notes |
|---|---|---|---|
| `0x000000` – `0x05FFFF` | 384 KB | **`sbl_chooser` (custom stage-2 SBL)** | Signed `tiboot3.bin`. ROM hard-loads from here. |
| `0x060000` – `0x06FFFF` | 64 KB | **`FwBootMeta` + bank A header** | One sector for `FwBootMeta`, the rest is bank A's `FwBankHeader`. Edited every save → ping-pong with bank B header to avoid wear. |
| `0x070000` – `0x07FFFF` | 64 KB | **`FwBankHeader` for Bank B** | Separate sector so updating one bank's metadata doesn't erase the other's. |
| `0x080000` – `0x1FFFFF` | 1.5 MB | **Bank A app image** | mcelf.hs_fs. Same offset TI's stock SBL hardloads today → first-flash migration is just "flash our SBL to 0x000000" + write bank A header. The app at 0x80000 stays put. |
| `0x200000` – `0x37FFFF` | 1.5 MB | **Bank B app image** | mcelf.hs_fs. Inactive bank target for OTA. |
| `0x380000` – `0x4FFFFF` | 1.5 MB | **Golden image** | Manufacturing-flashed read-mostly. SBL falls back here if both A and B fail validation. Phase 4 of OTA plan. |
| `0x500000` – `0x5FFFFF` | 1 MB | **RESERVED** | Future log buffers, asset files, larger Golden. |
| `0x600000` – `0x61FFFF` | 128 KB | `lp_device_config` ping-pong banks | **already migrated here** (May 2026). Per-board role/IP/MAC. |
| `0x620000` – `0x7FFFFF` | ~2 MB | `LpSettings` vault (future) | Currently MSRAM-only. When OSPI persistence lands, lives here. |

**Why these specific sizes:**

- 384 KB SBL: TI's stock `sbl_ospi_multi_partition` is ~256 KB unsigned,
  ~340 KB after HS_FS signing. Round up to 6 sectors (96 × 4 KB) for
  safety margin.
- 64 KB header sectors: smallest unit is one 4 KB sector but we want
  per-bank isolation — updating bank B's CRC must NEVER erase bank A's
  header. Two separate 64 KB regions keep them on independent erase
  blocks. (The remaining 60 KB per region is reserved for future
  per-bank metadata: signed manifest, dependency graph, etc.)
- 1.5 MB per bank: current `nova_lp.release.mcelf.hs_fs` is 525 KB
  (3× margin). Future ethernet+USB+TLS could push it past 1 MB. Plenty
  of room.
- The `0x080000` Bank A offset is the **single biggest design decision**:
  by matching it to TI's stock SBL load offset, we get a **migration-
  free first flash**. Existing boards' app stays where it is; only the
  SBL gets replaced. This eliminates "every existing board needs a
  full re-flash" risk.

---

## 3. Bank selection algorithm (executed by `sbl_chooser`)

```
1. Read FwBootMeta from 0x060000.
2. Read FwBankHeader[A] from 0x060000+0x80, FwBankHeader[B] from 0x070000.
3. Increment boot_count.
4. Increment watchdog_strikes (cleared by app once "healthy").
5. Build candidates list, sorted by (sequence DESC, valid DESC):
     A if A.magic OK AND A.valid AND A.crc==stored
     B if B.magic OK AND B.valid AND B.crc==stored
     GOLDEN unconditionally
6. For each candidate:
     If watchdog_strikes >= 3 AND this is the highest-sequence bank:
       skip — assume previous boot bricked and we shouldn't try again.
       Log boot_reason = FALLBACK.
     Try to parse the mcelf at this candidate's offset.
     On success: jump to it (and the app clears strikes within 30 s).
     On failure: continue to next candidate.
7. If no candidate boots: spin in SBL with [SBL] FATAL on UART.
```

**Strike counting:**

- SBL increments `watchdog_strikes` BEFORE jumping to the chosen bank.
- App clears strikes via `NovaFwUpdate_ConfirmBoot()` once 30 s of
  alive criteria hold (see watchdog F3 doc).
- If the app hangs before 30 s, next boot's SBL sees `strikes=2`.
  After three such boots, `strikes>=3` triggers fallback to the
  previous bank (or Golden).

**Atomicity of the strike write:** SBL writes `FwBootMeta` BEFORE
loading the app image. If power fails between strike-write and
bank-jump, next boot sees the higher count and proceeds normally —
worst case is one extra strike accounted to a fault that wasn't a
real hang. Acceptable.

**Why `NovaFwUpdate_BootValidation` already in
`Platform/nova_fw_update.c` lines 357-396 implements exactly this**
— we can re-use it 95% as-is. The only changes are: offset constants
move to `0x060000` for headers, `0x080000` / `0x200000` for banks
(matching this map).

---

## 4. Healthy milestone (= "the new image works")

Wired by main core's watchdog heartbeat task. From
[`docs/lp-am2434-watchdog-design.md`](lp-am2434-watchdog-design.md) §5:

| Bit | Source |
|---|---|
| ALIVE_MODBUS | orbit Modbus accept-loop heartbeat |
| ALIVE_SYSTEMSTATUS | tick counter in main.c::data_exchange |
| ALIVE_LWIP_LINK | netif_default flags |
| ALIVE_ENGINE_TICK | lp_engine_tick last-ran timestamp |
| ALIVE_WD_PEER | watchdog core's wd_counter advanced in last 2 s |

**Threshold:** all 5 bits must hold for 30 consecutive seconds. Once
that hits, `NovaFwUpdate_ConfirmBoot()` writes `watchdog_strikes = 0`
to OSPI. Idempotent — only writes if `strikes != 0`. No flash wear
on healthy boots.

If the new firmware is broken in a way that prevents reaching healthy
within 30 s × 3 boots, the SBL chooser falls back automatically.

---

## 5. Migration plan (existing-board safe)

The bench has 3 LP boards already provisioned with TI's stock SBL +
fw 0.A.x at `0x080000`. The migration to F2c needs to be lossless —
no required JTAG re-flash dance, no hardware-store trip.

Per board, in this order:
1. **Build & test new app at 0.A.60 (no SBL change yet).** Confirms
   the new app codepath (which now links `nova_fw_update.c`) doesn't
   break anything on a stock-SBL system.
2. **JTAG-flash custom `sbl_chooser` to 0x000000.** This replaces TI's
   stock SBL. The app at `0x080000` stays put.
3. **JTAG-write FwBootMeta + bank-A FwBankHeader at 0x060000.** Set
   sequence=1, magic=NOVA, valid=1, image_size and crc computed from
   the existing image at `0x080000`. After this, the SBL chooser will
   boot the existing app on the next reset.
4. **Reset and verify.** Boot trace shows `[SBL] bank=A seq=1 strikes=1`
   (the 1 is normal — clears within 30 s when app reports healthy).
5. **First OTA push then writes Bank B at `0x200000`** with sequence=2.
   Subsequent updates ping-pong between A and B.

**Recovery path if `sbl_chooser` itself is broken:** flash TI's stock
`sbl_ospi` back to `0x000000` via `Flash-LP.ps1 -Probe X -Sbl
TI-stock` (a new flag). The app at `0x080000` is untouched and boots
normally without bank metadata.

---

## 6. What gets implemented when

### Session 1 (this one) — non-flash-touching pieces

- ✅ This design doc.
- Update `nova_fw_update.h` offsets to match canonical map.
- Link `Platform/nova_fw_update.c` + `Platform/hal_flash.c` into the
  LP build. Call `NovaFwUpdate_Init()` after `LpDeviceConfig_Init()`.
- Wire `NovaFwUpdate_ConfirmBoot()` from the watchdog heartbeat task
  once 30 s healthy.
- Add diagnostic CGI / proto fields to expose `FwBootMeta` + both
  bank headers to the bridge (UI debug only — read-only).

These are reversible — none of them change what's flashed at any
specific offset on the board. If the link fails or
`NovaFwUpdate_ConfirmBoot` misbehaves, just revert the makefile +
client-side hooks; the OSPI is untouched.

### Session 2 (next bench session) — `sbl_chooser` fork

- Copy TI's `sbl_ospi_multi_partition` example into
  `Nova_Firmware/lp_am2434/sbl_chooser/`.
- Insert A/B selection logic into its `main.c` (use existing
  `NovaFwUpdate_BootValidation` from Platform, port to NoRTOS bare
  metal — no FreeRTOS in the SBL).
- Build + sign per TI's standard flow → `sbl_chooser.release.tiboot3.bin`.
- JTAG-flash to `0x000000`. **First on a single bench board only.**
- Verify boot trace, then verify negative test (corrupt Bank A's
  CRC manually, confirm fallback to Golden which we'll have written
  too).

### Session 3 — bridge OTA push module

Phase 5 of `LP-AM2434-OTA-Update-Plan.md`. Not blocked by F2c except
in the trivial sense that `NovaFwUpdate_Activate` becomes useful only
once F2c is on the board.

---

## 7. Risks + open questions

1. **Does ROM's HS_FS device-tree care about SBL size?** TI's stock
   SBL is 256 KB unsigned; we may add ~30 KB for the chooser logic.
   Need to verify HS_FS x509 cert stays within the device-tree's
   loadable-region declaration. (Mitigation: signing tool will reject
   oversized images at flash time — fail-safe.)
2. **OSPI DAC mode timing during boot.** `lp_device_config.c` already
   uses indirect read (DAC disabled in syscfg). The SBL chooser does
   the same — `Flash_read` via OSPI indirect mode for header polling.
   No DAC changes.
3. **First-boot strike accounting.** A board fresh from manufacturing
   has `strikes=0`, `boot_count=0`, no banks — empty OSPI everywhere.
   SBL must treat this as "JTAG bringup mode" and just try to load
   from `0x080000` (the legacy/bench flow). Detect via `magic !=
   NOVA` on both bank headers AND boot_count == 0.
4. **`CMD_REBOOT_SOC` already verified working** (May 3 entry in
   `firmware-version-current.md`). OTA Activate calls this; SBL
   sees the warm reset, picks the new bank.
5. **Can we test without bricking?** Yes — JTAG `xdsdfu` can rewrite
   `0x000000` even after a bad SBL flash, because R5F can be loaded
   from CCS into RAM independently. As long as one of the three
   bench boards has a known-good SBL backup we can cross-flash from,
   we always have a recovery path. **Action item before session 2:**
   image one board's current OSPI sector 0 to a `.bin` archive for
   quick restore.

---

## 8. Doc-trail

- Architecture (this file): `docs/lp-am2434-f2c-sbl-chooser-design.md`.
- Watchdog interactions: `docs/lp-am2434-watchdog-design.md` §5.
- Existing OTA plan (Phases 1-5): `docs/LP-AM2434-OTA-Update-Plan.md`.
- Hardware spec recommendations driving F2c needs:
  `docs/Constellation-Board-Hardware-Spec.md` §1.2 / §2.2.
- Quick-ref + bench recipe (created in session 2):
  `/memories/repo/lp-sbl-chooser-bringup.md`.
- Foundation index: `/memories/repo/foundation-plan.md` row F2c.
