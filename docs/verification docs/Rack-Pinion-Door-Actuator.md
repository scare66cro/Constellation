# Stepper-Driven Rack & Pinion Door Actuator — Design & BOM

**Purpose:** Replace $500 commercial linear actuators with a stepper motor + worm gearbox + rack & pinion mechanism for hinged storage doors (side-hinged or top-hinged).  
**Approach:** NEMA 23 stepper motor → NMRV030 worm gearbox (30:1, self-locking) → mod 2 pinion → steel rack bolted to door. Open-loop position via step counting, stall detection via TMC2209 StallGuard.  
**Interface:** Modbus RTU slave on RS485 (connects to Orbit Port B). Up to 8 doors per controller board.  
**Environment:** -20°C to +60°C, up to 95% RH, outdoor-exposed door frames, 80 mph wind design load.

---

## 1. Why Rack & Pinion

| Problem with linear actuators | How rack & pinion solves it |
|---|---|
| $500 per door | ~$90 per door (mechanism + share of controller) |
| Force varies through hinge arc (moment arm changes) | Constant torque at pinion = consistent force throughout swing |
| Motor must stall or brake to hold position under wind | Worm gearbox is **self-locking** — holds position with zero power |
| Fixed stroke length, fixed mount points | Rack can be any length, follows door arc naturally |
| No position feedback unless wired (pot unused today) | Stepper step-counting = exact position, no sensor needed |
| 2 DOs per door consumed on Orbit | 0 DOs — Modbus RTU over RS485 |

A competitor is already using rack and pinion in Idaho. Previous pneumatic attempts failed due to inability to hold partial positions (air is compressible). The worm gearbox eliminates this — it physically cannot be back-driven.

---

## 2. Door Specifications

| Parameter | Value |
|---|---|
| **Door size** | 7' × 7' (2.13m × 2.13m), 49 sq ft |
| **Door weight** | ~200 lb (91 kg), insulated |
| **Hinge type** | Side-hinged or top-hinged (same mechanism, different mount orientation) |
| **Wind design speed** | 80 mph (36 m/s) |
| **Wind pressure** | 16.4 psf ($q = 0.00256 \times 80^2$) |
| **Wind force on door** | ~803 lbf ($16.4 \times 49$) |
| **Full stroke** | ~36" of linear rack travel (90° door swing) |
| **Current actuator time** | 90 seconds full stroke |
| **Desired speed** | ≤90 seconds (match or beat current) |
| **Doors per bay** | Up to 8 (onion curing) |
| **Cycles per day** | 2–20 (seasonal dependent) |
| **Lifetime** | 5–10 years outdoor service |

---

## 3. Mechanical Design

### 3.1 Assembly Overview

```
Door (hinged at side or top)
    │
    │  Steel rack (bolted to door face, follows arc of swing)
    │
    ├──── Mod 2 steel pinion (on gearbox output shaft)
    │         │
    │     NMRV030 worm gearbox (30:1, self-locking)
    │         │
    │     NEMA 23 stepper motor (via input flange adapter)
    │         │
    │     TMC2209 stepper driver (on controller PCB)
    │         │
    │     STM32F030 controller → RS485 → Orbit Port B
    │
Frame (fixed mount for gearbox + motor)
```

### 3.2 Mounting Configurations

**Side-hinged door:**
```
    ┌─────────────────────────────┐
    │                             │
    │         DOOR                │
    │                             │
    │    Rack (horizontal)────────┤ ← Hinge axis (vertical)
    │                             │
    └─────────────────────────────┘
         ▲
    Pinion + Gearbox + Motor
    (mounted on door frame)
```

**Top-hinged door:**
```
    ═══════════════════════════════ ← Hinge axis (horizontal, at top)
    │                             │
    │         DOOR                │
    │                             │
    │    Rack (vertical) ─────────┤
    │                             │
    └─────────────────────────────┘
         ▲
    Pinion + Gearbox + Motor
    (mounted on door frame)
```

In both cases the rack is bolted to the door face and the motor/gearbox assembly is fixed to the building frame. The rack slides linearly past the pinion as the door swings.

---

## 4. Drive Train Analysis

### 4.1 Stepper Motor Selection

**StepperOnline 23HS30-2804S** (primary recommendation)

| Parameter | Value |
|---|---|
| **Frame** | NEMA 23 (57×57mm) |
| **Body length** | 76mm |
| **Holding torque** | 1.26 N·m (178 oz·in) |
| **Rated current** | 2.8A/phase |
| **Resistance** | 0.9Ω/phase |
| **Inductance** | 2.5mH/phase |
| **Step angle** | 1.8° (200 steps/rev) |
| **Shaft** | **8mm diameter, 20mm length, D-cut flat** |
| **Wires** | 4-wire bipolar |
| **Weight** | 0.7 kg |
| **Price** | ~$15–18 |

