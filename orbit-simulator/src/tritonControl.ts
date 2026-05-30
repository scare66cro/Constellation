// =============================================================================
// tritonControl.ts — Triton refrigeration control core (Phase 1: pure extract)
// =============================================================================
// PURPOSE
//   Houses the Triton control loop that was previously inlined in
//   `OrbitSimulator.updateTriton()`.  This module is the future home of the
//   GRC-faithful state machines (compressor, condenser fans, EXV PID, defrost,
//   per-sensor failure handling) that Phases 2-6 of the GRC port plan will
//   bring online.
//
//   Phase 1 is a STRICTLY ZERO-BEHAVIOR-CHANGE EXTRACT.  Every line of logic
//   in `tickTritonControl()` is an exact translation of the original method
//   body.  No safety-input rewiring (the simulator's `di[5]=runProve`,
//   `di[2]=lpSwitch` mappings remain wrong on purpose — Phase 2 fixes them
//   along with the rest of the GRC compressor SM rewrite).
//
// PORTABILITY (production target: AM2432)
//   The control core takes a `TritonControlCtx` object instead of reaching
//   into `OrbitSimulator` internals.  Adapter callbacks (`writeTempBoard`,
//   `writePressBoard`) replace the in-place `this.state.analogBoards[i]`
//   mutation so the same tick can run on:
//     - Node TS in the orbit-simulator (sim path: callbacks mutate the
//       in-memory analog-board records)
//     - C on the AM2432 (production path: callbacks become register writes)
//
//   Avoid `Date.now()` inside the tick — the caller passes `nowMs` so the
//   port to C/RTOS can substitute its own monotonic clock.  Avoid Node-only
//   APIs (timers, fs, net) entirely.
//
// FULL DESIGN: docs/triton-grc-port-plan.md, /memories/repo/triton-orbit-spec.md
// =============================================================================

import {
  TRITON_ALARM_BITS,
  type TritonAlarm,
  type TritonFailureMode,
  type TritonFailureState,
  type TritonSetpoints,
  type TritonState,
} from './orbitSimulator';

// =============================================================================
// Refrigerant tables (lifted from GRC `Refrigerant.c`).  Kept here so the
// control core is self-contained and the eventual C port maps 1:1.
// =============================================================================

/** GRC `PressureDefaults` — 16-element vector per refrigerant.  Indexed by
 *  the GRC refrigerant enum (R22=0, R410A=1, R407C=2, R134A=3, R404A=4,
 *  R507=5, R407A=6).  Slots are: OFF, ON, DISC, HHP1, HHP2, HHP3, HHP4,
 *  LSA, MIN, FIX, FAN, U1, U2, U3, U4, DOF. */
type GrcPressureVec = readonly [
  number, number, number, number, number, number, number, number,
  number, number, number, number, number, number, number, number,
];

const GRC_PRESSURE_DEFAULTS: Record<number, GrcPressureVec> = {
  // GRC enum:           OFF, ON, DISC,HHP1,HHP2,HHP3,HHP4, LSA, MIN, FIX, FAN, U1,  U2, U3, U4, DOF
  /* R_22    */ 0:  [25, 55, 325, 290, 300, 310, 320, 20, 100, 200, 10,  60,  55, 50, 45, 250],
  /* R_410A  */ 1:  [50, 95, 519, 482, 492, 502, 512, 20, 169, 326, 14, 105, 100, 95, 90, 401],
  /* R_407C  */ 2:  [25, 55, 336, 300, 310, 320, 330, 20,  96, 202, 10,  54,  49, 44, 39, 254],
  /* R_134A  */ 3:  [22, 30, 225, 192, 202, 212, 222, 20,  57, 128,  7,  29,  24, 19, 14, 164],
  /* R_404A  */ 4:  [25, 55, 400, 358, 368, 378, 388, 20, 123, 241, 10,  74,  69, 64, 59, 298],
  /* R_507   */ 5:  [25, 55, 400, 360, 370, 380, 390, 20, 130, 251, 10,  80,  75, 70, 65, 310],
  /* R_407A  */ 6:  [25, 55, 360, 327, 337, 347, 357, 20, 100, 205, 10,  50,  45, 40, 35, 274],
};

/** Temperature → saturated-vapor pressure cubic coefficients [a,b,c,d]
 *  for `P(t) = a*t^3 + b*t^2 + c*t + d`, with `t` in °F and `P` in PSI.
 *  Lifted verbatim from GRC `Refrigerant.c TempToPressureCoefficients`.
 *  Indexed by GRC refrigerant enum (R22=0, R410A=1, R407C=2, R134A=3,
 *  R404A=4, R507=5, R407A=6). */
const GRC_TEMP_TO_P_COEFF: Record<number, readonly [number, number, number, number]> = {
  /* R_22    */ 0: [2.87064e-5, 6.104444e-3, 0.821491477, 24.04855744],
  /* R_410A  */ 1: [4.54707e-5, 9.520674e-3, 1.278976335, 48.66722633],
  /* R_407C  */ 2: [3.49531e-5, 6.717706e-3, 0.940156102, 29.99215385],
  /* R_134A  */ 3: [2.56407e-5, 4.204094e-3, 0.500317987,  6.628019008],
  /* R_404A  */ 4: [3.39324e-5, 7.142816e-3, 0.968937731, 32.11551756],
  /* R_507   */ 5: [4.35828e-5, 5.933340e-3, 1.053624172, 35.44995267],
  /* R_407A  */ 6: [3.57635e-5, 6.973933e-3, 0.997833893, 32.30318463],
};

/** Saturation-vapor pressure → temperature quartic coefficients
 *  [a4,a3,a2,a1,b] for `T(P) = a4*P^4 + a3*P^3 + a2*P^2 + a1*P + b`,
 *  with `P` in PSI and `T` in °F.  Lifted verbatim from GRC
 *  `Refrigerant.c PressureToTempCoefficients`.  Indexed by GRC
 *  refrigerant enum.  R-407A and R-407C have a separate liquid-side
 *  curve used when the input pressure is taken on the liquid line
 *  (subcooling); for vapor-side superheat we always use the entries
 *  here.  The two extra liquid entries (`7`, `8`) cover R-407C and
 *  R-407A respectively to mirror the GRC `R_407CLiq`/`R_407ALiq`
 *  table indices. */
const GRC_P_TO_T_COEFF: Record<number, readonly [number, number, number, number, number]> = {
  /* R_22    vapor */ 0: [-1.23077e-8, 1.27346e-5, -0.005152426, 1.255366373, -26.05491894],
  /* R_410A  vapor */ 1: [-2.02333e-9, 3.38782e-6, -0.002212261, 0.851125696, -35.23515217],
  /* R_407C  vapor */ 2: [-1.11101e-8, 1.20528e-5, -0.005030362, 1.220283896, -20.4302572 ],
  /* R_134A  vapor */ 3: [-6.00652e-8, 4.11758e-5, -0.010866345, 1.702829601,  -8.723462409],
  /* R_404A  vapor */ 4: [-6.33033e-9, 7.82539e-6, -0.003782307, 1.092220235, -30.08415286],
  /* R_507   vapor */ 5: [-5.0235e-9,  6.59017e-6, -0.003409960, 1.053080767, -31.94709005],
  /* R_407A  vapor */ 6: [-8.79238e-9, 1.00055e-5, -0.004420016, 1.148444786, -22.10834435],
  /* R_407C  liquid*/ 7: [-7.12622e-9, 8.55120e-6, -0.004010167, 1.121077350, -28.90536169],
  /* R_407A  liquid*/ 8: [-6.03405e-9, 7.51250e-6, -0.003664389, 1.072499444, -29.74479047],
};

/** Triton refrigerantType enum value → GRC refrigerant index.  Returns null
 *  when the Triton refrigerant has no GRC entry (caller falls back to R-404A
 *  as the closest HFC analog). */
export function tritonToGrcRefrigerant(triton: number): number | null {
  switch (triton) {
    case 0:  return 0; // R22
    case 1:  return 3; // R134A
    case 2:  return 4; // R404A
    case 3:  return 6; // R407A
    case 4:  return 2; // R407C
    case 5:  return 1; // R410A
    // 6=R448A, 7=R449A, 8=R450A, 9=R454A, 10=R513A, 11=R600A, 12=R744,
    // 13=R32, 14=R454B, 15=R454C, 16=R455A, 17=R1234yf, 18=R1234ze(E),
    // 19=R466A, 20=R515B — no GRC data; use closest analog via fallback.
    default: return null;
  }
}

/** Saturation pressure (PSI) for a given Triton refrigerant index at
 *  `tempF` °F.  Mirrors GRC `Refrigerant.c CalculatePressure()`.  Falls
 *  back to R-404A when the Triton refrigerant has no GRC analog. */
export function psatF(tritonType: number, tempF: number): number {
  const grc = tritonToGrcRefrigerant(tritonType) ?? 4;
  const c = GRC_TEMP_TO_P_COEFF[grc] ?? GRC_TEMP_TO_P_COEFF[4];
  const t  = tempF;
  const t2 = t * t;
  return c[0] * t2 * t + c[1] * t2 + c[2] * t + c[3];
}

/** Saturation temperature (°F) for a given Triton refrigerant index at
 *  `pressurePSI` PSI.  Mirrors GRC `Refrigerant.c CalculateSaturationTemp()`.
 *  When `vapor === false` and the refrigerant is a glide blend (R-407A /
 *  R-407C), the GRC liquid-side curve is used (subcooling); otherwise
 *  the vapor-side curve is used (superheat).  Falls back to R-404A when
 *  the Triton refrigerant has no GRC analog. */
export function tsatF(tritonType: number, pressurePSI: number, vapor = true): number {
  const grc = tritonToGrcRefrigerant(tritonType) ?? 4;
  let idx = grc;
  if (!vapor) {
    if (grc === 2) idx = 7;        // R_407C liquid
    else if (grc === 6) idx = 8;   // R_407A liquid
  }
  const c = GRC_P_TO_T_COEFF[idx] ?? GRC_P_TO_T_COEFF[4];
  const p  = pressurePSI;
  const p2 = p * p;
  return c[0] * p2 * p2 + c[1] * p2 * p + c[2] * p2 + c[3] * p + c[4];
}

/** Returns the GRC pressure-default vector for a Triton refrigerantType,
 *  falling back to R-404A for refrigerants the GRC firmware never shipped. */
export function getGrcPressureDefaults(tritonType: number): GrcPressureVec {
  const grc = tritonToGrcRefrigerant(tritonType);
  if (grc != null && GRC_PRESSURE_DEFAULTS[grc]) return GRC_PRESSURE_DEFAULTS[grc];
  return GRC_PRESSURE_DEFAULTS[4]; // R-404A fallback
}

/**
 * Re-seed all pressure-derived setpoints from the GRC PressureDefaults table
 * for the given refrigerant.  Mirrors GRC `SetRefrigerationSettings()` in
 * Settings.c.  Called whenever the refrigerant type changes and once at
 * Triton creation.  Returns the same setpoints reference for chaining.
 */
