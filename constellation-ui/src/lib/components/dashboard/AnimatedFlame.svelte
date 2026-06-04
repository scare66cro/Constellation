<script lang="ts">
  // Flame icon for burner / plenum heat / cavity heat. Two layered
  // flame paths (outer orange + inner yellow) with offset flicker
  // animations create a natural shimmer. When off, both paths fade
  // to gray and stop animating.
  export let active: boolean = false;
  export let size: number = 60;
  export let label: string = '';
</script>

<svg viewBox="0 0 60 80" width={size} height={size} class:active>
  <!-- Base ring (burner port) -->
  <ellipse cx="30" cy="72" rx="18" ry="4"
           fill={active ? '#1f2937' : '#e5e7eb'}
           stroke={active ? '#111827' : '#9ca3af'}
           stroke-width="0.5"/>

  <!-- Outer flame -->
  <path class="flame outer"
        d="M 30 70
           C 18 60, 14 45, 22 30
           C 18 38, 26 44, 26 32
           C 30 20, 38 28, 36 36
           C 40 26, 48 36, 42 50
           C 48 55, 42 65, 30 70 Z"
        fill={active ? '#fb923c' : '#cbd5e1'}/>

  <!-- Inner flame -->
  <path class="flame inner"
        d="M 30 68
           C 24 60, 22 50, 26 42
           C 24 46, 30 50, 28 42
           C 32 34, 36 42, 34 48
           C 38 44, 40 52, 36 58
           C 38 62, 34 66, 30 68 Z"
        fill={active ? '#fef08a' : '#d1d5db'}/>

  <!-- Core hot spot -->
  {#if active}
    <ellipse class="core" cx="30" cy="58" rx="3" ry="6"
             fill="#ffffff" opacity="0.85"/>
  {/if}

  {#if label}
    <text x="30" y="78" text-anchor="middle" font-size="4" fill="#475569">{label}</text>
  {/if}
</svg>

<style>
  svg { display: block; }
  .flame {
    transform-origin: 30px 70px;
  }
  /* Inner flame flickers slightly faster than outer for natural feel */
  .active .outer {
    animation: flicker 0.7s ease-in-out infinite alternate;
  }
  .active .inner {
    animation: flicker-inner 0.4s ease-in-out infinite alternate;
  }
  .active .core {
    animation: pulse 0.5s ease-in-out infinite alternate;
  }
  @keyframes flicker {
    0%   { transform: scaleY(1.0) scaleX(1.0); opacity: 0.92; }
    100% { transform: scaleY(1.08) scaleX(0.96); opacity: 1.0; }
  }
  @keyframes flicker-inner {
    0%   { transform: scaleY(1.0) scaleX(1.02); opacity: 0.9; }
    100% { transform: scaleY(0.92) scaleX(0.98); opacity: 1.0; }
  }
  @keyframes pulse {
    0%   { opacity: 0.6; }
    100% { opacity: 1.0; }
  }
</style>
