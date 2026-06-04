<script lang="ts">
  // Curved airflow paths drawn over the pile cross-section. Two
  // arcs sweep from intake side to far wall, with arrows traveling
  // along the path when the system is actively pushing air. Speed
  // of arrow travel scales with `fanPct` so faster fan = faster
  // visual flow. When fan is off, the arrows pause.
  export let fanPct: number = 0;
  export let running: boolean = false;
  export let arrowCount: number = 6;

  $: period = fanPct > 0 ? Math.max(1.2, 5.5 - (fanPct / 100) * 4.0) : 0;
  $: active = running && fanPct > 0;

  // Pre-compute stagger offsets for N arrows.
  $: offsets = Array.from({ length: arrowCount }, (_, i) => i / arrowCount);
</script>

<svg viewBox="0 0 400 200" preserveAspectRatio="none"
     class="airflow-svg" class:active
     style="--period: {period}s">

  <!-- Define arc paths once, then reference for animateMotion -->
  <defs>
    <path id="airpath-top"
          d="M 20 30 Q 200 5 380 30 Q 200 60 20 60"
          fill="none"/>
    <path id="airpath-bot"
          d="M 20 150 Q 200 175 380 150 Q 200 190 20 190"
          fill="none"/>
  </defs>

  <!-- Visible faint path lines so operators see "this is the flow" -->
  <use href="#airpath-top" stroke="#bae6fd" stroke-width="1.5" opacity="0.5" fill="none" stroke-dasharray="4 4"/>
  <use href="#airpath-bot" stroke="#bae6fd" stroke-width="1.5" opacity="0.5" fill="none" stroke-dasharray="4 4"/>

  {#if active}
    <!-- Arrows traveling along top arc -->
    {#each offsets as off, i (i)}
      <g class="airflow-arrow">
        <text font-size="14" fill="#0369a1" opacity="0.85">
          <animateMotion dur="{period}s" repeatCount="indefinite"
                         begin="-{off * period}s" rotate="auto">
            <mpath href="#airpath-top"/>
          </animateMotion>
          ➤
        </text>
      </g>
    {/each}

    <!-- Arrows traveling along bottom arc -->
    {#each offsets as off, i (i + arrowCount)}
      <g class="airflow-arrow">
        <text font-size="14" fill="#0369a1" opacity="0.85">
          <animateMotion dur="{period}s" repeatCount="indefinite"
                         begin="-{off * period}s" rotate="auto">
            <mpath href="#airpath-bot"/>
          </animateMotion>
          ➤
        </text>
      </g>
    {/each}
  {/if}
</svg>

<style>
  .airflow-svg {
    width: 100%;
    height: 100%;
    pointer-events: none;
  }
</style>
