# Agristar Constellation — Firmware Equipment Control Reference

> **tl;dr** — How the firmware controls equipment: what triggers mode
> changes, which sensors drive decisions, when each equipment turns on
> and off, and the cadence at which PIDs run. AS2 cadence is preserved
> bit-for-bit (P=5, I=15, D=2, U=3 s for Door / Refrig / Burner /
> Climacell). For wire-protocol invariants on the save path go to
> [`firmware-bridge-protocol.md`](firmware-bridge-protocol.md).
>
> **Last updated:** 2026-05-07 (renumbered §17, encoding fix in §17).

---

## 1. Control Loop Overview

The firmware runs a continuous loop on R5F-0 (FreeRTOS):

| Thread | Cadence | Responsibility |
|--------|---------|----------------|
| ThreadMonitor | ~1s | Calls `SetSystemState()` → `SetMode()` — the master state machine |
| ThreadSerialShift | 250ms | Polls Orbit DI/DO via Modbus TCP (`orbit_poll_io`) |
| ThreadSerialCom | 250ms | Reads Orbit sensor passthrough registers (`orbit_read_sensors`) |
| ThreadUIUpdate | 50ms ticks | Sends protobuf messages to bridge (status, equipment, warnings) |

**The core cycle is:** Read switches → Read sensors → Determine state → Call mode function → Mode function calls Ctrl* functions → Ctrl* functions set OutputOn/OutputOff and PWM values → Next poll writes those outputs to Orbit hardware.

---

## 2. System Modes (Crop Type)

The system operates in one of two crop modes, set in `Settings.SystemMode`:

| Mode | Value | Description |
|------|-------|-------------|
| **Potato** | 0 (`SM_POTATO`) | Cooling, heating, refrig, climacell, humidifiers |
| **Onion** | 1 (`SM_ONION`) | Air cure, burner cure, cooling, refrig, dehumidification |

The crop mode determines which state machine runs (`SetStatePotato` vs `SetStateOnion`) and which equipment is available.

---

## 3. Sensor Layout

All sensors come from Orbit RS-485 passthrough (Modbus holding regs 200+). The firmware reads them into `Settings.AnalogBoard[].Sensor[].Value`.

### Default Sensor Board Assignments

| Board Index | Board Type | Sensor 0 | Sensor 1 | Sensor 2 | Sensor 3 |
|-------------|-----------|----------|----------|----------|----------|
| 0 (DEFAULT_TEMP_BOARD) | Temperature | Plenum Temp 1 | Plenum Temp 2 | Outside Temp | Return Temp |
| 1 (DEFAULT_HUMID_BOARD) | Humidity | Outside Humid | Plenum Humid | Return Humid | CO2 or IR Temp |

### Key Sensor Variables Used by Control Logic

| Variable | Source | Used By |
|----------|--------|---------|
| `PlenumTempAvg` | Average of Plenum Temp 1 & 2 | Cooling, heating, refrig, burner PID, door PID |
| `*OutsideTemp` | Board 0, Sensor 2 | Start temp calc, cooling entry, cure transitions |
| `*ReturnTemp` | Board 0, Sensor 3 | Fan speed auto (delta-T), dehumid decisions |
| `StartTemp` | Calculated from ref temp ± differential | Cooling vs recirc/refrig threshold |
| `Settings.Plenum.TempSet` | User setpoint | PID target for cooling/heating/refrig |
| `Settings.Plenum.HumidSet` | User setpoint | Humidifier PID target (potato) |
| Outside Humid | Board 1, Sensor 0 | Dehumid decisions, cure transitions (onion) |
| Plenum Humid | Board 1, Sensor 1 | Humidifier control, burn cure ref (onion) |
| Return Humid | Board 1, Sensor 2 | Dehumid state (onion) |
| CO2 | Board 1, Sensor 3 | CO2 purge auto mode |

### Sensor Fault Value

`0x7FFF` = undefined/disconnected. The firmware and HAL now replace this with 0 and raise `ORBIT_WARN_SENSOR_FAULT`. When a critical sensor (plenum temp, outside temp) returns undefined, the state machine enters `ST_FAILURE` with `WARN_NO_PLENTEMP` or `WARN_NO_STARTTEMP`.

---

## 4. Run Clock (Time-Based Scheduling)

The firmware has a 48-slot run clock (one slot per half-hour, covering 24 hours). Each slot specifies a mode:

| RunClock Value | Constant | Meaning |
|----------------|----------|---------|
| 0 | `RC_NONE` | No programmed mode (system goes to standby) |
| 1 | `RC_COOLING` | Cooling requested |
| 2 | `RC_RECIRC` | Recirculation requested |
| 3 | `RC_STANDBY` | Standby requested |
| 4 | `RC_REFRIG` | Refrigeration requested |
| 5 | `RC_CURE` | Cure requested (onion only) |

