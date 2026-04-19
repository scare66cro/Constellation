the purpose of this emulator is to mostly keep the svelte UI intact and create a new control panel (constellation) with a completeley different architecture than the existing as2 architecture. the as2 did run equipment well so it is good to look up how it handles things in the event that equipment doesnt run the same on this new architecture. I always want to keep this system ready for production deployment, so I want to avoid adding any code that would be difficult to remove later. I also want to make sure that the code is well organized and easy to understand, so that it can be easily maintained and updated in the future. log anything we do different for the simulation than we would have in the production environment. The main goal is to produce firmware for the nova am2434 that can run the same UI code as the simulator, with minimal changes needed to switch between the two. The simulator should be a close approximation of the production environment, so that we can test and debug the UI code effectively before deploying to real hardware.
it is ok to modify any part of this new system to make this new control system a modern system. i want the equipment functionality to perform the same but i dont care if we have to change any part of it to keep it modern and efficient. I want to make sure that we are using modern programming practices and tools, and that we are following best practices for software development. I also want to make sure that we are using the latest technologies and frameworks, so that we can take advantage of new features and improvements. Overall, the goal is to create a modern, efficient, and maintainable control system that can run on both the simulator and real hardware with minimal changes needed to be ready for production deployment.

update this file as you see necessary to remind you of how to interface with things that dont seem intuitive at first, and to log any differences between the simulator and production environments. This will help ensure that we can maintain a clear understanding of how the system works and make it easier to transition from simulation to production whedn the time comes.

if something is tough to figure out, do not shim stub are make an api workaround. continue troubleshooting until you figure it out and then document if necessary to remember.

# How to use & maintain this file (read first)

This file is auto-loaded into every Copilot session — keep it lean and
treat it as a **router**, not an encyclopedia. Detailed knowledge lives
in `docs/` and in source-file header comments; this file just points
there.

**When starting a task:**
1. Skim the bullet sections below for top-of-mind invariants.
2. Follow the doc links in the relevant section before editing — the
   linked docs hold the actual rules and gotchas.
3. Check `/memories/repo/` and `/memories/session/` for any active
   notes before re-discovering something.

**When you learn something new (a bug fix, a non-obvious invariant, a
sim-vs-prod difference, an undocumented API quirk):**
- **Protocol / save-path / firmware-bridge lessons** → append to
  [`docs/firmware-bridge-protocol.md`](../../docs/firmware-bridge-protocol.md).
- **Sim-only behaviour or transition gotcha** → append to
  [`docs/Simulator-to-Production-Transition.md`](../../docs/Simulator-to-Production-Transition.md).
- **Equipment state-machine behaviour** → append to
  [`docs/firmware-equipment-control.md`](../../docs/firmware-equipment-control.md).
- **Invariant that should be enforced at the point of use** → add a
  doc-comment in the source file (e.g. above the function or at the top
  of the module) and link back to the canonical doc.
- **Repo-scoped fact that survives sessions but doesn't belong in user
  docs** (build commands, file layout quirks) → `/memories/repo/`.
- **Only edit this file** when adding a *new section/router entry* for a
  subsystem that doesn't have one yet, or when the top-of-mind
  invariants list genuinely changes. Resist the urge to inline details
  — link out instead.

**Rule of thumb:** if your addition is more than ~5 lines, it belongs
in a `docs/` file with a one-line link from here.

**Verification before committing a doc change:** make sure every link
in this file resolves and that the linked doc actually contains the
detail the bullet promises.

