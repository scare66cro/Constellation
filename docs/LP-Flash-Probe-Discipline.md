# LP-AM2434 Flash & Probe Discipline

> **Read before flashing any LP-AM2434 board.** The DSS toolchain has a
> well-known wrong-probe trap that has wasted hours on this project
> repeatedly. The rules in this doc exist to prevent it.

This doc covers the **mechanical workflow** for safely identifying,
enabling, disabling, flashing, and resetting LP-AM2434 boards via
their on-board XDS110 JTAG probes. For the bigger picture (live
architecture, what-runs-where), read
[`docs/System-State.md`](./System-State.md) first.

## 1. The wrong-probe trap (must understand before flashing)

DSS (`dss.bat`) **silently routes every JTAG operation to whichever
XDS110 enumerated first**, regardless of:

- the `S24Lxxxx` serial filter inside the `.ccxml` file
- the `XDS_USB_SERIAL_NUM` environment variable
- which `LP_CCXML` path is passed

This means: with **two or more XDS110 probes plugged in**, a
`Flash-LP.ps1 -Probe N` invocation can — and has — silently flashed
**CONTROLLER firmware onto the STORAGE board**. The symptom is hard
to detect because both boards then look "alive" on UART, but the
mis-flashed board runs the wrong role, doesn't accept Modbus
connections from rpi5, and the real CONTROLLER's `[ORBIT 0]` worker
fails to talk to it.

How to detect: read the UART0 boot trace from the suspect board. A
STORAGE board running CONTROLLER firmware will print:
```
[ORBIT 0] starting worker for 10.47.27.2:5502   ← itself!
[ORBIT 1] starting worker for 10.47.27.3:5502
[ORBIT 2] starting worker for 10.47.27.4:5502
[ORBIT 0] connect 10.47.27.2:5502 failed errno=103
```
A correctly-flashed STORAGE board prints Modbus server logs, never
`[ORBIT N]` worker logs.

**Recovery is mandatory** when wrong-probe corruption happens — see §5.

## 2. Probe ↔ board mapping (canonical)

Single source of truth:
[`/memories/repo/lan-ip-map.md`](/memories/repo/lan-ip-map.md). Quick
table:

| Probe | XDS Serial | App-UART COM | Aux COM | Role        | IP          |
|-------|-----------|--------------|---------|-------------|-------------|
| N     | S24L0707  | COM9         | COM10   | CONTROLLER  | 10.47.27.1  |
| A     | S24L0417  | COM5         | COM6    | STORAGE     | 10.47.27.2  |
| B     | S24L0707  | COM9         | COM10   | GDC         | 10.47.27.3  |
| P     | S24L0727  | COM7         | COM8    | GDC → TRITON | 10.47.27.3 → .4 |
| T     | S24L0957  | COM4         | COM3    | TRITON      | 10.47.27.4  |

COM port numbers depend on Windows USB enumeration order and may
shift if you re-plug. Verify with:

```powershell
Get-PnpDevice -Class 'Ports' -Status OK |
    Where-Object FriendlyName -match 'XDS110' |
    Select-Object FriendlyName, @{N='Parent';E={
        (Get-PnpDeviceProperty -InstanceId $_.InstanceId -KeyName DEVPKEY_Device_Parent).Data
    }} | Format-Table -AutoSize
```

The "Parent" column shows the XDS serial each COM port belongs to.

## 3. Probe enumeration & enable/disable

### 3.1 What the scripts do

- [`Flash-LP.ps1`](../Nova_Firmware/lp_am2434/Flash-LP.ps1) — build +
  OSPI-flash one role onto one probe's board. **Refuses to flash if
  the requested probe isn't the only one enumerated** (auto-runs
  `Set-Probe -Action Solo` to disable the others). Requires admin.
- [`Set-Probe.ps1`](../Nova_Firmware/lp_am2434/Set-Probe.ps1) —
  disable / enable / "solo" XDS110 probes at the Windows USB layer.
  Requires admin. The `Solo` mode disables every XDS110 except the
  named one.
- `ospi_flash/system_reset.js` — issues a SoC system reset via DSS so
  the SBL re-runs and loads the freshly-flashed image from OSPI.
  Reads target ccxml path from env var `LP_CCXML`.

