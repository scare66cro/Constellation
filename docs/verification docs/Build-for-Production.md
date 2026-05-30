# Build for Production — Emulation to Hardware Deployment

> ⚠️ **STALE — pre-Nova / pre-LP-AM2434 era (Apr 2026 migration).**
> Talks about `armSimulator.js` and the QEMU ARM simulator deployment
> path. The current production target is rpi5 + LP-AM2434 boards over
> UART2 / Modbus TCP. See
> [`docs/Simulator-to-Production-Transition.md`](../Simulator-to-Production-Transition.md)
> for the current build/flash/deploy commands and
> [`/memories/repo/rpi5-deployment.md`](../../memories/repo/rpi5-deployment.md)
> for the rpi5 service layout.
> Kept for historical reference only.

## Overview

This document explains how the Agristar VFD Modbus fan control system was developed
in an emulation environment and how to build, package, and deploy it to production
Raspberry Pi 5 hardware.

---

## Architecture Findings

### What the Bridge Server Replaces

The production Pi historically runs three backend processes behind lighttpd:

| URL Pattern | Old Handler | Port | Role |
|---|---|---|---|
| `/cgi/*`, `/get/*` | **gellertserverd** (C, FastCGI) | unix socket | Serial UART daemon — owns `/dev/ttyAMA0`, speaks CRC-32 protocol to ARM TM4C129 |
| `/iot/*`, `/gt/*` | **iotclient** (Node.js) | 2035 | REST API — reads data from gellertserverd's CGI cache, serves JSON to UI + mobile app, Azure IoT Hub telemetry |
| Everything else | **uisvelte** (SvelteKit) | 3000 | Web UI server-side rendering |

**The bridge server replaces both gellertserverd and iotclient with a single Node.js process:**

| URL Pattern | New Handler | Port | Role |
|---|---|---|---|
| `/iot/*`, `/gt/*`, `/cgi/*`, `/get/*` | **agristar-bridge** (Node.js) | 3001 | Serial UART + REST API + WebSocket + VFD Modbus |
| Everything else | **uisvelte** (SvelteKit) | 3000 | Web UI (unchanged) |

### Why gellertserverd Must Be Replaced (Not Coexist)

- `/dev/ttyAMA0` is **exclusive** — only one process can hold the serial port
- The bridge opens the serial port directly, speaks the same CRC-32 framed protocol (`^tag=value$CRC!`), and maintains an in-memory data cache
- gellertserverd and the bridge **cannot run simultaneously**

### Why iotclient Must Also Be Replaced

- iotclient has **no serial port access** — it depends entirely on gellertserverd's CGI variables for ARM data
- With gellertserverd stopped, iotclient has no data source
- The bridge provides the same 69+ REST endpoints at `/iot/*` that iotclient served
- The one feature not yet replicated is **Azure IoT Hub MQTT telemetry** (the `hub.settings` cloud connection) — this can be added later

### What Does NOT Break

- **Fan page** (`/level2/fans`) — calls `/iot/fans/*` endpoints served by the bridge's VFD Modbus client
- **Alarm history** — bridge reads ARM alarm data via serial, merges VFD drive alarms in `logDataStore`, serves at `/iot/alarms/*` and pushes via WebSocket
- **All existing pages** — same `/iot/*` endpoints, same JSON response format
- **Mobile app** — hits the same `/iot/*` REST API through lighttpd proxy (port changed from 2035 → 3001, but lighttpd handles the routing transparently)

---

## Emulation vs Production Environment

| Component | Emulation (WSL/QEMU) | Production (RPi5) |
|---|---|---|
| ARM controller | QEMU `qemu-system-arm` emulating TM4C129 | Physical TM4C129 on Mini_IO board |
| Serial port | `tcp://localhost:9000` (TCP socket) | `/dev/ttyAMA0` (physical UART) |
| RS485 responder | `rs485Responder.ts` on port 9002 | Not needed (real hardware) |
| VFD drives | `vfdSimulator.ts` on port 5020 | Real VFD drives via Modbus TCP gateway (port 502) |
| Bridge server | `npx tsx src/index.ts` (dev mode) | `node dist/index.js` (compiled JS) |
| SvelteKit UI | `npx vite dev` on port 5173 | `node index.js` (adapter-node build) on port 3000 |
| lighttpd | Not used (direct port access) | Reverse proxy on port 80 |
| Node.js | WSL Ubuntu 24.04 | Ubuntu 16.04 on RPi5 |

