<script lang="ts">
  import GellertPage from "$lib/components/GellertPage.svelte";
  import ScrollableArea from "$lib/components/ScrollableArea.svelte";
  import AlarmHistoryForm from "$lib/components/AlarmHistoryForm.svelte";
  import { t } from "svelte-i18n";

  let title = $t('level1.history.alarm-log');
  let wait = false;
  let ready = false;
</script>

<GellertPage {title} level={1} name="history" {wait} {ready}>
  <ScrollableArea>
    <AlarmHistoryForm bind:wait bind:ready />
  </ScrollableArea>
</GellertPage>
