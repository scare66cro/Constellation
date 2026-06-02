# Orbit-side VFD Poll Scheduler — contract for the STORAGE orbit's RS485 RTU master

> **Status:** wire contract landed 2026-06-02 (bridge + Nova + proto).
> Orbit-side implementation pending hardware bring-up.
> This document is the spec the orbit firmware author writes against.

## TL;DR

The STORAGE orbit board is the **only** thing that talks Modbus RTU to
VFDs / contactors / other Modbus-RTU devices. Bridge ⇄ Nova ⇄ orbit is
all Modbus TCP (with the bridge↔Nova hop riding the production UART
airgap). Vendor knowledge — register maps, FC dispatch, scaling,
action sequences — lives **bridge-side** in
[`vfdRegisterMaps.ts`](../constellation-ui/server/src/vfdRegisterMaps.ts).

The orbit is a **thin configurable scheduler + write router**. It
receives a per-entry config table from the bridge at boot, polls each
entry at the requested cadence, stashes results in its 48-reg HR cache
window (HR 100..147), and routes bridge HR writes back out over RS485
according to each cache slot's per-entry FC + native address.

**Adding a new VFD vendor or a Modbus-driven contactor is a
TypeScript-only change.** The orbit binary never grows. This is the
property that lets us support unlimited vendors on the AM2434's
constrained MSRAM budget.

## Wire flow

```
                                                  RS485
                                                   ↓↑
   Bridge  ─UART/COBS─→  Nova  ─Modbus TCP─→  STORAGE orbit  ─Modbus RTU─→  VFD 1 (unit 1)
                                                   │                       ↓↑
                                                   │                       VFD 2 (unit 2)
                                                   │                       ↓↑
                                                   │                       Contactor (unit 3)
                                                   ↓
                                       cycle 1Hz: poll all entries,
                                       stash into HR 100..147 cache.
                                       Publish via existing
                                       OrbitSensorBank @ hr_base=100.

   Bridge composes a `VfdPollConfig` envelope (tag 127) at startup
   from each drive's profile in `vfdRegisterMaps.ts`. Nova receives,
   forwards to the orbit (see "Nova → orbit forwarding" below).
```

## Existing wire pieces (don't change)

These are already live; the orbit just needs to plug into them:

- **`OrbitSensorBank` (envelope tag 124)** — orbit emits one bank per
  100ms tick, round-robin across role windows. For STORAGE that's
  `(slot, hr_base=200, 64 regs sensor block) + (slot, hr_base=100, 48 regs
  VFD cache)`. Phase 4b Sub-3 (real) landed 0.A.220 — the orbit's HR
  100..147 region is what feeds drive data to the bridge.
- **`OrbitRegWrite` (envelope tag 126)** — bridge writes HR values to
  the orbit; orbit's TCP slave caches them. For VFD-cache slots
  (HR 100..147), the orbit's RTU master needs to **forward the write
  out over RS485** per the slot's configured `nativeAddr` + `fc`.

## New wire piece: `VfdPollConfig` (envelope tag 127)

```proto
message VfdPollConfig {
  uint32 slot                  = 1;  // STORAGE orbit slot (0..15)
  repeated VfdPollEntry entries = 2; // ≤ 48 (one per HR 100..147 slot)
}

message VfdPollEntry {
  uint32 cache_slot   = 1;  // 0..47 — index into the orbit's HR 100..147 window
  uint32 unit_id      = 2;  // Modbus RTU unit ID (1..247)
  uint32 native_addr  = 3;  // Modbus-standard register address (1-based per vendor)
  uint32 fc           = 4;  // low byte = FC, high byte = sub-FC (see table below)
  uint32 poll_rate_ms = 5;  // 0 = on-demand only (write target); else periodic
  uint32 writable     = 6;  // 1 = bridge writes to this cache_slot forward to RS485
}
```

The bridge replays this on every Nova reconnect, so the orbit doesn't
need to OSPI-persist the table (matches existing patterns for
`OrbitRoleAssign`, `AoEquipAssign`). On startup the orbit's table
should be empty; the entries-list arrives within a few seconds of the
bridge handshake.

### FC encoding

