# Agristar AS2 Upgrade Package — Complete Analysis

> **Date:** March 2026  
> **Source:** Extracted AS2 upgrade file with preserved folder hierarchy  
> **Location in repo:** `as2 upgrade file/`

---

## 1. Package Structure

The upgrade package is delivered as two ZIP archives:

```
as2 upgrade file/
├── control.zip/                         ← ARM TM4C129 firmware + serial daemon
│   ├── AS2.sre                          (479,292 bytes) — Motorola S-record firmware
│   ├── gellertserverd                   (687,616 bytes) — RPi5 serial daemon binary
│   └── settings/
│       └── lighttpd.conf.httpserver     — Production lighttpd v5.44 config
│
└── display.zip/                         ← RPi5 display/UI upgrade
    ├── Install.sh                       (256 lines) — Master installer script
    ├── MinimumVersion                   Contains "5.0"
    ├── Display.tar.gz                   — Legacy display package (not analyzed)
    ├── uisvelte.zip                     — SvelteKit UI application
    ├── uisvelte.service                 — systemd unit for SvelteKit UI
    ├── iotclient.zip                    — IoT client application
    ├── iotclient.service                — systemd unit for IoT client
    ├── languageEnglishEquipDesc.txt     — Equipment description text
    └── Gellert/
        ├── bin/                         (empty — binaries from control.zip)
        ├── scripts/
        │   └── WindowsPrograms.sh       — Chromium kiosk launcher (wayland/wlroots)
        ├── settings/
        │   └── lighttpd.conf.httpserver — Identical to control.zip version
        └── version/
            └── UpdateVersion            Contains "5"
```

---

## 2. Firmware Analysis: AS2.sre

### 2.1 Format

The firmware is delivered in **Motorola S-record** format (`.sre`), a text-based encoding widely used for microcontroller flash programming.

| Property | Value |
|---|---|
| **Format** | Motorola S-record (S0/S2/S5/S8) |
| **S0 Header** | `"Gellert AS2"` |
| **Record type** | S2 — 24-bit (3-byte) address records |
| **Total S2 records** | 6,144 |
| **Total lines** | 6,147 |
| **File size** | 479,292 bytes (text) |
| **S8 Start address** | `0x00000000` |

### 2.2 Converted Binary

Using `qemu-tm4c/convert_sre.py` to decode:

| Property | Value |
|---|---|
| **Address range** | `0x00000000` → `0x00030000` |
| **Binary size** | 196,608 bytes (192 KB) |
| **MD5 (raw binary)** | `ee82dbf909bb659a0f06ba61fdab235f` |
| **Version string** | **3.20** |
| **Board version** | **2.00** |

### 2.3 Comparison with Current firmware.bin (v1.07)

| Property | AS2.sre (upgrade) | firmware.bin (current) |
|---|---|---|
| **Version** | 3.20 | 1.07 |
| **Board version** | 2.00 | *(no board version string found)* |
| **Binary size** | 196,608 bytes (192 KB) | 161,020 bytes (~157 KB) |
| **MD5** | `ee82dbf909bb659a0f06ba61fdab235f` | `827153CC3B522A9B78FD51FFB3F45FA5` |
| **Vector table** | Completely different | Completely different |

**Result:** The two firmwares are **completely different code bases** — 158,001 byte differences across the first 161,020 bytes. Different vector tables at offset `0x000000`. This is a major version jump, not a patch.

### 2.4 Version Compatibility

- `gellertserverd` sends `Initialize=LTX&Version=2.00` as its init handshake
- AS2.sre firmware v3.20 contains board version string **"2.00"** — this **matches**
- Current firmware.bin v1.07 does not contain a matching "2.00" board version — a version mismatch alarm (`WARN_VERSION`) would fire even if serial communication were established

This means the upgrade firmware (v3.20) is the **correct** firmware to pair with the current `gellertserverd`.

---

## 3. gellertserverd Analysis

### 3.1 Binary Details

| Property | Value |
|---|---|
| **File size** | 687,616 bytes |
| **MD5** | `68590973395dca74a9f1804488fcfecd` |
| **Architecture** | ARM aarch64 (RPi5) |

### 3.2 Key Finding: Upgrade vs Running Version — IDENTICAL

The `gellertserverd` in the upgrade package is **byte-for-byte identical** to the one currently running inside the RPi5 QEMU guest:

```
Upgrade:  MD5 = 68590973395dca74a9f1804488fcfecd  (687,616 bytes)
RPi5:     MD5 = 68590973395dca74a9f1804488fcfecd  (687,616 bytes)
```

There is no newer version of `gellertserverd` available in this upgrade package.

### 3.3 gellertserverd Internals (from strings/source analysis)

| Feature | Detail |
|---|---|
| **Config file** | `/home/gellert/Gellert/settings/serial.conf` (first line = device path) |
| **Baud rate** | 230,400 bps, 8N1 |
| **Init handshake** | Sends `Initialize=LTX&Version=2.00` every 2 seconds until ARM responds |
| **Watchdog** | 30s no response → re-init; 600s (10 min) → `WARN_ARMCOMM` alarm |
| **Reboot logic** | If serial port fails to open 10 consecutive times → system reboot |
| **Protocol** | `^tag=value$CRC!` framing with CRC-32 |
| **FastCGI** | Runs as a FastCGI handler under lighttpd |
| **Version check** | ARM must respond with matching version in `ProcessMsgInit()` or `WARN_VERSION` alarm fires |

