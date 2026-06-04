<script lang="ts">
  // ═══════════════════════════════════════════════════════════════════
  //   /dashboard — Operator SCADA Dashboard (PREVIEW)
  // ═══════════════════════════════════════════════════════════════════
  //   Read-only operator view. Mirrors Gellert's internal PowerPoint
  //   mockup (cross-section of storage pile with sensor labels overlaid
  //   in spatial position, intake assembly on the left, plenum strip
  //   across the bottom, equipment cards on the right) while
  //   selectively borrowing from Imperium/IVI what they do well
  //   (status pill with traffic-light dots, outdoor weather card,
  //   Soft Stop / Reset / Hard Stop action trio).
  //
  //   NOT IN THE MENU. To remove cleanly: delete this folder.
  //   Access by typing /dashboard in the URL.
  //
  //   All data is read-only via the typed proto stores; no control
  //   surfaces are wired through. Soft Stop / Reset / Hard Stop are
  //   rendered as placeholder targets only.
  // ═══════════════════════════════════════════════════════════════════
  import {
    systemStatus,
    equipmentComposite,
    plenumSettings,
    basicSetup,
    fanRuntime,
    outsideAirSettings,
    refrigSettings,
    warningReport,
  } from "$lib/business/protoStores";
  import { headersStore, navigationStore } from "$lib/store";
  import { EQ } from "$lib/business/equipmentEnum";
  import { goto } from "$app/navigation";
  import { locale } from "svelte-i18n";
  import AnimatedFan       from "$lib/components/dashboard/AnimatedFan.svelte";
  import AnimatedDamper    from "$lib/components/dashboard/AnimatedDamper.svelte";
  import AnimatedClimacell from "$lib/components/dashboard/AnimatedClimacell.svelte";
  import AnimatedFlame     from "$lib/components/dashboard/AnimatedFlame.svelte";
  import AnimatedPilePath  from "$lib/components/dashboard/AnimatedPilePath.svelte";
  import SensorBadge       from "$lib/components/dashboard/SensorBadge.svelte";
  import RefrigTopologyCard from "$lib/components/dashboard/RefrigTopologyCard.svelte";
  import AudioAlert        from "$lib/components/dashboard/AudioAlert.svelte";
  import CureIntakeAssembly from "$lib/components/dashboard/CureIntakeAssembly.svelte";
  import DraggableSensorBadge from "$lib/components/dashboard/DraggableSensorBadge.svelte";
  import ZoneSelector from "$lib/components/dashboard/ZoneSelector.svelte";
  import { equipmentComposite as eqStore } from "$lib/business/protoStores";

  // ─── Reactive proto data ──────────────────────────────────────────
  $: ss   = $systemStatus;
  $: ec   = $equipmentComposite;
  $: ps   = $plenumSettings;
  $: bs   = $basicSetup;
  $: oas  = $outsideAirSettings;
  $: rs   = $refrigSettings;
  $: fr   = $fanRuntime;
  $: wr   = $warningReport;

  // ─── Formatters ───────────────────────────────────────────────────
  const f1 = (v: number | undefined | null): string =>
    v == null ? '--' : v.toFixed(1);
  const fi = (v: number | undefined | null): string =>
    v == null ? '--' : Math.round(v).toString();

  // ─── Sensors / setpoints ──────────────────────────────────────────
  $: outsideTemp  = f1(ss?.outsideTemp);
  $: outsideHumid = fi(ss?.outsideHumid);
  $: plenumTemp   = f1(ss?.plenumTemp);
  $: plenumHumid  = fi(ss?.plenumHumid);
  $: returnTemp   = f1(ss?.returnTemp);
  $: returnHumid  = fi(ss?.returnHumid);
  $: co2Level     = fi(ss?.co2Level);
  $: startTemp    = f1(ss?.startTemp);
  $: setpointT    = f1(ps?.tempSetpoint);
  $: setpointH    = fi(ps?.humidSetpoint);

  // ─── Equipment ────────────────────────────────────────────────────
  $: fanOn         = ec?.eqByIdx(EQ.FAN)?.outputOn ?? false;
  $: climacellOn   = ec?.eqByIdx(EQ.CLIMACELL)?.outputOn ?? false;
  $: refrigOnAny   = ec?.eqByIdx(EQ.REFRIGERATION)?.outputOn ?? false;
  $: light1On      = ec?.eqByIdx(EQ.LIGHTS1)?.inputOn ?? false;
  $: light2On      = ec?.eqByIdx(EQ.LIGHTS2)?.inputOn ?? false;
  $: heatOn        = ec?.eqByIdx(EQ.HEAT)?.outputOn ?? false;
  $: burnerOn      = ec?.eqByIdx(EQ.BURNER)?.outputOn ?? false;
  $: cavityHeatOn  = ec?.eqByIdx(EQ.CAVITY_HEAT)?.outputOn ?? false;
  $: doorsRemoteOff = ec?.eqByIdx(EQ.DOORS)?.remoteOff ?? 0;

  // ─── Live PWM percents ────────────────────────────────────────────
  $: doorsPct  = ss?.pwmDoorsPct ?? 0;
  $: refrigPct = ss?.pwmRefrigPct ?? 0;
  $: fanSpeed  = ss?.fanSpeed ?? 'Off';
  // Parse fan speed string ("25"/"Off"/"Manual"/…) → numeric % for
  // the animated fan's rotation rate. "Off"/"Manual"/anything non-
  // numeric → 0. The Fan card still renders the raw string for the
  // operator's label.
  $: fanPctNum = parseFanPct(fanSpeed);
  function parseFanPct(s: string): number {
    const n = parseInt(s, 10);
    return Number.isFinite(n) && n > 0 ? n : 0;
  }

  // ─── Build mode ──────────────────────────────────────────────────
  // basicSetup.systemMode: 0=Potato 1=Onion. Drives crop icon + curing
  // visualization (onion mode swaps intake fans for cure coil + burner
  // when in cure state).
  $: isOnion = (bs?.systemMode ?? 0) === 1;

  // ─── System state → label + color theme ─────────────────────────
  // Legacy AS2 SystemState constants (States.h):
  //   6 ST_COOLING       27 ST_RECIRC      28 ST_REFRIG
  //   31 ST_REFRIG_STANDBY  5 ST_AIRCURE   24 ST_BURNERCURE
  //   18 ST_PURGE        34 ST_STANDBY    33 ST_SHUTDOWN
  //   11 ST_FAILURE      etc.
  $: systemState = ss?.systemState ?? 0;
  $: modeInfo    = stateToModeInfo(systemState);

  // ─── Mode-driven canvas swap ──────────────────────────────────────
  // ST_AIRCURE (5) and ST_BURNERCURE (24) trigger the cure-mode
  // intake assembly + amber pile texture. ST_PURGE (18) shifts the
  // pile background to a green theme to make the CO₂ purge cycle
  // visually distinct from regular cooling/refrig.
  $: isCuring   = systemState === 5 || systemState === 24;
  $: isPurging  = systemState === 18;
  $: pileTheme  = pileBackground(systemState);
  function pileBackground(s: number): string {
    if (s === 5 || s === 24)
      return 'linear-gradient(180deg, #fde68a 0%, #d97706 50%, #92400e 100%)';   // amber/onion
    if (s === 18)
      return 'linear-gradient(180deg, #bbf7d0 0%, #16a34a 55%, #166534 100%)';   // green/purge
    return 'linear-gradient(180deg, #c8a060 0%, #8b6334 50%, #6b4824 100%)';     // default potato/onion brown
  }
  $: pileBorderClass = isCuring  ? 'border-amber-700'
                     : isPurging ? 'border-emerald-700'
                     :             'border-amber-700';
  // For the cure coil heat indicator: scale from plenum temp delta
  // above 80°F (a reasonable "starting to cure" baseline).
  $: cureCoilHeat = ss?.plenumTemp != null
    ? Math.max(0, Math.min(100, (ss.plenumTemp - 80) * 5))
    : 0;

  function stateToModeInfo(s: number): { label: string; banner: string; dot: string } {
    switch (s) {
      case 6:  return { label: 'Cooling',           banner: 'bg-emerald-50 border-emerald-500', dot: 'bg-emerald-500' };
      case 27: return { label: 'Recirculation',     banner: 'bg-slate-50 border-slate-400',     dot: 'bg-slate-500'   };
      case 28: return { label: 'Refrigeration',     banner: 'bg-sky-50 border-sky-500',         dot: 'bg-sky-500'     };
      case 31: return { label: 'Refrig Standby',    banner: 'bg-slate-50 border-slate-400',     dot: 'bg-slate-500'   };
      case 5:  return { label: 'Cure — Air',        banner: 'bg-amber-50 border-amber-500',     dot: 'bg-amber-500'   };
      case 24: return { label: 'Cure — Burner',     banner: 'bg-amber-100 border-amber-600',    dot: 'bg-amber-600'   };
      case 18: return { label: 'CO₂ Purge',         banner: 'bg-teal-50 border-teal-500',       dot: 'bg-teal-500'    };
      case 34: return { label: 'Standby',           banner: 'bg-gray-50 border-gray-400',       dot: 'bg-gray-400'    };
      case 33: return { label: 'Shutdown',          banner: 'bg-red-50 border-red-500',         dot: 'bg-red-500'     };
      case 11: return { label: 'Failure',           banner: 'bg-red-100 border-red-700',        dot: 'bg-red-700'     };
      default: return { label: `State ${s}`,        banner: 'bg-white border-gray-300',         dot: 'bg-gray-300'    };
    }
  }

  // ─── Outdoor "weather" icon — simple temp band ───────────────────
  $: outsideIcon = outsideTempIcon(ss?.outsideTemp);
  function outsideTempIcon(t: number | undefined | null): string {
    if (t == null) return '·';
    if (t < 32) return '❄';
    if (t < 50) return '☁';
    if (t < 75) return '⛅';
    return '☀';
  }

  // ─── Cool/heat label for the plenum strip ─────────────────────────
  // Uses the SystemStatus.coolLabel field (firmware's mode-tag):
  //   "0" = cooling output (doors PWM is the demand)
  //   "1" = refrigeration output (refrig PWM is the demand)
  //   "2" = burner output  (onion cure)
  $: outputDemandLabel = labelFromCoolLabel(ss?.coolLabel);
  $: outputDemandPct   = (ss?.coolLabel === '1' ? refrigPct : doorsPct);
  function labelFromCoolLabel(c: string | undefined): string {
    switch (c) {
      case '1': return 'Refrigeration Output';
      case '2': return 'Burner Output';
      case '3': return 'Doors Diag';
      default:  return 'Cooling Output';
    }
  }

  // ─── Active alarms count (for footer pill) ────────────────────────
  $: alarms = (wr as any)?.entries ?? (wr as any)?.alarms ?? [];
  $: alarmCount = Array.isArray(alarms) ? alarms.length : 0;

  // ─── Total fan runtime ────────────────────────────────────────────
  $: totalHours = fr?.totalHours ?? 0;

  // ─── Clock ────────────────────────────────────────────────────────
  let now = new Date();
  let clockTimer: ReturnType<typeof setInterval> | null = null;
  import { onMount, onDestroy } from "svelte";
  onMount(() => {
    clockTimer = setInterval(() => { now = new Date(); }, 1000);
  });
  onDestroy(() => {
    if (clockTimer) clearInterval(clockTimer);
  });

  // ─── Pile sensor slots (drag-positionable, persisted per ID) ─────
  // Each slot has a stable id (used for localStorage persistence) +
  // an initial (x, y) fraction of the pile canvas. Until we have
  // per-position sensor data, all values show plenum temp. Operators
  // can drag each pill to its actual spatial location on the pile;
  // their layout persists across reloads. Production view should
  // toggle `editable={false}` to lock layout.
  $: pileSensorSlots = [
    { id: 'pt-n-bay',   label: 'PT N Bay',   x: 0.15, y: 0.20 },
    { id: 'pt-n-mid',   label: 'PT N Mid',   x: 0.50, y: 0.20 },
    { id: 'pt-n-end',   label: 'PT N End',   x: 0.85, y: 0.20 },
    { id: 'pt-mid-bay', label: 'PT Mid Bay', x: 0.15, y: 0.50 },
    { id: 'pt-mid-mid', label: 'PT Mid Mid', x: 0.50, y: 0.50 },
    { id: 'pt-mid-end', label: 'PT Mid End', x: 0.85, y: 0.50 },
    { id: 'pt-s-bay',   label: 'PT S Bay',   x: 0.15, y: 0.78 },
    { id: 'pt-s-mid',   label: 'PT S Mid',   x: 0.50, y: 0.78 },
    { id: 'pt-s-end',   label: 'PT S End',   x: 0.85, y: 0.78 },
  ];

  // Whether sensor pills are draggable. In production this should
  // be derived from auth level / a site-admin toggle.
  let sensorsEditable = true;

  function backToHome() {
    $navigationStore.level = 0;
    goto('/');
  }

  // ─── Language flag toggle ────────────────────────────────────────
  // svelte-i18n is initialized with 'en' and 'zh' (see src/lib/i18n).
  // Click cycles between them. Resolved-locale strings can carry a
  // region tag (e.g. 'en-US', 'zh-CN') so we test for the base code.
  $: localeShort = ($locale ?? 'en').slice(0, 2).toLowerCase();
  $: flagInfo = localeShort === 'zh'
    ? { emoji: '🇨🇳', label: '中文', next: 'EN' }
    : { emoji: '🇺🇸', label: 'EN',   next: '中文' };

  function toggleLocale() {
    locale.set(localeShort === 'zh' ? 'en' : 'zh');
  }