In onion mode with the cure switch ON, it reads from `Settings.Cure.RunTimes[]` instead of `Settings.RunTimes[]`.

`RunClockMode()` reads the current time, divides into the current half-hour slot, and returns the programmed mode for that slot.

---

## 5. The State Machine: `SetSystemState()`

This is called once per second and determines `SystemState`. Flow:

```
SetSystemState()
  ├── CheckSystemStatus()     ← guards (power, switches, sensors, failures)
  │     ├── CheckDoorStatus()       (door init / position tracking)
  │     ├── CheckCureSwitchStatus() (cure switch state tracking)
  │     ├── Start/Stop switch OFF  →  ST_SHUTDOWN (via CheckStartSwitchStatus)
  │     ├── Power input missing     →  ST_FAILURE  (WARN_POWER)
  │     ├── Fan switch = Manual     →  ST_FAN_MANUAL
  │     ├── Fan+Freshair both OFF   →  ST_FAN_OFF
  │     ├── Fan RemoteOff = 1       →  ST_FAN_REMOTEOFF
  │     ├── Fan RemoteOff = 3       →  ST_SYSTEM_REMOTEOFF
  │     ├── RunClock = Standby      →  ST_STANDBY
  │     ├── Remote Standby input    →  ST_REMOTE_STANDBY (WARN_REMOTESTANDBY)
  │     ├── No plenum temp sensor   →  ST_FAILURE  (WARN_NO_PLENTEMP)
  │     ├── Can't calc start temp   →  ST_FAILURE  (WARN_NO_STARTTEMP)
  │     └── SystemFailuresChk()     →  ST_FAILURE
  │
  ├── Default: ST_STANDBY
  ├── SetRamp()                ← temperature ramping
  │
  ├── If RunClock=RECIRC + Fan Auto → ST_RECIRC
  │
  ├── If Potato: SetStatePotato()
  │     ├── SetStateRefrig()   ← RC_REFRIG + switches OK → ST_REFRIG
  │     ├── SetStateCooling()  ← outside < start temp → ST_COOLING
  │     └── SetStateHeating()  ← plenum < setpoint - threshold → ST_HEATING
  │
  ├── If Onion: SetStateOnion()
  │     ├── If Cure switch ON:
  │     │     └── SetStateCure()
  │     │           ├── Outside warm + dry → ST_AIRCURE
  │     │           ├── Outside warm + humid → ST_BURNERCURE
  │     │           └── Outside cold → ST_BURNERCURE
  │     ├── If Cure switch OFF:
  │     │     ├── SetStateRefrig()
  │     │     └── SetStateCooling() (if calc humidity ≤ max)
  │     └── SetDehumidification()  ← adds *DEHUMID variants
  │
  ├── SetStateDefrost()        ← after N hours of refrig runtime
  ├── CheckOutsideTempSensorStatus()
  ├── CheckPlenumSensorStatus()
  └── StartShortCycleTimer()   ← anti-short-cycle protection
```

---

## 6. State → Mode Mapping (`SetMode()`)

Once `SystemState` is determined, `SetMode()` calls the appropriate mode function:

| SystemState | Mode Function | Red Light | Yellow Light | UI Mode |
|-------------|--------------|-----------|-------------|---------|
| `ST_COOLING` | `ModeCooling(COOL_MODE)` | OFF | OFF | COOLING |
| `ST_HEATING` | `ModeHeating()` | OFF | OFF | HEATING |
| `ST_RECIRC` | `ModeRecirc()` | OFF | ON | RECIRC |
| `ST_REFRIG` | `ModeRefrig(REFRIG_MODE)` | OFF | ON | REFRIG |
| `ST_DEFROST` | `ModeRefrig(DEFROST_MODE)` | OFF | OFF | DEFROST |
| `ST_SHUTDOWN` | `ModeShutdown()` | ON | OFF | SHUTDOWN |
| `ST_STANDBY` | `ModeStandby()` | OFF | ON | STANDBY |
| `ST_REMOTE_STANDBY` | `ModeStandby()` | ON | ON | REMOTE_STANDBY |
| `ST_SYSTEM_REMOTEOFF` | `ModeShutdown()` | ON | OFF | SYSTEM_REMOTEOFF |
| `ST_FAN_MANUAL` | `ModeFanManual()` | ON | ON | FAN_MANUAL |
| `ST_FAN_OFF` | `ModeFanOff()` | OFF | ON | FAN_OFF |
| `ST_FAN_REMOTEOFF` | `ModeFanOff()` | ON | ON | FAN_REMOTEOFF |
| `ST_REFRIG_REMOTEOFF` | `ModeStandby()` | ON | ON | REFRIG_REMOTEOFF |
| `ST_REFRIG_STANDBY` | `ModeStandby()` | ON | ON | REFRIG_STANDBY |
| `ST_AIRCURE` | `ModeAirCure()` | OFF | OFF | AIRCURE |
| `ST_BURNERCURE` | `ModeBurnerCure()` | OFF | OFF | BURNERCURE |
| `ST_COOLDEHUMID` | `ModeCooling(DEHUMID_MODE)` | OFF | OFF | COOLDEHUMID |
| `ST_REFRIGDEHUMID` | `ModeRefrig(DEHUMID_MODE)` | OFF | ON | REFRIGDEHUMID |
| `ST_FAILURE` | `ModeStandby()` | ON | ON | FAILURE |

