# Per-page proto-direct migration pattern

> Companion to [`proto-direct-redesign-plan.md`](proto-direct-redesign-plan.md).
> Read that first for the architecture context. This file is the
> **mechanical recipe** to apply to each page.

The Phase 0.7 scaffold (UI proto store + bridge `/proto/stream`) lets a
page replace its CGI/CSV plumbing with three lines of import. The shape
of the change is the same on every page; the only thing that varies is
which protobuf message the page actually needs.

---

## 1. Identify the proto message(s) you need

Open `proto/agristar/envelope.proto` and find the `oneof msg` field that
matches the data the page consumes today. Make a note of:

- the **field number** (e.g. `equipment_status = 11`),
- the **message type** (e.g. `EquipmentStatus`),
- the **proto file** it lives in (e.g. `equipment.proto` →
  `$proto/agristar/equipment.js`).

If the page consumes multiple kinds of data, list them all.

## 2. Make sure each tag is wired in `protoTags.ts`

Open [`constellation-ui/src/lib/business/protoTags.ts`](../constellation-ui/src/lib/business/protoTags.ts).
For each message:

1. Import the type at the top of the file.
2. Add an entry to `TAG` (use the field number as the value).
3. Add the type to `TagPayload`.
4. Add the decoder to `TAG_DECODERS`.

The TS compiler will yell if any of the four steps are missed — the
mapping is fully type-checked.

## 3. Export a per-tag store from `protoStores.ts`

In [`constellation-ui/src/lib/business/protoStores.ts`](../constellation-ui/src/lib/business/protoStores.ts),
add **one line** per tag (alphabetical):

```ts
export const equipmentStatus = createTagStore(TAG.EquipmentStatus);
```

The store is `Readable<MyMessage | null>`. It is `null` until the first
frame arrives. The bridge replays the latest cached frame on subscribe
(see `protoStream.replayTag`), so the store usually populates within a
single round-trip — no explicit prefetch needed.

## 4. Migrate the page

Strip the page down to:

```svelte
<script lang="ts">
  import { equipmentStatus } from '$lib/business/protoStores';
  // ...
</script>

{#if $equipmentStatus}
  <p>{$equipmentStatus.machineState}</p>
{/if}
```

Delete:

- the `+page.ts` server loader (the bridge replays on subscribe),
- the `WsClient` import + lifecycle (`onMount(connect)` /
  `onDestroy(close)`),
- any `parseSensorFeeds`-style CSV parser,
- any `loadIotData('/iot/...')` call that fetched the same payload.

If the page imports legacy helpers that take CSV-shaped data
(e.g. `SensorInfo` arrays), write a small adapter inline that maps from
the proto type to the legacy shape — keep helper signatures stable so
other pages still in CSV-land aren't disturbed. The pile page is the
canonical example of this pattern.

## 5. Verify

1. Restart the bridge:
   `pwsh F:\Constellation\Start-Constellation.ps1 -Restart`
2. Open the page in the running UI; confirm data renders.
3. The bridge log should show one new `[ProtoStream] subscribe tags=[N]`
   line per page mount (and a matching `unsubscribe` on unmount). If
   `acquireTag` ref-counting is working, repeated mounts of the same
   tag share the underlying server subscription.

## 5b. Write path — `writeProto()` (Phase 0.4+)

For pages that save settings, the eventual cutover from
`SaveButton`'s `POST /iot/<page>` to typed proto is:

```ts
import { writeProto } from '$lib/business/protoWrite';
import { TAG } from '$lib/business/protoTags';

// instead of fetch('/iot/alerts', { method:'POST', body:JSON.stringify(alerts) })
await writeProto(TAG.AlertSettings, { alertFlags });
```

What this does:
1. Looks up the **firmware SettingsUpdate field number** for the tag
   in `SETTINGS_FIELD` (e.g. `TAG.AlertSettings → 35`).
2. Encodes the message via the same ts-proto type used for reads
   (`TAG_DECODERS[tag]`).
3. POSTs raw bytes to `/proto/write/:field` on the bridge as
   `application/octet-stream`.
4. Throws `ProtoWriteError` on 4xx/5xx; caller shows toast and leaves
   SaveButton in dirty state.

**Verification:** writeProto returning `ok:true` only means the bridge
forwarded the envelope over UART. Value persistence is confirmed by the
corresponding `/proto/stream` tag push echoing your write back into the
subscribed store (the live-push pattern from §4 already handles this —
the page re-renders from the store, not from the POST response).

⚠ **Do NOT migrate a page's write path until:**
- The page's current save semantics (filtered index spaces, bitstrings,
  mode-positional layouts) have been moved from `apiRoutes.ts` +
  `legacyShim.ts` into the UI, OR
- The proto schema for that page has been reshaped to absorb them (e.g.
  `oneof` per mode for mode-bearing pages — see Phase 3 schema
  improvements in the redesign plan).

If neither is true, leave the page on the legacy write path — the
hybrid (proto read + legacy write) is intentional and stable.

⚠ **`SETTINGS_FIELD` authority is the firmware switch**
(`Nova_Firmware/Platform/nova_dataexc.c::handle_settings_update()`),
NOT the proto `SettingsUpdate.payload` oneof. They have diverged. If
you add a new settings write, check the firmware switch first and add
the entry to both `SETTINGS_FIELD` and the firmware switch together.

## 6. Cleanup checklist (per page)

- [ ] All consumers of the legacy CGI varName have been migrated.
  - If yes, delete the corresponding `dataStore.on(...)` →
    `dataCache.updateFromArm(...)` translator in
    `constellation-ui/server/src/novaAdapter.ts`.
  - Delete the `case` in `wsManager.ts::buildPushPayload`.
  - Delete the `/iot/<endpoint>` route in `apiRoutes.ts`.
- [ ] `legacyShim.ts` no longer references the varName.
- [ ] No tests/specs still hit the deleted endpoint.

These deletions are part of **Phase 5** (bulk CGI/CSV adapter removal),
not the per-page migration — but list them in the page's PR so the
follow-up sweep is mechanical.

---

## Pitfalls

- **proto3 zero-suppression**: numeric fields with value `0` are absent
  from the wire. Treat `undefined` and `0` as equivalent in the UI.
- **Repeated arrays**: ts-proto returns `[]` for absent repeated fields,
  not `undefined`. Use `?.` defensively anyway in case the store is
  still `null`.
- **Stale labels**: `SensorLabels` is sent on connect / change. The
  bridge cache + replay-on-subscribe handles this — the UI does not
  need to request it explicitly. If labels seem stale, check
  `NovaDataStore.rawMessages.get(28)` is being populated.
- **Subscribing during SSR**: the singleton `protoStream` no-ops if
  `window` is undefined. Stores still construct safely; they just won't
  receive frames until the client hydrates.
