# Agristar Bridge Server

WebSocket + REST bridge server that replaces the legacy C-based GellertServer.  
Connects the Svelte UI directly to the ARM controller (TI TM4C129) via serial UART  
**or** to the software ARM simulator via TCP (no hardware needed).

## Architecture

```
                                ┌─────────────────┐
                                │  ARM Simulator   │  ← armSimulator.ts
                                │  TCP :9000       │     (runs without hardware)
                                └────────┬────────┘
                                         │ TCP
┌──────────────┐  HTTP/WS :5173  ┌───────┴──────────┐  UART 230400   ┌─────────────┐
│  Svelte UI   │ ──────────────▸ │  Bridge Server   │ ─────────────▸ │  ARM MCU    │
│  (browser)   │  Vite proxy     │  HTTP :3001       │  8N1           │  TM4C129    │
│              │  /iot/*         │                   │                │  FreeRTOS   │
└──────────────┘                │  dataCache.ts     │                └─────────────┘
                                │  serialBridge.ts  │
                                │  protocol.ts      │
                                └───────────────────┘
```

---

## Quick Start (step by step)

### Prerequisites

- **Node.js** 18+ (check with `node --version`)
- **npm** (comes with Node.js)

### 1. Install dependencies (one time)

```bash
cd ui-svelte/server
npm install

cd ../          # back to ui-svelte
npm install
```

### 2. Pick a mode and start

There are **three ways** to run the system. Pick one:

---

#### Mode A — Simulator (recommended for development)

No hardware needed. The ARM simulator mimics the real TM4C129 controller  
with drifting temperatures, realistic timers, and full write support.

**Open three terminals:**

```bash
# Terminal 1 — ARM Simulator (TCP port 9000)
cd ui-svelte/server
npm run dev:sim

# Terminal 2 — Bridge Server (HTTP port 3001, connected to simulator)
cd ui-svelte/server
# Windows PowerShell:
$env:SERIAL_PORT = "tcp://localhost:9000"; npx tsx src/index.ts
# Linux/Mac:
SERIAL_PORT=tcp://localhost:9000 npx tsx src/index.ts

# Terminal 3 — SvelteKit UI (port 5173)
cd ui-svelte
npm run dev
```

Open **http://localhost:5173** in your browser.

---

#### Mode B — Mock (static data, no protocol)

Instant startup, no ARM simulator. Data is static (never changes).  
Useful for quick UI layout work.

```bash
# Terminal 1 — Bridge in mock mode
cd ui-svelte/server
npm run dev          # SERIAL_PORT defaults to "mock"

# Terminal 2 — SvelteKit UI
cd ui-svelte
npm run dev
```

---

#### Mode C — Real Hardware

Connect to a real TM4C129 ARM controller via USB-to-serial adapter.

```bash
# Terminal 1 — Bridge with real serial port
cd ui-svelte/server
# Windows:
$env:SERIAL_PORT = "COM3"; npx tsx src/index.ts
# Linux:
SERIAL_PORT=/dev/ttyUSB0 npx tsx src/index.ts

# Terminal 2 — SvelteKit UI
cd ui-svelte
npm run dev
```

---

### 3. Verify it works

After starting, open a browser or run:

```bash
# Health check — should return { status: "ok", cached: 44, cgiVars: 89 }
curl http://localhost:3001/iot/health

# Check live data — should return plenum temp, humidity, etc.
curl http://localhost:3001/iot/ws/frontmatter-data
```

---

## Environment Variables

| Variable       | Default  | Description                                     |
|---------------|----------|-------------------------------------------------|
| `PORT`        | `3001`   | HTTP/WebSocket listen port                      |
| `SERIAL_PORT` | `mock`   | `mock`, `tcp://host:port`, or serial device path|
| `SERIAL_BAUD` | `230400` | Serial baud rate (ignored for TCP/mock)         |
| `SVELTE_BUILD`| _(empty)_| Path to SvelteKit build output for static serving |

## npm Scripts (server)

| Script       | Command                      | Description                              |
|-------------|------------------------------|------------------------------------------|
| `dev`       | `tsx watch src/index.ts`     | Bridge in mock mode with auto-reload     |
| `dev:sim`   | `tsx src/armSimulator.ts`    | Start the ARM simulator on TCP port 9000 |
| `build`     | `tsc`                        | Compile TypeScript to dist/              |
| `start`     | `node dist/index.js`         | Run production build                     |

