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

  // ─── Sensor label slots (placeholder spatial layout) ──────────────
  // 3 zones (North / Middle / South) × 3 depths (Top / Middle / Bottom).
  // Until we have the customer-specific bay layout, all slots show the
  // plenum temp as a placeholder. Real per-position sensor values plug
  // in here from sensorData / SystemStatus extension fields.
  $: pileSensors = [
    [{ label: 'PT N Bay',   t: plenumTemp }, { label: 'PT N Mid',  t: plenumTemp }, { label: 'PT N End',  t: plenumTemp }],
    [{ label: 'PT Mid Bay', t: plenumTemp }, { label: 'PT Mid Mid',t: plenumTemp }, { label: 'PT Mid End',t: plenumTemp }],
    [{ label: 'PT S Bay',   t: plenumTemp }, { label: 'PT S Mid',  t: plenumTemp }, { label: 'PT S End',  t: plenumTemp }],
  ];

  function backToHome() {
    $navigationStore.level = 0;
    goto('/');
  }
</script>

<svelte:head>
  <title>Operator Dashboard (Preview)</title>
</svelte:head>

<!-- ════════════════════════════════════════════════════════════════ -->
<!-- Preview banner — visible reminder this is experimental / opt-in -->
<!-- ════════════════════════════════════════════════════════════════ -->
<div class="bg-orange-100 border-b-2 border-orange-400 text-orange-900 text-sm py-1 px-3 flex justify-between items-center">
  <div>
    <strong>PREVIEW</strong> · Operator Dashboard · Read-only ·
    Not in menu (access via /dashboard URL only)
  </div>
  <button class="underline" on:click={backToHome}>← Back to Home</button>
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

  <!-- ──── INTAKE STACK (left) ──────────────────────────────────── -->
  <div class="col-span-2 flex flex-col gap-2">
    <div class="text-xs text-center font-bold text-gray-500 tracking-wider">INTAKE</div>

    <!-- Top damper -->
    <div class="bg-white border-2 rounded-lg p-3 text-center {doorsPct > 5 ? 'border-blue-500' : 'border-gray-300'}">
      <div class="text-xs text-gray-500">Top Damper</div>
      <div class="text-2xl font-bold {doorsPct > 5 ? 'text-blue-600' : 'text-gray-400'}">{doorsPct}%</div>
      {#if doorsRemoteOff === 2}
        <div class="text-xs text-blue-700 font-bold mt-1">MANUAL</div>
      {/if}
    </div>

    <!-- Climacell evap -->
    <div class="border-2 rounded-lg p-3 text-center {climacellOn ? 'bg-cyan-100 border-cyan-500' : 'bg-white border-gray-300'}">
      <div class="text-xs {climacellOn ? 'text-cyan-800' : 'text-gray-500'}">Climacell</div>
      <div class="text-2xl">{climacellOn ? '❄' : '·'}</div>
      <div class="text-xs font-bold {climacellOn ? 'text-cyan-700' : 'text-gray-400'}">{climacellOn ? 'ACTIVE' : 'OFF'}</div>
    </div>

    <!-- Fan stack -->
    <div class="border-2 rounded-lg p-3 text-center {fanOn ? 'bg-emerald-50 border-emerald-500' : 'bg-white border-gray-300'}">
      <div class="text-xs {fanOn ? 'text-emerald-800' : 'text-gray-500'}">Fan</div>
      <div class="text-2xl font-bold {fanOn ? 'text-emerald-700' : 'text-gray-400'}">
        {fanSpeed === 'Off' ? 'OFF' : `${fanSpeed}%`}
      </div>
    </div>

    <!-- Heat path: burner (onion) OR heat (potato), and cavity heat -->
    {#if isOnion}
      <div class="border-2 rounded-lg p-3 text-center {burnerOn ? 'bg-orange-100 border-orange-500' : 'bg-white border-gray-300'}">
        <div class="text-xs {burnerOn ? 'text-orange-800' : 'text-gray-500'}">Burner</div>
        <div class="text-2xl">{burnerOn ? '🔥' : '·'}</div>
      </div>
    {:else}
      <div class="border-2 rounded-lg p-3 text-center {heatOn ? 'bg-orange-100 border-orange-500' : 'bg-white border-gray-300'}">
        <div class="text-xs {heatOn ? 'text-orange-800' : 'text-gray-500'}">Plenum Heat</div>
        <div class="text-2xl">{heatOn ? '🔥' : '·'}</div>
      </div>
    {/if}

    <!-- Bottom damper -->
    <div class="bg-white border-2 rounded-lg p-3 text-center {doorsPct > 5 ? 'border-blue-500' : 'border-gray-300'}">
      <div class="text-xs text-gray-500">Bottom Damper</div>
      <div class="text-2xl font-bold {doorsPct > 5 ? 'text-blue-600' : 'text-gray-400'}">{doorsPct}%</div>
    </div>

    <!-- Cavity heat (if onion build) -->
    {#if cavityHeatOn !== undefined}
      <div class="border-2 rounded-lg p-2 text-center text-xs {cavityHeatOn ? 'bg-amber-100 border-amber-500' : 'bg-white border-gray-200'}">
        <span class="font-bold">Cavity Heat</span>
        <span class={cavityHeatOn ? 'text-amber-700 font-bold' : 'text-gray-400'}>
          {cavityHeatOn ? 'ON' : 'OFF'}
        </span>
      </div>
    {/if}
  </div>

  <!-- ──── PILE CROSS-SECTION (center) ──────────────────────────── -->
  <div class="col-span-7 rounded-lg relative overflow-hidden border-2 border-amber-700"
       style="background: linear-gradient(180deg, #c8a060 0%, #8b6334 50%, #6b4824 100%);">

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

    <!-- Airflow arrows (top row, animated via CSS pulse) -->
    <div class="absolute top-2 left-0 right-0 flex justify-around text-blue-300 text-2xl select-none pointer-events-none">
      {#each Array(8) as _, i (i)}
        <span class="airflow-arrow" style="animation-delay: {i * 120}ms">↪</span>
      {/each}
    </div>

    <!-- Sensor labels overlaid spatially -->
    <div class="absolute inset-0 grid grid-rows-3 grid-cols-3 gap-2 p-6 pt-12 pb-20">
      {#each pileSensors as row, ri (ri)}
        {#each row as cell, ci (ci)}
          <div class="flex {ri === 0 ? 'items-start' : ri === 1 ? 'items-center' : 'items-end'} {ci === 0 ? 'justify-start' : ci === 1 ? 'justify-center' : 'justify-end'}">
            <div class="bg-orange-300 bg-opacity-90 rounded-md px-2 py-1 shadow-md border border-orange-700">
              <div class="text-[10px] text-gray-700 leading-tight">{cell.label}</div>
              <div class="font-bold text-sm leading-tight">{cell.t}°F</div>
            </div>
          </div>
        {/each}
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

    <!-- Refrigeration card -->
    <div class="border-2 rounded-lg p-3 {refrigPct > 5 ? 'bg-sky-50 border-sky-500' : 'bg-white border-gray-300'}">
      <div class="flex justify-between items-baseline">
        <div class="font-bold text-sm">Refrigeration</div>
        <div class="text-[10px] text-gray-500">
          {(rs as any)?.refrigMode === 0 ? 'ECO' : (rs as any)?.refrigMode === 1 ? 'REFRIG' : (rs as any)?.refrigMode === 2 ? 'ENTH' : ''}
        </div>
      </div>
      <div class="text-4xl font-bold {refrigPct > 5 ? 'text-sky-700' : 'text-gray-400'}">{refrigPct}%</div>
      <div class="text-xs text-gray-500">
        Output {outputDemandLabel === 'Refrigeration Output' ? 'demand' : ''}
      </div>
    </div>

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
  @keyframes airflow {
    0%   { opacity: 0.3; transform: translateX(0); }
    50%  { opacity: 0.9; transform: translateX(8px); }
    100% { opacity: 0.3; transform: translateX(0); }
  }
  .airflow-arrow {
    display: inline-block;
    animation: airflow 1.6s ease-in-out infinite;
  }
</style>
