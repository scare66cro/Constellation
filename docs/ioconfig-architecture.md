# IO Config — End-to-End Architecture

The Level 2 **IO Config** page assigns equipment (Fan, Refrigeration,
Bay Lights, …) to physical ports (DO1…DO10, DI1…DI10) on every
controller / orbit board in the system. It is one of the most
load-bearing pages in Constellation — almost every other equipment-
control feature reads through this map. This doc is the single
orientation point for everyone (and every Copilot session) touching
that path.

> **Read order:** start at *§1 Mental Model*, then jump to whichever
> tier you're editing. Each tier links to the canonical source files;
> code is the authority, this doc is the map.

---

## 1. Mental model

### 1.1 Two coordinate systems

The same assignment is represented two different ways depending on
which side of the wire you're on:

| View | Index by | Value | Used by |
|---|---|---|---|
| **Eq-indexed map** | equipment slot (0…60) | `port id` | firmware `s_io_config.input_map[]` / `output_map[]`; UI `ioConfig.config.outputConfig[port]` is the inverse view of this |
| **Port-indexed view** | port id (1…N) | equipment label string | what the operator sees on the IO Config page; built UI-side by inverting the eq-indexed map |

**Port id formula** (the *only* place this conversion lives):

```
PID = board * 12 + pin       // board 0-based, pin 0-based
                             // pin 0 reserved → real ports start at PID 1
```

- Board 0 = MAIN controller board (10 DO + 10 DI + DI11 = E-Stop)
- Boards 1+ = additional orbit boards (Storage, GDC, Triton)
- Legacy was 3 boards (PIDs 0…35); multi-orbit allows up to 16 boards
  (PIDs 0…191)
- The two arrays' length implicitly encodes the board count

### 1.2 Equipment slot table

The eq-indexed map's slot numbers reference `s_io_defaults[]` in
[Nova_Firmware/lp_am2434/lp_settings.c](../Nova_Firmware/lp_am2434/lp_settings.c)
(currently 0…60). Each slot is one row of:

```c
typedef struct { uint8_t mode; uint8_t io_type; uint8_t renamable; } io_default;
```

The same slot order is mirrored UI-side in
[constellation-ui/src/lib/business/equipmentMeta.ts](../constellation-ui/src/lib/business/equipmentMeta.ts)
(`EQ_LABEL_KEYS[]`), which holds the i18n key for each slot. Slots
43-58 are SW_* "switch" entries — kept wire-compatible with legacy
AS2; do **not** renumber them.

**Append-only invariant.** Adding a new equipment slot means:
1. Append to `s_io_defaults[]` at the next free index (never insert)
2. Append the same i18n key to `EQ_LABEL_KEYS` at the matching index
3. Add the localized label to `en.json` + `zh.json` under `equipment.*`
4. Bump firmware alpha + add a changelog row

Renumbering an existing slot silently re-maps every saved IoConfig
blob in every customer's OSPI bank. Don't.

### 1.3 Hardware-pinned slots

Two slots are wired to soldered-down pins on every controller board
and are not operator-assignable:

| Slot | Equipment | Pin | Enforced where |
|---|---|---|---|
| **59** | `EQ_AUX_LOW_LIMIT` | DI1 (port 1) | `io_config_pin_hardware_inputs()` in lp_settings.c |
| **60** | `EQ_ESTOP` | DI11 (port 11) | same; UI lock in InputCell.svelte |

The pin function runs at **two** points so a stale OSPI blob can't
repurpose either pin:

1. End of `LpSettings_DataInit()` — cold-boot path
2. Tail of `LpSettings_Deserialize()` — after every replay from OSPI

Both slots are marked `M_NONE / IO_NONE` in `s_io_defaults` so they
never appear in any operator dropdown. The InputCell renders DI1 +
DI11 as locked rows with a `*` suffix.

The legacy `EQ_LOW_TEMP` (slot 37) is **deprecated** — superseded by
hardware-pinned `EQ_AUX_LOW_LIMIT` on DI1. The slot remains in
`s_io_defaults` only for wire compatibility with old saves; its label
key is blanked, the entry is `M_NONE / IO_NONE`, and
`io_config_pin_hardware_inputs()` force-clears its map entry on every
boot.

