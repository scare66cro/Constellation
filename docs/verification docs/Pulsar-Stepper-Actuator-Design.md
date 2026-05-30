# Constellation — Pulsar: Stepper Motor Actuator Driver

**Date:** March 27, 2026
**Status:** Design Phase — Concept
**Parent:** Constellation-Control-Architecture.md
**Replaces:** AS2 pulsed-door relay mechanism (Controls.c `CtrlDoorsPulsed()`)
**Product Family:** Nova (brain) · Orbit (general I/O) · **Pulsar (stepper actuator driver)**

---

## 1. Problem Statement

The current AS2 controller drives doors and vents using timed relay pulses — an open relay fires for X seconds, a close relay fires for Y seconds, and the system counts pulse time to estimate position. There is no position feedback, no stall detection, and the mechanism relies on a full-close initialization on every boot to establish a known reference.

The biggest operational problem is the **sequential stage architecture**. The AS2 firmware processes all actuators in a single control loop using a state machine with discrete stages. If one actuator stalls (frozen door, debris, mechanical failure), the stage cannot complete and the entire system blocks. A single jammed door can prevent all other doors from closing, leaving the building's ventilation uncontrolled.

**Constellation solves this** by giving each actuator its own Pulsar — an independent stepper driver board on the Modbus network. Each Pulsar runs its own TMC2226 driver, detects stalls locally, and reports status back to Nova. If one door stalls, Nova continues commanding the rest. Graceful degradation instead of total failure.

---

## 2. Architecture Overview

### 2.1 How It Fits Into Constellation

The stepper actuator Pulsar follows the same "all Orbits are dumb" principle from the main architecture document. Nova runs the PID loop centrally and sends position commands over Modbus TCP. The Pulsar translates those commands into step pulses locally.

```
Nova (AM2434)                    Pulsar (STM32G474)              Actuator
┌─────────────┐   Modbus TCP    ┌────────────────┐   Step/Dir   ┌──────────┐
│ PID loop    │ ──────────────► │ Motion         │ ───────────► │ Stepper  │
│             │   REG_POSITION  │ controller     │              │ motor    │
│ Zone state  │ ◄────────────── │                │              │          │
│ machine     │   REG_STATUS    │ Stall detect   │              │ Door /   │
│             │                 │ (StallGuard)   │              │ Vent /   │
│ Alarm mgmt  │                 │                │              │ Damper   │
└─────────────┘                 │ Safety rules   │              └──────────┘
                                └────────────────┘
                                    │
                                    │ STEP, DIR, EN, DIAG
                                    ▼
                                ┌────────────────┐
                                │ TMC2226/5160   │
                                │ Driver IC      │
                                └────────────────┘
```

### 2.2 What Changes vs. Current AS2

| Aspect | AS2 (Current) | Constellation Pulsar |
|---|---|---|
| **Actuator type** | Timed relay pulses → gear motor | Stepper motor with driver IC |
| **Position tracking** | Pulse-time counting (estimated) | Step counting (exact) |
| **Position feedback** | None | StallGuard sensorless + step count |
| **Stall detection** | None — motor burns out or breaker trips | StallGuard detects in milliseconds |
| **Initialization** | Full-close for `ActuatorTime` seconds on every boot | Sensorless auto-calibration (stall-to-stall) |
| **Control resolution** | ON/OFF relay cycling | Proportional step positioning (any % of travel) |
| **Fault isolation** | One stalled door blocks all stages | Each door is independent on the bus |
| **PID output** | PWM % → timed pulse → estimated position | % → step count → exact position |
| **Recovery from stall** | Manual intervention required | Auto-retry every N seconds, alarm if persistent |

---

## 3. Component Selection

### 3.1 Stepper Driver IC — TMC2226 (Standard)

The **Trinamic TMC2226** is the recommended driver for door/vent actuators. It is the direct upgrade to the TMC2209 — same package, same pinout, same UART interface — but rated for 3.0A RMS (4.2A peak), which supports the higher-current motors needed for reliable door force.

| Parameter | TMC2226 |
|---|---|
| **Motor voltage** | 4.75V–36V |
| **Max current (RMS)** | 3.0A (peak 4.2A) |
| **Microstepping** | Up to 256 microsteps/full step |
| **Interface** | STEP/DIR + single-wire UART |
| **Stall detection** | StallGuard4 — sensorless load measurement |
| **CoolStep** | Yes — automatic current reduction at low load |
| **StealthChop** | Yes — silent operation below ~100 RPM |
| **Package** | QFN 5×5mm (28-pin), drop-in for TMC2209 footprint |
| **Unit cost** | ~$2.00–$2.50 |

**Why TMC2226 over TMC2209:**
- 3.0A RMS vs 2.0A — comfortably drives 2.8A NEMA 23 motors for serious door force
- Same pinout as TMC2209 — no PCB redesign, drop-in swap
- StallGuard4 provides sensorless stall detection without limit switches or encoders
- CoolStep reduces motor current (and heat) automatically when load is light, and automatically increases current to push through wind gusts
- StealthChop eliminates audible motor whine — important in occupied buildings
- Single-wire UART allows runtime tuning of current, StallGuard threshold, and microstepping from the Pulsar MCU

**Why not TMC2209:** The TMC2209 maxes out at 2.0A RMS — insufficient for the 2.8A NEMA 23 motor. The TMC2226 drives the full 2.8A with margin to spare, nearly doubling the available torque for only ~$0.50 more per channel.

**Driver comparison:**

| Driver | Max Current (RMS) | Max Voltage | StallGuard | Cost | Notes |
|---|---|---|---|---|---|
| TMC2209 | 2.0A | 29V | SG4 | ~$2.00 | Undersized for 2.8A NEMA 23 motor |
| **TMC2226** | **3.0A** | **36V** | **SG4** | **~$2.50** | **Recommended — drives 2.8A with headroom** |
| TMC5160 | 20A (ext FET) | 60V | SG2 | ~$6.00 | Overkill — external MOSFETs, 2.8A needs only TMC2226 |
| DRV8825 | 2.5A | 45V | None | ~$1.00 | No StallGuard — loses sensorless detection |

### 3.2 When to Use TMC5160 Instead

The **TMC5160** is the high-power alternative for heavy actuators (large roll-up doors, heavy dampers).

| Parameter | TMC5160 |
|---|---|
| **Motor voltage** | 8V–60V |
| **Max current (RMS)** | 20A (external MOSFETs) |
| **Microstepping** | Up to 256 |
| **Interface** | SPI + STEP/DIR |
| **Stall detection** | StallGuard2 |
| **Package** | QFN 10×10mm (48-pin) |
| **Unit cost** | ~$5–$8 |

Use TMC5160 when:
- Motor current exceeds 3.0A RMS (e.g., large NEMA 34 motors on roll-up doors)
- Motor voltage exceeds 36V (48V high-speed applications)
- Application requires very high-speed positioning (TMC5160 has an internal motion controller)

For typical storage doors and vents, TMC2226 at 24V is sufficient and preferred.

### 3.3 Stepper Motor Sizing

| Application | Typical Motor | Holding Torque | Current | Driver | Notes |
|---|---|---|---|---|---|
| Vent damper | NEMA 17 | 0.4–0.6 Nm | 1.5–2.0A | TMC2226 | Light load, gear-reduced |
| **Standard door** | **NEMA 23** | **1.2 Nm (at 3.0A driver limit)** | **3.0A** | **TMC2226** | **Recommended — IP65 motor (23IP65-20) with 50:1 worm gear** |
| Heavy door | NEMA 23/34 | 1.9–3.0 Nm | 3.0–6.0A | TMC2226 or TMC5160 | Large slide doors |
| Roll-up door | NEMA 34 | 3.0–5.0 Nm | 4.0–6.0A | TMC5160 | TMC5160 required, gear motor |

**Gear reduction is key.** A NEMA 23 IP65 motor (23IP65-20, 2.0 Nm rated) driven at the TMC2226’s 3.0A limit produces 1.2 Nm. With a 50:1 worm gear reducer this yields **60 Nm theoretical / 34.8 Nm after worm efficiency**. Through a rack-and-pinion linkage (see Section 8), this translates to **~614 lbs** of effective linear force — 23% above the 500 lb design load. The worm gear also provides mechanical self-locking (the door cannot be back-driven by wind loads).

### 3.4 Power Supply — Why 24V Is the Sweet Spot

Higher voltage does **not** increase torque at low speed — it only extends torque at higher RPM. At the speeds doors actually move (through a 50:1 gearbox), 24V delivers full rated torque.

**Voltage vs. torque at speed (NEMA 23, 2.8A):**

| Voltage | Holding torque | Torque at 500 RPM | Torque at 1000 RPM | Max useful speed |
|---|---|---|---|---|
| 12V | 100% | ~60% | ~25% | ~600 RPM |
| **24V** | **100%** | **~85%** | **~55%** | **~1200 RPM** |
| 48V | 100% | ~95% | ~80% | ~2000 RPM |

Through a 50:1 worm gear, motor speeds translate to:
- 24V @ 1200 RPM motor → 24 RPM output shaft → adequate for all door/vent applications
- 48V would require TMC5160 ($6 vs $2.50) for marginal speed benefit that doors don't need

**24V DC power supplies are commodity DIN-rail units** — readily available, cheap, and standard in industrial panels. A single 24V/5A (120W) supply handles one motor channel with headroom. A 24V/20A supply handles a full 4-channel Pulsar.

| Component | Voltage | Current (per channel) |
|---|---|---|
| TMC2226 logic | 3.3V or 5V (from Pulsar regulator) | <50 mA |
| TMC2226 motor | **24V DC** | Up to 3.0A RMS per motor |
| TMC5160 motor | 24V or 48V DC | Up to 20A per motor |

A single **24V / 20A** power supply can drive 4 TMC2226 channels (4× 2.8A = 11.2A) simultaneously with margin. The Pulsar board needs its own 3.3V regulator (SMPS from the 24V rail).

---

## 4. Pulsar Firmware — Motion Control

### 4.1 Register Map (Stepper Extension)

These registers extend the standard Orbit register map defined in the main architecture document.

**Nova writes (holding registers):**

| Register | Address | Type | Description |
|---|---|---|---|
| `REG_STEP_TARGET[n]` | 0x0300–0x030F | int32 | Target position in steps (0 = fully closed) |
| `REG_STEP_SPEED[n]` | 0x0310–0x031F | uint16 | Max speed in steps/sec |
| `REG_STEP_ACCEL[n]` | 0x0320–0x032F | uint16 | Acceleration in steps/sec² |
| `REG_STEP_CMD[n]` | 0x0330–0x033F | uint16 | Command: 0=idle, 1=goto, 2=calibrate, 3=stop, 4=clear_stall |
| `REG_STEP_STALLGUARD_THRESH[n]` | 0x0340–0x034F | uint16 | StallGuard sensitivity (0–255) |
| `REG_STEP_RUN_CURRENT[n]` | 0x0350–0x035F | uint16 | Run current (0–31, maps to mA via Rsense) |
| `REG_STEP_HOLD_CURRENT[n]` | 0x0360–0x036F | uint16 | Hold current (0–31) |

**Pulsar reads (input registers):**

| Register | Address | Type | Description |
|---|---|---|---|
| `REG_STEP_POSITION[n]` | 0x0400–0x040F | int32 | Current position in steps |
| `REG_STEP_STATUS[n]` | 0x0410–0x041F | uint16 | Bitmask (see below) |
| `REG_STEP_LOAD[n]` | 0x0420–0x042F | uint16 | StallGuard load value (0–1023) |
| `REG_STEP_CALIBRATED_RANGE[n]` | 0x0430–0x043F | int32 | Total steps from close-to-open after calibration |

**Status bitmask (`REG_STEP_STATUS`):**

| Bit | Name | Meaning |
|---|---|---|
| 0 | `MOVING` | Motor is actively stepping |
| 1 | `AT_TARGET` | Current position == target position |
| 2 | `STALL_DETECTED` | StallGuard triggered during last move |
| 3 | `CALIBRATED` | Auto-calibration has been completed |
| 4 | `OVERTEMP` | TMC driver over-temperature warning |
| 5 | `OPEN_LOAD` | TMC detected open coil (motor disconnected) |
| 6 | `SHORT_FAULT` | TMC detected shorted coil |
| 7 | `STALL_LATCHED` | Stall persists after retry — waiting for Nova `clear_stall` |

### 4.2 Motion Controller Task

A new FreeRTOS task on the Pulsar handles step generation:

```
StepperTask (Priority: Medium-High, Period: 1 ms tick)
│
├── For each channel (0..N):
│   ├── Read REG_STEP_CMD — new command from Nova?
│   │   ├── CMD_GOTO:      Set target, start trapezoidal profile
│   │   ├── CMD_CALIBRATE: Begin sensorless auto-calibration sequence
│   │   ├── CMD_STOP:      Decelerate to stop at current position
│   │   └── CMD_CLEAR_STALL: Clear STALL_LATCHED, allow new commands
│   │
│   ├── Execute trapezoidal motion profile:
│   │   ├── Acceleration phase: ramp step rate from 0 to REG_STEP_SPEED
│   │   ├── Cruise phase: hold at REG_STEP_SPEED
│   │   └── Deceleration phase: ramp down to 0 at target
│   │
│   ├── Check StallGuard flag (TMC2226 UART read, ~20 µs):
│   │   ├── If stall detected:
│   │   │   ├── Stop immediately
│   │   │   ├── Set STALL_DETECTED in REG_STEP_STATUS
│   │   │   └── Update REG_STEP_POSITION with actual position
│   │   └── If no stall: continue stepping
│   │
│   └── Update REG_STEP_POSITION, REG_STEP_STATUS, REG_STEP_LOAD
│
└── Sleep until next 1 ms tick
```

**Resource cost on STM32G474:**
- One hardware timer per stepper channel (TIM2/TIM3/TIM4/TIM5 — 32-bit timers)
- Timer generates step pulses in output-compare mode — zero CPU during cruise phase
- StallGuard UART polling: ~20 µs per channel per ms tick = <1% CPU for 4 channels
- Total RAM: ~64 bytes per channel for motion state

### 4.3 Trapezoidal Motion Profile

The Pulsar generates a trapezoidal velocity profile locally — Nova only sends the target position.

```
Speed
  │
  │        ┌──────────────────┐
  │       /│                  │\
  │      / │    Cruise        │ \
  │     /  │                  │  \
  │    /   │                  │   \
  │   /    │                  │    \
  │  / Acc │                  │Dec  \
  │ /      │                  │      \
  └────────┴──────────────────┴────────── Time
  0     t_accel            t_decel    t_end

  t_accel = max_speed / acceleration
  t_decel = max_speed / acceleration (symmetric)
  cruise_steps = total_steps - accel_steps - decel_steps
```

For short moves where the motor can't reach full speed, the profile becomes triangular (accelerate → immediately decelerate).

### 4.4 Simultaneous Multi-Channel Operation

All stepper channels on a Pulsar run **truly simultaneously** — not sequenced. Each channel has its own dedicated hardware timer:

| Channel | Timer | Step Generation |
|---|---|---|
| 0 | TIM2 | Output-compare pulse, independent |
| 1 | TIM3 | Output-compare pulse, independent |
| 2 | TIM4 | Output-compare pulse, independent |
| 3 | TIM5 | Output-compare pulse, independent |

Each timer runs autonomously in silicon. During the cruise phase, the CPU is completely uninvolved — the timer peripheral generates step pulses with **zero CPU cycles**. Whether one channel or all four are moving makes no difference to the MCU.

**What this means in practice:**

Nova sends a single multi-register Modbus TCP write to the Pulsar:

```
REG_STEP_TARGET[0] = N_total    // Door 1: full open
REG_STEP_TARGET[1] = N_total    // Door 2: full open
REG_STEP_TARGET[2] = N_total    // Door 3: full open
REG_STEP_TARGET[3] = N_total    // Door 4: full open
REG_STEP_SPEED[0..3] = SPEED_MAX
REG_STEP_CMD[0..3] = CMD_GOTO

// All 4 doors begin moving within microseconds of each other
// Each finishes independently based on its travel distance
```

Door 1 might finish in 8 seconds (short travel), door 3 in 12 seconds (long travel), and door 2 might stall on ice — while doors 1, 3, and 4 all reach full open. Nova knows the exact position of each one individually.

Contrast with the AS2: a single pair of relay outputs (OPEN/CLOSE) drives all doors on the same circuit. They all move together, and if one stalls, they all stop.

**Power isolation:** Each TMC2226 channel draws from the motor supply rail independently. With individual 24V power feeds per actuator (or a single supply rated for the combined load), there is no current stacking issue. Four doors at 2.8A each require 11.2A total — or four separate 5A supplies for complete isolation.

---

## 5. Sensorless Auto-Calibration

### 5.1 The Problem with Limit Switches

Traditional actuator calibration uses physical limit switches at each end of travel. These add:
- Wiring complexity (2 wires per switch, 2 switches per actuator)
- Failure modes (switch corrosion, wire breaks, adjustment drift)
- Installation labor (physical mounting and alignment)
- Spare parts inventory

StallGuard eliminates all of this.

### 5.2 How StallGuard Works

The TMC2226 measures back-EMF from the motor coils during each microstep. When the motor hits a mechanical stop, rotor velocity drops to zero and back-EMF vanishes. The driver reports this as a stall event within **one electrical period** (~4 full steps).

Detection latency at typical calibration speeds (slow):

| Motor speed | Detection time |
|---|---|
| 100 steps/sec | ~40 ms |
| 200 steps/sec | ~20 ms |
| 50 steps/sec | ~80 ms |

At calibration speed (slow, ~100 steps/sec), the motor overshoots by 4–8 steps before the stall is detected. This is negligible for door/vent positioning.

### 5.3 Auto-Calibration Sequence

When Nova sends `CMD_CALIBRATE`, the Pulsar runs:

```
AutoCalibrate(channel):
│
├── 1. Set run current to calibration current (50–70% of normal)
│      Lower current = less mechanical stress on end stops
│
├── 2. Drive CLOSE at slow speed (~100 steps/sec)
│      └── Wait for StallGuard trigger → CLOSED end stop found
│          Record position as 0 (home)
│
├── 3. Drive OPEN at slow speed
│      └── Wait for StallGuard trigger → OPEN end stop found
│          Record position as N_total (full travel)
│
├── 4. Store N_total in REG_STEP_CALIBRATED_RANGE
│      This is the total step count from closed to open
│
├── 5. Drive to position 0 (fully closed) at normal speed
│      └── Door/vent is now in known closed position
│
├── 6. Set CALIBRATED flag in REG_STEP_STATUS
│      Restore normal run current
│
└── 7. Report N_total to Nova for PID range mapping
```

**Time estimate:** For a door with 5,000 full steps of travel at 200 steps/sec:
- Close drive: ~25 seconds (worst case, door starts fully open)
- Open drive: ~25 seconds
- Return to close: ~25 seconds
- **Total: ~75 seconds** (one-time, on commissioning or power-up)

### 5.4 PID Mapping After Calibration

Once Nova knows `N_total`, it maps PID percentage to step position:

```
target_steps = (PID_output_percent / 100.0) × N_total

Example (door with 5,000 total steps):
  PID output = 0%   → target = 0 steps     (fully closed)
  PID output = 50%  → target = 2,500 steps  (half open)
  PID output = 100% → target = 5,000 steps  (fully open)
```

This is the same concept as the current AS2 PID-to-pulse mapping, but **exact** instead of estimated. Nova writes `REG_STEP_TARGET[n] = 2500` and the Pulsar drives exactly to that position.

### 5.5 Adaptive Speed Control — Error-Proportional Traverse Rate

Unlike the AS2's relay pulses (which always fire at the same speed), the stepper system lets Nova control **both** where the door goes and **how fast** it gets there. Nova adjusts `REG_STEP_SPEED` based on the magnitude of the PID error:

```
// Nova PID update (every U seconds)
error = abs(plenum_temp - setpoint)

if mode_transition:
    // State change (cure available, cooling→refrig, shutdown)
    // Slam to target at maximum speed
    speed = SPEED_MAX         // e.g., 1000 steps/sec
    target = mode_target       // 0 (close) or N_total (open)
elif error > 2.0°F:
    speed = SPEED_FAST         // e.g., 800 steps/sec
elif error > 0.5°F:
    speed = SPEED_NORMAL       // e.g., 400 steps/sec
elif error > 0.2°F:
    speed = SPEED_CREEP        // e.g., 100 steps/sec
else:
    speed = 0                  // Hold position — at setpoint

clamped_output = clamp(pid_output, last_output ± max_increment)
target_steps = (clamped_output / 100.0) × N_total

write REG_STEP_SPEED[n] = speed
write REG_STEP_TARGET[n] = target_steps
```

**Three distinct speed regimes:**

| Regime | Error | Speed | Behavior |
|---|---|---|---|
| **Far from setpoint** | >2°F | Fast (800 steps/sec) | Door moves aggressively to correct large temperature error |
| **Approaching setpoint** | 0.5–2°F | Normal (400 steps/sec) | Moderate positioning — avoiding overshoot |
| **Near setpoint** | 0.2–0.5°F | Creep (100 steps/sec) | Gentle, precise adjustments — tenths of a degree |
| **At setpoint** | <0.2°F | Hold (0) | Motor holds position, no movement, minimal power |
| **Mode transition** | N/A | Max (1000 steps/sec) | Slam to target — cure window opens, shutdown, etc. |

**Why mode transitions get full speed:**
When onion cure conditions are met (outside temp + humidity thresholds), the doors need to open **now** to capture the curing window. Similarly, when cure conditions fail (rain, temp drop), the doors need to close fast to protect the crop. These are not gradual PID adjustments — they are state machine transitions where every second counts.

The creep regime is where the proportional advantage over relays really shows. The AS2 can't do "barely move" — a relay pulse is always the same duration regardless of how close to setpoint you are. Steppers at creep speed make sub-percent position changes that are impossible with pulsed relays.

**Implementation cost:** ~10 lines of code in Nova's PID update function — a speed lookup table indexed by error magnitude. The Pulsar doesn't need any changes; it already accepts `REG_STEP_SPEED` per channel.

---

## 6. Stall Detection and Retry During Operation

### 6.1 Runtime Stall Detection

StallGuard remains active during normal operation (not just calibration). If a door hits an obstruction mid-travel:

1. TMC2226 detects the stall (4–40 ms depending on speed)
2. Pulsar stops stepping immediately
3. `STALL_DETECTED` flag set in `REG_STEP_STATUS`
4. `REG_STEP_POSITION` updated to actual stopped position
5. Nova reads the stall on next 1 Hz poll

### 6.2 Nova Retry Logic

Nova implements a configurable retry strategy:

```
Per-channel stall state (in Nova zone context):
  stall_count        = 0
  retry_interval     = 10 seconds (configurable)
  max_retries        = 6 (configurable, 0 = infinite)
  last_retry_time    = 0

On stall detected:
  stall_count++
  if stall_count <= max_retries:
      log WARNING: "Door #n stalled at {position}%, retry {stall_count}/{max_retries}"
      after retry_interval:
          send CMD_CLEAR_STALL → Pulsar
          re-send CMD_GOTO with same target → Pulsar
  else:
      latch STALL_LATCHED on Pulsar
      raise AL_ACTUATOR_STALL alarm
      log ALARM: "Door #n stall persistent after {max_retries} retries"
      continue controlling all other actuators normally

On successful move (AT_TARGET and no stall):
  stall_count = 0
```

### 6.3 Why Retrying Doesn't Hurt the Motor

A stepper motor at stall is **not damaged** by continued attempts. Key facts:

- **No current spike.** Unlike DC motors that draw locked-rotor current (5–10× rated), a stepper's current is regulated by the TMC driver's chopper circuit. Stalled current equals normal run current. The driver does not care if the rotor is spinning or locked.
- **Equal thermal dissipation.** A stepper holding position at zero speed dissipates the same power as a stepper stalled against a load. Both are the designed-for worst case (I²R losses at full rated current).
- **StallGuard triggers in milliseconds.** The motor doesn't grind against the obstruction — it tries 4–8 microsteps, detects the stall, and stops. Each retry attempt exposes the motor to load for <100 ms.
- **CoolStep reduces hold current.** Between retries, CoolStep automatically reduces current to the minimum needed to hold position, reducing heat dissipation during the wait period.

**Practical scenario:** A frozen door retries every 10 seconds for 6 attempts. Each attempt lasts ~50 ms before StallGuard fires. Total motor-under-load time: 300 ms out of 60 seconds. The motor runs cooler during this failure than during normal continuous positioning.

You could retry every 10 seconds for a week and the stepper would not be damaged.

### 6.4 Independent Fault Handling — The Key Advantage

This is the single biggest operational improvement over the AS2:

| Scenario | AS2 (Current) | Constellation |
|---|---|---|
| Door #3 frozen shut | Stage execution blocks. All doors stop responding. Building ventilation stuck in last state until tech arrives. | Nova logs alarm for Door #3, retries on 10-second interval. Doors #1, #2, #4 continue operating normally. Building maintains ventilation with one door down. |
| Ice on vent damper | If the vent is in the same stage group, other equipment in that stage is delayed | Vent Pulsar reports stall. Nova keeps all other equipment running. Vent retries automatically — when ice melts, it closes on the next attempt. |
| Two doors stall simultaneously | System may enter failure state, shutting down the zone | Nova alarms on both, continues operating fans, refrig, and remaining doors. Partial ventilation is maintained. |
| Debris clears itself | System remains blocked until next full stage cycle or manual restart | Next 10-second retry succeeds. `stall_count` resets. Nova resumes normal positioning. No operator intervention needed. |

