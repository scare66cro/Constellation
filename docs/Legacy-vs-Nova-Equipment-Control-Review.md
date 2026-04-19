# AS2 Mini IO (Legacy) vs Nova Firmware — Equipment Control Review

> Generated: April 18, 2026
> Purpose: Line-by-line review of how Nova replicates legacy equipment control.
> Status: **DRAFT — needs walkthrough to confirm/correct each finding**

---

## Architecture Overview

Both firmwares share the **same application-level source files** — Controls.c, States.c, Modes.c, SerialShift.c, etc. are compiled directly from the legacy `Mini_IO legacy refference only firmware/Application/` directory via the Nova Makefile.

The differences are entirely in the **hardware abstraction layer (HAL)**:

| Layer | Legacy (TM4C129E) | Nova (AM2434 R5F) |
|---|---|---|
| Digital outputs | Shift registers driven by ISR (`SerialShift.c` + `Timer.c`) | `IoBoard[].OutputState` → Modbus TCP coils on Orbit boards (`hal_orbit.c`) |
| PWM outputs | TM4C hardware PWM timers (`PWM.c`) | AM2434 EPWM (`hal_pwm.c`) + Orbit Analog Outputs via Modbus |
| Inputs (switches) | 18 DIP switches via shift register | 10 Orbit discrete inputs + E-Stop, with synthesized switch states |
| Sensors | Direct ADC / RS485 | Orbit Modbus TCP Holding Registers 200+ (RS485 passthrough) |
| UI comms | Proprietary ASCII over RS485 / Lantronix | COBS + Protobuf over UART1 ("Nova Bridge") |

---

## Finding 1: Pulsed Door Actuator Timing

### Legacy Behavior
- A dedicated ISR (`SerialShiftTimerISR` in `Timer.c`) fires every 250 ms (30,000,000 counts at 120 MHz).
- A `PulseCount` increments each ISR tick; at count 4 (every 1.0 second), the pulse logic executes.
- The ISR bit-bangs `SS_MAIN_PD_UP` (bit 1) and `SS_MAIN_PD_DN` (bit 0) on the shift register.
- Actuator position is tracked via `PulseDoorPosition` and `PulseDoorMove` counters inside the ISR.
- A `PulseDoorFlag` semaphore prevents race conditions between the main loop and the ISR.
- `EQ_PULSEDOOR_POWER` (`SS_MAIN_PD_PWR`, bit 2) must be set to enable actuator power before driving.
- Travel time is configured via `Settings.Door.ActuatorTime`.

### Nova Behavior
- The `SerialShiftTimerISR` is **removed entirely** — there is no equivalent ISR in Nova.
- Pulsed door outputs (`PD_UP`, `PD_DN`) are mapped to Orbit coils 8 and 9 on the MAIN board.
- Physical actuation depends on the next `orbit_poll_io` Modbus TCP write cycle.
- Alternatively, the Orbit can be configured as a **GDC (Gellert Door Controller)**, where door commands are written to Holding Register 300+ and the Orbit handles actuator timing internally (HR 307 = travel time).
- `EQ_PULSEDOOR_POWER` is **not mapped** — Orbit manages power enable internally.

### Questions to Resolve
- [ ] Is the Orbit GDC mode fully implemented and tested, or are pulsed doors still driven by coil toggling?
- [ ] If coil-toggling mode: what is the `orbit_poll_io` interval, and is 1-second pulse resolution achievable?
- [ ] Is the removal of `PulseDoorFlag` semaphore safe given the new polling architecture?
- [ ] Does the legacy `PulseDoorPosition` tracking logic still function correctly without the ISR feeding it?

---

## Finding 2: PWM Output Implementation

### Legacy Behavior
- 4 PWM channels: `PWM_DOORS`, `PWM_REFRIGERATION`, `PWM_FAN`, `PWM_BURNER`.
- Driven by TM4C hardware timers at 10 kHz (120 MHz / 32 prescaler = 3.75 MHz clock, period = 375 counts).
- Value range: 55 (`PWM_MIN_VALUE`) to 277 (`PWM_MAX_VALUE`).
- Output is **inverted** (`375 - Output`) because opto-isolators on the AS2 board invert the signal.

