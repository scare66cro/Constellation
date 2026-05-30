<script lang="ts">
	import { goto } from '$app/navigation';
  import GellertPage from '$lib/components/GellertPage.svelte';
	import Button from '$lib/ui/Button.svelte';
	import Card from '$lib/ui/Card.svelte';
	import Column from '$lib/ui/Column.svelte';
	import Row from '$lib/ui/Row.svelte';
	import Table from '$lib/ui/Table.svelte';
	import { SlideToggle } from '@skeletonlabs/skeleton';
	import { onMount } from 'svelte';
	import Select from '$lib/ui/Select.svelte';
  import { navigationStore, pidStore } from '$lib/store';
  import { getHttpUrl } from "$lib/business/util";
  import { pidLogSettings } from "$lib/business/protoStores";
  import { writeProto } from "$lib/business/protoWrite";
  import { TAG } from "$lib/business/protoTags";
	import SaveButton from '$lib/components/SaveButton.svelte';
  import { cloneDeep, isEqual } from "lodash-es";
  import { t } from "svelte-i18n";

  let title = $t('level2.pid.pid-logs');

  $: recordDoors = false;

  $: recordRefrig = false;

  $: ready = false;
  $: wait = false;
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

  onMount(async () => {
    try {
      $navigationStore.isDirty = () => !isEqual(pid, original);
      if (hydrated) ready = true;
    } catch (error) {
      console.error(error);
    }
  });

  // Toggle handlers: proto-direct save of PidLogSettings (TAG=67, sfield=37).
  // Force-encode logDoors/logRefrig because 0 is a meaningful value (Off);
  // proto3 zero-suppression would otherwise drop it and the firmware
  // in-place patcher would leave the previous ON state intact.
  // wrap is also force-encoded for the same reason — even though we're
  // not touching it, sending the partial-message overrides require all
  // three fields to keep the firmware row consistent.
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
  // The /iot/button shape is preserved for now; once /api/logs/clear-pid
  // gets a typed route this can move to a fetch on that path.
  async function postClearButton() {
    wait = true;
    await fetch(getHttpUrl('/iot/button'), {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json'
      },
      body: JSON.stringify({
        tag: 'button2',
        PIDClearLog: 'Clear',
      }),
    });
    wait = false;
  }

  function viewDoors() {
    $pidStore.endpoint = 'viewdoors';
    goto('/level2/pidlog' );
  }

  function graphDoors() {
    $pidStore.endpoint = 'graphdoors';
    goto('/level2/pidlog');
  }

  function downloadDoors() {
    $pidStore.endpoint = 'downloaddoors';
    goto('/level2/pidlog');
  }

  function viewRefrig() {
    $pidStore.endpoint = 'viewrefrig';
    goto('/level2/pidlog');
  }

  function graphRefrig() {
    $pidStore.endpoint = 'graphrefrig';
    goto('/level2/pidlog');
  }

  function downloadRefrig() {
    $pidStore.endpoint = 'downloadrefrig';
    goto('/level2/pidlog');
  }
</script>

<GellertPage {wait} {title} {ready} level={2} name="pid">
  <Card class="mx-2 flex flex-col">
    <Table class="text-size-xl">
      <Row>
        <Column class="w-1/2 border-r border-gray-400">{ $t('level2.pid.fresh-air-doors') }</Column>
        <Column class="flex items-center">
          <div class="ml-auto flex items-center">
            { $t('level2.pid.logging') }:
            <SlideToggle
              size={window.innerWidth < 680 ? 'md' : 'lg'}
              class="ml-2 touch-interactive"
              name="Doors"
              background="bg-gray-400"
              active="bg-primary-900"
              bind:checked={recordDoors}
              on:change={saveLogToggles}
              data-touch-interactive="true"
            />
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
            <SlideToggle
              size={window.innerWidth < 680 ? 'md' : 'lg'}
              class="ml-2 touch-interactive"
              name="Refrig"
              background="bg-gray-400"
              active="bg-primary-900"
              bind:checked={recordRefrig}
              on:change={saveLogToggles}
              data-touch-interactive="true"
            />
          </div>
          <Button size="xl" class="mx-4" on:click={viewRefrig}>{ $t('level2.pid.view') }</Button>
          <Button size="xl"class="mx-4" on:click={graphRefrig}>{ $t('level2.pid.graph') }</Button>
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
    <SaveButton edit={true} bind:wait={wait} data={pid} bind:original={original}  route="pid" autoSave
      onSave={async (d: { pidWrap: string, logDoors: string, logRefrig: string }) => {
        // wrap/logDoors/logRefrig are bool toggles where 0 = OFF.
        // forceFieldRegistry.ts pins all three on the wire so OFF
        // propagates correctly (proto3 would otherwise drop the 0).
        const wrap      = parseInt(d.pidWrap,   10) || 0;
        const logDoors  = parseInt(d.logDoors,  10) || 0;
        const logRefrig = parseInt(d.logRefrig, 10) || 0;
        await writeProto(TAG.PidLogSettings, { wrap, logDoors, logRefrig });
      }} />
  </Card>
</GellertPage>




