# Proto-Direct Redesign Plan

> **Status:** Phase 5 active (May 2026). Most pages are proto-direct;
> the bridge is a transparent transport gateway. New work follows the
> patterns documented here. Per-page migration recipe lives in
> [`proto-migration-pattern.md`](./proto-migration-pattern.md).

The original goal — eliminate the bridge's CSV-translation layer so the
SvelteKit UI consumes typed protobuf directly — is largely achieved.
What remains is per-page write cutover on the holdouts and a final
`legacyShim` deletion sweep.

## Current state (May 2026)

### Architecture

```
┌──────────────────────────┐
│  SvelteKit UI            │  proto-direct stores (one per envelope tag)
│  ─ typed proto stores    │  + ProtoDraft<K> via useDraft.ts
│  ─ writeProto(TAG, msg)  │
└──────────┬───────────────┘
           │ /proto/stream  : binary [u16 LE tag][u32 LE len][payload]
           │ /proto/write/* : POST encoded SettingsUpdate
           │ /iot/*         : remaining legacy CSV (shrinking)
┌──────────▼───────────────┐
│  Bridge (rpi5)           │  ~stateless transport gateway
│  ─ UART ↔ COBS+CRC       │  + small composites (frontMatter, header)
│  ─ /proto/stream fanout  │  + bridge-emitted tags 200-209 (VFD, etc.)
│  ─ legacyShim: shrinking │
└──────────┬───────────────┘
           │ COBS+CRC envelope over UART2 @ 921600
┌──────────▼───────────────┐
│  CONTROLLER LP-AM2434    │  source of truth (OSPI banks)
└──────────────────────────┘
```

### What's done

- **Phase 0 (foundation):** `/proto/stream` WS server in
  [`protoStream.ts`](../constellation-ui/server/src/protoStream.ts);
  browser client + per-tag store factory in
  [`protoStreamClient.ts`](../constellation-ui/src/lib/business/protoStreamClient.ts)
  + [`protoStores.ts`](../constellation-ui/src/lib/business/protoStores.ts).
  Initial-snapshot replay, JSON control plane, ping keepalive.
- **Phase 0.4 (write foundation):** `POST /proto/write/:settingsField`
  endpoint + UI [`writeProto()`](../constellation-ui/src/lib/business/protoWrite.ts)
  helper with `SETTINGS_FIELD` envelope-tag → firmware-field map.
- **Phase 0.2 schema gaps:** All 9 originally-identified gaps closed
  (`MasterSlaveSettings`, `BasicSetup.net_monitor_enabled`,
  `NetworkConfig`, `VfdStatus` + `vfd.proto`, etc.).
- **Phase 1 (pilot — pile + level2/analog):** Recipe captured in
  `proto-migration-pattern.md`.
- **Phase 2 (read-only pages):** All major read-only pages on
  proto-direct stores. `frontMatterComposite` + `headerComposite`
  derived stores reproduce the legacy WS channels client-side from
  typed proto stores. Bridge-side `frontmatter-data` and `header-data`
  channels deleted.
- **Phase 3 (settings pages):** Most settings pages migrated. TCP/IP,
  IO config, equipment, refrigeration, fans, burner, climacell, door,
  master, service, fanboost, miscellaneous, log, lights, fanspeed,
  outside, co2, email, plenum temp set, etc.
- **Phase 5.2 (`useDraft` rollout):** Single-message settings pages
  use `useDraft<K>(...)` returning `{ draft, hydrated, dirty, live,
  save, revert }`. Companion `numField()` helper for numeric
  text-bindings without losing typing. Boilerplate per page is now
  ~5 lines instead of ~50.
- **VFD bridge frames:** `VfdStatus` (envelope tag 200) emitted by the
  bridge from `vfdClient.getDrives()`. Bridge-emitted tag range is
  200-209.

### What's left