### Key Environment Variables

| Variable | Emulation | Production |
|---|---|---|
| `SERIAL_PORT` | `tcp://localhost:9000` | `/dev/ttyAMA0` |
| `SERIAL_BAUD` | `230400` | `230400` |
| `PORT` | `3001` | `3001` |
| `VFD_HOST` | `127.0.0.1` | Modbus TCP gateway IP on your network |
| `VFD_PORT` | `5020` (simulator) | `502` (standard Modbus TCP) |
| `VFD_MAX_SCAN` | `8` | `8` (or number of drive units) |

---

## Build Process

### Prerequisites

- Node.js (v18+ recommended)
- TypeScript compiler (`npx tsc`)
- WSL with Ubuntu (for building on Windows)

### Step 1: Build the Bridge Server (TypeScript → JavaScript)

```bash
cd ui-svelte/server
npx tsc
```

This compiles all `.ts` files in `src/` to JavaScript in `dist/`.

**Production files included (13 modules):**
- `index.js` — main entry point, serial bridge, VFD fault handling
- `apiRoutes.js` — 69+ REST endpoints
- `dataCache.js` — in-memory ARM data cache
- `serialBridge.js` — serial UART protocol (CRC-32 framing)
- `protocol.js` — message parsing/encoding
- `wsManager.js` — WebSocket server for real-time push
- `logDataStore.js` — alarm/activity log management
- `equipDescTransfer.js` — equipment description transfer to ARM
- `upgradeManager.js` — firmware upgrade orchestration
- `warningTranslator.js` — warning code translation
- `simConfig.js` — configuration file management
- `vfdClient.js` — VFD Modbus TCP client (auto-discovery, polling, control)
- `vfdRegisterMaps.js` — VFD register profiles (ABB ACS310, Phase Tech DXL)

**Files excluded from production (4 modules):**
- `armSimulator.js` — QEMU ARM simulator (emulation only)
- `rs485Responder.js` — RS485 protocol responder (emulation only)
- `rs485Panel.js` — RS485 panel simulator (emulation only)
- `vfdSimulator.js` — VFD drive simulator (emulation only)

### Step 2: Build the SvelteKit UI

The SvelteKit app uses `@sveltejs/adapter-node` which produces a standalone Node.js
server in the `build/` directory.

**On NTFS (Windows/WSL), vite build is extremely slow.** The workaround is to copy
source to a native Linux filesystem, build there, then copy back:

```bash
# Copy source to native ext4 (fast I/O)
rsync -a --exclude=node_modules --exclude=.svelte-kit --exclude=build \
  /mnt/f/Agristar/Agristar/ui-svelte/ /tmp/svbuild/

# Symlink node_modules (avoid copying 500MB)
ln -s /mnt/f/Agristar/Agristar/ui-svelte/node_modules /tmp/svbuild/node_modules

# Build (~2 minutes on ext4 vs 10+ minutes on NTFS)
cd /tmp/svbuild && TMPDIR=/tmp npx vite build

# Copy build output back
cp -r /tmp/svbuild/build /mnt/f/Agristar/Agristar/ui-svelte/
```

The build output contains:
- `index.js` — Node.js entry point (adapter-node)
- `handler.js` — SvelteKit request handler
- `env.js`, `shims.js` — runtime helpers
- `client/` — static assets (images, CSS, JS bundles)
- `server/` — SSR server chunks

### Step 3: Build the display.zip Package

Run the automated packaging script:

```bash
cd ui-svelte/server/deploy
bash build_display_zip.sh
```

This creates `server/deploy/display.zip` (~29MB) containing:

```
display.zip/
  Install.sh                          — Production installer script
  agristar-bridge.service             — systemd unit for bridge server
  uisvelte.service                    — systemd unit for SvelteKit UI
  bridge-server.zip                   — Bridge server dist/ + package.json
  uisvelte.zip                        — SvelteKit build/ + package.json
  languageEnglishEquipDesc.txt        — Equipment name translations
  MinimumVersion                      — Minimum compatible firmware (5.0)
  Gellert/
    scripts/WindowsPrograms.sh        — Kiosk browser launcher
    settings/lighttpd.conf.httpserver  — New lighttpd config (no gellertserverd)
    version/UpdateVersion             — Version number (6)
```

