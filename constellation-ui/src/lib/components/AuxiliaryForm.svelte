<script lang="ts">
  import Button from "$lib/ui/Button.svelte";
  import Card from "$lib/ui/Card.svelte";
  import Column from "$lib/ui/Column.svelte";
  import Row from "$lib/ui/Row.svelte";
  import Table from "$lib/ui/Table.svelte";
  import { onMount } from "svelte";
  import { Auxiliary } from "$lib/business/auxOptions";
  import Rule from "$lib/components/Rule.svelte";
  import Rule2 from "$lib/components/Rule2.svelte";
  import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
  import { AdornmentType } from "$lib/business/adornmentType";
  import TextField from "$lib/ui/TextField.svelte";
  import Select from "$lib/ui/Select.svelte";
  import SaveButton from "$lib/components/SaveButton.svelte";
  import { auxiliaryOptionsStore, navigationStore, keyboardStore } from "$lib/store";
  import { auxiliaryComposite } from "$lib/business/protoStores";
  import { writeProtoRaw, buildForceVarintBytes, buildForceFloatBytes, wrapAsLengthDelim } from "$lib/business/protoWrite";
  import { TAG } from "$lib/business/protoTags";
  import { cloneDeep, isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
  import { getModalStore, type ModalSettings } from "@skeletonlabs/skeleton";

  // Shared body of the Auxiliary Output Programming page (level2/auxiliary):
  // per-output rule list (sensor/equipment conditions + and/or chains) and the
  // duty-cycle/period, with Back/Next navigation across the CONFIGURED aux
  // outputs (an aux only appears here once it's assigned an output in IO Config).
  // Rendered from ONE source of truth — the classic page AND a wide dashboard
  // modal. Page-chrome sizing (heightsStore / inner ScrollableArea) dropped so it
  // flows in both; the host scrolls. Prop contract mirrors FanSpeedForm (L2).
  // docs/spatial-ui-page-migration.md
  export let wait = false;
  export let ready = false;
  export let embedded = false;
  export let theme: 'light' | 'dark' = 'light';
  export let canEdit: boolean | null = null;

  const modalStore = getModalStore();

  $: edit = canEdit ?? ($navigationStore.level > 0);
  let validation = clearValidation();

  $: sensorValidation = [
    validation.sen1, validation.sen2, validation.sen3,
    validation.sen4, validation.sen5, validation.sen6,
  ];
  $: diffValidation = [
    validation.diff1, validation.diff2, validation.diff3,
    validation.diff4, validation.diff5, validation.diff6,
  ];

  let aux: Auxiliary | undefined = undefined;
  let data: Auxiliary | undefined = undefined;
  let navigationKey = 0;
  let allAux: { auxProg: string[], rules: any[] }[] = [];
  let currentIndex = 0;
  let reset = false;

  function isDirty(): boolean {
    return !!(data && aux && !isEqual(data, aux));
  }

  function hydrateFromComposite(view: any): void {
    if (!view) return;
    if (allAux.length !== view.allAux.length || !isEqual(allAux, view.allAux)) {
      allAux = view.allAux;
      if (currentIndex >= allAux.length) currentIndex = 0;
    }
    if (isDirty()) return;
    let count = 0;
    let idx = currentIndex;
    while (count < allAux.length) {
      const item = allAux[idx];
      if (item && item.rules && item.rules.length > 0) { currentIndex = idx; break; }
      idx = (idx + 1) % allAux.length;
      count++;
    }
    const baseData = {
      InputConfig: view.InputConfig,
      OutputConfig: view.OutputConfig,
      IoNames: view.IoNames,
      systemMode: view.systemMode,
    };
    const next = createAuxiliary(baseData, allAux[currentIndex]);
    if (!isEqual(data, next)) {
      data = next;
      aux = cloneDeep(data);
      reset = true;
    }
  }

  onMount(() => {
    if (!embedded) $navigationStore.isDirty = () => isDirty();
    const unsub = auxiliaryComposite.subscribe((v) => {
      if (!v) return;
      hydrateFromComposite(v as any);
      ready = true;
    });
    return () => unsub();
  });

  // Modal close hook — flush the current aux output if it has unsaved edits.
  let saveBtn: { save: () => Promise<void> } | undefined;
  export async function flush(): Promise<void> {
    if (isDirty() && saveBtn) await saveBtn.save();
  }

  function createAuxiliary(base: any, item: { auxProg: string[], rules: any[] }) {
    if (!base || !item) return undefined;
    return { ...base, ...item } as Auxiliary;
  }

  async function handleSaveComplete(event: CustomEvent<{ success: boolean }>) {
    if (event.detail.success) wait = false;
  }

  function checkDirty(action: () => void) {
    // In the dashboard modal, skip the Skeleton confirm (not wired there) —
    // autoSave + debounce already persisted any edit.
    if (!embedded && !$keyboardStore.hidden && isDirty()) {
      const modal: ModalSettings = {
        type: 'confirm',
        title: $t('global.confirm'),
        body: $t('global.are-you-sure'),
      };
      modal.buttonTextCancel = $t('global.no');
      modal.buttonTextConfirm = $t('global.yes');
      modal.response = (r: boolean) => { if (r) action(); };
      modalStore.trigger(modal);
    } else {
      action();
    }
  }

  function clearValidation() {
    return {
      sen1: '', sen2: '', sen3: '', sen4: '', sen5: '', sen6: '',
      diff1: '', diff2: '', diff3: '', diff4: '', diff5: '', diff6: '',
      dutyCycle: '', period: '',
    };
  }

  async function moveAux(dir: string) {
    checkDirty(async () => {
      if (!allAux || allAux.length === 0) return;
      let nextIndex = currentIndex;
      let count = 0;
      do {
        if (dir === 'Back') nextIndex = (nextIndex - 1 + allAux.length) % allAux.length;
        else nextIndex = (nextIndex + 1) % allAux.length;
        count++;
        const item = allAux[nextIndex];
        if (item && item.rules && item.rules.length > 0) break;
      } while (count < allAux.length);
      currentIndex = nextIndex;
      const baseData = { ...data };
      delete baseData.auxProg;
      delete baseData.rules;
      data = createAuxiliary(baseData, allAux[currentIndex]);
      aux = cloneDeep(data);
      navigationKey += 1;
      validation = clearValidation();
      reset = true;
    });
  }

  async function saveAuxProgram(d: any): Promise<void> {
    const eqIdx = parseInt(d.auxProg?.[0] ?? '0', 10);
    const auxIdx = eqIdx - 25; // EQ_AUX1
    if (auxIdx < 0 || auxIdx >= 8) {
      console.warn('[aux] eq_index out of range, skipping save', { eqIdx, auxIdx });
      return;
    }
    const ruleSubmsgs: Uint8Array[] = [];
    for (const r of (d.rules ?? [])) {
      const type = parseInt(r.type ?? '0', 10);
      if (type === 0) continue;
      const ioIdx = parseInt(r.io ?? '0', 10);
      const state = parseInt(r.st ?? '0', 10);
      const op = parseInt(r.op ?? '0', 10);
      const sen = parseFloat(r.sen ?? '0');
      const andOr = parseInt(r.andOr ?? '0', 10);
      const ref = parseInt(r.ref ?? '0', 10);
      const varParts = buildForceVarintBytes({ 1: type, 2: ioIdx, 3: state, 4: op, 6: andOr, 7: ref });
      const floatPart = buildForceFloatBytes({ 5: sen });
      const ruleBody = new Uint8Array(varParts.length + floatPart.length);
      ruleBody.set(varParts, 0);
      ruleBody.set(floatPart, varParts.length);
      ruleSubmsgs.push(wrapAsLengthDelim(6, ruleBody));
    }
    const headerVarints = buildForceVarintBytes({
      1: auxIdx,
      3: parseInt(d.auxProg?.[1] ?? '0', 10),
      4: parseInt(d.auxProg?.[2] ?? '0', 10),
      5: parseInt(d.auxProg?.[3] ?? '0', 10),
    });
    let entryLen = headerVarints.length;
    for (const r of ruleSubmsgs) entryLen += r.length;
    const entry = new Uint8Array(entryLen);
    entry.set(headerVarints, 0);
    let off = headerVarints.length;
    for (const r of ruleSubmsgs) { entry.set(r, off); off += r.length; }
    const inner = wrapAsLengthDelim(1, entry);
    await writeProtoRaw(TAG.AuxProgramSettings, inner);
  }
</script>

<div class="pform pform--{theme}">
  <Card class="w-[98%] mx-2 flex flex-col">
    {#if !data || aux === undefined || aux.rules.length === 0}
      <span class="text-size-xl">{ $t('level2.auxiliary.no-auxiliary-output-defined') }.</span>
    {:else}
    {#key navigationKey}
      <Table class="text-size-xl">
        <Row>
          <Column class="font-bold border-r border-gray-400">{ $t('global.output') }</Column>
          <Column class="font-bold" colspan={3}>{ $t('level2.auxiliary.rule') }</Column>
        </Row>
      </Table>

      <Table class="text-size-xl">
        {#each aux.rules as rule, index}
          {#if rule.andOr !== '256'}
            <Row>
              <Rule {index} name={aux.IoNames?.[parseInt(aux.auxProg[0], 10)]?.name ?? ''}
                bind:rule={rule} bind:aux={aux} on:change={() => rule = rule}/>
            </Row>
            {#if rule.type === '4' && rule.st !== '255' && data?.rules[index]}
              <Row>
                <Rule2 bind:rule={rule} bind:data={data.rules[index]} sensorValidation={sensorValidation[index]} diffValidation={diffValidation[index]}/>
              </Row>
            {/if}
          {/if}
        {/each}
      </Table>

      <Table class="text-size-xl">
        <Row>
          <Column colspan={4}>
            { $t('global.duty-cycle') }:
            <TextField size="xl" class="w-36" bind:value={aux.auxProg[1]} label="Duty Cycle"
              keyboardType={KeyboardTypes.Numeric} adornmentType={AdornmentType.Percent} {edit} validation={validation.dutyCycle}/>
            <span class="ml-4">{ $t('level2.auxiliary.period') }:</span>
            <TextField size="xl" class="w-24" bind:value={aux.auxProg[2]} label="Period"
              keyboardType={KeyboardTypes.Numeric} {edit} validation={validation.period}/>
            <Select class="w-64" size="xl" bind:value={aux.auxProg[3]} options={$auxiliaryOptionsStore.unitOptions} {edit} />
          </Column>
        </Row>
      </Table>
    {/key}

    <div class="flex flex-row items-center">
      <Button size="xl" class="ml-auto mr-2 mb-0" on:click={() => moveAux('Back')}>{ $t('global.back') }</Button>
      <SaveButton bind:this={saveBtn} {edit} bind:wait={wait} data={aux} bind:original={data} bind:validation={validation} bind:reset={reset} on:complete={handleSaveComplete} autoSave onSave={saveAuxProgram} />
      <Button size="xl" class="mr-auto mb-0" on:click={() => moveAux('Next')}>{ $t('global.next') }</Button>
    </div>
    {/if}
  </Card>
</div>
