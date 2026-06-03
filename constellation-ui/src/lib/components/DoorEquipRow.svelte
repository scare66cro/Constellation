<script lang="ts">
  // 2026-06-03: Custom Equipment Status row for fresh-air doors.
  // Same 3-column shape as EquipmentRow but intercepts the Close/Open
  // dropdown picks to show DoorTargetModal — the operator types a
  // target % which gets persisted to DoorSettings.manual_pct before
  // the equipment command flips remote_off to MANUAL. Firmware drives
  // PWM_DOORS to that % via the post-mode override in
  // `lp_engine_shim.c::lp_engine_tick`.
  //
  // AUTO is still a direct POST (no modal); the firmware handles the
  // "AUTO closes the doors first then engine PID resumes" behavior
  // via the MANUAL→AUTO transition path in lp_engine_shim.c.
  import { getHttpUrl } from "$lib/business/util";
  import Column from "$lib/ui/Column.svelte";
  import Row from "$lib/ui/Row.svelte";
  import { t } from "svelte-i18n";
  import DoorTargetModal from "./DoorTargetModal.svelte";

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
  export let offLabel: string = '';
  export let manualLabel: string = '';

  let switchMode: 'auto' | 'off' | 'manual' = 'auto';
  $: switchMode = remoteStatus === '2'
    ? 'manual'
    : remoteStatus === '1'
      ? 'off'
      : 'auto';

  let modalOpen = false;
  let modalInitialPct = 0;
  let pendingMode: 'off' | 'manual' | null = null;

  async function postModeNow(value: 'Auto' | 'Off' | 'On') {
    if (!remSwitchName) return;
    wait = true;
    await fetch(getHttpUrl('/iot/button'), {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ tag: 'button2', [remSwitchName]: value }),
    });
    wait = false;
  }

  async function onModeChange() {
    // AUTO is direct — firmware handles the close+reset on transition.
    if (switchMode === 'auto') {
      await postModeNow('Auto');
      return;
    }
    // Close / Open both pop the modal — the wire is the same MANUAL
    // state with a different target %. Close defaults the modal to 0,
    // Open defaults to 100. If the operator cancels we revert the
    // dropdown back to the last live remoteStatus.
    pendingMode = switchMode;
    modalInitialPct = switchMode === 'off' ? 0 : 100;
    modalOpen = true;
  }

  async function onModalSave(_pct: number) {
    // Modal already wrote DoorSettings.manual_pct via writeProto.
    // Send the equipment command to flip remote_off → MANUAL=2.
    // (Both Close and Open route through MANUAL — the % is the
    // operator's real intent, the label is just the default.)
    await postModeNow('On');
    pendingMode = null;
  }

  // When the modal closes without saving, snap the dropdown back to
  // the live state so the UI doesn't lie about what the firmware has.
  $: if (!modalOpen && pendingMode !== null) {
    pendingMode = null;
    switchMode = remoteStatus === '2'
      ? 'manual'
      : remoteStatus === '1'
        ? 'off'
        : 'auto';
  }

  $: modeColor = switchMode === 'auto'
    ? 'text-green-700 font-bold'
    : switchMode === 'manual'
      ? 'text-blue-700 font-bold'
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
          <option value="off">{offLabel || $t('global.off')}</option>
          <option value="manual">{manualLabel || $t('global.manual')}</option>
        </select>
      {:else}
        <span class="{panelSwitchColor}">{panelSwitchStatus}</span>
      {/if}
    </Column>
  </Row>
  <DoorTargetModal
    bind:open={modalOpen}
    initialPct={modalInitialPct}
    onSave={onModalSave}
  />
{/if}
