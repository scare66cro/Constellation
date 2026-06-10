<script lang="ts">
  import { onMount } from "svelte";
  import Card from "$lib/ui/Card.svelte";
  import TextField from "$lib/ui/TextField.svelte";
  import Select from "$lib/ui/Select.svelte";
  import Table from "$lib/ui/Table.svelte";
  import Row from "$lib/ui/Row.svelte";
  import Column from "$lib/ui/Column.svelte";
  import { navigationStore } from "$lib/store";
  import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
  import { AdornmentType } from "$lib/business/adornmentType";
  import SaveButton from "$lib/components/SaveButton.svelte";
  import { isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
  import { fanBoostSettings } from "$lib/business/protoStores";
  import { useDraft, numField } from "$lib/business/useDraft";
  import { writeProtoRaw, buildForceVarintBytes, buildForceFloatBytes } from "$lib/business/protoWrite";
  import { TAG } from "$lib/business/protoTags";

  // Shared body of the Fan Boost Control page (level1/fanboost): boost mode +
  // (when enabled) target speed / outside-temp / interval / duration. Rendered
  // in two presentations from ONE source of truth — the classic touchscreen
  // page and the "Fan Boost" tab of the dashboard fan modal.
  // Prop contract mirrors FanSpeedForm. docs/spatial-ui-page-migration.md
  export let wait = false;
  export let ready = false;
  export let embedded = false;
  export let theme: 'light' | 'dark' = 'light';
  export let canEdit: boolean | null = null;

  const fb = useDraft(fanBoostSettings, TAG.FanBoostSettings);
  const { draft, hydrated, live } = fb;

  const speedStr    = numField(draft, 'speed',    'float');
  const tempStr     = numField(draft, 'temp',     'float');
  const intervalStr = numField(draft, 'interval', 'int');
  const durationStr = numField(draft, 'duration', 'int');

  $: boostMode = [
    { text: $t('global.none-default'),                 value: 0 },
    { text: $t('level1.fanboost.temperature-based'),   value: 1 },
    { text: $t('level1.fanboost.runtime-based'),       value: 2 },
  ];

  let validation = { 'speed': '', 'temp': '', 'hours': '', 'time': '' };

  $: edit = canEdit ?? ($navigationStore.level > 0);

  /**
   * Phase 5.1 proto-direct save (settings field 3 / TAG.FanBoostSettings).
   * Field map (apply_fan_boost in nova_dataexc.c):
   *   1=mode (varint), 2=speed (FLOAT — schema says uint32), 3=interval,
   *   4=duration, 5=temp (FLOAT — schema says uint32).
   * See firmware-bridge-protocol.md for the wire-vs-schema discrepancy.
   */
  async function save(): Promise<void> {
    const v = buildForceVarintBytes({
      1: $draft.mode,      // 0 valid (Off)
      3: $draft.interval,
      4: $draft.duration,
    });
    const f = buildForceFloatBytes({
      2: $draft.speed,
      5: $draft.temp,
    });
    const out = new Uint8Array(v.length + f.length);
    out.set(v, 0);
    out.set(f, v.length);
    await writeProtoRaw(TAG.FanBoostSettings, out);
  }

  let saveBtn: { save: () => Promise<void> } | undefined;
  export async function flush(): Promise<void> {
    if (!isEqual($draft, $live) && saveBtn) await saveBtn.save();
  }

  onMount(() => {
    if (!embedded) $navigationStore.isDirty = () => !isEqual($draft, $live);
    ready = true;
  });
</script>

<div class="pform pform--{theme}">
  <Card class="mx-auto flex flex-col w-3/4 mt-2">
    {#if $hydrated}
    <Table class="mb-3">
      <Row>
        <Column class="items-center text-size-xl">{ $t('level1.fanboost.fan-boost-control-mode') }:
          <Select bind:value={$draft.mode} class="w-96 3xl:w-144" size="xl" options={boostMode} {edit} />
        </Column>
      </Row>
      {#if $draft.mode !== 0}
        <Row>
          <Column class="items-center text-size-xl">{ $t('level1.fanboost.the-fan-speed-will-be-increased-to') }
            <TextField class="w-28 3xl:w-36" size="xl" bind:value={$speedStr} {edit} keyboardType={KeyboardTypes.Float} adornmentType={AdornmentType.Percent} validation={validation.speed} />
            { $t('level1.fanboost.if') }
            {#if $draft.mode === 1}
              { $t('level1.fanboost.the-outside-temperature-is-below')} <TextField class="w-28 3xl:w-36" size="xl" bind:value={$tempStr} {edit} keyboardType={KeyboardTypes.Float} adornmentType={AdornmentType.Temperature} validation={validation.temp}/>
              { $t('level1.fanboost.and-it') } { $t('level1.fanboost.has-been') }
              <TextField class="w-28 3xl:w-36" size="xl" bind:value={$intervalStr} {edit} keyboardType={KeyboardTypes.Numeric} validation={validation.hours}/>
              { $t('level1.fanboost.hours-since-the-last-fan-boost-period') }
            {:else if $draft.mode === 2}
              { $t('level1.fanboost.the-continuous-fan-runtime-exceeds') } <TextField class="w-28 3xl:w-36" size="xl" bind:value={$intervalStr} {edit} keyboardType={KeyboardTypes.Numeric} validation={validation.hours} /> { $t('global.hours') }.
            {/if}
            <br />
            { $t('level1.fanboost.the-fan-boost-period-will-last-for') }
            <TextField class="w-28 3xl:w-36" size="xl" bind:value={$durationStr} {edit} keyboardType={KeyboardTypes.Numeric} validation={validation.time}/>
            { $t('global.minutes') }.
          </Column>
        </Row>
      {/if}
    </Table>
    <SaveButton bind:this={saveBtn} {edit} bind:wait={wait} data={$draft} original={$live} bind:validation={validation} autoSave onSave={save}/>
    {/if}
  </Card>
</div>
