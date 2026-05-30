#!/bin/bash
###############################################################################
# start_nova_qemu.sh — Launch ONLY the Nova QEMU instance (for Windows PS1)
#
# This is the minimal WSL-side script called by Start-Constellation.ps1.
# It starts QEMU with:
#   UART0 → debug log file
#   UART1 → TCP:9000 (bridge server connects here)
#   UART2 → TCP:5502 (Orbit simulator Modbus TCP tunnel)
#
# The Windows launcher handles: sensor sim, orbit sim, bridge, UI.
# This script handles ONLY QEMU.
###############################################################################

NOVA_DIR="/mnt/f/Constellation/Nova_Firmware"
NOVA_FW="$NOVA_DIR/build/nova_firmware.bin"
NOVA_COMMS="$NOVA_DIR/build/nova_comms.bin"
NOVA_WDG="$NOVA_DIR/build/nova_watchdog.bin"
QEMU_ARM="$HOME/qemu-tm4c/build/qemu-system-arm"

# Validate
for f in "$QEMU_ARM" "$NOVA_FW"; do
    if [ ! -f "$f" ]; then
        echo "ERROR: Missing $f" >&2
        exit 1
    fi
done

# Kill any existing Nova QEMU
pkill -f "constellation-nova" 2>/dev/null || true
sleep 1

# ---------------------------------------------------------------------------
# Persistent OSPI flash backing file.
#
# The Nova firmware stores settings in OSPI NOR via ping-pong banking
# (nova_settings_store.c).  Without a backing file the QEMU OSPI region
# is anonymous RAM and resets to zeros on every QEMU restart, causing
# saved settings to revert to defaults.
#
# constellation_nova.c reads NOVA_OSPI_FILE at machine init and, if set,
# backs the OSPI region with that file via memory_region_init_ram_from_file
# (mmap-shared).  The file is created automatically if it does not exist.
#
# IMPORTANT: This file MUST live on the WSL native filesystem (ext4),
# NOT on /mnt/f (DrvFS / Windows-mounted).  DrvFS does not honor
# MAP_SHARED writes — firmware writes succeed in RAM but never make it
# back to disk, defeating persistence entirely.  We mirror the file
# back to the Windows tree on QEMU shutdown so it can be inspected,
# committed to git history snapshots, etc.
#
# To force a clean settings reset: delete this file before launch.
# ---------------------------------------------------------------------------
NOVA_OSPI_DIR="$HOME/.constellation"
NOVA_OSPI_FILE="$NOVA_OSPI_DIR/nova_ospi.bin"
NOVA_OSPI_MIRROR="$NOVA_DIR/../qemu-constellation/nova_ospi.bin"
mkdir -p "$NOVA_OSPI_DIR"

# Seed the WSL-side file from the Windows-side mirror if the WSL copy
# does not exist yet (first run after migration), so we don't lose any
# pre-existing OSPI snapshot the user committed.
if [ ! -f "$NOVA_OSPI_FILE" ] && [ -f "$NOVA_OSPI_MIRROR" ]; then
    cp "$NOVA_OSPI_MIRROR" "$NOVA_OSPI_FILE"
    echo "[Nova QEMU] Seeded OSPI from mirror: $NOVA_OSPI_MIRROR"
fi

export NOVA_OSPI_FILE
echo "[Nova QEMU] OSPI persistence: $NOVA_OSPI_FILE (WSL ext4, mmap-shared)"

# When QEMU exits, copy the (now flushed) OSPI image back to the
# Windows-side mirror so it's visible from PowerShell / VS Code.
trap 'cp "$NOVA_OSPI_FILE" "$NOVA_OSPI_MIRROR" 2>/dev/null && \
      echo "[Nova QEMU] OSPI mirrored to $NOVA_OSPI_MIRROR"' EXIT

# Build QEMU command
# -smp matches mc->max_cpus in constellation_nova.c (4 vCPU TCG threads:
# R5F-0 control, R5F-1 comms, R5F-2 watchdog, R5F-3 spare).  Without
# this QEMU asserts in tcg_register_thread when secondary CPUs spawn.
QEMU_CMD="$QEMU_ARM -M constellation-nova -smp 4"
QEMU_CMD="$QEMU_CMD -kernel $NOVA_FW"

# Secondary core images — only load if QEMU supports multi-core.
# The constellation-nova machine must be compiled with max_cpus > 1.
# If not available, QEMU will fail with "Specified boot CPU#N is nonexistent".
# We try without secondary cores first if a flag file indicates single-core QEMU.
MULTI_CORE=0
if [ ! -f "$HOME/.constellation_single_core" ]; then
    if [ -f "$NOVA_COMMS" ] && [ -f "$NOVA_WDG" ]; then
        MULTI_CORE=1
    fi
fi

if [ "$MULTI_CORE" -eq 1 ]; then
    QEMU_CMD="$QEMU_CMD -device loader,file=$NOVA_COMMS,addr=0x00180000,cpu-num=1"
    QEMU_CMD="$QEMU_CMD -device loader,file=$NOVA_WDG,addr=0x001C0000,cpu-num=2"
    echo "[Nova QEMU] Multi-core: loading R5F-1 comms + R5F-2 watchdog"
else
    echo "[Nova QEMU] Single-core mode (R5F-0 only)"
fi

QEMU_CMD="$QEMU_CMD -display none"
QEMU_CMD="$QEMU_CMD -serial file:/tmp/constellation_uart0.txt"
QEMU_CMD="$QEMU_CMD -serial tcp::9000,server,nowait"

# UART2 connects to the Orbit simulator Modbus TCP on the Windows host.
# In WSL, 'localhost' doesn't reliably reach Windows-side services.
# Use the default gateway (Windows host IP) instead.
WIN_HOST=$(ip route show default | awk '{print $3}')
QEMU_CMD="$QEMU_CMD -chardev socket,id=uart2,host=$WIN_HOST,port=5502,reconnect=1 -serial chardev:uart2"

echo "[Nova QEMU] Starting: UART1→:9000, UART2→:5502"

# Run QEMU in foreground so the WSL session stays alive.
# (nohup + background doesn't survive WSL session exit — SIGHUP kills it)
exec $QEMU_CMD > /tmp/constellation_nova.log 2>&1
