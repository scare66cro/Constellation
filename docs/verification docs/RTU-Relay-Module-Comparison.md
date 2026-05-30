# Modbus RTU Relay Module Comparison & Selection Guide

**Date:** March 21, 2026  
**Context:** Module selection for Agristar RTU migration — replacing CPLD shift register outputs and expansion boards  
**Related:** [RTU-Migration-Guide.md](RTU-Migration-Guide.md), [VFD-Modbus-RTU-Architecture-Proposal.md](VFD-Modbus-RTU-Architecture-Proposal.md)

---

## 1. What to Look For

An RTU relay module for agricultural storage building control needs:

- **Relay outputs** (8 minimum) rated for contactor coil switching (10A 250VAC)
- **Optoisolated digital inputs** (8 minimum) for field sensors, switches, interlocks
- **Per-channel LED indicators** for visual confirmation during commissioning and troubleshooting
- **DIP switch addressing** for zero-configuration field replacement
- **RS485 surge protection** (TVS diodes) — essential near VFD drives and motor contactors
- **Wide temperature range** — storage building control enclosures may not be climate-controlled
- **DIN rail mount** — standard industrial panel mounting
- **Standard Modbus RTU register map** — FC01 (read coils), FC02 (read DI), FC05 (write coil), FC0F (write multiple coils)

---

## 2. Tier 1 — Industrial Rated (Hardened, Certified)

These are full industrial I/O modules from major automation vendors. UL/CSA/ATEX certified, designed for the harshest environments. Significantly more expensive and generally overkill for agricultural storage building HVAC, but required if customer specifications mandate UL listing.

### Schneider Electric Zelio SR3 XT

- **Part:** SR3B261FU (base) + SR3MBU01BD (Modbus comm module)
- **Outputs:** 10 relay (8A 250VAC)
- **Inputs:** 16 digital + 2 analog (0–10V)
- **Temp range:** -20°C to +55°C
- **Addressing:** Software configuration via Zelio Soft
- **Certifications:** CE, UL, CSA
- **Features:** Built-in real-time clock, data logging, ladder logic engine
- **Price:** ~$300–400 (base + comm module)
- **Notes:** More of a micro-PLC than a simple relay module. Brings its own logic engine which is unnecessary — our ARM handles all control logic. Best suited for customers who require North American UL listing.

### Siemens LOGO! 8

- **Part:** 6ED1052-1MD08-0BA1 + CSM 1277 comm module
- **Outputs:** 8 relay (10A 250VAC)
- **Inputs:** 8 digital + 4 analog
- **Temp range:** -20°C to +55°C
- **Addressing:** Software via LOGO! Soft Comfort
- **Certifications:** CE, UL
- **Features:** Web server, data logging, built-in display, cloud connectivity
- **Price:** ~$250–350
- **Notes:** Similar to Zelio — a micro-PLC with Modbus capability. Modbus TCP native, requires add-on module for RTU. Overkill for our use case.

### Phoenix Contact Inline I/O

- **Part:** IB IL 24 DO 8 (2861234) + IL RS 485 bus coupler
- **Outputs:** 8 relay (2A — designed for contactor coil driving)
- **Inputs:** Separate module (IB IL 24 DI 8)
- **Temp range:** -25°C to +55°C
- **Addressing:** Bus coupler configuration
- **Certifications:** CE, UL, ATEX (hazardous area)
- **Features:** Relay cycle counter, board temperature monitor, vibration rated
- **Price:** ~$150 (DO module) + $200 (bus coupler) + $100 (DI module) = ~$450
- **Notes:** True industrial I/O designed for harsh environments. The 2A relay rating is intentional — designed to drive contactor coils, not switch loads directly. ATEX rating for hazardous areas. Modular: buy only the I/O types you need.

---

## 3. Tier 2 — Robust Commercial (Recommended)

Well-documented, wide temperature range, surge protection, readily available. The right balance of reliability and cost for greenhouse control.

### Waveshare Industrial 8-Channel (SKU 23406) — PRIMARY RECOMMENDATION

- **Part:** Waveshare 23406
- **Outputs:** 8 relay (10A 250VAC NO/NC each)
- **Inputs:** 8 optoisolated digital inputs (dry contact or wet, configurable)
- **Temp range:** -40°C to +85°C
- **Addressing:** 5-position DIP switch (addresses 1–31)
- **RS485 protection:** TVS diodes on A/B lines
- **Power supply:** 12V DC (model 23406) or 24V DC (model 23407)
- **LED indicators:** Per-relay + per-input status LEDs
- **Baud rates:** 4800, 9600, 19200, 38400, 57600, 115200 (configurable via holding register)
- **Default config:** 9600 baud, 8N1, address 1
- **DIN rail:** Yes
- **Certifications:** CE
- **Price:** ~$30–35
- **Availability:** Amazon, Waveshare direct, AliExpress, Mouser

**Register map:**

| Register Type | Address Range | Function Code | Description |
|---|---|---|---|
| Coils | 0x0000–0x0007 | FC01 (read), FC05 (write one), FC0F (write all) | Relay outputs 1–8 |
| Discrete Inputs | 0x0000–0x0007 | FC02 (read) | Optoisolated inputs 1–8 |
| Holding Register | 0x0000 | FC03/FC06 | Device address (1–247) |
| Holding Register | 0x0001 | FC03/FC06 | Baud rate setting |
| Holding Register | 0x0002 | FC03 (read-only) | Software version |

**Why this is the primary recommendation:**
- Best English documentation (PDF manual with register map, wiring diagrams, timing specs, example code)
- Largest community — firmware examples for Arduino, STM32, ESP32, Raspberry Pi all available
- DIP switch addressing — zero configuration, field-replaceable by any technician
- -40°C to +85°C covers unheated greenhouse enclosures in all climates
- $30 — cheap enough to stock 2–3 spares per installation
- Available on Amazon for next-day replacement, not just AliExpress

### Waveshare Industrial 4-Channel (SKU 17532)

- **Part:** Waveshare 17532
- **Outputs:** 4 relay (10A 250VAC)
- **Inputs:** 4 optoisolated digital inputs
- **All other specs identical to 23406**
- **Price:** ~$20–25
- **Notes:** Smaller footprint for installations that need fewer channels. Same register map, just fewer addresses.

### EBYTE MA01-AACX0880

