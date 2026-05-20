# Constellation — Current System State

> **tl;dr** — How the system is wired today. The live bench rig, the
> day-to-day developer workflow, and the hard lessons from the Phase-C
> bringup (May 2026). For the doc map ("where do I find X?") go to the
> repo-root [`CLAUDE.md`](../CLAUDE.md). For protocol / save-path
> invariants go to [`firmware-bridge-protocol.md`](firmware-bridge-protocol.md).
>
> **Last updated:** 2026-05-07 (Phase-C complete; Phase-D in progress).

---

## 1. The system, in one diagram

```
┌────────────────────────────────────────────────────────────────────────┐
│ Workstation (Windows PC)                                               │
│  • Flash-LP.ps1   (JTAG flash via XDS110)                              │
│  • Load-Nova.ps1  (RAM load, no OSPI write)                            │
│  • npm run build + WSL deploy.sh  (UI → rpi5)                          │
│  • scp constellation-ui/server/src → rpi5  (bridge code)               │
│  • Workstation: 10.47.27.10/24 on Ethernet 2 (updated 2026-05-20)      │
└──────────────┬─────────────────────────────────────────┬───────────────┘
               │ ssh / scp (10.47.27.108)                │ JTAG / UART0 console
               ▼                                         ▼
┌──────────────────────────────────────────┐   ┌───────────────────────┐
│ rpi5 (gellert@10.47.27.108)              │   │ Bench LP-AM2434       │
│  • lighttpd :80 proxy → :9001            │   │  XDS110 USB-CDC       │
│  • uisvelte.service :3000                │   │  (flash + UART0       │
│  • agristar-bridge.service :9001         │   │   debug log)          │
│      npx tsx src/index.ts                │   └───────────────────────┘
│      transparent COBS+CRC ↔ proto        │
│      stream forwarder                    │
│  • UART2 = /dev/ttyAMA0 @ 921600         │
└──────────────┬───────────────────────────┘
               │ COBS+CRC envelope, protobuf payload
               ▼
┌────────────────────────────────────────────────────────────────────────┐
│ CONTROLLER LP-AM2434 (Probe N, S24L0707)  10.47.27.1                   │
│  • Owns OSPI settings + device-config banks                            │
│  • Modbus TCP master → STORAGE / GDC / TRITON                          │
│  • Equipment control state machines (legacy AS2 cadence preserved)     │
└──────────┬─────────────────┬─────────────────────┬─────────────────────┘
           │ Modbus TCP :502 │                     │
           ▼                 ▼                     ▼
   STORAGE 10.47.27.2    GDC 10.47.27.3      TRITON 10.47.27.4
   (Probe A, S24L0417)   (Probe B = N's hw,  (Probe T, S24L0957
                          or Probe P=S24L0727 flashed 2026-05-20)
                          Pulsar)
```

The same `nova_lp.release.mcelf.hs_fs` binary runs on every probe. Role /
IP / board-id are written to OSPI device-config at flash time by
`Flash-LP.ps1` and read at boot by `LpDeviceConfig_Get*()`.

---

## 2. The transparent bridge + proto-direct UI

Destination architecture — **no CSV translation in the bridge for new code.**

### 2.1 Three transport surfaces between UI and bridge

| Surface | Direction | Purpose |
|---------|-----------|---------|
| `/proto/stream` (WS) | bridge → UI | Binary frames `[u16 LE tag][u32 LE len][payload]`. One frame per envelope-tag protobuf message (e.g. tag 10 = `SystemStatus`). Initial-snapshot replay on connect. JSON control plane piggybacks the same socket. |
| `/proto/write/:settingsField` (POST) | UI → bridge | Encoded `SettingsUpdate` for one field. Bridge wraps in COBS+CRC and forwards to firmware. |
| `/iot/*` (REST) | UI ↔ bridge | Legacy CSV channels — **shrinking.** New pages must not add entries. |

UI helpers:
- [`useDraft<K>(store, tag)`](../constellation-ui/src/lib/business/useDraft.ts)
  — single-message settings-page boilerplate.
- [`writeProto(tag, msg, opts?)`](../constellation-ui/src/lib/business/protoWrite.ts)
  — encodes + POSTs. `opts.forceVarints` is the proto3-zero escape.
- [`protoStores.ts`](../constellation-ui/src/lib/business/protoStores.ts)
  + composites (`frontMatterComposite`, `headerComposite`,
  `equipmentComposite`) replace the old per-channel WS handlers.

### 2.2 Bridge responsibilities

The bridge (`constellation-ui/server/`) is now a **stateless transport
gateway** plus a tiny set of bridge-emitted tags (200–209, e.g. VFD
status from a Modbus client).

Hard rules:

- **No data fix-ups in the bridge.** No unit conversion, scaling,
  default-substitution. Conversion to user units happens exactly once,
  in `Nova_Firmware/Platform/nova_thread_overrides.c::ReadAnalogBoards()`.
- **`legacyShim.ts` is shrinking, not growing.** When the last UI page
  reading a CGI variable is migrated, the producer in `dataCache.ts`
  must be deleted in the same commit.
