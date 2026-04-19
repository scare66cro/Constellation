<script lang="ts">
  import GellertPage from "$lib/components/GellertPage.svelte";
  import DownloadFile from "$lib/components/DownloadFile.svelte";
  import List from "$lib/ui/List.svelte";
  import Table from "$lib/ui/Table.svelte";
  import Row from "$lib/ui/Row.svelte";
  import Column from "$lib/ui/Column.svelte";
  import Button from "$lib/ui/Button.svelte";
  import Card from "$lib/ui/Card.svelte";
	import { format } from "date-fns";
	import { onMount } from "svelte";
	import { navigationStore } from "$lib/store";
  import { t } from "svelte-i18n";
  import { safeJsonParse, getHttpUrl } from "$lib/business/util";

  export let data: { array: any };

  let showMain = true;
  let showDownload = false;
  let showRestore = false;
  let wait = false;
  let displayOptions = [] as { text: string, value: string }[];
  let filename = '';
  let restoreIp = '';
  let getDisplays = true;

  $: fileRestoreError = false;
  $: display = data?.array?.LocalIpAdd?.[0]
  $: fileList = {} as Record<string, {name: string, records: string}>; // ip, filename, # of records
  $: ipList = [] as { text: string, value: string }[]; // display, value: ip
  $: if (showDownload && getDisplays) {
    getDisplays = false;
    wait = true;
    displayOptions = [];
    fetch(getHttpUrl(`/iot/displays`))
      .then(async (result) => {
        try {
          const displays = await safeJsonParse(result);
          for (let i = 0; i < displays?.data?.DisplayList?.length || 0; i += 5) {
            displayOptions.push({ text: `${displays?.data?.DisplayList[i] || ''} ${displays?.data?.DisplayList[i + 2] || ''}`, value: displays?.data?.DisplayList[i] || '' });
          }
          filename = `Settings_${format(new Date(), 'MM-dd-yyyy_HH-mm')}`
          displayOptions = displayOptions;
          display = display;
        } catch (error) {
          console.error("Error parsing displays data:", error);
          displayOptions = [];
        }
      })
      .finally(() => {
        wait = false;
      });
  }

  $: if (!showDownload) {
    getDisplays = true;
  }

  function settingsToFile() {
    showDownload = true;
    showMain = false;
  }

  async function settingsFromFile() {
    showRestore = true;
    showMain = false;
    wait = true;
    
    // Clear the list first
    ipList = [];
    fileList = {};
    
    const response = await fetch(getHttpUrl(`/iot/settings/files`));
    try {
      const data = (await safeJsonParse(response))?.DisplayList || [];
      const newIpList = [];
      const newFileList = {} as Record<string, {name: string, records: string}>;
      
      for (let i = 0; i < data.length; i += 4) {
        newIpList.push({ text: `${data[i + 1]} (${data[i]}) - ${data[i + 2]}`, value: data[i] });
        newFileList[data[i]] = { name: data[i + 2], records: data[i + 3] };
      }
      
      // Assign the new arrays to trigger reactivity
      ipList = newIpList;
      fileList = newFileList;
    } catch (error) {
      console.error("Error parsing settings files data:", error);
      ipList = [];
      fileList = {};
    }
    wait = false;
  }

  async function settingsToPanelDefault() {
    wait = true;
    const response = await fetch(getHttpUrl('/iot/button'), {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
      },
      body: JSON.stringify({
        tag: 'button2',
        SetDefault: 'Save',
      }),
		});
    try {
      const json = await safeJsonParse(response);
      if (typeof window !== 'undefined' && window.handleUnauthorized) {
        await window.handleUnauthorized(json);
      }
    } catch (error) {
      console.error("Error parsing panel default settings response:", error);
    }
    wait = false;
  }

  async function restoreToPanelDefault() {
    wait = true;
		const response = await fetch(getHttpUrl('/iot/button'), {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json'
      },
      body: JSON.stringify({
        tag: 'button2',
        PanelDefault: 'Restore',
      }),
		});
    try {
      const json = await safeJsonParse(response);
      if (typeof window !== 'undefined' && window.handleUnauthorized) {
        await window.handleUnauthorized(json);
      }
    } catch (error) {
      console.error("Error parsing panel default restore response:", error);
    }
    wait = false;
  }

  async function restoreToFactoryDefault() {
    wait = true;
    const response = await fetch(getHttpUrl('/iot/button'), {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
      },
      body: JSON.stringify({
        tag: 'button2',
        FactoryDefault: 'Restore',
      }),
		});
    try {
      const json = await safeJsonParse(response);
      if (typeof window !== 'undefined' && window.handleUnauthorized) {
        await window.handleUnauthorized(json);
      }
    } catch (error) {
      console.error("Error parsing factory default restore response:", error);
    }
    wait = false;
  }

  async function downloadPanelSettings() {
    wait = true;
    const response = await fetch(getHttpUrl('/iot/settings/download'), {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
      },
      body: JSON.stringify({ downloadSettings: 1, fileName: filename, selDisplay: display, btnUSBSettings: 'Download' }),
    });
    try {
      const json = await safeJsonParse(response);
      if (typeof window !== 'undefined' && window.handleUnauthorized) {
        await window.handleUnauthorized(json);
      }
    } catch (error) {
      console.error("Error parsing panel settings download response:", error);
    }
    showDownload = false;
    showMain = true;
    wait = false;
  }

  async function restore() {
    wait = true;
    fileRestoreError = false;
    const response = await fetch(getHttpUrl('/iot/settings/restore'), {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
      },
      body: JSON.stringify({
        display: restoreIp,
        filename: fileList[restoreIp].name,
        records: fileList[restoreIp].records,
      }),
    });
    try {
      const json = await safeJsonParse(response);
      if (typeof window !== 'undefined' && window.handleUnauthorized) {
        await window.handleUnauthorized(json);
      }
      if (json?.status !== 200) {
        fileRestoreError = true;
      }
    } catch (error) {
      console.error("Error parsing restore response:", error);
    }
    showRestore = false;
    showMain = true;
    wait = false;
  }

  onMount(async () => {
    $navigationStore.data = getHttpUrl(`/iot/network`);
  });
