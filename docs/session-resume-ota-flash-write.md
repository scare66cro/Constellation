# OTA Flash_write — session resume (2026-05-09, post-WRRSB-validation)

> **HISTORICAL — RESOLVED 2026-05-09 (firmware 0.A.112).** And the
> resolution was itself superseded twice:
>
> 1. The vTaskSuspendAll fix turned out to be incomplete on 0.A.113 —
>    see [`memories/repo/ota-flash-write-still-fails-0a113.md`](../memories/repo/ota-flash-write-still-fails-0a113.md).
> 2. The actual definitive fix landed in 0.A.141 by migrating
>    `Flash_norOspiWrite` calls to `hal_flash_write_dac` —
>    INDIRECT_WRITE_XFER is broken in FreeRTOS+CPSW runtime context;
>    DAC mode (CPU memcpy to `0x60000000 + flash_addr`) is the working
>    path. Canonical rule: invariant #11 + [`docs/lp-am2434-ospi-dac-writes.md`](lp-am2434-ospi-dac-writes.md).
>    Memory: [`memories/repo/lp-am2434-ota-dac-mode-fix.md`](../memories/repo/lp-am2434-ota-dac-mode-fix.md).
>
> The TI E2E ticket draft below was **never needed** — delete instead
> of sending. Full session timeline retained for context only.

