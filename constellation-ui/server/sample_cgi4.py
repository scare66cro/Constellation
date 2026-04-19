import urllib.request, time, re

# Compare data?page=main vs GellertData.jsp
endpoints = [
    ('page=main', 'http://localhost/get/data?page=main'),
    ('GellertData', 'http://localhost/get/GellertData.jsp'),
]

for label, url in endpoints:
    print(f'\n===== {label}: {url} =====')
    for i in range(10):
        try:
            r = urllib.request.urlopen(url, timeout=3)
            body = r.read().decode()
            sz = len(body)
            var_names = re.findall(r'var\s+(\w+)\s*=', body)
            has_main = 'MainData' in body
            m = re.search(r'var MainData = "([^"]*)"', body)
            fan = None
            if m:
                fields = m.group(1).split(',')
                fan = fields[10] if len(fields) > 10 else '?'
            status = 'FULL' if has_main else 'TRUNC'
            print(f'  {i+1:2d} [{status}] {sz:5d}b  fan={fan}  nvars={len(var_names)}')
        except Exception as e:
            print(f'  {i+1:2d} ERROR {e}')
        time.sleep(1)
