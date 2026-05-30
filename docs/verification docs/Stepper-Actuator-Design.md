# Constellation Actuator Position Controller — Design & BOM

**Purpose:** Modbus RTU position controller retrofit for existing 24VDC linear actuators used on cold storage doors (7'×7', ~200 lb) and fresh air dampers.  
**Approach:** Add-on controller board only — the existing actuators already contain a 10kΩ position potentiometer and end-of-travel limit switches. The controller reads position, drives the motor via H-bridge, and exposes everything over Modbus RTU.  
**Interface:** Modbus RTU slave on RS485 (connects to Orbit Port B or daisy-chained on existing bus).  
**Environment:** -20°C to +60°C, up to 95% RH, vented steel cabinets or outdoor-exposed door frames.

---

## 1. Existing Actuator Baseline

The sites currently use commercial 24VDC electric linear actuators with these characteristics:

| Parameter | Value |
|---|---|
| **Supply** | 24VDC |
| **Push force** | 1000 lbf rated |
| **Current draw** | <4A (continuous duty) |
| **Duty cycle** | Continuous — can hold under load 24/7 |
| **Stroke** | 36" |
| **Self-locking** | Yes — worm gear / ACME screw internal to actuator |
| **Position potentiometer** | Built-in 10kΩ linear pot — 3 wires: Vcc, signal, GND |
| **Limit switches** | Built-in at both ends of travel — also prevent reverse polarity (motor cannot be driven past end stops) |
| **Unit cost** | ~$500 |
| **Lifetime target** | 5–10 years in cold storage environment |

**Current control method:** Orbit timed-output registers (§4.9 in design doc) pulse the actuator open/close for a calculated number of seconds based on a UI-configurable full stroke time. The built-in pot and limit switches are not currently wired back to the control panel — only the 2-wire motor power is connected. This is time-proportional positioning — no feedback, position drifts over time, requires periodic full-close to re-zero.

**Problem:** The actuator already has position feedback hardware inside it — it's just not being used. Adding 3 wires from the pot and connecting to a controller board gives us closed-loop position control.

---

## 2. Design Overview

Instead of replacing the actuator, add a **position controller module** that:
1. **Reads the actuator's built-in 10kΩ pot** via 3 wires brought back from the actuator
2. **Reads the actuator's built-in limit switches** — already wired internally, exposed on the actuator cable
3. **Drives the actuator motor** via an H-bridge (polarity reversal for extend/retract)
4. **Accepts Modbus RTU commands** — "go to 35.5% open" — and drives to position using closed-loop control
5. **Reports actual position** back over Modbus for Nova to read and display in the UI

```
Orbit Port B (RS485)
    │
    ▼
┌──────────────────────────────────────────────────────────┐
│           Position Controller Module                      │
│                                                          │
│  ┌──────────┐   ┌──────────┐   ┌─────────────────────┐  │
│  │STM32F030 │──►│ BTS7960  │──►│ Existing 24V DC     │  │
│  │          │   │ H-bridge │   │ Linear Actuator      │  │
│  │ Modbus   │   │ 43A cont │   │ (1000 lbf, <4A)     │  │
│  │ RTU      │   └──────────┘   │                     │  │
│  │ slave    │                   │  Built-in:          │  │
│  │          │◄──────────────────│  • 10kΩ pot (3-wire)│  │
│  │          │                   │  • Limit switches   │  │
│  │          │◄── Current sense (INA180, inline shunt) │  │
│  │          │                   └─────────────────────┘  │
│  └──────────┘                                            │
│       │                                                  │
│    MAX3485                                               │
│       │                                                  │
│    RS485 A/B                                             │
└──────────────────────────────────────────────────────────┘
```

The actuator's motor + pot + limit switch wires all route through the controller instead of directly to the Orbit DO. The controller reads position from the actuator's internal pot, respects its internal limit switches, and accepts Modbus position commands instead of timed pulses. **Total wiring from actuator to controller: 7 wires** — 2 motor, 3 pot (Vcc/signal/GND), 2 limit switches (already connected to motor internally for reverse-polarity protection — exposed as status inputs to the controller).

---

## 3. Load Analysis

### 3.1 Door Specifications

