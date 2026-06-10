<script lang="ts">
  import { onMount } from "svelte";
  import Card from "$lib/ui/Card.svelte";
  import Table from "$lib/ui/Table.svelte";
  import Row from "$lib/ui/Row.svelte";
  import Column from "$lib/ui/Column.svelte";
  import Select from "$lib/ui/Select.svelte";
  import TextField from "$lib/ui/TextField.svelte";
  import SaveButton from "$lib/components/SaveButton.svelte";
  import { navigationStore } from "$lib/store";
  import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
  import { AdornmentType } from "$lib/business/adornmentType";
  import { isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
  import { miscSettings, sensorList as sensorListStore } from "$lib/business/protoStores";
  import { useDraft, numField } from "$lib/business/useDraft";
  import { SensorTypes, type SensorInfo } from "$lib/business/analog";
  import { TAG } from "$lib/business/protoTags";

  // Shared body of the (new) Heat Control page. Two heating subsystems, both
  // backed by MiscSettings (TAG.MiscSettings):
  //   • Plenum heater turn-on threshold (heat_temp_thresh) — HEAT engages when
  //     plenum temp falls this far below setpoint.
  //   • Cavity heater (cavity_target / cavity_mode / cavity_diff /
  //     cavity_duty_or_sensor / cavity_standby_on) — EQ_CAVITY_HEAT.
  // Both relocated from level1/miscellaneous (the dedicated heat page now owns
  // all heat controls). Draft saves the full MiscSettings (same path the misc
  // page uses; remaining misc fields echoed from live). Prop contract mirrors
  // PlenumSetpointsForm. docs/spatial-ui-page-migration.md
  export let wait = false;
  export let ready = false;
  export let embedded = false;
  export let theme: 'light' | 'dark' = 'light';
  export let canEdit: boolean | null = null;

  const misc = useDraft(miscSettings, TAG.MiscSettings);
  const { draft, hydrated, live } = misc;
  const heatTempThreshStr = numField(draft, 'heatTempThresh',     'float');
  const cavityDiffStr     = numField(draft, 'cavityDiff',         'float');
  const cavityDutyStr     = numField(draft, 'cavityDutyOrSensor', 'int');

  $: level = $navigationStore.level;
  $: edit = canEdit ?? (level > 0);
  $: textSize = edit ? 'text-size-large' : 'text-size-xl';
  $: compSize = edit ? 'lg' : 'xl';

  $: isOff    = $draft.cavityMode === 1;
  $: isManual = $draft.cavityMode === 2;
  $: isAuto   = $draft.cavityMode === 3;

  $: cavityTargetOptions = [
    { text: $t('level1.miscellaneous.cavity-heater-control'), value: 0 },
    { text: $t('level1.miscellaneous.pile-fan-control'),      value: 1 },
  ];
  $: cavityModeOptions = [
    // Backend expects 1=Off, 2=Manual, 3=Automatic
    { text: $t('global.off'),       value: 1 },
    { text: $t('global.manual'),    value: 2 },
    { text: $t('global.automatic'), value: 3 },
  ];
  $: standbyOptions = [
    { text: $t('global.off'), value: 0 },
    { text: $t('global.on'),  value: 1 },
  ];

  // Pile-temperature sensor list for cavity Auto mode.
  type Option = { text: string; value: number };
  $: sensorOptions = (($sensorListStore as SensorInfo[]) ?? [])
    .filter((s) => s.type === SensorTypes.SENSOR_PILE_TEMP && !(s as any).disabled)
    .map((s): Option => ({ text: s.label, value: Number(s.id) }));

  let validation = { tempThresh: '', cavityDiff: '', cavityDutyCycle: '' };

  // ─── Cavity-mode change: apply per-mode defaults ─────────────────────
  let prevCavityMode: number | null = null;
  $: if ($hydrated && $draft.cavityMode !== prevCavityMode) {
    if (prevCavityMode !== null) onCavityModeChange($draft.cavityMode, prevCavityMode);
    prevCavityMode = $draft.cavityMode;
  }
  function onCavityModeChange(next: number, prev: number) {
    if (next === 2 && prev !== 2) {
      cavityDutyStr.set('50'); // Manual → default duty cycle 50.
    }
    if (next === 3) {
      // Automatic → ensure a valid pile sensor is selected.
      const cur = $draft.cavityDutyOrSensor;
      const valid = sensorOptions.some((o) => o.value === cur);
      if (!valid && sensorOptions.length > 0) {
        cavityDutyStr.set(String(sensorOptions[0].value));
      }
    }
  }

  // SaveButton ref so the modal can flush on close-unless-cancel.
  let saveBtn: { save: () => Promise<void> } | undefined;
  export async function flush(): Promise<void> {
    if (!isEqual($draft, $live) && saveBtn) await saveBtn.save();
  }

  onMount(() => {
    if (!embedded) $navigationStore.isDirty = () => !isEqual($draft, $live);
    ready = true;
  });
</script>

<div class="pform pform--{theme}">
  <Card class="mx-auto mt-2 flex flex-col container-wide">
    {#if $hydrated}
      <Table class="mb-2">
        <Row>
          <Column class="px-2 {textSize}">
            { $t('level1.miscellaneous.heater-will-turn-on-if-plenum-temperature-is') }
            <TextField class="w-36" size={compSize} bind:value={$heatTempThreshStr} {edit} label="Heater On Temperature" keyboardType={KeyboardTypes.Float} adornmentType={AdornmentType.Temperature} validation={validation.tempThresh}/>
            { $t('level1.miscellaneous.below-plenum-setpoint') }
          </Column>
        </Row>
      </Table>

      <Table class="mb-2">
        <Row>
          <Column class={textSize}>
            <Select class="ml-2 w-144" size={compSize} bind:value={$draft.cavityTarget} options={cavityTargetOptions} {edit}/>
            <Select class="ml-2 w-128" size={compSize} bind:value={$draft.cavityMode} options={cavityModeOptions} {edit}/>
          </Column>
        </Row>
        <Row>
          <Column class={textSize}>
            { $t('level1.miscellaneous.temperature-differential') }: <TextField class="w-36" size={compSize} bind:value={$cavityDiffStr} {edit} label="Temperature Differential" keyboardType={KeyboardTypes.Float} adornmentType={AdornmentType.Temperature} validation={validation.cavityDiff} disabled={isOff}/>
          </Column>
        </Row>
        {#if isManual}
          <Row>
            <Column class={textSize}>
              { $t('global.duty-cycle') }: <TextField class="w-36" size={compSize} bind:value={$cavityDutyStr} {edit} label="Duty Cycle" keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Percent} validation={validation.cavityDutyCycle} disabled={isOff}/>
            </Column>
          </Row>
        {:else if isAuto}
          <Row>
            <Column class={textSize}>
              { $t('global.automatic') }: <Select class="ml-2 w-128" size={compSize} bind:value={$draft.cavityDutyOrSensor} options={sensorOptions} {edit} disabled={isOff}/>
            </Column>
          </Row>
        {/if}
        <Row>
          <Column class={textSize}>
            { $t('level1.miscellaneous.run-in-standby') }: <Select class="ml-2 w-96" size={compSize} bind:value={$draft.cavityStandbyOn} options={standbyOptions} {edit}/>
          </Column>
        </Row>
      </Table>

      <SaveButton bind:this={saveBtn} {edit} bind:wait={wait} data={$draft} original={$live} bind:validation={validation} autoSave onSave={() => misc.save()} />
    {/if}
  </Card>
</div>
