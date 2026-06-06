> **tl;dr** — Auto-loaded every Claude session. This is the **canonical
> doc map** and the **invariants you must not violate**. For "how the
> system actually runs today" go to [`docs/System-State.md`](docs/System-State.md).
> For "I learned something new, where does it go?" see the bottom of this file.

# Project mission

The purpose of this codebase is to keep the SvelteKit UI intact and replace
the legacy AS2 control panel with a modern architecture (the Constellation
control panel) running on Nova AM2434 hardware. The AS2 system ran equipment
well — when something doesn't behave the same on the new architecture, look
up how AS2 handled it (`docs/legacy_AS2_reference/` is **read-only**
reference) and port the behaviour into the modern stack.

Production-ready always. Don't add code that's hard to remove later. No
shims, stubs, or "TODO: real fix later" workarounds — keep troubleshooting
until the actual problem is solved, then document it. Equipment behaviour
must match AS2; everything around it (architecture, tools, dependencies)
can be modernised freely.

When something is non-obvious, log it. When two simulators or environments
differ from production, log the difference. The eventual cutover should be
mechanical, not a research project.

# Hard invariants (violating any of these silently breaks the system)

0. **`docs/legacy_AS2_reference/` is read-only.** Nova firmware
   transitionally compiles ~24 `Application/*.c` files from this tree, but
   new equipment behaviour goes Nova-side under `Nova_Firmware/Platform/`.
   Never add `#ifdef CONSTELLATION_NOVA` blocks or weak hooks inside legacy.

1. **proto3 zero-suppression.** `pb_uint32` / `pbVarint` skip 0; use
   `pb_uint32_force` / `pbVarintForce` whenever 0 is a meaningful value
   (mode=OFF, index=0, threshold=0, fixed-length array slots, cross-coupled
   settings). Full rules: [`docs/firmware-bridge-protocol.md`](docs/firmware-bridge-protocol.md) §1.

2. **Mode-dependent encoders mirror legacy `StoreXxx()` positional layout
   per mode** — never send a fixed-position superset.
   See [`docs/firmware-bridge-protocol.md`](docs/firmware-bridge-protocol.md) §2.

3. **Repeated-submsg decoders clear destination arrays first;** their
   counters MUST be function-local, never `static`.
   See [`docs/firmware-bridge-protocol.md`](docs/firmware-bridge-protocol.md) §3.

4. **TX path goes through `NovaProto_SendRaw` mutex.** Bypassing it
   corrupts COBS framing and produces a CRC-error storm.
   See [`docs/firmware-bridge-protocol.md`](docs/firmware-bridge-protocol.md) §6.

5. **Verification:** `/health` must report `rxCrcErrors:0` /
   `rxCobsErrors:0` after any save campaign.

6. **No hardcoded sensor / setpoint / unit values, ever.** Firmware must
   not seed `Sensor.Value`, `Plenum.TempSet`, `Settings.TempType`, etc.
   with literals "for testing". The orbit sim sliders + Level 2 Basic
   Settings page are the only inputs. The bridge (`dataCache.ts`,
   `djangoSync.ts`) is a **transparent passthrough** — no temperature unit
   conversion, no scaling, no fix-ups. ADC → engineering conversion happens
   exactly once, on the STORAGE orbit board in
   [`Nova_Firmware/lp_am2434/orbit_server/adc_convert.c`](Nova_Firmware/lp_am2434/orbit_server/adc_convert.c)
   (1:1 port of legacy `Analog_Input.c::ConvertToTemp/Humid/CO2`).

7. **Never flash with multiple XDS110 probes plugged in.** DSS silently
   routes to whichever probe enumerated first regardless of `.ccxml`
   serial filter. `Flash-LP.ps1` enforces this via `xdsdfu -e` precheck;
   do not pass `-Force`. Recovery requires `-WipeDevCfg` to clear the
   OSPI device-config bank. See [`docs/LP-Flash-Probe-Discipline.md`](docs/LP-Flash-Probe-Discipline.md).

8. **Both LP boards ship with the same default MAC.** IP (written to OSPI
   device-config at flash time) is the only safe board identifier.

