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
  import { systemStatus, sensorData, warningReport, plenumSettings, equipmentStatus } from "$lib/business/protoStores";
  import { navigationStore, frontMatterStore, modeToColorStore, localeStore } from "$lib/store";
  import { locale } from "svelte-i18n";
  import { INPUT_GOOD, OUTPUT_ON } from "$lib/business/mode";
  import { EQ } from "$lib/business/equipmentEnum";
  import PlenumSetpointsForm from "$lib/components/PlenumSetpointsForm.svelte";
  import HumidifierControlForm from "$lib/components/HumidifierControlForm.svelte";
  import RefrigerationForm from "$lib/components/RefrigerationForm.svelte";

  // ─── Language switch — reuses the app's svelte-i18n + persisted localeStore.
  //   Adding a language = one row here + a matching locales/<code>.json
  //   (registered in lib/i18n.ts). `tag` is a kiosk-safe 2-char marker, NOT a
  //   flag emoji — Pi/Linux Chromium renders flag emoji as "US"/"CN" letterboxes.
  const LANGS = [
    { code: "en", tag: "EN", label: "English" },
    { code: "es", tag: "ES", label: "Español" },
    { code: "fr", tag: "FR", label: "Français" },
    { code: "zh", tag: "中", label: "中文" },
  ];
  let langOpen = false;
  $: curLang = LANGS.find((l) => ($locale ?? "en").toLowerCase().startsWith(l.code)) ?? LANGS[0];
  function setLang(code: string) { localeStore.set(code); locale.set(code); langOpen = false; }

  // ─── TEMPORARY access unlock ──────────────────────────────────────
  // The form gates editing on `navigationStore.level > 0`; the dashboard
  // runs at level 0, so program/settings fields are read-only. This
  // button bumps the level directly so equipment can be programmed from
  // the spatial UI during bring-up.
  // TODO(auth): replace with Azure website account permissions + auto
  //   sign-in over Bluetooth — no manual unlock button in production.
  $: programUnlocked = $navigationStore.level > 0;
  function toggleProgram() {
    $navigationStore.level = programUnlocked ? 0 : 2;
  }

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
  $: clockStr = now.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit", second: "2-digit" });
  $: dateStr  = now.toLocaleDateString([], { month: "short", day: "numeric" });

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
  $: wr = $warningReport;
  $: alarms = (wr as any)?.entries ?? (wr as any)?.alarms ?? [];
  $: alarmCount = Array.isArray(alarms) ? alarms.length : 0;
  $: pls = $plenumSettings;
  $: tempSP = (pls as any)?.tempSetpoint;

  // readouts
  $: outsideTemp = ss?.outsideTemp;
  $: plenumT = ss?.plenumTemp;
  $: plenumH = ss?.plenumHumid;
  $: returnT = ss?.returnTemp;
  $: returnH = ss?.returnHumid;
  $: co2v = ss?.co2Level;
  let locked = false;   // edit-lock: when on, pieces can't be dragged

  // ─── Equipment settings modal ─────────────────────────────────────
  //   Generic dispatcher: clicking an equipment hotspot sets `activeModal`
  //   to a key; the modal renders the matching shared *Form.svelte (the
  //   SAME component its classic page uses — real writeProto save path).
  //   Adding a migrated form = one entry in MODAL_TITLES + one hotspot +
  //   one {:else if} branch. docs/spatial-ui-page-migration.md
  type ModalKey = 'plenum' | 'humidifier' | 'refrig';
  const MODAL_TITLES: Record<ModalKey, string> = {
    plenum: 'Plenum Setpoints & Alarms',
    humidifier: 'Humidifier Control',
    refrig: 'Refrigeration Setup',
  };
  let activeModal: ModalKey | null = null;
  let modalUnit: number | null = null;  // for per-unit forms (e.g. humidifier head)
  let justMoved = false;   // distinguishes a tap (open) from a drag (move)
  let modalForm: { flush: () => Promise<void> } | undefined;
  let saving = false;
  function openModal(key: ModalKey, unit: number | null = null) {
    if (justMoved) return;   // a drag just ended — don't treat as a tap
    modalUnit = unit;
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
    if (persist && modalForm) {
      saving = true;
      try { await modalForm.flush(); }
      catch (e) { console.error('[plan3d] settings save failed:', e); }
      finally { saving = false; }
    }
    activeModal = null;
    modalUnit = null;
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
  $: cavityHeat = !!eqOut[EQ.CAVITY_HEAT];
  $: heatOn = !!eqOut[EQ.HEAT];
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
  $: refrigColor = refrigState === "defrost" ? "#ef4444" : refrigState === "off" ? "#94a3b8" : "#3b82f6";

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
  const th = 34;                     // ridge half-width
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

<div class="stage">
  <!-- title bar -->
  <div class="titlebar">
    <div class="tb-id">
      <span class="tb-name">Storage 1 · Building A</span>
      <span class="tb-clock">{dateStr} · {clockStr} · iso view</span>
    </div>
    <div class="tb-mode" style="border-color:{mode.color}">
      <span class="dot" style="background:{mode.color}"></span>
      <span class="tb-mode-label" style="color:{mode.color}">{mode.label}</span>
    </div>
    <div class="tb-right">
    <div class="tb-health" class:bad={alarmCount > 0}>
      <span class="hdot" class:ok={alarmCount === 0} class:alarm={alarmCount > 0}></span>
      {alarmCount > 0 ? `${alarmCount} Active Alarm${alarmCount > 1 ? 's' : ''}` : 'All Clear'}
    </div>
    <!-- language switch (top-right) — drives the app-wide locale -->
    <div class="tb-lang">
      <button class="lang-btn" on:click={() => (langOpen = !langOpen)} aria-haspopup="listbox" aria-expanded={langOpen}>
        <span class="lang-flag">{curLang.tag}</span>
        <span class="lang-name">{curLang.label}</span>
        <span class="lang-caret">▾</span>
      </button>
      {#if langOpen}
        <button class="lang-scrim" aria-label="Close language menu" on:click={() => (langOpen = false)}></button>
        <div class="lang-menu" role="listbox">
          {#each LANGS as l (l.code)}
            <button class="lang-item" class:active={l.code === curLang.code} role="option"
                    aria-selected={l.code === curLang.code} on:click={() => setLang(l.code)}>
              <span class="lang-flag">{l.tag}</span> {l.label}
            </button>
          {/each}
        </div>
      {/if}
    </div>
    </div>
  </div>

  <!-- stats + a few concept controls -->
  <div class="hdr">
    <div class="hdr-stats">
      <div class="stat"><span class="k">Outside</span><span class="v">{f0(outsideTemp)}°F</span></div>
      <div class="stat"><span class="k">Plenum</span><span class="v">{f1(plenumT)}°</span><span class="u">SP {f0(tempSP)}</span></div>
      <div class="stat"><span class="k">CO₂</span><span class="v">{f0(co2v)}</span><span class="u">ppm</span></div>
      <div class="stat"><span class="k">Fan</span><span class="v">{fanDisplay}</span></div>
    </div>
    <!-- Door readout moved onto the door graphic in the 3D scene. -->
    <!-- Cavity-heat / refrigeration / heat / humidifier status pills removed —
         those run live in the 3D scene now (wall tint, coil, flame, mist) and
         in the center mode indicator. Header keeps only the door readout +
         functional controls (lock, program). -->
    <button class="lock-btn" class:on={locked} on:click={() => locked = !locked}>
      {locked ? '🔒 Locked' : '🔓 Unlocked'}
    </button>
    <button class="prog-btn" class:on={programUnlocked} on:click={toggleProgram}
      title="TEMPORARY: unlock program/settings editing. Production = Azure account permissions + Bluetooth auto sign-in.">
      {programUnlocked ? '⚙ Program: ON' : '⚙ Program: OFF'}
    </button>
  </div>

  <svg viewBox="0 0 1200 470" class="plan" class:locked bind:this={svgEl} role="application" aria-label="Building plan 3D"
       style="--fan-dur:{fanSpinDur}s; --fan-play:{fanSpinning ? 'running' : 'paused'}"
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
         on:pointerdown={(e) => startDrag(e, 'eq-' + eqb.type, eqb.x, spineCy)}
         on:click={() => { if (eqb.type === 'refrig') openModal('refrig'); }} role="button" tabindex="0">
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
          {#each ccDrops as drop (drop.id)}
            <ellipse class="cc-drop" cx={drop.x} cy="-80" rx="1.5" ry="2.7" fill="#7dd3fc"
                     style="animation-delay:{drop.delay}s; animation-duration:{drop.dur}s"/>
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
    <!-- live door readout ON the graphic (PWM_DOORS %) -->
    <g transform="translate({doorTagPt[0]},{doorTagPt[1]})" class="door-tag">
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
        <text x="0" y="-2" text-anchor="middle" class="s-val" fill={hp === 'alarm' ? '#fecaca' : '#f8fafc'}>{f1(v)}°</text>
      </g>
    {/each}

    <!-- ═══ PLENUM readout card (draggable; click/tap opens setpoints) ═══ -->
    <g transform="translate({plenumPt[0]},{plenumPt[1]})" class="drag3"
       on:pointerdown={(e) => startDrag(e, 'plenum', 150, spineCy)}
       on:click={() => openModal('plenum')} role="button" tabindex="0">
      <rect x="-36" y="-30" width="72" height="46" rx="8" fill="#0c4a6e" stroke="#0ea5e9" stroke-width="2" filter="url(#soft3)"/>
      <text x="0" y="-17" text-anchor="middle" class="card-hd">PLENUM ⚙</text>
      <text x="0" y="0"  text-anchor="middle" class="card-big">{f1(plenumT)}°</text>
      <text x="0" y="12" text-anchor="middle" class="card-sm">{f0(plenumH)}% RH</text>
    </g>

    <!-- ═══ RETURN AIR readout card (draggable) ═══ -->
    <g transform="translate({returnPt[0]},{returnPt[1]})" class="drag3"
       on:pointerdown={(e) => startDrag(e, 'return', 470, spineCy)} role="button" tabindex="0">
      <rect x="-42" y="-34" width="84" height="60" rx="8" fill="#0b1220" stroke="#38bdf8" stroke-width="1.5" filter="url(#soft3)"/>
      <text x="-34" y="-20" class="card-hd2">RETURN AIR</text>
      <text x="-34" y="-5"  class="card-k">Temp</text><text x="36" y="-5" text-anchor="end" class="card-v">{f1(returnT)}°</text>
      <text x="-34" y="8"   class="card-k">RH</text><text x="36" y="8" text-anchor="end" class="card-v">{f0(returnH)}%</text>
      <text x="-34" y="21"  class="card-k">CO₂</text><text x="36" y="21" text-anchor="end" class="card-v" fill={healthColor(co2Health(co2v))}>{f0(co2v)}</text>
    </g>
  </svg>

  <div class="legend">
    <span><span class="sw" style="background:#10b981"></span>ok</span>
    <span><span class="sw" style="background:#f59e0b"></span>warn</span>
    <span><span class="sw" style="background:#ef4444"></span>alarm</span>
    <span class="hint">click the PLENUM card → setpoints · isometric prototype</span>
  </div>

  <!-- ═══ EQUIPMENT SETTINGS POPUP — shared *Form.svelte (real save path) ═══
       Anything but Cancel autosaves: overlay / X / Save → flush, Cancel → discard. -->
  {#if activeModal}
    <div class="ovl" role="presentation" on:click={() => closeModal(true)}>
      <div class="dlg" role="dialog" aria-modal="true" on:click|stopPropagation on:keydown|stopPropagation>
        <header class="dlg-hd">
          <span>{modalTitle}</span>
          <button class="dlg-x" title="Save & close" on:click={() => closeModal(true)}>×</button>
        </header>
        <div class="dlg-body">
          {#if activeModal === 'plenum'}
            <PlenumSetpointsForm bind:this={modalForm} embedded theme="dark" canEdit={programUnlocked} />
          {:else if activeModal === 'humidifier'}
            <HumidifierControlForm bind:this={modalForm} embedded theme="dark" canEdit={programUnlocked} unit={modalUnit} />
          {:else if activeModal === 'refrig'}
            <RefrigerationForm bind:this={modalForm} embedded theme="dark" canEdit={programUnlocked} />
          {/if}
        </div>
        <footer class="dlg-ft">
          <span class="ft-tag">edits autosave on close — Cancel to discard</span>
          <button class="btn ghost" on:click={() => closeModal(false)} disabled={saving}>Cancel</button>
          <button class="btn save" on:click={() => closeModal(true)} disabled={saving}>
            {saving ? 'Saving…' : 'Save & Close'}
          </button>
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

  .titlebar { position:relative; display:flex; align-items:center; justify-content:space-between; gap:16px; background:#0b1220; border:1px solid #1e293b; border-radius:12px; padding:8px 16px; margin-bottom:10px; }
  .tb-id { display:flex; flex-direction:column; line-height:1.15; }
  .tb-name { font-size:16px; font-weight:800; color:#f1f5f9; }
  .tb-clock { font-size:12px; color:#94a3b8; font-variant-numeric:tabular-nums; }
  /* center mode indicator — the color-changing centerpiece (border + dot +
     label all driven by the live mode colour). Absolutely centred so the
     left id-block and right health-pill don't shift it. */
  .tb-mode { position:absolute; left:50%; top:50%; transform:translate(-50%,-50%);
    display:flex; align-items:center; gap:11px; padding:8px 26px; border-radius:999px;
    background:#0b1220; border:2px solid #334155; font-weight:800; white-space:nowrap; }
  .tb-mode .dot { width:14px; height:14px; }
  .tb-mode-label { font-size:20px; letter-spacing:.02em; text-transform:uppercase; }
  .dot { width:10px; height:10px; border-radius:50%; box-shadow:0 0 8px currentColor; }
  .tb-health { display:flex; align-items:center; gap:8px; font-size:14px; font-weight:700; padding:8px 16px; border-radius:999px; background:#052e1a; color:#86efac; border:1px solid #166534; }
  .tb-health.bad { background:#3a0d0d; color:#fecaca; border-color:#b91c1c; }
  .tb-right { display:flex; align-items:center; gap:12px; }
  .tb-lang { position:relative; }
  .lang-btn { display:flex; align-items:center; gap:7px; background:#0b1220; border:1px solid #334155; color:#cbd5e1; border-radius:999px; padding:7px 12px; font-size:13px; font-weight:600; cursor:pointer; }
  .lang-btn:hover { border-color:#475569; }
  /* kiosk-safe code badge (no flag emoji — Pi Chromium can't render those) */
  .lang-flag { display:inline-flex; align-items:center; justify-content:center; min-width:24px; height:18px; padding:0 4px; font-size:11px; font-weight:800; line-height:1; letter-spacing:0.3px; color:#93c5fd; background:#16233b; border:1px solid #334155; border-radius:5px; }
  .lang-caret { font-size:10px; color:#64748b; }
  .lang-scrim { position:fixed; inset:0; background:transparent; border:none; z-index:40; cursor:default; }
  .lang-menu { position:absolute; right:0; top:calc(100% + 6px); z-index:41; background:#0f1827; border:1px solid #334155; border-radius:10px; padding:4px; min-width:148px; box-shadow:0 10px 30px rgba(0,0,0,0.5); }
  .lang-item { display:flex; align-items:center; gap:8px; width:100%; text-align:left; background:none; border:none; color:#cbd5e1; border-radius:7px; padding:8px 10px; font-size:14px; cursor:pointer; }
  .lang-item:hover { background:#1e293b; }
  .lang-item.active { color:#7dd3fc; font-weight:700; }
  .hdot { width:11px; height:11px; border-radius:50%; }
  .hdot.ok { background:#22c55e; box-shadow:0 0 8px #22c55e; }
  .hdot.alarm { background:#ef4444; box-shadow:0 0 8px #ef4444; animation:pulse 1.1s ease-in-out infinite; }

  .hdr { display:flex; align-items:center; gap:14px; margin-bottom:10px; flex-wrap:wrap; }
  .hdr-stats { display:flex; gap:10px; }
  .stat { display:flex; align-items:baseline; gap:5px; background:#0b1220; border:1px solid #334155; border-radius:8px; padding:5px 12px; }
  .stat .k { font-size:11px; color:#94a3b8; text-transform:uppercase; letter-spacing:.05em; }
  .stat .v { font-size:18px; font-weight:700; color:#f8fafc; }
  .stat .u { font-size:11px; color:#64748b; }
  /* on-graphic door readout billboard */
  :global(.plan .door-tag .dtag-k) { font-size:8px; fill:#94a3b8; font-weight:700; letter-spacing:.04em; }
  :global(.plan .door-tag .dtag-v) { font-size:13px; font-weight:800; }
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
  .lock-btn { background:#0b1220; border:1px solid #334155; color:#cbd5e1; border-radius:999px; padding:6px 14px; font-size:13px; font-weight:600; cursor:pointer; }
  .lock-btn.on { border-color:#f59e0b; color:#fde68a; background:#3a2a07; }
  /* TEMPORARY program-access toggle (see toggleProgram). */
  .prog-btn { background:#0b1220; border:1px solid #334155; color:#cbd5e1; border-radius:999px; padding:6px 14px; font-size:13px; font-weight:600; cursor:pointer; }
  .prog-btn.on { border-color:#10b981; color:#bbf7d0; background:#06281d; }
  :global(.plan .card-hd)  { font-size:8px;  fill:#7dd3fc; font-weight:800; letter-spacing:.12em; }
  :global(.plan .card-big) { font-size:15px; fill:#ffffff; font-weight:800; }
  :global(.plan .card-sm)  { font-size:9px;  fill:#bae6fd; font-weight:600; }
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
  .dlg-hd { display:flex; align-items:center; justify-content:space-between; padding:13px 18px; background:#0c4a6e; color:#e0f2fe; font-size:16px; font-weight:800; flex:0 0 auto; }
  .dlg-x { background:none; border:none; color:#bae6fd; font-size:22px; line-height:1; cursor:pointer; }
  .dlg-body { flex:1 1 auto; overflow:auto; background:#0f1827; padding:10px 16px 4px; }
  .dlg-ft { display:flex; align-items:center; gap:10px; padding:12px 18px; background:#0b1220; border-top:1px solid #1e293b; flex:0 0 auto; }
  .ft-tag { font-size:11px; color:#64748b; font-style:italic; margin-right:auto; }
  .btn { border-radius:8px; padding:8px 18px; font-size:14px; font-weight:600; cursor:pointer; border:1px solid #334155; }
  .btn:disabled { opacity:.5; cursor:default; }
  .btn.ghost { background:#1e293b; color:#cbd5e1; }
  .btn.save { background:#0ea5e9; color:#04293b; border-color:#0ea5e9; }
  /* The form's dark skin now lives inside PlenumSetpointsForm (theme="dark"),
     so the modal only owns its own chrome (overlay / header / footer). */

  .legend { display:flex; gap:16px; align-items:center; margin-top:10px; font-size:12px; color:#94a3b8; }
  .legend .sw { display:inline-block; width:14px; height:14px; border-radius:3px; margin-right:5px; vertical-align:-2px; }
  .legend .hint { margin-left:auto; font-style:italic; }
</style>
