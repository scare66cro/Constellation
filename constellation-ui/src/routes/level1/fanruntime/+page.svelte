<script lang="ts">
  import GellertPage from "$lib/components/GellertPage.svelte";
  import ScrollableArea from "$lib/components/ScrollableArea.svelte";
  import FanRuntimeForm from "$lib/components/FanRuntimeForm.svelte";
  import { navigationStore } from "$lib/store";
  import { t } from "svelte-i18n";

  let title = $t('level1.fanruntime.fan-runtimes');
  let wait = false;
  let ready = false;
  $: level = $navigationStore.level;
</script>

<GellertPage {wait} {ready} {title} {level} name="fanruntime">
  <ScrollableArea>
    <FanRuntimeForm bind:wait bind:ready />
  </ScrollableArea>
</GellertPage>
