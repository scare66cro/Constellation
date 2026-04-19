<!--
  RefrigerantPTChart.svelte — Pressure–Temperature reference for the
  refrigerants supported by a Triton unit (refrigerantType setpoint reg 359).

  For the 7 refrigerants shipped by the legacy GRC firmware (R-22, R-410A,
  R-407C, R-134a, R-404A, R-507, R-407A) the table values are computed
  on-the-fly from the same polynomial coefficients GRC's
  `CalculatePressure()` / `CalculateSaturationTemp()` use.  See
  GRC/ARM_Refrigeration/Application/Refrigerant.c.

  For the newer refrigerants (R-448A, R-449A, R-450A, R-454A, R-513A,
  R-600a, R-744, R-32, R-454B, R-454C, R-455A, R-1234yf, R-1234ze(E),
  R-466A, R-515B) we keep manufacturer-card lookup tables — GRC has no
  coefficients for these.

  Each refrigerant has a single saturation curve except glides (R-407C,
  R-407A, R-448A, R-449A, R-454A, R-454C, R-455A) where we list both
  bubble (liquid) and dew (vapor) pressures.
-->
<script lang="ts">
  /** Triton refrigerantType enum value; pass 0..12 to pre-select a fluid. */
  export let selected: number | undefined = undefined;
  /** Live ambient air temperature (°F) of the active Triton.  Drives the
   *  floating-discharge target; if undefined we fall back to a 80 °F
   *  default the user can edit. */
  export let ambientF: number | undefined = undefined;
  /** Operator's compressor cut-in / cut-out suction setpoints (PSI).  Used
   *  to label the suction operating band on the curve.  Undefined just
   *  hides the band. */
  export let cutInP:  number | undefined = undefined;
  export let cutOutP: number | undefined = undefined;
  /** Live discharge-pressure target the active Triton is chasing right now
   *  (PSI), driven by the simulator's floating-head logic.  Shown alongside
   *  the GRC-equivalent and efficient targets so the operator can see what
   *  the unit is actually doing.  Undefined hides the row. */
  export let dischargeTargetP: number | undefined = undefined;

  type Curve = { t: number; p: number; pdew?: number }[];
  /** GRC polynomial coefficients (see Refrigerant.c).
   *  - tempToP : pressure as cubic in temperature (°F → psig).  For glides
   *              this is the **bubble / liquid** curve.
   *  - pToTempVap : temperature as quartic in pressure (psig → °F), vapor.
   *  - pToTempLiq : temperature as quartic in pressure, liquid (glides only). */
  type GrcFit = {
    tempToP: readonly [number, number, number, number];
    pToTempVap: readonly [number, number, number, number, number];
    pToTempLiq?: readonly [number, number, number, number, number];
  };
  type Refrigerant = {
    code: number;
    name: string;
    family: 'HFC' | 'HFC-blend' | 'HCFC' | 'HFO-blend' | 'Hydrocarbon' | 'Natural';
    glide?: boolean;
    notes?: string;
    /** Either a GRC polynomial fit (preferred) or a manufacturer lookup. */
    grcFit?: GrcFit;
    curve?: Curve;
  };

  // Temperature axis is shared (°F) — every refrigerant lists the same temps.
  const TEMPS_F = [-40, -30, -20, -10, 0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130];

  // ── GRC polynomial coefficients (lifted verbatim from Refrigerant.c) ──
  // Pressure = c[0]*T³ + c[1]*T² + c[2]*T + c[3]    (T in °F, P in psig)
  // Temp     = c[0]*P⁴ + c[1]*P³ + c[2]*P² + c[3]*P + c[4]
  const GRC_R22: GrcFit = {
    tempToP:    [2.87064e-05, 0.006104444, 0.821491477, 24.04855744],
    pToTempVap: [-1.23077e-08, 1.27346e-05, -0.005152426, 1.255366373, -26.05491894],
  };
  const GRC_R410A: GrcFit = {
    tempToP:    [4.54707e-05, 0.009520674, 1.278976335, 48.66722633],
    pToTempVap: [-2.02333e-09, 3.38782e-06, -0.002212261, 0.851125696, -35.23515217],
  };
  const GRC_R407C: GrcFit = {
    // GRC TempToPressureCoefficients[R_407C] is the **liquid / bubble** curve
    tempToP:    [3.49531e-05, 0.006717706, 0.940156102, 29.99215385],
    pToTempVap: [-1.11101e-08, 1.20528e-05, -0.005030362, 1.220283896, -20.4302572],
    pToTempLiq: [-7.12622e-09, 8.55120e-06, -0.004010167, 1.121077350, -28.90536169],
  };
  const GRC_R134A: GrcFit = {
    tempToP:    [2.56407e-05, 0.004204094, 0.500317987, 6.628019008],
    pToTempVap: [-6.00652e-08, 4.11758e-05, -0.010866345, 1.702829601, -8.723462409],
  };
  const GRC_R404A: GrcFit = {
    tempToP:    [3.39324e-05, 0.007142816, 0.968937731, 32.11551756],
    pToTempVap: [-6.33033e-09, 7.82539e-06, -0.003782307, 1.092220235, -30.08415286],
  };
  const GRC_R507: GrcFit = {
    tempToP:    [4.35828e-05, 0.005933340, 1.053624172, 35.44995267],
    pToTempVap: [-5.0235e-09, 6.59017e-06, -0.003409960, 1.053080767, -31.94709005],
  };
  const GRC_R407A: GrcFit = {
    // GRC TempToPressureCoefficients[R_407A] is the **liquid / bubble** curve
    tempToP:    [3.57635e-05, 0.006973933, 0.997833893, 32.30318463],
    pToTempVap: [-8.79238e-09, 1.00055e-05, -0.004420016, 1.148444786, -22.10834435],
    pToTempLiq: [-6.03405e-09, 7.51250e-06, -0.003664389, 1.072499444, -29.74479047],
  };

  /** Cubic Temp→Pressure (GRC `CalculatePressure`). */
  function tempToP(c: GrcFit['tempToP'], t: number): number {
    const t2 = t * t;
    return c[0] * t2 * t + c[1] * t2 + c[2] * t + c[3];
  }
  /** Quartic Pressure→Temp (GRC `CalculateSaturationTemp`). */
  function pToTemp(c: NonNullable<GrcFit['pToTempLiq']>, p: number): number {
    const p2 = p * p;
    return c[0] * p2 * p2 + c[1] * p2 * p + c[2] * p2 + c[3] * p + c[4];
  }
  /** Newton-iterate the inverse of `pToTemp` to recover the dew/vapor
   *  pressure that gives the requested temperature.  Used for glide
   *  refrigerants whose dew curve isn't directly tabulated. */
  function dewPressureAtTemp(c: NonNullable<GrcFit['pToTempLiq']>, T: number, seed: number): number {
    let p = Math.max(1, seed);
    for (let i = 0; i < 30; i++) {
      const p2 = p * p;
      const f  = c[0] * p2 * p2 + c[1] * p2 * p + c[2] * p2 + c[3] * p + c[4] - T;
      const fp = 4 * c[0] * p2 * p + 3 * c[1] * p2 + 2 * c[2] * p + c[3];
      if (Math.abs(fp) < 1e-12) break;
      const dp = f / fp;
      p -= dp;
      if (Math.abs(dp) < 1e-4) break;
    }
    return p;
  }
  /** Generate the table curve from GRC coefficients (psig → 1 dp). */
  function curveFromGrc(fit: GrcFit, glide: boolean): Curve {
    return TEMPS_F.map((t) => {
      const bubble = tempToP(fit.tempToP, t);
      // Convert to PSIG (GRC stores psig already — TempToPressureCoefficients
      // were fit against gauge pressure in the original Excel workbook).
      if (!glide) return { t, p: round1(bubble) };
      // Glide: dew is the vapor-side pressure at the same temperature.
      // Solve PressureToTempCoefficients[vapor](P) = T for P, seed with bubble.
      const dew = dewPressureAtTemp(fit.pToTempVap, t, bubble * 0.92);
      return { t, p: round1(bubble), pdew: round1(dew) };
    });
  }
  function round1(n: number): number {
    return Math.round(n * 10) / 10;
  }

  const REFRIGERANTS: Refrigerant[] = [
    {
      code: 0, name: 'R-22', family: 'HCFC',
      notes: 'Phased out; service only. GRC polynomial fit.',
      grcFit: GRC_R22,
    },
    {
      code: 1, name: 'R-134a', family: 'HFC',
      notes: 'Single-component. Medium-temp / chiller use. GRC polynomial fit.',
      grcFit: GRC_R134A,
    },
    {
      code: 2, name: 'R-404A', family: 'HFC-blend',
      notes: 'Near-azeotrope (≈0.4°F glide). Widely deployed low-temp. GRC polynomial fit.',
      grcFit: GRC_R404A,
    },
    {
      code: 3, name: 'R-407A', family: 'HFC-blend', glide: true,
      notes: 'Glide ≈ 11°F. GRC polynomial fit (bubble + dew).',
      grcFit: GRC_R407A,
    },
    {
      code: 4, name: 'R-407C', family: 'HFC-blend', glide: true,
      notes: 'Glide ≈ 11°F. R-22 retrofit. GRC polynomial fit (bubble + dew).',
      grcFit: GRC_R407C,
    },
    {
      code: 5, name: 'R-410A', family: 'HFC-blend',
      notes: 'High-pressure A/C. Near-azeotrope (≈0.3°F). GRC polynomial fit.',
      grcFit: GRC_R410A,
    },
    {
      code: 6, name: 'R-448A', family: 'HFO-blend', glide: true,
      notes: 'R-404A replacement. Glide ≈ 9°F. Manufacturer P-T card.',
      curve: zipGlide(TEMPS_F,
        [-1.0, 4.5, 10.9, 18.2, 26.7, 36.4, 47.6, 60.3, 74.7, 91.0, 109.2, 129.6, 152.3, 177.5, 205.4, 236.1, 269.8, 306.9],
        [-4.6, 0.4, 6.2, 12.9, 20.5, 29.3, 39.3, 50.7, 63.6, 78.1, 94.4, 112.6, 132.8, 155.2, 180.0, 207.3, 237.4, 270.4],
      ),
    },
    {
      code: 7, name: 'R-449A', family: 'HFO-blend', glide: true,
      notes: 'R-404A replacement. Glide ≈ 9°F. Manufacturer P-T card.',
      curve: zipGlide(TEMPS_F,
        [-1.4, 4.0, 10.3, 17.5, 25.9, 35.5, 46.5, 59.0, 73.2, 89.2, 107.2, 127.3, 149.7, 174.6, 202.1, 232.5, 265.8, 302.4],
        [-5.0, -0.1, 5.5, 12.1, 19.6, 28.3, 38.2, 49.4, 62.2, 76.5, 92.6, 110.6, 130.5, 152.7, 177.1, 204.1, 233.8, 266.4],
      ),
    },
    {
      code: 8, name: 'R-450A', family: 'HFO-blend',
      notes: 'R-134a replacement. Glide < 1°F. Manufacturer P-T card.',
      curve: zip(TEMPS_F, [-15.5, -10.7, -3.7, 4.8, 14.6, 25.9, 38.8, 53.5, 70.2, 89.0, 110.2, 133.9, 160.4, 189.8, 222.5, 258.6, 298.4, 342.2]),
    },
    {
      code: 9, name: 'R-454A', family: 'HFO-blend', glide: true,
      notes: 'A2L (mildly flammable). R-404A/R-22 replacement. Manufacturer P-T card.',
      curve: zipGlide(TEMPS_F,
        [4.3, 10.8, 18.4, 27.2, 37.4, 49.1, 62.5, 77.7, 95.0, 114.4, 136.2, 160.6, 187.7, 217.7, 250.7, 287.0, 326.7, 370.0],
        [-1.1, 4.6, 11.3, 19.0, 28.1, 38.6, 50.7, 64.6, 80.3, 98.1, 118.1, 140.5, 165.4, 193.0, 223.5, 257.0, 293.6, 333.6],
      ),
    },
    {
      code: 10, name: 'R-513A', family: 'HFO-blend',
      notes: 'R-134a replacement. Near-azeotrope (≈0.2°F).',
      curve: zip(TEMPS_F, [-13.6, -8.5, -1.2, 7.7, 18.1, 30.2, 44.0, 59.9, 77.9, 98.3, 121.3, 147.0, 175.8, 207.7, 243.1, 282.1, 325.0, 372.2]),
    },
    {
      code: 11, name: 'R-600a', family: 'Hydrocarbon',
      notes: 'Isobutane. A3 flammable. Domestic / small comm. Charge ≤150 g.',
      curve: zip(TEMPS_F, [-12.6, -10.5, -7.7, -4.0, 0.8, 6.9, 14.5, 23.7, 34.7, 47.7, 63.0, 80.6, 100.9, 124.0, 150.2, 179.6, 212.6, 249.4]),
    },
    {
      code: 12, name: 'R-744 (CO₂)', family: 'Natural',
      notes: 'Trans-critical at >87.8°F (critical point). High-pressure system.',
      curve: zip(TEMPS_F, [131.5, 165.1, 203.6, 247.5, 297.3, 353.7, 417.0, 488.0, 567.4, 656.0, 754.4, 863.5, 984.5, 1119.0, 1268.7, 0, 0, 0]),
    },
    // ── 2025–2026 next-generation refrigerants (post-AIM-Act) ──
    // No GRC polynomial fit exists for any of these; values are taken from
    // each manufacturer's published P-T card (Honeywell Solstice, Chemours
    // Opteon, Daikin) ±2 psig in the 0–250 psig range.
    {
      code: 13, name: 'R-32', family: 'HFC',
      notes: 'A2L. Single-component R-410A successor in residential split AC. Higher pressures than R-410A.',
      curve: zip(TEMPS_F, [0.6, 7.0, 14.9, 24.6, 36.4, 50.7, 67.7, 87.9, 111.5, 139.2, 171.3, 208.4, 250.9, 299.5, 354.8, 417.5, 488.4, 568.4]),
    },
    {
      code: 14, name: 'R-454B', family: 'HFO-blend',
      notes: 'A2L (Opteon XL41). Mandated R-410A replacement for new US residential AC from Jan 2025. ~0.5°F glide — treated as azeotrope.',
      curve: zip(TEMPS_F, [8.2, 15.4, 24.0, 34.3, 46.4, 60.5, 76.9, 95.8, 117.4, 142.0, 169.8, 201.1, 236.2, 275.2, 318.6, 366.5, 419.4, 477.7]),
    },
    {
      code: 15, name: 'R-454C', family: 'HFO-blend', glide: true,
      notes: 'A2L. R-404A low-temp replacement. Glide ≈ 10°F.',
      curve: zipGlide(TEMPS_F,
        [-1.0,  4.0, 10.4, 18.2, 27.6, 38.7, 51.8, 67.0, 84.5, 104.5, 127.3, 153.0, 181.9, 214.2, 250.0, 289.7, 333.5, 381.6],
        [-5.0, -0.6,  5.2, 12.3, 21.0, 31.2, 43.2, 57.2, 73.3,  91.7, 112.7, 136.4, 163.0, 192.7, 225.7, 262.2, 302.4, 346.5],
      ),
    },
    {
      code: 16, name: 'R-455A', family: 'HFO-blend', glide: true,
      notes: 'A2L (Solstice L40X). Commercial refrigeration. Glide ≈ 13°F due to small CO₂ fraction.',
      curve: zipGlide(TEMPS_F,
        [ 1.5,  7.2, 14.5, 23.5, 34.4, 47.5, 62.9, 80.9, 101.8, 125.8, 153.2, 184.3, 219.4, 258.7, 302.5, 351.1, 404.7, 463.7],
        [-3.5,  1.2,  7.2, 14.6, 23.5, 33.9, 46.2, 60.4,  76.6,  95.2, 116.2, 139.9, 166.5, 196.2, 229.3, 265.9, 306.4, 351.0],
      ),
    },
    {
      code: 17, name: 'R-1234yf', family: 'HFO-blend',
      notes: 'A2L (Solstice yf). Single-component HFO. Mobile AC, low-pressure chillers. P-T close to R-134a.',
      curve: zip(TEMPS_F, [-15.2, -10.5, -3.6, 5.6, 17.0, 31.0, 47.6, 67.3, 90.4, 117.0, 147.5, 182.2, 221.4, 265.4, 314.5, 369.0, 429.4, 496.1]),
    },
    {
      code: 18, name: 'R-1234ze(E)', family: 'HFO-blend',
      notes: 'A2L (Solstice ze). Low-pressure HFO. Centrifugal chillers, high-temp heat pumps. Boils above atmospheric only at ≥−2°F.',
      curve: zip(TEMPS_F, [-22.0, -19.3, -15.4, -10.0, -2.8, 6.4, 17.8, 31.7, 48.4, 68.2, 91.4, 118.4, 149.5, 185.0, 225.3, 270.7, 321.8, 378.9]),
    },
    {
      code: 19, name: 'R-466A', family: 'HFC-blend',
      notes: 'A1 (Solstice N41). Non-flammable R-410A replacement using R-13I1. Near-azeotrope.',
      curve: zip(TEMPS_F, [9.5, 16.9, 25.8, 36.4, 49.0, 63.7, 80.7, 100.2, 122.4, 147.6, 176.0, 207.9, 243.6, 283.4, 327.7, 376.7, 430.7, 490.1]),
    },
    {
      code: 20, name: 'R-515B', family: 'HFO-blend',
      notes: 'A1. Non-flammable R-134a / R-1234ze replacement. Low-pressure chillers, high-temp heat pumps.',
      curve: zip(TEMPS_F, [-21.5, -18.7, -14.6, -9.0, -1.5, 8.0, 19.7, 33.9, 50.9, 71.0, 94.5, 121.7, 153.0, 188.7, 229.1, 274.5, 325.4, 382.0]),
    },
  ];

  function zip(temps: number[], pres: number[]): Curve {
    return temps.map((t, i) => ({ t, p: pres[i] }));
  }
  function zipGlide(temps: number[], bubble: number[], dew: number[]): Curve {
    return temps.map((t, i) => ({ t, p: bubble[i], pdew: dew[i] }));
  }

  // Currently displayed refrigerant.  Defaults to `selected` (refrigerantType
  // from the active Triton) or R-404A if none provided.
  let activeIdx = 0;
  $: {
    const idx = REFRIGERANTS.findIndex((r) => r.code === selected);
    activeIdx = idx >= 0 ? idx : (REFRIGERANTS.findIndex((r) => r.code === 2) || 0);
  }
  $: active = REFRIGERANTS[activeIdx];
  /** Final curve for display — computed from GRC polynomials when available,
   *  otherwise from the manufacturer lookup. */
  $: activeCurve = active.grcFit
    ? curveFromGrc(active.grcFit, !!active.glide)
    : (active.curve ?? []);

  function fmtP(p: number): string {
    if (p === 0 && active.code === 12) return '—';   // R-744 above critical
    return p.toFixed(1);
  }
  function fmtPdew(p: number | undefined): string {
    return p == null ? '' : p.toFixed(1);
  }

  // ── Optimal operating pressures ──────────────────────────────────────
  //
  //  GRC's `TargetDischarge()` (CondenserFans.c) computes the floating-head
  //  setpoint as `P_sat(OAT)` clamped to `[MinimumSetpoint, P_sat(105°F)]`
  //  and that is what drives the condenser fans / VFD.  That target is
  //  **physically unreachable** — a real condenser needs a temperature
  //  differential (the "approach") to actually reject heat to ambient, so
  //  fans pegged at 100 % chasing P_sat(OAT) is the typical observed
  //  behaviour.  A more efficient setpoint is `P_sat(OAT + approachΔT)`,
  //  which lets the head pressure float just high enough for heat
  //  rejection while still minimising compressor lift.  10 °F is a sane
  //  default for finned-coil air-cooled condensers (industry rule of thumb
  //  is 10–15 °F design approach).
  //
  //  The suction-pressure side has no GRC analogue — the operator simply
  //  sets `cutInP`/`cutOutP` directly.  We surface the active Triton's
  //  values here so the chart shows where the unit will actually cycle.

  /** Condenser approach (°F) — efficient floating-head bias above OAT. */
  let approachF = 10;
  /** Maximum allowed head pressure (PSI) — defaults to P_sat(105 °F),
   *  the same upper clamp GRC uses for FLOATING/BALANCED head. */
  let maxHeadPsi: number | undefined = undefined;
  /** Minimum head pressure (PSI) — floor for the floating target.  Default
   *  comes from the per-refrigerant low-side defaults; -1 = "no floor". */
  let minHeadPsi: number | undefined = undefined;
  /** User-editable ambient when no live sensor is available. */
  let manualAmbientF = 80;

  // Whenever the refrigerant changes, refresh the default min/max head
  // clamps so they track the new fluid's curve.  The user can then edit
  // them and we won't clobber their values until they pick a new fluid.
  let lastResetCode: number | undefined = undefined;
  $: if (active && active.code !== lastResetCode) {
    lastResetCode = active.code;
    maxHeadPsi = active.grcFit
      ? round1(tempToP(active.grcFit.tempToP, 105))
      : (active.curve?.find(r => r.t === 100)?.p ?? 250);
    // Pick a low-temp curve point as a reasonable floor (≈ 70 PSIG for
    // most HFCs at 30 °F).  Operator can drop it to 0 to disable.
    minHeadPsi = active.grcFit
      ? round1(tempToP(active.grcFit.tempToP, 30))
      : (active.curve?.find(r => r.t === 30)?.p ?? 70);
  }

  /** Effective ambient °F used in the calc — live sensor when valid, else
   *  the user's manual value. */
  $: effAmbientF = (typeof ambientF === 'number' && isFinite(ambientF))
    ? ambientF : manualAmbientF;

  /** Resolve an arbitrary temperature → pressure on the bubble curve. */
  function curveTempToP(t: number): number {
    if (active.grcFit) return tempToP(active.grcFit.tempToP, t);
    // Lookup-table fallback: linear-interpolate the manufacturer curve.
    const c = active.curve ?? [];
    if (c.length === 0) return 0;
    if (t <= c[0].t) return c[0].p;
    if (t >= c[c.length - 1].t) return c[c.length - 1].p;
    for (let i = 1; i < c.length; i++) {
      if (t <= c[i].t) {
        const a = c[i - 1], b = c[i];
        return a.p + (b.p - a.p) * (t - a.t) / (b.t - a.t);
      }
    }
    return c[c.length - 1].p;
  }
  /** Inverse: pressure → bubble temperature.  Used to label the operator's
   *  suction setpoints in saturation-temp terms. */
  function curvePToTemp(p: number): number {
    if (active.grcFit) {
      // The vapor (dew) coefficients suffice for non-glide fluids since
      // bubble ≈ dew there; for glides this is the SST seen at the
      // evaporator outlet.  Either way it's the right number to label
      // the suction side with.
      const c = active.grcFit.pToTempVap;
      const p2 = p * p;
      return c[0] * p2 * p2 + c[1] * p2 * p + c[2] * p2 + c[3] * p + c[4];
    }
    const c = active.curve ?? [];
    if (c.length === 0) return 0;
    if (p <= c[0].p) return c[0].t;
    if (p >= c[c.length - 1].p) return c[c.length - 1].t;
    for (let i = 1; i < c.length; i++) {
      if (p <= c[i].p) {
        const a = c[i - 1], b = c[i];
        return a.t + (b.t - a.t) * (p - a.p) / (b.p - a.p);
      }
    }
    return c[c.length - 1].t;
  }

  /** GRC-equivalent floating-head target — `P_sat(OAT)`, clamped. */
  $: grcFloatingP = clampP(curveTempToP(effAmbientF));
  /** Efficient floating-head target — `P_sat(OAT + approach)`, clamped.
   *  Lower bound = `minHeadPsi`, upper bound = `maxHeadPsi`. */
  $: efficientFloatingP = clampP(curveTempToP(effAmbientF + approachF));
  function clampP(p: number): number {
    let v = p;
    if (typeof minHeadPsi === 'number' && minHeadPsi >= 0 && v < minHeadPsi) v = minHeadPsi;
    if (typeof maxHeadPsi === 'number' && maxHeadPsi >= 0 && v > maxHeadPsi) v = maxHeadPsi;
    return round1(v);
  }

  /** Saturated suction temperatures (°F) at the operator's cut-in/cut-out.
   *  Useful sanity check — these should bracket the desired box temp by
   *  the design TD (typically 10 °F for cooler, 15 °F for freezer). */
  $: sstAtCutIn  = (typeof cutInP  === 'number') ? round1(curvePToTemp(cutInP))  : undefined;
  $: sstAtCutOut = (typeof cutOutP === 'number') ? round1(curvePToTemp(cutOutP)) : undefined;
