import urllib.request, time, re

# Time how long it takes for MainData to reappear after being consumed
# Hit rapidly until we get MainData, note the time, then hit rapidly again

print('=== Measuring MainData refresh cycle ===')
last_main_time = None
cycle_times = []

for i in range(100):
    t = time.time()
    try:
        r = urllib.request.urlopen('http://localhost/get/GellertData.jsp', timeout=3)
        body = r.read().decode()
        has_main = 'MainData' in body
        
        if has_main:
            m = re.search(r'var MainData = "([^"]*)"', body)
            fan = None
            if m:
                fields = m.group(1).split(',')
                fan = fields[10] if len(fields) > 10 else '?'
            
            if last_main_time is not None:
                gap = t - last_main_time
                cycle_times.append(gap)
                print(f'{i+1:3d} t={t:.2f} HAS_MAIN fan={fan} gap={gap:.3f}s')
            else:
                print(f'{i+1:3d} t={t:.2f} HAS_MAIN fan={fan} (first)')
            last_main_time = t
    except Exception as e:
        print(f'{i+1:3d} ERROR {e}')
    time.sleep(0.05)  # 50ms between requests for tight measurement

if cycle_times:
    print(f'\nMainData refresh cycle: min={min(cycle_times):.3f}s max={max(cycle_times):.3f}s avg={sum(cycle_times)/len(cycle_times):.3f}s')
    print(f'Samples: {len(cycle_times)}')
