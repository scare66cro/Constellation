# Agristar Constellation — Product Family Overview

**Date:** March 27, 2026
**Status:** Design Phase
**Parent:** Constellation-Control-Architecture.md

---

## 1. What Is Constellation?

Constellation is Agristar's next-generation distributed control platform, replacing the monolithic AS2 controller. Instead of one board doing everything, Constellation splits responsibility across a family of purpose-built cards connected over Ethernet. Each card has a celestial name and a specific role.

The current AS2 runs all control logic, all I/O, all sensor reading, all communication, and all safety on a single TM4C129 microcontroller. If any part fails, everything fails. Constellation eliminates this single point of failure by distributing the system across independent, field-replaceable modules.

---

## 2. The Family

```
┌─────────────────────────────────────────────────────────────────┐
│                     CONSTELLATION                                │
│                                                                  │
│   ┌──────────┐   ┌──────────┐   ┌──────────┐   ┌──────────┐   │
│   │          │   │          │   │          │   │          │   │
│   │   NOVA   │   │  ORBIT   │   │  PULSAR  │   │  TRITON  │   │
│   │  Brain   │   │   I/O    │   │  Stepper │   │  Refrig  │   │
│   │          │   │          │   │  Driver  │   │ Control  │   │
│   └──────────┘   └──────────┘   └──────────┘   └──────────┘   │
│                                                                  │
│   "The brain      "The hands     "The muscles    "The cold       │
│    that thinks"    that feel"     that move"      that chills"   │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

| Card | MCU | Role | Named For |
|---|---|---|---|
| **Nova** | AM2434 (4× Cortex-R5F @ 800 MHz) | Central controller — runs ALL control logic | A stellar explosion — the central intelligence |
| **Orbit** | STM32G474CE (Cortex-M4F @ 170 MHz) | General-purpose I/O module | Satellites orbiting the brain |
| **Pulsar** | STM32G474CE (Cortex-M4F @ 170 MHz) | Stepper motor actuator driver for doors/vents | Rotating star emitting precise pulses — step pulses |
| **Triton** | STM32G474CE (Cortex-M4F @ 170 MHz) | Refrigeration management card | Neptune's frozen moon — the coldest surface in the solar system |

---

## 3. Nova — The Brain

**One per site. Runs everything.**

Nova is the central controller. It runs every state machine, every PID loop, every failure check, every scheduling decision, and every alarm for every zone on the site. No other card in the system makes control decisions — Nova is the single source of truth.

### What It Does

- **State machine** — evaluates sensor data, switch positions, run clock schedules, and alarm status to determine the operating state (cooling, refrigeration, defrost, heating, recirculation, standby, shutdown, cure)
- **PID control** — runs five independent PID controllers per zone: doors, refrigeration, climacell, humidifier, burner
- **Equipment scheduling** — fan boost cycles, CO2 purge, defrost timing, temperature ramp programs, run clock evaluation
- **Failure detection** — monitors 20+ failure types per zone (fan fail, compressor fail, sensor fail, airflow restriction, burner fail, low/high temp limits)
- **Alarm management** — email alerts, UI notifications, alarm latching, failure mode enforcement
- **Settings storage** — all site configuration lives on Nova's QSPI flash with ping-pong CRC32 protection and triple backup (QSPI + RPi5 filesystem + Azure cloud)
- **Sensor data caching** — maintains a real-time cache of all Orbit/Pulsar/Triton register data, polled at 1 Hz over Modbus TCP
- **Runtime tracking** — fan hours, compressor hours, defrost cycles, per-mode runtimes
- **Cross-zone coordination** — shared compressor racks, demand prioritization across buildings
- **RPi5 bridge** — serves the existing UI via the same `^tag=value$CRC!` serial protocol

### Hardware Highlights

| Feature | Specification |
|---|---|
| Processor | TI AM2434 — 4× Cortex-R5F @ 800 MHz |
| Memory | 2.5 MB on-chip SRAM (no DDR — pure MCU) |
| Settings storage | QSPI flash, dual-sector ping-pong, CRC32 |
| Communication | Ethernet (Modbus TCP to all cards), UART (to RPi5) |
| Safety watchdog | Dedicated R5F core, MPU-isolated, cannot be corrupted by firmware bugs |

### Core Allocation

| Core | Role |
|---|---|
| R5F-0 | Control engine — all zone logic, all PID, all failure detection |
| R5F-1 | Communications — Modbus TCP polling, RPi5 UART bridge, data cache |
| R5F-2 | Safety watchdog — monitors R5F-0 and R5F-1 independently |
| R5F-3 | Spare — diagnostics, OTA orchestration, future expansion |

---

## 4. Orbit — General-Purpose I/O

**One or more per zone. Reads sensors, drives outputs, follows orders.**

Orbit is the general-purpose I/O card. It reads temperature/humidity/CO2 sensors via RS485, drives digital outputs (fans, heaters, burners, humidifiers, lights), sends VFD speed commands, and reports everything back to Nova. It does not make control decisions — it executes what Nova commands.

### What It Does

- **Sensor polling** — reads analog boards on Port A RS485 (temperature, humidity, CO2, IR temp) using the same protocol and conversion math as the current AS2
- **Digital outputs** — drives relays/contactors for fans, heaters, burners, humidifiers, cavity heat, aux outputs, bay lights via ULN2803A shift register outputs
- **VFD communication** — acts as Modbus RTU master on Port B RS485 to control variable frequency drives (ABB ACS380, etc.) for fan speed
- **Safety rules** — runs a small set of locally-stored safety rules at 200 ms that can only turn things OFF, never ON (e.g., if suction pressure drops below threshold, kill compressor — even if Nova is unreachable)
- **Comms-loss behavior** — if Nova stops talking, holds outputs for a configurable timeout, then drives to safe state (configurable per output)

### Hardware Highlights

| Feature | Specification |
|---|---|
| Processor | STM32G474CE — Cortex-M4F @ 170 MHz |
| Memory | 512 KB flash, 128 KB SRAM |
| Ethernet | W5500 hardwired TCP/IP (SPI), dual RJ45 with KSZ8863 3-port switch IC for daisy-chaining |
| RS485 ports | 2 — Port A (sensors), Port B (VFDs) |
| Digital outputs | 14–16 channels via 2× TI DRV8908 SPI-controlled protected drivers |
| Digital inputs | 14–16 channels via 2× 74HC165 shift registers |
| Output protection | Per-channel overcurrent limiting (microseconds), open-load detection, fault reporting to Nova |
| Address | DIP switch — set and forget |
| Local storage | ~224 bytes (safety rules + comms-loss config only) |

### Output Driver — DRV8908 (Replaces ULN2803A + Shift Registers + Fuses)

Each Orbit uses two TI DRV8908 SPI-controlled low-side drivers instead of the traditional 74HC595 shift register + ULN2803A + PTC fuse approach. The DRV8908 integrates all three functions into one chip:

```
Old design (6 chips + 14 fuses):              New design (2 chips, zero fuses):
  74HC595 → ULN2803A → [PTC] → Output           DRV8908 #1 → Outputs 0–7
  74HC595 → ULN2803A → [PTC] → Output           DRV8908 #2 → Outputs 8–13
  14× PTC fuses
  ~400 mm² board space                           ~70 mm² board space