# ui-svelte Copilot Instructions
- Purpose: SvelteKit industrial control panel for agricultural storage; three-tier access (Level 0/1/2), real-time WebSocket updates, persistent stores, DH+AES auth.
- Time discipline: use controller time helpers from [src/lib/business/timeUtils.ts](src/lib/business/timeUtils.ts) (mirrors IoTClient pattern); avoid raw `Date.now()`/`new Date()`.
- Store architecture: global stores in [src/lib/store.ts](src/lib/store.ts) (`keysStore`, `navigationStore`, `frontMatterStore`, `backgroundStore`); use `createPersistedStore()` for localStorage-backed state.
- Page guard: `GellertPage.svelte` wraps all pages with auth guards, swipe navigation, retry logic. Route hierarchy: `/` (level 0) → `/level1/[page]` → `/level2/[page]`.
- WebSocket: `WebSocketClient` in [src/lib/business/webSocketClient.ts](src/lib/business/webSocketClient.ts) with auto-reconnect/exponential backoff. Protocols: `frontmatter-data`, `upgrade-data`, `download-data`, `tcpip-data`.
- Auth flow: `checkPassword()` in [src/lib/business/util.ts](src/lib/business/util.ts) handles DH key exchange via `/iot/dlr`. `isLocalView` controls screen blanking/auto-access.
- API endpoints: `/iot/*` for device comms, `/api/*` for app logic. Always use `safeJsonParse()` for responses; industrial data often as `{ array: string[] }`.
- UI conventions: Skeleton UI + Tailwind; i18n via svelte-i18n (`$t('key')`), keys in [src/lib/locales](src/lib/locales). Props: `level`, `name`, `ready`, `wait` for GellertPage.
- Build/test: `npm run dev` (dev server), `npm run build` (production), `npm run test` (Playwright e2e), `npm run test:swipe` (swipe navigation).
- Deployment: `@sveltejs/adapter-node`; static assets in `/static/background/`. Target: industrial touch interface (cursor hiding, screen blanking).
- Troubleshooting: WebSocket—check connection state/logs; auth—verify DH exchange + `keysStore.secret`; navigation—ensure `$navigationStore.level` matches route.

# Nova firmware ↔ bridge protocol — quick reference

The bridge (`constellation-ui/server/src/`) and Nova AM2434 firmware
(`Nova_Firmware/Platform/`) are joined by a COBS+CRC envelope carrying
protobuf messages. The wire layer is identical in simulator and production;
bugs in either environment will reproduce in the other.

> **Nova-only invariants (post-migration, Apr 2026):** the legacy ASCII
> RTS/ACK bridge and the TS ARM simulator have been deleted. There is no
> `USE_NOVA` switch, no `serialBridge`, no `.arm-settings-bank-*.json`.
> Settings persist exclusively in OSPI ping-pong banks. See
> [`/memories/repo/nova-migration.md`](/memories/repo/nova-migration.md)
> for the full list of removed files, the SR_CLEAR vs SR_REQUEST firmware
> trap, and the OSPI cold-boot success log signature.

**Top-of-mind invariants:**

1. **proto3 zero-suppression** — `pb_uint32` / `pbVarint` skip 0; use
   `pb_uint32_force` / `pbVarintForce` whenever 0 is a meaningful value
   (mode=OFF, index=0, threshold=0, fixed-length array slots, cross-coupled
   settings).
2. **Mode-dependent encoders** mirror legacy `StoreXxx()` positional layout
   per mode — never send a fixed-position superset.
3. **Repeated-submsg decoders** clear destination arrays first; their
   counters MUST be function-local, never `static`.
4. **TX path goes through `NovaProto_SendRaw`** mutex; bypassing it
   corrupts COBS framing and produces a CRC-error storm.
5. **Verification:** `/health` must report `rxCrcErrors:0` /
   `rxCobsErrors:0` after any save campaign.
6. **No hardcoded sensor / setpoint / unit values, ever.** Firmware must
   not seed `Sensor.Value`, `Plenum.TempSet`, `Settings.TempType`, etc.
   with literals "for testing". The orbit sim sliders + Level 2 Basic
   Settings page are the only inputs. The bridge
   (`dataCache.ts`, `djangoSync.ts`) is a **transparent passthrough** —
   no temperature unit conversion, no scaling, no fix-ups. Conversion to
   user units happens exactly once, in
   `Nova_Firmware/Platform/nova_thread_overrides.c::ReadAnalogBoards()`.
   Full rules: [`/memories/repo/data-path-rules.md`](/memories/repo/data-path-rules.md).

**Full reference:** [`docs/firmware-bridge-protocol.md`](../../docs/firmware-bridge-protocol.md)
covers all invariants in detail, the build/restart loop, the smoke-test
pattern, and the SystemStatus extension fields.

**Sim vs. production deltas:** [`docs/Simulator-to-Production-Transition.md`](../../docs/Simulator-to-Production-Transition.md)
covers the architecture differences (transport, flash, build flags).
Settings persistence is now identical in sim and production: OSPI
ping-pong banks, file-backed in WSL ext4 at `~/.constellation/nova_ospi.bin`
under QEMU.

**Equipment state machine reference:** [`docs/firmware-equipment-control.md`](../../docs/firmware-equipment-control.md).

When fixing a save-path bug, add the lesson to
`docs/firmware-bridge-protocol.md` rather than this file — keep this
router lean.

