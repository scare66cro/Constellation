# Constellation AM243x Platform Architecture

**Date:** April 3, 2026
**Status:** Design Phase
**Parent:** Constellation-Product-Family.md

---

## 1. Overview — One Chip Family for the Entire Platform

Constellation uses the **TI Sitara AM243x** family for every MCU in the system — Nova, Orbit, Pulsar, and Triton. All cards share a single chip vendor (Texas Instruments), a single toolchain (Code Composer Studio + MCU+ SDK), a single debugger (XDS110 JTAG), and a single set of peripheral APIs. The RPi5 is the only non-TI device in the architecture, and it serves as the HMI/display layer only — it is not in the control path.

### Why AM243x?

The AM243x series are **real-time Cortex-R5F microcontrollers**, not Linux application processors. They boot in milliseconds, run bare-metal or FreeRTOS, have on-chip SRAM and flash (no external DDR or SD card), and include **built-in industrial Ethernet** with an integrated 2-port switch via the PRU-ICSS subsystem. This eliminates the W5500 and KSZ8863 from every board in the system.

| Feature | AM243x (Sitara MCU) | Previous design (STM32G474 + W5500 + KSZ8863) |
|---|---|---|
| Ethernet | Built-in Gigabit MAC, 2 ports with hardware switch | External W5500 (SPI, 10/100 only) + KSZ8863 switch IC |
| Boot time | <50 ms (bare-metal) | <50 ms |
| OS | Bare-metal / FreeRTOS (no Linux) | Bare-metal / FreeRTOS |
| External memory needed | None — on-chip SRAM + flash | None |
| Industrial protocols | PRU-ICSS: EtherNet/IP, PROFINET, EtherCAT in hardware | Software-only Modbus TCP |
| Chips per board (networking) | 1 MCU + 1 Ethernet PHY | 1 MCU + 1 W5500 + 1 KSZ8863 |
| Supply chain | Single vendor (TI) | Two vendors (ST + WIZnet + Microchip) |
| Toolchain | One (CCS + MCU+ SDK) for all cards | One (CubeIDE) for all cards |

---

## 2. Chip Selection — Nova vs Orbit/Pulsar/Triton

| | **AM2434** (Nova) | **AM2432** (Orbit / Pulsar / Triton) |
|---|---|---|
| Cores | 2× Cortex-R5F @ 800 MHz | 2× Cortex-R5F @ 800 MHz |
| On-chip SRAM | 2 MB ECC | 1.5 MB ECC |
| On-chip flash | 2 MB | 1.5 MB |
| Ethernet ports | 2× Gigabit (PRU-ICSS) | 2× Gigabit (PRU-ICSS) |
| Internal switch | Yes — 2-port cut-through | Yes — 2-port cut-through |
| UART | 9 | 6 |
| CAN-FD | 2 | 2 |
| SPI | 5 | 3 |
| ADC | 2× 12-bit, 4.6 Msps | 2× 12-bit, 4.6 Msps |
| GPIO | 144 | 100 |
| PRU-ICSS | 2× PRU @ 200 MHz | 2× PRU @ 200 MHz |
| Package | BGA 196-pin, 11×11mm | BGA 196-pin, 11×11mm |
| Approx cost | ~$14 | ~$10 |
| Role | Central controller — PID, state machines, VFD profiles, card polling | Remote I/O — Modbus server, relay/stepper/sensor drivers |

### Why AM2434 for Nova (not AM2432)?

- 2 MB flash vs 1.5 MB — room for VFD profiles, site config, dual-image A/B firmware updates
- 2 MB SRAM vs 1.5 MB — headroom for 40+ zone PID controllers, 31-card register cache, VFD state tracking
- 9 UARTs — RPi5 bridge + RS-485 Port A (VFDs) + RS-485 Port B (legacy sensors) + spare

### Why AM2432 for I/O Cards (not AM2431)?

- **Two Ethernet ports** enable daisy-chaining — Port A upstream, Port B downstream, hardware switch forwards traffic
- AM2431 has only 1 Ethernet port — requires the old KSZ8863 external switch IC to daisy-chain, defeating the purpose
- $2 cost difference eliminates a $3 KSZ8863 + PCB routing — net savings

---

## 3. Architecture — Control Independence

