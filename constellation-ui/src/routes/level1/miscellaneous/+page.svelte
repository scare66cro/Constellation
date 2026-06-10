<script lang="ts">
  import GellertPage from "$lib/components/GellertPage.svelte";
  import ScrollableArea from "$lib/components/ScrollableArea.svelte";
  import Card from "$lib/ui/Card.svelte";
  import { navigationStore } from "$lib/store";
  import { t } from "svelte-i18n";

  // 2026-06-08: the miscellaneous "junk drawer" was emptied — its settings
  // moved to dedicated pages:
  //   • heater threshold + cavity heater → /level1/heat (HeatForm)
  //   • refrigeration mode / enthalpy / defrost → /level2/refrigeration
  //   • keypad preference → dropped (unused)
  // Page hidden from the nav list (+layout.svelte). Kept as a stub so any
  // bookmarked URL lands somewhere sensible rather than 404.
  let title = $t('level1.miscellaneous.miscellaneous-program-parameters');
  let wait = false;
  let ready = true;
  $: level = $navigationStore.level;
</script>

<GellertPage {wait} {ready} {title} {level} name="miscellaneous">
  <ScrollableArea>
    <Card class="mx-auto mt-2 flex flex-col container-wide p-6 text-center text-size-xl opacity-70">
      <p>These settings have moved:</p>
      <p class="mt-2">Heater &amp; cavity heater → <b>Heat</b></p>
      <p>Refrigeration mode / enthalpy / defrost → <b>Refrigeration</b></p>
    </Card>
  </ScrollableArea>
</GellertPage>