After mode execution, manual overrides (`ApplyManualOverrides()`) force equipment ON for any `RemoteOff[i] == 2` (REMOTE_MANUAL). Purge and fan boost overlays then apply.

---

## 7. Equipment Control Detail

### 7.1 Fan (`CtrlFan()`)

**Equipment:** `EQ_FAN` (index 0), `PWM_FAN` (PWM channel 2)

**When it turns ON:**
- Any mode except Standby, Shutdown, Fan Off, System RemoteOff
- `CheckInputs(SW_FAN_AUTO)` must be active (start/stop switch ON + fan auto switch ON)

**When it turns OFF:**
- `speed == CTRL_OFF` (standby/shutdown modes)
- `Settings.RemoteOff[RO_FAN] == 1` (remotely forced off)
- Fan switch in OFF position

**Speed control:**
- **Fixed speed:** Recirc uses `RecircSpeed`, Refrig uses `RefrigSpeed`, Cure uses `MaxSpeed`
- **Auto (delta-T):** Cooling and Heating modes. Every `Fan.UpdatePeriod` hours:
  - Potato: Compare Ref2 (return temp or sensor) against Ref1 (plenum setpoint or avg)
  - If `Ref2 - Ref1 > Diff` → ramp up (increment PWM by `PWM_INC_VALUE`)
  - If `Ref2 - Ref1 ≤ Diff` → ramp down (decrement PWM by `PWM_INC_VALUE`)
  - Clamps between `Fan.MinSpeed` and `Fan.MaxSpeed`
  - Onion with `UpdateMode=1`: Uses humidity instead of temperature (Plenum HumidSet vs sensor)

**Note:** Fan speed is NOT adjusted during active Purge or Fan Boost.

---

### 7.2 Fresh Air Doors (`CtrlDoors()`)

**Equipment:** `EQ_DOORS` (index 1), `PWM_DOORS` (PWM channel 0), `EQ_PULSEDOOR_OPEN/CLOSE` (indices 41/42)

**When doors OPEN (PID target):**
- **Cooling mode:** PID controller targets `PlenumTempAvg → Settings.Plenum.TempSet`
  - If `PlenumTempAvg > TempSet` → doors open more
  - If `PlenumTempAvg < TempSet` → doors close more
- **Burner cure:** Doors used to modulate temperature when burner is at threshold

**When doors CLOSE:**
- Recirc, Refrig, Heating, Standby, Shutdown — all call `CtrlDoorsClose()`
- Exception: Doors stay open during CO2 purge (`PurgeOn()`)

**Anti-short-cycle:** When exiting cooling (doors > 95% open and outside temp exceeds start temp), a short-cycle timer prevents re-entering cooling for `Door.CoolAirCycle` minutes.

---

### 7.3 Refrigeration (`CtrlRefrig()`)

**Equipment:** `EQ_REFRIGERATION` (index 2), `PWM_REFRIGERATION` (PWM channel 1), `EQ_REFRIG_STAGE1-8` (indices 13-20), `EQ_REFRIG_DEFROST1-2` (indices 21-22)

**When it turns ON (all required in `SetStateRefrig()`):**
- `RunClockMode() == RC_REFRIG`
- `CheckInputs(SW_REFRIG_AUTO)` active
- `CheckInputs(SW_FAN_AUTO)` active
- `Settings.RemoteOff[RO_REFRIGERATION] != 1` (else → `ST_REFRIG_REMOTEOFF`)
- `CheckInputs(EQ_REFRIG_STANDBY)` active (else → `ST_REFRIG_STANDBY` + `WARN_REFRIGSTANDBY`)
- If `AL_REFRIGERATION` alarm: behavior depends on `Settings.Refrig.FailMode` (0=recirc, 1=standby, 2=keep running refrig)

