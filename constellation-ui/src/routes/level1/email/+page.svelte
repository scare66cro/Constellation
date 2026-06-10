<script lang="ts">
  import GellertPage from "$lib/components/GellertPage.svelte";
  import ScrollableArea from "$lib/components/ScrollableArea.svelte";
  import EmailForm from "$lib/components/EmailForm.svelte";
  import { navigationStore } from "$lib/store";
  import { t } from "svelte-i18n";

  let title = $t('level1.email.email-alerts');
  let wait = false;
  let ready = false;
  $: level = $navigationStore.level;
</script>

<GellertPage {wait} {ready} {title} {level} name="email">
  <ScrollableArea>
    <EmailForm bind:wait bind:ready />
  </ScrollableArea>
</GellertPage>