</script>

<svelte:head>
  <title>Operator Dashboard (Preview)</title>
</svelte:head>

<!-- Critical-state audio + visual alert. Shows a fixed banner along
     the very top of the viewport when SystemState enters FAILURE /
     SHUTDOWN / REMOTE_STANDBY / SYS_REMOTEOFF, beeps once per
     transition, persistable mute via localStorage. -->
<AudioAlert systemState={systemState} label={modeInfo.label}/>

<!-- ════════════════════════════════════════════════════════════════ -->
<!-- Preview banner — visible reminder this is experimental / opt-in -->
<!-- ════════════════════════════════════════════════════════════════ -->
<div class="bg-orange-100 border-b-2 border-orange-400 text-orange-900 text-sm py-1 px-3 flex justify-between items-center">
  <div>
    <strong>PREVIEW</strong> · Operator Dashboard · Read-only ·
    Not in menu (access via /dashboard URL only)
  </div>
  <div class="flex items-center gap-3">
    <!-- Multi-zone selector: lists this Pi5 ("Local") + every panel
         in /iot/remote-systems. Picking a remote opens its dashboard /
         orbits / home in a new tab (Tier A). Live online indicators
         refresh every 10s. -->
    <ZoneSelector/>

    <!-- Sensor layout edit-toggle: when enabled, pile sensor badges
         are draggable + show ⋮⋮ handles. Layout persists per ID
         to localStorage. Lock for production / kiosk viewing. -->
    <button
      class="px-2 py-0.5 rounded border text-xs font-bold
             {sensorsEditable ? 'bg-blue-100 border-blue-400 text-blue-800' : 'bg-white border-gray-300 text-gray-600'}"
      on:click={() => sensorsEditable = !sensorsEditable}
      title={sensorsEditable ? 'Lock sensor layout' : 'Unlock sensor layout (drag to position)'}
    >
      {sensorsEditable ? '🔓 Layout' : '🔒 Locked'}
    </button>
    <!-- Language flag: click to swap UI language. Currently EN ↔ ZH;
         add more in src/lib/i18n/index.ts and extend the toggle. -->
    <button
      class="flex items-center gap-1 px-2 py-0.5 rounded border border-orange-300 bg-white hover:bg-orange-50"
      on:click={toggleLocale}
      title="Switch to {flagInfo.next}"
    >
      <span class="text-lg leading-none">{flagInfo.emoji}</span>
      <span class="text-xs font-bold">{flagInfo.label}</span>
    </button>
    <button class="underline" on:click={backToHome}>← Back to Home</button>
  </div>
