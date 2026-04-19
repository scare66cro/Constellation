<!--
  TritonScadaSection.svelte — Bottom section of the Level 2 Refrigeration page.

  PHASE 1 (this file): tab strip + read-only telemetry text per Triton.
  PHASE 2 (planned)  : replace the body of <section> with an SVG mimic.
  PHASE 3 (planned)  : add click hotspots that open Skeleton popups.
  PHASE 4 (planned)  : popups gain edit forms (Level-2 perms required).

  Polls /iot/triton/{slot} every 1 s for the active tab only.  Renders nothing
  if no Tritons are present, so the page still works on a stages-only setup.
-->
<script lang="ts">
  import { onMount, onDestroy } from "svelte";
  import Card from "$lib/ui/Card.svelte";
  import Button from "$lib/ui/Button.svelte";
  import { getHttpUrl } from "$lib/business/util";
  import { t } from "svelte-i18n";
  import TritonMimic from "./TritonMimic.svelte";
  import TritonHotspotPopup from "./TritonHotspotPopup.svelte";

  export let tritons: { slot: number; connected: boolean; label: string }[] = [];

  /** Two-way bound output: refrigerantType setpoint of the active Triton.
   *  Lets the parent page link external widgets (e.g. the P-T chart) to
   *  whatever fluid the operator is currently looking at. */
  export let activeRefrigerantType: number | undefined = undefined;
  /** Live ambient temperature (°F) of the active Triton — feeds the
   *  floating-discharge calc on the P-T chart.  Undefined when the sensor
   *  is invalid or no Triton is selected; the chart falls back to its own
   *  user-editable value. */
  export let activeAmbientF: number | undefined = undefined;
  /** Operator's compressor cut-in / cut-out suction setpoints (PSI).  The
   *  P-T chart highlights the band between them as the Triton's
   *  configured suction operating range. */
  export let activeCutInP:  number | undefined = undefined;
  export let activeCutOutP: number | undefined = undefined;
  /** Live discharge-pressure target the Triton is currently chasing (PSI).
   *  Driven by the simulator's floating-head logic (mirrors GRC
   *  `TargetDischarge`).  Undefined when no Triton is selected. */
  export let activeDischargeTargetP: number | undefined = undefined;

  // Working copy that we keep up-to-date by polling /iot/triton/list every
  // few seconds.  This way Tritons added to orbits *after* the page loaded
  // (e.g. user flipped a slot's role in the orbit-sim panel) appear without
  // requiring a page refresh.
  let liveTritons = tritons;
  let listTimer: ReturnType<typeof setInterval> | null = null;

  let activeSlot: number | null = tritons[0]?.slot ?? null;
  let state: any = null;
  let pollTimer: ReturnType<typeof setInterval> | null = null;
  let loading = false;
  let err = '';

  // Currently-open hotspot popup ('' = none).  Phase 3 popup is read-only;
  // Phase 4 will turn it into an edit form.
  let openHotspot: string = '';

  $: activeAlarmCount = state?.alarms ? state.alarms.filter((a: any) => a.active).length : 0;
  $: activeRefrigerantType = state?.setpoints?.refrigerantType;
  $: activeAmbientF = state?.sensors?.ambientT?.valid
                        ? state.sensors.ambientT.value
                        : undefined;
  $: activeCutInP   = state?.setpoints?.cutInP;
  $: activeCutOutP  = state?.setpoints?.cutOutP;
  $: activeDischargeTargetP = (typeof state?.condenserFans?.targetP === 'number')
                                ? state.condenserFans.targetP
                                : undefined;

  async function fetchList() {
    try {
      const resp = await fetch(getHttpUrl('/iot/triton/list'));
      if (!resp.ok) return;
      const data = await resp.json();
      if (Array.isArray(data?.tritons)) {
        liveTritons = data.tritons;
        // If the active slot disappeared (role downgraded) pick a new one.
        if (activeSlot !== null && !liveTritons.find(t => t.slot === activeSlot)) {
          activeSlot = liveTritons[0]?.slot ?? null;
          state = null;
        } else if (activeSlot === null && liveTritons.length > 0) {
          activeSlot = liveTritons[0].slot;
          fetchActive();
        }
      }
    } catch { /* ignore — next tick will retry */ }
  }

  async function fetchActive() {
    if (activeSlot === null) { state = null; return; }
    loading = true;
    err = '';
    try {
      const resp = await fetch(getHttpUrl(`/iot/triton/${activeSlot}`));
      const data = await resp.json();
      state = data?.present ? data : null;
      if (!data?.present) err = $t('level2.refrigeration.triton-offline') || 'Triton offline';
    } catch (e: any) {
      err = e?.message ?? 'fetch failed';
      state = null;
    }
    loading = false;
  }

  function selectTab(slot: number) {
    activeSlot = slot;
    state = null;
    openHotspot = '';
    fetchActive();
  }

  function fmtRuntime(s: number): string {
    const h = Math.floor(s / 3600);
    const m = Math.floor((s % 3600) / 60);
    return `${h}h ${m}m`;
  }

  // Open the read-only popup for the clicked hotspot.
  function onMimicSelect(e: CustomEvent<{ id: string }>) {
    openHotspot = e.detail.id;
  }

  onMount(() => {
    if (activeSlot !== null) fetchActive();
    pollTimer = setInterval(fetchActive, 1000);
    // Refresh the list every 5s so newly-added Tritons surface live.
    fetchList();
    listTimer = setInterval(fetchList, 5000);
  });
  onDestroy(() => {
    if (pollTimer) clearInterval(pollTimer);
    if (listTimer) clearInterval(listTimer);
  });
