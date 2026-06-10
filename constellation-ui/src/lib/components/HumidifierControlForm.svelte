<script lang="ts">
  import { onMount } from "svelte";
  import Card from "$lib/ui/Card.svelte";
  import Table from "$lib/ui/Table.svelte";
  import Row from "$lib/ui/Row.svelte";
  import Column from "$lib/ui/Column.svelte";
  import TextField from "$lib/ui/TextField.svelte";
  import Select from "$lib/ui/Select.svelte";
  import { frontMatterStore, navigationStore } from "$lib/store";
  import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
  import SaveButton from "$lib/components/SaveButton.svelte";
  import { EQUIP_NOT_DEFINED } from "$lib/business/mode";
  import { t } from "svelte-i18n";
  import { humidCtrlSettings, equipmentStatus } from "$lib/business/protoStores";
  import { useDraftRepeated, numField } from "$lib/business/useDraft";
  import { TAG } from "$lib/business/protoTags";
  import { HumidCtrlEntry } from "$proto/agristar/settings.js";

  // Shared body of the Humidifier Control page (level1/humidifier). Renders
  // in the classic page and the dashboard humidifier modal from one source.
  // See PlenumSetpointsForm for the prop contract; docs/spatial-ui-page-migration.md.
  export let wait = false;
  export let ready = false;
  export let embedded = false;
  export let theme: 'light' | 'dark' = 'light';
  export let canEdit: boolean | null = null;
  // `unit` locks the form to a single humidifier head (0/1/2) and hides the
  // equipment-selection dropdown — used by the dashboard, where each
  // humidifier has its own hotspot/modal. null = classic page behaviour
  // (operator picks the head from the dropdown).
  export let unit: number | null = null;

  // Proto-direct page state (Phase-5 useDraftRepeated pilot). See the
  // original page / useDraft.ts for the hook contract + force-emit reasoning.
  const hum = useDraftRepeated(humidCtrlSettings, TAG.HumidCtrlSettings, {
    rowsKey: 'entries',
    rowCodec: HumidCtrlEntry,
    repeatedFieldNum: 1,
    initialIndex: 0,
    defaultRow: (idx) => ({
      index: idx, mode: 0,
      coolOn: 0, coolOff: 0,
      recircOn: 0, recircOff: 0,
      refrigOn: 0, refrigOff: 0,
    }),
    forceVarints: (r) => ({ 1: r.index, 2: r.mode }),
  });
  const { draft, hydrated, dirty, live } = hum;

  // String views for TextField bindings (TextField expects string).
  const coolOnStr    = numField(draft, 'coolOn',    'int');
  const coolOffStr   = numField(draft, 'coolOff',   'int');
  const recircOnStr  = numField(draft, 'recircOn',  'int');
  const recircOffStr = numField(draft, 'recircOff', 'int');
  const refrigOnStr  = numField(draft, 'refrigOn',  'int');
  const refrigOffStr = numField(draft, 'refrigOff', 'int');

  let autoControl = [
    { text: $t('global.manual'), value: 0 },
    { text: $t('level1.humidifier.timer-default'), value: 1 },
    { text: $t('global.automatic'), value: 2 },
  ];

  let options = [] as { text: string, value: string }[];

  let validation = {
    'coolOn': '', 'coolOff': '',
    'recircOn': '', 'recircOff': '',
    'refrigOn': '', 'refrigOff': ''
  };

  // Selected slot UI binding (string for the Select). Swap rows on change.
  // When `unit` is fixed (dashboard modal), pin the selection to it.
  let selected = '0';
  $: if (unit !== null) selected = String(unit);
  $: hum.select(parseInt(selected, 10) || 0);

  $: main = $frontMatterStore?.main as string[];
  $: edit = canEdit ?? ($navigationStore.level > 0);
  $: textSize = edit ? 'text-size-large' : 'text-size-xl';
  $: compSize = edit ? 'lg' : 'xl';

  // SaveButton ref so the modal can flush on close-unless-cancel.
  let saveBtn: { save: () => Promise<void> } | undefined;
  export async function flush(): Promise<void> {
    if ($dirty && saveBtn) await saveBtn.save();
  }

  // Equipment status (replaces legacy `equip[9..93]` indexes).
  let humidStatus: string[] = [];
  $: if ($equipmentStatus?.items?.length) {
    const byIdx: Record<number, number> = {};
    for (const it of $equipmentStatus.items) {
      byIdx[it.eqIndex] = it.outputOn ? 1 : 0;
    }
    humidStatus = [
      String(byIdx[9]  ?? 0), String(byIdx[12] ?? 0), String(byIdx[76] ?? 0),
      String(byIdx[39] ?? 0), String(byIdx[40] ?? 0), String(byIdx[93] ?? 0),
    ];
  }

  function getStatus(sel: string): string {
    const idx = parseInt(sel, 10);
    if (humidStatus?.[idx] === '0') {
      return $t('global.on');
    } else {
      if (humidStatus?.[idx + 3] === '0') {
        return $t('global.off');
      } else {
        return $t('level1.humidifier.remote-off');
      }
    }
  }

  onMount(() => {
    try {
      if (!embedded) $navigationStore.isDirty = () => $dirty;
      if (($frontMatterStore?.panel as string[])?.[14] !== EQUIP_NOT_DEFINED) options.push({ text: `${$t('global.humidifier')} #1`, value: '0' });
      if (($frontMatterStore?.panel as string[])?.[18] !== EQUIP_NOT_DEFINED) options.push({ text: `${$t('global.humidifier')} #2`, value: '1' });
      if (($frontMatterStore?.panel as string[])?.[22] !== EQUIP_NOT_DEFINED) options.push({ text: `${$t('global.humidifier')} #3`, value: '2' });
    } catch (error) {
      console.error(error);
    }
    ready = true;
  });
