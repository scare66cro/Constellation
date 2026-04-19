# Simulator → Production Transition Guide

What must change when moving from the dev simulator stack to real Nova + Orbit hardware.

---

## Architecture Overview

```
SIMULATOR STACK                         PRODUCTION STACK
═══════════════                         ════════════════

┌─────────────────┐                     ┌─────────────────┐
│  ARM Simulator   │  TCP :9000          │  Nova AM2434     │  UART /dev/ttyAMA0
│  (armSimulator.ts)│◄───────────┐       │  (real firmware)  │◄───────────┐
└─────────────────┘             │       └─────────────────┘             │
                                │                                       │
┌─────────────────┐             │       ┌─────────────────┐             │
│  Bridge Server   │─────────────┘       │  Bridge Server   │─────────────┘
│  (index.ts)      │                     │  (index.js)      │  ← same code
│  port 81         │                     │  port 3001       │
└────────┬────────┘                     └────────┬────────┘
         │ Modbus TCP                             │ ← NOT USED
         │ :5502                                  │
┌────────▼────────┐                     ┌─────────────────┐
│  Orbit Simulator │                     │  Nova firmware   │  Modbus TCP :502
│  (orbitSimulator) │                     │  (hal_orbit.c)   │────────────────┐
│  port 9010-9013  │                     └─────────────────┘                │
└─────────────────┘                                                        │
                                        ┌─────────────────┐                │
                                        │  Real Orbit PCB  │◄───────────────┘
                                        │  (embedded FW)   │
                                        │  192.168.0.{DIP} │
                                        └─────────────────┘
```

---

## 1. Bridge Server → Nova Communication

| Aspect | Simulator | Production | What Changes |
|--------|-----------|------------|--------------|
| **Transport** | TCP socket to `localhost:9000` | UART `/dev/ttyAMA0` @ 230400 baud | Set `SERIAL_PORT=/dev/ttyAMA0` env var |
| **Protocol** | Same CRC-32 framed messages | Same | Nothing — `serialBridge.ts` already supports both |
| **ARM firmware** | `armSimulator.ts` (Node.js) | Real Nova R5F firmware (C) | Remove armSimulator from deployment |
| **Configuration** | `SERIAL_PORT=tcp://localhost:9000` | `SERIAL_PORT=/dev/ttyAMA0` | One env var change |

**File:** `constellation-ui/server/deploy/agristar-bridge.service` — already has production env vars.

---

## 2. Nova → Orbit Communication (Modbus TCP)

| Aspect | Simulator | Production | What Changes |
|--------|-----------|------------|--------------|
| **Who polls Orbits** | Bridge server (`orbitClient.ts`) | Nova firmware (`hal_orbit.c`) | **Remove bridge Modbus polling** |
| **IP addressing** | `10.47.27.{DIP}:5502` (QEMU net) | `192.168.0.{DIP}:502` (real Ethernet) | Already handled by `#ifdef QEMU_BUILD` in `hal_orbit.h` |
| **Equipment → DO mapping** | Bridge reads OutputConfig, maps to orbit DOs | Nova firmware does this directly via `IoBoard[].OutputState` → coils | Bridge no longer needs `syncEquipOutputsViaModbus()` |
| **DI → Equipment mapping** | Bridge doesn't read orbit DIs (ARM sim generates inputs) | Nova reads orbit DIs via FC02 → `IoBoard[].InputState` | Already implemented in `hal_orbit.c orbit_poll_io()` |
| **Orbit labels** | REST API pushes labels for the web panel | Not needed — no web panel on real Orbit boards | Labels are a simulator-only convenience |

### Key: `syncEquipOutputsViaModbus()` goes away in production

In the simulator, the bridge acts as a stand-in for Nova's Modbus TCP master. On real hardware, Nova itself does the I/O polling. The bridge only needs to serve the UI and relay user commands to Nova via UART.

**Production TODO:** Add a `SIMULATOR_MODE` flag or detect whether an orbit client is connected. When running on real hardware, skip `syncEquipOutputsViaModbus()` and `syncIoConfigToOrbit()`.

---

