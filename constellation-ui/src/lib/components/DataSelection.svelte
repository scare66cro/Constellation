<script lang="ts">
  import Button from "$lib/ui/Button.svelte";
  import Card from "$lib/ui/Card.svelte";
  import { dataSelectionStore, historyStore } from "$lib/store";
  import { toggleSelection, clearCacheForType } from "$lib/business/charting";
  import { heightsStore } from "$lib/store";
  import { t } from "svelte-i18n";
  import { onMount } from "svelte";
  import { loadGraphFavorites, saveGraphFavorites } from "$lib/business/graphFavorites";
	import ScrollableArea from "./ScrollableArea.svelte";

  $: height = $heightsStore.main - $heightsStore.header - $heightsStore.footer - 55;
  $: columnLength = Math.floor(($dataSelectionStore.selections.length + 2) / 3);
  
  let currentLogType = '';
  
  // Track favorites operation status
  let savingFavorites = false;
  let loadingFavorites = false;
  
  // Watch for changes in log type and clear cache accordingly
  $: if ($historyStore.type !== currentLogType && currentLogType !== '') {
    clearCacheForType($historyStore.type);
    currentLogType = $historyStore.type;
  }
  
  async function handleSaveFavorites() {
    savingFavorites = true;
    try {
      await saveGraphFavorites(dataSelectionStore);
    } finally {
      savingFavorites = false;
    }
  }
  
  async function handleLoadFavorites() {
    loadingFavorites = true;
    try {
      await loadGraphFavorites(dataSelectionStore);
    } finally {
      loadingFavorites = false;
    }
  }
  
  onMount(() => {
    currentLogType = $historyStore.type;
  });
</script>

<div class="flex flex-1 flex-col h-full">
  <div class="w-full flex flex-col bg-gradient-to-b from-gray-300/50">
    <Card class="mx-2 mt-2 flex flex-col" {height}>
      <div class="flex flex-row font-bold text-size-xl mb-3 mx-auto">{$t('level1.history.data-selection')}</div>
      
      <ScrollableArea>
        {#each $dataSelectionStore.selections.slice(0, columnLength) as item, index}
          <div class="flex flex-row items-center">
            <Button class="mx-2 !my-1 w-1/3 {$dataSelectionStore.selected[index] ? '!bg-primary-500 hover:!bg-primary-600 focus:!bg-primary-800 focus:hover:!bg-primary-700 text-white' : 'focus:!bg-primary-500'}" size="xl" tabindex="-1" noFocus={true} on:click={() => toggleSelection(index)}>{item.label}</Button>
            <Button
              class="mx-2 !my-1 w-1/3 {$dataSelectionStore.selected[index + columnLength] ? '!bg-primary-500 hover:!bg-primary-600 focus:!bg-primary-800 focus:hover:!bg-primary-700 text-white' : 'focus:!bg-primary-500'}"
              size="xl"
              tabindex="-1"
              noFocus={true}
              on:click={() => toggleSelection(index + columnLength)}
            >
              {$dataSelectionStore.selections[index + columnLength].label}
            </Button>
            {#if index + columnLength * 2 < $dataSelectionStore.selections.length}
              <Button
                class="mx-2 !my-1 w-1/3 {$dataSelectionStore.selected[index + columnLength * 2] ? '!bg-primary-500 hover:!bg-primary-600 focus:!bg-primary-800 focus:hover:!bg-primary-700 text-white' : 'focus:!bg-primary-500'}"
                size="xl"
                tabindex="-1"
                noFocus={true}
                on:click={() => toggleSelection(index + columnLength * 2)}
              >
                {$dataSelectionStore.selections[index + columnLength * 2].label}
              </Button>
            {:else}
              <div class="mx-2 !my-1 w-1/3"></div>
            {/if}
          </div>
        {/each}
      </ScrollableArea>
      
      <!-- Favorites buttons -->
      <div class="flex flex-row justify-center gap-4 mt-3">
        <Button 
          size="xl" 
          disabled={loadingFavorites}
          on:click={handleLoadFavorites}
        >
          {loadingFavorites ? $t('global.loading') : $t('level1.history.load-favorites')}
        </Button>
        
        <Button 
          size="xl" 
          disabled={savingFavorites}
          on:click={handleSaveFavorites}
        >
          {savingFavorites ? $t('global.saving') : $t('level1.history.save-favorites')}
        </Button>
      </div>
    </Card>
  </div>
</div>