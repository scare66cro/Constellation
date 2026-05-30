#!/bin/bash
set -e
echo "=== backup current ui-svelte (excluding node_modules) ==="
cd /home/gellert/Gellert/ui-svelte
sudo systemctl stop uisvelte
echo "=== overwrite with new build/ ==="
ls /tmp/ui-build/
cp -rfv /tmp/ui-build/client /tmp/ui-build/server /tmp/ui-build/env.js /tmp/ui-build/handler.js /tmp/ui-build/index.js /tmp/ui-build/shims.js .
echo "=== restart ==="
sudo systemctl start uisvelte
sleep 3
sudo systemctl is-active uisvelte
echo "=== smoke test (HEAD /level2/orbit-sensors) ==="
curl -sI http://localhost:3000/level2/orbit-sensors | head -3
