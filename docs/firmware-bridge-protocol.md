# Nova Firmware ↔ Bridge Wire Protocol

> **tl;dr** — Hard-won invariants for code that crosses the COBS+CRC
> envelope between Nova AM2434 firmware (`Nova_Firmware/Platform/`) and
> the TypeScript bridge (`constellation-ui/server/src/`). **Violations
> are silent** — the wire ack still returns `{"ok":true}`, but the
> setting either didn't move, moved to the wrong slot, or overlaid stale
> state. Every numbered section below is a perennial rule. Resolved bug
> stories live in [`history/save-path-postmortems.md`](history/save-path-postmortems.md).
>
> **Last updated:** 2026-05-07 (split out post-mortems).

---

## 1. proto3 zero-suppression — the #1 footgun

Both sides have two encoder families:

| Family | Behaviour |
|---|---|
| `pb_uint32` (firmware) / `pbVarint` (bridge) | Skip emission when `val == 0` (proto3 default semantics). |
| `pb_uint32_force` (firmware) / `pbVarintForce` (bridge) | Always emit the field, including a zero value. |

**Use the `_force` variant for any field where 0 is a legitimate,
distinguishable value.** Categories that always need force-encoding:

- **Mode enums** where `0` = OFF / Standby and the receiver must
  distinguish "explicit OFF" from "unchanged" (Burner, FanBoost, CO2
  Purge, Refrig).
- **Indexes / channels** where `0` is a real slot (`aux_index=0`,
  `pwm_idx=0`, `humid_index=0`, etc.).
- **Threshold / cycle counts** where `0` means "disabled"
  (`PurgeThreshold=0` → no fan-speed gate; `RefrigerationPurge=0` →
  purge disabled).
- **Every slot of a fixed-length repeated array** (e.g. all 48
  Climacell half-hour slots) — without force-encoding, a slot of value
  0 is dropped and the partial new schedule overlays whatever was there
  before.
- **Cross-coupled settings** — fields stored under one struct but
  written via a different page's save path (e.g.
  `Settings.Co2.Purge.RefrigThresh` is owned by `apply_refrig`, encoded
  inside `RefrigSettings`).

When in doubt: force it. The cost is one byte per field per save; the
cost of the bug is hours of "why didn't my save stick".

---

## 2. Mode-dependent encoders mirror legacy `StoreXxx()` exactly

The legacy AS2 `StoreBurner` / `StoreFanBoost` / `StoreCO2` handlers
parse a positional CSV whose **layout depends on Mode**. Mode-aware
encoders in `index.ts::legacyShim.sendPost` must mirror that exactly:

- **OFF (mode 0):** emit only `Mode` (force-encoded) and any
  always-present trailing fields (e.g. Burner's altitude / altUnit at
  fields 9 / 10).
- **Mode-1 (Manual / Temp):** emit Mode + the manual subset only.
- **Mode-2/3 (Economy / Max / Auto):** emit Mode + the full auto subset.

**Do NOT send a fixed-position superset.** The legacy `apply_*()`
decoder treats *missing* fields as "leave alone" and *present* fields as
"overwrite" — so an over-broad encoder that always emits all fields
overwrites mode-specific state with whatever happened to be in the
unused UI inputs.

---

## 3. Repeated-submessage decoders MUST clear before applying

`apply_pwm_settings`, `apply_aux_program` (per-aux rule list),
`apply_climacell_times`, `apply_refrig` (stages / defrosts) all wipe the
destination slice up front so a save is a *replacement*, not an
*overlay*.

**Counter variables in those decoders MUST be function-local** — never
`static`. A `static` counter survives across saves and silently lands
the next save's first entry in the slot after the previous save's last
entry.

```c
/* WRONG — static counter persists across calls */
static int rule_idx = 0;
/* RIGHT — local counter resets on every save */
int rule_idx = 0;
```

---

## 4. Bridge handlers layer on top of `apiRoutes.pageSaveMap`

Each `legacyShim.sendPost` handler consumes the **CSV / form-field shape
that `pageSaveMap.<page>.extract()` produces**, not the raw shape the UI
POSTs. The data flow is:

```
UI POST ──► apiRoutes.pageSaveMap.<page>.extract(body) ──► CSV string
        ──► legacyShim.sendPost(armTag, csv) ──► protobuf encode
        ──► nova_dataexc.c::apply_<page>() ──► Settings struct
```

