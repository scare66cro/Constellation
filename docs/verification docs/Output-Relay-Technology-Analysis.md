# Output Relay Technology & Bus Architecture Analysis

**Date:** March 22, 2026  
**Context:** Relay technology selection, EMI analysis, bus topology, and UL 508A component acceptance for Agristar AS2 control panels  
**Related:** [RTU-Relay-Module-Comparison.md](RTU-Relay-Module-Comparison.md), [RTU-Migration-Guide.md](RTU-Migration-Guide.md), [VFD-Modbus-RTU-Architecture-Proposal.md](VFD-Modbus-RTU-Architecture-Proposal.md)

---

## 1. Relay Technology Comparison — EMR vs SSR vs Hybrid

### Electromechanical Relay (EMR)

Standard mechanical relay with physical contacts — what the current CPLD board uses and what Modbus RTU relay modules (Waveshare 23406) contain.

| Attribute | Value |
|---|---|
| **Switching speed** | 5–15ms (includes contact bounce) |
| **EMI generated** | Moderate — contact arc + bounce produces broadband RF (1–300 MHz) |
| **Lifespan** | 100K–500K cycles |
| **Failure mode** | Typically **open** (spring fatigue, contact erosion). Can **weld shut** under inrush. |
| **Leakage current** | Zero when off |
| **Heat dissipation** | Near zero at steady state |
| **Inductive loads** | Handles natively — no snubber required |
| **Cost per channel** | ~$2 (PCB mount), ~$4 (DIN-rail module channel) |

**Contact welding risk:** EMR contacts can weld shut from inrush current. Contactor coils draw 5–10x rated current for the first ~10ms. A relay rated 10A can see 30–50A inrush from a large contactor coil. At Agristar's cycle rates (a few toggles per hour), welding risk is low but non-zero.

### Solid State Relay (SSR)

Semiconductor switching element (triac/thyristor). No moving parts.

| Attribute | Value |
|---|---|
| **Switching speed** | <1ms (zero-cross types wait for next zero crossing, max ~8.3ms at 60Hz) |
| **EMI generated** | Near zero with zero-cross types; high with random-cross types |
| **Lifespan** | 100M+ cycles (no mechanical wear) |
| **Failure mode** | Typically **shorted** (triac breakdown — conducts permanently) |
| **Leakage current** | 1–5mA when "off" |
| **Heat dissipation** | ~1W per channel under load — requires heatsink |
| **Inductive loads** | Needs snubber (RC network) to prevent dV/dt false triggering |
| **Cost per channel** | $3–8 (Crydom/Omron DIN-rail) |

### Hybrid Relay (Zero-Cross EMR)

Combines an SSR for the switching transition with EMR contacts for steady-state current carrying. Best of both worlds.

**Turn-on sequence:**
```
Command ON → SSR fires at zero crossing (no arc, no EMI)
           → ~10ms later, EMR contacts close (no arc — SSR carrying current)
           → SSR turns off, EMR carries the load (zero heat, zero leakage)
```

**Turn-off sequence:**
```
Command OFF → SSR turns on (takes current from EMR contacts)
            → EMR contacts open (no arc — SSR carrying current)
            → SSR turns off at next zero crossing (clean, no EMI)
```

| Attribute | Value |
|---|---|
| **Switching EMI** | Near zero — zero-cross on both transitions |
| **Failure mode** | Fail-open (both stages must conduct for output to be ON) |
| **Leakage** | Zero at steady state (EMR carries current) |
| **Heat** | Near zero (EMR carries steady state) |
| **Cost per channel** | $20–40 (Crydom DRA1-CX, Carlo Gavazzi RGC1A, Omron G3PJ) |
| **Modbus built-in** | No — single-channel DIN-rail, requires separate I/O module to drive |

**Products:**

| Product | Rating | Price |
|---|---|---|
| Crydom DRA1-CX series | 5–40A, 240/480VAC | ~$25–40 |
| Carlo Gavazzi RGC1A | 20–75A, 600VAC | ~$35–60 |
| Omron G3PJ | 15–45A, 480VAC | ~$30–50 |
| Phoenix Contact PLC-OSC | 6A, 250VAC | ~$20 |
| Schneider Zelio SSH1A | 10–25A, 480VAC | ~$30 |

---

## 2. Zero-Cross vs Random-Cross SSR

For Agristar's application (ON/OFF switching of contactor coils), **zero-cross is the only correct choice**.

| | Zero-Cross | Random (Instant-On) |
|---|---|---|
| **Switches at** | Next AC voltage zero crossing | Immediately, wherever on the sine wave |
| **dV/dt at turn-on** | ~0 V — voltage is at zero | Up to 170V (120VAC peak) or 340V (240VAC peak) |
| **EMI generated** | Minimal | High — hard voltage step + current inrush |
| **Inrush on contactor coils** | Controlled — coil magnetizes gradually from zero | Violent — full voltage slammed onto inductance |
| **Acoustic noise** | Silent | Contactor "clunks" harder |
| **Use case** | ON/OFF switching (heaters, motors, contactors, fans) | Phase-angle dimming, proportional power control |

**Why it matters for contactor coils:** A random-cross SSR firing at sine wave peak (170V on 120VAC) causes: instantaneous high dV/dt across inductance → massive dI/dt → current spike → conducted EMI burst. A zero-cross SSR starts at ~0V, ramping gradually — no spike, no EMI.

**Random-cross SSRs exist only for phase-angle control** (dimming, proportional power). Agristar has no phase-angle control requirement. Zero-cross is always the answer for ON/OFF loads.

**Recommended zero-cross SSR products (if SSR path is chosen):**

