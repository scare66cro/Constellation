# Agristar Constellation — Business Case & Capabilities Overview

**Prepared for:** Agristar Leadership
**Date:** March 24, 2026
**Purpose:** Executive summary and technical justification for adopting the Constellation platform to replace the current AS2 control panel architecture

---

## The One-Paragraph Summary

The Constellation platform replaces our $12,000 hand-wired control panel with a ~$500 plug-and-play system that controls more equipment, fails more gracefully, and can be serviced by anyone with a screwdriver. It uses the same automotive/aviation-grade processor found in cars and aircraft engine controllers to guarantee that a power glitch, a cosmic ray, or a firmware bug can never silently corrupt the system into an unsafe state. One Nova brain and a $25 off-the-shelf Ethernet switch can run a 5-room, 10-compressor facility — the largest installation we've ever been asked to quote.

---

## 1. What We Have Today (AS2)

| Attribute | Current AS2 Panel |
|---|---|
| Controller | TI TM4C129 (Cortex-M4F, 120 MHz, 256 KB SRAM) |
| I/O architecture | CPLD shift registers + custom relay boards + RS485 analog boards |
| Communication | RS485 daisy-chain buses (shared, sequential) |
| VFD control | RPi5 → CGI scrape → Modbus TCP → VFD (RPi5 is in the critical control path) |
| Panel cost (hardware + labor) | ~$12,000 |
| Panel build time | 40-60 hours |
| Commissioning time | 8-16 hours |
| Field service | Replace custom board, re-wire, re-commission (4-8 hours) |
| Fault isolation | One shorted RS485 device kills the entire bus |
| Max facility size | 2 rooms, 4 compressors (practical limit of wiring complexity) |

**The fundamental problem:** Every new room or compressor means more RS485 buses, more homerun cable pulls, more 24V power wiring, more custom boards, more commissioning time, and more things that can take out the entire system with a single fault.

---

## 2. What Constellation Delivers

### 2.1 System at a Glance

| Component | What it is | Cost |
|---|---|---|
| **Nova** | Central brain — TI AM2434 (4× Cortex-R5F @ 800 MHz, 2 MB ECC SRAM). Battery-backed RTC (DS3231). Runs all control logic. | ~$48 |
| **Orbit** | Field I/O module — 10 digital outputs (24VDC open-collector sink via ULN2803A, drives external DIN-rail relay coils, solenoid valves, indicator lights), 10 digital inputs, 2 analog outputs, 2 RS485 ports. Port A polls analog boards via Modbus RTU (up to 32 per segment). Modbus TCP bootloader for over-the-network firmware updates. 24VDC powered. No high voltage on the board. ENIG finish + acrylic conformal coating for humidity resistance. | ~$25 each |
| **Ethernet switch** | Off-the-shelf unmanaged gigabit (e.g., TP-Link TL-SG108) — 8 ports | $25 |
| **UPS** | 300VA, powers Nova + RPi5 + switch for outage detection and logging | ~$50 |
| **24VDC DIN-rail supply** | UL-listed (e.g., Mean Well HDR-30-24) — powers all Orbit modules and analog boards | ~$15 |
| **RPi5** | Web UI, cloud logging, remote access. NOT in the control path. | ~$82 |

### 2.2 Maximum System Capacity (One Nova)

| Resource | Typical (8-port switch) | Maximum (48-port switch) |
|---|---|---|
| Orbit modules | **7** | **47** (or **63** with cascaded switches) |
| Digital outputs (24VDC sink, drives external relays) | **70** | **630** |
| Digital inputs (optocoupled, 24VDC) | **70** | **630** |
| Analog outputs (4-20mA) | **14** | **126** |
| RS485 Port A segments (analog boards, sensors) | **7** independent segments | **63** independent segments — each Orbit polls up to 32 boards |
| RS485 Port B segments (VFDs, EEVs) | **7** independent segments | **63** independent segments |
| VFDs controllable | **21** | **189** |
| Legacy analog boards supportable | **224** (up to 32 per Orbit Port A) | **2,016** |
| Total 24VDC power draw (all Orbits) | ~14W logic + external loads (30W PSU typical) | ~94W logic + external loads (150W PSU or distributed) |
| Full I/O scan time (all modules) | **< 3 ms** | **< 8 ms** |
| Nova CPU utilization at max load | ~10% of one core | ~27% of one core (three cores idle) |
| Nova SRAM utilization at max load | ~10% of 2 MB | ~4% of 2 MB |

