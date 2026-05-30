# sensor-injector

Standalone tool that pushes synthetic sensor values into a Constellation
**orbit LP firmware** over Modbus TCP, until the real RS-485 sensor
boards are built. Replaces the orbit-simulator's sensor-board RTU
stand-in for the controller bring-up loop.

## Why this exists

The orbit LP firmware (Nova_Firmware/lp_am2434, role=STORAGE/GDC/TRITON)
serves Modbus TCP on `:5502` and exposes its sensor block at
`HR 200..263` (16 boards × 4 channels) as **writable** until the RS-485
sensor-board path is wired. This injector lets a developer move sliders
and have those values land in the orbit LP exactly where the controller
firmware expects them — bit-identical to what real sensor boards will
eventually populate.

The legacy `orbit-simulator/` package is still here for now because the
bridge (`constellation-ui/server/src/apiRoutes.ts`) still calls its
REST endpoints for IO config / status / VFD. Migration of those calls
to direct Modbus TCP at the orbit LP is a separate work item; once
that's done, `orbit-simulator/` retires and this injector is the only
sensor stand-in.

## Quick start

```powershell
cd f:\Constellation\sensor-injector
npm install
$env:ORBIT_HOST = "10.1.2.200"   # STORAGE LP
npm start
# → open http://localhost:9100/
```

## Env vars

| Var          | Default        | Notes                                     |
|--------------|----------------|-------------------------------------------|
| `ORBIT_HOST` | `10.1.2.200`   | Target orbit LP IP                        |
| `ORBIT_PORT` | `5502`         | Modbus TCP port (matches LP firmware)     |
| `ORBIT_UNIT` | `1`            | Modbus unit ID                            |
| `PANEL_PORT` | `9100`         | Web panel + REST API listen port          |

## Wire layout — same as Nova_Firmware/lp_am2434/orbit_server/orbit_storage.h

`HR 200 + board*4 + ch` → int16 engineering value:

| Sensor type | Encoding                      |
|-------------|-------------------------------|
| TEMP / IR_TEMP | `round(°C * 10)`           |
| HUMID       | `round(%RH * 10)`             |
| CO2         | `round(ppm)`                  |
| NONE        | `0x7FFF` (UNDEF sentinel)     |

The injector pushes per-slider via FC06 and re-pushes the whole 64-reg
block via FC16 every 5 s, which is below the LP's 5-s comm-loss
safe-mode timeout — a brief socket drop won't trip safe mode.
