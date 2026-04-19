#!/bin/bash
# Build SvelteKit UI and copy output back
# Run from within WSL: bash /mnt/f/Constellation/constellation-ui/build_production.sh
set -e
SRC="/mnt/f/Constellation/constellation-ui"
cd "$SRC"
echo "[build] Starting SvelteKit production build..."
TMPDIR=/tmp npx vite build
echo "[build] Build complete!"
ls -la "$SRC/build/index.js"
echo "[build] Output in $SRC/build/"
