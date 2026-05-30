# Constellation

Agristar's next-generation control platform — currently being built from the ground up. Replaces the legacy Mini_IO TM4C129 firmware with a TI AM2434 quad-core ARM platform (Nova) plus a sensor-board satellite (Orbit), driven by a SvelteKit operator UI and an Azure cloud backend.

## Layout

| Path | Purpose |
|---|---|
| `Nova_Firmware/` | AM2434 R5F-0 control core firmware (FreeRTOS). Reuses many `Mini_IO/Application/*` modules. |
| `orbit-simulator/` | TypeScript Modbus-RTU sensor-board simulator (TCP :5502). Standalone REST :9010+ retired Apr 29 2026 — bridge talks Modbus TCP to LPs / sensor-injector instead. |
| `sensor-injector/` | Standalone Modbus-TCP master that pushes sensor values into an LP / orbit (REST + web panel on :9100). |
| `qemu-constellation/` | QEMU machine + scripts for running Nova firmware on a developer workstation. |
| `constellation-ui/` | SvelteKit operator PWA + bridge server (Node :9001). |
| `Azure/` | Azure Functions (`AgristarCloud`), Django backend (`Backend_Gel_Ops`), local dev compose. |
| `proto/` | Nanopb / protobuf schema shared between firmware and bridge. |
| `docs/` | Architecture and integration plans. |
| `Mini_IO legacy refference only firmware/` | Reference-only copy of the legacy firmware. Not built. |

## Hardware data path (single source of truth)

```
Sensor Board ──Modbus RTU──▶ Orbit ──Modbus TCP HR 200+──▶ Nova / ARM
```

Simulators use the same wire interfaces as real hardware. No HTTP shortcuts in the data path.

## Local dev stack

```powershell
# Windows (PowerShell)
.\Start-Constellation.ps1            # Start everything
.\Start-Constellation.ps1 -Restart   # Restart everything
.\Start-Constellation.ps1 -Stop      # Stop everything
```

Brings up: PostgreSQL → Orbit Sim → Nova QEMU → Bridge (:9001) → Django (:8000) → React PWA (:3000) → Constellation UI (:81).

## External dependencies

- **`Mini_IO/`** (sibling repo): Nova_Firmware compiles against many headers in the legacy `Mini_IO/Application/` tree. This repo does not vendor them. Clone the `AgriStar2Testing` repo as a sibling directory if you need to build firmware locally.
- **WSL** + `arm-none-eabi-gcc` + `qemu-system-arm` for firmware build & run.
- **Node 20+** for `constellation-ui` and `orbit-simulator`.
- **Python 3.11+** for `Azure/Backend_Gel_Ops`.

## Status

In active development. See `docs/` for the integration plan.
