#!/bin/bash
###############################################################################
# Boot the project-local rpi5 image in QEMU aarch64.
# Image lives in qemu-constellation/images/ (copied here from ~/rpi5.qcow2,
# excluded from git via .gitignore).
#
# Port forwards:
#   2222 → 22   (SSH)
#   8080 → 80   (HTTP / lighttpd if present)
#   8443 → 443  (HTTPS)
#   8181 → 3000 (SvelteKit UI — `uisvelte` systemd service, listens on :3000)
###############################################################################
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
IMG_DIR="$DIR/images"
QEMU="${QEMU_AARCH64:-$HOME/qemu-tm4c/build/qemu-system-aarch64}"

KERNEL="$IMG_DIR/rpi5_custom_kernel.img"
DISK="$IMG_DIR/rpi5.qcow2"

[ -x "$QEMU" ] || { echo "qemu-system-aarch64 not found at $QEMU"; exit 1; }
[ -f "$KERNEL" ] || { echo "Missing kernel: $KERNEL"; exit 1; }
[ -f "$DISK" ]   || { echo "Missing disk:   $DISK"; exit 1; }

LOG="${RPI5_LOG:-/tmp/rpi5_qemu.log}"
SERIAL="${RPI5_SERIAL:-/tmp/rpi5_serial.log}"

echo "[boot_rpi5] starting QEMU (log=$LOG, serial=$SERIAL)"

nohup "$QEMU" \
    -machine virt,gic-version=3 \
    -cpu max \
    -smp 4 \
    -m 2G \
    -kernel "$KERNEL" \
    -append "root=/dev/vda2 rootfstype=btrfs rootsubvol=@ rootwait rootdelay=5 console=ttyAMA0,115200 earlycon" \
    -drive "file=$DISK,format=qcow2,if=virtio" \
    -netdev user,id=net0,hostfwd=tcp::2222-:22,hostfwd=tcp::8080-:80,hostfwd=tcp::8443-:443,hostfwd=tcp::8181-:3000,hostfwd=tcp::9001-:9001 \
    -device virtio-net-device,netdev=net0 \
    -serial "file:$SERIAL" \
    -display none \
    -monitor none \
    > "$LOG" 2>&1 &

PID=$!
disown
echo "[boot_rpi5] PID=$PID"
echo $PID > /tmp/rpi5_qemu.pid
