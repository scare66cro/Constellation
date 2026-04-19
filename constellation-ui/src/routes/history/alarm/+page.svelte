<script lang="ts">
	import { goto } from "$app/navigation";
	import GellertPage from "$lib/components/GellertPage.svelte";
	import Button from "$lib/ui/Button.svelte";
	import Card from "$lib/ui/Card.svelte";
	import Column from "$lib/ui/Column.svelte";
	import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
	import Row from "$lib/ui/Row.svelte";
	import Table from "$lib/ui/Table.svelte";
	import TextField from "$lib/ui/TextField.svelte";
	import { format } from "date-fns";
	import { modeToColorStore } from "$lib/store";
  import { t } from "svelte-i18n";
	import { getHttpUrl, safeJsonParse } from "$lib/business/util";
	import ScrollableArea from "$lib/components/ScrollableArea.svelte";

  let showRange = true;
  let showLogs = false;
  let wait = false;
  let title = $t('level1.history.alarm-log'); 
  let values = '1';
  let alarms: Array<{date: Date, currentmode: string, alarmdata: string[]}>;

  async function display() {
    wait = true;
    const response = await fetch(getHttpUrl(`/iot/alarms/${values}`));
    try {
      alarms = await safeJsonParse(response);
      title = `Alarm Log - Last ${values}`;
      showRange = false;
      showLogs = true;
    } catch (error) {
      console.error('Error fetching alarm data:', error);
      showRange = true;
      showLogs = false;
      title = $t('level1.history.alarm-log');
    } finally {
      wait = false;
    }
  }
</script>

<GellertPage {title} level={1} name='history' {wait}>
  {#if showRange}
    <Card class="mx-auto w-3/4 flex flex-col">
      <div class="text-center font-bold text-size-xl">
        {$t('level1.history.the-number-of-most-recent-alarms-to-display')}: <TextField size="xl" bind:value={values} class="w-36" edit={true} keyboardType={KeyboardTypes.Numeric} />
      </div>
      <div class="flex flex-row">
        <Button size="xl" class="ml-auto mr-2 w-48" on:click={() => goto('/history')}>{$t('global.back')}</Button>
        <Button size="xl" class="mr-auto ml-2 w-48" on:click={display}>{$t('global.next')}</Button>
      </div>
    </Card>
  {:else}
    <Card class="mx-auto w-3/4 flex flex-col">
      <ScrollableArea>
        <Table class="text-size-large">
          <Row class="font-bold">
            <Column class="border-r border-gray-400">{$t('global.date')}</Column><Column class="border-r border-gray-400">{$t('global.time')}</Column><Column>{$t('global.mode')}</Column>
          </Row>
          {#each alarms as alarm}
            <Row>
              <Column class="border-r border-gray-400">{format(new Date(alarm.date), "MM/dd/yyyy")}</Column>
              <Column class="border-r border-gray-400">{format(new Date(alarm.date), "HH:mm:ss")}</Column>
              <Column>{$modeToColorStore[parseInt(alarm.currentmode, 10)]?.text ?? $t('level2.analog.unknown')}</Column>
            </Row>
            {#each alarm.alarmdata[0].split(',') as data}
              <Row>
                <Column class="!text-left pl-2" colspan={3}>{data.split('=')[1]}</Column>
              </Row>
            {/each}
          {/each}
        </Table>
      </ScrollableArea>
    </Card>
  {/if}
</GellertPage>