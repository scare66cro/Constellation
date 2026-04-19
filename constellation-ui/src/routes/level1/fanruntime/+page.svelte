<script lang="ts">
  import GellertPage from "$lib/components/GellertPage.svelte";
	import Card from "$lib/ui/Card.svelte";
	import Button from "$lib/ui/Button.svelte";
	import Table from "$lib/ui/Table.svelte";
  import TextField from "$lib/ui/TextField.svelte";
	import Row from "$lib/ui/Row.svelte";
	import Column from "$lib/ui/Column.svelte";
	import { frontMatterStore, navigationStore } from "$lib/store";
	import { AdornmentType } from "$lib/business/adornmentType";
	import { SYSTEM_MODE } from "$lib/business/mode";
  import { t } from "svelte-i18n";

  let title = $t('level1.fanruntime.fan-runtimes');
  let mode = ($frontMatterStore?.panel as string[])?.[8];
  $: ready = $frontMatterStore !== undefined && $frontMatterStore.panel !== undefined;
  $: wait = false;
  $: level = $navigationStore.level;
  $: edit = $navigationStore.level > 0;
  $: panel = $frontMatterStore.panel as string[];

  async function reset(type: string) {
    const response = await fetch(`/iot/reset/${type}`, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
      },
    });
  }
</script>

<GellertPage {wait} {ready} {title} {level} name="fanruntime">
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
</GellertPage>