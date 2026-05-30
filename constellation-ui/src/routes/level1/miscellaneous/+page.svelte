<script lang="ts">
  import { onMount } from "svelte";
  import GellertPage from "$lib/components/GellertPage.svelte";
  import Card from "$lib/ui/Card.svelte";
  import Table from "$lib/ui/Table.svelte";
  import Row from "$lib/ui/Row.svelte";
  import Column from "$lib/ui/Column.svelte";
  import Select from "$lib/ui/Select.svelte";
  import TextField from "$lib/ui/TextField.svelte";
  import { navigationStore } from "$lib/store";
  import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
  import { AdornmentType } from "$lib/business/adornmentType";
  import SaveButton from "$lib/components/SaveButton.svelte";
  import { isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
  import { miscSettings, sensorList as sensorListStore } from "$lib/business/protoStores";
  import { useDraft, numField } from "$lib/business/useDraft";
  import { SensorTypes, type SensorInfo } from "$lib/business/analog";
  import { TAG } from "$lib/business/protoTags";

  // ─── Proto-direct state ──────────────────────────────────────────────
  // Reference implementation for the proto-direct migration pattern
  // (see /memories/agristar-principles.md and useDraft.ts). Replaces
  // the legacy `miscData: string[14]` positional-index page state — see
  // the git history of this file for the bug-prone shape it replaced
  // (slot [4] vs [5] cavityMode mirror, off-by-one cascades, etc.).
  const misc = useDraft(miscSettings, TAG.MiscSettings);
  const { draft, hydrated, live } = misc;

  // String mirrors for TextField (string-typed). numField() handles the
  // two-way string↔number bridge so the typed draft stays the source of
  // truth without per-page parseInt/String() boilerplate.
  const defrostIntervalStr = numField(draft, 'defrostInterval', 'int');
  const defrostDurationStr = numField(draft, 'defrostDuration', 'int');
  const heatTempThreshStr  = numField(draft, 'heatTempThresh',  'float');
  const cavityDiffStr      = numField(draft, 'cavityDiff',      'float');
  const cavityDutyStr      = numField(draft, 'cavityDutyOrSensor', 'int');
  const enthalpyOffPctStr  = numField(draft, 'enthalpyOffPct',  'int');

  // ─── Derived UI state ────────────────────────────────────────────────
  $: title = $t('level1.miscellaneous.miscellaneous-program-parameters');
  $: ready = false;
  $: wait = false;
  $: level = $navigationStore.level;
  $: edit = $navigationStore.level > 0;
  $: useEnthalpy = $draft.refrigMode === 2;
  $: textSize = edit ? 'text-size-large' : 'text-size-xl';
  $: compSize = edit ? 'lg' : 'xl';

  $: isOff = $draft.cavityMode === 1;
  $: isManual = $draft.cavityMode === 2;
  $: isAuto = $draft.cavityMode === 3;

  // ─── Option lists (typed numeric values matching proto fields) ───────
  $: refrigModeOptions = [
    { text: $t('level1.miscellaneous.economizer'), value: 0 },
    { text: $t('level1.miscellaneous.refrigeration-only'), value: 1 },
    { text: $t('level1.miscellaneous.enthalpy-cooling'), value: 2 },
  ];
  $: cavityTargetOptions = [
    { text: $t('level1.miscellaneous.cavity-heater-control'), value: 0 },
    { text: $t('level1.miscellaneous.pile-fan-control'), value: 1 },
  ];
  $: cavityModeOptions = [
    // Backend expects 1=Off, 2=Manual, 3=Automatic
    { text: $t('global.off'), value: 1 },
    { text: $t('global.manual'), value: 2 },
    { text: $t('global.automatic'), value: 3 },
  ];
  $: keypadOptions = [
    { text: $t('level1.miscellaneous.standard'), value: 0 },
    { text: $t('level1.miscellaneous.alphabetic'), value: 1 },
  ];
  $: standbyOptions = [
    { text: $t('global.off'), value: 0 },
    { text: $t('global.on'), value: 1 },
  ];

  // Pile-temperature sensor list for cavity Auto mode.
  type Option = { text: string; value: number };
  $: sensorOptions = (($sensorListStore as SensorInfo[]) ?? [])
    .filter((s) => s.type === SensorTypes.SENSOR_PILE_TEMP && !(s as any).disabled)
    .map((s): Option => ({ text: s.label, value: Number(s.id) }));

  let validation = {
    'defrostInterval': '',
    'defrostTime': '',
    'tempThresh': '',
    'cavityDiff': '',
    'cavityDutyCycle': '',
    'refrigThresh': '',
    'enthTarget': '',
  };

  // ─── Cavity-mode change: apply per-mode defaults ─────────────────────
  let prevCavityMode: number | null = null;
  $: if ($hydrated && $draft.cavityMode !== prevCavityMode) {
    if (prevCavityMode !== null) onCavityModeChange($draft.cavityMode, prevCavityMode);
    prevCavityMode = $draft.cavityMode;
  }
  function onCavityModeChange(next: number, prev: number) {
    if (next === 2 && prev !== 2) {
      // Switching to Manual → default duty cycle to 50.
      cavityDutyStr.set('50');
    }
    if (next === 3) {
      // Switching to Automatic → ensure a valid pile sensor is selected.
      const cur = $draft.cavityDutyOrSensor;
      const valid = sensorOptions.some((o) => o.value === cur);
      if (!valid && sensorOptions.length > 0) {
        cavityDutyStr.set(String(sensorOptions[0].value));
      }
    }
  }

  // ─── Save (proto-direct, typed) ──────────────────────────────────────
  // Zero-meaningful fields (mode=econ, intervals=disabled, kbPref=standard
  // …) are registered in forceFieldRegistry.ts, so writeProto force-emits
  // them automatically. See firmware apply_misc() in nova_dataexc.c for
  // the wire field numbers.
  async function save(): Promise<void> {
    await misc.save();
  }

  onMount(() => {
    $navigationStore.isDirty = () => !isEqual($draft, $live);
    ready = true;
  });
</script>

<GellertPage {wait} {ready} {title} {level} name="miscellaneous">
  <Card class="mx-auto mt-2 flex flex-col container-wide">
    {#if $hydrated}
      <Table class="mb-2">
        <Row>
          <Column class="m-2 {textSize}">
            { $t('global.refrigeration-mode') }: <Select class="ml-2 w-128" size={compSize} bind:value={$draft.refrigMode} options={refrigModeOptions} {edit}/>
          </Column>
        </Row>
        {#if useEnthalpy}
          <Row>
            <Column class="m-2 {textSize}">
              { $t('level1.miscellaneous.enthalpy-cooling-will-turn-off-if-refrigeration-is') }
              <TextField class="w-36" size={compSize} bind:value={$enthalpyOffPctStr} {edit} label="Enthalpy On" keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Percent} validation={validation.enthTarget}/>
              { $t('level1.miscellaneous.or-greater') }
            </Column>
          </Row>
        {/if}
        <Row>
          <Column class="m-2 {textSize}">
            { $t('level1.miscellaneous.refrigeration-will-air-defrost-every') }
            <TextField class="w-36" size={compSize} bind:value={$defrostIntervalStr} {edit} label="Defrost Start" keyboardType={KeyboardTypes.Numeric} validation={validation.defrostInterval}/>
            { $t('level1.miscellaneous.hours-for') }
            <TextField class="w-36" size={compSize} bind:value={$defrostDurationStr} {edit} label="Defrost Duration" keyboardType={KeyboardTypes.Numeric} validation={validation.defrostTime}/>
            { $t('global.minutes') }.
          </Column>
        </Row>
      </Table>

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

      <Table class="mb-2">
        <Row>
          <Column class={textSize}>
            { $t('level1.miscellaneous.system-keypad-preference') }: <Select class="ml-2 w-96" size={compSize} bind:value={$draft.kbPref} options={keypadOptions} {edit}/>
          </Column>
        </Row>
      </Table>

      <SaveButton {edit} bind:wait={wait} data={$draft} original={$live} bind:validation={validation} autoSave onSave={save} />
    {/if}
  </Card>
</GellertPage>
