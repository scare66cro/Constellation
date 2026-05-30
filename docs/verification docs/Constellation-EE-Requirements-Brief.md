# Agristar Constellation — Engineering Requirements Brief

**Prepared for:** External EE / PCB design firm
**Prepared by:** Agristar Controls Engineering
**Date:** March 24, 2026
**Revision:** 1.0
**Reference:** `docs/Next-Gen-Controller-Design.md` (full system design document — provided separately)

---

## 1. Project Overview

Agristar manufactures cold storage and refrigeration control panels for the agricultural industry. We are replacing our current control platform (TI TM4C129 + CPLD + RS485 I/O) with a new architecture called **Constellation**. This brief defines the two custom PCB assemblies we need designed and prototyped.

**What we need from your firm:**
- Schematic capture for both boards
- PCB layout (EDA files — KiCad or Altium preferred)
- Design-for-manufacturing review
- BOM finalization with distributor part numbers
- Gerber output + assembly drawings for prototype fabrication
- Optional: first-run prototype assembly (10 units of each board)

**What we provide:**
- Complete firmware (we write all embedded software in-house)
- Full system design document with register maps, protocols, and I/O assignments
- Functional test procedures
- Application context (what the boards control, safety requirements)

**There are two boards to design:**

| Board | Name | Function | Qty per system |
|---|---|---|---|
| **Board 1** | **Nova** | Central controller — runs all control logic | 1 |
| **Board 2** | **Orbit** | 24VDC-powered field I/O module — relays, inputs, analog outputs, dual RS485 | 4–6 |

---

## 2. Board 1 — Nova Controller

### 2.1 Purpose

The Nova board is the central brain. It runs FreeRTOS firmware that manages all refrigeration and storage control logic. It communicates with Orbit I/O modules over Ethernet (Modbus TCP) and with a Raspberry Pi 5 over a UART serial link. There is no user-facing I/O on this board — all field I/O is on the Orbit modules.

### 2.2 MCU

| Parameter | Requirement |
|---|---|
| **Processor** | TI AM2434 (LFBGA 324-pin, 17×17mm) |
| **Clock** | 800 MHz (4× Cortex-R5F cores) |
| **SDK** | TI MCU+ SDK — we handle all firmware development |
| **Fallback** | If AM2434 has lead time issues, design should be pin-adaptable to AM2634 (same package/pinout, 2 cores instead of 4). Alert us before committing layout. |

### 2.3 Memory

| Component | Specification | Notes |
|---|---|---|
| **DDR4 SDRAM** | 512 MB minimum, 1 GB preferred | Single chip. AM2434 has DDR4 controller. Follow TI reference design for routing (length-matched, impedance-controlled). |
| **OSPI NOR Flash** | 8–64 MB | OSPI interface (up to 200 MHz). Used for firmware A/B dual-image storage + configuration data + HMAC key storage. Must support hardware write-protect via WP# pin tied to a GPIO output — protects firmware images and cryptographic keys from software-level tampering. |
| **eMMC** | 8 GB (optional) | Backup on-board log storage. DNP by default — primary logging uses Nova's 64 KB SRAM ring buffer → RPi5 filesystem via UART1. Populate at commissioning (~$4) for sites requiring non-volatile logging independent of RPi5. Route to MMC interface. |

### 2.4 Power

| Parameter | Specification |
|---|---|
| **Input voltage** | 5VDC (from external DIN-rail supply or USB-C for bench use) |
| **Internal rails** | 1.0V core, 1.8V I/O, 3.3V peripherals, 1.1V DDR4 |
| **Regulators** | Follow TI AM2434 EVM power tree reference design. TPS6286x or equivalent point-of-load converters. |
| **Hold-up capacitors** | 100µF + 10µF on flash VCC rail — provides 1-2 ms to complete in-progress flash writes during power loss |
| **Current estimate** | ~1.5A at 5V total board draw |

### 2.5 Ethernet

| Parameter | Specification |
|---|---|
| **Interface** | CPSW (Common Platform Switch) — built into AM2434 |
| **PHY** | External Gigabit Ethernet PHY (DP83869HM or equivalent). Follow TI reference design. |
| **Connector** | 1× RJ45 with integrated magnetics (standard Ethernet jack, data only — no PoE on this board, powered from external 5V supply) |
| **Speed** | 10/100/1000 Mbps |
| **LED indicators** | Link (green) + Activity (green blink) on RJ45 jack |

