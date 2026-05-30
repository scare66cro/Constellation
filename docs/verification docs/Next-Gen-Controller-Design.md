# Agristar Constellation — System Design Document

**Date:** March 24, 2026
**Status:** Design Phase
**Replaces:** TM4C129ENCPDT + CPLD shift register architecture

---

## 1. Executive Summary

### 1.1 Platform Overview

This document defines the **Agristar Constellation** platform — the next-generation Agristar controller system.

| Name | Component | Description |
|---|---|---|
| **Constellation** | The platform | The complete system — brain, network, and all I/O modules working as one |
| **Nova** | AM2434 brain board | The central controller — runs all control logic, talks to every module |
| **Orbit** | I/O expansion modules | 24VDC-powered field modules — 10DO/10DI/2AO + dual RS485 gateways |

A single **Nova** controller manages multiple independent **zones** through **Orbit** I/O modules connected over a Modbus TCP star network. A zone is a logical grouping — typically one cold storage room — that can include:

| Zone component | Orbit modules | Role |
|---|---|---|
| **Storage control** | 1× Orbit | Ventilation fans, humidifiers, heaters, dampers, analog boards (Port A) |
| **Door control** | 1× Orbit (optional) | Fresh air dampers, door actuators, position feedback, interlocks. Shared across rooms or dedicated per room. |
| **Refrigeration** | 1–4× Orbit | One per compressor system. Each hosts VFD/EEV on Port B (Modbus RTU, 19200 baud). |

A typical 2-room site with 4 compressors uses 7 Orbits: 2 storage + 1 door + 4 refrigeration. All modules are the same hardware — zone assignment is a configuration-time mapping in Nova's config flash.

Each Orbit module carries **three simultaneous communication channels** on one Cat5 cable:

| Channel | Hardware on Module | Function |
|---|---|---|
| Modbus TCP (Ethernet) | W5500 via SPI | Brain reads/writes all DI, DO, AO registers |
| RS485 Port A (USART1) | MAX3485 #1 | Analog boards — Modbus RTU master, polls up to 32 boards (9600 baud). Sensor data stored in Orbit registers 0x0700+. |
| RS485 Port B (USART2) | MAX3485 #2 | VFDs and EEVs — Modbus RTU (19200 baud) |

The design eliminates the CPLD shift register, eliminates dedicated homerun wiring to every field device, eliminates the FMBT-21 Modbus TCP gateway per VFD ($200 each), and replaces physical panel-door A/O/M switches with a software Equipment Control page — while keeping a hardwired E-stop for code compliance.

---

## 2. Processor Selection

### 2.1 Requirements

| Requirement | Minimum | Preferred |
|---|---|---|
| UART count | 2 (debug + RPi5) — all field I/O over Ethernet | 9+ (spare UARTs for retrofit, future expansion) |
| Ethernet | 1× 10/100 or 10/100/1000 | Gigabit with hardware MAC |
| SRAM | 256 KB | 2 MB+ |
| Flash | External QSPI/OSPI | 8–64 MB (board design choice) |
| CPU cores | 1 | 2–4 (role-per-core isolation) |
| Architecture | Cortex-R or Cortex-M (real-time) | Cortex-R5F (deterministic, ECC SRAM) |
| FreeRTOS support | Required | TI MCU+ SDK preferred (direct port from TM4C) |
| Industrial temp | -40°C to +85°C | -40°C to +125°C |
| CAN FD | Optional | 2× MCAN (future multi-board networking) |
| ECC on SRAM | Required | Hardware single-bit correct, double-bit detect |
| Safety certification | Preferred | IEC 61508 SIL-2 capable, AEC-Q100 |
| Production lifecycle | 10+ years | 15+ years (TI industrial commitment) |

### 2.2 Recommended: TI AM2434 (~$22–30)

**Primary choice.** 4× Cortex-R5F @ 800 MHz, 2 MB ECC SRAM, 9 UARTs, PRU-ICSS, 2× MCAN (CAN FD), CPSW Gigabit Ethernet, OSPI/QSPI.

| Feature | Value |
|---|---|
| Cores | 4× Cortex-R5F @ 800 MHz |
| SRAM | 2 MB with hardware ECC |
| UARTs | 9 |
| Ethernet | CPSW 3-port Gigabit (2 external MACs) |
| CAN | 2× MCAN (CAN FD) |
| Flash interface | OSPI (up to Octal SPI, 200 MHz) |
| PRU-ICSS | 2× PRU cores for custom protocols or software UARTs |
| Safety | IEC 61508 SIL-2 capable, AEC-Q100 automotive |
| SDK | TI MCU+ SDK — FreeRTOS, lwIP, Modbus, drivers |
| Temperature | -40°C to +125°C (industrial/automotive) |
| Package | 324-pin BGA (17×17mm) |

**Why AM2434:**
- Same MCU+ SDK as all Sitara AM2x parts — firmware ports without changes
- 9 UARTs means plenty of RS485 buses for VFDs, EEVs, analog boards
- Gigabit Ethernet with hardware TCP/IP offload for Modbus TCP I/O modules
- 4 R5F cores enable role-per-core architecture (control, comms, safety watchdog, spare)
- ECC SRAM catches bit-flip errors — automotive/aviation-grade memory integrity
- MPU per core — safety watchdog on R5F-2 can't be corrupted by firmware bugs on R5F-0
- 15+ year TI industrial production commitment

### 2.3 Fallback: TI AM2634 (~$15–20)

If the AM2434 is overkill or cost-sensitive:

| Feature | AM2634 | AM2434 |
|---|---|---|
| Cores | 2× R5F @ 400 MHz | 4× R5F @ 800 MHz |
| SRAM | 2 MB ECC | 2 MB ECC |
| UARTs | 6 | 9 |
| Ethernet | CPSW Gigabit | CPSW Gigabit |
| CAN | 2× MCAN | 2× MCAN |
| PRU | None | 2× PRU |
| SDK | Same MCU+ SDK | Same MCU+ SDK |

Meets all current requirements with 6 UARTs. Limits: zero spare UARTs, 2 cores instead of 4, half the clock speed. Code compiles without changes from AM2434 — same SDK.

### 2.4 Alternative: STM32H755 (~$15–22)

If TI supply chain becomes an issue:

| Feature | STM32H755 |
|---|---|
| Cores | 1× Cortex-M7 @ 480 MHz + 1× Cortex-M4 @ 240 MHz |
| SRAM | 1 MB |
| UARTs | 8 |
| Ethernet | 10/100 Mbps (no Gigabit) |
| CAN | 2× CAN FD |
| SDK | STM32CubeMX + FreeRTOS |

Requires 2–3 weeks to port (different HAL, same FreeRTOS). Largest MCU community in the world. 100 Mbps Ethernet is sufficient for Modbus TCP (packets are tiny).

### 2.5 Multi-Core Architecture (AM2434)

Cores are assigned by **role**, not by zone:

| Core | Role | Why |
|---|---|---|
| R5F-0 | All control logic, all zones, Modbus TCP client, Modbus RTU client | One core handles everything — 4 zones at ~2% CPU. Single-core FreeRTOS is simpler to develop and debug. |
| R5F-1 | RPi5 communication bridge | Isolated from control loop — a chatty/stuck RPi5 link can't stall compressor control. Handles UART RX/TX, packet framing, CGI cache, OTA update receiver. |
| R5F-2 | Safety watchdog | Boots a minimal image. Monitors R5F-0 heartbeat. If heartbeat stops → forces all watchdog-enabled outputs to safe state. Independent — MPU prevents cross-core memory corruption. |
| R5F-3 | Spare / future | Sleep, or CAN FD for multi-board networking. |

### 2.6 Memory Architecture

**External QSPI NOR Flash (8–64 MB, on-board):**

| Technique | Purpose |
|---|---|
| Dual-image A/B | Two firmware copies. Write upgrade to inactive slot. Flip active flag only after CRC verify. Power loss mid-write → reboot into good slot. |
| CRC32 at boot | SBL (Secondary Boot Loader) checks CRC before jumping. Corrupt → fall back to other slot. Built into MCU+ SDK. |
| Config ping-pong | 2 copies of runtime config. Write copy B → verify → mark active. Power loss at any step leaves one valid copy. |
| Hardware ECC | OSPI controller has built-in ECC on reads. |
| WP# pin | Flash write-protect tied to GPIO. Locked 99.9% of the time. Only unlocked during firmware upgrade or config write. |
| Hold-up capacitors | 100µF + 10µF near flash VCC. Provides 1-2 ms to complete any in-progress page write during power loss. |

**Real-Time Clock (RTC):**

| Component | Specification | Purpose |
|---|---|---|
| DS3231MZ+ | I2C, ±2 ppm, -40°C to +85°C, SO-8 package | Battery-backed real-time clock for wall-clock time independent of RPi5 |
| CR2032 coin cell holder | Standard through-hole, accessible with lid removed | Maintains RTC during power loss — weeks to months of backup |
| I2C bus | AM2434 I2C0 (shared with other on-board peripherals) | 2-wire connection, 100/400 kHz |

**Why an RTC on Nova:**
- Defrost scheduling, timestamped event logs, and alarm records require wall-clock time
- RPi5 provides NTP-synced time via UART1, but if the RPi5 is rebooting, offline, or failed, Nova loses its clock
- DS3231 maintains time on coin cell for weeks — Nova always knows the time at boot
- Nova syncs the RTC from RPi5 periodically (once per hour); the RTC drifts < 1 second/day
- Nova also writes the current time to each Orbit module via Modbus holding registers 0x0600–0x0601 (~once per minute), giving every module in the system a synchronized clock for local event timestamps

**On-Chip SRAM (2 MB with ECC):**

| Technique | Purpose |
|---|---|
| Hardware ECC | Single-bit auto-correct, double-bit detect + trap. Same as automotive ECUs. |
| MPU regions | Each R5F core's memory is protected. R5F-2 (watchdog) physically cannot be corrupted by R5F-0. |
| FreeRTOS stack canary + MPU guard | Stack overflow → hardware fault → immediate safe-state, not silent corruption. |

---

## 3. Communication Architecture

### 3.1 Overview — All I/O Over Modbus TCP Star Topology

```
Nova Controller (AM2434)
    │
    ├── Ethernet (CPSW) ──► Ethernet Switch ──► Orbit Modules: 10DO/10DI/2AO + dual RS485
    │   192.168.0.1 (master)     │              ├── Port A (USART1) → analog boards (Modbus RTU master, 9600)
    │   ISOLATED SUBNET          │              └── Port B (USART2) → VFDs, EEVs (Modbus RTU, 19200)
    │   NO INTERNET             └──────► (modules only — NO RPi5 on this network)
    │
    ├── UART0 (115200) ──► Debug console
    ├── UART1 (230400) ──► RPi5 (serial bridge — primary data link)
    │                        └── RPi5 eth0: site network (10.x / DHCP) → internet, web UI, cloud
    ├── UART2–8 ──► ALL SPARE (8 UARTs free)
    │
    └── [Optional] UART2 + RS485 Hub → Legacy AS2 analog boards (retrofit sites only)
```

**Primary architecture: everything over Modbus TCP on an isolated control subnet.**
- Orbit modules (10DO/10DI/2AO + dual RS485) — control equipment + tunnel RTU to VFDs/EEVs + poll analog boards via Modbus RTU
- All Orbit modules on **192.168.0.0/24** — isolated, air-gapped from the internet
- RPi5 connects to Nova via **UART1** (serial bridge) — the RPi5 is NOT on the Modbus TCP subnet
- RPi5's Ethernet connects to the site/internet network for web UI, cloud, remote access
- Each Orbit module carries **3 simultaneous channels** on one Cat5: Modbus TCP (I/O) + Port A (analog boards via Modbus RTU) + Port B (VFDs/EEVs via Modbus RTU)

### 3.2 Network Isolation — Control vs. Internet

The Modbus TCP control network and the internet-facing network are **physically separate**:

```
┌─────────────────────────────────────────────────────────────────────┐
│                                                                     │
│   CONTROL NETWORK (192.168.0.0/24)      SITE NETWORK (10.x/DHCP)  │
│   ─────────────────────────────────     ──────────────────────────  │
│   Ethernet Switch                        Site router / DHCP        │
│    ├── 192.168.0.1   Nova Controller (master)│                     │
│    ├── 192.168.0.2   Orbit 1 (Rm A Stor)    │                     │
│    ├── 192.168.0.3   Orbit 2 (Rm A Ref1)    │                     │
│    ├── 192.168.0.4   Orbit 3 (Rm A Ref2)    │                     │
│    ├── 192.168.0.5   Orbit 4 (Rm B Stor)    ├── RPi5 (10.1.2.137)│
│    ├── 192.168.0.6   Orbit 5 (Rm B Ref1)    │   Web UI (LAN :80)│
│    └── 192.168.0.7   Orbit 6 (Rm B Ref2)    │   Outbound TLS    │
│                                               │   to Azure only   │
│         │ NO ROUTER │ NO GATEWAY │            └── No inbound ports │
│         │ NO DNS    │ NO INTERNET│                                  │
│                                                                     │
│              Nova ◄───── UART1 (230400) ─────► RPi5                │
│              (control)     Serial bridge          (UI/cloud)        │
│                            Point-to-point                           │
│                            Not network-accessible                   │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

**Why this isolation matters:**
- A compromised RPi5 (hacked web server, SSH exploit, malware) **physically cannot** send packets to the control network — there is no Ethernet path between them
- A misconfigured module or switch cannot leak traffic onto the site network
- No firewall rules needed — the isolation is **physical**, not logical
- UL 508A inspectors understand physical isolation; they're skeptical of software firewalls
- UART1 is a point-to-point serial link — not addressable from any network

**What flows over UART1 (same protocol as current architecture):**
- RPi5 → Nova: UI commands (set temperature, change override, start defrost, acknowledge alarm)
- Nova → RPi5: Sensor readings, equipment status, alarm states, VFD speeds, energy data
- Binary packet protocol, 230400 baud, ~2 KB/s typical, ~10 KB/s peak

**What flows over Ethernet (192.168.0.x):**
- Nova → Modules: DO commands, AO setpoints, RTU gateway requests (VFD speed, EEV position)
- Modules → Nova: DI states, AO readback, sensor values, RTU gateway responses, watchdog status

### 3.3 Module IP Assignment

**DIP switch + fixed formula (recommended):**

Each module has a 6-position DIP switch that sets its IP address:

| DIP Value | IP Address | Assignment (twin system example) |
|---|---|---|
| 1 | 192.168.0.2 | Room A — Storage Orbit |
| 2 | 192.168.0.3 | Room A — Refrigeration 1 Orbit |
| 3 | 192.168.0.4 | Room A — Refrigeration 2 Orbit |
| 4 | 192.168.0.5 | Room B — Storage Orbit |
| 5 | 192.168.0.6 | Room B — Refrigeration 1 Orbit |
| 6 | 192.168.0.7 | Room B — Refrigeration 2 Orbit |
| 7–63 | 192.168.0.8–64 | Available for expansion (more rooms, sensor modules, etc.) |

**Formula:** IP = `192.168.0.{DIP_value + 1}`, subnet mask = `255.255.255.0`, gateway = `192.168.0.1` (Nova).

**Nova brain (gateway/master):** Fixed IP = `192.168.0.1`, same subnet.

**Why DIP switches:**
- Same approach as current AS2 analog boards — techs already know it
- Same as every Modbus RTU device in the industry — set address, plug in, done
- Zero network configuration. No DHCP server, no DNS, no admin console.
- A replacement Orbit module: set the same DIP position, plug in the Cat5, running in 60 seconds

### 3.4 Modbus TCP — Custom Orbit Modules

**Protocol:** Modbus TCP (port 502) over standard Ethernet.
**Power:** 24VDC from a UL-listed DIN-rail power supply (shared with analog boards and DI circuits). 24V→5V buck→3.3V LDO on board.
**Topology:** Star from Ethernet switch. Each module has its own Cat5 run. No daisy-chain, no termination resistors.
**Speed:** All modules queried in parallel — full I/O scan < 2 ms.
**Fault isolation:** Per-port. A cable fault to one module doesn't affect any others.

### 3.3 I/O Isolation from the Main Controller

The Modbus TCP star architecture provides **complete communication and fault isolation** between the Nova controller and all field I/O. This is the single strongest argument for this design over direct-wired or RS485-only approaches.

**Galvanic isolation — no shared data copper:**
- The only data connection between the Nova board and field equipment is a Cat5 Ethernet cable through a switch
- The Ethernet magnetics (transformers inside the RJ45 jack) on both Nova and Orbit provide galvanic isolation on the data path
- A field wiring fault (short to 240VAC, lightning transient, condensation, rodent damage) destroys that **~$25 Orbit module** — the **$500+ Nova controller is untouched**
- On RS485 by contrast, a shorted transceiver can back-feed through the bus and damage the MCU's UART peripheral or even the MCU itself

**Local power per module:**
- The Nova board runs on its own dedicated 5VDC supply
- Each Orbit module has a local 24VDC→5V→3.3V power path — a per-module fuse isolates each module's power
- Analog boards share the same local 24V rail — one supply powers the entire field station (Orbit + analog boards + DI circuits)
- A blown fuse on one module has zero effect on any other module or the Nova board

**Star topology fault containment:**
- Each module is on its own switch port with its own Cat5 run
- A dead or disconnected module → that one module goes offline, every other module continues operating normally
- Compare RS485 daisy-chain: one shorted transceiver, one pinched cable, one loose shield → **every device on that bus goes dark**

**Field replacement in 60 seconds:**
- Module dies → unplug Cat5 → plug in identical replacement → set same DIP switch position → running
- No rewiring, no bus termination changes, no address conflicts, no power supply recalculation
- A ~$25 Orbit module and a screwdriver is the entire repair kit

**Why this matters for refrigeration:**
- Compressor contactors, defrost heaters, and condenser fans create massive EMI when switching. That EMI is contained at the Orbit module — it never reaches the controller
- Refrigeration panels live in harsh environments (condensation, temperature swings, cleaning chemicals). Nova stays in the clean main panel; Orbit modules go wherever the equipment is
- A single field wiring mistake during commissioning (wrong wire on a terminal) kills a ~$25 module, not the entire control system

### 3.4 Modbus RTU — VFDs and EEVs (Via Orbit Module RS485 Port B)

**Protocol:** Modbus RTU over RS485, 19200 baud — tunneled through Orbit module Port B over Ethernet.
**Physical connection:** Short RS485 stubs (3-10 ft) from Orbit module to nearby VFDs/EEVs.
**Why RTU for VFDs:** Every ABB ACS380 has RS485 Modbus RTU built in — $0. Modbus TCP requires the FMBT-21 adapter at ~$200 per drive. For 8 VFDs, that's $1,600 saved.
**Why RTU for EEVs:** Danfoss, Carel, and Emerson EEV controllers all have RS485 built in.
**Fault isolation:** Each module's RS485 segment is independent — a VFD fault on one compressor can't affect any other module.

### 3.5 Analog Boards — Via Orbit Module RS485 Port A (Modbus RTU)

**The existing AS2 analog boards (PIC16F876-20/SO + ADM2687E) are reflashed with Modbus RTU slave firmware. Zero hardware changes — firmware only.**

Analog boards connect via RS485 to the nearest **Orbit module's Port A** (USART1). The Orbit firmware runs as a **Modbus RTU master** on Port A — it polls each analog board using standard Modbus FC 04 (Read Input Registers), and stores sensor values in Orbit input registers 0x0700+. Nova reads these registers via Modbus TCP just like DI, DO, and AO data — one protocol for everything, end to end.

```
Nova Controller             Orbit Module                 Analog Boards (Modbus RTU slaves)
───────────────             ────────────                 ─────────────────────────────────
                           ┌──────────────┐
  Modbus TCP FC 04         │  Storage        │    RS485 Port A (9600 baud, Modbus RTU)
  read input regs  ───────►│  Orbit Module   ├───►  Analog Board 1 (addr 1)
  0x0700–0x07FF            │  Polls boards   │───►  Analog Board 2 (addr 2)
  (sensor values)          │  via Modbus RTU │───►  ...
  ◄────────────────────    │  Port B: VFDs   │───►  Analog Board 32 (addr 32)
  sensor data in           │   (also RTU)    │
  standard Modbus regs     └──────────────┘
