<script lang="ts">
	import { onMount } from "svelte";
  import GellertPage from "$lib/components/GellertPage.svelte";
	import Card from "$lib/ui/Card.svelte";
	import Table from "$lib/ui/Table.svelte";
	import Row from "$lib/ui/Row.svelte";
	import Select from "$lib/ui/Select.svelte";
	import TextField from "$lib/ui/TextField.svelte";
	import { frontMatterStore, navigationStore } from "$lib/store";
  import { getHttpUrl } from "$lib/business/util";
	import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
	import { AdornmentType } from "$lib/business/adornmentType";
	import SaveButton from "$lib/components/SaveButton.svelte";
	import { cloneDeep, isEqual } from "lodash-es";
  import { isValidSensor } from "$lib/business/analog";
  import { t } from "svelte-i18n";
	import type { ArrayResponse } from "$lib/business/util";

  export let data: ArrayResponse & { refrig?: string[] };

  let title = `${$t('level1.co2.high')} CO2 ${ $t('level1.co2.level-purge-control') }`;

  let modeOptions = [
    { value: '0', text: $t('global.none-default')},
    { value: '1', text: $t('global.manual') },
    { value: '2', text: $t('global.automatic') },
  ];

  // Refrigeration purge-mode dropdown options (used by the bottom card; the
  // setting was relocated here from /level2/refrigeration so all CO2-purge
  // controls live in one place).
  let refrigerationOptions = [
    { value: '0', text: $t('level2.refrigeration.normal') },
    { value: '1', text: $t('level2.refrigeration.pump-down') },
  ];

  let validation = {
    'minTemp': '',
    'maxTemp': '',
    'co2SetPoint': '',
    'co2Target': '',
    'time': '',
    'fanOutput': '',
    'doorOutput': '',
    'PurgeHours': ''
  }

  // Validation slot for the refrigeration sub-form (separate save scope).
  let refrigValidation: Record<string, string> = { PurgeThreshold: '' };

  $: co2 = [] as string[];
  $: refrigeration = [] as string[];
  $: ready = false;
  $: wait = false;
  $: refrigWait = false;
  $: level = $navigationStore.level;
  $: edit = $navigationStore.level > 0;
  $: current = ($frontMatterStore.main as string[])?.[17];
  $: current2 = ($frontMatterStore.main as string[])?.[36];

  onMount(async () => {
    try {
      $navigationStore.data = getHttpUrl('/iot/co2');
      $navigationStore.isDirty = () => !isEqual(co2, data.array)
        || !isEqual(refrigeration, data.refrig ?? []);
      co2 = cloneDeep(data.array);
      refrigeration = cloneDeep(data.refrig ?? []);
    } catch (e) {
      console.error(e);
    }
		ready = true;
  });
</script>

<GellertPage {wait} {ready} {title} {level} name="co2">
  <Card class="mx-auto w-3/4 mt-2 flex flex-col">
    {#if co2.length > 7}
      <Table class="text-size-xl">
        <Row>
          <div class="m-2 text-center">
            CO<sub>2</sub> { $t('level1.co2.purge-control-mode') }
            <Select class="w-64 md:w-96 xl:w-128 ml-2" size="xl" bind:value={co2[0]} options={modeOptions} {edit}/>
          </div>
        </Row>
        {#if co2[0] !== '0'}
          <Row>
            <div class="m-2 text-center">
              { $t('level1.co2.a') } CO<sub>2</sub> { $t('level1.co2.purge-will-occur-if') }
              {#if co2[0] === '1'}
                { $t('level1.co2.it-has-been') }
                <TextField class="w-36" size="xl" bind:value={co2[4]} {edit} label="Last Purge" keyboardType={KeyboardTypes.Numeric} validation={validation.PurgeHours} />
                { $t('level1.co2.hours-since-the-last-purge') } <br />
              {:else if co2[0] === '2'}
                { $t('level1.co2.the-co') }<sub>2</sub> { $t('level1.co2.level-is-above') }
                <TextField class="w-36" size="xl" bind:value={co2[7]} {edit} label="CO2 Level" keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.CO2} validation={validation.co2SetPoint}/>
              {/if}
              { $t('level1.co2.and-the-outside-air-temperature-is-between-a-minumum-of') }
              <TextField class="w-36" size="xl" bind:value={co2[1]} {edit} label="Minimum Temperature" keyboardType={KeyboardTypes.Float} adornmentType={AdornmentType.Temperature} validation={validation.minTemp}/>
              { $t('level1.co2.and-a-maximum-of') }
              <TextField class="w-36" size="xl" bind:value={co2[2]} {edit} label="Maximum Temperature" keyboardType={KeyboardTypes.Float} adornmentType={AdornmentType.Temperature} validation={validation.maxTemp}/>
              { $t('global.between-chinese')}
              <br />{ $t('level1.co2.the-purge-will-last-for') }
              <TextField class="w-36" size="xl" bind:value={co2[3]} {edit} label="Purge Time" keyboardType={KeyboardTypes.Numeric} validation={validation.time}/>
              { $t('level1.co2.minutes-with') } { $t('level1.co2.a-fan-output-of') }
              <TextField class="w-36" size="xl" bind:value={co2[5]} {edit} label="Fan Output" keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Percent} validation={validation.fanOutput}/>
              { $t('level1.co2.and-a-door-output-of') }
              <TextField class="w-36" size="xl" bind:value={co2[6]} {edit} label="Door Output" keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Percent} validation={validation.doorOutput}/>
              { $t('level1.co2.door-output') }
            </div>
          </Row>
        {/if}
        <Row>
          <div class="my-4 text-center">
            { $t('level1.co2.current') } CO<sub>2</sub> { $t('level1.co2.level') } <TextField class="w-48" size="xl" value={current} edit={false} label="Current CO2 Level" adornmentType={AdornmentType.CO2} />
              {#if isValidSensor(current2)}
                | <TextField class="w-48" size="xl" value={current2} edit={false} label="Current 2 CO2 Level" adornmentType={AdornmentType.CO2} />
              {/if}
          </div>
        </Row>
      </Table>
      <SaveButton {edit} bind:wait={wait} data={co2} bind:original={data.array} route="co2" bind:validation={validation} autoSave />
    {/if}
  </Card>

  <!--
    Refrigeration purge mode + threshold — relocated from /level2/refrigeration
    so all CO2-purge-related controls live on this page.  Saves through its
    own route ("refrigeration") so it doesn't disturb the CO2 save scope.
    Only renders if the refrigeration payload is present (length > 17 covers
    indices 16/17 used below).
  -->
  {#if refrigeration.length > 17}
    <Card class="mx-auto w-3/4 mt-2 flex flex-col">
      <Table class="text-size-xl">
        <Row>
          <div class="m-2 text-center">
            { $t('level2.refrigeration.refrigeration-purge-mode-is') }
            <Select class="w-64 md:w-96 ml-2" size="lg" bind:value={refrigeration[16]} options={refrigerationOptions} {edit}/>
            { $t('level2.refrigeration.and-output-must-be-below') }
            <TextField class="w-24 ml-2" size="lg" bind:value={refrigeration[17]} {edit} label="Output Below" keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Percent} validation={refrigValidation.PurgeThreshold}/>
            { $t('level2.refrigeration.to-purge') }.
          </div>
        </Row>
      </Table>
      <SaveButton {edit} bind:wait={refrigWait} data={refrigeration} bind:original={data.refrig} route="refrigeration" bind:validation={refrigValidation} autoSave />
    </Card>
  {/if}
</GellertPage>