| Product | Rating | Price |
|---|---|---|
| Crydom CKR2420 | 20A, 24–280VAC, zero-cross, DIN-rail + heatsink | ~$12 |
| Omron G3NA-220B | 20A, 24–240VAC, zero-cross, DIN clip | ~$15 |
| Carlo Gavazzi RM1A23D25 | 25A, zero-cross, DIN mount | ~$18 |

All are oversized for 2A contactor coils — this keeps the SSR cool and extends life.

---

## 3. Failure Mode Analysis for Agricultural Storage Buildings

The critical question isn't EMR vs SSR — it's **what happens when the output fails stuck-ON** in an unattended building.

| Output | Stuck-ON Consequence | Severity |
|---|---|---|
| **Heater** | Heater stays ON permanently | **Critical — fire risk, crop destruction** |
| **Refrigeration compressor** | Compressor stays ON | **High — overfreeze, compressor burnout** |
| **Humidifier** | Stays ON | **High — over-humidification, mold** |
| **Fan motor** | Fan stays ON | Low — just keeps running |
| **Door actuator** | Stays energized | Medium — motor burnout, mechanical damage |

**Stuck-OFF is almost always the safe state** in agricultural storage. Equipment that won't turn on is an "I'll fix it tomorrow" problem. Equipment that won't turn off is a "the building is on fire" problem.

### Failure Mode by Technology

| Technology | Primary failure mode | Stuck-ON risk |
|---|---|---|
| **EMR** | Open (spring fatigue, contact erosion) | Low — possible from contact welding under inrush |
| **SSR** | Shorted (triac breakdown) | Higher — triac fails shorted and conducts permanently |
| **Hybrid** | Open (both stages must conduct) | Lowest — two independent elements must both fail |

### The Real Answer: Output Monitoring

Regardless of relay technology, **stuck-output detection** is required for safe operation:

```
Output commanded ON  + DI reads ON  = Normal
Output commanded OFF + DI reads OFF = Normal
Output commanded OFF + DI reads ON  = STUCK OUTPUT → ALARM
Output commanded ON  + DI reads OFF = FAILED OPEN → ALARM
```

The Waveshare 23406 has **8 digital inputs** alongside its 8 relay outputs. Wire a current sense or auxiliary contact from each contactor back to the DIs. The firmware then detects both stuck-ON (any technology) and failed-open conditions.

This catches EMR weld, SSR fail-short, and any other failure mode. The response can be:
1. Sound alarm / send notification via UI
2. Trip upstream breaker (if wired to a shunt trip)
3. Display red fault on the Svelte UI

---

## 4. EMI Analysis — What Actually Matters

### EMI Sources in an Agristar Panel (Ranked by Magnitude)

| Source | Magnitude | What Generates It |
|---|---|---|
| **VFD (ACS380) IGBT switching** | **High** — #1 EMI source | IGBT switching at 4–16 kHz, dV/dt up to 5,000 V/µs |
| **Power contactor contacts closing** | **Medium** | 40A 3-phase contacts closing onto running motor = arc |
| **Contactor coil energize/de-energize** | **Low** | 2A coil, small inductive kick |
| **Control relay (EMR or SSR)** | **Very low** | 2A at 24/120VAC — trivial compared to above |

The control relay — whether EMR or SSR — is the **smallest EMI contributor** in the panel. Replacing it with an SSR optimizes the wrong layer.

### What SSRs Do and Don't Fix

**SSRs eliminate:**
- Contact arc EMI (broadband RF burst at each EMR open/close)
- Contact bounce EMI (3–10 bounces in first ~5ms of EMR closing)

**SSRs do NOT eliminate:**
- **Backfed EMI from the load** — when the SSR energizes a contactor coil, the contactor's power contacts still switch 3-phase motor current. That's where the real EMI comes from.
- **Motor start/stop transients** — VFDs, across-the-line starters, soft starters all generate EMI during switching regardless of control relay type.
- **Conducted EMI on the power bus** — switching transients travel through power wiring. The control circuit relay has no effect.

### What Actually Protects the RS485 Bus

| Protection | What It Does | Cost |
|---|---|---|
| **Shielded twisted pair** (Belden 9841) | Rejects common-mode noise on RS485 differential pair | ~$0.50/ft |
| **TVS diodes on RS485** (built into Waveshare) | Clamps voltage spikes from reaching transceiver | Included |
| **Optoisolation** (built into Waveshare) | Galvanic barrier between RS485 and relay logic | Included |
| **Differential signaling** (RS485 by design) | Inherently rejects common-mode noise | Inherent |
| **120Ω termination** | Prevents signal reflections | $0.10 resistor |
| **Routing separation** | RS485 cable ≥6" from power wiring, cross at 90° | Free |
| **EMC line filter on VFD input** | Suppresses conducted EMI from VFD into panel | ~$30–50 |
| **Ferrite choke on RS485 cable** | Last-resort filter at module end | ~$2 |

### Conclusion on EMI

A 2A EMR arc from a contactor coil, transmitted through shielded twisted pair, hitting TVS-protected, optoisolated, differential RS485 inputs is **buried in the noise floor**. RS485 was designed for factory floors with motors, VFDs, contactors, and welders. The EMI from Agristar's control relays is insignificant with standard protections in place.

---

## 5. VFD vs Output Bus Separation

### Recommendation: Keep VFD and Outputs on Separate Buses

VFD speed control and relay output switching have fundamentally different timing requirements. They should not share a bus.

### Three Options

**Option A — Shared RS485 Bus (NOT recommended)**
```
ARM UART3 → MAX485 → RS485 bus
                        ├── VFD (ACS380 native RS485)
                        ├── Waveshare relay module #1
                        ├── Waveshare relay module #2
                        └── DI module
```

Problems:
- 15+ Modbus transactions per cycle at 9600 baud = ~300ms full cycle
- VFD speed updates compete with output updates for bus time
- Bus fault kills VFD and outputs simultaneously
- Single point of failure

