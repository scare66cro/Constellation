<script lang="ts">
  import { onMount } from "svelte";
  import GellertPage from "$lib/components/GellertPage.svelte";
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

  // Proto-direct page state (Phase-5 useDraftRepeated pilot).
  // Replaces the legacy `humidifier.control: string[][]` positional shape
  // and the hand-rolled encode/force/wrap/POST dance. The hook owns:
  //   • per-row draft snapshot from HumidCtrlSettings.entries
  //   • dirty tracking against the live row
  //   • save: encode HumidCtrlEntry, force-emit zero-meaningful fields
  //     1=index and 2=mode, wrap as `repeated entries = 1`, POST.
  // See useDraft.ts for the hook contract; firmware-bridge-protocol.md
  // for the wire-level reasoning behind force-emit.
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
  // numField writes back through the typed draft as numbers.
  const coolOnStr    = numField(draft, 'coolOn',    'int');
  const coolOffStr   = numField(draft, 'coolOff',   'int');
  const recircOnStr  = numField(draft, 'recircOn',  'int');
  const recircOffStr = numField(draft, 'recircOff', 'int');
  const refrigOnStr  = numField(draft, 'refrigOn',  'int');
  const refrigOffStr = numField(draft, 'refrigOff', 'int');

  let title = $t('level1.humidifier.humidifier-control');

  let autoControl = [
    { text: $t('global.manual'), value: 0 },
    { text: $t('level1.humidifier.timer-default'), value: 1 },
    { text: $t('global.automatic'), value: 2 },
  ];

  let options = [] as { text: string, value: string }[];

  let validation = {
    'coolOn': '',
    'coolOff': '',
    'recircOn': '',
    'recircOff': '',
    'refrigOn': '',
    'refrigOff': ''
  };

  // Selected slot UI binding (string for the Select). When it changes,
  // tell the hook to swap rows — that snaps draft/live to the new slot.
  let selected = '0';
  $: hum.select(parseInt(selected, 10) || 0);

  $: main = $frontMatterStore?.main as string[];
  $: ready = false;
  $: wait = false;
  $: level = $navigationStore.level;
  $: edit = $navigationStore.level > 0;
  $: textSize = edit ? 'text-size-large' : 'text-size-xl';
  $: compSize = edit ? 'lg' : 'xl';

  // Equipment status (replaces legacy `equip[9..93]` indexes).
  // EquipmentStatus.items is a sparse array indexed by eq_index.
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
      $navigationStore.isDirty = () => $dirty;
      if (($frontMatterStore?.panel as string[])?.[14] !== EQUIP_NOT_DEFINED) options.push({ text: `${$t('global.humidifier')} #1`, value: '0' });
      if (($frontMatterStore?.panel as string[])?.[18] !== EQUIP_NOT_DEFINED) options.push({ text: `${$t('global.humidifier')} #2`, value: '1' });
      if (($frontMatterStore?.panel as string[])?.[22] !== EQUIP_NOT_DEFINED) options.push({ text: `${$t('global.humidifier')} #3`, value: '2' });
    } catch (error) {
      console.error(error);
    }
    ready = true;
  });
</script>

<GellertPage {wait} {ready} {title} {level} name="humidifier">
  <Card class="mx-auto w-3/4 flex flex-col mt-1">
    <Table class="mb-1 {textSize}">
      <Row>
        <Column class="xl:py-1" colspan={3}>{ $t('level1.humidifier.equipment-selection') }:
          <Select class="w-64 md:w-96 xl:w-128 my-1" size={compSize} bind:value={selected} edit={true} {options} />
        </Column>
      </Row>
    </Table>
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
      <SaveButton {edit} bind:wait={wait} data={$draft} original={$live} bind:validation={validation} autoSave
        onSave={async () => { await hum.save(); }} />
    {/if}
  </Card>
</GellertPage>