**PID Control:**
- Error = `PlenumTempAvg - Settings.Plenum.TempSet`
- PID output mapped to PWM, updated at `Settings.Refrig.PID.U` interval
- Output changes limited to `PWM_INC_VALUE` per step (smooth ramping)

**Stage Switching:**
- Each stage has configurable `On` and `Off` thresholds (% of PID output)
- Stage N turns ON when `RefrigPercent >= Stage[N].On`
- Stage N turns OFF when `RefrigPercent < Stage[N].Off`
- Hysteresis between On/Off thresholds prevents rapid cycling

**Defrost** (handled in `ModeRefrig(DEFROST_MODE)` and `SetStateDefrost()`, NOT in `CtrlRefrig`):
- Triggers after `Settings.Refrig.DefrostPeriod` hours of refrig runtime
- During defrost: `CtrlRefrigOff()`, then defrost heater outputs ON (unless diagnostic or alarmed)
- Saves and restores PID state across defrost cycles

---

### 7.4 Burner (`CtrlBurner()`) — Onion Only

**Equipment:** `EQ_BURNER` (index 6), `PWM_BURNER` (PWM channel 3)

**When it turns ON:**
- Burner cure mode (`ST_BURNERCURE`)
- Cooling/Refrig dehumid mode (`DEHUMID_MODE`) — burner runs at `Settings.Burner.Low` (low fire)

**When it turns OFF:**
- `Settings.RemoteOff[RO_BURNER] == 1`
- `SystemAlarm[AL_BURNER] != FM_NONE` (burner failure)
- `SystemAlarm[AL_HIGHPLENTEMP] != FM_NONE` (high plenum temp alarm)
- Any mode that passes `CTRL_OFF` to CtrlBurner

**PID Control (Auto mode):**
- Error = `Settings.Burner.TempSet - PlenumTempAvg`
- Modulates PWM output for proportional burner control

**Burner Cure State Machine (BURNER_MAX mode):**
1. `CS_BURNER` → Modulate burner via PID
2. `CS_MOD_DOOR` → Burner at threshold, modulate doors for temp
3. `CS_MOD_BURNER` → Doors maxed, reduce burner
4. `CS_MOD_BURNER_DOOR_LOCK` → Doors closed, increase burner
5. `CS_MOD_BURNER_DOOR_UNLOCK` → Burner exceeded threshold, start reopening doors
6. `CS_HOLD_BURNER_MOD_DOOR` → At setpoint, hold burner and fine-tune with doors
7. `CS_DEHUMID` → Warm+humid: low-fire burner with door temp control
8. `CS_MANUAL` → Fixed burner output

---

### 7.5 Heater — Potato Only

**Equipment:** `EQ_HEAT` (index 4)

**Entry conditions** (in `SetStateHeating()`, not `CtrlHeat()`):
- `Settings.EquipIo[EQ_HEAT].Enabled == 1`
- `Settings.RemoteOff[RO_HEAT] != 1`
- `CheckInputs(SW_FAN_AUTO)` active
- `RunClock == RC_COOLING || RC_RECIRC || RC_REFRIG`
- `*OutsideTemp < StartTemp` (outside air too cold to cool)
- `PlenumTempAvg < Settings.Plenum.TempSet - HeatTempThresh` (to enter)
- Once running, stays on until `PlenumTempAvg >= Settings.Plenum.TempSet` (hysteresis)

**`CtrlHeat()` itself is trivial:** If no `AL_HEAT` alarm and `RemoteOff[RO_HEAT] == 0` → `OutputOn(EQ_HEAT)`, else `OutputOff`. All temperature logic is in the state machine, not the controller.

**When it turns OFF:**
- Plenum reaches setpoint (state machine exits `ST_HEATING`)
- Any mode other than heating
- `RemoteOff[RO_HEAT] == 1`, or heat alarm

---

### 7.6 Climacell (`CtrlClimacell()`) — Potato Only

**Equipment:** `EQ_CLIMACELL` (index 3)

**When it turns ON:**
- `CheckInputs(SW_CLIMACELL_AUTO)` active
- Cooling, Refrig, Recirc, or Heating modes (potato)
- PID-controlled based on humidity setpoint, or clock-schedule mode

**Control Modes:**
1. **Auto (PID):** Targets plenum humidity setpoint
2. **Clock:** Time-of-day schedule with configurable on/off slots

**When it turns OFF:**
- `Settings.RemoteOff` for climacell
- Switch not in auto
- Onion modes (climacell is potato-only equipment)

---

### 7.7 Humidifiers (`CtrlHumidifier()`) — Potato Only

**Equipment:** `EQ_HUMID_HEAD1-3` (indices 7, 9, 11), `EQ_HUMID_PUMP1-3` (indices 8, 10, 12)