9. **Firmware identity** — every LP-AM2434 image carries a version
   `0.A.<n>+<git-sha>[-dirty]`. Bump
   [`docs/firmware-version-current.md`](docs/firmware-version-current.md)
   before flashing a meaningful change. `Flash-LP.ps1` injects the string
   at build time and refuses to flash a dirty tree without `-Force`. Every
   successful flash auto-appends a row to [`docs/firmware-deployed.md`](docs/firmware-deployed.md).

10. **Register / UI map is canonical.** Before adding a new value to the
    UI, check [`docs/register-and-ui-map.md`](docs/register-and-ui-map.md)
    — if the field is already plumbed, extend the existing path; do not
    parallel-implement. Regenerate after touching `orbit_*.h` or
    `proto/agristar/envelope.proto` with
    `powershell -File scripts/Generate-RegisterMap.ps1`.

11. **OSPI DAC-mode writes — never toggle the DAC bit between page
    programs.** On AM2434+Cypress S25HL512T, disabling
    `CONFIG_REG[ENB_DIR_ACC_CTLR]` or clearing `IND_AHB_ADDR_TRIGGER_REG`
    between sub-PPs causes the controller to silently drop all AHB
    writes past the first 5 32-bit words of every sub-PP from the
    second onward. The chip cycles WIP correctly so the failure is
    invisible without a per-write byte-level readback. SDK's
    `OSPI_lld_writeDirect` leaves both registers enabled across calls
    — match that pattern exactly. Full rule + 3-day debug trail:
    [`docs/lp-am2434-ospi-dac-writes.md`](docs/lp-am2434-ospi-dac-writes.md).

12. **🚨 UART airgap is the production data path between Pi5 and the
    equipment LAN — not a transport choice.** In production the Pi5
    lives on the customer/internet side (e.g. `192.168.10.108`) and
    has NO IP route to `10.47.27.0/24`. The Nova-controller LP
    bridges via UART (`/dev/ttyAMA0` @ 921600, COBS+protobuf
    envelopes) on the Pi5 side and Ethernet on the LP side. Every
    bit of bridge↔LP communication must traverse Nova.
    **TODAY'S BENCH HAS THE PI5 DUAL-HOMED ON `10.47.27.108` FOR
    DEV CONVENIENCE.** That breaks the airgap, but the legacy
    direct-TCP-to-LP modules that USED to depend on it are gone now
    on the OTA side. **OTA migration (Phase 4) FULLY LANDED 2026-05-29:**
    `firmwareInstaller.ts` drives the Nova-side broker exclusively over
    UART envelopes (`FwInstallBegin/ComponentBegin/Chunk/ComponentFinalize/
    Complete`); the broker forwards to orbits via Nova-side `nova_ota_push.c`.
    End-to-end validated 2026-05-29 across STORAGE / GDC / TRITON /
    CONTROLLER on the 4-board fleet (`overallState: "done"`, `.1
    active=bankB banks=AB`). The 7th-layer SBL-wipe bug that hid
    behind the 6-layer fix from 2026-05-26 was the last gating issue;
    fixed in 0.A.208 by relocating `FW_HEADER_OFFSET` from `0x60000`
    to `0x300000`. Memory: [`memories/repo/sbl-wipe-controller-self-update-7th-layer-2026-05-29.md`](memories/repo/sbl-wipe-controller-self-update-7th-layer-2026-05-29.md).
    Phase 4b deletion of `orbitOtaPush.ts` / `orbitFleetResolver.ts`
    / `probe_fleet.ts` LANDED 2026-05-29 (commit `5465ab5`, 816 lines).
    `vfdClient.ts` and `orbitMbtcp.ts` (Modbus paths) HAVE NOT started
    migration and remain on borrowed time. If you are writing new
    bridge↔LP code and you see TCP-from-Pi5-to-LP in your design,
    that is the migration target, not the production pattern — design
    the envelope, not the socket.

# Doc map (single source of truth for "where is X?")