The Nova MCU is the **real-time control brain**. It runs every PID loop, every state machine, every VFD command, and every card poll. The RPi5 is the display/logging/remote-access layer. If the RPi5 crashes, the Nova keeps ventilating, refrigerating, and controlling doors.

```
┌──────────────────────────────────────────────────────┐
│                    RPi5 (HMI only)                    │
│  Touchscreen, web UI, historical logging, cloud sync  │
│  Remote access, configuration editor, alert routing   │
│                                                       │
│  Pi dies → fans still run, doors still work,          │
│  VFDs still go, compressors still stage               │
└──────────────────┬───────────────────────────────────┘
                   │ UART (serial, 230400 baud)
                   │ Status/config only — NOT in control path
┌──────────────────┴───────────────────────────────────┐
│             Nova MCU — AM2434                         │
│  ALL control logic lives here                         │
│                                                       │
│  Core R5F-0: Control engine                          │
│    PID loops, state machines, zone logic,             │
│    equipment scheduling, alarm evaluation             │
│                                                       │
│  Core R5F-1: Communications                          │
│    Modbus TCP client → poll all cards (Ethernet)      │
│    Modbus RTU client → command VFDs (RS-485)          │
│    RPi5 UART bridge → serve status, accept config     │
│    VFD profile engine → translate generic commands     │
│                                                       │
│  Settings: QSPI flash, dual-sector ping-pong, CRC32  │
│  VFD profiles: stored in on-chip flash (1 KB each)    │
│  Site config: stored in on-chip flash                 │
└──────────────────┬───────────────────────────────────┘
                   │ Ethernet (Modbus TCP, 192.168.1.x)
     ┌─────────────┼─────────────┬──────────────┐
     │             │             │              │
┌────┴────┐  ┌────┴────┐  ┌────┴────┐  ┌─────┴─────┐
│ Orbit   │  │ Orbit   │  │ Pulsar  │  │  Triton   │
│ AM2432  │  │ AM2432  │  │ AM2432  │  │  AM2432   │
│ Storage │  │ Aux/VFD │  │ Doors   │  │  Refrig   │
│   I/O   │  │   I/O   │  │ stepper │  │ compress  │
└─────────┘  └─────────┘  └─────────┘  └───────────┘
     │            │             │              │
  Sensors,     Lights,      Stepper        Compressors,
  fans,        humidifiers, motors,        defrost,
  heaters      aux relays   doors/vents    condensers
```

### What Runs Where

| Function | Where | Why |
|---|---|---|
| PID control | Nova R5F-0 | Centralized — cross-zone coordination, one source of truth |
| State machines | Nova R5F-0 | All mode transitions (cooling, heating, cure, defrost) |
| Card bus polling | Nova R5F-1 | Modbus TCP client, polls 31 cards at 1 Hz |
| VFD speed commands | Nova R5F-1 | Modbus RTU client via RS-485, translates through VFD profiles |
| RPi5 bridge | Nova R5F-1 | UART, existing `^tag=value$CRC!` protocol |
| VFD profile engine | Nova R5F-1 | Loads profile from flash, maps generic run/stop/speed to drive registers |
| Safety watchdog | Nova R5F-2 | Independent core, monitors R5F-0 and R5F-1 heartbeats |
| Relay switching | Orbit MCU | Executes coil commands from Nova, local safety rules at 200 ms |
| Sensor reading | Orbit MCU | ADC/RS-485 polling, updates input registers for Nova to read |
| Stepper positioning | Pulsar MCU | TMC2226 UART commands, StallGuard monitoring, position tracking |
| Compressor staging | Triton MCU | Drives contactors per Nova's staging commands, local pressure safety |
| Web UI / display | RPi5 | Svelte SPA, lighttpd, touchscreen — display only |
| Historical logging | RPi5 | SQLite, 10-year trend storage on 128 GB NVMe — display only |
| Cloud sync / alerts | RPi5 | Azure IoT Hub, email alerts — display only |

---

## 4. Built-In Ethernet — No W5500, No KSZ8863

The single biggest hardware simplification of the AM243x migration. Every I/O card drops **two external ICs** (W5500 + KSZ8863) and replaces them with **one Ethernet PHY** (DP83826, ~$1).

### Old Design (STM32G474)

