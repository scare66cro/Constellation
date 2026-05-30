# Office Bench Checklist â€” 2026-05-02

> âš  **HISTORICAL â€” superseded.** This was the runbook drafted before
> the 2026-05-02 bench session. The session ran and Phase C closed
> end-to-end; current state lives in
> [`docs/System-State.md`](./System-State.md).
>
> The IP plan in this checklist (`10.1.2.210/.200-.202`) is the OLD
> shared-VoIP subnet. The live rig now runs on `10.47.27.0/24`
> (CONTROLLER `.1`, STORAGE `.2`, GDC `.3`, TRITON `.4`). rpi5 stays
> at `10.47.27.108` with a `10.47.27.108` alias.
>
> Kept for archaeological reference of the consolidated pre-bench
> recipes from `phase-c-resume.md` (deleted), `lp-am2434-ota-phase1a.md`,
> `lp-am2434-bringup.md`, and `LP-AM2434-Hardware-Bringup-Plan.md`.
> Work top-to-bottom; do not skip a verification step. If a step fails,
> stop and diagnose â€” don't move on hoping the next step magically fixes
> it. Every step has a "verify" line; if you can't verify, you didn't do it.

---

## Pre-flight at the desk (before leaving)

- [ ] **Latest firmware built clean.**
  - Check `Get-ChildItem F:\Constellation\Nova_Firmware\lp_am2434\ti-arm-clang\nova_lp.release.mcelf.hs_fs` â€” file size ~447 KB, mtime today.
  - If not: `cd F:\Constellation\Nova_Firmware\lp_am2434\ti-arm-clang; $env:CCS_PATH='C:/ti/ccs2050/ccs'; $env:SYSCFG_PATH='C:/ti/sysconfig_1.27.0'; & 'C:/ti/ccs2050/ccs/utils/bin/gmake.exe' PROFILE=release`
- [ ] **Bridge typecheck clean.** `cd F:\Constellation\constellation-ui\server; npx tsc --noEmit` â€” no output = clean.
- [ ] **UI svelte-check clean.** `cd F:\Constellation\constellation-ui; npx svelte-check --threshold error` â€” `0 errors`.
- [ ] **Pi 5 bridge running.** `Test-NetConnection 10.47.27.108 -Port 9001 -InformationLevel Quiet` â†’ True.
- [ ] **JTAG probe IDs known:**
  - NOVA  : XDS110 `S24L0957`, COM4
  - STORAGE / GDC / TRITON: probe IDs in `Flash-LP.ps1 -Probe S/G/T`
- [ ] **Print this checklist.** (Or open it on the laptop, but printed paper survives bench-Wi-Fi outages.)

---

## Phase 0 â€” Power up and net-check (5 min)

- [ ] Plug in the bench rig. Wait 30 s for full boot.
- [ ] **Ping every node:**
  ```powershell
  '10.47.27.108','10.1.2.210','10.1.2.200','10.1.2.201','10.1.2.202' | %{
      $r = Test-Connection $_ -Count 1 -Quiet -TimeoutSeconds 2
      "{0,-12} = {1}" -f $_, $r
  }
  ```
- [ ] **All four LPs reachable:**
  - `.108` rpi5 bridge
  - `.210` NOVA controller
  - `.200` STORAGE / `.201` GDC / `.202` TRITON
- [ ] **If a board is DOWN:** check ARP `arp -a | Select-String '1c-63-49'`, USB-C reseat, last-resort JTAG re-flash (Phase 1 below). Do NOT proceed to Phase C verification with a missing orbit.

---

## Phase 1 â€” Flash NOVA controller with current firmware (10 min)

> Carries the OSPI offset migration (legacy `0x200000`/`0x210000` â†’ new
> `0x600000`/`0x610000` in Settings vault region) AND the OTA Phase 1A
> listener on `:5503`. First boot after this flash will scan the legacy
> banks, find the existing provisioning record, and re-save it forward
> automatically.

