# AM2612 Platform Architecture — Complete Control System Design

**Date:** March 22–23, 2026  
**Context:** Agristar next-generation controller board design — replacing TM4C129ENCPDT with TI AM2612 Sitara  
**Scope:** Full system architecture for cold storage, refrigeration (GRC), VFD control, and field I/O over Modbus RTU  
**Updated:** March 23 — added communication strategy (latching, polling, VFD timeout tuning, immediate commands), field commissioning (DIP switch templates, auto-discovery, checklist), IO Config Level 2 integration  
**Related docs:** [VFD-Modbus-RTU-Architecture-Proposal.md](VFD-Modbus-RTU-Architecture-Proposal.md), [RTU-Migration-Guide.md](RTU-Migration-Guide.md), [Output-Relay-Technology-Analysis.md](Output-Relay-Technology-Analysis.md), [RTU-Relay-Module-Comparison.md](RTU-Relay-Module-Comparison.md)

---

## 1. Why the AM2612

The TI AM2612 (Sitara family) is a Cortex-R5F based industrial MCU designed for real-time control applications. It replaces the TM4C129ENCPDT, which has served as the AS2 controller but has reached its architectural limits — not in processing power, but in I/O strategy. The current board uses a custom CPLD shift register for digital outputs, a proprietary RS485 protocol for analog sensors, and pushes VFD control through a 5-layer RPi5-in-the-loop path that has proven fragile.

The AM2612 doesn't just solve these problems — it eliminates the need for them to exist. By moving to industry-standard Modbus RTU on multiple RS485 buses, the controller can talk directly to VFDs, relay modules, EEV controllers, power meters, and any other Modbus device — all without the RPi5 in the control path.

### AM2612 vs TM4C129ENCPDT

| Attribute | TM4C129ENCPDT (Current) | AM2612 Sitara (New) |
|---|---|---|
| **Core** | ARM Cortex-M4F | ARM Cortex-R5F |
| **Clock** | 120 MHz | Up to 400 MHz |
| **Architecture** | Microcontroller (MCU) | Microprocessor-class MCU (MPU/MCU hybrid) |
| **RAM** | 256 KB SRAM | 512 KB+ on-chip SRAM (+ external DDR support) |
| **Flash** | 1 MB on-chip | External OSPI/QSPI flash (larger, upgradeable) |
| **UARTs** | 8 | 6+ UART (4–9 depending on package) |
| **PRU-ICSS** | None | **Yes — 2× 200 MHz real-time I/O coprocessors** |
| **Ethernet** | 10/100 MAC | CPSW (Gigabit Ethernet, EtherCAT/PROFINET capable) |
| **CAN** | 2× CAN 2.0B | 2× MCAN (CAN FD capable) |
| **ADC** | 2× 12-bit, 24 channels | ADC with up to 6+ channels |
| **SPI** | 4× SSI | OSPI + McSPI (higher speed, DMA capable) |
| **Timer/PWM** | 8× GPTM + 8× PWM | EPWM, ECAP, EQEP (industrial motion control peripherals) |
| **Operating System** | FreeRTOS | FreeRTOS, TI-RTOS, or bare-metal |
| **SDK** | TivaWare | MCU+ SDK (mature industrial SDK) |
| **Lockstep** | None | Optional dual-core R5F lockstep for SIL-2/ASIL-B safety |
| **Industrial temp** | -40°C to +105°C | -40°C to +125°C |
| **Package** | 128-pin TQFP | 100–361 pin (package-dependent) |

### What the AM2612 Changes Architecturally

| What | TM4C129 (Current) | AM2612 (New) |
|---|---|---|
| **Digital outputs** | CPLD shift register (custom SPI bit-bang, 250ms poll) | Modbus RTU to commodity relay modules |
| **Digital inputs** | CPLD shift register (debounced, custom protocol) | Modbus RTU relay module DI ports |
| **VFD control** | RPi5 → CGI → Node.js → Modbus TCP → FMBT-21 ($200/drive) | Direct Modbus RTU on RS485 ($0.50 wire/drive) |
| **EEV control** | Not possible (no spare bus, no firmware support) | Modbus RTU to Danfoss/Carel EEV controllers |
| **Sensor reading** | Proprietary RS485 protocol on UART2, 9600 baud | Preserved as-is (backward compatible) |
| **RPi5 role** | Real-time control path (VFD, some decisions) | UI/logging/remote access only — never in control path |
| **Output expansion** | New CPLD expansion board (board redesign per project) | Add another $30 relay module to the bus |
| **Field wiring** | 1 dedicated wire per output (homerun to panel) | 2-wire RS485 daisy-chain to any number of outputs |
| **Failure resilience** | RPi5 reboot = loss of VFD control for 20–40 seconds | RPi5 reboot = zero impact on any control function |

---

## 2. AM2612 Peripheral Capabilities

### 2.1 UART / Serial — The Primary Control Interface

The AM2612 provides 6+ UART channels, all independently configurable with DMA support. Each UART connected to an RS485 transceiver (MAX485 or SN65HVD72, ~$1 each) becomes an independent industrial communication bus capable of:

- **247 devices per bus** (Modbus address space)
- **1200 meters** maximum cable length (RS485 differential signaling)
- **19200 baud** standard speed (115200 achievable over shorter runs)
- **Full electrical isolation** from field wiring (via the transceiver)

At 19200 baud with 8-byte Modbus frames, a single bus can complete a request-response transaction in ~10ms, supporting 50–100 device polls per second — far more than any Agristar installation requires.

### 2.2 PRU-ICSS — Deterministic I/O Coprocessors

The AM2612's two PRU (Programmable Real-time Unit) cores run at 200 MHz independently of the R5F main core. Each PRU has:

- Dedicated GPIO access (no OS jitter)
- Cycle-accurate timing (5ns resolution)
- Shared memory with main core
- Industrial Ethernet capability (EtherCAT slave, PROFINET)

**Potential uses for Agristar:**

| PRU Application | Description |
|---|---|
| **Custom protocol engine** | Run the existing analog board protocol (UART2, 9600 baud) without consuming a main-core UART |
| **High-speed pulse counting** | Flow meter input, energy metering pulse output |
| **Shift register emulation** | If backward compatibility with existing CPLD boards is needed during transition |
| **Industrial Ethernet slave** | Present the controller as an EtherCAT or PROFINET device to high-end BMS/SCADA |
| **PWM generation** | Variable-frequency drive gate signals (if ever needed for direct motor control) |

For the initial AM2612 board, the PRUs are likely reserved for future use. The main R5F core handles all Modbus RTU master tasks with margin to spare.

### 2.3 Ethernet — CPSW (Common Platform Switch)

The AM2612 includes an industrial-grade Ethernet subsystem:

- Gigabit Ethernet MAC with integrated switch
- IEEE 1588 PTP (Precision Time Protocol) hardware timestamping
- Hardware support for EtherCAT/PROFINET via PRU-ICSS
- RMII/RGMII PHY interface

**Current use:** Ethernet to RPi5 for UI bridge (replaces UART1 serial link — optional upgrade path).  
**Future use:** Direct Modbus TCP to cloud gateway, BMS integration, or multi-controller networking.

With Ethernet on the AM2612, the RPi5 bridge link could eventually move from UART1 (230400 baud serial, ~23 KB/s) to Ethernet TCP (100+ Mbps). This would allow:
- Higher data throughput for historical trend data
- Direct HTTP API from the controller (no serial tag translation needed)
- Multi-client access without the CGI dirty-flag bottleneck

However, this is a Phase 2+ optimization. UART1 serial bridge works today and transitions unchanged.

### 2.4 CAN FD — Vehicle and Agricultural Equipment

The AM2612 includes 2× MCAN peripherals supporting CAN FD (Flexible Data Rate):

- 64-byte payloads (vs 8-byte classic CAN)
- Up to 5 Mbps data rate
- SAE J1939 compatible (tractor/implement bus)

**Potential uses:**
- ISOBUS (ISO 11783) integration with tractors under precision ag
- Direct communication with vehicle-mounted controllers
- Multi-controller CAN bus between AS2 boards in large facilities

### 2.5 ADC — Direct Analog Measurement

The AM2612 has on-chip ADC channels with:

- 12-bit resolution
- Multiple simultaneous sample-and-hold
- DMA support for continuous sampling

This could eventually replace the external analog board for simple sensor reads (thermistors, 4–20mA pressure transmitters) — but the existing analog board over UART2 is proven and stays for now.

### 2.6 OSPI / QSPI Flash

External NOR flash via OSPI (Octal SPI) or QSPI (Quad SPI):

- 1–64 MB+ flash (vs 1 MB on-chip for TM4C129)
- XIP (Execute-In-Place) capability
- Firmware update via the bus (no physical access needed)
- Room for multiple firmware images, configuration storage, and data logging

This eliminates the SD card dependency for settings and logs that the TM4C129 currently has via the KFS filesystem on SSI0.

---

## 3. Complete UART Bus Allocation

### 3.1 Bus Map

| UART | RS485 Bus | Function | Baud | Devices | Status |
|---|---|---|---|---|---|
| **UART0** | — | Debug console | 115200 | — | Carried over from TM4C129 |
| **UART1** | — | RPi5 bridge (LTX serial protocol) | 230400 | RPi5 | Carried over from TM4C129 |
| **UART2** | Bus A | Analog sensor boards (proprietary protocol) | 9600 | Up to 4 analog boards | Carried over from TM4C129 |
| **UART3** | Bus B | Relay modules + digital I/O (Modbus RTU) | 19200 | Waveshare relay modules × N | **New** |
| **UART4** | Bus C | EEV controllers (Modbus RTU) | 19200 | Danfoss AK-CC / Carel EVD × N | **New** |
| **UART5** | Bus D | VFD drives (Modbus RTU) | 19200 | ABB ACS380 / other VFDs × N | **New** |

### 3.2 Why Separate Buses

Each subsystem has different timing requirements and criticality:

| Bus | Poll Rate | Criticality | Why Separate |
|---|---|---|---|
| **Bus A** (Analog sensors) | 250ms | High — temperature/pressure data feeds all control decisions | Legacy protocol, 9600 baud — would slow down a shared bus |
| **Bus B** (Relay modules) | 250ms | High — controls compressors, heaters, fans, defrost | Fastest response needed for safety lockouts |
| **Bus C** (EEV controllers) | 1–2s | Medium — superheat setpoints change slowly; real-time PID runs locally on EEV controller | Separate to avoid contention with relay writes during defrost transitions |
| **Bus D** (VFD drives) | 200ms | High — speed reference must be updated consistently to avoid comm timeout | Isolated from relay bus to prevent a stuck relay module from blocking VFD updates |

**Cost of separation:** 4× MAX485 transceivers = **$4 total**. This buys complete bus independence — a fault on one bus cannot affect any other subsystem.

### 3.3 Bus Architecture Diagram

```
                         ┌──────────────────────────────────────────────┐
                         │              AM2612 Sitara R5F               │
                         │              FreeRTOS @ 400 MHz              │
                         │                                              │
                         │  ┌──────────────────────────────────────┐    │
                         │  │  Control Logic (carried from TM4C)   │    │
                         │  │  ┌─────────┐ ┌──────────┐ ┌───────┐ │    │
                         │  │  │  PID    │ │ Staging  │ │Defrost│ │    │
                         │  │  │ Control │ │  Logic   │ │Manager│ │    │
                         │  │  └─────────┘ └──────────┘ └───────┘ │    │
                         │  │  ┌─────────┐ ┌──────────┐ ┌───────┐ │    │
                         │  │  │ Safety  │ │  Mode    │ │ Aux   │ │    │
                         │  │  │ Monitor │ │ Manager  │ │Control│ │    │
                         │  │  └─────────┘ └──────────┘ └───────┘ │    │
                         │  └──────────────────────────────────────┘    │
                         │                                              │
                         │  UART0 ── Debug (115200)                     │
                         │  UART1 ── RPi5 Bridge (230400)               │
                         │  UART2 ── MAX485 ── Bus A (Analog, 9600)     │
                         │  UART3 ── MAX485 ── Bus B (Relay, 19200)     │
                         │  UART4 ── MAX485 ── Bus C (EEV, 19200)       │
                         │  UART5 ── MAX485 ── Bus D (VFD, 19200)       │
                         │  Ethernet ── RPi5 (future upgrade path)      │
                         │  CAN0/1 ── reserved (J1939/ISOBUS future)    │
                         │  PRU0/1 ── reserved (protocol offload)       │
                         └──────────────────────────────────────────────┘
                              │          │           │           │
                              │          │           │           │
          ┌───────────────────┘          │           │           └───────────────────┐
          │ Bus A (UART2)                │           │             Bus D (UART5)     │
          │ Analog Sensors               │           │             VFD Drives        │
          │ 9600 baud                    │           │             19200 baud        │
          │                              │           │                               │
   ┌──────┴──────┐               ┌──────┴──────┐  ┌─┴───────────┐           ┌──────┴──────┐
   │ Analog Bd 1 │               │ Waveshare#1 │  │ AK-CC 750A  │           │ ACS380 #1   │
   │ (Temps,     │               │ 8DO + 8DI   │  │ EEV Ctrl #1 │           │ Fan VFD     │
   │  Humidity,  │               │ Unit ID=10  │  │ Unit ID=20  │           │ Unit ID=1   │
   │  CO2)       │               ├─────────────┤  ├─────────────┤           ├─────────────┤
   ├─────────────┤               │ Waveshare#2 │  │ AK-CC 750A  │           │ ACS380 #2   │
   │ Analog Bd 2 │               │ 8DO + 8DI   │  │ EEV Ctrl #2 │           │ Comp VFD    │
   │ (Addl temps │               │ Unit ID=11  │  │ Unit ID=21  │           │ Unit ID=2   │
   │  Pressure)  │               ├─────────────┤  ├─────────────┤           ├─────────────┤
   └─────────────┘               │ Waveshare#3 │  │ EVD Evol #3 │           │ ACS380 #3   │
                                 │ 8DO + 8DI   │  │ EEV Ctrl #3 │           │ Cond Fan VFD│
                                 │ Unit ID=12  │  │ Unit ID=22  │           │ Unit ID=3   │
                                 └─────────────┘  └─────────────┘           └─────────────┘
                                  Bus B (UART3)     Bus C (UART4)
                                  Relay Modules     EEV Controllers
                                  19200 baud        19200 baud
```

---

## 4. What the AM2612 Controls — Complete Subsystem Map

### 4.1 Raw Vegetable Cold Storage (AS2 Application)

| Subsystem | Devices on Bus | What ARM Does | Bus |
|---|---|---|---|
| **Box temperature** | Analog board NTC sensors | PID control → fan speed, refrigeration staging | A |
| **Humidity** | Analog board humidity sensors | Humidifier staging (pump + heads, 3 stages) | A |
| **CO2 monitoring** | Analog board CO2 sensor | Vent control for respiration management | A |
| **Evaporator fans** | Relay module → fan contactors | ON/OFF or VFD speed control during pull-down vs hold | B / D |
| **Condenser fans** | Relay module → fan contactors or VFD | Head pressure control algorithm | B / D |
| **Compressor staging** | Relay module → compressor contactors | Stage up/down based on suction pressure + box temp | B |
| **Defrost** | Relay module → defrost heaters or hot gas valves | Time-initiated, temperature-terminated cycles | B |
| **Door heaters** | Relay module → door frame heaters | Anti-ice during defrost cycles | B |
| **Dampers / vents** | Relay module → actuator power | Fresh air exchange, ethylene management | B |
| **Lighting** | Relay module → light contactors | Scheduled or occupancy-based | B |
| **Alarms** | Relay module → beacon / siren | High temp, low temp, pressure fault, power failure | B |
| **EEV superheat** | Danfoss AK-CC / Carel EVD | Set superheat setpoint, read actual superheat | C |
| **VFD fan speed** | ABB ACS380 (built-in RS485) | Write speed reference, read actual Hz / amps / faults | D |
| **VFD compressor** | ABB ACS380 or equivalent | Capacity modulation on scroll/screw compressors | D |
| **Safety lockouts** | Relay module DIs ← pressure switches | High/low pressure cutout, oil pressure, phase monitor | B |
| **Door switches** | Relay module DIs ← contacts | Track door state for defrost inhibit, alarm | B |

### 4.2 GRC Refrigeration Controller (Same AS2 Board)

The GRC uses the same AM2612 board but runs different control firmware for dedicated refrigeration systems at 40–140 ton scale. The bus architecture is identical — only the control logic (task code) changes.

| Subsystem | What It Controls | Devices | Bus |
|---|---|---|---|
| **Compressor staging** | 2–6 compressors, anti-short-cycle, rotation | Relay module → 3-phase contactors (Schneider LC1D, ABB AF) | B |
| **Condenser head pressure** | Target discharge pressure via fan staging or VFD | Relay module (staging) or VFD (modulation) | B / D |
| **Evaporator superheat** | EEV setpoint and monitoring per circuit | AK-CC 750A or EVD Evolution, 3–6 circuits | C |
| **Defrost management** | Time-initiated, temp-terminated, demand-based | Relay module → hot gas valves or electric heaters | B |
| **Oil management** | Oil return cycles, oil level monitoring | Relay module → solenoid valves, DI ← level switches | B |
| **Suction pressure control** | Target suction via staging + EEV coordination | PID in firmware, outputs to staging + EEV setpoint | B + C |
| **Economizer** | Subcooling via EEV on economizer circuit | Additional EEV controller on Bus C | C |
| **Safety chain** | High pressure, low oil, motor overload, phase loss | Relay module DIs ← safety switches | B |
| **Pump-down** | Evacuate evaporator before compressor OFF | Sequence: close EEV → wait for low suction → compressor OFF | B + C |
| **Hot gas bypass** | Prevent low suction during extreme part-load | Relay module → HGBP solenoid (or EEV-controlled) | B / C |

