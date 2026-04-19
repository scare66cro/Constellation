<script lang="ts">
	import { AdornmentType } from "$lib/business/adornmentType";
  import GellertPage from "$lib/components/GellertPage.svelte";
	import PIDU from "$lib/components/PIDU.svelte";
	import Card from "$lib/ui/Card.svelte";
	import Column from "$lib/ui/Column.svelte";
	import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
	import Row from "$lib/ui/Row.svelte";
	import Table from "$lib/ui/Table.svelte";
	import TextField from "$lib/ui/TextField.svelte";
  import Select from "$lib/ui/Select.svelte";
	import { onMount } from "svelte";
	import SaveButton from "$lib/components/SaveButton.svelte";
  import { navigationStore } from "$lib/store";
  import { getHttpUrl } from "$lib/business/util";
  import { cloneDeep, isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
	import type { ArrayResponse } from "$lib/business/util";

  export let data: ArrayResponse;

  let edit = true;
  let title = $t('level2.climacell.climacell-setup');
  let altitudeOptions = [
    { text: $t('global.feet'), value: '0' },
    { text: $t('global.meters'), value: '1' },
  ];

  let validation = {
    'ClimacellEff': '',
    'Altitude': '',
    'PClimacellValue': '',
    'IClimacellValue': '',
    'DClimacellValue': '',
    'UClimacellValue': ''
  };

  $: ready = false;
  $: wait = false;
  $: climacell = [] as string[];

  onMount(async () => {
    try {
      $navigationStore.data = getHttpUrl('/iot/climacell');
      $navigationStore.isDirty = () => !isEqual(climacell, data.array);
      climacell = cloneDeep(data.array);
    } catch (error) {
      console.error(error);
    }
    ready = true;
  });
</script>

<GellertPage {wait} {title} level={2} {ready} name="climacell">
  <Card class="mx-2 flex flex-col">
    <Table class="mb-2 text-size-xl">
      <Row>
        <Column class="w-5/12 border-r border-gray-400">{ $t('level2.climacell.climacell-efficiency') }</Column>
        <Column class="w-7/12">
          <TextField class="w-36" size="xl" bind:value={climacell[0]} label="Efficiency" keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Percent} {edit} validation={validation.ClimacellEff}/>
        </Column>
      </Row>
      <Row>
        <Column class="w-5/12 border-r border-gray-400">{ $t('global.altitude') }</Column>
        <Column class="w-7/12">
          <p class="text-center">
            <TextField class="mr-2 w-24 xl:w-48" size="xl" bind:value={climacell[1]} label="Altitude" keyboardType={KeyboardTypes.Numeric} {edit} validation={validation.Altitude}/>
            <Select class="w-64 3xl:w-96" size="xl" bind:value={climacell[2]} options={altitudeOptions} {edit}/>
          </p>
        </Column>
      </Row>
      <Row>
        <Column class="w-5/12 border-r border-gray-400">{ $t('level2.climacell.climacell-humidifier-s') } {#if window.innerWidth <= 1280}<br/>{/if}{ $t('global.pidu-values') }</Column>
        <Column class="w-7/12">
          <PIDU bind:p={climacell[3]} bind:i={climacell[4]} bind:d={climacell[5]} bind:u={climacell[6]} pvalid={validation.PClimacellValue} ivalid={validation.IClimacellValue} dvalid={validation.DClimacellValue} uvalid={validation.UClimacellValue}/>
        </Column>
      </Row>
    </Table>
    <SaveButton {edit} bind:wait={wait} data={climacell} bind:original={data.array} route="climacell" bind:validation={validation} autoSave/>
  </Card>
</GellertPage>

