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
  import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
  import { AdornmentType } from "$lib/business/adornmentType";
  import SaveButton from "$lib/components/SaveButton.svelte";
  import { isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
  import { fanSpeedSettings, sensorList as sensorListStore } from "$lib/business/protoStores";
  import { useDraft, numField } from "$lib/business/useDraft";
  import { writeProto } from "$lib/business/protoWrite";
  import { TAG } from "$lib/business/protoTags";
  import { SensorTypes } from "$lib/business/analog";

  // Phase 5.1: pile/return-temp dropdown derives from $sensorList
  // (aggregated AnalogBoard pushes). Legacy +page.ts loader removed.
  const fan = useDraft(fanSpeedSettings, TAG.FanSpeedSettings);
  const { draft, hydrated, live } = fan;

  const maxSpeedStr     = numField(draft, 'maxSpeed',     'int');
  const minSpeedStr     = numField(draft, 'minSpeed',     'int');
  const refrigSpeedStr  = numField(draft, 'refrigSpeed',  'int');
  const recircSpeedStr  = numField(draft, 'recircSpeed',  'int');
  const updatePeriodStr = numField(draft, 'updatePeriod', 'int');
  const tempDiffStr     = numField(draft, 'tempDiff',     'float');
  const prevSpeedStr    = numField(draft, 'prevSpeed',    'int');

  let title = $t('level1.fanspeed.fan-speed-control');

  $: tempRef1 = [
    { text: $t('global.plenum-setpoint-default'), value: 0 },
    { text: $t('global.plenum-temperature'),      value: 1 },
  ];
  $: defaultTemp2       = [{ text: $t('global.return-air-temp-default'),  value: 255 }];
  $: defaultTemp2NoEdit = [{ text: $t('global.return-air-temperature'),   value: 255 }];

  let validation: Record<string, string> = {
    'maxFanSpeed': '', 'minFanSpeed': '', 'refrFanSpeed': '', 'recircFanSpeed': '',
    'updFanSpeed': '', 'tempDiff': '', 'setFanSpeed': '', 'maxStaticPressure': '',
  };

  $: ready = false;
  $: wait = false;
  $: error = false;
  $: level = $navigationStore.level;
  $: edit = $navigationStore.level > 0;

  // Pile/return-temp dropdown values (sensor list filter, sim+prod identical).
  $: pile = ($sensorListStore ?? [])
    .filter((s) => !s.disabled
      && (s.type === SensorTypes.SENSOR_RETURN_TEMP_2
        || s.type === SensorTypes.SENSOR_PILE_TEMP))
    .flatMap((s) => [s.label, String(s.id)]);

  $: tempRef2 = (() => {
    const out: { text: string, value: number }[] = [];
    out.push(...(edit ? defaultTemp2 : defaultTemp2NoEdit));
    pile.forEach((item, index) => {
      if (index % 2 == 0) out.push({ text: item, value: parseInt(pile[index + 1], 10) || 0 });
    });
    return out;
  })();

  $: textSize = edit ? 'text-size-large' : 'text-size-xl';
  $: compSize = edit ? 'lg' : 'xl';

  // Static-pressure visibility: legacy slot [10]; not present in proto schema
  // yet, so the optional UI block stays hidden for now.
  const staticPressureSupported = false;

  onMount(() => {
    $navigationStore.isDirty = () => !isEqual($draft, $live);
    ready = true;
  });

  async function setNewSpeed() {
    wait = true;
    error = false;
    try {
      const v = parseInt($prevSpeedStr, 10);
      if (Number.isNaN(v)) {
        validation.setFanSpeed = 'Invalid';
        wait = false;
        return;
      }
      // S9k cont 10: was POST /iot/setfanspeed → maxFanSpeed CSV. Now writes
      // FanSpeedSettings.prevSpeed (current cooling %) directly. Field 9
      // (prevSpeed) is registered in forceFieldRegistry.ts because 0 is a
      // legitimate "stop fan" value.
      await writeProto(TAG.FanSpeedSettings, { prevSpeed: v });
      validation.setFanSpeed = '';
    } catch (err) {
      console.error((err as Error).message);
      error = true;
    }
    wait = false;
  }

  /** Full save — every zero-meaningful field is registered in
   *  forceFieldRegistry.ts so writeProto force-emits them automatically. */
  async function saveFan(): Promise<void> {
    await writeProto(TAG.FanSpeedSettings, $draft);
  }
</script>

<GellertPage {wait} {ready} {title} {level} name="fanspeed">
  <Card class="mx-2 flex flex-col mt-0 md:mt-2">
    {#if $hydrated}
    <Table class="mb-1">
      <Row>
        <Column class="xl:py-1 items-center {textSize}" colspan={2}>{ $t('level1.fanspeed.current-cooling-fan-speed') }
          <TextField class="w-36" size={compSize} bind:value={$prevSpeedStr} {edit} label="Cooling Fan Speed" keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Percent} validation={validation.setFanSpeed}/>
          {#if edit}
            <Button size={compSize} class="ml-8 {error ? 'text-red-500' : ''}" on:click={setNewSpeed}>{ error ? $t('global.retry') : $t('level1.fanspeed.set-new-cooling-speed') }</Button>
          {/if}
        </Column>
      </Row>
      <Row>
        <Column class="xl:py-1 w-2/3 {textSize} border-r border-gray-400">{ $t('level1.fanspeed.cooling-mode-maximum') }</Column>
        <Column class="xl:py-1 w-1/3"><TextField class="w-36" size={compSize} bind:value={$maxSpeedStr} {edit} label="Cooling Mode Maximum" keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Percent} validation={validation.maxFanSpeed}/></Column>
      </Row>
      <Row>
        <Column class="xl:py-1 w-2/3 {textSize} border-r border-gray-400">{ $t('level1.fanspeed.cooling-mode-minimum') }</Column>
        <Column class="xl:py-1 w-1/3"><TextField class="w-36" size={compSize} bind:value={$minSpeedStr} {edit} label="Cooling Mode Minimum" keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Percent} validation={validation.minFanSpeed}/></Column>
      </Row>
      <Row>
        <Column class="xl:py-1 w-2/3 {textSize} border-r border-gray-400">{ $t('global.refrigeration-mode') }</Column>
        <Column class="xl:py-1 w-1/3"><TextField class="w-36" size={compSize} bind:value={$refrigSpeedStr} {edit} label="Refrigeration Mode" keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Percent} validation={validation.refrFanSpeed}/></Column>
      </Row>
      <Row>
        <Column class="xl:py-1 w-2/3 {textSize} border-r border-gray-400">{ $t('level1.fanspeed.recirculation-mode') }</Column>
        <Column class="xl:py-1 w-1/3"><TextField class="w-36" size={compSize} bind:value={$recircSpeedStr} {edit} label="Recirculation Mode" keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Percent} validation={validation.recircFanSpeed}/></Column>
      </Row>
    </Table>
    {#if staticPressureSupported}
      <!-- Reserved: static-pressure cap (legacy slot 10). Not in current proto schema. -->
    {/if}
    <Card class="mb-0 w-full mx-auto bg-surface-100">
      <p class="text-center {textSize}">{ $t('level1.fanspeed.in-cooling-mode-fan-speed-will-update-every') }
        <TextField class="w-16 md:w-24 xl:w-36" size={compSize} bind:value={$updatePeriodStr} {edit} label="Update Time" keyboardType={KeyboardTypes.Numeric} validation={validation.updFanSpeed}/> { $t('global.hours') } { $t('level1.fanspeed.to-maintain') }
        { $t('level1.fanspeed.a-temperature-differential-of') }
        <TextField class="w-16 md:w-24 xl:w-36" size={compSize} bind:value={$tempDiffStr} {edit} label="Temperature Differential" keyboardType={KeyboardTypes.Float} adornmentType={AdornmentType.Temperature} validation={validation.tempDiff}/>
        { $t('level1.fanspeed.temperature-difference') }
        { $t('global.between') } <Select class="w-64 xl:w-96 3xl:w-[44rem] text-center" size={compSize} bind:value={$draft.tempRef1} options={tempRef1} {edit} /> { $t('global.and') }
        <Select class="w-64 xl:w-96 3xl:w-[44rem] text-center" size={compSize} bind:value={$draft.tempRef2} options={tempRef2} {edit} />
        { $t('global.between-chinese') }
      </p>
    </Card>
    <SaveButton {edit} bind:wait={wait} data={$draft} original={$live} bind:validation={validation} autoSave onSave={saveFan} />
    {/if}
  </Card>
</GellertPage>
