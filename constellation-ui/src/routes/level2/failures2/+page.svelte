<script lang="ts">
  import GellertPage from "$lib/components/GellertPage.svelte";
  import ScrollableArea from "$lib/components/ScrollableArea.svelte";
  import FailuresForm2 from "$lib/components/FailuresForm2.svelte";
  import { t } from "svelte-i18n";

  let title = $t('level2.failures2.failures-setup-2');
  let wait = false;
  let ready = false;
</script>

<GellertPage {wait} {ready} {title} level={2} name="failures2">
  <ScrollableArea>
    <FailuresForm2 bind:wait bind:ready />
  </ScrollableArea>
</GellertPage>