**Graceful degradation** — the building never loses all ventilation because of one stuck actuator. The AS2's sequential stage architecture means everything is coupled. Constellation decouples every actuator.

### 6.5 Torque / Force Limiting — Programmable Current Control

A stepper motor's output torque is directly proportional to its coil current. The TMC2226's chopper circuit regulates current to a programmable limit — the motor **physically cannot produce more torque** than the current setting allows. This turns the driver IC into a zero-cost electronic torque limiter, replacing mechanical slip clutches ($20–50 each, wear out over time).

**TMC2226 current registers (writable over single-wire UART at any time, even mid-move):**

| Register | TMC2226 Field | Range | Purpose |
|---|---|---|---|
| `IRUN` | Run current | 0–31 (maps to 0–3.0A via Rsense) | Max current while motor is stepping |
| `IHOLD` | Hold current | 0–31 | Current while motor is stationary |

**Force scales linearly with current:**

```
Force at output ∝ Motor current × Gear ratio × (1 / pinion radius)

Example (NEMA 23 IP65, 2.0 Nm rated at 5.0A, TMC2226 limits to 3.0A = 1.2 Nm, 50:1 gear):

  IRUN = 31 (100%, 3.0A) → 60.0 Nm shaft → 100% of driver-limited torque
  IRUN = 22 (73%,  2.2A) → 44.0 Nm shaft →  73%
  IRUN = 16 (53%,  1.6A) → 32.0 Nm shaft →  53%
  IRUN = 12 (40%,  1.2A) → 24.0 Nm shaft →  40%
  IRUN = 6  (20%,  0.6A) → 12.0 Nm shaft →  20%

  Actual door force depends on rack-and-pinion geometry (see Section 8).
  Module 1.5, 17T pinion (r=12.75mm) at 100%: 60,000/12.75 = 4,706 N ≈ 1,058 lbs
  After worm efficiency (~58%):              2,729 N ≈   614 lbs
```

When the door hits resistance exceeding the current-limited torque, the motor simply stalls — it cannot push harder. StallGuard detects the stall instantly. Nothing is damaged: not the motor, not the door, not the gearbox, not whatever is in the way.

**Nova sets context-appropriate current limits before each move:**

| Operating Context | IRUN Setting | Shaft Torque | Rationale |
|---|---|---|---|
| Normal positioning | 70% | 42.0 Nm | Standard door movement |
| Calibration | 50% | 30.0 Nm | Gentle stall-to-stall, protect mechanical end stops |
| Closing against seal | 40% | 24.0 Nm | Enough to compress weatherstrip, not enough to crush frame |
| Stall retry | 60% | 36.0 Nm | Moderate — break through light ice, don't force heavy obstruction |
| Emergency close | 90% | 54.0 Nm | Maximum force — protecting the crop |
| Hold position | 20% (IHOLD) | 12.0 Nm | Resist wind loads, but a person can pull the door open |

**How Nova applies this per move:**

```
// Before commanding a move, set the appropriate current
if context == CALIBRATION:
    write REG_STEP_RUN_CURRENT[n]  = 16   // 50%
    write REG_STEP_HOLD_CURRENT[n] = 6    // 20%
elif context == CLOSING_TO_SEAL:
    write REG_STEP_RUN_CURRENT[n]  = 12   // 40%
    write REG_STEP_HOLD_CURRENT[n] = 6    // 20%
elif context == EMERGENCY_CLOSE:
    write REG_STEP_RUN_CURRENT[n]  = 28   // 90%
    write REG_STEP_HOLD_CURRENT[n] = 10   // 30%
else:  // Normal
    write REG_STEP_RUN_CURRENT[n]  = 22   // 70%
    write REG_STEP_HOLD_CURRENT[n] = 6    // 20%

// Then command the move
write REG_STEP_TARGET[n] = target_steps
write REG_STEP_CMD[n] = CMD_GOTO
```

**Key safety advantage:** With relay-driven gear motors, the motor produces whatever torque it can until a breaker trips or something breaks. There is no way to limit force electronically. With the TMC2226, the maximum force is a software setting that Nova can adjust per context, per channel, per move — and the physics of stepper current regulation makes it impossible to exceed.

**CoolStep interaction:** Between moves, the TMC2226's CoolStep feature automatically reduces current below `IHOLD` to the minimum needed to maintain position. This further reduces heat dissipation and power consumption when the door is stationary.

### 6.6 Load Monitoring — Wind Gusts and Predictive Maintenance

The TMC2226's StallGuard doesn't just report binary stall/no-stall — it provides a **continuous load value** (`SG_RESULT`, 0–1023) that the Pulsar exposes via `REG_STEP_LOAD[n]`. This enables two important capabilities:

**Wind gust handling (automatic, no firmware logic needed):**

CoolStep monitors the StallGuard load value in real time and adjusts motor current automatically:

- Light load (door moving freely) → CoolStep reduces current below `IRUN` to save power
- Moderate load (wind gust, friction) → CoolStep increases current up to `IRUN` to maintain speed
- Exceeds `IRUN` capacity → motor stalls, StallGuard fires

A wind gust that increases resistance but doesn't exceed the motor's torque at `IRUN` is handled **entirely in hardware** — CoolStep pushes more current, the motor powers through, and continues to the target. No stall, no retry, no interruption. The Pulsar firmware doesn't need to do anything.

The stall event only fires when the motor **actually stops rotating** — not when it encounters increased resistance. This means the door pushes through wind gusts, light ice, and sticky seals automatically, only reporting a stall when it hits something it truly cannot move.

**Predictive maintenance via load trending:**

Nova reads `REG_STEP_LOAD[n]` on every 1 Hz poll. Over time, this reveals mechanical degradation:

| Load Value (SG_RESULT) | Interpretation | Nova Action |
|---|---|---|
| >800 | Free moving — healthy | Normal operation |
| 400–800 | Moderate resistance | Normal — log if sustained |
| 100–400 | Heavy resistance | Warning — bearing wear? ice buildup? track debris? |
| <100 | Near-stall | Alert — something is binding, schedule maintenance |
| 0 | Stalled | Stall handler (retry logic from Section 6.2) |

If door #3's average load value has been trending downward over weeks (increasing resistance), Nova can proactively flag it: **"Door #3 bearing resistance increasing — schedule maintenance."** This catches problems before they become stalls — something the AS2's relay system has zero visibility into.

---

## 7. Pulsar Hardware

### 7.1 Per-Channel Bill of Materials

| Component | Part | Qty | Unit Cost | Notes |
|---|---|---|---|---|
| Stepper driver | TMC2226-SA | 1 | $2.50 | QFN 5×5mm, drop-in for TMC2209 |
| Sense resistor | 0.075Ω 1% 0603 | 2 | $0.05 | Sets 3.0A full-scale current |
| Bulk capacitor | 100µF 50V electrolytic | 1 | $0.35 | Motor supply decoupling (24V rail) |
| Bypass cap | 100nF 0603 | 3 | $0.03 | VCC_IO, VS, 5VOUT |
| STEP/DIR routing | — | — | — | GPIO from STM32G474 |
| UART routing | — | — | — | Single-wire GPIO for config |
| **Per-channel total** | | | **~$2.95** | |

### 7.2 4-Channel Pulsar Board Layout

A 4-channel Pulsar supports stepper channels alongside standard Orbit I/O.

```
┌─────────────────────────────────────────────────────────────┐
│  Stepper Actuator Pulsar (4-Channel)                                │
│                                                             │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐   │
│  │ TMC2226  │  │ TMC2226  │  │ TMC2226  │  │ TMC2226  │   │
│  │ Channel 0│  │ Channel 1│  │ Channel 2│  │ Channel 3│   │
│  └──┬───┬───┘  └──┬───┬───┘  └──┬───┬───┘  └──┬───┬───┘   │
│     │   │         │   │         │   │         │   │        │
│   STEP DIR      STEP DIR      STEP DIR      STEP DIR      │
│     │   │         │   │         │   │         │   │        │
│  ┌──┴───┴─────────┴───┴─────────┴───┴─────────┴───┴──┐    │
│  │             STM32G474CE (Cortex-M4F)               │    │
│  │                                                     │    │
│  │  TIM2 → Ch0 STEP    TIM4 → Ch2 STEP               │    │
│  │  TIM3 → Ch1 STEP    TIM5 → Ch3 STEP               │    │
│  │                                                     │    │
│  │  UART4 → TMC2226 single-wire config bus            │    │
│  │  USART1 → Port A RS485 (analog boards / sensors)   │    │
│  │  USART3 → Port B RS485 (spare / VFDs)              │    │
│  │  SPI1 → W5500 Ethernet                             │    │
│  └─────────────────────────────────────────────────────┘    │
│                                                             │
│  ┌──────────┐  ┌──────────┐  ┌──────────────────────┐     │
│  │ W5500    │  │ RS485    │  │ 24V → 3.3V SMPS      │     │
│  │ Ethernet │  │ xcvr ×2  │  │ (power from motor    │     │
│  │          │  │          │  │  supply rail)         │     │
│  └──────────┘  └──────────┘  └──────────────────────┘     │
│                                                             │
│  Connectors:                                                │
│   J1: RJ45 Ethernet                                        │
│   J2: RS485-A (sensors)                                     │
│   J3: RS485-B (spare)                                       │
│   J4-J7: Motor outputs (4-pin: A+, A-, B+, B-)             │
│   J8: 24V power input                                       │
│   J9: DIP switch (Modbus address)                           │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### 7.3 Pin Allocation on STM32G474CE

| STM32 Pin | Function | Notes |
|---|---|---|
| PA0 (TIM2_CH1) | Ch0 STEP | Output compare, pulse generation |
| PA1 | Ch0 DIR | GPIO output |
| PA6 (TIM3_CH1) | Ch1 STEP | Output compare |
| PA7 | Ch1 DIR | GPIO output |
| PB6 (TIM4_CH1) | Ch2 STEP | Output compare |
| PB7 | Ch2 DIR | GPIO output |
| PA0 (TIM5_CH1) | Ch3 STEP | Output compare (if TIM5 remapped) |
| PA3 | Ch3 DIR | GPIO output |
| PC10 (UART4_TX) | TMC config bus | Single-wire UART to all TMC2226s |
| PA9 (USART1_TX) | Port A RS485 TX | Analog board polling |
| PA10 (USART1_RX) | Port A RS485 RX | |
| PB10 (USART3_TX) | Port B RS485 TX | Spare / VFD |
| PB11 (USART3_RX) | Port B RS485 RX | |
| PA5 (SPI1_SCK) | W5500 SPI | Ethernet |
| PA6 (SPI1_MISO) | W5500 SPI | |
| PA7 (SPI1_MOSI) | W5500 SPI | |
| PB0 | W5500 CS | |
| PB1 | W5500 INT | |
| PC0–PC3 | TMC DIAG pins | One per channel, active on stall |
| PC4–PC7 | TMC EN pins | One per channel, active-low enable |

The STM32G474CE has 81 GPIO pins, 7 general-purpose timers, and 5 UARTs — more than enough for 4 stepper channels plus the standard Orbit peripherals.

### 7.4 8-Channel Pulsar Variant

For installations with 5–8 doors (onion curing bays), a purpose-built 8-channel Pulsar eliminates the need to split doors across two boards. Same MCU, same Ethernet, same firmware framework — but the board drops the shift registers, digital outputs, and second RS485 port in favor of 8 stepper channels.

**Timer allocation (2 output-compare channels per timer):**

| Timer | Output Compare Channels | Stepper Channels |
|---|---|---|
| TIM2 (32-bit) | CH1, CH2 | Ch0 STEP, Ch1 STEP |
| TIM3 (16-bit) | CH1, CH2 | Ch2 STEP, Ch3 STEP |
| TIM4 (16-bit) | CH1, CH2 | Ch4 STEP, Ch5 STEP |
| TIM5 (32-bit) | CH1, CH2 | Ch6 STEP, Ch7 STEP |

All 8 timers run independently in silicon — zero CPU during cruise phase, all 8 doors move simultaneously.

**TMC2226 UART addressing (4 addresses per bus, 2 buses for 8 drivers):**

| Bus | UART | MS1/MS2 Addresses | Drivers |
|---|---|---|---|
| Bus A | UART4 | 0, 1, 2, 3 | TMC2226 channels 0–3 |
| Bus B | UART5 | 0, 1, 2, 3 | TMC2226 channels 4–7 |

The G474 has 5 UARTs — using 2 for TMC config leaves USART1 for RS485 sensors and SPI1 for Ethernet.

**GPIO budget:**

| Function | Pins | Notes |
|---|---|---|
| 8× STEP | 8 | Timer output-compare |
| 8× DIR | 8 | GPIO output |
| 8× DIAG | 8 | GPIO input (stall interrupt) |
| 8× EN | 8 | GPIO output (active-low enable) |
| SPI (W5500) | 5 | SCK, MISO, MOSI, CS, INT |
| UART4 (TMC bus A) | 1 | Single-wire |
| UART5 (TMC bus B) | 1 | Single-wire |
| USART1 (RS485) | 2 | TX, RX for sensor polling |
| DIP switch | 4 | Modbus address |
| **Total** | **53** | **of 81 available — 28 spare** |

**Board dimensions: 4.5" × 5.5" (114mm × 140mm), 2-layer or 4-layer**

| Zone | Height | Contents |
|---|---|---|
| Driver channels (8 rows) | ~3.5" (90mm) | 8× TMC2226 + passives, motor connectors left, power connectors right |
| MCU / comms / power reg | ~2.0" (50mm) | STM32G474, W5500 + RJ45, RS485 xcvr, 3.3V SMPS, DIP switch |

Width budget across the 4.5":
- Left-edge 4-pin motor connectors: ~0.5" (12mm)
- TMC2226 + sense resistors + decoupling: ~0.8" (20mm)
- Routing channel (STEP/DIR/EN/DIAG traces): ~0.9" (23mm)
- STM32G474 + support components: ~1.0" (25mm) — shares center zone
- TMC2226 power-side passives (100µF bulk cap): ~0.6" (15mm)
- Right-edge 2-pin power connectors: ~0.4" (10mm)
- Edge clearance: ~0.3" (9mm)

**Board layout:**

Each TMC2226 driver has its motor output (4-pin: A1, A2, B1, B2) on the **left edge** and its dedicated 24V power input (2-pin: +24V, GND) on the **right edge**. The installer wires each channel independently — one EPS-120-24 feeds the 2-pin power plug, one motor cable runs from the 4-pin plug to the door.

```
  MOTOR OUTPUT          4.5" (114mm)                        24V POWER IN
  (4-pin each)     ◄──────────────────►                     (2-pin each)
      ║                                                          ║
      ▼                                                          ▼
