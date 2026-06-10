# Spatial-UI page migration — catalog + checklist

> **Goal.** The `spatial-ui` branch makes the one-screen dashboard
> (`/dashboard/plan3d`) *the* operator UI. Each classic settings page's
> **body** is extracted into a shared `*Form.svelte` rendered in **two
> presentations**: the classic touchscreen page (untouched) and a
> dashboard modal. One source of truth, two skins — **do not rebuild
> pages.**
>
> **Status:** in progress. Started 2026-06-05. Check items off as forms land.
> Project memory: `memories` / `project-spatial-ui`. Concept + vision:
> [`memories`]. Deployed live on the Pi5 at
> `http://10.47.27.108/dashboard/plan3d`.

## Access model (Monitor / L1 / L2) — design contract

The dashboard is **view-only by default**. A TEMPORARY ⚙ Program button in the
plan3d titlebar **cycles Monitor → Program L1 → Program L2 → Monitor**:

| Program state | `navigationStore.level` | Editable |
|---|---|---|
| Monitor (view only) | 0 | nothing |
| Program Level 1 | 1 | `level1/*` forms |
| Program Level 2 | 2 | `level1/*` + `level2/*` forms |

A form's required level = its route origin. Editing is gated through the
`modalCanEdit` reactive (`programLevel >= MODAL_LEVEL[key]`) in
`plan3d/+page.svelte` — **not** a plain boolean unlock. **When migrating a
page, add its key to `MODAL_LEVEL` with the right level** (level1=1, level2=2).
Production: this level→edit mapping is the seam Azure account permissions
(auto sign-in over Bluetooth) plug into — keep it intact, just replace the
manual button. Memory: `project-spatial-ui-access-levels`.

**Tabbed modals** are the agreed grouping pattern: related pages live as tabs
under one hotspot (door = Fresh-Air + GDC; refrig = Settings + TRITON; climacell
= Run Clock + Config). Each tab is a `*Form.svelte` with its own `flush()`;
flush all dirty tabs on close. Design new modals tab-ready.

## Tabbed modals (built 2026-06-08)

`MODAL_TABS: Partial<Record<ModalKey, {id,label}[]>>` in `plan3d/+page.svelte`.
A key with an entry renders a tab strip; each tab renders its own
`*Form.svelte` (`bind:this={tabForms[id]}`). **All tabs stay mounted** (toggled
with `.tab-panel.hidden`, not `{#if}`) so edits survive a tab switch, and the
single **Save & Close** flushes every tab via `Promise.all(Object.values(
tabForms).map(f => f?.flush()))` — each `flush()` is a no-op when its sub-form
isn't dirty. Tab strip CSS (`.modal-tabs`/`.mtab`) lives in the plan3d `<style>`.
First user: `door` (**Outside Air** + Fresh-Air Door + GDC Stages). Add a
multi-page hotspot by listing its tabs here + a render branch per `tab.id`.

**Per-tab access level.** A `Tab` may carry its own `level?: 1|2`; when omitted
it inherits `MODAL_LEVEL[key]`. Tabs can therefore MIX levels — the door modal
(L2) hosts the **level-1** Outside-Air control as its first tab, so at Program
L1 the Outside-Air tab is editable while Fresh-Air Door / GDC stay read-only.
Each panel computes `{@const tabCanEdit = programLevel >= (tab.level ??
MODAL_LEVEL[activeModal])}` and passes it as `canEdit`. **This is the standard
way to colocate related-but-different-level pages under one hotspot.**

**One level per tab (rule).** Don't put L1 and L2 controls on the SAME tab —
it reads as a half-locked screen and confused us once (CO₂ purge briefly shared
the L2 Fresh-Air Door tab; pulled into its own L1 tab 2026-06-08). If two pages
are related but different levels, give each its own tab (e.g. door modal = Outside
Air · CO₂ Purge [both L1] · Fresh-Air Door · GDC Stages [both L2]). Group tabs by
level so the lock progression reads left→right.

**Multiple sub-forms per tab** is still supported (each bound to its own
`tabForms[id]` key + `canEdit`; `closeModal` flushes every entry) — but only use
it for **same-level** sub-forms (e.g. `Co2PurgeForm`'s own CO₂ + refrig-purge
SaveButtons are both L1).

## Non-equipment pages — `SETUP_GROUPS` (now top-bar, level-gated)

Equipment pages map to 3D **hotspots**; everything else (system / program /
network / accounts / I-O / alerts) lives in `SETUP_GROUPS` (`plan3d/+page.svelte`).
**2026-06-09:** these moved from a single ⚙ Setup modal to **per-group dropdown
buttons on the top `.hdr` bar**, each gated by login level — `itemsFor(g)` shows
only items with `it.level <= programLevel`, and a group with no accessible items
is hidden (Monitor → no setup, L1 → level-1 items, L2 → all). `openGroup` tracks
the open dropdown. Each `SetupItem` has a `level` (L1/L2) and either:
- `modal` — opens an in-place dashboard modal (once the page is migrated), or
- `route` — navigates to the classic page **transitionally** (shown with a ↗).

