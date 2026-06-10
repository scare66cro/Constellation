<script lang="ts">
  import GellertPage from "$lib/components/GellertPage.svelte";
  import ScrollableArea from "$lib/components/ScrollableArea.svelte";
  import ClimacellConfigForm from "$lib/components/ClimacellConfigForm.svelte";
  import { t } from "svelte-i18n";

  let title = $t('level2.climacell.climacell-setup');
  let wait = false;
  let ready = false;
</script>

<GellertPage {wait} {title} level={2} {ready} name="climacell">
  <ScrollableArea>
    <ClimacellConfigForm bind:wait bind:ready />
  </ScrollableArea>
</GellertPage>
