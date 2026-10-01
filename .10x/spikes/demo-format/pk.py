import struct, sys
sys.path.insert(0, sys.argv[0].rsplit('\\', 1)[0])
from bits import *
import msgs
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
print('packet', n, 'cmd', cmd, 'tick', tick, 'len', ln)
b = BitReader(data, pos, pos + ln)
info = {}
while b.left() >= 6:
    at = b.pos - pos * 8
    t = b.ubit(6)
    print(' msg', t, 'at', at)
    if t == 2:
        tid = b.ubit(32); fn = b.string(); req = b.bit(); print('   file', tid, repr(fn), req); continue
    try:
        msgs.parse_msg(b, t, info)
    except Exception as e:
        print('   ERR', e); break
print('left', b.left())
print('next 80 bits:', ''.join(str(b.bit()) for _ in range(min(80, b.left()))))
