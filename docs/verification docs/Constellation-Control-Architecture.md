# Agristar Constellation — Control Architecture

**Date:** March 26, 2026
**Status:** Design Phase — Architecture Committed
**Reference:** Current AS2 firmware (`Mini_IO/Application/`) — States.c, Controls.c, Modes.c, Failures.c, Analog_Input.c

---

## 1. Executive Summary

This document defines how the Constellation platform distributes control responsibility between the **Nova** (AM2434 central controller) and **Orbit** (STM32G474 I/O modules). It is derived from a complete analysis of the current AS2 monolithic firmware and maps every control function to its new owner in the distributed architecture.

**Key architectural principles:**

1. **All Orbits are dumb** — every Orbit is a pure I/O executor. It reads sensors, drives outputs, and runs local safety rules. It does not run state machines, PID loops, or control logic. Nova makes every control decision.
2. **Nova is the brain** — it runs ALL control logic centrally: storage state machines, refrigeration PID/staging/defrost, door PID, fan speed, burner control, aux programs — everything. Nova has full visibility into all zones simultaneously.
3. **Local safety rules protect the crop** — each Orbit runs a small set of safety rules (conditional and latched) that can only turn things OFF, never ON. These execute at 200 ms regardless of Nova status.
4. **Nova is the vault** — all site settings live on Nova's QSPI flash with ping-pong CRC32 protection. Orbits store only ~224 bytes of survival config. Triple backup: Nova QSPI + RPi5 filesystem + Azure cloud.
5. **Every Orbit runs the same firmware binary** — role is irrelevant since all Orbits are I/O executors. DIP switch sets the address only.
6. **Orbits are disposable, field-replaceable endpoints** — plug in, set DIP, walk away. Nova pushes the survival config automatically.

**Design validation summary:**

| Concern | Result |
|---|---|
| Nova CPU (20 Orbits) | 0.4% of one R5F core at 800 MHz |
| Nova comms (20 Orbits) | 240 ms of 1,000 ms budget (24%) |
| Nova memory | 438 KB used / 2,560 KB available (17%) |
| Orbit cache on Nova | 20 Orbits + 18 VFDs = ~20 KB |
| RPi5 response time | 0.6 µs cache lookup. UART serialization (35 ms) is the bottleneck. |
| VFD bus (8 drives @ 19200) | 600 ms/cycle = 60% of 1 Hz budget |
| Ethernet traffic (20 Orbits) | ~12 KB/s = 0.1% of 100 Mbps |

---

## 2. Current AS2 Firmware — What It Does

The current TM4C129 firmware runs everything in one monolithic 1 Hz control loop. Here is every control function, grouped by subsystem:

### 2.1 State Machine (States.c — 1,257 lines)

The heart of the system. `SetSystemState()` evaluates sensor data, switch positions, run clock schedules, and alarm status to determine the current operating state. Each state maps to a mode function that controls equipment.

| State | Constant | Trigger | Mode Function |
|---|---|---|---|
| **Cooling** | `ST_COOLING` | Outside temp < start temp, doors switch in auto, run clock = cooling/recirc/refrig | `ModeCooling()` |
| **Cooling + Dehumid** | `ST_COOLDEHUMID` | Cooling + return humidity > setpoint, burner available (onion) | `ModeCooling(DEHUMID_MODE)` |
| **Refrigeration** | `ST_REFRIG` | Outside temp > start temp, or run clock = refrig only, refrig switch in auto | `ModeRefrig(REFRIG_MODE)` |
| **Refrig + Dehumid** | `ST_REFRIGDEHUMID` | Refrig + return humidity > setpoint, refrig output < 90% (onion) | `ModeRefrig(DEHUMID_MODE)` |
| **Defrost** | `ST_DEFROST` | Refrig runtime exceeds defrost period (configurable hours) | `ModeRefrig(DEFROST_MODE)` |
| **Heating** | `ST_HEATING` | Outside temp < start temp AND plenum temp < setpoint - heat threshold (potato only) | `ModeHeating()` |
| **Recirculation** | `ST_RECIRC` | Outside temp sensor alarm, plenum sensor alarm, or no valid cooling/refrig condition | `ModeRecirc()` |
| **Standby** | `ST_STANDBY` | Run clock = standby | `ModeStandby()` |
| **Remote Standby** | `ST_REMOTE_STANDBY` | Remote standby input active | `ModeStandby()` |
| **Shutdown** | `ST_SHUTDOWN` | Start/stop switch = stop | `ModeShutdown()` |
| **Fan Manual** | `ST_FAN_MANUAL` | Fan switch in manual position | `ModeFanManual()` |
| **Fan Off** | `ST_FAN_OFF` | Fan switch off | `ModeFanOff()` |
| **Failure** | `ST_FAILURE` | Any hard failure (power, sensor, fan, airflow, etc.) | `ModeStandby()` |
| **Air Cure** | `ST_AIRCURE` | Onion: cure switch auto, outside temp > cure start temp, humidity < cure start humidity | `ModeAirCure()` |
| **Burner Cure** | `ST_BURNERCURE` | Onion: cure switch auto, conditions don't meet air cure thresholds, burner available | `ModeBurnerCure()` |

**State evaluation order** (simplified):

```
1. CheckStartSwitchStatus()     → start/stop switch off? → ST_SHUTDOWN
2. CheckPower()                  → power input missing? → ST_FAILURE
3. CheckFanSwitch()              → manual? → ST_FAN_MANUAL. Off? → ST_FAN_OFF
4. CheckRemoteOff()              → fan remote off? → ST_FAN_REMOTEOFF
5. CheckRunClock()               → standby period? → ST_STANDBY
6. CheckRemoteStandby()          → remote standby input? → ST_REMOTE_STANDBY
7. CheckPlenumSensor()           → no plenum temp? → ST_FAILURE
8. SetStartTemp()                → calc start temp (ref + diff ± wet bulb depression)
9. SystemFailuresChk()           → any hard failures? → ST_FAILURE
10. ── POTATO MODE ──
    SetStateCooling()            → outside temp < start temp? → ST_COOLING
    SetStateRefrig()             → outside temp > start temp? → ST_REFRIG
    SetStateDefrost()            → refrig runtime > defrost period? → ST_DEFROST
    SetStateHeating()            → plenum temp < setpoint - threshold? → ST_HEATING
    SetDehumidification()        → humidity > setpoint? → add DEHUMID variant
11. ── ONION MODE ──
    SetStateCure()               → cure switch auto?
      → outside temp OK + humidity OK → ST_AIRCURE
      → else, burner available → ST_BURNERCURE
      → else → ST_RECIRC
    SetStateCooling()            → (same as potato)
    SetStateRefrig()             → (same as potato)
    SetStateDefrost()            → (same as potato)
    SetDehumidification()        → (same as potato)
12. SetMode()                    → call the mode function for the current state
```

### 2.2 Start Temperature Calculation

The start temperature determines whether outside air can cool the storage (cooling mode) or refrigeration is needed. It is a critical decision point.

```
StartTemp = ReferenceTemp + TempDiff ± WetBulbDepression × ClimacellEfficiency

Where:
  ReferenceTemp = plenum setpoint, return air temp, or any sensor (configurable)
  TempDiff      = user-configured offset (above or below reference)
  WetBulbDepression = psychrometric calc from outside temp/humidity (potato + climacell only)
  ClimacellEfficiency = user-configured % (how effective the evaporative cooler is)
```

The wet bulb depression calculation (`WetBulbDepression()` in States.c) uses `pow()`, `log10()`, and `exp()` — the heaviest floating-point math in the firmware.

### 2.3 Temperature Ramp Program (States.c — `SetRamp()`)

Gradually adjusts the plenum temperature setpoint over time. Two modes:

| Mode | Behavior |
|---|---|
| **Manual** | Every N hours of cooling/refrig runtime, adjust setpoint by ±X degrees toward target |
| **Automatic** | Ramp down only. When reference temp - setpoint ≤ configured diff, drop setpoint by X degrees |

The ramp program is a slow process (hours/days) that modifies `Settings.Plenum.TempSet` in real time.

### 2.4 Run Clock (States.c — `RunClockMode()`)

A 24-hour schedule divided into 48 half-hour slots. Each slot is assigned a mode:

| Run Clock Value | Constant | Meaning |
|---|---|---|
| 0 | `RC_COOLING` | Allow cooling (outside air) |
| 1 | `RC_RECIRC` | Recirculation only |
| 2 | `RC_REFRIG` | Allow refrigeration |
| 3 | `RC_STANDBY` | System standby |
| 4 | `RC_CURE` | Onion: allow curing |

Onion cure mode has its own separate run clock schedule (`Settings.Cure.RunTimes[]`).

### 2.5 Equipment Control Functions (Controls.c — 1,835 lines)

Each piece of equipment has a dedicated control function:

#### 2.5.1 Fan Control — `CtrlFan(speed)`

