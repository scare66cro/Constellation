# Constellation Protocol Modernization Proposal

## Executive Summary

Replace the legacy text-based serial protocol (`^tag=value$CRC!`) inherited from the 2010-era
Lantronix web module with a modern, structured binary protocol between the AM2434 firmware
and the bridge server. This is the single highest-impact architectural change for Constellation
— it eliminates the root cause of most integration bugs and unlocks MQTT, fleet management,
and proper data modeling.

---

## What We're Replacing

### Current Protocol (2010 Lantronix Heritage)

```
^main=21.5,22.0,1,0,0,46,1,0,0,0,0,0,0,0,0$3847291056!
```

| Aspect | Current State | Problem |
|--------|---------------|---------|
| **Format** | ASCII text, comma-separated values | No structure, no types |
| **Schema** | 93 CGI tags, each with positional index arrays | Position 47 in `EquipStatusData` means... what? |
| **Typing** | Everything is `char*` → `atoi()`/`atof()` | Silent corruption on format changes |
| **Versioning** | None | Can't evolve firmware and bridge independently |
| **Framing** | `^payload$CRC!` with RTS/ACK/REPOST handshake | 4-message round trip for a single POST |
| **TX buffer** | 230 bytes max (Lantronix RX limit) | Forced message fragmentation via MultiMsg |
| **Delimiters** | `,` for values, `+` for runtimes, `&` for POST fields | Inconsistent, hardcoded exceptions |
| **Transport** | UART at 230,400 baud, half-duplex state machine | No multiplexing, no priority |
| **Security** | CRC-32 (error detection only) | No authentication, no encryption |

### Why It Matters

Every bug we've fixed in the bridge stems from this:
- Orbit label mismatch → wrong index in EquipStatusData
- RemoteOff value collision → untyped string→int conversion
- Equipment sort order → magic index assumptions
- MultiMsg fragmentation → complex state machine for basic data transfer

---

## Proposed Architecture

### Layer 1: Transport — COBS + CRC16 over UART

Replace the ASCII delimiter framing with **Consistent Overhead Byte Stuffing (COBS)**.

```
[LENGTH:2][COBS-encoded payload][CRC16:2]
```

| Feature | Current | Proposed |
|---------|---------|----------|
| Framing | Start/end delimiters (`^`, `$`, `!`) | COBS — zero-byte packet delimiter |
| Baud rate | 230,400 (Lantronix default) | **921,600** (4x faster, standard rate) |
| Error detection | CRC-32 as decimal string | CRC-16/CCITT as raw bytes |
| Byte efficiency | ~15% overhead (delimiters + decimal CRC) | ~0.4% overhead (COBS) |
| Max message size | 230 bytes (Lantronix limit) | **4096 bytes** (AM2434 has 512KB+ RAM) |
| Escaping | `%xx` URL encoding | None needed (COBS guarantees no zero bytes) |

**Why COBS:** Used by USB, CAN, and many industrial protocols. Constant overhead,
deterministic timing, no escape sequences. A 200-byte payload adds at most 2 bytes.

**Baud rate: 921,600** — Both the AM2434 UART (16550-compatible) and RPi5 PL011 support
this standard rate with clean clock divisors. It's 4x the current 230,400, giving ~92 KB/s
throughput vs ~2 KB/s steady-state demand — massive headroom for log downloads and
settings dumps. Well within signal integrity limits for a short board-to-board trace inside
an enclosure. Every serial library (node `serialport`, Linux `termios`, QEMU) supports
`B921600` natively. Going higher (1.5M, 2M, 3M) introduces clock divisor rounding errors
that vary by chip — not worth the risk for industrial.

**Future transport upgrade path:** If 921,600 ever becomes a bottleneck (unlikely), the
AM2434 has USB 2.0 — changing to USB CDC gives 12 Mbps (Full Speed) or 480 Mbps (High Speed)
and appears as `/dev/ttyACM0` on the RPi5. The bridge code just changes `SERIAL_PORT` —
no protocol changes needed.

