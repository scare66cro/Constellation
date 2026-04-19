<!--
  TritonHotspotPopup.svelte — Telemetry + GRC settings panel for one hotspot.

  Each hotspot opens a focused modal that mirrors the corresponding section
  of the legacy GRC Level-2 settings page, but scoped to the equipment the
  user clicked on:

    compressor → Basic + Advanced + Lead/Lag tabs
    condenser  → Staging table + VFD config + Fan-count
    exv        → PID + Travel limits + Manual override
    suction    → Pressure switch + Fail mode
    discharge  → High-P/T thresholds + Refrigerant + Fail mode
    oil        → Low-P alarm + Fail mode
    evaporator → Defrost mode + Interval + Term temp + Drip
    ambient    → Reading + Low-ambient cutout + Fail mode
    receiver   → Liquid-line T + Fail mode
    alarms     → Latched-alarm list + ack
    io         → Analog-out + Digital-out role mapping (also reachable from anywhere)

  Writes go through the bridge (which translates to Modbus FC06):
    POST /iot/triton/{slot}/manual      { mode }
    POST /iot/triton/{slot}/setpoints   partial TritonSetpoints
    POST /iot/triton/{slot}/ioconfig    { aoMode?, doRole? }
    POST /iot/triton/{slot}/failures    { sensor: { mode, delaySec }, ... }
    POST /iot/triton/{slot}/ack         (acks all latched alarms)
