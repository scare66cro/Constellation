import urllib.request, time, re
for i in range(15):
    try:
        data = urllib.request.urlopen('http://localhost/get/data?page=main', timeout=2).read().decode()
        # find MainData line
        for line in data.split('\n'):
            if 'MainData' in line:
                print(time.strftime('%H:%M:%S'), line.strip()[:200])
                break
    except Exception as e:
        print(time.strftime('%H:%M:%S'), 'ERROR', str(e)[:60])
    time.sleep(2)
