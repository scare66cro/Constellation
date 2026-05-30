<script lang="ts">
	import { onMount } from "svelte";
  import GellertPage from "$lib/components/GellertPage.svelte";
	import Card from "$lib/ui/Card.svelte";
	import Button from "$lib/ui/Button.svelte";
	import { frontMatterStore, navigationStore } from "$lib/store";

	import Table from "$lib/ui/Table.svelte";
	import Row from "$lib/ui/Row.svelte";
  import Column from "$lib/ui/Column.svelte";
	import RunTime from "$lib/components/RunTime.svelte";
	import SaveButton from "$lib/components/SaveButton.svelte";
	import { cloneDeep, isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
  import { FooterNavigationAdapter } from "$lib/utils/footerNavigationAdapter";
  import { runtimes as runtimesStore } from "$lib/business/protoStores";
  import { writeProtoRaw } from "$lib/business/protoWrite";
  import { TAG } from "$lib/business/protoTags";

  // Phase 5.1: hydration migrated to $runtimes proto store. The legacy
  // `+page.ts` loader was deleted — this page no longer needs a `data`
  // prop; SaveButton binds against `originalRuntimes` for dirty tracking.

  let title = $t('level1.runclock.system-run-clock');

  $: ready = false;

  $: wait = false;

  $: edit = $navigationStore.level > 0;

  $: level = $navigationStore.level;

  $: currentRunOperation = '3'; // standby

  $: runtimes = [] as string[];
  // Snapshot of the last hydrated value (used as `original` for SaveButton
  // dirty-tracking and post-save baseline reset).
  let originalRuntimes: string[] = [];

  $: onionMode = ($frontMatterStore?.panel as string[])?.[8] === '1';

  let operationColor: Record<string, string> = {
    '1': 'bg-green-500 !text-black',
    '2': 'bg-purple-500 !text-black',
    '3': 'bg-yellow-500 !text-black',
    '4': 'bg-blue-400 !text-black',
    '5': 'bg-orange-500 !text-black',
  };

  function getOperationColor(value: string, doHighLight: boolean): string {
    return operationColor[value] + (doHighLight ? ' ring-4 ring-white' : '');
  }

  /** Convert proto Runtimes payload → legacy 48-element string[] keyed
   * by half-hour slot. Slot defaults to '3' (standby) when missing. */
  function runtimesProtoToArray(rt: { entries: { slot: number; mode: number }[] } | null): string[] {
    const out = new Array<string>(48).fill('3');
    if (!rt) return out;
    for (const e of rt.entries) {
      if (e.slot >= 0 && e.slot < 48) out[e.slot] = String(e.mode || 3);
    }
    return out;
  }

  onMount(() => {
    $navigationStore.isDirty = () => !isEqual(runtimes, originalRuntimes);
    const unsub = runtimesStore.subscribe((rt) => {
      // Live-push: only re-baseline while the user has no pending edits;
      // otherwise we would clobber an in-progress change.
      if (!$navigationStore.isDirty?.()) {
        const next = runtimesProtoToArray(rt as any);
        originalRuntimes = next;
        runtimes = cloneDeep(next);
        ready = true;
      }
    });
    return () => unsub();
  });

  function selectAll() {
    runtimes = runtimes.map((_) => currentRunOperation);
  }

  function cooling() {
    currentRunOperation = '1';
  }

  function recirculation() {
    currentRunOperation = '2';
  }

  function standby() {
    currentRunOperation = '3';
  }

  function refrigeration() {
    currentRunOperation = '4';
  }

  function cure() {
    currentRunOperation = '5';
  }

  async function handleNextPage() {
    await FooterNavigationAdapter.navigateToNext(level, 'runclock');
  }

  async function handlePreviousPage() {
    await FooterNavigationAdapter.navigateToPrevious(level, 'runclock');
  }
</script>

<GellertPage {wait} {ready} {title} {level} name="runclock">
  <Card class="mx-auto mt-2 flex flex-col container-full">
    <Table class="mb-2 text-size-xl">
      <Row>
        <Column class="w-1/12 px-2" rowspan={2}>AM</Column>
        <Column class="w-11/12" rowspan={2}>
          <RunTime isAM={true} bind:runtimes={runtimes} {edit} {currentRunOperation} {getOperationColor} on:nextPage={handleNextPage} on:previousPage={handlePreviousPage}/>
        </Column>
      </Row>
    </Table>
    <Table class="text-size-xl">
      <Row>
        <Column class="w-1/12 px-2" rowspan={2}>PM</Column>
        <Column class="w-11/12" rowspan={2}>
          <RunTime isAM={false} bind:runtimes={runtimes} {edit} {currentRunOperation} {getOperationColor} on:nextPage={handleNextPage} on:previousPage={handlePreviousPage}/>
        </Column>
      </Row>
    </Table>
    {#if edit}
      <Button class="mx-auto !my-1 !{getOperationColor(currentRunOperation, false)}" size="xl" on:click={selectAll} {edit}>{ $t('level1.runclock.select-all') }</Button>
    {/if}
    <div class="flex flex-row text-center mx-auto my-1">
      {#if onionMode}
        <Button class="!my-1 !bg-yellow-500 !text-black {currentRunOperation === '3' && 'ring-4 ring-yellow-600'}" size="xl" on:click={standby} disabled={!edit}>{ $t('global.standby') }</Button>
        <Button class="ml-4 !my-1 !bg-orange-500 !text-black {currentRunOperation === '5'&& 'ring-4 ring-orange-700'}" size="xl" on:click={cure} disabled={!edit}>{ $t('global.cure') }</Button>
      {:else}
        <Button class="!my-1 !bg-green-500 !text-black {currentRunOperation === '1' && 'ring-4 ring-green-600'}" size="xl" on:click={cooling} disabled={!edit}>{ $t('global.cooling') }</Button>
        <Button class="ml-4 !my-1 !bg-purple-500 !text-black {currentRunOperation === '2' && 'ring-4 ring-purple-600'}" size="xl" on:click={recirculation} disabled={!edit}>{ $t('global.recirculation') }</Button>
        <Button class="ml-4 !my-1 !bg-yellow-500 !text-black {currentRunOperation === '3' && 'ring-4 ring-yellow-600'}" size="xl" on:click={standby} disabled={!edit}>{ $t('global.standby') }</Button>
        <Button class="ml-4 !my-1 !bg-blue-400 !text-black {currentRunOperation === '4' && 'ring-4 ring-blue-500'}" size="xl" on:click={refrigeration} disabled={!edit}>{ $t('global.refrigeration') }</Button>
      {/if}
    </div>
    <SaveButton {edit} bind:wait={wait} data={runtimes} bind:original={originalRuntimes} autoSave
      onSave={async (d: string[]) => {
        /* LpSettings_ApplyRuntime (Nova_Firmware/lp_am2434/lp_settings.c)
         * reads RuntimeSettings field 1 as a *repeated submessage* — each
         * inner blob is a RuntimeEntry { slot=1 (varint), mode=2 (varint) }.
         * Slot is explicit, not positional. Encoding mirrors
         * LpSettings_BuildRuntimeBody (same file) so a save round-trips
         * bit-identically through the LP settings vault and is replayed
         * on reconnect.
         *
         * Slot is force-encoded (slot 0 is valid). Mode is also force-
         * encoded so RC_OFF (=0) slots aren't zero-suppressed away.
         */
        const buf: number[] = [];
        for (let i = 0; i < 48; i++) {
          const mode = (Number(d[i] ?? 3) | 0) & 0x7f;
          // inner: tag(1<<3|0=varint) slot tag(2<<3|0=varint) mode
          const inner = [0x08, i & 0x7f, 0x10, mode];
          // outer: tag(1<<3|2=length-delim) len(=4) inner...
          buf.push(0x0a, inner.length, ...inner);
        }
        await writeProtoRaw(TAG.Runtimes, new Uint8Array(buf));
      }} />
  </Card>
</GellertPage>


