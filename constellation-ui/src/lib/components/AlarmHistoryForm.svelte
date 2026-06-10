<script lang="ts">
  import { onMount } from "svelte";
  import Card from "$lib/ui/Card.svelte";
  import Column from "$lib/ui/Column.svelte";
  import Row from "$lib/ui/Row.svelte";
  import Table from "$lib/ui/Table.svelte";
  import TextField from "$lib/ui/TextField.svelte";
  import Button from "$lib/ui/Button.svelte";
  import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
  import { format } from "date-fns";
  import { modeToColorStore } from "$lib/store";
  import { t } from "svelte-i18n";
  import { getHttpUrl, safeJsonParse } from "$lib/business/util";

  // Shared body of the Alarm History viewer (history/alarm): the last N alarm
  // records (date / time / mode + the alarm lines). READ-ONLY — rendered on
  // the classic page AND as the dashboard alarm-history modal (no save; the
  // modal footer is Close-only via MODAL_NOSAVE). docs/spatial-ui-page-migration.md
  export let wait = false;
  export let ready = false;
  export let embedded = false;
  export let theme: 'light' | 'dark' = 'light';
  // Read-only viewer — nothing to persist; satisfies the modal form contract.
  export async function flush(): Promise<void> {}

  let values = '25';
  let alarms: Array<{ date: string; currentmode: string; alarmdata: string[] }> = [];
  let loaded = false;

  async function display(): Promise<void> {
    wait = true;
    try {
      const response = await fetch(getHttpUrl(`/iot/alarms/${values}`));
      alarms = (await safeJsonParse(response)) ?? [];
    } catch (error) {
      console.error('Error fetching alarm history:', error);
      alarms = [];
    } finally {
      wait = false;
      loaded = true;
      ready = true;
    }
  }

  onMount(display);
</script>

<div class="pform pform--{theme}">
  <Card class="mx-auto w-full flex flex-col">
    <div class="flex flex-row items-center justify-center gap-3 py-2 text-size-large">
      {$t('level1.history.the-number-of-most-recent-alarms-to-display')}:
      <TextField size="lg" bind:value={values} class="w-24" edit={true} keyboardType={KeyboardTypes.Numeric} />
      <Button size="lg" class="w-fit" on:click={display}>{$t('level1.history.display')}</Button>
    </div>
    {#if loaded}
      {#if alarms.length === 0}
        <p class="text-center opacity-60 py-6 text-size-large">{ $t('global.no-alarms') }</p>
      {:else}
        <Table class="text-size-large">
          <Row class="font-bold">
            <Column class="border-r border-gray-400">{$t('global.date')}</Column>
            <Column class="border-r border-gray-400">{$t('global.time')}</Column>
            <Column>{$t('global.mode')}</Column>
          </Row>
          {#each alarms as alarm}
            <Row>
              <Column class="border-r border-gray-400">{format(new Date(alarm.date), "MM/dd/yyyy")}</Column>
              <Column class="border-r border-gray-400">{format(new Date(alarm.date), "HH:mm:ss")}</Column>
              <Column>{$modeToColorStore[parseInt(alarm.currentmode, 10)]?.text ?? $t('level2.analog.unknown')}</Column>
            </Row>
            {#each (alarm.alarmdata?.[0]?.split(',') ?? []) as data}
              <Row>
                <Column class="!text-left pl-2" colspan={3}>{data.split('=')[1] ?? data}</Column>
              </Row>
            {/each}
          {/each}
        </Table>
      {/if}
    {/if}
  </Card>
</div>
