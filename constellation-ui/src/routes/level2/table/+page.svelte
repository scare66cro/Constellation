<script lang="ts">
	import GellertPage from '$lib/components/GellertPage.svelte';
  import { dataSelectionStore, navigationStore, pidStore } from '$lib/store';

	import Card from '$lib/ui/Card.svelte';
	import Column from '$lib/ui/Column.svelte';
	import Row from '$lib/ui/Row.svelte';
	import Table from '$lib/ui/Table.svelte';
	import { onMount } from 'svelte';
  import { heightsStore } from '$lib/store';
  import { t } from "svelte-i18n";
  import { getDate, loadData } from "$lib/business/util";
	import type { PidType } from '$lib/business/pidtype';
	import VirtualList from '$lib/components/Virtual/VirtualList.svelte';

  let ready = false;
  let data = {};

  onMount(async () => {
    data = await loadData();
    ready = true;
  });

  let tableHeight: number = 0;

  $: height = $heightsStore.main - $heightsStore.header - $heightsStore.footer - 115;
  $: pids = data ? Object.values(data).filter((item) => !Array.isArray(item)) as Array<PidType> : [];

  let title = `${$t('global.pid-log')} ${$pidStore.startLog} - ${$pidStore.endLog}`;
</script>

<div class="w-full flex flex-col bg-gradient-to-b from-gray-300/50">
  <div class="flex flex-row">
    <div class="w-1/4 text-size-xl -mt-4 ml-2 text-left"></div>
    <div class="w-1/2 text-size-xl font-bold -mt-4 text-center">{title}</div>
    <div class="w-1/4 -mt-4 text-center mr-2 flex justify-end items-center gap-4"></div>
  </div>
</div>
<Card class="m-2 flex flex-col flex-1">
  {#if pids.length > 0}
  <VirtualList {height} itemCount={pids.length} itemSize={48}>
    <div slot="header">
      <Table class="!flex-none !shadow-none text-xl" style="table-layout: fixed; width: 100%; border-collapse: collapse;" bind:height={tableHeight}>
        <Row class="bg-primary-900 text-white h-12 !border-0">
          <Column class="w-4/12 border-r border-gray-400 font-bold py-0">{ $t('global.date') }</Column>
          <Column class="w-1/12 border-r border-gray-400 font-bold py-0">{ $t('level2.table.error') }</Column>
          <Column class="w-2/12 border-r border-gray-400 font-bold py-0">P</Column>
          <Column class="w-2/12 border-r border-gray-400 font-bold py-0">I</Column>
          <Column class="w-2/12 border-r border-gray-400 font-bold py-0">D</Column>
          <Column class="w-1/12 font-bold py-0">{ $t('global.output') }</Column>
        </Row>
      </Table>
    </div>
    <div slot="item" let:index let:style={style} style={style}>
      {#if pids[index]}
        <Table class="!flex-none !shadow-none text-size-large" style="table-layout: fixed; width: 100%; border-collapse: collapse;">
          <Row class="bg-primary-100 h-12 border-y border-gray-400">
            <Column class="w-4/12 border-r border-gray-400 py-0">{getDate(pids[index].date)}</Column>
            <Column class="w-1/12 border-r border-gray-400 py-0">{pids[index].Error}</Column>
            <Column class="w-2/12 border-r border-gray-400 py-0">{pids[index].P}</Column>
            <Column class="w-2/12 border-r border-gray-400 py-0">{pids[index].I}</Column>
            <Column class="w-2/12 border-r border-gray-400 py-0">{pids[index].D}</Column>
            <Column class="w-1/12 py-0">{pids[index].Output}</Column>
          </Row>
        </Table>
      {/if}
    </div>
  </VirtualList>
  {:else}
    <div class="flex flex-col items-center justify-center h-full text-size-xl">
      { $t('level2.table.no-data-available') }
    </div>
  {/if}
</Card>
