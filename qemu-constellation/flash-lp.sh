#!/usr/bin/env bash
# flash-lp.sh — Reflash the LP-AM2434 from the RPi5 over /dev/ttyAMA0.
#
# Workflow:
#   1. Stops agristar-bridge to free /dev/ttyAMA0.
#   2. Prompts you to set SW4 to UART-boot (1110 0000) and power-cycle the LP.
#   3. Runs uart_uniflash.py with flash_nova_lp_pi.cfg.
#   4. Prompts you to set SW4 back to OSPI-boot (0100 0100) and power-cycle.
#   5. Restarts agristar-bridge.
#
# Files expected in this directory (~/lp_flash):
#   uart_uniflash.py
#   flash_nova_lp_pi.cfg
#   nova_lp.release.mcelf.hs_fs
#   sbl_prebuilt/am243x-lp/sbl_uart_uniflash.release.hs_fs.tiimage
#   sbl_prebuilt/am243x-lp/sbl_ospi.release.hs_fs.tiimage

set -euo pipefail
cd "$(dirname "$0")"

PORT="${PORT:-/dev/ttyAMA0}"
CFG="${CFG:-flash_nova_lp_pi.cfg}"

confirm() {
    local prompt="$1"
    read -r -p "$prompt [press Enter to continue, Ctrl-C to abort] "
}

echo "=== Stopping agristar-bridge to release ${PORT} ==="
sudo systemctl stop agristar-bridge

echo
echo ">>> Set SW4 to UART-boot mode (1110 0000) and power-cycle the LP-AM2434 <<<"
confirm "Ready?"

echo
echo "=== Running uart_uniflash.py ==="
python3 uart_uniflash.py -p "$PORT" --cfg "$CFG"
RC=$?

echo
if [ $RC -eq 0 ]; then
    echo "=== Flash succeeded ==="
else
    echo "!!! Flash failed (exit $RC) !!!"
fi

echo
echo ">>> Set SW4 back to OSPI-boot mode (0100 0100) and power-cycle the LP-AM2434 <<<"
confirm "Ready?"

echo
echo "=== Restarting agristar-bridge ==="
sudo systemctl start agristar-bridge
sleep 2
systemctl is-active agristar-bridge

echo
echo "=== Tailing journal (Ctrl-C to exit) ==="
journalctl -u agristar-bridge -n 30 --no-pager
exit $RC
