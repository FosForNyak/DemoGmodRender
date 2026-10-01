import struct, sys
sys.path.insert(0, sys.argv[0].rsplit('\\', 1)[0])
from bits import *
data = open(sys.argv[1], 'rb').read()
want = int(sys.argv[2])
pos = 1072; n = 0
while True:
    cmd = data[pos]; tick = struct.unpack_from('<i', data, pos + 1)[0]; pos += 5
    if cmd in (1, 2):
        pos += 84; ln = struct.unpack_from('<i', data, pos)[0]; pos += 4
        n += 1
        if n == want: break
        pos += ln
    elif cmd == 3: pass
    elif cmd in (4, 6, 8): l2 = struct.unpack_from('<i', data, pos)[0]; pos += 4 + l2
    elif cmd == 5: pos += 4; l2 = struct.unpack_from('<i', data, pos)[0]; pos += 4 + l2
total = ln * 8
print('packet', n, 'bits', total)
for off in range(6, 120):
    for w in (8, 11, 12, 13, 16, 17, 18, 20, 24, 32):
        b = BitReader(data, pos, pos + ln); b.pos = pos * 8 + off
        try:
            v = b.ubit(w)
        except EOFError:
            continue
        end = off + w + v
        if total - 6 < end <= total:
            print(f'off={off} width={w} value={v} end={end} (bits) / bytes-as-len end={off + w + v * 8}')
        end8 = off + w + v * 8
        if total - 6 < end8 <= total:
            print(f'off={off} width={w} value={v} bytes -> end={end8}')
    b = BitReader(data, pos, pos + ln); b.pos = pos * 8 + off
    try:
        v = b.varint32(); end = b.pos - pos * 8 + v
        if total - 6 < end <= total: print(f'off={off} varint={v} end={end}')
    except EOFError:
        pass