- **Part:** EBYTE MA01-AACX0880
- **Outputs:** 8 relay (**16A 250VAC** — highest rating in this class)
- **Inputs:** 8 optoisolated digital inputs
- **Temp range:** -40°C to +85°C
- **Addressing:** Holding register write (software configured, default address 1)
- **RS485 protection:** TVS diodes on A/B lines
- **Reverse polarity protection:** Yes (on power input)
- **Power supply:** 7–30V DC (wide input range)
- **LED indicators:** Per-relay + per-input status LEDs
- **Board voltage monitor:** Yes (readable via holding register)
- **DIN rail:** Yes
- **Certifications:** CE
- **Price:** ~$40–50
- **Availability:** AliExpress, EBYTE direct

**Why consider this:**
- 16A relay contacts can switch larger loads directly without an intermediate contactor
- Wide 7–30V power input — works with whatever supply voltage is available in the panel
- Reverse polarity protection prevents damage from wiring mistakes
- Board voltage monitoring detects brown-out conditions before relays drop out

**Drawback:** Software-configured addressing requires connecting one module at a time to set the address via a Modbus holding register write. Slightly more work during commissioning vs DIP switches, but only done once.

### Tongke / USR-IO808

- **Part:** USR-IO808 or Tongke TK-RMD0808
- **Outputs:** 8 relay (10A 250VAC)
- **Inputs:** 8 optoisolated digital inputs
- **Temp range:** -40°C to +70°C
- **Addressing:** DIP switch + software configurable (both options)
- **RS485 protection:** Basic (no TVS)
- **Power supply:** 12V or 24V DC
- **LED indicators:** Per-relay + per-input
- **Communication watchdog:** Yes — auto-resets all relays to safe state if no valid Modbus communication within configurable timeout
- **DIN rail:** Yes
- **Certifications:** CE
- **Price:** ~$25–35
- **Availability:** AliExpress, USR direct

**Why consider this:**
- Built-in communication watchdog is a unique safety feature. If the ARM stops talking (crash, bus break, power loss), the relay module automatically turns off all outputs after a configurable timeout (e.g. 5 seconds). This provides defense-in-depth beyond the ARM firmware's own safety checks.

**Drawback:** Narrower temperature range (-40 to +70 vs +85), basic RS485 protection (no TVS diodes — consider adding external TVS if wiring runs near VFDs).

---

## 4. Tier 3 — Budget (Functional, Fewer Protections)

### HF HF46F Series / Generic Chinese Modules

- **Outputs:** 4 or 8 relay (10A)
- **Inputs:** Some models include DI, some don't
- **Temp range:** 0°C to +50°C (narrow)
- **Addressing:** DIP switch (basic models) or software
- **RS485 protection:** Minimal or none
- **LED indicators:** Basic (power + RS485 activity only, not per-channel)
- **Documentation:** Often poor, Chinese-only, or incomplete register maps
- **Price:** ~$12–20
- **Availability:** AliExpress

**Not recommended for production use.** The narrow temperature range, missing surge protection, and poor documentation make these unsuitable for field installations where reliability matters. Fine for bench testing or prototyping.

---

## 5. Feature Comparison Matrix

| Feature | Waveshare 23406 | EBYTE MA01 | Tongke IO808 | Phoenix Contact | Schneider Zelio |
|---|---|---|---|---|---|
| **Relay channels** | 8 | 8 | 8 | 8 | 10 |
| **Relay rating** | 10A 250VAC | **16A 250VAC** | 10A 250VAC | 2A (coil driver) | 8A 250VAC |
| **Digital inputs** | 8 optoisolated | 8 optoisolated | 8 optoisolated | Separate module | 16 |
| **Analog inputs** | None | None | None | Separate module | 2 (0–10V) |
| **Temp range** | -40 to +85°C | -40 to +85°C | -40 to +70°C | -25 to +55°C | -20 to +55°C |
| **RS485 surge protection** | TVS diodes | TVS diodes | Basic | Industrial grade | Industrial grade |
| **Reverse polarity protection** | No | **Yes** | No | Yes | Yes |
| **Addressing method** | DIP switch | Holding register | DIP + software | Bus coupler | Software |
| **Comm watchdog** | No | No | **Yes** | Yes | Yes |
| **Relay state readback** | FC01 | FC01 | FC01 | FC01 | FC01 |
| **DI state readback** | FC02 | FC02 | FC02 | FC02 | FC02 |
| **Relay cycle counter** | No | No | No | **Yes** | No |
| **Board voltage monitor** | No | **Yes** | No | Yes | Yes |
| **Board temp monitor** | No | No | No | **Yes** | No |
| **Per-channel LED** | Relay + DI | Relay + DI | Relay + DI | Relay only | Relay only |
| **DIN rail mount** | Yes | Yes | Yes | Yes | Yes |
| **Certifications** | CE | CE | CE | **CE/UL/ATEX** | **CE/UL/CSA** |
| **English documentation** | **Excellent** | Good | Decent | Excellent | Excellent |
| **Price** | ~$30 | ~$45 | ~$30 | ~$450 | ~$350 |
| **Replacement availability** | **Amazon (next-day)** | AliExpress (1–3 weeks) | AliExpress (1–3 weeks) | Mouser/Digi-Key (2–5 days) | Mouser/Digi-Key (2–5 days) |

---

## 6. Key Feature Differences Explained

### Relay Rating (2A vs 10A vs 16A)

- **2A (Phoenix Contact):** Designed to drive contactor coils only. The contactor handles the actual load. This is correct industrial practice for loads over a few amps — the relay module controls the contactor, the contactor controls the motor/heater.
- **10A (Waveshare, Tongke):** Can directly switch loads up to ~2.4kW at 240VAC. Suitable for small solenoid valves, lighting circuits, alarm beacons, and small contactor coils.
- **16A (EBYTE):** Can directly switch loads up to ~3.8kW at 240VAC. Can handle larger contactors and some heating elements directly.

**Recommendation:** Use 10A modules and drive larger loads through contactors. Don't rely on relay module contacts for continuous high-current loads — the contacts will wear faster and eventually weld.

### Communication Watchdog

The Tongke IO808 has a configurable communication watchdog:
- If no valid Modbus frame is received within X seconds (configurable), the module automatically sets all relay outputs to OFF
- This is defense-in-depth: if the ARM crashes, the bus breaks, or the transceiver fails, outputs go to a defined safe state
- Without this feature, relays stay in their last commanded state indefinitely

The ARM firmware's own safety checks (`safety_check()` in the poll task) already handle this — but having it on the module too means safety does not depend on any single point of failure.

**Recommendation:** If you don't use the Tongke, implement a firmware-side watchdog where the ARM periodically writes a "keepalive" coil. If the ARM crashes mid-write, the relay module's timeout (on models that support it) provides a backup. For modules without built-in watchdog, the ARM firmware simply needs to handle its own restart gracefully — which it already does via the FreeRTOS watchdog.

