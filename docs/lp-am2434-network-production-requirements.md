# Network requirements for the production Nova PCB

> **Audience:** the engineer doing the custom-PCB layout that replaces
> the LP-AM2434 LaunchPad. Captures what's known to fail on the
> LaunchPad, what the AM2434's CPSW3G can actually do, and what the
> production PCB needs to do differently so the firmware's 100M-FD
> downshift workaround can be removed.
>
> **Source of truth for what's been investigated firmware-side:**
> `memories/repo/lp-am2434-cpsw-tx-debug.md` (14 updates over ~3 weeks).
> This doc is the hardware-side companion — what the PCB needs to be
> for the firmware to drop its hack.

## TL;DR

1. **LaunchPad gigabit doesn't work.** Documented in the memory note's
   Update 13 — "LAUNCHXL board-level RGMII trace-timing margin at
   125 MHz DDR. Not firmware-fixable." Firmware permanently downshifts
   to 100M-FD at boot. Today's bench symptom (`.2` reachable from `.1`
   via Modbus but not from pi5 via ARP) is consistent with the same
   class of L2/CPSW state instability that the downshift mostly
   papers over.
2. **Production needs gigabit.** Not for raw bandwidth (Modbus polling
   needs ~kbps), but because **the AM2434's CPSW3G is a 3-port
   Ethernet switch** (1 host + 2 external) and we want each orbit LP
   to **daisy-chain to its neighbors**. Daisy-chain only makes sense
   if every link in the chain runs reliably at the same rate as the
   uplink to the controller — i.e. gigabit, end-to-end, no downshift.
3. **Two RJ45 jacks per orbit board.** Each connected to its own
   DP83869HM (or compatible) PHY, on CPSW3G MAC port 1 and MAC
   port 2. Internally the CPSW switches between host + both PHYs in
   hardware. Wiring: controller → orbit-N-jack-A; orbit-N-jack-B →
   orbit-(N+1)-jack-A; etc. The last orbit has its B jack unused (or
   loops back to controller for ring redundancy).
4. **DP83869HM, not DP83867.** Defaults differ (Shift vs Align mode);
   our firmware is already validated against DP83869HM on the
   LaunchPad. Two of them per board keeps the proven part choice.

## Why LaunchPad gigabit fails (firmware-side conclusion)

From `memories/repo/lp-am2434-cpsw-tx-debug.md` Update 13:

> Wire-side L1 is **perfect** at gigabit — both directions clean.
> Yet all 3 L2 probes fail with errno=103 at 1000M. At 100M the same
> code path works for hours.
>
> User confirmed this matches a documented TI E2E forum issue:
> > *"1-Gbps IP Failure: When operating in 1-Gbps full-duplex mode,
> > the board may fail to acquire an IP address (showing 0.0.0.0),
> > while 100 Mbps may work fine."*
>
> **Root cause:** LAUNCHXL board-level RGMII trace-timing margin at
> 125 MHz DDR. Not firmware-fixable.

And per Update 6: setting RGMIICTL clock-delay enable bits (`bit0
RXCLKDLY`, `bit1 TXCLKDLY`) on the DP83869HM **breaks RX entirely**
on the LaunchPad. The board's PCB layout assumed delays applied
elsewhere (PHY strap or external skew). Our firmware respects this
by leaving those bits clear, but the bias gets baked into gigabit
timing-margin failure.

## AM2434 CPSW3G architecture (recap)

The CPSW3G inside the AM2434 is **a hardware Ethernet switch with
three ports**:

```
         ┌──────────────────────────────────┐
         │       CPSW3G (in-SoC switch)     │
         │                                  │
   Host  │ port 0 ───── ALE ───── port 1 ───┼── MAC1 ──[RGMII]── PHY1 ── [RJ45-A]
  (R5F)──┤                                  │
         │              ALE ───── port 2 ───┼── MAC2 ──[RGMII]── PHY2 ── [RJ45-B]
         │                                  │
         └──────────────────────────────────┘
```

