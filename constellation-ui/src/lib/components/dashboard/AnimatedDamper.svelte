<script lang="ts">
  // 5-slat damper. Slats pivot from horizontal (0% = closed) to ~85°
  // (100% = fully open) via CSS transition — operator sees the
  // physical state of the louver match the % value. Smooth animated
  // transitions when the % changes.
  export let pct: number = 0;       // 0..100
  export let label: string = '';
  export let width: number = 100;
  export let height: number = 90;

  $: pctClamped = Math.max(0, Math.min(100, pct));
  $: angleDeg   = (pctClamped / 100) * 75;   // 0° closed, 75° wide open
  $: active     = pctClamped > 5;
</script>

<svg viewBox="0 0 100 90" width={width} height={height}>
  <!-- Frame -->
  <rect x="2" y="2" width="96" height="86" rx="3" fill="#f8fafc" stroke="#475569" stroke-width="1.5"/>

  <!-- Slats (5 horizontal at rest, rotated by --angle when open) -->
  {#each [16, 30, 44, 58, 72] as y, i (i)}
    <g class="slat" style="--y: {y}px; --angle: {angleDeg}deg">
      <rect x="12" y={y - 3} width="76" height="6" rx="1"
            fill={active ? '#60a5fa' : '#94a3b8'}
            stroke={active ? '#2563eb' : '#475569'}
            stroke-width="0.5"/>
      <line x1="50" y1={y - 3} x2="50" y2={y + 3} stroke="#1e293b" stroke-width="0.5"/>
    </g>
  {/each}

  {#if label}
    <text x="50" y="86" text-anchor="middle" font-size="6" fill="#475569">{label}</text>
  {/if}
</svg>

<style>
  svg { display: block; }
  .slat {
    transform-origin: 50px var(--y);
    transform: rotateX(var(--angle, 0deg));
    transition: transform 0.6s ease-in-out;
    /* CSS rotateX gives a 3D-ish "louver tilting back" look without
     * needing perspective context. Browsers all support this. */
  }
</style>
