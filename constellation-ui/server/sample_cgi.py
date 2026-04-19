import urllib.request, time

for i in range(20):
    try:
        r = urllib.request.urlopen('http://localhost/get/data?page=main', timeout=3)
        body = r.read().decode()
        has_main = 'MainData' in body
        print(f'Sample {i+1}: {len(body)} chars, MainData={has_main}')
    except Exception as e:
        print(f'Sample {i+1}: ERROR {e}')
    time.sleep(1)
