# UART airgap architecture + OTA migration plan

> **🚨 INVARIANT — IF YOU ARE WORKING ON BRIDGE↔NOVA COMMUNICATION, READ THIS FIRST 🚨**
>
> The **production** Constellation deployment has the Pi5 on the operator/
> internet-side LAN (e.g. `192.168.10.0/24`) and Nova on the equipment LAN
> (`10.47.27.0/24` via dipswitch-assigned IPs). The **only** physical path
> between the two networks is the **UART** at `/dev/ttyAMA0` between the
> Pi5 and the Nova-controller LP.
>
> **In the production architecture, the Pi5 has NO IP route to any orbit LP.**
> Any bridge-side code that opens a TCP socket to `10.47.27.x` is dev-only
> and must not ship.
>
> **THE CURRENT BENCH (2026-05-30) IS DUAL-HOMED FOR DEV CONVENIENCE.**
> The Pi5 sits at `10.47.27.108` on the same LAN as the orbit LPs.
>
> **Phase 4 (OTA) FULLY LANDED 2026-05-29.** The bridge-side direct-LP
> OTA modules `orbitOtaPush.ts`, `orbitFleetResolver.ts`,
> `probe_fleet.ts` are **deleted** (commit `5465ab5`, 816 lines). OTA
> goes UART → Nova-side broker (`nova_ota_broker.c`) → orbit `:5503`
> via Nova-side `nova_ota_push.c`. The 4-board fleet validated
> end-to-end on 0.A.208.
>
> **Phase 4b remaining: `vfdClient.ts` and `orbitMbtcp.ts` (Modbus
> paths) still TCP-from-Pi5-to-LP.** These are the next migration
> targets. If you are writing new bridge↔LP code and you see
> TCP-from-Pi5-to-LP, that is the migration target, not the production
> design.

## The intended architecture (production)

```
┌──────────────────────────┐
│  Operator workstation /  │
│  customer office WiFi    │
│  192.168.10.0/24         │
└────────────┬─────────────┘
             │ HTTPS, SvelteKit UI (port 80/443 via lighttpd)
             │
┌────────────▼─────────────────────────────────┐
│              Raspberry Pi 5                  │
│              192.168.10.108                  │
│                                              │
│  - SvelteKit UI (operator-facing)            │
│  - agristar-bridge.service (REST/WS, :9001)  │
│  - Speaks ONLY UART to Nova                  │
│  - NO route to 10.47.27.x                    │
└────────────┬─────────────────────────────────┘
             │ /dev/ttyAMA0  @ 921600 baud
             │ COBS + protobuf envelopes
             │ (the ONLY data path between sides)
             │
┌────────────▼─────────────────────────────────┐
│         Nova-controller LP (AM2434)          │
│         10.47.27.1  (dipswitch-set)          │
│                                              │
│  - UART2 ↔ Pi5 (bridge_uart_task)            │
│  - CPSW Ethernet ↔ 10.47.27.0/24 LAN         │
│  - Modbus TCP master to Storage/GDC/Triton   │
│  - OTA broker (relay role — see migration)   │
└────────────┬─────────────────────────────────┘
             │ Ethernet (gigabit RGMII via DP83869 PHY)
             │ 10.47.27.0/24 — AIRGAPPED equipment LAN
             │
   ┌─────────┴─────────┬──────────┬──────────┐
   │                   │          │          │
┌──▼──────┐  ┌─────────▼──┐  ┌────▼────┐  ┌──▼──────┐
│ Storage │  │  GDC       │  │ Triton  │  │ Pulsar  │
│ 10.47.  │  │  10.47.    │  │ 10.47.  │  │ 10.47.  │
│  27.2   │  │   27.3     │  │  27.4   │  │  27.5+  │
└─────────┘  └────────────┘  └─────────┘  └─────────┘
```

The dipswitches on each LP set the bottom-octet of its `10.47.27.x` IP
and are read at boot before any networking comes up. There's no DHCP on
this LAN — every IP is hardware-pinned.

## Why the UART matters

The UART isn't just convention; it's the entire security argument for
the system. Specifically:

| Threat | UART airgap mitigation |
|---|---|
| Pi5 web UI compromise (RCE via SvelteKit/Node CVE) | Attacker has shell on Pi5 but no IP route to LPs. To touch equipment they must speak the bridge envelope protocol over UART — a structured proto3-over-COBS interface that the Nova firmware validates field-by-field. |
| Customer/operator network exposure | Pi5 sits on the office LAN; the equipment LAN is physically separate. Internet-side attackers can't pivot to the LP bus. |
| Lateral movement from one LP to another | LPs are on a closed LAN with only Nova as Modbus master. A compromised LP can't pivot to other LPs because no LP runs a Modbus client (only Nova does). |
| Malicious firmware delivery | OTA traffic must traverse Nova. Nova can validate manifest signatures, role-vs-binary consistency, and version-downgrade policy before any byte hits the destination LP. Choke point = control point. |

