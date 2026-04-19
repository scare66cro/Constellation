#!/bin/bash
# ═══════════════════════════════════════════════════════════════════
# Package the Agristar bridge server + SvelteKit UI for deployment
# to production Raspberry Pi 5 hardware.
#
# Creates:  deploy/agristar-vfd-upgrade.tar.gz
#
# To install on a Pi:
#   scp agristar-vfd-upgrade.tar.gz gellert@<PI_IP>:/tmp/
#   ssh gellert@<PI_IP>
#   cd /tmp && tar xzf agristar-vfd-upgrade.tar.gz && sudo bash install.sh
#
# Or use deploy.sh to deploy directly over SSH:
#   ./deploy/deploy.sh gellert@<PI_IP> --vfd-host=<GATEWAY_IP> --vfd-port=502
# ═══════════════════════════════════════════════════════════════════
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SERVER_DIR="$SCRIPT_DIR/server"
BUILD_DIR="$SCRIPT_DIR/build"
DEPLOY_DIR="$SCRIPT_DIR/server/deploy"
PKG_DIR="/tmp/agristar-vfd-pkg"
PKG_NAME="agristar-vfd-upgrade.tar.gz"

echo "╔═══════════════════════════════════════════════════════╗"
echo "║  Agristar VFD Fan Control — Production Packager      ║"
echo "╚═══════════════════════════════════════════════════════╝"

# Step 1: Verify builds exist
echo ""
echo "[1/4] Checking build outputs..."

if [ ! -f "$SERVER_DIR/dist/index.js" ]; then
  echo "  Bridge server not built. Building..."
  cd "$SERVER_DIR" && npx tsc
fi
echo "  ✓ Bridge server: $SERVER_DIR/dist/index.js"

if [ ! -f "$BUILD_DIR/index.js" ]; then
  echo "  ERROR: SvelteKit UI not built. Run: cd ui-svelte && npx vite build"
  exit 1
fi
echo "  ✓ SvelteKit UI:  $BUILD_DIR/index.js"

# Step 2: Create package directory
echo ""
echo "[2/4] Assembling package..."
rm -rf "$PKG_DIR"
mkdir -p "$PKG_DIR/bridge-server/dist"
mkdir -p "$PKG_DIR/ui-svelte"

# Bridge server — only production files (no simulator, responder, panel)
BRIDGE_FILES=(
  index.js apiRoutes.js dataCache.js serialBridge.js protocol.js
  wsManager.js logDataStore.js equipDescTransfer.js upgradeManager.js
  warningTranslator.js simConfig.js vfdClient.js vfdRegisterMaps.js
)
for f in "${BRIDGE_FILES[@]}"; do
  cp "$SERVER_DIR/dist/$f" "$PKG_DIR/bridge-server/dist/"
  # Also copy source maps if they exist
  [ -f "$SERVER_DIR/dist/${f}.map" ] && cp "$SERVER_DIR/dist/${f}.map" "$PKG_DIR/bridge-server/dist/"
done
cp "$SERVER_DIR/package.json" "$PKG_DIR/bridge-server/"
[ -f "$SERVER_DIR/package-lock.json" ] && cp "$SERVER_DIR/package-lock.json" "$PKG_DIR/bridge-server/"