- **Fixed speed:** Direct percentage (used in recirc, refrig, fan boost, cure)
- **Auto mode:** Periodic speed adjustment based on temperature differential
  - Potato: `Ref2 - Ref1 > TempDiff` → speed up. Else → slow down. Update period in hours.
  - Onion: Same logic but driven by humidity differential
  - Ref1 = plenum setpoint or plenum avg (configurable)
  - Ref2 = return air temp or any sensor (configurable)
  - Speed bounded by `Settings.Fan.MinSpeed` and `Settings.Fan.MaxSpeed`
- **Previous speed restore:** When re-entering cooling mode, restores the fan speed from last cooling period
- **Inhibited during:** Purge cycles, fan boost cycles

#### 2.5.2 Door Control — `CtrlDoors(Actual, Target)`

PID-controlled door position:

- PID input: `Actual - Target` (typically plenum temp - setpoint)
- PID output: PWM value 0–100% mapped to door position
- Update interval: configurable (`Settings.Door.PID.U`)
- Rate limiting: output change capped at `PWM_INC_VALUE` per update
- **Pulsed door mechanism:** PWM percentage is converted to a timed open/close pulse via `CtrlDoorsPulsed()`. A Timer ISR generates the actual pulse outputs (`EQ_PULSEDOOR_OPEN`, `EQ_PULSEDOOR_CLOSE`). Position is tracked by step counting (no position sensor).
- **Door initialization:** On startup or switch change, doors close fully (`ActuatorTime` seconds of close pulse) to establish known position.

#### 2.5.3 Refrigeration Control — `CtrlRefrig(ActualTemp, TargetTemp)`

PID-controlled refrigeration staging:

- PID input: `ActualTemp - TargetTemp` (plenum avg vs setpoint)
- PID output: percentage 0–100%
- Update interval: configurable (`Settings.Refrig.PID.U`)
- Rate limiting: same `PWM_INC_VALUE` per step
- **Multi-stage compressor:** Up to `NUM_REFRIG_STAGES` stages, each with configurable On/Off thresholds:
  - Stage ON when `RefrigPercent >= Stage[i].On`
  - Stage OFF when `RefrigPercent < Stage[i].Off`
  - Dead band between On/Off prevents short-cycling
- **Failure check:** If `AL_REFRIGERATION` alarm is set (not preliminary), calls `CtrlRefrigOff()`. If fail mode = 2, allows PID to continue running but inhibits stage outputs.
- **Diagnostic mode:** Individual stages can be forced on/off from the UI for troubleshooting. Auto-reverts after 60 minutes.

#### 2.5.4 Defrost — `SetStateDefrost()`

- Triggered when accumulated refrigeration runtime (`IT_REFRIGRUNTIME`) exceeds `Settings.Refrig.DefrostPeriod` hours
- Duration: `Settings.Refrig.DefrostDuration` minutes
- On entry: saves current PID state (output, error terms) so refrig resumes smoothly after defrost
- On exit: restores saved PID state, resets refrig runtime counter
- Has separate diagnostic mode per defrost stage (`Settings.Refrig.Defrost[i].Diagnostic`)

#### 2.5.5 Burner Control — `CtrlBurner(Output)` (Onion only)

PID-controlled burner for cure mode:

- PID input: `Settings.Burner.TempSet - PlenumTempAvg`
- PID output: percentage 0–100%
- Digital output (`EQ_BURNER`) turns on when PID output ≥ `Settings.Burner.On` threshold
- Digital output turns off when PID output drops to On - 3%
- Three modes: Economy (PID + dehumid fallback), Max (complex 6-state machine with door/burner balance), Manual
- **Max mode state machine** (cure substates):
  - `CS_BURNER` — modulate burner, PID control
  - `CS_DEHUMID` — warm + humid: burner at low, doors control temp
  - `CS_MOD_DOOR` — burner at threshold, doors modulate
  - `CS_MOD_BURNER` — doors fully open, reduce burner
  - `CS_MOD_BURNER_DOOR_LOCK` — doors closed, increase burner
  - `CS_MOD_BURNER_DOOR_UNLOCK` — burner dropping, unlock doors
  - `CS_HOLD_BURNER_MOD_DOOR` — burner holding, doors fine-tune temp

#### 2.5.6 Outside Air Blend — `CtrlOutsideAirBlend(CureMode)` (Onion only)

Controls door position during cure mode based on both temperature and humidity limits:

- Air cure: PID drives doors using lesser of temp diff or humidity diff (either can close doors)
- Burner cure: Calculates a percentage within the range [low limit, start temp] for temp, [start humid, high limit] for humidity, uses the lesser percentage
- Closes doors if either limit exceeded

#### 2.5.7 Humidifier Control — `CtrlHumidifier(EquipType, SysMode)`

Up to 3 humidifiers + heads, each independently controlled:

| Mode | Behavior |
|---|---|
| Manual | Always on when switch is in manual |
| Timer | Duty cycle at configurable on/off periods per system mode (cool, recirc, etc.) |
| Auto | PID control against humidity setpoint. #2 and #3 follow #1 in auto. |

- Heads run whenever pump is on (continuous heads prevent moisture damage)
- PID uses same update interval and gains as Climacell

#### 2.5.8 Climacell (Evaporative Cooler) — `CtrlClimacell(EquipType, SysMode)` (Potato only)

Similar to humidifier with additional conditions:

- Auto: Runs when outside temp > plenum setpoint (evaporative cooling is effective)
- Cool Only: Runs only during cooling mode
- Clock schedule: Own 48-slot schedule separate from main run clock
- PID control in "Auto" clock mode against humidity setpoint

#### 2.5.9 Cavity Heat — `CtrlCavityHeat()`

Prevents condensation in building cavities/cold spots:

- Mode 2 (On): Outside temp-based trigger with configurable duty cycle
- Mode 3 (Auto): Sensor-based trigger (any configurable sensor vs plenum setpoint + differential)
- 1.0°F hysteresis

#### 2.5.10 Heater — `CtrlHeat()` (Potato only)

Simple on/off:

- On when state = `ST_HEATING`, remote off = 0, no alarm
- Off otherwise

#### 2.5.11 Fan Boost — `CtrlFanBoost()`

Periodic high-speed fan cycle to prevent hot spots or condensation:

| Mode | Trigger |
|---|---|
| Temperature-based | Outside temp < configurable limit AND fan ≤ 80% AND interval elapsed |
| Runtime-based | Continuous fan time at low speed exceeds configurable hours |

- Duration: configurable minutes
- Speed: configurable percentage
- Saves/restores fan speed

#### 2.5.12 CO2 Purge — `CtrlPurge()`

Opens doors fully to ventilate CO2 buildup:

| Mode | Trigger |
|---|---|
| Manual | Cycle time interval reached |
| Automatic | CO2 sensor value exceeds setpoint, minimum 10-minute interval |

- Inhibited when: outside temp too hot/cold, plenum temp drifting ±5°F from setpoint, refrig output > threshold
- Duration: configurable minutes

#### 2.5.13 Auxiliary Outputs — `CtrlAux()`

Up to `NUM_AUX_OUTPUTS` programmable outputs, each with a multi-rule evaluation engine:

- Rules: Manual (always on), Output state, Input state, Switch state, Sensor comparison, System mode
- Operators: ==, >, < with 2% hysteresis
- Logic: AND/OR chaining of multiple rules
- Duty cycle: configurable on period as percentage of total period
- Reference values can be sensor readings, setpoints, output percentages, or the start temperature

#### 2.5.14 Bay Lights — `CtrlBayLights()`

Simple remote on/off per light output.

### 2.6 Sensor Processing (Analog_Input.c — 668 lines)

- **Analog boards** communicate on RS485 (9600 baud) using a custom packet protocol
- **Discovery** (`FindAnalogBoards()`): Scans all 32 addresses, queries firmware version, marks present boards
- **Reading** (`ReadAnalogBoards()`): Polls present boards in groups. Default boards (temp, humid) read every cycle. Others read in rotating groups.
- **Sensor conversion:**
  - Temperature: ADC → resistance via voltage divider formula → lookup table with linear interpolation (-40°C to 83°C). °C or °F based on settings.
  - Humidity: 4-20mA linear scale, 180–900 ADC range = 0–100% RH
  - CO2: 4-20mA linear scale, 180–895 ADC range = 0–10,000 ppm
  - IR Temperature: 4-20mA linear scale, manufacturer formula `y = 28.125x - 80.5`
- **Plenum average** (`CalculatePlenumTemp()`): Average of Plenum 1 and Plenum 2 sensors. Alarm if either fails. Falls back to single sensor.
- **Communication error handling:** Preliminary warning after first miss, alarm after ~3 minutes (55 consecutive misses). Marks sensor values as undefined after alarm.

### 2.7 Failure Detection (Failures.c — 1,129 lines)

Each equipment type has a failure checker that follows the same pattern:

