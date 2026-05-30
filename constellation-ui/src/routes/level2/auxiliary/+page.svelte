<script lang="ts">
	import GellertPage from "$lib/components/GellertPage.svelte";
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
  import { auxiliaryOptionsStore, navigationStore, heightsStore, keyboardStore } from "$lib/store";
  import { auxiliaryComposite } from "$lib/business/protoStores";
  import { writeProtoRaw, buildForceVarintBytes, buildForceFloatBytes, wrapAsLengthDelim } from "$lib/business/protoWrite";
  import { TAG } from "$lib/business/protoTags";
  import { cloneDeep, isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
  import { getModalStore, type ModalSettings } from "@skeletonlabs/skeleton";
  import ScrollableArea from "$lib/components/ScrollableArea.svelte";

  const modalStore = getModalStore();

  let title = $t('level2.auxiliary.auxiliary-output-programming');
  let edit = true;
  let validation = clearValidation();

  $: sensorValidation = [
    validation.sen1,
    validation.sen2,
    validation.sen3,
    validation.sen4,
    validation.sen5,
    validation.sen6,
  ];

  $: diffValidation = [
    validation.diff1,
    validation.diff2,
    validation.diff3,
    validation.diff4,
    validation.diff5,
    validation.diff6,
  ];

  $: aux = undefined as Auxiliary | undefined;
  $: ready = false;
  $: wait = false;
  let saveHeight: number = 0;
  let headerHeight: number = 0;
  let dutyHeight: number = 0;
  let height = 768;
  $: if (typeof window !== 'undefined') {
    // Responsive offset similar to IO Config page
    const availableHeight = window.innerHeight;
    let offset = 5;
    if (availableHeight >= 1080) {
      const baseHeight = 1080;
      const baseOffset = 75;
      const maxOffset = 120;
      const heightRatio = Math.min((availableHeight - baseHeight) / (1440 - baseHeight), 1);
      offset = baseOffset + (maxOffset - baseOffset) * heightRatio;
    } else if (availableHeight >= 768) {
      const baseHeight = 768;
      const baseOffset = 35;
      const maxOffset = 75;
      const heightRatio = (availableHeight - baseHeight) / (1080 - baseHeight);
      offset = baseOffset + (maxOffset - baseOffset) * heightRatio;
    } else if (availableHeight >= 600) {
      offset = 25;
    }
    const minBottomMargin = 4;
    height = $heightsStore.main - $heightsStore.header - $heightsStore.footer - offset - minBottomMargin;
  }
  
  // Add data variable to hold fetched data
  let data: Auxiliary | undefined = undefined;
  
  // Use a key to track navigation changes
  let navigationKey = 0;

  let allAux: { auxProg: string[], rules: any[] }[] = [];
  let currentIndex = 0;
  let reset = false;

  // Hydration gate: while the user is editing (data/aux differ), skip
  // composite-driven re-hydration so the next 5 s firmware push doesn't
  // overwrite an in-flight edit. Mirrors the plentemp/failures1 pattern.
  function isDirty(): boolean {
    return !!(data && aux && !isEqual(data, aux));
  }

  function hydrateFromComposite(view: ReturnType<typeof asView>): void {
    if (!view) return;
    if (allAux.length !== view.allAux.length
        || !isEqual(allAux, view.allAux)) {
      allAux = view.allAux;
      if (currentIndex >= allAux.length) currentIndex = 0;
    }
    if (isDirty()) return;
    // Find first valid auxiliary if current is empty.
    let count = 0;
    let idx = currentIndex;
    while (count < allAux.length) {
      const item = allAux[idx];
      if (item && item.rules && item.rules.length > 0) {
        currentIndex = idx;
        break;
      }
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

  // Helper alias so hydrateFromComposite's parameter type stays in sync
  // with the store without re-importing the AuxiliaryView type.
  function asView(v: any) { return v as any; }

  onMount(() => {
    $navigationStore.isDirty = () => isDirty();
    const unsub = auxiliaryComposite.subscribe((v) => {
      if (!v) return;
      hydrateFromComposite(asView(v));
      ready = true;
    });
    return () => unsub();
  });

  function createAuxiliary(base: any, item: { auxProg: string[], rules: any[] }) {
      if (!base || !item) return undefined;
      return { ...base, ...item } as Auxiliary;
  }

  async function handleSaveComplete(event: CustomEvent<{ success: boolean }>) {
      // Proto-direct: firmware re-broadcasts the AuxProgramBundle every
      // 5 s, and `auxiliaryComposite` re-hydrates automatically when the
      // next frame arrives. No explicit refresh needed; the dirty gate
      // in hydrateFromComposite already prevents clobbering in-flight
      // edits, so the post-save baseline simply settles when the next
      // bundle lands. Just clear the busy flag.
      if (event.detail.success) {
          wait = false;
      }
  }

  function checkDirty(action: () => void) {
    // Skip the "continue without saving?" prompt unless the user is
    // actively typing (keyboard visible). SaveButton autoSave handles
    // the persistence — see GellertFooter.checkDirty for full rationale.
    if (!$keyboardStore.hidden && $navigationStore.isDirty()) {
      const modal: ModalSettings = {
        type: 'confirm',
        title: $t('global.confirm'),
        body: $t('global.are-you-sure'),
      };
      modal.buttonTextCancel = $t('global.no');
      modal.buttonTextConfirm = $t('global.yes');
      modal.response = (r: boolean) => { if (r) {
        action();
      }};
      modalStore.trigger(modal);
    } else {
        action();
    }
  }

  function clearValidation() {
    return {
      sen1: '',
      sen2: '',
      sen3: '',
      sen4: '',
      sen5: '',
      sen6: '',
      diff1: '',
      diff2: '',
      diff3: '',
      diff4: '',
      diff5: '',
      diff6: '',
      dutyCycle: '',
      period: '',
    };
  }

  async function moveAux(dir: string) {
    checkDirty(async () => {
      // wait = true;
      if (!allAux || allAux.length === 0) return;

      let nextIndex = currentIndex;
      let count = 0;
      
      do {
        if (dir === 'Back') {
           nextIndex = (nextIndex - 1 + allAux.length) % allAux.length;
        } else {
           nextIndex = (nextIndex + 1) % allAux.length;
        }
        count++;
        // Check if item is valid
        const item = allAux[nextIndex];
        if (item && item.rules && item.rules.length > 0) {
           break;
        }
      } while (count < allAux.length);
      
      currentIndex = nextIndex;

      // Reconstruct data from current base properties + new item
      // We assume data has the common props.
      const baseData = { ...data };
      delete baseData.auxProg;
      delete baseData.rules;
      
      data = createAuxiliary(baseData, allAux[currentIndex]);
      aux = cloneDeep(data);
      
      navigationKey += 1; 
      validation = clearValidation();
      reset = true;
      // wait = false;
    });
  }

  /**
   * Phase 5.1 — proto-direct write for AuxProgramSettings (firmware
   * settings field 41). Mirrors the legacy bridge `auxProgram` shim
   * (deleted from constellation-ui/server/src/index.ts) but builds the
   * envelope client-side and posts raw bytes to /proto/write/41.
   *
   * Wire layout (matches firmware apply_aux_program in
   * Nova_Firmware/Platform/nova_dataexc.c):
   *   outer = field 1 (length-delim) = AuxProgramSettings sub-msg, where
   *     AuxProgramSettings = {
   *       1: aux_index (varint, force — 0 valid for AUX1)
   *       3: duty_cycle (varint, force — 0 means "off")
   *       4: period     (varint, force)
   *       5: units      (varint, force — 0 = seconds)
   *       6: AuxRule    (length-delim, repeated) = {
   *         1: type (varint, force), 2: io_index (force),
   *         3: state (force),        4: op       (force),
   *         5: sensor_value (float, force — 0.0 valid),
   *         6: and_or (force),       7: reference_index (force)
   *       }
   *     }
   * Field 2 (eq_index) is intentionally OMITTED — firmware ignores it
   * and infers EQ_AUX1+aux_index. Empty rules (type==0) are skipped.
   */
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
      if (type === 0) continue; // firmware skips empty rules — match here too
      const ioIdx = parseInt(r.io ?? '0', 10);
      const state = parseInt(r.st ?? '0', 10);
      const op = parseInt(r.op ?? '0', 10);
      const sen = parseFloat(r.sen ?? '0');
      const andOr = parseInt(r.andOr ?? '0', 10);
      const ref = parseInt(r.ref ?? '0', 10);
      const varParts = buildForceVarintBytes({ 1: type, 2: ioIdx, 3: state, 4: op, 6: andOr, 7: ref });
      const floatPart = buildForceFloatBytes({ 5: sen });
      // Concatenate then wrap as field 6 of AuxProgramSettings.
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

<GellertPage {wait} {ready} {title} level={2} name="auxiliary">
  <Card class="w-[98%] mx-2 flex flex-col" {height}>
    {#if !data || aux === undefined || aux.rules.length === 0}
      <span class="text-size-xl">{ $t('level2.auxiliary.no-auxiliary-output-defined') }.</span>
    {:else}
  {#key navigationKey}
    <!-- Fixed header -->
    <div bind:clientHeight={headerHeight}>
      <Table class="text-size-xl">
        <Row>
          <Column class="font-bold border-r border-gray-400">{ $t('global.output') }</Column>
          <Column class="font-bold" colspan={3}>{ $t('level2.auxiliary.rule') }</Column>
        </Row>
      </Table>
    </div>

    <!-- Scrollable rules -->
    <ScrollableArea>
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
    </ScrollableArea>
    <!-- Fixed duty cycle -->
    <div bind:clientHeight={dutyHeight}>
      <Table class="text-size-xl">
        <Row>
          <Column colspan={4}>
            { $t('global.duty-cycle') }:
            <TextField
              size="xl"
              class="w-36"
              bind:value={aux.auxProg[1]}
              label="Duty Cycle"
              keyboardType={KeyboardTypes.Numeric}
              adornmentType={AdornmentType.Percent}
              {edit}
              validation={validation.dutyCycle}
            />
            <span class="ml-4">{ $t('level2.auxiliary.period') }:</span>
            <TextField
              size="xl"
              class="w-24"
              bind:value={aux.auxProg[2]}
              label="Period"
              keyboardType={KeyboardTypes.Numeric}
              {edit}
              validation={validation.period}
            />
            <Select class="w-64" size="xl" bind:value={aux.auxProg[3]} options={$auxiliaryOptionsStore.unitOptions} {edit} />
          </Column>
        </Row>
      </Table>
    </div>
  {/key}

  <div class="flex flex-row items-center" bind:clientHeight={saveHeight}>
        <Button size="xl" class="ml-auto mr-2 mb-0" on:click={() => moveAux('Back')}>{ $t('global.back') }</Button>
        <SaveButton {edit} bind:wait={wait} data={aux} bind:original={data} bind:validation={validation} bind:reset={reset} on:complete={handleSaveComplete} autoSave onSave={saveAuxProgram} />
        <Button size="xl" class="mr-auto mb-0" on:click={() => moveAux('Next')}>{ $t('global.next') }</Button>
      </div>
    {/if}
  </Card>
</GellertPage>



