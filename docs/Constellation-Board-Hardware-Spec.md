# Constellation Board — Hardware Spec (v0)

> **Status:** v0 draft, 2026-05-01.  Captures the hardware decisions
> that have been forced on us by debugging the LP-AM2434 launchpads and
> by the OTA / production-deployment plan in
> [LP-AM2434-OTA-Update-Plan.md](LP-AM2434-OTA-Update-Plan.md).
>
> **Audience:** PCB designer + parts buyer.  Use this as the
> requirements list when laying out the first custom Constellation
> board.  When something here changes, update both this doc and
> [/memories/repo/foundation-plan.md](/memories/repo/foundation-plan.md)
> §F4 so the next AI session sees the same spec.
>
> **Why now:** the LP launchpad has been useful for firmware bring-up
> but four classes of pain are now blocking us, all of which need
> hardware fixes:
>   1. We can't reset the SoC remotely (no RESETz wired anywhere
>      we control).  Every firmware iteration costs a USB-C unplug.
>   2. There is no hardware watchdog, so a hung firmware image after
>      OTA = brick-until-bench-visit.
>   3. OSPI flash is only 8 MB (W25Q64).  The A/B/Golden/Settings
>      layout in `proto/agristar/firmware.proto` cramps each region
>      to ~2 MB.
>   4. The PHY (DP83867) needs a runtime hack
>      (`lwip_phy_fixup_task`) before TX works at gigabit; this is
>      production-fragile.

---

## 1. Top-3 must-haves (production gate)

These three items are the hardware half of the OTA-without-power-cycles
story.  Without them, [Phase 3](LP-AM2434-OTA-Update-Plan.md) of the
OTA plan can't execute end-to-end, and we cannot ship firmware updates
remotely.

### 1.1  RESETz under software control
- **Wire MCU `MCU_PORz_OUT` (or `RESETz`) to a GPIO of the rpi5** AND
  to a supervisor IC (1.2 below).
- **rpi5 path:** any free GPIO; pulse low for ≥5 ms drives a clean
  warm reset.  Required because the AM2434 has *no* working remote
  reset path from the running firmware
  (verified 2026-05-01, see [/memories/repo/lp-am2434-no-remote-reset.md](/memories/repo/lp-am2434-no-remote-reset.md)
  — `target.reset()`, DSS "System Reset", and `SW_MAIN_WARMRST`
  register write all fail).
- **Test point + jumper:** include a 0Ω jumper between rpi5 GPIO and
  RESETz so the link can be cut for safety/debug.
- **Cost:** ~free (one trace + one resistor).

### 1.2  Hardware watchdog supervisor
- **Part candidate:** `TPS3823-33DBVR` or `MAX6369KA+T` (8-pin SOT-23,
  windowed watchdog with internal RESETz drive).
- **Wiring:**
  - `WDI`  ← MCU GPIO (firmware strobes from a low-priority FreeRTOS
    task that itself only ticks if the "healthy" counters in
    higher-priority tasks are still incrementing — see foundation-plan
    F3).
  - `RESETz` → MCU `MCU_PORz_OUT` (and same trace as 1.1).
  - Timeout: **10–30 s window** (slow enough that long flash erases
    during OTA don't trip it; fast enough that a hung CPU reboots
    quickly).
- **Why this matters for OTA:** combined with the A/B
  `FwBootMeta.watchdog_strikes` field (`Nova_Firmware/Platform/nova_fw_update.h`)
  and the Phase 3 stage-2 SBL chooser, this gives **automatic rollback
  when a freshly-flashed firmware image hangs at boot** — without any
  human intervention.  This is the single largest unlock for
  unattended remote updates.
- **Cost:** ~$0.50.

### 1.3  OSPI flash ≥16 MB (32 MB strongly preferred)
- **Today (LP):** Winbond W25Q64 = 8 MB.  `firmware.proto` carves
  this into header (64 KB) + Bank A (~2 MB) + Bank B (~2 MB) +
  Golden (~2 MB) + Settings (~2 MB).  No room for log buffers,
  asset/translation files, or future image growth.
- **Target:** Winbond W25Q128JV (16 MB) minimum, W25Q256JV (32 MB)
  preferred.  Pin-compatible with W25Q64; `hal_flash.c` already
  treats it as generic SPI NOR.
- **New layout (32 MB):** header 64 KB + Bank A 8 MB + Bank B 8 MB +
  Golden 8 MB + Settings 4 MB + spare 4 MB.  Leaves comfortable
  margin for image growth.
- **Cost:** ~$1 BOM diff vs. W25Q64.

---

## 2. Strongly recommended (operational hygiene)

### 2.1  Brownout detector + hold-up cap
- **Part candidate:** TI `TPS3839L30DBV` (3.0 V threshold, 10 µA) on
  the 3V3 rail; or use the supervisor IC from 1.2 if it includes
  brownout (TPS3823 does).
- **Hold-up:** ≥220 µF on 3V3 close to the OSPI VCC pin sized to
  hold up for one full-sector erase (typ 100 ms, max 400 ms for a
  W25Q* sector).