| Item | Status | Notes |
|------|--------|-------|
| Repeated-submsg pages (humidifier, refrigeration, accounts, analog, auxiliary, ioconfig) | proto-direct READ + raw `writeProtoRaw` WRITE | Intentionally not on `useDraft`; the per-row repeat semantics need explicit encoding. Keep as-is. |
| Encrypted pages (basic, tcpip) | proto-direct READ + DH+AES POST | DH+AES auth path stays on `/iot/dlr` until Phase 4 routes it through `/proto/control`. Low priority. |
| `legacyShim.ts` | **rename pending** — every remaining branch is an imperative command dispatcher (button presses, system commands, log clears), not a CSV translator. See [`/memories/repo/legacyshim-audit-may2026.md`](/memories/repo/legacyshim-audit-may2026.md). |
| `/iot/settings/restore` | **broken** — POSTs `RestoreSettings=…` strings to a deleted shim handler. Needs proto-direct restore implementation in `level2/settings/+page.svelte`. |
| `dataCache.ts` CGI variables | passthrough only | `buildFrontmatterData` (~280 LOC) deleted. `getIoNamesCsv()` survives only for Azure-cloud `djangoSync`; delete when cloud migrates. `buildTcpIpData()` is the only remaining composer. |
| Firmware `pb_uint32_force` audit | continuous | Schema-level `(agristar.force_zero) = true` annotation deferred — track as a single follow-up commit, not per page. |

## Patterns

### Single-tag settings page (use `useDraft`)

```ts
import { useDraft } from '$lib/business/useDraft';
import { burnerSettings } from '$lib/business/protoStores';
import { TAG } from '$lib/business/protoTags';

const { draft, hydrated, dirty, live, save, revert } =
  useDraft(burnerSettings, TAG.BurnerSettings);

// In <input bind:value={$draft.someField}> the draft mutates the
// editing copy; save() encodes and POSTs; revert() drops local edits.
```

⚠ **`ProtoDraft` is NOT a Svelte store.** Destructure first; treating
the return value as a store directly produced 30+ TS errors on the
burner page on first attempt. `dirty` is `boolean | null`; coerce
`!!(…)` when binding to a strictly-bool prop.

### Repeated-submsg page (raw write)

For pages where the on-the-wire encoding has per-row semantics
(`forceVarints` for slot-0, mode-positional payloads, etc.), keep the
explicit `writeProtoRaw` path:

```ts
await writeProto(
  TAG.IoNameUpdate,
  { index: idx, newName: name },
  { forceVarints: { 1: idx } }   // proto3 zero-suppression escape
);
```

### Composite derived stores

When a legacy channel mixes ~10 proto messages plus translation, build
a client-side `Readable<X>` derived store and let `+layout.svelte`
write the result into the existing global store ([`frontMatterStore`],
[`headersStore`], …). Single-point bridge keeps every consumer page
unchanged. See:

- [`frontMatterComposite.ts`](../constellation-ui/src/lib/business/frontMatterComposite.ts)
- [`headerComposite.ts`](../constellation-ui/src/lib/business/headerComposite.ts)
- [`equipmentComposite` in protoStores.ts](../constellation-ui/src/lib/business/protoStores.ts)

## Hard rules (still applicable)

1. **Field map authority is firmware `handle_settings_update()`**, not
   the proto `SettingsUpdate.payload` oneof. They have diverged
   historically; regenerate the proto from firmware, never the other
   way around.
2. **proto3 zero-suppression** — `pb_uint32` skips 0; use
   `pb_uint32_force` whenever 0 is meaningful (Mode=OFF, index=0,
   threshold=0, fixed-length array slot 0, cross-coupled settings).
   Same applies to TS-side: `forceVarints` option on `writeProto`.
3. **Field numbers immutable.** Once chosen, `reserved` if removed.
4. **One commit = one migration.** No drive-by refactors in migration
   commits.
5. **Per-page save verification:** save → reset board (DSS
   `system_reset.js`) → reload page → value persists. `/health` shows
   `rxCrcErrors:0 rxCobsErrors:0` after any save campaign.

## Sim-vs-prod note

