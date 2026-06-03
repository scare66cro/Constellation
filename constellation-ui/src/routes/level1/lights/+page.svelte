<script lang="ts">
  import GellertPage from "$lib/components/GellertPage.svelte";
  import Card from "$lib/ui/Card.svelte";
  import { onMount } from "svelte";
  import { navigationStore } from "$lib/store";
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
  import { equipmentComposite, loadMonitorSettings } from "$lib/business/protoStores";
  import { EQ } from "$lib/business/equipmentEnum";
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
  // Live lights state from EquipmentStatus (typed proto). The bay
  // light wiring model on Constellation is "3-way switch" style:
  // the firmware drives an output coil (toggle command) and the
  // light starter has a current-sensing relay that reports back
  // through the INPUT. So "are the lights actually on?" reads from
  // EquipState.inputOn — current is flowing → relay closed → DI
  // high under the active-high polarity convention (mode.ts). If
  // an operator pulls the bulb or the breaker trips, output stays
  // on but inputOn drops to false and the status correctly shows
  // OFF.
  $: light1On = !!$equipmentComposite?.eqByIdx(EQ.LIGHTS1)?.inputOn;
  $: light2On = !!$equipmentComposite?.eqByIdx(EQ.LIGHTS2)?.inputOn;

  onMount(() => {
    $navigationStore.isDirty = () => !isEqual($draft, $live);
    ready = true;
  });

  function statusColor(on: boolean): string {
    return on ? 'text-green-500 font-bold' : 'text-red-500 font-bold';
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
        <Column><span class={statusColor(light1On)}>{light1On ? $t('global.on') : $t('global.off')}</span></Column>
        <Column>
          <Button size="xl" on:click={() => postButton(1)}>{ $t('global.toggle') }</Button>
        </Column>
      </Row>
      <Row>
        <Column><TextField size="xl" bind:value={$draft.bay2Label} {edit} keyboardType={KeyboardTypes.Alpha} validation={validation.lightsBay2Label}/></Column>
        <Column><span class={statusColor(light2On)}>{light2On ? $t('global.on') : $t('global.off')}</span></Column>
        <Column>
          <Button size="xl" on:click={() => postButton(2)}>{ $t('global.toggle') }</Button>
        </Column>
      </Row>
    </Table>
    <SaveButton {edit} bind:wait={wait} data={$draft} original={$live} bind:validation={validation}
      onSave={() => lm.save()} />
  </Card>
</GellertPage>


