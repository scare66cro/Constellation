# Log / History Data Flow Analysis

## Complete Data-Flow Report: ARM SD Card → Server → UI

> **Research scope:** How log/history data (user logs, system/activity logs, load logs,
> PID logs, warning logs) flows from the ARM firmware's SD card through the server
> layers to the browser UI, and what the bridge server currently handles vs. what is
> missing.

---

## 1. Complete Log-Retrieval Data Flow (end-to-end)

The log data lifecycle has **two distinct paths**:

| Path | Purpose | Trigger |
|------|---------|---------|
| **A — In-browser viewing** (graph / record table) | User views historical data in the UI | Selecting date range and clicking "View" on a graph or record page |
| **B — USB download** | Exporting log data as CSV to a USB drive attached to a display | Selecting date range and clicking "Download" on a USB page |

### Path A — In-Browser Viewing (graph / record table)

```
┌──────────┐   POST form        ┌────────────┐  serial ^tag=val$CRC!  ┌───────────┐
│  Browser  │ ─────────────────► │   Server   │ ─────────────────────► │ ARM TM4C  │
│  (UI)     │ cgi/PostGellert.jsp│(gellertserverd)                     │ Firmware   │
└──────────┘                     └────────────┘                        └───────────┘
     │                                 │                                    │
     │  AJAX POST                      │  ^tag=value$CRC!                   │ reads SD card
     │  cgi/PostSave.jsp               │  (RTS → ACK handshake)            │
     │  ↓ polls for ArmReply           │                                    │
     │                                 │◄─── MultiMsg=SID,LogData,Key,...   │
     │                           StoreCgiVar() stores each sub-message      │
     │                           in LogData[] linked lists                  │
     │                                 │◄─── Data=EOR (end-of-record)       │
     │                           LogMsgCounter++ (progress tracking)        │
     │                                 │◄─── MultiEnd=LogData               │
     │                           ArmReply="true" → PostSave returns         │
     │                                 │                                    │
     │◄── PostSave response ───────────│                                    │
     │    (Data_Loaded="true")         │                                    │
     │                                 │                                    │
     │  GET GellertLogData.jsp ───────►│                                    │
     │                           GetLogData() writes LogData[]              │
     │                           as JavaScript variables to HTML            │
     │◄── <script>var Key="...";       │                                    │
     │    var Data="..."; etc.         │                                    │
     │                                 │                                    │
     │  processData() evals JS vars   │                                    │
     │  → top.Data, top.Key, etc.     │                                    │
     │  → runGraph() / drawRecords()  │                                    │
```

**Step-by-step:**

1. **UI submits form** — The history page (e.g. `grPlottedGraph.htm`) has a `<form action="cgi/PostGellert.jsp">` containing hidden fields: `SessionID`, date range, selected graph items, etc. The form is submitted via `document.forms[0].submit()` (standard HTML POST).

2. **UI initiates save-wait** — Immediately after form submit, `startData()` (in `jsHistoryCodeLib.js:336`) calls:
   ```js
   (parent.AjaxSW).request('postsp', 'cgi/PostSave.jsp',
       "SavePage=historyPage&SessionID=" + top.SessionID, form);
   ```
   This AJAX POST to `PostSave.jsp` blocks server-side in `PostSave()` (CallBacks.c:1778), polling `Session[SessionID].ArmReply` for up to 25 seconds.

3. **Server sends POST to ARM** — `ProcessPost()` (CallBacks.c:2296) packages the form body into `Message.TxBuffer` and sets `Message.State = MSG_STATE_RTS` to initiate the serial RTS/ACK handshake with the ARM firmware.

4. **ARM reads SD card** — The ARM firmware receives the request, reads the requested history data from the SD card, and begins sending it back via the MultiMsg protocol.

5. **ARM sends MultiMsg sequence** — The ARM sends:
   ```
   ^MultiMsg=<SessionID>,LogData,Key,Dates,TimeStamps,Data,Warnings$CRC!
   ```
   Followed by individual sub-messages:
   ```
   ^Key=header1,header2,...$CRC!
   ^Dates=date1,date2,...$CRC!
   ^TimeStamps=ts1,ts2,...$CRC!
   ^Data=value1,value2,...$CRC!      (may repeat; "EOR" marks end-of-record)
   ^Warnings=warn1,warn2,...$CRC!
   ```
   Terminated by:
   ```
   ^MultiEnd=LogData$CRC!
   ```