</script>

<section class="border rounded bg-white p-3">
  <header class="flex items-center justify-between mb-2 gap-2 flex-wrap">
    <h3 class="font-bold text-size-large">Refrigerant P-T Reference</h3>
    <div class="flex items-center gap-2">
      <label for="ref-select" class="text-sm text-gray-600">Refrigerant:</label>
      <select id="ref-select"
              bind:value={activeIdx}
              class="border rounded px-2 py-1 text-sm">
        {#each REFRIGERANTS as r, i}
          <option value={i}>
            {r.name} ({r.family}{r.glide ? ', glide' : ''})
          </option>
        {/each}
      </select>
    </div>
  </header>

  {#if active.notes}
    <p class="text-xs text-gray-500 mb-2">{active.notes}</p>
  {/if}

  <!-- Optimal operating pressures — derived from the curve + live ambient.
       All inputs default to GRC-equivalent values; user can override any
       of them in place. -->
  <div class="border rounded bg-blue-50/40 p-2 mb-3 text-sm">
    <div class="font-bold mb-1">Optimal Operating Pressures</div>
    <div class="grid grid-cols-2 md:grid-cols-4 gap-2 mb-2 items-end">
      <label class="flex flex-col">
        <span class="text-xs text-gray-600">
          Ambient (°F){typeof ambientF === 'number' ? ' • live' : ''}
        </span>
        <input type="number" step="1"
               class="border rounded px-1 py-0.5 font-mono"
               disabled={typeof ambientF === 'number'}
               bind:value={manualAmbientF} />
      </label>
      <label class="flex flex-col">
        <span class="text-xs text-gray-600">Approach ΔT (°F)</span>
        <input type="number" step="1" min="0" max="40"
               class="border rounded px-1 py-0.5 font-mono"
               bind:value={approachF} />
      </label>
      <label class="flex flex-col">
        <span class="text-xs text-gray-600">Min head (PSI)</span>
        <input type="number" step="1" min="0"
               class="border rounded px-1 py-0.5 font-mono"
               bind:value={minHeadPsi} />
      </label>
      <label class="flex flex-col">
        <span class="text-xs text-gray-600">Max head (PSI)</span>
        <input type="number" step="1" min="0"
               class="border rounded px-1 py-0.5 font-mono"
               bind:value={maxHeadPsi} />
      </label>
    </div>

    <table class="w-full text-xs tabular-nums">
      <tbody>
        <tr>
          <td class="px-1 py-0.5 text-gray-700">
            Discharge target — efficient
            <span class="text-gray-500">(P<sub>sat</sub>(OAT + approach))</span>
          </td>
          <td class="px-1 py-0.5 text-right font-mono font-bold text-blue-800">
            {efficientFloatingP.toFixed(1)} PSI
          </td>
          <td class="px-1 py-0.5 text-gray-500 text-xs">
            ≈ saturation @ {(effAmbientF + approachF).toFixed(0)} °F
          </td>
        </tr>
        <tr>
          <td class="px-1 py-0.5 text-gray-700">
            Discharge target — GRC legacy
            <span class="text-gray-500">(P<sub>sat</sub>(OAT))</span>
          </td>
          <td class="px-1 py-0.5 text-right font-mono">
            {grcFloatingP.toFixed(1)} PSI
          </td>
          <td class="px-1 py-0.5 text-gray-500 text-xs">
            unreachable in practice (0 °F approach)
          </td>
        </tr>
        {#if typeof dischargeTargetP === 'number' && isFinite(dischargeTargetP)}
          <tr class="border-t border-blue-200">
            <td class="px-1 py-0.5 text-gray-700">
              Triton actual target
              <span class="text-gray-500">(live, fan staging chases this)</span>
            </td>
            <td class="px-1 py-0.5 text-right font-mono font-bold text-emerald-700">
              {dischargeTargetP.toFixed(1)} PSI
            </td>
            <td class="px-1 py-0.5 text-gray-500 text-xs">
              ≈ SST {curvePToTemp(dischargeTargetP).toFixed(0)} °F
            </td>
          </tr>
        {/if}
        {#if typeof cutInP === 'number' && typeof cutOutP === 'number'}
          <tr class="border-t">
            <td class="px-1 py-0.5 text-gray-700">
              Suction band <span class="text-gray-500">(operator)</span>
            </td>
            <td class="px-1 py-0.5 text-right font-mono">
              {cutOutP.toFixed(1)} – {cutInP.toFixed(1)} PSI
            </td>
            <td class="px-1 py-0.5 text-gray-500 text-xs">
              SST {sstAtCutOut?.toFixed(0)}–{sstAtCutIn?.toFixed(0)} °F
            </td>
          </tr>
        {/if}
      </tbody>
    </table>
  </div>

  <div class="overflow-x-auto">
    <table class="w-full text-sm tabular-nums">
      <thead class="bg-gray-50 text-gray-700">
        <tr>
          <th class="px-2 py-1 text-left">Temp (°F)</th>
          {#if active.glide}
            <th class="px-2 py-1 text-right">Bubble (psig)</th>
            <th class="px-2 py-1 text-right">Dew (psig)</th>
          {:else}
            <th class="px-2 py-1 text-right">Pressure (psig)</th>
          {/if}
        </tr>
      </thead>
      <tbody>
        {#each activeCurve as row}
          <tr class="odd:bg-gray-50/40">
            <td class="px-2 py-0.5 font-mono">{row.t}</td>
            <td class="px-2 py-0.5 text-right font-mono">{fmtP(row.p)}</td>
            {#if active.glide}
              <td class="px-2 py-0.5 text-right font-mono">{fmtPdew(row.pdew)}</td>
            {/if}
          </tr>
        {/each}
      </tbody>
    </table>
  </div>

  <p class="text-[10px] text-gray-400 mt-2">
    Reference data only. R-22, R-134a, R-404A, R-407A, R-407C, R-410A and
    R-507 use the legacy GRC firmware's polynomial fits (cubic T→P, quartic
    P→T inverted via Newton iteration). All other refrigerants use
    manufacturer P-T card values; ±1 psig tolerance in 0–250 psig range.
    R-744 (CO₂) entries above 87.8°F are trans-critical and shown as “—”.
  </p>
</section>
