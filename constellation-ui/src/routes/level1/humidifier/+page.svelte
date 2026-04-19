<script lang="ts">
	import { onMount } from "svelte";
  import GellertPage from "$lib/components/GellertPage.svelte";
	import Card from "$lib/ui/Card.svelte";
	import Table from "$lib/ui/Table.svelte";
	import Row from "$lib/ui/Row.svelte";
	import Column from "$lib/ui/Column.svelte";
	import TextField from "$lib/ui/TextField.svelte";
  import Select from "$lib/ui/Select.svelte";
	import { frontMatterStore, navigationStore } from "$lib/store";
  import { getHttpUrl } from "$lib/business/util";
	import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
	import SaveButton from "$lib/components/SaveButton.svelte";
	import { cloneDeep, isEqual } from "lodash-es";
  import { EQUIP_NOT_DEFINED } from "$lib/business/mode";
  import { t } from "svelte-i18n";
	import { text } from "@sveltejs/kit";

  export let data: { control: string[][], boardType: string, humidStatus: string[] };

  let title = $t('level1.humidifier.humidifier-control');

  let autoControl = [
    { text: $t('global.manual'), value: '0' },
    { text: $t('level1.humidifier.timer-default'), value: '1' },
    { text: $t('global.automatic'), value: '2' },
  ];

  let options = [] as { text: string, value: string}[];

  let validation = {
    'coolOn': '',
    'coolOff': '',
    'recircOn': '',
    'recircOff': '',
    'refrigOn': '',
    'refrigOff': ''
  }
  $: main = $frontMatterStore?.main as string[];
  $: ready = false;
  $: wait = false;
  $: selected = '0';
  $: index = parseInt(selected, 10);
  $: level = $navigationStore.level;
  $: edit = $navigationStore.level > 0;
  $: humidifier = {} as { control: string[][], boardType: string, humidStatus: string[] };
  $: textSize = edit ? 'text-size-large' : 'text-size-xl';
  $: compSize = edit ? 'lg' : 'xl';

  function getStatus(sel: string): string {
    const idx = parseInt(sel, 10);
    if (humidifier.humidStatus?.[idx] === '0') {
      return $t('global.on');
    } else {
      if (humidifier.humidStatus?.[idx + 3] === '0') {
        return $t('global.off');
      } else {
        return $t('level1.humidifier.remote-off');
      }
    }
  }

  onMount(async () => {
		try {
      $navigationStore.data = getHttpUrl('/iot/humidifier');
      $navigationStore.isDirty = () => !isEqual(humidifier.control?.[parseInt(selected, 10)], data.control?.[parseInt(selected, 10)]);
      humidifier = cloneDeep(data);
      if (($frontMatterStore?.panel as string[])?.[14] !== EQUIP_NOT_DEFINED) options.push({ text: `${$t('global.humidifier')} #1`, value: '0' });
      if (($frontMatterStore?.panel as string[])?.[18] !== EQUIP_NOT_DEFINED) options.push({ text: `${$t('global.humidifier')} #2`, value: '1' });
      if (($frontMatterStore?.panel as string[])?.[22] !== EQUIP_NOT_DEFINED) options.push({ text: `${$t('global.humidifier')} #3`, value: '2' });
		} catch (error) {
      console.error(error);
		}
    ready = true;
  });
</script>

