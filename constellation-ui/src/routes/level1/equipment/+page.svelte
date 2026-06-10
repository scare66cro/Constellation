<script lang="ts">
  import GellertPage from "$lib/components/GellertPage.svelte";
  import ScrollableArea from "$lib/components/ScrollableArea.svelte";
  import EquipmentControlForm from "$lib/components/EquipmentControlForm.svelte";
  import { navigationStore } from "$lib/store";
  import { t } from "svelte-i18n";

  let title = $t('level1.equipment.equipment-status');
  let wait = false;
  let ready = false;
  $: level = $navigationStore.level;
</script>

<GellertPage {wait} {ready} {title} {level} name="equipment">
  <ScrollableArea>
    <EquipmentControlForm bind:wait bind:ready />
  </ScrollableArea>
</GellertPage>
