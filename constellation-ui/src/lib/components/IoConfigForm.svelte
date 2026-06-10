<script lang="ts">
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
  import { ioConfigComposite } from "$lib/business/protoStores";
  import { writeProtoRaw, writeProto, buildForceVarintBytes, wrapAsLengthDelim } from "$lib/business/protoWrite";
  import { TAG } from "$lib/business/protoTags";
  import { resolveEquipmentLabel, entryAppliesToSystem, IoOptionEnum } from "$lib/business/equipmentMeta";

  // Shared body of the System I/O Configuration page (level2/ioconfig): orbit
  // role assignment + per-board output/input pin mapping (OutputCell/InputCell)
  // with aggregated validation + Set-To-Default. Rendered from ONE source of
  // truth — the classic page AND a FULL-BLEED dashboard modal (⚙ Setup → System
  // → IO Config). Page-chrome (ScrollableArea + footer slots) dropped; Save +
  // Set-To-Default relocated inline. Prop contract mirrors FanSpeedForm (L2).
  // Architecture: docs/ioconfig-architecture.md. docs/spatial-ui-page-migration.md
  export let wait = false;
  export let ready = false;
  export let embedded = false;
  export let theme: 'light' | 'dark' = 'light';
  export let canEdit: boolean | null = null;

  $: edit = canEdit ?? ($navigationStore.level > 0);

  let data: IOConfigType = {
    ioAvailable: [],
    config: { outputConfig: [], inputConfig: [] },
    ioNames: [],
    systemMode: '0',
  };

  async function saveIoConfig(cfg: { outputConfig: string[], inputConfig: string[] }): Promise<void> {
    const parts: Uint8Array[] = [];
    const emit = (port: number, eqRaw: string, outerField: 3 | 4) => {
      if (eqRaw === undefined || eqRaw === '' || eqRaw === '-1') return;
      const eqIdx = parseInt(eqRaw, 10);
      if (!Number.isFinite(eqIdx) || eqIdx < 0) return;
      const inner = buildForceVarintBytes({ 1: eqIdx, 2: port });
      parts.push(wrapAsLengthDelim(outerField, inner));
    };
    (cfg.outputConfig ?? []).forEach((v, port) => emit(port, v, 3));
    (cfg.inputConfig  ?? []).forEach((v, port) => emit(port, v, 4));
    const total = parts.reduce((n, p) => n + p.length, 0);
    const buf = new Uint8Array(total);
    let off = 0;
    for (const p of parts) { buf.set(p, off); off += p.length; }
    await writeProtoRaw(TAG.IoConfig, buf);

    data = {
      ...data,
      config: {
        outputConfig: [...cfg.outputConfig],
        inputConfig:  [...cfg.inputConfig],
      },
    };

    pendingEcho = { out: [...cfg.outputConfig], in_: [...cfg.inputConfig] };
    if (pendingEchoTimer) clearTimeout(pendingEchoTimer);
    pendingEchoTimer = setTimeout(() => { clearPendingEcho(); }, 1000);

    try {
      await fetch(getHttpUrl('/iot/sync/ioconfig'), {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(cfg),
      });
    } catch { /* orbit may not be running; not fatal */ }
  }

  $: ioConfig = {
      ioAvailable: [],
      config: { outputConfig: [], inputConfig: [] },
      ioNames: [],
      systemMode: '0',
  } as IOConfigType;

  $: outputList = [{ text: $t('global.none'), value: '-1'}];
  $: inputList = [{ text: $t('global.none'), value: '-1'}];
  const lights: {text: string, value: string}[] = [];

  const board = [$t('level2.ioconfig.main'), `${$t('global.board')} 1`, `${$t('global.board')} 2`];

  const gdcOutputLabels = ['Open 1', 'Close 1', 'Open 2', 'Close 2', 'Open 3', 'Close 3', 'Open 4', 'Close 4', 'Open 5', 'Close 5'];
  const gdcInputLabels = ['Open SW 1', 'Close SW 1', 'Open SW 2', 'Close SW 2', 'Open SW 3', 'Close SW 3', 'Open SW 4', 'Close SW 4', 'Open SW 5', 'Close SW 5'];

  function getBoardName(boardIndex: number): string {
    const info = ioInfo[boardIndex];
    if (!info || !info[0]) return board[boardIndex] ?? `Board ${boardIndex}`;
    const label = info[0];
    if (label.startsWith('Orbit') || label.startsWith('GDC')) return label;
    return board[boardIndex] ?? label;
  }

  function isGDCBoard(boardIndex: number): boolean {
    const info = ioInfo[boardIndex];
    return info && info[3] === '2';
  }

  function getRowLabel(boardIndex: number, pinIndex: number): string {
    if (isGDCBoard(boardIndex)) return pinIndex <= 5 ? `Door ${pinIndex}` : `Door ${pinIndex - 5}`;
    return String(pinIndex);
  }

  function getOutputHeader(boardIndex: number, pinIndex: number): string {
    if (isGDCBoard(boardIndex) && pinIndex > 0 && pinIndex <= 10) return gdcOutputLabels[pinIndex - 1] ?? `DO${pinIndex}`;
    return $t('global.output');
  }

  function getInputHeader(boardIndex: number, pinIndex: number): string {
    if (isGDCBoard(boardIndex) && pinIndex > 0 && pinIndex <= 10) return gdcInputLabels[pinIndex - 1] ?? `DI${pinIndex}`;
    return $t('global.input');
  }

  let ioInfo: Array<Array<string>> = [];
  let validation: Record<string, string> = {};

  function buildEquipmentList() {
    outputList.splice(0, outputList.length);
    inputList.splice(0, inputList.length);
    lights.splice(0, lights.length);
    outputList.push({ text: $t('global.none'), value: '-1'});
    inputList.push({ text: $t('global.none'), value: '-1'});
    const sys = parseInt(ioConfig.systemMode, 10);
    for (let i = 0; i < ioConfig.ioNames.length - 1; i += 1) {
      const entry = ioConfig.ioNames[i];
      if (entry.ioType === IoOptionEnum.SWITCH) continue;
      if (!entryAppliesToSystem(entry.mode, sys)) continue;
      const idStr = String(entry.index);
      if (entry.index === 40) continue;
      const label = resolveEquipmentLabel(entry, $t);
      if (entry.index === 23 || entry.index === 24) { lights.push({ text: label, value: idStr }); continue; }
      if (entry.ioType === IoOptionEnum.OUTPUT || entry.ioType === IoOptionEnum.BOTH) outputList.push({ text: label, value: idStr });
      if (entry.ioType === IoOptionEnum.INPUT || entry.ioType === IoOptionEnum.BOTH) inputList.push({ text: label, value: idStr });
    }
    const lightOrder: Record<string, number> = { '38': 1, '39': 2, '0': 3 };
    outputList.sort((a, b) => (lightOrder[a.value] ?? 99) - (lightOrder[b.value] ?? 99));
  }

  function setupIoConfig(d: IOConfigType) {
    ioConfig = d;
    if (ioConfig.ioAvailable.length > 0 && ioConfig.ioAvailable[ioConfig.ioAvailable.length - 1] === '') {
      ioConfig.ioAvailable.pop();
    }
    if (ioConfig.ioAvailable.length === 0 && orbits.length > 0) {
      const storage = orbits.filter((o) => o.connected && o.role === 1).sort((a, z) => a.slot - z.slot);
      ioConfig.ioAvailable = storage.map((_, i) => `Orbit ${i + 1}:10:11:1`);
    }
    ioConfig.ioAvailable.map((io, i) => { ioInfo[i] = io.split(':'); });
    buildEquipmentList();
  }

  function clearStaleAssignments(arr: string[], eqRaw: string, keepPid: number): string[] {
    if (eqRaw === '-1' || eqRaw === '' || eqRaw === undefined) return arr;
    const eqIdx = parseInt(eqRaw, 10);
    if (!Number.isFinite(eqIdx) || eqIdx < 0) return arr;
    if (eqIdx === 23 || eqIdx === 24) return arr;
    let mutated = false;
    const out = arr.slice();
    for (let p = 0; p < out.length; p++) {
      if (p === keepPid) continue;
      if (out[p] === eqRaw) { out[p] = '-1'; mutated = true; }
    }
    return mutated ? out : arr;
  }

  function outputChanged(event: CustomEvent<{ i: number, j: number, value: string }>) {
    const i = event.detail.i;
    const j = event.detail.j;
    const value = parseInt(event.detail.value, 10);
    const pid = (12 * i + j).toString();
    const pidNum = parseInt(pid, 10);
    ioConfig.config.outputConfig[pidNum] = event.detail.value;
    ioConfig.config.outputConfig = clearStaleAssignments(ioConfig.config.outputConfig, event.detail.value, pidNum);
    try {
      const numInputs = parseInt(ioInfo[i]?.[2] || '0', 10);
      const allowedInput = (j <= numInputs) && ((i !== 0) || (j !== 1));
      const equipOptions = [...inputList, ...lights];
      if (allowedInput && equipOptions.some(opt => opt.value === event.detail.value)) {
        ioConfig.config.inputConfig[pidNum] = event.detail.value;
        ioConfig.config.inputConfig = clearStaleAssignments(ioConfig.config.inputConfig, event.detail.value, pidNum);
      }
    } catch {}
    if (value === 7 || value === 9 || value === 11) {
      const nextPid = (pidNum + 1).toString();
      const nextPidNum = pidNum + 1;
      const nextValue = (value + 1).toString();
      ioConfig.config.outputConfig[nextPidNum] = nextValue;
      ioConfig.config.outputConfig = clearStaleAssignments(ioConfig.config.outputConfig, nextValue, nextPidNum);
      try {
        const nextBoard = Math.floor((parseInt(nextPid, 10)) / 12);
        const nextJ = (parseInt(nextPid, 10)) % 12 || 12;
        const numInputs2 = parseInt(ioInfo[nextBoard]?.[2] || '0', 10);
        const allowedInput2 = (nextJ <= numInputs2) && ((nextBoard !== 0) || (nextJ !== 1));
        const equipOptions2 = [...inputList, ...lights];
        if (allowedInput2 && equipOptions2.some(opt => opt.value === nextValue)) {
          ioConfig.config.inputConfig[nextPidNum] = nextValue;
          ioConfig.config.inputConfig = clearStaleAssignments(ioConfig.config.inputConfig, nextValue, nextPidNum);
        }
      } catch {}
    }
    ioConfig.config.outputConfig = [...ioConfig.config.outputConfig];
    ioConfig.config.inputConfig = [...ioConfig.config.inputConfig];
    ioConfig = { ...ioConfig };
  }

  function inputChanged(event: CustomEvent<{ i: number, j: number, value: string }>) {
    const i = event.detail.i;
    const j = event.detail.j;
    const pid = (12 * i + j).toString();
    const pidNum = parseInt(pid, 10);
    ioConfig.config.inputConfig[pidNum] = event.detail.value;
    ioConfig.config.inputConfig = clearStaleAssignments(ioConfig.config.inputConfig, event.detail.value, pidNum);
    ioConfig.config.inputConfig = [...ioConfig.config.inputConfig];
    ioConfig = { ...ioConfig };
  }

  async function outputRenamed(event: CustomEvent<{ i: number, j: number, name: string, type: number }>) {
    const pid = (event.detail.i * 12) + event.detail.j;
    const idx = parseInt(event.detail.type === 0 ? ioConfig.config.outputConfig[pid] : ioConfig.config.inputConfig[pid], 10);
    if (idx !== -1) {
      wait = true;
      try {
        await writeProto(TAG.IoNameUpdate, { index: idx, newName: event.detail.name });
      } catch (e) {
        console.error('IoNameUpdate failed', e);
      }
      const parts = ioConfig.ioNames[idx];
      parts.name = event.detail.name;
      ioConfig.ioNames[idx] = parts;
      buildEquipmentList();
      outputList = [...outputList];
      inputList = [...inputList];
      ioConfig = {...ioConfig};
      wait = false;
    }
  }

  function localIsDirty(): boolean {
    return !isEqual(ioConfig.config.outputConfig, data.config.outputConfig) ||
           !isEqual(ioConfig.config.inputConfig,  data.config.inputConfig);
  }

  onMount(() => {
    if (!embedded) {
      $navigationStore.data = '';
      $navigationStore.isDirty = localIsDirty;
    }
    void loadOrbits();
    const unsub = ioConfigComposite.subscribe((view) => {
      if (!view) return;
      if (pendingEcho && !pendingDefaultReset) {
        const matches =
          isEqual(view.config.outputConfig, pendingEcho.out) &&
          isEqual(view.config.inputConfig,  pendingEcho.in_);
        if (!matches) { ready = true; return; }
        clearPendingEcho();
      }
      if (pendingDefaultReset || !localIsDirty()) {
        const sameConfig =
          isEqual(view.config.outputConfig, ioConfig.config.outputConfig) &&
          isEqual(view.config.inputConfig,  ioConfig.config.inputConfig);
        data = view;
        if (!sameConfig || pendingDefaultReset) setup(view);
        if (pendingDefaultReset) {
          pendingDefaultReset = false;
          if (defaultTimer) { clearTimeout(defaultTimer); defaultTimer = undefined; }
          wait = false;
        }
        if (!embedded) $navigationStore.isDirty = localIsDirty;
      }
      ready = true;
    });
    return () => { unsub(); clearPendingEcho(); };
  });

  // Modal close hook — flush a dirty config (aggregated validation guards it).
  let saveBtn: { save: () => Promise<void> } | undefined;
  export async function flush(): Promise<void> {
    if (localIsDirty() && saveBtn) await saveBtn.save();
  }

  function setup(d: IOConfigType) {
    if (d) setupIoConfig(cloneDeep(d));
  }

  type OrbitRow = { slot: number; dipswitchId: number; connected: boolean; role: number };
  let orbits: OrbitRow[] = [];
  let orbitsBusy = false;

  const ROLE_OPTIONS = [
    { value: 0, label: 'Unassigned' },
    { value: 1, label: 'Storage' },
    { value: 2, label: 'Door (GDC)' },
    { value: 3, label: 'Refrigeration (Triton)' },
  ];

  function roleLabel(r: number): string {
    return ROLE_OPTIONS.find(o => o.value === r)?.label ?? 'Unknown';
  }

  async function loadOrbits() {
    try {
      const resp = await fetch(getHttpUrl('/iot/orbits'));
      if (!resp.ok) return;
      const j = await resp.json();
      orbits = (j.orbits ?? []).filter((o: OrbitRow) => o.connected);
      if (ioConfig && (ioConfig.ioAvailable?.length ?? 0) === 0 && orbits.length > 0) {
        setupIoConfig(ioConfig);
        ioConfig = { ...ioConfig };
      }
    } catch (e) {
      console.error('loadOrbits failed:', e);
    }
  }

  async function setOrbitRole(slot: number, role: number) {
    orbitsBusy = true;
    try {
      const resp = await fetch(getHttpUrl('/iot/orbits/role'), {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ slot, role }),
      });
      if (!resp.ok) console.error('setOrbitRole failed:', await resp.text());
      await loadOrbits();
      $navigationStore = { ...$navigationStore, invalidate: true };
    } catch (e) {
      console.error('setOrbitRole error:', e);
    } finally {
      orbitsBusy = false;
    }
  }

  function onOrbitRoleChange(slot: number, ev: Event) {
    const target = ev.currentTarget as HTMLSelectElement;
    setOrbitRole(slot, parseInt(target.value, 10));
  }

  let defaultTimer: ReturnType<typeof setTimeout> | undefined;
  let pendingDefaultReset = false;
  let pendingEcho: { out: string[]; in_: string[] } | null = null;
  let pendingEchoTimer: ReturnType<typeof setTimeout> | undefined;

  function clearPendingEcho() {
    pendingEcho = null;
    if (pendingEchoTimer) { clearTimeout(pendingEchoTimer); pendingEchoTimer = undefined; }
  }

  async function setToDefault() {
    if (defaultTimer) { clearTimeout(defaultTimer); defaultTimer = undefined; }
    wait = true;
    pendingDefaultReset = true;
    try {
      await fetch(getHttpUrl('/iot/button'), {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ tag: 'button2', ResetIoConfig: '1' }),
      });
      defaultTimer = setTimeout(() => {
        pendingDefaultReset = false;
        wait = false;
        defaultTimer = undefined;
      }, 10000);
    } catch (e) {
      console.error('Set To Default request failed:', e);
      pendingDefaultReset = false;
      wait = false;
    }
  }

  onDestroy(() => {
    if (defaultTimer) clearTimeout(defaultTimer);
  });
