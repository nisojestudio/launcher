import base64, binascii

key_part = 'NmI2ZjdhNmQ1MWEyNGIwMDg3MGJiZDNkMzQyYzk1MGNhZWYxY2Y4MmU4YzVjNDQ4YjExOWRm'
print(f'Length: {len(key_part)} chars (hex = {len(key_part)//2} bytes)')

# Try as hex
try:
    decoded = bytes.fromhex(key_part)
    print(f'Hex decode: {decoded}')
    print(f'As int: {int.from_bytes(decoded, "big")}')
except Exception as e:
    print(f'Not valid hex: {e}')

# Try as base64
try:
    padded = key_part + '=' * (4 - len(key_part) % 4)
    decoded = base64.urlsafe_b64decode(padded)
    print(f'Base64 decode: {decoded}')
except Exception as e:
    print(f'Not valid base64: {e}')