1. Is the failure monitoring mode enabled (None/Alarm/Fail)?
2. Is the output commanded ON?
3. Is the feedback input indicating a problem?
4. Start a timer. If condition persists for `Settings.Failure[type].Timer` minutes → raise alarm
5. Alarm mode: warn the operator but continue running
6. Fail mode: latch the failure, stop the equipment, potentially halt the system

| Failure Check | Equipment | Feedback Input | Modes |
|---|---|---|---|
| `FanFailChk()` | Ventilation fan | Current/status feedback | Alarm, Fail (latched) |
| `RefrigFailChk()` | Compressor | Status feedback, 4-20mA monitor | Alarm, Fail |
| `RefrigStageChk()` | Individual compressor stages | Per-stage status | Alarm, Fail |
| `Refrig420Chk()` | Refrig 4-20mA monitor | Analog measurement | Alarm |
| `HeatFailChk()` | Electric heater | Status feedback | Alarm, Fail |
| `BurnerFailChk()` | Gas burner (onion) | Flame status | Alarm, Fail |
| `CavFailChk()` | Cavity heat | Status feedback | Alarm, Fail |
| `ClimacellFailChk()` | Climacell pump | Status feedback | Alarm, Fail |
| `HumidFailChk()` | Humidifier pumps | Status feedback | Alarm, Fail |
| `AuxFailChk()` | Aux outputs | Status feedback | Alarm, Fail |
| `CO2FailChk()` | CO2 level | CO2 sensor > high limit | Alarm, Fail |
| `PlenumTempFailLowChk()` | Low plenum temp | Plenum avg < setpoint - limit | Alarm, Fail |
| `PlenumTempFailHighChk()` | High plenum temp | Plenum avg > setpoint + limit | Alarm, Fail |
| `PlenumSensorFailChk()` | Plenum sensors | Both sensors invalid | Alarm |
| `OutsideTempSensorFailChk()` | Outside temp sensor | Sensor invalid | Alarm |
| `OutsideHumidSensorFailChk()` | Outside humidity sensor | Sensor invalid | Alarm |
| `OutsideHumidVarFailChk()` | Humidity variance | Outside humid changed > 10% in short period | Alarm |
| `PlenumHumidFailChk()` | Plenum humidity | Plenum humid > setpoint + limit | Alarm |
| `AirFlowFailChk()` | Airflow restriction | Hardware input (latched fail) | Fail (hard fault) |
| `AuxLowPlenumTempFailChk()` | Low temp safety | Hardware input (latched fail, requires site visit) | Fail (hard fault) |
| `BayLightsMonitor()` | Bay lights left on | Light feedback input | Alarm only |

**Hard faults** (`AirFlowFailChk`, `AuxLowPlenumTempFailChk`) are latched — they require cycling the start/stop switch to clear. This forces a site visit.

### 2.8 PID Controller (Controls.c — `PIDController()`)

Standard discrete PID used for all proportional control:

```
Error = CurError (calculated by calling function)
P = Kp × Error
I = Ki × IntError (accumulated, anti-windup at ±PID_WINDUP_LIMIT)
D = Kd × (CurError - PrevError)

Output = (P + I + D) / 100    (percent scaling)
Clamped to RangeMin..RangeMax
```

Five independent PID instances: Door, Refrigeration, Climacell, Humidifier, Burner. Each with configurable Kp, Ki, Kd, and update interval U.

### 2.9 Runtime Tracking

- Fan: daily, total, per-mode (cooling, refrig, recirc, cure, standby). Daily resets at noon.
- Refrigeration: accumulated runtime drives defrost scheduling
- Continuous fan time: drives runtime-based fan boost

---

## 3. Constellation Responsibility Map

### 3.1 Core Principle — Centralized Brain, Dumb Endpoints

Every Orbit is an identical I/O executor. Nova runs ALL control logic for ALL zones centrally on R5F-0.

**Why this works:**
- Typical site (2 storage + 4 refrig + 2 door Orbits): ~1.1 ms compute per 1,000 ms cycle = 0.11% of one R5F core
- Large site (20 Orbits): ~4 ms compute = 0.4% of core
- Communication for 20 Orbits: ~240 ms = 24% of 1 Hz budget
- Nova SRAM: 438 KB used of 2,560 KB available = 17%

**Why this is better than smart Orbits:**
- One firmware to maintain (Nova), one thin I/O binary (Orbit) — not two full control stacks
- No settings sync problem — Nova owns all settings, Orbits don't need them
- Cross-zone coordination is free (shared compressor racks, demand prioritization)
- Orbit replacement is instant — no settings to restore, no commissioning wizard
- All control algorithms in one debuggable codebase

### 3.2 Role Summary

| Function | Current AS2 | Constellation: Nova | Constellation: Orbit (any role) |
|---|---|---|---|
| State machine | TM4C | **Runs centrally per zone** | — |
| Run clock schedule | TM4C | **Evaluates centrally** | — |
| Start temp calc | TM4C | **Calculates centrally** | — |
| Wet bulb / psychrometrics | TM4C | **Calculates centrally** | — |
| Temperature ramp | TM4C | **Runs centrally** | — |
| Fan speed control | TM4C | **PID runs centrally** | Executes DO/AO commands |
| Door PID control | TM4C | **PID runs centrally** | Executes position commands |
| Humidifier control | TM4C | **Runs centrally** | Executes DO commands |
| Climacell control | TM4C | **Runs centrally** | Executes DO commands |
| Heater control | TM4C | **Runs centrally** | Executes DO commands |
| Cavity heat control | TM4C | **Runs centrally** | Executes DO commands |
| Fan boost | TM4C | **Runs centrally** | Executes DO commands |
| CO2 purge | TM4C | **Runs centrally** | Executes DO commands |
| Aux output programs | TM4C | **Runs centrally** | Executes DO commands |
| Burner control (onion) | TM4C | **PID runs centrally** | Executes DO/AO commands |
| Outside air blend (onion) | TM4C | **Runs centrally** | Executes DO commands |
| Refrig PID + staging | TM4C | **Runs centrally** | Executes DO commands |
| Defrost scheduling | TM4C | **Runs centrally** | Executes DO commands |
| Refrig diagnostics | TM4C | **Runs centrally** | Reports DI status |
| Failure detection (all) | TM4C | **Runs centrally** | Reports DI/sensor status |
| Sensor reading (analog bds) | TM4C | Reads cached values | **Polls on Port A RS485** |
| Settings storage | Flash/SD | **QSPI flash (single source of truth)** | ~224 bytes survival config only |
| Runtime tracking | TM4C | **Tracks centrally** | — |
| Alarm notifications | TM4C → UI | **Email, UI, Azure** | — |
| Data logging | TM4C → SD | **Logs to RPi5 filesystem** | — |
| UI data serving | TM4C → RPi5 | **Bridges to RPi5 UART** | — |
| Time sync | Internal RTC | **RTC source, syncs all Orbits** | Receives sync |
| Firmware updates | SD card | **Orchestrates OTA** | Receives via Modbus TCP |
| Local safety rules | — | Pushes rules at commissioning | **Executes at 200 ms** |
| VFD communication | — | Sends speed commands | **RTU master on Port B** |

### 3.3 Nova — What It Runs

The Nova (AM2434, 4× Cortex-R5F @ 800 MHz, 2.5 MB on-chip SRAM, no DDR) runs all control logic for every zone. There is no external RAM — the AM2434 is a pure MCU.

**R5F Core Assignment:**

| Core | Role | Tasks |
|---|---|---|
| R5F-0 | Control Engine | All zone state machines (1 Hz), all PID loops, all equipment control, failure detection, runtime tracking, zone coordination |
| R5F-1 | Communications + Data Bridge | Modbus TCP client (polls all Orbits), RPi5 UART bridge, Orbit data cache management |
| R5F-2 | Safety Watchdog | Independent watchdog — monitors R5F-0 and R5F-1, MPU-isolated, cannot be corrupted by firmware bugs |
| R5F-3 | Spare / Future | Available for diagnostics, OTA orchestration, or CAN FD expansion |

#### 3.3.1 Nova Zone Manager (R5F-0)

Nova runs the complete control logic for every zone. Each zone is a struct on R5F-0, processed sequentially in the 1 Hz control loop.

**Zone configuration (stored in QSPI flash):**

```c
typedef struct {
    uint8_t  zone_id;
    char     zone_name[32];             // "Building A", "Bay 3"
    uint8_t  zone_type;                 // POTATO, ONION
    uint8_t  storage_orbit_addr;        // DIP address of Storage Orbit
    uint8_t  refrig_orbit_addr[MAX_REFRIG_PER_ZONE];
    uint8_t  refrig_orbit_count;
    uint8_t  door_orbit_addr[MAX_DOOR_PER_ZONE];
    uint8_t  door_orbit_count;
} ZONE_CONFIG;
```

**Per-zone control state (in SRAM):**