**Upgrade option: StepperOnline 23HS45-4204S** (if more torque margin needed)

| Parameter | Value |
|---|---|
| **Holding torque** | 3.0 N·m (425 oz·in) |
| **Rated current** | 4.2A/phase (current-limit to 2.0A RMS on TMC2209) |
| **Body length** | 112mm |
| **Shaft** | 8mm diameter, 20mm length, D-cut flat (same as above) |
| **Price** | ~$20–25 |

### 4.2 Worm Gearbox -

**NMRV030, 30:1 ratio, with NEMA 23 input flange**

| Parameter | Value |
|---|---|
| **Model** | NMRV030 (standard Chinese/European worm reducer) |
| **Ratio** | 30:1 |
| **Input** | NEMA 23 flange adapter (included in kit), 8mm bore bushing |
| **Output shaft** | **14mm diameter, solid, 5×5mm keyway, 30mm protrusion** |
| **Self-locking** | **Yes** — at 30:1 the worm cannot be back-driven |
| **Rated input speed** | 1400 RPM (our use: <500 RPM) |
| **Efficiency** | ~45–55% (typical for 30:1 worm) |
| **Housing** | Aluminum alloy, 4× M6 face-mount holes, 55mm bolt pattern |
| **Weight** | ~1.2 kg |
| **Price** | ~$22–30 (with NEMA 23 flange adapter kit) |

Search terms for sourcing:
- eBay/AliExpress: **"NMRV030 worm gearbox 30:1 NEMA23 input flange"**
- Amazon: **"Happybuy NMRV030 30:1"** or **"Befenybay NMRV030"**
- Automation Direct or McMaster for US-sourced

Most NMRV030 kits for NEMA 23 include: gearbox body, input shaft adapter/coupling (8mm D-cut bore), NEMA 23 mounting flange plate. The output shaft is integral.

### 4.3 Pinion Gear

**Module 2, 20 teeth, 14mm bore with keyway**

| Parameter | Value |
|---|---|
| **Module** | 2 |
| **Teeth** | 20 |
| **Pitch diameter** | 40mm |
| **Outside diameter** | 44mm |
| **Bore** | **14mm with 5×5mm keyway** (matches NMRV030 output shaft) |
| **Pressure angle** | 20° |
| **Face width** | 20mm |
| **Material** | C45 steel or S45C (hardened preferred) |
| **Price** | ~$8–12 |

Sources:
- KHK Gears (khkgears.net): SS2-20 — specify bore to 14mm with keyway
- SDP-SI (sdp-si.com): gear configurator, module 2, 20T, 14mm bore
- AliExpress/eBay: **"module 2 spur gear 20 teeth 14mm bore keyway"**

### 4.4 Rack

**Module 2, 20° pressure angle, steel, 1000mm length**

| Parameter | Value |
|---|---|
| **Module** | 2 |
| **Pressure angle** | 20° (must match pinion) |
| **Cross section** | 20mm × 20mm with gear teeth on one face |
| **Length** | 1000mm standard (39.4" — covers 36" stroke with margin) |
| **Material** | C45 steel |
| **Mounting** | Holes along back face (or drill your own) |
| **Price** | ~$15–20 |

Sources:
- KHK Gears: KRG2-1000 (ground rack, mod 2, 1000mm)
- McMaster-Carr: **"metric spur gear rack module 2"** — Part# 57655K58 or similar
- AliExpress: **"module 2 gear rack 1000mm steel"**

### 4.5 Shaft Compatibility Check

```
Stepper motor shaft:     8mm D-cut
        │
        ▼
NMRV030 input adapter:   8mm bore (D-cut), included in NEMA 23 kit
        │
        ▼
NMRV030 output shaft:    14mm, 5×5mm keyway
        │
        ▼
Pinion bore:             14mm, 5×5mm keyway  ← MATCHES
        │
        ▼
Mod 2 rack:              20° pressure angle   ← MATCHES pinion
```

All shaft interfaces are standard. No custom machining required.

---

## 5. Performance Calculations

### 5.1 Speed