**When they turn ON:**
- `CheckInputs(SW_HUMID_AUTO)` active
- Active in: Cooling, Refrig, Recirc, Heating, Fan Manual modes
- Each humidifier has independent head and pump outputs

**Control Modes:**
1. **Manual** (`HM_MANUAL`): Forces on when switch and fan conditions met
2. **Timer** (`HM_TIMER`): Duty cycle with configurable On/Off durations per system mode
3. **Auto (PID)** (`HM_AUTO`): Uses Climacell PID settings. Error = `HumidSet - PlenumHumid`. Humidifiers 2 & 3 mirror humidifier 1 when all are in auto.

**Head vs Pump behavior:** Heads run continuously; pumps are duty-cycled. When cycling ON, both pump and head turn on together. When cycling OFF, only the pump turns off (head stays on). When fully off (`HE_CONTROL_OFF`), both turn off. There is NO pump-then-head delay sequence.

---

### 7.8 Cavity Heat (`CtrlCavityHeat()`)

**Equipment:** `EQ_CAVITY_HEAT` (index 5)

**When it turns ON:**
- Active in most running modes (not standby/shutdown)
- Either duty-cycle mode or auto (temperature-based)
- `Settings.CavityHeat.Mode` determines behavior

**When it turns OFF:**
- Standby or shutdown
- `Settings.RemoteOff` for cavity heat

---

### 7.9 CO2 Purge (`CtrlPurge()`)

**Equipment:** Uses fan + doors (not a separate output)

**When it activates:**
- CO2 sensor exceeds `Settings.Co2.Purge.Setpoint` (auto mode)
- Manual trigger via `Settings.Co2.Purge.Start`

**What it does:**
1. Opens doors to `Settings.Co2.DoorOutput` (configurable percentage, not always 100%)
2. Sets fan to `Settings.Co2.FanOutput` (configurable speed, not always max)
3. In refrig mode: if `Settings.Refrig.Purge == 1`, `ModeRefrig()` calls `CtrlRefrigOff()` during purge (this happens in the mode function, not in CtrlPurge itself). Purge also aborts if refrig output exceeds `Co2.Purge.RefrigThresh`.
4. Saves previous door/fan values, runs for programmed duration, then restores them

---

### 7.10 Fan Boost (`CtrlFanBoost()`)

**Equipment:** Uses fan (not a separate output)

**When it activates** (only in `ST_COOLING`, `ST_RECIRC`, or `ST_HEATING`, and not during purge):
- **Mode 1 (Temperature-based):** If `*OutsideTemp < Settings.FanBoost.Temp` (outside is cold enough) AND fan speed ≤ `CONTINUOUSFAN_THRESHOLD` AND interval elapsed
- **Mode 2 (Runtime-based):** When fan has run continuously for `Settings.FanBoost.Interval` hours

**What it does:**
1. Overrides current fan speed to max
2. Runs for programmed duration
3. Restores previous fan speed when complete

---

### 7.11 Auxiliary Outputs (`CtrlAux()`)

**Equipment:** `EQ_AUX1-8` (indices 25-32)

**Control: Programmable rules** via `Settings.AuxProgram[]`. Each aux output has rules that can be:
- **Manual:** Fixed on/off + duty cycle
- **Output-based:** Follows another equipment output
- **Input-based:** Follows a digital input
- **Switch-based:** Follows a switch position
- **Sensor-based:** Compares a sensor value to a threshold (with 2% hysteresis)
- **Mode-based:** Active during specific operating modes

---

### 7.12 Bay Lights (`CtrlBayLights()`)

**Equipment:** `EQ_LIGHTS1`, `EQ_LIGHTS2` (indices 23, 24)

Lights are **ON by default** when `RemoteOff == 0` (AUTO). Set `RemoteOff == 1` to turn them OFF. No automatic control logic — purely software-toggled.

---

### 7.13 Indicator Lights (`SetLightStatus()`)

**Equipment:** `EQ_REDLIGHT` (index 38), `EQ_YELLOWLIGHT` (index 39)

| Red | Yellow | Meaning |
|-----|--------|---------|
| OFF | OFF | Normal running (cooling, cure) |
| OFF | ON | Passive modes (standby, recirc, refrig, fan off) |
| ON | OFF | Shutdown, system remote off |
| ON | ON | Manual, remote standby, failure, refrig remote off |
| BLINK | * | Active alarm |

---

## 8. Switch Inputs and Their Effects