6. **Server accumulates data** — `StoreCgiVar()` (CGI.c:1243) processes each incoming sub-message:
   - On `MultiMsg` tag: parses the header, creates `MULTI_MSG_DATA` linked list entries in `LogData[MAX_GRAPH_ITEMS+1]` (up to 11 graph items).
   - On each sub-tag (`Key`, `Dates`, `Data`, etc.): `MultiMsgStoreVar()` appends data to the corresponding linked list.
   - On `Data=EOR`: increments `Session[sid].LogMsgCounter` and calls `SendRecordToFile()` (if USB output active).
   - On `MultiEnd`: sets `DataLoadStatus` → `ArmReply="true"` via `ProcessDataLoad()`.
   - Overflow protection: if `malloc()` fails, sets `ArmReply="overflow"` and kills the message.

7. **PostSave unblocks** — When `ArmReply` changes from `"waitLTX"`, `PostSave()` returns the value (`"true"`, `"none"`, `"overflow"`, etc.) as HTTP response text.

8. **UI processes PostSave response** — In `jsAjax.js`, `processPostResponse()` for `cgi/PostSave.jsp`:
   - Calls `processData()` which `eval()`s embedded JS variables, setting `top.Data_Loaded`.
   - If `Data_Loaded == "true"` and it's a history page (not USB): calls `requestData("get/GellertLogData.jsp", this.pgName)`.
   - If `"none"`, `"busy"`, `"overflow"`: shows error message.

9. **UI fetches log data** — `requestData()` (jsDataExcLib.js:2716) sends:
   ```
   GET get/GellertLogData.jsp?UniqueID=<millis>&SessionID=<sid>
   ```
   This calls `GetLogData()` (CallBacks.c:398):
   ```c
   HttpWriteData(pClient, "<HTML><HEAD><SCRIPT LANGUAGE=\"JAVASCRIPT\">\r\n");
   GetMultiMsgData(pClient, LogData, 0, GT_HTML);
   HttpWriteData(pClient, "</SCRIPT></HEAD><BODY></BODY></HTML>\r\n");
   MultiMsgFree(LogData);
   Session[sid].LogMsgCounter = 0;
   ```
   `GetMultiMsgData()` (MultiMsg.c) traverses each LogData linked list and writes JavaScript variable assignments: `var Key="..."; var Data="...";` etc.

10. **UI renders data** — `processData()` in jsAjax.js parses the response HTML, extracting `var X="Y"` statements and eval-ing them into the top frame. For graphs, it then calls `endHistoryProcess()` → `runGraph()` → `formatData()` (jsGraphCodeLib.js) which reads `parent.Data`, `parent.Key`, `parent.Dates`, `parent.TimeStamps`, splits by comma, and draws on canvas. For records, it calls `drawRecords()` (jsRecordCodeLib.js) which builds an HTML table.

### Progress Counter

During data loading, the UI shows a progress counter:
- `SavePopUp()` (jsCodeLib.js) creates a popup with `logCounter` / `logTotal` elements.
- `top.logCounterInt = setInterval("getLogCounterData()", 1000)` polls every second.
- `getLogCounterData()` (jsDataExcLib.js:543) reads `top.LogCounter` (from periodic `GellertData.jsp` polling) and displays it against `top.LogTotal`.
- Server-side, `BuildCgiVars()` writes `LogCounter` from `Session[sid].LogMsgCounter` into the regular CGI data response.

### Path B — USB Download