</script>

{#if liveTritons.length > 0}
  <Card class="mx-2 mt-4 flex flex-col">
    <!-- Tab strip across the top -->
    <div class="flex flex-row border-b border-gray-300 bg-gray-50">
      {#each liveTritons as tr}
        <button
          type="button"
          class="px-4 py-2 text-size-large border-r border-gray-300 transition-colors
                 {activeSlot === tr.slot
                   ? 'bg-white font-bold border-b-2 border-b-blue-600 -mb-px'
                   : 'text-gray-600 hover:bg-gray-100'}"
          on:click={() => selectTab(tr.slot)}
        >
          {tr.label}
          {#if !tr.connected}
            <span class="ml-1 text-xs text-red-500">●</span>
          {/if}
        </button>
      {/each}
    </div>

    <!-- Active Triton body — Phase 2 SVG mimic + header badges + alarm list -->
    <div class="p-4 min-h-[300px]">
      {#if loading && !state}
        <p class="text-gray-500">Loading…</p>
      {:else if !state}
        <p class="text-gray-500">{err || 'No data'}</p>
      {:else}
        <div class="space-y-3">
          <!-- Header strip: label, run state, mode, alarm count, runtime -->
          <div class="flex items-center flex-wrap gap-3 text-size-xl">
            <span class="font-bold">{state.label}</span>
            <span class="px-2 py-0.5 rounded text-sm
                         {state.compressor.on
                           ? 'bg-green-200 text-green-900'
                           : 'bg-gray-200 text-gray-700'}">
              Compressor {state.compressor.on ? 'ON' : 'OFF'}
            </span>
            <span class="px-2 py-0.5 rounded text-sm
                         {state.manualMode === 'auto'
                           ? 'bg-blue-100 text-blue-900'
                           : 'bg-yellow-200 text-yellow-900'}">
              {state.manualMode}
            </span>
            <span class="text-sm text-gray-500">
              Runtime: {fmtRuntime(state.compressor.runtimeSec)}
            </span>
            {#if state.alarms.length > 0}
              <button type="button"
                      class="px-2 py-0.5 rounded text-sm bg-red-200 text-red-900 hover:bg-red-300"
                      on:click={() => (openHotspot = 'alarms')}>
                {activeAlarmCount} alarm(s)
              </button>
            {/if}
            {#if state.safeties && (state.safeties.lockoutMask ?? 0) !== 0}
              <span class="px-2 py-0.5 rounded text-sm bg-red-300 text-red-900 font-bold">
                LOCKOUT
              </span>
            {/if}
            <button type="button"
                    class="px-2 py-0.5 rounded text-sm bg-amber-200 text-amber-900 hover:bg-amber-300 ml-auto"
                    on:click={() => (openHotspot = 'safety')}>
                ⚠ Safeties
            </button>
            <button type="button"
                    class="px-2 py-0.5 rounded text-sm bg-gray-200 text-gray-800 hover:bg-gray-300"
                    on:click={() => (openHotspot = 'io')}>
              ⚙ I/O Config
            </button>
          </div>

          <!-- SVG mimic — clickable hotspots dispatch 'select' events. -->
          <div class="border rounded bg-white">
            <TritonMimic {state} on:select={onMimicSelect} />
          </div>

          {#if state.alarms.length > 0}
            <div class="border border-red-300 bg-red-50 rounded p-2">
              <div class="font-bold text-red-900 mb-1">Alarms</div>
              <ul class="text-sm">
                {#each state.alarms as a}
                  <li class="flex justify-between">
                    <span>{a.label} {a.active ? '' : '(cleared)'}</span>
                    <span class="text-xs text-gray-500">
                      {a.acked ? 'acked' : 'unacked'}
                    </span>
                  </li>
                {/each}
              </ul>
            </div>
          {/if}

          <p class="text-xs text-gray-400 italic">
            Click any component on the schematic to inspect it (popups land in Phase 3).
          </p>
        </div>
      {/if}
    </div>
  </Card>

  <!-- Hotspot detail popup — Phase 3: read-only telemetry, Phase 4: edits. -->
  <TritonHotspotPopup hotspotId={openHotspot} {state} slot={activeSlot}
                      on:close={() => (openHotspot = '')}
                      on:saved={fetchActive} />
{/if}
