<script lang="ts">
	import Button from "$lib/ui/Button.svelte";
  import Wait from "$lib/ui/Wait.svelte";
	import { type DrawerSettings, getDrawerStore } from "@skeletonlabs/skeleton";
	import { onMount, onDestroy } from "svelte";
  import { mdiChartLine, mdiCogOutline, mdiTable } from "@mdi/js";
	import Icon from "$lib/ui/Icon.svelte";
	import Card from "$lib/ui/Card.svelte";
  import { buildSensorList } from "$lib/business/charting";
  import { fetchDisplayOptions } from "$lib/business/displayUtils";
  import { plotDataStore, datesStore, dataSelectionStore, keysStore, historyStore, frontMatterStore } from "$lib/store";
  import { format } from "date-fns";
  import { heightsStore } from "$lib/store";
  import Chart, { ChartData, createChartData, createSeries } from "$lib/components/Chart.svelte";
  import DataSelection from "$lib/components/DataSelection.svelte";
	import LogType from "$lib/components/LogType.svelte";
	import RangeSelection from "$lib/components/RangeSelection.svelte";
  import DownloadFile from "$lib/components/DownloadFile.svelte";
  import { goto } from "$app/navigation";
  import { getHttpUrl } from "$lib/business/util";
  import { plenumSettings, sensorList } from "$lib/business/protoStores";
  import { get } from "svelte/store";
	import VirtualList from "$lib/components/Virtual/VirtualList.svelte";
	import { safeJsonParse } from "$lib/business/util";
  import WsClient, { type DownloadProgress } from '$lib/business/wsClient';
	import Table from "$lib/ui/Table.svelte";
	import Row from "$lib/ui/Row.svelte";
	import Column from "$lib/ui/Column.svelte";

  const drawerStore = getDrawerStore();
  const drawerSettings: DrawerSettings = {
    id: 'UserLog',
    position: 'right',
    width: 'w-1/4',
  };
  let ready = true;
  let dataReady = false;
  let headerTable: HTMLDivElement;
  let tableHeight: number = 0;
  let zoom = '';
  let wsClient: WsClient | null = null;
  let downloadHasStarted = false;
  let getDisplays = true;
  $: progress = { current: undefined, total: undefined } as DownloadProgress;

  $: filename = '';
  $: wait = false;
  $: error = false;
  $: downloadFromBackup  = $historyStore.logType === 'Backup';
  $: displayOptions = [] as { text: string, value: string }[];
  $: height = $heightsStore.main - $heightsStore.header - $heightsStore.footer - ($historyStore.logType === 'Graph' ? 115 : 160);
  $: if ($historyStore.logType === 'Graph' && $plotDataStore && $datesStore) {
    updateChart($plotDataStore);
  }
  // When the Download step is shown, fetch display options for this log type
  $: if ($historyStore.showDownload && getDisplays) {
    getDisplays = false;
    progress = { current: undefined, total: undefined } as DownloadProgress;
    wait = true;
    error = false; // Reset error state while loading
    displayOptions = [];
    // Reset selection so DownloadFile can pick the first available option when options arrive
    $historyStore.display = '';
    
    fetchDisplayOptions('HistoryLog')
      .then((result) => {
        displayOptions = result.displays;
        filename = result.filename;
        
        if (!result.success) {
          console.warn('Display fetch completed with issues:', result.error);
          // Don't set error here - wait for finally block
        }
      })
      .catch((err) => {
        console.error('Critical error in display fetching:', err);
        displayOptions = [{ text: 'Default Display', value: 'default' }];
        filename = `HistoryLog_${format(new Date(), 'MM-dd-yyyy_HH-mm')}`;
        // Don't set error here - wait for finally block
      })
      .finally(() => {
        wait = false;
        // Only show error after loading is complete and we have no valid displays
        if (displayOptions.length === 0 || displayOptions[0].value === 'default' || displayOptions[0].value === 'none' || displayOptions[0].value === '') {
          error = true;
        }
      });
  }

  $: if (!$historyStore.showDownload) {
    getDisplays = true;
  }

  function openSettings() {
    drawerStore.open(drawerSettings);
  }

  import { getDate } from "$lib/business/util";

  // Format sensor value based on type - static pressure needs 2 decimal places
  function formatSensorValue(value: any, sensorValue: string): string {
    if (value === null || value === '' || value === undefined) return '--';
    if (typeof value === 'string') return value; // Mode or other string values
    
    // Static pressure sensors (spress prefix) should display 2 decimal places
    if (sensorValue.startsWith('spress')) {
      return Number(value).toFixed(2);
    }
    
    // All other numeric values display as-is (already formatted by backend)
    return String(value);
  }

  let chartData: ChartData

  function updateChart(data: any) {
    const keys = Object.keys(data);
    chartData = createChartData({}, [], zoom);
    keys.forEach((item: string) => {
      chartData.series.push(createSeries($dataSelectionStore.selections.find((sensor => sensor.value === item))?.label ?? '', data[item]));
    });
  }

  async function downloadFile() {
    wait = true;
    error = false;
    const url = new URL(getHttpUrl('/iot/user/download'));
    url.searchParams.append('fileName', filename);
    url.searchParams.append('start', format($historyStore.startDate, "MM/dd/yyyy"));
    url.searchParams.append('end', format($historyStore.endDate, "MM/dd/yyyy"));

    if (downloadFromBackup) {
      url.searchParams.append('backup', 'true');
      $historyStore.logType = '';
      progress = { current: 0, total: 0 };
      downloadHasStarted = false; // Reset flag here
      wsClient = new WsClient(getHttpUrl('/iot/ws'), 'download-data', (data) => {
        const incomingMsg = {...data as DownloadProgress};

        if (!downloadHasStarted && incomingMsg.current && incomingMsg.current > 0 && incomingMsg.total && incomingMsg.total > 0) {
          downloadHasStarted = true;
        }

        if (downloadHasStarted && incomingMsg.current === 0 && progress.total && progress.total > 0) {
          progress = { current: progress.total, total: progress.total };
        } else {
          progress = incomingMsg;
        }
      });
      wsClient.connect();
      const controller = new AbortController();
      const timeoutId = setTimeout(() => {
        controller.abort('Request timed out');
        if (wsClient) {
          wsClient.close(1001, 'Timeout');
          wsClient = null;
        }
        error = true;
        progress = { current: undefined, total: undefined };
      }, 600000);

      try {
        const resp = await fetch(url, { signal: controller.signal });
        if (resp.status !== 200) {
          error = true;
          if (wsClient) {
            wsClient.close();
            wsClient = null;
          }
          progress = { current: undefined, total: undefined };
        }
      } catch (fetchError) {
        console.error('Error during user log download initiation:', fetchError);
        error = true;
        if (wsClient) {
          wsClient.close();
          wsClient = null;
        }
        progress = { current: undefined, total: undefined };
      } finally {
        clearTimeout(timeoutId);
      }
    } else {
      try {
        const resp = await fetch(url);
        if (resp.status !== 200) {
          error = true;
        } else {
          // Save the CSV body to the user's browser downloads folder.
          // Bridge sets Content-Disposition: attachment; filename="…",
          // but `fetch()` won't honor that — we have to materialise the
          // blob and click a temporary anchor ourselves.
          const blob = await resp.blob();
          const objectUrl = URL.createObjectURL(blob);
          const a = document.createElement('a');
          a.href = objectUrl;
          a.download = filename || 'userlog.csv';
          document.body.appendChild(a);
          a.click();
          a.remove();
          URL.revokeObjectURL(objectUrl);
        }
      } catch (e) {
        error = true;
      }
    }

    $historyStore.showDownload = false;
    $historyStore.showLog = true;
    wait = false;
  }

  function handleLogType(e: CustomEvent) {
    $historyStore.logType = e.detail.logType;
    $historyStore.showLog = false;
    $historyStore.showRange = true;
    // Only reset dates when coming from log type selection (first time)
    if ($historyStore.inSequence) {
      $historyStore.startDate = $historyStore.endDate = new Date();
    }
  }

  onMount(async () => {
    if ($keysStore.accessLevel < 1) {
      goto("/history");
    }

    wsClient = new WsClient(getHttpUrl('/iot/ws'), 'download-data', (data) => {
        progress = {...data as DownloadProgress};
    });

    // Apr 2026: sensors come from the typed `sensorList` proto store
    // (derived from AnalogBoard frames) instead of `/iot/sensors/all`.
    try {
      const sensors = get(sensorList);
      // Pull the live `main` array from the global store (composite-fed).
      // Was: a third HTTP call to `/iot/frontmatter`; now zero-RTT since
      // `+layout.svelte` keeps `$frontMatterStore` warm via the typed
      // proto stream.
      const frontmatter = ($frontMatterStore?.main as string[]) ?? [];
      // Plenum setpoints (specifically humidSetpointRef at slot [2]) for
      // buildSensorList come from $plenumSettings (TAG 1) — was a /iot/plensetup
      // GET that returned the wrong shape (bare array, not {setpoints:...}).
      const plen = get(plenumSettings);
      const setPoints: string[] = plen ? [
        String(plen.tempSetpoint ?? 0),
        String(plen.humidSetpoint ?? 0),
        String(plen.humidSetpointRef ?? 0),
        String(plen.burnerTempSetpoint ?? 0),
        String(plen.burnerThreshold ?? 0),
      ] : [];
      $dataSelectionStore.selections = buildSensorList(false, frontmatter, setPoints, sensors);
      $dataSelectionStore.selected = [];
      $historyStore = {
        showLog: true, showRange: false, showDownload: false, showData: false, showMain: false, logType: '',
        startDate: new Date(), endDate: new Date(), start: '1', end: '200', type: 'User',
        inSequence: true, display: ''
      };
      dataReady = true;
    } catch {
      error = true;
    }
  });

  onDestroy(() => {
    if (wsClient) {
      wsClient.close();
      wsClient = null;
    }
  });
