<script lang="ts">
	import { getHttpUrl, safeJsonParse } from "$lib/business/util";
  import { historyStore } from "$lib/store";
  import Card from "$lib/ui/Card.svelte";
  import DatePicker from "$lib/ui/DatePicker.svelte";
  import { addDays, startOfDay, isBefore, isAfter, isValid } from "date-fns";
  import { onMount } from "svelte";
  import { t } from "svelte-i18n";

  export let downloadFromBackup: boolean;

  let maxDate = addDays(new Date(), 1);
  let minDate: Date | undefined = undefined;

  // `downloadFromBackup` selected the SD-card backup partition's start
  // date on AS2; on Constellation there's no backup partition (logs
  // live in pg on the rpi5 SSD), so the flag now just collapses to the
  // database start date. The prop is kept so the existing call sites
  // don't have to change.
  let records = {
    database: {
      activityCount: 0,
      historyCount: 0,
      percentUsed: 0,
      startDate: '',
    },
  };

  function parseDate(value: string | Date | undefined): Date | undefined {
    if (!value) return undefined;
    const parsed = value instanceof Date ? value : new Date(value);
    return isValid(parsed) ? parsed : undefined;
  }

  // Clamp a date to the valid range
  function clampDate(date: Date, min: Date | undefined, max: Date): Date {
    let result = startOfDay(date);
    if (min && isValid(min) && isBefore(result, startOfDay(min))) {
      result = startOfDay(min);
    }
    if (isValid(max) && isAfter(result, startOfDay(max))) {
      result = startOfDay(max);
    }
    return result;
  }

  // Recalculate minDate when records are loaded.
  $: {
    if (records.database?.startDate) {
      const dbStartDate = parseDate(records.database?.startDate);
      minDate = startOfDay(dbStartDate ?? new Date());

      // Clamp store dates to valid range
      $historyStore.startDate = clampDate($historyStore.startDate, minDate, maxDate);
      $historyStore.endDate = clampDate($historyStore.endDate, minDate, maxDate);
    }
  }

  function handleStartDateSelect(event: CustomEvent<Date>): void {
    $historyStore.startDate = event.detail;
  }

  function handleEndDateSelect(event: CustomEvent<Date>): void {
    $historyStore.endDate = event.detail;
  }

  onMount(async () => { 
    try {
      const response = await fetch(getHttpUrl('/iot/datainfo'));
      records = await safeJsonParse(response);
    } catch (e) {
      console.error('RangeSelection error retrieving data', { error: String(e) });
    }
  });
</script>
<div class="flex flex-1 flex-col h-full">
  <div class="w-full flex flex-col bg-gradient-to-b from-gray-300/50">
    <Card class="mx-auto container-wide flex flex-col mt-4">
      <div class="flex flex-col text-size-xl">
        <div class="flex flex-row mx-auto mb-2">{$t('level1.history.date-range-selection')}:</div>
        <div class="flex flex-row mx-auto">
          {$t('level1.history.start-date')}: <DatePicker class="mx-2 w-48 3xl:w-96" min={minDate} max={maxDate} bind:value={$historyStore.startDate} on:select={handleStartDateSelect}/>
          <span class="ml-10">{$t('level1.history.end-date')}:</span> <DatePicker class="ml-2 mr-auto w-48 3xl:w-96" min={minDate} max={maxDate} bind:value={$historyStore.endDate} on:select={handleEndDateSelect}/>
        </div>
      </div>
    </Card>
  </div>
</div>