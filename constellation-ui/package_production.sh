#!/bin/bash
###############################################################################
# Package the Constellation bridge server + SvelteKit UI for production.
#
# Produces:  server/deploy/constellation-bridge-<timestamp>.tar.gz
#
# Layout inside the tarball (matches what install.sh expects):
#   bridge-server/
#     dist/                                  (nested tree as emitted by tsc)
#       constellation-ui/server/src/*.js     (transport + bridge logic)
#       generated/ts/agristar/*.js           (proto codecs)
#     package.json
#     package-lock.json
#   ui-svelte/
#     index.js handler.js env.js shims.js client/ server/
#     package.json package-lock.json
#   agristar-bridge.service                  (systemd unit)
#   install.sh
#
# Sim-only modules are EXCLUDED from the bridge package (orbit physics,
# rs485 panel, vfd simulator, etc.). Production talks to real hardware.
#
# To install on a Pi:
#   scp constellation-bridge-*.tar.gz gellert@<PI_IP>:/tmp/
#   ssh gellert@<PI_IP>
#   cd /tmp && tar xzf constellation-bridge-*.tar.gz && sudo bash install.sh
###############################################################################
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SERVER_DIR="$SCRIPT_DIR/server"
BUILD_DIR="$SCRIPT_DIR/build"
DEPLOY_DIR="$SERVER_DIR/deploy"
PKG_DIR="$(mktemp -d -t constellation-pkg-XXXXXX)"
TS="$(date +%Y%m%d-%H%M%S)"
PKG_NAME="constellation-bridge-${TS}.tar.gz"

# Files in server/dist/constellation-ui/server/src/ that are SIMULATOR-ONLY
# and must NOT ship to production. Update this list when adding new sim
# modules; do NOT keep a positive whitelist (it bit-rots — see
# /memories/repo/rpi5-deployment.md).
SIM_BLOCKLIST=(
  orbitSimulator.js   # storage temp/humid/CO2 physics model
  rs485Panel.js       # SCADA validation panel that injects fake analog data
  rs485Responder.js   # RS-485 stub responder
  vfdSimulator.js     # in-process VFD drive emulator
)

mkdir -p "$DEPLOY_DIR"

echo "╔═══════════════════════════════════════════════════════╗"
echo "║  Constellation — Production Packager                 ║"
echo "╚═══════════════════════════════════════════════════════╝"
echo "  Tarball : $DEPLOY_DIR/$PKG_NAME"
echo "  Staging : $PKG_DIR"
echo ""

# ─── 1. Build outputs ─────────────────────────────────────────────────────
echo "[1/4] Verifying / building outputs..."

if [ ! -f "$SERVER_DIR/dist/constellation-ui/server/src/index.js" ]; then
  echo "  Bridge server not built. Running tsc..."
  (cd "$SERVER_DIR" && npx tsc)
fi
echo "  ✓ Bridge dist tree"

if [ ! -f "$BUILD_DIR/index.js" ]; then
  echo "  ERROR: SvelteKit UI not built. Run: npm run build" >&2
  exit 1
fi
echo "  ✓ SvelteKit build/"

# ─── 2. Stage bridge-server ───────────────────────────────────────────────
echo ""
echo "[2/4] Staging bridge-server (excluding ${#SIM_BLOCKLIST[@]} sim modules)..."

mkdir -p "$PKG_DIR/bridge-server"
# Build rsync exclude args from SIM_BLOCKLIST.
EXCLUDES=()
for f in "${SIM_BLOCKLIST[@]}"; do
  EXCLUDES+=(--exclude="$f" --exclude="${f}.map")
done
# Copy the entire dist/ tree, preserving structure (relative imports
# between server/src/*.js and generated/ts/agristar/*.js depend on it).
rsync -a "${EXCLUDES[@]}" "$SERVER_DIR/dist/" "$PKG_DIR/bridge-server/dist/"

