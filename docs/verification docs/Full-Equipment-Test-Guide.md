# Full Equipment & Mode Verification Test — Setup & Flow Guide

This document describes the required UI settings and the complete testing flow for the 14-phase equipment verification matrix in the RS485 Panel Test Lab (`http://localhost:9001/` → Test Lab tab).

---

## Pre-Test Settings (configure in the Agristar UI before running)

These settings **must** be configured by the operator in the UI. The test cannot change them via API because some values (like run clocks) don't sync to the live firmware state when POSTed.

### Temperature & Humidity
| Setting | Value | Where to Set |
|---------|-------|--------------|
| Temperature Setpoint | **46°F** | Level 1 → Setpoints |
| Humidity Setpoint | **95%** | Level 1 → Setpoints |
| Heating Threshold | **11°F** below SP (triggers at 35°F) | Level 2 → Setpoints |

### Run Clock
| Setting | Value | Where to Set |
|---------|-------|--------------|
| Run Clock Mode | **Refrigeration** (all 48 half-hour slots) | Level 2 → Run Clocks |

> This allows cooling, refrigeration, recirculation, and heating modes to activate based on conditions. If you need to test Recirc independently, change the run clock to Recirc — or Phase 4 will attempt to force recirc by failing all refrig stages.

### Fan & Cooling
| Setting | Value | Where to Set |
|---------|-------|--------------|
| Cooling Fan Speed | **25%** | Level 2 → Fan Speed |
| Refrig Fan Speed | **100%** | Level 2 → Fan Speed |
| Recirc Fan Speed | **50%** | Level 2 → Fan Speed |
| Short Cycle Timer | **1 minute** | Level 2 → Fresh Air Door |

### CO2 Purge
| Setting | Value | Where to Set |
|---------|-------|--------------|
| CO2 Trigger Level | **2000 ppm** | Level 2 → CO2 |
| CO2 Outside Temp Low | **40°F** | Level 2 → CO2 |
| CO2 Outside Temp High | **50°F** | Level 2 → CO2 |
| CO2 Purge Duration | **5 minutes** | Level 2 → CO2 |

### Humidifiers
| Humidifier | Mode | On/Off Times | Where to Set |
|------------|------|-------------|--------------|
| Humid1 | **Manual** (on when fans run) | — | Level 2 → Humidity |
| Humid2 | **Timer** | 60s on / 60s off | Level 2 → Humidity |
| Humid3 | **Auto** (PID, ~3% below SP) | — | Level 2 → Humidity |

> **Humidifier behavior**: In timer/auto mode the head stays ON and only the pump cycles on/off. On proof failure, **both** head and pump turn off.

### Refrigeration Staging
| Stage | ON Threshold | OFF Threshold |
|-------|-------------|---------------|
| Stage 1 | 20% | 10% |
| Stage 2 | 30% | 20% |
| Stage 3 | 40% | 30% |
| Stage 4 | 50% | 40% |
| Stage 5 | 60% | 50% |
| Stage 6 | 70% | 60% |

> These are the **default** staging values from `p2Refrigeration`. Stages activate based on the refrig PID output percentage, not by temperature delta.

### Alarm Settings
| Setting | Value | Where to Set |
|---------|-------|--------------|
| All Equipment Proof Delays | **1 minute** | Level 2 → Alert Setup |
| Fan Proof Failure | **System Failure** | Level 2 → Alert Setup |
| Air Flow Restriction | **Immediate System Failure** | Level 2 → Alert Setup |
| Low Temp | **Immediate System Failure** | Level 2 → Alert Setup |
| All Other Proofs | **Alarm + Stop Equipment Only** | Level 2 → Alert Setup |

### Aux1 / Cavity Heat
| Setting | Value | Notes |
|---------|-------|-------|
| Aux1 (Door Heat Tape) | ON below **28°F** outside | Default from `p2Misc` |
| Cavity Heat | ON below **30°F** outside | Default from `p2Misc` |

