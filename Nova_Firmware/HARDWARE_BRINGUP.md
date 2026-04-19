# Nova Firmware — Simulation to Real Hardware Bring-Up

This document lists **every change required** to take the Nova firmware from
the QEMU-based development environment to the real AM2434 Cortex-R5F
hardware on the Constellation control board.

The firmware is organized so that *everything that must change is either
guarded by `#ifdef QEMU_BUILD` or isolated to one file per subsystem*. The
shared Application layer (`Mini_IO/Application/`) is **never** modified —
all hardware differences live in `Nova_Firmware/Platform/`.

---

## 1. Build configuration

### 1.1 Remove `QEMU_BUILD` from the defines

File: `Nova_Firmware/Makefile`

```make
DEFS = \
    -Dgcc \
    -DPART_AM2434 \
    -DQEMU_BUILD \           # ← remove for production
    -DCONSTELLATION_NOVA \
    -DSNOWFLAKE \
    -DNO_SHA256 \
    -DAPP_BASE=0x00000000
```

Removing `-DQEMU_BUILD` automatically switches every HAL file below to its
real-hardware code path. `NO_SHA256` and `SNOWFLAKE` stay — those are
product flags, not simulator flags.

### 1.2 Switch the linker script

```make
LDSCRIPT = am2434_qemu_r5f0.ld    # ← change to am2434_r5f.ld
```

The `am2434_qemu_*.ld` scripts place code at QEMU's boot-RAM addresses.
`am2434_r5f.ld` is the real-silicon layout (TCM/MSRAM as per AM2434 TRM).

### 1.3 Toolchain flags

- `-marm` (no Thumb) **must stay** — FreeRTOS R5 context restore uses
  `movs pc, lr` which relies on ARM mode.
- `-mfloat-abi=softfp -mfpu=vfpv3-d16` is correct for the R5F. Do not
  switch to `hard` float without also rebuilding newlib.

---

## 2. HAL files with QEMU-specific code paths

Each of these already has a `#ifdef QEMU_BUILD ... #else ... #endif`
split. Removing `-DQEMU_BUILD` activates the real-hardware branch; no
source edits needed unless noted.

| File | QEMU path | Real-hardware path | Action |
|---|---|---|---|
| `hal_flash.c` | memcpy against OSPI memory-mapped region | TI MCU+ SDK `Flash_open`/`Flash_write`/`Flash_eraseBlk` | **Verify `CONFIG_FLASH0` matches SysConfig output for the W25Q64JV.** |
| `hal_uart.c` | QEMU 16550 FIFO driven from `Usart_ISR()` polling | AM2434 UART with real interrupts | **Wire the UART ISR into the VIM; remove the explicit `Usart_ISR()` call in `ThreadUIUpdate`.** |
| `hal_timer.c` | QEMU generic timer | AM2434 DMTimer / RTI | Verify tick frequency matches `configCPU_CLOCK_HZ`. |
| `hal_pwm.c` | No-ops (EPWM not modeled in QEMU) | AM2434 EPWM register programming (TBCTL/AQCTL/CMPA) | **Test against real fan/refrig/burner loads at low duty first.** |
| `hal_watchdog.c` | No-op kick | AM2434 RTI window watchdog | **Set a conservative timeout (e.g. 2 s) for initial bring-up; tune down later.** Requires `hal_watchdog_force_safe_gpio()` to be implemented — currently missing, breaks `nova_watchdog.elf` link (safe to ignore for main firmware; watchdog helper ELF is optional). |
| `hal_modbus_tcp.c` | In-process Orbit simulator over TCP | Real Modbus/TCP client to Orbit board over Ethernet | **Set the Orbit IP/port in `hal_orbit.c` init; confirm Ethernet driver init order in `main.c`.** |
| `hal_orbit.c` | Same as above | Same as above | Verify `orbit_discover()` timeout tolerates real-network RTT (bump from 100 ms to ~500 ms if needed). |

---

## 3. Threading differences

### 3.1 `ThreadSerialCom` — control loop
File: `nova_thread_overrides.c:623-657`

The QEMU override is simplified: it calls `ReadAnalogBoards` + `SetSystemState`
+ `SetMode` in a 1 s delay loop. **On hardware, restore the original
AS2/Mini_IO `ThreadSerialCom` implementation** (or verify the current
simplified version covers all needed periodic control actions — it does
for Constellation since Nova is a pure-digital I/O board without the
serial-shift bit-bang of AS2).

### 3.2 `ThreadUIUpdate` — UART RX polling
File: `nova_thread_overrides.c:800-810`

In QEMU we poll `Usart_ISR()` and `Usart_CharsBuffered()` in a tight loop
because QEMU doesn't deliver UART RX interrupts. On real hardware,
`Usart_ISR()` is called by the actual hardware interrupt; **remove the
explicit call** from the loop or guard it with `#ifdef QEMU_BUILD`.

### 3.3 `nova_delay_ms`
File: `nova_thread_overrides.c:255-270`

Already guarded — uses a busy-wait under `QEMU_BUILD`, `vTaskDelay` on
hardware. No changes.

---

## 4. Flash layout (must match real silicon)

The 8 MB W25Q64JV QSPI NOR is partitioned as defined in
`nova_fw_update.h`. These offsets are **absolute** and must be respected
by the bootloader and any external flashing tool:

```
0x000000 – 0x00FFFF   Boot header / bank metadata     (64 KB)
0x010000 – 0x1FFFFF   Bank A firmware image           (~2 MB)
0x200000 – 0x3FFFFF   Bank B firmware image           (~2 MB)
0x400000 – 0x5FFFFF   Golden recovery image           (~2 MB, write-once)
0x600000 – 0x7FFFFF   Settings vault                  (~2 MB)
```

### 4.1 Settings vault layout
File: `nova_settings_store.h`

```
+0x000000  Bank A header   (4 KB sector, FwBankHeader)
+0x001000  Bank A data     (up to ~500 KB)
+0x080000  Bank B header
+0x081000  Bank B data
+0x100000  Panel defaults header + data
+0x180000  Reserved
```

All regions are sector-aligned (4 KB). **No bring-up changes required** —
the same layout is valid on QEMU (RAM-backed) and real silicon
(W25Q64JV sector-erase 0x20, page-program 0x02).

### 4.2 First boot on blank flash

Blank flash reads as 0xFF. `read_and_verify_header()` rejects this
(magic 0xFFFFFFFF ≠ `FW_BANK_MAGIC`), `NovaSettings_Init()` sets
`active_bank = -1`, `NovaSettings_Load()` returns `NSS_ERR_NO_VALID_BANK`,
and `main.c` falls through to the factory defaults populated by
`Settings_Init()` + `SerialShift_Init()`. The first user "Save Settings"
writes to Bank A. **No factory pre-programming needed.**

---

## 5. OSPI memory-mapping (DAC mode) on real hardware

`hal_flash.c`'s real path expects the OSPI controller configured in
**DAC (Direct Access Controller) mode** so that reads through
`OSPI_DATA_BASE` (0x50000000) transparently hit the NOR. SysConfig should
generate this in the board init. **Verify:**

1. `board_init()` (SDK-generated) runs **before** `hal_flash_init()` in `main.c`.
2. `CONFIG_FLASH0` is the W25Q64JV handle defined in the SysConfig project.
3. Cache coherence: if the R5F data cache is enabled over OSPI, either
   mark the region non-cacheable in the MPU or call `CacheP_inv`/`CacheP_wb`
   around every `hal_flash_read`. The current driver assumes
   non-cacheable (safest for initial bring-up).

---

## 6. Network / Ethernet

The firmware currently uses **lwIP 1.4.1** from the shared `Mini_IO/Platform`
tree. Bring-up steps:

1. Implement AM2434 CPSW driver in a new `hal_net.c` (replacing the
   TM4C emac driver).
2. Update `lwip-1.4.1/ports/tiva-tm4c129/` equivalent for AM2434 — the
   lwIP core is unmodified, only the port layer changes.
3. Set the Orbit board IP in `hal_orbit.c` (`ORBIT_IP_ADDR` macro).
4. DHCP vs static: currently static, hardcoded via the Settings struct.
   No change needed at bring-up.

---

## 7. Interrupts / VIM configuration

QEMU delivers interrupts via the generic ARM GIC model; real hardware
uses the AM2434 VIM (Vectored Interrupt Manager). The startup code in
`startup_r5f.c` already installs the FreeRTOS vector table.

**On hardware, wire these ISRs in VIM init (in `pinout.c` or new `hal_irq.c`):**

- UART1 RX → `Usart_ISR` (replaces QEMU polling)
- DMTimer0 tick → FreeRTOS tick handler
- OSPI DMA done (if used) — not required for initial bring-up
- CPSW RX/TX → lwIP port callbacks

---

## 8. Pre-production smoke test checklist

In order, these should all succeed before declaring the firmware
hardware-ready:

- [ ] Boots to UART banner ("Nova AM2434: v1.0.0-nova")
- [ ] `hal_flash_init` succeeds ("OSPI flash initialized" message)
- [ ] `NovaSettings_Init` logs "no valid bank" on virgin flash
- [ ] Ethernet link-up within 5 s of boot
- [ ] Orbit Modbus/TCP handshake succeeds (`orbit_discover`)
- [ ] Bridge connects over UART1, full settings burst visible on UI
- [ ] User "Save Settings" bumps bank sequence by 1 and writes to the
      *inactive* bank
- [ ] Reboot: `NovaSettings_Load` recovers the saved blob, UI shows the
      same values
- [ ] `CMD_FACTORY_DEFAULT` wipes to defaults and persists
- [ ] Watchdog kicks continuously; intentional hang triggers reset within
      the configured timeout
- [ ] PWM outputs measured on scope match commanded duty within ±1%
- [ ] 24h soak test: no flash corruption, monotonic sequence counter,
      both banks readable and CRC-valid

---

## 9. Files that are **QEMU-only** and must not ship to production

None. Every QEMU-specific code path is compiled out when `QEMU_BUILD` is
undefined. The same source tree builds both targets.

---

## 10. Summary: single-knob switch-over

For a minimal-risk first-silicon build:

1. `Makefile`: remove `-DQEMU_BUILD`, set `LDSCRIPT = am2434_r5f.ld`.
2. Add the SDK-generated board init sources to `NOVA_PLATFORM_SRCS`.
3. Implement `hal_watchdog_force_safe_gpio()` (currently the only known
   undefined symbol on a full link).
4. Wire VIM interrupts for UART1 RX and the FreeRTOS tick.
5. Flash via JTAG to Bank A, set `active=1 sequence=1` in the boot header.

Everything else — settings persistence, firmware update protocol, Orbit
comms, bridge protocol, UI behavior — is **bit-identical** between
simulator and hardware because it all sits above the HAL line.
