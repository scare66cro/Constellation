<script lang="ts">
  import GellertPage from "$lib/components/GellertPage.svelte";
  import HumidifierControlForm from "$lib/components/HumidifierControlForm.svelte";
  import { navigationStore } from "$lib/store";
  import { t } from "svelte-i18n";

  // Thin route shell — body lives in the shared HumidifierControlForm so the
  // dashboard humidifier modal renders the same form. See plentemp for the
  // pattern. docs/spatial-ui-page-migration.md
  let title = $t('level1.humidifier.humidifier-control');
  let wait = false;
  let ready = false;
  $: level = $navigationStore.level;
</script>

<GellertPage {wait} {ready} {title} {level} name="humidifier">
  <HumidifierControlForm bind:wait bind:ready />
</GellertPage>
