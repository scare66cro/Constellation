import urllib.request, time, re
for i in range(10):
    try:
        data = urllib.request.urlopen('http://localhost/get/data?page=main', timeout=2).read().decode()
        m = re.search(r'var MainData = "(.*?)"', data)
        if m:
            f = m.group(1).split(',')
            print(time.strftime('%H:%M:%S'), 'fan=' + (f[10] if len(f) > 10 else 'short'))
        else:
            print(time.strftime('%H:%M:%S'), 'no match')
    except Exception as e:
        print(time.strftime('%H:%M:%S'), str(e)[:60])
    time.sleep(2)