### Step 4: Build the .rpi Upgrade Package

The `.rpi` file is a password-protected ZIP that the software upgrade page expects.
It wraps `Control.zip` (ARM firmware) + `Display.zip` (our display.zip) together.

```bash
cd ui-svelte/server/deploy
python3 build_rpi_package.py --version 5.46
```

This:
1. Extracts `Control.zip` from the existing `AS2_2.0.0.j_5.45.rpi` (preserving ARM firmware)
2. Uses our `display.zip` as `Display.zip`
3. Creates `AS2_2.0.0.j_5.46.rpi` password-protected with `galaxy2008upgrade321software3587`

Requires `7z` or `zip` in PATH. Output: `server/deploy/AS2_2.0.0.j_5.46.rpi` (~28MB).

---

## Deployment to Production

### Option A: Via the Software Upgrade Page (Recommended)

The `.rpi` file is ready to use directly:

1. Copy `AS2_2.0.0.j_5.46.rpi` to a USB drive
2. Insert USB into the Raspberry Pi
3. On the kiosk, navigate to **Level 2 → Version/Upgrade**
4. Select the upgrade file and install

The upgrade system extracts `Control.zip` (ARM firmware) and `Display.zip`
(bridge + UI) from the password-protected archive and runs `Install.sh`.

### Option B: Manual SSH Install

```bash
# Copy to the Pi
scp ui-svelte/server/deploy/display.zip gellert@<PI_IP>:/tmp/

# SSH in and install
ssh gellert@<PI_IP>
cd /tmp
unzip display.zip -d upgrade
cd upgrade
sudo bash Install.sh
```

### Option C: Direct Deploy Script (SSH)

```bash
cd ui-svelte/server/deploy
./deploy.sh gellert@<PI_IP> --vfd-host=<MODBUS_GATEWAY_IP> --vfd-port=502
```

---

## What Install.sh Does

1. **Copies language files** → `/var/www/languageEnglishEquipDesc.txt`
2. **Copies scripts and settings** → `/home/gellert/Gellert/scripts/`, `settings/`
3. **Installs new lighttpd config** — removes `mod_fastcgi` and gellertserverd routes, points ALL API traffic to bridge:3001
4. **Updates system version** → `5.46` (major.minor from existing + UpdateVersion `6`)
5. **Stops all services** — iotclient, gellertserverd (kill), agristar-bridge, uisvelte
6. **Extracts bridge-server.zip** → `/home/gellert/Gellert/bridge-server/`
7. **Runs `npm ci --omit=dev`** to install bridge dependencies (express, ws, serialport, modbus-serial)
8. **Preserves hub.settings** from old iotclient dir (for future Azure IoT support)
9. **Extracts uisvelte.zip** → `/home/gellert/Gellert/ui-svelte/`
10. **Runs `npm ci --omit=dev`** to install SvelteKit runtime dependencies
11. **Installs systemd service files** and reloads daemon
12. **Enables and starts** agristar-bridge + uisvelte
13. **Restarts lighttpd** to load the new proxy config

---

## lighttpd Configuration Change

### Before (v5.44 — gellertserverd + iotclient)

```
/cgi/*, /get/*  →  FastCGI → gellertserverd (unix socket)
/iot/*, /gt/*   →  proxy   → 127.0.0.1:2035 (iotclient)
everything else →  proxy   → 127.0.0.1:3000 (uisvelte)
```

The `mod_fastcgi` module spawns and manages gellertserverd as a FastCGI child process.
iotclient runs independently on port 2035.

### After (v5.45 — bridge only)

```
/iot/*, /gt/*, /cgi/*, /get/*  →  proxy → 127.0.0.1:3001 (agristar-bridge)
everything else                →  proxy → 127.0.0.1:3000 (uisvelte)
```

The `mod_fastcgi` module is removed entirely. All API traffic goes to the bridge via
`mod_proxy`. The bridge owns the serial port directly (not managed by lighttpd).

---

## VFD Modbus Configuration

The bridge server's VFD client is configured via environment variables in
`agristar-bridge.service`:

