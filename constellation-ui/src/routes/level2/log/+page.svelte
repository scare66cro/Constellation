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
  import { userLogSettings } from "$lib/business/protoStores";
  import { useDraft, numField } from "$lib/business/useDraft";
  import { TAG } from "$lib/business/protoTags";
  import { isEqual } from "lodash-es";
  import { t } from "svelte-i18n";

  let title = $t('level2.log.log-settings');
  let validation = { recInterval: '' };

  $: edit = true;
  $: wait = false;
  $: ready = false;

  // UserLogSettings draft (tag 59).
  const userLog = useDraft(userLogSettings, TAG.UserLogSettings);
  const { draft, live, hydrated } = userLog;
  const intervalStr = numField(draft, 'intervalMinutes', 'int');
  // yes/no options use string values — bridge `enabled` (uint32) through a
  // numField so the Select can two-way bind to its string-shaped option set.
  const enabledStr = numField(draft, 'enabled', 'int');

  onMount(() => {
    $navigationStore.isDirty = () => !isEqual($draft, $live);
    ready = true;
  });

  // SdCardInit removed Apr 2026: Nova LP-AM2434 has no SD card. The
  // legacy AS2 button shape is gone from the UI as a permanent
  // architectural fact. History/activity logs live in pg on rpi5
  // (see /memories/repo/pg-logging-on-rpi5.md) and the clear buttons
  // below TRUNCATE the corresponding tables via the bridge.

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
            <TextField class="w-24" size="xl" label="Log Interval" bind:value={$intervalStr} {edit} keyboardType={KeyboardTypes.Numeric} validation={validation.recInterval}/>
            { $t('global.minutes') }
          </p>
        </Column>
      </Row>
      <Row>
        <Column>
          <p class="text-center">
            { $t('level2.log.overwrite-old-records-if-necessary') }: <Select class="w-48 xl:w-64" size="xl" bind:value={$enabledStr} {edit} options={$yesNoOptionsStore}/>
          </p>
        </Column>
      </Row>
    </Table>
    <Table>
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
    <SaveButton {edit} bind:wait={wait} data={$draft} original={$live} route="log" bind:validation={validation} autoSave
      onSave={() => userLog.save()} />
  </Card>
</GellertPage>