### 3.4 "System Controller Not Responding" Alarm Root Cause

The alarm on the RPi5 QEMU at `:8080` is **real and expected** because:

1. **No `serial.conf` exists** on the RPi5 guest → `gellertserverd` can't find a serial device path
2. Even if serial were connected, firmware v1.07 would cause a **version mismatch** (gellertserverd expects v2.00, firmware reports v1.07)
3. After 10 minutes (600 seconds) of no ARM communication, `ArmMessagingWatchDog()` sets `WARN_ARMCOMM`

---

## 4. lighttpd Configuration (v5.44)

The production lighttpd config (identical in both `control.zip/settings/` and `display.zip/Gellert/settings/`) reveals the complete production routing architecture:

### 4.1 URL Routing Rules

```
URL Pattern         → Backend                          Purpose
─────────────────────────────────────────────────────────────────
/cgi/*  /get/*      → FastCGI gellertserverd            Serial daemon (ARM comms)
                      bin-path: /home/gellert/Gellert/bin/gellertserverd
                      socket: /tmp/GellertGet.fcgi.socket

/iot/*  /gt/*       → proxy 127.0.0.1:2035              IoT client (Node.js)
                      + X-Forwarded-For, X-Forwarded-Cookie headers

Everything else     → proxy 127.0.0.1:3000              SvelteKit UI
                      + X-Forwarded-For header
```

### 4.2 Key Configuration Values

| Setting | Value |
|---|---|
| **Config version** | v5.44 |
| **Listening port** | 80 (preserved from existing config during upgrade) |
| **Bind address** | `0.0.0.0` (all interfaces) |
| **Document root** | `/var/www` |
| **Keep-alive idle** | 3600 seconds |
| **Read idle** | 3600 seconds |
| **Stream response body** | 2 (chunked) |
| **Compression** | deflate on JS, CSS, HTML, plain text |

### 4.3 Architecture Implications

The production system runs **three separate backend services** behind lighttpd:

1. **`gellertserverd`** (FastCGI) — handles `/cgi/` and `/get/` requests for direct ARM serial communication
2. **IoT client** (Node.js, port 2035) — handles `/iot/` and `/gt/` requests for cloud/remote monitoring
3. **SvelteKit UI** (Node.js, port 3000) — serves the main web UI for all other routes

For our QEMU simulation environment, the bridge server on port 3001 replaces `gellertserverd`, and the lighttpd proxy rules need to be adjusted accordingly.

---

## 5. Install.sh — Master Installer

The 256-line installer (`display.zip/Install.sh`) performs the complete RPi5 upgrade:

### 5.1 Installation Steps (in order)

1. **Copy `languageEnglishEquipDesc.txt`** → `/var/www/` (also as `languageEquipDesc.txt`)
2. **Copy bin files** from `Gellert/bin/` → `/home/gellert/Gellert/bin/`
3. **Copy scripts** from `Gellert/scripts/` → `/home/gellert/Gellert/scripts/` (chown gellert, chmod +x)
4. **Copy settings** from `Gellert/settings/` → `/home/gellert/Gellert/settings/`
5. **Backup lighttpd config** → `lighttpd.conf.original`
6. **Update lighttpd.conf** — copies new config but **preserves the existing port number**
7. **Update SystemVersion** — reads `UpdateVersion` ("5"), combines with existing `majorMinor` from `SystemVersion`
8. **Stop & disable** `iotclient.service` and `uisvelte.service`
9. **Delete & recreate** `/home/gellert/Gellert/ui-svelte/`
10. **Extract `uisvelte.zip`** → `/home/gellert/Gellert/ui-svelte/` (chown gellert)
11. **Back up `hub.settings`** from existing iotclient
12. **Delete & recreate** `/home/gellert/Gellert/iotclient/`
13. **Extract `iotclient.zip`** → `/home/gellert/Gellert/iotclient/` (chown gellert)
14. **Restore `hub.settings`** (or create empty if missing)
15. **Install `iotclient.service`** — generates `AUTH_SECRET` via `openssl rand -base64 32` and injects into service file
16. **Install `uisvelte.service`**
17. **Enable & start** both services

### 5.2 Key Design Decisions

- **Port preservation:** The installer reads the existing port from `/etc/lighttpd/lighttpd.conf` and writes it into the new config. If no port is found, it falls back to `DefaultLantronixIP` or port 80.
- **Auth secret generation:** `openssl rand -base64 32 | tr -d '/'` is injected into `iotclient.service` by replacing `[secret here]` placeholder.
- **Rolling upgrade:** The installer backs up `hub.settings` before deleting the iotclient directory, ensuring persistent IoT hub configuration survives upgrades.
- **No firmware flashing:** Install.sh only handles the display/UI side. The `AS2.sre` firmware upload to the TM4C129 is handled separately by `GellertProgResponder` via the admin UI.