## 3. Sensor Boards

| Aspect | Simulator | Production | What Changes |
|--------|-----------|------------|--------------|
| **Sensor data source** | Orbit sim emits fake temps/humidity directly from built-in default boards | Real I2C/SPI sensor boards on Orbit PCB | None — orbit sim already fabricates sensor data |
| **Data path** | Orbit sim exposes sensor registers → bridge reads via Modbus TCP | Nova reads Orbit holding regs 200+ → gets real sensor data | Already in `hal_orbit.c orbit_read_sensors()` |
| **Analog board config** | Orbit sim has default temp/humidity boards | Real boards auto-detected by Orbit firmware | No code change needed |

---

## 4. VFD Drives

| Aspect | Simulator | Production | What Changes |
|--------|-----------|------------|--------------|
| **VFD communication** | Orbit sim has built-in VFD simulator (holding regs 100+) | Real VFD drives on RS-485 bus via Orbit Port B | No bridge change — VFD regs are passthrough |
| **Bridge VFD client** | Reads VFD registers from orbit sim via Modbus TCP | Same path — reads from Nova which reads from Orbit | Set `VFD_HOST` and `VFD_PORT` env vars |

---

## 5. Network & IP Addressing

| Aspect | Simulator | Production |
|--------|-----------|------------|
| **Orbit IPs** | `10.47.27.{DIP}` (QEMU) or `127.0.0.1:9010+` (local) | `192.168.0.{DIP}` |
| **Nova IP** | N/A (runs on same machine) | `192.168.0.1` (or DHCP) |
| **Bridge IP** | `localhost:81` | `192.168.0.1:3001` (same machine as RPi5) |
| **QEMU flag** | `QEMU_BUILD` defined in firmware Makefile | Remove `QEMU_BUILD` — use production linker script `am2434_r5f.ld` |

**hal_orbit.h already handles this:**
```c
#ifdef QEMU_BUILD
#define ORBIT_IP_A  10
#define ORBIT_IP_B  47
#define ORBIT_IP_C  27
#define ORBIT_MBTCP_PORT  5502
#else
#define ORBIT_IP_A  192
#define ORBIT_IP_B  168
#define ORBIT_IP_C  0
#define ORBIT_MBTCP_PORT  502
#endif
```

---

## 6. Firmware Build

| Aspect | Simulator | Production |
|--------|-----------|------------|
| **Target** | QEMU `am2434_qemu_r5f0.ld` (flat RAM at 0x0) | Real `am2434_r5f.ld` (ATCM/BTCM/MSRAM split) |
| **Build flag** | `DEFS += -DQEMU_BUILD` | Remove `QEMU_BUILD` |
| **Orbit stub** | Optional `QEMU_STUB_ORBIT` skips Modbus polling | Remove — real polling always active |
| **Timer** | QEMU tick timer at `0x02400000`, virtual clock | Real PMU cycle counter, hardware timer |
| **Flash** | RAM-backed fake flash (`hal_flash.c` QEMU path) | Real OSPI NOR via TI MCU+ SDK (`Flash_write/read`) |
| **UART** | QEMU 16550 serial_mm with TCP chardev backend | Real AM2434 UART hardware — same 16550 register interface |

---

## 7. What Stays the Same (No Changes Needed)

These components work identically in simulator and production:

- **Bridge REST API** (`apiRoutes.ts`) — serves the UI, relays commands
- **WebSocket manager** (`wsManager.ts`) — real-time UI updates
- **Data cache** (`dataCache.ts`) — CGI variable cache
- **Serial protocol** (`protocol.ts`) — CRC-32 framing, ACK/NAK
- **SvelteKit UI** (`constellation-ui/src/`) — entire frontend
- **IO Config page** — equipment assignment, port mapping
- **Equipment status engine** — ARM sim and real firmware use identical logic

---

## 8. Wire Protocol Invariants (sim and prod identical)

Even though the *transport* changes (TCP↔UART), the COBS+CRC envelope and protobuf payload semantics are byte-identical between simulator and production. The same encode/decode rules apply both places, and bugs found in one will manifest in the other.