### Switches (before test starts)
| Switch | Position |
|--------|----------|
| Start/Stop | **Start** |
| Fan | **Auto** |
| Fresh Air | **Auto** |
| Climacell | **Auto** |
| Humid | **Auto** |
| Refrig | **Auto** |

---

## Test Infrastructure

### How it works
- The test runs **entirely from the browser** (Test Lab tab in the RS485 panel).
- It manipulates **sensor values** via the RS485 panel API (`/api/sensors`) to drive mode transitions.
- It opens/closes **digital inputs** via `/api/digital-io` to test proof failures.
- It toggles **switches** via `/api/switches` for Fan Manual, Shutdown, and Start.
- It sends **ClearAlarm** via the bridge (`/iot/post`) to recover from alarm states.
- It reads **equipment status** from the bridge WebSocket (`/iot/ws/equipment-data`) and **header data** (`/iot/ws/header-data`) to verify outputs.
- **Physics engine is disabled** at the start of each phase so sensor overrides remain stable.

### Proof test helper (`testProof`)
The `testProof()` function handles all proof failure tests:
1. Opens the specified DI port (proof fails)
2. Waits for the alarm/failure to trigger (alarm `waitFor` covers the ~60s proof timer)
3. Waits **10 additional seconds** after alarm for equipment to fully shut down
4. For system failures: verifies FAILURE mode, Fan OFF, Green OFF
5. For equipment proofs: verifies the equipment output is OFF, plus the **head** output if applicable (humidifiers have separate head and pump outputs)
6. Restores the DI, waits 2s, sends ClearAlarm
7. For system failures: waits for recovery to a running mode

### Output verification helper (`verifyOut`)
The `verifyOut()` function checks multiple equipment outputs against expected values:
- Fan, Door Switch (AUTO), Climacell, Heat, Green, Yellow, Red, Humid1 Pump
- **CPLD Door Close** / **CPLD Door Open** (read from CPLD shift register via panel `/api/status`, NOT from EquipStatus)
- Fan speed (±5% tolerance)

> **Wire format note**: The EquipStatus `eq[29]` field is the **door switch position** (0=off, 1=auto, 2=manual) per firmware `CheckInputs(SW_FRESHAIR_AUTO/MANUAL)`. Pulsed door CPLD outputs (close/open/power) are **not in EquipStatus** at all — the production firmware only drives them via the shift register. The test reads CPLD output state via `getCpldDoorState()` which calls the panel’s `/api/status` endpoint to read `cpldOutputs.main` bits 0 (close), 1 (open), 2 (power).

### What it does NOT change
- No modifications to the ARM simulator or firmware behavior.
- No modifications to the Svelte UI code.
- Settings are **not** changed via API (operator configures them in the UI beforehand).

### Running
- **Run All**: Executes phases 1–14 sequentially. If a phase fails, it continues to the next.
- **Run Individual**: Click the ▶ button next to any test card to run just that phase.
- **Stop**: Click Stop to abort the current running phase (cleanup runs in `finally` blocks).
- **Total estimated time**: ~45–75 minutes (Phase 3 Defrost alone can take up to 60 minutes).

---

## Test Phases — Detailed Flow

### Phase 1: Cooling Mode + Proofs
**Goal**: Verify the system enters Cooling and all cooling-related outputs are correct.

**Sensor Setup**:
- Plenum: SP + 3°F (e.g., 49°F)
- Outside: 30°F (below SP, so cooling is available)

**Verifications**:
1. Mode enters **Cooling** or **Cool Ramp** within 90s
2. Fan ON at 25%, Doors OPEN, Climacell ON, Heat OFF
3. Green light ON, Yellow OFF, Red OFF
4. Humid1 pump ON (manual — pump runs while fans are running)
5. **Climacell proof test**: open main DI port 3 → wait for alarm + 10s shutdown → climacell OFF → restore + ClearAlarm
6. **Humid1 proof test**: open main DI port 4 → wait for alarm + 10s shutdown → humid1 pump OFF **and** head OFF → restore + ClearAlarm
7. Cool output is > 0%
8. Drop plenum to SP − 3°F → polling loop checks every 10s for up to **15 minutes** (PID ramp-down), stall detection after **3 minutes** (18 checks) → cool output drops to 0%, CPLD close pulse ON (checked via `getCpldDoorState()`), fan stays ON