```

**Why this matters:** With the old ULN2803A design, a single shorted relay coil trips the shared PTC fuse and kills ALL outputs — fans, heaters, everything. With DRV8908, each output has its own hardware current limit that trips in **microseconds** (not seconds like a PTC). A shorted humidifier only kills that one channel. The other 13 outputs keep running.

The DRV8908 also detects **open loads** — if a relay coil wire breaks, the chip reports it via SPI diagnostic readback. Nova can alarm "Output #7 open load — check wiring" before anyone notices the equipment isn't running.

| Feature | ULN2803A + PTC (old) | DRV8908 (new) |
|---|---|---|
| Overcurrent protection | PTC — seconds to trip, can damage driver | Hardware current limit — microseconds |
| Fault isolation | One short kills all outputs | Per-channel — other outputs unaffected |
| Fault reporting | None | Per-channel overcurrent + open-load via SPI |
| Board space (14 outputs) | ~400 mm² (6 chips + 14 fuses) | ~70 mm² (2 chips) |
| BOM cost | ~$3.00 | ~$5.00 |

### Daisy-Chain Ethernet

Every Orbit, Pulsar, and Triton has **two RJ45 ports** (IN and OUT) connected through a Microchip KSZ8863 3-port Ethernet switch IC (~$3). This allows daisy-chaining boards that share the same enclosure and power feed:

```
Nova ─── Main Switch ─── Orbit #1 ─── Orbit #2    (same enclosure, 6" patch cable)
              │
              └────── Triton #1 ─── Triton #2 ─── Triton #3    (compressor room)
```

One Ethernet cable from the main switch to the first board in each enclosure, then short patch cables between colocated boards. No need for a separate switch port per card.

**Rule:** Only daisy-chain boards on the same power feed. If a board in the middle loses power, downstream boards lose connectivity. In a compressor room where all Tritons share one electrical panel, this is a non-issue — if one loses power, they all do.

### Multiple Orbits Per Building

Nova's zone map can assign any number of Orbits to a zone. A single building might use:

```
Zone "Building A":
  Orbit #1 (DIP 01): fans, heater, cavity heat, bay lights, RS485 sensors
  Orbit #2 (DIP 02): humidifiers, climacell, burner, aux outputs, CO2 purge
  Pulsar #3 (DIP 03): doors
  Triton #4 (DIP 04): compressor circuit 1