### 4.3 Output Count Comparison

| Metric | TM4C129 (Current) | AM2612 (New) |
|---|---|---|
| **Main board DO** | 11 (CPLD shift register) | Unlimited (add relay modules to Bus B) |
| **Expansion board DO** | 8 per board (CPLD, requires board redesign) | Unlimited (add relay modules, no board change) |
| **Main board DI** | 8 (CPLD shift register) | Unlimited (relay module DI ports on Bus B) |
| **Maximum outputs** | 11 + (8 × expansion count), hard-limited by CPLD design | 247 × 8 = 1,976 theoretical; practical: as many relay modules as needed |
| **Cost per expansion** | Custom CPLD expansion board (~$40–60) | Waveshare 8-ch relay module ($30) |
| **Wiring per expansion** | Ribbon cable to main board (short distance only) | RS485 daisy-chain (up to 1200m from controller) |

---

## 5. Firmware Architecture on the AM2612

### 5.1 FreeRTOS Task Structure

The current TM4C129 firmware runs 5 tasks, all at the same priority. The AM2612 firmware restructures these with proper priority stratification and adds Modbus master tasks for each bus:

| Task | Priority | Period | Stack | Purpose |
|---|---|---|---|---|
| `SafetyMonitor` | **Highest** | 500ms | 512 | Pressure switch lockouts, motor overload, phase loss. Reads Bus B DIs. Can override any output. |
| `ModbusRelayPoll` | **High** | 5s | 1024 | Read relay module states + DI inputs on Bus B. Execute queued output commands immediately via `xQueueReceive`. Feed module watchdog timers. |
| `ModbusVFDPoll` | **High** | 120s | 1024 | Read actual speed, current, fault codes from VFDs on Bus D. Execute queued speed/start/stop commands immediately via `xQueueReceive`. VFD timeout disabled (Param 51.25=0) — drives hold last speed between polls. |
| `CompressorStaging` | **High** | 1s | 512 | Stage compressors up/down based on suction pressure, box temperature, and anti-short-cycle timers. |
| `DefrostManager` | **High** | 10s | 512 | Track accumulated run-time, initiate time-based defrosts, terminate on temperature, manage drip time. |
| `ModbusEEVPoll` | **Medium** | 2s | 1024 | Write superheat setpoint to EEV controllers on Bus C. Read actual superheat, valve position, alarms. |
| `SystemControl` | **Medium** | 1s | 2048 | PID loops (temperature, humidity), mode management, auxiliary control rules. **This is the existing control logic migrated from TM4C129.** |
| `AnalogBoardPoll` | **Medium** | 250ms | 1024 | Existing analog board protocol on Bus A (UART2). Temperature, humidity, CO2, pressure sensor reads. |
| `CondenserFanControl` | **Medium** | 5s | 512 | Head pressure algorithm — stage condenser fans or adjust VFD speed based on discharge pressure. |
| `UIBridge` | **Low** | 100ms | 2048 | Forward all data to RPi5 over UART1 using existing LTX serial tag protocol. Receive UI commands. |
| `TaskMonitor` | **Low** | 5s | 256 | Watchdog feed, task health monitoring, stack high-water-mark checking. Carried from TM4C129. |
| `FileManager` | **Low** | On-demand | 1024 | Firmware upgrade reception, settings save/load to QSPI flash. Replaces SD card KFS. |

**Total: 12 tasks** — the AM2612 at 400 MHz with 512+ KB RAM can handle this with <20% CPU utilization. The TM4C129 runs 5 tasks at 120 MHz with 256 KB and is nowhere near its limits either. The AM2612 provides headroom for future expansion without concern.

### 5.2 Modbus RTU Master — Shared Library

All three Modbus buses (B, C, D) share the same master implementation. Each bus gets its own task with its own UART handle, but the frame building, CRC calculation, and response validation are common:

```
modbus_rtu.c / .h — shared Modbus RTU library (~200 lines)
├── mb_crc16()              — CRC16 with 0xA001 polynomial
├── mb_build_request()      — Assemble TX frame (unit ID + FC + addr + data + CRC)
├── mb_validate_response()  — Check CRC, unit ID, exception codes
├── mb_read_registers()     — FC03/04 wrapper (read holding/input registers)
├── mb_write_register()     — FC06 wrapper (write single register)
├── mb_write_registers()    — FC10 wrapper (write multiple registers)
├── mb_read_coils()         — FC01 wrapper (read relay output states)
├── mb_write_coil()         — FC05 wrapper (write single relay)
└── mb_transaction()        — Send request, wait for response with timeout
```

Each bus task calls `mb_transaction()` with its own UART handle and direction GPIO. The library doesn't know or care what's on the other end — VFD, relay module, or EEV controller. It just sends Modbus frames and validates responses.

### 5.3 What Migrates Unchanged from TM4C129

| Component | Source Files | Migration Effort |
|---|---|---|
| **PID control loops** | `Controls.c` — `PIDController()`, `PID_Init()` | Zero — pure math, no hardware dependency |
| **Compressor staging logic** | `Controls.c`, `Modes.c` | Zero — currently outputs to `OutputOn()`/`OutputOff()`, retarget to Modbus relay write |
| **Defrost management** | `Controls.c` — `CtrlDefrost()` | Zero — timer logic, retarget output calls |
| **Mode/state machine** | `Modes.c`, `States.c` | Zero — pure logic |
| **Auxiliary control rules** | `Controls.c` — `CtrlAux()` | Zero — duty cycle logic, retarget outputs |
| **Temperature conversion** | `Analog_Input.c` — `ConvertToTemp()` | Zero — NTC lookup table, pure math |
| **Settings management** | `Settings.h`, `SDCard.c` | Retarget from KFS/SD to QSPI flash — same data structures, different storage driver |
| **Serial bridge protocol** | `ThreadSerialCom.c` (LTX tags) | Zero — UART1 protocol unchanged |
| **Analog board protocol** | `RS485.c`, `Analog_Input.c` | Zero — UART2 protocol unchanged, same custom packet format |

**What's new (not migrated — written fresh):**

| Component | Estimated Size | Purpose |
|---|---|---|
| `modbus_rtu.c` | ~200 lines | Shared Modbus RTU master library |
| `task_modbus_relay.c` | ~150 lines | Bus B relay module poll task |
| `task_modbus_vfd.c` | ~150 lines | Bus D VFD drive poll task |
| `task_modbus_eev.c` | ~150 lines | Bus C EEV controller poll task |
| `task_safety.c` | ~100 lines | Safety monitor (reads DIs, enforces lockouts) |
| `output_abstraction.c` | ~80 lines | `OutputOn()`/`OutputOff()` reimplemented as Modbus coil writes — **the bridge that lets all existing control code work unchanged** |
| **Total new code** | **~830 lines** | Everything else is migrated as-is |

### 5.4 Output Abstraction Layer — The Migration Bridge

The key to painless migration is reimplementing `OutputOn()` and `OutputOff()` (currently in `SerialShift.c`) to write Modbus coils instead of shift register bits. The existing control code calls:

```c
OutputOn(EQ_REFRIG_STAGE_1);   // Turn on compressor stage 1
OutputOff(EQ_HEAT);            // Turn off heater
```

Currently this sets a bit in `IoBoard[board].OutputState` which `ThreadSerialShift` pushes to the CPLD every 250ms. On the AM2612, the same call maps to:

```c
void OutputOn(EQUIPMENT_IO equip) {
    uint8_t unit_id = EquipMap[equip].modbus_unit_id;  // e.g. 10
    uint16_t coil   = EquipMap[equip].modbus_coil;     // e.g. 0x0003
    mb_write_coil(BUS_B_UART, unit_id, coil, true);
}
```

The `EquipMap[]` table replaces `Settings.EquipIo[]` — it maps each equipment enum to a Modbus unit ID + coil address instead of a board number + bit position. The control logic above this layer doesn't change at all. 

**The ~60 equipment enums in EQUIPMENT_IO** (fans, heaters, stages, defrosts, doors, humidifiers, etc.) all work through this one abstraction layer. That's why ~80 lines of `output_abstraction.c` replaces the entire CPLD shift register subsystem.

---

## 6. What the RPi5 Becomes

With the AM2612 handling all real-time control, the RPi5's role simplifies to:

| Function | Implementation | Data Source |
|---|---|---|
| **Web UI** | Svelte app served by lighttpd (unchanged) | ARM sends all data over UART1 serial tags |
| **Historical logging** | GellertFileSystem.out (unchanged, runs on RPi5) | Receives data via CGI from bridge server |
| **Email alerts** | GellertEmailResponder (unchanged) | Triggered by alarm tags from ARM |
| **Network discovery** | GellertQueryResponder UDP (unchanged) | Already self-contained on RPi5 |
| **Firmware upgrades** | GellertProgResponder (unchanged) | RPi5 receives file, sends over UART1 to ARM |
| **Cloud/IoT gateway** | iotclient (unchanged) | Reads CGI data like any other client |
| **VFD status display** | Bridge server parses VFD serial tags → WebSocket → UI | ARM sends `^VFD1_SPD=1480$`, `^VFD1_CUR=3.2$` etc. |
| **EEV status display** | Bridge server parses EEV serial tags → WebSocket → UI | ARM sends `^EEV1_SH=10.2$`, `^EEV1_POS=45$` etc. |
| **Remote configuration** | UI POST → bridge → serial command → ARM → Modbus write | User changes setpoint, ARM executes immediately |

**What gets removed from the RPi5:**
- `vfdServer.ts` and `vfdClient.ts` — VFD control moves to ARM firmware
- FMBT-21 Modbus TCP adapter ($200 per drive) — no longer needed
- `/vfd/*` proxy in lighttpd.conf
- `agristar-vfd.service` systemd unit

**What stays completely unchanged:**
- lighttpd + FastCGI (gellertgetd / gellertpostd)
- All responder daemons (FileSystem, Query, Email, Prog)
- The entire Svelte UI application
- Bridge server (gains new serial tags for VFD/EEV but pattern is identical)
- iotclient

The RPi5 is now a **pure display terminal and network gateway**. If it crashes, reboots, or its SD card dies — the cold room keeps running. The AM2612 maintains all temperatures, pressures, fan speeds, compressor staging, superheat, and defrost cycles autonomously until the RPi5 comes back online.

---

## 7. Board BOM Impact

### 7.1 What's Removed from the Board

| Component | Qty (TM4C129 board) | Cost | Notes |
|---|---|---|---|
| CPLD IC | 1 | ~$5–8 | Xilinx/Lattice shift register controller |
| Shift register ICs | 2–4 | ~$2–4 | 74HC595 or equivalent output registers |
| Latch ICs | 1–2 | ~$1–2 | Input latch registers |
| Buffer/driver ICs | 2–3 | ~$2–3 | Output current drivers |
| Connector headers (shift register bus) | 3–5 | ~$2–3 | Board-to-board for expansion |
| TVS/protection on shift register lines | 4–8 | ~$1–2 | ESD protection on custom bus |
| **Total removed** | | **~$13–22** | Plus significant PCB routing simplification |

### 7.2 What's Added to the Board

| Component | Qty | Cost | Purpose |
|---|---|---|---|
| MAX485 / SN65HVD72 RS485 transceiver | 4 | ~$4 | One per bus (B, C, D + existing A) |
| 2-position screw terminal (RS485 A/B) | 3 | ~$1.50 | Bus B, C, D connectors (Bus A already exists) |
| 120Ω termination resistor + jumper | 3 | ~$0.30 | End-of-bus termination (selectable) |
| 560Ω bias resistors (pull-up/down) | 6 | ~$0.10 | Bus idle state (2 per bus) |
| **Total added** | | **~$6** | Plus trivial PCB routing (3 traces per bus) |

### 7.3 Net Board Cost Change

```
Removed: ~$13–22 (CPLD + shift registers + drivers + connectors)
Added:   ~$6     (4× MAX485 + terminals + passives)
─────────────────────────────────────────────────────
Net:     $7–16 savings per board
```

The board gets **simpler and cheaper** while gaining vastly more I/O capability. The PCB routing simplification is arguably worth more than the BOM savings — the CPLD shift register bus requires careful trace routing, controlled impedance, and multi-layer routing. Four RS485 buses each need exactly 3 traces (TX, RX, DIR) to a transceiver.

### 7.4 Per-Installation Cost Impact

| Item | TM4C129 System | AM2612 System | Delta |
|---|---|---|---|
| Controller board | $44 (estimated) | ~$30–35 | **-$10 to -$14** |
| FMBT-21 per VFD | $200 × N | $0 | **-$200 × N** |
| Expansion CPLD boards | ~$40–60 × N | $0 | **-$40–60 × N** |
| Waveshare relay modules | $0 | $30 × N | +$30 × N |
| EEV controllers (new capability) | N/A | ~$300–800 per circuit | New cost, but <1 year payback in energy |
| **Typical 4-VFD, 24-output system** | ~$44 + $800 + $120 = **$964** | ~$35 + $0 + $90 = **$125** + EEV if desired | **-$839 before EEV** |

---

## 8. Reliability and Safety Architecture

### 8.1 Failure Mode Comparison

| Failure | TM4C129 System Impact | AM2612 System Impact |
|---|---|---|
| **RPi5 reboot** | VFD control lost 20–40s, UI offline | UI offline, **all control continues** |
| **RPi5 SD card corrupt** | VFD control lost permanently until service; UI dead | UI dead until SD replaced, **all control continues** |
| **lighttpd crash** | CGI unavailable → VFD speed stale → comm timeout | UI dead until restart, **all control continues** |
| **Node.js hang** | Modbus TCP writes stop → VFD comm timeout (2s) | Bridge data stale, **all control continues** |
| **Ethernet cable disconnected** | VFD Modbus TCP broken → drives fault | UI offline, **all control continues** (RS485 is independent) |
| **RS485 Bus B wire break** | N/A (using CPLD) | Relay module offline → safety timeout → safe fallback |
| **RS485 Bus D wire break** | N/A (using Ethernet) | VFD comm timeout → drive holds last speed or faults (configurable) |
| **ARM watchdog reset** | All control lost <1s, auto-recover | Same — but RPi5 is NOT needed for recovery |
| **Power interruption** | All states lost, RPi5 must boot to resume VFD | All states lost, AM2612 boots in <1s, resumes immediately |

### 8.2 Safety Layering

```
Layer 1 (Hardware):    Pressure switches, motor overloads, phase monitors
                       (hardwired, bypass all software)

Layer 2 (Firmware):    SafetyMonitor task, 500ms poll of DI bus
                       Reads relay module DIs, enforces software lockouts
                       Can override any output via Modbus coil write

Layer 3 (Output):      Relay module output monitoring — contactor aux contacts
                       wired back to DI ports on same relay module
                       Firmware detects stuck outputs (coil OFF but DI still ON)

Layer 4 (Comm):        Modbus comm timeout on relay modules — if ARM stops
                       polling, modules can be configured to trip all outputs OFF

Layer 5 (Watchdog):    ARM hardware watchdog — if firmware hangs, hard reset
                       in <1s, auto-resume all control states from QSPI flash
```

**Layer 1 is never software-dependent.** A high-pressure cutout switch physically breaks the contactor coil circuit. This is unchanged from the current system and is required by UL 508A regardless of controller architecture.

### 8.3 Modbus Timeout Behavior

Each Modbus slave device has a configurable communication timeout. If the AM2612 stops polling (crash, bus fault, etc.):

| Device Type | Timeout | Behavior |
|---|---|---|
| **Waveshare relay module** | Configurable (typically 10–30s) | All outputs OFF (safe state) |
| **ABB ACS380 VFD** | Parameter 51.25 (default 2s) | Fault stop or coast stop (configurable) |
| **Danfoss AK-CC EEV** | Configurable | Close valve (safe state for pump-down) |
| **Carel EVD EEV** | Configurable | Hold last position or close (configurable) |

This means the system fails safe **even if the ARM dies completely** — field devices self-protect on communication loss.

---

## 9. The Product Line — One Board, Multiple Applications

The AM2612 board is a universal industrial controller. The hardware is identical across all applications — only the firmware (FreeRTOS task configuration and control logic) changes:

| Product | Application | Key Subsystems | Firmware Variant |
|---|---|---|---|
| **AS2** | Raw vegetable cold storage | Box temp PID, fan staging, humid control, defrost, VFD | `as2_storage.bin` |
| **GRC** | Refrigeration controller (40–140 ton) | Compressor staging, suction control, superheat (EEV), head pressure, defrost, oil mgmt | `grc_refrig.bin` |
| **AS2 + GRC** | Integrated storage + refrigeration | All of the above in one controller | `as2_grc_combined.bin` |
| **Future: Greenhouse** | Climate control | Vent actuators, shade curtains, irrigation, supplemental lighting, heating | `as2_greenhouse.bin` |
| **Future: Packing** | Packing house climate | Evap cooling, ventilation, humidity, dock door management | `as2_packing.bin` |