```

**How it works:**
- Orbit firmware polls analog boards every 500 ms (configurable via register 0x0305)
- For each board: sends Modbus RTU FC 04 request at 9600 baud, reads 4 sensor values + sensor type
- Stores 4 sensor values per board in Modbus input registers 0x0700–0x07FF (scaled ×100)
- Reports per-board health in status registers 0x0740–0x075F (OK / timeout / CRC error)
- Nova reads sensor data via standard Modbus TCP FC 04 — **the entire system speaks one protocol**

**Analog board firmware change (PIC16F876):**
- Replace proprietary Gellert protocol handler with Modbus RTU slave stack (~400 lines of C, XC8 compiler)
- DIP switch sets Modbus slave address 1–32 (same hardware addressing as today)
- Expose 4 ADC channels as Modbus input registers 0x0001–0x0004 (raw 10-bit values, scaled ×100)
- Register 0x0000 = sensor type (0=IR temp, 1=humidity, 2=CO2, 3=NTC temp)
- Register 0x0005 = firmware version
- Hardware unchanged: same PIC16F876-20/SO, same ADM2687E isolated transceiver, same ADC front-end, same DIP switch, same PCB

**Why this is better than the Gellert auto-poll approach:**
- **One protocol everywhere** — Modbus RTU on both Port A and Port B, Modbus TCP on Ethernet. No proprietary protocol in the system.
- Orbit firmware is simpler — standard Modbus RTU master, no Gellert parser needed (~500 lines of protocol code eliminated)
- Analog boards become **standard industrial Modbus sensors** — testable with any off-the-shelf Modbus tool (no proprietary knowledge needed)
- Supports **up to 32 boards per Port A segment** (DIP switch addresses 1–32) — same as the current system's addressing range
- Nova firmware is unchanged — it reads Orbit input registers 0x0700+ via Modbus TCP FC 04, same as before
- If a future Orbit variant has on-board ADCs, it populates the same registers (0x0700+) — **Nova firmware zero changes**
- Per-board health reporting lets Nova detect a failed analog board by register, not by timeout guesswork

**Analog board addressing is per-Orbit (namespaced):** Each Orbit's Port A is a physically separate RS485 bus. Analog board address 1 on Orbit 3's Port A is a completely different wire than address 1 on Orbit 7's Port A. From Nova's perspective, the unique identifier for any analog board is **(Orbit IP, board address)** — e.g., (192.168.0.2, board 3) vs (192.168.0.5, board 3) are two different sensors on two different buses. This means 8 storage rooms × 32 boards each = **256 analog boards** with zero addressing conflicts and **no changes to the analog board DIP switches**.

**Configuration per Orbit module with analog boards:**
- Register 0x0303 = 1 (Modbus RTU master poll mode on Port A, default)
- Register 0x0300 = 9600 (Port A baud rate, matching analog board firmware)
- Register 0x0304 = number of boards on segment (1–32, default: 6)
- Register 0x0305 = poll interval in ms (default: 500)
- Analog boards daisy-chain on Port A's RS485 terminals (up to 32 boards per segment)

**Analog board Modbus RTU register map (PIC16F876 firmware):**

| Register | Type | Content |
|---|---|---|
| 0x0000 | Input Register | Sensor type: 0=IR temp, 1=humidity, 2=CO2, 3=NTC temp |
| 0x0001 | Input Register | Channel 1 ADC value (scaled ×100) |
| 0x0002 | Input Register | Channel 2 ADC value (scaled ×100) |
| 0x0003 | Input Register | Channel 3 ADC value (scaled ×100) |
| 0x0004 | Input Register | Channel 4 ADC value (scaled ×100) |
| 0x0005 | Input Register | Firmware version (e.g., 0x0300 = v3.0) |

**Analog board hardware (unchanged):**
- MCU: PIC16F876-20/SO (8K flash, 368B SRAM, 1× USART, 5× 10-bit ADC)
- RS485 transceiver: ADM2687E (isolated, integrated DC-DC — no external isolated supply needed)
- Address: DIP switch, 1–32
- Channels: 4 analog inputs per board
- Firmware change only — same PCB, same components, same DIP switch, same wiring

### 3.6 Legacy Support — Direct RS485 Hub (Not Needed on New Boards)

**The on-board RS485 hub (4× MAX3485 + MUX) is no longer needed.** Analog boards connect through Orbit module RS485 Port A instead. Do not populate the hub on new Nova PCBs — saves $4 and board space. All 9 UARTs on the AM2434 remain spare except UART0 (debug) and UART1 (RPi5 bridge).

---

## 4. Custom Modbus TCP Orbit Module

### 4.1 Module Specification

| Feature | Specification |
|---|---|
| **Digital Outputs** | 10× 24VDC open-collector sink outputs via ULN2803A Darlington drivers (2×, DIP-18, socketed). 500 mA max per channel. Drive chain: 74HC595 shift registers (2×, DIP-16, socketed) → ULN2803A. COM pin tied to +24V — built-in clamp diodes absorb inductive kickback from external relay coils and solenoid valves. No high voltage on the board. Typical loads: DIN-rail relay coils (15–80 mA), solenoid valves (≤300 mA), indicator lights (e.g., Siemens 8WD46 LED stack light ~25 mA). 20-position 5.08mm pluggable terminal block (V+/DO alternating pairs × 10 channels). Power loss → ULN2803A goes high-impedance → external relay coils de-energize → load contacts open (inherent fail-safe). |
| **Digital Inputs** | 10× optocoupled (24VDC, wet contact) |
| **Analog Outputs** | 2× 4-20mA (12-bit DAC, loop-powered) |
| **RS485 Port A** | USART1 + MAX3485 — Modbus RTU master mode (default): polls analog boards at 9600 baud, stores sensor data in Orbit input registers 0x0700+. Up to 32 boards per segment (DIP switch addresses 1–32). Raw serial pass-through mode available as fallback (register 0x0303 = 0). |
| **RS485 Port B** | USART2 + MAX3485 — Modbus RTU pass-through for VFDs/EEVs (19200 baud) |
| **Communication** | Modbus TCP, port 502, static IP or DHCP |
| **Power** | 24VDC input (from UL-listed DIN-rail supply, shared with analog boards). 24V→5V buck→3.3V LDO on board. ~2.0W typical (logic only — external load current bypasses the regulator via pass-through terminal pins). |
| **Connectors** | 1× RJ45 (data only) + 1× 2-pos screw terminal (24VDC+, 24VDC−) + 1× 20-pos 5.08mm pluggable header (DO) |
| **Address config** | 6-bit DIP switch (module ID 1–63 in Modbus device ID register) |
| **Watchdog** | Configurable timeout (register 0x0100). Per-output comms-loss behavior (registers 0x0101–0x010A): 0 = off (default), 1 = hold last state, 2 = force on, 3 = force off. Per-AO comms-loss behavior (registers 0x010B–0x010C): 0 = 4mA/off (default), 1 = hold last setpoint. Timed outputs (registers 0x0110–0x0119): per-DO pulse duration in seconds — see §4.9. On watchdog expiry, active timers are cancelled and each output executes its configured comms-loss action. Power loss → ULN2803A goes high-impedance → all loads de-energize (hardware fail-safe regardless of firmware). Module reports comms-loss status in register 0x010D. All outputs start off at power-on. See §4.7 for application-specific comms-loss policies. |
| **Hardware watchdog** | STM32F091 independent watchdog (IWDG, 40 kHz LSI oscillator). Firmware must kick IWDG every 500 ms. If the main firmware locks up, IWDG resets the MCU → clean reboot → outputs off → comms-loss behavior resumes once watchdog timer expires. IWDG is independent of the main system clock — it catches hard faults that a software watchdog cannot. |
| **LED indicators** | **Green:** per-channel DO (on = output active), per-channel DI (on = input closed), 24V power, Ethernet link. **Blue:** RS485-A TX/RX activity, RS485-B TX/RX activity. **Red:** Fault/watchdog. All visible from enclosure front. |
| **Mounting** | DIN rail |
| **Enclosure** | IP20 industrial, ~100×110×50mm (sized to ~90×100mm PCB + terminal blocks) |
| **Temperature** | -20°C to +60°C operating |
| **Humidity** | Up to 95% RH non-condensing (IEC 60721-3-3 Class 3C4). ENIG finish + acrylic conformal coating + gold-plated DIP sockets. |

### 4.2 Module Hardware Design

**Active Components**

| Component | Function | Cost |
|---|---|---|
| STM32F091CCT6 | Cortex-M0, 256KB flash, 32KB SRAM, Modbus TCP slave + bootloader | $1.50 |
| W5500 | Hardware TCP/IP + MAC + PHY (100 Mbps, SPI interface) | $2.50 |
| TPS54302 or equiv. | 24V→5V synchronous buck converter (3A max, ~0.5A typical draw) | $0.80 |
| AMS1117-3.3 or equiv. | 5V→3.3V LDO (500 mA) | $0.15 |
| RJ45 with integrated magnetics | Standard Ethernet jack (data only, no PoE) | $0.80 |
| ULN2803A × 2 + 2× 74HC595 | Darlington sink drivers (10 channels, 500 mA/ch max, built-in clamp diodes) + shift registers. **All in DIP sockets** | $1.00 |
| 2× DIP-18 + 2× DIP-16 sockets (gold-plated) | Sockets for ULN2803A drivers + 74HC595 shift registers — Au/Ni contacts for humidity resistance | $0.60 |
| 10× PC817 optocouplers | DI input isolation (24V → 3.3V logic) | $1.00 |
| MCP4822 (2-ch 12-bit DAC, SPI) | AO voltage generation | $1.20 |
| 2× XTR111 | Voltage-to-4-20mA current loop driver | $4.00 |
| MAX3485 × 2 | RS485 transceivers — Port A (USART1) + Port B (USART2). **Socketable (DIP-8)** | $0.70 |
| 2× machined-pin DIP-8 sockets (gold-plated) | Sockets for MAX3485 — Au/Ni contacts, field-replaceable after RS485 bus faults | $0.40 |
| 4× TVS diodes (SMBJ6.0CA or equiv.) | RS485 ESD protection — one per data line, both ports | $0.20 |
| 1× 25 MHz crystal + 2× 18pF load caps | W5500 clock source | $0.15 |
| 1× Schottky diode (SS14 or equiv.) | 24VDC input reverse-polarity protection | $0.08 |
| **Active subtotal** | | **$15.08** |

**Passive Components (~130 pieces)**

| Category | Components | Qty | Cost |
|---|---|---|---|
| Buck regulator support | 10µF/50V input cap, 22µF/10V output cap, 100nF bootstrap, 10µH power inductor (wound), 2× feedback resistors (1%) | 6 | $0.30 |
| LDO support | 10µF input cap, 10µF output cap | 2 | $0.06 |
| MCU decoupling + config | 4× 100nF (VDD/VDDA), 1× 4.7µF bulk, 100nF + 10kΩ nRST, 10kΩ BOOT0 pull-down | 8 | $0.10 |
| W5500 decoupling | 4× 100nF + 1× 10µF, ferrite bead (AVDD), 2× 49.9Ω TX series termination | 8 | $0.15 |
| DI input passives | 10× 4.7kΩ series (LED current limit at 24V), 10× 10kΩ pull-up (collector to 3.3V) | 20 | $0.10 |
| DO driver decoupling | 2× 100nF (74HC595), OE pull-down resistor | 3 | $0.03 |
| RS485 passives | 2× 120Ω termination, 2× 100nF bypass | 4 | $0.04 |
| DAC + AO passives | 100nF DAC bypass, VREF cap, 4–6× precision resistors (0.1% for XTR111 current set), 2× loop bypass caps | 10 | $0.24 |
| DIP switch pull-downs | 6× 10kΩ | 6 | $0.03 |
| LEDs (26 total) | 10× green DO, 10× green DI, 1× green power, 4× blue RS485, 1× red fault | 26 | $0.26 |
| LED current limit resistors | 26× 330Ω–1kΩ (0402 or 0603) | 26 | $0.13 |
| Misc decoupling | Spare 100nF on power planes, bulk caps near connectors | 6 | $0.06 |
| **Passive subtotal** | **~130 pieces** | | **$1.50** |

**Connectors, PCB, Enclosure**

| Component | Function | Cost |
|---|---|---|
| 20-pos 5.08mm pluggable header + mating plug | DO terminal (V+/DO × 10 pairs, Phoenix Contact MSTB 2,5/ or equiv.) | $1.80 |
| 12-pos screw terminal | DI (10 DI + V+ + COM) | $0.50 |
| 4-pos screw terminal | AO (2 ch × output + return) | $0.20 |
| 2× 3-pos screw terminals | RS485 A + B (A+, B−, GND per port) | $0.30 |
| 2-pos screw terminal | 24VDC power input | $0.10 |
| 6-position DIP switch | Module ID (1-63) | $0.20 |
| PCB (2-layer, ~90×100mm) | FR-4, **ENIG finish**, 1 oz copper, all low-voltage. Price at 100 qty from JLCPCB/PCBWay. | $1.90 |
| DIN rail enclosure | Standard industrial ABS, ~100×110×50mm | $2.00 |
| Conformal coating (acrylic, Type AR) | IPC-CC-830C. Applied by CM after assembly. Mask-off: sockets, terminals, connectors. | $1.50 |
| **Connectors/PCB/enclosure subtotal** | | **$8.50** |

**BOM Total**

| | Cost |
|---|---|
| Active components | $15.08 |
| Passive components (~130 pcs) | $1.50 |
| Connectors, PCB, enclosure | $8.50 |
| **Total BOM (components + PCB + enclosure)** | **$25.08** |

**Estimated CM Assembly Cost (per unit)**

| Item | 10 units | 100 units | 500 units | Notes |
|---|---|---|---|---|
| SMT placement + reflow (SMD passives, MCU, W5500, DAC, LDO, optocouplers) | $8.00 | $4.50 | $2.80 | ~60 SMD placements. Stencil + P&P + reflow. Stencil cost (~$30) amortized. |
| Through-hole (DIP sockets, DIP switch, screw terminals, RJ45, pluggable header, TVS, crystal) | $4.00 | $2.50 | $1.50 | ~20 THT insertions. Wave solder or hand. |
| ICT / functional test | $1.50 | $1.00 | $0.60 | Power-on, program bootloader, verify Ethernet link, toggle DO, read DI |
| **CM assembly subtotal** | **$13.50** | **$8.00** | **$4.90** | |
| | | | | |
| **Fully loaded unit cost (BOM + PCB + enclosure + assembly)** | **~$39** | **~$33** | **~$30** | Does not include NRE (stencil, programming fixture, test jig — ~$500 one-time) |

### 4.3 Module Power Budget

| Component | Power (max simultaneous) |
|---|---|
| STM32F091 + W5500 + 2× MAX3485 + shift registers + ULN2803A drivers + logic | 0.45W |
| 10× DI optocoupler LEDs | 0.1W |
| 2× AO current loop (20mA each) | 1.0W |
| Voltage regulators (efficiency loss ~15%) | 0.2W |
| **Total (on-board)** | **~2.0W** |
| **24VDC input available** | **Unlimited (sized per DIN-rail supply)** |
| **Headroom** | **Unconstrained** |

**Note:** External load current (relay coils, solenoid valves, indicator lights) flows through the 24V pass-through terminals on the DO pluggable connector and does **not** pass through the on-board voltage regulator. With all 10 outputs driving 80 mA relay coils, external load current is only 800 mA — well within a standard 30W DIN-rail supply.

### 4.4 Field Serviceability — Socketable Components

All high-failure-risk components are in machined-pin DIP sockets for field replacement without soldering. UL 508A compatible — socketed IC modules are standard industrial practice.

**Socketable components:**

| Component | Socket | Failure mode | Field swap time |
|---|---|---|---|
| 2× MAX3485 RS485 transceivers | DIP-8 machined-pin | RS485 bus faults, ESD from miswiring, lightning transients | 30 seconds per chip |
| 2× ULN2803A Darlington drivers | DIP-18 machined-pin | Output overcurrent (unlikely — 500 mA/ch limit, typical loads 15–80 mA) | 30 seconds per chip |
| 2× 74HC595 shift registers | DIP-16 machined-pin | Drives digital output selection | 30 seconds per chip |

**Not socketable (swap entire module if failed):**

| Component | Package | Reason |
|---|---|---|
| STM32F091 MCU | LQFP-48 | Surface-mount only, no DIP variant. MCU failure = swap module (~$25) |
| W5500 Ethernet | LQFP-48 | Same — and Ethernet PHY failure is extremely rare |
| MCP4822 DAC | SOP-8 | Analog precision part, socket contact resistance would degrade accuracy |
| XTR111 loop drivers | SOT-23 | Tiny package, SMD only |
| PC817 optocouplers | DIP-4 | Could be socketed but failure rate is negligible — not worth 10 extra sockets |

**Pluggable terminal block — bench wiring:**

The DO terminal block uses a **20-position 5.08mm pitch pluggable** connector (Phoenix Contact MSTB 2,5/ or equivalent). The installer wires loads into the mating plug at the bench, then clicks the plug onto the board header. To replace an Orbit module:

1. Unplug the DO connector (one click)
2. Unplug Cat5 + 24VDC power
3. Remove the old module from DIN rail
4. Snap in the new module, set DIP switch, reconnect power + Cat5
5. Click the DO plug back in — all field wiring remains intact

**Spares kit recommendation:** 2× MAX3485 ($0.70), 1× ULN2803A ($0.20) = **$0.90 total** covers the most likely field failures for the entire installation.

### 4.5 Module Firmware

The Modbus TCP slave firmware is approximately 1,500 lines of C (including Modbus RTU master for analog board polling and Modbus TCP bootloader):

**Flash layout (STM32F091CC — 256 KB):**

| Region | Address | Size | Purpose |
|---|---|---|---|
| Bootloader | 0x0800_0000 | 32 KB | Modbus TCP bootloader — accepts firmware images over Ethernet. Runs on power-up, checks update flag. If set: receives firmware via Modbus TCP writes, verifies CRC, writes to application region. If not set or on CRC failure: jumps to application. |
| Application | 0x0800_8000 | 216 KB | Main Orbit firmware — Modbus TCP slave, digital output control, RS485 Modbus RTU master (Port A) + pass-through (Port B), watchdog |
| Config | 0x0803_E000 | 8 KB | Runtime configuration — Port A mode, baud rates, board count, poll interval, watchdog timeout. Dual-page for wear-leveling. |

**Modbus register map:**

| Register | Address | Type | Description |
|---|---|---|---|
| DO coils | 0x0000–0x0009 | Coil (FC 01/05/0F) | 10 digital outputs (write 1 = output ON / sink current, write 0 = output OFF / high-Z) |
| DI status | 0x0000–0x0009 | Discrete Input (FC 02) | 10 digital inputs |
| AO setpoint | 0x0000–0x0001 | Holding Register (FC 03/06/10) | 2 analog outputs (0-10000 = 0.00-100.00%) |
| AO readback | 0x0010–0x0011 | Input Register (FC 04) | 2 analog output actual values |
| Watchdog timer | 0x0100 | Holding Register | Timeout in seconds (0 = disabled) |
| DO comms-loss behavior | 0x0101–0x010A | Holding Register | Per-output comms-loss action (10 registers, one per DO). 0 = off (default), 1 = hold last state, 2 = force on, 3 = force off. Applied when watchdog expires. Persisted in config flash. |
| AO comms-loss behavior | 0x010B–0x010C | Holding Register | Per-AO comms-loss action (2 registers). 0 = 4mA/off (default), 1 = hold last setpoint. Applied when watchdog expires. |
| Comms-loss status | 0x010D | Input Register | 0 = normal, 1 = watchdog expired (comms lost), 2 = recovered. Cleared on first successful Modbus TCP transaction after recovery. |
| DO timed output | 0x0110–0x0119 | Holding Register (FC 06/10) | Timed pulse per DO (10 registers). Write N = energize output for N seconds then auto-off. Write 0 = cancel active timer and turn output off. Read = remaining seconds (0 = idle). See §4.9. |
| Module ID | 0x0200 | Input Register | DIP switch value (read-only) |
| Firmware version | 0x0201 | Input Register | Module firmware version |
| Uptime | 0x0202–0x0203 | Input Register | Seconds since power-on (32-bit) |
| DO status readback | 0x0010–0x0019 | Discrete Input (FC 02) | Output state readback (confirms ULN2803A output active/inactive) |
| RTU Port A baud | 0x0300 | Holding Register | RS485 Port A baud rate (default: 9600 — analog boards) |
| RTU Port A timeout | 0x0301 | Holding Register | Port A response timeout in ms (default: 200) |
| RTU Port A status | 0x0302 | Input Register | 0=idle, 1=busy, 2=last TX OK, 3=last TX timeout |
| Port A TX/RX buffer | 0x0400–0x043F | Holding Register | 128-byte raw serial send/receive buffer (Port A) |
| Port A RX length | 0x0440 | Input Register | Bytes received in last Port A transaction |
| RTU Port B baud | 0x0310 | Holding Register | RS485 Port B baud rate (default: 19200 — VFDs/EEVs) |
| RTU Port B timeout | 0x0311 | Holding Register | Port B response timeout in ms (default: 100) |
| RTU Port B status | 0x0312 | Input Register | 0=idle, 1=busy, 2=last TX OK, 3=last TX timeout |
| Port B TX/RX buffer | 0x0500–0x053F | Holding Register | 128-byte raw serial send/receive buffer (Port B) |
| Port B RX length | 0x0540 | Input Register | Bytes received in last Port B transaction |
| **Time sync** | | | |
| Time sync (high) | 0x0600 | Holding Register | 32-bit Unix timestamp — upper 16 bits. Written by Nova periodically (~1/min). Orbit increments locally with SysTick between updates. |
| Time sync (low) | 0x0601 | Holding Register | 32-bit Unix timestamp — lower 16 bits |
| **Analog board polling (Port A Modbus RTU master)** | | | |
| Port A mode | 0x0303 | Holding Register | 0 = raw pass-through (registers 0x0400–0x0440 active), 1 = Modbus RTU master poll (registers 0x0700+ active). Default: 1 |
| Port A board count | 0x0304 | Holding Register | Number of analog boards on segment (1–32, default: 6). Poll cycles through Modbus addresses 1 to N. |
| Port A poll interval | 0x0305 | Holding Register | Milliseconds between poll cycles (default: 500) |
| Sensor data | 0x0700–0x07FF | Input Register | Up to 32 boards × 4 channels = 128 sensor values. Scaled integer (×100, so 2847 = 28.47°F). Registers for unconfigured boards read 0xFFFF. |
| Board status | 0x0740–0x075F | Input Register | Per-board status: 0=OK, 1=timeout, 2=CRC error, 3=not configured |
| Last poll timestamp | 0x0750–0x0751 | Input Register | 32-bit timestamp of last successful auto-poll cycle |
| **Bootloader** | | | |
| Bootloader command | 0x0800 | Holding Register | Write 0xB007 to enter bootloader on next reboot. Write 0x0000 to clear. |
| Bootloader status | 0x0801 | Input Register | 0=application running, 1=bootloader active, 2=update in progress, 3=last update failed CRC |
| **Future: local analog inputs (reserved)** | | | |
| Local AI data | 0x0900–0x0913 | Input Register | Reserved for future Orbit variant with on-board ADC. Read 0xFFFF on modules without ADC hardware. |

**RS485 Dual-Port Gateway:**

Each Orbit module has **two independent RS485 ports** that operate simultaneously. No mode switching — both ports are always active.

**Port A — Modbus RTU Master Mode (USART1, default — register 0x0303 = 1):**

In Modbus RTU master mode, the Orbit firmware polls analog boards on Port A using standard Modbus FC 04 and presents sensor data as input registers. Nova never interacts with Port A directly — it just reads Orbit registers, identical to how it reads DI, DO, and AO data.

The module:

1. Cycles through analog boards 1 to N (configurable via register 0x0304, default: 6, max: 32)
2. For each board: sends a Modbus RTU FC 04 request (read input registers 0x0000–0x0004) at 9600 baud
3. Receives the standard Modbus RTU response — 4 sensor values + sensor type
4. Stores the values in Orbit input registers 0x0700–0x07FF (scaled ×100, so 2847 = 28.47°F)
5. Updates per-board health status in registers 0x0740–0x075F (0=OK, 1=timeout, 2=CRC error)
6. Nova reads sensor data via standard Modbus TCP FC 04 — same as any other input register

**The entire system speaks Modbus end-to-end** — Modbus RTU on both RS485 ports, Modbus TCP on Ethernet. No proprietary protocols.

**Benefits of Modbus RTU master mode:**
- One protocol everywhere — no proprietary Gellert parser, no proprietary code anywhere in the system
- Analog boards are standard Modbus sensors — testable with any off-the-shelf Modbus tool
- Sensor data is always current in registers — Nova reads are instant, no RS485 round-trip wait
- Nova's scan loop is uniform: for each Orbit, read input registers. No special-case handling.
- If a future Orbit variant has on-board ADCs, it populates the same registers (0x0700+) — Nova firmware doesn't change
- Per-board health reporting in dedicated status registers (0x0740+) — Nova can detect a failed board immediately
- Supports up to 32 boards per segment (same as the legacy DIP switch addressing range)

**Port A — Raw Serial Pass-Through (USART1, fallback — register 0x0303 = 0):**

For debugging or non-standard RS485 devices, raw pass-through mode is available. Nova writes raw bytes into Port A's TX buffer registers (0x0400–0x043F), triggers a send, and reads the response from the RX buffer. The module:

1. Receives the raw byte payload over Modbus TCP (written to holding registers)
2. Sends the bytes verbatim out RS485 Port A at 9600 baud — no framing, no CRC, no interpretation
3. Collects the response bytes (up to 128 bytes, configurable timeout)
4. Stores them in the Port A RX buffer registers
5. Nova reads the response via Modbus TCP

**Modbus RTU master mode is the default for all new installations.** Raw pass-through exists as a fallback for debugging.

**Port B — Modbus RTU (USART2, for VFDs and EEVs):**

The Nova brain sends a standard Modbus TCP request to the module with a unit ID in the range 100–131. The module:

1. Receives the TCP frame, extracts the embedded RTU slave address (unit ID minus 100)
2. Builds a standard Modbus RTU frame (address + function code + data + CRC16)
3. Sends it out RS485 Port B at 19200 baud
4. Waits for the RTU response (configurable timeout, default 100 ms)
5. Wraps the response back into a Modbus TCP frame and returns it to Nova

This is functionally identical to the ABB FMBT-21 adapter — except it's built into a ~$25 module instead of a $200 standalone device.

**Both ports operate in parallel.** The Nova brain can query analog boards on Port A and a VFD on Port B in overlapping time windows. Each port has its own USART, its own MAX3485 transceiver, its own buffer registers, and its own status register — zero contention.

**Unit ID routing:**
- Unit ID **1** (or 0xFF / 255): the module's own I/O (DO/DI/AO registers)
- Unit ID **200**: Port A, raw serial pass-through (fallback mode, register 0x0303 = 0) — used for debugging or non-standard RS485 devices. In default Modbus RTU master mode (register 0x0303 = 1), analog board data is available directly in input registers 0x0700+.
- Unit IDs **100–131**: Port B, Modbus RTU — forwarded as RTU address 0–31 (VFDs/EEVs)

Example (VFD): Nova sends Modbus TCP to Orbit module at 192.168.0.3, unit ID 101 → module sends RTU frame out Port B to RS485 address 1 (the compressor VFD) → VFD responds → module wraps response in TCP → Nova receives it.

Example (Analog boards — Modbus RTU master mode, default): Orbit at 192.168.0.2 polls analog boards on Port A every 500 ms via standard Modbus RTU FC 04. Nova reads sensor data from input registers 0x0700–0x07FF via standard Modbus TCP FC 04 — one protocol end to end.

Example (Analog boards — raw pass-through fallback): Nova sends Modbus TCP to Orbit module at 192.168.0.2, unit ID 200 with raw byte payload → module sends raw bytes out Port A at 9600 baud → device responds → module returns raw response bytes to Nova over TCP.

**Watchdog behavior:** If no valid Modbus TCP transaction is received within the configured timeout, any active timed outputs (§4.9) are cancelled and each output executes its configured comms-loss action (registers 0x0101–0x010C). The default is all-off, but individual outputs can be configured to hold their last state, force on, or force off. Power loss always causes all outputs to go high-impedance with no firmware intervention required (hardware fail-safe). On power-up, all outputs and timers start off regardless of stored comms-loss config. The module reports comms-loss status in register 0x010D. Normal polling from Nova continuously resets the watchdog. See §4.7 for application-specific comms-loss policies.

**Hardware watchdog (IWDG):** The STM32F091's independent watchdog (IWDG) is always enabled. It runs on the 40 kHz LSI oscillator — completely independent of the main clock and firmware. If the firmware locks up (hard fault, stack overflow, infinite loop), the IWDG resets the MCU within 1 second. After reset, all outputs start off and the module reboots into normal operation. The IWDG catches failure modes that a software watchdog cannot.

**IP address assignment:** DIP switch (6-bit) sets the last octet of a fixed subnet. Example: DIP = 5 → IP = 192.168.0.6. Nova (gateway/master) is always 192.168.0.1. Formula: IP = 192.168.0.{DIP + 1}. Supports up to 63 Orbit modules per Nova. Nova knows which IP maps to which module based on the DIP switch / site configuration.

### 4.7 Comms-Loss Behavior — Per-Output Policy

The Orbit module supports **per-output comms-loss actions** so the same hardware works safely in both refrigeration and storage applications. Nova writes the comms-loss policy to each Orbit at commissioning (registers 0x0101–0x010C). The policy persists in config flash — it survives power cycles.

**Why this matters:**

| Application | What happens if Nova goes down | Correct comms-loss behavior |
|---|---|---|
| **Refrigeration** | Compressor off = product warms slowly (hours). Safe. | **All off** — turn off compressor, fans, valves. Call for service. |
| **Potato / onion storage** | Ventilation fans off = CO2 builds up → crop damage (minutes to hours). Dangerous. | **Hold last** — keep ventilation fans and VFDs running at their last commanded speed until power is pulled or a tech intervenes. |
| **Fresh air intake** | Dampers stuck open in winter = freezing outside air enters storage room → crop freeze damage. | **Off** — de-energize both OPEN and CLOSE motor outputs. Electric hold-position actuators stop in place. Comms-loss action = 0 (off). Nova periodically commands full-close to re-zero position, so typical comms-loss gap leaves dampers near-closed or at a small opening. See §4.9. |

The all-off default is correct for ~80% of outputs. The per-output policy lets the 20% of safety-critical "keep running" outputs hold their state.

**Comms-loss action values:**

| Value | Action | Use case |
|---|---|---|
| 0 | **Off** (default) | Compressors, heaters, solenoids, valves, **fresh air damper motors** — anything that should stop on loss of control. For electric hold-position actuators, off = motor stops, damper holds current position. |
| 1 | **Hold last state** | Ventilation fan contactors, VFD speed (AO), humidifier pumps — equipment that must keep running at last known setpoint |
| 2 | **Force on** | Alarm relays, status lights — force to known-safe energized state |
| 3 | **Force off** | Same as 0, but explicit — useful for clarity in configuration dumps |

**Storage module example (site with fresh air damper, no climacell):**

```
Nova writes at commissioning:
  Orbit Storage (192.168.0.2):
    Reg 0x0100 = 300         (watchdog timeout: 5 minutes)
    Reg 0x0101 = 0           (DO 1 Red Light: off — cosmetic)
    Reg 0x0102 = 0           (DO 2 Amber Light: off — cosmetic)
    Reg 0x0103 = 0           (DO 3 Green Light: off — cosmetic)
    Reg 0x0104 = 0           (DO 4 Fresh Air Damper Open: OFF — motor stops, actuator holds position)
    Reg 0x0105 = 1           (DO 5 Humidifier Head 1: hold last)
    Reg 0x0106 = 1           (DO 6 Humidifier Pump 1: hold last)
    Reg 0x0107 = 1           (DO 7 Humidifier Head 2: hold last)
    Reg 0x0108 = 1           (DO 8 Humidifier Pump 2: hold last)
    Reg 0x0109 = 0           (DO 9 Heat: off — never hold a heater)
    Reg 0x010A = 0           (DO 10 Cavity Heat: off — never hold a heater)
    Reg 0x010B = 1           (AO 1 Vent Fan VFD: hold last speed)
    Reg 0x010C = 0           (AO 2 Refrigeration fallback: off)