### Optoisolation on Digital Inputs

All Tier 2 modules have optoisolated inputs, meaning:
- The input circuit is electrically isolated from the RS485 bus and module logic (typically 2.5kV isolation)
- A wiring fault on a field sensor won't damage the module or corrupt the bus
- Ground loops between the module and distant sensors are eliminated

This matters in greenhouse environments where sensor wiring may run alongside power cables, through wet conduit, or near VFD-driven motors.

### RS485 Surge Protection

TVS (Transient Voltage Suppressor) diodes on the A/B lines clamp voltage spikes to safe levels. Sources of spikes in greenhouse installations:
- VFD switching noise on nearby power cables
- Contactor coil back-EMF (even with snubbers, some energy couples to signal wiring)
- Lightning-induced surges on long cable runs
- Static discharge from equipment or personnel

Waveshare and EBYTE include TVS protection. Tongke has basic protection only. For modules without built-in TVS, add external TVS diodes (e.g. SMBJ6.5CA, ~$0.10 each) at the ARM board end of the bus.

### Power Supply Range

- **Fixed voltage (12V or 24V):** Waveshare, Tongke — must match the panel supply voltage. Order the correct model.
- **Wide range (7–30V):** EBYTE — works with any DC supply available in the panel. Convenient for retrofit installations or panels with non-standard supply voltages.

### Addressing Method

- **DIP switch:** Waveshare, Tongke — set the address physically, no tools needed. A field technician can replace a module by copying the switch positions. Limited to 31 addresses (5 switches) but more than sufficient for any single bus.
- **Software/holding register:** EBYTE — set address by writing to holding register 0x0001. Requires a Modbus tool or the ARM firmware to configure. No address limit (1–247). Slightly more work during initial commissioning, but the address is stored in non-volatile memory and survives power cycles.

---

## 7. Recommended Configuration for Agristar

### Standard Installation (4–8 outputs, 4–8 inputs)

| Qty | Part | Purpose | Cost |
|---|---|---|---|
| 1 | Waveshare 23406 (8-ch) | All equipment outputs + field sensor inputs | $30 |
| 1 | Waveshare 23406 (spare) | Stock at site for field replacement | $30 |
| **Total** | | | **$60** |

DIP switch settings: Address 10, matching the bus addressing plan in the migration guide.

### Large Installation (16+ outputs)

| Qty | Part | Purpose | Cost |
|---|---|---|---|
| 2 | Waveshare 23406 (8-ch) | 16 outputs + 16 inputs across 2 modules | $60 |
| 1 | Waveshare 23406 (spare) | Stock at site | $30 |
| **Total** | | | **$90** |

DIP switch settings: Module 1 at address 10, Module 2 at address 11.

### Compared to current expansion board approach

| | Current (Expansion Boards) | RTU Relay Modules |
|---|---|---|
| 8 extra outputs | 1 expansion board (custom PCB, custom CPLD, custom firmware) | 1 Waveshare 23406 ($30, off-the-shelf) |
| 16 extra outputs | 2 expansion boards | 2 Waveshare 23406 ($60) |
| Lead time for replacement | Manufacture + program custom board | Amazon next-day delivery |
| Inventory cost | Must stock specific custom PCBs | Generic $30 modules, interchangeable |
| Field repair skill | Board-level electronics knowledge | Set DIP switches, connect 2 wires |

---

## 8. UL-Certified Mid-Range Options

The gap between $30 (CE only) and $350+ (Schneider/Phoenix) isn't as wide as it looks. These industrial brands carry UL listing at moderate prices.

### Advantech ADAM-4068 — Best Value UL Option

- **Part:** Advantech ADAM-4068
- **Outputs:** 8 relay (5A 250VAC NO)
- **Inputs:** None (pair with ADAM-4051 for 16 DI)
- **Protocol:** Modbus RTU over RS485
- **Temp range:** -10°C to +70°C
- **Certifications:** **UL**, CE, FCC
- **Addressing:** Software configurable via ADAM utility
- **Surge protection:** 2kV on RS485 lines
- **DIN rail:** Yes
- **Price:** ~$120–150
- **Availability:** Mouser, Digi-Key, Newark (2–5 day shipping)

Pair with **ADAM-4051** (16 DI, UL, ~$90) for discrete inputs on the same bus.

**Total for 8 relay + 16 DI: ~$210–240** — about half the Phoenix Contact price.

### Advantech ADAM-4060

- **Part:** Advantech ADAM-4060
- **Outputs:** 4 relay (5A 250VAC)
- Same specs as 4068, fewer channels
- **UL listed**
- **Price:** ~$90–110

### ICP DAS M-7065

- **Part:** ICP DAS M-7065
- **Outputs:** 5 relay (5A 250VAC)
- **Inputs:** 4 optoisolated DI
- **Protocol:** Modbus RTU over RS485
- **Temp range:** -25°C to +75°C
- **Certifications:** **UL**, CE
- **Power over RS485 bus:** Yes (no separate power supply needed if bus supplies power)
- **DIN rail:** Yes
- **Price:** ~$100–130
- **Availability:** Mouser, ICP DAS distributors

### ICP DAS M-7068

- **Part:** ICP DAS M-7068
- **Outputs:** 8 relay (5A)
- Same platform as M-7065
- **UL listed**
- **Price:** ~$130–160

### UL-Certified Price Comparison

| Module | Relays | DI | UL | Price |
|---|---|---|---|---|
| Advantech ADAM-4068 | 8 | 0 | Yes | ~$130 |
| Advantech ADAM-4068 + ADAM-4051 | 8 | 16 | Yes | ~$220 |
| ICP DAS M-7065 | 5 | 4 | Yes | ~$115 |
| ICP DAS M-7068 | 8 | 0 | Yes | ~$145 |
| Phoenix Contact (IL DO 8 + coupler) | 8 | 0 | Yes | ~$450 |
| Schneider Zelio + comm module | 10 | 16 | Yes | ~$350 |
| **Waveshare 23406 (no UL)** | **8** | **8** | **No** | **~$30** |

**Note on 5A relay rating:** The Advantech and ICP DAS modules use 5A contacts rather than 10A. This is appropriate — they are designed to switch contactor coils (which draw well under 1A), not to switch heavy loads directly. The contactor handles the power; the relay module handles the control signal.

---

## 9. 3-Phase Motor Control — RTU Contactors (UL, 30A+)

For 3-phase motor control (fan motors, compressors, large heater banks), there are two approaches: integrated RTU contactors or RTU relay modules driving standard contactors.

