# LP-AM2434 — Ethernet Bringup Plan

**Goal:** get the LP-AM2434 LaunchPad onto the LAN over its built-in
RGMII RJ45 jack, reachable from the orbit-sim host, so the LP firmware
can later poll Modbus TCP at `10.1.2.100:5502+` and emit `SystemStatus`
proto frames over UART to the bridge.

**Current state (entering this plan):**
- LP firmware builds + runs at 921600 baud over UART2 to RPi5 bridge.
- Bridge sees `connected:true` (Heartbeat + DataLoadStatus emitted).
- LP firmware has **no IP stack, no Ethernet driver, no PHY init**.
- Ethernet cable is physically plugged in to LP RJ45 (per user).

**Hardware on the LP-AM2434:**
- One on-board RJ45 driven from CPSW2G via RGMII to a TI DP83867 PHY.
- A second copper port is exposed via the SOM-style headers but the
  on-board jack is what we need.
- PHY MDIO/MDC on `PRG1_MDIO0_MDIO/MDC`. PHY reset via GPIO `GPMC0_AD11`.
- RGMII MUX select via GPIO `GPMC0_AD12`.
- (All of the above are pinned in the SDK reference syscfg —
  `C:\ti\mcu_plus_sdk_am243x_12_00_00_26\source\networking\enet\core\examples\lwip\enet_lwip_cpsw\am243x-lp\r5fss0-0_freertos\example.syscfg`.)

**SDK building blocks** (all already present in
`C:\ti\mcu_plus_sdk_am243x_12_00_00_26`):
- `source/networking/enet/lib/enet-cpsw.am243x.r5f.ti-arm-clang.freertos.release.lib`
- `source/networking/enet/lib/lwipif-cpsw-freertos.am243x.r5f.ti-arm-clang.release.lib`
- `source/networking/lwip/lib/lwip-freertos.am243x.r5f.ti-arm-clang.release.lib`
- `source/networking/lwip/lib/lwip-contrib-freertos.am243x.r5f.ti-arm-clang.release.lib`
- Reference example sources (`test_enet.c`, `test_enet_cpsw.c`,
  `enetextphy.c`, `dp83867.c`, …).

---

## Phased plan

### Phase A — Build & run the unmodified SDK reference example

**Why:** prove that the LP hardware (PHY, link, MAC, MDIO) is healthy
and that the SDK's lwIP DHCP works on this exact board, **before** we
start grafting it into our Constellation project. This is a 30-minute
checkpoint, not a long detour.

1. `cp -r` the SDK reference dir to a scratch location:
   `Nova_Firmware/lp_am2434_enet_ref/r5fss0-0_freertos/`.
2. Build with the SDK's makefile via `gmake -C ti-arm-clang all`.
3. Load on the LP with the same `Load-Nova.ps1` mechanism (it loads
   any `.out` over XDS110).
4. Plug a USB-serial dongle on UART0 (XDS110 USB CDC), open at
   115200 8N1.
5. Expected console output:
   ```
   [LWIPIF_LWIP] NETIF status: LINK_DOWN ...
   [LWIPIF_LWIP] Enet has been started successfully
   [LWIPIF_LWIP] NETIF status: LINK_UP ...
   [DHCPC] Got IP address : 192.168.x.x
   ```
6. From a host on the same LAN: `ping <ip>` → success.
7. **Exit criterion:** DHCP IP shown in console + ping reply.

If Phase A fails the rest of the plan is moot. Likely failures and
fixes:
- "no link" → cable to a switch that does autoneg, check LEDs on jack.
- "no DHCP" → put a DHCP server on the LAN (most home routers do).
- "MDIO read fails" → PHY reset GPIO not asserting; check pinmux.

### Phase B — Merge Ethernet into the Constellation `lp_am2434/` project

Goal: keep our existing UART2 bridge + heartbeat/DLS path **and** add
Ethernet. Single firmware image.

1. Copy the SDK reference's syscfg additions into our
   `lp_am2434/example.syscfg`:
   - `enet_cpsw` module + 2 `netifInstance`s + 2 `ethphy_cpsw_icssg`.
   - `udma` module.
   - 2 GPIO instances (PHY reset + RGMII mux sel).
   - `i2c0` + `eeprom` (CPSW examples use board EEPROM for MAC).
   - Extra `mpu_armv7` regions for ENET DMA.
   - Heap bump (`general1.heap_size = 34000`).
   - 2 new memory sections: `.enet_dma_mem`, `bss:ENET_DMA_*`.
2. Regenerate `generated/` via SysConfig (`sysconfig_cli`).
3. Copy these files from the SDK reference into our project (under
   `lp_am2434/enet/`):
   - `test_enet.c`, `test_enet_cpsw.c`, `udp_iperf.c`, `enetextphy.c`,
     `enetextphy_phymdio_dflt.c`, `dp83867.c`, `dp83869.c`,
     `generic_phy.c`, `lwipcfg.h`, `test_enet_extphy.h`,
     `test_enet_lwip.h`.
   - We will NOT take the SDK `main.c` — keep our own.