```
STM32G474 ── SPI ── W5500 ── single RJ45
                      │
              KSZ8863 switch IC ── second RJ45 (daisy-chain)

3 ICs for networking: MCU + W5500 ($3) + KSZ8863 ($3) = $6 in networking silicon
2 SPI buses consumed (W5500 + KSZ8863)
100 Mbps max
TCP/IP stack limited by W5500 hardware (8 sockets max)
```

### New Design (AM2432)

```
AM2432 ── RGMII ── DP83826 PHY ── RJ45 Port A (upstream)
  │
  └──── RGMII ── DP83826 PHY ── RJ45 Port B (downstream / daisy-chain)

1 IC for networking: 2× DP83826 PHY ($1 each) = $2 in networking silicon
0 SPI buses consumed
Gigabit capable (100 Mbps default — more than enough)
PRU-ICSS handles switching in hardware — zero CPU load
Full TCP/IP stack in software with unlimited sockets
```

### Savings Per Board

| Component | Old | New | Delta |
|---|---|---|---|
| W5500 | $3.00 | — | –$3.00 |
| KSZ8863 | $3.00 | — | –$3.00 |
| 2× DP83826 | — | $2.00 | +$2.00 |
| MCU (STM32G474 → AM2432) | $5.00 | $10.00 | +$5.00 |
| **Net component delta** | | | **+$1.00** |
| **PCB simplification** | 2 fewer SPI buses, fewer passives, smaller layout | | –$1–2 |
| **Net per board** | | | **≈ $0 to –$1** |

Cost-neutral — but you gain built-in gigabit, hardware switching, daisy-chain without external ICs, single-vendor supply chain, and industrial protocol capability (EtherNet/IP, PROFINET) if you ever need it.

---

## 5. Daisy-Chain Ethernet — No External Switch Needed

Every Orbit, Pulsar, and Triton has **two RJ45 ports** connected through the AM2432's PRU-ICSS internal switch. Traffic flows through in hardware at wire speed — the R5F application core doesn't touch pass-through frames.

### Refrigeration Compressor Lineup

```
Nova ──── 50 ft ──── Orbit-R 1 ──── 6 ft ──── Orbit-R 2 ──── 6 ft ──── Orbit-R 3 ──── 6 ft ──── Orbit-R 4
                     (comp 1)                  (comp 2)                  (comp 3)                  (comp 4)
                     Port A  B                 Port A  B                 Port A  B                 Port A
                     ↑in   ↑out               ↑in   ↑out               ↑in   ↑out               ↑in
```

**One Ethernet cable** from the main panel to the first compressor card. Short 6-inch jumpers between adjacent cards. No external switch in the compressor room.

### Storage Building Fan-Out

```
Nova ── Main Switch ─┬── 200 ft ── Orbit #1 (Building A) ── 6 ft ── Orbit #2 (Building A)
                     │
                     ├── 150 ft ── Orbit #3 (Building B) ── 6 ft ── Pulsar #4 (Building B)
                     │
                     └── 30 ft ── Orbit-R #5 ── Orbit-R #6 ── Orbit-R #7 ── Orbit-R #8
                                  (compressor room — 4 circuits daisy-chained)
```

### Fault Isolation

If an Orbit in the middle of a daisy chain loses power, does the chain break?

**Only if the board loses power entirely.** The PRU-ICSS switch operates in hardware independent of the R5F application core. If the firmware crashes, the switch keeps forwarding frames. The chain only breaks on a complete power loss — and in a compressor room where all cards share the same electrical panel, if one loses power, they all do.

For critical paths where independent power feeds exist, use a star topology with a $20 unmanaged switch instead of daisy-chaining.

---

## 6. Card Addressing — Dual-Mode DIP Switch + DHCP

Every card supports two addressing modes, selected by a 5-position DIP switch on the board.

| DIP Value | Mode | IP Address | Modbus Address |
|---|---|---|---|
| **0** | DHCP | Assigned by Nova's dnsmasq | Assigned by Nova config |
| **1–31** | Static | `192.168.1.{DIP}` | `{DIP}` |

### Static Mode (DIP 1–31) — Default for Most Installs

Installer sets the DIP switch. Card boots with a fixed IP and Modbus address. Nova polls the address directly. No discovery protocol needed.

```
DIP = 3  →  IP = 192.168.1.3  →  Modbus slave 3  →  Nova polls this address
```

