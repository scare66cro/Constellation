<script lang="ts">
  import Card from "$lib/ui/Card.svelte";
  import Column from "$lib/ui/Column.svelte";
  import Row from "$lib/ui/Row.svelte";
  import Table from "$lib/ui/Table.svelte";
  import Select from "$lib/ui/Select.svelte";
  import PIDU from "$lib/components/PIDU.svelte";
  import TextField from "$lib/ui/TextField.svelte";
  import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
  import { AdornmentType } from "$lib/business/adornmentType";
  import SaveButton from "$lib/components/SaveButton.svelte";
  import { navigationStore } from "$lib/store";
  import { burnerSettings, climacellSettings } from "$lib/business/protoStores";
  import { useDraft, numField } from "$lib/business/useDraft";
  import {
    writeProto,
    writeProtoRaw,
    buildForceVarintBytes,
    buildForceFloatBytes,
  } from "$lib/business/protoWrite";
  import { TAG } from "$lib/business/protoTags";
  import { isEqual } from "lodash-es";
  import { onMount } from "svelte";
  import { t } from "svelte-i18n";

  // Shared body of the Burner Setup page (level2/burner): burner control mode +
  // (per mode) manual output, ignite/low thresholds, PIDU, and altitude.
  // The burner is an ONION-cure subsystem, so on the dashboard this rides as the
  // "Burner" tab of the heat modal, shown only in ONION mode (gated plan3d-side).
  // Edits TWO protos — BurnerSettings + ClimacellSettings.altitude/altUnits.
  // Prop contract mirrors FanSpeedForm (L2). docs/spatial-ui-page-migration.md
  export let wait = false;
  export let ready = false;
  export let embedded = false;
  export let theme: 'light' | 'dark' = 'light';
  export let canEdit: boolean | null = null;

  const burner = useDraft(burnerSettings,    TAG.BurnerSettings);
  const cc     = useDraft(climacellSettings, TAG.ClimacellSettings);
  const { draft: bDraft, live: bLive, hydrated: bHydrated } = burner;
  const { draft: cDraft, live: cLive, hydrated: cHydrated } = cc;

  const onStr     = numField(bDraft, 'on',       'int');
  const lowStr    = numField(bDraft, 'low',      'int');
  const manualStr = numField(bDraft, 'manual',   'int');
  const pStr      = numField(bDraft, 'pGain',    'float');
  const iStr      = numField(bDraft, 'iGain',    'float');
  const dStr      = numField(bDraft, 'dGain',    'float');
  const uStr      = numField(bDraft, 'uLimit',   'float');
  const altStr    = numField(cDraft, 'altitude', 'int');

  $: burnerOptions = [
    { text: $t('global.none'),                  value: 0 },
    { text: $t('global.manual'),                value: 1 },
    { text: $t('level2.burner.economy-cure'),   value: 2 },
    { text: $t('level2.burner.maximum-cure'),   value: 3 },
  ];
  $: altOptions = [
    { text: $t('global.feet'),   value: 0 },
    { text: $t('global.meters'), value: 1 },
  ];

  let validation = {
    burnerManual: '', burnerOn: '', burnerLow: '',
    PBurnerValue: '', IBurnerValue: '', DBurnerValue: '', UBurnerValue: '',
    Altitude: '',
  };

  // Program-level edit gate (modal Program toggle / global level), AND the
  // page's own rule: mode=0 (none/locked) makes the value inputs read-only —
  // the mode dropdown itself stays editable so the user can leave "none".
  $: canEditLvl = canEdit ?? ($navigationStore.level > 0);
  $: edit = canEditLvl && $bDraft.mode !== 0;
  $: hydrated = $bHydrated && $cHydrated;

  /**
   * Burner save: full BurnerSettings + partial ClimacellSettings (alt only).
   * Force-encode everything because mode=0/On=0/etc. are all valid values.
   */
  async function saveBurner(): Promise<void> {
    const v = buildForceVarintBytes({
      1:  $bDraft.on,
      2:  $bDraft.low,
      7:  $bDraft.mode,
      8:  $bDraft.manual,
    });
    const f = buildForceFloatBytes({
      3: $bDraft.pGain,
      4: $bDraft.iGain,
      5: $bDraft.dGain,
      6: $bDraft.uLimit,
    });
    const out = new Uint8Array(v.length + f.length);
    out.set(v, 0);
    out.set(f, v.length);
    await writeProtoRaw(TAG.BurnerSettings, out);
    // Altitude/units live on ClimacellSettings (fields 2 + 3); forceFieldRegistry
    // force-emits both so 0 ft / 0 = feet is honoured on the wire.
    await writeProto(TAG.ClimacellSettings, {
      altitude: $cDraft.altitude,
      altUnits: $cDraft.altUnits,
    });
    cc.revert();
  }

  let saveBtn: { save: () => Promise<void> } | undefined;
  function dirty(): boolean {
    return !isEqual($bDraft, $bLive) ||
      !!($cLive && ($cDraft.altitude !== $cLive.altitude || $cDraft.altUnits !== $cLive.altUnits));
  }
  export async function flush(): Promise<void> {
    if (dirty() && saveBtn) await saveBtn.save();
  }

  onMount(() => {
    if (!embedded) $navigationStore.isDirty = dirty;
    ready = true;
  });