Low byte is the primary Modbus function code; high byte is the
sub-function (for vendor-custom function codes that have a sub-FC).

| `fc` value | Meaning | RS485 payload |
|---|---|---|
| `0x01` | Read Coils | standard FC1 request |
| `0x02` | Read Discrete Inputs | standard FC2 |
| `0x03` | Read Holding Registers | standard FC3 |
| `0x04` | Read Input Registers | standard FC4 |
| `0x05` | Write Single Coil | standard FC5 (used on writes) |
| `0x06` | Write Single Holding Register | standard FC6 (used on writes, single value) |
| `0x0F` | Write Multiple Coils | standard FC15 |
| `0x10` | Write Multiple HRs | standard FC16 (used on writes, multi value) |
| `0x4201` | Phase Tech FC 66 sub 1 (Set HOA Mode) | FC 66 / 0x01 — data byte is values[0] low byte |
| `0x4202` | Phase Tech FC 66 sub 2 (Set Run State) | FC 66 / 0x02 — data byte is values[0] low byte |
| `0x4203` | Phase Tech FC 66 sub 3 (Reset System Status) | FC 66 / 0x03 — data byte is values[0] low byte (must be `0xA5`) |

For **read** entries the orbit uses the FC verbatim. For **write**
entries (`writable = 1`), the orbit picks the appropriate write FC
based on the value count and the entry's read FC:

- `fc=0x01/0x02` + writable + 1 value → FC5 (Write Single Coil)
- `fc=0x01/0x02` + writable + N values → FC15 (Write Multi Coils)
- `fc=0x03/0x04` + writable + 1 value → FC6 (Write Single HR)
- `fc=0x03/0x04` + writable + N values → FC16 (Write Multi HRs)
- `fc=0x42XX` → always FC 66 sub XX (single byte payload)

## Cache layout (HR 100..147)

48 slots, partitioned as 3 drives × 16 slots each:

| Cache slot range | Belongs to | Drive's profile slot |
|---|---|---|
| 100..115 | Drive 1 (cache offset 0) | Profile slot 0..15 |
| 116..131 | Drive 2 (cache offset 16) | Profile slot 0..15 |
| 132..147 | Drive 3 (cache offset 32) | Profile slot 0..15 |

The bridge handles the per-drive offset translation: when it composes
`VfdPollConfig`, it maps each profile's "slot 4" (e.g. output
frequency) to orbit cache slot `(drive_idx) × 16 + 4`. The orbit just
sees a flat 48-entry table.

## Orbit responsibilities

1. **Receive `VfdPollConfig`** — through whatever mechanism Nova
   forwards it. Cache the entries in RAM. ≤ 48 entries × ~16 B = ~768 B
   RAM total.

2. **For each non-zero `poll_rate_ms` entry:** schedule a periodic
   Modbus RTU read at `unit_id` / `native_addr` / `fc`. On success,
   pack the response into the corresponding cache slot
   (HR 100 + `cache_slot`). For multi-byte responses (coil packs, etc.),
   pack to a single u16: bit 0 = the requested register's value, bits
   1..15 = 0.

3. **For each `poll_rate_ms == 0` entry:** never poll. Only forward
   bridge writes (these are write-only commands like Phase Tech FC 66).

4. **On bridge HR write to a `cache_slot` in 100..147:**
   - Look up the entry for that cache slot.
   - If `writable == 0`, ignore (or stash for next read like today).
   - If `writable == 1`, translate the values + entry's FC into the
     appropriate RTU write and emit on RS485. **Stash the value in
     the cache slot so the bridge's read-back-via-OrbitSensorBank
     reflects what was just commanded** (until the next periodic
     read overwrites).

5. **Optional housekeeping**: track per-entry RTU error counts;
   surface in a future diagnostics window.

## Worked examples

### Phase Tech LH AquaPhase pump, unit_id = 1

Bridge composes (from
[`vfdRegisterMaps.ts::PHASE_TECH_LHAP`](../constellation-ui/server/src/vfdRegisterMaps.ts)):

