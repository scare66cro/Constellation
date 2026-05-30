# Simulator → Production Transition Guide

How the dev stack maps to real LP-AM2434 hardware. Linked from
[`CLAUDE.md`](../CLAUDE.md).

> **Status:** **TRANSITION COMPLETE.** Rewritten May 2026 for the
> LP-AM2434 era; refreshed 2026-05-30 for the Phase 5 proto-direct
> landing. The pre-Nova ARM/TM4C simulator (`armSimulator.ts`,
> `serialBridge.ts`, `.arm-settings-bank-{a,b}.json`, `USE_NOVA` flag)
> and its scaffolding are **deleted**. The pre-LP QEMU AM2434 machine
> model and `qemu-constellation/` working directory are **deleted**
> (commit `fd30841`, 2026-05-30; the directory was renamed to `rpi5/`
> in commit `b8c95a9` and now holds production rpi5 deployment assets).
> The orbit-simulator REST API on `:9010+` is dev-only — it exists in
> the workspace for ad-hoc Modbus TCP debugging but is not a production
> component.
>
> **There is no longer a "sim vs prod" code split inside the bridge or
> the firmware** — the same binaries run in both environments and only
> the *transport peer* differs.

---

## Architecture overview

```
DEVELOPMENT STACK                          PRODUCTION STACK
=================                          ================

+---------------------+                    +---------------------+
| LP-AM2434 board     | UART2              | LP-AM2434 board     | UART2
| (CONTROLLER role)   | 921600 baud        | (CONTROLLER role)   | 921600 baud
| flashed via JTAG    | ---+               | flashed via JTAG    | ---+
| (Probe N / S24L0707)|    |               | (production fixture)|    |
+----------+----------+    |               +----------+----------+    |
  Eth :502 |               |                 Eth :502 |               |
           v               v                          v               v
   +---------------+  +---------------------+  +---------------+  +---------------------+
   | STORAGE / GDC |  | rpi5 @ 10.47.27.108 |  | STORAGE / GDC |  | rpi5 (customer)     |
   | / TRITON LPs  |  |  - agristar-bridge  |  | / TRITON LPs  |  |  - agristar-bridge  |
   | on 10.47.27.x |  |  - uisvelte         |  | 10.47.27.x    |  |  - uisvelte         |
   | (airgapped)   |  |  - /dev/ttyAMA0     |  |               |  |  - /dev/ttyAMA0     |
   +---------------+  +----------+----------+  +---------------+  +----------+----------+
                                 ^                                            ^
                                 | ssh tunnel `localhost:9001`                |
                                 |                                            |
                       +---------+--------+                                   |
                       | developer PC     |                                   |
                       | (Windows + WSL)  |   192.168.10.0/24 customer LAN    |
                       | Flash-LP.ps1     |                                   |
                       | deploy.sh        |                                   |
                       +------------------+                                   |
                                          The production Pi5 has NO IP route -+
                                          to 10.47.27.0/24 — UART airgap only.
                                          See uart-airgap-architecture.md.
```

Both columns run the **same binaries**. The only thing that changes
between dev and a customer install is which physical fixture the LP
boards are mounted in and which network they sit on.

> **TODAY'S BENCH IS DUAL-HOMED FOR DEV.** Bench Pi5 sits at
> `10.47.27.108` on the same LAN as the orbit LPs. This breaks the
> production airgap; it is a dev shortcut. See invariant #12 in
> [`CLAUDE.md`](../CLAUDE.md) and
> [`docs/uart-airgap-architecture.md`](uart-airgap-architecture.md)
> for the migration plan that moves the bench Pi5 off the equipment
> LAN.

---

## 1. Bridge ↔ CONTROLLER LP

| Aspect              | Dev / Prod                                          |
|---------------------|-----------------------------------------------------|
| **Transport**       | UART2 → `/dev/ttyAMA0` @ 921600 baud, 8N1           |
| **Wire protocol**   | COBS + CRC-16 envelope → protobuf (per `proto/agristar/envelope.proto`) |
| **Bridge process**  | `agristar-bridge.service` on rpi5, `npx tsx src/index.ts` (no build step) |
| **Firmware**        | `nova_lp.release.mcelf.hs_fs` flashed to OSPI @ 0x80000 (see `Flash-LP.ps1`) |

There is no "simulator transport". The dev workflow points the same
service at a real LP board over the same UART. The legacy TCP-shimmed
ARM simulator is gone.

## 2. CONTROLLER LP ↔ orbit LPs (STORAGE / GDC / TRITON)

