<script lang="ts">
	import { onMount } from "svelte";
  import GellertPage from "$lib/components/GellertPage.svelte";
	import Card from "$lib/ui/Card.svelte";
	import Button from "$lib/ui/Button.svelte";
	import TextField from "$lib/ui/TextField.svelte";
  import Select from "$lib/ui/Select.svelte";
	import Table from "$lib/ui/Table.svelte";
	import Row from "$lib/ui/Row.svelte";
	import Column from "$lib/ui/Column.svelte";
	import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
	import SaveButton from "$lib/components/SaveButton.svelte";
	import { navigationStore } from "$lib/store";
  import { getHttpUrl } from "$lib/business/util";
  import { cloneDeep, isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
	import { invalidate } from "$app/navigation";

  export let data: { Timeout: string, AccessToken: string, Protocol: string, Version: string };

  let title = $t('level2.iotclient.iot-client-configuration');

  let edit = true;

  $: iotClient = { Timeout: '', AccessToken: '', Protocol: '', Version: '' };
  $: ready = false;
  $: wait = false;
  $: logs = '';

  onMount(async () => {
		try {
      $navigationStore.data = getHttpUrl(`/iot/config`);
      $navigationStore.isDirty = () => !isEqual(iotClient, data);
      iotClient = cloneDeep(data);
		} catch (error) {
      console.error(error);
		}
		ready = true;
  });

  async function getLogs() {
    wait = true;
    logs = await (await fetch(getHttpUrl(`/iot/logs`))).text();
    wait = false;
  }

  async function update() {
    /* `/iot/PostSave.jsp` was the legacy ARM2 bulk-commit endpoint; the
     * bridge handler was removed in the 2026-04-21 S9k cleanup
     * (apiRoutes.ts:1199-1201) on the assumption that no UI page still
     * called it. This page slipped through that audit and was POSTing
     * into a 404 on every save — silently, since we never checked
     * resp.ok. The actual save already happened via SaveButton's POST
     * to /iot/config (apiRoutes.ts:705) before this handler fired, so
     * functionality wasn't broken; this just removes the dead call.
     * `invalidate` re-runs +page.ts so the page-data prop refreshes
     * with whatever the bridge persisted. */
    await invalidate($navigationStore.data);
    iotClient = cloneDeep(data);
  }
</script>

<GellertPage {wait} {ready} {title} level={2} name="iotclient">
  <Card class="mx-2 flex flex-col">
    <Table class="text-size-xl">
      <Row>
        <Column class="w-1/2 border-r border-gray-400">{ $t('level2.iotclient.access-token') }</Column>
        <Column class="w-1/2">
          <TextField extended="mx-2" size="xl" bind:value={iotClient.AccessToken} {edit} label="Access Token" keyboardType={KeyboardTypes.Alpha}/>
        </Column>
      </Row>
      <Row>
        <Column class="w-1/2 border-r border-gray-400">{ $t('level2.iotclient.timeout-secs') }</Column>
        <Column class="w-1/2"><TextField extended="mx-2" size="xl" bind:value={iotClient.Timeout} {edit} label="Timeout in seconds" keyboardType={KeyboardTypes.Numeric}/></Column>
      </Row>
      <Row>
        <Column class="w-1/2 border-r border-gray-400">{ $t('level2.iotclient.protocol') }</Column>
        <Column class="w-1/2">
          <Select class="w-96" size="xl" bind:value={iotClient.Protocol} {edit}>
            <option value="mqtt">mqtt</option>
            <option value="http">http</option>
          </Select>
        </Column>
      </Row>
      <Row>
        <Column class="w-1/2 border-r border-gray-400 py-2">{ $t('level2.iotclient.iot-client-version') }</Column>
        <Column class="w-1/2 py-2"><TextField class="w-fit" size="xl" bind:value={iotClient.Version} edit={false}/></Column>
      </Row>
    </Table>
    <div class="flex flex-row">
      <SaveButton {edit} bind:wait={wait} data={iotClient} bind:original={data} route="config" on:complete={(e) => { if (e.detail.success) update(); }} autoSave />
    </div>
  </Card>
  <Card class="my-2 mx-2 flex flex-col flex-1">
    <div class="flex flex-row">
      <Button class="!my-0" size="xl" on:click={getLogs}>{ $t('level2.iotclient.get-logs') }</Button>
    </div>
    <textarea class="w-full h-full mt-2 mb-0 text-size-large" value={logs} disabled style="resize: none;" />
  </Card>
</GellertPage>