**Card replacement:** Swap the dead card, set the DIP to the same number, plug in, done. Nova resumes polling. No UI interaction required.

### DHCP Mode (DIP 0) — Zero-Touch for Large Sites

Card boots, sends DHCP request. Nova's dnsmasq assigns an IP. Card announces itself via UDP broadcast with its STM32 unique ID (96-bit, factory-burned, unforgeable).

```
Card boots → DHCP request → gets 192.168.1.47
Card broadcasts: { uid: "0x0032001F3530", type: "orbit", channels: 14, ip: "192.168.1.47" }
Nova receives → looks up UID in config → assigns Modbus address → starts polling
```

**Card replacement:** New card has a different UID. Nova detects "Card offline" + "New card detected." Installer opens UI → Settings → Card Bus → clicks "Replace" next to the offline card → selects the new card. Two clicks.

**Auto-replace:** If only one card is offline and only one new card of the same type is unassigned, Nova can auto-match them. Toast notification: "Card 3 replaced — verify operation."

### Firmware Boot Logic

```c
dip = read_dip_switches();   // 0–31

if (dip == 0) {
    // DHCP mode
    ip = dhcp_request();
    modbus_addr = 0;          // assigned by Nova after announcement
    start_udp_announce(uid, type, channels, ip);
} else {
    // Static mode
    ip = make_ip(192, 168, 1, dip);
    modbus_addr = dip;
    // Ready — Nova polls this address directly
}
```

### Why 31 Max?

A 5-position DIP switch gives 0–31 (32 values). No realistic site needs 31 I/O cards on one Nova. For the rare site that does, extra cards use DIP 0 (DHCP mode). Both modes coexist on the same bus.

---

## 7. VFD Profile Engine — Drive-Agnostic Speed Control

Nova talks to VFDs directly on RS-485 via Modbus RTU. Every VFD brand has a different register map. Nova abstracts this with a **profile-based translation layer** stored in flash.

### The Problem

| Function | ABB ACS580 | Danfoss VLT FC51 | Yaskawa GA500 | WEG CFW500 |
|---|---|---|---|---|
| Control word | 40001 | 49901 | 40001 | 40683 |
| Speed reference | 40002 | 49902 | 40002 | 40684 |
| Status word | 40003 | 49903 | 40003 | 40685 |
| Output frequency | 40004 | 49904 | 40102 | 40686 |
| Run forward bit | bit 0 | bit 6 | bit 0 | bit 0 |
| Speed scaling | 0–10000 | 0–32767 | 0–10000 | 0–10000 |

Not just different register addresses — different bit positions, different value scaling, different control word logic.

### The Solution — JSON Profiles in Flash

Each VFD model has a profile (~1 KB) stored in Nova's flash. Nova's VFD engine loads the profile and translates generic commands into drive-specific register writes.

```json
{
  "name": "ABB ACS580",
  "default_baud": 19200,
  "registers": {
    "control_word":   { "addr": 1,  "type": "holding" },
    "speed_ref":      { "addr": 2,  "type": "holding", "scale": 10000 },
    "status_word":    { "addr": 3,  "type": "holding" },
    "output_freq":    { "addr": 4,  "type": "holding", "scale": 100 },
    "output_current": { "addr": 5,  "type": "holding", "scale": 100 }
  },
  "commands": {
    "run_forward": { "register": "control_word", "value": "0x0001" },
    "stop":        { "register": "control_word", "value": "0x0004" },
    "fault_reset": { "register": "control_word", "value": "0x0080" }
  },
  "status_bits": {
    "running":  { "register": "status_word", "bit": 0 },
    "faulted":  { "register": "status_word", "bit": 3 },
    "at_speed": { "register": "status_word", "bit": 2 },
    "ready":    { "register": "status_word", "bit": 1 }
  }
}
```

### Translation Example

Nova's PID outputs "run fan at 75%":

```
With ABB profile:
  → write register 2 = 7500  (75% × 10000 scale)
  → write register 1 = 0x0001 (run forward)

With Danfoss profile:
  → write register 49902 = 24576  (75% × 32767 scale)
  → write register 49901 = 0x0040 (run forward = bit 6)
```

Same Nova code, different profile — just swapped JSON.

### Flash Budget for VFD Profiles