**Option B — VFD on Modbus TCP, Outputs on RS485 (RECOMMENDED NOW)**
```
ARM UART1 → RPi5 → Ethernet → FMBT-21 → VFD (existing, working, stable)
ARM UART3 → MAX485 → RS485 bus
                        ├── Waveshare relay module #1
                        ├── Waveshare relay module #2
                        └── DI module
```

Benefits:
- VFD speed goes over Ethernet at wire speed — no contention
- RS485 handles only slow output/input operations (toggle a few times per hour)
- Bus fault on RS485 → outputs freeze, VFD keeps running (or safely stops)
- VFD fault → VFD stops, outputs (heaters, doors, humidifiers) keep working
- Zero new hardware — FMBT-21 is already installed and working

**Option C — Two Separate RS485 Buses (FUTURE — eliminates FMBT-21)**
```
ARM UART3 → MAX485 #1 → RS485 bus A → VFD (ACS380 native RS485 port)
ARM UART4 → MAX485 #2 → RS485 bus B → Relay modules + DI modules
```

Benefits:
- Eliminates FMBT-21 ($200 saved per drive)
- Complete bus isolation between VFD and outputs
- Two MAX485 chips = ~$6 total
- ARM has 5 free UARTs — no hardware constraint

### Comparison

| Concern | Shared Bus | Separate Buses |
|---|---|---|
| **VFD response time** | 200–300ms (shares bus) | <50ms (dedicated link) |
| **Contactor EMI isolation** | Relay modules switch on same bus as VFD | Relay bus absorbs EMI, VFD bus is clean |
| **Fault isolation** | One bad connection kills everything | VFD runs if output bus fails |
| **Firmware complexity** | One task juggles priority | Two independent tasks, each simple |
| **Baud rate** | Lowest common denominator | Each bus optimized independently |
| **Debugging** | Hard to isolate delays | Each bus has own log/timing |

### Migration Path

1. **Now:** Option B — VFD stays on Modbus TCP (FMBT-21), add UART3 + MAX485 for relay modules only
2. **Next board revision:** Option C — two RS485 buses, eliminate FMBT-21, save $200/drive
3. Firmware RTU task written for relay modules can be cloned for VFD bus with minimal changes

---

## 6. RS485 Wiring — 2-Wire vs 4-Wire

### 2-Wire (Half-Duplex) — What Agristar Uses

```
Wire A (D+)  ─── all devices
Wire B (D-)  ─── all devices
GND          ─── all devices (via shield drain wire)
```

- **Half-duplex:** one device talks at a time (master query → slave response)
- **This is standard Modbus RTU** — inherently request/response, half-duplex
- All devices share one bus — daisy chain, up to 32 devices (256 with repeater)
- Sometimes called "3-wire" when GND is counted

### 4-Wire (Full-Duplex) — NOT Used for Modbus RTU

```
TX+ (master→slave)  ─── all devices RX+
TX- (master→slave)  ─── all devices RX-
RX+ (slave→master)  ─── all devices TX+
RX- (slave→master)  ─── all devices TX-
GND                  ─── all devices
```

- **Full-duplex:** simultaneous bidirectional communication
- **Not standard Modbus RTU** — Modbus is defined as half-duplex
- Requires 2 shielded twisted pairs instead of 1
- No throughput benefit for request/response protocols
- Sometimes called "5-wire" when GND is counted

### Naming Confusion

| What people say | What they mean |
|---|---|
| "2-wire RS485" | A + B, half-duplex (ground assumed) |
| "3-wire RS485" | A + B + GND, half-duplex |
| "4-wire RS485" | TX pair + RX pair, full-duplex (ground assumed) |
| "5-wire RS485" | TX pair + RX pair + GND, full-duplex |

### Cable Specification

**Belden 9841** or equivalent:
- 1 shielded twisted pair
- 24 AWG solid conductor
- Foil shield + drain wire
- Characteristic impedance: 120Ω (matches RS485 termination)
- Connect drain wire to ground at **one end only** (ARM/master end) to avoid ground loops

---

## 7. Cost Comparison — Relay Technology Options for 8 Outputs

| Approach | Components | Cost (8 outputs) | Modbus Built-in | Notes |
|---|---|---|---|---|
| **Waveshare 23406 (EMR)** | 1 module | **$30** | Yes | Standard choice. EMR contacts, RS485/Modbus RTU, 8ch relay + 8ch DI |
| **Advantech ADAM-4068 (EMR, UL)** | 1 module | **$130** | Yes | UL-listed. Same functionality, 4x the price |
| **DIN-rail SSRs + I/O module** | 8 SSRs + 1 RTU module | **$126–194** | Via RTU module | 8× Crydom CKR ($12) + Waveshare ($30) to drive them |
| **DIN-rail hybrid relays + I/O module** | 8 hybrids + 1 RTU module | **$190–350** | Via RTU module | 8× Crydom DRA1 ($20–40) + Waveshare ($30) |
| **Current CPLD board (EMR)** | Custom PCB + components | **~$44** | No (SPI shift register) | What's in production now. 11 outputs. |

### Decision Matrix

| Factor | Waveshare EMR | SSR Array | Hybrid Array |
|---|---|---|---|
| **Cost** | $30 | $126–194 | $190–350 |
| **EMI from switching** | Low (2A coil arc) | Near zero | Near zero |
| **Fail-safe** | Mostly open (possible weld) | Fails shorted | Fails open |
| **Leakage** | Zero | 1–5mA per channel | Zero |
| **Heat** | None | ~1W per channel | None |
| **DIN rail space** | 4" (1 module) | ~12" (8 modules + driver) | ~12" (8 modules + driver) |
| **Wiring complexity** | 1 terminal block | 8 individual modules | 8 individual modules |
| **Field replacement** | Swap 1 module | Swap 1 SSR | Swap 1 hybrid |
| **Output monitoring** | Built-in DIs | Need separate DI module | Need separate DI module |