| Parameter | Value | Calculation |
|---|---|---|
| **Door size** | 7' × 7' (2.13m × 2.13m) | 49 sq ft (4.55 m²) |
| **Door weight** | ~200 lb (91 kg) | Insulated cold storage door |
| **Wind speed** | 80 mph (36 m/s) design max | |
| **Wind pressure** | ~16.4 psf (785 Pa) | $q = 0.00256 \times V^2 = 0.00256 \times 80^2 = 16.4$ psf |
| **Wind force on door** | ~803 lbf | $16.4 \text{ psf} \times 49 \text{ sq ft} = 803$ lbf |
| **Actuator force required** | ~500 lbf at actuator | Depends on hinge geometry and linkage mechanical advantage. Actuator rated at 1000 lbf provides 2:1 safety factor. |

### 3.2 Operating Scenarios

| Scenario | Force | Current | Duration | Frequency |
|---|---|---|---|---|
| **Opening against spring/gravity** | 200–300 lbf | ~2A | 30–60 sec | 2–20× per day |
| **Closing against wind** | Up to 500 lbf | ~3A | 30–60 sec | 2–20× per day |
| **Holding open (calm)** | 0 lbf (self-locking) | 0A | Hours to days | Continuous summer |
| **Holding closed against wind** | Up to 803 lbf | 0A (self-locking) | Hours | Storm events |
| **Holding open in wind** | Up to 803 lbf | 0A (self-locking) | Hours | Ventilation in wind |

**Key insight:** The actuator's internal worm gear / ACME screw is self-locking. It holds position with zero current in all scenarios — wind, gravity, spring load. The motor only draws current during movement. This is why the existing actuators survive 24/7 duty and last 5–10 years — the motor isn't running continuously.

---

## 4. Built-In Position Sensor

The existing actuators already contain a **10kΩ linear potentiometer** that tracks the push rod position internally. This eliminates the need for any external position sensor.

| Parameter | Value |
|---|---|
| **Type** | Built-in linear potentiometer (inside actuator housing) |
| **Resistance** | 10kΩ |
| **Output** | Ratiometric — voltage divider output proportional to rod position |
| **Wiring** | 3 wires: Vcc (excitation), Signal (wiper), GND |
| **Linearity** | Typically ±1–2% (manufacturer spec varies) — adequate for door positioning |
| **Resolution** | Effectively infinite (analog) |
| **Life** | Matched to actuator life (5–10 years) — the pot is the actuator's built-in component |
| **Environment** | Sealed inside actuator housing — no additional environmental protection needed |
| **Installation** | Already there — just bring 3 wires back to the controller |
| **Cost** | **$0** — already included in the $500 actuator |

The actuator also contains **built-in limit switches** at both ends of travel that prevent reverse polarity (the motor cannot be driven past the end stops). These are already wired internally to the motor circuit and protect the actuator mechanically. The controller reads the limit switch state as digital inputs for end-of-travel confirmation and safety backup.

**What needs to change in the field:** Currently only 2 wires (motor +/−) are run from the control panel to the actuator. To add position control, run a **7-conductor cable** instead: 2 motor + 3 pot + 2 limit switch status. If the actuator cable already carries the pot and limit switch wires (just not connected at the panel end), it's a matter of terminating them at the controller.

---

## 5. Controller PCB

Small board, ~50×80mm, mounts in a DIN-rail enclosure or a weatherproof junction box near the actuator.

### 5.1 Schematic Block Diagram

```
+24V ──┬───────────────────────────────────────── BTS7960 H-bridge ──► Actuator motor (2-wire)
       │                                              │ │
       │                                         RPWM │ │ LPWM
       │                                              │ │
       ├── TPS54302 (24V→5V) ── AMS1117-3.3 ── STM32F030C8T6
       │                                          │ │ │ │
       │                              UART1(TX/RX)│ │ │ │ADC (PA0)
       │                                          │ │ │ │
       │                              MAX3485 ◄───┘ │ │ └──► Actuator pot signal (0–3.3V via divider)
       │                                 │          │ │
       │                              RS485 A/B     │ │ADC (PA1)
       │                                            │ └──► Current sense (INA180 output)
       │                                            │
       │                                      GPIO: Actuator limit switches (2×), LED status
       │                                            │
       │                                      GPIO: R_EN, L_EN (H-bridge enable)
       │
       └── 0.005Ω shunt ── INA180 ── current feedback to ADC
```

### 5.2 H-Bridge Selection: BTS7960

