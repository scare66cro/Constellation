<script lang="ts">
	import GellertPage from "$lib/components/GellertPage.svelte";
	import OnOff from "$lib/components/OnOff.svelte";
	import Card from "$lib/ui/Card.svelte";
	import Column from "$lib/ui/Column.svelte";
	import Row from "$lib/ui/Row.svelte";
	import Table from "$lib/ui/Table.svelte";
	import { onMount } from "svelte";
  import PIDU from "$lib/components/PIDU.svelte";
	import Select from "$lib/ui/Select.svelte";
	import TextField from "$lib/ui/TextField.svelte";
	import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
	import { AdornmentType } from "$lib/business/adornmentType";
	import Button from "$lib/ui/Button.svelte";
	import { goto } from "$app/navigation";
	import { pidStore, frontMatterStore, navigationStore } from "$lib/store";
  import { getHttpUrl } from "$lib/business/util";
	import SaveButton from "$lib/components/SaveButton.svelte";
  import { cloneDeep, isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
	import type { ArrayResponse } from "$lib/business/util";
  import TritonScadaSection from "./TritonScadaSection.svelte";
  import RefrigerantPTChart from "./RefrigerantPTChart.svelte";

  export let data: ArrayResponse & { tritons?: { slot: number; connected: boolean; label: string }[] };

  let title = $t('level2.refrigeration.refrigeration-setup');
  let edit = true;
  /** Refrigerant code of the currently-selected Triton tab; piped through to
   *  the P-T chart so it tracks the operator's view. */
  let activeRefrigerantType: number | undefined = undefined;
  /** Live values forwarded from the active Triton to the P-T chart so the
   *  optimal-pressure widget shows the unit's actual operating context. */
  let activeAmbientF: number | undefined = undefined;
  let activeCutInP:  number | undefined = undefined;
  let activeCutOutP: number | undefined = undefined;
  let activeDischargeTargetP: number | undefined = undefined;

  let refrigerationOptions = [
    { text: $t('level2.refrigeration.normal'), value: '0' },
    { text: $t('level2.refrigeration.pump-down'), value: '1' },
  ];

  const refrigIndex = [0, 2, 4, 6, 8, 10, 19, 21];

  let validation: Record<string, string> = {
    Stage1On: '',
    Stage1Off: '',
    Stage2On: '',
    Stage2Off: '',
    Stage3On: '',
    Stage3Off: '',
    Stage4On: '',
    Stage4Off: '',
    Stage5On: '',
    Stage5Off: '',
    Stage6On: '',
    Stage6Off: '',
    Stage7On: '',
    Stage7Off: '',
    Stage8On: '',
    Stage8Off: '',
    PRefrValue: '',
    IRefrValue: '',
    DRefrValue: '',
    URefrValue: '',
    PurgeThreshold: '',
  };

  $: ready = false;
  $: wait = false;
  $: refrigeration = [] as string[];
  $: refrigData = $frontMatterStore?.refrigData as string[];
  $: available = refrigIndex.map((index, i) => {
    if (refrigData?.[i] !== '-1') {
      return index;
    }
  })?.filter((i) => i !== undefined) as number[];
  $: remaining = available ? available.length % 2 : 0;

  onMount(async () => {
    try {
      $navigationStore.data = getHttpUrl(`/iot/refrigeration`);
      $navigationStore.isDirty = () => !isEqual(refrigeration, data.array);
      refrigeration = cloneDeep(data.array);
    } catch (error) {
      console.error(error);
    }
    ready = true;
  });

  function gotoLogs() {
    $pidStore.returnPage = '/level2/refrigeration';
    goto('/level2/pid');
  }
</script>

<GellertPage {wait} {title} {ready} level={2} name="refrigeration">
  <!--
    Legacy stages / PID / purge-threshold UI.
    Renders only when at least one refrigeration stage is configured.  When no
    stages are configured AND a Triton orbit is connected, the page collapses
    to just the SCADA section below.  When neither stages nor a Triton are
    present we still render the (empty) legacy form so the user has an entry
    point to configure stages.
  -->
  {#if available.length > 0 || (data.tritons?.length ?? 0) === 0}
  <Card class="mx-2 flex flex-col">
    <div class="flex flex-row text-size-xl">
      <Table class="mb-2 mr-1">
        {#each available.slice(0, Math.floor(available.length / 2) + remaining) as index, i}
          <Row>
            <Column class="w-3/12 border-r border-gray-400">{ $t('level2.refrigeration.stage') } {i + 1}</Column>
            <Column class="w-9/12">
              <OnOff bind:on={refrigeration[index]} bind:off={refrigeration[index + 1]} validationOn={validation[`Stage${i + 1}On`]} validationOff={validation[`Stage${i + 1}Off`]} />
            </Column>
          </Row>
        {/each}
      </Table>
      <Table class="mb-2 ml-1">
        {#each available.slice(Math.floor(available.length / 2) + remaining) as index, i}
          <Row>
            <Column class="w-3/12 border-r border-gray-400">{ $t('level2.refrigeration.stage') } {i + 1 + Math.floor(available.length / 2) + remaining}</Column>
            <Column class="w-9/12">
              <OnOff bind:on={refrigeration[index]} bind:off={refrigeration[index + 1]} validationOn={validation[`Stage${i + 1}On`]} validationOff={validation[`Stage${i + 1}Off`]}/>
            </Column>
          </Row>
        {/each}
      </Table>
    </div>
    <Table class="text-size-xl">
      <Row>
        <Column class="w-1/3 border-r border-gray-400">{ $t('global.pidu-values') }</Column>
        <Column class="w-2/3">
          <PIDU bind:p={refrigeration[12]} bind:i={refrigeration[13]} bind:d={refrigeration[14]} bind:u={refrigeration[15]} pvalid={validation.PRefrValue} ivalid={validation.IRefrValue} dvalid={validation.DRefrValue} uvalid={validation.URefrValue} />
        </Column>
      </Row>
      <Row>
        <Column colspan={2}>
          <p class="text-center mx-auto my-1 text-size-large">
            { $t('level2.refrigeration.refrigeration-purge-mode-is') }
            <span class="italic text-gray-500">→ <a href="/level1/co2" class="underline">CO<sub>2</sub> {$t('level1.co2.level-purge-control')}</a></span>
          </p>
        </Column>
      </Row>
      <Row>
        <Column colspan={2}>
          <p class="text-center mx-4 text-size-large">
            { $t('global.pid-controller-output-logging-options') }:
            <Button size="lg" on:click={gotoLogs} class="ml-2">{ $t('global.logs') }</Button>
          </p>
        </Column>
      </Row>
    </Table>
    <SaveButton {edit} bind:wait={wait} data={refrigeration} bind:original={data.array} route="refrigeration" bind:validation={validation} autoSave />
  </Card>
  {/if}

  <TritonScadaSection tritons={data.tritons ?? []}
                      bind:activeRefrigerantType={activeRefrigerantType}
                      bind:activeAmbientF={activeAmbientF}
                      bind:activeCutInP={activeCutInP}
                      bind:activeCutOutP={activeCutOutP}
                      bind:activeDischargeTargetP={activeDischargeTargetP} />
  <RefrigerantPTChart selected={activeRefrigerantType}
                      ambientF={activeAmbientF}
                      cutInP={activeCutInP}
                      cutOutP={activeCutOutP}
                      dischargeTargetP={activeDischargeTargetP} />
</GellertPage>


