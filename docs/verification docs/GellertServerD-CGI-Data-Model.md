# GellertServerD CGI Data Model — Dirty-Flag Consumption Pattern

**Date:** March 20, 2026  
**System:** Agristar AS2 production panel — RPi5 at 10.1.2.137  
**Binary:** `/home/gellert/Gellert/bin/gellertserverd` (FastCGI daemon)

---

## 1. Summary

`gellertserverd` uses a **dirty-flag, consumed-on-read** data model. Variables returned in a CGI response are marked as consumed and will not appear again until the ARM serial cycle refreshes them. This is an intentional design for the browser's incremental overlay protocol, but it causes data loss when multiple consumers poll the same endpoint.

## 2. Architecture

```
ARM TM4C firmware
    ↓  Serial (/dev/ttyAMA0) — ~1 second cycle
gellertserverd (single-process FastCGI)
    ↓  /tmp/GellertGet.fcgi.socket
lighttpd (port 80)
    ↓  HTTP
Consumers (browser, iotclient, VFD server)
```

### lighttpd FastCGI configuration

```
$HTTP["url"] =~ "^/(cgi|get)/.+$" {
  fastcgi.server = (
    "" => (
      "gellert.fcgi.handler" => (
        "bin-path" => "/home/gellert/Gellert/bin/gellertserverd",
        "socket" => "/tmp/GellertGet.fcgi.socket",
        "check-local" => "disable",
        "max-procs" => 1       ← single process handles ALL requests
      )
    )
  )
}
```

Key setting: `server.stream-response-body = 2` — lighttpd streams the response body as it arrives from the FastCGI backend, rather than buffering the entire response first.

## 3. The Dirty-Flag Data Model

### How it works

1. A **serial thread** inside `gellertserverd` communicates with the ARM controller over UART at ~1 second intervals
2. Each serial exchange updates one or more **CGI variable groups** (e.g. `CGI_MAINDATA`, `CGI_EQUIPSTATUS`, `CGI_MODE`, etc.)
3. Updated groups are flagged as **dirty** in a shared cache
4. When a CGI GET request arrives, gellertserverd builds a response containing **only the dirty variables**, then **clears their dirty flags**
5. The next request gets only variables that were refreshed since the last response

### Proof — rapid vs slow polling

**Rapid-fire test (100ms intervals, sole consumer):**
```
 1 [HAS_MAIN]  1012b   ← first request consumes MainData
 2 [NO_MAIN]    485b   ← immediately empty — nothing refreshed yet
 3 [NO_MAIN]    485b
 ...                    ← 13 empty responses in a row
15 [HAS_MAIN]  6019b   ← ARM serial cycle completed, big dump (73 vars)
16 [NO_MAIN]    485b   ← consumed again
```

**Slow test (3s intervals, sole consumer):**
```
 1 [HAS_MAIN]   635b   ← always has MainData
 2 [HAS_MAIN]  1012b   ← always
 3 [HAS_MAIN]  1012b   ← always
 ...                    ← 10 out of 10 had MainData
```

When polled infrequently enough for the serial cycle to complete between requests, every response contains MainData.

### MainData refresh cycle timing

Measured by consuming MainData and timing how long until it reappears:

| Metric | Value |
|--------|-------|
| Minimum | 0.051s |
| Maximum | 1.016s |
| Average | 0.248s |

MainData refreshes roughly every 1 second from the ARM, but individual groups can refresh faster within the serial cycle.

## 4. Response Size Tiers

Responses vary dramatically in size depending on which groups have been refreshed:

| Size | Variables included | Frequency |
|------|-------------------|-----------|
| 485 bytes | Base only: SessionID, RequestCount, LogCounter, BoardType, pgmLevel, Data_Loaded, ClientIpAdd, LogTotal, AlarmData, NetworkMonitor, GFSFileInfo | ~50% under load |
| 635–1012 bytes | Base + MainData + DateTimeData + Sensors | Common |
| 1138–1242 bytes | Base + MainData + EquipStatusData + DateTimeData + Sensors | Occasional |
| 1331 bytes | + CurrentMode | After mode changes |
| ~6000 bytes | Full dump (73 variables) | Every ~15–30 seconds |

The 485-byte "base only" response is the **empty response** — it means all data groups were already consumed by a previous request. It always includes `AlarmData`, `NetworkMonitor`, and `GFSFileInfo` (these appear to never be marked consumed, or are always re-dirtied).

