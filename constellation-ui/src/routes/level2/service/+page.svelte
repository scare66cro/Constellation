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
  import { isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
  import { serviceInfo } from "$lib/business/protoStores";
  import { useDraft } from "$lib/business/useDraft";
  import { TAG } from "$lib/business/protoTags";

  // Proto-direct page state. Replaces `service: string[4]` positional shape.
  const svc = useDraft(serviceInfo, TAG.ServiceInfo);
  const { draft, hydrated, live } = svc;

  let title = $t('level2.service.sales-and-service-setup');
  let edit = true;

  let validation = { dealerName: '', dealerPhone: '', techName: '', techPhone: '' };

  $: ready = false;
  $: wait = false;

  onMount(() => {
    $navigationStore.isDirty = () => !isEqual($draft, $live);
    ready = true;
  });
</script>

<GellertPage {wait} {ready} {title} level={2} name="service">
  <Card class="xl:w-3/4 xl:mx-auto md:mx-2 mt-2 flex flex-col">
    {#if $hydrated}
    <Table class="text-size-xl">
      <Row class="flex flex-row">
        <Column class="w-1/2 border-r border-gray-400"><div class="py-2">{ $t('level1.service.local-gellert-dealer') }</div></Column>
        <Column class="w-1/2"><TextField size="xl" extended="w-11/12 mx-2" bind:value={$draft.dealerName} {edit} label="Local Gellert Dealer" keyboardType={KeyboardTypes.Alpha} validation={validation.dealerName}/></Column>
      </Row>
      <Row class="flex flex-row">
        <Column class="w-1/2 border-r border-gray-400"><div class="py-2">{ $t('level1.service.office-phone-number') }</div></Column>
        <Column class="w-1/2"><TextField size="xl" extended="w-11/12 mx-2" bind:value={$draft.dealerPhone} {edit} label="Office Phone Number" keyboardType={KeyboardTypes.Alpha} validation={validation.dealerPhone} /></Column>
      </Row>
      <Row class="flex flex-row">
        <Column class="w-1/2 border-r border-gray-400"><div class="py-2">{ $t('level1.service.primary-service-tech') }</div></Column>
        <Column class="w-1/2"><TextField size="xl" extended="w-11/12 mx-2" bind:value={$draft.techName} {edit} label="Primary Service Tech" keyboardType={KeyboardTypes.Alpha} validation={validation.techName}/></Column>
      </Row>
      <Row class="flex flex-row">
        <Column class="w-1/2 border-r border-gray-400"><div class="py-2">{ $t('level1.service.primary-service-tech-number') }</div></Column>
        <Column class="w-1/2"><TextField size="xl" extended="w-11/12 mx-2" bind:value={$draft.techPhone} {edit} label="Primary Service Tech Number" keyboardType={KeyboardTypes.Alpha} validation={validation.techPhone} /></Column>
      </Row>
    </Table>
    <SaveButton {edit} bind:wait={wait} data={$draft} original={$live} bind:validation={validation}
                onSave={() => svc.save()} autoSave />
    {/if}
  </Card>
</GellertPage>
