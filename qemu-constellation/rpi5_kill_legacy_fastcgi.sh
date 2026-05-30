#!/bin/bash
set -e
CONF=/etc/lighttpd/lighttpd.conf
TS=$(date +%Y%m%d_%H%M%S)

echo "=== backup ==="
sudo cp $CONF $CONF.bak.$TS
echo "backup: $CONF.bak.$TS"

echo
echo "=== rewriting lighttpd.conf — strip mod_fastcgi + fastcgi.server + cgi/get rule ==="
sudo python3 <<'PY'
import re
p = '/etc/lighttpd/lighttpd.conf'
s = open(p).read()

# 1) drop "mod_fastcgi" from server.modules list (handle trailing comma)
s = re.sub(r'^\s*"mod_fastcgi"\s*,?\s*\n', '', s, flags=re.M)

# 2) drop the fastcgi.server = ( ... ) block — match balanced parens conservatively
def strip_block(text, start_pat):
    m = re.search(start_pat, text)
    if not m:
        return text
    i = m.start()
    # find first '(' after key
    j = text.find('(', m.end())
    if j < 0: return text
    depth = 0
    k = j
    while k < len(text):
        c = text[k]
        if c == '(': depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0:
                # consume trailing whitespace + newline
                end = k + 1
                while end < len(text) and text[end] in ' \t': end += 1
                if end < len(text) and text[end] == '\n': end += 1
                return text[:i] + text[end:]
        k += 1
    return text

s = strip_block(s, r'fastcgi\.server\s*=')

# 3) drop the $HTTP["url"] =~ "^/(cgi|get)..." block
s = strip_block(s, r'\$HTTP\["url"\]\s*=~\s*"\^/\(cgi\|get\)')

open(p, 'w').write(s)
print("rewrite ok")
PY

echo
echo "=== validate ==="
sudo lighttpd -tt -f $CONF

echo
echo "=== restart lighttpd ==="
sudo systemctl restart lighttpd
sleep 1
systemctl is-active lighttpd

echo
echo "=== kill any lingering gellertserverd ==="
sudo pkill -f gellertserverd 2>/dev/null || echo "no process to kill (good)"
sleep 1
pgrep -af gellertserverd || echo "confirmed: no gellertserverd running"

echo
echo "=== restart bridge so it gets a clean UART ==="
sudo systemctl restart agristar-bridge
sleep 6

echo
echo "=== UART ownership (should be ONE process) ==="
sudo lsof /dev/ttyAMA0 2>/dev/null || echo "no holder?"

echo
echo "=== bridge health (after 6s) ==="
curl -s http://localhost:9001/health | python3 -m json.tool

echo
echo "=== last 15 bridge log lines ==="
sudo journalctl -u agristar-bridge --no-pager -n 15