The hardware BOM, PCB layout, enclosure, and field wiring pattern are the same. A field tech deploys the same board everywhere and loads the appropriate firmware via the RPi5 upgrade mechanism.

### 9.1 Peripheral Usage by Application

| Peripheral | AS2 Storage | GRC Refrig | Greenhouse | Packing |
|---|---|---|---|---|
| Bus A (Analog sensors) | ✅ Temp, humid, CO2 | ✅ Temp, pressure | ✅ Temp, humid, light, wind | ✅ Temp, humid |
| Bus B (Relay modules) | ✅ Fans, heaters, dampers | ✅ Compressors, fans, defrost, solenoids | ✅ Vents, curtains, irrigation, lights | ✅ Fans, cooling, docks |
| Bus C (EEV controllers) | Optional | ✅ 3–6 circuits | ❌ | ❌ |
| Bus D (VFD drives) | ✅ 1–4 fans | ✅ Comp + fans | ✅ HAF fans | ✅ Evap fans |
| Ethernet | ✅ RPi5 bridge | ✅ RPi5 bridge | ✅ RPi5 bridge | ✅ RPi5 bridge |
| CAN | ❌ | ❌ | Future (ISOBUS) | ❌ |
| PRU | Reserved | Reserved | Reserved | Reserved |

---

## 10. Migration Path — Phase Plan

### Phase 1: AM2612 Board Bring-Up
- Layout new PCB with AM2612 + 4× MAX485 + screw terminals
- Port FreeRTOS BSP from TivaWare to MCU+ SDK
- Verify UART0 (debug), UART1 (RPi5 bridge), UART2 (analog boards)
- Confirm all existing sensors read correctly through analog board protocol
- **Gate:** AM2612 board runs existing control logic with existing analog boards and RPi5 UI

### Phase 2: Modbus RTU Master + Relay Modules
- Implement `modbus_rtu.c` shared library (~200 lines)
- Implement `output_abstraction.c` — `OutputOn()`/`OutputOff()` → Modbus coil writes (~80 lines)
- Implement `task_modbus_relay.c` for Bus B (~150 lines)
- Wire Waveshare relay modules in test panel
- Verify all existing equipment outputs work through relay modules
- **Gate:** All digital outputs controlled via Modbus relay modules instead of CPLD

### Phase 3: VFD Direct Control
- Implement `task_modbus_vfd.c` for Bus D (~150 lines)
- Configure ACS380 drives for Modbus RTU (Parameter 51.21–51.25)
- Remove FMBT-21 adapters
- Add VFD serial tags to bridge server for UI display
- **Gate:** VFDs controlled directly by ARM, no RPi5 in control path

### Phase 4: EEV Integration (GRC)
- Implement `task_modbus_eev.c` for Bus C (~150 lines)
- Wire Danfoss AK-CC or Carel EVD controllers to Bus C
- Add superheat serial tags to bridge server for UI display
- Implement superheat setpoint control from staging logic
- **Gate:** Superheat controlled via Modbus EEV, visible in UI

### Phase 5: Remove Legacy
- Remove CPLD shift register code (`SerialShift.c`, `ThreadSerialShift`)
- Remove CPLD hardware from board BOM
- Remove `vfdServer.ts`, `vfdClient.ts` from RPi5 deployment
- Remove FMBT-21 from VFD installation spec
- Update field technician installation procedures
- **Gate:** Clean AM2612-only system, no legacy CPLD or RPi5-in-loop code

---

## 11. Summary

The AM2612 is not an incremental upgrade from the TM4C129 — it's an architectural shift from a custom-everything controller to an industry-standard bus-based platform. The processor capability increase is significant (Cortex-R5F @ 400 MHz vs Cortex-M4F @ 120 MHz, 4× the SRAM, PRU coprocessors), but the real value is in what it enables at the system level:

| Metric | TM4C129 System | AM2612 System |
|---|---|---|
| **Lines of custom hardware I/O code** | ~2,000 (CPLD + shift registers) | ~80 (output abstraction layer) |
| **Custom ICs on board** | CPLD + shift registers + latches + drivers | 0 (all commodity parts) |
| **Per-VFD cost** | +$200 (FMBT-21) | +$0.50 (wire) |
| **Per-output expansion cost** | +$40–60 (CPLD expansion board) | +$3.75 (one channel on a $30/8-ch relay module) |
| **Max outputs without board redesign** | ~27 (1 main + 2 expansion) | Unlimited (add modules to bus) |
| **EEV capability** | Not possible | Yes — 247 controllers per bus |
| **RPi5 failure impact** | Loss of VFD control + UI | Loss of UI only |
| **Time from ARM power-on to full control** | <1s | <1s |
| **Time from RPi5 power-on to full control** | 20–40s (Linux boot + services) | N/A (RPi5 not needed for control) |
| **New firmware to write** | — | ~830 lines total |
| **Existing firmware reused** | — | ~95% (all control logic, PID, modes, analog protocol) |
| **Board BOM cost change** | Baseline | **-$7 to -$16** (simpler, cheaper) |
| **System cost at 4 VFDs + 24 outputs** | ~$964 | ~$125 |

One board. Four RS485 buses. Standard Modbus RTU. Controls cold storage, refrigeration, VFDs, EEVs, and any future Modbus device — all without the RPi5 in the control path.

---

## 12. Communication Strategy — Latching, Polling, and Immediate Commands

### 12.1 Relay Modules Are Latch-and-Forget

Modbus relay modules use physical electromechanical relays. When the AM2612 writes a coil via FC05 (`Write Single Coil`) or FC0F (`Write Multiple Coils`), the relay physically closes and **stays closed permanently** until the firmware explicitly writes a different value. There is no keep-alive required for the output state itself.

```
6:00 AM  — Staging logic decides: stage up compressor 1
           ARM sends: FC05, Unit 1, Coil 0, ON         (one 8ms transaction)
           ARM reads: FC01, Unit 1, Coils 0–7           (confirms ON)
           Done. Bus goes quiet.

6:00 AM → 2:00 PM — Compressor runs. Relay stays latched.
                     No communication needed for this output.

2:00 PM  — Staging logic decides: stage down
           ARM sends: FC05, Unit 1, Coil 0, OFF        (one 8ms transaction)
           ARM reads: FC01, Unit 1, Coils 0–7           (confirms OFF)
           Done.
```

**Total bus time for 8 hours of compressor run: ~32ms** (two writes + two reads).

### 12.2 Why Poll at All?

If relays latch, why does the firmware poll relay modules? Three reasons:

| Reason | Description |
|---|---|
| **Stuck output detection** | Read back relay states (FC01) and compare with expected. If firmware wrote OFF but reads ON → relay welded → raise alarm. This is the real safety layer. |
| **Digital input reads** | Pressure switches, door contacts, overload relays are wired to the module's DI ports. FC02 reads these. |
| **Watchdog keepalive** | If the module's comm watchdog is enabled, any Modbus transaction (including a read) resets the timer. |

A **2–5 second poll interval** for relay modules is more than adequate. Equipment states change on the order of minutes to hours, not milliseconds.

### 12.3 Recommended Poll Rates by Device Type

| Device | Poll Interval | Why | Bus Time per Cycle |
|---|---|---|---|
| **Relay modules** (Bus B) | **5 seconds** | Latch-and-forget; poll is for DI reads + stuck output detection + watchdog | ~16ms per module (2 reads) |
| **VFD drives** (Bus D) | **2 minutes** | Timeout disabled (Param 51.25=0); drive holds last speed; poll is for UI display + fault monitoring only | ~10ms per drive (1 read) |
| **EEV controllers** (Bus C) | **2 seconds** | Superheat setpoints change slowly; poll is for UI display of actual superheat; EEV controller runs its own PID internally | ~10ms per controller (1 read) |
| **Analog boards** (Bus A) | **250ms** | Temperature and pressure readings feed PID loops — these actually need to be fast | ~15ms per board (existing protocol) |

### 12.4 VFD Timeout Tuning — Hold Last Speed

By default, the ACS380 faults after 2 seconds of no communication. This is designed for applications like pump pressure control where losing the controller means the motor must stop. For cold storage fans that run at a steady speed for hours, this is counterproductive.

**ACS380 Parameters for Agristar configuration:**

| Parameter | Name | Default | Agristar Setting | Effect |
|---|---|---|---|---|
| **51.25** | Communication break time | 2.0s | **0** (disabled) | Drive never faults on comm loss — holds last speed forever |
| **51.26** | Communication break action | 0 (Fault) | **1** (Warning + last speed) | If timeout is ever re-enabled, drive warns but keeps running |
| **46.01** | Minimum speed | Varies | **0 Hz** | Linear 1:1 scaling: 50% command = 50% speed = 30 Hz |

With timeout disabled (51.25=0):
- **ARM sends 60% → drive runs at 60% indefinitely with zero further communication**
- ARM polls every 2 minutes only to read actual speed/current/faults for UI display
- If ARM reboots (< 1 second), the drive never notices — keeps running at 60%
- If ARM crashes for 10 minutes, drive holds 60% — product stays cold

**For a cold storage fan, a running fan is always safer than a stopped fan.** Product warming, compressor overwork, and head pressure rise are all worse outcomes than a fan running at its last commanded speed.

### 12.5 Bus Utilization with Optimized Polling

Typical installation: 3 relay modules + 4 VFD drives + 2 EEV controllers + 2 analog boards.

| Bus | Devices | Poll Interval | Transactions/Hour | Bus Time/Hour | Utilization |
|---|---|---|---|---|---|
| **Bus A** (Analog) | 2 boards | 250ms | 14,400 | ~216s | 6.0% |
| **Bus B** (Relay) | 3 modules | 5s | 2,160 | ~35s | 0.97% |
| **Bus C** (EEV) | 2 controllers | 2s | 3,600 | ~36s | 1.0% |
| **Bus D** (VFD) | 4 drives | 120s | 120 | ~1.2s | **0.03%** |
| | | | **Total** | **~288s** | **< 2% average across all buses** |

**Write (command) transactions are negligible** — a few dozen per day across all buses, total of maybe 1 second of bus time.

### 12.6 Immediate Commands — Event-Driven Writes

The poll interval is purely for background health monitoring. **Actual commands (start, stop, speed change) execute immediately** regardless of where the poll cycle is.

The firmware uses a FreeRTOS queue to decouple control decisions from bus timing:

```c
void task_modbus_vfd(void *pv) {
    for (;;) {
        // Block for up to 120 seconds waiting for a command
        // If a command arrives, wake immediately and execute it
        VFDCommand cmd;
        if (xQueueReceive(vfdCmdQueue, &cmd, pdMS_TO_TICKS(120000)) == pdTRUE) {
            // Immediate command — execute NOW
            uint16_t regs[2] = { cmd.cw, cmd.ref };
            mb_write_registers(BUS_D, cmd.unit_id, 0x0001, 2, regs);
            mb_read_registers(BUS_D, cmd.unit_id, 0x0003, 4, &drives[cmd.idx].status);
        }

        // Either a command just fired or 120s elapsed — do health poll
        for (int i = 0; i < num_vfds; i++) {
            mb_read_registers(BUS_D, drives[i].id, 0x0003, 4, &drives[i].status);
        }
    }
}
```

The `xQueueReceive` with a 120-second timeout means the task sleeps (zero CPU) until either:
- **A command arrives** → wakes instantly, executes the Modbus write, then health poll
- **120 seconds elapse** → wakes for scheduled health poll

When the user presses Shutdown in the UI:

```
User taps Shutdown button                              →  0 ms
UI sends HTTP POST to bridge server                    →  ~5–20 ms
Bridge sends serial tag ^SHUTDOWN=1$CRC! to ARM UART1  →  ~2–5 ms
ARM control task processes shutdown, queues VFD stop   →  < 1 ms
VFD task wakes from sleep, sends CW=STOP to drive     →  ~10 ms
──────────────────────────────────────────────────────────────────
Total: button press to drive receiving STOP command     =  ~20–35 ms
```

Same pattern for relay modules — the relay poll task uses `xQueueReceive` with a 5-second timeout. Any `OutputOn()` or `OutputOff()` call queues an immediate write that executes within milliseconds.

### 12.7 Relay Module Watchdog Strategy

Most Modbus relay modules (including Waveshare) have a configurable communication watchdog. When enabled, if no Modbus transaction reaches the module within the timeout period, all outputs reset to OFF. Any transaction (including a read) resets the timer.

**Per-module watchdog assignment:**

| Module Function | Watchdog | Timeout | Rationale |
|---|---|---|---|
| **Compressor stages** (Module 1) | **Enabled** | 30s | If ARM dies, compressors should stop — prevents running without safety monitoring. Hardware high-pressure switch provides backup. |
| **Fans + Heaters** (Module 2) | **Split** — fans OFF, heaters ON | 30s | Fans running unmonitored is safe; heaters running unmonitored during defrost is a fire risk. If module doesn't support per-channel watchdog, enable for the whole module (heaters are higher risk than stopping fans). |
| **Defrost + Aux** (Module 3) | **Enabled** | 30s | Defrost heaters must stop if ARM is unresponsive — no temperature termination monitoring = overheating risk. |
| **Alarms / Lights** | **Disabled** | — | If ARM dies with alarms active, the alarm should keep screaming. |

The 5-second relay poll (Section 12.3) keeps the watchdog well within its 30-second timeout. Even if 5 consecutive polls are missed (unlikely), there's still 5 seconds of margin.

### 12.8 Power Loss and State Recovery

If a relay module loses power and comes back:

| Behavior | Description |
|---|---|
| **Default (all OFF on power-up)** | Standard for Waveshare and most modules. ARM's next poll detects mismatch between expected and actual states → re-writes all expected states. |
| **Restore last state (configurable)** | Some modules support saving last state to flash. Not recommended for safety-critical outputs — you want the ARM's staging logic to verify safety (anti-short-cycle timers, pressure equalization) before re-energizing compressors. |

All-OFF on power-up is the correct default for refrigeration. The AM2612 boots in < 1 second, discovers modules, and re-establishes all output states based on current control logic — not blindly restoring whatever was running before the outage.

---

## 13. Field Commissioning — DIP Switches and Standard Templates

### 13.1 Relay Module Address Configuration

Waveshare relay modules set their Modbus address via **physical DIP switches** on the board. No software programming, no USB connection, no configuration tool. A field technician sets the DIP switches, wires the RS485 A/B, connects 24V power, and the module is operational on the bus.

DIP switch to address mapping (8-position binary):

```
Module 1:  SW1=ON, rest OFF           → Address 1   (binary: 00000001)
Module 2:  SW2=ON, rest OFF           → Address 2   (binary: 00000010)
Module 3:  SW1+SW2=ON, rest OFF       → Address 3   (binary: 00000011)
Module 4:  SW3=ON, rest OFF           → Address 4   (binary: 00000100)
...
Module 10: SW2+SW4=ON                 → Address 10  (binary: 00001010)
```

### 13.2 Agristar Standard Address Map

Define once. Print on a label inside every panel door.

**Bus B — Relay Modules:**

| DIP Setting | Address | Standard Function | Watchdog |
|---|---|---|---|
| SW1 only | **1** | **Compressor stages** — DO1–DO8 = stages 1–8 | ON, 30s |
| SW2 only | **2** | **Fans + Heaters** — DO1–4 = evap fans, DO5–6 = cond fans, DO7–8 = heaters | ON, 30s |
| SW1 + SW2 | **3** | **Defrost + Aux** — DO1–2 = defrost, DO3–4 = dampers, DO5 = alarm, DO6–8 = spare | ON, 30s |
| SW3 only | **4** | **Expansion** (customer-specific, additional outputs) | Configurable |

**Bus D — VFD Drives:**

| VFD Keypad | Address | Standard Function |
|---|---|---|
| Param 51.22 = 1 | **1** | Evaporator fan VFD |
| Param 51.22 = 2 | **2** | Condenser fan VFD |
| Param 51.22 = 3 | **3** | Compressor VFD (if applicable) |
| Param 51.22 = 4–8 | **4–8** | Additional drives as needed |

**Bus C — EEV Controllers:**

| Controller DIP/Config | Address | Standard Function |
|---|---|---|
| 20 | **20** | Evaporator circuit 1 superheat |
| 21 | **21** | Evaporator circuit 2 superheat |
| 22 | **22** | Evaporator circuit 3 superheat |
| 23–28 | **23–28** | Additional circuits as needed |

### 13.3 VFD One-Time Configuration

Each ACS380 requires 7 parameters set once via the keypad (2 minutes per drive):

```
Parameter 51.21 = Modbus RTU          (fieldbus protocol)
Parameter 51.22 = [1–8]               (station address — unique per drive)
Parameter 51.23 = 19200               (baud rate)
Parameter 51.24 = Even                (parity — Modbus standard)
Parameter 51.25 = 0                   (comm timeout disabled — hold last speed)
Parameter 51.26 = 1                   (comm break action — warning + last speed)
Parameter 46.01 = 0                   (minimum speed — 0 Hz for linear scaling)
```

