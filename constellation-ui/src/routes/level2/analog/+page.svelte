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
    /* Hydration is push-driven via `analogBoardArrays` (derived from the
     * AnalogBoard proto stream). No SSR loader — the page stays in `wait`
     * until the first frame lands. */

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

  /* Custom 4-20 mA scaling per slot for the CURRENT board.
   * Source of truth: AnalogBoard proto stream → analogBoards aggregate
   * store. We snapshot it whenever the user opens the dialog so concurrent
   * firmware updates don't blow away in-flight edits. Persisted via the
   * normal saveAnalog() path (proto fields 7/8/9 of AnalogSensor). */
  type CustomCfg = { displayUnit: number; engMin: number; engMax: number };
  const EMPTY_CFG: CustomCfg = { displayUnit: 0, engMin: 0, engMax: 0 };
  let customConfigs: CustomCfg[] = [ {...EMPTY_CFG}, {...EMPTY_CFG}, {...EMPTY_CFG}, {...EMPTY_CFG} ];

  /* Editable rows for the modal — string-typed for TextField bindings. */
  let showCustomModal = false;
  let modalRows: { displayUnit: string; engMin: string; engMax: string }[] = [];
  /* Optional slot (0..3) clicked via the sensor # link — used to highlight
   * the corresponding modal row. -1 means "no specific focus". */
  let customFocusSlot = -1;

  // Keep enum in lockstep with proto/agristar/io.proto AnalogSensor.display_unit.
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
    // Save directly — these fields aren't part of the 25-element `analog`
    // array that SaveButton diffs, so the dirty-detection mechanism would
    // never fire if we relied on it.
    void saveAnalog(analog);
  }

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

    const boardNum = analog[0];

    /* Board #2 (DEFAULT_HUMID_BOARD) — slots 0 & 1 are physically wired
     * to the Outside / Plenum humidity probes per the AS2 contract.
     * Force their Type to "Humidity" (1) so the value is decoded correctly
     * even if the firmware reported something different (e.g. fresh OSPI).
     * Also force the labels — these slots are non-editable in the UI so
     * stale label strings (e.g. "Plenum Temperature" left over from a
     * previous board layout) would otherwise stick around forever. */
    if (boardNum === '2') {
      analog[5]  = '1'; /* slot 0 type: Humidity */
      analog[10] = '1'; /* slot 1 type: Humidity */
      analog[6]  = $t('level2.analog.outside-humidity') || 'Outside Humidity';
      analog[11] = $t('level2.analog.plenum-humidity')  || 'Plenum Humidity';

      /* Slots 2 & 3 are user-selectable from PileHumidOptions, but a fresh
       * board (or one previously mis-typed as a temperature board) needs
       * sensible defaults: slot 2 = Return Humidity #1 (type 6), slot 3 =
       * CO2 #1 (type 2). Also scrub labels that match known stale strings
       * from the temperature-board layout so the default-label logic below
       * gets a chance to refill them. */
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
      // Only set default if label is empty or whitespace
      if (!label || label.trim() === '') {
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
    if (boardNum === '2') {
      return $t('level2.analog.default-humidity') || 'Default Humidity';
    }
    return getBoardType(boardType);
  }

  /* Push-driven hydration: subscribe to the typed proto store. The
   * `analogBoardArrays` derived store re-emits the legacy 25-element row
   * shape every time any AnalogBoard frame arrives (firmware cadence
   * ≈5 s). The dirty-gate prevents a mid-edit broadcast from clobbering
   * unsaved user input — same pattern as the auxiliary page migration. */
  function isDirty(): boolean {
    return allBoards.length > 0
        && currentBoardIndex >= 0
        && currentBoardIndex < allBoards.length
        && !isEqual(analog, allBoards[currentBoardIndex]);
  }

  function hydrateFromStore(rows: string[][]) {
    if (!Array.isArray(rows) || rows.length === 0) return;
    if (isDirty()) return;
    // Preserve the currently-viewed board across re-broadcasts by matching
    // on address (rows[i][0]) rather than positional index, since the
    // store may not include unpopulated slots.
    const prevAddr = analog?.[0];
    allBoards = cloneDeep(rows);
    let idx = -1;
    if (prevAddr !== undefined) {
      idx = allBoards.findIndex(b => b?.[0] === prevAddr);
    }
    if (idx < 0) idx = 0;
    currentBoardIndex = idx;
    analog = allBoards[currentBoardIndex] ? cloneDeep(allBoards[currentBoardIndex]) : [];
    populateDefaultLabels();
    updateDisabled();
    syncCurrentBoard();
    ready = true;
  }

  onMount(() => {
    $navigationStore.isDirty = isDirty;
    const unsub = analogBoardArrays.subscribe(hydrateFromStore);
    /* Even if no frame has arrived yet, mark ready after a short window so
     * GellertPage stops blanking — empty allBoards just shows no rows. */
    const t = setTimeout(() => { ready = true; }, 1500);
    return () => { unsub(); clearTimeout(t); };
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
    refreshCustomFromStore();
  }

  /**
   * Phase 5.1 proto-direct save (settings field 42 / TAG.AnalogBoard).
   *
   * Firmware apply_analog_board (nova_dataexc.c) takes:
   *   field 1 = boardIdx (uint32)
   *   field 6 = repeated sensor submsg { 1=slot, 3=label, 4=offset, 5=disabled }
   * Force-encode boardIdx, slot, offset, and disabled — every one has a
   * legitimate zero value (board 0, slot 0, offset 0.0, disabled=false).
   *
   * The 25-element page array layout (from /iot/analog):
   *   [0]=addr, [2]=label, [4]=disabled,
   *   per-sensor i=0..3 at base = 5 + i*5:
   *     base+0=type, base+1=label, base+2=offset, base+3=disabled, base+4=value
   * `type` and `value` are RS485 sim-cache state, not in the firmware
   * AnalogBoard schema — pushed via /iot/sync/analog instead.
   */
  async function saveAnalog(arr: string[]): Promise<void> {
    if (!Array.isArray(arr) || arr.length < 25) return;
    const boardIdx = parseInt(arr[0] ?? '0', 10) || 0;
    const sensorParts: Uint8Array[] = [];
    for (let s = 0; s < 4; s++) {
      const base = 5 + s * 5;
      const label = String(arr[base + 1] ?? '');
      const offset = parseFloat(arr[base + 2] ?? '0') || 0;
      const disabled = parseInt(arr[base + 3] ?? '0', 10) || 0;
      const cfg = customConfigs[s] ?? EMPTY_CFG;
      const labelBytes = new TextEncoder().encode(label);
      const slot = buildForceVarintBytes({ 1: s });
      const labelTLV = wrapAsLengthDelim(3, labelBytes);
      const off = buildForceFloatBytes({ 4: offset });
      const dis = buildForceVarintBytes({ 5: disabled });
      /* Custom 4-20 mA scaling: only emit when set, so default sensors
       * stay zero-suppressed on the wire (matches firmware decoder
       * behaviour — missing field == 0). */
      const dispU = cfg.displayUnit !== 0 ? buildForceVarintBytes({ 7: cfg.displayUnit }) : new Uint8Array(0);
      const eMin = (cfg.engMin !== 0 || cfg.engMax !== 0) ? buildForceFloatBytes({ 8: cfg.engMin }) : new Uint8Array(0);
      const eMax = (cfg.engMin !== 0 || cfg.engMax !== 0) ? buildForceFloatBytes({ 9: cfg.engMax }) : new Uint8Array(0);
      const total = slot.length + labelTLV.length + off.length + dis.length
                  + dispU.length + eMin.length + eMax.length;
      const sensor = new Uint8Array(total);
      let p = 0;
      sensor.set(slot, p); p += slot.length;
      sensor.set(labelTLV, p); p += labelTLV.length;
      sensor.set(off, p); p += off.length;
      sensor.set(dis, p); p += dis.length;
      if (dispU.length) { sensor.set(dispU, p); p += dispU.length; }
      if (eMin.length)  { sensor.set(eMin,  p); p += eMin.length; }
      if (eMax.length)  { sensor.set(eMax,  p); p += eMax.length; }
      sensorParts.push(wrapAsLengthDelim(6, sensor));
    }
    const board = buildForceVarintBytes({ 1: boardIdx });
    const totalLen = board.length + sensorParts.reduce((n, p) => n + p.length, 0);
    const buf = new Uint8Array(totalLen);
    let off2 = 0;
    buf.set(board, off2); off2 += board.length;
    for (const sp of sensorParts) { buf.set(sp, off2); off2 += sp.length; }
    await writeProtoRaw(TAG.AnalogBoard, buf);

    // Sim-only side-effect: push the 25-element body to the RS485 cache so
    // /iot/analog GET reflects the change. Production firmware owns the
    // RS485 link directly so this is a no-op there.
    try {
      await fetch(getHttpUrl('/iot/sync/analog'), {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(arr),
      });
    } catch { /* sim simulator may not be running; not fatal */ }
  }

  function checkDirty(action: () => void) {
    // Skip the "continue without saving?" prompt unless the user is
    // actively typing (keyboard visible). With SaveButton autoSave, any
    // pending change has already been persisted or will be within the
    // 1.2 s debounce window after the keyboard closes.
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

  /* `Refresh sensors` re-applies the latest store snapshot. The proto
   * stream pushes new frames automatically every ~5 s, so this is now
   * just a manual rehydration trigger that bypasses the dirty-gate. */
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

  /* `Find boards` rescans from the latest store snapshot. Since the
   * firmware re-broadcasts AnalogBoard frames continuously the store is
   * always current; this just resets the view to the first board. The
   * RS485 discovery is firmware-side and not user-triggerable from here. */
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
    // Board 1 = Default Temperature Board (mirrors firmware DEFAULT_TEMP_BOARD)
    if (boardNum === '1') {
      switch (sensorIndex) {
        case 0: return $t('level2.analog.plenum-temp-1') || 'Plenum Temperature';
        case 1: return $t('level2.analog.plenum-temp-2') || 'Plenum 2 Temperature';
        case 2: return $t('level2.analog.outside-temp') || 'Outside Temperature';
        case 3: return $t('level2.analog.return-temp') || 'Return Temperature';
        default: return getSensorType(sensorType);
      }
    }
    // Board 2 = Default Humidity Board (mirrors firmware DEFAULT_HUMID_BOARD).
    // Slots 0 & 1 are fixed Outside/Plenum humidity per AS2 contract;
    // 2 & 3 are user-selectable from the dropdown so fall through to type.
    if (boardNum === '2') {
      switch (sensorIndex) {
        case 0: return $t('level2.analog.outside-humidity') || 'Outside Humidity';
        case 1: return $t('level2.analog.plenum-humidity') || 'Plenum Humidity';
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
                {#if parseInt(analog?.[0] ?? '0', 10) >= 3}
                  <!-- Sensor # is a link to the Custom 4-20 mA modal — only
                       meaningful for user-addable boards (#3+); default
                       temp/humid boards (#1/#2) have fixed sensor types. -->
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
                  <!-- Board #2 (DEFAULT_HUMID_BOARD): slots 0 & 1 are fixed
                       Outside/Plenum humidity probes; only slots 2 & 3 are
                       user-selectable from the pile humidity / CO2 dropdown. -->
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
                <input class="checkbox w-8 h-8" type="checkbox" bind:checked={sensorDisabled[i]} />
              </Column>
            </Row>
          {/each}
        {/key}
      {/if}
    </Table>
    <!-- Bottom action row: evenly spaced across the page width.
         Custom 4-20 mA setup is now reached via the clickable sensor #
         link in the sensor table (only active on user-addable boards
         #3+), so it no longer competes with the navigation buttons
         for horizontal space. -->
    <div class="flex flex-row justify-evenly items-center gap-2 w-full">
      <Button size="xl" class="mb-0" on:click={refresh}>{ $t('level2.analog.refresh-sensors') }</Button>
      <Button size="xl" class="mb-0" on:click={back}>{ $t('level2.analog.previous-board') }</Button>
      <SaveButton {edit} bind:wait={wait} data={analog} bind:original={allBoards[currentBoardIndex]} bind:validation={validation} bind:reset={reset} on:complete={handleSaveComplete} autoSave onSave={saveAnalog} />
      <Button size="xl" class="mb-0" on:click={next}>{ $t('level2.analog.next-board') }</Button>
      <Button size="xl" class="mb-0" on:click={findBoards}>{ $t('level2.analog.find-boards') }</Button>
    </div>
  </Card>
</GellertPage>

{#if showCustomModal}
  <!-- Inline custom 4-20 mA modal. Skeleton's <Modal/> registry isn't wired
       in this app, so we render a plain overlay; same UX as a normal dialog. -->
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