┌───────────────────────────────────────────────────────────────────────┐
│                  8-Channel Stepper Actuator Pulsar                    │
│                  4.5" × 5.5" (114mm × 140mm)                        │
│                                                                      │
│  J3  ◄── [A1 A2 B1 B2] ──┤ TMC2226  Ch 0 ├── [+24V GND] ──► J11   │
│  J4  ◄── [A1 A2 B1 B2] ──┤ TMC2226  Ch 1 ├── [+24V GND] ──► J12   │
│  J5  ◄── [A1 A2 B1 B2] ──┤ TMC2226  Ch 2 ├── [+24V GND] ──► J13   │
│  J6  ◄── [A1 A2 B1 B2] ──┤ TMC2226  Ch 3 ├── [+24V GND] ──► J14   │
│          ─ ─ ─ ─ ─ ─  UART4 bus A (addr 0-3)  ─ ─ ─ ─ ─ ─         │
│  J7  ◄── [A1 A2 B1 B2] ──┤ TMC2226  Ch 4 ├── [+24V GND] ──► J15   │
│  J8  ◄── [A1 A2 B1 B2] ──┤ TMC2226  Ch 5 ├── [+24V GND] ──► J16   │
│  J9  ◄── [A1 A2 B1 B2] ──┤ TMC2226  Ch 6 ├── [+24V GND] ──► J17   │
│  J10 ◄── [A1 A2 B1 B2] ──┤ TMC2226  Ch 7 ├── [+24V GND] ──► J18   │
│          ─ ─ ─ ─ ─ ─  UART5 bus B (addr 0-3)  ─ ─ ─ ─ ─ ─         │
│                                                                      │
│  ┌──────────────────────────────────────────┐                       │
│  │          STM32G474CE (Cortex-M4F)        │                       │
│  │                                          │                       │
│  │  TIM2_CH1/CH2 → Ch0/Ch1 STEP            │                       │
│  │  TIM3_CH1/CH2 → Ch2/Ch3 STEP            │                       │
│  │  TIM4_CH1/CH2 → Ch4/Ch5 STEP            │                       │
│  │  TIM5_CH1/CH2 → Ch6/Ch7 STEP            │                       │
│  │                                          │                       │
│  │  UART4 → TMC bus A (channels 0-3)       │                       │
│  │  UART5 → TMC bus B (channels 4-7)       │                       │
│  │  USART1 → Port A RS485 (sensors)        │                       │
│  │  SPI1 → W5500 Ethernet                  │                       │
│  └──────────────────────────────────────────┘                       │
│                                                                      │
│  ┌─────────┐  ┌─────────┐  ┌──────────────┐  ┌───────────┐        │
│  │ W5500   │  │ RS485   │  │ 24V → 3.3V   │  │ DIP switch│        │
│  │ Ethernet│  │ xcvr ×1 │  │ SMPS (diode- │  │ (Modbus   │        │
│  │         │  │         │  │ OR J11–J18)  │  │  address)  │        │
│  └─────────┘  └─────────┘  └──────────────┘  └───────────┘        │
│      J1            J2                                               │
│                                                                      │
└──────────────────────────────────────────────────────────────────────┘

  Connector summary:
   J1:   RJ45 Ethernet (bottom edge)
   J2:   RS485 sensors (bottom edge)
   J3–J10:  Motor outputs Ch 0–7 (LEFT edge, 4-pin: A1, A2, B1, B2)
   J11–J18: 24V power inputs Ch 0–7 (RIGHT edge, 2-pin: +24V, GND)
```

**Power isolation:** Each TMC2226's motor supply (VMOT) comes from its own 2-pin connector. The 3.3V logic regulator taps 24V through a diode-OR from all eight feeds — any one live PSU keeps the MCU running. If a supply fails, only its motor channel goes down; the board and all other channels continue operating.

**What it drops vs. the 4-channel general-purpose Pulsar:**

| Feature | 4-Channel (general) | 8-Channel (stepper-only) |
|---|---|---|
| Stepper channels | 4 | **8** |
| Digital outputs (ULN2803A) | Yes | No — not needed |
| Shift registers | Yes | No — not needed |
| Port B RS485 (VFDs) | Yes | No — stepper-only board |
| Port A RS485 (sensors) | Yes | Yes (for temp/humid monitoring) |
| Ethernet | Yes | Yes |
| DIP switch | Yes | Yes |
| Same MCU / firmware framework | Yes | Yes |
| Same Modbus register map | Yes | Yes (extended to 8 channels) |

**Power for 8 channels (1:1 per motor):**

| Supply | Spec | Qty | Total Cost | Notes |
|---|---|---|---|---|
| **Mean Well EPS-120-24** | 120W, 24V, 5.0A, open-frame | **8** | **~$128** | One per motor — lose a supply, lose one door |

Each EPS-120-24 feeds one 2-pin power connector (J11–J18). The TMC2226 draws up to 3.0A from the motor coils; the chopper supply current is only ~0.19A from 24V, well under the 5.0A rating. The oversized supply ensures zero risk of current overdraw under any operating condition.

**8-channel Pulsar BOM adder over base Orbit:**

| Item | Cost |
|---|---|
| 8× TMC2226-SA | $20.00 |
| Passives (caps, sense resistors) | $3.50 |
| 8× motor connectors (4-pin, left edge) | $6.00 |
| 8× power connectors (2-pin, right edge) | $3.00 |
| Diode-OR for 3.3V regulator feed | $0.50 |
| Extra UART5 routing | $0.00 (MCU pin) |
| PCB area increase (~60%) | $3.00 |
| **Total adder** | **~$36.00** |

Shared across 8 actuators: **$4.50 per actuator** for the Pulsar-side electronics.

---

## 8. Mechanical Linkage — Rack and Pinion with Dual Pivot

This section describes the physical connection between the Pulsar's motor/gearbox assembly and the door. It is the primary reference for the mechanical engineer designing the actuator mounting frame.

### 8.1 Linkage Concept

The gearbox output shaft drives a **pinion gear** that meshes with a **rack** (linear gear bar) attached to the door. Both ends of the linkage use **pivot joints** to accommodate the door's arc of travel:

```
                            ┌─ Pivot A (frame mount)
                            │
  ┌─────────────────────┐   │   ┌──────────────────────┐
  │  Stepper + Gearbox  │───┤   │  Rack (linear gear)  │──── Door
  │  (NEMA 17 + 50:1)   │   │   │                      │
  └─────────────────────┘   │   └──────────────────────┘
        Fixed to frame      │         Fixed to door
                            │
  Pinion (on gearbox        └─ Pivot B (door attachment)
  output shaft)

  Side view:
             Pivot A
               ○─────── Rack ──────○ Pivot B
              /                      \
             /  Pinion engagement      \  Door hinge arc
            │                           │
     ═══════╪═══════                ════╪════════
        Frame mount                  Door leaf