- **TX from bridge to firmware always goes through the COBS framer.**
  Bypassing the framer corrupts CRC and produces an error storm visible
  at `/health`.

Full rules: [`proto-direct-redesign-plan.md`](proto-direct-redesign-plan.md).

---

## 3. Hard lessons from Phase-C bringup (2026-05-02)

These cost hours each. Read them before flashing or chasing an
"impossible" boot bug.

### 3.1 The DSS wrong-probe trap

With **two or more XDS110 probes** plugged in, DSS silently routes
**every** JTAG operation to whichever probe enumerated first, regardless of:
- the `S24Lxxxx` serial filter inside the `.ccxml`
- the `XDS_USB_SERIAL_NUM` environment variable
- which `LP_CCXML` path is passed

We saw `Flash-LP -Probe N` repeatedly write CONTROLLER firmware to
**STORAGE** because STORAGE's S24L0417 enumerated first. Recovery cost a
full reflash of two boards.

**Permanent guard** is already in
[`Flash-LP.ps1`](../Nova_Firmware/lp_am2434/Flash-LP.ps1):

```powershell
$present = & xdsdfu.exe -e | Select-String 'Serial:\s+(\S+)' ...
if ($present.Count -gt 1 -and -not $Force) {
    throw "Refusing to flash: multiple XDS110 probes attached. Unplug all but $TargetSerial."
}
if ($present -notcontains $TargetSerial) {
    throw "Probe $TargetSerial not present (saw $present)."
}
```

The `-Force` override exists but **does NOT bypass DSS's bad routing** —
it only silences the script's check. Do not use it.

**Mandatory recovery procedure when wrong-probe corruption happens:**

1. Physically unplug the wrong board's USB cable.
2. Run `xdsdfu.exe -e` — must show exactly `Found 1 device` with the
   intended serial.
3. `Flash-LP.ps1 -Probe <X> -Role <ROLE> -Ip <IP> -WipeDevCfg` — the
   `-WipeDevCfg` flag erases OSPI device-config banks at 0x600000 /
   0x610000 so the board doesn't boot with the wrong cached role.
4. `dss .\system_reset.js .\AM2434_LP_NOVA.ccxml` — verify boot trace on
   UART0 (XDS110 COM port, 115200 8N1).
5. Plug the other board back in only after the first one is confirmed
   healthy.

The `xdsdfu.exe` binary lives at
`C:\ti\ccs2050\ccs\ccs_base\common\uscif\xds110\xdsdfu.exe`. The script
auto-locates it; if missing, install CCS.

### 3.2 Two LP boards, one default MAC

Both LP-AM2434 boards ship with TI default MAC `34:10:5d:68:dc:03` (no
per-board EEPROM MAC fuse). **IP — written into OSPI at flash time — is
the only safe board identifier.** Never trust ARP / MAC for "which board
is this".

### 3.3 proto3 varint canary pitfall

A single-byte canary `0xC1` written into a uint32 protobuf field violated
varint encoding (high bit set ⇒ continuation expected). The UI decoder
consumed the next field's tag byte as the rest of the varint and threw
`RangeError: premature EOF`.

Lesson: any hand-crafted varint canary must be `< 128` (single clean
byte) **or** properly encoded with continuation bytes. We ultimately
stripped the canary entirely; the smoke-test now relies on real sensor
floats reaching the UI.

### 3.4 Pre-scheduler boot discipline

Two boot bugs from the same sitting (full memory note:
[`memories/repo/lp-am2434-lwip-sys-init.md`](../memories/repo/lp-am2434-lwip-sys-init.md)):

1. **No `DebugP_log` before `vTaskStartScheduler`.** It internally takes
   a FreeRTOS mutex; pre-scheduler the mutex is in an undefined state and
   the R5F data-aborts. Use `bb_uart0_putc` / `bb_uart0_puts` (bit-bang
   to UART0 THR after polling LSR) for any pre-scheduler logging.
2. **Call `sys_init()` before `vTaskStartScheduler`** if any task
   created during `OrbitGdc_Init` / `OrbitTriton_Init` touches lwIP heap
   macros. The SDK's prebuilt lwIP expects a mutex normally created later
   inside `tcpip_init()`, but the pre-empted orbit task reaches lwIP
   first and asserts at `sys_arch.c:156`.

### 3.5 XDS110 USB-CDC race on early-boot logs

Bytes printed to UART0 before the host opens its COM port are silently
dropped. Open the SerialPort in PowerShell **first**, then trigger
`system_reset.js` in a `Start-Job`. Without this, you'll mis-bisect
"the abort is before `main()`" when the abort is actually mid-init and
the early markers were dropped.

---

## 4. Ethernet — getting it actually working

Production custom Nova hardware will have a clean RGMII PCB layout and
gigabit will Just Work. **On the LP-AM2434 launchpad it does not.** Lessons
that are *not* obvious:

### 4.1 Forced 100M-FD downshift is mandatory on LP