</script>

<div class="pform pform--{theme}">
  <Card class="mx-auto w-3/4 flex flex-col mt-1">
    {#if unit === null}
      <Table class="mb-1 {textSize}">
        <Row>
          <Column class="xl:py-1" colspan={3}>{ $t('level1.humidifier.equipment-selection') }:
            <Select class="w-64 md:w-96 xl:w-128 my-1" size={compSize} bind:value={selected} edit={true} {options} />
          </Column>
        </Row>
      </Table>
    {/if}
    {#if $hydrated}
      <Table class="mb-1 {textSize}">
        <Row>
          <Column class="xl:py-1" colspan={3}>{ $t('level1.humidifier.auto-control-mode') }: <Select class="w-64 md:w-96 3xl:w-128 my-1" size={compSize} bind:value={$draft.mode} options={autoControl} {edit} /></Column>
        </Row>
      </Table>
      {#if $draft.mode === 2}
        <Table class="mb-1 {textSize}">
          <Row>
            <Column class="xl:py-1">{ $t('level1.humidifier.the-system-will-automatically-maintain-the') } { $t('level1.humidifier.plenum-humidity-setpoint-of') } {main?.[6]}%.</Column>
          </Row>
        </Table>
      {:else if $draft.mode === 1}
        <Table class="mb-1 {textSize}">
          <Row>
            <Column class="xl:py-1 w-1/3 border-r border-gray-400" rowspan={2}>{ $t('level1.humidifier.system-mode') }</Column>
            <Column class="xl:py-1 w-1/3" colspan={2}>{ $t('level1.humidifier.cycle-duration') }</Column>
          </Row>
          <Row>
            <Column class="xl:py-1 w-1/3 border-r border-gray-400">{ $t('level1.humidifier.seconds-on') }</Column>
            <Column class="xl:py-1 w-1/3">{ $t('level1.humidifier.seconds-off') }</Column>
          </Row>
          <Row>
            <Column class="xl:py-1 w-1/3 border-r border-gray-400">{ $t('global.cooling') }</Column>
            <Column class="xl:py-1 w-1/3 border-r border-gray-400"><TextField bind:value={$coolOnStr} size={compSize} {edit} label="Cooling On" keyboardType={KeyboardTypes.Numeric} validation={validation.coolOn}/></Column>
            <Column class="xl:py-1 w-1/3"><TextField bind:value={$coolOffStr} size={compSize} {edit} label="Cooling Off" keyboardType={KeyboardTypes.Numeric} validation={validation.coolOff}/></Column>
          </Row>
          <Row>
            <Column class="xl:py-1 w-1/3 border-r border-gray-400">{ $t('global.recirculation') }</Column>
            <Column class="xl:py-1 w-1/3 border-r border-gray-400"><TextField bind:value={$recircOnStr} size={compSize} {edit} label="Recirculation On" keyboardType={KeyboardTypes.Numeric} validation={validation.recircOn}/></Column>
            <Column class="xl:py-1 w-1/3"><TextField bind:value={$recircOffStr} size={compSize} {edit} label="Recirculation Off" keyboardType={KeyboardTypes.Numeric} validation={validation.recircOff}/></Column>
          </Row>
          <Row>
            <Column class="xl:py-1 w-1/3 border-r border-gray-400">{ $t('global.refrigeration') }</Column>
            <Column class="xl:py-1 w-1/3 border-r border-gray-400"><TextField bind:value={$refrigOnStr} size={compSize} {edit} label="Refrigeration On" keyboardType={KeyboardTypes.Numeric} validation={validation.refrigOn}/></Column>
            <Column class="xl:py-1 w-1/3"><TextField bind:value={$refrigOffStr} size={compSize} {edit} label="Refrigeration Off" keyboardType={KeyboardTypes.Numeric} validation={validation.refrigOff}/></Column>
          </Row>
        </Table>
      {/if}
      <Table class={textSize}>
        <Row>
          <Column class="xl:py-1 w-1/3 border-r border-gray-400">{ $t('level1.humidifier.humidifier-status') }:</Column>
          <Column class="xl:py-1 w-2/3">{getStatus(selected)}</Column>
        </Row>
      </Table>
      <SaveButton bind:this={saveBtn} {edit} bind:wait={wait} data={$draft} original={$live} bind:validation={validation} autoSave
        onSave={async () => { await hum.save(); }} />
    {/if}
  </Card>
</div>