**Duration**: ~5–12 minutes (PID ramp-down can vary)

---

### Phase 2: Refrigeration Mode + Stage Sequencing
**Goal**: Enter Refrig mode, watch stages activate 1→6, test stage proofs, verify Humid2 timer.

**Sensor Setup**:
- Plenum: SP + 4°F (e.g., 50°F)
- Outside: 60°F (above cooling available temp → forces refrig)

**Entry Method**: Stop/Start cycle — the test switches to Shutdown, verifies shutdown, then switches back to Start. With these sensor values the firmware enters Refrigeration directly, bypassing the Cooling ramp that would otherwise occur.

**Verifications**:
1. Shutdown confirmed, then Start → mode enters **Refrigeration** within 120s
2. Fan ON at 100%, Doors CLOSED, Climacell ON, Heat OFF
3. Green ON, Yellow ON, Red OFF
4. Watch refrig PID output build over ~12 minutes — stages turn on sequentially:
   - Stage 1 at 20%, Stage 2 at 30%, ... Stage 6 at 70%
5. **Stage 1–3 proof tests**: open EX2 DI ports 0–2 → each stage shuts off after alarm + 10s → restore + ClearAlarm
6. **Humid2 timer cycling**: sample Humid2 pump state every 20s for ~2.5 min, verify at least 1 ON/OFF transition
7. **Humid2 proof test** (if pump currently ON): open main DI port 6 → humid2 pump OFF **and** head OFF → restore + ClearAlarm

**Duration**: ~15 minutes (PID ramp takes time)

---

### Phase 3: Defrost Cycle + Proofs
**Goal**: Wait for automatic defrost cycle (1-hour timer in refrig mode), then test defrost outputs.

**Sensor Setup**:
- Plenum: SP + 4°F, Outside: 60°F (same as Phase 2, enters Refrig via Stop/Start cycle)

**Verifications**:
1. Enter **Refrigeration** via Stop/Start cycle
2. Wait **up to 60 minutes** for defrost timer to trigger
3. When **Defrost** mode starts:
   - Fan ON at 100%, Doors CLOSED
   - Green ON, Yellow ON, Red OFF
   - Defrost 1 ON, Defrost 2 ON
   - All refrig stages OFF
4. **Defrost 1 proof test**: open EX2 DI port 6 → defrost 1 stops → restore + ClearAlarm
5. **Defrost 2 proof test**: open EX2 DI port 7 → defrost 2 stops → restore + ClearAlarm (skipped if defrost already ended)
6. Wait for defrost to end and return to Refrig (up to 15 min)

**Duration**: Up to **60+ minutes** (waiting for defrost timer)

---

### Phase 4: Recirculation Mode (Refrig Main Failure)
**Goal**: Force recirc by failing all 6 refrig stage proofs, then test Humid3 auto mode.

**Sensor Setup**:
- Same as Refrig (plenum SP + 4°F, outside 60°F)

**Entry Method**: Stop/Start cycle (same as Phase 2) to enter Refrigeration directly.

**Flow**:
1. Enter Refrigeration via Stop/Start cycle
2. Wait **10 minutes** for refrig PID to build and stages to activate
3. After 10 min, check if all 6 stages are ON. If not, wait **1 additional minute** and re-check (repeats up to 15 times)
4. Once all 6 stages confirmed active, **open all 6 stage proof DIs** (EX2 ports 0–5) simultaneously
5. Wait **>1 minute** (75s) for all stage alarms to trigger → **Refrig Main Failure** → system falls to **Recirculation**

**Verifications**:
1. Mode enters **Recirculation**
2. Fan ON at 50%, Doors CLOSED, Climacell ON, Heat OFF
3. Green ON, Yellow ON, Red OFF
4. **Humid3 auto test**: set plenum humidity to 80% (15% below 95% SP) → Humid3 pump should turn ON
5. **Humid3 proof test**: open EX1 DI port 0 → humid3 pump OFF **and** head OFF → restore + ClearAlarm
6. Set plenum humidity to 100% → Humid3 pump should turn OFF (above SP)
7. Restore all 6 stage DIs + ClearAlarm

