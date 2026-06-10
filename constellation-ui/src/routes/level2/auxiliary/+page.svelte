<script lang="ts">
  import GellertPage from "$lib/components/GellertPage.svelte";
  import ScrollableArea from "$lib/components/ScrollableArea.svelte";
  import AuxiliaryForm from "$lib/components/AuxiliaryForm.svelte";
  import { t } from "svelte-i18n";

  let title = $t('level2.auxiliary.auxiliary-output-programming');
  let wait = false;
  let ready = false;
</script>

<GellertPage {wait} {title} level={2} {ready} name="auxiliary">
  <ScrollableArea>
    <AuxiliaryForm bind:wait bind:ready />
  </ScrollableArea>
</GellertPage>
