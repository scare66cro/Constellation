<script lang="ts">
	import { onMount } from "svelte";
  import GellertPage from "$lib/components/GellertPage.svelte";
	import Card from "$lib/ui/Card.svelte";
	import Table from "$lib/ui/Table.svelte";
	import Row from "$lib/ui/Row.svelte";
	import Column from "$lib/ui/Column.svelte";
	import FailureMode from "$lib/components/FailureMode.svelte";
	import TextField from "$lib/ui/TextField.svelte";
	import { AdornmentType } from "$lib/business/adornmentType";
	import SaveButton from "$lib/components/SaveButton.svelte";
  import { navigationStore, failureOptionsStore } from "$lib/store";
  import { getHttpUrl } from "$lib/business/util";
  import { cloneDeep, isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
	import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
	import type { ArrayResponse } from "$lib/business/util";
  import ScrollableArea from "$lib/components/ScrollableArea.svelte";

  export let data: ArrayResponse;

  let title = $t('level2.failures2.failures-setup-2');

  let edit = true;

  let validation = {
      OutAirTimer: '',
      OutHumidTimer: '',
      HighCo2Timer: '',
      Co2Setpt: '',
      LowHumidTimer: '',
      LowHumidSet: '',
      PlenSenTimer: '',
      PlenSenDiff: '',
  };

  $: ready = false;
  $: wait = false;
  $: failures2 = [] as string[];

  onMount(async () => {
		try {
      $navigationStore.data = getHttpUrl(`/iot/failures2`);
      $navigationStore.isDirty = () => !isEqual(failures2, data.array);
      failures2 = cloneDeep(data.array);
		} catch (error) {
      console.error(error);
		}
		ready = true;
  });
</script>

<GellertPage {wait} {ready} {title} level={2} name="failures2">
  <Card class="xl:container-wide text-size-xl md:mx-2 xl:mx-auto flex flex-col">
    <ScrollableArea>
    <Table class="text-size-xl">
      <Row>
        <Column class="border-r border-gray-400 w-1/3 text-size-large">{ $t('level2.failures2.outside-temperature-sensor') }</Column>
        <Column class="w-2/3">
          <FailureMode modeOptions={[$failureOptionsStore.modeOptions[1], $failureOptionsStore.modeOptions[2]]} bind:mode={failures2[0]} bind:timer={failures2[1]} showMinutes validation={validation.OutAirTimer} />
        </Column>
      </Row>
      <Row>
        <Column class="border-r border-gray-400 text-size-large">{ $t('level2.failures2.outside-humidity-sensor') }</Column>
        <Column>
          <FailureMode modeOptions={$failureOptionsStore.modeOptions} bind:mode={failures2[2]} bind:timer={failures2[3]} showMinutes validation={validation.OutHumidTimer}/>
        </Column>
      </Row>
      <Row>
        <Column class="border-r border-gray-400 text-size-xl" rowspan={2}>{$t('level2.failures2.high')} CO<sub>2</sub> {$t('level2.failures2.level')}</Column>
        <Column>
          <FailureMode modeOptions={$failureOptionsStore.modeOptions} bind:mode={failures2[4]} bind:timer={failures2[5]} showMinutes validation={validation.HighCo2Timer}/>
        </Column>
      </Row>
      <Row>
        <Column class="text-size-xl">
          CO<sub>2</sub> { $t('level2.failures2.setpoint') } <TextField size="xl" class="w-48" bind:value={failures2[6]} type="number" {edit} keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.CO2} validation={validation.Co2Setpt}/>
        </Column>
      </Row>
      <Row>
        <Column class="border-r border-gray-400 text-size-xl" rowspan={2}>{ $t('level2.failures2.low-plenum-humidity') }</Column>
        <Column>
          <FailureMode modeOptions={$failureOptionsStore.modeOptions} bind:mode={failures2[7]} bind:timer={failures2[8]} showMinutes validation={validation.LowHumidTimer} />
        </Column>
      </Row>
      <Row>
        <Column class="text-size-xl">
          { $t('level2.failures2.low-humidity-setpoint') } <TextField size="xl" class="w-36" bind:value={failures2[9]} type="number" {edit} keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Percent} validation={validation.LowHumidSet}/>
        </Column>
      </Row>
      <Row>
        <Column class="border-r border-gray-400 text-size-xl" rowspan={2}>{ $t('level2.failures2.plenum-sensor') }</Column>
        <Column>
          <FailureMode modeOptions={[$failureOptionsStore.modeOptions[1], $failureOptionsStore.modeOptions[2]]} bind:mode={failures2[10]} bind:timer={failures2[11]} showMinutes validation={validation.PlenSenTimer}/>
        </Column>
      </Row>
      <Row>
        <Column class="text-size-xl">
          { $t('level2.failures2.difference-of') } <TextField size="xl" class="w-36" bind:value={failures2[12]} type="number" {edit} keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Temperature} validation={validation.PlenSenDiff}/>
          { $t('global.between')}
          { $t('level2.failures2.plenum-temperature-sensor') } #1
          { $t('global.and')}
          { $t('level2.failures2.plenum-temperature-sensor') } #2
          { $t('level2.failures2.triggers-alarm-failure') }.
        </Column>
      </Row>
    </Table>
      <svelte:fragment slot="footer-center">
        <SaveButton {edit} bind:wait={wait} data={ failures2 } bind:original={data.array} route="failures2" bind:validation={validation} autoSave />
      </svelte:fragment>
    </ScrollableArea>
  </Card>
</GellertPage>


