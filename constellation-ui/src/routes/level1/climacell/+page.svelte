<script lang="ts">
	import { onMount } from "svelte";
  import GellertPage from "$lib/components/GellertPage.svelte";
	import Card from "$lib/ui/Card.svelte";
	import Button from "$lib/ui/Button.svelte";
	import { navigationStore } from "$lib/store";
	import Table from "$lib/ui/Table.svelte";
	import Row from "$lib/ui/Row.svelte";
  import Column from "$lib/ui/Column.svelte";
	import RunTime from "$lib/components/RunTime.svelte";
	import SaveButton from "$lib/components/SaveButton.svelte";
  import { isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
  import { FooterNavigationAdapter } from "$lib/utils/footerNavigationAdapter";
  import { climacellTimes as climacellTimesStore } from "$lib/business/protoStores";
  import { writeProtoRaw } from "$lib/business/protoWrite";
  import { TAG } from "$lib/business/protoTags";

  let title = $t('level1.climacell.climacell-control');

  $: ready = false;

  $: wait = false;

  $: edit = $navigationStore.level > 0;

  $: level = $navigationStore.level;

  $: currentRunOperation = '2';

  let runtimes: string[] = [];
  let original: string[] = [];
  let hydrated = false;

  $: if (!hydrated && $climacellTimesStore) {
    const slots = $climacellTimesStore.hourlyEfficiency ?? [];
    runtimes = Array.from({ length: 48 }, (_, i) => String(slots[i] ?? '1'));
    original = [...runtimes];
    hydrated = true;
    ready = true;
  }

  let operationColor: Record<string, string> = {
    '1': 'bg-red-500 !text-black',
    '2': 'bg-green-500 !text-black',
    '3': 'bg-lime-300 !text-black',
    '4': 'bg-blue-400 !text-black',
  };

  function getOperationColor(value: string, doHighLight: boolean): string {
    return operationColor[value] + (doHighLight ? ' ring-4 ring-white' : '');
  }

  onMount(() => {
    $navigationStore.isDirty = () => !isEqual(runtimes, original);
    if (hydrated) ready = true;
  });

  function selectAll() {
    runtimes = runtimes.map((_) => currentRunOperation);
  }

  function off() {
    currentRunOperation = '1';
  }

  function on() {
    currentRunOperation = '2';
  }

  function auto() {
    currentRunOperation = '3';
  }

  function cooling() {
    currentRunOperation = '4';
  }

  async function handleNextPage() {
    await FooterNavigationAdapter.navigateToNext(level, 'climacell');
  }

  async function handlePreviousPage() {
    await FooterNavigationAdapter.navigateToPrevious(level, 'climacell');
  }
</script>

<GellertPage {wait} {ready} {title} {level} name="climacell">
  <Card class="mx-auto mt-2 flex flex-col container-full">
    <Table class="mb-2 text-size-xl">
      <Row>
        <Column class="w-1/12 px-2" rowspan={2}>AM</Column>
        <Column class="w-11/12" rowspan={2}>
          <RunTime isAM={true} bind:runtimes={runtimes} {edit} {currentRunOperation} {getOperationColor} on:nextPage={handleNextPage} on:previousPage={handlePreviousPage}/>
        </Column>
      </Row>
    </Table>
    <Table class="mb-2 text-size-xl">
      <Row>
        <Column class="w-1/12 px-2fh" rowspan={2}>PM</Column>
        <Column class="w-11/12" rowspan={2}>
          <RunTime isAM={false} bind:runtimes={runtimes} {edit} {currentRunOperation} {getOperationColor} on:nextPage={handleNextPage} on:previousPage={handlePreviousPage}/>
        </Column>
      </Row>
    </Table>
    {#if edit}
      <Button class="mx-auto !{getOperationColor(currentRunOperation, false)}" size="xl" on:click={selectAll}>{ $t('level1.climacell.always-on') }</Button>
    {/if}
    <div class="flex flex-row text-center mx-auto">
      <Button class="!bg-red-500 !text-black {currentRunOperation === '1' && 'ring-4 ring-red-600'}" size="xl" on:click={off} disabled={!edit}>{ $t('global.off') }</Button>
      <Button class="ml-4 !bg-green-500 !text-black {currentRunOperation === '2' && 'ring-4 ring-green-600'}" size="xl" on:click={on} disabled={!edit}>{ $t('global.on') }</Button>
      <Button class="ml-4 !bg-lime-300 !text-black {currentRunOperation === '3' && 'ring-4 ring-lime-500'}" size="xl" on:click={auto} disabled={!edit}>{ $t('global.auto') }</Button>
      <Button class="ml-4 !bg-blue-400 !text-black {currentRunOperation === '4' && 'ring-4 ring-blue-500'}" size="xl" on:click={cooling} disabled={!edit}>{ $t('level1.climacell.cooling-only') }</Button>
    </div>
    <SaveButton {edit} bind:wait={wait} data={runtimes} bind:original={original} autoSave
      onSave={async (d: string[]) => {
        /* Firmware decoder reads field 1 as repeated UNPACKED varints
         * (one tag 0x08 + varint per slot, 48 slots). proto3 default
         * "packed" encoding from ts-proto would be silently ignored, so
         * we hand-build the inner bytes and bypass the typed encoder.
         * See apply_climacell_times in nova_dataexc.c. */
        const buf: number[] = [];
        for (let i = 0; i < 48; i++) {
          const v = Number(d[i] ?? 1) | 0;
          buf.push(0x08); // (1 << 3) | wireType 0 (varint)
          // value range 1..4 → single-byte varint
          buf.push(v & 0x7f);
        }
        await writeProtoRaw(TAG.ClimacellTimes, new Uint8Array(buf));
      }} />
  </Card>
</GellertPage>


