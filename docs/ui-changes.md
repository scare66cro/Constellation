# UI Changes Log

Running log of intentional Svelte UI structure / navigation changes that
deviate from the legacy AS2 page layout. Use this to remember why a page
moved, why a menu entry disappeared, or why a legacy URL still resolves
somewhere unexpected.

Append new entries to the **top**. Keep each entry tight: one paragraph
of *what* + *why*, then a bullet list of touched files.

---

## 2026-05-30 — Phase 5 proto-direct landing (~30 +page.ts removed)

Commit `57769cd` — UI proto-direct landing (~30 per-page `+page.ts`
files deleted in favour of `useDraft` + `protoStores` + `writeProto`).
Most single-tag settings pages now use the canonical 5-line `useDraft`
boilerplate. Repeated-submsg pages (humidifier, refrigeration,
accounts, analog, auxiliary, ioconfig) stay on raw `writeProtoRaw`
because their per-row encoding semantics need explicit control.

- New page: `constellation-ui/src/routes/level2/orbit-sensors/+page.svelte`
  surfaces the per-orbit sensor bank stream (envelope tag 124).
- Ramp page removed (consolidated into plentemp per the 2026-04-21
  entry below; the `+page.ts` cleanup just finished the job).
- Per-page details captured in
  [`docs/proto-migration-pattern.md`](proto-migration-pattern.md).

---

## 2026-04-21 — Ramp Rate merged into Plenum Setpoints

The standalone `/level1/ramp` page was redundant — its only purpose was
to edit `RampRateSettings` (tag 43) which conceptually belongs with the
plenum-setpoint controls. Promoted the ramp form to the bottom of the
plenum-setpoints page as a second `Card` with its own `SaveButton` (so
ramp saves don't force-resubmit the plenum/alarm tags), and removed the
"Ramp Rate" dropdown menu entry. Legacy alias `mnRampRate.htm` and the
`/level1/ramp` URL still resolve to `/level1/plentemp` for any old
bookmark / footer redirect.

- [`constellation-ui/src/routes/level1/plentemp/+page.svelte`](../constellation-ui/src/routes/level1/plentemp/+page.svelte) — added ramp imports, state, reactive blocks, merged dirty check, and the second Card+SaveButton block.
- [`constellation-ui/src/routes/+layout.svelte`](../constellation-ui/src/routes/+layout.svelte) — removed `page-list.ramp-rate` menu entry.
- [`constellation-ui/src/lib/business/paging.ts`](../constellation-ui/src/lib/business/paging.ts) — `mnRampRate.htm` and `/level1/ramp` now resolve to `/level1/plentemp`.
- Deleted `constellation-ui/src/routes/level1/ramp/`.