### Nova Behavior
- `hal_pwm.c` configures AM2434 EPWM units to match the legacy 10 kHz / 375-count period.
- Uses 64-bit tick scaling to convert legacy tick values to the AM2434's 100 MHz TBCLK.
- Replicates the opto-isolator inversion for local EPWM channels.
- **Additionally**, PWM values can be mirrored to Orbit Analog Outputs (0–10V, Holding Registers 0–1) via Modbus FC06, scaled to 0–10,000 mV.
- Door PWM is converted to 0–1000 (0.1% resolution) and written to the Orbit GDC register.

### Questions to Resolve
- [ ] Are both local EPWM and Orbit AO active simultaneously for the same channel (Fan, Burner)? If so, which one drives the physical load?
- [ ] Does `PWM_REFRIGERATION` have a meaningful role in Nova, or is refrigeration purely relay-staged?
- [ ] Is the opto-isolator inversion still correct for the Nova hardware, or does the AM2434 board have different signal conditioning?

---

## Finding 3: Digital Output Propagation Delay

### Legacy Behavior
- `OutputOn()` / `OutputOff()` writes a bitmask to `IoBoard[].OutputState`.
- The `SerialShiftTimerISR` shifts bits out to physical registers every 250 ms (worst case).
- Effective output latency: 0–250 ms.

### Nova Behavior
- `OutputOn()` / `OutputOff()` writes the same bitmask (shared source code).
- Physical actuation waits for the next `orbit_poll_io()` call, which performs a Modbus TCP transaction.
- Effective output latency: depends on poll interval + Modbus round-trip time.

### Questions to Resolve
- [ ] What is the `orbit_poll_io` polling interval? (FreeRTOS task period)
- [ ] What is the typical Modbus TCP round-trip to an Orbit board on the local network?
- [ ] Is the total latency acceptable for fast-cycling equipment (humidifier duty cycles, climacell)?
- [ ] Could duty cycle resolution degrade if the poll interval is >250 ms?

---

## Finding 4: Switch / Input Handling

### Legacy Behavior
- 18 physical DIP switches read via the shift register:
  - `SW_START_STOP`, `SW_FAN_AUTO`, `SW_FAN_MANUAL`, `SW_FRESHAIR_AUTO`, `SW_FRESHAIR_MANUAL`
  - `SW_CLIMACELL_AUTO`, `SW_CLIMACELL_MANUAL`, `SW_HUMID_AUTO`, `SW_HUMID_MANUAL`
  - `SW_REFRIG_AUTO`, `SW_CURE_AUTO`, `SW_BURNER_AUTO`
  - `SW_AUX1_AUTO`, `SW_AUX1_MANUAL`, `SW_AUX2_AUTO`, `SW_AUX2_MANUAL`
  - Plus service/diagnostic switches.
- These directly control auto/manual modes for each subsystem.

### Nova Behavior
- Orbit boards provide only **10 discrete inputs (DI1–DI10)** plus a dedicated **E-Stop** (DI bit 10).
- `nova_thread_overrides.c` **synthesizes** missing switch states — injects a fake "CPLD Version 2" bit to force the legacy code into the correct bit-layout branch.
- E-Stop maps to `SW_START_STOP` off → forces `ST_SHUTDOWN`.

### Questions to Resolve
- [ ] Which of the 18 legacy switches are mapped to which of the 10 Orbit DIs?
- [ ] Which switches are synthesized (always on / always off / UI-controlled)?
- [ ] Are manual override modes (fan manual, fresh air manual, humid manual, etc.) accessible via the constellation UI?
- [ ] Is there a risk that hardcoded synthesized switch states prevent certain operating modes?

---