| Item | Size |
|---|---|
| Profile engine code | ~5 KB |
| One VFD profile | ~1 KB |
| 10 supported VFD models | ~10 KB |
| **Total** | **~15 KB** (of 2 MB flash on AM2434) |

### Supported Models (Phase 1)

| Brand | Model | Market Coverage |
|---|---|---|
| ABB | ACS580 | Very common in ag/HVAC |
| Danfoss | VLT FC51 | Very common in ag/HVAC |
| Yaskawa | GA500 | Common |
| WEG | CFW500 | Growing |
| Agristar FMBT21 | Single model | Existing installed base |

### Adding New VFD Models

1. **From the UI:** Nova UI has a profile creator form — installer fills in register addresses and bit positions from the VFD manual, saves, done
2. **From support:** Agristar technician creates a JSON profile, uploads to Nova via RPi5
3. **Over-the-air:** New profiles ship with Nova firmware updates — like a printer driver database

New profiles require zero firmware changes. They're data, not code.

---

## 8. Modbus Register Map — Per-Card-Type

Each card type has a **fixed register map baked into firmware**. The DIP switch / DHCP address determines which Modbus slave responds. All cards of the same type have identical register maps — Nova's site config assigns meaning (which register maps to which real-world equipment).

### Register Types (Modbus Standard)

| Range | Type | R/W | Used For |
|---|---|---|---|
| 0xxxx (coils) | Discrete output | R/W | Relay ON/OFF, motor enable |
| 1xxxx (discrete inputs) | Discrete input | Read | Door switches, pressure switches |
| 3xxxx (input registers) | 16-bit input | Read | Temperatures, positions, load values, status |
| 4xxxx (holding registers) | 16-bit config/control | R/W | Speed targets, position targets, calibration, modes |

### Orbit Register Map (14-Channel I/O)

```
COILS (0xxxx) — Relay Outputs
  00001–00014   Relay channels 1–14 (ON/OFF)

DISCRETE INPUTS (1xxxx) — Digital Inputs
  10001–10014   DI channels 1–14 (0/1)

INPUT REGISTERS (3xxxx) — Status (read-only)
  30001–30004   Analog inputs 1–4 (raw ADC, 0–4095)
  30005         Board temperature (°C × 10)
  30006         Supply voltage (mV)
  30007         Firmware version (major.minor as 0x0102 = v1.2)
  30008         Card type ID (0x01 = Orbit)
  30009         Channel count
  30010         Uptime (seconds)
  30011         Error/fault flags (bitmask)
  30012–30025   DRV8908 diagnostic: per-channel fault status

HOLDING REGISTERS (4xxxx) — Configuration (read/write)
  40001–40014   Output mode per channel (0=manual, 1=PID, 2=schedule, 3=follow-DI)
  40015–40018   Analog calibration offsets
  40019         Watchdog timeout (seconds, 0=disabled)
  40020         Failsafe relay state (bitmask: which relays ON if comms lost)
  40021–40034   Safety rule slots (locally enforced at 200 ms)
```

### Pulsar Register Map (8-Channel Stepper)

```
COILS (0xxxx) — Motor Enable
  00001–00008   Motor 1–8 enable (ON/OFF)

INPUT REGISTERS (3xxxx) — Status (read-only)
  30001–30008   Motor 1–8 position (0–10000 = 0.0%–100.0%)
  30009–30016   Motor 1–8 load (StallGuard value, 0–1023)
  30017–30024   Motor 1–8 status (0=idle, 1=moving, 2=stalled, 3=error)
  30025         Board temperature (°C × 10)
  30026         Supply voltage (mV)
  30027         Firmware version
  30028         Card type ID (0x02 = Pulsar)
  30029         Channel count (4 or 8)

HOLDING REGISTERS (4xxxx) — Control (read/write)
  40001–40008   Motor 1–8 target position (0–10000 = 0.0%–100.0%)
  40009–40016   Motor 1–8 speed (steps/sec)
  40017–40024   Motor 1–8 current limit (mA)
  40025         Stall detection threshold (global)
  40026         Stall retry count
  40027         Watchdog timeout
  40028         Failsafe position (0=hold, 1=close, 2=open)
```

### Why the Register Map Doesn't Change Per Site

Every Orbit runs identical firmware. Register 00001 is always "relay channel 1" on every Orbit. The difference between "relay 1 = fan" and "relay 1 = heater" exists only in **Nova's site config** — the Orbit doesn't know or care what's wired to its outputs.