The ALE (Address Lookup Engine) does **L2 learning and switching in
hardware between port 1 and port 2** without involving the host CPU
unless a frame is for the host. So an orbit board's PHY1 (RJ45-A,
upstream) and PHY2 (RJ45-B, downstream) talk to each other directly
through the switch, and the host R5F is just one more endpoint hanging
off the same switch fabric.

This is exactly the topology the user described: "two switched
Ethernet ports per orbit." No external switch IC required.

**LaunchPad uses only one of them.** MAC1 → DP83869 → RJ45 → controller.
MAC2 is unrouted. Production PCB must route MAC2 to a second PHY +
RJ45 to enable daisy-chain.

## DP83869HM RGMII timing (the actual gigabit fix)

The firmware investigation (memory note Update 6) and the DP83869
search results agree:

| Thing | DP83869HM default | What it means |
|---|---|---|
| RGMIICTL (reg 0x32) bit 0 — RX_CLK_DELAY | 0 (delay OFF) | "RGMII receive clock shifted vs receive data" — i.e. PHY assumes MAC delays it OR the PCB does |
| RGMIICTL (reg 0x32) bit 1 — TX_CLK_DELAY | 0 (delay OFF) | "RGMII transmit clock shifted vs transmit data" — same |
| RGMIIDCTL (reg 0x86, MMD-31) bits [3:0] — RX_DELAY | 0x7 (per LaunchPad bench dump) | Delay VALUE programmed (~2.0 ns at 250 ps/step) but **not enabled** until RGMIICTL bit 0 is set |
| RGMIIDCTL bits [7:4] — TX_DELAY | 0x7 | Same for TX |

DP83869HM defaults to "Shift Mode" — clock shifted vs data by
default. DP83867 defaults to "Align Mode." We use DP83869HM.

**Where the gigabit-margin problem hides:** in pure Shift Mode the
PHY emits TX_CLK already-shifted relative to TX_DATA, so the MAC
receives data with healthy setup/hold against the clock edge — at
gigabit (125 MHz DDR), the eye is ~4 ns wide. The LaunchPad's PCB
trace lengths between the AM2434 RGMII pins and the DP83869HM aren't
matched to the typical eye margin, so the data ends up arriving
outside the clock's setup window after the on-chip RGMII pad delays
add up. At 100 Mbps (25 MHz, 40 ns period), the eye is huge and the
mismatch doesn't matter.

