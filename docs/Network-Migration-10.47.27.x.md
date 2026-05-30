# Network Migration — `10.1.2.x` → `10.47.27.x`

> **HISTORICAL — migration COMPLETE 2026-05-03.** Live IP map is in
> [`docs/System-State.md`](System-State.md) §1. This doc is the
> original migration plan + checklist; kept as the record of what
> was changed and why. **The `.20` proposed rpi5 IP in §3.2 below was
> abandoned — final rpi5 address is `10.47.27.108` (kept the `.108`
> host octet so deploy scripts only needed the subnet swap, not a full
> rename).**

> 🚨 **BENCH PI5 IS DUAL-HOMED FOR DEV — NOT THE PRODUCTION ARCHITECTURE 🚨**
>
> The bench rpi5 currently sits at `10.47.27.108` on the same LAN as the
> orbit LPs. This is a **dev-only shortcut**. **In production, the Pi5
> must live on the customer/internet-side LAN (e.g. `192.168.10.108`)
> with NO IP route to `10.47.27.0/24`.** The only authorised
> Pi5↔equipment data path is the UART (`/dev/ttyAMA0` @ 921600,
> COBS+protobuf envelopes) between the Pi5 and the Nova-controller LP.
> See [`docs/uart-airgap-architecture.md`](uart-airgap-architecture.md)
> for the full architecture invariant and the migration plan. The
> bridge-side direct-LP OTA-push modules (`orbitOtaPush.ts`,
> `orbitFleetResolver.ts`, `probe_fleet.ts`) were **deleted in Phase
> 4b** (commit `5465ab5`, 2026-05-29); `vfdClient.ts` and
> `orbitMbtcp.ts` (Modbus paths) are the remaining Phase-4b migration
> targets. Also codified as [`CLAUDE.md`](../CLAUDE.md) invariant #12.

> ✅ **DONE 2026-05-03:** LP boards on `10.47.27.0/24` (CONTROLLER `.1`,
> STORAGE `.2`, GDC `.3`, TRITON `.4`). Workstation has dual-IP NIC
> (`10.47.27.100/24` + `10.1.2.135/24` on `Ethernet 2`). **rpi5 at
> `10.47.27.108`**. The old `10.1.2.x` lab subnet is no longer
> reachable.
>
> **Why:** the lab `10.1.2.x` subnet is shared with the office VoIP
> system. The SIP phone at `10.1.2.150` (MAC `00:30:4d:f4:a4:db`,
> TI OUI) collides with both legacy AS2 orbit OUIs and our previous
> NOVA controller default IP. Moving the entire Constellation stack to
> a private `10.47.27.0/24` block ends those collisions permanently.
>
> **Cross-refs:**
> [/memories/repo/lan-ip-map.md](../memories/repo/lan-ip-map.md) (current map),
> [/memories/repo/foundation-plan.md](../memories/repo/foundation-plan.md)
> §F5.

---

## 1. New target subnet

**`10.47.27.0/24` — Constellation private LAN**, gateway `10.47.27.1`.

Run on a dedicated unmanaged switch (or VLAN-tagged port group) that
never touches the office VoIP subnet. The rpi5 + workstation will need
secondary NICs / VLAN tags to talk to both networks during the
transition.

## 2. New IP map

| Role | Old `10.1.2.x` | New `10.47.27.x` | Identifier |
|------|----------------|------------------|------------|
| Gateway / switch | `.1` | `.1` | router |
| **Workstation (Aaron's PC)** | `.230` | `.10` | dev / build host |
| (reserved future workstations) | — | `.11..19` | |
| **rpi5 controller (`gellert`)** | `.108` | `.108` | `agristar-bridge.service`, UART2→NOVA (kept `.108` for script continuity) |
| (reserved future rpi5) | — | `.21..29` | |
| **NOVA controller LP #1** | `.210` | `.100` | MAC `1c:63:49:13:d8:61` |
| (reserved future NOVAs) | — | `.101..119` | |
| **STORAGE orbit (slot 0)** | `.200` | `.200` | LP-AM2434 |
| **GDC orbit (slot 1)** | `.201` | `.201` | LP-AM2434 |
| **TRITON orbit (slot 2)** | `.202` | `.202` | LP-AM2434 |
| (reserved future orbits) | — | `.203..219` | |
| (DHCP pool — visitors / probes) | — | `.220..240` | |
| (reserved infra — APs, IPMI) | — | `.241..254` | |

Orbit IPs intentionally keep `.200+slot` so `resolveOrbitHost(slot)`
math stays unchanged — only the `ORBIT_IP_BASE` env var moves.

## 3. Files / settings that need updating

This is the canonical checklist — produced 2026-05-01 by grepping
`10\.1\.2\.\d+` across the workspace. **Each line is "old → new".**
Don't try to do these in a single sed pass; some files (firmware
constants in OSPI, deploy scripts) need extra care.

