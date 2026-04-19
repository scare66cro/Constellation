<script lang="ts">
	import { onMount } from "svelte";
  import GellertPage from "$lib/components/GellertPage.svelte";
	import Card from "$lib/ui/Card.svelte";
	import Button from "$lib/ui/Button.svelte";
	import TextField from "$lib/ui/TextField.svelte";
	import Table from "$lib/ui/Table.svelte";
	import Row from "$lib/ui/Row.svelte";
	import Column from "$lib/ui/Column.svelte";
	import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
	import { AdornmentType } from "$lib/business/adornmentType";
	import SaveButton from "$lib/components/SaveButton.svelte";
	import { localeStore, navigationStore } from "$lib/store";
  import { getHttpUrl } from "$lib/business/util";
  import { cloneDeep, isEqual } from "lodash-es";
	import { BoardTypes } from "$lib/business/analog";
	import Select from "$lib/ui/Select.svelte";
  import { t } from "svelte-i18n";
  import { getModalStore, type ModalSettings } from "@skeletonlabs/skeleton";
    export let data: { allBoards: string[][], unifiedSensors: any };

    let title = $t('level2.analog.analog-board-setup');

    let edit = true;

    const modalStore = getModalStore();

    // State for all boards and current index
    let allBoards: string[][] = [];
    let currentBoardIndex = 0;

    let validation: Record<string, string> = {
      BDis: '',
      Sen1Off: '',
      Sen1Dis: '',
      Sen2Off: '',
      Sen2Dis: '',
      Sen3Off: '',
      Sen3Dis: '',
      Sen4Off: '',
      Sen4Dis: ''
    };

  $: sensorOff = [
    validation.Sen1Off,
    validation.Sen2Off,
    validation.Sen3Off,
    validation.Sen4Off
  ];

  const PileTempOptions: {text: string, value: string}[] = [
    { text: $t('global.pile-temperature'), value: '9' },
    { text: `${$t('level2.analog.return-temperature')} #2`, value: '5'}
  ];

  const PileHumidOptions: {text: string, value: string}[] = [
    { text: 'CO2 #1', value: '2' },
    { text: 'CO2 #2', value: '8' },
    { text: $t('global.pile-humidity'), value: '10' },
    { text: `${$t('global.return-humidity')} #1`, value: '6' },
    { text: `${$t('global.return-humidity')} #2`, value: '7' },
    { text: `${$t('global.static-pressure')}`, value: '11'},
  ];

  $: if (ready && analog?.length > 4) analog[4] = boardDisabled ? '1' : '0';
  $: if (ready && analog?.length > 23) {
    analog[8] = sensorDisabled[0] ? '1' : '0';
    analog[13] = sensorDisabled[1] ? '1' : '0';
    analog[18] = sensorDisabled[2] ? '1' : '0';
    analog[23] = sensorDisabled[3] ? '1' : '0';
  }

  let ready = false;
  let wait = false;
  let analog: string[] = [];
  let boardDisabled = false;
  let sensorDisabled: boolean[] = [];
  let reset = false;

  $: boardType = getBoardType(analog?.length > 1 ? analog[1] : undefined);
  $: adornments = [
    getAdornment(analog?.length > 5 ? analog[5] : undefined),
    getAdornment(analog?.length > 10 ? analog[10] : undefined),
    getAdornment(analog?.length > 15 ? analog[15] : undefined),
    getAdornment(analog?.length > 20 ? analog[20] : undefined)
  ];

  // Populate empty sensor labels with defaults based on board type
  function populateDefaultLabels() {
    if (!analog || analog.length < 24) return;
    
    for (let i = 0; i < 4; i++) {
      const labelIndex = i * 5 + 6;
      const label = analog[labelIndex];
      // Only set default if label is empty or whitespace
      if (!label || label.trim() === '') {
        const boardNum = analog[0];
        const sensorType = analog[i * 5 + 5];
        analog[labelIndex] = getDefaultSensorLabel(boardNum, i, sensorType);
      }
    }
    // Also set board label if empty
    if (!analog[2] || analog[2].trim() === '') {
      analog[2] = getDefaultBoardLabel(analog[0], analog[1]);
    }
  }

  // Get default board label based on board number and type
  function getDefaultBoardLabel(boardNum: string, boardType: string): string {
    if (boardNum === '1') {
      return $t('level2.analog.default-temperature') || 'Default Temperature';
    }
    return getBoardType(boardType);
  }

  function mergeSensorData(boards: string[][], unified: any): string[][] {
    if (!boards || !unified) return [];
    
    // Safety check if unified is malformed
    const { SensorLabels, SensorValues, SensorSettings } = unified;
    
    return boards.map(board => {
        // Start with board definition [Addr, Type, Label, Version, Disabled]
        const newBoard = board.slice(0, 5); 
        
        const addr = parseInt(newBoard[0]);
        if (isNaN(addr)) return newBoard;
        
        const boardIdx = addr - 1;
        
        for (let s = 0; s < 4; s++) {
            const sid = boardIdx * 4 + s;
            // Default to enabled (0) so values show up even if settings are missing
            // Default type to 255 (Undefined)
            let type = '255', label = '', offset = '0.0', dis = '0', val = '--';
            
            // Find in Settings (Stride 4: SID, Type, Offset, Dis)
            if (SensorSettings && Array.isArray(SensorSettings)) {
                for (let i = 0; i < SensorSettings.length; i += 4) {
                    if (parseInt(SensorSettings[i]) === sid) {
                        type = SensorSettings[i+1];
                        offset = SensorSettings[i+2];
                        dis = SensorSettings[i+3] === '1' ? '1' : '0';
                        break;
                    }
                }
            }
            
            // Find in Labels (Stride 2: SID, Label)
            if (SensorLabels && Array.isArray(SensorLabels)) {
                for (let i = 0; i < SensorLabels.length; i += 2) {
                    if (parseInt(SensorLabels[i]) === sid) {
                        label = SensorLabels[i+1];
                        break;
                    }
                }
            }
            
            // Find in Values (Stride 2: SID, Value)
            if (SensorValues && Array.isArray(SensorValues)) {
                for (let i = 0; i < SensorValues.length; i += 2) {
                    if (parseInt(SensorValues[i]) === sid) {
                        val = SensorValues[i+1];
                        break;
                    }
                }
            }
            
            if (dis === '1') val = 'dis';
            
            // Push in order expected by template: [Type, Label, Offset, Disabled, Value]
            newBoard.push(type, label, offset, dis, val);
        }
        return newBoard;
    });
  }

  onMount(async () => {
		try {
      // SSR returns {} for IoT data – detect and fetch client-side
      let allBoardsRaw = data.allBoards;
      let unifiedRaw = data.unifiedSensors;
      if (!Array.isArray(allBoardsRaw) || allBoardsRaw.length === 0) {
        const [boardsRes, unifiedRes] = await Promise.all([
          fetch(getHttpUrl('/iot/analog/all')),
          fetch(getHttpUrl('/iot/sensors/unified'))
        ]);
        allBoardsRaw = await boardsRes.json();
        unifiedRaw = await unifiedRes.json();
      }
      allBoards = mergeSensorData(allBoardsRaw, unifiedRaw);
      // Find first non-empty board or default to first
      currentBoardIndex = allBoards.findIndex(b => b && b.length > 0);
      if (currentBoardIndex < 0) currentBoardIndex = 0;
      analog = allBoards[currentBoardIndex] ? cloneDeep(allBoards[currentBoardIndex]) : [];
      
      $navigationStore.data = getHttpUrl(`/iot/analog`);
      $navigationStore.isDirty = () => !isEqual(analog, allBoards[currentBoardIndex]);
      
      populateDefaultLabels();
      updateDisabled();
      syncCurrentBoard();
		} catch (error) {
		}
		ready = true;
  });

  function updateSensorFromBoard() {
    sensorDisabled = [8, 13, 18, 23].map(i => analog?.length > 4 && analog[4] === '1')
  }

  function updateDisabled() {
    sensorDisabled = [8, 13, 18, 23].map(i => analog?.length > 23 && analog[i] === '1');
    boardDisabled = analog?.length > 4 && analog[4] === '1';
  }

  function syncCurrentBoard() {
    if (allBoards.length === 0) return;
    allBoards[currentBoardIndex] = cloneDeep(analog);
    $navigationStore.isDirty = () => !isEqual(analog, allBoards[currentBoardIndex]);
  }

  function checkDirty(action: () => void) {
    if ($navigationStore.isDirty()) {
      const modal: ModalSettings = {
        type: 'confirm',
        // Data
        title: $t('global.confirm'),
        body: $t('global.are-you-sure'),
      };
      modal.buttonTextCancel=$t('global.no');
     	modal.buttonTextConfirm=$t('global.yes');

      // TRUE if confirm pressed, FALSE if cancel pressed
      modal.response = (r: boolean) => { if (r) {
        action();
      }};
      modalStore.trigger(modal);
    } else {
      action();
    }
  }

  async function refresh() {
    wait = true;
    reset = true;
    try {
      const [boardsRes, unifiedRes] = await Promise.all([
        fetch(getHttpUrl('/iot/analog/all')),
        fetch(getHttpUrl('/iot/sensors/unified'))
      ]);
      
      const rawBoards = await boardsRes.json();
      const unified = await unifiedRes.json();
      
      allBoards = mergeSensorData(rawBoards, unified);
      analog = allBoards[currentBoardIndex] ? cloneDeep(allBoards[currentBoardIndex]) : [];
      populateDefaultLabels();
      updateDisabled();
      syncCurrentBoard();
    } catch (error) {
      console.error(error);
    }
    wait = false;
  }

  function back() {
    checkDirty(() => {
      if (!allBoards || allBoards.length === 0) return;
      
      // Find previous non-empty board with wrap-around
      let prevIndex = currentBoardIndex;
      let count = 0;
      
      do {
        prevIndex = (prevIndex - 1 + allBoards.length) % allBoards.length;
        count++;
        // If we found a valid board
        if (allBoards[prevIndex] && allBoards[prevIndex].length > 0) {
          break;
        }
      } while (count < allBoards.length); // Prevent infinite loop if no boards found

      // If we found a valid board (even if it's the same one), switch to it
      if (allBoards[prevIndex] && allBoards[prevIndex].length > 0) {
        currentBoardIndex = prevIndex;
        analog = cloneDeep(allBoards[currentBoardIndex]);
        populateDefaultLabels();
        updateDisabled();
        syncCurrentBoard();
        reset = true;
      }
    });
  }

  function next() {
    checkDirty(() => {
      if (!allBoards || allBoards.length === 0) return;

      // Find next non-empty board with wrap-around
      let nextIndex = currentBoardIndex;
      let count = 0;

      do {
        nextIndex = (nextIndex + 1) % allBoards.length;
        count++;
        // If we found a valid board
        if (allBoards[nextIndex] && allBoards[nextIndex].length > 0) {
          break;
        }
      } while (count < allBoards.length); // Prevent infinite loop

      if (allBoards[nextIndex] && allBoards[nextIndex].length > 0) {
        currentBoardIndex = nextIndex;
        analog = cloneDeep(allBoards[currentBoardIndex]);
        populateDefaultLabels();
        updateDisabled();
        syncCurrentBoard();
        reset = true;
      }
    });
  }

  async function findBoards() {
    checkDirty(async () => {
      wait = true;
      reset = true;
      try {
        const [boardsRes, unifiedRes] = await Promise.all([
          fetch(getHttpUrl('/iot/analog/all')),
          fetch(getHttpUrl('/iot/sensors/unified'))
        ]);
        
        const rawBoards = await boardsRes.json();
        const unified = await unifiedRes.json();
        
        allBoards = mergeSensorData(rawBoards, unified);
        currentBoardIndex = allBoards.findIndex(b => b && b.length > 0);
        if (currentBoardIndex < 0) currentBoardIndex = 0;
        analog = allBoards[currentBoardIndex] ? cloneDeep(allBoards[currentBoardIndex]) : [];
        populateDefaultLabels();
        updateDisabled();
        syncCurrentBoard();
      } catch (error) {
        console.error(error);
      }
      wait = false;
    });
  }

  function handleSaveComplete(event: CustomEvent<{ success: boolean }>) {
    if (!event.detail.success) return;

    // Optimistic commit: the SaveButton already cloned the saved data into
    // `allBoards[currentBoardIndex]` via bind:original; keep local state in sync.
    syncCurrentBoard();
    populateDefaultLabels();
    updateDisabled();
    reset = true;
  }

  function getAdornment(sensorType: string | undefined): AdornmentType {
    switch (sensorType) {
      case '1':
      case '6':
      case '7':
      case '10':
        return AdornmentType.Humidity;
      case '2':
      case '8':
        return AdornmentType.CO2;
      case '11':
        return AdornmentType.StaticPressure;
      case '0':
      case '3':
      case '4':
      case '5':
      case '9':
      default:
        return AdornmentType.Temperature;
    }
  }

  function getBoardType(boardType: string | undefined): string {
    switch (boardType) {
      case '0':
        return $t('level2.analog.temperature-ir');
      case '1':
        return $t('level2.analog.humidity');
      case '2':
        return 'CO2';
      case '3':
        return $t('level2.analog.temperature');
      default:
        return $t('level2.analog.unknown');
    }
  }

  function getSensorType(sensorType: string): string {
    switch (sensorType) {
      case '1':
        return $t('level2.analog.humidity');
      case '2':
        return 'CO2 #1';
      case '0':
      case '3':
        return $t('level2.analog.temperature');
      case '4':
        return `${$t('level2.analog.return-temperature')} #1`;
      case '5':
        return `${$t('level2.analog.return-temperature')} #2`;
      case '6':
        return `${$t('global.return-humidity')} #1`;
      case '7':
        return `${$t('global.return-humidity')} #2`;
      case '8':
        return 'CO2 #2';
      case '9':
        return $t('global.pile-temperature');
      case '10':
        return $t('global.pile-humidity');
      case '11':
        return $t('global.static-pressure');
      default:
        return $t('level2.analog.undefined');
    }
  }

  // Get default sensor label based on board number and sensor index
  function getDefaultSensorLabel(boardNum: string, sensorIndex: number, sensorType: string): string {
    // Board 1 = Default Temperature Board
    if (boardNum === '1') {
      switch (sensorIndex) {
        case 0: return $t('level2.analog.plenum-temp-1') || 'Plenum Temperature';
        case 1: return $t('level2.analog.plenum-temp-2') || 'Plenum 2 Temperature';
        case 2: return $t('level2.analog.outside-temp') || 'Outside Temperature';
        case 3: return $t('level2.analog.return-temp') || 'Return Temperature';
        default: return getSensorType(sensorType);
      }
    }
    // Other boards - use sensor type as default
    return getSensorType(sensorType);
  }

  // Get sensor label with fallback to default
  function getSensorLabel(sensorIndex: number): string {
    const label = analog[sensorIndex * 5 + 6];
    if (label && label.trim() !== '') {
      return label;
    }
    const boardNum = analog[0];
    const sensorType = analog[sensorIndex * 5 + 5];
    return getDefaultSensorLabel(boardNum, sensorIndex, sensorType);
  }

  // Set sensor label (for binding)
  function setSensorLabel(sensorIndex: number, value: string) {
    analog[sensorIndex * 5 + 6] = value;
    analog = analog; // trigger reactivity
  }
