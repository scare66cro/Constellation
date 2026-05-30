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

  // Storage info now reports the rpi5 SSD partition that holds the
  // bridge process + Postgres `paneldb`. The legacy `sdcard[]` 10-slot
  // array (a verbatim AS2 RTS/ACK shape) was deleted Apr 2026 — Nova
  // LP-AM2434 has no SD card; pg lives on the rpi5 SSD per
  // /memories/repo/pg-logging-on-rpi5.md.
  let records = {
    database: {
      activityCount: 0,
      historyCount: 0,
      percentUsed: 0,
      startDate: new Date(),
    },
    storage: {
      mount: '',
      totalBytes: 0,
      usedBytes: 0,
      freeBytes: 0,
      percentUsed: 0,
    },
  };
  let ready = false;

  function formatBytes(bytes: number): string {
    if (!bytes) return '0';
    const units = ['B', 'KB', 'MB', 'GB', 'TB'];
    let i = 0;
    let v = bytes;
    while (v >= 1024 && i < units.length - 1) { v /= 1024; i++; }
    return `${v.toFixed(v >= 10 ? 0 : 1)} ${units[i]}`;
  }

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
      <Row><Column class="w-1/2 xl:py-1 border-r border-gray-400">{$t('level1.history.capacity-records')}</Column><Column class="w-1/2 xl:py-1">{formatBytes(records.storage.totalBytes)}</Column></Row>
      <Row><Column class="w-1/2 xl:py-1 border-r border-gray-400">{$t('level1.history.records-used')}</Column><Column class="w-1/2 xl:py-1">{formatBytes(records.storage.usedBytes)}</Column></Row>
      <Row><Column class="w-1/2 xl:py-1 border-r border-gray-400">{$t('level1.history.percent-used')}</Column><Column class="w-1/2 xl:py-1">{records.storage.percentUsed} %</Column></Row>
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