### 3.1 Firmware (rebuild + reflash required)

| File | Field | Old | New |
|------|-------|-----|-----|
| [Nova_Firmware/lp_am2434/lp_device_config.h](../Nova_Firmware/lp_am2434/lp_device_config.h) | `LP_DEVCFG_DEFAULT_IP_CONTROLLER` | `0x0A0102D2` (`.210`) | `0x0A2F1B64` (`.100`) |
| [Nova_Firmware/lp_am2434/lp_device_config.h](../Nova_Firmware/lp_am2434/lp_device_config.h) | `LP_DEVCFG_DEFAULT_IP_STORAGE` | `0x0A0102C8` (`.200`) | `0x0A2F1BC8` (`.200`) |
| [Nova_Firmware/lp_am2434/lp_device_config.h](../Nova_Firmware/lp_am2434/lp_device_config.h) | `LP_DEVCFG_DEFAULT_IP_GDC` | `0x0A0102C9` (`.201`) | `0x0A2F1BC9` (`.201`) |
| [Nova_Firmware/lp_am2434/lp_device_config.h](../Nova_Firmware/lp_am2434/lp_device_config.h) | `LP_DEVCFG_DEFAULT_IP_TRITON` | `0x0A0102CA` (`.202`) | `0x0A2F1BCA` (`.202`) |
| [Nova_Firmware/lp_am2434/lp_device_config.h](../Nova_Firmware/lp_am2434/lp_device_config.h) | gateway / netmask | gateway `.1` (`0x0A010201`) | gateway `0x0A2F1B01` |
| [Nova_Firmware/lp_am2434/Flash-LP.ps1](../Nova_Firmware/lp_am2434/Flash-LP.ps1) | `$roleMap[*].DefaultIp` | `10.1.2.X` strings | `10.47.27.X` strings |
| [Nova_Firmware/lp_am2434/enet/test.c](../Nova_Firmware/lp_am2434/enet/test.c) | `gStaticIP[]` fallback | `10.1.2.249` etc | `10.47.27.249` etc |
| [Nova_Firmware/lp_am2434_enet_ref/test.c](../Nova_Firmware/lp_am2434_enet_ref/test.c) | demo `gStaticIP[]` | `10.1.2.210/.211` | `10.47.27.100/.101` |

> The factory-provisioning override in
> `lp_device_config.c::LpDeviceConfig_Init` will overwrite the stored
> OSPI bank with the new compile-time IP at next cold boot. So the
> migration "just works" after one reflash + USB-C cycle per board.

### 3.2 Bridge / server (TS rebuild required)

| File | Field | Old | New |
|------|-------|-----|-----|
| [constellation-ui/server/src/orbitMbtcp.ts](../constellation-ui/server/src/orbitMbtcp.ts) | `ORBIT_IP_BASE` default | `'10.1.2.200'` | `'10.47.27.200'` |
| [constellation-ui/server/src/index.ts](../constellation-ui/server/src/index.ts) (if any literal) | bind / advertise | check | — |
| [constellation-ui/deploy.sh](../constellation-ui/deploy.sh) | `HOST="${HOST:-10.1.2.108}"` | `.108` | `.20` |
| [Start-Constellation.ps1](../Start-Constellation.ps1) | banner text | `http://10.1.2.108:3000` | `http://10.47.27.20:3000` |
| [_restart_bridge.sh](../_restart_bridge.sh) | ssh target | `gellert@10.1.2.108` | `gellert@10.47.27.20` |
| [_get_bridge_log.sh](../_get_bridge_log.sh) | ssh target | `gellert@10.1.2.108` | `gellert@10.47.27.20` |

### 3.3 Sensor-injector / dev tools

| File | Field | Old | New |
|------|-------|-----|-----|
| [sensor-injector/src/index.ts](../sensor-injector/src/index.ts) | `ORBIT_HOST` default | `'10.1.2.200'` | `'10.47.27.200'` |
| [sensor-injector/README.md](../sensor-injector/README.md) | example | `10.1.2.200` | `10.47.27.200` |

### 3.4 rpi5 deploy / probe scripts (`rpi5/`)

Bulk update — every `gellert@10.1.2.108` → `gellert@10.47.27.20`. Use:

```powershell
Get-ChildItem F:\Constellation\rpi5\*.sh |
  ForEach-Object {
    (Get-Content $_) -replace 'gellert@10\.1\.2\.108', 'gellert@10.47.27.20' |
      Set-Content $_
  }
```

