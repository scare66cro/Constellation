#!/bin/bash
# WSL wrapper â€” stage files + run privileged install via sudo+sshpass.
PW='4gri*st4r'
HOST='gellert@10.47.27.108'
SSH_OPTS='-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o PubkeyAuthentication=no'

# Stage the clean conf
SSHPASS="$PW" sshpass -e ssh $SSH_OPTS "$HOST" 'cat > /tmp/lighttpd_clean.conf' < /mnt/f/Constellation/rpi5/rpi5_lighttpd_clean.conf

# Stage the install script
SSHPASS="$PW" sshpass -e ssh $SSH_OPTS "$HOST" 'cat > /tmp/install.sh' < /mnt/f/Constellation/rpi5/rpi5_install_clean_lighttpd.sh

# Stage password file with restrictive perms; run install via sudo -S reading from it
SSHPASS="$PW" sshpass -e ssh $SSH_OPTS "$HOST" "umask 077; printf '%s\n' '$PW' > /tmp/.pw && sudo -S -p '' bash /tmp/install.sh < /tmp/.pw 2>&1; rm -f /tmp/.pw /tmp/install.sh /tmp/lighttpd_clean.conf"