| cache_slot | native_addr | fc | poll_rate_ms | writable | Bridge intent |
|---:|---:|---:|---:|---:|---|
| 0 | 10001 | `0x02` Read DI | 500 | 0 | Read HOA Run-Stop bit |
| 2 | 30001 | `0x04` Read IR | 1000 | 0 | Current HOA mode |
| 5 | 30030 | `0x04` Read IR | 1000 | 0 | System Status (fault) |
| 6 | 30013 | `0x04` Read IR | 200 | 0 | Output Frequency ×10 |
| 7 | 30002 | `0x04` Read IR | 500 | 0 | I_u ×100 |
| 8 | 30006 | `0x04` Read IR | 500 | 0 | Output kW ×10 |
| 9 | 30009 | `0x04` Read IR | 1000 | 0 | DC bus voltage |
| 10 | 30010 | `0x04` Read IR | 1000 | 0 | Input voltage |
| 11 | 30017 | `0x04` Read IR | 5000 | 0 | IGBT temp ×10 |
| 12 | 40002 | `0x03` Read HR | 10000 | 1 | Min frequency ×10 |
| 13 | 40003 | `0x03` Read HR | 10000 | 1 | Max frequency ×10 |
| 14 | 40004 | `0x03` Read HR | 10000 | 1 | Start-Up Ramp ×10 |
| 15 | 40074 | `0x03` Read HR | 5000 | 1 | PSI Setpoint ×10 |
| 1 | (unused) | `0x4201` FC66/1 | 0 | 1 | Set HOA Mode (write-only) |
| 3 | (unused) | `0x4202` FC66/2 | 0 | 1 | Set Run State (write-only) |
| 4 | (unused) | `0x4203` FC66/3 | 0 | 1 | Reset System Status (write-only) |

For unit_id=1, cache slots map to orbit HR `100 + slot` (drive offset 0).

Operator presses START in the UI → bridge issues two `OrbitRegWrite`s
in sequence:

1. `OrbitRegWrite(STORAGE_slot, addr=101, values=[0x0F])` →
   orbit looks up cache_slot=1 entry → FC `0x4201` (Phase Tech FC 66 sub 1) →
   sends RS485: `[unit=1, fc=66, sub=1, data=0x0F, CRC]` → drive enters Manual mode.
2. After 50ms delay (encoded in bridge-side `delayAfterMs`):
   `OrbitRegWrite(STORAGE_slot, addr=103, values=[0x0F])` →
   orbit looks up cache_slot=3 entry → FC `0x4202` (FC 66 sub 2) →
   sends RS485: `[unit=1, fc=66, sub=2, data=0x0F, CRC]` → drive starts.

### Modbus contactor, unit_id = 3

Bridge composes (from `MODBUS_CONTACTOR` profile):

| cache_slot | native_addr | fc | poll_rate_ms | writable | Bridge intent |
|---:|---:|---:|---:|---:|---|
| 0 | 1 | `0x01` Read Coil | 500 | 0 | Current state |
| 1 | 1 | `0x05` Write Single Coil | 0 | 1 | Command |

For unit_id=3, cache slots 0+1 map to orbit HR 132+133 (drive offset 32).

Operator clicks "Energize" → bridge issues
`OrbitRegWrite(STORAGE_slot, addr=133, values=[0xFF00])` →
orbit looks up cache_slot=33 entry → FC `0x05` → sends RS485:
`[unit=3, fc=5, addr=1, value=0xFF00, CRC]` → contactor coil energizes.

### ABB ACS310 fan, unit_id = 2

All entries use FC `0x03` (Read HR) since the FENA-01 layout exposes
process data + all parameters as native holding registers.

| cache_slot | native_addr | fc | poll_rate_ms | writable | Bridge intent |
|---:|---:|---:|---:|---:|---|
| 0 | 0 | `0x03` Read HR | 200 | 1 | ControlWord |
| 1 | 1 | `0x03` Read HR | 200 | 1 | SpeedRef ×100 |
| 2 | 2 | `0x03` Read HR | 200 | 0 | StatusWord |
| 3 | 3 | `0x03` Read HR | 200 | 0 | ActualSpeed ×100 |
| 4 | 100 | `0x03` Read HR | 500 | 0 | 01.01 Output Freq |
| ... | ... | ... | ... | ... | ... |

For unit_id=2, cache slots map to orbit HR 116..131 (drive offset 16).

