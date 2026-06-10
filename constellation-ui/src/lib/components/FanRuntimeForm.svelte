<script lang="ts">
  import Card from "$lib/ui/Card.svelte";
  import Button from "$lib/ui/Button.svelte";
  import Table from "$lib/ui/Table.svelte";
  import TextField from "$lib/ui/TextField.svelte";
  import Row from "$lib/ui/Row.svelte";
  import Column from "$lib/ui/Column.svelte";
  import { frontMatterStore, navigationStore } from "$lib/store";
  import { getHttpUrl } from "$lib/business/util";
  import { AdornmentType } from "$lib/business/adornmentType";
  import { SYSTEM_MODE } from "$lib/business/mode";
  import { t } from "svelte-i18n";

  // Shared body of the Fan Runtimes page (level1/fanruntime): read-only
  // cumulative runtime hours (daily / total / per-mode) with Daily/Total reset
  // buttons when editable. Rendered from ONE source of truth — the classic page
  // AND a no-save tab/tile of the dashboard History & Logs hub. Prop contract
  // mirrors FanSpeedForm. docs/spatial-ui-page-migration.md
  export let wait = false;
  export let ready = false;
  export let embedded = false;
  export let theme: 'light' | 'dark' = 'light';
  export let canEdit: boolean | null = null;

  $: edit = canEdit ?? ($navigationStore.level > 0);
  $: ready = $frontMatterStore !== undefined && $frontMatterStore.panel !== undefined;
  $: panel = ($frontMatterStore?.panel as string[]) ?? [];
  $: mode = panel?.[8];

  // No-save view — nothing to flush (resets fire imperatively).
  export async function flush(): Promise<void> {}

  async function reset(type: string) {
    try {
      await fetch(getHttpUrl(`/iot/reset/${type}`), {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
      });
    } catch (e) {
      console.error('[fanruntime] reset failed:', e);
    }
  }
</script>

<div class="pform pform--{theme}">
  <Card class="mx-auto flex flex-col w-3/4 mt-2">
    <Table class="mb-3">
      <Row>
        <Column class="text-size-xl items-center border-r border-gray-400">{ $t('level1.fanruntime.daily-fan-runtime-since-noon') }</Column>
        <Column class="items-center border-r border-gray-400"><TextField size="xl" value={panel?.[1]} edit={false} adornmentType={AdornmentType.Hours} /></Column>
        {#if edit}
          <Column class="items-center"><Button size="xl" on:click={() => reset('Daily')}>{ $t('global.reset') }</Button></Column>
        {/if}
      </Row>
      <Row>
        <Column class="text-size-xl items-center border-r border-gray-400">{ $t('global.total-fan-runtime') }</Column>
        <Column class="items-center border-r border-gray-400"><TextField size="xl" value={panel?.[2]} edit={false} adornmentType={AdornmentType.Hours} /></Column>
        {#if edit}
          <Column class="items-center"><Button size="xl" on:click={() => reset('Total')}>{ $t('global.reset') }</Button></Column>
        {/if}
      </Row>
    </Table>
    <Table class="mb-2">
      <Row>
        <Column class="text-size-xl items-center border-r border-gray-400">{ $t('global.refrigeration') }</Column>
        <Column class="items-center border-r border-gray-400"><TextField size="xl" value={panel?.[3]} edit={false} adornmentType={AdornmentType.Hours} /></Column>
      </Row>
      <Row>
        <Column class="text-size-xl items-center border-r border-gray-400">{ $t('global.cooling') }</Column>
        <Column class="items-center border-r border-gray-400"><TextField size="xl" value={panel?.[4]} edit={false} adornmentType={AdornmentType.Hours} /></Column>
      </Row>
      <Row>
        <Column class="text-size-xl items-center border-r border-gray-400">{ $t('global.recirculation') }</Column>
        <Column class="items-center border-r border-gray-400"><TextField size="xl" value={panel?.[5]} edit={false} adornmentType={AdornmentType.Hours} /></Column>
      </Row>
      {#if mode === SYSTEM_MODE.ONION_MODE}
        <Row>
          <Column class="text-size-xl items-center border-r border-gray-400">{ $t('global.cure') }</Column>
          <Column class="items-center border-r border-gray-400"><TextField size="xl" value={panel?.[6]} edit={false} adornmentType={AdornmentType.Hours} /></Column>
        </Row>
      {/if}
      <Row>
        <Column class="text-size-xl items-center border-r border-gray-400">{ $t('global.standby') }</Column>
        <Column class="items-center border-r border-gray-400"><TextField size="xl" value={panel?.[7]} edit={false} adornmentType={AdornmentType.Hours} /></Column>
      </Row>
    </Table>
  </Card>
</div>
