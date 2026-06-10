<script lang="ts">
  import { onMount, createEventDispatcher } from "svelte";
  import { goto } from "$app/navigation";
  import Card from "$lib/ui/Card.svelte";
  import Column from "$lib/ui/Column.svelte";
  import Row from "$lib/ui/Row.svelte";
  import Table from "$lib/ui/Table.svelte";
  import PIDU from "$lib/components/PIDU.svelte";
  import Button from "$lib/ui/Button.svelte";
  import TextField from "$lib/ui/TextField.svelte";
  import SaveButton from "$lib/components/SaveButton.svelte";
  import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
  import { navigationStore, pidStore } from "$lib/store";
  import { isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
  import { doorSettings } from "$lib/business/protoStores";
  import { useDraft, numField } from "$lib/business/useDraft";
  import { TAG } from "$lib/business/protoTags";

  // Shared body of the Fresh-Air Door settings (level2/door, main card):
  // PIDU + actuator time + cool-air-cycle, proto-direct save
  // (TAG.DoorSettings). Rendered on the classic page AND as the
  // "Fresh-Air Door" tab of the dashboard door modal. The GDC stage grid is
  // a SEPARATE form (GdcStagesForm) — the second tab. Prop contract mirrors
  // PlenumSetpointsForm / FanSpeedForm. docs/spatial-ui-page-migration.md
  export let wait = false;
  export let ready = false;
  export let embedded = false;
  export let theme: 'light' | 'dark' = 'light';
  export let canEdit: boolean | null = null;

  $: level = $navigationStore.level;
  $: edit = canEdit ?? (level > 0);

  let validation = {
    PAirValue: '', IAirValue: '', DAirValue: '', UAirValue: '',
    ActuatorTimes: '', CoolAirCycle: ''
  };

  // Proto-direct DoorSettings draft (replaces door[6]-positional shape).
  const door = useDraft(doorSettings, TAG.DoorSettings);
  const { draft, live, hydrated } = door;
  const pStr   = numField(draft, 'pGain',        'float');
  const iStr   = numField(draft, 'iGain',        'float');
  const dStr   = numField(draft, 'dGain',        'float');
  const uStr   = numField(draft, 'uLimit',       'float');
  const actStr = numField(draft, 'actuatorTime', 'int');
  const cycStr = numField(draft, 'coolAirCycle', 'int');

  // SaveButton ref so the modal can flush on close-unless-cancel.
  let saveBtn: { save: () => Promise<void> } | undefined;
  export async function flush(): Promise<void> {
    if (!isEqual($draft, $live) && saveBtn) await saveBtn.save();
  }

  onMount(() => {
    if (!embedded) $navigationStore.isDirty = () => !isEqual($draft, $live);
    ready = true;
  });

  const dispatch = createEventDispatcher();
  function gotoLogs() {
    // Dashboard: open the in-place PID modal (plan3d listens for `viewlogs`);
    // classic page: navigate to /level2/pid as before.
    if (embedded) { dispatch('viewlogs'); return; }
    $pidStore.returnPage = '/level2/door';
    goto('/level2/pid');
  }
</script>

<div class="pform pform--{theme}">
  <Card class="md:mx-2 xl:mx-auto flex flex-col container-wide 3xl:container-standard">
    {#if $hydrated}
    <Table class="text-size-xl">
      <Row>
        <Column class="w-1/2 border-r border-gray-400">{ $t('global.pidu-values') }</Column>
        <Column class="w-1/2">
          <PIDU bind:p={$pStr} bind:i={$iStr} bind:d={$dStr} bind:u={$uStr} {edit} pvalid={validation.PAirValue} ivalid={validation.IAirValue} dvalid={validation.DAirValue} uvalid={validation.UAirValue} />
        </Column>
      </Row>
      <Row>
        <Column class="w-1/2 border-r border-gray-400">{ $t('level2.door.total-open-time-for-all-actuator-stages') }</Column>
        <Column class="w-1/2 items-center">
          <p class="text-center">
            <TextField class="w-36 mr-2" size="xl" bind:value={$actStr} {edit} keyboardType={KeyboardTypes.Numeric} validation={validation.ActuatorTimes}/> {$t('level2.door.seconds')}
          </p>
        </Column>
      </Row>
      <Row>
        <Column class="w-1/2 border-r border-gray-400">{ $t('level2.door.cooling-air-short-cycle-timer') }</Column>
        <Column class="w-1/2 items-center">
          <p class="text-center">
            <TextField class="w-36 mr-2" size="xl" bind:value={$cycStr} {edit} keyboardType={KeyboardTypes.Numeric} validation={validation.CoolAirCycle}/> {$t('global.minutes')}
          </p>
        </Column>
      </Row>
      {#if !embedded}
        <Row>
          <Column class="w-1/2 border-r border-gray-400">{ $t('global.pid-controller-output-logging-options') }</Column>
          <Column class="w-1/2">
            <Button size="xl" class="w-fit" on:click={gotoLogs}>{ $t('global.logs') }</Button>
          </Column>
        </Row>
      {/if}
    </Table>
    <SaveButton bind:this={saveBtn} {edit} bind:wait={wait} data={$draft} original={$live} route="door" bind:validation={validation} autoSave
      onSave={() => door.save()}/>
    {/if}
  </Card>
</div>