| Switch | Equipment Index | Effect When Active |
|--------|----------------|-------------------|
| Start/Stop | `SW_START_STOP` (43) | System runs. When inactive → `ST_SHUTDOWN` |
| Fan Auto | `SW_FAN_AUTO` (44) | Enables automatic fan control. Required for all auto modes |
| Fan Manual | `SW_FAN_MANUAL` (45) | Forces `ST_FAN_MANUAL` — fan runs at previous speed |
| Freshair Auto | `SW_FRESHAIR_AUTO` (46) | Enables door PID control. Required for cooling entry |
| Freshair Manual | `SW_FRESHAIR_MANUAL` (47) | Not currently used by state machine |
| Climacell Auto | `SW_CLIMACELL_AUTO` (48) | Enables climacell control (potato) |
| Climacell Manual | `SW_CLIMACELL_MANUAL` (49) | Not currently used by state machine |
| Humid Auto | `SW_HUMID_AUTO` (50) | Enables humidifier control (potato) |
| Humid Manual | `SW_HUMID_MANUAL` (51) | Not currently used by state machine |
| Refrig Auto | `SW_REFRIG_AUTO` (52) | Required for `ST_REFRIG` entry |
| Cure Auto | `SW_CURE_AUTO` (53) | Selects cure run clock and enables cure states (onion) |
| Burner Auto | `SW_BURNER_AUTO` (54) | Enables burner control (onion). Failure check requires this |
| Aux 1 Auto/Manual | `SW_AUX1_AUTO/MANUAL` (55-56) | Aux 1 programmable control |
| Aux 2 Auto/Manual | `SW_AUX2_AUTO/MANUAL` (57-58) | Aux 2 programmable control |

On Nova/Orbit, physical DIP switches are read as Modbus discrete inputs and mapped into `IoBoard[].InputState` bitfields. The legacy switch positions are emulated via the `InputState` bit layout (v2 CPLD format).

---

## 9. RemoteOff States (Software Overrides)

Each equipment has a `Settings.RemoteOff[i]` value:

| Value | Constant | Effect |
|-------|----------|--------|
| 0 | `REMOTE_AUTO` | Normal automatic control |
| 1 | `REMOTE_OFF` | Equipment forced off — overrides all automatic logic |
| 2 | `REMOTE_MANUAL` | Equipment forced ON — `ApplyManualOverrides()` sets output |
| 3 | `REMOTE_SYSSTOP` | Emergency system stop — all equipment off, latching |

`REMOTE_SYSSTOP` is set by `CMD_SYSTEM_STOP` (system command 5) from the bridge. It sets ALL equipment to SYSSTOP. Cleared only by `CMD_CLEAR_ALARM` (system command 1).

When `RemoteOff[RO_FAN] == 3`, the state machine enters `ST_SYSTEM_REMOTEOFF` which calls `ModeShutdown()`.

---

## 10. Failure Detection

`SystemFailuresChk()` runs during `CheckSystemStatus()`. Failures use timer-based detection with configurable `Mode` (None/Alarm/Fail) and `Timer` (minutes).

### Pattern
1. Equipment output is ON
2. Equipment input (feedback) does not confirm within `Timer` minutes
3. Alarm is set; if `Mode == Fail`, system enters `ST_FAILURE`

### Key Failure Checks

| Failure | Checks | Alarm |
|---------|--------|-------|
| Fan Fail | Fan output ON + Fan Auto switch ON, but no DI feedback | `AL_FAN` |
| Refrig Fail | Refrig PWM output > 0 but no 4-20mA feedback | `AL_REFRIGERATION` |
| Refrig Stage N | Stage N output ON but no DI feedback | `AL_REFRIG_STAGE1-8` |
| Climacell Fail | Climacell output ON + switch auto, no feedback | `AL_CLIMACELL` |
| Humid Fail | Humid head output ON + switch auto, no feedback | `AL_HUMID1-3` |
| Heat Fail | Heat output ON, no feedback | `AL_HEAT` |
| Cavity Heat Fail | Cavity heat output ON, no feedback | `AL_CAVITYHEAT` |
| Burner Fail | Burner output ON + switch auto, no feedback | `AL_BURNER` |
| High Plenum Temp | `PlenumTempAvg > Settings.Failure[].Value` | `AL_HIGHPLENTEMP` |
| Low Plenum Temp | `PlenumTempAvg < Settings.Failure[].Value` | `AL_LOWPLENTEMP` |
| Plenum Sensor | Plenum Temp 1 and 2 differ by more than threshold | `AL_PLENSENSOR` |
| Outside Temp Sensor | Sensor returns undefined | `AL_OUTTEMPSENSOR` |
| Outside Humid Sensor | Sensor returns undefined | `AL_OUTHUMIDSENSOR` |
| Outside Humid Variance | Sensor does NOT vary by 3% over 24 hours (stuck sensor) | `AL_OUTHUMIDVAR` |
| Low Plenum Humidity | Below `HumidLowFailure` threshold | `AL_PLENHUMID` |
| High CO2 | Above `Co2.HighFailure` threshold | `AL_HIGHCO2` |
| Airflow | Airflow DI not active when fan is running | `AL_AIRFLOW` |
| Power | Power DI not active | (handled in CheckSystemStatus directly) |