-->
<script lang="ts">
  import { createEventDispatcher, onMount, onDestroy } from "svelte";
  import Button from "$lib/ui/Button.svelte";
  import { getHttpUrl } from "$lib/business/util";
  import { navigationStore } from "$lib/store";

  /** Hotspot id from TritonMimic — drives which fields render. */
  export let hotspotId: string = '';
  /** Full Triton snapshot from /iot/triton/{slot}. */
  export let state: any = null;
  /** Slot of the active Triton — needed for /iot/triton/{slot}/... writes. */
  export let slot: number | null = null;

  const dispatch = createEventDispatcher<{ close: void; saved: void }>();

  $: editAllowed = $navigationStore.level >= 2;

  // ─── Enums (mirror the orbit-simulator Modbus encoding) ───────────────
  const PUMPDOWN_MODES   = ['NONE','SWITCH','REMOTE','CONTINUOUS'];
  const REFRIGERANTS     = ['R22','R134A','R404A','R407A','R407C','R410A',
                            'R448A','R449A','R450A','R454A','R513A','R600A','R744',
                            'R32','R454B','R454C','R455A','R1234yf','R1234ze(E)','R466A','R515B'];
  const COND_VFD_MODES   = ['Staged (relays)','VFD (analog out)'];
  /** Cond-fan target-pressure mode.  Mirrors GRC `CONDENSER_MODE` plus an
   *  added BALANCED slot reserved for true VFD-PID floating control. */
  const COND_MODES       = ['Fixed','Floating','Balanced'];
  const DEFROST_MODES    = ['NONE','TIMED','DEMAND','HOT_GAS','ELEC'];
  const FAILURE_MODES    = ['ALARM_ONLY','SAFE_OFF','RUN_THROUGH'];
  const AO_MODES         = ['UNUSED','EEV','COMP_VFD','COND_VFD','EVAP_VFD'];
  const DO_ROLES         = ['UNUSED','COMP','COND_FAN','EVAP_FAN','DEFROST',
                            'LIQ_SOL','UNLOADER1','UNLOADER2','UNLOADER3',
                            'UNLOADER4','OIL_PUMP'];
  const COMP_STATUS      = ['AUTO_STANDBY','AUTO_RUN','DEFROST','DEFROST_OVR',
                            'PROVE','SW_PUMPDOWN','RM_PUMPDOWN','SW_OFF',
                            'RM_OFF','SYS_OFF','ERROR','STARTING','PUMPDOWN',
                            'UNLOADING','DEFROST_MAN'];

  // ─── Formatters ────────────────────────────────────────────────────────
  function fmt(v: any, unit = '', digits = 1): string {
    if (v == null || isNaN(Number(v))) return '—';
    return `${Number(v).toFixed(digits)}${unit}`;
  }
  function fmtRuntime(s: number): string {
    if (s == null) return '—';
    const h = Math.floor(s / 3600);
    const m = Math.floor((s % 3600) / 60);
    return `${h}h ${m}m`;
  }
  function fmtTime(ms: number): string {
    if (!ms) return '—';
    return new Date(ms).toLocaleString();
  }
  function enumName(arr: string[], idx: number): string {
    return arr[idx] ?? `(${idx})`;
  }

  // ─── REST helpers ──────────────────────────────────────────────────────
  let busy = false;
  let saveErr = '';
  let saveOk  = '';

  async function postJson(path: string, body: Record<string, unknown>) {
    busy = true; saveErr = ''; saveOk = '';
    try {
      const resp = await fetch(getHttpUrl(path), {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify(body),
      });
      const data = await resp.json().catch(() => ({}));
      if (!resp.ok || data?.ok === false) {
        saveErr = data?.error || `HTTP ${resp.status}`;
      } else {
        const w = data?.written, t = data?.total;
        saveOk = (w != null && t != null) ? `saved (${w}/${t})` : 'saved';
        dispatch('saved');
        setTimeout(() => { saveOk = ''; }, 1500);
      }
    } catch (e: any) {
      saveErr = e?.message ?? 'request failed';
    } finally {
      busy = false;
    }
  }

  function setManual(mode: 'auto'|'force-on'|'force-off') {
    if (slot == null) return;
    postJson(`/iot/triton/${slot}/manual`, { mode });
  }
  function patchSetpoints(patch: Record<string, unknown>) {
    if (slot == null || Object.keys(patch).length === 0) return;
    postJson(`/iot/triton/${slot}/setpoints`, patch);
  }
  function postIoConfig(patch: { aoMode?: number[]; doRole?: number[] }) {
    if (slot == null) return;
    postJson(`/iot/triton/${slot}/ioconfig`, patch);
  }
  function postFailures(patch: Record<string, { mode: number; delaySec: number }>) {
    if (slot == null) return;
    postJson(`/iot/triton/${slot}/failures`, patch);
  }
  function ackAll() {
    if (slot == null) return;
    postJson(`/iot/triton/${slot}/ack`, { all: true });
  }

  /** DI port indices for each safety (matches sim TRITON_DI_NAMES). */
  const SAFETY_DI: Array<{ key: string; label: string; di: number; closedSafe: boolean }> = [
    { key: 'phaseMonitor',      label: 'Phase Monitor',       di: 0, closedSafe: true },
    { key: 'hpSwitch',          label: 'HP Switch',           di: 1, closedSafe: true },
    { key: 'lpSwitch',          label: 'LP Switch',           di: 2, closedSafe: true },
    { key: 'compOverload',      label: 'Compressor Overload', di: 3, closedSafe: true },
    { key: 'condFanOverload',   label: 'Cond Fan Overload',   di: 4, closedSafe: true },
    { key: 'runProve',          label: 'Run Prove',           di: 5, closedSafe: true },
    { key: 'pumpdownSwitch',    label: 'Pumpdown Switch',     di: 6, closedSafe: true },
    { key: 'autoRunPermissive', label: 'Auto-Run Permissive', di: 7, closedSafe: true },
  ];

  function toggleSafetyDi(index: number, value: boolean) {
    if (slot == null) return;
    postJson(`/iot/triton/${slot}/safety/di`, { index, value });
  }
  function resetLockout() {
    if (slot == null) return;
    postJson(`/iot/triton/${slot}/safety/reset`, { mask: 0xFF });
  }

  // ─── Edit-form local state ────────────────────────────────────────────
  // Seed from the latest snapshot whenever a popup opens or the active
  // hotspot changes.  Strings (so <input type=number> binds without quirks).
  //
  // IMPORTANT: the parent polls /iot/triton/{slot} every 1 s and re-passes
  // the new snapshot in via the `state` prop.  If we re-seed on every
  // `state` change the user's in-progress edits (especially <select>
  // choices in the I/O modal) get clobbered every second and feel
  // "unselectable".  Track the last (hotspotId, slot) we seeded for and
  // only re-seed when *that* changes — i.e. when the popup actually opens
  // or the operator picks a different hotspot.  Apply buttons send the
  // local form state to the bridge and dispatch 'saved' so the parent
  // immediately re-fetches the snapshot, after which a fresh open will
  // pick up the new values.
  let edits: Record<string, string> = {};
  let aoMode: number[] = [0, 0, 0, 0];
  let doRole: number[] = Array(10).fill(0);
  let failures: Record<string, { mode: number; delaySec: number }> = {};
  let fanOn: string[]  = ['','','','','',''];
  let fanOff: string[] = ['','','','','',''];
  /** Per-stage cond-fan ON / OFF differentials (PSI above the floating-head
   *  target).  Mirrors GRC `FanDifferential[].on/off` and feeds Triton
   *  setpoints `fanDiffOnP / fanDiffOffP`. */
  let fanDiffOn:  string[] = ['','','','','',''];
  let fanDiffOff: string[] = ['','','','','',''];
  let _seededFor = '';

  $: {
    const key = hotspotId && state ? `${slot}|${hotspotId}` : '';
    if (key && key !== _seededFor) {
      _seededFor = key;
      seedFromState();
    } else if (!hotspotId) {
      _seededFor = '';
    }
  }

  function seedFromState() {
    if (!state) return;
    const sp = state.setpoints ?? {};
    edits = {
      // Capacity / start-stop
      cutInP: String(sp.cutInP ?? ''),
      cutOutP: String(sp.cutOutP ?? ''),
      minOffTime: String(sp.minOffTime ?? ''),
      minRuntime: String(sp.minRuntime ?? ''),
      warmUpSec: String(sp.warmUpSec ?? ''),
      proveSec: String(sp.proveSec ?? ''),
      powerFailMinutes: String(sp.powerFailMinutes ?? ''),
      crankcaseRunHours: String(sp.crankcaseRunHours ?? ''),
      lowAmbientCutoutF: String(sp.lowAmbientCutoutF ?? ''),
      pumpDownMode: String(sp.pumpDownMode ?? '0'),
      variableStartPct: String(sp.variableStartPct ?? ''),
      refrigerantType: String(sp.refrigerantType ?? '0'),
      groupId: String(sp.groupId ?? '0'),
      rotationOrder: String(sp.rotationOrder ?? '1'),
      rotateHours: String(sp.rotateHours ?? ''),
      // Superheat
      superheatTarget: String(sp.superheatTarget ?? ''),
      superheatLowF: String(sp.superheatLowF ?? ''),
      superheatWindowHighF: String(sp.superheatWindowHighF ?? ''),
      superheatWindowLowF: String(sp.superheatWindowLowF ?? ''),
      // Forced unload
      discHighUnloadP: String(sp.discHighUnloadP ?? ''),
      sucLowUnloadP: String(sp.sucLowUnloadP ?? ''),
      // Cond fans
      condFanCount: String(sp.condFanCount ?? '4'),
      condFanVfdMode: String(sp.condFanVfdMode ?? '0'),
      condFanVfdMinPct: String(sp.condFanVfdMinPct ?? ''),
      condFanVfdMaxPct: String(sp.condFanVfdMaxPct ?? ''),
      condFanVfdSetpointP: String(sp.condFanVfdSetpointP ?? ''),
      // Floating-head condenser control (GRC TargetDischarge equivalents)
      condenserMode: String(sp.condenserMode ?? '1'),
      condApproachF: String(sp.condApproachF ?? '10'),
      condMinHeadP:  String(sp.condMinHeadP  ?? ''),
      condMaxHeadP:  String(sp.condMaxHeadP  ?? ''),
      // EXV
      exvKp: String(sp.exvKp ?? ''),
      exvKi: String(sp.exvKi ?? ''),
      exvKd: String(sp.exvKd ?? ''),
      exvMinPct: String(sp.exvMinPct ?? ''),
      exvMaxPct: String(sp.exvMaxPct ?? ''),
      exvManualPct: String(sp.exvManualPct ?? ''),
      subcoolingTarget: String(sp.subcoolingTarget ?? ''),
      // Defrost
      defrostMode: String(sp.defrostMode ?? '0'),
      defrostStages: String(sp.defrostStages ?? '1'),
      defrostIntervalHours: String(sp.defrostIntervalHours ?? ''),
      defrostMaxMinutes: String(sp.defrostMaxMinutes ?? ''),
      defrostTermT: String(sp.defrostTermT ?? ''),
      dripTimeSec: String(sp.dripTimeSec ?? ''),
      pumpDownBeforeDefrost: String(sp.pumpDownBeforeDefrost ?? '0'),
      // PID
      capP: String(sp.capP ?? ''), capI: String(sp.capI ?? ''),
      capD: String(sp.capD ?? ''), capU: String(sp.capU ?? ''),
      condP: String(sp.condP ?? ''), condI: String(sp.condI ?? ''),
      condD: String(sp.condD ?? ''), condU: String(sp.condU ?? ''),
    };
    fanOn  = (sp.fanStageOnP  ?? []).slice(0, 6).map((v: any) => String(v));
    fanOff = (sp.fanStageOffP ?? []).slice(0, 6).map((v: any) => String(v));
    while (fanOn.length  < 6) fanOn.push('');
    while (fanOff.length < 6) fanOff.push('');
    fanDiffOn  = (sp.fanDiffOnP  ?? []).slice(0, 6).map((v: any) => String(v));
    fanDiffOff = (sp.fanDiffOffP ?? []).slice(0, 6).map((v: any) => String(v));
    while (fanDiffOn.length  < 6) fanDiffOn.push('');
    while (fanDiffOff.length < 6) fanDiffOff.push('');
    aoMode = (state.ioConfig?.aoMode ?? [0,0,0,0]).slice(0, 4);
    doRole = (state.ioConfig?.doRole ?? Array(10).fill(0)).slice(0, 10);
    while (aoMode.length < 4)  aoMode.push(0);
    while (doRole.length < 10) doRole.push(0);
    const f = state.failures ?? {};
    failures = {};
    for (const k of ['suctionP','dischargeP','oilP','suctionT','dischargeT','llsT','ambientT']) {
      failures[k] = {
        mode: f[k]?.mode ?? 0,
        delaySec: f[k]?.delaySec ?? 0,
      };
    }
  }

  function num(s: string): number | null {
    const v = parseFloat(s);
    return isFinite(v) ? v : null;
  }
  function patchFromKeys(keys: string[]): Record<string, number> {
    const out: Record<string, number> = {};
    for (const k of keys) {
      const v = num(edits[k] ?? '');
      if (v != null) out[k] = v;
    }
    return out;
  }
  function intPatchFromKeys(keys: string[]): Record<string, number> {
    const out: Record<string, number> = {};
    for (const k of keys) {
      const v = parseInt(edits[k] ?? '', 10);
      if (Number.isFinite(v)) out[k] = v;
    }
    return out;
  }
  function applyFanStaging() {
    const onArr  = fanOn.map(s  => num(s)).filter(v => v != null) as number[];
    const offArr = fanOff.map(s => num(s)).filter(v => v != null) as number[];
    patchSetpoints({ fanStageOnP: onArr, fanStageOffP: offArr });
  }
  /** Push the per-stage floating-head differential arrays.  Empty cells are
   *  dropped — the simulator falls back to its defaults for missing slots. */
  function applyFanDiffs() {
    const onArr  = fanDiffOn.map(s  => num(s)).filter(v => v != null) as number[];
    const offArr = fanDiffOff.map(s => num(s)).filter(v => v != null) as number[];
    patchSetpoints({ fanDiffOnP: onArr, fanDiffOffP: offArr });
  }

  // ─── Per-hotspot title + read-only rows ───────────────────────────────
  $: detail = (() => {
    if (!state) return { title: 'No data', rows: [] as { label: string; value: string }[] };
    const s  = state.sensors  ?? {};
    const sp = state.setpoints ?? {};
    switch (hotspotId) {
      case 'compressor':
        return {
          title: 'Compressor',
          rows: [
            { label: 'Status',         value: enumName(COMP_STATUS, state.compressor?.status ?? 0) },
            { label: 'Run',            value: state.compressor?.on ? 'ON' : 'OFF' },
            { label: 'Manual mode',    value: state.manualMode ?? 'auto' },
            { label: 'Current draw',   value: fmt(state.compressor?.amps, ' A', 1) },
            { label: 'Runtime (live)', value: fmtRuntime(state.compressor?.runtimeSec) },
            { label: 'Total runtime',  value: `${state.compressor?.totalRuntimeHours ?? 0} h` },
            { label: 'Daily runtime',  value: `${state.compressor?.dailyRuntimeHours ?? 0} h` },
            { label: 'Refrigerant',    value: enumName(REFRIGERANTS, sp.refrigerantType ?? 0) },
            { label: 'Lead/lag group', value: `${sp.groupId ?? 0} / order ${sp.rotationOrder ?? 1}` },
          ],
        };
      case 'condenser':
      case 'condFans':
        return {
          title: 'Condenser',
          rows: [
            { label: 'Discharge P',   value: fmt(s.dischargeP?.value, ' PSI', 1) },
            { label: 'Discharge T',   value: fmt(s.dischargeT?.value, '°F', 1) },
            { label: 'Fan stage',     value: `${state.condenserFans?.stage ?? 0} / ${state.condenserFans?.count ?? 0}` },
            { label: 'Mode',          value: enumName(COND_VFD_MODES, sp.condFanVfdMode ?? 0) },
            { label: 'Target mode',   value: enumName(COND_MODES, sp.condenserMode ?? 0) },
            { label: 'Live target',   value: fmt(state.condenserFans?.targetP, ' PSI', 1) },
            { label: 'Approach ΔT',   value: fmt(sp.condApproachF, '°F', 0) },
            { label: 'Head clamp',    value: `${sp.condMinHeadP ?? '—'} – ${sp.condMaxHeadP ?? '—'} PSI` },
          ],
        };
      case 'receiver':
      case 'liquid':
        return {
          title: 'Receiver / Liquid Line',
          rows: [
            { label: 'Liquid line T', value: fmt(s.llsT?.value, '°F', 1) },
            { label: 'High alarm T',  value: fmt(s.llsT?.highAlarm, '°F', 0) },
          ],
        };
      case 'exv':
        return {
          title: 'Expansion Valve (EXV)',
          rows: [
            { label: 'Open %',     value: fmt(state.exvOpenPct, ' %', 1) },
            { label: 'Superheat',  value: fmt(state.derived?.superheat, '°F', 1) },
            { label: 'SH target',  value: fmt(sp.superheatTarget, '°F', 1) },
            { label: 'Subcooling', value: fmt(sp.subcoolingTarget, '°F', 1) },
          ],
        };
      case 'evaporator':
        return {
          title: 'Evaporator / Defrost',
          rows: [
            { label: 'Suction P',      value: fmt(s.suctionP?.value, ' PSI', 1) },
            { label: 'Suction T',      value: fmt(s.suctionT?.value, '°F', 1) },
            { label: 'Superheat',      value: fmt(state.derived?.superheat, '°F', 1) },
            { label: 'Defrost mode',   value: enumName(DEFROST_MODES, sp.defrostMode ?? 0) },
            { label: 'Defrost active', value: state.defrostActive ? 'YES' : 'no' },
            { label: 'Last defrost',   value: fmtTime(state.lastDefrostEnd) },
          ],
        };
      case 'suction':
        return {
          title: 'Suction Line',
          rows: [
            { label: 'Pressure',     value: fmt(s.suctionP?.value, ' PSI', 1) },
            { label: 'Temperature',  value: fmt(s.suctionT?.value, '°F', 1) },
            { label: 'Low alarm',    value: fmt(s.suctionP?.lowAlarm, ' PSI', 0) },
            { label: 'High alarm',   value: fmt(s.suctionP?.highAlarm, ' PSI', 0) },
            { label: 'Low unload P', value: fmt(sp.sucLowUnloadP, ' PSI', 0) },
          ],
        };
      case 'discharge':
        return {
          title: 'Discharge Line',
          rows: [
            { label: 'Pressure',      value: fmt(s.dischargeP?.value, ' PSI', 1) },
            { label: 'Temperature',   value: fmt(s.dischargeT?.value, '°F', 1) },
            { label: 'High P alarm',  value: fmt(s.dischargeP?.highAlarm, ' PSI', 0) },
            { label: 'High T alarm',  value: fmt(s.dischargeT?.highAlarm, '°F', 0) },
            { label: 'High unload P', value: fmt(sp.discHighUnloadP, ' PSI', 0) },
          ],
        };
      case 'oil':
        return {
          title: 'Oil System',
          rows: [
            { label: 'Pressure',  value: fmt(s.oilP?.value, ' PSI', 1) },
            { label: 'Low alarm', value: fmt(s.oilP?.lowAlarm, ' PSI', 0) },
          ],
        };
      case 'ambient':
        return {
          title: 'Ambient',
          rows: [
            { label: 'Temperature',         value: fmt(s.ambientT?.value, '°F', 1) },
            { label: 'Low-ambient cutout',  value: fmt(sp.lowAmbientCutoutF, '°F', 0) },
          ],
        };
      case 'alarms':
        return { title: 'Alarms', rows: [] };
      case 'io':
        return { title: 'I/O Configuration', rows: [] };
      case 'safety': {
        const sf = state.safeties ?? {};
        const ok = (b: any) => b ? 'OK (closed)' : 'TRIPPED (open)';
        return {
          title: 'Safety Interlocks',
          rows: [
            { label: 'Phase Monitor',        value: ok(sf.phaseMonitor) },
            { label: 'HP Switch',            value: ok(sf.hpSwitch) },
            { label: 'LP Switch',            value: ok(sf.lpSwitch) },
            { label: 'Compressor Overload',  value: ok(sf.compOverload) },
            { label: 'Cond Fan Overload',    value: ok(sf.condFanOverload) },
            { label: 'Run Prove',            value: ok(sf.runProve) },
            { label: 'Pumpdown Switch',      value: sf.pumpdownSwitch ? 'Idle (closed)' : 'PUMPDOWN (open)' },
            { label: 'Auto-Run Permissive',  value: ok(sf.autoRunPermissive) },
            { label: 'Lockout latched',      value: '0x' + ((sf.lockoutMask ?? 0) & 0xFF).toString(16).padStart(2, '0') },
          ],
        };
      }
      default:
        return { title: hotspotId || 'Detail', rows: [] };
    }
  })();

  function handleKey(e: KeyboardEvent) {
    if (e.key === 'Escape') dispatch('close');
  }
  onMount(() => window.addEventListener('keydown', handleKey));
  onDestroy(() => window.removeEventListener('keydown', handleKey));