ABB supports saving all parameters to a **User Parameter Set** on one golden drive, then copying to all subsequent drives via the keypad. Set up one drive → save → load on each new drive → change only 51.22 (address).

### 13.4 Firmware Auto-Discovery

On boot, the AM2612 scans each bus to determine what's actually connected:

```c
void modbus_discover_devices(void) {
    // Bus B: scan relay modules at addresses 1–16
    for (uint8_t addr = 1; addr <= 16; addr++) {
        if (mb_read_coils(BUS_B, addr, 0, 1, &dummy) == MB_OK) {
            relay_modules[num_relay_modules++] = addr;
            log_info("Bus B: relay module found at address %d", addr);
        }
        // ~15ms timeout per non-responding address
    }

    // Bus D: scan VFDs at addresses 1–8
    for (uint8_t addr = 1; addr <= 8; addr++) {
        if (mb_read_registers(BUS_D, addr, 0x0003, 1, &dummy) == MB_OK) {
            vfd_drives[num_vfds++] = addr;
            log_info("Bus D: VFD drive found at address %d", addr);
        }
    }

    // Bus C: scan EEV controllers at addresses 20–28
    for (uint8_t addr = 20; addr <= 28; addr++) {
        if (mb_read_registers(BUS_C, addr, 0x0000, 1, &dummy) == MB_OK) {
            eev_controllers[num_eevs++] = addr;
            log_info("Bus C: EEV controller found at address %d", addr);
        }
    }
}
```

Total scan time: ~500ms at boot (33 addresses × ~15ms timeout per non-responding address). After discovery, the firmware only polls devices that actually responded. The UI shows discovered devices on the System → Devices page.

### 13.5 Three Commissioning Levels

| Level | Who | What They Do | Tools Needed |
|---|---|---|---|
| **Standard install** | Field technician | Set DIP switches per label in panel door, wire RS485 A/B + 24V power, wire contactors to relay outputs, set 7 VFD parameters per printed checklist | Screwdriver, VFD keypad |
| **Custom mapping** | Advanced technician | Change equipment-to-output assignment via UI IO Config Level 2 page | Web browser (RPi5 UI) |
| **Full custom** | Engineer | Define new equipment map, new control logic, custom firmware variant | Firmware build + upload |

**Level 1 covers 90%+ of installations.** A technician who has never worked with Modbus can commission a complete panel by following a one-page laminated guide:

```
AGRISTAR AM2612 COMMISSIONING CHECKLIST

□ Wire 24VDC power to all relay modules
□ Wire RS485 A/B from AM2612 Bus B terminal to all relay modules (daisy chain)
□ Wire RS485 A/B from AM2612 Bus D terminal to all VFDs (daisy chain)
□ Wire RS485 A/B from AM2612 Bus C terminal to all EEV controllers (daisy chain)
□ Set relay module DIP switches:
    Compressor module:  SW1 = ON (all others OFF)
    Fan/Heater module:  SW2 = ON (all others OFF)
    Defrost/Aux module: SW1 + SW2 = ON (all others OFF)
□ Set each VFD (7 parameters via keypad):
    51.21=Modbus RTU  51.22=[drive#]  51.23=19200
    51.24=Even  51.25=0  51.26=1  46.01=0
□ Power on AM2612
□ Open UI → System → Devices — verify all modules and drives show "Online"
□ Test each output from UI → Manual Override page
□ Verify DI inputs: trigger each safety switch, confirm status in UI
```

No laptop. No programming software. No Modbus configuration utility. DIP switches + VFD keypad + screwdriver.

### 13.6 Comparison with Current Commissioning

| Step | TM4C129 (CPLD) | AM2612 (Modbus RTU) |
|---|---|---|
| Wire outputs | 1 dedicated wire per output, homerun to controller board | 2 wires (A/B) daisy-chain to all modules |
| Configure module addresses | N/A (hardwired to board) | DIP switches (10 seconds per module) |
| Configure VFDs | RPi5 must be running + VFD server software configured | VFD keypad (2 min per drive) |
| Add more outputs | Order custom CPLD expansion board, wait for delivery, install, reconfigure firmware | Buy $30 Waveshare from Amazon, set DIP, daisy-chain to existing bus |
| Troubleshoot a failed output | Oscilloscope on CPLD shift register clock/data lines | Any $20 USB-to-RS485 adapter + free Modbus poll tool on laptop |
| Replace failed module | Replace entire CPLD board (custom part, lead time) | Swap Waveshare, set same DIP switch, done in 5 minutes |

---

## 14. IO Config Level 2 Integration — Same UI, New Backend

### 14.1 How It Works Today

The existing IO Config Level 2 page in the UI lets engineers assign equipment functions to physical outputs. Each assignment specifies:

- **Equipment** — dropdown: Compressor Stage 1, Evap Fan, Heater, Defrost 1, etc. (from `EQUIPMENT_IO` enum)
- **Board** — dropdown: 0 (main), 1–3 (expansion CPLD boards)
- **Output** — dropdown: 0–7 (bit position on that board)

The firmware stores this in `Settings.EquipIo[]`, which `OutputOn()`/`OutputOff()` uses to look up where to set the bit in `IoBoard[board].OutputState`.

### 14.2 What Changes

The IO Config Level 2 page keeps the same layout. Two label changes:

| Current UI Label | New UI Label | Values |
|---|---|---|
| "Board" | **"Module"** | 1–16 (Modbus address, set by DIP switch) |
| "Output" | "Output" | 1–8 (relay number on the module) |

The "Module" dropdown shows auto-discovered devices with their status:

```
Module 1 — Online (8 DO, 8 DI) — "Compressors"
Module 2 — Online (8 DO, 8 DI) — "Fans/Heaters"
Module 3 — Online (8 DO, 8 DI) — "Defrost/Aux"
Module 4 — Not detected
```

### 14.3 Settings Data Structure

```c
// Current (TM4C129)
typedef struct {
    uint8_t board;    // 0 = main, 1–3 = expansion CPLD boards
    uint8_t bit;      // 0–7 (output position on that board)
} EquipIoMapping;

// New (AM2612)
typedef struct {
    uint8_t unit_id;  // 1–16 (Modbus address, set by DIP switch on module)
    uint8_t coil;     // 0–7 (relay number on that module)
} EquipIoMapping;
```

Same two bytes per mapping. Same array size. Same save/load to flash. Same UI editing pattern. The `OutputOn()`/`OutputOff()` abstraction reads this table:

```c
void OutputOn(EQUIPMENT_IO equip) {
    EquipIoMapping *map = &Settings.EquipIo[equip];
    mb_write_coil(BUS_B, map->unit_id, map->coil, true);
}

void OutputOff(EQUIPMENT_IO equip) {
    EquipIoMapping *map = &Settings.EquipIo[equip];
    mb_write_coil(BUS_B, map->unit_id, map->coil, false);
}
```

### 14.4 Default Mapping

When the firmware boots with a fresh settings file (new installation or factory reset), it loads the standard template map matching the DIP switch convention from Section 13.2:

```c
static const EquipIoMapping DEFAULT_EQUIP_MAP[] = {
    // Module 1 (DIP: SW1) — Compressor stages
    [EQ_REFRIG_STAGE_1] = { .unit_id = 1, .coil = 0 },
    [EQ_REFRIG_STAGE_2] = { .unit_id = 1, .coil = 1 },
    [EQ_REFRIG_STAGE_3] = { .unit_id = 1, .coil = 2 },
    [EQ_REFRIG_STAGE_4] = { .unit_id = 1, .coil = 3 },
    [EQ_REFRIG_STAGE_5] = { .unit_id = 1, .coil = 4 },
    [EQ_REFRIG_STAGE_6] = { .unit_id = 1, .coil = 5 },
    [EQ_REFRIG_STAGE_7] = { .unit_id = 1, .coil = 6 },
    [EQ_REFRIG_STAGE_8] = { .unit_id = 1, .coil = 7 },

    // Module 2 (DIP: SW2) — Fans + Heaters
    [EQ_FAN]            = { .unit_id = 2, .coil = 0 },
    [EQ_EVAP_FAN_2]     = { .unit_id = 2, .coil = 1 },
    [EQ_EVAP_FAN_3]     = { .unit_id = 2, .coil = 2 },
    [EQ_EVAP_FAN_4]     = { .unit_id = 2, .coil = 3 },
    [EQ_COND_FAN_1]     = { .unit_id = 2, .coil = 4 },
    [EQ_COND_FAN_2]     = { .unit_id = 2, .coil = 5 },
    [EQ_HEAT]           = { .unit_id = 2, .coil = 6 },
    [EQ_HEAT_2]         = { .unit_id = 2, .coil = 7 },

    // Module 3 (DIP: SW1+SW2) — Defrost + Aux
    [EQ_DEFROST_1]      = { .unit_id = 3, .coil = 0 },
    [EQ_DEFROST_2]      = { .unit_id = 3, .coil = 1 },
    [EQ_DOORS]          = { .unit_id = 3, .coil = 2 },
    [EQ_DAMPER_1]       = { .unit_id = 3, .coil = 3 },
    [EQ_ALARM]          = { .unit_id = 3, .coil = 4 },
    [EQ_LIGHT]          = { .unit_id = 3, .coil = 5 },
    [EQ_AUX_1]          = { .unit_id = 3, .coil = 6 },
    [EQ_AUX_2]          = { .unit_id = 3, .coil = 7 },
};
```

If the field technician follows the standard DIP switch assignment and wiring template, **the IO Config Level 2 page is already correct out of the box.** The engineer opens it, verifies the defaults match the physical wiring, and saves. For non-standard jobs, they reassign any equipment to any module + output — no firmware change needed.

### 14.5 Commissioning Flow

1. **Field tech** wires relay modules, sets DIP switches per standard template (Section 13.2)
2. **AM2612 boots** → auto-discovers modules 1, 2, 3 online (Section 13.4)
3. **Engineer** opens UI → IO Config Level 2
4. Sees all equipment pre-mapped to the standard layout
5. Sees discovered modules in the Module dropdown (green = online, red = not found)
6. Verifies or adjusts assignments → saves
7. Tests via UI → Manual Override page (same as today — toggle each output, verify contactors click)
8. Verifies DI inputs — trigger each safety switch, confirm UI shows correct state

For a standard installation with 3 relay modules + 4 VFDs: steps 3–6 take under 5 minutes because the defaults are already correct.

---

## 15. MCU Selection — Ranked Comparison

### 15.1 What We Need

Based on the Agristar control architecture, the MCU must provide:

- **6+ UARTs** (debug, RPi5 bridge, analog boards, relay bus, EEV bus, VFD bus)
- **FreeRTOS support** (existing firmware is FreeRTOS — lowest porting friction)
- **Industrial temperature** (-40°C to +85°C minimum, +105°C or +125°C preferred)
- **256 KB+ SRAM** (512 KB–2 MB preferred for multi-zone)
- **Cortex-R or Cortex-M** (real-time deterministic, not Linux-class Cortex-A)
- **CAN** (future multi-board networking)
- **GPIO for RS485 direction control** (4 pins minimum)
- **Available OSPI/QSPI** (external flash for settings and firmware images)
- **Long-term availability** (10+ year production commitment — industrial, not consumer)

### 15.2 Candidates

#### Tier 1 — TI Sitara MCU+ Family (Same SDK, Easiest Port)

| Chip | Cores | Clock | SRAM | UARTs | PRU | CAN | Ethernet | Price | Availability |
|---|---|---|---|---|---|---|---|---|---|
| **AM2612** | 1× R5F | 400 MHz | 512 KB | 3–4 | No | 2× MCAN | Basic | ~$8–12 | Active |
| **AM2632** | 1× R5F | 400 MHz | 2 MB | **6** | **Yes** | 2× MCAN | CPSW | ~$12–16 | Active |
| **AM2634** | **2× R5F** | 400 MHz | 2 MB | **6** | **Yes** | 2× MCAN | CPSW | ~$15–20 | Active |
| **AM2432** | **2× R5F** | **800 MHz** | 2 MB | **9** | **Yes** | 2× MCAN | CPSW | ~$18–25 | Active |
| **AM2434** | **4× R5F** | **800 MHz** | 2 MB | **9** | **Yes** | 2× MCAN | CPSW 3-port | ~$22–30 | Active |
| **AM6442** | 2× R5F + 2× A53 | 1 GHz | 2 MB (+DDR) | **9** | **Yes** | 2× MCAN | Gigabit | ~$25–40 | Active |

#### Tier 2 — TI Cortex-M (Same Vendor, Moderate Port)

| Chip | Core | Clock | SRAM | UARTs | CAN | Ethernet | Price | Notes |
|---|---|---|---|---|---|---|---|---|
| **TM4C1294** | Cortex-M4F | 120 MHz | 256 KB | **8** | 2 | 10/100 | ~$12–15 | Current family — almost zero port effort, but same CPLD limitations |
| **MSPM0G3507** | Cortex-M0+ | 80 MHz | 32 KB | 4 | No | No | ~$2–4 | Too small — no FreeRTOS headroom, no CAN, not enough SRAM |

#### Tier 3 — STM32 (Different Vendor, FreeRTOS Native)

| Chip | Core | Clock | SRAM | UARTs | CAN | Ethernet | Price | Notes |
|---|---|---|---|---|---|---|---|---|
| **STM32H743** | Cortex-M7 | 480 MHz | 1 MB | **8** | 2× FDCAN | Gigabit | ~$12–18 | Excellent performance, huge ecosystem |
| **STM32H755** | M7 + M4 (dual) | 480/240 MHz | 1 MB | **8** | 2× FDCAN | Gigabit | ~$15–22 | Dual-core for separation of concerns |
| **STM32H7S3** | Cortex-M7 | 600 MHz | 620 KB | **8** | 2× FDCAN | Gigabit | ~$10–15 | Newest H7 generation |

#### Tier 4 — NXP (Different Vendor, FreeRTOS Native)

| Chip | Core | Clock | SRAM | UARTs | CAN | Ethernet | Price | Notes |
|---|---|---|---|---|---|---|---|---|
| **i.MX RT1062** | Cortex-M7 | 600 MHz | 1 MB | **8 (LPUART)** | 2× FlexCAN | 10/100 | ~$10–14 | Very fast MCU, NXP MCUXpresso SDK |
| **i.MX RT1176** | M7 + M4 (dual) | 1 GHz / 400 MHz | 2 MB | **12 (LPUART)** | 3× FlexCAN FD | Gigabit | ~$18–25 | Massive UART count, dual-core |
| **S32K344** | 2× Cortex-M7 | 160 MHz | 512 KB | 12 (LPUART) | 3× FlexCAN FD | No | ~$12–18 | Automotive-grade, ASIL-B, long lifecycle |

#### Tier 5 — Microchip (Different Vendor)

| Chip | Core | Clock | SRAM | UARTs | CAN | Ethernet | Price | Notes |
|---|---|---|---|---|---|---|---|---|
| **SAME70** | Cortex-M7 | 300 MHz | 384 KB | **5** | 2× MCAN | 10/100 | ~$10–15 | Atmel heritage, ASF4 drivers |
| **PIC32MZ** | MIPS M-class | 200 MHz | 512 KB | **6** | 2 | 10/100 | ~$8–12 | Not ARM — different architecture entirely |

### 15.3 Ranked by Porting Ease from TM4C129

**Scoring: 1 = drop-in, 5 = major rewrite**

| Rank | Chip | Port Effort | Why |
|---|---|---|---|
| **1** | **TM4C1294NCPDT** | ★☆☆☆☆ (trivial) | Same family. Same TivaWare SDK. Same pin names. Same UART driver API. Literally recompile with minor pin changes. But: same 120 MHz M4, same limitations that drove the switch. |
| **2** | **AM2632 / AM2634** | ★★☆☆☆ (easy) | Same vendor (TI). MCU+ SDK replaces TivaWare but API patterns are similar. FreeRTOS is identical. UART/GPIO/SPI drivers have same concepts, different function names. Pin mux via SysConfig GUI tool. **The sweet spot.** |
| **3** | **AM2432 / AM2434** | ★★☆☆☆ (easy) | Identical to AM2632/AM2634 port — same MCU+ SDK. Multi-core adds new capability but single-core mode works identically. Only extra effort: new linker script for larger memory map. |
| **4** | **STM32H743 / H755** | ★★★☆☆ (moderate) | Different vendor. Different SDK (STM32 HAL/LL via STM32CubeMX). FreeRTOS port is identical (same upstream). UART/GPIO drivers are conceptually the same but all function names change. CubeMX generates init code automatically. Need to learn STM32 HAL patterns. 2–3 weeks for an experienced developer. |
| **5** | **NXP i.MX RT1062** | ★★★☆☆ (moderate) | Different vendor. MCUXpresso SDK. FreeRTOS identical. LPUART drivers are different from TI UART but same concepts. NXP's SDK is well-documented. Similar 2–3 week learning curve. |
| **6** | **NXP i.MX RT1176** | ★★★☆☆ (moderate) | Same as RT1062 but dual-core adds complexity. Single-core mode available for initial bring-up. |
| **7** | **NXP S32K344** | ★★★☆☆ (moderate) | Same NXP ecosystem but automotive SDK (S32 Design Studio). More rigid safety architecture. |
| **8** | **AM6442** | ★★★★☆ (significant) | TI MCU+ SDK for R5F cores (easy), but A53 cores run Linux — dual-OS architecture is complex. Overkill unless you want to run Linux services directly on the controller. |
| **9** | **Microchip SAME70** | ★★★★☆ (significant) | Different vendor, different SDK (ASF4/START). Smaller community. UART count is borderline (5). Fewer code examples. |
| **10** | **PIC32MZ** | ★★★★★ (major) | MIPS architecture — not ARM. All assembly-level assumptions change. Different toolchain (XC32). FreeRTOS port exists but less maintained. Not recommended. |