| Parameter | With 23HS30-2804S | With 23HS45-4204S |
|---|---|---|
| Max stepper speed (light load) | ~500 RPM | ~400 RPM |
| After 30:1 gearbox | 16.67 RPM | 13.33 RPM |
| Pinion pitch diameter | 40mm | 40mm |
| Linear speed at rack | 35 mm/s (1.38 in/s) | 28 mm/s (1.10 in/s) |
| **Time for 36" stroke** | **~26 seconds** | **~33 seconds** |

Both are significantly faster than the current 90-second actuators. In practice, firmware would limit speed for gentler operation — e.g., 250 RPM stepper = ~52 seconds for 36", still faster than current.

### 5.2 Torque / Force

| Parameter | With 23HS30-2804S | With 23HS45-4204S |
|---|---|---|
| Stepper torque at rated speed | ~0.5 N·m | ~1.0 N·m |
| After 30:1 worm (55% efficiency) | ~8.25 N·m | ~16.5 N·m |
| Force at pinion (20mm radius) | 412 N (**93 lbf**) | 825 N (**185 lbf**) |
| Stepper torque at low speed (near stall) | ~1.26 N·m | ~3.0 N·m |
| Force at pinion (low speed) | 1040 N (**234 lbf**) | 2475 N (**556 lbf**) |

For a 200 lb hinged door, the force needed at mid-point of the door to overcome gravity on a side-hinged door is roughly 100 lbf. The 23HS30-2804S handles this comfortably at moderate speed.

### 5.3 Wind Load Holding

The worm gearbox is self-locking at 30:1. When the motor is de-energized:
- **No power consumed**
- **No motor stall current**
- **Wind force cannot back-drive the mechanism**

The 803 lbf wind force on the door reaches the worm through the rack → pinion → output shaft. The worm gear's self-locking friction exceeds the back-drive force at any load. This is the key advantage over the pneumatic systems (compressible air) and the current linear actuators (holding via motor stall current).

### 5.4 Position Resolution

| Parameter | Value |
|---|---|
| Steps per motor revolution | 200 |
| Microstepping (TMC2209, 16×) | 3,200 microsteps/rev |
| Gearbox ratio | 30:1 |
| Microsteps per output revolution | 96,000 |
| Pinion circumference | 125.66mm |
| **Linear resolution** | **0.00131mm per microstep** |
| Over 36" (914mm) stroke | ~697,000 microsteps total |
| **Position granularity** | **~0.00014% of stroke per microstep** |

Far more precision than any door application requires. Practical positioning accuracy will be limited by rack/pinion backlash (~0.1mm) and gearbox play (~0.5°), yielding real-world accuracy of ~0.5–1mm (0.05–0.1% of stroke).

---

## 6. Position Feedback — Step Counting + StallGuard

### 6.1 No External Sensor Needed

The stepper motor does not have a built-in encoder. Position is tracked by counting steps in firmware:

```
position_steps = 0   (at home/closed position)

// On every step pulse:
if (direction == OPENING)
    position_steps++
else
    position_steps--

position_percent = position_steps * 10000 / total_stroke_steps
```

This is reliable as long as the motor never skips steps. With the 30:1 worm gearbox providing massive torque multiplication, the motor operates well below its stall point during normal door movement — step skipping is effectively impossible under normal load.

### 6.2 TMC2209 StallGuard4 — Sensorless Stall Detection

The TMC2209 driver has built-in StallGuard4, which measures back-EMF during motor operation. This provides:

| Function | How it works |
|---|---|
| **Obstacle detection** | Door hits a person/object → motor load spikes → StallGuard triggers → motor stops immediately. Safety without limit switches. |
| **End-of-travel detection** | Door reaches frame → mechanical stop → StallGuard detects stall → firmware marks as home position and resets step counter. No limit switches needed. |
| **Overcurrent protection** | TMC2209 limits motor current automatically. No external current sense needed. |

### 6.3 Homing Routine

On power-up, the controller doesn't know the door position (step count lost). Homing restores this:

```
1. Drive motor in CLOSE direction at low speed
2. StallGuard detects contact with door frame (stall)
3. Stop motor, reset step counter to 0
4. Position is now known — door is fully closed
5. Resume normal Modbus operation
```

This runs automatically on power-up or on command from Nova. Takes ~30–90 seconds depending on where the door was.

### 6.4 Periodic Re-Home

Over months of operation, tiny step count errors could accumulate. Firmware includes optional periodic re-homing:
- Every N cycles (configurable, default 100), on the next close command, drive all the way to frame contact instead of stopping at step count = 0
- StallGuard confirms frame contact, step counter is reset
- Eliminates long-term drift

---

## 7. Controller Electronics

### 7.1 Architecture