</script>

{#if hotspotId}
  <div class="fixed inset-0 bg-black/40 z-50 flex items-center justify-center p-4"
       on:click={() => dispatch('close')}
       on:keydown={(e) => { if (e.key === 'Enter') dispatch('close'); }}
       role="presentation">
    <!-- svelte-ignore a11y-no-noninteractive-element-interactions -->
    <div class="bg-white rounded-lg shadow-xl max-w-2xl w-full max-h-[90vh] overflow-y-auto"
         on:click|stopPropagation
         on:keydown|stopPropagation
         role="dialog" aria-modal="true" aria-labelledby="popup-title">
      <header class="flex items-center justify-between border-b px-4 py-2 bg-gray-50 sticky top-0 z-10">
        <h3 id="popup-title" class="font-bold text-size-large">{detail.title}</h3>
        <Button size="sm" on:click={() => dispatch('close')} class="ml-2">Close</Button>
      </header>

      <div class="p-4 space-y-3">
        {#if detail.rows.length > 0}
          <dl class="grid grid-cols-[max-content_1fr] gap-x-4 gap-y-1 text-sm">
            {#each detail.rows as r}
              <dt class="text-gray-500">{r.label}</dt>
              <dd class="font-mono text-right">{r.value}</dd>
            {/each}
          </dl>
        {/if}

        {#if editAllowed && slot != null}
          <!-- ────────── COMPRESSOR ────────── -->
          {#if hotspotId === 'compressor'}
            <div class="border-t pt-3">
              <div class="text-xs font-bold text-gray-700 mb-1">Manual override</div>
              <div class="flex gap-2">
                <Button size="sm" disabled={busy} on:click={() => setManual('auto')}>Auto</Button>
                <Button size="sm" disabled={busy} on:click={() => setManual('force-on')}>Force ON</Button>
                <Button size="sm" disabled={busy} on:click={() => setManual('force-off')}>Force OFF</Button>
              </div>
            </div>

            <details open class="border-t pt-2">
              <summary class="cursor-pointer text-xs font-bold text-gray-700">Capacity (start/stop)</summary>
              <div class="grid grid-cols-2 gap-2 text-sm mt-2">
                <label class="flex flex-col">Cut-in PSI<input type="number" class="border rounded px-2 py-1 font-mono" bind:value={edits.cutInP}/></label>
                <label class="flex flex-col">Cut-out PSI<input type="number" class="border rounded px-2 py-1 font-mono" bind:value={edits.cutOutP}/></label>
                <label class="flex flex-col">Min off (s)<input type="number" class="border rounded px-2 py-1 font-mono" bind:value={edits.minOffTime}/></label>
                <label class="flex flex-col">Min runtime (s)<input type="number" class="border rounded px-2 py-1 font-mono" bind:value={edits.minRuntime}/></label>
                <Button size="sm" class="col-span-2" disabled={busy}
                        on:click={() => patchSetpoints(patchFromKeys(['cutInP','cutOutP','minOffTime','minRuntime']))}>Apply</Button>
              </div>
            </details>

            <details class="border-t pt-2">
              <summary class="cursor-pointer text-xs font-bold text-gray-700">Crankcase / Prove / Power-fail</summary>
              <div class="grid grid-cols-2 gap-2 text-sm mt-2">
                <label class="flex flex-col">Warm-up (s)<input type="number" class="border rounded px-2 py-1 font-mono" bind:value={edits.warmUpSec}/></label>
                <label class="flex flex-col">Prove (s)<input type="number" class="border rounded px-2 py-1 font-mono" bind:value={edits.proveSec}/></label>
                <label class="flex flex-col">Power-fail (min)<input type="number" class="border rounded px-2 py-1 font-mono" bind:value={edits.powerFailMinutes}/></label>
                <label class="flex flex-col">Crankcase run (h)<input type="number" class="border rounded px-2 py-1 font-mono" bind:value={edits.crankcaseRunHours}/></label>
                <label class="flex flex-col">Var. start %<input type="number" class="border rounded px-2 py-1 font-mono" bind:value={edits.variableStartPct}/></label>
                <label class="flex flex-col">Pump-down mode
                  <select class="border rounded px-2 py-1" bind:value={edits.pumpDownMode}>
                    {#each PUMPDOWN_MODES as m, i}<option value={String(i)}>{m}</option>{/each}
                  </select>
                </label>
                <Button size="sm" class="col-span-2" disabled={busy}
                        on:click={() => patchSetpoints(intPatchFromKeys(
                          ['warmUpSec','proveSec','powerFailMinutes','crankcaseRunHours','variableStartPct','pumpDownMode']))}>Apply</Button>
              </div>
            </details>

            <details class="border-t pt-2">
              <summary class="cursor-pointer text-xs font-bold text-gray-700">Lead/Lag rotation</summary>
              <div class="grid grid-cols-2 gap-2 text-sm mt-2">
                <label class="flex flex-col">Group ID<input type="number" class="border rounded px-2 py-1 font-mono" bind:value={edits.groupId}/></label>
                <label class="flex flex-col">Order (1..6)<input type="number" min="1" max="6" class="border rounded px-2 py-1 font-mono" bind:value={edits.rotationOrder}/></label>
                <label class="flex flex-col col-span-2">Rotate every (h)<input type="number" class="border rounded px-2 py-1 font-mono" bind:value={edits.rotateHours}/></label>
                <Button size="sm" class="col-span-2" disabled={busy}
                        on:click={() => patchSetpoints(intPatchFromKeys(['groupId','rotationOrder','rotateHours']))}>Apply</Button>
              </div>
            </details>

            <details class="border-t pt-2">
              <summary class="cursor-pointer text-xs font-bold text-gray-700">Refrigerant</summary>
              <div class="grid grid-cols-2 gap-2 text-sm mt-2">
                <label class="flex flex-col col-span-2">Type
                  <select class="border rounded px-2 py-1" bind:value={edits.refrigerantType}>
                    {#each REFRIGERANTS as r, i}<option value={String(i)}>{r}</option>{/each}
                  </select>
                </label>
                <Button size="sm" class="col-span-2" disabled={busy}
                        on:click={() => patchSetpoints(intPatchFromKeys(['refrigerantType']))}>Apply</Button>
              </div>
            </details>
          {/if}

          <!-- ────────── CONDENSER ────────── -->
          {#if hotspotId === 'condenser' || hotspotId === 'condFans'}
            <details open class="border-t pt-2">
              <summary class="cursor-pointer text-xs font-bold text-gray-700">Floating-head target (GRC TargetDischarge)</summary>
              <div class="grid grid-cols-2 gap-2 text-sm mt-2">
                <label class="flex flex-col col-span-2">Target mode
                  <select class="border rounded px-2 py-1" bind:value={edits.condenserMode}>
                    {#each COND_MODES as m, i}<option value={String(i)}>{m}</option>{/each}
                  </select>
                  <span class="text-xs text-gray-500 mt-0.5">
                    Floating chases <code>P<sub>sat</sub>(OAT&nbsp;+&nbsp;approach)</code>
                    clamped to [min,max]; Fixed uses the head-pressure setpoint below.
                  </span>
                </label>
                <label class="flex flex-col">Approach ΔT (°F)<input type="number" step="1" min="0" max="40" class="border rounded px-2 py-1 font-mono" bind:value={edits.condApproachF}/></label>
                <label class="flex flex-col">Min head (PSI)<input type="number" min="0" class="border rounded px-2 py-1 font-mono" bind:value={edits.condMinHeadP}/></label>
                <label class="flex flex-col col-span-2">Max head (PSI)<input type="number" min="0" class="border rounded px-2 py-1 font-mono" bind:value={edits.condMaxHeadP}/></label>
                {#if typeof state?.condenserFans?.targetP === 'number'}
                  <div class="col-span-2 text-xs text-emerald-700 font-mono">
                    Live target: {state.condenserFans.targetP.toFixed(1)} PSI
                  </div>
                {/if}
                <Button size="sm" class="col-span-2" disabled={busy}
                        on:click={() => patchSetpoints({
                          ...intPatchFromKeys(['condenserMode','condMinHeadP','condMaxHeadP']),
                          ...patchFromKeys(['condApproachF']),
                        })}>Apply</Button>
              </div>
            </details>

            <details class="border-t pt-2">
              <summary class="cursor-pointer text-xs font-bold text-gray-700">Fan differentials (PSI relative to floating target)</summary>
              <p class="text-xs text-gray-500 mt-1">
                Used in Floating / Balanced mode. Each stage turns ON when discharge ≥ target + diffOn,
                and OFF when ≤ target + diffOff. Mirrors GRC <code>FanDifferential[].on/off</code>
                (defaults: ON = (i+1)·10, OFF = 5).
              </p>
              <table class="w-full text-sm mt-2 border-collapse">
                <thead class="bg-gray-50 text-xs">
                  <tr><th class="border px-1">Stage</th><th class="border px-1">+On</th><th class="border px-1">+Off</th></tr>
                </thead>
                <tbody>
                  {#each Array(6) as _, i}
                    <tr>
                      <td class="border px-1 text-center">{i + 1}</td>
                      <td class="border"><input type="number" class="w-full px-1 font-mono" bind:value={fanDiffOn[i]}/></td>
                      <td class="border"><input type="number" class="w-full px-1 font-mono" bind:value={fanDiffOff[i]}/></td>
                    </tr>
                  {/each}
                </tbody>
              </table>
              <Button size="sm" class="mt-2 w-full" disabled={busy} on:click={applyFanDiffs}>Apply differentials</Button>
            </details>

            <details class="border-t pt-2">
              <summary class="cursor-pointer text-xs font-bold text-gray-700">Fan staging — absolute PSI (Fixed mode)</summary>
              <p class="text-xs text-gray-500 mt-1">
                Used in Fixed mode (or when the OAT sensor is invalid). Each stage's
                ON/OFF threshold is an absolute discharge pressure.
              </p>
              <table class="w-full text-sm mt-2 border-collapse">
                <thead class="bg-gray-50 text-xs">
                  <tr><th class="border px-1">Stage</th><th class="border px-1">On</th><th class="border px-1">Off</th></tr>
                </thead>
                <tbody>
                  {#each Array(6) as _, i}
                    <tr>
                      <td class="border px-1 text-center">{i + 1}</td>
                      <td class="border"><input type="number" class="w-full px-1 font-mono" bind:value={fanOn[i]}/></td>
                      <td class="border"><input type="number" class="w-full px-1 font-mono" bind:value={fanOff[i]}/></td>
                    </tr>
                  {/each}
                </tbody>
              </table>
              <Button size="sm" class="mt-2 w-full" disabled={busy} on:click={applyFanStaging}>Apply staging table</Button>
            </details>

            <details class="border-t pt-2">
              <summary class="cursor-pointer text-xs font-bold text-gray-700">Fan count + VFD config</summary>
              <div class="grid grid-cols-2 gap-2 text-sm mt-2">
                <label class="flex flex-col">Fan count (1-6)<input type="number" min="1" max="6" class="border rounded px-2 py-1 font-mono" bind:value={edits.condFanCount}/></label>
                <label class="flex flex-col">Mode
                  <select class="border rounded px-2 py-1" bind:value={edits.condFanVfdMode}>
                    {#each COND_VFD_MODES as m, i}<option value={String(i)}>{m}</option>{/each}
                  </select>
                </label>
                <label class="flex flex-col">VFD min %<input type="number" class="border rounded px-2 py-1 font-mono" bind:value={edits.condFanVfdMinPct}/></label>
                <label class="flex flex-col">VFD max %<input type="number" class="border rounded px-2 py-1 font-mono" bind:value={edits.condFanVfdMaxPct}/></label>
                <label class="flex flex-col col-span-2">Head pressure setpoint (PSI)<input type="number" class="border rounded px-2 py-1 font-mono" bind:value={edits.condFanVfdSetpointP}/></label>
                <Button size="sm" class="col-span-2" disabled={busy}
                        on:click={() => patchSetpoints(intPatchFromKeys(
                          ['condFanCount','condFanVfdMode','condFanVfdMinPct','condFanVfdMaxPct','condFanVfdSetpointP']))}>Apply</Button>
              </div>
            </details>

            <details class="border-t pt-2">
              <summary class="cursor-pointer text-xs font-bold text-gray-700">Condenser PID</summary>
              <div class="grid grid-cols-4 gap-2 text-sm mt-2">
                <label class="flex flex-col">P<input type="number" step="0.1" class="border rounded px-2 py-1 font-mono" bind:value={edits.condP}/></label>
                <label class="flex flex-col">I<input type="number" step="0.1" class="border rounded px-2 py-1 font-mono" bind:value={edits.condI}/></label>
                <label class="flex flex-col">D<input type="number" step="0.1" class="border rounded px-2 py-1 font-mono" bind:value={edits.condD}/></label>
                <label class="flex flex-col">U<input type="number" step="0.1" class="border rounded px-2 py-1 font-mono" bind:value={edits.condU}/></label>
                <Button size="sm" class="col-span-4" disabled={busy}
                        on:click={() => patchSetpoints(patchFromKeys(['condP','condI','condD','condU']))}>Apply PID</Button>
              </div>
            </details>
          {/if}

          <!-- ────────── EXV ────────── -->
          {#if hotspotId === 'exv'}
            <details open class="border-t pt-2">
              <summary class="cursor-pointer text-xs font-bold text-gray-700">Superheat / Subcooling targets</summary>
              <div class="grid grid-cols-2 gap-2 text-sm mt-2">
                <label class="flex flex-col">SH target (°F)<input type="number" step="0.5" class="border rounded px-2 py-1 font-mono" bind:value={edits.superheatTarget}/></label>
                <label class="flex flex-col">SC target (°F)<input type="number" step="0.5" class="border rounded px-2 py-1 font-mono" bind:value={edits.subcoolingTarget}/></label>
                <label class="flex flex-col">SH low (°F)<input type="number" step="0.5" class="border rounded px-2 py-1 font-mono" bind:value={edits.superheatLowF}/></label>
                <label class="flex flex-col">SH window low<input type="number" step="0.5" class="border rounded px-2 py-1 font-mono" bind:value={edits.superheatWindowLowF}/></label>
                <label class="flex flex-col col-span-2">SH window high (°F)<input type="number" step="0.5" class="border rounded px-2 py-1 font-mono" bind:value={edits.superheatWindowHighF}/></label>
                <Button size="sm" class="col-span-2" disabled={busy}
                        on:click={() => patchSetpoints(patchFromKeys(
                          ['superheatTarget','subcoolingTarget','superheatLowF','superheatWindowLowF','superheatWindowHighF']))}>Apply</Button>
              </div>
            </details>

            <details class="border-t pt-2">
              <summary class="cursor-pointer text-xs font-bold text-gray-700">EXV PID + travel limits</summary>
              <div class="grid grid-cols-3 gap-2 text-sm mt-2">
                <label class="flex flex-col">Kp<input type="number" step="0.1" class="border rounded px-2 py-1 font-mono" bind:value={edits.exvKp}/></label>
                <label class="flex flex-col">Ki<input type="number" step="0.1" class="border rounded px-2 py-1 font-mono" bind:value={edits.exvKi}/></label>
                <label class="flex flex-col">Kd<input type="number" step="0.1" class="border rounded px-2 py-1 font-mono" bind:value={edits.exvKd}/></label>
                <label class="flex flex-col">Min %<input type="number" class="border rounded px-2 py-1 font-mono" bind:value={edits.exvMinPct}/></label>
                <label class="flex flex-col">Max %<input type="number" class="border rounded px-2 py-1 font-mono" bind:value={edits.exvMaxPct}/></label>
                <label class="flex flex-col">Manual %<input type="number" class="border rounded px-2 py-1 font-mono" bind:value={edits.exvManualPct}/></label>
                <Button size="sm" class="col-span-3" disabled={busy}
                        on:click={() => patchSetpoints({
                          ...patchFromKeys(['exvKp','exvKi','exvKd']),
                          ...intPatchFromKeys(['exvMinPct','exvMaxPct','exvManualPct']),
                        })}>Apply</Button>
              </div>
            </details>
          {/if}

          <!-- ────────── EVAPORATOR / DEFROST ────────── -->
          {#if hotspotId === 'evaporator'}
            <details open class="border-t pt-2">
              <summary class="cursor-pointer text-xs font-bold text-gray-700">Defrost</summary>
              <div class="grid grid-cols-2 gap-2 text-sm mt-2">
                <label class="flex flex-col">Mode
                  <select class="border rounded px-2 py-1" bind:value={edits.defrostMode}>
                    {#each DEFROST_MODES as m, i}<option value={String(i)}>{m}</option>{/each}
                  </select>
                </label>
                <label class="flex flex-col">Stages (1-2)<input type="number" min="1" max="2" class="border rounded px-2 py-1 font-mono" bind:value={edits.defrostStages}/></label>
                <label class="flex flex-col">Interval (h)<input type="number" class="border rounded px-2 py-1 font-mono" bind:value={edits.defrostIntervalHours}/></label>
                <label class="flex flex-col">Max (min)<input type="number" class="border rounded px-2 py-1 font-mono" bind:value={edits.defrostMaxMinutes}/></label>
                <label class="flex flex-col">Term temp (°F)<input type="number" class="border rounded px-2 py-1 font-mono" bind:value={edits.defrostTermT}/></label>
                <label class="flex flex-col">Drip time (s)<input type="number" class="border rounded px-2 py-1 font-mono" bind:value={edits.dripTimeSec}/></label>
                <label class="flex flex-col col-span-2">Pump-down before defrost
                  <select class="border rounded px-2 py-1" bind:value={edits.pumpDownBeforeDefrost}>
                    <option value="0">NO</option>
                    <option value="1">YES</option>
                  </select>
                </label>
                <Button size="sm" class="col-span-2" disabled={busy}
                        on:click={() => patchSetpoints({
                          ...intPatchFromKeys(['defrostMode','defrostStages','defrostIntervalHours','defrostMaxMinutes','dripTimeSec','pumpDownBeforeDefrost']),
                          ...patchFromKeys(['defrostTermT']),
                        })}>Apply defrost</Button>
              </div>
            </details>

            <details class="border-t pt-2">
              <summary class="cursor-pointer text-xs font-bold text-gray-700">Capacity PID</summary>
              <div class="grid grid-cols-4 gap-2 text-sm mt-2">
                <label class="flex flex-col">P<input type="number" step="0.1" class="border rounded px-2 py-1 font-mono" bind:value={edits.capP}/></label>
                <label class="flex flex-col">I<input type="number" step="0.1" class="border rounded px-2 py-1 font-mono" bind:value={edits.capI}/></label>
                <label class="flex flex-col">D<input type="number" step="0.1" class="border rounded px-2 py-1 font-mono" bind:value={edits.capD}/></label>
                <label class="flex flex-col">U<input type="number" step="0.1" class="border rounded px-2 py-1 font-mono" bind:value={edits.capU}/></label>
                <Button size="sm" class="col-span-4" disabled={busy}
                        on:click={() => patchSetpoints(patchFromKeys(['capP','capI','capD','capU']))}>Apply PID</Button>
              </div>
            </details>
          {/if}

          <!-- ────────── DISCHARGE ────────── -->
          {#if hotspotId === 'discharge'}
            <details open class="border-t pt-2">
              <summary class="cursor-pointer text-xs font-bold text-gray-700">High-discharge unload</summary>
              <div class="grid grid-cols-2 gap-2 text-sm mt-2">
                <label class="flex flex-col col-span-2">Unload above (PSI)<input type="number" class="border rounded px-2 py-1 font-mono" bind:value={edits.discHighUnloadP}/></label>
                <Button size="sm" class="col-span-2" disabled={busy}
                        on:click={() => patchSetpoints(intPatchFromKeys(['discHighUnloadP']))}>Apply</Button>
              </div>
            </details>

            <details class="border-t pt-2">
              <summary class="cursor-pointer text-xs font-bold text-gray-700">Refrigerant</summary>
              <div class="grid grid-cols-2 gap-2 text-sm mt-2">
                <label class="flex flex-col col-span-2">Type
                  <select class="border rounded px-2 py-1" bind:value={edits.refrigerantType}>
                    {#each REFRIGERANTS as r, i}<option value={String(i)}>{r}</option>{/each}
                  </select>
                </label>
                <Button size="sm" class="col-span-2" disabled={busy}
                        on:click={() => patchSetpoints(intPatchFromKeys(['refrigerantType']))}>Apply</Button>
              </div>
            </details>

            <details class="border-t pt-2">
              <summary class="cursor-pointer text-xs font-bold text-gray-700">Failure handling</summary>
              {#each [['dischargeP','Discharge P'], ['dischargeT','Discharge T']] as [k, label]}
                <div class="grid grid-cols-3 gap-2 text-sm mt-2 items-end">
                  <label class="flex flex-col">{label}
                    <select class="border rounded px-2 py-1" bind:value={failures[k].mode}>
                      {#each FAILURE_MODES as m, i}<option value={i}>{m}</option>{/each}
                    </select>
                  </label>
                  <label class="flex flex-col">Delay (s)<input type="number" class="border rounded px-2 py-1 font-mono" bind:value={failures[k].delaySec}/></label>
                  <Button size="sm" disabled={busy} on:click={() => postFailures({ [k]: failures[k] })}>Apply</Button>
                </div>
              {/each}
            </details>
          {/if}

          <!-- ────────── SUCTION ────────── -->
          {#if hotspotId === 'suction'}
            <details open class="border-t pt-2">
              <summary class="cursor-pointer text-xs font-bold text-gray-700">Low-suction unload</summary>
              <div class="grid grid-cols-2 gap-2 text-sm mt-2">
                <label class="flex flex-col col-span-2">Unload below (PSI)<input type="number" class="border rounded px-2 py-1 font-mono" bind:value={edits.sucLowUnloadP}/></label>
                <Button size="sm" class="col-span-2" disabled={busy}
                        on:click={() => patchSetpoints(intPatchFromKeys(['sucLowUnloadP']))}>Apply</Button>
              </div>
            </details>

            <details class="border-t pt-2">
              <summary class="cursor-pointer text-xs font-bold text-gray-700">Failure handling</summary>
              {#each [['suctionP','Suction P'], ['suctionT','Suction T']] as [k, label]}
                <div class="grid grid-cols-3 gap-2 text-sm mt-2 items-end">
                  <label class="flex flex-col">{label}
                    <select class="border rounded px-2 py-1" bind:value={failures[k].mode}>
                      {#each FAILURE_MODES as m, i}<option value={i}>{m}</option>{/each}
                    </select>
                  </label>
                  <label class="flex flex-col">Delay (s)<input type="number" class="border rounded px-2 py-1 font-mono" bind:value={failures[k].delaySec}/></label>
                  <Button size="sm" disabled={busy} on:click={() => postFailures({ [k]: failures[k] })}>Apply</Button>
                </div>
              {/each}
            </details>
          {/if}

          <!-- ────────── OIL ────────── -->
          {#if hotspotId === 'oil'}
            <details open class="border-t pt-2">
              <summary class="cursor-pointer text-xs font-bold text-gray-700">Oil failure handling</summary>
              <div class="grid grid-cols-3 gap-2 text-sm mt-2 items-end">
                <label class="flex flex-col">Mode
                  <select class="border rounded px-2 py-1" bind:value={failures.oilP.mode}>
                    {#each FAILURE_MODES as m, i}<option value={i}>{m}</option>{/each}
                  </select>
                </label>
                <label class="flex flex-col">Delay (s)<input type="number" class="border rounded px-2 py-1 font-mono" bind:value={failures.oilP.delaySec}/></label>
                <Button size="sm" disabled={busy} on:click={() => postFailures({ oilP: failures.oilP })}>Apply</Button>
              </div>
            </details>
          {/if}

          <!-- ────────── AMBIENT ────────── -->
          {#if hotspotId === 'ambient'}
            <details open class="border-t pt-2">
              <summary class="cursor-pointer text-xs font-bold text-gray-700">Low-ambient cutout</summary>
              <div class="grid grid-cols-2 gap-2 text-sm mt-2">
                <label class="flex flex-col col-span-2">Won't run below (°F)<input type="number" class="border rounded px-2 py-1 font-mono" bind:value={edits.lowAmbientCutoutF}/></label>
                <Button size="sm" class="col-span-2" disabled={busy}
                        on:click={() => patchSetpoints(patchFromKeys(['lowAmbientCutoutF']))}>Apply</Button>
              </div>
            </details>

            <details class="border-t pt-2">
              <summary class="cursor-pointer text-xs font-bold text-gray-700">Failure handling</summary>
              <div class="grid grid-cols-3 gap-2 text-sm mt-2 items-end">
                <label class="flex flex-col">Mode
                  <select class="border rounded px-2 py-1" bind:value={failures.ambientT.mode}>
                    {#each FAILURE_MODES as m, i}<option value={i}>{m}</option>{/each}
                  </select>
                </label>
                <label class="flex flex-col">Delay (s)<input type="number" class="border rounded px-2 py-1 font-mono" bind:value={failures.ambientT.delaySec}/></label>
                <Button size="sm" disabled={busy} on:click={() => postFailures({ ambientT: failures.ambientT })}>Apply</Button>
              </div>
            </details>
          {/if}

          <!-- ────────── RECEIVER / LIQUID ────────── -->
          {#if hotspotId === 'receiver' || hotspotId === 'liquid'}
            <details open class="border-t pt-2">
              <summary class="cursor-pointer text-xs font-bold text-gray-700">Liquid-line failure handling</summary>
              <div class="grid grid-cols-3 gap-2 text-sm mt-2 items-end">
                <label class="flex flex-col">Mode
                  <select class="border rounded px-2 py-1" bind:value={failures.llsT.mode}>
                    {#each FAILURE_MODES as m, i}<option value={i}>{m}</option>{/each}
                  </select>
                </label>
                <label class="flex flex-col">Delay (s)<input type="number" class="border rounded px-2 py-1 font-mono" bind:value={failures.llsT.delaySec}/></label>
                <Button size="sm" disabled={busy} on:click={() => postFailures({ llsT: failures.llsT })}>Apply</Button>
              </div>
            </details>
          {/if}

          <!-- ────────── I/O CONFIG ────────── -->
          {#if hotspotId === 'io'}
            <details open class="border-t pt-2">
              <summary class="cursor-pointer text-xs font-bold text-gray-700">Analog outputs (4)</summary>
              <table class="w-full text-sm mt-2">
                <thead class="bg-gray-50 text-xs"><tr><th class="border px-1">Ch</th><th class="border px-1">Role</th></tr></thead>
                <tbody>
                  {#each aoMode as v, i}
                    <tr>
                      <td class="border text-center">AO{i + 1}</td>
                      <td class="border">
                        <select class="w-full px-1" bind:value={aoMode[i]}>
                          {#each AO_MODES as m, j}<option value={j}>{m}</option>{/each}
                        </select>
                      </td>
                    </tr>
                  {/each}
                </tbody>
              </table>
              <Button size="sm" class="mt-2 w-full" disabled={busy}
                      on:click={() => postIoConfig({ aoMode })}>Apply AO mapping</Button>
            </details>

            <details class="border-t pt-2">
              <summary class="cursor-pointer text-xs font-bold text-gray-700">Digital outputs (10)</summary>
              <table class="w-full text-sm mt-2">
                <thead class="bg-gray-50 text-xs"><tr><th class="border px-1">Ch</th><th class="border px-1">Role</th></tr></thead>
                <tbody>
                  {#each doRole as v, i}
                    <tr>
                      <td class="border text-center">DO{i + 1}</td>
                      <td class="border">
                        <select class="w-full px-1" bind:value={doRole[i]}>
                          {#each DO_ROLES as m, j}<option value={j}>{m}</option>{/each}
                        </select>
                      </td>
                    </tr>
                  {/each}
                </tbody>
              </table>
              <Button size="sm" class="mt-2 w-full" disabled={busy}
                      on:click={() => postIoConfig({ doRole })}>Apply DO mapping</Button>
              <p class="text-xs text-gray-500 mt-1 italic">
                Number of channels role'd as COND_FAN sets the condenser-fan stage count.
              </p>
            </details>
          {/if}

          <!-- ────────── SAFETY INTERLOCKS ────────── -->
          {#if hotspotId === 'safety'}
            <div class="border-t pt-3 space-y-2">
              <p class="text-xs text-gray-500">
                Hard-wired safety inputs (DI 1-8 on the Triton orbit). Click a row
                to flip the corresponding contact in the simulator. HP Switch,
                Compressor Overload, and Run Prove latch a lockout that must be
                manually reset before the compressor can restart.
              </p>
              <ul class="text-sm space-y-1">
                {#each SAFETY_DI as s}
                  {@const live = state?.safeties?.[s.key]}
                  <li class="flex items-center justify-between border rounded px-2 py-1">
                    <span>
                      <span class="font-mono text-xs text-gray-400 mr-2">DI{s.di + 1}</span>
                      {s.label}
                    </span>
                    <span class="flex items-center gap-2">
                      <span class={live ? 'text-green-700 text-xs' : 'text-red-700 text-xs font-bold'}>
                        {live ? 'OK' : 'TRIPPED'}
                      </span>
                      <Button size="sm" disabled={busy}
                              on:click={() => toggleSafetyDi(s.di, !live)}>
                        {live ? 'Trip' : 'Restore'}
                      </Button>
                    </span>
                  </li>
                {/each}
              </ul>
              <div class="flex items-center justify-between border-t pt-2 mt-2">
                <span class="text-sm">
                  Lockout latched:
                  <span class="font-mono">
                    0x{((state?.safeties?.lockoutMask ?? 0) & 0xFF).toString(16).padStart(2, '0')}
                  </span>
                </span>
                <Button size="sm" disabled={busy || (state?.safeties?.lockoutMask ?? 0) === 0}
                        on:click={resetLockout}>
                  Reset Lockout
                </Button>
              </div>
            </div>
          {/if}

          <!-- ────────── ALARMS ────────── -->
          {#if hotspotId === 'alarms'}
            <div class="border-t pt-3 space-y-2">
              {#if state?.alarms?.length}
                <ul class="text-sm space-y-1">
                  {#each state.alarms as a}
                    <li class="flex items-center justify-between border rounded px-2 py-1">
                      <span>
                        {a.label}
                        <span class="text-xs text-gray-500 ml-1">
                          ({a.active ? 'active' : 'cleared'}{a.acked ? ', acked' : ''})
                        </span>
                      </span>
                    </li>
                  {/each}
                </ul>
                <Button size="sm" disabled={busy} on:click={ackAll}>Ack all</Button>
              {:else}
                <p class="text-gray-500 text-sm">No alarms.</p>
              {/if}
            </div>
          {/if}
        {:else if slot != null}
          <p class="text-xs text-gray-400 italic border-t pt-2">
            Read-only view (Level 2 required to edit setpoints).
          </p>
        {/if}

        {#if saveErr}
          <p class="text-sm text-red-700 border-t pt-2">⚠ {saveErr}</p>
        {:else if saveOk}
          <p class="text-sm text-green-700 border-t pt-2">✓ {saveOk}</p>
        {/if}
      </div>
    </div>
  </div>
{/if}