---

## 8. UL 508A Component Acceptance — Using Unlisted Components

**Agristar holds an active UL 508A panel shop certification.** This means Agristar can evaluate and accept unlisted components for use in UL-labeled panels. UL Listed components are not required for every part inside a 508A panel.

### Three Tiers of Component Acceptance

| Tier | Mark | What It Means | Documentation |
|---|---|---|---|
| **UL Listed** | UL mark | Independently tested and listed by UL | Reference UL file number — done |
| **UL Recognized** | UR mark | Tested as component for use inside listed equipment | Reference UR file number, verify Conditions of Acceptability |
| **Unlisted/Unrecognized** | None (CE, etc.) | Not evaluated by UL | **Panel shop evaluates per 508A requirements** |

The Waveshare 23406 falls into Tier 3. Agristar's 508A certification authorizes evaluation and acceptance.

### Evaluation Procedure for an Unlisted Component

**One-time evaluation per component type. Reuse on every panel.**

1. **Relay contact ratings** — verify relay ratings (10A 250VAC per Waveshare spec) meet or exceed load. File the relay datasheet (Songle SRD-05VDC-SL-C or equivalent).

2. **Spacing / clearance** — measure PCB trace spacing between line voltage and low voltage with digital caliper:
   - Clearance (through air): min 6.4mm per Table SB4.1 at 300V
   - Creepage (over surface): min 12.7mm per Table SB4.1 at 300V
   - Between relay contact pins and coil pins
   - Between adjacent relay contacts on different circuits

3. **SCCR contribution** — include module relays in panel SCCR calculation. Without UL-assigned SCCR, conservatively rate at 100A. With 10A fuse per output circuit, SCCR can be raised per series-rated combination.

4. **Temperature rating** — verify module operating range (-40°C to +85°C per Waveshare) covers enclosure environment.

5. **Documentation** — photos with caliper measurements, relay datasheet, completed evaluation form. File in 508A quality system binder.

### If PCB Spacing Fails

- **Option A:** Add barrier terminal blocks (Phoenix Contact UK series, UL-Recognized) between Waveshare outputs and field wiring. Provides physical separation.
- **Option B:** Apply conformal coating to tight PCB traces (MG Chemicals 419D). Increases effective creepage.
- **Option C:** Use Advantech ADAM-4068 ($130, UL Listed) for panels requiring UL. Skip evaluation entirely.

### Annual Audit

During the annual UL field engineer visit:
- They see the Waveshare in a panel
- Ask if it's Listed (no)
- Ask for the evaluation file (yes — here it is)
- Review spacing measurements and SCCR documentation
- If paperwork is solid, passes in 5 minutes

This is routine — most panel shops use unlisted terminal blocks, power supplies, and other components regularly.

### When Evaluation Is Worth It

| Volume | Recommendation |
|---|---|
| 10+ UL panels/year | Evaluate Waveshare once, save ~$100/panel ongoing |
| 1–5 UL panels/year | Use Advantech ADAM-4068, skip the paperwork |
| Mixed (some UL, some not) | Waveshare for non-UL, Advantech for UL |

### Evaluation Report Template

```
UNLISTED COMPONENT EVALUATION — UL 508A

Date:        ____________
Evaluator:   ____________
Component:   Waveshare 23406 Modbus RTU 8-Channel Relay Module
Manufacturer: Waveshare Electronics (Shenzhen)
Part No.:    23406
Certifications: CE (LVD + EMC)
UL Listing:  None

Application: Digital output module in Agristar AS2 industrial control panels.
Switches 24VAC/120VAC contactor coils and solenoid valves.
Controlled via RS485 Modbus RTU from ARM processor board.

Rating Verification:
  Relay type: ________________
  Relay rating: ___A ___VAC
  Max load in application: ___A ___VAC
  Load within relay rating: YES / NO

Spacing Verification (per UL 508A Table SB4.1):
  250VAC trace to 5V logic — clearance: required 6.4mm, measured ___mm  PASS/FAIL
  250VAC trace to 5V logic — creepage: required 12.7mm, measured ___mm  PASS/FAIL
  Relay contact to coil pins — clearance: required 6.4mm, measured ___mm  PASS/FAIL
  Adjacent relay contacts — clearance: required 6.4mm, measured ___mm     PASS/FAIL

Photos attached: YES / NO (require caliper in frame)

Temperature: Module rated -40°C to +85°C, enclosure 0°C to +50°C — PASS

SCCR: Conservative 100A (no fuse), or ___A with branch fuse

Conclusion: ACCEPTABLE / NOT ACCEPTABLE for use in UL 508A panels

Signed: ________________________
```

---

## 9. RTU-Controlled 3-Phase Contactors

These are smart contactors with built-in Modbus RTU communication — controlled directly over the RS485 bus without needing a separate relay module to drive their coil. The ARM sends a Modbus command and the contactor opens/closes, reports status, and provides diagnostics.

### Schneider Electric TeSys D with LULC033 Control Unit

| Attribute | Value |
|---|---|
| **Contactor** | LC1D25 (25A), LC1D32 (32A), LC1D38 (38A), LC1D40 (40A) |
| **Communication module** | LULC033 (Modbus RTU, RS485, 2-wire) |
| **Base unit** | LUB32 (up to 32A) or LUB38 (up to 38A) starter base |
| **Features** | Remote ON/OFF, status readback, overload trip, thermal monitoring, fault codes |
| **Baud rates** | 9600, 19200, 38400 |
| **Addressing** | DIP switch, 1–247 |
| **Certifications** | UL 508, CSA, CE, IEC |
| **Cost** | ~$200–250 (base + contactor + comm module) |
| **Notes** | Full motor starter with built-in overload protection (adjustable). Replaces contactor + overload relay + control relay. Most integrated option. |

