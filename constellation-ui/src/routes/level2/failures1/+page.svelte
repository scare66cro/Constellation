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
  import { getHttpUrl } from "$lib/business/util";
  import { cloneDeep, isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
  import ScrollableArea from "$lib/components/ScrollableArea.svelte";

  type Failures = {
    InputConfig: string[],
    OutputConfig: string[],
    PwmConfig: string[],
    systemMode: string,
    boardType: string,
    controllerVersion: string,
    failures: string[],
  };

  export let data: Failures;

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
    noBurner = failures1.InputConfig[6] === '-1' && failures1.PwmConfig[3].split(':')[2] === '-1';
    onionMode = failures1.systemMode === '1';
  };

  onMount(async () => {
		try {
      $navigationStore.data = getHttpUrl(`/iot/failures1`);
      $navigationStore.isDirty = () => !isEqual(failures1.failures, data.failures);
      failures1 = cloneDeep(data);
		} catch (error) {
      console.error(error);
		}
		ready = true;
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
        <SaveButton {edit} bind:wait={wait} data={ failures1.failures } bind:original={data.failures} route="failures1" bind:validation={validation} autoSave/>
      </svelte:fragment>
    </ScrollableArea>
  </Card>
</GellertPage>


