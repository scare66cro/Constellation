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
  import { systemStatus, sensorData, warningReport, plenumSettings } from "$lib/business/protoStores";

  // ─── Live proto data (read-only) — live value, else faked drift ────
  $: ss = $systemStatus;
  $: sd = $sensorData;
  const lv = (live: number | null | undefined, fake: number): number =>
    (live != null && Number.isFinite(live)) ? live : fake;
  const sensorAt = (arr: any[] | undefined, i: number): number | null => {
    const r = arr?.[i];
    return (r && r.valid !== false && Number.isFinite(r.value)) ? r.value : null;
  };

  // tick for gentle drift
  let t = 0;
  let timer: ReturnType<typeof setInterval> | null = null;
  onMount(() => { timer = setInterval(() => (t += 1), 1200); });
  onDestroy(() => { if (timer) clearInterval(timer); });
  const wob = (seed: number, amp: number): number => Math.sin((t + seed * 1.7) * 0.5) * amp;

  // ─── Header: clock / mode / alarms / setpoints ────────────────────
  let now = new Date();
  let clk: ReturnType<typeof setInterval> | null = null;
  onMount(() => { clk = setInterval(() => (now = new Date()), 1000); });
  onDestroy(() => { if (clk) clearInterval(clk); });
  $: clockStr = now.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", second: "2-digit" });
  $: dateStr  = now.toLocaleDateString([], { month: "short", day: "numeric" });

  function stateToMode(s: number) {
    switch (s) {
      case 28: return { label: "Refrigeration", dot: "#0ea5e9", tint: "#f0f9ff" };
      case 5: case 24: return { label: "Curing", dot: "#f59e0b", tint: "#fffbeb" };
      case 18: return { label: "CO₂ Purge", dot: "#14b8a6", tint: "#f0fdfa" };
      case 34: return { label: "Standby", dot: "#94a3b8", tint: "#f8fafc" };
      case 33: return { label: "Shutdown", dot: "#ef4444", tint: "#fef2f2" };
      case 11: return { label: "Failure", dot: "#b91c1c", tint: "#fee2e2" };
      default: return { label: "Cooling", dot: "#10b981", tint: "#ecfdf5" };
    }
  }
  $: mode = stateToMode(ss?.systemState ?? 6);
  $: wr = $warningReport;
  $: alarms = (wr as any)?.entries ?? (wr as any)?.alarms ?? [];
  $: alarmCount = Array.isArray(alarms) ? alarms.length : 0;
  $: pls = $plenumSettings;
  $: tempSP = lv((pls as any)?.tempSetpoint, 42);

  // readouts
  $: outsideTemp = lv(ss?.outsideTemp, 47);
  $: plenumT = lv(ss?.plenumTemp, 41 + wob(5, 0.5));
  $: plenumH = lv(ss?.plenumHumid, 92 + wob(6, 1.2));
  $: returnT = lv(ss?.returnTemp, 52 + wob(7, 0.6));
  $: returnH = lv(ss?.returnHumid, 85 + wob(8, 1.2));
  $: co2v = lv(ss?.co2Level, 640 + Math.round(wob(9, 18)));
  let locked = false;   // edit-lock: when on, pieces can't be dragged

  // ─── Mock settings popup (clicking the PLENUM card opens the plenum
  //   setpoints page as a modal — mirrors level1/plentemp fields). ──
  let settingsOpen = false;
  let justMoved = false;   // distinguishes a tap (open) from a drag (move)
  let mSetT = 42, mSetH = 95, mUseRamp = false;
  let mRampRate = 0.5, mRampHrs = 4, mRampTarget = 38;
  let mAloDeg = 3, mAloMin = 30, mAhiDeg = 3, mAhiMin = 30;
  let mSaved = false;
  function saveMock() { mSaved = true; setTimeout(() => { mSaved = false; settingsOpen = false; }, 900); }

  // ─── Sensor health ────────────────────────────────────────────────
  type Health = "ok" | "warn" | "alarm";
  const pileHealth = (v: number): Health => (v >= 53 ? "alarm" : v >= 48 ? "warn" : "ok");
  const co2Health = (v: number): Health => (v >= 1500 ? "alarm" : v >= 1000 ? "warn" : "ok");
  const healthColor = (h: Health): string => (h === "alarm" ? "#ef4444" : h === "warn" ? "#f59e0b" : "#10b981");

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
  $: stationVal = (s: Station): number => sensorAt(sd?.temperatures, s.idx) ?? (s.base + wob(s.base, 0.6));

  // concept equipment state (toggles drive the iso scene)
  let doorsPct = 100;
  let cavityHeat = true;
  let refrigState: "off" | "cool" | "defrost" = "cool";
  const cycleRefrig = () => { refrigState = refrigState === "off" ? "cool" : refrigState === "cool" ? "defrost" : "off"; };
  $: refrigColor = refrigState === "defrost" ? "#ef4444" : refrigState === "off" ? "#94a3b8" : "#3b82f6";
  let humidPump = true;
  let heatOn = false;

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
  const th = 34;                     // ridge half-width
  const rim = 20;                    // perimeter rim height (cavity envelope)
  const doorWallH = 104;             // door-end wall height (x = L end, faces camera)

  const bayCenterY = (bay: 1 | 2) => (bay === 1 ? (BAY1.y0 + BAY1.y1) / 2 : (BAY2.y0 + BAY2.y1) / 2);
  const spineCy = (SPINE.y0 + SPINE.y1) / 2;
  const finXs = Array.from({ length: 13 }, (_, i) => -34 + i * 5.7);   // refrig coil fins
  const fluteXs = Array.from({ length: 10 }, (_, i) => -33 + i * 7.3);  // climacell media flutes
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
  interface Spud { x: number; y: number; z: number; r: number; g: 1 | 2; depth: number; }
  function genSpuds(bay: { y0: number; y1: number; h: number }): Spud[] {
    const out: Spud[] = [];
    const cy = (bay.y0 + bay.y1) / 2;
    for (let x = 6; x < L - 1; x += 15) {
      for (let y = cy - th - 16; y < bay.y1 + 16; y += 12) {   // light cover over the top (slope loop below packs the faces)
        const jx = x + (rnd() - 0.5) * 14;
        const jy = y + (rnd() - 0.5) * 9;
        const z = surfH(bay, jy) + (rnd() - 0.5) * 4;
        out.push({ x: jx, y: jy, z, r: 7 + rnd() * 5, g: rnd() > 0.5 ? 1 : 2, depth: jx + jy });
      }
    }
    // extra fill packed onto the steep front slope (crest → base)
    for (let x = 12; x < L - 1; x += 13) {
      for (let y = cy + th - 2; y < bay.y1 + 14; y += 6) {
        const jx = x + (rnd() - 0.5) * 13;
        const jy = y + (rnd() - 0.5) * 6;
        const z = surfH(bay, jy) + (rnd() - 0.5) * 3;
        out.push({ x: jx, y: jy, z, r: 6 + rnd() * 5, g: rnd() > 0.5 ? 1 : 2, depth: jx + jy });
      }
    }
    return out.sort((a, b) => a.depth - b.depth);  // far → near
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
    if (locked) return;
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
    const nx = Math.max(10, Math.min(L - 10, grabWorld.x + (dxy + sxy) / 2));
    const ny = Math.max(2,  Math.min(D - 2,  grabWorld.y + (sxy - dxy) / 2));
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

<div class="stage">
  <!-- title bar -->
  <div class="titlebar">
    <div class="tb-id">
      <span class="tb-name">Storage 1 · Building A</span>
      <span class="tb-clock">{dateStr} · {clockStr} · iso view</span>
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

  <!-- stats + a few concept controls -->
  <div class="hdr">
    <div class="hdr-stats">
      <div class="stat"><span class="k">Outside</span><span class="v">{outsideTemp.toFixed(0)}°F</span></div>
      <div class="stat"><span class="k">Plenum</span><span class="v">{plenumT.toFixed(1)}°</span><span class="u">SP {tempSP.toFixed(0)}</span></div>
      <div class="stat"><span class="k">CO₂</span><span class="v">{co2v.toFixed(0)}</span><span class="u">ppm</span></div>
    </div>
    <label class="door-ctl">
      <span class="dl">Doors</span>
      <input type="range" min="0" max="100" bind:value={doorsPct}/>
      <span class="dv">{doorsPct}%</span>
    </label>
    <button class="cavity-toggle" class:on={cavityHeat} on:click={() => cavityHeat = !cavityHeat}>
      <span class="cdot" style={cavityHeat ? 'background:#ef4444;box-shadow:0 0 8px #ef4444' : ''}></span>
      Cavity Heat {cavityHeat ? 'ON' : 'OFF'}
    </button>
    <button class="refrig-btn" data-st={refrigState} on:click={cycleRefrig}>
      <span class="cdot" style="background:{refrigColor}"></span>
      Refrig {refrigState === 'off' ? 'OFF' : refrigState === 'cool' ? 'COOL' : 'DEFROST'}
    </button>
    <button class="cavity-toggle" class:on={heatOn} on:click={() => heatOn = !heatOn}>
      <span class="cdot" style={heatOn ? 'background:#f97316;box-shadow:0 0 8px #f97316' : ''}></span>
      Heat {heatOn ? 'ON' : 'OFF'}
    </button>
    <button class="cavity-toggle" class:on={humidPump} on:click={() => humidPump = !humidPump}>
      <span class="cdot" style={humidPump ? 'background:#38bdf8;box-shadow:0 0 8px #38bdf8' : ''}></span>
      Humidify {humidPump ? 'ON' : 'OFF'}
    </button>
    <button class="lock-btn" class:on={locked} on:click={() => locked = !locked}>
      {locked ? '🔒 Locked' : '🔓 Unlocked'}
    </button>
  </div>

  <svg viewBox="0 0 1200 470" class="plan" class:locked bind:this={svgEl} role="application" aria-label="Building plan 3D"
       on:pointermove={onDrag} on:pointerup={endDrag} on:pointerleave={endDrag}>
    <defs>
      <linearGradient id="spudA" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stop-color="#c89863"/><stop offset="55%" stop-color="#9a6836"/><stop offset="100%" stop-color="#5e3d20"/>
      </linearGradient>
      <linearGradient id="spudB" x1="0" y1="0" x2="0" y2="1">
        <stop offset="0%" stop-color="#b3814e"/><stop offset="55%" stop-color="#855420"/><stop offset="100%" stop-color="#523618"/>
      </linearGradient>
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
      <!-- scattered russet potatoes over the visible surface -->
      {#each M.spuds as s (s.x + '_' + s.y)}
        {@const c = P(s.x, s.y, s.z)}
        <ellipse cx={c[0]} cy={c[1]} rx={s.r} ry={s.r * 0.82}
                 fill={s.g === 1 ? 'url(#spudA)' : 'url(#spudB)'} stroke="#3a2510" stroke-width="0.5"/>
      {/each}
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
         on:pointerdown={(e) => startDrag(e, 'eq-' + eqb.type, eqb.x, spineCy)} role="button" tabindex="0">
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
          {#each [-26, -10, 6, 22] as u, k (u)}
            <rect class="cc-drip" x={u} y="-82" width="2.4" height="12" rx="1" fill="#7dd3fc" opacity="0.8" style="animation-delay:{k * 0.4}s"/>
          {/each}
        {/if}
      </g>
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

    <!-- ═══ DUCTING: plenum air ducted up/down into the piles ═══ -->
    {#each [BAY1, BAY2] as bay (bay.y0)}
      {@const cyb = (bay.y0 + bay.y1) / 2}
      {@const sEdge = bay === BAY1 ? SPINE.y0 : SPINE.y1}
      {#each [180, 340, 500] as x (x)}
        {@const a = P(x, sEdge, 8)}
        {@const c = P(x, cyb, bay.h * 0.7)}
        <line x1={a[0]} y1={a[1]} x2={c[0]} y2={c[1]} stroke="#60a5fa" stroke-width="2"
              stroke-dasharray="7 10" class="duct3" opacity="0.8"/>
      {/each}
    {/each}

    <!-- ═══ HUMIDIFIERS + 3D mist on the plenum ═══ -->
    {#each [315, 220, 125] as hx (hx)}
      {@const hp2 = dragPos['hum-' + hx] ?? { x: hx, y: spineCy }}
      {@const base = P(hp2.x, hp2.y, 0)}
      <ellipse cx={base[0]} cy={base[1]} rx="13" ry="6" fill="#0f172a" opacity="0.45"/>
      <!-- aligned along the plenum; draggable. -->
      <g transform="matrix({A} {B} 0 1 {base[0]} {base[1]})" class="drag3"
         on:pointerdown={(e) => startDrag(e, 'hum-' + hx, hx, spineCy)} role="button" tabindex="0">
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
        <!-- mist off the disc, toward the door -->
        {#if humidPump}
          {#each [0, 1, 2] as k (k)}
            <g class="mistP" style="animation-delay:{k * 1.0}s" filter="url(#mist3)">
              <circle cx="-20" cy="-40" r="6"   fill="#8ba2bd"/>
              <circle cx="-16" cy="-33" r="7"   fill="#8ba2bd"/>
              <circle cx="-25" cy="-36" r="6.5" fill="#8ba2bd"/>
            </g>
          {/each}
        {/if}
      </g>
    {/each}

    <!-- ═══ HEAT flame on the plenum ═══ -->
    {#if heatOn}
      {@const he = dragPos['heat'] ?? { x: 385, y: spineCy }}
      {@const fp = P(he.x, he.y, 16)}
      <g transform="translate({fp[0]},{fp[1]})" class="drag3"
         on:pointerdown={(e) => startDrag(e, 'heat', 385, spineCy)} role="button" tabindex="0">
        <g class="heat3">
          <path class="flame3 f1" d="M0,0 C-9,-10 -6,-22 0,-30 C6,-22 9,-10 0,0 Z" fill="#ea580c"/>
          <path class="flame3 f2" d="M0,0 C-6,-7 -4,-15 0,-21 C4,-15 6,-7 0,0 Z" fill="#fb923c"/>
          <path class="flame3 f3" d="M0,0 C-3,-4 -2,-9 0,-13 C3,-9 3,-4 0,0 Z" fill="#fde68a"/>
        </g>
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
      {@const hp = pileHealth(v)}
      <line x1={base[0]} y1={base[1]} x2={top[0]} y2={top[1]} stroke="#0f172a" stroke-width="1.5" opacity="0.6"/>
      <circle cx={base[0]} cy={base[1]} r="3" fill={healthColor(hp)}/>
      <g transform="translate({top[0]},{top[1]})" class="pbadge3" class:dragging={dragId === s.id}
         on:pointerdown={(e) => startDrag(e, s.id, 72 + (s.p - 1) * (L - 150) / 3, bayCenterY(s.bay))} role="button" tabindex="0">
        <rect x="-26" y="-26" width="52" height="28" rx="7" fill="#0b1220" filter="url(#soft3)"/>
        <rect x="-26" y="-26" width="52" height="28" rx="7" fill="none" stroke={healthColor(hp)} stroke-width={hp === 'ok' ? 1 : 2.5} class:b-alarm={hp === 'alarm'}/>
        <text x="0" y="-14" text-anchor="middle" class="s-lbl">P{s.p} · B{s.bay}</text>
        <text x="0" y="-2" text-anchor="middle" class="s-val" fill={hp === 'alarm' ? '#fecaca' : '#f8fafc'}>{v.toFixed(1)}°</text>
      </g>
    {/each}

    <!-- ═══ PLENUM readout card (draggable; click/tap opens setpoints) ═══ -->
    <g transform="translate({plenumPt[0]},{plenumPt[1]})" class="drag3"
       on:pointerdown={(e) => startDrag(e, 'plenum', 150, spineCy)}
       on:click={() => { if (!justMoved) settingsOpen = true; }} role="button" tabindex="0">
      <rect x="-36" y="-30" width="72" height="46" rx="8" fill="#0c4a6e" stroke="#0ea5e9" stroke-width="2" filter="url(#soft3)"/>
      <text x="0" y="-17" text-anchor="middle" class="card-hd">PLENUM ⚙</text>
      <text x="0" y="0"  text-anchor="middle" class="card-big">{plenumT.toFixed(1)}°</text>
      <text x="0" y="12" text-anchor="middle" class="card-sm">{plenumH.toFixed(0)}% RH</text>
    </g>

    <!-- ═══ RETURN AIR readout card (draggable) ═══ -->
    <g transform="translate({returnPt[0]},{returnPt[1]})" class="drag3"
       on:pointerdown={(e) => startDrag(e, 'return', 470, spineCy)} role="button" tabindex="0">
      <rect x="-42" y="-34" width="84" height="60" rx="8" fill="#0b1220" stroke="#38bdf8" stroke-width="1.5" filter="url(#soft3)"/>
      <text x="-34" y="-20" class="card-hd2">RETURN AIR</text>
      <text x="-34" y="-5"  class="card-k">Temp</text><text x="36" y="-5" text-anchor="end" class="card-v">{returnT.toFixed(1)}°</text>
      <text x="-34" y="8"   class="card-k">RH</text><text x="36" y="8" text-anchor="end" class="card-v">{returnH.toFixed(0)}%</text>
      <text x="-34" y="21"  class="card-k">CO₂</text><text x="36" y="21" text-anchor="end" class="card-v" fill={healthColor(co2Health(co2v))}>{co2v.toFixed(0)}</text>
    </g>
  </svg>

  <div class="legend">
    <span><span class="sw" style="background:#10b981"></span>ok</span>
    <span><span class="sw" style="background:#f59e0b"></span>warn</span>
    <span><span class="sw" style="background:#ef4444"></span>alarm</span>
    <span class="hint">click the PLENUM card → setpoints · isometric prototype</span>
  </div>

  <!-- ═══ MOCK SETTINGS POPUP — Plenum Setpoints & Alarms (level1/plentemp) ═══ -->
  {#if settingsOpen}
    <div class="ovl" role="presentation" on:click={() => settingsOpen = false}>
      <div class="dlg" role="dialog" aria-modal="true" on:click|stopPropagation on:keydown|stopPropagation>
        <header class="dlg-hd">
          <span>Plenum Setpoints &amp; Alarms</span>
          <button class="dlg-x" on:click={() => settingsOpen = false}>×</button>
        </header>
        <div class="dlg-body">
          <div class="fld">
            <label for="sp-t">Plenum Temperature Setpoint</label>
            <div class="inp"><input id="sp-t" type="number" bind:value={mSetT}/><span>°F</span></div>
          </div>
          <div class="fld">
            <label for="sp-h">Plenum Humidity Setpoint</label>
            <div class="inp"><input id="sp-h" type="number" bind:value={mSetH}/><span>%</span></div>
          </div>
          <label class="chk"><input type="checkbox" bind:checked={mUseRamp}/> Use ramp rate</label>
          {#if mUseRamp}
            <div class="ramp">
              Change setpoint <input type="number" bind:value={mRampRate}/>°F every
              <input type="number" bind:value={mRampHrs}/> h until it reaches
              <input type="number" bind:value={mRampTarget}/>°F.
            </div>
          {/if}

          <div class="sec">Alarms</div>
          <p class="alarm">
            Alarm if plenum temperature is <strong>below</strong> setpoint by
            <input type="number" bind:value={mAloDeg}/>°F for
            <input type="number" bind:value={mAloMin}/> min, <em>or</em>
            <strong>above</strong> setpoint by
            <input type="number" bind:value={mAhiDeg}/>°F for
            <input type="number" bind:value={mAhiMin}/> min.
          </p>
        </div>
        <footer class="dlg-ft">
          <span class="mock-tag">mock · not saved to controller</span>
          {#if mSaved}<span class="saved-ok">✓ Saved</span>{/if}
          <button class="btn ghost" on:click={() => settingsOpen = false}>Cancel</button>
          <button class="btn save" on:click={saveMock}>Save</button>
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

  .titlebar { display:flex; align-items:center; gap:16px; background:#0b1220; border:1px solid #1e293b; border-radius:12px; padding:8px 16px; margin-bottom:10px; }
  .tb-id { display:flex; flex-direction:column; line-height:1.15; }
  .tb-name { font-size:16px; font-weight:800; color:#f1f5f9; }
  .tb-clock { font-size:12px; color:#94a3b8; font-variant-numeric:tabular-nums; }
  .tb-mode { margin-left:auto; display:flex; align-items:center; gap:9px; padding:8px 18px; border-radius:999px; color:#0f172a; font-weight:700; }
  .tb-mode-label { font-size:16px; }
  .dot { width:10px; height:10px; border-radius:50%; box-shadow:0 0 8px currentColor; }
  .tb-health { display:flex; align-items:center; gap:8px; font-size:14px; font-weight:700; padding:8px 16px; border-radius:999px; background:#052e1a; color:#86efac; border:1px solid #166534; }
  .tb-health.bad { background:#3a0d0d; color:#fecaca; border-color:#b91c1c; }
  .hdot { width:11px; height:11px; border-radius:50%; }
  .hdot.ok { background:#22c55e; box-shadow:0 0 8px #22c55e; }
  .hdot.alarm { background:#ef4444; box-shadow:0 0 8px #ef4444; animation:pulse 1.1s ease-in-out infinite; }

  .hdr { display:flex; align-items:center; gap:14px; margin-bottom:10px; flex-wrap:wrap; }
  .hdr-stats { display:flex; gap:10px; }
  .stat { display:flex; align-items:baseline; gap:5px; background:#0b1220; border:1px solid #334155; border-radius:8px; padding:5px 12px; }
  .stat .k { font-size:11px; color:#94a3b8; text-transform:uppercase; letter-spacing:.05em; }
  .stat .v { font-size:18px; font-weight:700; color:#f8fafc; }
  .stat .u { font-size:11px; color:#64748b; }
  .door-ctl { display:flex; align-items:center; gap:8px; margin-left:auto; background:#0b1220; border:1px solid #334155; border-radius:999px; padding:5px 14px; font-size:13px; color:#cbd5e1; }
  .door-ctl .dl { color:#94a3b8; text-transform:uppercase; font-size:11px; }
  .door-ctl .dv { font-weight:700; color:#f8fafc; min-width:38px; text-align:right; }
  .door-ctl input[type=range] { accent-color:#60a5fa; width:110px; }
  .cavity-toggle, .refrig-btn { display:flex; align-items:center; gap:7px; background:#0b1220; border:1px solid #334155; color:#cbd5e1; border-radius:999px; padding:6px 14px; font-size:13px; font-weight:600; cursor:pointer; }
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
  .lock-btn { background:#0b1220; border:1px solid #334155; color:#cbd5e1; border-radius:999px; padding:6px 14px; font-size:13px; font-weight:600; cursor:pointer; }
  .lock-btn.on { border-color:#f59e0b; color:#fde68a; background:#3a2a07; }
  :global(.plan .card-hd)  { font-size:8px;  fill:#7dd3fc; font-weight:800; letter-spacing:.12em; }
  :global(.plan .card-big) { font-size:15px; fill:#ffffff; font-weight:800; }
  :global(.plan .card-sm)  { font-size:9px;  fill:#bae6fd; font-weight:600; }
  :global(.plan .card-hd2) { font-size:8px;  fill:#7dd3fc; font-weight:800; letter-spacing:.12em; }
  :global(.plan .card-k)   { font-size:9px;  fill:#94a3b8; }
  :global(.plan .card-v)   { font-size:11px; fill:#f8fafc; font-weight:700; }
  :global(.plan .fan3-b) { transform-box:fill-box; transform-origin:center; animation:spin3 1.1s linear infinite; }
  @keyframes spin3 { to { transform:rotate(360deg); } }
  :global(.plan .duct3) { animation: marchD 1.6s linear infinite; }
  @keyframes marchD { to { stroke-dashoffset:-34; } }
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
  :global(.plan .cc-drip) { transform-box:fill-box; animation: ccdrip 1.4s linear infinite; }
  @keyframes ccdrip { 0%{transform:translateY(0);opacity:0} 20%{opacity:.85} 100%{transform:translateY(46px);opacity:0} }
  @keyframes pulse { 0%,100%{opacity:1;} 50%{opacity:.4;} }

  /* mock settings modal */
  .ovl { position:fixed; inset:0; background:rgba(2,6,23,0.6); display:flex; align-items:center; justify-content:center; z-index:50; }
  .dlg { width:440px; max-width:92vw; background:#0f1827; border:1px solid #334155; border-radius:14px; box-shadow:0 20px 60px rgba(0,0,0,0.5); overflow:hidden; }
  .dlg-hd { display:flex; align-items:center; justify-content:space-between; padding:13px 18px; background:#0c4a6e; color:#e0f2fe; font-size:16px; font-weight:800; }
  .dlg-x { background:none; border:none; color:#bae6fd; font-size:22px; line-height:1; cursor:pointer; }
  .dlg-body { padding:16px 18px; color:#e2e8f0; }
  .fld { display:flex; align-items:center; justify-content:space-between; margin-bottom:12px; }
  .fld label { font-size:14px; color:#cbd5e1; }
  .inp { display:flex; align-items:center; gap:6px; }
  .inp input, .ramp input, .alarm input { width:62px; background:#0b1220; border:1px solid #475569; color:#f8fafc; border-radius:7px; padding:6px 8px; font-size:15px; text-align:center; }
  .inp span { color:#64748b; font-size:13px; }
  .chk { display:flex; align-items:center; gap:8px; font-size:14px; color:#cbd5e1; margin:4px 0 12px; }
  .ramp { background:#0b1220; border:1px solid #1e293b; border-radius:8px; padding:10px 12px; font-size:13px; line-height:2; color:#cbd5e1; margin-bottom:12px; }
  .ramp input { width:54px; }
  .sec { font-size:12px; font-weight:700; color:#7dd3fc; text-transform:uppercase; letter-spacing:.08em; border-top:1px solid #1e293b; padding-top:12px; margin-bottom:8px; }
  .alarm { font-size:13px; line-height:2.1; color:#cbd5e1; margin:0; }
  .alarm input { width:50px; }
  .alarm strong { color:#f8fafc; } .alarm em { color:#94a3b8; font-style:normal; }
  .dlg-ft { display:flex; align-items:center; gap:10px; padding:12px 18px; background:#0b1220; border-top:1px solid #1e293b; }
  .mock-tag { font-size:11px; color:#64748b; font-style:italic; margin-right:auto; }
  .saved-ok { color:#86efac; font-size:13px; font-weight:700; }
  .btn { border-radius:8px; padding:8px 18px; font-size:14px; font-weight:600; cursor:pointer; border:1px solid #334155; }
  .btn.ghost { background:#1e293b; color:#cbd5e1; }
  .btn.save { background:#0ea5e9; color:#04293b; border-color:#0ea5e9; }

  .legend { display:flex; gap:16px; align-items:center; margin-top:10px; font-size:12px; color:#94a3b8; }
  .legend .sw { display:inline-block; width:14px; height:14px; border-radius:3px; margin-right:5px; vertical-align:-2px; }
  .legend .hint { margin-left:auto; font-style:italic; }
</style>