### Base response structure (485 bytes, no data)

```html
<HTML><HEAD><SCRIPT LANGUAGE="JAVASCRIPT">
var SessionID = "-1";
var RequestCount = "3981";
var LogCounter = "0";
var BoardType = "AS2";
var pgmLevel = "";
var Data_Loaded = "";
var ClientIpAdd = "";
var LogTotal = "";
var AlarmData = "";
var NetworkMonitor = "...,10.1.2.137:80,Gellert Agri-Star,...";
var GFSFileInfo = "";
</SCRIPT></HEAD><BODY></BODY></HTML>
```

Note: `NetworkMonitor` contains farm IP addresses and names from all panels on the network. Its size varies based on network population. `AlarmData` contains the current active alarm text.

## 5. Consumer Competition

Three consumers poll gellertserverd every ~3 seconds simultaneously:

| Consumer | Endpoint | SessionID | Poll interval |
|----------|----------|-----------|---------------|
| Chromium kiosk browser | `/get/GellertData.jsp?SessionID=X` | Obtained via `POST /cgi/PostUIVersion.jsp` | 3 seconds |
| iotclient (Node.js) | `/get/GellertData.jsp?SessionID=Y` | Obtained via `POST /cgi/PostUIVersion.jsp` | 3 seconds |
| VFD server (Node.js) | `/get/data?page=main` | None (SessionID=-1) | 3 seconds |

### SessionID and per-session dirty flags

The browser and iotclient both obtain a SessionID via:
```
POST /cgi/PostUIVersion.jsp  →  "SessionID,pgmLevel"
```

This **may** give them independent dirty-flag tracking, meaning each session gets its own snapshot of which variables have been consumed. Our VFD server uses SessionID=-1 (anonymous), so it shares dirty flags with all other anonymous consumers.

### Observed under 3 consumers

With 3 consumers at 3-second intervals, `GellertData.jsp` shows a repeating 3-beat cycle:
```
FULL  (1010b) → TRUNC (485b) → TRUNC (862b) → FULL → TRUNC → TRUNC ...
```

The `data?page=main` endpoint shows worse results: ~50% of responses are truncated (485b, no MainData).

### RequestCount evidence

In the slow test, RequestCount incremented by 2 between our samples (4186→4188→4190), confirming exactly one other consumer is interleaved between our requests.

## 6. The Browser's Intended Pattern

The browser JavaScript in `/var/www/jsAjax.js` was **designed for** this incremental model:

```javascript
this.processData = function(text, url, page) {
    // Parse response and extract var declarations
    var processing = text.substring(...).split(";");
    for (var i = 0; i < processing.length - 1; i++) {
        processVar = processing[i].split("=")[0];  // variable name
        processValue = processing[i].split("=")[1]; // value
        eval(processVar + "=" + "processValue");    // merge into global scope
    }
    // Schedule next poll in 3 seconds
    if (url == "get/GellertData.jsp" && top.Upgrading == "false") {
        CheckLevel();
        setTimeout(func, 3000);
    }
};
```

The browser uses `eval()` to merge each partial response into its `top` scope. If MainData is missing from a response, the browser simply retains its previous value. Variables accumulate over time — the browser never loses data it already has.

## 7. Known CGI Variable Groups

Extracted from binary strings — these are the serial query tags / CGI variable mappings:

```
CGI_MAINDATA          → var MainData
CGI_EQUIPSTATUS       → var EquipStatusData
CGI_MODE              → var CurrentMode
CGI_DATETIME          → var DateTimeData
CGI_PGMDATA           → var PgmData
CGI_DAILYFAN          → var DailyFanRun
CGI_TOTALFAN          → var TotalFanRun
CGI_OUTSIDE           → var OutsideAirData
CGI_RUNTIMES          → var RunTimesData
CGI_FREQCTRL          → var FreqCtrlData
CGI_RAMPRATE          → var RampRateData
CGI_HUMIDCTRL         → var HumidCtrlData
CGI_CO2PURGE          → var Co2PurgeData
CGI_MISCDATA          → var MiscData
CGI_BASICSETUP        → var P2BasicSetupData
CGI_FAILURES1         → var P2FailuresData
CGI_FAILURES2         → var P2Failures2Data
CGI_FRESHAIR          → var P2FreshAirData
CGI_REFRIG            → var P2RefrigerationData
CGI_CLIMACELL         → var P2ClimacellData
CGI_TEMPDEV           → var PlenTempDevData
CGI_SERVICE           → var P2ServiceData
CGI_USERLOG           → var UserLogSettings
CGI_MYDISPLAY         → var MyDisplay
CGI_IPADD             → var LocalIpAdd
CGI_IPMASK            → var LocalIpMask
CGI_IPGATEWAY         → var LocalIpGateway
CGI_IPDMODE           → var LocalIpMode
CGI_HTTPPORT          → var HttpPort
CGI_LTXVERSION        → var LtxVersion
CGI_NETMONITOR        → var NetworkMonitor
CGI_USERACCTS         → var UserAccounts
CGI_ALERTSETUP        → var AlertSetupData
CGI_GRAPHFAVS         → var GraphFavorites
CGI_FANBOOST          → var FanBoostData
CGI_AIRCURE           → var AirCureData
CGI_MAC               → var MAC
CGI_SETTINGS          → var RestoreSettings
CGI_CLIMACELLTIMES    → var ClimacellTimesData
CGI_HUMIDMODES        → var HumidModes
CGI_PWMCHANNELS       → var P2PwmChannelData
CGI_LOADMONITOR       → var LoadMonitorData
CGI_AVAILABLEIO       → var AvailableIoData
CGI_OUTPUTCONFIG      → var OutputConfigData
CGI_INPUTCONFIG       → var InputConfigData
CGI_AUXPROGRAM        → var AuxProgramData
CGI_AUXSWITCHES       → var AuxSwitchesData
CGI_PID0              → var P2PidDoor
CGI_PID1              → var P2PidRefrig
CGI_TEMPSENSOR        → var PileTempsLabels
CGI_HUMIDSENSOR       → var PileHumidsLabels
CGI_VAR_NODE          → var P2NodeSetupData
```

## 8. MainData Field Layout

`var MainData = "54.3,dis,48.2,0,19,0,42.6,19,47.8,4124.0,75,0,1,--,--,48.0,60.6,63,0,--,--,--,--,--";`

| Index | Sample | Description |
|-------|--------|-------------|
| 0 | 54.3 | Plenum temperature setpoint |
| 1 | dis | Plenum humidity setpoint |
| 2 | 48.2 | Outside temperature |
| 3 | 0 | (unknown) |
| 4 | 19 | Outside humidity |
| 5 | 0 | (unknown) |
| 6 | 42.6 | Cooling available temperature |
| 7 | 19 | Return humidity |
| 8 | 47.8 | Return temperature |
| 9 | 4124.0 | CO2 level |
| **10** | **75** | **Fan speed (% or "Off" or "--")** |
| 11 | 0 | Cool output |
| 12 | 1 | Refrig output |
| 13–23 | -- | Extended fields |

## 9. Implications for System Design

### Problem: Any non-browser consumer that needs reliable data must handle missing variables

The dirty-flag consumption model means:
- **You cannot assume any variable will be present in any given response**
- **Polling faster than the serial refresh cycle wastes requests** — you just get empty responses
- **Adding more consumers degrades reliability for all existing consumers** — the ARM only refreshes data once per second, but each consumer that reads it clears the flag

### Recommendations for consuming CGI data

1. **Obtain a SessionID** — POST to `/cgi/PostUIVersion.jsp` with `version=2.00&UniqueID=<timestamp>`. This may give the consumer its own per-session dirty-flag tracking, preventing competition with other consumers.

2. **Cache variables locally** — Adopt the browser's overlay model: maintain a local variable cache and merge each partial response into it. Never treat a missing variable as "value is 0" — treat it as "no update available."

3. **Use `/get/GellertData.jsp`** — This is the same endpoint the browser and iotclient use. The `/get/data?page=main` endpoint returns identical data but may have additional routing overhead.

4. **Poll at the right frequency** — 3 seconds matches the browser's polling rate and is long enough for multiple serial refreshes (1 second each) to accumulate data.

5. **Never default missing data to zero in a control system** — The original VFD speed bouncing bug was caused by treating a missing `MainData[10]` as "speed = 0", which sent a STOP command to a running motor every time the dirty flag had already been consumed by another client.