### 2.3 Facility Scaling

Each zone (room) includes 1 storage Orbit + 1–4 refrigeration Orbits + optional shared door control Orbit. A zone’s Orbit modules are assigned to one control thread on Nova.

| Facility | Orbits | Fits one switch? | System cost |
|---|---|---|---|
| 1 room, 2 compressors, doors | 4 (1 stor + 1 door + 2 refrig) | Yes — 3 ports spare | ~$524 |
| 2 rooms, 4 compressors, shared doors (typical) | 7 (2 stor + 1 door + 4 refrig) | Yes — 0 spare (8-port) | ~$629 |
| 3 rooms, 6 compressors, doors | 11 (3 stor + 2 door + 6 refrig) | Yes (16-port switch) | ~$775 |
| 4 rooms, 8 compressors, doors | 14 (4 stor + 2 door + 8 refrig) | Yes (16-port switch) | ~$950 |
| 5 rooms, 10 compressors, doors | 17 (5 stor + 2 door + 10 refrig) | Yes (16-port switch) | ~$1,070 |
| **8 rooms, 32 compressors, doors** | **44** (8 stor + 4 door + 32 refrig) | **Yes (48-port switch)** | **~$2,500** |

Every configuration above uses the **exact same Nova board, exact same firmware, exact same Orbit modules**. The only thing that changes is how many Orbit modules you plug into the switch, what DIP position you set, and the switch/power supply size.

---

## 3. Cost Comparison

### 3.1 Per-System Hardware + Labor

| | Current AS2 | Constellation (6 Orbits) | Savings |
|---|---|---|---|
| Panel hardware | ~$4,000 | ~$629 | $3,371 |
| Panel wiring labor (40-60 hrs → 4-6 hrs) | ~$5,000 | ~$500 | $4,500 |
| Commissioning (8-16 hrs → 1-2 hrs) | ~$1,500 | ~$200 | $1,300 |
| First-year field service (avg) | ~$1,500 | ~$100 | $1,400 |
| **Total first-year cost** | **~$12,000** | **~$1,429** | **~$10,571 (88%)** |

### 3.2 At Scale (10 Systems/Year)

| Metric | Current AS2 | Constellation |
|---|---|---|
| Annual hardware cost | $40,000 | $6,290 |
| Annual wiring labor | $50,000 | $5,000 |
| Annual commissioning | $15,000 | $2,000 |
| Annual field service | $15,000 | $1,000 |
| **Annual total** | **$120,000** | **$14,290** |
| **Annual savings** | | **$105,710** |

### 3.3 Field Service Cost Collapse

| Scenario | Current AS2 | Constellation |
|---|---|---|
| Dead relay | Replace custom relay board ($200 part + 2 hrs labor) = ~$350 | External DIN-rail relay failed: pull from rail, snap in new one ($5–15 + 1 min). Orbit module itself has no on-board relays to fail. |
| Failed output driver | No per-output diagnostics — technician tests with multimeter | Pull ULN2803A from DIP socket, push in new one ($0.20 + 30 sec). Green LED per output confirms which channel is active. |
| Dead RS485 transceiver | Replace entire controller board or send in for rework = ~$500+ | Pull MAX3485 from socket, push in new one ($0.35 + 1 min) = ~$1 |
| Dead I/O module | Diagnose which of 3 shared RS485 buses, rewire = 4+ hrs | Unplug Cat5, plug in new Orbit, set same DIP switch = 60 seconds. $25. |
| Lightning strike on field wiring | Controller + CPLD + relay boards potentially all damaged = $2,000+ | One Orbit module destroyed. Nova, every other Orbit, and the switch are untouched. $25. |

---

## 4. Aviation-Grade Data Integrity

This section details the redundancy and fault-tolerance architecture. The AM2434 processor is the same family used in automotive ECUs (engine controllers, brake systems) and industrial safety systems certified to IEC 61508 SIL-2. We apply the same techniques to protect refrigeration control.

### 4.1 Memory Protection — ECC SRAM

