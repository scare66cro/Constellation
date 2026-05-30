#!/bin/bash
# SSH into the project-local rpi5 image and run inventory queries.
# Used for one-shot probes; fully scriptable.
set -u
HOST="${RPI5_SSH_HOST:-localhost}"
PORT="${RPI5_SSH_PORT:-2222}"
USER="${RPI5_SSH_USER:-gellert}"
PASS="${RPI5_SSH_PASS:-4gri*st4r}"

if [ $# -lt 1 ]; then
  echo "usage: $0 <remote command...>" >&2
  exit 2
fi

exec sshpass -p "$PASS" ssh \
  -o StrictHostKeyChecking=no \
  -o UserKnownHostsFile=/dev/null \
  -o LogLevel=ERROR \
  -o ConnectTimeout=10 \
  -p "$PORT" "$USER@$HOST" "$@"
