# Phase 4b — Modbus-into-Nova migration plan

> **Audience:** the engineer migrating `vfdClient.ts` + `orbitMbtcp.ts`
> off the Pi5 and into Nova firmware so the production UART airgap
> works. Drafted 2026-06-01, multi-session.
>
> **Source of truth for the airgap principle:**
> [`docs/uart-airgap-architecture.md`](uart-airgap-architecture.md)
> §"Phase 4b — delete the dead direct-TCP modules" (this doc fills out
> the last unchecked items on that list).
>
> **Source of truth for the bridge today:**
> [`constellation-ui/server/src/orbitMbtcp.ts`](../constellation-ui/server/src/orbitMbtcp.ts),
> [`constellation-ui/server/src/vfdClient.ts`](../constellation-ui/server/src/vfdClient.ts),
> [`constellation-ui/server/src/apiRoutes.ts`](../constellation-ui/server/src/apiRoutes.ts).

## TL;DR

1. **Today's bench works because Pi5 is dual-homed on `10.47.27.0/24`.**
   The bridge's `orbitMbtcp.ts` opens Modbus TCP sockets directly to
   each orbit LP `:5502`; `vfdClient.ts` opens Modbus TCP directly to
   the VFD (`FENA-01` adapter or `rs485Responder` sim). Per the
   airgap doc both will be unreachable in production.
2. **Migration target:** Nova becomes the only Modbus master on the
   equipment LAN. The bridge consumes the data via envelopes that Nova
   emits over the existing UART. The `/iot/*` HTTP API on the bridge
   stays unchanged — handlers just read from `dataCache`/`novaStore`
   instead of opening fresh Modbus sockets.
3. **Three sub-phases:** (1) orbit reads (largest chunk by callsite
   count), (2) orbit writes (small, mostly mechanical), (3) VFD
   (new Nova-side task + envelope routing). Each sub-phase ships
   incrementally; bench continues to work via the bridge fallback
   until the sub-phase is fully validated.
4. **No new transport contract** for orbit reads — extend the existing
   `OrbitStatus` / `SystemStatus` / `SensorData` envelopes with the
   fields the bridge today re-fetches via direct Modbus. **One new
   envelope** (`OrbitRegWrite`, generalising `TritonRegWrite`) for
   GDC writes. **One new Nova-side task** (`nova_vfd_client_task`)
   for VFD — the existing `VfdStatus` envelope (tag 200) becomes
   Nova-emitted instead of bridge-emitted.

## Current state — audit

### `orbitMbtcp` callsites in `apiRoutes.ts`

| Line | Operation | Purpose | Data direction | Already in `dataCache` / envelopes? |
|---|---|---|---|---|
| 492 | `resolveOrbitHost(slot)` | OTA version probe target | Pi5 → LP `:5503` | n/a (OTA path, already Nova-side per Phase 4) |
| 2564 | `resolveOrbitHost(gdc.slot)` | GDC host lookup (for the reads below) | host string only | — |
| 2573 | `readHoldingRegs(host, 300, 40)` | GDC `/iot/gdc` — door state, actuator pos, stage assignment, status bits | LP→bridge (40 regs) | **NO** — needs new envelope (or extend OrbitStatus) |
| 2689 | `writeHoldingRegs(host, 325, stageOf)` | GDC `/iot/gdc/stages` — actuator-to-stage assignment | bridge→LP (5 regs) | **NO** — needs OrbitRegWrite envelope |
| 2704 | `writeHoldingReg(host, 305, 1)` | GDC `/iot/gdc/calibrate` — kick HR_GDC_CAL_REQUEST | bridge→LP (1 reg) | **NO** — same OrbitRegWrite envelope |
| 2775 | `resolveOrbitHost(board.slot)` | Triton host lookup (for the reads below) | host string only | — |
| 2778-2782 | `readHoldingRegs(host, 400/440/460/480/530, ...)` | Triton `/iot/triton/:slot` — setpoints, failure modes, live sensors, live equipment, alarms (5 blocks, ~95 regs total) | LP→bridge | **PARTIAL** — `SystemStatus`/`SensorData` carry the live sensor + alarm subset; setpoints + failure-mode tables are NOT carried today |
| 3157 | `writeHoldingReg(resolveOrbitHost(slot), 542, 1)` | Triton `/iot/triton/:slot/safety/reset` | bridge→LP | ✅ **already exists** as `serialBridge.tritonRegWrite(slot, 542, 1)` — just swap |
| 3219-3221 | `readDiscreteInputs/readCoils/readHoldingRegs(host, 0..)` | `/iot/orbit/status` — live DI/DO/AO state on every connected board | LP→bridge | **NO** — needs new envelope or DI/DO/AO fields added to OrbitStatus |