## Architecture & state
- [`docs/System-State.md`](docs/System-State.md) — **read first.** Live architecture (fleet on 0.A.208, Phase 5 proto-direct landed), bench rig, workflow.
- [`docs/firmware-bridge-protocol.md`](docs/firmware-bridge-protocol.md) — COBS+CRC + save-path invariants + `/health`.
- [`docs/firmware-equipment-control.md`](docs/firmware-equipment-control.md) — equipment state machines, PID cadence, AO fan-out.
- [`docs/Legacy-vs-Nova-Equipment-Control-Review.md`](docs/Legacy-vs-Nova-Equipment-Control-Review.md) — AS2 vs Nova per-equipment (Phase 17 FINAL — all equipment ported Nova-native).
- [`docs/proto-direct-redesign-plan.md`](docs/proto-direct-redesign-plan.md) — transport architecture, `useDraft` / `writeProto` (Phase 5 LANDED 2026-05-30).
- [`docs/proto-migration-pattern.md`](docs/proto-migration-pattern.md) — per-page CSV→proto migration recipe.
- [`docs/proto-force-zero-design.md`](docs/proto-force-zero-design.md) — design rationale for force-encoders.
- [`docs/Simulator-to-Production-Transition.md`](docs/Simulator-to-Production-Transition.md) — sim→prod transition COMPLETE; bench == prod binaries.
- [`docs/ioconfig-architecture.md`](docs/ioconfig-architecture.md) — IO Config wire layout, slot invariants.
- [`docs/triton-grc-port-plan.md`](docs/triton-grc-port-plan.md) — TRITON refrigeration GPIO/EPWM (Phase 17 LANDED).
- [`docs/ui-changes.md`](docs/ui-changes.md) — UI page moves, swipe-nav, menu changes.
- [`docs/swipe-navigation.md`](docs/swipe-navigation.md), [`docs/touch-slider-fix.md`](docs/touch-slider-fix.md), [`docs/version-page-refactor.md`](docs/version-page-refactor.md).

## Hardware / bringup
- [`docs/LP-AM2434-Hardware-Bringup-Plan.md`](docs/LP-AM2434-Hardware-Bringup-Plan.md) — bringup reference, bench rig (4-board fleet live).
- [`docs/LP-AM2434-Ethernet-Bringup.md`](docs/LP-AM2434-Ethernet-Bringup.md) — CPSW/ICSSG init, PHY (historical bringup; LP runs 100M-FD in production).
- [`docs/LP-AM2434-OTA-Update-Plan.md`](docs/LP-AM2434-OTA-Update-Plan.md) — OTA design (Phase 4 end-to-end VALIDATED 2026-05-29).
- [`docs/lp-am2434-ota-hardening-plan.md`](docs/lp-am2434-ota-hardening-plan.md) — OTA defensive checks (G1-G4 DONE; Gap 5/6 DEFERRED).
- [`docs/lp-am2434-ospi-dac-writes.md`](docs/lp-am2434-ospi-dac-writes.md) — **DAC-mode write invariant** (the rule that took 3 days to find; never toggle DAC bit between sub-PPs).
- [`docs/LP-Flash-Probe-Discipline.md`](docs/LP-Flash-Probe-Discipline.md) — flash/probe/reset, wrong-probe recovery.
- [`docs/Constellation-Board-Hardware-Spec.md`](docs/Constellation-Board-Hardware-Spec.md) — custom production PCB v0 spec.
- [`docs/lp-am2434-network-production-requirements.md`](docs/lp-am2434-network-production-requirements.md) — production-PCB network requirements (drop the 100M-FD downshift).
- [`docs/Network-Migration-10.47.27.x.md`](docs/Network-Migration-10.47.27.x.md) — lab network migration (COMPLETE 2026-05-03; rpi5 at `.108`).
- [`docs/uart-airgap-architecture.md`](docs/uart-airgap-architecture.md) — **production UART airgap + Nova-as-OTA-broker migration.** Phase 4 (OTA) LANDED. Phase 4b vfdClient/orbitMbtcp Modbus migration is the next significant chunk. Bench Pi5 is still dual-homed for dev.
- [`docs/lp-am2434-watchdog-design.md`](docs/lp-am2434-watchdog-design.md), [`docs/lp-am2434-f2c-sbl-chooser-design.md`](docs/lp-am2434-f2c-sbl-chooser-design.md).
- [`docs/Bench-Checklist-2026-05-02.md`](docs/Bench-Checklist-2026-05-02.md) — archived 2026-05-02 runbook (historical).