export function applyRefrigerantDefaults(sp: TritonSetpoints, tritonType: number): TritonSetpoints {
  const v = getGrcPressureDefaults(tritonType);
  sp.refrigerantType   = tritonType;
  sp.cutOutP           = v[0];
  sp.cutInP            = v[1];
  sp.discHighUnloadP   = v[2];
  sp.sucLowUnloadP     = v[7];      // LOW_SUCTION_ALARM
  // Floating-head cond-fan setpoint defaults to GRC FIXED_CONTROL value
  sp.condFanVfdSetpointP = v[9];
  // Floating-head clamps — `MinimumSetpoint` floor and P_sat(105 °F) ceiling,
  // exactly matching GRC `TargetDischarge()`.
  sp.condMinHeadP = v[8];
  sp.condMaxHeadP = Math.round(psatF(tritonType, 105));
  // Per-stage cond-fan staging: GRC uses (n+1)*FAN_DIFFERENTIAL above setpoint,
  // off = on - 2*FAN_DIFFERENTIAL.  We pre-compute six absolute thresholds
  // (PSI) anchored at FIXED_CONTROL so the staged-relay logic stays simple.
  const fixed = v[9];
  const diff  = v[10];
  for (let s = 0; s < 6; s++) {
    sp.fanStageOnP[s]  = fixed + (s + 1) * diff;
    sp.fanStageOffP[s] = fixed + (s + 1) * diff - 2 * diff;
    // Differential-mode equivalents (offsets above the floating target).
    // ON  = (s+1) * FAN_DIFFERENTIAL  (e.g. 10/20/30/40/50/60 for R-404A)
    // OFF = 5 PSI for every stage     (matches GRC default `fanXoff`=5)
    sp.fanDiffOnP[s]  = (s + 1) * diff;
    sp.fanDiffOffP[s] = 5;
  }
  return sp;
}

// =============================================================================
// Tick context — everything the pure tick needs from the host environment
// =============================================================================

/** Adapter callback to mirror the live temperature-board sensor values back
 *  into the host's analog-board representation.  In the simulator this writes
 *  `analogBoards[0].sensorValues[]`; on the AM2432 it would write the
 *  appropriate HAL register / Modbus HR slot. */
export type TritonBoardWriter = (
  /** ch0 = suctionT (°F * 10), ch1 = dischargeT, ch2 = llsT, ch3 = ambientT
   *  for the temp board.  ch0 = suctionP (PSI * 10), ch1 = dischargeP,
   *  ch2 = oilP, ch3 = demand-percent (raw 0-100) for the press board. */
  values: readonly [number, number, number, number],
) => void;

/** Per-tick context.  Built fresh by the host every tick.  Anything the
 *  control loop needs from the outside world goes here — it MUST NOT reach
 *  for `Date.now()`, `setInterval`, or any module-global state. */
export interface TritonControlCtx {
  /** Tick interval in seconds (e.g. 0.5 for the simulator's 500 ms cadence). */
  dt: number;
  /** Wall-clock ms (host-supplied so unit tests / power-fail simulations can
   *  inject a fake clock).  Used for alarm timestamps and the power-fail
   *  gap detector. */
  nowMs: number;
  /** Live digital-input array (read-only).  Indexing matches the simulator's
   *  current (Phase 1: BUGGY) layout — Phase 2 fixes this against the
   *  authoritative GRC pinout. */
  digitalInputs: readonly boolean[];
  /** Apply the temperature board mirror.  May be undefined if the host has
   *  no temp board (alarm `BOARD_MISSING_TEMP` will latch). */
  writeTempBoard?: TritonBoardWriter;
  /** Apply the pressure board mirror.  May be undefined if the host has no
   *  press board (alarm `BOARD_MISSING_PRESS` will latch). */
  writePressBoard?: TritonBoardWriter;
}

// =============================================================================
// Pure tick — runs the full Triton control loop for one cycle.
//
// PHASE 2 (compressor SM rewrite): Sections 0/1/2 below are the GRC-faithful
// port of the small-comm-refrig compressor state machine, wired to the
// authoritative Triton DI pinout (docs/triton-grc-port-plan.md §1).  Sections
// 3-7 still hold the Phase 1 verbatim extract (sensor physics, EXV, leak
// detector, alarm machinery) — those get rewritten in Phases 3-6.
//
// HOST CONTRACT
//   • `ctx.digitalInputs` is the orbit's 10-channel DI vector.  Indexing
//     follows the GRC main-board pinout (see TRITON_DI_NAMES in
//     orbitSimulator.ts):
//       [0]=DI_LT1 phase, [1]=DI_AF1 reserved, [2]=DI_1 crankcase current,
//       [3]=DI_2 cond OL, [4]=DI_3 comp OL, [5]=DI_4 oil fail switch,
//       [6]=DI_5 HP, [7]=DI_6 LP, [8]=DI_7 auto, [9]=DI_8 pumpdown.
//   • Production (AM2432) wires these the same way; the C port substitutes
//     `ctx.nowMs` with a monotonic clock and `ctx.dt` with the RTOS tick.
// =============================================================================

