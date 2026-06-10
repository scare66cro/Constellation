<script lang="ts">
  import GellertPage from "$lib/components/GellertPage.svelte";
  import ScrollableArea from "$lib/components/ScrollableArea.svelte";
  import FanSpeedForm from "$lib/components/FanSpeedForm.svelte";
  import { navigationStore } from "$lib/store";
  import { t } from "svelte-i18n";

  let title = $t('level1.fanspeed.fan-speed-control');
  let wait = false;
  let ready = false;
  $: level = $navigationStore.level;
</script>

<GellertPage {wait} {ready} {title} {level} name="fanspeed">
  <ScrollableArea>
    <FanSpeedForm bind:wait bind:ready />
  </ScrollableArea>
</GellertPage>