```ini
Environment=VFD_HOST=127.0.0.1   # Modbus TCP gateway IP
Environment=VFD_PORT=502          # Standard Modbus TCP port
Environment=VFD_MAX_SCAN=8        # Scan unit IDs 1–8 for VFD drives
```

**To change VFD settings after installation:**

```bash
sudo systemctl edit agristar-bridge
```

Add override values:
```ini
[Service]
Environment=VFD_HOST=192.168.1.100
Environment=VFD_PORT=502
Environment=VFD_MAX_SCAN=4
```

Then restart: `sudo systemctl restart agristar-bridge`

**To disable VFD entirely**, set `VFD_HOST` to an empty string.

### Fan Control Modes

The system starts in **legacy mode** (ARM firmware controls fans directly via I/O outputs).
To activate VFD Modbus control:

1. Navigate to **Level 2 → VFD Drives** in the UI
2. Toggle from "Legacy" to "VFD" mode
3. The bridge will now send speed commands to VFD drives via Modbus TCP

In VFD mode, the bridge:
- Auto-discovers VFD drives by scanning Modbus unit IDs
- Identifies drive type via register fingerprinting (ABB ACS310, Phase Tech DXL)
- Polls drive status (speed, current, voltage, faults) every second
- Translates ARM fan speed requests to Modbus speed commands
- Reports VFD faults as system alarms (with 10-second fan fail delay)

---

## Rollback Procedure

To revert to the original gellertserverd + iotclient architecture:

```bash
# Stop the bridge
sudo systemctl stop agristar-bridge
sudo systemctl disable agristar-bridge

# Re-enable iotclient
sudo systemctl enable iotclient
sudo systemctl start iotclient

# Restore the old lighttpd config (backed up during install)
sudo cp /etc/lighttpd/lighttpd.conf.bak.* /etc/lighttpd/lighttpd.conf
sudo systemctl restart lighttpd
```

Or simply install a standard upgrade package (`.rpi` file) which will restore the
original `lighttpd.conf` with gellertserverd FastCGI routes and re-enable iotclient.

---

## Diagnostics

### Check Service Status

```bash
systemctl status agristar-bridge
systemctl status uisvelte
systemctl status lighttpd
```

### View Bridge Logs

```bash
journalctl -u agristar-bridge -f
```

### Test VFD Connectivity

```bash
curl localhost:3001/iot/fans
curl localhost:3001/iot/fans/config
```

### Test Serial Communication

```bash
curl localhost:3001/iot/health
curl localhost:3001/iot/frontmatter
```

### Verify lighttpd Routing

```bash
curl localhost/iot/health          # Should reach bridge:3001
curl localhost/iot/fans            # Should return VFD status
```

---

## File Locations on Production Pi

| Path | Contents |
|---|---|
| `/home/gellert/Gellert/bridge-server/` | Bridge server (dist/, package.json, node_modules/) |
| `/home/gellert/Gellert/bridge-server/dist/` | 13 compiled JavaScript modules |
| `/home/gellert/Gellert/ui-svelte/` | SvelteKit UI build output |
| `/etc/systemd/system/agristar-bridge.service` | Bridge systemd unit |
| `/etc/systemd/system/uisvelte.service` | SvelteKit systemd unit |
| `/etc/lighttpd/lighttpd.conf` | lighttpd proxy config |
| `/var/www/languageEquipDesc.txt` | Equipment name translations |
| `/home/gellert/Gellert/settings/` | System settings |
| `/home/gellert/Gellert/version/SystemVersion` | System version string |

---

## Summary of Changes from Standard Firmware

| Component | Standard Firmware | VFD Fan Control Build |
|---|---|---|
| Serial daemon | gellertserverd (C, FastCGI) | agristar-bridge (Node.js, standalone) |
| REST API | iotclient (Node.js, port 2035) | agristar-bridge (same endpoints, port 3001) |
| lighttpd | FastCGI + two proxy backends | Single proxy backend (bridge:3001) |
| Fan control | ARM firmware I/O only | ARM I/O (legacy) OR VFD Modbus TCP (switchable) |
| VFD support | None | Auto-discovery, polling, alarms, UI page |
| Azure IoT Hub | iotclient MQTT telemetry | Not yet implemented (hub.settings preserved) |
| UI | SvelteKit (no fans page) | SvelteKit + Level 2 VFD Drives page |
| WebSocket | Not available | Real-time push for all data channels |
