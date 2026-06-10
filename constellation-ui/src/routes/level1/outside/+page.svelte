<script lang="ts">
  import GellertPage from "$lib/components/GellertPage.svelte";
  import ScrollableArea from "$lib/components/ScrollableArea.svelte";
  import OutsideAirForm from "$lib/components/OutsideAirForm.svelte";
  import { navigationStore } from "$lib/store";
  import { t } from "svelte-i18n";

  let title = $t('level1.outside.outside-air-control');
  let wait = false;
  let ready = false;
  $: level = $navigationStore.level;
</script>

<GellertPage {wait} {ready} {title} {level} name="outside">
  <ScrollableArea>
    <OutsideAirForm bind:wait bind:ready />
  </ScrollableArea>
</GellertPage>
