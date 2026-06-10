<script lang="ts">
  import GellertPage from "$lib/components/GellertPage.svelte";
  import ScrollableArea from "$lib/components/ScrollableArea.svelte";
  import FreshAirDoorSettingsForm from "$lib/components/FreshAirDoorSettingsForm.svelte";
  import GdcStagesForm from "$lib/components/GdcStagesForm.svelte";
  import { navigationStore } from "$lib/store";
  import { t } from "svelte-i18n";

  let title = $t('level2.door.fresh-air-door-setup');
  let wait = false;
  let ready = false;
  $: level = $navigationStore.level;
  $: edit = level > 0;
</script>

<GellertPage {wait} {title} {ready} level={2} name="door">
  <ScrollableArea>
    <FreshAirDoorSettingsForm bind:wait bind:ready />
    <GdcStagesForm canEdit={edit} />
  </ScrollableArea>
</GellertPage>
