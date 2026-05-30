<script lang="ts">
	import GellertPage from "$lib/components/GellertPage.svelte";
	import OnOff from "$lib/components/OnOff.svelte";
	import Card from "$lib/ui/Card.svelte";
	import Column from "$lib/ui/Column.svelte";
	import Row from "$lib/ui/Row.svelte";
	import Table from "$lib/ui/Table.svelte";
	import { onMount } from "svelte";
  import PIDU from "$lib/components/PIDU.svelte";
	import Select from "$lib/ui/Select.svelte";
	import TextField from "$lib/ui/TextField.svelte";
	import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
	import { AdornmentType } from "$lib/business/adornmentType";
	import Button from "$lib/ui/Button.svelte";
	import { goto } from "$app/navigation";
	import { pidStore, frontMatterStore, navigationStore } from "$lib/store";
	import SaveButton from "$lib/components/SaveButton.svelte";
  import { isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
  import TritonScadaSection from "./TritonScadaSection.svelte";
  import RefrigerantPTChart from "./RefrigerantPTChart.svelte";
  import ScrollableArea from "$lib/components/ScrollableArea.svelte";
  import { refrigSettings } from "$lib/business/protoStores";
  import { writeProtoRaw, buildForceVarintBytes, buildForceFloatBytes, wrapAsLengthDelim } from "$lib/business/protoWrite";
  import { TAG } from "$lib/business/protoTags";

  // Phase 5.1: refrig page now hydrates from the RefrigSettings proto
  // broadcast (envelope tag 44 / settings field 5) directly. The legacy
  // /iot/refrigeration GET is no longer consumed by the UI; +page.ts only
  // loads the bridge-side Triton sidecar (orbit-only metadata, no proto
  // representation).
  export let data: { tritons?: { slot: number; connected: boolean; label: string }[] };

  let title = $t('level2.refrigeration.refrigeration-setup');
  let edit = true;
  /** Refrigerant code of the currently-selected Triton tab; piped through to
   *  the P-T chart so it tracks the operator's view. */
  let activeRefrigerantType: number | undefined = undefined;
  /** Live values forwarded from the active Triton to the P-T chart so the
   *  optimal-pressure widget shows the unit's actual operating context. */
  let activeAmbientF: number | undefined = undefined;
  let activeCutInP:  number | undefined = undefined;
  let activeCutOutP: number | undefined = undefined;
  let activeDischargeTargetP: number | undefined = undefined;

  let refrigerationOptions = [
    { text: $t('level2.refrigeration.normal'), value: '0' },
    { text: $t('level2.refrigeration.pump-down'), value: '1' },
  ];

  const refrigIndex = [0, 2, 4, 6, 8, 10, 19, 21];

  let validation: Record<string, string> = {
    Stage1On: '',
    Stage1Off: '',
    Stage2On: '',
    Stage2Off: '',
    Stage3On: '',
    Stage3Off: '',
    Stage4On: '',
    Stage4Off: '',
    Stage5On: '',
    Stage5Off: '',
    Stage6On: '',
    Stage6Off: '',
    Stage7On: '',
    Stage7Off: '',
    Stage8On: '',
    Stage8Off: '',
    PRefrValue: '',
    IRefrValue: '',
    DRefrValue: '',
    URefrValue: '',
    PurgeThreshold: '',
  };

  $: ready = false;
  $: wait = false;
  let refrigeration: string[] = new Array(23).fill('0');
  let originalRefrigeration: string[] = new Array(23).fill('0');
  $: refrigData = $frontMatterStore?.refrigData as string[];
  $: available = refrigIndex.map((index, i) => {
    if (refrigData?.[i] !== '-1') {
      return index;
    }
  })?.filter((i) => i !== undefined) as number[];
  $: remaining = available ? available.length % 2 : 0;

  // Compose the 23-slot string[] the existing template + saveRefrigeration
  // expect from the proto store. Layout matches legacy UI_SendRefrig() CSV
  // (see novaAdapter.ts refrigSettings handler):
  //   [0..11]  stages 1-6 on/off pairs
  //   [12..15] PIDU
  //   [16]     Refrig.Purge mode
  //   [17]     Co2.Purge.RefrigThresh
  //   [18]     Log.PID.Refrig (not in RefrigSettings; left 0 here)
  //   [19,20]  stage 7 on/off
  //   [21,22]  stage 8 on/off
  // Trim the f32→f64 precision noise that creeps in on the wire (e.g.
  // 1.1f → 1.100000023841858 in JS). Round to 4 decimals — matches the UI's
  // PIDU keypad precision — and re-stringify so "5.0" displays as "5".
  function fmtFloat(v: any): string {
    const n = Number(v);
    if (!Number.isFinite(n)) return '0';
    return String(parseFloat(n.toFixed(4)));
  }
  function rsToArray(rs: any): string[] {
    const stages = rs?.stages ?? [];
    const stage = (i: number) => ({
      on:  String(stages[i]?.on  ?? 0),
      off: String(stages[i]?.off ?? 0),
    });
    const out: string[] = [];
    for (let i = 0; i < 6; i++) {
      const s = stage(i);
      out.push(s.on, s.off);
    }
    out.push(
      fmtFloat(rs?.pGain),
      fmtFloat(rs?.iGain),
      fmtFloat(rs?.dGain),
      fmtFloat(rs?.uLimit),
      String(rs?.purge ?? 0),
      String(rs?.purgeThreshold ?? 0),
      '0',  // Log.PID.Refrig — not part of RefrigSettings
    );
    const s6 = stage(6); const s7 = stage(7);
    out.push(s6.on, s6.off, s7.on, s7.off);
    return out;
  }

  onMount(() => {
    const unsub = refrigSettings.subscribe((rs) => {
      if (!rs) return;
      const fresh = rsToArray(rs);
      if (!ready || isEqual(refrigeration, originalRefrigeration)) {
        refrigeration = fresh;
      }
      originalRefrigeration = fresh;
      ready = true;
      if (!$navigationStore.isDirty?.()) {
        $navigationStore = { ...$navigationStore, invalidate: true };
      }
    });
    $navigationStore.isDirty = () => !isEqual(refrigeration, originalRefrigeration);
    return () => unsub();
  });

  function gotoLogs() {
    $pidStore.returnPage = '/level2/refrigeration';
    goto('/level2/pid');
  }

  /**
   * Phase 5.1 proto-direct save (settings field 5 / TAG.RefrigSettings).
   *
   * Page state `refrigeration: string[23]`:
   *   [0..11]  = 6 stage on/off pairs (idx 0,2,4,6,8,10 = on; 1,3,... = off)
   *   [12..15] = PIDU (P, I, D, U) — floats
   *   [16]     = legacy RefrigerationPurge mode
   *   [17]     = PurgeThreshold
   *   [18]     = reserved (unused on the wire)
   *   [19..22] = stages 7+8 on/off (only present on 8-stage builds)
   *
   * Firmware apply_refrig (nova_dataexc.c, field 5):
   *   field 2  = repeated RefrigStage { 1=on, 2=off } (force-varint inside)
   *   field 3..6 = PIDU floats
   *   field 10 = Refrig.Purge      (force-varint, 0 = off mode)
   *   field 11 = Co2.Purge.RefrigThresh (force-varint)
   * apply_refrig clears stage_idx on every call — we MUST send all
   * configured stages in order, no gaps.
   */
  async function saveRefrigeration(arr: string[]): Promise<void> {
    if (!arr || arr.length === 0) return;
    const parts: Uint8Array[] = [];

    // ── Stages 1–6 (indices 0,2,4,6,8,10) ──
    for (let s = 0; s < 6; s++) {
      const on  = arr[s * 2];
      const off = arr[s * 2 + 1];
      if (on === undefined || on === '' || off === undefined || off === '') continue;
      const inner = buildForceVarintBytes({ 1: parseInt(on) || 0, 2: parseInt(off) || 0 });
      parts.push(wrapAsLengthDelim(2, inner));
    }
    // ── Stages 7–8 (indices 19,21) — same field 2, appended in order ──
    for (const baseIdx of [19, 21]) {
      const on  = arr[baseIdx];
      const off = arr[baseIdx + 1];
      if (on === undefined || on === '' || off === undefined || off === '') continue;
      const inner = buildForceVarintBytes({ 1: parseInt(on) || 0, 2: parseInt(off) || 0 });
      parts.push(wrapAsLengthDelim(2, inner));
    }

    // ── PIDU (fields 3,4,5,6 — floats, force-encoded since 0 valid) ──
    parts.push(buildForceFloatBytes({
      3: parseFloat(arr[12] ?? '0') || 0,
      4: parseFloat(arr[13] ?? '0') || 0,
      5: parseFloat(arr[14] ?? '0') || 0,
      6: parseFloat(arr[15] ?? '0') || 0,
    }));

    // ── Purge mode (field 10) + Purge threshold (field 11) ──
    parts.push(buildForceVarintBytes({
      10: parseInt(arr[16] ?? '0') || 0,
      11: parseInt(arr[17] ?? '0') || 0,
    }));

    // Concatenate all parts into one outer payload.
    let total = 0;
    for (const p of parts) total += p.length;
    const out = new Uint8Array(total);
    let off = 0;
    for (const p of parts) { out.set(p, off); off += p.length; }

    await writeProtoRaw(TAG.RefrigSettings, out);
  }
</script>

<GellertPage {wait} {title} {ready} level={2} name="refrigeration">
  <ScrollableArea>
  <!--
    Legacy stages / PID / purge-threshold UI.
    Renders only when at least one refrigeration stage is configured.  When no
    stages are configured AND a Triton orbit is connected, the page collapses
    to just the SCADA section below.  When neither stages nor a Triton are
    present we still render the (empty) legacy form so the user has an entry
    point to configure stages.
  -->
  {#if available.length > 0 || (data.tritons?.length ?? 0) === 0}
  <Card class="mx-2 flex flex-col">
    <div class="flex flex-row text-size-xl">
      <Table class="mb-2 mr-1">
        {#each available.slice(0, Math.floor(available.length / 2) + remaining) as index, i}
          <Row>
            <Column class="w-3/12 border-r border-gray-400">{ $t('level2.refrigeration.stage') } {i + 1}</Column>
            <Column class="w-9/12">
              <OnOff bind:on={refrigeration[index]} bind:off={refrigeration[index + 1]} validationOn={validation[`Stage${i + 1}On`]} validationOff={validation[`Stage${i + 1}Off`]} />
            </Column>
          </Row>
        {/each}
      </Table>
      <Table class="mb-2 ml-1">
        {#each available.slice(Math.floor(available.length / 2) + remaining) as index, i}
          <Row>
            <Column class="w-3/12 border-r border-gray-400">{ $t('level2.refrigeration.stage') } {i + 1 + Math.floor(available.length / 2) + remaining}</Column>
            <Column class="w-9/12">
              <OnOff bind:on={refrigeration[index]} bind:off={refrigeration[index + 1]} validationOn={validation[`Stage${i + 1}On`]} validationOff={validation[`Stage${i + 1}Off`]}/>
            </Column>
          </Row>
        {/each}
      </Table>
    </div>
    <Table class="text-size-xl">
      <Row>
        <Column class="w-1/3 border-r border-gray-400">{ $t('global.pidu-values') }</Column>
        <Column class="w-2/3">
          <PIDU bind:p={refrigeration[12]} bind:i={refrigeration[13]} bind:d={refrigeration[14]} bind:u={refrigeration[15]} pvalid={validation.PRefrValue} ivalid={validation.IRefrValue} dvalid={validation.DRefrValue} uvalid={validation.URefrValue} />
        </Column>
      </Row>
      <Row>
        <Column colspan={2}>
          <p class="text-center mx-4 text-size-large">
            { $t('global.pid-controller-output-logging-options') }:
            <Button size="lg" on:click={gotoLogs} class="ml-2">{ $t('global.logs') }</Button>
          </p>
        </Column>
      </Row>
    </Table>
    <SaveButton {edit} bind:wait={wait} data={refrigeration} bind:original={originalRefrigeration} bind:validation={validation} autoSave onSave={saveRefrigeration} />
  </Card>
  {/if}

  <TritonScadaSection tritons={data.tritons ?? []}
                      bind:activeRefrigerantType={activeRefrigerantType}
                      bind:activeAmbientF={activeAmbientF}
                      bind:activeCutInP={activeCutInP}
                      bind:activeCutOutP={activeCutOutP}
                      bind:activeDischargeTargetP={activeDischargeTargetP} />
  <RefrigerantPTChart selected={activeRefrigerantType}
                      ambientF={activeAmbientF}
                      cutInP={activeCutInP}
                      cutOutP={activeCutOutP}
                      dischargeTargetP={activeDischargeTargetP} />
  </ScrollableArea>
</GellertPage>


