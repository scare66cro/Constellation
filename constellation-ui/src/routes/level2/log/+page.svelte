<script lang="ts">
	import { onMount } from "svelte";
	import GellertPage from "$lib/components/GellertPage.svelte";
	import Card from "$lib/ui/Card.svelte";
	import Row from "$lib/ui/Row.svelte";
	import Column from "$lib/ui/Column.svelte";
	import TextField from "$lib/ui/TextField.svelte";
	import Table from "$lib/ui/Table.svelte";
	import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
	import Select from "$lib/ui/Select.svelte";
  import Button from "$lib/ui/Button.svelte";
	import SaveButton from "$lib/components/SaveButton.svelte";
	import { navigationStore, yesNoOptionsStore } from "$lib/store";
  import { getHttpUrl } from "$lib/business/util";
  import { cloneDeep, isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
	import type { ArrayResponse } from "$lib/business/util";

  export let data: ArrayResponse;

  let ready = false;
  let title = $t('level2.log.log-settings');
  let validation = {
    recInterval: ''
  };

  $: edit = true;
  $: wait = false;
  $: log = [] as string[];

  onMount(async () => {
    try {
      $navigationStore.data = getHttpUrl(`/iot/log`);
      $navigationStore.isDirty = () => !isEqual(log, data.array);
      log = cloneDeep(data.array);
    } catch (error) {
      console.error(error);
    }
    ready = true;
  });

  async function initializeSDCard() {
    wait = true;
    const responnse = await fetch(getHttpUrl('/iot/button'), {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
      },
      body: JSON.stringify({
        tag: 'button2',
        SdCardInit: 'Initalize SD Card',
      }),
    });
    wait = false;
  }

  async function clearHistoryLog() {
    wait = true;
    const responnse = await fetch(getHttpUrl('/iot/button'), {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
      },
      body: JSON.stringify({
        tag: 'button2',
        ClearUserLog: 'Clear History Log',
      }),
    });
    wait = false;
  }

  async function clearActivityLog() {
    wait = true;
    const responnse = await fetch(getHttpUrl('/iot/button'), {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
      },
      body: JSON.stringify({
        tag: 'button2',
        ClearSystemLog: 'Clear Activity Log',
      }),
    });
    wait = false;
  }
</script>

<GellertPage {wait} {title} {ready} level={2} name="log">
  <Card class="xl:w-3/4 md:mx-2 xl:mx-auto flex flex-col">
    <Table class="mb-2 text-size-xl">
      <Row>
        <Column class="py-2">
          { $t('level2.log.history-log-settings') }
        </Column>
      </Row>
      <Row>
        <Column>
          <p class="text-center">
            { $t('level2.log.a-record-will-be-taken-every') }
            <TextField class="w-24" size="xl" label="Log Interval" bind:value={log[0]} {edit} keyboardType={KeyboardTypes.Numeric} validation={validation.recInterval}/>
            { $t('global.minutes') }
          </p>
        </Column>
      </Row>
      <Row>
        <Column>
          <p class="text-center">
            { $t('level2.log.overwrite-old-records-if-necessary') }: <Select class="w-48 xl:w-64" size="xl" bind:value={log[1]} {edit} options={$yesNoOptionsStore}/>
          </p>
        </Column>
      </Row>
    </Table>
    <Table>
      <Row>
        <Column>
          <Button size="xl" class="w-1/3 mx-auto p-3" on:click={initializeSDCard}>{ $t('level2.log.initialize-sd-card') }</Button>
        </Column>
      </Row>
      <Row>
        <Column>
          <Button size="xl" class="w-1/3 mx-auto p-3" on:click={clearHistoryLog}>{ $t('level2.log.clear-history-log') }</Button>
        </Column>
      </Row>
      <Row>
        <Column>
          <Button size="xl" class="w-1/3 mx-auto p-3" on:click={clearActivityLog}>{ $t('level2.log.clear-activity-log') }</Button>
        </Column>
      </Row>
    </Table>
    <SaveButton {edit} bind:wait={wait} data={log} bind:original={data.array} route="log" bind:validation={validation} autoSave />
  </Card>
</GellertPage>