| Aspect              | Dev / Prod                                          |
|---------------------|-----------------------------------------------------|
| **Transport**       | Modbus TCP, port 502, on the airgapped 10.47.27.0/24 LAN |
| **Polling**         | CONTROLLER firmware (`orbit_client.c`) cycles slots every 100 ms |
| **Sensor data**     | HR base 200, length 64 — pushed up as `OrbitSensorBank` (envelope tag 124) |
| **Status / DI / DO**| `OrbitStatus.boards[]` (envelope tag 120), every 5 s |

Current bench fleet (2026-05-30):

| Role        | Probe | XDS Serial  | UART  | IP              |
|-------------|-------|-------------|-------|-----------------|
| CONTROLLER  | N     | S24L0707    | COM9  | 10.47.27.1      |
| STORAGE     | A     | S24L0417    | COM5  | 10.47.27.2      |
| GDC         | P     | S24L0727    | COM7  | 10.47.27.3      |
| TRITON      | T     | S24L0957    | COM4  | 10.47.27.4      |
| rpi5 alias  | n/a   | n/a         | n/a   | 10.47.27.108/24 |

Each board's role + IP are persisted in OSPI by `lp_device_config.c`;
`Flash-LP.ps1 -Probe X` programs both the firmware and the role/IP
defaults for that probe.

## 3. Settings persistence (sim ≡ prod)

OSPI ping-pong banks (`lp_settings_store.c`) are the **only** persistence
layer. Bank A / Bank B alternate per save with a sequence number and
CRC; cold boot picks the higher-sequence valid bank.

The legacy `~/.constellation/.arm-settings-bank-{a,b}.json` files no
longer exist. Anything that reads / writes settings goes through
`LpSettings_*` and `LpDeviceConfig_*` — there is no JSON shim.

## 4. Wire protocol invariants (sim ≡ prod)

The COBS + CRC framing and protobuf payload semantics are byte-identical
in dev and production. Bugs in either reproduce in the other. Full list
in [`firmware-bridge-protocol.md`](firmware-bridge-protocol.md). Headlines:

1. **proto3 zero-suppression** — use `pb_uint32_force` /
   `pbVarintForce` whenever 0 is meaningful (Mode=OFF, index=0,
   threshold=0, fixed-length array slot 0, cross-coupled settings).
2. **Mode-dependent encoders** mirror legacy `StoreXxx()` positional
   layout per mode — never send a fixed-position superset.
3. **Repeated-submsg decoders** clear the destination array first; the
   counters MUST be function-local, never `static`.
4. **TX path goes through `NovaProto_SendRaw` mutex** — bypassing it
   corrupts COBS framing into a CRC-error storm.
5. **Verification:** `/health` should report `rxCrcErrors:0` /
   `rxCobsErrors:0` after any save campaign.

## 5. Build / flash / deploy commands

| Operation              | Command (Windows PowerShell)                                                |
|------------------------|------------------------------------------------------------------------------|
| Build LP firmware      | `cd F:\Constellation\Nova_Firmware\lp_am2434; gmake PROFILE=release`         |
| JTAG-flash a board     | `cd F:\Constellation\Nova_Firmware\lp_am2434; .\Flash-LP.ps1 -Probe N`       |
| Skip build, just flash | `.\Flash-LP.ps1 -Probe N -SkipBuild`                                         |
| Build universal `.cfu` | `.\Build-Cfu.ps1` — produces `firmware-bundles\constellation-0.A.<n>.cfu` plus universal `nova_lp.release.mcelf.hs_fs` |
| Cold-boot LP after flash | `cd ospi_flash; & "C:\ti\ccs2050\ccs\ccs_base\scripting\bin\dss.bat" .\system_reset.js F:/Constellation/Nova_Firmware/lp_am2434/AM2434_LP_NOVA.ccxml` |
| Build SvelteKit UI     | `cd F:\Constellation\constellation-ui; npm run build`                         |
| Deploy UI to rpi5      | `wsl bash -c "cd /mnt/f/Constellation/constellation-ui && ./deploy.sh --target=production"` |
| Deploy bridge to rpi5  | `bash F:\Constellation\rpi5\_deploy_bridge_to_pi5_hw.sh` — snapshots + rsync + `npm install --omit=dev` + restart + `/health` check |
| Tail bridge log        | `bash F:\Constellation\_get_bridge_log.sh`                                   |

`Flash-LP.ps1` enforces a per-probe XDS110-serial guard (it refuses to
flash unless the requested probe is the **only** one enumerated, unless
`-Force` is passed). This prevents accidentally writing CONTROLLER
firmware to the STORAGE board, etc. See invariant #7 and
[`docs/LP-Flash-Probe-Discipline.md`](LP-Flash-Probe-Discipline.md).