## WebSocket Protocol

Connect to `ws://localhost:3001/iot/ws` and send JSON messages:

### Subscribe to a channel
```json
{ "action": "subscribe", "channel": "frontmatter-data" }
```

### Server pushes
```json
{ "channel": "frontmatter-data", "data": { "MainData": "22.5,65,..." }, "ts": 1709123456789 }
```

### Available channels
- `frontmatter-data` — Main dashboard data (pushed every 3s or on ARM update)
- `header-data` — Panel name, mode, date/time
- `tcpip-data` — Network node list
- `upgrade-data` — Firmware upgrade progress
- `network-data` — Network monitor
- `download-data` — Log download progress
- `equipment-data` — Equipment status

## REST API (backward compatible)

All endpoints under `/iot/*`:

| Method | Path | Description |
|--------|------|-------------|
| GET | `/iot/ws/<channel>` | Polling fallback for any WebSocket channel |
| GET | `/iot/<page>` | Page data (basic, analog, pid, etc.) |
| GET | `/iot/health` | Server health check |
| GET | `/iot/debug/cache` | Dump all cached CGI variables |
| POST | `/iot/button` | Send button press to ARM |
| POST | `/iot/PostSave.jsp` | Save settings to ARM |

## Serial Protocol

Frame format matching the ARM firmware:
```
^<Tag>=<Value>$<CRC32 decimal>!
```

- Start: `^` (0x5E)
- Payload: `Tag=Value1,Value2,...`
- CRC separator: `$` (0x24)
- CRC: Standard CRC-32 as decimal string
- End: `!` (0x21)

## Development with SvelteKit

The Vite dev server is configured to proxy `/iot/*` to `http://localhost:3001`:

```bash
# Terminal 1: Start bridge server
cd ui-svelte/server && npm run dev

# Terminal 2: Start SvelteKit dev server
cd ui-svelte && npm run dev
```

The proxy is configured in `vite.config.ts` with WebSocket support for `/iot/ws`.

## Files

| File | Purpose |
|------|---------|
| `src/index.ts` | Main entry — Express + HTTP + WebSocket server |
| `src/wsManager.ts` | WebSocket connection manager, channel subscriptions |
| `src/serialBridge.ts` | Serial/TCP/mock communication with ARM |
| `src/protocol.ts` | CRC-32, message framing, ACK/NAK |
| `src/dataCache.ts` | In-memory cache of 89 CGI variables from ARM |
| `src/apiRoutes.ts` | REST API endpoints |
| `src/armSimulator.ts` | ARM TM4C129 simulator (TCP, no hardware needed) |

## ARM Simulator Details

The ARM simulator (`armSimulator.ts`) faithfully reproduces the behavior of the
real TM4C129 firmware so you can develop and test the UI without hardware.

### What it simulates

| Feature | Detail |
|---------|--------|
| **Init handshake** | Version check, `Initialize=1.07,AS2` response |
| **Push timers** | 3s (Main, Mode, DateTime, EquipStatus), 7s (SensorData, Warnings), 50s (all settings) |
| **Sensor drift** | Plenum/outside/return temps drift ±0.15°C every 2s; humidity ±0.5%; CO2 ±10 ppm |
| **MultiMsg protocol** | Versions, SensorLabels, IoDefinition sent as `MultiMsg`/`MultiEnd` sequences |
| **Write protocol** | Full RTS → ACK → POST → REPOST → DataLoadStatus handshake |
| **POST application** | Writing `p1Plenum` or `CurrentMode` updates the live sim state |

### CLI options

```bash
npx tsx src/armSimulator.ts              # default: TCP port 9000
npx tsx src/armSimulator.ts --port 9001  # custom port
```

## Troubleshooting

| Problem | Solution |
|---------|----------|
| `EADDRINUSE :9000` | Kill old node processes: `Get-Process node \| Stop-Process` (Windows) or `pkill node` (Linux) |
| `EADDRINUSE :3001` | Same as above — old bridge still running |
| Bridge shows "mock mode" | Set `SERIAL_PORT=tcp://localhost:9000` before starting bridge |
| "Cannot GET /iot/health" | Bridge not started yet, or wrong port. Check `http://localhost:3001/iot/health` |
| UI shows no data | Wait ~5s after bridge starts for init handshake + first data push |