**Migration flips `route` → `modal`** as each system page gets a `*Form.svelte`.
The gear is the permanent entry point; the classic route is the fallback until
then. Added 2026-06-08 (scaffold; all items still `route`). Groups: System /
Program / Alerts & Cloud / Network / I/O & Sensors / Accounts & Service.
2026-06-09: Master/Slave moved Program→Network; Run Clock removed (now a
plenum-modal tab).

- **2026-06-09** — `level1/runclock` → `RunClockForm`, added as the 2nd tab of
  the **plenum** modal (right of "Setpoints & Alarms"). Plenum modal is now
  tabbed + joined `MODAL_WIDE` (the run-clock grid is 12 cols). Added the
  run-clock mode colours (purple/yellow/orange) to the `.pform--dark` semantic-
  button override so they don't wash out in the dark modal. Removed Run Clock
  from the Setup gear.
- **2026-06-09** — **Equipment Control modal** (`EquipmentControlForm`,
  extracted from level1/equipment minus GellertPage/ScrollableArea/height).
  First **no-save / control-panel** modal: imperative `/iot/button` actions,
  no edit buffer → `flush()` is a no-op, and a new `MODAL_NOSAVE` set makes the
  footer a single **Close** button (no Cancel / Save & Close). Wide (2-col
  output/status/switch layout). Reached via ⚙ Setup → System (gear item flipped
  `route`→`modal`). Row-build logic keys off `navigationStore.level` (= the
  Program level on the dashboard), so cycling Program rebuilds the L0/L1 view.

## The classic menu is being retired

The `level1Pages`/`level2Pages` list in `+layout.svelte` and the swipe/footer
nav are **going away** — the 3D dashboard (`/dashboard/plan3d`) is the only UI.
So the migration target is purely **"items reachable on the 3D page"** (hotspot
/ modal / tab). Don't maintain the page list, nav order, or menu entries. The
`*Form.svelte` is the real deliverable (the modal renders it); the classic
`/levelN/*` route is just a thin transitional host and can be ignored for nav.

## The migration recipe (what's necessary)

For each migratable page:

1. **Create `src/lib/components/<Name>Form.svelte`** — move the page's
   `<script>` (state + load + `writeProto`/hook + save) and the markup
   **body** (everything inside `<GellertPage>`, minus `<ScrollableArea>`).
   Wrap the root in `<div class="pform pform--{theme}"> … </div>`.
2. **Shared prop contract** (copy from `PlenumSetpointsForm` /
   `HumidifierControlForm`):
   - `export let wait = false; export let ready = false;` — bound out for
     the GellertPage chrome on the classic page.
   - `export let embedded = false;` — when true, **skip**
     `$navigationStore.isDirty = …` registration (swipe-nav is a page
     concern; the modal manages its own dismissal).
     - **⚠ Gotcha (2026-06-09):** if the form hydrates from a store
       `subscribe()` and guards against clobbering in-progress edits, that
       guard MUST use a **local** dirty check (`!isEqual(local, original)`),
       NOT `$navigationStore.isDirty?.()`. In a modal `embedded` is true so
       isDirty was never registered → the guard is always false → every live
       firmware push wipes the user's edits (hit on `AlertSetupForm`). Local
       check works in both page and modal.
   - `export let theme: 'light' | 'dark' = 'light';` — dark = modal skin.
   - `export let canEdit: boolean | null = null;` then
     `$: edit = canEdit ?? ($navigationStore.level > 0);` — lets the modal
     drive edit mode from its own Program toggle, independent of global level.
   - `export async function flush()` — saves the dirty sub-form(s) by
     calling the (now `export`ed) `SaveButton.save()` via `bind:this`.
     This is how the modal persists on close (no route nav fires
     `beforeNavigate`).
3. **Slim the route** to:
   ```svelte
   <GellertPage {wait} {ready} {title} {level} name="…">
     <ScrollableArea>
       <XForm bind:wait bind:ready />
     </ScrollableArea>
   </GellertPage>
   ```
   (`ScrollableArea` is page chrome — it sizes from `heightsStore` and
   mis-sizes in a modal, so it stays in the route, not the form.)
4. **Wire the modal** in `plan3d/+page.svelte`:
   - add a key to `type ModalKey` + `MODAL_TITLES`
   - add an `on:click={() => { if (!justMoved) activeModal = '<key>'; }}`
     to the matching SVG equipment hotspot
   - add an `{:else if activeModal === '<key>'}<XForm bind:this={modalForm}
     embedded theme="dark" canEdit={programUnlocked} />` branch