export function tickTritonControl(t: TritonState, ctx: TritonControlCtx): void {
  const dt = ctx.dt;

  // Defensive init for fields added in Phase 2 — old persisted snapshots
  // (or fresh `createDefaultTriton` calls predating this version) may load
  // without them; ensure they're numbers before any arithmetic.
  if (typeof t.crankcaseProveSecRemaining !== 'number') t.crankcaseProveSecRemaining = 0;
  if (typeof t.pumpdownSecRemaining       !== 'number') t.pumpdownSecRemaining = 0;
  if (typeof t.phaseLossSec               !== 'number') t.phaseLossSec = 0;

  // Phase 7 — log buffers.  Defensive init so old snapshots / fresh
  // states without `logs` work.  Capture a snapshot of the alarm-active
  // map and run-state at tick start so Section 8 can diff against
  // post-tick state and emit edge-driven SysLog entries.
  if (!t.logs) t.logs = { pid: [], user: [], sys: [], lastUserLogMs: 0 };
  const preAlarmActive: Record<string, boolean> = {};
  for (const a of t.alarms) preAlarmActive[a.code] = a.active;
  const preCompOn      = t.compressorOn;
  const preDefrostStg  = t.defrostStage ?? 0;

  // Generic ring-buffer push: append + evict-oldest when capacity hit.
  // Tied to log structures only so it stays portable to a static C array
  // with a head/tail index pair on the AM2432.
  const pushLog = <T>(buf: T[], entry: T, cap: number): void => {
    buf.push(entry);
    if (buf.length > cap) buf.splice(0, buf.length - cap);
  };

  // ── Local alarm helpers (used by every section) ────────────────────────
  // Latching alarm: stays in the list (acked or active) until both
  // (cleared AND acked).  Identical semantics to the GRC `Alarm[]` table.
  const latchAlarm = (code: string, label: string, cond: boolean) => {
    const ex = t.alarms.find(a => a.code === code);
    if (cond) {
      if (ex) ex.active = true;
      else t.alarms.push({ code, label, active: true, acked: false, timestamp: ctx.nowMs });
    } else if (ex) {
      ex.active = false;
      if (ex.acked) t.alarms = t.alarms.filter(a => a !== ex);
    }
  };

  // 0. Read safety inputs from the corrected GRC DI pinout and evaluate
  //    interlocks.  Hard faults (HP / Comp OL / Oil-fail switch) latch into
  //    `lockoutMask`; cond-fan overload is ALARM-ONLY (matches GRC
  //    `CompressorChk` which does not include cond OL in the hard-trip set
  //    and the field convention that a tripped cond-fan should not stop
  //    the compressor on its own — discharge-pressure hard cutout will).
  const di = ctx.digitalInputs;
  const sf = t.safeties;
  sf.phaseMonitor      = !!di[0];
  // di[1] = DI_AF1 reserved — intentionally not read.
  sf.crankcaseCurrent  = !!di[2];
  sf.condFanOverload   = !!di[3];
  sf.compOverload      = !!di[4];
  sf.oilFailSwitch     = !!di[5];
  sf.hpSwitch          = !!di[6];
  sf.lpSwitch          = !!di[7];
  sf.autoRunPermissive = !!di[8];
  sf.pumpdownSwitch    = !!di[9];

  // Latching faults (manual reset via ack-all clears the bits).  Set the
  // bit on any open input; the alarm machinery below maps each bit to its
  // SAF_* code.  Per GRC: cond-fan OL is NOT a latching trip.
  if (!sf.hpSwitch)      sf.lockoutMask |= 0x01;
  if (!sf.compOverload)  sf.lockoutMask |= 0x02;
  if (!sf.oilFailSwitch) sf.lockoutMask |= 0x04;

  // ── Power-fail / phase-monitor tiered response (GRC States.c §3) ──
  //    Tracks how long DI_LT1 has been continuously open.  Three thresholds:
  //      ≥20 s   → ensure compressor off, status=SYSTEM_OFF
  //      ≥3 min  → raise SAF_PHASE alarm (auto-clears when phase returns)
  //      ≥`powerFailMinutes` → latch FAIL_POWER and force fresh prove
  if (!sf.phaseMonitor) t.phaseLossSec += dt;
  else                  t.phaseLossSec  = 0;
  const phaseSysOff = t.phaseLossSec >= 20;
  const phaseAlarm  = t.phaseLossSec >= 3 * 60;
  if (t.phaseLossSec >= Math.max(60, t.setpoints.powerFailMinutes * 60)) {
    // Force a fresh crankcase prove on extended phase loss.
    const fullProveSec = Math.max(0, t.setpoints.crankcaseRunHours) * 3600;
    if (fullProveSec > t.crankcaseProveSecRemaining) {
      t.crankcaseProveSecRemaining = fullProveSec;
    }
  }

  // ── Crankcase prove timer (GRC Compressor.c::UpdateCompressorProveTime) ──
  //    Decrements continuously.  If the heater current is lost while the
  //    compressor is OFF (DI_1 open), restart the timer to its full value.
  //    A non-zero timer blocks compressor cut-in (`COMP_PROVE`).
  if (!t.compressorOn && !sf.crankcaseCurrent) {
    const fullProveSec = Math.max(0, t.setpoints.crankcaseRunHours) * 3600;
    if (fullProveSec > t.crankcaseProveSecRemaining) {
      t.crankcaseProveSecRemaining = fullProveSec;
    }
  }
  if (t.crankcaseProveSecRemaining > 0) {
    t.crankcaseProveSecRemaining = Math.max(0, t.crankcaseProveSecRemaining - dt);
  }
  const proveBlocked = t.crankcaseProveSecRemaining > 0;
  if (proveBlocked) sf.lockoutMask |= 0x08;
  else              sf.lockoutMask &= ~0x08;

  // Pumpdown is "active" any tick a manual switch command is open OR an
  // auto-pumpdown countdown is running.  GRC does NOT software-suppress the
  // LP-switch DI during pumpdown — the LP alarm escalation is handled by the
  // FAIL_SUCTION timer instead.  We mirror that behaviour: `pumpdownActive`
  // gates compressor stop logic but does NOT bypass the LP hard-trip.
  const manualPumpdown = !sf.pumpdownSwitch && t.compressorOn;
  const pumpdownActive = manualPumpdown || t.pumpdownSecRemaining > 0;

  // Hard interlocks — any of these tripped → compressor not permitted to run.
  // Note `!sf.lpSwitch` is a hard trip in GRC (no software bypass during
  // pumpdown); short-cycle escalation is via FAIL_SUCTION timer.
  const hardTrip =
       phaseSysOff
    || !sf.autoRunPermissive
    || !sf.lpSwitch
    || sf.lockoutMask !== 0
    || (t.safeOffMask ?? 0) !== 0;

  // Latch the SAF_* alarms.  Cond-fan OL is alarm-only.
  latchAlarm('SAF_PHASE',   'Phase Monitor Trip',          phaseAlarm);
  latchAlarm('SAF_HP',      'High-Pressure Switch Open',   !sf.hpSwitch || (sf.lockoutMask & 0x01) !== 0);
  latchAlarm('SAF_LP',      'Low-Pressure Switch Open',    !sf.lpSwitch);
  latchAlarm('SAF_COMP_OL', 'Compressor Overload',         !sf.compOverload || (sf.lockoutMask & 0x02) !== 0);
  latchAlarm('SAF_COND_OL', 'Cond Fan Overload',           !sf.condFanOverload);
  latchAlarm('SAF_PERMIT',  'Auto-Run Permissive Open',    !sf.autoRunPermissive);
  // SAF_PROVE bit (12) is reused for the GRC crankcase-prove block.  The
  // operator sees "Crankcase Prove" in the UI; the bit number stays stable.
  latchAlarm('SAF_PROVE',   'Crankcase Prove (heater not yet warmed)', proveBlocked);

  // ── 0c. Defrost state machine (Phase 5).  Mirrors GRC `DefrostCtrl.c`
  //        with simplifications appropriate for single-circuit Triton:
  //          • Trend-based DEMAND defrost is not yet implemented (would
  //            require a 128-sample suction-pressure ring buffer + ≥2 h
  //            of data before the first sample is trusted).  TODO Phase
  //            5.5 — see docs/triton-grc-port-plan.md §6.
  //          • TIMED initiation fires on the override timer
  //            (`defrostSinceLastSec >= defrostIntervalHours * 3600`).
  //          • MANUAL initiation comes from `defrostManualPending` set by
  //            the REST endpoint /api/triton/defrost {action:'start'}.
  //
  //        Stage transitions:
  //          0 NONE → 1 PUMPDOWN     when (TIMED || MANUAL) AND
  //                                   pumpDownBeforeDefrost==1 AND comp ON.
  //          0 NONE → 2 ACTIVE       when initiation fires and either
  //                                   pumpDownBeforeDefrost==0 OR comp OFF.
  //          1 PUMPDOWN → 2 ACTIVE   when compressor reaches OFF.
  //          2 ACTIVE → 3 DRIP       when termination condition met
  //                                   (TIME / TEMPERATURE / PRESSURE)
  //                                   OR `defrostMaxMinutes` ceiling hit.
  //          3 DRIP → 0 NONE         when `defrostStageSec >= dripTimeSec`.
  //
  //        While `defrostStage > 0`, `t.defrostActive = true` and the
  //        compressor is forced OFF in §1; in HOT_GAS mode (`defrostMode==3`)
  //        all condenser fans are forced ON during ACTIVE (§5 honours this
  //        through the `defrostHotGasFans` flag set below).
  if (typeof t.defrostStage         !== 'number' || !isFinite(t.defrostStage))         t.defrostStage         = 0;
  if (typeof t.defrostType          !== 'number' || !isFinite(t.defrostType))          t.defrostType          = 0;
  if (typeof t.defrostStageSec      !== 'number' || !isFinite(t.defrostStageSec))      t.defrostStageSec      = 0;
  if (typeof t.defrostSinceLastSec  !== 'number' || !isFinite(t.defrostSinceLastSec))  t.defrostSinceLastSec  = 0;
  if (typeof t.defrostManualPending !== 'boolean')                                     t.defrostManualPending = false;

  // Bookkeeping: count seconds since last defrost end (override timer);
  // grow the per-stage timer.
  t.defrostSinceLastSec += dt;
  if (t.defrostStage > 0) t.defrostStageSec += dt;
  else                    t.defrostStageSec  = 0;

  const dpSp = t.setpoints;
  const overrideArmed = dpSp.defrostIntervalHours > 0
    && t.defrostSinceLastSec >= dpSp.defrostIntervalHours * 3600;

  // ── Trend-based DEMAND defrost sampling (Phase 5.5).  Mirrors GRC
  //     `Trend[].trend[128]` ring + `IT_DEFROST_INITIATE` sustain timer
  //     (DefrostCtrl.c §1).  Only runs when operator has enabled it via
  //     `defrostTrendVarTimerMin > 0`.  Sampling happens only while the
  //     compressor is ON and outside a defrost cycle, so the buffer
  //     reflects steady-state evaporator load — not pumpdown / off
  //     transients.
  const trendVarMin = (dpSp as any).defrostTrendVarTimerMin ?? 0;
  if (trendVarMin > 0) {
    if (!t.defrostTrend || !Array.isArray(t.defrostTrend.ring) || t.defrostTrend.ring.length !== 128) {
      t.defrostTrend = {
        ring: new Array(128).fill(NaN),
        head: 0, count: 0, sampleAccumSec: 0, warmupSec: 0,
        trendPsi: NaN, initiateSec: 0,
      };
    }
    const tr = t.defrostTrend;
    if (t.compressorOn && t.defrostStage === 0) {
      tr.warmupSec     += dt;
      tr.sampleAccumSec += dt;
      while (tr.sampleAccumSec >= 60) {
        tr.sampleAccumSec -= 60;
        tr.ring[tr.head] = t.sensors.suctionP.value;
        tr.head = (tr.head + 1) % 128;
        if (tr.count < 128) tr.count++;
      }
      // Compute rolling mean once warm-up has elapsed (mirrors GRC
      // `if (VariableTimer minutes elapsed) CurrentSuctionTrend = mean`).
      if (tr.warmupSec >= trendVarMin * 60 && tr.count > 0) {
        let sum = 0, n = 0;
        for (let i = 0; i < tr.count; i++) {
          const v = tr.ring[i];
          if (typeof v === 'number' && isFinite(v)) { sum += v; n++; }
        }
        tr.trendPsi = n > 0 ? sum / n : NaN;
      }
    } else {
      // Compressor off OR defrost in progress: hold buffer, freeze warmup
      // (so a brief comp-off blip doesn't reset the gate), but reset the
      // sustain-timer because the trigger predicate isn't valid.
      tr.sampleAccumSec = 0;
      tr.initiateSec = 0;
    }
  } else if (t.defrostTrend) {
    // Operator disabled trend defrost — freeze the sustain timer + clear
    // the latest mean so UI shows "disabled" cleanly.  Keep the ring so
    // re-enabling doesn't cost a fresh warm-up if the operator toggles
    // the feature back on within the same compressor run.
    t.defrostTrend.initiateSec = 0;
    t.defrostTrend.trendPsi = NaN;
  }

  // Initiate from NONE.
  let initiateType = 0;
  if (t.defrostStage === 0) {
    if (t.defrostManualPending) initiateType = 2;       // MANUAL
    else if (overrideArmed && dpSp.defrostMode !== 0) initiateType = 1; // TIMED
    else if (
      trendVarMin > 0 && dpSp.defrostMode !== 0 && t.compressorOn && t.defrostTrend
      && t.defrostTrend.warmupSec >= trendVarMin * 60
      && isFinite(t.defrostTrend.trendPsi)
    ) {
      // Trend trigger: suction has dropped `defrostTrendDiffP` PSI below
      // the rolling mean AND the coil is freezing (`suctionT - superheat
      // <= 35 °F` mirrors GRC `coil <= 1.6 °C`).  Hold the predicate for
      // `defrostTrendInitiateMin` minutes before committing.
      const diff   = (dpSp as any).defrostTrendDiffP ?? 5;
      const sustainMin = (dpSp as any).defrostTrendInitiateMin ?? 5;
      const sucP   = t.sensors.suctionP.value;
      const sucT   = t.sensors.suctionT.value;
      const sh     = (t as any).derived?.superheat ?? (sucT - tsatF(t.setpoints.refrigerantType, sucP));
      const coilOk = (sucT - sh) <= 35; // °F
      const trigger = sucP <= (t.defrostTrend.trendPsi - diff) && coilOk;
      if (trigger) {
        t.defrostTrend.initiateSec += dt;
        if (t.defrostTrend.initiateSec >= sustainMin * 60) {
          initiateType = 3; // TREND
        }
      } else {
        t.defrostTrend.initiateSec = 0;
      }
    }
    if (initiateType !== 0) {
      t.defrostType = initiateType;
      t.defrostManualPending = false;
      if (t.defrostTrend) t.defrostTrend.initiateSec = 0;
      // Pick PUMPDOWN or ACTIVE based on pumpDownBeforeDefrost + comp state.
      if (dpSp.pumpDownBeforeDefrost === 1 && t.compressorOn) {
        t.defrostStage = 1; // PUMPDOWN
        // Arm a pumpdown countdown so suction bleeds rather than slamming
        // off; mirrors the existing manual-pumpdown path in §1.
        if (t.pumpdownSecRemaining <= 0) t.pumpdownSecRemaining = 60;
      } else {
        t.defrostStage = 2; // ACTIVE
      }
      t.defrostStageSec = 0;
    }
  } else if (t.defrostStage === 1) {
    // PUMPDOWN — wait for compressor to reach OFF (§1 will drop it once
    // suction <= cutOutP or the 60 s pumpdown timer runs out).  Hard
    // ceiling: if pumpdown takes longer than 5 min, force ACTIVE anyway.
    if (!t.compressorOn || t.defrostStageSec >= 5 * 60) {
      t.defrostStage = 2;
      t.defrostStageSec = 0;
      t.pumpdownSecRemaining = 0;
    }
  } else if (t.defrostStage === 2) {
    // ACTIVE — compressor is forced off in §1; check termination.
    let term = false;
    if (dpSp.defrostTermType === 1) {
      // TEMPERATURE: dischargeT >= termT (1-min minimum to avoid sensor
      // settling false-trips, mirrors GRC `≥1 min` guard).
      if (t.defrostStageSec >= 60 && t.sensors.dischargeT.value >= dpSp.defrostTermT) {
        term = true;
      }
    } else if (dpSp.defrostTermType === 2) {
      // PRESSURE: dischargeP >= termP (same 1-min guard).
      if (t.defrostStageSec >= 60 && t.sensors.dischargeP.value >= dpSp.defrostTermP) {
        term = true;
      }
    }
    // TIME ceiling — also acts as hard cap for TEMP/PRESSURE termination
    // in case the sensor never reaches the threshold (mirrors GRC).
    if (!term && t.defrostStageSec >= dpSp.defrostMaxMinutes * 60) term = true;

    if (term) {
      t.defrostStage = 3;       // DRIP
      t.defrostStageSec = 0;
      // Set lastDefrostEnd as the canonical "defrost just ended" timestamp
      // (the override timer will start counting from the end of DRIP — see
      // stage 3 → 0 transition below).
    }
  } else if (t.defrostStage === 3) {
    // DRIP — compressor + fans stay off so meltwater drains.
    if (t.defrostStageSec >= Math.max(0, dpSp.dripTimeSec)) {
      t.defrostStage         = 0;     // NONE
      t.defrostType          = 0;
      t.defrostStageSec      = 0;
      t.defrostSinceLastSec  = 0;     // restart the override-timer clock
      t.lastDefrostEnd       = ctx.nowMs;
    }
  }
  t.defrostActive = t.defrostStage > 0;

  // Mark whether the cond-fan section should force fans ON for hot-gas
  // recovery.  Mirrors GRC `IsDefrostWithCondenser()`: the gas-defrost
  // signature is `TerminationType ∈ {PRESSURE, TEMPERATURE}` AND the
  // defrost output is energized (stage==ACTIVE).  In that case the
  // condenser fans run flat-out to dump head-pressure built up by the
  // hot-gas circulation through the evaporator.  Compressor stays OFF
  // either way.
  const defrostHotGasFans =
       t.defrostStage === 2
    && (dpSp.defrostTermType === 1 || dpSp.defrostTermType === 2);

  // 1. Decide compressor run state for this tick.  Mirrors GRC
  //    `ManageCompressors()` (Compressor.c §3).  Order of precedence:
  //      a) Hard trip (incl. phase-loss ≥20 s, lockout, SAFE_OFF, LP, permit)
  //      b) Manual force-on / force-off
  //      c) Pumpdown countdown active → keep comp ON until exit conditions
  //      d) Manual pumpdown switch open while running → enter pumpdown
  //      e) Auto cut-out (suction <= cutOutP) → enter pumpdown OR drop
  //      f) Auto cut-in (suction >= cutInP) → start (gated by prove + ASC)
  let shouldRun = t.compressorOn;
  const sucP = t.sensors.suctionP.value;
  const lowSuctionHardCutout = Math.max(t.setpoints.cutOutP - 10, 5);

  if (hardTrip) {
    shouldRun = false;
    if (t.pumpdownSecRemaining > 0) t.pumpdownSecRemaining = 0;
  } else if (t.defrostStage === 2 || t.defrostStage === 3) {
    // Defrost ACTIVE or DRIP — compressor is unconditionally OFF.  Kills
    // any in-flight pumpdown so we don't restart it on the way back up.
    shouldRun = false;
    if (t.pumpdownSecRemaining > 0) t.pumpdownSecRemaining = 0;
  } else if (t.manualMode === 'force-off' || !t.enabled) {
    shouldRun = false;
    if (t.pumpdownSecRemaining > 0) t.pumpdownSecRemaining = 0;
  } else if (t.manualMode === 'force-on') {
    // Force-on still respects the prove timer — the field is destroyed by
    // starting a cold compressor.  GRC behaviour: panel BYPASS skips
    // FAIL_* timers but does NOT skip the prove window.
    if (!proveBlocked) shouldRun = true;
    else               shouldRun = false;
  } else if (t.compressorOn) {
    // Currently running.  Check pumpdown exit / auto cut-out.
    if (t.pumpdownSecRemaining > 0) {
      // In pumpdown — bleed timer + suction floor.
      t.pumpdownSecRemaining = Math.max(0, t.pumpdownSecRemaining - dt);
      if (sucP <= t.setpoints.cutOutP || t.pumpdownSecRemaining === 0) {
        shouldRun = false;
        t.pumpdownSecRemaining = 0;
      }
    } else if (manualPumpdown) {
      // Operator threw the panel pumpdown switch — enter pumpdown.  Use a
      // 60 s safety ceiling (GRC has no explicit max; relies on the LP
      // switch + cut-out).
      t.pumpdownSecRemaining = 60;
    } else if (sucP <= t.setpoints.cutOutP) {
      // Auto cut-out.  If suction is already at the hard-cutout floor or
      // pumpDownMode is NONE (0), drop straight to off; otherwise
      // pumpdown for up to 60 s.
      if (sucP <= lowSuctionHardCutout || t.setpoints.pumpDownMode === 0) {
        shouldRun = false;
      } else {
        t.pumpdownSecRemaining = 60;
      }
    }
  } else {
    // Currently off — check cut-in conditions.  Anti-short-cycle is the
    // only min-off gate (GRC has no min-on).  Prove timer must have
    // expired.
    const offDurSec = (ctx.nowMs - t.lastDefrostEnd) / 1000;
    const ascMet    = offDurSec >= t.setpoints.minOffTime;
    if (!proveBlocked && ascMet && sucP >= t.setpoints.cutInP) {
      shouldRun = true;
    }
  }

  if (shouldRun !== t.compressorOn) {
    t.compressorOn = shouldRun;
    if (!shouldRun) {
      // Mark off-time so the anti-short-cycle gate (minOffTime) starts
      // counting from this exact moment.
      t.lastDefrostEnd = ctx.nowMs;
    }
  }

  // 1b. Derive `compressorStatus` for SCADA.  Order of precedence matches
  //     GRC `GetCompressorStatus()` so the same UI strings light up.
  if (phaseSysOff)                         t.compressorStatus = 9;  // SYSTEM_OFF
  else if (!sf.autoRunPermissive)          t.compressorStatus = 7;  // SWITCH_OFF
  else if ((sf.lockoutMask & ~0x08) !== 0  // any latched fault except prove
        || (t.safeOffMask ?? 0) !== 0)     t.compressorStatus = 10; // ERROR
  else if (t.defrostStage === 2 || t.defrostStage === 3) {
    // Defrost in progress — pick the GRC code matching the trigger.
    if (t.defrostType === 2)               t.compressorStatus = 14; // DEFROST_MANUAL
    else if (t.defrostType === 1)          t.compressorStatus = 3;  // DEFROST_OVERRIDE (TIMED)
    else                                   t.compressorStatus = 2;  // DEFROST (TREND, future)
  }
  else if (manualPumpdown && t.compressorOn) t.compressorStatus = 5;  // SWITCH_PUMPDOWN
  else if (t.pumpdownSecRemaining > 0)     t.compressorStatus = 12; // PUMPDOWN
  else if (proveBlocked && !t.compressorOn) t.compressorStatus = 4; // PROVE
  else if (t.compressorOn)                 t.compressorStatus = 1;  // AUTO_RUN
  else                                     t.compressorStatus = 0;  // AUTO_STANDBY

  // 1c. Power-fail latched alarm (independent of the per-tick
  //     `lastTickMs` gap detector at §7b).  Driven by `phaseLossSec`.
  latchAlarm('FAIL_POWER', 'Power-Fail (sustained phase loss)',
    t.phaseLossSec >= Math.max(60, t.setpoints.powerFailMinutes * 60));

  // 2. Advance compressor runtime / amps.  No more "run-prove via DI6"
  //    check — the GRC mechanism is the crankcase prove timer above.
  if (t.compressorOn) {
    t.compressorRuntimeSec += dt;
    // Realistic-ish RLA: 12-18 amps under typical load.
    t.compressorAmps = 14.0 + (t.sensors.dischargeP.value - 150) * 0.02;
  } else {
    t.compressorAmps = Math.max(0, t.compressorAmps - dt * 5);
    // Reset live runtime counter when stopped (GRC tracks lifetime hours
    // separately in `totalRuntimeHours` — accumulated by Nova / UI).
    t.compressorRuntimeSec = 0;
  }

  // 3. Drive the sensors toward equilibrium values that depend on state.
  //    When idle, suction PSI rises with the heat load on the box — model
  //    that by drifting toward (cutInP + 5) so the bang-bang cycle
  //    naturally restarts the compressor without operator intervention.
  //    When running, the suction-side temperature equilibrium is coupled
  //    to the EXV opening so the Phase 4 PID actually has feedback:
  //      SH_eq = 30 °F at EXV=0 % (starved evaporator)
  //      SH_eq =  3 °F at EXV=100 % (flooded evaporator)
  //    SH is interpolated linearly across `exvMinPct..exvMaxPct`; the
  //    PID drives EXV opening so SH lands on `superheatTarget`.
  const ambient = t.sensors.ambientT.value;
  const offSuctionTarget = Math.max(t.setpoints.cutInP + 5, 25);
  // Suction-side coupling: more EXV → less superheat.  Compute exv span
  // and current saturation temp here (section 4 recomputes both for the
  // PID — this is a cheap forward reference, not a precision-sensitive
  // path).
  const exvMinPctSec3 = Math.max(0, Math.min(100, t.setpoints.exvMinPct));
  const exvMaxPctSec3 = Math.max(exvMinPctSec3, Math.min(100, t.setpoints.exvMaxPct));
  const exvSpanSec3   = Math.max(1, exvMaxPctSec3 - exvMinPctSec3);
  const exvFracSec3   = Math.max(0, Math.min(1, (t.exvOpenPct - exvMinPctSec3) / exvSpanSec3));
  const shEqF         = 30 - 27 * exvFracSec3;
  const satTsec3      = tsatF(t.setpoints.refrigerantType, t.sensors.suctionP.value);
  const sucT_run      = satTsec3 + shEqF;
  const target = {
    suctionP:   t.compressorOn ? Math.max(t.setpoints.cutOutP - 5, 25) : offSuctionTarget,
    suctionT:   t.compressorOn ? sucT_run : ambient - 5,
    dischargeP: t.compressorOn ? Math.min(140 + (ambient - 70) * 2.5, 350) : ambient + 30,
    dischargeT: t.compressorOn ? 130 + (ambient - 70) : ambient + 5,
    llsT:       t.compressorOn ? Math.min(95 + (ambient - 70) * 0.3, 130) : ambient,
    oilP:       t.compressorOn ? 55 + (t.sensors.dischargeP.value - 150) * 0.05 : 0,
    ambientT:   ambient, // free-running, set by user / chaos
  };
  const tau = 0.05; // exponential approach rate (per tick)
  const lerp = (a: number, b: number) => a + (b - a) * tau;
  t.sensors.suctionP.value   = lerp(t.sensors.suctionP.value,   target.suctionP);
  t.sensors.suctionT.value   = lerp(t.sensors.suctionT.value,   target.suctionT);
  t.sensors.dischargeP.value = lerp(t.sensors.dischargeP.value, target.dischargeP);
  t.sensors.dischargeT.value = lerp(t.sensors.dischargeT.value, target.dischargeT);
  t.sensors.llsT.value       = lerp(t.sensors.llsT.value,       target.llsT);
  t.sensors.oilP.value       = lerp(t.sensors.oilP.value,       target.oilP);

  // 3b. Mirror live values onto the sensor-bus analog boards so polls of
  //     /api/sensors reflect the same physics as /api/triton.  Board 0 is
  //     temperature (suction T, discharge T, LLS T, ambient T) and board 1
  //     is pressure (suction P, discharge P, oil P, demand).  All values
  //     are stored as int *10 to match the existing storage convention,
  //     except channel 4 of the pressure board which carries the raw 0-100
  //     demand percentage.
  if (ctx.writeTempBoard) {
    ctx.writeTempBoard([
      Math.round(t.sensors.suctionT.value   * 10),
      Math.round(t.sensors.dischargeT.value * 10),
      Math.round(t.sensors.llsT.value       * 10),
      Math.round(t.sensors.ambientT.value   * 10),
    ]);
  }
  if (ctx.writePressBoard) {
    ctx.writePressBoard([
      Math.round(t.sensors.suctionP.value   * 10),
      Math.round(t.sensors.dischargeP.value * 10),
      Math.round(t.sensors.oilP.value       * 10),
      Math.round(t.demand),
    ]);
  }

  // 4. EXV PID — GRC-faithful port of `ExpansionValve.c::PIDExpansion()` +
  //    `PIDCtrl.c::PIDController()`.  Controls superheat (SH = SuctionT -
  //    Tsat(SuctionP)) toward `superheatTarget` by modulating the EXV
  //    opening 0..100 %.  Run-time invariants:
  //      • LLS gate = compressorOn (Triton has no separate LLS DO).  When
  //        the compressor is OFF the EXV is forced to `exvMinPct` and the
  //        PID state (integrator, prevErr, warm-up timer) is fully reset.
  //      • Just after cut-in: a `exvWarmupMin`-minute warm-up window holds
  //        the EXV at `exvStartPct` and seeds the integrator so the first
  //        PID sample matches `exvStartPct` exactly (GRC `PIDCtrl.c::
  //        PIDLoopReset()` behaviour).  No bump on hand-off.
  //      • PID is recomputed at most once per `exvSamplePeriod` seconds.
  //        Between samples the live `exvOpenPct` is slewed toward the most
  //        recent PID target by ~11.25 %/s (mirrors GRC's 10-PWM-counts/cycle
  //        of the 222-count 0-100 % range, every 0.4 s ThreadSerialCom).
  //      • Anti-windup: integrator is one-sided (clamped to [0, WindupLimit])
  //        with `WindupLimit = Range / (0.022 * Ki)` — inversely proportional
  //        to Ki.  Mirrors GRC `PIDCtrl.c:115-132`.  Output clamped to
  //        [exvMinPct, exvMaxPct].
  const sucPsi = t.sensors.suctionP.value;
  const sucTf  = t.sensors.suctionT.value;
  const satT   = tsatF(t.setpoints.refrigerantType, sucPsi);
  const superheat = sucTf - satT;

  // Defensive init for the Phase 4 PID state — old persisted snapshots
  // (or fresh `createDefaultTriton` calls predating this version) may
  // load without it.
  if (!t.exvPid) {
    t.exvPid = { intError: 0, prevErr: 0, lastPidMs: 0, warmupSec: 0, targetPct: 0 };
  }
  const pid = t.exvPid;
  const sp  = t.setpoints;
  const exvMin = Math.max(0, Math.min(100, sp.exvMinPct));
  const exvMax = Math.max(exvMin, Math.min(100, sp.exvMaxPct));
  const range  = Math.max(1, exvMax - exvMin);

  if (!t.compressorOn) {
    // LLS off — reset to MIN, clear all PID state.  Mirrors GRC
    // `PIDExpansion()` early-return when LLS=off.
    // Safe-mode override (deferral #4): if a hard-trip / safeOff is
    // active, the operator can choose a different EXV position via
    // `safePolicyExvPct` (default 0 — close, prevent flood-back).
    const inSafeMode = (t.safeOffMask ?? 0) !== 0 || sf.lockoutMask !== 0;
    const safeExv    = inSafeMode
      ? Math.max(exvMin, Math.min(exvMax, (sp as any).safePolicyExvPct ?? exvMin))
      : exvMin;
    t.exvOpenPct       = safeExv;
    pid.intError       = 0;
    pid.prevErr        = 0;
    pid.warmupSec      = 0;
    pid.targetPct      = safeExv;
    pid.lastPidMs      = 0;
  } else if (t.manualMode === 'force-on' || t.manualMode === 'force-off') {
    // Operator manual override — slew toward the manual-percent setpoint.
    pid.targetPct = Math.max(exvMin, Math.min(exvMax, sp.exvManualPct));
  } else {
    // Auto mode while running.  Advance warmup timer.
    pid.warmupSec += dt;
    const warmupLimit = Math.max(0, sp.exvWarmupMin) * 60;

    if (pid.warmupSec < warmupLimit) {
      // Warm-up: hold at StartPercent and seed the integrator so the
      // first real PID sample produces the same output (no step on
      // hand-off).  GRC `PIDLoopReset()` formula: IntError such that
      // `Ki * IntError * 0.022 == StartOutput - Kp*err` (P term takes
      // current err into account so the seed is exact).
      const startOut = Math.max(exvMin, Math.min(exvMax, sp.exvStartPct));
      pid.targetPct = startOut;
      const err = Math.round((superheat - sp.superheatTarget) * 10) / 10;
      const ki  = Math.max(0.001, sp.exvKi);
      pid.intError = (startOut - sp.exvKp * err) / (ki * 0.022);
      // Clamp seed into the one-sided window so anti-windup engages on
      // the very first sample if needed.
      const wind  = range / (0.022 * ki);
      if (pid.intError < 0)    pid.intError = 0;
      if (pid.intError > wind) pid.intError = wind;
      pid.prevErr   = err;
      pid.lastPidMs = ctx.nowMs;
    } else {
      // Past warm-up — gate PID computation to the U-period.
      const periodMs = Math.max(100, sp.exvSamplePeriod * 1000);
      if (pid.lastPidMs === 0 || (ctx.nowMs - pid.lastPidMs) >= periodMs) {
        const err  = Math.round((superheat - sp.superheatTarget) * 10) / 10;
        const ki   = Math.max(0.001, sp.exvKi);
        const wind = range / (0.022 * ki);
        // Integrator update — one-sided positive only (GRC clamps to
        // [0, WindupLimit] before computing the I-term).
        pid.intError += err;
        if (pid.intError < 0)    pid.intError = 0;
        if (pid.intError > wind) pid.intError = wind;
        const pTerm = sp.exvKp * err;
        const iTerm = ki * pid.intError * 0.022;
        const dTerm = sp.exvKd * (err - pid.prevErr);
        const out = pTerm + iTerm + dTerm;
        let target = out;
        if (target < exvMin) target = exvMin;
        if (target > exvMax) target = exvMax;
        pid.targetPct = target;
        pid.prevErr   = err;
        pid.lastPidMs = ctx.nowMs;
        // Phase 7 — PIDLog entry.  Mirrors GRC `PIDLogWrite()` call from
        // `PIDController()` at PIDCtrl.c:166 — same field order, same
        // post-clamp `Output`.  Type 0 = EXV (only PID Triton has so far).
        pushLog(t.logs!.pid, {
          ts: ctx.nowMs, type: 0,
          P: pTerm, I: iTerm, D: dTerm,
          output: target, error: err,
        }, 256);
      }
    }
  }

  // Slew the live EXV toward the PID target at ~11.25 %/s (GRC PWM_INC=10
  // counts/cycle of a 222-count range, every 0.4 s ThreadSerialCom).
  if (t.compressorOn) {
    const slew = 11.25 * dt;
    const delta = pid.targetPct - t.exvOpenPct;
    if (Math.abs(delta) <= slew) t.exvOpenPct = pid.targetPct;
    else                          t.exvOpenPct += Math.sign(delta) * slew;
    if (t.exvOpenPct < exvMin) t.exvOpenPct = exvMin;
    if (t.exvOpenPct > exvMax) t.exvOpenPct = exvMax;
  }

  // 4b. Leak / low-charge detection.
  //     A refrigerant leak shows up as the EXV opening further and further
  //     while superheat refuses to come down — there is simply not enough
  //     liquid to feed the evaporator.  We sample the EWMA of both signals
  //     once the compressor has been running for a stabilisation window
  //     (2 × warmUpSec, default 120 s); when the filtered superheat is
  //     above target+margin AND the filtered EXV is at/above the open
  //     threshold for `leakSustainMinutes` continuously, we latch a
  //     LEAK_SUSP alarm.  Conditions reset on compressor stop or defrost.
  const ld = t.leakDetect;
  if (!t.compressorOn || t.defrostActive) {
    ld.warmupSec    = 0;
    ld.sustainedSec = 0;
  } else {
    ld.warmupSec += dt;
  }
  if (t.setpoints.leakDetectEnabled && t.compressorOn && !t.defrostActive) {
    // EWMA with τ ≈ 60 s (α = dt/τ = 0.5/60 ≈ 0.0083).
    const alpha = dt / 60;
    // Seed the filters on the first sample of a new run.
    if (ld.warmupSec <= dt) {
      ld.shAvgF    = superheat;
      ld.exvAvgPct = t.exvOpenPct;
    } else {
      ld.shAvgF    += alpha * (superheat        - ld.shAvgF);
      ld.exvAvgPct += alpha * (t.exvOpenPct     - ld.exvAvgPct);
    }
    // Only credit sustain time once past the stabilisation gate.
    const stableAfter = Math.max(60, t.setpoints.warmUpSec * 2);
    if (ld.warmupSec >= stableAfter) {
      const shHigh  = ld.shAvgF    >= t.setpoints.superheatTarget + t.setpoints.leakSuperheatMarginF;
      const exvHigh = ld.exvAvgPct >= t.setpoints.leakExvOpenPct;
      if (shHigh && exvHigh) ld.sustainedSec += dt;
      else                   ld.sustainedSec  = 0;
      if (ld.sustainedSec >= t.setpoints.leakSustainMinutes * 60) {
        ld.leakAlarmActive = true;
      }
    }
  } else {
    // Detector disabled — keep the live filters frozen but never alarm.
    ld.sustainedSec    = 0;
    ld.leakAlarmActive = false;
  }

  // 5. Condenser fan staging.  Mirrors GRC `StageCondenserFans()` +
  //    `CondenserPID()` + `SetLeadFans()` in CondenserFans.c.
  //
  //    Algorithm:
  //      a) Gate: if compressor is OFF, in error, or in hot-gas defrost,
  //         force ALL fans off and zero the live VFD output (mirrors GRC
  //         `CtrlCondenserFansOff()` from Compressor.c::CompressorOff).
  //         The condenser cools by ambient natural convection from there.
  //      b) Otherwise compute the head-pressure target via TargetDischarge:
  //           FIXED (mode=0) or OAT-invalid → operator setpoint
  //           FLOATING/BALANCED  → P_sat(OAT + approach), clamped to
  //                                 [condMinHeadP, condMaxHeadP].
  //      c) STAGED mode (`condFanVfdMode==0`): per-fan bang-bang with
  //         hysteresis on dischargeP.  Stage j ON when discharge >= target +
  //         diffOn[j]; OFF when discharge <= target + diffOff[j] (or when
  //         using FIXED, the absolute thresholds in fanStageOnP/OffP).
  //         Fans are commanded in lead-order (lead, lead+1, ..., lead+n-1),
  //         so rotation actually re-distributes runtime.
  //      d) VFD mode (`condFanVfdMode==1`): same target, but a single PID
  //         output (`condenserVfdPct`) modulates the VFD speed reference
  //         from `condFanVfdMinPct` to `condFanVfdMaxPct`.  Slew-rate
  //         limited at ~11.25 %/s to mirror GRC's PWM_INC=10.  All
  //         physical fans run; the VFD modulates them together.
  //      e) Lead-fan rotation: when `rotateHours > 0`, every tick
  //         accumulates runtime on the lead slot.  Once it exceeds
  //         `rotateHours * 3600`, the lead index advances by one and the
  //         counter resets — exactly mirroring GRC `SetLeadFans()`.
  const condSp = t.setpoints;
  const condErrTrip = (t.safeOffMask ?? 0) !== 0 || sf.lockoutMask !== 0;
  // Gas-defrost ACTIVE overrides the comp-off gate: hot gas pumped through
  // the evaporator builds head pressure that condenser fans must dump.
  // Outside that window, fans follow the normal "comp on → fans allowed"
  // rule.  Mirrors GRC `IsDefrostWithCondenser()` + `DefrostCondenserFans()`.
  const condGateOff = condErrTrip
    || (defrostHotGasFans ? false : !t.compressorOn || t.defrostActive);

  // Defensive init for the Phase 3.5 fields — pre-Phase-3.5 snapshots
  // (or test fixtures) may load without them.
  if (!Array.isArray(t.condenserFans)
      || t.condenserFans.length !== t.condenserFanCount) {
    const prev = Array.isArray(t.condenserFans) ? t.condenserFans : [];
    t.condenserFans = Array.from({ length: t.condenserFanCount }, (_, i) =>
      prev[i] ?? { on: false, runtimeSec: 0 });
  }
  if (typeof t.condenserLeadIndex !== 'number'
      || t.condenserLeadIndex < 0
      || t.condenserLeadIndex >= t.condenserFanCount) {
    t.condenserLeadIndex = 0;
  }
  if (typeof t.condenserVfdPct !== 'number') t.condenserVfdPct = 0;
  const fans = t.condenserFans!;
  const lead = t.condenserLeadIndex!;

  if (condGateOff) {
    // ── (a) Safe-mode policy applied to condenser fans (deferral #4).
    //   When the gate is closed (lockoutMask | safeOffMask | comp-off | defrost),
    //   the operator picks the cond-fan disposition via
    //   `safePolicyCondFans`: 0=ALL_OFF, 1=HOLD_LAST, 2=ALL_ON.
    //   In VFD mode (`condFanVfdMode==1`), `safePolicyCondVfdPct` snaps
    //   the VFD to the operator-configured speed (default 100 % so
    //   residual head pressure is dumped).  The PID integrator is
    //   always reset so restart re-converges from scratch.
    const policyFans = (condSp as any).safePolicyCondFans ?? 0;
    const policyVfd  = Math.max(0, Math.min(100,
      (condSp as any).safePolicyCondVfdPct ?? 0));
    if (policyFans === 2) {
      for (const f of fans) { f.on = true; f.runtimeSec += dt; }
      t.condenserFanStage = t.condenserFanCount;
    } else if (policyFans === 1) {
      // HOLD_LAST: leave fans untouched.  Runtime accrues for any that
      // are on so rotation accounting stays consistent.
      for (const f of fans) if (f.on) f.runtimeSec += dt;
      t.condenserFanStage = fans.filter(f => f.on).length;
    } else {
      // ALL_OFF (default for backward compatibility on older snapshots).
      for (const f of fans) f.on = false;
      t.condenserFanStage = 0;
    }
    if (condSp.condFanVfdMode === 1) {
      t.condenserVfdPct = policyVfd;
    } else {
      t.condenserVfdPct = 0;
    }
    // Reset the PID integrator so a restart re-converges from scratch
    // instead of dumping a saturated I-term onto the VFD.  Mirrors GRC
    // `PWM_ResetChannel()` clearing PIDCtrl.IntError on shutdown.
    const cp: any = (t as any).condPid;
    if (cp) { cp.intError = 0; cp.prevErr = 0; cp.lastPidMs = 0; cp.targetPct = 0; }
    // Don't drift dischargeTargetP — keep the last-computed target visible
    // so SCADA shows what fans WOULD chase if comp restarted.
  } else if (defrostHotGasFans) {
    // ── (a') gas-defrost ACTIVE — force all fans on at max, regardless
    //         of dischargeP.  Mirrors GRC `DefrostCondenserFans()` +
    //         `PWM_MaxOutput()`.  Runtime accrues so rotation still
    //         happens normally.
    for (const f of fans) {
      f.on = true;
      f.runtimeSec += dt;
    }
    t.condenserFanStage = t.condenserFanCount;
    if (condSp.condFanVfdMode === 1) {
      t.condenserVfdPct = Math.max(0, Math.min(100, condSp.condFanVfdMaxPct));
    }
  } else {
    // ── (b) compute head-pressure target (TargetDischarge equivalent)
    let headTarget: number;
    let useDiffs = false;
    const oatValid = t.sensors.ambientT.valid;
    if (condSp.condenserMode === 0 || !oatValid) {
      headTarget = condSp.condFanVfdSetpointP;
    } else {
      const psat = psatF(condSp.refrigerantType, t.sensors.ambientT.value + condSp.condApproachF);
      let p = psat;
      if (condSp.condMinHeadP > 0 && p < condSp.condMinHeadP) p = condSp.condMinHeadP;
      if (condSp.condMaxHeadP > 0 && p > condSp.condMaxHeadP) p = condSp.condMaxHeadP;
      headTarget = p;
      useDiffs = true;
    }
    t.dischargeTargetP = Math.round(headTarget * 10) / 10;
    const dischargeP = t.sensors.dischargeP.value;

    if (condSp.condFanVfdMode === 1) {
      // ── (d) VFD mode — full GRC-faithful PID (deferral #3).  Mirrors
      //   `CondenserPID()` → `PIDLoop()` → `PIDController()` from
      //   CondenserFans.c / PIDCtrl.c.  PID arithmetic is done in the
      //   raw [PWM_MIN..PWM_MAX] = [55..277] range exactly like GRC; the
      //   resulting PWM count is converted to % and clamped to the
      //   user-configured [condFanVfdMinPct, condFanVfdMaxPct] band
      //   (analogous to GRC `Settings.PWM[].Min/Max` post-clamp).
      const vfdMin = Math.max(0, Math.min(100, condSp.condFanVfdMinPct));
      const vfdMax = Math.max(vfdMin, Math.min(100, condSp.condFanVfdMaxPct));
      // GRC PWM constants (PWM.h §41-44).
      const PWM_MIN = 55;
      const PWM_MAX = 277;
      const PWM_RANGE = PWM_MAX - PWM_MIN;       // 222
      const PWM_INC = 10;                         // counts/cycle
      const SCALAR  = 0.022;                      // GRC PIDController scalar

      // Defensive init — the PID struct is optional in TritonState.
      const cp: any = (t as any).condPid ??
        ((t as any).condPid = { intError: 0, prevErr: 0, lastPidMs: 0, targetPct: 0 });

      const periodMs = Math.max(100, ((condSp as any).condU ?? 3) * 1000);
      const kp = (condSp as any).condP ?? 5;
      const ki = Math.max(0.001, (condSp as any).condI ?? 15);
      const kd = (condSp as any).condD ?? 2;
      const wind = PWM_RANGE / (SCALAR * ki);

      // Gate PID re-compute to U-period (GRC `pid->Timer`).
      if (cp.lastPidMs === 0 || (ctx.nowMs - cp.lastPidMs) >= periodMs) {
        // CondenserFanError: actual - target (PSI).  Round to .1 PSI to
        // match GRC `IntErr = (CurError + .05) * 10` quantisation.
        const errRaw = dischargeP - headTarget;
        const err = Math.round((errRaw + 0.05) * 10) / 10;
        // One-sided positive integrator clamped to [0, WindupLimit].
        cp.intError += err;
        if (cp.intError < 0)    cp.intError = 0;
        if (cp.intError > wind) cp.intError = wind;
        // D-term ignored on first sample (matches GRC PrevError sentinel).
        const dErr = (cp.lastPidMs === 0) ? 0 : (err - cp.prevErr);
        const P = kp * err;
        const I = ki * cp.intError * SCALAR;
        const D = kd * dErr;
        let out = P + I + D;
        // Clamp to PID range [0..PWM_RANGE], then shift to [PWM_MIN..PWM_MAX].
        if (out < 0)         out = 0;
        if (out > PWM_RANGE) out = PWM_RANGE;
        const pwmTarget = out + PWM_MIN;
        // Convert raw PWM target → % and clamp to user min/max band.
        let pctTarget = ((pwmTarget - PWM_MIN) / PWM_RANGE) * 100;
        if (pctTarget < vfdMin) pctTarget = vfdMin;
        if (pctTarget > vfdMax) pctTarget = vfdMax;
        cp.targetPct = pctTarget;
        cp.prevErr   = err;
        cp.lastPidMs = ctx.nowMs;
        // Phase 7-style PIDLog entry — type 1 = COND (reserved alongside
        // type 0 = EXV).  Same field layout: P/I/D/output/error.
        pushLog(t.logs!.pid, {
          ts: ctx.nowMs, type: 1,
          P, I, D,
          output: pctTarget, error: err,
        }, 256);
      }

      // Slew rate: GRC bumps PWM_INC=10 PWM counts per call to PIDLoop
      // (called every ThreadSerialCom iteration, ~0.4 s) → ~11.25 %/s.
      const slewPctPerSec = (PWM_INC / PWM_RANGE) / 0.4 * 100;
      const slew = slewPctPerSec * dt;
      const delta = cp.targetPct - t.condenserVfdPct;
      if (Math.abs(delta) <= slew) t.condenserVfdPct = cp.targetPct;
      else                          t.condenserVfdPct += Math.sign(delta) * slew;
      if (t.condenserVfdPct < vfdMin) t.condenserVfdPct = vfdMin;
      if (t.condenserVfdPct > vfdMax) t.condenserVfdPct = vfdMax;
      // In VFD mode every fan runs whenever the compressor runs.
      for (const f of fans) f.on = true;
      t.condenserFanStage = t.condenserFanCount;
    } else {
      // ── (c) STAGED mode — per-fan bang-bang in lead-order.
      t.condenserVfdPct = 0;
      // Walk j=0..count-1 in lead-order; flip the fan at (lead+j)%count.
      // Determine current ON-count (stage) from the live array so the
      // operator can hand-toggle a fan in the panel without us forgetting.
      let stage = fans.filter(f => f.on).length;
      for (let j = 0; j < t.condenserFanCount; j++) {
        const onP  = useDiffs
          ? headTarget + (condSp.fanDiffOnP[j]  ?? 999)
          : (condSp.fanStageOnP[j]  ?? 999);
        const offP = useDiffs
          ? headTarget + (condSp.fanDiffOffP[j] ?? 0)
          : (condSp.fanStageOffP[j] ?? 0);
        if (stage <= j && dischargeP >= onP) stage = j + 1;
        else if (stage > j && dischargeP <= offP) stage = j;
      }
      stage = Math.max(0, Math.min(stage, t.condenserFanCount));
      // Apply stage in lead-order.
      for (let j = 0; j < t.condenserFanCount; j++) {
        const idx = (lead + j) % t.condenserFanCount;
        fans[idx].on = j < stage;
      }
      t.condenserFanStage = stage;
    }

    // ── (e) Per-fan runtime accumulation + lead rotation.
    for (const f of fans) {
      if (f.on) f.runtimeSec += dt;
    }
    if (condSp.rotateHours > 0 && fans.length > 1) {
      const leadFan = fans[lead];
      if (leadFan && leadFan.runtimeSec > condSp.rotateHours * 3600) {
        // Rotate to next fan; reset the (former) lead's runtime so the
        // counter tracks lead-slot occupancy, matching GRC LeadRuntime.
        leadFan.runtimeSec = 0;
        t.condenserLeadIndex = (lead + 1) % t.condenserFanCount;
      }
    }
  }

  // 6. Latch alarms when a sensor crosses its threshold.  Acked alarms
  //    stay in the list until they go inactive (operator acknowledges
  //    before clearing); inactive+acked alarms are pruned.
  const maybeAlarm = (code: string, label: string, condition: boolean) => {
    const existing = t.alarms.find(a => a.code === code);
    if (condition) {
      if (existing) {
        existing.active = true;
      } else {
        t.alarms.push({ code, label, active: true, acked: false, timestamp: ctx.nowMs });
      }
    } else if (existing) {
      existing.active = false;
      if (existing.acked) {
        t.alarms = t.alarms.filter(a => a !== existing);
      }
    }
  };
  maybeAlarm('SUC_LOW_P',  'Suction Pressure Low',  t.sensors.suctionP.value   < t.sensors.suctionP.lowAlarm);
  maybeAlarm('SUC_HIGH_P', 'Suction Pressure High', t.sensors.suctionP.value   > t.sensors.suctionP.highAlarm);
  maybeAlarm('DIS_HIGH_P', 'Discharge Pressure High', t.sensors.dischargeP.value > t.sensors.dischargeP.highAlarm);
  maybeAlarm('DIS_HIGH_T', 'Discharge Temp High',     t.sensors.dischargeT.value > t.sensors.dischargeT.highAlarm);
  maybeAlarm('OIL_LOW_P',  'Oil Pressure Low',        t.compressorOn && t.sensors.oilP.value < t.sensors.oilP.lowAlarm);
  // Leak / low-charge — driven by the EWMA detector above.  When the
  // operator acks the alarm and we want to give the detector a fresh
  // chance, we also clear the latch so a renewed leak signature can
  // re-trigger after the same sustain window.
  maybeAlarm('LEAK_SUSP',  'Refrigerant Leak Suspected (high SH + EXV wide open)',
             t.leakDetect.leakAlarmActive);
  {
    const ackedLeak = t.alarms.find(a => a.code === 'LEAK_SUSP' && a.acked);
    if (ackedLeak && !ackedLeak.active) {
      t.leakDetect.leakAlarmActive = false;
      t.leakDetect.sustainedSec    = 0;
    }
  }

  // 7. GRC FAILURE machinery — timed thresholds with mode-respecting
  //    actions.  Each `t.failures.*` channel has a `delaySec` and a
  //    `mode` (0=ALARM_ONLY, 1=SAFE_OFF, 2=RUN_THROUGH).  We integrate
  //    seconds-above-threshold per channel; when the timer expires we
  //    latch the corresponding FAIL_* alarm and, for SAFE_OFF mode,
  //    set a bit in `safeOffMask` that participates in `hardTrip` next
  //    tick.  Latches and timers all clear via the ack-all command.
  if (!t.failureStates) {
    t.failureStates = {
      suctionP:     { overSec: 0, tripped: false },
      dischargeP:   { overSec: 0, tripped: false },
      oilP:         { overSec: 0, tripped: false },
      suctionT:     { overSec: 0, tripped: false },
      superheatLow: { overSec: 0, tripped: false },
      dischargeT:   { overSec: 0, tripped: false },
      llsT:         { overSec: 0, tripped: false },
      ambientT:     { overSec: 0, tripped: false },
    };
  }
  if (typeof t.safeOffMask !== 'number') t.safeOffMask = 0;

  const fs = t.failureStates;
  const evalFail = (
    code: string, label: string,
    cfg: TritonFailureMode, st: TritonFailureState,
    cond: boolean,
  ) => {
    // RUN_THROUGH (2) means: alarm only, never set safeOff bit.  ALARM_ONLY
    // (0) — same as RUN_THROUGH for the simulator's purposes.  SAFE_OFF
    // (1) — also engages the safeOffMask once tripped.
    if (cond) st.overSec += dt;
    else      st.overSec  = 0;

    // GRC `ManageAlarm` semantics: trip when the condition has been
    // continuously true for >= delaySec.  delaySec=0 means "trip on
    // the first tick the condition is true" — NOT "trip unconditionally".
    // The `cond` guard prevents `0 >= 0` from latching on a clean boot.
    if (!st.tripped && cond && st.overSec >= Math.max(0, cfg.delaySec)) {
      st.tripped = true;
    }
    // Untripped (cleared by ack-all path setting tripped=false) → also
    // clear the one-shot pumpdown arm so the next event can re-arm it.
    if (!st.tripped) st._pumpdownArmed = false;
    const bit = TRITON_ALARM_BITS[code] ?? -1;
    if (st.tripped && cfg.mode === 1 && bit >= 0) {
      t.safeOffMask = (t.safeOffMask ?? 0) | (1 << bit);
    }
    maybeAlarm(code, label, st.tripped);
  };

  // Phase 6 — GRC `IsCompressorWarmedUp()` proxy.  GRC requires the
  // compressor to have been running for `Failure[FAIL_SUPERHEAT].Timer
  // * T_MINS` (or 60 s default) before checking superheat / oil / low-
  // suction conditions, so transient suction surges and the cold-start
  // SH overshoot don't false-trip on every cut-in.  We use the SUPERHEAT
  // channel's `delaySec` (in seconds) as the warm-up window — same field
  // GRC uses (`Settings.Failure[FAIL_SUPERHEAT].Timer * T_MINS`).
  const warmupSec     = Math.max(60, t.failures.suctionT.delaySec);
  const compWarmedUp  = t.compressorOn && (t.compressorRuntimeSec >= warmupSec);

  // Helper for the GRC "pump down + set failure" pattern: when these
  // FAILs trip, GRC calls `CompressorPumpdown(); SetCompressorFailure();`
  // (Compressor.c §LowSuctionAlarm / OilPressureChk / SuperHeatChk).
  // We arm a 60 s pumpdown countdown the FIRST tick the latch trips so
  // refrigerant is recovered before the comp drops via safeOffMask.
  const armPumpdownOnce = (st: TritonFailureState) => {
    if (st.tripped && !st._pumpdownArmed) {
      st._pumpdownArmed = true;
      if (t.compressorOn && t.pumpdownSecRemaining <= 0) {
        t.pumpdownSecRemaining = 60;
      }
    }
  };

  // FAIL_SUPERHEAT — superheat above target+window for delaySec while running
  evalFail('FAIL_SUPERHEAT', 'Superheat High (timed)',
    t.failures.suctionT, fs.suctionT,
    compWarmedUp && !t.defrostActive
      && superheat > t.setpoints.superheatTarget + t.setpoints.superheatWindowHighF);
  armPumpdownOnce(fs.suctionT);
  // FAIL_SUPERHEATLOW — superheat below the floodback floor
  evalFail('FAIL_SUPERHEATLOW', 'Superheat Low (floodback risk)',
    t.failures.suctionT, fs.superheatLow,
    compWarmedUp && !t.defrostActive
      && superheat < t.setpoints.superheatLowF);
  armPumpdownOnce(fs.superheatLow);
  // FAIL_DISCHARGE — discharge pressure over high alarm.  GRC forces the
  // delay timer to 0 (Compressor.c:1529) — high discharge is immediate.
  evalFail('FAIL_DISCHARGE', 'Discharge Pressure (sustained over)',
    { ...t.failures.dischargeP, delaySec: 0 }, fs.dischargeP,
    t.sensors.dischargeP.value > t.sensors.dischargeP.highAlarm);
  // FAIL_SUCTION — suction pressure out of range for delaySec.  GRC only
  // checks the LOW side here (high-suction is a separate sensor-overrange
  // check, see HIGH_SUC_PRESSURE below).  Warmup-gated.
  evalFail('FAIL_SUCTION', 'Suction Pressure (sustained low)',
    t.failures.suctionP, fs.suctionP,
    compWarmedUp
      && t.sensors.suctionP.value < t.sensors.suctionP.lowAlarm);
  armPumpdownOnce(fs.suctionP);
  // FAIL_OIL — oil DIFFERENTIAL low for delaySec while comp running.
  // GRC: `(oilP - suctionP) < OilPressureLow` (Compressor.c:1614).
  // Compressor's positive oil-pressure feed must overcome suction; the
  // differential is what protects the bearings.  `lowAlarm` here is the
  // minimum acceptable differential in PSI.
  evalFail('FAIL_OIL', 'Oil Pressure Differential Low',
    t.failures.oilP, fs.oilP,
    compWarmedUp
      && (t.sensors.oilP.value - t.sensors.suctionP.value) < t.sensors.oilP.lowAlarm);
  armPumpdownOnce(fs.oilP);
  // FAIL_OUTSIDE_AIR — ambient below configured cutout for delaySec
  evalFail('FAIL_OUTSIDE_AIR', 'Outside Air Below Cutout',
    t.failures.ambientT, fs.ambientT,
    t.sensors.ambientT.valid
      && t.sensors.ambientT.value < t.setpoints.lowAmbientCutoutF);

  // HIGH_SUC_PRESSURE — sensor pegged at transducer max while comp on.
  // Mirrors GRC `HighSuctionPressure()` (Compressor.c §1564): immediate
  // alarm only, no safe-off, only checked while compressor is running OR
  // when we're certain the pressure isn't a real over-range from a real
  // event we can't trip on (we don't have transducer model data here, so
  // use `highAlarm` as proxy — sim test path).
  maybeAlarm('HIGH_SUC_PRESSURE', 'Suction Pressure Sensor Over-range',
    t.compressorOn
      && t.sensors.suctionP.valid
      && t.sensors.suctionP.value >= t.sensors.suctionP.highAlarm);

  // 7b. Power-fail detector — gap between updateTriton ticks larger than
  //     `powerFailMinutes` indicates the box was off long enough to need
  //     the crankcase prove.  Latches FAIL_POWER until ack-all.
  const lastMs = t.lastTickMs ?? 0;
  if (lastMs > 0) {
    const gapMin = (ctx.nowMs - lastMs) / 60000;
    if (gapMin > Math.max(1, t.setpoints.powerFailMinutes)) {
      const bit = TRITON_ALARM_BITS.FAIL_POWER;
      // Power-fail is treated as ALARM_ONLY in the simulator (no
      // safeOff) — Nova decides whether to enforce the prove.
      const ex = t.alarms.find(a => a.code === 'FAIL_POWER');
      if (ex) ex.active = true;
      else t.alarms.push({
        code: 'FAIL_POWER', label: 'Power-Fail (gap exceeded)',
        active: true, acked: false, timestamp: ctx.nowMs,
      });
      // Mark on safeOffMask only if Nova / operator wants to enforce it
      // by setting the failureModes mode for ambientT to SAFE_OFF; here
      // we keep it as a plain alarm.
      void bit;
    }
  }
  t.lastTickMs = ctx.nowMs;

  // 7c. Sensor-validity faults (transducer disconnected / out of range).
  //     Each maps 1:1 to a SENS_FAULT_* code.  These don't go through
  //     the timer — a single-tick invalid reading raises the alarm.
  maybeAlarm('SENS_FAULT_SUC_P', 'Suction-P Sensor Fault',  !t.sensors.suctionP.valid);
  maybeAlarm('SENS_FAULT_DIS_P', 'Discharge-P Sensor Fault', !t.sensors.dischargeP.valid);
  maybeAlarm('SENS_FAULT_OIL_P', 'Oil-P Sensor Fault',       !t.sensors.oilP.valid);
  maybeAlarm('SENS_FAULT_SUC_T', 'Suction-T Sensor Fault',   !t.sensors.suctionT.valid);
  maybeAlarm('SENS_FAULT_DIS_T', 'Discharge-T Sensor Fault', !t.sensors.dischargeT.valid);
  maybeAlarm('SENS_FAULT_LLS_T', 'LLS-T Sensor Fault',       !t.sensors.llsT.valid);
  maybeAlarm('SENS_FAULT_AMB_T', 'Ambient-T Sensor Fault',   !t.sensors.ambientT.valid);

  // 7d. Sensor-board missing — the host signals "no temp board" by passing
  //     a null `writeTempBoard` callback (likewise for press).
  maybeAlarm('BOARD_MISSING_TEMP',  'Temperature Board Missing',
             ctx.writeTempBoard == null);
  maybeAlarm('BOARD_MISSING_PRESS', 'Pressure Board Missing',
             ctx.writePressBoard == null);

  // 7e. NO_DISCHARGE — compressor has been running past the prove window
  //     but discharge pressure is essentially the same as suction (no
  //     compression happening).  Simple delta check.
  maybeAlarm('NO_DISCHARGE', 'No Discharge Pressure Rise',
    t.compressorOn
      && t.compressorRuntimeSec > t.setpoints.proveSec
      && t.sensors.dischargeP.value < t.sensors.suctionP.value + 20);

  // 7f. BAD_OIL_SENSOR — value pinned out-of-range while running.
  maybeAlarm('BAD_OIL_SENSOR', 'Oil Sensor Out of Range',
    t.compressorOn
      && (t.sensors.oilP.value < -5 || t.sensors.oilP.value > 500));

  // 7g. Capacity-stage / unloaders + oil pump.  Mirrors GRC
  //     `ManageEquipment()` (demand→stage hysteresis) +
  //     `ManageUnloaders()` (HP/LP overrides).  Triton supports up to
  //     2 unloaders mapped via `ioConfig.doRole === UNLOADER1/2`; oil
  //     pump (DO role 10) follows compressor on/off.  When comp is
  //     OFF, in pumpdown, in defrost, or safe-off → all unloaders
  //     forced OFF + their HP/LP latches reset (next demand cycle
  //     starts clean).
  const nUnl = Math.min(
    2,
    t.unloaderOn?.length ?? 0,
    dpSp.unloaderOnPct?.length ?? 0,
  );
  if (nUnl > 0) {
    if (!Array.isArray(t.unloaderOn))       t.unloaderOn       = [false, false];
    if (!Array.isArray(t.unloaderHpForced)) t.unloaderHpForced = [false, false];
    if (!Array.isArray(t.unloaderLpForced)) t.unloaderLpForced = [false, false];
    const compRunning = t.compressorOn
      && (t.pumpdownSecRemaining ?? 0) === 0
      && (t.defrostStage ?? 0) === 0
      && (t.safeOffMask ?? 0) === 0;
    const dischargeP = t.sensors.dischargeP.value;
    const suctionP   = t.sensors.suctionP.value;
    const demandPct  = Math.max(0, Math.min(100, t.demand ?? 0));
    if (!compRunning) {
      for (let i = 0; i < nUnl; i++) {
        t.unloaderOn[i]       = false;
        t.unloaderHpForced[i] = false;
        t.unloaderLpForced[i] = false;
      }
    } else {
      for (let i = 0; i < nUnl; i++) {
        // HP override hysteresis (forced UNLOAD on high discharge).
        const hpUnl = dpSp.unloaderHpUnloadPsi?.[i] ?? 350;
        const hpLd  = dpSp.unloaderHpLoadPsi?.[i]   ?? 320;
        if (dischargeP >= hpUnl)        t.unloaderHpForced[i] = true;
        else if (dischargeP <= hpLd)    t.unloaderHpForced[i] = false;
        // LP override hysteresis (forced UNLOAD on low suction).
        const lpUnl = dpSp.unloaderLpUnloadPsi?.[i] ?? 5;
        const lpLd  = dpSp.unloaderLpLoadPsi?.[i]   ?? 15;
        if (suctionP <= lpUnl)          t.unloaderLpForced[i] = true;
        else if (suctionP >= lpLd)      t.unloaderLpForced[i] = false;
        // Demand-based load/unload with hysteresis.  An override of
        // either kind pins the unloader OFF.
        if (t.unloaderHpForced[i] || t.unloaderLpForced[i]) {
          t.unloaderOn[i] = false;
        } else {
          const onPct  = dpSp.unloaderOnPct?.[i]  ?? 100;
          const offPct = dpSp.unloaderOffPct?.[i] ?? 0;
          if (t.unloaderOn[i]) {
            if (demandPct <= offPct) t.unloaderOn[i] = false;
          } else {
            if (demandPct >= onPct)  t.unloaderOn[i] = true;
          }
        }
      }
    }
  }
  // Oil pump: simple slave-to-compressor (mirrors GRC `EQ_OILPUMP`
  // default behaviour — staged on with comp).  Drops immediately on
  // pumpdown / defrost.  In safe-mode, `safePolicyOilPump` decides:
  //   0=OFF (default — drop),  1=HOLD_LAST (leave the prior commanded value).
  const inSafeModeOil = (t.safeOffMask ?? 0) !== 0 || sf.lockoutMask !== 0;
  if (inSafeModeOil) {
    const policyOil = (dpSp as any).safePolicyOilPump ?? 0;
    if (policyOil === 0) t.oilPumpOn = false;
    // policyOil === 1: leave t.oilPumpOn at its prior value (HOLD).
  } else {
    t.oilPumpOn = t.compressorOn
      && (t.defrostStage ?? 0) === 0;
  }

  // 8. Phase 7 — diagnostic logs.  Three channels, all RAM-resident:
  //   8a. PIDLog: written inline by Section 4 when the EXV PID computes.
  //   8b. SysLog (activity log): edge-driven from pre/post snapshots
  //       captured at tick start.
  //   8c. UserLog: 1-min cadence snapshot of operator-visible state.
  // GRC equivalents: PIDLogs.c, SystemLogs.c (activity entries),
  // UserLogs.c.  Buffers are fixed-size FIFO; oldest evicted on overflow.
  const logs = t.logs!;

  // 8a-pre. BOOT entry on first tick after a fresh logs structure.
  if (logs.sys.length === 0 && (logs.lastUserLogMs ?? 0) === 0) {
    pushLog(logs.sys, { ts: ctx.nowMs, kind: 8, code: 'BOOT' }, 256);
  }

  // 8b. SysLog — alarm-active edges.
  for (const a of t.alarms) {
    const was = preAlarmActive[a.code] === true;
    if (a.active && !was) {
      pushLog(logs.sys, { ts: ctx.nowMs, kind: 0, code: a.code, label: a.label }, 256);
    } else if (!a.active && was) {
      pushLog(logs.sys, { ts: ctx.nowMs, kind: 1, code: a.code, label: a.label }, 256);
    }
  }
  // SysLog — compressor start/stop edges.
  if (t.compressorOn !== preCompOn) {
    pushLog(logs.sys,
      { ts: ctx.nowMs, kind: t.compressorOn ? 3 : 4,
        code: t.compressorOn ? 'COMP_START' : 'COMP_STOP' },
      256);
  }
  // SysLog — defrost begin/end edges.
  const postDefStg = t.defrostStage ?? 0;
  if (preDefrostStg === 0 && postDefStg !== 0) {
    pushLog(logs.sys,
      { ts: ctx.nowMs, kind: 6, code: `DEFROST_BEGIN_T${t.defrostType ?? 0}` },
      256);
  } else if (preDefrostStg !== 0 && postDefStg === 0) {
    pushLog(logs.sys, { ts: ctx.nowMs, kind: 7, code: 'DEFROST_END' }, 256);
  }
  // SysLog — manual-mode change.  REST handlers mutate `manualMode`
  // between ticks, so we compare against a snapshot stored on the logs
  // structure (not the within-tick pre snapshot, which would always be
  // equal to the post value).
  const lastManual = logs.lastManualMode;
  if (lastManual !== undefined && lastManual !== t.manualMode) {
    pushLog(logs.sys, { ts: ctx.nowMs, kind: 5, code: t.manualMode }, 256);
  }
  logs.lastManualMode = t.manualMode;

  // 8c. UserLog — once per minute (mirrors GRC `NUM_SYSLOGS_PER_DAY = 1440`
  //               cadence from SystemLogs.h:31).
  const userLastMs = logs.lastUserLogMs ?? 0;
  if (userLastMs === 0 || (ctx.nowMs - userLastMs) >= 60_000) {
    pushLog(logs.user, {
      ts:         ctx.nowMs,
      mode:       t.compressorStatus,
      suctionP:   Math.round(t.sensors.suctionP.value   * 10) / 10,
      suctionT:   Math.round(t.sensors.suctionT.value   * 10) / 10,
      dischargeP: Math.round(t.sensors.dischargeP.value * 10) / 10,
      dischargeT: Math.round(t.sensors.dischargeT.value * 10) / 10,
      oilP:       Math.round(t.sensors.oilP.value       * 10) / 10,
      ambientT:   Math.round(t.sensors.ambientT.value   * 10) / 10,
      superheat:  Math.round(superheat                  * 10) / 10,
      exvPct:     Math.round(t.exvOpenPct               * 10) / 10,
      fanStage:   t.condenserFanStage,
      vfdPct:     Math.round((t.condenserVfdPct ?? 0)   * 10) / 10,
      demand:     t.demand,
    }, 256);
    logs.lastUserLogMs = ctx.nowMs;
  }

  // Used by the EXV / leak block above; nothing else to do with it here.
  void satT;
}
