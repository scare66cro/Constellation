import urllib.request, time, re

# Rapid-fire test: hit the CGI as fast as possible to see the consumption pattern
print('=== RAPID FIRE (0.1s intervals, 20 samples) ===')
for i in range(20):
    try:
        r = urllib.request.urlopen('http://localhost/get/GellertData.jsp', timeout=3)
        body = r.read().decode()
        sz = len(body)
        var_names = re.findall(r'var\s+(\w+)\s*=', body)
        has_main = 'MainData' in body
        m = re.search(r'var MainData = "([^"]*)"', body)
        fan = None
        if m:
            fields = m.group(1).split(',')
            fan = fields[10] if len(fields) > 10 else '?'
        
        # Also extract RequestCount
        rc = re.search(r'var RequestCount = "([^"]*)"', body)
        rc_val = rc.group(1) if rc else '?'
        
        dl = re.search(r'var Data_Loaded = "([^"]*)"', body)
        dl_val = dl.group(1) if dl else '?'
        
        status = 'HAS_MAIN' if has_main else 'NO_MAIN'
        print(f'{i+1:2d} [{status}] {sz:5d}b RC={rc_val} DL="{dl_val}" nvars={len(var_names)} vars={[v for v in var_names if v not in ("SessionID","RequestCount","LogCounter","BoardType","pgmLevel","Data_Loaded","ClientIpAdd","LogTotal","AlarmData","NetworkMonitor","GFSFileInfo")]}')
    except Exception as e:
        print(f'{i+1:2d} ERROR {e}')
    time.sleep(0.1)

print()
print('=== SLOW FIRE (3s intervals, 10 samples) ===')
for i in range(10):
    try:
        r = urllib.request.urlopen('http://localhost/get/GellertData.jsp', timeout=3)
        body = r.read().decode()
        sz = len(body)
        var_names = re.findall(r'var\s+(\w+)\s*=', body)
        has_main = 'MainData' in body
        m = re.search(r'var MainData = "([^"]*)"', body)
        fan = None
        if m:
            fields = m.group(1).split(',')
            fan = fields[10] if len(fields) > 10 else '?'
        rc = re.search(r'var RequestCount = "([^"]*)"', body)
        rc_val = rc.group(1) if rc else '?'
        dl = re.search(r'var Data_Loaded = "([^"]*)"', body)
        dl_val = dl.group(1) if dl else '?'
        status = 'HAS_MAIN' if has_main else 'NO_MAIN'
        print(f'{i+1:2d} [{status}] {sz:5d}b RC={rc_val} DL="{dl_val}" nvars={len(var_names)} vars={[v for v in var_names if v not in ("SessionID","RequestCount","LogCounter","BoardType","pgmLevel","Data_Loaded","ClientIpAdd","LogTotal","AlarmData","NetworkMonitor","GFSFileInfo")]}')
    except Exception as e:
        print(f'{i+1:2d} ERROR {e}')
    time.sleep(3)
