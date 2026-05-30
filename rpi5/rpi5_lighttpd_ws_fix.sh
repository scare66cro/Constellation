#!/bin/bash
set -e
CONF=/etc/lighttpd/lighttpd.conf
TS=$(date +%Y%m%d_%H%M%S)
sudo cp $CONF $CONF.backup.$TS
echo "=== before ==="
sudo grep -nA 6 'HTTP\["url"\] =~ "\^/(iot|gt)"' $CONF
echo
# Add proxy.header upgrade=enable + proto/stream support to the iot/gt block.
# Use python for a precise multi-line rewrite (sed with multi-line is fragile).
sudo python3 <<'PY'
import re
p = '/etc/lighttpd/lighttpd.conf'
s = open(p).read()
# 1) Replace existing iot|gt block to:
#    a) include /proto in the regex (UI uses /proto/stream WS + /proto/snapshot)
#    b) enable WebSocket upgrade
new_block = '''$HTTP["url"] =~ "^/(iot|gt|proto)" {
  proxy.server = (
    "" => ( (
      "host" => "127.0.0.1",
      "port" => 9001
    ) )
  )
  proxy.header = ( "upgrade" => "enable" )
}'''
s2 = re.sub(
    r'\$HTTP\["url"\] =~ "\^/\(iot\|gt\)" \{[^}]*\}',
    new_block,
    s,
    count=1,
    flags=re.DOTALL,
)
# 2) Update the catch-all !~ exclusion list to include proto (so / still routes
#    to the svelte UI on :3000 and proto/* doesn't accidentally match).
s2 = s2.replace(
    '$HTTP["url"] !~ "^/(iot|gt|cgi|get)"',
    '$HTTP["url"] !~ "^/(iot|gt|proto|cgi|get)"',
)
open(p,'w').write(s2)
print('rewritten')
PY
echo
echo "=== after ==="
sudo grep -nA 8 'HTTP\["url"\] =~ "\^/(iot|gt|proto)"' $CONF
sudo grep -n 'proto|cgi' $CONF
echo
echo "=== validate ==="
sudo lighttpd -t -f $CONF
echo
echo "=== restart ==="
sudo systemctl restart lighttpd
sleep 2
sudo systemctl is-active lighttpd
echo
echo "=== WS handshake through :80 (expect 101) ==="
curl -v -i -N -H "Connection: Upgrade" -H "Upgrade: websocket" -H "Sec-WebSocket-Version: 13" -H "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==" --max-time 3 http://localhost:80/iot/ws 2>&1 | grep -E 'HTTP/|Upgrade|Connection|Sec-WebSocket'
echo
echo "=== /proto/stream handshake through :80 ==="
curl -v -i -N -H "Connection: Upgrade" -H "Upgrade: websocket" -H "Sec-WebSocket-Version: 13" -H "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==" --max-time 3 http://localhost:80/proto/stream 2>&1 | grep -E 'HTTP/|Upgrade|Connection|Sec-WebSocket'
echo
echo "=== regression: /iot/sensors/unified still works ==="
curl -sI http://localhost:80/iot/sensors/unified | head -3
echo "=== regression: / still works ==="
curl -sI http://localhost:80/ | head -3
