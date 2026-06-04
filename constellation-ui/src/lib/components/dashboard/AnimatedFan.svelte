<script lang="ts">
  // SVG fan with 5 blades. Spin speed proportional to `pct` (0..100).
  // pct === 0 (or `running` false) → static, gray. >0 → green tint
  // and a continuous CSS rotation. Spin period: 2.4s @ 25%, 0.6s @ 100%.
  export let pct: number = 0;       // 0..100 fan output
  export let running: boolean = false;
  export let size: number = 80;

  // Period in seconds. Bottom out at 0.4s so 100% still looks like a
  // fast fan, not a strobe.
  $: period = pct > 0 ? Math.max(0.4, 3.0 - (pct / 100) * 2.6) : 0;
  $: active = running && pct > 0;
</script>

<svg viewBox="0 0 100 100" width={size} height={size} class:active>
  <!-- Hub plate -->
  <circle cx="50" cy="50" r="48" fill="#f5f5f5" stroke="#888" stroke-width="2"/>
  <circle cx="50" cy="50" r="42" fill="none" stroke="#bbb" stroke-width="0.5"/>

  <!-- Blades (5 evenly spaced) -->
  <g class="blades" style="--period: {period}s">
    {#each [0, 72, 144, 216, 288] as angle (angle)}
      <g transform="rotate({angle} 50 50)">
        <path
          d="M 50 50 Q 60 22 50 12 Q 40 22 50 50 Z"
          fill={active ? '#16a34a' : '#9ca3af'}
          stroke={active ? '#15803d' : '#6b7280'}
          stroke-width="0.5"
          stroke-linejoin="round"
        />
      </g>
    {/each}
  </g>

  <!-- Center hub on top of blades -->
  <circle cx="50" cy="50" r="8" fill="#444"/>
  <circle cx="50" cy="50" r="3" fill="#222"/>
</svg>

<style>
  svg {
    display: block;
  }
  .blades {
    transform-origin: 50px 50px;
    animation: spin var(--period, 0s) linear infinite;
    animation-play-state: paused;
  }
  .active .blades {
    animation-play-state: running;
  }
  @keyframes spin {
    to { transform: rotate(360deg); }
  }
</style>
