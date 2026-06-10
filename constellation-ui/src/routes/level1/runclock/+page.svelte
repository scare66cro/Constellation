<script lang="ts">
  import GellertPage from "$lib/components/GellertPage.svelte";
  import ScrollableArea from "$lib/components/ScrollableArea.svelte";
  import RunClockForm from "$lib/components/RunClockForm.svelte";
  import { navigationStore } from "$lib/store";
  import { t } from "svelte-i18n";

  let title = $t('level1.runclock.system-run-clock');
  let wait = false;
  let ready = false;
  $: level = $navigationStore.level;
</script>

<GellertPage {wait} {ready} {title} {level} name="runclock">
  <ScrollableArea>
    <RunClockForm bind:wait bind:ready />
  </ScrollableArea>
</GellertPage>
