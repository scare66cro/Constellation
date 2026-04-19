# Constellation QEMU Emulator

QEMU machine models for the Agristar Constellation platform — **completely independent** from the existing AS2/TM4C emulator in `qemu-tm4c/`.

## Target Hardware

| Machine | Chip | CPU | SRAM | Flash | Role |
|---|---|---|---|---|---|
| `constellation-nova` | **AM2434** | 2× Cortex-R5F @ 800 MHz | 2 MB | 2 MB | Central controller — PID, state machines, VFD, card bus |
| `constellation-orbit` | **AM2432** | 2× Cortex-R5F @ 800 MHz | 1.5 MB | 1.5 MB | Remote I/O — Orbit, Pulsar, Triton (same silicon) |

## Project Structure

```
qemu-constellation/
├── README.md                               ← you are here
├── install_into_qemu.sh                    ← copies files into a QEMU source tree
├── include/
│   └── hw/arm/
│       └── am243x.h                        ← AM243x memory map + SoC variant info
└── hw/arm/am243x/
    ├── constellation_nova.c                ← Nova machine model (AM2434)
    ├── constellation_orbit.c               ← Orbit machine model (AM2432)
    ├── Kconfig                             ← QEMU build config
    └── meson.build                         ← QEMU build rules
```

## How It Works

The AM2434 and AM2432 are **TI Sitara AM243x** real-time MCUs with Cortex-R5F cores. QEMU supports the `cortex-r5f` CPU type natively. These machine models wire up:

- **Memory**: TCM (64 KB ATCM + 64 KB BTCM), MSRAM (2 MB / 1.5 MB), on-chip flash, QSPI window
- **UARTs**: 16550-compatible, mapped to QEMU serial backends
- **Peripheral stubs**: GPIO, SPI, I2C, timers, ADC, watchdog, Ethernet (CPSW), PRU-ICSS — logged but unimplemented until firmware needs them

This is the same approach used in `qemu-tm4c/` for the existing AS2 emulator: start with memory + UARTs + stubs, then incrementally implement peripherals as the firmware exercises them.

## Building

### 1. Clone QEMU

```bash
cd ~
git clone https://gitlab.com/qemu-project/qemu.git qemu-constellation
cd qemu-constellation
git checkout v9.2.0   # or latest stable
git submodule update --init --recursive
```

### 2. Install machine models

```bash
cd /path/to/Agristar/qemu-constellation
chmod +x install_into_qemu.sh
./install_into_qemu.sh ~/qemu-constellation
```

### 3. Configure and build

```bash
cd ~/qemu-constellation/build
mkdir -p ~/qemu-constellation/build && cd ~/qemu-constellation/build
../configure --target-list=arm-softmmu \
    --enable-tcg \
    --disable-kvm \
    --enable-slirp \
    --disable-docs \
    --disable-gtk \
    --disable-sdl

ninja -j$(nproc) qemu-system-arm
```

### 4. Verify

```bash
./qemu-system-arm -machine help | grep constellation
# Should show:
#   constellation-nova    Agristar Constellation Nova (AM2434 Cortex-R5F)
#   constellation-orbit   Agristar Constellation Orbit (AM2432 Cortex-R5F)
```

## Running

### Nova (central controller)

```bash
qemu-system-arm -machine constellation-nova \
    -kernel nova_firmware.bin \
    -serial stdio \                          # UART0: debug console
    -serial tcp::9100,server,nowait \        # UART1: RPi5 bridge
    -serial tcp::9101,server,nowait \        # UART2: RS-485 VFD bus
    -serial tcp::9102,server,nowait          # UART3: RS-485 spare
```

### Orbit (remote I/O card)

```bash
qemu-system-arm -machine constellation-orbit \
    -kernel orbit_firmware.bin \
    -serial stdio \                          # UART0: debug console
    -nic user,hostfwd=tcp::15020-:502        # Modbus TCP server on 502
```

### Full Stack (future)

```
Nova QEMU ──tcp:9100──→ RPi5 bridge ──→ Browser (Svelte UI)
    │
    ├──tcp:9101──→ VFD simulator (Modbus RTU over TCP)
    │
    ├──Modbus TCP──→ Orbit QEMU #1 (port 15021)
    ├──Modbus TCP──→ Orbit QEMU #2 (port 15022)
    ├──Modbus TCP──→ Pulsar QEMU #3 (port 15023)
    └──Modbus TCP──→ Triton QEMU #4 (port 15024)
```

## Relationship to qemu-tm4c

| | `qemu-tm4c/` | `qemu-constellation/` |
|---|---|---|
| Product | AS2 (current) | Constellation (next-gen) |
| MCU | TM4C129ENCPDT (Cortex-M4F) | AM2434/AM2432 (Cortex-R5F) |
| Architecture | ARMv7-M | ARMv7-R |
| Machine name | `agristar-as2` | `constellation-nova`, `constellation-orbit` |
| Shared code | None | None — completely independent |

## Development Roadmap

1. **Phase 1 (current)**: Machine models + memory map + UARTs + stubs
2. **Phase 2**: Timer (DMTIMER) implementation for FreeRTOS tick
3. **Phase 3**: GPIO with DRV8908/TMC2240 simulation models
4. **Phase 4**: CPSW Ethernet model for Modbus TCP between Nova and Orbits
5. **Phase 5**: PRU-ICSS stub (pass-through, no real PRU emulation)
6. **Phase 6**: QSPI flash model with ping-pong settings vault
7. **Phase 7**: Full stack — Nova + multiple Orbits + VFD sim + RPi5 bridge
