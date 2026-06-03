<script lang="ts">
  // 2026-06-03: Modal that asks the operator for a target % when they
  // pick Close or Open from the doors Equipment Control row. Save
  // writes DoorSettings.manual_pct via /proto/write/19, then the
  // caller fires the EquipmentCmd that sets remote_off=MANUAL on
  // EQ.DOORS. Firmware drives PWM_DOORS to that % via the post-mode
  // override in `lp_engine_shim.c::lp_engine_tick`.
  import Button from "$lib/ui/Button.svelte";
  import { TAG } from "$lib/business/protoTags";
  import { writeProto } from "$lib/business/protoWrite";
  import { doorSettings } from "$lib/business/protoStores";
  import { get } from "svelte/store";
  import { t } from "svelte-i18n";

  export let open: boolean = false;
  export let initialPct: number = 0;
  /** Fired after a successful save with the target %. The page-level
   * handler runs the EquipmentCmd that flips remote_off to MANUAL. */
  export let onSave: (pct: number) => Promise<void> | void = () => {};

  let inputValue: string = String(initialPct);
  let saving = false;
  let error = '';

  $: if (open) {
    inputValue = String(initialPct);
    error = '';
  }

  async function commit() {
    const n = parseInt(inputValue, 10);
    if (Number.isNaN(n) || n < 0 || n > 100) {
      error = $t('global.invalid-percent') || 'Enter 0-100';
      return;
    }
    saving = true;
    try {
      const current = get(doorSettings);
      // Round-trip the whole DoorSettings shape so other fields aren't
      // wiped by the proto3 partial encoder dropping unset values.
      const next = {
        ...(current ?? {
          pGain: 0, iGain: 0, dGain: 0, uLimit: 0,
          actuatorTime: 0, coolAirCycle: 0, manualPct: 0,
        }),
        manualPct: n,
      };
      await writeProto(TAG.DoorSettings, next);
      await onSave(n);
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
    <div class="bg-white rounded-lg p-6 shadow-2xl min-w-[24rem]">
      <h2 class="text-size-xl font-bold mb-4">
        {$t('level2.pid.fresh-air-doors')} — {$t('global.target')}
      </h2>
      <label class="block mb-3 text-size-xl">
        % (0 – 100)
        <input
          type="number"
          min="0"
          max="100"
          step="1"
          bind:value={inputValue}
          class="w-full mt-1 px-3 py-2 border rounded text-size-xl"
          disabled={saving}
          autofocus
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