The cost is that **every bit of communication between operator-side
and equipment-side has to go through Nova**. Adding a new control
surface means designing a new envelope field, decoding it in Nova,
acting on it. There's no "just open another port from the Pi5" shortcut.
That cost is the price of the airgap, and it's worth it.

## The current dev compromise (2026-05-30)

Today's bench has the Pi5 dual-homed (or single-homed on the airgapped
LAN at `10.47.27.108`).

**OTA path: migrated.** Phase 4 / Phase 4b landed 2026-05-29:
`firmwareInstaller.ts` now drives the Nova-side broker exclusively over
UART envelopes. The bridge-side OTA modules that previously TCP-pushed
to LPs (`orbitOtaPush.ts`, `orbitFleetResolver.ts`, `probe_fleet.ts`)
are deleted.

**Modbus path: still bench-direct.** What remains:

- `vfdClient.ts` — Modbus TCP client that talks directly to LP `:5502`
  for VFD drive status/control.
- `orbitMbtcp.ts` — direct Modbus TCP polls of orbit boards (and the
  underlying transport for `vfdClient`).

**None of these works in the production airgap.** They work on the
bench because the Pi5 is on the equipment LAN. The Modbus migration
into Nova (`Nova` polls orbits, publishes via envelopes) is the next
significant migration after Phase 4b's OTA deletion.

## Migration plan — OTA via Nova

The goal: the Pi5 is back on the operator LAN. Nova becomes the OTA
broker. The LP-side wire protocol stays the same.

### Phase 1 — Capture the bridge-side algorithms as Nova-side specs

What today's `firmwareInstaller.ts` + `orbitOtaPush.ts` +
`orbitFleetResolver.ts` do, that Nova will need to do:

1. **Fleet snapshot.** Probe every LP on `10.47.27.0/24`, capture
   `FwBankInfo` (active bank, version, role, valid bits).
2. **Manifest validation.** Verify the .cfu manifest sha256, schema
   version, role-coherent component layout.
3. **Role-to-IP routing.** For each manifest component, match
   `comp.role` to an LP whose `FwBankInfo.current_role` matches.
   Throw on `noMatch` or `ambiguous` (today's `FleetResolverError`).
4. **Per-LP push.** Connect to LP `:5503`, send `FwBeginUpdate`
   (with `expected_role` + `allow_downgrade`), stream chunks,
   `FwFinalizeUpdate`, `FwActivateBank`, wait for return.
5. **Audit logging.** Record per-component pre-push state, push
   timing, success/failure with the LP error code.

In the migrated architecture all of this runs on Nova. The Pi5 just
sends the .cfu blob and receives progress updates.

### Phase 2 — Add bridge envelope for OTA initiation — **LANDED 2026-05-22**

> **RESOLVED 2026-05-22** — envelope contract landed in
> `proto/agristar/firmware.proto` (Pi5↔Nova install-orchestration
> section) and `proto/agristar/envelope.proto` (oneof slots 130-149).
> C codegen verified. Bridge-side TS regen deferred until protoc is
> on the dev box (or run on the Pi5 / CI).
> _Original sketch retained below for context._

Per-component streaming was chosen over whole-bundle buffering so each
LP only receives its own component and Nova never buffers more than
an in-flight chunk:

| Field | Direction | Purpose |
|---|---|---|
| `FwInstallBegin` (130) | Pi5→Nova | Enter install mode, declare bundle |
| `FwInstallComponentBegin` (131) | Pi5→Nova | Start one component (role-routed) |
| `FwInstallChunk` (132) | Pi5→Nova | Stream chunk (open-loop, UART line rate) |
| `FwInstallComponentFinalize` (133) | Pi5→Nova | All chunks sent — finalize + activate |
| `FwInstallComplete` (134) | Pi5→Nova | All components done — exit install mode |
| `FwInstallAbort` (135) | Pi5→Nova | Cancel install |
| `FwFleetProbe` (136) | Pi5→Nova | Pre-flight fleet probe |
| `FwInstallProgress` (140) | Nova→Pi5 | Per-component progress + bundle overall |
| `FwInstallResult` (141) | Nova→Pi5 | Terminal per-component summary |
| `FwFleetSnapshot` (142) | Nova→Pi5 | Result of FwFleetProbe |

Routing model: Pi5 sends `role` (OrbitRole) per component; Nova
matches against its own `current_role` (→ self-update path) or
fleet-probed `FwBankInfo.current_role` (→ TCP push). The Pi5 stays
agnostic of IP-to-role mapping and dipswitch ordering.

Memory: Nova never buffers a full component. Chunks flow Pi5 → UART
→ Nova → TCP → LP without intermediate storage beyond the in-flight
chunk. Open-loop streaming (no per-chunk envelope ack) because the
UART side is the bottleneck — LP TCP path drains chunks faster than
UART supplies them.

Install mode: `FwInstallBegin` causes Nova to suspend non-essential
tasks (sensor polling, equipment loops, settings broadcasts) until
`FwInstallComplete`/`FwInstallAbort`. Per user direction 2026-05-22:
"It is fine if the nova has to shutdown and its memory be flushed in
order to handle the push to the orbits."

_Original sketch:_

- `FwInstallBundle { bytes manifest_json; bytes cfu_blob; }` — Pi5 →
  Nova: here's the .cfu, please install across the fleet. Or a
  chunked variant `FwInstallChunk { uint32 offset; bytes data; }` if
  the .cfu doesn't fit in a single envelope (likely; .cfu is ~300 KB,
  envelope budget is much smaller).