</div>

<!-- ════════════════════════════════════════════════════════════════ -->
<!-- Mode banner — color-coded to current system state                -->
<!-- ════════════════════════════════════════════════════════════════ -->
<div class="border-b-4 {modeInfo.banner} px-4 py-3 flex items-center justify-between">
  <div class="flex items-center gap-4">
    <!-- Traffic-light dots (Imperium pattern) -->
    <div class="flex gap-1.5">
      <span class="w-3 h-3 rounded-full {systemState === 11 || systemState === 33 ? 'bg-red-500' : 'bg-red-200'}"></span>
      <span class="w-3 h-3 rounded-full {alarmCount > 0 ? 'bg-amber-500' : 'bg-amber-200'}"></span>
      <span class="w-3 h-3 rounded-full {systemState !== 11 && systemState !== 33 && alarmCount === 0 ? 'bg-green-500' : 'bg-green-200'}"></span>
    </div>
    <div>
      <div class="text-3xl font-bold leading-tight">{modeInfo.label}</div>
      <div class="text-sm text-gray-600">
        Setpoint <strong>{setpointT}°F</strong> ·
        Humidity SP <strong>{setpointH}%</strong> ·
        Building <strong>{isOnion ? 'Onion' : 'Potato'}</strong>
      </div>
    </div>
  </div>
  <!-- Outdoor weather card (Imperium pattern) -->
  <div class="text-right">
    <div class="flex items-center gap-2 justify-end">
      <span class="text-3xl">{outsideIcon}</span>
      <span class="text-3xl font-bold">{outsideTemp}°F</span>
    </div>
    <div class="text-sm text-gray-600">Outside · {outsideHumid}% RH</div>
  </div>