---

## 6. WindowsPrograms.sh — Kiosk Launcher

This script runs as the UI entrypoint on the RPi5's display output:

| Feature | Detail |
|---|---|
| **Display server** | Wayland (wlroots), with X11 fallback via `xrandr` |
| **Browser** | Chromium in `--kiosk` mode, `--incognito`, `--no-first-run` |
| **URL** | `http://127.0.0.1[:port]` (reads port from lighttpd.conf) |
| **Screen timeout** | 1800 seconds (30 min) via `swayidle` + `wlopm` |
| **Touch calibration** | Auto-calibrates on first boot |
| **Board detection** | Adjusts resolution: RPi5 → 1920x1080, generic → 800x600, other → 640x480 |
| **Reboot behavior** | In normal mode, Chromium exit triggers `reboot` |

---

## 7. Version Numbering Scheme

| File / Property | Value | Meaning |
|---|---|---|
| `MinimumVersion` | `5.0` | Minimum RPi5 system version required to install this upgrade |
| `UpdateVersion` | `5` | Appended to `majorMinor` in `SystemVersion` → e.g., `2.05` |
| AS2.sre firmware | `3.20` | TM4C129 ARM firmware version |
| AS2.sre board version | `2.00` | Expected by gellertserverd's init handshake |
| Current firmware.bin | `1.07` | Currently loaded in QEMU ARM QEMU |
| lighttpd.conf | `v5.44` | Config file version |

---

## 8. Service Architecture (Post-Upgrade)

After a successful upgrade, the RPi5 runs this service stack:

```
┌─────────────────────────────────────────────────────────────┐
│  lighttpd (:80)                                             │
│  ┌──────────────┐  ┌──────────────┐  ┌───────────────────┐ │
│  │ /cgi/* /get/* │  │ /iot/* /gt/* │  │ Everything else   │ │
│  │  FastCGI      │  │  proxy:2035  │  │  proxy:3000       │ │
│  │  gellertserverd│  │  iotclient  │  │  uisvelte         │ │
│  └──────┬───────┘  └──────┬───────┘  └──────┬────────────┘ │
│         │                 │                  │              │
│    /dev/ttyAMA0      hub.settings       SvelteKit app      │
│    (serial → ARM)    (cloud config)     (Node.js SSR)      │
│         │                                                   │
│         ▼                                                   │
│  ┌──────────────┐                                           │
│  │ TM4C129 ARM  │ ← AS2.sre firmware v3.20                 │
│  │ (RS485 bus)  │                                           │
│  └──────────────┘                                           │
│                                                             │
│  Other RPi5 services:                                       │
│  • GellertFileSystem.out (:9209) — history/log I/O          │
│  • GellertQueryResponder (:9210) — UDP network discovery    │
│  • GellertEmailResponder          — alert email sending     │
│  • GellertProgResponder           — firmware upgrades       │
│  • MonitorFtp.sh                  — FTP monitoring          │
│  • NightlyRestart.sh              — scheduled restart       │
└─────────────────────────────────────────────────────────────┘
```

---

## 9. Implications for QEMU Simulation

### 9.1 Firmware Mismatch

Our QEMU ARM runs firmware v1.07, but the production system expects v3.20. The upgrade firmware (AS2.sre) contains board version "2.00" which matches what `gellertserverd` expects. Our v1.07 firmware does **not** contain this board version — even if serial communication were established, it would trigger a `WARN_VERSION` alarm.

**Options:**
1. Convert AS2.sre → binary and boot it in QEMU ARM (untested, may require new peripheral models)
2. Keep v1.07 and suppress the version alarm (current approach — gellertserverd is disabled)

### 9.2 Bridge Server Port Mapping

Production routes `/iot/*` to port **2035** (iotclient). Our bridge server runs on port **3001**. The QEMU lighttpd config must be patched to route `/iot/*` → `3001` instead of `2035`.

### 9.3 SvelteKit UI Port

Production UI runs on port **3000**. Our Vite dev server runs on port **5173/5174**. For production-like testing inside the RPi5 QEMU, the SvelteKit app should be built and served on port 3000.

---

## 10. File Hashes Reference

| File | Size | MD5 |
|---|---|---|
| `control.zip/AS2.sre` | 479,292 bytes | `1A78E755056AF56F1EB74F14F3594739` |
| AS2.sre → converted binary | 196,608 bytes | `ee82dbf909bb659a0f06ba61fdab235f` |
| `control.zip/gellertserverd` | 687,616 bytes | `68590973395dca74a9f1804488fcfecd` |
| RPi5 running gellertserverd | 687,616 bytes | `68590973395dca74a9f1804488fcfecd` |
| `Mini_IO/build/firmware.bin` | 161,020 bytes | `827153CC3B522A9B78FD51FFB3F45FA5` |

---

## 11. Tools Created During Analysis

- **`qemu-tm4c/convert_sre.py`** — Python script that parses Motorola S-record files, converts to raw binary, extracts version strings, and performs byte-by-byte comparison with existing firmware. Usage:
  ```bash
  python3 convert_sre.py [sre_file] [firmware_file]
  ```
