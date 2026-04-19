<script lang="ts">
	import { onMount } from "svelte";
  import GellertPage from "$lib/components/GellertPage.svelte";
	import Card from "$lib/ui/Card.svelte";
	import Table from "$lib/ui/Table.svelte";
	import Row from "$lib/ui/Row.svelte";
	import { navigationStore } from "$lib/store";
  import { getHttpUrl } from "$lib/business/util";
	import Column from "$lib/ui/Column.svelte";
	import SaveButton from "$lib/components/SaveButton.svelte";
	import { cloneDeep, isEqual } from "lodash-es";
	import { goto } from "$app/navigation";
	import { browser } from "$app/environment";
  import { t } from "svelte-i18n";
  import ScrollableArea from "$lib/components/ScrollableArea.svelte";

  export let data: { alerts: boolean[], labels: string[] };

  const title = $t('level1.alerts.select-email-alerts');

  // Always treat incoming data.labels as an array; fallback to [] so SSR never breaks
  $: alerts = [] as boolean[];
  $: labels = Array.isArray(data.labels) ? data.labels : [];
  // Sentinel 'group2' divides primary / secondary alert groups. If not present, all go in primary.
  $: group2Index = labels.indexOf('group2');
  $: primaryLabels = group2Index >= 0 ? labels.slice(0, group2Index) : labels;
  // Secondary labels exclude the sentinel itself.
  $: secondaryLabels = group2Index >= 0 ? labels.slice(group2Index + 1) : [];
  $: ready = false;
  $: wait = false;
  $: level = $navigationStore.level;
  $: edit = $navigationStore.level > 0;
  $: if (!edit && browser) {
    goto('email');
  }

  onMount(async () => {
    try {
      $navigationStore.data = getHttpUrl('/iot/alerts');
      $navigationStore.isDirty = () => !isEqual(alerts, data.alerts);
      alerts = cloneDeep(data.alerts);
    } catch (e) {
      console.error(e);
    }
		ready = true;
  });
</script>

<GellertPage {wait} {ready} {title} {level} name="alerts">
  <Card class="mb-0 w-3/4 mx-auto flex flex-col">
    <ScrollableArea>
      <Table class="mb-0 text-size-xl">
        <Row>
          <Column class="w-1/6 font-bold border-r border-gray-400">
            { $t('global.enable') }
          </Column>
          <Column class="w-5/6 font-bold">
            { $t('level1.alerts.primary-system-alerts') }
          </Column>
        </Row>
        {#each primaryLabels as _, index}
          <Row>
            <Column class="w-1/6 border-r border-gray-400">
              <input type="checkbox" bind:checked={alerts[index]} class="checkbox w-8 h-8" />
            </Column>
            <Column class="w-5/6 !text-left pl-4">
              {labels[index]}
            </Column>
          </Row>
        {/each}
      </Table>
      <Table class="mb-2 text-size-xl">
        <Row>
          <Column class="w-1/6 border-r border-gray-400 font-bold">
            { $t('global.enable') }
          </Column>
          <Column class="w-5/6 font-bold">
            { $t('level1.alerts.secondary-system-alerts') }
          </Column>
        </Row>
        {#each secondaryLabels as label, index}
          <Row>
            <Column class="w-1/6 border-r border-gray-400">
              <input type="checkbox" bind:checked={alerts[index + primaryLabels.length]} class="checkbox w-8 h-8" />
            </Column>
            <Column class="w-5/6 !text-left pl-4">
              {label}
            </Column>
          </Row>
        {/each}
      </Table>
      <svelte:fragment slot="footer-center">
        <SaveButton {edit} bind:wait={wait} data={alerts} bind:original={data.alerts} route="alerts" autoSave />
      </svelte:fragment>
    </ScrollableArea>
  </Card>
</GellertPage>



