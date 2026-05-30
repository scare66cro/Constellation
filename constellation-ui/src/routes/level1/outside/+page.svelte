<script lang="ts">
  import { onMount } from "svelte";
  import GellertPage from "$lib/components/GellertPage.svelte";
  import Card from "$lib/ui/Card.svelte";
  import Select from "$lib/ui/Select.svelte";
  import TextField from "$lib/ui/TextField.svelte";
  import { frontMatterStore, navigationStore } from "$lib/store";
  import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
  import { AdornmentType } from "$lib/business/adornmentType";
  import Table from "$lib/ui/Table.svelte";
  import Row from "$lib/ui/Row.svelte";
  import Column from "$lib/ui/Column.svelte";
  import SaveButton from "$lib/components/SaveButton.svelte";
  import { isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
  import { SensorTypes, type SensorInfo } from "$lib/business/analog";
  import { outsideAirSettings, cureSettings, tempAlarmSettings,
           sensorList as sensorListStore } from "$lib/business/protoStores";
  import { useDraft, numField } from "$lib/business/useDraft";
  import { writeProto } from "$lib/business/protoWrite";
  import { TAG } from "$lib/business/protoTags";

  // READ outside-air / cure / dev limit via proto stores (tags 51, 47, 55).
  // Sensor list now derives from $sensorList (aggregated AnalogBoard pushes).
  const air  = useDraft(outsideAirSettings, TAG.OutsideAirSettings);
  const cure = useDraft(cureSettings,        TAG.CureSettings);
  const { draft: aDraft, live: aLive, hydrated: aHyd } = air;
  const { draft: cDraft, live: cLive, hydrated: cHyd } = cure;

  const diffStr  = numField(aDraft, 'differential', 'float');
  const calcStr  = numField(aDraft, 'calcHumidMax', 'int');
  const startTempStr  = numField(cDraft, 'startTemp',     'float');
  const startHumidStr = numField(cDraft, 'startHumid',    'float');
  const highLimitStr  = numField(cDraft, 'humidHighLimit','float');

  let title = $t('level1.outside.outside-air-control');

  $: defaultSelTempRef       = { text: $t('global.plenum-setpoint-default'),  value: 255 };
  $: defaultSelTempRefNoEdit = { text: $t('level1.outside.plenum-setpoint'),  value: 255 };

  let validation: Record<string, string> = {
    'OutsideAirSet': '', 'CureStartTemp': '', 'CureStartHumid': '', 'CureHumidHighLimit': '',
  };

  $: ctrlMode = [
    { text: $t('level1.outside.outside-air'), value: 0 },
    { text: $t('level1.outside.plenum'),      value: 1 },
  ];
  $: selAboveBelow = [
    { text: $t('level1.outside.above'), value: 0 },
    { text: $t('level1.outside.below'), value: 1 },
  ];
  $: humidOptions = [
    { text: $t('global.plenum-humidity'),           value: 0 },
    { text: $t('level1.outside.calculated-humidity'), value: 1 },
  ];

  $: ready = false;
  $: wait = false;
  $: level = $navigationStore.level;
  $: sensorList = $sensorListStore as SensorInfo[];
  $: edit = $navigationStore.level > 0;
  $: onionMode = ($frontMatterStore?.panel as string[])?.[8] === '1';
  $: hydrated = $aHyd && $cHyd;
  $: dev = $tempAlarmSettings ? String($tempAlarmSettings.lowTemp ?? 1.0) : '1.0';

  // Rebuild the temperature-reference dropdown reactively.
  let selTempRef: { text: string, value: number }[] = [];
  $: if (ready) {
    const next: { text: string, value: number }[] = [];
    next.push(edit ? defaultSelTempRef : defaultSelTempRefNoEdit);
    if ($aDraft.mode === 0) {
      sensorList.forEach((sensor) => {
        if (sensor.type === SensorTypes.SENSOR_RETURN_TEMP_1) {
          next.push({ text: sensor.label, value: 254 });
        } else if (sensor.type === SensorTypes.SENSOR_RETURN_TEMP_2 || sensor.type === SensorTypes.SENSOR_PILE_TEMP) {
          next.push({ text: sensor.label, value: sensor.id });
        }
      });
    }
    selTempRef = next;
  }

  onMount(() => {
    $navigationStore.isDirty = () =>
      !isEqual($aDraft, $aLive) || !isEqual($cDraft, $cLive);
    ready = true;
  });

  async function saveOutside(): Promise<void> {
    // Zero-meaningful fields (mode/aboveBelow/tempRef/calcHumidMax for
    // OutsideAirSettings, humidRef for CureSettings) are registered in
    // forceFieldRegistry.ts and force-emitted automatically.
    await writeProto(TAG.OutsideAirSettings, $aDraft);
    // CureSettings: only persist when onion-mode UI surfaced cure inputs.
    if (onionMode) {
      await writeProto(TAG.CureSettings, $cDraft);
    }
  }
</script>

<GellertPage {wait} {ready} {title} {level} name='outside'>
  <Card class="mt-2 flex flex-col w-3/4 mx-auto">
    {#if hydrated}
    <Table class="mb-2">
      <Row>
        <Column class="text-size-xl">
          {#if onionMode}
            <p class="text-center my-2 mx-4">{ $t('level1.outside.system-will-enter-outside-air-cure-mode-when') }
              { $t('level1.outside.outside-air-temperature-is-above') } <TextField class="w-36" size="xl" bind:value={$startTempStr} {edit} label="Cure Start Temp" keyboardType={KeyboardTypes.Float} adornmentType={AdornmentType.Temperature} validation={validation.CureStartTemp}/>
              { $t('global.and') } <Select class="w-36 3xl:w-48" size="xl" bind:value={$cDraft.humidRef} options={humidOptions} {edit} />
              { $t('level1.outside.is-below') } <TextField class="w-36" size="xl" bind:value={$startHumidStr} {edit} label="Cure Start Humid" keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Percent} validation={validation.CureStartHumid}/>.
            </p>
            <p class="text-center my-2 mx-4">{ $t('level1.outside.in-outside-air-and-burner-cure-modes-system-will-blend-outside-air-while') }
              { $t('level1.outside.plenum-temperature-remains-above-the-low-deviation-alarm-value-of') } <TextField class="w-36" size="xl" value={dev} edit={false} adornmentType={AdornmentType.Temperature} />
              { $t('level1.outside.above-chinese') } { $t('global.and') } <br/>
              <TextField class="w-36" size="xl" value={$cDraft.humidRef === 0 ? $t('level1.outside.plenum') : $t('global.calculated')} edit={false} /> { $t('level1.outside.humidity-remains-below') }
              <TextField class="w-36" size="xl" bind:value={$highLimitStr} {edit} label="Humidity" keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Percent} validation={validation.CureHumidHighLimit} />&nbsp;.
            </p>
          {:else}
            <p class="text-center my-2 mx-4">{ $t('level1.outside.system-can-run-on-outside-air-cooling-when') }
              <Select class="w-36 xl:w-96 3xl:w-128" size="xl" bind:value={$aDraft.mode} options={ctrlMode} {edit} />
              { $t('level1.outside.temperature-is') }
              <TextField class="w-16 md:w-24 xl:w-36" size="xl" bind:value={$diffStr} {edit} label="Cooling Start" keyboardType={KeyboardTypes.Float} adornmentType={AdornmentType.Temperature} validation={validation.OutsideAirSet}/>
              <Select class="w-36 xl:w-48 3xl:w-96" size="xl" bind:value={$aDraft.aboveBelow} options={selAboveBelow} {edit} />
              <Select class="w-64 xl:w-128 3xl:w-[44rem]" size="xl" bind:value={$aDraft.tempRef} options={selTempRef} {edit} />
            </p>
          {/if}
        </Column>
      </Row>
    </Table>
    <SaveButton {edit} bind:wait={wait}
                data={{ a: $aDraft, c: $cDraft }}
                original={{ a: $aLive, c: $cLive }}
                bind:validation={validation} autoSave
                onSave={saveOutside} />
    {/if}
  </Card>
</GellertPage>
