<script lang="ts">
  import { onMount } from "svelte";
  import Card from "$lib/ui/Card.svelte";
  import Select from "$lib/ui/Select.svelte";
  import TextField from "$lib/ui/TextField.svelte";
  import Table from "$lib/ui/Table.svelte";
  import Row from "$lib/ui/Row.svelte";
  import Column from "$lib/ui/Column.svelte";
  import SaveButton from "$lib/components/SaveButton.svelte";
  import { frontMatterStore, navigationStore } from "$lib/store";
  import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
  import { AdornmentType } from "$lib/business/adornmentType";
  import { isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
  import { SensorTypes, type SensorInfo } from "$lib/business/analog";
  import { outsideAirSettings, cureSettings, tempAlarmSettings,
           sensorList as sensorListStore } from "$lib/business/protoStores";
  import { useDraft, numField } from "$lib/business/useDraft";
  import { writeProto } from "$lib/business/protoWrite";
  import { TAG } from "$lib/business/protoTags";

  // Shared body of the Outside-Air Control page (level1/outside): the
  // free-cooling economizer setpoint (+ onion-cure start/limit inputs).
  // Rendered on the classic page AND as the "Outside Air" tab of the
  // dashboard door modal (air-side grouping). This is a LEVEL-1 control, so
  // even inside the L2 door modal its tab unlocks at Program L1 (the host
  // passes a level-1 canEdit). Prop contract mirrors PlenumSetpointsForm.
  // docs/spatial-ui-page-migration.md
  export let wait = false;
  export let ready = false;
  export let embedded = false;
  export let theme: 'light' | 'dark' = 'light';
  export let canEdit: boolean | null = null;

  const air  = useDraft(outsideAirSettings, TAG.OutsideAirSettings);
  const cure = useDraft(cureSettings,        TAG.CureSettings);
  const { draft: aDraft, live: aLive, hydrated: aHyd } = air;
  const { draft: cDraft, live: cLive, hydrated: cHyd } = cure;

  const diffStr  = numField(aDraft, 'differential', 'float');
  const startTempStr  = numField(cDraft, 'startTemp',     'float');
  const startHumidStr = numField(cDraft, 'startHumid',    'float');
  const highLimitStr  = numField(cDraft, 'humidHighLimit','float');

  $: defaultSelTempRef       = { text: $t('global.plenum-setpoint-default'),  value: 255 };
  $: defaultSelTempRefNoEdit = { text: $t('level1.outside.plenum-setpoint'),  value: 255 };

  let validation: Record<string, string> = {
    'OutsideAirSet': '', 'CureStartTemp': '', 'CureStartHumid': '', 'CureHumidHighLimit': '',
  };

  $: selAboveBelow = [
    { text: $t('level1.outside.above'), value: 0 },
    { text: $t('level1.outside.below'), value: 1 },
  ];
  $: humidOptions = [
    { text: $t('global.plenum-humidity'),             value: 0 },
    { text: $t('level1.outside.calculated-humidity'), value: 1 },
  ];

  $: level = $navigationStore.level;
  $: edit = canEdit ?? (level > 0);
  $: sensorList = $sensorListStore as SensorInfo[];
  $: onionMode = ($frontMatterStore?.panel as string[])?.[8] === '1';
  $: hydrated = $aHyd && $cHyd;
  $: dev = $tempAlarmSettings ? String($tempAlarmSettings.lowTemp ?? 1.0) : '1.0';

  // Pin the economizer reference to OUTSIDE (mode 0); a stale OSPI blob may
  // still hold the removed "Plenum" value (1). Firmware clamps it too.
  $: if ($aHyd && $aDraft.mode !== 0) $aDraft.mode = 0;

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

  // SaveButton ref so the modal can flush on close-unless-cancel.
  let saveBtn: { save: () => Promise<void> } | undefined;
  export async function flush(): Promise<void> {
    const dirty = !isEqual($aDraft, $aLive) || !isEqual($cDraft, $cLive);
    if (dirty && saveBtn) await saveBtn.save();
  }

  onMount(() => {
    if (!embedded) {
      $navigationStore.isDirty = () =>
        !isEqual($aDraft, $aLive) || !isEqual($cDraft, $cLive);
    }
    ready = true;
  });

  async function saveOutside(): Promise<void> {
    await writeProto(TAG.OutsideAirSettings, $aDraft);
    if (onionMode) await writeProto(TAG.CureSettings, $cDraft);
  }
</script>

<div class="pform pform--{theme}">
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
              <span class="font-bold">{ $t('level1.outside.outside-air') }</span>
              { $t('level1.outside.temperature-is') }
              <TextField class="w-16 md:w-24 xl:w-36" size="xl" bind:value={$diffStr} {edit} label="Cooling Start" keyboardType={KeyboardTypes.Float} adornmentType={AdornmentType.Temperature} validation={validation.OutsideAirSet}/>
              <Select class="w-36 xl:w-48 3xl:w-96" size="xl" bind:value={$aDraft.aboveBelow} options={selAboveBelow} {edit} />
              <Select class="w-64 xl:w-128 3xl:w-[44rem]" size="xl" bind:value={$aDraft.tempRef} options={selTempRef} {edit} />
            </p>
          {/if}
        </Column>
      </Row>
    </Table>
    <SaveButton bind:this={saveBtn} {edit} bind:wait={wait}
                data={{ a: $aDraft, c: $cDraft }}
                original={{ a: $aLive, c: $cLive }}
                bind:validation={validation} autoSave
                onSave={saveOutside} />
    {/if}
  </Card>
</div>