### 1.4 Soft default (cold boot only)

`LpSettings_DataInit()` seeds `input_map[33] = 2`, i.e. **DI2 = EQ_POWER**
on a virgin OSPI. The operator can reassign DI2 on the IO Config page
and the next deserialize will replay over that default.

This is the *only* IO Config soft-default we currently seed. Q2.9 in
[docs/factory-defaults-review-2026-05.md](factory-defaults-review-2026-05.md)
proposes a fuller AS2-style default set; deferred until OSPI A/B
ping-pong with CRC is fully validated (today's banks are A/B but a
single corrupt CRC could still wipe the operator's commissioning work).

---

## 2. Wire layout

### 2.1 The protobuf message

Defined in [proto/agristar/io.proto](../proto/agristar/io.proto):

```proto
message IoConfig {
  repeated uint32 output_map = 1;   // eq-indexed, value = portId
  repeated uint32 input_map  = 2;   // eq-indexed, value = portId
  uint32 board_count         = 3;   // 0 = legacy 3-board default
}
```

It travels two channels:

| Direction | Container | Field/tag |
|---|---|---|
| Firmware → UI broadcast | top-level envelope | tag **24** (`IoConfig`) |
| UI → Firmware save | `SettingsUpdate` submsg | field **39** (`io_config`) inside envelope tag 23 |

> **Common gotcha.** The save path uses field **39** of `SettingsUpdate`,
> not 36 (which is `sys_mode`). A stale comment in
> `level2/ioconfig/+page.svelte` previously claimed "field 36" — fixed.
> See [proto/agristar/settings.proto](../proto/agristar/settings.proto) line 57.

### 2.2 Two encodings on the wire

The same `IoConfig` message is encoded two different ways depending
on direction:

#### A. **Dense / eq-indexed** — firmware → UI broadcast

Used by `LpSettings_BuildIoConfigBody()` and `novaDataStore.decodeIoConfig`.
Both maps are emitted as **packed varint** repeated arrays:

```
field 1, wire 2 (LEN) → [v0, v1, v2, …]   // output_map[eq] = port
field 2, wire 2 (LEN) → [v0, v1, v2, …]   // input_map[eq]  = port
field 3, wire 0 (varint) → board_count    // optional
```

`novaDataStore.decodeIoConfig` accepts **both** wire=2 (packed) and
wire=0 (unpacked-repeated) for forward compatibility. Always emit
packed; accept both.

#### B. **Sparse / indexed-submsg** — UI → firmware save

A 60-slot dense encoding overflows the 1500-byte settings envelope
budget once you start adding boards. The Level 2 IO Config save uses
a sparse encoding instead, disambiguated by **wire type** so it
doesn't conflict with `field 3 wire=0 board_count`:

```
field 3, wire 2 (LEN) → submsg { slot(1)=eqIdx, value(2)=portId }   // OUTPUT
field 4, wire 2 (LEN) → submsg { slot(1)=eqIdx, value(2)=portId }   // INPUT
```

Both inner fields MUST be **force-varint** because slot=0 (`EQ_FAN`)
and value=0 (port DO0) are both valid and proto3 zero-suppression
would drop them otherwise. The UI uses `buildForceVarintBytes()` from
[constellation-ui/src/lib/business/protoWrite.ts](../constellation-ui/src/lib/business/protoWrite.ts).

**Wipe-and-repopulate semantics.** When firmware sees the FIRST
indexed sub-message of a given direction, it `memset`s that whole map
to 0 before applying the new entries. So the UI only needs to send
the slots that are actually assigned — `'-1'` / unassigned slots are
just omitted.

The implementation lives in
`LpSettings_ApplyIoConfig` at the bottom of [Nova_Firmware/lp_am2434/lp_settings.c](../Nova_Firmware/lp_am2434/lp_settings.c)
(search for `out_indexed_started`).

### 2.3 IoEntry — labels, mode, type, renamable

`IoConfig` is the *assignment*. The *catalog* — equipment label, what
system mode (potato/onion/all/etc.) it applies to, whether it's an
input/output/both/switch, and whether the operator can rename it —
lives in a separate message:

```proto
message IoDefinition { repeated IoEntry entries = 1; }
message IoEntry {
  uint32 index    = 1;   // matches IoConfig slot index
  string name     = 2;   // user-overridden display name (blank → use i18n key)
  uint32 mode     = 3;   // SYSTEM_MODE category
  uint32 io_pin   = 4;   // legacy holdover, mostly unused
  bool   renamable= 5;   // pencil icon shown in InputCell/OutputCell
  bool   visible  = 6;
  uint32 io_type  = 7;   // 0=OUTPUT, 1=INPUT, 2=BOTH, 3=SWITCH, 4=NONE
                         //   force-encoded — 0 is meaningful
}
```

UI side, the dropdown content is built in
`buildEquipmentList()` inside the IO Config page:
- iterate `ioConfig.ioNames[]` (the IoDefinition entries)
- skip `entry.ioType === SWITCH` (switches are a separate UX)
- skip when `!entryAppliesToSystem(entry.mode, sys)`
- route Bay Lights (slots 23, 24) into the secondary `lights` list
- route everything else into `outputList` and/or `inputList` based on
  `io_type`
- sort indicator lights (Red, Yellow, Fan/Green) to the top of the
  output list

The label resolution helper is `resolveEquipmentLabel(entry, $t)` in
[constellation-ui/src/lib/business/equipmentMeta.ts](../constellation-ui/src/lib/business/equipmentMeta.ts)
— user-set `name` wins; otherwise the localized `EQ_LABEL_KEYS[index]`;
otherwise `IO N` as last-resort.

### 2.4 Renaming a slot

The Level 2 IO Config page sends an `IoNameUpdate` (envelope `TAG.IoNameUpdate`)
when the operator clicks the pencil and edits a name. Firmware splices
it into `s_io_definition.entries[index].name`. This persists in OSPI
in the `IoDefinition` block, *not* in `IoConfig`. The two messages
travel independently.

---

## 3. Persistence

| Tier | Where it lives | Notes |
|---|---|---|
| Live RAM | `s_data.io_config` in lp_settings.c | What `_Build*()` reads when re-emitting |
| Save blob | `LpSettings_Serialize()` field 24 (IoConfig) + field 75 (IoDefinition) | Single OSPI blob, ping-pong A/B |
| OSPI banks | LP-AM2434 `0x600000` device-config region | A/B with CRC validation |
| Bridge cache | `dataCache` (transparent) | No interpretation; just relays the proto |
| UI store | `ioConfigStore` typed proto store | Receives full snapshots on every broadcast |

The bridge holds **no** IoConfig state of its own — it is a
transparent passthrough. Earlier architectures had a CSV-shim (`IoConfigOutData` / `IoConfigInData`
varNames in `dataCache.cgiTable`); those are vestigial and being
removed under the proto-direct migration.

---

## 4. Common pitfalls (history of pain)

1. **Wrong SettingsUpdate field.** IoConfig save = field **39**, not 36
   (sys_mode is 36, accounts is 38). Easy to miss because the broadcast
   uses envelope tag 24 directly without going through SettingsUpdate.

2. **Decoder only accepts one wire type.** Until 0.A.9 the bridge
   `decodeIoConfig` only handled wire=0 (unpacked repeated) but firmware
   emits wire=2 (packed). Result: every save round-tripped to "all None"
   in the UI. Fix: always accept both wire types on every map field.

3. **Indexed submsg vs board_count collision.** Field 3 has two
   meanings disambiguated only by wire type. If you add a fourth use,
   you must keep wire-type uniqueness or break every save.

4. **Force-varint for slot 0 / value 0.** Without `buildForceVarintBytes`
   the UI silently drops `{slot:0, value:0}` (i.e. EQ_FAN → DO0) because
   proto3 zero-suppresses both fields. Same trap exists firmware-side
   in every encoder that handles slot/index 0.

5. **Hardware pin not enforced after deserialize.** `io_config_pin_hardware_inputs()`
   MUST be called from both `_DataInit` AND tail of `_Deserialize`;
   missing the second call lets a corrupted blob silently repurpose
   DI1 or DI11.

6. **EQ_LOW_TEMP zombie.** Slot 37 is deprecated; its `EQ_LABEL_KEYS`
   entry is blank, its locale entries removed, `s_io_defaults[37]`
   forced to `M_NONE/IO_NONE`. Anything that imports a "low temp"
   constant should use `EQ_AUX_LOW_LIMIT` (slot 59) or the per-aux
   alarm fields instead.

7. **EQ_AUX_LOW_LIMIT is hardware-only too.** It's `M_NONE/IO_NONE`
   in `s_io_defaults` so the operator can't accidentally re-assign DI1
   from the dropdown. The InputCell lock (`i===0 && (j===1 || j===11)`)
   guarantees the rendered cell is non-editable.

8. **Renaming an IoEntry doesn't change IoConfig.** They're independent
   messages with independent OSPI blocks. A fresh `IoConfig` save will
   not reset `IoEntry.name` and vice versa.

9. **Stale page comment after a wire-format change.** The IoConfig
   save comment in `+page.svelte` referenced "settings field 36" for
   weeks after the actual field moved to 39. **Always update the
   doc-comment when you change the wire**; lying comments cost more
   debug time than no comment.

---

## 5. File index

| Concern | File |
|---|---|
| Proto schema | [proto/agristar/io.proto](../proto/agristar/io.proto), [proto/agristar/settings.proto](../proto/agristar/settings.proto) |
| Firmware apply/build | [Nova_Firmware/lp_am2434/lp_settings.c](../Nova_Firmware/lp_am2434/lp_settings.c) — search `LpSettings_ApplyIoConfig`, `LpSettings_BuildIoConfigBody`, `io_config_pin_hardware_inputs`, `s_io_defaults`, `io_definition_seed_defaults` |
| Firmware label seed (legacy compat) | `Nova_Firmware/lp_am2434/lp_engine_shim.c` (previously `Platform/nova_thread_overrides.c`, deleted in 2026-05 QEMU cleanup) — search `EquipIo` defaults table |
| Bridge decode | [constellation-ui/server/src/novaDataStore.ts](../constellation-ui/server/src/novaDataStore.ts) — `decodeIoConfig` |
| UI page | [constellation-ui/src/routes/level2/ioconfig/+page.svelte](../constellation-ui/src/routes/level2/ioconfig/+page.svelte) — `saveIoConfig`, `buildEquipmentList`, `setupIoConfig` |
| UI cells | [constellation-ui/src/lib/components/InputCell.svelte](../constellation-ui/src/lib/components/InputCell.svelte), [constellation-ui/src/lib/components/OutputCell.svelte](../constellation-ui/src/lib/components/OutputCell.svelte) |
| Equipment metadata | [constellation-ui/src/lib/business/equipmentMeta.ts](../constellation-ui/src/lib/business/equipmentMeta.ts) — `EQ_LABEL_KEYS`, `entryAppliesToSystem`, `IoOptionEnum`, `SystemModeEnum`, `resolveEquipmentLabel` |
| Locale labels | [constellation-ui/src/lib/locales/en.json](../constellation-ui/src/lib/locales/en.json), [constellation-ui/src/lib/locales/zh.json](../constellation-ui/src/lib/locales/zh.json) — `equipment.*` keys |
| Save-write helper | [constellation-ui/src/lib/business/protoWrite.ts](../constellation-ui/src/lib/business/protoWrite.ts) — `writeProtoRaw`, `buildForceVarintBytes`, `wrapAsLengthDelim` |
| Factory-defaults plan | [docs/factory-defaults-review-2026-05.md](factory-defaults-review-2026-05.md) §Q2.9 |

---

## 6. When you change something here, also…

- Bump `docs/firmware-version-current.md` if the wire or default
  changed. Add a changelog row that says **what** moved on the wire,
  not just **that** it moved.
- Re-run a save→reload smoke test against both LPs. The "stays then
  reverts" symptom almost always means the bridge or decoder didn't
  catch up to a wire-format change.
- If you add or rename an equipment slot, update **all six** places in
  §1.2's append-only checklist before flashing.
