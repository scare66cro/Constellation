<script lang="ts">
	import { onMount } from "svelte";
  import GellertPage from "$lib/components/GellertPage.svelte";
	import Card from "$lib/ui/Card.svelte";
  import Select from "$lib/ui/Select.svelte";
  import TextField from "$lib/ui/TextField.svelte";
  import { frontMatterStore, navigationStore } from "$lib/store";
  import { getHttpUrl } from "$lib/business/util";
	import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
	import { AdornmentType } from "$lib/business/adornmentType";
	import Table from "$lib/ui/Table.svelte";
  import Row from "$lib/ui/Row.svelte";
  import Column from "$lib/ui/Column.svelte";
	import SaveButton from "$lib/components/SaveButton.svelte";
	import { cloneDeep, isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
	import { SensorTypes, type SensorInfo } from "$lib/business/analog";

  export let data: { outside: string[], cure: string[], dev: string,  sensors: SensorInfo[] };

  let title = $t('level1.outside.outside-air-control');

  let defaultSelTempRef = { text: $t('global.plenum-setpoint-default'), value: '255'};
  let defaultSelTempRefNoEdit = { text: $t('level1.outside.plenum-setpoint'), value: '255'};

  let validation: Record<string, string> = {
    'OutsideAirSet': '',
    'CureStartTemp': '',
    'CureStartHumid': '',
    'CureHumidHighLimit': '',
  }

  let ctrlMode = [
    { text: $t('level1.outside.outside-air'), value: '0' },
    { text: $t('level1.outside.plenum'), value: '1' },
  ];

  let selAboveBelow = [
    { text: $t('level1.outside.above'), value: '0' },
    { text: $t('level1.outside.below'), value: '1' },
  ];

  let selTempRef = [] as {text: string, value: string}[];

  let humidOptions = [
    { text: $t('global.plenum-humidity'), value: '0' },
    { text: $t('level1.outside.calculated-humidity'), value: '1' }
  ];

  $: ready = false;
  $: wait = false;
  $: level = $navigationStore.level;
  $: sensorList = (data?.sensors ?? []) as SensorInfo[];
  let originalAir: { outside: string[], cure: string[], dev: string } = { outside: [], cure: [], dev: '' };
  $: air = {} as { outside: string[], cure: string[], dev: string };
  $: edit = $navigationStore.level > 0;
  $: onionMode = ($frontMatterStore?.panel as string[])?.[8] === '1';

  /* Rebuild the temperature-reference dropdown whenever the page becomes
   * ready, the edit mode toggles, the control mode flips between outside
   * air (0) and plenum (1), or the sensor list arrives. */
  $: if (ready) {
    const next: {text: string, value: string}[] = [];
    next.push(edit ? defaultSelTempRef : defaultSelTempRefNoEdit);
    if (air.outside?.[4] === '0') {
      sensorList.forEach((sensor) => {
        if (sensor.type === SensorTypes.SENSOR_RETURN_TEMP_1) {
          next.push({ text: sensor.label, value: '254' });
        } else if (sensor.type === SensorTypes.SENSOR_RETURN_TEMP_2 || sensor.type === SensorTypes.SENSOR_PILE_TEMP) {
          next.push({ text: sensor.label, value: sensor.id.toString() });
        }
      });
    }
    selTempRef = next;   /* reassign so Svelte updates the <Select> */
  }

  onMount(async () => {
		try {
      $navigationStore.data = getHttpUrl('/iot/outside');
      originalAir = {
        outside: cloneDeep(data.outside),
        cure: cloneDeep(data.cure),
        dev: data.dev,
      };
      $navigationStore.isDirty = () => !isEqual(air, originalAir);
      air = cloneDeep(originalAir);
		} catch (error) {
      console.error(error);
		}
		ready = true;
  });
</script>

<GellertPage {wait} {ready} {title} {level} name='outside'>
  <Card class="mt-2 flex flex-col w-3/4 mx-auto">
    <Table class="mb-2">
      <Row>
        <Column class="text-size-xl">
          {#if onionMode && air.outside && air.cure && air.dev}
            <p class="text-center my-2 mx-4">{ $t('level1.outside.system-will-enter-outside-air-cure-mode-when') }
              { $t('level1.outside.outside-air-temperature-is-above') } <TextField class="w-36" size="xl" bind:value={air.cure[0]} {edit} label="Cure Start Temp" keyboardType={KeyboardTypes.Float} adornmentType={AdornmentType.Temperature} validation={validation.CureStartTemp}/>
              { $t('global.and') } <Select class="w-36 3xl:w-48" size="xl" bind:value={air.cure[1]} options={humidOptions} {edit} />
              { $t('level1.outside.is-below') } <TextField class="w-36" size="xl" bind:value={air.cure[2]} {edit} label="Cure Start Humid" keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Percent} validation={validation.CureStartHumid}/>.
            </p>
            <p class="text-center my-2 mx-4">{ $t('level1.outside.in-outside-air-and-burner-cure-modes-system-will-blend-outside-air-while') }
              { $t('level1.outside.plenum-temperature-remains-above-the-low-deviation-alarm-value-of') } <TextField class="w-36" size="xl" value={air.dev} edit={false} adornmentType={AdornmentType.Temperature} />
              { $t('level1.outside.above-chinese') } { $t('global.and') } <br/>
              <TextField class="w-36" size="xl" value={air.cure[1] === '0' ? $t('level1.outside.plenum') : $t('global.calculated')} edit={false} /> { $t('level1.outside.humidity-remains-below') }
              <TextField class="w-36" size="xl" value={air.cure[3]} {edit} label="Humidity" keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Percent} validation={validation.CureHumidHighLimit} />&nbsp;.
            </p>
          {:else if air.outside && air.cure}
            <p class="text-center my-2 mx-4">{ $t('level1.outside.system-can-run-on-outside-air-cooling-when') } 
              <Select class="w-36 xl:w-96 3xl:w-128" size="xl" bind:value={air.outside[4]} options={ctrlMode} {edit} />
              { $t('level1.outside.temperature-is') }
              <TextField class="w-16 md:w-24 xl:w-36" size="xl" bind:value={air.outside[0]} {edit} label="Cooling Start" keyboardType={KeyboardTypes.Float} adornmentType={AdornmentType.Temperature} validation={validation.OutsideAirSet}/>
              <Select class="w-36 xl:w-48 3xl:w-96" size="xl" bind:value={air.outside[1]} options={selAboveBelow} {edit} />
              <Select class="w-64 xl:w-128 3xl:w-[44rem]" size="xl" bind:value={air.outside[2]} options={selTempRef} {edit} />
            </p>
          {/if}
        </Column>
      </Row>
    </Table>
    <SaveButton {edit} bind:wait={wait} data={air} bind:original={originalAir} bind:validation={validation} route="outside" autoSave />
  </Card>
</GellertPage>