</div>

<!-- ════════════════════════════════════════════════════════════════ -->
<!-- Main grid: intake | pile cross-section | right panel             -->
<!-- ════════════════════════════════════════════════════════════════ -->
<div class="grid grid-cols-12 gap-3 p-3" style="min-height: calc(100vh - 200px);">

  <!-- ──── INTAKE COLUMN (left) — canvas swap by mode ──────────────
       Standard intake (damper/climacell/fan/heat/damper) when in
       cooling/refrig/recirc/standby. Curing modes (ST_AIRCURE/
       ST_BURNERCURE) swap in the CureIntakeAssembly: vertical cure
       coil + dual flanking burners + fan still pushing cured air
       to the plenum. The pile background also shifts to an amber
       onion theme (see pileTheme). -->
  {#if isCuring}
    <div class="col-span-2">
      <CureIntakeAssembly
        fanPct={fanPctNum}
        fanRunning={fanOn}
        leftBurnerOn={burnerOn}
        rightBurnerOn={burnerOn}
        coilHeat={cureCoilHeat}
      />
    </div>
  {:else}
  <div class="col-span-2 flex flex-col gap-2">
    <div class="text-xs text-center font-bold text-gray-500 tracking-wider">INTAKE</div>

    <!-- TOP DAMPER = EXHAUST.
         Gravity-closed: only opens when the bottom fresh-air doors
         open AND positive pressure (the fans pushing return air
         that can't recirculate through the closed bottom intake
         path) forces it open. So the visual reflects the same %
         as the intake damper, but only when both are commanded > 5%.
         When intake is closed, exhaust is held shut by gravity even
         if the operator commanded it open. -->
    <div class="bg-white border-2 rounded-lg p-2 flex flex-col items-center {doorsPct > 5 ? 'border-blue-500' : 'border-gray-300'}">
      <div class="text-xs text-gray-500 mb-1">Exhaust</div>
      <AnimatedDamper pct={doorsPct > 5 ? doorsPct : 0} width={90} height={70} flow="out"/>
      <div class="text-lg font-bold {doorsPct > 5 ? 'text-blue-600' : 'text-gray-400'} mt-1">{doorsPct > 5 ? doorsPct : 0}%</div>
      {#if doorsRemoteOff === 2}
        <div class="text-[10px] text-blue-700 font-bold">MANUAL</div>
      {/if}
      <div class="text-[9px] text-gray-400 mt-0.5">Gravity-closed</div>
    </div>

    <!-- Climacell evap: droplets cascade when active -->
    <div class="border-2 rounded-lg p-2 flex flex-col items-center {climacellOn ? 'bg-cyan-50 border-cyan-500' : 'bg-white border-gray-300'}">
      <div class="text-xs {climacellOn ? 'text-cyan-800' : 'text-gray-500'} mb-1">Climacell</div>
      <!-- Climacell demand: tracks fan PWM when climacell is active —
           more airflow = more evaporative work, so droplet density
           rises with fan demand. Falls back to 50 (steady) when
           fanPctNum is 0 but the coil is running. -->
      <AnimatedClimacell
        active={climacellOn}
        demand={fanPctNum > 0 ? fanPctNum : 50}
        size={70}/>
      <div class="text-[10px] font-bold {climacellOn ? 'text-cyan-700' : 'text-gray-400'}">{climacellOn ? 'ACTIVE' : 'OFF'}</div>
    </div>

    <!-- Fan stack: blades spin at fan % rate -->
    <div class="border-2 rounded-lg p-2 flex flex-col items-center {fanOn ? 'bg-emerald-50 border-emerald-500' : 'bg-white border-gray-300'}">
      <div class="text-xs {fanOn ? 'text-emerald-800' : 'text-gray-500'} mb-1">Fan</div>
      <AnimatedFan pct={fanPctNum} running={fanOn} size={70}/>
      <div class="text-lg font-bold {fanOn ? 'text-emerald-700' : 'text-gray-400'} mt-1">
        {fanSpeed === 'Off' ? 'OFF' : `${fanSpeed}%`}
      </div>
    </div>

    <!-- Heat path: burner (onion) OR heat (potato), flame flickers when on -->
    {#if isOnion}
      <div class="border-2 rounded-lg p-2 flex flex-col items-center {burnerOn ? 'bg-orange-100 border-orange-500' : 'bg-white border-gray-300'}">
        <div class="text-xs {burnerOn ? 'text-orange-800' : 'text-gray-500'} mb-1">Burner</div>
        <AnimatedFlame active={burnerOn} size={56}/>
      </div>
    {:else}
      <div class="border-2 rounded-lg p-2 flex flex-col items-center {heatOn ? 'bg-orange-100 border-orange-500' : 'bg-white border-gray-300'}">
        <div class="text-xs {heatOn ? 'text-orange-800' : 'text-gray-500'} mb-1">Plenum Heat</div>
        <AnimatedFlame active={heatOn} size={56}/>
      </div>
    {/if}

    <!-- BOTTOM DAMPER = INTAKE.
         Driven directly by the doors PWM (the operator's commanded
         fresh-air % or the engine's PID demand in AUTO). When this
         opens, the gravity-closed top exhaust opens with it under
         positive pressure; both closed → the fan recirculates. -->
    <div class="bg-white border-2 rounded-lg p-2 flex flex-col items-center {doorsPct > 5 ? 'border-blue-500' : 'border-gray-300'}">
      <div class="text-xs text-gray-500 mb-1">Intake</div>
      <AnimatedDamper pct={doorsPct} width={90} height={70} flow="in"/>
      <div class="text-lg font-bold {doorsPct > 5 ? 'text-blue-600' : 'text-gray-400'} mt-1">{doorsPct}%</div>
    </div>

    <!-- Cavity heat (if onion build): inline flame indicator -->
    {#if cavityHeatOn !== undefined}
      <div class="border-2 rounded-lg p-2 flex items-center justify-center gap-2 text-xs {cavityHeatOn ? 'bg-amber-100 border-amber-500' : 'bg-white border-gray-200'}">
        <AnimatedFlame active={cavityHeatOn} size={28}/>
        <div>
          <div class="font-bold">Cavity Heat</div>
          <div class={cavityHeatOn ? 'text-amber-700 font-bold' : 'text-gray-400'}>
            {cavityHeatOn ? 'ON' : 'OFF'}
          </div>
        </div>
      </div>
    {/if}
  </div>
  {/if}

  <!-- ──── PILE CROSS-SECTION (center) — theme by mode ─────────────
       Background gradient swaps per systemState: amber-onion for
       curing, green for CO2 purge, default brown for cooling/refrig.
       Border tint follows the same convention. -->
  <div class="col-span-7 rounded-lg relative overflow-hidden border-2 {pileBorderClass}"
       style="background: {pileTheme};">

    <!-- Texture: scatter of darker spots for pile feel -->
    <svg viewBox="0 0 400 300" preserveAspectRatio="none" class="absolute inset-0 w-full h-full opacity-30 pointer-events-none">
      {#each Array(160) as _, i (i)}
        <circle
          cx={(i * 37) % 400}
          cy={(i * 53) % 300}
          r={1 + (i % 3)}
          fill={i % 2 ? 'rgba(60,40,15,0.5)' : 'rgba(110,80,40,0.5)'}
        />
      {/each}
    </svg>

    <!-- Distribution duct: horizontal pipe along the pile floor with
         outlet vents that emit vertical air streams rising through
         the product. Matches the actual physical flow: fan → plenum
         → duct → distributed bottom-to-top through the pile. -->
    <div class="absolute inset-0 pointer-events-none">
      <AnimatedPilePath fanPct={fanPctNum} running={fanOn} outlets={9}/>
    </div>

    <!-- Pile sensor pills — DRAG-POSITIONABLE. Operators move each
         pill to its real spatial location on the pile (per-site
         layout persists in localStorage). Color shifts blue→green
         →red by delta from plenum setpoint; mini-sparkline shows
         recent trend. Drag-handle (⋮⋮) visible in edit mode. -->
    <div class="absolute inset-0 p-6 pt-12 pb-20">
      {#each pileSensorSlots as slot (slot.id)}
        <DraggableSensorBadge
          id={slot.id}
          label={slot.label}
          value={ss?.plenumTemp ?? null}
          setpoint={ps?.tempSetpoint ?? null}
          initialX={slot.x}
          initialY={slot.y}
          editable={sensorsEditable}
        />
      {/each}
    </div>

    <!-- Bay light bulbs overlaid in pile (Gellert pattern) -->
    <div class="absolute inset-0 grid grid-cols-2 pointer-events-none p-12">
      <div class="flex items-center justify-center">
        <span class="text-4xl {light1On ? '' : 'opacity-30'}" title="Bay Lights 1">
          {light1On ? '💡' : '🌑'}
        </span>
      </div>
      <div class="flex items-center justify-center">
        <span class="text-4xl {light2On ? '' : 'opacity-30'}" title="Bay Lights 2">
          {light2On ? '💡' : '🌑'}
        </span>
      </div>
    </div>

    <!-- Plenum strip (bottom) -->
    <div class="absolute bottom-2 left-2 right-2 bg-white bg-opacity-95 rounded-md border border-gray-300 p-2 text-sm flex justify-around items-center">
      <div>
        <span class="text-xs text-gray-500">PL Avg</span>
        <span class="font-bold ml-1 text-lg">{plenumTemp}°F</span>
        <span class="text-xs text-gray-500 ml-1">SP {setpointT}</span>
      </div>
      <div>
        <span class="text-xs text-gray-500">PL Hum</span>
        <span class="font-bold ml-1 text-lg">{plenumHumid}%</span>
        <span class="text-xs text-gray-500 ml-1">SP {setpointH}</span>
      </div>
      <div>
        <span class="text-xs text-gray-500">Return</span>
        <span class="font-bold ml-1 text-lg">{returnTemp}°F</span>
        <span class="text-xs text-gray-500 ml-1">{returnHumid}% RH</span>
      </div>
    </div>
  </div>

  <!-- ──── RIGHT PANEL (data cards) ──────────────────────────────── -->
  <div class="col-span-3 flex flex-col gap-2">

    <!-- Refrigeration topology card: auto-picks between Stages / PWM /
         TRITON based on what's configured. Mirrors the 0.A.227
         widened gate in CtrlRefrig. tritonConfigured is hard-wired
         false until the dashboard subscribes to OrbitStatus tag 120
         and walks slots for role=TRITON (next iteration). -->
    <RefrigTopologyCard
      equipment={$eqStore}
      refrigPct={refrigPct}
      refrigModeRaw={(rs as any)?.refrigMode}
      tritonConfigured={false}
    />

    <!-- Cooling-available temp card -->
    <div class="border-2 rounded-lg p-3 bg-white border-gray-300">
      <div class="font-bold text-sm">Cooling Available</div>
      <div class="text-4xl font-bold text-emerald-700">{startTemp}°F</div>
      <div class="text-xs text-gray-500">vs Outside {outsideTemp}°F</div>
    </div>

    <!-- CO2 card -->
    <div class="border-2 rounded-lg p-3 bg-white border-gray-300">
      <div class="font-bold text-sm">CO₂</div>
      <div class="text-3xl font-bold">{co2Level} <span class="text-base text-gray-500">ppm</span></div>
    </div>

    <!-- Outside air control mode (informational) -->
    <div class="border rounded-lg p-2 bg-gray-50 border-gray-200 text-xs">
      <div class="font-bold text-gray-700">Outside Air Ctrl</div>
      <div class="flex justify-between mt-1">
        <span class="text-gray-500">Mode</span>
        <span class="font-bold">{(oas as any)?.mode === 1 ? 'Plenum' : 'Outside'}</span>
      </div>
      <div class="flex justify-between">
        <span class="text-gray-500">Diff</span>
        <span class="font-bold">{f1((oas as any)?.differential)}°F</span>
      </div>
      <div class="flex justify-between">
        <span class="text-gray-500">A/B</span>
        <span class="font-bold">{(oas as any)?.aboveBelow === 1 ? 'Below' : 'Above'}</span>
      </div>
    </div>

    <!-- Runtime card -->
    <div class="border rounded-lg p-2 bg-gray-50 border-gray-200 text-xs">
      <div class="font-bold text-gray-700">Fan Total Hours</div>
      <div class="text-2xl font-bold">{totalHours.toLocaleString()}</div>
    </div>
  </div>
</div>

<!-- ════════════════════════════════════════════════════════════════ -->
<!-- Footer: action buttons (disabled in preview) + clock + alarms    -->
<!-- ════════════════════════════════════════════════════════════════ -->
<div class="border-t-2 border-gray-300 bg-gray-100 px-3 py-2 flex items-center justify-between text-sm">
  <div class="flex items-center gap-2">
    <!-- Imperium-style action trio: Soft Stop / Reset / Hard Stop -->
    <button class="bg-red-200 text-red-900 font-bold px-3 py-1 rounded opacity-60 cursor-not-allowed"
            disabled title="Disabled in Preview">Soft Stop</button>
    <button class="bg-green-200 text-green-900 font-bold px-3 py-1 rounded opacity-60 cursor-not-allowed"
            disabled title="Disabled in Preview">Reset</button>
    <button class="bg-red-600 text-white font-bold px-3 py-1 rounded opacity-60 cursor-not-allowed"
            disabled title="Disabled in Preview">HARD STOP</button>
    <span class="text-xs text-gray-500 ml-2">(controls in next iteration)</span>
  </div>

  <div class="flex items-center gap-4">
    <!-- Alarms pill -->
    <div class="flex items-center gap-1">
      <span class="text-lg">{alarmCount > 0 ? '⚠️' : '✓'}</span>
      <span class="font-bold {alarmCount > 0 ? 'text-red-700' : 'text-green-700'}">
        Alarms ({alarmCount})
      </span>
    </div>
    <!-- Clock -->
    <div class="text-gray-700">
      {now.toLocaleDateString()} {now.toLocaleTimeString()}
    </div>
  </div>
</div>

<style>
  /* All equipment animations live in their respective components
     (AnimatedFan / AnimatedDamper / AnimatedClimacell / AnimatedFlame /
     AnimatedRefrigCoil / AnimatedAirflow). This file is layout-only. */
</style>