### `vfdClient` callsites in `apiRoutes.ts`

| Line | Operation | Purpose | Migration verdict |
|---|---|---|---|
| 1349-1354 | `vfdClient.getDrives()` + `sendAction(unit, 'reset')` | Bulk fault-reset all faulted drives | Bridge reads `vfdStateStore` (envelope-fed), sends `VfdAction` envelope to Nova |
| 2233 | `vfdClient.getDrives()` | `/iot/fans` — list drives | Bridge reads `vfdStateStore` (envelope-fed) |
| 2257 | `vfdClient.sendAction(unitId, action, speedRefPercent)` | `/iot/fans/control` start/stop/reset | New `VfdAction` envelope Pi5→Nova |
| 2260 | `vfdClient.writeDrive(unitId, controlWord, speedRefPercent)` | `/iot/fans/control` raw control word | Same `VfdAction` envelope |
| 2283 | `vfdClient.getDrive(unitId)` | `/iot/fans/inject-fault` (now a 503-stub anyway) | Already retired |
| 2344-2352 | `vfdClient.getDrive` + `writeParam(unitId, addr, value)` | `/iot/fans/param` — write any single VFD parameter | Same `VfdAction` envelope (param-write variant) |
| 2368 | `vfdClient.setDriveMeta(unitId, ...)` | `/iot/fans/meta` — label, manufacturer | Bridge-only state (no Modbus); stays on bridge |
| 2391-2400 | `vfdClient.getDrives()` + per-drive action/control | `/iot/fans/set-all` | Bridge reads cache, iterates `VfdAction` envelopes |

## Migration target architecture

### Wire contracts to add

| Envelope | Direction | Tag | Purpose | Notes |
|---|---|---|---|---|
| `OrbitRegWrite` | Pi5→Nova | 124 (new) | Generalised orbit register write (FC06/FC16) — `{ slot, addr, values[] }` | Replaces direct `writeHoldingReg`/`writeHoldingRegs` calls for GDC + non-TRITON orbits. `TritonRegWrite` (125) stays as the existing tag for TRITON-specific writes that already work. |
| `OrbitGdcStatus` | Nova→Pi5 | 122 (new) | GDC actuator pos, stage assignment, calibration state — the 40-reg readback at HR 300..339 | Carries what `/iot/gdc` GET returns today. |
| `OrbitTritonExtended` | Nova→Pi5 | 123 (new) OR extend `SystemStatus` | TRITON setpoints + failure-mode tables (the parts NOT in SensorData) | Either a new submsg or extra fields on `SystemStatus`/`AlarmData`. Lean toward EXTENDING since UI already consumes SystemStatus. |
| `OrbitIoLive` | Nova→Pi5 | (new) | Per-orbit DI/DO/AO live state (the `/iot/orbit/status` per-board live probe) | OR add to `OrbitStatus` as optional fields. |
| `VfdStatus` | Nova→Pi5 | 200 | EXISTING — currently bridge-emitted. After migration, Nova emits over UART and the bridge passes through. | No proto change. |
| `VfdAction` | Pi5→Nova | (new) | Start / stop / reset / write-param / write-control-word | Replaces `vfdClient.sendAction`, `writeDrive`, `writeParam`. |

