<script lang="ts">
  // 3-column row for bay-light equipment on the Equipment Status page.
  // Replaces the EquipmentRow AUTO/OFF/MANUAL dropdown with the same
  // Toggle-button + on/off status pattern the dedicated Level 1 Bay
  // Lights page used to render. Consolidates two control surfaces into
  // one, matches the 2-state nature of the underlying lights button
  // (bridge dispatches `lights1Btn=Toggle` via `lightsButtonNextState`
  // regardless of what the dropdown said).
  //
  // Inputs:
  //   - equipmentName : user-renamed label from IO Config
  //                     (`renamedAs(io.LIGHTS1, defaultLabel)`)
  //   - statusOn      : current-sense relay (inputOn) — "are the lights
  //                     physically on?" under the active-high polarity
  //                     convention from `mode.ts` (INPUT_GOOD='1').
  //   - btn           : 'lights1Btn' | 'lights2Btn'
  //   - outputColor   : background-color hint for the name column,
  //                     same convention as EquipmentRow.
  //   - exists / edit : same gating as EquipmentRow.
  import Button from "$lib/ui/Button.svelte";
  import Column from "$lib/ui/Column.svelte";
  import Row from "$lib/ui/Row.svelte";
  import { t } from "svelte-i18n";
  import { getHttpUrl } from "$lib/business/util";

  export let exists: boolean = false;
  export let name: string = '';
  export let equipmentName: string = '';
  export let statusOn: boolean = false;
  export let remSwitchName: string = '';
  export let outputColor: string = 'bg-gray-200';
  export let edit: boolean = false;
  export let wait: boolean = false;

  $: statusColor = statusOn
    ? 'text-green-700 font-bold'
    : 'text-red-500 font-bold';

  async function toggle() {
    if (!remSwitchName) return;
    wait = true;
    await fetch(getHttpUrl('/iot/button'), {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({
        tag: 'button2',
        [remSwitchName]: 'Toggle',
      }),
    });
    wait = false;
  }
</script>

{#if exists}
  <Row>
    <Column class="border-r border-gray-400 {outputColor}">
      {equipmentName}
    </Column>
    <Column class="border-r border-gray-400 {statusColor}">
      {statusOn ? $t('global.on') : $t('global.off')}
    </Column>
    <Column>
      {#if edit}
        <Button size="xl" on:click={toggle}>{$t('global.toggle')}</Button>
      {/if}
    </Column>
  </Row>
{/if}