```

If Nova goes down: heaters shut off immediately, fresh air damper motors de-energize (actuators hold their current position — electric hold-position actuators do not move on power loss), and the ventilation fans keep running at whatever speed they were last commanded — recirculating internal air to prevent CO2 buildup. Since Nova periodically commands full-close to re-zero the damper position reference, the damper is typically near-closed or at a small opening when comms loss occurs. The VFD receives a steady 4-20mA signal from the held AO output. The Modbus RTU commands to the VFD on Port B also freeze at their last state — the Orbit stops forwarding new commands (because Nova isn't sending any), so the VFD holds its last received speed setpoint natively (standard Modbus VFD behavior — drives hold last register value until a new write arrives or the drive's own timeout fires).

**Refrigeration module example (all-off):**

All registers 0x0101–0x010C = 0 (default). On comms loss, everything de-energizes. The compressor pump-down sequence does not execute (no orderly shutdown) — the compressor simply stops. This is acceptable: modern compressors handle hard stops, and the LLS solenoid also de-energizes (closing), preventing liquid flood-back.

**Important safety constraints:**
- Heaters (DO 9, 10 in storage — if applicable) must **never** be set to hold-last or force-on. Firmware should reject writes of value 1 or 2 to any output flagged as a heater in the module configuration. Uncontrolled heating can cause fires.
- Fresh air damper actuators are **electric hold-position** (motor-driven, non-spring-return). The actuator holds its physical position when de-energized — it does not move on comms loss or power loss. Each damper uses two DOs (OPEN + CLOSE direction) controlled via timed-output registers (§4.9). Nova positions dampers by sending time-proportional pulses based on a UI-configurable full stroke time (default 360 seconds). The comms-loss action for damper motor outputs must be **0 (off)** — de-energize both motors so the actuator stops in place. Nova periodically commands full-close to re-zero the position reference, keeping the damper near-closed during normal operation. **Never set damper motor outputs to hold-last or force-on** (force-on would drive the actuator continuously into its end stop).
- The comms-loss action only takes effect after the watchdog timer expires — brief network glitches within the timeout window are invisible to the outputs.
- Power loss always overrides everything: ULN2803A goes high-impedance, all loads de-energize, no firmware involved.

### 4.9 Timed Output — Pulsed Actuator Control

The Orbit supports **timed digital outputs** via holding registers 0x0110–0x0119 (one per DO). This enables time-proportional positioning of electric damper actuators and other pulsed loads without requiring Nova to track countdown timers or send a second "off" command over the network.

**Write behavior:**

| Write value | Effect |
|---|---|
| N > 0 | Output turns ON immediately. Orbit starts a local 1 Hz countdown from N. Output turns OFF automatically when countdown reaches zero. |
| 0 | Cancels any active timer on that output. Output turns OFF immediately. |

**Read behavior:** Returns remaining seconds. 0 = idle (timer not running or already completed).

**Interaction with other registers:**

- A timed write **overrides the DO coil** (0x0000–0x0009) for the duration — Nova does not need to separately write the coil ON.
- If Nova writes the DO coil OFF (FC 05, value 0) during an active timer, the timer is **cancelled** and the output turns off immediately (explicit coil write always wins).
- If the comms-loss watchdog fires during an active timer, the timer is cancelled and the output executes its comms-loss action (registers 0x0101–0x010A). Typically this means off — the actuator holds its current position.
- On MCU reset (IWDG or power cycle), all timers are cleared and outputs start off.

**Firmware implementation:** A `uint16_t remaining_s[10]` array decremented by a 1 Hz SysTick callback. The output driver checks both the coil state and the timer — if `remaining_s[ch] > 0`, the output is forced ON regardless of the coil register. When the countdown reaches zero, the output reverts to the coil register state (which is 0 unless Nova has written it separately).

**Primary use case — fresh air damper actuators:**

Fresh air dampers use **electric hold-position actuators** (motor-driven, non-spring-return). The actuator holds its physical position when de-energized — it does not move on power loss or comms loss. Each damper requires two Orbit DOs: one for the OPEN direction and one for the CLOSE direction.

Nova controls damper position using **time-proportional pulses:**

1. The UI defines a **full stroke time** (configurable, default 360 seconds) — the time for the actuator to travel from fully closed to fully open.
2. To position a damper at X% open, Nova writes `ceil(X/100 × full_stroke_time)` seconds to the timed-output register of the OPEN DO.
3. To close a damper by Y%, Nova writes `ceil(Y/100 × full_stroke_time)` seconds to the CLOSE DO.
4. The Orbit energizes the motor for exactly that duration, then de-energizes. The actuator stops and holds.

**Example — open fresh air damper to 1%:**
```
Full stroke time = 360 seconds (configured in UI)
Desired position = 1%
Pulse duration = ceil(0.01 × 360) = 4 seconds

