<script lang="ts">
  import { onMount } from "svelte";
  import Card from "$lib/ui/Card.svelte";
  import Table from "$lib/ui/Table.svelte";
  import Row from "$lib/ui/Row.svelte";
  import Column from "$lib/ui/Column.svelte";
  import FailureMode from "$lib/components/FailureMode.svelte";
  import TextField from "$lib/ui/TextField.svelte";
  import { AdornmentType } from "$lib/business/adornmentType";
  import SaveButton from "$lib/components/SaveButton.svelte";
  import { navigationStore, failureOptionsStore } from "$lib/store";
  import { isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
  import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
  import { failureSettings2 } from "$lib/business/protoStores";
  import { writeProto } from "$lib/business/protoWrite";
  import { TAG } from "$lib/business/protoTags";

  // Shared body of Failures Setup 2 (level2/failures2): sensor/CO2/humidity
  // failure modes. Classic page AND a tab of the dashboard "Alarms" modal.
  // docs/spatial-ui-page-migration.md
  export let wait = false;
  export let ready = false;
  export let embedded = false;
  export let theme: 'light' | 'dark' = 'light';
  export let canEdit: boolean | null = null;

  $: edit = canEdit ?? ($navigationStore.level > 0);
  let validation = {
    OutAirTimer: '', OutHumidTimer: '', HighCo2Timer: '', Co2Setpt: '',
    LowHumidTimer: '', LowHumidSet: '', PlenSenTimer: '', PlenSenDiff: '',
  };

  function fs2ToArray(fs2: any): string[] {
    const v = fs2 ?? {};
    return [
      String(v.outAirMode ?? 0), String(v.outAirTimer ?? 0),
      String(v.outHumidMode ?? 0), String(v.outHumidTimer ?? 0),
      String(v.highCo2Mode ?? 0), String(v.highCo2Timer ?? 0), String(v.co2Setpt ?? 0),
      String(v.lowHumidMode ?? 0), String(v.lowHumidTimer ?? 0), String(v.lowHumidSet ?? 0),
      String(v.plenSenMode ?? 0), String(v.plenSenTimer ?? 0), String(v.plenSenDiff ?? 0),
    ];
  }

  let failures2: string[] = new Array(13).fill('0');
  let originalFailures2: string[] = new Array(13).fill('0');

  let saveBtn: { save: () => Promise<void> } | undefined;
  export async function flush(): Promise<void> {
    if (!isEqual(failures2, originalFailures2) && saveBtn) await saveBtn.save();
  }

  onMount(() => {
    const unsub = failureSettings2.subscribe((fs2) => {
      if (!fs2) return;
      const fresh = fs2ToArray(fs2);
      if (!ready || isEqual(failures2, originalFailures2)) failures2 = fresh;
      originalFailures2 = fresh;
      ready = true;
    });
    if (!embedded) $navigationStore.isDirty = () => !isEqual(failures2, originalFailures2);
    return () => unsub();
  });
</script>

<div class="pform pform--{theme}">
  <Card class="xl:container-wide text-size-xl md:mx-2 xl:mx-auto flex flex-col">
    {#if ready}
    <Table class="text-size-xl">
      <Row>
        <Column class="border-r border-gray-400 w-1/3 text-size-large">{ $t('level2.failures2.outside-temperature-sensor') }</Column>
        <Column class="w-2/3"><FailureMode modeOptions={[$failureOptionsStore.modeOptions[1], $failureOptionsStore.modeOptions[2]]} bind:mode={failures2[0]} bind:timer={failures2[1]} showMinutes validation={validation.OutAirTimer} /></Column>
      </Row>
      <Row>
        <Column class="border-r border-gray-400 text-size-large">{ $t('level2.failures2.outside-humidity-sensor') }</Column>
        <Column><FailureMode modeOptions={$failureOptionsStore.modeOptions} bind:mode={failures2[2]} bind:timer={failures2[3]} showMinutes validation={validation.OutHumidTimer}/></Column>
      </Row>
      <Row>
        <Column class="border-r border-gray-400 text-size-xl" rowspan={2}>{$t('level2.failures2.high')} CO<sub>2</sub> {$t('level2.failures2.level')}</Column>
        <Column><FailureMode modeOptions={$failureOptionsStore.modeOptions} bind:mode={failures2[4]} bind:timer={failures2[5]} showMinutes validation={validation.HighCo2Timer}/></Column>
      </Row>
      <Row>
        <Column class="text-size-xl">CO<sub>2</sub> { $t('level2.failures2.setpoint') } <TextField size="xl" class="w-48" bind:value={failures2[6]} type="number" {edit} keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.CO2} validation={validation.Co2Setpt}/></Column>
      </Row>
      <Row>
        <Column class="border-r border-gray-400 text-size-xl" rowspan={2}>{ $t('level2.failures2.low-plenum-humidity') }</Column>
        <Column><FailureMode modeOptions={$failureOptionsStore.modeOptions} bind:mode={failures2[7]} bind:timer={failures2[8]} showMinutes validation={validation.LowHumidTimer} /></Column>
      </Row>
      <Row>
        <Column class="text-size-xl">{ $t('level2.failures2.low-humidity-setpoint') } <TextField size="xl" class="w-36" bind:value={failures2[9]} type="number" {edit} keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Percent} validation={validation.LowHumidSet}/></Column>
      </Row>
      <Row>
        <Column class="border-r border-gray-400 text-size-xl" rowspan={2}>{ $t('level2.failures2.plenum-sensor') }</Column>
        <Column><FailureMode modeOptions={[$failureOptionsStore.modeOptions[1], $failureOptionsStore.modeOptions[2]]} bind:mode={failures2[10]} bind:timer={failures2[11]} showMinutes validation={validation.PlenSenTimer}/></Column>
      </Row>
      <Row>
        <Column class="text-size-xl">{ $t('level2.failures2.difference-of') } <TextField size="xl" class="w-36" bind:value={failures2[12]} type="number" {edit} keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Temperature} validation={validation.PlenSenDiff}/> { $t('global.between')} { $t('level2.failures2.plenum-temperature-sensor') } #1 { $t('global.and')} { $t('level2.failures2.plenum-temperature-sensor') } #2 { $t('level2.failures2.triggers-alarm-failure') }.</Column>
      </Row>
    </Table>
    <SaveButton bind:this={saveBtn} {edit} bind:wait={wait} data={failures2} bind:original={originalFailures2} route="failures2" bind:validation={validation} autoSave
      onSave={async (d) => {
        await writeProto(TAG.FailureSettings2, {
          outAirMode: parseInt(d[0],10)||0, outAirTimer: parseInt(d[1],10)||0,
          outHumidMode: parseInt(d[2],10)||0, outHumidTimer: parseInt(d[3],10)||0,
          highCo2Mode: parseInt(d[4],10)||0, highCo2Timer: parseInt(d[5],10)||0,
          co2Setpt: parseInt(d[6],10)||0,
          lowHumidMode: parseInt(d[7],10)||0, lowHumidTimer: parseInt(d[8],10)||0, lowHumidSet: parseInt(d[9],10)||0,
          plenSenMode: parseInt(d[10],10)||0, plenSenTimer: parseInt(d[11],10)||0,
          plenSenDiff: parseFloat(d[12]) || 0,
        });
      }} />
    {/if}
  </Card>
</div>