### 15.4 Ranked by Features and Benefits

**Scoring: 5 stars = best for Agristar**

| Rank | Chip | Score | Key Benefits |
|---|---|---|---|
| **1** | **AM2434** | ★★★★★ | 4 cores, 9 UARTs, 2 MB SRAM, 800 MHz, PRU, CAN FD, Gigabit Ethernet. Runs 4+ zones, 30 compressors, unlimited expansion. 3 spare UARTs. Multi-core for dedicated safety or I/O processing. TI industrial lifecycle (15+ years). |
| **2** | **NXP i.MX RT1176** | ★★★★★ | 12 UARTs (most of any candidate), dual-core M7+M4, 2 MB SRAM, 1 GHz, Gigabit Ethernet, 3× CAN FD. Massive peripheral set. |
| **3** | **AM2432** | ★★★★☆ | Same as AM2434 but 2 cores instead of 4. Still 9 UARTs, 2 MB, 800 MHz. Plenty for 4 zones. |
| **4** | **STM32H755** | ★★★★☆ | Dual-core M7+M4, 1 MB SRAM, 8 UARTs, 480 MHz, massive STM32 ecosystem. Huge community, countless examples, CubeMX code generator. |
| **5** | **AM2634** | ★★★★☆ | 2 cores, 6 UARTs (exactly enough), 2 MB SRAM, PRU for extras. Direct TI port. |
| **6** | **NXP i.MX RT1062** | ★★★★☆ | 8 UARTs, 600 MHz M7, 1 MB SRAM, excellent real-time performance. Strong NXP ecosystem. |
| **7** | **STM32H743** | ★★★★☆ | 8 UARTs, 480 MHz M7, 1 MB SRAM. Single core keeps firmware simple. Enormous community. |
| **8** | **AM2632** | ★★★☆☆ | 6 UARTs (exact minimum), 2 MB SRAM, PRU available. Lowest-cost option that meets all requirements. |
| **9** | **NXP S32K344** | ★★★☆☆ | Automotive-grade reliability, ASIL-B lockstep. 12 UARTs. But no Ethernet — limits future BMS integration. |
| **10** | **AM2612** | ★★☆☆☆ | Only 3–4 UARTs — not enough without PRU or bus merging. No PRU. The cost savings vs AM2632 ($4–8) doesn't justify the limitation. |
| **11** | **TM4C1294** | ★★☆☆☆ | 8 UARTs is great, but 120 MHz M4 and 256 KB SRAM limits multi-zone. Same CPLD trap. Not a step forward. |
| **12** | **SAME70** | ★★☆☆☆ | Only 5 UARTs (not enough). Smaller ecosystem. Limited community support. |

### 15.5 Ranked by Functional Limitations

**What can't each chip do that Agristar might need?**

| Chip | Critical Limitations |
|---|---|
| **AM2612** | Only 3–4 UARTs — must merge buses or use workarounds. No PRU. Limits bus independence. |
| **TM4C1294** | 120 MHz / 256 KB — limits multi-zone to 2 max. Same CPLD architecture problem. No PRU. No OSPI. |
| **AM2632** | 6 UARTs is exact minimum — zero spare channels. One core only. If a future feature needs another serial bus, you're stuck. |
| **AM2634** | 6 UARTs — same zero-spare situation. But 2 cores give CPU headroom. |
| **AM2432** | 2 cores — enough but not ideal for 4-zone core-per-zone architecture. |
| **AM2434** | **No meaningful limitations for Agristar.** 9 UARTs (3 spare), 4 cores, 2 MB SRAM, PRU, CAN FD, Gigabit Ethernet. Cost is ~$22–30 but the savings elsewhere dwarf this. |
| **STM32H743** | No PRU — can't offload custom protocols. Single core. 1 MB SRAM (vs 2 MB on TI). No TI SysConfig pin mux tool. Different vendor = new learning curve. |
| **STM32H755** | Same as H743 limitations except dual-core. The M4 subsystem has limited peripheral access — not as flexible as dual R5F. |
| **NXP RT1062** | 10/100 Ethernet only (no Gigabit). Single core. Different vendor ecosystem. |
| **NXP RT1176** | 12 UARTs is excellent but different SDK ecosystem. NXP's documentation is good but not as mature as TI or STM32 for industrial FreeRTOS. |
| **NXP S32K344** | **No Ethernet** — can't do Modbus TCP gateway, can't do Ethernet bridge to RPi5 (future upgrade). Automotive focus means industrial I/O examples are scarce. |
| **SAME70** | Only 5 UARTs. Smaller community. Microchip's industrial MCU support is less comprehensive than TI or ST. |

### 15.6 Recommendation Matrix

| If Your Priority Is... | Choose | Why |
|---|---|---|
| **Lowest risk, fastest port** | **AM2634** | Same TI SDK, 6 UARTs (just enough), 2 cores, 2 MB. Direct port from TM4C129 with minimal learning. |
| **Maximum headroom, still easy port** | **AM2434** | Same TI SDK, 9 UARTs (3 spare), 4 cores, 2 MB, PRU. Future-proof for multi-zone and multi-board. ~$10 more than AM2634. |
| **Lowest cost that works** | **AM2632** | Same TI SDK, 6 UARTs (exact minimum), 1 core, 2 MB. ~$12–16. No extras but meets all current requirements. |
| **Best non-TI alternative** | **STM32H755** | Huge ecosystem, dual-core, 8 UARTs, 1 MB SRAM. 2–3 week learning curve for STM32 HAL. Strongest community of any MCU vendor. |
| **Maximum UART count** | **NXP i.MX RT1176** | 12 UARTs, dual-core, 2 MB, 1 GHz. If you ever need >6 serial buses, this is the chip. |
| **Automotive-grade safety** | **NXP S32K344** | ASIL-B lockstep, 12 UARTs, but no Ethernet. Only if a customer mandates functional safety certification. |

### 15.7 Final Recommendation

**Primary: TI AM2434** (~$22–30)

- 4× R5F @ 800 MHz — run each zone on its own core, or use 1 core with 75% headroom
- 9 UARTs — 6 allocated + 3 spare for future expansion
- 2 MB SRAM — room for extensive data logging and multi-zone state
- PRU-ICSS — offload custom protocols or add software UARTs if ever needed
- 2× MCAN (CAN FD) — multi-board networking future
- CPSW Gigabit Ethernet — future RPi5 Ethernet bridge and BMS integration
- Same MCU+ SDK as AM2612/AM2632/AM2634 — firmware ports identically
- TI industrial lifecycle — 15+ year production commitment

**Fallback: TI AM2634** (~$15–20)

- If the AM2434 is overkill or cost-sensitive, the AM2634 meets all current requirements with 6 UARTs, 2 cores, 2 MB SRAM. Same SDK — code compiles without changes. Limits: zero spare UARTs, 2 cores instead of 4.

**Alternative: STM32H755** (~$15–22)

- If TI supply chain becomes an issue, STM32 is the safest alternative. Largest MCU community in the world. Dual-core M7+M4, 8 UARTs, 1 MB SRAM. Requires 2–3 weeks to port (different HAL, same FreeRTOS). CubeMX generates all peripheral init code.

The $10–15 difference between the AM2632 ($12) and the AM2434 ($25) is irrelevant on a controller board that saves $800+ per installation by eliminating FMBT-21 adapters and CPLD expansion boards. Buy the headroom.

---

## 16. Reference Design — Twin-Panel Cold Storage (2 Rooms, 2 Compressors Each)

This section details a complete system architecture for a twin cold-storage facility using one AM2434 AS2 board, Agristar-style analog boards for sensors, and Modbus RTU for everything else. This is a real-world representative installation — the kind Agristar builds today.

### 16.1 System Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                    MAIN CONTROL PANEL                               │
│                                                                     │
│  ┌──────────┐   UART1    ┌──────────┐                              │
│  │  AM2434  │◄──230400──►│   RPi5   │◄── Ethernet ── UI / Cloud   │
│  │  AS2     │            └──────────┘                              │
│  │  Board   │                                                       │
│  │          │── UART0 ── Debug (115200)                             │
│  │          │── UART2 ── RS485-A: Analog boards (9600, Gellert)    │
│  │          │── UART3 ── RS485-B: Relay + DI modules (19200, RTU)  │
│  │          │── UART4 ── RS485-C: VFDs (19200, Modbus RTU)         │
│  │          │── UART5 ── Spare (future EEV bus or expansion)       │
│  └──────────┘                                                       │
└─────────────────────────────────────────────────────────────────────┘
```

**One AS2 board controls both rooms.** No second controller needed.

### 16.2 I/O Summary

| | Cold Storage A | Cold Storage B | **Total** |
|---|---|---|---|
| Room equipment DO | 8 | 8 | **16** |
| Room equipment DI | 8 | 8 | **16** |
| Compressor 1 DO | 8 | 8 | **16** |
| Compressor 1 DI | 8 | 8 | **16** |
| Compressor 2 DO | 8 | 8 | **16** |
| Compressor 2 DI | 8 | 8 | **16** |
| Ventilation fan VFDs | 4 | 4 | **8** |
| Analog sensors | 8–12 | 8–12 | **16–24** |
| **Total field devices** | | | **12 Modbus modules + 8 VFDs + 4–6 analog boards** |

### 16.3 Bus Architecture — Three RS485 Buses

#### Bus A — Analog Sensors (UART2, 9600 baud, Gellert Protocol)

This is the existing AS2 analog board architecture — **unchanged from the current product**. Custom Agristar analog boards with on-board 10-bit ADC, 4 channels each, polled via the proprietary Gellert master-slave protocol (0x7E framing, RS485_QRY_SENSORS command).

| Board | DIP addr | Ch 1 | Ch 2 | Ch 3 | Ch 4 |
|---|---|---|---|---|---|
| AB-1 | 0 | A: Suction temp (NTC) | A: Discharge temp (NTC) | A: Return air temp (NTC) | A: Supply/plenum temp (NTC) |
| AB-2 | 1 | A: Suction pressure (4-20mA) | A: Head pressure (4-20mA) | A: Liquid line temp (NTC) | A: Ambient temp (NTC) |
| AB-3 | 2 | A: Humidity (4-20mA) | A: CO2 (4-20mA) | A: Spare | A: Spare |
| AB-4 | 3 | B: Suction temp (NTC) | B: Discharge temp (NTC) | B: Return air temp (NTC) | B: Supply/plenum temp (NTC) |
| AB-5 | 4 | B: Suction pressure (4-20mA) | B: Head pressure (4-20mA) | B: Liquid line temp (NTC) | B: Ambient temp (NTC) |
| AB-6 | 5 | B: Humidity (4-20mA) | B: CO2 (4-20mA) | B: Spare | B: Spare |

**6 analog boards × 4 channels = 24 sensor inputs.** Polled in groups of 2 per 250 ms tick (existing ReadAnalogBoards() pattern). Full scan of all 6 boards = 750 ms.

**Bus utilization:** 6 boards ÷ 2 per group × 250 ms = 3 poll cycles/sec. At 9600 baud with ~20-byte packets, each transaction ≈ 25 ms. 3 × 25 ms = 75 ms/sec = **7.5%** — well within capacity.

#### Bus B — Relay and DI Modules (UART3, 19200 baud, Modbus RTU)

All digital outputs (contactors, solenoid valves, heaters) and digital inputs (status contacts, pressure switches, safety switches) run on one Modbus RTU bus.

**Cold Storage A modules:**

| Modbus addr | DIP | Module | Type |
|---|---|---|---|
| **1** | 00001 | 8-ch DO — Room A equipment | Relay output |
| **2** | 00010 | 8-ch DI — Room A equipment | Discrete input |
| **3** | 00011 | 8-ch DO — A Compressor 1 | Relay output |
| **4** | 00100 | 8-ch DI — A Compressor 1 | Discrete input |
| **5** | 00101 | 8-ch DO — A Compressor 2 | Relay output |
| **6** | 00110 | 8-ch DI — A Compressor 2 | Discrete input |

**Cold Storage B modules:**

| Modbus addr | DIP | Module | Type |
|---|---|---|---|
| **7** | 00111 | 8-ch DO — Room B equipment | Relay output |
| **8** | 01000 | 8-ch DI — Room B equipment | Discrete input |
| **9** | 01001 | 8-ch DO — B Compressor 1 | Relay output |
| **10** | 01010 | 8-ch DI — B Compressor 1 | Discrete input |
| **11** | 01011 | 8-ch DO — B Compressor 2 | Relay output |
| **12** | 01100 | 8-ch DI — B Compressor 2 | Discrete input |

**12 modules total** on one bus — comfortably under the 32-device RS485 limit.

**Bus utilization:** 12 modules × 1 poll each / 5 sec = 2.4 trans/sec × 15 ms = 36 ms/sec = **3.6%** utilization. Immediate commands (compressor start/stop) add < 1 trans/sec.

#### Bus C — VFDs (UART4, 19200 baud, Modbus RTU)

Ventilation/evaporator fan VFDs on a dedicated bus. Isolated from relay traffic so a VFD communication fault never delays a compressor safety command.
| Modbus addr | DIP/keypad | Assignment |
|---|---|---|
| **1** | P1 | A: Evap fan 1 |
| **2** | P2 | A: Evap fan 2 |
| **3** | P3 | A: Evap fan 3 |
| **4** | P4 | A: Evap fan 4 |
| **5** | P5 | B: Evap fan 1 |
| **6** | P6 | B: Evap fan 2 |
| **7** | P7 | B: Evap fan 3 |
| **8** | P8 | B: Evap fan 4 |

**8 VFDs — all on one bus.** Parameter 51.25 = 0 (timeout disabled), poll at 120 s for display. Speed commands via xQueueReceive on demand.

**Bus utilization:** 8 VFDs × 1 poll / 120 s = 0.067 trans/sec. Even with speed changes every few seconds: < **1%** utilization.

### 16.4 Digital Output Assignment — What Each Point Controls

#### Room Equipment DO (8 per room) — Addresses 1, 7

| Point | Function | Contactor | Watchdog |
|---|---|---|---|
| DO-1 | Defrost heater contactor | 25A 3-pole | **Yes (30 s)** |
| DO-2 | Defrost termination solenoid | — | Yes (30 s) |
| DO-3 | Drain pan heater | 10A | Yes (30 s) |
| DO-4 | Room light relay | 10A | No |
| DO-5 | Alarm horn | — | No |
| DO-6 | Economizer damper actuator | — | No |
| DO-7 | Humidifier relay | — | No |
| DO-8 | Spare | — | No |

#### Compressor DO (8 per compressor) — Addresses 3, 5, 9, 11

| Point | Function | Contactor | Watchdog |
|---|---|---|---|
| DO-1 | Compressor contactor | 40A+ 3-pole | **Yes (30 s)** |
| DO-2 | Unloader solenoid (capacity control) | — | Yes (30 s) |
| DO-3 | Crankcase heater relay | 5A | No (always on when comp off) |
| DO-4 | Condenser fan contactor | 15A | Yes (30 s) |
| DO-5 | Liquid line solenoid valve | — | **Yes (30 s)** |
| DO-6 | Hot gas bypass solenoid | — | Yes (30 s) |
| DO-7 | Oil pump relay (if applicable) | 5A | Yes (30 s) |
| DO-8 | Alarm/fault relay | — | No |

**Watchdog logic:** If the AS2 stops polling a module for > 30 seconds, all watchdog-enabled outputs on that module de-energize. Compressor contactor drops → compressor stops → safe state. Non-watchdog outputs (lights, alarms, crankcase heaters) stay in last state.

### 16.5 Digital Input Assignment — What Each Point Reads

#### Room Equipment DI (8 per room) — Addresses 2, 8

| Point | Function | Normal state | Alarm on |
|---|---|---|---|
| DI-1 | Door switch | Closed (door shut) | Open (door open > 5 min) |
| DI-2 | Defrost termination thermostat | Closed (below setpoint) | Open (temp reached) |
| DI-3 | Drain pan overflow float | Open (normal) | Closed (overflow) |
| DI-4 | High temperature alarm | Open (normal) | Closed (high temp) |
| DI-5 | Low temperature alarm | Open (normal) | Closed (low temp) |
| DI-6 | Smoke detector input | Open (normal) | Closed (smoke) |
| DI-7 | Emergency stop | Closed (normal) | Open (E-stop pressed) |
| DI-8 | Spare | — | — |

#### Compressor DI (8 per compressor) — Addresses 4, 6, 10, 12

| Point | Function | Normal state | Alarm on |
|---|---|---|---|
| DI-1 | Compressor run feedback (aux contact) | Matches command | Mismatch > 5 s |
| DI-2 | High pressure cutout | Closed (normal) | Open (tripped) |
| DI-3 | Low pressure cutout | Closed (normal) | Open (tripped) |
| DI-4 | Oil pressure differential switch | Closed (normal) | Open (low oil) |
| DI-5 | Motor overload / thermal | Closed (normal) | Open (tripped) |
| DI-6 | Phase monitor relay | Closed (3-phase OK) | Open (phase loss/reversal) |
| DI-7 | Anti-recycle timer status | Closed (ready) | Open (timing) |
| DI-8 | Oil level switch | Closed (normal) | Open (low oil level) |

### 16.6 Analog Sensor Assignment

| Sensor | Type | Range | Purpose | Poll rate |
|---|---|---|---|---|
| Suction temp | NTC thermistor | -40 to +50°C | Superheat calc, low temp protection | 250 ms |
| Discharge temp | NTC thermistor | 0 to +150°C | High discharge alarm, oil degradation | 250 ms |
| Return air temp | NTC thermistor | -40 to +50°C | Room temperature control (primary) | 250 ms |
| Supply/plenum temp | NTC thermistor | -40 to +50°C | Coil performance, defrost termination | 250 ms |
| Liquid line temp | NTC thermistor | -10 to +60°C | Subcooling calc, TXV performance | 1 s |
| Ambient temp | NTC thermistor | -40 to +55°C | Head pressure control, condenser fan staging | 1 s |
| Suction pressure | 4-20mA xducer | 0–150 psig | Superheat calc, low pressure protection | 250 ms |
| Head pressure | 4-20mA xducer | 0–500 psig | High pressure protection, subcooling calc | 250 ms |
| Humidity | 4-20mA | 0–100% RH | Storage quality (produce) | 5 s |
| CO2 | 4-20mA | 0–10,000 ppm | Controlled atmosphere (if applicable) | 5 s |

**Per room: 6 NTC + 2 pressure + 2 optional (humidity/CO2) = 8–10 analog channels = 2–3 AS2 analog boards.**

### 16.7 Wire Run Summary

| Run | Wire | Max length | Termination |
|---|---|---|---|
| Panel → Room A relay/DI modules | Belden 9841 (shielded twisted pair) | 1,200 m | 120Ω A-to-B at last module |
| Panel → Room B relay/DI modules | Same cable, daisy-chain continues from Room A | Same bus | 120Ω at last B module |
| Panel → VFDs (both rooms) | Separate Belden 9841 | 1,200 m | 120Ω at last VFD |
| Panel → Analog boards (both rooms) | Same Belden 9841 | 1,200 m | 120Ω at last analog board |
| Sensor → Analog board | 18 AWG shielded, < 30 m per sensor | 30 m (recommended) | Shield grounded at analog board end only |

**Three 2-wire RS485 runs leave the panel.** Each daisy-chains through both rooms. Total panel junction: 3 × RS485 + 1 × debug + 1 × RPi5 = 5 terminal blocks on the AS2 board.

### 16.8 Physical Wiring — Daisy-Chain Topology

```
PANEL                    ROOM A                           ROOM B
┌──────┐               ┌───────────────────┐            ┌───────────────────┐
│      │  Bus B (RTU)  │                   │            │                   │
│ UART3├──A/B──┬─►[DO-1]─►[DI-2]─►[DO-3]─►[DI-4]─►[DO-5]─►[DI-6]──►...  │
│      │       │       │  Addr 1   Addr 2  │ Addr 3     │   Addr 7  etc    │
│      │     120Ω      │                   │            │          ...──►[DI-12]
│      │  (panel end)  │                   │            │               120Ω│
│      │               └───────────────────┘            └───────────────────┘
│      │  Bus C (VFD)
│ UART4├──A/B──┬─►[VFD-1]─►[VFD-2]─►[VFD-3]─►[VFD-4]─►[VFD-5]─►...─►[VFD-8]
│      │     120Ω                                                        120Ω
│      │
│      │  Bus A (Analog)
│ UART2├──A/B──┬─►[AB-1]─►[AB-2]─►[AB-3]─►[AB-4]─►[AB-5]─►[AB-6]
│      │     120Ω                                              120Ω
└──────┘
```

**Every bus:** 120Ω termination resistor across A-to-B at both physical ends (panel and last device).

### 16.9 FreeRTOS Task Map (R5F-0)

All control logic runs on a single R5F core with FreeRTOS. Tasks are not per-zone — they are per-function:

| Task | Priority | Stack | Rate | Description |
|---|---|---|---|---|
| AnalogPoll | 5 (high) | 512 B | 250 ms | Read AS2 analog boards (UART2), group-of-2 pattern. Convert NTC/4-20mA to engineering units. Update sensor[] arrays. |
| ModbusPoll | 4 | 1 KB | 5 s | Read all 12 DI/DO modules on Bus B (UART3). Update di_state[] and verify do_state[] matches commanded outputs. Stuck output detection. |
| VfdPoll | 3 | 512 B | 120 s | Read 8 VFD status registers on Bus C (UART4). Update vfd_status[] for UI display. |
| ControlLoop_A | 4 | 2 KB | 1 s | Room A refrigeration logic: suction pressure control, compressor staging (lead/lag), defrost scheduling, fan staging, alarm evaluation. Writes to command queues. |
| ControlLoop_B | 4 | 2 KB | 1 s | Room B — identical logic, separate state struct. |
| CommandWriter | 5 (high) | 512 B | Event-driven | xQueueReceive from ControlLoop tasks. Sends Modbus write commands (DO state, VFD speed) to Bus B and Bus C. Priority ensures immediate commands (compressor trip, E-stop) aren't blocked by polling. |
| Rpi5Bridge | 3 | 2 KB | On-demand | UART1 packet handler. Receives UI commands, serves sensor/status data to RPi5. |
| Watchdog | 6 (highest) | 256 B | 1 s | Monitors heartbeats from all tasks. If any critical task misses 3 beats → force safe-state on all watchdog-enabled outputs. Runs on R5F-2 as hardware backup. |
| SafetyMonitor | 5 (high) | 512 B | 100 ms | Checks DI inputs for safety trips (HP cutout, E-stop, phase loss). Bypasses ControlLoop — sends immediate STOP to CommandWriter queue. Latches alarm until manual reset. |

**Total SRAM usage:** ~10 KB stacks + ~8 KB state structs + ~4 KB Modbus buffers + ~2 KB sensor arrays = **~24 KB** out of 2 MB available.

### 16.10 Compressor Control Logic

Each compressor is controlled by its own state machine inside the ControlLoop task:

```
                    ┌─────────┐
          ┌────────►│  IDLE   │◄──── Manual OFF or alarm lockout
          │         └────┬────┘
          │              │ Call for cooling (return air > setpoint + deadband)
          │         ┌────▼────┐
          │         │ PRE-RUN │ Crankcase heater off, oil pump on (if applicable)
          │         │  (30 s) │ Check: phase monitor OK, anti-recycle complete
          │         └────┬────┘
          │              │ All pre-checks pass
          │         ┌────▼────┐
          │         │ STARTING│ Compressor contactor ON, liquid solenoid ON
          │         │  (10 s) │ Wait for run feedback (DI-1)
          │         └────┬────┘
          │              │ Run feedback confirmed
          │         ┌────▼────┐
          │         │ RUNNING │ Normal operation — modulate unloaders/capacity
          │         │         │ Monitor: HP, LP, oil, discharge temp, motor thermal
          │         └────┬────┘
          │              │ Setpoint satisfied OR safety trip
          │         ┌────▼─────┐
          │         │ STOPPING │ Compressor OFF, liquid solenoid OFF
          │         │  pump-   │ Condenser fan: run 60 s for head pressure equalization
          │         │  down    │ Crankcase heater ON
          │         └────┬─────┘
          │              │ Anti-recycle timer starts (5 min)
          └──────────────┘