**Part numbers for a 32A motor:**
- LUB32 — starter base (up to 32A)
- LUCA32BL — control unit, 8–32A overload setting, 24VDC coil
- LULC033 — Modbus RTU communication module
- LC1D32BL — contactor, 32A, 24VDC coil (used with LUB32 base)

### Schneider Electric TeSys island (TeSys Deca + VW3A)

| Attribute | Value |
|---|---|
| **Contactor** | LC1D09–LC1D150 (9A–150A range) |
| **Communication module** | VW3A3720 (Modbus RTU gateway for TeSys island) |
| **Features** | Multi-contactor control from one gateway (up to 20 starters), status/diagnostics/energy monitoring |
| **Baud rates** | 9600, 19200, 38400, 57600, 115200 |
| **Certifications** | UL, CSA, CE |
| **Cost** | ~$300–400 (gateway + contactor + starter module) |
| **Notes** | Designed for large installations with many motors. Gateway connects to RS485 bus, manages multiple starters over internal bus. Overkill for 1–3 motors. |

### ABB AF Series with Ekip Com Modbus Module

| Attribute | Value |
|---|---|
| **Contactor** | AF26 (26A), AF30 (30A), AF38 (38A), AF40 (40A) |
| **Communication module** | Ekip Com Modbus (for Tmax breaker) or 1SFA899300R1003 (Modbus RTU module for AF contactors via UA interface) |
| **Features** | Remote ON/OFF, status, coil feedback, cycle counter |
| **Baud rates** | 9600, 19200 |
| **Certifications** | UL 508, CSA, CE, IEC |
| **Cost** | ~$150–210 (contactor + comm module) |
| **Notes** | ABB's AF series uses electronically controlled coils (wide-range 100–250VAC/DC) which are inherently low-inrush. Less common choice for RTU integration — most people use AF with separate relay modules. |

**Part numbers for a 30A motor:**
- AF30-30-00-13 — contactor, 30A, 100–250VAC/DC coil
- 1SFA899300R1003 — Modbus RTU communication adapter

### Eaton xStart with SmartWire-DT

| Attribute | Value |
|---|---|
| **Contactor** | DILM25 (25A), DILM32 (32A), DILM38 (38A) |
| **Communication module** | SWD4-SFK-25 (SmartWire-DT module per contactor) + EU5C-SWD-MODBUS (Modbus RTU gateway) |
| **Features** | Remote ON/OFF, status, diagnostics, up to 99 devices per gateway |
| **Baud rates** | 9600, 19200, 38400, 57600 |
| **Certifications** | UL 508, CSA, CE |
| **Cost** | ~$180–250 (gateway + contactor + SWD module) |
| **Notes** | SmartWire-DT is Eaton's proprietary device-level bus. The Modbus gateway bridges it to standard RS485. Good for Eaton-heavy installations. Gateway cost is amortized across multiple starters. |

**Part numbers for a 32A motor:**
- DILM32-10(24VDC) — contactor, 32A, 24VDC coil
- SWD4-SFK-25 — SmartWire-DT communication module
- EU5C-SWD-MODBUS — Modbus RTU gateway (one per installation)

### Siemens SIRIUS 3RT2 with 3RW Communication Module

| Attribute | Value |
|---|---|
| **Contactor** | 3RT2026 (25A), 3RT2028 (38A), 3RT2036 (50A) |
| **Communication module** | 3RW5980-0HS00 (Modbus RTU for SIRIUS starters) |
| **Features** | Remote ON/OFF, status, overload data, diagnostics |
| **Certifications** | UL 508, CSA, CE, IEC |
| **Cost** | ~$200–280 (contactor + starter + comm module) |
| **Notes** | Siemens ecosystem. Communication module is part of the 3RW5 soft starter line — integrating a plain 3RT2 contactor with Modbus requires either the soft starter or a SIRIUS IO-Link/Modbus gateway (3UF7). |

### Comparison Summary

#### RTU-Controlled (Smart) Contactors

| Manufacturer | Contactor + Comm | Rating | RTU Cost | UL 508 | Ease of Modbus Integration |
|---|---|---|---|---|---|
| **Schneider TeSys D** | LC1D09 + LUB12 + LULC033 | 9A | ~$160 | Yes | **Best** — purpose-built for Modbus RTU |
| **Schneider TeSys D** | LC1D18 + LUB12 + LULC033 | 18A | ~$170 | Yes | Same system, 18A frame |
| **Schneider TeSys D** | LC1D25 + LUB32 + LULC033 | 25A | ~$190 | Yes | Same system, 25A frame |
| **Schneider TeSys D** | LC1D32 + LUB32 + LULC033 | 32A | ~$200 | Yes | Same system, 32A frame |
| **ABB AF** | AF09 + Modbus adapter | 9A | ~$120 | Yes | Compact — smallest AF frame |
| **ABB AF** | AF16 + Modbus adapter | 16A | ~$140 | Yes | Mid-range |
| **ABB AF** | AF26 + Modbus adapter | 26A | ~$160 | Yes | Common size |
| **ABB AF** | AF30 + Modbus adapter | 30A | ~$180 | Yes | Largest in compact range |
| **Eaton xStart** | DILM9 + SWD + gateway | 9A | ~$150 | Yes | Requires proprietary gateway |
| **Eaton xStart** | DILM17 + SWD + gateway | 17A | ~$170 | Yes | Same gateway, 17A frame |
| **Eaton xStart** | DILM25 + SWD + gateway | 25A | ~$190 | Yes | Same gateway, 25A frame |
| **Eaton xStart** | DILM32 + SWD + gateway | 32A | ~$200 | Yes | Same gateway, 32A frame |

