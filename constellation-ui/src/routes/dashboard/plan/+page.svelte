<script lang="ts">
  // ═══════════════════════════════════════════════════════════════════
  //   /dashboard/plan — Top-down Building View (CONCEPT ART)
  // ═══════════════════════════════════════════════════════════════════
  //   Plan / floor-plan SCADA view of one building (= one Constellation
  //   panel). Traced from Aaron's hand sketch (Agri-Stor, 2026-06-04):
  //
  //     ┌──────────────── air envelope (return) ────────────────┐
  //     │  ┌──────────── BAY 1 (Bay Lights 1) ──────────────┐   │
  //     │  │  P1     P2     P3     P4  · Pile Fans           │   │
  //     │  └────────────────────────────────────────────────┘   │
  //     │  Doors│Refrig│Climacell│Evap Cell│hum1│Heat│hum2│hum3  │  ← spine
  //     │  ┌──────────── BAY 2 (Bay Light 2) ───────────────┐   │
  //     │  │  P1     P2     P3     P4  · Pile Fans           │   │
  //     │  └────────────────────────────────────────────────┘   │
  //     └────────────────────────────────────────────────────────┘
  //
  //   SENSORS WIRED (read-only): pile temps + humidity from `sensorData`,
  //   plenum / return / outside / CO₂ from `systemStatus`. Each readout
  //   shows the LIVE value when the proto stream is connected, else a
  //   gentle faked drift so it still demos standalone. This file only
  //   READS existing stores — no bridge / shared-UI changes.
  //   Equipment animations are still driven by the header toggles (concept
  //   controls), not yet wired to equipmentComposite.
  //   Pile sensor badges are drag-to-place; positions persist in localStorage.
  //
  //   NOT IN THE MENU. To remove cleanly: delete this folder.
  //   Access by typing /dashboard/plan in the URL.
  // ═══════════════════════════════════════════════════════════════════
  import { onMount, onDestroy } from "svelte";
  import { goto } from "$app/navigation";
  import { systemStatus, sensorData, sensorLabels, warningReport, plenumSettings } from "$lib/business/protoStores";

  // ─── Live proto data (read-only) ──────────────────────────────────
  // Each readout shows the LIVE stream value when present, else a gentle
  // faked drift so the page still demos standalone. This file only READS
  // existing stores — no bridge / shared-UI changes.
  $: ss = $systemStatus;
  $: sd = $sensorData;
  $: sl = $sensorLabels;
  const lv = (live: number | null | undefined, fake: number): number =>
    (live != null && Number.isFinite(live)) ? live : fake;
  // value of the i-th SensorReading in a live temperatures/humidities array
  const sensorAt = (arr: any[] | undefined, i: number): number | null => {
    const r = arr?.[i];
    return (r && r.valid !== false && Number.isFinite(r.value)) ? r.value : null;
  };
  const labelAt = (arr: any[] | undefined, i: number, fallback: string): string =>
    arr?.[i]?.label || fallback;

  // ─── Fake "live" tick — makes sensor values drift gently ──────────
  let t = 0;
  let timer: ReturnType<typeof setInterval> | null = null;
  onMount(() => { timer = setInterval(() => { t += 1; }, 1200); });
  onDestroy(() => { if (timer) clearInterval(timer); });

  // Gentle deterministic drift so the concept feels alive without RNG.
  const wob = (seed: number, amp: number): number =>
    Math.sin((t + seed * 1.7) * 0.5) * amp;

  // ─── Header: live clock, mode-from-state, health/alarms, setpoints ──
  let now = new Date();
  let clk: ReturnType<typeof setInterval> | null = null;
  onMount(() => { clk = setInterval(() => (now = new Date()), 1000); });
  onDestroy(() => { if (clk) clearInterval(clk); });
  $: clockStr = now.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", second: "2-digit" });
  $: dateStr  = now.toLocaleDateString([], { month: "short", day: "numeric" });

  // Mode pill from engine SystemState (falls back to Cooling for the demo).
  function stateToMode(s: number): { label: string; dot: string; tint: string } {
    switch (s) {
      case 6:  return { label: "Cooling",       dot: "#10b981", tint: "#ecfdf5" };
      case 27: return { label: "Recirculation", dot: "#64748b", tint: "#f8fafc" };
      case 28: return { label: "Refrigeration", dot: "#0ea5e9", tint: "#f0f9ff" };
      case 31: return { label: "Refrig Standby",dot: "#64748b", tint: "#f8fafc" };
      case 5:  case 24: return { label: "Curing", dot: "#f59e0b", tint: "#fffbeb" };
      case 18: return { label: "CO₂ Purge",     dot: "#14b8a6", tint: "#f0fdfa" };
      case 34: return { label: "Standby",       dot: "#94a3b8", tint: "#f8fafc" };
      case 33: return { label: "Shutdown",      dot: "#ef4444", tint: "#fef2f2" };
      case 11: return { label: "Failure",       dot: "#b91c1c", tint: "#fee2e2" };
      default: return { label: "Cooling",       dot: "#10b981", tint: "#ecfdf5" };
    }
  }
  $: mode = stateToMode(ss?.systemState ?? 6);

  // Active alarms from warningReport (empty when no stream → "All Clear").
  $: wr = $warningReport;
  $: alarms = (wr as any)?.entries ?? (wr as any)?.alarms ?? [];
  $: alarmCount = Array.isArray(alarms) ? alarms.length : 0;

  // Setpoints (for setpoint-vs-actual on the plenum).
  $: pls = $plenumSettings;
  $: tempSP  = lv((pls as any)?.tempSetpoint,  42);
  $: humidSP = lv((pls as any)?.humidSetpoint, 95);
  $: tDev    = plenumT - tempSP;   // plenum deviation from setpoint

  // ─── Sensor health: normal / warning / alarm ──────────────────────
  type Health = "ok" | "warn" | "alarm";
  const pileHealth = (v: number): Health => (v >= 53 ? "alarm" : v >= 48 ? "warn" : "ok");
  const co2Health  = (v: number): Health => (v >= 1500 ? "alarm" : v >= 1000 ? "warn" : "ok");
  const healthColor = (h: Health): string =>
    h === "alarm" ? "#ef4444" : h === "warn" ? "#f59e0b" : "#10b981";

  // ─── Building state ───────────────────────────────────────────────
  $: outsideTemp  = lv(ss?.outsideTemp,  47);
  $: outsideHumid = lv(ss?.outsideHumid, 62);
  $: co2          = lv(ss?.co2Level,     640 + Math.round(wob(9, 18)));

  // Equipment on/off (faked)
  const eq = {
    fan: true,
    climacell: true,
    lights1: true,
    lights2: false,
  };

  // Heat output — the spine heat box only exists when heat is firing,
  // and pops up with a flame when it does. CONCEPT toggle in header.
  let heatOn = false;

  // Plenum humidifier pump — mist only sprays into the air stream when on.
  let humidPump = true;

  // Refrigeration: off → cooling (blue coils) → defrost (red coils).
  // CONCEPT cycle button in the header previews all three.
  let refrigState: "off" | "cool" | "defrost" = "cool";
  let refrigPct = 80;     // output level — drives coil-glow intensity
  const cycleRefrig = () => {
    refrigState = refrigState === "off" ? "cool" : refrigState === "cool" ? "defrost" : "off";
  };
  $: refrigColor = refrigState === "defrost" ? "#ef4444" : refrigState === "off" ? "#94a3b8" : "#3b82f6";
  // Glow blur scales with output: ~1px at low output, ~10px at full.
  $: glowPx = refrigState === "off" ? 0 : 1 + (refrigPct / 100) * 9;
  let doorsPct = 100;     // fresh-air door output % — slider-driven in concept
  const fanPct = 65;

  // Cavity-heat air-envelope output. The cavity (between cold exterior
  // wall and interior sheet) runs heated storage air to insulate the
  // room and stop condensation dripping onto the pile. The red airflow
  // in the envelope only MARCHES when this output is energized.
  // CONCEPT toggle — flip it in the header to preview both states.
  let cavityHeat = true;

  // ─── 8 pile sensor stations (P1..P4 × 2 bays) — faked temps ───────
  // value() recomputed reactively off the tick so they drift.
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
  // live pile temp (by array position) or faked drift
  $: stationVal = (s: Station): number =>
    sensorAt(sd?.temperatures, s.idx) ?? (s.base + wob(s.base, 0.6));

  // 3 humidity sensors on the spine (faked)
  $: hums = [
    { id: "hum1", label: "Hum 1", x: 500, v: sensorAt(sd?.humidities, 0) ?? (90 + wob(1, 1.5)) },
    { id: "hum2", label: "Hum 2", x: 720, v: sensorAt(sd?.humidities, 1) ?? (88 + wob(2, 1.5)) },
    { id: "hum3", label: "Hum 3", x: 940, v: sensorAt(sd?.humidities, 2) ?? (86 + wob(3, 1.5)) },
  ];

  // Plenum (mid-spine) + Return air (by the doors) + CO₂ — live or fallback
  $: plenumT = lv(ss?.plenumTemp,  41 + wob(5, 0.5));
  $: plenumH = lv(ss?.plenumHumid, 92 + wob(6, 1.2));
  $: returnT = lv(ss?.returnTemp,  52 + wob(7, 0.6));
  $: returnH = lv(ss?.returnHumid, 85 + wob(8, 1.2));
  $: co2v    = lv(ss?.co2Level,    640 + Math.round(wob(9, 18)));

  // Temp → color ramp (cool blue → warm red), for the pile badges.
  function tempColor(v: number): string {
    const lo = 36, hi = 55;
    const k = Math.max(0, Math.min(1, (v - lo) / (hi - lo)));
    const h = 210 - k * 210;          // 210=blue → 0=red
    return `hsl(${h}, 80%, 50%)`;
  }

  // ─── SVG geometry ─────────────────────────────────────────────────
  // viewBox 1200×760. Doors/intake at LEFT, far end at RIGHT.
  const ROOM = { x: 96, y: 118, w: 1008, h: 524 };
  const SPINE = { y: 350, h: 60 };            // central cavity band
  const BAY1 = { y: ROOM.y + 8, h: SPINE.y - (ROOM.y + 8) - 6 };
  const BAY2 = { y: SPINE.y + SPINE.h + 6, h: (ROOM.y + ROOM.h) - (SPINE.y + SPINE.h) - 8 };

  // P1..P4 default X positions inside the room (near doors → far end)
  const pX = [320, 520, 730, 940];
  function defaultXY(s: Station): { x: number; y: number } {
    const bay = s.bay === 1 ? BAY1 : BAY2;
    return { x: pX[s.p - 1], y: bay.y + bay.h * 0.5 };
  }

  // ─── Draggable pile sensors — operators drag each probe to its real
  //   spot on the pile; positions persist per-site in localStorage. ──
  let svgEl: SVGSVGElement;
  let dragPos: Record<string, { x: number; y: number }> = {};
  let dragId: string | null = null;
  let dragMoved = false;
  let startClient = { x: 0, y: 0 };
  let selectedSensor: string | null = null;   // tap a probe → trend popup
  const DRAG_KEY = "planPileSensorPos";

  // recent-value history per probe (for tap-for-trend sparklines)
  let history: Record<string, number[]> = {};
  const HIST_LEN = 30;
  let histTimer: ReturnType<typeof setInterval> | null = null;

  onMount(() => {
    try { const s = localStorage.getItem(DRAG_KEY); if (s) dragPos = JSON.parse(s); } catch {}
    // seed a wavy recent history so the trend shows immediately on first tap
    for (const s of stations)
      history[s.id] = Array.from({ length: HIST_LEN }, (_, i) => +(s.base + Math.sin(i * 0.45) * 1.4).toFixed(1));
    history = history;
    histTimer = setInterval(() => {
      for (const s of stations) {
        const arr = history[s.id] ?? [];
        arr.push(+stationVal(s).toFixed(1));
        if (arr.length > HIST_LEN) arr.shift();
        history[s.id] = arr;
      }
      history = history;
    }, 2000);
  });
  onDestroy(() => { if (histTimer) clearInterval(histTimer); });

  function svgPoint(e: PointerEvent): { x: number; y: number } {
    const pt = svgEl.createSVGPoint(); pt.x = e.clientX; pt.y = e.clientY;
    const m = svgEl.getScreenCTM(); if (!m) return { x: 0, y: 0 };
    const p = pt.matrixTransform(m.inverse());
    return { x: p.x, y: p.y };
  }
  function startDrag(e: PointerEvent, s: Station) {
    dragId = s.id; dragMoved = false; startClient = { x: e.clientX, y: e.clientY };
    (e.target as Element).setPointerCapture?.(e.pointerId);
    e.stopPropagation(); e.preventDefault();
  }
  function onDrag(e: PointerEvent) {
    if (!dragId) return;
    if (Math.hypot(e.clientX - startClient.x, e.clientY - startClient.y) > 4) dragMoved = true;
    if (dragMoved) {
      const p = svgPoint(e);
      dragPos = { ...dragPos, [dragId]: { x: Math.round(p.x), y: Math.round(p.y) } };
    }
  }
  function endDrag() {
    if (!dragId) return;
    if (dragMoved) {
      try { localStorage.setItem(DRAG_KEY, JSON.stringify(dragPos)); } catch {}
    } else {
      selectedSensor = selectedSensor === dragId ? null : dragId;   // tap = toggle trend
    }
    dragId = null;
  }