| Parameter | Value | Notes |
|---|---|---|
| **Part** | BTS7960 (half-bridge) × 2, or IBT-2 module | Full H-bridge = 2× BTS7960 ICs |
| **Continuous current** | 43A per IC | Massive headroom for a 4A actuator |
| **Supply voltage** | 5.5–27.5V | Covers 24V system |
| **RDS(on)** | 16 mΩ | Power dissipation at 4A: $P = I^2 \times R_{DS} \times 2 = 16 \times 0.016 \times 2 = 0.51W$ — no heatsink needed |
| **Protection** | Over-temp, over-current, under-voltage | Built into the IC |
| **Control** | RPWM + LPWM (one per direction) + R_EN + L_EN | PWM for soft start/stop, or just digital on/off |
| **Cost** | ~$3.00 for 2× BTS7960 ICs, or ~$5 for pre-built IBT-2 module | |

**Why BTS7960:** It's the standard for driving DC linear actuators at 24V. The 43A rating means the 4A actuator load is trivial — 10% of capacity. No thermal concerns even in a sealed enclosure. Built-in protections handle stall current (which can spike to 8–12A briefly on actuator reversal).

### 5.3 Current Sensing

| Parameter | Value |
|---|---|
| **Shunt resistor** | 0.005Ω, 3W, Kelvin sense |
| **Amplifier** | INA180A3 (gain = 100 V/V) |
| **At 4A** | Shunt voltage = 20mV → amplified = 2.0V → ADC reads ~2482/4096 |
| **At 8A (stall)** | Shunt voltage = 40mV → amplified = 4.0V → clamp at 3.3V (ADC saturates = stall detected) |
| **At 0A (idle)** | 0V → ADC reads ~0 (confirms motor stopped) |

Current sensing enables:
- **Stall detection** — if current exceeds threshold for >2 seconds, stop motor and report error
- **End-of-travel detection** — current spike when actuator hits mechanical stop (backup to limit switches)
- **Motor health monitoring** — gradual increase in running current indicates wear or binding
- **Power consumption reporting** — Nova can log energy usage per door

### 5.4 Controller BOM

| Ref | Part | Description | Qty | Unit Cost | Ext Cost |
|---|---|---|---|---|---|
| U1 | STM32F030C8T6 | Cortex-M0, 48 MHz, 64 KB flash, 8 KB SRAM, LQFP-48 | 1 | $1.50 | $1.50 |
| U2a,b | BTS7960 | Half-bridge driver, 43A, SO-16 (2 ICs = full H-bridge) | 2 | $1.50 | $3.00 |
| U3 | MAX3485ESA | RS485 transceiver, 3.3V, half-duplex | 1 | $0.80 | $0.80 |
| U4 | TPS54302 | 24V→5V buck, 3A, SOT-23-6 | 1 | $0.90 | $0.90 |
| U5 | AMS1117-3.3 | 3.3V LDO, 1A, SOT-223 | 1 | $0.15 | $0.15 |
| U6 | INA180A3 | Current sense amp, gain=100, SOT-23-5 | 1 | $0.80 | $0.80 |
| R_shunt | 0.005Ω 3W | Kelvin-sense current shunt | 1 | $0.50 | $0.50 |
| — | Passives | Caps, resistors, inductor for buck, TVS, voltage divider for pot input | ~30 pcs | — | $1.50 |
| J1 | Screw terminals | RS485 (2-pos) + 24V power (2-pos) + actuator motor (2-pos) + pot (3-pos) + limit SW (2-pos) | 5 | $0.40 | $2.00 |
| SW1 | DIP switch | 4-bit address select | 1 | $0.20 | $0.20 |
| LED | Status LEDs | Green (power), Blue (comms), Red (fault), Yellow (moving) | 4 | $0.05 | $0.20 |
| — | PCB | 50×80mm, 2-layer, ENIG, FR-4 1.6mm | 1 | $1.00 | $1.00 |
| — | Conformal coating | Acrylic, IPC-CC-830C | 1 | $0.50 | $0.50 |
| | | | | **Controller PCB subtotal** | **$12.85** |

---

## 6. Enclosure & Installation Hardware

| Ref | Part | Description | Qty | Unit Cost | Ext Cost |
|---|---|---|---|---|---|
| ENC | Junction box | Polycarbonate, IP66, ~120×80×55mm, DIN-rail mount option | 1 | $8.00 | $8.00 |
| — | Cable glands | PG7 or M16, for power/RS485/actuator cable entries (3 entries) | 3 | $0.50 | $1.50 |
| — | DIN rail clip | 35mm, snap-on (if DIN mounted in panel) | 1 | $0.50 | $0.50 |
| — | Actuator cable | 7-conductor shielded, 18 AWG (2 motor + 3 pot + 2 limit SW), field length | 1 | $3.00 | $3.00 |
| | | | | **Enclosure/install subtotal** | **$13.00** |