### Approach 1: Integrated RTU Motor Contactors

These combine the contactor and Modbus RTU communication in one device or a contactor + plug-in communication module.

#### Schneider Electric TeSys D + LULC033

- **Contactor:** LC1D32 (32A, 3-pole, 600VAC rated)
- **Communication module:** LULC033 (Modbus RTU RS485, plugs into contactor)
- **Certifications:** **UL 508**, CSA, CE, IEC
- **Temp range:** -25°C to +60°C
- **Modbus features:**
  - Coil register: start/stop motor
  - Holding registers: real-time current (all 3 phases), last fault code, motor runtime hours, thermal state
  - Discrete input: overload trip status
- **Price:** ~$80 (LC1D32) + ~$120 (LULC033) = **~$200 per motor**
- **Availability:** Mouser, Digi-Key, Grainger, AutomationDirect

#### Schneider Electric TeSys D Full Motor Starter (LUB/LUCM)

- **Part:** LUB32 (starter base, 32A) + LUCM (Modbus comm module)
- **What it is:** Complete motor protection + control in one DIN-rail unit:
  - 3-phase contactor
  - Thermal overload relay
  - Short-circuit protection
  - Modbus RTU communication
- **Certifications:** **UL 508**, CSA, CE, IEC
- **Modbus features:**
  - Start/stop/jog control
  - 3-phase current monitoring
  - Thermal state (% of trip threshold)
  - Trip cause history (overload, phase loss, locked rotor)
  - Operating hours counter
  - Reset overload remotely via Modbus
- **Price:** ~$250–350 complete
- **Notes:** Replaces separate contactor + overload relay + current transformer. One device, one bus address, full motor protection with remote diagnostics.

#### ABB AF Series + Ekip Com Modbus

- **Contactor:** AF26-30-00 (26A) or AF38-30-00 (38A), 3-pole, 600VAC
- **Communication:** ABB Ekip Com Modbus RS485 module
- **Certifications:** **UL**, CSA, CE
- **Price:** ~$60 (contactor) + ~$150 (comm module) = **~$210**
- **Notes:** ABB ecosystem — same vendor as the ACS380 VFD drives already in the system.

### Approach 2: RTU Relay Module + Standard 3-Phase Contactor (Recommended)

Simpler, cheaper, and more flexible. The RTU relay module switches the contactor coil; the contactor handles the 3-phase power.

```
ARM (UART3)
  ↓ RS485 Modbus RTU
RTU Relay Module (e.g. Advantech ADAM-4068, UL)
  ↓ Relay contact (switches 24VDC contactor coil)
3-Phase Contactor (e.g. Schneider LC1D32, UL 508)
  ↓ 3-phase power contacts (32A per pole)
Motor / Heater Bank / Compressor
```

#### UL-Listed 3-Phase Contactors (Standard, No Communication)

| Part | Manufacturer | Rating | Coil | UL | Price |
|---|---|---|---|---|---|
| **LC1D32BL** | Schneider Electric | 32A, 3-pole, 600VAC | 24VDC | UL 508 | ~$50–70 |
| **LC1D38BL** | Schneider Electric | 38A, 3-pole, 600VAC | 24VDC | UL 508 | ~$60–80 |
| **AF26-30-00-11** | ABB | 26A, 3-pole, 600VAC | 24VDC | UL | ~$45–60 |
| **AF38-30-00-11** | ABB | 38A, 3-pole, 600VAC | 24VDC | UL | ~$55–75 |
| **XTCE032C10A** | Eaton | 32A, 3-pole, 600VAC | 24VDC | UL 508 | ~$40–55 |
| **XTCE040C10A** | Eaton | 40A, 3-pole, 600VAC | 24VDC | UL 508 | ~$50–65 |
| **3RT2026-1BB40** | Siemens | 25A, 3-pole, 600VAC | 24VDC | UL | ~$50–65 |
| **3RT2028-1BB40** | Siemens | 38A, 3-pole, 600VAC | 24VDC | UL | ~$60–80 |

All of these have 24VDC coils drawing well under 1A — easily switched by any relay module.

#### Wiring Diagram

```
                RTU Relay Module                    3-Phase Contactor
                (Advantech ADAM-4068)                (Schneider LC1D32)
                ┌──────────────┐                    ┌──────────────┐
    RS485 A ───→│ A            │                    │              │
    RS485 B ───→│ B            │                    │  L1 ──→ T1  │──→ Motor U
                │              │                    │  L2 ──→ T2  │──→ Motor V
    24VDC + ───→│ V+           │  Relay 1 COM ─┐   │  L3 ──→ T3  │──→ Motor W
    24VDC - ───→│ V-           │  Relay 1 NO ──┤   │              │
                │              │               │   │  A1 ←── 24V+ │←── From Relay NO
                │  Coil 0 ───→│ Relay 1       │   │  A2 ←── 24V- │←── Common GND
                │              │               │   │              │
                └──────────────┘               │   │  13 ──→ 14   │──→ Aux contact (to DI
                                               │   │  (NO aux)    │    for run confirmation)
                                    24VDC+ ────┘   └──────────────┘
```

The contactor's auxiliary contact (terminals 13/14) can be wired back to a discrete input on the relay module to confirm the contactor actually pulled in — verifying the motor circuit is energized, not just that the relay was commanded.

### Cost Comparison — 3-Motor Installation

| Approach | Motor 1 | Motor 2 | Motor 3 | Total | UL |
|---|---|---|---|---|---|
| **Integrated TeSys D + LULC033** | $200 | $200 | $200 | **$600** | UL 508 |
| **Integrated LUB32 + LUCM (full starter)** | $300 | $300 | $300 | **$900** | UL 508 |
| **ADAM-4068 + 3× LC1D32** | $130 + $60 | $60 | $60 | **$310** | UL + UL 508 |
| **ADAM-4068 + 3× Eaton XTCE032C** | $130 + $45 | $45 | $45 | **$265** | UL + UL 508 |
| **Waveshare 23406 + 3× LC1D32 (no UL on module)** | $30 + $60 | $60 | $60 | **$210** | CE + UL 508 |

With Approach 2, the relay module handles up to 8 contactors. Each additional motor only costs the contactor ($45–70), not another communication module.

### When to Choose Integrated vs Separate

