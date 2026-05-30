# Triton / GRC port plan — read-only discovery

> Source tree: `f:\Constellation\GRC\ARM_Refrigeration\` (TI Tiva TM4C123, FreeRTOS).
> All citations are `path:line` against that tree. **No GRC source is modified by this document.**
> Target: TypeScript orbit-board simulator at `orbit-simulator/src/orbitSimulator.ts`,
> eventual production C on TI Sitara AM2432.
>
> The GRC firmware controls **up to three compressor circuits** in one box — Triton =
> one of them. Throughout this doc, "compressor *N*" / `PHYSICALCOMPRESSOR` indexes
> the circuit; for a single-Triton sim, you only need `COMPRESSOR1`.

---

## 1. I/O channel map

GRC routes all DI/DO through a CPLD-fronted serial-shift chain (main board + up to two
expansion boards). `ThreadSerialShift` clocks `BOARD_COUNT=3` boards every 10 ms,
debounces inputs across two consecutive identical reads, and pushes outputs the same
cycle.

- Per-circuit equipment ID space (`COMPRESSOR_EQ`, 27 entries) is replicated 3× into
  a flat `EQUIPMENT_IO` enum (`EQ_COMPRESSOR_1` … `EQ_SUCTION_PRESSURE_3` + 7 system
  inputs):
  [GRC/ARM_Refrigeration/Application/SerialShift.h](../GRC/ARM_Refrigeration/Application/SerialShift.h#L52-L96) (per-circuit `COMPRESSOR_EQ`),
  [SerialShift.h:97-180](../GRC/ARM_Refrigeration/Application/SerialShift.h#L97-L180) (flat `EQUIPMENT_IO`).
- Per-equipment IO binding is a `IO_CONFIG{Name,Renamable,Enabled,Input,Output,IO}`
  in `Settings.EquipIo[EQ_TOTAL_IO+2]`, and the bit on the shift register is
  resolved at runtime via `Settings.EquipIo[eq].Input/Output`:
  [SerialShift.h:354-378](../GRC/ARM_Refrigeration/Application/SerialShift.h#L354-L378),
  [SerialShift.c:69-116](../GRC/ARM_Refrigeration/Application/SerialShift.c#L69-L116) (`CheckInputs`).
- Hardware-fixed switches on the main panel: `EQ_COMPRESSOR_AUTO_n` = panel switch 7,
  `EQ_COMPRESSOR_PUMPDOWN_n` = panel switch 8 (set in `VerifyBoardIo()`):
  [SerialShift.c:531-552](../GRC/ARM_Refrigeration/Application/SerialShift.c#L531-L552).
- System DIs always live on main board: `EQ_POWER_MONITOR` (phase/power) =
  `SS_MAIN_POWER_V2` (bit 23), `EQ_AIRFLOW` = `SS_MAIN_AIRFLOW_V2` (bit 22):
  [SerialShift.h:225-235](../GRC/ARM_Refrigeration/Application/SerialShift.h#L225-L235),
  [SerialShift.c:357](../GRC/ARM_Refrigeration/Application/SerialShift.c#L357).

### Per-circuit digital inputs (programmable, default unset)

| `COMPRESSOR_EQ`           | Meaning                                 | UI input #                   |
|---------------------------|-----------------------------------------|------------------------------|
| `EQ_CRANKCASE_HEATER`     | Crankcase prove (heater current sense)  | DI? (configurable)           |
| `EQ_CONDENSER_OVERLOAD`   | Condenser-fan overload aux (Aux 1)      | DI?                          |
| `EQ_COMPRESSOR_OVERLOAD`  | Compressor overload (Aux 2)             | DI?                          |
| `EQ_OIL_FAIL`             | Oil-pressure failsafe (Aux 3)           | DI?                          |
| `EQ_HH_PRESSURE`          | High-head (HP) cutout switch (Aux 4)    | DI?                          |
| `EQ_SUCTION_PRESSURE`     | Low-suction (LP) cutout switch (Aux 5)  | DI?                          |

Defined: [SerialShift.h:78-86](../GRC/ARM_Refrigeration/Application/SerialShift.h#L78-L86).
Polled in `CheckCompressorInputs()`:
[Compressor.c:1727-1740](../GRC/ARM_Refrigeration/Application/Compressor.c#L1727-L1740).

### Authoritative GRC main-board terminal pinout

Source: Gellert / Demar Lott wiring drawing **"GRC Inputs"** dated 2019-02-05
(image referenced from operator manual; not in source tree). Each terminal looks
for a **closed relay contact** that *proves* the device is healthy/running; an
**open** contact = alarm/fail. This is the production pinout that the AM2432
Triton orbit board should replicate so field wiring is unchanged.

| Terminal | Signal              | `EQUIPMENT_IO` enum         | Healthy = | Notes                                       |
|----------|---------------------|-----------------------------|-----------|---------------------------------------------|
| DI_LT1   | POWER MONITOR       | `EQ_POWER_MONITOR`          | closed    | Phase / 3-φ rotation watchdog (system DI)   |
| DI_AF1   | NOT USED            | (`EQ_AIRFLOW` slot — unused for refrig) | —      | Reserved by `VerifyBoardIo()`               |
| DI_1     | CRANKCASE HEATER    | `EQ_CRANKCASE_HEATER`       | current sensed | Inverted prove: closed while comp is OFF means heater is energized; ⇒ "run-prove" mechanism |
| DI_2     | CONDENSER OVERLOAD  | `EQ_CONDENSER_OVERLOAD`     | closed    | Aux 1                                       |
| DI_3     | COMPRESSOR OVERLOAD | `EQ_COMPRESSOR_OVERLOAD`    | closed    | Aux 2                                       |
| DI_4     | OIL PRESSURE FAILURE | `EQ_OIL_FAIL`              | closed    | Aux 3 — discrete switch, NOT derived from analog oil-P |
| DI_5     | HIGH HEAD PRESSURE  | `EQ_HH_PRESSURE`            | closed    | Aux 4 — HP cutout switch                    |
| DI_6     | SUCTION PRESSURE    | `EQ_SUCTION_PRESSURE`       | closed    | Aux 5 — **LP cutout switch** (NOT a "run-prove" input — that mechanism uses DI_1) |
| DI_7     | AUTO RUN            | `EQ_COMPRESSOR_AUTO_n`      | closed    | Panel auto-permissive (hardware-fixed in `VerifyBoardIo()`) |
| DI_8     | PUMP DOWN           | `EQ_COMPRESSOR_PUMPDOWN_n`  | closed    | Panel pumpdown switch (hardware-fixed)      |

10 input terminals total, 9 active.

### AM2432 Triton orbit — hardware spec (production target)

The Triton orbit board is built on the **same hardware skeleton as the storage
orbit**: **10 DI, 10 DO, 2 AO**. The 2 AOs are software-configurable per-channel
to drive any one of: **EEV** (4-20 mA), **compressor VFD** speed reference, or
**condenser-fan VFD** speed reference. The orbit's per-channel "AO function"
setting selects the source.

**DI map (10 channels):** identical to the GRC main-board pinout above.
**DI_AF1 is dropped as an airflow input** (per operator: airflow was always
jumpered out in legacy installs); the slot stays in the table reserved for a
future user-configurable input.

**DO budget vs. GRC superset.** GRC's per-circuit DO superset adds up to 18+
signals (`EQ_COMPRESSOR`, `EQ_LLS`, 6× cond fans, 4× unloaders, 2× aux,
`EQ_DEFROST`, `EQ_COMPRESSOR_ERROR`, 3× screw-comp pulses). The Triton orbit
only ships **10 DOs**, so the operator selects which 10 GRC equipment slots map
to physical channels via the same `EquipIo[eq].Output` mechanism (per §1 above).

> **Architectural shift vs. GRC.** GRC was a single controller that scaled
> *vertically* via expansion boards — one MCU drove **up to 3 compressors**
> (9 DI / 8 DO base + expansion DO/DI) sharing one settings struct. Nova/Triton
> scales *horizontally*: **one Triton orbit per compressor**. There is no
> expansion-board concept; if you need a second compressor, you add a second
> Triton orbit. This is why each Triton orbit only needs 10 DO and not the
> GRC superset — its job is exactly one circuit.

Recommended default for an air-cooled scroll/recip Triton (single-circuit):

| Orbit DO | Recommended default          | Equipment enum               |
|----------|------------------------------|------------------------------|
| DO_1     | Compressor contactor         | `EQ_COMPRESSOR`              |
| DO_2     | Liquid-line solenoid (LLS)   | `EQ_LLS`                     |
| DO_3..6  | Condenser fans 1-4           | `EQ_CONDENSER_FAN1..4`       |
| DO_7..8  | Unloaders 1-2                | `EQ_UNLOADER1..2`            |
| DO_9     | Defrost solenoid / heater    | `EQ_DEFROST`                 |
| DO_10    | Compressor alarm output      | `EQ_COMPRESSOR_ERROR`        |

**No crankcase-heater DO.** Per GRC mechanical design, the crankcase heater is
wired through the compressor contactor's NC contact — energized whenever the
compressor is off, no firmware control needed. **DI_1 still senses heater
current** so the prove-crankcase logic is unchanged. This frees a DO slot
relative to a naïve port.

The remaining 10 DOs are operator-reassignable. Common swaps:
- Add `EQ_CONDENSER_FAN5..6` if the unit has more than 4 fans (drop unloaders
  or alarm output).
- Swap `EQ_DEFROST` for `EQ_AUXOUTPUT1..2` on units with no defrost.
- Swap unloaders for `EQ_SCREWCOMP_START/LOAD/UNLOAD` on screw compressors.
- Add a second `EQ_DEFROST`-driven heater output for electric-defrost coils.

**AO budget vs. GRC superset.** GRC reserves 9 PWM channels (3× condenser, 3×
compressor VFD, 3× EXV — see `PWM_EQUIPMENT` in §1). The Triton orbit ships
**2 AOs**, each independently set to one of `{EEV, COMPRESSOR_VFD,
CONDENSER_FAN_VFD}`. A single-circuit Triton with EEV + condenser-fan VFD uses
both. A fixed-speed unit with EEV uses one and leaves the second unconfigured.

### Per-circuit digital outputs

| `COMPRESSOR_EQ`            | Meaning                          | Notes |
|----------------------------|----------------------------------|-------|
| `EQ_COMPRESSOR`            | Compressor contactor             | first to turn on, last to turn off |
| `EQ_LLS`                   | Liquid-line solenoid             | gates demand; off = pumpdown start |
| `EQ_CONDENSER_FAN1..6`     | Condenser fans, staged           | up to 6 per circuit |
| `EQ_UNLOADER1..4`          | Compressor unloaders             | normally-opened or normally-closed (`Settings.Compressor.UnloaderNormal`) |
| `EQ_AUXOUTPUT1..2`         | User-defined demand-staged aux   | turns on like LLS |
| `EQ_DEFROST`               | Defrost solenoid (hot-gas / electric) | |
| `EQ_COMPRESSOR_ERROR`      | Per-circuit alarm output         | |
| `EQ_SCREWCOMP_START`       | Screw start solenoid             | only if pulse-screw enabled |
| `EQ_SCREWCOMP_LOAD`        | Screw load solenoid              | half-second pulses |
| `EQ_SCREWCOMP_UNLOAD`      | Screw unload solenoid            | half-second pulses |

[SerialShift.h:54-72](../GRC/ARM_Refrigeration/Application/SerialShift.h#L54-L72).
Drive via `OutputOn(eq)/OutputOff(eq)` + `SetOutput()` shifted once per
`SerialShiftTimer` tick (~333 ms — see §10):
[Timer.c:260-283](../GRC/ARM_Refrigeration/Application/Timer.c#L260-L283).

### PWM (4-20 mA) outputs

`PWM_EQUIPMENT` enum is condenser × 3, compressor × 3 (variable speed),
expansion × 3 (EXV):
[PWM.h:46-57](../GRC/ARM_Refrigeration/Application/PWM.h#L46-L57).
The Tiva PWM peripheral is loaded with raw counter values
(`PWM_MIN_VALUE=55, PWM_MAX_VALUE=277, PWM_PERIOD=375, PWM_INC_VALUE=10` rate-limit
per cycle): [PWM.h:39-43](../GRC/ARM_Refrigeration/Application/PWM.h#L39-L43).
Conversion 0-100 % ↔ counter is in
[Helper.c:140-164](../GRC/ARM_Refrigeration/Application/Helper.c#L140-L164).

### Analog boards (8 max, 6 used by GRC) — RS-485 bus

`Analog_Input.h` declares 8 board slots, 4 sensors per board. Boards 0/2/4 are
**Temperature**, 1/3/5 are **Pressure** — one pair per circuit:
[Analog_Input.h:28-66](../GRC/ARM_Refrigeration/Application/Analog_Input.h#L28-L66).

| Board | Sensor index            | Meaning                | Scaling |
|-------|-------------------------|------------------------|---------|
| `COMPRESSORn_TEMP_BOARD` (n*2)   | `SENSOR_SUCTION_TEMP`     | suction line temp °F/°C | NTC table lookup, `ConvertToTemp()` ([Analog_Input.c:113-158](../GRC/ARM_Refrigeration/Application/Analog_Input.c#L113-L158)) |
| "  | `SENSOR_DISCHARGE_TEMP`   | discharge line temp     | "  |
| "  | `SENSOR_LLS_TEMP`         | liquid-line solenoid temp (subcool) | " |
| "  | `SENSOR_OUTSIDE_TEMP` (board 0 only) | ambient °F/°C  | " |
| `COMPRESSORn_PRESSURE_BOARD` (n*2+1) | `SENSOR_SUCTION_PRESSURE` | psig | linear, `(ScaledADC-180)/720 * Transducer` PSI ([Analog_Input.c:210-225](../GRC/ARM_Refrigeration/Application/Analog_Input.c#L210-L225)) |
| "  | `SENSOR_DISCHARGE_PRESSURE`| psig | "  |
| "  | `SENSOR_OIL_PRESSURE`     | psig | "  |
| "  | `SENSOR_REFRIG_DEMAND` (board 1 only) | 0-100 % | linear `(ScaledADC-180)/720 * 100` ([Analog_Input.c:180-194](../GRC/ARM_Refrigeration/Application/Analog_Input.c#L180-L194)) |

Aliases used by control: `OutsideTemp` and `RefrigDemand` are pointers into
`Settings.AnalogBoard[…].Sensor[…].Value`:
[Analog_Input.c:48-49](../GRC/ARM_Refrigeration/Application/Analog_Input.c#L48-L49).

`PressureTransducer` is per-circuit: 500 / 650 / 750 PSI full scale at 20 mA
([Analog_Input.h:80-86](../GRC/ARM_Refrigeration/Application/Analog_Input.h#L80-L86)). The
ADC is multiplied ×16 on the analog board for resolution, so `ScaledADC =
ADC/16`. 4 mA = 180 counts, 20 mA = 900 counts. Saturation above 900 returns
**transducer max** (per Gellert, "common"):
[Analog_Input.c:222](../GRC/ARM_Refrigeration/Application/Analog_Input.c#L222).
`SENSOR_VAL_UNDEFINED = 0x7FFF` is the "no reading" sentinel everywhere.

> **Pinout corrections (authoritative — Demar Lott 2019 wiring drawing):**
>
> - GRC has no separate "phase monitor" enum at the firmware level — it lands on
>   `EQ_POWER_MONITOR` wired to terminal **DI_LT1** (a single contact).
> - **"Run-prove" is the crankcase-heater current input** (`EQ_CRANKCASE_HEATER`,
>   terminal **DI_1**), used inverted: when the contactor is off, that input must
>   read true — otherwise the circuit latches `WARN_PROVE_CRANKCASE` after the
>   crankcase prove window. There is **no separate run-prove DI**.
> - **DI_6 is the low-suction (LP) cutout switch** (`EQ_SUCTION_PRESSURE`), NOT
>   any kind of run-prove input. An earlier draft of this document had this
>   wrong — it has been corrected in §1.
> - The auto/pumpdown permissives come from front-panel switches
>   (`EQ_COMPRESSOR_AUTO_n` = DI_7, `EQ_COMPRESSOR_PUMPDOWN_n` = DI_8), wired in
>   `VerifyBoardIo()` and not re-mappable.
> - **Pumpdown behavior (operator-confirmed):** the LLS (`EQ_LLS`) closes
>   immediately on stop request; the compressor continues running until the
>   analog suction pressure transducer (Pressure board ch 0) drops below
>   `LowSuctionPressureOff`, at which point the contactor drops out. There is no
>   software bypass of the DI_6 LP switch during pumpdown — pumpdown timing
>   relies on the persistent-alarm timer to suppress nuisance LP trips while
>   suction is being drawn down.

---

## 2. Settings struct

Top-level: [`SYSTEM_SETTINGS`](../GRC/ARM_Refrigeration/Application/Settings.h#L80-L132)
in `Settings.h:80-132`. CRC-protected, persisted to internal flash + SD card via
`SaveSettings(ACTIVE|SAVED)` ([Settings.h:48 forward decl](../GRC/ARM_Refrigeration/Application/Settings.h#L48)).

### Per-circuit refrigeration settings (`REFRIGERATION_SETTINGS`)

[Settings.h:55-77](../GRC/ARM_Refrigeration/Application/Settings.h#L55-L77):

```
REFRIGERANT  Refrigerant;             // R_22, R_410A, R_407C, R_134A, R_404A, R_507, R_407A
short        LowSuctionPressureOn;    // psi: cut-IN  (compressor turns on when suction >= this)
short        LowSuctionPressureOff;   // psi: cut-OUT (compressor turns off when suction <= this)
short        DischargeHigh;           // psi: high discharge alarm
char         OilPressureLow;          // psi: (oil - suction) low limit
short        LowSuctionCutout;        // psi: hard low cutout (overrides pumpdown, latches off)
LOADSTAGE    HighHeadPressure[4];     // {load,unload} per unloader: discharge psi to drop/add unloader
LOADSTAGE    LowSuctionOverride[4];   // {load,unload} per unloader: suction psi to force unload at low side
short        TerminationPressure;     // defrost: discharge psi to terminate
char         TerminationTemperature;  // defrost: discharge temp to terminate
short        ControlPoint;            // condenser fan target (FIXED_HEAD)
short        MinimumSetpoint;         // floor for floating-head target
STAGE        FanDifferential[6];      // {on,off} differential per fan stage (psi above target)
PRESSURE_TRANSDUCER PressureTransducer; // 500/650/750
```

### `COMPRESSOR_CTRL` (shared across all circuits)

[Compressor.h:96-114](../GRC/ARM_Refrigeration/Application/Compressor.h#L96-L114):

```
short ShortCycle;          // seconds: anti-short-cycle (default 300)
short RotateCompressor;    // hours: lead rotation
signed char LowAmbientTemperature;  // °F/°C cutout (default 40 °F)
char  PowerFailTimeout;    // minutes — power loss > this forces full crankcase prove (default 30)
char  CrankcaseRunTimer;   // hours of crankcase warmup before allowing start (default 8)
signed char SuperHeatLow;            // °F/°C low-superheat alarm (default 5)
signed char SuperHeatWindowHigh;     // (default 30)
signed char SuperHeatWindowLow;      // (default 10)
short SuperHeatSetPoint;   // EXV target (default 15)
char  VariableStart;       // % min variable-speed start (default 50)
NORMAL_STATE UnloaderNormal;         // NORMALLY_OPENED / NORMALLY_CLOSED
char  ContinuousPumpdown;
PULSE_TYPE Pulse;          // screw-comp half-second pulse params
COMPRESSOR_STAGE Stage[15];          // demand→equipment staging table
COMPRESSOR_OPERATION Op[3];          // per-circuit refrigerant / runtime / RemoteStatus
```

`COMPRESSOR_STAGE`:
[Compressor.h:55-61](../GRC/ARM_Refrigeration/Application/Compressor.h#L55-L61) —
`{Ordinal, On, Off, Equipment}`. Each stage maps a 0-100 demand window to a
`COMPRESSOR_EQ`. Defaults (single-circuit setup) seeded by
[Settings.c:236-238](../GRC/ARM_Refrigeration/Application/Settings.c#L236-L238):
`Stage1=LLS@C1 on=20/off=10`, `Stage2=Unloader1@C1 on=30/off=20`,
`Stage3=Unloader2@C1 on=40/off=30`.

### Defrost (`DEFROST_CTRL`)

[DefrostCtrl.h:56-66](../GRC/ARM_Refrigeration/Application/DefrostCtrl.h#L56-L66):

```
short VariableTimer;        // suction-trend window, minutes (default 30)
short OverrideTimer;        // hours since last defrost → forced defrost (default 6)
char  SuctionDifferential;  // psi drop below trend to count as freezing (default 5)
char  InitiateTimer;        // minutes below-trend before initiating defrost (default 5)
char  TerminationTime;      // minutes max defrost (default 10)
TERMINATION_TYPE TerminationType; // PRESSURE | TEMPERATURE | TIME
char  DisableDefrost;
```

Defaults set in
[Settings.c:267-273](../GRC/ARM_Refrigeration/Application/Settings.c#L267-L273).

### EXV (`EXPANSIONVALVE_CTRL`)

[ExpansionValve.h:21-25](../GRC/ARM_Refrigeration/Application/ExpansionValve.h#L21-L25):
`{StartPercent, Warmup, PID_PARAMS PID}`. Defaults
`P=5, I=15, D=2, U=3 s, StartPercent=30, Warmup=3 min`
([Settings.c:218-224](../GRC/ARM_Refrigeration/Application/Settings.c#L218-L224)).

### Condenser-fan PID (`CONDENSERFAN_CTRL`)

[CondenserFans.h:51-55](../GRC/ARM_Refrigeration/Application/CondenserFans.h#L51-L55):
`{CondenserMode = FIXED_HEAD|FLOATING_HEAD|BALANCED_PID, PID_PARAMS PID}`.
Defaults `mode=FLOATING_HEAD, P=5, I=15, D=2, U=3 s`
([Settings.c:226-231](../GRC/ARM_Refrigeration/Application/Settings.c#L226-L231)).

### Failure modes (`FAILURE_MODE Failure[NUM_FAILURES]`)

`FAILURE_MODE = {char Mode, char Timer}`:
[SettingsTypes.h:155-158](../GRC/ARM_Refrigeration/Application/SettingsTypes.h#L155-L158).
`Mode ∈ {FM_NONE=0, FM_ALARM=1, FM_FAIL=2}`
([States.h:25-28](../GRC/ARM_Refrigeration/Application/States.h#L25-L28)).
`NUM_FAILURES = 7`: SUPERHEAT, SUPERHEATLOW, DISCHARGE, SUCTION, OIL, OUTSIDE_AIR,
POWER ([Warnings.h:31-40](../GRC/ARM_Refrigeration/Application/Warnings.h#L31-L40)).
`Timer` is the persistence in **minutes** before the alarm escalates / latches.
This is the **3-way ALARM_ONLY / SAFE_OFF / RUN_THROUGH selector** the user asked
about: GRC encodes RUN_THROUGH as `FM_NONE`, ALARM_ONLY as `FM_ALARM`, SAFE_OFF as
`FM_FAIL`. See `ManageAlarm()`:
[Failures.c:79-103](../GRC/ARM_Refrigeration/Application/Failures.c#L79-L103).

### Persisted vs. runtime

- **Persisted** (in `SYSTEM_SETTINGS` → flash + SD): everything in §2 above plus
  `Compressor.Op[].TotalRuntime`, `CondenserRotation.Unit[].TotalRuntime`,
  `LeadCompressor`, `LeadFan`, network nodes, passwords, analog board labels.
- **Runtime only** (zeroed at boot or recomputed): `IntervalTimer[]` and
  `AlarmTimer[]` in [Timer.h:158-160](../GRC/ARM_Refrigeration/Application/Timer.h#L158-L160),
  `SystemAlarm[]`, `FailureAlarm[]`
  ([Warnings.c:35-37](../GRC/ARM_Refrigeration/Application/Warnings.c#L35-L37)),
  `DefrostOn[]`, `Trend[]` ([DefrostCtrl.c:27-29](../GRC/ARM_Refrigeration/Application/DefrostCtrl.c#L27-L29)),
  `Pumpdown[]`, `ForcedUnload[][]`, `LowSuctionUnload[][]`, `Screw[]`
  ([Compressor.c:31-35](../GRC/ARM_Refrigeration/Application/Compressor.c#L31-L35)),
  `OrdinalToPhysicalMap[]`, `FanOrdinalToPhysicalMap[][]`.
- `DailyRuntime` fields are zeroed at 02:00 daily by `DailyJobs()`
  ([Timer.c:198-218](../GRC/ARM_Refrigeration/Application/Timer.c#L198-L218)).
- "Prove time remaining" survives reboots: `SYSTEM_TIME.prove[]` is written every
  `MT_SYSTEMSETTINGS` tick and restored in `CheckPowerInterruption()`
  ([States.c:84-127](../GRC/ARM_Refrigeration/Application/States.c#L84-L127)).

---

## 3. Compressor state machine

The state space `COMPRESSOR_STATUS` (UI-visible, returned from
`GetCompressorStatus()`):
[Compressor.h:27-44](../GRC/ARM_Refrigeration/Application/Compressor.h#L27-L44) —
`COMP_AUTO_STANDBY, COMP_AUTO_RUN, COMP_DEFROST, COMP_DEFROST_OVERRIDE,
COMP_PROVE, COMP_SWITCH_PUMPDOWN, COMP_REMOTE_PUMPDOWN, COMP_SWITCH_OFF,
COMP_REMOTE_OFF, COMP_SYSTEM_OFF, COMP_ERROR, COMP_STARTING, COMP_PUMPDOWN,
COMP_UNLOADING, COMP_DEFROST_MANUAL`.

The status is **derived from output bits + flags**, not stored. Decision tree:
[Compressor.c:96-176](../GRC/ARM_Refrigeration/Application/Compressor.c#L96-L176)
(`GetCompressorStatus`).

### Cut-in / cut-out (suction PSI hysteresis)

In `ManageCompressors()`
([Compressor.c:1025-1090](../GRC/ARM_Refrigeration/Application/Compressor.c#L1025-L1090)):

- **Cut-IN guard**: requires `suction >= LowSuctionPressureOn` AND not proving
  AND not defrosting AND short-cycle elapsed AND `CheckValidPressures()=true`
  AND demand >= LLS-stage `Off` AND LLS already on. Then `TurnOnCompressor()`
  ([Compressor.c:1058-1061](../GRC/ARM_Refrigeration/Application/Compressor.c#L1058-L1061),
  [Compressor.c:1836-1839](../GRC/ARM_Refrigeration/Application/Compressor.c#L1836-L1839)).
- **Cut-OUT**: `suction <= LowSuctionPressureOff` (or sensor undefined) →
  if LLS off OR `suction <= LowSuctionCutout` → hard `CompressorOff()`,
  otherwise `CompressorPumpdown()`
  ([Compressor.c:1067-1078](../GRC/ARM_Refrigeration/Application/Compressor.c#L1067-L1078)).

### Min-on / min-off

GRC has **no min-on**. Min-OFF is the **anti-short-cycle** timer
`Settings.Compressor.ShortCycle` (seconds, default 300):
- written when compressor turns off
  ([Compressor.c:389](../GRC/ARM_Refrigeration/Application/Compressor.c#L389)),
- gated on next cut-in
  ([Compressor.c:1052-1056](../GRC/ARM_Refrigeration/Application/Compressor.c#L1052-L1056),
  [Compressor.c:1838](../GRC/ARM_Refrigeration/Application/Compressor.c#L1838)),
- shared via `shortCycleMet[]` for the LLS stage in
  [Compressor.c:603-605](../GRC/ARM_Refrigeration/Application/Compressor.c#L603-L605).

### Prove time / crankcase warm-up

- On any of: cold boot with crankcase off, power outage longer than
  `PowerFailTimeout` minutes, or crankcase-heater input lost while compressor was
  off → `IntervalTimer[IT_PROVE_n] = CrankcaseRunTimer * T_MINS`.
  Sources:
  [States.c:113-119](../GRC/ARM_Refrigeration/Application/States.c#L113-L119),
  [States.c:193-198](../GRC/ARM_Refrigeration/Application/States.c#L193-L198),
  [Compressor.c:1720-1724](../GRC/ARM_Refrigeration/Application/Compressor.c#L1720-L1724).
- `UpdateCompressorProveTime()` decrements every minute; on hitting 0 it clears
  the failure: [Compressor.c:182-203](../GRC/ARM_Refrigeration/Application/Compressor.c#L182-L203).
- `IsCompressorProving()` blocks cut-in:
  [Compressor.c:1048](../GRC/ARM_Refrigeration/Application/Compressor.c#L1048).
- If **all** enabled compressors are simultaneously proving → system-wide
  `WARN_PROVING_ALL` and `STANDBY`
  ([Failures.c:151-178](../GRC/ARM_Refrigeration/Application/Failures.c#L151-L178)).

### Pumpdown sequence

`CompressorPumpdown(unit)` just sets `Pumpdown[physical]=1`
([Compressor.c:408-415](../GRC/ARM_Refrigeration/Application/Compressor.c#L408-L415)).
The actual orchestration is in `ManagePumpdown()` (called every loop) and
the LLS stage in `ManageEquipment()`
([Compressor.c:495-501](../GRC/ARM_Refrigeration/Application/Compressor.c#L495-L501)):
- LLS goes off, compressor stays on, condenser fans stay on, suction pressure
  bleeds down. When `suction <= LowSuctionPressureOff` (cut-out) the
  compressor finally drops in `ManageCompressors()`.
- LP-switch bypass window: GRC does **not** software-suppress the LP-switch DI
  during pumpdown; it relies on the `Settings.Failure[FAIL_SUCTION].Timer`
  (minutes) to delay alarm escalation. Hard short-cycle prevention comes from
  `ShortCycle` only.
- `ContinuousPumpdown` (boolean, per-system): when 1, allows pumpdown every
  cycle even when demand is still high; otherwise only when demand drops below
  the LLS stage's `On` threshold:
  [Compressor.c:1002-1011](../GRC/ARM_Refrigeration/Application/Compressor.c#L1002-L1011)
  (`CanPumpdown`).

### Multi-stage / unloader sequencing

Driven by the **demand stage table** (`Settings.Compressor.Stage[15]`).
`CtrlCompressor(demand)` walks all 15 entries; for each enabled stage
`ManageEquipment()` switches the equipment based on `demand >= On / demand <= Off`:
[Compressor.c:586-613](../GRC/ARM_Refrigeration/Application/Compressor.c#L586-L613),
[Compressor.c:457-555](../GRC/ARM_Refrigeration/Application/Compressor.c#L457-L555).
- Unloaders only enable when (a) compressor + LLS already on, (b) not in
  `Pumpdown`, (c) not in forced-unload (HP override) and not in
  low-suction-override:
  [Compressor.c:512-535](../GRC/ARM_Refrigeration/Application/Compressor.c#L512-L535).
- Sequencing delay between stages: there is **no explicit inter-stage timer** —
  ordering is enforced by demand crossing the per-stage `On` thresholds in the
  table (you author them ascending, e.g. LLS=20, U1=30, U2=40 …).
- `Settings.Compressor.UnloaderNormal` flips the polarity of the unloader
  output (NO vs NC).
  See [Compressor.h:16-19](../GRC/ARM_Refrigeration/Application/Compressor.h#L16-L19).

### `ManageUnloaders()` — discharge-pressure overrides

[Compressor.c:944-995](../GRC/ARM_Refrigeration/Application/Compressor.c#L944-L995):
- High discharge: if `discharge >= HighHeadPressure[i].unload` → `UnloaderOff()`
  (forced unload). Cleared when `discharge <= HighHeadPressure[i].load`.
- Low suction: if `suction <= LowSuctionOverride[i].unload` → output off and
  `LowSuctionUnload[][]=1`. Cleared at `suction >= LowSuctionOverride[i].load`.
  These are **per-unloader independent** thresholds.

### Hard-trip safeties (latched FM_FAIL)

`CompressorChk(physical)` in
[Failures.c:114-148](../GRC/ARM_Refrigeration/Application/Failures.c#L114-L148)
returns 1 on hard fail (HP, COMP_OL, COND_OL, OIL, LP, CRANKCASE, missing
pressure board, missing pressure sensor, low-suction superheat, power) — the
compressor stays in `COMP_ERROR`. Returns -1 specifically for high discharge so
condenser fans keep running.

Clearing: `ClearAlarmsByCompressor()`
([Timer.c:131-148](../GRC/ARM_Refrigeration/Application/Timer.c#L131-L148))
is invoked when the operator throws the panel switch to OFF, in
`CheckCompressorsRunPumpdown()`
([Compressor.c:248-258](../GRC/ARM_Refrigeration/Application/Compressor.c#L248-L258)).
**That is the only "reset" path** — there is no UI ack.

`Power` (phase) failure is handled by `CheckSystemStatus()`:
- `EQ_POWER_MONITOR` true for ≥3 min → `WARN_POWER` (alarm).
- ≥`PowerFailTimeout` min → all compressors `FailCompressor()`, system
  `ST_FAILURE`.
- ≥20 s → just shut everything down to STANDBY without latching.
  [States.c:140-205](../GRC/ARM_Refrigeration/Application/States.c#L140-L205).

---

## 4. Condenser-fan staging

Two parallel loops run every `ThreadSerialCom` tick (~400 ms — see §10):

### `StageCondenserFans()` — bang-bang on discharge psi

[CondenserFans.c:115-178](../GRC/ARM_Refrigeration/Application/CondenserFans.c#L115-L178):
- Skip if compressor disabled, in error, or in gas defrost
  (`IsDefrostWithCondenser()`).
- For each enabled fan in lead order: turn ON when
  `discharge >= TargetDischarge + FanDifferential[stage].on`,
  turn OFF when `discharge <= TargetDischarge + FanDifferential[stage].off`.
- `FanDifferential` defaults: `{(j+1)*10, (j+1)*10 - 20}` per stage j=0..5
  ([Settings.c:259-265](../GRC/ARM_Refrigeration/Application/Settings.c#L259-L265)).

### `TargetDischarge(refrigerant)` — control point

[CondenserFans.c:54-100](../GRC/ARM_Refrigeration/Application/CondenserFans.c#L54-L100):
- `FIXED_HEAD`: returns `Refrigeration[i].ControlPoint` (psi).
- `FLOATING_HEAD` / `BALANCED_PID`: target = `CalculatePressure(outsideTemp,
  refrigerant)`, clamped to `MinimumSetpoint` low and to `CalculatePressure(105 °F)`
  high. Falls back to `ControlPoint` if outside-temp sensor invalid.

### `CondenserPID()` — PWM trim

[CondenserFans.c:215-225](../GRC/ARM_Refrigeration/Application/CondenserFans.c#L215-L225):
runs `PIDLoop(PWM_CONDENSER_n, PID_CONDENSER_n, target,
&Settings.CondenserFan.PID, …)` against `discharge - target`. PWM range
55-277 counts; rate-limited 10 counts/cycle (see §5).

### Fan delay after compressor stop

There is **no explicit fan post-run timer**. Fans are forced off in
`CtrlCondenserFansOff(unit)` whenever the compressor goes off
([Compressor.c:251-257](../GRC/ARM_Refrigeration/Application/Compressor.c#L251-L257)),
and also from `CompressorOff()` via PWM channel reset.

### Lead rotation

`CondenserRotation.RotateFans` (hours, default 24).
`SetLeadFans()` rotates the lead fan when its `LeadRuntime > RotateFans * 3600`
or when the current lead is disabled:
[CondenserFans.c:248-282](../GRC/ARM_Refrigeration/Application/CondenserFans.c#L248-L282).

---

## 5. EXV PID (`ExpansionValve.c` + `PIDCtrl.c`)

### Controlled variable / setpoint

Controlled = **superheat** = `SuctionTemp -
CalculateSaturationTemp(SuctionPressure, VAPOR, refrigerant)` (converted to
selected unit): [Compressor.c:309-318](../GRC/ARM_Refrigeration/Application/Compressor.c#L309-L318).
Setpoint = `Settings.Compressor.SuperHeatSetPoint` (°F default 15):
[ExpansionValve.c:106-110](../GRC/ARM_Refrigeration/Application/ExpansionValve.c#L106-L110).

### Pre-check / warm-up

[ExpansionValve.c:46-87](../GRC/ARM_Refrigeration/Application/ExpansionValve.c#L46-L87):
- LLS off → reset PWM to MIN, clear warmup timer, return 0 (no PID).
- LLS just on → for `Settings.Expansion.Warmup` minutes, force output to
  `PercentToPWMVal(StartPercent)` and seed `IntError` so the I-term equals that
  starting position when PID kicks in.

### PID gains / sample period / output range

`PID_PARAMS = {short P, I, D, U}` ([PIDCtrl.h:46-52](../GRC/ARM_Refrigeration/Application/PIDCtrl.h#L46-L52)).
Defaults: `P=5, I=15, D=2, U=3 s`. `U` is the **PID sample period in seconds**
([PIDCtrl.c:227-235](../GRC/ARM_Refrigeration/Application/PIDCtrl.c#L227-L235)).
`PIDController()` clamps output to `[RangeMin, RangeMax]` (initialized to PWM
counts 55..277, ie. 0-100 %): [PIDCtrl.c:106-167](../GRC/ARM_Refrigeration/Application/PIDCtrl.c#L106-L167).

### Anti-windup / scaling

[PIDCtrl.c:115-132](../GRC/ARM_Refrigeration/Application/PIDCtrl.c#L115-L132):
- Output = `P*err + I*intErr*0.022 + D*(err - prevErr)`.
- `WindupLimit = Range / (0.022 * Ki)` — inversely proportional to I gain.
- IntError clamped to `[0, WindupLimit]` (note: **only positive integral**!).
- Errors rounded to 0.1 before integration.
- Slew-rate limit `PWM_INC_VALUE=10` counts/cycle in `PIDLoop()`:
  [PIDCtrl.c:240-258](../GRC/ARM_Refrigeration/Application/PIDCtrl.c#L240-L258).

### Saturation-pressure / pressure-saturation lookup

Per-refrigerant cubic / quartic polynomial fits in
[Refrigerant.c:35-58](../GRC/ARM_Refrigeration/Application/Refrigerant.c#L35-L58):

```
TempToPressureCoefficients[REF][4]   // a3*T^3 + a2*T^2 + a1*T + b  → psi (input °F)
PressureToTempCoefficients[REF][5]   // a4*P^4 + … + b              → °F  (input psi)
```

Refrigerants: `R_22, R_410A, R_407C, R_134A, R_404A, R_507, R_407A`
([Refrigerant.h:23-39](../GRC/ARM_Refrigeration/Application/Refrigerant.h#L23-L39)).
`R_407A` and `R_407C` use a **separate liquid-side curve** (`R_407ALiq, R_407CLiq`)
when `pressureType=LIQUID` (subcooling): [Refrigerant.c:99-122](../GRC/ARM_Refrigeration/Application/Refrigerant.c#L99-L122).
The temperature input is always **°F** — `TargetDischarge` converts ambient to °F
before the lookup if `Settings.TempType` is Celsius
([CondenserFans.c:74-82](../GRC/ARM_Refrigeration/Application/CondenserFans.c#L74-L82)).

---

## 6. Defrost state machine (`DefrostCtrl.c`)

Per-circuit state stored in `DefrostOn[3]`, values from
[DefrostCtrl.h:33-40](../GRC/ARM_Refrigeration/Application/DefrostCtrl.h#L33-L40):
`DEFROST_TYPE_NONE, _TREND, _OVERRIDE, _MANUAL` (+ `_PENDING` variants used when
trying to start while the compressor is still running and needs pumpdown first).

### Trend tracking

`Trend[3].trend[128]` ring; one suction-psi sample appended every minute by
`TrendAdd()` ([Compressor.c:1085-1094](../GRC/ARM_Refrigeration/Application/Compressor.c#L1085-L1094)).
Once `VariableTimer` minutes of samples are collected,
`CurrentSuctionTrend` = mean of buffer
([DefrostCtrl.c:124-159](../GRC/ARM_Refrigeration/Application/DefrostCtrl.c#L124-L159)).

### Trigger logic — `CtrlDefrost()`

[DefrostCtrl.c:349-470](../GRC/ARM_Refrigeration/Application/DefrostCtrl.c#L349-L470):

- **Trend-based**: after `VariableTimer * 60 s` of samples, if
  `currentSuction <= trend - SuctionDifferential` AND coils freezing
  (`suctionTemp - superheat <= 35 °F` / `1.6 °C`) AND
  `VariableTimer > 0` AND `!DisableDefrost` →
  start `IT_DEFROST_INITIATE`. After `InitiateTimer` minutes still below trend →
  `TurnDefrostOn(DEFROST_TYPE_TREND)`.
- **Override**: if `DefrostOverride[i] >= OverrideTimer * 3600` (hours since
  last defrost) AND no other circuit defrosting AND `IT_DEFROST_WAIT + 5 min`
  elapsed → `TurnDefrostOn(DEFROST_TYPE_OVERRIDE)`.
- **Manual**: posted by UI / DataExc; flagged as `_MANUAL_PENDING` until
  pumpdown completes
  ([DataExc.c:411-422](../GRC/ARM_Refrigeration/Application/DataExc.c#L411-L422)).

### `TurnDefrostOn()`

[DefrostCtrl.c:268-330](../GRC/ARM_Refrigeration/Application/DefrostCtrl.c#L268-L330):
1. If compressor on → start pumpdown; if LLS still on → switch off.
2. Once compressor is off → drive `EQ_DEFROST` on, start
   `IT_DEFROST_TERMINATION` and `IT_DEFROST_TREND` timers, set
   `DefrostOn[i] = type`, clear `DefrostOverride`.
3. For PRESSURE / TEMPERATURE termination types (gas defrost): force all
   condenser fans on, max condenser PWM (`DefrostCondenserFans()`).

### Termination

In the same `CtrlDefrost()` loop:
- `PRESSURE` mode: after `≥1 min` running, if
  `discharge >= TerminationPressure` → off.
- `TEMPERATURE` mode: after `≥1 min`, if
  `dischargeTemp >= TerminationTemperature` (or sensor saturated) → off.
- **`TIME` mode and all modes**: if `IT_DEFROST_TERMINATION + TerminationTime *
  60 s` elapsed → off.

### `TurnDefrostOff()`

[DefrostCtrl.c:170-209](../GRC/ARM_Refrigeration/Application/DefrostCtrl.c#L170-L209):
drops `EQ_DEFROST`, zeros all defrost timers, sets `IT_DEFROST_WAIT = now`
(so override won't immediately re-trigger), and for gas defrost (`PRESSURE` /
`TEMPERATURE`) drops fans + resets condenser PWM. There is **no separate
"DRIP / FAN-DELAY / BACK" stage** — termination just opens LLS again on the
next demand cycle.

### Interaction with stages

`IsDefrosting()` short-circuits `ManageEquipment()`
([Compressor.c:476-479](../GRC/ARM_Refrigeration/Application/Compressor.c#L476-L479)):
no demand stages run during defrost.

---

## 7. Safety / failure handling (`Failures.c`, `Warnings.c`)

### Two parallel arrays

- `WARNING_ITEMS` (≈45 entries; `Warnings.h:96-176`): UI-facing alarm catalog,
  written via `WarningsSet(item, FM_NONE|FM_ALARM|FM_FAIL, ...)`. Latched in the
  `Warning[i].Status` byte until `WarningClearByIndex()` or `WarningsClear()`.
- `FAIL_ALARM` (`Warnings.h:42-78`, 36 entries `IN_*` and `FAL_*`): per-circuit
  failure latches consumed by `CompressorChk()`. `FailureAlarm[]` byte array
  ([Warnings.c:37](../GRC/ARM_Refrigeration/Application/Warnings.c#L37)).
- `EQUIP_ALARMS` (`Timer.h:25-46`, 20 entries `AL_*`): per-circuit timed alarms
  (superheat, suction, oil, discharge, low temp, power, no broadcast, etc.).
  Backing array `SystemAlarm[]` + `AlarmTimer[]` debounce
  ([Timer.c:50-52](../GRC/ARM_Refrigeration/Application/Timer.c#L50-L52)).

### `ManageAlarm()` (timed)

[Failures.c:79-103](../GRC/ARM_Refrigeration/Application/Failures.c#L79-L103):
- On true: arm `AlarmTimer[alarm]` if not armed, then after `Failure[fail].Timer`
  minutes set `SystemAlarm[alarm] = Failure[fail].Mode`. Returns 1 only if the
  resulting mode is `FM_FAIL`.
- On false (when `reset=RESET`): clear `SystemAlarm[alarm]`, reset timer.

### `ManageFailure()` (untimed)

[Failures.c:51-66](../GRC/ARM_Refrigeration/Application/Failures.c#L51-L66):
- Immediate: set `WARN_*`, `FailureAlarm[fail] = FM_FAIL`, call
  `FailCompressor(unit)` (which off-pulls compressor + condensers and latches
  `SetCompressorFailure`).
- `reset=NO_FAIL` → just `FM_ALARM` (keeps running).
- `reset=RESET` and condition false → clear.

### Per-alarm summary

| Alarm                    | Source                                 | Trigger                                | Default mode                          | Action                       |
|--------------------------|----------------------------------------|----------------------------------------|---------------------------------------|------------------------------|
| `AL_POWER`               | `EQ_POWER_MONITOR` true ≥ `PowerFailTimeout` min | phase loss/voltage drop      | `FM_FAIL`, timer forced 0 ([States.c:144-147](../GRC/ARM_Refrigeration/Application/States.c#L144-L147)) | All comps fail, ST_FAILURE |
| `AL_SUPERHEAT_n`         | superheat outside `[Low, High]` window after `Failure[FAIL_SUPERHEAT].Timer` min of compressor warm-up | sensor or load issue | `FM_ALARM` default, timer 5 min | pumpdown + fail |
| `AL_SUPERHEATLOW_n`      | superheat < `SuperHeatLow`             | flooded suction                        | `FM_ALARM`                            | pumpdown + fail              |
| `AL_DISCHARGE_n`         | discharge >= `DischargeHigh`           |                                       | `FM_ALARM`, timer 0                   | drops compressor only (returns -1; fans keep running) |
| `AL_SUCTION_n`           | suction PSI persistently low           |                                       | `FM_FAIL`                             | hard fail                    |
| `AL_OIL_n`               | (oil - suction) < `OilPressureLow`     |                                       | `FM_ALARM`                            | hard fail when escalates     |
| `AL_OUTTEMPSENSOR`       | outside temp sensor lost ≥ `Failure[FAIL_OUTSIDE_AIR].Timer` min | | `FM_ALARM`, timer 10 | log / report only         |
| `AL_NOBROADCAST`         | master/slave broadcast missing 10 min  |                                       | `FM_ALARM`                            | falls back to local sensors  |
| `AL_LOWTEMP`             | low ambient cutout                     | `outsideTemp <= LowAmbientTemperature` | (forced check)                        | system → STANDBY ([States.c:65-75](../GRC/ARM_Refrigeration/Application/States.c#L65-L75)) |
| `IN_CRANKCASE_n`         | crankcase-heater DI false while comp off ≥ `PowerFailTimeout` min |   | hard `FM_FAIL`                        | re-prove (CrankcaseRunTimer) |
| `IN_CONDENSERLOAD_n`     | `EQ_CONDENSER_OVERLOAD` true           |                                       | hard `FM_FAIL`                        | hard fail                    |
| `IN_COMPRESSORLOAD_n`    | `EQ_COMPRESSOR_OVERLOAD` true          |                                       | hard `FM_FAIL`                        | hard fail                    |
| `IN_OIL_n`               | `EQ_OIL_FAIL` DI true                  |                                       | hard `FM_FAIL`                        | hard fail                    |
| `IN_DISCHARGE_n`         | `EQ_HH_PRESSURE` (HP switch) true      |                                       | hard `FM_FAIL`                        | hard fail                    |
| `IN_LOWSUCTION_n`        | `EQ_SUCTION_PRESSURE` (LP) true        |                                       | hard `FM_FAIL`                        | hard fail                    |
| `FAL_HIGHSUCTION_n`      | suction pressure sensor undefined      |                                       | latched                               | hard fail                    |
| `FAL_NODISCHARGE_n`      | discharge pressure sensor undefined    |                                       | latched                               | hard fail                    |
| `FAL_NOPRESS_n`/`BOARDNOTPRESS_n`/`PRESSDIS_n` | pressure analog board missing/wrong type/disabled | | latched                  | hard fail                    |

Per-sensor RUN_THROUGH / ALARM_ONLY / SAFE_OFF selector lives in
`Settings.Failure[FAILURES]`:
- `Mode=FM_NONE` (RUN_THROUGH) → `ManageAlarm` won't escalate and the
  `Failure[i].Mode != FM_NONE` guard in the per-failure check skips it (e.g.
  `OutsideTempSensorFailChk` — [Failures.c:332-334](../GRC/ARM_Refrigeration/Application/Failures.c#L332-L334)).
- `FM_ALARM` (ALARM_ONLY) → sets `SystemAlarm` but `ManageAlarm` returns 0,
  so the compressor keeps running.
- `FM_FAIL` (SAFE_OFF) → `ManageAlarm` returns 1 → caller calls
  `CompressorPumpdown` + `SetCompressorFailure` (e.g. SuperHeatChk
  [Failures.c:259-271](../GRC/ARM_Refrigeration/Application/Failures.c#L259-L271)).

### Clearing

- Per-circuit alarms / failures cleared by
  `ClearAlarmsByCompressor(physical)` when the operator throws the panel
  switch to OFF
  ([Compressor.c:248-258](../GRC/ARM_Refrigeration/Application/Compressor.c#L248-L258),
  [Timer.c:131-148](../GRC/ARM_Refrigeration/Application/Timer.c#L131-L148)).
- Whole `WarningsClear()` is called on factory-default reset
  ([States.c:259-262](../GRC/ARM_Refrigeration/Application/States.c#L259-L262) via
  `ST_RESET`).
- Power alarm `AL_POWER` clears as soon as `EQ_POWER_MONITOR` reads false again
  ([States.c:188-205](../GRC/ARM_Refrigeration/Application/States.c#L188-L205)).

---

## 8. Logs

### `PIDLogs.c` — PID telemetry

- Record: `PIDLOG_RECORD` written by `PIDLogWrite(Type, P, I, D, Output, Error)`
  inside `PIDController()` after every PID iteration **for the channels enabled
  in `Settings.Log.PID.{Cond1..3, Expansion1..3}`** (defaults all-on):
  [PIDCtrl.c:147-165](../GRC/ARM_Refrigeration/Application/PIDCtrl.c#L147-L165),
  [PIDLogs.c:482-595](../GRC/ARM_Refrigeration/Application/PIDLogs.c#L482-L595).
- Storage: SD-card sectors, 14 records per 512-byte block
  (`SDCARD_PIDLOG_RECSPERBLOCK=14`, `SDCARD_BLOCK_SIZE=512`):
  [SDCard.h:33,47](../GRC/ARM_Refrigeration/Application/SDCard.h#L33).
- Sample rate = the PID `U` period (default 3 s).
- Wrap behaviour: `Settings.Log.PID.Wrap` (default 1) — when set, oldest block
  is overwritten when SD card region fills; otherwise logging stops on full.
- Skipped entirely when **any** screw-comp circuit is enabled (avoids SD
  contention with pulse semaphore): [PIDCtrl.c:158-163](../GRC/ARM_Refrigeration/Application/PIDCtrl.c#L158-L163).

### `UserLogs.c` — sensor history (UI "History Log")

- One record per `Settings.Log.User.Interval` minutes (default 60 min).
- Captures *all* analog sensors (`Sensors[TOTAL_ANALOG_BOARDS_PER_SYSTEM *
  ANALOG_SENSORS_PER_BOARD]`) in `USERLOG_RECORD UserLog`:
  [UserLogs.c:43-44](../GRC/ARM_Refrigeration/Application/UserLogs.c#L43-L44),
  [UserLogs.c:984-1145](../GRC/ARM_Refrigeration/Application/UserLogs.c#L984-L1145).
- Wrap controlled by `Settings.Log.User.Wrap` (default 1).
- Export format: CSV-ish key/value over the UI's HTTP POST protocol (Lantronix);
  query window by date range — `UserLogRangeBegin/End`.

### `SystemLogs.c` — activity / alarm log (UI "Activity Log")

- One record per **state change or alarm transition**, written by
  `SystemLogWrite()`: [SystemLogs.c:1534-1696](../GRC/ARM_Refrigeration/Application/SystemLogs.c#L1534-L1696).
- Snapshot includes mode, per-compressor status, equipment-status string, and
  any new warning bitmap delta vs. `PreviousWarnings[NUM_WARNINGS]`.
- Same SD layout as User logs.

---

## 9. Demand input handling

GRC treats demand as a single 0-100 % integer, fed from a 4-20 mA loop on
analog board 1 sensor 3 (`SENSOR_REFRIG_DEMAND`).
**It is not a target suction PSI and not a stage mask** — it is a continuous
"how much capacity should be online" signal that walks the
`Settings.Compressor.Stage[15]` table.

- Source pointer aliased: `RefrigDemand = &Settings.AnalogBoard[1].Sensor[3].Value`
  ([Analog_Input.c:49](../GRC/ARM_Refrigeration/Application/Analog_Input.c#L49)).
- Master-slave override: when `MasterSlave == MSMODE_SLAVE` and master broadcast
  is alive, the master sends both `OutsideTemp` and `RefrigDemand` over the
  RS-485 broadcast — local sensor reading is bypassed:
  [Analog_Input.c:509-538](../GRC/ARM_Refrigeration/Application/Analog_Input.c#L509-L538).
- Conversion: 4-20 mA mapped to 0-100 % linearly; <180 ADC = 0, >900 = 100,
  saturate ≥ 901 returns 100; ADC zero returns
  `SENSOR_VAL_UNDEFINED` (treated as "demand unknown"):
  [Analog_Input.c:180-194](../GRC/ARM_Refrigeration/Application/Analog_Input.c#L180-L194).
- `CtrlCompressor(demand)` clamps invalid (`<-10 || >110 || UNDEFINED`) to
  `0` ([Compressor.c:586-591](../GRC/ARM_Refrigeration/Application/Compressor.c#L586-L591)).

### Decline-to-run threshold

Per-stage hysteresis. For the LLS stage at default `On=20, Off=10`:
- `demand >= 20` → LLS on (compressor will follow if suction OK)
- `demand <= 10` → LLS off → pumpdown
- `10 < demand < 20` → no change

So the system **declines to run** only when demand has crossed `Off` after
having been on. Adding stages U1, U2, … steps the unloaders in at
`On=30, 40, …` thresholds. The **lowest-stage `Off`** (LLS Off, default 10)
acts as the global "no demand" cutout.

### Continuous-pumpdown override

`Settings.Compressor.ContinuousPumpdown=1` allows the system to pump down even
with demand still active (≥5 %), useful for storage modes that want LLS-only
control: [Compressor.c:1002-1011](../GRC/ARM_Refrigeration/Application/Compressor.c#L1002-L1011).

---

## 10. Tick rate / timing constants

- `XTimerVal` is a free-running **seconds** counter, value of FreeRTOS
  `uptime_sec`. **All** `IntervalTimer[]` arithmetic is in seconds:
  [Timer.c:60](../GRC/ARM_Refrigeration/Application/Timer.c#L60),
  [Timer.c:438-442](../GRC/ARM_Refrigeration/Application/Timer.c#L438-L442).
  Macros `T_SECS=1, T_MINS=60, T_HOURS=3600`
  ([Timer.h:140-142](../GRC/ARM_Refrigeration/Application/Timer.h#L140)).
- **Main control loop** (`ThreadSerialCom`): runs `vTaskDelay(400 ms)` between
  iterations. Sequence per tick: `ReadAnalogBoards` → `CheckCompressorsRunPumpdown`
  → `ManagePumpdown` → `SetLeadCompressor` → `SetLeadFans` → `SetSystemState`
  → `SetMode` (which calls `ModeRefrig` → `CtrlCompressor` + `CtrlExpansionValves`
  + `CtrlCondenserFans`) → `CompressorRuntime` → `CondenserFanRuntime` →
  `CtrlDefrost`: [ThreadSerialCom.c:62-127](../GRC/ARM_Refrigeration/Application/ThreadSerialCom.c#L62-L127).
- **PID sample period**: `Settings.{Expansion,CondenserFan}.PID.U` seconds
  (default 3 s). Enforced inside `PIDLoop()`:
  [PIDCtrl.c:227-235](../GRC/ARM_Refrigeration/Application/PIDCtrl.c#L227-L235).
- **Defrost timer resolution**: `T_MINS = 60 s` for `VariableTimer`,
  `InitiateTimer`, `TerminationTime`; `T_HOURS` for `OverrideTimer`.
  Trend sample cadence = 1/min ([Compressor.c:1085-1094](../GRC/ARM_Refrigeration/Application/Compressor.c#L1085-L1094)).
- **Serial-shift timer (DI/DO)**: hardware Timer2 ISR every
  `30,000,000` SysClock ticks → ~600 ms (Tiva typical 50 MHz core); `shift`
  flag → `ThreadSerialShift` does one I/O cycle per flag, idling at
  `vTaskDelay(10 ms)`: [Timer.c:260-283](../GRC/ARM_Refrigeration/Application/Timer.c#L260-L283),
  [ThreadSerialShift.c:184-244](../GRC/ARM_Refrigeration/Application/ThreadSerialShift.c#L184-L244).
- **External watchdog**: Timer1 toggles `WD_CPLD` every `300,000` ticks (~6 ms);
  if the heartbeat stops the CPLD safely shuts outputs:
  [Timer.c:296-320](../GRC/ARM_Refrigeration/Application/Timer.c#L296-L320).
- **Internal watchdog**: WDT0 reloaded with `0x3FFFFFFF`; reset by
  `ThreadSerialCom` after every iteration: [Timer.c:351-380](../GRC/ARM_Refrigeration/Application/Timer.c#L351-L380).
- **Daily reset**: at 02:00 each day `DailyJobs()` zeros `DailyRuntime` for
  every compressor / fan ([Timer.c:198-218](../GRC/ARM_Refrigeration/Application/Timer.c#L198-L218)).
- **Screw-comp pulse**: half-second cadence inside `ManagePulse()` (every 2nd
  serial-shift tick): [ThreadSerialShift.c:124-167](../GRC/ARM_Refrigeration/Application/ThreadSerialShift.c#L124-L167).

For a single-Triton TS sim, the natural mapping is
`setInterval(loop, 400 /*ms*/)` for the main control, and `setInterval(io, 100
/*ms*/)` for shift-register-equivalent DI/DO debouncing. Use
`Math.floor((Date.now() - bootTime) / 1000)` to mimic `XTimerVal`.

---

## 11. Open questions / gotchas

1. **Integral term is one-sided.** `PIDController()` clamps `IntError` to
   `[0, WindupLimit]` (no negative integral)
   ([PIDCtrl.c:124-128](../GRC/ARM_Refrigeration/Application/PIDCtrl.c#L124-L128)).
   Combined with `WindupLimit ∝ 1/Ki`, this means low-gain loops can integrate
   for hours before saturating, while high-gain loops barely integrate at all.
   The sim should reproduce this faithfully — operators are tuned to it.

2. **Demand stage table semantics are positional, not enum.**
   `Stage[i].Equipment` is a `COMPRESSOR_EQ` and `Stage[i].Ordinal` selects
   which physical compressor takes that slot. The sim must walk all 15 slots
   in order; an "empty" slot has `Equipment==0 && Ordinal==0` (`IsStageEnabled`
   at [Compressor.c:561-568](../GRC/ARM_Refrigeration/Application/Compressor.c#L561-L568)).

3. **Refrigerant defaults are slot-shifted.** When the user changes refrigerant,
   `ChangePressureDefaults()` compacts `Settings.Refrigeration[0..2]`
   ([Refrigerant.c:127-180](../GRC/ARM_Refrigeration/Application/Refrigerant.c#L127-L180))
   — index 0 is **not** "compressor 1", it's "first non-undefined refrigerant".
   `GetRefrigeration(physical)` does the lookup; never index `Refrigeration[]`
   by physical compressor.

4. **Pumpdown is implicit, not a state.** There's no `ST_PUMPDOWN`. Pumpdown
   shows up only as `Pumpdown[physical]` boolean + LLS=off + compressor=on,
   resolved by suction crossing `LowSuctionPressureOff`. The UI status is
   `COMP_PUMPDOWN` derived from those flags
   ([Compressor.c:128-132](../GRC/ARM_Refrigeration/Application/Compressor.c#L128-L132)).

5. **Termination type "TIME" disables both pressure and temperature checks.**
   Pressure / temperature termination only run when `TerminationType == PRESSURE`
   or `== TEMPERATURE`; the time termination always runs as a hard cap.

6. **Saturation lookup is always °F-based.** `CalculatePressure` /
   `CalculateSaturationTemp` take/return °F — internal code converts to/from C
   at the boundary ([CondenserFans.c:74-82](../GRC/ARM_Refrigeration/Application/CondenserFans.c#L74-L82),
   [Compressor.c:317-318](../GRC/ARM_Refrigeration/Application/Compressor.c#L317-L318)).
   The sim should keep one canonical unit internally and convert at the UI
   boundary to avoid recreating GRC's `if (TempType == 0) ... else ...`
   sprawl.

7. **Phase / power monitor has THREE timeouts.** 20 s (silent shutdown), 3 min
   (alarm), and `PowerFailTimeout` min (hard fail). Easy to under-implement.

8. **Crankcase prove eats short-cycle.** When the prove timer fires, the system
   is held in `IT_PROVE_n` regardless of `ShortCycle`. Conversely, when prove
   completes the short-cycle timer is **not** rearmed
   ([Compressor.c:200-203](../GRC/ARM_Refrigeration/Application/Compressor.c#L200-L203))
   — first start after prove is immediate.

9. **`SENSOR_VAL_UNDEFINED = 0x7FFF`** is treated as both "missing reading"
   *and*, for analog board failures, as a hard fail trigger via
   `CheckValidPressures()`
   ([Compressor.c:42-58](../GRC/ARM_Refrigeration/Application/Compressor.c#L42-L58)).
   Don't quietly substitute 0 in the sim — preserve the sentinel.

10. **`SENSOR_VAL_MAX = 0x7FFE`** is the "transducer pegged" sentinel —
    discharge temp at max counts as defrost termination
    ([DefrostCtrl.c:451-455](../GRC/ARM_Refrigeration/Application/DefrostCtrl.c#L451-L455)).
    Don't conflate with SENSOR_VAL_UNDEFINED.

11. **Smoothing on every analog read.** `SmoothSensor()`
    ([Analog_Input.c:69-79](../GRC/ARM_Refrigeration/Application/Analog_Input.c#L69-L79))
    applies an EMA-ish filter unless the new reading is >8 units off — this
    means small step changes appear gradually. Worth replicating in the sim if
    you want PID tuning to feel right.

12. **`Settings.MasterSlave == MSMODE_SLAVE`** completely overrides local
    sensors (outside temp + demand) with broadcast values. If you sim a slave,
    expect demand to come over the orbit's RS-485 (or its TS equivalent),
    not from the demand 4-20 mA input.

13. **All shift-register I/O is 1-based in the boundary code.** `Input[1]..[10]`
    map to DI1..DI10; `Input[0]` is unused
    ([SerialShift.c:519-528](../GRC/ARM_Refrigeration/Application/SerialShift.c#L519-L528)).
    Easy off-by-one when porting.

14. **Defrost trend buffer is 128 samples max.** `_end` saturates at
    `SUCTION_TREND_MAX-1=127` — past 127 minutes the trend stops growing but
    keeps being recomputed against the sliding window
    ([DefrostCtrl.c:140-146](../GRC/ARM_Refrigeration/Application/DefrostCtrl.c#L140-L146)).

---

*End of discovery — no source modified.*