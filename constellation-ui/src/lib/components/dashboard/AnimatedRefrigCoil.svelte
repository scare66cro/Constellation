<script lang="ts">
  // Refrigeration evaporator coil. Serpentine pipe with refrigerant
  // animation: when active, blue "frost" gradient flows along the
  // coil path. Intensity of the frost color reflects `pct` (0..100).
  export let pct: number = 0;
  export let active: boolean = false;
  export let size: number = 100;

  $: intensity = Math.max(0, Math.min(100, pct)) / 100;
  $: pipeFill  = active ? `rgb(${56 - intensity * 30}, ${189 - intensity * 100}, ${248})` : '#cbd5e1';
</script>

<svg viewBox="0 0 100 80" width={size} height={size * 0.8} class:active>
  <!-- Backdrop -->
  <rect x="2" y="2" width="96" height="76" rx="3"
        fill={active ? '#eff6ff' : '#f8fafc'}
        stroke={active ? '#3b82f6' : '#94a3b8'}
        stroke-width="1"/>

  <!-- Serpentine coil. 4 horizontal runs connected by U-bends. -->
  <path d="M 10 18
           L 90 18
           A 8 8 0 0 1 90 34
           L 10 34
           A 8 8 0 0 0 10 50
           L 90 50
           A 8 8 0 0 1 90 66
           L 10 66"
        fill="none"
        stroke={pipeFill}
        stroke-width="5"
        stroke-linecap="round"/>

  <!-- Fins overlaid as faint vertical lines -->
  {#each [16, 24, 32, 40, 48, 56, 64, 72, 80, 88] as x (x)}
    <line x1={x} y1="10" x2={x} y2="74"
          stroke={active ? '#bfdbfe' : '#e2e8f0'}
          stroke-width="0.5"/>
  {/each}

  <!-- Frost dots flowing along coil when active. Each dot starts at
       a different point along the path so the effect looks continuous. -->
  {#if active}
    {#each [0, 0.3, 0.6] as offset, i (i)}
      <circle r="3"
              fill="#fff"
              opacity="0.9"
              class="frost-dot"
              style="animation-delay: {offset}s">
        <animateMotion dur="2.4s" repeatCount="indefinite" begin="{offset}s"
          path="M 10 18 L 90 18 A 8 8 0 0 1 90 34 L 10 34 A 8 8 0 0 0 10 50 L 90 50 A 8 8 0 0 1 90 66 L 10 66"/>
      </circle>
    {/each}
  {/if}
</svg>

<style>
  svg { display: block; }
</style>