| Use Case | Recommendation |
|---|---|
| Need per-motor current monitoring via Modbus | Integrated (TeSys D + LULC033) |
| Need motor protection (overload/short-circuit) via Modbus | Integrated (LUB32 + LUCM) |
| Just need start/stop control, no motor diagnostics | **Separate (relay module + contactor)** |
| Multiple motors sharing one RTU bus address | **Separate** — one relay module, multiple contactors |
| Budget-constrained | **Separate** — cheapest per motor |
| Customer requires per-motor runtime logging | Integrated — built-in hour counters |

**For most Agristar installations:** Approach 2 (separate) is the right choice. Fan motors, heater contactors, and vent actuators are simple on/off loads that don't require per-motor current monitoring. If motor diagnostics are needed for a specific installation, swap in an integrated unit at that motor position — the ARM firmware treats it as another Modbus device on the same bus with holding registers for current/fault data.

---

## 10. Firmware Compatibility

All modules listed — relay modules, UL mid-range, and integrated motor contactors — use the same standard Modbus RTU register map for coils and discrete inputs. The ARM firmware's Modbus RTU master code works with any of them without modification:

- **FC01** (Read Coils) → relay state readback
- **FC02** (Read Discrete Inputs) → field sensor/switch state
- **FC05** (Write Single Coil) → set/clear one relay or start/stop one motor
- **FC0F** (Write Multiple Coils) → set/clear all relays in one transaction
- **FC03** (Read Holding Registers) → read current, fault code, runtime hours (integrated contactors)

If you start with Waveshare and later decide to switch to Advantech (for UL) or Phoenix Contact (for ATEX), the firmware does not change. If you add an integrated TeSys D motor contactor to the bus, the firmware just adds its unit ID to the poll list and reads its holding registers alongside the relay modules. One bus, one protocol, interchangeable devices.

---

## 11. Panel Shop Strategy — UL 508A vs Non-UL Builds

**Agristar is already UL 508A certified.** The panel shop holds an active UL 508A listing and can label panels in-house without third-party field evaluation. This means UL orders are a material/documentation choice, not an engineering or certification hurdle.

The RTU architecture makes this even simpler: the same ARM board, same firmware, same UI, and same wiring topology for both UL 508A panels and standard (non-UL) panels. The only variable is which commodity modules and contactors the panel shop pulls from the shelf.

### One Firmware, Two BOMs

| Panel Type | RTU Relay Module | Contactors | I/O Cost (8 outputs, 3 motors) |
|---|---|---|---|
| **UL 508 panel** | Advantech ADAM-4068 (~$130, UL) | Schneider LC1D32 (~$60 ea, UL 508) | ~$310 |
| **Non-UL panel** | Waveshare 23406 (~$30, CE) | Eaton XTCE032C or equiv (~$45 ea) | ~$165 |

Same ARM board. Same firmware binary. Same UI. Same RS485 bus. Same commissioning procedure. The panel shop workflow is:

```
Sales order received
  ↓
UL 508 required?
  │
  ├── YES → Pick from UL BOM shelf:
  │           • Advantech ADAM-4068 (relay module)
  │           • Advantech ADAM-4051 (DI module, if needed)
  │           • Schneider LC1D32BL contactors
  │           • UL-listed enclosure, wire, terminal blocks
  │           • Apply UL 508A label
  │
  └── NO ─→ Pick from standard BOM shelf:
              • Waveshare 23406 (relay module)
              • Standard contactors (Eaton, ABB, etc.)
              • Standard enclosure, wire, terminals
  ↓
Same wiring diagram (only part numbers differ)
Same DIP switch / address settings
Same ARM board + same firmware flash
Same commissioning procedure
Same field service procedure
```

### Panel Shop Inventory

**UL shelf:**

| Part | Qty to Stock | Unit Cost | Purpose |
|---|---|---|---|
| Advantech ADAM-4068 | 3 | $130 | RTU relay module (UL) |
| Advantech ADAM-4051 | 2 | $90 | RTU DI module (UL, if needed) |
| Schneider LC1D32BL | 6 | $60 | 3-phase contactor, 32A (UL 508) |
| Schneider LC1D38BL | 3 | $70 | 3-phase contactor, 38A (UL 508) |

**Standard shelf:**

| Part | Qty to Stock | Unit Cost | Purpose |
|---|---|---|---|
| Waveshare 23406 | 5 | $30 | RTU relay module (CE) |
| Waveshare 23406 (spares) | 2 | $30 | Customer site spares |
| Eaton XTCE032C or equiv | 6 | $45 | 3-phase contactor, 32A |

### Why This Approach Works

- **No firmware variants** — one binary image for all panel types, no conditional compilation, no configuration flags for UL vs non-UL
- **No UI variants** — one Svelte app for all panels. The UI doesn't know or care about UL status.
- **No wiring diagram variants** — same topology, same terminal labels, same bus addressing. Only part numbers on the BOM change.
- **No training variants** — panel builders and field technicians learn one system. The UL/non-UL decision is a purchasing decision, not an engineering decision.
- **Mixed-certification installations** — if a customer needs UL on motor circuits but not on low-voltage I/O, use a UL contactor on the motors and a Waveshare module for solenoid valves. Same bus, same firmware, both devices coexist.
- **Field upgradeability** — a non-UL panel can be upgraded to UL 508 by swapping the relay module (Waveshare → Advantech) and contactors. No firmware reflash, no rewiring of the RS485 bus, no reconfiguration of the ARM.

### Compared to Current Approach

With the current CPLD shift register design, the I/O hardware is baked into the custom main board and expansion boards. There is no way to selectively upgrade individual outputs for UL certification — the entire board is either listed or it isn't, and getting a custom board UL-listed requires expensive per-design testing and ongoing factory audits.

With RTU, the custom board is just the ARM + MAX485 (a simple, low-component-count design that could itself be UL-listed once if desired). All field I/O is handled by off-the-shelf modules that carry their own certifications from their respective manufacturers. The certification burden shifts from Agristar's custom hardware to commodity industrial components that are already tested and listed.

Since Agristar already holds a UL 508A panel shop certification, building UL-labeled panels with RTU modules requires only: (1) selecting components and evaluating them per 508A requirements (see Section 11a below), (2) calculating and documenting SCCR, and (3) applying the UL 508A nameplate. No additional product testing or factory audits beyond the existing annual UL panel shop audit.

---

## 11a. UL 508A Component Acceptance — Using Unlisted Components

UL 508A does **not** require every component inside the panel to be UL Listed. The standard recognizes three tiers of component acceptance:

| Tier | Mark | What It Means | Documentation Needed |
|---|---|---|---|
| **UL Listed** | UL mark | Independently tested and listed by UL | Reference the UL file number — done |
| **UL Recognized** | UR mark | Tested as a component for use inside listed equipment | Reference the UR file number, verify Conditions of Acceptability are met |
| **Unlisted / Unrecognized** | None (CE, etc.) | Not evaluated by UL | **Panel shop evaluates per 508A requirements** |