## Status (2026-05-09 update)
**WRRSB SafeBoot-recovery hypothesis ruled out.** 0.A.108 with proper
protocol-switched WRRSB recovery validated functional on bench (probe B,
hardware S24L0707): chip accepts the command, NV write lands in ~10 ms,
SR1 reads back 0x00. **But the OTA Flash_write still fails identically**
(SR1=0x00 going into attempt 1 → chip wasn't in SafeBoot to begin with).
The post-scheduler INDIRECT_WRITE_XFER wedge is a **different root cause**
than the SafeBoot path 0.A.105–0.A.108 was chasing.

**Side casualty:** original Probe N hardware (S24L0957) bricked by 0.A.105
WRARG writes earlier in the day. ROM bootloader can't recover; needs
hardware-level fix. Probe B's hardware (S24L0707) reused as the dev
CONTROLLER until S24L0957 is restored.

## What's still wrong (2026-05-09 hypothesis)
Per `uart_ota_108_PROBE_B.log` — same shape as 0.A.104:
- WREN reaches chip via STIG path (SR1[1]=WEL set when checked between attempts)
- PP via `INDIRECT_WRITE_XFER` returns SystemP_FAILURE
- Chip stays clean (`SR1=0x00` per attempt, no error flags latched)
- Read-back of target page = 0xFF (data didn't land)
- `Sciclient_pmSetModuleRst` of FSS0_OSPI_0 doesn't recover

The wedge is in the **OSPI controller's INDIRECT_WRITE_XFER data path**
post-scheduler. STIG works (commands reach chip), INDIRECT_READ works
(Flash_read works post-scheduler), only INDIRECT_WRITE doesn't.

## Earlier (2026-05-08) status — kept for context
**Software escalation exhausted.** 27 firmware revisions today (0.A.78 → 0.A.104).
Chip is fine. SDK is fine. Controller is fine after every reset path. PP just
doesn't land. **This is silicon- or board-level.**

## Evidence we have (decisive)

`uart_ota_104.log` (latest):
```
[OTA] BEGIN ... first block erased at 0x900000
[OTA] BEGIN rc=0 erase_ms=889                          ← erase WORKS
[Flash] Write ENTER #1 addr=0x900000 len=256
[Flash] SR1=0x00 (attempt 1)                           ← chip clean
[Flash] OSPI peripheral hard-reset rs_assert=0 rs_deassert=0  ← Sciclient PM reset succeeded
[Flash] full PM-reset+OSPI+Flash re-init before retry 1
[Flash] SR1=0x00 (attempt 2)                           ← still clean after PM reset
[Flash] OSPI peripheral hard-reset rs_assert=0 rs_deassert=0
[Flash] post-fail verify @0x900000: FF FF FF FF FF FF FF FF  ← data did NOT land
[Flash] Write FAIL addr=0x900000 len=256 err=-1 attempts=3
```

## Eliminated hypotheses (with bench evidence)

| Hypothesis | Verdict | Test that ruled it out |
|---|---|---|
| Chip wedged (PRGERR/ERSERR set) | NO | SR1=0x00 every attempt (0.A.99) |
| Block protect bits | NO | SR1 BP bits=0 (0.A.99) |
| WREN not reaching chip | NO | WEL=1 after attempt 1 in 0.A.101 |
| Address-specific (0x200000) | NO | 0x400000 same fail (0.A.83); 0x900000 same fail (0.A.102) |
| pageSize override | NO | Tried 16, default — both fail (0.A.94) |
| HwiP_disable / IRQ corruption | NO | 0.A.95+; SDK 12.0 says OSPI is polling-only |
| Multi-PP / single-PP write size | NO | 16, 256, 1024-byte all fail (0.A.79+) |
| Sticky firmware (any retries help) | NO | Up to 8 retries × 100 ms — none succeed |
| SDK Flash driver state | NO | Flash_close+Flash_open recovery fires, no help (0.A.96) |
| OSPI driver state | NO | OSPI_close+OSPI_open re-init, no help (0.A.103) |
| OSPI controller state | NO | Full PM reset of FSS0_OSPI_0 module, no help (0.A.104) |
| CLPEF (clear program/erase failure) | NO — but already clean | st=0 every call, SR1 still 0x00 (0.A.99) |
| Address-decode / 4-byte mode | NO | 0x900000 (4-byte-only region) same fail (0.A.102) |
| Auto-flasher writes succeed at 0x80000 | YES — but only NoRTOS context | flasher_uart `write 1/2 ok` consistent |

## Confirmed working (post-scheduler, FreeRTOS context)

- **Flash_read** — works for any address (LpDeviceConfig_Init reads bank headers, NovaFwUpdate_Init reads, post-fail verify reads)
- **Flash_eraseBlk** — works at 0x400000, 0x900000 (Begin's first-block erase succeeds 800-1000 ms)
- **OSPI_writeCmd (STIG path)** — RSTEN, RST, CLPEF, WREN all work (chip's WEL gets set)
- **OSPI_readCmd (STIG path)** — SR1 reads return real values
- **Sciclient_pmSetModuleRst** — peripheral reset succeeds (rs=0)
- **Auto-flasher Flash_write at 0x80000** — works in NoRTOS context with same OSPI config

## NOT working

- **Flash_write (any size, any address) post-scheduler in FreeRTOS context.** Specifically the OSPI controller's INDIRECT_WRITE_XFER data path. STIG path works, INDIRECT_READ works, INDIRECT_WRITE doesn't.

## The shape of the bug

- WREN reaches chip (via STIG) → WEL=1
- PP attempted via INDIRECT_WRITE_XFER → no data lands
- Chip stays clean (no error flags)
- Surviving cold power-cycle would clear it; nothing else does

This pattern says either:
1. **OSPI hardware bug** — INDIRECT_WRITE_XFER state machine has a path that wedges in our specific config (DTR + PHY-disabled + post-many-reads), and the peripheral's PM reset doesn't actually reset that state.
2. **Silicon errata** — undocumented behavior of the FSS0_OSPI_0 block on AM243x ALX silicon at the runtime conditions we hit.
3. **Board-level** — power/decoupling on the OSPI bus marginal, but consistent across all our test cycles, this seems unlikely.
4. **SDK config bug** — some SysCfg setting we haven't found differs from what TI's published examples use.

## Monday's first move: file a TI E2E ticket

Use the draft below. Attach `uart_ota_104.log` and ideally one of the SR1=0x00 pattern logs (any of 0.A.99-104).

### Draft ticket title
> AM243X-LP S25HL512T OSPI Flash_write fails post-scheduler in FreeRTOS — WREN reaches chip, PP doesn't land, no SDK/controller/PM reset path recovers

### Draft ticket body

> **Setup:**
> - AM243X-LP (LaunchPad), R5FSS0_0 cluster, FreeRTOS
> - MCU+ SDK 12.00.00
> - Cypress S25HL512T OSPI flash, syscfg `FLASH_CFG_PROTO_4S_4D_4D`
> - OSPI driver: `phyEnable=FALSE`, `intrEnable=FALSE`, `dmaEnable=FALSE`, `dacEnable=FALSE`, `inputClkFreq=100MHz`, `baudRateDiv=4`
> - Same OSPI config as TI's `flasher_uart` (sbl_jtag_uniflash) example which DOES work in NoRTOS
>
> **Symptom:**
> Post-scheduler `Flash_write(handle, addr, buf, len)` from a FreeRTOS task returns `SystemP_FAILURE` for any address (tested 0x200000, 0x400000, 0x900000) and any length (16, 256, 1024 bytes). Read-back via `Flash_read` after the failed write shows the target page is still 0xFF — write genuinely did not land. The same Flash_write call against the same chip succeeds inside `flasher_uart` (NoRTOS) and in `lp_device_config::Save` when called pre-scheduler.
>
> **What works post-scheduler:**
> - `Flash_read` (any address)
> - `Flash_eraseBlk` (any address — confirmed via UART `[OTA] BEGIN rc=0 erase_ms=N`)
> - `OSPI_writeCmd` STIG path (WREN, CLPEF, chip RSTEN/RST all reach the chip — confirmed by SR1 reads)
> - `OSPI_readCmd` STIG path (SR1 cmd 0x05 returns real values — 0x00 when clean, 0x02 after WREN)
>
> **What fails:**
> - Specifically `Flash_norOspiWrite`'s `OSPI_writeIndirect` (INDIRECT_WRITE_XFER) data transfer path
> - WREN succeeds (`SR1[1] WEL = 1` confirmed), but PP that follows doesn't program any data
>
> **Recovery paths tried that DON'T work:**
> 1. `Flash_close + Flash_open` (re-runs `Flash_quirkQSPIEarlyFixup` SafeBoot recovery)
> 2. `OSPI_close + OSPI_open + Flash_open` (full driver tear-down)
> 3. `Sciclient_pmSetModuleRst(TISCI_DEV_FSS0_OSPI_0=75, 1)` then 0 (peripheral hardware reset via PM)
> 4. CLPEF (cmd 0x30) before each write — chip already clean per SR1
> 5. Up to 8 retries with 100 ms gaps + 50 ms post-reset settling
>
> **Only thing that recovers the chip:** physical cold power-cycle.
>
> **Question for TI:** is there a known issue with `Flash_norOspiWrite` post-scheduler on AM243X-LP + S25HL512T? What additional reset/init path beyond `Sciclient_pmSetModuleRst` is needed to clear OSPI peripheral state below the SDK driver level?
>
> **Attachments:**
> - UART log showing the failure pattern with full diagnostic trace
> - hal_flash.c source showing the recovery path that doesn't work
> - example.syscfg + ti_drivers_config.c showing identical OSPI driver settings to flasher_uart (which works)

## Files to attach to TI ticket
- `f:/Constellation/logs/uart_ota_104.utf8.log` (latest, shows PM reset path)
- `f:/Constellation/Nova_Firmware/Platform/hal_flash.c` (recovery path source)
- `f:/Constellation/Nova_Firmware/lp_am2434/ti-arm-clang/generated/ti_drivers_config.c` (OSPI config)
- `f:/Constellation/Nova_Firmware/lp_am2434/ti-arm-clang/generated/ti_board_open_close.c` (Flash devCfg)

## What we know is solid (so we don't re-litigate)
- The Flash_norOspiWaitReady `||` infinite-loop bug (`flash_nor_ospi.c:138`) is real, but ours is not affected because cmdRead succeeds (chip is responsive). The clamp on `flashBusyTimeout` from 6000000 → 100000 is good defense regardless.
- HwiP_disable doesn't help (and SDK 12.0 release notes say OSPI is polling-only — IRQs don't drive transfers anyway).
- pageSize override doesn't help (and itself broke writes — left at default 256).
- Chip software reset via raw RSTEN+RST without re-running Flash_open is harmful (puts chip in 1S-1S-1S, SDK keeps in DTR — protocol mismatch). Don't do this.
- Disabling `LWIP_*_APP` flags + removing `udp_iperf.c` from makefile is load-bearing for bridge UART2 TX. Don't disable. (Already documented in `memories/repo/lp-msram-bloat-audit-may2026.md`.)

## State of the firmware on disk

- Latest source: 0.A.104 with full PM-reset recovery path. Builds clean.
- Last successful flash: 0.A.104 (this evening's chip is wedged from the test).
- Bench needs cold power-cycle Monday before any flash attempt.

## Next actions (any of these are independent)
1. **File the TI E2E ticket** with the draft above.
2. **Hardware investigation** — talk to whoever has signal-integrity equipment and probe OSPI bus during a failing Flash_write. PP command appears to be sent (LED-style indication?) but data doesn't land. May be slew-rate or termination on the OSPI data lines.
3. **Try the SDK's `ospi_flash_file_io` example unmodified** — flash it standalone to the LP and verify whether IT can do FreeRTOS Flash_write on the same hardware. If it works, our setup has a difference we haven't found. If it fails, the bug is in our SDK / chip / board combo and TI needs to fix.