```
┌──────────┐  POST form          ┌────────────┐  GFS protocol (TCP:9209) ┌─────────────────────┐
│  Browser  │ ──────────────────► │   Server   │ ──────────────────────► │ GellertFileSystem.out │
│  (UI)     │ PostOpenOutputFile  │            │  GFS_OPEN_LOG           │  (display daemon)     │
└──────────┘                     └────────────┘                          └─────────────────────┘
     │                                 │                                         │
     │                                 │  GFSopen(LOG, displayIP)                │ opens CSV file
     │                                 │                                         │ on USB storage
     │  POST cgi/PostGellert.jsp ────► │                                         │
     │  + PostSave.jsp                 │  ^tag=val$CRC! ──► ARM                  │
     │                                 │  ARM sends MultiMsg LogData             │
     │                                 │  on each Data=EOR:                      │
     │                                 │    SendRecordToFile() → GFSwrite()      │
     │                                 │         ───► TCP:9209 ────────────────► │ writes record
     │                                 │  ARM sends LogToFile=EOF:               │
     │                                 │    GFSclose() → TCP:9209 ──────────────► │ closes file
     │                                 │  PostSave unblocks                      │
     │◄── response ────────────────────│                                         │
```

**Step-by-step:**

1. **UI submits to PostOpenOutputFile.jsp** — USB download pages (`hsUserLogToUSB.htm`, `hsLoadLogToUSB.htm`, etc.) have `<form action="cgi/PostOpenOutputFile.jsp">` with fields: `selDisplay` (target display IP), `startDate`, `endDate`, `fileName`.

2. **Server opens file via GFS** — `PostOpenOutputFile()` (CallBacks.c:1646) calls `GFSopen(&OutputFile[sid], GFS_OPEN_LOG | GFS_CREATE)`. This sends a binary `GFS_Struct` over TCP to GellertFileSystem.out on port 9209 of the target display.

3. **Regular log POST follows** — The UI then submits the same date range to `cgi/PostGellert.jsp` and `cgi/PostSave.jsp`, initiating the same ARM request as Path A.

4. **Server writes records to file** — As each `Data=EOR` arrives, `SendRecordToFile()` (MultiMsg.c) traverses the LogData linked list, concatenates the record into a comma-separated string, and calls `GFSwrite()` which sends 256-byte chunks to GellertFileSystem.out via TCP.

5. **ARM signals completion** — ARM sends `^LogToFile=EOF$CRC!`. `ProcessLogToFile()` (ArmMessaging.c) calls `GFSclose()` to send a close command to GellertFileSystem.out. The file is now complete on the USB drive.

---

## 2. Serial Protocol Tags/Commands for Log Retrieval

### Tags Sent TO the ARM (from server, as form field data)

| ARM Tag (msgTag) | CGI Index | JS Variable Name | Purpose |
|:----|:----|:----|:----|
| `recInterval` | `CGI_USERLOG` | `UserLogSettings` | User log configuration (intervals, items) |
| `LogTotal` | `CGI_LOGTOTAL` | `LogTotal` | Total number of log records available |
| `GraphFavorites` | `CGI_GRAPHFAVS` | `GraphFavorites` | Saved graph item selections |
| `syslogRec` | `CGI_SYSLOGREC` | `syslogRec` | System/activity log record request |
| `syslogEquip` | `CGI_SYSLOGEQUIP` | `syslogEquip` | System log equipment filter |
| `syslogRemote` | `CGI_SYSLOGREMOTE` | `syslogRemote` | System log remote filter |
| `pidLog` | `CGI_PIDLOG` | `pidLog` | PID log data request |
| `bay1Label` | `CGI_LOADMONITOR` | `LoadMonitorData` | Load monitoring log data |

### Tags Received FROM the ARM

| Tag | Values | Handler | Purpose |
|:----|:-------|:--------|:--------|
| `MultiMsg` | `<SessionID>,LogData,Key,Dates,TimeStamps,Data,Warnings` | `StoreCgiVar()` | Start of multi-message log data sequence |
| `Key` | Comma-separated column headers | `MultiMsgStoreVar()` | Graph/record column names |
| `Dates` | Comma-separated date values | `MultiMsgStoreVar()` | Date axis for graph data |
| `TimeStamps` | Comma-separated timestamps | `MultiMsgStoreVar()` | Time axis for graph data |
| `Data` | CSV values, or `EOR` | `MultiMsgStoreVar()` / `SendRecordToFile()` | Actual log values; `EOR` = end of one record |
| `Warnings` | CSV warning indicators | `MultiMsgStoreVar()` | Warning flags per data point |
| `LogAlarms` | Expanded alarm data | `StoreCgiVar()` (expanded flag) | Alarm details within log data |
| `LogSearch` | Search results | `StoreCgiVar()` | Filtered log search results |
| `MultiEnd` | `LogData` | `processMultiMsgEnd()` | End of multi-message sequence |
| `DataLoadStatus` | `<status>,<SessionID>` | `ProcessDataLoad()` | ARM's reply: success/fail/busy |
| `LogToFile` | `EOR` or `EOF` | `ProcessLogToFile()` | USB file write control: `EOR` = write record, `EOF` = close file |