The Waveshare 23406 and most Tier 2/3 modules fall into Tier 3. Agristar's UL 508A panel shop certification gives us the authority to evaluate and accept unlisted components.

### What to Document for an Unlisted Component (e.g. Waveshare 23406)

1. **Relay contact ratings** — Verify relay ratings (10A 250VAC per spec) meet or exceed the load. File the relay datasheet (likely Songle SRD-05VDC-SL-C or equivalent).
2. **Spacing / clearance** — Measure PCB trace spacing between line voltage and low voltage. Must meet Table SB4.1 (typically 6.4mm through-air, 12.7mm over-surface for 300V). This is the most likely failure point on cheap modules — if traces are too close between the 250VAC relay side and 5V logic side, the module fails evaluation.
3. **SCCR contribution** — Include the module's relays in the panel SCCR calculation. Without a UL-assigned SCCR, conservatively rate at 100A or perform a tested/evaluated determination.
4. **Temperature rating** — Verify module operating range covers the enclosure environment.
5. **Wiring diagram** — Show the module on the panel drawing with terminal designations.

### Practical Audit Experience

During the annual UL audit, the field engineer will:
- See the Waveshare in the panel
- Ask if it's UL Listed (no)
- Ask if Agristar evaluated it per our procedures (yes — here's the evaluation file)
- Review spacing evaluation and SCCR documentation
- If paperwork is in order, it passes

This is routine — most panel shops use unlisted terminal blocks, power supplies, and other components regularly. It's a normal part of 508A panel building.

### The Spacing Risk

Chinese-manufactured relay modules sometimes have inadequate creepage/clearance between high-voltage relay contacts and the low-voltage control side. If the Waveshare PCB doesn't meet Table SB4.1 spacing:
- Measure with a caliper once, document the results
- If it passes → re-use that evaluation on every panel (one-time effort)
- If it fails → add external barrier terminals to isolate line-voltage wiring, or use Advantech ADAM-4068 for UL builds

### When Evaluation Is Worth It vs. Just Using Advantech

| Scenario | Recommendation |
|---|---|
| Building 10+ UL panels/year | Evaluate Waveshare once, save ~$100/panel ongoing |
| Building 1–5 UL panels/year | Use Advantech ADAM-4068 ($130), skip the paperwork |
| Mixed panel (some UL, some not) | Waveshare for non-UL outputs, Advantech for UL-critical circuits |

---

## 11b. Cost Comparison — Current Custom Board vs RTU

### Per-Panel BOM: CPLD Output Stage

| Component | Qty | Est. Unit | Total |
|---|---|---|---|
| CPLD (MAX II / Lattice) | 1 | ~$5 | $5 |
| Relay drivers (ULN2803 or equiv) | 2 | ~$1.50 | $3 |
| PCB relays (Omron G5LE or similar) | 11 | ~$2 | $22 |
| Optocouplers | 11 | ~$0.50 | $5.50 |
| Connectors, passives, PCB area | — | — | ~$8 |
| **Output stage subtotal** | | | **~$44** |

Add an expansion board (8 more outputs): ~$30 in components. Total for 11–19 outputs: **~$44–74**.

All covered under existing UL 508A panel shop cert — no additional per-component UL cost.

### Per-Panel BOM: RTU Modules

| Option | Outputs | Cost |
|---|---|---|
| 1× Waveshare 23406 | 8 relays | $30 |
| 2× Waveshare 23406 | 16 relays | $60 |
| 1× Advantech ADAM-4068 (UL) | 8 relays | $130 |
| 2× Advantech ADAM-4068 (UL) | 16 relays | $260 |

Plus ~$3 for MAX485 transceiver on the ARM board.

### Where Each Approach Wins

| Factor | Current Custom Board | RTU Modules |
|---|---|---|
| **BOM cost (11 outputs)** | **~$44 (cheaper)** | ~$60–130 |
| **Board design & layout** | Maintain a custom PCB | Zero — commodity module |
| **Board fabrication** | Custom PCB run per batch | Buy off the shelf |
| **Assembly labor** | Solder CPLD + 11 relays + drivers + optos + connectors | Plug in a DIN-rail module |
| **Testing** | Test each board after assembly | Factory-tested, ships working |
| **Inventory** | Stock CPLDs, relays, drivers, optos, connectors, bare PCBs | Stock one SKU |
| **Scalability** | Fixed 11 main + 8 per expansion. Different layout? New PCB. | Add/remove modules on bus |
| **Field replacement** | Swap custom board (trained tech, exact part) | Swap $30 DIN-rail module (any electrician) |
| **Expansion boards** | Custom design, custom fab, custom assembly | Same module, different DIP switch address |
| **Board variants** | Different boards for different installation sizes | One ARM board + N modules |
| **Supply chain risk** | CPLD goes EOL → redesign | Module discontinued → pick another Modbus module |

### The Hybrid Approach

Keep the current main board for standard installations (11 outputs is often enough). Add UART3 + MAX485 to the ARM board for sites that need more — those get RTU relay modules on the bus for expansion instead of custom expansion boards.

- **Standard builds:** Same cheap custom board already in production
- **Large builds:** Custom main board + Waveshare modules for expansion (cheaper and more flexible than custom expansion boards)
- **No hardware redesign** needed for the main board — just add the RTU firmware path as an expansion method

This gets the RTU benefit where it matters (scalability, expansion, field serviceability) without throwing away what already works.

---

## 12. UL Requirements — When It's Required and When It Isn't

There is **no federal law requiring UL listing for all industrial equipment in the US.** The requirement comes from a patchwork of local codes, inspectors, customer specs, and insurance policies. For agricultural storage building controls, UL is required less often than most people assume.

### What the Codes Actually Say

**NEC (NFPA 70) Article 409 — Industrial Control Panels:**
- Requires panels to be "listed" or "labeled" by a Nationally Recognized Testing Laboratory (NRTL)
- NEC is a model code — it only has force of law where **adopted by the local jurisdiction**
- Most US states adopt NEC, but enforcement varies widely by jurisdiction

**OSHA 29 CFR 1910.303(b)(2):**
- Requires equipment to be "approved" — OSHA defines this as "acceptable to the authority having jurisdiction"
- OSHA recognizes NRTL listing (UL, CSA, ETL, TÜV) as one way to demonstrate approval
- OSHA also accepts **field evaluation** by a qualified inspector as an alternative to product listing
- Applies to workplaces with employees, not private agricultural buildings operated by owners

**NEC Article 547 — Agricultural Buildings:**
- Has different, often less strict requirements than Article 409
- Focuses on wiring methods in wet/corrosive/dusty environments, not on equipment listing
- Raw vegetable storage buildings are classified under Article 547, not Article 409
- No public access + no packing = lightest enforcement category in agriculture

### Agricultural Exemptions

- **Many states exempt agricultural buildings** from commercial electrical code enforcement. Vegetable storage buildings on agricultural land are classified as agricultural structures, not commercial buildings.
- **Owner-operated farms** without employees may not fall under OSHA jurisdiction (OSHA has a longstanding appropriations rider exempting small farms with no outside employees).
- **Field evaluation** is a legal alternative to UL product listing for companies without a panel shop certification — a qualified NRTL field evaluator (UL, CSA, ETL, or TÜV offer this service) can inspect and label a single panel for ~$1,000–3,000. **Agristar does not need this — we hold an active UL 508A panel shop certification and can self-label.**

### Who Enforces UL Requirements

| Entity | When They Check | Strictness |
|---|---|---|
| **Local building inspector (AHJ)** | During building permit / occupancy inspection | Varies by jurisdiction — strict in cities, rarely enforced in rural agricultural areas |
| **OSHA** | After complaints or workplace incidents | Only applies to employers with employees; longstanding ag exemption for small farms |
| **Insurance company** | During underwriting or after a claim | Some require UL, many don't specifically check for agricultural equipment |
| **Customer procurement** | Specified in PO or bid documents | Large corporate growers often require it; small growers never ask |

### When UL 508A Is Required

| Trigger | Why | Example |
|---|---|---|
| **US/Canada commercial building** | NEC Article 409 + local AHJ enforcement | Storage building on a university or corporate campus |
| **Customer PO specifies "UL"** | Contractual obligation — large agriculture operations often have blanket procurement specs | Large packing operations, corporate farm groups |
| **Insurance policy requires it** | Commercial property insurer mandates UL-listed electrical equipment | FM Global, Hartford Steam Boiler insured sites |
| **Strict state/county inspector** | Some jurisdictions enforce UL 508A during permit inspection | California, Massachusetts — see Section 13 for full state list |
| **Export to Canada** | CSA or cUL marking is legally required (Canadian Electrical Code, CSA C22.2) | Any Canadian greenhouse installation |
| **Government/military contracts** | Federal procurement mandates NRTL listing | USDA research facilities, state university storage buildings |

### When UL 508A Is NOT Required

| Scenario | Why | Example |
|---|---|---|
| **Private agricultural building** | Raw vegetable storage buildings on agricultural land with no public access are almost never inspected as commercial buildings. No customers inside, no packing — purely agricultural use. | Family farm storage building, private grower |
| **Non-US/Canada markets** | CE marking is the requirement in EU/EEA. UL is not recognized outside North America. | European, South American, Middle Eastern, Asian markets |
| **Retrofit/replacement** | Replacing an existing non-UL panel in a facility that was never UL-required | Upgrading a 20-year-old controller in an existing storage building |
| **Customer PO doesn't mention UL** | No contractual requirement, no inspection trigger | Most small-to-medium private storage operations |
| **Rural jurisdictions with light enforcement** | Many rural counties do not actively enforce Article 409 for agricultural buildings | Rural Texas, Idaho, Montana, Wyoming, Midwest — rarely enforced |
| **R&D/prototype/demo** | Internal test installations, demo units, trade show displays | Agristar test facilities, trade shows |

### Decision Flowchart for Office/Sales Staff

```
New order received
  ↓
Is the installation in Canada?
  ├── YES → UL 508A required (cUL/CSA marking)
  └── NO
       ↓
Does the customer PO or spec sheet mention UL, NRTL, or "listed" equipment?
  ├── YES → UL 508A required
  └── NO
       ↓
Is the site a commercial building subject to NEC inspection?
(university, corporate campus, government facility, multi-tenant building, packing house)
  ├── YES → UL 508A required
  └── NO
       ↓
Does the customer's insurer require UL-listed equipment?
  ├── YES → UL 508A required
  ├── UNKNOWN → Ask the customer to verify with their insurer
  └── NO
       ↓
Standard (non-UL) panel is acceptable — use standard BOM
```

### What Changes Between UL and Non-UL Builds

| Component | UL 508A Build | Standard Build |
|---|---|---|
| **RTU relay module** | Advantech ADAM-4068 (~$130, UL) | Waveshare 23406 (~$30, CE) |
| **Contactors** | Schneider LC1D series (UL 508) | Eaton/ABB/generic |
| **Enclosure** | UL-listed type (e.g. Rittal, Hammond, Saginaw with UL Type rating) | Standard NEMA-rated |
| **Wire** | UL-listed (MTW or THHN) | Standard |
| **Terminal blocks** | UL-recognized (Phoenix Contact, Wago with UL mark) | Standard |
| **DIN rail** | UL-recognized | Standard |
| **Panel label** | UL 508A nameplate with SCCR, voltage, enclosure type | Agristar standard label |
| **SCCR (Short Circuit Current Rating)** | Must be calculated and documented | Not required |
| **Documentation package** | Wiring diagram + BOM + SCCR calculation | Standard wiring diagram |
| **ARM board** | **Same** | **Same** |
| **Firmware** | **Same** | **Same** |
| **UI** | **Same** | **Same** |
| **RS485 bus wiring** | **Same** | **Same** |
| **Commissioning procedure** | **Same** | **Same** |
| **Cost delta** | **~$100–200 more per panel** | Baseline |

### Estimated Split for Agristar Orders

Based on the typical agricultural vegetable storage market:
- **~10–20% of orders** will require UL 508A (Canadian sites, corporate ag operations, cold storage with ammonia, sites in strict-enforcement states)
- **~80–90% of orders** will not require UL (private farms, small growers, international, most US agricultural storage)

The UL percentage is lower than greenhouse operations because: (1) no customers or public inside the building, (2) no packing or processing operations that trigger food-safety inspections, (3) purely agricultural classification under NEC Article 547.

Since Agristar already holds a UL 508A panel shop certification, fulfilling UL orders adds no engineering overhead — just a different BOM and the standard SCCR documentation package. The panel shop pulls from the UL shelf, calculates SCCR, applies the label, and ships.

---

## 13. State-by-State UL Enforcement for Agricultural Vegetable Storage

**Building type context:** Raw vegetable storage buildings — no customers inside, no greenhouses, no packing operations. Purely agricultural use for product storage (onions, potatoes, carrots, etc.). These buildings typically have ventilation fans, heaters, refrigeration compressors, humidifiers, and motorized doors/vents controlled by our equipment.

This classification matters enormously. The less public-facing and less industrial a building is, the less enforcement it attracts.

### Enforcement Tiers

#### Tier 1 — Strict Enforcement (Assume UL Required)

| State | Why | Notes |
|---|---|---|
| **California** | CalOSHA is separate from federal OSHA, actively inspects agricultural operations. State requires UL/NRTL listing. | Even ag storage buildings get inspected if they have any employees. CalOSHA has no small-farm exemption. |
| **Massachusetts** | Strict state electrical code, aggressive AHJ enforcement even in rural areas. | Board of Fire Prevention & Life Safety enforces uniformly. |
| **Connecticut** | State-administered code enforcement — no local opt-out. | Even rural towns have state-level inspectors. |
| **New Jersey** | Uniform Construction Code enforced statewide by DCA. | No agricultural exemption for electrical panels in buildings with >200A service. |
| **New York** | NYC and downstate strict; upstate varies but state code still applies. | Large ag operations in western NY (onion country) may get inspected. |

#### Tier 2 — Moderate Enforcement (Ask Before Assuming)

| State | Why | Notes |
|---|---|---|
| **Washington** | L&I actively enforces electrical code. Large ag presence (Yakima/Wenatchee) means inspectors are familiar with storage buildings. | Commercial-scale cold storage will be inspected. Small on-farm storage often not. |
| **Oregon** | BCD enforces statewide. Similar to Washington. | Large operations in Willamette Valley get inspected. |
| **Minnesota** | State electrical board enforces uniformly. | But ag storage is well understood — inspectors are practical. |
| **Wisconsin** | DSPS enforces statewide. | Active dairy/ag state — inspectors familiar with farm buildings but do inspect. |
| **Pennsylvania** | UCC adopted statewide but enforcement delegated to municipalities. | Varies — Lancaster County (ag heavy) is practical; Philadelphia suburbs strict. |
| **Florida** | Varies by county. South FL (Dade, Broward) very strict; Panhandle/rural much less. | Ag storage in Homestead area — ask. Rural north FL — unlikely to be inspected. |
| **Colorado** | Depends on jurisdiction. Front Range strict; Western Slope rural. | Large potato/onion storage on Western Slope — usually not inspected. |

#### Tier 3 — Light / No Enforcement (Standard BOM Fine)

| State | Why | Notes |
|---|---|---|
| **Texas** | No state electrical code. Enforcement is local only, and most rural counties have no electrical inspector at all. | Even large ag operations in the Panhandle/Rio Grande Valley operate without UL panels. |
| **Idaho** | Major potato storage state. Electrical code enforcement is local; most rural counties don't inspect agricultural buildings. | Idaho potato growers almost never require UL for storage building controls. |
| **Montana** | Minimal state-level enforcement. Rural counties rarely inspect. | |
| **Wyoming** | No state electrical code for agricultural buildings. | |
| **North Dakota / South Dakota** | Light enforcement in agricultural areas. | Potato/grain storage — rarely inspected. |
| **Nebraska / Kansas** | Local enforcement only; rural areas exempt in practice. | |
| **Iowa** | State code exists but ag buildings on farms are largely exempt from permit requirements. | |
| **Indiana / Ohio** | Local enforcement varies. Rural ag areas — very light. | Large potato/onion operations in OH — usually not inspected. |
| **Michigan** | Mixed — depends on county. Rural ag areas light. | West MI fruit/veg storage — varies by county. |
| **Georgia / Alabama / Mississippi** | Light enforcement outside metro areas. | |
| **North Carolina / South Carolina** | NC has state enforcement (moderate); SC is local only (light). | Sweet potato storage in NC — moderate, ask first. |
| **Maine / Vermont / New Hampshire** | Rural and agricultural — light enforcement for ag buildings. | |
| **Arizona / New Mexico** | Minimal for agricultural. | |
| **Utah** | Local enforcement only; ag areas light. | Large onion/potato storage in western UT — rarely inspected. |

### Special Cases — Always Assume UL Required

| Scenario | Why |
|---|---|
| **Cold storage with ammonia refrigeration** | Ammonia systems trigger PSM (Process Safety Management) inspections under OSHA 29 CFR 1910.119. Any inspector on-site for ammonia will also inspect electrical panels. |
| **Building with employees from a non-family company** | Staffing agency workers, contracted labor = OSHA jurisdiction applies even on farms. |
| **Building attached to or part of a packing facility** | Packing operations are classified as agricultural processing, not storage — different code treatment. |
| **Canadian provinces (all)** | CSA C22.2 (Canadian Electrical Code) requires product certification. No exceptions for agricultural buildings. cUL or CSA mark is legally required. |
| **Insurance carrier specifically requires UL** | Contractual — regardless of jurisdiction. Common with FM Global, Hartford Steam Boiler. |

### Practical Decision Table for Sales

| Customer says... | Action |
|---|---|
| "We're a family farm in Idaho storing potatoes" | Standard BOM. No UL needed. |
| "We have onion storage in eastern Oregon" | Ask: "Does your county require electrical permits for ag buildings?" If yes → UL. If no/unsure → Standard. |
| "We're in California" | UL BOM. Don't ask, just do it. |
| "We're in Texas" | Standard BOM. |
| "We're building cold storage with ammonia" | UL BOM. Ammonia = inspected. |
| "We're in Canada" | UL BOM (cUL/CSA required). |
| "We have contracted workers in the building" | Check state — if Tier 1 or 2, use UL BOM. If Tier 3, Standard is probably fine but ask. |
| "Our insurance requires UL" | UL BOM. |
| "We're a university research facility" | UL BOM. Government/institutional = always inspected. |
| Customer PO mentions UL, NRTL, or "listed" | UL BOM. |
| None of the above triggers | Standard BOM. |

### Bottom Line for Agristar's Market

Raw vegetable storage buildings with no public access, no packing, and no customers inside are the **lowest-risk electrical classification** in agriculture. These are essentially equipment sheds with climate control. In the vast majority of US states and counties where these buildings are located (rural agricultural areas), UL 508A is not enforced and not expected.

**Realistic estimate for Agristar orders:**
- **~5–15% will need UL** (California, Canada, ammonia cold storage, corporate with procurement specs)
- **~85–95% will not** (private farms, rural states, international)

The dual-BOM approach costs nothing extra to maintain and covers the occasional strict-jurisdiction order without over-engineering every panel.
