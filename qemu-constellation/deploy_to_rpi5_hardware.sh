#!/bin/bash
###############################################################################
# Deploy Constellation bridge (TS source) + UI build to the REAL RPi5 hardware.
#
# Target: gellert@10.47.27.108 (RPi5 Model B Rev 1.0, Debian 12 bookworm)
# Serial: /dev/ttyAMA0 (RP1 PL011 UART on GPIO14/15, 921600 baud)
#
# Layout on the rpi5 (matches QEMU deploy):
#   /home/gellert/Gellert/constellation/
#     constellation-ui/server/   <-- bridge TS source (run via tsx)
#     constellation-ui/server/node_modules/
#     generated/ts/agristar/...  <-- proto codecs (relative path matches repo)
#     generated/node_modules/
#   /home/gellert/Gellert/ui-svelte/  <-- (existing) SvelteKit build
#
# Differences from QEMU deploy (deploy_source_to_rpi5.sh):
#   - SSH to 10.47.27.108:22 (not localhost:2222)
#   - SERIAL_PORT=/dev/ttyAMA0 (not tcp://10.0.2.2:9000)
#   - Stops legacy iotclient service (not present in QEMU image)
###############################################################################
set -euo pipefail

REPO=/mnt/f/Constellation
RPI_HOST="${RPI5_HOST:-10.47.27.108}"
SSH_OPTS="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR"
RPI="gellert@${RPI_HOST}"
PASS='4gri*st4r'

step() { echo; echo "â”€â”€ $1 â”€â”€"; }

step "1. Check SvelteKit UI build"
if [ ! -f "$REPO/constellation-ui/build/index.js" ]; then
  echo "  build/ missing â€” run 'npm run build' from constellation-ui/ first"
  exit 1
fi
echo "  âœ“ build/ exists"

step "2. Stop services on rpi5"
SSHPASS="$PASS" sshpass -e ssh $SSH_OPTS $RPI \
  "sudo -n systemctl stop agristar-bridge 2>/dev/null || true; \
   sudo -n systemctl stop iotclient 2>/dev/null || true; \
   sudo -n systemctl stop uisvelte 2>/dev/null || true; \
   sudo -n mkdir -p /home/gellert/Gellert/constellation/constellation-ui/server \
                    /home/gellert/Gellert/constellation/generated; \
   sudo -n chown -R gellert:gellert /home/gellert/Gellert/constellation; \
   echo '  âœ“ services stopped, dirs ready'"

step "3. Rsync bridge source (server/src, server/package.json)"
SSHPASS="$PASS" sshpass -e rsync -az --delete \
  -e "ssh $SSH_OPTS" \
  --exclude=node_modules --exclude=dist --exclude=deploy --exclude=data \
  "$REPO/constellation-ui/server/" \
  "$RPI:/home/gellert/Gellert/constellation/constellation-ui/server/"
echo "  âœ“ bridge source synced"

step "4. Rsync generated/ (proto TS bindings + package.json)"
SSHPASS="$PASS" sshpass -e rsync -az --delete \
  -e "ssh $SSH_OPTS" \
  --exclude=node_modules \
  "$REPO/generated/" \
  "$RPI:/home/gellert/Gellert/constellation/generated/"
echo "  âœ“ generated/ synced"

step "5. Rsync SvelteKit build â†’ ui-svelte (preserve node_modules)"
SSHPASS="$PASS" sshpass -e rsync -az --delete \
  -e "ssh $SSH_OPTS" \
  --rsync-path="sudo -n rsync" \
  --exclude=node_modules --exclude=package.json --exclude=package-lock.json \
  --chown=gellert:gellert \
  "$REPO/constellation-ui/build/" \
  "$RPI:/home/gellert/Gellert/ui-svelte/"
echo "  âœ“ UI build synced"

step "6. npm install in bridge + generated"
SSHPASS="$PASS" sshpass -e ssh $SSH_OPTS $RPI bash <<'REMOTE'
set -e
BRIDGE=/home/gellert/Gellert/constellation/constellation-ui/server
GEN=/home/gellert/Gellert/constellation/generated

cd "$GEN"
if [ ! -d node_modules ] || [ package.json -nt node_modules/.package-lock.json 2>/dev/null ]; then
  echo "  [npm] generated/"
  npm install --no-audit --no-fund 2>&1 | tail -3
fi

cd "$BRIDGE"
if [ ! -d node_modules ] || [ package.json -nt node_modules/.package-lock.json 2>/dev/null ]; then
  echo "  [npm] bridge (rebuilds better-sqlite3 native; may take a minute on ARM)"
  npm install --no-audit --no-fund 2>&1 | tail -5
fi
echo "  âœ“ dependencies ready"
REMOTE

