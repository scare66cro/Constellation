<script lang="ts">
  // ═══════════════════════════════════════════════════════════════════
  //   /dashboard/plan3d — Isometric (2.5D) Building View  [PROTOTYPE]
  // ═══════════════════════════════════════════════════════════════════
  //   Throwaway iso experiment alongside the flat /dashboard/plan. Same
  //   data wiring (sensors live, demo fallback), reprojected into an
  //   isometric scene: extruded perimeter + door wall, two 3D potato
  //   pile mounds, boxy plenum equipment, billboarded sensor readouts.
  //
  //   NOT IN THE MENU. Delete this folder to remove. URL: /dashboard/plan3d
  // ═══════════════════════════════════════════════════════════════════
  import { onMount, onDestroy } from "svelte";
  import { goto } from "$app/navigation";
  import { systemStatus, sensorData, warningReport, plenumSettings, equipmentStatus, ioConfig, co2Settings, fanSpeedSettings, basicSetup, burnerSettings } from "$lib/business/protoStores";
  import { navigationStore, frontMatterStore, modeToColorStore, localeStore, themeStore, keysStore, keyboardStore } from "$lib/store";
  import { getHttpUrl, checkPassword } from "$lib/business/util";
  import { writeProto } from "$lib/business/protoWrite";
  import { TAG } from "$lib/business/protoTags";
  import { KeyboardTypes } from "$lib/ui/Keyboard.svelte";
  import { locale } from "svelte-i18n";
  import { INPUT_GOOD, OUTPUT_ON } from "$lib/business/mode";
  import { EQ, REMOTE, interpretEquipmentInput } from "$lib/business/equipmentEnum";
  import PlenumSetpointsForm from "$lib/components/PlenumSetpointsForm.svelte";
  import HumidifierControlForm from "$lib/components/HumidifierControlForm.svelte";
  import RefrigerationForm from "$lib/components/RefrigerationForm.svelte";
  import FanSpeedForm from "$lib/components/FanSpeedForm.svelte";
  import FanBoostForm from "$lib/components/FanBoostForm.svelte";
  import ClimacellRunClockForm from "$lib/components/ClimacellRunClockForm.svelte";
  import ClimacellConfigForm from "$lib/components/ClimacellConfigForm.svelte";
  import FreshAirDoorSettingsForm from "$lib/components/FreshAirDoorSettingsForm.svelte";
  import GdcStagesForm from "$lib/components/GdcStagesForm.svelte";
  import OutsideAirForm from "$lib/components/OutsideAirForm.svelte";
  import Co2PurgeForm from "$lib/components/Co2PurgeForm.svelte";
  import HeatForm from "$lib/components/HeatForm.svelte";
  import BurnerForm from "$lib/components/BurnerForm.svelte";
  import AnalogConfigForm from "$lib/components/AnalogConfigForm.svelte";
  import AuxiliaryForm from "$lib/components/AuxiliaryForm.svelte";
  import IoConfigForm from "$lib/components/IoConfigForm.svelte";
  import VersionForm from "$lib/components/VersionForm.svelte";
  import FanRuntimeForm from "$lib/components/FanRuntimeForm.svelte";
  import RunClockForm from "$lib/components/RunClockForm.svelte";
  import EquipmentControlForm from "$lib/components/EquipmentControlForm.svelte";
  import DateTimeForm from "$lib/components/DateTimeForm.svelte";
  import AlarmHistoryForm from "$lib/components/AlarmHistoryForm.svelte";
  import AccountsForm from "$lib/components/AccountsForm.svelte";
  import AccountActivityForm from "$lib/components/AccountActivityForm.svelte";
  import PidLogForm from "$lib/components/PidLogForm.svelte";
  import FailuresForm1 from "$lib/components/FailuresForm1.svelte";
  import FailuresForm2 from "$lib/components/FailuresForm2.svelte";
  import AlertSetupForm from "$lib/components/AlertSetupForm.svelte";
  import EmailForm from "$lib/components/EmailForm.svelte";
  import Flag from "$lib/components/Flag.svelte";

  // ─── Language switch — reuses the app's svelte-i18n + persisted localeStore.
  //   Adding a language = one row here + a matching locales/<code>.json
  //   (registered in lib/i18n.ts) + a flag branch in Flag.svelte. Flags are
  //   inline SVG (Flag.svelte), NOT flag emoji — Pi/Linux Chromium has no
  //   flag-emoji font and would render those as "US"/"CN" letterboxes.
  const LANGS = [
    { code: "en", label: "English" },
    { code: "es", label: "Español" },
    { code: "fr", label: "Français" },
    { code: "zh", label: "中文" },
  ];
  let langOpen = false;
  $: curLang = LANGS.find((l) => ($locale ?? "en").toLowerCase().startsWith(l.code)) ?? LANGS[0];
  function setLang(code: string) { localeStore.set(code); locale.set(code); langOpen = false; }

  // ─── Access level ─────────────────────────────────────────────────
  //   `navigationStore.level` (0 = Monitor/view-only, 1, 2) is now set by the
  //   real Sign In flow (checkPassword → the controller's account level).
  //   Each modal form is gated on `programLevel >= MODAL_LEVEL[key]` (see
  //   modalCanEdit). (The temp ⚙ Program cycle button was removed 2026-06-09
  //   once real login landed; level → edit is the contract Azure roles plug
  //   into.)
  $: programLevel = ($navigationStore.level ?? 0) as 0 | 1 | 2;

  // ─── Live controller data (read-only). Production: real values only —
  //   no fabricated/demo fallbacks. Missing value → "—" (see f0/f1). ──
  $: ss = $systemStatus;
  $: sd = $sensorData;
  // Live-only display: real controller value, or "—" when absent. NO
  // fabricated/demo values — plan3d is production and reads the live
  // controller exclusively.
  const f1 = (v: number | null | undefined): string =>
    Number.isFinite(v as number) ? (v as number).toFixed(1) : "—";
  const f0 = (v: number | null | undefined): string =>
    Number.isFinite(v as number) ? (v as number).toFixed(0) : "—";
  const sensorAt = (arr: any[] | undefined, i: number): number | null => {
    const r = arr?.[i];
    return (r && r.valid !== false && Number.isFinite(r.value)) ? r.value : null;
  };

  // ─── Header: clock / mode / alarms / setpoints ────────────────────
  let now = new Date();
  let clk: ReturnType<typeof setInterval> | null = null;
  onMount(() => { clk = setInterval(() => (now = new Date()), 1000); });
  onDestroy(() => { if (clk) clearInterval(clk); });
  $: clockStr = now.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" });
  $: dateStr  = now.toLocaleDateString([], { month: "short", day: "numeric" });
  // Title from Level 2 → Basic Settings (BasicSetup.storageName); fall back
  // to a generic label until the controller reports one.
  $: storageName = (($basicSetup as any)?.storageName ?? '').trim() || 'Storage';

  // Canonical mode label + color — the SAME map the production header uses
  //   ($modeToColorStore[currentMode], populated in +layout, potato/onion
  //   aware). Reads SystemStatus.current_mode (the UI mode code), NOT
  //   systemState — the earlier hand-rolled table mis-mapped them
  //   (e.g. showed "Curing" in potato/refrigeration mode).
  $: mc = $modeToColorStore?.[ss?.currentMode ?? 0] ?? { color: "#94a3b8", text: "" };
  // Some mode colours (Failure #5c1212, remote-off, shutdown 'black') are
  // dark accents meant for a light header — unreadable as text on the dark
  // dashboard chip. Brighten toward white so the mode is always legible
  // while still shifting hue per mode. Fall back to the raw mode number if
  // the label map hasn't loaded.
  function brighten(c: string, amt = 0.55): string {
    if (typeof c !== "string" || c[0] !== "#" || c.length !== 7) return "#cbd5e1";
    const ch = (i: number) => parseInt(c.slice(i, i + 2), 16);
    const mix = (v: number) => Math.round(v + (255 - v) * amt).toString(16).padStart(2, "0");
    return `#${mix(ch(1))}${mix(ch(3))}${mix(ch(5))}`;
  }
  $: mode = {
    label: mc.text || `Mode ${ss?.currentMode ?? "—"}`,
    dot: mc.color || "#94a3b8",
    color: brighten(mc.color || "#94a3b8"),
  };
  // Active alarms come from the COMPOSED, translated AlarmData on the front-
  // matter store (`['WARN_KEY=Human text', …]`, built in frontMatterComposite
  // from WarningReport — same source the classic Alarms.svelte uses), NOT the
  // raw $warningReport (which isn't shaped as entries[] and stayed empty, so
  // the old pill never went red). Each entry's human text is after the '='.
  $: alarms = ($frontMatterStore?.AlarmData as string[]) ?? [];
  $: alarmTexts = alarms
    .map((a) => (a.includes('=') ? a.split('=').slice(1).join('=') : a).trim())
    .filter(Boolean);
  $: alarmCount = alarmTexts.length;
  // Top-center indicator text: the specific alarm(s) when any are active
  // (first + "+N more"), otherwise the normal operating mode.
  $: alarmLabel = alarmCount === 0 ? ''
    : alarmCount === 1 ? alarmTexts[0]
    : `${alarmTexts[0]} +${alarmCount - 1} more`;

  // ─── Login (real auth) — reuses checkPassword + the keyboard's
  //   loginPassword mode (same path as GellertFooter's program button).
  //   On success the controller returns the account's access level, which we
  //   write to navigationStore.level (the same level the dashboard gates on).
  //   The temp ⚙ Program button stays as a dev override for now.
  let loginError = false;
  function navigateLevel(level: number) { $navigationStore.level = level; }
  function openLogin() {
    $keyboardStore.keyboardType = KeyboardTypes.Alpha;
    $keyboardStore.label = 'Sign In';
    $keyboardStore.start = '';
    $keyboardStore.inputType = 'loginPassword';
    $keyboardStore.resultReady = async (data: string) => {
      const u = (data as string).split(':');
      await checkPassword(
        u.length > 1 ? u[0] : '',
        u.length > 1 ? u[1] : u[0],
        () => {}, (e) => { loginError = e; }, async (level) => navigateLevel(level),
      );
    };
    $keyboardStore.hidden = false;
    $keyboardStore = $keyboardStore;
  }
  async function logout() {
    try {
      await fetch(getHttpUrl('/iot/logout'), { method: 'POST', headers: { 'Content-Type': 'application/json' } });
    } catch (e) { console.error('[plan3d] logout failed:', e); }
    navigateLevel(0);
  }

  // ─── System Start / Stop ──────────────────────────────────────────
  //   Software stop, same path as the classic home page Start/Stop button:
  //   POST /iot/button {tag:'button2', remoteStop:'Stop'|'Start'}. 'Stop'
  //   engages SYSTEM_STOP (firmware CurrentMode=20 / SYSTEM_REMOTEOFF), 'Start'
  //   clears it. We do NOT level-gate it — a physical E-Stop on the panel front
  //   is the hard cutoff; this on-screen control must always be reachable.
  $: stopped = Number(ss?.currentMode) === 20;
  let runBusy = false;
  async function toggleRun() {
    if (runBusy) return;
    runBusy = true;
    const next = stopped ? 'Start' : 'Stop';
    try {
      await fetch(getHttpUrl('/iot/button'), {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ tag: 'button2', remoteStop: next }),
      });
    } catch (e) { console.error('[plan3d] start/stop failed:', e); }
    // The mode flips in via the proto stream; brief debounce against double-tap.
    setTimeout(() => { runBusy = false; }, 1500);
  }

  // ─── Basic settings (distributed) — storage name, temp scale, crop type ──
  //   These three live spread across the dashboard (name on the titlebar, °F/°C
  //   by the weather, crop under ⚙ Setup → System) instead of one Basic page.
  //   The legacy shared password is GONE — remote access is Azure cloud accounts
  //   via the Accounts modal. Home-page selector / animations / multi-view: dropped.
  //
  //   ⚠ All five force-registered BasicSetup varints (tempType, systemMode,
  //   multiView, localLogin, animations) are force-emitted by writeProto whether
  //   present or not — an ABSENT one goes out as 0 and CLOBBERS the stored value
  //   (resolveForceFields, forceFieldRegistry.ts). So every partial write must
  //   carry all five at their current values; `writeBasic` does that.
  function writeBasic(patch: Record<string, unknown>) {
    const b = ($basicSetup as any) ?? {};
    return writeProto(TAG.BasicSetup, {
      tempType:   Number(b.tempType   ?? 0),
      systemMode: Number(b.systemMode ?? 0),
      multiView:  Number(b.multiView  ?? 0),
      localLogin: Number(b.localLogin ?? 0),
      animations: Number(b.animations ?? 0),
      ...patch,
    } as any);
  }

  // Temperature scale (BasicSetup.tempType: 0 = °F, 1 = °C) — toggle by weather.
  $: tempIsC = Number(($basicSetup as any)?.tempType) === 1;
  let unitBusy = false;
  async function setTempUnit(c: boolean) {
    if (unitBusy || programLevel < 2 || c === tempIsC) return;
    unitBusy = true;
    try { await writeBasic({ tempType: c ? 1 : 0 }); }
    catch (e) { console.error('[plan3d] temp unit save failed:', e); }
    setTimeout(() => { unitBusy = false; }, 800);
  }

  // Storage name — click the titlebar name to edit (on-screen keyboard). L2.
  function editName() {
    if (programLevel < 2) return;
    $keyboardStore.keyboardType = KeyboardTypes.Alpha;
    $keyboardStore.label = 'Storage Name';
    $keyboardStore.start = storageName === 'Storage' ? '' : storageName;
    $keyboardStore.inputType = 'text';
    $keyboardStore.resultReady = async (val: string) => {
      const name = String(val ?? '').trim();
      try { await writeBasic({ storageName: name }); }
      catch (e) { console.error('[plan3d] storage name save failed:', e); }
    };
    $keyboardStore.hidden = false;
    $keyboardStore = $keyboardStore;
  }

  // Crop type (BasicSetup.systemMode) — ⚙ Setup → System modal. Changing it
  // reshapes which equipment is active (onion adds burner/cure, etc.), so it's
  // a deliberate select-then-Apply, not a one-tap. Bee/Pecan are in the enum
  // ready for the cross-software merge.
  const CROPS = [
    { value: 0, label: 'Potato' },
    { value: 1, label: 'Onion' },
    { value: 2, label: 'Bee' },
    { value: 3, label: 'Pecan' },
  ];
  $: cropMode = Number(($basicSetup as any)?.systemMode ?? 0);
  let pendingCrop = 0;
  let cropBusy = false;
  async function applyCrop() {
    if (cropBusy || programLevel < 2 || pendingCrop === cropMode) return;
    cropBusy = true;
    try { await writeBasic({ systemMode: pendingCrop }); closeModal(false); }
    catch (e) { console.error('[plan3d] crop type save failed:', e); }
    cropBusy = false;
  }

  // Alarm window — opened by clicking the center indicator. Lists the active
  // alarms and offers Clear (same /iot/button {ClearAlarm} the classic
  // Alarms.svelte uses).
  let alarmOpen = false;
  let clearingAlarm = false;
  async function clearAlarms() {
    clearingAlarm = true;
    try {
      await fetch(getHttpUrl('/iot/button'), {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ tag: 'button2', ClearAlarm: 'ClearAlarm' }),
      });
    } catch (e) { console.error('[plan3d] clear alarm failed:', e); }
    finally { clearingAlarm = false; alarmOpen = false; }
  }
  $: pls = $plenumSettings;
  $: tempSP = (pls as any)?.tempSetpoint;
  $: humidSP = (pls as any)?.humidSetpoint;

  // readouts
  $: outsideTemp = ss?.outsideTemp;
  $: outsideHumid = ss?.outsideHumid;
  $: plenumT = ss?.plenumTemp;
  $: plenumH = ss?.plenumHumid;
  // Plenum-temp at-a-glance colour vs setpoint (1:1 with the classic home
  // page `plenumTempColor`): within ±0.2° = green, ±0.5° = yellow, beyond =
  // red. Light variants for the dark card; white until temp/SP load.
  $: plenumTColor = (() => {
    const v = Number(plenumT), sp = Number(tempSP);
    if (!Number.isFinite(v) || !Number.isFinite(sp)) return '#ffffff';
    const d = Math.abs(v - sp);
    return d > 0.5 ? '#fca5a5' : d > 0.2 ? '#fde047' : '#86efac';
  })();
  // Plenum-humidity colour (1:1 with classic `plenumHumidColor`): green
  // normally, red when it drops more than 4% below the humidity setpoint.
  $: plenumHColor = (() => {
    const v = Number(plenumH), sp = Number(humidSP);
    if (!Number.isFinite(v) || !Number.isFinite(sp)) return '#bae6fd';
    return v < sp - 4 ? '#fca5a5' : '#86efac';
  })();
  // CO₂ colour vs the purge setpoint: anything under the setpoint is green,
  // at/above is red. The purge setpoint (CO2 level that triggers a purge) is
  // Co2Settings.cycleOrSet, which is only a CO2 level in AUTOMATIC mode (2);
  // otherwise default green.
  $: co2SP = Number(($co2Settings as any)?.cycleOrSet);
  $: co2Mode = Number(($co2Settings as any)?.mode);
  $: co2Color = (() => {
    const v = Number(co2v);
    if (!Number.isFinite(v)) return '#94a3b8';
    if (co2Mode === 2 && Number.isFinite(co2SP) && co2SP > 0) return v >= co2SP ? '#fca5a5' : '#86efac';
    return '#86efac';
  })();
  // Return-air colour vs the fan-speed temperature differential. The fan
  // modulates to hold a `tempDiff` (default 1°) between the Plenum Setpoint
  // and Return Air Temp. Colour by how close the ACTUAL differential is to
  // that target: within 0.2° = green, within 0.4° = yellow, beyond = red.
  $: fanTempDiff = Number(($fanSpeedSettings as any)?.tempDiff);
  $: returnTColor = (() => {
    const rt = Number(returnT), sp = Number(tempSP);
    const td = (Number.isFinite(fanTempDiff) && fanTempDiff > 0) ? fanTempDiff : 1;
    if (!Number.isFinite(rt) || !Number.isFinite(sp)) return '#f8fafc';
    const err = Math.abs(Math.abs(rt - sp) - td);
    return err > 0.4 ? '#fca5a5' : err > 0.2 ? '#fde047' : '#86efac';
  })();
  $: returnT = ss?.returnTemp;
  $: returnH = ss?.returnHumid;
  $: co2v = ss?.co2Level;
  let locked = true;   // edit-lock: when on, pieces can't be dragged (starts locked)
  // Rearranging the layout is gated behind Program Level 1: below L1 the scene
  // is always locked and the Lock/Unlock button is hidden, so Monitor can't
  // drag pieces. At L1+ the operator can unlock to rearrange.
  $: effLocked = programLevel < 1 || locked;

  // ─── Equipment settings modal ─────────────────────────────────────
  //   Generic dispatcher: clicking an equipment hotspot sets `activeModal`
  //   to a key; the modal renders the matching shared *Form.svelte (the
  //   SAME component its classic page uses — real writeProto save path).
  //   Adding a migrated form = one entry in MODAL_TITLES + one hotspot +
  //   one {:else if} branch. docs/spatial-ui-page-migration.md
  type ModalKey = 'plenum' | 'humidifier' | 'refrig' | 'fan' | 'climacell' | 'door' | 'heat' | 'equipment' | 'datetime' | 'alarmhist' | 'accounts' | 'alarms' | 'alerts' | 'system' | 'analog' | 'auxiliary' | 'ioconfig' | 'version' | 'fanruntime' | 'accountactivity' | 'pidlogs';
  const MODAL_TITLES: Record<ModalKey, string> = {
    plenum: 'Plenum & Run Clock',
    humidifier: 'Humidifier Control',
    refrig: 'Refrigeration Setup',
    fan: 'Fan Control',
    climacell: 'Climacell',
    door: 'Air & Doors',
    heat: 'Heat Control',
    equipment: 'Equipment Control',
    datetime: 'Set Date & Time',
    alarmhist: 'Alarm History',
    accounts: 'User Accounts',
    accountactivity: 'Account Activity',
    pidlogs: 'PID Logging',
    alarms: 'Alarms — Failure Modes',
    alerts: 'Alerts',
    system: 'Crop Type',
    analog: 'Analog Board Setup',
    auxiliary: 'Auxiliary Output Programming',
    ioconfig: 'System I/O Configuration',
    version: 'Software Versions & Firmware Update',
    fanruntime: 'Fan Runtimes',
  };
  // No-save modals are imperative control panels (no edit buffer / flush) —
  // every action fires immediately. Their footer shows a single Close button
  // instead of Cancel / Save & Close.
  const MODAL_NOSAVE = new Set<ModalKey>(['equipment', 'alarmhist', 'accounts', 'system', 'version', 'fanruntime', 'accountactivity']);
  $: modalNoSave = !!activeModal && MODAL_NOSAVE.has(activeModal);
  // Tabbed modals: a key maps to >1 sub-form, each rendered as a tab. Single-
  // form modals just omit an entry. ONE "Save & Close" flushes EVERY tab's
  // dirty form (see closeModal). A tab may carry its OWN access `level` —
  // tabs can mix levels (e.g. the door modal groups the level-1 Outside-Air
  // control with the level-2 door/GDC settings); when omitted a tab inherits
  // MODAL_LEVEL[key]. Build new multi-page hotspots this way.
  type Tab = { id: string; label: string; level?: 1 | 2 };
  const MODAL_TABS: Partial<Record<ModalKey, Tab[]>> = {
    plenum: [
      { id: 'plenum-setpoints', label: 'Setpoints & Alarms', level: 1 },
      { id: 'plenum-runclock',  label: 'Run Clock',          level: 1 },
    ],
    fan: [
      { id: 'fan-speed', label: 'Fan Speed', level: 1 },
      { id: 'fan-boost', label: 'Fan Boost', level: 1 },
    ],
    climacell: [
      { id: 'cell-runclock', label: 'Run Clock', level: 1 },
      { id: 'cell-config',   label: 'Config',    level: 2 },
    ],
    // The Burner tab is ONLY surfaced in ONION mode (filtered into modalTabs
    // below) — burner is an onion-cure subsystem. In Potato/Bee/Pecan the heat
    // modal stays a single HeatForm (no tab strip).
    heat: [
      { id: 'heat-main',   label: 'Heat',   level: 1 },
      { id: 'heat-burner', label: 'Burner', level: 2 },
    ],
    door: [
      { id: 'door-outside', label: 'Outside Air',     level: 1 },
      { id: 'door-co2',     label: 'CO₂ Purge',       level: 1 },
      { id: 'door-fresh',   label: 'Fresh-Air Door',  level: 2 },
      { id: 'door-gdc',     label: 'GDC Stages',      level: 2 },
    ],
    alarms: [
      { id: 'alarms-f1', label: 'Failures 1', level: 2 },
      { id: 'alarms-f2', label: 'Failures 2', level: 2 },
    ],
    alerts: [
      { id: 'alerts-setup', label: 'Alert Setup',   level: 1 },
      { id: 'alerts-email', label: 'Email Server',  level: 1 },
    ],
  };
  // ── Burner availability — 1:1 port of AS2 `BurnerAvailable` (States.c:1091-
  //    1097) so the Burner tab appears exactly when AS2 showed the burner:
  //      Settings.SystemMode == SM_ONION                              (config)
  //   && SystemState ∈ {ST_AIRCURE, ST_BURNERCURE}                    (in cure)
  //   && CheckInputs(SW_BURNER_AUTO)                                  (switch on)
  //   && Settings.Burner.Mode != BURNER_OFF
  //   && Settings.RemoteOff[RO_BURNER] != 1
  //    UI mapping: onion = BasicSetup.systemMode==1; "in cure" = the UI mode
  //    code currentMode ∈ {16 air-cure, 17 burner-cure} (same map the header
  //    uses); "burner switch on" = burner RemoteOff == Auto (Nova synthesizes
  //    SW_BURNER_AUTO from RemoteOff; Auto already excludes Off=1); Burner.Mode
  //    != 0 from BurnerSettings.mode. AS2Archaeologist 2026-06-09.
  $: isOnion = Number(($basicSetup as any)?.systemMode) === 1;
  $: inCure = [16, 17].includes(Number(ss?.currentMode));
  $: burnerSwitchOn = eqRemote[EQ.BURNER] === REMOTE.AUTO
    && Number(($burnerSettings as any)?.mode) !== 0;
  $: burnerAvailable = isOnion && inCure && burnerSwitchOn;
  $: modalTabs = !activeModal ? []
    // Heat modal is tabbed (Heat · Burner) ONLY while the burner is available
    // per AS2; otherwise a single HeatForm (empty tabs → single-form path).
    : activeModal === 'heat' ? (burnerAvailable ? (MODAL_TABS.heat ?? []) : [])
    : (MODAL_TABS[activeModal] ?? []);
  // Grid-heavy forms need a wider dialog than the default 760px setpoint
  // modal. Membership here applies the `.dlg.wide` skin (≈1400px, capped at
  // 94vw). The body scrolls either way, so this only buys horizontal room.
  const MODAL_WIDE = new Set<ModalKey>(['climacell', 'plenum', 'alarmhist', 'accounts', 'alarms', 'alerts', 'analog', 'auxiliary', 'accountactivity', 'pidlogs']);  // wide tables/forms
  $: modalWide = !!activeModal && MODAL_WIDE.has(activeModal);
  // Full-bleed modals: the densest pages (IO Config) need to overtake the whole
  // screen (≈98vw × 94vh) — still a modal, no separate route. `.dlg.full`.
  const MODAL_FULL = new Set<ModalKey>(['ioconfig']);
  $: modalFull = !!activeModal && MODAL_FULL.has(activeModal);

  // Access level each modal requires to be editable: level-1 pages unlock at
  // Program L1, level-2 pages at Program L2. The form opens read-only below
  // that and editable at/above it. (Maps to the route's level1/ vs level2/
  // origin — keep in sync when migrating a new page.)
  const MODAL_LEVEL: Record<ModalKey, 1 | 2> = {
    plenum: 1, humidifier: 1, fan: 1, climacell: 1, heat: 1, equipment: 1, datetime: 1, alarmhist: 1, alerts: 1, version: 1, fanruntime: 1,
    refrig: 2, door: 2, accounts: 2, alarms: 2, system: 2, analog: 2, auxiliary: 2, ioconfig: 2,
    accountactivity: 2, pidlogs: 2,
  };
  $: modalCanEdit = !!activeModal && programLevel >= (MODAL_LEVEL[activeModal] ?? 1);
  // ─── Setup menu (the ⚙ gear) — non-equipment / system settings ────────
  //   The classic page menu is being retired, so these pages need a home on
  //   the dashboard. Data-driven launcher: each item opens an in-place modal
  //   once it's migrated to a `*Form.svelte` (set `modal`), else navigates to
  //   the classic route transitionally (`route`). As pages migrate, flip
  //   `route` → `modal`; the gear is the permanent entry point. `level` is the
  //   page's access tier (badge only — the target enforces its own edit gate).
  type SetupItem = { label: string; route?: string; modal?: ModalKey; level: 1 | 2; flag?: 'aux' };
  type SetupGroup = { title: string; items: SetupItem[] };
  const SETUP_GROUPS: SetupGroup[] = [
    { title: 'System', items: [
      { label: 'IO Config',         modal: 'ioconfig',           level: 2 },
      { label: 'Equipment Control', modal: 'equipment',          level: 1 },
      { label: 'Crop Type',         modal: 'system',             level: 2 },
      { label: 'Analog Boards',     modal: 'analog',             level: 2 },
      { label: 'Date & Time',       modal: 'datetime',           level: 1 },
      { label: 'Software Version',  modal: 'version',            level: 1 },
      { label: 'Service Info',      route: '/level1/service',    level: 1 },
    ]},
    { title: 'Program', items: [
      { label: 'Auxiliary Outputs', modal: 'auxiliary', level: 2, flag: 'aux' },
    ]},
    { title: 'Network', items: [
      { label: 'Network',     route: '/level1/network', level: 1 },
      { label: 'TCP / IP',    route: '/level2/tcpip',   level: 2 },
      { label: 'Master / Slave', route: '/level2/master', level: 2 },
      { label: 'IoT Client',  route: '/level2/iotclient', level: 2 },
    ]},
    { title: 'I/O & Sensors', items: [
      { label: 'Orbit Sensors', route: '/level2/orbit-sensors', level: 2 },
    ]},
    // 'Accounts' setup group retired 2026-06-10 — User Accounts now lives in
    // the titlebar account menu (👤 Level 2 ▾ → Account Setup), so it's no
    // longer a top-bar setup dropdown. The 'accounts' modal itself is unchanged.
  ];
  let historyOpen = false;   // History & Logs hub (migrated from the /history menu page)
  // Setup groups now live as per-group dropdown buttons on the top bar. Each
  // dropdown lists only the items the current login level grants (item.level
  // <= programLevel); a group with no accessible items is hidden. So Monitor
  // sees no setup, L1 sees level-1 items, L2 sees everything.
  let openGroup: string | null = null;
  function openSetupItem(it: SetupItem) {
    openGroup = null;
    if (it.modal) openModal(it.modal);
    else if (it.route) goto(it.route);   // transitional until migrated to a modal
  }

  let activeModal: ModalKey | null = null;
  let modalUnit: number | null = null;  // for per-unit forms (e.g. humidifier head)
  let justMoved = false;   // distinguishes a tap (open) from a drag (move)
  let modalForm: { flush: () => Promise<void> } | undefined;       // single-form modals
  let tabForms: Record<string, { flush: () => Promise<void> } | undefined> = {};  // tabbed
  let activeTab = '';      // current tab id (tabbed modals only)
  let saving = false;
  function openModal(key: ModalKey, unit: number | null = null) {
    if (justMoved) return;   // a drag just ended — don't treat as a tap
    modalUnit = unit;
    tabForms = {};
    activeTab = MODAL_TABS[key]?.[0]?.id ?? '';   // land on the first tab
    if (key === 'system') pendingCrop = cropMode;  // crop picker starts on current
    activeModal = key;
  }
  // Per-unit modal title (humidifier #1/#2/#3); static otherwise.
  $: modalTitle = !activeModal ? ''
    : activeModal === 'humidifier' && modalUnit !== null
      ? `Humidifier #${modalUnit + 1} Control`
      : MODAL_TITLES[activeModal];

  // ─── Humidifier head IO-config assignment ─────────────────────────
  //   panel[14]/[18]/[22] = the IO-config output-port assignment for
  //   HUMID_HEAD1/2/3 ('-1' when unassigned — see frontMatterComposite).
  //   A head's 3D hotspot renders only when its port is assigned, and its
  //   modal is locked to that unit.
  $: humPanel = ($frontMatterStore?.panel as string[]) ?? [];
  $: humAssigned = [humPanel[14], humPanel[18], humPanel[22]].map((p) => p !== undefined && p !== '-1');

  // ─── Humidifier running state (mist) — production logic ────────────
  //   Per AS2 CtrlHumidifier (Controls.c:1500-1516): the atomizer HEAD
  //   latches ON whenever the unit is active (runs continuously to avoid
  //   rust/wear) and the PUMP is the duty-cycled output in ALL modes
  //   (manual/timer/auto) — pump-on == active misting. So the mist tracks
  //   the PUMP output (panel[17/21/25]), gated by the head proving input
  //   (panel[15/19/23]) so an un-proven/failed unit shows no mist. The pump
  //   cycles on a seconds-scale duty in timer/auto mode, so the mist
  //   visibly pulses with it. (Don't gate on the head output — it stays on
  //   through the pump's off-half, which would make the mist look constant.)
  //     per head u: head proving in = 15+4u, head out = 16+4u, pump out = 17+4u
  function isHumRunning(panel: string[], u: number): boolean {
    return panel[15 + u * 4] === INPUT_GOOD
      && panel[17 + u * 4] === OUTPUT_ON;
  }
  $: humOn = [0, 1, 2].map((u) => isHumRunning(humPanel, u));
  // Close behaviour: anything EXCEPT Cancel autosaves. The X, the overlay
  // backdrop, and the Save button all flush the form's dirty sub-forms;
  // Cancel just closes (the form component is destroyed, discarding edits).
  async function closeModal(persist = true) {
    if (persist) {
      saving = true;
      try {
        if (modalTabs.length) {
          // ONE Save & Close flushes EVERY tab — each form's flush() is a
          // no-op when its sub-form isn't dirty, so this is safe to fan out.
          await Promise.all(Object.values(tabForms).map((f) => f?.flush()));
        } else {
          await modalForm?.flush();
        }
      } catch (e) { console.error('[plan3d] settings save failed:', e); }
      finally { saving = false; }
    }
    activeModal = null;
    modalUnit = null;
    activeTab = '';
    tabForms = {};
  }

  // "Logs" button inside the refrig / door modals → swap to the in-place PID
  // logs modal. Flush + close the current modal first (matches the classic
  // page's navigate-away autosave), then open PID logs.
  async function switchToPidLogs() {
    await closeModal(true);
    openModal('pidlogs');
  }

  // ─── Sensor health ────────────────────────────────────────────────
  type Health = "ok" | "warn" | "alarm" | "nodata";
  const pileHealth = (v: number | null | undefined): Health =>
    !Number.isFinite(v as number) ? "nodata" : ((v as number) >= 53 ? "alarm" : (v as number) >= 48 ? "warn" : "ok");
  const co2Health = (v: number | null | undefined): Health =>
    !Number.isFinite(v as number) ? "nodata" : ((v as number) >= 1500 ? "alarm" : (v as number) >= 1000 ? "warn" : "ok");
  const healthColor = (h: Health): string =>
    h === "alarm" ? "#ef4444" : h === "warn" ? "#f59e0b" : h === "nodata" ? "#64748b" : "#10b981";

  // pile probes (P1..P4 × 2 bays), live or fallback
  interface Station { id: string; bay: 1 | 2; p: 1 | 2 | 3 | 4; base: number; idx: number; }
  const stations: Station[] = [
    { id: "b1p1", bay: 1, p: 1, base: 39, idx: 0 },
    { id: "b1p2", bay: 1, p: 2, base: 41, idx: 1 },
    { id: "b1p3", bay: 1, p: 3, base: 44, idx: 2 },
    { id: "b1p4", bay: 1, p: 4, base: 48, idx: 3 },
    { id: "b2p1", bay: 2, p: 1, base: 40, idx: 4 },
    { id: "b2p2", bay: 2, p: 2, base: 42, idx: 5 },
    { id: "b2p3", bay: 2, p: 3, base: 46, idx: 6 },
    { id: "b2p4", bay: 2, p: 4, base: 54, idx: 7 },
  ];
  $: stationVal = (s: Station): number | null => sensorAt(sd?.temperatures, s.idx);

  // concept equipment state (toggles drive the iso scene)
  // Fresh-air door output — REAL value: SystemStatus.pwm_doors_pct (field 20,
  // 0..100, the PWM_DOORS % the engine drives). Same source the home page /
  // equipment page use. Read-only on the dashboard; the 3D door angle (`dth`)
  // follows it.
  $: doorsPct = Math.round(ss?.pwmDoorsPct ?? 0);

  // ─── Real equipment output state (production) ─────────────────────
  //   plan3d is production now → plenum animations reflect REAL Nova
  //   equipment state, not demo toggles. EquipmentStatus.items is a sparse
  //   list keyed by eqIndex; outputOn = the firmware-driven output coil.
  $: eqOut = (() => {
    const m: Record<number, boolean> = {};
    for (const it of ($equipmentStatus?.items ?? [])) m[it.eqIndex] = !!it.outputOn;
    return m;
  })();
  // Same, for the equipment INPUT (proving/status DI). inputOn = the input
  // contact closed (proved/running). Used to drive the fan pill border red
  // the instant the fan status input opens.
  $: eqIn = (() => {
    const m: Record<number, boolean> = {};
    for (const it of ($equipmentStatus?.items ?? [])) m[it.eqIndex] = !!it.inputOn;
    return m;
  })();
  // Per-equipment RemoteOff (Auto/Off/Manual/SysStop) — the software switch
  // state. Used to evaluate the AS2 "burner switch on" term (Auto).
  $: eqRemote = (() => {
    const m: Record<number, number> = {};
    for (const it of ($equipmentStatus?.items ?? [])) m[it.eqIndex] = Number(it.remoteOff ?? 0);
    return m;
  })();
  // Fan proving delegates to the shared interpretEquipmentInput helper — the
  // single source of truth for per-equipment input polarity — so plan3d and
  // the Equipment Control page can never disagree. For PROVING equipment that
  // helper encodes the AS2 convention (DI asserted = FAULT, healthy = DI low).
  $: fanProved = interpretEquipmentInput(EQ.FAN, !!eqIn[EQ.FAN]).healthy;
  // Same proving/health for the refrigeration + climacell readouts below the
  // equipment (both are PROVING inputs in equipmentEnum) — green = proved /
  // healthy (DI low), red = fault (DI asserted). Mirrors the fan pill.
  $: refrigProved = interpretEquipmentInput(EQ.REFRIGERATION, !!eqIn[EQ.REFRIGERATION]).healthy;
  $: climacellProved = interpretEquipmentInput(EQ.CLIMACELL, !!eqIn[EQ.CLIMACELL]).healthy;
  // FAN/REFRIG/CLIMACELL readout colour is three-state: GRAY when the output is
  // OFF (not commanded), GREEN when commanded ON and the proving input is
  // healthy, RED when commanded ON but the proving/fault input asserts. (When
  // off the proving DI reads healthy, so we must gate on the output first.)
  $: fanOn = fanPct > 0 || !!eqOut[EQ.FAN];
  const proveStroke = (on: boolean, proved: boolean): string => !on ? '#475569' : proved ? '#34d399' : '#ef4444';
  const proveText   = (on: boolean, proved: boolean): string => !on ? '#94a3b8' : proved ? '#bbf7d0' : '#fca5a5';
  // Bay HID high-bay lights (EQ.LIGHTS1/2 — CURRENT_SENSE): one ceiling fixture
  // over each bay, LIT from the CURRENT-SENSE INPUT (the lamp is actually drawing
  // current), not the commanded output. This matches AS2's "lights on" indicator
  // (status[36] = the current-sense DI, not the output coil) so the fixture
  // always agrees with the input — a commanded-but-not-sensed light shows OFF.
  $: lights1On = !!eqIn[EQ.LIGHTS1];
  $: lights2On = !!eqIn[EQ.LIGHTS2];
  $: cavityHeat = !!eqOut[EQ.CAVITY_HEAT];
  $: heatOn = !!eqOut[EQ.HEAT];
  // Climacell output coil — gates the media-wall water animation so the
  // droplets only rain while the pad is actually being wetted.
  $: climacellOn = !!eqOut[EQ.CLIMACELL];
  // Is the HEAT output actually mapped in IO Config? (Same test as
  // frontMatterComposite.isConfigured.) When unprogrammed, hide the HEAT
  // tag/hotspot entirely rather than showing a permanently-OFF banner.
  const UNASSIGNED_PORTS = new Set([0, 255, 0xffff, 0xffffffff]);
  $: heatConfigured = (() => {
    const p = ($ioConfig?.outputMap as Record<number, number> | undefined)?.[EQ.HEAT];
    return p !== undefined && !UNASSIGNED_PORTS.has(p);
  })();
  // Bay lights are gated the same way as HEAT — a fixture is only drawn when its
  // light circuit (EQ.LIGHTS1/2) is mapped to an output in IO Config.
  $: lights1Configured = (() => {
    const p = ($ioConfig?.outputMap as Record<number, number> | undefined)?.[EQ.LIGHTS1];
    return p !== undefined && !UNASSIGNED_PORTS.has(p);
  })();
  $: lights2Configured = (() => {
    const p = ($ioConfig?.outputMap as Record<number, number> | undefined)?.[EQ.LIGHTS2];
    return p !== undefined && !UNASSIGNED_PORTS.has(p);
  })();
  // Aux outputs "show up when set in IO Config" — the Auxiliary setup item only
  // appears when at least one AUX1..AUX8 output is mapped (matches the page,
  // which otherwise just says "no auxiliary output defined").
  $: anyAuxConfigured = (() => {
    const om = $ioConfig?.outputMap as Record<number, number> | undefined;
    if (!om) return false;
    for (let eq = EQ.AUX1; eq <= EQ.AUX8; eq++) {
      const p = om[eq];
      if (p !== undefined && !UNASSIGNED_PORTS.has(p)) return true;
    }
    return false;
  })();
  // Refrigeration cooling output %. Refrigeration can be delivered three
  // ways (see CtrlRefrig): discrete AS2 stages, a 4-20mA AO (PWM_REFRIG,
  // SystemStatus.pwm_refrig_pct), or a TRITON orbit. So "cooling" must check
  // the AO %, not just the discrete stage coils — otherwise an AO/TRITON
  // install (95% output, no stage coils) shows the coil dead.
  $: refrigPct = ss?.pwmRefrigPct ?? 0;
  // any defrost output → defrost; else AO output OR a cool stage / refrig
  // master → cool; else off.
  $: refrigState = (eqOut[EQ.REFRIG_DEFROST1] || eqOut[EQ.REFRIG_DEFROST2])
    ? "defrost"
    : (refrigPct > 0 || eqOut[EQ.REFRIGERATION] || eqOut[EQ.REFRIG_STAGE1]
        || eqOut[EQ.REFRIG_STAGE2] || eqOut[EQ.REFRIG_STAGE3] || eqOut[EQ.REFRIG_STAGE4])
      ? "cool" : "off";
  // Unified cooling intensity 0..100 for the coil tint. AO/TRITON installs
  // report it directly (pwm_refrig_pct); discrete-stage installs have no AO
  // %, so derive it from the fraction of active cool stages (master-only
  // with no stage info → full).
  $: coolPct = (() => {
    if (refrigPct > 0) return Math.min(100, refrigPct);
    const stages = [EQ.REFRIG_STAGE1, EQ.REFRIG_STAGE2, EQ.REFRIG_STAGE3, EQ.REFRIG_STAGE4];
    const active = stages.filter((s) => eqOut[s]).length;
    if (active > 0) return Math.round((active / stages.length) * 100);
    return eqOut[EQ.REFRIGERATION] ? 100 : 0;
  })();
  // Cool coil tint scales with output: dark blue (#1e3a8a) at full, brightening
  // toward light blue as the output drops. Defrost = red, off = grey.
  $: refrigColor = refrigState === "defrost" ? "#ef4444"
    : refrigState === "off" ? "#94a3b8"
    : brighten("#1e3a8a", (1 - coolPct / 100) * 0.75);

  // ─── Fan output → blade spin speed + readout ──────────────────────
  //   SystemStatus.fan_speed is a string ("75%" / "Manual" / "Off"). Parse
  //   a 0..100 % for the spin; faster as % rises, blades STOP at 0 (fan off).
  $: fanRaw = (ss?.fanSpeed ?? "").toString();
  $: fanPct = (() => {
    if (/off/i.test(fanRaw)) return 0;
    const m = fanRaw.match(/(\d+(?:\.\d+)?)/);
    if (m) return Math.round(parseFloat(m[1]));
    return /manual/i.test(fanRaw) ? 100 : 0;
  })();
  $: fanSpinning = fanPct > 0;
  // 100% → ~0.18 s/rev (fast blur); ~5% → ~2.5 s/rev (slow).
  $: fanSpinDur = fanSpinning ? Math.max(0.18, 2.6 - (fanPct / 100) * 2.42).toFixed(2) : "1.1";
  $: fanDisplay = fanRaw ? (/^\d+(?:\.\d+)?$/.test(fanRaw) ? fanRaw + "%" : fanRaw) : "—";

  // ═══ ISOMETRIC PROJECTION ═════════════════════════════════════════
  // World: x = length (0..L), y = depth/across (0..D), z = up.
  const A = 1.0, B = 0.26;            // iso horizontal / depth scale (very shallow tilt)
  const OX = 458, OY = 158;          // screen origin of world (0,0,0)
  const P = (x: number, y: number, z = 0): [number, number] =>
    [OX + (x - y) * A, OY + (x + y) * B - z];
  const pt = (p: [number, number]) => `${p[0].toFixed(1)},${p[1].toFixed(1)}`;
  const poly = (...ps: [number, number, number][]) => ps.map((q) => pt(P(q[0], q[1], q[2]))).join(" ");

  // Building dims
  const L = 660;                     // length (x)
  const bayD = 152, spineD = 72;
  const D = bayD * 2 + spineD;       // total depth (y)
  // Back bay (BAY1) is full height; FRONT bay (BAY2) is loaded lower so the
  // operator can see over it into the plenum aisle behind it.
  const BAY1 = { y0: 0, y1: bayD, h: 98 };
  const SPINE = { y0: bayD, y1: bayD + spineD };
  const BAY2 = { y0: bayD + spineD, y1: D, h: 52 };
  const th = 26;                     // ridge half-width (narrower crest; slopes unchanged — they still run to the bay edges)
  const rim = 20;                    // perimeter rim height (cavity envelope)
  const doorWallH = 104;             // door-end wall height (x = L end, faces camera)

  const bayCenterY = (bay: 1 | 2) => (bay === 1 ? (BAY1.y0 + BAY1.y1) / 2 : (BAY2.y0 + BAY2.y1) / 2);
  const spineCy = (SPINE.y0 + SPINE.y1) / 2;
  const finXs = Array.from({ length: 13 }, (_, i) => -34 + i * 5.7);   // refrig coil fins
  const fluteXs = Array.from({ length: 10 }, (_, i) => -33 + i * 7.3);  // climacell media flutes
  // Climacell water = droplets raining down the media. 8 columns × 2 phases
  // = continuous rain per column; index-based jitter in x / delay / speed so
  // it doesn't look like a marching grid.
  const ccDrops = Array.from({ length: 16 }, (_, i) => {
    const col = i % 8;
    return {
      id: i,
      x: -32 + col * 9.1 + ((i % 3) - 1) * 1.6,
      delay: +(((i * 0.41) % 1.7)).toFixed(2),
      dur: +(1.25 + (col % 4) * 0.18 + (i >= 8 ? 0.1 : 0)).toFixed(2),
    };
  });
  // crest/slope surface height within a bay
  function surfH(bay: { y0: number; y1: number; h: number }, y: number): number {
    const cy = (bay.y0 + bay.y1) / 2;
    const d = Math.abs(y - cy);
    if (d <= th) return bay.h;
    const span = (bay.y1 - cy) - th;
    return bay.h * Math.max(0, 1 - (d - th) / span);
  }

  // ─── Deterministic potato scatter per bay (generated once) ────────
  let seed = 1337;
  const rnd = () => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed / 0x7fffffff; };
  // A pool of irregular, oblong "potato" outlines generated once and reused
  // across every spud via <use>, so the pile reads as lumpy russets instead of
  // smooth ovals — at the SAME node count as the old ellipses. Each is a smooth
  // closed quadratic blob of ~unit radius; the per-spud transform scales/rotates
  // it and a russet gradient supplies the skin.
  function makeBlob(lobes: number, aspect: number): string {
    const pts: [number, number][] = [];
    for (let i = 0; i < lobes; i++) {
      const a = (i / lobes) * Math.PI * 2;
      const r = 0.74 + rnd() * 0.4;                 // lumpy radius
      pts.push([Math.cos(a) * r, Math.sin(a) * r * aspect]);
    }
    const mid = (p: [number, number], q: [number, number]): [number, number] =>
      [(p[0] + q[0]) / 2, (p[1] + q[1]) / 2];
    const f = (n: number) => n.toFixed(2);
    const s0 = mid(pts[lobes - 1], pts[0]);
    let d = `M${f(s0[0])},${f(s0[1])}`;
    for (let i = 0; i < lobes; i++) {
      const c = pts[i], m = mid(pts[i], pts[(i + 1) % lobes]);
      d += `Q${f(c[0])},${f(c[1])} ${f(m[0])},${f(m[1])}`;
    }
    return d + 'Z';
  }
  const SPUD_BLOBS = Array.from({ length: 16 }, (_, i) =>
    makeBlob(8 + (i % 3), 0.5 + (i % 4) * 0.08));   // varied lobe count + aspect
  // `blob` picks one of the shapes above; `rot` = screen angle; `g` = russet
  // skin gradient (1..4). One <use> per tuber — lumpy and tonally varied.
  interface Spud { x: number; y: number; z: number; r: number; g: number; depth: number; rot: number; blob: number; }
  function genSpuds(bay: { y0: number; y1: number; h: number }): Spud[] {
    const out: Spud[] = [];
    const cy = (bay.y0 + bay.y1) / 2;
    const half = bay.y1 - cy;                  // half bay depth (crest → front base)
    // Pass 1 — dome cover (crest → base). Each spud is LIFTED off the surface
    // by a "mound bump" that peaks at the crest and tapers to the base, so the
    // heap has real volume and the top doesn't foreshorten into flat pancakes.
    for (let x = 2; x < L - 1; x += 11) {
      for (let y = cy - th - 6; y < bay.y1 - 1; y += 7) {
        const jx = x + (rnd() - 0.5) * 11;
        const jy = Math.min(bay.y1 - 1, Math.max(cy - th - 8, y + (rnd() - 0.5) * 6));
        const crest = Math.max(0, 1 - Math.abs(jy - cy) / half);   // 1 at crest → 0 at base
        const bump = crest * crest * 9 + rnd() * 5;
        const z = Math.max(2, surfH(bay, jy) + bump);
        out.push({ x: jx, y: jy, z, r: 6.5 + rnd() * 5, g: 1 + Math.floor(rnd() * 4), depth: jx + jy, rot: rnd() * 360, blob: Math.floor(rnd() * SPUD_BLOBS.length) });
      }
    }
    // Pass 2 — heavy packing on the VISIBLE FRONT SLOPE + base toe (cy → front
    // base), hugging the slope surface with low clustered spuds so the brown
    // body never shows ("potatoes on dirt") and the pile reads as a solid mass
    // right down to the floor instead of a sparse single row.
    for (let x = 5; x < L - 1; x += 8) {
      for (let y = cy + th * 0.3; y < bay.y1; y += 5) {
        const jx = x + (rnd() - 0.5) * 9;
        const jy = Math.min(bay.y1 - 1, y + (rnd() - 0.5) * 4);
        const z = Math.max(2, surfH(bay, jy) + rnd() * 4);
        out.push({ x: jx, y: jy, z, r: 6 + rnd() * 4.5, g: 1 + Math.floor(rnd() * 4), depth: jx + jy, rot: rnd() * 360, blob: Math.floor(rnd() * SPUD_BLOBS.length) });
      }
    }
    return out.sort((a, b) => a.depth - b.depth);  // far → near (painter's order)
  }
  const spuds1 = genSpuds(BAY1);
  const spuds2 = genSpuds(BAY2);

  // ─── Sensor billboards (world anchor above the crest) ─────────────
  // ─── Draggable pile sensors (iso) — drag a badge to set its pile (x,y).
  //   Delta-based so there's no grab-jump; persists per-site to localStorage. ──
  let svgEl: SVGSVGElement;
  let dragPos: Record<string, { x: number; y: number }> = {};
  let dragId: string | null = null;
  let grabWorld = { x: 0, y: 0 };
  let grabSvg = { x: 0, y: 0 };
  const DRAG_KEY3 = "plan3dSensorPos";
  onMount(() => { try { const s = localStorage.getItem(DRAG_KEY3); if (s) dragPos = JSON.parse(s); } catch {} });
  function svgPoint(e: PointerEvent): { x: number; y: number } {
    const pt = svgEl.createSVGPoint(); pt.x = e.clientX; pt.y = e.clientY;
    const m = svgEl.getScreenCTM(); if (!m) return { x: 0, y: 0 };
    const p = pt.matrixTransform(m.inverse());
    return { x: p.x, y: p.y };
  }
  // Generic drag: any element by id, with a default ground (x,y).
  function startDrag(e: PointerEvent, id: string, dx: number, dy: number) {
    justMoved = false;
    if (effLocked) return;
    dragId = id; grabWorld = dragPos[id] ?? { x: dx, y: dy }; grabSvg = svgPoint(e);
    (e.target as Element).setPointerCapture?.(e.pointerId);
    e.stopPropagation(); e.preventDefault();
  }
  function onDrag(e: PointerEvent) {
    if (!dragId) return;
    justMoved = true;
    const cur = svgPoint(e);
    const dxy = (cur.x - grabSvg.x) / A;     // Δ(x − y)
    const sxy = (cur.y - grabSvg.y) / B;     // Δ(x + y) — z is constant, cancels
    // Readout cards may be placed in the FOREGROUND, past the front rim (the
    // red building-cutaway line at y=D); equipment/sensors stay in the
    // building footprint.
    const isCard = dragId === 'plenum' || dragId === 'return';
    const yMax = isCard ? D + 90 : D - 2;
    const nx = Math.max(10, Math.min(L - 10, grabWorld.x + (dxy + sxy) / 2));
    const ny = Math.max(2,  Math.min(yMax,   grabWorld.y + (sxy - dxy) / 2));
    dragPos = { ...dragPos, [dragId]: { x: nx, y: ny } };
  }
  function endDrag() {
    if (!dragId) return;
    try { localStorage.setItem(DRAG_KEY3, JSON.stringify(dragPos)); } catch {}
    dragId = null;
  }
  // Draggable readout cards (plenum + return air), projected billboards.
  $: plenumCard = dragPos['plenum'] ?? { x: 150, y: spineCy };
  $: returnCard = dragPos['return'] ?? { x: 470, y: spineCy };
  // Ground point under the HEAT flame — anchors the always-visible HEAT tag
  // (follows the flame when dragged).
  $: heatTagPt = P((dragPos['heat']?.x ?? 385), (dragPos['heat']?.y ?? spineCy), 0);
  $: plenumPt = P(plenumCard.x, plenumCard.y, 92);
  $: returnPt = P(returnCard.x, returnCard.y, 92);

  // equipment box helper: returns the 3 visible faces of a box at world
  // (x,y) footprint [w×d] standing height h.
  function boxFaces(x: number, y: number, w: number, d: number, h: number) {
    return {
      top:   poly([x, y, h], [x + w, y, h], [x + w, y + d, h], [x, y + d, h]),
      front: poly([x, y + d, h], [x + w, y + d, h], [x + w, y + d, 0], [x, y + d, 0]),
      side:  poly([x + w, y, h], [x + w, y + d, h], [x + w, y + d, 0], [x + w, y, 0]),
    };
  }

  // cavity-heat tinted colors + door lift (reactive; used in the SVG)
  $: wTop  = cavityHeat ? "#7f1d1d" : "#334155";
  $: wWall = cavityHeat ? "#5b1717" : "#1e293b";
  $: rimC  = cavityHeat ? "#b91c1c" : "#475569";

  // Small top-hinged fresh-air doors at the TOP of the right (x=L) wall,
  // clustered toward the SE corner. Hinged at the top; swing INWARD (−x).
  const doorH3 = 40, dwid = 44, dgap = 10;
  const doorSpan = 4 * dwid + 3 * dgap;
  const doorY0 = (D - doorSpan) / 2;                 // centered on the right (x=L) wall
  const doorYA = doorY0 - 6;
  const doorYB = doorY0 + doorSpan + 6;
  const doorLift = doorH3 / 2;                        // raise doors half their height above the wall top
  const doorBot = doorWallH + doorLift;
  const doorTop = doorWallH + doorH3 + doorLift;     // top-hinge line
  // Anchor for the on-graphic door readout (billboard above the door section).
  const doorTagPt = P(L, doorY0 + doorSpan / 2, doorTop + 24);
  $: dth = (doorsPct / 100) * (78 * Math.PI / 180);
  $: dXv = L - doorH3 * Math.sin(dth);              // leaf bottom x (inward, −x)
  $: dZv = doorTop - doorH3 * Math.cos(dth);        // leaf bottom z (= doorBot when closed)
