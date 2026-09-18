import urllib.request, json, re

urls = [
    'https://api.eulerstream.com/openapi.json',
    'https://api.eulerstream.com/docs/openapi.json',
    'https://api.eulerstream.com/v1/openapi.json',
    'https://api.eulerstream.com/api-docs',
    'https://www.eulerstream.com/openapi.json',
    'https://www.eulerstream.com/docs/openapi.json',
]

for url in urls:
    headers = {'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36'}
    req = urllib.request.Request(url, headers=headers, method='GET')
    try:
        with urllib.request.urlopen(req, timeout=5) as resp:
            print(f'GET {url}: {resp.status}')
            content = resp.read().decode()
            if 'paths' in content:
                paths = re.findall(r'"(/[^"]+)"', content[:5000])
                print(f'  Found paths: {paths[:30]}')
            else:
                print(f'  Content: {content[:300]}')
    except urllib.error.HTTPError as e:
        print(f'GET {url}: {e.code}')
    except Exception as e:
        print(f'GET {url}: Error - {e}')