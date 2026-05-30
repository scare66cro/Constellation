<script lang="ts">
	import { onMount } from "svelte";
  import GellertPage from "$lib/components/GellertPage.svelte";
	import Card from "$lib/ui/Card.svelte";
	import Table from "$lib/ui/Table.svelte";
	import Row from "$lib/ui/Row.svelte";
	import { navigationStore } from "$lib/store";
	import Column from "$lib/ui/Column.svelte";
	import SaveButton from "$lib/components/SaveButton.svelte";
	import { isEqual } from "lodash-es";
	import { goto } from "$app/navigation";
	import { browser } from "$app/environment";
  import { t } from "svelte-i18n";
  import ScrollableArea from "$lib/components/ScrollableArea.svelte";
  import { alertSettings } from "$lib/business/protoStores";
  import { writeProtoRaw, buildForceVarintBytes } from "$lib/business/protoWrite";
  import { TAG } from "$lib/business/protoTags";
  import { getFilteredAlertLabels, getIncludedAlertIndices } from "$lib/business/alertMetadata";

  // Phase 5.1: alert ENABLE BITS derived live from $alertSettings.alertFlags
  // (proto envelope tag 35, full-bitmap indices). Bridge GET /iot/alerts is
  // no longer consumed.
  // Apr 27 2026: labels + includedIndices were the bridge endpoints
  // /iot/alert/{labels,indices} — pure static derivations of WARNING_KEYS
  // for the AGRISTAR board type. Now derived client-side via
  // alertMetadata.ts. Bridge endpoints + helpers will be deleted next.
  const labels: string[] = getFilteredAlertLabels();
  const indices: number[] = getIncludedAlertIndices();

  /**
   * Phase 5.1 proto-direct save. UI sends its filtered boolean[] mapped
   * back to firmware full-bitmap indices via data.includedIndices, then
   * encodes the enabled set as repeated force-varints into field 1 of
   * AlertSettings (settings field 35). apply_alert_setup() clears all
   * flags first then sets each idx it sees, so we only emit the enabled
   * ones. force-varint is required because idx=0 (WARN_PLENTEMP1) is
   * legitimately enable-able.
   */
  async function saveAlerts(filteredBits: boolean[]): Promise<void> {
    const idx = indices ?? [];
    const enabledFullIndices: number[] = [];
    for (let i = 0; i < filteredBits.length && i < idx.length; i++) {
      if (filteredBits[i]) enabledFullIndices.push(idx[i]);
    }
    // buildForceVarintBytes only takes one value per field; emit per-idx
    // and concatenate so we get repeated field 1.
    const parts: Uint8Array[] = enabledFullIndices.map(idx => buildForceVarintBytes({ 1: idx }));
    const total = parts.reduce((n, p) => n + p.length, 0);
    const inner = new Uint8Array(total);
    let off = 0;
    for (const p of parts) { inner.set(p, off); off += p.length; }
    await writeProtoRaw(TAG.AlertSettings, inner);
  }

  const title = $t('level1.alerts.select-email-alerts');

  // Initial `data.alerts` (SSR-loaded from /iot/alerts) is already in the
  // page's *filtered* index space that matches `labels[]`. We preserve that
  // mapping for the rendered bitmap. The typed `$alertSettings` store is
  // still imported so any future live push from firmware triggers a
  // re-fetch (see onMount).
  let alerts: boolean[] = [];
  let original: boolean[] = [];
  // Sentinel 'group2' divides primary / secondary alert groups. If not present, all go in primary.
  $: group2Index = labels.indexOf('group2');
  $: primaryLabels = group2Index >= 0 ? labels.slice(0, group2Index) : labels;
  // Secondary labels exclude the sentinel itself.
  $: secondaryLabels = group2Index >= 0 ? labels.slice(group2Index + 1) : [];
  $: ready = false;
  $: wait = false;
  $: level = $navigationStore.level;
  $: edit = $navigationStore.level > 0;
  $: if (!edit && browser) {
    goto('email');
  }

  onMount(() => {
    $navigationStore.isDirty = () => !isEqual(alerts, original);
    const unsub = alertSettings.subscribe((v) => {
      if (!v || !browser) return;
      // Don't clobber in-progress checkbox edits with a stale firmware
      // echo. Once the user has touched anything (alerts != original),
      // suspend rehydration until the SaveButton round-trip lands and
      // resets `original` to the saved state.
      if ($navigationStore?.isDirty?.()) {
        ready = true;
        return;
      }
      const enabled = new Set<number>((v.alertFlags ?? []) as number[]);
      const next = indices.map((idx) => enabled.has(idx));
      if (!isEqual(next, alerts) || !ready) {
        alerts = next;
        original = [...next];
      }
      ready = true;
    });
    // Even if the store hasn't fired yet, mark ready so GellertPage shows
    // labels (will populate booleans on first proto frame).
    if (!ready) ready = true;
    return () => unsub();
  });
</script>

<GellertPage {wait} {ready} {title} {level} name="alerts">
  <Card class="mb-0 w-3/4 mx-auto flex flex-col">
    <ScrollableArea>
      <Table class="mb-0 text-size-xl">
        <Row>
          <Column class="w-1/6 font-bold border-r border-gray-400">
            { $t('global.enable') }
          </Column>
          <Column class="w-5/6 font-bold">
            { $t('level1.alerts.primary-system-alerts') }
          </Column>
        </Row>
        {#each primaryLabels as _, index}
          <Row>
            <Column class="w-1/6 border-r border-gray-400">
              <input type="checkbox" bind:checked={alerts[index]} class="checkbox w-8 h-8" />
            </Column>
            <Column class="w-5/6 !text-left pl-4">
              {labels[index]}
            </Column>
          </Row>
        {/each}
      </Table>
      <Table class="mb-2 text-size-xl">
        <Row>
          <Column class="w-1/6 border-r border-gray-400 font-bold">
            { $t('global.enable') }
          </Column>
          <Column class="w-5/6 font-bold">
            { $t('level1.alerts.secondary-system-alerts') }
          </Column>
        </Row>
        {#each secondaryLabels as label, index}
          <Row>
            <Column class="w-1/6 border-r border-gray-400">
              <input type="checkbox" bind:checked={alerts[index + primaryLabels.length]} class="checkbox w-8 h-8" />
            </Column>
            <Column class="w-5/6 !text-left pl-4">
              {label}
            </Column>
          </Row>
        {/each}
      </Table>
      <svelte:fragment slot="footer-center">
        <SaveButton {edit} bind:wait={wait} data={alerts} bind:original={original} autoSave onSave={saveAlerts} />
      </svelte:fragment>
    </ScrollableArea>
  </Card>
</GellertPage>