**Duration**: ~15–25 minutes (10+ min waiting for stages to build)

---

### Phase 5: Heating Mode + Proofs
**Goal**: Drop temperature well below setpoint to trigger heating, verify heat and cavity heat.

**Sensor Setup**:
- Plenum: SP − 15°F (e.g., 31°F)
- Outside: 20°F (cold, also triggers cavity heat at < 30°F)

**Verifications**:
1. Mode enters **Heating** within 120s
2. Fan ON at 25% (uses cooling speed), Doors CLOSED, Climacell ON, Heat ON
3. Green ON, Yellow OFF, Red OFF
4. **Cavity heat** is ON (outside 20°F < 30°F threshold)
5. **Heat proof test**: open EX1 DI port 2 → heat stops → restore + ClearAlarm
6. **Cavity heat proof test**: open EX1 DI port 3 → cavity heat stops → restore + ClearAlarm
7. Raise plenum to SP → heat turns OFF

**Duration**: ~5 minutes

---

### Phase 6: CO2 Purge + Recovery
**Goal**: Trigger CO2 purge and verify it overrides current mode, then returns to previous state.

**Sensor Setup**:
- First establish **Refrigeration** baseline (plenum SP + 4°F, outside 60°F) via Stop/Start cycle
- Then inject CO2 = 2200 ppm, outside = 45°F (within 40–50°F window)

> **Important**: CO2 purge does **not** activate from Cooling mode. It must be tested from Refrigeration.

**Verifications**:
1. Refrigeration confirmed, record pre-purge mode, fan speed, and cool output
2. Mode enters **CO2 Purge** within 120s
3. Fan at 100%, Doors at 100%
4. Drop CO2 back to 400 ppm → purge should end within 7 minutes (5 min duration + margin)
5. After purge: mode returns to previous, fan speed restores to approximate pre-purge value

**Duration**: ~8–10 minutes

---

### Phase 7: Aux1 Door Heat Tape + Proof
**Goal**: Verify Aux1 activates based on outside temperature threshold.

**Sensor Setup**:
- Plenum: SP + 3°F (in a running mode)
- Outside: 20°F (below 28°F threshold)

**Verifications**:
1. Aux1 turns ON within 60s at outside 20°F
2. **Aux1 proof test**: open EX1 DI port 4 → Aux1 stops → restore + ClearAlarm
3. Raise outside to 35°F (above 28°F) → Aux1 turns OFF

**Duration**: ~3 minutes

---

### Phase 8: System Failure Tests
**Goal**: Test the three system-failure-level proofs: fan proof (1 min delay), airflow (immediate), and low temp (immediate).

**Sensor Setup**:
- Establish Cooling mode (plenum SP + 3°F, outside 30°F)

**Sub-tests**:

#### 8a. Fan Proof (1-minute delay)
1. Verify fan is ON
2. Open main DI port 2 (fan proof fails)
3. Wait up to 60s + 10s shutdown → **System Failure** mode, Fan OFF, Green OFF
4. Restore DI + ClearAlarm → recover to Cooling

#### 8b. Airflow Restriction (IMMEDIATE)
1. Verify fan is ON
2. Open main DI port 8 (airflow fail)
3. Should enter **Failure** within 20s (no 1-min delay)
4. Fan OFF, Red ON, Green OFF, Yellow OFF
5. Restore DI + ClearAlarm → recover

#### 8c. Low Temp (IMMEDIATE)
1. Open main DI port 9 (low temp fail)
2. Should enter **Failure** within 20s
3. Restore DI + ClearAlarm → recover

**Duration**: ~5–7 minutes (includes 1-min wait for fan proof + recovery periods)

---

### Phase 9: Power Failure + Recovery
**Goal**: Simulate a power failure and verify alarm handling.

