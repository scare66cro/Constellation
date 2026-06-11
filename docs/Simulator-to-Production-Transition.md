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
  (target 10.47.27.2:5502 via the rpi5's 10.47.27.108 alias). It writes
  synthetic values **directly** into the STORAGE board's writable HR
  windows over Modbus TCP — it does **not** go through an RTU passthrough
  (the orbit's RS-485 sensor sweep is inert; see §9). It does not exist
  on a customer install. **Run exactly one injector** — a second copy
  fighting it over the LP's single Modbus connection makes values
  alternate (see [`memories/repo/rpi5-second-modbus-master-sensor-injector.md`](../memories/repo/rpi5-second-modbus-master-sensor-injector.md)).
  See [`memories/repo/sensor-injector.md`](../memories/repo/sensor-injector.md)
  and the full scaffolding catalog in **§9** below.
- **`orbit-simulator/` workspace** (separate from the deleted
  in-bridge `orbitSimulator.ts`) is still in the repo for ad-hoc
  Modbus TCP testing. Not auto-launched; not a production component.

When in doubt about whether something is sim-only, check
[`memories/repo/`](../memories/repo/) — every recurring sim/prod
delta has a memory file.

## 9. Bench scaffolding pending real sensor / DI / DO hardware

The bench has **no real analog sensors and no real digital-input wiring**
on the orbit boards yet. Until those arrive, the controller's analog
readings and failure detection are fed by **software injection**. This
section catalogs every temporary piece so the hardware cutover is
mechanical. None of it ships to a customer.

> **One-line summary:** sensors and DIs are *injected over Modbus TCP*
> into the STORAGE orbit's writable HR windows; the firmware accepts them
> as if they were live. When real hardware is wired, the firmware's own
> sweep overwrites the injected values automatically (**live wins, no
> mode switch**) — so the cutover is "wire the hardware, bind the
> transport, stop the injector," not a code rewrite.

### 9.1 What is injected, and where it lands

| What | Injected by | Wire path | Firmware accept point | Backing field |
|------|-------------|-----------|-----------------------|---------------|
| **Analog sensors** (temp/RH/CO₂) | `sensor-injector` | FC06/FC16 → HR **200..263** (eng = °C×10) | [`orbit_storage.c:162‑173`](../Nova_Firmware/lp_am2434/orbit_server/orbit_storage.c#L162-L173) (`write_one_hr` sensor range) | `s->sensor_block[64]` ([`orbit_state.h:206`](../Nova_Firmware/lp_am2434/orbit_server/orbit_state.h#L206)) |
| **Digital inputs** DI 0..9 | `sensor-injector` auto-prove | FC06 → HR **41000..41009** | [`orbit_storage.c:192‑199`](../Nova_Firmware/lp_am2434/orbit_server/orbit_storage.c#L192-L199) | `s->di[i]` |
| **E-Stop** (idx 10) | `sensor-injector` auto-prove | FC06 → HR **41010** | same | `s->e_stop` |
| **DC24V rails** 0..3 | `sensor-injector` (manual) | FC06 → HR **41011..41014** | same | `s->dc24v[]` |
| **CPU temperature** | manual (thermal-alarm test) | FC06 → HR **40004** | [`orbit_storage.c:184‑189`](../Nova_Firmware/lp_am2434/orbit_server/orbit_storage.c#L184-L189) | `s->cpu_temp_x10` |

The DI-inject window is defined at
[`orbit_storage.c:32‑44`](../Nova_Firmware/lp_am2434/orbit_server/orbit_storage.c#L32-L44)
(`HR_DI_INJECT_BASE 41000`); read-back at
[`orbit_storage.c:108‑113`](../Nova_Firmware/lp_am2434/orbit_server/orbit_storage.c#L108-L113).

### 9.2 The injector tool (`sensor-injector/`)

One Node/TypeScript process per orbit LP. Modbus-TCP master + a small
REST/web panel on `:9100`.

- **Analog push.** Channel defaults + FC16 block push:
  [`sensor-injector/src/index.ts:117`](../sensor-injector/src/index.ts#L117)
  (defaults `[21.5, 22.0, 18.0, 20.5]`), re-push every 5 s
  ([`index.ts:202`](../sensor-injector/src/index.ts#L202), `REPUSH_MS`).
- **Auto-prove** ([`index.ts:226‑263`](../sensor-injector/src/index.ts#L226-L263)).
  Every `AUTOPROVE_MS` (default 1000 ms) it drives the orbit DIs to the
  **healthy** state so the controller's (now-active) failure detection is
  satisfied with no hardware attached. **Polarity is split** and
  load-bearing:
  - DI 0..9 are **active-high FAULT** inputs → healthy = **LOW (0)**.
  - E-Stop (idx 10) is **normally-closed** → healthy = **HIGH (1)**
    ([`lp_engine_shim.c:307`](../Nova_Firmware/lp_am2434/lp_engine_shim.c#L307):
    `di10=1` healthy, `0` tripped; orbit-unreachable also fail-safes to
    tripped).
  Manual "pins" (`POST /api/eq/di`, [`index.ts:392`](../sensor-injector/src/index.ts#L392))
  win over auto-prove — pin a DI HIGH to fault-test it, or pin E-Stop to
  0 to trip it. Disable the whole behaviour with `AUTO_PROVE=0`.
  Polarity rationale lives in
  [`memories/repo/reference_bench_di_polarity.md`](../memories/repo/reference_bench_di_polarity.md).
- **Temp unit follows Basic Settings** ([`index.ts` `watchBasicSetup`](../sensor-injector/src/index.ts),
  subscribes to envelope tag 20 over `/proto/stream`). This is a
  **permanent** UX feature, *not* scaffolding — the panel shows °F/°C to
  match the System Monitor. The wire/eng value stays °C either way.
- **Deployment.** Runs as `sensor-injector.service` on the Pi5
  (`/home/gellert/sensor-injector`, `npm start`, env
  `ORBIT_HOST=10.47.27.2 ORBIT_PORT=5502`). The Pi5 is airgapped from
  npm, so dependency updates are tar-copied from the dev box
  ([`memories/repo/rpi5-sensor-injector-update-offline-npm-2026-06-06.md`](../memories/repo/rpi5-sensor-injector-update-offline-npm-2026-06-06.md)).
  **Exactly one injector may write a given LP** — two masters fighting
  the LP's single Modbus connection make readings alternate every few
  seconds and produce `transaction in progress` errors.

### 9.3 The cutover seam — the inert RS-485 sensor sweep

`orbit_sensor_rtu.c` is the production sensor path, and it is **already
written but dormant**. It is the single place real sensor hardware gets
wired in:

- `poll_one()` returns immediately while no transport is bound
  ([`orbit_sensor_rtu.c:61`](../Nova_Firmware/lp_am2434/orbit_server/orbit_sensor_rtu.c#L61));
  the task loop idles
  ([`orbit_sensor_rtu.c:124‑131`](../Nova_Firmware/lp_am2434/orbit_server/orbit_sensor_rtu.c#L124-L131),
  logs `no transport bound — UART4 driver pending`).
- `orbit_sensor_rtu_bind_transport()`
  ([`orbit_sensor_rtu.c:27`](../Nova_Firmware/lp_am2434/orbit_server/orbit_sensor_rtu.c#L27))
  has **zero callers** today — binding it is the cutover.
- Once bound, the **1 Hz** sweep (`ORBIT_SENSOR_POLL_MS=1000`,
  [`orbit_sensor_rtu.h:32`](../Nova_Firmware/lp_am2434/orbit_server/orbit_sensor_rtu.h#L32))
  overwrites `s->sensor_block[]` with live RTU data on every tick, so
  injected values are superseded automatically — **live wins, no mode
  switch** ([`orbit_storage.c:163‑170`](../Nova_Firmware/lp_am2434/orbit_server/orbit_storage.c#L163-L170)).
- ADC→engineering conversion math lives in `adc_convert.c` (a 1:1 port of
  legacy `Analog_Input.c`); it is currently never invoked because nothing
  drives the sweep. This is the **only** place ADC→eng conversion may
  happen (invariant #6).

The DI side has the analogous seam: the comment at
[`orbit_storage.c:35,41`](../Nova_Firmware/lp_am2434/orbit_server/orbit_storage.c#L35-L41)
notes that once real GPIO drives `s->di[]` every tick (via
`LpOrbitIo_LatchInputs`), injected DI values are overwritten before the
next FC02 read — same "live wins" contract. Production builds **need not
strip** the HR 41000 inject window; it simply becomes inert.

### 9.4 DO (digital outputs) — already real, no shim

There is **no DO simulation to remove.** The controller computes
equipment outputs and the orbit firmware owns the coils/AO fields
(`s->ao_pct[]`, `s->ao_mode[]`, VFD regs) as real firmware state; the UI
animates equipment from those. "Drive actual DO" is therefore a
**hardware-wiring** task (relays/contactors on the orbit GPIO outputs),
not a software shim. The only output-side stub is VFD register writes
being cached locally pending the orbit-side RTU master
([`orbit_storage.c:156‑160`](../Nova_Firmware/lp_am2434/orbit_server/orbit_storage.c#L156-L160)),
tracked separately under the `vfdClient.ts` / `orbitMbtcp.ts` Phase 4b
migration (see §7 and `uart-airgap-architecture.md`).

### 9.5 Hardware cutover checklist (mechanical)

When real sensor boards + DI wiring land on an orbit:

1. **Analog:** implement/wire the UART4 RS-485 driver and call
   `orbit_sensor_rtu_bind_transport()` so the 1 Hz sweep runs. Confirm
   `s->sensor_block[]` tracks the physical sensors. No injector change
   needed — the sweep overwrites injection.
2. **DI:** confirm `LpOrbitIo_LatchInputs` sweeps the real GPIO into
   `s->di[]`/`s->e_stop`/`s->dc24v[]`. Re-validate polarity against
   `reference_bench_di_polarity.md` (active-high FAULT for DI 0..9, NC
   for E-Stop) — real wiring must match or failure detection inverts.
3. **Stop the injector:** `sudo systemctl disable --now
   sensor-injector.service` on the Pi5 (and stop any dev-box copy). Or,
   to keep it for forcing test values, run it with `AUTO_PROVE=0` so it
   stops fighting real DIs.
4. **DO:** wire relays/contactors to the orbit GPIO outputs; verify with
   the equipment-IO panel that commanded coils drive the load.
5. **Verify:** `/health` → `rxCrcErrors:0`; sensor values on the System
   Monitor track the physical sensors; pull a DI and confirm the matching
   failure raises (and clears).

## 10. Static-pressure sensor (type 11) — wire encoding, sim ≡ prod

Added 2026-06-10 to bench-test the Nova static-pressure fan-fail feature.
A 4-20 mA static-pressure transducer (0.00–2.50 inches water column)
presents at the orbit HR sensor block as sensor **type nibble 11**
(`SENSOR_TYPE_STATIC_PRESSURE`), matching AS2
(`ANALOG_SENSOR_TYPE_STATIC_PRESS=11`), the Nova orbit firmware, and the
UI `AnalogConfigForm`. **No sim-only divergence in the data path** — the
sim presents exactly the int16 a real board would. Two encoding facts are
load-bearing and must stay bit-exact across sim ↔ firmware:

- **×100 wire encoding (NOT ×10).** Unlike temp/humid (×10) and CO2
  (raw), static pressure packs one-hundredths of an inch wc: `1.25 "wc →
  int16 125`. This is a **new Gellert encoding with no AS2 ancestor** —
  AS2 stored static pressure as a plain integer; the ×100 packing is
  Constellation-only. The Nova controller descales ÷100. Mismatching the
  scale on either end is a 10× error, not a rounding error.
- **ADC→eng conversion (AS2 `ConvertToStaticPressure`, verbatim):**
  `scaled = adc/16`; `scaled < 180 → UNDEF (0x7FFF)`; `scaled > 900 → 2.5`;
  else `((scaled-180)/720)*2.5`; then `round(eng*100) & 0xFFFF`.
  Implemented in `orbit-simulator/src/adcConversion.ts::adcToStaticP` +
  the `case 11` of `adcToOrbitRegister`. Keep bit-exact with the orbit
  firmware `adc_convert.c` static-pressure case.

The sim has **no fan→MinSpeed or alarm logic** for this sensor — that is
Nova-side control behaviour. The sim's job ends at presenting the
converted ×100 value at the orbit HR (≥200). The injector pushes "wc
directly (`engToHrInt16(1.25, 11) → 125`); the panel slider for type 11 is
in wire units 0–250 (0.00–2.50 "wc, `div: 100`). When real RS-485
static-pressure boards land, the §9.5 analog cutover applies unchanged —
the 1 Hz sweep overwrites injection, no injector change needed.
