# VFD Speed Bouncing — Root Cause Analysis & Fix

**Date:** March 20, 2026  
**System:** Agristar AS2 production panel — RPi5 + ABB ACS380 VFD via FMBT-21 Modbus TCP  
**Symptom:** Fan speed drops to 0% and ramps back up every 3–12 seconds  

---

## 1. Problem Description

After the VFD overlay system was deployed to production, the fan motor exhibited periodic speed drops. The drive display showed the motor decelerating to 0 Hz, then immediately ramping back up to the commanded speed (75%). This cycle repeated every 3–12 seconds, creating visible and audible instability.

## 2. Architecture Overview

The fan speed control chain on the production RPi5:

```
ARM firmware (gellertserverd)
    ↓  CGI response via lighttpd
vfdServer.ts (Node.js, port 3002)
    ↓  Modbus TCP
vfdClient.ts → FMBT-21 adapter (10.1.2.157:502)
    ↓  EFB protocol
ABB ACS380 VFD → Fan motor
```

- **gellertserverd** is the original C daemon that manages the ARM controller. It exposes a CGI endpoint at `GET /get/data?page=main` returning JavaScript variable declarations.
- **vfdServer.ts** polls that CGI every 3 seconds, extracts `MainData[10]` (fan speed percentage), and forwards it to the VFD via Modbus TCP.
- **vfdClient.ts** writes the Control Word + Speed Reference to the ACS380 every 1 second and reads back status registers.

## 3. Investigation Timeline

### 3.1 Initial Theory: Heartbeat Timing

The first hypothesis was that the VFD's FMBT-21 communication timeout was being hit. The Modbus poll cycle performed 6 register reads (~50–200ms each) before writing the heartbeat Control Word, leaving a gap of 300–1200ms between writes.

**Fix applied:** Moved the heartbeat CW + speed reference write to the **top** of each poll cycle (before reads) in `vfdClient.ts`. Also added a concurrency guard (`polling` flag) to prevent overlapping `pollAll()` calls.

**Result:** Improved write consistency but did not resolve the speed bouncing.

### 3.2 Modbus Traffic Capture

Installed `tcpdump` on the Pi and captured 30 seconds of Modbus traffic at the TCP level:

```bash
sudo tcpdump -i any port 502 -w /tmp/modbus_capture.pcap
```

Two separate captures (30 seconds each, after system restarts) showed:

- **FC16 Write** (CW=0x047F, Ref=0x3A98) every **1.000 seconds** — perfectly consistent
- Speed ramp visible: 13250 → 14760 → 14967 → 14997 → 15000 (stable at 75%)
- **No dropped writes, no gaps, no speed dips** in the Modbus traffic itself

**Conclusion:** The Modbus side was clean. The problem was upstream.

### 3.3 FMBT-21 Communication Timeout Check

Accessed the FMBT-21 web interface via digest authentication to read the communication timeout parameters:

```
GET https://10.1.2.157/service/driveparam?5120=R&5121=R&5123=R
```

| Parameter | Value | Meaning |
|-----------|-------|---------|
| 51.20 (Control timeout) | 20 | 2.0 seconds |
| 51.21 (Timeout mode) | 1 | "Any Message" — resets on any Modbus access |
| 51.23 (Address mode) | 0 | Mode 0 (reg = 100 × Group + Index) |

With timeout mode = 1 ("Any Message"), any Modbus packet resets the timer. The maximum observed gap between packets was ~820ms — well within the 2-second timeout.

**Conclusion:** The FMBT-21 timeout was not the cause.

### 3.4 Root Cause Found: CGI Response Truncation

Examined VFD service logs and discovered the CGI poller was alternating between 75% and 0%:

```
17:13:20 → Forwarded fan speed 0% to 1 drives    (STOP)
17:13:23 → Forwarded fan speed 75% to 1 drives   (START)
17:13:26 → Forwarded fan speed 0% to 1 drives    (STOP)
17:13:32 → Forwarded fan speed 75% to 1 drives   (START)
```

Investigation of the CGI endpoint revealed:

1. The `gellertserverd` CGI response (`/get/data?page=main`) **intermittently returns truncated responses** (~1013 bytes) that do not include `MainData` at all.
2. The full response is much larger because it includes a `NetworkMonitor` variable containing IP addresses and metadata for all farms on the network.
3. When the response is truncated, it cuts off in the middle of `NetworkMonitor`, before `MainData` appears.