**Sensor Setup**:
- Establish Cooling mode

**Flow**:
1. **Close** power DI (main port 0) — inverted polarity: closed = fault
2. Wait up to 90s for Power alarm
3. **Open** power DI (restore power)
4. Verify alarm persists (requires explicit clear)
5. Send **ClearAlarm** via bridge POST
6. Verify power alarm is gone

**Important**: Power input is **inverted** — the normal state is OPEN, and closing it signals a fault.

**Duration**: ~3 minutes

---

### Phase 10: Remote Standby
**Goal**: Trigger Remote Standby via digital input and verify the system goes into standby.

**Flow**:
1. Establish Cooling mode
2. **Close** Remote Standby DI (main port 1) — inverted: closed = active
3. Wait for **Remote Standby** mode (up to 60s)
4. Verify: Fan OFF, Doors CLOSED, Climacell OFF, Heat OFF
5. Green OFF, **Yellow ON**, Red OFF
6. Open Remote Standby DI → system recovers to running mode

**Duration**: ~3 minutes

---

### Phase 11: Fan Manual Mode
**Goal**: Switch fan to manual and verify all 3 indicator lights are solid ON.

**Flow**:
1. Send `POST /api/switches` with `fan: 'manual'`
2. Wait for **Fan Manual** mode
3. Verify: Fan ON at 100%
4. **All 3 lights solid ON**: Green ✓, Yellow ✓, Red ✓ (this is normal for Fan Manual, NOT an alarm)
5. Confirm lights remain stable (not flashing)
6. Switch fan back to `auto`

**Duration**: ~1 minute

---

### Phase 12: Shutdown
**Goal**: Verify the system shuts down cleanly when the Start/Stop switch is set to Stop.

**Flow**:
1. Send `POST /api/switches` with `startStop: 'shutdown'`
2. Wait for **Shutdown** mode
3. Verify: Fan OFF, Doors CLOSED, Climacell OFF, Heat OFF
4. Green OFF, Yellow OFF, **Red solid ON**
5. Switch back to `startStop: 'start'` → system resumes

**Duration**: ~2 minutes

---

### Phase 13: Refrig Stage Sequencing (Detailed)
**Goal**: Verify stages turn ON in order 1→2→3→4→5→6 and OFF in reverse order 6→5→4→3→2→1.

**Sensor Setup**:
- Plenum: SP + 4°F, Outside: 60°F (Refrig conditions)

**Entry Method**: Stop/Start cycle to enter Refrigeration directly.

**Flow**:
1. Enter Refrigeration via Stop/Start cycle
2. **ON sequencing**: Monitor stages as refrig PID output builds over ~12 min
   - Stage 1 should come on first (at 20%), then 2 (at 30%), etc.
   - Log the time and order of each stage activation
   - Verify order is strictly 1→2→3→4→5→6
3. **OFF sequencing**: Drop plenum to SP − 2°F so output drops
   - Stages should turn off in reverse: 6 first, then 5, 4, 3, 2, 1
   - Log the time and order of each stage deactivation
   - Verify order is strictly 6→5→4→3→2→1

**Duration**: ~15–20 minutes (PID ramp up + ramp down)

---

### Phase 14: Standby Mode
**Goal**: Verify the system enters Standby when temperature is satisfied and no action is needed.

**Sensor Setup**:
- Plenum: SP − 1°F (temperature is satisfied, no cooling needed)
- Outside: SP + 15°F (warm, so cooling is unavailable)

**Verifications**:
1. Mode enters **Standby** within 180s
2. Fan OFF, Door switch **AUTO** (`eq[29]=1`), CPLD close pulse **ON** (from `getCpldDoorState()`), Climacell OFF, Heat OFF
3. Green OFF, **Yellow ON**, Red OFF

> **Note**: In Standby, the firmware's Timer ISR holds `OutputOn(EQ_PULSEDOOR_CLOSE)` when the door is at its min position (fully closed). The EquipStatus `eq[29]` reports the switch position (AUTO), not CPLD power. The `eq[30]` movement direction is `0` (stopped) because `PulseDoorMove=0`. The test verifies the CPLD close output directly via the panel’s `/api/status` → `cpldOutputs.main` bit 0.

