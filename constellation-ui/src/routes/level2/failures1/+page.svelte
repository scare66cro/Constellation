<script lang="ts">
  import GellertPage from "$lib/components/GellertPage.svelte";
  import ScrollableArea from "$lib/components/ScrollableArea.svelte";
  import FailuresForm1 from "$lib/components/FailuresForm1.svelte";
  import { t } from "svelte-i18n";

  let title = $t('level2.failures1.failures-setup-1');
  let wait = false;
  let ready = false;
</script>

<GellertPage {wait} {ready} {title} level={2} name="failures1">
  <ScrollableArea>
    <FailuresForm1 bind:wait bind:ready />
  </ScrollableArea>
</GellertPage>
