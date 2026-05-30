#!/bin/bash
# Poll the UI through QEMU's :8181 forward until uisvelte responds.
for i in $(seq 1 90); do
  CODE=$(curl -sS -o /dev/null -w '%{http_code}' --max-time 3 http://localhost:8181/ 2>/dev/null || echo 000)
  if [ "$CODE" = "200" ]; then
    echo "UI_READY at ${i}s (HTTP $CODE)"
    BYTES=$(curl -sS http://localhost:8181/ | wc -c)
    echo "page_bytes=$BYTES"
    curl -sS http://localhost:8181/ | head -10
    exit 0
  fi
  sleep 1
done
echo "TIMEOUT (last code $CODE)"
exit 1
