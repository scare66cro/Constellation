<script lang="ts">
  import GellertPage from '$lib/components/GellertPage.svelte';
  import { navigationStore, pidStore } from '$lib/store';

  import Card from '$lib/ui/Card.svelte';
  import { onMount } from 'svelte';
  import { format } from 'date-fns';
  import TextField from '$lib/ui/TextField.svelte';
  import Button from '$lib/ui/Button.svelte';
	import { goto } from '$app/navigation';
  import { t } from "svelte-i18n";
  import { getDate, loadData } from "$lib/business/util";
	import type { PidType } from '$lib/business/pidtype';

  let data = {};

  let ready = false;

  $: pids = Object.values(data).filter((item) => !Array.isArray(item)) as Array<PidType>;

  let title = `PID Log ${$pidStore.startLog} - ${$pidStore.endLog}`;
  const currentTime = format(new Date(), 'MM-dd-yyyy_HH-mm');
  let filename = `PIDLog_${currentTime}.csv`;

  function downloadFile() {
    ready = false;
    const csvContent = convertToCSV(pids);
    const blob = new Blob([csvContent], { type: 'text/csv' });
    const link = document.createElement('a');
    const blobUrl = window.URL.createObjectURL(blob);
    link.href = blobUrl;
    link.download = `${filename}`;
    link.click();
    
    // Clean up blob URL to free memory
    setTimeout(() => {
      window.URL.revokeObjectURL(blobUrl);
    }, 100);
    
    ready = true;
    goto($pidStore.returnPage);
  }

  function convertToCSV(data: Array<PidType>) {
    const header = ['Date', 'Time', 'Error', 'P', 'I', 'D', 'Output'].join(',');
    const rows = data.map(obj => `${getDate(obj.date)?.split(' ').join(',') ?? ','},${obj.Error},${obj.P},${obj.I},${obj.D},${obj.Output}`);
    return `${header}\n${rows.join('\n')}`;
  }

  onMount(async () => {
    data = await loadData();
    ready = true;
  });
</script>

<GellertPage {title} {ready} level={2} name="download">
  <Card class="flex flex-col text-size-xl container-standard">
    {#if pids.length > 0}
      <div class="flex justify-center w-full">
        <TextField size="xl" extended="w-3/4" class="w-full" bind:value={filename} edit={true}/>
      </div>
      <Button size="xl" class="mx-auto w-64" on:click={downloadFile}>{ $t('global.download') }</Button>
    {:else}
      <div class="flex flex-col items-center justify-center h-full">
        <span class="text-size-xl">{ $t('level2.table.no-data-available') }</span>
      </div>
    {/if}


  </Card>
</GellertPage>