```c
typedef struct {
    ZONE_CONFIG config;

    // State machine (ported from States.c)
    uint8_t  system_state;              // ST_COOLING, ST_REFRIG, etc.
    uint8_t  current_mode;              // UI mode display
    uint8_t  cure_state;                // Onion cure substate
    float    start_temp;                // Calculated start temperature
    float    plenum_temp_avg;           // From Storage Orbit sensors
    float    outside_temp;              // From Storage Orbit sensors
    float    return_temp;               // From Storage Orbit sensors
    float    plenum_humidity;           // From Storage Orbit sensors
    float    outside_humidity;          // From Storage Orbit sensors
    float    co2_level;                 // From Storage Orbit sensors

    // PID controllers (ported from Controls.c)
    PID_CTRL pid_door;
    PID_CTRL pid_refrig;
    PID_CTRL pid_climacell;
    PID_CTRL pid_humidifier;
    PID_CTRL pid_burner;

    // Equipment state
    uint8_t  fan_speed;                 // Current fan speed %
    uint8_t  door_position;             // Current door position %
    int      refrig_percent;            // Refrig PID output %
    uint8_t  stage_on[NUM_REFRIG_STAGES];
    uint8_t  burner_output;             // Burner PID output %

    // Defrost
    uint32_t refrig_runtime;            // Accumulated runtime → triggers defrost
    uint32_t defrost_timer;             // Countdown during defrost
    PWM_OUTPUT_STATE saved_pid;         // Saved for resume after defrost
    uint8_t  defrost_stage_on[NUM_DEFROST_STAGES];

    // Ramp program
    uint8_t  ramp_active;
    float    ramp_target;

    // Failure detection (ported from Failures.c)
    uint32_t alarm_bitmap;
    uint32_t warning_bitmap;
    uint32_t failure_timers[NUM_FAILURE_TYPES];

    // Runtime tracking
    uint32_t fan_runtime_daily;
    uint32_t fan_runtime_total;
    uint32_t fan_runtime_cooling;
    uint32_t fan_runtime_refrig;
    uint32_t fan_runtime_recirc;
    uint32_t fan_runtime_cure;
    uint32_t fan_runtime_continuous;
    uint32_t compressor_hours;

    // Settings (complete SYSTEM_SETTINGS — same struct as current firmware)
    SYSTEM_SETTINGS settings;           // ~1,500 bytes per zone
} ZONE_STATE;
```

**Nova control loop (R5F-0, 1 Hz):**

```c
void ControlTask(void *pvParameters) {
    while (1) {
        for (int z = 0; z < num_zones; z++) {
            ZONE_STATE *zone = &zones[z];

            // 1. Pull latest sensor data from Orbit cache (R5F-1 keeps this fresh)
            UpdateZoneSensors(zone);

            // 2. Run state machine (direct port of SetSystemState)
            SetSystemState(zone);

            // 3. Run mode function (direct port of SetMode)
            SetMode(zone);

            // 4. Run failure detection (direct port of Failures.c checkers)
            CheckFailures(zone);

            // 5. Run ramp program
            SetRamp(zone);

            // 6. Track runtimes
            UpdateRuntimes(zone);

            // 7. Queue output commands for R5F-1 to send to Orbits
            QueueOrbitCommands(zone);
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
```

**Control logic is a direct port of the current AS2 firmware** — the same `SetSystemState()`, `ModeCooling()`, `CtrlFan()`, `CtrlDoors()`, `CtrlRefrig()`, `PIDController()` functions. The only difference is the hardware abstraction layer: instead of writing to CPLD shift registers, they write to a command queue that R5F-1 sends to Orbits via Modbus TCP.

#### 3.3.2 Nova Orbit Cache + Data Bridge (R5F-1)

R5F-1 owns all external communication and maintains a real-time cache of every Orbit's register state.

**Orbit data cache (in SRAM):**

```c
typedef struct {
    uint8_t  addr;                      // DIP address
    uint8_t  online;                    // 1=responding, 0=offline
    uint32_t last_seen;                 // tick count of last successful poll
    uint16_t do_status[10];             // Digital output states
    uint16_t di_status[10];             // Digital input states
    uint16_t ao_values[2];              // Analog output values
    float    sensors[64];               // All sensor readings from analog boards
    uint16_t vfd_regs[MAX_VFD][18];     // VFD register cache (speed, current, faults, etc.)
    uint16_t firmware_version;
    uint32_t uptime;
    uint16_t watchdog_resets;
} ORBIT_CACHE;

ORBIT_CACHE orbit_cache[MAX_ORBITS];    // ~20 KB for 20 Orbits + 18 VFDs
```

**R5F-1 polling loop (1 Hz per Orbit):**

```
For each Orbit:
1. Read DI status, sensor values, VFD cache, system info
2. Update orbit_cache[] ← R5F-0 reads from here (memcpy, 0.6 µs)
3. Write DO commands, AO setpoints from R5F-0's command queue
4. Write time sync every 60 seconds
```

**RPi5 Data Bridge:**

R5F-1 also handles the UART link to RPi5:

```
Browser → lighttpd → gellertpostd → RPi5 UART → Nova R5F-1
                                                      │
                                                      ├── Parse serial protocol (same ^tag=value$CRC! format)
                                                      ├── Map to zone settings / commands
                                                      ├── R5F-0 processes on next control cycle
                                                      │
                                                      └── Reverse: zone state + cached Orbit data
                                                           → serial format → RPi5 → browser
```

The RPi5 sees Nova as if it were a single AS2 controller. The existing serial protocol (`^tag=value$CRC!`) is unchanged. The RPi5 doesn't know about Orbits. When the UI asks for "Building A sensors," R5F-1 looks up Zone 0's sensors in the orbit_cache and formats them into the serial response.

**UART link specs:**
- 230400 baud (upgradable to 921600 with one config change)
- Steady-state traffic: ~2.5 KB/s (11% of 23 KB/s capacity)
- Nova and RPi5 are ~2 inches apart on the DIN rail — UART is simpler, proven, and provides a true air gap between the control network and the RPi5's site LAN Ethernet

**Ethernet link specs:**
- Dedicated control switch (off-the-shelf Gigabit unmanaged, e.g., TP-Link TL-SG108, ~$25)
- Nova: CPSW Gigabit MAC with RGMII PHY
- Orbits: W5500 hardwired TCP/IP at 10/100 Mbps
- Switch auto-negotiates down for W5500s
- Total traffic for 20 Orbits: ~12 KB/s = 0.1% of 100 Mbps
- RPi5 is NOT on this switch — RPi5's Ethernet goes to site LAN/internet only

#### 3.3.3 Nova Cross-Zone Coordination

Nova is the only entity with visibility into all zones simultaneously:

| Function | Logic |
|---|---|
| **Refrig fault → Zone fallback** | If Refrig Orbit faults or goes offline, Nova's state machine for that zone transitions `ST_REFRIG → ST_COOLING` or `ST_RECIRC`. Automatic — no inter-Orbit communication needed. |
| **Multi-room demand priority** | When multiple zones compete for shared compressor capacity, Nova serves the zone furthest from setpoint first. |
| **Time sync** | Nova's DS3231MZ+ RTC is the time source. Every 60 seconds, Nova writes current time to all Orbits. Used for sensor timestamp correlation. |
| **Firmware version check** | On boot, Nova reads firmware version registers from all Orbits. Alerts if mismatched. |
| **Orbit health monitoring** | Nova tracks each Orbit's uptime, watchdog reset count, and response times. Anomalies trigger proactive alerts. |

### 3.4 Orbit — What It Runs (All Roles)

The Orbit (STM32G474CE, Cortex-M4F @ 170 MHz, 512 KB flash, 128 KB SRAM) is a pure I/O executor with local safety rules.

**FreeRTOS tasks on every Orbit:**

| Task | Priority | Period | Function |
|---|---|---|---|
| `SensorTask` | Medium | ~200 ms | Poll analog boards on Port A RS485. Execute local safety rules. Update sensor registers. |
| `ModbusTask` | Medium | Event-driven | Modbus TCP slave on W5500 Ethernet. Responds to Nova reads/writes. Commits DO/AO commands to shift registers. |
| `VFDTask` | Low | 1–3 Hz | Modbus RTU master on Port B RS485. Poll VFD drives, write CW+SpeedRef, cache status registers. |
| `WatchdogTask` | Highest | 100 ms | IWDG kick. If any task hangs, MCU resets. |

**What Nova writes → What Orbit does:**

| Nova writes | Orbit action |
|---|---|
| `REG_DO[0] = 1` | Turn on output 0 (ULN2803A sink) |
| `REG_DO[3] = 0` | Turn off output 3 |
| `REG_AO[0] = 0x0800` | Set analog output to 50% (MCP4822 DAC or G474 DAC) |
| `REG_VFD_CW[n] = 0x047F` | Write control word to VFD #n on Port B RS485 |
| `REG_VFD_REF[n] = 7500` | Write speed reference (75.00%) to VFD #n |
| `REG_SAFETY_RULES[...]` | Store local safety rules (written once at commissioning) |
| `REG_COMMS_LOSS_CFG[...]` | Store comms-loss config (written once at commissioning) |
| `REG_TIME_SYNC` | Update local time counter |

