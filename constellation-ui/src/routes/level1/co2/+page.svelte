<script lang="ts">
  import GellertPage from "$lib/components/GellertPage.svelte";
  import ScrollableArea from "$lib/components/ScrollableArea.svelte";
  import Co2PurgeForm from "$lib/components/Co2PurgeForm.svelte";
  import { navigationStore } from "$lib/store";
  import { t } from "svelte-i18n";

  let title = `${$t('level1.co2.high')} CO2 ${ $t('level1.co2.level-purge-control') }`;
  let wait = false;
  let ready = false;
  $: level = $navigationStore.level;
</script>

<GellertPage {wait} {ready} {title} {level} name="co2">
  <ScrollableArea>
    <Co2PurgeForm bind:wait bind:ready />
  </ScrollableArea>
</GellertPage>
