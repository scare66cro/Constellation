#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────
# image_pi5.sh — clean snapshot of the Pi5 bridge NVMe into the project.
#
#   Stops the bridge services, syncs, images the USED span of the NVMe
#   (partition table + all 3 partitions; skips the empty ~90 GB tail),
#   compresses on the Pi, streams over SSH to backups/, then restarts
#   the services. The compressed .img.gz is gitignored (won't hit GitHub).
#
# USAGE:
#   1) Put your Pi sudo password in a file OUTSIDE the repo, e.g.:
#        printf '%s' 'YOUR_SUDO_PW' > "$HOME/.pi_sudo_pw" && chmod 600 "$HOME/.pi_sudo_pw"
#   2) Run:
#        bash image_pi5.sh "$HOME/.pi_sudo_pw"
#   3) Tell Claude when it finishes — it'll checksum + write/commit the manifest.
#
# Notes:
#   - SSH login is key-based (no password needed for login); the password
#     is only piped to `sudo -S` on the Pi, read from your file at runtime.
#   - Override the imaged span with COUNT (4 MiB blocks): COUNT=9000 bash ...
# ─────────────────────────────────────────────────────────────────────────
set -euo pipefail

PWFILE="${1:?usage: bash image_pi5.sh <path-to-sudo-password-file>}"
[[ -r "$PWFILE" ]] || { echo "password file not readable: $PWFILE" >&2; exit 1; }
PW="$(cat "$PWFILE")"

HOST="gellert@10.47.27.108"
DEV="/dev/nvme0n1"
COUNT="${COUNT:-8192}"                 # 4 MiB * 8192 = 32 GiB (covers the ~29 GiB of partitions)
TS="$(date +%Y%m%d-%H%M)"
OUT="/f/Constellation/backups/pi5-nvme-${TS}.img.gz"
SSH=(ssh -o BatchMode=yes -o ConnectTimeout=10 "$HOST")

echo ">> [1/5] choosing compressor on the Pi..."
COMP="$("${SSH[@]}" 'command -v pigz || echo gzip')"
echo "         using: $COMP -1"

echo ">> [2/5] stopping bridge services + sync..."
"${SSH[@]}" 'sudo -S systemctl stop agristar-bridge uisvelte 2>/dev/null; sync' <<< "$PW"

echo ">> [3/5] imaging $DEV  (first $((COUNT * 4 / 1024)) GiB)  ->  $OUT"
echo "         (progress shows below; this is the long step)"
"${SSH[@]}" "sudo -S dd if=$DEV bs=4M count=$COUNT status=progress | $COMP -1" <<< "$PW" > "$OUT"

echo ">> [4/5] restarting bridge services..."
"${SSH[@]}" 'sudo -S systemctl start agristar-bridge uisvelte' <<< "$PW"

echo ">> [5/5] done."
ls -lh "$OUT"
echo ">> verify the bridge is back:  bash _get_bridge_log.sh"
echo ">> then tell Claude the file name so it can checksum + commit the manifest."
