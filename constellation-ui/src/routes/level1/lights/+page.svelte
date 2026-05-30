<script lang="ts">
  import GellertPage from "$lib/components/GellertPage.svelte";
  import Card from "$lib/ui/Card.svelte";
  import { onMount } from "svelte";
  import { frontMatterStore, navigationStore } from "$lib/store";
  import { getHttpUrl } from "$lib/business/util";
  import Table from "$lib/ui/Table.svelte";
  import Row from "$lib/ui/Row.svelte";
  import Column from "$lib/ui/Column.svelte";
  import TextField from "$lib/ui/TextField.svelte"
  import SaveButton from "$lib/components/SaveButton.svelte";
  import Button from "$lib/ui/Button.svelte";
  import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
  import { isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
  import { loadMonitorSettings } from "$lib/business/protoStores";
  import { useDraft } from "$lib/business/useDraft";
  import { TAG } from "$lib/business/protoTags";

  let title = $t('level1.lights.bay-light-control');

  let validation = { lightsBay1Label: '', lightsBay2Label: '' };

  // SETTINGS_FIELD[TAG.LoadMonitorSettings] = 20 → apply_load_monitor()
  // in nova_dataexc.c writes Settings.LoadMonitor.Bay[0..1].Label.
  const lm = useDraft(loadMonitorSettings, TAG.LoadMonitorSettings);
  const { draft, live, hydrated } = lm;

  $: level = $navigationStore?.level;
  $: ready = false;
  $: wait = false;
  $: edit = $navigationStore?.level > 0;
  $: main = $frontMatterStore?.main as string[];
  $: statusColor1 = getStatusColor(main?.[19]);
  $: statusColor2 = getStatusColor(main?.[21]);

  onMount(() => {
    $navigationStore.isDirty = () => !isEqual($draft, $live);
    ready = true;
  });

  function getStatusColor(status: string): string {
    return status === '0' ? 'text-green-500 font-bold' : 'text-red-500 font-bold';
  }

  async function postButton(lights: number) {
    wait = true;
    const name = lights === 1 ? 'lights1Btn' : 'lights2Btn';
    const result = await fetch(getHttpUrl('/iot/button'), {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json'
      },
      body: JSON.stringify({
        tag: 'button2',
        [name]: 'Toggle',
      })
    });
    wait = false;
  }
</script>

<GellertPage {wait} {ready} {title} {level} name="lights">
  <Card class="mt-2 flex flex-col mx-auto container-standard">
    <Table class="text-size-xl">
      <Row>
        <Column class="w-5/12">{ $t('global.output') }</Column>
        <Column class="w-5/12">{ $t('global.status') }</Column>
        <Column class="w-2/12">{ $t('global.control') }</Column>
      </Row>
      <Row>
        <Column><TextField size="xl" bind:value={$draft.bay1Label} {edit} keyboardType={KeyboardTypes.Alpha} validation={validation.lightsBay1Label}/></Column>
        <Column><span class={statusColor1}>{main?.[19] === '0' ? $t('global.on') : $t('global.off')}</span></Column>
        <Column>
          <Button size="xl" on:click={() => postButton(1)}>{ $t('global.toggle') }</Button>
        </Column>
      </Row>
      <Row>
        <Column><TextField size="xl" bind:value={$draft.bay2Label} {edit} keyboardType={KeyboardTypes.Alpha} validation={validation.lightsBay2Label}/></Column>
        <Column><span class={statusColor2}>{main?.[21] === '0' ? $t('global.on') : $t('global.off')}</span></Column>
        <Column>
          <Button size="xl" on:click={() => postButton(2)}>{ $t('global.toggle') }</Button>
        </Column>
      </Row>
    </Table>
    <SaveButton {edit} bind:wait={wait} data={$draft} original={$live} bind:validation={validation} autoSave
      onSave={() => lm.save()} />
  </Card>
</GellertPage>


