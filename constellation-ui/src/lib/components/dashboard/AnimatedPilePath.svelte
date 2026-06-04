<script lang="ts">
  // Distribution-duct airflow. Replaces the "curved arcs over the
  // top of the pile" model with the actual physical flow:
  //
  //   fan → plenum → perforated duct along the pile floor →
  //     air rises vertically through the product distributing
  //     evenly from bottom to top
  //
  // Vertical arrows ↑ rise from each outlet hole in the duct,
  // fading in near the floor and fading out near the pile top.
  // Speed scales with fanPct (faster fan = quicker rise time).
  export let fanPct: number = 0;
  export let running: boolean = false;
  export let outlets: number = 10;

  $: period = fanPct > 0 ? Math.max(1.4, 5.5 - (fanPct / 100) * 3.8) : 0;
  $: active = running && fanPct > 0;
  $: outletXs = Array.from({ length: outlets }, (_, i) => 30 + (i * (340 / (outlets - 1))));
</script>

<svg viewBox="0 0 400 200" preserveAspectRatio="none" class="duct-svg" class:active>
  <!-- Distribution duct: horizontal pipe along the pile floor -->
  <rect x="14" y="174" width="372" height="16" rx="3"
        fill={active ? '#0ea5e9' : '#64748b'}
        stroke={active ? '#0369a1' : '#334155'}
        stroke-width="1.5"/>
  <!-- Duct end caps -->
  <ellipse cx="14" cy="182" rx="3" ry="8"
           fill={active ? '#0369a1' : '#475569'}/>
  <ellipse cx="386" cy="182" rx="3" ry="8"
           fill={active ? '#0369a1' : '#475569'}/>

  <!-- Outlet vents along the top of the duct -->
  {#each outletXs as x (x)}
    <circle cx={x} cy="174" r="2.5" fill="#0f172a"/>
  {/each}

  <!-- Rising air arrows from each outlet (only when fan running) -->
  {#if active}
    {#each outletXs as x, i (i)}
      <g class="rising-arrow" style="--delay: {(i % 3) * 0.4}s; --period: {period}s">
        <text x={x} y="174"
              font-size="16"
              text-anchor="middle"
              fill="#0369a1"
              opacity="0">↑</text>
      </g>
    {/each}
    <!-- Second pass with offset delay → continuous "wave" of rising air -->
    {#each outletXs as x, i (i + 100)}
      <g class="rising-arrow"
         style="--delay: {(i % 3) * 0.4 + period * 0.5}s; --period: {period}s">
        <text x={x} y="174"
              font-size="16"
              text-anchor="middle"
              fill="#0369a1"
              opacity="0">↑</text>
      </g>
    {/each}
  {/if}

  <!-- Plenum-to-duct entry (left side hint) -->
  <path d="M 4 182 Q 0 182 0 175 L 0 200 L 14 200 L 14 190"
        fill={active ? '#0ea5e9' : '#64748b'}
        opacity="0.6"/>
</svg>

<style>
  .duct-svg {
    width: 100%;
    height: 100%;
    pointer-events: none;
  }
  .rising-arrow text {
    animation: rise var(--period, 0s) linear infinite;
    animation-delay: var(--delay, 0s);
  }
  @keyframes rise {
    0%   { transform: translateY(0);     opacity: 0;    }
    15%  { opacity: 0.9;                                }
    85%  { opacity: 0.7;                                }
    100% { transform: translateY(-165px); opacity: 0;   }
  }
</style>
