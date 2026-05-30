<script lang="ts">
	import { onMount } from "svelte";
  import GellertPage from "$lib/components/GellertPage.svelte";
	import Card from "$lib/ui/Card.svelte";
	import Select from "$lib/ui/Select.svelte";
	import Table from "$lib/ui/Table.svelte";
	import Row from "$lib/ui/Row.svelte";
	import Column from "$lib/ui/Column.svelte";
	import FailureMode from "$lib/components/FailureMode.svelte";
	import SaveButton from "$lib/components/SaveButton.svelte";
	import { navigationStore, failureOptionsStore } from "$lib/store";
  import { cloneDeep, isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
  import ScrollableArea from "$lib/components/ScrollableArea.svelte";
  import { failures1Composite, type Failures1View } from "$lib/business/protoStores";
  import { writeProto } from "$lib/business/protoWrite";
  import { TAG } from "$lib/business/protoTags";

  type Failures = Failures1View;

  let title = $t('level2.failures1.failures-setup-1');

  let edit = true;

  let noFan = true;
  let noRefrig = true;
  let noRefrigStage = true;
  let noCavity = true;
  let noLights = true;
  let noClimacell = true;
  let noHumid = true;
  let noHeat = true;
  let noAux = true;
  let noBurner = true;
  let onionMode: boolean;
  let validation = {
    FanTimer: '',
    ClimacellTimer: '',
    BurnerTimer: '',
    RefridgeTimer: '',
    RefrStagesTimer: '',
    HumidifiersTimer: '',
    AuxTimer: '',
    HeatTimer: '',
    CavityHeatTimer: '',
    LightsTimer: ''
  }

  $: ready = false;
  $: wait = false;
  $: failures1 = {} as Failures;
  let original: string[] = [];
  $: if (ready) {
    noFan = failures1.InputConfig[0] === '-1' && failures1.PwmConfig[2]?.split(':')[2] === '-1';
    noRefrig = failures1.PwmConfig[1]?.split(':')[2] === '-1' && failures1.OutputConfig[13] === '-1';
    noRefrigStage = failures1.InputConfig[13] === '-1' && failures1.InputConfig[21] === '-1';
    noCavity = failures1.InputConfig[5] === '-1';
    noLights = failures1.InputConfig[23] === '-1' && failures1.InputConfig[24] === '-1';
    noClimacell = failures1.InputConfig[3] === '-1';
    noHumid = failures1.InputConfig[7] === '-1';
    noHeat = failures1.InputConfig[4] === '-1';
    noAux = failures1.InputConfig.reduce((acc, curr, index) => (index >= 25 && index <= 32 && curr !== '-1') ? acc + 1 : acc, 0) === 0;
    noBurner = failures1.InputConfig[6] === '-1' && failures1.PwmConfig[3]?.split(':')[2] === '-1';
    onionMode = failures1.systemMode === '1';
  };

  onMount(() => {
    // Hydrate from the typed proto composite (replaces the deleted
    // `/iot/failures1` GET). The composite emits `null` until every
    // constituent proto store has produced its first frame; ready stays
    // false until then so the page shows the loading state.
    const unsub = failures1Composite.subscribe((view) => {
      if (!view) return;
      if (!ready) {
        failures1 = cloneDeep(view);
        original = cloneDeep(view.failures);
        $navigationStore.isDirty = () => !isEqual(failures1.failures, original);
        ready = true;
      } else {
        // Live updates: refresh dependent fields (InputConfig/OutputConfig
        // /PwmConfig/systemMode) but preserve the user's in-progress edits
        // to `failures` until they save.
        failures1 = {
          ...view,
          failures: failures1.failures,
        };
      }
    });
    return () => unsub();
  });
</script>

<GellertPage {wait} {ready} {title} level={2} name="failures1">
  <Card class="xl:w-11/12 text-size-xl md:mx-2 xl:mx-auto flex flex-col">
    <ScrollableArea>
    <Table class="text-size-xl">
      {#if !noFan}
        <Row>
          <Column class="border-r border-gray-400">{ $t('level2.failures1.fan') }</Column>
          <Column colspan={4}>
            <FailureMode modeOptions={[$failureOptionsStore.modeOptions[2]]} bind:mode={failures1.failures[0]} bind:timer={failures1.failures[1]} showMinutes validation={validation.FanTimer}/>
          </Column>
        </Row>
      {/if}
      {#if onionMode && !noBurner}
        <Row>
          <Column class="border-r border-gray-400">{ $t('level2.failures1.burner') }</Column>
          <Column colspan={4}>
            <FailureMode modeOptions={$failureOptionsStore.modeOptions} bind:mode={failures1.failures[17]} bind:timer={failures1.failures[18]} showMinutes validation={validation.BurnerTimer}/>
          </Column>
        </Row>
      {/if}
      {#if !noClimacell && !onionMode}
        <Row>
          <Column class="border-r border-gray-400">ClimaCell</Column>
          <Column colspan={4}>
            <FailureMode modeOptions={$failureOptionsStore.modeOptions} bind:mode={failures1.failures[2]} bind:timer={failures1.failures[3]} showMinutes validation={validation.ClimacellTimer}/>
          </Column>
        </Row>
      {/if}
      {#if !noRefrig}
        <Row>
          <Column class="border-r border-gray-400">{ $t('level2.failures1.refrigeration-master') }</Column>
          <Column colspan={4}>
            <FailureMode modeOptions={$failureOptionsStore.modeOptions} bind:mode={failures1.failures[4]} bind:timer={failures1.failures[5]} showMinutes validation={validation.RefridgeTimer}/>
          </Column>
        </Row>
      {/if}
      {#if failures1.failures && failures1.failures[4] === '1' } <!-- Refridge Mode -->
        <Row>
          <Column></Column>
          <Column colspan={4}>{ $t('level2.failures1.run-in') }:
            <Select class="w-128" size="xl" bind:value={failures1.failures[6]} options={$failureOptionsStore.refridgeRunOptions(failures1.boardType, failures1.controllerVersion)} {edit} />
          </Column>
        </Row>
      {/if}
      {#if !noRefrigStage}
        <Row>
          <Column class="border-r border-gray-400">{ $t('level2.failures1.refrigeration-stages') }</Column>
          <Column colspan={4}>
            <FailureMode modeOptions={$failureOptionsStore.modeOptions} bind:mode={failures1.failures[7]} bind:timer={failures1.failures[8]} showMinutes validation={validation.RefrStagesTimer}/>
          </Column>
        </Row>
      {/if}
      {#if !noHumid}
        <Row>
          <Column class="border-r border-gray-400">{ $t('level2.failures1.humidifiers') }</Column>
          <Column colspan={4}>
            <FailureMode modeOptions={$failureOptionsStore.modeOptions} bind:mode={failures1.failures[9]} bind:timer={failures1.failures[10]} showMinutes validation={validation.HumidifiersTimer} />
          </Column>
        </Row>
      {/if}
      {#if !noAux}
        <Row>
          <Column class="border-r border-gray-400">{ $t('global.auxiliary') }</Column>
          <Column colspan={4}>
            <FailureMode modeOptions={$failureOptionsStore.modeOptions} bind:mode={failures1.failures[11]} bind:timer={failures1.failures[12]} showMinutes validation={validation.AuxTimer}/>
          </Column>
        </Row>
      {/if}
      {#if !noHeat}
        <Row>
          <Column class="border-r border-gray-400">{ $t('level2.failures1.heat') }</Column>
          <Column colspan={4}>
            <FailureMode modeOptions={$failureOptionsStore.modeOptions} bind:mode={failures1.failures[13]} bind:timer={failures1.failures[14]} showMinutes validation={validation.HeatTimer} />
          </Column>
        </Row>
      {/if}
      {#if !noCavity}
        <Row>
          <Column class="border-r border-gray-400">{ $t('level2.failures1.cavity-heater') }</Column>
          <Column colspan={4}>
            <FailureMode modeOptions={$failureOptionsStore.modeOptions} bind:mode={failures1.failures[15]} bind:timer={failures1.failures[16]} showMinutes validation={validation.CavityHeatTimer}/>
          </Column>
        </Row>
      {/if}
      {#if !noLights}
        <Row>
          <Column class="border-r border-gray-400">{ $t('level2.failures1.bay-lights-monitor') }</Column>
          <Column colspan={4}>
            <FailureMode modeOptions={$failureOptionsStore.modeLightOptions} bind:mode={failures1.failures[19]} bind:timer={failures1.failures[20]} validation={validation.LightsTimer}>
              <Select class="w-72" size="xl" bind:value={failures1.failures[21]} options={$failureOptionsStore.LightsOptions} {edit} />
            </FailureMode>
          </Column>
        </Row>
      {/if}
    </Table>
      <svelte:fragment slot="footer-center">
        <SaveButton {edit} bind:wait={wait} data={ failures1.failures } bind:original={original} route="failures1" bind:validation={validation} autoSave
          onSave={async (d: string[]) => {
            // Mirrors firmware NovaMsg_SendFailureSettings layout. All
            // uint32 force-emitted (Mode=0/Timer=0 are legitimate).
            const ints = {
              1:  parseInt(d[0],  10) || 0,  // FanMode
              2:  parseInt(d[1],  10) || 0,  // FanTimer
              3:  parseInt(d[13], 10) || 0,  // HeatMode
              4:  parseInt(d[14], 10) || 0,  // HeatTimer
              5:  parseInt(d[4],  10) || 0,  // RefrigMode
              6:  parseInt(d[5],  10) || 0,  // RefrigTimer
              7:  parseInt(d[6],  10) || 0,  // RefrigFailMode (run-in)
              8:  parseInt(d[17], 10) || 0,  // BurnerMode
              9:  parseInt(d[18], 10) || 0,  // BurnerTimer
              10: parseInt(d[10], 10) || 0,  // HumidTimer
              11: parseInt(d[3],  10) || 0,  // ClimacellTimer
              12: parseInt(d[19], 10) || 0,  // LightsMode
              13: parseInt(d[20], 10) || 0,  // LightsTimer
              14: parseInt(d[21], 10) || 0,  // LightsUnits (1=mins, 60=hours)
              15: parseInt(d[2],  10) || 0,  // ClimacellMode
              16: parseInt(d[7],  10) || 0,  // RefrigStagesMode
              17: parseInt(d[8],  10) || 0,  // RefrigStagesTimer
              18: parseInt(d[9],  10) || 0,  // HumidMode
              19: parseInt(d[11], 10) || 0,  // AuxMode
              20: parseInt(d[12], 10) || 0,  // AuxTimer
              21: parseInt(d[15], 10) || 0,  // CavityHeatMode
              22: parseInt(d[16], 10) || 0,  // CavityHeatTimer
            };
            await writeProto(TAG.FailureSettings, {
              fanMode: ints[1], fanTimer: ints[2], heatMode: ints[3],
              heatTimer: ints[4], refrigMode: ints[5], refrigTimer: ints[6],
              refrigFailMode: ints[7], burnerMode: ints[8],
              burnerTimer: ints[9], humidTimer: ints[10],
              climacellTimer: ints[11],
              lightsMode: ints[12], lightsTimer: ints[13],
              lightsUnits: ints[14],
              climacellMode: ints[15],
              refrigStagesMode: ints[16], refrigStagesTimer: ints[17],
              humidMode: ints[18],
              auxMode: ints[19], auxTimer: ints[20],
              cavityHeatMode: ints[21], cavityHeatTimer: ints[22],
            });
          }} />
      </svelte:fragment>
    </ScrollableArea>
  </Card>
</GellertPage>


