<script lang="ts">
	import { onDestroy, onMount } from "svelte";
  import GellertPage from "$lib/components/GellertPage.svelte";
	import Card from "$lib/ui/Card.svelte";
	import { navigationStore, modeToColorStore } from "$lib/store";
	import Table from "$lib/ui/Table.svelte";
  import Row from "$lib/ui/Row.svelte";
  import Column from "$lib/ui/Column.svelte";
	import TextField from "$lib/ui/TextField.svelte";
	import { AdornmentType } from "$lib/business/adornmentType";
  import RefreshList from "$lib/components/RefreshList.svelte";
  import { processNetworkMonitorData, type NetworkPanel } from "$lib/business/network";
  import { networkComposite } from "$lib/business/protoStores";
  import { navigateToStoragePanel, extractStorageInfo } from "$lib/business/util";
  import { t } from "svelte-i18n";
  import { goto } from "$app/navigation";

  let title = $t('level1.network.network-monitor');

  $: ready = false;
  $: wait = false;
  $: level = $navigationStore.level;
  $: network = [] as NetworkPanel[];

  onMount(() => {
    // S9g: was a WsClient subscription to `network-data`. The bridge
    // stub never had live peer telemetry; the proto-direct composite
    // delivers the same shape (LocalIp/Mask + configured node list)
    // and updates reactively when networkConfig/networkNodes change.
    const unsub = networkComposite.subscribe((data) => {
      if (!data) return;
      const { panels } = processNetworkMonitorData(data);
      network = panels;
      ready = true;
    });
    return unsub;
  });

  onDestroy(() => {});

  function tempColor(color: string): string {
    let retcolor = 'black';
    if (color === '1') {
      retcolor = 'green';
    } else if (color === '2') {
      retcolor = 'red';
    } else if (color === '3') {
      retcolor = '#ff7f00';
    }
    return retcolor;
  }

  function humidColor(color: string, value: string): string {
    let retcolor = 'black';
    if (value === 'dis') {
      retcolor = '#B4B4B4';
    } else if (color === '1') {
      retcolor = 'green';
    } else if (color === '2') {
      retcolor = 'red';
    }
    return retcolor;
  }

  function modeColor(mode: string): string {
    if (mode === '') {
      return 'red';
    }
    return $modeToColorStore[parseInt(mode, 10)].color;
  }

  function modeText(mode: string): string {
    if (mode === '') {
      return $t('level1.network.no-communication');
    }
    return $modeToColorStore[parseInt(mode, 10)].text;
  }

  async function gotoStoragePanel(storageString: string) {
    const { ip } = extractStorageInfo(storageString);
    
    if (!ip) return;
    
    // Use the utility function with custom same-panel behavior
    await navigateToStoragePanel(
      ip, 
      '/', 
      true, // addRedirectParam for GellertHeader style navigation
      async () => {
        // Custom behavior for same panel navigation
        $navigationStore.level = 0;
        $navigationStore.name = '';
        $navigationStore.dropDownPage = '';
        await goto('/');
      }
    );
  }

  async function refreshList() {
    // S9g: was POST /iot/network (no-op stub on the bridge anyway). The
    // composite is reactive — every networkConfig / networkNodes update
    // refreshes the panel list automatically. Brief wait flash for UX.
    wait = true;
    await new Promise((r) => setTimeout(r, 100));
    wait = false;
  }
</script>

<!-- svelte-ignore -->
<GellertPage {wait} {ready} {title} {level} name='network' action={RefreshList as any} on:click={refreshList}>
  <Card class="mt-2 flex flex-col mx-2">
    <Table class="mb-2">
      <Row class="font-bold text-size-large">
        <Column class="w-4/12">{ $t('level1.network.storage') }</Column><Column class="w-4/12">{ $t('global.mode') }</Column>
        <Column class="w-1/12">{ $t('level1.network.set') }</Column><Column class="w-1/12">{ $t('level1.network.plen') }</Column>
        <Column class="w-1/12">{ $t('level1.network.ret') }</Column><Column class="w-1/12">{ $t('level1.network.hum') }</Column>
      </Row>
      {#each network as net}
        <Row class="text-size-large">
          <Column>
            <button 
              class="text-left text-blue-600 hover:text-blue-800 hover:underline cursor-pointer w-full bg-transparent border-none p-0"
              on:click={() => gotoStoragePanel(net.storage)}
              title="Click to navigate to this storage panel"
            >
              {net.storage}
            </button>
          </Column>
          <Column style={`color: ${modeColor(net.mode)}`}>
              {modeText(net.mode)}
          </Column>
          <Column>
            {#if net.mode !== ''}
              <TextField class="font-normal" value={net.set} edit={false} adornmentType={AdornmentType.Temperature} />
            {:else}
              <span style="color: red">!</span>
            {/if}
          </Column>
          <Column style={`color: ${tempColor(net.tempColor)}`}>
            {#if net.plen !== ''}
              <TextField class="font-normal" value={net.plen} edit={false} adornmentType={AdornmentType.Temperature} />
            {:else}
              <span style="color: red">!</span>
            {/if}
          </Column>
          <Column>
            {#if net.mode !== ''}
              <TextField class="font-normal" value={net.ret} edit={false} adornmentType={AdornmentType.Temperature}/>
            {:else}
              <span style="color: red">!</span>
            {/if}
          </Column>
          <Column style={`color: ${humidColor(net.humidColor, net.hum)}`}>
            {#if net.hum !== ''}
              <TextField class="font-normal" value={net.hum} edit={false} adornmentType={AdornmentType.Percent}/>
            {:else}
              <span style="color: red">!</span>
            {/if}
          </Column>
        </Row>
      {/each}
    </Table>
  </Card>
</GellertPage>
