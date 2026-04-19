<script lang="ts">
  import { createEventDispatcher } from "svelte";
	import Button from "$lib/ui/Button.svelte";
	import Card from "$lib/ui/Card.svelte";
	import { goto } from "$app/navigation";
  import { t } from "svelte-i18n";

  export let log = 'User';
  export let error = false;
  export let backup = false;

  const dispatch = createEventDispatcher();

  function setLogType(type: string) {
    dispatch('logType', { logType: type });
  }

  function showDataInfo() {
    goto('/history/datainfo');
  }
</script>

<div class="flex flex-1 flex-col h-full">
  <div class="w-full flex flex-col bg-gradient-to-b from-gray-300/50">
    <div class="text-size-xl font-bold -mt-4 text-center">
      {#if log === 'User'}
        {$t('level1.history.history-log')}
      {:else}
        {$t('level1.history.activity-log')}
      {/if}
    </div>
  </div>

  <Card class="mx-auto w-1/2 mt-4 flex flex-col">
    <div class="flex flex-col">
      {#if log === 'User'}
        <Button class="mx-auto mb-2" size="xl" on:click={() => setLogType('Graph')}>{$t('level1.history.view-as-graph')}</Button>
        <Button class="mx-auto mb-2" size="xl" on:click={() => setLogType('Table')}>{$t('level1.history.view-as-table')}</Button>
        <Button class="mx-auto mb-2 {error && !backup ? '!variant-ghost-error' : ''}" size="xl" on:click={() => setLogType('File')}>{$t('level1.history.download-to-file')}</Button>
        <Button class="mx-auto mb-2 {error && backup ? '!variant-ghost-error' : ''}" size="xl" on:click={() => setLogType('Backup')}>{$t('level1.history.download-from-backup')}</Button>
        <Button class="mx-auto mb-2" size="xl" on:click={() => showDataInfo()}>{$t('level1.history.sd-card-information')}</Button>
      {:else}
        <Button class="mx-auto mb-2" size="xl" on:click={() => setLogType('Table')}>{$t('level1.history.view-on-screen')}</Button>
        <Button class="mx-auto mb-2 {error && !backup ? '!variant-ghost-error' : ''}" size="xl" on:click={() => setLogType('File')}>{$t('level1.history.download-to-file')}</Button>
        <Button class="mx-auto mb-2 {error && backup ? '!variant-ghost-error' : ''}" size="xl" on:click={() => setLogType('Backup')}>{$t('level1.history.download-from-backup')}</Button>
      {/if}
    </div>
  </Card>
</div>