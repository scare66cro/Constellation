<script lang="ts">
  import GellertPage from "$lib/components/GellertPage.svelte";
  import ScrollableArea from "$lib/components/ScrollableArea.svelte";
  import BurnerForm from "$lib/components/BurnerForm.svelte";
  import { t } from "svelte-i18n";

  let title = $t('level2.burner.burner-setup');
  let wait = false;
  let ready = false;
</script>

<GellertPage {wait} {title} level={2} {ready} name="burner">
  <ScrollableArea>
    <BurnerForm bind:wait bind:ready />
  </ScrollableArea>
</GellertPage>
