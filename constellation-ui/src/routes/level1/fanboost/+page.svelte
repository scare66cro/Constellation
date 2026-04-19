<script lang="ts">
	import { onMount } from "svelte";
  import GellertPage from "$lib/components/GellertPage.svelte";
	import Card from "$lib/ui/Card.svelte";
	import TextField from "$lib/ui/TextField.svelte";
  import Select from "$lib/ui/Select.svelte";
	import Table from "$lib/ui/Table.svelte";
	import Row from "$lib/ui/Row.svelte";
	import Column from "$lib/ui/Column.svelte";
	import { navigationStore } from "$lib/store";
  import { getHttpUrl } from "$lib/business/util";
	import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
	import { AdornmentType } from "$lib/business/adornmentType";
	import SaveButton from "$lib/components/SaveButton.svelte";
	import { cloneDeep, isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
	import type { ArrayResponse } from "$lib/business/util";

  export let data: ArrayResponse;

  let title = $t('level1.fanboost.fan-boost-control');

  let boostMode = [
    { text: $t('global.none-default'), value: '0' },
    { text: $t('level1.fanboost.temperature-based'), value: '1' },
    { text: $t('level1.fanboost.runtime-based'), value: '2' },
  ];

  let validation = {
    'speed': '',
    'temp': '',
    'hours': '',
    'time': ''
  };

  $: ready = false;
  $: wait = false;
  $: level = $navigationStore.level;
  $: edit = $navigationStore.level > 0;
  let fanboost = [] as string[];

  onMount(async () => {
    try {
      $navigationStore.data = getHttpUrl('/iot/fanboost');
      $navigationStore.isDirty = () => !isEqual(fanboost, data.array);
      fanboost = Array.isArray(data.array) ? cloneDeep(data.array) : [];
    } catch (e) {
      console.error(e);
    }
		ready = true;
  });
</script>

<GellertPage {wait} {ready} {title} {level} name="fanboost">
  <Card class="mx-auto flex flex-col w-3/4 mt-2">
    {#if fanboost.length > 4}
    <Table class="mb-3">
      <Row>
        <Column class="items-center text-size-xl">{ $t('level1.fanboost.fan-boost-control-mode') }:
          <Select bind:value={fanboost[0]} class="w-96 3xl:w-144" size="xl" options={boostMode} {edit} />
        </Column>
      </Row>
      {#if fanboost[0] !== '0'}
        <Row>
          <Column class="items-center text-size-xl">{ $t('level1.fanboost.the-fan-speed-will-be-increased-to') }
            <TextField class="w-28 3xl:w-36" size="xl" bind:value={fanboost[1]} {edit} keyboardType={KeyboardTypes.Float} adornmentType={AdornmentType.Percent} validation={validation.speed} />
            { $t('level1.fanboost.if') }
            {#if fanboost[0] === '1'}
              { $t('level1.fanboost.the-outside-temperature-is-below')} <TextField class="w-28 3xl:w-36" size="xl" bind:value={fanboost[4]} {edit} keyboardType={KeyboardTypes.Float} adornmentType={AdornmentType.Temperature} validation={validation.temp}/>
              { $t('level1.fanboost.and-it') } { $t('level1.fanboost.has-been') }
              <TextField class="w-28 3xl:w-36" size="xl" bind:value={fanboost[2]} {edit} keyboardType={KeyboardTypes.Numeric} validation={validation.hours}/>
              { $t('level1.fanboost.hours-since-the-last-fan-boost-period') }
            {:else if fanboost[0] === '2'}
              { $t('level1.fanboost.the-continuous-fan-runtime-exceeds') } <TextField class="w-28 3xl:w-36" size="xl" bind:value={fanboost[2]} {edit} keyboardType={KeyboardTypes.Numeric} validation={validation.hours} /> { $t('global.hours') }.
            {/if}
            <br />
            { $t('level1.fanboost.the-fan-boost-period-will-last-for') }
            <TextField class="w-28 3xl:w-36" size="xl" bind:value={fanboost[3]} {edit} keyboardType={KeyboardTypes.Numeric} validation={validation.time}/>
            { $t('global.minutes') }.
          </Column>
        </Row>
      {/if}
    </Table>
    <SaveButton {edit} bind:wait={wait} data={fanboost} bind:original={data.array} route="fanboost" bind:validation={validation} autoSave/>
    {/if}
  </Card>
</GellertPage>


