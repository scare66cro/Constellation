<script lang="ts">
  // Tiny inline trend chart. Renders the supplied numeric history
  // (oldest → newest) as a polyline. Color reflects the recent
  // slope: red rising, sky blue falling, gray flat / not enough
  // data. Designed to fit inside a SensorBadge but reusable for any
  // micro-chart use.
  export let history: number[] = [];
  export let width: number = 50;
  export let height: number = 14;
  /** Optional: setpoint reference line drawn dashed across the chart. */
  export let setpoint: number | null = null;

  $: hasData = history.length > 1;
  $: range = computeRange(history, setpoint);

  function computeRange(h: number[], sp: number | null): { lo: number; hi: number } {
    if (h.length === 0) return { lo: 0, hi: 1 };
    let lo = h[0], hi = h[0];
    for (const v of h) { if (v < lo) lo = v; if (v > hi) hi = v; }
    if (sp != null) { if (sp < lo) lo = sp; if (sp > hi) hi = sp; }
    if (hi === lo) hi = lo + 0.5;   // avoid div-by-zero on flat data
    // Pad slightly so the line doesn't kiss the edges.
    const pad = (hi - lo) * 0.1;
    return { lo: lo - pad, hi: hi + pad };
  }

  $: points = hasData ? pointsStr(history, range, width, height) : '';
  function pointsStr(h: number[], r: { lo: number; hi: number }, w: number, hh: number): string {
    const step = h.length === 1 ? w : w / (h.length - 1);
    return h.map((v, i) => {
      const x = i * step;
      const y = hh - ((v - r.lo) / (r.hi - r.lo)) * hh;
      return `${x.toFixed(1)},${y.toFixed(1)}`;
    }).join(' ');
  }

  $: slope = computeSlope(history);
  function computeSlope(h: number[]): number {
    if (h.length < 3) return 0;
    const tail = h.slice(-5);   // average over last 5 samples
    return tail[tail.length - 1] - tail[0];
  }
  $: strokeColor =
       slope > 0.3  ? '#dc2626' :   // rising > 0.3°F
       slope < -0.3 ? '#0284c7' :   // falling > 0.3°F
                      '#64748b';    // flat

  $: setpointY = setpoint != null
    ? height - ((setpoint - range.lo) / (range.hi - range.lo)) * height
    : null;
</script>

{#if hasData}
  <svg viewBox="0 0 {width} {height}" width={width} height={height}
       class="block" preserveAspectRatio="none">
    {#if setpointY != null}
      <line x1="0" y1={setpointY} x2={width} y2={setpointY}
            stroke="#94a3b8" stroke-width="0.5" stroke-dasharray="2 2"/>
    {/if}
    <polyline points={points}
              fill="none"
              stroke={strokeColor}
              stroke-width="1.2"
              stroke-linecap="round"
              stroke-linejoin="round"/>
  </svg>
{/if}
