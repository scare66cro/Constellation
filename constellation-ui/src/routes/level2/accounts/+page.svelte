<script lang="ts">
  import GellertPage from "$lib/components/GellertPage.svelte";
  import ScrollableArea from "$lib/components/ScrollableArea.svelte";
  import AccountsForm from "$lib/components/AccountsForm.svelte";
  import { navigationStore } from "$lib/store";
  import { t } from "svelte-i18n";

  let title = `${$t('level2.accounts.set-level')} 1 ${$t('level2.accounts.user-accounts')}`;
  let wait = false;
  let ready = false;
  $: level = $navigationStore.level;
</script>

<GellertPage {wait} {title} {ready} {level} name="accounts">
  <ScrollableArea>
    <AccountsForm bind:wait bind:ready />
  </ScrollableArea>
</GellertPage>
