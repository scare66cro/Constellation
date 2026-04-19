<script lang="ts">
	import { onMount } from "svelte";
  import GellertPage from "$lib/components/GellertPage.svelte";
	import Card from "$lib/ui/Card.svelte";
	import Button from "$lib/ui/Button.svelte";
	import { frontMatterStore, navigationStore } from "$lib/store";
  import { getHttpUrl } from "$lib/business/util";
	import Table from "$lib/ui/Table.svelte";
	import Row from "$lib/ui/Row.svelte";
  import Column from "$lib/ui/Column.svelte";
	import RunTime from "$lib/components/RunTime.svelte";
	import SaveButton from "$lib/components/SaveButton.svelte";
	import { cloneDeep, isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
	import type { ArrayResponse } from "$lib/business/util";
  import { FooterNavigationAdapter } from "$lib/utils/footerNavigationAdapter";

  export let data: ArrayResponse;

  let title = $t('level1.runclock.system-run-clock');

  $: ready = false;

  $: wait = false;

  $: edit = $navigationStore.level > 0;

  $: level = $navigationStore.level;

  $: currentRunOperation = '3'; // standby

  $: runtimes = [] as string[];

  $: onionMode = ($frontMatterStore?.panel as string[])?.[8] === '1';

  let operationColor: Record<string, string> = {
    '1': 'bg-green-500 !text-black',
    '2': 'bg-purple-500 !text-black',
    '3': 'bg-yellow-500 !text-black',
    '4': 'bg-blue-400 !text-black',
    '5': 'bg-orange-500 !text-black',
  };

  function getOperationColor(value: string, doHighLight: boolean): string {
    return operationColor[value] + (doHighLight ? ' ring-4 ring-white' : '');
  }

  onMount(async () => {
		try {
      $navigationStore.data = getHttpUrl('/iot/runtimes');
      $navigationStore.isDirty = () => !isEqual(runtimes, data.array);
      runtimes = cloneDeep(data.array);
		} catch (error) {
      console.error(error);
		}
		ready = true;
  });

  function selectAll() {
    runtimes = runtimes.map((_) => currentRunOperation);
  }

  function cooling() {
    currentRunOperation = '1';
  }

  function recirculation() {
    currentRunOperation = '2';
  }

  function standby() {
    currentRunOperation = '3';
  }

  function refrigeration() {
    currentRunOperation = '4';
  }

  function cure() {
    currentRunOperation = '5';
  }

  async function handleNextPage() {
    await FooterNavigationAdapter.navigateToNext(level, 'runclock');
  }

  async function handlePreviousPage() {
    await FooterNavigationAdapter.navigateToPrevious(level, 'runclock');
  }
</script>

<GellertPage {wait} {ready} {title} {level} name="runclock">
  <Card class="mx-auto mt-2 flex flex-col container-full">
    <Table class="mb-2 text-size-xl">
      <Row>
        <Column class="w-1/12 px-2" rowspan={2}>AM</Column>
        <Column class="w-11/12" rowspan={2}>
          <RunTime isAM={true} bind:runtimes={runtimes} {edit} {currentRunOperation} {getOperationColor} on:nextPage={handleNextPage} on:previousPage={handlePreviousPage}/>
        </Column>
      </Row>
    </Table>
    <Table class="text-size-xl">
      <Row>
        <Column class="w-1/12 px-2" rowspan={2}>PM</Column>
        <Column class="w-11/12" rowspan={2}>
          <RunTime isAM={false} bind:runtimes={runtimes} {edit} {currentRunOperation} {getOperationColor} on:nextPage={handleNextPage} on:previousPage={handlePreviousPage}/>
        </Column>
      </Row>
    </Table>
    {#if edit}
      <Button class="mx-auto !my-1 !{getOperationColor(currentRunOperation, false)}" size="xl" on:click={selectAll} {edit}>{ $t('level1.runclock.select-all') }</Button>
    {/if}
    <div class="flex flex-row text-center mx-auto my-1">
      {#if onionMode}
        <Button class="!my-1 !bg-yellow-500 !text-black {currentRunOperation === '3' && 'ring-4 ring-yellow-600'}" size="xl" on:click={standby} disabled={!edit}>{ $t('global.standby') }</Button>
        <Button class="ml-4 !my-1 !bg-orange-500 !text-black {currentRunOperation === '5'&& 'ring-4 ring-orange-700'}" size="xl" on:click={cure} disabled={!edit}>{ $t('global.cure') }</Button>
      {:else}
        <Button class="!my-1 !bg-green-500 !text-black {currentRunOperation === '1' && 'ring-4 ring-green-600'}" size="xl" on:click={cooling} disabled={!edit}>{ $t('global.cooling') }</Button>
        <Button class="ml-4 !my-1 !bg-purple-500 !text-black {currentRunOperation === '2' && 'ring-4 ring-purple-600'}" size="xl" on:click={recirculation} disabled={!edit}>{ $t('global.recirculation') }</Button>
        <Button class="ml-4 !my-1 !bg-yellow-500 !text-black {currentRunOperation === '3' && 'ring-4 ring-yellow-600'}" size="xl" on:click={standby} disabled={!edit}>{ $t('global.standby') }</Button>
        <Button class="ml-4 !my-1 !bg-blue-400 !text-black {currentRunOperation === '4' && 'ring-4 ring-blue-500'}" size="xl" on:click={refrigeration} disabled={!edit}>{ $t('global.refrigeration') }</Button>
      {/if}
    </div>
    <SaveButton {edit} bind:wait={wait} data={runtimes} bind:original={data.array} route="runtimes" autoSave />
  </Card>
</GellertPage>