## Firmware versioning
- [`docs/firmware-version-current.md`](docs/firmware-version-current.md) — bump before meaningful flash.
- [`docs/firmware-deployed.md`](docs/firmware-deployed.md) — auto-appended chain of custody.
- [`docs/register-and-ui-map.md`](docs/register-and-ui-map.md) — `HR / proto field ↔ UI page + label`.

## Operational
- [`docs/factory-defaults-review-2026-05.md`](docs/factory-defaults-review-2026-05.md) — defaults audit.
- [`docs/session-resume-ota-flash-write.md`](docs/session-resume-ota-flash-write.md) — historical session resume (OTA Flash_write resolved 0.A.112; full root cause in `memories/repo/ota-flash-write-preemption-root-cause.md`).
- [`docs/Azure-Cloud-Integration-Plan.md`](docs/Azure-Cloud-Integration-Plan.md) — cloud sync plan.
- [`docs/remote-claude-code-from-phone.md`](docs/remote-claude-code-from-phone.md) — reach the office (native-Windows) Claude Code CLI from a phone over Tailscale: RDP, or OpenSSH + MSYS2 tmux.
- [`docs/history/save-path-postmortems.md`](docs/history/save-path-postmortems.md) — resolved save-path bugs.

## Repo-local memory notes (gitignored, ~100 files)

**Always start bug-search at [`memories/repo/INDEX.md`](memories/repo/INDEX.md)** — keyword-indexed one-line summary of every note. Match symptom → read that one note. New notes use [`memories/repo/_TEMPLATE.md`](memories/repo/_TEMPLATE.md) and **must add a row to INDEX in the same commit** or they're invisible to future-Claude.

# UI conventions (constellation-ui)

- SvelteKit industrial control panel; three-tier access (Level 0 / 1 / 2);
  real-time WebSocket updates; persistent stores; DH+AES auth.
- Time discipline: use controller-time helpers from
  [`constellation-ui/src/lib/business/timeUtils.ts`](constellation-ui/src/lib/business/timeUtils.ts);
  avoid raw `Date.now()` / `new Date()`.
- Stores: [`constellation-ui/src/lib/store.ts`](constellation-ui/src/lib/store.ts)
  (`keysStore`, `navigationStore`, `frontMatterStore`, `backgroundStore`).
  Use `createPersistedStore()` for localStorage-backed state.
- Page guard: `GellertPage.svelte` wraps all pages with auth, swipe nav,
  retry logic. Routes: `/` (level 0) → `/level1/[page]` → `/level2/[page]`.
- WebSocket: [`constellation-ui/src/lib/business/wsClient.ts`](constellation-ui/src/lib/business/wsClient.ts)
  (auto-reconnect / exponential backoff). Protocols: `frontmatter-data`,
  `upgrade-data`, `download-data`, `tcpip-data`.
- Auth: `checkPassword()` in
  [`constellation-ui/src/lib/business/util.ts`](constellation-ui/src/lib/business/util.ts)
  handles DH key exchange via `/iot/dlr`.
- API: `/iot/*` for device comms (legacy CSV — shrinking), `/api/*` for app
  logic, `/proto/stream` (WS) + `/proto/write/:field` (POST) for proto-direct.
  Use `safeJsonParse()` for responses.
- UI: Skeleton UI + Tailwind; i18n via svelte-i18n (`$t('key')`), keys in
  [`constellation-ui/src/lib/locales`](constellation-ui/src/lib/locales).
- Build / test: `npm run dev` (dev), `npm run build` (prod),
  `npm run test` (Playwright e2e), `npm run test:swipe`.
- Deployment: `@sveltejs/adapter-node`. Static assets in `/static/background/`.