</script>

<svelte:head><title>Building Plan 3D (Prototype)</title></svelte:head>

<div class="banner">
  <div><strong>PREVIEW · 3D</strong> · Isometric Building View · /dashboard/plan3d</div>
  <div class="banner-actions">
    <button on:click={() => goto('/dashboard/plan')}>▦ Flat view</button>
    <button on:click={() => goto('/')}>⌂ Home</button>
  </div>
</div>

<div class="stage" data-theme={$themeStore}>
  <!-- title bar -->
  <div class="titlebar">
    <div class="tb-left">
      <img class="tb-logo" src="/gellert-logo.png" alt="Gellert Certified Systems" draggable="false" />
      <div class="tb-id">
        <button class="tb-name" class:editable={programLevel >= 2} on:click={editName}
          title={programLevel >= 2 ? 'Rename storage' : ''}>{storageName}{#if programLevel >= 2}<span class="tb-name-pen"> ✎</span>{/if}</button>
        <button class="tb-clock tb-clock-btn" on:click={() => openModal('datetime')} title="Set controller date &amp; time">{dateStr} · {clockStr} · iso view</button>
      </div>
    </div>
    <!-- Center indicator: shows the SPECIFIC active alarm (red, pulsing) when
         any are raised, otherwise the live operating mode. Click → alarm
         window (list + Clear). -->
    <button class="tb-mode" class:alarming={alarmCount > 0} style="border-color:{alarmCount > 0 ? '#ef4444' : mode.color}"
      on:click={() => alarmOpen = true} title="View alarms">
      <span class="dot" style="background:{alarmCount > 0 ? '#ef4444' : mode.color}"></span>
      <span class="tb-mode-label" style="color:{alarmCount > 0 ? '#fecaca' : mode.color}">{alarmCount > 0 ? alarmLabel : mode.label}</span>
    </button>
    <div class="tb-right">
    <!-- language switch (top-right) — drives the app-wide locale -->
    <div class="tb-lang">
      <button class="lang-btn" on:click={() => (langOpen = !langOpen)} aria-haspopup="listbox" aria-expanded={langOpen}>
        <Flag code={curLang.code} />
        <span class="lang-name">{curLang.label}</span>
        <span class="lang-caret">▾</span>
      </button>
      {#if langOpen}
        <button class="lang-scrim" aria-label="Close language menu" on:click={() => (langOpen = false)}></button>
        <div class="lang-menu" role="listbox">
          {#each LANGS as l (l.code)}
            <button class="lang-item" class:active={l.code === curLang.code} role="option"
                    aria-selected={l.code === curLang.code} on:click={() => setLang(l.code)}>
              <Flag code={l.code} /> {l.label}
            </button>
          {/each}
        </div>
      {/if}
    </div>
    <!-- little outside-air "weather report" — live OutsideAir temp + humidity -->
    <div class="tb-wx" title="Outside air">
      <span class="wx-ico">⛅</span>
      <span class="wx-t">{f0(outsideTemp)}°</span>
      <span class="wx-h">{f0(outsideHumid)}%</span>
    </div>
    <!-- Temperature scale (BasicSetup.tempType) — °F / °C segmented toggle by
         the weather. Editable at Program L2; a read-only indicator below. -->
    <div class="tb-units" class:locked={programLevel < 2} title="Temperature units">
      <button class="unit" class:on={!tempIsC} on:click={() => setTempUnit(false)} disabled={programLevel < 2 || unitBusy}>°F</button>
      <button class="unit" class:on={tempIsC}  on:click={() => setTempUnit(true)}  disabled={programLevel < 2 || unitBusy}>°C</button>
    </div>
    <!-- sign-in (real auth via checkPassword) — right of the weather report -->
    {#if $navigationStore.level > 0}
      {#if programLevel >= 2}
        <!-- L2: the account pill is a menu (Account Setup + Sign Out), which is
             why the standalone 'Accounts' setup group was retired. Reuses the
             shared openGroup single-open state + .grp/.grp-menu pattern. -->
        <div class="grp">
          <button class="login-btn in" on:click={() => openGroup = openGroup === 'account' ? null : 'account'}
            title="Account">
            👤 Level {$navigationStore.level} ▾
          </button>
          {#if openGroup === 'account'}
            <button class="grp-scrim" aria-label="Close menu" on:click={() => openGroup = null}></button>
            <div class="grp-menu right" role="menu">
              <button class="grp-item" on:click={() => { openGroup = null; openModal('accounts'); }}>
                <span class="gi-label">Account Setup</span>
                <span class="gi-lvl l2">L2</span>
              </button>
              <button class="grp-item" on:click={() => { openGroup = null; logout(); }}>
                <span class="gi-label">Sign Out</span>
              </button>
            </div>
          {/if}
        </div>
      {:else}
        <button class="login-btn in" on:click={logout} title="Sign out">
          👤 Level {$navigationStore.level} · Sign out
        </button>
      {/if}
    {:else}
      <button class="login-btn" class:err={loginError} on:click={openLogin} title="Sign in">
        🔐 {loginError ? 'Retry sign-in' : 'Sign In'}
      </button>
    {/if}
    </div>
  </div>

  <!-- stats + a few concept controls -->
  <div class="hdr">
    <!-- Stat tiles removed 2026-06-09 (readings live in the 3D scene + weather
         pill). Top-row action buttons go here. -->
    <div class="hdr-stats">
      <button class="gear-btn" on:click={() => historyOpen = true} title="History &amp; logs">
        📜 History &amp; Logs
      </button>
      {#if programLevel >= 1}
        <button class="gear-btn" on:click={() => openModal('alerts')} title="Email alert setup">
          🔔 Alerts
        </button>
      {/if}
      {#if programLevel >= 2}
        <button class="gear-btn" on:click={() => openModal('alarms')} title="Failure-mode setup">
          🚨 Alarms
        </button>
      {/if}
    </div>
    <!-- Door readout moved onto the door graphic in the 3D scene. -->
    <!-- Cavity-heat / refrigeration / heat / humidifier status pills removed —
         those run live in the 3D scene now (wall tint, coil, flame, mist) and
         in the center mode indicator. Header keeps only the door readout +
         functional controls (lock, program). -->
    {#if programLevel >= 1}
      <button class="lock-btn" class:on={locked} on:click={() => locked = !locked}>
        {locked ? '🔒 Locked' : '🔓 Unlocked'}
      </button>
    {/if}
    <!-- Setup groups as level-gated dropdown buttons (replaces the single
         ⚙ Setup modal). A group only shows if the logged-in level can reach
         at least one of its items. -->
    {#each SETUP_GROUPS as g (g.title)}
      {@const items = g.items.filter((it) => programLevel >= it.level && (it.flag !== 'aux' || anyAuxConfigured))}
      {#if items.length}
        <div class="grp">
          <button class="gear-btn" on:click={() => openGroup = openGroup === g.title ? null : g.title}>
            {g.title} ▾
          </button>
          {#if openGroup === g.title}
            <button class="grp-scrim" aria-label="Close menu" on:click={() => openGroup = null}></button>
            <div class="grp-menu" role="menu">
              {#each items as it (it.label)}
                <button class="grp-item" on:click={() => openSetupItem(it)}>
                  <span class="gi-label">{it.label}</span>
                  <span class="gi-lvl" class:l2={it.level === 2}>L{it.level}</span>
                  {#if !it.modal}<span class="st-ext">↗</span>{/if}
                </button>
              {/each}
            </div>
          {/if}
        </div>
      {/if}
    {/each}
    <button class="gear-btn" on:click={() => themeStore.set($themeStore === 'dark' ? 'light' : 'dark')}
      title="Toggle light / dark theme">
      {$themeStore === 'dark' ? '☀ Light' : '🌙 Dark'}
    </button>
  </div>

  <svg viewBox="0 0 1200 470" class="plan" class:locked={effLocked} bind:this={svgEl} role="application" aria-label="Building plan 3D"
       style="--fan-dur:{fanSpinDur}s; --fan-play:{fanSpinning ? 'running' : 'paused'}"
       on:pointermove={onDrag} on:pointerup={endDrag} on:pointerleave={endDrag}>
    <defs>
      <!-- russet skin gradients (matte tan→russet→shadow, slightly off-vertical
           so the light reads organic; one of 4 per tuber for tonal variation) -->
      <linearGradient id="spud1" x1="0" y1="0" x2="0.25" y2="1">
        <stop offset="0%" stop-color="#dcb87e"/><stop offset="50%" stop-color="#a87a40"/><stop offset="100%" stop-color="#66421f"/>
      </linearGradient>
      <linearGradient id="spud2" x1="0" y1="0" x2="0.25" y2="1">
        <stop offset="0%" stop-color="#caa05f"/><stop offset="52%" stop-color="#925f2c"/><stop offset="100%" stop-color="#583618"/>
      </linearGradient>
      <linearGradient id="spud3" x1="0" y1="0" x2="0.25" y2="1">
        <stop offset="0%" stop-color="#d2a866"/><stop offset="50%" stop-color="#9c6a36"/><stop offset="100%" stop-color="#5e3d1d"/>
      </linearGradient>
      <linearGradient id="spud4" x1="0" y1="0" x2="0.25" y2="1">
        <stop offset="0%" stop-color="#e3c08c"/><stop offset="50%" stop-color="#b3884c"/><stop offset="100%" stop-color="#71502a"/>
      </linearGradient>
      <!-- reusable russet outlines (SPUD_BLOBS); fill/stroke come from each <use> -->
      {#each SPUD_BLOBS as bd, i (i)}<path id="sb{i}" d={bd}/>{/each}
      <!-- faint netted russet-skin grain: grayscale fractal noise, compressed to
           ~0.78..1.0 and multiplied over the spuds only. Applied once per bay to
           the spud group (static → rasterized once, then cached). -->
      <filter id="russetSkin" x="-5%" y="-5%" width="110%" height="110%">
        <feTurbulence type="fractalNoise" baseFrequency="0.55" numOctaves="2" seed="9" stitchTiles="stitch" result="n"/>
        <feColorMatrix in="n" type="saturate" values="0" result="ng"/>
        <feComponentTransfer in="ng" result="grain">
          <feFuncR type="linear" slope="0.22" intercept="0.78"/>
          <feFuncG type="linear" slope="0.22" intercept="0.78"/>
          <feFuncB type="linear" slope="0.22" intercept="0.78"/>
          <feFuncA type="linear" slope="0" intercept="1"/>
        </feComponentTransfer>
        <feComposite in="grain" in2="SourceGraphic" operator="in" result="gm"/>
        <feBlend in="SourceGraphic" in2="gm" mode="multiply"/>
      </filter>
      <!-- HID bay-light glow (halo at the bulb) + warm pool cast on the pile -->
      <radialGradient id="hidGlow">
        <stop offset="0%" stop-color="#ffffff" stop-opacity="1"/>
        <stop offset="42%" stop-color="#fff2b8" stop-opacity="0.72"/>
        <stop offset="100%" stop-color="#fde68a" stop-opacity="0"/>
      </radialGradient>
      <radialGradient id="hidPool">
        <stop offset="0%" stop-color="#fffbe6" stop-opacity="0.92"/>
        <stop offset="38%" stop-color="#ffe7a0" stop-opacity="0.62"/>
        <stop offset="100%" stop-color="#fde68a" stop-opacity="0"/>
      </radialGradient>
      <filter id="soft3" x="-30%" y="-30%" width="160%" height="160%">
        <feDropShadow dx="0" dy="3" stdDeviation="4" flood-color="#020617" flood-opacity="0.45"/>
      </filter>
      <filter id="mist3" x="-80%" y="-80%" width="260%" height="260%">
        <feGaussianBlur stdDeviation="3"/>
      </filter>
    </defs>

    <!-- ground shadow -->
    <polygon points={poly([-30,-30,0],[L+30,-30,0],[L+30,D+30,0],[-30,D+30,0])} fill="#020617" opacity="0.35"/>

    <!-- ═══ FLOOR ═══ -->
    <polygon points={poly([0,0,0],[L,0,0],[L,D,0],[0,D,0])} fill="#1e293b" stroke="#0f172a" stroke-width="1"/>

    <!-- ═══ BACK WALLS (far x=0 and far y=0), with cavity-heat tint ═══ -->
    <!-- back-left wall (x=0 face) -->
    <polygon points={poly([0,0,doorWallH],[0,D,doorWallH],[0,D,0],[0,0,0])} fill={wWall} stroke="#0f172a" stroke-width="1"/>
    <!-- back-right wall (y=0 face) -->
    <polygon points={poly([0,0,doorWallH],[L,0,doorWallH],[L,0,0],[0,0,0])} fill={wWall} opacity="0.92" stroke="#0f172a" stroke-width="1"/>
    <!-- wall tops -->
    <polygon points={poly([0,0,doorWallH],[0,D,doorWallH],[-4,D,doorWallH],[-4,0,doorWallH])} fill={wTop}/>
    <polygon points={poly([0,0,doorWallH],[L,0,doorWallH],[L,-4,doorWallH],[0,-4,doorWallH])} fill={wTop}/>

    <!-- ═══ PILE MOUNDS (drawn far bay first) ═══ -->
    {#each [{bay:BAY1, spuds:spuds1}, {bay:BAY2, spuds:spuds2}] as M (M.bay.y0)}
      {@const cy = (M.bay.y0 + M.bay.y1) / 2}
      <!-- ridge-loaf body so no floor shows through -->
      <polygon points={poly([10,cy+th,M.bay.h],[L,cy+th,M.bay.h],[L,M.bay.y1,0],[10,M.bay.y1,0])} fill="#48300f"/>
      <polygon points={poly([L,cy-th,M.bay.h],[L,cy+th,M.bay.h],[L,M.bay.y1,0],[L,M.bay.y0,0])} fill="#3d2810"/>
      <polygon points={poly([10,cy-th,M.bay.h],[L,cy-th,M.bay.h],[L,cy+th,M.bay.h],[10,cy+th,M.bay.h])} fill="#7e5a32"/>
      <!-- scattered russet potatoes; the russetSkin filter adds a faint netted
           grain (one filtered group per bay — rasterized once, then cached) -->
      <g filter="url(#russetSkin)">
        {#each M.spuds as s (s.x + '_' + s.y)}
          {@const c = P(s.x, s.y, s.z)}
          <use href="#sb{s.blob}"
               transform="translate({c[0].toFixed(1)} {c[1].toFixed(1)}) rotate({s.rot.toFixed(0)}) scale({s.r.toFixed(2)})"
               fill="url(#spud{s.g})" stroke="#3f2810" stroke-width={(0.5 / s.r).toFixed(3)}/>
        {/each}
      </g>
    {/each}

    <!-- ═══ PERIMETER RIM (low cavity-heat envelope on front edges) ═══ -->
    <polygon points={poly([0,D,rim],[L,D,rim],[L,D,0],[0,D,0])} fill={rimC} opacity="0.85"/>
    <polygon points={poly([L,0,rim],[L,D,rim],[L,D,0],[L,0,0])} fill={rimC} opacity="0.7"/>

    <!-- ═══ PLENUM SPINE (sunken channel between bays) + equipment ═══ -->
    <polygon points={poly([0,SPINE.y0,2],[L,SPINE.y0,2],[L,SPINE.y1,2],[0,SPINE.y1,2])} fill="#cdd9e6" opacity="0.9"/>
    <!-- equipment standing UPRIGHT in the plenum aisle, facing the operator
         (drawn far→near). No boxes — each unit's detail faces the camera. -->
    {#each [
      {x:454, type:'cell'},
      {x:522, type:'fan'},
      {x:590, type:'refrig'}
    ] as eqb (eqb.type)}
      {@const ep = dragPos['eq-' + eqb.type] ?? { x: eqb.x, y: spineCy }}
      {@const base = P(ep.x, ep.y, 0)}
      <ellipse cx={base[0]} cy={base[1]} rx="18" ry="8" fill="#0f172a" opacity="0.5"/>
      <!-- shear matrix: panel stands as a wall across the plenum; draggable. -->
      <g transform="matrix({-A} {B} 0 1 {base[0]} {base[1]})" class="drag3"
         on:pointerdown={(e) => startDrag(e, 'eq-' + eqb.type, eqb.x, spineCy)}
         on:click={() => {
           if (eqb.type === 'refrig') openModal('refrig');
           else if (eqb.type === 'fan') openModal('fan');
           else if (eqb.type === 'cell') openModal('climacell');
         }} role="button" tabindex="0">
        {#if eqb.type === 'fan'}
          <!-- galvanized fan wall: round fans in square cells + solid lower panel -->
          <rect x="-38" y="-90" width="76" height="84" rx="2" fill="#aeb7c2" stroke="#5b6573" stroke-width="2"/>
          <rect x="-38" y="-90" width="4" height="84" fill="#79828f"/>
          <rect x="34"  y="-90" width="4" height="84" fill="#79828f"/>
          {#each [-64, -26] as fcy (fcy)}
            {#each [-24, 0, 24] as cx (cx)}
              <rect x={cx - 12} y={fcy - 20} width="24" height="40" fill="#cdd4dc" stroke="#79828f" stroke-width="1"/>
              <g transform="translate({cx},{fcy})">
                <circle r="11" fill="#140d06"/>
                <g class="fan3-b">
                  {#each [0,60,120,180,240,300] as a (a)}<ellipse cx="0" cy="-7" rx="2.5" ry="7" fill="#b8924f" transform="rotate({a})"/>{/each}
                  <circle r="2.5" fill="#0f172a"/>
                </g>
                <circle r="11" fill="none" stroke="#79828f" stroke-width="1"/>
              </g>
            {/each}
          {/each}
        {:else if eqb.type === 'refrig'}
          <!-- finned coil bank: silver aluminum fins + a connected serpentine
               copper refrigerant tube weaving through (blue cool / red defrost) -->
          <rect x="-38" y="-90" width="76" height="84" rx="2" fill="#7c8794" stroke="#5b6573" stroke-width="2"/>
          {#each finXs as fx (fx)}
            <line x1={fx} y1="-86" x2={fx} y2="-12" stroke="#bcc4ce" stroke-width="1.5" opacity="0.85"/>
          {/each}
          <path d="M-34,-80 H34 A8 8 0 0 1 34,-64 H-34 A8 8 0 0 0 -34,-48 H34 A8 8 0 0 1 34,-32 H-34 A8 8 0 0 0 -34,-16 H34"
                fill="none" stroke={refrigColor} stroke-width="3.6" stroke-linecap="round" stroke-linejoin="round"
                class:rf-pulse={refrigState === 'defrost'} opacity={refrigState === 'off' ? 0.5 : 1}/>
        {:else}
          <!-- climacell evaporative media wall (dark corrugated) with water -->
          <rect x="-38" y="-90" width="76" height="84" rx="2" fill="#352718" stroke="#5b6573" stroke-width="2"/>
          {#each fluteXs as fx (fx)}
            <line x1={fx} y1="-85" x2={fx} y2="-9" stroke="#1d140c" stroke-width="1.6" opacity="0.85"/>
          {/each}
          {#each [-70, -52, -34, -16] as hy (hy)}
            <line x1="-36" y1={hy} x2="36" y2={hy} stroke="#241a0f" stroke-width="1.2" opacity="0.7"/>
          {/each}
          <rect x="-38" y="-90" width="76" height="8" fill="#4a5663"/>
          {#if climacellOn}
            {#each ccDrops as drop (drop.id)}
              <ellipse class="cc-drop" cx={drop.x} cy="-80" rx="1.5" ry="2.7" fill="#7dd3fc"
                       style="animation-delay:{drop.delay}s; animation-duration:{drop.dur}s"/>
            {/each}
          {/if}
        {/if}
      </g>
      {#if eqb.type === 'refrig'}
        <!-- live cooling-output readout UNDER the coil (drawn outside the
             sheared wall group so the text stays upright). % = coolPct. -->
        <g transform="translate({base[0]},{base[1] + 24})" class="door-tag">
          <rect x="-44" y="-16" width="88" height="30" rx="7" fill="#0b1220"
                stroke={proveStroke(refrigState !== 'off', refrigProved)} stroke-width="1.5" filter="url(#soft3)"/>
          <text x="0" y="-4" text-anchor="middle" class="dtag-k">REFRIGERATION</text>
          <text x="0" y="9"  text-anchor="middle" class="dtag-v"
                fill={proveText(refrigState !== 'off', refrigProved)}>
            {refrigState === 'defrost' ? 'DEFROST' : coolPct + '%'}
          </text>
        </g>
      {/if}
      {#if eqb.type === 'fan'}
        <!-- live fan-speed readout UNDER the fan wall (drawn outside the
             sheared wall group so the text stays upright). -->
        <g transform="translate({base[0]},{base[1] + 24})" class="door-tag">
          <rect x="-32" y="-16" width="64" height="30" rx="7" fill="#0b1220"
                stroke={proveStroke(fanOn, fanProved)} stroke-width="1.5" filter="url(#soft3)"/>
          <text x="0" y="-4" text-anchor="middle" class="dtag-k">FAN</text>
          <text x="0" y="9"  text-anchor="middle" class="dtag-v"
                fill={proveText(fanOn, fanProved)}>{fanDisplay}</text>
        </g>
      {/if}
      {#if eqb.type === 'cell'}
        <!-- climacell media wall → opens its run-clock schedule (48-slot AM/PM
             grid) in a WIDE modal. Tag is the visible affordance. -->
        <g transform="translate({base[0]},{base[1] + 24})" class="door-tag link-tag"
           on:click|stopPropagation={() => openModal('climacell')}
           role="button" tabindex="0">
          <rect x="-34" y="-16" width="68" height="30" rx="7" fill="#0b1220"
                stroke={proveStroke(climacellOn, climacellProved)} stroke-width="1.5" filter="url(#soft3)"/>
          <text x="0" y="-4" text-anchor="middle" class="dtag-k">CLIMACELL</text>
          <text x="0" y="9"  text-anchor="middle" class="dtag-v"
                fill={proveText(climacellOn, climacellProved)}>{climacellOn ? 'ON' : 'OFF'}</text>
        </g>
      {/if}
    {/each}

    <!-- ═══ FRESH-AIR DOORS — top-hinged, swing inward, at the TOP of the
         right (x=L) wall on the SE corner (rest of that wall is cut low). ═══ -->
    <!-- door header band so the doors read as a wall section up top -->
    <polygon points={poly([L,doorYA,doorTop+6],[L,doorYB,doorTop+6],[L,doorYB,doorBot-4],[L,doorYA,doorBot-4])}
             fill={cavityHeat ? '#5b1717' : '#33414f'} stroke="#1b232e" stroke-width="1"/>
    {#each [0, 1, 2, 3] as i (i)}
      {@const y0 = doorY0 + i * (dwid + dgap)}
      {@const y1 = y0 + dwid}
      <!-- opening -->
      <polygon points={poly([L,y0,doorTop],[L,y1,doorTop],[L,y1,doorBot],[L,y0,doorBot])}
               fill="#0b1220" opacity="0.7"/>
      <!-- leaf, hinged at the top edge, swung inward (−x) and up -->
      <polygon points={poly([L,y0,doorTop],[L,y1,doorTop],[dXv,y1,dZv],[dXv,y0,dZv])}
               fill="#cbd5e1" stroke="#475569" stroke-width="0.8" opacity="0.92"/>
    {/each}
    <!-- live door readout ON the graphic (PWM_DOORS %); click → door modal -->
    <g transform="translate({doorTagPt[0]},{doorTagPt[1]})" class="door-tag link-tag"
       on:click|stopPropagation={() => openModal('door')} role="button" tabindex="0">
      <rect x="-30" y="-16" width="60" height="30" rx="7" fill="#0b1220"
            stroke={doorsPct > 0 ? '#60a5fa' : '#475569'} stroke-width="1.5" filter="url(#soft3)"/>
      <text x="0" y="-4" text-anchor="middle" class="dtag-k">DOORS</text>
      <text x="0" y="9"  text-anchor="middle" class="dtag-v"
            fill={doorsPct > 0 ? '#bfdbfe' : '#94a3b8'}>{doorsPct}%</text>
    </g>

    <!-- ═══ HUMIDIFIERS + 3D mist on the plenum ═══
         Order along the plenum (climacell media wall is at x=454, to the
         RIGHT — screenX = OX + (x−y)·A, no x-flip): head #1 sits closest to
         the climacell (highest x=315), then #2 (220) and #3 (125) to the
         left. Each unit's x is fixed, so an unassigned head leaves its slot
         empty rather than shifting the others. -->
    {#each [{x:315,u:0},{x:220,u:1},{x:125,u:2}] as h (h.u)}
      {#if humAssigned[h.u]}
      {@const hp2 = dragPos['hum-' + h.x] ?? { x: h.x, y: spineCy }}
      {@const base = P(hp2.x, hp2.y, 0)}
      <ellipse cx={base[0]} cy={base[1]} rx="13" ry="6" fill="#0f172a" opacity="0.45"/>
      <!-- aligned along the plenum; draggable. one hotspot per ASSIGNED head;
           click opens that head's own control modal. -->
      <g transform="matrix({A} {B} 0 1 {base[0]} {base[1]})" class="drag3"
         on:pointerdown={(e) => startDrag(e, 'hum-' + h.x, h.x, spineCy)}
         on:click={() => openModal('humidifier', h.u)} role="button" tabindex="0">
        <!-- louvered sump cabinet (base) -->
        <rect x="-11" y="-17" width="22" height="17" rx="2" fill="#46566a" stroke="#1b232e" stroke-width="1"/>
        {#each [-7, -2, 3, 8] as lv (lv)}
          <line x1={lv} y1="-15" x2={lv} y2="-2" stroke="#1b232e" stroke-width="0.8" opacity="0.5"/>
        {/each}
        <!-- yoke + motor (down-plenum side) + shaft -->
        <rect x="-1.5" y="-32" width="3" height="16" fill="#3a4658"/>
        <rect x="6" y="-44" width="11" height="13" rx="2" fill="#33414f" stroke="#1b232e" stroke-width="0.8"/>
        <line x1="1" y1="-37" x2="7" y2="-37" stroke="#64748b" stroke-width="2"/>
        <!-- big atomizer disc (output end, toward the door) with rings/hub/cage -->
        <circle cx="-11" cy="-38" r="14" fill="#5b6b80" stroke="#2b3543" stroke-width="1.5"/>
        <circle cx="-11" cy="-38" r="9"  fill="none" stroke="#8896a8" stroke-width="1"/>
        <circle cx="-11" cy="-38" r="4"  fill="#1b232e"/>
        {#each [0, 60, 120] as a (a)}
          <line x1="-11" y1="-38" x2="-11" y2="-52" stroke="#7c8a9c" stroke-width="1" transform="rotate({a} -11 -38)"/>
        {/each}
        <!-- mist off the disc, toward the door — only when this head is
             actually running per production logic (humOn[h.u]). -->
        {#if humOn[h.u]}
          {#each [0, 1, 2] as k (k)}
            <g class="mistP" style="animation-delay:{k * 1.0}s" filter="url(#mist3)">
              <circle cx="-20" cy="-40" r="6"   fill="#8ba2bd"/>
              <circle cx="-16" cy="-33" r="7"   fill="#8ba2bd"/>
              <circle cx="-25" cy="-36" r="6.5" fill="#8ba2bd"/>
            </g>
          {/each}
        {/if}
      </g>
      {/if}
    {/each}

    <!-- ═══ HEAT flame on the plenum (shows only while the HEAT output is on) ═══ -->
    {#if heatOn}
      {@const fp = P((dragPos['heat']?.x ?? 385), (dragPos['heat']?.y ?? spineCy), 16)}
      <g transform="translate({fp[0]},{fp[1]})" class="drag3"
         on:pointerdown={(e) => startDrag(e, 'heat', 385, spineCy)} role="button" tabindex="0">
        <g class="heat3">
          <path class="flame3 f1" d="M0,0 C-9,-10 -6,-22 0,-30 C6,-22 9,-10 0,0 Z" fill="#ea580c"/>
          <path class="flame3 f2" d="M0,0 C-6,-7 -4,-15 0,-21 C4,-15 6,-7 0,0 Z" fill="#fb923c"/>
          <path class="flame3 f3" d="M0,0 C-3,-4 -2,-9 0,-13 C3,-9 3,-4 0,0 Z" fill="#fde68a"/>
        </g>
      </g>
    {/if}
    <!-- HEAT status tag + hotspot — only when a HEAT output is mapped in IO
         Config (no permanently-OFF banner for unprogrammed heat). Glows orange
         while heating → click opens the Heat Control modal. -->
    {#if heatConfigured}
      <g transform="translate({heatTagPt[0]},{heatTagPt[1] + 24})" class="door-tag link-tag"
         on:click|stopPropagation={() => openModal('heat')} role="button" tabindex="0">
        <rect x="-32" y="-16" width="64" height="30" rx="7" fill="#0b1220"
              stroke={heatOn ? '#f97316' : '#475569'} stroke-width="1.5" filter="url(#soft3)"/>
        <text x="0" y="-4" text-anchor="middle" class="dtag-k">HEAT</text>
        <text x="0" y="9"  text-anchor="middle" class="dtag-v"
              fill={heatOn ? '#fed7aa' : '#94a3b8'}>{heatOn ? 'ON' : 'OFF'}</text>
      </g>
    {/if}

    <!-- ═══ CAVITY-HEAT red flow around the envelope (gated) ═══ -->
    <polyline points={poly([0,0,doorWallH],[L,0,doorWallH],[L,D,rim],[0,D,rim],[0,0,doorWallH])}
              fill="none" stroke="#ef4444" stroke-width="3" stroke-linejoin="round"
              stroke-dasharray="11 17" class="cav-flow" class:on={cavityHeat}/>

    <!-- ═══ PILE FANS at the far end (x≈0) of each bay ═══ -->
    {#each [BAY1, BAY2] as bay, bi (bay.y0)}
      {@const fwy = (bay.y0 + bay.y1) / 2}
      {@const fp2 = dragPos['fan-' + bi] ?? { x: 26, y: fwy }}
      {@const fsurf = surfH(bay, Math.max(bay.y0, Math.min(bay.y1, fp2.y)))}
      {@const fc = P(fp2.x, fp2.y, fsurf + 24)}
      {@const fb = P(fp2.x, fp2.y, fsurf)}
      <ellipse cx={fb[0]} cy={fb[1]} rx="17" ry="7" fill="#0f172a" opacity="0.45"/>
      <g transform="translate({fc[0]},{fc[1]})" class="fan3 drag3"
         on:pointerdown={(e) => startDrag(e, 'fan-' + bi, 26, fwy)} role="button" tabindex="0">
        <!-- mount leg down to the pile -->
        <rect x="-3" y="10" width="6" height="16" rx="1.5" fill="#475569" stroke="#1b232e" stroke-width="0.6"/>
        <!-- round housing / shroud -->
        <circle r="18" fill="#2f3c4b" stroke="#1b232e" stroke-width="2"/>
        <circle r="14.5" fill="#0b1220"/>
        <!-- spinning rotor -->
        <g class="fan3-b">
          {#each [0,60,120,180,240,300] as a (a)}<ellipse cx="0" cy="-9" rx="3.4" ry="9" fill="#64748b" transform="rotate({a})"/>{/each}
          <circle r="3.6" fill="#1e293b"/>
        </g>
        <!-- guard ring + struts -->
        <circle r="14.5" fill="none" stroke="#475569" stroke-width="1" opacity="0.5"/>
        {#each [0, 90] as a (a)}
          <line x1="-14.5" y1="0" x2="14.5" y2="0" stroke="#334155" stroke-width="1" opacity="0.55" transform="rotate({a})"/>
        {/each}
      </g>
    {/each}

    <!-- ═══ BAY HID HIGH-BAY LIGHTS (EQ.LIGHTS1/2) — one ceiling fixture per bay ═══
         Hung at ceiling height (z=130) over each bay; warm glow + a light pool on
         the pile when lit, dark grey when off. Only drawn when the light is mapped
         in IO Config. DRAGGABLE when unlocked (dragPos['light-N'], persisted) and a
         TAP opens Equipment Control (openModal guards justMoved so a drag won't
         also open it). Front bay (2) draws last so it sits in front. -->
    {#each [{ bay: BAY1, n: 1, on: lights1On, cfg: lights1Configured }, { bay: BAY2, n: 2, on: lights2On, cfg: lights2Configured }] as BL (BL.n)}
      {#if BL.cfg}
        {@const defY = (BL.bay.y0 + BL.bay.y1) / 2}
        {@const dl = dragPos['light-' + BL.n]}
        {@const lx = dl ? dl.x : L / 2}
        {@const ly = dl ? dl.y : defY}
        {@const lc = P(lx, ly, 130)}
        {@const pool = P(lx, ly, surfH(BL.bay, Math.max(BL.bay.y0, Math.min(BL.bay.y1, ly))))}
        {#if BL.on}
          <!-- light shaft fanning down + bright warm pool on the pile (non-interactive) -->
          <polygon points="{(lc[0]-5).toFixed(1)},{(lc[1]+2).toFixed(1)} {(lc[0]+5).toFixed(1)},{(lc[1]+2).toFixed(1)} {(pool[0]+54).toFixed(1)},{pool[1].toFixed(1)} {(pool[0]-54).toFixed(1)},{pool[1].toFixed(1)}"
                   fill="#fde68a" opacity="0.18" pointer-events="none"/>
          <ellipse cx={pool[0]} cy={pool[1]} rx="54" ry="15" fill="url(#hidPool)" pointer-events="none"/>
          <ellipse cx={pool[0]} cy={pool[1]} rx="26" ry="7" fill="#fff7d6" opacity="0.42" pointer-events="none"/>
        {/if}
        <!-- fixture: suspension rod + bell reflector + HID bulb + bay number -->
        <g transform="translate({lc[0]},{lc[1]})" class="drag3" role="button" tabindex="0"
           on:pointerdown={(e) => startDrag(e, 'light-' + BL.n, L / 2, defY)}
           on:click={() => openModal('equipment')}>
          <!-- enlarged transparent hit area for touch -->
          <rect x="-16" y="-26" width="32" height="38" fill="transparent"/>
          <!-- suspension rod + mount eyelet -->
          <line x1="0" y1="-23" x2="0" y2="-13" stroke="#475569" stroke-width="1.5"/>
          <circle cx="0" cy="-23" r="2.2" fill="none" stroke="#64748b" stroke-width="1.3"/>
          <!-- finned aluminium heat-sink body (industrial high-bay) -->
          <path d="M -12 -13 L 12 -13 L 9 -5 L -9 -5 Z" fill="#39434f" stroke="#1b232e" stroke-width="1.1"/>
          <line x1="-10.5" y1="-11" x2="10.5" y2="-11" stroke="#1b232e" stroke-width="0.8" opacity="0.7" pointer-events="none"/>
          <line x1="-10" y1="-9" x2="10" y2="-9" stroke="#1b232e" stroke-width="0.8" opacity="0.7" pointer-events="none"/>
          <line x1="-9.5" y1="-7" x2="9.5" y2="-7" stroke="#1b232e" stroke-width="0.8" opacity="0.7" pointer-events="none"/>
          <!-- flared reflector skirt (warms when lit) -->
          <path d="M -9 -5 L 9 -5 L 13 2 L -13 2 Z" fill={BL.on ? '#5a5230' : '#2b3540'} stroke="#1b232e" stroke-width="1.1"/>
          {#if BL.on}<ellipse cx="0" cy="2" rx="20" ry="8" fill="url(#hidGlow)" pointer-events="none"/>{/if}
          <!-- lit lens at the mouth -->
          <ellipse cx="0" cy="2" rx="12" ry="3.2" fill={BL.on ? '#fff7d6' : '#0b1220'}
                   stroke={BL.on ? '#fde68a' : '#334155'} stroke-width="1"/>
          <!-- wire guard across the lens -->
          <line x1="-12.5" y1="2" x2="12.5" y2="2" stroke={BL.on ? '#b9962f' : '#475569'} stroke-width="0.8" opacity="0.8" pointer-events="none"/>
          <path d="M -8 -4.5 Q 0 1.5 8 -4.5" fill="none" stroke={BL.on ? '#b9962f' : '#475569'} stroke-width="0.8" opacity="0.55" pointer-events="none"/>
          <!-- bay number (to the side) -->
          <text x="17" y="1" text-anchor="start" font-size="9" font-weight="800"
                fill={BL.on ? '#fde68a' : '#64748b'} pointer-events="none">{BL.n}</text>
        </g>
      {/if}
    {/each}

    <!-- ═══ SENSOR BILLBOARDS — drag to place on the pile ═══ -->
    {#each stations as s (s.id)}
      {@const d = dragPos[s.id]}
      {@const wx = d ? d.x : 72 + (s.p - 1) * (L - 150) / 3}
      {@const wy = d ? d.y : bayCenterY(s.bay)}
      {@const bay = s.bay === 1 ? BAY1 : BAY2}
      {@const wsurf = surfH(bay, Math.max(bay.y0, Math.min(bay.y1, wy)))}
      {@const base = P(wx, wy, wsurf)}
      {@const top  = P(wx, wy, wsurf + 14)}
      {@const v = stationVal(s)}
      <!-- Pile-sensor status border: GREEN by default (most piles aren't fully
           sensored, so a disabled / not-deployed sensor reads "--" and stays
           green — no false alarm). RED only when a sensor IS reading (enabled)
           but the value is OUT OF SCALE — i.e. it errors but isn't disabled.
           (`v` is null for a "--"/invalid reading.) -->
      {@const pileErr = v != null && (v < -40 || v > 200)}
      <line x1={base[0]} y1={base[1]} x2={top[0]} y2={top[1]} stroke="#0f172a" stroke-width="1.5" opacity="0.6"/>
      <circle cx={base[0]} cy={base[1]} r="3" fill={pileErr ? '#ef4444' : '#22c55e'}/>
      <g transform="translate({top[0]},{top[1]})" class="pbadge3" class:dragging={dragId === s.id}
         on:pointerdown={(e) => startDrag(e, s.id, 72 + (s.p - 1) * (L - 150) / 3, bayCenterY(s.bay))} role="button" tabindex="0">
        <rect x="-26" y="-26" width="52" height="28" rx="7" fill="#0b1220" filter="url(#soft3)"/>
        <rect x="-26" y="-26" width="52" height="28" rx="7" fill="none" stroke={pileErr ? '#ef4444' : '#22c55e'} stroke-width={pileErr ? 2.5 : 1.5} class:b-alarm={pileErr}/>
        <text x="0" y="-14" text-anchor="middle" class="s-lbl">P{s.p} · B{s.bay}</text>
        <text x="0" y="-2" text-anchor="middle" class="s-val" style={pileErr ? 'fill:#fecaca' : ''}>{f1(v)}°</text>
      </g>
    {/each}

    <!-- ═══ PLENUM readout card (draggable; click/tap opens setpoints) ═══ -->
    <g transform="translate({plenumPt[0]},{plenumPt[1]})" class="drag3"
       on:pointerdown={(e) => startDrag(e, 'plenum', 150, spineCy)}
       on:click={() => openModal('plenum')} role="button" tabindex="0">
      <rect x="-40" y="-32" width="80" height="64" rx="8" fill="#000000" stroke={plenumTColor} stroke-width="2.5" filter="url(#soft3)"/>
      <text x="0" y="-21" text-anchor="middle" class="card-hd">PLENUM ⚙</text>
      <text x="0" y="-6" text-anchor="middle" class="card-big" style="fill:{plenumTColor}">{f1(plenumT)}°</text>
      <text x="0" y="4"  text-anchor="middle" class="card-sp">SP {f1(tempSP)}°</text>
      <text x="0" y="16" text-anchor="middle" class="card-sm" style="fill:{plenumHColor}">{f0(plenumH)}% RH</text>
      <text x="0" y="26" text-anchor="middle" class="card-sp">SP {f0(humidSP)}%</text>
    </g>

    <!-- ═══ RETURN AIR readout card (draggable) ═══ -->
    <g transform="translate({returnPt[0]},{returnPt[1]})" class="drag3"
       on:pointerdown={(e) => startDrag(e, 'return', 470, spineCy)} role="button" tabindex="0">
      <rect x="-42" y="-34" width="84" height="60" rx="8" fill="#0b1220" stroke="#38bdf8" stroke-width="1.5" filter="url(#soft3)"/>
      <text x="-34" y="-20" class="card-hd2">RETURN AIR</text>
      <text x="-34" y="-5"  class="card-k">Temp</text><text x="36" y="-5" text-anchor="end" class="card-v" style="fill:{returnTColor}">{f1(returnT)}°</text>
      <text x="-34" y="8"   class="card-k">RH</text><text x="36" y="8" text-anchor="end" class="card-v">{f0(returnH)}%</text>
      <text x="-34" y="21"  class="card-k">CO₂</text><text x="36" y="21" text-anchor="end" class="card-v" style="fill:{co2Color}">{f0(co2v)}</text>
    </g>
  </svg>

  <div class="legend">
    <span><span class="sw" style="background:#10b981"></span>ok</span>
    <span><span class="sw" style="background:#f59e0b"></span>warn</span>
    <span><span class="sw" style="background:#ef4444"></span>alarm</span>
    <span class="hint">click the PLENUM card → setpoints · isometric prototype</span>
  </div>

  <!-- System Start/Stop — fixed bottom-right. Software stop (NOT level-gated;
       the panel-front E-Stop is the hard cutoff). Red ■ Stop while running,
       green ▶ Start when stopped (CurrentMode=20). -->
  <button class="run-btn" class:stopped on:click={toggleRun} disabled={runBusy}
    title={stopped ? 'Start the system' : 'Stop the system'}>
    {stopped ? '▶ Start' : '■ Stop'}
  </button>

  <!-- ═══ EQUIPMENT SETTINGS POPUP — shared *Form.svelte (real save path) ═══
       Anything but Cancel autosaves: overlay / X / Save → flush, Cancel → discard. -->
  {#if activeModal}
    <div class="ovl" role="presentation" on:click={() => closeModal(true)}>
      <div class="dlg" class:wide={modalWide} class:full={modalFull} role="dialog" aria-modal="true" on:click|stopPropagation on:keydown|stopPropagation>
        <header class="dlg-hd">
          <span>{modalTitle}</span>
          <button class="dlg-x" title="Save & close" on:click={() => closeModal(true)}>×</button>
        </header>
        {#if modalTabs.length}
          <div class="modal-tabs">
            {#each modalTabs as tab (tab.id)}
              <button class="mtab" class:active={activeTab === tab.id} on:click={() => activeTab = tab.id}>{tab.label}</button>
            {/each}
          </div>
        {/if}
        <div class="dlg-body">
          {#if modalTabs.length}
            <!-- All tabs stay MOUNTED (hidden, not destroyed) so edits survive
                 a tab switch and one Save & Close flushes every dirty form. -->
            {#each modalTabs as tab (tab.id)}
              {@const tabCanEdit = programLevel >= (tab.level ?? MODAL_LEVEL[activeModal] ?? 1)}
              <div class="tab-panel" class:hidden={activeTab !== tab.id}>
                {#if tab.id === 'plenum-setpoints'}
                  <PlenumSetpointsForm bind:this={tabForms['plenum-setpoints']} embedded theme={$themeStore} canEdit={tabCanEdit} />
                {:else if tab.id === 'plenum-runclock'}
                  <RunClockForm bind:this={tabForms['plenum-runclock']} embedded theme={$themeStore} canEdit={tabCanEdit} />
                {:else if tab.id === 'door-outside'}
                  <OutsideAirForm bind:this={tabForms['door-outside']} embedded theme={$themeStore} canEdit={tabCanEdit} />
                {:else if tab.id === 'door-co2'}
                  <Co2PurgeForm bind:this={tabForms['door-co2']} embedded theme={$themeStore} canEdit={tabCanEdit} />
                {:else if tab.id === 'door-fresh'}
                  <FreshAirDoorSettingsForm bind:this={tabForms['door-fresh']} embedded theme={$themeStore} canEdit={tabCanEdit} on:viewlogs={switchToPidLogs} />
                {:else if tab.id === 'door-gdc'}
                  <GdcStagesForm bind:this={tabForms['door-gdc']} embedded theme={$themeStore} canEdit={tabCanEdit} />
                {:else if tab.id === 'alarms-f1'}
                  <FailuresForm1 bind:this={tabForms['alarms-f1']} embedded theme={$themeStore} canEdit={tabCanEdit} />
                {:else if tab.id === 'alarms-f2'}
                  <FailuresForm2 bind:this={tabForms['alarms-f2']} embedded theme={$themeStore} canEdit={tabCanEdit} />
                {:else if tab.id === 'alerts-setup'}
                  <AlertSetupForm bind:this={tabForms['alerts-setup']} embedded theme={$themeStore} canEdit={tabCanEdit} />
                {:else if tab.id === 'alerts-email'}
                  <EmailForm bind:this={tabForms['alerts-email']} embedded theme={$themeStore} canEdit={tabCanEdit} />
                {:else if tab.id === 'fan-speed'}
                  <FanSpeedForm bind:this={tabForms['fan-speed']} embedded theme={$themeStore} canEdit={tabCanEdit} />
                {:else if tab.id === 'fan-boost'}
                  <FanBoostForm bind:this={tabForms['fan-boost']} embedded theme={$themeStore} canEdit={tabCanEdit} />
                {:else if tab.id === 'cell-runclock'}
                  <ClimacellRunClockForm bind:this={tabForms['cell-runclock']} embedded theme={$themeStore} canEdit={tabCanEdit} />
                {:else if tab.id === 'cell-config'}
                  <ClimacellConfigForm bind:this={tabForms['cell-config']} embedded theme={$themeStore} canEdit={tabCanEdit} />
                {:else if tab.id === 'heat-main'}
                  <HeatForm bind:this={tabForms['heat-main']} embedded theme={$themeStore} canEdit={tabCanEdit} />
                {:else if tab.id === 'heat-burner'}
                  <BurnerForm bind:this={tabForms['heat-burner']} embedded theme={$themeStore} canEdit={tabCanEdit} />
                {/if}
              </div>
            {/each}
          {:else if activeModal === 'humidifier'}
            <HumidifierControlForm bind:this={modalForm} embedded theme={$themeStore} canEdit={modalCanEdit} unit={modalUnit} />
          {:else if activeModal === 'refrig'}
            <RefrigerationForm bind:this={modalForm} embedded theme={$themeStore} canEdit={modalCanEdit} on:viewlogs={switchToPidLogs} />
          {:else if activeModal === 'heat'}
            <HeatForm bind:this={modalForm} embedded theme={$themeStore} canEdit={modalCanEdit} />
          {:else if activeModal === 'equipment'}
            <EquipmentControlForm bind:this={modalForm} embedded theme={$themeStore} canEdit={modalCanEdit} />
          {:else if activeModal === 'datetime'}
            <DateTimeForm bind:this={modalForm} embedded theme={$themeStore} canEdit={modalCanEdit} />
          {:else if activeModal === 'alarmhist'}
            <AlarmHistoryForm bind:this={modalForm} embedded theme={$themeStore} />
          {:else if activeModal === 'accounts'}
            <AccountsForm bind:this={modalForm} embedded theme={$themeStore} canEdit={modalCanEdit} />
          {:else if activeModal === 'accountactivity'}
            <AccountActivityForm bind:this={modalForm} embedded theme={$themeStore} />
          {:else if activeModal === 'pidlogs'}
            <PidLogForm bind:this={modalForm} embedded theme={$themeStore} />
          {:else if activeModal === 'analog'}
            <AnalogConfigForm bind:this={modalForm} embedded theme={$themeStore} canEdit={modalCanEdit} />
          {:else if activeModal === 'auxiliary'}
            <AuxiliaryForm bind:this={modalForm} embedded theme={$themeStore} canEdit={modalCanEdit} />
          {:else if activeModal === 'ioconfig'}
            <IoConfigForm bind:this={modalForm} embedded theme={$themeStore} canEdit={modalCanEdit} />
          {:else if activeModal === 'version'}
            <VersionForm bind:this={modalForm} embedded theme={$themeStore} canEdit={modalCanEdit} />
          {:else if activeModal === 'fanruntime'}
            <FanRuntimeForm bind:this={modalForm} embedded theme={$themeStore} canEdit={modalCanEdit} />
          {:else if activeModal === 'system'}
            <div class="pform pform--{$themeStore}">
              <div class="crop-pick">
                <p class="crop-warn">⚠ Crop type sets which equipment + control logic are active
                  (onion enables burner &amp; cure, etc.). Change it only when reconfiguring the
                  system — not during a run.</p>
                <div class="crop-grid">
                  {#each CROPS as c (c.value)}
                    <button class="crop-opt" class:current={cropMode === c.value}
                      class:pending={pendingCrop === c.value && pendingCrop !== cropMode}
                      disabled={programLevel < 2}
                      on:click={() => pendingCrop = c.value}>
                      {c.label}{#if cropMode === c.value} <span class="crop-now">(current)</span>{/if}
                    </button>
                  {/each}
                </div>
                <div class="crop-apply">
                  <button class="btn save" on:click={applyCrop} disabled={programLevel < 2 || pendingCrop === cropMode || cropBusy}>
                    {cropBusy ? 'Applying…' : pendingCrop === cropMode ? 'No change' : `Apply ${CROPS[pendingCrop]?.label ?? ''}`}
                  </button>
                </div>
                {#if programLevel < 2}<p class="crop-note">Sign in at Program Level 2 to change crop type.</p>{/if}
              </div>
            </div>
          {/if}
        </div>
        <footer class="dlg-ft">
          {#if modalNoSave}
            <span class="ft-tag">live control — changes apply immediately</span>
            <button class="btn save" on:click={() => closeModal(false)}>Close</button>
          {:else}
            <span class="ft-tag">edits autosave on close — Cancel to discard</span>
            <button class="btn ghost" on:click={() => closeModal(false)} disabled={saving}>Cancel</button>
            <button class="btn save" on:click={() => closeModal(true)} disabled={saving}>
              {saving ? 'Saving…' : 'Save & Close'}
            </button>
          {/if}
        </footer>
      </div>
    </div>
  {/if}

  <!-- ═══ ALARM WINDOW — opened by clicking the center indicator ═══ -->
  {#if alarmOpen}
    <div class="ovl" role="presentation" on:click={() => alarmOpen = false}>
      <div class="dlg" role="dialog" aria-modal="true" on:click|stopPropagation on:keydown|stopPropagation>
        <header class="dlg-hd" class:alarm-hd={alarmCount > 0}>
          <span>{alarmCount > 0 ? `Active Alarms (${alarmCount})` : 'Alarms'}</span>
          <button class="dlg-x" title="Close" on:click={() => alarmOpen = false}>×</button>
        </header>
        <div class="dlg-body">
          {#if alarmCount === 0}
            <p class="alarm-none">✓ No active alarms.</p>
          {:else}
            <ul class="alarm-list">
              {#each alarmTexts as a (a)}
                <li><span class="alarm-dot"></span>{a}</li>
              {/each}
            </ul>
          {/if}
        </div>
        <footer class="dlg-ft">
          <span class="ft-tag">live from the controller warning report</span>
          <button class="btn ghost" on:click={() => { alarmOpen = false; openModal('alarmhist'); }}>View history →</button>
          <button class="btn ghost" on:click={() => alarmOpen = false}>Close</button>
          {#if alarmCount > 0}
            <button class="btn save" on:click={clearAlarms} disabled={clearingAlarm}>
              {clearingAlarm ? 'Clearing…' : 'Clear Alarms'}
            </button>
          {/if}
        </footer>
      </div>
    </div>
  {/if}

  <!-- ═══ HISTORY & LOGS hub — migrated from the /history menu page ═══
       Alarm History + Active Alarms open in-place; the heavy data-download
       log viewers (Activity / User) route to their pages (↗) until Pi5-side
       logging is built. -->
  {#if historyOpen}
    <div class="ovl" role="presentation" on:click={() => historyOpen = false}>
      <div class="dlg" role="dialog" aria-modal="true" on:click|stopPropagation on:keydown|stopPropagation>
        <header class="dlg-hd">
          <span>📜 History &amp; Logs</span>
          <button class="dlg-x" title="Close" on:click={() => historyOpen = false}>×</button>
        </header>
        <div class="setup-body">
          <div class="setup-tiles">
            <div class="st-head">History</div>
            <button class="setup-tile" on:click={() => { historyOpen = false; openModal('alarmhist'); }}>
              <span class="st-label">🔔 Alarm History</span>
            </button>
            <button class="setup-tile" on:click={() => { historyOpen = false; alarmOpen = true; }}>
              <span class="st-label">⚠ Active Alarms</span>
            </button>
            <button class="setup-tile" on:click={() => { historyOpen = false; openModal('fanruntime'); }}>
              <span class="st-label">🌀 Fan Runtimes</span>
            </button>
          </div>
          <div class="setup-tiles">
            <div class="st-head">Logs</div>
            {#if programLevel >= 2}
              <button class="setup-tile" on:click={() => { historyOpen = false; openModal('accountactivity'); }}>
                <span class="st-label">👥 Account Activity</span>
              </button>
            {/if}
            <button class="setup-tile" on:click={() => goto('/history/activitylog')}>
              <span class="st-label">📋 Activity Log</span><span class="st-ext">↗</span>
            </button>
            <button class="setup-tile" on:click={() => goto('/history/userlog')}>
              <span class="st-label">👤 User Log</span><span class="st-ext">↗</span>
            </button>
          </div>
          {#if programLevel >= 2}
            <div class="setup-tiles">
              <div class="st-head">PID</div>
              <button class="setup-tile" on:click={() => { historyOpen = false; openModal('pidlogs'); }}>
                <span class="st-label">🎛 PID Logs</span>
              </button>
            </div>
          {/if}
        </div>
        <footer class="dlg-ft">
          <span class="ft-tag">↗ opens the full data viewer (page) · log data pending Pi5 logging</span>
          <button class="btn save" on:click={() => historyOpen = false}>Close</button>
        </footer>
      </div>
    </div>
  {/if}
</div>

<style>
  .banner {
    display:flex; justify-content:space-between; align-items:center;
    background:#fff7ed; border-bottom:2px solid #fb923c; color:#7c2d12;
    font-size:13px; padding:4px 12px;
  }
  .banner-actions button { background:#fff; border:1px solid #fdba74; color:#9a3412; border-radius:6px; padding:2px 10px; margin-left:8px; font-size:12px; cursor:pointer; }
  .stage { background:linear-gradient(160deg,#0b1220 0%,#1e293b 100%); min-height:calc(100vh - 30px); padding:12px 18px 20px; color:#e2e8f0; }

  .titlebar { position:relative; display:flex; align-items:center; justify-content:space-between; gap:18px; background:#0b1220; border:1px solid #1e293b; border-radius:12px; padding:14px 24px; margin-bottom:12px; }
  .tb-left { display:flex; align-items:center; gap:18px; min-width:0; }
  .tb-logo { height:60px; width:auto; display:block; background:#fff; border-radius:8px; padding:6px 12px; box-shadow:0 1px 3px rgba(0,0,0,0.35); user-select:none; }
  .tb-id { display:flex; flex-direction:column; line-height:1.15; }
  .tb-name { font-size:30px; font-weight:800; color:#f1f5f9; background:none; border:none; padding:0; margin:0; font-family:inherit; text-align:left; cursor:default; line-height:1.05; }
  .tb-name.editable { cursor:pointer; }
  .tb-name.editable:hover { color:#7dd3fc; }
  .tb-name-pen { font-size:16px; color:#64748b; font-weight:400; vertical-align:middle; }
  .tb-name.editable:hover .tb-name-pen { color:#7dd3fc; }
  .tb-clock { font-size:15px; color:#94a3b8; font-variant-numeric:tabular-nums; }
  /* temperature scale segmented toggle (by the weather pill) */
  .tb-units { display:flex; border:1px solid #334155; border-radius:999px; overflow:hidden; background:#0b1220; }
  .tb-units .unit { background:none; border:none; color:#64748b; padding:8px 14px; font-size:16px; font-weight:800; cursor:pointer; font-family:inherit; }
  .tb-units .unit.on { background:#1e3a5f; color:#bae6fd; }
  .tb-units.locked .unit { cursor:default; }
  .tb-units .unit:disabled { cursor:default; }
  /* crop-type picker (⚙ Setup → System) */
  .crop-pick { padding:6px 4px; max-width:560px; }
  .crop-warn { font-size:15px; line-height:1.5; color:#fcd34d; background:rgba(245,158,11,0.1); border-left:3px solid #f59e0b; border-radius:6px; padding:10px 12px; margin:0 0 14px 0; }
  .crop-grid { display:grid; grid-template-columns:1fr 1fr; gap:12px; }
  .crop-opt { background:#0f1827; border:2px solid #334155; color:#e2e8f0; border-radius:10px; padding:18px; font-size:20px; font-weight:800; cursor:pointer; font-family:inherit; }
  .crop-opt:hover:not(:disabled) { border-color:#475569; }
  .crop-opt.current { border-color:#10b981; }
  .crop-opt.pending { border-color:#38bdf8; background:#13263b; }
  .crop-opt:disabled { opacity:.55; cursor:default; }
  .crop-now { font-size:13px; font-weight:600; color:#86efac; }
  .crop-apply { display:flex; justify-content:flex-end; margin-top:16px; }
  .crop-note { text-align:right; font-size:13px; color:#94a3b8; margin:8px 0 0; }
  .tb-clock-btn { background:none; border:none; padding:0; text-align:left; cursor:pointer; }
  .tb-clock-btn:hover { color:#7dd3fc; }
  /* center mode indicator — the color-changing centerpiece (border + dot +
     label all driven by the live mode colour). Absolutely centred so the
     left id-block and right health-pill don't shift it. */
  .tb-mode { position:absolute; left:50%; top:50%; transform:translate(-50%,-50%);
    display:flex; align-items:center; gap:13px; padding:12px 36px; border-radius:999px;
    background:#0b1220; border:2px solid #334155; font-weight:800; white-space:nowrap;
    cursor:pointer; font-family:inherit; }
  .alarm-hd { background:#7f1d1d !important; }
  .alarm-none { text-align:center; padding:18px; color:#86efac; font-size:16px; font-weight:700; }
  .alarm-list { list-style:none; margin:0; padding:6px 4px; display:flex; flex-direction:column; gap:8px; }
  .alarm-list li { display:flex; align-items:center; gap:10px; padding:10px 14px; background:#3a0d0d; border:1px solid #b91c1c; border-radius:9px; color:#fecaca; font-size:15px; font-weight:600; }
  .alarm-dot { width:10px; height:10px; border-radius:50%; background:#ef4444; box-shadow:0 0 8px #ef4444; flex:0 0 auto; }
  .tb-mode .dot { width:18px; height:18px; }
  .tb-mode-label { font-size:27px; letter-spacing:.02em; text-transform:uppercase; }
  .dot { width:10px; height:10px; border-radius:50%; box-shadow:0 0 8px currentColor; }
  .tb-right { display:flex; align-items:center; gap:14px; }
  /* System Start/Stop — fixed bottom-right floating control (ungated). */
  .run-btn { position:fixed; right:24px; bottom:22px; z-index:30;
    background:#dc2626; border:2px solid #f87171; color:#fff; border-radius:999px;
    padding:14px 30px; font-size:20px; font-weight:800; letter-spacing:.03em;
    cursor:pointer; white-space:nowrap; box-shadow:0 6px 20px rgba(0,0,0,0.45);
    font-family:inherit; transition:filter .12s; }
  .run-btn:hover { filter:brightness(1.08); }
  .run-btn:active { transform:translateY(1px); }
  .run-btn.stopped { background:#16a34a; border-color:#4ade80; }
  .run-btn:disabled { opacity:.6; cursor:default; }
  /* outside-air weather report (right of the language switcher) */
  .tb-wx { display:flex; align-items:center; gap:9px; background:#0b1220; border:1px solid #334155; border-radius:999px; padding:10px 20px; font-weight:700; white-space:nowrap; }
  .tb-wx .wx-ico { font-size:19px; }
  .tb-wx .wx-t { color:#fde68a; font-size:19px; }
  .tb-wx .wx-h { color:#7dd3fc; font-size:17px; }
  .login-btn { background:#0b1220; border:1px solid #334155; color:#cbd5e1; border-radius:999px; padding:11px 22px; font-size:17px; font-weight:700; cursor:pointer; white-space:nowrap; }
  .login-btn:hover { border-color:#7dd3fc; color:#e0f2fe; }
  .login-btn.in { border-color:#10b981; color:#bbf7d0; background:#06281d; }
  .login-btn.err { border-color:#ef4444; color:#fecaca; background:#3a0d0d; }
  .tb-lang { position:relative; }
  .lang-btn { display:flex; align-items:center; gap:8px; background:#0b1220; border:1px solid #334155; color:#cbd5e1; border-radius:999px; padding:11px 18px; font-size:17px; font-weight:600; cursor:pointer; }
  .lang-btn:hover { border-color:#475569; }
  /* flag chips (inline SVG via Flag.svelte — see lang-btn / lang-item) */
  .lang-caret { font-size:10px; color:#64748b; }
  .lang-scrim { position:fixed; inset:0; background:transparent; border:none; z-index:40; cursor:default; }
  .lang-menu { position:absolute; right:0; top:calc(100% + 6px); z-index:41; background:#0f1827; border:1px solid #334155; border-radius:10px; padding:4px; min-width:148px; box-shadow:0 10px 30px rgba(0,0,0,0.5); }
  .lang-item { display:flex; align-items:center; gap:8px; width:100%; text-align:left; background:none; border:none; color:#cbd5e1; border-radius:7px; padding:8px 10px; font-size:14px; cursor:pointer; }
  .lang-item:hover { background:#1e293b; }
  .lang-item.active { color:#7dd3fc; font-weight:700; }
  /* top-bar setup-group dropdowns */
  .grp { position:relative; display:inline-block; }
  .grp-scrim { position:fixed; inset:0; background:transparent; border:none; z-index:40; cursor:default; }
  .grp-menu { position:absolute; left:0; top:calc(100% + 6px); z-index:41; background:#0f1827; border:1px solid #334155; border-radius:10px; padding:6px; min-width:250px; box-shadow:0 10px 30px rgba(0,0,0,0.5); }
  /* right-aligned variant for the titlebar account pill (sits on the right edge) */
  .grp-menu.right { left:auto; right:0; }
  .grp-item { display:flex; align-items:center; gap:9px; width:100%; text-align:left; background:none; border:none; color:#cbd5e1; border-radius:7px; padding:12px 14px; font-size:17px; font-weight:600; cursor:pointer; }
  .grp-item:hover { background:#1e293b; }
  .gi-label { flex:1 1 auto; }
  .gi-lvl { font-size:11px; font-weight:800; color:#fde68a; border:1px solid #b45309; border-radius:6px; padding:1px 6px; }
  .gi-lvl.l2 { color:#bbf7d0; border-color:#15803d; }
  /* center indicator pulses red while an alarm is active */
  .tb-mode.alarming { animation:pulse 1.1s ease-in-out infinite; }

  .hdr { display:flex; align-items:center; gap:18px; margin-bottom:12px; flex-wrap:wrap; }
  .hdr-stats { display:flex; gap:14px; }
  .stat { display:flex; align-items:baseline; gap:5px; background:#0b1220; border:1px solid #334155; border-radius:8px; padding:5px 12px; }
  .stat .k { font-size:11px; color:#94a3b8; text-transform:uppercase; letter-spacing:.05em; }
  .stat .v { font-size:18px; font-weight:700; color:#f8fafc; }
  .stat .u { font-size:11px; color:#64748b; }
  /* on-graphic door readout billboard */
  :global(.plan .door-tag .dtag-k) { font-size:8px; fill:#94a3b8; font-weight:700; letter-spacing:.04em; }
  :global(.plan .door-tag .dtag-v) { font-size:13px; font-weight:800; }
  :global(.plan .link-tag) { cursor:pointer; }
  :global(.plan .link-tag:hover rect) { stroke:#7dd3fc; }
  .cavity-toggle, .refrig-btn { display:flex; align-items:center; gap:7px; background:#0b1220; border:1px solid #334155; color:#cbd5e1; border-radius:999px; padding:6px 14px; font-size:13px; font-weight:600; cursor:pointer; }
  .cavity-toggle.status, .refrig-btn.status { cursor:default; }
  .cavity-toggle.on { border-color:#ef4444; color:#fecaca; }
  .refrig-btn[data-st="cool"] { border-color:#3b82f6; color:#bfdbfe; }
  .refrig-btn[data-st="defrost"] { border-color:#ef4444; color:#fecaca; }
  .cdot { width:10px; height:10px; border-radius:50%; background:#475569; }

  .plan { width:100%; height:auto; max-height:76vh; background:#0b1220; border:1px solid #1e293b; border-radius:12px; }
  :global(.plan .eqlbl) { font-size:10px; fill:#f8fafc; font-weight:700; }
  :global(.plan .s-lbl) { font-size:8px; fill:#94a3b8; font-weight:600; }
  :global(.plan .s-val) { font-size:12px; fill:#f8fafc; font-weight:800; }
  :global(.plan .b-alarm) { animation:pulse 1s ease-in-out infinite; }
  :global(.plan .pbadge3) { cursor: grab; touch-action: none; }
  :global(.plan .pbadge3.dragging) { cursor: grabbing; }
  :global(.plan .drag3) { cursor: grab; touch-action: none; }
  :global(.plan .drag3:active) { cursor: grabbing; }
  :global(.plan.locked .drag3), :global(.plan.locked .pbadge3) { cursor: default; }
  .lock-btn { background:#0b1220; border:1px solid #334155; color:#cbd5e1; border-radius:999px; padding:11px 22px; font-size:17px; font-weight:600; cursor:pointer; }
  .lock-btn.on { border-color:#f59e0b; color:#fde68a; background:#3a2a07; }
  .gear-btn { background:#0b1220; border:1px solid #334155; color:#cbd5e1; border-radius:999px; padding:11px 22px; font-size:17px; font-weight:600; cursor:pointer; }
  .gear-btn:hover { border-color:#7dd3fc; color:#e0f2fe; }
  /* History hub modal body (reuses .ovl + .dlg) */
  .setup-body { flex:1 1 auto; overflow:auto; padding:14px 18px; display:grid; grid-template-columns:repeat(auto-fit, minmax(200px, 1fr)); gap:16px 22px; align-items:start; }
  .setup-tiles { display:flex; flex-direction:column; gap:7px; }
  .st-head { font-size:11px; font-weight:800; letter-spacing:0.08em; text-transform:uppercase; color:#64748b; padding:0 2px 5px; border-bottom:1px solid #334155; margin-bottom:2px; }
  .setup-tile { display:flex; align-items:center; gap:9px; background:#0b1220; border:1px solid #334155; border-radius:9px; padding:9px 13px; color:#e2e8f0; font-size:14px; font-weight:600; cursor:pointer; text-align:left; }
  .setup-tile:hover { border-color:#38bdf8; background:#0c2233; }
  .st-label { flex:1 1 auto; }
  .st-ext { color:#64748b; font-size:13px; }

  /* ═══ LIGHT THEME — chrome only ═══════════════════════════════════════
     The .plan 3D canvas (and its SVG scene, text, and readout tags) stays
     DARK in both themes — a dark viewport inside a light app — so none of
     the scene colours need a light variant. Toggled by data-theme on .stage
     (see themeStore). :global so the override isn't pruned. */
  :global(.stage[data-theme="light"]) { background:linear-gradient(160deg,#eef2f7 0%,#d7e0ea 100%); color:#1e293b; }
  :global([data-theme="light"] .titlebar) { background:#ffffff; border-color:#cbd5e1; }
  :global([data-theme="light"] .tb-name)  { color:#0f172a; }
  :global([data-theme="light"] .tb-clock) { color:#64748b; }
  :global([data-theme="light"] .tb-clock-btn:hover) { color:#0284c7; }
  :global([data-theme="light"] .tb-mode)  { background:#ffffff; }
  :global([data-theme="light"] .lock-btn),
  :global([data-theme="light"] .gear-btn),
  :global([data-theme="light"] .lang-btn),
  :global([data-theme="light"] .stat) { background:#ffffff; border-color:#cbd5e1; color:#334155; }
  :global([data-theme="light"] .stat .v) { color:#0f172a; }
  :global([data-theme="light"] .tb-wx) { background:#ffffff; border-color:#cbd5e1; }
  :global([data-theme="light"] .tb-wx .wx-t) { color:#b45309; }
  :global([data-theme="light"] .tb-wx .wx-h) { color:#0369a1; }
  :global([data-theme="light"] .login-btn) { background:#ffffff; border-color:#cbd5e1; color:#334155; }
  :global([data-theme="light"] .login-btn.in) { background:#dcfce7; border-color:#22c55e; color:#15803d; }
  :global([data-theme="light"] .login-btn.err) { background:#fee2e2; border-color:#ef4444; color:#b91c1c; }
  :global([data-theme="light"] .stat .k), :global([data-theme="light"] .lang-caret) { color:#64748b; }
  /* keep the lock status tint legible on a light pill */
  :global([data-theme="light"] .lock-btn.on) { background:#fef3c7; border-color:#f59e0b; color:#b45309; }
  :global([data-theme="light"] .lang-menu) { background:#ffffff; border-color:#cbd5e1; }
  :global([data-theme="light"] .lang-item) { color:#334155; }
  :global([data-theme="light"] .lang-item:hover) { background:#e2e8f0; }
  :global([data-theme="light"] .lang-item.active) { color:#0284c7; }
  :global([data-theme="light"] .grp-menu) { background:#ffffff; border-color:#cbd5e1; }
  :global([data-theme="light"] .grp-item) { color:#334155; }
  :global([data-theme="light"] .grp-item:hover) { background:#e2e8f0; }
  :global([data-theme="light"] .legend) { color:#475569; }
  /* modals — dlg header band + tab strip keep the teal accent in both themes */
  :global([data-theme="light"] .dlg) { background:#ffffff; border-color:#cbd5e1; }
  :global([data-theme="light"] .dlg-body) { background:#ffffff; }
  :global([data-theme="light"] .dlg-ft) { background:#f1f5f9; border-color:#e2e8f0; }
  :global([data-theme="light"] .ft-tag) { color:#64748b; }
  :global([data-theme="light"] .mtab) { background:#e2e8f0; color:#475569; border-color:#cbd5e1; }
  :global([data-theme="light"] .mtab.active) { background:#ffffff; color:#0c4a6e; }
  :global([data-theme="light"] .setup-tile) { background:#f8fafc; border-color:#cbd5e1; color:#1e293b; }
  :global([data-theme="light"] .st-head) { border-color:#e2e8f0; }
  :global([data-theme="light"] .setup-tile:hover) { background:#e0f2fe; border-color:#38bdf8; }
  :global(.plan .card-hd)  { font-size:8px;  fill:#7dd3fc; font-weight:800; letter-spacing:.12em; }
  :global(.plan .card-big) { font-size:15px; fill:#ffffff; font-weight:800; }
  :global(.plan .card-sm)  { font-size:9px;  fill:#bae6fd; font-weight:600; }
  :global(.plan .card-sp)  { font-size:8px;  fill:#7dd3fc; font-weight:700; letter-spacing:.04em; }
  :global(.plan .card-hd2) { font-size:8px;  fill:#7dd3fc; font-weight:800; letter-spacing:.12em; }
  :global(.plan .card-k)   { font-size:9px;  fill:#94a3b8; }
  :global(.plan .card-v)   { font-size:11px; fill:#f8fafc; font-weight:700; }
  /* fan blades — spin speed driven by fan output % via --fan-dur; paused at 0 */
  :global(.plan .fan3-b) { transform-box:fill-box; transform-origin:center; animation:spin3 var(--fan-dur,1.1s) linear infinite; animation-play-state:var(--fan-play,running); }
  @keyframes spin3 { to { transform:rotate(360deg); } }
  :global(.plan .cav-flow) { opacity:.2; }
  :global(.plan .cav-flow.on) { opacity:1; animation: marchR 1.5s linear infinite; }
  @keyframes marchR { to { stroke-dashoffset:-56; } }
  :global(.plan .mist3a) { transform-box:fill-box; transform-origin:center; animation: rise3 3.2s ease-out infinite; }
  @keyframes rise3 { 0%{ transform:translate(0,0) scale(.5); opacity:0 } 25%{ opacity:.7 } 100%{ transform:translate(-24px,-46px) scale(1.5); opacity:0 } }
  :global(.plan .heat3) { transform-box:fill-box; transform-origin:center bottom; animation: heatpop3 .4s cubic-bezier(.2,1.5,.4,1); }
  @keyframes heatpop3 { 0%{transform:scale(.1);opacity:0} 60%{opacity:1} 100%{transform:scale(1);opacity:1} }
  :global(.plan .flame3) { transform-box:fill-box; transform-origin:bottom center; }
  :global(.plan .flame3.f1) { animation: flick3 .7s ease-in-out infinite alternate; }
  :global(.plan .flame3.f2) { animation: flick3 .5s ease-in-out infinite alternate; }
  :global(.plan .flame3.f3) { animation: flick3 .4s ease-in-out infinite alternate; }
  @keyframes flick3 { from{transform:scaleY(.82) scaleX(1.05)} to{transform:scaleY(1.12) scaleX(.94)} }
  :global(.plan .rf-pulse) { animation: pulse 1.2s ease-in-out infinite; }
  :global(.plan .mistP) { transform-box:fill-box; animation: mistP 2.8s ease-out infinite; }
  @keyframes mistP { 0%{transform:translate(0,0) scale(.4);opacity:0} 25%{opacity:.7} 100%{transform:translate(-36px,-8px) scale(1.5);opacity:0} }
  /* climacell water — droplets raining down the media (duration set per-drop inline) */
  :global(.plan .cc-drop) { transform-box:fill-box; animation: ccrain 1.5s linear infinite; }
  @keyframes ccrain { 0%{transform:translateY(0);opacity:0} 12%{opacity:.9} 80%{opacity:.85} 100%{transform:translateY(72px);opacity:0} }
  @keyframes pulse { 0%,100%{opacity:1;} 50%{opacity:.4;} }

  /* settings modal — hosts the shared PlenumSetpointsForm, reskinned dark
     to match the dashboard chrome (the form stays light on its level1 page). */
  .ovl { position:fixed; inset:0; background:rgba(2,6,23,0.6); display:flex; align-items:center; justify-content:center; z-index:50; }
  .dlg { width:760px; max-width:94vw; max-height:88vh; display:flex; flex-direction:column; background:#0f1827; border:1px solid #334155; border-radius:14px; box-shadow:0 20px 60px rgba(0,0,0,0.5); overflow:hidden; }
  .dlg.wide { width:1400px; }
  /* full-bleed: overtakes the screen (densest pages, e.g. IO Config) */
  .dlg.full { width:98vw; max-width:98vw; height:94vh; max-height:94vh; }
  .dlg-hd { display:flex; align-items:center; justify-content:space-between; padding:13px 18px; background:#0c4a6e; color:#e0f2fe; font-size:16px; font-weight:800; flex:0 0 auto; }
  .dlg-x { background:none; border:none; color:#bae6fd; font-size:22px; line-height:1; cursor:pointer; }
  .modal-tabs { display:flex; gap:4px; padding:8px 14px 0; background:#0c4a6e; flex:0 0 auto; }
  .mtab { background:#0b1220; border:1px solid #334155; border-bottom:none; color:#94a3b8; font-size:13px; font-weight:700; padding:7px 16px; border-radius:8px 8px 0 0; cursor:pointer; }
  .mtab.active { background:#0f1827; color:#e0f2fe; border-color:#334155; }
  .tab-panel.hidden { display:none; }
  .dlg-body { flex:1 1 auto; overflow:auto; background:#0f1827; padding:10px 16px 4px; }
  .dlg-ft { display:flex; align-items:center; gap:10px; padding:12px 18px; background:#0b1220; border-top:1px solid #1e293b; flex:0 0 auto; }
  .ft-tag { font-size:11px; color:#64748b; font-style:italic; margin-right:auto; }
  .btn { border-radius:8px; padding:8px 18px; font-size:14px; font-weight:600; cursor:pointer; border:1px solid #334155; }
  .btn:disabled { opacity:.5; cursor:default; }
  .btn.ghost { background:#1e293b; color:#cbd5e1; }
  .btn.save { background:#0ea5e9; color:#04293b; border-color:#0ea5e9; }
  /* The form's dark skin now lives inside PlenumSetpointsForm (theme={$themeStore}),
     so the modal only owns its own chrome (overlay / header / footer). */

  .legend { display:flex; gap:16px; align-items:center; margin-top:10px; font-size:12px; color:#94a3b8; }
  .legend .sw { display:inline-block; width:14px; height:14px; border-radius:3px; margin-right:5px; vertical-align:-2px; }
  .legend .hint { margin-left:auto; font-style:italic; }
</style>