**What Orbit reads → What Nova receives:**

| Orbit register | Content |
|---|---|
| `REG_DI[0..9]` | Digital input states (optocoupler reads) |
| `REG_DO_STATUS[0..9]` | Actual output states (confirms shift register latched correctly) |
| `REG_SENSOR[0..63]` | All sensor values from analog boards (float32) |
| `REG_VFD_STATUS[n][0..17]` | VFD cached registers (speed, current, torque, voltage, faults, temp) |
| `REG_FW_VERSION` | Firmware version |
| `REG_UPTIME` | Seconds since boot |
| `REG_WDG_RESETS` | Watchdog reset count (lifetime) |
| `REG_SAFETY_STATE` | Bitmap of active safety force-offs |

**Port B RS485 — VFD Communication:**

Each Orbit can master up to 8 VFD drives on its Port B RS485 bus:

| Per-VFD Transaction | Registers | Wire Time @ 19200 | Purpose |
|---|---|---|---|
| Read process data (0–5) | 6 | ~15 ms | CW, Status, Speed |
| Read diagnostics (101–110) | 9 | ~15 ms | Speed, freq, torque, current, power, voltage, DC bus, temp |
| Read fault (401) | 1 | ~15 ms | Active fault code |
| Write CW + SpeedRef | 2 | ~15 ms | Control word + speed reference |
| **Total per VFD** | **18 regs** | **~75 ms** | **4–5 transactions** |

| VFDs per Orbit | Total wire time | % of 1 Hz budget | Notes |
|---|---|---|---|
| 2 | 150 ms | 15% | Typical storage/door Orbit |
| 6 | 450 ms | 45% | Typical refrig Orbit |
| 8 | 600 ms | 60% | Heavy refrig — still fits in 1 Hz |

For heavy installations (8 VFDs), diagnostic registers can be polled at 0.33 Hz (every 3 seconds) while CW+SpeedRef writes remain at 1 Hz — dropping bus utilization to ~35%.

VFD comms timeout (ACS380 parameter 30.01) should be set to 10 seconds, giving 3× margin over the 3-second worst-case poll interval.

### 3.5 Local Safety Rules

Each Orbit executes a small set of safety rules at 200 ms (SensorTask cycle), independent of Nova. These rules can only force outputs OFF — they can never turn anything ON. Nova is the only entity that can turn things ON.

**Safety rule struct (stored in Orbit flash, pushed by Nova at commissioning):**

```c
typedef enum {
    RULE_CONDITIONAL,  // Auto-clearing: releases when condition clears
    RULE_LATCHED       // Sticky: requires Nova to send REG_CLEAR_LATCH
} RULE_TYPE;

typedef struct {
    RULE_TYPE rule_type;
    uint8_t  sensor_index;      // Which local sensor to read
    uint8_t  compare;           // GT, LT, EQ
    float    threshold;         // Trip point
    uint8_t  do_index;          // Which output to force off
    uint8_t  do_action;         // ACTION_FORCE_OFF (only valid action)
    uint16_t alarm_code;        // Reported to Nova in safety state register
} LOCAL_SAFETY_RULE;
```

**Safety/Nova convergence model:**

```
DO_state = Nova_command AND (NOT safety_force_off)
```

Safety always wins. If a safety rule forces DO[0] OFF, Nova's command to DO[0] is blocked until the safety condition clears (conditional) or Nova sends an explicit `REG_CLEAR_LATCH` (latched).

**Two types of safety rules:**

| Type | Behavior | Example | Recovery |
|---|---|---|---|
| `RULE_CONDITIONAL` | Auto-clears when sensor returns to safe range | Pump-down: suction < 25 psi → force compressor OFF. When suction rises above 25 psi, force-off releases and Nova's command flows through. | Automatic |
| `RULE_LATCHED` | Stays active until Nova explicitly clears | Discharge pressure > 350 psi → force compressor OFF + set alarm. Stays latched even if pressure drops. | Requires tech intervention via UI → Nova → `REG_CLEAR_LATCH` |

**Example safety rules for a Refrig Orbit:**

| Rule | Type | Sensor | Compare | Threshold | Output | Alarm |
|---|---|---|---|---|---|---|
| High discharge pressure | LATCHED | Discharge temp sensor | GT | 350 psi | Compressor OFF | `AL_HIGH_DISCHARGE` |
| Low suction pressure | CONDITIONAL | Suction sensor | LT | 25 psi | Compressor OFF | `AL_LOW_SUCTION` |
| High discharge temp | LATCHED | Discharge temp | GT | 275°F | Compressor OFF | `AL_HIGH_DISCH_TEMP` |
| Low oil pressure | LATCHED | Oil pressure DI | EQ | 0 (switch open) | Compressor OFF | `AL_LOW_OIL` |

**Resource cost:** ~30 lines of firmware, ~128 bytes of config per Orbit, 8 rules max. Executes in <10 µs at 200 ms intervals.

### 3.6 Comms-Loss Behavior

Each output on an Orbit has a configurable comms-loss behavior, written by Nova at commissioning and stored in Orbit flash (~30 bytes total):

```c
typedef struct {
    uint8_t  hold_timeout_sec;    // Hold current state for N seconds after last Nova message
    uint8_t  safe_state;          // 0=OFF, 1=HOLD (for doors: hold position)
} COMMS_LOSS_OUTPUT;
```

**If Nova stops communicating:**

| Time | Orbit behavior |
|---|---|
| 0–5 seconds | Normal — continues executing last commands |
| 5 seconds | Comms-loss timer starts |
| 5s + hold_timeout | Each output transitions to its configured safe state |

**Typical defaults:**

| Output type | Hold timeout | Safe state |
|---|---|---|
| Compressors | 30 seconds | OFF |
| Defrost heaters | 0 seconds (immediate) | OFF |
| Condenser fans | 30 seconds | OFF |
| Ventilation fans | 60 seconds | OFF |
| Doors | 120 seconds | CLOSE (full close pulse) |
| Heaters | 0 seconds | OFF |
| Burners | 0 seconds | OFF |

**Safety rules continue running during comms-loss.** If a latched safety event fires while Nova is offline, the output stays forced OFF. When Nova reconnects, it reads `REG_SAFETY_STATE` and discovers the latch.

---

## 4. Communication Protocol — What Flows Where

### 4.1 Nova ↔ Orbit (Modbus TCP, 1 Hz polling)

All Orbits expose the same register map. Nova reads sensors and status, writes output commands.

**Nova reads from any Orbit (holding registers):**

| Register Block | Address | Type | Description |
|---|---|---|---|
| DO Status | 0x0200–0x0209 | uint16×10 | Each output: 0=off, 1=on (actual latched state) |
| DI Status | 0x0210–0x0219 | uint16×10 | Each input: 0=open, 1=closed |
| AO Values | 0x0220–0x0221 | uint16×2 | AO1, AO2 raw DAC values |
| Sensor Array | 0x0500–0x057F | float32×64 | All sensor values from analog boards |
| VFD Cache | 0x0A00–0x0AFF | uint16×N | VFD register data (speed, current, faults, temp per drive) |
| Safety State | 0x0B00 | uint32 | Bitmap of active safety force-offs |
| Safety Latch | 0x0B01 | uint32 | Bitmap of latched safety events (require clear) |
| Firmware Version | 0xF000 | uint16 | Firmware version |
| Uptime | 0xF001 | uint32 | Seconds since boot |
| Watchdog Resets | 0xF002 | uint16 | Lifetime watchdog reset count |

**Nova writes to any Orbit (holding registers):**

| Register Block | Address | Type | Description |
|---|---|---|---|
| DO Commands | 0x0200–0x0209 | uint16×10 | Turn outputs on/off (subject to safety override) |
| AO Setpoints | 0x0220–0x0221 | uint16×2 | Analog output values (4-20mA / 0-10V) |
| VFD CW + Ref | 0x0A80–0x0A8F | uint16×N | Control word + speed ref per VFD |
| Time Sync | 0x1600–0x1603 | uint32×2 | Unix timestamp + sub-second offset |
| Safety Rules | 0x2000–0x207F | binary | LOCAL_SAFETY_RULE array (written at commissioning) |
| Comms-Loss Config | 0x2080–0x209F | binary | Per-output hold/timeout/safe-state (written at commissioning) |
| Clear Latch | 0x2100 | uint32 | Bitmap of latched safety events to clear |
| Sensor Cal Offsets | 0x2200–0x223F | float32×32 | Per-sensor calibration offsets |

### 4.2 Nova ↔ RPi5 (UART, 230400 baud)

Same `^tag=value$CRC!` serial protocol used by current AS2. Nova R5F-1 emulates the current TM4C's serial behavior exactly — the RPi5 firmware (gellertpostd/gellertgetd) does not change.