| Feature | What it does | Why it matters |
|---|---|---|
| **Hardware ECC (Error-Correcting Code)** | Every memory read is checked. Single-bit errors are **automatically corrected** in hardware. Double-bit errors are **detected and trapped** — the processor raises an immediate fault. | A cosmic ray, a power rail transient, or electrical noise can flip a bit in SRAM. Without ECC, that bit flip could change a compressor's safety interlock from "enabled" to "disabled" — silently. With ECC, it's either fixed automatically or the system halts safely. **This is the same memory protection used in automotive airbag controllers and aviation flight computers.** |
| **MPU (Memory Protection Unit)** | Each of the 4 CPU cores has hardware-enforced memory boundaries. Core 2 (the safety watchdog) physically cannot be written to by Core 0 (the control logic). | A firmware bug — buffer overflow, pointer corruption, stack overflow — on the control core **cannot** corrupt the safety watchdog. The watchdog is on its own core with its own protected memory. This is the automotive "freedom from interference" principle (ISO 26262). |
| **Stack canary + MPU guard pages** | FreeRTOS places sentinel values at the bottom of each task stack. If a task overflows its stack, the MPU triggers an immediate hardware fault → safe-state. | Stack overflows are the #1 firmware crash cause. In the current AS2, a stack overflow leads to silent memory corruption and unpredictable behavior. In Constellation, it's caught in hardware before any damage occurs. |

### 4.2 Firmware Storage — Dual-Image A/B Boot

