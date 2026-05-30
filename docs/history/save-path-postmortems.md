# Save-path post-mortems — resolved bugs

> **tl;dr** — Resolved save-path bugs and their lessons. The lessons
> drove the perennial rules in
> [`../firmware-bridge-protocol.md`](../firmware-bridge-protocol.md);
> the stories live here so the main doc stays a clean rule list. When
> a save mysteriously doesn't stick, scan this file for similar symptoms.
>
> **Last updated:** 2026-05-07.

---

## Runtimes (`/iot/runclock`, SettingsUpdate field 33, MSG_RUNTIMES=14) — RESOLVED 2026-04-20

Originally diagnosed as "firmware never ACKs". Re-investigated and
fixed; the firmware was always responding correctly. Three real bugs:

1. **legacyShim splitter bug** — `index.ts` runTimes handler used
   `firstVal.split('+')` but `apiRoutes.ts pageSaveMap.runtimes` joins
   with `,`. The `+` comment in apiRoutes was stale; no such conversion
   ever happened. Fixed: `split(',')`, pad to 48.
2. **novaAdapter wrong cache key** — listener called
   `dataCache.updateFromArm('RunTimesData', ...)`, which is the
   *varName*. `updateFromArm` keys by *msgTag*. Fixed:
   `updateFromArm('runTimes', ...)`. Without this, periodic firmware
   re-broadcasts never landed in the cache, so `/iot/runtimes` always
   returned the static seed (48 × RC_COOLING from
   `nova_thread_overrides.c::ReadAnalogBoards()`).
3. **Proto3 zero-suppression** on RC_OFF (=0) slots — both encoders
   (firmware `NovaMsg_SendRuntimes` slot index, and bridge legacyShim
   value field) used the suppress-zero variants. Decoder increments
   `idx` per parsed value, so dropped zeros misaligned the entire
   schedule. Fixed: `pb_uint32_force` (firmware) and `pbVarintForce`
   (bridge) for both fields.

Round-trip verified with 48-slot pattern `0,2,5,…` → Match: True,
`rxCrcErrors:0`. Page now uses live-push pattern (subscribes to
`runtimes` proto store).

**Lesson (codified as protocol rule 1):** when a firmware save "hangs",
first check the cache key and varint zero-suppression on every field
before suspecting the firmware. The msgTag-vs-varName trap is silent
(returns false from `updateFromArm` but no log) and accounts for the
majority of "settings don't persist" reports.

---

## Bridge UART RX ring overflow — RESOLVED Apr 2026

Symptom: every `/proto/write/<field>` POST returns `503 Command
timeout (seq=N, msg=90)`, the bridge log shows `[NovaBridge]
Connected — firmware ready` re-firing every ~50 s (one per
`UI_SendAllSettings` burst), and the firmware UART0 trace shows
`[Nova] RX bytes=N overflows=0` with `N` **stuck at the same value
across many log cycles** — i.e. firmware is no longer ingesting bytes
from the bridge even though TX from firmware to bridge keeps flowing
(`rxFrames` on `/health` keeps growing).

Root cause: `Usart_ISR()` in `Nova_Firmware/Platform/nova_usart.c` was
draining the QEMU 16550 RX FIFO into a 600-byte SPSC ring buffer with
**no full-detection**. When the producer wraps far enough to land
WriteIndex on top of ReadIndex, `Usart_CharsBuffered()` returns 0
forever (full state aliases to empty state). Every byte after that is
silently overwritten and the bridge's command stream stops being
processed; from the bridge's POV firmware just goes quiet on the RX
path.

OSPI persistence keeps working through this state — the firmware's
`SaveSettingsRequest` flag is non-blocking and the periodic 60-min
auto-save still fires — which is why `[Settings] Loaded … seq=N` keeps
advancing across reboots even though the user's UI saves never
landed. That advancing seq is misleading; do **not** treat it as
evidence the save path is healthy.

**Fix** (`nova_usart.c`):

- Reserve one slot as the empty/full discriminator. Producer writes
  only when `(Write+1) % SIZE != Read`; on full, drop the byte and
  bump a `s_rx_overflows` counter. The COBS+CRC framer in
  `NovaProto_FeedByte` re-syncs on the next `0x00` delimiter, so
  dropping is preferable to overwriting (which would corrupt an
  in-flight valid frame and surface as a phantom `rxCrcErrors`).
- Surface `Usart_GetOverflows()` and include it in the periodic
  `[Nova] RX bytes=N overflows=M` debug log so a non-zero / growing
  `M` is the first signal that the bridge is producing faster than
  `ThreadUIUpdate` can drain — the *real* "saves time out" symptom.
- Bridge-side cleanup (`novaSerialBridge.ts`): suppress the
  `Connected — firmware ready` log when already connected so the
  recurring `DataLoadStatus(ready=true)` from `UI_SendAllSettings`
  doesn't masquerade as repeated re-connections in `journalctl`.

This was sim-relevant because QEMU's chardev pumps bytes faster than
115200 / 230400 baud (no rate-limit), so the polled drain in
`ThreadUIUpdate` (vTaskDelay-1 cadence) loses the back-pressure that
would exist on real hardware. On the AM2434 the bridge UART is
interrupt-driven and back-pressure honoured — but the ring discipline
is the same, so this fix is correct on both platforms and should not
be reverted at the production cutover.

