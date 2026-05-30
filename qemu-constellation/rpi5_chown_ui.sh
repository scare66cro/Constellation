#!/bin/bash
PW='4gri*st4r'
HOST='gellert@10.47.27.108'
SSH_OPTS='-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o PubkeyAuthentication=no'

cat > /tmp/_chown.sh << 'REMOTE'
sudo -S -p '' chown -R gellert:gellert /home/gellert/Gellert/ui-svelte < /tmp/.pw
sudo -S -p '' systemctl restart uisvelte < /tmp/.pw
sleep 3
echo "=== uisvelte ==="
systemctl is-active uisvelte
echo "=== HTTP / ==="
curl -sI http://localhost:3000/ | head -3
echo "=== HTTP /level2/orbit-sensors ==="
curl -sI http://localhost:3000/level2/orbit-sensors | head -3
echo "=== via lighttpd / ==="
curl -sI http://localhost/ | head -3
REMOTE

SSHPASS="$PW" sshpass -e scp $SSH_OPTS /tmp/_chown.sh "$HOST:/tmp/_chown.sh"
SSHPASS="$PW" sshpass -e ssh $SSH_OPTS "$HOST" "umask 077; printf '%s\n' '$PW' > /tmp/.pw; bash /tmp/_chown.sh; rm -f /tmp/.pw /tmp/_chown.sh"
