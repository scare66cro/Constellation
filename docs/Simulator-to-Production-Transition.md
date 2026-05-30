# Simulator â†’ Production Transition Guide

How the dev stack maps to real LP-AM2434 hardware. Linked from
[`CLAUDE.md`](../CLAUDE.md).

> **Status:** Rewritten May 2026 for the LP-AM2434 era. The pre-Nova
> ARM/TM4C simulator (`armSimulator.ts`, `serialBridge.ts`,
> `.arm-settings-bank-{a,b}.json`, `USE_NOVA` flag) and its scaffolding
> are **deleted**. There is no longer a "sim vs prod" code split inside
> the bridge or the firmware â€” the same binaries run in both
> environments and only the *transport peer* differs.

---

## Architecture overview

```
DEVELOPMENT STACK                        PRODUCTION STACK
â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•                        â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•â•

â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”                  â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”
â”‚ LP-AM2434 board     â”‚ UART2            â”‚ LP-AM2434 board     â”‚ UART2
â”‚ (CONTROLLER role)   â”‚ 921600 baud      â”‚ (CONTROLLER role)   â”‚ 921600 baud
â”‚ flashed via JTAG    â”‚ â”€â”€â”              â”‚ flashed via JTAG    â”‚ â”€â”€â”
â”‚ (Probe N / S24L0957)â”‚   â”‚              â”‚ (production fixture)â”‚   â”‚
â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”¬â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜   â”‚              â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”¬â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜   â”‚
   Eth :502 â”‚             â”‚                 Eth :502 â”‚             â”‚
           â–¼             â–¼                          â–¼             â–¼
   â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”  â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”  â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”  â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”
   â”‚ STORAGE / GDC â”‚  â”‚ rpi5 @ 10.47.27.108  â”‚  â”‚ STORAGE / GDCâ”‚  â”‚ rpi5 (panel)       â”‚
   â”‚ /TRITON LPs   â”‚  â”‚ â”€ agristar-bridge  â”‚  â”‚ /TRITON LPs  â”‚  â”‚ â”€ agristar-bridge  â”‚
   â”‚ on 10.47.27.x â”‚  â”‚ â”€ uisvelte         â”‚  â”‚ 10.47.27.x   â”‚  â”‚ â”€ uisvelte         â”‚
   â”‚ (air-gapped)  â”‚  â”‚ â”€ /dev/ttyAMA0     â”‚  â”‚              â”‚  â”‚ â”€ /dev/ttyAMA0     â”‚
   â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜  â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜  â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜  â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜
                                  â–²
                                  â”‚ ssh tunnel `localhost:9001`
                                  â”‚
                        â”Œâ”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”
                        â”‚ developer PC     â”‚
                        â”‚ (Windows + WSL)  â”‚
                        â”‚ Flash-LP.ps1     â”‚
                        â”‚ deploy.sh        â”‚
                        â””â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”˜
```

Both columns are the **same binaries**. The only thing that changes
between dev and a customer install is which physical fixture the
LP boards are mounted in and which network they sit on.

---

## 1. Bridge â†” CONTROLLER LP

| Aspect              | Dev / Prod                                          |
|---------------------|-----------------------------------------------------|
| **Transport**       | UART2 â†’ `/dev/ttyAMA0` @ 921600 baud, 8N1           |
| **Wire protocol**   | COBS + CRC-16 envelope â†’ protobuf (per `proto/agristar/envelope.proto`) |
| **Bridge process**  | `agristar-bridge.service` on rpi5, `npx tsx src/index.ts` (no build step) |
| **Firmware**        | `nova_lp.release.mcelf.hs_fs` flashed to OSPI @ 0x80000 (see `Flash-LP.ps1`) |

There is no "simulator transport". The dev workflow points the same
service at a real LP board over the same UART. The legacy TCP-shimmed
ARM simulator is gone.

## 2. CONTROLLER LP â†” orbit LPs (STORAGE / GDC / TRITON)

| Aspect              | Dev / Prod                                          |
|---------------------|-----------------------------------------------------|
| **Transport**       | Modbus TCP, port 502, on the air-gapped 10.47.27.0/24 LAN |
| **Polling**         | CONTROLLER firmware (`orbit_client.c`) cycles slots every 100 ms |
| **Sensor data**     | HR base 200, length 64 â€” pushed up as `OrbitSensorBank` (envelope tag 124) |
| **Status / DI / DO**| `OrbitStatus.boards[]` (envelope tag 120), every 5 s |

IP map for the dev fixture:

| Role        | Probe | XDS Serial  | UART  | IP              |
|-------------|-------|-------------|-------|-----------------|
| CONTROLLER  | N     | S24L0957    | COM4  | 10.47.27.1      |
| STORAGE     | A     | S24L0417    | COM5  | 10.47.27.2      |
| GDC         | B     | S24L0707    | COM9  | 10.47.27.3      |
| TRITON      | (TBD) | (TBD)       | (TBD) | 10.47.27.4      |
| rpi5 alias  | n/a   | n/a         | n/a   | 10.47.27.108/24 |

Each board's role + IP are persisted in OSPI by `lp_device_config.c`;
`Flash-LP.ps1 -Probe X` programs both the firmware and the role/IP
defaults for that probe.

## 3. Settings persistence (sim â‰¡ prod)

OSPI ping-pong banks (`lp_settings_store.c`) are the **only** persistence
layer. Bank A / Bank B alternate per save with a sequence number and
CRC; cold boot picks the higher-sequence valid bank.

The legacy `~/.constellation/.arm-settings-bank-{a,b}.json` files no
longer exist. Anything that reads / writes settings goes through
`LpSettings_*` and `LpDeviceConfig_*` â€” there is no JSON shim.