```

### 8.2 Why Dual Pivots Are Required

A hinged door swings in an arc. The rack must follow this arc, but the gearbox is fixed to the building frame. Without pivots, the rack would bind against the pinion as the door opens — the geometry becomes over-constrained.

**Pivot A** (gearbox end) allows the motor/gearbox assembly to tilt as the door swings. **Pivot B** (door end) allows the rack attachment point to rotate relative to the door face. Together, the two pivots form a **floating link** that accommodates the changing angle between frame and door throughout the full range of travel.

| Pivot | Location | Axis | Load |
|---|---|---|---|
| **A** | Motor/gearbox bracket → frame | Vertical (parallel to hinge) | Full actuator force, static + dynamic |
| **B** | Rack end → door bracket | Vertical (parallel to hinge) | Full actuator force, transferred to door |

### 8.3 Mechanical Design Constraints

The mechanical engineer should design the mounting frame with these specifications:

**Motor/gearbox assembly:**
- NEMA 23 face: 57.0mm × 57.0mm, M5 mounting holes on 47.14mm centers
- Gearbox: NMRVS30 worm reducer, 14mm keyed output shaft (via RV30-AS), 50:1 ratio
- Combined motor+gearbox length: ~145–160mm (86mm motor body + gearbox)
- Weight: ~2.0–2.5 kg
- IP rating: Motor IP65 (dust-tight, water-jet protected)

**Pinion gear:**
- Mounts on 14mm gearbox output shaft (keyed)
- Module 1.5, 17 teeth (25.5mm pitch diameter)
- Material: hardened steel

**Rack:**
- Matching module to pinion
- Length = door travel distance + engagement margin
- Material: hardened steel  
- Mounts to door via Pivot B bracket

**Force and torque at the pinion:**

| Motor Config | Gearbox Output Torque | Pinion Radius | Linear Force (theoretical) | After Worm Eff. (~58%) |
|---|---|---|---|---|
| **3.0A (1.2 Nm, TMC2226 limit)** | **60.0 Nm** | **12.75mm (Module 1.5, 17T)** | **~1,058 lbs** | **~614 lbs** |
| 3.0A (1.2 Nm) | 60.0 Nm | 15mm (Module 1.5, 20T) | ~898 lbs | ~521 lbs |
| 3.0A (1.2 Nm) | 60.0 Nm | 10mm (Module 1.0, 20T) | ~1,349 lbs | ~783 lbs |

> Force = Torque / Pinion radius. Worm gears at 50:1 typically achieve 55–60% efficiency; 58% is used for the design calculation. The 23IP65-20 motor is rated 2.0 Nm at 5.0A but the TMC2226 limits drive current to 3.0A, producing 1.2 Nm at the motor shaft.

**Pivot requirements:**
- Both pivot pins must be hardened steel, minimum 10mm diameter
- Pivot A bracket: rated for full actuator force + vibration (bolted to structural frame member)
- Pivot B bracket: rated for full actuator force (bolted through door structural rail, not panel)
- Bushings: bronze or UHMW sleeve bearings (maintenance-free, corrosion-resistant)
- Pivot axis: parallel to door hinge axis

### Rack-and-Pinion Parts List — Single Actuator

Complete bill of materials for one intake door actuator, including motor, gearbox, rack-and-pinion linkage, and mounting hardware. Multiply by 8 for a full enclosure installation.

| Item | Specification | Example Source / P/N | Qty | Unit | Ext |
|---|---|---|---|---|---|
| **Drive** | | | | | |
| Stepper motor | NEMA 23, IP65, 5.0A, 2.0 Nm, 86mm body, 8mm D-shaft | StepperOnline 23IP65-20 | 1 | $49.87 | $49.87 |
| Worm gearbox | 50:1, NMRVS30, NEMA 23 input, self-locking | StepperOnline NMRVS30-G50-D11 | 1 | $34.00 | $34.00 |
| Shaft sleeve adaptor | 11mm → 8mm, for NMRVS30 input | StepperOnline RV30-K11-D8 | 1 | $5.00 | $5.00 |
| Output shaft | 14mm keyed, for NMRVS30 hollow bore | StepperOnline RV30-AS | 1 | $8.00 | $8.00 |
| Motor cable | 4-conductor 16AWG shielded, 10m (35 ft) | Belden 19364 or equiv 16/4 SH | 1 | $19.00 | $19.00 |
| | | *braid shield → enclosure ground bus, float at motor end* | | | |
| **Rack & Pinion** | | | | | |
| Pinion gear | Module 1.5, 17T, 14mm bore w/ keyway, 20° PA, steel | SDP-SI or equiv | 1 | $14.00 | $14.00 |
| Rack gear | Module 1.5, 20° PA, 12mm face, 1200mm (48"), steel | AliExpress / Amazon M1.5 20×20 rack | 1 | $40.00 | $40.00 |
| Rack guide | UHMW bearing block, 12mm slot, bolts to frame | McMaster 9513K34 or custom | 2 | $4.00 | $8.00 |
| **Pivot & Mounting** | | | | | |
| Pivot pin | 10mm × 50mm hardened steel dowel pin | McMaster 98381A548 | 2 | $1.50 | $3.00 |
| Pivot bushing | 10mm ID × 14mm OD, flanged bronze sleeve | McMaster 6338K416 | 4 | $1.80 | $7.20 |
| Motor/gearbox bracket | 3/16" steel plate, drilled for NEMA 23 + Pivot A | Fabricated or laser-cut | 1 | $12.00 | $12.00 |
| Door bracket | 3/16" steel plate, welded to door rail, Pivot B mount | Fabricated or laser-cut | 1 | $8.00 | $8.00 |
| Fasteners | M5 bolts, lock nuts, flat washers (frame + rack mount) | McMaster assorted | 1 set | $2.00 | $2.00 |
| **Per-Actuator Total** | | | | | **$210.07** |

> **Design load: 500 lbs.** The IP65 motor (23IP65-20, 2.0 Nm rated at 5.0A) is driven by the TMC2226 at its 3.0A limit, producing **1.2 Nm** (60% of rated). Through the 50:1 NMRVS30 worm gear: 1.2 Nm × 50 = 60 Nm theoretical, × 58% worm efficiency = **34.8 Nm effective**. At the Module 1.5 / 17T pinion (r=12.75mm): 34,800 / 12.75 = 2,729 N ≈ **614 lbs** — 23% above the 500 lb design load.
>
> **Why IP65:** Agricultural storage environments are high-humidity with condensation cycles. Standard open-frame motors corrode at the iron teeth and winding insulation. The 23IP65-20 has O-ring seals, sealed bearings, and hermetic cable feed-through — fully dust-tight and protected against water jets. Running at 60% of rated current means less heat, longer life, and the sealed housing retains all TMC2226 features (StallGuard4, CoolStep, StealthChop).

**Pinion tooth spec — Module 1.5, 17 teeth:**
- Pitch diameter: 25.5mm (1.004")
- Bore: 14mm with keyway (matches RV30-AS gearbox output shaft)
- Force at 500 lb door load: motor input required = 1.04 Nm (at worm gear input) — 87% of the driver-limited 1.2 Nm capacity

**Recommended rack length by door travel:**

| Door Swing | Hinge-to-Pivot B | Rack Travel | Rack Length (+ margin) | Approx Cost |
|---|---|---|---|---|
| 90° | 18" (460mm) | 25.5" (648mm) | 750mm rack | ~$26 |
| 90° | 24" (610mm) | 33.9" (861mm) | 1000mm rack | ~$32 |
| 60° | 24" (610mm) | 24.0" (610mm) | 700mm rack | ~$24 |

Rack prices scale roughly linearly with length. The 1200mm (48") rack in the BOM covers 36" door travel with full margin for pinion engagement and guide support at both endpoints. For shorter doors, order cut-to-length. Source racks from AliExpress or Amazon (~$25–40 for 1000–1500mm M1.5 steel) rather than McMaster (~$180+) — the savings are 5:1.

### 8.4 Mounting Geometry

The distance between the door hinge and the rack attachment point (Pivot B) determines the mechanical advantage and the required rack travel:

```
  Hinge ──── R ────── Pivot B
  │                     │
  │     Door swing      │ Rack force direction
  │         arc         │
  │                     ▼

  Rack travel = 2 × R × sin(θ/2)  where θ = door swing angle

  Example:
    R = 24" (rack attaches 24" from hinge)
    θ = 90° swing
    Rack travel = 2 × 24 × sin(45°) = 2 × 24 × 0.707 = 33.9"

    At 200 steps/sec through 50:1 gear (4 RPM output):
    Pinion circumference (Module 1.5, 20T): 94.2mm = 3.71"
    Linear speed: 4 × 3.71 = 14.8" per minute
    Time to open 90°: 33.9 / 14.8 = ~2.3 minutes at slow speed
    At 800 steps/sec: ~35 seconds
```

Shorter hinge-to-pivot distance = less rack travel but more force required. Longer distance = more rack travel but less force needed. The 24" dimension is a reasonable starting point for standard agricultural storage doors.

### 8.5 Environmental Considerations

The actuator operates in agricultural storage environments:
- Temperature: −30°F to +120°F (potato cooling to onion curing)
- Humidity: 0–100% RH (condensation possible)
- Dust/debris: grain dust, dirt, chaff
- Water exposure: rain and wash-down possible on door-mounted components

**Protection requirements:**
- Motor: IP54 minimum (sealed bearing, potted windings available)
- Gearbox: sealed, grease-filled (no external lubrication points)
- Rack and pinion: stainless steel or zinc-plated with periodic grease
- Pulsar electronics: mounted inside the control panel (NEMA 4X enclosure)
- Motor cables: outdoor-rated, UV-resistant jacket, strain-relieved at both ends
- Pivot bearings: sealed bronze or UHMW — no ball bearings (corrosion risk)

### 8.6 Motor Cable — Wiring and Grounding

Each motor connects to the Pulsar via a **4-conductor 16AWG shielded cable** (16/4 SH). The braided shield serves as the drain path — no separate drain conductor is needed.

| Conductor | Function | Color (suggested) |
|---|---|---|
| 1 | A+ (coil A positive) | Black |
| 2 | A− (coil A negative) | Green |
| 3 | B+ (coil B positive) | Red |
| 4 | B− (coil B negative) | Blue |
| Braid | Shield / drain | Bare (tinned copper braid) |

**Why 16AWG:** The remote enclosure mounts at the center of the door row. Motor cables fan out up to **32 feet (10m) in each direction**. At 3.5A on 16AWG over 32 ft:

$$V_{drop} = I \times R_{round-trip} = 3.5 \times (4.016 \times \frac{64}{1000}) = 0.90\text{V (3.75% of 24V)}$$

18AWG at the same 32 ft would drop 1.43V (6%) — marginal for maintaining StallGuard accuracy and motor torque. 16AWG keeps it under 4%.

**Drain wire grounding — single-point ground:**
- **Enclosure end:** Tie the cable braid/shield to the **enclosure safety ground bus** — the same copper bus bar bonded to the stainless enclosure chassis and AC safety ground (green wire). Do NOT connect the shield to the Pulsar board's 24V DC ground (0V rail) — this would couple shield noise into the TMC2226's current sense path and corrupt StallGuard readings.
- **Motor end:** Leave the shield braid **floating** (not connected to the motor housing). Single-point grounding prevents ground loop currents from flowing through the shield.
- All 8 shield braids terminate at the same enclosure ground bus → AC safety ground → building earth ground.

**Maximum cable distance by gauge:**

| Wire Length | Voltage | Gauge | V_drop @ 3.5A | Notes |
|---|---|---|---|---|
| < 3 ft (1 m) | 24V | 18–20 AWG | <0.14V | Typical panel wiring |
| 3–15 ft (1–5 m) | 24V | 18 AWG | 0.14–0.67V | Fine with shielded cable |
| 15–35 ft (5–10 m) | 24V | **16 AWG** | 0.67–0.98V | **Recommended for remote enclosure** |
| 35–50 ft (10–15 m) | 24V | 14 AWG | 0.57–0.81V | Long fan-out from center mount |
| > 50 ft (15 m) | — | — | — | Mount the driver at the motor instead |

**Control signals (STEP/DIR or UART):** Low-current logic lines are vulnerable to noise over long runs. Beyond 10 feet, use shielded cable. Beyond 30 feet, use RS-485 differential signaling or mount the Pulsar board at the motor and communicate back via Ethernet.

### 8.7 Remote Enclosure Deployment

For deployments where intake doors are distant from the main control panel, mount the 8-channel Pulsar in a **remote steel enclosure** near the motors instead of running long motor cables back to the panel.

**Why steel, not fiberglass:** Steel has ~170× better thermal conductivity than fiberglass (~50 W/m·K vs ~0.3 W/m·K). The enclosure wall itself becomes a heatsink, radiating heat into the surrounding air. Use **304 or 316 stainless steel** for corrosion resistance in agricultural environments.

**Recommended configuration — 8 intake doors:**

| Component | Qty | Per-Unit Heat | Purpose |
|---|---|---|---|
| 8-channel Pulsar (STM32G474 + W5500 + 8× TMC2226) | 1 | ~2W idle, ~10W all moving | Single board drives all 8 intake doors |
| Mean Well EPS-120-24 (120W, 24V, 5.0A) | 8 | ~4.5W at full load | One PSU per motor — lose a supply, lose one door |
| NEMA 23 IP65 stepper motor (23IP65-20) | 8 | ~1.5W moving, ~0.2W holding | TMC2226 limits to 3.0A; 60% of rated — runs cool |

**Why one PSU per motor:** Intake doors are the primary ventilation path. One shared supply makes all 8 doors a single point of failure. Individual supplies mean a failed PSU takes out exactly one door — the other seven keep working. The EPS-120-24 (120W, 24V, 5.0A, open-frame, ~$16) provides 67% headroom over the TMC2226's 3.0A motor current limit — the supply will never hiccup from current overdraw.

**One board, eight supplies:** The 8-channel Pulsar variant (Section 7.4) puts all 8 TMC2226 drivers on a single PCB. Each motor output connector (J3–J10) carries its own dedicated 24V feed from its own EPS-120-24. The Pulsar board's logic rail (3.3V regulator) taps off any one of the eight 24V feeds through a diode — if that particular PSU fails, the regulator picks up from the next live feed automatically.

**Power budget per channel (23IP65-20 at TMC2226 3.0A limit):**
- Motor coil resistance: 0.44Ω
- Coil voltage: 3.0A × 0.44Ω = 1.32V
- Chopper supply current: (3.0A × 1.32V) / 24V / 0.9 = **0.18A**
- TMC2226 driver quiescent: ~10mA
- **Worst case: ~0.19A** — well under the EPS-120-24's 5.0A rating (96% headroom)
- Low-resistance IP65 motor draws less supply current than high-resistance motors
- Motors run for seconds then idle — each PSU loafs at <4% capacity most of the time

**Thermal analysis (all 8 doors moving simultaneously — worst case):**

$$T_{rise} = \frac{P_{total}}{h \cdot A_{surface}}$$

Where:
- $P_{total}$ ≈ 8 × 2.0W + 10W (drivers) = 26W (all doors moving at once)
- $h$ ≈ 10 W/(m²·K) for natural convection on steel
- Target $T_{rise}$ = 20°C

$$A_{surface} = \frac{26}{10 \times 20} = 0.13 \text{ m}^2 \approx 1.4 \text{ ft}^2$$

A standard **16"×12"×8" stainless NEMA 4X** enclosure provides ample surface area with room for 8 PSUs laid out in two rows. In an unheated building (35–55°F typical for storage), worst-case junction temperatures stay well under limits:

- Ambient: 55°F (13°C) + 20°C rise = 33°C — far below TMC2226's 150°C limit
- Typical (doors idle, holding or de-energized): <5W total heat — negligible

**Enclosure contents:**
- 8× EPS-120-24 open-frame PSUs (5.0"×3.0"×1.2" each, two rows of four)
- 1× 8-channel Pulsar board
- Cable glands: 8× motor cables + 1× Ethernet + 1× AC mains entry
- DIN rail or standoff mounting

**Connection to main panel:**
A single Ethernet cable runs from the remote enclosure back to the Nova card bus switch. The Pulsar appears as a single node on the 192.168.1.x card bus with 8 stepper channels addressed via Modbus registers.

---

## 9. Comms-Loss Behavior

### 9.1 Safe State — Autonomous Failsafe Close

If Nova stops communicating, the Pulsar's comms-loss handler (defined in the main architecture document) applies to stepper channels:

| Phase | Behavior |
|---|---|
| 0–5 seconds | Continue holding current position |
| 5s + hold_timeout (default 120s) | Drive to safe position (configurable) |

**Configurable safe positions per channel:**

| Safe position option | Behavior | Use case |
|---|---|---|
| `SAFE_HOLD` | Hold current position indefinitely | Default for most doors |
| `SAFE_CLOSE` | Drive to position 0 (fully closed) | Cooling doors — prevent heat ingress |
| `SAFE_OPEN` | Drive to position N_total (fully open) | Emergency ventilation dampers |
| `SAFE_PARTIAL` | Drive to configured % of range | Maintain minimum ventilation |

**How failsafe close works without Nova:**

Nova pushes the safe-state configuration to the Pulsar's flash at commissioning time. Once stored, the Pulsar can execute the failsafe independently — no network communication required.

When `SAFE_CLOSE` is configured and the comms-loss timeout expires:
1. Pulsar sets run current to stall-retry level (60%)
2. Drives toward position 0 at normal speed
3. Uses StallGuard for end-stop detection — if the door reaches the mechanical close stop, the stall confirms position 0
4. Reduces to hold current (20%) and holds closed indefinitely
5. When Nova reconnects, it reads the Pulsar's position and resumes normal control

The door closes safely even if the Ethernet switch fails, Nova crashes, or the network cable is unplugged. The Pulsar acts on its own stored rules.

### 9.2 Power Loss / Watchdog Reset

On Pulsar reboot (power cycle or watchdog):
1. Motor position is unknown (steppers have no absolute position memory)
2. Pulsar sets `CALIBRATED = 0` in status register
3. Nova reads the cleared flag on next poll
4. Nova sends `CMD_CALIBRATE` to re-establish position reference
5. Auto-calibration runs (~75 seconds), normal operation resumes

**Calibration persistence — avoiding unnecessary re-calibration:**

The Pulsar stores `N_total` (calibrated range) in flash. Nova also stores each door's `N_total` in its QSPI zone map. This enables two optimizations:

| Scenario | Recovery |
|---|---|
| **Watchdog reset** (no power loss) | Pulsar's backup SRAM retains position. Pulsar reports `CALIBRATED = 1` on reboot. Nova verifies N_total matches its stored value → **no re-calibration needed**, resumes immediately. |
| **Power cycle** (position lost) | Pulsar reports `CALIBRATED = 0`. Nova pushes its stored N_total to the Pulsar, then sends a single stall-to-close re-home. Pulsar drives to close stop (~25s), confirms position 0 → **25 seconds** instead of full 75-second calibration. |
| **New motor or first-time** | Nova has no stored N_total for this channel. Sends full `CMD_CALIBRATE` → close-to-open-to-close sweep → **75 seconds**. |

Nova can also compare each channel's N_total over time. If a door's total step count drifts (expanding/contracting frame, bearing wear changing effective travel), Nova logs a maintenance note: "Door #3 travel range changed from 5,012 to 4,890 steps — check mechanical alignment."

---

## 10. Example Deployment

### 10.1 Potato Storage — 4 Doors, 2 Vents

```
Nova ── Ethernet Switch ─┬─ Orbit #1 (Storage I/O)      Fans, heat, humid, sensors
                         ├─ Pulsar #2 (Door/Vent)      4× TMC2226 doors + 2× TMC2226 vents
                         ├─ Orbit #3 (Refrig)            Compressors, defrost, cond fans
                         └─ Orbit #4 (Refrig 2)          Additional compressor stages

