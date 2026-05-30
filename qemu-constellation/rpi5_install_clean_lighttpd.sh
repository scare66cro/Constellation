#!/bin/bash
# Restore good lighttpd.conf, kill legacy gellertserverd, restart bridge,
# verify exclusive UART ownership.
set -e

BAK=$(ls -t /etc/lighttpd/lighttpd.conf.bak.* | head -1)
echo "=== restoring from latest backup: $BAK ==="
sudo cp "$BAK" /etc/lighttpd/lighttpd.conf

echo
echo "=== installing clean config ==="
sudo cp /tmp/lighttpd_clean.conf /etc/lighttpd/lighttpd.conf

echo
echo "=== validate ==="
sudo lighttpd -tt -f /etc/lighttpd/lighttpd.conf

echo
echo "=== restart lighttpd ==="
sudo systemctl restart lighttpd
sleep 1
systemctl is-active lighttpd

echo
echo "=== kill legacy gellertserverd ==="
sudo pkill -9 -f gellertserverd 2>/dev/null || echo "(none running)"
sleep 2
if pgrep -af gellertserverd; then
  echo "ERROR: still running!"
  exit 1
else
  echo "OK: no gellertserverd"
fi

echo
echo "=== restart bridge for clean UART ==="
sudo systemctl restart agristar-bridge
sleep 8

echo
echo "=== UART owners (should be ONLY node) ==="
sudo lsof /dev/ttyAMA0 2>/dev/null | awk 'NR==1 || /CHR/ {print}'

echo
echo "=== bridge health ==="
curl -s http://localhost:9001/health | python3 -m json.tool

echo
echo "=== last 8 bridge log lines ==="
sudo journalctl -u agristar-bridge --no-pager -n 8

echo
echo "=== HTTP smoke through lighttpd ==="
curl -s -o /dev/null -w "/ : %{http_code}\n" http://localhost/
curl -s -o /dev/null -w "/iot/sensors/unified : %{http_code}\n" http://localhost/iot/sensors/unified
curl -s -o /dev/null -w "/proto/snapshot : %{http_code}\n" http://localhost/proto/snapshot
