# GellertServerD Complete Inventory

**Version**: LHS v1.8 (GellertServer), v1.1 (GellertGet), v1.2 (GellertPost)  
**Compatible Firmware**: AS2 v1.07, Agri-Star v4.10  
**Source Directories**: `LinuxHttpServer/GellertServer/`, `GellertHttpShared/`, `GellertGet/`, `GellertPost/`

---

## 1. Architecture Overview

GellertServerD is a multi-threaded C daemon that bridges an ARM TM4C microcontroller (connected via UART) to a web UI served by lighttpd + FastCGI. It consists of four binaries:

| Binary | Source | Role |
|---|---|---|
| `gellertserverd` | GellertServer/src/GellertServer.c | Main daemon — serial comms, data cache, request routing |
| `gellertgetd` | GellertGet/src/GellertGet.c | FastCGI GET handler — receives `lighttpd` GET requests, forwards to gellertserverd via named pipes |
| `gellertpostd` | GellertPost/src/GellertPost.c | FastCGI POST handler — receives `lighttpd` POST requests, forwards to gellertserverd via named pipes |
| (linked into gellertserverd) | GellertHttpShared/src/*.c | Shared libraries for pipe and logging operations |

### Thread Model

```
main thread    ─ reads URI pipe, spawns per-request threads
                  ├── GetServer thread (per GET request)
                  └── PostServer thread (per POST request)
ArmMessaging   ─ serial send/receive state machine, watchdog
LtxMessaging   ─ UDP broadcast receive/send (network monitor)
DeviceCommP2P  ─ UDP listener for inter-device P2P packets
```

---

## 2. Serial Protocol Handling

### 2.1 Physical Layer (`SerialComm.c` — 564 lines)

| Function | Purpose |
|---|---|
| `SerialPortInit()` | Opens serial port (default `/dev/ttyAMA0`, configurable via `serial.conf`) at **230400 baud, 8N1**, no flow control. Reboots system after 10 failed open attempts. |
| `SerialReceive()` | Reads available bytes into a circular buffer from the serial port. |
| `ProcessRxBuffer()` | Extracts complete messages framed between `^` (start) and `!` (end) from the circular buffer. |
| `ProcessSerialMessage()` | Validates CRC, strips framing, dispatches to `ProcessArmMessage()`. |
| `SendMessage()` | Formats a message as `^<payload>$<CRC>!` and writes it to the serial port. |
| `SerialSend()` | Low-level write to serial fd. |
| `SerialRead()` | Low-level read with timeout. |
| `SerialFlushBuffer()` / `SerialCharsBuffered()` | Buffer management utilities. |

**Wire Format**: `^tag=value$CRC!`  
- `^` = start delimiter  
- `=` = tag/value divider  
- `$` = value/CRC terminator  
- `!` = end delimiter  
- CRC = CRC-32 (polynomial `0xEDB88320`) of the payload (`tag=value`), decimal

### 2.2 CRC Validation (`CRC.c` — 155 lines)

| Function | Purpose |
|---|---|
| `CRC()` | CRC-32 calculation (matches ARM implementation). |
| `CheckCRC()` | Validates incoming message CRC. |
| `DelayThread()` | Thread sleep utility (ms or seconds). |

### 2.3 Message State Machine (`ArmMessaging.c` — 1154 lines)

States: `WAITING` → `RTS` → `ACK` → `SEND` → `REPOST` → `WAITING`

| State | Meaning |
|---|---|
| `MSG_STATE_WAITING` | Idle, ready for next outgoing message |
| `MSG_STATE_RTS` | Request-to-send queued, sends `RTS=` on serial |
| `MSG_STATE_ACK` | Waiting for ARM's `ACK` before sending payload |
| `MSG_STATE_SEND` | ARM acknowledged, payload transmitted |
| `MSG_STATE_REPOST` | Waiting for ARM reply to LTX-initiated post |

**Key ArmMessaging Functions**:

| Function | Purpose |
|---|---|
| `ArmMessagingInit()` | Starts ARM messaging service thread. |
| `ArmMessagingService()` | Main loop — `SerialReceive()`, `ProcessRxBuffer()`, state machine tick, initialization messaging (every 2s if not initialized), watchdog, session timeout, debug. |
| `ArmMessagingWatchDog()` | 30s silence → restart messaging. 600s silence → set `WARN_ARMCOMM`. |
| `ProcessArmMessage()` | Top-level dispatcher for all ARM messages (see §2.4). |
| `SendACK()` | Sends `ACK=<tag>` (or `KILL=<tag>&SessionID=<id>` to abort a request). |
| `ProcessACK()` / `ProcessNAK()` | ACK/NAK handling with retry logic. |

### 2.4 ARM Message Dispatch (`ProcessArmMessage()`)

Every incoming message's tag is matched and routed:

| ARM Message Tag | Handler | Action |
|---|---|---|
| `ACK` | `ProcessACK()` | Confirm message receipt, advance state to SEND or WAITING |
| `NAK` | `ProcessNAK()` | Retry current message |
| `DataLoadStatus` | `ProcessDataLoad()` | Store ARM reply (data loading status) for the HTTP session |
| `Initialize` | `ProcessMsgInit()` | Version negotiation, sends `Initialize=LTX&Version=1.07`, writes board type file, triggers equipment description transfer |
| `LogToFile` | `ProcessLogToFile()` | Handles `EOR` (end-of-record) → `GFSclose()`, `EOF` (end-of-file) → `GFSclose()` |
| `MasterBroadcast` | `ProcessMasterBroadcast()` | UDP broadcasts `Master=<data>&IP=<localip>` to other controllers |
| `NetMonitorMode` (CGI_NETMONITOR.MsgTag) | `ProcessNetMonitor()` | Stores network monitor mode |
| `Date` (CGI_DATETIME.MsgTag) | `ProcessDateTime()` | Stores date/time, sets RPi system clock via `date -s` |
| `dlr` (CGI_AUTHORIZE.MsgTag) | `ProcessProgramLevel()` | Stores authorization status for the HTTP session |
| All other tags | `ProcessMessage()` | Routes to `StoreCgiVar()` or `ProcessRepost()` depending on state |

### 2.5 Messages Sent to ARM

| Message | When |
|---|---|
| `RTS=` | Before every outgoing payload |
| `ACK=<tag>` | After receiving unsolicited message |
| `NAK=<tag>` | On CRC failure (implicit retry) |
| `KILL=<tag>&SessionID=<id>` | To abort an in-progress ARM request |
| `Initialize=LTX&Version=<ver>` | On startup initialization |
| `FindNodes=<ip>&<node_list>` | Network node discovery |
| `NodeUpdate=<id>&IpAdd=<ip>&PublicIp=<pip>&HttpPort=<port>` | Public IP update |
| `SlaveUpdate=<broadcast_data>` | Master/slave relay |
| `FanRemoteOff=2` / `FanRemoteOff=0` | Settings restore (remote off/on) |
| Equipment descriptions | 100+ translated strings during `EquipmentDescTranslate()` |
| Any HTTP POST body | Forwarded verbatim via `ProcessPost()` |

---

## 3. Data Caching & Transformation

### 3.1 CGI Variable Cache (`CGI.c` — 1577 lines)

The core data cache is an array of `CGI_DATA` structs (~70 entries), each mapping an ARM serial tag to a JavaScript variable name.

**`CgiDataInit()` — Complete CGI Variable Map:**

| Enum | MsgTag (ARM) | VarName (JS) | Purpose |
|---|---|---|---|
| `CGI_MAINDATA` | `main` | `MainData` | Live sensor data (16 comma-separated fields) |
| `CGI_PGMDATA` | `p1Plenum` | `PgmData` | Program level data (5 fields) |
| `CGI_DAILYFAN` | `DailyFanRuntime` | `DailyFanRun` | Daily fan runtime counters |
| `CGI_TOTALFAN` | `TotalFanRuntime` | `TotalFanRun` | Total fan runtime counters |
| `CGI_OUTSIDE` | `ctrlMode` | `OutsideAirData` | Outside air control settings |
| `CGI_AIRCURE` | `CureStartTemp` | `AirCureData` | Air cure settings |
| `CGI_RUNTIMES` | `runTimes` | `RunTimesData` | Equipment run times |
| `CGI_FREQCTRL` | `maxFanSpeed` | `FreqCtrlData` | Frequency controller settings |
| `CGI_RAMPRATE` | `updTemp` | `RampRateData` | Temperature ramp rate settings |
| `CGI_HUMIDCTRL` | `selHumidType` | `HumidCtrlData` | Humidity control settings |
| `CGI_CO2PURGE` | `selPurgeMode` | `Co2PurgeData` | CO2 purge settings |
| `CGI_EQUIPSTATUS` | `EquipStatus` | `EquipStatusData` | Equipment I/O status (100 fields) |
| `CGI_MISCDATA` | `p1Misc` | `MiscData` | Miscellaneous data |
| `CGI_BASICSETUP` | `StorageName` | `P2BasicSetupData` | System settings (name, mode, master/slave, etc.) |
| `CGI_PASSWORD` | `Passwords` | `P2Password` | System passwords (sent only on explicit request) |
| `CGI_BOARDS` | `BAdd` | `P2AnalogBoardData` | Analog board configuration |
| `CGI_FRESHAIR` | `PAirValue` | `P2FreshAirData` | Fresh air settings |
| `CGI_REFRIG` | `p2Refrigeration` | `P2RefrigerationData` | Refrigeration settings |
| `CGI_CLIIMACELL` | `ClimacellEff` | `P2ClimacellData` | Climacell settings |
| `CGI_FAILURES1` | `FanMode` | `P2FailuresData` | Failure mode settings (set 1) |
| `CGI_FAILURES2` | `OutAirMode` | `P2Failures2Data` | Failure mode settings (set 2) |
| `CGI_MODE` | `CurrentMode` | `CurrentMode` | Current operating mode |
| `CGI_TEMPDEV` | `AlarmTempLow` | `PlenTempDevData` | Temperature deviation alarm settings |
| `CGI_DATETIME` | `Date` | `DateTimeData` | Date/time (8 fields) |
| `CGI_SERVICE` | `dealerName` | `P2ServiceData` | Dealer/service contact info |
| `CGI_USERLOG` | `recInterval` | `UserLogSettings` | User log configuration |
| `CGI_AUTHORIZE` | `dlr` | `DlrMode` | Authorization/login mode |
| `CGI_BURNER` | `selBurnerMode` | `P2BurnerData` | Burner settings |
| `CGI_NETMONITOR` | `NetMonitorMode` | `NetMonitorEnabled` | Network monitor on/off |
| `CGI_USERACCTS` | `AcctId0` | `UserAccounts` | User accounts data |
| `CGI_ALERTSETUP` | `AlertSetup` | `AlertSetupData` | Per-warning email alert enable/disable |
| `CGI_LOGTOTAL` | `LogTotal` | `LogTotal` | Log record count / session ID |
| `CGI_GRAPHFAVS` | `GraphFavorites` | `GraphFavorites` | Graph favorites configuration |
| `CGI_FANBOOST` | `selBoostMode` | `FanBoostData` | Fan boost settings |
| `CGI_SETTINGS` | `RestoreSettings` | `RestoreSettings` | Settings restore status |
| `CGI_CLIMACELLTIMES` | `climacellTimes` | `ClimacellTimesData` | Climacell schedule |
| `CGI_HUMIDMODES` | `HumidModes` | `HumidModes` | Humidity mode options |
| `CGI_PWMCHANNELS` | `p2PwmOutputs` | `P2PwmChannelData` | PWM output configuration |
| `CGI_LOADMONITOR` | `bay1Label` | `LoadMonitorData` | Load monitor per-bay data |
| `CGI_AVAILABLEIO` | `AvailableIo` | `AvailableIoData` | Available I/O listing |
| `CGI_OUTPUTCONFIG` | `OutputConfig` | `OutputConfigData` | Output configuration (43 fields) |
| `CGI_INPUTCONFIG` | `InputConfig` | `InputConfigData` | Input configuration |
| `CGI_AUXPROGRAM` | `AuxProgram` | `AuxProgramData` | Auxiliary program settings |
| `CGI_AUXSWITCHES` | `AuxSwitches` | `AuxSwitchesData` | Auxiliary switch settings |
| Internal: `CGI_IPADD` | (local IP address) | — | |
| Internal: `CGI_MASK` | (subnet mask) | — | |
| Internal: `CGI_MAC` | (MAC address) | — | |
| Internal: `CGI_GATEWAY` | (gateway) | — | |
| Internal: `CGI_HTTPPORT` | (HTTP port, public IP) | — | |
| Internal: `CGI_LTXVERSION` | (LHS version) | — | |
| Internal: `CGI_FILESTART` | `fileStart` | — | Equipment desc transfer trigger |
| Internal: `CGI_EQUIPDESC` | `equipDesc` | — | Equipment description storage |
| Internal: `CGI_PWMDESC` | `pwmDesc` | — | PWM description storage |
| Internal: `CGI_QUERYTAG` | `queryTag` | — | Query tag storage |
| Internal: `CGI_SYSLOGREC/EQUIP/REMOTE` | `syslogRec/Equip/Remote` | — | Log label storage |
| Internal: `CGI_PIDLOG` | `pidLog` | — | PID log label storage |
| Internal: `CGI_SYSMODE` | `sysmode` | — | System mode names |
| Internal: `CGI_BOARDLABEL` | `boardLabel` | — | Board labels |
| Internal: `CGI_TEMPSENSOR/HUMIDSENSOR` | `tempSensor/humidSensor` | — | Sensor names |
| Internal: `CGI_BAYLABEL` | `bayLabel` | — | Bay labels |

**Key CGI Functions:**

| Function | Purpose |
|---|---|
| `StoreCgiVar()` | **Central message storage router** — handles `MultiMsg` (start multi-message sequence), `MultiEnd` (end sequence + trigger warnings/email), regular CGI tags, multi-message data, `LogAlarms`, `LogSearch`, `SaveSettings` (write to GFS), `ClearAlarm`, `Debug` (write debug data to USB), and `CGI_FILESTART` (triggers equipment description transfer). |
| `GetCgiData()` | Writes all CGI variables to HTTP client as `var VarName = "value";` |
| `GetCgiItem()` / `GetJsonItem()` | Returns single CGI variable in HTML or JSON format |
| `GetCgiElement()` | Extracts a specific comma-separated element from a CGI variable |
| `CgiVarToJson()` | Converts comma-separated CGI string to JSON using a model field-name array |
| `CgiIgnoreHttp()` | Preserves local HTTP port setting over ARM-reported value |
| `ClearCgiData()` | Clears all expired CGI data when ARM stops responding |
| `ExpandLogAlarms()` | Converts alarm index references in log data to full alarm message strings |
| `GetCustomAlertSetup()` | Filters alert setup for board type (AS2 vs Agri-Star have different available warnings) |
| `GetCgiPasswordData()` | Returns password data only on explicit request (security measure) |

### 3.2 Multi-Message System (`MultiMsg.c` — 403 lines)

For variable-length data that doesn't fit in a single serial message, the ARM sends sequences:

```
^MultiMsg=<SessionID>,<Type>,<Tag1>,<Tag2>,...$CRC!   ← start
^Tag1=data$CRC!                                       ← data elements (linked list)
^Tag2=data$CRC!
^MultiEnd=<Type>$CRC!                                 ← end
```

**Multi-Message Types:**

| Type | Array | Max Tags | Purpose |
|---|---|---|---|
| `LogData` | `LogData[11]` | Key, Dates, TimeStamps, Data, Warnings, etc. | Historical log data (graph/table) |
| `Warning` | `Warnings[2]` | Warning index + status | Active warnings |
| `SensorData` | `SensorData[3]` | Sensor readings | Real-time sensor data |
| `SensorLabels` | `SensorLabels[3]` | Sensor names | Sensor label strings |
| `Network` | `Network[2]` | Network node data | Network discovery results |
| `Versions` | `Versions[2]` | Version info | Software/firmware versions |
| `Email` | `Email[2]` | SMTP config | Email server/account settings |
| `IoDefinition` | `IoDefinition[2]` | I/O definitions | Input/output names |

**Key MultiMsg Functions:**

| Function | Purpose |
|---|---|
| `MultiMsgStoreVar()` | Appends data to the linked list for the matching tag |
| `MultiMsgFree()` | Frees all linked-list nodes |
| `GetMultiMsgData()` | Writes linked list contents to HTTP response |
| `MultiMsgToString()` | Converts linked list to a flat string (used by parsers) |
| `SendRecordToFile()` | Writes log records to USB via `GFSwrite()` |

### 3.3 JSON Data Models (`JsonDataModels.h` — 264 lines)

String arrays mapping CGI enum indices to JSON field names:

| Model | Fields | CGI Source |
|---|---|---|
| `MainDataModel[16]` | Mode, StartTemp, PlenumTemp, ReturnTemp, OutsideTemp, ..., BurnerOutput, OutputLabel | `CGI_MAINDATA` |
| `PgmDataModel[5]` | TempSetpoint, HumidSetpoint, HumidReference, BurnerSetpoint, CureSetpoint | `CGI_PGMDATA` |
| `EquipStatusModel[100]` | Full I/O status: switches, inputs, outputs for fans, climacells, humidifiers, refrigeration, aux, lights, heat, burner, cure, PWM, door | `CGI_EQUIPSTATUS` |
| `ModeDataModel[4]` | Mode, ModeSource, EquipDescVersion, OnionPgmMax | `CGI_MODE` |
| `OutputConfigModel[43]` | Full output config: FanType, Climacell, Humid1-3 types, Refrig stages/defrosts, Aux1-8, Lights, Heat, CavHeat, Burner, Cure, PWM channels | `CGI_OUTPUTCONFIG` |

### 3.4 JSON Serialization Functions

| Function (in `CGI.c`) | Purpose |
|---|---|
| `GetSystemMonitorJson()` | Builds comprehensive JSON: mode, main data, pgm data, color-coded status, output config |
| `GetSystemMonitorColors()` | Calculates green/amber/red status colors based on temp/humid/CO2 thresholds |
| `GetStorageListItemJson()` | JSON with mode, temps, humidity, colors for the storage list view |
| `GetClimacellImage()` | Returns climacell animation image path based on equipment status |
| `GetHumidifierImage()` | Returns humidifier animation image path |
| `GetLightsImage()` | Returns lights animation image path |

---

## 4. GET Request Handling

### 4.1 FastCGI GET Handler (`GellertGet.c` — 205 lines)

- Accepts FastCGI GET requests from lighttpd
- Creates per-request named pipes (`GellertGetToClient.<tid>.<id>`, `GellertGetToServer.<tid>.<id>`)
- Writes URI to the shared `URI_SERVER` pipe
- Exchanges HTTP info (RemoteAddress, ServerAddress, ServerPort)
- Reads response and returns to client
- Returns `204 No Content` on timeout

### 4.2 GET Routing (`CallBacks.c::GetGellert()`)

| URL Path | Handler | Response |
|---|---|---|
| `/GellertLogData.jsp` | `GetLogData()` | Multi-msg log data (HTML CGI format) |
| `/GellertSoftwareUpgrade.jsp` | `GetNetworkNodes(filter=1)` | Discovered controllers & displays with firmware |
| `/GellertListDevices.jsp` | `GetNetworkNodes(filter=0)` | All discovered network devices |
| `/GellertSettingsFileFind.jsp` | `GetSettingsFiles()` | Settings files on USB/local disk |
| `/GellertUpgradeStatus.jsp` | `GetUpgradeStatus()` | Current upgrade progress message |
| `/GellertAccounts.jsp` | `GetUserAccounts()` | User accounts data |
| `/GellertAlarmStrings.jsp` | `GetAlarmStrings()` | All translated warning messages (filtered by board type) |
| `/GellertMobile.jsp` | `GetMobile()` | JSON mobile API (see §4.3) |
| `/AgristarGet.jsp` | `GetAgristar()` | JSON Svelte UI API (see §4.4) |
| (default) | `BuildCgiVars()` | Full HTML CGI response (all variables) |

### 4.3 Mobile JSON API (`GetMobile()`)

Dispatches by `RequestID` query parameter:

| RequestID | Response |
|---|---|
| 100 | Device discovery JSON |
| 101 | Device discovery JSON (node setup) |
| 200 | New session → SessionID, PgmLevel |
| 201 | Basic data: MainData, PgmData, Mode, EquipStatus, DateTime, DailyFanRun, BoardType, SessionID, pgmLevel, Data_Loaded |
| 202 | Sensor data, sensor labels |
| 210 | Program level 1 data |
| 220 | Program level 2 data |

### 4.4 Svelte UI JSON API (`GetAgristar()`)

Dispatches by `RequestID` query parameter:

| RequestID | Response |
|---|---|
| 200 | New session → SessionID, PgmLevel |
| 300 | Device discovery JSON array |
| 350 | Storage list item JSON (mode, temps, humidity, colors) |
| 400 | System monitor JSON (full composite with colors and output config) |
| 450 | Climacell image path |
| 451 | Humidifier 1 image path |
| 452 | Humidifier 2 image path |
| 453 | Humidifier 3 image path |
| 454 | Lights image path |
| 500 | Profile files list |
| 501 | Profile restore (read profile from USB/FTP) |
| 800 | Display data |
| (default) | Single CGI item by index (JSON format) |

### 4.5 Default HTML CGI Response (`BuildCgiVars()`)

Returns a complete HTML page with embedded JavaScript variables:

```html
<HTML><HEAD><SCRIPT LANGUAGE="JAVASCRIPT">
var SessionID = "0";
var RequestCount = "5";
var LogCounter = "0";
var BoardType = "AS2";
var pgmLevel = "0";
var Data_Loaded = "true";
var ClientIpAdd = "192.168.1.100";
var LogTotal = "0";
var NetMonitorEnabled = "0";
var AlarmData = "...";
var MainData = "1,48.5,47.2,...";
var PgmData = "48.0,65,0,...";
... (all CGI variables with update timestamps)
var GFSFileInfo = "...";
</SCRIPT></HEAD><BODY></BODY></HTML>
```

---

## 5. POST Request Handling

### 5.1 FastCGI POST Handler (`GellertPost.c` — 351 lines)

- Accepts FastCGI POST requests from lighttpd
- Creates per-request named pipes (`GellertPostToClient.<tid>.<id>`, `GellertPostToServer.<tid>.<id>`)
- Reads `CONTENT_LENGTH` and `QUERY_STRING`
- Exchanges content length, post body, and HTTP info with gellertserverd
- Returns response to client

### 5.2 POST Routing (`CallBacks.c::PostGellert()`)

| URL Path | Handler | Action |
|---|---|---|
| `/AgristarPost.jsp` | `PostAgristar()` | JSON POST API (PostID 500 = profile save) |
| `/PostDiscoverNodes.jsp` | `PostDiscoverNodes()` | Network discovery → `BuildNodeList()` → sends `FindNodes` to ARM |
| `/PostKillSession.jsp` | `PostKillSession()` | Kills ARM request for a session |
| `/PostSoftwareUpgrade.jsp` | `PostSoftwareUpgrade()` | Launches `ProgramSystemLauncher()` |
| `/PostUIVersion.jsp` | `PostUIVersion()` → `ReplyToPostUIVersion()` | Stores UI version string, returns SessionID |
| `/PostMyDisplay.jsp` | `PostMyDisplay()` | Stores display IP address |
| `/PostTestEmail.jsp` | `PostTestEmail()` | Sends test email via `SendEmailAlert(EMAIL_TEST)` |
| `/PostAlertSetup.jsp` | `PostAlertSetup()` | Configures per-warning email enable/disable |
| `/PostPingController.jsp` | `PostPingController()` | Pings a network device via `ping -q -c 1 -W 1` |
| `/PostNetMonReset.jsp` | `PostNetMonReset()` | Clears network monitor status list |
| `/PostClearAlarms.jsp` | `ProcessPost()` | Forwards `ClearAlarm` to ARM |
| `/PostOpenOutputFile.jsp` | `PostOpenOutputFile()` | Opens log file on USB via `GFSopen()` |
| `/PostOpenDebugFile.jsp` | `PostOpenDebugFile()` | Opens/closes debug capture file on USB |
| `/PostOpenSettingsFile.jsp` | `PostOpenSettingsFile()` | Opens settings file for save |
| `/PostSettingsRestore.jsp` | `PostSettingsRestore()` | Reads settings file, sends each line to ARM via `RelaySystemSettingsMessage()`, bracketed by RemoteOff/RemoteOn |
| `/PostIoRename.jsp` | Generic POST + `PostSave()` | Sends I/O rename data to ARM then saves |
| `/PostSave.jsp` | `PostSave()` | Waits up to 25s for ARM save confirmation |
| (default) | `ProcessPost()` → `ReplyToPost()` | Forwards POST body to ARM, waits for reply (3s timeout, 15s for FindBoard) |

### 5.3 Key POST Functions

| Function | Purpose |
|---|---|
| `ProcessPost()` | Copies POST body to `Message.TxBuffer`, sets state to `MSG_STATE_RTS`. Waits if another session owns the ARM. 3s timeout → "busy". |
| `PostSave()` | After a standard ProcessPost, waits up to 25 seconds for ARM confirmation. Returns timeout or refreshed CGI data. |
| `ReplyToPost()` | Waits for ARM reply, returns "busy" on timeout. Special handling for `ShowPassword` and `dlr` (authorization). Default returns `BuildCgiVars()`. |
| `PostProfileSave()` | Writes profile data to USB via GFS (used by AgristarPost). |
| `PostSettingsRestore()` | FTP/local file read, sends each line to ARM as settings, brackets with RemoteOff/RemoteOn. |
| `PostSoftwareUpgrade()` | Calls `ProgramSystemLauncher()` if upgrading self. |

---

## 6. Service Interactions

### 6.1 GellertFileSystem (`GellertFileSystem.c` — 698 lines)

TCP client to the GellertFileSystem service (port **9209**) for USB file I/O. Supports both local and remote (network) file operations.

| Function | Purpose |
|---|---|
| `GFSopen()` | Opens a file on USB or local disk. Remote: TCP connect + GFS protocol with checksummed packets. Local: `ProcessReceivedMessageLocal()` mounts USB, opens file. |
| `GFSwrite()` | Writes data in 256-byte chunks. Supports retry/skip protocol. |
| `GFSclose()` | Closes file, syncs, archives settings files, unmounts USB. |
| `ProcessReceivedMessageLocal()` | Local file operations: mount USB/fallback to `/var/log`, open/close files, archive settings. |
| `GetGFSFileInfo()` | Returns currently open debug file name to HTTP client. |

**File Types**: `GFS_OPEN_LOGFILE` (0), `GFS_OPEN_SETTINGSFILE` (6), `GFS_OPEN_PROFILEFILE` (8), `GFS_OPEN_DEBUGFILE` (7)

### 6.2 Device Communication — UDP (`DeviceComm.c` — 841 lines)

Handles UDP peer-to-peer communication between controllers on the network.

| Function | Purpose |
|---|---|
| `DeviceCommInit()` | Starts `DeviceCommP2P` listener thread. |
| `DeviceCommP2P()` | UDP listener on port **9214** — stores received P2P packets. |
| `DeviceCommRead()` | Returns buffered P2P data. |
| `DeviceCommWrite()` | UDP broadcast on port 9214 to `255.255.255.255`. |
| `DeviceCommClearBuffer()` | Clears P2P receive buffer. |
| `DeviceCommQueryDevicesLocal()` | Broadcasts device query on port **9210**, listens on port **9211**. Returns discovered devices (controllers + displays) with firmware versions, names, IPs, MACs. |
| `DeviceCommGetUpgradeMessage()` / `DeviceCommClearUpgradeMessage()` | Get/clear upgrade progress messages. |
| `ArmProg()` | Programs ARM firmware via serial: locks serial port, enters bootloader, sends hex records line-by-line with ACK/retry. |
| `SetARMForProgramming()` | Sends `BOOTLOADER=1` command, waits for `BOOT_START=1` response.  |
| `MSendPacket()` / `MGetPacket()` | Low-level serial send/receive for firmware programming. |

**UDP Ports Used:**
- 9210: Device query (outgoing broadcast)
- 9211: Device response (incoming)
- 9214: P2P messaging (bidirectional broadcast)

### 6.3 Network Monitoring (`LtxMessaging.c` — 857 lines)

Manages inter-controller status broadcasting and tracking.

| Function | Purpose |
|---|---|
| `LtxMessagingInit()` | Starts LTX messaging service thread. |
| `LtxMessagingService()` | Main loop: reads P2P broadcasts via `DeviceCommRead()`, broadcasts own status every 60s, manages status timeouts, GFS debug file keep-alive (30s writes). |
| `ProcessLtxMessage()` | Validates CRC, dispatches: `Master` → relay broadcast to ARM as slave, `SysStatus`/`SysInfo` → update network status. |
| `SendSystemStatus()` | Broadcasts: IP, StorageName, CurrentMode, Alarm, TempSet, PlenTemp, TempColor, ReturnTemp, PlenHumid, HumidColor, HttpPort, PublicIP. Color-coded: green (in range), amber (±0.2°), red (±0.5° or humid deviation). |
| `SendSystemInfo()` | Broadcasts: IP, HttpPort, PublicIP, MAC. |
| `SendLtxMessage()` | Wraps message in `^...$CRC!` delimiters, sends via `DeviceCommWrite()`. |
| `RelayBroadcastMessage()` | Slave mode: relays master's broadcast to local ARM as `SlaveUpdate=<data>`. |
| `NetworkStatusUpd()` | Add/update network controllers in status array. |
| `StatusTimeout()` | Expires entries after 90 seconds of silence. |
| `NetworkStatusReset()` | Clears all network status entries. |
| `GetNetworkMonitorData()` | Returns all network status as HTML variable. |
| `GetDeviceDiscoveryJson()` / `GetDeviceDiscovery()` | Returns network status as JSON array. |

### 6.4 Email Alerts (`Email.c` — 324 lines)

| Function | Purpose |
|---|---|
| `SendEmailAlert()` | Three modes: `EMAIL_ALERT` (active warnings), `EMAIL_CLEAR` (all clear), `EMAIL_TEST`. Builds message from Warning[] array. Executes `SendMail.sh` with SMTP settings from Email[] multi-msg (server, port, auth, account, password, from, to). |
| `BuildEmailAlertMsg()` | Constructs email body listing all active warnings with translated messages. |
| `FormatAlertMsg()` | Appends formatted warning to email body. |
| `StoreEmailHeaderStrings()` | Stores translated email header strings from language file. |

### 6.5 Firmware Upgrade (`ProgResponder.c` — 345 lines)

| Function | Purpose |
|---|---|
| `ProgramSystemLauncher()` | Entry point — creates log file, setup /tmp, calls `ProgramSystem()`, reboots. |
| `ProgramSystem()` | Orchestrates: `InitializeUpgrade()` → `PrepareUpgradeFiles()` → `ArmProg()` → `ProgramUI()` → `ProgramHttpServer()` → `ProgramDisplay()`. |
| `InitializeUpgrade()` | Locates upgrade file on USB (local) or via FTP (remote display). |
| `SendUpgradeMessage()` | Sets upgrade progress string (polled by `GetUpgradeStatus()`). |

### 6.6 IoT / Azure Hub (`IotMessaging.c` — 2187 lines)

Azure IoT Hub integration using MQTT (mostly commented out / not actively deployed):

- Defines Azure serializer models for `SystemMonitor`, `MainData`, `PgmData`, `EquipSwitches`, `EquipIo`, `EquipAux`, `SensorList`
- `IotMessagingInit()` — waits for data then starts IoT thread
- `JsonSerializerInit()` — initializes Azure serializer
- Tracks data changes via `DATA_TRACKING` structs
- Cloud-to-device callbacks: `receive_msg_callback()`, `device_method_callback()` (SetTelemetryInterval), `connection_status_callback()`

---

## 7. Timer / Periodic Tasks

| Timer | Location | Interval | Action |
|---|---|---|---|
| ARM Init Messaging | `ArmMessagingService()` | 2s | Sends `Initialize=LTX` if not yet initialized |
| ARM Watchdog | `ArmMessagingWatchDog()` | 30s silence → restart, 600s → `WARN_ARMCOMM` |
| Session Timeout | `SessionTimeout()` | 60s inactivity | Clears HTTP session, kills pending ARM request |
| Network Status Broadcast | `LtxMessagingService()` | 60s | Broadcasts `SysStatus` + `SysInfo` via UDP |
| Network Status Timeout | `StatusTimeout()` | 90s silence | Clears expired network controller entries |
| GFS Debug Keep-alive | `LtxMessagingService()` | 30s | Writes "." to debug file to keep TCP connection alive |
| PostSave Timeout | `PostSave()` | 25s | Returns "timeout" to HTTP client |
| ProcessPost Timeout | `ProcessPost()` | 3s | Returns "busy" to HTTP client |
| ReplyToPost Timeout | `ReplyToPost()` | 3s (15s for FindBoard) | Returns "busy" to HTTP client |
| CGI Data Clear | `ClearCgiData()` | (triggered by watchdog) | Clears stale CGI variables |

---

## 8. Alarm / Warning Handling

### 8.1 Warning System (`LtxWarnings.c` — 755 lines)

~90 warning types, each with Status, Email flag, Sent tracker, Value[2] (bitmapped), and translated Msg string.

**Key Warning Functions:**

| Function | Purpose |
|---|---|
| `LtxWarningsBuild()` | Builds `AlarmData` string for UI from Warning[] array. |
| `WarningValueCheck()` | Handles bitmapped errors (equipment failures per-board, per-output), mode changes, load monitor bay alerts. |
| `BuildBitMappedErrors()` | Creates per-bit warning strings for: `SYSCONFIG_EQ`, `NO_OUTPUT`, `AUX`, `REFRIG_STAGE`, `REFRIG_DEFROST`, `HUMIDIFIER`, `LIGHTS`, `EXPANSIONBOARD`. |
| `LtxWarningsClear()` | Clears all warnings, sends `EMAIL_CLEAR`. |
| `LtxWarningsTranslate()` | Reads `languageAlarms.txt` — maps ~90 `WARN_*` names to translated UI strings. |
| `StoreWarningString()` | Maps warning name strings to Warning[] entries. |
| `StoreAlarmStrings()` | Stores ALARM_FAIL suffixes (board-specific failure descriptions). |
| `StoreSystemModes()` | Stores SYSTEM_MODES translations. |
| `EquipmentDescTranslate()` | Reads `languageEquipDesc.txt`, then sends all equipment/log/mode translations to ARM via serial (interrupts normal messaging by setting `UpgradingSoftware=1`). |
| `ProcessWarnings()` (in Parsers.c) | Parses warning data from multi-message: `index&status,value0,value1&index&...` format. Updates Warning[] array, triggers email. |

**Selected Warning Types:**

| Enum | Name | Description |
|---|---|---|
| `WARN_FAN_START` | Fan start failure | |
| `WARN_OVERTEMP` | Over-temperature alarm | |
| `WARN_UNDERTEMP` | Under-temperature alarm | |
| `WARN_HUMID_HIGH` | Humidity too high | |
| `WARN_HUMID_LOW` | Humidity too low | |
| `WARN_CO2_HIGH` | CO2 level high | |
| `WARN_MODECHANGE` | Operating mode changed | Email re-sent on each change |
| `WARN_ARMCOMM` | ARM communication lost | Set after 600s serial silence |
| `WARN_SOFTWAREUPDATE` | Software update in progress | |
| `WARN_FILEACCESS` | USB file access error | |
| `WARN_FILEWRITE` | USB file write error | |
| `WARN_FILENAME` | Invalid filename | |
| `WARN_SYSCONFIG_EQ` | System config equipment error (bitmapped) | |
| `WARN_NO_OUTPUT` | No output (bitmapped) | |
| `WARN_REFRIG_STAGE` | Refrigeration stage failure (bitmapped) | |
| `WARN_REFRIG_DEFROST` | Refrigeration defrost failure (bitmapped) | |
| `WARN_HUMIDIFIER` | Humidifier failure (bitmapped) | |
| `WARN_AUX` | Auxiliary failure (bitmapped) | |
| `WARN_LIGHTS` | Lights failure (bitmapped) | |
| `WARN_EXPANSIONBOARD` | Expansion board failure (bitmapped) | |
| `WARN_LOADMONITOR_BAY1..7` | Load monitor bay alarms | |

---

## 9. History / Logging

### 9.1 Log Data Flow

1. UI requests log data → POST forwarded to ARM
2. ARM sends `MultiMsg=<SessionID>,LogData,Key,Dates,TimeStamps,Data,Warnings` → starts sequence
3. ARM sends each tag's data as individual messages
4. On `Data` messages containing `EOR` (end-of-record): `SendRecordToFile()` writes record to USB via `GFSwrite()`, increments `LogMsgCounter`, frees `LogData` linked list
5. On `MultiEnd=LogData`: no special log action (logs handled per-record)
6. On malloc failure: sets `ArmReply = "overflow"`, frees memory, kills ARM request

### 9.2 Settings Save/Restore

**Save** (`StoreCgiVar` handles `SaveSettings` tag):
- Writes each settings line to USB via `GFSwrite()`
- On write failure: closes file, sets `WARN_FILEWRITE`, kills transmission

**Restore** (`PostSettingsRestore()`):
- Reads settings file from USB or FTP from another display
- Sends `FanRemoteOff=2` (remote off)
- Sends each line to ARM via `RelaySystemSettingsMessage()`
- Sends `FanRemoteOff=0` (remote on)

### 9.3 Debug Logging

- `PostOpenDebugFile()` opens a debug capture file on USB
- ARM `Debug` messages are written to the file via `GFSwrite()`
- `LtxMessagingService()` sends keep-alive writes every 30s
- Slave mode also logs master broadcast messages

### 9.4 Application Logging (`Logging.c` — 105 lines)

| Function | Purpose |
|---|---|
| `LoggingStart()` | Creates log files (`/tmp/Gellert*.log`, `/tmp/Gellert*.err`). Options: OFF, TO_CONSOLE, TO_FILE, REDIRECT_STDOUT. |
| `LoggingStop()` | Closes log files, restores stdout. |

---

## 10. Session Management

### 10.1 HTTP Sessions (`CallBacks.c`)

- **32 sessions max** (`NUM_HTTP_SESSIONS`)
- **60 second timeout** (`MAX_HTTP_SESSION_TIME`)

**Session Data** (per session):

| Field | Purpose |
|---|---|
| `ID` | Session index (0-31) |
| `ClientIP` | Remote client IP address |
| `PgmLevel` | Authorization level (-1 = not logged in, 0 = local password disabled) |
| `AuthStatus` | Authorization reply from ARM |
| `ArmReply` | ARM response string ("waitLTX" = pending) |
| `DataLoaded` | Data load status |
| `Timer` | Last activity timestamp |
| `LogMsgCounter` | Log records received counter |
| `LastRequest` | Last request timestamp |
| `RequestCount` | Total requests for this session |

**Session Functions:**

| Function | Purpose |
|---|---|
| `SessionSetup()` | Finds free session slot, initializes, returns SessionID |
| `SessionInfoFind()` | Finds/recreates session by SessionID |
| `SessionInfoInit()` | Initializes all 32 sessions |
| `SessionInfoWrite()` | Writes session info to HTTP response (HTML or JSON) |
| `SessionTimeout()` | Clears sessions inactive > 60s, kills pending ARM requests |
| `SessionShow()` | Debug dump of active sessions |
| `SessionFindIndex()` | Finds next available session slot |

---

## 11. IPC — Named Pipes (`Pipes.c` — 170 lines)

Named pipes (FIFOs) connect the three processes:

| Pipe | Direction | Purpose |
|---|---|---|
| `/tmp/GellertServer.fifo` (`URI_SERVER`) | Get/Post → Server | URI delivery |
| `/tmp/GellertGetToServer.fifo.<tid>.<id>` | Get → Server | HTTP info & response channel |
| `/tmp/GellertGetToClient.fifo.<tid>.<id>` | Server → Get | Response data |
| `/tmp/GellertPostToServer.fifo.<tid>.<id>` | Post → Server | POST body & HTTP info |
| `/tmp/GellertPostToClient.fifo.<tid>.<id>` | Server → Post | Response data |

**Pipe Functions:**

| Function | Purpose |
|---|---|
| `NamedPipeCreate()` | `mkfifo()` with 0777 permissions |
| `NamedPipeOpen()` | `open()` with specified flags |
| `NamedPipeRead()` | `select()` + `read()` with configurable timeout (default 30s) |
| `NamedPipeClose()` | `close()` |

---

## 12. Initialization (`LtxStartup.c` — 66 lines)

`LtxInit()` startup sequence:

1. `SerialPortInit()` — open serial port
2. `CgiDataInit()` — initialize CGI variable map
3. `LtxWarningsTranslate()` — load translated warning strings
4. `SessionInfoInit()` — initialize 32 HTTP sessions
5. `GetLocalIpConfig()` — read local IP, mask, MAC, gateway, HTTP port
6. `DeviceCommInit()` — start UDP P2P listener thread
7. `ArmMessagingInit()` — start ARM serial messaging thread
8. `LtxMessagingInit()` — start network monitoring thread

---

## 13. Equipment Description Transfer

When ARM communication is first established:

1. `ProcessMsgInit()` sends `Initialize=LTX&Version=1.07`
2. ARM replies with board type → stored in `BoardType` file
3. LTX requests equipment description transfer
4. ARM sends `fileStart=1` → LTX sends `ACK=fileStart`
5. `EquipmentDescTranslate()` reads `languageEquipDesc.txt`
6. `RelayEquipmentDescMessage()` sends 100+ translated strings to ARM covering:
   - **Equipment names** (EQ_Fan, EQ_Climacell, EQ_Humid1-3, EQ_Refrig, EQ_Heat, EQ_CavHeat, EQ_Burner, EQ_Door, EQ_Cure, EQ_Aux1-8, EQ_Lights1-2)
   - **Switch names** (SW_Fan, SW_Climacell, SW_Humid, SW_Refrig, SW_Door, SW_Heat, SW_CavHeat, SW_Burner, SW_Cure)
   - **PWM channels** (PWM_FanSpeed, PWM_ClimacellSpeed, PWM_BurnerOutput)
   - **Query tags** (sensor names, IO labels)
   - **System log record labels** (date, time, mode, equipment columns)
   - **System log equipment labels** (per-equipment column headers)
   - **System log remote labels** (remote logging columns)
   - **PID log labels**
   - **System modes** (fan/vent mode names, humid mode names)
   - **Board labels**, **Temp sensor names**, **Humid sensor names**, **Bay labels**
7. Normal ARM messaging resumes

---

## 14. Parsing (`Parsers.c` — 726 lines)

| Function | Purpose |
|---|---|
| `GetMessageTag()` | Extract tag from `tag=value` message |
| `GetMessageVar()` | Extract value from `tag=value` message |
| `GetMessage()` | Extract payload (before `$` terminator) |
| `GetMessageCRC()` | Extract CRC string (between `$` and `!`) |
| `ParseCgiVar()` | Extract nth comma-separated element from CGI variable |
| `GetMultiMsgElement()` | Extract element from multi-message linked list |
| `HttpDecodeQueryData()` | Parse `tag=value&tag=value` POST body into arrays (fixed-size) |
| `HttpDecodeQueryData2()` | Parse POST body into dynamically allocated arrays (for AgristarPost) |
| `HttpGetQueryVar()` / `HttpGetQueryVar2()` | Look up a field value by name |
| `HttpGetQueryVarInt()` / `HttpGetQueryVarInt2()` | Look up integer field value |
| `HttpFreeQueryVars2()` | Free dynamically allocated query vars |
| `GetLocalIpConfig()` | Reads local IP, mask, MAC, gateway, HTTP port from system |
| `ConvertToNum()` | Converts decimal string (e.g., "48.5") to integer (485) for comparison |
| `ConvertIp()` | Converts "a.b.c.d" string to 4-byte array |
| `IpAddNonZero()` | Checks if a 4-byte IP is non-zero |
| `ProcessWarnings()` | Parses warning multi-message data into Warning[] array |
| `StoreArmVersion()` | Stores ARM firmware version |
| `StoreLtxName()` | Stores controller storage name (from CGI_BASICSETUP) |
| `StoreLtxVersion()` | Stores LHS version string |

---

## 15. Complete File Index

### GellertServer/src/ (16 files)

| File | Lines | Purpose |
|---|---|---|
| `GellertServer.c` | 346 | Main entry point, pipe reader, GetServer/PostServer thread spawning |
| `ArmMessaging.c` | 1154 | Serial message state machine, ARM protocol handler, equipment desc relay |
| `CallBacks.c` | 2768 | GET/POST routing, session management, BuildCgiVars, JSON APIs, all POST handlers |
| `CGI.c` | 1577 | CGI variable cache, StoreCgiVar, JSON serialization, system monitor |
| `SerialComm.c` | 564 | Serial port init, circular buffer, message framing, CRC send |
| `LtxWarnings.c` | 755 | Warning management, translation, bitmapped errors, alarm building |
| `LtxMessaging.c` | 857 | Network monitoring, UDP status broadcast, master/slave relay |
| `DeviceComm.c` | 841 | UDP P2P, device discovery, ARM firmware programming |
| `GellertFileSystem.c` | 698 | GFS TCP client for USB file I/O (log, settings, profile, debug) |
| `MultiMsg.c` | 403 | Multi-message linked list storage and retrieval |
| `Email.c` | 324 | Email alert system |
| `Parsers.c` | 726 | Message parsing, CGI parsing, IP config, POST decoding |
| `IotMessaging.c` | 2187 | Azure IoT Hub integration (mostly dormant) |
| `ProgResponder.c` | 345 | Firmware upgrade orchestration |
| `LtxStartup.c` | 66 | Initialization sequence |
| `CRC.c` | 155 | CRC-32, delay utility |

### GellertServer/src/ Headers (18 files)

| File | Lines | Key Contents |
|---|---|---|
| `ArmMessaging.h` | ~60 | Timer defines, MSG_STATE_* enum, extern functions |
| `CallBacks.h` | ~100 | SESSION_DATA struct, GFS_OUTPUTFILE struct, GET_TYPE enum, NUM_HTTP_SESSIONS=32 |
| `CGI.h` | 434 | CGI_VARS enum (~70), MAIN_DATA/PGM_DATA/EQUIPMENT_STATUS enums, CGI_DATA struct |
| `CRC.h` | ~30 | CRC(), CheckCRC(), DelayThread(), DELAY_UNITS |
| `DeviceComm.h` | ~80 | Device discovery, firmware programming, UDP port defines |
| `Email.h` | ~30 | EMAIL_HEADER struct, port 9208, SendMail.sh path |
| `GellertFileSystem.h` | ~50 | GFS_O_* flags, GFS_OPEN_* types, GFSFile extern, GFS_TIMEOUT=60 |
| `IotMessaging.h` | ~40 | IoT function prototypes (mostly commented-out enums) |
| `JsonDataModels.h` | 264 | JSON field name arrays for MainData, PgmData, EquipStatus, OutputConfig |
| `LtxMessaging.h` | ~80 | NETWORK_STATUS struct, LTXMSG_BUFFER_SIZE, HttpClient struct, message delimiters |
| `LtxStartup.h` | ~30 | LHS_VERSION, BOARD_AS2/BOARD_AGRISTAR defines, LtxInit() |
| `LtxWarnings.h` | 450 | WARNING_ITEMS enum (~90), log label enums, UI mode defines, WARNING struct |
| `MultiMsg.h` | ~50 | MULTI_MSG_DATA struct, extern arrays for all multi-msg types |
| `Parsers.h` | ~40 | Parser function prototypes |
| `ProgResponder.h` | ~20 | ProgramSystemLauncher(), SendUpgradeMessage() |
| `SerialComm.h` | ~60 | Buffer sizes, DEBUG_LEVEL enum, MESSAGE_BUFFER/SERIAL_BUFFER structs |

### GellertGet/src/ (1 file)

| File | Lines | Purpose |
|---|---|---|
| `GellertGet.c` | 205 | FastCGI GET handler with named pipe IPC |

### GellertPost/src/ (1 file)

| File | Lines | Purpose |
|---|---|---|
| `GellertPost.c` | 351 | FastCGI POST handler with named pipe IPC |

### GellertHttpShared/src/ (4 files)

| File | Lines | Purpose |
|---|---|---|
| `Logging.c` | 105 | Log file management (stdout redirect, file logging) |
| `Logging.h` | ~30 | GELLERT_LOGFILE path, LOGGING_OPTIONS enum |
| `Pipes.c` | 170 | Named pipe create/open/read/close with timeouts |
| `Pipes.h` | ~30 | Pipe paths, MAX_POST_SIZE=600 |

**Total**: ~13,500 lines of C source across 20 implementation files and 18 headers.

---

## 16. Bridge Server Mapping

For the Node.js bridge server that replaces `gellertserverd`, the key responsibilities to replicate are:

| GellertServerD Responsibility | Bridge Equivalent |
|---|---|
| Serial port open/read/write at 230400 baud | TCP socket to QEMU ARM UART1 (tcp://10.0.2.2:9000) |
| `^tag=value$CRC!` framing | Same protocol over TCP |
| CRC-32 validation | Same CRC algorithm |
| RTS → ACK → SEND state machine | Same handshake protocol |
| CGI variable cache (70+ entries) | In-memory Map/Object cache |
| Multi-message linked lists | Same linked list / array accumulation |
| Session management (32 sessions, 60s timeout) | Session tracking |
| GET/POST routing to ARM | REST + WebSocket endpoints |
| Warning processing + alarm strings | Same algorithm |
| BuildCgiVars HTML response | CGI variable serialization |
| JSON APIs (GetAgristar, GetMobile) | REST JSON endpoints |
| Equipment description transfer | Same serial relay |

**NOT replaced by bridge** (still running in RPi5 QEMU guest):
- GellertFileSystem (port 9209) — USB file I/O
- GellertQueryResponder (port 9210) — UDP network discovery
- GellertEmailResponder (port 9208) — email alerts
- GellertProgResponder — firmware upgrades
- lighttpd + FastCGI (gellertgetd / gellertpostd)
- MonitorFtp.sh, NightlyRestart.sh