</script>

<svelte:head><title>Building Plan (Concept)</title></svelte:head>

<!-- Preview banner -->
<div class="banner">
  <div><strong>PREVIEW</strong> · Top-Down Building View · sensors live (demo fallback) · /dashboard/plan</div>
  <div class="banner-actions">
    <button on:click={() => goto('/dashboard')}>↕ Cross-section view</button>
    <button on:click={() => goto('/')}>⌂ Home</button>
  </div>
</div>

<div class="stage">
  <!-- ── Operator title bar ── -->
  <div class="titlebar">
    <div class="tb-id">
      <span class="tb-name">Storage 1 · Building A</span>
      <span class="tb-clock">{dateStr} · {clockStr}</span>
    </div>
    <div class="tb-mode" style="background:{mode.tint}">
      <span class="dot" style="background:{mode.dot}"></span>
      <span class="tb-mode-label">{mode.label}</span>
    </div>
    <div class="tb-health" class:bad={alarmCount > 0}>
      <span class="hdot" class:ok={alarmCount === 0} class:alarm={alarmCount > 0}></span>
      {alarmCount > 0 ? `${alarmCount} Active Alarm${alarmCount > 1 ? 's' : ''}` : 'All Clear'}
    </div>
  </div>

  <!-- ── Stats + concept controls ── -->
  <div class="hdr">
    <div class="hdr-stats">
      <div class="stat"><span class="k">Outside</span><span class="v">{outsideTemp.toFixed(0)}°F</span><span class="u">{outsideHumid.toFixed(0)}%RH</span></div>
      <div class="stat"><span class="k">CO₂</span><span class="v">{co2.toFixed(0)}</span><span class="u">ppm</span></div>
      <div class="stat"><span class="k">Fan</span><span class="v">{fanPct}%</span></div>
    </div>
    <label class="door-ctl">
      <span class="dl">Doors</span>
      <input type="range" min="0" max="100" bind:value={doorsPct}/>
      <span class="dv">{doorsPct}%</span>
    </label>
    <button class="cavity-toggle" class:on={cavityHeat} on:click={() => cavityHeat = !cavityHeat}>
      <span class="cdot"></span> Cavity Heat {cavityHeat ? 'ON' : 'OFF'}
    </button>
    <button class="refrig-btn" data-st={refrigState} on:click={cycleRefrig}>
      <span class="cdot" style="background:{refrigColor}"></span>
      Refrig {refrigState === 'off' ? 'OFF' : refrigState === 'cool' ? 'COOL' : 'DEFROST'}
    </button>
    {#if refrigState !== 'off'}
      <label class="door-ctl">
        <span class="dl">Output</span>
        <input type="range" min="0" max="100" bind:value={refrigPct}/>
        <span class="dv">{refrigPct}%</span>
      </label>
    {/if}
    <button class="cavity-toggle" class:on={heatOn} on:click={() => heatOn = !heatOn}>
      <span class="cdot" style={heatOn ? 'background:#f97316;box-shadow:0 0 8px #f97316' : ''}></span>
      Heat {heatOn ? 'ON' : 'OFF'}
    </button>
    <button class="cavity-toggle" class:on={humidPump} on:click={() => humidPump = !humidPump}>
      <span class="cdot" style={humidPump ? 'background:#38bdf8;box-shadow:0 0 8px #38bdf8' : ''}></span>
      Humidify {humidPump ? 'ON' : 'OFF'}
    </button>
  </div>

  <svg viewBox="0 0 1200 760" class="plan" bind:this={svgEl} role="application" aria-label="Building plan"
       on:pointermove={onDrag} on:pointerup={endDrag} on:pointerleave={endDrag}>
    <defs>
      <linearGradient id="pile1" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%"  stop-color="#d9b377"/>
        <stop offset="55%" stop-color="#9b7240"/>
        <stop offset="100%" stop-color="#6b4824"/>
      </linearGradient>
      <linearGradient id="pile2" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%"  stop-color="#6b4824"/>
        <stop offset="45%" stop-color="#9b7240"/>
        <stop offset="100%" stop-color="#d9b377"/>
      </linearGradient>
      <linearGradient id="pileTan" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%"   stop-color="#e6d1a8"/>
        <stop offset="100%" stop-color="#caae80"/>
      </linearGradient>
      <!-- bulk russet potato pile: tightly packed lumps, mixed sizes,
           each shaded for roundness; dark base = shadow between spuds -->
      <radialGradient id="spud" cx="37%" cy="30%" r="72%">
        <stop offset="0%"   stop-color="#c2925e"/>
        <stop offset="50%"  stop-color="#97632f"/>
        <stop offset="100%" stop-color="#5b3a1c"/>
      </radialGradient>
      <radialGradient id="spud2" cx="40%" cy="30%" r="72%">
        <stop offset="0%"   stop-color="#b07c49"/>
        <stop offset="50%"  stop-color="#85531f"/>
        <stop offset="100%" stop-color="#4e3216"/>
      </radialGradient>
      <pattern id="potatoPile" width="88" height="70" patternUnits="userSpaceOnUse" patternTransform="rotate(2) scale(1.3)">
        <rect width="88" height="70" fill="#43290f"/>
        <g stroke="#371f0c" stroke-width="0.5" stroke-opacity="0.5">
          <ellipse cx="12" cy="10" rx="16" ry="12"  fill="url(#spud)"  transform="rotate(-14 12 10)"/>
          <ellipse cx="40" cy="8"  rx="12" ry="9"   fill="url(#spud2)" transform="rotate(10 40 8)"/>
          <ellipse cx="66" cy="12" rx="15" ry="11"  fill="url(#spud)"  transform="rotate(18 66 12)"/>
          <ellipse cx="26" cy="27" rx="13" ry="10"  fill="url(#spud2)" transform="rotate(-6 26 27)"/>
          <ellipse cx="52" cy="29" rx="15" ry="11"  fill="url(#spud)"  transform="rotate(8 52 29)"/>
          <ellipse cx="84" cy="28" rx="12" ry="9"   fill="url(#spud2)" transform="rotate(-20 84 28)"/>
          <ellipse cx="9"  cy="40" rx="13" ry="10"  fill="url(#spud)"  transform="rotate(6 9 40)"/>
          <ellipse cx="38" cy="47" rx="16" ry="12"  fill="url(#spud2)" transform="rotate(-16 38 47)"/>
          <ellipse cx="64" cy="44" rx="12" ry="9"   fill="url(#spud)"  transform="rotate(14 64 44)"/>
          <ellipse cx="84" cy="52" rx="14" ry="10"  fill="url(#spud)"  transform="rotate(24 84 52)"/>
          <ellipse cx="18" cy="58" rx="15" ry="11"  fill="url(#spud2)" transform="rotate(-4 18 58)"/>
          <ellipse cx="48" cy="64" rx="12" ry="9"   fill="url(#spud)"  transform="rotate(12 48 64)"/>
          <ellipse cx="72" cy="64" rx="15" ry="11"  fill="url(#spud2)" transform="rotate(-10 72 64)"/>
          <!-- mid-size spuds filling the gaps (between the bigs) -->
          <ellipse cx="32" cy="16" rx="10" ry="8"   fill="url(#spud)"  transform="rotate(20 32 16)"/>
          <ellipse cx="58" cy="16" rx="10" ry="7.5" fill="url(#spud2)" transform="rotate(-12 58 16)"/>
          <ellipse cx="44" cy="36" rx="10" ry="8"   fill="url(#spud)"  transform="rotate(6 44 36)"/>
          <ellipse cx="76" cy="38" rx="10" ry="7.5" fill="url(#spud2)" transform="rotate(-8 76 38)"/>
          <ellipse cx="28" cy="44" rx="9"  ry="7"   fill="url(#spud2)" transform="rotate(10 28 44)"/>
          <ellipse cx="60" cy="56" rx="10" ry="8"   fill="url(#spud)"  transform="rotate(-6 60 56)"/>
          <!-- horizontal wrap copies -->
          <ellipse cx="84" cy="28" rx="12" ry="9"  fill="url(#spud2)" transform="translate(-88,0) rotate(-20 84 28)"/>
          <ellipse cx="84" cy="52" rx="14" ry="10" fill="url(#spud)"  transform="translate(-88,0) rotate(24 84 52)"/>
          <ellipse cx="9"  cy="40" rx="13" ry="10" fill="url(#spud)"  transform="translate(88,0) rotate(6 9 40)"/>
          <ellipse cx="12" cy="10" rx="16" ry="12" fill="url(#spud)"  transform="translate(88,0) rotate(-14 12 10)"/>
          <!-- vertical wrap copies -->
          <ellipse cx="12" cy="10" rx="16" ry="12" fill="url(#spud)"  transform="translate(0,70) rotate(-14 12 10)"/>
          <ellipse cx="40" cy="8"  rx="12" ry="9"  fill="url(#spud2)" transform="translate(0,70) rotate(10 40 8)"/>
          <ellipse cx="66" cy="12" rx="15" ry="11" fill="url(#spud)"  transform="translate(0,70) rotate(18 66 12)"/>
          <ellipse cx="18" cy="58" rx="15" ry="11" fill="url(#spud2)" transform="translate(0,-70) rotate(-4 18 58)"/>
          <ellipse cx="48" cy="64" rx="12" ry="9"  fill="url(#spud)"  transform="translate(0,-70) rotate(12 48 64)"/>
          <ellipse cx="72" cy="64" rx="15" ry="11" fill="url(#spud2)" transform="translate(0,-70) rotate(-10 72 64)"/>
        </g>
      </pattern>
      <linearGradient id="spineGrad" x1="0" y1="0" x2="1" y2="0">
        <stop offset="0%"  stop-color="#dbeafe"/>
        <stop offset="100%" stop-color="#eff6ff"/>
      </linearGradient>
      <filter id="soft" x="-20%" y="-20%" width="140%" height="140%">
        <feDropShadow dx="0" dy="2" stdDeviation="3" flood-color="#0f172a" flood-opacity="0.18"/>
      </filter>
      <filter id="mistblur" x="-60%" y="-60%" width="220%" height="220%">
        <feGaussianBlur stdDeviation="1.6"/>
      </filter>
    </defs>

    <!-- ══ Cavity heat air envelope — double wall ══════════════════════
         Cold exterior wall ↔ interior sheet, with heated storage air
         circulating in the gap to insulate + prevent condensation. -->
    <!-- Cold exterior wall -->
    <rect x="48" y="70" width="1104" height="620" rx="14"
          fill="#dbe4ef" stroke="#1e293b" stroke-width="3"/>
    <!-- Cavity channel tint (warms toward red when cavity heat is on) -->
    <rect x="48" y="70" width="1104" height="620" rx="14"
          fill="none" stroke={cavityHeat ? '#fca5a5' : '#cbd5e1'}
          stroke-width="22" stroke-opacity="0.45"/>
    <!-- Interior sheet (the wall facing the pile) -->
    <rect x="74" y="96" width="1052" height="568" rx="9"
          fill="#fafaf9" stroke="#94a3b8" stroke-width="2"/>
    <!-- Heated storage air in the cavity — injected at the far-end (right)
         centre, splits and flows up-&-over and down-&-under around the pile,
         ending at the top and bottom of the doors. RED dashes MARCH only
         when the cavity-heat output is energized; faint+static when off. -->
    <path d="M1140,380 L1140,82 L60,82 L60,318"
          fill="none" stroke="#ef4444" stroke-width="3"
          stroke-linecap="round" stroke-linejoin="round"
          stroke-dasharray="9 23" class="flow-cavity" class:running={cavityHeat}/>
    <path d="M1140,380 L1140,678 L60,678 L60,456"
          fill="none" stroke="#ef4444" stroke-width="3"
          stroke-linecap="round" stroke-linejoin="round"
          stroke-dasharray="9 23" class="flow-cavity" class:running={cavityHeat}/>
    <!-- cavity-heat supply node at the far-end centre -->
    <circle cx="1140" cy="380" r="5" fill={cavityHeat ? '#ef4444' : '#cbd5e1'} class:pulse={cavityHeat}/>
    <text x="600" y="58" text-anchor="middle" class="env-label" class:hot={cavityHeat}>
      Cavity Heat Air Envelope {cavityHeat ? '· HEAT ON' : '· OFF'}
    </text>

    <!-- ── Bay 1 (top) ── -->
    <rect x={ROOM.x + 6} y={BAY1.y} width={ROOM.w - 12} height={BAY1.h} rx="6"
          fill="url(#potatoPile)" stroke="#9c855e" stroke-width="1.5"/>
    <text x={ROOM.x + 190} y={BAY1.y + 26} class="bay-label">BAY 1</text>

    <!-- Return air (temp / humidity / CO₂) — up by the doors, where the
         return comes back over the pile and down behind the fresh-air doors -->
    <g transform="translate({ROOM.x + 14},{ROOM.y + 14})">
      <rect x="0" y="0" width="152" height="80" rx="10"
            fill="#0b1220" fill-opacity="0.9" stroke="#38bdf8" stroke-width="1.2"/>
      <text x="12" y="19" class="ra-title">RETURN AIR</text>
      <text x="12"  y="39" class="ra-k">Temp</text>
      <text x="140" y="39" text-anchor="end" class="ra-v">{returnT.toFixed(1)}°</text>
      <text x="12"  y="57" class="ra-k">RH</text>
      <text x="140" y="57" text-anchor="end" class="ra-v">{returnH.toFixed(0)}%</text>
      <text x="12"  y="75" class="ra-k">CO₂</text>
      <text x="140" y="75" text-anchor="end" class="ra-v" fill={healthColor(co2Health(co2v))}>{co2v.toFixed(0)}<tspan class="ra-u"> ppm</tspan></text>
    </g>

    <!-- ── Bay 2 (bottom) ── -->
    <rect x={ROOM.x + 6} y={BAY2.y} width={ROOM.w - 12} height={BAY2.h} rx="6"
          fill="url(#potatoPile)" stroke="#9c855e" stroke-width="1.5"/>
    <text x={ROOM.x + 22} y={BAY2.y + BAY2.h - 14} class="bay-label">BAY 2</text>

    <!-- ── Central cavity spine ── -->
    <rect x={ROOM.x + 6} y={SPINE.y} width={ROOM.w - 12} height={SPINE.h} rx="6"
          fill="url(#spineGrad)" stroke="#60a5fa" stroke-width="1.5"/>
    <!-- supply airflow down the spine (left → right) -->
    {#each [0, 1, 2] as r (r)}
      <line x1={ROOM.x + 30} y1={SPINE.y + 18 + r * 12} x2={ROOM.x + ROOM.w - 120} y2={SPINE.y + 18 + r * 12}
            stroke="#3b82f6" stroke-width="2" stroke-dasharray="10 16"
            class="flow-spine" class:running={eq.fan} style="--d:{r * 0.4}s"/>
    {/each}

    <!-- ══ Storage recirculation (blue) — return pulled back over the top
         of the pile toward the door end, down behind the fresh-air door,
         and back through the equipment. Fan-driven. ══ -->
    {#each [BAY1, BAY2] as bay, bi (bi)}
      {@const retY = bi === 0 ? bay.y + 14 : bay.y + bay.h - 27}
      {#each [0, 1] as r (r)}
        <line x1={ROOM.x + ROOM.w - 130} y1={retY + r * 13}
              x2={ROOM.x + 152} y2={retY + r * 13}
              stroke="#38bdf8" stroke-width="2" stroke-dasharray="10 16"
              class="flow-return" class:running={eq.fan} style="--d:{r * 0.5}s"/>
      {/each}
      <path d="M {ROOM.x + 150} {retY + 7} l 11 -6 v 12 z"
            fill="#38bdf8" opacity={eq.fan ? 0.9 : 0.18}/>
      <text x={ROOM.x + ROOM.w - 150} y={retY - 4} text-anchor="end" class="flow-cap">return ▸ door</text>
    {/each}

    <!-- Plenum distribution: storage air ducted from the central plenum
         UP into bay 1 and DOWN into bay 2, through the pile. -->
    {#each [400, 580, 760, 920] as dx, di (dx)}
      <line x1={dx} y1={SPINE.y - 3} x2={dx} y2={BAY1.y + 30}
            stroke="#60a5fa" stroke-width="2.5" stroke-dasharray="8 12"
            class="flow-duct" class:running={eq.fan} style="--d:{di * 0.3}s"/>
      <path d="M{dx},{BAY1.y + 24} l -5,9 h10 z" fill="#60a5fa" opacity={eq.fan ? 0.9 : 0.15}/>
      <line x1={dx} y1={SPINE.y + SPINE.h + 3} x2={dx} y2={BAY2.y + BAY2.h - 30}
            stroke="#60a5fa" stroke-width="2.5" stroke-dasharray="8 12"
            class="flow-duct" class:running={eq.fan} style="--d:{di * 0.3}s"/>
      <path d="M{dx},{BAY2.y + BAY2.h - 24} l -5,-9 h10 z" fill="#60a5fa" opacity={eq.fan ? 0.9 : 0.15}/>
    {/each}

    <!-- ════ FRESH-AIR DOORS — set into the LEFT exterior (cavity-heat) wall.
         4 doors hinged at the inner wall face; they swing into the room and
         draw fresh air through the wall as the output % rises. ════ -->
    {#each [0, 1, 2, 3] as i (i)}
      {@const hy = 318 + i * 36}
      <!-- opening cut into the exterior / cavity-heat wall -->
      <rect x="48" y={hy} width="28" height="30" rx="2" fill="#0b1220" fill-opacity="0.45"/>
      <!-- fresh air drawn in through the opening when the door is open -->
      <line x1="26" y1={hy + 15} x2="104" y2={hy + 15}
            stroke="#7dd3fc" stroke-width="2" stroke-dasharray="6 9"
            class="flow-fresh" class:running={doorsPct > 5} style="--d:{i * 0.2}s"
            opacity={doorsPct > 5 ? 0.9 : 0}/>
      <!-- door leaf, hinged at the inner wall face (x≈74), swinging into the room -->
      <g transform="translate(74,{hy + 2})">
        <g class="door-leaf" style="transform: rotate({-(doorsPct / 100) * 78}deg)">
          <rect x="-2.6" y="0" width="5.2" height="30" rx="1.5" fill="#d7dde5" stroke="#475569" stroke-width="1"/>
          <rect x="-1.2" y="4" width="2.4" height="22" rx="1" fill="#aeb8c4"/>
          <circle cx="0" cy="26" r="1.3" fill="#334155"/>
        </g>
      </g>
    {/each}
    <text x="60" y="308" text-anchor="middle" class="eq-cap">Fresh-Air Doors</text>
    <text x="60" y="476" text-anchor="middle" class="eq-state" fill="#475569">{doorsPct}%</text>

    {#each [ ["Refrig", refrigState !== "off", "#0ea5e9"], ["Fan Wall", eq.fan, "#06b6d4"], ["Climacell", eq.climacell, "#38bdf8"] ] as [name, on, col], i (name)}
      <g transform="translate({ROOM.x + 16 + i * 106},{SPINE.y - 30})">
        <rect x="0" y="0" width="86" height="120" rx="6"
              fill={on ? col + "22" : "#f8fafc"} stroke={on ? col : "#cbd5e1"} stroke-width="1.5" filter="url(#soft)"/>
        <circle cx="14" cy="16" r="5" fill={on ? "#10b981" : "#cbd5e1"} class:pulse={on}/>
        <text x="43" y="22" text-anchor="middle" class="eq-name">{name}</text>

        {#if name === 'Fan Wall'}
          <!-- a literal wall of fans (the fans that drive the storage loop) -->
          {#each [46, 74, 102] as cy (cy)}
            <g transform="translate(43,{cy})">
              <circle r="13" fill="#0f172a" opacity="0.06"/>
              <g class="mini-fan" class:spin={on}>
                {#each [0, 72, 144, 216, 288] as a (a)}
                  <ellipse cx="0" cy="-7" rx="2.6" ry="7" fill={on ? '#0e7490' : '#94a3b8'} transform="rotate({a})"/>
                {/each}
                <circle r="3" fill="#1e293b"/>
              </g>
            </g>
          {/each}

        {:else if name === 'Refrig'}
          <!-- serpentine refrigeration coil: blue=cooling, red=defrost -->
          <g transform="translate(11,36)" class="coil"
             class:defrost={refrigState === 'defrost'}
             style="--glow:{glowPx}px; --gcol:{refrigColor};">
            <path d="M2,6 H56 a8,8 0 0 1 0,16 H2 a8,8 0 0 0 0,16 H56 a8,8 0 0 1 0,16 H2 a8,8 0 0 0 0,16 H56"
                  fill="none" stroke={refrigColor} stroke-width="4.5" stroke-linecap="round"/>
            <!-- coil fins -->
            {#each [8, 20, 32, 44] as fx (fx)}
              <line x1={fx} y1="0" x2={fx} y2="70" stroke={refrigColor} stroke-width="1" opacity="0.3"/>
            {/each}
          </g>
          <text x="43" y="116" text-anchor="middle" class="eq-state"
                fill={refrigState === 'defrost' ? '#b91c1c' : refrigState === 'off' ? '#94a3b8' : '#2563eb'}>
            {refrigState === 'off' ? 'off' : refrigState === 'cool' ? 'COOLING' : 'DEFROST'}
          </text>

        {:else if name === 'Climacell'}
          <!-- evaporative media (corrugated cardboard pads) + water sheeting down -->
          <clipPath id="cc-clip"><rect x="11" y="32" width="64" height="80" rx="3"/></clipPath>
          <rect x="11" y="32" width="64" height="80" rx="3" fill="#cdb992" stroke="#9c855e" stroke-width="1.5"/>
          <g clip-path="url(#cc-clip)" stroke="#9c855e" stroke-width="1" opacity="0.55">
            {#each [-64, -44, -24, -4, 16, 36, 56, 76] as o (o)}
              <line x1={11 + o} y1="32" x2={11 + o + 40} y2="112"/>
              <line x1={11 + o + 40} y1="32" x2={11 + o} y2="112"/>
            {/each}
          </g>
          {#if on}
            <g clip-path="url(#cc-clip)">
              {#each [ {x:18,d:'0s'}, {x:30,d:'0.7s'}, {x:42,d:'0.3s'}, {x:54,d:'0.9s'}, {x:64,d:'0.5s'} ] as w (w.x)}
                <rect class="drip" x={w.x} y="32" width="2.4" height="13" rx="1.2" fill="#38bdf8" style="animation-delay:{w.d}"/>
              {/each}
            </g>
          {/if}
        {/if}
      </g>
    {/each}

    <!-- ════ SPINE SENSORS + HEAT ════ -->
    <!-- Heat (between hum1 and hum2) — only exists when firing; pops up
         with a flickering flame on transition. -->
    {#if heatOn}
      <g transform="translate(573,{SPINE.y - 2})">
        <g class="heat-pop">
          <rect x="0" y="0" width="74" height="62" rx="7"
                fill="#fff7ed" stroke="#f97316" stroke-width="1.5" filter="url(#soft)"/>
          <g transform="translate(37,46)">
            <path class="flame f1" d="M0,0 C-10,-11 -7,-24 0,-33 C7,-24 10,-11 0,0 Z" fill="#ea580c"/>
            <path class="flame f2" d="M0,0 C-7,-8 -5,-17 0,-24 C5,-17 7,-8 0,0 Z" fill="#fb923c"/>
            <path class="flame f3" d="M0,0 C-4,-5 -3,-10 0,-15 C4,-10 3,-5 0,0 Z" fill="#fde68a"/>
          </g>
          <text x="37" y="58" text-anchor="middle" class="eq-cap" fill="#9a3412">Heat ON</text>
        </g>
      </g>
    {/if}

    {#each hums as h (h.id)}
      <!-- Plenum humidifier — SIDE PROFILE: water tub on the bottom, the
           spinning atomizer head above it seen edge-on, throwing a misty
           spray into the plenum air stream (left) when the pump is on. -->
      <g transform="translate({h.x},{SPINE.y})">
        <text x="0" y="7" text-anchor="middle"><tspan class="hum-label">{h.label}</tspan><tspan class="hum-val" dx="6">{h.v.toFixed(0)}%</tspan></text>

        <!-- misty spray into the air stream when the pump is on -->
        {#if humidPump}
          <!-- soft misty cloud billowing off the disc, downstream -->
          {#each [ {d:'0s'}, {d:'1.1s'}, {d:'2.2s'} ] as c (c.d)}
            <g class="mist-cloud" style="animation-delay:{c.d}" filter="url(#mistblur)">
              <circle cx="15" cy="22" r="6"   fill="#8ba2bd"/>
              <circle cx="22" cy="16" r="8.5" fill="#8ba2bd"/>
              <circle cx="24" cy="29" r="8"   fill="#8ba2bd"/>
              <circle cx="32" cy="21" r="8.5" fill="#8ba2bd"/>
              <circle cx="40" cy="23" r="6.5" fill="#8ba2bd"/>
            </g>
          {/each}
          <path class="spray-cone" d="M10,22 L32,12 Q37,22 32,32 Z" fill="#7dd3fc" opacity="0.16"/>
          {#each [ {y:15,d:'0s'}, {y:19,d:'0.3s'}, {y:23,d:'0.55s'}, {y:27,d:'0.85s'} ] as m (m.d)}
            <circle class="spray" cx="13" cy={m.y} r="1.5" fill="#7dd3fc" style="animation-delay:{m.d}"/>
          {/each}
        {/if}

        <!-- spinning atomizer head (side profile, disc edge-on) + motor -->
        <rect x="-12" y="14" width="15" height="17" rx="3" fill="#94a3b8" stroke="#64748b" stroke-width="1"/>
        <g class="hum-disc" class:spin={humidPump}>
          <ellipse cx="8" cy="22" rx="4" ry="13" fill="#bae6fd" stroke="#0891b2" stroke-width="1.5"/>
          <ellipse cx="8" cy="22" rx="2" ry="8"  fill="none"    stroke="#67e8f9" stroke-width="0.8"/>
        </g>
        <circle cx="8" cy="22" r="1.8" fill="#0e7490"/>

        <!-- water tub / sump on the bottom -->
        <rect x="-13" y="36" width="26" height="20" rx="2" fill="#cbd5e1" stroke="#64748b" stroke-width="1"/>
        {#each [40, 44, 48, 52] as sy (sy)}
          <line x1="-11" y1={sy} x2="11" y2={sy} stroke="#64748b" stroke-width="0.8" opacity="0.5"/>
        {/each}
      </g>
    {/each}

    <!-- PLENUM supply readout with setpoint-vs-actual. The plenum is the
         conditioned air delivered to the pile; SP = target, Δ = deviation. -->
    <g transform="translate(830,{SPINE.y - 1})">
      <rect x="-40" y="0" width="80" height="62" rx="8" fill="#0c4a6e" stroke="#0ea5e9" stroke-width="2" filter="url(#soft)"/>
      <text x="0" y="13" text-anchor="middle" class="plenum-hd">PLENUM</text>
      <text x="0" y="30" text-anchor="middle" class="plenum-t2">{plenumT.toFixed(1)}°</text>
      <text x="0" y="43" text-anchor="middle" class="plenum-sp">SP {tempSP.toFixed(0)}° <tspan
            fill={Math.abs(tDev) > 3 ? '#f87171' : Math.abs(tDev) > 1.5 ? '#fbbf24' : '#86efac'}
            >Δ{tDev >= 0 ? '+' : ''}{tDev.toFixed(1)}</tspan></text>
      <text x="0" y="56" text-anchor="middle" class="plenum-h2">{plenumH.toFixed(0)}% / {humidSP.toFixed(0)}% RH</text>
    </g>

    <!-- ════ PILE FANS (far end of each bay) ════ -->
    {#each [BAY1, BAY2] as bay, i (i)}
      <g transform="translate({ROOM.x + ROOM.w - 78},{bay.y + bay.h * 0.5})" class="fan-grp">
        <circle r="32" fill="#0f172a" opacity="0.06"/>
        <g class="fan-blades" class:spin={eq.fan}>
          {#each [0, 60, 120, 180, 240, 300] as a (a)}
            <ellipse cx="0" cy="-15" rx="6" ry="15" fill="#475569" transform="rotate({a})"/>
          {/each}
          <circle r="7" fill="#1e293b"/>
        </g>
        <text x="0" y="48" text-anchor="middle" class="eq-cap">Pile Fans</text>
        <text x="0" y="-40" text-anchor="middle" class="eq-cap">{eq.fan ? fanPct + "%" : "Off"}</text>
      </g>
    {/each}

    <!-- ════ BAY LIGHTS ════ -->
    {#each [ [BAY1, eq.lights1, "Bay Lights 1"], [BAY2, eq.lights2, "Bay Light 2"] ] as [bay, on, label], i (i)}
      <g transform="translate(600,{bay.y + bay.h * 0.5})">
        <circle r="11" fill={on ? "#fde047" : "#e2e8f0"} stroke={on ? "#eab308" : "#cbd5e1"} stroke-width="1.5"/>
        {#if on}
          {#each [0, 45, 90, 135, 180, 225, 270, 315] as a (a)}
            <line x1="0" y1="-14" x2="0" y2="-19" stroke="#eab308" stroke-width="2" transform="rotate({a})"/>
          {/each}
        {/if}
        <text x="0" y="34" text-anchor="middle" class="eq-cap">{label}</text>
      </g>
    {/each}

    <!-- bay drilldown click targets — UNDER the draggable badges -->
    <rect x={ROOM.x + 6} y={BAY1.y} width={ROOM.w - 12} height={BAY1.h}
          fill="transparent" class="bay-hit" on:click={() => goto('/dashboard')} role="button" tabindex="0"/>
    <rect x={ROOM.x + 6} y={BAY2.y} width={ROOM.w - 12} height={BAY2.h}
          fill="transparent" class="bay-hit" on:click={() => goto('/dashboard')} role="button" tabindex="0"/>

    <!-- ════ PILE SENSOR STATIONS (P1..P4 × 2 bays) — drag to place ════ -->
    {#each stations as s (s.id)}
      {@const xy = dragPos[s.id] ?? defaultXY(s)}
      {@const v = stationVal(s)}
      {@const hp = pileHealth(v)}
      <g transform="translate({xy.x},{xy.y})">
        <line x1="0" y1="0" x2="0" y2="-20" stroke="#1e293b" stroke-width="1" opacity="0.4"/>
        <circle cx="0" cy="0" r="3" fill={healthColor(hp)} opacity="0.8"/>
        <g transform="translate(0,-44)" class="pile-badge" class:dragging={dragId === s.id}
           on:pointerdown={(e) => startDrag(e, s)} role="button" tabindex="0">
          <rect x="-30" y="-15" width="60" height="30" rx="8" fill="#0f172a" filter="url(#soft)"/>
          <rect x="-30" y="-15" width="6" height="30" rx="3" fill={tempColor(v)}/>
          <!-- health border: green ok / amber warn / red (pulsing) alarm -->
          <rect x="-30" y="-15" width="60" height="30" rx="8" fill="none"
                stroke={healthColor(hp)} stroke-width={hp === 'ok' ? 1 : 2.5}
                class:badge-alarm={hp === 'alarm'}/>
          <text x="3" y="-2" text-anchor="middle" class="st-label">P{s.p}</text>
          <text x="3" y="10" text-anchor="middle" class="st-val" fill={hp === 'alarm' ? '#fecaca' : '#f8fafc'}>{v.toFixed(1)}°</text>
        </g>
      </g>
    {/each}

    <!-- ════ TAP-FOR-TREND sparkline popup ════ -->
    {#if selectedSensor}
      {@const st = stations.find((s) => s.id === selectedSensor)}
      {@const sxy = dragPos[selectedSensor] ?? defaultXY(st)}
      {@const hist = history[selectedSensor] ?? []}
      {@const mn = Math.min(...hist)}
      {@const mx = Math.max(...hist)}
      {@const rng = (mx - mn) || 1}
      {@const W = 116}
      {@const H = 32}
      {@const px = Math.max(ROOM.x + 4, Math.min(sxy.x - (W + 12) / 2, ROOM.x + ROOM.w - (W + 12) - 6))}
      {@const py = st.bay === 1 ? Math.max(ROOM.y + 2, sxy.y - 134) : sxy.y + 14}
      {@const last = hist[hist.length - 1] ?? mn}
      <g transform="translate({px},{py})" class="trend-pop">
        <rect x="0" y="0" width={W + 12} height="64" rx="8" fill="#0b1220" stroke="#38bdf8" stroke-width="1.5" filter="url(#soft)"/>
        <text x="8" y="15" class="trend-title">P{st.p} · Bay {st.bay} trend</text>
        <text x={W + 4} y="15" text-anchor="end" class="trend-cur" fill={healthColor(pileHealth(last))}>{last.toFixed(1)}°</text>
        <g transform="translate(6,22)">
          <polyline points={hist.map((v, i) => `${(i / Math.max(1, hist.length - 1)) * W},${H - ((v - mn) / rng) * H}`).join(' ')}
                    fill="none" stroke="#38bdf8" stroke-width="1.5" stroke-linejoin="round"/>
          <circle cx={W} cy={H - ((last - mn) / rng) * H} r="2.4" fill="#38bdf8"/>
        </g>
        <text x="8" y="61" class="trend-mm">min {mn.toFixed(1)}°  ·  max {mx.toFixed(1)}°</text>
        <text x={W + 4} y="61" text-anchor="end" class="trend-mm">tap ✕</text>
      </g>
    {/if}
  </svg>

  <!-- ── Legend ── -->
  <div class="legend">
    <span><span class="sw" style="background:hsl(210,80%,50%)"></span>cool pile</span>
    <span><span class="sw" style="background:hsl(0,80%,50%)"></span>warm pile</span>
    <span class="sep">|</span>
    <span><span class="sw" style="background:#10b981"></span>ok</span>
    <span><span class="sw" style="background:#f59e0b"></span>warn</span>
    <span><span class="sw" style="background:#ef4444"></span>alarm</span>
    <span class="sep">|</span>
    <span><span class="sw flow blue"></span>storage recirc</span>
    <span><span class="sw flow red"></span>cavity heat (gated)</span>
    <span class="hint">tap a probe → trend · drag to place · click a bay → cross-section</span>
  </div>
</div>

<style>
  .banner {
    display:flex; justify-content:space-between; align-items:center;
    background:#fff7ed; border-bottom:2px solid #fb923c; color:#7c2d12;
    font-size:13px; padding:4px 12px;
  }
  .banner-actions button {
    background:#fff; border:1px solid #fdba74; color:#9a3412;
    border-radius:6px; padding:2px 10px; margin-left:8px; font-size:12px; cursor:pointer;
  }
  .banner-actions button:hover { background:#ffedd5; }

  .stage {
    background:linear-gradient(160deg,#0f172a 0%,#1e293b 100%);
    min-height:calc(100vh - 30px); padding:14px 18px 22px; color:#e2e8f0;
  }

  /* operator title bar */
  .titlebar {
    display:flex; align-items:center; gap:16px;
    background:#0b1220; border:1px solid #1e293b; border-radius:12px;
    padding:8px 16px; margin-bottom:10px;
  }
  .tb-id { display:flex; flex-direction:column; line-height:1.15; }
  .tb-name { font-size:16px; font-weight:800; color:#f1f5f9; letter-spacing:.01em; }
  .tb-clock { font-size:12px; color:#94a3b8; font-variant-numeric:tabular-nums; }
  .tb-mode {
    margin-left:auto; display:flex; align-items:center; gap:9px;
    padding:8px 18px; border-radius:999px; color:#0f172a; font-weight:700;
  }
  .tb-mode-label { font-size:16px; }
  .tb-health {
    display:flex; align-items:center; gap:8px; font-size:14px; font-weight:700;
    padding:8px 16px; border-radius:999px;
    background:#052e1a; color:#86efac; border:1px solid #166534;
  }
  .tb-health.bad { background:#3a0d0d; color:#fecaca; border-color:#b91c1c; }
  .hdot { width:11px; height:11px; border-radius:50%; }
  .hdot.ok { background:#22c55e; box-shadow:0 0 8px #22c55e; }
  .hdot.alarm { background:#ef4444; box-shadow:0 0 8px #ef4444; animation:pulse 1.1s ease-in-out infinite; }

  .hdr { display:flex; align-items:center; gap:18px; margin-bottom:10px; }
  .dot { width:10px; height:10px; border-radius:50%; box-shadow:0 0 8px currentColor; }
  .hdr-stats { display:flex; gap:10px; flex-wrap:wrap; }
  .stat {
    display:flex; align-items:baseline; gap:5px;
    background:#0b1220; border:1px solid #334155; border-radius:8px; padding:5px 12px;
  }
  .stat .k { font-size:11px; color:#94a3b8; text-transform:uppercase; letter-spacing:.05em; }
  .stat .v { font-size:18px; font-weight:700; color:#f8fafc; }
  .stat .u { font-size:11px; color:#64748b; }

  .plan {
    width:100%; height:auto; max-height:74vh;
    background:#0b1220; border:1px solid #1e293b; border-radius:12px;
  }

  .door-ctl {
    display:flex; align-items:center; gap:8px; margin-left:auto;
    background:#0b1220; border:1px solid #334155; border-radius:999px;
    padding:5px 14px; font-size:13px; color:#cbd5e1;
  }
  .door-ctl .dl { color:#94a3b8; text-transform:uppercase; letter-spacing:.05em; font-size:11px; }
  .door-ctl .dv { font-weight:700; color:#f8fafc; min-width:38px; text-align:right; }
  .door-ctl input[type=range] { accent-color:#60a5fa; width:120px; }

  /* Top-hinged door leaves: pivot about their own top edge, animate
     smoothly as the door output % changes. */
  :global(.plan .door-leaf) {
    transform-box: fill-box; transform-origin: 50% 0%;
    transition: transform .6s cubic-bezier(.34,.01,.2,1);
  }
  :global(.plan .mini-fan) { transform-box: fill-box; transform-origin: center; }
  :global(.plan .mini-fan.spin) { animation: spin 1.1s linear infinite; }

  /* refrigeration coil glow — intensity scales with --glow (output %) */
  :global(.plan .coil) { filter: drop-shadow(0 0 var(--glow, 0px) var(--gcol, #3b82f6)); }
  :global(.plan .coil.defrost) { animation: coilpulse 1.3s ease-in-out infinite; }
  @keyframes coilpulse {
    0%,100% { filter: drop-shadow(0 0 calc(var(--glow) * 0.35) var(--gcol)); }
    50%     { filter: drop-shadow(0 0 var(--glow) var(--gcol)); }
  }

  /* heat box pop-up + flickering flame */
  :global(.plan .heat-pop) {
    transform-box: fill-box; transform-origin: center bottom;
    animation: heatpop .42s cubic-bezier(.2,1.5,.4,1);
  }
  @keyframes heatpop {
    0%   { transform: scale(.1); opacity: 0; }
    60%  { opacity: 1; }
    100% { transform: scale(1); opacity: 1; }
  }
  :global(.plan .flame) { transform-box: fill-box; transform-origin: bottom center; }
  :global(.plan .flame.f1) { animation: flick .7s ease-in-out infinite alternate; }
  :global(.plan .flame.f2) { animation: flick .5s ease-in-out infinite alternate; }
  :global(.plan .flame.f3) { animation: flick .4s ease-in-out infinite alternate; }
  @keyframes flick {
    from { transform: scaleY(.82) scaleX(1.06); }
    to   { transform: scaleY(1.12) scaleX(.94); }
  }
  /* climacell water sheeting down the evaporative media */
  :global(.plan .drip) { transform-box: fill-box; animation: drip 1.7s linear infinite; }
  @keyframes drip {
    0%   { transform: translateY(0);    opacity: 0; }
    12%  { opacity: .85; }
    88%  { opacity: .85; }
    100% { transform: translateY(78px); opacity: 0; }
  }

  .refrig-btn {
    display:flex; align-items:center; gap:7px;
    background:#0b1220; border:1px solid #334155; color:#cbd5e1;
    border-radius:999px; padding:6px 14px; font-size:13px; font-weight:600; cursor:pointer;
  }
  .refrig-btn[data-st="cool"]    { border-color:#3b82f6; color:#bfdbfe; }
  .refrig-btn[data-st="defrost"] { border-color:#ef4444; color:#fecaca; }

  .cavity-toggle {
    display:flex; align-items:center; gap:7px;
    background:#0b1220; border:1px solid #334155; color:#94a3b8;
    border-radius:999px; padding:6px 14px; font-size:13px; font-weight:600; cursor:pointer;
  }
  .cavity-toggle .cdot { width:10px; height:10px; border-radius:50%; background:#475569; }
  .cavity-toggle.on { border-color:#ef4444; color:#fecaca; }
  .cavity-toggle.on .cdot { background:#ef4444; box-shadow:0 0 8px #ef4444; }

  /* SVG text classes */
  :global(.plan .env-label) { font-size:13px; fill:#93c5fd; letter-spacing:.12em; text-transform:uppercase; }
  :global(.plan .env-label.hot) { fill:#fca5a5; }
  :global(.plan .flow-cap) { font-size:9px; fill:#38bdf8; letter-spacing:.04em; opacity:.7; }
  :global(.plan .bay-label) { font-size:15px; font-weight:700; fill:#3b2a14; letter-spacing:.08em; }
  :global(.plan .eq-name)  { font-size:12px; font-weight:600; fill:#1e293b; }
  :global(.plan .eq-state) { font-size:10px; font-weight:700; letter-spacing:.05em; }
  :global(.plan .eq-cap)   { font-size:11px; font-weight:600; fill:#cbd5e1; }
  :global(.plan .hum-label){ font-size:9px;  fill:#0e7490; font-weight:600; }
  :global(.plan .hum-val)  { font-size:11px; fill:#0891b2; font-weight:700; }
  /* atomizer disc seen edge-on: width oscillates to read as spinning,
     while staying in side profile (never opens to a full face). */
  :global(.plan .hum-disc) { transform-box: fill-box; transform-origin: center; }
  :global(.plan .hum-disc.spin) { animation: discspin .14s linear infinite; }
  @keyframes discspin {
    0%   { transform: scaleX(.35); }
    50%  { transform: scaleX(1);   }
    100% { transform: scaleX(.35); }
  }
  /* soft misty cloud drifting downstream off the disc */
  :global(.plan .mist-cloud) {
    transform-box: fill-box; transform-origin: 15px 22px;
    animation: mistcloud 3.3s ease-out infinite;
  }
  @keyframes mistcloud {
    0%   { transform: translate(0,0)    scale(.4);  opacity: 0;   }
    20%  { opacity: .95; }
    70%  { opacity: .8; }
    100% { transform: translate(42px,-4px) scale(1.8); opacity: 0; }
  }
  /* misty spray droplets fanning into the air stream */
  :global(.plan .spray) { transform-box: fill-box; animation: spray .9s ease-out infinite; }
  @keyframes spray {
    0%   { transform: translate(0,0);   opacity: 0; }
    20%  { opacity: .85; }
    100% { transform: translate(20px,0); opacity: 0; }
  }
  :global(.plan .plenum-hd){ font-size:8px;  fill:#7dd3fc; font-weight:800; letter-spacing:.14em; }
  :global(.plan .plenum-t2){ font-size:15px; fill:#ffffff; font-weight:800; }
  :global(.plan .plenum-h2){ font-size:9px;  fill:#bae6fd; font-weight:600; }
  :global(.plan .plenum-sp){ font-size:8.5px; fill:#7dd3fc; font-weight:600; }
  :global(.plan .ra-title) { font-size:9px;  fill:#7dd3fc; font-weight:700; letter-spacing:.12em; }
  :global(.plan .ra-k)     { font-size:10px; fill:#94a3b8; }
  :global(.plan .ra-v)     { font-size:13px; fill:#f8fafc; font-weight:700; }
  :global(.plan .ra-u)     { font-size:9px;  fill:#64748b; font-weight:400; }
  :global(.plan .st-label) { font-size:9px; fill:#94a3b8; font-weight:600; }
  :global(.plan .st-val)   { font-size:12px; fill:#f8fafc; font-weight:700; }

  .bay-hit { cursor:pointer; }
  .bay-hit:hover { fill:rgba(255,255,255,0.05); }
  :global(.plan .pile-badge) { cursor: grab; touch-action: none; }
  :global(.plan .pile-badge.dragging) { cursor: grabbing; }
  :global(.plan .badge-alarm) { animation: pulse 1s ease-in-out infinite; }
  :global(.plan .trend-pop) { pointer-events: none; }
  :global(.plan .trend-title) { font-size:9px;  fill:#94a3b8; font-weight:700; }
  :global(.plan .trend-cur)   { font-size:13px; fill:#38bdf8; font-weight:800; }
  :global(.plan .trend-mm)    { font-size:8px;  fill:#64748b; }

  /* animations */
  /* Cavity heat: red dots ALWAYS present (it's the envelope) but only
     MARCH when the cavity-heat output is energized. */
  .flow-cavity { stroke-dashoffset:0; opacity:.22; }
  .flow-cavity.running { opacity:1; animation:march 2.2s linear infinite; }
  .flow-spine { stroke-dashoffset:0; opacity:0; }
  .flow-spine.running { opacity:.9; animation:march 1.6s linear infinite; animation-delay:var(--d); }
  .flow-return { stroke-dashoffset:0; opacity:0; }
  .flow-return.running { opacity:.85; animation:march 1.9s linear infinite; animation-delay:var(--d); }
  .flow-duct { stroke-dashoffset:0; opacity:0; }
  .flow-duct.running { opacity:.8; animation:march 1.7s linear infinite; animation-delay:var(--d); }
  .flow-fresh { stroke-dashoffset:0; }
  .flow-fresh.running { animation:march 1.4s linear infinite; animation-delay:var(--d); }
  @keyframes march { to { stroke-dashoffset:-72; } }

  .fan-blades { transform-box: fill-box; transform-origin: center; }
  .fan-blades.spin { animation:spin 1.1s linear infinite; }
  @keyframes spin { to { transform:rotate(360deg); } }

  :global(.plan .pulse) { animation:pulse 1.6s ease-in-out infinite; }
  @keyframes pulse { 0%,100%{opacity:1;} 50%{opacity:.4;} }

  .legend {
    display:flex; gap:18px; align-items:center; margin-top:10px;
    font-size:12px; color:#94a3b8;
  }
  .legend .sw { display:inline-block; width:14px; height:14px; border-radius:3px; margin-right:5px; vertical-align:-2px; }
  .legend .sw.flow.blue { background:repeating-linear-gradient(90deg,#38bdf8 0 6px,transparent 6px 11px); }
  .legend .sw.flow.red  { background:repeating-linear-gradient(90deg,#ef4444 0 6px,transparent 6px 11px); }
  .legend .hint { margin-left:auto; font-style:italic; }
  .legend .sep { color:#334155; }
</style>