Nova writes: Orbit Door (192.168.0.8), register 0x0114 = 4
  → DO 5 (Damper 1 OPEN) energizes for 4 seconds
  → Actuator motors open 1% of travel, stops, holds position
```

**Example — close fresh air damper fully (from any position):**
```
Nova writes: Orbit Door (192.168.0.8), register 0x0115 = 360
  → DO 6 (Damper 1 CLOSE) energizes for full stroke time
  → Actuator motors to fully closed regardless of starting position
```

The Orbit does not track actuator position — it only executes timed pulses. Nova maintains the position model (accumulated open/close pulses) and periodically commands a full-close to re-zero the position reference.

### 4.10 Module Variants

The same PCB design supports multiple configurations by populating different components:

All variants include both RS485 ports (2× MAX3485 + screw terminals). Port A and Port B are always available regardless of which I/O channels are populated.

| Variant | DO | DI | AO | AI | RS485 Ports | BOM | Use case |
|---|---|---|---|---|---|---|---|
| **Standard (10/10/2)** | 10 × 24VDC sink (ULN2803A, 500 mA/ch, pluggable terminal) | 10 opto | 2 × 4-20mA | — | A + B | ~$25 | Storage + refrigeration modules. Port A polls analog boards via Modbus RTU. |
| **Output-heavy (10/10/0)** | 10 × 24VDC sink (ULN2803A, 500 mA/ch, pluggable terminal) | 10 opto | — | — | A + B | ~$17 | Simple on/off, no modulating valves |
| **Mixed (8/8/2/4)** | 8 × 24VDC sink (ULN2803A, pluggable terminal) | 8 opto | 2 × 4-20mA out | 4 × 4-20mA in | A + B | ~$33 | Rooms needing local sensor inputs |
| **Sensor (0/0/0/10)** | — | — | — | 10 × 4-20mA in | A + B | ~$20 | Future sensor module — if/when replacing legacy analog boards |
| **Sensor-NTC (0/0/0/8 NTC + 2 mA)** | — | — | — | 8 × NTC + 2 × 4-20mA | A + B | ~$18 | Future: replaces legacy analog boards directly |

**Note:** The sensor variants are optional future designs. On current systems, analog boards (reflashed with Modbus RTU firmware) connect via the standard Orbit module's RS485 Port A — the Orbit polls the boards via Modbus RTU and presents sensor data in input registers 0x0700+. No dedicated sensor module required.

---

## 5. Orbit Module Point Assignment

### 5.1 System Layout

Each cold storage room requires **3 Orbit modules** (all identical hardware — standard 10/10/2 + dual RS485):
- **Storage Orbit** — controls room equipment + analog boards via Port A (Modbus RTU, up to 32 boards) + ventilation fan VFDs via AO 1 (4-20mA)
- **Refrigeration Orbit 1** — controls compressor 1 + VFD/EEV via Port B (Modbus RTU)
- **Refrigeration Orbit 2** — controls compressor 2 + VFD/EEV via Port B (Modbus RTU)

Port A and Port B operate **simultaneously** on every module. The storage Orbit uses Port A for analog boards; Port B is free (or available for a future VFD). The refrigeration Orbits use Port B for VFDs/EEVs; Port A is free (or available for local sensors).

A twin cold-storage facility (Room A + Room B, 2 compressors each) requires **6 Orbit modules:**

| Module | DIP ID | IP Address | Port A (USART1) | Port B (USART2) | Assignment |
|---|---|---|---|---|---|
| 1 | 01 | 192.168.0.2 | Analog boards (auto-poll) / 9600 | Free | Room A — Storage |
| 2 | 02 | 192.168.0.3 | Free | VFD+EEV / 19200 | Room A — Refrigeration 1 |
| 3 | 03 | 192.168.0.4 | Free | VFD+EEV / 19200 | Room A — Refrigeration 2 |
| 4 | 04 | 192.168.0.5 | Analog boards (auto-poll) / 9600 | Free | Room B — Storage |
| 5 | 05 | 192.168.0.6 | Free | VFD+EEV / 19200 | Room B — Refrigeration 1 |
| 6 | 06 | 192.168.0.7 | Free | VFD+EEV / 19200 | Room B — Refrigeration 2 |

---

### 5.2 Storage Module — Digital Outputs

| DO | Equipment | Description | Watchdog |
|---|---|---|---|
| 1 | Red Light | Room alarm indicator — on during active alarm | No |
| 2 | Amber Light | Room warning indicator — on during warning condition | No |
| 3 | Green Light | Room normal indicator — on when system is running normally | No |
| 4 | Climacell | Climacell atmosphere control valve/actuator | Yes |
| 5 | Humidifier Head 1 | Humidifier spray head solenoid, system 1 | Yes |
| 6 | Humidifier Pump 1 | Humidifier circulation pump, system 1 | Yes |
| 7 | Humidifier Head 2 | Humidifier spray head solenoid, system 2 | Yes |
| 8 | Humidifier Pump 2 | Humidifier circulation pump, system 2 | Yes |
| 9 | Heat | Room space heater contactor | Yes |
| 10 | Cavity Heat | Wall/floor cavity anti-frost heater contactor | Yes |

**Notes:**
- Status lights (DO 1–3) are non-watchdog — they stay in last state if comms drop (cosmetic, not safety)
- Humidifier heads (DO 5, 7) must always shut off with their pumps (DO 6, 8) — firmware enforces pump-before-head interlock
- Heat and cavity heat (DO 9–10) are watchdog-enabled — de-energize on comms loss to prevent uncontrolled heating. **Comms-loss action must always be 0 (off).** Firmware rejects hold-last or force-on for heater outputs.
- **Ventilation fan (AO 1) and climacell/humidifier (DO 4–8) should be configured as hold-last (comms-loss action = 1)** in potato/onion storage applications — crop damage from CO2 buildup occurs faster than spoilage from temperature drift. See §4.7 for per-output comms-loss configuration.

**Site-specific output flexibility — fresh air intake dampers:**

Sites with fresh air intake dampers (motorized louvers for outside air ventilation) assign the damper actuator to a storage Orbit DO by repurposing an unused output. Not every site has dual humidifiers or climacell — in those cases, DO 4–8 have available slots:

| Site has | Available DOs for fresh air damper |
|---|---|
| No climacell, single humidifier | DO 4, 7, 8 |
| No climacell, no humidifier | DO 4, 5, 6, 7, 8 |
| Full equipment (all 10 used) | Use a second Orbit module dedicated to ventilation/damper control, or repurpose a status light (DO 1–3) |

**Fresh air damper actuators are electric hold-position** (motor-driven, non-spring-return). Each damper requires two DOs: one for the OPEN motor direction and one for the CLOSE motor direction. Nova positions dampers by writing timed pulses to the appropriate direction register (see §4.9). The full stroke time (default 360 seconds) is configurable per site in the UI. The comms-loss action for any damper motor output must be **0 (off)** — de-energize both motors so the actuator holds its current position. Nova periodically commands full-close to re-zero the position reference, so dampers are typically near-closed when comms loss occurs. See §4.7.

### 5.3 Storage Module — Digital Inputs

| DI | Equipment | Normal State | Alarm Condition |
|---|---|---|---|
| 1 | Low Temp Limit | Open (above limit) | Closed — room temperature below critical minimum, freeze protection |
| 2 | Power Failure | Closed (power OK) | Open — loss of main power to room equipment |
| 3 | Ventilation Fan VFD Proof | Closed (running) | Open — main ventilation fan VFD relay indicates failure or not running. Optional — only wired if VFD has a status relay output |
| 4 | Climacell Flow Switch | Closed (flow OK) | Open — no flow through climacell, valve/piping fault |
| 5 | Humidifier Head 1 Current Relay | Closed (current flowing) | Open — head 1 commanded ON but no current detected (clogged/failed nozzle) |
| 6 | Remote Standby | Open (normal run) | Closed — external signal requesting system standby (building automation, manual override) |
| 7 | Humidifier Head 2 Current Relay | Closed (current flowing) | Open — head 2 commanded ON but no current detected |
| 8 | Refrigeration Standby | Open (normal run) | Closed — external signal requesting refrigeration system to standby |
| 9 | Heat Current Relay | Closed (current flowing) | Open — heat commanded ON but no current detected (heater element failed, contactor stuck) |
| 10 | Cavity Heat Current Relay | Closed (current flowing) | Open — cavity heat commanded ON but no current detected |

**Notes:**
- Current relays (DI 5, 7, 9, 10) provide **proof of operation** — Nova can detect a stuck contactor or burned-out element by comparing the DO command state to the DI feedback
- Low Temp Limit (DI 1) triggers an immediate alarm — the control loop should already prevent this, but the limit switch is a safety backstop
- Remote Standby (DI 6) and Refrigeration Standby (DI 8) are external override inputs — useful for demand response or building automation integration

### 5.4 Storage Module — Analog Outputs

| AO | Equipment | Range | Control Source |
|---|---|---|---|
| 1 | Ventilation Fan VFD | 4-20mA → 0-100% speed | PID — room temperature control, ramp fan speed to maintain setpoint |
| 2 | Refrigeration (Modbus fallback) | 4-20mA → 0-100% capacity | PID — drives refrigeration capacity when Modbus RTU is not available |

**Notes:**
- AO 1 is the **primary speed reference** for the ventilation fan VFD — the storage Orbit's Port A is dedicated to analog boards (Modbus RTU master, 9600 baud), so the VFD gets its speed reference from the analog output instead. 4-20mA is the standard speed input on all VFDs and requires no protocol configuration. **On comms loss, AO 1 should hold its last setpoint (comms-loss register 0x010B = 1)** — the VFD continues running at its last commanded speed, keeping ventilation active. See §4.7.
- AO 2 is a **hardwired fallback** for refrigeration capacity control — 4-20mA signal to the compressor or unloader system in case Modbus RTU cannot be used (older equipment, incompatible VFD brand, site preference). When Modbus RTU is available, this AO is unused

---

### 5.5 Refrigeration Module — Digital Outputs

| DO | Equipment | Description | Watchdog |
|---|---|---|---|
| 1 | Compressor | Main compressor contactor | Yes — **30s timeout** |
| 2 | LLS (Liquid Line Solenoid) | Liquid refrigerant solenoid valve — opens to allow flow to evaporator | Yes |
| 3 | Unloader 1 | Capacity unloader solenoid 1 (reduces compressor capacity by ~25-33%) | Yes |
| 4 | Unloader 2 | Capacity unloader solenoid 2 (reduces compressor capacity by ~25-33%) | Yes |
| 5 | Condensing Fan 1 | Condenser fan contactor 1 | Yes |
| 6 | Condensing Fan 2 | Condenser fan contactor 2 | Yes |
| 7 | Condensing Fan 3 | Condenser fan contactor 3 | Yes |
| 8 | Condensing Fan 4 | Condenser fan contactor 4 | Yes |
| 9 | Open | Spare — available for future equipment (hot gas bypass, crankcase heater control, etc.) | — |
| 10 | Failure Relay | Wired via N.C. contact — relay SET = alarm clear, relay RESET (default) = alarm active. Signals external BMS/alarm panel | No |

**Notes:**
- Compressor (DO 1) has a **30-second watchdog** — if Nova stops communicating, the compressor shuts down. This is the single most important safety output
- LLS (DO 2) must close (de-energize) before compressor stops to pump down the evaporator — firmware enforces the pump-down sequence
- Unloaders (DO 3–4) de-energize on watchdog timeout — compressor runs at minimum capacity until orderly shutdown
- Condensing fans (DO 5–8) staged in sequence based on head pressure — firmware stages 1→2→3→4 on rising pressure, 4→3→2→1 on falling
- Failure Relay (DO 10) uses an external relay wired to the Orbit's N.C. logic. Default (output OFF) state = external relay de-energized = N.C. closed = alarm circuit active. Firmware turns the output ON once the system is confirmed healthy, energizing the external relay and opening the N.C. contact = alarm clear. On compressor fault, firmware turns the output OFF → external relay de-energizes → N.C. closes → BMS sees alarm. On module power-up (all outputs off) the Failure Relay starts in alarm state until firmware confirms healthy — correct fail-safe behavior

### 5.6 Refrigeration Module — Digital Inputs

| DI | Equipment | Normal State | Alarm Condition |
|---|---|---|---|
| 1 | Power Monitor | Closed (power OK) | Open — loss of power to compressor circuit, phase monitor relay tripped |
| 2 | (Nothing) | — | — (unused, available as spare) |
| 3 | Crankcase Heater | Closed (heater energized) | Open — crankcase heater failed, risk of liquid slugging on startup |
| 4 | Condenser Overload | Closed (OK) | Open — condenser fan motor overload tripped |
| 5 | Compressor Overload | Closed (OK) | Open — compressor motor overload relay tripped |
| 6 | Oil Pressure | Closed (OK) | Open — oil pressure differential switch tripped (lubrication failure) |
| 7 | High Head Pressure Switch | Closed (OK) | Open — discharge pressure above safe limit, compressor must stop immediately |
| 8 | Suction Pressure Switch | Closed (OK) | Open — suction pressure below safe limit (freeze-up risk) |
| 9 | Auto/Run | Closed (auto run enabled) | Open — local disconnect or service switch in OFF position, compressor locked out |
| 10 | Pump Down | Closed (OK) | Open — pump-down cycle complete or low-pressure cutout engaged |

**Notes:**
- Safety chain inputs (DI 4–8) are **hardwired N.C. contacts** — an open input always means a fault. This is fail-safe: a broken wire reads the same as a tripped switch
- Oil Pressure (DI 6) requires a **time delay** in firmware — oil pressure switch opens briefly during startup; Nova ignores it for the first 90-120 seconds after compressor start
- Auto/Run (DI 9) is the local panel disconnect — a tech can lock out the compressor at the panel without touching the Nova software
- Crankcase Heater (DI 3) should be checked **before** compressor start — if heater has been off, delay compressor start by 4-8 hours to allow oil warmup

### 5.7 Refrigeration Module — Analog Outputs

| AO | Equipment | Range | Control Source |
|---|---|---|---|
| 1 | Compressor VFD *or* Condensing Fan VFD | 4-20mA → 0-100% speed | Configurable at commissioning — see notes |
| 2 | EEV *or* Condensing Fan VFD | 4-20mA → 0-100% | Configurable at commissioning — see notes |

**AO 1 — configurable per site:**
- **Compressor VFD** (preferred) — drives compressor speed for capacity modulation. This offers the **largest energy cost savings** of any single VFD in the system
- **Condensing Fan VFD** (alternative) — drives condenser fan speed for head pressure control, if no compressor VFD is installed
- Configure at commissioning: one or the other, not both

**AO 2 — configurable per site:**
- **EEV** (default) — drives electronic expansion valve via 4-20mA for superheat control. If the EEV controller is on Modbus RTU (Section 7), this AO is unused
- **Condensing Fan VFD** (alternative) — if no EEVs are installed, AO 2 can drive a condensing fan VFD for head pressure control while AO 1 handles the compressor VFD

**Common configurations:**

| AO 1 | AO 2 | Scenario |
|---|---|---|
| Compressor VFD | EEV | Full modulation system — maximum efficiency |
| Compressor VFD | Condensing Fan VFD | No EEVs — both VFDs on analog, TXV for expansion |
| Condensing Fan VFD | EEV | No compressor VFD — fixed-speed compressor with unloaders |
| *(unused)* | *(unused)* | All VFDs and EEVs on Modbus RTU — 10/10/0 module variant sufficient |

---

### 5.8 Per-System I/O Summary

**Zone definition — one cold storage room + compressors + doors:**

A single zone can include one storage Orbit, one door control Orbit (optional, can be shared across zones), and up to four refrigeration Orbits — one per compressor system. Each refrigeration Orbit's Port B hosts the compressor VFD and EEV on a local Modbus RTU segment (19200 baud). The storage Orbit's Port A polls analog boards via Modbus RTU (9600 baud). The door Orbit typically has both ports free (no analog boards, no VFDs).

**Minimum zone (1 room, 1 compressor, no dedicated door module — 2 modules):**

| Resource | Storage Orbit | Refrigeration Orbit | Total |
|---|---|---|---|
| DO used | 10 of 10 | 9 of 10 (1 spare) | 19 |
| DI used | 10 of 10 | 9 of 10 (1 spare) | 19 |
| AO used | 2 of 2 | 2 of 2 | 4 |
| Port A (USART1) | Analog boards (auto-poll / 9600) | Free | 1 active |
| Port B (USART2) | Free | VFD/EEV (Modbus RTU / 19200) | 1 active |

The 10/10/2 Orbit module is an **exact fit** for both the storage and refrigeration roles — no wasted I/O, no I/O shortage. Analog boards connect via the storage Orbit's Port A (Modbus RTU master → Orbit input registers 0x0700+), eliminating the need for dedicated sensor modules.

**Typical site (2 rooms, 4 compressors, 1 shared door module — 7 modules):**

| DIP | IP | Zone | Role | Port A | Port B |
|---|---|---|---|---|---|
| 01 | 192.168.0.2 | Room A | Storage control | Analog boards (auto-poll / 9600) | Free |
| 02 | 192.168.0.3 | Room A | Refrigeration 1 | Free | VFD+EEV / 19200 |
| 03 | 192.168.0.4 | Room A | Refrigeration 2 | Free | VFD+EEV / 19200 |
| 04 | 192.168.0.5 | Room B | Storage control | Analog boards (auto-poll / 9600) | Free |
| 05 | 192.168.0.6 | Room B | Refrigeration 1 | Free | VFD+EEV / 19200 |
| 06 | 192.168.0.7 | Room B | Refrigeration 2 | Free | VFD+EEV / 19200 |
| 07 | 192.168.0.8 | Shared | Door control | Free | Free |

| Item | Count |
|---|---|
| Storage Orbit modules | 2 (one per room — Port A polls analog boards via Modbus RTU) |
| Door control Orbit module | 1 (shared — fresh air dampers, door actuators for both rooms) |
| Refrigeration Orbit modules | 4 (one per compressor — Port B hosts VFD/EEV) |
| **Total modules** | **7** |
| Total DO points | 70 (20 storage + 10 doors + 40 refrigeration) |
| Total DI points | 70 |
| Total AO points | 14 |
| RS485 segments | 14 (2× Port A for analog boards, 4× Port B for VFDs/EEVs, 8 free) |
| Total switch ports required | 7 Orbit modules + Nova = 8 of 8 on switch (upgrade to 16-port for spares) |
| Total Orbit power draw | 7 × 2.0W = 14W logic from 24VDC supply (external load current additional) |
| Total Orbit power draw | 6 × 2.0W = 12W logic from 24VDC supply (external load current additional) |

---

## 6. VFD Control — Modbus RTU via Orbit Module Port B

### 6.1 Why RTU for VFDs

Every ABB ACS380 includes RS485 Modbus RTU at no additional cost. Modbus TCP requires the FMBT-21 adapter at ~$200 per drive. For a system with 8 VFDs, RTU saves $1,600.

### 6.2 How It Connects

VFDs **do not** require their own RS485 homerun cable back to the main panel. Instead, each VFD connects via a short RS485 stub to **Port B on the nearest Orbit module**. The Orbit module tunnels the RTU traffic back to Nova over the existing Ethernet connection.

```
Nova Controller                                     Equipment Location
───────────────                                     ──────────────────
                   ┌──────────────────┐
  Modbus TCP       │  Refrig Orbit Mod  │     RS485 Port B (short stub, ~3-10 ft)
  ─────────────────┤  10DO/10DI/2AO    ├────────► Compressor VFD (addr 1)
  (via Eth switch) │  Port A: free      │────────► EEV controller (addr 2)
                   │  Port B: RTU 19200 │
                   └──────────────────┘
