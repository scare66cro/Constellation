#!/bin/bash
# Production bridge deploy to rpi5 @ 10.47.27.108.
# - Snapshots existing bridge src + package files (rollback insurance)
# - Rsyncs server/src/, package.json, package-lock.json
# - npm install --omit=dev
# - reset-failed + restart
# - validates /health + journal lines containing "firmware" / "[API]"
set -euo pipefail

REPO=/mnt/f/Constellation
SSH_OPTS=(-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR)
RPI=gellert@10.47.27.108
export SSHPASS='4gri*st4r'

step() { echo; echo "=== $1 ==="; }

step "1. Snapshot live bridge tree on Pi5"
TS=$(date +%Y%m%d-%H%M%S)
sshpass -e ssh "${SSH_OPTS[@]}" "$RPI" "set -e
BACKUP=/home/gellert/Gellert/constellation-backup-${TS}
mkdir -p \$BACKUP
cp -a /home/gellert/Gellert/constellation/constellation-ui/server/src \$BACKUP/src
cp /home/gellert/Gellert/constellation/constellation-ui/server/package.json \$BACKUP/package.json
cp /home/gellert/Gellert/constellation/constellation-ui/server/package-lock.json \$BACKUP/package-lock.json 2>/dev/null || true
echo BACKUP_AT=\$BACKUP
ls \$BACKUP | head -20"

step "2. Rsync bridge src + package files"
sshpass -e rsync -az --delete \
  -e "ssh ${SSH_OPTS[*]}" \
  --exclude=node_modules --exclude=dist --exclude=deploy --exclude=data --exclude='*.bak' \
  "$REPO/constellation-ui/server/src/" \
  "$RPI:/home/gellert/Gellert/constellation/constellation-ui/server/src/"

sshpass -e scp "${SSH_OPTS[@]}" \
  "$REPO/constellation-ui/server/package.json" \
  "$REPO/constellation-ui/server/package-lock.json" \
  "$RPI:/home/gellert/Gellert/constellation/constellation-ui/server/"

step "3. npm install --omit=dev on Pi5"
sshpass -e ssh "${SSH_OPTS[@]}" "$RPI" \
  "cd /home/gellert/Gellert/constellation/constellation-ui/server && npm install --omit=dev --no-audit --no-fund 2>&1 | tail -15"

step "4. Reset-failed + restart agristar-bridge"
sshpass -e ssh "${SSH_OPTS[@]}" "$RPI" \
  "sudo -n systemctl reset-failed agristar-bridge && sudo -n systemctl restart agristar-bridge"

step "5. Wait 8s for bridge to come up"
sleep 8

step "6. systemctl is-active + status (first 12 lines)"
sshpass -e ssh "${SSH_OPTS[@]}" "$RPI" \
  "systemctl is-active agristar-bridge; sudo -n systemctl status agristar-bridge --no-pager | head -14"

step "7. /health"
sshpass -e ssh "${SSH_OPTS[@]}" "$RPI" \
  "curl -s --max-time 5 http://localhost:9001/health | head -c 800"

step "8. journal last 60 lines, grep for [API]/firmware/error"
sshpass -e ssh "${SSH_OPTS[@]}" "$RPI" \
  "sudo -n journalctl -u agristar-bridge -n 60 --no-pager | grep -iE 'firmware|\[API\]|error|warn|listen' | head -40"

echo
echo "=== bridge deploy DONE ==="
