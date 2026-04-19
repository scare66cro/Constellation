import urllib.request, time

# Capture a few truncated vs full responses to compare
for i in range(10):
    try:
        r = urllib.request.urlopen('http://localhost/get/data?page=main', timeout=3)
        body = r.read().decode()
        has_main = 'MainData' in body
        sz = len(body)
        if sz < 600 or not has_main:
            print(f'=== TRUNCATED Sample {i+1}: {sz} chars ===')
            print(body[:800])
            print('--- END ---')
            break
    except Exception as e:
        print(f'Sample {i+1}: ERROR {e}')
    time.sleep(0.5)

print()
# Also get a full one
for i in range(10):
    try:
        r = urllib.request.urlopen('http://localhost/get/data?page=main', timeout=3)
        body = r.read().decode()
        has_main = 'MainData' in body
        sz = len(body)
        if has_main and sz > 900:
            print(f'=== FULL Sample: {sz} chars ===')
            print(body[:2000])
            print('--- END ---')
            break
    except Exception as e:
        pass
    time.sleep(0.5)