- `FwInstallProgress { string component; ... bytes_written/total; state; }` —
  Nova → Pi5: per-component progress, mirroring the existing
  `firmware-progress` WebSocket on the bridge.
- `FwInstallResult { ComponentResult[] components; ... }` — Nova →
  Pi5: terminal result.

These envelope fields are the contract between the Pi5 and Nova for
OTA. The Pi5 becomes a thin client that hands Nova the bundle and
listens for progress.

### Phase 3 — Implement Nova OTA broker

In Nova firmware:

- New task `nova_ota_broker_task` (similar shape to today's
  `lp_ota_task` but Nova-side).
- Reuses the existing TCP `:5503` push state machine from
  `orbitOtaPush.ts` — port it to C in Nova. The wire protocol on
  LP `:5503` is unchanged.
- Adds a `FwInstallBundle` handler that buffers the .cfu in MSRAM
  or a temp OSPI scratch region, validates the manifest, then
  fans out per-component pushes.
- Sends `FwInstallProgress` envelopes back to the Pi5 every chunk
  or every state transition.

This task replaces the bridge-side `firmwareInstaller.ts` for
production. The Pi5 still has `firmwareInstaller.ts` as the
**thin-client orchestrator** but it just relays envelopes — no
direct TCP to LPs.

### Phase 4 — Retire bridge-side direct-LP TCP