One controller PCB drives **up to 8 doors**. Each door has its own TMC2209 driver on the board.

```
    ┌─────────────────────────────────────────────────────────┐
    │              Door Controller PCB                         │
    │                                                         │
    │  STM32F030C8T6 (master)                                 │
    │      │                                                  │
    │      ├── UART1 → MAX3485 → RS485 A/B (to Orbit Port B) │
    │      ├── UART2 → TMC2209 #1 UART (motor config)        │
    │      │                                                  │
    │      ├── TIM3 CH1 → STEP #1 ── TMC2209 #1 ── Motor #1  │
    │      ├── TIM3 CH2 → STEP #2 ── TMC2209 #2 ── Motor #2  │
    │      ├── TIM3 CH3 → STEP #3 ── TMC2209 #3 ── Motor #3  │
    │      ├── TIM3 CH4 → STEP #4 ── TMC2209 #4 ── Motor #4  │
    │      ├── GPIO     → STEP #5 ── TMC2209 #5 ── Motor #5  │
    │      ├── GPIO     → STEP #6 ── TMC2209 #6 ── Motor #6  │
    │      ├── GPIO     → STEP #7 ── TMC2209 #7 ── Motor #7  │
    │      ├── GPIO     → STEP #8 ── TMC2209 #8 ── Motor #8  │
    │      │                                                  │
    │      ├── GPIO → DIR #1..#8 (direction pins)             │
    │      ├── GPIO → EN  #1..#8 (enable pins)                │
    │      │                                                  │
    │      └── TMC2209 UART bus (daisy-chained, addressed)    │
    │              TMC2209 supports single-wire UART with      │
    │              MS1/MS2 addressing (4 addresses per pin)    │
    │                                                         │
    │  Power: +24V input → TPS54302 (24V→5V) → AMS1117-3.3   │
    │  Indicators: 8× door LEDs + 1 power + 1 fault           │
    └─────────────────────────────────────────────────────────┘
```

### 7.2 TMC2209 UART Addressing

The TMC2209 supports single-wire UART with 4 addresses per bus (set by MS1/MS2 pins). With two UART buses from the STM32 (or one bus with GPIO-switched direction), 8 drivers can be individually configured:

- UART bus 1: TMC2209 #1–#4 (MS1/MS2 = 00, 01, 10, 11)
- UART bus 2: TMC2209 #5–#8 (MS1/MS2 = 00, 01, 10, 11)

This allows per-motor configuration of: current limit, microstepping, StallGuard threshold, StealthChop mode.

### 7.3 Controller BOM

| Ref | Part | Description | Qty | Unit Cost | Ext Cost |
|---|---|---|---|---|---|
| U1 | STM32F030C8T6 | Cortex-M0, 48 MHz, 64 KB flash, LQFP-48 | 1 | $1.50 | $1.50 |
| U2–U9 | TMC2209 | Stepper driver, 2.8A peak, StallGuard4, UART, QFN-24 | 8 | $1.70 | $13.60 |
| U10 | MAX3485ESA | RS485 transceiver, 3.3V, half-duplex | 1 | $0.80 | $0.80 |
| U11 | TPS54302 | 24V→5V buck, 3A, SOT-23-6 | 1 | $0.90 | $0.90 |
| U12 | AMS1117-3.3 | 3.3V LDO, 1A, SOT-223 | 1 | $0.15 | $0.15 |
| — | Passives | Caps (100µF motor, 0.1µF per driver), resistors, inductors | ~60 pcs | — | $3.00 |
| J1 | RS485 terminal | 2-pos screw terminal (A/B) | 1 | $0.30 | $0.30 |
| J2 | Power terminal | 2-pos screw terminal (+24V/GND) | 1 | $0.30 | $0.30 |
| J3–J10 | Motor terminals | 4-pos screw terminal per motor (A+/A-/B+/B-) | 8 | $0.40 | $3.20 |
| SW1 | DIP switch | 4-bit Modbus address select | 1 | $0.20 | $0.20 |
| LED | Status LEDs | 1× power, 1× fault, 8× per-door activity | 10 | $0.05 | $0.50 |
| — | PCB | 80×100mm, 2-layer, ENIG, FR-4 1.6mm | 1 | $2.00 | $2.00 |
| — | Conformal coating | Acrylic, IPC-CC-830C | 1 | $0.50 | $0.50 |
| | | | | **Controller PCB subtotal** | **$26.95** |

**Per-door controller cost: $26.95 ÷ 8 = $3.37.** Even partially populated (4 doors), the per-door share is $6.74.

### 7.4 Complete Per-Door BOM