</script>

<div class="pform pform--{theme}">
  <Card class="w-full text-size-xl flex flex-col">
    {#if orbits.length > 0}
      <Table class="mb-4">
        <Row class="text-size-large">
          <Column class="w-3/12 my-2 border-r border-gray-400 font-bold">Installed Orbits</Column>
          <Column class="w-3/12 my-2 border-r border-gray-400 font-bold">Slot / IP</Column>
          <Column class="w-3/12 my-2 border-r border-gray-400 font-bold">Current Role</Column>
          <Column class="w-3/12 my-2 font-bold">Assign Role</Column>
        </Row>
        {#each orbits as orbit (orbit.slot)}
          <Row>
            <Column class="w-3/12 border-r border-gray-400 font-semibold">Orbit {orbit.slot + 1}</Column>
            <Column class="w-3/12 border-r border-gray-400">slot {orbit.slot} · DIP {orbit.dipswitchId}</Column>
            <Column class="w-3/12 border-r border-gray-400">{roleLabel(orbit.role)}</Column>
            <Column class="w-3/12">
              <select class="w-full text-size-large border border-gray-400 rounded p-1 bg-white"
                disabled={orbitsBusy || !edit} value={orbit.role}
                on:change={(e) => onOrbitRoleChange(orbit.slot, e)}>
                {#each ROLE_OPTIONS as opt}
                  <option value={opt.value}>{opt.label}</option>
                {/each}
              </select>
            </Column>
          </Row>
        {/each}
      </Table>
    {/if}

    {#each ioConfig.ioAvailable as io, i}
      {#if ioInfo[i][0].indexOf('none') === -1}
        <Table class="mb-2">
          <Row class="text-size-large">
            <Column class="w-2/12 my-2 border-r border-gray-400 font-bold">{getBoardName(i)}</Column>
            <Column class="w-5/12 my-2 border-r border-gray-400 font-bold">{ $t('global.output') }</Column>
            <Column class="w-5/12 my-2 font-bold">{ $t('global.input') }</Column>
          </Row>
          {#if isGDCBoard(i)}
            <Row class="bg-gray-50">
              <Column class="w-2/12 border-r border-gray-400 text-size-small text-gray-500 italic" />
              <Column class="w-5/12 border-r border-gray-400 text-size-small text-gray-500 italic px-2">Paired: Open / Close per actuator</Column>
              <Column class="w-5/12 text-size-small text-gray-500 italic px-2">Paired: Open Limit / Close Limit per actuator</Column>
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
            {#each Array.from({ length: Math.max(parseInt(ioInfo[i][1]) || 0, parseInt(ioInfo[i][2]) || 0) }, (_, index) => index + 1) as j}
              <Row>
                <Column class="w-2/12 border-r border-gray-400 text-size-large">{j}</Column>
                {#if j <= (parseInt(ioInfo[i][1]) || 0)}
                  <OutputCell {i} {j} {ioConfig} {outputList} {lights}
                    value={(ioConfig.config.outputConfig[(12*i)+j] ?? '-1')}
                    on:change={outputChanged} on:rename={outputRenamed}
                    validation={validation[`o${(12*i) + j}`]}/>
                {:else}
                  <Column class="w-5/12 border-r border-gray-400 text-size-small text-gray-400 italic px-2">—</Column>
                {/if}
                <InputCell {i} {j} ioInfo={ioInfo[i]} {ioConfig} {inputList} {lights}
                  value={(ioConfig.config.inputConfig[(12*i)+j] ?? '-1')}
                  on:change={inputChanged} on:rename={outputRenamed}
                  validation={validation[`i${(12*i) + j}`]}/>
              </Row>
            {/each}
          {/if}
        </Table>
      {/if}
    {/each}

    <div class="flex flex-row items-center gap-3 mt-3">
      <SaveButton bind:this={saveBtn} {edit} bind:wait={wait} data={ioConfig.config}
        route='ioconfig' bind:original={data.config} bind:validation={validation} onSave={saveIoConfig}/>
      <Button size="xl" class="ml-auto !mb-0" on:click={setToDefault} disabled={!edit}>{ $t('level2.ioconfig.set-to-default') }</Button>
    </div>
  </Card>
</div>