- [ ] `F:\Constellation\Nova_Firmware\lp_am2434\Flash-LP.ps1 -Probe N -Role CONTROLLER -Ip 10.1.2.210`
- [ ] Wait for `Flash succeeded` line in the script output.
- [ ] **Cold-boot the LP** (USB-C unplug 5 s, plug back in).
- [ ] **Verify firmware is running:**
  ```powershell
  ping -n 4 10.1.2.210
  ```
  All replies, < 5 ms typical.
- [ ] **Verify OSPI migration succeeded** (UART log on COM4, 115200-8N1):
  - Look for `[DevCfg] legacy bank found seq=N role=0 ip=0x0A0102D2 â€” migrating forward`
  - Followed by `[DevCfg] Save bank=A seq=N+1 role=0 ip=0x0A0102D2`
  - On subsequent boots: `[DevCfg] loaded bank=A seq=N+1 ...` (no legacy block â€” migration is one-shot).
- [ ] **Repeat for STORAGE / GDC / TRITON** (`-Probe S -Role STORAGE -Ip 10.1.2.200`, etc.) only if you want the full fleet on the new build. Not strictly required for Phase C verification â€” STORAGE is the only one tested below.

---

## Phase 2 â€” OTA Phase 1A version reporting (5 min)

> Verifies `lp_ota_task` is alive on `:5503` and emits `FwBankInfo` on connect.

- [ ] **Direct probe (bypass bridge):**
  ```powershell
  $ip='10.1.2.210'; $port=5503
  $c = New-Object System.Net.Sockets.TcpClient
  $c.Connect($ip,$port)
  $s = $c.GetStream()
  Start-Sleep -Milliseconds 500
  $buf = New-Object byte[] 256
  $n = $s.Read($buf, 0, $buf.Length)
  $c.Close()
  "got $n bytes: " + (($buf[0..($n-1)] | %{ $_.ToString('X2') }) -join ' ')
  ```
  - Expect: `00 00 00 NN 21 ...` where tag `0x21` = `FwBankInfo`.
  - Body should contain `12 0A 31 2E 30 2E 30 2D 6E 6F 76 61` â€” the string `"1.0.0-nova"`.
- [ ] **Through the bridge:**
  ```powershell
  curl http://10.47.27.108:9001/iot/orbit/ota/version?slot=0
  ```
  - Expect JSON: `{"slot":0,"host":"10.1.2.200","reachable":true,"bankInfo":{"bankAVersion":"1.0.0-nova",...}}`
- [ ] **In the UI** (Level 1 â†’ Version page on the touch panel):
  - Scroll the "Orbit firmware" row.
  - Expect: `STORAGE (slot 0) â€” v1.0.0-nova @ 10.1.2.200`, similarly for GDC/TRITON if those are flashed.
  - Boards on stale firmware will show `offline` â€” that's fine if you only flashed NOVA in Phase 1.

---

## Phase 3 â€” Phase C SystemStatus canary verification (15 min)

> The whole reason for the bench visit. Confirms `build_system_status_envelope`
> is correctly pulling sensor reads from STORAGE slot 0.

- [ ] **STORAGE up and Modbus-listening:**
  ```powershell
  Test-NetConnection 10.1.2.200 -Port 5502 -InformationLevel Quiet  # True
  ```
- [ ] **Inject test sensor values into STORAGE** (PT1=72.5Â°F â†’ HR200=725):
  ```powershell
  cd F:\Constellation\sensor-injector
  npm start -- --target 10.1.2.200 --pt1 725 --pt2 730 --pt3 720
  ```
  Or use the orbit-simulator panel at `http://10.1.2.200:9010` if that's wired up.
- [ ] **Capture tag 10 body via WS:**
  ```powershell
  node -e "const ws=new (require('ws'))('ws://10.47.27.108:9001/proto/stream');
  ws.on('open',()=>ws.send('{\"action\":\"subscribe\",\"tags\":[10]}'));
  ws.on('message',d=>{if(Buffer.isBuffer(d)){const t=d[0]|d[1]<<8,
  l=d[2]|d[3]<<8;if(t===10)console.log('tag10 len='+l+' body='+
  d.slice(4,4+l).toString('hex'));}});setTimeout(()=>process.exit(),10000);"
  ```