### Orbit-Specific Warnings (NEW — codes 0x100+)

| Warning | Code | Severity | Trigger |
|---------|------|----------|---------|
| Orbit E-Stop | 0x100 | Alarm | Orbit DI bit 10 active |
| Orbit Safe Mode | 0x101 | Alarm | Orbit watchdog tripped |
| Orbit Comm Lost | 0x102 | Alarm | 5+ consecutive Modbus errors |
| Orbit Sensor Fault | 0x103 | Warning | Any sensor register = 0x7FFF |
| Orbit CPU Overtemp | 0x104 | Warning | Orbit CPU > 85°C |

> **Mirror dependency (shadow-mirror bug class).** Every per-equipment
> failure check above gates on `Settings.Failure[FAIL_X].Mode != FM_NONE`.
> Those entries are populated from `LpFailure`/`LpFailure2` by
> `mirror_failure()` in `Nova_Firmware/lp_am2434/lp_engine_shim.c`. If a
> failure-config field the engine reads ever has no mirror, the value
> stays BSS-zero (`FM_NONE`) and **the check is silently disabled** — no
> error, no log, just absent protection. This is the inverse of the more
> familiar "equipment stuck at 0%" symptom. Before adding any new
> failure check, confirm its config has a `mirror_*()` populating it.
> Full audit + bug catalogue: `memories/repo/lp-engine-shadow-mirror-rule.md`.

---

## 11. Start Temperature Calculation

`SetStartTemp()` determines the temperature threshold for entering cooling mode:

```
StartTemp = ReferenceTemp + OutsideAir.Diff
```

Where `ReferenceTemp` can be:
- **Plenum setpoint** (`OutsideAir.TempRef == 255`)
- **Return air temp** (`OutsideAir.TempRef == 254`)
- **Any sensor** (by sensor ID)

And `Diff` is signed (can be above or below reference, based on `OutsideAir.AboveBelow`).

