<script lang="ts">
	import GellertPage from "$lib/components/GellertPage.svelte";
	import Card from "$lib/ui/Card.svelte";
	import Column from "$lib/ui/Column.svelte";
	import Row from "$lib/ui/Row.svelte";
	import Table from "$lib/ui/Table.svelte";
  import { onMount, onDestroy } from "svelte";
	import Button from "$lib/ui/Button.svelte";
	import type { IOConfigType } from "$lib/business/ioConfig";
	import OutputCell from "$lib/components/OutputCell.svelte";
	import InputCell from "$lib/components/InputCell.svelte";
	import { navigationStore } from "$lib/store";
  import { getHttpUrl } from "$lib/business/util";
	import SaveButton from "$lib/components/SaveButton.svelte";
  import { cloneDeep, isEqual } from "lodash-es";
  import { t } from "svelte-i18n";
  import { safeJsonParse } from "$lib/business/util";
	import ScrollableArea from "$lib/components/ScrollableArea.svelte";

  export let data: IOConfigType;

  let title = $t('level2.ioconfig.system-i-o-configuration');
  $: ioConfig = {
      ioAvailable: [],
      config: {
        outputConfig: [],
        inputConfig: [],
      },
      ioNames: [],
      systemMode: '0',
  } as IOConfigType;

  $: outputList = [{ text: $t('global.none'), value: '-1'}];
  $: inputList = [{ text: $t('global.none'), value: '-1'}];

  const lights: {text: string, value: string}[] = [];

  let potatoMode: boolean;
  let onionMode: boolean;
  let pecanMode: boolean;

  const board = [$t('level2.ioconfig.main'), `${$t('global.board')} 1`, `${$t('global.board')} 2`]
  
  // GDC (Door Controller) actuator labels
  // Paired I/O: DO1/DO2 = Actuator 1 Open/Close, DO3/DO4 = Actuator 2, etc.
  const gdcOutputLabels = ['Open 1', 'Close 1', 'Open 2', 'Close 2', 'Open 3', 'Close 3', 'Open 4', 'Close 4', 'Open 5', 'Close 5'];
  const gdcInputLabels = ['Open SW 1', 'Close SW 1', 'Open SW 2', 'Close SW 2', 'Open SW 3', 'Close SW 3', 'Open SW 4', 'Close SW 4', 'Open SW 5', 'Close SW 5'];
  
  /**
   * Get the display name for a board based on ioAvailable entry.
   * For orbit boards, uses the label from ioAvailable (e.g., "Orbit 1", "GDC 2")
   * For legacy boards, uses the hardcoded board[] array.
   */
  function getBoardName(boardIndex: number): string {
    const info = ioInfo[boardIndex];
    if (!info || !info[0]) return board[boardIndex] ?? `Board ${boardIndex}`;
    
    // If label starts with "Orbit" or "GDC", it's a Constellation orbit board
    const label = info[0];
    if (label.startsWith('Orbit') || label.startsWith('GDC')) {
      return label;
    }
    
    // Legacy names
    return board[boardIndex] ?? label;
  }
  
  /**
   * Check if a board is a GDC (Door Controller) based on boardType.
   * boardType is the 4th field in ioAvailable format: "label:numOut:numIn:boardType"
   */
  function isGDCBoard(boardIndex: number): boolean {
    const info = ioInfo[boardIndex];
    return info && info[3] === '2'; // boardType 2 = GDC
  }
  
  /**
   * Get row label for I/O pin (handles GDC special labels vs generic numbers)
   */
  function getRowLabel(boardIndex: number, pinIndex: number): string {
    if (isGDCBoard(boardIndex)) {
      // For GDC, use actuator-specific labels
      return pinIndex <= 5 ? `Door ${pinIndex}` : `Door ${pinIndex - 5}`;
    }
    return String(pinIndex);
  }
  
  /**
   * Get column header for output column (handles GDC actuator labels)
   */
  function getOutputHeader(boardIndex: number, pinIndex: number): string {
    if (isGDCBoard(boardIndex) && pinIndex > 0 && pinIndex <= 10) {
      return gdcOutputLabels[pinIndex - 1] ?? `DO${pinIndex}`;
    }
    return $t('global.output');
  }
  
  /**
   * Get column header for input column (handles GDC actuator labels)
   */
  function getInputHeader(boardIndex: number, pinIndex: number): string {
    if (isGDCBoard(boardIndex) && pinIndex > 0 && pinIndex <= 10) {
      return gdcInputLabels[pinIndex - 1] ?? `DI${pinIndex}`;
    }
    return $t('global.input');
  }

  let ioInfo: Array<Array<string>> = [];
  let validation: Record<string, string> = {};
  // Backend now provides board view directly inside ioConfig.config.{outputConfig,inputConfig};
  // remove legacy boardOutput/boardInput aliases and work with arrays in place.

  $: ready = false;
  $: wait = false;

  function buildEquipmentList() {
    outputList.splice(0, outputList.length);
    inputList.splice(0,inputList.length);
    lights.splice(0, lights.length);
    outputList.push({ text: $t('global.none'), value: '-1'});
    inputList.push({ text: $t('global.none'), value: '-1'});

    for (let i = 0; i < ioConfig.ioNames.length - 1; i += 1) {
      const listInfo = ioConfig.ioNames[i].split(':');
      if ((((potatoMode || pecanMode) && listInfo[1] === '1') // potato
        || (onionMode && listInfo[1] === '2') // onion
        || (listInfo[1] === '4' || listInfo[1] === '5' || listInfo[1] === '6' || listInfo[1] === '7')) // all
        && listInfo[2] !== '3') { // not a switch
          // Check if this is a baylight by equipment ID (23 or 24)
          if (listInfo[4] === '23' || listInfo[4] === '24') {
            // Only baylights (23, 24) go into lights array
            lights.push({ text: listInfo[0], value: listInfo[4] });
          } else {
            // Non-baylight equipment goes into output/input lists
            if (listInfo[2] === '0' || listInfo[2] === '2') { // output or both
              outputList.push({ text: listInfo[0], value: listInfo[4] })
            }
            if (listInfo[2] === '1' || listInfo[2] === '2') { // input or both
              inputList.push({ text: listInfo[0], value: listInfo[4] })
            }
          }
      }
    }

    // Sort indicator lights to the top of the output list:
    // Red Light (38), Yellow Light (39), Fan/Green Light (0)
    const lightOrder: Record<string, number> = { '38': 1, '39': 2, '0': 3 };
    outputList.sort((a, b) => {
      const ao = lightOrder[a.value] ?? 99;
      const bo = lightOrder[b.value] ?? 99;
      return ao - bo;
    });
  }

  function setupIoConfig(data: IOConfigType) {
    ioConfig = data;
    if (ioConfig.ioAvailable.length > 0 && ioConfig.ioAvailable[ioConfig.ioAvailable.length - 1] === '') {
      ioConfig.ioAvailable.pop();
    }
    ioConfig.ioAvailable.map((io, i) => {
      ioInfo[i] = io.split(':');
    });
    potatoMode = ioConfig.systemMode === '0';
    onionMode = ioConfig.systemMode === '1';
    pecanMode = ioConfig.systemMode === '3';
    buildEquipmentList();
  }

  // No rebuild needed; backend already sends board view.

  function outputChanged(event: CustomEvent<{ i: number, j: number, value: string }>) {
    const i = event.detail.i;
    const j = event.detail.j;
    const value = parseInt(event.detail.value, 10);
    const pid = (12 * i + j).toString();
    // Directly write outputConfig (pid-indexed)
    ioConfig.config.outputConfig[parseInt(pid, 10)] = event.detail.value;
    // Also mirror to input when allowed and valid for this slot
    try {
      const numInputs = parseInt(ioInfo[i]?.[2] || '0', 10);
      const allowedInput = (j <= numInputs) && ((i !== 0) || (j !== 1));
      const equipOptions = (i >= 1 && (j % 12) >= 7) ? [...inputList, ...lights] : inputList;
      if (allowedInput && equipOptions.some(opt => opt.value === event.detail.value)) {
        ioConfig.config.inputConfig[parseInt(pid, 10)] = event.detail.value;
      }
    } catch {}
    // Special case: humidifier heads auto-map adjacent output as before
    if (value === 7 || value === 9 || value === 11) {
      const nextPid = (parseInt(pid, 10) + 1).toString();
      ioConfig.config.outputConfig[parseInt(nextPid, 10)] = (value + 1).toString();
      // Mirror adjacent to input if allowed/valid
      try {
        const nextBoard = Math.floor((parseInt(nextPid, 10)) / 12);
        const nextJ = (parseInt(nextPid, 10)) % 12 || 12; // convert pid to 1..12 within board
        const numInputs2 = parseInt(ioInfo[nextBoard]?.[2] || '0', 10);
        const allowedInput2 = (nextJ <= numInputs2) && ((nextBoard !== 0) || (nextJ !== 1));
        const equipOptions2 = (nextBoard >= 1 && nextJ >= 7) ? [...inputList, ...lights] : inputList;
        const nextValue = (value + 1).toString();
        if (allowedInput2 && equipOptions2.some(opt => opt.value === nextValue)) {
          ioConfig.config.inputConfig[parseInt(nextPid, 10)] = nextValue;
        }
      } catch {}
    }
    // Force reactivity
    ioConfig.config.outputConfig = [...ioConfig.config.outputConfig];
    ioConfig.config.inputConfig = [...ioConfig.config.inputConfig];
    ioConfig = { ...ioConfig };
  }

  function inputChanged(event: CustomEvent<{ i: number, j: number, value: string }>) {
    const i = event.detail.i;
    const j = event.detail.j;
    const pid = (12 * i + j).toString();
    ioConfig.config.inputConfig[parseInt(pid, 10)] = event.detail.value;
    ioConfig.config.inputConfig = [...ioConfig.config.inputConfig];
    ioConfig = { ...ioConfig };
  }

  async function outputRenamed(event: CustomEvent<{ i: number, j: number, name: string, type: number }>) {
    const pid = (event.detail.i * 12) + event.detail.j;
    const idx = parseInt(event.detail.type === 0 ? ioConfig.config.outputConfig[pid] : ioConfig.config.inputConfig[pid], 10);
    if (idx !== -1) {
      wait = true;
      await fetch(getHttpUrl(`/iot/ioconfig/${idx}/${event.detail.name}`), {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json'
        }
      });
      const parts = ioConfig.ioNames[idx].split(':');
      parts[0] = event.detail.name;
      ioConfig.ioNames[idx] = parts.join(':');
      buildEquipmentList();
      outputList = [...outputList];
      inputList = [...inputList];
      ioConfig = {...ioConfig};
      wait = false;
    }
  }

  onMount(async () => {
    try {
      $navigationStore.data = getHttpUrl(`/iot/ioconfig`);
      $navigationStore.isDirty = () => !isEqual(ioConfig.config.outputConfig, data.config.outputConfig) || !isEqual(ioConfig.config.inputConfig, data.config.inputConfig);
      setup(data);
    } catch (error) {
      console.error(error);
    }
    ready = true;
  });

  function setup(data: IOConfigType) {
    // Initialize page state with board arrays supplied
    if (data) {
      setupIoConfig(cloneDeep(data));
    }
  }


  let defaultTimer: NodeJS.Timeout | undefined;
  let pendingDefaultReset = false; // indicates we expect fresh defaults on next data load

  async function setToDefault() {
    // If user clicks repeatedly, clear any pending refresh
    if (defaultTimer) {
      clearTimeout(defaultTimer);
      defaultTimer = undefined;
    }
    wait = true;
    try {
      await fetch(getHttpUrl('/iot/button'), {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          tag: 'button2',
          value: 'resetIoConfig',
        }),
      });
      // Backend needs time to regenerate default IO config; wait 10s then mark page data invalid so loader refetches
      defaultTimer = setTimeout(() => {
        pendingDefaultReset = true; // mark that incoming data should overwrite regardless of dirty state
        $navigationStore = { ...$navigationStore, invalidate: true }; // let GellertPage clear the flag
        wait = false;
        defaultTimer = undefined;
      }, 5000);
    } catch (e) {
      console.error('Set To Default request failed:', e);
      wait = false;
    }
  }

  onDestroy(() => {
    if (defaultTimer) {
      clearTimeout(defaultTimer);
    }
  });

  // When new data arrives after invalidation, GellertPage will have already cleared invalidate.
  // We just need to detect the new reference and decide whether to apply it.

  // Additionally, if SvelteKit replaces the `data` prop (e.g., due to a parent invalidation) without using invalidate flag
  // ensure we realign our local state. We guard to avoid overwriting unsaved edits if user has modified (isDirty true).
  let lastDataRef: IOConfigType | undefined = undefined;
  $: if (data && data !== lastDataRef) {
    // Force apply if pending default reset; otherwise avoid overwriting unsaved edits
    if (pendingDefaultReset || !$navigationStore.isDirty()) {
      setup(data);
      lastDataRef = data;
      pendingDefaultReset = false; // consumed
      $navigationStore.isDirty = () => !isEqual(ioConfig.config.outputConfig, data.config.outputConfig) || !isEqual(ioConfig.config.inputConfig, data.config.inputConfig);
    }
  }