5. **Theme is shared** — the dark skin lives once in
   [`src/app.postcss`](../constellation-ui/src/app.postcss) under
   `.pform--dark`. New forms inherit it from the `pform pform--{theme}`
   wrapper; no per-form `<style>`.
   - **Gotcha — semantic button colours.** `.pform--dark button.btn` paints
     *every* button sky-blue with `!important`. A form whose buttons carry
     meaning via `!bg-*` utilities (e.g. the climacell run-clock grid:
     off=red/on=green/auto=lime/cooling=blue) will wash out to one blue in
     the modal. Fix once in `app.postcss`: re-assert
     `.pform--dark button.\!bg-<colour> { background:… !important }` (equal
     specificity, later source order wins). Added 2026-06-08 for the four
     run-clock colours; reuse for any future colour-coded buttons.
   - **⚠ Gotcha — `$themeStore` defaults to dark; classic pages never set it.**
     `themeStore` (`store.ts`, persisted key `dashTheme`) defaults to `'dark'`
     and is ONLY ever written by the `/dashboard/plan3d` ☀/🌙 toggle. A classic
     `/levelN` kiosk never writes `dashTheme`, so any **shared component**
     rendered on both the dashboard and classic pages that binds `data-theme`
     (or a skin) straight to `$themeStore` will render **dark on every classic
     page** — including the global on-screen `Keyboard` (it regressed white→dark
     this way; fixed 2026-06-10 by gating on the route:
     `$: kbTheme = $page.url?.pathname?.startsWith('/dashboard') ? $themeStore
     : 'light'`). Rule: shared/global components must derive their theme from
     **route context** (dashboard → follow `$themeStore`; classic → `'light'`)
     or take an explicit `theme` prop — never bind a globally-rendered element
     to `$themeStore` directly. (Embedded `*Form`s are safe: the modal passes
     `theme="dark"` and the classic route passes nothing → `'light'`.)
   - **⚠ Gotcha — `async onMount` silently drops its cleanup return.** A form
     that does `onMount(async () => { … return () => unsub(); })` returns a
     `Promise`, which Svelte ignores — so the cleanup never runs and any
     `store.subscribe()` inside leaks on every mount/unmount (hit on
     `AccountsForm`, fixed 2026-06-10). Keep `onMount` **synchronous**: run the
     async loads fire-and-forget (`loadX().then(…).finally(() => ready = true)`)
     and `return () => unsub()` synchronously.

**Save semantics in the modal:** overlay-click / X / "Save & Close" →
`flush()` (autosave); **Cancel** → close without saving (form destroyed,
edits discarded). To *edit* in the modal the operator needs the **⚙ Program**
toggle on (sets `canEdit`). Verify every migrated save round-trips and
`/health` stays `rxCrcErrors:0`.

---

## Checklist

### A. Equipment settings forms — map to a spatial hotspot (clean fit)