</script>

<div class="pform pform--{theme}">
  <Card class="w-3/4 mx-auto flex flex-col">
    {#if hydrated}
    <Table>
      <Row>
        <Column class="border-r border-gray-400">{ $t('level2.burner.burner-control-mode') }</Column>
        <Column>
          <Select class="w-48" size="xl" bind:value={$bDraft.mode} options={burnerOptions} edit={canEditLvl}/>
        </Column>
      </Row>
      {#if $bDraft.mode === 1}
        <Row>
          <Column class="border-r border-gray-400">{ $t('global.burner-output') }</Column>
          <Column>
            <TextField class="w-24" bind:value={$manualStr} {edit} keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Percent} validation={validation.burnerManual}/>
          </Column>
        </Row>
      {:else}
        <Row>
          <Column class="border-r border-gray-400">{ $t('level2.burner.burner-will-ignite-when-output-reaches') }</Column>
          <Column>
            <TextField class="w-24" bind:value={$onStr} {edit} keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Percent} validation={validation.burnerOn}/>
          </Column>
        </Row>
        <Row>
          <Column class="border-r border-gray-400">{ $t('level2.burner.low-burner-level') }</Column>
          <Column>
            <TextField class="w-24" bind:value={$lowStr} {edit} keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Percent} validation={validation.burnerLow}/>
          </Column>
        </Row>
        <Row>
          <Column class="border-r border-gray-400">{ $t('global.pidu-values') }</Column>
          <Column>
            <PIDU bind:p={$pStr} bind:i={$iStr} bind:d={$dStr} bind:u={$uStr} {edit} pvalid={validation.PBurnerValue} ivalid={validation.IBurnerValue} dvalid={validation.DBurnerValue} uvalid={validation.UBurnerValue}/>
          </Column>
        </Row>
      {/if}
      <Row>
        <Column class="border-r border-gray-400">{ $t('global.altitude') }</Column>
        <Column>
          <TextField class="w-24" bind:value={$altStr} {edit} keyboardType={KeyboardTypes.Numeric} validation={validation.Altitude}/>
          <Select class="w-48" size="xl" bind:value={$cDraft.altUnits} options={altOptions} {edit} />
        </Column>
      </Row>
    </Table>
    <SaveButton bind:this={saveBtn} {edit} bind:wait={wait}
                data={{ b: $bDraft, a: $cDraft.altitude, u: $cDraft.altUnits }}
                original={{ b: $bLive, a: $cLive?.altitude, u: $cLive?.altUnits }}
                bind:validation={validation} autoSave onSave={saveBurner}/>
    {/if}
  </Card>
</div>
