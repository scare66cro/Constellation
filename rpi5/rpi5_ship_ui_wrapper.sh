#!/bin/bash
PW='4gri*st4r'
HOST='gellert@10.47.27.108'
SSH_OPTS='-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o PubkeyAuthentication=no'

echo "=== sync build to Pi5 /tmp/ui-build ==="
SSHPASS="$PW" sshpass -e ssh $SSH_OPTS "$HOST" 'rm -rf /tmp/ui-build && mkdir -p /tmp/ui-build'
SSHPASS="$PW" sshpass -e rsync -az --delete -e "ssh $SSH_OPTS" \
  /mnt/f/Constellation/constellation-ui/build/ "$HOST:/tmp/ui-build/"

echo "=== copy deploy script ==="
SSHPASS="$PW" sshpass -e scp $SSH_OPTS \
  /mnt/f/Constellation/rpi5/rpi5_deploy_ui.sh "$HOST:/tmp/_deploy.sh"

echo "=== run deploy via sudo ==="
SSHPASS="$PW" sshpass -e ssh $SSH_OPTS "$HOST" \
  "umask 077; printf '%s\n' '$PW' > /tmp/.pw; sudo -S -p '' bash /tmp/_deploy.sh < /tmp/.pw 2>&1; rm -f /tmp/.pw /tmp/_deploy.sh"