```

Splitting equipment across two Orbits provides I/O headroom (20 outputs instead of 10), wiring simplification (put the second Orbit near distant equipment), and fault isolation (a shorted humidifier relay on Orbit #2 can't affect fan outputs on Orbit #1).

### Key Property: Every Orbit Is Identical

All Orbits run the same firmware binary. The DIP switch sets the Modbus address. Nova assigns the role (storage I/O, refrig I/O, door I/O) via the zone map in its QSPI flash. If an Orbit dies, you swap in a new one, set the DIP switch, and walk away — Nova pushes the safety rules automatically.

---

## 5. Pulsar — Stepper Motor Actuator Driver

**One per group of doors/vents. Replaces relay-pulsed gear motors with precision stepper control.**

Pulsar is the stepper motor driver card. It replaces the AS2's timed relay pulses with precise step-counted positioning using Trinamic TMC2226 driver ICs. Each channel knows exactly where its door is, detects stalls in milliseconds, retries automatically, and reports real-time position and load back to Nova.

### What It Does

- **Precision positioning** — translates Nova's PID output (0–100%) into exact step positions via calibrated range (N_total)
- **Sensorless calibration** — uses StallGuard to find mechanical end stops without limit switches (stall-to-stall sweep, ~75 seconds)
- **Stall detection** — TMC2226 StallGuard4 detects obstructions in <40 ms. Pulsar stops immediately, reports to Nova, which retries on configurable interval
- **Adaptive speed** — Nova sets speed per move: fast when far from setpoint, creep when close, full speed on mode transitions (cure window opens)
- **Torque/force limiting** — programmable motor current per context (gentle calibration, normal positioning, emergency close)
- **Load monitoring** — continuous StallGuard load value (0–1023) enables predictive maintenance and automatic wind gust handling via CoolStep
- **Independent fault handling** — one stalled door does not affect any other door. This is the single biggest improvement over the AS2.
- **Comms-loss failsafe** — Pulsar stores safe-state config in flash. If Nova goes silent, it autonomously drives doors to safe position (typically closed) using StallGuard for end-stop detection. No network required.

### Hardware Highlights

| Feature | Specification |
|---|---|
| Processor | STM32G474CE — Cortex-M4F @ 170 MHz (same as Orbit) |
| Driver IC | TMC2226-SA — 2.8A RMS (3.0A with thermal headroom), StallGuard4, CoolStep, StealthChop |
| Variants | 4-channel (general) or 8-channel (stepper-only) |
| Motor | NEMA 23 IP65 (23IP65-20) — 5.0A rated, 2.0 Nm, driven at TMC2226 3.0A limit (1.2 Nm) |
| Gearbox | NMRVS30-G50-D11 — 50:1 self-locking worm, NEMA 23 face mount |
| Power | 24V DC (commodity DIN-rail supply) |
| Linkage | M1.5 rack and pinion (1200mm rack, 17T pinion) — 614 lbs force at 3.0A, 500 lb design load |
| Position resolution | 256 microsteps × 50:1 gear = 0.000016% per microstep |
| Per-actuator cost | ~$210 per door complete with IP65 motor, gearbox, and rack-and-pinion linkage |

### What It Replaces

| AS2 (Current) | Pulsar |
|---|---|
| Timed relay pulses → gear motor | Stepper motor with TMC2226 driver |
| Position estimated from pulse time | Position exact from step count |
| No stall detection | StallGuard detects in milliseconds |
| One stalled door blocks all stages | Each door independent |
| Full-close initialization on every boot | Sensorless auto-calibration |
| No load feedback | Continuous load value + 30-day trending |

**Full design: [Pulsar-Stepper-Actuator-Design.md](Pulsar-Stepper-Actuator-Design.md)**

---

## 6. Triton — Refrigeration Management

**One per compressor rack or refrigeration circuit. Manages the cold side.**

Triton is the dedicated refrigeration control card. It executes Nova's compressor staging commands, manages defrost sequences locally, monitors suction/discharge pressure and temperature, and provides the safety interlocks that protect compressors from damage.

### What It Does

- **Compressor staging** — drives individual compressor stage contactors based on Nova's PID output and stage On/Off thresholds
- **Defrost execution** — when Nova triggers a defrost cycle, Triton manages the local sequence: shut down compressors, energize defrost outputs (heaters or hot gas valves), monitor termination conditions, restart compressors with anti-short-cycle delay
- **Pressure monitoring** — reads suction and discharge pressure transducers (4–20mA). Reports values to Nova and enforces local safety cutouts (low suction, high discharge) even if Nova is unreachable
- **Temperature monitoring** — reads evaporator coil temp, condenser temp, superheat/subcooling sensors via RS485 analog boards
- **Condenser fan control** — stages condenser fans based on discharge pressure or ambient temperature (head pressure control)
- **Liquid line solenoid** — controls refrigerant flow solenoid valves, coordinates with compressor staging to prevent liquid slugging
- **Safety interlocks** — local safety rules running at 200 ms: anti-short-cycle timers (minimum off time between compressor starts), high-pressure lockout, low-pressure cutout, oil pressure differential, motor overload monitoring
- **Diagnostic mode** — individual stages can be forced on/off from the UI for troubleshooting (auto-reverts after 60 minutes)
- **Comms-loss behavior** — if Nova goes silent, Triton holds current staging for configurable timeout, then shuts down compressors in sequence (reverse-order staging) to protect equipment

### I/O Requirements Per Triton

Each Triton manages one compressor circuit and needs:

| I/O Type | Count | Examples |
|---|---|---|
| **RS485 analog boards** | 2 | Suction/discharge pressure transducers, coil temps, superheat/subcooling, ambient |
| **Digital outputs** | ~10 | Compressor contactor, condenser fans (2–3), defrost heater/hot gas valve, liquid line solenoid, crank case heater, oil return solenoid |
| **Digital inputs** | ~10 | Compressor status feedback, high-pressure switch, low-pressure switch, oil differential, motor overload, defrost termination thermostat |

This is the same I/O footprint as a Storage Orbit — same DRV8908 output drivers, same 74HC165 input shift registers, same RS485 for analog boards. **Triton is an Orbit assigned to a compressor circuit.** The hardware is identical. The only difference is what safety rules Nova pushes to it.

### Hardware Highlights

| Feature | Specification |
|---|---|
| Processor | STM32G474CE — Cortex-M4F @ 170 MHz (same as Orbit) |
| Ethernet | W5500 hardwired TCP/IP (SPI), dual RJ45 with KSZ8863 for daisy-chaining |
| RS485 | Port A for pressure/temperature sensors (2 analog boards) |
| Digital outputs | ~10 via DRV8908 — per-channel protected, fault reporting |
| Digital inputs | ~10 via 74HC165 — compressor status, pressure switches, overloads |
| Address | DIP switch |

A compressor room with 4 circuits uses 4 Tritons daisy-chained in the same enclosure — one Ethernet cable in, three 6-inch patch cables between boards.

### Why a Separate Card?

The AS2 manages refrigeration through the same control loop and the same I/O as everything else — fans, doors, heaters, sensors all share one MCU. A compressor starting draws 6–8× rated current for several seconds. If the firmware hangs or the control loop stalls during a compressor start, there's no independent protection.

Triton isolates refrigeration onto its own card with its own independent safety rules. If Nova crashes or the network goes down, Triton's local safety logic keeps running at 200 ms — anti-short-cycle timers still enforce, high-pressure cutouts still fire, the compressor is protected. This is the same "safety rules can only turn things OFF" principle as Orbit, but applied specifically to the most expensive and most failure-sensitive equipment on the site.

---

## 7. How They Work Together

### 7.1 Network Topology

```
        ┌─────────────────────────────────────────────────┐
        │                     RPi5                         │
        │  lighttpd + UI + bridge + history + alerts       │
        └──────────────────────┬──────────────────────────┘
                               │ UART (serial protocol)
        ┌──────────────────────┴──────────────────────────┐
        │                     NOVA                         │
        │  All control logic · PID · state machines        │
        │  Settings vault · Alarm manager · Data logger    │
        └──────────────┬──────────────────────────────────┘
                       │ Ethernet (Modbus TCP, 1 Hz poll)
          ┌────────────┼────────────┬────────────┐
          │            │            │            │
     ┌────┴────┐  ┌───┴────┐  ┌───┴────┐  ┌───┴────┐
     │  ORBIT  │  │ ORBIT  │  │ PULSAR │  │ TRITON │
     │ Storage │  │ Aux/VFD│  │ Doors  │  │ Refrig │
     │   I/O   │  │   I/O  │  │ 4 or 8 │  │Compress│
     └─────────┘  └────────┘  └────────┘  └────────┘
         │             │           │            │
     Sensors,      VFDs,       Stepper      Compressors,
     fans,         lights,     motors,      defrost,
     heaters,      humidifiers gearboxes,   condensers,
     burners       aux relays  doors/vents  solenoids