| Category | Part | Cost |
|---|---|---|
| **Motor** | StepperOnline 23HS30-2804S (NEMA 23, 1.26 N·m, 2.8A) | $17.00 |
| **Gearbox** | NMRV030 30:1 with NEMA 23 flange adapter kit | $25.00 |
| **Pinion** | Mod 2, 20T, 14mm bore, 5×5mm keyway, C45 steel | $10.00 |
| **Rack** | Mod 2, 1000mm, C45 steel (cut to fit) | $18.00 |
| **Motor mount** | Fabricated steel bracket (laser cut + welded) | $12.00 |
| **4-conductor cable** | 18 AWG, from controller to motor (A+/A-/B+/B-) | $3.00 |
| **Controller share** | $26.95 ÷ 8 doors | $3.37 |
| **Enclosure share** | IP66 enclosure for controller ÷ 8 | $1.50 |
| | **Total per door** | **$89.87** |

### 7.5 Volume Pricing (8-door onion bay)

| Quantity | Per-door | 8-door bay total | Notes |
|---|---|---|---|
| Prototype (1 bay) | ~$110 | ~$880 | No volume discounts |
| 5 bays (40 doors) | ~$95 | ~$760 | Motor + gearbox volume |
| 10 bays (80 doors) | ~$85 | ~$680 | PCB assembly volume breaks |
| 25+ bays (200 doors) | ~$75 | ~$600 | Full production pricing |

---

## 8. Comparison to Current System

| | Current ($500 linear actuator) | Rack & Pinion (~$90/door) |
|---|---|---|
| **Cost per door** | $500 | ~$90 |
| **8-door onion bay** | $4,000 | ~$720 |
| **Savings per bay** | — | **$3,280** |
| **Position feedback** | None (pot unused) | Step counting — exact position |
| **Position hold (no power)** | Motor stall current | Worm gear self-locking — zero power |
| **Wind load holding** | Motor fights wind continuously | Worm gear — physically locked |
| **Obstacle detection** | None | TMC2209 StallGuard — built into driver |
| **Orbit DOs consumed** | 2 per door (16 for 8 doors) | 0 — Modbus RTU on Port B |
| **Orbits needed for doors** | 2 (16 DOs ÷ 10 DOs/Orbit) | 0 — sharing bay Orbit Port B |
| **Speed** | 90 seconds full stroke | ~26–52 seconds (configurable) |
| **Noise** | DC motor (audible) | TMC2209 StealthChop (near silent) |
| **Soft start/stop** | No | Yes — stepper acceleration ramp in firmware |
| **Motor hold current** | Full current to hold (heat, wear) | Zero (worm gear holds, motor disabled) |
| **Lifetime concern** | Motor brush wear under sustained load | Stepper = brushless, worm gear = hardened steel |

---

## 9. Modbus Register Map (Per Door)

Each door occupies a block of 16 registers. Door 1 starts at base address 0x0000, Door 2 at 0x0010, etc.

| Offset | Address (Door 1) | Type | Description |
|---|---|---|---|
| +0x00 | 0x0000 | Holding (FC 06/16) | Target position: 0–10000 = 0.00–100.00% |
| +0x01 | 0x0001 | Input (FC 04) | Current position: 0–10000 = 0.00–100.00% |
| +0x02 | 0x0002 | Holding (FC 06) | Max speed: steps/sec (default 5000 = ~300 RPM motor) |
| +0x03 | 0x0003 | Input (FC 04) | Status: 0=idle, 1=opening, 2=closing, 3=stalled, 4=at-target, 5=homing, 6=fault |
| +0x04 | 0x0004 | Holding (FC 06) | Command: 0=nop, 1=go-to-target, 2=close(home), 3=stop, 4=full-open, 5=re-home |
| +0x05 | 0x0005 | Holding (FC 06) | Total stroke steps (default 697000 — calibrated during install) |
| +0x06 | 0x0006 | Holding (FC 06) | Acceleration: steps/sec² (default 2000). Controls soft start/stop ramp. |
| +0x07 | 0x0007 | Holding (FC 06) | StallGuard threshold: 0–255 (default 50). Lower = more sensitive. |
| +0x08 | 0x0008 | Input (FC 04) | StallGuard live value: 0–255 (real-time load indicator) |
| +0x09 | 0x0009 | Input (FC 04) | Motor current (mA, read from TMC2209 via UART) |
| +0x0A | 0x000A | Holding (FC 06) | Motor run current: 0–31 (TMC2209 IRUN register, default 20 = ~1.4A) |
| +0x0B | 0x000B | Holding (FC 06) | Motor hold current: 0–31 (TMC2209 IHOLD, default 0 = disabled — worm holds) |
| +0x0C | 0x000C | Input (FC 04) | Error code: 0=none, 1=stall, 2=StallGuard-fault, 3=driver-overtemp, 4=homing-fail |
| +0x0D | 0x000D | Input (FC 04) | Cycle count (total open/close cycles since reset) |
| +0x0E | 0x000E | Holding (FC 06) | Re-home interval: cycles between automatic re-home (default 100, 0=disabled) |
| +0x0F | 0x000F | Input (FC 04) | Driver temperature flag (from TMC2209 OTP) |