```
RPi5 → Nova:  ^PLTEMPSET=43.5$A7!     (set plenum temp setpoint)
Nova → RPi5:  ^PLTEMP=44.2$B3!         (report plenum temp)
Nova → RPi5:  ^STATE=2$1F!             (report system state)
Nova → RPi5:  ^FANSPEED=65$C2!         (report fan speed)
```

When the UI requests data from a specific zone, Nova formats the response from its zone state and orbit cache. The RPi5 never talks to Orbits directly.

**Link budget:**
- Capacity: 23 KB/s at 230400 baud
- Steady state: ~2.5 KB/s (11%)
- Burst (full settings page load): ~8 KB/s (35%)
- Upgradable to 921600 baud (92 KB/s) with one config change on both sides

---

## 5. Settings Architecture

### 5.1 Single Source of Truth — Nova QSPI Flash

All site settings live on Nova. Orbits do not store control settings.

**Nova QSPI flash layout:**

| Section | Size | Content |
|---|---|---|
| Zone map | ~256 bytes | ZONE_CONFIG array — which Orbits serve which zones |
| Zone settings | ~1,500 × N zones | Complete SYSTEM_SETTINGS per zone (setpoints, PID, run clock, failure modes, equipment, aux) |
| Safety rules | ~128 × N Orbits | LOCAL_SAFETY_RULE arrays for each Orbit |
| Comms-loss configs | ~30 × N Orbits | Per-output hold/timeout/safe-state |
| VFD assignments | ~32 × N VFDs | Unit ID, Orbit mapping, comm parameters |
| Sensor calibration | ~64 × N Orbits | Per-board offsets |
| Firmware metadata | ~64 bytes | Nova + Orbit firmware versions, last update timestamp |

**Total for worst case (20 Orbits, 10 zones, 18 VFDs):** ~17 KB

**Ping-pong dual-copy protection:**

```
QSPI Sector A: [settings_blob] [CRC32]     ← active
QSPI Sector B: [settings_blob] [CRC32]     ← backup

Normal write:
1. Write new data to inactive sector (B)
2. Write CRC32 to B
3. Mark B as active, A as inactive
4. Next write goes to A (alternates)

Boot with sector A corrupted:
1. Read A → CRC fail
2. Read B → CRC pass → load from B
3. Immediately repair A by writing B's data to A
4. Both sectors now valid. Resume alternating.

Both sectors corrupted:
1. Load factory defaults
2. Write defaults to both A and B
3. Nova comes up "unconfigured"
4. RPi5 detects unconfigured state → offers restore from local backup
```