**Note:** The Nova's Ethernet connects to an isolated 192.168.0.0/24 control network. Nova is the gateway/master at 192.168.0.1. The RPi5 is on a separate 10.x site network — there is no internet connection on the control interface.

### 2.6 Serial Interfaces

| Interface | Connector | Configuration | Purpose |
|---|---|---|---|
| **UART0** | 3-pin header (TX, RX, GND) — 2.54mm | 115200 8N1 | Debug console. Always available. |
| **UART1** | 4-pin header (TX, RX, GND, VCC) — 2.54mm | 230400 8N1 | Primary data link to Raspberry Pi 5. Point-to-point, not RS485. 3.3V LVCMOS levels — the RPi5 side handles level shifting. Firmware uses HMAC-SHA256 authenticated packets with sequence numbers (no hardware impact — software protocol only). |
| **UART2** | 4-pin header (TX, RX, GND, DE) — 2.54mm | Configurable | Spare. Pinout allows optional MAX3485 RS485 transceiver (DNP footprint on board). For retrofit sites that need a direct RS485 bus. |
| **UART3–8** | No connectors needed | — | 6× spare UARTs. Route to test pads only. Future expansion. |

### 2.7 JTAG / Debug

| Parameter | Specification |
|---|---|
| **Connector** | 20-pin ARM JTAG (1.27mm / 0.05" pitch) |
| **Interface** | Standard ARM CoreSight. Compatible with TI XDS110 debug probe (comes with AM2434 EVM). |
| **Access** | Must be accessible when board is in DIN-rail enclosure (top or side of PCB) |

### 2.8 Miscellaneous I/O

| Feature | Specification | Notes |
|---|---|---|
| **Reset button** | Tactile pushbutton, active-low | Standard POR circuit with debounce cap |
| **Boot mode pins** | Per TI AM2434 SYSBOOT configuration | Default to OSPI boot. Expose test pads for alternate boot modes. |
| **RTC** | DS3231MZ+ (I2C, ±2 ppm, SO-8) + CR2032 coin cell holder | Battery-backed real-time clock. Connect to AM2434 I2C0. Coin cell accessible with enclosure lid removed. Provides wall-clock time independent of RPi5 for defrost scheduling, event timestamps, and time sync to Orbit modules. |
| **User LEDs** | 3× GPIO-driven LEDs: power (green, solid), heartbeat (green, flashing — ARM R5F-0 toggled at ~1 Hz to confirm firmware is running), fault (red, solid) | Visible from panel front. Heartbeat LED is the primary visual indicator that the AM2434 is alive and executing the control loop. |
| **GPIO header** | 1× 10-pin 2.54mm header, 8 GPIOs + VCC + GND | General expansion. Not used in initial application. Route to available AM2434 GPIO pads. |

### 2.9 PCB Requirements

| Parameter | Specification |
|---|---|
| **Layers** | 4 minimum (signal-ground-power-signal). 6-layer if DDR4 routing requires it. |
| **Size** | Target ~80×60mm. Must fit inside DIN-rail enclosure. |
| **Impedance** | Controlled impedance for DDR4 (40Ω single-ended, 80Ω differential — per TI reference), OSPI, and Gigabit Ethernet differential pairs |
| **Finish** | **ENIG** (electroless nickel immersion gold) — mandatory. No HASL. Gold resists oxidation and sulfide corrosion in high-humidity environments. |
| **Stackup** | Follow TI AM2434 EVM recommendations for DDR4 signal integrity |
| **Mounting** | 4× M3 mounting holes for DIN-rail enclosure standoffs |
| **Thermal** | Exposed pad on MCU — thermal vias to internal ground plane. No active cooling required (~1.5W total). |
| **Conformal coating** | Acrylic conformal coating (IPC-CC-830C, Type AR). Applied after assembly. Mask-off: connectors, debug headers, coin cell holder. See Section 7.6. |

### 2.10 DIN-Rail Enclosure

| Parameter | Specification |
|---|---|
| **Type** | Standard 35mm DIN rail mount, IP20 |
| **Size** | Sized to PCB — approximately 90×70×40mm external |
| **Openings** | RJ45 (front), JTAG (top or side), UART headers (accessible with lid removed), USB-C power (side), LEDs (front, light pipes or clear panel) |
| **Material** | ABS or polycarbonate, UL94 V-0 rated |

### 2.11 Standards & Compliance Targets

| Standard | Applicability |
|---|---|
| **UL 508A** | Industrial control panels — the Nova board goes inside a UL-listed panel |
| **IEC 61010-1** | Safety of electrical equipment for measurement, control, and laboratory use |
| **EMC (FCC Part 15B, CISPR 11)** | Emissions. Industrial environment, Class A. |
| **RoHS** | All components and PCB must be RoHS compliant |

---

## 3. Board 2 — Orbit I/O Module

### 3.1 Purpose

The Orbit module is a 24VDC-powered field I/O module that sits near refrigeration/storage equipment. Each module provides 10 digital outputs (24VDC open-collector sink via ULN2803A Darlington drivers), 10 digital inputs (optocoupled), 2 analog outputs (4-20mA), and 2 independent RS485 ports. It communicates with the Nova controller over Modbus TCP on a dedicated Ethernet network.

There is no high voltage on this board. The digital outputs sink 24VDC to drive external DIN-rail relay coils, solenoid valves, indicator lights, and other 24VDC loads. All high-voltage switching is handled by external relays in the installer's panel.

### 3.2 MCU

| Parameter | Requirement |
|---|---|
| **Processor** | STM32F091CCT6 (Cortex-M0, LQFP-48) — pin-compatible upgrade from F072CB, same package/pinout |
| **Clock** | 48 MHz |
| **Flash** | 256 KB (32 KB bootloader + 216 KB application + 8 KB config) |
| **SRAM** | 32 KB |
| **Peripherals used** | SPI1 (W5500), SPI2 (shift registers + DAC), USART1 (RS485 Port A), USART2 (RS485 Port B), I2C1 (future expansion), USB (bench programming), SWD (debug/programming), GPIO (DIP switch, LEDs), **IWDG** (independent watchdog, 40 kHz LSI — must be enabled in firmware, always-on after first kick) |
| **Debug** | SWD 2-wire (SWDIO + SWCLK) via 10-pin ARM Cortex debug header (1.27mm pitch). Compatible with ST-LINK/V2 and J-Link. Must be accessible with enclosure lid removed. |

### 3.3 Ethernet + Power Input

| Parameter | Specification |
|---|---|
| **Ethernet controller** | WIZnet W5500 (hardware TCP/IP + MAC + PHY, SPI interface) |
| **SPI connection** | STM32 SPI1 → W5500 SPI (up to 33 MHz). CS, MOSI, MISO, SCK + INT + RST. |
| **Power input** | 24VDC via 2-position screw terminal (24V+ / 24V−). Supplied by UL-listed DIN-rail power supply (e.g., Mean Well HDR-30-24). Same 24V rail that powers analog boards and DI circuits. |
| **Buck regulator** | TPS54302 or equivalent: 24V → 5V, up to 3A. Supplies 3.3V LDO input. |
| **LDO** | AMS1117-3.3 or equivalent: 5V → 3.3V, 1A. Supplies MCU, W5500, transceivers. |
| **RJ45 connector** | Standard RJ45 with integrated magnetics (data only, no PoE). Ethernet data + link/activity LEDs. |
| **24VDC terminal** | 2-position screw terminal, rated ≥10A. Accepts 18–24 AWG wire. Located on logic (LV) edge of PCB. |
| **Link/Activity LEDs** | On RJ45 jack, driven by W5500 |

### 3.4 Digital Outputs (10 channels)

**There is no high voltage on this board.** All digital outputs are 24VDC open-collector sinks that drive external loads (DIN-rail relay coils, solenoid valves, indicator lights). All high-voltage switching is handled by external relays in the installer's panel — this is the same approach used in Agristar's existing AS2 control panels.

| Parameter | Specification |
|---|---|
| **Output type** | 24VDC open-collector sink — ULN2803A Darlington driver pulls the load's return to ground. External load connects between +24V and the output pin. When output is ON, current flows through the load and sinks through the ULN2803A to ground. When OFF, the output goes high-impedance — no current flows. |
| **Max current per channel** | 500 mA (ULN2803A absolute max). Typical loads: DIN-rail relay coils 15–80 mA, solenoid valves up to 300 mA, Siemens 8WD46 LED stack light elements ~25 mA. |
| **Drive chain** | **74HC595 shift registers** (2×, DIP-16, socketed) → **ULN2803A Darlington drivers** (2×, DIP-18, socketed). 10 output channels total. ULN2803A COM pin tied to +24V — built-in clamp diodes absorb inductive kickback from external relay coils and solenoid valves. No external flyback diodes needed. |
| **Overcurrent protection** | **PTC resettable fuse per ULN2803A bank** — one PTC per 8-channel driver (2× total). Rated **2.5A hold / 5.0A trip** (e.g. Bourns MF-R250). Placed in series on the 24V COM pin of each ULN2803A. Protects against shorted output wiring or stalled solenoid. Self-resetting (~30 seconds after fault clears) — no service call to replace blown fuses. ~$0.30 each. |
| **Terminal block** | **20-position 5.08mm pitch pluggable** PCB header + mating plug (Phoenix Contact MSTB 2,5/ or equivalent). Pinout: V+ / DO alternating pairs × 10 channels. Installer wires loads into the plug at the bench, then clicks the plug onto the board header. |
| **Fail-safe** | Power loss → ULN2803A goes high-impedance → no current flows → external relay coils de-energize → load contacts open. Inherent fail-safe with no firmware intervention required. |
| **DO status LEDs** | 10× **green** LEDs (one per channel) visible from enclosure front. LED ON = output active (ULN2803A sinking current). LED OFF = output off. Driven from shift register output signal. |

### 3.5 Digital Inputs (10 channels)

| Parameter | Specification |
|---|---|
| **Input type** | Wet contact, 24VDC |
| **Isolation** | PC817 optocoupler per channel (or equivalent) — galvanic isolation from MCU logic |
| **Input circuit** | 24V → 4.7kΩ series resistor → optocoupler LED anode → cathode → DI common |
| **Logic output** | Optocoupler collector → MCU GPIO input (active low with 10kΩ pull-up to 3.3V) |
| **Terminal block** | 12-position screw terminal (10 DI channels + V+ + COM) |
| **DI status LEDs** | 10× **green** LEDs (one per input) visible from enclosure front. LED ON = input closed (24VDC present). |
| **Input voltage range** | 18-30VDC operating, 36VDC max (survivable) |
| **Response time** | ≤ 5 ms (plenty for relay contact closure detection) |

### 3.6 Analog Outputs (2 channels)

| Parameter | Specification |
|---|---|
| **Output type** | 4-20mA current loop |
| **DAC** | MCP4822 (2-ch, 12-bit, SPI). Connects to STM32 SPI2. |
| **Current driver** | XTR111 voltage-to-current converter per channel (one per AO). Converts DAC voltage output to 4-20mA current loop. |
| **Power** | Loop-powered — the receiving device (VFD, EEV, valve actuator) provides the loop power. The XTR111 regulates current only. |
| **Terminal block** | 4-position screw terminal (AO1+, AO1−, AO2+, AO2−) |
| **Accuracy** | ≤ ±0.5% FSR at 25°C |
| **Compliance voltage** | 10-36VDC (standard for industrial 4-20mA loops) |

### 3.7 RS485 Ports (2 independent channels)

| Parameter | Port A | Port B |
|---|---|---|
| **Transceiver** | MAX3485 in **DIP-8 socket** (field-replaceable) | Same |
| **MCU UART** | USART1 | USART2 |
| **Default baud** | 9600 (analog boards) | 19200 (VFDs/EEVs) |
| **Protocol** | Modbus RTU master — Orbit polls analog boards, stores sensor data in registers (firmware-controlled) | Modbus RTU pass-through (firmware-controlled) |
| **Terminal block** | 3-position (A+, B−, GND) per port | Same |
| **Termination** | 120Ω resistor with jumper (solder bridge or 2-pin header) per port | Same |
| **Direction control** | MAX3485 DE/RE̅ tied together → one GPIO per port (TX enable) | Same |
| **TX/RX LEDs** | 2× **blue** LEDs per port (TX activity, RX activity) — indicates bus data traffic. Visible from enclosure front. | Same |
| **ESD protection** | TVS diode on each data line (SMBJ6.0CA or equivalent) — RS485 lines are frequently miswired in the field | Same |

**Both ports operate simultaneously and independently. This is a firm requirement — they must not share any hardware resource.**

### 3.8 DIP Switch (Address Configuration)

| Parameter | Specification |
|---|---|
| **Type** | 6-position DIP switch (piano style, PCB mount) |
| **Function** | Sets module IP address and Modbus device ID. Binary encoding: DIP value 1–63. |
| **Connection** | Each switch position → MCU GPIO input with 10kΩ pull-down. Switch ON = logic high. |
| **Readout** | Firmware reads DIP switch value at boot. IP = 192.168.0.{DIP_value + 1}. Nova (gateway/master) is fixed at 192.168.0.1. DIP 1 → .2, DIP 2 → .3, …, DIP 63 → .64. |
| **Accessibility** | Must be accessible when board is installed — visible through enclosure lid window or accessible with lid removed. Does not need to be changeable while powered. |

### 3.9 LED Indicators Summary

| LED | Color | Function |
|---|---|---|
| Power | Green (solid) | 24V power present and regulated |
| Link | Green (solid) | Ethernet link established (driven by W5500) |
| Activity | Green (blink) | Ethernet TX/RX activity (driven by W5500) |
| RS485-A TX | Blue | Port A transmitting (bus data activity) |
| RS485-A RX | Blue | Port A receiving (bus data activity) |
| RS485-B TX | Blue | Port B transmitting (bus data activity) |
| RS485-B RX | Blue | Port B receiving (bus data activity) |
| DO 1–10 | Green | Digital output active (ULN2803A sinking current) |
| DI 1–10 | Green | Digital input closed (24VDC present on input) |
| Fault/Watchdog | Red | Watchdog timeout active or module fault |

All LEDs must be visible from the enclosure front panel. Use light pipes or clear enclosure sections as needed.

### 3.10 Power Architecture

```
24VDC input (2-pos screw terminal)
    │
    ├──► 5A board-level fuse (fast-blow, 5×20mm cartridge in PCB fuse clip)
    │         │
    │         ├──► 24V rail → PTC fuse (2.5A) → ULN2803A #1 COM (DO 1–8)
    │         │                PTC fuse (2.5A) → ULN2803A #2 COM (DO 9–10)
    │         │                → DO terminal block V+ pins (pass-through to loads)
    │         │
    │         ├──► TPS54302 buck regulator (24V → 5V, 3A max)
    │         │         │
    │         │         └── AMS1117-3.3 LDO → 3.3V rail (~200mA)
    │         │                  │
    │         │                  ├── STM32F091 MCU
    │         │                  ├── W5500 Ethernet controller
    │         │                  ├── 2× MAX3485 RS485 transceivers
    │         │                  ├── 2× 74HC595 shift registers
    │         │                  ├── MCP4822 DAC
    │         │                  └── DIP switch pull-downs, LED drivers
    │         │
    │         └── 24V return (through screw terminal)
    │
    └── Protective earth / chassis bond (if applicable)
```

The 24V rail passes through the DO pluggable terminal block V+ pins to power external loads. When a ULN2803A output turns ON, it sinks current from the load through the DO pin to ground. The ULN2803A itself draws negligible power (~5mW total for all 10 channels). External load current flows through the 24VDC supply, not through the on-board regulator.

| Rail | Voltage | Max current | Source |
|---|---|---|---|
| 24VDC input | 24V (20–28V) | 0.25A (logic) + load current (pass-through) | DIN-rail power supply (shared with analog boards) |
| **Board input fuse** | — | **5A fast-blow** (5×20mm cartridge in PCB fuse clip) | Protects PCB traces and power supply from catastrophic short |
| **PTC per ULN2803A bank** | — | **2.5A hold / 5.0A trip** (Bourns MF-R250 or equiv.) | Self-resetting overcurrent protection for output banks |
| Primary regulated | 5V | 0.5A | TPS54302 buck regulator |
| Logic | 3.3V | 200 mA | AMS1117-3.3 LDO |
| **Steady-state (any/all outputs ON)** | — | — | **~2.0W** (logic only — external load current bypasses the regulator) |
| **Steady-state (all outputs OFF)** | — | — | **~2.0W** (logic only) |

### 3.11 PCB Requirements

| Parameter | Specification |
|---|---|
| **Layers** | 2-layer minimum. 4-layer if routing density requires it (preferred for better EMC). |
| **Size** | **114×140mm (4.5" × 5.5")** — confirmed fit for all terminal blocks (31 screw-terminal pins at 5.08mm pitch in two edge rows), 6 DIP sockets, MCU + W5500 + buck/LDO + RJ45, DIP switch, and fuse clip. No HV isolation zone — entire board is 24VDC/3.3V logic. |
| **Finish** | **ENIG** (electroless nickel immersion gold) — mandatory. No HASL. Gold resists oxidation and sulfide corrosion in high-humidity environments. |
| **Copper** | 1 oz everywhere (no high-current traces — load current flows through terminal block, not PCB traces) |
| **Mounting** | 4× M3 mounting holes for DIN-rail enclosure standoffs |
| **Component placement** | All components are low-voltage. Group DO pluggable header on one edge, DI/AO/RS485 terminals on opposite edge. MCU, W5500, buck/LDO, RJ45, 24VDC input, DIP switch arranged for clean routing. |
| **Socketed components** | MAX3485 sockets (DIP-8), ULN2803A sockets (DIP-18), 74HC595 sockets (DIP-16) — all machined-pin DIP, **gold-plated contacts** (Au over Ni). Tin-plated sockets will develop oxide/sulfide films in high-humidity environments. Ensure through-hole pad clearance for DIP sockets on both sides. |
| **Conformal coating** | Acrylic conformal coating (IPC-CC-830C, Type AR). Applied after assembly. Mask-off: DIP sockets, pluggable terminal block, screw terminals, RJ45 jack, DIP switch, SWD header. See Section 7.6. |

### 3.12 DIN-Rail Enclosure

| Parameter | Specification |
|---|---|
| **Type** | Standard 35mm DIN rail mount, IP20 |
| **Size** | Approximately 124×150×50mm external (sized to 114×140mm PCB + terminal blocks) |
| **Openings** | Top: DO pluggable terminal (20 pos). Bottom: DI (12 pos), AO (4 pos), RS485 A (3 pos), RS485 B (3 pos). Side: RJ45 (data) + 24VDC screw terminal (2 pos). Front: LED window/light pipes. |
| **Material** | ABS or polycarbonate, UL94 V-0 rated |
| **Ventilation** | Passive (no fans). Slots on top and bottom for convection. |
| **Field access** | Lid removable for relay/IC replacement. DIP switch accessible with lid open. |

### 3.13 Standards & Compliance Targets

| Standard | Applicability |
|---|---|
| **UL 508A** | Entire module goes inside a UL-listed industrial control panel |
| **IEC 61010-1** | Safety — low-voltage equipment, creepage, clearance (24VDC max on board) |
| **EMC (FCC Part 15B, CISPR 11)** | Industrial environment, Class A emissions |
| **RoHS** | All components and PCB must be RoHS compliant |

---

## 4. Firmware Responsibility Split

| Area | Agristar (in-house) | EE Firm |
|---|---|---|
| **Nova firmware** (FreeRTOS, control logic, Modbus TCP client, RPi5 bridge) | YES — we write this | No |
| **Orbit firmware** (Modbus TCP slave, DO control, RS485 gateway, watchdog) | YES — we write this | No |
| **Schematic design** | Review / approve | YES — you design this |
| **PCB layout** | Review / approve | YES — you design this |
| **BOM finalization** | Review / approve | YES — you select final part numbers with distributor sources |
| **DFM review** | Participate | YES — you lead |
| **Prototype assembly** | Functional test + firmware load | Optional — you assemble, we test |

**We need schematic + PCB + BOM + Gerbers from you. We handle all firmware, testing, and production.**

---

## 5. Deliverables Checklist

### 5.1 Per Board (Nova and Orbit)

- [ ] Schematic PDF + native EDA source files (KiCad or Altium)
- [ ] PCB layout files (native + Gerber RS-274X + drill files)
- [ ] BOM with manufacturer P/N, distributor P/N (DigiKey/Mouser), unit cost, quantity
- [ ] Assembly drawing (component placement, polarity marks, silkscreen)
- [ ] 3D model / step file (for enclosure fit check)
- [ ] Design-for-manufacturing report (DRC clean, via sizes, trace widths, minimum annular ring)
- [ ] Impedance stackup report (Nova: DDR4 + Gigabit Ethernet. Orbit: optional if 4-layer.)
- [ ] Test point map (critical signals labeled)

### 5.2 Additional

- [ ] Consolidated BOM with preferred ordering quantities for 10-unit prototype run
- [ ] Recommended PCB fabrication house (for prototype and production)
- [ ] Recommended assembly house (if separate from your firm)
- [ ] Power consumption measurements (actual, from prototype)

---

## 6. Reference Material Provided

| Document | Description |
|---|---|
| `docs/Next-Gen-Controller-Design.md` | Full system design document — 1700+ lines covering processor selection, communication architecture, Orbit module specs, I/O point assignments, register maps, firmware task architecture, data flow diagrams, and cost analysis |
| TI AM2434 datasheet | [AM2434 Product Page](https://www.ti.com/product/AM2434) |
| TI AM2434 EVM schematic | Reference design for power tree, DDR4 routing, Ethernet PHY — available from TI |
| STM32F091CCT6 datasheet | [STM32F091 Product Page](https://www.st.com/en/microcontrollers-microprocessors/stm32f091cc.html) — pin-compatible with F072CB (same LQFP-48) |
| DS3231MZ+ datasheet | [DS3231 Product Page](https://www.analog.com/en/products/ds3231.html) — RTC for Nova board |
| WIZnet W5500 datasheet | [W5500 Product Page](https://www.wiznet.io/product-item/w5500/) |
| TPS54302 datasheet | 24V→5V buck regulator — TI |
| ULN2803A datasheet | 8-channel Darlington driver, DIP-18 — TI (or equivalent) |

---

## 7. Design Notes & Warnings

### 7.1 Orbit — 24VDC Output Design

The Orbit module has **no high voltage on the board**. The maximum voltage present is 24VDC (input supply), and the maximum voltage on any output terminal is 24VDC (pass-through to external loads). All high-voltage switching is performed by external DIN-rail relays in the installer's panel.

- ULN2803A COM pin must be tied to +24V **through PTC resettable fuse** (enables built-in clamp diodes for inductive load protection while protecting against shorted field wiring)
- Each ULN2803A bank has its own PTC fuse (2.5A hold / 5.0A trip) — a short on DO1–8 does not affect DO9–10 and vice versa
- Board-level 5A fast-blow input fuse (5×20mm cartridge in PCB fuse clip) protects upstream supply and PCB traces from catastrophic fault
- PTC fuses are self-resetting (~30 seconds after fault removal) — no truck roll to replace blown fuses
- DO terminal block carries +24V and open-collector sink pins only — no mains voltage ever enters the board
- Standard 24VDC creepage/clearance rules apply (no special HV isolation required)
- No routed PCB slot needed — entire board is a single low-voltage zone

### 7.2 Orbit — DIP IC Sockets Are Mandatory

Every DIP-package IC (MAX3485, ULN2803A, 74HC595) goes in a machined-pin DIP socket with **gold-plated contacts** (Au over Ni). These are field-replaceable components. A technician in the field must be able to pull a failed IC and push in a new one in 30 seconds. This is a firm product requirement.

Gold plating is required because approximately 20% of installations are in high-humidity environments (up to 95% RH) where tin-plated contacts develop oxide and sulfide films within months, causing intermittent failures.

### 7.3 Nova — Follow the TI Reference Design

The AM2434 is a complex BGA with DDR4. We strongly prefer you follow the TI AM2434 EVM reference design for:

- DDR4 routing (length matching, impedance, termination)
- Power delivery (voltage rail sequencing, decoupling)
- OSPI flash connection
- Gigabit Ethernet PHY connection

Deviate from the reference design only where our board size/connector requirements differ.

### 7.4 Both — DIN Rail Enclosure Fit

Both boards go into off-the-shelf DIN rail enclosures. Confirm enclosure dimensions with us before committing PCB size. We can provide target enclosures or work with your recommendation.

### 7.5 Both — Design for Testability

- Include test pads for all power rails, SPI buses, UART TX/RX, and critical GPIOs
- JTAG accessible on both boards (20-pin ARM on Nova, SWD 10-pin on Orbit)
- At least one unpopulated LED footprint on each power rail for visual power-up verification

### 7.6 Both — Environmental Protection (Humidity / Corrosion)

These boards operate in cold storage and refrigeration facilities. Approximately 80% of installations are in climate-controlled equipment rooms inside NEMA 4X enclosures (warm interior prevents condensation). The remaining ~20% are in harsher conditions: vented steel cabinets that admit dust, outside air, and up to **95% relative humidity** — classified as IEC 60721-3-3 Class 3C4 (high corrosion risk).

All boards (Nova and Orbit) are built with universal environmental protection so a single SKU ships to every site:

| Protection Layer | Specification | Purpose |
|---|---|---|
| **ENIG PCB finish** | Electroless nickel (3–6 µm) + immersion gold (0.05–0.1 µm) per IPC-4552 | Nickel barrier resists sulfide/chloride corrosion; gold prevents oxidation of exposed pads and vias |
| **Acrylic conformal coating** | Type AR per IPC-CC-830C. 25–75 µm dry film thickness. Applied by CM after assembly. | Seals solder joints, passive leads, and exposed traces against moisture ingress and ionic contamination |
| **Gold-plated DIP sockets** | Machined-pin, Au over Ni contacts (e.g. Mill-Max 110-series or equiv.) | Prevents oxide/sulfide film on socket contacts that causes intermittent IC connection failures |
| **Mask-off zones** | DIP sockets (mating surfaces), pluggable/screw terminal blocks, RJ45 jack, DIP switch, SWD/JTAG headers | These surfaces must remain uncoated for mechanical insertion, field service, and electrical contact |

**Notes:**
- Enclosures remain IP20 — conformal coating provides moisture protection at the board level while maintaining easy field access (lid-off for DIP switch, IC swaps). A sealed IP54+ enclosure would impede service and add unnecessary cost.
- Acrylic coating is field-reworkable: dissolves in IPA (isopropyl alcohol) for localized IC replacement, then re-spray to reseal.
- Conformal coating does **not** replace proper wiring practice — field wire terminations should use ferrules or tinned ends.

---

## 8. Prototype Quantities & Timeline

| Item | Qty | Notes |
|---|---|---|
| Nova PCB (bare board) | 10 | 5 for assembly, 5 spare |
| Nova assembled | 5 | Or we assemble in-house if you provide stencil |
| Orbit PCB (bare board) | 20 | 10 for assembly, 10 spare |
| Orbit assembled | 10 | Same — or we do it with stencil |
| Components | Enough for above + 20% overage | BOM order placed by your firm or ours — confirm approach |

**We are flexible on timeline. Quality and correctness matter more than speed. Target: schematic review in 4 weeks, layout review in 8 weeks, prototype boards in 12 weeks.**

---

## 9. Contact & Communication

| Role | Contact |
|---|---|
| Technical lead (firmware + system architecture) | [Agristar engineering contact] |
| Mechanical / panel design | [Agristar mechanical contact] |
| Purchasing / procurement | [Agristar purchasing contact] |

**Preferred communication:** Weekly design review calls + shared EDA project repository. We want to review schematics before layout begins and review layout before Gerber generation.

---

## 10. Appendix — Quick Reference Card

### Nova at a Glance

```
┌──────────────────────────────────────────────┐
│  NOVA CONTROLLER                             │
│  ─────────────────                           │
│  MCU: TI AM2434 (4× R5F @ 800 MHz)          │
│  RAM: 512 MB DDR4 + 2 MB on-chip ECC SRAM   │
│  Flash: 8-64 MB OSPI NOR (A/B dual-image)   │
│  Ethernet: Gigabit (CPSW + DP83869 PHY)     │
│  Serial: UART0 (debug) + UART1 (RPi5 link)  │
│  RTC: DS3231MZ+ (I2C, coin cell backup)      │
│  LEDs: Power (green), Heartbeat (green flash)│
│        Fault (red)                           │
│  Power: 5VDC input, ~1.5A                    │
│  PCB: ~80×60mm, 4+ layer                    │
│  Enclosure: DIN rail, ~90×70×40mm            │
└──────────────────────────────────────────────┘
```

### Orbit at a Glance

```
┌──────────────────────────────────────────────┐
│  ORBIT I/O MODULE                            │
│  ────────────────                            │
│  MCU: STM32F091CCT6 (Cortex-M0, 48 MHz)     │
│  Flash: 256 KB (32 KB boot + 216 KB app)      │
│  SRAM: 32 KB                                  │
│  Ethernet: W5500 (SPI, 100 Mbps)            │
│  Power: 24VDC input, 24V→5V→3.3V, ~2.0W     │
│  DO: 10× 24VDC open-collector sink (ULN2803A)│
│      500mA/ch max, pluggable 20-pos terminal │
│      PTC fuse per driver bank (2.5A hold)    │
│      5A board-level input fuse               │
│  DI: 10× optocoupled, 24VDC                 │
│  AO: 2× 4-20mA (MCP4822 DAC + XTR111)      │
│  RS485 A: MAX3485 (socketed), 9600 default   │
│           Modbus RTU master → analog boards    │
│  RS485 B: MAX3485 (socketed), 19200 default  │
│  LEDs: Green=DO active/DI closed             │
│        Blue=RS485 bus activity               │
│  Address: 6-pos DIP switch (192.168.0.{DIP+1})│
│  PCB: 114×140mm (4.5" × 5.5"), 2-4 layer    │
│  Enclosure: DIN rail, ~124×150×50mm          │
│  BOM: ~$25                                   │
└──────────────────────────────────────────────┘
```