**Global registers (shared across all doors):**

| Address | Type | Description |
|---|---|---|
| 0x0100 | Holding | Modbus slave address (1–247, default from DIP switch) |
| 0x0101 | Holding | Baud rate: 0=9600, 1=19200, 2=38400, 3=57600 (default 1) |
| 0x0102 | Input | Firmware version (major×100 + minor) |
| 0x0103 | Input | Number of active doors (detected TMC2209s) |
| 0x0110 | Holding | Command ALL: 2=close-all, 3=stop-all (emergency) |

---

## 10. Firmware Outline (STM32F030)

```c
// 8-door stepper rack & pinion controller
// FreeRTOS or bare-metal superloop

main():
    HAL_Init()
    SystemClock_Config(48MHz)
    Modbus_RTU_Init(UART1, address=dip_switch_read(), baud=19200)
    TMC2209_Init(UART2)        // Configure all 8 drivers via UART
    Stepper_Init()             // Timer-based step pulse generation

    // Detect which doors are populated (TMC2209 responds to UART)
    for door in 0..7:
        if TMC2209_Ping(door):
            door_active[door] = true
            TMC2209_SetCurrent(door, IRUN=20, IHOLD=0)  // ~1.4A run, 0 hold
            TMC2209_SetMicrostep(door, 16)
            TMC2209_SetStallGuard(door, threshold=50)
            TMC2209_EnableStealthChop(door)

    // Home all active doors on startup
    for door in 0..7:
        if door_active[door]:
            Home_Door(door)

    while(1):
        Modbus_Poll()
        for door in 0..7:
            if door_active[door]:
                Motion_Update(door)     // Step generation, acceleration
                StallGuard_Check(door)  // Read SG value from TMC2209
                Status_Update(door)     // Update Modbus registers

Home_Door(door):
    // Drive toward closed at low speed
    Set_Direction(door, CLOSING)
    Set_Speed(door, 1000)  // slow
    while (!StallGuard_Triggered(door)):
        continue  // step generation runs in timer ISR
    Stop(door)
    step_count[door] = 0
    status[door] = IDLE

Motion_Update(door):
    if command[door] == GO_TO_TARGET:
        target_steps = target_pct[door] * total_steps[door] / 10000
        error = target_steps - step_count[door]

        if abs(error) < 10:  // within deadband
            Stop(door)
            status[door] = AT_TARGET
            return

        if error > 0:
            Set_Direction(door, OPENING)
        else:
            Set_Direction(door, CLOSING)

        // Trapezoidal acceleration profile
        speed = Calculate_Speed(error, accel[door], max_speed[door])
        Set_Speed(door, speed)

StallGuard_Check(door):
    sg_value = TMC2209_ReadStallGuard(door)
    registers.sg_live[door] = sg_value

    if motor_running[door] && sg_value < sg_threshold[door]:
        Stop(door)
        status[door] = STALLED
        error_code[door] = 1  // Obstacle or mechanical jam
        // Nova receives stall status on next Modbus poll → alarm
```

---

## 11. Wiring Diagram

### 11.1 Controller to Motors

```
                        DOOR CONTROLLER
                   ┌──────────────────────────────────┐
Orbit Port B       │                                  │
RS485 A ───────────┤ RS485 A                          │
RS485 B ───────────┤ RS485 B                          │        DOOR 1
                   │                                  │       MOTOR
24VDC Supply       │        Motor 1:  A+ ─────────────┼──────── A+
+24V ──────────────┤ +24V             A- ─────────────┼──────── A-
GND ───────────────┤ GND              B+ ─────────────┼──────── B+
                   │                  B- ─────────────┼──────── B-
                   │                                  │
                   │        Motor 2:  A+ ─────────────┼──────── (Door 2)
                   │                  A- ─────────────┼────────
                   │                  B+ ─────────────┼────────
                   │                  B- ─────────────┼────────
                   │                                  │
                   │         ... (up to 8 motors)     │
                   │                                  │
                   │    LEDs: ● Green    Power OK     │
                   │          ● Red      Fault        │
                   │          ● 1-8      Per-door     │
                   │    DIP:  [1][2][3][4] Address    │
                   └──────────────────────────────────┘
```

