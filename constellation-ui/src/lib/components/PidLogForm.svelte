<script lang="ts">
  import { onMount } from "svelte";
  import { goto } from "$app/navigation";
  import Card from "$lib/ui/Card.svelte";
  import Table from "$lib/ui/Table.svelte";
  import Row from "$lib/ui/Row.svelte";
  import Column from "$lib/ui/Column.svelte";
  import Button from "$lib/ui/Button.svelte";
  import Select from "$lib/ui/Select.svelte";
  import SaveButton from "$lib/components/SaveButton.svelte";
  import { SlideToggle } from "@skeletonlabs/skeleton";
  import { navigationStore, pidStore } from "$lib/store";
  import { getHttpUrl } from "$lib/business/util";
  import { pidLogSettings } from "$lib/business/protoStores";
  import { writeProto } from "$lib/business/protoWrite";
  import { TAG } from "$lib/business/protoTags";
  import { isEqual } from "lodash-es";
  import { t } from "svelte-i18n";

  // Shared body of the PID logging hub (level2/pid): per-controller (Fresh-Air
  // Doors / Refrigeration) record on/off + View / Graph / Download + Clear +
  // overwrite-oldest (wrap), saving PidLogSettings (proto). Rendered on the
  // classic page AND as the dashboard 'pidlogs' modal, surfaced under the PID
  // section of the History & Logs hub (moved there 2026-06-10). The View /
  // Graph / Download actions route to the classic /level2/table|graph|download
  // viewers transitionally (heavy data viewers not yet modal-migrated, same as
  // the Activity/User log links). docs/spatial-ui-page-migration.md
  export let wait = false;
  export let ready = false;
  export let embedded = false;
  export let theme: 'light' | 'dark' = 'light';
  export async function flush(): Promise<void> {
    if (!isEqual(pid, original)) await saveBtn?.save?.();
  }
  let saveBtn: { save: () => Promise<void> } | undefined;

  let recordDoors = false;
  let recordRefrig = false;
  let pid = { pidWrap: '0', logDoors: '0', logRefrig: '0' };
  let original = { pidWrap: '0', logDoors: '0', logRefrig: '0' };
  let hydrated = false;

  $: if (!hydrated && $pidLogSettings) {
    pid = {
      pidWrap:   String($pidLogSettings.wrap ?? 0),
      logDoors:  String($pidLogSettings.logDoors ?? 0),
      logRefrig: String($pidLogSettings.logRefrig ?? 0),
    };
    original = { ...pid };
    recordDoors = pid.logDoors === '1';
    recordRefrig = pid.logRefrig === '1';
    hydrated = true;
    ready = true;
  }

  onMount(() => {
    // Local dirty check (NOT $navigationStore.isDirty?.()) so it works whether
    // page or modal; only register the swipe-nav guard on the classic page.
    if (!embedded) $navigationStore.isDirty = () => !isEqual(pid, original);
    if (hydrated) ready = true;
    // From the dashboard hub, View/Graph/Download route to the classic viewers;
    // make their "return" come back to the dashboard rather than a stale page.
    if (embedded) $pidStore.returnPage = '/dashboard/plan3d';
  });

  // Toggle handlers: proto-direct save of PidLogSettings (TAG=67, sfield=37).
  // Force-encode logDoors/logRefrig because 0 is a meaningful value (Off);
  // proto3 zero-suppression would otherwise drop it and the firmware
  // in-place patcher would leave the previous ON state intact. wrap is also
  // force-encoded to keep the firmware row consistent on a partial write.
  async function saveLogToggles() {
    wait = true;
    pid.logDoors  = recordDoors  ? '1' : '0';
    pid.logRefrig = recordRefrig ? '1' : '0';
    const wrap      = parseInt(pid.pidWrap,   10) || 0;
    const logDoors  = parseInt(pid.logDoors,  10) || 0;
    const logRefrig = parseInt(pid.logRefrig, 10) || 0;
    try {
      await writeProto(TAG.PidLogSettings, { wrap, logDoors, logRefrig });
      original = { ...pid };  // toggle survived; suppress dirty-prompt
    } finally {
      wait = false;
    }
  }

  // Clear handler: bridge truncates pg `pid_log` table directly
  // (rpi5 Postgres lives outside firmware — see /memories/repo/pg-logging-on-rpi5.md).
  async function postClearButton() {
    wait = true;
    await fetch(getHttpUrl('/iot/button'), {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ tag: 'button2', PIDClearLog: 'Clear' }),
    });
    wait = false;
  }

  function viewDoors()     { $pidStore.endpoint = 'viewdoors';     goto('/level2/pidlog'); }
  function graphDoors()    { $pidStore.endpoint = 'graphdoors';    goto('/level2/pidlog'); }
  function downloadDoors() { $pidStore.endpoint = 'downloaddoors'; goto('/level2/pidlog'); }
  function viewRefrig()    { $pidStore.endpoint = 'viewrefrig';    goto('/level2/pidlog'); }
  function graphRefrig()   { $pidStore.endpoint = 'graphrefrig';   goto('/level2/pidlog'); }
  function downloadRefrig(){ $pidStore.endpoint = 'downloadrefrig';goto('/level2/pidlog'); }

  const toggleSize = (typeof window !== 'undefined' && window.innerWidth < 680) ? 'md' : 'lg';