```
Nova's config:
  Card address 1, coil 1  →  "Barn 1 Fan 1"     (PID zone: Barn 1)
  Card address 1, coil 2  →  "Barn 1 Fan 2"     (PID zone: Barn 1)
  Card address 2, coil 1  →  "Barn 2 Heater 1"  (PID zone: Barn 2)

On the wire:
  Nova → 192.168.1.1: write coil 1 = ON   →  Fan 1 turns on
  Nova → 192.168.1.2: write coil 1 = ON   →  Heater 1 turns on
```

Same register, different card address, completely different equipment. The **address is the namespace**.

---

## 9. Nova Flash Layout

The AM2434 has 2 MB on-chip flash. Reserved as follows:

```
0x00000000 – 0x001BFFFF  (1,792 KB)  Firmware image A (active)
0x001C0000 – 0x001DFFFF  (128 KB)    Firmware image B (OTA staging)
0x001E0000 – 0x001EFFFF  (64 KB)     Site config (cards, zones, equipment, PID tuning)
0x001F0000 – 0x001F3FFF  (16 KB)     VFD profiles (up to 10 models × ~1 KB)
0x001F4000 – 0x001F7FFF  (16 KB)     Safety rules (pushed to cards on boot)
0x001F8000 – 0x001FBFFF  (16 KB)     Runtime counters (fan hours, compressor hours)
0x001FC000 – 0x001FFFFF  (16 KB)     Boot config (active image flag, boot count, watchdog resets)
```

Additionally, an external **QSPI NOR flash (8–64 MB)** stores the settings vault with dual-sector ping-pong CRC32 protection and triple backup (QSPI + RPi5 filesystem + Azure cloud).

---

## 10. Nova Resource Budget

### Flash Usage (2 MB available)

| Component | Size | Notes |
|---|---|---|
| TI-RTOS / FreeRTOS | ~40 KB | Scheduler, queues, timers, multi-core IPC |
| Ethernet driver (LWIP + PRU-ICSS) | ~60 KB | TCP/IP stack + Modbus TCP client |
| Modbus RTU client | ~5 KB | UART-based, VFD polling |
| VFD profile engine | ~5 KB | Load profile, translate commands |
| PID controller (40 zones) | ~8 KB | ISA PID, anti-windup, bumpless transfer |
| State machine engine | ~30 KB | All operating modes, transitions, schedules |
| Safety / alarm manager | ~15 KB | 20+ failure types, latching, escalation |
| QSPI flash driver | ~5 KB | Settings vault, ping-pong, CRC32 |
| RPi5 UART bridge | ~10 KB | Serial protocol encoder/decoder |
| Hardware drivers (GPIO/ADC/UART/SPI) | ~20 KB | TI HAL/LL drivers |
| Application logic | ~40 KB | Equipment scheduler, cross-zone coordination |
| **Total firmware** | **~238 KB** | **12% of 2 MB** |
| VFD profiles (10 models) | ~10 KB | Data, not code |
| Site config | ~10 KB | Data |
| **Total flash used** | **~258 KB** | **13% of 2 MB** |

### SRAM Usage (2 MB available)

| Component | Size | Notes |
|---|---|---|
| Task stacks (8 tasks) | ~16 KB | 2 KB each |
| Card register cache (31 cards) | ~6 KB | 100 regs × 2 bytes × 31 |
| VFD state (20 drives) | ~2 KB | Speed, status, fault, current per drive |
| PID state (40 zones) | ~4 KB | Setpoint, integral, output, mode per zone |
| Zone state machines (40 zones) | ~8 KB | Current state, timers, counters per zone |
| Modbus TX/RX buffers | ~4 KB | TCP + RTU |
| Site config (working copy) | ~10 KB | Loaded from flash on boot |
| Alarm state | ~2 KB | Active alarms, latch state, escalation timers |
| RTOS kernel objects | ~4 KB | Queues, semaphores, timers, IPC mailboxes |
| **Total** | **~56 KB** | **3% of 2 MB** |

**Massive headroom.** Both flash and SRAM are under 15% utilization. The system could scale to 100+ zones before approaching limits.

---

## 11. Orbit/Pulsar/Triton Resource Budget (AM2432)

