<script lang="ts">
  import GellertPage from "$lib/components/GellertPage.svelte";
  import ScrollableArea from "$lib/components/ScrollableArea.svelte";
  import ClimacellRunClockForm from "$lib/components/ClimacellRunClockForm.svelte";
  import { navigationStore } from "$lib/store";
  import { t } from "svelte-i18n";

  let title = $t('level1.climacell.climacell-control');
  let wait = false;
  let ready = false;
  $: level = $navigationStore.level;
</script>

<GellertPage {wait} {ready} {title} {level} name="climacell">
  <ScrollableArea>
    <ClimacellRunClockForm bind:wait bind:ready />
  </ScrollableArea>
</GellertPage>