```

### 7.2 Communication Flow

1. **Nova polls every card** at 1 Hz over Modbus TCP — reads all input registers (sensors, positions, status, load), writes all holding registers (output commands, speed targets, position targets)
2. **Each card executes locally** — Orbit drives its relays, Pulsar drives its steppers, Triton drives its contactors
3. **Safety rules run independently** at 200 ms on every card — can shut things off even if Nova is unreachable
4. **Nova bridges to RPi5** via UART — the existing Svelte UI sees the same `^tag=value$CRC!` protocol with no changes

### 7.3 Example: Potato Storage Site

```
Site: 2 storage buildings, shared compressor rack

Nova ── Ethernet Switch ─┬─ Orbit #1  (Building A sensors, fans, heater)
                         ├─ Orbit #2  (Building A aux, VFDs, humidifier)
                         ├─ Pulsar #3 (Building A doors — 4 channels)
                         ├─ Orbit #4  (Building B sensors, fans, heater)
                         ├─ Orbit #5  (Building B aux, VFDs, humidifier)
                         ├─ Pulsar #6 (Building B doors — 4 channels)
                         └─ Triton #7 (Shared compressor rack — 4 stages + defrost)
```

Nova runs two zone state machines (Building A, Building B) and coordinates refrigeration demand across both — if Building A needs 80% cooling and Building B needs 40%, Nova stages compressors to satisfy both, prioritizing the zone with the largest temperature error.

### 7.4 Example: Onion Curing Bay

```
Site: 1 curing bay, 8 doors, gas burner, VFD fans

