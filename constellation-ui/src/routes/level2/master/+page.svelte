<script lang="ts">
  import GellertPage from "$lib/components/GellertPage.svelte";
  import Card from "$lib/ui/Card.svelte";
  import Select from "$lib/ui/Select.svelte";
  import SaveButton from "$lib/components/SaveButton.svelte";
  import Button from "$lib/ui/Button.svelte";
	import { onMount } from "svelte";
	import Table from "$lib/ui/Table.svelte";
	import Row from "$lib/ui/Row.svelte";
	import Column from "$lib/ui/Column.svelte";
	import { navigationStore } from "$lib/store";
  import { getHttpUrl } from "$lib/business/util";
  import { cloneDeep, isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
  import { type ArrayResponse } from "$lib/business/util";

  export let data: ArrayResponse;

  let level = 2;
  let title = $t('level2.master.master-slave-local-mode-setup');
  let options = [
    { text: $t('level2.master.local-default'), value: '0' },
    { text: $t('level2.master.master'), value: '1' },
    { text: $t('level2.master.slave'), value: '2' },
  ];
  $: masters  = [] as Array<{text: string, value: string}>;
  $: masterData = [] as string[];

  $: ready = false;
  $: wait = false;

  $: disabled = masterData[0] !== '2';
  
  // Build master list from the same WebSocket-fed navigation nodes used by Remote Systems
  function updateMastersFromNavigationNodes() {
    masters = [];
    const localIP = $navigationStore.localIP || '';

    for (const node of $navigationStore.nodes || []) {
      const ip = node.value.split(':')[0];
      // Skip the current/local panel to avoid duplicates or invalid self entries
      if (!ip || ip === localIP) continue;

      const octets = ip.split('.');
      const suffix = octets.length === 4 ? octets[3] : ip; // fall back gracefully if malformed
      masters.push({ text: `${node.text} (...${suffix})`, value: ip });
    }
  }

  async function find() {
    wait = true;
    await fetch(getHttpUrl('/iot/button'), {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
      },
      body: JSON.stringify({ tag: 'findNodes' }),
    });

    // Trigger backend discovery; actual list will come via tcpip-data WebSocket (filtered/validated)
    await fetch(getHttpUrl(`/iot/node`));

    // Use the WebSocket-fed navigationStore (same data as Remote Systems) to populate list
    updateMastersFromNavigationNodes();
    wait = false;
  }

  onMount(async () => {
    $navigationStore.data = getHttpUrl(`/iot/master`);
    $navigationStore.isDirty = () => !isEqual(masterData, data.array);
    masterData = cloneDeep(data.array);

    if (masterData[0] === '2') {
      ready = true;
      await find();
    }
    ready = true;
  });

  // Keep masters list in sync with navigationStore nodes (already filtered like Remote Systems)
  $: if (masterData[0] === '2') {
    updateMastersFromNavigationNodes();
  }
</script>

<GellertPage {wait} {ready} {title} {level} name="master" >
  <Card class="text-xl mx-auto flex flex-col w-3/4 mt-2">
    <Table class="mb-2 text-size-xl">
      <Row>
        <Column class="border-r border-gray-400">{ $t('global.mode') }:</Column>
        <Column class="px-2"><Select class="w-128" size="xl" {options} bind:value={masterData[0]} edit={true}/></Column>
      </Row>
      <Row>
        <Column class="border-r border-gray-400">{ $t('level2.master.master') }:</Column>
        <Column class="px-2"><Select size="xl" extended="w-full" options={masters} bind:value={masterData[1]} edit={true} {disabled}/></Column>
      </Row>
      <Row>
        <Column colspan={2}>
          { $t('global.discover-agri-star-system-controllers') } <Button size="xl" class="ml-2" on:click={find}>{ $t('global.find') }</Button>
        </Column>
      </Row>
    </Table>
    <SaveButton edit={true} bind:wait={wait} route="master" data={masterData} bind:original={data.array} autoSave/>
  </Card>
</GellertPage>