---

## 7. Assembly & Overhead

| Item | Cost | Notes |
|---|---|---|
| PCB assembly (PCBA) | $5.00 | Pick-and-place + reflow + test, at qty 25+ |
| Controller test | $2.00 | Automated: Modbus comms, H-bridge direction, ADC cal, current sense |
| Firmware flash | $0.50 | SWD programming during PCBA |
| Kit packaging | $1.00 | Controller in enclosure + actuator cable + wiring diagram |
| | **Assembly subtotal** | **$8.50** |

---

## 8. Total Unit Cost Summary

| Category | Cost |
|---|---|
| Existing actuator (purchased separately, already in budget) | $500.00 |
| Controller PCB (populated) | $12.85 |
| Position sensor | $0 (built into actuator) |
| Limit switches | $0 (built into actuator) |
| Enclosure & installation hardware | $13.00 |
| Assembly & overhead | $8.50 |
| **Position controller add-on total** | **$34.35** |
| **Complete system (actuator + controller)** | **$534.35** |

### Volume pricing estimates (controller add-on only)

| Quantity | Unit Cost | Notes |
|---|---|---|
| 1–9 (prototype) | ~$50–60 | No volume discounts on PCBA or enclosure |
| 10–24 | ~$40–50 | Moderate PCBA volume pricing |
| **25–49** | **~$35–40** | PCBA + enclosure volume breaks |
| 50–99 | ~$30–35 | Sweet spot — all components at volume |
| 100+ | ~$25–30 | Full production pricing |

---

## 9. Cost-Benefit Analysis

### What the ~$35 add-on buys you

