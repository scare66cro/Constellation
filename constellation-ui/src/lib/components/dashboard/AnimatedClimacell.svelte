<script lang="ts">
  // Climacell evaporative cooler. When active, blue droplets fall
  // continuously down through the pad zone. When off, the pad is
  // dry / gray. Mirrors the physical reality — operators see "water
  // is flowing through the pad" and immediately understand state.
  export let active: boolean = false;
  export let size: number = 80;

  // 7 droplets staggered for a continuous "rain" effect.
  const drops = [
    { x: 22, delay: 0.0 },
    { x: 32, delay: 0.4 },
    { x: 42, delay: 0.8 },
    { x: 52, delay: 0.2 },
    { x: 62, delay: 0.6 },
    { x: 72, delay: 1.0 },
    { x: 82, delay: 0.3 },
  ];
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

  <!-- Falling droplets (only animated when active) -->
  {#if active}
    {#each drops as drop, i (i)}
      <ellipse class="drop" cx={drop.x} cy="0" rx="2" ry="3"
               fill="#0284c7"
               style="animation-delay: {drop.delay}s"/>
    {/each}
  {/if}

  <!-- Snowflake symbol when active (cooling indication) -->
  {#if active}
    <text x="50" y="55" text-anchor="middle" font-size="22"
          fill="#0369a1" opacity="0.4">❄</text>
  {/if}
</svg>

<style>
  svg { display: block; }
  .drop {
    animation: rain 1.4s linear infinite;
  }
  @keyframes rain {
    from { transform: translateY(0); opacity: 0; }
    10%  { opacity: 0.9; }
    90%  { opacity: 0.9; }
    to   { transform: translateY(95px); opacity: 0; }
  }
</style>
