<script lang="ts">
  import GellertPage from "$lib/components/GellertPage.svelte";
  import ScrollableArea from "$lib/components/ScrollableArea.svelte";
  import IoConfigForm from "$lib/components/IoConfigForm.svelte";
  import { t } from "svelte-i18n";

  let title = $t('level2.ioconfig.system-i-o-configuration');
  let wait = false;
  let ready = false;
</script>

<GellertPage {wait} {title} level={2} {ready} name="ioconfig">
  <ScrollableArea>
    <IoConfigForm bind:wait bind:ready />
  </ScrollableArea>
</GellertPage>