`deploy.sh` only pushes the SvelteKit UI build — bridge `server/src/*.ts`
ships via `_deploy_bridge_to_pi5_hw.sh`. The bridge runs via `tsx` (no
build step).

## 6. What stays the same (no per-environment branches)

These are **identical** in dev and prod:

- Bridge REST + WS surface (`apiRoutes.ts`, `protoStream.ts`).
- Data cache (`dataCache.ts`) — passthrough; no unit conversion.
- Proto schema (`proto/agristar/*.proto`) and ts-proto / nanopb codegen.
- SvelteKit UI (`constellation-ui/src/`).
- LP firmware binary (`nova_lp.release.mcelf.hs_fs`) — single artifact
  per role, no `#ifdef SIM` / `#ifdef PROD` paths. Universal binaries
  (no `CONFIG_NOVA_LP_ROLE` / `CONFIG_NOVA_LP_IP`) are the OTA-shipped
  artifact.
- Settings persistence (OSPI ping-pong via `lp_settings_store.c`).

## 7. Removed concepts (do not re-introduce)

- `armSimulator.ts` — TS pure-software ARM emulator. Deleted.
- `serialBridge.ts` — legacy ASCII RTS/ACK bridge. Deleted.
- `USE_NOVA` env switch — there is no fallback path. Deleted.
- `dev-orbit-probe` / `DEV_ORBIT_PROBE` env — bridge does **not**
  populate `dataCache.getOrbitBoards()` from local code. Real boards do.
- `.arm-settings-bank-{a,b}.json` — replaced by OSPI banks.
- `QEMU_BUILD` / `am2434_qemu_r5f0.ld` — the LP firmware targets real
  silicon. The QEMU machine model that once lived in
  `qemu-constellation/` is **fully retired** (deleted 2026-05-30,
  commit `fd30841`). The directory was renamed to `rpi5/` in commit
  `b8c95a9` and now holds production rpi5 deployment assets only.
- Bridge-side direct-TCP-to-LP OTA modules — `orbitOtaPush.ts`,
  `orbitFleetResolver.ts`, `probe_fleet.ts` deleted in Phase 4b
  (commit `5465ab5`, 2026-05-29). OTA goes UART → Nova broker →
  orbit `:5503` now. See
  [`docs/uart-airgap-architecture.md`](uart-airgap-architecture.md).
- Bridge-side `orbitSimulator.ts` (in-server), `rs485Panel.ts`,
  `vfdSimulator.ts`, `rs485Responder.ts`, `vfdServer.ts`,
  `warningTranslator.ts`, `settingsSchema.ts`, `auditDiff.ts`,
  `orbitClient.ts` — deleted in Phase 5 (commit `291766e`,
  2026-05-30).
- Bridge-side Modbus TCP polling of orbits (`syncEquipOutputsViaModbus`,
  `syncIoConfigToOrbit`). The CONTROLLER LP polls orbits directly.
  (`vfdClient.ts` and `orbitMbtcp.ts` still exist as bench-only
  Modbus paths on borrowed time — migration into Nova is the next
  Phase 4b chunk.)

If you find one of these in code or docs, it's stale — delete it or
mark it with the obsolescence block from `CLAUDE.md`.

## 8. Sim-only behaviours that **do** still exist (and why)

- **`localhost:9001` ssh tunnel from the dev PC.** Convenience only —
  forwards to `agristar-bridge` on the rpi5. Production users hit the
  rpi5's lighttpd proxy on `:80` instead.
- **`Start-Constellation.ps1`** is *not* a stack launcher anymore. It
  just opens the sensor-injector panel page on the rpi5 in a browser.
  No local processes are spawned.
- **Sensor injector** runs as `sensor-injector.service` on the rpi5
  (target 10.47.27.2:5502 via the rpi5's 10.47.27.108 alias). This is
  a developer convenience for forcing sensor values into the STORAGE
  board's RTU passthrough; it does not exist on a customer install.
  See [`memories/repo/sensor-injector.md`](../memories/repo/sensor-injector.md).
- **`orbit-simulator/` workspace** (separate from the deleted
  in-bridge `orbitSimulator.ts`) is still in the repo for ad-hoc
  Modbus TCP testing. Not auto-launched; not a production component.

When in doubt about whether something is sim-only, check
[`memories/repo/`](../memories/repo/) — every recurring sim/prod
delta has a memory file.