**Duration**: ~3 minutes

---

## Expected Mode Outputs Matrix

| Mode | Fan | Fan% | DoorSw (29) | DoorDir (30) | CPLD Close | CPLD Open | Climacell | Heat | Refrig | Green | Yellow | Red |
|------|-----|------|-------------|--------------|------------|-----------|-----------|------|--------|-------|--------|-----|
| Cooling | ON | 25 | AUTO (1) | 0/1/2 (PID) | varies | varies | ON | OFF | OFF | ON | OFF | OFF |
| Refrig | ON | 100 | AUTO (1) | 0 | **ON** | OFF | ON | OFF | staged | ON | ON | OFF |
| Recirc | ON | 50 | AUTO (1) | 0 | **ON** | OFF | ON | OFF | OFF | ON | ON | OFF |
| Heating | ON | 25 | AUTO (1) | 0 | **ON** | OFF | ON | ON | OFF | ON | OFF | OFF |
| CO2 Purge | ON | 100 | AUTO (1) | 2 | OFF | **ON** | as-was | as-was | as-was | as-was | as-was | as-was |
| Defrost | ON | 100 | AUTO (1) | 0 | **ON** | OFF | ON | OFF | OFF | ON | ON | OFF |
| Standby | OFF | 0 | AUTO (1) | 0 | **ON** | OFF | OFF | OFF | OFF | OFF | ON | OFF |
| Fan Manual | ON | 100 | — | — | — | — | OFF | OFF | OFF | ON | ON | ON |
| Rem Standby | OFF | 0 | AUTO (1) | 0 | **ON** | OFF | OFF | OFF | OFF | OFF | ON | OFF |
| Shutdown | OFF | 0 | AUTO (1) | 0 | **ON** | OFF | OFF | OFF | OFF | OFF | OFF | ON |
| Failure | OFF | 0 | AUTO (1) | 0 | **ON** | OFF | OFF | OFF | OFF | OFF | OFF | RED |

> **Column legend**: DoorSw = `eq[29]` = `SW_FRESHAIR` switch position (firmware EquipStatus). DoorDir = `eq[30]` = `PulseDoorMove` direction (0=stopped at target, 1=closing, 2=opening). CPLD Close/Open = physical CPLD shift register outputs read via panel `/api/status` → `cpldOutputs.main` (bit 0 = close, bit 1 = open, bit 2 = power). These are NOT in EquipStatus.

---

## DI Port Reference (for proof testing)

> **Important**: The responder API uses **0-indexed** port numbers. The firmware labels DI ports starting at 1 (DI1, DI2, etc.), so firmware DI4 = API port 3, DI5 = API port 4, etc. The ports listed below are the **API (0-indexed)** values used in the test code.

### Main Board (API ports 0–9)
| API Port | Firmware DI | Function | Polarity | Failure Type |
|----------|-------------|----------|----------|-------------|
| 0 | DI1 | Power | **Inverted** (closed = fault) | Alarm (1 min) |
| 1 | DI2 | Remote Standby | **Inverted** (closed = active) | Mode change |
| 2 | DI3 | Fan Proof | Normal (open = fault) | **System Failure** (1 min) |
| 3 | DI4 | Climacell Proof | Normal (open = fault) | Equipment stop (1 min) |
| 4 | DI5 | Humid1 Proof | Normal (open = fault) | Equipment stop (1 min) |
| 5 | DI6 | — | — | — |
| 6 | DI7 | Humid2 Proof | Normal (open = fault) | Equipment stop (1 min) |
| 7 | DI8 | — | — | — |
| 8 | DI9 | Airflow Restriction | Normal (open = fault) | **System Failure (IMMEDIATE)** |
| 9 | DI10 | Low Temp | Normal (open = fault) | **System Failure (IMMEDIATE)** |