**Why not just JSON over UART:** JSON parsing is expensive on a Cortex-R5F with no OS-level
string libraries. Binary is 3-10x faster to encode/decode and much smaller.

### Layer 2: Serialization — Protocol Buffers (nanopb)

Replace comma-delimited strings with **Protocol Buffers** — field-tagged binary encoding
with guaranteed backward/forward compatibility.

```protobuf
// constellation.proto — THE source of truth for all messages
syntax = "proto3";
package agristar;

message SystemStatus {
  float  plenum_temp     = 1;
  float  target_temp     = 2;
  float  outside_temp    = 3;
  float  humidity_actual = 4;
  float  humidity_target = 5;
  bool   fan_running     = 6;
  uint32 fan_speed       = 7;   // 0-100%
  SystemMode system_mode = 8;
  SystemState system_state = 9;
  uint32 uptime_sec      = 10;
  uint32 alarm_bitmask   = 11;
}

enum SystemMode {
  MODE_STANDBY  = 0;
  MODE_COOLING  = 1;
  MODE_HEATING  = 2;
  MODE_DRYING   = 3;
  MODE_CURING   = 4;
}
```

Field numbers are the contract. Firmware v1.0 can omit `field 12` that v2.0 adds —
the bridge fills in defaults. Bridge v2.0 can decode v1.0 firmware messages without
error. This is critical for a fleet of hundreds of units.

| Aspect | Current (ASCII) | Protobuf |
|--------|----------------|----------|
| Type safety | None (all strings) | Compile-time on both sides |
| Versioning | None | Field numbers = backward compatible |
| Wire size | ~80 bytes for SystemStatus | ~30 bytes |
| Firmware lib | `sprintf` + `atoi` | nanopb (~4KB flash, zero alloc) |
| Bridge lib | string split + parseInt | ts-proto (generated interfaces) |
| Schema | Comments in dataCache.ts | `.proto` file = executable docs |

### Layer 3: Message Schema — Proto-Defined, Field-Tagged, Versioned

Replace the 93-entry CGI index table with `.proto` message definitions.
`nanopb_generator` produces C structs; `ts-proto` produces TypeScript interfaces.

```protobuf
// ═══════════════════════════════════════════════════════════
// proto/agristar/common.proto — shared across all messages
// ═══════════════════════════════════════════════════════════
syntax = "proto3";
package agristar;

// Envelope wraps every message on the wire
message Envelope {
  uint32 protocol_version = 1;  // bump on breaking changes only
  uint32 seq              = 2;  // sequence number for ACK matching
  oneof msg {
    SystemStatus     system_status     = 10;
    EquipmentStatus  equipment_status  = 11;
    ProgramSettings  program_settings  = 12;
    WarningReport    warning_report    = 13;
    IoConfig         io_config         = 14;
    Runtimes         runtimes          = 15;
    EquipmentCmd     equipment_cmd     = 80;
    SettingsUpdate   settings_update   = 81;
    SystemCmd        system_cmd        = 82;
    Ack              ack               = 100;
    DataRequest      data_request      = 101;
    LogChunk         log_chunk         = 102;
  }
}

message Ack {
  uint32 ref_seq = 1;  // sequence number of the message being acknowledged
  AckStatus status = 2;
}

enum AckStatus {
  ACK_OK             = 0;
  ACK_INVALID_PARAM  = 1;
  ACK_ALARM_BLOCK    = 2;
  ACK_BUSY           = 3;
  ACK_VERSION_MISMATCH = 4;
}
```

