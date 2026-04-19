<script lang="ts">
	import { onMount } from "svelte";
  import GellertPage from "$lib/components/GellertPage.svelte";
	import Card from "$lib/ui/Card.svelte";
	import Table from "$lib/ui/Table.svelte";
	import Row from "$lib/ui/Row.svelte";
	import Column from "$lib/ui/Column.svelte";
	import TextField from "$lib/ui/TextField.svelte";
	import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
	import SaveButton from "$lib/components/SaveButton.svelte";
	import { navigationStore } from "$lib/store";
  import { getHttpUrl } from "$lib/business/util";
  import { cloneDeep, isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
	import type { ArrayResponse } from "$lib/business/util";
	
  export let data: ArrayResponse;

  let title = $t('level2.service.sales-and-service-setup');

  let edit = true;

  let validation = {
    dealerName: '',
    dealerPhone: '',
    techName: '',
    techPhone: '',
  };

  $: ready = false;
  $: wait = false;
  $: service = [] as string[];

  onMount(async () => {
		try {
      $navigationStore.data = getHttpUrl(`/iot/service`);
      $navigationStore.isDirty = () => !isEqual(service, data.array);
      service = cloneDeep(data.array);
		} catch (error) {
      console.error(error);
		}
		ready = true;
  });
</script>

<GellertPage {wait} {ready} {title} level={2} name="service">
  <Card class="xl:w-3/4 xl:mx-auto md:mx-2 mt-2 flex flex-col">
    <Table class="text-size-xl">
      <Row class="flex flex-row">
        <Column class="w-1/2 border-r border-gray-400"><div class="py-2">{ $t('level1.service.local-gellert-dealer') }</div></Column>
        <Column class="w-1/2"><TextField size="xl" extended="w-11/12 mx-2" bind:value={service[0]} {edit} label="Local Gellert Dealer" keyboardType={KeyboardTypes.Alpha} validation={validation.dealerName}/></Column>
      </Row>
      <Row class="flex flex-row">
        <Column class="w-1/2 border-r border-gray-400"><div class="py-2">{ $t('level1.service.office-phone-number') }</div></Column>
        <Column class="w-1/2"><TextField size="xl" extended="w-11/12 mx-2" bind:value={service[1]} {edit} label="Office Phone Number" keyboardType={KeyboardTypes.Alpha} validation={validation.dealerPhone} /></Column>
      </Row>
      <Row class="flex flex-row">
        <Column class="w-1/2 border-r border-gray-400"><div class="py-2">{ $t('level1.service.primary-service-tech') }</div></Column>
        <Column class="w-1/2"><TextField size="xl" extended="w-11/12 mx-2" bind:value={service[2]} {edit} label="Primary Service Tech" keyboardType={KeyboardTypes.Alpha} validation={validation.techName}/></Column>
      </Row>
      <Row class="flex flex-row">
        <Column class="w-1/2 border-r border-gray-400"><div class="py-2">{ $t('level1.service.primary-service-tech-number') }</div></Column>
        <Column class="w-1/2"><TextField size="xl" extended="w-11/12 mx-2" bind:value={service[3]} {edit} label="Primary Service Tech Number" keyboardType={KeyboardTypes.Alpha} validation={validation.techPhone} /></Column>
      </Row>
    </Table>
    <SaveButton {edit} bind:wait={wait} data={service} bind:original={data.array} route="service" bind:validation={validation} autoSave/>
  </Card>
</GellertPage>