# SvelteKit UI — full build output
cp -r "$BUILD_DIR"/* "$PKG_DIR/ui-svelte/"
# Also include package.json for the SvelteKit runtime
cp "$SCRIPT_DIR/package.json" "$PKG_DIR/ui-svelte/"
[ -f "$SCRIPT_DIR/package-lock.json" ] && cp "$SCRIPT_DIR/package-lock.json" "$PKG_DIR/ui-svelte/"

# Systemd service files
cp "$SERVER_DIR/deploy/agristar-bridge.service" "$PKG_DIR/"

echo "  ✓ Bridge server dist/ (${#BRIDGE_FILES[@]} modules)"
echo "  ✓ SvelteKit UI build/"
echo "  ✓ systemd service file"

# Step 3: Create install script
echo ""
echo "[3/4] Generating install script..."

cat > "$PKG_DIR/install.sh" << 'INSTALL_SCRIPT'
#!/bin/bash
# ═══════════════════════════════════════════════════════════════════
# Agristar VFD Fan Control — Production Installer
#
# Installs the bridge server (replaces iotclient) and updated
# SvelteKit UI with VFD Modbus fan control support.
#
# Usage:
#   sudo bash install.sh [--vfd-host=IP] [--vfd-port=PORT] [--vfd-scan=N]
#
# Defaults:
#   --vfd-host=127.0.0.1  (Modbus TCP gateway IP)
#   --vfd-port=502        (Modbus TCP port)
#   --vfd-scan=8          (max drive unit ID to scan)
# ═══════════════════════════════════════════════════════════════════
set -e

# Parse args
VFD_HOST="127.0.0.1"
VFD_PORT="502"
VFD_SCAN="8"
for arg in "$@"; do
  case $arg in
    --vfd-host=*) VFD_HOST="${arg#*=}" ;;
    --vfd-port=*) VFD_PORT="${arg#*=}" ;;
    --vfd-scan=*) VFD_SCAN="${arg#*=}" ;;
  esac
done

GELLERT_HOME="/home/gellert/Gellert"
BRIDGE_DIR="$GELLERT_HOME/bridge-server"
UI_DIR="$GELLERT_HOME/ui-svelte"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "╔═══════════════════════════════════════════════════════╗"
echo "║  Agristar VFD Fan Control — Installing               ║"
echo "╠═══════════════════════════════════════════════════════╣"
echo "║  VFD Gateway : ${VFD_HOST}:${VFD_PORT}"
echo "║  Max Scan ID : ${VFD_SCAN}"
echo "╚═══════════════════════════════════════════════════════╝"
echo ""

# 1. Stop existing services
echo "[1/7] Stopping services..."
systemctl stop iotclient.service 2>/dev/null || true
systemctl stop agristar-bridge.service 2>/dev/null || true
systemctl stop uisvelte.service 2>/dev/null || true
echo "  ✓ Services stopped"

# 2. Install bridge server
echo "[2/7] Installing bridge server..."
mkdir -p "$BRIDGE_DIR/dist"
cp -f "$SCRIPT_DIR/bridge-server/dist/"*.js "$BRIDGE_DIR/dist/"
cp -f "$SCRIPT_DIR/bridge-server/package.json" "$BRIDGE_DIR/"
[ -f "$SCRIPT_DIR/bridge-server/package-lock.json" ] && cp -f "$SCRIPT_DIR/bridge-server/package-lock.json" "$BRIDGE_DIR/"
chown -R gellert:gellert "$BRIDGE_DIR"
echo "  ✓ Bridge server files installed"

# 3. Install Node.js dependencies
echo "[3/7] Installing bridge dependencies..."
cd "$BRIDGE_DIR"
su -c "npm ci --omit=dev 2>&1 | tail -3" gellert
echo "  ✓ Dependencies installed"

# 4. Install SvelteKit UI
echo "[4/7] Installing SvelteKit UI..."
# Back up current UI
[ -d "$UI_DIR" ] && mv "$UI_DIR" "${UI_DIR}.bak.$(date +%Y%m%d%H%M%S)" 2>/dev/null || true
mkdir -p "$UI_DIR"
cp -r "$SCRIPT_DIR/ui-svelte/"* "$UI_DIR/"
cd "$UI_DIR"
su -c "npm ci --omit=dev 2>&1 | tail -3" gellert
chown -R gellert:gellert "$UI_DIR"
echo "  ✓ SvelteKit UI installed"

# 5. Install systemd service for bridge
echo "[5/7] Installing bridge service..."
# Set VFD parameters in service file
sed -e "s|Environment=VFD_HOST=.*|Environment=VFD_HOST=${VFD_HOST}|" \
    -e "s|Environment=VFD_PORT=.*|Environment=VFD_PORT=${VFD_PORT}|" \
    -e "s|Environment=VFD_MAX_SCAN=.*|Environment=VFD_MAX_SCAN=${VFD_SCAN}|" \
    "$SCRIPT_DIR/agristar-bridge.service" > /etc/systemd/system/agristar-bridge.service
systemctl daemon-reload
echo "  ✓ Bridge service installed"

# 6. Update lighttpd proxy — route /iot to bridge:3001
echo "[6/7] Updating lighttpd proxy..."
LCONF="/etc/lighttpd/lighttpd.conf"
if [ -f "$LCONF" ]; then
  if grep -q '127.0.0.1:2035' "$LCONF"; then
    sed -i 's|127.0.0.1:2035|127.0.0.1:3001|g' "$LCONF"
    echo "  Patched: iotclient:2035 → bridge:3001"
  elif grep -q '127.0.0.1:3001' "$LCONF"; then
    echo "  Already pointing to bridge:3001"
  fi
  systemctl restart lighttpd
fi
# Disable iotclient (bridge replaces it)
systemctl disable iotclient.service 2>/dev/null || true
echo "  ✓ lighttpd updated"

# 7. Start services
echo "[7/7] Starting services..."
systemctl enable agristar-bridge
systemctl start agristar-bridge
systemctl enable uisvelte
systemctl start uisvelte
echo "  ✓ Services started"

echo ""
echo "╔═══════════════════════════════════════════════════════╗"
echo "║  Installation complete!                              ║"
echo "╠═══════════════════════════════════════════════════════╣"
echo "║                                                      ║"
echo "║  Bridge  : systemctl status agristar-bridge          ║"
echo "║  UI      : systemctl status uisvelte                 ║"
echo "║  Logs    : journalctl -u agristar-bridge -f          ║"
echo "║  VFD     : curl localhost:3001/iot/fans/status        ║"
echo "║                                                      ║"
echo "║  To roll back:                                       ║"
echo "║    systemctl stop agristar-bridge                    ║"
echo "║    systemctl enable iotclient && systemctl start it  ║"
echo "║    sed -i 's|:3001|:2035|' /etc/lighttpd/lighttpd.conf║"
echo "║    systemctl restart lighttpd                        ║"
echo "║                                                      ║"
echo "╚═══════════════════════════════════════════════════════╝"
INSTALL_SCRIPT

chmod +x "$PKG_DIR/install.sh"
echo "  ✓ install.sh generated"

# Step 4: Create tarball
echo ""
echo "[4/4] Creating package..."
cd /tmp
tar czf "$DEPLOY_DIR/$PKG_NAME" -C "$PKG_DIR" .
rm -rf "$PKG_DIR"

PKGSIZE=$(du -h "$DEPLOY_DIR/$PKG_NAME" | cut -f1)
echo "  ✓ $PKG_NAME ($PKGSIZE)"

echo ""
echo "╔═══════════════════════════════════════════════════════╗"
echo "║  Package ready: server/deploy/$PKG_NAME"
echo "╠═══════════════════════════════════════════════════════╣"
echo "║                                                      ║"
echo "║  To deploy to production:                            ║"
echo "║    scp server/deploy/$PKG_NAME gellert@<PI_IP>:/tmp/"
echo "║    ssh gellert@<PI_IP>"
echo "║    cd /tmp && tar xzf $PKG_NAME"
echo "║    sudo bash install.sh --vfd-host=<GATEWAY_IP>      ║"
echo "║                                                      ║"
echo "║  Or use the direct deploy script:                    ║"
echo "║    ./deploy/deploy.sh gellert@<PI_IP>                ║"
echo "║                                                      ║"
echo "╚═══════════════════════════════════════════════════════╝"