Symptoms at gigabit (per `lwip_smoke.c::gigabit_diag`):
- L1 PHY-side healthy: `1000BT_STS local_rcvr=OK remote_rcvr=OK idle_err=0`
- All L2 socket probes fail with `errno=103` to gateway, rpi5, PC
- Identical breakage in auto-slave AND forced-master roles

Root cause: MAC↔PHY RGMII trace timing margin on the LAUNCHXL board at
125 MHz DDR. **Not firmware-fixable.** RGMIICTL clock-delay enable bits
are locked out on this board (any change breaks RX entirely — see
Update 6 of `lp-am2434-cpsw-tx-debug.md`).

Firmware ships with `LP_GIGABIT_DIAG=0` →
`downshift_phy_to_100m_aneg()` runs at every boot. Modbus polling has
zero gigabit need. To re-validate gigabit on a future production PCB:
flip `LP_GIGABIT_DIAG=1` in `lwip_smoke.c`, rebuild, run the diag once,
restore.

Full chronology + gigabit-diag interpretation:
[`memories/repo/lp-am2434-cpsw-tx-debug.md`](../memories/repo/lp-am2434-cpsw-tx-debug.md).

### 4.2 Workstation reaches both LANs without adapter swapping

Add the office IP as a secondary on the same NIC that's already on the
airgapped LAN:

```powershell
# admin PowerShell, once per PC
New-NetIPAddress -InterfaceAlias 'Ethernet 2' `
    -IPAddress 10.1.2.135 -PrefixLength 24
```

Now `Ethernet 2` carries `10.47.27.100/24` (airgapped) **and**
`10.1.2.135/24` (office). The airgapped switch uplinks to the office
switch, so a single cable reaches both subnets. No adapter dance.

### 4.3 LP needs LAN before bridge work even starts

Cold-boot should produce on UART0:

```
ENET LWIP App → MAC port 1 open → PHY 3 alive → PHY 15 alive →
SAFEMODE comm restored → [MBT] socket() retrying ...
```

If `PHY 3 alive` doesn't appear within ~3 s of `vTaskStartScheduler`,
you have a hardware/cable issue, not a firmware issue.

CONTROLLER → orbit Modbus polling success looks like:

```
[ORBIT 0] poll OK  HR[200]=225 HR[201]=235 ...
```

### 4.4 Per-board IP is in OSPI device-config, not source

`Flash-LP.ps1 -Probe N -Role CONTROLLER -Ip 10.47.27.1 -WipeDevCfg`
writes the role + IP into OSPI device-config (banks at 0x600000 and
0x610000). The firmware reads these at boot via `LpDeviceConfig_GetIp()`.
Reflashing firmware **without** `-WipeDevCfg` preserves the existing
role/IP — useful for routine firmware updates, dangerous after a
wrong-probe incident.

---

## 5. Day-to-day developer workflow

### Local edit → board (firmware)

1. Edit firmware in `Nova_Firmware/lp_am2434/` (or `Nova_Firmware/Platform/`).
2. `cd Nova_Firmware\lp_am2434; gmake PROFILE=release`
3. Verify only the intended XDS110 is plugged in; `xdsdfu.exe -e` shows
   `Found 1 device. Serial: <expected>`.
4. `.\Flash-LP.ps1 -Probe N` (or A / B). `-WipeDevCfg` only if changing
   the board's role.
5. `dss .\system_reset.js .\AM2434_LP_NOVA.ccxml` (cold boot from OSPI).
   Watch UART0 at 115200 8N1 for the boot trace.

### UI / bridge code → rpi5

1. UI: `cd constellation-ui; npm run build` then
   `wsl bash -c "cd /mnt/f/Constellation/constellation-ui && ./deploy.sh --target=production"`.
2. Bridge: `scp constellation-ui/server/src/foo.ts gellert@10.47.27.108:/home/gellert/Gellert/constellation/constellation-ui/server/src/foo.ts`
   then `ssh gellert@10.47.27.108 'sudo systemctl reset-failed agristar-bridge && sudo systemctl restart agristar-bridge'`.
3. Tail bridge log: `bash F:\Constellation\_get_bridge_log.sh`.
4. Sanity: `curl http://10.47.27.108/health` → `rxCrcErrors:0` after any
   write campaign.

### Verify a save round-trip

Save → cold-boot the LP (`system_reset.js`) → reload the UI page → value
persists. `/health` clean throughout.

---

## 6. Section update protocol

When something in this doc becomes wrong:

- **Architecture / transport / write path** → §2 lives in
  [`proto-direct-redesign-plan.md`](proto-direct-redesign-plan.md);
  update there and re-summarize here.
- **New flashing / hardware lesson** → append to the relevant
  `memories/repo/lp-am2434-*.md`, then add a one-line bullet to §3 here.
- **Ethernet** → append to
  [`memories/repo/lp-am2434-cpsw-tx-debug.md`](../memories/repo/lp-am2434-cpsw-tx-debug.md);
  only update §4 if the production-vs-LP guidance itself changes.
- **New doc** → add a row to the doc map in [`CLAUDE.md`](../CLAUDE.md),
  not here.
