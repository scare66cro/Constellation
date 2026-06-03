<script lang="ts">
  // 2026-06-03: Modal the operator sees when picking Close or Open
  // from the Fresh Air Doors Equipment Control row. Two inputs:
  //
  //   Target %      — drives PWM_DOORS to this value when firmware
  //                   sees remote_off=MANUAL on EQ_DOORS. Close
  //                   defaults to 0, Open defaults to 100.
  //   Timeout (min) — optional. 0 = stay manual until the operator
  //                   flips back to AUTO. >0 = firmware auto-reverts
  //                   to AUTO after N minutes (close + PID reset).
  //                   Replaces the legacy door-diag 60-min clear.
  //
  // Save writes DoorSettings (manual_pct + manual_timeout_mins) via
  // /proto/write/19, then calls back so the caller can flip
  // remote_off to MANUAL via the equipment command.
  import Button from "$lib/ui/Button.svelte";
  import { TAG } from "$lib/business/protoTags";
  import { writeProto } from "$lib/business/protoWrite";
  import { doorSettings } from "$lib/business/protoStores";
  import { get } from "svelte/store";
  import { t } from "svelte-i18n";

  export let open: boolean = false;
  export let initialPct: number = 0;
  /** Fired after a successful save. The caller runs the EquipmentCmd
   * that flips remote_off=MANUAL on EQ.DOORS. */
  export let onSave: (pct: number, timeoutMins: number) => Promise<void> | void = () => {};

  let pctInput: string = String(initialPct);
  let timeoutInput: string = '0';
  let saving = false;
  let error = '';

  $: if (open) {
    pctInput = String(initialPct);
    // Seed timeout from current persisted value so the operator's last
    // pick is the default — first-boot default is 0 (= persistent).
    const cur = get(doorSettings);
    timeoutInput = String(cur?.manualTimeoutMins ?? 0);
    error = '';
  }

  async function commit() {
    const pct = parseInt(pctInput, 10);
    const timeout = parseInt(timeoutInput, 10);
    if (Number.isNaN(pct) || pct < 0 || pct > 100) {
      error = $t('global.invalid-percent') || 'Target must be 0-100';
      return;
    }
    if (Number.isNaN(timeout) || timeout < 0 || timeout > 1440) {
      error = 'Timeout must be 0-1440 minutes';
      return;
    }
    saving = true;
    try {
      const current = get(doorSettings);
      const next = {
        ...(current ?? {
          pGain: 0, iGain: 0, dGain: 0, uLimit: 0,
          actuatorTime: 0, coolAirCycle: 0,
          manualPct: 0, manualTimeoutMins: 0,
        }),
        manualPct: pct,
        manualTimeoutMins: timeout,
      };
      await writeProto(TAG.DoorSettings, next);
      await onSave(pct, timeout);
      open = false;
    } catch (e: any) {
      error = e?.message ?? 'Save failed';
    } finally {
      saving = false;
    }
  }

  function cancel() {
    if (!saving) open = false;
  }
</script>

{#if open}
  <div
    class="fixed inset-0 z-50 flex items-center justify-center bg-black/60"
    on:click|self={cancel}
    on:keydown={(e) => { if (e.key === 'Escape') cancel(); }}
    role="dialog"
    tabindex="-1"
  >
    <div class="bg-white rounded-lg p-6 shadow-2xl min-w-[26rem]">
      <h2 class="text-size-xl font-bold mb-4">
        {$t('level2.pid.fresh-air-doors')} — {$t('global.manual')}
      </h2>
      <label class="block mb-3 text-size-xl">
        {$t('global.target')} % (0 – 100)
        <input
          type="number"
          min="0"
          max="100"
          step="1"
          bind:value={pctInput}
          class="w-full mt-1 px-3 py-2 border rounded text-size-xl"
          disabled={saving}
          autofocus
        />
      </label>
      <label class="block mb-3 text-size-xl">
        Auto-revert to AUTO after (minutes, 0 = stay manual)
        <input
          type="number"
          min="0"
          max="1440"
          step="1"
          bind:value={timeoutInput}
          class="w-full mt-1 px-3 py-2 border rounded text-size-xl"
          disabled={saving}
        />
      </label>
      {#if error}
        <p class="text-red-600 text-sm mb-3">{error}</p>
      {/if}
      <div class="flex justify-end gap-2 mt-4">
        <Button size="xl" on:click={cancel} disabled={saving}>
          {$t('global.cancel')}
        </Button>
        <Button size="xl" on:click={commit} disabled={saving}>
          {saving ? '…' : $t('global.save')}
        </Button>
      </div>
    </div>
  </div>
{/if}
