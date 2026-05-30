#!/bin/bash
###############################################################################
# Constellation Full Simulation Stack Launcher
# Starts the Constellation emulated system:
#   1. Orbit I/O QEMU (AM2432 R5F)   — RS-485 I/O card
#   2. Nova QEMU (AM2434 R5F)        — Central controller, UART1→9000
#   3. Bridge Server (REST/WebSocket) — port 3001
#   4. QEMU aarch64 (RPi 5 image)    — SSH→2222, HTTP→8080
#
# This is the Constellation equivalent of qemu-tm4c/start_agristar.sh.
# The original AS2 emulator is NOT affected by this script.
#
# Usage: bash start_constellation.sh
# Stop:  Ctrl+C
###############################################################################

BASE="/mnt/f/Constellation"
NOVA_DIR="$BASE/Nova_Firmware"
NOVA_FW="$NOVA_DIR/build/nova_firmware.bin"
SERVER_DIR="$BASE/constellation-ui/server"
QEMU_ARM="$HOME/qemu-tm4c/build/qemu-system-arm"
QEMU_AARCH64="$HOME/qemu-tm4c/build/qemu-system-aarch64"
# Project-local rpi5 image (qemu-constellation/images/, .gitignored).
# Was previously $HOME/rpi5_custom_kernel.img + $HOME/rpi5_constellation.qcow2;
# moved into the repo on 2026-04-20 so deploy is self-contained.
RPI_KERNEL="$BASE/qemu-constellation/images/rpi5_custom_kernel.img"
RPI_DISK="$BASE/qemu-constellation/images/rpi5.qcow2"

# Color helpers
RED='\033[0;31m'; GREEN='\033[0;32m'; CYAN='\033[0;36m'; YELLOW='\033[1;33m'; NC='\033[0m'
ok()   { echo -e "${GREEN}[✓]${NC} $*"; }
info() { echo -e "${CYAN}[i]${NC} $*"; }
warn() { echo -e "${YELLOW}[!]${NC} $*"; }
fail() { echo -e "${RED}[✗]${NC} $*"; }

PIDS=()
CLEANED=0

cleanup() {
    [ "$CLEANED" -eq 1 ] && return
    CLEANED=1
    echo ""
    warn "Shutting down Constellation stack..."
    for pid in "${PIDS[@]}"; do
        kill "$pid" 2>/dev/null
    done
    pkill -TERM -f "constellation-nova" 2>/dev/null || true
    pkill -TERM -f "constellation-orbit" 2>/dev/null || true
    pkill -TERM -f "rpi5_constellation" 2>/dev/null || true
    pkill -f "tsx src/index" 2>/dev/null || true
    sleep 2
    pkill -9 -f "constellation-nova" 2>/dev/null || true
    pkill -9 -f "constellation-orbit" 2>/dev/null || true
    sleep 1
    ok "Constellation stack stopped."
    exit 0
}
trap cleanup INT TERM EXIT

# ─── Preflight ──────────────────────────────────────────────────────────────
echo ""
echo -e "${CYAN}╔══════════════════════════════════════════════════════╗${NC}"
echo -e "${CYAN}║     Constellation Simulation Stack Launcher         ║${NC}"
echo -e "${CYAN}║     Nova (AM2434) + Orbit (AM2432) + RPi5           ║${NC}"
echo -e "${CYAN}╚══════════════════════════════════════════════════════╝${NC}"
echo ""

for cmd in npx node curl; do
    if ! command -v "$cmd" &>/dev/null; then
        fail "Required: $cmd"
        exit 1
    fi
done

MISSING=0
for f in "$QEMU_ARM" "$NOVA_FW"; do
    if [ ! -f "$f" ]; then
        fail "Missing: $f"
        MISSING=1
    fi
done
[ "$MISSING" -eq 1 ] && { fail "Cannot start."; exit 1; }

