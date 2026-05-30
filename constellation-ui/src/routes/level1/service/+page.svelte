<script lang="ts">
  import GellertPage from "$lib/components/GellertPage.svelte";
  import Card from "$lib/ui/Card.svelte";
  import Table from "$lib/ui/Table.svelte";
  import Row from "$lib/ui/Row.svelte";
  import Column from "$lib/ui/Column.svelte";
  import { navigationStore } from "$lib/store";
  import { t } from "svelte-i18n";
  import { serviceInfo } from "$lib/business/protoStores";

  let title = $t('level1.service.sales-and-service-information');

  // L1 service page is read-only by design — editing lives in /level2/service.
  $: ready = $serviceInfo !== null;
  $: wait = false;
  $: level = $navigationStore.level;
</script>

<GellertPage {wait} {ready} {title} {level} name="service">
  <Card class="xl:w-3/4 xl:mx-auto md:mx-2 mt-2 flex flex-col">
    {#if $serviceInfo}
    <Table class="mb-2">
      <Row class="flex flex-row text-size-xl">
        <Column class="w-1/2 border-r border-gray-400"><div class="py-2">{ $t('level1.service.local-gellert-dealer') }</div></Column>
        <Column>{$serviceInfo.dealerName ?? ''}</Column>
      </Row>
      <Row class="flex flex-row text-size-xl">
        <Column class="w-1/2 border-r border-gray-400"><div class="py-2">{ $t('level1.service.office-phone-number') }</div></Column>
        <Column>{$serviceInfo.dealerPhone ?? ''}</Column>
      </Row>
      <Row class="flex flex-row text-size-xl">
        <Column class="w-1/2 border-r border-gray-400"><div class="py-2">{ $t('level1.service.primary-service-tech') }</div></Column>
        <Column>{$serviceInfo.techName ?? ''}</Column>
      </Row>
      <Row class="flex flex-row text-size-xl">
        <Column class="w-1/2 border-r border-gray-400"><div class="py-2">{ $t('level1.service.primary-service-tech-number') }</div></Column>
        <Column>{$serviceInfo.techPhone ?? ''}</Column>
      </Row>
    </Table>
    {/if}
  </Card>
</GellertPage>