```

The refrigeration Orbit module is already located near the compressor — the VFD and EEV are right there. A 3-10 foot RS485 cable connects them. No long homerun, no dedicated UART on the AM2434.

### 6.3 Bus Configuration (Per Module)

| Parameter | Value |
|---|---|
| Physical connection | RS485 Port B on refrigeration Orbit module |
| Transport to Nova | Modbus TCP (tunneled through Orbit module) |
| Local baud rate | 19200 (configurable via Modbus register 0x0310) |
| Data format | 8-N-1 |
| Topology | Short daisy-chain from module to nearby VFDs/EEVs |
| Max distance | 3-10 ft typical (same equipment skid/panel) |
| Max devices per module | 4-6 (one compressor VFD + condensing fan VFD + EEV, or similar) |
| Poll rate | 120 s for VFD status display, 2 s for EEV superheat |
| Speed commands | On-demand from control loop, tunneled through TCP |

### 6.4 VFD Parameters (ABB ACS380)

Set on each VFD via built-in keypad:

| Parameter | Value | Purpose |
|---|---|---|
| 49.05 | EFB | Control source = Modbus |
| 49.06 | EFB | Reference source = Modbus |
| 53.01 | 1–N (unique per RS485 segment) | Modbus slave address |
| 53.02 | 19200 | Baud rate |
| 53.03 | 8-N-1 | Data format |
| 51.25 | 0 | Communication timeout = disabled (drive holds last speed forever) |
| 46.01 | 0 Hz | Minimum speed reference = 0 (linear scaling, 0-100% maps to 0-max Hz) |
| 22.02 | Per fan spec (e.g., 10 Hz) | Minimum speed limit (fan won't stall below this) |
| 22.01 | 50 or 60 Hz | Maximum speed |

### 6.5 VFD/EEV Address Map (Per Refrigeration Module)

Each refrigeration Orbit module has its own independent RS485 segment on Port B. Addresses are local to each segment — no cross-module conflicts.

| RS485 Addr | TCP Unit ID | Assignment |
|---|---|---|
| 1 | 101 | Compressor VFD |
| 2 | 102 | Condensing fan VFD (if installed) |
| 3 | 103 | EEV controller |
| 4 | 104 | Spare |

Nova addresses each device by the module's IP + the gateway unit ID. Example:
- Room A Compressor 1 VFD → TCP to 192.168.0.3, unit ID 101
- Room A Compressor 1 EEV → TCP to 192.168.0.3, unit ID 103
- Room B Compressor 2 VFD → TCP to 192.168.0.7, unit ID 101

Every refrigeration module uses the same RTU address map (1-4). No global address planning needed.

### 6.6 Ventilation Fan VFDs (Storage Orbit)

The storage Orbit's **Port A** is dedicated to analog boards (Modbus RTU master, 9600 baud — sensor data available in Orbit registers 0x0700+). Ventilation fan VFDs use **AO 1 (4-20mA → 0-100% speed)** — the simpler, more reliable approach. 4-20mA requires no protocol configuration, no address assignment, and works with every VFD brand. **Port B is free** on the storage Orbit — if Modbus RTU control of the ventilation VFD is ever preferred over 4-20mA, Port B is available.

| Signal | Source | Destination | Method |
|---|---|---|---|
| Speed reference | Storage Orbit AO 1 | VFD analog input | 4-20mA hardwired |
| Run/Stop | Storage Orbit DO (existing) | VFD digital input | Dry contact |
| VFD proof | VFD relay output | Storage Orbit DI 3 | Dry contact |

If a second ventilation fan VFD is needed and AO 2 is not reserved for refrigeration fallback, it can drive a second VFD. Alternatively, Port B can be configured for Modbus RTU to control multiple fan VFDs over RS485.

---

## 7. EEV Control — Via Orbit Module Port B

### 7.1 Connection Method

EEVs connect to the same RS485 Port B as VFDs on the refrigeration Orbit module. Each module's Port B segment has the compressor VFD, optional condensing fan VFD, and the EEV controller — all on one short local bus.

### 7.2 Supported EEV Controllers

| Manufacturer | Model | RTU built-in | Typical address |
|---|---|---|---|
| Carel | EVD Evolution | Yes | 3 |
| Danfoss | AK-CC 210/550 | Yes | 3 |
| Emerson | EC3-X33 | Yes | 3 |

### 7.3 EEV Poll Rate

| Parameter | Value |
|---|---|
| Superheat reading | Every 2 s |
| Valve position | Every 2 s |
| Setpoint writes | On demand |
| Transport | Modbus TCP to Orbit module → Port B RTU gateway → local RS485 |

---

## 8. Network Topology

### 8.1 Architecture Diagram — All I/O Over Ethernet Modbus TCP Star

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                           MAIN CONTROL PANEL                                  │
│                                                                               │
│  ISOLATED CONTROL NETWORK (192.168.0.0/24)    SITE NETWORK                   │
│  ─────────────────────────────────────   ────────────                   │
│                                                                               │
│  ┌────────────┐     ┌────────────────────┐                                    │
│  │  AM2434     │─Eth►│  Eth Switch          │      ┌──────────┐            │
│  │  Nova       │     │  192.168.0.0/24      │      │  RPi5     │            │
│  │  .0.1       │     │  NO internet gateway │      │  10.1.2.x │──► Internet│
│  │            │     └┬─┬─┬─┬─┬─┬────────┘      │  Web UI  │   Cloud    │
│  │            │─UART1►│serial bridge│───────────►  Cloud   │   Remote   │
│  │            │      │ │ │ │ │ │               └──────────┘            │
│  │            │      │ │ │ │ │ └─── .0.7 Rm B Refrig 2 (Orbit)       │
│  │            │      │ │ │ │ └──── .0.6 Rm B Refrig 1 (Orbit)       │
│  └────────────┘      │ │ │ └───── .0.5 Rm B Storage  (Orbit)        │
│                       │ │ └────── .0.4 Rm A Refrig 2 (Orbit)        │
│  UART0 ─► Debug       │ └─────── .0.3 Rm A Refrig 1 (Orbit)        │
│  UART2-8 → SPARE      └──────── .0.2 Rm A Storage  (Orbit)         │
│                                                                               │
└─────────────────────────────────────────────────────────────────────────────────┘

       │ Cat5             │ Cat5             │ Cat5
       ▼                 ▼                 ▼
  ┌──────────┐     ┌──────────┐     ┌──────────┐
  │ Room A   │     │ Room A   │     │ Room A   │
  │ Storage  │     │ Refrig 1 │     │ Refrig 2 │
  │ Orbit    │     │ Orbit    │     │ Orbit    │
  │10DO/10DI │     │10DO/10DI │     │10DO/10DI │
  │  2AO     │     │  2AO     │     │  2AO     │
  │ Port A──►│     │ Port B──►│     │ Port B──►│
  │ Analog   │     │  Comp    │     │  Comp    │
  │ Boards   │     │  VFD     │     │  VFD     │
  │ Port B:  │     │  Cond    │     │  Cond    │
  │  free    │     │  VFD     │     │  VFD     │
  └──────────┘     │  EEV     │     │  EEV     │
  AO 1 → Vent VFD └──────────┘     └──────────┘
                   (local stubs)    (local stubs)

  Same layout repeated for Room B (3 more Orbit modules)
```