**Trace end-to-end before changing wire layout in either direction.**
Modifying just the `apply_*` decoder without aligning the bridge
handler (or vice versa) creates silent corruption.

---

## 5. Channel vs equipment index — always label which one

| Name | Meaning | Range |
|---|---|---|
| `eqIdx` | Index into `Settings.PWM[]` / `Settings.AuxProgram[]` | `0..PWM_TOTAL_EQ-1` / `0..NUM_AUX_OUTPUTS-1` |
| `hwChan` | Physical PWM peripheral / output port | hardware-defined |

Aux Program: bridge converts `eqIdx` → 0-based `auxIdx` by subtracting
`EQ_AUX1` (=25) before sending. Reject out-of-range values rather than
silently truncating.

PWM was a silent no-op for months because the bridge sent
`idx=hwChannel, channel=eqIdx`, and the firmware silently ignored
out-of-range `eqIdx`s. Always label which space you're in.

---

## 6. TX serialization in firmware

All TX bytes must go through `NovaProto_SendRaw` (in
`nova_protocol.c`), which wraps the COBS encode + UART transmit in a
lazily-created mutex (`xSemaphoreCreateMutex`). Without it, two tasks
racing to push frames produce interleaved COBS framing and the receiver
sees a flood of CRC errors (~73 % loss observed before the fix).

**Verification:** `GET http://localhost:9001/health` exposes `txFrames`,
`rxFrames`, `rxCrcErrors`, `rxCobsErrors`, `rxOverflows`. After a clean
restart and any save campaign, **`rxCrcErrors` and `rxCobsErrors` must
remain 0**. Any non-zero count means a TX path bypassed the mutex.

---

## 7. Force-encoded reads matter too

When the firmware encodes a setting *back* to the bridge (e.g.
`NovaMsg_SendRefrigSettings`), it must use the `_force` variants for
the same fields the save path force-encodes. Otherwise the UI sees
stale defaults after restart, even though the saved value is correct in
NV storage.

---

## 8. Build / restart loop

```powershell
# Firmware build (WSL)
wsl bash -c "cd /mnt/f/Constellation/Nova_Firmware && make 2>&1 | grep -E 'error:|nova_firmware.bin' | tail -5"

# Restart the whole stack
powershell -ExecutionPolicy Bypass -File .\Start-Constellation.ps1 -Restart
# Sleep ≥16 s before health-polling — the bridge needs time to reattach
Start-Sleep 18
Invoke-RestMethod -Uri http://localhost:9001/health | ConvertTo-Json -Depth 4
```

The trailing `/bin/sh: 1: Syntax error: end of file unexpected (expecting "then")`
from `make` output is benign noise from a sub-shell, **not** a build
failure. A successful build prints `[BIN] build/nova_firmware.bin`.

---

## 9. Smoke-test pattern after a save-path change

For any modified save handler, run a 3-POST campaign that exercises:

1. A "normal" mode/value combination.
2. The **explicit-zero** case (mode 0, threshold 0, index 0 — whichever
   is relevant).
3. A second non-zero combination (different mode if applicable).

Expect every reply to be `{"ok":true,"reply":"true"}`, then check
`/health` for `rxCrcErrors:0`. Any CRC errors after a save campaign
mean a wire-layout mismatch between encoder and decoder.