Nova ── Ethernet Switch ─┬─ Orbit #1  (Bay sensors, burner, fans, climate)
                         ├─ Pulsar #2 (Bay doors — 8-channel variant)
                         ├─ Orbit #3  (VFDs — 8× ACS380 on RS485)
                         └─ Triton #4 (Refrigeration — if post-cure cold storage)
```

When cure conditions are met (outside temp + humidity thresholds), Nova writes `SPEED_MAX` to all 8 Pulsar channels simultaneously. All 8 doors open in seconds. If door #5 stalls on debris, doors #1–4 and #6–8 still reach full open. Nova alarms on #5, retries every 10 seconds, and it self-clears when the obstruction moves.

---

## 8. Network Architecture — Why Two Subnets Are Required

### 8.1 The Problem: One Flat Subnet Doesn't Scale

The instinct is to put everything on one network — RPi5, Nova, all cards, all sites — so a tech can plug in anywhere and see everything. This feels simpler. It is not.

**AgriNW scenario:** 40 sites. Each site has 1 Nova, 1 RPi5, and 5–12 cards (Orbits, Pulsars, Tritons). On a single flat subnet:

| Resource | Count | Problem |
|---|---|---|
| RPi5s | 40 | Each needs a unique IP — manageable |
| Novas | 40 | Each needs a unique IP — still manageable |
| Cards | 200–480 | Each needs a unique IP — **now you have a problem** |
| **Total devices** | **280–560** | Exceeds a /24 subnet's 254 usable addresses |

Even a /23 (510 addresses) barely fits and leaves no room for growth. And this assumes perfect coordination — every DIP switch on every card across 40 sites must be unique. One mistake and two Orbits answer the same address. The wrong building starts cooling.

**Worse: Modbus address conflicts.** Every Nova polls its cards by DIP switch address. If Site A's Orbit #1 (DIP 01) and Site B's Orbit #1 (DIP 01) are on the same subnet, Nova A's Modbus poll hits both. One responds first and the other gets ignored, or worse, both respond and the data is corrupted. Sensor readings from Building A bleed into Building B's control loop. Fan commands for potatoes go to onions.

**Worse still: Broadcast storms.** 560 devices on one Layer 2 domain means every ARP request hits every device. The W5500 on each card has a small packet buffer — it can be overwhelmed by broadcast traffic from 559 other devices it doesn't care about.

### 8.2 The Solution: Isolated Card Bus Per Nova

Every Nova gets its own physically isolated Ethernet segment. The RPi5 connects to the site LAN (or directly to the internet). The two networks never touch — they're connected by a UART serial cable, not Ethernet.

```
Site LAN / Internet (unique IPs per RPi5)      Card Bus (always 192.168.1.x)
══════════════════════════════════════          ════════════════════════════════

  AgriNW corporate network                     Nova A's bus (isolated wire)     
  or farmer's router                           ┌─────────────────────────────┐
  or 4G modem                                  │ Nova A:  192.168.1.1        │
       │                                       │ Orbit 1: 192.168.1.11       │
  ┌────┴─────┐                                 │ Orbit 2: 192.168.1.12       │
  │  RPi5 A  │── UART (serial, not IP) ──────│ Pulsar:  192.168.1.13       │
  └──────────┘                                 │ Triton:  192.168.1.14       │
       │                                       └─────────────────────────────┘
       │ (same site LAN)
       │                                       Nova B's bus (separate wire)
  ┌────┴─────┐                                 ┌─────────────────────────────┐
  │  RPi5 B  │── UART (serial, not IP) ──────│ Nova B:  192.168.1.1        │
  └──────────┘                                 │ Orbit 1: 192.168.1.11       │
                                               │ Orbit 2: 192.168.1.12       │
                                               │ Triton:  192.168.1.14       │
                                               └─────────────────────────────┘