### Flash Usage (1.5 MB available)

| Component | Size | Notes |
|---|---|---|
| RTOS + drivers | ~50 KB | FreeRTOS + Ethernet + PRU-ICSS |
| Modbus TCP server | ~10 KB | Responds to Nova's polls |
| Register table + callbacks | ~8 KB | All I/O register definitions |
| Hardware drivers (GPIO/ADC/DRV8908/TMC2226) | ~15 KB | Variant-specific |
| Safety rule engine | ~5 KB | Local rules, 200 ms execution |
| **Total** | **~88 KB** | **6% of 1.5 MB** |

### SRAM Usage (1.5 MB available)

| Component | Size | Notes |
|---|---|---|
| Task stacks (4 tasks) | ~8 KB | 2 KB each |
| Register table | ~2 KB | 100 registers × 16 bytes |
| Modbus TX/RX buffers | ~2 KB | TCP |
| Safety rules (local copy) | ~1 KB | Pushed by Nova on boot |
| DRV8908 / TMC2226 state | ~1 KB | Per-channel status |
| **Total** | **~14 KB** | **1% of 1.5 MB** |

---

## 12. Revised Cost Architecture

| Card | MCU | Networking | Output Drivers | Approx Board Cost |
|---|---|---|---|---|
| **Nova** | AM2434 (~$14) | 2× DP83826 PHY ($2) | N/A | ~$85–120 |
| **Orbit** | AM2432 (~$10) | 2× DP83826 PHY ($2) | 2× DRV8908 ($5) | ~$25–35 |
| **Pulsar** (4-ch) | AM2432 (~$10) | 2× DP83826 PHY ($2) | 4× TMC2226 ($10) | ~$40–48 |
| **Pulsar** (8-ch) | AM2432 (~$10) | 2× DP83826 PHY ($2) | 8× TMC2226 ($20) | ~$55–63 |
| **Triton** | AM2432 (~$10) | 2× DP83826 PHY ($2) | 2× DRV8908 ($5) | ~$25–35 |

Orbit and Triton are the **same physical board** — same PCB, same BOM, same firmware. Role is assigned by Nova's site config.

### Compared to Previous Design

| Component | Old (STM32 + W5500 + KSZ8863) | New (AM2432 + 2× DP83826) |
|---|---|---|
| MCU | $5 | $10 |
| Ethernet controller | $3 (W5500) | — (built in) |
| Ethernet switch | $3 (KSZ8863) | — (built in) |
| Ethernet PHY | — | $2 (2× DP83826) |
| **Networking total** | **$11** | **$12** |
| SPI buses consumed | 2 (W5500 + KSZ8863) | 0 |
| Ethernet speed | 10/100 Mbps | Gigabit capable |
| Toolchain | STM32 CubeIDE | TI CCS (same as Nova) |
| Industrial protocols | None | PRU-ICSS: EtherNet/IP, PROFINET |

Net cost: essentially the same. Net capability: significantly higher.

---

## 13. Summary — What AM243x Brings to Constellation

| Benefit | Detail |
|---|---|
| **Single chip family** | Nova (AM2434) and all I/O cards (AM2432) from TI Sitara — one vendor, one toolchain, one learning curve |
| **Built-in dual Ethernet** | No W5500, no KSZ8863 — two Gigabit ports with hardware switch per card |
| **Daisy-chain native** | PRU-ICSS switch forwards traffic at wire speed — chain compressor cards with 6-inch jumpers |
| **Control independence** | Nova runs all PID, state machines, and VFD commands without the RPi5 — Pi is display only |
| **VFD profile engine** | Drive-agnostic — support ABB, Danfoss, Yaskawa, WEG with JSON profiles in flash |
| **Massive headroom** | Nova uses 13% of flash, 3% of SRAM — room for 100+ zones |
| **Industrial protocol ready** | PRU-ICSS can run EtherNet/IP or PROFINET if a customer requires it — future-proof |
| **Same-family upgrade path** | If a card needs more power, step up within AM243x — same code, bigger chip |
| **Dual-mode addressing** | DIP switch for simple static IPs, DHCP for zero-touch — both modes on same firmware |
| **Cost-neutral migration** | AM2432 + 2× PHY ≈ STM32 + W5500 + KSZ8863 — same cost, more capability |
