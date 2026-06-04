<script lang="ts">
  // Temperature sensor pill overlaid on the pile cross-section.
  // Background color shifts from blue (cold) → on-target → red
  // (warm) based on (value - setpoint) delta. Operators see hot
  // spots instantly without reading numbers.
  //
  // Now also renders an inline trend sparkline: the badge keeps a
  // 60-sample rolling buffer of `value`, sampled every 10 s (so
  // ~10 minutes of history fits). Line color reflects slope
  // (rising red, falling blue, flat gray), with a dashed setpoint
  // reference line behind. Operators can spot a climbing/falling
  // sensor before it triggers an alarm.
  import { onMount, onDestroy } from "svelte";
  import Sparkline from "./Sparkline.svelte";

  export let label: string = '';
  export let value: number | null = null;
  export let setpoint: number | null = null;
  export let unit: string = '°F';
  /** Sample interval in milliseconds. Default 10s. */
  export let sampleMs: number = 10_000;
  /** Max history samples kept (rolling). Default 60 = 10 minutes
   * at 10s sample rate. */
  export let historyMax: number = 60;
  /** When true, hides the inline sparkline (e.g. very small badges). */
  export let compact: boolean = false;

  let history: number[] = [];
  let timer: ReturnType<typeof setInterval> | null = null;

  onMount(() => {
    timer = setInterval(() => {
      if (value != null && !Number.isNaN(value)) {
        history = [...history.slice(-(historyMax - 1)), value];
      }
    }, sampleMs);
  });
  onDestroy(() => {
    if (timer) clearInterval(timer);
  });

  // Color band from delta. Tuned for storage scale (±5°F is a lot).
  function bg(delta: number | null): string {
    if (delta == null) return 'bg-orange-200 bg-opacity-90';
    if (delta < -3)    return 'bg-sky-300 bg-opacity-95';
    if (delta < -1)    return 'bg-sky-200 bg-opacity-95';
    if (delta < 1)     return 'bg-emerald-200 bg-opacity-95';
    if (delta < 3)     return 'bg-amber-200 bg-opacity-95';
    return                    'bg-red-300 bg-opacity-95';
  }
  function ring(delta: number | null): string {
    if (delta == null) return 'border-orange-600';
    if (Math.abs(delta) > 3) return 'border-red-700';
    if (Math.abs(delta) > 1) return 'border-amber-700';
    return 'border-emerald-700';
  }

  $: delta = (value != null && setpoint != null) ? value - setpoint : null;
  $: bgClass = bg(delta);
  $: ringClass = ring(delta);
  $: display = value == null ? '--' : value.toFixed(1);
</script>

<div class="{bgClass} {ringClass} border rounded-md px-2 py-1 shadow-md inline-block min-w-[64px]">
  <div class="text-[10px] text-gray-800 leading-tight whitespace-nowrap">{label}</div>
  <div class="font-bold text-sm leading-tight">{display}{unit}</div>
  {#if !compact && history.length > 1}
    <div class="mt-0.5 -mx-1">
      <Sparkline {history} {setpoint} width={56} height={12}/>
    </div>
  {/if}
</div>