See [`firmware-bridge-protocol.md`](firmware-bridge-protocol.md) for the full list of invariants. The headline ones:

1. **proto3 zero-suppression** — use `pb_uint32_force` / `pbVarintForce` for any field where 0 is a meaningful value (Mode=OFF, index=0, threshold=0, fixed-length array slots, cross-coupled settings).
2. **Mode-dependent encoders** mirror the legacy `StoreXxx()` positional layout per mode — never send a fixed-position superset.
3. **Repeated-submessage decoders** clear the destination array up front; counters MUST be function-local, not `static`.
4. **TX serialization** — all firmware TX goes through `NovaProto_SendRaw` mutex; bypassing it produces interleaved COBS framing and a CRC-error storm.
5. **`/health`** exposes `txFrames` / `rxFrames` / `rxCrcErrors` — these must read 0 errors after any clean save campaign in *both* environments.

If a save works in the simulator but fails on real hardware (or vice versa), suspect:
- Timing differences exposing a missing TX-mutex bypass.
- Non-zero `rxOverflows` indicating UART buffer depth needs tuning at 230400 baud on real hardware.
- Endianness or struct-packing differences in `Settings.*` fields touched outside the proto encoder/decoder layer.

---

## 9. Sim-only behaviours to be aware of

- **Bank-A / Bank-B JSON** files (`constellation-ui/server/src/.arm-settings-bank-{a,b}.json`) stand in for ARM persistent settings storage. In production, settings live in OSPI NOR via the SDK Flash API.
- **`Start-Constellation.ps1`** orchestrates the full sim stack (QEMU + bridge + UI). Production uses systemd / init scripts on the panel.
- **Bridge runs in a hidden PowerShell window** under WSL/Windows hybrid; logs only reach the agent via files written to the workspace path. Production runs the bridge as a foregrounded Node process under a service supervisor.
- **CGI shim layer** (`legacyShim.sendPost` in `index.ts`) translates POST bodies built by the legacy AS2 UI into `MSG_SETTINGS_UPDATE` envelopes. This shim is intentional and stays in production until the UI is fully migrated to native protobuf calls — it is **not** a hack to remove.

- **GDC door control** — same register map (300-319), same safety interlocks

---

## 8. Deployment Checklist

### Bridge Server (RPi5)
- [ ] Build: `npm run build` in `constellation-ui/server/`
- [ ] Set env: `SERIAL_PORT=/dev/ttyAMA0`, `SERIAL_BAUD=230400`
- [ ] Set env: `VFD_HOST=<orbit-ip>`, `VFD_PORT=502` (if VFD drives present)
- [ ] Disable: `syncEquipOutputsViaModbus()` (Nova handles orbit I/O directly)
- [ ] Disable: `syncIoConfigToOrbit()` (labels are simulator-only)
- [ ] Install systemd service: `deploy/agristar-bridge.service`

### Nova Firmware
- [ ] Remove `QEMU_BUILD` from Makefile DEFS
- [ ] Remove `QEMU_STUB_ORBIT` if set
- [ ] Use production linker script `am2434_r5f.ld`
- [ ] Configure Orbit board DIP switch IDs in Settings/EEPROM
- [ ] Verify UART1 baud rate matches bridge (230400)

### Orbit Boards
- [ ] Set DIP switches (ID 2 = MAIN, 3 = EXP_1, etc.)
- [ ] Connect Ethernet to Nova's switch/network
- [ ] Verify Modbus TCP port 502 accessible
- [ ] Connect I/O wiring per IO Config assignments

--- 

## 9. Components to Remove from Production Deployment

These are simulator-only and should NOT be deployed:

| Component | Port | Purpose |
|-----------|------|---------|
| `armSimulator.ts` | 9000 | Fake ARM controller |
| `orbitSimulator.ts` | 9010-9013, 5502+ | Fake Orbit boards |
| `Start-Constellation.ps1` | — | Dev launcher script |
| QEMU (`start_nova_qemu.sh`) | — | CPU emulator |
| Orbit web panels (`panel/index.html`) | — | Debug UI for simulator |