### 8.2 Twin Cold Storage System — Module Count

| Bus | Devices | Protocol | Infrastructure |
|---|---|---|---|
| Ethernet 192.168.0.0/24 | 6× Orbit + Nova = **7 devices** | Modbus TCP | Ethernet switch ($25) + 6× Cat5 runs |
| UART1 (serial) | RPi5 → Nova data bridge | Binary packet | 3-wire serial, point-to-point |
| Port B per refrig Orbit | 1-3 RTU devices each (VFD, cond VFD, EEV) | Modbus RTU (tunneled) | 3-10 ft stub per module |
| Port A per storage Orbit | Analog boards (Modbus RTU, up to 32) | Modbus RTU master (sensor data in Orbit regs) | 3-10 ft stub to analog board |

**RPi5 is NOT on the control switch.** It connects to Nova via UART1 and to the site network via its own Ethernet. The Modbus TCP control network is physically isolated from the internet.

### 8.3 Cost Comparison

| Item | Pure RS485 RTU | Modbus TCP Star (Primary Architecture) |
|---|---|---|
| 6× Orbit modules | 6 × $9 (RTU combo) = $54 | 6 × ~$25 (TCP + dual RS485 + ULN2803A DO + pluggable terminal) = $150 |
| Legacy analog boards | 2 × $9 = $18 (existing, unchanged) | 2 × $9 = $18 (existing, via Orbit Port A) |
| 8× VFD communication | $0 (RTU built in) | $0 (RTU built in, local to Orbit Port B) |
| 4× EEV communication | $0 (RTU built in) | $0 (RTU built in, local to Orbit Port B) |
| Ethernet switch (8-port) | $0 | $25 (unmanaged gigabit) |
| RS485 homerun cable (3+ buses) | $360 | **$0** (eliminated — all local stubs) |
| RS485 stub cable (local, 3-10ft) | $0 | $30 (short pre-made cables) |
| Cat5 cable (7 devices) | $0 | $110 |
| 24V power wiring to modules | $100 | $80 (24VDC from DIN-rail supply to each module) |
| RS485 hub on Nova board | $4 | **$0** (not populated on new boards) |
| Analog board power/cabling | $50 | **$0** (shares same 24V supply as Orbit) |
| 24VDC DIN-rail supply (UL-listed) | Already in panel | $15 (Mean Well HDR-30-24 or equiv.) |
| UPS for Nova + RPi5 + switch | $0 | $50 (300VA, powers outage detection + logging) |
| **Total infrastructure** | **~$587** | **~$496** |
| **Install labor difference** | Baseline | **Significantly less** — no RS485 homerun pulls, star topology, shared 24V rail |
| **Fault isolation** | Entire bus affected by one fault | **Per-module** — fault on one module can't affect any other |
| **Scan speed** | Sequential: ~100 ms for all modules | **Parallel: < 2 ms for all modules** |
| **VFD/EEV fault isolation** | All VFDs on one bus — one fault takes out all | **Per-compressor** — each module's RS485 is independent |
| **Analog board isolation** | All analog boards share one RS485 bus from brain | **Per-room** — each storage Orbit's Port A is independent |

---

## 9. Panel Design — Software A/O/M Overrides

### 9.1 Eliminating Physical Selector Switches

Physical Auto/Off/Manual 3-position selector switches are replaced with software overrides on the RPi5 UI "Equipment Control" page. No code (NEC, UL 508A) requires physical A/O/M switches for HVAC/R equipment. Only the **E-stop** and **disconnect means** (breaker) are required to be physical.

### 9.2 The E-Stop Stays Hardwired

```
[E-STOP mushroom button] ─── N.C. contact ─── hardwired to:
   ├── Compressor 1 contactor hold-in circuit (breaks coil power)
   ├── Compressor 2 contactor hold-in circuit
   ├── Condenser fan contactor
   └── DI module input (so Nova knows E-stop was pressed for logging)
```

The E-stop physically cuts contactor coil power — it does not go through Nova, the Modbus bus, or any software. Contactors drop mechanically.

### 9.3 Equipment Control Page

The RPi5 web UI provides per-equipment override control:

- **One tap** to change mode (AUTO / OFF / MANUAL ON)
- **Bold visual state** (green = running, grey = stopped, red = alarm)
- **"MANUAL OVERRIDES ACTIVE" banner** whenever anything is not in AUTO
- **"RESET ALL TO AUTO" button** — one tap returns everything to normal
- **Override persists through power cycle** — stored in Nova flash (ping-pong safe)
- **Optional override timeout** — auto-return to AUTO after N hours (catches forgotten overrides)
- **Override logging** — timestamped record of who changed what and when

### 9.4 What Happens When the Screen Dies

The RPi5 is the UI, not the controller. If the RPi5 dies:
- Nova continues running all control loops on last mode (AUTO stays AUTO)
- No overrides can be changed until RPi5 is restored
- A tech who needs to force a compressor off can: (1) flip the breaker, (2) press E-stop, (3) access RPi5 web UI from phone/laptop on site network

### 9.5 Savings Per Panel

| Item | With physical switches (10 per system) | Without |
|---|---|---|
| Selector switches | 20 × $3 = $60 | $0 |
| I/O expanders (MCP23017) | $3.60 | $0 |
| Ribbon cable + headers | $2.50 | $0 |
| Panel door drilling (20 holes) | ~$40 labor | $0 |
| Panel door space | Large panel door | Smaller panel |
| **Total savings per panel** | | **~$106 + smaller panel** |

---

## 10. Ethernet Switch Selection

### 10.1 Requirements

| Requirement | Value |
|---|---|
| Ports | 8 minimum (6 Orbit modules + Nova + spare) — RPi5 is NOT on this switch |
| PoE | **Not required** — Orbit modules powered locally from 24VDC |
| Management | **Not required** — Modbus TCP is plain TCP/IP on port 502, works on any switch |
| Temperature | 0-50°C minimum (panel interior) |
| Mounting | DIN rail preferred, shelf acceptable |

### 10.2 Managed vs Unmanaged

Modbus TCP packets are tiny (~20 bytes), low-frequency (250 ms polls), and standard unicast TCP. There is no QoS requirement, no multicast, no VLAN dependency. An unmanaged switch is the correct choice for production:

- **Fewer failure modes** — no configuration to corrupt, no firmware to update
- **Nothing to misconfigure** — no admin password, no VLAN/STP mistakes
- **Same performance** — Modbus TCP saturates < 0.01% of a 100 Mbps link

Managed switches are only useful during development (port mirroring for Wireshark) or if the I/O network shares infrastructure with office traffic (VLANs) — which it should not.

### 10.3 Options

| Switch | Ports | Type | DIN rail | Approx cost |
|---|---|---|---|---|
| **TP-Link TL-SG108** | **8** | **Unmanaged gigabit** | No | **$25** |
| Netgear GS108 | 8 | Unmanaged gigabit | No | $30 |
| Moxa EDS-208 | 8 | Industrial unmanaged | **Yes** | $150-200 |
| TP-Link TL-SG1016D | 16 | Unmanaged gigabit | No | $50 |

**Recommendation:** TP-Link TL-SG108 (~$25, 8-port gigabit, unmanaged). Adequate for 7 devices with 1 spare port. If more modules are anticipated, step up to a 16-port (~$50). Industrial DIN-rail switches (Moxa) are justified only if the panel environment demands it (vibration, extreme temperatures). The switch goes on the UPS with Nova and RPi5.

---

## 11. UART Allocation Summary (AM2434)

| UART | Function | Baud | Protocol |
|---|---|---|---|
| UART0 | Debug console | 115200 | ASCII serial |
| UART1 | RPi5 bridge (or Ethernet replaces) | 230400 | Agristar binary packet |
| UART2 | **Spare** (or legacy analog hub on retrofit sites) | — | |
| UART3 | **Spare** | — | |
| UART4 | **Spare** | — | |
| UART5 | **Spare** | — | |
| UART6 | **Spare** | — | |
| UART7 | **Spare** | — | |
| UART8 | **Spare** | — | |

**Note:** With Orbit modules handling all field I/O, analog boards, and RTU gateways over Ethernet, the AM2434 needs **zero dedicated UARTs for field devices on new installations**. Only the debug console and RPi5 bridge use UARTs. **Eight UARTs are spare** — available for retrofit legacy analog support, future CAN FD bridging, or direct RS485 for site-specific needs.

---

## 12. FreeRTOS Task Architecture (R5F-0)

Nova runs one control loop task per zone. Each zone task owns the Orbit modules assigned to that zone — it reads their DI/sensor registers, runs the control logic, and writes DO/AO commands. Zone assignment is stored in Nova's config flash and configured through the UI at commissioning.

| Task | Priority | Stack | Rate | Description |
|---|---|---|---|---|
| ModbusTcpClient | 5 (high) | 4 KB | 250 ms | Query all Orbit modules in parallel via TCP sockets. Update di_state[], verify do_matches_commanded[], read AO feedback. Also tunnels VFD/EEV RTU requests through module Port B. |
| VfdGatewayPoll | 3 | 1 KB | 120 s | Read VFD status via TCP to Orbit module Port B (unit IDs 101+). Speed commands via event queue. No UART needed on Nova. |
| EevGatewayPoll | 4 | 1 KB | 2 s | Read EEV superheat/position via TCP to Orbit module Port B (unit IDs 103+). Setpoint writes on demand. No UART needed on Nova. |
| AnalogPoll | 4 | 512 B | 250 ms | Read sensor data from Orbit module Modbus input registers (0x0700+). Orbit firmware polls analog boards via Modbus RTU on Port A — Nova just reads standard Modbus registers. |
| ZoneControl_A | 4 | 2 KB | 1 s | Room A: storage control (ventilation fan PID, humidity, heating), up to 4 refrigeration compressor loops (suction pressure, staging, defrost, fan staging), door control (fresh air dampers, interlocks), alarm evaluation. Owns: 1 storage Orbit + 1–4 refrigeration Orbits + 0–1 door Orbits. |
| ZoneControl_B | 4 | 2 KB | 1 s | Room B: identical logic, separate state struct, separate Orbit assignments. |
| ZoneControl_Doors | 4 | 1 KB | 1 s | Shared door control (if door Orbit is shared across zones rather than assigned to one zone). Fresh air damper sequencing, position feedback, interlock logic. Runs independently of room control loops — coordinates via shared state. |
| CommandWriter | 5 (high) | 1 KB | Event-driven | Receives commands from ZoneControl tasks. Sends Modbus TCP writes to Orbit modules, Modbus RTU writes to VFDs/EEVs. |
| SafetyMonitor | 5 (high) | 512 B | 100 ms | Checks DI inputs for safety trips (HP cutout, E-stop, phase loss). Bypasses ZoneControl — sends immediate commands. |
| Rpi5Bridge | 3 | 2 KB | On-demand | UART1 or Ethernet. Serves sensor/status data, receives UI commands. |
| Watchdog | 6 (highest) | 256 B | 1 s | Monitors heartbeats from all tasks. Missing beats → safe-state all outputs. |

**Zone task structure — what each ZoneControl task manages:**

```
ZoneControl_A {
  Storage Orbit (192.168.0.2):
    - Read DI: door switches, temp limits, current relays
    - Read sensors: analog boards via registers 0x0700+
    - Write DO: fans, humidifiers, heaters, dampers
    - Write AO: ventilation fan VFD speed (4-20mA)

  Refrigeration Orbit 1 (192.168.0.3):
    - Read DI: HP/LP cutouts, oil pressure, overloads
    - Write DO: compressor, LLS, unloaders, condenser fans
    - Write AO: compressor VFD speed, EEV position
    - Port B RTU: VFD speed/status, EEV superheat/position

  Refrigeration Orbit 2 (192.168.0.4):
    - (same structure as Refrig 1)

  Door Orbit (192.168.0.8) — shared, or owned by this zone:
    - Read DI: door position switches, limit switches
    - Write DO: fresh air damper actuators (spring-return close)
}
```

Each zone task is a self-contained control thread. Adding a room means adding another ZoneControl task with its Orbit assignments. The task reads all its Orbits, runs the PID loops and state machines, and pushes commands to CommandWriter. At ~2% CPU per zone, a Nova running 8 zones (8 rooms, 32 compressors, 2 door modules) uses ~16% of one R5F core.

**Total SRAM:** ~20 KB stacks + ~16 KB state + ~8 KB TCP socket buffers + ~4 KB Modbus buffers = **~48 KB** out of 2 MB.

---

## 13. Scaling

A zone is one cold storage room with its full complement of Orbit modules. Each zone can include:
- 1× storage Orbit (ventilation, humidity, heating, analog boards on Port A)
- 0–1× door control Orbit (fresh air dampers, door actuators — can be shared across zones)
- 1–4× refrigeration Orbits (one per compressor, each with VFD/EEV on Port B)

All modules in a zone are assigned to one ZoneControl task on Nova. Adding a room = adding modules + configuring a new zone in the UI.

| Change | What to do |
|---|---|
| Add 3rd/4th cold storage room | Add more Orbit modules to Ethernet switch. Same Nova firmware — configure new zones in UI. Each zone gets 1 storage + 1–4 refrig + optional door Orbits. |
| More compressors per room | Add refrigeration Orbits. Up to 4 per zone, each with its own VFD/EEV on Port B. |
| More VFDs per compressor | Add to the nearest Orbit module's Port B (up to 4-6 per module), or add another Orbit module. |
| Dedicated door control | Add a door control Orbit. Assign to a specific zone or share across zones. |
| More EEVs | Add to the nearest refrigeration Orbit's Port B. Each module's segment is independent. |
| More sensors | Add "Sensor" variant Orbit modules (10× 4-20mA AI) to Ethernet switch, or retain analog boards on Port A. Analog board addresses are per-Orbit (namespaced), so adding rooms never conflicts with existing addresses. |
| Modulating actuators | Use AO outputs on existing modules, or add AO-heavy variants. |
| Multi-building | Add Ethernet fiber media converters for runs > 100 m. No protocol change. |
| Replace analog boards entirely | Deploy "Mixed" Orbit module variant (8DO/8DI/2AO/4AI) for rooms that need local sensor inputs. Standard analog boards (reflashed to Modbus RTU) continue to work on Port A indefinitely. Phase out legacy boards when convenient. |

### 13.1 Maximum Single-Nova Capacity

| Configuration | Orbits | Switch | 24V PSU | Analog boards | System cost |
|---|---|---|---|---|---|
| 1 room, 2 compressors | 4 (1 stor + 1 door + 2 refrig) | 8-port | 30W | up to 32 | ~$525 |
| 2 rooms, 4 compressors | 8 (2 stor + 1 door + 4 refrig + 1 spare) | 8-port | 30W | up to 64 | ~$650 |
| 4 rooms, 8 compressors | 14 (4 stor + 2 door + 8 refrig) | 16-port | 60W | up to 128 | ~$950 |
| 5 rooms, 10 compressors | 17 (5 stor + 2 door + 10 refrig) | 16-port | 60W | up to 160 | ~$1,070 |
| **8 rooms, 32 compressors** | **44** (8 stor + 4 door + 32 refrig) | **48-port** | **150W** | **up to 256** | **~$2,500** |
| Theoretical max (63 Orbits) | 63 | 2× 48-port cascaded | 250W | up to 2,016 | — |

The 8-room/32-compressor configuration (44 Orbits) uses ~27% of one AM2434 core and ~80 KB of 2 MB SRAM. Full I/O scan completes in ~9 ms against a 100 ms control loop. The 6-position DIP switch supports up to 63 modules, and the 192.168.0.0/24 subnet provides 253 usable addresses — neither is a bottleneck.

---

## 14. Data Logging Architecture

The Constellation platform uses a three-tier logging strategy that provides gap-free data recording from sensor reading to long-term archive. The key design principle: **Nova never depends on RPi5 for data integrity.**

### 14.1 Tier 1 — Nova SRAM Ring Buffer (Real-Time)

Nova maintains a **64 KB circular buffer** in its 2 MB ECC SRAM. Because Nova is the central Modbus TCP master polling **every Orbit module** on the control network, the ring buffer contains a unified log of the entire system. Every control cycle (~100 ms), the following are appended from all modules:

