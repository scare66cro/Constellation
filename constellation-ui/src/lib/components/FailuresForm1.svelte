<script lang="ts">
  import { onMount } from "svelte";
  import Card from "$lib/ui/Card.svelte";
  import Select from "$lib/ui/Select.svelte";
  import Table from "$lib/ui/Table.svelte";
  import Row from "$lib/ui/Row.svelte";
  import Column from "$lib/ui/Column.svelte";
  import FailureMode from "$lib/components/FailureMode.svelte";
  import SaveButton from "$lib/components/SaveButton.svelte";
  import { navigationStore, failureOptionsStore } from "$lib/store";
  import { cloneDeep, isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
  import { failures1Composite, type Failures1View } from "$lib/business/protoStores";
  import { writeProto } from "$lib/business/protoWrite";
  import { TAG } from "$lib/business/protoTags";

  // Shared body of Failures Setup 1 (level2/failures1): per-equipment failure
  // mode + timer. Rendered on the classic page AND as a tab of the dashboard
  // "Alarms" modal. Prop contract mirrors the other forms.
  // docs/spatial-ui-page-migration.md
  export let wait = false;
  export let ready = false;
  export let embedded = false;
  export let theme: 'light' | 'dark' = 'light';
  export let canEdit: boolean | null = null;

  type Failures = Failures1View;
  $: edit = canEdit ?? ($navigationStore.level > 0);

  let noFan = true, noRefrig = true, noRefrigStage = true, noCavity = true,
      noLights = true, noClimacell = true, noHumid = true, noHeat = true,
      noAux = true, noBurner = true;
  let onionMode: boolean;
  let validation = {
    FanTimer: '', ClimacellTimer: '', BurnerTimer: '', RefridgeTimer: '',
    RefrStagesTimer: '', HumidifiersTimer: '', AuxTimer: '', HeatTimer: '',
    CavityHeatTimer: '', LightsTimer: '',
  };

  let failures1 = {} as Failures;
  let original: string[] = [];
  $: if (ready) {
    noFan = failures1.InputConfig[0] === '-1' && failures1.PwmConfig[2]?.split(':')[2] === '-1';
    noRefrig = failures1.PwmConfig[1]?.split(':')[2] === '-1' && failures1.OutputConfig[13] === '-1';
    noRefrigStage = failures1.InputConfig[13] === '-1' && failures1.InputConfig[21] === '-1';
    noCavity = failures1.InputConfig[5] === '-1';
    noLights = failures1.InputConfig[23] === '-1' && failures1.InputConfig[24] === '-1';
    noClimacell = failures1.InputConfig[3] === '-1';
    noHumid = failures1.InputConfig[7] === '-1';
    noHeat = failures1.InputConfig[4] === '-1';
    noAux = failures1.InputConfig.reduce((acc, curr, index) => (index >= 25 && index <= 32 && curr !== '-1') ? acc + 1 : acc, 0) === 0;
    noBurner = failures1.InputConfig[6] === '-1' && failures1.PwmConfig[3]?.split(':')[2] === '-1';
    onionMode = failures1.systemMode === '1';
  }

  let saveBtn: { save: () => Promise<void> } | undefined;
  export async function flush(): Promise<void> {
    if (ready && !isEqual(failures1.failures, original) && saveBtn) await saveBtn.save();
  }

  onMount(() => {
    const unsub = failures1Composite.subscribe((view) => {
      if (!view) return;
      if (!ready) {
        failures1 = cloneDeep(view);
        original = cloneDeep(view.failures);
        if (!embedded) $navigationStore.isDirty = () => !isEqual(failures1.failures, original);
        ready = true;
      } else {
        failures1 = { ...view, failures: failures1.failures };
      }
    });
    return () => unsub();
  });
</script>

<div class="pform pform--{theme}">
  <Card class="xl:w-11/12 text-size-xl md:mx-2 xl:mx-auto flex flex-col">
    {#if ready}
    <Table class="text-size-xl">
      {#if !noFan}
        <Row><Column class="border-r border-gray-400">{ $t('level2.failures1.fan') }</Column><Column colspan={4}><FailureMode modeOptions={[$failureOptionsStore.modeOptions[2]]} bind:mode={failures1.failures[0]} bind:timer={failures1.failures[1]} showMinutes validation={validation.FanTimer}/></Column></Row>
      {/if}
      {#if onionMode && !noBurner}
        <Row><Column class="border-r border-gray-400">{ $t('level2.failures1.burner') }</Column><Column colspan={4}><FailureMode modeOptions={$failureOptionsStore.modeOptions} bind:mode={failures1.failures[17]} bind:timer={failures1.failures[18]} showMinutes validation={validation.BurnerTimer}/></Column></Row>
      {/if}
      {#if !noClimacell && !onionMode}
        <Row><Column class="border-r border-gray-400">ClimaCell</Column><Column colspan={4}><FailureMode modeOptions={$failureOptionsStore.modeOptions} bind:mode={failures1.failures[2]} bind:timer={failures1.failures[3]} showMinutes validation={validation.ClimacellTimer}/></Column></Row>
      {/if}
      {#if !noRefrig}
        <Row><Column class="border-r border-gray-400">{ $t('level2.failures1.refrigeration-master') }</Column><Column colspan={4}><FailureMode modeOptions={$failureOptionsStore.modeOptions} bind:mode={failures1.failures[4]} bind:timer={failures1.failures[5]} showMinutes validation={validation.RefridgeTimer}/></Column></Row>
      {/if}
      {#if failures1.failures && failures1.failures[4] === '1'}
        <Row><Column></Column><Column colspan={4}>{ $t('level2.failures1.run-in') }: <Select class="w-128" size="xl" bind:value={failures1.failures[6]} options={$failureOptionsStore.refridgeRunOptions(failures1.boardType, failures1.controllerVersion)} {edit} /></Column></Row>
      {/if}
      {#if !noRefrigStage}
        <Row><Column class="border-r border-gray-400">{ $t('level2.failures1.refrigeration-stages') }</Column><Column colspan={4}><FailureMode modeOptions={$failureOptionsStore.modeOptions} bind:mode={failures1.failures[7]} bind:timer={failures1.failures[8]} showMinutes validation={validation.RefrStagesTimer}/></Column></Row>
      {/if}
      {#if !noHumid}
        <Row><Column class="border-r border-gray-400">{ $t('level2.failures1.humidifiers') }</Column><Column colspan={4}><FailureMode modeOptions={$failureOptionsStore.modeOptions} bind:mode={failures1.failures[9]} bind:timer={failures1.failures[10]} showMinutes validation={validation.HumidifiersTimer} /></Column></Row>
      {/if}
      {#if !noAux}
        <Row><Column class="border-r border-gray-400">{ $t('global.auxiliary') }</Column><Column colspan={4}><FailureMode modeOptions={$failureOptionsStore.modeOptions} bind:mode={failures1.failures[11]} bind:timer={failures1.failures[12]} showMinutes validation={validation.AuxTimer}/></Column></Row>
      {/if}
      {#if !noHeat}
        <Row><Column class="border-r border-gray-400">{ $t('level2.failures1.heat') }</Column><Column colspan={4}><FailureMode modeOptions={$failureOptionsStore.modeOptions} bind:mode={failures1.failures[13]} bind:timer={failures1.failures[14]} showMinutes validation={validation.HeatTimer} /></Column></Row>
      {/if}
      {#if !noCavity}
        <Row><Column class="border-r border-gray-400">{ $t('level2.failures1.cavity-heater') }</Column><Column colspan={4}><FailureMode modeOptions={$failureOptionsStore.modeOptions} bind:mode={failures1.failures[15]} bind:timer={failures1.failures[16]} showMinutes validation={validation.CavityHeatTimer}/></Column></Row>
      {/if}
      {#if !noLights}
        <Row><Column class="border-r border-gray-400">{ $t('level2.failures1.bay-lights-monitor') }</Column><Column colspan={4}><FailureMode modeOptions={$failureOptionsStore.modeLightOptions} bind:mode={failures1.failures[19]} bind:timer={failures1.failures[20]} validation={validation.LightsTimer}><Select class="w-72" size="xl" bind:value={failures1.failures[21]} options={$failureOptionsStore.LightsOptions} {edit} /></FailureMode></Column></Row>
      {/if}
    </Table>
    <SaveButton bind:this={saveBtn} {edit} bind:wait={wait} data={failures1.failures} bind:original={original} route="failures1" bind:validation={validation} autoSave
      onSave={async (d) => {
        await writeProto(TAG.FailureSettings, {
          fanMode: parseInt(d[0],10)||0, fanTimer: parseInt(d[1],10)||0,
          heatMode: parseInt(d[13],10)||0, heatTimer: parseInt(d[14],10)||0,
          refrigMode: parseInt(d[4],10)||0, refrigTimer: parseInt(d[5],10)||0,
          refrigFailMode: parseInt(d[6],10)||0,
          burnerMode: parseInt(d[17],10)||0, burnerTimer: parseInt(d[18],10)||0,
          humidTimer: parseInt(d[10],10)||0, climacellTimer: parseInt(d[3],10)||0,
          lightsMode: parseInt(d[19],10)||0, lightsTimer: parseInt(d[20],10)||0,
          lightsUnits: parseInt(d[21],10)||0, climacellMode: parseInt(d[2],10)||0,
          refrigStagesMode: parseInt(d[7],10)||0, refrigStagesTimer: parseInt(d[8],10)||0,
          humidMode: parseInt(d[9],10)||0,
          auxMode: parseInt(d[11],10)||0, auxTimer: parseInt(d[12],10)||0,
          cavityHeatMode: parseInt(d[15],10)||0, cavityHeatTimer: parseInt(d[16],10)||0,
        });
      }} />
    {/if}
  </Card>
</div>