- [ ] **Decide canary outcome from the body's last 3 bytes:**
  | Last bytes  | Meaning                                                     | Action                                                         |
  |-------------|-------------------------------------------------------------|----------------------------------------------------------------|
  | `88 01 c1`  | `OrbitClient_GetSample(slot 0)` failed (orbit unreachable)  | STOP. Re-check Phase 0+1; STORAGE is not actually answering.   |
  | `88 01 c2`  | Sample OK but every register `0x7FFF` (UNDEF)               | Sensor injector didn't land. Re-run injector, capture again.   |
  | `88 01 c3`  | At least one float field emitted (success)                  | **PROCEED to Phase 4.**                                        |

---

## Phase 4 â€” Strip the canary (10 min)

> Only after a clean `c3` capture. Don't strip on `c1`/`c2`.

- [ ] Open `F:\Constellation\Nova_Firmware\lp_am2434\main.c`.
- [ ] Find the `build_system_status_envelope` function. Locate the three lines marked `Phase C canary â€”`. Delete them.
- [ ] Rebuild (Phase 0 build command).
- [ ] Re-flash NOVA (Phase 1 flash command).
- [ ] Re-capture tag 10 body â€” verify last bytes are NOT `88 01 cX` anymore (just regular proto fields).
- [ ] Update memory: edit `/memories/repo/phase-c-resume.md` to mark Phase C complete; remove the "BLOCKED" header.

---

## Phase 5 â€” Optional: flash STORAGE/GDC/TRITON to current firmware

Same recipe as Phase 1 with different `-Probe`/`-Role`/`-Ip`. Only needed if
you want the OTA :5503 listener on those orbits as well (so the UI version
row populates for all boards). Not required for Phase C completion.

---

## Phase 6 â€” Pack-up checks before leaving

- [ ] All four LPs respond to ping.
- [ ] UI Level 1 Version page shows correct firmware versions for any flashed boards.
- [ ] No `[DevCfg] legacy bank found` log lines on subsequent reboots (proves migration is one-shot).
- [ ] Document anything weird in `/memories/repo/` BEFORE you leave (won't remember details by tomorrow).
- [ ] Push any new memory files: they survive sessions automatically; no commit needed.

---

## What NOT to attempt at this bench session

- âŒ **Phase 1B firmware-side flash writes.** Architecturally a brick-risk
  (copy-on-activate). Wait for custom board with watchdog supervisor.
- âŒ **`10.47.27.x` migration.** That's a separate bench session â€” see
  `docs/Network-Migration-10.47.27.x.md`. Doing it interleaved with
  Phase C verification doubles the failure surface.
- âŒ **`orbitOtaPush.ts` end-to-end test against firmware.** Phase 1A
  firmware will reply NOT_IMPL=1 to every Begin frame. Code is shelved
  until Phase 1B / Phase 3.

---

## Cross-references (don't print, but read once before you go)

- `memories/repo/phase-c-resume.md` (deleted; referenced for historical context only) — was Phase C deep context
- [/memories/repo/lp-am2434-ota-phase1a.md](../memories/repo/lp-am2434-ota-phase1a.md) â€” OTA :5503 listener internals
- [/memories/repo/lp-am2434-bringup.md](../memories/repo/lp-am2434-bringup.md) â€” orbit board cold-bringup recipe
- [/memories/repo/lan-ip-map.md](../memories/repo/lan-ip-map.md) â€” SIP phone vs TI MAC trap
- [docs/LP-AM2434-OTA-Update-Plan.md](LP-AM2434-OTA-Update-Plan.md) â€” multi-phase OTA design
- [docs/Constellation-Board-Hardware-Spec.md](Constellation-Board-Hardware-Spec.md) â€” custom board must-haves