### Nova-side changes

1. **`Platform/Equipment/` or `lp_am2434/nova_vfd_client.c` (new)** — Modbus TCP client task running on the controller. Pollster like `orbit_client.c` but for VFD targets. Owns the VfdStatus envelope emission (today done in `protoStream.ts`).

2. **`lp_am2434/orbit_client.c` (extend)** — add GDC + Triton HR snapshot reads to the existing per-orbit poll loop. Emit `OrbitGdcStatus` / `OrbitTritonExtended` envelopes (or extend existing).

3. **`lp_am2434/nova_ota_broker.c` (or new `nova_orbit_writer.c`)** — handler for the new `OrbitRegWrite` envelope. Issues Modbus FC06/FC16 to the target orbit's `:5502`. Mostly a copy of the bridge's `orbitMbtcp.ts::writeHoldingRegs`.

4. **Settings storage for VFD config** — drive labels, manufacturer, unit IDs. Today this is in `vfd-drives` sim-config file on the bridge. Nova-side: needs OSPI persistence (small new HR region or settings blob field).

### Bridge-side changes

1. **`apiRoutes.ts`** — each `orbitMbtcp` / `vfdClient` callsite swaps to either:
   - Reading from `dataCache`/`novaStore` (for the read endpoints)
   - Calling `serialBridge.send<EnvelopeName>(...)` (for the write endpoints)

2. **`vfdClient.ts`** — strip the Modbus polling. Becomes a thin wrapper around `dataCache.getVfdDrives()` (or similar). Eventually deletable.

3. **`orbitMbtcp.ts`** — fully deletable after Sub-phase 1 + 2 land.

4. **`index.ts`** — drop `VFD_HOST` env handling and the `VFDClient` instantiation; bridge no longer needs the equipment-LAN interface for Modbus.

5. **`protoStream.ts`** — `encodeVfdStatus()` becomes a pass-through (decode from Nova-emitted envelope, re-emit on the WS) instead of building from `vfdClient` cache.

## Sub-phases

### Sub-phase 1 — Orbit reads (largest, lowest risk)

**Goal:** every `readHoldingRegs` / `readCoils` / `readDiscreteInputs` call in `apiRoutes.ts` reads from `dataCache` instead.

**Order of operations:**

1. **Audit which TRITON HR 400..542 fields the UI actually consumes** vs which are "we read them because we could". Skinny down the migration target if some bytes can simply be dropped.
2. **Decide envelope shape per-orbit-role.** Recommendation: extend `SystemStatus` with optional submsgs `OrbitGdcExtra`, `OrbitTritonExtra` rather than two new top-level envelopes — keeps the UI proto-store list short.
3. **Nova-side:** extend `lp_am2434/main.c::build_system_status_envelope` (already at `main.c:3015`) to include the new fields per role. The data is already polled by `orbit_client.c` on the controller; just plumb it into the envelope.
4. **Bridge-side:** `dataCache` gains getters for the new fields. `apiRoutes.ts` `/iot/gdc`, `/iot/triton/:slot`, `/iot/orbit/status` handlers swap from Modbus calls to `dataCache.getOrbitGdc(slot)` etc.
5. **Bench:** verify `/iot/gdc` + `/iot/triton/:slot` responses are unchanged before/after for the same orbit. Diff JSON.
6. **Once green, delete the orbit-read imports** from `apiRoutes.ts`.

**Acceptance:**
- ✅ `apiRoutes.ts` no longer imports `readHoldingRegs` / `readCoils` / `readDiscreteInputs` from `orbitMbtcp.ts`
- ✅ All `/iot/gdc`, `/iot/triton/*`, `/iot/orbit/status` GET responses byte-identical to pre-migration
- ✅ Bench fleet probe + UI Refrigeration page + UI Door page still work end-to-end
- ✅ With Pi5 firewall blocking outbound to `10.47.27.0/24:5502`, the UI still loads (proves migration is complete)