- **Why:** prevents partial-erase brick during OTA.  The Phase 1B
  "copy Bank B → 0x80000 on activate" hack is doomed without this.
  Even Phase 3 wants the safety net.

### 2.2  Recovery GPIO into the stage-2 stub
- **Wire one GPIO** to a small momentary button (or a 2-pin
  test-point + jumper).  The Phase 3 stage-2 SBL chooser reads this
  pin at boot; if asserted, it bypasses A/B selection and jumps
  directly to the Golden image at OSPI `0x400000`.
- **Why:** if both Bank A and Bank B brick (impossible-but-real
  scenario: bad image flashed twice in a row), the field tech can
  recover the board with a paper clip — no JTAG ever leaves the lab.

### 2.3  MAC EEPROM with factory-programmed address
- **Part candidate:** Microchip `24AA025E48-I/SN` (8-pin SOIC, ~$0.20,
  pre-programmed EUI-48).  Sits on a free I²C bus.
- **Why:** today the LP draws a per-board random MAC at boot
  (`1c:63:49:13:d8:61` on our controller).  This caused us to
  misidentify the lab SIP phone as our LP for several hours
  (see [/memories/repo/lan-ip-map.md](/memories/repo/lan-ip-map.md)).
  A factory-programmed MAC with a stable Agristar-owned OUI eliminates
  that whole class of bug, and is required for any kind of network
  asset tracking.
- **Action item:** **Apply for an IEEE OUI for Agristar before tape-out.**
  ($1750 one-time fee, ~6 weeks lead time.  Without it, every
  Constellation board ships under "locally administered" MACs which
  is acceptable but unprofessional.)

---

## 3. Nice-to-have / future-proofing

### 3.1  PHY with a mature MCU+ SDK driver
- **Today:** TI DP83867.  Required runtime hack
  (`lwip_phy_fixup_task` — see
  [/memories/repo/lp-am2434-cpsw-tx-debug.md](/memories/repo/lp-am2434-cpsw-tx-debug.md)
  Updates 12-14) to get gigabit RGMII TX working.  100M downshift
  occasionally needed.
- **Recommended swap:** DP83822IRHBT (10/100, RMII).  Smaller,
  cheaper, has none of the gigabit timing quirks, supported by
  `dp83822` driver in MCU+ SDK with no fixups.  Industrial control
  panels almost never need >100M.
- **If gigabit is required for some future feature:** stay on
  DP83867 but accept that the PHY-fixup task must remain.

### 3.2  RTC with battery backup
- **Part:** `MCP79410-I/SN` (~$0.50) on I²C.  Coin cell holdup.
- **Why:** the LP has an internal RTC but no battery, so wall-clock
  time resets every reboot.  Today the bridge syncs time from the
  rpi5 over UART — works fine *while the bridge is up*.  Putting an
  MCP79410 on each board lets stand-alone diagnostics keep accurate
  timestamps even with no bridge connection.

### 3.3  USB-C power + dedicated debug UART header
- USB-C PD-aware sink so the same cable that powers the rpi5 can
  power a Constellation board for service.
- 0.1" pin header on `MCU_UART0_TXD/RXD` for service console (Phase
  D BB tracing — see [/memories/repo/lp-am2434-uart-rx-fifo-trap.md](/memories/repo/lp-am2434-uart-rx-fifo-trap.md)).

### 3.4  Provision for in-circuit programming
- **Tag-Connect TC2050-IDC-NL footprint** for ARM 10-pin JTAG.
  Used at manufacturing for first-flash and one-time bootloader/key
  programming, then never again.  Do NOT populate the connector —
  pads only — to keep the production board cheap and tamper-resistant.

---

## 4. Action checklist before PCB tape-out

- [ ] Confirm OSPI part choice (W25Q128 vs W25Q256) with cost analysis.
- [ ] Pick supervisor IC (TPS3823 vs MAX6369) — windowed-watchdog
      timeout values, RESETz drive level, cost.
- [ ] Pick PHY (DP83822 RMII vs DP83867 RGMII).
- [ ] Apply for IEEE OUI for Agristar.
- [ ] Decide on rpi5 ↔ board interconnect (UART today, possibly add
      I²C/SPI for richer telemetry).
- [ ] Pick whether to populate JTAG connector at manufacturing or
      pads-only.
- [ ] Confirm voltage rails (3V3 sole rail vs split MCU/IO/PHY).
- [ ] Lock the boot mode pin strap so the SBL always loads from OSPI.

---

## 5. Cross-references

- OTA plan: [LP-AM2434-OTA-Update-Plan.md](LP-AM2434-OTA-Update-Plan.md)
  (Phases 1-3, hardware enablers in Phase 3).
- Foundation plan: [/memories/repo/foundation-plan.md](/memories/repo/foundation-plan.md)
  §F4 (this doc IS the live deliverable for F4).
- Bring-up history (LP launchpad): [LP-AM2434-Hardware-Bringup-Plan.md](LP-AM2434-Hardware-Bringup-Plan.md)
  — record of every PHY / OSPI / reset trap we hit.
- IP map / MAC trap: [/memories/repo/lan-ip-map.md](/memories/repo/lan-ip-map.md).
