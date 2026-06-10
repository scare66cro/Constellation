<script lang="ts">
  import { onMount } from "svelte";
  import GellertPage from "$lib/components/GellertPage.svelte";
  import ScrollableArea from "$lib/components/ScrollableArea.svelte";
  import RefrigerationForm from "$lib/components/RefrigerationForm.svelte";
  import TritonScadaSection from "./TritonScadaSection.svelte";
  import RefrigerantPTChart from "./RefrigerantPTChart.svelte";
  import { frontMatterStore } from "$lib/store";
  import { t } from "svelte-i18n";

  // Thin route shell. The editable stages/PIDU/purge settings live in the
  // shared RefrigerationForm (also rendered by the dashboard refrig-coil
  // modal). The TRITON SCADA mimic + P-T chart stay here — they consume the
  // bridge-side Triton sidecar (`data.tritons`) loaded by +page.ts, which
  // has no proto representation. docs/spatial-ui-page-migration.md
  export let data: { tritons?: { slot: number; connected: boolean; label: string }[] };

  let title = $t('level2.refrigeration.refrigeration-setup');
  let wait = false;
  let ready = false;

  // Forwarded from the active Triton tab to the P-T chart.
  let activeRefrigerantType: number | undefined = undefined;
  let activeAmbientF: number | undefined = undefined;
  let activeCutInP:  number | undefined = undefined;
  let activeCutOutP: number | undefined = undefined;
  let activeDischargeTargetP: number | undefined = undefined;

  // Collapse guard: hide the legacy stage form ONLY when no AS2 stages are
  // configured AND a Triton orbit is connected (then it's SCADA-only).
  const refrigIndex = [0, 2, 4, 6, 8, 10, 19, 21];
  $: refrigData = $frontMatterStore?.refrigData as string[];
  $: available = refrigIndex.map((index, i) => {
    if (refrigData?.[i] !== '-1') {
      return index;
    }
  })?.filter((i) => i !== undefined) as number[];

  // `ready` gates GellertPage's loading veil. Own it here (not bound from
  // the form) so the SCADA-only case — where the form isn't rendered —
  // isn't stuck on the spinner.
  onMount(() => { ready = true; });
</script>

<GellertPage {wait} {title} {ready} level={2} name="refrigeration">
  <ScrollableArea>
  {#if available.length > 0 || (data.tritons?.length ?? 0) === 0}
    <RefrigerationForm bind:wait />
  {/if}

  <TritonScadaSection tritons={data.tritons ?? []}
                      bind:activeRefrigerantType={activeRefrigerantType}
                      bind:activeAmbientF={activeAmbientF}
                      bind:activeCutInP={activeCutInP}
                      bind:activeCutOutP={activeCutOutP}
                      bind:activeDischargeTargetP={activeDischargeTargetP} />
  <RefrigerantPTChart selected={activeRefrigerantType}
                      ambientF={activeAmbientF}
                      cutInP={activeCutInP}
                      cutOutP={activeCutOutP}
                      dischargeTargetP={activeDischargeTargetP} />
  </ScrollableArea>
</GellertPage>
