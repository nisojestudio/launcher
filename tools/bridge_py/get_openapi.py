import urllib.request, json, re

key = 'euler_NmI2ZjdhNmQ1MWEyNGIwMDg3MGJiZDNkMzQyYzk1MGNhZWYxY2Y4MmU4YzVjNDQ4YjExOWRm'
url = 'https://api.eulerstream.com/docs/openapi.json'
headers = {
    'Authorization': f'Bearer {key}',
    'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36',
}
req = urllib.request.Request(url, headers=headers, method='GET')
try:
    with urllib.request.urlopen(req, timeout=10) as resp:
        print(f'Status: {resp.status}')
        content = resp.read().decode()
        if 'paths' in content:
            paths = re.findall(r'"(/[^"]+)"', content[:10000])
            print(f'Found paths: {paths[:50]}')
        else:
            print(f'Content: {content[:500]}')
except urllib.error.HTTPError as e:
    print(f'HTTP Error: {e.code}')
    print(f'Response: {e.read().decode()[:500]}')
except Exception as e:
    print(f'Error: {e}')