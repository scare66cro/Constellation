<script lang="ts">
	import { onMount } from "svelte";
  import GellertPage from "$lib/components/GellertPage.svelte";
	import Card from "$lib/ui/Card.svelte";
	import Table from "$lib/ui/Table.svelte";
  import Row from "$lib/ui/Row.svelte";
  import Column from "$lib/ui/Column.svelte";
	import TextField from "$lib/ui/TextField.svelte";
	import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
	import { AdornmentType } from "$lib/business/adornmentType";
	import SaveButton from "$lib/components/SaveButton.svelte";
	import { frontMatterStore, navigationStore } from "$lib/store";
  import { getHttpUrl } from "$lib/business/util";
  import { cloneDeep, isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
	import type { ArrayResponse } from "$lib/business/util";

  // Expect an object with a plensetupData property which is a string array
  export let data: ArrayResponse = { array: [] };

  let title = $t('level1.plentemp.plenum-setpoints-and-alarms');

  let validation = {
    'PlenumTempSet': '',
    'PlenumHumidSet': '',
    'CureTempLowLimit': '',
    'AlarmTempLow': '',
    'AlarmMinLow': '',
    'CureTimeHighLimit': '',
    'AlarmTempHigh': '',
    'AlarmMinHigh': ''
  };

  $: plensetup = [] as string[];
  $: ready = false;
  $: wait = false;
  $: level = $navigationStore.level;
  $: edit = level > 0;
  $: onionMode = ($frontMatterStore?.panel as string[])?.[8] === '1';

  onMount(async () => {
    try {
      $navigationStore.data = getHttpUrl('/iot/plensetup');
      $navigationStore.isDirty = () => !isEqual(plensetup, data.array);
      plensetup = cloneDeep(data.array);
    } catch (e) {
      console.error(e);
    }
		ready = true;
  });
</script>

<GellertPage {wait} {ready} {title} {level} name="plentemp">
  <Card class="mt-2 flex flex-col container-wide">
    {#if plensetup.length > 8}
    <Table class="mb-2">
      <Row>
        <Column class="text-size-xl border-r border-gray-400">
          { onionMode ? $t('level1.plentemp.plenum-temperature-setpoint-for-burner-cure') : $t('global.plenum-temperature-setpoint') }
        </Column>
        <Column class="text-size-xl">
          {#if onionMode}
            <TextField class="w-36 border-none rounded-none" size="xl" bind:value={plensetup[3]} {edit} label="Plenum Temperature Setpoint"  keyboardType={KeyboardTypes.Float} adornmentType={AdornmentType.Temperature}/>
          {:else}
            <TextField class="w-36 border-none rounded-none" size="xl" bind:value={plensetup[0]} {edit} label="Plenum Temperature Setpoint"  keyboardType={KeyboardTypes.Float} adornmentType={AdornmentType.Temperature} validation={validation.PlenumTempSet}/>
          {/if}
         </Column>
     </Row>
      <Row>
        <Column class="text-size-xl border-r border-gray-400">
          { onionMode ? $t('level1.plentemp.burner-output-threshold-for-maximum-cure') : $t('global.plenum-humidity-setpoint') }
        </Column>
        <Column class="text-size-xl">
          {#if onionMode}
            <TextField class="w-36 border-none rounded-none" size="xl" bind:value={plensetup[4]} {edit} label="Plenum Humidity Setpoint"  keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Humidity}/>
          {:else}
            <TextField class="w-36 border-none rounded-none" size="xl" bind:value={plensetup[1]} {edit} label="Plenum Humidity Setpoint"  keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Humidity} validation={validation.PlenumHumidSet}/>
          {/if}
        </Column>
      </Row>
    </Table>
    <Card class="w-full mx-auto my-2 flex flex-col bg-surface-100">
      {#if onionMode}
        <p class="text-center text-size-xl">
          { $t('level1.plentemp.system-will-alarm-if') } { $t('level1.plentemp.plenum-temperature-is-below') } <TextField class="w-36" size="xl" bind:value={plensetup[9]} {edit} label="Cure Temperature Low" keyboardType={KeyboardTypes.Float} adornmentType={AdornmentType.Temperature} validation={validation.CureTempLowLimit}/>
          { $t('level1.plentemp.for') } <TextField class="w-36" size="xl" bind:value={plensetup[6]} {edit} label="Alarm Temperature Low Duration" keyboardType={KeyboardTypes.Numeric} validation={validation.AlarmMinLow}/> { $t('level1.plentemp.minutes-or') } 
          { $t('level1.plentemp.plenum-temperature-is-above') } <TextField class="w-36" size="xl" bind:value={plensetup[10]} {edit} label="Cure Temperature High" keyboardType={KeyboardTypes.Float} adornmentType={AdornmentType.Temperature} validation={validation.CureTimeHighLimit}/>
          { $t('level1.plentemp.for') } <TextField class="w-36" size="xl" bind:value={plensetup[8]} {edit} label="Alarm Temperature High Duration" keyboardType={KeyboardTypes.Numeric} validation={validation.AlarmMinHigh}/> { $t('global.minutes') }.
        </p>
      {:else}
        <p class="text-center text-size-xl">
          { $t('level1.plentemp.system-will-alarm-if') } { $t('level1.plentemp.plenum-temperature-is')} <TextField class="w-36" size="xl" bind:value={plensetup[5]} {edit} label="Alarm Temperature Low" keyboardType={KeyboardTypes.Float} adornmentType={AdornmentType.Temperature} validation={validation.AlarmTempLow}/>
          { $t('level1.plentemp.below-plenum-setpoint-for') } <TextField class="w-36" size="xl" bind:value={plensetup[6]} {edit} label="Alarm Temperature Low Duration" keyboardType={KeyboardTypes.Numeric} validation={validation.AlarmMinLow}/> { $t('level1.plentemp.minutes-or') } 
          { $t('level1.plentemp.plenum-temperature-is') } <TextField class="w-36" size="xl" bind:value={plensetup[7]} {edit} label="Alarm Temperature High" keyboardType={KeyboardTypes.Float} adornmentType={AdornmentType.Temperature} validation={validation.AlarmTempHigh}/>
          { $t('level1.plentemp.above-plenum-setpoint-for') } <TextField class="w-36" size="xl" bind:value={plensetup[8]} {edit} label="Alarm Temperature High Duration" keyboardType={KeyboardTypes.Numeric} validation={validation.AlarmMinHigh}/> { $t('global.minutes') }.
        </p>
      {/if}
    </Card>
    <SaveButton {edit} bind:wait={wait} data={plensetup} bind:original={data.array} route="plensetup" bind:validation={validation} autoSave/>
    {/if}
  </Card>
</GellertPage>