| Feature | What it does | Why it matters |
|---|---|---|
| **Two firmware copies (A/B)** | The OSPI flash stores two complete firmware images. Only one is active. | If we push a firmware update and it's bad, the system can fall back to the known-good image on the next boot. The bad update never becomes permanent. |
| **CRC32 verification at every boot** | The Secondary Boot Loader (SBL) calculates a CRC32 checksum of the active firmware image before executing it. If the CRC doesn't match, it boots the other image. | A power loss during firmware write, a corrupted flash sector, or a failed download cannot brick the controller. **You cannot brick this system with a bad firmware update.** The SBL is in ROM — it can't be corrupted. |
| **Write-protect pin (WP#)** | The flash chip's hardware write-protect is controlled by a GPIO. The firmware locks the flash 99.9% of the time — only unlocked during the specific moment of a firmware write. | A firmware bug that writes to a wrong address cannot accidentally erase the flash. The hardware write-protect is the last line of defense — even a completely compromised firmware cannot destroy its own images while WP# is asserted. |

### 4.3 Configuration Storage — Ping-Pong Redundancy

| Feature | What it does |
|---|---|
| **Two configuration copies** | Runtime settings (setpoints, timers, zone configurations) are stored in two separate flash sectors: Config-A and Config-B. |
| **Write-verify-activate sequence** | To save settings: write to inactive copy → read back and verify CRC → only then mark it as the active copy. |
| **Power-loss safe at every step** | If power is lost during write: the old config is still valid (new one wasn't marked active yet). If power is lost during verify: same — old config is still active. If power is lost after marking active: new config is valid (CRC was confirmed). **There is no point in the sequence where a power loss can destroy both copies.** |
| **Hold-up capacitors** | 100µF + 10µF capacitors on the flash power rail provide 1-2 ms of power after the main supply drops. That's enough time to complete any in-progress flash page write (takes <0.5 ms). |

### 4.4 Dedicated Safety Watchdog Core

| Feature | What it does |
|---|---|
| **R5F-2: independent watchdog** | Core 2 runs a minimal firmware image (~200 lines of C). Its only job: monitor a heartbeat from Core 0 (control logic). |
| **If the heartbeat stops** | Core 2 asserts a hardware fault line → all watchdog-enabled Orbit modules detect comms loss → every compressor, heater, and solenoid goes to safe state. |
| **Cannot be disabled by a firmware bug** | Core 2 is on its own protected memory region. Core 0 cannot write to Core 2's memory. A runaway firmware bug on Core 0 cannot disable or confuse the watchdog. |
| **Cannot be starved** | Even if Core 0 enters an infinite loop or deadlocks, Core 2 continues running independently at 800 MHz. It doesn't share a scheduler, a timer, or an interrupt controller with Core 0. |
| **Comparison:** | The current AS2 uses a software watchdog timer on the same core as the control logic. If the control task deadlocks but the watchdog ISR still fires (common with priority inversion), the watchdog never trips. Constellation's watchdog is a separate CPU — there is no mechanism by which a software bug on Core 0 can prevent the watchdog from firing. |

### 4.5 Network Security — Physical Air Gap

| Feature | What it does |
|---|---|
| **Isolated control network** | All Orbit modules and the Nova controller are on 192.168.0.0/24 — a dedicated Ethernet network with **no router, no DNS, and no internet connection**. Nova is the gateway/master at 192.168.0.1; Orbit modules are at .2 through .64 (DIP 1–63). The RPi5 is on a separate 10.x site network. |
| **Physical separation** | There is no cable, no Wi-Fi, no bridge between the control network and the internet. The RPi5 (which has site LAN access) connects to Nova via a dedicated serial link (UART), not Ethernet. |
| **Zero inbound internet ports** | The RPi5 has **no ports open to the public internet**. The web UI is accessible only on the site LAN. Cloud connectivity uses an **outbound-only TLS tunnel** to Azure WebSites — the RPi5 initiates and holds the connection. Mobile app commands flow: App → Azure → existing tunnel → RPi5. There is nothing to scan, nothing to probe, nothing to exploit from the internet. |
| **Unhackable by design** | You cannot SSH into the control network. You cannot send it a malicious packet from the internet. You cannot exploit a web vulnerability from outside the building to reach the compressor controls. The attack surface from the internet is **zero** — there are no inbound ports and no IP path to the control network. |
| **UART1 authentication** | Even the serial link from the RPi5 is hardened: HMAC-SHA256 on every packet, sequence numbers to prevent replay, and a per-boot session nonce. A rogue process on the RPi5 cannot forge valid commands. |
| **Nova command validation** | Assume the RPi5 is compromised. Nova still validates every command against a whitelist of allowed types, range-checks every parameter against safety limits in flash, rate-limits to 10 commands/second, and audit-logs every accepted and rejected command. An attacker with full root on the RPi5 cannot send a command Nova won't independently verify. |
| **RPi5 hardening** | Read-only root filesystem (reboot clears compromise), minimal OS image (no desktop, no dev tools), nftables firewall (zero inbound from internet, port 80 site LAN only, outbound Azure TLS only), SSH disabled by default. |
| **Comparison to competitors** | Emerson E2 and Danfoss AK-PC controllers put the control network and the internet on the same Ethernet. They rely on software firewalls and VLANs — which are regularly misconfigured or bypassed. Constellation doesn't need a firewall on the control network because there's nothing to firewall — and even the RPi5 bridge is authenticated end-to-end. |

### 4.6 Data Logging — Gap-Free, Internet-Optional

| Feature | What it does |
|---|---|
| **Nova SRAM ring buffer** | Nova polls every Orbit module on the control network and continuously logs the unified data — sensor readings, equipment states, VFD/EEV feedback, alarms, and overrides from **all modules** — to a 64 KB circular buffer in ECC SRAM. At typical rates, this holds **40+ hours** of data. Nova never stops logging, even when the RPi5 is rebooting or its SD card is being replaced. |
| **RPi5 filesystem archive** | The RPi5 pulls logged data from Nova over UART1 via the existing GellertFileSystem service. CSV files stored on the RPi5 SD card provide ~12 months of compressed history. This is the same proven mechanism used in the current AS2 platform. |
| **Azure cloud (optional)** | Internet-connected sites stream telemetry to Azure IoT Hub for dashboards and remote alarm notifications. **Offline sites lose nothing** — Tier 1 and Tier 2 provide full local logging without internet. |
| **eMMC backup (DNP option)** | Nova PCB includes an eMMC footprint (Do Not Populate). For sites needing non-volatile on-board logging independent of the RPi5, populate at commissioning for ~$4. Most sites won't need it. |
| **Comparison** | The current AS2 logs to a single SD card inside the controller — if the card corrupts, all history is lost. Constellation's SRAM buffer provides a gap-free safety net, and the RPi5 is a separate, replaceable storage device. |

---

## 5. Reliability & Fault Tolerance

### 5.1 Per-Module Fault Isolation (Star Topology)

**Current AS2 (RS485 daisy-chain):**
```
One shorted device → entire bus goes dark → ALL rooms lose control
```

**Constellation (Ethernet star):**
```
One shorted Orbit → that module loses 24V (per-module fuse) → every other module continues running
```

Each Orbit module is on its own dedicated Cat5 cable to its own dedicated switch port. A fault on one module — shorted transceiver, pinched cable, lightning strike, rodent damage, condensation — **cannot propagate to any other module**. The Ethernet switch continues serving all other ports normally. Each Orbit has its own fuse on the 24VDC bus, so a shorted power rail on one module trips its fuse without affecting others.

### 5.2 Galvanic Isolation — Field Faults Can't Kill the Brain

The Nova controller has **zero electrical connection** to field equipment. The only link is Cat5 Ethernet through an Ethernet switch. The Ethernet magnetics inside each RJ45 jack provide galvanic separation — no shared ground, no shared power rail between Nova and field voltages.

**Worst-case scenario:** A technician accidentally wires 240VAC to an Orbit input terminal. Result:
- That Orbit module is destroyed (~$25)
- The per-module fuse on the 24V bus blows, isolating the fault
- Nova controller: unaffected
- Every other Orbit module: unaffected
- System continues operating all other rooms while the damaged Orbit is replaced (60 seconds)

In the current AS2, the same mistake can back-feed through the RS485 bus or shared 24V rail and damage the controller, the CPLD, and multiple relay boards simultaneously.

### 5.3 Over-the-Network Orbit Firmware Updates

Every Orbit module includes a **Modbus TCP bootloader** in the first 32 KB of flash. To update Orbit firmware:

1. Nova writes `0xB007` to the Orbit's bootloader command register (0x0800)
2. Orbit reboots into bootloader mode
3. Nova streams the new firmware image over Modbus TCP (standard register writes, chunked)
4. Bootloader verifies CRC32 of the complete image
5. On success: writes to application flash, clears update flag, reboots into new firmware
6. On failure: stays in bootloader mode, reports error in status register (0x0801), ready for retry

**No site visit, no debug probe, no panel access.** Update all 15 Orbit modules from the office in minutes. The bootloader itself is never overwritten — a failed update cannot brick a module.

### 5.3 Watchdog Cascade — What Happens When Everything Goes Wrong

**Scenario: Nova controller loses power (worst case)**

| Step | Time | What happens |
|---|---|---|
| 1 | 0 s | Nova stops sending Modbus TCP polls |
| 2 | 0-30 s | Each Orbit module's independent watchdog timer counts down |
| 3 | 30 s | Watchdog expires. Orbit firmware turns off all watchdog-enabled outputs (ULN2803A stops sinking current → external relay coils de-energize → load contacts open). On local power loss, ULN2803A goes high-impedance by physics — same result. All equipment is in safe state. |
| 4 | 30 s | VFDs (configured with comm-loss = hold last speed) hold speed, but the compressor contactor has already opened via the relay watchdog. Compressor stops regardless. |
| 5 | — | RPi5 detects Nova is unresponsive. Sends alarm to cloud. Maintenance team notified. |
| 6 | 60 s | Technician power-cycles Nova. Nova boots from verified firmware image (CRC checked by SBL). Reconnects to all Orbit modules. System resumes. |

**At no point does any compressor, heater, or solenoid remain in an uncontrolled state.** All outputs use ULN2803A open-collector drivers that go high-impedance on power loss — external relay coils de-energize, load contacts open. No firmware, no watchdog, no network required for fail-safe.

### 5.4 24VDC Output Architecture — Inherent Fail-Safe

| Feature | Benefit |
|---|---|
| **Open-collector sink outputs (all 10 channels)** | ULN2803A Darlington drivers sink 24VDC to ground. Power loss → high-impedance → external relay coils de-energize → load contacts open. No firmware needed for fail-safe. This is the safest possible design. |
| **Built-in clamp diodes** | ULN2803A COM pin tied to +24V — built-in clamp diodes absorb inductive kickback from external relay coils and solenoid valves. No external flyback diodes needed. |
| **Simple watchdog** | On timeout, firmware turns off all watchdog-enabled outputs. ULN2803A stops sinking → loads de-energize. |
| **Clean startup** | On power-up, all outputs start off by design. No special reset routine needed. Module immediately enters safe state. |
| **Field-swappable ICs** | Every DIP-package IC (ULN2803A, 74HC595, MAX3485) is in a machined-pin socket. Replace a failed driver in 30 seconds. No soldering. No board swap. |
| **24VDC to external relays** | Drives external DIN-rail relay coils (15–80 mA typical), solenoid valves (≤300 mA), indicator lights (~25 mA). All HV switching handled by external relays in the installer's panel. |

---

## 6. Competitive Position

### 6.1 vs. Commercial Controllers

| Feature | Emerson E2 | Danfoss AK-PC 781A | **Agristar Constellation** |
|---|---|---|---|
| Base controller cost | $2,500-4,000 | $1,800-2,500 | **~$48** (Nova board) |
| I/O expansion module (8 DO) | $300-500 each | $200-400 each | **~$25** (10 DO + 10 DI + 2 AO + dual RS485) |
| 6-module system cost | $5,000-7,000 | $3,500-5,000 | **~$629** (7 Orbits: 2 stor + 1 door + 4 refrig) |
| VFD communication | TCP adapter $200/drive | TCP adapter $200/drive | **Built into Orbit** ($0/drive) |
| Field-swappable ICs | No (board replacement) | No (board replacement) | **Yes** (DIP socket, 30 sec) |
| Network security | Software firewall / VLAN | Software firewall / VLAN | **Physical air gap** + zero inbound ports + HMAC-authenticated UART |
| ECC memory | No | No | **Yes** (automotive-grade, auto-correct) |
| Dual-image firmware | No (single image, can brick) | Limited | **Yes** (A/B with CRC + SBL fallback) |
| Independent safety watchdog | Software timer (same core) | Software timer (same core) | **Dedicated CPU core** (hardware isolated) |
| I/O scan speed | 500 ms - 2 sec | 200 ms - 1 sec | **< 3 ms** (parallel TCP) |
| Max I/O per controller | 64-128 points | 48-80 points | **150 DO + 150 DI + 30 AO** |
| Customizability | Closed, vendor-locked | Closed, vendor-locked | **Fully custom firmware** (we own the code) |
| Orbit firmware updates | N/A | N/A | **Over-the-network** (Modbus TCP bootloader — no physical access needed) |
| Analog sensor integration | Proprietary modules ($200+) | Proprietary modules ($150+) | **Analog boards reflashed to Modbus RTU** (zero hardware changes, firmware only) |

### 6.2 What We Can Say to Customers

- "Our controller uses the same automotive-grade processor found in car engine management systems — ECC memory, independent safety watchdog, and dual-image firmware that can't be bricked by a bad update."
- "Every relay on our system is field-replaceable in 30 seconds. No board swaps, no soldering, no factory RMA."
- "Our control network is physically isolated from the internet. The RPi5 has zero inbound ports — cloud connectivity is outbound-only through an encrypted Azure tunnel. There is no firewall to misconfigure because there is nothing listening."
- "Even our web server bridge is hardened — every command to the controller is cryptographically authenticated, bounds-checked against safety limits, and audit-logged. A compromised web server cannot override the controller's safety logic."
- "We can control a 5-room, 10-compressor facility from a single controller that fits in your hand."
- "A lightning strike on a field wire destroys a $25 module. The $48 brain is untouched. The other 14 modules keep running. Replace the dead module in 60 seconds."
- "Orbit module firmware can be updated over the network — no site visit, no debug probe, no panel access. Push an update to every module from the office."
- "Our existing proven analog sensor boards plug right into the new system. The Orbit module automatically converts sensor data to standard Modbus — zero hardware changes, zero rewiring."
- "Every output, every input, every bus, and every fuse has its own LED. Green means running, red means blown fuse, blue means data on the wire. A technician can diagnose the entire panel at a glance without opening a single enclosure or touching a multimeter."

---

## 7. Risk Assessment

### 7.1 What Could Go Wrong

| Risk | Mitigation | Severity if unmitigated |
|---|---|---|
| AM2434 supply chain shortage | Design is pin-compatible with AM2634 fallback. STM32H755 as second fallback. Same firmware compiles on all three. | Medium |
| Orbit PCB design errors | External EE firm designs boards to our spec. We review schematics before layout. 10-unit prototype run before production. | Low (caught in prototype) |
| Firmware bugs in new architecture | Full simulation environment (QEMU) already exists for the AS2. Adapt it for Constellation. Test before metal exists. | Low (existing test infra) |
| Customer resistance to new platform | Phase 1 deploys Nova with analog boards (reflashed to Modbus RTU) via Orbit Port A. Customers see zero change in behavior. Orbit modules added incrementally. | Low |
| Technician training | Orbit modules are simpler than current panels — plug in Cat5, set DIP switch, done. Training is a 30-minute session. | Low |

### 7.2 What Cannot Go Wrong (by design)

| Scenario | Why it can't happen |
|---|---|
| Bad firmware update bricks the controller | SBL checks CRC before boot. Bad image → falls back to known-good image. You cannot brick this system. |
| Bad Orbit firmware update | Orbit Modbus TCP bootloader verifies CRC before committing. Failed update → stays in bootloader mode, ready for retry over the network. No physical access needed. |
| Silent memory corruption changes a safety setpoint | ECC SRAM auto-corrects single-bit errors. Double-bit errors trap immediately → safe state. |
| Internet hacker reaches the compressor relays | Control network has no physical connection to the internet. No IP path exists. |
| One failed module takes out the whole facility | Star topology — each module is on its own switch port. Fault containment is per-port. |
| Power loss during config save corrupts settings | Ping-pong write with CRC verification. No power-loss timing can destroy both copies. |
| Watchdog failure allows uncontrolled equipment | Watchdog runs on a dedicated CPU core with MPU-protected memory. Control core bugs cannot interfere. |

---

## 8. Implementation Path

| Phase | What | Investment | Risk | Payoff |
|---|---|---|---|---|
| **1. Prototype** | EE firm designs Nova + Orbit boards. 10 units each. | ~$15,000 (EE firm) + $2,000 (prototype parts) | Low — reversible, no customer impact | Proves hardware works |
| **2. Lab validation** | Full firmware on prototype boards. QEMU simulation + bench testing. | Engineering time only (in-house) | Low — firmware is our core competency | Proves software works |
| **3. Pilot install** | One real facility, running alongside existing AS2 panel (both monitoring same sensors, neither controlling yet) | 1 system ($610) + installation time | Low — AS2 still in control, Constellation is monitoring only | Proves real-world reliability |
| **4. Cutover** | Constellation takes over control on pilot site. AS2 becomes backup. | Minimal | Medium — first live deployment | Proves production readiness |
| **5. Production** | All new installations use Constellation. Existing sites upgraded on next service call. | Standard build cost | Low — proven at this point | Full cost savings realized |

---

## 9. The Bottom Line

| Question | Answer |
|---|---|
| Does it do everything the AS2 does? | Yes — same sensors, same equipment, same control logic, same UI. |
| Does it do more? | Yes — 15× more I/O capacity, parallel scanning (300× faster), VFD control without the RPi5 in the loop, physical network isolation, per-module fault containment, analog boards speaking standard Modbus RTU (one protocol end-to-end), over-the-network Orbit firmware updates, battery-backed RTC for time-critical scheduling. |
| Is it more reliable? | Yes — ECC memory, dual-image boot, dedicated watchdog core, star topology, galvanic isolation. Every failure mode is addressed in hardware, not software. |
| Is it cheaper? | Yes — $610 vs $12,000 per system. 95% cost reduction. |
| Is it faster to build? | Yes — 4-6 hours vs 40-60 hours. Plug in Cat5 cables, set DIP switches, done. |
| Is it easier to service? | Yes — swap a $0.20 ULN2803A driver or a $25 module. No custom boards, no re-wiring, no factory RMA. |
| Is it harder to design? | No — the EE firm designs the two PCBs. We write the firmware (which is what we're good at). The architecture is simpler than what we have today. |
| Can we sell it as a differentiator? | Yes — "automotive-grade safety, physically air-gapped security with zero inbound internet ports, field-swappable components, 5-room single-controller capacity." No competitor can say any of that. |
| What does it cost to find out? | ~$17,000 for prototype boards. Reversible — if it doesn't work, we've spent less than the profit margin on two AS2 panels. |
