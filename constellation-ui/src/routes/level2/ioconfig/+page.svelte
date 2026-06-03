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
  import { ioConfigComposite } from "$lib/business/protoStores";
  import { writeProtoRaw, writeProto, buildForceVarintBytes, wrapAsLengthDelim } from "$lib/business/protoWrite";
  import { TAG } from "$lib/business/protoTags";
  import { resolveEquipmentLabel, entryAppliesToSystem, IoOptionEnum } from "$lib/business/equipmentMeta";

  // Phase 5.1 proto-direct hydration: data is sourced from `ioConfigComposite`
  // (a derived store over IoConfig + IoDefinition + BasicSetup + OrbitStatus)
  // instead of the legacy +page.ts /iot/ioconfig fetch. The bridge GET stays
  // alive only for the rs485Panel sim test fixture; the SvelteKit page no
  // longer touches it. The composite emits null until all four upstream
  // tags have arrived from firmware, which gates `ready`.
  let data: IOConfigType = {
    ioAvailable: [],
    config: { outputConfig: [], inputConfig: [] },
    ioNames: [],
    systemMode: '0',
  };

  /**
   * Phase 5.1 proto-direct save (SettingsUpdate field 39 / TAG.IoConfig).
   *
   * **Architecture reference:** docs/ioconfig-architecture.md (read first
   * if you're touching this — wire layout, hardware-pinned slots, and
   * the EQ index table all live there).
   *
   * Firmware LpSettings_ApplyIoConfig (lp_settings.c) accepts an INDEXED form:
   *   field 3 = repeated submsg { slot(1)=eqIdx, value(2)=ioPort } for OUTPUTS
   *   field 4 = repeated submsg { slot(1)=eqIdx, value(2)=ioPort } for INPUTS
   * Whenever any indexed entry is seen, the firmware first clears the
   * EquipIo[] table (Output=Input=IO_UNDEFINED), so we only need to send
   * the slots that are actually assigned. Both inner fields MUST be
   * force-varint: slot=0 (EQ_FAN) and value=0 (port DO0) are valid.
   *
   * The page outputConfig/inputConfig arrays are keyed by ioPort (pid)
   * with values = eqIdx (or '-1' = unassigned). We invert: for each
   * port with a real eqIdx, emit submsg{slot=eqIdx, value=port}.
   *
   * After the firmware write, we POST the same body to /iot/sync/ioconfig
   * so the bridge can push human-readable equipment names to each orbit
   * simulator's /api/ioconfig endpoint.
   */
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

    // Re-baseline `data` to match what we just sent so isDirty() goes back
    // to false. Without this, the firmware-echo broadcast is silently
    // dropped by the dirty-state guard in the composite subscriber and the
    // page's view of `data` lags forever — the symptom is that subsequent
    // edits appear to save fine on the wire (firmware OSPI mirror confirms)
    // but earlier saves "revert" because the next dropdown change runs
    // against a stale baseline.
    data = {
      ...data,
      config: {
        outputConfig: [...cfg.outputConfig],
        inputConfig:  [...cfg.inputConfig],
      },
    };

    // Arm pending-echo guard. Drop any non-matching broadcasts for up to
    // 1 s; matching one clears it immediately. Firmware round-trip is
    // typically <500 ms; if we still don't see a match after 1 s the
    // firmware has likely rejected an entry (e.g. trying to assign a
    // hardware-pinned slot like PULSEDOOR_OPEN to anything other than
    // DO11), and we want the operator to see the rejection promptly so
    // they can pick a valid slot rather than thinking the save worked.
    pendingEcho = {
      out: [...cfg.outputConfig],
      in_: [...cfg.inputConfig],
    };
    if (pendingEchoTimer) clearTimeout(pendingEchoTimer);
    pendingEchoTimer = setTimeout(() => { clearPendingEcho(); }, 1000);

    // Side-effect: push equipment labels to orbit simulators (best-effort).
    try {
      await fetch(getHttpUrl('/iot/sync/ioconfig'), {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(cfg),
      });
    } catch { /* orbit may not be running; not fatal */ }
  }

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

  // Equipment label + mode-filter logic now live in equipmentMeta.ts
  // (mirrors AS2's design where the UI owns localized labels — see
  // StoreIoTranslate in legacy StorePostData.c:1242).

  function buildEquipmentList() {
    outputList.splice(0, outputList.length);
    inputList.splice(0,inputList.length);
    lights.splice(0, lights.length);
    outputList.push({ text: $t('global.none'), value: '-1'});
    inputList.push({ text: $t('global.none'), value: '-1'});

    const sys = parseInt(ioConfig.systemMode, 10);
    for (let i = 0; i < ioConfig.ioNames.length - 1; i += 1) {
      const entry = ioConfig.ioNames[i];
      if (entry.ioType === IoOptionEnum.SWITCH) continue;
      if (!entryAppliesToSystem(entry.mode, sys)) continue;

      const idStr = String(entry.index);
      // Hide PULSEDOOR_POWER (eq=40) — not user-assignable; the open/close
      // pair drives the latching pulse door directly. Keeping it in the
      // dropdown only causes confusion.
      if (entry.index === 40) continue;
      const label = resolveEquipmentLabel(entry, $t);
      // Bay lights (EQ_LIGHTS1=23, EQ_LIGHTS2=24) get their own list
      if (entry.index === 23 || entry.index === 24) {
        lights.push({ text: label, value: idStr });
        continue;
      }
      if (entry.ioType === IoOptionEnum.OUTPUT || entry.ioType === IoOptionEnum.BOTH) {
        outputList.push({ text: label, value: idStr });
      }
      if (entry.ioType === IoOptionEnum.INPUT || entry.ioType === IoOptionEnum.BOTH) {
        inputList.push({ text: label, value: idStr });
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
    // Fallback: when the proto OrbitStatus tag (120) hasn't arrived yet
    // the composite emits ioAvailable=[] even though /iot/orbits already
    // shows storage boards connected. Build entries from the REST orbit
    // list so the configuration table renders during proto warm-up and
    // through any subsequent OrbitStatus drop-out.
    if (ioConfig.ioAvailable.length === 0 && orbits.length > 0) {
      const storage = orbits
        .filter((o) => o.connected && o.role === 1)
        .sort((a, z) => a.slot - z.slot);
      ioConfig.ioAvailable = storage.map((_, i) => `Orbit ${i + 1}:10:11:1`);
    }
    ioConfig.ioAvailable.map((io, i) => {
      ioInfo[i] = io.split(':');
    });
    buildEquipmentList();
  }

  // No rebuild needed; backend already sends board view.

  /**
   * Equipment-port mapping is exclusive: a given EQ_* index can only
   * be wired to one port at a time. When the operator assigns an eq
   * to a new port, any other ports already holding that eq must be
   * cleared, otherwise the save serializes both assignments and the
   * firmware's last-write-wins indexed apply lands the eq on whichever
   * port came last in the buffer (typically the stale one). Symptom:
   * "I picked PULSEDOOR_OPEN on DO9 but it disappears" — the prior
   * default at DO11 was still in the array and overwrote the firmware
   * map slot, then the bridge echo decoded the firmware-truth (port
   * 11 — which doesn't render on a 10-DO board) and the UI showed
   * blank. Skip eqRaw==='-1' (clearing) and lights (23/24, EQ_LIGHTS1/2)
   * which are explicitly allowed on multiple ports for bay-light
   * scenarios.
   */
  function clearStaleAssignments(
    arr: string[],
    eqRaw: string,
    keepPid: number,
  ): string[] {
    if (eqRaw === '-1' || eqRaw === '' || eqRaw === undefined) return arr;
    const eqIdx = parseInt(eqRaw, 10);
    if (!Number.isFinite(eqIdx) || eqIdx < 0) return arr;
    if (eqIdx === 23 || eqIdx === 24) return arr; // bay lights are multi-port
    let mutated = false;
    const out = arr.slice();
    for (let p = 0; p < out.length; p++) {
      if (p === keepPid) continue;
      if (out[p] === eqRaw) {
        out[p] = '-1';
        mutated = true;
      }
    }
    return mutated ? out : arr;
  }

  function outputChanged(event: CustomEvent<{ i: number, j: number, value: string }>) {
    const i = event.detail.i;
    const j = event.detail.j;
    const value = parseInt(event.detail.value, 10);
    const pid = (12 * i + j).toString();
    const pidNum = parseInt(pid, 10);
    // Directly write outputConfig (pid-indexed)
    ioConfig.config.outputConfig[pidNum] = event.detail.value;
    // Drop any stale assignment of this same eq to a different output port.
    ioConfig.config.outputConfig = clearStaleAssignments(
      ioConfig.config.outputConfig, event.detail.value, pidNum);
    // Also mirror to input when allowed and valid for this slot
    try {
      const numInputs = parseInt(ioInfo[i]?.[2] || '0', 10);
      const allowedInput = (j <= numInputs) && ((i !== 0) || (j !== 1));
      // Bay lights assignable to any input. Legacy AS2 expansion-board-1
      // port-7/8 hardware limit removed on Constellation.
      const equipOptions = [...inputList, ...lights];
      if (allowedInput && equipOptions.some(opt => opt.value === event.detail.value)) {
        ioConfig.config.inputConfig[pidNum] = event.detail.value;
        ioConfig.config.inputConfig = clearStaleAssignments(
          ioConfig.config.inputConfig, event.detail.value, pidNum);
      }
    } catch {}
    // Special case: humidifier heads auto-map adjacent output as before
    if (value === 7 || value === 9 || value === 11) {
      const nextPid = (pidNum + 1).toString();
      const nextPidNum = pidNum + 1;
      const nextValue = (value + 1).toString();
      ioConfig.config.outputConfig[nextPidNum] = nextValue;
      ioConfig.config.outputConfig = clearStaleAssignments(
        ioConfig.config.outputConfig, nextValue, nextPidNum);
      // Mirror adjacent to input if allowed/valid
      try {
        const nextBoard = Math.floor((parseInt(nextPid, 10)) / 12);
        const nextJ = (parseInt(nextPid, 10)) % 12 || 12; // convert pid to 1..12 within board
        const numInputs2 = parseInt(ioInfo[nextBoard]?.[2] || '0', 10);
        const allowedInput2 = (nextJ <= numInputs2) && ((nextBoard !== 0) || (nextJ !== 1));
        // Same as equipOptions above — bay lights everywhere on Constellation.
        const equipOptions2 = [...inputList, ...lights];
        if (allowedInput2 && equipOptions2.some(opt => opt.value === nextValue)) {
          ioConfig.config.inputConfig[nextPidNum] = nextValue;
          ioConfig.config.inputConfig = clearStaleAssignments(
            ioConfig.config.inputConfig, nextValue, nextPidNum);
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
    const pidNum = parseInt(pid, 10);
    ioConfig.config.inputConfig[pidNum] = event.detail.value;
    ioConfig.config.inputConfig = clearStaleAssignments(
      ioConfig.config.inputConfig, event.detail.value, pidNum);
    ioConfig.config.inputConfig = [...ioConfig.config.inputConfig];
    ioConfig = { ...ioConfig };
  }

  async function outputRenamed(event: CustomEvent<{ i: number, j: number, name: string, type: number }>) {
    const pid = (event.detail.i * 12) + event.detail.j;
    const idx = parseInt(event.detail.type === 0 ? ioConfig.config.outputConfig[pid] : ioConfig.config.inputConfig[pid], 10);
    if (idx !== -1) {
      wait = true;
      // Proto-direct rename: writes envelope sfield 40 (IoNameUpdate),
      // dispatched by Nova firmware to LpSettings_ApplyIoName which
      // validates the slot's renamable flag, persists the rename via
      // the OSPI ping-pong banks, and re-broadcasts IoDefinition (TAG 25).
      // forceFieldRegistry.ts pins field 1 (index) on the wire so slot 0
      // survives proto3 zero-suppression.
      try {
        await writeProto(
          TAG.IoNameUpdate,
          { index: idx, newName: event.detail.name }
        );
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

  onMount(() => {
    $navigationStore.data = '';
    $navigationStore.isDirty = () =>
      !isEqual(ioConfig.config.outputConfig, data.config.outputConfig) ||
      !isEqual(ioConfig.config.inputConfig, data.config.inputConfig);
    void loadOrbits();

    // Hydrate page state from the proto-derived composite. Each emission
    // is treated like a fresh `data` prop: if the user has unsaved edits
    // we skip overwriting (unless a Set-To-Default is pending), matching
    // the legacy invalidate-and-reload flow. Note: `saveIoConfig` re-
    // baselines `data` to match what was just sent, so the firmware echo
    // here will correctly find isDirty() == false and apply.
    const unsub = ioConfigComposite.subscribe((view) => {
      if (!view) return;

      // Pending-echo guard: drop stale broadcasts that don't yet reflect
      // the most recent save. Set-To-Default bypasses this guard since we
      // explicitly want the next broadcast (factory defaults) to apply.
      if (pendingEcho && !pendingDefaultReset) {
        const matches =
          isEqual(view.config.outputConfig, pendingEcho.out) &&
          isEqual(view.config.inputConfig,  pendingEcho.in_);
        if (!matches) {
          // Stale; ignore. Timer will eventually expire if firmware never
          // echoes a matching value.
          ready = true;
          return;
        }
        // Confirmed save — release guard and let normal apply path run.
        clearPendingEcho();
      }

      if (pendingDefaultReset || !$navigationStore.isDirty?.()) {
        // Skip the full setup() rebuild when the incoming view already
        // matches what's on screen. setup() invokes setupIoConfig which
        // does a cloneDeep and reassigns `ioConfig` — forcing every
        // OutputCell/InputCell to re-render against a fresh value prop
        // and producing a visible "disappear / reappear" flicker on the
        // dropdown the operator just changed. When nothing actually
        // changed we just need to bump `data` so isDirty() goes back to
        // false; no UI work required.
        const sameConfig =
          isEqual(view.config.outputConfig, ioConfig.config.outputConfig) &&
          isEqual(view.config.inputConfig,  ioConfig.config.inputConfig);
        data = view;
        if (!sameConfig || pendingDefaultReset) {
          setup(view);
        }
        if (pendingDefaultReset) {
          pendingDefaultReset = false;
          if (defaultTimer) { clearTimeout(defaultTimer); defaultTimer = undefined; }
          wait = false;
        }
        $navigationStore.isDirty = () =>
          !isEqual(ioConfig.config.outputConfig, data.config.outputConfig) ||
          !isEqual(ioConfig.config.inputConfig, data.config.inputConfig);
      }
      ready = true;
    });
    return () => { unsub(); clearPendingEcho(); };
  });

  function setup(data: IOConfigType) {
    // Initialize page state with board arrays supplied
    if (data) {
      setupIoConfig(cloneDeep(data));
    }
  }


  // ─── Orbit topology / role assignment ──────────────────────────────
  // Loaded from /iot/orbits — the bridge knows which Orbit boards have
  // responded to Modbus discovery and what role firmware has stored for
  // each.  The operator picks a role per slot here; the choice is sent
  // to firmware which persists it in OSPI Settings.OrbitRole[].
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
      // Re-run setup if the IO table is still empty so the REST orbit
      // list populates ioAvailable when the proto OrbitStatus tag is
      // missing/late.
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
      if (!resp.ok) {
        console.error('setOrbitRole failed:', await resp.text());
      }
      // Reload bridge view + the IOConfig data (ioAvailable depends on roles).
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
  let pendingDefaultReset = false; // indicates we expect fresh defaults on next data load

  // Pending-echo guard: after a save, the firmware will eventually echo
  // the new config back via the IoConfig broadcast, but a stale broadcast
  // (already in flight at save-time, or a 30-s periodic that hasn't yet
  // incorporated the save) can arrive AFTER the save returns and clobber
  // the optimistic UI state. While `pendingEcho` is set we only accept
  // broadcasts that match the saved snapshot — anything else is treated
  // as stale and dropped. Hard timeout in case the echo never matches
  // (e.g. firmware rejected an entry); after that we accept whatever
  // arrives so the UI eventually converges with reality.
  let pendingEcho: { out: string[]; in_: string[] } | null = null;
  let pendingEchoTimer: ReturnType<typeof setTimeout> | undefined;

  function clearPendingEcho() {
    pendingEcho = null;
    if (pendingEchoTimer) { clearTimeout(pendingEchoTimer); pendingEchoTimer = undefined; }
  }

  async function setToDefault() {
    // If user clicks repeatedly, clear any pending refresh
    if (defaultTimer) {
      clearTimeout(defaultTimer);
      defaultTimer = undefined;
    }
    wait = true;
    // Arm the override IMMEDIATELY — the firmware echo broadcast may
    // arrive in well under a second on a healthy bridge. If we waited 5s
    // the operator could already have started editing and the broadcast
    // would be dropped by isDirty(). The flag self-clears on first apply
    // (see subscriber below).
    pendingDefaultReset = true;
    try {
      await fetch(getHttpUrl('/iot/button'), {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          tag: 'button2',
          // Use the same {tag:'button2', <ActionTag>:<Value>} shape as
          // other Level-2 action buttons (SetDefault, btnPIDDoorLog, …).
          // The bridge /iot/button handler routes this through the
          // generic-extra-fields branch → legacyShim sees firstTag =
          // 'ResetIoConfig' and dispatches to SystemCmd(8). Sending
          // `value: 'resetIoConfig'` instead would collapse to
          // `button2=resetIoConfig` and silently fall through to the
          // unhandled-warning (button never reaches firmware).
          ResetIoConfig: '1',
        }),
      });
      // Safety net: if for some reason the broadcast never arrives, drop
      // the override after 10s so a stale flag doesn't clobber a future
      // legitimate edit. The subscriber clears it on first apply.
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
    if (defaultTimer) {
      clearTimeout(defaultTimer);
    }
  });

  // Note: the legacy `lastDataRef` reactive-sync block is gone — the
  // ioConfigComposite subscriber above is the sole entry point for new
  // firmware-sourced state. The `pendingDefaultReset` flag is honored
  // there so a Set-To-Default forces a re-apply over in-progress edits.
</script>

<GellertPage {wait} {title} {ready} level={2} name="ioconfig">
  <Card class="xl:w-11/12 text-size-xl md:mx-2 xl:mx-auto flex flex-col">
    <ScrollableArea>
      <!-- ── Orbit topology section ──────────────────────────────────
           Lists every Orbit board the controller has discovered on
           Modbus.  The operator picks each slot's role:
             • Storage  → I/O is configured below in the grid.
             • Door     → I/O is configured on the Fresh-Air Door page.
             • Refrig.  → I/O is configured on the Refrigeration page.
             • Unassigned → slot is parked, doesn't appear anywhere.
           The role is persisted to firmware OSPI Settings.OrbitRole[]. -->
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
                <select
                  class="w-full text-size-large border border-gray-400 rounded p-1 bg-white"
                  disabled={orbitsBusy}
                  value={orbit.role}
                  on:change={(e) => onOrbitRoleChange(orbit.slot, e)}
                >
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
              <!-- Standard Mode: render rows up to max(numOutputs, numInputs).
                   LP-AM2434 board has 10 DO + 11 DI — row 11 shows the
                   E-Stop input only (DO column rendered blank). -->
              {#each Array.from({ length: Math.max(parseInt(ioInfo[i][1]) || 0, parseInt(ioInfo[i][2]) || 0) }, (_, index) => index + 1) as j}
                <Row>
                  <Column class="w-2/12 border-r border-gray-400 text-size-large">{j}</Column>
                  {#if j <= (parseInt(ioInfo[i][1]) || 0)}
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
                  {:else}
                    <Column class="w-5/12 border-r border-gray-400 text-size-small text-gray-400 italic px-2">—</Column>
                  {/if}
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
          onSave={saveIoConfig}
        />
      </svelte:fragment>

      <svelte:fragment slot="footer-right">
        <Button size="xl" class="ml-auto !mb-0" on:click={setToDefault}>{ $t('level2.ioconfig.set-to-default') }</Button>
      </svelte:fragment>
    </ScrollableArea>
  </Card>
</GellertPage>