### 3.2 The `.ccxml` files

Each board has its own ccxml because each carries the XDS serial
filter:

| File                         | Probe | Used by                          |
|------------------------------|-------|----------------------------------|
| `AM2434_LP_NOVA.ccxml`       | N     | `Flash-LP -Probe N`              |
| `AM2434_LP_A.ccxml`          | A     | `Flash-LP -Probe A`              |
| `AM2434_LP_B.ccxml`          | B     | `Flash-LP -Probe B`              |
| `AM2434_LP_P.ccxml`          | P     | `Flash-LP -Probe P` (Pulsar)     |
| `AM2434_LP_T.ccxml`          | T     | `Flash-LP -Probe T` (TRITON)     |
| `AM2434_LP.ccxml`            | (no serial filter — fallback)    |

**Do NOT invent a new ccxml name** (e.g. `AM2434_LP_STORAGE.ccxml`).
DSS will throw "Cannot read System Setup data from XML file" because
the file doesn't exist. When pointing `system_reset.js` at a board,
use the per-probe file:

```powershell
$env:LP_CCXML = 'F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP_A.ccxml'
& 'C:\ti\ccs2050\ccs\ccs_base\scripting\bin\dss.bat' `
   'F:\Constellation\Nova_Firmware\lp_am2434\ospi_flash\system_reset.js'
```

### 3.3 Recovering a probe stuck in Windows "Error" state

XDS110 USB devices occasionally land in `Error` state (driver crash,
USB power glitch, prior debug session left it locked). Symptoms:
`xdsdfu -e` doesn't list the serial; `Get-PnpDevice` shows
`Status=Error` for `USB\VID_0451&PID_BEF3\S24Lxxxx`.

Fix (admin PowerShell):

```powershell
Disable-PnpDevice -InstanceId 'USB\VID_0451&PID_BEF3\S24L0417' -Confirm:$false
Start-Sleep -Seconds 2
Enable-PnpDevice  -InstanceId 'USB\VID_0451&PID_BEF3\S24L0417' -Confirm:$false
```

After ~5s, status returns to `OK` and the COM ports re-appear. Run
under admin — the non-elevated form returns `HRESULT 0x80041001
Generic failure`.

## 4. Day-to-day flash workflow

**Always launch the flash script in an admin PowerShell window.** The
non-admin invocation fails on the first `Disable-PnpDevice` call.

```powershell
Start-Process powershell -Verb RunAs -ArgumentList '-NoExit', '-Command', `
  "cd F:\Constellation\Nova_Firmware\lp_am2434; .\Flash-LP.ps1 -Probe A"
```

`Flash-LP.ps1` auto-handles:
1. `xdsdfu -e` precheck — refuses to flash if requested probe isn't
   present, or if multiple probes are enumerated and `Set-Probe`
   can't isolate the target.
2. `Set-Probe -Action Solo` — disables all XDS110s except the target.
3. Build with `CONFIG_NOVA_LP_ROLE` and `CONFIG_NOVA_LP_IP` baked in
   for the role's defaults.
4. OSPI flash via `dss.bat uniflash_run.js`.
5. (Does NOT auto-issue system reset — run `system_reset.js`
   separately if you need the board to boot the new image
   immediately. Power-cycling works too.)

After flashing, **re-enable the other probes** so they're available
for the next session:

```powershell
.\Set-Probe.ps1 -Probe N -Action Enable
.\Set-Probe.ps1 -Probe B -Action Enable
```

Or simpler: just unplug-replug the USB Micro on each board.

### 4.1 Role override

`Flash-LP.ps1 -Probe A -Role STORAGE -Ip 10.47.27.2` — explicit role
+ IP. Required when:
- Bringing up a brand-new board (no defaults yet for that probe).
- Re-purposing a board to a different role.
- Recovering from wrong-probe corruption (see §5).

### 4.2 The `-WipeDevCfg` flag

OSPI has two device-config banks at `0x600000` / `0x610000` holding
the cached role + IP. Boot reads these via
`LpDeviceConfig_GetIp()` / `LpDeviceConfig_GetRole()`, **overriding
the compile-time defaults**. So reflashing firmware alone does NOT
change a mis-roled board — you must also wipe device-config:

```powershell
.\Flash-LP.ps1 -Probe A -Role STORAGE -Ip 10.47.27.2 -WipeDevCfg
```

Use `-WipeDevCfg` whenever:
- Recovering from wrong-probe (§5).
- Changing a board's role.
- A board boots with mystery wrong IP/role.

Routine firmware updates do NOT need `-WipeDevCfg` — preserving the
existing role/IP across an update is the correct default.

See [`/memories/repo/lp-role-change-reflash.md`](/memories/repo/lp-role-change-reflash.md)
for the deeper history.

## 5. Wrong-probe recovery procedure

When you suspect (or have confirmed via UART trace) that a board is
running the wrong role:

1. **Identify the offender.** Read each board's UART0 (App UART COM
   port at 115200 8N1) for ~30 s. CONTROLLER prints `[ORBIT N]
   poll #X OK`; orbit boards print Modbus server activity. If you
   find two boards both running CONTROLLER firmware, you have a
   wrong-probe.
2. **Physically isolate.** Run `Set-Probe -Probe <target> -Action Solo`
   to disable every XDS110 except the one belonging to the wrong-flashed
   board. Verify with `xdsdfu -e` — must show exactly one device with
   the intended serial.
3. **Reflash with `-WipeDevCfg`:**
   ```powershell
   .\Flash-LP.ps1 -Probe A -Role STORAGE -Ip 10.47.27.2 -WipeDevCfg
   ```
4. **Boot trace verification.** Open the App UART COM port (e.g.
   COM5 for Probe A) **before** issuing system reset, then run:
   ```powershell
   $env:LP_CCXML = 'F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP_A.ccxml'
   & 'C:\ti\ccs2050\ccs\ccs_base\scripting\bin\dss.bat' `
      'F:\Constellation\Nova_Firmware\lp_am2434\ospi_flash\system_reset.js'
   ```
   Watch for the role-correct boot trace (orbit boards print Modbus
   server startup, not `[ORBIT N]` worker startup).
5. **Re-enable other probes** via `Set-Probe -Action Enable` once the
   reflashed board is verified healthy.
6. **Cross-check from rpi5.** From rpi5: `ping 10.47.27.<IP>` must
   succeed. From CONTROLLER's UART trace: `[ORBIT N] poll #X OK` must
   appear within ~10 s.

## 6. Why this discipline matters

- **Wrong-probe wastes hours.** A misflash takes 30 s to commit and
  hours to diagnose because the board still appears "alive."
- **OSPI persistence amplifies the trap.** Without `-WipeDevCfg`, a
  reflash with the right role still boots with the cached wrong role
  because device-config wins over compile-time defaults.
- **No remote reset path exists yet.** OTA is designed
  ([`docs/LP-AM2434-OTA-Update-Plan.md`](./LP-AM2434-OTA-Update-Plan.md))
  but not implemented. Recovery requires physical USB access to the
  affected board.

## 7. Cross-references

- [`docs/System-State.md`](./System-State.md) §3.1 — origin of the
  wrong-probe trap (Phase-C bringup history).
- [`docs/LP-AM2434-Hardware-Bringup-Plan.md`](./LP-AM2434-Hardware-Bringup-Plan.md)
  — full bringup chronology.
- [`/memories/repo/lp-am2434-bringup.md`](/memories/repo/lp-am2434-bringup.md)
  — end-to-end "bring a new board to life" recipe with all known
  pitfalls.
- [`/memories/repo/lp-am2434-ospi-uniflash.md`](/memories/repo/lp-am2434-ospi-uniflash.md)
  — JTAG auto-flasher mechanics + SBL XMODEM partial-flash trap.
- [`/memories/repo/lp-role-change-reflash.md`](/memories/repo/lp-role-change-reflash.md)
  — OSPI device-config wipe procedure.
- [`/memories/repo/lp-device-config-ospi.md`](/memories/repo/lp-device-config-ospi.md)
  — OSPI device-config bank layout.
- [`/memories/repo/lan-ip-map.md`](/memories/repo/lan-ip-map.md) —
  canonical Probe ↔ role ↔ IP ↔ COM map.