Operator presses START with speedRefPercent=50 → bridge issues a single
`OrbitRegWrite(STORAGE_slot, addr=116, values=[0x047F, 5000])` →
orbit looks up cache_slot=16 entry → FC `0x03` + writable + 2 values →
FC16 (Write Multi HRs) → sends RS485:
`[unit=2, fc=16, addr=0, qty=2, bcnt=4, 0x047F, 5000, CRC]` → drive
starts at 50% speed.

## Nova → orbit forwarding (TBD)

How Nova actually delivers `VfdPollConfig` to the orbit is the one
remaining design choice. Two natural options:

### Option N1: Dedicated config region in the orbit's HR map
Nova translates `VfdPollConfig` into a series of FC16 writes to a
designated region (say HR 600..815, 48 entries × 4 regs each). The
orbit's TCP slave detects writes to this region and refreshes the
table on the next scheduler tick. **Pro:** no new orbit-side TCP
endpoint. **Con:** orbit needs to know the region's HR layout.

### Option N2: New orbit-side Modbus FC
Nova issues a custom Modbus TCP message (vendor FC 67?) carrying the
config blob verbatim. Orbit dispatches on FC.  **Pro:** typed wire.
**Con:** one more Modbus FC to maintain.

Recommend **N1** — keeps the orbit's TCP surface lean and reuses
the existing FC16 path. Layout:

```
HR 600..603  entry 0: (cache_slot | fc<<16, unit_id, native_addr, poll_rate_ms | writable<<31)
HR 604..607  entry 1: same shape
...
HR 600 + 4N..603 + 4N  entry N
```

Last entry marker: `cache_slot = 0xFFFF` in the first reg.

Nova's `VfdPollConfig` handler at field 127 (in `main.c`) iterates the
proto entries, packs into the 4-reg-per-entry layout, and issues an
FC16 to (orbit_slot, addr=600, values=[...]). This handler is currently
an ack-only stub; flesh it out when the orbit's config region is
implemented.

## What's NOT covered yet

- **Arbitrary parameter writes** (`/iot/fans/param` body
  `{ unitId, param: "rampUp", value }`). The current vendor-agnostic
  scheduler only writes to cache slots whose entries are
  pre-configured. For full per-param writes we either:
  - (a) Extend the schedule (operator preconfigures which params they
    want bridge-writable), or
  - (b) Add a separate "raw native write" envelope where the bridge
    sends `(unit_id, fc, native_addr, values)` directly. The orbit's
    write path handles it without any cache slot involvement.
- **RTU error reporting** — the bridge needs to know when an RS485
  read fails (drive offline, timeout, CRC error). Plausible path:
  per-entry error counter exposed in a new diagnostics HR region the
  bridge can poll via existing OrbitSensorBank.
- **Modbus RTU master concurrency** — the orbit handles one
  transaction at a time; the scheduler should rate-limit if the
  cumulative poll budget exceeds the RS485 budget.
- **Vendor-specific status decoding** — Phase Tech's FC 17 (Report
  Slave ID) returns run state + fault. We're not using FC 17; we
  read the equivalent from discrete inputs / input registers. Fine
  for now.

## References

- [`docs/phase-4b-modbus-migration.md`](phase-4b-modbus-migration.md) §
  Sub-phase 3 — the overall Phase 4b VFD migration plan
- [`proto/agristar/vfd.proto`](../proto/agristar/vfd.proto) — wire
  definitions for `VfdPollConfig`, `VfdPollEntry`, `VfdStatus`
- [`constellation-ui/server/src/vfdRegisterMaps.ts`](../constellation-ui/server/src/vfdRegisterMaps.ts) — vendor profile registry (where new VFD models drop in)
- [`constellation-ui/server/src/vfdClient.ts`](../constellation-ui/server/src/vfdClient.ts) — bridge side that composes + sends `VfdPollConfig`
- [`Nova_Firmware/lp_am2434/orbit_server/orbit_storage.h`](../Nova_Firmware/lp_am2434/orbit_server/orbit_storage.h) — current STORAGE HR map (where the new config region lands)
- Phase Tech Modbus User's Manual V1.0_11082022 — vendor reference for
  the LH AquaPhase / LH-V series register layout + custom FC 66
