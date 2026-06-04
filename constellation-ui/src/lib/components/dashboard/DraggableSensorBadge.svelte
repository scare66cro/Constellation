<script lang="ts">
  // Drag-positionable wrapper around SensorBadge. Operators can drag
  // each sensor pill to its real spatial position on the pile cross-
  // section; positions persist per-site in localStorage so they
  // survive page reloads. Position is stored as a {x%, y%} fraction
  // of the parent container so it scales correctly when the dashboard
  // is viewed at different display sizes.
  //
  // Usage:
  //   <div class="relative" style="aspect-ratio: 4/3">
  //     <DraggableSensorBadge id="pt-n-bay" label="PT N Bay"
  //       value={47.9} setpoint={46.0} initialX={0.15} initialY={0.20}/>
  //   </div>
  //
  // Editable / locked: set `editable=false` for production view so
  // operators can't accidentally drag a sensor off the pile while
  // troubleshooting. Default true for the preview dashboard.
  import { onMount, onDestroy } from "svelte";
  import SensorBadge from "./SensorBadge.svelte";

  export let id: string = '';
  export let label: string = '';
  export let value: number | null = null;
  export let setpoint: number | null = null;
  /** Fractional starting positions (0..1) within the parent. Used
   * when nothing is saved in localStorage yet. */
  export let initialX: number = 0.5;
  export let initialY: number = 0.5;
  /** Allow drag (preview), or lock (production). */
  export let editable: boolean = true;
  /** Storage key namespace. Multiple dashboards or sites get their
   * own layout by changing this. */
  export let storageNs: string = 'dashboard.sensorLayout';

  let xPct = initialX;
  let yPct = initialY;
  let dragging = false;
  let pointerStart: { x: number; y: number } | null = null;
  let posStart: { x: number; y: number } | null = null;
  let containerEl: HTMLDivElement | null = null;
  let containerParent: HTMLElement | null = null;

  $: storageKey = `${storageNs}.${id}`;

  onMount(() => {
    try {
      const saved = localStorage.getItem(storageKey);
      if (saved) {
        const { x, y } = JSON.parse(saved);
        if (typeof x === 'number' && typeof y === 'number') {
          xPct = clampFrac(x);
          yPct = clampFrac(y);
        }
      }
    } catch {}
    containerParent = containerEl?.parentElement ?? null;
  });

  onDestroy(() => {
    window.removeEventListener('pointermove', onPointerMove);
    window.removeEventListener('pointerup',   onPointerUp);
  });

  function clampFrac(v: number): number {
    return Math.max(0, Math.min(1, v));
  }

  function persist() {
    try {
      localStorage.setItem(storageKey, JSON.stringify({ x: xPct, y: yPct }));
    } catch {}
  }

  function onPointerDown(e: PointerEvent) {
    if (!editable || !containerParent) return;
    dragging = true;
    pointerStart = { x: e.clientX, y: e.clientY };
    posStart    = { x: xPct, y: yPct };
    (e.target as HTMLElement).setPointerCapture?.(e.pointerId);
    window.addEventListener('pointermove', onPointerMove);
    window.addEventListener('pointerup',   onPointerUp);
    e.preventDefault();
  }

  function onPointerMove(e: PointerEvent) {
    if (!dragging || !pointerStart || !posStart || !containerParent) return;
    const rect = containerParent.getBoundingClientRect();
    const dxFrac = (e.clientX - pointerStart.x) / rect.width;
    const dyFrac = (e.clientY - pointerStart.y) / rect.height;
    xPct = clampFrac(posStart.x + dxFrac);
    yPct = clampFrac(posStart.y + dyFrac);
  }

  function onPointerUp() {
    if (!dragging) return;
    dragging = false;
    pointerStart = null;
    posStart = null;
    window.removeEventListener('pointermove', onPointerMove);
    window.removeEventListener('pointerup',   onPointerUp);
    persist();
  }

  // Translate -50% so the badge centers on the (x,y) anchor point.
  $: leftStyle = `left: ${(xPct * 100).toFixed(2)}%`;
  $: topStyle  = `top: ${(yPct * 100).toFixed(2)}%`;
</script>

<div bind:this={containerEl}
     class="absolute"
     style="{leftStyle}; {topStyle}; transform: translate(-50%, -50%); cursor: {editable ? (dragging ? 'grabbing' : 'grab') : 'default'};"
     on:pointerdown={onPointerDown}
     role={editable ? 'button' : undefined}
     tabindex={editable ? 0 : undefined}>
  <SensorBadge {label} {value} {setpoint}/>
  {#if editable && !dragging}
    <div class="absolute -top-2 -right-2 w-4 h-4 bg-white border border-gray-400 rounded-full
                flex items-center justify-center text-[10px] shadow opacity-50 pointer-events-none">
      ⋮⋮
    </div>
  {/if}
</div>

<style>
  /* The drag handle dots are decorative; pointer events go through
   * to the badge underneath. The whole wrapper IS the drag handle. */
</style>