#### Standard (Dumb) Contactors — Used with Waveshare Relay Module

These are driven by the Waveshare 23406 relay outputs. No built-in communication.

| Manufacturer | Part Number | Rating | Coil | UL 508 | Price |
|---|---|---|---|---|---|
| **Schneider LC1D** | LC1D09B7 | 9A | 24VAC | Yes | ~$30 |
| **Schneider LC1D** | LC1D12B7 | 12A | 24VAC | Yes | ~$35 |
| **Schneider LC1D** | LC1D18B7 | 18A | 24VAC | Yes | ~$40 |
| **Schneider LC1D** | LC1D25B7 | 25A | 24VAC | Yes | ~$50 |
| **Schneider LC1D** | LC1D32B7 | 32A | 24VAC | Yes | ~$60 |
| **Eaton XTCE** | XTCE009B10A | 9A | 110VAC | Yes | ~$25 |
| **Eaton XTCE** | XTCE012B10A | 12A | 110VAC | Yes | ~$28 |
| **Eaton XTCE** | XTCE018C10A | 18A | 110VAC | Yes | ~$35 |
| **Eaton XTCE** | XTCE025C10A | 25A | 110VAC | Yes | ~$40 |
| **Eaton XTCE** | XTCE032C10A | 32A | 110VAC | Yes | ~$45 |
| **ABB AF** | AF09-30-10-13 | 9A | 100–250V AC/DC | Yes | ~$30 |
| **ABB AF** | AF16-30-10-13 | 16A | 100–250V AC/DC | Yes | ~$35 |
| **ABB AF** | AF26-30-10-13 | 26A | 100–250V AC/DC | Yes | ~$45 |
| **ABB AF** | AF30-30-00-13 | 30A | 100–250V AC/DC | Yes | ~$55 |

**Note on ABB AF coils:** The "-13" suffix means 100–250V AC/DC wide-range electronic coil. These draw very low inrush (~0.1A) compared to traditional AC coils (~5–10A inrush), which virtually eliminates contact welding risk on the Waveshare relay driving them. Worth considering even at a slight price premium.

**Sizing guidance for typical Agristar loads:**

| Load | Typical FLA | Contactor Size |
|---|---|---|
| 1/4 HP fan motor (120V) | 5.8A | 9A contactor |
| 1/2 HP fan motor (120V) | 9.8A | 12A contactor |
| 1 HP fan motor (240V) | 5.0A | 9A contactor |
| 2 HP fan motor (240V) | 6.8A | 9A contactor |
| 3 HP fan motor (240V) | 9.6A | 12A contactor |
| 5 HP fan motor (240V) | 15.2A | 18A contactor |
| 5 HP compressor (240V) | 15.2A | 18A contactor |
| 10 HP compressor (240V) | 28A | 32A contactor |
| Heater bank (5kW, 240V) | 20.8A | 25A contactor |
| Heater bank (10kW, 240V) | 41.7A | Need 50A+ frame |

Most Agristar installations use motors in the **1/2 HP to 5 HP range** → 9A to 18A contactors. The 32A+ sizes are only needed for large compressors or heater banks.

### RTU Contactor vs Separate Relay Module + Dumb Contactor

**For typical Agristar loads (3 motors, 9–18A range):**

| Approach | Cost (3 motors) | Diagnostics | Wiring |
|---|---|---|---|
| **3× Schneider TeSys D RTU (9A)** | ~$480 | Full — overload, thermal, cycle count, coil status | RS485 daisy chain to each starter |
| **3× Schneider TeSys D RTU (18A)** | ~$510 | Same | Same |
| **Waveshare + 3× ABB AF09 (9A)** | ~$120 (Waveshare $30 + 3× AF09 $30) | Basic — DI feedback from aux contacts | Waveshare relay outputs to contactor coils |
| **Waveshare + 3× Eaton XTCE009 (9A)** | ~$105 (Waveshare $30 + 3× XTCE $25) | Basic — DI feedback from aux contacts | Same |
| **Waveshare + 3× Schneider LC1D18 (18A)** | ~$150 (Waveshare $30 + 3× LC1D18 $40) | Basic — DI feedback from aux contacts | Same |
| **Advantech + 3× Schneider LC1D18 (18A, UL)** | ~$250 (ADAM $130 + 3× LC1D18 $40) | Basic — DI feedback from aux contacts | Same |

**When to use RTU contactors:**
- Customer requires motor-level diagnostics (overload history, thermal trending, cycle counts)
- Installation has 5+ motors and justifies the per-motor cost
- Replacing existing motor starters that already have communication capability
- Customer specification requires "smart" motor protection

**When to use dumb contactors + relay module:**
- Most Agristar installations (1–3 motors)
- Cost-sensitive — $165 vs $600 for 3 motors
- No requirement for motor-level diagnostics
- Simpler firmware — relay module uses standard FC05/FC0F, no motor-specific register maps

---

## 11. Modbus-Controlled Electronic Expansion Valves (EEV) for Refrigeration

**Context:** Agristar's GRC (refrigeration controller) uses the same AS2 ARM board. Systems range from **40 to 140 tons** — commercial/industrial scale. EEV control over Modbus RTU is a natural extension of the RS485 bus architecture.

### What an EEV Does

Replaces the traditional thermostatic expansion valve (TXV/TEV). Instead of a mechanical bulb/diaphragm responding to suction line temperature, an electronic stepper motor drives the valve based on superheat calculation from pressure and temperature sensors.