step "7. Install agristar-bridge.service (real hardware â€” /dev/ttyAMA0)"
SSHPASS="$PASS" sshpass -e ssh $SSH_OPTS $RPI sudo -n tee /etc/systemd/system/agristar-bridge.service >/dev/null <<'UNIT'
[Unit]
Description=Constellation Bridge (proto/COBS â†’ Nova, REST/WS â†’ UI)
After=network-online.target postgresql.service
Wants=network-online.target
StartLimitIntervalSec=300
StartLimitBurst=10

[Service]
Type=simple
User=gellert
Group=dialout
WorkingDirectory=/home/gellert/Gellert/constellation/constellation-ui/server
ExecStart=/usr/bin/npx tsx src/index.ts
Restart=always
RestartSec=5
TimeoutStopSec=15
KillSignal=SIGTERM
MemoryMax=512M
LogRateLimitIntervalSec=0
LogRateLimitBurst=0

# Hardening
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=full
ProtectHome=read-only
ProtectKernelTunables=true
ProtectKernelModules=true
ProtectControlGroups=true
RestrictSUIDSGID=true
RestrictRealtime=true
LockPersonality=true
SystemCallArchitectures=native
# Whitelist directories the bridge legitimately writes to (otherwise
# ProtectHome=read-only and ProtectSystem=full block log spool +
# .sim-config writes).
ReadWritePaths=/home/gellert/.constellation /home/gellert/Gellert/constellation/constellation-ui/server/.sim-config /home/gellert/Gellert/upgrade /var/run/postgresql

# Real hardware: RP1 PL011 UART (GPIO14/15) at 921600 baud
Environment=SERIAL_PORT=/dev/ttyAMA0
Environment=PORT=9001
Environment=VFD_HOST=
Environment=NODE_ENV=production
Environment=PG_DSN=postgres:///paneldb?host=/var/run/postgresql
Environment=UPGRADE_DIR=/home/gellert/Gellert/upgrade
Environment=UPLOAD_DIR=/home/gellert/Gellert/upgrade/uploads

StandardOutput=journal
StandardError=journal
SyslogIdentifier=agristar-bridge

[Install]
WantedBy=multi-user.target
UNIT
echo "  âœ“ service unit installed"

step "7b. Install pg backup script + timer"
SSHPASS="$PASS" sshpass -e scp $SSH_OPTS \
  "$REPO/qemu-constellation/agristar-pg-backup.sh" \
  "$REPO/qemu-constellation/agristar-pg-backup.service" \
  "$REPO/qemu-constellation/agristar-pg-backup.timer" \
  "$RPI:/tmp/"
SSHPASS="$PASS" sshpass -e ssh $SSH_OPTS $RPI bash <<'BACKUP'
sudo -n install -m 0755 -o root -g root /tmp/agristar-pg-backup.sh /usr/local/sbin/agristar-pg-backup.sh
sudo -n install -m 0644 -o root -g root /tmp/agristar-pg-backup.service /etc/systemd/system/agristar-pg-backup.service
sudo -n install -m 0644 -o root -g root /tmp/agristar-pg-backup.timer /etc/systemd/system/agristar-pg-backup.timer
rm -f /tmp/agristar-pg-backup.{sh,service,timer}
echo "  âœ“ pg backup artifacts installed"
BACKUP

step "8. Disable legacy iotclient, enable + start bridge"
SSHPASS="$PASS" sshpass -e ssh $SSH_OPTS $RPI bash <<'REMOTE'
sudo -n systemctl disable iotclient 2>&1 | grep -v "Removed" || true
sudo -n systemctl daemon-reload
sudo -n systemctl enable agristar-bridge uisvelte 2>&1 | tail -2
sudo -n systemctl enable --now agristar-pg-backup.timer 2>&1 | tail -2
sudo -n systemctl restart agristar-bridge uisvelte
sleep 4
echo
echo "=== agristar-bridge ==="
sudo -n systemctl status agristar-bridge --no-pager 2>&1 | head -12
echo
echo "=== uisvelte ==="
sudo -n systemctl status uisvelte --no-pager 2>&1 | head -8
echo
echo "=== pg backup timer ==="
sudo -n systemctl list-timers agristar-pg-backup.timer --no-pager 2>&1 | head -5
echo
echo "=== serial port ==="
ls -la /dev/ttyAMA0
echo
echo "=== bridge health ==="
curl -s http://localhost:9001/health 2>&1 || echo "(bridge may still be starting)"
REMOTE

echo
echo "â”€â”€ DONE â”€â”€"
echo "  UI:     http://${RPI_HOST}:3000/"
echo "  Bridge: http://${RPI_HOST}:9001/health"
echo "  Logs:   ssh gellert@${RPI_HOST} 'sudo journalctl -u agristar-bridge -f'"
