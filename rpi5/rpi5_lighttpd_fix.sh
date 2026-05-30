#!/bin/bash
set -e
CONF=/etc/lighttpd/lighttpd.conf
TS=$(date +%Y%m%d_%H%M%S)
echo "=== backing up to .backup.$TS ==="
sudo cp $CONF $CONF.backup.$TS
echo "=== before ==="
sudo grep -n 'port" => 2035\|port" => 9001\|port" => 3000' $CONF
echo
echo "=== rewriting iot proxy port 2035 -> 9001 ==="
sudo sed -i 's/"port" => 2035/"port" => 9001/' $CONF
echo "=== after ==="
sudo grep -n 'port" => 2035\|port" => 9001\|port" => 3000' $CONF
echo
echo "=== validate ==="
sudo lighttpd -t -f $CONF 2>&1
echo
echo "=== restart + enable ==="
sudo systemctl restart lighttpd
sudo systemctl enable lighttpd 2>&1 | tail -3
sleep 2
sudo systemctl is-active lighttpd
sudo systemctl is-enabled lighttpd
echo
echo "=== smoke test from Pi5 ==="
curl -sI http://localhost:80/ | head -3
echo "--- /iot/sensors/unified ---"
curl -sI http://localhost:80/iot/sensors/unified | head -3
echo "--- /level2/orbit-sensors ---"
curl -sI http://localhost:80/level2/orbit-sensors | head -3
echo "--- payload sample ---"
curl -s http://localhost:80/iot/orbits/sensor-banks | python3 -c "import sys,json
d=json.load(sys.stdin); print('banks:', len(d.get('banks',[])))"
