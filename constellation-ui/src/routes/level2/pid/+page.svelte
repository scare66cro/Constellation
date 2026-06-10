<script lang="ts">
  import GellertPage from "$lib/components/GellertPage.svelte";
  import ScrollableArea from "$lib/components/ScrollableArea.svelte";
  import PidLogForm from "$lib/components/PidLogForm.svelte";
  import { t } from "svelte-i18n";

  // Thin route shell. The PID logging body (record toggles + View/Graph/Download
  // + Clear + wrap, saving PidLogSettings) lives in the shared PidLogForm so the
  // dashboard renders the same form under History & Logs › PID.
  // docs/spatial-ui-page-migration.md
  let title = $t('level2.pid.pid-logs');
  let wait = false;
  let ready = false;
</script>

<GellertPage {wait} {ready} {title} level={2} name="pid">
  <ScrollableArea>
    <PidLogForm bind:wait bind:ready />
  </ScrollableArea>
</GellertPage>
