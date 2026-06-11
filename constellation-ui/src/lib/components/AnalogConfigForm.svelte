<script lang="ts">
  import { onMount } from "svelte";
  import Card from "$lib/ui/Card.svelte";
  import Button from "$lib/ui/Button.svelte";
  import TextField from "$lib/ui/TextField.svelte";
  import Table from "$lib/ui/Table.svelte";
  import Row from "$lib/ui/Row.svelte";
  import Column from "$lib/ui/Column.svelte";
  import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
  import { AdornmentType } from "$lib/business/adornmentType";
  import SaveButton from "$lib/components/SaveButton.svelte";
  import { localeStore, navigationStore, keyboardStore } from "$lib/store";
  import { getHttpUrl } from "$lib/business/util";
  import { cloneDeep, isEqual } from "lodash-es";
  import { BoardTypes } from "$lib/business/analog";
  import Select from "$lib/ui/Select.svelte";
  import { t } from "svelte-i18n";
  import { getModalStore, type ModalSettings } from "@skeletonlabs/skeleton";
  import { get } from "svelte/store";
  import { analogBoards, analogBoardArrays } from "$lib/business/protoStores";
  import { writeProtoRaw, buildForceVarintBytes, buildForceFloatBytes, wrapAsLengthDelim } from "$lib/business/protoWrite";
  import { TAG } from "$lib/business/protoTags";

  // Shared body of the Analog Board Setup page (level2/analog): per-board sensor
  // table (type/label/offset/disabled), board nav (prev/next/find), and the
  // custom 4-20 mA scaling modal. Rendered in two presentations from ONE source
  // of truth — the classic touchscreen page AND a WIDE dashboard modal (⚙ Setup
  // → System → Analog Boards). Prop contract mirrors FanSpeedForm (L2).
  // docs/spatial-ui-page-migration.md
  export let wait = false;
  export let ready = false;
  export let embedded = false;
  export let theme: 'light' | 'dark' = 'light';
  export let canEdit: boolean | null = null;

  let title = $t('level2.analog.analog-board-setup');
  $: edit = canEdit ?? ($navigationStore.level > 0);

  const modalStore = getModalStore();

  // State for all boards and current index
  let allBoards: string[][] = [];
  let currentBoardIndex = 0;

  let validation: Record<string, string> = {
    BDis: '',
    Sen1Off: '', Sen1Dis: '',
    Sen2Off: '', Sen2Dis: '',
    Sen3Off: '', Sen3Dis: '',
    Sen4Off: '', Sen4Dis: ''
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

  let analog: string[] = [];
  let boardDisabled = false;
  let sensorDisabled: boolean[] = [];
  let reset = false;

  /* Custom 4-20 mA scaling per slot for the CURRENT board. */
  type CustomCfg = { displayUnit: number; engMin: number; engMax: number };
  const EMPTY_CFG: CustomCfg = { displayUnit: 0, engMin: 0, engMax: 0 };
  let customConfigs: CustomCfg[] = [ {...EMPTY_CFG}, {...EMPTY_CFG}, {...EMPTY_CFG}, {...EMPTY_CFG} ];

  let showCustomModal = false;
  let modalRows: { displayUnit: string; engMin: string; engMax: string }[] = [];
  let customFocusSlot = -1;

  $: unitOptions = [
    { value: '0', text: $t('level2.analog.unit-none') },
    { value: '1', text: $t('level2.analog.unit-percent') },
    { value: '2', text: $t('level2.analog.unit-ppm') },
    { value: '3', text: $t('level2.analog.unit-inh2o') },
    { value: '4', text: $t('level2.analog.unit-volts') },
    { value: '5', text: $t('level2.analog.unit-amps') },
    { value: '6', text: $t('level2.analog.unit-cfm') },
    { value: '7', text: $t('level2.analog.unit-fpm') },
    { value: '8', text: $t('level2.analog.unit-temp420') },
    { value: '9', text: $t('level2.analog.unit-psi') },
  ];

  function refreshCustomFromStore() {
    if (!analog || analog.length < 1) return;
    const boardIdx = (parseInt(analog[0], 10) || 1) - 1;
    const board = ($analogBoards || []).find(b => (b as any)?.boardIdx === boardIdx);
    const sensors = (board as any)?.sensors ?? [];
    const out: CustomCfg[] = [ {...EMPTY_CFG}, {...EMPTY_CFG}, {...EMPTY_CFG}, {...EMPTY_CFG} ];
    for (const s of sensors) {
      const slot = Number(s?.slot ?? 0);
      if (slot >= 0 && slot < 4) {
        out[slot] = {
          displayUnit: Number(s?.displayUnit ?? 0),
          engMin: Number(s?.engMin ?? 0),
          engMax: Number(s?.engMax ?? 0),
        };
      }
    }
    customConfigs = out;
  }

  function openCustomModal(focusSlot: number = -1) {
    refreshCustomFromStore();
    modalRows = customConfigs.map(c => ({
      displayUnit: String(c.displayUnit ?? 0),
      engMin: String(c.engMin ?? 0),
      engMax: String(c.engMax ?? 0),
    }));
    customFocusSlot = focusSlot;
    showCustomModal = true;
  }

  function cancelCustomModal() {
    showCustomModal = false;
    customFocusSlot = -1;
  }

  function applyCustomModal() {
    customConfigs = modalRows.map(r => ({
      displayUnit: parseInt(r.displayUnit, 10) || 0,
      engMin: parseFloat(r.engMin) || 0,
      engMax: parseFloat(r.engMax) || 0,
    }));
    showCustomModal = false;
    customFocusSlot = -1;
    void saveAnalog(analog);
  }

  $: boardType = getBoardType(analog?.length > 1 ? analog[1] : undefined);
  $: adornments = [
    getAdornment(analog?.length > 5 ? analog[5] : undefined),
    getAdornment(analog?.length > 10 ? analog[10] : undefined),
    getAdornment(analog?.length > 15 ? analog[15] : undefined),
    getAdornment(analog?.length > 20 ? analog[20] : undefined)
  ];

  function populateDefaultLabels() {
    if (!analog || analog.length < 24) return;
    const boardNum = analog[0];
    if (boardNum === '2') {
      analog[5]  = '1';
      analog[10] = '1';
      analog[6]  = $t('level2.analog.outside-humidity') || 'Outside Humidity';
      analog[11] = $t('level2.analog.plenum-humidity')  || 'Plenum Humidity';
      const validHumidTypes = new Set(['2','6','7','8','10','11']);
      if (!validHumidTypes.has(analog[15])) analog[15] = '6';
      if (!validHumidTypes.has(analog[20])) analog[20] = '2';
      const stale = new Set([
        $t('level2.analog.plenum-temp-1'), $t('level2.analog.plenum-temp-2'),
        $t('level2.analog.outside-temp'),  $t('level2.analog.return-temp'),
        $t('level2.analog.default-temperature'),
        'Plenum Temperature', 'Plenum 2 Temperature',
        'Outside Temperature', 'Return Temperature',
      ]);
      if (stale.has(analog[16])) analog[16] = '';
      if (stale.has(analog[21])) analog[21] = '';
    }
    for (let i = 0; i < 4; i++) {
      const labelIndex = i * 5 + 6;
      const label = analog[labelIndex];
      if (!label || label.trim() === '') {
        const sensorType = analog[i * 5 + 5];
        analog[labelIndex] = getDefaultSensorLabel(boardNum, i, sensorType);
      }
    }
    if (!analog[2] || analog[2].trim() === '') {
      analog[2] = getDefaultBoardLabel(analog[0], analog[1]);
    }
  }

  function getDefaultBoardLabel(boardNum: string, boardType: string): string {
    if (boardNum === '1') return $t('level2.analog.default-temperature') || 'Default Temperature';
    if (boardNum === '2') return $t('level2.analog.default-humidity') || 'Default Humidity';
    return getBoardType(boardType);
  }

  function isDirty(): boolean {
    return allBoards.length > 0
        && currentBoardIndex >= 0
        && currentBoardIndex < allBoards.length
        && !isEqual(analog, allBoards[currentBoardIndex]);
  }

  function hydrateFromStore(rows: string[][]) {
    if (!Array.isArray(rows) || rows.length === 0) return;
    if (isDirty()) return;
    const prevAddr = analog?.[0];
    allBoards = cloneDeep(rows);
    let idx = -1;
    if (prevAddr !== undefined) idx = allBoards.findIndex(b => b?.[0] === prevAddr);
    if (idx < 0) idx = 0;
    currentBoardIndex = idx;
    analog = allBoards[currentBoardIndex] ? cloneDeep(allBoards[currentBoardIndex]) : [];
    populateDefaultLabels();
    updateDisabled();
    syncCurrentBoard();
    ready = true;
  }

  onMount(() => {
    if (!embedded) $navigationStore.isDirty = isDirty;
    const unsub = analogBoardArrays.subscribe(hydrateFromStore);
    const tm = setTimeout(() => { ready = true; }, 1500);
    return () => { unsub(); clearTimeout(tm); };
  });

  // Modal close hook — flush the current board if it has unsaved edits.
  let saveBtn: { save: () => Promise<void> } | undefined;
  export async function flush(): Promise<void> {
    if (isDirty() && saveBtn) await saveBtn.save();
  }

  function updateSensorFromBoard() {
    sensorDisabled = [8, 13, 18, 23].map(() => analog?.length > 4 && analog[4] === '1');
  }

  function updateDisabled() {
    sensorDisabled = [8, 13, 18, 23].map(i => analog?.length > 23 && analog[i] === '1');
    boardDisabled = analog?.length > 4 && analog[4] === '1';
  }

  function syncCurrentBoard() {
    if (allBoards.length === 0) return;
    allBoards[currentBoardIndex] = cloneDeep(analog);
    if (!embedded) $navigationStore.isDirty = () => !isEqual(analog, allBoards[currentBoardIndex]);
    refreshCustomFromStore();
  }

  async function saveAnalog(arr: string[]): Promise<void> {
    if (!Array.isArray(arr) || arr.length < 25) return;
    const boardIdx = parseInt(arr[0] ?? '0', 10) || 0;

    // Build the NESTED message the firmware decoder expects
    // (lp_settings.c: LpSettings_ApplyAnalogBoard → apply_analog_board_one →
    // apply_sensor_config). Wire shape (settings.proto numbering — the side
    // the firmware actually decodes):
    //   payload = { board@1 (submsg) }
    //   board   = { address@1 varint, sensors@4 (repeated submsg) }
    //   sensor  = { slot@1, label@2 string, offset@3 float, disabled@4, type@5 }
    // Earlier this emitted a FLAT { boardIdx@1 varint, sensor@6 } shape with
    // io.proto-style field numbers, which the firmware skipped wholesale — so
    // NO per-sensor config (incl. type) ever persisted (gap #4). Fixed
    // 2026-06-10 with the static-pressure port. (The status read-back still
    // decodes via io.proto AnalogSensor — a separate numbering mismatch that
    // is the remaining gap-#4 display work; it does NOT affect whether the
    // firmware resolves/acts on the saved type.)
    const sensorParts: Uint8Array[] = [];
    for (let s = 0; s < 4; s++) {
      const base = 5 + s * 5;
      // arr[base] = the sensor "type" selector — the AS2 ROLE enum
      // (PileTemp/PileHumidOptions values), NOT the firmware
      // ANALOG_SENSOR_TYPE_* hardware enum. They coincide only for
      // STATIC_PRESS (11). Map just that case so the firmware's
      // SetAnalogBoardTypes resolves the static-pressure sensor; every other
      // role → 0, which the firmware's resolve_sensor_type treats as "unset →
      // historical fixed-slot role" (preserves today's temp/humid/CO2
      // behavior). Full role→type mapping for the dual-bay sensors is the
      // remaining gap #4/#5 work.
      const uiRole = String(arr[base] ?? '');
      const fwType = uiRole === '11' ? 11 : 0;
      const label = String(arr[base + 1] ?? '');
      const offset = parseFloat(arr[base + 2] ?? '0') || 0;
      const disabled = parseInt(arr[base + 3] ?? '0', 10) || 0;
      const labelBytes = new TextEncoder().encode(label);

      const slot   = buildForceVarintBytes({ 1: s });
      const labelT = wrapAsLengthDelim(2, labelBytes);
      const off    = buildForceFloatBytes({ 3: offset });
      const dis    = buildForceVarintBytes({ 4: disabled });
      const typeB  = buildForceVarintBytes({ 5: fwType });

      const sLen = slot.length + labelT.length + off.length + dis.length + typeB.length;
      const sensor = new Uint8Array(sLen);
      let p = 0;
      sensor.set(slot, p);   p += slot.length;
      sensor.set(labelT, p); p += labelT.length;
      sensor.set(off, p);    p += off.length;
      sensor.set(dis, p);    p += dis.length;
      sensor.set(typeB, p);  p += typeB.length;
      sensorParts.push(wrapAsLengthDelim(4, sensor));   // board.sensors @ field 4
    }

    // board submsg: address@1 + each sensor@4
    const addr = buildForceVarintBytes({ 1: boardIdx });
    const bLen = addr.length + sensorParts.reduce((n, x) => n + x.length, 0);
    const boardInner = new Uint8Array(bLen);
    let bp = 0;
    boardInner.set(addr, bp); bp += addr.length;
    for (const sp of sensorParts) { boardInner.set(sp, bp); bp += sp.length; }

    // top-level: one board wrapped at field 1
    const buf = wrapAsLengthDelim(1, boardInner);
    await writeProtoRaw(TAG.AnalogBoard, buf);

    try {
      await fetch(getHttpUrl('/iot/sync/analog'), {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(arr),
      });
    } catch { /* sim simulator may not be running; not fatal */ }
  }

  function checkDirty(action: () => void) {
    // In the dashboard modal we skip the Skeleton confirm dialog (not wired
    // there) — autoSave + the 1.2 s debounce already persisted any edit.
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

  function refresh() {
    wait = true;
    reset = true;
    const rows = get(analogBoardArrays);
    if (Array.isArray(rows) && rows.length > 0) {
      allBoards = cloneDeep(rows);
      analog = allBoards[currentBoardIndex] ? cloneDeep(allBoards[currentBoardIndex]) : [];
      populateDefaultLabels();
      updateDisabled();
      syncCurrentBoard();
    }
    wait = false;
  }

  function back() {
    checkDirty(() => {
      if (!allBoards || allBoards.length === 0) return;
      let prevIndex = currentBoardIndex;
      let count = 0;
      do {
        prevIndex = (prevIndex - 1 + allBoards.length) % allBoards.length;
        count++;
        if (allBoards[prevIndex] && allBoards[prevIndex].length > 0) break;
      } while (count < allBoards.length);
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
      let nextIndex = currentBoardIndex;
      let count = 0;
      do {
        nextIndex = (nextIndex + 1) % allBoards.length;
        count++;
        if (allBoards[nextIndex] && allBoards[nextIndex].length > 0) break;
      } while (count < allBoards.length);
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

  function findBoards() {
    checkDirty(() => {
      wait = true;
      reset = true;
      const rows = get(analogBoardArrays);
      if (Array.isArray(rows) && rows.length > 0) {
        allBoards = cloneDeep(rows);
        currentBoardIndex = allBoards.findIndex(b => b && b.length > 0);
        if (currentBoardIndex < 0) currentBoardIndex = 0;
        analog = allBoards[currentBoardIndex] ? cloneDeep(allBoards[currentBoardIndex]) : [];
        populateDefaultLabels();
        updateDisabled();
        syncCurrentBoard();
      }
      wait = false;
    });
  }

  function handleSaveComplete(event: CustomEvent<{ success: boolean }>) {
    if (!event.detail.success) return;
    syncCurrentBoard();
    populateDefaultLabels();
    updateDisabled();
    reset = true;
  }

  function getAdornment(sensorType: string | undefined): AdornmentType {
    switch (sensorType) {
      case '1': case '6': case '7': case '10': return AdornmentType.Humidity;
      case '2': case '8': return AdornmentType.CO2;
      case '11': return AdornmentType.StaticPressure;
      case '0': case '3': case '4': case '5': case '9':
      default: return AdornmentType.Temperature;
    }
  }

  function getBoardType(boardType: string | undefined): string {
    switch (boardType) {
      case '0': return $t('level2.analog.temperature-ir');
      case '1': return $t('level2.analog.humidity');
      case '2': return 'CO2';
      case '3': return $t('level2.analog.temperature');
      default: return $t('level2.analog.unknown');
    }
  }

  function getSensorType(sensorType: string): string {
    switch (sensorType) {
      case '1': return $t('level2.analog.humidity');
      case '2': return 'CO2 #1';
      case '0': case '3': return $t('level2.analog.temperature');
      case '4': return `${$t('level2.analog.return-temperature')} #1`;
      case '5': return `${$t('level2.analog.return-temperature')} #2`;
      case '6': return `${$t('global.return-humidity')} #1`;
      case '7': return `${$t('global.return-humidity')} #2`;
      case '8': return 'CO2 #2';
      case '9': return $t('global.pile-temperature');
      case '10': return $t('global.pile-humidity');
      case '11': return $t('global.static-pressure');
      default: return $t('level2.analog.undefined');
    }
  }

  function getDefaultSensorLabel(boardNum: string, sensorIndex: number, sensorType: string): string {
    if (boardNum === '1') {
      switch (sensorIndex) {
        case 0: return $t('level2.analog.plenum-temp-1') || 'Plenum Temperature';
        case 1: return $t('level2.analog.plenum-temp-2') || 'Plenum 2 Temperature';
        case 2: return $t('level2.analog.outside-temp') || 'Outside Temperature';
        case 3: return $t('level2.analog.return-temp') || 'Return Temperature';
        default: return getSensorType(sensorType);
      }
    }
    if (boardNum === '2') {
      switch (sensorIndex) {
        case 0: return $t('level2.analog.outside-humidity') || 'Outside Humidity';
        case 1: return $t('level2.analog.plenum-humidity') || 'Plenum Humidity';
        default: return getSensorType(sensorType);
      }
    }
    return getSensorType(sensorType);
  }
</script>

<div class="pform pform--{theme}">
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
          <input class="checkbox w-8 h-8" type="checkbox" bind:checked={boardDisabled} on:change={updateSensorFromBoard} disabled={!edit}/>
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
                {#if parseInt(analog?.[0] ?? '0', 10) >= 3}
                  <button type="button"
                          class="text-size-xl underline text-primary-600 hover:text-primary-800 cursor-pointer bg-transparent border-0 p-0"
                          on:click={() => openCustomModal(i)}>
                    # {i+1}
                  </button>
                {:else}
                  <span class="text-size-xl"># {i+1}</span>
                {/if}
              </Column>
              <Column class="w-4/12 border-r border-gray-400 py-1 px-2 text-size-large">
                {#if analog[1] === BoardTypes.BOARD_TEMP}
                  {#if analog[0] === '1'}
                    {getSensorType(analog[i*5 + 5])}
                  {:else}
                    <Select size="lg" extended="w-full" class="mx-2" bind:value={analog[i*5 + 5]} options={PileTempOptions} {edit} on:change={() => analog = analog}/>
                  {/if}
                {:else}
                  {#if analog[0] === '2' && i < 2}
                    {getSensorType(analog[i*5 + 5])}
                  {:else}
                    <Select size="lg" extended="w-full" class="mx-2" bind:value={analog[i*5 + 5]} options={PileHumidOptions} {edit} on:change={() => analog = analog} />
                  {/if}
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
                <input class="checkbox w-8 h-8" type="checkbox" bind:checked={sensorDisabled[i]} disabled={!edit}/>
              </Column>
            </Row>
          {/each}
        {/key}
      {/if}
    </Table>
    <div class="flex flex-row justify-evenly items-center gap-2 w-full">
      <Button size="xl" class="mb-0" on:click={refresh}>{ $t('level2.analog.refresh-sensors') }</Button>
      <Button size="xl" class="mb-0" on:click={back}>{ $t('level2.analog.previous-board') }</Button>
      <SaveButton bind:this={saveBtn} {edit} bind:wait={wait} data={analog} bind:original={allBoards[currentBoardIndex]} bind:validation={validation} bind:reset={reset} on:complete={handleSaveComplete} autoSave onSave={saveAnalog} />
      <Button size="xl" class="mb-0" on:click={next}>{ $t('level2.analog.next-board') }</Button>
      <Button size="xl" class="mb-0" on:click={findBoards}>{ $t('level2.analog.find-boards') }</Button>
    </div>
  </Card>
</div>

{#if showCustomModal}
  <div class="fixed inset-0 z-[1000] bg-black/60 flex items-center justify-center"
       on:click={cancelCustomModal} role="presentation">
    <div class="bg-surface-100 rounded-lg shadow-2xl p-6 max-w-3xl w-full mx-4"
         on:click|stopPropagation on:keydown|stopPropagation
         role="dialog" aria-modal="true" tabindex="-1">
      <header class="text-2xl font-bold mb-1">
        {$t('level2.analog.define-custom-sensors')}
      </header>
      <p class="text-base text-surface-600 mb-4">
        {$t('global.board')} #{analog?.[0] ?? ''} — {analog?.[2] ?? ''}
      </p>
      <table class="w-full mb-4 text-base">
        <thead>
          <tr class="border-b border-surface-300">
            <th class="text-left py-2 px-2 w-1/12">#</th>
            <th class="text-left py-2 px-2 w-4/12">{$t('level2.analog.display-unit')}</th>
            <th class="text-left py-2 px-2 w-3/12">{$t('level2.analog.eng-min')}</th>
            <th class="text-left py-2 px-2 w-3/12">{$t('level2.analog.eng-max')}</th>
          </tr>
        </thead>
        <tbody>
          {#each modalRows as row, i}
            <tr class="border-b border-surface-200 {customFocusSlot === i ? 'bg-primary-100' : ''}">
              <td class="py-2 px-2 font-semibold">#{i + 1}</td>
              <td class="py-2 px-2">
                <Select size="lg" extended="w-full" bind:value={row.displayUnit}
                        options={unitOptions} edit={true} />
              </td>
              <td class="py-2 px-2">
                <TextField class="w-32" size="lg" bind:value={row.engMin} edit={true}
                           label={$t('level2.analog.eng-min')}
                           keyboardType={KeyboardTypes.Float} />
              </td>
              <td class="py-2 px-2">
                <TextField class="w-32" size="lg" bind:value={row.engMax} edit={true}
                           label={$t('level2.analog.eng-max')}
                           keyboardType={KeyboardTypes.Float} />
              </td>
            </tr>
          {/each}
        </tbody>
      </table>
      <footer class="flex justify-end gap-2">
        <Button size="lg" on:click={cancelCustomModal}>{$t('global.cancel')}</Button>
        <Button size="lg" on:click={applyCustomModal}>{$t('global.save')}</Button>
      </footer>
    </div>
  </div>
{/if}
