<!--
  TritonMimic.svelte — SCADA-style schematic of a single Triton refrigeration
  unit.  Purely presentational; receives the snapshot from /iot/triton/{slot}
  and dispatches a 'select' event when the operator clicks a hotspot.

  Layout (viewBox 1000×420) — Triton-specific flow loop:

    Discharge HP (red, hot) routes over the top from compressor → EXV.
    EXV → evaporator (short amber feed).
    Evaporator → condenser (red, hot gas now carrying box heat).
    Condenser → accumulator (amber, cooled gas).
    Accumulator → compressor suction (blue).

    ┌──── Discharge HP red ────────────────────────────────────────┐
    │                                                              │
    │    ┌──── Condenser ────────────────────────┐                 │
    │    ↑                                        ↓ Cond out amber │
    │   Evap→Cond                               Accumulator        │
    │    │                                        │                ↓
   Compressor ←──── Suction blue ─────────── Accumulator         EXV
    │                                                              │
    └────── Evaporator (bottom) ──── EXV→Evap (amber) ◄────────────┘

  Loop matches plant: compressor → EXV → evaporator → condenser →
  accumulator → compressor.  The accumulator is on the *suction* side
  (pump-down vessel), not the high-side receiver.

  Refrigerant-flow lines use stroke-dasharray + an animated stroke-dashoffset
  (driven by CSS) so they appear to flow when the compressor is on.  When the
  compressor is off the dash animation is paused — same line, no motion.

  Color convention:
    red    = hot (compressor discharge HP, and evap-out hot gas to condenser)
    amber  = cooled / liquid-feed (cond-out, EXV→evap)
    blue   = low-pressure suction gas (accumulator → compressor)
    gray   = inactive / no flow

  Hotspots (rect overlays with cursor:pointer) dispatch 'select' with one of:
    'compressor' | 'condenser' | 'condFans' | 'accumulator' | 'exv'
    | 'evaporator' | 'suction' | 'discharge' | 'liquid'
    | 'oil' | 'ambient'
-->
<script lang="ts">
  import { createEventDispatcher } from "svelte";

  // Telemetry snapshot shape from /iot/triton/{slot}.  Kept loose ('any')
  // because the schema is still evolving in the simulator.
  export let state: any;

  const dispatch = createEventDispatcher<{ select: { id: string } }>();

  // Convenient derived flags.  Defaults guard against partial states during
  // first render or when the orbit briefly drops connection.
  $: on              = !!state?.compressor?.on;
  $: condFanStage    = state?.condenserFans?.stage ?? 0;
  $: condFanCount    = state?.condenserFans?.count ?? 0;
  $: exvPct          = Number(state?.exvOpenPct ?? 0);
  $: suctionP        = state?.sensors?.suctionP?.value;
  $: suctionT        = state?.sensors?.suctionT?.value;
  $: dischargeP      = state?.sensors?.dischargeP?.value;
  $: dischargeT      = state?.sensors?.dischargeT?.value;
  $: llsT            = state?.sensors?.llsT?.value;
  $: oilP            = state?.sensors?.oilP?.value;
  $: ambientT        = state?.sensors?.ambientT?.value;
  $: superheat       = state?.derived?.superheat;
  $: amps            = state?.compressor?.amps ?? 0;

  // Helper: format numeric reading with units, '--' if missing.
  function fmt(v: any, unit: string, digits = 1): string {
    if (v == null || isNaN(Number(v))) return `--${unit}`;
    return `${Number(v).toFixed(digits)}${unit}`;
  }

  // Hotspot click → dispatch upward.
  function pick(id: string) {
    dispatch('select', { id });
  }

  // Keyboard activation for SVG hotspots (Enter / Space) — required so the
  // mimic remains operable without a mouse and to satisfy svelte a11y.
  function onKey(id: string) {
    return (e: KeyboardEvent) => {
      if (e.key === 'Enter' || e.key === ' ') {
        e.preventDefault();
        pick(id);
      }
    };
  }

  // Layout constants for the condenser fan row (centered above the coil).
  // We render up to N fans evenly spaced inside the coil rectangle.
  $: fanXs = (() => {
    const xs: number[] = [];
    const n = Math.max(1, condFanCount);
    const left = 290, right = 760;
    const gap = (right - left) / n;
    for (let i = 0; i < n; i++) xs.push(left + gap * (i + 0.5));
    return xs;
  })();
</script>

<!--
  Container: scales to parent width but caps height so the page doesn't grow
  unboundedly on ultra-wide screens.  Aspect ratio matches viewBox.