</script>

<GellertPage {wait} {title} {ready} level={2} name="ioconfig">
  <Card class="xl:w-11/12 text-size-xl md:mx-2 xl:mx-auto flex flex-col">
    <ScrollableArea>
      {#each ioConfig.ioAvailable as io, i}
        {#if ioInfo[i][0].indexOf('none') === -1}
          <Table class="mb-2">
            <Row class="text-size-large">
              <Column class="w-2/12 my-2 border-r border-gray-400 font-bold">{getBoardName(i)}</Column>
              <Column class="w-5/12 my-2 border-r border-gray-400 font-bold">{ $t('global.output') }</Column>
              <Column class="w-5/12 my-2 font-bold">{ $t('global.input') }</Column>
            </Row>
            {#if isGDCBoard(i)}
              <!-- GDC Mode: Paired I/O per actuator (DO1/2=Act1, DO3/4=Act2, ...) -->
              <Row class="bg-gray-50">
                <Column class="w-2/12 border-r border-gray-400 text-size-small text-gray-500 italic" />
                <Column class="w-5/12 border-r border-gray-400 text-size-small text-gray-500 italic px-2">
                  Paired: Open / Close per actuator
                </Column>
                <Column class="w-5/12 text-size-small text-gray-500 italic px-2">
                  Paired: Open Limit / Close Limit per actuator
                </Column>
              </Row>
              {#each [1, 2, 3, 4, 5] as actNum}
                <Row>
                  <Column class="w-2/12 border-r border-gray-400 text-size-large font-semibold">{$t('level2.door.doors')} {actNum}</Column>
                  <Column class="w-5/12 border-r border-gray-400">
                    <div class="flex gap-4 px-2">
                      <span class="text-green-700">DO{actNum * 2 - 1} Open</span>
                      <span class="text-red-700">DO{actNum * 2} Close</span>
                    </div>
                  </Column>
                  <Column class="w-5/12">
                    <div class="flex gap-4 px-2">
                      <span class="text-green-700">DI{actNum * 2 - 1} Open SW</span>
                      <span class="text-red-700">DI{actNum * 2} Close SW</span>
                    </div>
                  </Column>
                </Row>
              {/each}
            {:else}
              <!-- Standard Mode: Show 10 I/O pins -->
              {#each Array.from({ length: parseInt(ioInfo[i][1]) }, (_, index) => index + 1) as j}
                <Row>
                  <Column class="w-2/12 border-r border-gray-400 text-size-large">{j}</Column>
                  <OutputCell
                    {i}
                    {j}
                    {ioConfig}
                    {outputList}
                    {lights}
                    value={(ioConfig.config.outputConfig[(12*i)+j] ?? '-1')}
                    on:change={outputChanged}
                    on:rename={outputRenamed}
                    validation={validation[`o${(12*i) + j}`]}
                  />
                  <InputCell {i} {j} ioInfo={ioInfo[i]} {ioConfig} {inputList} {lights} value={(ioConfig.config.inputConfig[(12*i)+j] ?? '-1')} on:change={inputChanged} on:rename={outputRenamed} validation={validation[`i${(12*i) + j}`]}/>
                </Row>
              {/each}
            {/if}
          </Table>
        {/if}
      {/each}

      <svelte:fragment slot="footer-center">
        <SaveButton
          edit={true}
          bind:wait={wait}
          data={ioConfig.config}
          route='ioconfig'
          bind:original={data.config}
          bind:validation={validation}
          autoSave
        />
      </svelte:fragment>

      <svelte:fragment slot="footer-right">
        <Button size="xl" class="ml-auto !mb-0" on:click={setToDefault}>{ $t('level2.ioconfig.set-to-default') }</Button>
      </svelte:fragment>
    </ScrollableArea>
  </Card>
</GellertPage>



