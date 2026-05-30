# Agristar Simulation Environment — Setup Runbook

> ⚠️ **STALE — pre-Nova / pre-LP-AM2434 era (Apr 2026 migration).**
> This guide references `armSimulator.ts`, `rs485Responder.ts`, the
> `F:\Agristar\Agristar` project layout, and the legacy port 9001
> bridge with no LP firmware in the loop. None of those exist anymore.
> For the current dev workflow see
> [`docs/Simulator-to-Production-Transition.md`](../Simulator-to-Production-Transition.md)
> and [`/memories/repo/lp-am2434-bringup.md`](../../memories/repo/lp-am2434-bringup.md).
> Kept as-is for historical reference only — do not follow these steps.

Deterministic setup guide. Every step is a concrete command with an explicit verification check.
An AI agent or developer can follow this linearly from top to bottom on a fresh Windows machine.

All commands must run **in order**. Do not skip steps. Every `VERIFY:` line must pass before continuing.

---

## Variables

Set these once. Every subsequent command uses them.

```
REPO_WIN    = F:\Agristar\Agristar              # Windows path to repo
REPO_WSL    = /mnt/f/Agristar/Agristar          # WSL equivalent (auto-mapped)
QEMU_HOME   = ~/qemu-tm4c                       # Where QEMU is built inside WSL
TOOLCHAIN   = ~/aarch64-toolchain                # aarch64 cross-compiler
KERNEL_BUILD= /tmp/rpi-kernel-build              # Kernel build directory
IMAGE_RAW   = ${REPO_WSL}/Rpi5 image/pi5_btrfs_2.0.11.img
IMAGE_QCOW2 = ~/rpi5.qcow2                      # Persistent location (not /tmp)
RPI_KERNEL  = ~/rpi5_custom_kernel.img           # Compiled kernel
RPI_INITRD  = ~/rpi5_boot/initramfs_2712         # Extracted initrd
RPI_BOOT    = ~/rpi5_boot                        # Extracted boot files
FIRMWARE    = ${REPO_WSL}/Mini_IO/build/firmware.bin
SSH_USER    = gellert
SSH_PASS    = 4gri*st4r
```

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│  WSL2 Host                                                      │
│                                                                 │
│  RS485 Responder (:9002)  ◄──TCP──►  QEMU ARM TM4C129          │
│  Control Panel   (:9001)              ├── UART1 → TCP :9000     │
│                                       ├── UART2 → TCP :9002     │
│                                       └── CPLD  → TCP :9003     │
│                                              │                  │
│                                        TCP :9000                │
│                                              ▼                  │
│  QEMU aarch64 (RPi5 guest)                                     │
│    ├── Bridge Server (:3001 → tcp://10.0.2.2:9000)             │
│    ├── lighttpd (:80/:443)  ── /iot/* proxy → bridge :3001     │
│    ├── FastCGI: gellertgetd, gellertpostd                      │
│    ├── GellertFileSystem.out (:9209)                           │
│    ├── GellertQueryResponder (UDP :9210)                       │
│    ├── GellertEmailResponder                                   │
│    └── GellertProgResponder                                    │
│                                                                 │
│  Port Forwards: 2222→22(SSH) 8080→80(HTTP) 8443→443(HTTPS)    │
└─────────────────────────────────────────────────────────────────┘
```

---

## Phase 1 — Windows: Clone and Install Node.js

### 1.1 Clone or copy the repository

```powershell
# OPTION A: git clone
cd F:\Agristar
git clone https://github.com/scare66cro/AgriStar2Testing.git Agristar
```
```powershell
# OPTION B: copy entire folder from another machine
# Just copy F:\Agristar\Agristar to the same path on the new machine
```

**VERIFY:** `Test-Path F:\Agristar\Agristar\ui-svelte\server\package.json` → `True`

### 1.2 Copy large binary files (not in git)

These must be transferred separately (USB, network share, etc.):

| File | Copy to | Size |
|------|---------|------|
| RPi5 disk image | `F:\Agristar\Agristar\Rpi5 image\pi5_btrfs_2.0.11.img` | ~4 GB |
| ARM firmware binary | `F:\Agristar\Agristar\Mini_IO\build\firmware.bin` | ~900 KB |
| Upgrade package (optional) | `F:\Agristar\Agristar\as2 upgrade file\` | ~50 MB |

If you only have `AS2_1.02.bin` (full flash image) or `AS2.sre` (S-record), that is OK —
Step 6 will convert it to `firmware.bin`.

**VERIFY:** `Test-Path "F:\Agristar\Agristar\Rpi5 image\pi5_btrfs_2.0.11.img"` → `True`

### 1.3 Install Node.js on Windows (for VS Code TypeScript development)

```powershell
# If Node.js is not installed, download and run the installer:
# https://nodejs.org/en/download/ — pick LTS v20+
node --version
# VERIFY: output is v20.x.x or higher
```

### 1.4 Install bridge server dependencies

```powershell
cd F:\Agristar\Agristar\ui-svelte\server
npm install
```

**VERIFY:** `Test-Path F:\Agristar\Agristar\ui-svelte\server\node_modules\express\package.json` → `True`

### 1.5 Build the bridge server (TypeScript → JavaScript)

```powershell
cd F:\Agristar\Agristar\ui-svelte\server
npx tsc
```

**VERIFY:** Exit code 0, zero errors printed. `Test-Path F:\Agristar\Agristar\ui-svelte\server\dist\index.js` → `True`

### 1.6 Smoke test — run RS485 responder standalone (no QEMU needed)

```powershell
cd F:\Agristar\Agristar\ui-svelte\server
npx tsx src/rs485Responder.ts
```

**VERIFY:** Terminal shows `RS485 Responder listening on port 9002` and `Panel available at http://localhost:9001`. Open http://localhost:9001 in a browser — control panel loads. Press Ctrl+C to stop.

> **CHECKPOINT:** If you only need the bridge server for development (no RPi5 image, no QEMU), you are done. The bridge can also run in simulator mode: `npx tsx src/armSimulator.ts` (emulates firmware in pure TypeScript, no QEMU binary needed).

---

## Phase 2 — WSL2: Install Linux Environment

### 2.1 Enable WSL2 and install Ubuntu

```powershell
# Run in elevated (admin) PowerShell:
wsl --install -d Ubuntu-24.04
# Restart if prompted. Set a username/password when Ubuntu first opens.
```

**VERIFY:** `wsl -l -v` shows `Ubuntu-24.04` with VERSION `2`

### 2.2 Verify the repo is accessible from WSL

```bash
# Inside WSL:
ls /mnt/f/Agristar/Agristar/ui-svelte/server/package.json
```

**VERIFY:** File is listed (no "No such file"). If F: drive is not mounted, check `ls /mnt/` for available drives.

### 2.3 Install system packages

```bash
sudo apt update
sudo apt install -y \
    build-essential git python3 python3-pip curl wget \
    ninja-build pkg-config libglib2.0-dev libpixman-1-dev \
    libslirp-dev zlib1g-dev flex bison libssl-dev \
    qemu-utils btrfs-progs kpartx sshpass
```

**VERIFY:** `ninja --version` prints a version. `python3 --version` prints 3.x. `btrfs --version` prints a version.

### 2.4 Install Node.js inside WSL

```bash
curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v0.39.7/install.sh | bash
export NVM_DIR="$HOME/.nvm"
[ -s "$NVM_DIR/nvm.sh" ] && . "$NVM_DIR/nvm.sh"
nvm install 20
nvm use 20
```

**VERIFY:** `node --version` → `v20.x.x` or higher inside WSL

---

## Phase 3 — WSL2: Build Custom QEMU

The ARM TM4C129 firmware runs in a custom QEMU with an `agristar-as2` machine model.
This machine emulates the TM4C129ENCPDT SoC, CPLD shift registers, and three UARTs as TCP sockets.

### 3.1 Clone QEMU source

```bash
cd ~
git clone --depth=1 https://gitlab.com/qemu-project/qemu.git qemu-tm4c-src
cd ~/qemu-tm4c-src
git submodule update --init --recursive
```

**VERIFY:** `ls ~/qemu-tm4c-src/hw/arm/` directory exists and contains `.c` files

### 3.2 Copy the custom Agristar machine model into QEMU source tree

The project provides 4 files in `qemu-tm4c/hw/arm/tm4c129x/`:
- `agristar_as2.c` — machine definition (1258 lines, wires up CPU + peripherals)
- `tm4c129x_sysctl.c` — system control register model
- `meson.build` — build rules for the 2 C files
- `Kconfig` — config entry declaring `AGRISTAR_AS2`

```bash
BASE="/mnt/f/Agristar/Agristar"

# Copy C source files into QEMU's hw/arm/ directory
cp "$BASE/qemu-tm4c/hw/arm/tm4c129x/agristar_as2.c" ~/qemu-tm4c-src/hw/arm/
cp "$BASE/qemu-tm4c/hw/arm/tm4c129x/tm4c129x_sysctl.c" ~/qemu-tm4c-src/hw/arm/

# Append the meson.build rules to QEMU's hw/arm/meson.build
cat "$BASE/qemu-tm4c/hw/arm/tm4c129x/meson.build" >> ~/qemu-tm4c-src/hw/arm/meson.build

# Append the Kconfig entry to QEMU's hw/arm/Kconfig
cat "$BASE/qemu-tm4c/hw/arm/tm4c129x/Kconfig" >> ~/qemu-tm4c-src/hw/arm/Kconfig
```

**VERIFY:** `grep -c "AGRISTAR_AS2" ~/qemu-tm4c-src/hw/arm/Kconfig` → at least `1`
**VERIFY:** `grep -c "agristar_as2" ~/qemu-tm4c-src/hw/arm/meson.build` → at least `1`
**VERIFY:** `ls ~/qemu-tm4c-src/hw/arm/agristar_as2.c ~/qemu-tm4c-src/hw/arm/tm4c129x_sysctl.c` → both exist

> **NOTE:** The custom machine also references a header `hw/arm/tm4c129x.h`. Check if this file
> already exists; if not, copy it from the project's `qemu-tm4c/include/` directory:
> ```bash
> ls ~/qemu-tm4c-src/include/hw/arm/tm4c129x.h 2>/dev/null || \
>     cp "$BASE/qemu-tm4c/include/"*.h ~/qemu-tm4c-src/include/hw/arm/
> ```

### 3.3 Configure QEMU (ARM + aarch64 targets only)

```bash
cd ~/qemu-tm4c-src
mkdir -p build && cd build
../configure \
    --target-list=arm-softmmu,aarch64-softmmu \
    --prefix=$HOME/qemu-tm4c \
    --disable-docs
```

**VERIFY:** Configure finishes without errors. Last line says something like `Build directory: .../build`

### 3.4 Compile QEMU

```bash
cd ~/qemu-tm4c-src/build
make -j$(nproc)
```

This takes **10-30 minutes** depending on CPU. There should be zero errors at the end.

**VERIFY:** `ls ~/qemu-tm4c-src/build/qemu-system-arm` → file exists (executable)
**VERIFY:** `ls ~/qemu-tm4c-src/build/qemu-system-aarch64` → file exists (executable)

### 3.5 Install to the expected path

```bash
cd ~/qemu-tm4c-src/build
make install
```

**VERIFY — critical test:**
```bash
~/qemu-tm4c/bin/qemu-system-arm -M help 2>/dev/null | grep agristar
```
Must output: `agristar-as2         Agristar AS2 (TM4C129ENCPDT)` (or similar).
If this line is missing, the machine model was not compiled in — go back to 3.2.

> **PATH NOTE:** The start scripts expect the binaries at `~/qemu-tm4c/build/`. Create symlinks
> if `make install` put them in `~/qemu-tm4c/bin/` instead:
> ```bash
> mkdir -p ~/qemu-tm4c/build
> ln -sf ~/qemu-tm4c/bin/qemu-system-arm ~/qemu-tm4c/build/qemu-system-arm
> ln -sf ~/qemu-tm4c/bin/qemu-system-aarch64 ~/qemu-tm4c/build/qemu-system-aarch64
> ln -sf ~/qemu-tm4c/bin/qemu-img ~/qemu-tm4c/build/qemu-img
> ```

---

## Phase 4 — WSL2: Prepare ARM Firmware

You need `Mini_IO/build/firmware.bin`. If you already have it, skip to 4.3.

### 4.1 If you have `AS2_1.02.bin` (1 MB full flash image)

```bash
bash /mnt/f/Agristar/Agristar/qemu-tm4c/flash_firmware.sh \
    /mnt/f/Agristar/Agristar/AS2_1.02.bin
```

The script auto-detects the full-flash format, extracts the app from offset 0x1C000,
patches the vector table to bypass the bootloader, and writes `firmware.bin`.

### 4.2 If you have `AS2.sre` (S-record from upgrade package)

```bash
bash /mnt/f/Agristar/Agristar/qemu-tm4c/flash_firmware.sh \
    "/mnt/f/Agristar/Agristar/as2 upgrade file/AS2.sre"
```

### 4.3 Verify firmware exists

```bash
ls -lh /mnt/f/Agristar/Agristar/Mini_IO/build/firmware.bin
```

**VERIFY:** File exists and is between 500 KB and 1 MB.

---

## Phase 5 — WSL2: Prepare RPi5 Disk Image

### 5.1 Verify raw image exists

```bash
ls -lh "/mnt/f/Agristar/Agristar/Rpi5 image/pi5_btrfs_2.0.11.img"
```

**VERIFY:** File exists and is approximately 4 GB.

### 5.2 Run the automated setup script

```bash
bash /mnt/f/Agristar/Agristar/qemu-tm4c/setup_rpi5_image.sh
```

This script:
1. Converts raw `.img` → qcow2 at `/tmp/rpi5.qcow2`
2. Resizes to 16 GB
3. Extracts boot files (kernel, initrd, dtb) to `/tmp/rpi5_boot/`
4. Mounts btrfs root via `qemu-nbd` and patches:
   - `serial.conf` → `/dev/ttyAMA0`
   - `lighttpd.conf` → adds `/iot/*` proxy → bridge :3001
   - `StartResponders.sh` → disables `gellertserverd`
   - Installs `agristar-bridge.service` (systemd unit)

**VERIFY:** Script finishes with `RPi 5 Image Setup Complete` banner.

### 5.3 Move output files to persistent locations

The setup script writes to `/tmp/` which can be cleared on WSL restart.
Move everything to your home directory:

```bash
mv /tmp/rpi5.qcow2 ~/rpi5.qcow2
mv /tmp/rpi5_boot ~/rpi5_boot
# Kernel will be created in Phase 6 — it also goes to ~/
```

**VERIFY:**
```bash
test -f ~/rpi5.qcow2 && echo "OK: qcow2" || echo "FAIL: qcow2 missing"
test -f ~/rpi5_boot/initramfs_2712 && echo "OK: initrd" || echo "FAIL: initrd missing"
```

### 5.4 If btrfs mount failed (common on some WSL installs)

The script prints a warning and says to use SSH-based patching. That is OK — the image
is still created. Boot it once (Phase 7), then run the SSH-based patcher:

```bash
bash /mnt/f/Agristar/Agristar/qemu-tm4c/patch_rpi5_firstboot.sh
```

---

## Phase 6 — WSL2: Build RPi5 Custom Kernel

The RPi5 disk image uses **btrfs** as its root filesystem. Stock kernels load btrfs as a
module, but QEMU's boot process needs it **built-in** (`CONFIG_BTRFS_FS=y`). This phase
cross-compiles an aarch64 kernel with btrfs built-in.

### 6.1 Download the ARM cross-compiler

```bash
cd /tmp
wget -q https://developer.arm.com/-/media/Files/downloads/gnu/13.2.rel1/binrel/arm-gnu-toolchain-13.2.rel1-x86_64-aarch64-none-linux-gnu.tar.xz \
    -O aarch64-toolchain.tar.xz
mkdir -p ~/aarch64-toolchain
tar -xf aarch64-toolchain.tar.xz -C ~/aarch64-toolcwhat usershain --strip-components=1
rm aarch64-toolchain.tar.xz
```

**VERIFY:** `~/aarch64-toolchain/bin/aarch64-none-linux-gnu-gcc --version` → prints GCC version

> If the download URL has changed, go to https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads
> and find the latest `aarch64-none-linux-gnu` tarball for Linux x86_64 hosts.

### 6.2 Clone the RPi kernel source

```bash
cd /tmp
git clone --depth=1 --branch rpi-6.6.y https://github.com/raspberrypi/linux.git rpi-kernel-build
cd rpi-kernel-build
```

**VERIFY:** `/tmp/rpi-kernel-build/Makefile` exists

### 6.3 Generate default config for RPi5

```bash
cd /tmp/rpi-kernel-build
make ARCH=arm64 CROSS_COMPILE=$HOME/aarch64-toolchain/bin/aarch64-none-linux-gnu- bcm2712_defconfig
```

**VERIFY:** `/tmp/rpi-kernel-build/.config` exists and `grep CONFIG_BTRFS_FS .config` shows `=m` (module — will be changed to built-in next)

### 6.4 Patch config and build kernel with btrfs built-in

```bash
bash /mnt/f/Agristar/Agristar/qemu-tm4c/rebuild_btrfs_builtin.sh
```

This script:
1. Changes `CONFIG_BTRFS_FS=m` → `CONFIG_BTRFS_FS=y` (and ~12 other dependencies)
2. Runs `make olddefconfig` to resolve dependencies
3. Compiles the kernel Image with `make -j$(nproc)`
4. Copies the result to `/tmp/rpi5_custom_kernel.img`

Build takes **20-45 minutes** depending on CPU.

**VERIFY:**
```bash
grep 'CONFIG_BTRFS_FS=y' /tmp/rpi-kernel-build/.config && echo "OK: btrfs built-in" || echo "FAIL"
test -f /tmp/rpi5_custom_kernel.img && echo "OK: kernel built" || echo "FAIL"
```

### 6.5 Move kernel to persistent location

```bash
mv /tmp/rpi5_custom_kernel.img ~/rpi5_custom_kernel.img
```

**VERIFY:** `ls -lh ~/rpi5_custom_kernel.img` → file exists, ~20-40 MB

---

## Phase 7 — WSL2: Update start script paths (if needed)

The launcher script `start_agristar.sh` has hardcoded paths. If you moved files to
persistent locations in Phases 5-6, update these variables:

```bash
nano /mnt/f/Agristar/Agristar/qemu-tm4c/start_agristar.sh
```

Find and update these lines near the top:

```bash
# Original:
RPI_KERNEL="/tmp/rpi5_custom_kernel.img"
RPI_INITRD="/tmp/rpi5_boot/initramfs_2712"
RPI_DISK="/tmp/rpi5.qcow2"

# Change to:
RPI_KERNEL="$HOME/rpi5_custom_kernel.img"
RPI_INITRD="$HOME/rpi5_boot/initramfs_2712"
RPI_DISK="$HOME/rpi5.qcow2"
```

Also confirm `BASE` matches your repo location:
```bash
BASE="/mnt/f/Agristar/Agristar"    # <-- change if repo is at a different path
```

**VERIFY:**
```bash
grep 'RPI_KERNEL=' /mnt/f/Agristar/Agristar/qemu-tm4c/start_agristar.sh
# Should show the persistent ~/... paths
```

---

## Phase 8 — Launch the Full Stack

### 8.1 Kill any leftover processes

```bash
killall -9 qemu-system-arm 2>/dev/null || true
pkill -9 -f "qemu-system-aarch64" 2>/dev/null || true
pkill -f "tsx src/" 2>/dev/null || true
```

### 8.2 Start everything

```bash
bash /mnt/f/Agristar/Agristar/qemu-tm4c/start_agristar.sh
```

Or from Windows PowerShell:
```powershell
powershell -ExecutionPolicy Bypass -File F:\Agristar\Agristar\qemu-tm4c\Start-Agristar.ps1
```

### 8.3 What starts (in this exact order)

| # | Service | Ports | Wait condition |
|---|---------|-------|----------------|
| 1 | RS485 Responder | TCP :9002, HTTP :9001 | Port 9002 listening |
| 2 | QEMU ARM (TM4C129) | TCP :9000, :9002, :9003 | Port 9000 listening |
| 3 | Bridge Server | HTTP :3001 | Port 3001 listening |
| 4 | QEMU aarch64 (RPi5) | SSH :2222, HTTP :8080, HTTPS :8443 | HTTP 200 on :8080 |

After RPi5 boots, `patch_rpi5_qemu.sh` is automatically called to apply QEMU-specific
patches (idempotent — safe to re-run).

### 8.4 Verify the stack

```bash
# In a second WSL terminal:

# 1. RS485 panel
curl -s http://localhost:9001 | head -1
# VERIFY: returns HTML (<!DOCTYPE html> or similar)

# 2. Bridge API
curl -s http://localhost:3001/iot/frontmatter
# VERIFY: returns JSON with firmware data

# 3. RPi5 UI (may take 60-90 seconds after start)
WSL_IP=$(hostname -I | awk '{print $1}')
curl -s --connect-timeout 5 http://$WSL_IP:8080/ | head -1
# VERIFY: returns HTML

# 4. RPi5 SSH
sshpass -p "4gri*st4r" ssh -o StrictHostKeyChecking=no -p 2222 gellert@localhost "echo OK"
# VERIFY: prints "OK"
```

### 8.5 Access from browser (Windows)

| What | URL |
|------|-----|
| Agristar SvelteKit UI | `http://<WSL_IP>:8080` |
| RS485 Control Panel | `http://localhost:9001` |
| Bridge REST API | `http://localhost:3001/iot/frontmatter` |

Get the WSL IP: `wsl -d Ubuntu-24.04 -- hostname -I`

### 8.6 Graceful degradation (no RPi5)

If the RPi5 image/kernel/initrd files are missing, the startup script **still works**.
It skips RPi5 and starts only the RS485 responder + QEMU ARM + bridge in standalone mode.
You lose: lighttpd UI, history logging, email alerts, firmware upgrades, network discovery.
You keep: bridge REST API (:3001), RS485 control panel (:9001), full firmware emulation.

### 8.7 Stop the stack

Press `Ctrl+C` in the terminal running `start_agristar.sh`. It sends SIGTERM to all children.

---

## Phase 9 — Post-Setup: Apply Upgrades and Flash Firmware

### 9.1 Apply a production upgrade package to the running RPi5

```bash
bash /mnt/f/Agristar/Agristar/qemu-tm4c/apply_upgrade.sh \
    "/mnt/f/Agristar/Agristar/as2 upgrade file"
```

Steps performed automatically:
1. Takes a qcow2 snapshot (rollback with `--rollback`)
2. SCPs `control.zip` + `display.zip` contents into RPi5 guest
3. Runs official `Install.sh` inside guest
4. Calls `patch_rpi5_qemu.sh` to re-apply QEMU patches

**VERIFY:** Script finishes without errors. `curl http://localhost:8080/` still loads the UI.

### 9.2 Roll back an upgrade

```bash
bash /mnt/f/Agristar/Agristar/qemu-tm4c/apply_upgrade.sh --rollback
```

### 9.3 Flash new ARM firmware

```bash
# From S-record (from upgrade package):
bash /mnt/f/Agristar/Agristar/qemu-tm4c/flash_firmware.sh \
    "/mnt/f/Agristar/Agristar/as2 upgrade file/AS2.sre" --backup

# From full binary:
bash /mnt/f/Agristar/Agristar/qemu-tm4c/flash_firmware.sh \
    /mnt/f/Agristar/Agristar/AS2_1.02.bin
```

**VERIFY:** `ls -lh /mnt/f/Agristar/Agristar/Mini_IO/build/firmware.bin` shows updated timestamp.

---

## Phase 10 — Deploy to Real RPi5 Hardware

The bridge server works on real RPi5 hardware with one config change (`SERIAL_PORT`).

```bash
cd /mnt/f/Agristar/Agristar/ui-svelte/server
bash deploy/deploy.sh gellert@<PI_IP_ADDRESS> /dev/ttyAMA0
```

The deploy script:
1. Builds TypeScript locally (`npm run build`)
2. SCPs `dist/`, `package.json`, `package-lock.json` to the Pi
3. Runs `npm ci --production` on the Pi (installs native `serialport` for ARM64)
4. Installs `agristar-bridge.service` systemd unit
5. Starts the bridge with `SERIAL_PORT=/dev/ttyAMA0` (physical UART to TM4C129)

**VERIFY:** `curl http://<PI_IP>:3001/iot/health` returns OK.

---

## Logs Reference

| Log file | Content |
|----------|---------|
| `/tmp/agri_rs485.log` | RS485 Responder (analog board sim) |
| `/tmp/agri_qemu_arm.log` | QEMU ARM stderr |
| `/tmp/uart0.txt` | Firmware debug console (UART0 output) |
| `/tmp/agri_bridge.log` | Bridge server (REST/WebSocket) |
| `/tmp/agri_rpi5.log` | QEMU aarch64 stderr |
| `/tmp/agri_rpi5_serial.log` | RPi5 guest serial console |

---

## Port Reference

| Port | Service | Where |
|------|---------|-------|
| 3001 | Bridge Server (REST + WebSocket) | WSL host |
| 8080 | lighttpd HTTP (→ RPi5 :80) | WSL host → guest |
| 8443 | lighttpd HTTPS (→ RPi5 :443) | WSL host → guest |
| 2222 | SSH (→ RPi5 :22) | WSL host → guest |
| 9000 | QEMU ARM UART1 (serial to bridge) | WSL host |
| 9001 | RS485 Control Panel (web UI) | WSL host |
| 9002 | QEMU ARM UART2 (RS485 to responder) | WSL host |
| 9003 | QEMU ARM CPLD (digital I/O inject) | WSL host |
| 9209 | GellertFileSystem (history/logging) | RPi5 guest |
| 9210 | GellertQueryResponder (UDP discovery) | RPi5 guest |

---

## Troubleshooting — Exact Symptoms and Fixes

### SYMPTOM: `qemu-system-arm: -M agristar-as2: unsupported machine type`
**CAUSE:** Custom machine model not compiled into QEMU.
**FIX:**
```bash
grep "AGRISTAR_AS2" ~/qemu-tm4c-src/hw/arm/Kconfig
# If missing, go back to Phase 3 step 3.2 and re-copy files. Then rebuild:
cd ~/qemu-tm4c-src/build && make -j$(nproc) && make install
```

### SYMPTOM: RPi5 boots but shows `VFS: Cannot open root device "vda2"`
**CAUSE:** Kernel does not have btrfs built-in (it is compiled as a module `=m` instead of `=y`).
**FIX:**
```bash
grep 'CONFIG_BTRFS_FS=' /tmp/rpi-kernel-build/.config
# If it shows =m, re-run:
bash /mnt/f/Agristar/Agristar/qemu-tm4c/rebuild_btrfs_builtin.sh
cp /tmp/rpi5_custom_kernel.img ~/rpi5_custom_kernel.img
```

### SYMPTOM: Bridge says `ECONNREFUSED 127.0.0.1:9000`
**CAUSE:** QEMU ARM did not start, or has not opened port 9000 yet.
**FIX:** The bridge must start AFTER QEMU ARM opens port 9000. Check:
```bash
ss -tlnp | grep :9000
# If nothing, QEMU ARM is not running. Check /tmp/agri_qemu_arm.log
```

### SYMPTOM: btrfs mount fails in `setup_rpi5_image.sh`
**CAUSE:** WSL2 kernel may lack btrfs support or `qemu-nbd` cannot create device.
**FIX:** Boot the RPi5 first (Phase 8), then apply patches via SSH:
```bash
bash /mnt/f/Agristar/Agristar/qemu-tm4c/patch_rpi5_firstboot.sh
```

### SYMPTOM: Port already in use (`EADDRINUSE`)
**FIX:**
```bash
killall -9 qemu-system-arm 2>/dev/null || true
pkill -9 -f "qemu-system-aarch64" 2>/dev/null || true
pkill -f "tsx src/" 2>/dev/null || true
sleep 2
# Verify ports are free:
ss -tlnp | grep -E ':9000|:9001|:9002|:3001|:8080'
# Should show nothing
```

### SYMPTOM: Windows browser cannot reach `http://<WSL_IP>:8080`
**CAUSE:** Windows firewall blocks WSL network.
**FIX (elevated PowerShell):**
```powershell
New-NetFirewallRule -DisplayName "WSL" -Direction Inbound -InterfaceAlias "vEthernet (WSL)" -Action Allow
```

### SYMPTOM: `/tmp` files gone after WSL restart
**CAUSE:** Some WSL configurations clear `/tmp` on restart.
**FIX:** This is why Phase 5.3 and 6.5 move files to `~/`. If you skipped those steps:
```bash
# You need to re-run setup_rpi5_image.sh and rebuild_btrfs_builtin.sh
# Then move the outputs to ~/ as described in those phases
```

### SYMPTOM: Need to use a different repo path (not F:\Agristar\Agristar)
**FIX:** Update `BASE` in these files:
```bash
# These 4 scripts have a BASE= variable near the top:
grep -l 'BASE=' /mnt/f/Agristar/Agristar/qemu-tm4c/{start_agristar,setup_rpi5_image,apply_upgrade,flash_firmware}.sh
# Edit each one and change the BASE= line.
```

---

## Directory Map

```
Agristar/
├── Mini_IO/                    ARM firmware source (TI CCS project)
│   ├── build/firmware.bin      Compiled firmware for QEMU [NOT IN GIT]
│   ├── Application/            Firmware application code
│   └── Platform/               HAL, drivers, FreeRTOS
│
├── qemu-tm4c/                  QEMU emulation layer
│   ├── hw/arm/tm4c129x/        Custom QEMU machine model (C source)
│   │   ├── agristar_as2.c      Machine: CPU + UARTs + CPLD + GPIO
│   │   ├── tm4c129x_sysctl.c   System control registers
│   │   ├── meson.build          Build rules (append to QEMU's hw/arm/meson.build)
│   │   └── Kconfig              Config entry (append to QEMU's hw/arm/Kconfig)
│   ├── include/                 Header files (copy to QEMU's include/hw/arm/)
│   ├── start_agristar.sh       Full stack launcher (WSL)
│   ├── Start-Agristar.ps1      PowerShell wrapper for Windows
│   ├── setup_rpi5_image.sh     One-time RPi5 image prep (Phase 5)
│   ├── patch_rpi5_qemu.sh      Idempotent QEMU patches (auto-applied on boot)
│   ├── patch_rpi5_firstboot.sh  SSH-based fallback patcher
│   ├── apply_upgrade.sh        Apply production upgrade packages (Phase 9)
│   ├── flash_firmware.sh       Convert + flash firmware (Phase 4)
│   ├── convert_sre.py          Motorola S-record → raw binary converter
│   ├── rebuild_btrfs_builtin.sh  Custom kernel build (Phase 6)
│   └── install_build_tools.sh  Helper: install flex/bison without sudo
│
├── ui-svelte/                  SvelteKit UI (DO NOT MODIFY source code)
│   ├── src/                    Svelte application source
│   └── server/                 Bridge server (Node.js / TypeScript)
│       ├── src/
│       │   ├── index.ts            Bridge server entry point
│       │   ├── armSimulator.ts     ARM firmware simulator (pure TS, no QEMU)
│       │   ├── rs485Responder.ts   RS485 analog board simulator
│       │   ├── rs485Panel.ts       Web control panel (:9001)
│       │   ├── serialBridge.ts     UART↔TCP serial bridge
│       │   ├── protocol.ts         ^tag=value$CRC! protocol codec
│       │   ├── wsManager.ts        WebSocket connection manager
│       │   ├── apiRoutes.ts        REST API routes (/iot/*)
│       │   ├── dataCache.ts        Firmware data cache
│       │   └── upgradeManager.ts   OTA upgrade support
│       ├── deploy/
│       │   ├── agristar-bridge.service   systemd unit file
│       │   └── deploy.sh                 Real hardware deploy script
│       ├── package.json        Dependencies: express, ws, busboy, serialport, cors
│       └── tsconfig.json       TypeScript: ES2022, strict, ESNext modules
│
├── Rpi5 image/                 [NOT IN GIT] ~4 GB raw disk image
│   └── pi5_btrfs_2.0.11.img
│
├── as2 upgrade file/           [NOT IN GIT] Production upgrade package
├── AS2_1.02.bin                [NOT IN GIT] Full flash firmware image
├── UI-International/           Legacy HTML/JS UI (served by lighttpd)
├── Display/                    Gellert display daemon (C source)
├── LinuxHttpServer/            Gellert HTTP server (C source)
├── SPEC_Agristar/              Firmware unit tests (fff/Unity framework)
└── docs/                       This documentation
```

---

## Summary — What Gets Built Where

| Artifact | Built by | Output location | Persistent location |
|----------|----------|-----------------|---------------------|
| Bridge server JS | `npx tsc` (Windows or WSL) | `ui-svelte/server/dist/` | In repo |
| Custom QEMU | `make` in WSL | `~/qemu-tm4c-src/build/` | `~/qemu-tm4c/` |
| RPi5 qcow2 image | `setup_rpi5_image.sh` | `/tmp/rpi5.qcow2` | Move to `~/rpi5.qcow2` |
| RPi5 boot files | `setup_rpi5_image.sh` | `/tmp/rpi5_boot/` | Move to `~/rpi5_boot/` |
| Custom kernel | `rebuild_btrfs_builtin.sh` | `/tmp/rpi5_custom_kernel.img` | Move to `~/` |
| firmware.bin | `flash_firmware.sh` | `Mini_IO/build/firmware.bin` | In repo |
| SD card image | `start_agristar.sh` (auto) | `~/agri_sdcard.img` | Already persistent |