**Estimated effort:** 1 full session.

### Sub-phase 2 — Orbit writes (small, mostly mechanical)

**Goal:** every `writeHoldingReg` / `writeHoldingRegs` call in `apiRoutes.ts` goes through Nova via the existing `TritonRegWrite` envelope (already wired) plus a new generalised `OrbitRegWrite` envelope (tag 124) for GDC.

**Order of operations:**

1. **Add `OrbitRegWrite` envelope to `proto/agristar/envelope.proto`** (tag 124, mirror `TritonRegWrite` structure with role-agnostic addressing).
2. **Nova-side handler** in `nova_ota_broker.c` (or split out): receives `OrbitRegWrite`, opens TCP to `OrbitClient_GetIpv4(slot)`, issues FC06 or FC16, returns status (could be fire-and-forget like `TritonRegWrite`).
3. **Bridge-side `novaSerialBridge.ts`:** add `sendOrbitRegWrite(slot, addr, values[])`.
4. **`apiRoutes.ts`:** swap each `writeHoldingReg(host, addr, val)` → `serialBridge.orbitRegWrite(slot, addr, [val])`. Swap each `writeHoldingRegs(host, addr, vals)` → `serialBridge.orbitRegWrite(slot, addr, vals)`. The `/iot/triton/:slot/safety/reset` callsite at line 3157 stays on `tritonRegWrite` (already correct).
5. **Bench:** trigger `/iot/gdc/stages` POST + `/iot/gdc/calibrate` POST + verify HR readback matches.

**Acceptance:**
- ✅ `apiRoutes.ts` no longer imports `writeHoldingReg` / `writeHoldingRegs` from `orbitMbtcp.ts`
- ✅ `/iot/gdc/stages` + `/iot/gdc/calibrate` POSTs land at the GDC LP (verify via HR readback)