Then audit any remaining `10\.1\.2\.\d+` references — most are probe
loops over the orbit IPs (`.230`, `.231`, etc.). Keep
**`set_pi_orbit_host.sh`** and **`rpi5_orbcheck.sh`** in mind — both
hardcode IP loops that need rewriting.

### 3.5 UI / frontend

The Svelte UI does not embed the rpi5 IP directly — it uses
relative URLs against whatever host serves the page. Should be a
no-op. Verify by grep after migration:

```powershell
Get-ChildItem F:\Constellation\constellation-ui\src -Recurse -Include *.ts,*.svelte |
  Select-String '10\.1\.2\.|10\.47\.27\.'
```

### 3.6 Documentation (low priority — fix opportunistically)

Many `docs/` files reference `10.1.2.x` as the verified bring-up
state. Don't bulk-rewrite — add a one-line note at the top of each
once the migration completes:

> **Network migration 2026-XX-XX:** subnet moved to
> `10.47.27.0/24`. Reference IPs in this doc may show `10.1.2.x`;
> the new equivalents per `docs/Network-Migration-10.47.27.x.md`.

## 4. rpi5 host configuration

The rpi5 needs its primary NIC reconfigured. Today it's on
`10.1.2.108` (DHCP reservation). After migration:

```bash
# /etc/dhcpcd.conf (or NetworkManager equivalent)
interface eth0
static ip_address=10.47.27.20/24
static routers=10.47.27.1
static domain_name_servers=10.47.27.1 1.1.1.1
```

Verify after reboot:

```bash
ip -4 addr show eth0    # 10.47.27.20/24
ip route                # default via 10.47.27.1
ping -c 2 10.47.27.1
```

Also update `/etc/hosts` if it has any literal references, and rerun
`sudo systemctl restart constellation-bridge` to pick up the new
`ORBIT_IP_BASE` env if you set it via systemd override.

## 5. Migration order at the office

Do these in sequence — out-of-order means you'll lose remote access
mid-migration.

1. **Stage everything offline first.** All firmware rebuilt and
   sitting in `nova_lp.release.mcelf.hs_fs` per role. All bridge code
   committed with new defaults. Don't push until step 7.
2. **Bring up the new switch / VLAN.** Workstation NIC #2 bound to
   `10.47.27.10`. Verify gateway `.1` answers ping.
3. **Reflash NOVA controller** to `10.47.27.100` via JTAG (reuse the
   existing `Flash-LP.ps1 -Probe N -Role CONTROLLER -Ip 10.47.27.100`
   path; the role-map default will already be the new IP after the
   code change in §3.1). Cold-boot. Verify on new subnet:
   ```powershell
   ping 10.47.27.100
   arp -a | Select-String '1c-63-49-13-d8-61'
   ```
4. **Reflash STORAGE / GDC / TRITON orbits** the same way. Verify
   each:
   ```powershell
   foreach ($ip in '10.47.27.200','10.47.27.201','10.47.27.202') {
     Test-NetConnection $ip -Port 5502 -InformationLevel Quiet
   }
   ```
5. **Reconfigure rpi5 networking** per §4. SSH in over the OLD subnet
   first to apply the change, accept that the SSH session will die,
   reconnect via new IP `10.47.27.20`.
6. **Deploy bridge** to rpi5 with `./deploy.sh --target=production`
   (HOST already set to `.20` per §3.2 change). Verify
   `agristar-bridge.service` comes up clean and finds all three
   orbits + NOVA.
7. **Test from workstation:**
   ```powershell
   curl http://10.47.27.20:3000/                       # UI
   curl http://10.47.27.20:9001/iot/orbit/ota/version?slot=0
   ```
8. **Update [/memories/repo/lan-ip-map.md](../memories/repo/lan-ip-map.md)**
   with the new map and mark the old one historical.
9. **Decommission `10.1.2.x` access.** Remove the OLD VLAN / NIC
   binding from rpi5 + workstation. From this point onward the
   Constellation stack lives entirely on `10.47.27.0/24`.

## 6. Rollback

If the migration breaks something at step 6 or later:
- **rpi5:** keep the OLD `dhcpcd.conf` as `dhcpcd.conf.pre-migration`.
  Boot off it via SD-card emergency entry if the new config hangs.
- **NOVA / orbits:** the OSPI bank still holds the previous IP. Re-flash
  with the old `LP_DEVCFG_DEFAULT_IP_*` macros to revert any individual
  board.
- **Bridge:** previous deploy archive lives at `/opt/constellation/releases/`
  on the rpi5 — symlink swap and restart.

Migration cost: rough estimate one focused work session at the
office. Risk: low, because every step is individually reversible.