4. Update `ti-arm-clang/makefile`:
   - Add the above sources to `FILES_common` (or new `FILES_enet`).
   - Add SDK include paths for `networking/enet/...`,
     `networking/lwip/...`, `board/ethphy/...`.
   - Add the four new libs to `LIBS_common` (`enet-cpsw`, `lwipif-cpsw`,
     `lwip`, `lwip-contrib`).
5. In our `main.c`:
   - After `Drivers_open()` + `Board_driversOpen()`, call the
     example's `enet_lwip_example()` once (it spawns the DHCP task,
     does not return).
   - Refactor: extract `enet_init_task()` so it returns control after
     DHCP is up, then we keep running our existing UART2 task.
6. Build & load. Verify:
   - UART2 still emits Heartbeat → bridge still `connected:true`.
   - UART0 console still shows DHCP IP.
   - `ping <ip>` still works.

### Phase C — TCP smoke test to orbit-sim

Goal: prove the LP can open a TCP socket to `10.1.2.100:5502` (orbit-sim
slot 0 / TRITON or wherever).

1. Add a `lwip_socket_smoke_task` that runs once after DHCP is up.
2. `socket()`, `connect()` to `ORBIT_HOST:5502`, send a 12-byte
   Modbus TCP "Read Holding Registers" frame for HR 200 length 4,
   read response, log to UART0, close.
3. Exit criterion: orbit-sim log shows the Modbus request; LP console
   logs the 4 register values.

### Phase D — Port `hal_modbus_tcp.c` + `hal_orbit.c`

After Phase C confirms TCP works, the existing `Nova_Firmware/Platform/`
HAL files are drop-in usable. We just need to wire `Orbit_Init()` and
a periodic poll task.

(Detailed plan deferred until Phase C is green — no point until we know
TCP works.)

### Phase E — `SystemStatus` proto encode + UART2 ship

After Phase D produces fresh sensor readings every poll cycle, encode
them into a `SystemStatus` envelope using
`Nova_Firmware/Platform/nova_messages.c` and ship via the existing
`NovaProto_SendRaw()` mutex path. UI `frontMatterComposite` reads
exactly this proto stream → live sensors in the home page.

---

## Execution order in this session

We do **A → B** in this session (or A only if it fails and needs
hardware diagnosis). **C–E** are tracked but executed in follow-on
sessions to keep change sets reviewable.

## Lessons / gotchas to log as we go

Append to this file's "Lessons" section below. Examples of expected
gotchas:
- LP RJ45 uses the *secondary* CPSW port (RGMII2) on this board — the
  SDK example actually ties both PHYs and host port in mac-only mode;
  the working PHY is enumerated at MDIO addr discovered by enetextphy
  (likely 0 or 3 — TBD).
- DHCP timeout vs UART2 task starvation — make sure the lwIP TCP/IP
  thread doesn't block our 100 ms UART task.
- `general1.heap_size` for FreeRTOS heap_4 must be ≥ ~28 KB for lwIP +
  enet pbufs. Existing project uses default heap; will need bump.

### Lessons (append on each gotcha)

**2026-04-24 — Phase A complete, Ethernet hardware confirmed working.**
- Built `Nova_Firmware/lp_am2434_enet_ref/` from SDK
  `enet_lwip_cpsw/am243x-lp/r5fss0-0_freertos`. Loads cleanly via
  `dss.bat load_enet_ref.js`. Both PHYs come up, MDIO addr 3 (port 0)
  and 15 (port 1).
- DHCP did NOT work on this LAN — `dhcp-enabled` banner but never got
  past `0.0.0.0`. Switched `test.c` to `USE_DHCP 0` / `USE_AUTOIP 0`
  with `gStaticIP[]` = `10.1.2.210` and `10.1.2.211`. Rebuild + reflash.
  (Originally used `.150/.151` until 2026-05-01 — moved out of the
  phone-system range; new default lives in
  `lp_device_config.h::LP_DEVCFG_DEFAULT_IP_CONTROLLER`.)
- After static-IP build: `[0]status_callback==UP, IP 10.1.2.210` +
  `[0] link_callback==UP`. **`ping 10.1.2.210` returns 4/4 @ ~0ms** from
  Windows host on the same 10.1.2.x switch. Phase A GREEN.
- Port 1 (10.1.2.211) — no cable, expected timeout. Both ports init OK.
- Phase A image MUST be reflashed only after a physical USB-C power
  cycle — see [`/memories/repo/lp-am2434-cpsw-reflash-trap.md`](#).
  DSS `target.reset()` does NOT clear the CPSW peripheral. Once the
  CPSW driver asserts (`Cpsw_openInternal:904 hRxRsvdFlow != NULL`),
  the JTAG DAP wedges (-1170) and the only fix is unplug 5s.
- Boot timing: nova banner at ~10s, "ENET LWIP App" banner ~70s after
  power-on (SDK example sleeps before calling `EnetApp_init`). Ping
  works ~80s after power-on.
- MAC addresses (programmed by SDK board EEPROM read):
  port 0 = `1c:63:49:13:d8:61`, port 1 = `70:ff:76:1f:af:c4`.

**Phase B starts next** — graft enet syscfg + sources into our
`Nova_Firmware/lp_am2434/` project while keeping the UART2 bridge alive.
See session note `/memories/session/lp-enet-phase-a-success.md` for the
concrete graft list.
