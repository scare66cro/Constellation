<script lang="ts">
	import { onMount } from "svelte";
  import GellertPage from "$lib/components/GellertPage.svelte";
	import Card from "$lib/ui/Card.svelte";
	import Button from "$lib/ui/Button.svelte";
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
	import { safeJsonParse } from "$lib/business/util";

  export let data: { speed: string[], pile: string[] };
  let title = $t('level1.fanspeed.fan-speed-control');

  let tempRef1 = [
    { text: $t('global.plenum-setpoint-default'), value: '0' },
    { text: $t('global.plenum-temperature'), value: '1' },
  ];

  let defaultTemp2 = [
    { text: $t('global.return-air-temp-default'), value: '255' },
  ];

  let defaultTemp2NoEdit = [
    { text: $t('global.return-air-temperature'), value: '255'},
  ];

  let tempRef2: { text: string, value: string }[] = [];

  let validation: Record<string, string> = {
    'maxFanSpeed': '',
    'minFanSpeed': '',
    'refrFanSpeed': '',
    'recircFanSpeed': '',
    'updFanSpeed': '',
    'tempDiff': '',
    'setFanSpeed': '',
  };

  $: ready = false;
  $: wait = false;
  $: error = false;
  $: level = $navigationStore.level;
  $: edit = $navigationStore.level > 0;
  $: fanspeed = { speed: [], pile: []} as { speed: string[], pile: string[] };
  $: textSize = edit ? 'text-size-large' : 'text-size-xl';
  $: compSize = edit ? 'lg' : 'xl';
  $: {
    tempRef2 = [];
    if (edit) {
      tempRef2.push(...defaultTemp2);
    } else {
      tempRef2.push(...defaultTemp2NoEdit);
    }
    fanspeed.pile.forEach((item, index) => { if (index % 2 == 0) tempRef2.push({ text: item, value: fanspeed.pile[index + 1] }); });
  };

  onMount(async () => {
		try {
      $navigationStore.data = getHttpUrl('/iot/fanspeed');
      $navigationStore.isDirty = () => !isEqual(fanspeed.speed, data.speed);
      fanspeed = cloneDeep(data);
		} catch (err) {
      console.error((err as Error).message);
    }
		ready = true;
  });

  async function setNewSpeed() {
    if (fanspeed.speed.length < 9) {
      error = true;
      return;
    }
    wait = true;
    error = false;
    const result = await fetch(getHttpUrl('/iot/setfanspeed'), {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
      },
      body: JSON.stringify({
        setFanSpeed: fanspeed.speed[8],
      }),
    });
    try {
      const json = await safeJsonParse(result);
      if (json.data?.Type === 'Validation') {
        Object.keys(json.data?.errors).forEach((key) => {
          const error = (json.data.errors[key] as string[])[0];
          if (error.indexOf(':') > -1) {
            validation[key] = error.split(':').slice(1).join(' ');
          } else {
            validation[key] = error;
          }
        });
      } else if (json.status === 200) {
        // update original
        data.speed[8] = fanspeed.speed[8];
      }
    } catch (err) {
      console.error((err as Error).message);
      error = true;
    }
    wait = false;
  }
</script>