```powershell
foreach ($body in $bodies) {
  Invoke-RestMethod -Uri http://localhost:9001/iot/PostSave.jsp `
    -Method POST -Body $body `
    -ContentType 'application/x-www-form-urlencoded' `
    -TimeoutSec 12 -UseBasicParsing
}
(Invoke-RestMethod -Uri http://localhost:9001/health).nova.stats
```

---

## 10. SystemStatus extension fields

| Field | Meaning | Source |
|---|---|---|
| `cure_state = 17` | Burner-cure substate (legacy `CureState` global, `CS_OFF=0`..`CS_HOLD_BURNER_MOD_DOOR=9`) | `nova_messages.c::NovaMsg_SendSystemStatus` |

Republished by `novaAdapter.ts` as the `CureState` legacy var for
downstream consumers.

---

## 11. Settings init / OSPI vault

Nova persists `Settings` exclusively via the OSPI ping-pong vault in
`nova_settings_store.c` (`NovaSettings_Init` / `NovaSettings_Load` /
`NovaSettings_Save`). The legacy AS2 bank/file probe in
`nova_settings.c` (`ReadSettings` / `SaveSettings` /
`SettingsRestoreFromLocalFlash`) targets `FLASH_SETTINGS_BANK_A/B` and
`FLASH_SETTINGS_SAVE_FILE` — addresses that live in the AT25 region and
are not part of the OSPI persistence map. **They are dead code on Nova.**

`Settings_Init()` therefore must do exactly one thing: call
`GetFactoryDefault()` to seed RAM `Settings`. The persisted blob is
overlaid by `NovaSettings_Load()` in `main.c` immediately after.
Keeping the legacy `do { ReadSettings… } while (++Retries < 3)` retry
in place produced a noisy 4×-repeated `BAD CRC … BOTH banks invalid`
boot block plus two stray `Bank B (seq 1/2)` writes to dead flash on
every cold boot. Fixed 2026-04-21.

---

## 12. Remote SoC reboot — `POST /iot/system/reboot`

The bridge can warm-reset the controller SoC over the wire — no JTAG,
no operator power-cycle. This is the **OTA Activate primitive**: once a
new image is staged in OSPI, this call makes it take effect.

### Wire path

```
POST /iot/system/reboot                       (apiRoutes.ts)
  → serialBridge.rebootSoc()                  (index.ts protoForwards)
  → novaBridge.sendRebootSoc()                (novaSerialBridge.ts)
  → sendSystemCmd(50)                          → envelope tag 82 SystemCmd
                                                  { cmd_type=CMD_REBOOT_SOC=50 }
  → firmware bridge_rx_callback case 50       (Nova_Firmware/lp_am2434/main.c)
  → ClockP_usleep(50_000)                      (UART FIFO drain — lets the
                                                 [RX] log line make it out)
  → Sciclient_pmDeviceReset(SystemP_WAIT_FOREVER)
                                                (does NOT return — DMSC tears
                                                 the SoC down, ROM re-runs,
                                                 SBL loads OSPI 0x80000)
```

The same `Sciclient_pmDeviceReset` is what `Flash-LP.ps1`'s auto-flasher
calls after a successful OSPI write — proven path on this silicon.
Bridge's `NovaSerialBridge` reconnects automatically; the full
re-handshake (DMSC banner → SBL → app → bridge `Connected — firmware
ready`) takes ~30 s end-to-end.

### Ack-vs-reset race — handled

The 50 ms drain is enough for the `[RX] CMD_REBOOT_SOC →` log to flush,
but NOT for the protobuf Ack to round-trip through the bridge's
`handleAck` before the serial port disconnects. `sendCommand`'s 3 s
timer fires; `apiRoutes.ts` catches the resulting `Command timeout
(seq=N, msg=82)` and **maps it to 200 OK** because the reset itself is
the success signal. Genuine transport errors (UART not open, COBS
encode failure) still bubble through as 502.

```ts
} catch (e: any) {
  const msg = String(e?.message ?? '');
  if (msg.startsWith('Command timeout')) {
    return res.json({ ok: true, note: 'reboot triggered (firmware reset before Ack landed)' });
  }
  res.status(502).json({ error: msg || 'reboot request failed' });
}
```

If "real" Ack-then-reset is ever wanted, the firmware would need to
queue the reset on a separate task and let `bridge_uart_task` flush the
Ack first. **Don't bother** — current behaviour is reliable and matches
`Flash-LP.ps1` semantics exactly.

### What the proto enum means

`CMD_REBOOT_SOC = 50` lives at the top of the System lifecycle range in
[`proto/agristar/alarms.proto`](../proto/agristar/alarms.proto)
(`SystemCmdType`). 50–59 is reserved for future lifecycle commands
(e.g. `CMD_SHUTDOWN`, `CMD_GOLDEN_BOOT`); 0–44 are the legacy AS2
actions. SystemCmd already has a generic dispatcher in
`bridge_rx_callback`, so adding new lifecycle commands is an enum
addition + one `case` — no new envelope field number.

### Operational use

- **Operator "Reboot Controller" button** — wire to `POST /iot/system/reboot`.
- **OTA Activate** — `orbitOtaPush.ts` calls `serialBridge.rebootSoc()`
  after the Bank B write CRC matches the manifest hash. This is the
  Phase 2 (staging-copy hack) and Phase 3 (true A/B) reset trigger in
  [`LP-AM2434-OTA-Update-Plan.md`](LP-AM2434-OTA-Update-Plan.md).
- **Azure-pushed unattended updates** — Azure → bridge command queue →
  same `rebootSoc()` call. The whole chain is now remote-able; no
  on-site operator required.

### Verification

```powershell
$rxBefore = (Invoke-RestMethod http://10.47.27.108:9001/health).nova.stats.rxFrames
Invoke-RestMethod -Method Post http://10.47.27.108:9001/iot/system/reboot -TimeoutSec 8
Start-Sleep 35
(Invoke-RestMethod http://10.47.27.108:9001/health).nova.connected   # → True
# COM4 UART will show uptime jump back to ~NN.Ns plus full
# [STATS:after-probes] / [ORBIT] OrbitClient_Init boot trace.
```

---

## 13. Engine shadow-mirror — every engine-read `Settings.*` group needs a `mirror_*`

The Nova equipment engine (`nova_controls.c` / `nova_states.c` /
`nova_modes.c` / `nova_failures.c` / `nova_pwm.c` / `nova_serialshift.c`)
does **not** read the proto-saved settings directly. It reads the legacy
`Settings.*` struct (`Platform/include/legacy/Settings.h`), which
`lp_engine_shim.c::lp_engine_tick()` repopulates **every tick** from
`LpSettingsData` (the proto-saved source) via a set of `mirror_*()`
functions.

**Invariant:** every `Settings.X` group the engine reads, and that is
operator-settable (carried in `LpSettingsData`), MUST have a
corresponding `mirror_X()` that copies **all** of its fields / array
slots / per-mode rows. A missing or **partial** mirror is **silent**:
the engine reads zero-initialised BSS, which for most fields is a
valid-looking default — `FM_NONE`, `HM_MANUAL`, `0%`, `RT_MANUAL`,
unassigned — so the firmware runs with the operator's config ignored and
nothing complains.

This bit hard on 2026-06-05: four groups had no mirror —
`humid_ctrl` (pump latched on instead of timer-cycling),
**`failure` (all 15 `FAIL_*` equipment failure detections silently
disabled — `FM_NONE`)**, `aoequip` (4-20 mA outputs gated off), and
`aux_program` (rule engine mis-evaluated). Same class, three different
severities. Full audit + root cause:
[`memories/repo/lp-engine-shadow-mirror-rule.md`](../memories/repo/lp-engine-shadow-mirror-rule.md).

**Guards:**
- **Static (CI):** [`scripts/Check-SettingsMirrorCoverage.ps1`](../scripts/Check-SettingsMirrorCoverage.ps1)
  diffs the `Settings.*` groups the engine reads against the ones any
  `mirror_*` writes, minus a documented allowlist of intentionally-
  unmirrored groups (engine-owned runtime accumulators, device-config-
  seeded, proto-schema gaps). Run it after touching the engine or the
  shim; it exits non-zero on an unaccounted-for gap.
- **Two adjacent gaps the static check can NOT catch** (both real today):
  1. **Partial mirrors** — a mirror that copies group `X` but misses a
     field / array slot / mode row. The coverage script works at
     group granularity; sub-field gaps need the round-trip check below.
  2. **Proto-schema gaps** — settings the engine reads that aren't in
     the proto at all yet (e.g. `Refrig.Defrost[].Diagnostic`,
     `Co2.Purge.RefrigThresh`). No mirror can exist until the schema
     carries them.

> **🚨 PRE-SHIP GATE (must be done before finalizing for customers).**
> The static coverage check (group-level) and code-read audits are
> necessary but not sufficient — they cannot prove *partial* mirrors or
> wrong-field mappings (e.g. the `HUMIDITY_CTRL.Output`-is-the-pump
> naming trap). Before the product ships, build a **settings round-trip
> verification harness**: for every operator-settable field, write it via
> the proto path, then read back the value the *engine* actually acts on,
> and assert equality. This is the only check that catches partial
> mirrors, inverted field maps, and unit/scale drift across the whole
> UI→firmware settings path. Tracked as a finalization blocker; do not
> declare the settings path "done" without it.

---

## 14. Hand-rolled repeated-submsg WRITES must match the firmware decoder shape — verify field numbers AND nesting

When a UI page builds raw proto bytes for a repeated-submessage settings write
(via `writeProtoRaw` + `buildForceVarintBytes`/`wrapAsLengthDelim`), the wire
shape MUST match the firmware decoder exactly — both the **nesting** and the
**field numbers**. ts-proto is NOT in this loop; nothing validates the bytes.

A long-lived example (fixed 2026-06-10): `AnalogConfigForm.saveAnalog` emitted a
FLAT `{ boardIdx@1 varint, sensor@6 }` while the firmware
(`lp_settings.c::LpSettings_ApplyAnalogBoard`) decodes a NESTED
`{ board@1:{ address@1, sensors@4:{ slot@1, label@2, offset@3, disabled@4,
type@5 } } }`. Field 1 was a bare varint where the decoder expected a submessage,
so the **entire write was silently skipped** — no per-sensor analog config ever
persisted. See [`memories/repo/analog-config-write-path-nested-2026-06-10.md`].

Compounding trap: the analog wire has **two incompatible numbering schemes** —
the firmware decode/emit use `settings.proto SensorConfig` numbering
(`slot@1,label@2,offset@3,disabled@4,type@5`), but the UI READ decodes the same
frame with `io.proto AnalogSensor` (`slot@1,type@2,label@3,offset@4,disabled@5,
value@6,…`). The WRITE must match the firmware (settings.proto); the read-back
display is mis-decoded until the two are reconciled. Rule of thumb: **the
firmware `apply_*` decoder is authoritative for writes; read it, don't trust the
.proto declaration or the io.proto twin.**

## 15. Live-alarm WARN_/AL_ enums are an append-only index, locked to `warningKeys.ts`

`Warning.code` on the wire indexes directly into
`constellation-ui/src/lib/business/warningKeys.ts`, which must mirror the firmware
`WARNING_ITEMS` enum (`Platform/include/legacy/Warnings.h`) **index-for-index**.
Add new codes by APPENDING (firmware enum end before `NUM_WARNINGS`, UI array end)
at the **same index in the same commit** — never reorder, never copy an AS2
positional value (the Nova override enum is a different length than AS2). Example:
`WARN_STATICPRESSUREHIGH` landed at index **97** on both sides (fw 0.A.234).
`FAILURES`/`EQUIP_ALARMS` arrays (`Settings.Failure[]`, `SystemAlarm[]`,
`AlarmTimer[]`) auto-resize off `NUM_*`, so appending is safe.

## 16. Raw diagnostic varints in SystemStatus must NOT collide with schema'd typed fields

`build_system_status_envelope` (`lp_am2434/main.c`) hand-emits SystemStatus. Some
slots are typed schema fields (decoded by ts-proto on the UI: `static_pressure`=24
float, `estop_active`=22, …); others are **raw `DIAG_EMIT_VARINT` diagnostics** the
probe tools read by hand. A diag varint and a typed field that share a field number
are a silent **last-wins collision** — proto3 keeps whichever occurs last in the
message body, and the other is dropped.

This bit us 2026-06-11: the new `static_pressure` (field 24, float, wire 5) was
emitted *before* a pre-existing `gate_bits` refrigeration diagnostic
`DIAG_EMIT_VARINT(0xC0,0x01,…)` (`0xC0 0x01` = varint 192 = `(24<<3)|0` = field 24,
wire 0). Decoders kept the diag varint (a constant `0x1058F` = 67087), and the live
static-pressure float never reached the UI. Fixed by relocating the gate/setpoint
diag varints to a **high reserved diagnostic block (fields 50/51/52)**, well above
the typed range.

**Rule:** keep raw SystemStatus diagnostics in a dedicated high range (≥50) that no
typed `system.proto` member will ever occupy. Before adding ANY typed SystemStatus
field, grep `main.c::build_system_status_envelope` for a `DIAG_EMIT_VARINT` already
parked on that number. `system.proto`'s comment block (fields 18-21,23) documents
which low numbers are diag — but the safe long-term home for raw diag is the high
block. (The same latent collision still exists on the OLD diag numbers 25/26 if
someone adds typed `system.proto` fields 25/26 — they were moved to 51/52 too.)

---

## See also

- [`Simulator-to-Production-Transition.md`](Simulator-to-Production-Transition.md)
  — what changes when moving to real Nova hardware.
- [`firmware-equipment-control.md`](firmware-equipment-control.md) —
  equipment state machine reference.
- [`history/save-path-postmortems.md`](history/save-path-postmortems.md)
  — resolved save-path bugs (RX ring overflow, autoSave nested-array,
  runtimes resync, navigation prompt). Read when something *similar*
  happens — the lessons there are the source for several invariants
  above.
- `Nova_Firmware/Platform/nova_dataexc.c` — top-of-file comment
  summarises encoder / decoder pairing.
- `constellation-ui/server/src/index.ts` — `legacyShim.sendPost` block
  header summarises the per-handler invariants.