### 11.2 Per-Door Mechanical

```
       4-conductor cable (18 AWG, A+/A-/B+/B-)
            from controller
                │
                ▼
        ┌──────────────┐
        │  NEMA 23     │
        │  Stepper     │    ← 57×57mm, bolted to gearbox input flange
        │  23HS30-2804S│
        │  8mm shaft ──┼──► NMRV030 input adapter (8mm bore)
        └──────────────┘
                         ┌─────────────────────────┐
                         │    NMRV030 Worm Gearbox  │
                         │    30:1 ratio            │
                         │                         │
                         │    Output: 14mm shaft ──┼──► Mod 2, 20T Pinion
                         │            5×5mm keyway  │   (14mm bore, keyway)
                         │                         │        │
                         └─────────────────────────┘        │
                              (bolted to frame)             ▼
                                                      ┌──────────┐
                                                      │ Mod 2    │
                                                      │ Steel    │
                                                      │ Rack     │
                                                      │ (bolted  │
                                                      │ to door) │
                                                      └──────────┘
```

**Wiring per door: 4 conductors only.** No position sensor wires, no limit switch wires, no encoder wires. Just motor phase wires (A+, A-, B+, B-). Step counting and StallGuard handle everything else.

---

## 12. Bench Test Parts List

Order these to build a working prototype on the bench:

| Qty | Part | Source | Est. Cost |
|---|---|---|---|
| 1 | StepperOnline 23HS30-2804S | stepperonline.com or Amazon | $17 |
| 1 | NMRV030 30:1 + NEMA 23 flange kit | AliExpress or Amazon | $28 |
| 1 | Mod 2 spur gear, 20T, 14mm bore + keyway | AliExpress or KHK | $10 |
| 1 | Mod 2 gear rack, 1000mm, C45 steel | AliExpress or McMaster | $18 |
| 1 | BIGTREETECH TMC2209 V1.3 StepStick module | Amazon | $4 |
| 1 | STM32 Nucleo-F030R8 dev board (or Arduino + CNC shield) | Mouser/Amazon | $12 |
| 1 | 24V 5A power supply (bench/wall wart) | Amazon | $10 |
| 1 | Breadboard + jumper wires | — | $5 |
| | **Bench test total** | | **~$104** |

**Quick-start with Arduino:** If you want to spin the motor before writing STM32 firmware, an Arduino Uno + TMC2209 StepStick + any stepper library works. Wire STEP/DIR/EN from Arduino to the TMC2209, power the motor from 24V, and run the AccelStepper library. You can have the motor + gearbox + rack moving on the bench in under an hour.

---

## 13. Environmental / Durability

| Concern | Mitigation |
|---|---|
| Dust/chaff on rack teeth | Mod 2 is coarse (2mm pitch) — tolerant of debris. Apply lithium grease at install. |
| Moisture on gearbox | NMRV030 has sealed aluminum housing with oil fill. Inspect/refill annually. |
| Freeze/thaw on rack | C45 steel rack is rust-prone — galvanize or zinc plate. Stainless rack available at ~2× cost. |
| Stepper motor exposure | IP54 rated motors available for ~$5 more. Or mount motor+gearbox under an overhang/shield. |
| Controller PCB humidity | ENIG + conformal coating + IP66 enclosure (same as Orbit boards). |
| Bird/rodent nesting on motor | Cover with perforated metal guard. |
| 4-wire cable damage | Shielded cable in conduit. If cable breaks, door stops moving — worm holds position safely. |

---

## 14. Risk & Mitigation

| Risk | Impact | Mitigation |
|---|---|---|
| Stepper skips steps under extreme load | Position error accumulates | Worm gearbox provides 30:1 torque multiplication — motor operates at <50% of stall torque. Periodic re-home resets any accumulated error. |
| Rack/pinion backlash | ±0.1mm position uncertainty | Acceptable — door positioning doesn't need sub-mm accuracy. |
| Worm gearbox wear (5–10 yr) | Increased backlash, eventually gear failure | Bronze worm wheel is the wear item. NMRV030 worm wheel is replaceable (~$10). Annual grease check extends life. |
| TMC2209 driver failure | One door stops working | Each door has its own TMC2209 — failure affects only that door. Board continues operating remaining doors. |
| StallGuard false triggers | Door stops unexpectedly | Tune SG threshold at commissioning. TMC2209 UART allows per-site tuning without re-flashing firmware. |
| Power loss during movement | Door at unknown position | Worm self-locks immediately — door holds position. On power restore, firmware runs homing routine (close until StallGuard → reset step count). |
| All 8 motors running simultaneously | 8 × 2.8A = 22.4A from 24V supply | Size power supply for site: 24V 30A supply (~$50) handles all 8 with margin. In practice, rarely all moving simultaneously. |