```protobuf
// ═══════════════════════════════════════════════════════════
// proto/agristar/equipment.proto
// ═══════════════════════════════════════════════════════════
syntax = "proto3";
package agristar;

message EquipmentStatus {
  repeated EquipState equipment = 1;
}

message EquipState {
  uint32 eq_index   = 1;  // EQUIPMENT_IO enum value
  bool   output_on  = 2;
  RemoteOffState remote_off = 3;
  AlarmState alarm  = 4;
}

enum RemoteOffState {
  REMOTE_AUTO   = 0;
  REMOTE_OFF    = 1;
  REMOTE_MANUAL = 2;
}

message EquipmentCmd {
  uint32 eq_index = 1;
  RemoteOffState new_state = 2;
}
```

**Key advantage:** Adding `float power_consumption = 5;` to `EquipState` in a future
firmware version requires zero changes to the bridge — unknown fields are silently
skipped. The bridge can also add fields it wants from newer firmware without breaking
older controllers.

```typescript
// ═══════════════════════════════════════════════════════════
// AUTO-GENERATED by ts-proto from equipment.proto
// Bridge imports this — never hand-edit
// ═══════════════════════════════════════════════════════════
export interface EquipmentStatus {
  equipment: EquipState[];
}

export interface EquipState {
  eqIndex: number;
  outputOn: boolean;
  remoteOff: RemoteOffState;
  alarm: AlarmState;
}

export enum RemoteOffState {
  REMOTE_AUTO = 0,
  REMOTE_OFF = 1,
  REMOTE_MANUAL = 2,
}
```

### Layer 4: Flow Control — Request/Response + Async Push

Replace the RTS/ACK/REPOST state machine with a simpler model:

```
┌─────────────────────────────────────────────────────────┐
│  Firmware → Bridge (async push, no request needed)      │
│                                                         │
│  MSG_SYSTEM_STATUS    every 1s (or on change)           │
│  MSG_EQUIPMENT_STATUS every 1s (or on change)           │
│  MSG_WARNINGS         on change only                    │
│  MSG_HEARTBEAT        every 5s (keepalive)              │
│                                                         │
│  Bridge → Firmware (request/response)                   │
│                                                         │
│  MSG_EQUIPMENT_CMD    → MSG_ACK (with status)           │
│  MSG_SETTINGS_UPDATE  → MSG_ACK (with status)           │
│  MSG_SYSTEM_CMD       → MSG_ACK (with status)           │
│  MSG_REQUEST_DATA     → MSG_xxx (requested data)        │
└─────────────────────────────────────────────────────────┘
```

| Feature | Current | Proposed |
|---------|---------|----------|
| GET data | Bridge polls with Initialize + tag request | Firmware pushes priority data automatically |
| POST data | RTS → ACK → SEND → REPOST → ACK (5 messages) | CMD → ACK (2 messages) |
| Confirmation | String echo/comparison | Sequence number + status code |
| Timeouts | 50ms tick polling, complex state machine | Simple seq-based timeout per command |
| Priority | None — everything queued equally | High/low priority queues |

**ACK message format:**
```c
typedef struct __attribute__((packed)) {
  uint8_t  version;
  uint8_t  msgId;       // MSG_ACK = 0xFF
  uint16_t seq;         // echoes the command's seq number
  uint8_t  status;      // 0=OK, 1=INVALID_PARAM, 2=ALARM_BLOCK, etc.
} MsgAck;
```

This eliminates the entire RTS/ACK/REPOST state machine (the single most complex and
fragile part of `serialBridge.ts`).

---

## Migration Strategy

### Phase 0: Proto Definitions + Codegen Pipeline (Week 1)

Create the `.proto` files and build pipeline:

```
Constellation/
  proto/
    agristar/
      common.proto          ← Shared enums (SystemMode, SystemState, etc.)
      system_status.proto    ← Live dashboard data
      equipment.proto        ← Equipment status + commands
      settings.proto         ← All settings pages
      alarms.proto           ← Warnings and alarms
      logs.proto             ← Log download chunks
    generate.sh             ← Runs nanopb + ts-proto, outputs to:
      generated/
        c/                  ← .pb.h + .pb.c (firmware includes these)
        ts/                 ← .ts interfaces (bridge imports these)
```