> **PARTIALLY LANDED 2026-05-26.** The full broker pipeline is proven
> end-to-end on the bench: bridge → UART → Nova broker → TCP → orbit LP,
> 523/523 chunks delivered, FINALIZE Bank-B CRC match, stage-copy
> completes bit-exact for a single-orbit (.2 STORAGE) install. Three
> related fixes landed today: LP stage-copy CRC vs broker-driven
> `image_crc=0` ([`memories/repo/lp-ota-stage-copy-crc-finalize-fix.md`](../memories/repo/lp-ota-stage-copy-crc-finalize-fix.md),
> 0.A.197), bridge chunk pump `WINDOW=4 → WINDOW=1` to fit Nova UART RX
> ([`memories/repo/broker-window-burst-uart-rx-overrun.md`](../memories/repo/broker-window-burst-uart-rx-overrun.md)),
> and broker per-chunk PUSHING progress emit (0.A.196). Two open issues
> block production-shape declaration:
>
> 1. **Fleet-probe race at install-begin** — broker's internal
>    `NovaOtaPush_ProbeFleet` consistently marks orbits 2..N unreachable
>    in the same install where direct probes show all healthy
>    (suspected lwIP TCP PCB TIME_WAIT exhaustion).
>    [`memories/repo/broker-install-begin-fleet-probe-race.md`](../memories/repo/broker-install-begin-fleet-probe-race.md).
> 2. **Post-OTA Bank-A boot failure** — stage-copy reports success
>    and broker reports DONE, but the LP is unbootable after warm-reset
>    even through a power-cycle. Likely cause: stage-copy doesn't update
>    Bank A's FwBankHeader at 0x060000, so SBL chooser rejects the
>    just-copied image. Recovery via `Recover-Fleet.ps1` (JTAG re-flash).
>    [`memories/repo/lp-ota-bank-a-post-ota-corruption-incomplete-fix.md`](../memories/repo/lp-ota-bank-a-post-ota-corruption-incomplete-fix.md).
>
> **RESOLVED 2026-05-29** — Both opens are closed:
>   1. broker fleet-probe race → fixed in 0.A.206 via non-blocking
>      `lwip_connect` + `lwip_select` in `probe_one_host`.
>   2. post-OTA Bank-A boot failure → was the SEVENTH-layer SBL-wipe bug,
>      not a stage-copy issue at all. `write_meta_block_atomic` erased
>      the SBL tail at `0x40000-0x4BE2D` every Activate. Fixed in 0.A.208
>      by relocating `FW_HEADER_OFFSET` from `0x060000` to `0x300000`.
>      Phase 4 OTA end-to-end validated 2026-05-29 on the 4-board fleet
>      — STORAGE / GDC / TRITON / CONTROLLER all reach `state: "done"`,
>      `.1 active=bankB banks=AB`. Memory:
>      [`memories/repo/sbl-wipe-controller-self-update-7th-layer-2026-05-29.md`](../memories/repo/sbl-wipe-controller-self-update-7th-layer-2026-05-29.md).
>
> Phase 4b deletion of `orbitOtaPush.ts` / `orbitFleetResolver.ts` /
> `probe_fleet.ts` LANDED 2026-05-29 (commit `5465ab5`, 816 lines).
> `/api/_debug/broker-fleet-probe` is the surviving diagnostic
> replacement for `probe_fleet.ts`. `vfdClient.ts` and `orbitMbtcp.ts`
> remain on disk — their Modbus-into-Nova migration is a separate
> session per the Phase 4b plan below.
>
> _Phase 4a architecture detail retained below for context._

**Phase 4a — install path rewired (LANDED 2026-05-23).**
`constellation-ui/server/src/firmwareInstaller.ts` no longer touches
`orbitOtaPush.ts` / `orbitFleetResolver.ts`. The new `installBundle()`:
- Pre-sorts manifest components so orbits go first and the controller
  goes LAST (when the controller activates Nova reboots, dropping
  install state — anything queued after would never reach the broker).
- Sends `FwInstallBegin` → per-component `FwInstallComponentBegin` +
  open-loop chunk stream + `FwInstallComponentFinalize` → `FwInstallComplete`.
  Bridge-side helpers: `NovaSerialBridge.sendFwInstallChunk`,
  `sendFwInstallComponentFinalize`, `sendFwInstallComplete` (new).
- Mirrors broker `FwInstallProgress` events onto the existing
  `firmware-progress` WS channel shape — UI didn't change.
- Builds a `Map<component_index, lastProgress>` keyed by wire index
  (Observation O1 from the previous bench round) so the per-component
  terminal-state lookup survives result envelopes that omit failed
  pre-Begin entries.
- Controller post-reboot reconciliation: waits for the bridge's
  `connected` edge + the first `VersionInfo` envelope, matches it
  against the manifest's controller version (prefix-match across
  `+git-sha` and `-dirty` suffixes), then emits a synthetic `done`.
- `InstallOptions.fleet` and the `ORBIT_FLEET` env var are ignored
  on the bridge side now (broker discovers); kept on the API for
  back-compat until Phase 4b deletion.
- The three pre-Phase-4 debug endpoints (`/api/_debug/broker-fleet-probe`,
  `/api/_debug/broker-install-fail-test`,
  `/api/_debug/broker-install-begin-abort`) are still mounted —
  delete in Phase 4b together with the now-dead Phase-1B files.

**Phase 4b — delete the dead direct-TCP modules.**
- ☑ Delete `orbitOtaPush.ts`, `orbitFleetResolver.ts`,
  `probe_fleet.ts` from the bridge. **DONE 2026-05-29 (`5465ab5`).**
- ☑ Drop `/api/_debug/broker-install-fail-test` +
  `/api/_debug/broker-install-begin-abort` debug endpoints.
  `/api/_debug/broker-fleet-probe` retained as the operator
  diagnostic replacement for `probe_fleet.ts`. **DONE 2026-05-29
  (`c199b82`).**
- ☑ Drop `InstallOptions.fleet` and `ORBIT_FLEET` env handling.
  **DONE 2026-05-29 (`c199b82`).**
- ☐ Move `vfdClient.ts` and `orbitMbtcp.ts` Modbus-TCP logic INTO Nova.
  Today these run on the Pi5 because that was the dev shortcut.
  Production: Nova polls the orbit LPs and VFD over Modbus, then
  publishes the results to the Pi5 via envelopes (today's
  `FrontMatter` and friends). **REMAINING — next significant
  migration chunk.**
