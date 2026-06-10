<script lang="ts">
  import GellertPage from "$lib/components/GellertPage.svelte";
  import ScrollableArea from "$lib/components/ScrollableArea.svelte";
  import FanBoostForm from "$lib/components/FanBoostForm.svelte";
  import { navigationStore } from "$lib/store";
  import { t } from "svelte-i18n";

  let title = $t('level1.fanboost.fan-boost-control');
  let wait = false;
  let ready = false;
  $: level = $navigationStore.level;
</script>

<GellertPage {wait} {ready} {title} {level} name="fanboost">
  <ScrollableArea>
    <FanBoostForm bind:wait bind:ready />
  </ScrollableArea>
</GellertPage>