# When you learn something new

| What you learned | Where it goes |
|---|---|
| Protocol / save-path / firmware-bridge invariant | Append to [`docs/firmware-bridge-protocol.md`](docs/firmware-bridge-protocol.md) |
| Resolved bug / post-mortem with lessons | Append to [`docs/history/save-path-postmortems.md`](docs/history/save-path-postmortems.md) |
| Sim-only quirk / transition gotcha | Append to [`docs/Simulator-to-Production-Transition.md`](docs/Simulator-to-Production-Transition.md) |
| Equipment state-machine behaviour | Append to [`docs/firmware-equipment-control.md`](docs/firmware-equipment-control.md) |
| UI page move / menu change / route alias | Append to [`docs/ui-changes.md`](docs/ui-changes.md) |
| Hardware / flashing / boot lesson | Append to relevant `memories/repo/lp-am2434-*.md`, summarize to `docs/System-State.md` §3 if it changes the orientation map |
| Repo-scoped fact (build commands, file layout quirks) | New file in `memories/repo/` |
| Invariant that should be enforced at the call site | Doc-comment in the source file linking back to the canonical doc |
| New subsystem with no router entry yet | Add a row to the doc map above |

**Rule of thumb:** if your addition is more than ~5 lines, it belongs in a
`docs/` file (or a memory note) with a one-line link added here. Resist
inlining detail — link out instead.

**Verify before committing a doc change:** every link in this file must
resolve. `git status memories/` should be empty (gitignored).

# Bug workflow (do not skip — this is how knowledge compounds)

**Before debugging:**
1. **Read [`memories/repo/INDEX.md`](memories/repo/INDEX.md) first.**
   It's a curated keyword index of every memory note (~100 of them).
   Match symptom keywords (error string, field name, register, function)
   → read that one note. **One read instead of fan-out grep.** Only fall
   back to `Grep memories/repo/` and `Grep docs/history/` if the index
   keywords don't catch the symptom. Re-fixing a bug we already solved
   is the most expensive failure mode here.

**When the bug is fixed:**
2. Capture **symptom + root cause + fix** in a memory note at
   `memories/repo/<short-kebab-name>.md`. Use [`memories/repo/_TEMPLATE.md`](memories/repo/_TEMPLATE.md)
   as the skeleton (Symptom / Root cause / Fix / Verification / Keywords).
   The **Keywords** line is what makes the note grep-findable later;
   include verbatim error strings, function names, file paths.
   Add the new note as a row in `memories/repo/INDEX.md` in the same
   commit — an unindexed note is invisible to future-Claude.
3. If the lesson generalises, add a numbered rule to the relevant
   invariants doc (firmware-bridge-protocol / firmware-equipment-control
   / System-State §3). Cross-link the memory note and the rule.
4. If the fix makes an existing **workaround obsolete**, find the
   workaround in the docs and either delete it or mark it with the
   obsolescence block below. Do not silently leave stale advice.
5. If the fix invalidates an **invariant in CLAUDE.md** (rare — these
   are perennial), update or renumber the invariant. Tell the user
   what changed.
6. Run the verification recipe (`/health` → `rxCrcErrors:0`,
   round-trip save survives `system_reset.js`, etc.) and paste the
   result into the memory note. "Believed fixed" is not fixed.

# Obsolescence convention

When a fix makes earlier guidance no longer apply, **mark it, don't
delete it silently** — future-Claude needs to know "this used to bite
us" vs "this still bites us":

```markdown
> **RESOLVED 2026-MM-DD** — root cause was X, fixed in
> `<file>:<func>`. Memory: [`memories/repo/<note>.md`].
> _Original advice retained below for context._
```

Strike-through is the lighter-weight alternative for one-liners:

```markdown
- ~~Always force-encode `Settings.X.Mode`~~ — no longer needed since
  default-clear was added to `apply_X` (2026-MM-DD,
  [`memories/repo/<note>.md`]).
```

When the original advice no longer adds historical value (a year+ old,
nobody references it, the fix is in the codebase), then delete it
outright in a separate doc-cleanup pass.
