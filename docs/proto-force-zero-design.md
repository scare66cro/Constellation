# `force_zero` — design for eliminating `forceVarints` boilerplate

**Status:** design only, May 2026. No code change yet.
**Owner:** queued as todo #8.

## Problem

proto3 encoders skip scalar fields whose value equals the type default
(0, 0.0, "", false). For most settings that's fine, but a number of
firmware fields treat 0 as a meaningful operating value (mode=OFF,
index=0, threshold=0, "stop fan"). For those, the UI must force the
value onto the wire even when it equals the default.

Today every page that touches such a field carries an inline
`forceVarints: { 2: $draft.port, 7: $draft.enabled, … }` (or
`forceFloats`) literal at every save call. There are 19 such call
sites today across 13 pages, with field-number magic numbers that
must stay in sync with the `.proto` schema.

The `forceVarints` / `forceFloats` mechanism in
[protoWrite.ts](../constellation-ui/src/lib/business/protoWrite.ts)
itself is correct and not the problem — the problem is that **knowledge
of which fields need forcing lives in the call sites instead of the
schema**.

## Goal

Move the "this field must always be on the wire" declaration into the
`.proto` file once, and have both ts-proto (UI-side encoders) and
nanopb (firmware-side decoders) honour it automatically. Pages then
write:

```ts
await writeProto(TAG.AccountSettings, $draft);   // no opts arg
```

…and the encoder emits `port`/`enabled`/`authType` even when they're
zero, because the schema says so.

## Approach A — proto-level field option (canonical)

### Schema

Add a custom field option in a new `proto/agristar/options.proto`:

```proto
syntax = "proto2";
package agristar;
import "google/protobuf/descriptor.proto";

extend google.protobuf.FieldOptions {
  // When true, the encoder MUST emit this scalar field even when its
  // value equals the proto3 default. Required for fields where 0 / ""
  // is a meaningful operating value (mode=OFF, idx=0, threshold=0).
  optional bool force_zero = 65001;
}
```

Then in `settings.proto`:

```proto
message AccountSettings {
  optional uint32 port     = 2 [(agristar.force_zero) = true];
  optional uint32 enabled  = 7 [(agristar.force_zero) = true];
  optional uint32 authType = 8 [(agristar.force_zero) = true];
  // …
}
```

### UI side (ts-proto) — the hard part

ts-proto 2.x emits encoders like:

```ts
encode(message, writer) {
  if (message.port !== 0) { writer.uint32(16).uint32(message.port); }
  …
}
```

There is **no extension hook** for custom field options. Honouring
`force_zero` requires one of:

1. **Fork ts-proto.** Add a custom-options pass that reads the raw
   `FieldDescriptorProto.options` and, when `force_zero` is set,
   removes the `if (message.x !== 0)` guard. Pin the fork in
   `constellation-ui/server/package.json` via a git URL. Maintenance
   cost: re-rebase on every ts-proto release we care about.
2. **Post-process generated `.ts` files.** A small node script run
   after `proto/generate.ps1` that walks the AST of generated
   encoders and strips the guard for known force_zero fields. Brittle
   against ts-proto output changes.
3. **Plugin architecture.** None exists today; would require upstream
   ts-proto change.

### Firmware side (nanopb)

nanopb honours `proto2 optional` fields with a `has_field` member —
already supported. The generated decoder already accepts a wire-
present zero. The work here is **none** for the decode path; on the
encode path (firmware → bridge for stream messages), nanopb's
`PB_ENCODE_DELIMITED` flag and the existing oneof / has_field model
cover what we need. Nothing to change.

### Cost / Benefit

- One-time: fork ts-proto + maintenance, ~1 day initial + ongoing.
- Recurring: zero per-page cost. New pages just annotate the proto.
- Risk: ts-proto fork drifts from upstream; broken on next major.
- Benefit: schema is single source of truth; field-number magic
  numbers gone from the UI.

## Approach B — TS-side per-tag registry (intermediate)

Keep ts-proto unmodified. Add
`constellation-ui/src/lib/business/forceFieldRegistry.ts`:

```ts
export const FORCE_FIELDS: Partial<Record<Tag, ForceSpec[]>> = {
  [TAG.AccountSettings]: [
    { name: 'port',     num: 2, type: 'int' },
    { name: 'enabled',  num: 7, type: 'int' },
    { name: 'authType', num: 8, type: 'int' },
  ],
  [TAG.FanSpeedSettings]: [
    { name: 'tempDiff', num: 6, type: 'float' },
    // …
  ],
  // …
};
```

`writeProto` consults the registry, pulls values out of the typed
message by property name, and merges with explicit opts (explicit
wins for back-compat).

### Cost / Benefit

- One-time: ~2 hours to build, ~1 hour per page to migrate to drop
  `forceVarints` arg (13 pages = 1 evening).
- Recurring: new pages add an entry to one file (still magic numbers,
  but in one location, not 13).
- Risk: registry vs proto can drift; needs a CI check.
- Benefit: 80% of the ergonomic win for 5% of the engineering cost
  of Approach A.

## Recommendation

**Do Approach B first**, ship it, live with it for a release cycle.
Re-evaluate Approach A only if:

- We add 5+ more force-zero fields and the registry maintenance
  becomes painful, OR
- ts-proto upstream lands a custom-options plugin API (track
  https://github.com/stephenh/ts-proto/issues for this).

Approach B is fully reversible: if/when Approach A ships,
`FORCE_FIELDS` becomes the authoritative source for the
codegen-time annotation pass and is then deleted.

## Migration plan (Approach B)

1. Build `forceFieldRegistry.ts` with all 19 current call sites
   pre-populated.
2. Wire `writeProto` to consult it; explicit opts still win.
3. Sweep the 13 pages: drop the `forceVarints` arg, leave the call
   as `writeProto(TAG.X, $draft)`.
4. Add a unit test that walks every entry in `FORCE_FIELDS` and
   verifies the named property exists on the corresponding ts-proto
   message type (compile-time-checked via lookup type).
5. Document in [`firmware-bridge-protocol.md`](firmware-bridge-protocol.md):
   "When a settings field's 0 value is meaningful, add to
   `FORCE_FIELDS` in `forceFieldRegistry.ts`; do not pass
   `forceVarints` opt at call sites."

## Out of scope

- Repeated-submsg pages on `writeProtoRaw` (humidifier,
  refrigeration, accounts, analog, auxiliary, ioconfig). Their
  encoding shape is mode-positional and won't benefit from per-field
  force annotations.
- nanopb encode-side changes — not needed for this work.
- Bridge-side changes — bridge is a transparent passthrough; it
  doesn't care which fields are present.
