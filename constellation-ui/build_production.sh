#!/bin/bash
# Build SvelteKit UI and copy output back
# Run from within WSL: bash /mnt/f/Agristar/Agristar/Constellation/constellation-ui/build_production.sh
set -e
SRC="/mnt/f/Agristar/Agristar/Constellation/constellation-ui"
cd "$SRC"
echo "[build] Starting SvelteKit production build..."
TMPDIR=/tmp npx vite build
echo "[build] Build complete!"
ls -la "$SRC/build/index.js"
echo "[build] Output in $SRC/build/"
