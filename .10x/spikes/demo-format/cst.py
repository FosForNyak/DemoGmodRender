import struct, sys
sys.path.insert(0, sys.argv[0].rsplit('/', 1)[0].rsplit('\\', 1)[0])
from bits import *
data = open(sys.argv[1], 'rb').read()
# packet 2
pos = 1072; n = 0
while True:
    cmd = data[pos]; pos += 5
    pos += 84; ln = struct.unpack_from('<i', data, pos)[0]; pos += 4
    n += 1
    if n == 2: break
    pos += ln
b = BitReader(data, pos, pos + ln)
print(b.ubit(6), b.ubit(32), b.ubit(16), b.ubit(16))
print('msg', b.ubit(6))
print('name', b.string())
s = b.pos
print(''.join(str(b.bit()) for _ in range(120)))
t = BitReader(data, pos, pos + ln); t.pos = s
print('max16', t.ubit(16))