Power loss during write: only the sector being written is at risk. The other sector (the one you're currently running from) is untouched. Worst case: lose the most recent settings change. All previous settings are safe.

### 5.2 Orbit Local Storage (~224 bytes)

The Orbit stores only what it needs to survive independently:

| Data | Size | Purpose |
|---|---|---|
| DIP address | 1 byte | Network identity |
| Safety rules | ~128 bytes | Execute during comms loss |
| Comms-loss config | ~30 bytes | Per-output hold/timeout/safe-state |
| Sensor calibration offsets | ~64 bytes | Per-board ADC correction |
| CRC32 | 4 bytes | Integrity check |

Written once at commissioning (or when safety rules change — rare). One flash sector, ping-pong CRC32 protected.

**What the Orbit does NOT store:**
- Setpoints (Nova owns them)
- PID gains (Nova owns them)
- Run clock schedules (Nova owns them)
- Failure thresholds (Nova owns them)
- Equipment configuration (Nova owns them)
- ANY control settings (Nova owns ALL of them)

### 5.3 Triple Settings Backup

| Copy | Storage | Integrity | Recovery Source |
|---|---|---|---|
| **Nova QSPI** (primary) | Ping-pong sectors, CRC32 | On every boot + every write. Self-healing from backup sector. | Self-healing |
| **RPi5 filesystem** (local backup) | JSON file at `/var/lib/agristar/settings_backup.json` | Nova sends CRC with every settings push. RPi5 validates on write. | Restore to Nova via UART |
| **Azure cloud** (disaster recovery) | IoT Hub blob, SHA256, TLS transport | Synced on every settings change when connected | Download to RPi5 → push to Nova |

**When RPi5 backup is written:** Every time any setting changes on Nova, Nova sends the complete settings blob over UART. RPi5 writes it to disk. Cost: one UART transfer (~17 KB / 23 KB/s = ~0.7 seconds) per settings change.

**Recovery scenarios:**

| What dies | Recovery |
|---|---|
| Nova QSPI sector A | Self-heal from sector B. Zero downtime. |
| Both QSPI sectors | Load defaults. RPi5 offers one-button restore from local JSON. |
| Nova hardware failure | Swap Nova. Upload settings file from RPi5 or laptop. Full site restored. |
| Nova + RPi5 die | New hardware. Download from Azure cloud portal. Full site restored. |
| Everything at site destroyed | New hardware, cloud restore. Settings survive any physical disaster. |

### 5.4 Site Settings Export/Import

**Export (from UI):**

```
Nova QSPI → UART → RPi5 → lighttpd → Browser → "Download" → tech's laptop or USB stick
```

Exports a single JSON file containing the complete site configuration: zone map, all zone settings, all safety rules, all comms-loss configs, all VFD assignments, all sensor calibrations.

**Import (from UI):**

```
File from laptop → Browser "Upload" → lighttpd → RPi5 → UART → Nova → QSPI flash
                                                                  ↓
                                                    Nova pushes survival configs
                                                    to all Orbits (224 bytes each)
```

Nova validates on import: checks CRC, checks firmware version compatibility, checks that Orbit addresses in the file match what's physically on the network. Warns on mismatches but allows partial restore.

**Use cases:**
- Nova replacement — plug in new Nova, upload settings file, entire site comes back
- Clone a site config to a new facility with same layout
- Pre-season restore ("here's last year's settings")
- Disaster recovery with new hardware

### 5.5 Settings Changes (Normal Operation)

```
1. User changes setpoint on touchscreen (e.g., plenum temp 45°F → 43°F)
2. Browser POST → lighttpd → gellertpostd → RPi5 UART → Nova R5F-1
3. Nova updates zone[z].settings in SRAM
4. Nova writes to QSPI flash (ping-pong)
5. Nova sends settings blob to RPi5 for local backup
6. R5F-0 uses new setpoint on next 1 Hz control cycle (within 1 second)
7. RPi5 syncs to Azure cloud (async, when connected)
```

No Orbit writes needed. The Orbit never had the settings in the first place.

### 5.6 Orbit Replacement Workflow

```
1. Unplug dead Orbit
2. Plug in new blank Orbit (same hardware, same firmware .bin)
3. Set DIP switches to match old Orbit's address
4. Power on
5. Nova detects new Orbit within 1 second (responds to poll, no safety rules)
6. Nova pushes 224 bytes of survival config from QSPI
7. Orbit starts executing commands immediately
```

**No laptop. No settings screens. No phone call. No re-commissioning.**

The tech doesn't need to know what role the Orbit serves — the hardware is identical across all roles, and Nova knows what that DIP address is supposed to do.

---

## 6. Failure Scenarios

### 6.1 Nova Goes Offline (Watchdog Reset or Hardware Failure)

| Component | Behavior |
|---|---|
| All Orbits | Safety rules continue executing at 200 ms. Comms-loss timer starts at 5 seconds. Outputs transition to safe state per comms-loss config (compressors OFF in 30s, heaters/burners OFF immediately, doors close in 2 min, fans OFF in 60s). |
| Crop impact | Building is well-insulated — at 38°F with doors closed and fans off, temperature drifts very slowly. Hours of thermal mass before any concern. |
| Nova watchdog reset | Reboots in ~10 seconds. Reconnects to all Orbits. Reads zone state from orbit registers. Resumes control. Orbits may not even notice the gap (within hold timeout). |
| Nova hardware failure | Tech swaps Nova board. Uploads settings file from RPi5/laptop/cloud. All zones resume. |

### 6.2 Single Orbit Goes Offline

| Component | Behavior |
|---|---|
| Nova | Detects missing poll response within 3 seconds. Updates zone state accordingly. Sends alarm (email, Azure, touchscreen popup: "Room X — Orbit Offline"). |
| Affected zone | If it's a storage Orbit: that room has no sensors, no I/O — Nova can't control it. Fan stops, doors hold. Single room affected. If it's a refrig Orbit: Nova transitions that zone to `ST_COOLING` or `ST_RECIRC`. If it's a door Orbit: doors hold position. |
| Other zones | **Completely unaffected.** Each zone is an independent struct. |
| Recovery | Plug in replacement Orbit, set DIP, Nova pushes config. Zone resumes. |

### 6.3 Refrig Orbit Goes Offline (or Compressor Hard Fault)

| Component | Behavior |
|---|---|
| Nova | Sets zone's refrigeration to FAULT state. Transitions zone state machine: `ST_REFRIG → ST_COOLING` (if outside air cool enough) or `ST_RECIRC` (if not). Fans continue. Doors close. |
| Alarm | Email, Azure alert, touchscreen popup: "Refrig Zone X — Compressor Fault." |
| Other zones | Completely unaffected. |
| Crop | Stays safe. Outside air or recirculation maintains temperature. Service call for refrig repair. |

### 6.4 Ethernet Cable Disconnected (Single Orbit)

Same as Orbit offline from Nova's perspective. The disconnected Orbit continues running safety rules and comms-loss behavior. When cable is reconnected, Nova resumes polling immediately.

### 6.5 Analog Board Communication Failure

Handled by the Orbit's SensorTask (same retry/timeout logic as current firmware):
- Preliminary warning after first miss
- Alarm after ~3 minutes (55 consecutive misses at 200 ms)
- Sensor values marked `SENSOR_VAL_UNDEFINED` in registers
- Nova reads undefined values → zone state machine responds (plenum sensor fail → enters reduced mode, outside temp fail → enters recirculation)
- Nova sends alarm notifications

### 6.6 QSPI Flash Corruption

| Severity | Detection | Recovery |
|---|---|---|
| One sector bad | CRC fail on boot or write | Self-heal from other sector. Zero downtime. |
| Both sectors bad | Both CRC fail on boot | Load factory defaults. RPi5 offers one-button restore. |
| QSPI chip failure | Read/write errors | Nova alerts. Replace Nova board. Restore from RPi5/cloud. |

---

## 7. Firmware Structure

### 7.1 Orbit Firmware (~8,000 lines estimated)

One binary, all Orbits identical. No role-specific code — all roles execute the same I/O path.

```
Orbit Firmware Binary (~80 KB compiled)
├── Boot + Hardware Init
│   ├── Clock tree (170 MHz), GPIO, SPI, UART, IWDG
│   ├── W5500 Ethernet init (static IP from DIP switch: 192.168.0.{DIP+1})
│   ├── Load survival config from flash (safety rules, comms-loss, calibration)
│   └── DIP switch read → set Modbus TCP slave address
│
├── SensorTask (200 ms)
│   ├── Modbus RTU master on Port A: poll analog boards
│   ├── Sensor conversion (temp lookup table, humidity/CO2/IR linear scale)
│   ├── Update sensor registers
│   └── Execute LOCAL_SAFETY_RULE array → force-off outputs as needed
│
├── ModbusTask (event-driven)
│   ├── W5500 socket management (Modbus TCP slave)
│   ├── Register read handler (sensors, DI, DO status, VFD cache, safety state)
│   ├── Register write handler (DO commands, AO setpoints, VFD commands)
│   ├── Commit DO commands to 74HC595 shift registers via SPI
│   ├── Commit AO commands to MCP4822 DAC via SPI
│   └── Comms-loss timeout monitor → safe state transition
│
├── VFDTask (1–3 Hz, if drives on Port B)
│   ├── Modbus RTU master on Port B: poll VFD drives
│   ├── Write CW + SpeedRef from Nova command registers
│   ├── Read status registers (speed, current, faults, temp)
│   └── Update VFD cache registers
│
└── WatchdogTask (100 ms)
    └── IWDG kick, task health monitor
```

**What the Orbit firmware does NOT contain:**
- States.c (state machine) — Nova runs this
- Controls.c (equipment control) — Nova runs this
- Modes.c (mode orchestration) — Nova runs this
- Failures.c (failure detection) — Nova runs this
- Timer.c (interval timers for equipment) — Nova runs this
- Settings.c (1,300 lines of load/save/validate) — not needed, Orbit has no settings
- StorePostData.c, UI_Messages.c, DataExc.c — Nova runs this
- SystemLogs.c, UserLogs.c — Nova runs this

### 7.2 Nova Firmware (~25,000 lines estimated)

```
Nova Firmware (AM2434, R5F-0 + R5F-1 + R5F-2)

R5F-0: Control Engine
├── Zone Manager
│   ├── Zone configuration (ZONE_CONFIG array from QSPI)
│   ├── Per-zone state allocation (ZONE_STATE array in SRAM)
│   └── 1 Hz control loop iterating over all zones
│
├── Ported AS2 Control Logic (per zone)
│   ├── States.c — SetSystemState(), SetStartTemp(), RunClockMode(), SetRamp()
│   ├── Controls.c — CtrlFan(), CtrlDoors(), CtrlRefrig(), CtrlBurner(),
│   │                CtrlHumidifier(), CtrlClimacell(), CtrlCavityHeat(),
│   │                CtrlHeat(), CtrlFanBoost(), CtrlPurge(), CtrlAux(),
│   │                CtrlBayLights(), CtrlOutsideAirBlend()
│   ├── Modes.c — ModeCooling(), ModeRefrig(), ModeRecirc(), ModeStandby(),
│   │             ModeHeating(), ModeFanManual(), ModeShutdown(),
│   │             ModeAirCure(), ModeBurnerCure()
│   ├── Failures.c — All failure checkers (fan, refrig, heat, burner, humid,
│   │                climacell, cavity, aux, sensor, CO2, plenum temp)
│   └── PIDController() — 5 instances per zone
│
├── Defrost Scheduler (per zone)
│   └── Same logic as current SetStateDefrost() + CtrlRefrig(DEFROST_MODE)
│
├── Runtime Tracker (per zone)
│   └── Fan daily/total/per-mode, compressor hours, continuous fan time
│
└── Command Queue
    └── Output commands → R5F-1 sends to Orbits

R5F-1: Communications
├── Modbus TCP Client
│   ├── lwIP TCP/IP stack on CPSW Gigabit MAC
│   ├── Poll all Orbits at 1 Hz (read sensors/DI/DO/VFD, write commands)
│   └── Maintain ORBIT_CACHE[] array (memcpy-accessible by R5F-0)
│
├── RPi5 UART Bridge
│   ├── Serial protocol parser/formatter (^tag=value$CRC!)
│   ├── Map serial tags to zone state + orbit cache data
│   └── Same protocol as current DataExc.c + UI_Messages.c + StorePostData.c
│
├── Settings Manager
│   ├── QSPI flash read/write (ping-pong CRC32)
│   ├── Settings change → push to QSPI + RPi5 backup + Azure sync
│   └── Site export/import via RPi5 UART
│
└── Orbit Provisioning
    ├── Detect unconfigured Orbits (new/replaced)
    ├── Push safety rules + comms-loss config (224 bytes)
    └── Firmware update orchestration (OTA via Modbus TCP)

R5F-2: Safety Watchdog
├── Monitor R5F-0 heartbeat (independent timer, MPU-isolated)
├── Monitor R5F-1 heartbeat
└── If either core hangs → force MCU reset → all cores reboot
```

---

## 8. Migration from Current AS2

### 8.1 What Changes

| Component | Current AS2 | Constellation |
|---|---|---|
| Where control logic runs | TM4C (local, monolithic) | Nova R5F-0 (centralized, per-zone) |
| Hardware abstraction | `SerialShift.c` — CPLD bit-bang SPI | Nova writes Modbus TCP registers → Orbit commits to 74HC595 |
| Switch inputs | `CheckInputs(SW_*)` — CPLD register reads | Nova reads Orbit `REG_DI[]` registers |
| Equipment outputs | `OutputOn/Off()` — CPLD register writes | Nova writes Orbit `REG_DO[]` registers |
| Analog outputs | PWM via CPLD | Nova writes Orbit `REG_AO[]` → MCP4822 SPI DAC |
| Analog board comms | Custom RS485 packet protocol | Orbit runs Modbus RTU master on Port A (same electrical) |
| VFD communication | RPi5 → FMBT-21 Modbus TCP ($200/drive) | Orbit runs Modbus RTU on Port B (direct, $0.50/drive) |
| Settings storage | N25Q128 NOR flash + SD card on TM4C | Nova QSPI (triple-backed to RPi5 + Azure) |
| UI communication | Custom serial protocol (`DataExc.c`) on TM4C | Same serial protocol on Nova R5F-1 |
| Logging | Local SD card writes (`SystemLogs.c`) on TM4C | Nova reads sensors → formats → RPi5 filesystem |
| Refrigeration | Local `CtrlRefrig()` on TM4C | Nova centralized (same algorithm, per-zone struct) |
| Time source | Internal RTC (limited accuracy) on TM4C | Nova DS3231MZ+ RTC, synced to all Orbits |
| Failure point per room | One TM4C board (entire room dead) | One Orbit (room dead) — but Nova alerts instantly, replacement is 2 minutes |

### 8.2 What Does NOT Change

- **Control algorithms** — same PID, same state machine logic, same mode functions, same failure detection
- **Sensor conversion** — same temperature tables, humidity/CO2/IR formulas
- **Settings structure** — same SYSTEM_SETTINGS fields
- **Run clock** — same 48-slot schedule
- **Ramp program** — same manual/auto logic
- **Psychrometric calculations** — same `CalculatedHumidity()`, `WetBulbDepression()`
- **PID tuning** — same Kp/Ki/Kd values (same plant, same sensors, same actuators)
- **UI** — Svelte code untouched, RPi5 services untouched
- **Serial protocol** — same `^tag=value$CRC!` format between RPi5 and controller

### 8.3 Porting Effort Estimate

| Task | Effort | Notes |
|---|---|---|
| **Orbit firmware (complete)** | 6 weeks | W5500 Modbus TCP slave, SPI shift register driver, Modbus RTU master (Port A + Port B), SensorTask, safety rule engine, comms-loss handler, FreeRTOS setup. ~8,000 lines. |
| **Nova Zone Manager** | 3 weeks | Zone config, ZONE_STATE allocation, 1 Hz loop, zone-to-Orbit command routing. ~2,000 lines. |
| **Nova control logic port** | 3 weeks | Port States.c + Controls.c + Modes.c + Failures.c. Replace hardware calls with orbit_cache reads and command queue writes. ~200 lines changed across 4,700 lines of ported code. |
| **Nova Modbus TCP client** | 3 weeks | lwIP + Modbus TCP client on CPSW MAC. Orbit polling, cache management. ~2,000 lines. |
| **Nova RPi5 bridge** | 4 weeks | Port DataExc.c + UI_Messages.c + StorePostData.c. Map serial tags to zone state + orbit cache. ~3,000 lines. |
| **Nova settings manager** | 2 weeks | QSPI flash driver, ping-pong, site export/import, RPi5 backup sync. ~1,500 lines. |
| **Nova Orbit provisioning** | 1 week | Detect unconfigured Orbits, push safety rules + comms-loss config. ~500 lines. |
| **Integration testing** | 4 weeks | End-to-end with QEMU emulation, then bench hardware. |
| **Total** | **~26 weeks** | One firmware engineer, sequential. Parallel tracks possible (Orbit + Nova simultaneously). |

---

## 9. System Diagrams

### 9.1 Typical 2-Room Potato Storage

```
                           ┌─────────────────┐
                           │     Nova         │
                           │  AM2434 4×R5F    │
                           │  2.5 MB SRAM     │
                           │  QSPI Flash      │
                           │                  │
                           │  R5F-0: ALL      │
                           │   control logic  │
                           │  R5F-1: Comms    │
                           │  R5F-2: Watchdog │
                           │  UART → RPi5     │
                           └────────┬─────────┘
                                    │ Ethernet (Modbus TCP)
                                    │ Gigabit switch
                 ┌──────────────────┼──────────────────────────────┐
                 │                  │                  │            │
          ┌──────┴──────┐   ┌──────┴──────┐   ┌──────┴──────┐  ┌──┴──────────┐
          │  Orbit #1    │   │  Orbit #2    │   │  Orbit #3    │  │  Orbit #4    │
          │  (Room 1     │   │  (Room 2     │   │  (Room 1     │  │  (Room 2     │
          │   Storage)   │   │   Storage)   │   │   Refrig)    │  │   Refrig)    │
          │              │   │              │   │              │  │              │
          │ Port A:      │   │ Port A:      │   │ Port A:      │  │ Port A:      │
          │  Analog Bds  │   │  Analog Bds  │   │  Suct/Disch  │  │  Suct/Disch  │
          │              │   │              │   │              │  │              │
          │ DO: Fans,    │   │ DO: Fans,    │   │ DO: Comp     │  │ DO: Comp     │
          │  Heat, Humid │   │  Heat, Humid │   │  stages,     │  │  stages,     │
          │  Climacell,  │   │  Climacell,  │   │  defrost,    │  │  defrost,    │
          │  Cavity, Aux │   │  Cavity, Aux │   │  cond fans   │  │  cond fans   │
          │  Doors       │   │  Doors       │   │              │  │              │
          │              │   │              │   │ AO: EEV pos  │  │ AO: EEV pos  │
          │ Safety rules │   │ Safety rules │   │ Safety rules │  │ Safety rules │
          └──────────────┘   └──────────────┘   └──────────────┘  └──────────────┘

All Orbits: identical hardware, identical firmware, identical register map.
Nova knows the role via zone map (QSPI flash).
```

### 9.2 Onion Curing Bay (1 Orbit per bay)

```
          ┌──────────────┐
          │  Orbit        │
          │  (Bay 1)      │
          │               │
          │ Port A:       │
          │  Analog Bds   │  ← temp, humid, CO2 sensors
          │ Port B:       │
          │  8× ACS380    │  ← curing fans on RS485 daisy chain (19200 baud)
          │  VFDs         │     600 ms/cycle = 60% of 1 Hz budget
          │               │
          │ DO1: Doors    │  ← ganged 8 doors OPEN
          │      OPEN     │
          │ DO2: Doors    │  ← ganged 8 doors CLOSE
          │      CLOSE    │
          │ DO3: Burner   │  ← burner ignition enable
          │               │
          │ AO1: Burner   │  ← burner modulation 4-20mA
          │      mod      │
          │               │
          │ 7 spare DOs   │
          │ 1 spare AO    │
          │               │
          │ Safety rules: │
          │  High temp →  │
          │  burner OFF   │
          └───────────────┘

Nova runs the full cure state machine (CS_BURNER, CS_DEHUMID, etc.)
and writes DO/AO commands. Orbit executes and polls VFDs.
```

### 9.3 Large Site (2 buildings, 10 Orbits, 18 VFDs)

```
Nova ── Ethernet Switch ─┬─ Orbit #1  (Bldg A Storage)   Port B: 2 VFDs
                         ├─ Orbit #2  (Bldg B Storage)   Port B: 2 VFDs
                         ├─ Orbit #3  (Bldg A Refrig 1)  Port B: 4 VFDs
                         ├─ Orbit #4  (Bldg A Refrig 2)  Port B: 4 VFDs
                         ├─ Orbit #5  (Bldg B Refrig 1)  Port B: 2 VFDs
                         ├─ Orbit #6  (Bldg B Refrig 2)  Port B: 2 VFDs
                         ├─ Orbit #7  (Bldg A Doors)     Port B: stepper ctrl
                         ├─ Orbit #8  (Bldg B Doors)     Port B: stepper ctrl
                         ├─ Orbit #9  (Bldg A Refrig 3)  Port B: 2 VFDs
                         └─ Orbit #10 (Bldg B Refrig 3)  —

Nova Zone Map (QSPI flash):
  Zone 0 "Building A": Storage=#1, Refrig=#3,#4,#9, Door=#7
  Zone 1 "Building B": Storage=#2, Refrig=#5,#6,#10, Door=#8

Nova compute: 2 zones × 0.55 ms = 1.1 ms / 1,000 ms = 0.11%
Nova comms:   10 Orbits × 12 ms = 120 ms / 1,000 ms = 12%
Nova cache:   10 Orbits + 18 VFDs = ~12 KB
Nova memory:  ~440 KB / 2,560 KB = 17%
```

### 9.4 Network Topology

```
┌─────────────────────────────────────────────────┐
│  CONTROL NETWORK (air-gapped)                    │
│                                                   │
│  ┌────────┐     ┌──────────────┐                 │
│  │  Nova   │─────│  Gigabit     │                 │
│  │ AM2434  │ GbE │  Switch      │                 │
│  └────┬───┘     │  (unmanaged)  │                 │
│       │UART     │  $25          │                 │
│  ┌────┴───┐     │              ├── Orbit #1 (100M)│
│  │  RPi5  │     │              ├── Orbit #2 (100M)│
│  │        │     │              ├── Orbit #3 (100M)│
│  └────┬───┘     │              └── ...            │
│       │ Eth     └──────────────┘                 │
└───────┼──────────────────────────────────────────┘
        │
   Site LAN / Internet
   (RPi5 Ethernet only — for UI access and Azure cloud)
```

Nova ↔ RPi5: **UART only** (2 inches apart, proven in hundreds of shipped units).
RPi5 Ethernet: **Site LAN only** (for browser access and Azure IoT Hub).
Control switch: **Dedicated, air-gapped** — no path from internet to Orbit network.

### 9.5 Cost Comparison

```
Current: 8-room site = 8 × AS2 panel ($1,870 each) = $14,960

Constellation: 8-room site
  1 × Nova                    $100
  1 × RPi5 + display          $120
  8 × Storage Orbits          $240   ($30 each)
  8 × Refrig Orbits           $240   ($30 each)
  1 × Door Orbit (shared)      $30
  1 × Gigabit switch            $25
  8 × Analog board sets       $2,400  ($300 each)
  ─────────────────────────────────
  Total:                      $3,155

  Savings: ~$11,800 per site (79% reduction)

  VFD savings (if 8 drives):
  Current:  8 × FMBT-21 adapter = $1,600
  Constellation: 1 × MAX485 = $1 + wire
  Additional savings: ~$1,600
```
