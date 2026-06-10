<script lang="ts">
  import { onMount } from "svelte";
  import { goto } from "$app/navigation";
  import OnOff from "$lib/components/OnOff.svelte";
  import Card from "$lib/ui/Card.svelte";
  import Column from "$lib/ui/Column.svelte";
  import Row from "$lib/ui/Row.svelte";
  import Table from "$lib/ui/Table.svelte";
  import PIDU from "$lib/components/PIDU.svelte";
  import Button from "$lib/ui/Button.svelte";
  import Select from "$lib/ui/Select.svelte";
  import TextField from "$lib/ui/TextField.svelte";
  import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
  import { AdornmentType } from "$lib/business/adornmentType";
  import SaveButton from "$lib/components/SaveButton.svelte";
  import { pidStore, frontMatterStore, navigationStore } from "$lib/store";
  import { isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
  import { refrigSettings, miscSettings } from "$lib/business/protoStores";
  import { useDraft, numField } from "$lib/business/useDraft";
  import { writeProtoRaw, buildForceVarintBytes, buildForceFloatBytes, wrapAsLengthDelim } from "$lib/business/protoWrite";
  import { TAG } from "$lib/business/protoTags";

  // Shared body of the Refrigeration setup page (level2/refrigeration):
  // stages on/off + PIDU + purge, proto-direct save (TAG.RefrigSettings).
  // The TRITON SCADA mimic + P-T chart stay page-side (they need the route
  // `data.tritons` sidecar); the dashboard refrig-coil modal renders just
  // these settings. Prop contract mirrors PlenumSetpointsForm /
  // HumidifierControlForm. docs/spatial-ui-page-migration.md
  export let wait = false;
  export let ready = false;
  export let embedded = false;
  export let theme: 'light' | 'dark' = 'light';
  export let canEdit: boolean | null = null;

  const refrigIndex = [0, 2, 4, 6, 8, 10, 19, 21];

  let validation: Record<string, string> = {
    Stage1On: '', Stage1Off: '', Stage2On: '', Stage2Off: '',
    Stage3On: '', Stage3Off: '', Stage4On: '', Stage4Off: '',
    Stage5On: '', Stage5Off: '', Stage6On: '', Stage6Off: '',
    Stage7On: '', Stage7Off: '', Stage8On: '', Stage8Off: '',
    PRefrValue: '', IRefrValue: '', DRefrValue: '', URefrValue: '',
    PurgeThreshold: '',
  };

  let refrigeration: string[] = new Array(23).fill('0');
  let originalRefrigeration: string[] = new Array(23).fill('0');
  $: level = $navigationStore.level;
  $: edit = canEdit ?? (level > 0);
  $: compSize = edit ? 'lg' : 'xl';

  // ─── Refrigeration mode / enthalpy / defrost (relocated from
  //     level1/miscellaneous 2026-06-08). These live in MiscSettings (a
  //     DIFFERENT proto from RefrigSettings), so they hydrate + save through
  //     their own draft + SaveButton; flush() covers both. Moving them here
  //     raised them from L1 → L2 (this form's access level).
  const miscD = useDraft(miscSettings, TAG.MiscSettings);
  const { draft: miscDraft, live: miscLive, hydrated: miscHyd } = miscD;
  const enthalpyOffPctStr  = numField(miscDraft, 'enthalpyOffPct',  'int');
  const defrostIntervalStr = numField(miscDraft, 'defrostInterval', 'int');
  const defrostDurationStr = numField(miscDraft, 'defrostDuration', 'int');
  $: useEnthalpy = $miscDraft?.refrigMode === 2;
  $: refrigModeOptions = [
    { text: $t('level1.miscellaneous.economizer'),          value: 0 },
    { text: $t('level1.miscellaneous.refrigeration-only'),  value: 1 },
    { text: $t('level1.miscellaneous.enthalpy-cooling'),    value: 2 },
  ];
  let miscSaveBtn: { save: () => Promise<void> } | undefined;
  let miscValidation = { enthTarget: '', defrostInterval: '', defrostTime: '' };
  $: refrigData = $frontMatterStore?.refrigData as string[] | undefined;
  // Only surface stages once the refrig/IO config has actually loaded.
  // Before then refrigData is undefined and `undefined !== '-1'` would mark
  // ALL 8 stages "available" → a bogus full 8-stage form on first paint.
  $: stagesLoaded = Array.isArray(refrigData);
  $: available = stagesLoaded
    ? refrigIndex.filter((_, i) => refrigData![i] !== undefined && refrigData![i] !== '-1')
    : [];
  $: remaining = available ? available.length % 2 : 0;

  // SaveButton ref so the modal can flush on close-unless-cancel.
  let saveBtn: { save: () => Promise<void> } | undefined;
  export async function flush(): Promise<void> {
    const tasks: Promise<void>[] = [];
    if (!isEqual(refrigeration, originalRefrigeration) && saveBtn) tasks.push(saveBtn.save());
    if (!isEqual($miscDraft, $miscLive) && miscSaveBtn) tasks.push(miscSaveBtn.save());
    await Promise.all(tasks);
  }

  // Trim f32→f64 precision noise (1.1f → 1.100000023841858) to 4 decimals.
  function fmtFloat(v: any): string {
    const n = Number(v);
    if (!Number.isFinite(n)) return '0';
    return String(parseFloat(n.toFixed(4)));
  }
  // Compose the 23-slot string[] the template + saveRefrigeration expect
  // from the proto store (layout matches legacy UI_SendRefrig() CSV).
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
    // Page-only swipe-nav dirty guard; the dashboard modal manages its own.
    if (!embedded) {
      $navigationStore.isDirty = () => !isEqual(refrigeration, originalRefrigeration);
    }
    return () => unsub();
  });

  function gotoLogs() {
    $pidStore.returnPage = '/level2/refrigeration';
    goto('/level2/pid');
  }

  /**
   * Phase 5.1 proto-direct save (settings field 5 / TAG.RefrigSettings).
   * Page state `refrigeration: string[23]` — see rsToArray for layout.
   * apply_refrig clears stage_idx on every call — send all configured
   * stages in order, no gaps.
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

<div class="pform pform--{theme}">
  <Card class="mx-2 flex flex-col">
    {#if !stagesLoaded}
      <p class="text-center text-size-large p-2 opacity-60">{ $t('level2.refrigeration.stage') }…</p>
    {:else if available.length === 0}
      <p class="text-center text-size-large p-2 opacity-70">{ $t('level2.refrigeration.stage') } —</p>
    {/if}
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
      {#if !embedded}
      <Row>
        <Column colspan={2}>
          <p class="text-center mx-4 text-size-large">
            { $t('global.pid-controller-output-logging-options') }:
            <Button size="lg" on:click={gotoLogs} class="ml-2">{ $t('global.logs') }</Button>
          </p>
        </Column>
      </Row>
      {/if}
    </Table>
    <SaveButton bind:this={saveBtn} {edit} bind:wait={wait} data={refrigeration} bind:original={originalRefrigeration} bind:validation={validation} autoSave onSave={saveRefrigeration} />
  </Card>

  <!-- Refrigeration mode / enthalpy / defrost (MiscSettings; moved here from
       level1/miscellaneous). Separate proto → its own SaveButton. -->
  {#if $miscHyd}
    <Card class="mx-2 mt-2 flex flex-col">
      <Table class="mb-2 text-size-xl">
        <Row>
          <Column class="m-2">
            { $t('global.refrigeration-mode') }: <Select class="ml-2 w-128" size={compSize} bind:value={$miscDraft.refrigMode} options={refrigModeOptions} {edit}/>
          </Column>
        </Row>
        {#if useEnthalpy}
          <Row>
            <Column class="m-2">
              { $t('level1.miscellaneous.enthalpy-cooling-will-turn-off-if-refrigeration-is') }
              <TextField class="w-36" size={compSize} bind:value={$enthalpyOffPctStr} {edit} label="Enthalpy On" keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Percent} validation={miscValidation.enthTarget}/>
              { $t('level1.miscellaneous.or-greater') }
            </Column>
          </Row>
        {/if}
        <Row>
          <Column class="m-2">
            { $t('level1.miscellaneous.refrigeration-will-air-defrost-every') }
            <TextField class="w-36" size={compSize} bind:value={$defrostIntervalStr} {edit} label="Defrost Start" keyboardType={KeyboardTypes.Numeric} validation={miscValidation.defrostInterval}/>
            { $t('level1.miscellaneous.hours-for') }
            <TextField class="w-36" size={compSize} bind:value={$defrostDurationStr} {edit} label="Defrost Duration" keyboardType={KeyboardTypes.Numeric} validation={miscValidation.defrostTime}/>
            { $t('global.minutes') }.
          </Column>
        </Row>
      </Table>
      <SaveButton bind:this={miscSaveBtn} {edit} bind:wait={wait} data={$miscDraft} original={$miscLive} bind:validation={miscValidation} autoSave onSave={() => miscD.save()} />
    </Card>
  {/if}
</div>