<GellertPage {wait} {ready} {title} {level} name="fanspeed">
  <Card class="mx-2 flex flex-col mt-0 md:mt-2">
    {#if fanspeed.speed.length > 10}
    <Table class="mb-1">
      <Row>
        <Column class="xl:py-1 items-center {textSize}" colspan={2}>{ $t('level1.fanspeed.current-cooling-fan-speed') }
          <TextField class="w-36" size={compSize} bind:value={fanspeed.speed[8]} {edit} label="Cooling Fan Speed" keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Percent} validation={validation.setFanSpeed}/>
          {#if edit}
            <Button size={compSize} class="ml-8 {error ? 'text-red-500' : ''}" on:click={setNewSpeed}>{ error ? $t('global.retry') : $t('level1.fanspeed.set-new-cooling-speed') }</Button>
          {/if}
        </Column>
      </Row>
      <Row>
        <Column class="xl:py-1 w-2/3 {textSize} border-r border-gray-400">{ $t('level1.fanspeed.cooling-mode-maximum') }</Column>
        <Column class="xl:py-1 w-1/3"><TextField class="w-36" size={compSize} bind:value={fanspeed.speed[0]} {edit} label="Cooling Mode Maximum" keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Percent} validation={validation.maxFanSpeed}/></Column>
      </Row>
      <Row>
        <Column class="xl:py-1 w-2/3 {textSize} border-r border-gray-400">{ $t('level1.fanspeed.cooling-mode-minimum') }</Column>
        <Column class="xl:py-1 w-1/3"><TextField class="w-36" size={compSize} bind:value={fanspeed.speed[1]} {edit} label="Cooling Mode Minimum" keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Percent} validation={validation.minFanSpeed}/></Column>
      </Row>
      <Row>
        <Column class="xl:py-1 w-2/3 {textSize} border-r border-gray-400">{ $t('global.refrigeration-mode') }</Column>
        <Column class="xl:py-1 w-1/3"><TextField class="w-36" size={compSize} bind:value={fanspeed.speed[2]} {edit} label="Refrigeration Mode" keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Percent} validation={validation.refrFanSpeed}/></Column>
      </Row>
      <Row>
        <Column class="xl:py-1 w-2/3 {textSize} border-r border-gray-400">{ $t('level1.fanspeed.recirculation-mode') }</Column>
        <Column class="xl:py-1 w-1/3"><TextField class="w-36" size={compSize} bind:value={fanspeed.speed[3]} {edit} label="Recirculation Mode" keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Percent} validation={validation.recircFanSpeed}/></Column>
      </Row>
    </Table>
    {#if parseFloat(fanspeed.speed[10]) >= 0}
      <Card class="mb-1 w-full mx-auto bg-surface-100">
        <p class="text-center {textSize}">
          { $t('level1.fanspeed.reduce-fanspeed-to-prevent-static-pressure-exceeding')}
          <TextField class="w-36 md:w-48" size={compSize} bind:value={fanspeed.speed[10]} {edit} label="Max Static Pressure" keyboardType={KeyboardTypes.Float} adornmentType={AdornmentType.StaticPressure} validation={validation.maxStaticPressure} />
        </p>
      </Card>
    {/if}
    <Card class="mb-0 w-full mx-auto bg-surface-100">
      <p class="text-center {textSize}">{ $t('level1.fanspeed.in-cooling-mode-fan-speed-will-update-every') }
        <TextField class="w-16 md:w-24 xl:w-36" size={compSize} bind:value={fanspeed.speed[4]} {edit} label="Update Time" keyboardType={KeyboardTypes.Numeric} validation={validation.updFanSpeed}/> { $t('global.hours') } { $t('level1.fanspeed.to-maintain') }
        { $t('level1.fanspeed.a-temperature-differential-of') }
        <TextField class="w-16 md:w-24 xl:w-36" size={compSize} bind:value={fanspeed.speed[5]} {edit} label="Temperature Differential" keyboardType={KeyboardTypes.Float} adornmentType={AdornmentType.Temperature} validation={validation.tempDiff}/>
        { $t('level1.fanspeed.temperature-difference') }
        { $t('global.between') } <Select class="w-64 xl:w-96 3xl:w-[44rem] text-center" size={compSize} bind:value={fanspeed.speed[6]} options={tempRef1} {edit} /> { $t('global.and') }
        <Select class="w-64 xl:w-96 3xl:w-[44rem] text-center" size={compSize} bind:value={fanspeed.speed[7]} options={tempRef2} {edit} />
        { $t('global.between-chinese') }
      </p>
    </Card>
    <SaveButton {edit} bind:wait={wait} data={fanspeed.speed} bind:original={data.speed} route="fanspeed" bind:validation={validation} autoSave />
    {/if}
  </Card>
</GellertPage>