**Cooling enters when:** `OutsideTemp < StartTemp` (outside air is cool enough to be useful)
**Cooling exits when:** `OutsideTemp > StartTemp` AND doors are > 95% open (outside air can't maintain setpoint)

For climacell-equipped potato systems, `StartTemp` can be adjusted by the wet-bulb depression (evaporative cooling potential).

---

## 12. Temperature Ramping

`SetRamp()` adjusts `Settings.Plenum.TempSet` over time:

- **Manual ramp:** Steps the setpoint by `Ramp.AdjustValue` every `Ramp.AdjustPeriod`
- **Auto ramp:** Moves setpoint toward a reference sensor value, clamped to `Ramp.AdjustValue` per period

Ramping modifies the live `TempSet` that all PID controllers use, so all modes respond to the ramp simultaneously.

---

## 13. Dehumidification (onion)

`SetDehumidification()` adds dehumidification to cooling or refrig modes. Both paths require:
- `CheckInputs(SW_BURNER_AUTO)` active
- `Settings.RemoteOff[RO_BURNER] != 1`
- `ReturnHumid != SENSOR_VAL_UNDEFINED`
- `ReturnHumid > Settings.Plenum.HumidSet` (to enter), or `≥ HumidSet × 0.95` (to stay in — 5% hysteresis)

**REFRIGDEHUMID** (`ST_REFRIG` → `ST_REFRIGDEHUMID`):
- Additional: refrig PWM output < 90%

**COOLDEHUMID** (`ST_COOLING` → `ST_COOLDEHUMID`):
- Additional: `*OutsideTemp > Settings.Refrig.Limit` AND `*OutsideTemp <= Settings.Plenum.TempSet - 4`

In dehumid mode, the burner runs at `Settings.Burner.Low` (low fire) to reduce humidity while maintaining temperature control.

---

## 14. Defrost Scheduling

`SetStateDefrost()` tracks compressor runtime via `RefrigRunTimer()`:

1. System must be in `ST_REFRIG` or `ST_REFRIGDEHUMID` with `Settings.Refrig.DefrostPeriod > 0`
2. When `IT_REFRIGRUNTIME >= Settings.Refrig.DefrostPeriod * T_HOURS` → `SystemState = ST_DEFROST`
3. On entry: saves refrig PID state (P/I/D accumulators)
4. During defrost: compressors OFF, defrost heater outputs ON
5. Stays in defrost until `DefrostDuration * T_MINS` elapsed
6. On exit: resets `IT_REFRIGRUNTIME` and `IT_DEFROSTCYCLE`, restores PID state, refrig resumes

---

## 15. Equipment I/O Mapping (Nova/Orbit)

On Nova, equipment is mapped to Orbit digital outputs via `Settings.EquipIo[]`:

| EQ Index | Default DO | Equipment |
|----------|-----------|-----------|
| 0 (EQ_FAN) | DO1 | Fan contactor |
| 1 (EQ_DOORS) | DO2 | Door actuator |
| 2 (EQ_REFRIGERATION) | DO3 | Refrig enable |
| 4 (EQ_HEAT) | DO4 | Heater contactor |
| 7 (EQ_HUMID_HEAD1) | DO5 | Humidifier 1 head |
| 8 (EQ_HUMID_PUMP1) | DO6 | Humidifier 1 pump |
| 3 (EQ_CLIMACELL) | DO7 | Climacell pump |
| 13 (EQ_REFRIG_STAGE1) | DO8 | Compressor stage 1 |

PWM outputs (Fan, Doors, Refrig, Burner) go to the Orbit's analog outputs (AO1/AO2, 0-10V or 4-20mA) or to VFD drives via Orbit RS-485 passthrough.

Each equipment entry in `Settings.EquipIo[]` specifies:
- `Output`: Which DO (or `IO_UNDEFINED` if no DO)
- `Input`: Which DI for feedback (or `IO_UNDEFINED`)
- `Enabled`: Whether this equipment is active
- `Mode`: Which system mode(s) this equipment applies to (Potato/Onion/All)

---

## 16. Priority of Overrides (Highest to Lowest)

1. **E-Stop** (Orbit DI bit 10) → clears `SW_START_STOP` → `ST_SHUTDOWN`
2. **System Stop** (`RemoteOff[RO_FAN] == 3`) → `ST_SYSTEM_REMOTEOFF`
3. **Power failure** → `ST_FAILURE`
4. **System failures** (`SystemFailuresChk()`) → `ST_FAILURE`
5. **Remote Off** (`RemoteOff[i] == 1`) → Equipment forced off
6. **Switch positions** (Fan Auto/Manual/Off, Refrig Auto, Cure Auto)
7. **Run Clock** (time-of-day schedule)
8. **Sensor-driven state machine** (outside temp vs start temp, etc.)
9. **PID controllers** (within a mode, fine-tune PWM outputs)
10. **Remote Manual** (`RemoteOff[i] == 2`) → Equipment forced ON (applied after mode)


---

## 17. Orbit analog-output fan-out (Nova-only, post-AS2)

The four legacy PWM channels (`PWM_FAN`, `PWM_DOORS`,
`PWM_REFRIGERATION`, `PWM_BURNER`) are mirrored onto operator-selectable
Orbit AO channels. Programming lives in `Settings.AoEquip[slot][ch]`
(persisted in OSPI). The Level 2 PWM 4-20 mA Output Setup page
(`/level2/pwm`) is the operator UI; per-AO writes hit
`POST /iot/orbits/aoequip`.

### Cadence (matches AS2)

- Underlying PID controllers (Door / Refrig / Burner / Climacell) re-run
  every `Settings.X.PID.U` seconds. Legacy factory defaults
  (`GetFactoryDefault` in `Application/Settings.c`):
  - **P=5, I=15, D=2, U=3** for `Door`, `Refrig`, `Climacell`, `Burner`.
- Fan in auto mode is throttled by `Settings.Fan.UpdatePeriod` (hours),
  driven by `CtrlFan` in `nova_controls.c`.
- `nova_fan_output_tick` (Nova orbit thread, ~1 Hz) samples
  `PwmChannel[X].Output` and writes to:
  1. **Modbus VFD path** — for present, non-faulted drives discovered
     by `nova_vfd_tick`.
  2. **Analog-output path** — for each `Settings.AoEquip[slot][ch]`
     selector, writes the corresponding channel's 0..1000 (percent × 10)
     value to that Orbit AO.
- 1 Hz is faster than the slowest PID (3 s), so AOs reflect every PID
  step with ≤ 1 s extra latency — matching legacy TM4C EPWM where
  `PWM_UpdateChannel()` pushed instantly on every write.

### Selectors (`ao_equip_t`, `nova_fan_output.h`)

`0=UNUSED, 1=FAN_SPEED, 2=DOORS, 3=REFRIG, 4=BURNER`. Persisted as
`uint8` per slot/channel — **never renumber.**

Full wire-up notes:
[`memories/repo/ao-equip-assign.md`](../memories/repo/ao-equip-assign.md).