- ☐ Update the bridge env: drop `VFD_HOST` (couples with Modbus
  migration above).
- ☐ Update Pi5 deployment: bind only to `192.168.10.108` (or whatever
  customer-side address), explicitly NOT to `10.47.27.0/24`. Config
  change on the box.

### Phase 5 — Move the dev Pi5 off the equipment LAN

Once Phase 4 is solid, the bench Pi5 stops needing the
`10.47.27.0/24` interface. Reconfigure the Pi5's network bringup
(systemd-networkd, dhcpcd, or whatever's used) to only have the
operator-side NIC. Verify with `ip route` that there's no route to
`10.47.27.0/24`. Then attempt every OTA, sensor-read, equipment-
command via the UART path only. If anything still works directly to
an LP, find and remove the offending bridge code.

### Migration risks + open questions

- **Size of .cfu through UART.** At 921600 baud, a 300 KB .cfu takes
  ~2.6 seconds raw, more with COBS escaping and per-chunk
  acknowledgement. Acceptable. The 4-component bundle is ~1.2 MB
  which is ~10 seconds — also OK.
- **Nova storage for the bundle during install.** MSRAM is ~1.4 MB.
  Today the bridge holds the .cfu in `/tmp/constellation-firmware-staging`.
  In the migrated architecture Nova holds it. Either (a) buffer the
  whole .cfu in MSRAM (fits today but tight), or (b) buffer per-
  component (stream chunk N of component K from Pi5 → Nova → LP
  without intermediate storage). The chunked variant is more code
  but avoids the MSRAM ceiling.
- **Progress reporting cadence.** Today the bridge emits
  `firmware-progress` over WebSocket every chunk. Echoing every
  chunk over UART to the Pi5 would saturate the UART. Aggregate
  to every-N-chunks or every-5%-progress.
- **Failure recovery.** If Nova's OTA broker hangs mid-install,
  the Pi5 needs a watchdog/timeout to surface the failure to the UI.
  Today the bridge has its own timeouts. Nova-side needs the same.

## What this means for already-shipped work

The work landed in commit `f7dea6f9` and earlier (today's WCC fix +
universal-binary build flow on top) is **mostly transport-independent
and survives the migration**:

| Component | Survives migration? | Notes |
|---|---|---|
| LP-side role gate (G1-LP, BEGIN handler) | ✅ | LP doesn't care if the chunks come from Pi5 or Nova. |
| LP-side downgrade gate (G2-LP) | ✅ | Same. |
| `FwBankInfo.current_role` proto field | ✅ | Used by whoever does the fleet probe. Today Pi5, tomorrow Nova. |
| `FwBeginUpdate.expected_role` / `allow_downgrade` | ✅ | Same. |
| Universal-binary build (`Build-Cfu.ps1`) | ✅ | The binary content is unchanged; only the orchestrator changes. |
| `hal_flash_init` WCC=0 fix | ✅ | Unrelated to transport. |
| LP-side OTA listener on `:5503` | ✅ | Migration changes whose IP it accepts from. |
| Bridge `orbitFleetResolver.ts` | ⚠️  Logic migrates to Nova | Same algorithm, different host. |
| Bridge `firmwareInstaller.ts` | ⚠️  Migrates partial | Thin-client orchestration stays on Pi5; per-component push moves to Nova. |
| Bridge `orbitOtaPush.ts` | ⚠️  Logic migrates to Nova | Port the TCP push state machine to C. |
| Bridge `probe_fleet.ts` | 🗑️  Becomes Nova-side diag CLI | Or a UI button that asks Nova "probe and report". |
| `ORBIT_FLEET` env var on bridge | 🗑️  Becomes a Nova-side config | Or hardcoded from dipswitch discovery. |

So the firmware-side work tonight is durable. The bridge-side work is
"correct dev tooling" that documents the production algorithms but
needs to move home.

## Where this doc fits

- Cross-referenced from [`CLAUDE.md`](../CLAUDE.md) invariant #12.
- Cross-referenced from [`docs/Network-Migration-10.47.27.x.md`](Network-Migration-10.47.27.x.md).
- Cross-referenced from [`docs/lp-am2434-ota-hardening-plan.md`](lp-am2434-ota-hardening-plan.md).
- Cross-referenced from `memories/repo/ota-bench-2026-05-20-three-layers.md`.

Update those if this doc moves or its invariants change.