Pulsar #2 register usage:
  Channel 0: Door 1  (NEMA 17, 3.5A + 50:1 worm gear)
  Channel 1: Door 2  (NEMA 17, 3.5A + 50:1 worm gear)
  Channel 2: Door 3  (NEMA 17, 3.5A + 50:1 worm gear)
  Channel 3: Door 4  (NEMA 17, 3.5A + 50:1 worm gear)
  (Vents on channels 4–5 if 6-channel variant, or second Pulsar)

Nova PID loop for doors:
  Input:  plenum_temp_avg - setpoint
  Output: 0–100% → mapped to step position via N_total
  Update: every REG_DOOR_PID_U seconds (configurable, same as AS2)
  Each door gets the same target % — individual Pulsars handle the motion independently
```

### 10.2 Onion Curing Bay — 8 Doors (8-Channel Pulsar)

```
Nova ── Ethernet Switch ─┬─ Orbit #1 (Bay 1 I/O)        Sensors, burner
                         ├─ Pulsar #2 (Bay 1 Doors)    8× doors (8-ch Pulsar)
                         └─ Orbit #3 (Bay 1 VFDs)       8× ACS380 on RS485

  Previously: AS2 single pair of ganged OPEN/CLOSE relays driving all 8 doors together
  Now: 8 independent stepper channels on ONE Pulsar
       Single Ethernet cable, single DIP address, single 24V/30A supply
       Each door independently positioned by Nova
       Individual stall detection per door
       Stalled door #5 doesn't affect doors #1–4, #6–8
       All 8 doors open simultaneously on cure → full speed in seconds
```

The 8-channel variant is purpose-built for this exact use case. One board replaces what previously took two 4-channel Pulsars — fewer cables, fewer addresses, simpler wiring.

```
  Pulsar #2 (8-channel) register usage:
    Channel 0: Bay Door 1   ─┐
    Channel 1: Bay Door 2    │
    Channel 2: Bay Door 3    │  All 8 move simultaneously
    Channel 3: Bay Door 4    │  when cure conditions are met.
    Channel 4: Bay Door 5    │  Each reports position and
    Channel 5: Bay Door 6    │  load independently.
    Channel 6: Bay Door 7    │
    Channel 7: Bay Door 8   ─┘

  Nova cure state machine:
    Cure available → write REG_STEP_TARGET[0..7] = N_total, SPEED_MAX
    → All 8 doors open at full speed within microseconds
    → Each reports AT_TARGET individually as they finish
    → Door 3 stalls? Doors 1,2,4-8 still reach full open.
```

---

## 11. Commissioning Workflow

### 11.1 First-Time Setup

1. **Mount motors** to doors/vents with appropriate gear reducers
2. **Wire motor cables** to Pulsar J4–J7 connectors (4-pin: A+, A−, B+, B−)
3. **Connect 24V power supply** to J8
4. **Set DIP switch** to assign Modbus address
5. **Connect Ethernet** — Pulsar powers up, appears on Nova's scan
6. **Nova auto-detects** Pulsar (reads firmware feature register)
7. **Configure in UI:**
   - Assign channels to zone (door 1, door 2, vent 1, etc.)
   - Set run current per channel (matched to motor rating)
   - Set StallGuard threshold (default works for most motors)
   - Set safe-state behavior per channel
8. **Nova pushes config** to Pulsar flash (same as safety rules)
9. **Run calibration** — Nova sends `CMD_CALIBRATE` to all channels
   - Each door/vent drives to close stop, then open stop, counting steps
   - N_total stored, PID range established
   - Channels report `CALIBRATED` — Pulsar is operational

### 11.2 Motor Replacement

1. Swap the physical motor
2. From UI: tap "Recalibrate" on the affected channel
3. Nova sends `CMD_CALIBRATE` for that channel only
4. 75 seconds later, the new motor's range is mapped

No firmware update, no settings change, no Pulsar replacement. Same procedure whether it's a NEMA 17 or NEMA 23 — the calibration discovers the range automatically.

---

## 12. UI Reporting — Door Status Page

### 12.1 Position Precision

With 256 microstepping and a 50:1 worm gearbox, the position resolution is far beyond what's needed:

```
NEMA 17 (1.8°/step) × 256 microsteps = 51,200 microsteps per motor revolution
51,200 × 50 (gear ratio) = 2,560,000 microsteps per output shaft revolution

For a door with 90° of travel: 640,000 microsteps total range
Position resolution: 0.000016% of travel per microstep
```

In practice, display to 0.1% — which represents ~640 microsteps. That level of precision is guaranteed accurate because the step count is tracked from a known calibrated reference, not estimated from pulse timing like the AS2.

### 12.2 Door Status Display

The UI door page shows real-time data from Pulsar registers polled at 1 Hz:

```
┌─────────────────────────────────────────────────────────┐
│  DOORS — Building A                         10:42:15 AM │
│─────────────────────────────────────────────────────────│
│                                                         │
│  Door 1  ████████████░░░░░░░░  62.4%     TRACKING      │
│          Target: 65.0%    Load: ████████░░  78%         │
│          Speed: Normal    Current: 70%                  │
│                                                         │
│  Door 2  ████████████████████  100.0%    AT TARGET      │
│          Target: 100.0%   Load: █████████░  92%         │
│          Speed: —         Current: Hold (20%)           │
│                                                         │
│  Door 3  ██████████████░░░░░░  71.8%     TRACKING      │
│          Target: 73.0%    Load: ██████░░░░  58%         │
│          Speed: Creep     Current: 70%                  │
│                                                         │
│  Door 4  ░░░░░░░░░░░░░░░░░░░░  0.0%     ⚠ STALLED     │
│          Target: 45.0%    Load: ░░░░░░░░░░  0%          │
│          Speed: —         Current: —                    │
│          Retry 3/6 — next attempt in 7s                 │
│                                                         │
│─────────────────────────────────────────────────────────│
│  PID Output: 65.0%   Plenum: 52.3°F   Setpoint: 52.0°F │
│  State: COOLING     Error: +0.3°F     Speed: Creep      │
└─────────────────────────────────────────────────────────┘
```

### 12.3 Data Fields per Channel

| UI Field | Source | Register / Calculation |
|---|---|---|
| **Position %** | Pulsar | `REG_STEP_POSITION[n] / N_total × 100` |
| **Position bar** | Pulsar | Visual progress bar from position % |
| **Target %** | Nova | `REG_STEP_TARGET[n] / N_total × 100` |
| **Status** | Pulsar + Nova | `REG_STEP_STATUS` bitmask → text label |
| **Load %** | Pulsar | `REG_STEP_LOAD[n] / 1023 × 100` |
| **Load bar** | Pulsar | Color-coded: green (>60%), yellow (30–60%), red (<30%) |
| **Speed** | Nova | Current speed regime: Fast / Normal / Creep / Hold |
| **Current** | Nova | IRUN setting and context (Normal / Calibrating / Hold) |
| **Stall count** | Nova | Retry counter: "Retry 3/6 — next attempt in Ns" |
| **Calibration** | Nova | "Calibrated 2h ago" or "CALIBRATING..." |

### 12.4 Load Trend Graph (Maintenance View)

The service tech page can show a 30-day load trend per door — data Nova logs to RPi5 storage:

```
Load %
100│
 90│  ──────────────────────────────────────
 80│                                   ╲
 70│                                    ╲──── Door #3
 60│                                         declining
 50│
 40│  ════════════════════════════════════════ Door #1 (healthy)
 30│
 20│
 10│
  0│─────────────────────────────────────────
    Mar 1      Mar 8      Mar 15     Mar 22

   ⚠ Door #3: Average load declining — schedule bearing inspection
