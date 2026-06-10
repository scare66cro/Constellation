<script lang="ts">
  import GellertPage from "$lib/components/GellertPage.svelte";
  import ScrollableArea from "$lib/components/ScrollableArea.svelte";
  import HeatForm from "$lib/components/HeatForm.svelte";
  import { navigationStore } from "$lib/store";
  import { t } from "svelte-i18n";

  // New dedicated Heat Control page. Seeded with the plenum-heater threshold
  // relocated from level1/miscellaneous; more heat controls to follow.
  let title = $t('equipment.heat');
  let wait = false;
  let ready = false;
  $: level = $navigationStore.level;
</script>

<GellertPage {wait} {ready} {title} {level} name="heat">
  <ScrollableArea>
    <HeatForm bind:wait bind:ready />
  </ScrollableArea>
</GellertPage>
