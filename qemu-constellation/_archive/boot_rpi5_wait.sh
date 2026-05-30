#!/bin/bash
# Boot QEMU rpi5 and wait for SSH to be ready
set -e
DIR="$(cd "$(dirname "$0")" && pwd)"
QEMU="$HOME/qemu-tm4c/build/qemu-system-aarch64"
KERNEL="$DIR/images/rpi5_custom_kernel.img"
DISK="$DIR/images/rpi5.qcow2"
SERIAL="/tmp/rpi5_serial.log"
LOG="/tmp/rpi5_qemu.log"

# Kill any existing QEMU aarch64
pkill -f 'qemu-system-aarch64.*rpi5' 2>/dev/null || true
sleep 1

rm -f "$SERIAL" "$LOG"

echo "[boot] Starting QEMU aarch64..."
$QEMU \
    -machine virt,gic-version=3 \
    -cpu max \
    -smp 4 \
    -m 2G \
    -kernel "$KERNEL" \
    -append "root=/dev/vda2 rootfstype=btrfs rootwait rootdelay=5 console=ttyAMA0,115200 earlycon" \
    -drive "file=$DISK,format=qcow2,if=virtio" \
    -netdev user,id=net0,hostfwd=tcp::2222-:22,hostfwd=tcp::8080-:80,hostfwd=tcp::8443-:443,hostfwd=tcp::8181-:3000,hostfwd=tcp::9001-:9001 \
    -device virtio-net-device,netdev=net0 \
    -serial "file:$SERIAL" \
    -display none \
    -monitor none \
    -daemonize \
    -pidfile /tmp/rpi5_qemu.pid

echo "[boot] QEMU daemonized, PID=$(cat /tmp/rpi5_qemu.pid)"

# Wait for SSH
echo "[boot] Waiting for SSH on port 2222..."
for i in $(seq 1 60); do
    if SSHPASS='4gri*st4r' sshpass -e ssh \
        -o StrictHostKeyChecking=no \
        -o UserKnownHostsFile=/dev/null \
        -o PubkeyAuthentication=no \
        -p 2222 \
        -o ConnectTimeout=3 \
        gellert@localhost 'echo OK' 2>/dev/null; then
        echo "[boot] SSH ready after ~$((i*5))s"
        exit 0
    fi
    sleep 5
done
echo "[boot] TIMEOUT after 300s"
echo "[boot] Serial log tail:"
tail -20 "$SERIAL" 2>/dev/null
exit 1