-->
<div class="triton-mimic w-full" role="img" aria-label="Refrigeration unit schematic">
  <svg viewBox="0 0 1000 420" preserveAspectRatio="xMidYMid meet"
       class="w-full h-auto" style="max-height: 500px;">

    <!-- ─── Defs: marker arrows + filters ─────────────────────────────── -->
    <defs>
      <marker id="arrow-red" viewBox="0 0 10 10" refX="9" refY="5"
              markerWidth="6" markerHeight="6" orient="auto-start-reverse">
        <path d="M0,0 L10,5 L0,10 z" fill="#dc2626"/>
      </marker>
      <marker id="arrow-amber" viewBox="0 0 10 10" refX="9" refY="5"
              markerWidth="6" markerHeight="6" orient="auto-start-reverse">
        <path d="M0,0 L10,5 L0,10 z" fill="#d97706"/>
      </marker>
      <marker id="arrow-blue" viewBox="0 0 10 10" refX="9" refY="5"
              markerWidth="6" markerHeight="6" orient="auto-start-reverse">
        <path d="M0,0 L10,5 L0,10 z" fill="#2563eb"/>
      </marker>
    </defs>

    <!-- Backdrop -->
    <rect x="0" y="0" width="1000" height="420" fill="#fafafa"/>

    <!-- ─── Condenser (top) ───────────────────────────────────────────── -->
    <!-- Coil tube serpentine (decorative) + fan circles.  Click → 'condenser'. -->
    <g on:click={() => pick('condenser')} on:keydown={onKey('condenser')}
       class="hotspot" role="button" tabindex="0" aria-label="Condenser">
      <rect x="280" y="30" width="490" height="80" rx="6"
            fill="#fff" stroke="#475569" stroke-width="2"/>
      <!-- Serpentine fins -->
      {#each Array(6) as _, i}
        <path d={`M ${290 + i * 80} 45 q 35 -10 70 0 q 35 10 70 0`}
              fill="none" stroke="#94a3b8" stroke-width="1.5"/>
        <path d={`M ${290 + i * 80} 95 q 35 10 70 0 q 35 -10 70 0`}
              fill="none" stroke="#94a3b8" stroke-width="1.5"/>
      {/each}
      <text x="525" y="22" text-anchor="middle" font-size="13" fill="#334155" font-weight="600">
        Condenser
      </text>
    </g>

    <!-- Condenser fans -->
    <g class="cond-fans">
      {#each fanXs as cx, i}
        <g on:click|stopPropagation={() => pick('condFans')}
           on:keydown|stopPropagation={onKey('condFans')}
           class="hotspot" role="button" tabindex="0" aria-label="Condenser fan">
          <circle cx={cx} cy="70" r="22" fill="#e2e8f0" stroke="#475569" stroke-width="1.5"/>
          <!-- Fan blades; rotate when the corresponding stage is energized -->
          <g class:spin={i < condFanStage} style="transform-origin: {cx}px 70px;">
            <path d={`M ${cx} 50 a 8 8 0 0 1 8 8 l -8 12 z`} fill="#475569"/>
            <path d={`M ${cx + 20} 70 a 8 8 0 0 1 -8 8 l -12 -8 z`} fill="#475569"/>
            <path d={`M ${cx} 90 a 8 8 0 0 1 -8 -8 l 8 -12 z`} fill="#475569"/>
            <path d={`M ${cx - 20} 70 a 8 8 0 0 1 8 -8 l 12 8 z`} fill="#475569"/>
          </g>
          <circle cx={cx} cy="70" r="3" fill="#1e293b"/>
        </g>
      {/each}
    </g>

    <!-- Stage indicator below condenser -->
    <text x="525" y="125" text-anchor="middle" font-size="11" fill="#64748b">
      Fans {condFanStage}/{condFanCount} • Discharge {fmt(dischargeP, ' PSI', 0)} / {fmt(dischargeT, '°F', 0)}
    </text>

    <!-- ─── Discharge line (compressor → condenser, red, hot HP gas) ─ -->
    <path d="M 160 220 L 160 70 L 280 70"
          fill="none" stroke="#dc2626" stroke-width="3"
          stroke-dasharray="10 6"
          class:flow={on}
          class="line line-red"
          marker-end="url(#arrow-red)"
          role="button" tabindex="0" aria-label="Discharge line"
          on:click={() => pick('discharge')}
          on:keydown={onKey('discharge')}/>
    <text x="170" y="150" font-size="11" fill="#dc2626" font-weight="600">Discharge</text>

    <!-- ─── Liquid line (condenser → EXV, amber, HP liquid) ──────────── -->
    <!-- Cooled & condensed in the condenser, the refrigerant is now a    -->
    <!-- high-pressure liquid that flows down the right side and into the -->
    <!-- expansion valve.                                                 -->
    <path d="M 770 70 L 920 70 L 920 350 L 745 350"
          fill="none" stroke="#d97706" stroke-width="3"
          stroke-dasharray="10 6"
          class:flow={on}
          class="line line-amber"
          marker-end="url(#arrow-amber)"
          role="button" tabindex="0" aria-label="Liquid line"
          on:click={() => pick('liquid')}
          on:keydown={onKey('liquid')}/>
    <text x="930" y="180" font-size="11" fill="#d97706" font-weight="600">Liquid</text>

    <!-- ─── EXV → Evap (amber feed, expanded LP gas/liquid mix) ─────── -->
    <path d="M 700 355 L 692 355"
          fill="none" stroke="#d97706" stroke-width="3"
          stroke-dasharray="4 3"
          class:flow={on}
          class="line line-amber"
          marker-end="url(#arrow-amber)"/>

    <!-- ─── Accumulator (suction-side, between evap and compressor) ── -->
    <!-- Vertical pill on the upper-left.  Catches liquid carryover so the -->
    <!-- compressor only ingests gas — also serves as pump-down storage.   -->
    <g on:click={() => pick('accumulator')} on:keydown={onKey('accumulator')}
       class="hotspot" role="button" tabindex="0" aria-label="Accumulator">
      <rect x="30" y="130" width="70" height="80" rx="35"
            fill="#fff" stroke="#475569" stroke-width="2"/>
      <!-- Liquid level indicator (pooled liquid carryover) -->
      <rect x="36" y="180" width="58" height="24" rx="3"
            fill={on ? '#bfdbfe' : '#cbd5e1'} opacity="0.6"/>
      <text x="65" y="125" text-anchor="middle" font-size="11" fill="#334155" font-weight="600">
        Accumulator
      </text>
      <text x="65" y="170" text-anchor="middle" font-size="10" fill="#64748b">
        LL {fmt(llsT, '°F', 0)}
      </text>
    </g>

    <!-- ─── EXV (electronic expansion valve) ──────────────────────────── -->
    <g on:click={() => pick('exv')} on:keydown={onKey('exv')}
       class="hotspot" role="button" tabindex="0" aria-label="Expansion valve">
      <!-- Triangle "valve" body -->
      <polygon points="700,330 740,330 720,360"
               fill="#fff" stroke="#475569" stroke-width="2"/>
      <polygon points="700,370 740,370 720,340"
               fill="#fff" stroke="#475569" stroke-width="2"/>
      <!-- Open-pct fill bar -->
      <rect x="745" y="340" width="40" height="20" rx="3"
            fill="#e2e8f0" stroke="#94a3b8" stroke-width="1"/>
      <rect x="745" y="340" width={Math.max(0, Math.min(40, exvPct * 0.4))} height="20"
            fill="#10b981"/>
      <text x="720" y="395" text-anchor="middle" font-size="11" fill="#334155" font-weight="600">
        EXV
      </text>
      <text x="765" y="334" text-anchor="middle" font-size="9" fill="#475569">
        {fmt(exvPct, '%', 0)}
      </text>
    </g>

    <!-- ─── Evaporator (bottom) ───────────────────────────────────────── -->
    <g on:click={() => pick('evaporator')} on:keydown={onKey('evaporator')}
       class="hotspot" role="button" tabindex="0" aria-label="Evaporator">
      <rect x="240" y="320" width="450" height="70" rx="6"
            fill="#fff" stroke="#475569" stroke-width="2"/>
      {#each Array(5) as _, i}
        <path d={`M ${250 + i * 90} 335 q 35 -8 70 0 q 35 8 70 0`}
              fill="none" stroke="#94a3b8" stroke-width="1.5"/>
        <path d={`M ${250 + i * 90} 375 q 35 8 70 0 q 35 -8 70 0`}
              fill="none" stroke="#94a3b8" stroke-width="1.5"/>
      {/each}
      <text x="465" y="312" text-anchor="middle" font-size="13" fill="#334155" font-weight="600">
        Evaporator
      </text>
      <text x="465" y="408" text-anchor="middle" font-size="11" fill="#64748b">
        SH {fmt(superheat, '°F', 1)} • Suction {fmt(suctionP, ' PSI', 0)} / {fmt(suctionT, '°F', 0)}
      </text>
    </g>

    <!-- ─── Suction line (evap → accumulator → compressor, blue) ─── -->
    <!-- Two-stage path: cold gas leaves the evaporator on the left, rises -->
    <!-- into the accumulator, then drops out the bottom into the          -->
    <!-- compressor suction port.                                          -->
    <!-- Evap → Accumulator inlet -->
    <path d="M 240 355 L 20 355 L 20 200 L 30 200"
          fill="none" stroke="#2563eb" stroke-width="3"
          stroke-dasharray="10 6"
          class:flow={on}
          class="line line-blue"
          marker-end="url(#arrow-blue)"
          role="button" tabindex="0" aria-label="Suction line"
          on:click={() => pick('suction')}
          on:keydown={onKey('suction')}/>
    <!-- Accumulator outlet → Compressor suction -->
    <path d="M 100 200 L 130 200 L 130 220"
          fill="none" stroke="#2563eb" stroke-width="3"
          stroke-dasharray="10 6"
          class:flow={on}
          class="line line-blue"
          marker-end="url(#arrow-blue)"/>
    <text x="260" y="350" font-size="11" fill="#2563eb" font-weight="600">Suction</text>

    <!-- ─── Compressor (left) ─────────────────────────────────────────── -->
    <g on:click={() => pick('compressor')} on:keydown={onKey('compressor')}
       class="hotspot" role="button" tabindex="0" aria-label="Compressor">
      <rect x="80" y="220" width="100" height="60" rx="8"
            fill={on ? '#dcfce7' : '#f1f5f9'}
            stroke="#475569" stroke-width="2"/>
      <text x="130" y="246" text-anchor="middle" font-size="13" fill="#334155" font-weight="600">
        Compressor
      </text>
      <text x="130" y="265" text-anchor="middle" font-size="11" fill={on ? '#15803d' : '#64748b'}>
        {on ? 'RUN' : 'OFF'} • {fmt(amps, ' A', 1)}
      </text>
    </g>

    <!-- ─── Oil pressure tag (under compressor) ───────────────────────── -->
    <g on:click={() => pick('oil')} on:keydown={onKey('oil')}
       class="hotspot" role="button" tabindex="0" aria-label="Oil pressure">
      <rect x="80" y="290" width="100" height="20" rx="3"
            fill="#fff" stroke="#94a3b8" stroke-width="1"/>
      <text x="130" y="304" text-anchor="middle" font-size="10" fill="#475569">
        Oil {fmt(oilP, ' PSI', 0)}
      </text>
    </g>

    <!-- ─── Ambient temperature tag (top-left corner) ─────────────────── -->
    <g on:click={() => pick('ambient')} on:keydown={onKey('ambient')}
       class="hotspot" role="button" tabindex="0" aria-label="Ambient temperature">
      <rect x="20" y="20" width="100" height="28" rx="4"
            fill="#fff" stroke="#94a3b8" stroke-width="1"/>
      <text x="70" y="38" text-anchor="middle" font-size="11" fill="#475569">
        Ambient {fmt(ambientT, '°F', 0)}
      </text>
    </g>

    <!-- Mode badge top-right -->
    <g>
      <rect x="880" y="20" width="100" height="28" rx="4"
            fill={state?.manualMode === 'auto' ? '#dbeafe' : '#fef3c7'}
            stroke={state?.manualMode === 'auto' ? '#3b82f6' : '#d97706'} stroke-width="1"/>
      <text x="930" y="38" text-anchor="middle" font-size="11" fill="#1e293b" font-weight="600">
        {state?.manualMode ?? 'auto'}
      </text>
    </g>
  </svg>
</div>

<style>
  /* Refrigerant-flow animation: shift the dash pattern leftward along each
     line at a steady rate.  The 'flow' class only animates while the
     compressor is on; otherwise the dashes sit still. */
  .line {
    cursor: pointer;
  }
  .line.flow {
    animation: dash-flow 1.2s linear infinite;
  }
  @keyframes dash-flow {
    to { stroke-dashoffset: -32; }
  }

  /* Spinning condenser/evap fans.  Static otherwise. */
  .spin {
    animation: spin 1.5s linear infinite;
  }
  @keyframes spin {
    to { transform: rotate(360deg); }
  }

  /* Visible feedback for clickable groups without using inline JS. */
  .hotspot { cursor: pointer; }
  .hotspot:hover rect:not([fill="none"]),
  .hotspot:hover polygon,
  .hotspot:hover circle {
    filter: brightness(0.95);
  }
  .hotspot:focus { outline: none; }
  .hotspot:focus-visible rect,
  .hotspot:focus-visible circle,
  .hotspot:focus-visible polygon {
    stroke: #2563eb;
    stroke-width: 2.5;
  }

  /* Reduce motion preference: stop all animation. */
  @media (prefers-reduced-motion: reduce) {
    .line.flow, .spin { animation: none; }
  }
</style>
