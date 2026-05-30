#!/bin/bash
###############################################################################
# Deploy Constellation bridge (TS source) + UI build to the rpi5 QEMU image.
#
# Layout on the rpi5:
#   /home/gellert/Gellert/constellation/
#     constellation-ui/server/   <-- bridge TS source (run via tsx)
#     constellation-ui/server/node_modules/
#     generated/ts/agristar/...  <-- proto codecs (relative path matches repo)
#     generated/node_modules/
#   /home/gellert/Gellert/ui-svelte/  <-- (existing) SvelteKit build
#
# systemd unit: agristar-bridge.service runs `npx tsx src/index.ts` from
# constellation-ui/server/, with SERIAL_PORT=tcp://10.0.2.2:9000 (QEMU sim
# default; on real hardware override to /dev/ttyAMA0 via drop-in).
#
# Why source + tsx instead of dist + node:
#   - matches host dev exactly; no dual code path
#   - sidesteps ts-proto's extensionless `import './common'` bug under
#     plain Node ESM
#   - tsx is a fine production runtime (esbuild under the hood)
###############################################################################
set -euo pipefail

REPO=/mnt/f/Constellation
SSH_OPTS="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -p 2222"
RPI=gellert@localhost
PASS='4gri*st4r'

step() { echo; echo "── $1 ──"; }

step "1. Build SvelteKit UI (skipped — assumes constellation-ui/build/ is current)"
if [ ! -f "$REPO/constellation-ui/build/index.js" ]; then
  echo "  build/ missing — run 'npm run build' from constellation-ui/ first"
  exit 1
fi
echo "  ✓ build/ exists"

step "2. Stop services on rpi5"
SSHPASS="$PASS" sshpass -e ssh $SSH_OPTS $RPI \
  "sudo -n systemctl stop agristar-bridge uisvelte 2>/dev/null || true; \
   sudo -n mkdir -p /home/gellert/Gellert/constellation/constellation-ui/server \
                    /home/gellert/Gellert/constellation/generated; \
   sudo -n chown -R gellert:gellert /home/gellert/Gellert/constellation"

step "3. Rsync bridge source (server/src, server/package.json)"
SSHPASS="$PASS" sshpass -e rsync -az --delete \
  -e "ssh $SSH_OPTS" \
  --exclude=node_modules --exclude=dist --exclude=deploy --exclude=data \
  "$REPO/constellation-ui/server/" \
  "$RPI:/home/gellert/Gellert/constellation/constellation-ui/server/"

step "4. Rsync generated/ (proto TS bindings + package.json)"
SSHPASS="$PASS" sshpass -e rsync -az --delete \
  -e "ssh $SSH_OPTS" \
  --exclude=node_modules \
  "$REPO/generated/" \
  "$RPI:/home/gellert/Gellert/constellation/generated/"

step "5. Rsync SvelteKit build → ui-svelte (preserve node_modules)"
SSHPASS="$PASS" sshpass -e rsync -az --delete \
  -e "ssh $SSH_OPTS" \
  --rsync-path="sudo -n rsync" \
  --exclude=node_modules --exclude=package.json --exclude=package-lock.json \
  --chown=gellert:gellert \
  "$REPO/constellation-ui/build/" \
  "$RPI:/home/gellert/Gellert/ui-svelte/"

step "6. npm ci in bridge + generated (only if package.json changed)"
SSHPASS="$PASS" sshpass -e ssh $SSH_OPTS $RPI bash <<'REMOTE'
set -e
BRIDGE=/home/gellert/Gellert/constellation/constellation-ui/server
GEN=/home/gellert/Gellert/constellation/generated

cd "$GEN"
if [ ! -d node_modules ] || [ package.json -nt node_modules/.package-lock.json 2>/dev/null ]; then
  echo "[npm] generated/"
  npm install --no-audit --no-fund 2>&1 | tail -3
fi

cd "$BRIDGE"
if [ ! -d node_modules ] || [ package.json -nt node_modules/.package-lock.json 2>/dev/null ]; then
  echo "[npm] bridge (this rebuilds better-sqlite3 native; can take a minute)"
  npm install --no-audit --no-fund 2>&1 | tail -5