---

## 15. Development Phases

| Phase | Deliverable | Est. Cost |
|---|---|---|
| **1. Bench prototype** | Nucleo/Arduino + TMC2209 + motor + gearbox + rack on workbench. Prove mechanism, measure torque, test StallGuard. | ~$104 (parts list above) |
| **2. Single-door field trial** | Mount on one real storage door. 30-day test: wind holding, position accuracy, homing reliability, StallGuard tuning. | ~$50 (mount bracket, cable, weather protection) |
| **3. Controller PCB v1** | First PCBA run (5 boards). STM32 firmware: 8-channel step generation, Modbus registers, StallGuard, homing, acceleration. | ~$200 (PCB fab + assembly + components for 5 boards) |
| **4. 8-door bay pilot** | Full onion curing bay. 8 motors + gearboxes + racks + controller. 1 season of operation. | ~$800 (8× mechanism + controller + enclosure + wiring) |
| **5. Production batch** | PCB v2 + batch of 10 controllers + 80 mechanism kits. | ~$7,500 (10 × $750/bay) |
| | **Total to first production bay** | **~$1,154** |

---

## 16. Sourcing Summary

| Part | Primary Source | Backup Source | Critical Spec |
|---|---|---|---|
| 23HS30-2804S stepper | StepperOnline | OMC StepperOnline | **8mm D-cut shaft** |
| NMRV030 30:1 + NEMA 23 kit | AliExpress (multiple vendors) | Amazon (Happybuy, Befenybay) | **8mm input bore adapter, 14mm output shaft, 5×5mm keyway** |
| Mod 2 pinion, 20T | AliExpress, KHK Gears, SDP-SI | McMaster-Carr | **14mm bore, 5×5mm keyway, 20° pressure angle** |
| Mod 2 rack, 1000mm | AliExpress, KHK Gears | McMaster-Carr (57655K58) | **Module 2, 20° pressure angle, C45 steel** |
| TMC2209 (IC) | LCSC, Mouser, DigiKey | Trinamic/Analog Devices | QFN-24, 2.8A peak |
| TMC2209 StepStick (prototype) | Amazon (BIGTREETECH) | Fysetc | V1.3 with UART header |
| STM32F030C8T6 | LCSC, Mouser, DigiKey | ST authorized distributors | LQFP-48 |
| 24V power supply (site) | Mean Well (LRS-350-24) | TDK-Lambda | 24V, 15A for 4-door, 30A for 8-door |

---

## 17. Key Design Decisions

| Decision | Rationale |
|---|---|
| **Stepper over DC motor** | No encoder or pot needed for position. TMC2209 gives stall detection for free. Brushless = longer motor life. Silent operation via StealthChop. |
| **Worm gearbox over planetary** | Self-locking holds door with zero power and zero motor current. Critical for wind loading and power-loss scenarios. Planetary is more efficient but not self-locking. |
| **30:1 ratio** | Balances torque (enough for 200 lb door), speed (~26 sec stroke), and self-locking guarantee. Higher ratios (50:1) are slower; lower ratios (15:1) may not reliably self-lock. |
| **Mod 2 gear** | Coarse enough for dust/outdoor exposure, strong enough for the loads, widely available. Mod 1 is too fine (dust sensitive). Mod 3 is unnecessarily bulky. |
| **20T pinion** | 40mm pitch diameter on 14mm shaft is a good fit. Smaller pinion = more torque but slower. 20T is the standard ratio that keeps speed reasonable. |
| **8 doors per controller** | Matches the onion curing bay max. Saves $21 in controller cost per door vs individual controllers. STM32F030 has enough timers and GPIO for 8 channels. |
| **No limit switches** | StallGuard replaces both limit switches and obstacle detection. Less wiring, less hardware, fewer failure points. If StallGuard is unreliable in the field, firmware can add a "slow creep to frame" homing mode that doesn't rely on it. |
| **Motor hold current = 0** | Worm gear holds position mechanically. Motor is disabled after reaching target. Zero power consumption while holding. Motor runs cool. |