Both firmware and bridge import from `generated/`. A field rename or
type change produces compile errors on BOTH sides — no more silent mismatches.

### Phase 1: Dual-Protocol Bridge (Week 2-3)

**No longer needed.** Since we're doing a full replacement (not incremental), and we
control both firmware and bridge, there's no dual-protocol period. The new protocol
ships with the first Constellation firmware image. The old ASCII protocol remains
in the AS2 tester (untouched, reference only).

### Phase 2: Firmware Protocol Layer (Week 2-4)

Replace the firmware's `DataExc.c` / `UI_Messages.c` layer:

```
New files in Constellation/Nova_Firmware/Platform/:
  nova_protocol.c  — COBS framing + CRC16 + nanopb encode/decode
  nova_messages.c  — Replaces UI_Messages.c: typed protobuf message builders
  nova_dataexc.c   — Replaces DataExc.c: new RX/TX state machine

Generated files (from proto/):
  generated/c/*.pb.h   — nanopb-generated struct definitions
  generated/c/*.pb.c   — nanopb-generated encode/decode functions
```

Because Nova_Firmware's Makefile can selectively override Mini_IO source files,
we can **replace** `DataExc.c` and `UI_Messages.c` with Nova versions while keeping
all other application code unchanged. The control logic (Controls.c, States.c, Modes.c)
doesn't care how data gets to the bridge — it just reads/writes Settings and outputs.

### Phase 3: Bridge-Side Migration (Week 3-5)

Replace the bridge's parsing layer:

| Current File | Replacement |
|-------------|-------------|
| `protocol.ts` (CRC32, delimiters) | `novaProtocol.ts` (COBS, CRC16, protobuf) |
| `serialBridge.ts` (RTS/ACK state machine) | `novaSerialBridge.ts` (push + request/response) |
| `dataCache.ts` (93-entry CGI string table) | `dataStore.ts` (typed protobuf message objects) |

The REST API and WebSocket layer (`apiRoutes.ts`, `wsManager.ts`) stay largely unchanged
— they just receive typed protobuf-decoded objects instead of comma-delimited strings.
`ts-proto` generates plain TypeScript interfaces (not classes), so `JSON.stringify()`
works directly for WebSocket broadcast.

### Phase 4: Eliminate MultiMsg (Week 4-5)

With a 4096-byte message limit, most data fits in a single message. For truly large data
(log downloads), use a proper chunked transfer defined in the proto schema:

```protobuf
message LogChunk {
  uint32 chunk_index  = 1;
  uint32 total_chunks = 2;
  bytes  data         = 3;   // up to ~3500 bytes per chunk
}
```

---

## Future Unlocks

### MQTT (Phase 5+)

With typed messages, publishing to MQTT is trivial:

```typescript
// In the bridge server:
onMessage(msg: MsgSystemStatus) {
  this.dataStore.update(msg);
  this.wsBroadcast('system-status', msg);

  // NEW: publish to MQTT broker
  this.mqtt.publish(`agristar/${panelId}/status`, JSON.stringify(msg));
}
```

**Topics:**
- `agristar/{id}/status` — system status (1s)
- `agristar/{id}/equipment` — equipment states (on change)
- `agristar/{id}/alarms` — alarm events (on change)
- `agristar/{id}/cmd` — inbound commands (subscribe)

### OPC-UA (Phase 6+)

The typed message schema maps directly to OPC-UA node structures. This matters for
SCADA integration in larger facilities that use Ignition, FactoryTalk, or WinCC.

### Fleet Management (Phase 5+)

The bridge can report to a cloud API:

```
Controller → Bridge → MQTT Broker → Cloud API → Dashboard
                 ↓
            Local UI (WebSocket)
```

Each controller becomes a managed endpoint with remote monitoring, firmware updates,
and centralized alarm management.

### Firmware Signing (Phase 5+)