</script>

<div class="pform pform--{theme}">
  <Card class="mx-auto w-full flex flex-col">
    <Table class="text-size-xl">
      <Row>
        <Column class="w-1/2 border-r border-gray-400">{ $t('level2.pid.fresh-air-doors') }</Column>
        <Column class="flex items-center">
          <div class="ml-auto flex items-center">
            { $t('level2.pid.logging') }:
            <SlideToggle size={toggleSize} class="ml-2 touch-interactive" name="Doors"
              background="bg-gray-400" active="bg-primary-900"
              bind:checked={recordDoors} on:change={saveLogToggles} data-touch-interactive="true" />
          </div>
          <Button size="xl" class="mx-4" on:click={viewDoors}>{ $t('level2.pid.view') }</Button>
          <Button size="xl" class="mx-4" on:click={graphDoors}>{ $t('level2.pid.graph') }</Button>
          <Button size="xl" class="mr-auto" on:click={downloadDoors}>{ $t('global.download') }</Button>
        </Column>
      </Row>
      <Row>
        <Column class="w-1/2 border-r border-gray-400">{ $t('global.refrigeration') }</Column>
        <Column class="flex items-center">
          <div class="ml-auto flex items-center">
            { $t('level2.pid.logging') }:
            <SlideToggle size={toggleSize} class="ml-2 touch-interactive" name="Refrig"
              background="bg-gray-400" active="bg-primary-900"
              bind:checked={recordRefrig} on:change={saveLogToggles} data-touch-interactive="true" />
          </div>
          <Button size="xl" class="mx-4" on:click={viewRefrig}>{ $t('level2.pid.view') }</Button>
          <Button size="xl" class="mx-4" on:click={graphRefrig}>{ $t('level2.pid.graph') }</Button>
          <Button size="xl" class="mr-auto" on:click={downloadRefrig}>{ $t('global.download') }</Button>
        </Column>
      </Row>
      <Row>
        <Column class="border-r border-gray-400">{ $t('level2.pid.clear-pid-log') }</Column>
        <Column><Button size="xl" on:click={postClearButton}>{ $t('global.clear') }</Button></Column>
      </Row>
      <Row>
        <Column class="border-r border-gray-400">{ $t('level2.pid.overwrite-oldest-records-if-necessary') }</Column>
        <Column class="flex items-center">
          <Select size="xl" extended="mx-auto w-64" bind:value={pid.pidWrap} edit={true}>
            <option value="0">{ $t('global.no') }</option>
            <option value="1">{ $t('global.yes') }</option>
          </Select>
        </Column>
      </Row>
      <Row>
        <Column class="xl:text-xl 3xl:text-2xl" colspan={2}>
          { $t('level2.pid.pid-overwrite-oldest-note') }
        </Column>
      </Row>
    </Table>
    <SaveButton bind:this={saveBtn} edit={true} bind:wait={wait} data={pid} bind:original={original} route="pid" autoSave
      onSave={async (d: { pidWrap: string, logDoors: string, logRefrig: string }) => {
        const wrap      = parseInt(d.pidWrap,   10) || 0;
        const logDoors  = parseInt(d.logDoors,  10) || 0;
        const logRefrig = parseInt(d.logRefrig, 10) || 0;
        await writeProto(TAG.PidLogSettings, { wrap, logDoors, logRefrig });
      }} />
  </Card>
</div>