cp "$SERVER_DIR/package.json" "$PKG_DIR/bridge-server/"
[ -f "$SERVER_DIR/package-lock.json" ] && cp "$SERVER_DIR/package-lock.json" "$PKG_DIR/bridge-server/"

INCLUDED=$(find "$PKG_DIR/bridge-server/dist" -name '*.js' | wc -l)
echo "  ✓ $INCLUDED .js files staged"

# ─── 3. Stage SvelteKit UI ────────────────────────────────────────────────
mkdir -p "$PKG_DIR/ui-svelte"
cp -r "$BUILD_DIR"/* "$PKG_DIR/ui-svelte/"
cp "$SCRIPT_DIR/package.json" "$PKG_DIR/ui-svelte/"
[ -f "$SCRIPT_DIR/package-lock.json" ] && cp "$SCRIPT_DIR/package-lock.json" "$PKG_DIR/ui-svelte/"
echo "  ✓ ui-svelte/ staged"

# ─── 4. Systemd unit + install script ─────────────────────────────────────
# Write a fresh service file (the tracked one in server/deploy/ has the
# legacy flat-dist ExecStart=node dist/index.js path).
cat > "$PKG_DIR/agristar-bridge.service" <<'UNIT'
[Unit]
Description=Constellation Bridge (proto/COBS over UART + Modbus VFD + REST/WS)
After=network.target
After=dev-ttyAMA0.device

[Service]
Type=simple
User=gellert
Group=dialout
WorkingDirectory=/home/gellert/Gellert/bridge-server
ExecStart=/usr/bin/node dist/constellation-ui/server/src/index.js
Restart=always
RestartSec=5

# Bridge HTTP / WebSocket port
Environment=PORT=3001

# Real hardware UART to Nova AM2434
Environment=SERIAL_PORT=/dev/ttyAMA0
Environment=SERIAL_BAUD=230400

# VFD Modbus TCP gateway; set VFD_HOST= (empty) to disable VFD scanning.
Environment=VFD_HOST=127.0.0.1
Environment=VFD_PORT=502
Environment=VFD_MAX_SCAN=8

StandardOutput=journal
StandardError=journal
SyslogIdentifier=agristar-bridge

[Install]
WantedBy=multi-user.target
UNIT

cat > "$PKG_DIR/install.sh" <<'INSTALL_SCRIPT'
#!/bin/bash
###############################################################################
# Constellation production installer (run as root: sudo bash install.sh)
#
# Defaults can be overridden with --vfd-host, --vfd-port, --vfd-scan.
###############################################################################
set -euo pipefail

VFD_HOST="127.0.0.1"; VFD_PORT="502"; VFD_SCAN="8"
for arg in "$@"; do
  case $arg in
    --vfd-host=*) VFD_HOST="${arg#*=}" ;;
    --vfd-port=*) VFD_PORT="${arg#*=}" ;;
    --vfd-scan=*) VFD_SCAN="${arg#*=}" ;;
  esac
done

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
GELLERT_HOME="/home/gellert/Gellert"
BRIDGE_DIR="$GELLERT_HOME/bridge-server"
UI_DIR="$GELLERT_HOME/ui-svelte"

echo "╔════════════════════════════════════════════════════════╗"
echo "║  Installing Constellation                              ║"
echo "║  VFD: ${VFD_HOST}:${VFD_PORT}  scan=${VFD_SCAN}"
echo "╚════════════════════════════════════════════════════════╝"

# 1. Stop services
echo "[1/6] Stopping services"
systemctl stop iotclient.service     2>/dev/null || true
systemctl stop agristar-bridge.service 2>/dev/null || true
systemctl stop uisvelte.service      2>/dev/null || true

# 2. Bridge server
echo "[2/6] Installing bridge-server → $BRIDGE_DIR"
mkdir -p "$BRIDGE_DIR"
# Wipe the old dist tree so renamed/removed files don't linger.
rm -rf "$BRIDGE_DIR/dist"
cp -r "$SCRIPT_DIR/bridge-server/dist" "$BRIDGE_DIR/"
cp -f "$SCRIPT_DIR/bridge-server/package.json" "$BRIDGE_DIR/"
[ -f "$SCRIPT_DIR/bridge-server/package-lock.json" ] && \
  cp -f "$SCRIPT_DIR/bridge-server/package-lock.json" "$BRIDGE_DIR/"
chown -R gellert:gellert "$BRIDGE_DIR"

# 3. Bridge deps
echo "[3/6] Installing bridge dependencies"
( cd "$BRIDGE_DIR" && su -c "npm ci --omit=dev --no-audit --no-fund 2>&1 | tail -5" gellert )

# 4. SvelteKit UI
echo "[4/6] Installing ui-svelte → $UI_DIR"
[ -d "$UI_DIR" ] && mv "$UI_DIR" "${UI_DIR}.bak.$(date +%Y%m%d%H%M%S)"
mkdir -p "$UI_DIR"
cp -r "$SCRIPT_DIR/ui-svelte/"* "$UI_DIR/"
( cd "$UI_DIR" && su -c "npm ci --omit=dev --no-audit --no-fund 2>&1 | tail -5" gellert )
chown -R gellert:gellert "$UI_DIR"

# 5. Systemd unit
echo "[5/6] Installing systemd unit"
sed -e "s|^Environment=VFD_HOST=.*|Environment=VFD_HOST=${VFD_HOST}|" \
    -e "s|^Environment=VFD_PORT=.*|Environment=VFD_PORT=${VFD_PORT}|" \
    -e "s|^Environment=VFD_MAX_SCAN=.*|Environment=VFD_MAX_SCAN=${VFD_SCAN}|" \
    "$SCRIPT_DIR/agristar-bridge.service" > /etc/systemd/system/agristar-bridge.service
systemctl daemon-reload
systemctl disable iotclient.service 2>/dev/null || true

# Patch lighttpd if present (legacy iotclient port → bridge port)
LCONF="/etc/lighttpd/lighttpd.conf"
if [ -f "$LCONF" ] && grep -q '127.0.0.1:2035' "$LCONF"; then
  sed -i 's|127.0.0.1:2035|127.0.0.1:3001|g' "$LCONF"
  systemctl restart lighttpd 2>/dev/null || true
fi

# 6. Start
echo "[6/6] Starting services"
systemctl enable agristar-bridge uisvelte
systemctl start  agristar-bridge uisvelte
sleep 2
systemctl --no-pager status agristar-bridge | head -8
systemctl --no-pager status uisvelte        | head -8

echo ""
echo "Done. Logs:"
echo "  journalctl -u agristar-bridge -f"
echo "  journalctl -u uisvelte        -f"
INSTALL_SCRIPT
chmod +x "$PKG_DIR/install.sh"
echo "  ✓ install.sh + agristar-bridge.service"

# ─── 5. Tarball ───────────────────────────────────────────────────────────
echo ""
echo "[3/4] Verifying staged tree..."
( cd "$PKG_DIR" && find . -maxdepth 3 -type d | sort )

echo ""
echo "[4/4] Creating tarball..."
tar czf "$DEPLOY_DIR/$PKG_NAME" -C "$PKG_DIR" .
rm -rf "$PKG_DIR"
PKGSIZE=$(du -h "$DEPLOY_DIR/$PKG_NAME" | cut -f1)
echo "  ✓ $DEPLOY_DIR/$PKG_NAME ($PKGSIZE)"

echo ""
echo "╔════════════════════════════════════════════════════════╗"
echo "║  Package ready                                         ║"
echo "╠════════════════════════════════════════════════════════╣"
echo "║  scp $PKG_NAME gellert@<PI>:/tmp/"
echo "║  ssh gellert@<PI>                                      ║"
echo "║  cd /tmp && tar xzf $PKG_NAME"
echo "║  sudo bash install.sh --vfd-host=<GATEWAY_IP>          ║"
echo "╚════════════════════════════════════════════════════════╝"
