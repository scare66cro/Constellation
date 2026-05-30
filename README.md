# Constellation

Agristar's next-generation control platform. Replaces the legacy
Mini_IO TM4C129 firmware with a TI AM2434 quad-core ARM platform
(Nova on a LP-AM2434 LaunchPad), driven by a SvelteKit operator
UI over a transparent COBS+protobuf bridge.

**Production status (2026-05-30):** the 4-board bench fleet
(CONTROLLER + STORAGE + GDC + TRITON) is running firmware
`0.A.208` on real hardware. The QEMU bringup path is retired.
The proto-direct UI architecture is landed end-to-end.

## Layout

| Path | Purpose |
|---|---|
| `Nova_Firmware/Platform/` | Cortex-R5F equipment-engine modules (Nova-native ports of the legacy AS2 `Application/*.c` logic — see `docs/firmware-equipment-control.md`). |
| `Nova_Firmware/lp_am2434/` | LP-AM2434 firmware project (CCS + SysConfig). Builds `nova_lp.release.mcelf.hs_fs` for OSPI flash via JTAG (`Flash-LP.ps1`) or UART (`uart_uniflash.py`). |
| `Nova_Firmware/lp_am2434/orbit_server/` | Orbit-board firmware (STORAGE / GDC / TRITON roles, same binary, role baked at flash time). |
| `Nova_Firmware/lp_am2434/watchdog_core/` | Separate R5F core that pets the hardware watchdog. |
| `Nova_Firmware/lp_am2434_enet_ref/` | Vendored TI SDK Ethernet reference baseline (read-only). |
| `constellation-ui/` | SvelteKit operator PWA (`src/`) + Node bridge server (`server/`, port `:9001`). |
| `proto/` | Nanopb / protobuf schema shared between firmware and bridge. |
| `orbit-simulator/` | Optional TS Modbus-TCP orbit simulator for dev (ports `5502+`, `9010+`). Not used in production. |
| `sensor-injector/` | Standalone Modbus-TCP sensor stand-in until the real RS-485 sensor boards arrive. Runs as a systemd service on the bench rpi5. |
| `rpi5/` | Production rpi5 deployment assets — systemd units, deploy scripts, lighttpd front-end config. |
| `Azure/` | Azure Functions (`AgristarCloud`), Django backend (`Backend_Gel_Ops`). |
| `scripts/` | Repo-wide PowerShell tools (`Generate-RegisterMap.ps1`, `Build-CfuBundle.ps1`). |
| `docs/` | Architecture docs + bringup history. Start at `docs/System-State.md` for current state, `CLAUDE.md` for the doc map. |
| `docs/legacy_AS2_reference/` | Read-only AS2 firmware reference — Nova transitionally compiles ~24 `Application/*.c` files from here. Per invariant #0, **never modify.** |
| `Mini_IO legacy refference only firmware/` | Read-only copy of the legacy TM4C129 firmware. Not built. |

## Hardware data path

```
Sensor board ──Modbus RTU──▶ STORAGE orbit ──Modbus TCP HR 200+──▶ CONTROLLER ──UART (COBS+proto)──▶ rpi5 (bridge) ──HTTP+WS──▶ UI
                              GDC orbit     ──Modbus TCP─────────▶
                              TRITON orbit  ──Modbus TCP─────────▶
```

ADC→engineering conversion happens exactly once, on the STORAGE
orbit in `Nova_Firmware/lp_am2434/orbit_server/adc_convert.c`
(per invariant #6). The bridge is a transparent transport gateway
— no scaling, no unit conversion, no default-substitution.

## Developer workflow

**Firmware build + flash (Windows PC, JTAG):**
```powershell
cd Nova_Firmware\lp_am2434
.\Build-Cfu.ps1                                            # build universal hs_fs + .cfu bundle
.\Flash-LP.ps1 -Probe N -Role CONTROLLER -Ip 10.47.27.1    # flash to CONTROLLER via XDS110 probe N
```
Probe discipline: `Flash-LP.ps1` enforces single-probe-attached
via `xdsdfu -e` precheck per invariant #7. See
`docs/LP-Flash-Probe-Discipline.md`.

**Bridge + UI deploy (to bench rpi5):**
```bash
# Bridge code over scp; UI built and rsync'd
scp constellation-ui/server/src/*.ts gellert@10.47.27.108:/home/gellert/Gellert/constellation/constellation-ui/server/src/
ssh gellert@10.47.27.108 'sudo systemctl restart agristar-bridge'
cd constellation-ui && npm run build
wsl ./deploy.sh --target=production
```

**OTA install (one-liner once a `.cfu` bundle is built):**
```bash
ssh gellert@10.47.27.108 \
  'UP=$(curl -s -F "file=@/tmp/constellation-0.A.208.cfu" http://localhost:9001/iot/firmware/upload); \
   SID=$(echo "$UP" | grep -oE "[0-9]+-[a-z0-9]+" | head -1); \
   curl -s -X POST -H "Content-Type: application/json" \
     -d "{\"stagingId\":\"$SID\",\"allowDowngrade\":true}" \
     http://localhost:9001/iot/firmware/install'
```

**Health check:**
```bash
curl http://10.47.27.108:9001/health    # rxCrcErrors:0, rxCobsErrors:0 after any write campaign
```

## External dependencies

- **AgriStar2Testing repo** (sibling): `Nova_Firmware` transitionally
  compiles ~24 `Application/*.c` files from
  `docs/legacy_AS2_reference/`. Clone it as a sibling directory if you
  need to build firmware locally.
- **CCS 12+** with TI MCU+ SDK 09.02+ for firmware build
  (ti-arm-clang, SysConfig, DSS).
- **Node 20+** for bridge + UI.
- **Python 3.11+** for Azure Functions backend.

## Where to read next

- `CLAUDE.md` — invariants + doc map (load-bearing).
- `docs/System-State.md` — how the system is wired today.
- `docs/firmware-bridge-protocol.md` — COBS+CRC + save-path rules.
- `docs/proto-direct-redesign-plan.md` — transport architecture.
- `memories/repo/INDEX.md` — keyword index of every memory note
  (gitignored, per-machine).