```

**Lead/lag staging:** Room A runs Compressor 1 as lead, Compressor 2 as lag. If lead runs > 15 min and suction pressure still above setpoint → start lag. If load drops → stop lag first. Lead/lag roles rotate on a configurable schedule (weekly or per defrost cycle) to equalize run hours.

### 16.11 Defrost Scheduling

Each room runs independent defrost cycles:

1. **Initiate** — Timer or demand (frost sensor on return air delta-T)
2. **Fan off** — All 4 evap fan VFDs → 0%
3. **Compressor off** — Both compressors for this room → IDLE (liquid solenoid OFF first, pump-down)
4. **Heater on** — Defrost heater contactor (DO-1 on room module)
5. **Terminate** — Defrost termination thermostat (DI-2) opens OR max timer (30 min failsafe)
6. **Drain time** — Heater off, drain pan heater on, wait 5 min for melt water to clear
7. **Fan restart** — Ramp fans to 30% for 2 min (prevent thermal shock), then normal
8. **Compressor restart** — Resume normal control, anti-recycle timer permitting

**Defrost does NOT cross rooms.** Room A can defrost while Room B runs normally.

### 16.12 VFD Fan Control

Each room's 4 evaporator fans are staged for energy savings:

| Condition | Fan 1 | Fan 2 | Fan 3 | Fan 4 |
|---|---|---|---|---|
| Light load (return air near setpoint) | 40% | OFF | OFF | OFF |
| Medium load | 60% | 60% | OFF | OFF |
| Heavy load | 80% | 80% | 80% | OFF |
| Maximum demand | 100% | 100% | 100% | 100% |
| Defrost | 0% | 0% | 0% | 0% |
| Post-defrost (2 min) | 30% | 30% | 30% | 30% |

**Speed commands flow:** ControlLoop_A/B calculates target speed → pushes {addr, speed%} to CommandWriter queue → CommandWriter sends Modbus write to VFD → VFD ramps internally.

### 16.13 Bill of Materials — Field Devices

| Qty | Device | Modbus addr | Unit cost | Total |
|---|---|---|---|---|
| 6 | Waveshare 8-ch DO relay module (RS485) | 1, 3, 5, 7, 9, 11 | ~$30 | $180 |
| 6 | Waveshare 8-ch DI module (RS485) | 2, 4, 6, 8, 10, 12 | ~$25 | $150 |
| 8 | ABB ACS380 VFD (fan-rated, 1–5 HP) | 1–8 (Bus C) | ~$400 | $3,200 |
| 6 | AS2 analog board (4-ch, Agristar) | 0–5 (Bus A) | ~$40 | $240 |
| 1 | AM2434 AS2 board | — | ~$200 | $200 |
| 1 | RPi5 (8 GB) | — | ~$80 | $80 |
| 12 | Contactors (25A–40A, per compressor/heater sizing) | — | ~$30 | $360 |
| ~20 | NTC/4-20mA sensors | — | ~$15 | $300 |
| 3 | Belden 9841 spools (RS485, 500 ft each) | — | ~$120 | $360 |
| 3 | 120Ω termination resistors (panel end) | — | ~$0.10 | $0.30 |
| **Total field I/O + wiring** | | | | **~$5,070** |

### 16.14 Panel Door Label — Standard Address Map

Print this on a laminated label inside the panel door:

```
═══════════════════════════════════════════════════════════
  AGRISTAR AS2 — TWIN COLD STORAGE — ADDRESS MAP
═══════════════════════════════════════════════════════════
  BUS A — ANALOG (UART2, 9600, Gellert Protocol)
  ─────────────────────────────────────────────────
  Addr 0: Room A temps (suction, discharge, return, supply)
  Addr 1: Room A pressures (suction, head, liquid, ambient)
  Addr 2: Room A environment (humidity, CO2, spare, spare)
  Addr 3: Room B temps (suction, discharge, return, supply)
  Addr 4: Room B pressures (suction, head, liquid, ambient)
  Addr 5: Room B environment (humidity, CO2, spare, spare)

  BUS B — RELAY / DI (UART3, 19200, Modbus RTU)
  ─────────────────────────────────────────────────
  Addr  1: Room A equipment — 8 DO (defrost, drain, light, alarm...)
  Addr  2: Room A equipment — 8 DI (door, overflow, E-stop...)
  Addr  3: Room A Comp 1   — 8 DO (contactor, unloader, condenser...)
  Addr  4: Room A Comp 1   — 8 DI (run FB, HP, LP, oil, thermal...)
  Addr  5: Room A Comp 2   — 8 DO
  Addr  6: Room A Comp 2   — 8 DI
  Addr  7: Room B equipment — 8 DO
  Addr  8: Room B equipment — 8 DI
  Addr  9: Room B Comp 1   — 8 DO
  Addr 10: Room B Comp 1   — 8 DI
  Addr 11: Room B Comp 2   — 8 DO
  Addr 12: Room B Comp 2   — 8 DI

  BUS C — VFDs (UART4, 19200, Modbus RTU)
  ─────────────────────────────────────────────────
  Addr 1: Room A evap fan 1    Addr 5: Room B evap fan 1
  Addr 2: Room A evap fan 2    Addr 6: Room B evap fan 2
  Addr 3: Room A evap fan 3    Addr 7: Room B evap fan 3
  Addr 4: Room A evap fan 4    Addr 8: Room B evap fan 4
═══════════════════════════════════════════════════════════
```

### 16.15 Commissioning Sequence

1. **Mount AS2 board + RPi5 in panel.** Connect power, Ethernet, debug UART.
2. **Set DIP switches** on all 12 relay/DI modules per address map. Daisy-chain Bus B through both rooms. Terminate both ends.
3. **Set VFD addresses** (keypad Parameter 53.01 = 1–8). Daisy-chain Bus C. Terminate both ends.
4. **Set analog board DIP addresses** (0–5). Daisy-chain Bus A. Terminate both ends.
5. **Power on AS2.** Boot sequence:
   - R5F-0 starts FreeRTOS
   - `FindAnalogBoards()` scans Bus A addresses 0–31, discovers 6 boards (~500 ms)
   - Modbus discovery scans Bus B addresses 1–12 (~200 ms)
   - Modbus discovery scans Bus C addresses 1–8 (~130 ms)
   - Total boot-to-ready: **< 2 seconds**
6. **Verify on RPi5 UI:** All 12 modules show green, all 8 VFDs show online, all 6 analog boards reporting temperatures.
7. **Run compressors one at a time.** Verify DI-1 (run feedback) matches command. Verify HP/LP cutout inputs respond to manual trip.
8. **Ramp each VFD.** Send 50% from UI, confirm fan spins at correct speed.
9. **Force a defrost.** Verify sequence: fans stop → compressors pump down → heater on → termination → drain → fan restart → compressor restart.
10. **Pull power on AS2.** Verify: all watchdog-enabled outputs drop within 30 s, compressors stop, fans coast down, crankcase heaters remain on (non-watchdog). **Power-loss safe-state confirmed.**

### 16.16 Scaling

This architecture scales without hardware changes to the AS2 board:

| Change | What to do |
|---|---|
| Add a 3rd cold storage room | Add 3 more DO modules, 3 more DI modules (addresses 13–18), 4 more VFDs (addresses 9–12), 3 more analog boards (addresses 6–8). Same buses, same firmware — just configure Zone C in the UI. |
| Add EEV control | Connect Carel/Danfoss EEV controllers to UART5 (spare bus). Add EEV poll task. |
| Bigger compressors | Same I/O — just use larger contactors. The relay module signals the contactor coil, not the motor directly. |
| More sensors | Add analog boards to Bus A (up to 32 boards = 128 sensors). |
| 3rd compressor per room | Add 2 more modules per room (1 DO + 1 DI). Addresses 19–22. Update lead/lag to lead/lag/standby rotation. |

---

## 17. On-Board RS485 Hub — Star Topology for Analog Buses

### 17.1 The Problem

RS485 is daisy-chain only — star topology creates impedance stubs that corrupt signals. For multi-room systems where 4 cold storage rooms radiate in different directions from the panel, daisy-chaining forces long cable runs that loop back and forth between rooms. Worse, a cable fault anywhere on a single daisy-chain kills sensor data for ALL rooms.

### 17.2 The Solution — On-Board Multiplexed RS485

Build a 4-port RS485 hub directly onto the AS2 board using 4 separate transceiver ICs and an analog MUX. One UART drives 4 electrically independent RS485 segments. Each segment gets its own cable run, its own termination, and its own fault domain.

### 17.3 Circuit Design

```
                          AM2434 AS2 Board
