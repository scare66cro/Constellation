<script lang="ts">
  import GellertPage from "$lib/components/GellertPage.svelte";
  import ScrollableArea from "$lib/components/ScrollableArea.svelte";
  import DateTimeForm from "$lib/components/DateTimeForm.svelte";
  import { navigationStore } from "$lib/store";
  import { t } from "svelte-i18n";

  let title = $t('level1.date.set-date-time');
  let wait = false;
  let ready = false;
  $: level = $navigationStore.level;
</script>

<GellertPage {wait} {ready} {title} {level} name="date">
  <ScrollableArea>
    <DateTimeForm bind:wait bind:ready />
  </ScrollableArea>
</GellertPage>
