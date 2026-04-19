<script lang="ts">
	import GellertPage from "$lib/components/GellertPage.svelte";
	import Card from "$lib/ui/Card.svelte";
	import Column from "$lib/ui/Column.svelte";
	import Row from "$lib/ui/Row.svelte";
	import Table from "$lib/ui/Table.svelte";
  import Select from "$lib/ui/Select.svelte";
	import { onMount } from "svelte";
  import PIDU from "$lib/components/PIDU.svelte";
	import TextField from "$lib/ui/TextField.svelte";
	import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
	import { AdornmentType } from "$lib/business/adornmentType";
	import SaveButton from "$lib/components/SaveButton.svelte";
  import { navigationStore } from "$lib/store";
  import { getHttpUrl } from "$lib/business/util";
  import { cloneDeep, isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
	import type { ArrayResponse } from "$lib/business/util";

  export let data: ArrayResponse;

  let title = $t('level2.burner.burner-setup');

  let burnerOptions = [
    { text: $t('global.none'), value: '0' },
    { text: $t('global.manual'), value: '1' },
    { text: $t('level2.burner.economy-cure'), value: '2'},
    { text: $t('level2.burner.maximum-cure'), value: '3' },
  ];

  let altOptions = [
    { text: $t('global.feet'), value: '0' },
    { text: $t('global.meters'), value: '1' },
  ];

  let validation = {
    'burnerManual': '',
    'burnerOn': '',
    'burnerLow': '',
    'PBurnerValue': '',
    'IBurnerValue': '',
    'DBurnerValue': '',
    'UBurnerValue': '',
    'Altitude': ''
  };

  $: ready = false;
  $: wait = false;
  $: burner = [] as string[];
  $: edit = burner[6] !== '0';

  onMount(async () => {
    try {
      $navigationStore.data = getHttpUrl(`/iot/burner`);
      $navigationStore.isDirty = () => !isEqual(burner, data.array);
      burner = cloneDeep(data.array);
    } catch (error) {
      console.error(error);
    }
    ready = true;
  });
</script>

<GellertPage {wait} {title} {ready} level={2} name="burner">
  <Card class="w-3/4 mx-auto flex flex-col">
    <Table>
      <Row>
        <Column class="border-r border-gray-400">{ $t('level2.burner.burner-control-mode') }</Column>
        <Column>
          <Select class="w-48" size="xl" bind:value={burner[6]} options={burnerOptions} edit={true}/>
        </Column>
      </Row>
      {#if burner[6] === '1'}
        <Row>
          <Column class="border-r border-gray-400">{ $t('global.burner-output') }</Column>
          <Column>
            <TextField class="w-24" bind:value={burner[7]} {edit} keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Percent} validation={validation.burnerManual}/>
          </Column>
        </Row>
      {:else}
        <Row>
          <Column class="border-r border-gray-400">{ $t('level2.burner.burner-will-ignite-when-output-reaches') }</Column>
          <Column>
            <TextField class="w-24" bind:value={burner[0]} {edit} keybordType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Percent} validation={validation.burnerOn}/>
          </Column>
        </Row>
        <Row>
          <Column class="border-r border-gray-400">{ $t('level2.burner.low-burner-level') }</Column>
          <Column>
            <TextField class="w-24" bind:value={burner[1]} {edit} keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Percent} validation={validation.burnerLow}/>
          </Column>
        </Row>
        <Row>
          <Column class="border-r border-gray-400">{ $t('global.pidu-values') }</Column>
          <Column>
            <PIDU bind:p={burner[2]} bind:i={burner[3]} bind:d={burner[4]} bind:u={burner[5]} {edit} pvalid={validation.PBurnerValue} ivalid={validation.IBurnerValue} dvalid={validation.DBurnerValue} uvalid={validation.UBurnerValue}/>
          </Column>
        </Row>
      {/if}
      <Row>
        <Column class="border-r border-gray-400">{ $t('global.altitude') }</Column>
        <Column>
          <TextField class="w-24" bind:value={burner[8]} {edit} keyboardType={KeyboardTypes.Numeric} />
          <Select class="w-48" size="xl" bind:value={burner[9]} options={altOptions} {edit} />
        </Column>
      </Row>
    </Table>
    <SaveButton {edit} bind:wait={wait} data={burner} bind:original={data.array} route="burner" bind:validation={validation} autoSave/>
  </Card>
</GellertPage>