┌─────────────────────────────────────────────────────────────────────┐
│                                                                     │
│                         ┌──────────┐                               │
│   UART2 TX ─────────┬──┤ MAX3485  ├── A1/B1 ──► Room A (6 boards) │
│                      │  │ #1       │     120Ω (remote end)         │
│                      │  └──────────┘                               │
│                      │  ┌──────────┐                               │
│                      ├──┤ MAX3485  ├── A2/B2 ──► Room B (6 boards) │
│                      │  │ #2       │     120Ω (remote end)         │
│                      │  └──────────┘                               │
│                      │  ┌──────────┐                               │
│                      ├──┤ MAX3485  ├── A3/B3 ──► Room C (6 boards) │
│                      │  │ #3       │     120Ω (remote end)         │
│                      │  └──────────┘                               │
│                      │  ┌──────────┐                               │
│                      └──┤ MAX3485  ├── A4/B4 ──► Room D (6 boards) │
│                         │ #4       │     120Ω (remote end)         │
│                         └──────────┘                               │
│                                                                     │
│                         ┌──────────┐                               │
│                    RX1──┤          │                               │
│                    RX2──┤ 74HC4052 ├──► UART2 RX                   │
│                    RX3──┤  MUX     │                               │
│                    RX4──┤          │                               │
│                         └────┬─────┘                               │
│                              │                                     │
│               GPIO_A ────► SEL0                                    │
│               GPIO_B ────► SEL1                                    │
│                                                                     │
│   GPIO_DE ──────────┬── DE/RE on MAX3485 #1                       │
│                      ├── DE/RE on MAX3485 #2                       │
│                      ├── DE/RE on MAX3485 #3                       │
│                      └── DE/RE on MAX3485 #4                       │
│                                                                     │
│   120Ω termination on-board for each segment (panel end)           │
└─────────────────────────────────────────────────────────────────────┘
```

### 17.4 How It Works

**TX path:** All 4 MAX3485 transceivers receive the same UART TX signal. When the firmware sends a query (e.g., `RS485_QRY_SENSORS` to board address 3), the packet is broadcast on all 4 segments simultaneously. Only the addressed board responds — all other boards on all segments ignore it. This is standard master-slave behavior in both the Gellert protocol and Modbus RTU.

**RX path:** The 74HC4052 analog MUX connects only one segment's RX line to the UART at a time. Before sending a query, firmware sets 2 GPIO select lines to route the correct segment's response to the UART RX pin.

**DE/RE control:** All 4 transceivers share a single GPIO for driver enable. They all switch between transmit and receive mode together. Since only one slave responds at a time, there's no contention.

**Termination:** Each segment has a 120Ω resistor on the board (panel end) and a 120Ω resistor at the last device in the room (remote end). Four independent, properly terminated segments.

### 17.5 Firmware Changes

The modification to the existing analog polling code is minimal:

```c
// Segment lookup table — populated at discovery or from IO config
// board_segment[addr] = 0..3 (which RS485 segment this board is on)
static uint8_t board_segment[MAX_ANALOG_BOARDS];  // 32 entries

static void select_segment(uint8_t board_addr) {
    uint8_t seg = board_segment[board_addr];
    GPIO_write(MUX_SEL0, seg & 0x01);
    GPIO_write(MUX_SEL1, (seg >> 1) & 0x01);
    // MUX propagation delay: < 10 ns — no wait needed
}

// Modified ReadSensors — only change is one line before the query
int ReadSensors(int Board) {
    select_segment(Board);          // ← NEW: route RX to correct segment
    SendPacket(Board, RS485_QRY_SENSORS);
    return WaitForResponse(Board);
}
```

**Auto-discovery works unchanged.** `FindAnalogBoards()` already scans addresses 0–31. For each address, it tries all 4 segments (set MUX, send query, check response). The discovered segment is stored in `board_segment[]`. Total discovery scan: ~2 seconds for all 4 segments × 32 addresses.

### 17.6 BOM Addition

| Part | Qty | Package | Unit cost | Total |
|---|---|---|---|---|
| MAX3485ESA (3.3V RS485 transceiver) | 4 | SO-8 | $0.50 | $2.00 |
| 74HC4052D (dual 4:1 analog MUX) | 1 | SO-16 | $0.30 | $0.30 |
| 120Ω 1% termination resistor | 4 | 0402 | $0.05 | $0.20 |
| 100nF decoupling cap (per transceiver) | 4 | 0402 | $0.02 | $0.08 |
| 4-position terminal block (per segment) | 4 | 3.5mm | $0.40 | $1.60 |
| **Total** | | | | **~$4.18** |

**Board area:** ~15mm × 20mm for the 4 transceivers + MUX + passives. Fits easily alongside the existing RS485 transceiver section.

### 17.7 Benefits

| Feature | Single daisy-chain | On-board RS485 hub |
|---|---|---|
| Topology | Daisy-chain only | **Star from panel to each room** |
| Fault isolation | One cut cable kills all sensors | **Only that room's sensors go offline** |
| Cable runs | Must chain rooms in sequence | **Direct run per room — shortest path** |
| EMI isolation | VFD noise in one room affects entire bus | **Noise stays on that segment** |
| Max boards per segment | 32 (shared) | **32 per segment = 128 total** |
| UARTs consumed | 1 (or 2 if split) | **1** |
| Additional GPIOs | 0 | **2** (MUX select) |
| Board cost increase | $0 | **~$4** |
| Firmware change | None | **One function call per query** |

### 17.8 Scan Time Improvement

With 4 independent segments, the firmware can interleave queries across segments. While waiting for a response on segment 0 (~25 ms at 9600 baud), the bus is idle — but segments 1-3 are also idle. In a future optimization, you could pipeline by switching to the next segment immediately after sending a query (before the response arrives), using DMA and interrupts to capture responses asynchronously. This would theoretically scan all 24 boards in the same time it takes to scan 6.

For the initial implementation, sequential polling is fine:

| Config | Boards | Full scan (sequential) | Fastest critical sensor |
|---|---|---|---|
| 24 boards, 1 daisy-chain, 9600 | 24 | 3.0 s | 3.0 s |
| 24 boards, 4 segments, 9600 (sequential) | 6 per segment | 3.0 s | 750 ms (fast boards first per segment) |
| 24 boards, 4 segments, 19200 (sequential) | 6 per segment | 1.5 s | 375 ms |
| 24 boards, 4 segments, 9600 (pipelined future) | 6 per segment | ~750 ms | ~250 ms |

With priority polling (critical sensors first on each segment), suction temp and pressure update within 750 ms even at 9600 baud — well within the safe range for compressor protection.

### 17.9 When to NOT Use the Hub

- **Single-room systems (1–6 analog boards):** One daisy-chain is simpler. The hub segments are simply unused — the firmware treats segment 0 as the only active segment.
- **Two-room systems with rooms adjacent:** A single daisy-chain through both rooms is fine. No topology problem.

The hub costs $4 whether you use it or not, so there's no reason to make a separate board variant. Every AS2 board ships with the 4-port hub. Small installations use segment 0 only. Large installations use 2–4 segments.

### 17.10 Updated UART Allocation (with On-Board Hub)

| UART | Function | Notes |
|---|---|---|
| UART0 | Debug console | 115200 baud |
| UART1 | RPi5 bridge | 230400 baud |
| UART2 | Analog hub — 4 segments via MUX | 9600–38400, Gellert Protocol, up to 128 sensors |
| UART3 | Relay/DI Modbus RTU bus | 19200 baud, up to 32 modules |
| UART4 | VFD Modbus RTU bus | 19200 baud, up to 32 VFDs |
| UART5 | **Spare** — EEV bus or 2nd relay bus | Available for expansion |
| UART6 | **Spare** | |
| UART7 | **Spare** | |
| UART8 | **Spare** | |

The on-board hub frees UART5 (which would have been needed for a second analog bus) while providing better fault isolation than a 2-bus split could.

---

## 18. Complete Setup Guide — Twin Cold Storage with Independent 2-Compressor Refrigeration

This section walks through the entire setup of a real system: **2 cold storage rooms, each with its own independent 2-compressor refrigeration rack, 4 evaporator fan VFDs, and full sensor suite — all driven by one AS2 board.** This is what a field technician does from the moment equipment arrives at the job site to the moment the rooms are holding temperature.

### 18.1 What's in the Boxes

Before starting, verify everything is on-site:

**Main control panel (pre-built in Agristar panel shop):**
- 1× AM2434 AS2 board (mounted in panel, powered)
- 1× RPi5 (mounted, Ethernet connected to site network)
- 4× RS485 terminal blocks on panel face (Bus A segments 1–2, Bus B, Bus C)
- 120Ω termination resistors pre-installed on all 4 panel-end bus connections
- 24V DIN rail power supply for AS2 board + relay module inputs
- Contactors pre-mounted and wired to panel terminal strips (sized per compressor/heater HP)

**Room A field devices:**
- 3× Waveshare 8-ch relay DO modules (Room equip, Comp 1, Comp 2)
- 3× Waveshare 8-ch DI modules (Room equip, Comp 1, Comp 2)
- 4× ABB ACS380 VFDs (evap fans 1–4)
- 6× AS2 analog boards (temps, pressures, environment)
- Sensors: 6× NTC thermistors, 2× pressure transducers (4-20mA), plus optional humidity/CO2

**Room B field devices:**
- Identical to Room A

**Cable:**
- 3× spools Belden 9841 shielded twisted pair (Bus A segment 1, Bus A segment 2, Bus B, Bus C — some share runs)
- 18 AWG shielded cable for sensor-to-analog-board runs
- 120Ω termination resistors (one per bus end in each room)

---

### 18.2 Step 1 — Set DIP Switches on All Modules (Before Mounting)

Do this on a workbench or table before mounting anything. Every module gets its Modbus address set via DIP switches.

**Room A relay/DI modules (Bus B):**

| Module | Function | Modbus address | DIP switches (5-bit) | Label it |
|---|---|---|---|---|
| DO-1 | Room A equipment outputs | **1** | ○○○○● | "A-ROOM-DO" |
| DI-1 | Room A equipment inputs | **2** | ○○○●○ | "A-ROOM-DI" |
| DO-2 | Room A compressor 1 outputs | **3** | ○○○●● | "A-C1-DO" |
| DI-2 | Room A compressor 1 inputs | **4** | ○○●○○ | "A-C1-DI" |
| DO-3 | Room A compressor 2 outputs | **5** | ○○●○● | "A-C2-DO" |
| DI-3 | Room A compressor 2 inputs | **6** | ○○●●○ | "A-C2-DI" |

**Room B relay/DI modules (same Bus B, continued):**

| Module | Function | Modbus address | DIP switches (5-bit) | Label it |
|---|---|---|---|---|
| DO-4 | Room B equipment outputs | **7** | ○○●●● | "B-ROOM-DO" |
| DI-4 | Room B equipment inputs | **8** | ○●○○○ | "B-ROOM-DI" |
| DO-5 | Room B compressor 1 outputs | **9** | ○●○○● | "B-C1-DO" |
| DI-5 | Room B compressor 1 inputs | **10** | ○●○●○ | "B-C1-DI" |
| DO-6 | Room B compressor 2 outputs | **11** | ○●○●● | "B-C2-DO" |
| DI-6 | Room B compressor 2 inputs | **12** | ○●●○○ | "B-C2-DI" |

**Room A analog boards (Bus A, segment 1):**

| Board | Gellert address | DIP | Sensors |
|---|---|---|---|
| AB-1 | 0 | ○○○○○ | A: suction temp, discharge temp, return air, supply air |
| AB-2 | 1 | ○○○○● | A: suction pressure, head pressure, liquid line temp, ambient |
| AB-3 | 2 | ○○○●○ | A: humidity, CO2, spare, spare |
| AB-4 | 3 | ○○○●● | A: Comp 1 discharge temp, Comp 2 discharge temp, oil sump 1, oil sump 2 |
| AB-5 | 4 | ○○●○○ | A: Comp 1 suction temp, Comp 2 suction temp, liquid subcool, spare |
| AB-6 | 5 | ○○●○● | A: Comp 1 suction press, Comp 2 suction press, head press backup, spare |

**Room B analog boards (Bus A, segment 2):** Same layout, addresses 6–11.

**Write the address on each module with a Sharpie.** This prevents confusion during mounting.

---

### 18.3 Step 2 — Mount Modules in the Field

**Relay/DI modules** mount on DIN rail inside local junction boxes (one per room, or one per compressor rack):

```
Room A Junction Box (or compressor rack sub-panel):
┌─────────────────────────────────────────┐
│  [DO-1]  [DI-1]  ← Room equipment      │
│  [DO-2]  [DI-2]  ← Compressor 1        │
│  [DO-3]  [DI-3]  ← Compressor 2        │
│  [Bus B terminal strip] ← A/B in, A/B out│
│  [24V power terminal]                   │
└─────────────────────────────────────────┘
```

**Analog boards** mount near the sensors they read — typically on the evaporator coil frame or on the compressor rack frame. Sensor wires should be < 30 m from board to sensor.

**VFDs** mount in the main panel or in a dedicated VFD enclosure near the fan motors. Conduit to motors.

---

### 18.4 Step 3 — Wire the RS485 Buses

This is the backbone. Three types of bus cable leave the control panel:

#### Bus A Segment 1 — Panel to Room A analog boards

```
Panel (UART2, Seg 1 terminal)     Room A
       A ──────────────────────── AB-1 ── AB-2 ── AB-3 ── AB-4 ── AB-5 ── AB-6
       B ──────────────────────── AB-1 ── AB-2 ── AB-3 ── AB-4 ── AB-5 ── AB-6
     120Ω                                                                  120Ω
     (on-board)                                                     (at AB-6)
```

#### Bus A Segment 2 — Panel to Room B analog boards

```
Panel (UART2, Seg 2 terminal)     Room B
       A ──────────────────────── AB-7 ── AB-8 ── AB-9 ── AB-10 ── AB-11 ── AB-12
       B ──────────────────────── AB-7 ── AB-8 ── AB-9 ── AB-10 ── AB-11 ── AB-12
     120Ω                                                                    120Ω
     (on-board)                                                       (at AB-12)
```

Each room has its own dedicated analog cable run from the panel, thanks to the on-board RS485 hub. **No daisy-chaining between rooms.**

#### Bus B — Panel through Room A to Room B relay/DI modules

```
Panel (UART3)     Room A J-box                          Room B J-box
   A ────────── DO-1 ── DI-1 ── DO-2 ── DI-2 ── DO-3 ── DI-3 ── DO-4 ── DI-4 ── DO-5 ── DI-5 ── DO-6 ── DI-6
   B ────────── DO-1 ── DI-1 ── DO-2 ── DI-2 ── DO-3 ── DI-3 ── DO-4 ── DI-4 ── DO-5 ── DI-5 ── DO-6 ── DI-6
 120Ω                                                                                                        120Ω
```

12 modules on one daisy-chain. This is fine because relay/DI modules poll at 5 s — bus utilization is < 4%. The daisy-chain runs from the panel to Room A's junction box, through all 6 Room A modules, then continues to Room B's junction box through all 6 Room B modules.

#### Bus C — Panel through Room A to Room B VFDs

```
Panel (UART4)     Room A VFDs              Room B VFDs
   A ────────── VFD1 ── VFD2 ── VFD3 ── VFD4 ── VFD5 ── VFD6 ── VFD7 ── VFD8
   B ────────── VFD1 ── VFD2 ── VFD3 ── VFD4 ── VFD5 ── VFD6 ── VFD7 ── VFD8
 120Ω                                                                      120Ω
```

8 VFDs on one daisy-chain. Bus utilization < 1%.

**Wiring rules:**
- **Daisy-chain only** — in at one terminal, out from the next terminal to the next device. Never splice, T-tap, or star.
- **Shield grounded at panel end only.** Do not ground the shield at the remote end — this prevents ground loops.
- **120Ω termination across A-to-B at both physical ends** of each bus/segment. The panel end is already done on the AS2 board. The field tech installs the remote end terminator.

---

### 18.5 Step 4 — Wire the Field Devices to the Modules

#### 18.5.1 Relay DO Modules → Contactors

Each relay DO module output connects to a **contactor coil**, not directly to the motor or heater. The contactor does the heavy switching.

```
DO module point 1 ────► Contactor coil (+)
24V common ────────────► Contactor coil (-)
                         Contactor power contacts ────► Compressor motor / Heater
```

**Room A equipment DO (Addr 1):**

| DO point | Wire to | Contactor |
|---|---|---|
| 1 | Defrost heater contactor coil | 25A 3-pole |
| 2 | Defrost termination solenoid coil | — (direct 24V valve) |
| 3 | Drain pan heater contactor coil | 10A |
| 4 | Room light relay coil | 10A |
| 5 | Alarm horn | — (direct 24V) |
| 6 | Economizer damper actuator | — (24V signal) |
| 7 | Humidifier relay coil | — |
| 8 | Spare | — |

**Room A Compressor 1 DO (Addr 3):**

| DO point | Wire to | Contactor |
|---|---|---|
| 1 | Compressor 1 contactor coil | 40A+ 3-pole |
| 2 | Unloader solenoid valve | — (24V valve) |
| 3 | Crankcase heater relay coil | 5A |
| 4 | Condenser fan 1 contactor coil | 15A |
| 5 | Liquid line solenoid valve | — (24V valve) |
| 6 | Hot gas bypass solenoid | — (24V valve) |
| 7 | Oil pump relay coil (if applicable) | 5A |
| 8 | Fault/alarm relay | — |

**Room A Compressor 2 DO (Addr 5):** Same wiring layout, different physical compressor.

**Room B:** Identical to Room A, addresses 7, 9, 11.

#### 18.5.2 DI Modules ← Field Switches/Contacts

Each DI point reads a dry contact (switch, relay aux contact, pressure cutout).

```
24V supply ────► Switch/contact ────► DI module input
                                      DI module common ────► 24V common