```

**Every Nova uses the exact same IP scheme:**
- Nova: `192.168.1.1`
- Card at DIP 01: `192.168.1.11`
- Card at DIP 02: `192.168.1.12`
- Card at DIP N: `192.168.1.(10 + N)`

40 Novas all running `192.168.1.1` simultaneously — no conflict, because each is on its own physical wire. No IP planning. No coordination between sites. Set the DIP switch, plug in, it works.

**IPs consumed on the site LAN:** Just the RPi5s. 40 RPi5s = 40 addresses. Leaves 214 addresses free for the farmer's phones, laptops, cameras, tractors, grain dryers, etc.

### 8.3 Why Not Just VLANs?

VLANs solve the collision problem but create a management nightmare:

- Every switch must be a managed switch (~$150+ vs $20 unmanaged)
- Each site needs VLAN tagging configured correctly — by someone who understands VLANs
- Every port assignment is a potential failure point
- A tech replacing a switch must replicate the VLAN config
- AgriNW's IT person (if they have one) must coordinate VLANs across 40 sites

The UART boundary achieves the same isolation with zero configuration, zero managed switches, and zero IT involvement. It's a physical air gap, not a logical one — it cannot be misconfigured.

### 8.4 The Tech Access Problem

The two-subnet architecture means a tech standing at the Triton panel in the compressor room cannot reach the RPi5's Svelte UI by plugging into the card bus. The card bus is `192.168.1.x`, the RPi5 is on a different physical network, and there's no IP route between them.

**This is a real problem.** Techs need to see live data while standing in front of the equipment they're servicing — suction pressure while adjusting a TXV, compressor status while replacing a contactor, door position while checking a stall.

### 8.5 Solutions for Tech Access at the Device

#### Solution A: RPi5 Wi-Fi Access Point (control room and nearby)

The RPi5 runs `hostapd` as a local Wi-Fi access point. Fixed SSID (e.g., `Agristar-SiteA`), WPA2 password on a sticker inside the main panel.

```
RPi5 ))) Wi-Fi AP: "Agristar-SiteA" ((( Tech's phone
192.168.4.1                               → browser → 192.168.4.1 → full Svelte UI
```

- Works within ~30 feet of the RPi5 — covers the control room
- Tech's phone connects automatically if they've joined before
- Full UI — all sensors, all outputs, all settings, all alarms
- **Limitation:** Won't reach through metal siding + 10" concrete to the condenser pad outside. The RPi5 is typically in the control room enclosure, and compressors are on the other side of a structural wall.

#### Solution B: 4G Cell Service (outside the building — truck, condenser pad, anywhere)

Most sites have cell service. The tech's phone is already in their pocket.

```
Tech's phone (4G) ─── cloud relay ─── RPi5 ─── UART ─── Nova
   anywhere              Azure         site             card bus
```

Each RPi5 maintains an outbound connection to a cloud relay (Azure IoT Hub, MQTT broker, or simple WebSocket tunnel). The tech connects through the cloud from any device with internet access.

- **Works at the condenser pad** — standing outside the concrete wall, phone pulls up the full Svelte UI over 4G
- **Works from the truck** — tech drives up, pulls out their phone, already connected before they walk inside
- **Works from the office** — dispatch checks all 40 sites from one dashboard
- **Works from home** — after-hours alarm response, remote diagnostics
- **No farmer IT involvement** — RPi5 connects outbound on port 443, works behind any NAT
- **No VPN** — too complex for field techs, breaks on network changes
- **No port forwarding** — never touch the farmer's router

For sites with no internet at all: the tech's 4G hotspot can serve as the RPi5's upstream connection. RPi5 joins the tech's hotspot SSID, tech's phone hits `rpi5.local` or a known IP. The tech brings their own backhaul.

**Limitation:** Requires cell service. Most agricultural sites have it, but not all. Dead zones exist. Also adds latency — the round trip through the cloud adds 50–200 ms vs local access. Not a problem for viewing data, but noticeable if forcing outputs.

#### Solution C: Cable Run from RPi5 Switch to Remote Location (primary on-site solution)

The best answer is the simplest one: run an Ethernet cable from the RPi5's switch to wherever the tech needs UI access. Put a small unmanaged switch or an RJ45 wall jack in the compressor room, the door motor enclosure, or anywhere they frequently service.

```
Control room                              Compressor room
┌────────────┐                            ┌──────────────────┐
│   RPi5     │                            │  RJ45 jack       │
│     │      │   single Ethernet run      │     │            │
│  Switch ───┼────────────────────────────┼─────┘            │
│            │   (along existing conduit) │  Tech plugs in   │
└────────────┘                            │  laptop or phone  │
                                          │  via USB-C adapter│
                                          └──────────────────┘
```

- **Full Svelte UI** — same as the control room, same subnet, same web server, all pages (GRC, doors, storage control, refrigeration, alarms, settings)
- **Zero latency** — direct Ethernet to RPi5, no cloud round-trip
- **Permanent installation** — run the cable once during commissioning, it's always available
- **Cheap** — $10 in cable and a keystone jack
- **Works without internet, without Wi-Fi, without cell service**
- **No load on Nova** — the web server runs on the RPi5 where it belongs. Nova keeps 100% of its 2.5 MB SRAM for control loops, PID, Modbus polling, and safety.

The conduit from the control room to the compressor room already carries power and signal wires — one more Ethernet cable costs nothing. For a multi-building site, run one cable per building. A 4-port unmanaged switch ($15) at each remote location covers a tech's laptop plus future expansion.

```
Example: 2-building potato storage with shared compressor rack

RPi5 Switch (control room)
    ├── RPi5 (Ethernet, site LAN / internet)
    ├── Cable run → Building A panel (RJ45 jack)
    ├── Cable run → Building B panel (RJ45 jack)
    └── Cable run → Compressor room (RJ45 jack or small switch)
```

A tech working on compressor #3 plugs their laptop into the compressor room jack, opens the RPi5's IP in a browser, and sees live suction pressure, staging status, defrost schedule, fault history — all the pages, not a stripped-down view. They adjust a TXV while watching the pressure reading update in real time.

**Why not put the web server on Nova instead?** Nova has 2.5 MB SRAM and needs to run up to 40 control loops, 40 PID controllers, Modbus TCP polling for all cards, safety monitoring, alarm management, and settings storage. The compiled Svelte SPA alone is 200–500 KB — that's 10–20% of Nova's total RAM before counting the HTTP stack, connection buffers, and API handlers. The RPi5 has 8 GB of RAM and runs Linux — it was built to serve web pages. Nova was built to control equipment. A $10 cable run keeps each device doing what it's good at.

#### Recommended Deployment: A + B + C

| Method | Where It Works | What It Shows | Requires | Best For |
|---|---|---|---|---|
| **A — RPi5 Wi-Fi AP** | Within ~30 ft of control room | Full Svelte UI | Phone/tablet | Quick check, settings changes |
| **B — 4G cell service** | Anywhere with cell coverage | Full Svelte UI | Phone + 4G | Truck, outside, remote, office |
| **C — Cable run from RPi5** | Any fixed remote location | Full Svelte UI | Pre-installed cable ($10) | Compressor room, remote panels |

A tech arriving at a site:
1. **Driving up:** checks the site on their phone via 4G (Solution B) — sees alarms, current state
2. **In the control room:** connects to the RPi5 Wi-Fi AP (Solution A) — full UI on their phone
3. **At the compressor pad:** plugs into the pre-run cable jack (Solution C) — full UI, zero latency, works in a Faraday cage
4. **Leaving the site:** checks status one more time from the truck via 4G (Solution B)

### 8.6 What the Boss's Flat-Subnet Approach Breaks

For reference, here's what happens if all devices share one Ethernet subnet:

| Problem | Impact | Severity |
|---|---|---|
| **IP exhaustion** | 40 sites × 12 cards = 480 cards + 40 RPi5s + 40 Novas = 560 devices. Exceeds /24 (254 max). | Fatal |
| **DIP address collision** | Site A Orbit #1 and Site B Orbit #1 share the same Modbus address on the same wire. Nova A gets responses from both. | Fatal |
| **Cross-site data corruption** | Sensor readings from Building A leak into Building B's control loop. Wrong equipment turns on. | Fatal |
| **Broadcast storms** | 560 devices ARPing on one L2 domain. W5500's 8KB buffer overflows. Cards miss Modbus polls. | High |
| **Site-wide single point of failure** | One misconfigured device (IP conflict, broadcast loop) takes down all 40 sites simultaneously. | High |
| **DIP switch coordination** | Every card across 40 sites needs a globally unique DIP setting. One overlap = cross-site control. | Unmanageable |
| **Switch infrastructure** | Need managed switches + VLANs + an IT person who understands L2 segmentation at every site. | Expensive |
| **Security** | Every card on every site reachable from every other site. One compromised device = access to all. | High |

The two-subnet architecture eliminates every one of these problems. The UART boundary between RPi5 and Nova is the key — it's a physical air gap that provides network isolation, security, and unlimited scaling with zero configuration.

### 8.7 Summary: Network Rules

1. **Card bus is always `192.168.1.x`** — same addresses on every site, no coordination
2. **Card bus is physically isolated** — no route to site LAN, no route to internet
3. **RPi5 connects to site LAN / internet** — the only device that touches the outside world
4. **Nova ↔ RPi5 is UART** — serial cable, not Ethernet, cannot be misconfigured
5. **40 sites = 40 isolated card buses + 40 RPi5s on the site LAN** — scales indefinitely
6. **Tech access in the control room: RPi5 Wi-Fi AP** — phone connects, full UI
7. **Tech access outside / remote: 4G cell service** — phone over cloud relay, full UI
8. **Tech access at the equipment: cable run from RPi5 switch** — one-time install, full UI, works everywhere
9. **Web server stays on RPi5 only** — Nova's 2.5 MB SRAM is reserved for control loops, not HTTP
10. **Never bridge the card bus to the site LAN** — the UART boundary is a security feature

---

## 9. Replacement and Field Service

The "all Orbits are dumb" architecture means every card except Nova is disposable and field-replaceable with zero configuration:

| Card Dies | Replacement Procedure |
|---|---|
| **Orbit** | Swap board, set DIP switch to same address, power up. Nova auto-pushes safety rules. Done. |
| **Pulsar** | Swap board, set DIP switch. Nova pushes config and stored N_total. Single re-home (~25s) instead of full calibration. Done. |
| **Triton** | Swap board, set DIP switch. Nova pushes safety rules and staging config. Anti-short-cycle timers restart. Done. |
| **Nova** | Swap board. Upload settings JSON from RPi5 (one-button restore) or Azure cloud. All cards resume automatically. |
| **RPi5** | Flash new SD from golden image. Nova still controls everything — RPi5 is UI only. |

No firmware update needed. No commissioning wizard. No laptop required (except Nova, where you restore settings). A service tech with a screwdriver and a spare board can replace any card in under 5 minutes.

---

## 10. Cost Architecture

| Card | MCU | Approx Board Cost | Role |
|---|---|---|---|
| **Nova** | AM2434 | ~$85–120 | One per site |
| **Orbit** | STM32G474CE + W5500 + 2× DRV8908 | ~$28–38 | One or more per building |
| **Pulsar** (4-ch) | STM32G474CE + 4× TMC2226 | ~$42–50 | One per 4 doors/vents |
| **Pulsar** (8-ch) | STM32G474CE + 8× TMC2226 | ~$57–65 | One per 8 doors/vents |
| **Triton** | STM32G474CE + W5500 + 2× DRV8908 | ~$28–38 | One per compressor circuit (2–6 per rack) |

A typical potato storage site (2 buildings, 4 doors each, 4-compressor shared rack):
- 1 Nova + 4 Orbits + 2 Pulsars + 4 Tritons = **11 cards total**

**Orbit and Triton are the same physical board** — same PCB, same components, same firmware. The only difference is which safety rules Nova pushes to them. Stock one board, deploy as either role. Pulsar is the only variant with different hardware (TMC2226 stepper drivers instead of DRV8908 output drivers).

The AS2 equivalent: 2 AS2 controllers ($8,000) + 2 door controllers ($6,000) + 1 refrig controller ($4,000) = **$18,000**. Constellation electronics for the same site: ~**$600**. Even after enclosures, power supplies, wiring, assembly, and margin, the cost gap is significant — and Constellation adds stall detection, load monitoring, per-output fault isolation, independent fault handling, and predictive maintenance that the AS2 cannot do at any price.

---

## 11. Summary Table

| | Nova | Orbit | Pulsar | Triton |
|---|---|---|---|---|
| **Full name** | Nova | Orbit | Pulsar | Triton |
| **Named for** | Stellar explosion | Satellite orbit | Rotating star (precise pulses) | Neptune's frozen moon |
| **MCU** | AM2434 (4× R5F @ 800 MHz) | STM32G474CE (M4F @ 170 MHz) | STM32G474CE (M4F @ 170 MHz) | STM32G474CE (M4F @ 170 MHz) |
| **Role** | Central brain — all control logic | General-purpose I/O | Stepper motor actuator driver | Refrigeration management |
| **Makes decisions?** | Yes — all of them | No | No | No |
| **Quantity per site** | 1 | 2–8 (varies by site) | 1–4 (varies by door count) | 1–2 (per compressor rack) |
| **Drives** | Nothing directly — commands all cards | Relays, VFDs, sensors | Stepper motors + gearboxes | Compressors, defrost, condensers |
| **Output protection** | N/A | DRV8908 per-channel OCP + open-load | TMC2226 per-channel current limit | DRV8908 per-channel OCP + open-load |
| **Same hardware as Orbit?** | No (AM2434) | — | No (TMC2226 drivers) | **Yes — identical board** |
| **Key safety feature** | Independent watchdog core (R5F-2) | Local safety rules (200 ms) | Autonomous failsafe close | Anti-short-cycle, pressure cutouts |
| **Field replacement** | Restore settings from RPi5/cloud | Set DIP, power up, done | Set DIP, 25s re-home, done | Set DIP, power up, done |
| **Unique capability** | Cross-zone coordination | Sensor polling, VFD bus | StallGuard stall detection, load trending | Defrost sequencing, pressure monitoring |