## Finding 5: E-Stop Handling (New in Nova)

### Legacy Behavior
- No dedicated E-Stop input.
- `SW_START_STOP` is a standard toggle switch — turning it off transitions to shutdown.

### Nova Behavior
- Orbit DI bit 10 is a dedicated E-Stop input.
- When active, `hal_orbit.c` forces `SW_START_STOP` off, triggering `ST_SHUTDOWN` in the state machine.
- This is additive — the legacy shutdown path is preserved.

### Questions to Resolve
- [ ] Does the E-Stop trigger an immediate output kill (all coils off), or does it rely on the state machine's shutdown sequence?
- [ ] Is there a latching requirement (E-Stop must be manually reset before restart)?
- [ ] Is the E-Stop state visible in the constellation UI?

---

## Finding 6: External Watchdog

### Legacy Behavior
- A hardware watchdog is toggled via GPIO at approximately 50 Hz in `Timer.c` (every ~20 ms).
- Uses a counter of 150,000 cycles at 120 MHz.

### Nova Behavior
- `hal_timer.c` replaces `Timer.c` with AM2434 PMU-based monotonic clock (`hal_timer_get_ms`).
- Watchdog kick mechanism differs — uses the AM2434's own watchdog peripheral or an external pin toggle.

### Questions to Resolve
- [ ] What is the Nova watchdog kick frequency?
- [ ] Does the AM2434 board have an external watchdog IC with a specific timeout window?
- [ ] If the watchdog timeout window is tight (e.g., 50 ms), does the Modbus polling load risk starving the kick task?

---

## Finding 7: Sensor Data Path

### Legacy Behavior
- Sensors read via direct ADC inputs or RS485 bus connected to the TM4C's UART.
- Processed in the main application loop.

### Nova Behavior
- Sensors are read from Orbit boards via Modbus TCP Holding Registers 200–263.
- The Orbit board performs the actual RS485 sensor communication and presents data as Modbus registers.
- Nova reads these in **16-register chunks** (to avoid QEMU chardev timeouts during simulation).

### Questions to Resolve
- [ ] Is the 16-register chunking a simulation workaround that should be removed for production?
- [ ] What is the sensor update rate on the Orbit side (how often does Orbit poll RS485)?
- [ ] Are all legacy sensor channels covered by the HR 200–263 range?

---

## Finding 8: Equipment Enum Coverage

### Legacy Equipment (EQUIPMENT_IO enum)
All of the following are defined in `SerialShift.h`:

| Category | Equipment IDs |
|---|---|
| Air | `EQ_FAN`, `EQ_DOORS`, `EQ_AIR_FLOW` |
| Refrigeration | `EQ_REFRIGERATION`, `EQ_REFRIG_STAGE1–8`, `EQ_REFRIG_DEFROST1–2`, `EQ_REFRIG_STANDBY` |
| Climate | `EQ_HEAT`, `EQ_CAVITY_HEAT`, `EQ_BURNER`, `EQ_CLIMACELL` |
| Humidity | `EQ_HUMID_HEAD1–3`, `EQ_HUMID_PUMP1–3` |
| Lights | `EQ_LIGHTS1–2`, `EQ_REDLIGHT`, `EQ_YELLOWLIGHT` |
| Aux | `EQ_AUX1–8` |
| Pulsed door | `EQ_PULSEDOOR_POWER`, `EQ_PULSEDOOR_OPEN`, `EQ_PULSEDOOR_CLOSE` |
| System | `EQ_POWER`, `EQ_REMOTE_STANDBY`, `EQ_LOW_TEMP` |

### Nova Coverage
Since Nova compiles the same `SerialShift.h` and `SerialShift.c`, all enum values exist in the code. The question is whether they all have a **physical output path** through the Orbit mapping.