</script>
<GellertPage title="{ $t('level2.settings.save-restore-panel-settings') }" name="settings" ready={true} level={2} {wait}>
  {#if showMain}
    <Card class="mx-auto w-11/12 flex flex-col">
      <Table class="mb-2 text-size-xl">
        <Row>
          <Column class="w-2/3 items-center border-r border-gray-400">{ $t('level2.settings.download-current-system-settings-to-file') }</Column>
          <Column class="w-1/3 items-center"><Button class="w-48 xl:w-64" size="xl" on:click={settingsToFile}>{ $t('global.download') }</Button></Column>
        </Row>
        <Row>
          <Column class="w-2/3 items-center border-r border-gray-400">{ $t('level2.settings.restore-system-settings-from-file') }</Column>
          <Column class="w-1/3 items-center"><Button class="w-48 xl:w-64 {fileRestoreError ? '!variant-ghost-error' : ''}" size="xl" on:click={settingsFromFile}>{ fileRestoreError ? $t('global.retry') : $t('level2.settings.restore') }</Button></Column>
        </Row>
        <Row>
          <Column class="w-2/3 items-center border-r border-gray-400">{ $t('level2.settings.save-current-system-settings-as-panel-default') }</Column>
          <Column class="w-1/3 items-center"><Button class="w-48 xl:w-64" size="xl" on:click={settingsToPanelDefault}>{ $t('global.save') }</Button></Column>
        </Row>
        <Row>
          <Column class="w-2/3 items-center border-r border-gray-400">{ $t('level2.settings.restore-to-panel-default-settings') }</Column>
          <Column class="w-1/3 items-center"><Button class="w-48 xl:w-64" size="xl" on:click={restoreToPanelDefault}>{ $t('level2.settings.restore') }</Button></Column>
        </Row>
        <Row>
          <Column class="w-2/3 items-center border-r border-gray-400">{ $t('level2.settings.restore-to-factory-default-settings') }</Column>
          <Column class="w-1/3 items-center"><Button class="w-48 xl:w-64" size="xl" on:click={restoreToFactoryDefault}>{ $t('level2.settings.restore') }</Button></Column>
        </Row>
      </Table>
    </Card>
  {:else if showDownload}
    <DownloadFile {displayOptions} bind:filename={filename}
      on:download={downloadPanelSettings}
      on:back={() => { showDownload = false; showMain = true; }}
    />
  {:else if showRestore}
    <Card class="mx-auto w-3/4 flex flex-col">
      <div class="text-center text-size-xl mb-2">{ $t('level2.settings.available-saved-settings-files') }</div>
      <List class="w-full mb-2 text-size-xl" style="height: 280px; min-height: 280px;" bind:value={restoreIp} options={ipList} edit={true} size={5} />
      <div class="flex flex-row mb-2">
        <Button class="w-48 ml-auto mr-4" size="lg" on:click={() => { showRestore = false; showMain = true; }}>{ $t('global.back') }</Button>
        <Button class="w-48 mr-auto" size="lg" on:click={restore}>{$t('level2.settings.restore')}</Button>
      </div>
    </Card>
  {/if}
</GellertPage>


