<script lang="ts">
  import Card from "$lib/ui/Card.svelte";
  import Button from "$lib/ui/Button.svelte";
  import Select from "$lib/ui/Select.svelte";
  import TextField from "$lib/ui/TextField.svelte";
  import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
  import { createEventDispatcher } from "svelte";
  import { t } from "svelte-i18n";
	import { historyStore } from "$lib/store";

  const dispatch = createEventDispatcher();

  export let filename: string;
  export let displayOptions: Array<{ text: string, value: string }>;
  
  // Set default display if not already set and options are available
  $: if (displayOptions.length > 0 && !$historyStore.display && displayOptions[0].value !== 'none' && displayOptions[0].value !== '') {
    $historyStore.display = displayOptions[0].value;
  }
  
  // Validate that we have valid options before allowing download
  $: canDownload = displayOptions.length > 0 && filename.trim().length > 0 && displayOptions[0].value !== 'none' && displayOptions[0].value !== '';
</script>

<div class="flex flex-1 flex-col h-full">
  <div class="w-full flex flex-col bg-gradient-to-b from-gray-300/50">
    <Card class="mx-auto w-3/4 flex flex-col mt-4">
      <div class="flex flex-row items-center justify-center text-size-xl">
        <span class="mr-2">{$t('level1.history.display')}:</span>
        <Select size="xl" extended="w-1/2" bind:value={$historyStore.display} options={displayOptions} edit={true} disabled={!canDownload}/>
      </div>
      <div class="flex flex-row items-center justify-center text-size-xl">
        <span class="mr-2">{$t('level1.history.file-name')}:</span>
        <TextField size="xl" extended="mr-2 w-1/2" bind:value={filename} edit={true} keyboardType={KeyboardTypes.Alpha}/>
      </div>
      <div class="flex flex-row mt-4 items-center">
        <Button class="mx-auto" size="xl" disabled={!canDownload} on:click={() => dispatch('download')}>{$t('global.download')}</Button>
      </div>
      {#if !canDownload && displayOptions.length > 0}
        <div class="text-center text-red-500 mt-2">
          No displays available. Please check your connection and try again.
        </div>
      {/if}
    </Card>
  </div>
</div>
