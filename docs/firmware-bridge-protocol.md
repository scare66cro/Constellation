# Nova Firmware ↔ Bridge Wire Protocol

Hard-won invariants for any code that crosses the COBS+CRC envelope between
Nova AM2434 firmware (`Nova_Firmware/Platform/`) and the TypeScript bridge
(`constellation-ui/server/src/`).

> **Why this matters:** Violations of these rules produce *silent* data loss.
> The wire ack still returns `{"ok":true,"reply":"true"}`, the firmware
> acknowledges the envelope, the UI reports success — but the setting either
> didn't move, moved to the wrong slot, or overlaid stale state. There is no
> error to chase.

---

## 1. proto3 zero-suppression — the #1 footgun

Both sides have two encoder families:

| Family | Behaviour |
|---|---|
| `pb_uint32` (firmware) / `pbVarint` (bridge) | Skip emission when `val == 0` (proto3 default semantics). |
| `pb_uint32_force` (firmware) / `pbVarintForce` (bridge) | Always emit the field, including a zero value. |

**Use the `_force` variant for any field where 0 is a legitimate, distinguishable value.** Categories that always need force-encoding:

- **Mode enums** where `0` = OFF/Standby and the receiver must distinguish "explicit OFF" from "unchanged" (Burner, FanBoost, CO2 Purge, Refrig).
- **Indexes / channels** where `0` is a real slot (`aux_index=0`, `pwm_idx=0`, `humid_index=0`, etc.).
- **Threshold / cycle counts** where `0` means "disabled" (`PurgeThreshold=0` → no fan-speed gate; `RefrigerationPurge=0` → purge disabled).
- **Every slot of a fixed-length repeated array** (e.g. all 48 Climacell half-hour slots) — without force-encoding, a slot of value 0 is dropped and the partial new schedule overlays whatever was there before.
- **Cross-coupled settings** — fields that are stored under one struct but written via a different page's save path (e.g. `Settings.Co2.Purge.RefrigThresh` is owned by `apply_refrig`, encoded inside `RefrigSettings`).

When in doubt: force it. The cost is one byte per field per save; the cost of the bug is hours of "why didn't my save stick".

---

## 2. Mode-dependent encoders mirror legacy `StoreXxx()` exactly

The legacy AS2 `StoreBurner` / `StoreFanBoost` / `StoreCO2` handlers parse a positional CSV whose **layout depends on Mode**. Mode-aware encoders in `index.ts::legacyShim.sendPost` must mirror that exactly:

- **OFF (mode 0):** emit only `Mode` (force-encoded) and any always-present trailing fields (e.g. Burner's altitude/altUnit at fields 9/10).
- **Mode-1 (Manual / Temp):** emit Mode + the manual subset only.
- **Mode-2/3 (Economy / Max / Auto):** emit Mode + the full auto subset.

**Do NOT send a fixed-position superset.** The legacy `apply_*()` decoder treats *missing* fields as "leave alone" and *present* fields as "overwrite" — so an over-broad encoder that always emits all fields will overwrite mode-specific state with whatever happened to be in the unused UI inputs.

---

## 3. Repeated-submessage decoders MUST clear before applying

`apply_pwm_settings`, `apply_aux_program` (per-aux rule list), `apply_climacell_times`, `apply_refrig` (stages/defrosts) all wipe the destination slice up front so a save is a *replacement*, not an *overlay*.

**Counter variables in those decoders MUST be function-local** — never `static`. A `static` counter survives across saves and silently lands the next save's first entry in the slot after the previous save's last entry. This bit Aux Program rules hard before the per-call counter fix.

```c
/* WRONG — static counter persists across calls */
static int rule_idx = 0;
/* RIGHT — local counter resets on every save */
int rule_idx = 0;
```

---

## 4. Bridge handlers layer on top of `apiRoutes.pageSaveMap`

Each `legacyShim.sendPost` handler consumes the **CSV / form-field shape that `pageSaveMap.<page>.extract()` produces**, not the raw shape the UI POSTs. The data flow is:

```
UI POST ──► apiRoutes.pageSaveMap.<page>.extract(body) ──► CSV string
        ──► legacyShim.sendPost(armTag, csv) ──► protobuf encode
        ──► nova_dataexc.c::apply_<page>() ──► Settings struct
```

**Trace end-to-end before changing wire layout in either direction.** Modifying just the `apply_*` decoder without aligning the bridge handler (or vice versa) creates silent corruption.

---

## 5. Channel vs. equipment index — always label which one

PWM was a silent no-op for months because the bridge sent `idx=hwChannel, channel=eqIdx`, and the firmware silently ignored out-of-range eqIdxs.

| Name | Meaning | Range |
|---|---|---|
| `eqIdx` | Index into `Settings.PWM[]` / `Settings.AuxProgram[]` | `0..PWM_TOTAL_EQ-1` / `0..NUM_AUX_OUTPUTS-1` |
| `hwChan` | Physical PWM peripheral / output port | hardware-defined |

Aux Program: bridge converts `eqIdx` → 0-based `auxIdx` by subtracting `EQ_AUX1` (=25) before sending. Reject out-of-range values rather than silently truncating.

---

## 6. TX serialization in firmware

All TX bytes must go through `NovaProto_SendRaw` (in `nova_protocol.c`), which wraps the COBS encode + UART transmit in a lazily-created mutex (`xSemaphoreCreateMutex`). Without it, two tasks racing to push frames produce interleaved COBS framing and the receiver sees a flood of CRC errors (~73% loss observed before the fix).

**Verification:** the `/health` endpoint (`GET http://localhost:9001/health`) exposes `txFrames`, `rxFrames`, `rxCrcErrors`, `rxCobsErrors`, `rxOverflows`. After a clean restart and any save campaign, **`rxCrcErrors` and `rxCobsErrors` must remain 0**. Any non-zero count means a TX path bypassed the mutex.

---

## 7. Force-encoded reads matter too

When the firmware encodes a setting *back* to the bridge (e.g. `NovaMsg_SendRefrigSettings`), it must use the `_force` variants for the same fields the save path force-encodes. Otherwise the UI sees stale defaults after restart, even though the saved value is correct in NV storage.

---

## 8. Build / restart loop

```powershell
# Firmware build (WSL)
wsl bash -c "cd /mnt/f/Constellation/Nova_Firmware && make 2>&1 | grep -E 'error:|nova_firmware.bin' | tail -5"

# Restart the whole stack
powershell -ExecutionPolicy Bypass -File .\Start-Constellation.ps1 -Restart
# Sleep ≥16s before health-polling — the bridge needs time to reattach
Start-Sleep 18
Invoke-RestMethod -Uri http://localhost:9001/health | ConvertTo-Json -Depth 4
```

The trailing `/bin/sh: 1: Syntax error: end of file unexpected (expecting "then")` from `make` output is benign noise from a sub-shell, **not** a build failure. A successful build prints `[BIN] build/nova_firmware.bin`.

---

## 9. Smoke-test pattern after a save-path change

For any modified save handler, run a 3-POST campaign that exercises:

1. A "normal" mode/value combination.
2. The **explicit-zero** case (mode 0, threshold 0, index 0 — whichever is relevant).
3. A second non-zero combination (different mode if applicable).

Expect every reply to be `{"ok":true,"reply":"true"}`, then check `/health` for `rxCrcErrors:0`. Any CRC errors after a save campaign mean a wire-layout mismatch between encoder and decoder.

Example skeleton (PowerShell):

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

## 10. Reference: SystemStatus extension fields

| Field | Meaning | Source |
|---|---|---|
| `cure_state = 17` | Burner-cure substate (legacy `CureState` global, `CS_OFF=0`..`CS_HOLD_BURNER_MOD_DOOR=9`) | `nova_messages.c::NovaMsg_SendSystemStatus` |

Republished by `novaAdapter.ts` as the `CureState` legacy var for downstream consumers.

---

## See also

- [`docs/Simulator-to-Production-Transition.md`](Simulator-to-Production-Transition.md) — what changes when moving to real Nova hardware.
- [`docs/firmware-equipment-control.md`](firmware-equipment-control.md) — equipment state machine reference.
- `Nova_Firmware/Platform/nova_dataexc.c` — top-of-file comment summarizes encoder/decoder pairing.
- `constellation-ui/server/src/index.ts` — `legacyShim.sendPost` block header summarizes the per-handler invariants.
