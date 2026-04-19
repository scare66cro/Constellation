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
  import { getHttpUrl } from "$lib/business/util";
	import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
	import { AdornmentType } from "$lib/business/adornmentType";
	import SaveButton from "$lib/components/SaveButton.svelte";
  import { cloneDeep, isEqual } from "lodash-es";
  import { t } from "svelte-i18n";

  export let data: { miscData: string[], outputConfig: string[], pwmConfig: string[] };

  $: title = $t('level1.miscellaneous.miscellaneous-program-parameters');

  $: refrigModeOptions = [
    { text: $t('level1.miscellaneous.economizer'), value: '0' },
    { text: $t('level1.miscellaneous.refrigeration-only'), value: '1' },
    { text: $t('level1.miscellaneous.enthalpy-cooling'), value: '2' },
  ];
  $: controlTargetOptions = [
    { text: $t('level1.miscellaneous.cavity-heater-control'), value: '0' },
    { text: $t('level1.miscellaneous.pile-fan-control'), value: '1' },
  ];

  $: controlOptions = [
  // Backend expects 1=Off, 2=Manual, 3=Automatic
  { text: $t('global.off'), value: '1' },
  { text: $t('global.manual'), value: '2' },
  { text: $t('global.automatic'), value: '3' },
  ];

  $: keypadOptions = [
    { text: $t('level1.miscellaneous.standard'), value: '0' },
    { text: $t('level1.miscellaneous.alphabetic'), value: '1' },
  ];

  $: standbyOptions = [
    { text: $t('global.off'), value: '0' },
    { text: $t('global.on'), value: '1' },
  ];

  // Removed locale options: handled in Preferences page

  let validation = {
    'defrostInterval': '',
    'defrostTime': '',
    'tempThresh': '',
    'cavityDiff': '',
    'cavityDutyCycle': '',
    'refrigThresh': '',
    'enthTarget': '',
  }

  $: miscData = [] as string[];
  $: outputConfig = data?.outputConfig ?? [];
  $: pwmConfig = data?.pwmConfig ?? [];
  $: ready = false;
  $: wait = false;
  $: level = $navigationStore.level;
  $: edit = $navigationStore.level > 0;
  $: useEnthalpy = miscData?.[0] === '2';
  $: textSize = edit ? 'text-size-large' : 'text-size-xl';
  $: compSize = edit ? 'lg' : 'xl';
  let showImageManager = false;

  // Dynamic Cavity Control UI state
  // Indices per backend mapping:
  // 4: selCtrlMode (target), 5: selCavityCtrl, 6: cavityDiff, 7: cavityDutyCycle or SensorId
  $: selCavityCtrl = miscData?.[5] ?? '1';
  $: isOff = selCavityCtrl === '1';
  $: isManual = selCavityCtrl === '2';
  $: isAuto = selCavityCtrl === '3';
  let prevSelCavityCtrl: string | null = null;

  type Option = { text: string; value: string };
  let sensorOptions: Option[] = [];

  function ensureDefaultsForMode(): void {
    // Called when mode changes to set sensible defaults
    // Guard against missing or invalid miscData
    if (!miscData || !Array.isArray(miscData)) {
      return;
    }
    
    if (isManual) {
      // Default duty cycle to 50 if empty
      if (!miscData?.[7] || isNaN(Number(miscData[7]))) {
        miscData[7] = '50';
      }
    } else if (isAuto) {
      // Default sensor to first available
      const current = miscData?.[7];
      const hasCurrent = !!current && sensorOptions.some((o) => o.value === current);
      if (!hasCurrent && sensorOptions.length > 0) {
        miscData[7] = sensorOptions[0].value;
      }
    }
  }

  // Removed changeLocale: handled in Preferences page
    onMount(async () => {
      try {
        $navigationStore.data = getHttpUrl('/iot/misc');
        $navigationStore.isDirty = () => !isEqual(miscData, data.miscData);
        miscData = cloneDeep(data.miscData ?? []);
        // Load pile temperature sensor labels for Automatic mode sensor selection
        try {
          const r = await fetch(getHttpUrl('/iot/ramp'), {
            method: 'GET',
            headers: { 'Content-Type': 'application/json' },
            credentials: 'include'
          });
          
          if (!r.ok) {
            console.warn(`Failed to fetch ramp data: ${r.status} ${r.statusText}`);
            // Continue without sensor options - automatic mode will be disabled
            sensorOptions = [];
          } else {
            const j = await r.json();
            const pile = (j?.pile ?? []) as string[];
            const opts: Option[] = [];
            for (let i = 0; i + 1 < pile.length; i += 2) {
              const text = pile[i];
              const value = pile[i + 1];
              if (typeof text === 'string' && typeof value === 'string') {
                opts.push({ text, value });
              }
            }
            sensorOptions = opts;
            // After loading sensors, ensure defaults are valid for current mode
            ensureDefaultsForMode();
          }
        } catch (e) {
          console.warn('Failed to load sensor labels for Automatic mode (network error)', e);
          // Set empty sensor options to gracefully handle offline state
          sensorOptions = [];
        }
      } catch (error) {
        console.error('Error initializing miscellaneous page:', error);
        // Ensure miscData is at least an empty array to prevent render crashes
        if (!miscData || !Array.isArray(miscData)) {
          miscData = [];
        }
      }
      ready = true;
    });

  // Handle change from user to avoid reactive cycles
  function handleSelCavityCtrlChange(event: Event) {
    const newVal = (event.target as HTMLSelectElement).value;
    const prev = prevSelCavityCtrl ?? miscData[5];
    miscData[5] = newVal;
    // When switching back to Manual, reset duty cycle to 50
    if (newVal === '2' && prev !== '2') {
      miscData[7] = '50';
    }
    // When switching to Automatic, ensure a valid sensor is selected
    if (newVal === '3') {
      const hasCurrent = !!miscData?.[7] && sensorOptions.some((o) => o.value === miscData[7]);
      if (!hasCurrent && sensorOptions.length > 0) {
        miscData[7] = sensorOptions[0].value;
      }
      prevSelCavityCtrl = newVal;
    }
    miscData = miscData;
  }