```

This predictive maintenance view is **impossible with relay-driven actuators** — they have zero load feedback. The stepper's continuous load value, logged over time, reveals mechanical degradation weeks before it causes a stall. Service techs can schedule maintenance proactively instead of responding to 2 AM alarm calls.

### 12.5 Comparison with Current AS2 Door Display

| Data Point | AS2 (Current) | Constellation Stepper |
|---|---|---|
| Door position | Estimated from pulse time ("~65%") | Exact step count ("62.4%") |
| Position accuracy | ±5–10% drift over time | ±0.001% (from calibration reference) |
| Load / resistance | Not available | Real-time 0–100% per channel |
| Stall status | Not detected | Instant detection + retry counter |
| Individual door status | One combined "Door: XX%" | Per-door position, load, status |
| Predictive maintenance | Not possible | 30-day load trending per channel |
| Motor current | Not known | Displayed per context |
| Calibration status | Not applicable | Per-channel calibration state |

---

## 13. Cost Comparison

### 13.1 Per-Actuator Cost: Relay vs. Stepper

| Component | AS2 Relay Method | Constellation Stepper |
|---|---|---|
| Actuator | Gear motor (~$30–60) | Stepper + gear reducer (~$40–70) |
| Driver | 2× relay + flyback ($3) | TMC2226 + passives ($2.95) |
| Position feedback | None | StallGuard (built into TMC, $0) |
| Limit switches | 2× micro switch + wiring ($8) | None needed ($0) |
| Control wires | 2 wires (open/close relay coils) | 4 wires (motor coils) |
| **Per-actuator total** | **~$41–71** | **~$43–73** |

Similar cost, but the stepper provides closed-loop position feedback, stall detection, programmable force limiting, load monitoring, and predictive maintenance — all of which the relay method cannot do at any price.

### 13.2 Pulsar Board Cost Adder

Adding 4 stepper channels to the standard Orbit board:

| Item | Cost |
|---|---|
| 4× TMC2226-SA | $10.00 |
| Passives (caps, sense resistors) | $2.00 |
| 4× motor connectors | $3.00 |
| PCB area increase (~30%) | $1.50 |
| **Total adder** | **~$16.50** |

Shared across 4 actuators: **$4.13 per actuator** for the Pulsar-side electronics.

### 13.3 Complete Parts List — Standard Door (2.8A Motor)

The cost-effective option for most standard storage doors.

| Component | Part Number / Spec | Qty | Unit Cost | Ext Cost | Notes |
|---|---|---|---|---|---|
| **Stepper motor** | NEMA 17, 2.8A, 0.65 Nm, 48mm body | 1 | $12.00 | $12.00 | 1.8°/step, bipolar |
| **Worm gearbox** | 50:1 ratio, NEMA 17 flange mount | 1 | $28.00 | $28.00 | Self-locking, 8mm output shaft |
| **Driver IC** | TMC2226-SA (QFN 5×5mm) | 1 | $2.50 | $2.50 | 3.0A RMS, StallGuard4 |
| **Sense resistors** | 0.075Ω 1% 0603 | 2 | $0.05 | $0.10 | Sets 3.0A full-scale |
| **Bulk capacitor** | 100µF 50V electrolytic | 1 | $0.35 | $0.35 | Motor 24V rail decoupling |
| **Bypass caps** | 100nF 50V 0603 ceramic | 3 | $0.02 | $0.06 | VCC_IO, VS, 5VOUT |
| **Power supply** | 24V 5A DIN-rail (Mean Well HDR-60-24) | 1 | $18.00 | $18.00 | 120W, per-channel supply |
| **Motor cable** | 4-conductor 16AWG shielded, 10m | 1 | $19.00 | $19.00 | A+, A−, B+, B−, braid drain |
| **Motor connector** | 4-pin Molex Micro-Fit 3.0 | 1 | $0.75 | $0.75 | Panel-mount, keyed |
| | | | **Total:** | **$80.76** | |

**Output torque:** 0.65 Nm × 50 = **32.5 Nm** at gearbox output shaft. Linear force depends on rack-and-pinion geometry (see Section 8): **~480 lbs** with Module 1.5 pinion, **~720 lbs** with Module 1.0 pinion.

### 13.4 Complete Parts List — High-Torque Door (3.5A Motor)

The recommended option for doors requiring maximum force (heavy doors, ice-prone environments).

| Component | Part Number / Spec | Qty | Unit Cost | Ext Cost | Notes |
|---|---|---|---|---|---|
| **Stepper motor** | NEMA 17, 3.5A, 1.0 Nm, 60mm body | 1 | $18.00 | $18.00 | 1.8°/step, bipolar, high-torque |
| **Worm gearbox** | 50:1 ratio, NEMA 17 flange mount | 1 | $28.00 | $28.00 | Self-locking, 8mm output shaft |
| **Driver IC** | TMC2226-SA (QFN 5×5mm) | 1 | $2.50 | $2.50 | 3.0A RMS max — motor runs at 85% driver rating |
| **Sense resistors** | 0.075Ω 1% 0603 | 2 | $0.05 | $0.10 | Sets 3.0A full-scale |
| **Bulk capacitor** | 100µF 50V electrolytic | 1 | $0.35 | $0.35 | Motor 24V rail decoupling |
| **Bypass caps** | 100nF 50V 0603 ceramic | 3 | $0.02 | $0.06 | VCC_IO, VS, 5VOUT |
| **Power supply** | 24V 5A DIN-rail (Mean Well HDR-60-24) | 1 | $18.00 | $18.00 | 120W, per-channel supply |
| **Motor cable** | 4-conductor 16AWG shielded, 10m | 1 | $19.00 | $19.00 | A+, A−, B+, B−, braid drain |
| **Motor connector** | 4-pin Molex Micro-Fit 3.0 | 1 | $0.75 | $0.75 | Panel-mount, keyed |
| | | | **Total:** | **$86.76** | |

**Output torque:** 1.0 Nm × 50 = **50 Nm** at gearbox output shaft. At TMC2226's 3.0A limit (85% of motor rated): **42.9 Nm**. Linear force depends on rack-and-pinion geometry (see Section 8): **~640 lbs** with Module 1.5 pinion, **~960 lbs** with Module 1.0 pinion.

**Note on 3.5A motor with 3.0A driver:** The TMC2226 is rated 3.0A RMS. Running a 3.5A motor at the driver's 3.0A limit gives 85% of rated torque (0.85 Nm vs 1.0 Nm). Through the 50:1 gearbox this still produces 42.5 Nm at the output shaft — more than adequate for all standard door applications (see force table in Section 8.3). For true 3.5A operation at 100% torque, use two TMC2226s in parallel per channel or step up to the TMC5160.

### 13.5 Shared Power Supply Option (4-Channel Pulsar)

Instead of one 5A supply per channel, a single larger supply can feed the entire Pulsar:

| Supply | Spec | Cost | Handles |
|---|---|---|---|
| Mean Well HDR-100-24 | 24V / 4.2A | $22.00 | 1 channel (with headroom) |
| **Mean Well NDR-480-24** | **24V / 20A** | **$55.00** | **4 channels simultaneously** |

4 channels with individual 5A supplies: 4 × $18 = **$72**
4 channels with one shared 20A supply: **$55** (saves $17 per Pulsar)

### 13.6 Recommended PSU — Remote Enclosure (1:1 Per Motor)

For remote enclosures housing an 8-channel Pulsar driving intake doors (see Section 8.7):

| Supply | Spec | Cost | Per-Channel Budget |
|---|---|---|---|
| **Mean Well EPS-120-24** | 120W, 24V, 5.0A, open-frame | ~$16 | IP65 motor at TMC2226 3.0A limit — 67% headroom, zero risk of current overdraw |

**One PSU per motor × 8 = ~$128 total** for all power supplies in the remote enclosure. Each EPS-120-24 feeds one motor channel on the Pulsar board. The board's 3.3V logic rail taps off the 24V bus through a diode-OR from all eight feeds.

**Why EPS-120-24 instead of EPS-65-24:** The EPS-65-24 is rated 2.71A — less than the TMC2226’s 3.0A motor current limit. While the chopper driver’s 24V supply current is only ~0.19A (buck converter principle), the supply must handle transient and inrush conditions without hiccup. The EPS-120-24 (5.0A, ~$16) provides 67% headroom over the 3.0A limit and matches the motor’s full 5.0A rating — no risk of overdraw under any condition.

**Why individual supplies for intake doors:**
- Intake doors are the primary ventilation path — losing all doors to a single PSU failure is unacceptable
- One PSU fails = one door down, seven still operate
- Simple field service: swap the failed $16 supply, done
- No inrush coordination — each supply only sees its own motor

**Main panel PSU:**
For the main control panel (Nova + RPi5 + panel I/O), use a DIN-rail supply such as the **Mean Well EDR-120-24** (120W, 24V, 5A, ~$35) — different form factor for a different environment.

### 13.7 Complete System Cost — 8-Channel Remote Enclosure

Full cost breakdown for an 8-intake-door installation with remote-mounted Pulsar.

**A. Pulsar Board — Components (on-board)**

| Item | Part / Spec | Qty | Unit | Ext | Notes |
|---|---|---|---|---|---|
| STM32G474CE | LQFP-64, Cortex-M4F 170 MHz | 1 | $4.50 | $4.50 | MCU |
| W5500 | LQFP-48, hardwired TCP/IP | 1 | $2.00 | $2.00 | Ethernet controller |
| RJ45 jack w/ magnetics | Tab-up, integrated LEDs | 1 | $1.50 | $1.50 | J1 |
| RS485 transceiver | MAX3485 or SP3485 | 1 | $0.60 | $0.60 | Sensors port |
| 3.3V SMPS regulator | LM2596 or RT8059 module | 1 | $0.80 | $0.80 | Logic rail |
| Diode-OR (Schottky) | SS34 (3A, 40V) | 8 | $0.06 | $0.48 | One per 24V feed to logic rail |
| 25 MHz crystal | HC49 or 3.2×2.5 | 1 | $0.20 | $0.20 | STM32 HSE |
| 25 MHz crystal | HC49 or 3.2×2.5 | 1 | $0.20 | $0.20 | W5500 clock |
| DIP switch | 4-pos SPST | 1 | $0.30 | $0.30 | Modbus address |
| Misc passives (MCU) | Bypass caps, pull-ups, ESD | — | — | $1.50 | Decoupling, reset, boot |
| **TMC2226-SA** | **HTSSOP-28, 3.0A RMS** | **8** | **$2.50** | **$20.00** | **Stepper drivers** |
| Sense resistors | 0.075Ω 1% 0603 | 16 | $0.05 | $0.80 | 2 per driver |
| Bulk caps (VMOT) | 100µF 50V electrolytic | 8 | $0.35 | $2.80 | 1 per driver |
| Bypass caps (driver) | 100nF 50V 0603 | 24 | $0.02 | $0.48 | 3 per driver |
| Motor connectors | 4-pin Molex Micro-Fit 3.0 | 8 | $0.75 | $6.00 | J3–J10, left edge |
| Power connectors | 2-pin 5.08mm screw terminal | 8 | $0.35 | $2.80 | J11–J18, right edge |
| RS485 connector | 3-pin 5.08mm screw terminal | 1 | $0.30 | $0.30 | J2 |
| | | | **Component total:** | **$45.26** | |

**B. PCB Fabrication & Assembly**

| Item | Spec | Cost | Notes |
|---|---|---|---|
| PCB fabrication | 4.5"×5.5", 4-layer, 1oz Cu, ENIG | ~$5.00 | JLCPCB/PCBWay qty 10 price |
| SMT assembly (top side) | ~80 placements | ~$15.00 | JLCPCB assembly, qty 10 |
| Through-hole connectors | 17 connectors, hand-solder or wave | ~$3.00 | RJ45 + screw terminals + Micro-Fit |
| Stencil | Shared across batch | ~$1.00 | Amortized |
| | | **Assembly total:** | **~$24.00** |

**C. Pulsar Board — Total Landed Cost**

| | Cost |
|---|---|
| Components (A) | $45.26 |
| PCB + Assembly (B) | $24.00 |
| **Board total** | **~$69** |
| Per channel (÷ 8) | **~$8.63** |

**D. Power Supplies**

| Item | Spec | Qty | Unit | Ext |
|---|---|---|---|---|
| Mean Well EPS-120-24 | 120W, 24V, 5.0A, open-frame | 8 | $16.00 | $128.00 |
| | | | **PSU total:** | **$128.00** |

**E. Motors, Gearboxes & Rack-and-Pinion Linkage (per door × 8)**

| Item | Spec / P/N | Qty | Unit | Ext |
|---|---|---|---|---|
| NEMA 23 IP65 stepper motor | 5.0A, 2.0 Nm, IP65 — StepperOnline 23IP65-20 | 8 | $49.87 | $398.96 |
| Worm gearbox | 50:1, NMRVS30, NEMA 23, self-locking — StepperOnline NMRVS30-G50-D11 | 8 | $34.00 | $272.00 |
| Shaft sleeve adaptor | 11mm → 8mm — StepperOnline RV30-K11-D8 | 8 | $5.00 | $40.00 |
| Output shaft | 14mm keyed — StepperOnline RV30-AS | 8 | $8.00 | $64.00 |
| Motor cable | 4-conductor 16AWG shielded, 10m (35 ft) — Belden 19364 or equiv | 8 | $19.00 | $152.00 |
| Pinion gear | Module 1.5, 17T, 14mm bore, steel — SDP-SI or equiv | 8 | $14.00 | $112.00 |
| Rack gear | Module 1.5, 1200mm (48"), steel — AliExpress / Amazon | 8 | $40.00 | $320.00 |
| Rack guide | UHMW bearing block, 12mm slot — McMaster 9513K34 | 16 | $4.00 | $64.00 |
| Pivot pins | 10mm × 50mm hardened dowel — McMaster 98381A548 | 16 | $1.50 | $24.00 |
| Pivot bushings | 10mm ID, flanged bronze sleeve — McMaster 6338K416 | 32 | $1.80 | $57.60 |
| Motor/gearbox bracket | 3/16" steel plate, NEMA 23 + Pivot A — fabricated | 8 | $12.00 | $96.00 |
| Door bracket | 3/16" steel plate, Pivot B mount — fabricated | 8 | $8.00 | $64.00 |
| Fasteners | M5 bolts, lock nuts, washers per actuator | 8 sets | $2.00 | $16.00 |
| | | | **Actuator total:** | **$1,680.56** |

**F. Enclosure & Wiring**

| Item | Spec | Qty | Unit | Ext |
|---|---|---|---|---|
| Stainless NEMA 4X enclosure | 16"×12"×8", 304 SS | 1 | $95.00 | $95.00 |
| Cable glands | PG13.5, stainless | 10 | $2.00 | $20.00 |
| Ethernet cable | Cat5e outdoor, to panel switch | 1 | $15.00 | $15.00 |
| DIN rail | 35mm, 12" length | 1 | $4.00 | $4.00 |
| AC wiring | 14AWG, breaker to enclosure | 1 | $8.00 | $8.00 |
| Terminal blocks, misc | Leveraged stock | — | — | $10.00 |
| | | | **Enclosure total:** | **$152.00** |

**G. System Total — 8 Intake Doors**

| Category | Cost | Per Door |
|---|---|---|
| Pulsar board (C) | $69.00 | $8.63 |
| Power supplies (D) | $128.00 | $16.00 |
| Motors, gearboxes & linkage (E) | $1,680.56 | $210.07 |
| Enclosure & wiring (F) | $152.00 | $19.00 |
| **System total** | **$2,029.56** | **$253.69** |

> The $1,680.56 actuator total reflects IP65 waterproof NEMA 23 motors (23IP65-20) with NMRVS30 worm gearboxes, 1200mm (48") rack-and-pinion linkage sized for 36" door travel, and 35-ft 16/4 shielded motor cables for center-mounted enclosure fan-out — see Section 8.3 parts list for individual part numbers and sourcing. Source racks from AliExpress/Amazon (~$40 ea) rather than McMaster (~$180+).

For comparison, 8 doors with the current AS2 relay method (gear motors + relays + limit switches + wiring + linkage + no position feedback): ~$600–900 for the actuator hardware alone, with zero stall detection, zero position accuracy, and the total-system-block failure mode.

The Constellation Pulsar adds ~$700–1,000 over the relay approach for 8 doors and delivers per-door position control, independent stall detection, automatic extend/retract limit calibration, load monitoring, IP65 motors rated for high-humidity storage, rack-and-pinion precision, predictive maintenance, and graceful degradation.

| Feature | Specification |
|---|---|
| **Driver IC** | TMC2226-SA (standard, 3.0A), TMC5160 (high-power, 20A) |
| **Standard motor** | NEMA 23 IP65 (23IP65-20), 2.0 Nm rated at 5.0A, driven at TMC2226 3.0A limit (1.2 Nm) |
| **Power supply** | 24V DC (commodity DIN-rail), EPS-120-24 per motor or 20A/30A shared |
| **Gearbox** | 50:1 NMRVS30 worm gear, self-locking, NEMA 23 flange |
| **Output torque** | 60 Nm theoretical / 34.8 Nm after worm efficiency; 614 lbs linear force via rack and pinion |
| **Mechanical linkage** | Rack and pinion with dual pivot joints (Section 8) |
| **Stall detection** | StallGuard4, sensorless, <40 ms at calibration speed |
| **Load monitoring** | Continuous 0–1023 load value, predictive maintenance trending |
| **Wind gust handling** | CoolStep auto-increases current to push through — hardware, no firmware |
| **Position resolution** | 256 microsteps/full step × gear ratio (0.000016% per microstep) |
| **UI reporting** | Per-door position (0.1%), load bar, status, stall count, 30-day trend |
| **Auto-calibration** | Stall-to-stall, ~75 seconds, no limit switches |
| **PID integration** | Nova PID output (0–100%) → step position via N_total |
| **Speed control** | Error-proportional traverse rate (creep near setpoint, full speed on mode change) |
| **Torque limiting** | Programmable IRUN per context — electronic slip clutch at $0 |
| **Retry on stall** | 10-second interval, configurable max retries, motor-safe |
| **Independent fault handling** | One stalled door does not affect any other actuator |
| **Comms-loss safe state** | Configurable: hold, close, open, or partial position |
| **Board variants** | 4-channel (general-purpose Pulsar) or 8-channel (purpose-built stepper-only) |
| **Channels per Pulsar** | 4 (general) or 8 (stepper-only), expandable with TMC5160 for high-power |
| **Per-actuator cost** | ~$64 (2.8A) or ~$70 (3.5A) complete with supply and gearbox |
| **Pulsar board cost adder** | ~$16.50 / 4-ch ($4.13/actuator) or ~$32 / 8-ch ($4.00/actuator) |

---

## 14. Prototype — Single-Door Bench Test

Before building the custom Pulsar board, validate the stepper + worm gearbox + rack-and-pinion concept on the actual door using off-the-shelf hardware and an RPi5. This proves: motor torque is sufficient for the spring, StallGuard detects the end stops, rack-and-pinion engagement is solid, and cable length doesn't cause issues.

### 14.1 Prototype Parts List

The **Pololu Tic T249** is a fully self-contained stepper motor controller built around the TMC2209 (same driver family as the TMC2226, identical StallGuard4 and UART protocol). It connects to the RPi5 via USB — no GPIO wiring, no level shifters, no breadboard.

| Feature | Tic T249 |
|---|---|
| Driver IC | TMC2209 (StallGuard4, CoolStep, StealthChop) |
| Max current | 2.1A RMS continuous (peak 2.8A) |
| Motor voltage | 5.5V–48V |
| Interface | USB, TTL serial, I²C, RC pulse |
| Motion controller | Built-in — acceleration, deceleration, speed limits, position commands |
| Connectors | Screw terminals for motor (4-pin) + power (2-pin) |
| Software | Tic Control Center GUI (Linux/Windows) + Python library (`ticcmd`, `pytic`) |
| Cost | ~$25 |

**Current limit note:** The TMC2209 on the Tic T249 is rated 2.1A continuous vs the TMC2226's 3.0A. At 2.1A the motor produces 67% of rated torque → 0.44 Nm → **22 Nm through the 50:1 gearbox** → **510 lbs through the pinion gear**. This is 70% above the 300 lb spring requirement — more than sufficient for prototype validation. The production Pulsar board's TMC2226 at 3.0A will have even more headroom.

**A. Electronics**

| Item | Specification | Source / P/N | Qty | Unit | Ext |
|---|---|---|---|---|---|
| Raspberry Pi 5 | 4GB (already on hand) | — | 1 | — | $0.00 |
| RPi5 power supply | 27W USB-C PD (already on hand) | — | 1 | — | $0.00 |
| Pololu Tic T249 | TMC2209, 2.1A, USB + serial + I²C, StallGuard4 | Pololu #3138 | 1 | $24.95 | $24.95 |
| Mean Well EPS-120-24 | 120W, 24V, 5.0A, open-frame | Mouser 709-EPS120-24 | 1 | $16.00 | $16.00 |
| USB-A to Micro-USB cable | Data cable for RPi5 → Tic T249 | Any | 1 | $3.00 | $3.00 |
| | | | | **Electronics total:** | **$43.95** |

**B. Motor & Gearbox**

| Item | Specification | Source / P/N | Qty | Unit | Ext |
|---|---|---|---|---|---|
| NEMA 23 IP65 stepper motor | 5.0A, 2.0 Nm, IP65, 86mm body, 8mm D-shaft | StepperOnline 23IP65-20 | 1 | $49.87 | $49.87 |
| Worm gearbox | 50:1, NMRVS30, NEMA 23 input, self-locking | StepperOnline NMRVS30-G50-D11 | 1 | $34.00 | $34.00 |
| Shaft sleeve adaptor | 11mm → 8mm, for NMRVS30 input | StepperOnline RV30-K11-D8 | 1 | $5.00 | $5.00 |
| Output shaft | 14mm keyed, for NMRVS30 hollow bore | StepperOnline RV30-AS | 1 | $8.00 | $8.00 |
| Motor cable | 4-conductor 16AWG shielded, 10m (35 ft) | Belden 19364 or equiv 16/4 SH | 1 | $19.00 | $19.00 |
| | | | | **Motor total:** | **$115.87** |

**C. Rack & Pinion Linkage**

| Item | Specification | Source / P/N | Qty | Unit | Ext |
|---|---|---|---|---|---|
| Pinion gear | Module 1.5, 17T, 14mm bore w/ keyway, steel | SDP-SI or equiv | 1 | $14.00 | $14.00 |
| Rack gear | Module 1.5, 1200mm (48"), steel | AliExpress / Amazon M1.5 20×20 | 1 | $40.00 | $40.00 |
| Rack guide | UHMW bearing block, 12mm slot | McMaster 9513K34 or custom | 2 | $4.00 | $8.00 |
| Pivot pins | 10mm × 50mm hardened steel dowel | McMaster 98381A548 | 2 | $1.50 | $3.00 |
| Pivot bushings | 10mm ID, flanged bronze sleeve | McMaster 6338K416 | 4 | $1.80 | $7.20 |
| Motor/gearbox bracket | 3/16" steel plate, NEMA 23 + Pivot A | Fabricated or laser-cut | 1 | $12.00 | $12.00 |
| Door bracket | 3/16" steel plate, Pivot B mount | Fabricated or laser-cut | 1 | $8.00 | $8.00 |
| Fasteners | M5 bolts, lock nuts, washers | McMaster assorted | 1 set | $2.00 | $2.00 |
| | | | | **Linkage total:** | **$94.20** |

**D. Prototype Total**

| Category | Cost |
|---|---|
| Electronics (A) | $43.95 |
| Motor & gearbox (B) | $115.87 |
| Rack & pinion linkage (C) | $94.20 |
| **Prototype total** | **$254.02** |

> The motor, gearbox, shaft adaptors, cable, and all linkage hardware are the same parts spec'd for production. After testing, they go straight onto the first real door — only the RPi5 + Tic T249 get replaced by the Pulsar PCB.

### 14.2 Wiring

The Tic T249 has screw terminals for everything — no GPIO wiring or soldering required.

```
RPi5                          Tic T249                      Motor + PSU
──────────                   ─────────────                  ──────────────
USB-A port ════ USB cable ════ Micro-USB                    
                               │                            
                               ├── VIN (+)  ◄──── EPS-120-24 (+24V)
                               ├── VIN (−)  ◄──── EPS-120-24 (GND)
                               │                            
                               ├── A1  ◄──────── Motor Coil A wire 1
                               ├── A2  ◄──────── Motor Coil A wire 2
                               ├── B1  ◄──────── Motor Coil B wire 1
                               └── B2  ◄──────── Motor Coil B wire 2