**Benefits over TXV:**
- Precise superheat control (±1°F vs ±5–10°F for TXV)
- Faster response to load changes
- Can close fully for pump-down (TXV can't)
- Remote control and monitoring via Modbus
- Adaptive to varying conditions without manual adjustment
- Significant energy savings at scale

### Modbus-Controlled EEV Products

#### Danfoss AKV / AKVA Series (with AK-CC Controller)

| Attribute | Value |
|---|---|
| **Valves** | AKV 10 (1–3 ton), AKV 15 (2–8 ton), AKV 20 (5–18 ton), AKVA 10/15/20 (same range, newer series) |
| **Large capacity** | AKVA 20-3 through AKVA 20-6 — up to 48 ton per valve (R-404A, 40°F evap) |
| **Controller** | AK-CC 550A (single circuit) or AK-CC 750A (dual circuit) |
| **Communication** | Modbus RTU (RS485) built into controller |
| **Sensors** | 2× AKS temperature sensors + 1× AKS pressure transmitter per circuit |
| **Features** | Superheat control, defrost management, fan control, alarm outputs |
| **Certifications** | UL, CSA, CE |
| **Cost** | ~$300–800 per valve (size-dependent) + ~$200–400 per controller + ~$100–200 per sensor set |
| **Notes** | Industry standard for commercial refrigeration. AK-CC does superheat PID internally — Modbus is for setpoint, monitoring, and override. |

**Part numbers (large capacity for 40–140 ton systems):**
- AKVA 20-3 (068F3271) — up to 18 ton per circuit, R-404A
- AKVA 20-4 (068F3272) — up to 28 ton per circuit, R-404A
- AKVA 20-5 (068F3273) — up to 38 ton per circuit, R-404A
- AKVA 20-6 (068F3274) — up to 48 ton per circuit, R-404A
- AK-CC 750A (084B8130) — dual circuit controller with Modbus RTU
- AKS 21 (060G2083) — temperature sensor, NTC
- AKS 33 (060G2050) — pressure transmitter, 4–20mA or ratiometric

**For a 140-ton system:** 3–4 evaporator circuits × AKVA 20-5/20-6 valves, each with AK-CC 750A controller.

#### Carel E2V Series (with EVD Evolution Controller)

| Attribute | Value |
|---|---|
| **Valves** | E2V (sizes from 0.5 to 30+ ton per valve) |
| **Large capacity** | E2V45 — up to 45 ton per valve (R-404A) |
| **Controller** | EVD Evolution (EVD0000E50) — one per valve |
| **Communication** | Modbus RTU (RS485) or pLAN (Carel proprietary) |
| **Features** | Superheat control, full valve positioning, energy optimization, data logging |
| **Certifications** | UL, CE |
| **Cost** | ~$200–600 per valve (size-dependent) + ~$150–300 per controller + ~$80–150 per sensor set |
| **Notes** | Excellent Modbus register documentation. EVD controller manages stepper motor — ARM sends superheat setpoint, reads actual values. |

**Part numbers (large capacity):**
- E2V45CSW00 — large capacity, R-404A
- E2V30CSW00 — medium-large capacity, R-404A
- EVD0000E50 — EVD Evolution controller, RS485 Modbus RTU
- NTC015WH01 — temperature sensor
- SPKT0053R0 — pressure transducer, 0–500 psig

**For a 140-ton system:** 3–5 evaporator circuits × E2V30/E2V45 valves, each with EVD Evolution.

#### Emerson/Copeland EX Series (with EC3-X33 Controller)

| Attribute | Value |
|---|---|
| **Valves** | EX4–EX8 (1–25 ton), larger via manifolded valves |
| **Controller** | EC3-X33 (Modbus RTU option) |
| **Communication** | Modbus RTU (RS485) |
| **Features** | Superheat control, liquid injection, hot gas bypass |
| **Certifications** | UL, CE |
| **Cost** | ~$350–800 per valve + ~$250–400 per controller |
| **Notes** | Emerson/Copeland ecosystem. Common with Copeland scroll and semi-hermetic compressors. |

### Integration with GRC / AS2 Architecture

```
GRC (AS2 ARM board)
  ├── UART1 → RPi5 → Ethernet → UI / monitoring
  ├── UART2 → RS485 → Analog board (existing sensors)
  ├── UART3 → RS485 → Relay modules (compressor contactors, fans, defrost)
  │                  ├── Waveshare #1 (compressor stages)
  │                  ├── Waveshare #2 (condenser fans, defrost heaters)
  │                  └── Waveshare #3 (DI: pressures switches, safeties)
  └── UART4 → RS485 → EEV controllers (separate bus for real-time valve control)
                     ├── EEV Controller #1 (evaporator circuit 1)
                     ├── EEV Controller #2 (evaporator circuit 2)
                     ├── EEV Controller #3 (evaporator circuit 3)
                     └── EEV Controller #4 (evaporator circuit 4)
```

**Key point:** EEV controllers handle their own superheat PID loop internally at ~100ms speed. The ARM does NOT do real-time valve stepping. The ARM sends:
- Superheat setpoint (e.g., 10°F)
- ON/OFF command
- Cooling capacity limit (optional)

And reads back:
- Actual superheat
- Valve position (%)
- Suction temperature
- Suction pressure
- Alarms/faults

Same Modbus RTU pattern as relay modules — FC03/FC04 reads, FC06/FC16 writes.

**Separate bus recommended:** EEV controllers should be on their own RS485 bus (UART4) rather than sharing with relay modules. The EEV controllers poll more frequently (~1–2s for real-time superheat data display) and the data is time-critical for monitoring purposes.

### Energy Savings Analysis — 40 to 140 Ton Systems

EEV superheat control typically delivers **10–25% reduction in compressor energy** vs TXV, documented by Danfoss, Carel, and ASHRAE studies. The savings come from:
1. **Lower superheat setpoint** — TXV needs 12–15°F margin for stability; EEV controls accurately at 8–10°F → more evaporator surface used for cooling
2. **Faster load matching** — EEV responds in seconds vs minutes for TXV → less compressor cycling
3. **Better part-load efficiency** — EEV can modulate capacity; TXV is essentially on/off

**Assumptions for savings calculation:**
- Electricity cost: $0.10/kWh (US average commercial)
- EER (Energy Efficiency Ratio): 10 BTU/Wh for these system types
- Annual runtime: 6,000 hours (typical for vegetable storage — not year-round)
- Conservative efficiency gain: 15% (midpoint of 10–25% range)

| System Size | Annual Energy (TXV) | 15% Savings | Annual $ Saved | EEV Hardware Cost | Payback |
|---|---|---|---|---|---|
| **40 ton** | 40×12,000/10 = 48 kW × 6,000 hr = 288,000 kWh | 43,200 kWh | **$4,320/yr** | ~$3,000 (3 circuits) | **< 1 year** |
| **70 ton** | 84 kW × 6,000 hr = 504,000 kWh | 75,600 kWh | **$7,560/yr** | ~$4,500 (4 circuits) | **< 1 year** |
| **100 ton** | 120 kW × 6,000 hr = 720,000 kWh | 108,000 kWh | **$10,800/yr** | ~$6,000 (5 circuits) | **< 1 year** |
| **140 ton** | 168 kW × 6,000 hr = 1,008,000 kWh | 151,200 kWh | **$15,120/yr** | ~$8,000 (6 circuits) | **< 1 year** |

**At 40–140 ton scale, EEV pays for itself in under a year.** This is not a marginal improvement — it's $4,000–15,000/year in energy savings per installation.

### Additional Operational Savings

Beyond energy:

| Benefit | Estimated Value |
|---|---|
| **Reduced service calls** — superheat always in range, no manual TXV adjustment needed | 2–4 fewer calls/year × $200–400/call = $400–1,600/yr |
| **Extended compressor life** — proper superheat = no liquid slugging, no overheating | Avoids $5,000–20,000 compressor replacement |
| **Pump-down capability** — EEV closes fully for service, no manual valve needed | Reduced refrigerant loss during service |
| **Remote diagnostics** — superheat trending via UI catches problems before failure | Prevents emergency calls, crop loss |
| **Defrost optimization** — faster, more precise defrost cycles | 5–10% additional energy savings during defrost |

### What This Means for Agristar GRC

| Factor | Current (TXV + ON/OFF) | With Modbus EEV |
|---|---|---|
| **Superheat control** | ±5–10°F, no visibility | ±1°F, real-time display in UI |
| **Efficiency** | Baseline | 10–25% better |
| **Remote monitoring** | Temperature only | Superheat, pressure, valve position, fault codes |
| **Capacity modulation** | ON/OFF compressor staging | Continuous via valve + staging |
| **Service capability** | Manual TXV adjustment, no data | Remote setpoint, trending, diagnostics |
| **Firmware change** | None | Add EEV poll task to RTU master (~100 lines) |
| **Hardware change** | None | UART4 + MAX485 ($3) on ARM board + EEV controllers/valves at each evaporator |
| **Annual savings (100-ton)** | Baseline | **~$10,000–15,000** in energy + avoided service |

This is arguably the highest-ROI feature in the entire RTU migration — and it's only possible because the RS485 bus infrastructure is already being added for relay modules. The marginal cost is one more MAX485 chip and the EEV controllers at the evaporators.

---

## 12. Recommendations Summary

### Output Switching Technology
**Use the Waveshare 23406 EMR module.** At $30 for 8 relay outputs + 8 digital inputs with built-in Modbus RTU, it is the clear winner on cost, simplicity, and integration. EMI from 2A contactor coil switching is insignificant given the Waveshare's built-in protections (TVS diodes, optoisolation, differential RS485).

### Output Monitoring
**Wire contactor auxiliary contacts or current sensors back to the Waveshare DIs.** Implement stuck-output detection in firmware. This is the safety layer that matters — it catches both EMR weld and any other failure mode regardless of relay technology.

### Bus Architecture
**Keep VFD on Modbus TCP (FMBT-21) and add RS485 (UART3 + MAX485) for relay modules only.** Separate buses for separate timing requirements. Migrate to dual RS485 (Option C) when the FMBT-21 is eliminated in a future board revision.

### RS485 Cabling
**2-wire half-duplex (A + B + shield/GND) using Belden 9841 or equivalent.** 120Ω termination at each end of the bus. Shield drain wire grounded at master end only.

### UL 508A
**Evaluate the Waveshare once, document it, reuse forever.** Or use Advantech ADAM-4068 for the occasional UL panel if the evaluation paperwork isn't worth the effort. Both approaches are fully compliant with Agristar's existing 508A panel shop certification.

### SSR / Hybrid Consideration
**Not recommended for general-purpose outputs.** The cost is 4–10x higher for marginal EMI benefit. Zero-cross SSRs or hybrid relays are appropriate only if:
- Specific outputs need very high cycle rates (PID temperature control with <1s cycles)
- A specific installation has documented RS485 interference problems that shielded cable + TVS doesn't solve (unlikely)
- Customer specification requires SSR

If SSR is ever needed, use **zero-cross only** (never random-cross) — Crydom CKR2420 at ~$12/channel is the best value.

### EEV for GRC Refrigeration
**Highest-ROI upgrade in the entire RTU migration.** At 40–140 ton system sizes, EEV pays for itself in under a year with $4,000–15,000/year energy savings per installation. Use Danfoss AKVA 20-series valves with AK-CC 750A controllers or Carel E2V with EVD Evolution — both have excellent Modbus RTU support. Run on a separate RS485 bus (UART4) dedicated to EEV controllers. The GRC already uses the AS2 board — firmware integration is ~100 lines for the EEV Modbus poll task.