**Verification recipe:**

1. Restart firmware (or wait for an overflow event during long-running
   operation).
2. Issue a save: `Invoke-WebRequest -Uri http://localhost:9001/proto/write/13 -Method POST -ContentType application/octet-stream -Body ([byte[]](0x68,0x01))`.
3. Expect `{ok:true, field:13, bytes:2}` and `[Settings] Saved … seq=N+1`
   in UART0 within 2 s.
4. Drive a sustained burst (5–10 saves back to back) and check the
   `RX bytes=… overflows=…` log at the end — `overflows` must stay 0
   in normal operation; any non-zero value points at a thread-starvation
   regression in `ThreadUIUpdate`'s drain rate.

---

## SaveButton autoSave + nested-array Select trap — RESOLVED 2026-04-22

Symptom: a Level-1 page (`level1/humidifier`, H1 → Manual) accepts the
mode change in the UI but **never POSTs** to `/proto/write/<field>`.
The on-wire / firmware / OSPI persistence path is provably fine — a
hand-built `/proto/write/11` body (`0a 12 18 … 08 00 10 00`)
round-trips correctly: firmware decoder honours the force-emitted
`index=0 / mode=0`, the new `Settings.HumidCtrl[0].Mode` survives
`Start-Constellation -Restart`, and the broadcast `tag 50` payload
reflects the change. The bug lived strictly in the SvelteKit page.

Root cause: `SaveButton.svelte`'s autoSave watcher

```svelte
$: if (autoSave && edit && data && original && $keyboardStore.hidden) {
  scheduleAutoSave(data, original);
}
```

is reactive on the **`data` prop reference**, not on its contents. A
typical Level-1 page binds `data={someObj.matrix[index]}` and writes
into individual cells via `bind:value={someObj.matrix[index][col]}`.
Mutating an inner cell does **not** change the outer array's identity,
so Svelte does not invalidate the `data` prop and the watcher does not
re-run. Pages with TextField rows incidentally trigger the watcher via
`$keyboardStore.hidden` toggling (keyboard show → hide), which masks
the bug. A pure-Select mutation (humidifier mode → Manual hides the
duty-cycle TextFields) leaves no incidental trigger and autoSave is
**permanently silent** for that mode change. Because `<SaveButton
autoSave>` renders no manual save button (only a status indicator),
the user has no fallback and the change appears to be silently dropped.

**Initial fix** at the Select call site:

```svelte
<Select bind:value={obj.matrix[index][col]} ...
  on:change={() => {
    /* Force `data` prop identity to change so SaveButton's autoSave
     * watcher re-evaluates. Mutating obj.matrix[index][col] alone does
     * not invalidate the matrix[index] array reference.  */
    obj.matrix[index] = [...obj.matrix[index]];
  }} />
```

Applied 2026-04-21 to
`constellation-ui/src/routes/level1/humidifier/+page.svelte` auto-control
mode Select.

**Permanent fix (2026-04-22) — SaveButton refactor.** `SaveButton.svelte`
now keeps a `lastScheduledSnapshot` (JSON-stringified `data`) and only
restarts the debounce timer when the snapshot actually changes. A hard
ceiling (`AUTOSAVE_MAX_WAIT_MS = 5000`) guarantees the save fires even
if the parent stays hot-reactive forever. Without this, the watcher
re-fires on every unrelated parent invalidation (e.g. the ~1 Hz
`equipmentStatus` push that mutates `humidifier.humidStatus` on every
humidifier page render) and `clearTimeout` wipes the pending 1.2 s
debounce indefinitely — the first save after page load *appears* to
schedule (status flips to "saving") but the timer never expires.

UI also moved to icon-only autosave indicator (spinning blue circle /
green check / red retry) — word-pills were too subtle on the touch
screen and users couldn't tell whether saves were landing.

**Lesson:** any time a SaveButton autoSave watcher hangs on a settings
page, look for nested-array bindings first. The fix lives in the
SaveButton itself now; per-call-site `[...spread]` workarounds are no
longer needed.

---

## "Continue without saving?" prompt — RESOLVED 2026-04-22 (keyboard-gated)

`GellertFooter.checkDirty` (and the page-local copies in
`level2/analog/+page.svelte` + `level2/auxiliary/+page.svelte`) now
**only** raises the confirm modal when `$keyboardStore.hidden === false`,
i.e. the user is actively typing into a TextField. Rationale:

- Almost every Save UX is now `<SaveButton autoSave>` with a 1.2 s
  debounce gated on the keyboard being hidden. By the time the user
  taps Next / Prev / Home with no keyboard up, autoSave has already
  persisted any change (or there was no change to begin with).
- The old prompt produced false positives whenever a ~1 Hz store push
  briefly desynced the page's `original` snapshot from `data` — the
  user got "are you sure?" after doing nothing, which trained them to
  ignore it.
- Pages without autoSave (TCP/IP, accounts, basic, date, service)
  still raise the prompt while the keyboard is up because the user is
  observably mid-edit.

The page-side `$navigationStore.isDirty = …` hooks are kept intact:
several level1 pages (humidifier, fanboost, co2, outside) use them as
**hydration guards** to prevent firmware echoes from clobbering
in-flight edits. That's a separate concern from the navigation prompt
and must not be deleted.
