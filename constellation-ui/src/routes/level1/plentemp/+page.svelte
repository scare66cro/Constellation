<script lang="ts">
  import GellertPage from "$lib/components/GellertPage.svelte";
  import ScrollableArea from "$lib/components/ScrollableArea.svelte";
  import PlenumSetpointsForm from "$lib/components/PlenumSetpointsForm.svelte";
  import { navigationStore } from "$lib/store";
  import { t } from "svelte-i18n";

  // Thin route shell. All fields + load + save logic live in the shared
  // PlenumSetpointsForm so the /dashboard/plan3d modal renders the exact
  // same form (single source of truth). The form binds `wait` / `ready`
  // back out for GellertPage's chrome; `title` / `level` are page-only.
  let title = $t('level1.plentemp.plenum-setpoints-and-alarms');
  let wait = false;
  let ready = false;
  $: level = $navigationStore.level;
</script>

<GellertPage {wait} {ready} {title} {level} name="plentemp">
  <ScrollableArea>
    <PlenumSetpointsForm bind:wait bind:ready />
  </ScrollableArea>
</GellertPage>
