<script lang="ts">
	import { onMount } from "svelte";
  import GellertPage from "$lib/components/GellertPage.svelte";
	import Card from "$lib/ui/Card.svelte";
	import Table from "$lib/ui/Table.svelte";
	import Row from "$lib/ui/Row.svelte";
	import Column from "$lib/ui/Column.svelte";
	import { navigationStore } from "$lib/store";
  import { getHttpUrl } from "$lib/business/util";
	import SaveButton from "$lib/components/SaveButton.svelte";
	import { cloneDeep, isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
	import type { ArrayResponse } from "$lib/business/util";

  export let data: ArrayResponse;

  let title = $t('level1.service.sales-and-service-information');

  $: service = [] as string[];
  $: ready = false;
  $: wait = false;
  $: level = $navigationStore.level;
  $: edit = $navigationStore.level > 1;

  onMount(async () => {
		try {
      $navigationStore.data = getHttpUrl('/iot/service');
      $navigationStore.isDirty = () => !isEqual(service, data.array);
      service = cloneDeep(data.array);
		} catch (error) {
      console.error(error);
		}
		ready = true;
  });
</script>

<GellertPage {wait} {ready} {title} {level} name="service">
  <Card class="xl:w-3/4 xl:mx-auto md:mx-2 mt-2 flex flex-col">
    {#if service.length > 3}
    <Table class="mb-2">
      <Row class="flex flex-row text-size-xl">
        <Column class="w-1/2 border-r border-gray-400"><div class="py-2">{ $t('level1.service.local-gellert-dealer') }</div></Column>
        <Column>{service[0]}</Column>
      </Row>
      <Row class="flex flex-row text-size-xl">
        <Column class="w-1/2 border-r border-gray-400"><div class="py-2">{ $t('level1.service.office-phone-number') }</div></Column>
        <Column>{service[1]}</Column>
      </Row>
      <Row class="flex flex-row text-size-xl">
        <Column class="w-1/2 border-r border-gray-400"><div class="py-2">{ $t('level1.service.primary-service-tech') }</div></Column>
        <Column>{service[2]}</Column>
      </Row>
      <Row class="flex flex-row text-size-xl">
        <Column class="w-1/2 border-r border-gray-400"><div class="py-2">{ $t('level1.service.primary-service-tech-number') }</div></Column>
        <Column>{service[3]}</Column>
      </Row>
    </Table>
    <SaveButton {edit} bind:wait={wait} data={service} bind:original={data.array} route="service" autoSave />
    {/if}
  </Card>
</GellertPage>


