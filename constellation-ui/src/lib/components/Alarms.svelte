<script lang="ts">
	import Card from "$lib/ui/Card.svelte";
  import { headersStore, frontMatterStore, alarmsStore } from "$lib/store";
	import Button from "$lib/ui/Button.svelte";
	import Row from "$lib/ui/Row.svelte";
	import Table from "$lib/ui/Table.svelte";
	import Column from "$lib/ui/Column.svelte";
  import Wait from "$lib/ui/Wait.svelte";
	import { getModalStore } from "@skeletonlabs/skeleton";
	import { t } from "svelte-i18n";
	import { getHttpUrl } from "$lib/business/util";

  export let alarms: string[];

  $: wait = false;

  const modalStore = getModalStore();

  function hide() {
    $alarmsStore.canShowAlarm = false;
    $alarmsStore.isShowingAlarms = false;
    modalStore.close();
    modalStore.clear();
  }

  async function clear() {
    wait = true;
    $frontMatterStore.AlarmData = [];
    $alarmsStore.canShowAlarm = false;
    modalStore.close();
    modalStore.clear();
    await fetch(getHttpUrl('/iot/button'), {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
      },
      body: JSON.stringify({
        tag: 'button2',
        ClearAlarm: 'ClearAlarm',
      }),
    });
    wait = false;
    $alarmsStore.isShowingAlarms = false;
  }

  $: if (($frontMatterStore.AlarmData as string[])?.length > 0) {
      alarms = $frontMatterStore.AlarmData as string[];
    } else {
      alarms = [];
    }
</script>

<Card class="w-5/6 h-5/6 border-4 border-red-500 bg-white md:mx-2 xl:mx-auto flex flex-col">
  <h1 class="text-size-xl font-bold mb-2 mx-auto">{$t('global.alarm-monitor')} -- {$headersStore.PanelName ?? 'Agristar Panel'}</h1>
  <div class="flex flex-col flex-1 mb-2">
    {#if alarms && alarms.length > 0}
      <Table>
        {#each alarms as alarm}
          <Row>
            <Column class="p-2 text-size-xl">{alarm.split('=')[1]}</Column>
          </Row>
        {/each}
      </Table>
    {:else}
      <div class="text-center text-size-xl font-bold">{$t('global.no-alarms')}</div>
    {/if}
  </div>
  <div class="mx-auto">
    <Button class="mr-2" on:click={hide} size="lg">{$t('global.hide')}</Button>
    {#if alarms && alarms.length > 0}
      <Button on:click={clear} size="lg">{$t('global.clear')}</Button>
    {/if}
  </div>
  {#if wait}
    <Wait show={wait} />
  {/if}
</Card>