**Production fix options (any one of them, but redundancy doesn't hurt):**

1. **PCB skew matching.** Lay out RGMII data + clock pairs trace-
   length-matched within ~25 mils each direction (TX side: TX_CLK +
   TXD[3:0] + TX_CTL; RX side: RX_CLK + RXD[3:0] + RX_CTL).
   This is by far the cleanest. The AM2434 datasheet sets per-pin
   skew budgets; the typical answer is "match within ±50 ps electrical,
   which is ~10 mm of trace at εr~4."
2. **Add PCB-side skew lines** (serpentines on the clock pair only)
   to shift TX_CLK relative to TXD by ~2 ns at the receiver. Equivalent
   to enabling the PHY's internal TX delay but done in copper instead
   of silicon, which has tighter tolerance.
3. **Enable PHY internal delays** by setting RGMIICTL bits 0+1 +
   choosing the right RGMIIDCTL value. **This is what failed on the
   LaunchPad** (Update 6: setting these bits broke RX) but only
   because the LaunchPad already has some delay elsewhere that
   compounded into "too much delay" when the PHY added more. On a
   fresh PCB layout with no extra delay, setting these bits + DCTL=0x77
   IS the standard fix.

The right answer is **(1) trace-length matching as the primary,
(3) PHY internal delays as the tuning knob** with `RGMIIDCTL` set
to 0x77 (2.0 ns/2.0 ns) as the starting point and BOTH RGMIICTL
delay-enable bits SET. Firmware-side change to support this:
remove the `restore_rgmii_ctl()` call that forces RGMIICTL back to
0x00D0 (delays OFF) — or make it `#ifdef LAUNCHPAD` only.

## Two-port daisy-chain topology spec

```
Pi5 (office side, 192.168.10.108) ──[UART /dev/ttyAMA0]── Nova-controller (10.47.27.1)
                                                                │
                                                                │ CPSW3G MAC1 ─[RGMII]─ PHY1 ─[RJ45]
                                                                │
                                                                ▼
                                                         ┌──────────────┐
                                                         │ STORAGE (.2) │   2 jacks: A (upstream), B (downstream)
                                                         └──────────────┘
                                                                │
                                                                ▼
                                                         ┌──────────────┐
                                                         │ GDC (.3)     │
                                                         └──────────────┘
                                                                │
                                                                ▼
                                                         ┌──────────────┐
                                                         │ TRITON (.4)  │   last hop, jack B unused
                                                         └──────────────┘
```

Controller can have one jack (since it terminates the chain on its
side), or two jacks if we want to support ring topology (TRITON's
B-jack loops back to controller's second jack for redundancy).

### MAC port assignment per role

| Role | CPSW MAC1 (jack A) | CPSW MAC2 (jack B) |
|---|---|---|
| Controller (.1) | DP83869 → RJ45 → next orbit's A | (unused, OR ring-closure B) |
| Storage (.2) | DP83869 → RJ45 ← controller's MAC1 | DP83869 → RJ45 → GDC's A |
| GDC (.3) | DP83869 → RJ45 ← storage's MAC2 | DP83869 → RJ45 → TRITON's A |
| TRITON (.4) | DP83869 → RJ45 ← GDC's MAC2 | (unused, OR ring-closure to controller) |

### MDIO bus + PHY addressing

CPSW3G has one MDIO master that talks to ALL on-board PHYs (both PHY1
and PHY2 on the same orbit). PHY addresses are set by the
**strap pins** at power-up — typically PHY_AD[4:0] on the DP83869HM.

LaunchPad currently has:
- PHY addr 3 (CPSW-routed jack)
- PHY addr 15 (ICSSG jack — unused by our firmware)

Production assignment:
- PHY1 (MAC1) → MDIO address 3 (match LaunchPad so firmware doesn't change)
- PHY2 (MAC2) → MDIO address 7 (or whatever's convenient — must differ from PHY1)

Firmware-side: `lwip_smoke_task` and `lwip_phy_fixup_task` currently
hard-code PHY addr 3 in places. For production we'll need to detect
both PHYs (the `scan_mdio_bus()` helper from the memory note Update 8
already exists) and apply the boot-time config to both.

## PCB-level requirements summary

| Item | Requirement | Why |
|---|---|---|
| RGMII trace impedance | 50 Ω single-ended, controlled | Reflection-free at 125 MHz |
| RGMII trace-length matching | TX group ±25 mils; RX group ±25 mils; differential pairs ±10 mils within pair | Setup/hold at gigabit eye |
| RGMII clock vs data delay | ≤200 ps skew between clock and data at PHY pin | DP83869 internal delays can add up to 4 ns to nudge into eye |
| MDIO bus | Single MDC/MDIO pair from CPSW to both PHYs | Standard topology |
| PHY power | 3.3 V VDDIO + 1.0 V VDDA + 2.5 V VDD2P5 per DS | Most production board fail-modes are decoupling on these |
| Magnetics | Industrial gigabit-rated, low jitter (look at e.g. Pulse H5008NL or similar) | LaunchPad uses commodity magnetics adequate for 100M but maybe marginal at 1000M |
| RJ45 jack | Industrial shielded, 5-class minimum, LEDs optional | EMC + reliability |
| Reset pin sequencing | PHY RESET held >10 ms after VDD ramp, then high before MDIO accesses | DP83869 strap latching window |
| Strap pins (PHY_AD, RGMII_TX_RX_DELAY) | Pull-up/down resistors chosen for desired strap value | Sets PHY address + default delay mode |
| OSC | 25 MHz ±50 ppm to PHY XI pin | Per DS |

## Firmware changes once production PCB lands

When the new PCB is bench-validated to run gigabit reliably:

1. Remove the `downshift_phy_to_100m_aneg()` call from
   `lwip_smoke_task` AND `lwip_phy_fixup_task` in `lwip_smoke.c`.
   Keep the function bodies behind a `#ifdef CONSTELLATION_LAUNCHPAD`
   in case we ever need to test on the LaunchPad again.
2. Remove the `restore_rgmii_ctl(0x00D0)` call (the "RGMII clock
   delays OFF" forcing). On the production board the right value
   will be `0x00D3` (both delays ENABLED), which is the SDK default.
3. Add a `lwip_phy_fixup_task` enhancement that initializes BOTH
   PHYs (MAC1 + MAC2) — currently only PHY at MDIO address 3 is
   touched.
4. Enable CPSW MAC2 in SysConfig (`example.syscfg` —
   `enet_cpsw1.DisableMacPort2` currently true; flip to false).
5. Re-validate ALE config — `macOnlyEn_macPort2` should mirror MAC1
   (true) and `macOnlyEn_hostPort` stays false (per Update 1 of the
   memory note).
6. Test the daisy-chain by physically chaining 4 boards and running
   `probe_fleet.ts` from pi5 — each orbit must respond at its
   configured IP.

## Open questions for the proto board bring-up

Things we can't decide from a desk; need bench testing on the first
custom PCB:

1. Whether 25-mil trace matching is sufficient, or we need tighter
   (some app notes say ±5 mils for safety margin).
2. Whether `RGMIIDCTL=0x77` (2.0 ns/2.0 ns) is the right starting
   point for the new layout, or we need a different value.
3. Whether two PHYs on the same MDIO bus cause any timing issues
   (theoretically fine; in practice some PHYs misbehave when their
   MDIO neighbor changes state mid-transaction).
4. Whether the ring-closure (TRITON's B jack to controller's MAC2)
   is worth the extra cabling complexity for the safety it provides.
   STP / RSTP would need to run on the CPSW switch fabric to avoid
   broadcast storms in a closed ring.
5. Whether we need TSN / PTP synchronization across the chain
   (CPSW3G supports it; not used by current firmware).

## References

- Internal: [`memories/repo/lp-am2434-cpsw-tx-debug.md`](../memories/repo/lp-am2434-cpsw-tx-debug.md) — 14-update firmware-side debug trail.
- Internal: [`docs/uart-airgap-architecture.md`](uart-airgap-architecture.md) — wider system context (Pi5↔Nova UART boundary).
- TI: [DP83869HM datasheet](https://www.ti.com/lit/ds/symlink/dp83869hm.pdf) — RGMII configuration in §7.5; ANA_RGMII_DLL_CTRL register (0x86) and RGMII_CTRL (0x32) detail.
- TI E2E: [AM2434: Daisy chain connection using AM2434 Ethernet](https://e2e.ti.com/support/microcontrollers/arm-based-microcontrollers-group/arm-based-microcontrollers/f/arm-based-microcontrollers-forum/1299908/am2434-daisy-chain-connection-using-am2434-ethernet) — community discussion of the same topology this doc specifies; confirms GMAC + ALE forwarding behavior in 2-port mode.
- TI E2E: [AM5728: DP83869 RGMII not working](https://e2e.ti.com/support/processors-group/processors/f/processors-forum/935462/am5728-dp83869-rgmii--1000basex-not-working) — adjacent-SoC report of same RGMII delay tuning issue.
- TI E2E: [AM3359: RGMII 1Gbps not able to Ping](https://e2e.ti.com/support/processors-group/processors/f/processors-forum/1442039/am3359-rgmii-1gbps-not-able-to-ping) — older Sitara hitting same class of issue; community workarounds applicable.
- TI E2E: [DP83869HM RGMII delay](https://e2e.ti.com/support/interface-group/interface/f/interface-forum/923963/dp83869hm-dp83869hm-rgmii-delay) — TI's own guidance on the 0x32 / 0x86 register pair.
- Linux netdev: ["net: dp83869: Add RGMII internal delay configuration"](https://www.spinics.net/lists/netdev/msg663006.html) — kernel-side patch series documenting which RGMII delay scenarios need driver-side intervention.
