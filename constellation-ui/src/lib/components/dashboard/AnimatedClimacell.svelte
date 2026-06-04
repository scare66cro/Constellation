<script lang="ts">
  // Climacell evaporative cooler. When active, blue droplets fall
  // continuously down through the pad zone. When off, the pad is
  // dry / gray. Mirrors the physical reality — operators see "water
  // is flowing through the pad" and immediately understand state.
  export let active: boolean = false;
  export let size: number = 80;
  /** Demand 0..100. Higher = more droplets and faster fall, signaling
   * the climacell is delivering more evaporative cooling. Default 50
   * (steady moderate flow). */
  export let demand: number = 50;

  // Always render the same 7 anchor positions; show fewer droplets
  // at low demand by shrinking the visible slice of the array.
  const allDrops = [
    { x: 22, delay: 0.0 },
    { x: 32, delay: 0.4 },
    { x: 42, delay: 0.8 },
    { x: 52, delay: 0.2 },
    { x: 62, delay: 0.6 },
    { x: 72, delay: 1.0 },
    { x: 82, delay: 0.3 },
  ];
  $: demandPct = Math.max(0, Math.min(100, demand));
  $: dropCount = Math.max(3, Math.round((demandPct / 100) * allDrops.length));
  $: drops    = allDrops.slice(0, dropCount);
  // Fall period scales with demand — fast at high demand, slow at low.
  $: fallPeriod = (1.8 - (demandPct / 100) * 0.9).toFixed(2);
</script>

<svg viewBox="0 0 100 100" width={size} height={size} class:active>
  <!-- Pad frame -->
  <rect x="10" y="10" width="80" height="80" rx="4"
        fill={active ? '#e0f2fe' : '#f1f5f9'}
        stroke={active ? '#0284c7' : '#94a3b8'}
        stroke-width="1.5"/>

  <!-- Honeycomb pattern to suggest the evap-pad media -->
  {#each [22, 36, 50, 64, 78] as y, ri (y)}
    {#each [18, 30, 42, 54, 66, 78] as x, ci (x)}
      <circle cx={x + ((ri % 2) ? 6 : 0)} cy={y} r="3"
              fill="none"
              stroke={active ? '#7dd3fc' : '#cbd5e1'}
              stroke-width="0.5"/>
    {/each}
  {/each}

  <!-- Falling droplets (only animated when active). Count scales
       with demand; fall period also scales (faster at high demand). -->
  {#if active}
    {#each drops as drop, i (i)}
      <ellipse class="drop" cx={drop.x} cy="0" rx="2" ry="3"
               fill="#0284c7"
               style="animation-delay: {drop.delay}s; animation-duration: {fallPeriod}s"/>
    {/each}
  {/if}
</svg>

<style>
  svg { display: block; }
  .drop {
    animation: rain 1.4s linear infinite;
    /* animation-duration overridden inline per-drop via demand prop */
  }
  @keyframes rain {
    from { transform: translateY(0); opacity: 0; }
    10%  { opacity: 0.9; }
    90%  { opacity: 0.9; }
    to   { transform: translateY(95px); opacity: 0; }
  }
</style>