**The failure chain:**

```
Truncated CGI response (1013 chars, no MainData)
    ↓
parseCgiVars() returns {} for MainData
    ↓
(vars['MainData'] || '').split(',')  →  ['']
    ↓
mainFields[10]  →  undefined
    ↓
rawSpeed = undefined ?? '0'  →  '0'
    ↓
speedPercent = parseInt('0') = 0
    ↓
sendAction('stop')  →  MOTOR DECELERATES
    ↓
3 seconds later: full CGI response returns 75%
    ↓
sendAction('start', 7500)  →  MOTOR ACCELERATES
    ↓
Repeat every 3–12 seconds
```

## 4. Fix

### 4.1 CGI Parse Safety (vfdServer.ts) — Primary Fix

In `pollCgiAndForward()`, added early-return guards when the CGI response is missing data:

```typescript
const mainRaw = vars['MainData'];

// If CGI didn't return a valid MainData, skip this cycle (keep last speed).
// The gellertserverd CGI sometimes returns truncated responses (~1013 chars)
// that don't include MainData — we must NOT treat this as speed=0.
if (!mainRaw) return;

const mainFields = mainRaw.split(',');
const rawSpeed = mainFields[10];
if (rawSpeed === undefined) return;
```

When `MainData` is missing, the poll cycle is **skipped entirely**. The `lastForwardedSpeed` is preserved, and the heartbeat in `vfdClient.ts` continues re-sending the last commanded CW + speed reference to the drive. The motor keeps running at its previous speed until a valid CGI response arrives.

### 4.2 Heartbeat-First Write Order (vfdClient.ts) — Supporting Fix

Moved the CW + speed reference write to execute **before** the register reads in each poll cycle:

```typescript
// Write heartbeat FIRST — ensures the drive gets CW+ref before
// we spend time on reads. Keeps the FMBT-21 comm timeout happy.
const commanded = this.commandedState.get(uid);
if (commanded) {
  await this.client.writeRegisters(profile.cwAddr, [commanded.cw, commanded.ref]);
} else if (profile.heartbeatCw) {
  await this.client.writeRegister(profile.cwAddr, profile.heartbeatCw);
}

// Then read register groups...
for (const spec of profile.reads) { ... }
```

### 4.3 Additional Hardening

- **Concurrency guard:** `private polling = false` flag in `VFDClient` prevents overlapping `pollAll()` calls if a poll cycle takes longer than the 1-second interval.
- **Speed dead-band:** `SPEED_DEADBAND = 2` in `vfdServer.ts` suppresses ±2% fluctuations from PID jitter so only meaningful speed changes are forwarded.

## 5. Verification

After deploying the fix:

- Fan held **stable at 75% for 10+ minutes** without any speed drops
- The ARM controller then performed a legitimate ramp-down (75% → 50% → Off) which was forwarded correctly
- No false 0% events appeared in the logs

Before the fix, the bouncing occurred every 3–12 seconds. After the fix, zero spurious speed changes.

## 6. Why the CGI Truncates

The exact cause of the intermittent truncation in `gellertserverd` was not investigated further. Likely candidates:

- **FastCGI buffer limits:** The `NetworkMonitor` variable includes data for all farms on the network, making the full response significantly larger than the fixed CGI response buffer.
- **lighttpd proxy timing:** lighttpd may close the connection before the daemon finishes writing the full response under load.
- **gellertserverd internal issue:** The C daemon may have a race condition in its CGI response assembly when `NetworkMonitor` data is large.

The skip-cycle approach is robust regardless of the upstream cause — any missing or malformed CGI response is safely ignored.

## 7. Key Takeaways

1. **Never default missing data to zero in a control system.** The original `?? '0'` fallback silently converted "I don't know the speed" into "the speed is zero," which sent a STOP command to a running motor.
2. **Modbus traffic analysis is essential for isolating fault domains.** The tcpdump captures proved the issue was upstream of the Modbus layer, saving significant debugging time.
3. **The FMBT-21 web API is valuable for diagnostics.** Reading drive parameters via `https://10.1.2.157/service/driveparam` confirmed timeout settings without needing physical access to the drive panel.

## 8. Files Changed

| File | Change |
|------|--------|
| `ui-svelte/server/src/vfdServer.ts` | Skip poll cycle when `MainData` missing from CGI; speed dead-band |
| `ui-svelte/server/src/vfdClient.ts` | Heartbeat-first write order; concurrency guard |
