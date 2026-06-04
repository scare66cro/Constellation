<script lang="ts">
  // Refrigeration topology card. Constellation supports three refrig
  // control paths (per the 0.A.227 widened gate):
  //
  //   1. AS2-style STAGES — any of EQ_REFRIG_STAGE1..8 mapped.
  //      Render small "stage chip" grid showing which stages are
  //      currently energized.
  //   2. PWM 0-100% — an orbit AO slot assigned to AO_EQUIP_REFRIG.
  //      Render the AnimatedRefrigCoil + big % readout.
  //   3. TRITON — orbit slot with role = TRITON (Modbus TCP demand).
  //      Render a TRITON-branded badge with the % and a note that
  //      the orbit owns staging.
  //
  // Auto-detected from equipmentComposite + ioConfig hints; if more
  // than one topology is present (rare — e.g. stage + AO during
  // transitional install), Stages takes precedence as the most
  // operationally-relevant view.
  import AnimatedRefrigCoil from "./AnimatedRefrigCoil.svelte";
  import type { Equipment } from "$lib/business/equipmentStatus";

  export let equipment: Equipment | null = null;
  export let refrigPct: number = 0;
  export let refrigModeRaw: number | undefined = undefined;
  export let tritonConfigured: boolean = false;

  // Walk equipment stages to detect which are energized + count.
  // EQ.REFRIG_STAGE1..8 = 13..20 (per equipmentEnum.ts)
  $: stages = (() => {
    if (!equipment) return [];
    const out: { idx: number; on: boolean; alarm: number }[] = [];
    for (let i = 0; i < 8; i++) {
      const st = equipment.eqByIdx(13 + i);
      if (st) out.push({ idx: i + 1, on: !!st.outputOn, alarm: st.alarm ?? 0 });
    }
    return out;
  })();
  $: anyStageMapped = stages.length > 0;
  $: stagesOn = stages.filter(s => s.on).length;

  $: topology = pickTopology(anyStageMapped, refrigPct, tritonConfigured);
  function pickTopology(stagesPresent: boolean, pct: number, triton: boolean): 'stages' | 'pwm' | 'triton' | 'none' {
    if (stagesPresent) return 'stages';
    if (triton)        return 'triton';
    if (pct > 0)       return 'pwm';
    return 'pwm';      // default visualization when nothing's running
  }

  $: refrigModeLabel = ((m: number | undefined) =>
    m === 0 ? 'Economizer' :
    m === 1 ? 'Refrig-only' :
    m === 2 ? 'Enthalpy' : '—')(refrigModeRaw);
</script>

<div class="border-2 rounded-lg p-3 {refrigPct > 5 ? 'bg-sky-50 border-sky-500' : 'bg-white border-gray-300'}">
  <div class="flex justify-between items-baseline mb-1">
    <div class="font-bold text-sm">Refrigeration</div>
    <div class="text-[10px] text-gray-500 uppercase tracking-wider">
      {topology}
    </div>
  </div>

  {#if topology === 'stages'}
    <!-- ── STAGES ────────────────────────────────────────────────── -->
    <div class="grid grid-cols-4 gap-1 mb-2">
      {#each stages as st (st.idx)}
        <div
          class="text-center text-xs font-bold rounded py-1 border
                 {st.on ? 'bg-sky-500 text-white border-sky-700' : 'bg-gray-100 text-gray-400 border-gray-300'}
                 {st.alarm === 2 ? 'ring-2 ring-red-500' : ''}"
          title="Stage {st.idx} · {st.on ? 'ON' : 'OFF'}{st.alarm === 2 ? ' · DIAG' : ''}"
        >
          {st.idx}
        </div>
      {/each}
    </div>
    <div class="text-xs text-gray-600">
      <strong>{stagesOn}</strong> of {stages.length} stages on
    </div>
  {:else if topology === 'triton'}
    <!-- ── TRITON ────────────────────────────────────────────────── -->
    <div class="flex items-center gap-2">
      <div class="bg-purple-600 text-white text-xs font-bold px-2 py-1 rounded">TRITON</div>
      <div class="text-3xl font-bold {refrigPct > 5 ? 'text-purple-700' : 'text-gray-400'}">{refrigPct}%</div>
    </div>
    <div class="text-xs text-gray-500 mt-1">Modbus TCP demand · orbit owns staging</div>
  {:else}
    <!-- ── PWM / no stages ───────────────────────────────────────── -->
    <div class="flex items-center gap-2">
      <AnimatedRefrigCoil pct={refrigPct} active={refrigPct > 5} size={110}/>
      <div>
        <div class="text-3xl font-bold {refrigPct > 5 ? 'text-sky-700' : 'text-gray-400'}">{refrigPct}%</div>
        <div class="text-xs text-gray-500">demand</div>
      </div>
    </div>
  {/if}

  <div class="text-[10px] text-gray-500 mt-1 border-t pt-1">
    Mode: <strong>{refrigModeLabel}</strong>
  </div>
</div>