### Serial Message Format

```
^tag=value$CRC!
```
- `^` = start delimiter
- `tag=value` = payload (URL-encoded for POST data)
- `$` = end-of-payload
- `CRC` = 2-character checksum
- `!` = end delimiter

### RTS/ACK Handshake

The server uses a state machine (`MSG_STATE_RTS` → `MSG_STATE_WAITING`) for reliable delivery:
1. Server sends message with `RTS` prefix
2. ARM replies with `ACK` to confirm receipt
3. Server transitions to `WAITING` state for response data
4. For MultiMsg: state becomes `MULTIMSG` until `MultiEnd` received

---

## 3. HTTP Endpoints the UI Calls for Log Data

### GET Endpoints

| Endpoint | Handler | Purpose |
|:---------|:--------|:--------|
| `get/GellertData.jsp` | `BuildCgiVars()` | Regular polling (every 3s) — includes `LogCounter`, `LogTotal`, `UserLogSettings`, all CGI variables |
| `get/GellertLogData.jsp` | `GetLogData()` | Fetches accumulated LogData as JS variables after PostSave confirms data is ready |
| `get/GellertListDevices.jsp` | `GetListDevices()` | Lists available displays for USB download target selection |

### POST Endpoints

| Endpoint | Handler | Purpose |
|:---------|:--------|:--------|
| `cgi/PostGellert.jsp` | `ProcessPost()` | Relays form data to ARM via serial (date range, graph items, etc.) |
| `cgi/PostSave.jsp` | `PostSave()` | Blocks until ARM responds (up to 25s); returns `ArmReply` value |
| `cgi/PostOpenOutputFile.jsp` | `PostOpenOutputFile()` | Opens a file via GFS for USB log download |
| `cgi/PostKillSession.jsp` | `PostKillSession()` | Aborts an in-progress log data request |

### UI Pages That Trigger Log Requests

| Page (form name) | File | Log Type |
|:-------|:-----|:---------|
| `graph` | `grPlottedGraph.htm` | User log graph (date range) |
| `record` | `rcPlottedRecords.htm` | User log table (date range) |
| `hsSystemLogRange` | `hsSystemLogRange.htm` | Activity/system log (record range) |
| `hsWarningLogRange` | via History.htm | Warning log (latest N records) |
| `hsLoadLogGraph` | `hsLoadLogGraph.htm` | Load log graph |
| `hsLoadLogTable` | `hsLoadLogTable.htm` | Load log table |
| `p2PIDLogRange` | via p2PIDLogs.htm | PID log table |
| `p2PIDGraph` | via p2PIDLogs.htm | PID log graph |
| `hsUserLogToUSB` | `hsUserLogToUSB.htm` | User log → USB CSV |
| `hsSystemLogToUSB` | via hsSystemLog.htm | Activity log → USB CSV |
| `hsLoadLogToUSB` | via hsLoadLog.htm | Load log → USB CSV |
| `p2PIDLogToUSB` | via p2PIDLogs.htm | PID log → USB CSV |
| `p2SettingsToUSB` | via p2PanelRestore.htm | Settings backup → USB |

### Request Flow Sequence (UI perspective)

```
1. form.submit()                          → POST cgi/PostGellert.jsp  (sends request to ARM)
2. AjaxSW.request('postsp', PostSave.jsp) → POST cgi/PostSave.jsp    (blocks for ARM reply)
3. processPostResponse() checks Data_Loaded:
   - "true"  → requestData(GellertLogData.jsp) → GET get/GellertLogData.jsp
   - "none"  → "No data available" error popup
   - "busy"  → "System busy" error popup
   - "overflow" → "Data overflow" error popup
4. processData() for GellertLogData.jsp → endHistoryProcess() → render
```

