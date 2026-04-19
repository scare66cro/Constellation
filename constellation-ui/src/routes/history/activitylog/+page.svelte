<script lang="ts">
	import Button from "$lib/ui/Button.svelte";
  import Wait from "$lib/ui/Wait.svelte";
	import { type DrawerSettings, getDrawerStore } from "@skeletonlabs/skeleton";
	import { onDestroy, onMount } from "svelte";
  import { mdiCogOutline } from "@mdi/js";
	import Icon from "$lib/ui/Icon.svelte";
	import Card from "$lib/ui/Card.svelte";
  import Table from "$lib/ui/Table.svelte";
  import Row from "$lib/ui/Row.svelte";
  import Column from "$lib/ui/Column.svelte";
  import { buildSensorList } from "$lib/business/charting";
  import { fetchDisplayOptions } from "$lib/business/displayUtils";
	import { plotDataStore, datesStore, dataSelectionStore, keysStore, equipListStore, remoteListStore, historyStore } from "$lib/store";
  import { format } from "date-fns";
  import { heightsStore } from "$lib/store";
  import DataSelection from "$lib/components/DataSelection.svelte";
  import DownloadFile from "$lib/components/DownloadFile.svelte";
	import LogType from "$lib/components/LogType.svelte";
	import { goto } from "$app/navigation";
  import { getHttpUrl } from "$lib/business/util";
	import TextField from "$lib/ui/TextField.svelte";
	import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
  import { t } from "svelte-i18n";
  import VirtualList from "$lib/components/Virtual/VirtualList.svelte";
	import { safeJsonParse } from "$lib/business/util";
  import WsClient, { type DownloadProgress } from "$lib/business/wsClient";
  
  const drawerStore = getDrawerStore();
  const drawerSettings: DrawerSettings = {
    id: 'UserLog',
    position: 'right',
    width: 'w-1/4',
  };
  let ready = true;
  let wait = false;
  let dataReady = false;
  let headerTable: HTMLDivElement;
  let tableHeight: number = 0;
  let wsClient: WsClient | null = null;
  let downloadHasStarted = false;
  let getDisplays = true;
  $: progress = { current: undefined, total: undefined } as DownloadProgress;

  $: filename = '';
  $: rowSize = 48 as number | number[];

  $: {
    const baseRowHeight = 48;
    const warningLineHeight = 64;

    if ($datesStore.length > 0) {
      rowSize = Array.from({ length: $datesStore.length }, (_, index) => {
        const warnEntry = $plotDataStore['warn']?.[index];
        if (!warnEntry) {
          return baseRowHeight;
        }

        const warnings = typeof warnEntry === 'string' ? warnEntry.split(',').filter(Boolean) : [];
        return baseRowHeight + warnings.length * warningLineHeight;
      });
    } else {
      rowSize = baseRowHeight;
    }
  }

  $: error = false;
  $: downloadFromBackup = $historyStore.logType === 'Backup';
  $: displayOptions = [] as { text: string, value: string }[];
  $: height = $heightsStore.main - $heightsStore.header - $heightsStore.footer - 160;
  $: if ($historyStore.showDownload && getDisplays) {
    getDisplays = false;
    progress = { current: undefined, total: undefined } as DownloadProgress;
    wait = true;
    error = false; // Reset error state while loading
    displayOptions = [];
    
    fetchDisplayOptions('ActivityLog')
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
        filename = `ActivityLog_${format(new Date(), 'MM-dd-yyyy_HH-mm')}`;
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

  onMount(async () => {
    downloadFromBackup = false;
    if ($keysStore.accessLevel < 1) {
      goto("/history");
    }
    Promise.all([
      fetch(getHttpUrl('/iot/plensetup')),
      fetch(getHttpUrl('/iot/frontmatter')),
      fetch(getHttpUrl('/iot/avail/equipment')),
      fetch(getHttpUrl('/iot/avail/remote')),
      fetch(getHttpUrl('/iot/sensors/all')),
    ]).then(async (responses) => {
      const plen = await safeJsonParse(responses[0]);
      const frontmatter = (await safeJsonParse(responses[1])).main;
      $equipListStore = await safeJsonParse(responses[2]);
      $remoteListStore = await safeJsonParse(responses[3]);
      const sensors = await safeJsonParse(responses[4]);
      const setPoints = plen.setpoints;
      $dataSelectionStore.selections = buildSensorList(true, frontmatter, setPoints, sensors);
      $dataSelectionStore.selected = [];
      $historyStore = {
        showLog: true, showRange: false, showDownload: false, showData: false, showMain: false, logType: '',
        startDate: new Date(), endDate: new Date(), start: '1', end: '200', type: 'Activity',
        inSequence: true, display: ''
      };
      dataReady = true;
    })
    .catch(() => {
      dataReady = false;
      error = true;
    });
  });

  function getArray(data: number | number[] | string): number[] | string[] {
    if (Array.isArray(data)) {
      return data;
    } else {
      return [];
    }
  }

  function getString(data: number | number[] | string): string {
    if (typeof data === 'string') {
      return data;
    } else {
      return '';
    }
  }

  async function downloadFile() {
    wait = true;
    error = false;
    const url = new URL(getHttpUrl('/iot/activity/download'));
    url.searchParams.append('fileName', filename);
    url.searchParams.append('start', $historyStore.start);
    url.searchParams.append('end', $historyStore.end);

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
      }, 600000); // 10 minutes

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
        console.error('Error during download initiation:', fetchError);
        error = true;
        if (wsClient) {
          wsClient.close();
          wsClient = null;
        }
        progress = { current: undefined, total: undefined };
      } finally {
        clearTimeout(timeoutId);
      }
    } else { // Non-backup download
      try {
        const resp = await fetch(url);
        if (resp.status !== 200) {
          error = true;
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
    $historyStore.startDate = $historyStore.endDate = new Date();
  }

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
      log="Activity"
      on:logType={handleLogType}
      bind:error={error}
      bind:backup={downloadFromBackup}
    />
  {/if}
  {#if $historyStore.showRange}
    <div class="flex flex-1 flex-col h-full">
      <div class="w-full flex flex-col bg-gradient-to-b from-gray-300/50">
        <Card class="mx-auto w-3/4 flex flex-col mt-4">
          <div class="flex flex-row items-center justify-center text-size-xl">
            <span class="mr-2">{$t('level1.history.records-to-display-or-download')}:</span>
            <TextField class="mr-2 w-36" size="xl" bind:value={$historyStore.start} edit={true} keyboardType={KeyboardTypes.Numeric}/> - <TextField class="ml-2 w-36" size="xl"bind:value={$historyStore.end} edit={true} keyboardType={KeyboardTypes.Numeric}/>
          </div>
        </Card>
      </div>
    </div>
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
              <Button class="mx-auto my-2" size="lg" on:click={() => { $historyStore.showMain = false; $historyStore.showRange = true; $historyStore.inSequence = false; }}>
                Records: {$historyStore.start} to {$historyStore.end}
              </Button>
            </div>
            <div class="w-1/3 flex">
              <Button class="ml-auto" size="lg" on:click={openSettings} disabled={!dataReady}>
                <Icon class="fill-white stroke-white" src={mdiCogOutline}/>
              </Button>
            </div>
          </div>
        </div>
        {#if $historyStore.logType === 'Table'}
          <VirtualList
            height={height}
            itemCount={$datesStore.length}
            itemSize={rowSize}
            on:horizontalScroll={(e) => {
              headerTable.scrollLeft = e.detail.scrollLeft;
            }}
          >
            <div slot="header">
              <div class="overflow-x-hidden" bind:this={headerTable}>
                <Table class="!flex-none text-size-xl" style="table-layout: fixed" bind:height={tableHeight}>
                  <Row class="bg-primary-900">
                    <Column class="text-white w-[400px] border-r border-gray-400 font-bold text-size">Date</Column>
                    {#each Object.keys($plotDataStore) as item}
                      {#if item !== 'warn' && item !== 'equip' && item !== 'remote' && item !== 'mode'}
                        <Column class="text-white {item === 'hsMode' ? 'w-[250px]' : 'w-[200px]'} border-r border-gray-400 font-bold text-size">{$dataSelectionStore.selections.find((sensor => sensor.value === item))?.label}</Column>
                      {/if}
                    {/each}
                    {#each Object.keys($plotDataStore) as item}
                      {#if item === 'equip'}
                        {#each $equipListStore.availLabels as label}
                          <Column class="text-white w-[150px] border-r border-gray-400 font-bold text-xl">{label}</Column>
                        {/each}
                      {:else if item === 'remote'}
                        {#each $remoteListStore.availLabels as label}
                          <Column class="text-white w-[150px] border-r border-gray-400 font-bold text-xl">{label}</Column>
                        {/each}
                      {/if}
                    {/each}
                    <Column class="flex-1 bg-gray-300 border-none"></Column>
                  </Row>
                </Table>
              </div>
            </div>
            <div slot="item" let:index let:style={style} style={style}>
              <div class="flex flex-row border border-gray-400 bg-primary-100">
                <div class="w-[400px] border-r border-gray-400 text-size-large text-center p-2">
                  {getDate($datesStore[index])}
                </div>
                {#each Object.keys($plotDataStore) as item}
                  {#if item !== 'warn' && item !== 'equip' && item !== 'remote'}
                    <div class="{item === 'hsMode' ? 'w-[250px]' : 'w-[200px]'} border-r border-gray-400 text-size-large text-center p-2">
                      {($plotDataStore[item][index] && $plotDataStore[item][index] !== '') ? $plotDataStore[item][index] : '--'}
                      {#if ($plotDataStore[item][index] !== null && $plotDataStore[item][index] !== 'Off' && $plotDataStore[item][index] !== '')}
                        {$dataSelectionStore.selections.find((sensor => sensor.value === item))?.units}
                      {/if}
                    </div>
                  {/if}
                {/each}
                {#each Object.keys($plotDataStore) as item}
                  {#if item === 'equip'}
                    {#each getArray($plotDataStore[item][index]) as value}
                      <div class="w-[150px] border-r border-gray-400 text-size-large text-center p-2">
                        {value}
                      </div>
                    {/each}
                  {:else if item === 'remote'}
                    {#each getArray($plotDataStore['remote'][index]) as value}
                      <div class="w-[150px] border-r border-gray-400 text-size-large text-center p-2">
                        {value}
                      </div>
                    {/each}
                  {/if}
                {/each}
                <div class="flex-1"></div>
              </div>
              {#if $plotDataStore['warn']?.[index]}
                <div class="flex flex-row border border-gray-400 bg-primary-100 leading-8">
                  {#each getString($plotDataStore['warn'][index]).split(',') as warning}
                    <div class="ml-10 text-size-large p-2">
                      {warning.split('=')[1]}
                    </div>
                  {/each}
                </div>
              {/if}
            </div>
          </VirtualList>
        {/if}
      </Card>
    </div>
  {/if}
{/if}

{#if wait}
  <Wait show={wait} current={progress.current} total={progress.total} />
{/if}