The AM2434 has hardware crypto accelerators. With protobuf, adding
HMAC-SHA256 message authentication is a field addition:

```protobuf
message SecureEnvelope {
  bytes  hmac    = 1;  // HMAC-SHA256 of payload
  bytes  payload = 2;  // serialized Envelope
}
```

---

## What Doesn't Change

- **Control logic**: Controls.c, States.c, Modes.c — completely untouched
- **I/O layer**: SerialShift.c, RS485, PWM, Analog_Input — untouched
- **Settings storage**: Settings.c, SettingsTypes.h — untouched
- **StorePostData.c**: The Store* functions stay, but get called with typed structs
  instead of string arrays
- **SvelteKit UI**: The UI talks to the bridge via REST/WebSocket — it never sees
  the binary protocol
- **Orbit/Sensor simulators**: They use Modbus TCP/RTU — completely separate

---

## Bill of Materials

### Firmware Libraries
| Library | Size | License | Purpose |
|---------|------|---------|---------|
| [nanopb](https://github.com/nanopb/nanopb) | ~4KB flash | Zlib | Protobuf encode/decode for embedded (zero-alloc, no malloc) |
| COBS | ~50 lines C | Public domain | Frame encoding |
| CRC-16/CCITT | ~20 lines C | Public domain | Error detection |

**Total firmware footprint: ~5KB flash, ~500 bytes RAM**

### Bridge Libraries
| Library | Size | License | Purpose |
|---------|------|---------|---------|
| `ts-proto` | codegen | Apache-2.0 | Generate TypeScript from .proto (idiomatic interfaces, not classes) |
| `protobufjs` | 45KB | BSD-3 | Runtime protobuf encode/decode (ts-proto uses this) |
| `cobs` (npm) | 2KB | MIT | COBS framing |

### Build Tooling
| Tool | Purpose |
|------|---------|
| `protoc` | Protocol Buffers compiler (runs .proto → plugin output) |
| `nanopb_generator` | protoc plugin for embedded C (generates .pb.h + .pb.c) |
| `ts-proto` | protoc plugin for TypeScript (generates .ts interfaces) |
| `generate.sh` | One script runs both generators, outputs to `generated/c/` and `generated/ts/` |

---

## Risk Assessment

| Risk | Mitigation |
|------|------------|
| Firmware/bridge protocol mismatch | `.proto` files generate both sides — compile-time safety |
| nanopb parsing bugs on firmware | nanopb is battle-tested (100M+ devices: Nest, Fitbit, etc.) |
| Regression in control logic | Protocol layer is completely separate from control logic |
| Can't debug binary on the wire | Bridge logs decoded JSON for every message; `protoc --decode` CLI tool |
| UART reliability at higher throughput | COBS + CRC-16 is MORE reliable than current ASCII framing |
| Development time | Full replacement is faster than maintaining dual protocol |
| Field number collisions | `.proto` syntax enforces unique field numbers at compile time |
| Version drift across fleet | Protobuf's field-tagged encoding handles unknown fields gracefully |

---

## Decision Required

### Decided (April 2026)

1. **Serialization: Protocol Buffers (nanopb)** — Field-tagged encoding guarantees
   backward/forward compatibility across firmware versions. Critical for fleet of
   hundreds of units where firmware and bridge versions will drift. The `.proto`
   file is the single source of truth; `nanopb_generator` produces C, `ts-proto`
   or `protobuf-ts` produces TypeScript.

2. **Migration scope: Full replacement** — No dual-protocol maintenance. Replace all
   93 CGI tags and 40+ Store functions with typed protobuf messages. The QEMU
   simulation environment gives us a safe place to validate the full migration
   before shipping.

3. **Source of truth: `.proto` files** — Generate both C (nanopb) and TypeScript
   from the same `.proto` definitions. The firmware team (C side) writes .proto
   files, CI generates both languages. Adding a field is one line in the .proto
   file → both sides get it at compile time.
