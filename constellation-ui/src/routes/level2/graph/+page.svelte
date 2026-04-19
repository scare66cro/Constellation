<script lang="ts">
	import GellertPage from '$lib/components/GellertPage.svelte';
  import { dataSelectionStore, navigationStore, pidStore } from '$lib/store';
  import { getHttpUrl } from "$lib/business/util";
	import Card from '$lib/ui/Card.svelte';
	import { onMount } from 'svelte';
	import Chart, { ChartData, createChartData, createSeries } from '$lib/components/Chart.svelte';
  import { t } from "svelte-i18n";
  import { getDate, loadData } from "$lib/business/util";
	import type { PidType } from '$lib/business/pidtype.js';
  import { heightsStore } from '$lib/store';

  let ready = false;
  let edit = true;
  let zoom = '';
  let pid: PidType[] = [];
  let data = {};

  $: {
    pid = Object.values(data);
    updateChart(pid);
  }
  $: height = $heightsStore.main - $heightsStore.header - $heightsStore.footer - 55;


  let title = `${$t('global.pid-log')} ${$pidStore.startLog} - ${$pidStore.endLog}`;
  let labels: string[] = [];
  let chartData: ChartData

  function updateChart(data: Array<{date: string, P: number, I: number, D: number, Output: number, Error: number}>) {
    const date: string[] = [];
    const error: number[] = [];
    const p: number[] = [];
    const i: number[] = [];
    const d: number[] = [];
    const output: number[] = [];
    data.forEach((item) => {
      date.push(getDate(item.date) ?? '04/06/1830 09:00:00');
      error.push(item.Error);
      p.push(item.P);
      i.push(item.I);
      d.push(item.D);
      output.push(item.Output);
    });
    chartData = createChartData({}, ['Error', 'P', 'I', 'D', 'Output'], zoom);
    chartData.series.push(createSeries('Error', error));
    chartData.series.push(createSeries('P', p));
    chartData.series.push(createSeries('I', i));
    chartData.series.push(createSeries('D', d));
    chartData.series.push(createSeries('Output', output));
    labels = date;
  }

  onMount(async () => {
    $navigationStore.data = getHttpUrl(`/iot/pids`);
    data = await loadData();
    ready = true;
  });
</script>

<GellertPage {title} {ready} level={2} name="graph">
  <Card class="mx-2 flex flex-col">
    {#if labels.length > 0}
      <Chart {labels} {chartData} editing={edit} id={`PID:${$pidStore.type}`} {height}/>
    {:else}
      <div class="flex justify-center items-center h-[calc(100%-8px)]">
        <div class="text-size-xl">{ $t('level2.graph.no-data') }</div>
      </div>
    {/if}
  </Card>
</GellertPage>


