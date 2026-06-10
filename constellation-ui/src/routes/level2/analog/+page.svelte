<script lang="ts">
  import GellertPage from "$lib/components/GellertPage.svelte";
  import ScrollableArea from "$lib/components/ScrollableArea.svelte";
  import AnalogConfigForm from "$lib/components/AnalogConfigForm.svelte";
  import { t } from "svelte-i18n";

  let title = $t('level2.analog.analog-board-setup');
  let wait = false;
  let ready = false;
</script>

<GellertPage {wait} {title} level={2} {ready} name="analog">
  <ScrollableArea>
    <AnalogConfigForm bind:wait bind:ready />
  </ScrollableArea>
</GellertPage>