Cable braid ──── PSU earth / frame ground (float at motor end)
```

That's it — 6 screw terminal connections + 1 USB cable. The Tic T249 has its own bulk capacitors and voltage regulator on board.

### 14.3 Test Software — Python on RPi5

The Pololu Tic has both a GUI app and command-line tools. Install on the RPi5:

```bash
# Install Tic software (includes ticcmd CLI and Tic Control Center GUI)
# Download from https://www.pololu.com/docs/0J71/3.2 — ARM64 .deb available
sudo dpkg -i pololu-tic-*.deb

# Python library
pip install pytic
```

**Phase 1 — GUI setup and basic motion:**
1. Open **Tic Control Center** on RPi5 (works over VNC or direct display)
2. Set current limit to 1.5A (conservative start), step mode to 1/8 microstepping
3. Use the GUI slider to command positions — verify motor + gearbox spins
4. Test both directions, confirm spring hold at close position
5. Increase current to 2.0A, repeat

**Phase 2 — StallGuard tuning:**
1. In Tic Control Center, enable StallGuard under the "Advanced" tab
2. Set SGTHRS (stall threshold) to 100 (mid-range)
3. Command slow motion (200 steps/sec) while watching the live SG_RESULT value
4. Run the door into the close stop — observe when StallGuard triggers
5. Adjust SGTHRS up/down until stall triggers reliably at the mechanical stops
6. Test both directions (close stop and open stop)

**Phase 3 — Scripted validation (Python):**

```python
import pytic
import time

tic = pytic.PyTic()
tic.open()                          # Connect via USB
tic.set_current_limit(2100)         # 2.1A max
tic.set_step_mode(pytic.STEP_MODE_8) # 1/8 microstepping
tic.energize()
tic.exit_safe_start()

# Drive to close stop (stall detection finds it)
tic.set_target_velocity(-2000000)   # Negative = close direction
# Monitor StallGuard — Tic firmware handles stall-stop automatically

time.sleep(30)                      # Wait for close
pos_close = tic.get_current_position()

# Drive to open stop
tic.set_target_velocity(2000000)
time.sleep(60)                      # Wait for open
pos_open = tic.get_current_position()

n_total = abs(pos_open - pos_close)
print(f"Total range: {n_total} steps")

# Go to 50% position
tic.set_target_position(pos_close + n_total // 2)
time.sleep(15)
print(f"At 50%: position {tic.get_current_position()}")

tic.deenergize()                    # Release motor — test self-locking
```

### 14.4 Success Criteria

| Test | Pass condition |
|---|---|
| Motor torque vs spring | Door closes fully against spring with no stall at IRUN = 80% |
| Stall detection | StallGuard reliably detects end stops within ±5 steps |
| Position accuracy | Door returns to same position (±2 steps) after 100 open/close cycles |
| Cable length | No missed steps or false stalls at 35-ft 16/4 cable |
| Self-locking | Door holds position with motor de-energized (IHOLD = 0) and spring pushing |
| Supply current | Measured supply draw matches chopper analysis within 20% |
| Rack mesh | No gear skip, no binding through full range of travel |
