<script lang="ts">
  import { onMount } from "svelte";
  import Card from "$lib/ui/Card.svelte";
  import Table from "$lib/ui/Table.svelte";
  import Row from "$lib/ui/Row.svelte";
  import Column from "$lib/ui/Column.svelte";
  import Button from "$lib/ui/Button.svelte";
  import SaveButton from "$lib/components/SaveButton.svelte";
  import { isEqual } from "lodash-es";
  import { browser } from "$app/environment";
  import { t } from "svelte-i18n";
  import { navigationStore } from "$lib/store";
  import { alertSettings } from "$lib/business/protoStores";
  import { writeProtoRaw, buildForceVarintBytes } from "$lib/business/protoWrite";
  import { TAG } from "$lib/business/protoTags";
  import { getFilteredAlertLabels, getIncludedAlertIndices } from "$lib/business/alertMetadata";

  // Shared body of Alert Setup (level1/alerts): which warnings raise email
  // alerts. Classic page AND the "Alert Setup" tab of the dashboard Alerts
  // modal. docs/spatial-ui-page-migration.md
  export let wait = false;
  export let ready = false;
  export let embedded = false;
  export let theme: 'light' | 'dark' = 'light';
  export let canEdit: boolean | null = null;

  const labels: string[] = getFilteredAlertLabels();
  const indices: number[] = getIncludedAlertIndices();

  async function saveAlerts(filteredBits: boolean[]): Promise<void> {
    const idx = indices ?? [];
    const enabledFullIndices: number[] = [];
    for (let i = 0; i < filteredBits.length && i < idx.length; i++) {
      if (filteredBits[i]) enabledFullIndices.push(idx[i]);
    }
    const parts: Uint8Array[] = enabledFullIndices.map((idx) => buildForceVarintBytes({ 1: idx }));
    const total = parts.reduce((n, p) => n + p.length, 0);
    const inner = new Uint8Array(total);
    let off = 0;
    for (const p of parts) { inner.set(p, off); off += p.length; }
    await writeProtoRaw(TAG.AlertSettings, inner);
  }

  let alerts: boolean[] = [];
  let original: boolean[] = [];
  $: group2Index = labels.indexOf('group2');
  $: primaryLabels = group2Index >= 0 ? labels.slice(0, group2Index) : labels;
  $: secondaryLabels = group2Index >= 0 ? labels.slice(group2Index + 1) : [];
  $: edit = canEdit ?? ($navigationStore.level > 0);

  // Select / deselect every alert checkbox. Skip the 'group2' divider
  // sentinel (it's a layout marker, not a real alert flag).
  function setAll(val: boolean) {
    alerts = labels.map((lbl, i) => (lbl === 'group2' ? false : val));
  }

  let saveBtn: { save: () => Promise<void> } | undefined;
  export async function flush(): Promise<void> {
    if (!isEqual(alerts, original) && saveBtn) await saveBtn.save();
  }

  onMount(() => {
    if (!embedded) $navigationStore.isDirty = () => !isEqual(alerts, original);
    const unsub = alertSettings.subscribe((v) => {
      if (!v || !browser) return;
      // Don't clobber in-progress edits with a live firmware echo. Use a LOCAL
      // dirty check (alerts vs original) so this works in the modal too — in
      // embedded mode we don't register $navigationStore.isDirty, so relying on
      // it here let every push wipe the user's selections. After Save & Close
      // the SaveButton write-through resets `original`, so re-hydration resumes.
      if (ready && !isEqual(alerts, original)) return;
      const enabled = new Set<number>((v.alertFlags ?? []) as number[]);
      const next = indices.map((idx) => enabled.has(idx));
      if (!isEqual(next, alerts) || !ready) { alerts = next; original = [...next]; }
      ready = true;
    });
    if (!ready) ready = true;
    return () => unsub();
  });
</script>

<div class="pform pform--{theme}">
  <Card class="mb-0 w-3/4 mx-auto flex flex-col">
    {#if edit}
      <div class="flex justify-center gap-3 py-2">
        <Button size="lg" class="w-fit" on:click={() => setAll(true)}>{ $t('level1.runclock.select-all') }</Button>
        <Button size="lg" class="w-fit" on:click={() => setAll(false)}>Deselect All</Button>
      </div>
    {/if}
    <Table class="mb-0 text-size-xl">
      <Row>
        <Column class="w-1/6 font-bold border-r border-gray-400">{ $t('global.enable') }</Column>
        <Column class="w-5/6 font-bold">{ $t('level1.alerts.primary-system-alerts') }</Column>
      </Row>
      {#each primaryLabels as _, index}
        <Row>
          <Column class="w-1/6 border-r border-gray-400"><input type="checkbox" bind:checked={alerts[index]} disabled={!edit} class="checkbox w-8 h-8" /></Column>
          <Column class="w-5/6 !text-left pl-4">{labels[index]}</Column>
        </Row>
      {/each}
    </Table>
    {#if secondaryLabels.length}
      <Table class="mb-2 text-size-xl">
        <Row>
          <Column class="w-1/6 border-r border-gray-400 font-bold">{ $t('global.enable') }</Column>
          <Column class="w-5/6 font-bold">{ $t('level1.alerts.secondary-system-alerts') }</Column>
        </Row>
        {#each secondaryLabels as label, index}
          <Row>
            <Column class="w-1/6 border-r border-gray-400"><input type="checkbox" bind:checked={alerts[index + primaryLabels.length]} disabled={!edit} class="checkbox w-8 h-8" /></Column>
            <Column class="w-5/6 !text-left pl-4">{label}</Column>
          </Row>
        {/each}
      </Table>
    {/if}
    <SaveButton bind:this={saveBtn} {edit} bind:wait={wait} data={alerts} bind:original={original} autoSave onSave={saveAlerts} />
  </Card>
</div>