| Capability | Without controller (current) | With controller (~$35 add-on) |
|---|---|---|
| **Position accuracy** | ±2–5% (time-proportional drift) | ±1–2% (closed-loop, actuator's built-in pot) |
| **Position knowledge** | None — Nova guesses based on pulse timing | Real-time actual position in Modbus register |
| **Stall / obstruction detection** | None — motor runs until timeout | Current sensing + position stall → alarm in <2 sec |
| **Door-closed confirmation** | None — assume closed after full-close pulse | Pot confirms position = 0% + limit switch = digital certainty |
| **Partial positioning** | Unreliable below ~5% | Reliable at any increment (e.g., 1.5% crack for fresh air) |
| **Motor health monitoring** | None — fails silently until door won't move | Running current trend → predictive maintenance |
| **Re-zeroing** | Periodic full-close pulse required | Not needed — pot is absolute reference |
| **Soft start / soft stop** | No — full voltage on/off | PWM ramp → less mechanical shock, longer actuator life |
| **Orbit DOs consumed** | 2 per actuator (OPEN + CLOSE) | 0 — Modbus on Port B, frees DOs for other uses |
| **Wiring to actuator** | 2 wires (motor only) — pot and limit switches unused | 7 wires (motor + pot + limit switches — all utilized) |

### Payback

- **$35 per door** — trivial cost for closed-loop position control
- **Eliminates 2 DOs per door** from the Orbit — a site with 4 doors recovers 8 DOs (nearly a full Orbit module worth of outputs)
- **Prevents freeze events** from undetected door-stuck-open — a single freeze event on a potato storage room can destroy $50,000–$200,000 of product
- **Extends actuator life** with soft start/stop and stall protection — if it avoids even one premature actuator replacement ($500), the controller has paid for itself 15× over

---

## 10. Modbus Register Map

| Register | Address | Type | Description |
|---|---|---|---|
| Target position | 0x0000 | Holding (FC 06/10) | 0–10000 = 0.00–100.00% of stroke |
| Current position | 0x0001 | Input (FC 04) | 0–10000 = 0.00–100.00% of stroke (from sensor) |
| Motor speed | 0x0002 | Holding (FC 06) | PWM duty 0–1000 = 0.0–100.0% (default 1000 = full speed). Reduce for soft operation. |
| Status | 0x0003 | Input (FC 04) | 0=idle, 1=extending, 2=retracting, 3=stalled, 4=at-target, 5=limit-hit, 6=overcurrent |
| Command | 0x0004 | Holding (FC 06) | 0=nop, 1=go-to-target, 2=full-close (home), 3=stop, 4=full-open |
| Deadband | 0x0005 | Holding (FC 06) | Position deadband in 0.01% units (default 50 = 0.50%). Motor stops when within deadband of target. Prevents hunting. |
| Stall current | 0x0006 | Holding (FC 06) | Overcurrent threshold in mA (default 6000 = 6A). Motor stops if exceeded for >2 sec. |
| Stall timeout | 0x0007 | Holding (FC 06) | Seconds with no position change before stall alarm (default 5). |
| Motor current | 0x0008 | Input (FC 04) | Actual motor current in mA (real-time) |
| Position raw | 0x0009 | Input (FC 04) | Raw ADC value from sensor (0–4095) — for calibration / diagnostics |
| Cal low | 0x000A | Holding (FC 06) | ADC value at fully closed position (default 100). Persisted in flash. Set during installation. |
| Cal high | 0x000B | Holding (FC 06) | ADC value at fully open position (default 3990). Persisted in flash. Set during installation. |
| RS485 address | 0x0010 | Holding (FC 06) | Modbus slave address (1–247, default 10). Persisted in flash. |
| Baud rate | 0x0011 | Holding (FC 06) | 0=9600, 1=19200, 2=38400, 3=57600 (default 1=19200) |
| Firmware version | 0x0020 | Input (FC 04) | Major.minor encoded as (major×100 + minor) |
| Error code | 0x0021 | Input (FC 04) | 0=none, 1=stall, 2=overcurrent, 3=sensor-fault, 4=limit-switch, 5=over-temp |
| Cycle count | 0x0022–0x0023 | Input (FC 04) | 32-bit total full-stroke-equivalent cycles (for maintenance tracking) |
| Operating hours | 0x0024–0x0025 | Input (FC 04) | 32-bit motor-running hours (for maintenance tracking) |

### Calibration Procedure (one-time at installation)

```
1. Wire actuator motor to controller M+ / M-
2. Wire actuator pot to controller SENSOR (Vcc / Signal / GND) — 3 wires from actuator
3. Wire actuator limit switches to controller LIM CLOSE / LIM OPEN (2 wires from actuator)
4. Wire RS485 to Orbit Port B bus
5. Power on — controller appears on Modbus at default address 10
6. Command = 2 (full-close) — actuator retracts fully (limit switch confirms)
7. Read register 0x0009 (raw ADC) — record value → write to 0x000A (Cal low)
8. Command = 4 (full-open) — actuator extends fully (limit switch confirms)
9. Read register 0x0009 (raw ADC) — record value → write to 0x000B (Cal high)
10. Done — position reads 0.00–100.00% automatically from now on
```

Nova can automate this calibration sequence — run it once at commissioning.

---

## 11. Firmware Outline (STM32F030)

```
main():
    HAL_Init()
    SystemClock_Config(48MHz)
    ADC_Init(CH0=position_sensor, CH1=current_sense)
    PWM_Init(TIM3, CH1=RPWM, CH2=LPWM)  // H-bridge control
    GPIO_Init(R_EN, L_EN, LIMIT_CLOSE, LIMIT_OPEN, LEDs)
    Modbus_RTU_Init(UART1, address=dip_switch_read(), baud=19200)
    Load_Calibration_From_Flash()
    
    while(1):
        Modbus_Poll()
        Position_Update()       // Read ADC, compute calibrated %
        Current_Update()        // Read current sense ADC
        Motor_Control()         // PID-ish closed-loop position control
        Safety_Check()          // Stall, overcurrent, limit switches

Position_Update():  // Called every 50ms
    raw_adc = ADC_Read(CH0)
    // Linear interpolation between cal_low and cal_high
    position_pct = (raw_adc - cal_low) * 10000 / (cal_high - cal_low)
    position_pct = clamp(position_pct, 0, 10000)

Motor_Control():
    error = target_position - current_position
    
    if (abs(error) < deadband):
        motor_stop()
        status = AT_TARGET
        return
    
    if (error > 0):
        // Need to extend (open)
        motor_extend(speed_pwm)
        status = EXTENDING
    else:
        // Need to retract (close)
        motor_retract(speed_pwm)
        status = RETRACTING
    
    // Soft approach: reduce PWM when close to target
    if (abs(error) < 200):  // Within 2% of target
        speed_pwm = speed_pwm / 2  // Half speed for precision

Safety_Check():
    // Overcurrent
    if (motor_current > stall_current_threshold && motor_running):
        motor_stop()
        status = OVERCURRENT
        error_code = 2
    
    // Stall detection (motor running but position not changing)
    if (motor_running && position_unchanged_for(stall_timeout)):
        motor_stop()
        status = STALLED
        error_code = 1
    
    // Limit switches (hard stop safety)
    if (LIMIT_CLOSE_pressed && direction == RETRACTING):
        motor_stop()
    if (LIMIT_OPEN_pressed && direction == EXTENDING):
        motor_stop()
    
    // Sensor fault (ADC reading out of expected range)
    if (raw_adc < 10 || raw_adc > 4085):
        motor_stop()
        error_code = 3  // Sensor disconnected or shorted
```

---

## 12. Wiring Diagram

```
                     POSITION CONTROLLER
                   ┌─────────────────────┐
Orbit Port B       │                     │
RS485 A ───────────┤ RS485 A             │
RS485 B ───────────┤ RS485 B             │
                   │                     │           EXISTING ACTUATOR
24VDC Supply       │                     │          ┌──────────────────┐
+24V ──────────────┤ +24V         M+  ───┼──────────┤ Motor +         │
GND ───────────────┤ GND          M-  ───┼──────────┤ Motor -         │
                   │                     │          │                  │
                   │       POT Vcc  ────┼──────────┤ Pot Vcc (red)   │
                   │       POT Sig  ────┼──────────┤ Pot Signal (wht)│
                   │       POT GND  ────┼──────────┤ Pot GND (blk)   │
                   │                     │          │                  │
                   │       LIM CLOSE ───┼──────────┤ Limit SW Close  │
                   │       LIM OPEN  ───┼──────────┤ Limit SW Open   │
                   │                     │          └──────────────────┘
                   │    LEDs:  ● Green   │ Power OK
                   │           ● Blue    │ Modbus activity
                   │           ● Yellow  │ Motor running
                   │           ● Red     │ Fault
                   └─────────────────────┘
```

**7-conductor actuator cable:** All signals from the actuator (motor, pot, limit switches) run through a single 7-conductor shielded cable to the controller. This replaces the existing 2-wire motor-only run.

**Installation notes:**
- Controller mounts near the actuator in an IP66 junction box (or in the control panel if within cable distance)
- All position and limit switch signals come from the actuator itself — no external sensors or switches to mount on the door frame
- RS485 daisy-chains: multiple door actuators on the same Port B bus (each with unique Modbus address)
- Existing actuator wiring: replace 2-wire motor cable with 7-conductor cable (motor + pot + limit switches)

---

## 13. Comparison to Current Approach

| | Current (timed pulse via Orbit DO) | With position controller add-on |
|---|---|---|
| **Actuator cost** | $500 | $500 (same actuator) |
| **Controller cost** | $0 (uses Orbit DOs) | ~$35 add-on |
| **Total per door** | $500 | ~$535 |
| **Position accuracy** | ±2–5% (drifts, requires re-zeroing) | ±1–2% (closed-loop, actuator's built-in pot) |
| **Position feedback** | None | Real-time % in Modbus register + UI |
| **Stall detection** | None | Current sense + position stall → alarm |
| **Door-closed confirmation** | Assumed | Measured (pot + limit switch) |
| **Soft start/stop** | No (bang-bang) | Yes (PWM ramp) |
| **Orbit DOs consumed** | 2 per door (OPEN + CLOSE) | 0 (Modbus RTU on Port B) |
| **Orbit needed for doors** | Dedicated door Orbit module for >5 doors | Port B handles up to 32 actuators on bus |
| **Freeze event risk** | High (undetected open door) | Low (position alarm + auto-close) |
| **Motor lifetime** | Shorter (hard start/stop, no stall protection) | Longer (soft start, stall cutoff) |

---

## 14. Multi-Actuator RS485 Bus Layout

A single Orbit Port B can drive multiple door actuator controllers on one RS485 bus:

```
Orbit Module (Door Control)
Port B (RS485, 19200 baud)
    │
    ├── Actuator Controller #1 (addr 10) ── Door 1 North
    ├── Actuator Controller #2 (addr 11) ── Door 1 South  
    ├── Actuator Controller #3 (addr 12) ── Door 2 North
    ├── Actuator Controller #4 (addr 13) ── Door 2 South
    ├── Actuator Controller #5 (addr 14) ── Fresh Air Damper 1
    └── Actuator Controller #6 (addr 15) ── Fresh Air Damper 2
```

**Bus capacity:** Up to 32 devices per RS485 segment (MAX3485 spec). A typical site with 4 doors + 2 dampers = 6 controllers — well within limits.

**Poll rate:** At 19200 baud, a position read (6 bytes TX + 7 bytes RX) takes ~7ms per device. 6 devices × 7ms = 42ms cycle. Nova gets full position updates from all doors every ~50ms — faster than the actuators can move.

---

## 15. Risk & Mitigation

| Risk | Impact | Mitigation |
|---|---|---|
| Actuator pot wear over 10 years | Drift, noisy signal | Pot is matched to actuator life. Current sensing provides backup position estimation (integration). Replacement = replace actuator (scheduled maintenance). |
| Pot wiring breaks / disconnects | Loss of position feedback | Firmware detects ADC out-of-range → raises sensor fault alarm, stops motor. Limit switches still work as backup end stops. |
| H-bridge failure (short) | Motor runs continuously | BTS7960 has over-temp and over-current shutdown built in. Firmware watchdog also kills PWM if position not changing as expected. |
| Condensation on PCB | Short circuit | Conformal coating (ENIG + acrylic, same spec as Orbit boards). IP66 enclosure. |
| Actuator motor brush wear (5–10 yr) | Motor failure | Operating hours register enables predictive replacement. Current trend monitoring detects increasing friction before total failure. |
| Wind gust during positioning | Position overshoot | Closed-loop control corrects continuously. Deadband prevents hunting. Current limiting prevents motor damage. |
| RS485 cable damage | Loss of control | Orbit comms-loss watchdog fires → actuator motor stops → self-locking holds position. Door stays where it is until comms restored or tech intervenes. |
| Power loss during movement | Door partially open | Actuator self-locking holds position. On power restore, controller reads pot, resumes to target position. |
| 7-conductor cable damage | Partial loss of signals | Shielded cable with strain relief at both ends. Motor still works with just 2 wires — fallback to timed-pulse mode via Orbit DOs if controller fails. |

---

## 16. Development Phases

| Phase | Deliverable | Est. Cost |
|---|---|---|
| **1. Breadboard prototype** | STM32 dev board + BTS7960 module + existing actuator on bench. Wire up pot and limit switches. Validate position control loop, Modbus comms, current sensing. | ~$80 (dev board + BTS7960 module) |
| **2. Controller PCB v1** | First PCBA run (5 pcs). Firmware: closed-loop position, Modbus registers, stall detect, calibration, limit switches. | ~$150 (PCB fab + assembly + components) |
| **3. Field trial** | Install on one real door. 30-day test: position accuracy, reliability, environmental. | ~$50 (7-conductor cable, junction box, field time) |
| **4. Revision + production batch** | PCB v2 if needed. First batch of 10 controllers + enclosures + cables. | ~$350 (10 × $35) |
| | **Total to first production** | **~$630** |

---

## 17. Key Sourcing Notes

- **BTS7960:** Infineon. Available as bare ICs on LCSC/Mouser (~$1.50 each) or as pre-built IBT-2 modules from Amazon/AliExpress (~$5 each, good for prototyping).
- **STM32F030C8T6:** ST Micro. $1.30–1.80 depending on qty. LCSC, Mouser, DigiKey.
- **INA180A3:** TI. $0.60–1.00. Mouser, DigiKey.
- **IP66 enclosures:** Bud Industries, Hammond, Polycase. $5–12 depending on size and material.
- **7-conductor shielded cable:** Alpha Wire, Belden, or bulk from industrial distributors. 18 AWG shielded, $0.50–1.00/ft.
- **Cable glands:** Bimed, Heyco, Jacob. $0.30–0.80 each in bulk.

---

## 18. Future Enhancements (Not in v1)

- **Built-in temperature sensor** (thermistor on PCB) — monitor controller and ambient temp, report via Modbus
- **De-ice mode** — on command from Nova, cycle door open/close slowly to break ice seal before full operation
- **Position logging** — store last 100 positions with timestamps in flash (ring buffer) for post-event analysis
- **Auto-calibration** — on command, drive to both limits and record cal values automatically
- **Dual-actuator synchronization** — two controllers on same bus coordinating to open a large bi-parting door evenly
