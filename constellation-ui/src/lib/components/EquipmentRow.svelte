<script lang="ts">
	import { getHttpUrl } from "$lib/business/util";
	import Column from "$lib/ui/Column.svelte";
	import Row from "$lib/ui/Row.svelte";
	import { t } from "svelte-i18n";

  export let exists: boolean = false;
  export let name: string = '';
  export let equipmentName: string = '';
  export let equipmentStatus: string = '';
  export let panelSwitchStatus = '--';
  export let equipOn: boolean = true;
  export let remoteStatus: string = '0';
  export let remSwitchName: string = '';
  export let outputColor = 'bg-gray-200';
  export let statusColor = 'bg-gray-200';
  export let panelSwitchColor = 'bg-gray-200';
  export let edit: boolean = false;
  export let wait: boolean = false;

  // Derive 3-way mode from remoteStatus:
  // '0' = auto (ARM controls), '1' = off (forced off), '2' = manual (forced on)
  let switchMode: 'auto' | 'off' | 'manual' = 'auto';

  $: switchMode = remoteStatus === '2' ? 'manual' : remoteStatus === '1' ? 'off' : 'auto';

  async function onModeChange() {
    if (!remSwitchName) return;
    wait = true;
    const value = switchMode === 'auto' ? 'Auto' : switchMode === 'manual' ? 'On' : 'Off';
    await fetch(getHttpUrl('/iot/button'), {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        tag: 'button2',
        [remSwitchName]: value,
      }),
    });
    wait = false;
  }

  $: modeColor = switchMode === 'auto' ? 'text-green-700 font-bold'
               : switchMode === 'manual' ? 'text-blue-700 font-bold'
               : 'text-red-500 font-bold';
</script>

{#if exists}
  <Row>
    <Column class="border-r border-gray-400 {outputColor}">
      {equipmentName}
    </Column>
    <Column class="border-r border-gray-400 {statusColor}">
      {equipmentStatus}
    </Column>
    <Column class="{panelSwitchColor}">
      {#if edit && remSwitchName}
        <select
          class="w-full text-center text-size-xl py-1 rounded border border-gray-300 {modeColor}"
          bind:value={switchMode}
          on:change={onModeChange}
        >
          <option value="auto">{$t('global.auto')}</option>
          <option value="off">{$t('global.off')}</option>
          <option value="manual">{$t('global.manual')}</option>
        </select>
      {:else}
        <span class="{panelSwitchColor}">{panelSwitchStatus}</span>
      {/if}
    </Column>
  </Row>
{/if}