</script>

{#if ready}
  {#if $historyStore.showLog}
    <LogType
      on:logType={handleLogType}
      bind:error={error}
      bind:backup={downloadFromBackup}
    />
  {/if}
  {#if $historyStore.showRange}
    <RangeSelection {downloadFromBackup} />
  {/if}
  {#if $historyStore.showDownload}
    <DownloadFile
      bind:filename={filename} {displayOptions}
      on:download={downloadFile}
    />
  {/if}

  {#if $historyStore.showData}
    <DataSelection />
  {/if}
  {#if $historyStore.showMain}
    <div class="flex flex-1 flex-col h-full">
      <Card class="m-2 flex flex-col flex-1">
        <div class="w-full flex flex-col bg-gradient-to-b from-gray-300/50">
          <div class="flex flex-row items-center">
            <div class="w-1/3"></div>
            <div class="w-1/3 flex">
              <Button size="xl" class="mx-auto my-2" on:click={() => { 
                $historyStore.showMain = false; 
                $historyStore.showRange = true; 
                $historyStore.inSequence = false; 
              }}>
                {format($historyStore.startDate, "MM/dd/yyyy")} to {format($historyStore.endDate, "MM/dd/yyyy")}
              </Button>
            </div>
            <div class="w-1/3 flex">
              <Button class="ml-auto" on:click={() => $historyStore.logType = ($historyStore.logType === 'Table' ? 'Graph' : 'Table')} disabled={!dataReady}>
                {#if $historyStore.logType === 'Table'}
                  <Icon class="fill-white stroke-white" src={mdiChartLine} />
                {:else}
                  <Icon class="fill-white stroke-white" src={mdiTable} />
                {/if}
              </Button>
              <Button class="ml-8" on:click={openSettings} disabled={!dataReady}>
                <Icon class="fill-white stroke-white" src={mdiCogOutline}/>
              </Button>
            </div>
          </div>
        </div>
        {#if $historyStore.logType === 'Table'}
          <VirtualList
            height={height}
            itemCount={$datesStore.length}
            itemSize={48}
            on:horizontalScroll={(e) => headerTable.scrollLeft = e.detail.scrollLeft}
          >
            <div slot="header">
              <div class="overflow-x-hidden" bind:this={headerTable}>
                <Table class="!flex-none text-size-xl" style="table-layout: fixed" bind:height={tableHeight}>
                  <Row class="bg-primary-900">
                    <Column class="text-white w-[400px] border border-r border-gray-400 font-bold text-size">Date</Column>
                    {#each Object.keys($plotDataStore) as item}
                      <Column class="text-white {item === 'hsMode' ? 'w-[250px]' : 'w-[200px]'} border border-r border-gray-400 font-bold text-size">{$dataSelectionStore.selections.find((sensor => sensor.value === item))?.label}</Column>
                    {/each}
                    <Column class="flex-1 bg-gray-300 border-none"></Column>
                  </Row>
                </Table>
              </div>
            </div>
            <div slot="item" let:index let:style={style} style={style}>
              <div class="flex flex-row border border-gray-400 bg-primary-100">
                <div class="w-[400px] border-r border-gray-400 text-size-large text-center">{getDate($datesStore[index])}</div>
                {#each Object.keys($plotDataStore) as item}
                  <div class="{item === 'hsMode' ? 'w-[250px]' : 'w-[200px]'} border-r border-gray-400 text-size-large text-center p-2">
                    {formatSensorValue($plotDataStore[item][index], item)}
                    {#if ($plotDataStore[item][index] !== null) && ($plotDataStore[item][index] !== 'Off' && $plotDataStore[item][index] !== '')}
                      {$dataSelectionStore.selections.find((sensor => sensor.value === item))?.units}
                    {/if}
                  </div>
                {/each}
                <div class="flex-1"></div>
              </div>
            </div>
          </VirtualList>
        {:else if $historyStore.logType === 'Graph'}
          <Chart labels={$datesStore} {chartData} editing={true} id="chart" {height}/>
        {/if}
      </Card>
    </div>
  {/if}
{/if}

{#if wait}
<Wait show={wait} current={progress.current} total={progress.total} />
{/if}