| Page | Form component | Modal key / hotspot | Status |
|---|---|---|---|
| `level1/plentemp` | `PlenumSetpointsForm` | `plenum` — PLENUM card | ✅ done (2026-06-05) |
| `level1/humidifier` | `HumidifierControlForm` | `humidifier` — **one hotspot per assigned head** | ✅ done (2026-06-05) |
| `level1/heat` (NEW) | `HeatForm` | `heat` — HEAT flame tag | ✅ done (2026-06-08) — new page; plenum-heater threshold **+ cavity-heater controls** (target/mode/diff/duty/standby) pulled from `miscellaneous`; L1 |
| `level2/burner` | `BurnerForm` | `heat` modal — "Burner" tab (**AS2 `BurnerAvailable`-gated**) | ✅ done (2026-06-09) — rides the HEAT flame hotspot; Burner tab surfaced by a 1:1 port of AS2 `BurnerAvailable` (onion + in-cure `currentMode∈{16,17}` + burner switch `RemoteOff==Auto` + `Burner.Mode!=0`); else single HeatForm (no tab strip). First conditionally-tabbed modal; live runtime gate. L2. Edits BurnerSettings + ClimacellSettings.altitude |
| `level2/refrigeration` | `RefrigerationForm` | `refrig` — refrig coil | ✅ done (2026-06-06) — settings only (stages/PIDU/purge); TRITON SCADA mimic + P-T chart stay page-side (need route `data.tritons`) |
| `level2/door` | `FreshAirDoorSettingsForm` + `GdcStagesForm` | `door` — DOORS readout tag | ✅ done (2026-06-08) — **first TABBED modal** (Fresh-Air Door / GDC Stages); L2-gated |
| `level1/co2` | `Co2PurgeForm` | `door` modal — its **own "CO₂ Purge" tab** | ✅ done (2026-06-08) — CO₂ purge drives the door open (AS2 `CtrlCo2`), so it rides in the door modal, but as a **separate L1 tab** (a tab stays single-level — don't mix L1/L2 in one tab) |
| `level1/outside` | `OutsideAirForm` | `door` modal — "Outside Air" tab | ✅ done (2026-06-08) — grouped into the door modal (air-side); **L1 tab inside an L2 modal** (per-tab level) |
| `level1/fanspeed` | `FanSpeedForm` | `fan` modal — "Fan Speed" tab | ✅ done (2026-06-08) — fan modal went **tabbed** 2026-06-09 (Fan Speed · Fan Boost) |
| `level1/fanboost` | `FanBoostForm` | `fan` modal — "Fan Boost" tab | ✅ done (2026-06-09) — 2nd tab of the fan modal; L1 |
| `level1/climacell` | `ClimacellRunClockForm` | `climacell` modal — "Run Clock" tab | ✅ done (2026-06-08) — wide modal; 48-slot AM/PM RunTime grid; went **tabbed** 2026-06-09 (Run Clock · Config) |
| `level2/climacell` | `ClimacellConfigForm` | `climacell` modal — "Config" tab | ✅ done (2026-06-09) — 2nd tab (efficiency / altitude / humidifier PIDU); **L2 tab inside an L1 modal** (per-tab level) |
| `level2/pwm` | — | (per-channel; maybe) | ☐ maybe |
| `level2/pid` | — | (equipment tuning; maybe) | ☐ maybe |

### B. Program / system settings forms — migratable, but no equipment anchor

> These are real `SaveButton`/`writeProto` forms that fit the `*Form.svelte`
> extraction, but they belong behind a **setup/menu** affordance on the
> dashboard rather than an equipment hotspot. Extract the form now; decide
> the menu entry point later.

| Page | Notes | Status |
|---|---|---|
| `level1/runclock` | `RunClockForm` | `plenum` modal — "Run Clock" tab | ✅ done (2026-06-09) — 48-slot AM/PM op schedule; 2nd tab of the plenum modal (which went tabbed + wide); L1 |
| `level2/basic` | basic settings — **distributed**, not one modal | ✅ done (2026-06-09) — storage name = click the **titlebar name** (on-screen keyboard); temperature scale = **°F/°C toggle by the weather pill**; crop type = **⚙ Setup → System → Crop Type** modal (Potato/Onion/Bee/Pecan, select-then-Apply, reshapes equipment). **Dropped:** home-page selector (menu retired), equipment-animations toggle, multi-view, **legacy shared password** (remote access = Azure cloud accounts via the Accounts modal). All L2. Classic page left intact transitionally. |
| ~~`level1/co2` *(if not A)*~~ | done as category A (door tab) | ✅ |
| `level2/analog` | `AnalogConfigForm` | ⚙ Setup → **System** → "Analog Boards" (wide modal) | ✅ done (2026-06-09) — full extraction: per-board sensor table (type/label/offset/disabled), board nav (prev/next/find), and the nested custom 4-20 mA modal (renders its own `z-[1000]` overlay above the dashboard modal). `flush()` saves the current board if dirty; the Skeleton dirty-confirm is skipped in `embedded`. Moved out of "I/O & Sensors" into System per request. L2 |
| `level2/auxiliary` | `AuxiliaryForm` | ⚙ Setup → Program → "Auxiliary Outputs" (wide modal) | ✅ done (2026-06-09) — per-output rule list + duty/period, Back/Next nav across CONFIGURED aux outputs. **Menu item only shows when at least one AUX1..AUX8 is mapped in IO Config** (`anyAuxConfigured` via `ioConfig.outputMap`) — "shows up when set in IO config". Page-chrome sizing (heightsStore/inner ScrollableArea) dropped so it flows in the modal; `flush()` saves the current output; Skeleton confirm skipped when embedded. L2 |
| `level2/failures1` / `failures2` | failure-mode tables | ☐ |
| `level2/master` | master/slave sync | ☐ |
| `level1/alerts` | alert thresholds | ☐ |
| `level1/email` | email/display config | ☐ |
| `level1/date` | `DateTimeForm` | ✅ done (2026-06-09) — modal opened by the **top-left titlebar clock** + the ⚙ Setup → System "Date & Time" item; L1 |
| `level1/miscellaneous` | misc settings (heater threshold + cavity-heater controls moved out to `level1/heat` 2026-06-08; now refrig mode / enthalpy / defrost / keypad) | ☐ |
| `history` (menu) | **📜 History & Logs** top-row button → hub modal | ✅ done (2026-06-09) — the `/history` menu page migrated to a hub modal: Alarm History + Active Alarms open in-place; Activity/User logs route (↗, heavy data-viewers pending Pi5 logging) |
| `level2/accounts` | `AccountsForm` | ✅ done (2026-06-09) — no-save wide modal (own Save + Show-Passwords flow); meta fetched client-side; **reached via the L2 titlebar account menu → Account Setup** (2026-06-10; was ⚙ Setup → Accounts, group retired); L2. **Account activity (audit log) split out → `AccountActivityForm` in the History & Logs hub** (2026-06-10) |
| `level2/iotclient` | cloud client config | ☐ |

### C. Won't fit cleanly — note + defer

> Admin/system/view/imperative pages. Not `*Form.svelte` candidates as-is;
> each needs its own treatment (a control panel, a viewer, or stays a page
> reached from a setup menu). Captured so nothing is silently dropped.

| Page | Why it doesn't fit | Likely treatment |
|---|---|---|
| ~~`level1/equipment`~~ | `EquipmentControlForm` | ✅ done (2026-06-09) — **no-save control-panel modal** (`MODAL_NOSAVE`); reached via ⚙ Setup → System. Imperative `/iot/button`, no flush; wide |
| `level2/fans` | drive status + start/stop (imperative) | fans control panel modal |
| `level1/pile` | read-only pile view | dashboard already shows piles |
| `level1/preferences` | locale toggle | small popover / setup menu |
| `level1/service` / `level2/service` | service/diagnostics | setup menu (page) |
| `level1/network` / `level2/tcpip` | network config w/ redirect-on-save | setup menu (page) |
| ~~`level2/ioconfig`~~ | `IoConfigForm` | ✅ done (2026-06-09) — **full-bleed modal** (new `.dlg.full` ≈98vw×94vh) under ⚙ Setup → **System → IO Config**. Earlier "too big for a modal" was overcautious: same extraction pattern as Analog/Aux (orbit-role table + per-board OutputCell/InputCell grid + aggregated validation + Set-To-Default, all verbatim). `ScrollableArea`/footer-slots dropped, Save + Set-To-Default relocated inline; `flush()` saves dirty config (validation-guarded). L2 |
| `level2/orbit-sensors` | orbit sensor mapping | dedicated page |
| `level2/settings` | settings dump/debug | dev page |
| `level2/remote` | remote-systems registry | swipe-between-panels nav (vision) |
| ~~`level1/version`~~ / ~~`level1/fanruntime`~~ | `VersionForm` / `FanRuntimeForm` | ✅ both done (2026-06-09) — **version**: no-save modal (⚙ Setup → System → Software Version): readout view-only at Monitor, `.cfu` firmware updater gated to L1+. **fanruntime**: no-save modal added as a **tile in the 📜 History & Logs hub** (runtime hours = historical data); Daily/Total reset gated to L1+ |
| `level2/graph` / `level2/table` | data views | viewer modal |
| `level2/log` / `level2/pidlog` | log views | viewer modal |
| `level2/download` | export/download | setup menu |
| `history/alarm` | `AlarmHistoryForm` | ✅ done (2026-06-09) — read-only viewer modal (`MODAL_NOSAVE` + wide); reached from the alarm window "View history →" + ⚙ Setup → History |
| `history/*` (activitylog, datainfo, userlog) | history viewers | viewer modals (gear "History" group; alarm done first) |
| `/` (root), `dashboard/*`, `orbit` | not migration targets | — (these ARE the new UI) |

---

## Progress log

- **2026-06-05** — Pattern established on `plentemp` (`PlenumSetpointsForm`).
  Dark theme factored into shared `app.postcss` `.pform--dark`. plan3d modal
  generalised to a `ModalKey` dispatcher (`activeModal` / `closeModal` /
  `MODAL_TITLES`). `SaveButton.save()` exported for `flush()`.
  `vite.config.ts` bridge proxy made env-configurable (`BRIDGE_HOST`).
  `level1/humidifier` migrated (`HumidifierControlForm`). **Done: 2 / category-A.**
- **2026-06-05** — **plan3d promoted to production.** Demo toggles replaced
  by real Nova state: humidifier mist (per-head, gated on the **pump**
  output `panel[17/21/25]` + head proving input — per AS2 `CtrlHumidifier`
  the head latches on and the PUMP is the duty-cycled misting output, so the
  mist pulses with the pump), and the Heat / Cavity-Heat / Refrigeration plenum
  animations now read `EquipmentStatus.items[eqIndex].outputOn` (`eqOut`
  map) — heat=`EQ.HEAT`, cavity=`EQ.CAVITY_HEAT`, refrig defrost/cool from
  `REFRIG_DEFROST*` / `REFRIGERATION`+`REFRIG_STAGE*`. The header demo
  toggle buttons became read-only live-status pills. ~~**Still demo:** the
  Doors slider (`doorsPct`)~~ — **RESOLVED 2026-06-08:** the door animation
  reads the real `SystemStatus.pwm_doors_pct` (field 20), the 0–100% cooling
  PWM output. In AS2 cooling mode `CtrlDoors` drives PWM_DOORS as the PID on
  the cooling error (plenum − setpoint), so the fresh-air door *is* the
  first/free cooling stage and `pwm_doors_pct` is the cooling PWM. Door angle
  is linear in it (`dth = doorsPct/100 × 78°`). No GDC-specific path needed —
  field 20 is always populated by the engine. Auto-deploy to the Pi5 after
  every change is the working cadence (user added the deploy permission rule).
- **2026-06-05** — Humidifier split per-unit: classic page keeps the head
  dropdown (`unit=null`); the dashboard renders **one hotspot per assigned
  head** (gated on `panel[14]/[18]/[22] !== '-1'`, the IO-config port
  assignment) and opens a unit-locked modal (`unit=0/1/2`, dropdown hidden,
  title "Humidifier #N Control"). Modal dispatcher gained `modalUnit` +
  `openModal(key, unit)` + dynamic `modalTitle` — the pattern for any
  multi-instance equipment (doors, fans, future per-zone forms).
- **2026-06-08** — Refrig coil tint now scales with cooling output
  (`coolPct`: `pwm_refrig_pct`, else active-cool-stage fraction) — dark blue
  (`#1e3a8a`) at full → light as it drops, via the existing `brighten()`.
  Live **REFRIGERATION** + **FAN** readout pills added under the coil / fan
  wall (drawn outside the shear matrix so text stays upright; follow drag).
  `level1/fanspeed` migrated (`FanSpeedForm`) — clean `useDraft` extraction,
  keeps the imperative "set new cooling speed" write; wired to the **fan**
  modal key + fan-wall hotspot. **Done: 4 / category-A.**
- **2026-06-08** — Climacell run clock (`level1/climacell`) ~~wired as a link~~
  **migrated to an embedded WIDE modal** (`ClimacellRunClockForm`). The link was
  only ever a default-width limitation: the modal already scrolls and caps at
  `94vw × 88vh`; the 760px default was tuned for narrow setpoint forms. Added a
  **wide-modal mechanism** — `MODAL_WIDE = new Set<ModalKey>([...])` + reactive
  `modalWide` → `class:wide` on `.dlg` → `.dlg.wide { width:1400px }` (still
  `max-width:94vw`). The 12-column AM/PM `RunTime` grid (~960px) fits
  comfortably. **This is the answer to "can every page become part of the 3D
  page?" — yes;** grid/large pages just join `MODAL_WIDE`. The CLIMACELL · RUN
  CLOCK › tag now opens the modal instead of navigating. Footer/swipe page-nav
  in `RunTime` is swallowed when `embedded`.
- **2026-06-08** — **Tabbed modals built; `level2/door` is the first.** Split
  into `FreshAirDoorSettingsForm` (PIDU/actuator/cool-air-cycle, proto
  `DoorSettings`) + `GdcStagesForm` (GDC stage grid, client-side `/iot/gdc`
  fetch since the route loader doesn't run on the dashboard). DOORS readout tag
  opens the door modal; tabs = Fresh-Air Door / GDC Stages; **one Save & Close
  flushes both**. L2-gated (`MODAL_LEVEL.door = 2`). Also landed the **3-state
  Program button** (Monitor → L1 → L2) + level-aware `modalCanEdit` — see the
  Access model section + memory `project-spatial-ui-access-levels`. Door moves
  from *partial* to **done**; the doorsPct animation was already real.
- **2026-06-08** — `level1/outside` → `OutsideAirForm`, added to the door modal
  as the **Outside Air** tab (L1); `level1/co2` → `Co2PurgeForm`, its own
  **CO₂ Purge** tab (L1). Door modal retitled **"Air & Doors"** — tabs grouped
  by level: Outside Air · CO₂ Purge (L1) · Fresh-Air Door · GDC Stages (L2).
  Established the rule **one access level per tab** (CO₂ briefly shared the L2
  door tab → split out).
- **2026-06-08** — **Heat:** flame confirmed gated on `eqOut[EQ.HEAT]`; added
  an always-visible **HEAT** tag/hotspot (ON/OFF, orange when heating) so the
  modal opens even when the heater is off. New `level1/heat` page + `HeatForm`
  (L1) seeded with the heater turn-on threshold moved off `level1/miscellaneous`
  (`heat_temp_thresh`). **Migrated: 9** (plenum, humidifier, fanspeed,
  climacell, refrigeration, door, outside, co2, heat).
- **2026-06-08** — Folded the **cavity-heater** controls (target / mode / diff /
  duty-cycle / pile-sensor / run-in-standby + the cavity-mode-change defaults)
  into `HeatForm` too, so the heat page owns both heating subsystems. Stripped
  them (and the now-unused cavity state / sensor list) from `miscellaneous`,
  which is now just refrig mode / enthalpy / defrost / keypad.
- **2026-06-08** — **Emptied & retired `miscellaneous`.** Refrigeration mode /
  enthalpy-off-pct / defrost interval+duration moved into `RefrigerationForm`
  (a 2nd MiscSettings draft + SaveButton; `flush()` saves both protos) — note
  this raised them **L1 → L2** (refrigeration's level). **Keypad preference
  dropped** (unused; field stays in MiscSettings, just no UI). `miscellaneous`
  is now a hidden stub (`display:false` in `+layout.svelte` page list) showing
  a "moved" note; the new **`heat`** page was added to the L1 page list (it
  was only reachable via the dashboard before). HeatForm + RefrigerationForm
  both full-save MiscSettings — safe because modals/classic pages are
  one-at-a-time (short-lived drafts, user settings don't change underneath).
- **2026-06-09** — Equipment Control no-save modal (⚙ Setup → System); first
  `MODAL_NOSAVE` (Close-only footer). Fixed the switch column white-on-dark
  (raw `<select>` + status text colours in `.pform--dark`) and made the panel
  narrower (dropped from `MODAL_WIDE`, columns 1/3·1/6·1/2). Darkened the
  Fresh-Air Doors Open/Close popup.
- **2026-06-09** — Potato-pile polish (mound bump for volume, dense front-slope
  pass, footprint clamp so none spill). HEAT tag now hidden unless heat output
  is mapped in IoConfig (`$ioConfig.outputMap`).
- **2026-06-09** — `level1/date` → `DateTimeForm`; the **top-left titlebar
  clock** (`tb-clock`) is now a button that opens it as a modal (L1), and the
  ⚙ Setup → System "Date & Time" gear item flipped `route`→`modal` — first gear
  item to become a real in-place modal.
- **2026-06-09** — Date/time UX: dropped seconds from the titlebar clock; time
  field takes bare digits and auto-formats right-to-left (`230→2:30`); the
  DatePicker calendar darkened in-modal + the modal reserves room only while
  it's open (`isOpen` now a bindable DatePicker prop).
- **2026-06-09** — **Light/dark theme toggle.** Persisted `themeStore`
  (`dashTheme`, default dark) → `data-theme` on `.stage`; titlebar ☀/🌙 toggle.
  **Key decision: the `.plan` 3D canvas stays DARK in both themes** (a dark
  viewport in a light app), so the entire SVG scene/text/tag palette is
  untouched — light mode only re-skins the *chrome* (stage, titlebar, pills,
  modals, setup, keyboard) via an additive `:global([data-theme="light"] …)`
  override block in plan3d + a light default in the global `Keyboard` (dark
  gated on `[data-theme="dark"]`). Embedded `*Form`s follow via
  `theme={$themeStore}` (`pform--light` = the classic look). ~25 chrome
  overrides, not 273 colours.
- **2026-06-09** — **Alarms on the dashboard.** Fixed the health pill reading
  the wrong source (raw `$warningReport` → never red); now reads the composed
  `$frontMatterStore.AlarmData` (same as classic `Alarms.svelte`). Removed the
  "All Clear" pill; the **center mode indicator** shows the specific active
  alarm (red+pulsing) and is a **button → alarm window** (lists active alarms +
  Clear Alarms `/iot/button{ClearAlarm}`; "No active alarms" when clear).
  **Alarm history** (`history/alarm` → `AlarmHistoryForm`) is the first history
  viewer: read-only `MODAL_NOSAVE` + wide modal, reached from the alarm window
  "View history →" and a new ⚙ Setup **History** group (Activity Log / User Log
  / Data Info still route-links there, to migrate next).
- **2026-06-09** — **📜 History & Logs** top-row button → hub modal (migrated the
  `/history` menu): Alarm History + Active Alarms open in-place; Activity/User
  logs route (↗). Removed the redundant gear History group.
- **2026-06-09** — **Fan + Climacell modals went tabbed.** `level1/fanboost` →
  `FanBoostForm` (boost mode + temp/runtime trigger), added as the **Fan Boost**
  tab of the **fan** modal (now Fan Speed · Fan Boost, both L1; retitled "Fan
  Control"). `level2/climacell` → `ClimacellConfigForm` (efficiency / altitude /
  humidifier PIDU), added as the **Config** tab of the **climacell** modal (now
  Run Clock [L1] · Config [L2]; retitled "Climacell") — an **L2 tab inside an L1
  modal** via per-tab `level`. Both single-form `activeModal === 'fan'/'climacell'`
  render branches replaced by tab branches; both classic routes slimmed to host
  their form. Remaining quick equipment win: `level2/burner`.
- **2026-06-09** — **Burner on the heat icon, gated by AS2 `BurnerAvailable`.**
  `level2/burner` → `BurnerForm` (mode / manual / ignite+low / PIDU / altitude;
  edits BurnerSettings + ClimacellSettings.altitude). Rides the **HEAT flame
  hotspot** as a **Burner** tab. The tab is surfaced by a **1:1 port of AS2's
  `BurnerAvailable` predicate** (`States.c:1091-1097`, via AS2Archaeologist):
  `SystemMode==ONION && SystemState∈{AIRCURE,BURNERCURE} && CheckInputs(SW_BURNER_AUTO)
  && Burner.Mode!=OFF && RemoteOff[RO_BURNER]!=1`. UI mapping: `BasicSetup.systemMode==1`
  · "in cure" = `SystemStatus.currentMode ∈ {16 air-cure, 17 burner-cure}` (same
  header map) · "burner switch on" = burner `RemoteOff==Auto` (Nova synthesizes
  `SW_BURNER_AUTO` from RemoteOff; Auto excludes Off) **and** `BurnerSettings.mode!=0`.
  When the predicate is false the heat modal is a single HeatForm (no tab strip).
  First **conditionally-tabbed** modal; the gate is **live runtime state** (the tab
  appears/disappears as cure + burner switch change). L2 tab. All category-A
  equipment pages are now migrated except `level2/pwm` / `level2/pid` (maybe).
- **2026-06-09** — **Basic Settings distributed (no single modal).** Its fields
  scatter to where they belong: **storage name** = click the titlebar name →
  on-screen keyboard; **°F/°C** = segmented toggle by the weather pill; **crop
  type** = ⚙ Setup → System → "Crop Type" modal (Potato/Onion/Bee/Pecan,
  select-then-Apply with an equipment-reshape warning). Dropped: home-page selector
  (menu retired), animations toggle, multi-view, and the **legacy shared password**
  (remote access is Azure cloud accounts via the Accounts modal). All L2; classic
  page left intact transitionally. **Save gotcha (in-code):** `resolveForceFields`
  force-emits ALL five registered BasicSetup varints (tempType/systemMode/multiView/
  localLogin/animations) whether present or not — an absent one goes out as 0 and
  clobbers the stored value. A shared `writeBasic(patch)` helper carries all five at
  current values on every partial write; future BasicSetup writers MUST do the same.
- **2026-06-09** — **Analog Boards → wide System modal.** `level2/analog` →
  `AnalogConfigForm` (full extraction of the heaviest settings page so far: board
  nav, per-sensor table, nested custom 4-20 mA modal). Wired as a wide modal under
  ⚙ Setup → **System → "Analog Boards"** (moved out of "I/O & Sensors" per request);
  classic route slimmed to host the form. `flush()` saves the current board if dirty;
  `checkDirty` skips the Skeleton confirm when `embedded` (modalStore confirm isn't
  wired in the dashboard — autoSave + debounce already persisted). The 4-20 mA modal
  renders its own `fixed z-[1000]` overlay so it stacks above the dashboard dialog.
  **Master/Slave** was already in the Network group (no move needed).
- **2026-06-09** — **Auxiliary Outputs → wide Program modal.** `level2/auxiliary` →
  `AuxiliaryForm` (per-output rule list with sensor/equipment conditions + and/or
  chains, duty-cycle/period, Back/Next nav). Dropped the page-chrome sizing
  (heightsStore + inner ScrollableArea) so it flows in the modal. **Conditional
  menu item:** the "Auxiliary Outputs" item (Program group) appears only when at
  least one `AUX1..AUX8` is mapped in IO Config — new `anyAuxConfigured` reactive
  (`ioConfig.outputMap`, reusing the heat-tag `UNASSIGNED_PORTS` set) + a `flag:'aux'`
  on the SetupItem that the dropdown filter honors. This is the first **IO-config-
  gated** setup item — the pattern for any equipment that "shows up when set in IO
  config." `flush()` saves the current output; Skeleton dirty-confirm skipped when
  embedded. L2.
- **2026-06-09** — **IO Config → full-bleed modal (the "too big" page wasn't).**
  `level2/ioconfig` → `IoConfigForm`, the biggest extraction yet (~600 lines: orbit
  role assignment + per-board output/input pin grid via `OutputCell`/`InputCell` +
  aggregated validation + the pending-echo/Set-To-Default machinery, all verbatim).
  Added a new **`.dlg.full`** modal size (`MODAL_FULL` set → `class:full` → ≈98vw×94vh)
  so the densest page **overtakes the 3D scene while open** — but it's still a modal,
  no separate route, still theme-aware + L2-gated. Dropped `ScrollableArea` + its
  `footer-center/right` slots; Save + Set-To-Default moved inline; `flush()` saves a
  dirty config (the SaveButton's aggregated validation still guards it). Placed under
  ⚙ Setup → **System → IO Config** (per request "right under setup"). This retires the
  doc's old "won't fit cleanly / dedicated page" call for ioconfig. `.dlg.full` is now
  reusable for any future page that needs the whole screen.
- **2026-06-09** — **Software Version → System modal; setup menu reorg.**
  `level1/version` → `VersionForm` (no-save modal under ⚙ Setup → System): version
  readout view-only, `.cfu` firmware uploader/installer (+ orbit OTA version polling)
  gated to L1+ via `canEdit`. **Preferences removed from the menu** (kept the route/page
  — languages are handled by the titlebar switcher now, and its image picker doesn't fit
  the spatial design; not deleted, just unlisted). **Service Info moved** Accounts→System
  (still a route ↗; "I'll find a place for it"); the now-single-item group renamed
  **Accounts & Service → Accounts**. Remaining route-links: fanruntime, preferences
  (unlisted), network, tcpip, master, iotclient, service, orbit-sensors, pwm, pid, remote,
  datainfo + the heavy log viewers (blocked on Pi5 logging).

## ⏳ DEFERRED — Pi5 logging backend + Activity/User log migration (another session)

The data-log viewers (`history/activitylog` ~410 lines, `history/userlog` ~370)
are **NOT yet migrated** and currently **have no data**:
- **Blocker:** there is no Pi5-side log store. AS2 logged to a microSD card;
  Constellation is meant to log on the Pi5, but that backend isn't built — so
  `/iot/alarms/:n`, the activity/user log downloads, etc. return nothing
  ("Show" in Alarm History does nothing for the same reason).
- These two pages are heavy data viewers (WebSocket downloads via `WsClient`,
  `VirtualList`, Skeleton drawers, plot/chart stores, `heightsStore` full-page
  sizing, graph/table/backup modes) — a full-page UX, not modal-friendly.
- **Plan for a future session:** (1) stand up Pi5 logging (the `/iot/*` log
  backend) so the viewers have data; (2) then decide whether activity/user logs
  become modals or stay full-page viewers reached from the History hub (likely
  the latter, given charting + virtual scroll + download). Until then they
  remain route-links (↗) in the History hub.

- **2026-06-09** — **Alarms + Alerts top-bar buttons** (tabbed modals):
  **🚨 Alarms** (L2) = `FailuresForm1` + `FailuresForm2` tabs (extracted from
  level2/failures1·2); **🔔 Alerts** (L1) = `AlertSetupForm` + `EmailForm` tabs
  (level1/alerts·email). Buttons are level-gated on the `.hdr` row. Moved
  **IoT Client** from the (now-removed) "Alerts & Cloud" group into **Network**;
  dropped "Failure Modes" from the Program group (now the Alarms modal). All
  four classic routes slimmed to render their forms. Migrated count keeps
  climbing — remaining route-links: basic, preferences, version, auxiliary,
  network, tcpip, master, iotclient, service, analog, ioconfig, orbit-sensors,
  pwm, pid, burner, fanboost, level2/climacell, remote, datainfo + the heavy
  log viewers.
- **2026-06-10** — **Account affordances consolidated.** (1) At Program L2 the
  titlebar account pill (`👤 Level 2 ▾`) became a dropdown — **Account Setup**
  (opens the accounts modal) + **Sign Out** — reusing the shared `openGroup` /
  `.grp-menu` pattern (right-aligned for the right-edge pill). The standalone
  **Accounts** `SETUP_GROUPS` entry was retired (no more top-bar Accounts
  button). At L1 the pill stays a plain Sign Out. (2) The **account activity
  (audit) log** was split out of `AccountsForm` into a new read-only
  `AccountActivityForm` (`/iot/audit` viewer, `MODAL_NOSAVE`, wide) and added as
  an **L2-gated tile in the 📜 History & Logs hub** — it's account *activity*,
  i.e. a log, and it's the one working entry in the hub's logs column (the
  Activity/User log route-links are still deferred on Pi5 logging). `AccountsForm`
  is now accounts/roles/cloud-links only.
