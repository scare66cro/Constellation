#!/bin/bash
# install_into_qemu.sh — Copy Constellation machine models into a QEMU source tree
#
# Usage:
#   ./install_into_qemu.sh /path/to/qemu-source
#
# This copies the AM243x machine files into the QEMU tree and patches
# the parent meson.build and Kconfig to include them.  After running
# this, reconfigure and rebuild QEMU.
#
# The script is idempotent — safe to run multiple times.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
QEMU_DIR="${1:-$HOME/qemu-constellation}"

if [ ! -f "$QEMU_DIR/meson.build" ]; then
    echo "ERROR: $QEMU_DIR does not look like a QEMU source tree"
    echo "Usage: $0 /path/to/qemu-source"
    exit 1
fi

echo "=== Installing Constellation machine models into $QEMU_DIR ==="

# 1. Copy machine model source files
DEST="$QEMU_DIR/hw/arm/am243x"
mkdir -p "$DEST"
cp -v "$SCRIPT_DIR/hw/arm/am243x/constellation_nova.c"  "$DEST/"
cp -v "$SCRIPT_DIR/hw/arm/am243x/constellation_orbit.c" "$DEST/"
cp -v "$SCRIPT_DIR/hw/arm/am243x/Kconfig"               "$DEST/"
cp -v "$SCRIPT_DIR/hw/arm/am243x/meson.build"           "$DEST/"

# 2. Copy header
DEST_INC="$QEMU_DIR/include/hw/arm"
mkdir -p "$DEST_INC"
cp -v "$SCRIPT_DIR/include/hw/arm/am243x.h" "$DEST_INC/"

# 3. Patch hw/arm/meson.build to include our subdirectory
ARM_MESON="$QEMU_DIR/hw/arm/meson.build"
if ! grep -q "am243x" "$ARM_MESON"; then
    echo "" >> "$ARM_MESON"
    echo "# Agristar Constellation AM243x machines" >> "$ARM_MESON"
    echo "subdir('am243x')" >> "$ARM_MESON"
    echo "  Patched: $ARM_MESON"
else
    echo "  Already patched: $ARM_MESON"
fi

# 4. Patch hw/arm/Kconfig to source our Kconfig
ARM_KCONFIG="$QEMU_DIR/hw/arm/Kconfig"
if ! grep -q "am243x" "$ARM_KCONFIG"; then
    echo "" >> "$ARM_KCONFIG"
    echo "source am243x/Kconfig" >> "$ARM_KCONFIG"
    echo "  Patched: $ARM_KCONFIG"
else
    echo "  Already patched: $ARM_KCONFIG"
fi

echo ""
echo "=== Done. Now rebuild QEMU: ==="
echo "  cd $QEMU_DIR/build"
echo "  ../configure --target-list=arm-softmmu --enable-tcg --disable-kvm \\"
echo "    --enable-slirp --disable-docs --disable-gtk --disable-sdl"
echo "  ninja -j\$(nproc) qemu-system-arm"
echo ""
echo "Then test:"
echo "  ./qemu-system-arm -machine constellation-nova -kernel nova.bin -serial stdio"
echo "  ./qemu-system-arm -machine constellation-orbit -kernel orbit.bin -serial stdio"