---

## 4. Bridge Server Current State vs. What's Missing

### What the Bridge Server Currently Handles

The bridge server (`ui-svelte/server/src/`) handles these MultiMsg types in the `multimsg-complete` event handler (`index.ts:257`):

| MultiMsg Type | Handling |
|:------|:------|
| `Warning` | Stores as `WarningData` + `AlarmData` in dataCache |
| `SensorData` | Stores each sub-tag (PileTempsData, etc.) in dataCache |
| `SensorLabels` | Converts to stride-3 format, stores as `SensorLabelData` |
| `Versions` | Stores as `FirmwareVersion` |
| `IoDefinition` | Stores as `IoNames` with default name overlay |
| `Email` | Stores as `EmailConfig` |
| `Network` | Stores as `P2NodeSetupData` + `MasterSlaveData` |

The bridge also handles these serial tags in `serialBridge.ts`:
- `DataLoadStatus` → resolves the active POST promise via `processDataLoadStatus()`
- `LogToFile` → logs the event and emits `'logToFile'` (no file writing)
- `MultiMsg`/`MultiEnd` → accumulates sub-messages via `MultiMsgSession`

### What is COMPLETELY MISSING for Log Retrieval

| Missing Feature | GellertServer Equivalent | Impact |
|:--------|:------|:------|
| **LogData MultiMsg handler** | `case 'LogData'` in `multimsg-complete` event — not present in `index.ts` switch | ARM log data is received and accumulated by `serialBridge.ts` but **discarded** at the application layer |
| **Log data storage** | `LogData[MAX_GRAPH_ITEMS+1]` linked lists in GellertServer | No equivalent data structure to hold multi-record log data between the serial receive and HTTP serve |
| **GET /get/GellertLogData.jsp** | `GetLogData()` → writes JS variables to HTML | No endpoint to serve accumulated log data to the UI. The stub `GET /logs` returns `"No logs available"` plaintext |
| **POST /cgi/PostGellert.jsp relay** | `ProcessPost()` → serial RTS/ACK | The bridge has `sendToArm()` but no endpoint that relays arbitrary form POST data to the ARM as the original server does for history pages |
| **POST /cgi/PostSave.jsp blocking** | `PostSave()` → polls ArmReply for 25s | No blocking POST endpoint that waits for ARM reply |
| **POST /cgi/PostOpenOutputFile.jsp** | `PostOpenOutputFile()` → GFSopen | No USB file open endpoint |
| **POST /cgi/PostKillSession.jsp** | `PostKillSession()` → aborts session | No session abort endpoint |
| **LogMsgCounter / progress tracking** | `Session[sid].LogMsgCounter` incremented on each `Data=EOR` | No progress counter mechanism |
| **LogToFile file writing** | `ProcessLogToFile()` → calls `SendRecordToFile()` → `GFSwrite()` | Bridge only logs and emits event; does not write to GFS |
| **GFS client (GellertFileSystem TCP protocol)** | `GFSopen()` / `GFSwrite()` / `GFSclose()` — binary TCP on port 9209 | No GFS client implementation exists in the bridge |

### Endpoints Available in Bridge (apiRoutes.ts)

| Endpoint | Status |
|:---------|:-------|
| `GET /iot/logs` | **Stub** — returns `"No logs available"` |
| `GET /iot/log` | Returns cached `recInterval` tag value (UserLogSettings page data only) |
| `GET /iot/alarms/:range` | **Stub** — returns `{ alarms: [] }` |
| `POST /iot/<page>` (pageSaveMap) | Handles settings saves but **not** history page saves |

---

## 5. How GellertFileSystem.out Fits In (Port 9209)

### Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                     RPi5 (or QEMU guest)                            │
│                                                                     │
│  ┌──────────────┐    TCP:9209     ┌──────────────────────────────┐  │
│  │ gellertserverd│ ─────────────► │ GellertFileSystem.out        │  │
│  │ (or bridge)  │  GFS_Struct    │ (separate daemon)            │  │
│  └──────────────┘  binary msgs    │                              │  │
│                                   │  Handles:                    │  │
│                                   │  - Open/Write/Close files    │  │
│                                   │  - Mount/unmount USB drives  │  │
│                                   │  - Path resolution           │  │
│                                   │  - File types: LOG, SETTINGS,│  │
│                                   │    PROFILE, DEBUG             │  │
│                                   └──────────────────────────────┘  │
│                                          │                          │
│                                          ▼                          │
│                              /media/usb/ (USB drive)                │
│                              or /var/agristar/logs/ (local)         │
└─────────────────────────────────────────────────────────────────────┘
```

### GFS Client Protocol (from GellertFileSystem.c)

The client side is in `LinuxHttpServer/GellertServer/src/GellertFileSystem.c` (698 lines).

**Data structure** — `GFS_Struct` (272 bytes):
```c
typedef struct {
    int  GFS_FUNCTION;   // operation code
    int  fd;             // file descriptor (returned by open)
    int  count;          // bytes to write/read
    int  flags;          // open flags (O_WRONLY|O_CREAT, etc.)
    char data[256];      // path (for open) or data payload (for write)
    int  CheckSum;       // integrity check
} GFS_Struct;
```

**Function codes:**
| Code | Constant | Purpose |
|:-----|:---------|:--------|
| 0 | `GFS_OPEN_LOG` | Open a log file (.csv) |
| 1 | `GFS_WRITE` | Write 256-byte chunk |
| 2 | `GFS_READ` | Read from file |
| 3 | `GFS_CLOSE` | Close file |
| 4 | `GFS_RETRY` | Retry failed operation |
| 5 | `GFS_NEXT` | Next file/record |
| 6 | `GFS_OPEN_SETTINGS` | Open settings file (.txt) |
| 8 | `GFS_OPEN_PROFILE` | Open profile file |

**Client functions** (in gellertserverd):
- `GFSopen(GFS_OUTPUTFILE *pFile, int flags)` — Opens a TCP socket to the target display's port 9209, sends an OPEN command with filename. If target IP == local IP, handles file operations locally instead.
- `GFSwrite(char *buf, int count)` — Splits data into 256-byte chunks, sends each as a WRITE command.
- `GFSclose()` — Sends CLOSE command and closes TCP socket.

**Local file handling** — `ProcessReceivedMessageLocal()`:
When the target display is the local machine, GFS operates directly on the filesystem:
- Log files → `FIRMWARE_MOUNT_POINT` (USB) or `LOG_HISTORY_DIR` (local fallback)
- Settings files → `LOG_SETTINGS_DIR`
- File extensions: `.csv` for logs, `.txt` for settings/debug

### Why GFS Matters

Per the project instructions, **GellertFileSystem.out runs unmodified inside the QEMU guest**. It is one of the services started by `StartResponders.sh`. The bridge server does NOT need to reimplement GellertFileSystem.out — it needs to implement the **GFS client** (the TCP protocol that gellertserverd uses to talk to it). Specifically:

1. When the UI requests a USB download, the bridge must open a file via TCP to GellertFileSystem.out on port 9209.
2. As log data arrives from the ARM, the bridge must write records to that file.
3. When the ARM signals `LogToFile=EOF`, the bridge must close the file.

Since GellertFileSystem.out is already running in the QEMU guest, the bridge only needs the client-side TCP socket code (~150 lines equivalent).

---

## 6. What Needs to Be Implemented in the Bridge for Log Retrieval

### Priority 1 — In-Browser Log Viewing (Path A)

These are required for graph and record pages to work:

#### 6.1 LogData Storage Structure

Create a session-aware log data accumulator equivalent to GellertServer's `LogData[MAX_GRAPH_ITEMS+1]` linked lists:

```typescript
// Conceptual structure (not code to copy)
interface LogDataSession {
  sessionId: number;
  type: string;
  data: Map<string, string[]>;  // tag → accumulated values (e.g. "Key" → ["header1,header2,..."])
  logMsgCounter: number;
  complete: boolean;
}
```

The existing `MultiMsgSession` in `serialBridge.ts` already accumulates sub-messages — the gap is in `index.ts` where the `'multimsg-complete'` handler has no `case 'LogData'`.

#### 6.2 Handle LogData MultiMsg Completion

Add `case 'LogData'` to the `multimsg-complete` handler in `index.ts`. Store the accumulated data in a per-session structure accessible to HTTP handlers.

#### 6.3 Legacy CGI Endpoints (for UI-International compatibility)

The original UI calls these endpoints and expects specific response formats:

| Endpoint | Response Format | What It Does |
|:---------|:----------------|:-------------|
| `POST cgi/PostGellert.jsp` | Empty 200 OK | Relay form body to ARM via serial |
| `POST cgi/PostSave.jsp` | `<SCRIPT>var Data_Loaded="true";</SCRIPT>` (HTML with embedded JS) | Block until ARM reply, return status |
| `GET get/GellertLogData.jsp` | `<HTML><HEAD><SCRIPT>var Key="...";var Data="...";var Dates="...";var TimeStamps="...";var Warnings="...";</SCRIPT></HEAD><BODY></BODY></HTML>` | Serve accumulated log data as JavaScript |
| `POST cgi/PostKillSession.jsp` | Empty 200 OK | Abort in-progress data load |

**Note:** These endpoints are served by lighttpd's FastCGI, which proxies to gellertserverd via named pipes. Since gellertserverd is disabled in the QEMU setup, these requests would need to be either:
- (a) Handled by the bridge server (requires adding routes and reverse-proxy config in lighttpd), or
- (b) Re-enabled through gellertserverd (defeats the purpose of the bridge)

The current lighttpd config proxies `/iot/*` to the bridge on port 3001. The `cgi/*` and `get/*` paths are handled by the FastCGI handlers which talk to gellertserverd — **which is disabled**. This is likely why log retrieval doesn't work at all currently.

#### 6.4 Progress Counter

Implement a per-session counter that increments on each `Data=EOR` received from ARM, and make it available via an endpoint (or the existing `/iot/data` polling). The UI reads `LogCounter` and `LogTotal` from the regular data poll.

### Priority 2 — USB Download (Path B)

These are required for downloading logs to USB drives attached to displays:

#### 6.5 GFS TCP Client

Implement the GFS binary protocol client (connect to port 9209, send `GFS_Struct` messages):
- `gfsOpen(displayIp, fileName, fileType)` — TCP connect + GFS_OPEN_LOG
- `gfsWrite(data)` — chunk data into 256-byte GFS_WRITE messages
- `gfsClose()` — GFS_CLOSE + TCP disconnect

#### 6.6 PostOpenOutputFile Endpoint

Add an endpoint (matching `POST cgi/PostOpenOutputFile.jsp` or creating a new `/iot/openOutputFile`) that:
1. Opens a GFS file on the target display
2. Sets a flag so that incoming `Data=EOR` records get written via `gfsWrite()`
3. On `LogToFile=EOF`, calls `gfsClose()`

#### 6.7 LogToFile Event Handler

The bridge currently emits `'logToFile'` but doesn't handle it. Add a listener that:
- On `EOR`: calls `SendRecordToFile()` equivalent (concatenate LogData records, write via GFS)
- On `EOF`: calls `gfsClose()`

### Implementation Approach Decision

There are two architectural approaches:

**Option A — Bridge handles everything via `/iot/*` routes:**
- Add all log endpoints to `apiRoutes.ts`
- Requires the new Svelte UI to call these new endpoints
- Does NOT help the original UI-International which calls `cgi/PostGellert.jsp` etc.

**Option B — Bridge serves legacy CGI paths:**
- Add lighttpd reverse-proxy rules for `cgi/PostGellert.jsp`, `cgi/PostSave.jsp`, `get/GellertLogData.jsp`, `cgi/PostOpenOutputFile.jsp`, `cgi/PostKillSession.jsp` → bridge:3001
- Bridge responds with the exact HTML/JS format the original UI expects
- Works for both UIs

Per the project goal ("not touch the svelte ui code" + "run all the same services the real hardware does"), **Option B is recommended** since the legacy UI runs inside QEMU via lighttpd and expects these exact endpoints. The bridge already proxies `/iot/*`; extending the proxy to cover these additional paths in `patch_rpi5_qemu.sh` would be straightforward.
