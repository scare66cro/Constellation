#!/bin/bash
# Local wrapper that runs from WSL â€” handles password without PowerShell mangling
PW='4gri*st4r'
HOST='gellert@10.47.27.108'
SCRIPT='/mnt/f/Constellation/rpi5/rpi5_kill_legacy_fastcgi.sh'

# stage script
SSHPASS="$PW" sshpass -e ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o PubkeyAuthentication=no "$HOST" 'cat > /tmp/fix.sh' < "$SCRIPT"

# stage password file with restrictive perms
SSHPASS="$PW" sshpass -e ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o PubkeyAuthentication=no "$HOST" "umask 077; printf '%s' '$PW' > /tmp/.pw"

# run via sudo -S, password fed from staged file
SSHPASS="$PW" sshpass -e ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o PubkeyAuthentication=no "$HOST" 'sudo -S -p "" bash /tmp/fix.sh < /tmp/.pw 2>&1; rm -f /tmp/.pw'
