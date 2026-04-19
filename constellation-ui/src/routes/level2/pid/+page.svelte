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
	import SaveButton from '$lib/components/SaveButton.svelte';
  import { cloneDeep, isEqual } from "lodash-es";
  import { t } from "svelte-i18n";

  export let data: { pidWrap: string, logDoors: string, logRefrig: string };

  let title = $t('level2.pid.pid-logs');

  $: recordDoors = false;

  $: recordRefrig = false;

  $: ready = false;
  $: wait = false;
  $: pid = {} as { pidWrap: string, logDoors: string, logRefrig: string };

  onMount(async () => {
    try {
      $navigationStore.data = getHttpUrl(`/iot/pid`);
      $navigationStore.isDirty = () => !isEqual(pid, data);
      pid = cloneDeep(data);
      recordDoors = pid.logDoors === '1';
      recordRefrig = pid.logRefrig === '1';
    } catch (error) {
      console.error(error);
    }
    ready = true;
  });

  async function postDoorsButton() {
    wait = true;
    await fetch(getHttpUrl('/iot/button'), {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json'
      },
      body: JSON.stringify({
        tag: 'button2',
        btnPIDDoorLog: recordDoors ? 'Turn On': 'Turn Off',
      }),
    });
    pid.logDoors = recordDoors ? '1' : '0';
    wait = false;
  }

  async function postRefrigButton() {
    wait = true;
    await fetch(getHttpUrl('/iot/button'), {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json'
      },
      body: JSON.stringify({
        tag: 'button2',
        btnPIDRefrigLog: recordRefrig ? 'Turn On': 'Turn Off',
      }),
    });
    pid.logRefrig = recordRefrig ? '1' : '0';
    wait = false;
  }

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
              on:change={postDoorsButton}
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
              on:change={postRefrigButton}
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
    <SaveButton edit={true} bind:wait={wait} data={pid} bind:original={data}  route="pid" autoSave />
  </Card>
</GellertPage>