### Expansion Board 1 (EX1, API ports 0–7)
| API Port | Function | Failure Type |
|----------|----------|-------------|
| 0 | Humid3 Head Proof | Equipment stop (1 min) |
| 1 | — | — |
| 2 | Heat Proof | Equipment stop (1 min) |
| 3 | Cavity Heat Proof | Equipment stop (1 min) |
| 4 | Aux1 Proof | Equipment stop (1 min) |
| 5 | Aux2 Proof | Equipment stop (1 min) |
| 6 | Bay Lights 1 | (skipped) |
| 7 | Bay Lights 2 | (skipped) |

### Expansion Board 2 (EX2, API ports 0–7)
| API Port | Function | Failure Type |
|----------|----------|-------------|
| 0 | Refrig Stage 1 Proof | Equipment stop (1 min) |
| 1 | Refrig Stage 2 Proof | Equipment stop (1 min) |
| 2 | Refrig Stage 3 Proof | Equipment stop (1 min) |
| 3 | Refrig Stage 4 Proof | Equipment stop (1 min) |
| 4 | Refrig Stage 5 Proof | Equipment stop (1 min) |
| 5 | Refrig Stage 6 Proof | Equipment stop (1 min) |
| 6 | Defrost 1 Proof | Equipment stop (1 min) |
| 7 | Defrost 2 Proof | Equipment stop (1 min) |

---

## Equipment Status Index Reference

Key indices in the `EquipStatusData` array used for output verification:

| Index | Equipment | Checked In |
|-------|-----------|-----------|
| 2 | Fan output | All phases |
| 5 | Climacell output | Phases 1–5 |
| 7 | Burner output | Phase 5 |
| 10 | Humid1 head | Phases 1, 2 |
| 13 | Humid2 head | Phase 2 |
| 17–22 | Refrig stages 1–6 | Phases 2, 3, 13 |
| 25 | Defrost 1 | Phase 3 |
| 26 | Defrost 2 | Phase 3 |
| 28 | Heat output | Phase 5 |
| 29 | Door switch (SW_FRESHAIR: 0=off, 1=auto, 2=manual) | Phases 1–5, 10, 12, 14 |
| 30 | Door movement direction (0=stopped, 1=closing, 2=opening) | (logged in snap) |
| 32 | Cavity heat output | Phase 5 |
| 33 | Green light | All phases |
| 34 | Yellow light | All phases |
| 35 | Red light | All phases |
| 47 | Aux1 output | Phase 7 |
| 77 | Humid3 head | Phase 4 |

> **Pulsed door outputs** (`EQ_PULSEDOOR_CLOSE`, `EQ_PULSEDOOR_OPEN`, `EQ_PULSEDOOR_POWER`) are **not** in the EquipStatus wire format. They are physical CPLD shift register outputs. The test reads them via `getCpldDoorState()` which calls the panel’s `/api/status` → `cpldOutputs.main` (bit 0 = close, bit 1 = open, bit 2 = power).

---

## Troubleshooting

| Problem | Likely Cause | Fix |
|---------|-------------|-----|
| Phase 1 doesn't enter Cooling | Plenum not above SP, or run clock wrong | Verify SP = 46°F, sensors applied |
| Phase 2 enters Cooling instead of Refrig | Didn't use Stop/Start | Test uses shutdown→start cycle to skip cooling ramp |
| Refrig stages never activate | PID takes ~10 min to build | Wait longer, or increase plenum temp |
| Phase 3 times out (no defrost) | Haven't been in refrig for 1 hour | Run Phase 2 first, then wait |
| Phase 4 can't reach Recirc | Run clock set to Cooling only | Change run clock to Refrig or Recirc |
| Alarms won't clear | ClearAlarm not reaching ARM | Check bridge is running on port 3001 |
| Proofs don't trigger alarm | Output wasn't active when DI was opened | Proofs only alarm when equipment is running |
| CO2 purge doesn't trigger | Not in Refrig, or outside temp not in 40–50°F window | CO2 purge only activates from Refrigeration mode, not Cooling |
| Standby never reached | System keeps cycling to cooling | Set outside temp very warm (SP + 15°F) |