</script>

<GellertPage {wait} {ready} {title} {level} name="miscellaneous">
  <Card class="mx-auto mt-2 flex flex-col container-wide">
    {#if miscData && Array.isArray(miscData) && miscData.length > 12}
      {#if outputConfig?.[13] !== '-1' || pwmConfig?.[1]?.split(':')[2] !== '-1'}
        <Table class="mb-2">
          <Row>
            <Column class="m-2 {textSize}">
              { $t('global.refrigeration-mode') }: <Select class="ml-2 w-128" size={compSize} bind:value={miscData[0]} options={refrigModeOptions} {edit}/>
            </Column>
          </Row>
          {#if useEnthalpy}
            <Row>
              <Column class="m-2 {textSize}">
                { $t('level1.miscellaneous.enthalpy-cooling-will-turn-off-if-refrigeration-is') }
                <TextField class="w-36" size={compSize} bind:value={miscData[12]} {edit} label="Enthalpy On" keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Percent} validation={validation.enthTarget}/>
                { $t('level1.miscellaneous.or-greater') }
              </Column>
            </Row>
          {/if}
          <Row>
            <Column class="m-2 {textSize}">
              { $t('level1.miscellaneous.refrigeration-will-air-defrost-every') }
              <TextField class="w-36" size={compSize} bind:value={miscData[1]} {edit} label="Defrost Start" keyboardType={KeyboardTypes.Numeric} validation={validation.defrostInterval}/>
              { $t('level1.miscellaneous.hours-for') }
              <TextField class="w-36" size={compSize} bind:value={miscData[2]} {edit} label="Defrost Duration" keyboardType={KeyboardTypes.Numeric} validation={validation.defrostTime}/>
              { $t('global.minutes') }.
            </Column>
          </Row>
        </Table>
      {/if}
      {#if outputConfig?.[4] !== '-1'}
        <Table class="mb-2">
          <Row>
            <Column class="px-2 {textSize}">
              { $t('level1.miscellaneous.heater-will-turn-on-if-plenum-temperature-is') }
              <TextField class="w-36" size={compSize} bind:value={miscData[3]} {edit} label="Heater On Temperature" keyboardType={KeyboardTypes.Float} adornmentType={AdornmentType.Temperature} validation={validation.tempThresh}/>
              { $t('level1.miscellaneous.below-plenum-setpoint') }
            </Column>
          </Row>
        </Table>
      {/if}
      {#if outputConfig?.[5] !== '-1'}
        <Table class="mb-2">
          <Row>
            <Column class={textSize}>
              <!-- selCtrlMode (target) -->
              <Select class="ml-2 w-144" size={compSize} bind:value={miscData[10]} options={controlTargetOptions} {edit}/>
              <!-- selCavityCtrl (1=Off, 2=Manual, 3=Automatic) -->
              <Select class="ml-2 w-128" size={compSize} bind:value={miscData[4]} options={controlOptions} {edit} on:change={handleSelCavityCtrlChange}/>
            </Column>
          </Row>
          <Row>
            <Column class={textSize}>
              { $t('level1.miscellaneous.temperature-differential') }: <TextField class="w-36" size={compSize} bind:value={miscData[6]} {edit} label="Temperature Differential" keyboardType={KeyboardTypes.Float} adornmentType={AdornmentType.Temperature} validation={validation.cavityDiff} disabled={isOff}/>
            </Column>
          </Row>
          {#if isManual}
            <Row>
              <Column class={textSize}>
                { $t('global.duty-cycle') }: <TextField class="w-36" size={compSize} bind:value={miscData[7]} {edit} label="Duty Cycle" keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Percent} validation={validation.cavityDutyCycle} disabled={isOff}/>
              </Column>
            </Row>
          {:else if isAuto}
            <Row>
              <Column class={textSize}>
                { $t('global.automatic') }: <Select class="ml-2 w-128" size={compSize} bind:value={miscData[7]} options={sensorOptions} {edit} disabled={isOff}/>
              </Column>
            </Row>
          {/if}
          <Row>
            <Column class={textSize}>
              { $t('level1.miscellaneous.run-in-standby') }: <Select class="ml-2 w-96" size={compSize} bind:value={miscData[13]} options={standbyOptions} {edit}/>
            </Column>
          </Row>
        </Table>
      {/if}
      <Table class="mb-2">
        <Row>
          <Column class={textSize}>
            { $t('level1.miscellaneous.system-keypad-preference') }: <Select class="ml-2 w-96" size={compSize} bind:value={miscData[8]} options={keypadOptions} {edit}/>
          </Column>
        </Row>
      </Table>
      <SaveButton {edit} bind:wait={wait} data={miscData} bind:original={data.miscData} route="misc" bind:validation={validation} autoSave />
    {/if}
  </Card>
</GellertPage>
