<script lang="ts">
  // Onion-cure mode intake assembly. Replaces the standard
  // damper/climacell/fan stack during ST_AIRCURE / ST_BURNERCURE.
  // Mirrors the Gellert mockup page 16 — vertical curing coil with
  // dual burner flames flanking it, fan still running below.
  //
  // The cure path bypasses fresh-air cooling: the operator is
  // running a controlled heat-rise to dry / heal the onion crop,
  // not bringing in outside air. Visually conveying "this is a
  // completely different mode of operation" matters because the
  // setpoints, alarms, and equipment behavior all change.
  import AnimatedFan   from "./AnimatedFan.svelte";
  import AnimatedFlame from "./AnimatedFlame.svelte";

  export let fanPct: number = 0;
  export let fanRunning: boolean = false;
  export let leftBurnerOn: boolean = false;
  export let rightBurnerOn: boolean = false;
  /** 0..100 — how hot the cure coil is rendered. Maps to color
   * intensity (cool=gray, hot=orange-red glowing). */
  export let coilHeat: number = 0;

  $: heatNorm = Math.max(0, Math.min(100, coilHeat)) / 100;
  $: coilColor = heatNorm > 0
    ? `rgb(${255 - heatNorm * 30}, ${130 + (1 - heatNorm) * 60}, ${40 + (1 - heatNorm) * 80})`
    : '#94a3b8';
  $: coilGlow = heatNorm > 0.4 ? `0 0 ${heatNorm * 12}px rgba(255, 90, 0, 0.6)` : 'none';
</script>

<div class="flex flex-col gap-2">
  <div class="text-xs text-center font-bold text-amber-700 tracking-wider">CURE</div>

  <!-- Dual burners flanking a vertical cure coil -->
  <div class="border-2 rounded-lg p-2 flex justify-center items-end gap-1
              {leftBurnerOn || rightBurnerOn ? 'bg-amber-50 border-amber-500' : 'bg-white border-gray-300'}">
    <!-- Left burner -->
    <div class="flex flex-col items-center">
      <AnimatedFlame active={leftBurnerOn} size={36}/>
      <div class="text-[9px] {leftBurnerOn ? 'text-amber-700 font-bold' : 'text-gray-400'}">L</div>
    </div>

    <!-- Vertical cure coil — heated radiator pattern -->
    <div class="cure-coil-wrap">
      <svg viewBox="0 0 24 80" width="32" height="80" class="cure-coil">
        <!-- Vertical pipe -->
        <rect x="10" y="4" width="4" height="72" rx="1"
              fill={coilColor}
              stroke="#475569"
              stroke-width="0.5"
              style="filter: drop-shadow({coilGlow})"/>
        <!-- Heat fins (horizontal blades) -->
        {#each [10, 18, 26, 34, 42, 50, 58, 66] as y (y)}
          <rect x="3" y={y} width="18" height="3" rx="1"
                fill={coilColor}
                opacity="0.85"
                stroke="#475569"
                stroke-width="0.3"/>
        {/each}
        <!-- Heat shimmer waves (only when hot) -->
        {#if heatNorm > 0.3}
          {#each [0, 1, 2] as offset (offset)}
            <path class="shimmer"
                  d="M 4 2 Q 12 -2 20 2"
                  fill="none"
                  stroke="rgba(255,180,80,0.6)"
                  stroke-width="0.8"
                  style="animation-delay: {offset * 0.4}s"/>
          {/each}
        {/if}
      </svg>
    </div>

    <!-- Right burner -->
    <div class="flex flex-col items-center">
      <AnimatedFlame active={rightBurnerOn} size={36}/>
      <div class="text-[9px] {rightBurnerOn ? 'text-amber-700 font-bold' : 'text-gray-400'}">R</div>
    </div>
  </div>

  <!-- Fan still running underneath (pushes cured air into the plenum) -->
  <div class="border-2 rounded-lg p-2 flex flex-col items-center {fanRunning ? 'bg-emerald-50 border-emerald-500' : 'bg-white border-gray-300'}">
    <div class="text-xs {fanRunning ? 'text-emerald-800' : 'text-gray-500'} mb-1">Fan</div>
    <AnimatedFan pct={fanPct} running={fanRunning} size={70}/>
    <div class="text-lg font-bold {fanRunning ? 'text-emerald-700' : 'text-gray-400'} mt-1">
      {fanRunning ? `${fanPct}%` : 'OFF'}
    </div>
  </div>

  <div class="text-[10px] text-center text-amber-700 font-bold border border-amber-300 bg-amber-50 rounded px-1 py-0.5">
    Curing — fresh-air bypass
  </div>
</div>

<style>
  .cure-coil-wrap {
    display: flex;
    align-items: center;
  }
  .shimmer {
    animation: shimmer 1.2s ease-in-out infinite;
    transform-origin: 12px 2px;
  }
  @keyframes shimmer {
    0%   { transform: translateY(0) scaleX(0.9); opacity: 0.0; }
    50%  { transform: translateY(-6px) scaleX(1.1); opacity: 0.7; }
    100% { transform: translateY(-14px) scaleX(0.8); opacity: 0.0; }
  }
</style>