# Image lives in the repo (qemu-constellation/images/, .gitignored).
# If it's missing, fall back to copying from $HOME/rpi5.qcow2 (legacy location).
if [ ! -f "$RPI_DISK" ]; then
    SRC_DISK="$HOME/rpi5.qcow2"
    if [ -f "$SRC_DISK" ]; then
        info "Seeding $RPI_DISK from $SRC_DISK ..."
        mkdir -p "$(dirname "$RPI_DISK")"
        cp --reflink=auto "$SRC_DISK" "$RPI_DISK" 2>/dev/null || cp "$SRC_DISK" "$RPI_DISK"
        ok "RPi5 disk seeded into repo at $RPI_DISK"
    else
        warn "No rpi5 image at $RPI_DISK or $SRC_DISK — RPi5 emulation will be skipped"
    fi
fi

RPI_AVAILABLE=0
[ -f "$RPI_KERNEL" ] && [ -f "$RPI_DISK" ] && RPI_AVAILABLE=1

port_open() {
    (echo > /dev/tcp/localhost/"$1") 2>/dev/null
}

# Kill old Constellation instances (not AS2 ones)
info "Cleaning up old Constellation processes..."
pkill -TERM -f "constellation-nova" 2>/dev/null || true
pkill -TERM -f "constellation-orbit" 2>/dev/null || true
pkill -TERM -f "rpi5_constellation" 2>/dev/null || true
sleep 2

# ─── 1. Orbit I/O Card (AM2432 R5F) ───────────────────────────────────────
# The Orbit provides shift register I/O (relays, DI) over Modbus/RS-485.
# For now it runs on port 9102 (TCP serial) — Nova's UART2 connects here.
echo ""
info "Starting Orbit I/O QEMU (AM2432)..."
info "  (Orbit placeholder — uses RS485 Responder for now)"

# Until we have Orbit firmware, reuse the RS485 Responder on port 9002
cd "$SERVER_DIR" || { fail "Cannot cd to $SERVER_DIR"; cleanup; }

if [ -f "src/rs485Responder.ts" ]; then
    RS485_HOST=0.0.0.0 RS485_PORT=9002 RS485_MODE=server RS485_VERBOSE=0 RS485_PANEL_PORT=9001 \
        npx tsx src/rs485Responder.ts > /tmp/constellation_orbit.log 2>&1 &
    PIDS+=($!)
    
    for i in $(seq 1 30); do
        port_open 9002 && break
        sleep 1
    done
    port_open 9002 && ok "Orbit I/O Responder ready (port 9002)" || warn "Orbit responder slow to start"
fi

# ─── 2. Nova Central Controller (AM2434 R5F) ──────────────────────────────
# Nova firmware runs in QEMU with:
#   UART0 → debug console (log file)
#   UART1 → TCP:9000 (bridge server connects here)
#   UART2 → TCP:9002 (Orbit RS-485 — reconnect mode)
echo ""
info "Starting Nova QEMU (AM2434)..."
cd "$NOVA_DIR" || { fail "Cannot cd to $NOVA_DIR"; cleanup; }

"$QEMU_ARM" -M constellation-nova \
    -kernel "$NOVA_FW" \
    -display none \
    -serial file:/tmp/constellation_uart0.txt \
    -serial tcp::9000,server,nowait \
    -chardev socket,id=uart2,host=localhost,port=9002,reconnect=1 -serial chardev:uart2 \
    > /tmp/constellation_nova.log 2>&1 &
PIDS+=($!)

info "Waiting for Nova to open UART1 listener on port 9000..."
for i in $(seq 1 15); do
    port_open 9000 && break
    sleep 1
done

if port_open 9000; then
    ok "Nova QEMU listening on port 9000 (paused — waiting for bridge)"
else
    fail "Nova QEMU failed — check /tmp/constellation_nova.log"
    cleanup
fi