There is no simulator anymore — see
[`docs/Simulator-to-Production-Transition.md`](./Simulator-to-Production-Transition.md).
Save-path verification runs against a real LP board over UART. The
"power-cycle QEMU" step from earlier phases is now "DSS `system_reset.js`
on the CONTROLLER LP" (or a USB-C cycle).

---

## History

The day-by-day chronology of the original Apr-2026 migration is
preserved here for context. Most rows are now subsumed by the
"What's done" section above.

| Date | Phase / Page | Notes |
|------|--------------|-------|
| 2026-04-19 | Plan created | Linked from `CLAUDE.md`. |
| 2026-04-19 | Phase 0.1 — CGI→proto audit | Identified 31 direct, 47 synthesized, 12 schema gaps, 2 bridge-owned (VFD). |
| 2026-04-19 | Phase 0.2 — schema gaps closed | `MasterSlaveSettings` (env tag 66), `BasicSetup.net_monitor_enabled`, `NetworkConfig` (env tag 19), bridge-only `VfdStatus` + `vfd.proto` (env tag 200, new 200-209 range). C + TS bindings regenerated. |
| 2026-04-19 | Phase 0.3-0.6 — `/proto/stream` live | Frame format `[u16 LE tag][u32 LE len][payload]`. JSON control plane. Initial-snapshot replay. Bridge-emitted `VfdStatus` synthesized via ts-proto. |
| 2026-04-19 | Phase 0.7 — UI proto store scaffold | `$proto` alias, `protoTags.ts`, `protoStreamClient.ts`, `protoStores.ts`, `@bufbuild/protobuf` installed. |
| 2026-04-19 | Phase 1 — pile page | First read-only page migrated. `level1/pile/+page.svelte` on `sensorData` + `sensorLabels`; `WsClient`/`parseSensorFeeds` deleted. |
| 2026-04-19 | Phase 2 — level2/fans | VFD page on push-based `vfdStatus` proto store, eliminated 5s `/vfd/fans` poll. `VfdDrive` extended with fields 11–34. |
| 2026-04-19 | Phase 3 starts — PWM rename + equipment composite | Renamed `PwmChannel.frequency`→`PwmChannel.port` (wire-compatible). `equipmentComposite` derived store reproduces bridge composition client-side. |
| 2026-04-19 | Phase 3 — level2/tcpip | `NetworkConfig` extended with `public_ip`+`dns`. Bridge POST persists to `tcpip.json` + broadcasts. |
| 2026-04-19 | level1/preferences | Already proto-clean; added `GET/POST /iot/locale` backed by `simConfig('locale')`. |
| 2026-04-19 | `frontMatterComposite` shipped | 71/71 fields parity vs bridge `/iot/frontmatter`. Single-point bridge in `+layout.svelte` writes composite into `$frontMatterStore` — every page reads composite-derived values, zero per-page edits. |
| 2026-04-19 | Phase 5 cleanup — `frontmatter-data` deleted | Bridge has no `frontmatter-data` channel anywhere; `dataCache.buildFrontmatterData` (~290 LOC) deleted. |
| 2026-04-19 | `header-data` migrated to `headerComposite` | `+layout.svelte` writes composite into `headersStore`. `header-data` WS push removed from `wsManager`. |
| 2026-04-19 | VFD-writes deferral closed | `/vfd/fans/{control,param,inject-fault,config}` stays as a clean REST→Modbus gateway. NOT part of proto-direct scope (real Modbus, not firmware-routed). |
| 2026-04-28 | Phase 5.2 — `useDraft` rollout | Single-message pages migrated: miscellaneous, fanboost, master L2, service L1+L2, climacell L2, burner L2, door L2, email L1, log L2, lights L1, fanspeed L1, outside L1 (multi-tag), co2 (kept `writeProtoRaw` dual-tag). `npx svelte-check` 0 errors. |
| 2026-05-02 | Plan condensed to current-state | Per-page chronology kept in this section; main body now describes the live architecture and hard rules. |
