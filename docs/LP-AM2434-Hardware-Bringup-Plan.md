# LP-AM2434 LaunchPad — Hardware Bringup Plan

> **Status:** **4-board fleet LIVE on bench** (2026-05-30) — CONTROLLER
> + STORAGE + GDC + TRITON all running 0.A.208. Bringup work is
> complete; this doc is now a reference + the historical bringup
> record. See [`docs/System-State.md`](System-State.md) for the live
> operational view and
> [`docs/lp-am2434-network-production-requirements.md`](lp-am2434-network-production-requirements.md)
> for what the custom production PCB needs to drop the 100M-FD
> downshift workaround.
>
> The pre-LP simulator stack (TS armSimulator + serialBridge + QEMU
> AM2434 machine model) is **deleted**. There is no `QEMU_BUILD` flag,
> no `am2434_qemu_*.ld` linker. The same `nova_lp.release.mcelf.hs_fs`
> binary runs on every probe — role/IP live in OSPI device-config.

The detailed bench debugging history (Phase C gigabit downshift saga,
2026-05-01 boot-sequence bug fixes, etc.) lives at the bottom under
[History](#history). The body of this doc describes the current live
configuration.

## 1. Hardware overview

**Board:** TI LP-AM2434 (LAUNCHXL-AM243x). [ti.com/tool/LP-AM2434](https://www.ti.com/tool/LP-AM2434).

| Feature | Detail |
|---------|--------|
| **CPU** | 4x Cortex-R5F @ 800 MHz (firmware uses R5FSS0_0 main core + R5FSS1_0 watchdog core) |
| **Memory** | 2 MB MSRAM + 64 KB ATCM + 64 KB BTCM per core |
| **OSPI Flash** | On-board Cypress S25HL512T — settings ping-pong banks + lp_device_config + watchdog + OTA Bank A/B + RESERVED metadata block. OSPI map in `Nova_Firmware/lp_am2434/nova_fw_update.h`. |
| **Ethernet** | 2x RJ45 (CPSW PHY 3 + ICSSG PHY 15). Forced 100M-FD by firmware (see Phase C history). |
| **UART** | UART0 over XDS110 USB (debug console), UART2 on header J1-3/J1-4 (bridge link, 921600 baud) |
| **Boot** | OSPI permanently. SW4: switches **2 and 6 ON** by default; **PCB rev 109A wires non-standardly (1+2 ON for OSPI boot)** — verify against the silkscreen. JTAG over USB-C works regardless. |

## 2. Live setup (May 2026)

### Office bench rig

Four LP boards on an airgapped 10.47.27.0/24 LAN, plus the rpi5 panel:

| Role        | Probe | XDS Serial  | UART  | IP            | Role byte |
|-------------|-------|-------------|-------|---------------|-----------|
| CONTROLLER  | N     | S24L0707    | COM9  | 10.47.27.1    | 0         |
| STORAGE     | A     | S24L0417    | COM5  | 10.47.27.2    | 1         |
| GDC         | P     | S24L0727    | COM7  | 10.47.27.3    | 2         |
| TRITON      | T     | S24L0957    | COM4  | 10.47.27.4    | 3         |
| rpi5 alias  | n/a   | n/a         | n/a   | 10.47.27.108  | —         |

The rpi5 (`gellert@10.47.27.108`) talks to CONTROLLER over UART2:
`/dev/ttyAMA0` @ 921600 baud, 8N1, COBS+CRC envelope, protobuf payloads.
CONTROLLER then polls STORAGE/GDC/TRITON by Modbus TCP on `10.47.27.x:502`.

```
PC (Windows)               rpi5 (10.47.27.108)            LP boards (10.47.27.x)
+---------------+ ssh      +--------------------------+   +-------------------+
| Flash-LP.ps1  | ------>  | agristar-bridge.service  |U2 | CONTROLLER N      |
| Build-Cfu.ps1 |          | npx tsx src/index.ts     |-->| 10.47.27.1        |
| deploy.sh     |          |   /proto/stream :9001    |   |                   |
+---------------+          |   /iot/* :9001           |   +--+-----------+----+
                           | uisvelte.service :3000   |      | Modbus TCP :502
                           | lighttpd :80 (proxy)     |      v
                           +--------------------------+   +--+----------------+
                                                          | STORAGE A   .2    |
                                                          | GDC     P   .3    |
                                                          | TRITON  T   .4    |
                                                          +-------------------+
```

## 3. Build / flash / reset commands

All run from a Windows PC; no office trip required (JTAG-flashing
through the work PC is the daily flow).

| Operation                    | Command                                                                                              |
|------------------------------|------------------------------------------------------------------------------------------------------|
| Build LP firmware            | `cd F:\Constellation\Nova_Firmware\lp_am2434; gmake PROFILE=release`                                 |
| Build + JTAG flash a board   | `cd F:\Constellation\Nova_Firmware\lp_am2434; .\Flash-LP.ps1 -Probe N` (or A / P / T)                |
| Skip build, just flash       | `.\Flash-LP.ps1 -Probe N -SkipBuild`                                                                 |
| Build universal `.cfu`       | `.\Build-Cfu.ps1` — produces `firmware-bundles\constellation-0.A.<n>.cfu` plus the universal `nova_lp.release.mcelf.hs_fs` (no role/IP baked in) |
| Cold-boot LP after flash     | `cd ospi_flash; & "C:\ti\ccs2050\ccs\ccs_base\scripting\bin\dss.bat" .\system_reset.js F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP_<X>.ccxml` |
| Tail bridge log on rpi5      | `bash F:\Constellation\_get_bridge_log.sh`                                                           |
| Restart bridge on rpi5       | `bash F:\Constellation\_restart_bridge.sh`                                                           |
| Build SvelteKit UI           | `cd F:\Constellation\constellation-ui; npm run build`                                                |
| Deploy UI to rpi5            | `wsl bash -c "cd /mnt/f/Constellation/constellation-ui && ./deploy.sh --target=production"`          |
| Deploy bridge to rpi5        | `bash F:\Constellation\rpi5\_deploy_bridge_to_pi5_hw.sh`                                             |

`Flash-LP.ps1` enforces a per-probe XDS110-serial guard (`xdsdfu -e`):
it refuses to flash unless the requested probe is the **only** one
enumerated, unless `-Force` is passed. This prevents writing CONTROLLER
firmware to the STORAGE board, etc. See invariant #7 and
[`docs/LP-Flash-Probe-Discipline.md`](LP-Flash-Probe-Discipline.md).

## 4. JTAG load (live debug, no OSPI write)

For interactive R5F debug, leave SW4 on OSPI permanently and load from
RAM via the XDS110:

```powershell
# build + JTAG load + run
F:\Constellation\Nova_Firmware\lp_am2434\Load-Nova.ps1
# load existing .out without rebuild
F:\Constellation\Nova_Firmware\lp_am2434\Load-Nova.ps1 -SkipBuild
```

The script uses DSS to init the DMSC, load `nova_lp.release.out` to
R5F0_0, and run. Firmware lives in RAM until next power cycle.

CCS gotchas (2026-04-23):

1. CCXML must be generated by the `TargetConfigurationGenerator` API —
   hand-written XML triggers DTD parser errors.
2. `createConfiguration("AM2434_LP")` makes a 0-byte file; pass
   `"AM2434_LP.ccxml"` with extension.
3. `sciclient_ccs_init` ends with `while(1)` — `target.run()` blocks
   forever; use `target.runAsynch()` + `Thread.sleep(5000)` + `halt()`.
4. The XDS110 USB-CDC race: bytes printed before any host opens COM
   are silently dropped. Open the SerialPort in PowerShell **first**,
   then trigger `system_reset.js` in a Start-Job. Without this, early
   boot markers look missing and you'll mis-bisect.

## 5. UART flash (recovery — full-cycle bringup or chip-state recovery)

```powershell
# Set LP SW4 to UART boot: switches 1, 2, 3 ON
F:\Constellation\.venv\Scripts\python.exe `
    C:\ti\mcu_plus_sdk_am243x_12_00_00_26\tools\boot\uart_uniflash.py `
    -p COM9 `
    --cfg=C:\ti\mcu_plus_sdk_am243x_12_00_00_26\tools\boot\flash_sbl_ospi_commission.cfg
# Writes SBL + PHY tuning + universal nova app in ~110s
# Set SW4 back to OSPI: switches 2 and 6 ON (or 1+2 on PCB 109A). USB cycle.
```

Use this when a chip needs full UART-side commissioning — e.g. after a
NV-register brick recovery or first-time boot. Recipe is
[`memories/repo/lp-am2434-s24l0707-full-recovery-recipe.md`](../memories/repo/lp-am2434-s24l0707-full-recovery-recipe.md).

There is also [`rpi5/flash-lp.sh`](../rpi5/flash-lp.sh) to reflash the
CONTROLLER LP from the rpi5 over `/dev/ttyAMA0` if the JTAG path is
unavailable.

## 6. Required pre-scheduler discipline (firmware)

Two boot bugs were isolated in 2026-05 that bite anyone touching
`main.c` early-init:

1. **No `DebugP_log` before `vTaskStartScheduler`.** It internally takes
   a FreeRTOS mutex; the scheduler hasn't started, the mutex is in an
   undefined state, and the R5F data-aborts. Use `bb_uart0_putc` /
   `bb_uart0_puts` (bit-bang UART0 directly to the THR after polling
   LSR) for any pre-scheduler logging. `bb_uart0_puts` is intentionally
   non-static so generated SysConfig files can call it for early
   `Board_driversOpen` markers.
2. **Call `sys_init()` before `vTaskStartScheduler`** if any task
   created in `OrbitGdc_Init` / `OrbitTriton_Init` / etc. touches lwIP
   heap macros. The SDK's prebuilt lwIP library uses
   `LWIP_FREERTOS_SYS_ARCH_PROTECT_USES_MUTEX=1`; that mutex is
   normally created by `tcpip_init()` deep inside the enet task, but
   pre-scheduler tasks reach lwIP first and assert. `sys_init()` itself
   only allocates a recursive mutex via heap_4 — safe pre-scheduler.
   See [`memories/repo/lp-am2434-lwip-sys-init.md`](../memories/repo/lp-am2434-lwip-sys-init.md).

## 7. Settings persistence (OSPI ping-pong banks)

`lp_settings_store.c` ↔ `LpSettings_*` API. Two banks alternate; cold
boot picks the higher-sequence valid bank. There is **no** JSON
fallback file — OSPI is the only persistence layer.

`lp_device_config.c` owns the role / IP / board-id record (also OSPI,
banks at 0x600000 / 0x610000). `Flash-LP.ps1 -Probe X` writes both the
firmware and the role/IP defaults for that probe so a freshly-flashed
board boots into its intended role.

## 8. Pinout (header J1)

UART2 (bridge link to rpi5):

| LP-AM2434 | rpi5 GPIO header | Direction               |
|-----------|------------------|-------------------------|
| J1-3 (UART2_RXD) | Pin 8 (GPIO14/TXD)  | Bridge → Nova           |
| J1-4 (UART2_TXD) | Pin 10 (GPIO15/RXD) | Nova → Bridge           |
| J3-22 (GND)      | Pin 6 (GND)         | Common ground           |

Both are 3.3 V — direct connection, no level shifter.

GPIO budget for orbit equipment I/O is documented per role in
[`docs/triton-grc-port-plan.md`](triton-grc-port-plan.md) and
[`docs/firmware-equipment-control.md`](firmware-equipment-control.md).

## 9. AM2434 ↔ AM2432 compatibility

Firmware built against AM2434 R5F0_0 is binary-compatible with the
AM2432 used on production orbits — same ISA, FPU, peripheral set,
SDK, and compiler flags. Moving to a custom orbit PCB only changes
SysConfig pin assignments and external component differences.

---

## History

### 2026-05-29 — Phase 4 OTA end-to-end VALIDATED

After 8 firmware iterations (0.A.198-207) chasing a controller
self-update brick, 0.A.208 finally landed the seventh-layer fix
(`FW_HEADER_OFFSET` relocation from `0x60000` to `0x300000` to
prevent `write_meta_block_atomic` from erasing the SBL tail in the
shared 256-KB block). All four boards installed cleanly via multi-orbit
OTA, `.1 active=bankB banks=AB`. Memory:
[`memories/repo/sbl-wipe-controller-self-update-7th-layer-2026-05-29.md`](../memories/repo/sbl-wipe-controller-self-update-7th-layer-2026-05-29.md).

### 2026-05-21 — Universal binary + lp_device_config persistence

End-to-end OTA bench-validated with a universal binary (no
`CONFIG_NOVA_LP_ROLE` / `CONFIG_NOVA_LP_IP` baked in). Four compounding
layers found and fixed; full table in
[`docs/lp-am2434-ota-hardening-plan.md`](lp-am2434-ota-hardening-plan.md)
§ "Final bench validation 2026-05-21".

### 2026-05-15 — OSPI DAC-mode write rule discovered

Three days of "stochastic byte loss on retry" was a single missing
pattern: `hal_flash_dac_pp_one` was toggling the DAC bit between
sub-PPs. Canonical rule + 3-day debug trail:
[`docs/lp-am2434-ospi-dac-writes.md`](lp-am2434-ospi-dac-writes.md).
Invariant #11 in [`CLAUDE.md`](../CLAUDE.md).

### 2026-05-01 — Boot fully resolved on Probe C (role=GDC)

Two distinct boot bugs were isolated in the same sitting:

**Bug 1 — pre-scheduler `DebugP_log` data-aborts.** The trace stopped
mid-init and the R5F landed in `HwiP_user_data_abort_handler_c`. Root
cause: `DebugP_log` takes a FreeRTOS mutex internally; pre-scheduler
the mutex is in an undefined state. Fix: bit-bang UART0
(`bb_uart0_puts`) for any pre-scheduler logging. Earlier bisects
mistakenly concluded the abort was *before* `main()` because the
XDS110 USB-CDC race silently dropped the early bytes.

**Bug 2 — lwIP `sys_arch_protect_mutex != NULL` assert.** ~2.4 s after
scheduler start, after PHYs came alive but before any traffic, lwIP
asserted at `sys_arch.c:156`. Root cause: the SDK's prebuilt lwIP
library expects `sys_init()` to have created a recursive mutex; that
normally happens inside `tcpip_init()` deep in the enet task, but the
`orbit_modbus_tcp` server task created by `OrbitGdc_Init` pre-empts
and reaches lwIP heap macros first. Fix: call `sys_init()` explicitly
from `main.c` right after role logging, **before**
`vTaskStartScheduler`. Memory note:
[`memories/repo/lp-am2434-lwip-sys-init.md`](../memories/repo/lp-am2434-lwip-sys-init.md).

### 2026-04-27 — Phase C closed (gigabit downshift permanent)

Per-board diagnostic (`Nova_Firmware/lp_am2434/lwip_smoke.c::gigabit_diag`)
proved at gigabit:

- L1 PHY-side healthy: `1000BT_STS local_rcvr=OK remote_rcvr=OK idle_err=0`
- Both auto-slave AND forced-master roles: identical L1-clean / L2-broken
- All 3 L2 probes (gateway, rpi5, PC) fail with `errno=103`
- Switching to 100M-FD via `downshift_phy_to_100m_aneg()` makes
  everything work — orbits poll at 1 Hz for hours

Root cause: MAC↔PHY RGMII trace timing margin on the LAUNCHXL board at
125 MHz DDR. **Not firmware-fixable.** RGMIICTL clock-delay enable bits
are also locked out on this board (Update 6 of cpsw-tx-debug.md proved
they break RX completely).

`lwip_smoke.c` ships with `LP_GIGABIT_DIAG=0` →
`downshift_phy_to_100m_aneg()` runs at every boot. Modbus polling has
zero gigabit need. Production custom Nova hardware (clean RGMII trace
layout) will not have this issue — gigabit can be re-validated by
setting `LP_GIGABIT_DIAG=1` once production PCBs are fabbed. Full
chronology in
[`memories/repo/lp-am2434-cpsw-tx-debug.md`](../memories/repo/lp-am2434-cpsw-tx-debug.md).

Production-PCB requirements to drop the downshift entirely:
[`docs/lp-am2434-network-production-requirements.md`](lp-am2434-network-production-requirements.md).

### 2026-04-25 — Triton write path landed

Bridge-side `OrbitClient` deleted (no Modbus client in the bridge
anymore — transparent transport gateway). Modbus master responsibility
lives entirely on the LP-AM2434.

- Proto: envelope tag **125** = `TritonRegWrite { slot, addr, value }`
  with all three fields force-encoded (0 is meaningful).
- LP firmware: `OrbitClient_WriteHoldingRegister(slot, addr, value)` in
  `orbit_client.c` — dedicated short-lived `lwip_socket`/`lwip_connect`
  (NOT the polling task's socket), 3 s timeout + TCP_NODELAY, FC06.

### 2026-04-23 — first bench JTAG bringup (CONTROLLER on Probe N)

CCS 2050 + MCU+ SDK 12.00.00.26 + SysConfig 1.27.0 + TI CLANG 4.0.4
LTS installed. SysConfig project at
`Nova_Firmware/lp_am2434/example.syscfg`. UART pin discovery: UART1
not on headers; switched to UART2 (J1-3/J1-4). `Load-Nova.ps1`
verified end-to-end via XDS110 JTAG (no DIP switch dance).

### 2026-04-07 — initial planning (pre-LP)

Project bringup notes when the LP-AM2434 was first chosen as the
prototype controller. Discussed the BoosterPack header GPIO/EPWM/ADC
budget for orbit equipment I/O, planned 2-board (Nova + Orbit) bench
rig. The "Mode A/B/C dev workflow" with TS orbit simulators on a PC
described there is **deprecated** — the live setup is real silicon
end-to-end (§2). Original notes preserved in repo history.
