<script lang="ts">
  import GellertPage from "$lib/components/GellertPage.svelte";
	import Card from "$lib/ui/Card.svelte";
	import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
	import TextField from "$lib/ui/TextField.svelte";
	import Button from "$lib/ui/Button.svelte";
	import { goto } from "$app/navigation";
  import { pidStore } from "$lib/store";
  import { t } from "svelte-i18n";

  let title = $t('level2.pid.pid-logs');

  let startLog = '1';
  let endLog = '2000';
  let dataEndpoint = $pidStore.endpoint;
  let returnTo = $pidStore.returnPage;

  $: ready = true;

  function returnToPage() {
    goto(returnTo);
  }

  function getData() {
    $pidStore.startLog = startLog;
    $pidStore.endLog = endLog;
    switch(dataEndpoint) {
      case 'viewdoors':
        $pidStore.type = '0';
        goto('/level2/table');
        break;
      case 'viewrefrig':
        $pidStore.type = '1';
        goto('/level2/table');
        break;
      case 'graphdoors':
        $pidStore.type = '0';
        goto('/level2/graph');
        break;
      case 'graphrefrig':
        $pidStore.type = '1';
        goto('/level2/graph');
        break;
      case 'downloaddoors':
        $pidStore.type = '0';
        goto('/level2/download');
        break;
      case 'downloadrefrig':
        $pidStore.type = '1';
        goto('/level2/download');
        break;
    }
  }
</script>

<GellertPage {title} {ready} level={2} name="pidlog">
  <Card class="mx-2 flex flex-col">
    <div class="flex flex-row items-center mb-2">
      <div class="mx-auto text-size-xl">{ $t('level2.pidlog.records-to-display-or-download') }: <TextField size="xl" class="w-36 mr-2" bind:value={startLog} keyboardType={KeyboardTypes.Numeric} edit={true}/> - <TextField size="xl" class="w-48 mr-2" bind:value={endLog} keyboardType={KeyboardTypes.Numeric} edit={true}/></div>
    </div>
    <div class="flex flex-row items-center">
      <Button size="xl" class="mx-auto" on:click={getData}>{ $t('level2.pidlog.get-data') }</Button>
    </div>
  </Card>
</GellertPage>