# ─── 3. Bridge Server ──────────────────────────────────────────────────────
echo ""
info "Starting Bridge Server..."
cd "$SERVER_DIR" || { fail "Cannot cd to $SERVER_DIR"; cleanup; }
SERIAL_PORT="tcp://localhost:9000" PORT=3001 \
    npx tsx src/index.ts > /tmp/constellation_bridge.log 2>&1 &
PIDS+=($!)

for i in $(seq 1 30); do
    port_open 3001 && break
    sleep 1
done

if port_open 3001; then
    ok "Bridge Server ready (port 3001)"
else
    fail "Bridge Server failed — check /tmp/constellation_bridge.log"
    cleanup
fi

# ─── 4. QEMU aarch64 (RPi 5 Image) ────────────────────────────────────────
if [ "$RPI_AVAILABLE" -eq 1 ]; then
    echo ""
    info "Starting RPi 5 Emulation (Constellation disk copy)..."
    "$QEMU_AARCH64" \
        -machine virt,gic-version=3 \
        -cpu max \
        -smp 4 \
        -m 2G \
        -kernel "$RPI_KERNEL" \
        -append "root=/dev/vda2 rootfstype=btrfs rootsubvol=@ rootwait rootdelay=5 console=ttyAMA0,115200 earlycon" \
        -drive "file=$RPI_DISK,format=qcow2,if=virtio" \
        -netdev user,id=net0,hostfwd=tcp::2222-:22,hostfwd=tcp::8080-:80,hostfwd=tcp::8443-:443,hostfwd=tcp::8181-:3000,hostfwd=tcp::9001-:9001 \
        -device virtio-net-device,netdev=net0 \
        -serial file:/tmp/constellation_rpi5_serial.log \
        -display none \
        -monitor none \
        > /tmp/constellation_rpi5.log 2>&1 &
    PIDS+=($!)
    
    # uisvelte.service inside the rpi5 image listens on :3000;
    # QEMU forwards host :8181 → guest :3000.
    for i in $(seq 1 150); do
        curl -s --connect-timeout 1 http://localhost:8181/ > /dev/null 2>&1 && break
        sleep 1
    done
    
    if curl -s --connect-timeout 2 http://localhost:8181/ > /dev/null 2>&1; then
        ok "RPi 5 booted — UI at http://localhost:8181"
    else
        warn "RPi 5 still booting — UI should appear at http://localhost:8181 shortly"
    fi

    PATCH_SCRIPT="/mnt/f/Agristar/Agristar/qemu-tm4c/patch_rpi5_qemu.sh"
    if [ -f "$PATCH_SCRIPT" ]; then
        info "Applying QEMU patches..."
        for i in $(seq 1 30); do
            if SSHPASS="4gri*st4r" sshpass -e ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
               -o LogLevel=ERROR -o ConnectTimeout=3 -p 2222 gellert@localhost "echo ok" 2>/dev/null | grep -q ok; then
                break
            fi
            sleep 1
        done
        bash "$PATCH_SCRIPT" --quiet && ok "Patches applied" || warn "Some patches may have failed"
    fi
fi

# ─── Summary ───────────────────────────────────────────────────────────────
echo ""
echo -e "${CYAN}═══════════════════════════════════════════════════════${NC}"
echo -e "${GREEN} Constellation Stack Running${NC}"
echo -e "${CYAN}═══════════════════════════════════════════════════════${NC}"
echo ""
echo -e "  Nova (AM2434)    UART debug:  /tmp/constellation_uart0.txt"
echo -e "  Nova UART1       Bridge:      tcp://localhost:9000"
echo -e "  Orbit (AM2432)   RS-485:      tcp://localhost:9002"
echo -e "  Bridge Server    REST/WS:     http://localhost:3001"
[ "$RPI_AVAILABLE" -eq 1 ] && \
echo -e "  RPi 5 UI         HTTP:        http://localhost:8080"
echo ""
echo -e "  ${YELLOW}Press Ctrl+C to stop all services${NC}"
echo ""

# Keep alive
wait