fi
REMOTE

step "7. Install systemd unit (tsx-based, points at host Nova)"
SSHPASS="$PASS" sshpass -e ssh $SSH_OPTS $RPI sudo -n tee /etc/systemd/system/agristar-bridge.service >/dev/null <<'UNIT'
[Unit]
Description=Constellation Bridge (proto/COBS → Nova, REST/WS → UI)
After=network-online.target postgresql.service
Wants=network-online.target
# Production policy: don't enter a restart loop forever if startup is
# hopelessly broken. 10 restarts in 5 minutes ⇒ stop and surface the
# failure (`systemctl status`, alerting).
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
# Give the bridge a fair shutdown window — must beat the 8 s deadline
# inside index.ts shutdown() but stay under systemd defaults.
TimeoutStopSec=15
KillSignal=SIGTERM
# OS-level soft cap. Bridge holds pg pool (8) + WS clients + buffers;
# 512 MiB is ~10× steady-state. Crashing here is preferable to OOM-killing
# the whole rpi5.
MemoryMax=512M
# Don't let journald rate-limit production logs.
LogRateLimitIntervalSec=0
LogRateLimitBurst=0

# Hardening — Constellation runs untrusted UI code via WS so the
# blast-radius of a bridge compromise must be small. Each option below
# matches a real failure mode, not just paranoia.
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=full          # /usr, /boot, /etc read-only; /var stays writable for backups.
ProtectHome=read-only       # bridge needs to read /home/gellert/Gellert; never writes there.
ProtectKernelTunables=true
ProtectKernelModules=true
ProtectControlGroups=true
RestrictSUIDSGID=true
RestrictRealtime=true
LockPersonality=true
SystemCallArchitectures=native

# QEMU sim: bridge dials host gateway → host Nova UART1 on tcp://localhost:9000
# Real hardware: drop-in override to /dev/ttyAMA0 (see /etc/systemd/system/agristar-bridge.service.d/)
Environment=SERIAL_PORT=tcp://10.0.2.2:9000
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

step "7b. Install pg backup script + timer"
SSHPASS="$PASS" sshpass -e scp $SSH_OPTS \
  qemu-constellation/agristar-pg-backup.sh \
  qemu-constellation/agristar-pg-backup.service \
  qemu-constellation/agristar-pg-backup.timer \
  $RPI:/tmp/
SSHPASS="$PASS" sshpass -e ssh $SSH_OPTS $RPI bash <<'BACKUP'
sudo -n install -m 0755 -o root -g root /tmp/agristar-pg-backup.sh /usr/local/sbin/agristar-pg-backup.sh
sudo -n install -m 0644 -o root -g root /tmp/agristar-pg-backup.service /etc/systemd/system/agristar-pg-backup.service
sudo -n install -m 0644 -o root -g root /tmp/agristar-pg-backup.timer /etc/systemd/system/agristar-pg-backup.timer
rm -f /tmp/agristar-pg-backup.{sh,service,timer}
BACKUP

step "8. Reload + start"
SSHPASS="$PASS" sshpass -e ssh $SSH_OPTS $RPI bash <<'REMOTE'
sudo -n systemctl daemon-reload
sudo -n systemctl enable agristar-bridge uisvelte 2>&1 | tail -2
sudo -n systemctl enable --now agristar-pg-backup.timer 2>&1 | tail -2
sudo -n systemctl restart agristar-bridge uisvelte
sleep 3
echo
echo "=== agristar-bridge status ==="
sudo -n systemctl status agristar-bridge --no-pager | head -10
echo
echo "=== uisvelte status ==="
sudo -n systemctl status uisvelte --no-pager | head -10
echo
echo "=== agristar-pg-backup.timer ==="
sudo -n systemctl list-timers agristar-pg-backup.timer --no-pager | head -5
REMOTE

echo
echo "── DONE ──"
echo "  UI:     http://localhost:8181/"
echo "  Bridge: forwarded as :9001 only if you add hostfwd; until then check via SSH:"
echo "    ssh -p2222 gellert@localhost 'curl -s http://localhost:9001/health'"