<GellertPage {wait} {ready} {title} {level} name="humidifier">
  <Card class="mx-auto w-3/4 flex flex-col mt-1">
    <Table class="mb-1 {textSize}">
      <Row>
        <Column class="xl:py-1" colspan={3}>{ $t('level1.humidifier.equipment-selection') }:
          <Select class="w-64 md:w-96 xl:w-128 my-1" size={compSize} bind:value={selected} edit={true} {options} />
        </Column>
      </Row>
    </Table>
    {#if humidifier.control && humidifier.control[index]}
      <Table class="mb-1 {textSize}">
        <Row>
          <Column class="xl:py-1" colspan={3}>{ $t('level1.humidifier.auto-control-mode') }: <Select class="w-64 md:w-96 3xl:w-128 my-1" size={compSize} bind:value={humidifier.control[index][1]} options={autoControl} {edit}/></Column>
        </Row>
      </Table>
      {#if humidifier.control[index][1] === '2'}
        <Table class="mb-1 {textSize}">
          <Row>
            <Column class="xl:py-1">{ $t('level1.humidifier.the-system-will-automatically-maintain-the') } { $t('level1.humidifier.plenum-humidity-setpoint-of') } {main?.[6]}%.</Column>
          </Row>
        </Table>
      {:else if humidifier.control[index][1] === '1'}
        <Table class="mb-1 {textSize}">
          <Row>
            <Column class="xl:py-1 w-1/3 border-r border-gray-400" rowspan={2}>{ $t('level1.humidifier.system-mode') }</Column>
            <Column class="xl:py-1 w-1/3" colspan={2}>{ $t('level1.humidifier.cycle-duration') }</Column>
          </Row>
          <Row>
            <Column class="xl:py-1 w-1/3 border-r border-gray-400">{ $t('level1.humidifier.seconds-on') }</Column>
            <Column class="xl:py-1 w-1/3">{ $t('level1.humidifier.seconds-off') }</Column>
          </Row>
          <Row>
            <Column class="xl:py-1 w-1/3 border-r border-gray-400">{ $t('global.cooling') }</Column>
            <Column class="xl:py-1 w-1/3 border-r border-gray-400"><TextField bind:value={humidifier.control[index][2]} size={compSize} {edit} label="Cooling On" keyboardType={KeyboardTypes.Numeric} validation={validation.coolOn}/></Column>
            <Column class="xl:py-1 w-1/3"><TextField bind:value={humidifier.control[index][3]} size={compSize} {edit} label="Cooling Off" keyboardType={KeyboardTypes.Numeric} validation={validation.coolOff}/></Column>
          </Row>
          <Row>
            <Column class="xl:py-1 w-1/3 border-r border-gray-400">{ $t('global.recirculation') }</Column>
            <Column class="xl:py-1 w-1/3 border-r border-gray-400"><TextField bind:value={humidifier.control[index][4]} size={compSize} {edit} label="Recirculation On" keyboardType={KeyboardTypes.Numeric} validation={validation.recircOn}/></Column>
            <Column class="xl:py-1 w-1/3"><TextField bind:value={humidifier.control[index][5]} size={compSize} {edit} label="Recirculation Off" keyboardType={KeyboardTypes.Numeric} validation={validation.recircOff}/></Column>
          </Row>
          <Row>
            <Column class="xl:py-1 w-1/3 border-r border-gray-400">{ $t('global.refrigeration') }</Column>
            <Column class="xl:py-1 w-1/3 border-r border-gray-400"><TextField bind:value={humidifier.control[index][6]} size={compSize} {edit} label="Refrigeration On" keyboardType={KeyboardTypes.Numeric} validation={validation.refrigOn}/></Column>
            <Column class="xl:py-1 w-1/3"><TextField bind:value={humidifier.control[index][7]} size={compSize} {edit} label="Refrigeration Off" keyboardType={KeyboardTypes.Numeric} validation={validation.refrigOff}/></Column>
          </Row>
        </Table>
      {/if}
      <Table class={textSize}>
        <Row>
          <Column class="xl:py-1 w-1/3 border-r border-gray-400">{ $t('level1.humidifier.humidifier-status') }:</Column>
          <Column class="xl:py-1 w-2/3">{getStatus(selected)}</Column>
        </Row>
      </Table>
      <SaveButton {edit} bind:wait={wait} data={humidifier.control[index]} bind:original={data.control[index]} route="humidifier" bind:validation={validation} autoSave />
    {/if}
  </Card>
</GellertPage>


