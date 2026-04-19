<script lang="ts">
	import { goto } from '$app/navigation';
	import { getHttpUrl, safeJsonParse } from '$lib/business/util';
	import GellertPage from '$lib/components/GellertPage.svelte';
	import { keysStore } from '$lib/store';
	import Card from '$lib/ui/Card.svelte';
	import Column from '$lib/ui/Column.svelte';
	import Row from '$lib/ui/Row.svelte';
	import Table from '$lib/ui/Table.svelte';
  import { onMount } from 'svelte';
  import { t } from 'svelte-i18n';

  let records = {
    database: {
      activityCount: 0,
      historyCount: 0,
      percentUsed: 0,
      startDate: new Date(),
    },
    sdcard: [] as string[],
  };
  let ready = false;

  onMount(async () => {
    if ($keysStore.accessLevel < 1) {
      goto("/history");
    }
    try {
      const response = await fetch(getHttpUrl('/iot/datainfo'));
      records = await safeJsonParse(response);
    } catch (error) {
      console.error('Error fetching storage information:', error);
    } finally {
      ready = true;
    }
  });
</script>

<GellertPage title={$t('level1.history.sd-card-information')} level={1} name="history" {ready}>
  <Card class="mx-auto mb-0 w-3/4 flex flex-col">
    <Table class="text-size-xl">
      <Row><Column class="w-1/2 xl:py-1 border-r border-gray-400">{$t('level1.history.capacity-records')}</Column><Column class="w-1/2 xl:py-1">{records.sdcard[3]}</Column></Row>
      <Row><Column class="w-1/2 xl:py-1 border-r border-gray-400">{$t('level1.history.records-used')}</Column><Column class="w-1/2 xl:py-1">{records.sdcard[4]}</Column></Row>
      <Row><Column class="w-1/2 xl:py-1 border-r border-gray-400">{$t('level1.history.percent-used')}</Column><Column class="w-1/2 xl:py-1">{records.sdcard[5]}</Column></Row>
    </Table>
    <div class="text-center font-bold text-size-xl">{$t('level1.history.write-errors')}</div>
    <Table class="text-size-xl">
      <Row><Column class="w-1/2 xl:py-1 border-r border-gray-400">{$t('level1.history.history-log')}</Column><Column class="w-1/2 xl:py-1">{records.sdcard[7]}</Column></Row>
      <Row><Column class="w-1/2 xl:py-1 border-r border-gray-400">{$t('level1.history.activity-log')}</Column><Column class="w-1/2 xl:py-1">{records.sdcard[6]}</Column></Row>
      <Row><Column class="w-1/2 xl:py-1 border-r border-gray-400">{$t('level1.history.statistics-record')}</Column><Column class="w-1/2 xl:py-1">{records.sdcard[8]}</Column></Row>
      <Row><Column class="w-1/2 xl:py-1 border-r border-gray-400">{$t('level1.history.total')}</Column><Column class="w-1/2 xl:py-1">{records.sdcard[9]}</Column></Row>
    </Table>
  </Card>
  <Card class="mx-auto w-3/4 flex flex-col">
    <div class="text-center font-bold text-size-xl">{$t('level1.history.database-information')}</div>
    <Table class="text-size-large">
      <Row><Column class="w-1/2 xl:py-1 border-r border-gray-400">{$t('level1.history.history-items')}</Column><Column class="w-1/2 xl:py-1">{records.database.historyCount}</Column></Row>
      <Row><Column class="w-1/2 xl:py-1 border-r border-gray-400">{$t('level1.history.activity-items')}</Column><Column class="w-1/2 xl:py-1">{records.database.activityCount}</Column></Row>
      <Row><Column class="w-1/2 xl:py-1 border-r border-gray-400">{$t('level1.history.percent-used')}</Column><Column class="w-1/2 xl:py-1">{records.database.percentUsed.toFixed(0)} %</Column></Row>
    </Table>
  </Card>
</GellertPage>