### Questions to Resolve
- [ ] How many Orbit boards (and how many DOs per board) are assumed in a standard Nova installation?
- [ ] With 10 DOs per Orbit × N boards, is there enough capacity for all equipment?
- [ ] Are `EQ_REDLIGHT` and `EQ_YELLOWLIGHT` mapped to physical outputs or handled differently (e.g., UI-only)?
- [ ] Is `EQ_PULSEDOOR_POWER` intentionally unmapped, or is it an oversight?

---

## Finding 9: Duty Cycle Resolution

### Legacy Behavior
- `DutyCycle()` in `Controls.c` cycles relays on/off over a configurable period.
- Humidifiers use a period of `100 * T_MINS` (100 minutes).
- Aux outputs use `Settings.AuxProgram[i].Period`.
- Resolution depends on the main loop execution rate and the 250 ms shift-register update.

### Nova Behavior
- Same `DutyCycle()` function (shared source), but relay toggling goes through Modbus TCP.
- If the Modbus polling interval is slower than the legacy shift-register update, effective duty cycle resolution decreases.

### Questions to Resolve
- [ ] For a 100-minute humidifier period with 50% duty: legacy toggles at ~50 min marks with <250 ms jitter. What jitter does Nova add?
- [ ] For short-period aux duty cycles, is Modbus latency significant?

---

## Finding 10: Anti-Short-Cycle and Safety Timers

### Legacy Behavior
- `StartShortCycleTimer` in `States.c`: prevents compressor re-start for `Settings.Door.CoolAirCycle * 60` seconds.
- `RefrigRunTimer`: tracks compressor runtime for maintenance and defrost triggers.
- Defrost duration: `Settings.Refrig.DefrostDuration * 60` seconds.
- Defrost interval: `Settings.Refrig.DefrostPeriod * 3600` seconds.
- All timers use `uptime_sec` from `IntervalTimer` array.

### Nova Behavior
- Same timer logic (shared source).
- `uptime_sec` is provided by `hal_timer.c` using AM2434 PMU.

### Questions to Resolve
- [ ] Is `uptime_sec` in Nova monotonic and consistent with the legacy implementation?
- [ ] Any risk of timer drift between the two implementations over long runtimes (days/weeks)?

---

## Finding 11: PID Controller

### Legacy Behavior
- Standard P+I+D controller in `Controls.c`.
- Integral scaling factor: 0.022.
- Anti-windup: `WindupLimit = Range / (Scalar * Ki)`.
- Used for doors, refrigeration, and humidification.

### Nova Behavior
- Identical (shared source).

### Questions to Resolve
- [ ] No expected discrepancies, but confirm the PID sample rate is the same (depends on main loop timing).

---

## Finding 12: Settings Persistence

### Legacy Behavior
- `Settings` structure serialized to SD Card or Flash.
- Read on boot, written on change.

### Nova Behavior
- Unknown — needs investigation.

### Questions to Resolve
- [ ] Where does Nova store its settings? (Flash, SD, network, protobuf config?)
- [ ] Is the settings structure binary-compatible with legacy?
- [ ] Can legacy settings files be imported into Nova?

---

## Summary: Priority Review Items

| # | Finding | Risk | Priority |
|---|---|---|---|
| 1 | Pulsed door timing (ISR removed) | Position accuracy | **High** |
| 2 | Dual PWM paths (local + Orbit) | Conflicting outputs | **High** |
| 3 | Output propagation delay | Duty cycle resolution | **Medium** |
| 4 | Reduced physical switches (18 → 10+E-Stop) | Missing manual overrides | **Medium** |
| 5 | E-Stop behavior | Safety compliance | **High** |
| 6 | Watchdog kick frequency | System reliability | **Medium** |
| 7 | Sensor chunked reads | Production performance | **Low** |
| 8 | Equipment enum physical coverage | Unmapped outputs | **Medium** |
| 9 | Duty cycle resolution | Control accuracy | **Low** |
| 10 | Timer monotonicity | Long-term drift | **Low** |
| 11 | PID sample rate | Control accuracy | **Low** |
| 12 | Settings persistence | Data migration | **Medium** |