```

**Room A Compressor 1 DI (Addr 4):**

| DI point | Wire from | Normal state |
|---|---|---|
| 1 | Compressor contactor aux contact | Closed when comp running |
| 2 | High pressure cutout | Closed (normal), opens on trip |
| 3 | Low pressure cutout | Closed (normal), opens on trip |
| 4 | Oil pressure differential switch | Closed (normal), opens on low oil |
| 5 | Motor overload/thermal relay | Closed (normal), opens on overload |
| 6 | Phase monitor relay aux | Closed (3-phase OK), opens on fault |
| 7 | Anti-recycle timer contact | Closed (ready), open (timing) |
| 8 | Oil level switch | Closed (normal), opens on low oil |

**Room A equipment DI (Addr 2):**

| DI point | Wire from | Normal state |
|---|---|---|
| 1 | Door switch | Closed (door shut) |
| 2 | Defrost termination thermostat | Closed (below setpoint) |
| 3 | Drain pan overflow float | Open (normal), closes on overflow |
| 4 | High temperature alarm contact | Open (normal) |
| 5 | Low temperature alarm contact | Open (normal) |
| 6 | Smoke detector input | Open (normal) |
| 7 | Emergency stop button (N.C.) | Closed (normal), opens on press |
| 8 | Spare | — |

#### 18.5.3 Analog Boards ← Sensors

Each analog board has 4 screw terminals for sensors. Wiring depends on sensor type:

**NTC thermistors (2-wire):**
```
Sensor lead 1 ────► Analog board CH input
Sensor lead 2 ────► Analog board CH common
(No polarity — resistive)
```

**4-20mA transducers (2-wire powered):**
```
24V supply (+) ────► Transducer (+)
Transducer (-) ────► Analog board CH input (current sense)
Analog board CH common ────► 24V common
```

**Sensor cable:** 18 AWG shielded, max 30 m per run. Shield grounded at analog board end only.

#### 18.5.4 VFDs ← Fan Motors

Each ACS380 VFD is wired:

```
3-phase supply ────► VFD input (L1, L2, L3)
VFD output (U, V, W) ────► Fan motor (shielded VFD-rated cable)
VFD RS485 (A, B) ────► Bus C daisy-chain
```

No control wiring between the VFD and the panel — **all control is via Modbus RTU on Bus C.** The VFD receives start/stop and speed commands digitally.

---

### 18.6 Step 5 — Configure the VFDs (Keypad)

Each ACS380 gets the same base parameters plus a unique Modbus address. This takes ~2 minutes per drive with the built-in keypad.

**Set on every VFD:**

| Parameter | Value | Purpose |
|---|---|---|
| 49.05 | EFB (Embedded Fieldbus) | Control source = Modbus |
| 49.06 | EFB | Reference source = Modbus |
| 53.01 | 1–8 (unique per VFD) | Modbus slave address |
| 53.02 | 19200 | Baud rate |
| 53.03 | 8-N-1 | Data format |
| 51.25 | 0 | Communication timeout = disabled |
| 46.01 | 0 Hz | Minimum speed reference = 0 (linear scaling) |
| 22.02 | 10 Hz (or per fan spec) | Minimum speed limit (fan won't stall) |
| 22.01 | 50 or 60 Hz | Maximum speed |

**Address assignments — write on label on each VFD:**

| VFD addr | Assignment |
|---|---|
| 1 | Room A evap fan 1 |
| 2 | Room A evap fan 2 |
| 3 | Room A evap fan 3 |
| 4 | Room A evap fan 4 |
| 5 | Room B evap fan 1 |
| 6 | Room B evap fan 2 |
| 7 | Room B evap fan 3 |
| 8 | Room B evap fan 4 |

---

### 18.7 Step 6 — Power On the AS2 Board and Verify Discovery

With all field wiring complete, power up the AS2 board. The boot sequence runs automatically:

```
[0.0 s] R5F-0 boots FreeRTOS, initializes all UARTs
[0.1 s] R5F-2 boots watchdog monitor

[0.2 s] FindAnalogBoards() starts:
        → Segment 1: scan addrs 0-31 → discovers AB-1 through AB-6  (Room A)
        → Segment 2: scan addrs 0-31 → discovers AB-7 through AB-12 (Room B)
        → Segments 3-4: scan → no boards found (unused for this install)
        Total: ~1.5 s

[1.7 s] Modbus Bus B discovery:
        → Scan addrs 1-12 → discovers all 12 relay/DI modules
        Total: ~200 ms

[1.9 s] Modbus Bus C discovery:
        → Scan addrs 1-8 → discovers all 8 VFDs
        Total: ~130 ms

[2.1 s] *** SYSTEM READY ***
        → AnalogPoll task begins reading sensors every 250 ms
        → ModbusPoll task begins reading DI states every 5 s
        → VfdPoll task begins reading VFD status every 120 s
        → ControlLoop_A and ControlLoop_B start (but idle — no setpoints yet)
```

**On the RPi5 UI (browser), verify:**

| Check | Where | Expected |
|---|---|---|
| All 12 analog boards online | Diagnostics → Analog | 12 green, addresses 0-11 |
| All 12 relay/DI modules online | Diagnostics → Modbus B | 12 green, addresses 1-12 |
| All 8 VFDs online | Diagnostics → Modbus C | 8 green, addresses 1-8 |
| Sensor readings plausible | Dashboard → Room A, Room B | Ambient temp ≈ site temperature, pressures at equalized 0 psi |
| All DI inputs in normal state | IO Status → Digital Inputs | Door closed, no trips, E-stop normal |
| All DO outputs OFF | IO Status → Digital Outputs | All off (system hasn't been commanded yet) |

**If any devices show offline:** Check DIP switch, check bus wiring continuity, check termination. The UI will show which specific address is missing.

---

### 18.8 Step 7 — Configure Zones in the UI

Now tell the AS2 which modules belong to which zone. This maps the physical addresses to logical control functions.

**Navigate to: Settings → Zone Configuration**

#### Zone A — Cold Storage Room A

| Setting | Value |
|---|---|
| Zone name | "Cold Storage A" |
| Building type | Cold storage — raw vegetable |
| **Room equipment** | |
| Room DO module | Address 1 |
| Room DI module | Address 2 |
| **Compressor 1 (Lead)** | |
| Comp 1 DO module | Address 3 |
| Comp 1 DI module | Address 4 |
| **Compressor 2 (Lag)** | |
| Comp 2 DO module | Address 5 |
| Comp 2 DI module | Address 6 |
| **Evaporator fans** | |
| Fan 1 VFD | Bus C, Address 1 |
| Fan 2 VFD | Bus C, Address 2 |
| Fan 3 VFD | Bus C, Address 3 |
| Fan 4 VFD | Bus C, Address 4 |
| **Sensors** | |
| Return air temp | Analog board 0, CH 3 |
| Supply air temp | Analog board 0, CH 4 |
| Room humidity | Analog board 2, CH 1 |
| Comp 1 suction temp | Analog board 4, CH 1 |
| Comp 1 suction pressure | Analog board 5, CH 1 |
| Comp 1 discharge temp | Analog board 3, CH 1 |
| Comp 2 suction temp | Analog board 4, CH 2 |
| Comp 2 suction pressure | Analog board 5, CH 2 |
| Comp 2 discharge temp | Analog board 3, CH 2 |
| Head pressure | Analog board 1, CH 2 |
| Liquid line temp | Analog board 1, CH 3 |
| Ambient temp | Analog board 1, CH 4 |

#### Zone B — Cold Storage Room B

Same structure, but mapped to addresses 7-12 (relay/DI), VFDs 5-8 (Bus C), and analog boards 6-11 (segment 2).

**Save → the AS2 stores this mapping in flash (dual-copy, power-loss safe).**

---

### 18.9 Step 8 — Set Operating Parameters

**Navigate to: Settings → Zone A → Operating Parameters**

| Parameter | Typical value | What it does |
|---|---|---|
| **Room setpoint** | 34°F (1.1°C) | Target return air temperature |
| **Cooling deadband** | 2°F (1.1°C) | Comp starts at setpoint + deadband, stops at setpoint |
| **Staging deadband** | 3°F (1.7°C) | Lag compressor starts if lead runs > 15 min and temp still above setpoint + staging deadband |
| **High temp alarm** | 45°F (7.2°C) | Alarm if return air exceeds this |
| **Low temp alarm** | 28°F (-2.2°C) | Alarm if return air drops below this |
| **Defrost interval** | 8 hours | Time between defrost cycles |
| **Defrost max duration** | 30 min | Failsafe — forces defrost termination |
| **Defrost termination temp** | 55°F (12.8°C) | Coil temp that ends defrost |
| **Drain time** | 5 min | Post-heater drain pause |
| **Fan restart speed** | 30% for 2 min, then auto | Post-defrost gentle restart |
| **Anti-recycle time** | 5 min | Minimum off-time between compressor starts |
| **High pressure trip** | 300 psig (R-448A) | Emergency compressor shutdown |
| **Low pressure trip** | 5 psig | Emergency compressor shutdown |
| **Max discharge temp** | 275°F (135°C) | Emergency compressor shutdown |
| **Lead/lag rotation** | Weekly | Equalize compressor run hours |
| **Fan staging** | Auto (based on load) | Controls how many fans run and at what speed |
| **Min fan speed** | 25% | Lowest VFD speed (prevents stall) |

**Repeat for Zone B** — parameters may differ (e.g., different setpoint for different product).

Each zone is **fully independent.** Room A can be at 34°F holding potatoes while Room B runs at 38°F holding apples. They share no control logic, no compressors, and no interlocks. The AS2 just runs two completely separate control programs that happen to share the same CPU.

---

### 18.10 Step 9 — Individual Device Checkout

Before enabling automatic control, test each device manually from the UI:

#### 18.10.1 Test Every DO Output

**Navigate to: Maintenance → Manual Override → Zone A**

For each output, toggle it ON from the UI and verify the field device responds:

| Test | Action | Verify |
|---|---|---|
| Comp 1 contactor | Toggle DO addr 3, point 1 ON | Contactor pulls in, aux contact closes (DI addr 4, point 1 = ON in UI) |
| Comp 1 contactor off | Toggle OFF | Contactor drops, DI shows OFF. Listen for a click. |
| Comp 2 contactor | Toggle DO addr 5, point 1 ON | Same verification |
| Condenser fan 1 | Toggle DO addr 3, point 4 ON | Fan runs |
| Liquid solenoid | Toggle DO addr 3, point 5 ON | Solenoid clicks, gas hiss |
| Defrost heater | Toggle DO addr 1, point 1 ON | Ammeter confirms heater draws rated amps |
| Room light | Toggle DO addr 1, point 4 ON | Room light turns on. Walk out and look. |

**Do this for every single DO point across all 6 DO modules (48 points total).** This catches wrong wires, blown fuses, bad contactors, and swapped terminals. Fix any issues now — it's trivial to fix wiring with the system de-energized and in manual mode.

#### 18.10.2 Test Every DI Input

For each DI, manually actuate the field switch and verify the UI shows the change:

| Test | Action | Verify |
|---|---|---|
| HP cutout | Manually trip high pressure cutout (test button on switch) | DI addr 4, point 2 changes from Closed to Open in UI |
| LP cutout | Manually trip low pressure cutout | DI addr 4, point 3 changes |
| E-stop | Press room E-stop button | DI addr 2, point 7 changes from Closed to Open |
| Door switch | Open room door | DI addr 2, point 1 changes |
| Oil pressure | Manually trip oil pressure switch (if test button exists) | DI addr 4, point 4 changes |

**Do this for every DI point across all 6 DI modules (48 points total).**

#### 18.10.3 Test Every VFD

**Navigate to: Maintenance → Manual Override → VFDs**

For each VFD, send a speed command from the UI:

| Test | Action | Verify |
|---|---|---|
| VFD 1 at 50% | Send 50% to Bus C addr 1 | Fan 1 spins at ~30 Hz. Check VFD display matches. |
| VFD 1 at 100% | Send 100% | Fan 1 spins at ~60 Hz |
| VFD 1 at 0% | Send 0% (STOP) | Fan 1 coasts down |
| Repeat for VFDs 2-8 | Same procedure | Each fan operates correctly |

**Check rotation direction.** If a fan spins backwards, swap any two motor leads at the VFD output.

#### 18.10.4 Verify Sensor Readings

**Navigate to: Dashboard → Sensors**

Compare displayed values against a known reference (spot-check with a calibrated thermometer and pressure gauge):

| Sensor | Read on UI | Verify with |
|---|---|---|
| Room A return air temp | Should read ambient (room not cooled yet) | Handheld thermometer in return air duct |
| Room A suction pressure | Should read equalized pressure (system off) | Manifold gauge on suction service valve |
| Room A head pressure | Same as suction (equalized) | Manifold gauge on discharge service valve |
| Room A humidity | Should read ambient humidity | Handheld hygrometer |

If any sensor reads wildly wrong: check wiring, check that the sensor type matches the analog board channel configuration (NTC vs 4-20mA), check that the transducer is powered.

---

### 18.11 Step 10 — Enable Automatic Control

Everything is tested individually. Now turn on the control loops:

1. **Close all manual overrides.** Return every DO to "Auto" mode.
2. **Navigate to: Settings → Zone A → Control Mode → Automatic**
3. **Navigate to: Settings → Zone B → Control Mode → Automatic**

**What happens next (Zone A as example):**

```
[0 s] ControlLoop_A reads return air temp → 75°F (room at ambient)
      Setpoint = 34°F → temp is 41°F above setpoint
      → Calls for cooling

[1 s] Start sequence: Compressor 1 (lead)
      → Crankcase heater OFF
      → Oil pump ON (if applicable)
      → Check pre-conditions: phase monitor OK, anti-recycle complete, no trips
      → After 30 s pre-run delay...

[31 s] Compressor 1 contactor ON (Addr 3, DO-1)
       Liquid line solenoid ON (Addr 3, DO-5)
       → Verify run feedback (Addr 4, DI-1) within 10 s

[41 s] Compressor 1 confirmed running
       → All 4 evap fans start at 100% (heavy pulldown load)
       → Condenser fan ON (Addr 3, DO-4)

       Suction pressure drops, head pressure rises — system is running.

[~15 min] If return air still > setpoint + staging deadband:
          → Start Compressor 2 (lag) — same sequence

[hours later] Room A reaches 34°F
              → Stop Compressor 2 (lag) first
              → If temp stays at setpoint, stop Compressor 1
              → Fans ramp down per staging table
              → Crankcase heaters ON for both compressors

[8 hours] Defrost initiates:
          → Fans OFF, compressors pump down, heaters ON
          → Terminate on coil temp or 30 min max
          → Drain time, fan restart, compressor restart
```

**Zone B operates identically but completely independently.** It starts its own compressors, runs its own fans, manages its own defrost schedule. If Zone A has a compressor trip, Zone B doesn't know or care — it keeps running.

---

### 18.12 Step 11 — Final Verification Checklist

Walk through every item before leaving the site:

| # | Check | Method | Pass |
|---|---|---|---|
| 1 | Both rooms pulling down temperature | Dashboard shows temps dropping | □ |
| 2 | All 4 compressors cycling correctly | Run hours incrementing on all 4 | □ |
| 3 | All 8 VFDs responding to speed commands | Fan speeds match UI display | □ |
| 4 | All 12 analog boards reporting | Diagnostics shows 12/12 green | □ |
| 5 | All 12 relay/DI modules reporting | Diagnostics shows 12/12 green | □ |
| 6 | High pressure cutout trips compressor | Trip HP switch → comp stops immediately | □ |
| 7 | E-stop stops everything in that room | Press E-stop → all compressors and fans stop | □ |
| 8 | Power loss safe state works | Pull AS2 power → all watchdog outputs drop within 30 s | □ |
| 9 | Power restore auto-recovery | Restore power → AS2 reboots, discovers devices, resumes control | □ |
| 10 | Force defrost runs correctly | Trigger manual defrost → observe full sequence | □ |
| 11 | Lead/lag staging works | Both comps run under heavy load, lag stops first as load drops | □ |
| 12 | Alarm notifications work | Trigger a high-temp alarm → verify alarm on UI and horn sounds | □ |
| 13 | RPi5 UI accessible from site network | Browser on phone/laptop connects to RPi5 IP | □ |
| 14 | Panel door label installed | Address map label laminated and attached inside panel door | □ |

---

### 18.13 Total Setup Time Estimate

| Phase | Time |
|---|---|
| DIP switches on 12 relay/DI modules | 15 min |
| DIP switches on 12 analog boards | 10 min |
| Mount modules in junction boxes and racks | 1–2 hours |
| Wire RS485 buses (3 runs + terminations) | 1–2 hours |
| Wire DO modules to contactors (48 points) | 3–4 hours |
| Wire DI modules to field switches (48 points) | 2–3 hours |
| Wire sensors to analog boards (24 boards × 4 channels) | 3–4 hours |
| Wire VFDs to motors and RS485 (8 VFDs) | 2–3 hours |
| Configure 8 VFDs via keypad | 20 min |
| Power on + verify discovery | 10 min |
| Configure zones in UI | 20 min |
| Set operating parameters | 10 min |
| Individual device checkout (48 DO + 48 DI + 8 VFD + sensors) | 2–3 hours |
| Enable auto, verify operation | 30 min |
| Final checklist | 30 min |
| **Total** | **~2 days (16–24 labor hours)** |

The AS2 board setup is a tiny fraction of this. The vast majority of time is point-to-point field wiring — which is identical regardless of what controller you use. The AS2-specific configuration (zones, parameters, DIP switches) adds about **1 hour** to what would already be a 2-day wiring job.