</script>

<GellertPage {wait} {ready} {title} level={2} name="analog">
  <Card class="mx-4 flex flex-col">
    <Table class="mb-2 {$localeStore !== 'en-us' ? 'text-size-large' : 'text-size-xl'}">
      <Row>
        <Column class="w-1/12 border-r border-gray-400 px-2"><b>{ $t('global.board') }</b></Column>
        <Column class="w-3/12 border-r border-gray-400 px-2"><b>{ $t('level2.analog.type') }</b></Column>
        <Column class="w-5/12 border-r border-gray-400 px-2"><b>{ $t('level2.analog.label') }</b></Column>
        <Column class="w-2/12 border-r border-gray-400 px-2"><b>{ $t('level2.analog.version') }</b></Column>
        <Column class="w-1/12 border-r border-gray-400 px-2"><b>{ $t('level2.analog.disabled') }</b></Column>
      </Row>
      <Row>
        {#if analog?.length > 3}
        <Column class="w-1/12 !py-1 px-2 border-r border-gray-400">
          <span class="text-size-xl">#</span> <TextField class="w-fit" size="xl" bind:value={analog[0]} edit={false}/>  
        </Column>
        <Column class="w-3/12 !py-1 px-2 border-r border-gray-400">
          <TextField class="w-fit mx-auto" size="xl" bind:value={boardType} edit={false}/>
        </Column>
        <Column class="w-5/12 !py-1 px-2 border-r border-gray-400">
          <TextField class="w-fit" size="xl" bind:value={analog[2]} {edit} label="Board Name" keyboardType={KeyboardTypes.Alpha}/>
        </Column>
        <Column class="w-2/12 !py-1 px-2 border-r border-gray-400">
          <TextField class="w-fit mx-auto" size="xl" bind:value={analog[3]} edit={false}/>
        </Column>
        <Column class="w-1/12 !py-1 px-2 ">
          <input class="checkbox w-8 h-8" type="checkbox" bind:checked={boardDisabled} on:change={updateSensorFromBoard}/>
        </Column>
        {:else}
          <Column class="w-1/12 !py-1 px-2 border-r border-gray-400" colspan={5}>
            { $t('global.none') }
          </Column>
        {/if}
      </Row>
    </Table>
    <Table class={$localeStore !== 'en-us' ? 'text-size-large' : 'text-size-xl'}>
      <Row>
        <Column class="w-1/12 border-r border-gray-400 px-2"><b>{ $t('global.sensor') }</b></Column>
        <Column class="w-3/12 border-r border-gray-400 px-2"><b>{ $t('level2.analog.type') }</b></Column>
        <Column class="w-3/12 border-r border-gray-400 px-2"><b>{ $t('level2.analog.label') }</b></Column>
        <Column class="w-2/12 border-r border-gray-400 px-2"><b>{ $t('level2.analog.offset') }</b></Column>
        <Column class="w-2/12 border-r border-gray-400 px-2"><b>{ $t('level2.analog.value') }</b></Column>
        <Column class="w-1/12 px-2"><b>{ $t('level2.analog.disabled') }</b></Column>
      </Row>
      {#if analog?.length > 23}
        {#key analog[0]}
          {#each [0,1,2,3] as i}
            <Row>
              <Column class="w-1/12 py-1 border-r border-gray-400 px-2">
                <span class="text-size-xl"># {i+1}</span>
              </Column>
              <Column class="w-4/12 border-r border-gray-400 py-1 px-2 text-size-large">
                {#if analog[1] === BoardTypes.BOARD_TEMP}
                  {#if analog[0] === '1'}
                    {getSensorType(analog[i*5 + 5])}
                  {:else}
                    <Select size="lg" extended="w-full" class="mx-2" bind:value={analog[i*5 + 5]} options={PileTempOptions} {edit} on:change={() => analog = analog}/>
                  {/if}
                {:else}
                  <Select size="lg" extended="w-full" class="mx-2" bind:value={analog[i*5 + 5]} options={PileHumidOptions} {edit} on:change={() => analog = analog} />
                {/if}
              </Column>
              <Column class="w-3/12 py-1 border-r border-gray-400 px-2">
                <TextField class="w-fit" size="xl" bind:value={analog[i*5+6]} {edit} label="Sensor Name" keyboardType={KeyboardTypes.Alpha}/>
              </Column>
              <Column class="w-1/12 py-1 border-r border-gray-400 px-2">
                <TextField class="w-36" size="xl" bind:value={analog[i*5+7]} {edit} label="Offset Value" keyboardType={KeyboardTypes.Float} validation={sensorOff[i]}/>
              </Column>
              <Column class="w-2/12 py-1 border-r border-gray-400 px-2 text-green-700">
                <TextField class="w-fit text-green-700" size="xl" bind:value={analog[i*5+9]} edit={false} adornmentType={adornments[i]}/>
              </Column>
              <Column class="w-1/12 py-1 px-2">
                <input class="checkbox w-8 h-8" type="checkbox" bind:checked={sensorDisabled[i]} />
              </Column>
            </Row>
          {/each}
        {/key}
      {/if}
    </Table>
    <div class="flex flex-row">
      <div class="w-1/4"><Button size="xl" on:click={refresh}>{ $t('level2.analog.refresh-sensors') }</Button></div>
      <div class="w-2/4 flex flex-row">
        <Button size="xl" class="ml-auto mr-2 mb-0" on:click={back}>{ $t('level2.analog.previous-board') }</Button>
        <SaveButton {edit} bind:wait={wait} data={analog} bind:original={allBoards[currentBoardIndex]} route="analog" bind:validation={validation} bind:reset={reset} on:complete={handleSaveComplete} autoSave />
        <Button size="xl" class="ml-2 mr-auto mb-0" on:click={next}>{ $t('level2.analog.next-board') }</Button>
      </div>
      <div class="w-1/4 flex flex-row"><Button class="ml-auto mr-2" size="xl" on:click={findBoards}>{ $t('level2.analog.find-boards') }</Button></div>
    </div>
  </Card>
</GellertPage>



