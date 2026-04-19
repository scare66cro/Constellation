import urllib.request, time, sys

# Capture detailed truncated vs full responses with variable lists
for i in range(30):
    try:
        r = urllib.request.urlopen('http://localhost/get/data?page=main', timeout=3)
        body = r.read().decode()
        sz = len(body)
        
        # Extract all var names
        import re
        var_names = re.findall(r'var\s+(\w+)\s*=', body)
        has_main = 'MainData' in body
        has_network = 'NetworkMonitor' in body
        has_current_mode = 'CurrentMode' in body
        
        # Extract MainData[10] if present
        m = re.search(r'var MainData = "([^"]*)"', body)
        fan = None
        if m:
            fields = m.group(1).split(',')
            fan = fields[10] if len(fields) > 10 else '?'
        
        # Extract CurrentMode
        cm = re.search(r'var CurrentMode = "([^"]*)"', body)
        mode = cm.group(1) if cm else 'N/A'
        
        status = 'FULL' if has_main else 'TRUNC'
        print(f'{i+1:2d} [{status}] {sz:5d}b  mode={mode}  fan={fan}  vars={var_names}')
        
        # Print full body for first truncated response
        if not has_main and i < 15:
            print(f'    --- RAW BODY ---')
            for line in body.split('\n'):
                print(f'    | {line}')
            print(f'    --- END RAW ---')
    except Exception as e:
        print(f'{i+1:2d} ERROR {e}')
    time.sleep(1)