- All sensor readings from every Orbit — suction/discharge temps, pressures, ambient (including analog board data from each module's Port A Modbus RTU polling)
- Equipment states from every Orbit — compressor run/stop, valve positions, fan staging, DI/DO status
- VFD/EEV feedback from every Orbit's Port B — speed, frequency, superheat, fault codes
- Alarm events with timestamps (from DS3231MZ+ RTC)
- Setpoint changes and manual overrides

At typical logging density (~25 bytes per record, 10 records/second), the ring buffer holds **~40+ hours** of data before overwriting. This means a dead RPi5 does not cause data loss — Nova keeps logging internally and the RPi5 catches up when it reboots.

| Parameter | Value |
|---|---|
| Buffer size | 64 KB (configurable, up to 256 KB) |
| Record size | ~25 bytes average (variable-length) |
| Write rate | ~10 records/second (all zones combined) |
| Retention | ~40 hours at typical density |
| Location | AM2434 ECC SRAM, battery-backed by RTC domain |

### 14.2 Tier 2 — RPi5 Filesystem Archive (Primary Storage)

The RPi5 pulls logged data from Nova over UART1 using the existing `GellertFileSystem.out` service (port 9209). This is the same mechanism used by the current AS2 platform — proven and reliable.

**Data flow:**
```
Nova SRAM buffer  →  UART1 (230400 baud)  →  RPi5 GellertFileSystem.out  →  /var/log/agristar/
```

**File format:** CSV files, one per day, named `YYYY-MM-DD.csv`. Each row contains a UTC timestamp, tag name, and value. This format is human-readable, grep-friendly, and easily imported into spreadsheet tools for field troubleshooting.

**Log rotation (cron):**
| Age | Action |
|---|---|
| 0–7 days | Raw CSV on filesystem |
| 7–90 days | Compressed (gzip) |
| 90+ days | Deleted |

On a 32 GB SD card with the OS and UI consuming ~8 GB, this provides **~12 months** of compressed log storage at typical site density before rotation is needed.

### 14.3 Tier 3 — Azure Cloud Telemetry (Optional)

For internet-connected sites, the RPi5's `iotclient` service pushes telemetry to Azure IoT Hub over the site network. This is the same cloud path used by current AS2 systems.

- **Connected sites:** Real-time telemetry streams to Azure. Maintenance dashboards, remote alarm notifications, and historical trend analysis are available via the Agristar cloud portal.
- **Offline sites:** Cloud logging is simply not configured. The system operates identically — Tier 1 and Tier 2 provide full local logging. No data is lost.

Cloud logging is a **convenience layer**, not a dependency. The control system, alarms, and local data recording function identically whether the RPi5 has internet or not.

### 14.4 eMMC Backup Option (DNP)

The Nova PCB includes a **footprint for an eMMC flash** (Do Not Populate by default). For sites that require logging to survive a dead RPi5 SD card, the eMMC can be populated at commissioning for ~$4.

When populated, Nova writes a secondary copy of log data to eMMC in addition to the SRAM ring buffer. This provides weeks of on-board non-volatile storage independent of the RPi5.

For most installations, this is unnecessary — the SRAM ring buffer bridges any RPi5 reboot or SD card replacement, and the RPi5 catches up automatically.

### 14.5 Data Flow Summary

```
Sensor/Equipment                                     Azure IoT Hub
      │                                                    ▲
      ▼                                                    │ (optional)
┌───────────┐    Modbus TCP     ┌──────────┐              │
│   Orbit    │ ──────────────── │   Nova   │              │
│  modules   │     Ctrl LAN      │  AM2434  │              │
└───────────┘                   └────┬─────┘              │
                                     │                     │
                              ┌──────┴──────┐              │
                              │ SRAM Ring   │              │
                              │ Buffer 64KB │              │
                              └──────┬──────┘              │
                                     │ UART1               │
                                     ▼                     │
                              ┌─────────────┐       ┌─────┴──────┐
                              │    RPi5     │ ───── │  iotclient │
                              │ /var/log/   │  site │  (Azure)   │
                              │ agristar/   │  net  └────────────┘
                              └─────────────┘
```

---

## 15. Security Hardening

The Constellation's physical air gap (§8.2) is the first line of defense, but the RPi5 is the one component that straddles both worlds — it has internet access AND a direct serial line to Nova. This section addresses the "what if the RPi5 is compromised?" threat model.

**Design philosophy: assume the RPi5 will eventually be compromised and ensure it doesn't matter.** Nova validates everything, bounds-checks everything, and logs everything.

### 15.1 Nova-Side Command Validation (Primary Defense)

Every command arriving over UART1 must pass all four checks before Nova acts on it. This is the real defense — it runs on Nova's protected firmware, not on the RPi5.

| Check | Implementation |
|---|---|
| **Command whitelist** | Nova accepts only a fixed set of known command types: override, setpoint change, alarm acknowledge, status request, time sync, configuration write. Any unrecognized command type is dropped and logged to the SRAM ring buffer. |
| **Bounds enforcement** | Every parameter is range-checked against physical safety limits stored in Nova's flash config. "Set compressor speed to 200%", "set suction setpoint to −80°C", or "write to holding register outside the allowed range" are all rejected. These limits are enforced by Nova firmware — the RPi5 cannot change them. |
| **Rate limiting** | Nova accepts at most 10 commands per second over UART1. A compromised RPi5 spamming rapid-fire commands is throttled. Excess commands are dropped and a rate-limit alarm is raised. |
| **Audit log** | Every UART1 command — accepted or rejected — is logged to the SRAM ring buffer with RTC timestamp, command type, parameters, and accept/reject reason. This forensic trail survives RPi5 compromise and is available for post-incident analysis. |

Even if an attacker has full root on the RPi5, they can only send commands that Nova is willing to accept, within bounds Nova enforces independently.

### 15.2 UART1 Protocol Hardening

The binary packet protocol (^tag=value$CRC!) provides basic integrity, but additional layers prevent forgery and replay:

| Mechanism | Implementation |
|---|---|
| **HMAC-SHA256** | Each UART1 packet includes an HMAC-SHA256 digest computed with a 256-bit shared secret. The secret is stored in Nova's OSPI flash (write-protected region) and in an encrypted keystore on the RPi5. A rogue process on the RPi5 without access to the key cannot forge valid commands. |
| **Sequence numbers** | Each packet carries a monotonically increasing 32-bit sequence number. Nova rejects packets with a sequence number ≤ the last accepted value. Prevents replay attacks from captured traffic. |
| **Session nonce** | Nova generates a 128-bit random nonce at boot and sends it to the RPi5 over UART1. All HMACs include this nonce. Captured packets from previous boot sessions are cryptographically invalid. |
| **Timeout** | If no valid authenticated packet arrives within 60 seconds, Nova raises a UART1 comms alarm. The control loop continues with last-known-good state — it does not freeze or fail-open. |

> **Limitation:** If an attacker has full root on the RPi5, they can read the HMAC key from the bridge process memory. HMAC protects against rogue processes and privilege escalation — not against full root compromise. That is why §15.1 Nova-side validation is the primary defense.

### 15.3 RPi5 OS Hardening

| Measure | Implementation |
|---|---|
| **Read-only root filesystem** | Mount `/` as read-only with a tmpfs overlay for `/tmp` and `/var/run`. A compromised process cannot persist malware across a power cycle. Reboot the RPi5 and it returns to its known-good image. |
| **Minimal image** | No desktop environment, no avahi/mDNS, no Bluetooth stack, no development tools, no package manager. The image contains only: lighttpd, bridge server, GellertFileSystem.out, iotclient, and supporting services. Fewer packages = fewer CVEs. |
| **Zero inbound ports from internet** | The RPi5 has **no open ports accessible from the internet**. The web UI (port 80/443) is served only on the site LAN (10.x). Cloud connectivity uses an outbound-only TLS connection to Azure WebSites — the RPi5 initiates the tunnel and holds it open. There is nothing to scan, nothing to probe, nothing to exploit from the public internet. |
| **nftables firewall** | Inbound: port 80/443 from site LAN only (web UI). Inbound from WAN: **DENY ALL**. Outbound: Azure IoT Hub (TLS 443), NTP (port 123). All other traffic dropped. |
| **SSH disabled by default** | For field maintenance, SSH can be enabled temporarily via a physical DIP switch or USB serial console. When enabled: key-only authentication (no passwords), non-standard port, fail2ban active, site LAN only. |
| **Automatic security updates** | `unattended-upgrades` for Debian/Ubuntu security patches only. Application code (bridge server, lighttpd config) is not affected by OS security updates. |
| **Service isolation** | Each service (bridge server, lighttpd, GellertFileSystem) runs as a dedicated non-root user with minimal filesystem permissions. systemd sandboxing: `ProtectHome=true`, `PrivateDevices=true`, `NoNewPrivileges=true`. Only the bridge server process has access to the UART device. |

### 15.4 Control Network Protection

| Measure | Implementation |
|---|---|
| **No IP route to internet** | The control network (192.168.0.0/24) has no router, no gateway, no DNS. The RPi5 is not on this network. There is no software bridge, no NAT, no tunnel. |
| **Orbit firmware signature** | The Modbus TCP bootloader (§4.5) verifies a CRC32 checksum on the firmware image before writing. A future enhancement can upgrade this to ECDSA signature verification once the Orbit MCU has sufficient flash headroom. |
| **Modbus protocol limits** | Orbits only accept function codes 0x01–0x06 and 0x0F–0x10 (standard read/write coils, registers). Any other function code is silently dropped. Register addresses outside the defined map (§4.5) return exception code 0x02 (illegal address). |

### 15.5 Defense-in-Depth Summary

```
Azure ← outbound TLS ← RPi5 → UART1 → Nova → Control Network → Orbit modules
  │                      │        │        │          │
  │ (no inbound ports)   │        │        │          └── §15.4 Modbus whitelist, firmware verification
  │                      │        │        └───────────── §15.1 Command whitelist + bounds + rate limit + audit
  └──────────────────────│        └────────────────────── §15.2 HMAC-SHA256 + sequence numbers + session nonce
  Mobile app commands    └─────────────────────────────── §15.3 Read-only FS, no inbound ports, minimal image
  flow: App → Azure →
  outbound tunnel → RPi5
```

The RPi5 has **zero inbound ports from the internet**. The only internet path is an outbound TLS tunnel to Azure that the RPi5 initiates and holds open. Mobile app commands flow: App → Azure → existing tunnel → RPi5 → UART1 → Nova. An internet attacker has no entry point — there is nothing to scan, probe, or exploit.

Each defensive layer operates independently. Compromising the Azure account does not bypass the HMAC. Stealing the HMAC key does not bypass Nova's bounds enforcement. No single compromise grants control over refrigeration equipment.

---

## 16. Migration Path

### Phase 1 — Immediate (Development)

- Port TM4C firmware to AM2434 (Nova) using MCU+ SDK + FreeRTOS
- Use Waveshare RS485 modules for lab testing (cheap, available now)
- Validate Modbus RTU control of VFDs and EEVs
- Design custom Orbit module schematic + PCB

### Phase 2 — Prototype

- Build 5 prototype Orbit modules
- Integrate W5500 Modbus TCP slave firmware + dual RS485 gateway
- Test parallel querying from AM2434 lwIP stack
- Lab test: 6 Orbit modules + 8 VFDs + 4 EEVs, full control loop

### Phase 3 — Pilot

- Deploy one twin cold-storage system at a controlled site
- Run alongside existing controller for 30 days
- Validate: power stability, watchdog behavior, scan times, alarm response
- Iterate on Orbit module hardware if needed

### Phase 4 — Production

- Manufacture Orbit modules in quantity (100+ batch)
- UL 508A listing for complete panel assembly
- Replace all new installations with Constellation platform
- Legacy TM4C systems continue operating — no forced upgrade

---

## Appendix A — Reference Documents

| Document | Location | Content |
|---|---|---|
| AM2612 Platform Architecture | docs/AM2612-Platform-Architecture.md | Comprehensive 18-section architecture document covering all design decisions |
| Output Relay Technology Analysis | docs/Output-Relay-Technology-Analysis.md | EMR vs SSR, contactor sizing, EEV analysis, UL strategy |
| RTU Relay Module Comparison | docs/RTU-Relay-Module-Comparison.md | Module tiers, hybrid approach, cost comparison |
| RTU Migration Guide | docs/RTU-Migration-Guide.md | Step-by-step migration from CPLD to Modbus RTU |
| VFD Modbus RTU Architecture | docs/VFD-Modbus-RTU-Architecture-Proposal.md | VFD-specific RS485 design |

---

## Appendix B — Constellation Data Flow Summary

This section traces every data path in the Agristar Constellation platform from physical sensor to user's browser, covering all three communication channels on every Orbit module.

### B.1 The Three Networks

The Constellation has **three physically separate communication paths** that never share copper:

```
 ┌─────────────────────────────────────────────────────────────────────────┐
 │                                                                         │
 │   1. CONTROL NETWORK              2. SERIAL BRIDGE         3. SITE     │
 │   ──────────────────              ──────────────────       ──────────   │
 │   192.168.0.0/24                  UART1 (230400)           10.x/DHCP   │
 │   Eth switch ◄──► Nova           Nova ◄──► RPi5           RPi5 ──► Azure (outbound TLS)
 │   Nova ◄──► 6× Orbit modules     Point-to-point           Web UI (site LAN only)
 │   Air-gapped from internet        3-wire serial             No inbound ports │
 │                                                                         │
 └─────────────────────────────────────────────────────────────────────────┘
```

1. **Control Network (Ethernet, 192.168.0.0/24):** Nova (192.168.0.1) talks to all Orbit modules (.2–.16) over Modbus TCP through an Ethernet switch. No DNS, no internet. Physical air gap.
2. **Serial Bridge (UART1, 230400 baud):** Nova talks to the RPi5 over a point-to-point serial link. Binary packet protocol. Not network-addressable.
3. **Site Network (RPi5 Ethernet, 10.x/DHCP):** RPi5 serves the web UI on the site LAN only (no public-facing ports). Cloud connectivity uses an **outbound-only TLS tunnel** to Azure WebSites — the RPi5 initiates the connection and holds it open. There are zero inbound ports from the internet.

A compromised internet connection cannot reach the control network. There is no IP route, no bridge, no software path — only UART1 separates them, and it speaks a fixed binary protocol, not TCP/IP. The RPi5 itself has no inbound internet ports — the only path in is through Azure's authenticated outbound tunnel.

### B.2 Data Flow — Sensor Reading to Browser

**Example: Room A suction temperature reading from analog board → displayed on RPi5 web UI**

```
Step  Location                 What Happens
────  ────────────────────     ──────────────────────────────────────────────────
 1    Analog Board (Room A)    NTC thermistor senses 2.3°C suction temperature.
                               Board's PIC16F876 ADC reads the resistance, converts
                               to a 16-bit scaled value; board waits for a Modbus
                               RTU query on RS485 (address set by DIP switch).

 2    Orbit 1 (Rm A Storage)   Orbit's Port A Modbus RTU master sends FC 04
      Port A (USART1)          (Read Input Registers) to analog board address at
                               9600 baud. Board responds with 4 sensor values.
                               Orbit stores values in input registers 0x0700+.

 3    RS485 bus (Port A)       Modbus RTU frame travels 3-10 ft on RS485 to analog
                               board and back. Standard CRC-16 error detection.

 4    Orbit 1 → Nova           Nova reads Orbit input registers 0x0700+ via
      (Ethernet return)        standard Modbus TCP FC 04. The 16-bit temperature
                               value: 2.3°C (stored as 230 = ×100 scaling).

 5    Nova (R5F-0)             ControlLoop_A task reads suction_temp = 2.3°C.
                               PID loop compares to setpoint. Decides: compressor
                               VFD → 65% speed, unloader 2 → ON, condenser fan
                               staging → 3 of 4 ON. Writes commands to output
                               queue.

 6    Nova → RPi5              Rpi5Bridge task on R5F-1 packages current state
      (UART1 serial)           into binary packet (^tag=value$CRC!) and sends
                               over UART1 at 230400 baud to RPi5.

 7    RPi5 (bridge server)     Bridge server (Node.js, ui-svelte/server/) receives
                               serial packet, parses suction_temp=2.3, updates
                               in-memory cache, pushes via WebSocket to browser.

 8    Browser (user)           Svelte UI receives WebSocket update. Room A tile
                               shows "Suction: 2.3°C". Real-time, sub-second
                               latency end-to-end.
```

### B.3 Data Flow — User Command to Equipment

**Example: User taps "Compressor 1 → MANUAL OFF" on RPi5 web UI**

```
Step  Location                 What Happens
────  ────────────────────     ──────────────────────────────────────────────────
 1    Browser                  User taps MANUAL OFF on Room A Compressor 1.
                               Svelte UI sends REST POST to RPi5 lighttpd.

 2    RPi5 (bridge server)     Bridge server receives command, translates to
                               binary packet: ^OVERRIDE=RM_A_COMP1,OFF$CRC!
                               Sends over UART1 at 230400 baud to Nova.

 3    Nova (R5F-1 → R5F-0)    Rpi5Bridge task on R5F-1 receives packet, validates
                               CRC, forwards override command to ControlLoop_A
                               via FreeRTOS queue. Override stored in flash
                               (ping-pong safe).

 4    Nova (R5F-0)             ControlLoop_A sees override: comp1_mode = OFF.
                               Initiates pump-down sequence: close LLS (DO 2),
                               wait for suction pressure to drop, then open
                               compressor contactor (DO 1). Writes commands via
                               CommandWriter task.

 5    Nova → Orbit 2           CommandWriter sends Modbus TCP to Orbit 2 at
      (Ethernet)               192.168.0.3: write coil 0x0001 = OFF (DO 2, LLS),
                               then after pump-down: write coil 0x0000 = OFF
                               (DO 1, compressor). Also sends unit ID 101 to
                               Port B: VFD speed = 0%.

 6    Orbit 2 (Rm A Ref 1)    Module de-energizes coils for DO 1 and DO 2
      Port B (USART2)          (relays open). Compressor contactor drops.
                               LLS closes. Module also sends Modbus RTU to
                               VFD on Port B: speed reference = 0%, run
                               command = stop.

 7    VFD (ABB ACS380)         VFD receives RTU stop command on RS485 Port B,
                               ramps compressor down to 0 Hz, opens output
                               contactor. Compressor stops.

 8    Orbit 2 → Nova           On next poll (250 ms), Orbit 2 reports: DO 1 = OFF,
      (Ethernet return)        DI 9 = Auto/Run still closed, DI 5 = compressor
                               overload OK. VFD status register confirms 0 Hz.

 9    Nova → RPi5 → Browser    Status update flows back: UART1 → bridge → WS.
                               UI shows Compressor 1: "OFF (MANUAL OVERRIDE)"
                               in grey with override banner.
```

### B.4 Data Flow — Safety Trip

**Example: High head pressure switch opens on Compressor 2**

```
Step  Location                 What Happens
────  ────────────────────     ──────────────────────────────────────────────────
 1    Orbit 3 (Rm A Ref 2)    DI 7 (High Head Pressure) transitions from closed
                               to open. Module updates its DI status register
                               immediately.

 2    Nova (R5F-0)             SafetyMonitor task reads DI update on next 100 ms
                               poll. Detects: Orbit 3, DI 7 = OPEN → high head
                               pressure trip. Bypasses ControlLoop — sends
                               immediate emergency commands.

 3    Nova → Orbit 3           CommandWriter sends Modbus TCP: all watchdog-
      (Ethernet)               enabled DOs → OFF (compressor, LLS, unloaders,
                               condenser fans all de-energize). VFD stop command
                               sent via unit ID 101 on Port B.

 4    Nova (alarm engine)      Alarm logged with timestamp, sensor readings at
                               time of trip, and affected equipment. Alarm state
                               set: COMP2_HIGH_HEAD = ACTIVE.

 5    Nova → RPi5 → Browser    Alarm packet sent over UART1. Bridge pushes alarm
                               via WebSocket. UI shows: red alarm banner,
                               "COMPRESSOR 2: HIGH HEAD PRESSURE", audible alert.

 6    Nova → RPi5 → Cloud      RPi5 sends alarm to cloud logging service via site
                               network Ethernet. Maintenance team notified.

 7    Orbit 3 watchdog         Even if the TCP alarm command from step 3 is lost,
                               the module's built-in watchdog timer expires
                               (default 30 s for compressor). Firmware
                               de-energizes all watchdog-enabled outputs.
                               The safety trip succeeds regardless of network
                               state.
```

### B.5 Data Flow — Watchdog Failsafe (Nova Dies)

**Example: Nova controller loses power or firmware crashes**

```
Step  Location                 What Happens
────  ────────────────────     ──────────────────────────────────────────────────
 1    All Orbit modules        Modbus TCP polls from Nova stop arriving. Each
                               module's watchdog timer begins counting down
                               independently (configurable, default 30 s for
                               compressor outputs, 60 s for non-critical).

 2    Each Orbit module        Watchdog expires. Firmware turns off all
      (independent)            watchdog-enabled outputs: compressors stop,
                               LLS valves close, heaters off, humidifiers off.
                               Non-watchdog outputs (status lights, failure
                               relay) remain energized until explicitly turned off.

 3    VFDs/EEVs                VFDs configured with comm timeout = disabled, so
                               they hold last speed. However, the compressor
                               contactor (DO 1) has already opened via watchdog
                               — VFD speed is irrelevant because the motor has
                               no power. EEV controller holds last position
                               (valve stays at last opening — safe).

 4    Analog boards            No more queries arrive on Port A. Analog boards
                               are passive — they only respond when asked. They
                               continue measuring but nobody reads them. No
                               harm — sensors don't have outputs to safe-state.

 5    RPi5                     UART1 serial goes silent. Bridge server detects
                               loss of heartbeat. UI shows: "CONTROLLER OFFLINE"
                               banner. All values freeze at last known state.
                               Cloud alert sent: "Site XYZ controller offline."

 6    E-stop (always works)    E-stop button remains functional at all times —
                               it is hardwired to contactor coil circuits,
                               completely independent of Nova, Orbit modules,
                               Modbus, Ethernet, or any software.
```

### B.6 Channel Summary — One Orbit Module

Every Orbit module in the Constellation has exactly **one RJ45 connector** that carries **three independent data channels** plus power:

```
                          Single Cat5 Cable (data only)
                          ═════════════════════════════
                                    │
                          24VDC in (screw terminal)
                                    │
                                    ▼
                          ┌─────────────────────┐
                          │     ORBIT MODULE     │
                          │                      │
   24VDC input from       │  24V→5V→3.3V buck/LDO│
   DIN-rail supply        │                      │
                          │                      │
   Modbus TCP ◄──────────►│  W5500 (SPI)         │──► 10× DO relays
   (port 502)             │  DI/DO/AO registers  │◄── 10× DI optocouplers
   Unit ID 1 = own I/O    │                      │──► 2× AO 4-20mA
                          │                      │
   Modbus RTU master ◄───────►│  Port A (USART1)     │──► RS485-A terminals
   (analog boards,        │  MAX3485 #1          │    Analog boards (9600)
    9600 baud)            │                      │
                          │                      │
   Modbus RTU ◄──────────►│  Port B (USART2)     │──► RS485-B terminals
   (unit IDs 100-131)     │  MAX3485 #2          │    VFDs/EEVs (19200)
                          │                      │
                          │  STM32F091 (SPI+2×   │
                          │  USART, all parallel) │
                          │                      │
                          │  DIP switch → IP addr │
                          │  Watchdog timer       │
                          │  LED indicators       │
                          └─────────────────────┘
```

All three channels operate **simultaneously and independently**. Nova can read DI states, poll analog board data (already cached in Orbit registers via Port A Modbus RTU), and query a VFD — all targeting the same Orbit module at the same time, with zero contention between channels.

### B.7 Complete System — Data Flow Map

```
Azure ◄── outbound TLS ── RPi5 (10.x) ◄── UART1 (230400) ──► Nova (192.168.0.1)
 Cloud                Web UI                              │
 Remote               Bridge Server                       │ Ethernet (switch)
                                                          │ 192.168.0.0/24
                                                          │
            ┌─────────────────────────────────────────────┤
            │                    │                         │
       ┌────┴────┐         ┌────┴────┐              ┌────┴────┐
       │ Orbit 1 │         │ Orbit 2 │              │ Orbit 3 │    ... Orbit 4-6
       │ Rm A    │         │ Rm A    │              │ Rm A    │
       │ Storage │         │ Ref 1   │              │ Ref 2   │
       └┬───┬──┬┘         └┬───┬──┬┘              └┬───┬──┬┘
        │   │  │            │   │  │                │   │  │
  TCP:  │   │  │      TCP:  │   │  │          TCP:  │   │  │
  DI/DO/│   │  │      DI/DO/│   │  │          DI/DO/│   │  │
  AO    │   │  │      AO    │   │  │          AO    │   │  │
        │   │  │            │   │  │                │   │  │
  PortA:│   │  PortB: PortA:│   │  PortB:    PortA:│   │  PortB:
  Analog│   │  (free) (free)│   │  VFD+EEV   (free)│   │  VFD+EEV
  Boards│   │               │   │  │                │   │  │
  9600  │   │               │   │  ▼ 19200          │   │  ▼ 19200
        │   │               │   │  ┌────────┐       │   │  ┌────────┐
        │   ▼               │   │  │Comp VFD│       │   │  │Comp VFD│
        │  Lights           │   │  │Cond VFD│       │   │  │Cond VFD│
        │  Climacell        │   │  │EEV     │       │   │  │EEV     │
        │  Humidifiers      │   │  └────────┘       │   │  └────────┘
        │  Heat             │   │                    │   │
        │  Cavity Heat      │   ▼                    │   ▼
        ▼                   │  Compressor            │  Compressor
  ┌──────────┐              │  LLS, Unloaders        │  LLS, Unloaders
  │Anlg Bd 1 │              │  Condenser Fans        │  Condenser Fans
  │Anlg Bd 2 │              │  Failure Relay         │  Failure Relay
  │Anlg Bd 3 │              │                        │
  └──────────┘              ▼                        ▼
  Temp/Humidity/     AO 1 → Comp/Cond VFD     AO 1 → Comp/Cond VFD
  CO2/Pressure       AO 2 → EEV/Cond VFD      AO 2 → EEV/Cond VFD
  (Modbus RTU)

  Vent Fan VFD ◄── AO 1 (4-20mA, storage Orbit)
```

**Every piece of data in the Constellation follows one of these paths:**

| Data | Path | Protocol |
|---|---|---|
| Sensor readings (temp, humidity, CO2, pressure) | Analog board → Port A → Orbit (Modbus RTU master) → Orbit input registers → Ethernet → Nova | Standard Modbus RTU + Modbus TCP FC 04 (Modbus end-to-end) |
| Equipment status (DI: overloads, switches, proofs) | Field wiring → Orbit DI optocoupler → Ethernet → Nova | Modbus TCP (FC 02) |
| Equipment commands (DO: contactors, solenoids, relays) | Nova → Ethernet → Orbit DO relay → Field wiring | Modbus TCP (FC 05/0F) |
| Modulating control (AO: VFD speed, EEV position) | Nova → Ethernet → Orbit AO 4-20mA → VFD/EEV | Modbus TCP (FC 06/10) |
| VFD speed/status (when RTU) | Nova → Ethernet → Orbit Port B → RS485 → VFD | Modbus RTU tunneled over TCP |
| EEV superheat/position (when RTU) | Nova → Ethernet → Orbit Port B → RS485 → EEV | Modbus RTU tunneled over TCP |
| UI display / user commands | Nova ↔ UART1 ↔ RPi5 ↔ Browser | Binary serial + HTTP/WebSocket |
| Cloud logging / remote access | RPi5 → outbound TLS tunnel → Azure WebSites (no inbound ports) | HTTPS (outbound-only) |
| Emergency stop | E-stop button → hardwired to contactor coils | Electrical (no software) |

---

## Appendix C — Constellation vs. Current AS2 System Cost

### C.1 Current AS2 Panel Cost (Baseline)

**Current cost per twin cold storage system: ~$12,000**

This includes the TM4C129 controller board, CPLD I/O shift registers, analog boards, relay boards, RS485 wiring, 24V power supplies, power wiring to each I/O board, control transformer, panel enclosure, DIN rail, wire duct, terminal blocks, field wiring terminations, and assembly labor. The RPi5, display, and network equipment are additional.

### C.2 Constellation System Cost (Single Zone — 1 Room, 2 Compressors, Doors)

| Item | Unit Cost | Qty | Total |
|---|---|---|---|
| **Nova controller board** (AM2434 + DDR4 + eMMC + Ethernet PHY + DS3231 RTC + coin cell + power regulation + PCB + enclosure) | ~$58 | 1 | $58 |
| **Orbit module** (STM32F091 + W5500 + 24V buck/LDO + 10× ULN2803A 24VDC sink outputs + 20-pos pluggable terminal + 10× opto DI + 2× AO + dual RS485 + Modbus RTU master + Modbus TCP bootloader + DIP switch + DIN rail enclosure + ENIG + conformal coat) | ~$25 | 4 | $100 |
| **Ethernet switch** (unmanaged gigabit, 8-port, e.g. TP-Link TL-SG108) | $25 | 1 | $25 |
| **Cat5 cable** (module runs, 10-50ft each, pre-terminated) | ~$8 | 5 | $40 |
| **RS485 stub cables** (Port A/B, 3-10ft, pre-made) | ~$3 | 6 | $18 |
| **RPi5** (4GB + SD card + case + heatsink) | ~$82 | 1 | $82 |
| **Spares kit** (2× MAX3485, 1× ULN2803A) | $0.90 | 1 | $1 |
| **Panel enclosure, DIN rail, wire duct** | ~$80 | 1 | $80 |
| **Control transformer + fusing** | ~$40 | 1 | $40 |
| **Miscellaneous** (labels, ferrules, zip ties) | ~$15 | 1 | $15 |
| **UPS** (300VA, Nova + RPi5 + switch, outage detection) | ~$50 | 1 | $50 |
| **24VDC DIN-rail power supply** (Mean Well HDR-30-24, UL listed) | ~$15 | 1 | $15 |
| | | | |
| **Constellation total (4 Orbits — 1 storage + 1 door + 2 refrig)** | | | **~$524** |

### C.3 Constellation System Cost (7-Orbit Configuration — Full Twin Room with Door Control)

| Item | Delta from 4-Orbit | Total |
|---|---|---|
| 3× additional Orbit modules (1 storage + 2 refrig for Room B) | +$75 | $599 |
| 3× additional Cat5 runs | +$24 | $623 |
| 2× additional RS485 stubs | +$6 | $629 |
| **Constellation total (7 Orbits — 2 storage + 1 door + 4 refrig)** | | **~$629** |

The door control Orbit is shared between both rooms.

### C.4 Savings Summary

| Configuration | Constellation Cost | Current AS2 Cost | Savings | % Reduction |
|---|---|---|---|---|
| **4 Orbits** (1 room, 2 compressors, doors) | ~$524 | ~$12,000 | **~$11,476** | **96%** |
| **7 Orbits** (2 rooms, 4 compressors, shared doors) | ~$629 | ~$12,000 | **~$11,371** | **95%** |

### C.5 Where the Savings Come From

| Cost eliminated or reduced | Current AS2 | Constellation | Savings |
|---|---|---|---|
| RS485 homerun cable (3+ buses, long pulls) | ~$360 | $0 (Ethernet star, Cat5) | $360 |
| 24V power wiring to every I/O board | ~$200 | ~$30 (short 24V runs, local supply) | $170 |
| 24V power supplies for I/O boards | ~$120 | ~$15 (shared DIN-rail supply) | $105 |
| Custom relay boards + assembly | ~$800+ | $100 (4× Orbit, pre-built) | $700 |
| Custom CPLD I/O boards + assembly | ~$400+ | $0 (replaced by Orbit modules) | $400 |
| TM4C129 controller + CPLD + RS485 hub | ~$300+ | $56 (Nova) | $244 |
| Panel wiring labor (point-to-point RS485, 24V distribution) | ~$3,000+ | ~$200 (Cat5 + short relay jumpers) | $2,800 |
| Panel assembly labor (custom boards, hand-soldered) | ~$4,000+ | ~$100 (DIN rail clip, plug in Cat5) | $3,900 |
| Commissioning/debugging (RS485 bus issues, address conflicts) | ~$1,000+ | ~$50 (set DIP switch, plug in, auto-discovers) | $950 |
| **Total** | **~$12,000** | **~$524** | **~$11,476** |

### C.6 What This Doesn't Include

The above comparison is hardware + panel assembly only. Both systems share these additional costs equally:

- Field wiring from panel to equipment (compressors, fans, solenoids) — same in both systems
- VFDs (ABB ACS380) — same in both systems
- EEV controllers — same in both systems
- Refrigeration piping, insulation, doors — not controls-related
- Engineering time for control logic development — one-time, amortized across all installations

### C.7 Per-Unit Margin Impact

At scale (10+ installations/year):

| Metric | Current AS2 | Constellation |
|---|---|---|
| Panel hardware cost | ~$12,000 | ~$580 |
| Panel assembly labor (hours) | 40-60 hrs | 4-6 hrs |
| Commissioning (hours) | 8-16 hrs | 1-2 hrs |
| Field service call (typical) | Replace board, re-wire, re-commission (4-8 hrs) | Swap Orbit module or pull IC from socket (15 min) |
| Inventory for field service | Multiple custom boards, specific revisions | Generic Orbit modules + $4.40 spares kit |