**Estimated effort:** ~½ session, mostly mechanical (Sub-phase 1's envelope pattern reused).

### Sub-phase 3 — VFD (new Nova-side task)

**Goal:** Nova owns Modbus to the VFD; bridge becomes pass-through.

**Order of operations:**

1. **Settings:** add VFD config fields to `lp_settings.h` — `vfd_host[4]` (IP), `vfd_port` (default 502), `vfd_unit_ids[8]`, `vfd_label[8][32]`, `vfd_manufacturer[8]`. Persist via OSPI like other settings.
2. **Nova-side:** write `Nova_Firmware/Platform/Equipment/nova_vfd_client.c` (or `lp_am2434/nova_vfd_client.c`):
   - Modbus TCP client (similar shape to `orbit_client.c`)
   - Polls each configured unit ID, reads CW/StatusWord/SpeedRef/ActualSpeed + Group 01 + Group 04 + Group 20/22/99 — same address map as `vfdClient.ts`
   - Caches `VFDDriveSnapshot`-equivalent in MSRAM
   - Encodes `VfdStatus` envelope (same layout as `vfd.proto`) — no proto change
   - Handler for `VfdAction` envelope (start/stop/reset/write-CW/write-param)
3. **Bridge-side `protoStream.ts`:** drop the bridge-side encoder (lines 285-329). Pass through Nova-emitted `VfdStatus` envelopes to the WS.
4. **Bridge-side `vfdClient.ts`:** delete (or stub `getDrives` to read from `vfdStateStore` populated by the proto stream).
5. **Bridge-side `apiRoutes.ts`:** swap `vfdClient.sendAction(...)` → `serialBridge.vfdAction(...)`.
6. **Bridge-side `index.ts`:** drop the `VFDClient` instantiation; drop `VFD_HOST` env handling.
7. **Bench:** point Nova at the bench VFD sim (`rs485Responder` on `:5020`), verify UI Fans page shows live drive data + start/stop/reset works.

**Acceptance:**
- ✅ `vfdClient.ts` deleted (or reduced to a metadata-only stub)
- ✅ Bridge `index.ts` no longer reads `VFD_HOST` env
- ✅ UI Fans page renders + control buttons fire with Pi5 firewall blocking outbound to VFD IP

**Estimated effort:** 1 full session (new Nova task + envelope handler + settings).

### Sub-phase 4 — Pi5 deployment hardening

**Goal:** Pi5 has no route to `10.47.27.0/24`. Proves the airgap.

**Order of operations:**

1. **Pi5 config:** remove the `10.47.27.x` interface (eth0 alias or whatever). Verify with `ip route` that there's no route. The bridge keeps the UART connection to Nova.
2. **Acceptance:** every UI page that previously worked still works. Any handler that secretly still talks Modbus surfaces as a 503.

**Estimated effort:** ~30 min, but bench-bound — needs physical Pi5 reconfig.

## Risk + rollback

| Sub-phase | Main risk | Rollback |
|---|---|---|
| 1 (orbit reads) | Envelope schema gets a field wrong, UI shows stale data | Re-enable the orbitMbtcp fallback path behind a feature flag; cherry-pick the bad envelope field |
| 2 (orbit writes) | OrbitRegWrite handler hits a bug, writes go silently to wrong addr | Existing `tritonRegWrite` path proves the pattern; mirror it carefully. Bench-verify each write with a follow-up read |
| 3 (VFD) | Nova-side VFD task introduces a regression in fan control on production rigs | Keep the bridge-side `vfdClient` behind a `BRIDGE_VFD_FALLBACK=1` env for the first deployment; remove after bench cycles look good |
| 4 (Pi5 config) | Some hidden route survives | Visual check via `ip route` + intentional `curl 10.47.27.2:5502` from Pi5 — must fail |

## Order of attack (recommended)

**Sub-phase 1 first** (orbit reads). Highest callsite count, lowest individual risk. Establishes the envelope pattern that Sub-phases 2 and 3 reuse.

**Sub-phase 2 second** (orbit writes). Smallest. Reuses Sub-phase 1's envelope/encoder/decoder pattern. Quick win.

**Sub-phase 3 third** (VFD). Most code (new Nova task + settings persistence + envelope handler) but cleanest isolation — no shared callsites with the other two sub-phases.

**Sub-phase 4 last**. After 1+2+3 land cleanly on the bench, reconfigure the Pi5 to prove the airgap.

## What this fix DOES NOT cover

- **Production wiring of the VFD.** Today's bench has the VFD on Pi5's reachable LAN (via `rs485Responder` sim on `:5020` or a real ACS310). In production, VFD must be on the equipment LAN that Nova reaches. PCB/install responsibility.
- **Master/slave fleet sync.** The bridge's `masterSlaveSync.ts` talks across multiple bridges over operator LAN. Unaffected by this migration (already operator-side).
- **The bridge's `simConfig.ts` files.** VFD label/manufacturer metadata stays on the bridge filesystem for now — it's not Modbus traffic, just operator-UI affordance. Could later migrate to OSPI but not in scope.

## References

- [`docs/uart-airgap-architecture.md`](uart-airgap-architecture.md) — §"Phase 4b — delete the dead direct-TCP modules" (the ☐ items this doc enumerates)
- [`memories/repo/bridge-mbtcp-migration.md`](../memories/repo/bridge-mbtcp-migration.md) — the previous migration (Apr 2026) that introduced `orbitMbtcp.ts`. This doc retires it.
- [`Nova_Firmware/lp_am2434/orbit_client.c`](../Nova_Firmware/lp_am2434/orbit_client.c) — existing controller-side Modbus polling that Sub-phase 1 extends.
- [`proto/agristar/envelope.proto`](../proto/agristar/envelope.proto) — envelope tag registry; new tags 122/123/124/(VfdAction) land here.
- [`proto/agristar/vfd.proto`](../proto/agristar/vfd.proto) — VfdStatus already defined; gets re-sourced from Nova in Sub-phase 3.
