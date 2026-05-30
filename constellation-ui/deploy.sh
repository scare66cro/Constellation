#!/bin/bash
###############################################################################
# Deploy locally-built SvelteKit UI to a Constellation Pi (real hardware OR
# the project-local rpi5 QEMU image).
#
# Usage:
#   ./deploy.sh                              # default: rpi5 QEMU @ localhost:2222
#   ./deploy.sh --target=qemu                # alias for the default
#   ./deploy.sh --target=production          # ssh gellert@10.47.27.108 (rpi5 hw)
#   ./deploy.sh --host=10.1.2.99 --port=22   # custom target
#   ./deploy.sh --with-deps                  # also push package.json + npm install
#
# The build/ tree must already be populated (`npm run build`).
#
# Remote layout: /home/gellert/Gellert/ui-svelte/  (uisvelte.service)
###############################################################################
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD="$DIR/build"

TARGET="${TARGET:-qemu}"
HOST=""; PORT=""; WITH_DEPS=0
PASS="${SSH_PASS:-4gri*st4r}"

for arg in "$@"; do
  case $arg in
    --target=*)    TARGET="${arg#*=}" ;;
    --host=*)      HOST="${arg#*=}" ;;
    --port=*)      PORT="${arg#*=}" ;;
    --with-deps)   WITH_DEPS=1 ;;
    -h|--help)
      sed -n '1,18p' "$0" | grep -E '^#' | sed 's/^# \?//'
      exit 0
      ;;
    *) echo "Unknown arg: $arg" >&2; exit 2 ;;
  esac
done

# Resolve target presets
if [ -z "$HOST" ] || [ -z "$PORT" ]; then
  case "$TARGET" in
    qemu)
      HOST="${HOST:-localhost}"
      PORT="${PORT:-2222}"
      ;;
    production)
      HOST="${HOST:-10.47.27.108}"
      PORT="${PORT:-22}"
      ;;
    *)
      echo "Unknown --target=$TARGET (use qemu or production)" >&2
      exit 2
      ;;
  esac
fi

REMOTE="/home/gellert/Gellert/ui-svelte"
[ -d "$BUILD" ] || { echo "No build at $BUILD â€” run 'npm run build' first" >&2; exit 1; }

SSHOPTS=(
  -o StrictHostKeyChecking=no
  -o UserKnownHostsFile=/dev/null
  -o LogLevel=ERROR
  -o PreferredAuthentications=password
  -o PubkeyAuthentication=no
  -o ConnectTimeout=10
)

remote() { sshpass -p "$PASS" ssh "${SSHOPTS[@]}" -p "$PORT" "gellert@$HOST" "$@"; }

echo "[deploy] target=$TARGET  gellert@$HOST:$PORT  â†’  $REMOTE"

echo "[deploy] stopping uisvelte.service"
remote "sudo -n systemctl stop uisvelte.service"

echo "[deploy] rsync build/ â†’ $REMOTE"
# Use sudo on the remote side because the install dir is root-owned (gellert
# can write existing files via the gellert group, but can't create new files
# at the dir root). gellert has NOPASSWD sudo on Constellation images.
sshpass -p "$PASS" rsync -rlptDv --delete \
  --rsync-path="sudo -n rsync" \
  --chown=gellert:gellert \
  --chmod=Du=rwx,Dg=rwxs,Do=rx,Fu=rwx,Fg=rwx,Fo=rx \
  --exclude=node_modules \
  --exclude=package.json \
  --exclude=package-lock.json \
  -e "ssh ${SSHOPTS[*]} -p $PORT" \
  "$BUILD/" "gellert@$HOST:$REMOTE/"

if [ "$WITH_DEPS" -eq 1 ]; then
  echo "[deploy] uploading package.json + reinstalling production deps"
  sshpass -p "$PASS" scp "${SSHOPTS[@]}" -P "$PORT" \
    "$DIR/package.json" "gellert@$HOST:$REMOTE/package.json"
  remote "cd $REMOTE && npm install --omit=dev --no-audit --no-fund 2>&1 | tail -10"
fi

echo "[deploy] starting uisvelte.service"
remote "sudo -n systemctl start uisvelte.service"
sleep 2
remote "systemctl --no-pager status uisvelte.service | head -10"

echo "[deploy] done"