## 4. Wire protocol invariants (sim â‰¡ prod)

The COBS + CRC framing and protobuf payload semantics are byte-identical
in dev and production. Bugs in either reproduce in the other. Full list
in [`firmware-bridge-protocol.md`](firmware-bridge-protocol.md). Headlines:

1. **proto3 zero-suppression** â€” use `pb_uint32_force` /
   `pbVarintForce` whenever 0 is meaningful (Mode=OFF, index=0,
   threshold=0, fixed-length array slot 0, cross-coupled settings).
2. **Mode-dependent encoders** mirror legacy `StoreXxx()` positional
   layout per mode â€” never send a fixed-position superset.
3. **Repeated-submsg decoders** clear the destination array first; the
   counters MUST be function-local, never `static`.
4. **TX path goes through `NovaProto_SendRaw` mutex** â€” bypassing it
   corrupts COBS framing into a CRC-error storm.
5. **Verification:** `/health` should report `rxCrcErrors:0` /
   `rxCobsErrors:0` after any save campaign.

## 5. Build / flash / deploy commands

| Operation              | Command (Windows PowerShell)                                                |
|------------------------|------------------------------------------------------------------------------|
| Build LP firmware      | `cd F:\Constellation\Nova_Firmware\lp_am2434; gmake PROFILE=release`         |
| JTAG-flash a board     | `cd F:\Constellation\Nova_Firmware\lp_am2434; .\Flash-LP.ps1 -Probe N`       |
| Skip build, just flash | `.\Flash-LP.ps1 -Probe N -SkipBuild`                                         |
| Cold-boot LP after flash | `cd ospi_flash; & "C:\ti\ccs2050\ccs\ccs_base\scripting\bin\dss.bat" .\system_reset.js F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP_NOVA.ccxml` |
| Build SvelteKit UI     | `cd F:\Constellation\constellation-ui; npm run build`                         |
| Deploy UI to rpi5      | `wsl bash -c "cd /mnt/f/Constellation/constellation-ui && ./deploy.sh --target=production"` |
| Push bridge .ts files  | `scp F:\Constellation\constellation-ui\server\src\<file>.ts gellert@10.47.27.108:/home/gellert/Gellert/constellation/constellation-ui/server/src/` |
| Restart bridge         | `ssh gellert@10.47.27.108 "sudo systemctl reset-failed agristar-bridge && sudo systemctl restart agristar-bridge"` |
| Tail bridge log        | `bash F:\Constellation\_get_bridge_log.sh`                                   |

`Flash-LP.ps1` enforces a per-probe XDS110-serial guard (it refuses to
flash unless the requested probe is the **only** one enumerated, unless
`-Force` is passed). This prevents accidentally writing CONTROLLER
firmware to the STORAGE board, etc. See
[`/memories/repo/lp-am2434-bringup.md`](../memories/repo/lp-am2434-bringup.md).

`deploy.sh` only pushes the SvelteKit UI build â€” bridge `server/src/*.ts`
must be `scp`'d manually. The bridge runs via `tsx` (no build step), so
file copy is sufficient.

## 6. What stays the same (no per-environment branches)

These are **identical** in dev and prod:

- Bridge REST + WS surface (`apiRoutes.ts`, `protoStream.ts`).
- Data cache (`dataCache.ts`) â€” passthrough; no unit conversion.
- Proto schema (`proto/agristar/*.proto`) and ts-proto / nanopb codegen.
- SvelteKit UI (`constellation-ui/src/`).
- LP firmware binary (`nova_lp.release.mcelf.hs_fs`) â€” single artifact
  per role, no `#ifdef SIM` / `#ifdef PROD` paths.
- Settings persistence (OSPI ping-pong via `lp_settings_store.c`).

## 7. Removed concepts (do not re-introduce)

- `armSimulator.ts` â€” TS pure-software ARM emulator. Deleted.
- `serialBridge.ts` â€” legacy ASCII RTS/ACK bridge. Deleted.
- `USE_NOVA` env switch â€” there is no fallback path. Deleted.
- `dev-orbit-probe` / `DEV_ORBIT_PROBE` env â€” bridge does **not**
  populate `dataCache.getOrbitBoards()` from local code. Real boards do.
- `.arm-settings-bank-{a,b}.json` â€” replaced by OSPI banks.
- `QEMU_BUILD` / `am2434_qemu_r5f0.ld` â€” the LP firmware now targets
  real silicon; the QEMU machine model in `qemu-constellation/` is a
  separate research project, not part of the dev workflow.
- Bridge-side Modbus TCP polling of orbits (`syncEquipOutputsViaModbus`,
  `syncIoConfigToOrbit`). The CONTROLLER LP polls orbits directly.
- Per-page `.arm-settings-bank-*.json` shims in `armSimulator`.

If you find one of these in code or docs, it's stale â€” delete it.

## 8. Sim-only behaviours that **do** still exist (and why)

- **`localhost:9001` ssh tunnel from the dev PC.** Convenience only â€”
  forwards to `agristar-bridge` on the rpi5. Production users hit the
  rpi5's lighttpd proxy on `:80` instead.
- **`Start-Constellation.ps1`** is *not* a stack launcher anymore. It
  just opens the sensor-injector panel page on the rpi5 in a browser.
  No local processes are spawned.
- **Sensor injector** runs as `sensor-injector.service` on the rpi5
  (target 10.47.27.2:5502 via the rpi5's 10.47.27.108 alias). This is
  a developer convenience for forcing sensor values into the STORAGE
  board's RTU passthrough; it does not exist on a customer install.

When in doubt about whether something is sim-only, check
[`/memories/repo/`](../memories/repo/) â€” every recurring sim/prod
delta has a memory file.
