import struct, sys
data = open(sys.argv[1], 'rb').read()
want = int(sys.argv[2])
pos = 1072; n = 0
while True:
    cmd = data[pos]; tick = struct.unpack_from('<i', data, pos + 1)[0]; start = pos; pos += 5
    if cmd in (1, 2):
        info = data[pos:pos + 84]
        pos += 84; ln = struct.unpack_from('<i', data, pos)[0]; pos += 4
        n += 1
        if n == want: break
        pos += ln
    elif cmd == 3: pass
    elif cmd in (4, 6, 8): l2 = struct.unpack_from('<i', data, pos)[0]; pos += 4 + l2
    elif cmd == 5: pos += 4; l2 = struct.unpack_from('<i', data, pos)[0]; pos += 4 + l2
print('packet', n, 'cmd', cmd, 'tick', tick, 'len', ln, 'cmd at', start)
print('cmdinfo', info.hex())
pk = data[pos:pos + ln]
for i in range(0, min(len(pk), 256), 32):
    chunk = pk[i:i + 32]
    print(f'{i:4d}', chunk.hex(), ''.join(chr(c) if 32 <= c < 127 else '.' for c in chunk))
