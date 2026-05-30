<script lang="ts">
	import { onMount } from "svelte";
  import GellertPage from "$lib/components/GellertPage.svelte";
	import Card from "$lib/ui/Card.svelte";
	import Table from "$lib/ui/Table.svelte";
	import Row from "$lib/ui/Row.svelte";
	import Column from "$lib/ui/Column.svelte";
	import FailureMode from "$lib/components/FailureMode.svelte";
	import TextField from "$lib/ui/TextField.svelte";
	import { AdornmentType } from "$lib/business/adornmentType";
	import SaveButton from "$lib/components/SaveButton.svelte";
  import { navigationStore, failureOptionsStore } from "$lib/store";
  import { isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
	import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
  import ScrollableArea from "$lib/components/ScrollableArea.svelte";
  import { failureSettings2 } from "$lib/business/protoStores";
  import { writeProto } from "$lib/business/protoWrite";
  import { TAG } from "$lib/business/protoTags";

  // Phase 5.1: hydrated from the FailureSettings2 proto broadcast (envelope
  // tag 54) instead of the legacy /iot/failures2 GET (deleted +page.ts).

  let title = $t('level2.failures2.failures-setup-2');

  let edit = true;

  let validation = {
      OutAirTimer: '',
      OutHumidTimer: '',
      HighCo2Timer: '',
      Co2Setpt: '',
      LowHumidTimer: '',
      LowHumidSet: '',
      PlenSenTimer: '',
      PlenSenDiff: '',
  };

  $: ready = false;
  $: wait = false;

  // Build the 13-slot string array the existing template + SaveButton expect.
  function fs2ToArray(fs2: any): string[] {
    const v = fs2 ?? {};
    return [
      String(v.outAirMode    ?? 0),
      String(v.outAirTimer   ?? 0),
      String(v.outHumidMode  ?? 0),
      String(v.outHumidTimer ?? 0),
      String(v.highCo2Mode   ?? 0),
      String(v.highCo2Timer  ?? 0),
      String(v.co2Setpt      ?? 0),
      String(v.lowHumidMode  ?? 0),
      String(v.lowHumidTimer ?? 0),
      String(v.lowHumidSet   ?? 0),
      String(v.plenSenMode   ?? 0),
      String(v.plenSenTimer  ?? 0),
      String(v.plenSenDiff   ?? 0),
    ];
  }

  let failures2: string[] = new Array(13).fill('0');
  let originalFailures2: string[] = new Array(13).fill('0');

  onMount(() => {
    const unsub = failureSettings2.subscribe((fs2) => {
      if (!fs2) return;
      const fresh = fs2ToArray(fs2);
      if (!ready || isEqual(failures2, originalFailures2)) {
        failures2 = fresh;
      }
      originalFailures2 = fresh;
      ready = true;
      if (!$navigationStore.isDirty?.()) {
        $navigationStore = { ...$navigationStore, invalidate: true };
      }
    });
    $navigationStore.isDirty = () => !isEqual(failures2, originalFailures2);
    return () => unsub();
  });
</script>

<GellertPage {wait} {ready} {title} level={2} name="failures2">
  <Card class="xl:container-wide text-size-xl md:mx-2 xl:mx-auto flex flex-col">
    <ScrollableArea>
    <Table class="text-size-xl">
      <Row>
        <Column class="border-r border-gray-400 w-1/3 text-size-large">{ $t('level2.failures2.outside-temperature-sensor') }</Column>
        <Column class="w-2/3">
          <FailureMode modeOptions={[$failureOptionsStore.modeOptions[1], $failureOptionsStore.modeOptions[2]]} bind:mode={failures2[0]} bind:timer={failures2[1]} showMinutes validation={validation.OutAirTimer} />
        </Column>
      </Row>
      <Row>
        <Column class="border-r border-gray-400 text-size-large">{ $t('level2.failures2.outside-humidity-sensor') }</Column>
        <Column>
          <FailureMode modeOptions={$failureOptionsStore.modeOptions} bind:mode={failures2[2]} bind:timer={failures2[3]} showMinutes validation={validation.OutHumidTimer}/>
        </Column>
      </Row>
      <Row>
        <Column class="border-r border-gray-400 text-size-xl" rowspan={2}>{$t('level2.failures2.high')} CO<sub>2</sub> {$t('level2.failures2.level')}</Column>
        <Column>
          <FailureMode modeOptions={$failureOptionsStore.modeOptions} bind:mode={failures2[4]} bind:timer={failures2[5]} showMinutes validation={validation.HighCo2Timer}/>
        </Column>
      </Row>
      <Row>
        <Column class="text-size-xl">
          CO<sub>2</sub> { $t('level2.failures2.setpoint') } <TextField size="xl" class="w-48" bind:value={failures2[6]} type="number" {edit} keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.CO2} validation={validation.Co2Setpt}/>
        </Column>
      </Row>
      <Row>
        <Column class="border-r border-gray-400 text-size-xl" rowspan={2}>{ $t('level2.failures2.low-plenum-humidity') }</Column>
        <Column>
          <FailureMode modeOptions={$failureOptionsStore.modeOptions} bind:mode={failures2[7]} bind:timer={failures2[8]} showMinutes validation={validation.LowHumidTimer} />
        </Column>
      </Row>
      <Row>
        <Column class="text-size-xl">
          { $t('level2.failures2.low-humidity-setpoint') } <TextField size="xl" class="w-36" bind:value={failures2[9]} type="number" {edit} keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Percent} validation={validation.LowHumidSet}/>
        </Column>
      </Row>
      <Row>
        <Column class="border-r border-gray-400 text-size-xl" rowspan={2}>{ $t('level2.failures2.plenum-sensor') }</Column>
        <Column>
          <FailureMode modeOptions={[$failureOptionsStore.modeOptions[1], $failureOptionsStore.modeOptions[2]]} bind:mode={failures2[10]} bind:timer={failures2[11]} showMinutes validation={validation.PlenSenTimer}/>
        </Column>
      </Row>
      <Row>
        <Column class="text-size-xl">
          { $t('level2.failures2.difference-of') } <TextField size="xl" class="w-36" bind:value={failures2[12]} type="number" {edit} keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Temperature} validation={validation.PlenSenDiff}/>
          { $t('global.between')}
          { $t('level2.failures2.plenum-temperature-sensor') } #1
          { $t('global.and')}
          { $t('level2.failures2.plenum-temperature-sensor') } #2
          { $t('level2.failures2.triggers-alarm-failure') }.
        </Column>
      </Row>
    </Table>
      <svelte:fragment slot="footer-center">
        <SaveButton {edit} bind:wait={wait} data={ failures2 } bind:original={originalFailures2} route="failures2" bind:validation={validation} autoSave
          onSave={async (d: string[]) => {
            // Mirrors legacyShim OutAirMode encoder. 13 firmware fields
            // (1-12 uint32, 13 float). All uint32 force-emitted (any can
            // be 0 = action "ignore").
            const ints = {
              1:  parseInt(d[0],  10) || 0,  // OutAirMode
              2:  parseInt(d[1],  10) || 0,  // OutAirTimer
              3:  parseInt(d[2],  10) || 0,  // OutHumidMode
              4:  parseInt(d[3],  10) || 0,  // OutHumidTimer
              5:  parseInt(d[4],  10) || 0,  // HighCo2Mode
              6:  parseInt(d[5],  10) || 0,  // HighCo2Timer
              7:  parseInt(d[6],  10) || 0,  // Co2Setpt
              8:  parseInt(d[7],  10) || 0,  // LowHumidMode
              9:  parseInt(d[8],  10) || 0,  // LowHumidTimer
              10: parseInt(d[9],  10) || 0,  // LowHumidSet
              11: parseInt(d[10], 10) || 0,  // PlenSenMode
              12: parseInt(d[11], 10) || 0,  // PlenSenTimer
            };
            await writeProto(TAG.FailureSettings2, {
              outAirMode:    ints[1],  outAirTimer:   ints[2],
              outHumidMode:  ints[3],  outHumidTimer: ints[4],
              highCo2Mode:   ints[5],  highCo2Timer:  ints[6],
              co2Setpt:      ints[7],
              lowHumidMode:  ints[8],  lowHumidTimer: ints[9], lowHumidSet: ints[10],
              plenSenMode:   ints[11], plenSenTimer:  ints[12],
              plenSenDiff:   parseFloat(d[12]) || 0,  // float, field 13
            });
          }} />
      </svelte:fragment>
    </ScrollableArea>
  </Card>
</GellertPage>


